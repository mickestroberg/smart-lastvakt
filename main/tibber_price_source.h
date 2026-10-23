#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

#include "esp_err.h"

/*
 * Read-only Tibber quarter-price shadow source.
 *
 * This module deliberately has no dependency on Shelly, COALA, contactors or
 * any relay/output API. Its result is observational data only. It keeps a
 * validated today/tomorrow price book in PSRAM and lets the UI copy a bounded
 * time window. It has no persistent cache, does not include grid tariffs and
 * cannot affect a physical output. Missing configuration or unreliable time
 * therefore produces no current suggestion. A failed refresh never destroys
 * the last complete in-memory book.
 */

typedef enum {
    TIBBER_PRICE_SOURCE_DISABLED = 0,
    TIBBER_PRICE_SOURCE_NOT_CONFIGURED,
    TIBBER_PRICE_SOURCE_WAITING_TIME,
    TIBBER_PRICE_SOURCE_WAITING_NETWORK,
    TIBBER_PRICE_SOURCE_SYNCING,
    TIBBER_PRICE_SOURCE_READY,
    TIBBER_PRICE_SOURCE_ERROR,
} tibber_price_source_state_t;

typedef enum {
    TIBBER_PRICE_SHADOW_NO_LIMIT = 0,
    TIBBER_PRICE_SHADOW_HALF_LIMIT,
    TIBBER_PRICE_SHADOW_FULL_BLOCK,
} tibber_price_shadow_tier_t;

typedef struct {
    bool available;
    time_t starts_at;
    time_t ends_at;
    float total_price;
    char currency[4];
    uint16_t descending_rank;
    uint16_t day_slot_count;
    tibber_price_shadow_tier_t suggested_tier;
} tibber_price_shadow_snapshot_t;

typedef struct {
    time_t starts_at;
    time_t ends_at;
    float total_price;
    char currency[4];
    uint16_t day_rank;
    uint16_t day_slot_count;
    bool is_tomorrow;
    tibber_price_shadow_tier_t suggested_tier;
} tibber_price_slot_snapshot_t;

typedef struct {
    bool available;
    bool today_complete;
    bool tomorrow_available;
    bool tomorrow_complete;
    uint32_t generation;
    time_t fetched_at;
    time_t coverage_start;
    time_t coverage_end;
    uint16_t slot_count;
    uint16_t today_slot_count;
    uint16_t tomorrow_slot_count;
    char currency[4];
} tibber_price_book_metadata_t;

esp_err_t tibber_price_source_start(void);
tibber_price_source_state_t tibber_price_source_get(tibber_price_shadow_snapshot_t *snapshot);

/*
 * Copies entries that overlap [window_start, window_end) from one stable book
 * generation. The returned count is the number actually copied, never greater
 * than capacity. Passing slots == NULL or capacity == 0 copies metadata only.
 * This is a RAM-only read; it never starts a network request.
 */
size_t tibber_price_source_copy_window(
    time_t window_start,
    time_t window_end,
    tibber_price_slot_snapshot_t *slots,
    size_t capacity,
    tibber_price_book_metadata_t *metadata);

const char *tibber_price_source_status_text(void);
