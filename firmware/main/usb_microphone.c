#include "usb_microphone.h"

#include <string.h>
#include "esp_log.h"
#include "usb_device_uac.h"
#include "voice_audio.h"

static const char *TAG = "usb_microphone";

static esp_err_t microphone_input(
    uint8_t *buffer,
    size_t length,
    size_t *bytes_read,
    void *context
)
{
    (void)context;
    esp_err_t result = voice_audio_read(buffer, length, bytes_read);
    if (result != ESP_OK) {
        memset(buffer, 0, length);
        *bytes_read = length;
    }
    return ESP_OK;
}
static void host_mute_changed(uint32_t muted, void *context)
{
    (void)context;
    voice_audio_set_host_muted(muted != 0);
}

esp_err_t usb_microphone_init(void)
{
    uac_device_config_t config = {
        .skip_tinyusb_init = false,
        .output_cb = NULL,
        .input_cb = microphone_input,
        .set_mute_cb = host_mute_changed,
        .set_volume_cb = NULL,
        .cb_ctx = NULL,
    };
    esp_err_t result = uac_device_init(&config);
    if (result == ESP_OK) {
        ESP_LOGI(TAG, "USB Audio Class microphone started with PCM gate closed");
    }
    return result;
}
