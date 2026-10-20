#pragma once

#include <stdbool.h>

typedef enum {
    PHASE_SOURCE_UNAVAILABLE,
    PHASE_SOURCE_STALE,
    PHASE_SOURCE_PARTIAL,
    PHASE_SOURCE_LIVE,
} phase_source_state_t;

typedef struct {
    float current_a[3];
    float percent[3];
    float main_fuse_a;
    bool available[3];
    bool fresh[3];
    phase_source_state_t state;
} phase_sample_t;

void phase_source_init(void);
phase_sample_t phase_source_read(void);
const char *phase_source_status_text(void);

