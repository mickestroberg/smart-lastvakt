#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define COOKING_TIMER_COUNT 4
#define COOKING_TIMER_MAX_SECONDS ((99 * 60 * 60) + (59 * 60) + 59)

typedef enum {
    COOKING_TIMER_IDLE,
    COOKING_TIMER_READY,
    COOKING_TIMER_RUNNING,
    COOKING_TIMER_PAUSED,
    COOKING_TIMER_FINISHED,
} cooking_timer_state_t;

typedef struct {
    cooking_timer_state_t state;
    int64_t remaining_ms;
    int64_t deadline_ms;
} cooking_timer_slot_t;

typedef struct {
    cooking_timer_slot_t slots[COOKING_TIMER_COUNT];
} cooking_timers_t;

void cooking_timers_init(cooking_timers_t *timers);
bool cooking_timer_adjust(
    cooking_timers_t *timers,
    size_t index,
    int32_t delta_seconds,
    int64_t now_ms);
bool cooking_timer_toggle(cooking_timers_t *timers, size_t index, int64_t now_ms);
void cooking_timer_reset(cooking_timers_t *timers, size_t index);
uint32_t cooking_timers_update(cooking_timers_t *timers, int64_t now_ms);
int64_t cooking_timer_remaining_ms(
    const cooking_timers_t *timers,
    size_t index,
    int64_t now_ms);
uint32_t cooking_timers_finished_mask(const cooking_timers_t *timers);
void cooking_timers_acknowledge_finished(cooking_timers_t *timers);
