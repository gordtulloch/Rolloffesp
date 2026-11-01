#include "rolloffesp_hardware.h"

#include <inttypes.h>

#include "sdkconfig.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"

#define RELAY_GPIO GPIO_NUM_25
#define OPEN_GPIO GPIO_NUM_34
#define CLOSED_GPIO GPIO_NUM_35
#define SWITCH_ACTIVE_LEVEL 0

#if CONFIG_ROLLOFFESP_RELAY_ACTIVE_HIGH
#define RELAY_ACTIVE_LEVEL 1
#else
#define RELAY_ACTIVE_LEVEL 0
#endif

#define RELAY_INACTIVE_LEVEL (!RELAY_ACTIVE_LEVEL)
#define MIN_RELAY_PULSE_MS 100

static const char *TAG = "rolloffesp_hw";
static esp_timer_handle_t relay_timer;
static portMUX_TYPE relay_state_mux = portMUX_INITIALIZER_UNLOCKED;
static bool relay_active;
static uint32_t default_pulse_ms = CONFIG_ROLLOFFESP_RELAY_PULSE_MS;

static void relay_release_callback(void *arg)
{
    (void)arg;
    const esp_err_t err = gpio_set_level(RELAY_GPIO, RELAY_INACTIVE_LEVEL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to release relay output: %s", esp_err_to_name(err));
        return;
    }

    portENTER_CRITICAL(&relay_state_mux);
    relay_active = false;
    portEXIT_CRITICAL(&relay_state_mux);
}

esp_err_t rolloffesp_hardware_init(void)
{
    if (CONFIG_ROLLOFFESP_RELAY_PULSE_MS > CONFIG_ROLLOFFESP_RELAY_PULSE_MAX_MS) {
        return ESP_ERR_INVALID_ARG;
    }

    const gpio_config_t input_config = {
        .pin_bit_mask = (1ULL << OPEN_GPIO) | (1ULL << CLOSED_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    esp_err_t err = gpio_set_level(RELAY_GPIO, RELAY_INACTIVE_LEVEL);
    if (err != ESP_OK) {
        return err;
    }

    const gpio_config_t relay_config = {
        .pin_bit_mask = 1ULL << RELAY_GPIO,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = RELAY_ACTIVE_LEVEL == 0
            ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE,
        .pull_down_en = RELAY_ACTIVE_LEVEL == 1
            ? GPIO_PULLDOWN_ENABLE : GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    // Preload the inactive output latch before enabling the GPIO driver.
    err = gpio_config(&relay_config);
    if (err != ESP_OK) {
        return err;
    }

    err = gpio_set_level(RELAY_GPIO, RELAY_INACTIVE_LEVEL);
    if (err != ESP_OK) {
        return err;
    }

    err = gpio_config(&input_config);
    if (err != ESP_OK) {
        return err;
    }

    const esp_timer_create_args_t timer_args = {
        .callback = relay_release_callback,
        .name = "relay_release",
    };
    err = esp_timer_create(&timer_args, &relay_timer);
    if (err != ESP_OK) {
        return err;
    }

    ESP_LOGI(TAG, "GPIO initialized: relay=%d, open=%d, closed=%d",
             RELAY_GPIO, OPEN_GPIO, CLOSED_GPIO);
    ESP_LOGI(TAG, "Relay active level is %s; inactive bias is enabled; default pulse is %d ms",
             RELAY_ACTIVE_LEVEL ? "HIGH" : "LOW",
             CONFIG_ROLLOFFESP_RELAY_PULSE_MS);
    return ESP_OK;
}

rolloffesp_position_t rolloffesp_read_position(void)
{
    const bool open_active = gpio_get_level(OPEN_GPIO) == SWITCH_ACTIVE_LEVEL;
    const bool closed_active = gpio_get_level(CLOSED_GPIO) == SWITCH_ACTIVE_LEVEL;

    if (open_active && closed_active) {
        return ROLLOFFESP_POSITION_INVALID;
    }
    if (open_active) {
        return ROLLOFFESP_POSITION_OPEN;
    }
    if (closed_active) {
        return ROLLOFFESP_POSITION_CLOSED;
    }
    return ROLLOFFESP_POSITION_UNKNOWN;
}

bool rolloffesp_relay_is_active(void)
{
    bool active;
    portENTER_CRITICAL(&relay_state_mux);
    active = relay_active;
    portEXIT_CRITICAL(&relay_state_mux);
    return active;
}

esp_err_t rolloffesp_relay_press_default(void)
{
    return rolloffesp_relay_press_for_ms(rolloffesp_get_default_pulse_ms());
}

esp_err_t rolloffesp_set_default_pulse_ms(uint32_t duration_ms)
{
    if (duration_ms < MIN_RELAY_PULSE_MS ||
        duration_ms > CONFIG_ROLLOFFESP_RELAY_PULSE_MAX_MS) {
        return ESP_ERR_INVALID_ARG;
    }
    portENTER_CRITICAL(&relay_state_mux);
    default_pulse_ms = duration_ms;
    portEXIT_CRITICAL(&relay_state_mux);
    return ESP_OK;
}

uint32_t rolloffesp_get_default_pulse_ms(void)
{
    uint32_t duration_ms;
    portENTER_CRITICAL(&relay_state_mux);
    duration_ms = default_pulse_ms;
    portEXIT_CRITICAL(&relay_state_mux);
    return duration_ms;
}

esp_err_t rolloffesp_relay_press_for_ms(uint32_t duration_ms)
{
    if (duration_ms < MIN_RELAY_PULSE_MS ||
        duration_ms > CONFIG_ROLLOFFESP_RELAY_PULSE_MAX_MS) {
        return ESP_ERR_INVALID_ARG;
    }

    portENTER_CRITICAL(&relay_state_mux);
    if (relay_active) {
        portEXIT_CRITICAL(&relay_state_mux);
        return ESP_ERR_INVALID_STATE;
    }
    relay_active = true;
    portEXIT_CRITICAL(&relay_state_mux);

    esp_err_t err = gpio_set_level(RELAY_GPIO, RELAY_ACTIVE_LEVEL);
    if (err == ESP_OK) {
        err = esp_timer_start_once(relay_timer, (uint64_t)duration_ms * 1000U);
    }

    if (err != ESP_OK) {
        const esp_err_t release_err = gpio_set_level(RELAY_GPIO, RELAY_INACTIVE_LEVEL);
        portENTER_CRITICAL(&relay_state_mux);
        relay_active = release_err != ESP_OK;
        portEXIT_CRITICAL(&relay_state_mux);
        ESP_LOGE(TAG, "Failed to activate timed relay pulse: %s", esp_err_to_name(err));
        if (release_err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to restore relay inactive output: %s",
                     esp_err_to_name(release_err));
        }
        return err;
    }

    ESP_LOGI(TAG, "Relay button pulse started for %" PRIu32 " ms", duration_ms);
    return ESP_OK;
}
