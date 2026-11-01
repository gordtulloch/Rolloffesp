#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

typedef enum {
    ROLLOFFESP_POSITION_UNKNOWN,
    ROLLOFFESP_POSITION_OPEN,
    ROLLOFFESP_POSITION_CLOSED,
    ROLLOFFESP_POSITION_INVALID,
} rolloffesp_position_t;

esp_err_t rolloffesp_hardware_init(void);
rolloffesp_position_t rolloffesp_read_position(void);
esp_err_t rolloffesp_relay_press_default(void);
esp_err_t rolloffesp_relay_press_for_ms(uint32_t duration_ms);
esp_err_t rolloffesp_set_default_pulse_ms(uint32_t duration_ms);
uint32_t rolloffesp_get_default_pulse_ms(void);
bool rolloffesp_relay_is_active(void);
