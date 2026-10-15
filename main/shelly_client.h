#pragma once

#include <stdbool.h>

#include "esp_err.h"

typedef enum {
    SHELLY_DISABLED,
    SHELLY_CONNECTING,
    SHELLY_READY,
    SHELLY_STALE,
} shelly_state_t;

typedef struct {
    bool available;
    bool output_on;
    float power_w;
    float current_a;
    float voltage_v;
    float temperature_c;
    double total_wh;
} shelly_sample_t;

esp_err_t shelly_client_start(void);
shelly_state_t shelly_client_get(shelly_sample_t *sample);
esp_err_t shelly_client_request_output(bool on);
bool shelly_client_command_pending(void);
const char *shelly_client_status_text(void);

