#pragma once

#include <stdbool.h>

typedef struct {
    float current_a[3];
    float percent[3];
    float main_fuse_a;
    bool simulated;
} phase_sample_t;

void phase_source_init(void);
phase_sample_t phase_source_read(void);
const char *phase_source_status_text(void);

