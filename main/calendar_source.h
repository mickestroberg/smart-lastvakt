#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <time.h>

#define CALENDAR_EVENT_LIMIT 6

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
    time_t end;
    char title[96];
} calendar_event_t;

void calendar_source_init(void);
calendar_source_state_t calendar_source_get(calendar_event_t *event);
calendar_source_state_t calendar_source_get_events(
    calendar_event_t *events,
    size_t capacity,
    size_t *count);
