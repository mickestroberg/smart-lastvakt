#include "tibber_price_source.h"

#include <ctype.h>
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app_config.h"
#include "app_time.h"
#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "network_setup.h"
#include "sdkconfig.h"

#define TIBBER_PRICE_API_URL "https://api.tibber.com/v1-beta/gql"
#define TIBBER_PRICE_USER_AGENT "smart-lastvakt-price-shadow/0.2 ESP32-S3"
#define TIBBER_PRICE_RESPONSE_BYTES (32U * 1024U)
#define TIBBER_PRICE_OBJECT_BYTES 384U
#define TIBBER_PRICE_MAX_SLOTS 100U
#define TIBBER_PRICE_QUARTER_SECONDS (15 * 60)
#define TIBBER_PRICE_INITIAL_DELAY_MIN_SECONDS 45U
#define TIBBER_PRICE_INITIAL_DELAY_JITTER_SECONDS 75U
#define TIBBER_PRICE_TOMORROW_PUBLISH_HOUR 13
#define TIBBER_PRICE_TOMORROW_PUBLISH_MINUTE 5
#define TIBBER_PRICE_TOMORROW_JITTER_SECONDS (30U * 60U)
#define TIBBER_PRICE_RETRY_MS (2 * 60 * 1000)
#define TIBBER_PRICE_RETRY_MAX_MS (60 * 60 * 1000)
#define TIBBER_PRICE_UNCONFIGURED_RETRY_MS (5 * 60 * 1000)

typedef struct {
    const char *begin;
    const char *end;
} json_slice_t;

typedef struct {
    char *data;
    size_t length;
    bool overflow;
} http_response_t;

typedef struct {
    time_t starts_at;
    double total;
    char currency[4];
    uint16_t rank;
} price_entry_t;

typedef struct {
    bool valid;
    time_t day_start;
    time_t day_end;
    size_t count;
    char currency[4];
    price_entry_t entries[TIBBER_PRICE_MAX_SLOTS];
} price_day_t;

typedef struct {
    bool valid;
    uint32_t generation;
    time_t fetched_at;
    time_t coverage_start;
    time_t coverage_end;
    size_t count;
    char currency[4];
    price_day_t today;
    price_day_t tomorrow;
} price_book_t;

static const char *TAG = "tibber_price";
static StaticSemaphore_t s_lock_storage;
static SemaphoreHandle_t s_lock;
static tibber_price_source_state_t s_state = TIBBER_PRICE_SOURCE_DISABLED;
static tibber_price_shadow_snapshot_t s_snapshot;
static price_book_t *s_active_book;
static uint32_t s_generation;
static bool s_started;

static void lock_source(void)
{
    configASSERT(s_lock != NULL);
    (void)xSemaphoreTake(s_lock, portMAX_DELAY);
}

static void unlock_source(void)
{
    (void)xSemaphoreGive(s_lock);
}

static const char *skip_ws(const char *cursor, const char *end)
{
    while (cursor < end && isspace((unsigned char)*cursor)) ++cursor;
    return cursor;
}
static bool is_hex(char value)
{
    return (value >= '0' && value <= '9') || (value >= 'a' && value <= 'f') ||
           (value >= 'A' && value <= 'F');
}

static const char *skip_json_string(const char *cursor, const char *end)
{
    if (cursor >= end || *cursor != '"') return NULL;
    ++cursor;
    while (cursor < end) {
        const unsigned char value = (unsigned char)*cursor++;
        if (value == '"') return cursor;
        if (value < 0x20) return NULL;
        if (value != '\\') continue;
        if (cursor >= end) return NULL;
        const char escape = *cursor++;
        if (escape == 'u') {
            if (end - cursor < 4) return NULL;
            for (int index = 0; index < 4; ++index) {
                if (!is_hex(cursor[index])) return NULL;
            }
            cursor += 4;
        } else if (strchr("\"\\/bfnrt", escape) == NULL) {
            return NULL;
        }
    }
    return NULL;
}

static const char *skip_json_value(const char *cursor, const char *end);

static const char *skip_json_object(const char *cursor, const char *end)
{
    if (cursor >= end || *cursor != '{') return NULL;
    cursor = skip_ws(cursor + 1, end);
    if (cursor < end && *cursor == '}') return cursor + 1;
    while (cursor < end) {
        cursor = skip_json_string(cursor, end);
        if (cursor == NULL) return NULL;
        cursor = skip_ws(cursor, end);
        if (cursor >= end || *cursor != ':') return NULL;
        cursor = skip_json_value(cursor + 1, end);
        if (cursor == NULL) return NULL;
        cursor = skip_ws(cursor, end);
        if (cursor < end && *cursor == '}') return cursor + 1;
        if (cursor >= end || *cursor != ',') return NULL;
        cursor = skip_ws(cursor + 1, end);
    }
    return NULL;
}

static const char *skip_json_array(const char *cursor, const char *end)
{
    if (cursor >= end || *cursor != '[') return NULL;
    cursor = skip_ws(cursor + 1, end);
    if (cursor < end && *cursor == ']') return cursor + 1;
    while (cursor < end) {
        cursor = skip_json_value(cursor, end);
        if (cursor == NULL) return NULL;
        cursor = skip_ws(cursor, end);
        if (cursor < end && *cursor == ']') return cursor + 1;
        if (cursor >= end || *cursor != ',') return NULL;
        cursor = skip_ws(cursor + 1, end);
    }
    return NULL;
}

static bool token_at(const char *cursor, const char *end, const char *token)
{
    const size_t length = strlen(token);
    if ((size_t)(end - cursor) < length || memcmp(cursor, token, length) != 0) return false;
    if ((size_t)(end - cursor) == length) return true;
    const char next = cursor[length];
    return isspace((unsigned char)next) || next == ',' || next == ']' || next == '}';
}

static const char *skip_json_value(const char *cursor, const char *end)
{
    cursor = skip_ws(cursor, end);
    if (cursor >= end) return NULL;
    if (*cursor == '"') return skip_json_string(cursor, end);
    if (*cursor == '{') return skip_json_object(cursor, end);
    if (*cursor == '[') return skip_json_array(cursor, end);
    if (token_at(cursor, end, "true")) return cursor + 4;
    if (token_at(cursor, end, "false")) return cursor + 5;
    if (token_at(cursor, end, "null")) return cursor + 4;
    if (*cursor != '-' && !isdigit((unsigned char)*cursor)) return NULL;
    char *number_end = NULL;
    (void)strtod(cursor, &number_end);
    if (number_end == cursor || number_end > end) return NULL;
    return number_end;
}

static bool raw_key_equals(const char *begin, const char *end, const char *expected)
{
    if (begin >= end || *begin != '"' || end[-1] != '"') return false;
    ++begin;
    --end;
    const size_t expected_length = strlen(expected);
    return (size_t)(end - begin) == expected_length && memchr(begin, '\\', expected_length) == NULL &&
           memcmp(begin, expected, expected_length) == 0;
}

static bool json_object_member(json_slice_t object, const char *name, json_slice_t *value)
{
    const char *cursor = skip_ws(object.begin, object.end);
    if (cursor >= object.end || *cursor != '{') return false;
    cursor = skip_ws(cursor + 1, object.end);
    while (cursor < object.end && *cursor != '}') {
        const char *key_begin = cursor;
        const char *key_end = skip_json_string(cursor, object.end);
        if (key_end == NULL) return false;
        cursor = skip_ws(key_end, object.end);
        if (cursor >= object.end || *cursor != ':') return false;
        const char *value_begin = skip_ws(cursor + 1, object.end);
        const char *value_end = skip_json_value(value_begin, object.end);
        if (value_end == NULL) return false;
        if (raw_key_equals(key_begin, key_end, name)) {
            if (value != NULL) *value = (json_slice_t){.begin = value_begin, .end = value_end};
            return true;
        }
        cursor = skip_ws(value_end, object.end);
        if (cursor < object.end && *cursor == ',') cursor = skip_ws(cursor + 1, object.end);
        else if (cursor >= object.end || *cursor != '}') return false;
    }
    return false;
}

static bool json_empty_or_null(json_slice_t value)
{
    const char *cursor = skip_ws(value.begin, value.end);
    if (token_at(cursor, value.end, "null")) return true;
    if (cursor >= value.end || *cursor != '[') return false;
    cursor = skip_ws(cursor + 1, value.end);
    return cursor < value.end && *cursor == ']';
}

static bool leap_year(int year)
{
    return year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
}

static int days_in_month(int year, int month)
{
    static const uint8_t days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month < 1 || month > 12) return 0;
    if (month == 2 && leap_year(year)) return 29;
    return days[month - 1];
}

static int64_t days_from_civil(int year, unsigned month, unsigned day)
{
    year -= month <= 2;
    const int era = (year >= 0 ? year : year - 399) / 400;
    const unsigned year_of_era = (unsigned)(year - era * 400);
    const unsigned adjusted_month = month > 2 ? month - 3U : month + 9U;
    const unsigned day_of_year = (153U * adjusted_month + 2U) / 5U + day - 1U;
    const unsigned day_of_era = year_of_era * 365U + year_of_era / 4U - year_of_era / 100U + day_of_year;
    return (int64_t)era * 146097LL + (int64_t)day_of_era - 719468LL;
}

static bool parse_two_digits(const char *text, int *value)
{
    if (!isdigit((unsigned char)text[0]) || !isdigit((unsigned char)text[1])) return false;
    *value = (text[0] - '0') * 10 + (text[1] - '0');
    return true;
}

static bool parse_starts_at(const char *text, time_t *result)
{
    if (text == NULL || result == NULL || strlen(text) < 20) return false;
    int year = 0;
    int month = 0;
    int day = 0;
    int hour = 0;
    int minute = 0;
    int second = 0;
    for (int index = 0; index < 4; ++index) {
        if (!isdigit((unsigned char)text[index])) return false;
        year = year * 10 + (text[index] - '0');
    }
    if (text[4] != '-' || !parse_two_digits(text + 5, &month) || text[7] != '-' ||
        !parse_two_digits(text + 8, &day) || (text[10] != 'T' && text[10] != 't') ||
        !parse_two_digits(text + 11, &hour) || text[13] != ':' ||
        !parse_two_digits(text + 14, &minute) || text[16] != ':' ||
        !parse_two_digits(text + 17, &second)) {
        return false;
    }
    if (year < 2024 || year > 2100 || day < 1 || day > days_in_month(year, month) ||
        hour > 23 || minute > 59 || second > 60) {
        return false;
    }

    const char *cursor = text + 19;
    if (*cursor == '.') {
        ++cursor;
        if (!isdigit((unsigned char)*cursor)) return false;
        while (isdigit((unsigned char)*cursor)) ++cursor;
    }

    int offset_seconds = 0;
    if ((*cursor == 'Z' || *cursor == 'z') && cursor[1] == '\0') {
        offset_seconds = 0;
    } else {
        const int sign = *cursor == '+' ? 1 : (*cursor == '-' ? -1 : 0);
        int offset_hour = 0;
        int offset_minute = 0;
        if (sign == 0 || !parse_two_digits(cursor + 1, &offset_hour) || cursor[3] != ':' ||
            !parse_two_digits(cursor + 4, &offset_minute) || cursor[6] != '\0' ||
            offset_hour > 14 || offset_minute > 59) {
            return false;
        }
        offset_seconds = sign * (offset_hour * 3600 + offset_minute * 60);
    }

    const int64_t epoch = days_from_civil(year, (unsigned)month, (unsigned)day) * 86400LL +
                          hour * 3600LL + minute * 60LL + second - offset_seconds;
    *result = (time_t)epoch;
    return (int64_t)*result == epoch;
}

static esp_err_t parse_price_object(json_slice_t object, price_entry_t *entry)
{
    const size_t length = (size_t)(object.end - object.begin);
    if (length == 0 || length >= TIBBER_PRICE_OBJECT_BYTES) return ESP_ERR_INVALID_SIZE;
    char object_text[TIBBER_PRICE_OBJECT_BYTES];
    memcpy(object_text, object.begin, length);
    object_text[length] = '\0';

    cJSON *root = cJSON_Parse(object_text);
    if (root == NULL) return ESP_ERR_INVALID_RESPONSE;
    cJSON *total = cJSON_GetObjectItemCaseSensitive(root, "total");
    cJSON *starts_at = cJSON_GetObjectItemCaseSensitive(root, "startsAt");
    cJSON *currency = cJSON_GetObjectItemCaseSensitive(root, "currency");
    const bool valid = cJSON_IsNumber(total) && isfinite(total->valuedouble) &&
                       fabs(total->valuedouble) <= FLT_MAX && cJSON_IsString(starts_at) &&
                       starts_at->valuestring != NULL && cJSON_IsString(currency) &&
                       currency->valuestring != NULL && strlen(currency->valuestring) == 3U &&
                       isupper((unsigned char)currency->valuestring[0]) &&
                       isupper((unsigned char)currency->valuestring[1]) &&
                       isupper((unsigned char)currency->valuestring[2]) &&
                       parse_starts_at(starts_at->valuestring, &entry->starts_at);
    if (valid) {
        entry->total = total->valuedouble;
        memcpy(entry->currency, currency->valuestring, 3U);
        entry->currency[3] = '\0';
    }
    cJSON_Delete(root);
    return valid ? ESP_OK : ESP_ERR_INVALID_RESPONSE;
}

static esp_err_t parse_price_array(json_slice_t array, price_day_t *day)
{
    const char *cursor = skip_ws(array.begin, array.end);
    if (cursor >= array.end || *cursor != '[') return ESP_ERR_INVALID_RESPONSE;
    cursor = skip_ws(cursor + 1, array.end);
    size_t count = 0;
    while (cursor < array.end && *cursor != ']') {
        if (count >= TIBBER_PRICE_MAX_SLOTS || *cursor != '{') return ESP_ERR_INVALID_SIZE;
        const char *item_end = skip_json_value(cursor, array.end);
        if (item_end == NULL) return ESP_ERR_INVALID_RESPONSE;
        esp_err_t err = parse_price_object((json_slice_t){.begin = cursor, .end = item_end}, &day->entries[count]);
        if (err != ESP_OK) return err;
        if (count == 0) memcpy(day->currency, day->entries[count].currency, sizeof(day->currency));
        else if (strcmp(day->currency, day->entries[count].currency) != 0) return ESP_ERR_INVALID_RESPONSE;
        ++count;
        cursor = skip_ws(item_end, array.end);
        if (cursor < array.end && *cursor == ',') cursor = skip_ws(cursor + 1, array.end);
        else if (cursor >= array.end || *cursor != ']') return ESP_ERR_INVALID_RESPONSE;
    }
    if (cursor >= array.end || *cursor != ']') return ESP_ERR_INVALID_RESPONSE;
    day->count = count;
    return ESP_OK;
}

static esp_err_t parse_graphql_response(const char *json, size_t length, price_book_t *book)
{
    json_slice_t root = {.begin = json, .end = json + length};
    json_slice_t value;
    if (json_object_member(root, "errors", &value) && !json_empty_or_null(value)) {
        ESP_LOGW(TAG, "Tibber GraphQL response contained one or more errors");
        return ESP_ERR_INVALID_RESPONSE;
    }
    if (!json_object_member(root, "data", &value)) return ESP_ERR_INVALID_RESPONSE;
    json_slice_t viewer;
    json_slice_t home;
    json_slice_t subscription;
    json_slice_t price_info;
    json_slice_t today;
    json_slice_t tomorrow;
    if (!json_object_member(value, "viewer", &viewer) || !json_object_member(viewer, "home", &home) ||
        !json_object_member(home, "currentSubscription", &subscription) ||
        !json_object_member(subscription, "priceInfo", &price_info) ||
        !json_object_member(price_info, "today", &today) ||
        !json_object_member(price_info, "tomorrow", &tomorrow)) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    esp_err_t err = parse_price_array(today, &book->today);
    if (err == ESP_OK && json_empty_or_null(tomorrow)) {
        memset(&book->tomorrow, 0, sizeof(book->tomorrow));
    } else if (err == ESP_OK) {
        err = parse_price_array(tomorrow, &book->tomorrow);
    }
    return err;
}

static int compare_start(const void *left, const void *right)
{
    const price_entry_t *a = left;
    const price_entry_t *b = right;
    return a->starts_at < b->starts_at ? -1 : (a->starts_at > b->starts_at ? 1 : 0);
}

static time_t local_day_start(time_t value)
{
    struct tm local = {0};
    localtime_r(&value, &local);
    local.tm_hour = 0;
    local.tm_min = 0;
    local.tm_sec = 0;
    local.tm_isdst = -1;
    return mktime(&local);
}

static time_t next_local_day_start(time_t day_start)
{
    struct tm local = {0};
    localtime_r(&day_start, &local);
    local.tm_mday += 1;
    local.tm_isdst = -1;
    return mktime(&local);
}

static bool valid_local_day(price_day_t *day, time_t day_start)
{
    const time_t day_end = next_local_day_start(day_start);
    if (day_start < 0 || day_end <= day_start || (day_end - day_start) % TIBBER_PRICE_QUARTER_SECONDS != 0) {
        return false;
    }
    const size_t expected_count = (size_t)((day_end - day_start) / TIBBER_PRICE_QUARTER_SECONDS);
    if ((expected_count != 92U && expected_count != 96U && expected_count != 100U) ||
        day->count != expected_count) {
        return false;
    }

    qsort(day->entries, day->count, sizeof(day->entries[0]), compare_start);
    for (size_t index = 0; index < day->count; ++index) {
        if (day->entries[index].starts_at != day_start + (time_t)index * TIBBER_PRICE_QUARTER_SECONDS) {
            return false;
        }
    }
    day->day_start = day_start;
    day->day_end = day_end;
    day->valid = true;
    return true;
}

static bool rank_before(const price_entry_t *left, const price_entry_t *right)
{
    if (left->total != right->total) return left->total > right->total;
    return left->starts_at < right->starts_at;
}

static void rank_prices(price_day_t *day)
{
    uint8_t order[TIBBER_PRICE_MAX_SLOTS];
    for (size_t index = 0; index < day->count; ++index) order[index] = (uint8_t)index;
    for (size_t index = 1; index < day->count; ++index) {
        const uint8_t candidate = order[index];
        size_t position = index;
        while (position > 0 && rank_before(&day->entries[candidate], &day->entries[order[position - 1]])) {
            order[position] = order[position - 1];
            --position;
        }
        order[position] = candidate;
    }
    for (size_t position = 0; position < day->count; ++position) {
        day->entries[order[position]].rank = (uint16_t)(position + 1U);
    }
}

static bool validate_price_book(price_book_t *book, time_t fetched_at)
{
    const time_t today_start = local_day_start(fetched_at);
    const time_t tomorrow_start = next_local_day_start(today_start);
    if (today_start < 0 || tomorrow_start <= today_start ||
        !valid_local_day(&book->today, today_start)) {
        return false;
    }

    rank_prices(&book->today);
    book->coverage_start = book->today.day_start;
    book->coverage_end = book->today.day_end;
    book->count = book->today.count;
    memcpy(book->currency, book->today.currency, sizeof(book->currency));

    if (book->tomorrow.count > 0) {
        if (!valid_local_day(&book->tomorrow, tomorrow_start) ||
            strcmp(book->today.currency, book->tomorrow.currency) != 0) {
            return false;
        }
        rank_prices(&book->tomorrow);
        book->coverage_end = book->tomorrow.day_end;
        book->count += book->tomorrow.count;
    } else {
        memset(&book->tomorrow, 0, sizeof(book->tomorrow));
    }

    book->fetched_at = fetched_at;
    book->valid = true;
    return true;
}

static bool valid_home_id(const char *home_id)
{
    if (home_id == NULL || *home_id == '\0') return false;
    for (const unsigned char *cursor = (const unsigned char *)home_id; *cursor != '\0'; ++cursor) {
        if (!isalnum(*cursor) && *cursor != '-' && *cursor != '_' && *cursor != '.' && *cursor != ':') return false;
    }
    return true;
}

static esp_err_t http_event(esp_http_client_event_t *event)
{
    if (event->event_id != HTTP_EVENT_ON_DATA || event->data_len <= 0) return ESP_OK;
    http_response_t *response = event->user_data;
    const size_t required = response->length + (size_t)event->data_len + 1U;
    if (required > TIBBER_PRICE_RESPONSE_BYTES) {
        response->overflow = true;
        return ESP_ERR_NO_MEM;
    }
    memcpy(response->data + response->length, event->data, (size_t)event->data_len);
    response->length += (size_t)event->data_len;
    response->data[response->length] = '\0';
    return ESP_OK;
}

static esp_err_t fetch_price_book(const app_config_t *app_config, price_book_t *book)
{
    if (!valid_home_id(app_config->tibber_home_id)) return ESP_ERR_INVALID_ARG;

    static const char query_format[] =
        "{\"query\":\"query PriceBook { viewer { home(id: \\\"%s\\\") { currentSubscription { "
        "priceInfo(resolution: QUARTER_HOURLY) { today { total startsAt currency } "
        "tomorrow { total startsAt currency } } } } } }\"}";
    char request_body[768];
    const int body_length = snprintf(request_body, sizeof(request_body), query_format, app_config->tibber_home_id);
    if (body_length <= 0 || (size_t)body_length >= sizeof(request_body)) return ESP_ERR_INVALID_SIZE;

    http_response_t response = {
        .data = heap_caps_calloc(1, TIBBER_PRICE_RESPONSE_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT),
    };
    if (response.data == NULL) return ESP_ERR_NO_MEM;

    char authorization[LASTVAKT_TIBBER_TOKEN_MAX + 16];
    const int authorization_length = snprintf(
        authorization,
        sizeof(authorization),
        "Bearer %s",
        app_config->tibber_token);
    if (authorization_length <= 0 || (size_t)authorization_length >= sizeof(authorization)) {
        free(response.data);
        return ESP_ERR_INVALID_SIZE;
    }

    esp_http_client_config_t http_config = {
        .url = TIBBER_PRICE_API_URL,
        .method = HTTP_METHOD_POST,
        .event_handler = http_event,
        .user_data = &response,
        .user_agent = TIBBER_PRICE_USER_AGENT,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 20000,
        .buffer_size = 2048,
        .buffer_size_tx = 1024,
    };
    esp_http_client_handle_t client = esp_http_client_init(&http_config);
    if (client == NULL) {
        memset(authorization, 0, sizeof(authorization));
        free(response.data);
        return ESP_ERR_NO_MEM;
    }

    esp_err_t err = esp_http_client_set_header(client, "Authorization", authorization);
    if (err == ESP_OK) err = esp_http_client_set_header(client, "Content-Type", "application/json");
    if (err == ESP_OK) err = esp_http_client_set_post_field(client, request_body, (int)body_length);
    if (err == ESP_OK) err = esp_http_client_perform(client);
    const int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    memset(authorization, 0, sizeof(authorization));
    memset(request_body, 0, sizeof(request_body));

    if (err == ESP_OK && status != 200) {
        ESP_LOGW(TAG, "Tibber price request returned HTTP %d", status);
        err = ESP_FAIL;
    }
    if (err == ESP_OK && (response.overflow || response.length == 0)) {
        err = response.overflow ? ESP_ERR_NO_MEM : ESP_ERR_INVALID_RESPONSE;
    }
    if (err == ESP_OK) err = parse_graphql_response(response.data, response.length, book);
    free(response.data);
    return err;
}

static tibber_price_shadow_tier_t tier_for_rank(uint16_t rank)
{
    if (rank <= CONFIG_LASTVAKT_TIBBER_PRICE_SHADOW_OFF_QUARTERS) return TIBBER_PRICE_SHADOW_FULL_BLOCK;
    if (rank <= CONFIG_LASTVAKT_TIBBER_PRICE_SHADOW_OFF_QUARTERS +
                    CONFIG_LASTVAKT_TIBBER_PRICE_SHADOW_HALF_QUARTERS) {
        return TIBBER_PRICE_SHADOW_HALF_LIMIT;
    }
    return TIBBER_PRICE_SHADOW_NO_LIMIT;
}

static bool snapshot_for_day(
    const price_day_t *day,
    time_t now,
    tibber_price_shadow_snapshot_t *snapshot)
{
    if (!day->valid || now < day->day_start || now >= day->day_end) return false;
    const size_t index = (size_t)((now - day->day_start) / TIBBER_PRICE_QUARTER_SECONDS);
    if (index >= day->count || day->entries[index].starts_at > now ||
        now >= day->entries[index].starts_at + TIBBER_PRICE_QUARTER_SECONDS) {
        return false;
    }
    const price_entry_t *entry = &day->entries[index];
    *snapshot = (tibber_price_shadow_snapshot_t){
        .available = true,
        .starts_at = entry->starts_at,
        .ends_at = entry->starts_at + TIBBER_PRICE_QUARTER_SECONDS,
        .total_price = (float)entry->total,
        .descending_rank = entry->rank,
        .day_slot_count = (uint16_t)day->count,
        .suggested_tier = tier_for_rank(entry->rank),
    };
    memcpy(snapshot->currency, day->currency, sizeof(snapshot->currency));
    return true;
}

static bool snapshot_for_book(
    const price_book_t *book,
    time_t now,
    tibber_price_shadow_snapshot_t *snapshot)
{
    if (book == NULL || !book->valid) return false;
    return snapshot_for_day(&book->today, now, snapshot) ||
           snapshot_for_day(&book->tomorrow, now, snapshot);
}

static bool publish_current_from_active(time_t now, tibber_price_source_state_t missing_state)
{
    tibber_price_shadow_snapshot_t snapshot = {0};
    lock_source();
    const bool available = snapshot_for_book(s_active_book, now, &snapshot);
    if (available) {
        s_snapshot = snapshot;
        s_state = TIBBER_PRICE_SOURCE_READY;
    } else {
        memset(&s_snapshot, 0, sizeof(s_snapshot));
        s_state = missing_state;
    }
    unlock_source();
    return available;
}

static void publish_refreshing(time_t now)
{
    tibber_price_shadow_snapshot_t snapshot = {0};
    lock_source();
    if (snapshot_for_book(s_active_book, now, &snapshot)) s_snapshot = snapshot;
    else memset(&s_snapshot, 0, sizeof(s_snapshot));
    s_state = TIBBER_PRICE_SOURCE_SYNCING;
    unlock_source();
}

static void publish_without_current(tibber_price_source_state_t state)
{
    lock_source();
    s_state = state;
    memset(&s_snapshot, 0, sizeof(s_snapshot));
    unlock_source();
}

static void publish_start_failure(void)
{
    lock_source();
    s_started = false;
    s_state = TIBBER_PRICE_SOURCE_ERROR;
    memset(&s_snapshot, 0, sizeof(s_snapshot));
    unlock_source();
}

static void active_book_status(
    time_t now,
    time_t current_day_start,
    bool *covers_now,
    bool *is_current_today,
    bool *has_tomorrow,
    time_t *current_ends_at)
{
    tibber_price_shadow_snapshot_t ignored = {0};
    lock_source();
    const price_book_t *book = s_active_book;
    *covers_now = snapshot_for_book(book, now, &ignored);
    *is_current_today = book != NULL && book->valid && book->today.valid &&
                        book->today.day_start == current_day_start;
    *has_tomorrow = *is_current_today && book->tomorrow.valid;
    *current_ends_at = *covers_now ? ignored.ends_at : 0;
    unlock_source();
}

static void inherit_existing_tomorrow(price_book_t *book)
{
    if (book->tomorrow.valid || !book->today.valid) return;
    lock_source();
    const price_book_t *active = s_active_book;
    if (active != NULL && active->valid && active->tomorrow.valid &&
        active->tomorrow.day_start == book->today.day_end &&
        strcmp(active->tomorrow.currency, book->currency) == 0) {
        book->tomorrow = active->tomorrow;
        book->coverage_end = book->tomorrow.day_end;
        book->count += book->tomorrow.count;
    }
    unlock_source();
}

static price_book_t *commit_book(price_book_t *book, time_t now)
{
    tibber_price_shadow_snapshot_t snapshot = {0};
    lock_source();
    ++s_generation;
    if (s_generation == 0) ++s_generation;
    book->generation = s_generation;
    price_book_t *old = s_active_book;
    s_active_book = book;
    if (snapshot_for_book(book, now, &snapshot)) {
        s_snapshot = snapshot;
        s_state = TIBBER_PRICE_SOURCE_READY;
    } else {
        memset(&s_snapshot, 0, sizeof(s_snapshot));
        s_state = TIBBER_PRICE_SOURCE_ERROR;
    }
    unlock_source();
    return old;
}

static time_t local_time_on_day(time_t day_start, int hour, int minute)
{
    struct tm local = {0};
    localtime_r(&day_start, &local);
    local.tm_hour = hour;
    local.tm_min = minute;
    local.tm_sec = 0;
    local.tm_isdst = -1;
    return mktime(&local);
}

static time_t retry_time(time_t now, unsigned int attempt)
{
    uint32_t delay_ms = TIBBER_PRICE_RETRY_MS;
    while (attempt > 0U && delay_ms < TIBBER_PRICE_RETRY_MAX_MS) {
        delay_ms = delay_ms > TIBBER_PRICE_RETRY_MAX_MS / 2U
                       ? TIBBER_PRICE_RETRY_MAX_MS
                       : delay_ms * 2U;
        --attempt;
    }
    return now + (time_t)(delay_ms / 1000U) + (time_t)(esp_random() % 16U);
}

static time_t later_time(time_t left, time_t right)
{
    return left > right ? left : right;
}

static void delay_at_most_one_minute(time_t now, time_t wake_at)
{
    time_t delay_seconds = wake_at > now ? wake_at - now : 1;
    if (delay_seconds > 60) delay_seconds = 60;
    vTaskDelay(pdMS_TO_TICKS((uint32_t)delay_seconds * 1000U));
}

static void tibber_price_task(void *arg)
{
    price_book_t *staging = arg;
    unsigned int retry_attempt = 0;
    time_t schedule_day = 0;
    time_t tomorrow_fetch_at = 0;
    time_t rollover_fetch_at = 0;
    time_t next_retry_at = 0;
    time_t initial_fetch_at = 0;

    while (true) {
        if (app_time_get_quality() != APP_TIME_QUALITY_NETWORK) {
            publish_without_current(TIBBER_PRICE_SOURCE_WAITING_TIME);
            vTaskDelay(pdMS_TO_TICKS(5000));
            continue;
        }

        const time_t now = time(NULL);
        const time_t today_start = local_day_start(now);
        if (today_start < 0) {
            publish_without_current(TIBBER_PRICE_SOURCE_WAITING_TIME);
            vTaskDelay(pdMS_TO_TICKS(5000));
            continue;
        }
        if (schedule_day != today_start) {
            schedule_day = today_start;
            tomorrow_fetch_at = local_time_on_day(
                                    today_start,
                                    TIBBER_PRICE_TOMORROW_PUBLISH_HOUR,
                                    TIBBER_PRICE_TOMORROW_PUBLISH_MINUTE) +
                                (time_t)(esp_random() % (TIBBER_PRICE_TOMORROW_JITTER_SECONDS + 1U));
            rollover_fetch_at = today_start + 30 + (time_t)(esp_random() % 61U);
            next_retry_at = 0;
            retry_attempt = 0;
        }
        if (initial_fetch_at == 0) {
            initial_fetch_at = now + TIBBER_PRICE_INITIAL_DELAY_MIN_SECONDS +
                               (time_t)(esp_random() % TIBBER_PRICE_INITIAL_DELAY_JITTER_SECONDS);
        }

        bool covers_now = false;
        bool is_current_today = false;
        bool has_tomorrow = false;
        time_t current_ends_at = 0;
        active_book_status(
            now,
            today_start,
            &covers_now,
            &is_current_today,
            &has_tomorrow,
            &current_ends_at);
        if (covers_now) (void)publish_current_from_active(now, TIBBER_PRICE_SOURCE_ERROR);

        const bool missing_current = !covers_now;
        const bool rollover_refresh = covers_now && !is_current_today;
        const bool tomorrow_refresh = is_current_today && !has_tomorrow && now >= tomorrow_fetch_at;
        const bool refresh_needed = missing_current || rollover_refresh || tomorrow_refresh;

        time_t due_at = initial_fetch_at;
        if (rollover_refresh) due_at = later_time(due_at, rollover_fetch_at);
        if (tomorrow_refresh) due_at = later_time(due_at, tomorrow_fetch_at);
        if (next_retry_at != 0) due_at = later_time(due_at, next_retry_at);

        if (!refresh_needed || now < due_at) {
            time_t wake_at = now + 60;
            if (current_ends_at > now && current_ends_at < wake_at) wake_at = current_ends_at;
            if (refresh_needed && due_at < wake_at) wake_at = due_at;
            if (!has_tomorrow && tomorrow_fetch_at > now && tomorrow_fetch_at < wake_at) {
                wake_at = tomorrow_fetch_at;
            }
            delay_at_most_one_minute(now, wake_at);
            continue;
        }

        if (!network_setup_wait_connected(pdMS_TO_TICKS(10000))) {
            if (!covers_now) publish_without_current(TIBBER_PRICE_SOURCE_WAITING_NETWORK);
            next_retry_at = now + 5;
            vTaskDelay(pdMS_TO_TICKS(5000));
            continue;
        }

        app_config_t app_config = {0};
        if (app_config_load(&app_config) != ESP_OK || !app_config_has_tibber(&app_config) ||
            app_config.tibber_home_id[0] == '\0') {
            memset(&app_config, 0, sizeof(app_config));
            if (!covers_now) publish_without_current(TIBBER_PRICE_SOURCE_NOT_CONFIGURED);
            next_retry_at = now + (time_t)(TIBBER_PRICE_UNCONFIGURED_RETRY_MS / 1000U);
            delay_at_most_one_minute(now, next_retry_at);
            continue;
        }

        memset(staging, 0, sizeof(*staging));
        publish_refreshing(now);
        const esp_err_t fetch_err = fetch_price_book(&app_config, staging);
        memset(&app_config, 0, sizeof(app_config));
        const time_t fetch_finished_at = time(NULL);
        const size_t ranked_quarters = CONFIG_LASTVAKT_TIBBER_PRICE_SHADOW_OFF_QUARTERS +
                                       CONFIG_LASTVAKT_TIBBER_PRICE_SHADOW_HALF_QUARTERS;
        esp_err_t result = fetch_err;
        if (result == ESP_OK &&
            (!validate_price_book(staging, fetch_finished_at) ||
             ranked_quarters > staging->today.count ||
             (staging->tomorrow.valid && ranked_quarters > staging->tomorrow.count))) {
            result = ESP_ERR_INVALID_RESPONSE;
        }

        if (result == ESP_OK) {
            inherit_existing_tomorrow(staging);
            const bool tomorrow_loaded = staging->tomorrow.valid;
            const size_t loaded_slots = staging->count;
            const time_t loaded_day_start = staging->today.day_start;
            staging = commit_book(staging, fetch_finished_at);
            struct tm local = {0};
            localtime_r(&loaded_day_start, &local);
            ESP_LOGI(
                TAG,
                "Quarter-price shadow loaded %u slots for %04d-%02d-%02d%s",
                (unsigned)loaded_slots,
                local.tm_year + 1900,
                local.tm_mon + 1,
                local.tm_mday,
                tomorrow_loaded ? " plus tomorrow" : " (tomorrow not published)");
            if (!tomorrow_loaded && fetch_finished_at >= tomorrow_fetch_at) {
                next_retry_at = retry_time(fetch_finished_at, retry_attempt);
                if (retry_attempt < 8U) ++retry_attempt;
            } else {
                next_retry_at = 0;
                retry_attempt = 0;
            }
            continue;
        }

        memset(staging, 0, sizeof(*staging));
        const bool retained_current = publish_current_from_active(fetch_finished_at, TIBBER_PRICE_SOURCE_ERROR);
        ESP_LOGW(TAG, "Quarter-price shadow refresh failed: %s; retained book=%s",
                 esp_err_to_name(result), retained_current ? "yes" : "no");
        next_retry_at = retry_time(fetch_finished_at, retry_attempt);
        if (retry_attempt < 8U) ++retry_attempt;
    }
}

esp_err_t tibber_price_source_start(void)
{
    if (s_lock == NULL) {
        s_lock = xSemaphoreCreateMutexStatic(&s_lock_storage);
        if (s_lock == NULL) return ESP_ERR_NO_MEM;
    }

    lock_source();
    if (s_started) {
        unlock_source();
        return ESP_ERR_INVALID_STATE;
    }
    s_started = true;
    s_state = TIBBER_PRICE_SOURCE_WAITING_TIME;
    memset(&s_snapshot, 0, sizeof(s_snapshot));
    unlock_source();

    price_book_t *active = heap_caps_calloc(1, sizeof(*active), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    price_book_t *staging = heap_caps_calloc(1, sizeof(*staging), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (active == NULL || staging == NULL) {
        free(active);
        free(staging);
        publish_start_failure();
        return ESP_ERR_NO_MEM;
    }
    lock_source();
    s_active_book = active;
    unlock_source();

    if (xTaskCreate(tibber_price_task, "tibber_price", 8192, staging, 3, NULL) != pdPASS) {
        lock_source();
        s_active_book = NULL;
        unlock_source();
        free(active);
        free(staging);
        publish_start_failure();
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "Read-only Tibber today/tomorrow price book enabled; no output is connected");
    return ESP_OK;
}

tibber_price_source_state_t tibber_price_source_get(tibber_price_shadow_snapshot_t *snapshot)
{
    if (s_lock == NULL) {
        if (snapshot != NULL) memset(snapshot, 0, sizeof(*snapshot));
        return TIBBER_PRICE_SOURCE_DISABLED;
    }
    lock_source();
    const tibber_price_source_state_t state = s_state;
    if (snapshot != NULL) *snapshot = s_snapshot;
    unlock_source();
    return state;
}

static void copy_metadata(
    const price_book_t *book,
    tibber_price_book_metadata_t *metadata)
{
    memset(metadata, 0, sizeof(*metadata));
    if (book == NULL || !book->valid) return;
    metadata->available = true;
    metadata->today_complete = book->today.valid;
    metadata->tomorrow_available = book->tomorrow.valid;
    metadata->tomorrow_complete = book->tomorrow.valid;
    metadata->generation = book->generation;
    metadata->fetched_at = book->fetched_at;
    metadata->coverage_start = book->coverage_start;
    metadata->coverage_end = book->coverage_end;
    metadata->slot_count = (uint16_t)book->count;
    metadata->today_slot_count = (uint16_t)book->today.count;
    metadata->tomorrow_slot_count = (uint16_t)book->tomorrow.count;
    memcpy(metadata->currency, book->currency, sizeof(metadata->currency));
}

static size_t copy_day_window(
    const price_day_t *day,
    bool is_tomorrow,
    time_t window_start,
    time_t window_end,
    tibber_price_slot_snapshot_t *slots,
    size_t capacity,
    size_t copied)
{
    if (!day->valid || copied >= capacity) return copied;
    for (size_t index = 0; index < day->count && copied < capacity; ++index) {
        const price_entry_t *entry = &day->entries[index];
        const time_t ends_at = entry->starts_at + TIBBER_PRICE_QUARTER_SECONDS;
        if (ends_at <= window_start || entry->starts_at >= window_end) continue;
        slots[copied] = (tibber_price_slot_snapshot_t){
            .starts_at = entry->starts_at,
            .ends_at = ends_at,
            .total_price = (float)entry->total,
            .day_rank = entry->rank,
            .day_slot_count = (uint16_t)day->count,
            .is_tomorrow = is_tomorrow,
            .suggested_tier = tier_for_rank(entry->rank),
        };
        memcpy(slots[copied].currency, day->currency, sizeof(slots[copied].currency));
        ++copied;
    }
    return copied;
}

size_t tibber_price_source_copy_window(
    time_t window_start,
    time_t window_end,
    tibber_price_slot_snapshot_t *slots,
    size_t capacity,
    tibber_price_book_metadata_t *metadata)
{
    if (metadata != NULL) memset(metadata, 0, sizeof(*metadata));
    if (s_lock == NULL || window_end <= window_start ||
        (capacity > 0 && slots == NULL)) {
        return 0;
    }

    lock_source();
    const price_book_t *book = s_active_book;
    if (metadata != NULL) copy_metadata(book, metadata);
    size_t copied = 0;
    if (book != NULL && book->valid && slots != NULL && capacity > 0) {
        copied = copy_day_window(
            &book->today, false, window_start, window_end, slots, capacity, copied);
        copied = copy_day_window(
            &book->tomorrow, true, window_start, window_end, slots, capacity, copied);
    }
    unlock_source();
    return copied;
}

const char *tibber_price_source_status_text(void)
{
    switch (tibber_price_source_get(NULL)) {
        case TIBBER_PRICE_SOURCE_NOT_CONFIGURED: return "Kvartspris saknar sparat Tibber-hem";
        case TIBBER_PRICE_SOURCE_WAITING_TIME: return "Kvartspris väntar på tillförlitlig tid";
        case TIBBER_PRICE_SOURCE_WAITING_NETWORK: return "Kvartspris väntar på Wi-Fi";
        case TIBBER_PRICE_SOURCE_SYNCING: return "Hämtar Tibbers kvartspriser";
        case TIBBER_PRICE_SOURCE_READY: return "Kvartspris laddat • endast skuggläge";
        case TIBBER_PRICE_SOURCE_ERROR: return "Kvartspris kunde inte uppdateras";
        case TIBBER_PRICE_SOURCE_DISABLED:
        default: return "Kvartsprisets skuggläge är avstängt";
    }
}
