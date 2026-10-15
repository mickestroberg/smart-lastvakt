#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"

esp_err_t network_setup_start(void);
bool network_setup_wait_connected(TickType_t timeout);
bool network_setup_is_connected(void);
const char *network_setup_status_text(void);
const char *network_setup_address_text(void);
const char *network_setup_url(void);
