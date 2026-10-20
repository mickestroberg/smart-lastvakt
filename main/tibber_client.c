#include "tibber_client.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app_config.h"
#include "cJSON.h"
#include "esp_check.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "esp_websocket_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "network_setup.h"

#define TIBBER_API_URL "https://api.tibber.com/v1-beta/gql"
#define USER_AGENT "smart-lastvakt/0.3.0 ESP32-S3"
#define TIBBER_ACK_TIMEOUT_US (20LL * 1000LL * 1000LL)
#define TIBBER_DISCONNECTED_TIMEOUT_US (30LL * 1000LL * 1000LL)
#define TIBBER_DATA_TIMEOUT_US ((int64_t)TIBBER_LIVE_MAX_AGE_MS * 1000LL)
#define TIBBER_SEND_TIMEOUT_TICKS pdMS_TO_TICKS(5000)
#define TIBBER_RETRY_MAX_SECONDS 120U

typedef enum {
    TIBBER_NOT_CONFIGURED,
    TIBBER_WAITING_NETWORK,
    TIBBER_DISCOVERING,
    TIBBER_CONNECTING,
    TIBBER_LIVE,
    TIBBER_NO_PULSE,
    TIBBER_ERROR,
} tibber_state_t;

typedef struct {
    char *data;
    size_t length;
    size_t capacity;
} http_response_t;

typedef struct {
    bool connected;
    bool acknowledged;
    bool restart_requested;
    int64_t connected_since_us;
    int64_t disconnected_since_us;
    int64_t session_last_phase_us[3];
} websocket_health_t;

typedef struct {
    esp_websocket_client_handle_t client;
    portMUX_TYPE lock;
    char home_id[LASTVAKT_HOME_ID_MAX + 1];
    char frame[4096];
    size_t frame_length;
    bool frame_overflow;
    bool logged_first_sample;
    bool warned_partial_sample;
    bool warned_empty_sample;
    websocket_health_t health;
} websocket_context_t;

static const char *TAG = "tibber";
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static tibber_state_t s_state = TIBBER_NOT_CONFIGURED;
static float s_current[3];
static int64_t s_last_phase_us[3];
static bool s_phase_in_current_session[3];

static void set_state(tibber_state_t state)
{
    portENTER_CRITICAL(&s_lock);
    s_state = state;
    portEXIT_CRITICAL(&s_lock);
}

static tibber_state_t get_state(void)
{
    portENTER_CRITICAL(&s_lock);
    const tibber_state_t state = s_state;
    portEXIT_CRITICAL(&s_lock);
    return state;
}

static unsigned next_retry_seconds(unsigned current)
{
    if (current >= TIBBER_RETRY_MAX_SECONDS) return TIBBER_RETRY_MAX_SECONDS;
    const unsigned doubled = current * 2U;
    return doubled > TIBBER_RETRY_MAX_SECONDS ? TIBBER_RETRY_MAX_SECONDS : doubled;
}

static void retry_delay(unsigned retry_seconds)
{
    const unsigned jitter_ms = esp_random() % 1000U;
    vTaskDelay(pdMS_TO_TICKS(retry_seconds * 1000U + jitter_ms));
}

static esp_err_t http_event(esp_http_client_event_t *event)
{
    if (event->event_id != HTTP_EVENT_ON_DATA || event->data_len <= 0) return ESP_OK;
    http_response_t *response = event->user_data;
    size_t required = response->length + (size_t)event->data_len + 1;
    if (required > response->capacity) {
        size_t capacity = required + 1024;
        char *expanded = realloc(response->data, capacity);
        if (expanded == NULL) return ESP_ERR_NO_MEM;
        response->data = expanded;
        response->capacity = capacity;
    }
    memcpy(response->data + response->length, event->data, event->data_len);
    response->length += event->data_len;
    response->data[response->length] = '\0';
    return ESP_OK;
}

static esp_err_t discover(const app_config_t *config, char *websocket_url, size_t url_size,
                          char *home_id, size_t home_size)
{
    static const char body[] =
        "{\"query\":\"{ viewer { websocketSubscriptionUrl homes { id appNickname features { realTimeConsumptionEnabled } } } }\"}";
    char authorization[LASTVAKT_TIBBER_TOKEN_MAX + 16];
    snprintf(authorization, sizeof(authorization), "Bearer %s", config->tibber_token);
    http_response_t response = {0};
    esp_http_client_config_t http_config = {
        .url = TIBBER_API_URL,
        .method = HTTP_METHOD_POST,
        .event_handler = http_event,
        .user_data = &response,
        .user_agent = USER_AGENT,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 15000,
    };
    esp_http_client_handle_t client = esp_http_client_init(&http_config);
    if (client == NULL) return ESP_ERR_NO_MEM;
    esp_http_client_set_header(client, "Authorization", authorization);
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, body, strlen(body));

    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    memset(authorization, 0, sizeof(authorization));
    if (err != ESP_OK || status != 200 || response.data == NULL) {
        ESP_LOGW(TAG, "Tibber discovery failed: %s, HTTP %d", esp_err_to_name(err), status);
        free(response.data);
        return err == ESP_OK ? ESP_FAIL : err;
    }

    cJSON *root = cJSON_Parse(response.data);
    free(response.data);
    if (root == NULL) return ESP_ERR_INVALID_RESPONSE;
    cJSON *viewer = cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(root, "data"), "viewer");
    cJSON *url = cJSON_GetObjectItemCaseSensitive(viewer, "websocketSubscriptionUrl");
    cJSON *homes = cJSON_GetObjectItemCaseSensitive(viewer, "homes");
    if (!cJSON_IsString(url) || !cJSON_IsArray(homes)) {
        cJSON_Delete(root);
        return ESP_ERR_INVALID_RESPONSE;
    }

    cJSON *selected = NULL;
    cJSON *home = NULL;
    cJSON_ArrayForEach(home, homes) {
        cJSON *id = cJSON_GetObjectItemCaseSensitive(home, "id");
        cJSON *features = cJSON_GetObjectItemCaseSensitive(home, "features");
        cJSON *enabled = cJSON_GetObjectItemCaseSensitive(features, "realTimeConsumptionEnabled");
        if (!cJSON_IsString(id) || !cJSON_IsTrue(enabled)) continue;
        if (selected == NULL) selected = home;
        if (config->tibber_home_id[0] != '\0' && strcmp(id->valuestring, config->tibber_home_id) == 0) {
            selected = home;
            break;
        }
    }

    if (selected == NULL) {
        cJSON_Delete(root);
        return ESP_ERR_NOT_FOUND;
    }
    cJSON *selected_id = cJSON_GetObjectItemCaseSensitive(selected, "id");
    snprintf(websocket_url, url_size, "%s", url->valuestring);
    snprintf(home_id, home_size, "%s", selected_id->valuestring);
    cJSON_Delete(root);
    return ESP_OK;
}

static websocket_health_t websocket_health(websocket_context_t *context)
{
    portENTER_CRITICAL(&context->lock);
    const websocket_health_t health = context->health;
    portEXIT_CRITICAL(&context->lock);
    return health;
}

static void websocket_set_connected(websocket_context_t *context, bool connected)
{
    const int64_t now_us = esp_timer_get_time();
    portENTER_CRITICAL(&context->lock);
    context->health.connected = connected;
    if (connected) {
        context->health.acknowledged = false;
        context->health.connected_since_us = now_us;
        context->health.disconnected_since_us = 0;
        memset(context->health.session_last_phase_us, 0, sizeof(context->health.session_last_phase_us));
    } else if (context->health.disconnected_since_us == 0) {
        context->health.disconnected_since_us = now_us;
    }
    portEXIT_CRITICAL(&context->lock);
}

static void websocket_set_acknowledged(websocket_context_t *context)
{
    portENTER_CRITICAL(&context->lock);
    context->health.acknowledged = true;
    portEXIT_CRITICAL(&context->lock);
}

static void websocket_note_phases(websocket_context_t *context, const bool present[3], int64_t now_us)
{
    portENTER_CRITICAL(&context->lock);
    for (int i = 0; i < 3; ++i) {
        if (present[i]) context->health.session_last_phase_us[i] = now_us;
    }
    portEXIT_CRITICAL(&context->lock);
}

static void websocket_request_restart(websocket_context_t *context, const char *reason)
{
    bool first_request = false;
    portENTER_CRITICAL(&context->lock);
    if (!context->health.restart_requested) {
        context->health.restart_requested = true;
        first_request = true;
    }
    portEXIT_CRITICAL(&context->lock);
    if (first_request) ESP_LOGW(TAG, "Restarting Tibber stream: %s", reason);
}

static bool send_text(websocket_context_t *context, const char *message)
{
    const int length = (int)strlen(message);
    return esp_websocket_client_send_text(
               context->client,
               message,
               length,
               TIBBER_SEND_TIMEOUT_TICKS) == length;
}

static bool send_subscription(websocket_context_t *context)
{
    char message[512];
    snprintf(message, sizeof(message),
             "{\"id\":\"lastvakt\",\"type\":\"subscribe\",\"payload\":{\"query\":\"subscription { liveMeasurement(homeId: \\\"%s\\\") { timestamp power currentL1 currentL2 currentL3 } }\"}}",
             context->home_id);
    return send_text(context, message);
}

static void process_next_message(websocket_context_t *context, cJSON *root)
{
    cJSON *payload = cJSON_GetObjectItemCaseSensitive(root, "payload");
    cJSON *data = cJSON_GetObjectItemCaseSensitive(payload, "data");
    cJSON *live = cJSON_GetObjectItemCaseSensitive(data, "liveMeasurement");
    cJSON *phase_items[3] = {
        cJSON_GetObjectItemCaseSensitive(live, "currentL1"),
        cJSON_GetObjectItemCaseSensitive(live, "currentL2"),
        cJSON_GetObjectItemCaseSensitive(live, "currentL3"),
    };

    const int64_t now_us = esp_timer_get_time();
    bool present[3] = {0};
    float values[3] = {0};
    int accepted = 0;
    for (int i = 0; i < 3; ++i) {
        if (cJSON_IsNumber(phase_items[i])) {
            present[i] = true;
            values[i] = (float)phase_items[i]->valuedouble;
            ++accepted;
        }
    }

    if (accepted == 0) {
        if (!context->warned_empty_sample) {
            ESP_LOGW(TAG, "Live message contained no numeric phase currents");
            context->warned_empty_sample = true;
        }
        return;
    }

    portENTER_CRITICAL(&s_lock);
    for (int i = 0; i < 3; ++i) {
        if (!present[i]) continue;
        s_current[i] = values[i];
        s_last_phase_us[i] = now_us;
        s_phase_in_current_session[i] = true;
    }
    s_state = TIBBER_LIVE;
    portEXIT_CRITICAL(&s_lock);
    websocket_note_phases(context, present, now_us);

    context->warned_empty_sample = false;
    if (!context->logged_first_sample) {
        ESP_LOGI(TAG, "First Tibber live measurement received (%d phase values)", accepted);
        context->logged_first_sample = true;
    }
    if (accepted < 3) {
        if (!context->warned_partial_sample) {
            ESP_LOGW(TAG, "Partial Tibber measurement received (%d of 3 phase values)", accepted);
            context->warned_partial_sample = true;
        }
    } else if (context->warned_partial_sample) {
        ESP_LOGI(TAG, "Complete three-phase Tibber measurements resumed");
        context->warned_partial_sample = false;
    }
}

static void process_text_frame(websocket_context_t *context, const char *message)
{
    cJSON *root = cJSON_Parse(message);
    if (root == NULL) {
        websocket_request_restart(context, "invalid GraphQL message");
        return;
    }

    cJSON *type = cJSON_GetObjectItemCaseSensitive(root, "type");
    if (!cJSON_IsString(type)) {
        cJSON_Delete(root);
        websocket_request_restart(context, "GraphQL message without type");
        return;
    }

    if (strcmp(type->valuestring, "connection_ack") == 0) {
        websocket_set_acknowledged(context);
        if (!send_subscription(context)) {
            websocket_request_restart(context, "subscription send failed");
        } else {
            ESP_LOGI(TAG, "Tibber stream acknowledged; subscription sent");
        }
    } else if (strcmp(type->valuestring, "next") == 0) {
        process_next_message(context, root);
    } else if (strcmp(type->valuestring, "ping") == 0) {
        if (!send_text(context, "{\"type\":\"pong\"}")) {
            websocket_request_restart(context, "GraphQL pong send failed");
        }
    } else if (strcmp(type->valuestring, "error") == 0) {
        websocket_request_restart(context, "GraphQL subscription error");
    } else if (strcmp(type->valuestring, "complete") == 0) {
        websocket_request_restart(context, "GraphQL subscription completed");
    }

    cJSON_Delete(root);
}

static void websocket_event(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    (void)base;
    websocket_context_t *context = handler_args;
    esp_websocket_event_data_t *event = event_data;

    if (event_id == WEBSOCKET_EVENT_CONNECTED) {
        websocket_set_connected(context, true);
        portENTER_CRITICAL(&s_lock);
        memset(s_phase_in_current_session, 0, sizeof(s_phase_in_current_session));
        s_state = TIBBER_CONNECTING;
        portEXIT_CRITICAL(&s_lock);
        static const char init[] = "{\"type\":\"connection_init\"}";
        if (!send_text(context, init)) websocket_request_restart(context, "connection_init send failed");
    } else if (event_id == WEBSOCKET_EVENT_DATA && event != NULL && event->op_code == 0x1) {
        if (event->payload_offset == 0) {
            context->frame_length = 0;
            context->frame_overflow = false;
        }
        size_t available = sizeof(context->frame) - context->frame_length - 1;
        size_t incoming = event->data_len > 0 ? (size_t)event->data_len : 0;
        size_t copy = incoming < available ? incoming : available;
        if (copy > 0) memcpy(context->frame + context->frame_length, event->data_ptr, copy);
        context->frame_length += copy;
        context->frame[context->frame_length] = '\0';
        if (copy < incoming) context->frame_overflow = true;

        if (event->payload_offset + event->data_len >= event->payload_len) {
            if (context->frame_overflow) {
                websocket_request_restart(context, "GraphQL message exceeded receive buffer");
            } else {
                process_text_frame(context, context->frame);
            }
        }
    } else if (event_id == WEBSOCKET_EVENT_DISCONNECTED) {
        websocket_set_connected(context, false);
        set_state(TIBBER_ERROR);
        ESP_LOGW(TAG, "Tibber WebSocket disconnected; client will reconnect");
    } else if (event_id == WEBSOCKET_EVENT_ERROR) {
        websocket_set_connected(context, false);
        set_state(TIBBER_ERROR);
        const int error_type = event == NULL ? -1 : (int)event->error_handle.error_type;
        ESP_LOGW(TAG, "Tibber WebSocket error type %d; client will reconnect", error_type);
    } else if (event_id == WEBSOCKET_EVENT_CLOSED) {
        websocket_set_connected(context, false);
        set_state(TIBBER_ERROR);
        const int close_code = event == NULL ? 0 : event->close_status_code;
        ESP_LOGW(TAG, "Tibber WebSocket closed cleanly (code %d); client will reconnect", close_code);
    } else if (event_id == WEBSOCKET_EVENT_FINISH) {
        websocket_request_restart(context, "WebSocket task finished");
    }
}

static bool websocket_watchdog_expired(websocket_context_t *context)
{
    const websocket_health_t health = websocket_health(context);
    if (health.restart_requested) return true;
    const int64_t now_us = esp_timer_get_time();
    if (!health.connected) {
        if (health.disconnected_since_us > 0 &&
            now_us - health.disconnected_since_us >= TIBBER_DISCONNECTED_TIMEOUT_US) {
            websocket_request_restart(context, "WebSocket remained disconnected for 30 seconds");
            return true;
        }
        return false;
    }

    if (!health.acknowledged && now_us - health.connected_since_us >= TIBBER_ACK_TIMEOUT_US) {
        websocket_request_restart(context, "no GraphQL acknowledgement within 20 seconds");
        return true;
    }
    if (health.acknowledged) {
        for (int i = 0; i < 3; ++i) {
            const int64_t phase_us = health.session_last_phase_us[i];
            if (phase_us == 0 && now_us - health.connected_since_us >= TIBBER_DATA_TIMEOUT_US) {
                websocket_request_restart(context, "one or more phase currents missing for 30 seconds");
                return true;
            }
            if (phase_us > 0 && now_us - phase_us >= TIBBER_DATA_TIMEOUT_US) {
                websocket_request_restart(context, "one or more phase currents stale for 30 seconds");
                return true;
            }
        }
    }
    return false;
}

static void tibber_task(void *arg)
{
    (void)arg;
    unsigned retry_seconds = 5;
    for (;;) {
        set_state(TIBBER_WAITING_NETWORK);
        if (!network_setup_wait_connected(portMAX_DELAY)) continue;

        app_config_t config;
        if (app_config_load(&config) != ESP_OK || !app_config_has_tibber(&config)) {
            set_state(TIBBER_NOT_CONFIGURED);
            vTaskDelete(NULL);
        }

        set_state(TIBBER_DISCOVERING);
        char url[256] = {0};
        websocket_context_t *context = calloc(1, sizeof(*context));
        if (context == NULL) {
            set_state(TIBBER_ERROR);
            retry_delay(retry_seconds);
            retry_seconds = next_retry_seconds(retry_seconds);
            continue;
        }
        portMUX_INITIALIZE(&context->lock);

        esp_err_t err = discover(&config, url, sizeof(url), context->home_id, sizeof(context->home_id));
        if (err == ESP_ERR_NOT_FOUND) set_state(TIBBER_NO_PULSE);
        else if (err != ESP_OK) set_state(TIBBER_ERROR);
        if (err != ESP_OK) {
            free(context);
            retry_delay(retry_seconds);
            retry_seconds = next_retry_seconds(retry_seconds);
            continue;
        }
        if (strcmp(context->home_id, config.tibber_home_id) != 0) app_config_save_home_id(context->home_id);

        char authorization[LASTVAKT_TIBBER_TOKEN_MAX + 32];
        snprintf(authorization, sizeof(authorization), "Authorization: Bearer %s\r\n", config.tibber_token);
        esp_websocket_client_config_t ws_config = {
            .uri = url,
            .subprotocol = "graphql-transport-ws",
            .user_agent = USER_AGENT,
            .headers = authorization,
            .crt_bundle_attach = esp_crt_bundle_attach,
            .reconnect_timeout_ms = 5000,
            .network_timeout_ms = 15000,
            .enable_close_reconnect = true,
        };
        context->client = esp_websocket_client_init(&ws_config);
        if (context->client == NULL) {
            memset(authorization, 0, sizeof(authorization));
            free(context);
            set_state(TIBBER_ERROR);
            retry_delay(retry_seconds);
            retry_seconds = next_retry_seconds(retry_seconds);
            continue;
        }

        esp_websocket_register_events(context->client, WEBSOCKET_EVENT_ANY, websocket_event, context);
        context->health.disconnected_since_us = esp_timer_get_time();
        set_state(TIBBER_CONNECTING);
        err = esp_websocket_client_start(context->client);
        memset(authorization, 0, sizeof(authorization));
        if (err != ESP_OK) {
            esp_websocket_client_destroy(context->client);
            free(context);
            set_state(TIBBER_ERROR);
            retry_delay(retry_seconds);
            retry_seconds = next_retry_seconds(retry_seconds);
            continue;
        }

        bool watchdog_restart = false;
        while (network_setup_is_connected()) {
            vTaskDelay(pdMS_TO_TICKS(1000));
            if (websocket_watchdog_expired(context)) {
                watchdog_restart = true;
                break;
            }
        }

        const websocket_health_t final_health = websocket_health(context);
        const bool received_session_data = final_health.session_last_phase_us[0] > 0 ||
                                           final_health.session_last_phase_us[1] > 0 ||
                                           final_health.session_last_phase_us[2] > 0;
        esp_websocket_client_destroy(context->client);
        free(context);

        if (!network_setup_is_connected()) {
            retry_seconds = 5;
            continue;
        }

        set_state(TIBBER_ERROR);
        const unsigned delay_seconds = received_session_data ? 5U : retry_seconds;
        if (watchdog_restart) retry_delay(delay_seconds);
        retry_seconds = received_session_data ? 5U : next_retry_seconds(retry_seconds);
    }
}

esp_err_t tibber_client_start(void)
{
    app_config_t config;
    ESP_RETURN_ON_ERROR(app_config_load(&config), TAG, "Could not load Tibber setup");
    if (!app_config_has_tibber(&config)) {
        set_state(TIBBER_NOT_CONFIGURED);
        return ESP_OK;
    }
    return xTaskCreate(tibber_task, "tibber", 9216, NULL, 4, NULL) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}

void tibber_client_get_snapshot(tibber_phase_snapshot_t *snapshot)
{
    if (snapshot == NULL) return;
    memset(snapshot, 0, sizeof(*snapshot));
    int64_t sample_us[3];

    portENTER_CRITICAL(&s_lock);
    memcpy(snapshot->current_a, s_current, sizeof(s_current));
    memcpy(sample_us, s_last_phase_us, sizeof(sample_us));
    memcpy(snapshot->session_live, s_phase_in_current_session, sizeof(s_phase_in_current_session));
    snapshot->stream_live = s_state == TIBBER_LIVE;
    portEXIT_CRITICAL(&s_lock);

    const int64_t now_us = esp_timer_get_time();
    for (int i = 0; i < 3; ++i) {
        snapshot->age_ms[i] = -1;
        if (sample_us[i] <= 0 || now_us < sample_us[i]) continue;
        snapshot->available[i] = true;
        snapshot->age_ms[i] = (now_us - sample_us[i]) / 1000;
    }
}

bool tibber_client_read_phase_with_age(size_t phase_index, float *current_a, int64_t *age_ms)
{
    if (phase_index >= 3) return false;
    tibber_phase_snapshot_t snapshot;
    tibber_client_get_snapshot(&snapshot);
    if (current_a != NULL) *current_a = snapshot.available[phase_index] ? snapshot.current_a[phase_index] : 0.0f;
    if (age_ms != NULL) *age_ms = snapshot.age_ms[phase_index];
    return snapshot.stream_live && snapshot.session_live[phase_index] && snapshot.available[phase_index];
}

bool tibber_client_read_with_age(float current_a[3], int64_t *age_ms)
{
    tibber_phase_snapshot_t snapshot;
    tibber_client_get_snapshot(&snapshot);
    int64_t oldest_ms = 0;
    for (int i = 0; i < 3; ++i) {
        if (!snapshot.stream_live || !snapshot.session_live[i] || !snapshot.available[i] || snapshot.age_ms[i] < 0 ||
            snapshot.age_ms[i] >= TIBBER_LIVE_MAX_AGE_MS) {
            if (age_ms != NULL) *age_ms = -1;
            return false;
        }
        if (snapshot.age_ms[i] > oldest_ms) oldest_ms = snapshot.age_ms[i];
    }
    if (current_a != NULL) memcpy(current_a, snapshot.current_a, sizeof(snapshot.current_a));
    if (age_ms != NULL) *age_ms = oldest_ms;
    return true;
}

bool tibber_client_read(float current_a[3])
{
    return tibber_client_read_with_age(current_a, NULL);
}

const char *tibber_client_status_text(void)
{
    const tibber_state_t state = get_state();
    if (state == TIBBER_LIVE) {
        tibber_phase_snapshot_t snapshot;
        tibber_client_get_snapshot(&snapshot);
        int fresh_count = 0;
        for (int i = 0; i < 3; ++i) {
            if (snapshot.session_live[i] && snapshot.available[i] && snapshot.age_ms[i] >= 0 &&
                snapshot.age_ms[i] < TIBBER_LIVE_MAX_AGE_MS) {
                ++fresh_count;
            }
        }
        if (fresh_count == 3) return "LIVE • TIBBER PULSE";
        if (fresh_count > 0) return "Tibberdata delvis tillgänglig";
        return "Tibberdata gammal • återansluter";
    }

    switch (state) {
        case TIBBER_WAITING_NETWORK: return "Tibber väntar på Wi-Fi";
        case TIBBER_DISCOVERING: return "Kontaktar Tibber…";
        case TIBBER_CONNECTING: return "Ansluter till Tibber Pulse…";
        case TIBBER_NO_PULSE: return "Tibber Pulse saknas för hemmet";
        case TIBBER_ERROR: return "Tibber offline • försöker igen";
        case TIBBER_NOT_CONFIGURED:
        default: return "Tibber är inte konfigurerad";
    }
}
