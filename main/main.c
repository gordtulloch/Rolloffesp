#include "sdkconfig.h"

#if CONFIG_ROLLOFFESP_ENABLE_BENCH_CONSOLE
#include <ctype.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#endif

#include "esp_log.h"
#include "nvs_flash.h"

#include "rolloffesp_hardware.h"
#include "rolloffesp_wifi_web.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "rolloffesp";

#if CONFIG_ROLLOFFESP_ENABLE_BENCH_CONSOLE
static const char *position_name(rolloffesp_position_t position)
{
    switch (position) {
    case ROLLOFFESP_POSITION_OPEN:
        return "OPEN";
    case ROLLOFFESP_POSITION_CLOSED:
        return "CLOSED";
    case ROLLOFFESP_POSITION_INVALID:
        return "INVALID (both switches active)";
    case ROLLOFFESP_POSITION_UNKNOWN:
    default:
        return "UNKNOWN (neither switch active)";
    }
}

static void print_status(void)
{
    ESP_LOGI(TAG, "Position: %s; relay: %s",
             position_name(rolloffesp_read_position()),
             rolloffesp_relay_is_active() ? "ACTIVE" : "inactive");
}

static void handle_console_line(char *line)
{
    while (isspace((unsigned char)*line)) {
        line++;
    }

    char *end = line + strlen(line);
    while (end > line && isspace((unsigned char)end[-1])) {
        *--end = '\0';
    }

    if (strcmp(line, "status") == 0) {
        print_status();
        return;
    }

    if (strcmp(line, "pulse") == 0) {
        const esp_err_t err = rolloffesp_relay_press_default();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Relay pulse rejected: %s", esp_err_to_name(err));
        }
        return;
    }

    if (strncmp(line, "pulse ", 6) == 0) {
        char *value = line + 6;
        char *value_end;
        errno = 0;
        const unsigned long duration_ms = strtoul(value, &value_end, 10);
        while (isspace((unsigned char)*value_end)) {
            value_end++;
        }
        if (errno != 0 || value == value_end || *value_end != '\0' ||
            duration_ms > UINT32_MAX) {
            ESP_LOGE(TAG, "Usage: pulse [duration_ms]");
            return;
        }

        const esp_err_t err = rolloffesp_relay_press_for_ms((uint32_t)duration_ms);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Relay pulse rejected: %s", esp_err_to_name(err));
        }
        return;
    }

    ESP_LOGI(TAG, "Commands: status | pulse [duration_ms]");
}
#endif

void app_main(void)
{
    ESP_ERROR_CHECK(rolloffesp_hardware_init());
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
    ESP_LOGI(TAG, "Hardware initialized; relay remains inactive.");
    ESP_ERROR_CHECK(rolloffesp_wifi_web_start());
#if CONFIG_ROLLOFFESP_ENABLE_BENCH_CONSOLE
    ESP_LOGI(TAG, "Bench console commands: status | pulse [duration_ms]");

    char line[64];
    while (true) {
        if (fgets(line, sizeof(line), stdin) != NULL) {
            handle_console_line(line);
        } else {
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }
#else
    ESP_LOGI(TAG, "USB serial bench console disabled.");
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
#endif
}
