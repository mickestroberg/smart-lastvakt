#include "calendar_source.h"

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app_config.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "network_setup.h"

#define CALENDAR_MAX_BYTES (2 * 1024 * 1024)
#define CALENDAR_REFRESH_MS (15 * 60 * 1000)
#define CALENDAR_RETRY_MS (2 * 60 * 1000)

typedef struct {
    char *data;
    size_t length;
    size_t capacity;
    bool overflow;
} http_response_t;

typedef enum {
    RECUR_NONE,
    RECUR_DAILY,
    RECUR_WEEKLY,
    RECUR_MONTHLY,
    RECUR_YEARLY,
} recurrence_frequency_t;

typedef struct {
    recurrence_frequency_t frequency;
    int interval;
    int count;
    int month_day;
    uint8_t weekday_mask;
    time_t until;
} recurrence_rule_t;

typedef struct {
    time_t start;
    time_t end;
    bool has_start;
    bool has_end;
    bool all_day;
    bool cancelled;
    char title[96];
    char rule[256];
} parsed_event_t;

static const char *TAG = "calendar_source";
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static calendar_source_state_t s_state = CALENDAR_SOURCE_DISABLED;
static calendar_event_t s_events[CALENDAR_EVENT_LIMIT];
static size_t s_event_count;

static int64_t days_from_civil(int year, unsigned month, unsigned day)
{
    year -= month <= 2;
    const int era = (year >= 0 ? year : year - 399) / 400;
    const unsigned year_of_era = (unsigned)(year - era * 400);
    const unsigned day_of_year = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
    const unsigned day_of_era = year_of_era * 365 + year_of_era / 4 - year_of_era / 100 + day_of_year;
    return (int64_t)era * 146097 + (int64_t)day_of_era - 719468;
}

static time_t utc_epoch(int year, int month, int day, int hour, int minute, int second)
{
    return (time_t)(days_from_civil(year, (unsigned)month, (unsigned)day) * 86400 +
                    hour * 3600 + minute * 60 + second);
}

static bool parse_ics_time(const char *value, time_t *result, bool *all_day)
{
    if (value == NULL || result == NULL || all_day == NULL) {
        return false;
    }

    int year = 0;
    int month = 0;
    int day = 0;
    int hour = 0;
    int minute = 0;
    int second = 0;
    const size_t length = strlen(value);

    if (length >= 15 && sscanf(value, "%4d%2d%2dT%2d%2d%2d", &year, &month, &day, &hour, &minute, &second) == 6) {
        *all_day = false;
    } else if (length >= 8 && sscanf(value, "%4d%2d%2d", &year, &month, &day) == 3) {
        *all_day = true;
    } else {
        return false;
    }

    if (year < 1970 || month < 1 || month > 12 || day < 1 || day > 31 ||
        hour < 0 || hour > 23 || minute < 0 || minute > 59 || second < 0 || second > 60) {
        return false;
    }

    if (!*all_day && length > 0 && value[length - 1] == 'Z') {
        *result = utc_epoch(year, month, day, hour, minute, second);
        return true;
    }

    struct tm local = {
        .tm_year = year - 1900,
        .tm_mon = month - 1,
        .tm_mday = day,
        .tm_hour = hour,
        .tm_min = minute,
        .tm_sec = second,
        .tm_isdst = -1,
    };
    *result = mktime(&local);
    return *result >= 0;
}

static void unescape_text(char *destination, size_t destination_size, const char *source)
{
    if (destination_size == 0) {
        return;
    }

    size_t written = 0;
    while (*source != '\0' && written + 1 < destination_size) {
        if (*source == '\\' && source[1] != '\0') {
            ++source;
            destination[written++] = (*source == 'n' || *source == 'N') ? ' ' : *source;
            ++source;
        } else {
            destination[written++] = *source++;
        }
    }
    destination[written] = '\0';
}

static void unfold_ics(char *text)
{
    char *read = text;
    char *write = text;
    while (*read != '\0') {
        if (read[0] == '\r' && read[1] == '\n' && (read[2] == ' ' || read[2] == '\t')) {
            read += 3;
        } else if (read[0] == '\n' && (read[1] == ' ' || read[1] == '\t')) {
            read += 2;
        } else if (*read == '\r') {
            ++read;
        } else {
            *write++ = *read++;
        }
    }
    *write = '\0';
}

static uint8_t weekday_bit(const char *value)
{
    if (strcmp(value, "SU") == 0) return 1u << 0;
    if (strcmp(value, "MO") == 0) return 1u << 1;
    if (strcmp(value, "TU") == 0) return 1u << 2;
    if (strcmp(value, "WE") == 0) return 1u << 3;
    if (strcmp(value, "TH") == 0) return 1u << 4;
    if (strcmp(value, "FR") == 0) return 1u << 5;
    if (strcmp(value, "SA") == 0) return 1u << 6;
    return 0;
}

static recurrence_rule_t parse_rule(const char *value)
{
    recurrence_rule_t rule = {.interval = 1};
    char copy[256];
    snprintf(copy, sizeof(copy), "%s", value);

    char *save = NULL;
    for (char *part = strtok_r(copy, ";", &save); part != NULL; part = strtok_r(NULL, ";", &save)) {
        if (strncmp(part, "FREQ=", 5) == 0) {
            const char *frequency = part + 5;
            if (strcmp(frequency, "DAILY") == 0) rule.frequency = RECUR_DAILY;
            else if (strcmp(frequency, "WEEKLY") == 0) rule.frequency = RECUR_WEEKLY;
            else if (strcmp(frequency, "MONTHLY") == 0) rule.frequency = RECUR_MONTHLY;
            else if (strcmp(frequency, "YEARLY") == 0) rule.frequency = RECUR_YEARLY;
        } else if (strncmp(part, "INTERVAL=", 9) == 0) {
            rule.interval = atoi(part + 9);
            if (rule.interval < 1) rule.interval = 1;
        } else if (strncmp(part, "COUNT=", 6) == 0) {
            rule.count = atoi(part + 6);
        } else if (strncmp(part, "BYMONTHDAY=", 11) == 0) {
            rule.month_day = atoi(part + 11);
        } else if (strncmp(part, "UNTIL=", 6) == 0) {
            bool all_day = false;
            parse_ics_time(part + 6, &rule.until, &all_day);
        } else if (strncmp(part, "BYDAY=", 6) == 0) {
            char days[64];
            snprintf(days, sizeof(days), "%s", part + 6);
            char *day_save = NULL;
            for (char *day = strtok_r(days, ",", &day_save); day != NULL; day = strtok_r(NULL, ",", &day_save)) {
                const size_t length = strlen(day);
                if (length >= 2) rule.weekday_mask |= weekday_bit(day + length - 2);
            }
        }
    }
    return rule;
}

static bool recurrence_matches(
    const struct tm *day,
    const struct tm *start,
    int days_since_start,
    const recurrence_rule_t *rule)
{
    switch (rule->frequency) {
        case RECUR_DAILY:
            return days_since_start % rule->interval == 0;
        case RECUR_WEEKLY: {
            const uint8_t mask = rule->weekday_mask != 0 ? rule->weekday_mask : (uint8_t)(1u << start->tm_wday);
            const int start_monday_index = (start->tm_wday + 6) % 7;
            const int week = (days_since_start + start_monday_index) / 7;
            return week % rule->interval == 0 && (mask & (1u << day->tm_wday)) != 0;
        }
        case RECUR_MONTHLY: {
            const int months = (day->tm_year - start->tm_year) * 12 + day->tm_mon - start->tm_mon;
            const int month_day = rule->month_day != 0 ? rule->month_day : start->tm_mday;
            return months >= 0 && months % rule->interval == 0 && day->tm_mday == month_day;
        }
        case RECUR_YEARLY:
            return (day->tm_year - start->tm_year) % rule->interval == 0 &&
                   day->tm_mon == start->tm_mon && day->tm_mday == start->tm_mday;
        case RECUR_NONE:
        default:
            return false;
    }
}

static time_t event_duration(const parsed_event_t *event)
{
    return event->has_end && event->end > event->start
               ? event->end - event->start
               : (event->all_day ? 24 * 60 * 60 : 60 * 60);
}

static bool event_candidate(const parsed_event_t *event, time_t now, time_t *candidate)
{
    if (!event->has_start || event->cancelled || event->title[0] == '\0') {
        return false;
    }

    const time_t end = event->start + event_duration(event);
    if (end <= now) {
        return false;
    }
    *candidate = event->start;
    return true;
}

static void insert_event(
    calendar_event_t *events,
    size_t capacity,
    size_t *count,
    const parsed_event_t *event,
    time_t start)
{
    for (size_t i = 0; i < *count; ++i) {
        if (events[i].start == start && strcmp(events[i].title, event->title) == 0) {
            return;
        }
    }

    size_t position = 0;
    while (position < *count && events[position].start <= start) {
        ++position;
    }
    if (*count == capacity && position == capacity) {
        return;
    }

    const size_t new_count = *count < capacity ? *count + 1 : *count;
    for (size_t i = new_count - 1; i > position; --i) {
        events[i] = events[i - 1];
    }

    events[position] = (calendar_event_t){
        .available = true,
        .all_day = event->all_day,
        .start = start,
    };
    snprintf(events[position].title, sizeof(events[position].title), "%s", event->title);
    *count = new_count;
}

static void insert_recurrences(
    calendar_event_t *events,
    size_t capacity,
    size_t *count,
    const parsed_event_t *event,
    const recurrence_rule_t *rule,
    time_t now)
{
    if (rule->frequency == RECUR_NONE) {
        return;
    }

    struct tm start_local = {0};
    struct tm now_local = {0};
    localtime_r(&event->start, &start_local);
    localtime_r(&now, &now_local);
    const int64_t start_day_number = days_from_civil(
        start_local.tm_year + 1900,
        (unsigned)(start_local.tm_mon + 1),
        (unsigned)start_local.tm_mday);

    struct tm day = start_local;
    day.tm_hour = 0;
    day.tm_min = 0;
    day.tm_sec = 0;
    day.tm_isdst = -1;
    mktime(&day);

    const time_t duration = event_duration(event);
    int occurrence_count = 0;
    size_t inserted = 0;
    const int final_year = now_local.tm_year + 1900 + 5;

    while (day.tm_year + 1900 <= final_year && inserted < capacity) {
        const int64_t day_number = days_from_civil(
            day.tm_year + 1900,
            (unsigned)(day.tm_mon + 1),
            (unsigned)day.tm_mday);
        const int days_since_start = (int)(day_number - start_day_number);

        if (days_since_start >= 0 && recurrence_matches(&day, &start_local, days_since_start, rule)) {
            struct tm occurrence_local = day;
            occurrence_local.tm_hour = start_local.tm_hour;
            occurrence_local.tm_min = start_local.tm_min;
            occurrence_local.tm_sec = start_local.tm_sec;
            occurrence_local.tm_isdst = -1;
            const time_t occurrence = mktime(&occurrence_local);

            if (occurrence >= event->start) {
                ++occurrence_count;
                if (rule->count > 0 && occurrence_count > rule->count) break;
                if (rule->until > 0 && occurrence > rule->until) break;
                if (occurrence + duration > now) {
                    insert_event(events, capacity, count, event, occurrence);
                    ++inserted;
                }
            }
        }

        ++day.tm_mday;
        day.tm_isdst = -1;
        mktime(&day);
    }
}

static size_t parse_calendar(char *text, calendar_event_t *events, size_t capacity)
{
    unfold_ics(text);
    const time_t now = time(NULL);
    bool inside_event = false;
    parsed_event_t event = {0};
    size_t count = 0;

    char *save = NULL;
    for (char *line = strtok_r(text, "\n", &save); line != NULL; line = strtok_r(NULL, "\n", &save)) {
        if (strcmp(line, "BEGIN:VEVENT") == 0) {
            memset(&event, 0, sizeof(event));
            inside_event = true;
            continue;
        }
        if (!inside_event) {
            continue;
        }
        if (strcmp(line, "END:VEVENT") == 0) {
            if (capacity > 0 && event.rule[0] != '\0') {
                const recurrence_rule_t rule = parse_rule(event.rule);
                insert_recurrences(events, capacity, &count, &event, &rule, now);
            } else {
                time_t candidate = 0;
                if (capacity > 0 && event_candidate(&event, now, &candidate)) {
                    insert_event(events, capacity, &count, &event, candidate);
                }
            }
            inside_event = false;
            continue;
        }

        char *value = strchr(line, ':');
        if (value == NULL) {
            continue;
        }
        *value++ = '\0';

        if (strncmp(line, "DTSTART", 7) == 0) {
            event.has_start = parse_ics_time(value, &event.start, &event.all_day);
        } else if (strncmp(line, "DTEND", 5) == 0) {
            bool end_all_day = false;
            event.has_end = parse_ics_time(value, &event.end, &end_all_day);
        } else if (strcmp(line, "SUMMARY") == 0 || strncmp(line, "SUMMARY;", 8) == 0) {
            unescape_text(event.title, sizeof(event.title), value);
        } else if (strcmp(line, "RRULE") == 0) {
            snprintf(event.rule, sizeof(event.rule), "%s", value);
        } else if (strcmp(line, "STATUS") == 0 && strcmp(value, "CANCELLED") == 0) {
            event.cancelled = true;
        }
    }
    return count;
}

static esp_err_t http_event(esp_http_client_event_t *event)
{
    if (event->event_id != HTTP_EVENT_ON_DATA || event->data_len <= 0) {
        return ESP_OK;
    }

    http_response_t *response = event->user_data;
    const size_t required = response->length + (size_t)event->data_len + 1;
    if (required > CALENDAR_MAX_BYTES) {
        response->overflow = true;
        return ESP_FAIL;
    }

    if (required > response->capacity) {
        size_t new_capacity = response->capacity == 0 ? 32768 : response->capacity;
        while (new_capacity < required) new_capacity *= 2;
        if (new_capacity > CALENDAR_MAX_BYTES) new_capacity = CALENDAR_MAX_BYTES;
        char *larger = heap_caps_realloc(response->data, new_capacity, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (larger == NULL) {
            response->overflow = true;
            return ESP_ERR_NO_MEM;
        }
        response->data = larger;
        response->capacity = new_capacity;
    }

    memcpy(response->data + response->length, event->data, (size_t)event->data_len);
    response->length += (size_t)event->data_len;
    response->data[response->length] = '\0';
    return ESP_OK;
}

static esp_err_t fetch_calendar(
    const char *url,
    calendar_event_t *events,
    size_t capacity,
    size_t *event_count)
{
    ESP_LOGI(
        TAG,
        "Calendar fetch starting; internal heap %u bytes, largest DMA block %u bytes",
        (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
        (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_DMA));
    http_response_t response = {0};
    esp_http_client_config_t config = {
        .url = url,
        .event_handler = http_event,
        .user_data = &response,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 20000,
        .buffer_size = 4096,
        .buffer_size_tx = 1024,
        .max_redirection_count = 4,
        .user_agent = "Smart-lastvakt/1.0",
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        return ESP_ERR_NO_MEM;
    }

    esp_err_t err = esp_http_client_perform(client);
    const int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (err == ESP_OK && status != 200) {
        ESP_LOGW(TAG, "Calendar returned HTTP %d", status);
        err = ESP_FAIL;
    }
    if (err == ESP_OK && (response.overflow || response.data == NULL)) {
        err = response.overflow ? ESP_ERR_NO_MEM : ESP_ERR_INVALID_RESPONSE;
    }
    if (err == ESP_OK) {
        memset(events, 0, capacity * sizeof(*events));
        *event_count = parse_calendar(response.data, events, capacity);
    } else {
        ESP_LOGW(TAG, "Calendar download stopped after %u bytes", (unsigned)response.length);
    }

    free(response.data);
    return err;
}

static void publish(calendar_source_state_t state, const calendar_event_t *events, size_t event_count)
{
    portENTER_CRITICAL(&s_lock);
    s_state = state;
    if (events != NULL) {
        if (event_count > CALENDAR_EVENT_LIMIT) event_count = CALENDAR_EVENT_LIMIT;
        memcpy(s_events, events, event_count * sizeof(*events));
        if (event_count < CALENDAR_EVENT_LIMIT) {
            memset(s_events + event_count, 0, (CALENDAR_EVENT_LIMIT - event_count) * sizeof(*events));
        }
        s_event_count = event_count;
    }
    portEXIT_CRITICAL(&s_lock);
}

static void calendar_task(void *arg)
{
    (void)arg;
    app_config_t config;
    if (app_config_load(&config) != ESP_OK || !app_config_has_calendar(&config)) {
        publish(CALENDAR_SOURCE_DISABLED, NULL, 0);
        vTaskDelete(NULL);
        return;
    }

    if (!network_setup_wait_connected(portMAX_DELAY)) {
        publish(CALENDAR_SOURCE_ERROR, NULL, 0);
        vTaskDelete(NULL);
        return;
    }

    vTaskDelay(pdMS_TO_TICKS(6000));
    while (true) {
        publish(CALENDAR_SOURCE_SYNCING, NULL, 0);
        calendar_event_t events[CALENDAR_EVENT_LIMIT] = {0};
        size_t event_count = 0;
        const esp_err_t err = fetch_calendar(
            config.calendar_ical_url,
            events,
            CALENDAR_EVENT_LIMIT,
            &event_count);
        if (err == ESP_OK) {
            publish(CALENDAR_SOURCE_READY, events, event_count);
            ESP_LOGI(TAG, "Calendar synchronized; %u upcoming event(s)", (unsigned)event_count);
            vTaskDelay(pdMS_TO_TICKS(CALENDAR_REFRESH_MS));
        } else {
            publish(CALENDAR_SOURCE_ERROR, NULL, 0);
            ESP_LOGW(TAG, "Calendar synchronization failed: %s", esp_err_to_name(err));
            vTaskDelay(pdMS_TO_TICKS(CALENDAR_RETRY_MS));
        }
    }
}

void calendar_source_init(void)
{
    app_config_t config;
    if (app_config_load(&config) != ESP_OK || !app_config_has_calendar(&config)) {
        publish(CALENDAR_SOURCE_DISABLED, NULL, 0);
        return;
    }

    publish(CALENDAR_SOURCE_SYNCING, NULL, 0);
    if (xTaskCreate(calendar_task, "calendar", 8192, NULL, 4, NULL) != pdPASS) {
        publish(CALENDAR_SOURCE_ERROR, NULL, 0);
    }
}

calendar_source_state_t calendar_source_get(calendar_event_t *event)
{
    portENTER_CRITICAL(&s_lock);
    const calendar_source_state_t state = s_state;
    if (event != NULL) {
        *event = s_event_count > 0 ? s_events[0] : (calendar_event_t){0};
    }
    portEXIT_CRITICAL(&s_lock);
    return state;
}

calendar_source_state_t calendar_source_get_events(
    calendar_event_t *events,
    size_t capacity,
    size_t *count)
{
    portENTER_CRITICAL(&s_lock);
    const calendar_source_state_t state = s_state;
    const size_t copy_count = s_event_count < capacity ? s_event_count : capacity;
    if (events != NULL && copy_count > 0) {
        memcpy(events, s_events, copy_count * sizeof(*events));
    }
    if (count != NULL) *count = copy_count;
    portEXIT_CRITICAL(&s_lock);
    return state;
}
