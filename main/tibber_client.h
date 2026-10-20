#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#define TIBBER_LIVE_MAX_AGE_MS 30000

typedef struct {
    float current_a[3];
    int64_t age_ms[3];
    bool available[3];
    bool session_live[3];
    bool stream_live;
} tibber_phase_snapshot_t;

esp_err_t tibber_client_start(void);
bool tibber_client_read(float current_a[3]);
bool tibber_client_read_with_age(float current_a[3], int64_t *age_ms);
bool tibber_client_read_phase_with_age(size_t phase_index, float *current_a, int64_t *age_ms);
void tibber_client_get_snapshot(tibber_phase_snapshot_t *snapshot);
const char *tibber_client_status_text(void);
