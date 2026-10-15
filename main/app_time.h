#pragma once

#include <stddef.h>

void app_time_init(void);
void app_time_start_network_sync(void);
void app_time_format(char *clock_text, size_t clock_size, char *date_text, size_t date_size);
const char *app_time_status_text(void);

