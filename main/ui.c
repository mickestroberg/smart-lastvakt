#include "ui.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "esp_timer.h"
#include "lvgl.h"
#include "sdkconfig.h"

#include "app_time.h"
#include "calendar_source.h"
#include "lastvakt_fonts.h"
#include "network_setup.h"
#include "phase_source.h"
#include "shelly_client.h"
#include "weather_source.h"

#define COLOR_BG 0x07111F
#define COLOR_PANEL 0x0D1B2B
#define COLOR_PANEL_LIGHT 0x13263A
#define COLOR_TEXT 0xF3F7FA
#define COLOR_MUTED 0x8CA0B3
#define COLOR_CYAN 0x34D6E7
#define COLOR_MINT 0x56D6A7
#define COLOR_WARNING 0xFFAA4C
#define COLOR_DANGER 0xFF5A67

typedef enum {
    LOAD_ZONE_GREEN,
    LOAD_ZONE_ORANGE,
    LOAD_ZONE_RED,
} load_zone_t;

typedef struct {
    lv_obj_t *clock_view;
    lv_obj_t *phase_view;
    lv_obj_t *calendar_view;
    lv_obj_t *status_view;
    lv_obj_t *shelly_view;
    lv_obj_t *clock_large;
    lv_obj_t *clock_small;
    lv_obj_t *calendar_clock;
    lv_obj_t *status_clock;
    lv_obj_t *shelly_clock;
    lv_obj_t *date;
    lv_obj_t *weather_temperature;
    lv_obj_t *weather_condition;
    lv_obj_t *weather_sun;
    lv_obj_t *weather_moon;
    lv_obj_t *weather_cloud;
    lv_obj_t *weather_rain;
    lv_obj_t *weather_snow;
    lv_obj_t *weather_fog;
    lv_obj_t *weather_lightning;
    lv_obj_t *calendar_event;
    lv_obj_t *source_badge;
    lv_obj_t *source_badge_label;
    lv_obj_t *phase_source_status;
    lv_obj_t *phase_alert_status;
    lv_obj_t *load_panel;
    lv_obj_t *load_status;
    lv_obj_t *load_value;
    lv_obj_t *microwave_status;
    lv_obj_t *phase_amp[3];
    lv_obj_t *phase_percent[3];
    lv_obj_t *phase_bar[3];
    lv_obj_t *phase_card[3];
    lv_obj_t *calendar_status;
    lv_obj_t *calendar_empty;
    lv_obj_t *calendar_row[CALENDAR_EVENT_LIMIT];
    lv_obj_t *calendar_when[CALENDAR_EVENT_LIMIT];
    lv_obj_t *calendar_title[CALENDAR_EVENT_LIMIT];
    lv_obj_t *status_network;
    lv_obj_t *status_time;
    lv_obj_t *status_tibber;
    lv_obj_t *status_calendar;
    lv_obj_t *status_weather;
    lv_obj_t *status_shelly;
    lv_obj_t *status_setup_url;
    lv_obj_t *status_qr;
    lv_obj_t *shelly_connection_status;
    lv_obj_t *shelly_state_large;
    lv_obj_t *shelly_power;
    lv_obj_t *shelly_details;
    lv_obj_t *shelly_control_button;
    lv_obj_t *shelly_control_label;
    lv_obj_t *shelly_control_hint;
    lv_obj_t *shelly_confirm_overlay;
    lv_obj_t *shelly_confirm_question;
    lv_obj_t *shelly_confirm_button;
    lv_obj_t *shelly_confirm_button_label;
    char qr_url[40];
    bool phase_visible;
    bool shelly_confirm_target;
    bool shelly_hold_triggered;
    int64_t shelly_hold_started_ms;
    int64_t last_over_threshold_ms;
    int64_t manual_phase_until_ms;
} ui_state_t;

static ui_state_t s_ui;

static lv_point_t s_sun_rays[][2] = {
    {{55, 5}, {55, 14}},
    {{55, 60}, {55, 69}},
    {{20, 37}, {29, 37}},
    {{81, 37}, {90, 37}},
    {{30, 12}, {36, 19}},
    {{74, 55}, {80, 62}},
    {{30, 62}, {36, 55}},
    {{74, 19}, {80, 12}},
};

static lv_point_t s_rain_drops[][2] = {
    {{35, 66}, {29, 78}},
    {{56, 66}, {50, 78}},
    {{77, 66}, {71, 78}},
};

static lv_point_t s_fog_lines[][2] = {
    {{23, 58}, {87, 58}},
    {{31, 69}, {79, 69}},
    {{23, 80}, {87, 80}},
};

static lv_point_t s_lightning_points[] = {
    {60, 62}, {50, 75}, {60, 75}, {52, 88},
};

static load_zone_t load_zone_for_current(float current_a, float main_fuse_a)
{
    const float green_limit_a = main_fuse_a * (float)CONFIG_LASTVAKT_ORANGE_PERCENT / 100.0f;
    const float orange_limit_a = main_fuse_a * (float)CONFIG_LASTVAKT_RED_PERCENT / 100.0f;

    if (current_a <= green_limit_a) {
        return LOAD_ZONE_GREEN;
    }
    if (current_a <= orange_limit_a) {
        return LOAD_ZONE_ORANGE;
    }
    return LOAD_ZONE_RED;
}

static uint32_t load_zone_color(load_zone_t zone)
{
    switch (zone) {
        case LOAD_ZONE_ORANGE:
            return COLOR_WARNING;
        case LOAD_ZONE_RED:
            return COLOR_DANGER;
        case LOAD_ZONE_GREEN:
        default:
            return COLOR_MINT;
    }
}

static void format_amps(char *buffer, size_t buffer_size, float current_a)
{
    snprintf(buffer, buffer_size, "%.1f A", current_a);
    for (char *p = buffer; *p != '\0'; ++p) {
        if (*p == '.') {
            *p = ',';
        }
    }
}

static void format_calendar_when(char *buffer, size_t buffer_size, const calendar_event_t *event)
{
    static const char *weekday[] = {"sön", "mån", "tis", "ons", "tors", "fre", "lör"};
    static const char *month[] = {
        "jan", "feb", "mar", "apr", "maj", "jun",
        "jul", "aug", "sep", "okt", "nov", "dec",
    };

    time_t now = time(NULL);
    struct tm current = {0};
    struct tm tomorrow = {0};
    struct tm start = {0};
    localtime_r(&now, &current);
    tomorrow = current;
    ++tomorrow.tm_mday;
    tomorrow.tm_isdst = -1;
    mktime(&tomorrow);
    localtime_r(&event->start, &start);

    const bool today = start.tm_year == current.tm_year && start.tm_yday == current.tm_yday;
    const bool next_day = start.tm_year == tomorrow.tm_year && start.tm_yday == tomorrow.tm_yday;
    if (today && event->all_day) {
        snprintf(buffer, buffer_size, "Idag");
    } else if (today) {
        snprintf(buffer, buffer_size, "Idag %02d:%02d", start.tm_hour, start.tm_min);
    } else if (next_day && event->all_day) {
        snprintf(buffer, buffer_size, "Imorgon");
    } else if (next_day) {
        snprintf(buffer, buffer_size, "Imorgon %02d:%02d", start.tm_hour, start.tm_min);
    } else if (event->all_day) {
        snprintf(buffer, buffer_size, "%s %d %s", weekday[start.tm_wday], start.tm_mday, month[start.tm_mon]);
    } else {
        snprintf(
            buffer,
            buffer_size,
            "%s %d %s %02d:%02d",
            weekday[start.tm_wday],
            start.tm_mday,
            month[start.tm_mon],
            start.tm_hour,
            start.tm_min);
    }
}

static void format_calendar_event(char *buffer, size_t buffer_size, const calendar_event_t *event)
{
    char when[48];
    format_calendar_when(when, sizeof(when), event);
    snprintf(buffer, buffer_size, "%s • %s", when, event->title);
}

static lv_obj_t *make_label(lv_obj_t *parent, const lv_font_t *font, uint32_t color)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_set_style_text_font(label, font, LV_PART_MAIN);
    lv_obj_set_style_text_color(label, lv_color_hex(color), LV_PART_MAIN);
    return label;
}

static lv_obj_t *make_icon_layer(lv_obj_t *parent)
{
    lv_obj_t *layer = lv_obj_create(parent);
    lv_obj_remove_style_all(layer);
    lv_obj_set_size(layer, 110, 92);
    lv_obj_set_pos(layer, 0, 0);
    lv_obj_clear_flag(layer, LV_OBJ_FLAG_SCROLLABLE);
    return layer;
}

static lv_obj_t *make_icon_disc(lv_obj_t *parent, int x, int y, int size, uint32_t color)
{
    lv_obj_t *disc = lv_obj_create(parent);
    lv_obj_remove_style_all(disc);
    lv_obj_set_size(disc, size, size);
    lv_obj_set_pos(disc, x, y);
    lv_obj_set_style_bg_color(disc, lv_color_hex(color), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(disc, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(disc, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    return disc;
}

static lv_obj_t *make_icon_line(lv_obj_t *parent, lv_point_t *points, uint32_t color, int width)
{
    lv_obj_t *line = lv_line_create(parent);
    lv_line_set_points(line, points, 2);
    lv_obj_set_style_line_color(line, lv_color_hex(color), LV_PART_MAIN);
    lv_obj_set_style_line_width(line, width, LV_PART_MAIN);
    lv_obj_set_style_line_rounded(line, true, LV_PART_MAIN);
    return line;
}

static const char *weather_description(int code)
{
    if (code == 0) return "Klart";
    if (code == 1) return "Mestadels klart";
    if (code == 2) return "Växlande molnighet";
    if (code == 3) return "Mulet";
    if (code == 45 || code == 48) return "Dimma";
    if (code >= 51 && code <= 57) return "Duggregn";
    if (code >= 61 && code <= 67) return "Regn";
    if (code >= 71 && code <= 77) return "Snö";
    if (code >= 80 && code <= 82) return "Regnskurar";
    if (code >= 85 && code <= 86) return "Snöbyar";
    if (code >= 95) return "Åska";
    return "Väder";
}

static void update_weather_icon(const weather_sample_t *weather)
{
    const int code = weather->weather_code;
    const bool fog = code == 45 || code == 48;
    const bool rain = (code >= 51 && code <= 67) || (code >= 80 && code <= 82);
    const bool snow = (code >= 71 && code <= 77) || (code >= 85 && code <= 86);
    const bool thunder = code >= 95;
    const bool cloudy = code == 2 || code == 3 || fog || rain || snow || thunder;
    const bool celestial = code <= 2;

    if (celestial && weather->is_day) lv_obj_clear_flag(s_ui.weather_sun, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(s_ui.weather_sun, LV_OBJ_FLAG_HIDDEN);
    if (celestial && !weather->is_day) lv_obj_clear_flag(s_ui.weather_moon, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(s_ui.weather_moon, LV_OBJ_FLAG_HIDDEN);
    if (cloudy && !fog) lv_obj_clear_flag(s_ui.weather_cloud, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(s_ui.weather_cloud, LV_OBJ_FLAG_HIDDEN);
    if (rain || thunder) lv_obj_clear_flag(s_ui.weather_rain, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(s_ui.weather_rain, LV_OBJ_FLAG_HIDDEN);
    if (snow) lv_obj_clear_flag(s_ui.weather_snow, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(s_ui.weather_snow, LV_OBJ_FLAG_HIDDEN);
    if (fog) lv_obj_clear_flag(s_ui.weather_fog, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(s_ui.weather_fog, LV_OBJ_FLAG_HIDDEN);
    if (thunder) lv_obj_clear_flag(s_ui.weather_lightning, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(s_ui.weather_lightning, LV_OBJ_FLAG_HIDDEN);
}

static void create_weather_icon(lv_obj_t *parent)
{
    lv_obj_t *icon = lv_obj_create(parent);
    lv_obj_remove_style_all(icon);
    lv_obj_set_size(icon, 110, 92);
    lv_obj_set_pos(icon, 640, 72);
    lv_obj_clear_flag(icon, LV_OBJ_FLAG_SCROLLABLE);

    s_ui.weather_sun = make_icon_layer(icon);
    make_icon_disc(s_ui.weather_sun, 37, 19, 36, 0xFFD166);
    for (size_t i = 0; i < sizeof(s_sun_rays) / sizeof(s_sun_rays[0]); ++i) {
        make_icon_line(s_ui.weather_sun, s_sun_rays[i], 0xFFD166, 4);
    }

    s_ui.weather_moon = make_icon_layer(icon);
    make_icon_disc(s_ui.weather_moon, 35, 14, 46, 0xFFE6A7);
    make_icon_disc(s_ui.weather_moon, 51, 7, 43, COLOR_BG);

    s_ui.weather_cloud = make_icon_layer(icon);
    make_icon_disc(s_ui.weather_cloud, 18, 40, 34, 0xD8E1E8);
    make_icon_disc(s_ui.weather_cloud, 38, 27, 46, 0xD8E1E8);
    make_icon_disc(s_ui.weather_cloud, 69, 41, 31, 0xD8E1E8);
    lv_obj_t *cloud_base = lv_obj_create(s_ui.weather_cloud);
    lv_obj_remove_style_all(cloud_base);
    lv_obj_set_size(cloud_base, 72, 29);
    lv_obj_set_pos(cloud_base, 23, 48);
    lv_obj_set_style_bg_color(cloud_base, lv_color_hex(0xD8E1E8), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(cloud_base, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(cloud_base, 14, LV_PART_MAIN);

    s_ui.weather_rain = make_icon_layer(icon);
    for (size_t i = 0; i < sizeof(s_rain_drops) / sizeof(s_rain_drops[0]); ++i) {
        make_icon_line(s_ui.weather_rain, s_rain_drops[i], COLOR_CYAN, 4);
    }

    s_ui.weather_snow = make_icon_layer(icon);
    make_icon_disc(s_ui.weather_snow, 30, 69, 7, COLOR_TEXT);
    make_icon_disc(s_ui.weather_snow, 52, 75, 7, COLOR_TEXT);
    make_icon_disc(s_ui.weather_snow, 75, 68, 7, COLOR_TEXT);

    s_ui.weather_fog = make_icon_layer(icon);
    for (size_t i = 0; i < sizeof(s_fog_lines) / sizeof(s_fog_lines[0]); ++i) {
        make_icon_line(s_ui.weather_fog, s_fog_lines[i], COLOR_MUTED, 5);
    }

    s_ui.weather_lightning = make_icon_layer(icon);
    lv_obj_t *lightning = lv_line_create(s_ui.weather_lightning);
    lv_line_set_points(
        lightning,
        s_lightning_points,
        sizeof(s_lightning_points) / sizeof(s_lightning_points[0]));
    lv_obj_set_style_line_color(lightning, lv_color_hex(0xFFD166), LV_PART_MAIN);
    lv_obj_set_style_line_width(lightning, 5, LV_PART_MAIN);
    lv_obj_set_style_line_rounded(lightning, true, LV_PART_MAIN);

    lv_obj_add_flag(s_ui.weather_sun, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_ui.weather_moon, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_ui.weather_cloud, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_ui.weather_rain, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_ui.weather_snow, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_ui.weather_fog, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_ui.weather_lightning, LV_OBJ_FLAG_HIDDEN);
}

static void style_panel(lv_obj_t *panel, uint32_t color, int radius)
{
    lv_obj_set_style_bg_color(panel, lv_color_hex(color), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(panel, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(panel, radius, LV_PART_MAIN);
    lv_obj_set_style_pad_all(panel, 0, LV_PART_MAIN);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
}

static lv_obj_t *make_button(lv_obj_t *parent, const char *text, lv_event_cb_t callback)
{
    lv_obj_t *button = lv_btn_create(parent);
    lv_obj_set_size(button, 126, 46);
    lv_obj_set_style_bg_color(button, lv_color_hex(COLOR_PANEL_LIGHT), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(button, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(button, lv_color_hex(0x274158), LV_PART_MAIN);
    lv_obj_set_style_radius(button, 23, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(button, 0, LV_PART_MAIN);
    lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED, NULL);

    lv_obj_t *label = make_label(button, &lv_font_lastvakt_16, COLOR_TEXT);
    lv_label_set_text(label, text);
    lv_obj_center(label);
    return button;
}

static void show_phase_view(bool show)
{
    s_ui.phase_visible = show;
    if (show) {
        lv_obj_add_flag(s_ui.clock_view, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_ui.calendar_view, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_ui.status_view, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_ui.shelly_view, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_ui.phase_view, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_ui.phase_view, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_ui.calendar_view, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_ui.status_view, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_ui.shelly_view, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_ui.clock_view, LV_OBJ_FLAG_HIDDEN);
    }
}

static void show_calendar_view(void)
{
    s_ui.phase_visible = false;
    s_ui.manual_phase_until_ms = 0;
    lv_obj_add_flag(s_ui.clock_view, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_ui.phase_view, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_ui.status_view, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_ui.shelly_view, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_ui.calendar_view, LV_OBJ_FLAG_HIDDEN);
}

static void show_status_view(void)
{
    s_ui.phase_visible = false;
    s_ui.manual_phase_until_ms = 0;
    lv_obj_add_flag(s_ui.clock_view, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_ui.phase_view, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_ui.calendar_view, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_ui.shelly_view, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_ui.status_view, LV_OBJ_FLAG_HIDDEN);
}

static void show_shelly_view(void)
{
    s_ui.phase_visible = false;
    s_ui.manual_phase_until_ms = 0;
    lv_obj_add_flag(s_ui.clock_view, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_ui.phase_view, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_ui.calendar_view, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_ui.status_view, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_ui.shelly_view, LV_OBJ_FLAG_HIDDEN);
}

static void view_button_event(lv_event_t *event)
{
    (void)event;
    const int64_t now_ms = esp_timer_get_time() / 1000;
    if (s_ui.phase_visible) {
        s_ui.manual_phase_until_ms = 0;
        show_phase_view(false);
    } else {
        s_ui.manual_phase_until_ms = now_ms + 15000;
        show_phase_view(true);
    }
}

static void calendar_panel_event(lv_event_t *event)
{
    (void)event;
    show_calendar_view();
}

static void calendar_back_event(lv_event_t *event)
{
    (void)event;
    show_phase_view(false);
}

static void status_panel_event(lv_event_t *event)
{
    (void)event;
    show_status_view();
}

static void status_back_event(lv_event_t *event)
{
    (void)event;
    show_phase_view(false);
}

static void shelly_panel_event(lv_event_t *event)
{
    (void)event;
    show_shelly_view();
}

static void shelly_back_event(lv_event_t *event)
{
    (void)event;
    lv_obj_add_flag(s_ui.shelly_confirm_overlay, LV_OBJ_FLAG_HIDDEN);
    show_phase_view(false);
}

static void shelly_cancel_event(lv_event_t *event)
{
    (void)event;
    lv_obj_add_flag(s_ui.shelly_confirm_overlay, LV_OBJ_FLAG_HIDDEN);
}

static void shelly_confirm_event(lv_event_t *event)
{
    (void)event;
    const esp_err_t err = shelly_client_request_output(s_ui.shelly_confirm_target);
    lv_obj_add_flag(s_ui.shelly_confirm_overlay, LV_OBJ_FLAG_HIDDEN);
    if (err != ESP_OK) {
        lv_label_set_text(s_ui.shelly_control_hint, "Kunde inte skicka kommandot • försök igen");
        lv_obj_set_style_text_color(s_ui.shelly_control_hint, lv_color_hex(COLOR_WARNING), LV_PART_MAIN);
    }
}

static void shelly_hold_event(lv_event_t *event)
{
    const lv_event_code_t code = lv_event_get_code(event);
    const int64_t now_ms = esp_timer_get_time() / 1000;
    if (code == LV_EVENT_PRESSED) {
        s_ui.shelly_hold_started_ms = now_ms;
        s_ui.shelly_hold_triggered = false;
        return;
    }
    if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        s_ui.shelly_hold_started_ms = 0;
        return;
    }
    if (code != LV_EVENT_PRESSING || s_ui.shelly_hold_triggered ||
        s_ui.shelly_hold_started_ms == 0 || now_ms - s_ui.shelly_hold_started_ms < 2000) {
        return;
    }

    shelly_sample_t sample = {0};
    if (shelly_client_get(&sample) != SHELLY_READY || !sample.available ||
        shelly_client_command_pending()) {
        return;
    }

    s_ui.shelly_hold_triggered = true;
    s_ui.shelly_confirm_target = !sample.output_on;
    lv_label_set_text(
        s_ui.shelly_confirm_question,
        s_ui.shelly_confirm_target
            ? "Slå på strömmen till Shelly-uttaget?"
            : "Bryt strömmen till Shelly-uttaget?");
    lv_label_set_text(
        s_ui.shelly_confirm_button_label,
        s_ui.shelly_confirm_target ? "Ja, slå på" : "Ja, stäng av");
    lv_obj_set_style_bg_color(
        s_ui.shelly_confirm_button,
        lv_color_hex(s_ui.shelly_confirm_target ? 0x14614B : 0x7A2630),
        LV_PART_MAIN);
    lv_obj_clear_flag(s_ui.shelly_confirm_overlay, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_ui.shelly_confirm_overlay);
}

static void create_clock_view(lv_obj_t *root)
{
    s_ui.clock_view = lv_obj_create(root);
    lv_obj_set_size(s_ui.clock_view, 800, 480);
    lv_obj_center(s_ui.clock_view);
    style_panel(s_ui.clock_view, COLOR_BG, 0);

    lv_obj_t *eyebrow = make_label(s_ui.clock_view, &lv_font_lastvakt_16, COLOR_CYAN);
    lv_label_set_text(eyebrow, "SMART LASTVAKT");
    lv_obj_align(eyebrow, LV_ALIGN_TOP_LEFT, 36, 28);
    lv_obj_add_flag(eyebrow, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(eyebrow, 14);
    lv_obj_add_event_cb(eyebrow, status_panel_event, LV_EVENT_CLICKED, NULL);

    s_ui.source_badge = lv_obj_create(s_ui.clock_view);
    lv_obj_set_size(s_ui.source_badge, 128, 34);
    lv_obj_align(s_ui.source_badge, LV_ALIGN_TOP_RIGHT, -36, 22);
    style_panel(s_ui.source_badge, 0x392814, 17);
    s_ui.source_badge_label = make_label(s_ui.source_badge, &lv_font_lastvakt_12, COLOR_WARNING);
    lv_label_set_text(s_ui.source_badge_label, "SIMULERING");
    lv_obj_center(s_ui.source_badge_label);

    s_ui.clock_large = make_label(s_ui.clock_view, &lv_font_lastvakt_clock_96, COLOR_TEXT);
    lv_label_set_text(s_ui.clock_large, "--:--");
    lv_obj_align(s_ui.clock_large, LV_ALIGN_TOP_MID, 0, 67);

    s_ui.weather_temperature = make_label(s_ui.clock_view, &lv_font_montserrat_48, COLOR_MUTED);
    lv_obj_set_width(s_ui.weather_temperature, 105);
    lv_obj_set_style_text_align(s_ui.weather_temperature, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
    lv_label_set_text(s_ui.weather_temperature, "--");
    lv_obj_set_pos(s_ui.weather_temperature, 35, 92);

    lv_obj_t *weather_unit = make_label(s_ui.clock_view, &lv_font_montserrat_20, COLOR_MUTED);
    lv_label_set_text(weather_unit, "°C");
    lv_obj_set_pos(weather_unit, 145, 99);

    lv_obj_t *weather_place = make_label(s_ui.clock_view, &lv_font_lastvakt_12, COLOR_CYAN);
    lv_obj_set_width(weather_place, 155);
    lv_obj_set_style_text_align(weather_place, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_label_set_text(weather_place, "UTE • ÄLVSJÖ");
    lv_obj_set_pos(weather_place, 29, 153);

    create_weather_icon(s_ui.clock_view);
    s_ui.weather_condition = make_label(s_ui.clock_view, &lv_font_lastvakt_12, COLOR_MUTED);
    lv_obj_set_width(s_ui.weather_condition, 170);
    lv_obj_set_style_text_align(s_ui.weather_condition, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_label_set_text(s_ui.weather_condition, "Hämtar väder…");
    lv_obj_set_pos(s_ui.weather_condition, 610, 164);

    s_ui.date = make_label(s_ui.clock_view, &lv_font_lastvakt_24, COLOR_MUTED);
    lv_label_set_text(s_ui.date, "Svensk tid");
    lv_obj_align(s_ui.date, LV_ALIGN_TOP_MID, 0, 190);

    lv_obj_t *calendar_panel = lv_obj_create(s_ui.clock_view);
    lv_obj_set_size(calendar_panel, 728, 44);
    lv_obj_set_pos(calendar_panel, 36, 260);
    style_panel(calendar_panel, COLOR_PANEL_LIGHT, 14);
    lv_obj_add_flag(calendar_panel, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(calendar_panel, lv_color_hex(COLOR_PANEL), LV_STATE_PRESSED);
    lv_obj_add_event_cb(calendar_panel, calendar_panel_event, LV_EVENT_CLICKED, NULL);

    lv_obj_t *calendar_caption = make_label(calendar_panel, &lv_font_lastvakt_12, COLOR_CYAN);
    lv_label_set_text(calendar_caption, "KALENDER");
    lv_obj_align(calendar_caption, LV_ALIGN_LEFT_MID, 16, 0);

    s_ui.calendar_event = make_label(calendar_panel, &lv_font_lastvakt_16, COLOR_MUTED);
    lv_obj_set_width(s_ui.calendar_event, 520);
    lv_label_set_long_mode(s_ui.calendar_event, LV_LABEL_LONG_DOT);
    lv_label_set_text(s_ui.calendar_event, "Lägg till iCal-adressen i Inställningar");
    lv_obj_align(s_ui.calendar_event, LV_ALIGN_LEFT_MID, 118, 0);

    lv_obj_t *calendar_hint = make_label(calendar_panel, &lv_font_lastvakt_12, COLOR_CYAN);
    lv_label_set_text(calendar_hint, "VISA");
    lv_obj_align(calendar_hint, LV_ALIGN_RIGHT_MID, -16, 0);

    s_ui.load_panel = lv_obj_create(s_ui.clock_view);
    lv_obj_set_size(s_ui.load_panel, 728, 122);
    lv_obj_align(s_ui.load_panel, LV_ALIGN_BOTTOM_MID, 0, -30);
    style_panel(s_ui.load_panel, COLOR_PANEL, 22);

    lv_obj_t *load_caption = make_label(s_ui.load_panel, &lv_font_lastvakt_12, COLOR_MUTED);
    lv_label_set_text(load_caption, "HÖGSTA FAS JUST NU");
    lv_obj_align(load_caption, LV_ALIGN_TOP_LEFT, 24, 19);

    s_ui.load_status = make_label(s_ui.load_panel, &lv_font_lastvakt_20, COLOR_MINT);
    lv_label_set_text(s_ui.load_status, "Gott om marginal");
    lv_obj_align(s_ui.load_status, LV_ALIGN_TOP_LEFT, 24, 48);

    s_ui.microwave_status = make_label(s_ui.load_panel, &lv_font_lastvakt_12, COLOR_MUTED);
    lv_label_set_text(s_ui.microwave_status, "MIKRO • Shelly är inte konfigurerad");
    lv_obj_align(s_ui.microwave_status, LV_ALIGN_TOP_LEFT, 24, 88);
    lv_obj_add_flag(s_ui.microwave_status, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(s_ui.microwave_status, 14);
    lv_obj_add_event_cb(s_ui.microwave_status, shelly_panel_event, LV_EVENT_CLICKED, NULL);

    s_ui.load_value = make_label(s_ui.load_panel, &lv_font_lastvakt_24, COLOR_MINT);
    lv_label_set_text(s_ui.load_value, "L1  0,0 A");
    lv_obj_align(s_ui.load_value, LV_ALIGN_RIGHT_MID, -170, 4);

    lv_obj_t *button = make_button(s_ui.load_panel, "Visa faser", view_button_event);
    lv_obj_align(button, LV_ALIGN_RIGHT_MID, -22, 0);
}

static void create_phase_row(lv_obj_t *parent, int index, int y, uint32_t color)
{
    static const char *names[] = {"L1", "L2", "L3"};

    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_size(card, 728, 92);
    lv_obj_set_pos(card, 36, y);
    style_panel(card, COLOR_PANEL, 18);
    s_ui.phase_card[index] = card;

    lv_obj_t *name = make_label(card, &lv_font_lastvakt_20, color);
    lv_label_set_text(name, names[index]);
    lv_obj_align(name, LV_ALIGN_LEFT_MID, 22, -10);

    s_ui.phase_amp[index] = make_label(card, &lv_font_lastvakt_20, COLOR_TEXT);
    lv_label_set_text(s_ui.phase_amp[index], "0,0 A");
    lv_obj_align(s_ui.phase_amp[index], LV_ALIGN_LEFT_MID, 82, -10);

    s_ui.phase_percent[index] = make_label(card, &lv_font_lastvakt_16, COLOR_MUTED);
    lv_label_set_text(s_ui.phase_percent[index], "0 %");
    lv_obj_align(s_ui.phase_percent[index], LV_ALIGN_RIGHT_MID, -22, -10);

    s_ui.phase_bar[index] = lv_bar_create(card);
    lv_obj_set_size(s_ui.phase_bar[index], 624, 12);
    lv_obj_align(s_ui.phase_bar[index], LV_ALIGN_BOTTOM_LEFT, 82, -18);
    lv_bar_set_range(s_ui.phase_bar[index], 0, 100);
    lv_bar_set_value(s_ui.phase_bar[index], 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(s_ui.phase_bar[index], lv_color_hex(0x20354A), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_ui.phase_bar[index], LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(s_ui.phase_bar[index], 6, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_ui.phase_bar[index], lv_color_hex(color), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(s_ui.phase_bar[index], LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(s_ui.phase_bar[index], 6, LV_PART_INDICATOR);
}

static void create_phase_view(lv_obj_t *root)
{
    s_ui.phase_view = lv_obj_create(root);
    lv_obj_set_size(s_ui.phase_view, 800, 480);
    lv_obj_center(s_ui.phase_view);
    style_panel(s_ui.phase_view, COLOR_BG, 0);

    lv_obj_t *title = make_label(s_ui.phase_view, &lv_font_lastvakt_20, COLOR_TEXT);
    lv_label_set_text(title, "Fasbelastning");
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 36, 23);

    s_ui.phase_source_status = make_label(s_ui.phase_view, &lv_font_lastvakt_12, COLOR_WARNING);
    lv_label_set_text(s_ui.phase_source_status, "TIBBER INTE KONFIGURERAD • SIMULERAR");
    lv_obj_align(s_ui.phase_source_status, LV_ALIGN_TOP_LEFT, 36, 54);

    s_ui.clock_small = make_label(s_ui.phase_view, &lv_font_lastvakt_24, COLOR_TEXT);
    lv_label_set_text(s_ui.clock_small, "--:--");
    lv_obj_align(s_ui.clock_small, LV_ALIGN_TOP_RIGHT, -186, 25);

    s_ui.phase_alert_status = make_label(s_ui.phase_view, &lv_font_lastvakt_16, COLOR_MINT);
    lv_label_set_text(s_ui.phase_alert_status, "Gott om marginal");
    lv_obj_align(s_ui.phase_alert_status, LV_ALIGN_TOP_RIGHT, -186, 57);

    lv_obj_t *button = make_button(s_ui.phase_view, "Klocka", view_button_event);
    lv_obj_align(button, LV_ALIGN_TOP_RIGHT, -36, 17);

    create_phase_row(s_ui.phase_view, 0, 94, COLOR_MINT);
    create_phase_row(s_ui.phase_view, 1, 200, COLOR_MINT);
    create_phase_row(s_ui.phase_view, 2, 306, COLOR_MINT);

    lv_obj_t *hint = make_label(s_ui.phase_view, &lv_font_lastvakt_12, COLOR_MUTED);
    char green_limit[16];
    char red_limit[16];
    char hint_text[128];
    snprintf(
        green_limit,
        sizeof(green_limit),
        "%.0f",
        CONFIG_LASTVAKT_MAIN_FUSE_A * (float)CONFIG_LASTVAKT_ORANGE_PERCENT / 100.0f);
    snprintf(
        red_limit,
        sizeof(red_limit),
        "%.1f",
        CONFIG_LASTVAKT_MAIN_FUSE_A * (float)CONFIG_LASTVAKT_RED_PERCENT / 100.0f);
    for (char *p = green_limit; *p != '\0'; ++p) {
        if (*p == '.') {
            *p = ',';
        }
    }
    for (char *p = red_limit; *p != '\0'; ++p) {
        if (*p == '.') {
            *p = ',';
        }
    }
    snprintf(
        hint_text,
        sizeof(hint_text),
        "Grönt till %s A • orange till %s A • rött över %s A",
        green_limit,
        red_limit,
        red_limit);
    lv_label_set_text(hint, hint_text);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -22);
}

static void create_calendar_view(lv_obj_t *root)
{
    s_ui.calendar_view = lv_obj_create(root);
    lv_obj_set_size(s_ui.calendar_view, 800, 480);
    lv_obj_center(s_ui.calendar_view);
    style_panel(s_ui.calendar_view, COLOR_BG, 0);

    lv_obj_t *title = make_label(s_ui.calendar_view, &lv_font_lastvakt_24, COLOR_TEXT);
    lv_label_set_text(title, "Familjekalender");
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 36, 18);

    s_ui.calendar_status = make_label(s_ui.calendar_view, &lv_font_lastvakt_12, COLOR_MUTED);
    lv_label_set_text(s_ui.calendar_status, "Nästa händelser");
    lv_obj_align(s_ui.calendar_status, LV_ALIGN_TOP_LEFT, 36, 52);

    s_ui.calendar_clock = make_label(s_ui.calendar_view, &lv_font_lastvakt_24, COLOR_TEXT);
    lv_label_set_text(s_ui.calendar_clock, "--:--");
    lv_obj_align(s_ui.calendar_clock, LV_ALIGN_TOP_RIGHT, -178, 24);

    lv_obj_t *button = make_button(s_ui.calendar_view, "Tillbaka", calendar_back_event);
    lv_obj_align(button, LV_ALIGN_TOP_RIGHT, -36, 14);

    for (size_t i = 0; i < CALENDAR_EVENT_LIMIT; ++i) {
        lv_obj_t *row = lv_obj_create(s_ui.calendar_view);
        lv_obj_set_size(row, 728, 54);
        lv_obj_set_pos(row, 36, 84 + (int)i * 61);
        style_panel(row, i == 0 ? COLOR_PANEL_LIGHT : COLOR_PANEL, 14);
        s_ui.calendar_row[i] = row;

        s_ui.calendar_when[i] = make_label(row, &lv_font_lastvakt_12, i == 0 ? COLOR_CYAN : COLOR_MUTED);
        lv_obj_set_width(s_ui.calendar_when[i], 174);
        lv_label_set_long_mode(s_ui.calendar_when[i], LV_LABEL_LONG_DOT);
        lv_obj_align(s_ui.calendar_when[i], LV_ALIGN_LEFT_MID, 18, 0);

        s_ui.calendar_title[i] = make_label(row, &lv_font_lastvakt_16, COLOR_TEXT);
        lv_obj_set_width(s_ui.calendar_title[i], 500);
        lv_label_set_long_mode(s_ui.calendar_title[i], LV_LABEL_LONG_DOT);
        lv_obj_align(s_ui.calendar_title[i], LV_ALIGN_LEFT_MID, 202, 0);
    }

    s_ui.calendar_empty = make_label(s_ui.calendar_view, &lv_font_lastvakt_20, COLOR_MUTED);
    lv_label_set_text(s_ui.calendar_empty, "Hämtar familjekalendern…");
    lv_obj_align(s_ui.calendar_empty, LV_ALIGN_CENTER, 0, 20);
}

static lv_obj_t *create_status_row(lv_obj_t *parent, int y, const char *caption)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_set_size(row, 480, 64);
    lv_obj_set_pos(row, 36, y);
    style_panel(row, COLOR_PANEL, 14);

    lv_obj_t *label = make_label(row, &lv_font_lastvakt_12, COLOR_CYAN);
    lv_label_set_text(label, caption);
    lv_obj_align(label, LV_ALIGN_TOP_LEFT, 18, 10);

    lv_obj_t *value = make_label(row, &lv_font_lastvakt_16, COLOR_TEXT);
    lv_obj_set_width(value, 444);
    lv_label_set_long_mode(value, LV_LABEL_LONG_DOT);
    lv_label_set_text(value, "Startar…");
    lv_obj_align(value, LV_ALIGN_BOTTOM_LEFT, 18, -9);
    return value;
}

static void create_status_view(lv_obj_t *root)
{
    s_ui.status_view = lv_obj_create(root);
    lv_obj_set_size(s_ui.status_view, 800, 480);
    lv_obj_center(s_ui.status_view);
    style_panel(s_ui.status_view, COLOR_BG, 0);

    lv_obj_t *title = make_label(s_ui.status_view, &lv_font_lastvakt_24, COLOR_TEXT);
    lv_label_set_text(title, "Systemstatus");
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 36, 18);

    lv_obj_t *subtitle = make_label(s_ui.status_view, &lv_font_lastvakt_12, COLOR_MUTED);
    lv_label_set_text(subtitle, "Anslutningar och tidskälla");
    lv_obj_align(subtitle, LV_ALIGN_TOP_LEFT, 36, 52);

    s_ui.status_clock = make_label(s_ui.status_view, &lv_font_lastvakt_24, COLOR_TEXT);
    lv_label_set_text(s_ui.status_clock, "--:--");
    lv_obj_align(s_ui.status_clock, LV_ALIGN_TOP_RIGHT, -178, 24);

    lv_obj_t *button = make_button(s_ui.status_view, "Tillbaka", status_back_event);
    lv_obj_align(button, LV_ALIGN_TOP_RIGHT, -36, 14);

    s_ui.status_network = create_status_row(s_ui.status_view, 88, "NÄTVERK OCH INSTÄLLNINGAR");
    s_ui.status_time = create_status_row(s_ui.status_view, 160, "KLOCKANS TIDSKÄLLA");
    s_ui.status_tibber = create_status_row(s_ui.status_view, 232, "TIBBER PULSE");
    s_ui.status_calendar = create_status_row(s_ui.status_view, 304, "FAMILJEKALENDER");

    s_ui.status_weather = make_label(s_ui.status_view, &lv_font_lastvakt_12, COLOR_MUTED);
    lv_obj_set_width(s_ui.status_weather, 480);
    lv_label_set_text(s_ui.status_weather, "Väder: hämtar Älvsjö • Open-Meteo");
    lv_obj_set_pos(s_ui.status_weather, 54, 390);

    s_ui.status_shelly = make_label(s_ui.status_view, &lv_font_lastvakt_12, COLOR_MUTED);
    lv_obj_set_width(s_ui.status_shelly, 480);
    lv_label_set_text(s_ui.status_shelly, "Shelly: inte konfigurerad");
    lv_obj_set_pos(s_ui.status_shelly, 54, 414);

    lv_obj_t *qr_caption = make_label(s_ui.status_view, &lv_font_lastvakt_12, COLOR_CYAN);
    lv_label_set_text(qr_caption, "ÖPPNA INSTÄLLNINGAR");
    lv_obj_align(qr_caption, LV_ALIGN_TOP_RIGHT, -50, 91);

    lv_obj_t *qr_panel = lv_obj_create(s_ui.status_view);
    lv_obj_set_size(qr_panel, 214, 214);
    lv_obj_set_pos(qr_panel, 550, 116);
    style_panel(qr_panel, 0xFFFFFF, 12);

    s_ui.status_qr = lv_qrcode_create(qr_panel, 190, lv_color_hex(0x07111F), lv_color_hex(0xFFFFFF));
    lv_obj_center(s_ui.status_qr);
    const char *setup_url = network_setup_url();
    lv_qrcode_update(s_ui.status_qr, setup_url, strlen(setup_url));
    snprintf(s_ui.qr_url, sizeof(s_ui.qr_url), "%s", setup_url);

    s_ui.status_setup_url = make_label(s_ui.status_view, &lv_font_lastvakt_12, COLOR_TEXT);
    lv_obj_set_width(s_ui.status_setup_url, 224);
    lv_label_set_long_mode(s_ui.status_setup_url, LV_LABEL_LONG_DOT);
    lv_label_set_text(s_ui.status_setup_url, setup_url);
    lv_obj_set_style_text_align(s_ui.status_setup_url, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_pos(s_ui.status_setup_url, 545, 343);

    lv_obj_t *qr_hint = make_label(s_ui.status_view, &lv_font_lastvakt_12, COLOR_MUTED);
    lv_obj_set_width(qr_hint, 224);
    lv_label_set_text(qr_hint, "Mobilen måste vara på samma nätverk");
    lv_obj_set_style_text_align(qr_hint, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_pos(qr_hint, 545, 372);
}

static void create_shelly_view(lv_obj_t *root)
{
    s_ui.shelly_view = lv_obj_create(root);
    lv_obj_set_size(s_ui.shelly_view, 800, 480);
    lv_obj_center(s_ui.shelly_view);
    style_panel(s_ui.shelly_view, COLOR_BG, 0);

    lv_obj_t *title = make_label(s_ui.shelly_view, &lv_font_lastvakt_24, COLOR_TEXT);
    lv_label_set_text(title, "Mikro och Shelly");
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 36, 18);

    s_ui.shelly_connection_status = make_label(s_ui.shelly_view, &lv_font_lastvakt_12, COLOR_MUTED);
    lv_label_set_text(s_ui.shelly_connection_status, "Lokal mätning och skyddad relästyrning");
    lv_obj_align(s_ui.shelly_connection_status, LV_ALIGN_TOP_LEFT, 36, 52);

    s_ui.shelly_clock = make_label(s_ui.shelly_view, &lv_font_lastvakt_24, COLOR_TEXT);
    lv_label_set_text(s_ui.shelly_clock, "--:--");
    lv_obj_align(s_ui.shelly_clock, LV_ALIGN_TOP_RIGHT, -178, 24);

    lv_obj_t *back = make_button(s_ui.shelly_view, "Tillbaka", shelly_back_event);
    lv_obj_align(back, LV_ALIGN_TOP_RIGHT, -36, 14);

    lv_obj_t *state_panel = lv_obj_create(s_ui.shelly_view);
    lv_obj_set_size(state_panel, 458, 270);
    lv_obj_set_pos(state_panel, 36, 92);
    style_panel(state_panel, COLOR_PANEL, 22);

    lv_obj_t *state_caption = make_label(state_panel, &lv_font_lastvakt_12, COLOR_CYAN);
    lv_label_set_text(state_caption, "UTTAGETS LÄGE");
    lv_obj_set_pos(state_caption, 24, 20);

    s_ui.shelly_state_large = make_label(state_panel, &lv_font_montserrat_48, COLOR_MINT);
    lv_label_set_text(s_ui.shelly_state_large, "ANSLUTER");
    lv_obj_set_pos(s_ui.shelly_state_large, 24, 55);

    s_ui.shelly_power = make_label(state_panel, &lv_font_lastvakt_24, COLOR_TEXT);
    lv_label_set_text(s_ui.shelly_power, "-- W");
    lv_obj_set_pos(s_ui.shelly_power, 24, 135);

    s_ui.shelly_details = make_label(state_panel, &lv_font_lastvakt_16, COLOR_MUTED);
    lv_obj_set_width(s_ui.shelly_details, 410);
    lv_label_set_text(s_ui.shelly_details, "Ström -- A  •  Spänning -- V\nShelly-temperatur -- C");
    lv_obj_set_style_text_line_space(s_ui.shelly_details, 10, LV_PART_MAIN);
    lv_obj_set_pos(s_ui.shelly_details, 24, 184);

    lv_obj_t *control_panel = lv_obj_create(s_ui.shelly_view);
    lv_obj_set_size(control_panel, 238, 270);
    lv_obj_set_pos(control_panel, 526, 92);
    style_panel(control_panel, COLOR_PANEL, 22);

    lv_obj_t *control_caption = make_label(control_panel, &lv_font_lastvakt_12, COLOR_CYAN);
    lv_label_set_text(control_caption, "RELÄSTYRNING");
    lv_obj_align(control_caption, LV_ALIGN_TOP_MID, 0, 20);

    s_ui.shelly_control_button = lv_btn_create(control_panel);
    lv_obj_set_size(s_ui.shelly_control_button, 190, 112);
    lv_obj_align(s_ui.shelly_control_button, LV_ALIGN_TOP_MID, 0, 52);
    lv_obj_set_style_bg_color(s_ui.shelly_control_button, lv_color_hex(COLOR_PANEL_LIGHT), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_ui.shelly_control_button, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_ui.shelly_control_button, 2, LV_PART_MAIN);
    lv_obj_set_style_border_color(s_ui.shelly_control_button, lv_color_hex(COLOR_CYAN), LV_PART_MAIN);
    lv_obj_set_style_radius(s_ui.shelly_control_button, 20, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(s_ui.shelly_control_button, 0, LV_PART_MAIN);
    lv_obj_add_event_cb(s_ui.shelly_control_button, shelly_hold_event, LV_EVENT_ALL, NULL);

    s_ui.shelly_control_label = make_label(s_ui.shelly_control_button, &lv_font_lastvakt_16, COLOR_TEXT);
    lv_obj_set_width(s_ui.shelly_control_label, 170);
    lv_obj_set_style_text_align(s_ui.shelly_control_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_label_set_text(s_ui.shelly_control_label, "HÅLL I 2 SEK\nSTÄNG AV");
    lv_obj_center(s_ui.shelly_control_label);

    s_ui.shelly_control_hint = make_label(control_panel, &lv_font_lastvakt_12, COLOR_MUTED);
    lv_obj_set_width(s_ui.shelly_control_hint, 198);
    lv_obj_set_style_text_align(s_ui.shelly_control_hint, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_label_set_text(s_ui.shelly_control_hint, "Efter långtryck får du bekräfta");
    lv_obj_align(s_ui.shelly_control_hint, LV_ALIGN_BOTTOM_MID, 0, -30);

    lv_obj_t *safety_panel = lv_obj_create(s_ui.shelly_view);
    lv_obj_set_size(safety_panel, 728, 76);
    lv_obj_set_pos(safety_panel, 36, 380);
    style_panel(safety_panel, COLOR_PANEL_LIGHT, 18);

    lv_obj_t *safety_title = make_label(safety_panel, &lv_font_lastvakt_12, COLOR_WARNING);
    lv_label_set_text(safety_title, "SÄKER STYRNING");
    lv_obj_set_pos(safety_title, 20, 13);

    lv_obj_t *safety_text = make_label(safety_panel, &lv_font_lastvakt_16, COLOR_TEXT);
    lv_obj_set_width(safety_text, 688);
    lv_label_set_long_mode(safety_text, LV_LABEL_LONG_DOT);
    lv_label_set_text(safety_text, "Kontrollera alltid apparaten innan strömmen slås på.");
    lv_obj_set_pos(safety_text, 20, 39);

    s_ui.shelly_confirm_overlay = lv_obj_create(s_ui.shelly_view);
    lv_obj_set_size(s_ui.shelly_confirm_overlay, 800, 480);
    lv_obj_set_pos(s_ui.shelly_confirm_overlay, 0, 0);
    lv_obj_set_style_bg_color(s_ui.shelly_confirm_overlay, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_ui.shelly_confirm_overlay, LV_OPA_70, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_ui.shelly_confirm_overlay, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(s_ui.shelly_confirm_overlay, 0, LV_PART_MAIN);
    lv_obj_clear_flag(s_ui.shelly_confirm_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_ui.shelly_confirm_overlay, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *dialog = lv_obj_create(s_ui.shelly_confirm_overlay);
    lv_obj_set_size(dialog, 500, 248);
    lv_obj_center(dialog);
    style_panel(dialog, COLOR_PANEL, 24);
    lv_obj_set_style_border_width(dialog, 2, LV_PART_MAIN);
    lv_obj_set_style_border_color(dialog, lv_color_hex(COLOR_CYAN), LV_PART_MAIN);

    lv_obj_t *dialog_title = make_label(dialog, &lv_font_lastvakt_20, COLOR_TEXT);
    lv_label_set_text(dialog_title, "Bekräfta reläändring");
    lv_obj_align(dialog_title, LV_ALIGN_TOP_MID, 0, 26);

    s_ui.shelly_confirm_question = make_label(dialog, &lv_font_lastvakt_16, COLOR_MUTED);
    lv_obj_set_width(s_ui.shelly_confirm_question, 440);
    lv_obj_set_style_text_align(s_ui.shelly_confirm_question, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_label_set_text(s_ui.shelly_confirm_question, "Ändra Shelly-uttagets läge?");
    lv_obj_align(s_ui.shelly_confirm_question, LV_ALIGN_TOP_MID, 0, 80);

    lv_obj_t *cancel = make_button(dialog, "Avbryt", shelly_cancel_event);
    lv_obj_set_size(cancel, 170, 50);
    lv_obj_align(cancel, LV_ALIGN_BOTTOM_LEFT, 36, -30);

    s_ui.shelly_confirm_button = make_button(dialog, "Bekräfta", shelly_confirm_event);
    lv_obj_set_size(s_ui.shelly_confirm_button, 210, 50);
    lv_obj_align(s_ui.shelly_confirm_button, LV_ALIGN_BOTTOM_RIGHT, -36, -30);
    s_ui.shelly_confirm_button_label = lv_obj_get_child(s_ui.shelly_confirm_button, 0);

    lv_obj_add_flag(s_ui.shelly_confirm_overlay, LV_OBJ_FLAG_HIDDEN);
}

static int highest_phase(const phase_sample_t *sample)
{
    int highest = 0;
    for (int i = 1; i < 3; ++i) {
        if (sample->percent[i] > sample->percent[highest]) {
            highest = i;
        }
    }
    return highest;
}

static const char *shelly_mode_label(const shelly_sample_t *sample)
{
    if (!sample->output_on) return "STRÖMLÖS";
    if (sample->power_w >= 20.0f) return "KÖR";
    return "REDO";
}

static uint32_t shelly_mode_color(const shelly_sample_t *sample)
{
    if (!sample->output_on) return COLOR_MUTED;
    if (sample->power_w >= 20.0f) return COLOR_CYAN;
    return COLOR_MINT;
}

static void ui_timer(lv_timer_t *timer)
{
    (void)timer;
    char clock_text[16];
    char date_text[64];
    app_time_format(clock_text, sizeof(clock_text), date_text, sizeof(date_text));
    lv_label_set_text(s_ui.clock_large, clock_text);
    lv_label_set_text(s_ui.clock_small, clock_text);
    lv_label_set_text(s_ui.calendar_clock, clock_text);
    lv_label_set_text(s_ui.status_clock, clock_text);
    lv_label_set_text(s_ui.shelly_clock, clock_text);
    lv_label_set_text(s_ui.date, date_text);

    weather_sample_t weather = {0};
    const weather_source_state_t weather_state = weather_source_get(&weather);
    if (weather.available) {
        char temperature[12];
        const int rounded_temperature =
            (int)(weather.temperature_c + (weather.temperature_c >= 0.0f ? 0.5f : -0.5f));
        snprintf(temperature, sizeof(temperature), "%d", rounded_temperature);
        lv_label_set_text(s_ui.weather_temperature, temperature);
        lv_label_set_text(s_ui.weather_condition, weather_description(weather.weather_code));
        update_weather_icon(&weather);

        char exact_temperature[16];
        snprintf(exact_temperature, sizeof(exact_temperature), "%.1f", weather.temperature_c);
        for (char *p = exact_temperature; *p != '\0'; ++p) {
            if (*p == '.') *p = ',';
        }
        char weather_status[96];
        snprintf(
            weather_status,
            sizeof(weather_status),
            "Väder: %s C i Älvsjö%s • Open-Meteo",
            exact_temperature,
            weather_state == WEATHER_SOURCE_STALE ? " • senaste värdet" : "");
        lv_label_set_text(s_ui.status_weather, weather_status);
        const uint32_t weather_color =
            weather_state == WEATHER_SOURCE_READY ? COLOR_TEXT : COLOR_WARNING;
        lv_obj_set_style_text_color(s_ui.weather_temperature, lv_color_hex(weather_color), LV_PART_MAIN);
        lv_obj_set_style_text_color(s_ui.weather_condition, lv_color_hex(weather_color), LV_PART_MAIN);
        lv_obj_set_style_text_color(s_ui.status_weather, lv_color_hex(weather_color), LV_PART_MAIN);
    } else {
        lv_label_set_text(s_ui.weather_temperature, "--");
        lv_label_set_text(
            s_ui.weather_condition,
            weather_state == WEATHER_SOURCE_SYNCING ? "Hämtar väder…" : "Vädret är offline");
        lv_label_set_text(
            s_ui.status_weather,
            weather_state == WEATHER_SOURCE_SYNCING
                ? "Väder: hämtar Älvsjö • Open-Meteo"
                : "Väder: kunde inte hämtas • försöker igen");
        lv_obj_set_style_text_color(s_ui.weather_temperature, lv_color_hex(COLOR_MUTED), LV_PART_MAIN);
        lv_obj_set_style_text_color(s_ui.weather_condition, lv_color_hex(COLOR_MUTED), LV_PART_MAIN);
        lv_obj_set_style_text_color(s_ui.status_weather, lv_color_hex(COLOR_WARNING), LV_PART_MAIN);
    }

    shelly_sample_t shelly = {0};
    const shelly_state_t shelly_state = shelly_client_get(&shelly);
    const bool shelly_command_pending = shelly_client_command_pending();
    if (shelly.available) {
        char microwave_text[96];
        snprintf(
            microwave_text,
            sizeof(microwave_text),
            "MIKRO • %s • %.0f W",
            shelly_command_pending ? "VÄXLAR" : shelly_mode_label(&shelly),
            shelly.power_w);
        lv_label_set_text(s_ui.microwave_status, microwave_text);

        char shelly_status[128];
        snprintf(
            shelly_status,
            sizeof(shelly_status),
            "Shelly: %s • %.0f W • %.0f V • %.0f C%s",
            shelly.output_on ? "på" : "av",
            shelly.power_w,
            shelly.voltage_v,
            shelly.temperature_c,
            shelly_state == SHELLY_STALE ? " • senaste värdet" : "");
        lv_label_set_text(s_ui.status_shelly, shelly_status);
        const uint32_t shelly_color = shelly_state == SHELLY_READY
                                          ? (shelly_command_pending ? COLOR_WARNING : shelly_mode_color(&shelly))
                                          : COLOR_WARNING;
        lv_obj_set_style_text_color(s_ui.microwave_status, lv_color_hex(shelly_color), LV_PART_MAIN);
        lv_obj_set_style_text_color(s_ui.status_shelly, lv_color_hex(shelly_color), LV_PART_MAIN);

        lv_label_set_text(
            s_ui.shelly_connection_status,
            shelly_state == SHELLY_READY ? "Shelly ansluten • live" : "Shelly ansluten • senaste värdet");
        lv_obj_set_style_text_color(
            s_ui.shelly_connection_status,
            lv_color_hex(shelly_state == SHELLY_READY ? COLOR_MINT : COLOR_WARNING),
            LV_PART_MAIN);
        lv_label_set_text(
            s_ui.shelly_state_large,
            shelly_command_pending ? "VÄXLAR…" : shelly_mode_label(&shelly));
        lv_obj_set_style_text_color(s_ui.shelly_state_large, lv_color_hex(shelly_color), LV_PART_MAIN);

        char power_text[32];
        snprintf(power_text, sizeof(power_text), "%.0f W", shelly.power_w);
        lv_label_set_text(s_ui.shelly_power, power_text);

        char details_text[128];
        snprintf(
            details_text,
            sizeof(details_text),
            "Ström %.2f A  •  Spänning %.0f V\nShelly-temperatur %.1f C",
            shelly.current_a,
            shelly.voltage_v,
            shelly.temperature_c);
        for (char *p = details_text; *p != '\0'; ++p) {
            if (*p == '.') *p = ',';
        }
        lv_label_set_text(s_ui.shelly_details, details_text);

        if (shelly_state == SHELLY_READY && !shelly_command_pending) {
            lv_obj_clear_state(s_ui.shelly_control_button, LV_STATE_DISABLED);
            lv_label_set_text(
                s_ui.shelly_control_label,
                shelly.output_on ? "HÅLL I 2 SEK\nSTÄNG AV" : "HÅLL I 2 SEK\nSLÅ PÅ");
            lv_label_set_text(s_ui.shelly_control_hint, "Efter långtryck får du bekräfta");
            lv_obj_set_style_text_color(s_ui.shelly_control_hint, lv_color_hex(COLOR_MUTED), LV_PART_MAIN);
            lv_obj_set_style_border_color(
                s_ui.shelly_control_button,
                lv_color_hex(shelly.output_on ? COLOR_DANGER : COLOR_MINT),
                LV_PART_MAIN);
        } else {
            lv_obj_add_state(s_ui.shelly_control_button, LV_STATE_DISABLED);
            lv_label_set_text(
                s_ui.shelly_control_label,
                shelly_command_pending ? "VÄXLAR…" : "INTE TILLGÄNGLIG");
            lv_label_set_text(
                s_ui.shelly_control_hint,
                shelly_command_pending ? "Väntar på bekräftelse från Shellyn" : "Anslutningen måste vara live");
            lv_obj_set_style_text_color(s_ui.shelly_control_hint, lv_color_hex(COLOR_WARNING), LV_PART_MAIN);
        }
    } else {
        const char *microwave_text = "MIKRO • Shelly är inte konfigurerad";
        if (shelly_state == SHELLY_CONNECTING) microwave_text = "MIKRO • ansluter till Shelly…";
        else if (shelly_state == SHELLY_STALE) microwave_text = "MIKRO • Shelly kan inte nås";
        lv_label_set_text(s_ui.microwave_status, microwave_text);
        lv_label_set_text(s_ui.status_shelly, shelly_client_status_text());
        lv_obj_set_style_text_color(
            s_ui.microwave_status,
            lv_color_hex(shelly_state == SHELLY_STALE ? COLOR_WARNING : COLOR_MUTED),
            LV_PART_MAIN);
        lv_obj_set_style_text_color(
            s_ui.status_shelly,
            lv_color_hex(shelly_state == SHELLY_STALE ? COLOR_WARNING : COLOR_MUTED),
            LV_PART_MAIN);

        const char *shelly_page_status = "Shelly är inte konfigurerad";
        if (shelly_state == SHELLY_CONNECTING) shelly_page_status = "Ansluter till Shelly…";
        else if (shelly_state == SHELLY_STALE) shelly_page_status = "Shelly kan inte nås";
        lv_label_set_text(s_ui.shelly_connection_status, shelly_page_status);
        lv_obj_set_style_text_color(
            s_ui.shelly_connection_status,
            lv_color_hex(shelly_state == SHELLY_STALE ? COLOR_WARNING : COLOR_MUTED),
            LV_PART_MAIN);
        lv_label_set_text(s_ui.shelly_state_large, shelly_state == SHELLY_CONNECTING ? "ANSLUTER" : "OFFLINE");
        lv_obj_set_style_text_color(s_ui.shelly_state_large, lv_color_hex(COLOR_MUTED), LV_PART_MAIN);
        lv_label_set_text(s_ui.shelly_power, "-- W");
        lv_label_set_text(s_ui.shelly_details, "Ström -- A  •  Spänning -- V\nShelly-temperatur -- C");
        lv_obj_add_state(s_ui.shelly_control_button, LV_STATE_DISABLED);
        lv_label_set_text(s_ui.shelly_control_label, "INTE TILLGÄNGLIG");
        lv_label_set_text(s_ui.shelly_control_hint, "Anslutningen måste vara live");
        lv_obj_set_style_text_color(s_ui.shelly_control_hint, lv_color_hex(COLOR_WARNING), LV_PART_MAIN);
    }

    lv_label_set_text(s_ui.status_network, network_setup_status_text());
    lv_obj_set_style_text_color(
        s_ui.status_network,
        lv_color_hex(network_setup_is_connected() ? COLOR_MINT : COLOR_WARNING),
        LV_PART_MAIN);
    lv_label_set_text(s_ui.status_time, app_time_status_text());

    const char *setup_url = network_setup_url();
    if (strcmp(setup_url, s_ui.qr_url) != 0) {
        if (lv_qrcode_update(s_ui.status_qr, setup_url, strlen(setup_url)) == LV_RES_OK) {
            snprintf(s_ui.qr_url, sizeof(s_ui.qr_url), "%s", setup_url);
        }
        lv_label_set_text(s_ui.status_setup_url, setup_url);
    }

    calendar_event_t calendar_events[CALENDAR_EVENT_LIMIT] = {0};
    size_t calendar_event_count = 0;
    const calendar_source_state_t calendar_state = calendar_source_get_events(
        calendar_events,
        CALENDAR_EVENT_LIMIT,
        &calendar_event_count);
    if (calendar_state == CALENDAR_SOURCE_READY && calendar_event_count > 0) {
        char event_text[160];
        format_calendar_event(event_text, sizeof(event_text), &calendar_events[0]);
        lv_label_set_text(s_ui.calendar_event, event_text);
        lv_obj_set_style_text_color(s_ui.calendar_event, lv_color_hex(COLOR_TEXT), LV_PART_MAIN);
    } else if (calendar_state == CALENDAR_SOURCE_READY) {
        lv_label_set_text(s_ui.calendar_event, "Inga kommande händelser");
        lv_obj_set_style_text_color(s_ui.calendar_event, lv_color_hex(COLOR_MUTED), LV_PART_MAIN);
    } else if (calendar_state == CALENDAR_SOURCE_SYNCING) {
        lv_label_set_text(s_ui.calendar_event, "Hämtar familjekalendern…");
        lv_obj_set_style_text_color(s_ui.calendar_event, lv_color_hex(COLOR_CYAN), LV_PART_MAIN);
    } else if (calendar_state == CALENDAR_SOURCE_ERROR) {
        lv_label_set_text(s_ui.calendar_event, "Kalendern kunde inte hämtas");
        lv_obj_set_style_text_color(s_ui.calendar_event, lv_color_hex(COLOR_WARNING), LV_PART_MAIN);
    } else {
        lv_label_set_text(s_ui.calendar_event, "Lägg till iCal-adressen i Inställningar");
        lv_obj_set_style_text_color(s_ui.calendar_event, lv_color_hex(COLOR_MUTED), LV_PART_MAIN);
    }

    for (size_t i = 0; i < CALENDAR_EVENT_LIMIT; ++i) {
        if (i < calendar_event_count) {
            char when[48];
            format_calendar_when(when, sizeof(when), &calendar_events[i]);
            lv_label_set_text(s_ui.calendar_when[i], when);
            lv_label_set_text(s_ui.calendar_title[i], calendar_events[i].title);
            lv_obj_clear_flag(s_ui.calendar_row[i], LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(s_ui.calendar_row[i], LV_OBJ_FLAG_HIDDEN);
        }
    }

    if (calendar_event_count > 0) {
        lv_obj_add_flag(s_ui.calendar_empty, LV_OBJ_FLAG_HIDDEN);
    } else {
        const char *empty_text = "Inga kommande händelser";
        if (calendar_state == CALENDAR_SOURCE_SYNCING) empty_text = "Hämtar familjekalendern…";
        else if (calendar_state == CALENDAR_SOURCE_ERROR) empty_text = "Kalendern kunde inte hämtas";
        else if (calendar_state == CALENDAR_SOURCE_DISABLED) empty_text = "Lägg till kalendern i Inställningar";
        lv_label_set_text(s_ui.calendar_empty, empty_text);
        lv_obj_clear_flag(s_ui.calendar_empty, LV_OBJ_FLAG_HIDDEN);
    }

    const char *calendar_status = "Nästa händelser";
    if (calendar_state == CALENDAR_SOURCE_SYNCING) calendar_status = "Uppdaterar kalendern…";
    else if (calendar_state == CALENDAR_SOURCE_ERROR) calendar_status = "Senast hämtade händelser • anslutningsfel";
    lv_label_set_text(s_ui.calendar_status, calendar_status);
    lv_obj_set_style_text_color(
        s_ui.calendar_status,
        lv_color_hex(calendar_state == CALENDAR_SOURCE_ERROR ? COLOR_WARNING : COLOR_MUTED),
        LV_PART_MAIN);

    const char *status_calendar = "Kalendern är ansluten";
    if (calendar_state == CALENDAR_SOURCE_SYNCING) status_calendar = "Hämtar kalenderhändelser…";
    else if (calendar_state == CALENDAR_SOURCE_ERROR) status_calendar = "Kalendern kunde inte hämtas";
    else if (calendar_state == CALENDAR_SOURCE_DISABLED) status_calendar = "Kalendern är inte konfigurerad";
    lv_label_set_text(s_ui.status_calendar, status_calendar);
    lv_obj_set_style_text_color(
        s_ui.status_calendar,
        lv_color_hex(calendar_state == CALENDAR_SOURCE_READY ? COLOR_MINT : COLOR_WARNING),
        LV_PART_MAIN);

    phase_sample_t sample = phase_source_read();
    lv_label_set_text(s_ui.source_badge_label, sample.simulated ? "SIMULERING" : "TIBBER LIVE");
    lv_label_set_text(s_ui.phase_source_status, phase_source_status_text());
    lv_obj_set_style_bg_color(s_ui.source_badge, lv_color_hex(sample.simulated ? 0x392814 : 0x12392E), LV_PART_MAIN);
    lv_obj_set_style_text_color(s_ui.source_badge_label, lv_color_hex(sample.simulated ? COLOR_WARNING : COLOR_MINT), LV_PART_MAIN);
    lv_obj_set_style_text_color(s_ui.phase_source_status, lv_color_hex(sample.simulated ? COLOR_WARNING : COLOR_MINT), LV_PART_MAIN);
    lv_label_set_text(s_ui.status_tibber, phase_source_status_text());
    lv_obj_set_style_text_color(
        s_ui.status_tibber,
        lv_color_hex(sample.simulated ? COLOR_WARNING : COLOR_MINT),
        LV_PART_MAIN);
    const int highest = highest_phase(&sample);
    const load_zone_t highest_zone = load_zone_for_current(sample.current_a[highest], sample.main_fuse_a);
    const int64_t now_ms = esp_timer_get_time() / 1000;

    for (int i = 0; i < 3; ++i) {
        char amps[20];
        char percent[16];
        format_amps(amps, sizeof(amps), sample.current_a[i]);
        snprintf(percent, sizeof(percent), "%d %%", (int)(sample.percent[i] + 0.5f));
        lv_label_set_text(s_ui.phase_amp[i], amps);
        lv_label_set_text(s_ui.phase_percent[i], percent);
        lv_bar_set_value(
            s_ui.phase_bar[i],
            sample.percent[i] > 100.0f ? 100 : (int)sample.percent[i],
            LV_ANIM_ON);

        const load_zone_t zone = load_zone_for_current(sample.current_a[i], sample.main_fuse_a);
        const uint32_t zone_color = load_zone_color(zone);
        lv_obj_set_style_bg_color(
            s_ui.phase_bar[i],
            lv_color_hex(zone_color),
            LV_PART_INDICATOR);
        lv_obj_set_style_text_color(s_ui.phase_amp[i], lv_color_hex(zone_color), LV_PART_MAIN);
        lv_obj_set_style_text_color(s_ui.phase_percent[i], lv_color_hex(zone_color), LV_PART_MAIN);
        lv_obj_set_style_border_width(s_ui.phase_card[i], zone == LOAD_ZONE_GREEN ? 0 : (zone == LOAD_ZONE_RED ? 2 : 1), LV_PART_MAIN);
        lv_obj_set_style_border_color(s_ui.phase_card[i], lv_color_hex(zone_color), LV_PART_MAIN);
    }

    char highest_amps[20];
    char load_value[40];
    format_amps(highest_amps, sizeof(highest_amps), sample.current_a[highest]);
    snprintf(load_value, sizeof(load_value), "L%d  %s", highest + 1, highest_amps);
    lv_label_set_text(s_ui.load_value, load_value);

    const uint32_t highest_color = load_zone_color(highest_zone);
    lv_obj_set_style_text_color(s_ui.load_status, lv_color_hex(highest_color), LV_PART_MAIN);
    lv_obj_set_style_text_color(s_ui.load_value, lv_color_hex(highest_color), LV_PART_MAIN);
    lv_obj_set_style_text_color(s_ui.phase_alert_status, lv_color_hex(highest_color), LV_PART_MAIN);
    lv_obj_set_style_border_width(s_ui.load_panel, highest_zone == LOAD_ZONE_GREEN ? 0 : 2, LV_PART_MAIN);
    lv_obj_set_style_border_color(s_ui.load_panel, lv_color_hex(highest_color), LV_PART_MAIN);

    if (highest_zone == LOAD_ZONE_RED) {
        lv_label_set_text(s_ui.load_status, "STARTA INGET MER");
        lv_label_set_text(s_ui.phase_alert_status, "STARTA INGET MER");
        s_ui.last_over_threshold_ms = now_ms;
        show_phase_view(true);
    } else {
        const char *status_text = highest_zone == LOAD_ZONE_ORANGE ? "Undvik stora laster" : "Gott om marginal";
        lv_label_set_text(s_ui.load_status, status_text);
        lv_label_set_text(s_ui.phase_alert_status, status_text);

        const bool alert_hold_finished = now_ms - s_ui.last_over_threshold_ms >
                                         (int64_t)CONFIG_LASTVAKT_ALERT_HOLD_SECONDS * 1000;
        const bool manual_hold_finished = now_ms > s_ui.manual_phase_until_ms;
        if (s_ui.phase_visible && alert_hold_finished && manual_hold_finished) {
            show_phase_view(false);
        }
    }
}

void ui_create(void)
{
    lv_obj_t *root = lv_scr_act();
    lv_obj_set_style_bg_color(root, lv_color_hex(COLOR_BG), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);

    create_clock_view(root);
    create_phase_view(root);
    create_calendar_view(root);
    create_status_view(root);
    create_shelly_view(root);
    show_phase_view(false);

    lv_timer_t *timer = lv_timer_create(ui_timer, 250, NULL);
    ui_timer(timer);
}
