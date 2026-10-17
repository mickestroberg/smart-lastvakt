#include "cooking_timers.h"

#include <limits.h>
#include <string.h>

static bool valid_index(size_t index)
{
    return index < COOKING_TIMER_COUNT;
}

static int64_t clamp_remaining_ms(int64_t value)
{
    const int64_t maximum_ms = (int64_t)COOKING_TIMER_MAX_SECONDS * 1000;
    if (value < 0) return 0;
    if (value > maximum_ms) return maximum_ms;
    return value;
}

void cooking_timers_init(cooking_timers_t *timers)
{
    if (timers == NULL) return;
    memset(timers, 0, sizeof(*timers));
}

int64_t cooking_timer_remaining_ms(
    const cooking_timers_t *timers,
    size_t index,
    int64_t now_ms)
{
    if (timers == NULL || !valid_index(index)) return 0;

    const cooking_timer_slot_t *slot = &timers->slots[index];
    if (slot->state != COOKING_TIMER_RUNNING) {
        return clamp_remaining_ms(slot->remaining_ms);
    }

    return clamp_remaining_ms(slot->deadline_ms - now_ms);
}

bool cooking_timer_adjust(
    cooking_timers_t *timers,
    size_t index,
    int32_t delta_seconds,
    int64_t now_ms)
{
    if (timers == NULL || !valid_index(index)) return false;

    cooking_timer_slot_t *slot = &timers->slots[index];
    if (slot->state == COOKING_TIMER_RUNNING) return false;

    const int64_t before_ms = cooking_timer_remaining_ms(timers, index, now_ms);
    const int64_t delta_ms = (int64_t)delta_seconds * 1000;
    int64_t after_ms;
    if (delta_ms > 0 && before_ms > INT64_MAX - delta_ms) {
        after_ms = INT64_MAX;
    } else {
        after_ms = before_ms + delta_ms;
    }
    after_ms = clamp_remaining_ms(after_ms);

    slot->remaining_ms = after_ms;
    slot->deadline_ms = 0;
    if (after_ms == 0) {
        slot->state = COOKING_TIMER_IDLE;
    } else if (slot->state != COOKING_TIMER_PAUSED) {
        slot->state = COOKING_TIMER_READY;
    }
    return after_ms != before_ms;
}

bool cooking_timer_toggle(cooking_timers_t *timers, size_t index, int64_t now_ms)
{
    if (timers == NULL || !valid_index(index)) return false;

    cooking_timer_slot_t *slot = &timers->slots[index];
    if (slot->state == COOKING_TIMER_RUNNING) {
        slot->remaining_ms = cooking_timer_remaining_ms(timers, index, now_ms);
        slot->deadline_ms = 0;
        slot->state = slot->remaining_ms > 0 ? COOKING_TIMER_PAUSED : COOKING_TIMER_FINISHED;
        return true;
    }

    if ((slot->state == COOKING_TIMER_READY || slot->state == COOKING_TIMER_PAUSED) &&
        slot->remaining_ms > 0) {
        slot->deadline_ms = now_ms + slot->remaining_ms;
        slot->state = COOKING_TIMER_RUNNING;
        return true;
    }
    return false;
}

void cooking_timer_reset(cooking_timers_t *timers, size_t index)
{
    if (timers == NULL || !valid_index(index)) return;
    memset(&timers->slots[index], 0, sizeof(timers->slots[index]));
}

uint32_t cooking_timers_update(cooking_timers_t *timers, int64_t now_ms)
{
    if (timers == NULL) return 0;

    uint32_t newly_finished = 0;
    for (size_t i = 0; i < COOKING_TIMER_COUNT; ++i) {
        cooking_timer_slot_t *slot = &timers->slots[i];
        if (slot->state == COOKING_TIMER_RUNNING && now_ms >= slot->deadline_ms) {
            slot->state = COOKING_TIMER_FINISHED;
            slot->remaining_ms = 0;
            slot->deadline_ms = 0;
            newly_finished |= (uint32_t)1U << i;
        }
    }
    return newly_finished;
}

uint32_t cooking_timers_finished_mask(const cooking_timers_t *timers)
{
    if (timers == NULL) return 0;

    uint32_t mask = 0;
    for (size_t i = 0; i < COOKING_TIMER_COUNT; ++i) {
        if (timers->slots[i].state == COOKING_TIMER_FINISHED) {
            mask |= (uint32_t)1U << i;
        }
    }
    return mask;
}

void cooking_timers_acknowledge_finished(cooking_timers_t *timers)
{
    if (timers == NULL) return;

    for (size_t i = 0; i < COOKING_TIMER_COUNT; ++i) {
        if (timers->slots[i].state == COOKING_TIMER_FINISHED) {
            cooking_timer_reset(timers, i);
        }
    }
}
