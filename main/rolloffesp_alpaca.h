#pragma once

#include <stdbool.h>

#include "esp_err.h"

esp_err_t rolloffesp_alpaca_start(void);
void rolloffesp_alpaca_set_network_ready(bool ready);
