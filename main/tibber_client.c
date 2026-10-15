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
#define USER_AGENT "smart-lastvakt/0.2.0 ESP32-S3"
#define LIVE_MAX_AGE_US (30LL * 1000LL * 1000LL)

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
    esp_websocket_client_handle_t client;
    char home_id[LASTVAKT_HOME_ID_MAX + 1];
    char frame[4096];
    size_t frame_length;
} websocket_context_t;

static const char *TAG = "tibber";
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static tibber_state_t s_state = TIBBER_NOT_CONFIGURED;
static float s_current[3];
static int64_t s_last_sample_us;

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

static void process_live_message(const char *message)
{
    cJSON *root = cJSON_Parse(message);
    if (root == NULL) return;
    cJSON *type = cJSON_GetObjectItemCaseSensitive(root, "type");
    if (!cJSON_IsString(type) || strcmp(type->valuestring, "next") != 0) {
        cJSON_Delete(root);
        return;
    }
    cJSON *payload = cJSON_GetObjectItemCaseSensitive(root, "payload");
    cJSON *data = cJSON_GetObjectItemCaseSensitive(payload, "data");
    cJSON *live = cJSON_GetObjectItemCaseSensitive(data, "liveMeasurement");
    cJSON *l1 = cJSON_GetObjectItemCaseSensitive(live, "currentL1");
    cJSON *l2 = cJSON_GetObjectItemCaseSensitive(live, "currentL2");
    cJSON *l3 = cJSON_GetObjectItemCaseSensitive(live, "currentL3");
    if (cJSON_IsNumber(l1) && cJSON_IsNumber(l2) && cJSON_IsNumber(l3)) {
        portENTER_CRITICAL(&s_lock);
        s_current[0] = (float)l1->valuedouble;
        s_current[1] = (float)l2->valuedouble;
        s_current[2] = (float)l3->valuedouble;
        s_last_sample_us = esp_timer_get_time();
        s_state = TIBBER_LIVE;
        portEXIT_CRITICAL(&s_lock);
    }
    cJSON_Delete(root);
}

static void send_subscription(websocket_context_t *context)
{
    char message[512];
    snprintf(message, sizeof(message),
             "{\"id\":\"lastvakt\",\"type\":\"subscribe\",\"payload\":{\"query\":\"subscription { liveMeasurement(homeId: \\\"%s\\\") { timestamp power currentL1 currentL2 currentL3 } }\"}}",
             context->home_id);
    esp_websocket_client_send_text(context->client, message, strlen(message), portMAX_DELAY);
}

static void websocket_event(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    (void)base;
    websocket_context_t *context = handler_args;
    esp_websocket_event_data_t *event = event_data;
    if (event_id == WEBSOCKET_EVENT_CONNECTED) {
        s_state = TIBBER_CONNECTING;
        static const char init[] = "{\"type\":\"connection_init\"}";
        esp_websocket_client_send_text(context->client, init, strlen(init), portMAX_DELAY);
    } else if (event_id == WEBSOCKET_EVENT_DATA && event->op_code == 0x1) {
        if (event->payload_offset == 0) context->frame_length = 0;
        size_t available = sizeof(context->frame) - context->frame_length - 1;
        size_t copy = (size_t)event->data_len < available ? (size_t)event->data_len : available;
        memcpy(context->frame + context->frame_length, event->data_ptr, copy);
        context->frame_length += copy;
        context->frame[context->frame_length] = '\0';
        if (event->payload_offset + event->data_len >= event->payload_len) {
            cJSON *root = cJSON_Parse(context->frame);
            cJSON *type = root == NULL ? NULL : cJSON_GetObjectItemCaseSensitive(root, "type");
            bool acknowledged = cJSON_IsString(type) && strcmp(type->valuestring, "connection_ack") == 0;
            cJSON_Delete(root);
            if (acknowledged) send_subscription(context);
            else process_live_message(context->frame);
        }
    } else if (event_id == WEBSOCKET_EVENT_DISCONNECTED || event_id == WEBSOCKET_EVENT_ERROR) {
        if (s_state == TIBBER_LIVE || s_state == TIBBER_CONNECTING) s_state = TIBBER_ERROR;
    }
}

static void tibber_task(void *arg)
{
    (void)arg;
    unsigned retry_seconds = 5;
    for (;;) {
        s_state = TIBBER_WAITING_NETWORK;
        if (!network_setup_wait_connected(portMAX_DELAY)) continue;

        app_config_t config;
        if (app_config_load(&config) != ESP_OK || !app_config_has_tibber(&config)) {
            s_state = TIBBER_NOT_CONFIGURED;
            vTaskDelete(NULL);
        }

        s_state = TIBBER_DISCOVERING;
        char url[256] = {0};
        websocket_context_t *context = calloc(1, sizeof(*context));
        if (context == NULL) {
            s_state = TIBBER_ERROR;
            vTaskDelay(pdMS_TO_TICKS(30000));
            continue;
        }
        esp_err_t err = discover(&config, url, sizeof(url), context->home_id, sizeof(context->home_id));
        if (err == ESP_ERR_NOT_FOUND) s_state = TIBBER_NO_PULSE;
        else if (err != ESP_OK) s_state = TIBBER_ERROR;
        if (err != ESP_OK) {
            free(context);
            unsigned jitter_ms = esp_random() % 1000;
            vTaskDelay(pdMS_TO_TICKS(retry_seconds * 1000 + jitter_ms));
            if (retry_seconds < 120) retry_seconds *= 2;
            continue;
        }
        retry_seconds = 5;
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
        };
        context->client = esp_websocket_client_init(&ws_config);
        if (context->client == NULL) {
            memset(authorization, 0, sizeof(authorization));
            free(context);
            s_state = TIBBER_ERROR;
            vTaskDelay(pdMS_TO_TICKS(30000));
            continue;
        }
        esp_websocket_register_events(context->client, WEBSOCKET_EVENT_ANY, websocket_event, context);
        s_state = TIBBER_CONNECTING;
        err = esp_websocket_client_start(context->client);
        memset(authorization, 0, sizeof(authorization));
        if (err != ESP_OK) {
            esp_websocket_client_destroy(context->client);
            free(context);
            s_state = TIBBER_ERROR;
            vTaskDelay(pdMS_TO_TICKS(30000));
            continue;
        }

        while (network_setup_is_connected()) vTaskDelay(pdMS_TO_TICKS(1000));
        esp_websocket_client_stop(context->client);
        esp_websocket_client_destroy(context->client);
        free(context);
    }
}

esp_err_t tibber_client_start(void)
{
    app_config_t config;
    ESP_RETURN_ON_ERROR(app_config_load(&config), TAG, "Could not load Tibber setup");
    if (!app_config_has_tibber(&config)) {
        s_state = TIBBER_NOT_CONFIGURED;
        return ESP_OK;
    }
    return xTaskCreate(tibber_task, "tibber", 9216, NULL, 4, NULL) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}

bool tibber_client_read(float current_a[3])
{
    bool valid;
    portENTER_CRITICAL(&s_lock);
    valid = s_last_sample_us > 0 && esp_timer_get_time() - s_last_sample_us < LIVE_MAX_AGE_US;
    if (valid) memcpy(current_a, s_current, sizeof(s_current));
    portEXIT_CRITICAL(&s_lock);
    return valid;
}

const char *tibber_client_status_text(void)
{
    switch (s_state) {
        case TIBBER_WAITING_NETWORK: return "Tibber väntar på Wi-Fi";
        case TIBBER_DISCOVERING: return "Kontaktar Tibber…";
        case TIBBER_CONNECTING: return "Ansluter till Tibber Pulse…";
        case TIBBER_LIVE: return "LIVE • TIBBER PULSE";
        case TIBBER_NO_PULSE: return "Tibber Pulse saknas för hemmet";
        case TIBBER_ERROR: return "Tibber offline • simulerar";
        case TIBBER_NOT_CONFIGURED:
        default: return "Tibber inte konfigurerad • simulerar";
    }
}
