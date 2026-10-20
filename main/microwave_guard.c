#include "microwave_guard.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#include "shelly_client.h"
#include "tibber_client.h"

#define GUARD_POLL_MS 250

static const char *TAG = "microwave_guard";
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static microwave_guard_status_t s_status = {
    .limit_a = (float)CONFIG_LASTVAKT_MICROWAVE_L1_LIMIT_A,
};

static void publish_status(const microwave_guard_status_t *status)
{
    portENTER_CRITICAL(&s_lock);
    s_status = *status;
    portEXIT_CRITICAL(&s_lock);
}

static void microwave_guard_task(void *arg)
{
    (void)arg;
    microwave_guard_status_t status = {
        .limit_a = (float)CONFIG_LASTVAKT_MICROWAVE_L1_LIMIT_A,
    };

    ESP_LOGI(
        TAG,
        "Armed for automatic OFF above %.0f A on L1 while Shelly load is at least %d W; never automatic ON",
        status.limit_a,
        CONFIG_LASTVAKT_MICROWAVE_ACTIVE_POWER_W);

    while (true) {
        float current_l1_a = 0.0f;
        int64_t sample_age_ms = -1;
        const bool tibber_valid = tibber_client_read_phase_with_age(0, &current_l1_a, &sample_age_ms);
        const bool tibber_fresh = tibber_valid && sample_age_ms >= 0 &&
                                  sample_age_ms <=
                                      (int64_t)CONFIG_LASTVAKT_MICROWAVE_MAX_TIBBER_AGE_SECONDS * 1000;

        shelly_sample_t shelly = {0};
        const shelly_state_t shelly_state = shelly_client_get(&shelly);
        const bool command_pending = shelly_client_command_pending();
        const bool shelly_ready = shelly_state == SHELLY_READY && shelly.available;

        status.protection_available = tibber_fresh && shelly_ready;
        status.current_l1_a = tibber_fresh ? current_l1_a : 0.0f;

        if (status.tripped) {
            /* Only a confirmed external/manual ON clears the latch. This task never requests ON. */
            if (shelly_ready && shelly.output_on && !command_pending) {
                ESP_LOGI(TAG, "Manual re-enable confirmed; microwave guard re-armed");
                status.tripped = false;
                status.trip_l1_a = 0.0f;
            }
        } else if (status.protection_available && shelly.output_on && !command_pending &&
                   shelly.power_w >= (float)CONFIG_LASTVAKT_MICROWAVE_ACTIVE_POWER_W &&
                   current_l1_a > status.limit_a) {
            const esp_err_t err = shelly_client_request_output(false);
            if (err == ESP_OK) {
                status.tripped = true;
                status.trip_l1_a = current_l1_a;
                ESP_LOGW(
                    TAG,
                    "Automatic microwave cutoff: L1 %.2f A > %.2f A, Shelly %.0f W",
                    current_l1_a,
                    status.limit_a,
                    shelly.power_w);
            } else {
                ESP_LOGW(TAG, "Automatic cutoff request failed: %s", esp_err_to_name(err));
            }
        }

        publish_status(&status);
        vTaskDelay(pdMS_TO_TICKS(GUARD_POLL_MS));
    }
}

esp_err_t microwave_guard_start(void)
{
    return xTaskCreate(
               microwave_guard_task,
               "microwave_guard",
               3072,
               NULL,
               4,
               NULL) == pdPASS
               ? ESP_OK
               : ESP_ERR_NO_MEM;
}

microwave_guard_status_t microwave_guard_get(void)
{
    microwave_guard_status_t status;
    portENTER_CRITICAL(&s_lock);
    status = s_status;
    portEXIT_CRITICAL(&s_lock);
    return status;
}
