#include "voice_audio.h"

#include <stdatomic.h>
#include <string.h>
#include "bsp/esp-bsp.h"
#include "driver/i2s_std.h"
#include "esp_codec_dev.h"
#include "esp_log.h"

#define MICROPHONE_GAIN_DB 30.0F

static const char *TAG = "voice_audio";
static esp_codec_dev_handle_t s_microphone;
static atomic_bool s_listening;
static atomic_bool s_host_muted;

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

    atomic_store(&s_listening, false);
    atomic_store(&s_host_muted, false);
    ESP_LOGI(TAG, "ES7210 microphone ready at %u Hz; PCM gate closed", VOICE_AUDIO_SAMPLE_RATE);
    return ESP_OK;
}

esp_err_t voice_audio_read(uint8_t *buffer, size_t length, size_t *bytes_read)
{
    if (buffer == NULL || bytes_read == NULL) return ESP_ERR_INVALID_ARG;
    *bytes_read = length;
    if (s_microphone == NULL) {
        memset(buffer, 0, length);
        return ESP_ERR_INVALID_STATE;
    }

    const int result = esp_codec_dev_read(s_microphone, buffer, (int)length);
    if (result != ESP_CODEC_DEV_OK) {
        memset(buffer, 0, length);
        return ESP_FAIL;
    }
    if (!atomic_load(&s_listening) || atomic_load(&s_host_muted)) {
        memset(buffer, 0, length);
    }
    return ESP_OK;
}

void voice_audio_set_listening(bool listening)
{
    atomic_store(&s_listening, listening);
}

void voice_audio_set_host_muted(bool muted)
{
    atomic_store(&s_host_muted, muted);
}

bool voice_audio_is_listening(void)
{
    return atomic_load(&s_listening) && !atomic_load(&s_host_muted);
}
