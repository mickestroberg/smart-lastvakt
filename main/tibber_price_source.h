#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

#include "esp_err.h"

/*
 * Read-only Tibber quarter-price shadow source.
 *
 * This module has deliberately no dependency on Shelly, COALA, contactors or
 * any relay/output API.  Its result is observational data only.  It keeps the
 * current local day's Tibber `total` prices in RAM, has no persistent cache,
 * does not prefetch tomorrow, does not include grid tariffs and does not feed
 * the UI or a physical output.  Missing configuration, unreliable time,
 * incomplete JSON or a network/API error therefore produces no suggestion.
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

esp_err_t tibber_price_source_start(void);
tibber_price_source_state_t tibber_price_source_get(tibber_price_shadow_snapshot_t *snapshot);
const char *tibber_price_source_status_text(void);
