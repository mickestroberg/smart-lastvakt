#include "weather_source.h"

#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/task.h"
#include "network_setup.h"

#define WEATHER_URL \
    "https://api.open-meteo.com/v1/forecast?latitude=59.27689&longitude=18.00221" \
    "&current=temperature_2m,weather_code,is_day&timezone=Europe%2FStockholm"
#define WEATHER_RESPONSE_BYTES 4096
#define WEATHER_INITIAL_DELAY_MS (30 * 1000)
#define WEATHER_REFRESH_MS (15 * 60 * 1000)
#define WEATHER_RETRY_MS (2 * 60 * 1000)

typedef struct {
    char *data;
    size_t length;
    bool overflow;
} weather_response_t;

static const char *TAG = "weather";
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static weather_source_state_t s_state = WEATHER_SOURCE_SYNCING;
static weather_sample_t s_sample;

static esp_err_t http_event(esp_http_client_event_t *event)
{
    if (event->event_id != HTTP_EVENT_ON_DATA || event->data_len <= 0) {
        return ESP_OK;
    }

    weather_response_t *response = event->user_data;
    const size_t required = response->length + (size_t)event->data_len + 1;
    if (required > WEATHER_RESPONSE_BYTES) {
        response->overflow = true;
        return ESP_FAIL;
    }
    memcpy(response->data + response->length, event->data, (size_t)event->data_len);
    response->length += (size_t)event->data_len;
    response->data[response->length] = '\0';
    return ESP_OK;
}

static esp_err_t fetch_weather(weather_sample_t *sample)
{
    weather_response_t response = {
        .data = heap_caps_calloc(1, WEATHER_RESPONSE_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT),
    };
    if (response.data == NULL) {
        response.data = calloc(1, WEATHER_RESPONSE_BYTES);
    }
    if (response.data == NULL) {
        return ESP_ERR_NO_MEM;
    }

    esp_http_client_config_t config = {
        .url = WEATHER_URL,
        .event_handler = http_event,
        .user_data = &response,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 15000,
        .buffer_size = 2048,
        .buffer_size_tx = 1024,
        .user_agent = "Smart-lastvakt/1.0",
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        free(response.data);
        return ESP_ERR_NO_MEM;
    }

    esp_err_t err = esp_http_client_perform(client);
    const int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    if (err == ESP_OK && status != 200) {
        ESP_LOGW(TAG, "Weather service returned HTTP %d", status);
        err = ESP_FAIL;
    }
    if (err == ESP_OK && (response.overflow || response.length == 0)) {
        err = response.overflow ? ESP_ERR_NO_MEM : ESP_ERR_INVALID_RESPONSE;
    }
    if (err != ESP_OK) {
        free(response.data);
        return err;
    }

    cJSON *root = cJSON_Parse(response.data);
    free(response.data);
    if (root == NULL) {
        return ESP_ERR_INVALID_RESPONSE;
    }

    cJSON *current = cJSON_GetObjectItemCaseSensitive(root, "current");
    cJSON *temperature = cJSON_GetObjectItemCaseSensitive(current, "temperature_2m");
    cJSON *weather_code = cJSON_GetObjectItemCaseSensitive(current, "weather_code");
    cJSON *is_day = cJSON_GetObjectItemCaseSensitive(current, "is_day");
    if (!cJSON_IsNumber(temperature) || !cJSON_IsNumber(weather_code) || !cJSON_IsNumber(is_day)) {
        cJSON_Delete(root);
        return ESP_ERR_INVALID_RESPONSE;
    }

    sample->available = true;
    sample->temperature_c = (float)temperature->valuedouble;
    sample->weather_code = weather_code->valueint;
    sample->is_day = is_day->valueint != 0;
    cJSON_Delete(root);
    return ESP_OK;
}

static void publish(weather_source_state_t state, const weather_sample_t *sample)
{
    portENTER_CRITICAL(&s_lock);
    s_state = state;
    if (sample != NULL) {
        s_sample = *sample;
    }
    portEXIT_CRITICAL(&s_lock);
}

static void weather_task(void *arg)
{
    (void)arg;
    if (!network_setup_wait_connected(portMAX_DELAY)) {
        publish(WEATHER_SOURCE_STALE, NULL);
        vTaskDeleteWithCaps(NULL);
        return;
    }

    /* Let the larger calendar download finish before opening another TLS connection. */
    vTaskDelay(pdMS_TO_TICKS(WEATHER_INITIAL_DELAY_MS));
    while (true) {
        weather_sample_t sample = {0};
        const esp_err_t err = fetch_weather(&sample);
        if (err == ESP_OK) {
            publish(WEATHER_SOURCE_READY, &sample);
            ESP_LOGI(
                TAG,
                "Alvsjo weather synchronized: %.1f C, WMO %d, %s",
                sample.temperature_c,
                sample.weather_code,
                sample.is_day ? "day" : "night");
            vTaskDelay(pdMS_TO_TICKS(WEATHER_REFRESH_MS));
        } else {
            publish(WEATHER_SOURCE_STALE, NULL);
            ESP_LOGW(TAG, "Weather synchronization failed: %s", esp_err_to_name(err));
            vTaskDelay(pdMS_TO_TICKS(WEATHER_RETRY_MS));
        }
    }
}

void weather_source_init(void)
{
    publish(WEATHER_SOURCE_SYNCING, NULL);
    if (xTaskCreateWithCaps(
            weather_task,
            "weather",
            6144,
            NULL,
            3,
            NULL,
            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) != pdPASS) {
        publish(WEATHER_SOURCE_STALE, NULL);
    }
}

weather_source_state_t weather_source_get(weather_sample_t *sample)
{
    portENTER_CRITICAL(&s_lock);
    const weather_source_state_t state = s_state;
    if (sample != NULL) {
        *sample = s_sample;
    }
    portEXIT_CRITICAL(&s_lock);
    return state;
}

