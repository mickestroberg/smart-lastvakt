#include "network_setup.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app_config.h"
#include "esp_check.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"

#define CONNECTED_BIT BIT0
#define MAX_STA_RETRIES 8

typedef enum {
    NET_STARTING,
    NET_SETUP_AP,
    NET_CONNECTING,
    NET_CONNECTED,
    NET_FAILED,
} network_state_t;

static const char *TAG = "network_setup";
static EventGroupHandle_t s_events;
static network_state_t s_state = NET_STARTING;
static int s_retries;
static bool s_ap_started;
static char s_address[64] = "Startar nätverk…";
static char s_url[40] = "http://192.168.4.1";

static const char SETUP_HTML[] =
    "<!doctype html><html lang='sv'><head><meta charset='utf-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>Smart lastvakt</title><style>"
    "body{font-family:system-ui;background:#07111f;color:#f3f7fa;margin:0;padding:24px}"
    "main{max-width:560px;margin:auto;background:#0d1b2b;padding:28px;border-radius:20px}"
    "h1{color:#34d6e7}label{display:block;margin:18px 0 6px}"
    "input{box-sizing:border-box;width:100%;padding:12px;border-radius:10px;border:1px solid #38506a;background:#13263a;color:white}"
    "button{margin-top:24px;padding:13px 22px;border:0;border-radius:22px;background:#34d6e7;color:#07111f;font-weight:700}"
    "small{color:#9eb0c0;display:block;margin-top:16px}</style></head><body><main>"
    "<h1>Smart lastvakt</h1><p>Ange hemnätverket och anslutningarna. Uppgifterna sparas bara lokalt i displayen.</p>"
    "<form method='post' action='/save'>"
    "<label for='ssid'>Wi-Fi-namn (SSID)</label><input id='ssid' name='ssid' maxlength='32' required>"
    "<label for='password'>Wi-Fi-lösenord</label><input id='password' name='password' type='password' maxlength='64' placeholder='Lämna tomt för att behålla sparat'>"
    "<label for='token'>Tibber Personal Access Token</label><input id='token' name='token' type='password' maxlength='255' placeholder='Lämna tomt för att behålla sparad token'>"
    "<label for='calendar'>Google Kalender – hemlig adress i iCal-format</label><input id='calendar' name='calendar' type='password' maxlength='255' placeholder='Lämna tomt för att behålla sparad adress'>"
    "<label for='shelly'>Shelly – IP-adress eller värdnamn</label><input id='shelly' name='shelly' maxlength='95' placeholder='Exempel: 192.168.68.121'>"
    "<button type='submit'>Spara och starta om</button></form>"
    "<small>Tibber-token skapas på developer.tibber.com. Kalenderadressen finns under Google Kalender → Inställningar → Integrera kalender → Hemlig adress i iCal-format.</small>"
    "</main></body></html>";

static void start_setup_ap(void)
{
    if (s_ap_started) {
        return;
    }

    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
    wifi_config_t ap = {0};
    snprintf((char *)ap.ap.ssid, sizeof(ap.ap.ssid), "Lastvakt-Setup-%02X%02X", mac[4], mac[5]);
    ap.ap.ssid_len = strlen((char *)ap.ap.ssid);
    ap.ap.channel = 1;
    ap.ap.max_connection = 4;
    ap.ap.authmode = WIFI_AUTH_OPEN;

    wifi_mode_t mode = WIFI_MODE_NULL;
    ESP_ERROR_CHECK(esp_wifi_get_mode(&mode));
    ESP_ERROR_CHECK(esp_wifi_set_mode(mode == WIFI_MODE_STA ? WIFI_MODE_APSTA : WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap));
    s_ap_started = true;
    s_state = NET_SETUP_AP;
    snprintf(s_url, sizeof(s_url), "http://192.168.4.1");
    snprintf(s_address, sizeof(s_address), "%s • 192.168.4.1", (char *)ap.ap.ssid);
    ESP_LOGI(TAG, "Setup network ready: %s", s_address);
}

static void wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        s_state = NET_CONNECTING;
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        xEventGroupClearBits(s_events, CONNECTED_BIT);
        if (++s_retries <= MAX_STA_RETRIES) {
            esp_wifi_connect();
        } else {
            s_state = NET_FAILED;
            start_setup_ap();
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *event = data;
        s_retries = 0;
        s_state = NET_CONNECTED;
        snprintf(s_url, sizeof(s_url), "http://" IPSTR, IP2STR(&event->ip_info.ip));
        snprintf(s_address, sizeof(s_address), "Inställningar: http://" IPSTR, IP2STR(&event->ip_info.ip));
        xEventGroupSetBits(s_events, CONNECTED_BIT);
        ESP_LOGI(TAG, "Wi-Fi connected; %s", s_address);
    }
}

static esp_err_t page_get(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, SETUP_HTML, HTTPD_RESP_USE_STRLEN);
}

static int hex_value(char value)
{
    if (value >= '0' && value <= '9') return value - '0';
    value = (char)tolower((unsigned char)value);
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    return -1;
}

static void url_decode(char *value)
{
    char *read = value;
    char *write = value;
    while (*read != '\0') {
        if (*read == '+' ) {
            *write++ = ' ';
            ++read;
        } else if (*read == '%' && read[1] != '\0' && read[2] != '\0') {
            int high = hex_value(read[1]);
            int low = hex_value(read[2]);
            if (high >= 0 && low >= 0) {
                *write++ = (char)((high << 4) | low);
                read += 3;
            } else {
                *write++ = *read++;
            }
        } else {
            *write++ = *read++;
        }
    }
    *write = '\0';
}

static bool form_value(const char *body, const char *name, char *output, size_t output_size)
{
    char key[32];
    snprintf(key, sizeof(key), "%s=", name);
    const char *start = strstr(body, key);
    if (start == NULL) return false;
    start += strlen(key);
    const char *end = strchr(start, '&');
    size_t length = end == NULL ? strlen(start) : (size_t)(end - start);
    if (length >= output_size) length = output_size - 1;
    memcpy(output, start, length);
    output[length] = '\0';
    url_decode(output);
    return true;
}

static void restart_task(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(900));
    esp_restart();
}

static esp_err_t save_post(httpd_req_t *req)
{
    if (req->content_len <= 0 || req->content_len > 2048) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Ogiltig formulärdata");
    }

    char *body = calloc(1, (size_t)req->content_len + 1);
    if (body == NULL) return httpd_resp_send_500(req);
    int received = 0;
    while (received < req->content_len) {
        int count = httpd_req_recv(req, body + received, req->content_len - received);
        if (count <= 0) {
            free(body);
            return ESP_FAIL;
        }
        received += count;
    }

    app_config_t config;
    esp_err_t err = app_config_load(&config);
    char ssid[LASTVAKT_SSID_MAX + 1] = {0};
    char password[LASTVAKT_WIFI_PASSWORD_MAX + 1] = {0};
    char token[LASTVAKT_TIBBER_TOKEN_MAX + 1] = {0};
    char calendar[LASTVAKT_CALENDAR_URL_MAX + 1] = {0};
    char shelly[LASTVAKT_SHELLY_HOST_MAX + 1] = {0};
    form_value(body, "ssid", ssid, sizeof(ssid));
    form_value(body, "password", password, sizeof(password));
    form_value(body, "token", token, sizeof(token));
    form_value(body, "calendar", calendar, sizeof(calendar));
    form_value(body, "shelly", shelly, sizeof(shelly));
    free(body);

    if (err != ESP_OK || ssid[0] == '\0') {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "SSID saknas");
    }
    snprintf(config.ssid, sizeof(config.ssid), "%s", ssid);
    if (password[0] != '\0') snprintf(config.wifi_password, sizeof(config.wifi_password), "%s", password);
    if (token[0] != '\0') snprintf(config.tibber_token, sizeof(config.tibber_token), "%s", token);
    if (calendar[0] != '\0') snprintf(config.calendar_ical_url, sizeof(config.calendar_ical_url), "%s", calendar);
    if (shelly[0] != '\0') snprintf(config.shelly_host, sizeof(config.shelly_host), "%s", shelly);
    config.tibber_home_id[0] = '\0';

    err = app_config_save(&config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Could not save setup: %s", esp_err_to_name(err));
        return httpd_resp_send_500(req);
    }

    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_sendstr(req, "<!doctype html><meta charset='utf-8'><h1>Sparat</h1><p>Displayen startar om och ansluter till Wi-Fi.</p>");
    xTaskCreate(restart_task, "restart", 2048, NULL, 3, NULL);
    return ESP_OK;
}

static esp_err_t shelly_post(httpd_req_t *req)
{
    if (req->content_len <= 0 || req->content_len > 256) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Ogiltig Shelly-adress");
    }

    char body[257] = {0};
    int received = 0;
    while (received < req->content_len) {
        int count = httpd_req_recv(req, body + received, req->content_len - received);
        if (count <= 0) return ESP_FAIL;
        received += count;
    }

    char host[LASTVAKT_SHELLY_HOST_MAX + 1] = {0};
    if (!form_value(body, "host", host, sizeof(host)) || host[0] == '\0') {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Shelly-adress saknas");
    }
    esp_err_t err = app_config_save_shelly_host(host);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Could not save Shelly address: %s", esp_err_to_name(err));
        return httpd_resp_send_500(req);
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"saved\":true,\"restarting\":true}");
    xTaskCreate(restart_task, "restart", 2048, NULL, 3, NULL);
    return ESP_OK;
}

static esp_err_t start_web_server(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.stack_size = 6144;
    httpd_handle_t server = NULL;
    ESP_RETURN_ON_ERROR(httpd_start(&server, &config), TAG, "Could not start setup server");
    const httpd_uri_t page = {.uri = "/", .method = HTTP_GET, .handler = page_get};
    const httpd_uri_t save = {.uri = "/save", .method = HTTP_POST, .handler = save_post};
    const httpd_uri_t shelly = {.uri = "/shelly", .method = HTTP_POST, .handler = shelly_post};
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(server, &page), TAG, "Could not register page");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(server, &save), TAG, "Could not register save handler");
    return httpd_register_uri_handler(server, &shelly);
}

esp_err_t network_setup_start(void)
{
    app_config_t config;
    ESP_RETURN_ON_ERROR(app_config_load(&config), TAG, "Could not load setup");
    ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "Could not initialize network stack");
    ESP_RETURN_ON_ERROR(esp_event_loop_create_default(), TAG, "Could not create event loop");
    esp_netif_create_default_wifi_sta();
    esp_netif_create_default_wifi_ap();

    s_events = xEventGroupCreate();
    if (s_events == NULL) return ESP_ERR_NO_MEM;

    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&init), TAG, "Could not initialize Wi-Fi");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event, NULL), TAG, "Could not register Wi-Fi event");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event, NULL), TAG, "Could not register IP event");

    if (app_config_has_wifi(&config)) {
        wifi_config_t sta = {0};
        memcpy(sta.sta.ssid, config.ssid, strnlen(config.ssid, sizeof(sta.sta.ssid)));
        memcpy(sta.sta.password, config.wifi_password, strnlen(config.wifi_password, sizeof(sta.sta.password)));
        sta.sta.threshold.authmode = config.wifi_password[0] == '\0' ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA2_PSK;
        ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "Could not set station mode");
        ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &sta), TAG, "Could not configure station");
    } else {
        ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_AP), TAG, "Could not set setup mode");
    }

    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "Could not start Wi-Fi");
    if (!app_config_has_wifi(&config)) start_setup_ap();
    return start_web_server();
}

bool network_setup_wait_connected(TickType_t timeout)
{
    if (s_events == NULL) return false;
    return (xEventGroupWaitBits(s_events, CONNECTED_BIT, pdFALSE, pdTRUE, timeout) & CONNECTED_BIT) != 0;
}

bool network_setup_is_connected(void)
{
    return s_events != NULL && (xEventGroupGetBits(s_events) & CONNECTED_BIT) != 0;
}

const char *network_setup_status_text(void)
{
    switch (s_state) {
        case NET_SETUP_AP: return "Öppna setupnätverket och 192.168.4.1";
        case NET_CONNECTING: return "Ansluter till Wi-Fi…";
        case NET_CONNECTED: return s_address;
        case NET_FAILED: return "Wi-Fi misslyckades • setup aktiv";
        case NET_STARTING:
        default: return "Startar nätverk…";
    }
}

const char *network_setup_address_text(void)
{
    return s_address;
}

const char *network_setup_url(void)
{
    return s_url;
}
