#pragma once

#include <stdbool.h>
#include "esp_err.h"

typedef enum {
    BUTTON_INPUT_NONE = 0,
    BUTTON_INPUT_NEXT,
    BUTTON_INPUT_SELECT,
} button_input_event_t;

esp_err_t button_input_init(void);
bool button_input_poll(button_input_event_t *event);
