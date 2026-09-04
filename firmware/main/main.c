#include <stdio.h>
#include <string.h>
#include "attention_client.h"
#include "attention_audio.h"
#include "attention_model.h"
#include "attention_ui.h"
#include "bsp/display.h"
#include "bsp/esp-bsp.h"
#include "button_input.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "sdkconfig.h"
#include "wifi_manager.h"

static const char *TAG = "codex_display";

typedef struct {
    char thread_id[ATTENTION_ID_MAX];
} detail_request_t;

static QueueHandle_t s_detail_queue;

static uint8_t attention_reason_mask(const attention_item_t *item)
{
    if (item == NULL) return 0;

    uint8_t mask = 0;
    if (item->unread) mask |= 1U << 0;
    if (item->pinned) mask |= 1U << 1;
    if (item->new_result) mask |= 1U << 2;
    if (item->status == ATTENTION_STATUS_WAITING_INPUT
        || item->status == ATTENTION_STATUS_WAITING_APPROVAL) {
        mask |= 1U << 3;
    }
    return mask;
}

static const attention_item_t *find_attention_item(
    const attention_snapshot_t *snapshot,
    const char *thread_id
)
{
    if (snapshot == NULL || thread_id == NULL) return NULL;

    for (uint32_t index = 0; index < snapshot->count; ++index) {
        if (strcmp(snapshot->items[index].id, thread_id) == 0) {
            return &snapshot->items[index];
        }
    }
    return NULL;
}

static bool snapshot_should_chime(
    const attention_snapshot_t *previous,
    const attention_snapshot_t *next
)
{
    if (previous == NULL || next == NULL || next->source_error[0] != '\0') return false;

    for (uint32_t index = 0; index < next->count; ++index) {
        const attention_item_t *item = &next->items[index];
        const attention_item_t *old_item = find_attention_item(previous, item->id);
        if (old_item == NULL) return true;

        const uint8_t old_reasons = attention_reason_mask(old_item);
        const uint8_t new_reasons = attention_reason_mask(item);
        if ((new_reasons & (uint8_t)~old_reasons) != 0) return true;
    }

    return false;
}

static void render_snapshot(const attention_snapshot_t *snapshot)
{
    bsp_display_lock(0);
    attention_ui_render(snapshot);
    bsp_display_unlock();
}

static void queue_detail(const char *thread_id, void *context)
{
    (void)context;
    if (thread_id == NULL || thread_id[0] == '\0' || s_detail_queue == NULL) return;

    attention_ui_show_detail_loading(thread_id);
    detail_request_t request = { 0 };
    strlcpy(request.thread_id, thread_id, sizeof(request.thread_id));
    (void)xQueueOverwrite(s_detail_queue, &request);
}

static void poll_task(void *argument)
{
    (void)argument;
    attention_snapshot_t current = { 0 };
    attention_snapshot_t previous_success = { 0 };
    bool has_previous_success = false;
    attention_snapshot_t fetched;

    while (true) {
        if (!wifi_manager_wait_connected(8000)) {
            strlcpy(current.source_error, "Wi-Fi not connected", sizeof(current.source_error));
            render_snapshot(&current);
            vTaskDelay(pdMS_TO_TICKS(2000));
            continue;
        }

        esp_err_t result = attention_client_fetch(&fetched);
        if (result == ESP_OK) {
            if (has_previous_success && snapshot_should_chime(&previous_success, &fetched)) {
                attention_audio_notify();
            }
            current = fetched;
            previous_success = fetched;
            has_previous_success = true;
        } else {
            snprintf(
                current.source_error,
                sizeof(current.source_error),
                "Request failed: %s",
                esp_err_to_name(result)
            );
            ESP_LOGW(TAG, "%s", current.source_error);
        }
        render_snapshot(&current);
        vTaskDelay(pdMS_TO_TICKS(CONFIG_CODEX_ATTENTION_POLL_INTERVAL_MS));
    }
}

static void detail_task(void *argument)
{
    (void)argument;
    detail_request_t request;

    while (true) {
        if (xQueueReceive(s_detail_queue, &request, portMAX_DELAY) != pdTRUE) continue;

        attention_detail_t detail = { 0 };
        esp_err_t result;
        if (!wifi_manager_wait_connected(8000)) result = ESP_ERR_TIMEOUT;
        else result = attention_client_fetch_detail(request.thread_id, &detail);

        bsp_display_lock(0);
        if (attention_ui_is_detail_for(request.thread_id)) {
            if (result == ESP_OK) {
                attention_ui_render_detail(&detail);
            } else {
                char message[ATTENTION_ERROR_MAX];
                snprintf(message, sizeof(message), "Could not load latest text: %s", esp_err_to_name(result));
                attention_ui_show_detail_error(request.thread_id, message);
            }
        }
        bsp_display_unlock();
    }
}

static void button_task(void *argument)
{
    (void)argument;
    button_input_event_t event;

    while (true) {
        if (!button_input_poll(&event)) {
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }

        bsp_display_lock(0);
        if (event == BUTTON_INPUT_NEXT) {
            if (attention_ui_is_settings_visible()) {
                attention_ui_show_list();
            } else if (attention_ui_is_detail_visible()) {
                attention_ui_show_list();
                if (attention_ui_select_next()) (void)attention_ui_activate_selected();
            } else {
                (void)attention_ui_select_next();
            }
        } else if (event == BUTTON_INPUT_SELECT) {
            if (attention_ui_is_detail_visible() || attention_ui_is_settings_visible()) {
                attention_ui_show_list();
            }
            else (void)attention_ui_activate_selected();
        }
        bsp_display_unlock();
    }
}

static void create_task_or_log(
    TaskFunction_t task,
    const char *name,
    uint32_t stack_depth,
    UBaseType_t priority
)
{
    if (xTaskCreate(task, name, stack_depth, NULL, priority, NULL) != pdPASS) {
        ESP_LOGE(TAG, "Could not create %s task", name);
    }
}

void app_main(void)
{
    esp_err_t nvs_result = nvs_flash_init();
    if (nvs_result == ESP_ERR_NVS_NO_FREE_PAGES || nvs_result == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        nvs_result = nvs_flash_init();
    }
    ESP_ERROR_CHECK(nvs_result);

    lv_display_t *display = bsp_display_start();
    if (display == NULL) {
        ESP_LOGE(TAG, "Could not start Waveshare display BSP");
        return;
    }
    ESP_ERROR_CHECK(bsp_display_backlight_on());

    s_detail_queue = xQueueCreate(1, sizeof(detail_request_t));
    if (s_detail_queue == NULL) {
        ESP_LOGE(TAG, "Could not create detail request queue");
        return;
    }

    bsp_display_lock(0);
    attention_ui_init(queue_detail, NULL);
    bsp_display_unlock();

    ESP_ERROR_CHECK(button_input_init());
    esp_err_t audio_result = attention_audio_init();
    if (audio_result != ESP_OK) {
        ESP_LOGW(TAG, "Attention audio disabled: %s", esp_err_to_name(audio_result));
    }
    ESP_ERROR_CHECK(wifi_manager_start());

    create_task_or_log(poll_task, "attention_poll", 16384, 5);
    create_task_or_log(detail_task, "attention_detail", 16384, 5);
    create_task_or_log(button_task, "attention_buttons", 4096, 6);
}
