#include "rolloffesp_indi.h"

#include <ctype.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include "freertos/task.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"

#include "rolloffesp_roof_control.h"

#define INDI_PORT 7624
#define INDI_DEVICE "RolloffESP"
#define INDI_BUFFER_SIZE 2048

typedef enum {
    INDI_COMMAND_NONE,
    INDI_COMMAND_CONNECT,
    INDI_COMMAND_DISCONNECT,
    INDI_COMMAND_OPEN,
    INDI_COMMAND_CLOSE,
    INDI_COMMAND_ABORT,
    INDI_COMMAND_PARK,
    INDI_COMMAND_UNPARK,
} indi_command_t;

static const char *TAG = "rolloffesp_indi";
static portMUX_TYPE s_indi_mux = portMUX_INITIALIZER_UNLOCKED;
static bool s_network_ready;
static bool s_client_connected;
static uint8_t s_defined_mask;
static int s_server_socket = -1;

static bool network_ready(void)
{
    bool ready;
    portENTER_CRITICAL(&s_indi_mux);
    ready = s_network_ready;
    portEXIT_CRITICAL(&s_indi_mux);
    return ready;
}

static int send_all(int socket_fd, const char *data, size_t length)
{
    size_t sent = 0;
    while (sent < length) {
        const int result = send(socket_fd, data + sent, length - sent, 0);
        if (result <= 0) {
            return -1;
        }
        sent += (size_t)result;
    }
    return 0;
}

static int send_xml(int socket_fd, const char *xml)
{
    return send_all(socket_fd, xml, strlen(xml));
}

static bool get_attribute(const char *tag, const char *attribute,
                          char *value, size_t value_size)
{
    const size_t attribute_length = strlen(attribute);
    const char *cursor = tag;
    while (*cursor != '\0') {
        while (*cursor != '\0' && !isspace((unsigned char)*cursor)) {
            cursor++;
        }
        while (*cursor != '\0' && isspace((unsigned char)*cursor)) {
            cursor++;
        }
        if (*cursor == '\0') {
            break;
        }
        if (strncmp(cursor, attribute, attribute_length) != 0 ||
            cursor[attribute_length] != '=') {
            continue;
        }
        cursor += attribute_length + 1;
        if (*cursor != '"' && *cursor != '\'') {
            return false;
        }
        const char quote = *cursor++;
        const char *end = strchr(cursor, quote);
        if (end == NULL) {
            return false;
        }
        const size_t length = (size_t)(end - cursor);
        if (length == 0 || length >= value_size) {
            return false;
        }
        memcpy(value, cursor, length);
        value[length] = '\0';
        return true;
    }
    return false;
}

static bool get_switch_value(const char *xml, const char *switch_name)
{
    char opening[96];
    const int opening_length = snprintf(opening, sizeof(opening),
                                        "<oneSwitch name=\"%s\"", switch_name);
    if (opening_length < 0 || (size_t)opening_length >= sizeof(opening)) {
        return false;
    }
    const char *item = strstr(xml, opening);
    if (item == NULL) {
        return false;
    }
    const char *content = strchr(item, '>');
    const char *end = content != NULL ? strstr(content + 1, "</oneSwitch>") : NULL;
    if (content == NULL || end == NULL) {
        return false;
    }
    content++;
    while (content < end && (*content == ' ' || *content == '\r' ||
                             *content == '\n' || *content == '\t')) {
        content++;
    }
    return (size_t)(end - content) >= 2 && strncmp(content, "On", 2) == 0;
}

static const char *indi_state_for_roof(rolloffesp_roof_status_t status)
{
    switch (status) {
    case ROLLOFFESP_ROOF_OPENING:
    case ROLLOFFESP_ROOF_CLOSING:
        return "Busy";
    case ROLLOFFESP_ROOF_OPEN:
    case ROLLOFFESP_ROOF_CLOSED:
        return "Ok";
    case ROLLOFFESP_ROOF_UNKNOWN:
    case ROLLOFFESP_ROOF_INVALID:
    default:
        return "Alert";
    }
}

static int send_definitions(int socket_fd, const char *requested_name)
{
    static const char *const definitions[] = {
        "<defSwitchVector device=\"RolloffESP\" name=\"CONNECTION\" label=\"Connection\" group=\"Main Control\" state=\"Idle\" perm=\"rw\" rule=\"OneOfMany\" timeout=\"60\"><defSwitch name=\"CONNECT\" label=\"Connect\">Off</defSwitch><defSwitch name=\"DISCONNECT\" label=\"Disconnect\">On</defSwitch></defSwitchVector>\n",
        "<defSwitchVector device=\"RolloffESP\" name=\"DOME_SHUTTER\" label=\"Roof Shutter\" group=\"Main Control\" state=\"Idle\" perm=\"rw\" rule=\"AtMostOne\" timeout=\"60\"><defSwitch name=\"SHUTTER_OPEN\" label=\"Open\">Off</defSwitch><defSwitch name=\"SHUTTER_CLOSE\" label=\"Close\">Off</defSwitch><defSwitch name=\"SHUTTER_ABORT\" label=\"Abort\">Off</defSwitch></defSwitchVector>\n",
        "<defSwitchVector device=\"RolloffESP\" name=\"DOME_ABORT_MOTION\" label=\"Abort Motion\" group=\"Main Control\" state=\"Idle\" perm=\"rw\" rule=\"AtMostOne\"><defSwitch name=\"ABORT\" label=\"Abort\">Off</defSwitch></defSwitchVector>\n",
        "<defSwitchVector device=\"RolloffESP\" name=\"DOME_PARK\" label=\"Park\" group=\"Main Control\" state=\"Idle\" perm=\"rw\" rule=\"AtMostOne\" timeout=\"60\"><defSwitch name=\"PARK\" label=\"Park\">Off</defSwitch><defSwitch name=\"UNPARK\" label=\"Unpark\">On</defSwitch></defSwitchVector>\n",
        "<defSwitchVector device=\"RolloffESP\" name=\"ROOF_POSITION\" label=\"Limit Switch Position\" group=\"Status\" state=\"Idle\" perm=\"ro\" rule=\"AtMostOne\"><defSwitch name=\"OPEN\" label=\"Open limit active\">Off</defSwitch><defSwitch name=\"CLOSED\" label=\"Closed limit active\">Off</defSwitch><defSwitch name=\"UNKNOWN\" label=\"Neither limit active\">On</defSwitch><defSwitch name=\"INVALID\" label=\"Both limits active\">Off</defSwitch></defSwitchVector>\n",
    };
    static const char *const names[] = {
        "CONNECTION", "DOME_SHUTTER", "DOME_ABORT_MOTION", "DOME_PARK",
        "ROOF_POSITION"
    };
    for (size_t i = 0; i < sizeof(definitions) / sizeof(definitions[0]); i++) {
        if ((requested_name[0] == '\0' || strcmp(requested_name, names[i]) == 0) &&
            send_xml(socket_fd, definitions[i]) != 0) {
            return -1;
        } else if (requested_name[0] == '\0' ||
                   strcmp(requested_name, names[i]) == 0) {
            s_defined_mask |= (uint8_t)(1U << i);
        }
    }
    return 0;
}

static int send_status(int socket_fd)
{
    const rolloffesp_roof_status_t status = rolloffesp_roof_status();
    const bool ready = network_ready();
    const char *connection_state = ready ? "Ok" : "Alert";
    char message[1024];
    int length;
    if ((s_defined_mask & (1U << 0)) != 0) {
        length = snprintf(message, sizeof(message),
            "<setSwitchVector device=\"RolloffESP\" name=\"CONNECTION\" state=\"%s\">"
            "<oneSwitch name=\"CONNECT\">%s</oneSwitch>"
            "<oneSwitch name=\"DISCONNECT\">%s</oneSwitch></setSwitchVector>\n",
            connection_state, s_client_connected ? "On" : "Off",
            s_client_connected ? "Off" : "On");
        if (length < 0 || (size_t)length >= sizeof(message) ||
            send_all(socket_fd, message, (size_t)length) != 0) {
            return -1;
        }
    }
    if ((s_defined_mask & (1U << 1)) != 0) {
        length = snprintf(message, sizeof(message),
            "<setSwitchVector device=\"RolloffESP\" name=\"DOME_SHUTTER\" state=\"%s\">"
            "<oneSwitch name=\"SHUTTER_OPEN\">Off</oneSwitch>"
            "<oneSwitch name=\"SHUTTER_CLOSE\">Off</oneSwitch>"
            "<oneSwitch name=\"SHUTTER_ABORT\">Off</oneSwitch></setSwitchVector>\n",
            indi_state_for_roof(status));
        if (length < 0 || (size_t)length >= sizeof(message) ||
            send_all(socket_fd, message, (size_t)length) != 0) {
            return -1;
        }
    }
    if ((s_defined_mask & (1U << 2)) != 0) {
        length = snprintf(message, sizeof(message),
            "<setSwitchVector device=\"RolloffESP\" name=\"DOME_ABORT_MOTION\" state=\"%s\">"
            "<oneSwitch name=\"ABORT\">Off</oneSwitch></setSwitchVector>\n",
            indi_state_for_roof(status));
        if (length < 0 || (size_t)length >= sizeof(message) ||
            send_all(socket_fd, message, (size_t)length) != 0) {
            return -1;
        }
    }
    if ((s_defined_mask & (1U << 3)) != 0) {
        length = snprintf(message, sizeof(message),
            "<setSwitchVector device=\"RolloffESP\" name=\"DOME_PARK\" state=\"%s\">"
            "<oneSwitch name=\"PARK\">%s</oneSwitch>"
            "<oneSwitch name=\"UNPARK\">%s</oneSwitch></setSwitchVector>\n",
            indi_state_for_roof(status),
            status == ROLLOFFESP_ROOF_CLOSED ? "On" : "Off",
            status == ROLLOFFESP_ROOF_OPEN ? "On" : "Off");
        if (length < 0 || (size_t)length >= sizeof(message) ||
            send_all(socket_fd, message, (size_t)length) != 0) {
            return -1;
        }
    }

    if ((s_defined_mask & (1U << 4)) == 0) {
        return 0;
    }
    const char *open = status == ROLLOFFESP_ROOF_OPEN ? "On" : "Off";
    const char *closed = status == ROLLOFFESP_ROOF_CLOSED ? "On" : "Off";
    const char *unknown = status == ROLLOFFESP_ROOF_UNKNOWN ? "On" : "Off";
    const char *invalid = status == ROLLOFFESP_ROOF_INVALID ? "On" : "Off";
    length = snprintf(message, sizeof(message),
        "<setSwitchVector device=\"RolloffESP\" name=\"ROOF_POSITION\" state=\"%s\">"
        "<oneSwitch name=\"OPEN\">%s</oneSwitch>"
        "<oneSwitch name=\"CLOSED\">%s</oneSwitch>"
        "<oneSwitch name=\"UNKNOWN\">%s</oneSwitch>"
        "<oneSwitch name=\"INVALID\">%s</oneSwitch></setSwitchVector>\n",
        indi_state_for_roof(status), open, closed, unknown, invalid);
    if (length < 0 || (size_t)length >= sizeof(message)) {
        return -1;
    }
    return send_all(socket_fd, message, (size_t)length);
}

static int send_message(int socket_fd, const char *text)
{
    char xml[256];
    const int length = snprintf(xml, sizeof(xml),
        "<message device=\"RolloffESP\" message=\"%s\"/>\n", text);
    if (length < 0 || (size_t)length >= sizeof(xml)) {
        return -1;
    }
    return send_all(socket_fd, xml, (size_t)length);
}

static indi_command_t parse_command(const char *xml, const char *vector_name)
{
    const char *root_end = strchr(xml, '>');
    if (root_end == NULL) {
        return INDI_COMMAND_NONE;
    }
    char root[160];
    const size_t root_length = (size_t)(root_end - xml + 1);
    if (root_length >= sizeof(root)) {
        return INDI_COMMAND_NONE;
    }
    memcpy(root, xml, root_length);
    root[root_length] = '\0';

    char device[48] = {0};
    char name[48] = {0};
    if (!get_attribute(root, "device", device, sizeof(device)) ||
        !get_attribute(root, "name", name, sizeof(name)) ||
        strcmp(device, INDI_DEVICE) != 0 ||
        strcmp(name, vector_name) != 0) {
        return INDI_COMMAND_NONE;
    }

    if (strcmp(vector_name, "CONNECTION") == 0) {
        if (get_switch_value(xml, "CONNECT")) {
            return INDI_COMMAND_CONNECT;
        }
        if (get_switch_value(xml, "DISCONNECT")) {
            return INDI_COMMAND_DISCONNECT;
        }
    } else if (strcmp(vector_name, "DOME_SHUTTER") == 0) {
        if (get_switch_value(xml, "SHUTTER_OPEN") ||
            get_switch_value(xml, "OPEN")) {
            return INDI_COMMAND_OPEN;
        }
        if (get_switch_value(xml, "SHUTTER_CLOSE") ||
            get_switch_value(xml, "CLOSE")) {
            return INDI_COMMAND_CLOSE;
        }
        if (get_switch_value(xml, "SHUTTER_ABORT") ||
            get_switch_value(xml, "ABORT")) {
            return INDI_COMMAND_ABORT;
        }
    } else if (strcmp(vector_name, "DOME_ABORT_MOTION") == 0) {
        if (get_switch_value(xml, "ABORT")) {
            return INDI_COMMAND_ABORT;
        }
    } else if (strcmp(vector_name, "DOME_PARK") == 0) {
        if (get_switch_value(xml, "PARK")) {
            return INDI_COMMAND_PARK;
        }
        if (get_switch_value(xml, "UNPARK")) {
            return INDI_COMMAND_UNPARK;
        }
    }
    return INDI_COMMAND_NONE;
}

static int process_xml(int socket_fd, const char *xml)
{
    if (strncmp(xml, "<getProperties", 14) == 0) {
        char device[48] = {0};
        char name[48] = {0};
        const char *root_end = strchr(xml, '>');
        if (root_end == NULL) {
            return -1;
        }
        char root[160];
        const size_t root_length = (size_t)(root_end - xml + 1);
        if (root_length >= sizeof(root)) {
            return -1;
        }
        memcpy(root, xml, root_length);
        root[root_length] = '\0';
        (void)get_attribute(root, "device", device, sizeof(device));
        (void)get_attribute(root, "name", name, sizeof(name));
        if (device[0] == '\0' || strcmp(device, INDI_DEVICE) == 0) {
            if (send_definitions(socket_fd, name) != 0) {
                return -1;
            }
            return send_status(socket_fd);
        }
        return 0;
    }

    static const char *const vector_names[] = {
        "CONNECTION", "DOME_SHUTTER", "DOME_ABORT_MOTION", "DOME_PARK"
    };
    indi_command_t command = INDI_COMMAND_NONE;
    for (size_t i = 0; i < sizeof(vector_names) / sizeof(vector_names[0]); i++) {
        if (strncmp(xml, "<newSwitchVector", 16) == 0) {
            command = parse_command(xml, vector_names[i]);
            if (command != INDI_COMMAND_NONE) {
                break;
            }
        }
    }
    if (command == INDI_COMMAND_NONE) {
        if (strncmp(xml, "<newSwitchVector", 16) == 0) {
            const char *root_end = strchr(xml, '>');
            char root[160];
            char device[48] = {0};
            if (root_end != NULL) {
                const size_t root_length = (size_t)(root_end - xml + 1);
                if (root_length < sizeof(root)) {
                    memcpy(root, xml, root_length);
                    root[root_length] = '\0';
                    (void)get_attribute(root, "device", device, sizeof(device));
                }
            }
            if (strcmp(device, INDI_DEVICE) == 0 &&
                send_message(socket_fd, "Unsupported INDI property or command.") != 0) {
                return -1;
            }
        }
        return 0;
    }

    esp_err_t err = ESP_OK;
    switch (command) {
    case INDI_COMMAND_CONNECT:
        if (!network_ready()) {
            err = ESP_ERR_INVALID_STATE;
        } else {
            s_client_connected = true;
        }
        break;
    case INDI_COMMAND_DISCONNECT:
        s_client_connected = false;
        break;
    case INDI_COMMAND_OPEN:
    case INDI_COMMAND_UNPARK:
        err = s_client_connected && network_ready()
            ? rolloffesp_roof_open() : ESP_ERR_INVALID_STATE;
        break;
    case INDI_COMMAND_CLOSE:
    case INDI_COMMAND_PARK:
        err = s_client_connected && network_ready()
            ? rolloffesp_roof_close() : ESP_ERR_INVALID_STATE;
        break;
    case INDI_COMMAND_ABORT:
        err = s_client_connected && network_ready()
            ? rolloffesp_roof_abort() : ESP_ERR_INVALID_STATE;
        break;
    case INDI_COMMAND_NONE:
    default:
        break;
    }
    if (err != ESP_OK &&
        send_message(socket_fd, "Command rejected: check connection and roof position.") != 0) {
        return -1;
    }
    return s_defined_mask != 0 ? send_status(socket_fd) : 0;
}

static bool find_complete_xml(char *buffer, size_t *length, size_t *document_length)
{
    while (*length > 0 && (buffer[0] == ' ' || buffer[0] == '\r' ||
                           buffer[0] == '\n' || buffer[0] == '\t')) {
        memmove(buffer, buffer + 1, --(*length));
    }
    buffer[*length] = '\0';
    if (*length == 0 || buffer[0] != '<') {
        return false;
    }
    if (strncmp(buffer, "<?", 2) == 0 || strncmp(buffer, "<!", 2) == 0) {
        const char *end = strchr(buffer, '>');
        if (end == NULL) {
            return false;
        }
        *document_length = (size_t)(end - buffer + 1);
        return true;
    }
    const char *tag_end = strchr(buffer, '>');
    if (tag_end == NULL) {
        return false;
    }
    const char *name_start = buffer + 1;
    const char *name_end = name_start;
    while (*name_end != '\0' && *name_end != ' ' && *name_end != '\t' &&
           *name_end != '\r' && *name_end != '\n' && *name_end != '>' &&
           *name_end != '/') {
        name_end++;
    }
    const size_t name_length = (size_t)(name_end - name_start);
    if (name_length == 0 || name_length >= 32) {
        return false;
    }
    if (tag_end > buffer && tag_end[-1] == '/') {
        *document_length = (size_t)(tag_end - buffer + 1);
        return true;
    }
    char closing[40];
    memcpy(closing, "</", 2);
    memcpy(closing + 2, name_start, name_length);
    memcpy(closing + 2 + name_length, ">", 2);
    const char *close = strstr(tag_end + 1, closing);
    if (close == NULL) {
        return false;
    }
    *document_length = (size_t)(close - buffer) + name_length + 3;
    return true;
}

static void serve_client(int socket_fd)
{
    struct timeval timeout = {.tv_sec = 1, .tv_usec = 0};
    (void)setsockopt(socket_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    (void)setsockopt(socket_fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
    char buffer[INDI_BUFFER_SIZE + 1] = {0};
    size_t used = 0;
    s_client_connected = false;
    s_defined_mask = 0;
    while (true) {
        const int received = recv(socket_fd, buffer + used,
                                  INDI_BUFFER_SIZE - used, 0);
        if (received == 0) {
            break;
        }
        if (received < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
                if (s_defined_mask != 0 && send_status(socket_fd) != 0) {
                    break;
                }
                continue;
            }
            ESP_LOGW(TAG, "INDI client receive failed: errno=%d", errno);
            break;
        }
        used += (size_t)received;
        buffer[used] = '\0';
        size_t document_length = 0;
        while (find_complete_xml(buffer, &used, &document_length)) {
            const char following = buffer[document_length];
            buffer[document_length] = '\0';
            if (process_xml(socket_fd, buffer) != 0) {
                used = 0;
                break;
            }
            buffer[document_length] = following;
            memmove(buffer, buffer + document_length, used - document_length);
            used -= document_length;
            buffer[used] = '\0';
        }
        if (used == INDI_BUFFER_SIZE) {
            ESP_LOGW(TAG, "Closing INDI client that exceeded XML buffer limit");
            break;
        }
    }
    s_client_connected = false;
    s_defined_mask = 0;
}

static void indi_server_task(void *arg)
{
    (void)arg;
    while (true) {
        struct sockaddr_in peer;
        socklen_t peer_length = sizeof(peer);
        const int client_fd = accept(s_server_socket, (struct sockaddr *)&peer,
                                     &peer_length);
        if (client_fd < 0) {
            ESP_LOGW(TAG, "INDI accept failed: errno=%d", errno);
            vTaskDelay(pdMS_TO_TICKS(250));
            continue;
        }
        ESP_LOGI(TAG, "INDI client connected");
        serve_client(client_fd);
        close(client_fd);
        ESP_LOGI(TAG, "INDI client disconnected");
    }
}

esp_err_t rolloffesp_indi_start(void)
{
    s_server_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s_server_socket < 0) {
        ESP_LOGE(TAG, "Failed to create INDI server socket: errno=%d", errno);
        return ESP_FAIL;
    }
    const int reuse = 1;
    (void)setsockopt(s_server_socket, SOL_SOCKET, SO_REUSEADDR,
                     &reuse, sizeof(reuse));
    const struct sockaddr_in address = {
        .sin_family = AF_INET,
        .sin_port = htons(INDI_PORT),
        .sin_addr.s_addr = htonl(INADDR_ANY),
    };
    if (bind(s_server_socket, (const struct sockaddr *)&address,
             sizeof(address)) != 0 ||
        listen(s_server_socket, 1) != 0) {
        ESP_LOGE(TAG, "Failed to listen on INDI port %u: errno=%d",
                 INDI_PORT, errno);
        close(s_server_socket);
        s_server_socket = -1;
        return ESP_FAIL;
    }
    if (xTaskCreate(indi_server_task, "indi_server", 6144, NULL, 4, NULL) != pdPASS) {
        close(s_server_socket);
        s_server_socket = -1;
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "INDI Dome server listening on TCP port %u", INDI_PORT);
    return ESP_OK;
}

void rolloffesp_indi_set_network_ready(bool ready)
{
    portENTER_CRITICAL(&s_indi_mux);
    s_network_ready = ready;
    portEXIT_CRITICAL(&s_indi_mux);
    ESP_LOGI(TAG, "INDI roof commands %s",
             ready ? "enabled on station network" : "disabled until station network is ready");
}
