#include "attention_ui.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "lvgl.h"

static lv_obj_t *s_list_view;
static lv_obj_t *s_detail_view;
static lv_obj_t *s_count_label;
static lv_obj_t *s_status_dot;
static lv_obj_t *s_list;
static lv_obj_t *s_cards[CONFIG_CODEX_ATTENTION_MAX_ITEMS];
static lv_obj_t *s_card_titles[CONFIG_CODEX_ATTENTION_MAX_ITEMS];
static lv_obj_t *s_detail_kind;
static lv_obj_t *s_detail_title;
static lv_obj_t *s_detail_meta;
static lv_obj_t *s_detail_text;
static lv_obj_t *s_detail_body;
static attention_snapshot_t s_snapshot;
static uint32_t s_selected_index;
static char s_selected_id[ATTENTION_ID_MAX];
static char s_detail_id[ATTENTION_ID_MAX];
static attention_ui_open_callback_t s_open_callback;
static void *s_open_context;

static const lv_color_t COLOR_BORDER = LV_COLOR_MAKE(51, 61, 76);
static const lv_color_t COLOR_TEXT = LV_COLOR_MAKE(242, 245, 249);
static const lv_color_t COLOR_MUTED = LV_COLOR_MAKE(151, 162, 179);
static const lv_color_t COLOR_AMBER = LV_COLOR_MAKE(242, 190, 80);
static const lv_color_t COLOR_BLUE = LV_COLOR_MAKE(116, 170, 245);
static const lv_color_t COLOR_GREEN = LV_COLOR_MAKE(101, 212, 183);
static const lv_color_t COLOR_RED = LV_COLOR_MAKE(255, 139, 151);

static void set_common_text(lv_obj_t *label, const lv_font_t *font, lv_color_t color)
{
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, color, 0);
}

static void make_root(lv_obj_t *root)
{
    lv_obj_remove_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(root, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_opa(root, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(root, 0, 0);
    lv_obj_set_style_pad_all(root, 0, 0);
}

static lv_obj_t *create_badge(lv_obj_t *parent, const char *text, lv_color_t color)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text);
    set_common_text(label, &lv_font_montserrat_12, color);
    return label;
}

static void format_age(uint32_t seconds, char *buffer, size_t size)
{
    if (seconds < 60) snprintf(buffer, size, "now");
    else if (seconds < 3600) snprintf(buffer, size, "%lum", (unsigned long)(seconds / 60));
    else if (seconds < 86400) snprintf(buffer, size, "%luh", (unsigned long)(seconds / 3600));
    else snprintf(buffer, size, "%lud", (unsigned long)(seconds / 86400));
}

static int32_t find_item(const char *thread_id)
{
    if (thread_id == NULL || thread_id[0] == '\0') return -1;
    for (uint32_t index = 0; index < s_snapshot.count; ++index) {
        if (strcmp(s_snapshot.items[index].id, thread_id) == 0) return (int32_t)index;
    }
    return -1;
}

static void apply_selection(bool scroll)
{
    for (uint32_t index = 0; index < s_snapshot.count; ++index) {
        lv_obj_t *card = s_cards[index];
        if (card == NULL) continue;
        const bool selected = index == s_selected_index;
        if (s_card_titles[index] != NULL) {
            lv_obj_set_style_text_color(
                s_card_titles[index],
                selected ? COLOR_GREEN : COLOR_TEXT,
                0
            );
        }
    }

    if (scroll && s_snapshot.count > 0 && s_selected_index < s_snapshot.count
        && s_cards[s_selected_index] != NULL) {
        lv_obj_scroll_to_view(s_cards[s_selected_index], LV_ANIM_ON);
    }
}

static void select_index(uint32_t index, bool scroll)
{
    if (s_snapshot.count == 0) {
        s_selected_index = 0;
        s_selected_id[0] = '\0';
        return;
    }
    s_selected_index = index % s_snapshot.count;
    strlcpy(s_selected_id, s_snapshot.items[s_selected_index].id, sizeof(s_selected_id));
    apply_selection(scroll);
}

static void card_clicked(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
    const uintptr_t encoded = (uintptr_t)lv_event_get_user_data(event);
    if (encoded == 0) return;
    const uint32_t index = (uint32_t)(encoded - 1U);
    if (index >= s_snapshot.count) return;
    select_index(index, false);
    if (s_open_callback != NULL) s_open_callback(s_selected_id, s_open_context);
}

static lv_obj_t *create_card(const attention_item_t *item, uint32_t index)
{
    lv_obj_t *card = lv_obj_create(s_list);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_width(card, lv_pct(100));
    lv_obj_set_height(card, LV_SIZE_CONTENT);
    lv_obj_set_style_min_height(card, 92, 0);
    lv_obj_set_style_radius(card, 0, 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_set_style_pad_all(card, 8, 0);
    lv_obj_set_style_pad_row(card, 7, 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_add_event_cb(card, card_clicked, LV_EVENT_CLICKED, (void *)(uintptr_t)(index + 1U));

    lv_obj_t *title = lv_label_create(card);
    lv_obj_set_width(title, lv_pct(100));
    lv_label_set_long_mode(title, LV_LABEL_LONG_WRAP);
    lv_label_set_text(title, item->title);
    set_common_text(title, &lv_font_montserrat_18, COLOR_TEXT);
    s_card_titles[index] = title;

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
    if (item->status == ATTENTION_STATUS_RUNNING) create_badge(badges, "RUNNING", COLOR_GREEN);
    if (item->status == ATTENTION_STATUS_ERROR) create_badge(badges, "ERROR", COLOR_RED);
    return card;
}

static void create_list_view(lv_obj_t *screen)
{
    s_list_view = lv_obj_create(screen);
    make_root(s_list_view);

    lv_obj_t *header = lv_obj_create(s_list_view);
    lv_obj_remove_flag(header, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(header, lv_pct(100), 54);
    lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_opa(header, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_set_style_pad_all(header, 0, 0);
    lv_obj_set_style_pad_column(header, 10, 0);
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(header, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *title = lv_label_create(header);
    lv_label_set_text(title, "Pinned, Unread");
    set_common_text(title, &lv_font_montserrat_24, COLOR_TEXT);

    s_status_dot = lv_obj_create(header);
    lv_obj_remove_flag(s_status_dot, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(s_status_dot, 11, 11);
    lv_obj_set_style_radius(s_status_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_status_dot, COLOR_RED, 0);
    lv_obj_set_style_bg_opa(s_status_dot, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_status_dot, 0, 0);
    s_count_label = lv_label_create(header);
    lv_label_set_text(s_count_label, "(--)");
    set_common_text(s_count_label, &lv_font_montserrat_16, COLOR_TEXT);

    lv_obj_t *divider = lv_obj_create(s_list_view);
    lv_obj_remove_flag(divider, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(divider, 390, 1);
    lv_obj_align(divider, LV_ALIGN_TOP_MID, 0, 54);
    lv_obj_set_style_bg_color(divider, COLOR_BORDER, 0);
    lv_obj_set_style_bg_opa(divider, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(divider, 0, 0);
    lv_obj_set_style_pad_all(divider, 0, 0);

    s_list = lv_obj_create(s_list_view);
    lv_obj_set_size(s_list, 390, 428);
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

static void create_detail_view(lv_obj_t *screen)
{
    s_detail_view = lv_obj_create(screen);
    make_root(s_detail_view);
    lv_obj_add_flag(s_detail_view, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *header = lv_obj_create(s_detail_view);
    lv_obj_remove_flag(header, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(header, 382, 118);
    lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 10);
    lv_obj_set_style_radius(header, 0, 0);
    lv_obj_set_style_bg_opa(header, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_set_style_pad_all(header, 14, 0);

    s_detail_kind = lv_label_create(header);
    lv_obj_set_width(s_detail_kind, 350);
    lv_label_set_long_mode(s_detail_kind, LV_LABEL_LONG_DOT);
    lv_label_set_text(s_detail_kind, "LATEST THREAD TEXT");
    set_common_text(s_detail_kind, &lv_font_montserrat_12, COLOR_BLUE);
    lv_obj_align(s_detail_kind, LV_ALIGN_TOP_LEFT, 0, 0);

    s_detail_title = lv_label_create(header);
    lv_obj_set_width(s_detail_title, 350);
    lv_label_set_long_mode(s_detail_title, LV_LABEL_LONG_DOT);
    lv_label_set_text(s_detail_title, "Loading…");
    set_common_text(s_detail_title, &lv_font_montserrat_20, COLOR_TEXT);
    lv_obj_align(s_detail_title, LV_ALIGN_TOP_LEFT, 0, 26);

    s_detail_meta = lv_label_create(header);
    lv_obj_set_width(s_detail_meta, 350);
    lv_label_set_long_mode(s_detail_meta, LV_LABEL_LONG_DOT);
    lv_label_set_text(s_detail_meta, "PWR back  ·  BOOT next");
    set_common_text(s_detail_meta, &lv_font_montserrat_12, COLOR_MUTED);
    lv_obj_align(s_detail_meta, LV_ALIGN_BOTTOM_LEFT, 0, 0);

    s_detail_body = lv_obj_create(s_detail_view);
    lv_obj_set_size(s_detail_body, 382, 354);
    lv_obj_align(s_detail_body, LV_ALIGN_BOTTOM_MID, 0, -10);
    lv_obj_set_style_radius(s_detail_body, 0, 0);
    lv_obj_set_style_bg_opa(s_detail_body, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_detail_body, 0, 0);
    lv_obj_set_style_pad_all(s_detail_body, 16, 0);
    lv_obj_set_scroll_dir(s_detail_body, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(s_detail_body, LV_SCROLLBAR_MODE_AUTO);

    s_detail_text = lv_label_create(s_detail_body);
    lv_obj_set_width(s_detail_text, 348);
    lv_label_set_long_mode(s_detail_text, LV_LABEL_LONG_WRAP);
    lv_label_set_text(s_detail_text, "Loading latest text…");
    lv_obj_set_style_text_line_space(s_detail_text, 7, 0);
    set_common_text(s_detail_text, &lv_font_montserrat_16, COLOR_TEXT);
}

void attention_ui_init(attention_ui_open_callback_t open_callback, void *context)
{
    s_open_callback = open_callback;
    s_open_context = context;
    memset(&s_snapshot, 0, sizeof(s_snapshot));
    memset(s_cards, 0, sizeof(s_cards));
    memset(s_card_titles, 0, sizeof(s_card_titles));
    s_selected_id[0] = '\0';
    s_detail_id[0] = '\0';

    lv_obj_t *screen = lv_screen_active();
    lv_obj_set_style_bg_opa(screen, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(screen, 0, 0);

    create_list_view(screen);
    create_detail_view(screen);
}

void attention_ui_render(const attention_snapshot_t *snapshot)
{
    if (snapshot == NULL) return;

    char prior_selected[ATTENTION_ID_MAX];
    strlcpy(prior_selected, s_selected_id, sizeof(prior_selected));
    s_snapshot = *snapshot;

    const int32_t selected = find_item(prior_selected);
    if (selected >= 0) {
        s_selected_index = (uint32_t)selected;
        strlcpy(s_selected_id, prior_selected, sizeof(s_selected_id));
    } else if (s_snapshot.count > 0) {
        s_selected_index = 0;
        strlcpy(s_selected_id, s_snapshot.items[0].id, sizeof(s_selected_id));
    } else {
        s_selected_index = 0;
        s_selected_id[0] = '\0';
    }

    char count[20];
    if (snapshot->total_count > 99) snprintf(count, sizeof(count), "99+");
    else snprintf(count, sizeof(count), "%lu", (unsigned long)snapshot->total_count);
    lv_label_set_text(s_count_label, count);

    const bool status_ok = snapshot->source_error[0] == '\0'
        && snapshot->desktop_state_available;
    lv_obj_set_style_bg_color(s_status_dot, status_ok ? COLOR_GREEN : COLOR_RED, 0);

    memset(s_cards, 0, sizeof(s_cards));
    memset(s_card_titles, 0, sizeof(s_card_titles));
    lv_obj_clean(s_list);
    if (snapshot->count == 0) {
        lv_obj_t *empty = lv_obj_create(s_list);
        lv_obj_remove_flag(empty, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_size(empty, lv_pct(100), 150);
        lv_obj_set_style_radius(empty, 0, 0);
        lv_obj_set_style_bg_opa(empty, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(empty, 0, 0);

        lv_obj_t *label = lv_label_create(empty);
        lv_label_set_text(label, snapshot->source_error[0] == '\0'
            ? "Inbox clear\nNo unread, pinned, or waiting threads."
            : "No cached threads\nCheck the Mac bridge and Wi-Fi.");
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_line_space(label, 8, 0);
        set_common_text(label, &lv_font_montserrat_16, COLOR_MUTED);
        lv_obj_center(label);
    } else {
        for (uint32_t index = 0; index < snapshot->count; ++index) {
            s_cards[index] = create_card(&snapshot->items[index], index);
        }
        apply_selection(false);
    }

    if (attention_ui_is_detail_visible() && find_item(s_detail_id) < 0) {
        attention_ui_show_list();
    }
}

bool attention_ui_select_next(void)
{
    if (s_snapshot.count == 0) return false;
    select_index((s_selected_index + 1U) % s_snapshot.count, true);
    return true;
}

bool attention_ui_activate_selected(void)
{
    if (s_snapshot.count == 0 || s_selected_id[0] == '\0') return false;
    if (s_open_callback != NULL) s_open_callback(s_selected_id, s_open_context);
    return true;
}

bool attention_ui_get_selected_id(char *output, size_t output_size)
{
    if (output == NULL || output_size == 0 || s_selected_id[0] == '\0') return false;
    strlcpy(output, s_selected_id, output_size);
    return true;
}

void attention_ui_show_list(void)
{
    s_detail_id[0] = '\0';
    lv_obj_add_flag(s_detail_view, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(s_list_view, LV_OBJ_FLAG_HIDDEN);
    apply_selection(true);
}

void attention_ui_show_detail_loading(const char *thread_id)
{
    if (thread_id == NULL || thread_id[0] == '\0') return;
    strlcpy(s_detail_id, thread_id, sizeof(s_detail_id));

    const int32_t index = find_item(thread_id);
    if (index >= 0) {
        const attention_item_t *item = &s_snapshot.items[index];
        lv_label_set_text(s_detail_title, item->title);
        lv_label_set_text_fmt(s_detail_meta, "%s  |  PWR back  ·  BOOT next", item->project);
    } else {
        lv_label_set_text(s_detail_title, "Codex thread");
        lv_label_set_text(s_detail_meta, "PWR back  ·  BOOT next");
    }
    lv_label_set_text(s_detail_kind, "LOADING LATEST TEXT");
    lv_obj_set_style_text_color(s_detail_kind, COLOR_BLUE, 0);
    lv_label_set_text(s_detail_text, "Loading latest text…");
    lv_obj_scroll_to_y(s_detail_body, 0, LV_ANIM_OFF);
    lv_obj_add_flag(s_list_view, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(s_detail_view, LV_OBJ_FLAG_HIDDEN);
}

void attention_ui_render_detail(const attention_detail_t *detail)
{
    if (detail == NULL || !attention_ui_is_detail_for(detail->id)) return;
    lv_label_set_text(s_detail_title, detail->title);
    lv_label_set_text_fmt(s_detail_meta, "%s  |  PWR back  ·  BOOT next", detail->project);
    lv_label_set_text_fmt(
        s_detail_kind,
        "%s%s",
        detail->kind,
        detail->truncated ? "  |  TRUNCATED" : ""
    );
    lv_obj_set_style_text_color(s_detail_kind, COLOR_BLUE, 0);
    lv_label_set_text(s_detail_text, detail->text);
    lv_obj_scroll_to_y(s_detail_body, 0, LV_ANIM_OFF);
}

void attention_ui_show_detail_error(const char *thread_id, const char *message)
{
    if (!attention_ui_is_detail_for(thread_id)) return;
    lv_label_set_text(s_detail_kind, "COULD NOT LOAD");
    lv_obj_set_style_text_color(s_detail_kind, COLOR_RED, 0);
    lv_label_set_text(s_detail_text, message == NULL ? "Unknown bridge error" : message);
    lv_obj_scroll_to_y(s_detail_body, 0, LV_ANIM_OFF);
}

bool attention_ui_is_detail_visible(void)
{
    return !lv_obj_has_flag(s_detail_view, LV_OBJ_FLAG_HIDDEN);
}

bool attention_ui_is_detail_for(const char *thread_id)
{
    return attention_ui_is_detail_visible()
        && thread_id != NULL
        && strcmp(s_detail_id, thread_id) == 0;
}
