#include "app_config.h"

#include <string.h>

#include "nvs.h"
#include "nvs_flash.h"
#include "esp_check.h"

#define CONFIG_NAMESPACE "lastvakt"

static esp_err_t get_string(nvs_handle_t handle, const char *key, char *value, size_t value_size)
{
    size_t required = value_size;
    esp_err_t err = nvs_get_str(handle, key, value, &required);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        value[0] = '\0';
        return ESP_OK;
    }
    if (err == ESP_ERR_NVS_INVALID_LENGTH) {
        value[0] = '\0';
    }
    return err;
}

esp_err_t app_config_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_RETURN_ON_ERROR(nvs_flash_erase(), "app_config", "Could not erase NVS");
        err = nvs_flash_init();
    }
    return err;
}

esp_err_t app_config_load(app_config_t *config)
{
    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(config, 0, sizeof(*config));
    config->price_summer_pause = true;

    nvs_handle_t handle;
    esp_err_t err = nvs_open(CONFIG_NAMESPACE, NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_OK;
    }
    if (err != ESP_OK) {
        return err;
    }

    err = get_string(handle, "ssid", config->ssid, sizeof(config->ssid));
    if (err == ESP_OK) err = get_string(handle, "wifi_pass", config->wifi_password, sizeof(config->wifi_password));
    if (err == ESP_OK) err = get_string(handle, "tibber", config->tibber_token, sizeof(config->tibber_token));
    if (err == ESP_OK) err = get_string(handle, "home_id", config->tibber_home_id, sizeof(config->tibber_home_id));
    if (err == ESP_OK) err = get_string(handle, "calendar", config->calendar_ical_url, sizeof(config->calendar_ical_url));
    if (err == ESP_OK) err = get_string(handle, "shelly", config->shelly_host, sizeof(config->shelly_host));
    if (err == ESP_OK) {
        uint8_t summer_pause = 1;
        esp_err_t pause_err = nvs_get_u8(handle, "price_summer", &summer_pause);
        if (pause_err == ESP_OK) config->price_summer_pause = summer_pause != 0;
        else if (pause_err != ESP_ERR_NVS_NOT_FOUND) err = pause_err;
    }
    nvs_close(handle);
    return err;
}

esp_err_t app_config_save(const app_config_t *config)
{
    if (config == NULL || config->ssid[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t handle;
    ESP_RETURN_ON_ERROR(nvs_open(CONFIG_NAMESPACE, NVS_READWRITE, &handle), "app_config", "Could not open NVS");
    esp_err_t err = nvs_set_str(handle, "ssid", config->ssid);
    if (err == ESP_OK) err = nvs_set_str(handle, "wifi_pass", config->wifi_password);
    if (err == ESP_OK) err = nvs_set_str(handle, "tibber", config->tibber_token);
    if (err == ESP_OK) err = nvs_set_str(handle, "home_id", config->tibber_home_id);
    if (err == ESP_OK) err = nvs_set_str(handle, "calendar", config->calendar_ical_url);
    if (err == ESP_OK) err = nvs_set_str(handle, "shelly", config->shelly_host);
    if (err == ESP_OK) err = nvs_set_u8(handle, "price_summer", config->price_summer_pause ? 1 : 0);
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    return err;
}

esp_err_t app_config_save_home_id(const char *home_id)
{
    if (home_id == NULL || home_id[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }
    nvs_handle_t handle;
    ESP_RETURN_ON_ERROR(nvs_open(CONFIG_NAMESPACE, NVS_READWRITE, &handle), "app_config", "Could not open NVS");
    esp_err_t err = nvs_set_str(handle, "home_id", home_id);
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    return err;
}

esp_err_t app_config_save_shelly_host(const char *host)
{
    if (host == NULL || host[0] == '\0' || strlen(host) > LASTVAKT_SHELLY_HOST_MAX) {
        return ESP_ERR_INVALID_ARG;
    }
    nvs_handle_t handle;
    ESP_RETURN_ON_ERROR(nvs_open(CONFIG_NAMESPACE, NVS_READWRITE, &handle), "app_config", "Could not open NVS");
    esp_err_t err = nvs_set_str(handle, "shelly", host);
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    return err;
}

bool app_config_has_wifi(const app_config_t *config)
{
    return config != NULL && config->ssid[0] != '\0';
}

bool app_config_has_tibber(const app_config_t *config)
{
    return config != NULL && config->tibber_token[0] != '\0';
}

bool app_config_has_calendar(const app_config_t *config)
{
    return config != NULL && config->calendar_ical_url[0] != '\0';
}

bool app_config_has_shelly(const app_config_t *config)
{
    return config != NULL && config->shelly_host[0] != '\0';
}
