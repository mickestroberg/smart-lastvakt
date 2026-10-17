#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

esp_err_t tibber_client_start(void);
bool tibber_client_read(float current_a[3]);
bool tibber_client_read_with_age(float current_a[3], int64_t *age_ms);
const char *tibber_client_status_text(void);
