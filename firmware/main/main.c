#include <stdio.h>
#include <string.h>
#include "attention_client.h"
#include "attention_model.h"
#include "attention_ui.h"
#include "bsp/display.h"
#include "bsp/esp-bsp.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "sdkconfig.h"
#include "wifi_manager.h"

static const char *TAG = "codex_display";

static void render_snapshot(const attention_snapshot_t *snapshot)
{
    bsp_display_lock(0);
    attention_ui_render(snapshot);
    bsp_display_unlock();
}

static void poll_task(void *argument)
{
    (void)argument;
    attention_snapshot_t current = { 0 };
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
            current = fetched;
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

    bsp_display_lock(0);
    attention_ui_init();
    bsp_display_unlock();

    ESP_ERROR_CHECK(wifi_manager_start());
    BaseType_t created = xTaskCreate(
        poll_task,
        "attention_poll",
        16384,
        NULL,
        5,
        NULL
    );
    if (created != pdPASS) ESP_LOGE(TAG, "Could not create poll task");
}
