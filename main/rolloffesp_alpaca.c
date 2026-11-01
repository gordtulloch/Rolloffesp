#include "rolloffesp_alpaca.h"

#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "esp_http_server.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include "freertos/task.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"

#include "rolloffesp_hardware.h"
#include "rolloffesp_roof_control.h"

#define ALPACA_PORT 11111
#define ALPACA_DISCOVERY_PORT 32227
#define ALPACA_DISCOVERY_REQUEST "alpacadiscovery1"
#define ALPACA_DISCOVERY_REPLY "{\"AlpacaPort\":11111}"
#define ALPACA_BODY_MAX 512
#define ALPACA_NOT_IMPLEMENTED 1024
#define ALPACA_INVALID_VALUE 1025
#define ALPACA_NOT_CONNECTED 1031
#define ALPACA_INVALID_OPERATION 1035

typedef struct {
    uint32_t client_id;
    uint32_t client_transaction_id;
} alpaca_request_ids_t;

static const char *TAG = "rolloffesp_alpaca";
static httpd_handle_t s_alpaca_server;
static portMUX_TYPE s_alpaca_mux = portMUX_INITIALIZER_UNLOCKED;
static uint32_t s_server_transaction_id;
static bool s_connected = true;
static bool s_network_ready;

static bool parse_uint32(const char *text, uint32_t *value)
{
    if (text == NULL || text[0] == '\0') {
        return false;
    }
    for (const char *p = text; *p != '\0'; p++) {
        if (*p < '0' || *p > '9') {
            return false;
        }
    }
    errno = 0;
    const unsigned long parsed = strtoul(text, NULL, 10);
    if (errno != 0 || parsed > UINT32_MAX) {
        return false;
    }
    *value = (uint32_t)parsed;
    return true;
}

static esp_err_t read_body(httpd_req_t *request, char *body, size_t capacity)
{
    if (request->content_len <= 0 ||
        (size_t)request->content_len >= capacity ||
        request->content_len > ALPACA_BODY_MAX) {
        return ESP_ERR_INVALID_SIZE;
    }
    size_t received = 0;
    while (received < (size_t)request->content_len) {
        const int result = httpd_req_recv(request, body + received,
                                          request->content_len - received);
        if (result <= 0) {
            return ESP_FAIL;
        }
        received += (size_t)result;
    }
    body[received] = '\0';
    return ESP_OK;
}

static bool form_field(const char *form, const char *name,
                       char *value, size_t value_size)
{
    const size_t name_length = strlen(name);
    const char *item = form;
    while (item != NULL && *item != '\0') {
        const char *end = strchr(item, '&');
        const size_t item_length = end != NULL
            ? (size_t)(end - item)
            : strlen(item);
        const char *equals = memchr(item, '=', item_length);
        if (equals != NULL && (size_t)(equals - item) == name_length &&
            memcmp(item, name, name_length) == 0) {
            const size_t encoded_length = item_length - name_length - 1;
            size_t output_length = 0;
            for (size_t i = 0; i < encoded_length; i++) {
                unsigned char character = (unsigned char)equals[1 + i];
                if (character == '+') {
                    character = ' ';
                } else if (character == '%') {
                    if (i + 2 >= encoded_length) {
                        return false;
                    }
                    const char hex[3] = {equals[1 + i + 1], equals[1 + i + 2], '\0'};
                    char *hex_end = NULL;
                    const unsigned long decoded = strtoul(hex, &hex_end, 16);
                    if (hex_end == hex || *hex_end != '\0' || decoded == 0) {
                        return false;
                    }
                    character = (unsigned char)decoded;
                    i += 2;
                }
                if (character == '\0' || output_length + 1 >= value_size) {
                    return false;
                }
                value[output_length++] = (char)character;
            }
            value[output_length] = '\0';
            return true;
        }
        item = end != NULL ? end + 1 : NULL;
    }
    return false;
}

static esp_err_t request_ids(httpd_req_t *request, alpaca_request_ids_t *ids,
                             char *form_body, size_t form_capacity)
{
    char query[ALPACA_BODY_MAX + 1] = {0};
    char client_id[16] = {0};
    char transaction_id[16] = {0};
    const char *source = NULL;
    if (request->method == HTTP_GET) {
        if (httpd_req_get_url_query_str(request, query, sizeof(query)) != ESP_OK) {
            return ESP_ERR_NOT_FOUND;
        }
        source = query;
    } else {
        const esp_err_t err = read_body(request, form_body, form_capacity);
        if (err != ESP_OK) {
            return err;
        }
        source = form_body;
    }
    if (!form_field(source, "ClientID", client_id, sizeof(client_id)) ||
        !form_field(source, "ClientTransactionID", transaction_id,
                    sizeof(transaction_id)) ||
        !parse_uint32(client_id, &ids->client_id) ||
        !parse_uint32(transaction_id, &ids->client_transaction_id)) {
        return ESP_ERR_INVALID_ARG;
    }
    return ESP_OK;
}

static uint32_t next_server_transaction_id(void)
{
    uint32_t result;
    portENTER_CRITICAL(&s_alpaca_mux);
    result = ++s_server_transaction_id;
    portEXIT_CRITICAL(&s_alpaca_mux);
    return result;
}

static esp_err_t send_response(httpd_req_t *request,
                               const alpaca_request_ids_t *ids,
                               int error_number, const char *error_message,
                               const char *value_json)
{
    char response[768];
    const uint32_t server_transaction_id = next_server_transaction_id();
    int length;
    if (value_json != NULL) {
        length = snprintf(response, sizeof(response),
            "{\"Value\":%s,\"ClientTransactionID\":%" PRIu32
            ",\"ServerTransactionID\":%" PRIu32
            ",\"ErrorNumber\":%d,\"ErrorMessage\":\"%s\"}",
            value_json, ids->client_transaction_id, server_transaction_id,
            error_number, error_message);
    } else {
        length = snprintf(response, sizeof(response),
            "{\"ClientTransactionID\":%" PRIu32
            ",\"ServerTransactionID\":%" PRIu32
            ",\"ErrorNumber\":%d,\"ErrorMessage\":\"%s\"}",
            ids->client_transaction_id, server_transaction_id,
            error_number, error_message);
    }
    if (length < 0 || (size_t)length >= sizeof(response)) {
        return ESP_FAIL;
    }
    httpd_resp_set_type(request, "application/json");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    return httpd_resp_send(request, response, length);
}

static const char *shutter_status_json(void)
{
    switch (rolloffesp_roof_status()) {
    case ROLLOFFESP_ROOF_OPEN:
        return "0";
    case ROLLOFFESP_ROOF_CLOSED:
        return "1";
    case ROLLOFFESP_ROOF_OPENING:
        return "2";
    case ROLLOFFESP_ROOF_CLOSING:
        return "3";
    case ROLLOFFESP_ROOF_UNKNOWN:
    case ROLLOFFESP_ROOF_INVALID:
    default:
        return "4";
    }
}

static bool is_connected(void)
{
    bool connected;
    portENTER_CRITICAL(&s_alpaca_mux);
    connected = s_connected && s_network_ready;
    portEXIT_CRITICAL(&s_alpaca_mux);
    return connected;
}

static bool get_property(const char *name, const char **value)
{
    if (strcmp(name, "connected") == 0) {
        *value = is_connected() ? "true" : "false";
    } else if (strcmp(name, "description") == 0) {
        *value = "\"ESP32 roll-off roof controller\"";
    } else if (strcmp(name, "driverinfo") == 0) {
        *value = "\"RolloffESP Alpaca Dome driver\"";
    } else if (strcmp(name, "driverversion") == 0) {
        *value = "\"1.0\"";
    } else if (strcmp(name, "interfaceversion") == 0) {
        *value = "3";
    } else if (strcmp(name, "name") == 0) {
        *value = "\"RolloffESP\"";
    } else if (strcmp(name, "supportedactions") == 0) {
        *value = "[]";
    } else if (strcmp(name, "cansetshutter") == 0 ||
               strcmp(name, "canpark") == 0) {
        *value = "true";
    } else if (strcmp(name, "canfindhome") == 0 ||
               strcmp(name, "cansetaltitude") == 0 ||
               strcmp(name, "cansetazimuth") == 0 ||
               strcmp(name, "cansetpark") == 0 ||
               strcmp(name, "canslave") == 0 ||
               strcmp(name, "cansync") == 0) {
        *value = "false";
    } else if (strcmp(name, "shutterstatus") == 0) {
        *value = shutter_status_json();
    } else if (strcmp(name, "slewing") == 0) {
        *value = (strcmp(shutter_status_json(), "2") == 0 ||
                  strcmp(shutter_status_json(), "3") == 0) ? "true" : "false";
    } else if (strcmp(name, "athome") == 0) {
        *value = "false";
    } else if (strcmp(name, "atpark") == 0) {
        *value = rolloffesp_read_position() == ROLLOFFESP_POSITION_CLOSED
            ? "true" : "false";
    } else if (strcmp(name, "altitude") == 0 ||
               strcmp(name, "azimuth") == 0) {
        return false;
    } else if (strcmp(name, "ispulseguiding") == 0) {
        *value = "false";
    } else {
        return false;
    }
    return true;
}

static esp_err_t dome_api_handler(httpd_req_t *request)
{
    char body[ALPACA_BODY_MAX + 1] = {0};
    char path[96];
    alpaca_request_ids_t ids = {0};
    esp_err_t err = request_ids(request, &ids, body, sizeof(body));
    if (err != ESP_OK) {
        memset(body, 0, sizeof(body));
        return send_response(request, &ids, ALPACA_INVALID_VALUE,
                             "ClientID and ClientTransactionID are required.", NULL);
    }

    if (strlcpy(path, request->uri, sizeof(path)) >= sizeof(path)) {
        memset(body, 0, sizeof(body));
        return send_response(request, &ids, ALPACA_NOT_IMPLEMENTED,
                             "Unknown Dome member.", NULL);
    }
    char *query_start = strchr(path, '?');
    if (query_start != NULL) {
        *query_start = '\0';
    }
    const char *member = strrchr(path, '/');
    if (member == NULL || member[1] == '\0') {
        memset(body, 0, sizeof(body));
        return send_response(request, &ids, ALPACA_NOT_IMPLEMENTED,
                             "Unknown Dome member.", NULL);
    }
    member++;
    char member_lower[32];
    const size_t member_length = strlen(member);
    if (member_length >= sizeof(member_lower)) {
        memset(body, 0, sizeof(body));
        return send_response(request, &ids, ALPACA_NOT_IMPLEMENTED,
                             "Unknown Dome member.", NULL);
    }
    for (size_t i = 0; i <= member_length; i++) {
        const char ch = member[i];
        member_lower[i] = ch >= 'A' && ch <= 'Z' ? (char)(ch + ('a' - 'A')) : ch;
    }

    if (request->method == HTTP_GET) {
        if (strcmp(member_lower, "connected") != 0 && !is_connected()) {
            memset(body, 0, sizeof(body));
            return send_response(request, &ids, ALPACA_NOT_CONNECTED,
                                 "Dome is not connected.", NULL);
        }
        const char *value = NULL;
        if (!get_property(member_lower, &value)) {
            memset(body, 0, sizeof(body));
            return send_response(request, &ids, ALPACA_NOT_IMPLEMENTED,
                                 "Dome member is not implemented.", NULL);
        }
        memset(body, 0, sizeof(body));
        return send_response(request, &ids, 0, "", value);
    }

    int error_number = 0;
    const char *error_message = "";
    if (strcmp(member_lower, "connected") == 0) {
        char connected[8] = {0};
        if (!form_field(body, "Connected", connected, sizeof(connected))) {
            error_number = ALPACA_INVALID_VALUE;
            error_message = "Connected must be true or false.";
        } else {
            for (size_t i = 0; connected[i] != '\0'; i++) {
                if (connected[i] >= 'A' && connected[i] <= 'Z') {
                    connected[i] = (char)(connected[i] + ('a' - 'A'));
                }
            }
            if (strcmp(connected, "true") != 0 &&
                strcmp(connected, "false") != 0) {
                error_number = ALPACA_INVALID_VALUE;
                error_message = "Connected must be true or false.";
            } else {
                portENTER_CRITICAL(&s_alpaca_mux);
                s_connected = strcmp(connected, "true") == 0;
                portEXIT_CRITICAL(&s_alpaca_mux);
            }
        }
    } else if (!is_connected()) {
        error_number = ALPACA_NOT_CONNECTED;
        error_message = "Dome is not connected.";
    } else if (strcmp(member_lower, "openshutter") == 0 ||
               strcmp(member_lower, "unpark") == 0) {
        err = rolloffesp_roof_open();
    } else if (strcmp(member_lower, "closeshutter") == 0 ||
               strcmp(member_lower, "park") == 0) {
        err = rolloffesp_roof_close();
    } else if (strcmp(member_lower, "abortslew") == 0) {
        err = rolloffesp_roof_abort();
    } else {
        error_number = ALPACA_NOT_IMPLEMENTED;
        error_message = "Dome method is not implemented.";
    }
    memset(body, 0, sizeof(body));

    if (err != ESP_OK && error_number == 0) {
        error_number = err == ESP_ERR_INVALID_STATE
            ? ALPACA_INVALID_OPERATION : ALPACA_INVALID_OPERATION;
        error_message = err == ESP_ERR_INVALID_STATE
            ? "Roof state is unknown or command is not valid in the current state."
            : "Relay press could not be started.";
    }
    if (error_number != 0) {
        ESP_LOGW(TAG, "Rejected Dome request '%s': %s (%d)",
                 member, error_message, error_number);
    }
    return send_response(request, &ids, error_number, error_message, NULL);
}

static esp_err_t management_handler(httpd_req_t *request)
{
    char query[ALPACA_BODY_MAX + 1] = {0};
    char path[96];
    char client_transaction_id[16] = {0};
    alpaca_request_ids_t ids = {0};
    if (httpd_req_get_url_query_str(request, query, sizeof(query)) == ESP_OK &&
        form_field(query, "ClientTransactionID", client_transaction_id,
                   sizeof(client_transaction_id))) {
        (void)parse_uint32(client_transaction_id, &ids.client_transaction_id);
    }

    if (strlcpy(path, request->uri, sizeof(path)) >= sizeof(path)) {
        return ESP_FAIL;
    }
    char *query_start = strchr(path, '?');
    if (query_start != NULL) {
        *query_start = '\0';
    }
    const char *value = NULL;
    if (strcmp(path, "/management/apiversions") == 0) {
        value = "[1]";
    } else if (strcmp(path, "/management/v1/description") == 0) {
        value = "{\"Manufacturer\":\"RolloffESP\",\"ManufacturerVersion\":\"1.0\","
                "\"Location\":\"ESP32\"}";
    } else if (strcmp(path, "/management/v1/configureddevices") == 0) {
        value = "[{\"DeviceName\":\"RolloffESP\",\"DeviceType\":\"Dome\","
                "\"DeviceNumber\":0,\"UniqueID\":\"rolloffesp-dome-0\"}]";
    } else {
        return ESP_FAIL;
    }
    return send_response(request, &ids, 0, "", value);
}

static esp_err_t start_alpaca_http_server(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = ALPACA_PORT;
    config.ctrl_port = 32769;
    config.max_uri_handlers = 8;
    config.max_open_sockets = 4;
    config.stack_size = 6144;
    config.lru_purge_enable = true;
    config.uri_match_fn = httpd_uri_match_wildcard;
    esp_err_t err = httpd_start(&s_alpaca_server, &config);
    if (err != ESP_OK) {
        return err;
    }

    static const httpd_uri_t dome_uri = {
        .uri = "/api/v1/dome/0/*",
        .method = HTTP_GET,
        .handler = dome_api_handler,
    };
    static const httpd_uri_t dome_put_uri = {
        .uri = "/api/v1/dome/0/*",
        .method = HTTP_PUT,
        .handler = dome_api_handler,
    };
    static const httpd_uri_t management_versions_uri = {
        .uri = "/management/apiversions",
        .method = HTTP_GET,
        .handler = management_handler,
    };
    static const httpd_uri_t management_description_uri = {
        .uri = "/management/v1/description",
        .method = HTTP_GET,
        .handler = management_handler,
    };
    static const httpd_uri_t management_devices_uri = {
        .uri = "/management/v1/configureddevices",
        .method = HTTP_GET,
        .handler = management_handler,
    };

    const httpd_uri_t *routes[] = {
        &dome_uri,
        &dome_put_uri,
        &management_versions_uri,
        &management_description_uri,
        &management_devices_uri,
    };
    for (size_t i = 0; i < sizeof(routes) / sizeof(routes[0]); i++) {
        err = httpd_register_uri_handler(s_alpaca_server, routes[i]);
        if (err != ESP_OK) {
            httpd_stop(s_alpaca_server);
            s_alpaca_server = NULL;
            return err;
        }
    }
    return ESP_OK;
}

static void discovery_task(void *arg)
{
    (void)arg;
    const int socket_fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (socket_fd < 0) {
        ESP_LOGE(TAG, "Failed to create Alpaca discovery socket: errno=%d", errno);
        vTaskDelete(NULL);
        return;
    }
    const int reuse = 1;
    (void)setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    struct sockaddr_in local_address = {
        .sin_family = AF_INET,
        .sin_port = htons(ALPACA_DISCOVERY_PORT),
        .sin_addr.s_addr = htonl(INADDR_ANY),
    };
    if (bind(socket_fd, (struct sockaddr *)&local_address, sizeof(local_address)) != 0) {
        ESP_LOGE(TAG, "Failed to bind Alpaca discovery port: errno=%d", errno);
        close(socket_fd);
        vTaskDelete(NULL);
        return;
    }

    char packet[64];
    while (true) {
        struct sockaddr_in peer;
        socklen_t peer_length = sizeof(peer);
        const int length = recvfrom(socket_fd, packet, sizeof(packet) - 1, 0,
                                    (struct sockaddr *)&peer, &peer_length);
        if (length < 0) {
            ESP_LOGW(TAG, "Alpaca discovery receive failed: errno=%d", errno);
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }
        packet[length] = '\0';
        if (strcmp(packet, ALPACA_DISCOVERY_REQUEST) == 0) {
            const char reply[] = ALPACA_DISCOVERY_REPLY;
            if (sendto(socket_fd, reply, sizeof(reply) - 1, 0,
                       (struct sockaddr *)&peer, peer_length) < 0) {
                ESP_LOGW(TAG, "Alpaca discovery reply failed: errno=%d", errno);
            }
        }
    }
}

esp_err_t rolloffesp_alpaca_start(void)
{
    esp_err_t err = start_alpaca_http_server();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start Alpaca HTTP server: %s", esp_err_to_name(err));
        return err;
    }
    if (xTaskCreate(discovery_task, "alpaca_discovery", 3072, NULL, 4, NULL) != pdPASS) {
        httpd_stop(s_alpaca_server);
        s_alpaca_server = NULL;
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "Alpaca Dome API listening on TCP port %u", ALPACA_PORT);
    ESP_LOGI(TAG, "Alpaca discovery listening on UDP port %u", ALPACA_DISCOVERY_PORT);
    return ESP_OK;
}

void rolloffesp_alpaca_set_network_ready(bool ready)
{
    portENTER_CRITICAL(&s_alpaca_mux);
    s_network_ready = ready;
    portEXIT_CRITICAL(&s_alpaca_mux);
    ESP_LOGI(TAG, "Alpaca command interface %s",
             ready ? "enabled on station network" : "disabled until station network is ready");
}
