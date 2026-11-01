#include "rolloffesp_wifi_web.h"
#include "rolloffesp_hardware.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sdkconfig.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "mdns.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "lwip/inet.h"

#define CONFIG_NAMESPACE "rolloffesp"
#define WIFI_SSID_KEY "wifi_ssid"
#define WIFI_PASSWORD_KEY "wifi_password"
#define PULSE_DURATION_KEY "pulse_ms"
#define HTTP_BODY_MAX 512
#define WIFI_RETRY_LIMIT 5

static const char *TAG = "rolloffesp_net";
static esp_netif_t *s_ap_netif;
static esp_netif_t *s_sta_netif;
static httpd_handle_t s_http_server;
static bool s_setup_ap_active;
static bool s_station_connected;
static bool s_connection_pending;
static uint8_t s_station_retries;
static char s_candidate_ssid[33];
static char s_candidate_password[64];
static char s_connection_message[48] = "Setup hotspot ready";

static esp_err_t read_string(nvs_handle_t nvs, const char *key, char *buffer, size_t size)
{
    size_t required = size;
    const esp_err_t err = nvs_get_str(nvs, key, buffer, &required);
    if (err == ESP_OK && (required == 0 || required > size)) {
        return ESP_ERR_INVALID_SIZE;
    }
    return err;
}

static esp_err_t save_wifi_credentials(const char *ssid, const char *password)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(CONFIG_NAMESPACE, NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_set_str(nvs, WIFI_SSID_KEY, ssid);
    if (err == ESP_OK) {
        err = nvs_set_str(nvs, WIFI_PASSWORD_KEY, password);
    }
    if (err == ESP_OK) {
        err = nvs_commit(nvs);
    }
    nvs_close(nvs);
    return err;
}

static esp_err_t save_pulse_duration(uint32_t duration_ms)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(CONFIG_NAMESPACE, NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_set_u32(nvs, PULSE_DURATION_KEY, duration_ms);
    if (err == ESP_OK) {
        err = nvs_commit(nvs);
    }
    nvs_close(nvs);
    return err;
}

static esp_err_t load_configuration(char *ssid, size_t ssid_size,
                                    char *password, size_t password_size,
                                    bool *has_credentials, uint32_t *pulse_ms)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(CONFIG_NAMESPACE, NVS_READONLY, &nvs);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        *has_credentials = false;
        *pulse_ms = CONFIG_ROLLOFFESP_RELAY_PULSE_MS;
        return ESP_OK;
    }
    if (err != ESP_OK) {
        return err;
    }

    esp_err_t ssid_err = read_string(nvs, WIFI_SSID_KEY, ssid, ssid_size);
    esp_err_t password_err = read_string(nvs, WIFI_PASSWORD_KEY, password, password_size);
    *has_credentials = ssid_err == ESP_OK && password_err == ESP_OK &&
                       ssid[0] != '\0' && strlen(password) >= 8;
    if (ssid_err != ESP_OK && ssid_err != ESP_ERR_NVS_NOT_FOUND) {
        err = ssid_err;
    } else if (password_err != ESP_OK && password_err != ESP_ERR_NVS_NOT_FOUND) {
        err = password_err;
    }

    uint32_t stored_pulse_ms = CONFIG_ROLLOFFESP_RELAY_PULSE_MS;
    esp_err_t pulse_err = nvs_get_u32(nvs, PULSE_DURATION_KEY, &stored_pulse_ms);
    if (pulse_err != ESP_OK && pulse_err != ESP_ERR_NVS_NOT_FOUND) {
        err = pulse_err;
    }
    if (stored_pulse_ms < 100 || stored_pulse_ms > CONFIG_ROLLOFFESP_RELAY_PULSE_MAX_MS) {
        ESP_LOGW(TAG, "Stored relay pulse duration is invalid; using configured default");
        stored_pulse_ms = CONFIG_ROLLOFFESP_RELAY_PULSE_MS;
    }
    *pulse_ms = stored_pulse_ms;
    nvs_close(nvs);
    return err;
}

static esp_err_t decode_form_value(const char *input, size_t input_len,
                                   char *output, size_t output_size, size_t *output_len)
{
    size_t out = 0;
    for (size_t i = 0; i < input_len; i++) {
        unsigned char value = (unsigned char)input[i];
        if (value == '+') {
            value = ' ';
        } else if (value == '%') {
            if (i + 2 >= input_len ||
                !isxdigit((unsigned char)input[i + 1]) ||
                !isxdigit((unsigned char)input[i + 2])) {
                return ESP_ERR_INVALID_ARG;
            }
            const char hex[3] = {input[i + 1], input[i + 2], '\0'};
            value = (unsigned char)strtoul(hex, NULL, 16);
            i += 2;
        }
        if (value == '\0' || value < 0x20 || value == 0x7f || out + 1 >= output_size) {
            return ESP_ERR_INVALID_ARG;
        }
        output[out++] = (char)value;
    }
    output[out] = '\0';
    *output_len = out;
    return ESP_OK;
}

static esp_err_t get_form_field(const char *body, size_t body_len, const char *name,
                                char *output, size_t output_size, size_t *output_len)
{
    const size_t name_len = strlen(name);
    size_t offset = 0;
    while (offset < body_len) {
        size_t end = offset;
        while (end < body_len && body[end] != '&') {
            end++;
        }
        size_t equals = offset;
        while (equals < end && body[equals] != '=') {
            equals++;
        }
        if (equals - offset == name_len &&
            memcmp(body + offset, name, name_len) == 0 &&
            equals < end) {
            return decode_form_value(body + equals + 1, end - equals - 1,
                                     output, output_size, output_len);
        }
        offset = end + 1;
    }
    return ESP_ERR_NOT_FOUND;
}

static esp_err_t receive_body(httpd_req_t *request, char *body, size_t capacity)
{
    if (request->content_len <= 0 || (size_t)request->content_len >= capacity ||
        request->content_len > HTTP_BODY_MAX) {
        return ESP_ERR_INVALID_SIZE;
    }
    size_t received = 0;
    while (received < (size_t)request->content_len) {
        const int ret = httpd_req_recv(request, body + received,
                                       request->content_len - received);
        if (ret <= 0) {
            return ESP_FAIL;
        }
        received += (size_t)ret;
    }
    body[received] = '\0';
    return ESP_OK;
}

static esp_err_t send_page(httpd_req_t *request, const char *message)
{
    const uint32_t pulse_ms = rolloffesp_get_default_pulse_ms();
    char page[1536];
    char station_ip[16] = "not connected";
    char setup_ip[16] = "192.168.4.1";
    esp_netif_ip_info_t ip_info;
    if (s_station_connected && esp_netif_get_ip_info(s_sta_netif, &ip_info) == ESP_OK) {
        snprintf(station_ip, sizeof(station_ip), IPSTR, IP2STR(&ip_info.ip));
    }
    if (esp_netif_get_ip_info(s_ap_netif, &ip_info) == ESP_OK) {
        snprintf(setup_ip, sizeof(setup_ip), IPSTR, IP2STR(&ip_info.ip));
    }
    const int length = snprintf(page, sizeof(page),
        "<!doctype html><html><head><meta name=\"viewport\" content=\"width=device-width,"
        "initial-scale=1\">%s<title>RolloffESP setup</title></head><body>"
        "<h1>RolloffESP</h1><p>%s</p><p>Wi-Fi: %s</p>"
        "<p>Setup address: http://%s</p><p>Home-network address: %s</p>"
        "<form method=\"post\" action=\"/pulse\"><label>Relay pulse (100-%u ms): "
        "<input name=\"duration_ms\" type=\"number\" min=\"100\" max=\"%u\" value=\"%lu\" "
        "required></label><button type=\"submit\">Save pulse duration</button></form>"
        "%s"
        "<p>Use a trusted local network. Setup access point: ROLLOFFESP.</p></body></html>",
        s_connection_pending ? "<meta http-equiv=\"refresh\" content=\"5\">" : "",
        message, s_station_connected ? "connected" : "not connected",
        setup_ip, station_ip,
        CONFIG_ROLLOFFESP_RELAY_PULSE_MAX_MS, CONFIG_ROLLOFFESP_RELAY_PULSE_MAX_MS,
        (unsigned long)pulse_ms,
        s_setup_ap_active
            ? "<form method=\"post\" action=\"/wifi\"><h2>Home Wi-Fi</h2>"
              "<label>Network name (SSID): <input name=\"ssid\" maxlength=\"32\" required></label>"
              "<label>Password: <input name=\"password\" type=\"password\" minlength=\"8\" "
              "maxlength=\"63\" required></label><button type=\"submit\">Connect</button></form>"
            : "<p>Wi-Fi credentials can be changed when the setup hotspot is active.</p>");
    if (length < 0 || (size_t)length >= sizeof(page)) {
        return ESP_FAIL;
    }
    httpd_resp_set_type(request, "text/html; charset=utf-8");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    return httpd_resp_send(request, page, length);
}

static esp_err_t root_get_handler(httpd_req_t *request)
{
    return send_page(request, s_connection_message);
}

static esp_err_t setup_get_handler(httpd_req_t *request)
{
    return send_page(request, s_connection_message);
}

static esp_err_t pulse_post_handler(httpd_req_t *request)
{
    char body[HTTP_BODY_MAX + 1];
    char value[16];
    size_t value_len;
    esp_err_t err = receive_body(request, body, sizeof(body));
    if (err == ESP_OK) {
        err = get_form_field(body, request->content_len, "duration_ms",
                             value, sizeof(value), &value_len);
    }
    if (err != ESP_OK || value_len == 0) {
        httpd_resp_set_status(request, "400 Bad Request");
        return httpd_resp_sendstr(request, "Invalid pulse duration.");
    }

    for (size_t i = 0; i < value_len; i++) {
        if (!isdigit((unsigned char)value[i])) {
            httpd_resp_set_status(request, "400 Bad Request");
            return httpd_resp_sendstr(request, "Pulse duration must be an integer.");
        }
    }
    const unsigned long duration_ms = strtoul(value, NULL, 10);
    if (duration_ms < 100 || duration_ms > CONFIG_ROLLOFFESP_RELAY_PULSE_MAX_MS) {
        httpd_resp_set_status(request, "400 Bad Request");
        return httpd_resp_sendstr(request, "Pulse duration is outside the allowed range.");
    }

    const uint32_t old_duration = rolloffesp_get_default_pulse_ms();
    err = rolloffesp_set_default_pulse_ms((uint32_t)duration_ms);
    if (err == ESP_OK) {
        err = save_pulse_duration((uint32_t)duration_ms);
    }
    if (err != ESP_OK) {
        rolloffesp_set_default_pulse_ms(old_duration);
        ESP_LOGE(TAG, "Failed to save pulse duration: %s", esp_err_to_name(err));
        httpd_resp_set_status(request, "500 Internal Server Error");
        return httpd_resp_sendstr(request, "Unable to save pulse duration.");
    }
    return send_page(request, "Relay pulse duration saved.");
}

static esp_err_t wifi_post_handler(httpd_req_t *request)
{
    if (!s_setup_ap_active) {
        ESP_LOGW(TAG, "Rejected Wi-Fi credential request because setup AP is inactive");
        httpd_resp_set_status(request, "403 Forbidden");
        return httpd_resp_sendstr(request,
            "Wi-Fi changes are allowed only from the setup hotspot. "
            "Connect to ROLLOFFESP and open http://192.168.4.1.");
    }

    char body[HTTP_BODY_MAX + 1];
    char ssid[33];
    char password[64];
    size_t ssid_len = 0;
    size_t password_len = 0;
    esp_err_t err = receive_body(request, body, sizeof(body));
    if (err == ESP_OK) {
        err = get_form_field(body, request->content_len, "ssid",
                             ssid, sizeof(ssid), &ssid_len);
    }
    if (err == ESP_OK) {
        err = get_form_field(body, request->content_len, "password",
                             password, sizeof(password), &password_len);
    }
    memset(body, 0, sizeof(body));
    if (err != ESP_OK || ssid_len == 0 || ssid_len > 32 ||
        password_len < 8 || password_len > 63) {
        memset(password, 0, sizeof(password));
        httpd_resp_set_status(request, "400 Bad Request");
        return httpd_resp_sendstr(request, "SSID or WPA2 password is invalid.");
    }

    memcpy(s_candidate_ssid, ssid, ssid_len + 1);
    memcpy(s_candidate_password, password, password_len + 1);
    memset(password, 0, sizeof(password));
    strlcpy(s_connection_message, "Connecting to the selected Wi-Fi network...",
            sizeof(s_connection_message));

    wifi_config_t station_config = {0};
    memcpy(station_config.sta.ssid, s_candidate_ssid, ssid_len);
    memcpy(station_config.sta.password, s_candidate_password, password_len);
    station_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    err = esp_wifi_set_mode(WIFI_MODE_APSTA);
    if (err == ESP_OK) {
        err = esp_wifi_set_config(WIFI_IF_STA, &station_config);
    }
    memset(&station_config, 0, sizeof(station_config));
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Starting connection attempt for SSID '%s'", s_candidate_ssid);
        s_station_retries = 0;
        s_station_connected = false;
        s_connection_pending = true;
        if (s_setup_ap_active) {
            httpd_resp_set_status(request, "202 Accepted");
            const esp_err_t response_err = send_page(
                request, "Credentials received. Connecting to home Wi-Fi; this page refreshes.");
            err = esp_wifi_connect();
            if (err != ESP_OK) {
                s_connection_pending = false;
                memset(s_candidate_password, 0, sizeof(s_candidate_password));
                ESP_LOGE(TAG, "Failed to start home Wi-Fi connection: %s", esp_err_to_name(err));
                strlcpy(s_connection_message, "Could not start Wi-Fi connection.",
                        sizeof(s_connection_message));
            }
            return response_err;
        }

        err = esp_wifi_disconnect();
        if (err == ESP_OK) {
            return send_page(request, "Wi-Fi credentials updated; reconnecting.");
        }
        if (err == ESP_ERR_WIFI_NOT_CONNECT) {
            err = esp_wifi_connect();
        }
    }
    if (err != ESP_OK) {
        s_connection_pending = false;
        memset(s_candidate_password, 0, sizeof(s_candidate_password));
        strlcpy(s_connection_message, "Unable to start Wi-Fi connection.",
                sizeof(s_connection_message));
        ESP_LOGE(TAG, "Could not start station connection: %s", esp_err_to_name(err));
        httpd_resp_set_status(request, "500 Internal Server Error");
        return httpd_resp_sendstr(request, "Unable to start Wi-Fi connection.");
    }

    httpd_resp_set_status(request, "202 Accepted");
    return send_page(request, "Credentials received. Waiting for the network connection.");
}

static const httpd_uri_t s_root_uri = {
    .uri = "/",
    .method = HTTP_GET,
    .handler = root_get_handler,
};

static const httpd_uri_t s_wifi_get_uri = {
    .uri = "/wifi",
    .method = HTTP_GET,
    .handler = setup_get_handler,
};

static const httpd_uri_t s_pulse_uri = {
    .uri = "/pulse",
    .method = HTTP_POST,
    .handler = pulse_post_handler,
};

static const httpd_uri_t s_wifi_uri = {
    .uri = "/wifi",
    .method = HTTP_POST,
    .handler = wifi_post_handler,
};

static esp_err_t start_web_server(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 8;
    config.max_open_sockets = 4;
    config.lru_purge_enable = true;
    esp_err_t err = httpd_start(&s_http_server, &config);
    if (err != ESP_OK) {
        return err;
    }
    err = httpd_register_uri_handler(s_http_server, &s_root_uri);
    if (err == ESP_OK) {
        err = httpd_register_uri_handler(s_http_server, &s_wifi_get_uri);
    }
    if (err == ESP_OK) {
        err = httpd_register_uri_handler(s_http_server, &s_pulse_uri);
    }
    if (err == ESP_OK) {
        err = httpd_register_uri_handler(s_http_server, &s_wifi_uri);
    }
    return err;
}

static esp_err_t start_setup_ap(void)
{
    const size_t ssid_len = strlen(CONFIG_ROLLOFFESP_SETUP_SSID);
    const size_t password_len = strlen(CONFIG_ROLLOFFESP_SETUP_PASSWORD);
    if (ssid_len == 0 || ssid_len > 32 || password_len < 8 || password_len > 63) {
        return ESP_ERR_INVALID_ARG;
    }

    wifi_config_t ap_config = {0};
    memcpy(ap_config.ap.ssid, CONFIG_ROLLOFFESP_SETUP_SSID, ssid_len);
    ap_config.ap.ssid_len = ssid_len;
    memcpy(ap_config.ap.password, CONFIG_ROLLOFFESP_SETUP_PASSWORD, password_len);
    ap_config.ap.channel = 1;
    ap_config.ap.max_connection = 4;
    ap_config.ap.authmode = WIFI_AUTH_WPA2_PSK;
    ap_config.ap.pmf_cfg.required = false;

    esp_err_t err = esp_wifi_set_config(WIFI_IF_AP, &ap_config);
    if (err == ESP_OK) {
        s_setup_ap_active = true;
    }
    return err;
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    (void)arg;
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG, "Station interface started");
        return;
    }
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_CONNECTED) {
        const wifi_event_sta_connected_t *event = event_data;
        ESP_LOGI(TAG, "Associated with SSID '%.*s' on channel %u",
                 event->ssid_len, event->ssid, event->channel);
        return;
    }
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_START) {
        ESP_LOGI(TAG, "Setup access point started");
        return;
    }
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STOP) {
        ESP_LOGI(TAG, "Setup access point stopped");
        s_setup_ap_active = false;
        return;
    }
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        s_station_connected = false;
        const wifi_event_sta_disconnected_t *event = event_data;
        ESP_LOGW(TAG, "Station disconnected; reason=%u, retry=%u/%u",
                 event->reason, s_station_retries + 1, WIFI_RETRY_LIMIT);
        if (s_station_retries < WIFI_RETRY_LIMIT) {
            s_station_retries++;
            ESP_LOGW(TAG, "Wi-Fi disconnected; retry %u of %u",
                     s_station_retries, WIFI_RETRY_LIMIT);
            esp_wifi_connect();
        } else {
            strlcpy(s_connection_message,
                    "Could not connect. Check credentials and retry from setup mode.",
                    sizeof(s_connection_message));
            s_connection_pending = false;
            memset(s_candidate_password, 0, sizeof(s_candidate_password));
            if (!s_setup_ap_active) {
                esp_err_t ap_err = esp_wifi_set_mode(WIFI_MODE_APSTA);
                if (ap_err == ESP_OK) {
                    ap_err = start_setup_ap();
                }
                if (ap_err != ESP_OK) {
                    ESP_LOGE(TAG, "Failed to restore setup AP: %s", esp_err_to_name(ap_err));
                }
            }
            ESP_LOGW(TAG, "Station connection attempts exhausted; setup AP is available");
        }
        return;
    }

    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *event = event_data;
        s_station_connected = true;
        s_connection_pending = false;
        s_station_retries = 0;

        if (s_candidate_ssid[0] != '\0' && s_candidate_password[0] != '\0') {
            const esp_err_t save_err = save_wifi_credentials(s_candidate_ssid,
                                                              s_candidate_password);
            if (save_err != ESP_OK) {
                ESP_LOGE(TAG, "Connected but failed to save Wi-Fi credentials: %s",
                         esp_err_to_name(save_err));
                strlcpy(s_connection_message,
                        "Connected, but could not save Wi-Fi settings.",
                        sizeof(s_connection_message));
            } else {
                memset(s_candidate_password, 0, sizeof(s_candidate_password));
                strlcpy(s_connection_message, "Connected to the home Wi-Fi network.",
                        sizeof(s_connection_message));
            }
        } else {
            strlcpy(s_connection_message, "Connected to the home Wi-Fi network.",
                    sizeof(s_connection_message));
        }

        ESP_LOGI(TAG, "Connected to home Wi-Fi; IP=" IPSTR, IP2STR(&event->ip_info.ip));
        if (s_setup_ap_active) {
            const esp_err_t mode_err = esp_wifi_set_mode(WIFI_MODE_STA);
            if (mode_err == ESP_OK) {
                s_setup_ap_active = false;
                ESP_LOGI(TAG, "Setup access point disabled after station association");
            } else {
                ESP_LOGE(TAG, "Unable to disable setup AP: %s", esp_err_to_name(mode_err));
            }
        }
    }
}

esp_err_t rolloffesp_wifi_web_start(void)
{
    char saved_ssid[33] = {0};
    char saved_password[64] = {0};
    bool has_credentials = false;
    uint32_t pulse_ms = CONFIG_ROLLOFFESP_RELAY_PULSE_MS;
    esp_err_t err = load_configuration(saved_ssid, sizeof(saved_ssid),
                                      saved_password, sizeof(saved_password),
                                      &has_credentials, &pulse_ms);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read saved configuration: %s", esp_err_to_name(err));
        return err;
    }
    err = rolloffesp_set_default_pulse_ms(pulse_ms);
    if (err != ESP_OK) {
        return err;
    }
    if (has_credentials) {
        strlcpy(s_candidate_ssid, saved_ssid, sizeof(s_candidate_ssid));
        strlcpy(s_candidate_password, saved_password, sizeof(s_candidate_password));
    }
    memset(saved_password, 0, sizeof(saved_password));

    err = esp_netif_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }
    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }
    s_ap_netif = esp_netif_create_default_wifi_ap();
    if (s_ap_netif == NULL) {
        return ESP_FAIL;
    }
    s_sta_netif = esp_netif_create_default_wifi_sta();
    if (s_sta_netif == NULL) {
        return ESP_FAIL;
    }

    wifi_init_config_t wifi_init = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&wifi_init);
    if (err != ESP_OK) {
        return err;
    }
    err = esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                     wifi_event_handler, NULL);
    if (err != ESP_OK) {
        return err;
    }
    err = esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                     wifi_event_handler, NULL);
    if (err != ESP_OK) {
        return err;
    }

    err = mdns_init();
    if (err != ESP_OK) {
        return err;
    }
    err = mdns_hostname_set("rolloffesp");
    if (err != ESP_OK) {
        return err;
    }
    err = mdns_instance_name_set("RolloffESP Roof Controller");
    if (err != ESP_OK) {
        return err;
    }

    err = start_web_server();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start configuration server: %s", esp_err_to_name(err));
        return err;
    }

    err = esp_wifi_set_mode(has_credentials ? WIFI_MODE_APSTA : WIFI_MODE_AP);
    if (err != ESP_OK) {
        return err;
    }
    err = start_setup_ap();
    if (err != ESP_OK) {
        return err;
    }
    err = esp_wifi_start();
    if (err != ESP_OK) {
        return err;
    }

    if (has_credentials) {
        wifi_config_t station_config = {0};
        memcpy(station_config.sta.ssid, s_candidate_ssid, strlen(s_candidate_ssid));
        memcpy(station_config.sta.password, s_candidate_password,
               strlen(s_candidate_password));
        station_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
        err = esp_wifi_set_config(WIFI_IF_STA, &station_config);
        if (err == ESP_OK) {
            strlcpy(s_connection_message, "Connecting to saved home Wi-Fi...",
                    sizeof(s_connection_message));
            ESP_LOGI(TAG, "Starting connection attempt for saved SSID '%s'",
                     s_candidate_ssid);
            err = esp_wifi_connect();
        }
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to start saved Wi-Fi connection: %s", esp_err_to_name(err));
            return err;
        }
    } else {
        strlcpy(s_connection_message, "Setup hotspot ready", sizeof(s_connection_message));
    }

    ESP_LOGI(TAG, "Configuration page available at http://rolloffesp.local");
    ESP_LOGI(TAG, "Setup page fallback: http://192.168.4.1");
    ESP_LOGI(TAG, "Setup AP SSID: %s; WPA2 password is configured (not logged)",
             CONFIG_ROLLOFFESP_SETUP_SSID);
    return ESP_OK;
}
