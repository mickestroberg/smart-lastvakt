#pragma once

#include <stdbool.h>
#include <time.h>

typedef enum {
    CALENDAR_SOURCE_DISABLED,
    CALENDAR_SOURCE_SYNCING,
    CALENDAR_SOURCE_READY,
    CALENDAR_SOURCE_ERROR,
} calendar_source_state_t;

typedef struct {
    bool available;
    bool all_day;
    time_t start;
    char title[96];
} calendar_event_t;

void calendar_source_init(void);
calendar_source_state_t calendar_source_get(calendar_event_t *event);
