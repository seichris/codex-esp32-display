#include "attention_display.h"
#include "display_frame.h"

#include <stdbool.h>
#include <assert.h>
#include "esp_err.h"
#include "bsp/display.h"
#include "bsp/touch.h"
#include "bsp/esp-bsp.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_io.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

// The board BSP initializes its CO5300 using the SH8601-compatible QSPI
// commands and a 22-column panel offset. Keep that initialization, but own
// transfer completion and errors instead of the BSP's unchecked flush path.
#define PANEL_X_OFFSET 22
#define PANEL_COMMAND(command) (0x02000000U | ((command) << 8))
#define PANEL_PIXELS_COMMAND (0x32000000U | (0x2CU << 8))

_Static_assert(DISPLAY_FRAME_WIDTH == BSP_LCD_H_RES && DISPLAY_FRAME_HEIGHT == BSP_LCD_V_RES,
               "Frame transport must match the Waveshare panel");
_Static_assert(DISPLAY_FRAME_WIDTH % 2 == 0 && DISPLAY_FRAME_HEIGHT % 2 == 0
               && DISPLAY_FRAME_STRIP_ROWS % 2 == 0, "Panel writes require even bounds");

static const char *TAG = "attention_display";
static esp_lcd_panel_io_handle_t s_io;
static SemaphoreHandle_t s_transfer_done;
static uint8_t *s_staging;
static bool s_in_flight;
static bool s_redraw_needed;

static bool transfer_done(esp_lcd_panel_io_handle_t io, esp_lcd_panel_io_event_data_t *event, void *context)
{
    (void)io;
    (void)event;
    (void)context;
    BaseType_t wake = pdFALSE;
    xSemaphoreGiveFromISR(s_transfer_done, &wake);
    return wake == pdTRUE;
}

static int wait_transfer(void *context)
{
    (void)context;
    if (!s_in_flight) return ESP_OK;
    if (xSemaphoreTake(s_transfer_done, pdMS_TO_TICKS(1000)) != pdTRUE) {
        // Keep the in-flight flag: never overwrite a buffer still owned by DMA.
        return ESP_ERR_TIMEOUT;
    }
    s_in_flight = false;
    return ESP_OK;
}

static int write_strip(void *context, unsigned y, unsigned rows, const uint8_t *pixels, size_t bytes)
{
    (void)context;
    const unsigned x_end = PANEL_X_OFFSET + DISPLAY_FRAME_WIDTH - 1;
    const unsigned y_end = y + rows - 1;
    const uint8_t columns[] = {0, PANEL_X_OFFSET, x_end >> 8, x_end & 0xFF};
    const uint8_t lines[] = {y >> 8, y & 0xFF, y_end >> 8, y_end & 0xFF};
    esp_err_t result = esp_lcd_panel_io_tx_param(s_io, PANEL_COMMAND(0x2A), columns, sizeof(columns));
    if (result != ESP_OK) return result;
    result = esp_lcd_panel_io_tx_param(s_io, PANEL_COMMAND(0x2B), lines, sizeof(lines));
    if (result != ESP_OK) return result;
    s_in_flight = true;
    result = esp_lcd_panel_io_tx_color(s_io, PANEL_PIXELS_COMMAND, pixels, bytes);
    if (result != ESP_OK) s_in_flight = false;
    return result;
}

static void flush_frame(lv_display_t *display, const lv_area_t *area, uint8_t *pixels)
{
    const display_frame_io_t io = {.wait = wait_transfer, .write = write_strip};
    esp_err_t result = ESP_ERR_INVALID_SIZE;
    if (area->x1 == 0 && area->y1 == 0
        && area->x2 == DISPLAY_FRAME_WIDTH - 1 && area->y2 == DISPLAY_FRAME_HEIGHT - 1) {
        result = display_frame_send((const uint16_t *)pixels, s_staging, &io);
    }
    if (result != ESP_OK && !s_redraw_needed) {
        ESP_LOGE(TAG, "Frame transfer failed: %s; complete redraw scheduled", esp_err_to_name(result));
    }
    if (result == ESP_OK && s_redraw_needed) ESP_LOGI(TAG, "Complete display redraw recovered");
    s_redraw_needed = result != ESP_OK;
    // The full LVGL image is separate from the permanent DMA staging buffer,
    // so it is safe to release it even when a failed DMA transfer is pending.
    lv_display_flush_ready(display);
}

static void retry_redraw(lv_timer_t *timer)
{
    (void)timer;
    if (s_redraw_needed) lv_obj_invalidate(lv_screen_active());
}

static void wake_display(lv_event_t *event)
{
    (void)event;
    lvgl_port_task_wake(LVGL_PORT_EVENT_DISPLAY, NULL);
}

lv_display_t *attention_display_start(void)
{
    // Reserve DMA memory before Wi-Fi and USB/audio tasks consume internal RAM.
    // The complete image lives in PSRAM; SPI never needs a frame-sized bounce allocation.
    s_staging = heap_caps_aligned_alloc(4, DISPLAY_FRAME_STRIP_BYTES, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
    uint8_t *frame = heap_caps_aligned_alloc(4, DISPLAY_FRAME_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    s_transfer_done = xSemaphoreCreateBinary();
    if (s_staging == NULL || frame == NULL || s_transfer_done == NULL) {
        ESP_LOGE(TAG, "Could not allocate frame buffers");
        heap_caps_free(s_staging);
        heap_caps_free(frame);
        if (s_transfer_done != NULL) vSemaphoreDelete(s_transfer_done);
        return NULL;
    }

    esp_lcd_panel_handle_t panel = NULL;
    const bsp_display_config_t panel_config = {.max_transfer_sz = DISPLAY_FRAME_STRIP_BYTES};
    ESP_ERROR_CHECK(bsp_display_new(&panel_config, &panel, &s_io));
    const esp_lcd_panel_io_callbacks_t callbacks = {.on_color_trans_done = transfer_done};
    ESP_ERROR_CHECK(esp_lcd_panel_io_register_event_callbacks(s_io, &callbacks, NULL));
    ESP_ERROR_CHECK(bsp_display_brightness_init());

    const lvgl_port_cfg_t port_config = ESP_LVGL_PORT_INIT_CONFIG();
    ESP_ERROR_CHECK(lvgl_port_init(&port_config));
    lvgl_port_lock(0);
    lv_display_t *display = lv_display_create(DISPLAY_FRAME_WIDTH, DISPLAY_FRAME_HEIGHT);
    assert(display != NULL);
    lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(display, frame, NULL, DISPLAY_FRAME_BYTES, LV_DISPLAY_RENDER_MODE_FULL);
    lv_display_set_flush_cb(display, flush_frame);
    lv_display_add_event_cb(display, wake_display, LV_EVENT_REFR_REQUEST, NULL);
    lv_timer_t *retry_timer = lv_timer_create(retry_redraw, 250, NULL);
    ESP_ERROR_CHECK(retry_timer != NULL ? ESP_OK : ESP_ERR_NO_MEM);
    lvgl_port_unlock();

    esp_lcd_touch_handle_t touch = NULL;
    ESP_ERROR_CHECK(bsp_touch_new(NULL, &touch));
    const lvgl_port_touch_cfg_t touch_config = {.disp = display, .handle = touch};
    lv_indev_t *input = lvgl_port_add_touch(&touch_config);
    ESP_ERROR_CHECK(input != NULL ? ESP_OK : ESP_ERR_NO_MEM);
    ESP_LOGI(TAG, "Full-frame display: %u bytes PSRAM, %u bytes reserved DMA staging", DISPLAY_FRAME_BYTES, DISPLAY_FRAME_STRIP_BYTES);
    return display;
}
