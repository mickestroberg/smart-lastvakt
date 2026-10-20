#include "phase_source.h"

#include "sdkconfig.h"
#include "tibber_client.h"

#define LAST_VALUE_MAX_AGE_MS (5LL * 60LL * 1000LL)

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
    const float fuse = (float)CONFIG_LASTVAKT_MAIN_FUSE_A;
    phase_sample_t sample = {.main_fuse_a = fuse};
    tibber_phase_snapshot_t snapshot;
    tibber_client_get_snapshot(&snapshot);

    int available_count = 0;
    int fresh_count = 0;

    for (int i = 0; i < 3; ++i) {
        sample.available[i] = snapshot.available[i] && snapshot.age_ms[i] >= 0 &&
                              snapshot.age_ms[i] < LAST_VALUE_MAX_AGE_MS;
        sample.fresh[i] = snapshot.stream_live && snapshot.session_live[i] && snapshot.available[i] && snapshot.age_ms[i] >= 0 &&
                          snapshot.age_ms[i] < TIBBER_LIVE_MAX_AGE_MS;
        if (sample.available[i]) ++available_count;
        if (sample.fresh[i]) ++fresh_count;
        sample.current_a[i] = sample.available[i] ? snapshot.current_a[i] : 0.0f;
        sample.current_a[i] = clampf(sample.current_a[i], 0.0f, fuse * 1.15f);
        sample.percent[i] = clampf((sample.current_a[i] / fuse) * 100.0f, 0.0f, 115.0f);
    }

    if (fresh_count == 3) sample.state = PHASE_SOURCE_LIVE;
    else if (fresh_count > 0) sample.state = PHASE_SOURCE_PARTIAL;
    else if (available_count > 0) sample.state = PHASE_SOURCE_STALE;
    else sample.state = PHASE_SOURCE_UNAVAILABLE;

    return sample;
}

const char *phase_source_status_text(void)
{
    return tibber_client_status_text();
}
