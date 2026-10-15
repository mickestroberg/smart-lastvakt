#pragma once

#include <stdbool.h>

#include "esp_err.h"

esp_err_t tibber_client_start(void);
bool tibber_client_read(float current_a[3]);
const char *tibber_client_status_text(void);
