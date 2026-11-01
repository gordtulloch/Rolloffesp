#pragma once

#include <stdbool.h>

#include "esp_err.h"

esp_err_t rolloffesp_indi_start(void);
void rolloffesp_indi_set_network_ready(bool ready);
