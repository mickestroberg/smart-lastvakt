#pragma once

#include <stdbool.h>

typedef enum {
    WEATHER_SOURCE_SYNCING,
    WEATHER_SOURCE_READY,
    WEATHER_SOURCE_STALE,
} weather_source_state_t;

typedef struct {
    bool available;
    float temperature_c;
    int weather_code;
    bool is_day;
} weather_sample_t;

void weather_source_init(void);
weather_source_state_t weather_source_get(weather_sample_t *sample);

