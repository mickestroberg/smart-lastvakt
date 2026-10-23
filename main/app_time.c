#include "app_time.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

#include "driver/i2c.h"
#include "esp_log.h"
#include "esp_netif_sntp.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "network_setup.h"
#include "waveshare_rgb_lcd_port.h"

#define PCF85063A_ADDRESS 0x51
#define PCF85063A_SECONDS_REGISTER 0x04

typedef enum {
    TIME_SOURCE_BUILD,
    TIME_SOURCE_RTC,
    TIME_SOURCE_CONNECTING,
    TIME_SOURCE_NTP,
    TIME_SOURCE_NETWORK_ERROR,
} time_source_t;

static const char *TAG = "app_time";
static portMUX_TYPE s_time_lock = portMUX_INITIALIZER_UNLOCKED;
static time_source_t s_time_source = TIME_SOURCE_BUILD;
static app_time_quality_t s_time_quality = APP_TIME_QUALITY_UNTRUSTED;

static void set_time_source(time_source_t source)
{
    portENTER_CRITICAL(&s_time_lock);
    s_time_source = source;
    portEXIT_CRITICAL(&s_time_lock);
}

static void set_time_quality(app_time_quality_t quality)
{
    portENTER_CRITICAL(&s_time_lock);
    s_time_quality = quality;
    portEXIT_CRITICAL(&s_time_lock);
}

static time_source_t get_time_source(void)
{
    portENTER_CRITICAL(&s_time_lock);
    const time_source_t source = s_time_source;
    portEXIT_CRITICAL(&s_time_lock);
    return source;
}

static uint8_t bcd_to_dec(uint8_t value)
{
    return (uint8_t)(((value >> 4) * 10) + (value & 0x0f));
}

static uint8_t dec_to_bcd(uint8_t value)
{
    return (uint8_t)(((value / 10) << 4) | (value % 10));
}

static bool rtc_read(struct tm *result)
{
    uint8_t first_register = PCF85063A_SECONDS_REGISTER;
    uint8_t raw[7] = {0};
    esp_err_t err = i2c_master_write_read_device(
        I2C_MASTER_NUM,
        PCF85063A_ADDRESS,
        &first_register,
        1,
        raw,
        sizeof(raw),
        pdMS_TO_TICKS(I2C_MASTER_TIMEOUT_MS));

    if (err != ESP_OK || (raw[0] & 0x80) != 0) {
        return false;
    }

    struct tm candidate = {
        .tm_sec = bcd_to_dec(raw[0] & 0x7f),
        .tm_min = bcd_to_dec(raw[1] & 0x7f),
        .tm_hour = bcd_to_dec(raw[2] & 0x3f),
        .tm_mday = bcd_to_dec(raw[3] & 0x3f),
        .tm_wday = bcd_to_dec(raw[4] & 0x07),
        .tm_mon = bcd_to_dec(raw[5] & 0x1f) - 1,
        .tm_year = (2000 + bcd_to_dec(raw[6])) - 1900,
        .tm_isdst = -1,
    };

    const int year = candidate.tm_year + 1900;
    if (year < 2024 || year > 2035 || candidate.tm_mon < 0 || candidate.tm_mon > 11 ||
        candidate.tm_mday < 1 || candidate.tm_mday > 31 || candidate.tm_hour > 23 ||
        candidate.tm_min > 59 || candidate.tm_sec > 59) {
        return false;
    }

    *result = candidate;
    return true;
}

static void rtc_write_local_time(void)
{
    time_t now = time(NULL);
    struct tm local = {0};
    localtime_r(&now, &local);

    uint8_t payload[8] = {
        PCF85063A_SECONDS_REGISTER,
        dec_to_bcd((uint8_t)local.tm_sec),
        dec_to_bcd((uint8_t)local.tm_min),
        dec_to_bcd((uint8_t)local.tm_hour),
        dec_to_bcd((uint8_t)local.tm_mday),
        dec_to_bcd((uint8_t)local.tm_wday),
        dec_to_bcd((uint8_t)(local.tm_mon + 1)),
        dec_to_bcd((uint8_t)((local.tm_year + 1900) % 100)),
    };

    esp_err_t err = i2c_master_write_to_device(
        I2C_MASTER_NUM,
        PCF85063A_ADDRESS,
        payload,
        sizeof(payload),
        pdMS_TO_TICKS(I2C_MASTER_TIMEOUT_MS));
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Could not update RTC: %s", esp_err_to_name(err));
    }
}

static bool build_time(struct tm *result)
{
    static const char *months = "JanFebMarAprMayJunJulAugSepOctNovDec";
    char month_name[4] = {0};
    int day = 0;
    int year = 0;
    int hour = 0;
    int minute = 0;
    int second = 0;

    if (sscanf(__DATE__, "%3s %d %d", month_name, &day, &year) != 3 ||
        sscanf(__TIME__, "%d:%d:%d", &hour, &minute, &second) != 3) {
        return false;
    }

    const char *month = strstr(months, month_name);
    if (month == NULL) {
        return false;
    }

    *result = (struct tm){
        .tm_sec = second,
        .tm_min = minute,
        .tm_hour = hour,
        .tm_mday = day,
        .tm_mon = (int)((month - months) / 3),
        .tm_year = year - 1900,
        .tm_isdst = -1,
    };
    return true;
}

static void set_system_time_from_local(struct tm *local)
{
    time_t epoch = mktime(local);
    if (epoch < 0) {
        return;
    }

    struct timeval tv = {
        .tv_sec = epoch,
        .tv_usec = 0,
    };
    settimeofday(&tv, NULL);
}

void app_time_init(void)
{
    /* POSIX rule for Europe/Stockholm: CET, with the EU summer-time switch. */
    setenv("TZ", "CET-1CEST,M3.5.0/2,M10.5.0/3", 1);
    tzset();

    struct tm local = {0};
    if (rtc_read(&local)) {
        set_system_time_from_local(&local);
        set_time_quality(APP_TIME_QUALITY_RTC);
        set_time_source(TIME_SOURCE_RTC);
        ESP_LOGI(TAG, "System clock initialized from PCF85063A");
        return;
    }

    if (build_time(&local)) {
        set_system_time_from_local(&local);
    }
    set_time_quality(APP_TIME_QUALITY_UNTRUSTED);
    set_time_source(TIME_SOURCE_BUILD);
    ESP_LOGW(TAG, "RTC time unavailable; using firmware build time until NTP sync");
}

static void wifi_time_task(void *arg)
{
    (void)arg;
    set_time_source(TIME_SOURCE_CONNECTING);
    if (network_setup_wait_connected(portMAX_DELAY)) {
        esp_sntp_config_t sntp_config = ESP_NETIF_SNTP_DEFAULT_CONFIG("se.pool.ntp.org");
        esp_netif_sntp_init(&sntp_config);
        while (esp_netif_sntp_sync_wait(pdMS_TO_TICKS(15000)) != ESP_OK) {
            set_time_source(TIME_SOURCE_NETWORK_ERROR);
            ESP_LOGW(TAG, "NTP synchronization timed out; retrying in 60 seconds");
            vTaskDelay(pdMS_TO_TICKS(60000));
        }
        rtc_write_local_time();
        set_time_quality(APP_TIME_QUALITY_NETWORK);
        set_time_source(TIME_SOURCE_NTP);
        ESP_LOGI(TAG, "Swedish local time synchronized with NTP");
    } else {
        set_time_source(TIME_SOURCE_NETWORK_ERROR);
        ESP_LOGW(TAG, "Wi-Fi connection failed or timed out");
    }

    vTaskDelete(NULL);
}

void app_time_start_network_sync(void)
{
    xTaskCreate(wifi_time_task, "wifi_time", 6144, NULL, 4, NULL);
}

void app_time_format(char *clock_text, size_t clock_size, char *date_text, size_t date_size)
{
    static const char *weekday[] = {
        "Söndag", "Måndag", "Tisdag", "Onsdag", "Torsdag", "Fredag", "Lördag",
    };
    static const char *month[] = {
        "januari", "februari", "mars", "april", "maj", "juni",
        "juli", "augusti", "september", "oktober", "november", "december",
    };

    time_t now = time(NULL);
    struct tm local = {0};
    localtime_r(&now, &local);

    snprintf(clock_text, clock_size, "%02d:%02d", local.tm_hour, local.tm_min);
    snprintf(
        date_text,
        date_size,
        "%s %d %s",
        weekday[local.tm_wday],
        local.tm_mday,
        month[local.tm_mon]);
}

const char *app_time_status_text(void)
{
    switch (get_time_source()) {
        case TIME_SOURCE_RTC:
            return "RTC • väntar på NTP";
        case TIME_SOURCE_CONNECTING:
            return "Synkar tid…";
        case TIME_SOURCE_NTP:
            return "NTP synkat • svensk tid";
        case TIME_SOURCE_NETWORK_ERROR:
            return "NTP väntar på nytt försök • RTC";
        case TIME_SOURCE_BUILD:
        default:
            return "RTC saknas • använder byggtid";
    }
}

app_time_quality_t app_time_get_quality(void)
{
    portENTER_CRITICAL(&s_time_lock);
    const app_time_quality_t quality = s_time_quality;
    portEXIT_CRITICAL(&s_time_lock);
    return quality;
}
