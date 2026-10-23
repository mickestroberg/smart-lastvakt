#pragma once

#include <stddef.h>

typedef enum {
    APP_TIME_QUALITY_UNTRUSTED = 0,
    APP_TIME_QUALITY_RTC,
    APP_TIME_QUALITY_NETWORK,
} app_time_quality_t;

void app_time_init(void);
void app_time_start_network_sync(void);
void app_time_format(char *clock_text, size_t clock_size, char *date_text, size_t date_size);
const char *app_time_status_text(void);
app_time_quality_t app_time_get_quality(void);

