#include "shelly_client.h"

#include <stdlib.h>
#include <string.h>

#include "app_config.h"
#include "cJSON.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/task.h"
#include "network_setup.h"

#define SHELLY_RESPONSE_BYTES 2048
#define SHELLY_POLL_MS 2000
#define SHELLY_RETRY_MS 5000

typedef struct {
    char *data;
    size_t length;
    bool overflow;
} shelly_response_t;

static const char *TAG = "shelly";
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static shelly_state_t s_state = SHELLY_DISABLED;
static shelly_sample_t s_sample;
static bool s_command_pending;
static bool s_command_target;

static esp_err_t http_event(esp_http_client_event_t *event)
{
    if (event->event_id != HTTP_EVENT_ON_DATA || event->data_len <= 0) {
        return ESP_OK;
    }

    shelly_response_t *response = event->user_data;
    const size_t required = response->length + (size_t)event->data_len + 1;
    if (required > SHELLY_RESPONSE_BYTES) {
        response->overflow = true;
        return ESP_FAIL;
    }
    memcpy(response->data + response->length, event->data, (size_t)event->data_len);
    response->length += (size_t)event->data_len;
    response->data[response->length] = '\0';
    return ESP_OK;
}

static void publish(shelly_state_t state, const shelly_sample_t *sample)
{
    portENTER_CRITICAL(&s_lock);
    s_state = state;
    if (sample != NULL) {
        s_sample = *sample;
    }
    portEXIT_CRITICAL(&s_lock);
}

static bool json_number(cJSON *object, const char *name, float *value)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(object, name);
    if (!cJSON_IsNumber(item)) return false;
    *value = (float)item->valuedouble;
    return true;
}

static esp_err_t fetch_status(const char *host, shelly_sample_t *sample)
{
    char url[LASTVAKT_SHELLY_HOST_MAX + 64];
    snprintf(
        url,
        sizeof(url),
        "%s%s/rpc/Switch.GetStatus?id=0",
        strncmp(host, "http://", 7) == 0 ? "" : "http://",
        host);

    shelly_response_t response = {
        .data = heap_caps_calloc(1, SHELLY_RESPONSE_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT),
    };
    if (response.data == NULL) response.data = calloc(1, SHELLY_RESPONSE_BYTES);
    if (response.data == NULL) return ESP_ERR_NO_MEM;

    esp_http_client_config_t config = {
        .url = url,
        .event_handler = http_event,
        .user_data = &response,
        .timeout_ms = 3000,
        .buffer_size = 1024,
        .buffer_size_tx = 512,
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
    if (err == ESP_OK && status != 200) err = ESP_FAIL;
    if (err == ESP_OK && (response.overflow || response.length == 0)) {
        err = response.overflow ? ESP_ERR_NO_MEM : ESP_ERR_INVALID_RESPONSE;
    }
    if (err != ESP_OK) {
        free(response.data);
        return err;
    }

    cJSON *root = cJSON_Parse(response.data);
    free(response.data);
    if (root == NULL) return ESP_ERR_INVALID_RESPONSE;

    cJSON *output = cJSON_GetObjectItemCaseSensitive(root, "output");
    cJSON *temperature = cJSON_GetObjectItemCaseSensitive(root, "temperature");
    cJSON *energy = cJSON_GetObjectItemCaseSensitive(root, "aenergy");
    float total_wh = 0.0f;
    const bool valid =
        cJSON_IsBool(output) &&
        json_number(root, "apower", &sample->power_w) &&
        json_number(root, "current", &sample->current_a) &&
        json_number(root, "voltage", &sample->voltage_v) &&
        json_number(temperature, "tC", &sample->temperature_c) &&
        json_number(energy, "total", &total_wh);
    if (valid) {
        sample->available = true;
        sample->output_on = cJSON_IsTrue(output);
        sample->total_wh = total_wh;
    }
    cJSON_Delete(root);
    return valid ? ESP_OK : ESP_ERR_INVALID_RESPONSE;
}

static esp_err_t set_output(const char *host, bool on)
{
    char url[LASTVAKT_SHELLY_HOST_MAX + 64];
    snprintf(
        url,
        sizeof(url),
        "%s%s/rpc/Switch.Set?id=0&on=%s",
        strncmp(host, "http://", 7) == 0 ? "" : "http://",
        host,
        on ? "true" : "false");

    esp_http_client_config_t config = {
        .url = url,
        .timeout_ms = 3000,
        .buffer_size = 512,
        .buffer_size_tx = 512,
        .user_agent = "Smart-lastvakt/1.0",
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) return ESP_ERR_NO_MEM;

    esp_err_t err = esp_http_client_perform(client);
    const int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    if (err == ESP_OK && status != 200) err = ESP_FAIL;
    return err;
}

static bool pending_command(bool *target)
{
    portENTER_CRITICAL(&s_lock);
    const bool pending = s_command_pending;
    if (pending && target != NULL) *target = s_command_target;
    portEXIT_CRITICAL(&s_lock);
    return pending;
}

static void finish_command(void)
{
    portENTER_CRITICAL(&s_lock);
    s_command_pending = false;
    portEXIT_CRITICAL(&s_lock);
}

static void shelly_task(void *arg)
{
    (void)arg;
    app_config_t config;
    if (app_config_load(&config) != ESP_OK || !app_config_has_shelly(&config)) {
        publish(SHELLY_DISABLED, NULL);
        vTaskDeleteWithCaps(NULL);
        return;
    }
    if (!network_setup_wait_connected(portMAX_DELAY)) {
        publish(SHELLY_STALE, NULL);
        vTaskDeleteWithCaps(NULL);
        return;
    }

    while (true) {
        bool target = false;
        const bool command_waiting = pending_command(&target);
        if (command_waiting) {
            const esp_err_t command_err = set_output(config.shelly_host, target);
            if (command_err == ESP_OK) {
                ESP_LOGI(TAG, "Relay command completed: %s", target ? "on" : "off");
                vTaskDelay(pdMS_TO_TICKS(250));
            } else {
                ESP_LOGW(TAG, "Relay command failed: %s", esp_err_to_name(command_err));
                publish(SHELLY_STALE, NULL);
                finish_command();
            }
        }

        shelly_sample_t sample = {0};
        const esp_err_t err = fetch_status(config.shelly_host, &sample);
        if (err == ESP_OK) {
            const bool first_sample = s_state != SHELLY_READY;
            publish(SHELLY_READY, &sample);
            if (first_sample) {
                ESP_LOGI(
                    TAG,
                    "Connected to %s: output %s, %.1f W, %.1f V",
                    config.shelly_host,
                    sample.output_on ? "on" : "off",
                    sample.power_w,
                    sample.voltage_v);
            }
            if (command_waiting) finish_command();
            vTaskDelay(pdMS_TO_TICKS(SHELLY_POLL_MS));
        } else {
            if (s_state != SHELLY_STALE) {
                ESP_LOGW(TAG, "Shelly status failed: %s", esp_err_to_name(err));
            }
            publish(SHELLY_STALE, NULL);
            if (command_waiting) finish_command();
            vTaskDelay(pdMS_TO_TICKS(SHELLY_RETRY_MS));
        }
    }
}

esp_err_t shelly_client_start(void)
{
    app_config_t config;
    if (app_config_load(&config) != ESP_OK || !app_config_has_shelly(&config)) {
        publish(SHELLY_DISABLED, NULL);
        return ESP_OK;
    }

    publish(SHELLY_CONNECTING, NULL);
    return xTaskCreateWithCaps(
               shelly_task,
               "shelly",
               5120,
               NULL,
               3,
               NULL,
               MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) == pdPASS
               ? ESP_OK
               : ESP_ERR_NO_MEM;
}

shelly_state_t shelly_client_get(shelly_sample_t *sample)
{
    portENTER_CRITICAL(&s_lock);
    const shelly_state_t state = s_state;
    if (sample != NULL) *sample = s_sample;
    portEXIT_CRITICAL(&s_lock);
    return state;
}

esp_err_t shelly_client_request_output(bool on)
{
    esp_err_t result = ESP_OK;
    portENTER_CRITICAL(&s_lock);
    if (s_state != SHELLY_READY || !s_sample.available || s_command_pending) {
        result = ESP_ERR_INVALID_STATE;
    } else {
        s_command_target = on;
        s_command_pending = true;
    }
    portEXIT_CRITICAL(&s_lock);
    return result;
}

bool shelly_client_command_pending(void)
{
    portENTER_CRITICAL(&s_lock);
    const bool pending = s_command_pending;
    portEXIT_CRITICAL(&s_lock);
    return pending;
}

const char *shelly_client_status_text(void)
{
    switch (s_state) {
        case SHELLY_CONNECTING: return "Ansluter till Shelly…";
        case SHELLY_READY: return "Shelly Plug S Gen3 är ansluten";
        case SHELLY_STALE: return s_sample.available ? "Shelly • senaste mätvärdet" : "Shelly kan inte nås";
        case SHELLY_DISABLED:
        default: return "Shelly är inte konfigurerad";
    }
}

