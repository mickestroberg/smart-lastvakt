#include "ui.h"

#include <stdbool.h>
#include <stdio.h>

#include "esp_timer.h"
#include "lvgl.h"
#include "sdkconfig.h"

#include "app_time.h"
#include "lastvakt_fonts.h"
#include "network_setup.h"
#include "phase_source.h"

#define COLOR_BG 0x07111F
#define COLOR_PANEL 0x0D1B2B
#define COLOR_PANEL_LIGHT 0x13263A
#define COLOR_TEXT 0xF3F7FA
#define COLOR_MUTED 0x8CA0B3
#define COLOR_CYAN 0x34D6E7
#define COLOR_PURPLE 0x9B8AFB
#define COLOR_MINT 0x56D6A7
#define COLOR_WARNING 0xFFAA4C

typedef struct {
    lv_obj_t *clock_view;
    lv_obj_t *phase_view;
    lv_obj_t *clock_large;
    lv_obj_t *clock_small;
    lv_obj_t *date;
    lv_obj_t *time_status;
    lv_obj_t *network_status;
    lv_obj_t *source_badge;
    lv_obj_t *source_badge_label;
    lv_obj_t *phase_source_status;
    lv_obj_t *load_status;
    lv_obj_t *load_value;
    lv_obj_t *phase_amp[3];
    lv_obj_t *phase_percent[3];
    lv_obj_t *phase_bar[3];
    lv_obj_t *phase_card[3];
    bool phase_visible;
    int64_t last_over_threshold_ms;
    int64_t manual_phase_until_ms;
} ui_state_t;

static ui_state_t s_ui;

static lv_obj_t *make_label(lv_obj_t *parent, const lv_font_t *font, uint32_t color)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_set_style_text_font(label, font, LV_PART_MAIN);
    lv_obj_set_style_text_color(label, lv_color_hex(color), LV_PART_MAIN);
    return label;
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
        lv_obj_clear_flag(s_ui.phase_view, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_ui.phase_view, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_ui.clock_view, LV_OBJ_FLAG_HIDDEN);
    }
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

static void create_clock_view(lv_obj_t *root)
{
    s_ui.clock_view = lv_obj_create(root);
    lv_obj_set_size(s_ui.clock_view, 800, 480);
    lv_obj_center(s_ui.clock_view);
    style_panel(s_ui.clock_view, COLOR_BG, 0);

    lv_obj_t *eyebrow = make_label(s_ui.clock_view, &lv_font_lastvakt_16, COLOR_CYAN);
    lv_label_set_text(eyebrow, "SMART LASTVAKT");
    lv_obj_align(eyebrow, LV_ALIGN_TOP_LEFT, 36, 28);

    s_ui.source_badge = lv_obj_create(s_ui.clock_view);
    lv_obj_set_size(s_ui.source_badge, 128, 34);
    lv_obj_align(s_ui.source_badge, LV_ALIGN_TOP_RIGHT, -36, 22);
    style_panel(s_ui.source_badge, 0x392814, 17);
    s_ui.source_badge_label = make_label(s_ui.source_badge, &lv_font_lastvakt_12, COLOR_WARNING);
    lv_label_set_text(s_ui.source_badge_label, "SIMULERING");
    lv_obj_center(s_ui.source_badge_label);

    s_ui.clock_large = make_label(s_ui.clock_view, &lv_font_montserrat_48, COLOR_TEXT);
    lv_label_set_text(s_ui.clock_large, "--:--");
    lv_obj_align(s_ui.clock_large, LV_ALIGN_TOP_MID, 0, 96);

    s_ui.date = make_label(s_ui.clock_view, &lv_font_lastvakt_24, COLOR_MUTED);
    lv_label_set_text(s_ui.date, "Svensk tid");
    lv_obj_align(s_ui.date, LV_ALIGN_TOP_MID, 0, 164);

    s_ui.time_status = make_label(s_ui.clock_view, &lv_font_lastvakt_12, COLOR_MUTED);
    lv_label_set_text(s_ui.time_status, "Offline");
    lv_obj_align(s_ui.time_status, LV_ALIGN_TOP_MID, 0, 207);

    s_ui.network_status = make_label(s_ui.clock_view, &lv_font_lastvakt_12, COLOR_MUTED);
    lv_label_set_text(s_ui.network_status, "Startar nätverk…");
    lv_obj_align(s_ui.network_status, LV_ALIGN_TOP_MID, 0, 232);

    lv_obj_t *load_panel = lv_obj_create(s_ui.clock_view);
    lv_obj_set_size(load_panel, 728, 122);
    lv_obj_align(load_panel, LV_ALIGN_BOTTOM_MID, 0, -30);
    style_panel(load_panel, COLOR_PANEL, 22);

    lv_obj_t *load_caption = make_label(load_panel, &lv_font_lastvakt_12, COLOR_MUTED);
    lv_label_set_text(load_caption, "HÖGSTA FAS JUST NU");
    lv_obj_align(load_caption, LV_ALIGN_TOP_LEFT, 24, 19);

    s_ui.load_status = make_label(load_panel, &lv_font_lastvakt_20, COLOR_MINT);
    lv_label_set_text(s_ui.load_status, "Alla faser lugna");
    lv_obj_align(s_ui.load_status, LV_ALIGN_TOP_LEFT, 24, 48);

    s_ui.load_value = make_label(load_panel, &lv_font_lastvakt_24, COLOR_TEXT);
    lv_label_set_text(s_ui.load_value, "0 %");
    lv_obj_align(s_ui.load_value, LV_ALIGN_RIGHT_MID, -170, 4);

    lv_obj_t *button = make_button(load_panel, "Visa faser", view_button_event);
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

    lv_obj_t *button = make_button(s_ui.phase_view, "Klocka", view_button_event);
    lv_obj_align(button, LV_ALIGN_TOP_RIGHT, -36, 17);

    create_phase_row(s_ui.phase_view, 0, 94, COLOR_CYAN);
    create_phase_row(s_ui.phase_view, 1, 200, COLOR_PURPLE);
    create_phase_row(s_ui.phase_view, 2, 306, COLOR_MINT);

    lv_obj_t *hint = make_label(s_ui.phase_view, &lv_font_lastvakt_12, COLOR_MUTED);
    lv_label_set_text_fmt(
        hint,
        "Automatisk fasvy över %d %% • huvudsäkring %d A",
        CONFIG_LASTVAKT_ALERT_PERCENT,
        CONFIG_LASTVAKT_MAIN_FUSE_A);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -22);
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

static void ui_timer(lv_timer_t *timer)
{
    (void)timer;
    char clock_text[16];
    char date_text[64];
    app_time_format(clock_text, sizeof(clock_text), date_text, sizeof(date_text));
    lv_label_set_text(s_ui.clock_large, clock_text);
    lv_label_set_text(s_ui.clock_small, clock_text);
    lv_label_set_text(s_ui.date, date_text);
    lv_label_set_text(s_ui.time_status, app_time_status_text());
    lv_label_set_text(s_ui.network_status, network_setup_status_text());

    phase_sample_t sample = phase_source_read();
    lv_label_set_text(s_ui.source_badge_label, sample.simulated ? "SIMULERING" : "TIBBER LIVE");
    lv_label_set_text(s_ui.phase_source_status, phase_source_status_text());
    lv_obj_set_style_bg_color(s_ui.source_badge, lv_color_hex(sample.simulated ? 0x392814 : 0x12392E), LV_PART_MAIN);
    lv_obj_set_style_text_color(s_ui.source_badge_label, lv_color_hex(sample.simulated ? COLOR_WARNING : COLOR_MINT), LV_PART_MAIN);
    lv_obj_set_style_text_color(s_ui.phase_source_status, lv_color_hex(sample.simulated ? COLOR_WARNING : COLOR_MINT), LV_PART_MAIN);
    const int highest = highest_phase(&sample);
    const bool over_threshold = sample.percent[highest] >= (float)CONFIG_LASTVAKT_ALERT_PERCENT;
    const int64_t now_ms = esp_timer_get_time() / 1000;

    static const uint32_t normal_colors[] = {COLOR_CYAN, COLOR_PURPLE, COLOR_MINT};
    for (int i = 0; i < 3; ++i) {
        char amps[20];
        snprintf(amps, sizeof(amps), "%.1f A", sample.current_a[i]);
        for (char *p = amps; *p != '\0'; ++p) {
            if (*p == '.') {
                *p = ',';
            }
        }
        lv_label_set_text(s_ui.phase_amp[i], amps);
        lv_label_set_text_fmt(s_ui.phase_percent[i], "%.0f %%", sample.percent[i]);
        lv_bar_set_value(
            s_ui.phase_bar[i],
            sample.percent[i] > 100.0f ? 100 : (int)sample.percent[i],
            LV_ANIM_ON);

        const bool row_warning = sample.percent[i] >= (float)CONFIG_LASTVAKT_ALERT_PERCENT;
        lv_obj_set_style_bg_color(
            s_ui.phase_bar[i],
            lv_color_hex(row_warning ? COLOR_WARNING : normal_colors[i]),
            LV_PART_INDICATOR);
        lv_obj_set_style_border_width(s_ui.phase_card[i], row_warning ? 2 : 0, LV_PART_MAIN);
        lv_obj_set_style_border_color(s_ui.phase_card[i], lv_color_hex(COLOR_WARNING), LV_PART_MAIN);
    }

    lv_label_set_text_fmt(s_ui.load_value, "%.0f %%", sample.percent[highest]);
    if (over_threshold) {
        lv_label_set_text_fmt(s_ui.load_status, "L%d behöver uppmärksamhet", highest + 1);
        lv_obj_set_style_text_color(s_ui.load_status, lv_color_hex(COLOR_WARNING), LV_PART_MAIN);
        s_ui.last_over_threshold_ms = now_ms;
        show_phase_view(true);
    } else {
        lv_label_set_text(s_ui.load_status, "Alla faser lugna");
        lv_obj_set_style_text_color(s_ui.load_status, lv_color_hex(COLOR_MINT), LV_PART_MAIN);

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
    show_phase_view(false);

    lv_timer_t *timer = lv_timer_create(ui_timer, 250, NULL);
    ui_timer(timer);
}
