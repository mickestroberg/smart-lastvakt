#pragma once

#include <stddef.h>

#include "esp_heap_caps.h"

static inline void *lastvakt_lvgl_malloc(size_t size)
{
    void *memory = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (memory == NULL) {
        memory = heap_caps_malloc(size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    return memory;
}

static inline void lastvakt_lvgl_free(void *memory)
{
    heap_caps_free(memory);
}

static inline void *lastvakt_lvgl_realloc(void *memory, size_t size)
{
    void *resized = heap_caps_realloc(memory, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (resized == NULL && size != 0) {
        resized = heap_caps_realloc(memory, size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    return resized;
}

#undef LV_MEM_CUSTOM_ALLOC
#undef LV_MEM_CUSTOM_FREE
#undef LV_MEM_CUSTOM_REALLOC
#define LV_MEM_CUSTOM_ALLOC lastvakt_lvgl_malloc
#define LV_MEM_CUSTOM_FREE lastvakt_lvgl_free
#define LV_MEM_CUSTOM_REALLOC lastvakt_lvgl_realloc
