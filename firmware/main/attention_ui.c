#include "attention_ui.h"

#include <stdio.h>
#include <string.h>
#include "lvgl.h"

static lv_obj_t *s_count_label;
static lv_obj_t *s_status_label;
static lv_obj_t *s_list;

static const lv_color_t COLOR_BG = LV_COLOR_MAKE(8, 10, 14);
static const lv_color_t COLOR_CARD = LV_COLOR_MAKE(22, 26, 34);
static const lv_color_t COLOR_CARD_WAIT = LV_COLOR_MAKE(48, 37, 18);
static const lv_color_t COLOR_BORDER = LV_COLOR_MAKE(51, 61, 76);
static const lv_color_t COLOR_TEXT = LV_COLOR_MAKE(242, 245, 249);
static const lv_color_t COLOR_MUTED = LV_COLOR_MAKE(151, 162, 179);
static const lv_color_t COLOR_AMBER = LV_COLOR_MAKE(242, 190, 80);
static const lv_color_t COLOR_BLUE = LV_COLOR_MAKE(116, 170, 245);
static const lv_color_t COLOR_PURPLE = LV_COLOR_MAKE(185, 147, 255);
static const lv_color_t COLOR_GREEN = LV_COLOR_MAKE(101, 212, 183);
static const lv_color_t COLOR_RED = LV_COLOR_MAKE(255, 139, 151);

static void set_common_text(lv_obj_t *label, const lv_font_t *font, lv_color_t color)
{
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, color, 0);
}

static lv_obj_t *create_badge(lv_obj_t *parent, const char *text, lv_color_t color)
{
    lv_obj_t *badge = lv_obj_create(parent);
    lv_obj_remove_flag(badge, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(badge, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_radius(badge, 20, 0);
    lv_obj_set_style_bg_opa(badge, LV_OPA_10, 0);
    lv_obj_set_style_bg_color(badge, color, 0);
    lv_obj_set_style_border_width(badge, 1, 0);
    lv_obj_set_style_border_color(badge, color, 0);
    lv_obj_set_style_pad_hor(badge, 8, 0);
    lv_obj_set_style_pad_ver(badge, 4, 0);

    lv_obj_t *label = lv_label_create(badge);
    lv_label_set_text(label, text);
    set_common_text(label, &lv_font_montserrat_12, color);
    return badge;
}

static void format_age(uint32_t seconds, char *buffer, size_t size)
{
    if (seconds < 60) snprintf(buffer, size, "now");
    else if (seconds < 3600) snprintf(buffer, size, "%lum", (unsigned long)(seconds / 60));
    else if (seconds < 86400) snprintf(buffer, size, "%luh", (unsigned long)(seconds / 3600));
    else snprintf(buffer, size, "%lud", (unsigned long)(seconds / 86400));
}

static void create_card(const attention_item_t *item)
{
    const bool waiting = item->status == ATTENTION_STATUS_WAITING_INPUT
        || item->status == ATTENTION_STATUS_WAITING_APPROVAL;

    lv_obj_t *card = lv_obj_create(s_list);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_width(card, lv_pct(100));
    lv_obj_set_height(card, LV_SIZE_CONTENT);
    lv_obj_set_style_min_height(card, 92, 0);
    lv_obj_set_style_radius(card, 16, 0);
    lv_obj_set_style_bg_color(card, waiting ? COLOR_CARD_WAIT : COLOR_CARD, 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_border_color(card, waiting ? COLOR_AMBER : COLOR_BORDER, 0);
    lv_obj_set_style_pad_all(card, 14, 0);
    lv_obj_set_style_pad_row(card, 7, 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);

    lv_obj_t *title = lv_label_create(card);
    lv_obj_set_width(title, lv_pct(100));
    lv_label_set_long_mode(title, LV_LABEL_LONG_WRAP);
    lv_label_set_text(title, item->title);
    set_common_text(title, &lv_font_montserrat_18, COLOR_TEXT);

    char age[16];
    char meta[ATTENTION_PROJECT_MAX + 24];
    format_age(item->age_seconds, age, sizeof(age));
    snprintf(meta, sizeof(meta), "%s  |  %s", item->project, age);
    lv_obj_t *meta_label = lv_label_create(card);
    lv_obj_set_width(meta_label, lv_pct(100));
    lv_label_set_long_mode(meta_label, LV_LABEL_LONG_DOT);
    lv_label_set_text(meta_label, meta);
    set_common_text(meta_label, &lv_font_montserrat_14, COLOR_MUTED);

    lv_obj_t *badges = lv_obj_create(card);
    lv_obj_remove_flag(badges, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(badges, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(badges, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(badges, 0, 0);
    lv_obj_set_style_pad_all(badges, 0, 0);
    lv_obj_set_style_pad_column(badges, 6, 0);
    lv_obj_set_style_pad_row(badges, 5, 0);
    lv_obj_set_flex_flow(badges, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(badges, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);

    if (item->status == ATTENTION_STATUS_WAITING_APPROVAL) create_badge(badges, "APPROVAL", COLOR_AMBER);
    if (item->status == ATTENTION_STATUS_WAITING_INPUT) create_badge(badges, "INPUT", COLOR_AMBER);
    if (item->new_result) create_badge(badges, "NEW", COLOR_BLUE);
    else if (item->unread) create_badge(badges, "UNREAD", COLOR_BLUE);
    if (item->pinned) create_badge(badges, "PINNED", COLOR_PURPLE);
    if (item->status == ATTENTION_STATUS_RUNNING) create_badge(badges, "RUNNING", COLOR_GREEN);
    if (item->status == ATTENTION_STATUS_ERROR) create_badge(badges, "ERROR", COLOR_RED);
}

void attention_ui_init(void)
{
    lv_obj_t *screen = lv_screen_active();
    lv_obj_set_style_bg_color(screen, COLOR_BG, 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(screen, 0, 0);

    lv_obj_t *header = lv_obj_create(screen);
    lv_obj_remove_flag(header, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(header, lv_pct(100), 92);
    lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_opa(header, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_set_style_pad_hor(header, 18, 0);
    lv_obj_set_style_pad_top(header, 17, 0);
    lv_obj_set_style_pad_bottom(header, 8, 0);

    lv_obj_t *eyebrow = lv_label_create(header);
    lv_label_set_text(eyebrow, "CODEX INBOX");
    set_common_text(eyebrow, &lv_font_montserrat_12, COLOR_MUTED);
    lv_obj_align(eyebrow, LV_ALIGN_TOP_LEFT, 0, 0);

    lv_obj_t *title = lv_label_create(header);
    lv_label_set_text(title, "Needs attention");
    set_common_text(title, &lv_font_montserrat_24, COLOR_TEXT);
    lv_obj_align(title, LV_ALIGN_BOTTOM_LEFT, 0, -3);

    lv_obj_t *count_pill = lv_obj_create(header);
    lv_obj_remove_flag(count_pill, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(count_pill, 58, 38);
    lv_obj_align(count_pill, LV_ALIGN_BOTTOM_RIGHT, 0, -3);
    lv_obj_set_style_radius(count_pill, 20, 0);
    lv_obj_set_style_bg_color(count_pill, COLOR_CARD, 0);
    lv_obj_set_style_bg_opa(count_pill, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(count_pill, COLOR_BORDER, 0);
    lv_obj_set_style_border_width(count_pill, 1, 0);

    s_count_label = lv_label_create(count_pill);
    lv_label_set_text(s_count_label, "--");
    set_common_text(s_count_label, &lv_font_montserrat_16, COLOR_TEXT);
    lv_obj_center(s_count_label);

    s_status_label = lv_label_create(screen);
    lv_obj_set_width(s_status_label, 374);
    lv_label_set_long_mode(s_status_label, LV_LABEL_LONG_DOT);
    lv_label_set_text(s_status_label, "Connecting to bridge…");
    set_common_text(s_status_label, &lv_font_montserrat_12, COLOR_MUTED);
    lv_obj_align(s_status_label, LV_ALIGN_TOP_MID, 0, 88);

    s_list = lv_obj_create(screen);
    lv_obj_set_size(s_list, 390, 382);
    lv_obj_align(s_list, LV_ALIGN_BOTTOM_MID, 0, -9);
    lv_obj_set_style_bg_opa(s_list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_list, 0, 0);
    lv_obj_set_style_pad_all(s_list, 0, 0);
    lv_obj_set_style_pad_row(s_list, 10, 0);
    lv_obj_set_flex_flow(s_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
    lv_obj_set_scroll_dir(s_list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(s_list, LV_SCROLLBAR_MODE_AUTO);
}

void attention_ui_render(const attention_snapshot_t *snapshot)
{
    if (snapshot == NULL) return;

    char count[20];
    if (snapshot->total_count > 99) snprintf(count, sizeof(count), "99+");
    else snprintf(count, sizeof(count), "%lu", (unsigned long)snapshot->total_count);
    lv_label_set_text(s_count_label, count);

    if (snapshot->source_error[0] != '\0') {
        lv_label_set_text_fmt(s_status_label, "BRIDGE ERROR  |  %s", snapshot->source_error);
        lv_obj_set_style_text_color(s_status_label, COLOR_RED, 0);
    } else if (!snapshot->desktop_state_available) {
        lv_label_set_text(s_status_label, "CONNECTED  |  unread state unavailable");
        lv_obj_set_style_text_color(s_status_label, COLOR_AMBER, 0);
    } else if (snapshot->truncated) {
        lv_label_set_text(s_status_label, "CONNECTED  |  scroll for priority items");
        lv_obj_set_style_text_color(s_status_label, COLOR_GREEN, 0);
    } else {
        lv_label_set_text(s_status_label, "CONNECTED  |  live");
        lv_obj_set_style_text_color(s_status_label, COLOR_GREEN, 0);
    }

    lv_obj_clean(s_list);
    if (snapshot->count == 0) {
        lv_obj_t *empty = lv_obj_create(s_list);
        lv_obj_remove_flag(empty, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_size(empty, lv_pct(100), 150);
        lv_obj_set_style_radius(empty, 18, 0);
        lv_obj_set_style_bg_color(empty, COLOR_CARD, 0);
        lv_obj_set_style_bg_opa(empty, LV_OPA_COVER, 0);
        lv_obj_set_style_border_color(empty, COLOR_BORDER, 0);
        lv_obj_set_style_border_width(empty, 1, 0);

        lv_obj_t *label = lv_label_create(empty);
        lv_label_set_text(label, snapshot->source_error[0] == '\0'
            ? "Inbox clear\nNo unread, pinned, or waiting threads."
            : "No cached threads\nCheck the Mac bridge and Wi-Fi.");
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_line_space(label, 8, 0);
        set_common_text(label, &lv_font_montserrat_16, COLOR_MUTED);
        lv_obj_center(label);
        return;
    }

    for (uint32_t index = 0; index < snapshot->count; ++index) {
        create_card(&snapshot->items[index]);
    }
}
