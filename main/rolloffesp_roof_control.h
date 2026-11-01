#pragma once

#include "esp_err.h"

typedef enum {
    ROLLOFFESP_ROOF_UNKNOWN,
    ROLLOFFESP_ROOF_OPEN,
    ROLLOFFESP_ROOF_CLOSED,
    ROLLOFFESP_ROOF_OPENING,
    ROLLOFFESP_ROOF_CLOSING,
    ROLLOFFESP_ROOF_INVALID,
} rolloffesp_roof_status_t;

rolloffesp_roof_status_t rolloffesp_roof_status(void);
esp_err_t rolloffesp_roof_open(void);
esp_err_t rolloffesp_roof_close(void);
esp_err_t rolloffesp_roof_abort(void);
