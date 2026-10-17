#pragma once

#include <stdbool.h>

#include "esp_err.h"

typedef struct {
    bool protection_available;
    bool tripped;
    float limit_a;
    float current_l1_a;
    float trip_l1_a;
} microwave_guard_status_t;

esp_err_t microwave_guard_start(void);
microwave_guard_status_t microwave_guard_get(void);
