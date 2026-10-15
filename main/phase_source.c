#include "phase_source.h"

#include <math.h>

#include "esp_timer.h"
#include "sdkconfig.h"
#include "tibber_client.h"

void phase_source_init(void)
{
    ESP_ERROR_CHECK(tibber_client_start());
}

static float clampf(float value, float low, float high)
{
    if (value < low) {
        return low;
    }
    if (value > high) {
        return high;
    }
    return value;
}

phase_sample_t phase_source_read(void)
{
    const float seconds = (float)(esp_timer_get_time() / 1000) / 1000.0f;
    const float fuse = (float)CONFIG_LASTVAKT_MAIN_FUSE_A;

    phase_sample_t sample = {.main_fuse_a = fuse};
    sample.simulated = !tibber_client_read(sample.current_a);
    if (sample.simulated) {
        sample.current_a[0] = 6.4f + 5.2f * sinf(seconds * 0.17f);
        sample.current_a[1] = 5.7f + 4.8f * sinf(seconds * 0.13f + 2.1f);
        sample.current_a[2] = 4.9f + 4.1f * sinf(seconds * 0.11f + 4.2f);
    }

    for (int i = 0; i < 3; ++i) {
        sample.current_a[i] = clampf(sample.current_a[i], 0.0f, fuse * 1.15f);
        sample.percent[i] = clampf((sample.current_a[i] / fuse) * 100.0f, 0.0f, 115.0f);
    }

    return sample;
}

const char *phase_source_status_text(void)
{
    return tibber_client_status_text();
}
