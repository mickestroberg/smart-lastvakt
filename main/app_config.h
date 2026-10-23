#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"

#define LASTVAKT_SSID_MAX 32
#define LASTVAKT_WIFI_PASSWORD_MAX 64
#define LASTVAKT_TIBBER_TOKEN_MAX 255
#define LASTVAKT_HOME_ID_MAX 63
#define LASTVAKT_CALENDAR_URL_MAX 255
#define LASTVAKT_SHELLY_HOST_MAX 95

typedef struct {
    char ssid[LASTVAKT_SSID_MAX + 1];
    char wifi_password[LASTVAKT_WIFI_PASSWORD_MAX + 1];
    char tibber_token[LASTVAKT_TIBBER_TOKEN_MAX + 1];
    char tibber_home_id[LASTVAKT_HOME_ID_MAX + 1];
    char calendar_ical_url[LASTVAKT_CALENDAR_URL_MAX + 1];
    char shelly_host[LASTVAKT_SHELLY_HOST_MAX + 1];
    bool price_summer_pause;
} app_config_t;

esp_err_t app_config_init(void);
esp_err_t app_config_load(app_config_t *config);
esp_err_t app_config_save(const app_config_t *config);
esp_err_t app_config_save_home_id(const char *home_id);
esp_err_t app_config_save_shelly_host(const char *host);
bool app_config_has_wifi(const app_config_t *config);
bool app_config_has_tibber(const app_config_t *config);
bool app_config_has_calendar(const app_config_t *config);
bool app_config_has_shelly(const app_config_t *config);
