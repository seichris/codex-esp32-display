#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#define VOICE_AUDIO_SAMPLE_RATE 48000U

esp_err_t voice_audio_init(void);
esp_err_t voice_audio_read(uint8_t *buffer, size_t length, size_t *bytes_read);
void voice_audio_set_listening(bool listening);
void voice_audio_set_host_muted(bool muted);
bool voice_audio_is_listening(void);
