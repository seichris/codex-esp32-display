#include "voice_audio.h"

#include <stdatomic.h>
#include <string.h>
#include "bsp/esp-bsp.h"
#include "driver/i2s_std.h"
#include "esp_codec_dev.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#define MICROPHONE_GAIN_DB 30.0F
#define CAPTURE_FRAME_BYTES 1920U
#define CAPTURE_RING_FRAMES 10U
#define CAPTURE_TASK_STACK 6144U
#define CAPTURE_TASK_PRIORITY 8U

typedef struct {
    uint8_t pcm[CAPTURE_FRAME_BYTES];
} capture_frame_t;

static const char *TAG = "voice_audio";
static esp_codec_dev_handle_t s_microphone;
static QueueHandle_t s_capture_queue;
static SemaphoreHandle_t s_queue_lock;
static SemaphoreHandle_t s_codec_lock;
static TaskHandle_t s_capture_task;
static atomic_bool s_listening;
static atomic_bool s_host_muted;
static atomic_bool s_capture_overflow;
static atomic_int s_source;
static capture_frame_t s_usb_pending;
static size_t s_usb_pending_offset;
static bool s_usb_pending_valid;

static void reset_capture_queue(void)
{
    if (s_capture_queue == NULL || s_queue_lock == NULL) return;
    if (xSemaphoreTake(s_queue_lock, pdMS_TO_TICKS(20)) == pdTRUE) {
        xQueueReset(s_capture_queue);
        s_usb_pending_offset = 0;
        s_usb_pending_valid = false;
        xSemaphoreGive(s_queue_lock);
    }
}

static bool receive_frame(capture_frame_t *frame, TickType_t timeout, voice_audio_source_t source)
{
    if (frame == NULL || s_capture_queue == NULL || s_queue_lock == NULL) return false;
    if (xSemaphoreTake(s_queue_lock, timeout) != pdTRUE) return false;
    if (!atomic_load(&s_listening) || atomic_load(&s_source) != source) {
        xSemaphoreGive(s_queue_lock);
        return false;
    }
    const bool received = xQueueReceive(s_capture_queue, frame, 0) == pdTRUE;
    xSemaphoreGive(s_queue_lock);
    return received;
}

static void capture_task(void *argument)
{
    (void)argument;
    capture_frame_t frame;
    while (true) {
        const int source_at_read = atomic_load(&s_source);
        if (s_microphone == NULL || s_codec_lock == NULL
            || xSemaphoreTake(s_codec_lock, portMAX_DELAY) != pdTRUE) {
            vTaskDelay(pdMS_TO_TICKS(5));
            continue;
        }
        const int codec_result = esp_codec_dev_read(s_microphone, frame.pcm, (int)sizeof(frame.pcm));
        xSemaphoreGive(s_codec_lock);
        if (codec_result != ESP_CODEC_DEV_OK) {
            vTaskDelay(pdMS_TO_TICKS(5));
            continue;
        }
        if (!atomic_load(&s_listening)) {
            reset_capture_queue();
            continue;
        }
        if (s_capture_queue == NULL || s_queue_lock == NULL
            || xSemaphoreTake(s_queue_lock, pdMS_TO_TICKS(20)) != pdTRUE) {
            atomic_store(&s_capture_overflow, true);
            continue;
        }
        const bool still_selected = atomic_load(&s_listening)
            && atomic_load(&s_source) == source_at_read;
        const bool queued = still_selected
            && xQueueSend(s_capture_queue, &frame, 0) == pdTRUE;
        xSemaphoreGive(s_queue_lock);
        if (still_selected && !queued) atomic_store(&s_capture_overflow, true);
    }
}

esp_err_t voice_audio_init(void)
{
    const i2s_std_config_t i2s_config = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(VOICE_AUDIO_SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIP_SLOT_DEFAULT_CONFIG(
            I2S_DATA_BIT_WIDTH_16BIT,
            I2S_SLOT_MODE_MONO
        ),
        .gpio_cfg = {
            .mclk = BSP_I2S_MCLK,
            .bclk = BSP_I2S_SCLK,
            .ws = BSP_I2S_LCLK,
            .dout = BSP_I2S_DOUT,
            .din = BSP_I2S_DSIN,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };
    esp_err_t result = bsp_audio_init(&i2s_config);
    if (result != ESP_OK) return result;

    s_microphone = bsp_audio_codec_microphone_init();
    if (s_microphone == NULL) return ESP_ERR_NOT_FOUND;

    esp_codec_dev_sample_info_t format = {
        .bits_per_sample = 16,
        .channel = 1,
        .channel_mask = 0,
        .sample_rate = VOICE_AUDIO_SAMPLE_RATE,
        .mclk_multiple = 0,
    };
    int codec_result = esp_codec_dev_open(s_microphone, &format);
    if (codec_result != ESP_CODEC_DEV_OK) return ESP_FAIL;
    codec_result = esp_codec_dev_set_in_gain(s_microphone, MICROPHONE_GAIN_DB);
    if (codec_result != ESP_CODEC_DEV_OK) {
        ESP_LOGW(TAG, "Could not set microphone gain: %d", codec_result);
    }

    s_capture_queue = xQueueCreate(CAPTURE_RING_FRAMES, sizeof(capture_frame_t));
    s_queue_lock = xSemaphoreCreateMutex();
    s_codec_lock = xSemaphoreCreateMutex();
    if (s_capture_queue == NULL || s_queue_lock == NULL || s_codec_lock == NULL) return ESP_ERR_NO_MEM;
    atomic_store(&s_listening, false);
    atomic_store(&s_host_muted, false);
    atomic_store(&s_capture_overflow, false);
    atomic_store(&s_source, VOICE_AUDIO_SOURCE_USB);
    s_usb_pending_offset = 0;
    s_usb_pending_valid = false;
    if (xTaskCreate(capture_task, "voice_capture", CAPTURE_TASK_STACK, NULL,
                    CAPTURE_TASK_PRIORITY, &s_capture_task) != pdPASS) {
        s_capture_task = NULL;
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "ES7210 microphone ready at %u Hz; one-reader capture ring online", VOICE_AUDIO_SAMPLE_RATE);
    return ESP_OK;
}

esp_err_t voice_audio_read(uint8_t *buffer, size_t length, size_t *bytes_read)
{
    if (buffer == NULL || bytes_read == NULL) return ESP_ERR_INVALID_ARG;
    *bytes_read = length;
    if (s_microphone == NULL || !atomic_load(&s_listening)
        || atomic_load(&s_source) != VOICE_AUDIO_SOURCE_USB) {
        memset(buffer, 0, length);
        return s_microphone == NULL ? ESP_ERR_INVALID_STATE : ESP_OK;
    }

    if (s_capture_queue == NULL || s_queue_lock == NULL
        || xSemaphoreTake(s_queue_lock, pdMS_TO_TICKS(20)) != pdTRUE) {
        memset(buffer, 0, length);
        return ESP_OK;
    }
    if (!atomic_load(&s_listening) || atomic_load(&s_source) != VOICE_AUDIO_SOURCE_USB) {
        memset(buffer, 0, length);
        xSemaphoreGive(s_queue_lock);
        return ESP_OK;
    }
    size_t offset = 0;
    while (offset < length) {
        if (!s_usb_pending_valid || s_usb_pending_offset >= sizeof(s_usb_pending.pcm)) {
            s_usb_pending_offset = 0;
            s_usb_pending_valid = xQueueReceive(s_capture_queue, &s_usb_pending, 0) == pdTRUE;
            if (!s_usb_pending_valid) {
                memset(buffer + offset, 0, length - offset);
                break;
            }
        }
        const size_t available = sizeof(s_usb_pending.pcm) - s_usb_pending_offset;
        const size_t amount = available < length - offset ? available : length - offset;
        memcpy(buffer + offset, s_usb_pending.pcm + s_usb_pending_offset, amount);
        s_usb_pending_offset += amount;
        offset += amount;
    }
    xSemaphoreGive(s_queue_lock);
    if (atomic_load(&s_host_muted)) memset(buffer, 0, length);
    return ESP_OK;
}

esp_err_t voice_audio_wireless_read_frame(uint8_t *buffer, size_t length, size_t *bytes_read)
{
    if (buffer == NULL || bytes_read == NULL) return ESP_ERR_INVALID_ARG;
    *bytes_read = 0;
    if (length < CAPTURE_FRAME_BYTES || !atomic_load(&s_listening)
        || atomic_load(&s_source) != VOICE_AUDIO_SOURCE_WIFI) {
        if (length > 0) memset(buffer, 0, length < CAPTURE_FRAME_BYTES ? length : CAPTURE_FRAME_BYTES);
        return ESP_ERR_INVALID_STATE;
    }
    capture_frame_t frame;
    if (!receive_frame(&frame, pdMS_TO_TICKS(30), VOICE_AUDIO_SOURCE_WIFI)) {
        memset(buffer, 0, CAPTURE_FRAME_BYTES);
        *bytes_read = CAPTURE_FRAME_BYTES;
        return ESP_ERR_TIMEOUT;
    }
    memcpy(buffer, frame.pcm, CAPTURE_FRAME_BYTES);
    *bytes_read = CAPTURE_FRAME_BYTES;
    return ESP_OK;
}

void voice_audio_set_listening(bool listening)
{
    const bool was_listening = atomic_exchange(&s_listening, listening);
    if (!listening || !was_listening) reset_capture_queue();
    if (!listening) atomic_store(&s_capture_overflow, false);
}

void voice_audio_set_host_muted(bool muted)
{
    atomic_store(&s_host_muted, muted);
}

void voice_audio_set_source(voice_audio_source_t source)
{
    if (source != VOICE_AUDIO_SOURCE_USB && source != VOICE_AUDIO_SOURCE_WIFI) return;
    const int previous = atomic_exchange(&s_source, source);
    if (previous != source) reset_capture_queue();
}

voice_audio_source_t voice_audio_source(void)
{
    return (voice_audio_source_t)atomic_load(&s_source);
}

bool voice_audio_take_overflow(void)
{
    return atomic_exchange(&s_capture_overflow, false);
}

bool voice_audio_is_listening(void)
{
    if (!atomic_load(&s_listening)) return false;
    return voice_audio_source() == VOICE_AUDIO_SOURCE_WIFI || !atomic_load(&s_host_muted);
}

bool voice_audio_try_lock_codec(uint32_t timeout_ms)
{
    return s_codec_lock != NULL
        && xSemaphoreTake(s_codec_lock, pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
}

void voice_audio_unlock_codec(void)
{
    if (s_codec_lock != NULL) xSemaphoreGive(s_codec_lock);
}
