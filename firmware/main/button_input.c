#include "button_input.h"

#include <stdint.h>
#include "bsp/esp-bsp.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define BOOT_BUTTON_GPIO GPIO_NUM_0
#define BOOT_DEBOUNCE_MS 35
#define BUTTON_LONG_PRESS_MS 1000
#define PWR_POLL_MS 50

#define AXP2101_ADDRESS 0x34
#define AXP2101_INTEN2 0x41
#define AXP2101_INTSTS2 0x49
#define AXP2101_IRQ_LEVEL 0x27
#define AXP2101_PKEY_SHORT_MASK (1U << 3)
#define AXP2101_PKEY_LONG_MASK (1U << 2)

static const char *TAG = "buttons";
static i2c_master_dev_handle_t s_pmu = NULL;
static bool s_boot_candidate_pressed = false;
static bool s_boot_stable_pressed = false;
static TickType_t s_boot_candidate_since = 0;
static TickType_t s_boot_pressed_since = 0;
static bool s_boot_long_sent = false;
static TickType_t s_last_pwr_poll = 0;

static esp_err_t pmu_read(uint8_t reg, uint8_t *value)
{
    if (s_pmu == NULL || value == NULL) return ESP_ERR_INVALID_STATE;
    return i2c_master_transmit_receive(s_pmu, &reg, 1, value, 1, 100);
}

static esp_err_t pmu_write(uint8_t reg, uint8_t value)
{
    if (s_pmu == NULL) return ESP_ERR_INVALID_STATE;
    const uint8_t payload[2] = { reg, value };
    return i2c_master_transmit(s_pmu, payload, sizeof(payload), 100);
}

static void init_pwr_button(void)
{
    i2c_master_bus_handle_t bus = bsp_i2c_get_handle();
    if (bus == NULL) {
        ESP_LOGW(TAG, "PWR button unavailable: BSP I2C bus is missing");
        return;
    }

    const i2c_device_config_t device = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = AXP2101_ADDRESS,
        .scl_speed_hz = 400000,
        .scl_wait_us = 0,
        .flags = {
            .disable_ack_check = 0,
        },
    };
    esp_err_t result = i2c_master_bus_add_device(bus, &device, &s_pmu);
    if (result != ESP_OK) {
        s_pmu = NULL;
        ESP_LOGW(TAG, "PWR button unavailable: AXP2101 add failed: %s", esp_err_to_name(result));
        return;
    }

    uint8_t enabled = 0;
    result = pmu_read(AXP2101_INTEN2, &enabled);
    const uint8_t button_interrupts = AXP2101_PKEY_SHORT_MASK | AXP2101_PKEY_LONG_MASK;
    if (result == ESP_OK) result = pmu_write(AXP2101_INTEN2, enabled | button_interrupts);
    if (result == ESP_OK) result = pmu_write(AXP2101_INTSTS2, button_interrupts);
    uint8_t irq_level = 0;
    if (result == ESP_OK) result = pmu_read(AXP2101_IRQ_LEVEL, &irq_level);
    // REG 27 bits 5:4 select the long-press IRQ threshold. 00 is one second;
    // preserve ONLEVEL, OFFLEVEL, and every unrelated PMIC setting.
    if (result == ESP_OK) result = pmu_write(AXP2101_IRQ_LEVEL, irq_level & (uint8_t)~0x30U);
    if (result != ESP_OK) {
        ESP_LOGW(TAG, "Could not initialize AXP2101 PKEY status: %s", esp_err_to_name(result));
    } else {
        ESP_LOGI(TAG, "PWR short/one-second-long input ready");
    }
}

esp_err_t button_input_init(void)
{
    const gpio_config_t boot_config = {
        .pin_bit_mask = 1ULL << BOOT_BUTTON_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t result = gpio_config(&boot_config);
    if (result != ESP_OK) return result;

    s_boot_candidate_pressed = gpio_get_level(BOOT_BUTTON_GPIO) == 0;
    s_boot_stable_pressed = s_boot_candidate_pressed;
    s_boot_candidate_since = xTaskGetTickCount();
    s_boot_pressed_since = s_boot_candidate_since;
    s_boot_long_sent = false;
    s_last_pwr_poll = s_boot_candidate_since;

    init_pwr_button();
    ESP_LOGI(TAG, "BOOT next-thread input ready on GPIO0");
    return ESP_OK;
}

bool button_input_poll(button_input_event_t *event)
{
    if (event == NULL) return false;
    *event = BUTTON_INPUT_NONE;

    const TickType_t now = xTaskGetTickCount();
    const bool boot_pressed = gpio_get_level(BOOT_BUTTON_GPIO) == 0;
    if (boot_pressed != s_boot_candidate_pressed) {
        s_boot_candidate_pressed = boot_pressed;
        s_boot_candidate_since = now;
    } else if (boot_pressed != s_boot_stable_pressed
        && (now - s_boot_candidate_since) >= pdMS_TO_TICKS(BOOT_DEBOUNCE_MS)) {
        s_boot_stable_pressed = boot_pressed;
        if (boot_pressed) {
            s_boot_pressed_since = now;
            s_boot_long_sent = false;
        } else if (!s_boot_long_sent) {
            *event = BUTTON_INPUT_BOOT_SHORT;
            return true;
        }
    }

    if (s_boot_stable_pressed && !s_boot_long_sent
        && (now - s_boot_pressed_since) >= pdMS_TO_TICKS(BUTTON_LONG_PRESS_MS)) {
        s_boot_long_sent = true;
        *event = BUTTON_INPUT_BOOT_LONG;
        return true;
    }

    if (s_pmu != NULL && (now - s_last_pwr_poll) >= pdMS_TO_TICKS(PWR_POLL_MS)) {
        s_last_pwr_poll = now;
        uint8_t status = 0;
        if (pmu_read(AXP2101_INTSTS2, &status) == ESP_OK) {
            const uint8_t button_status = status
                & (AXP2101_PKEY_SHORT_MASK | AXP2101_PKEY_LONG_MASK);
            if (button_status != 0) (void)pmu_write(AXP2101_INTSTS2, button_status);
            if ((button_status & AXP2101_PKEY_LONG_MASK) != 0) {
                *event = BUTTON_INPUT_PWR_LONG;
                return true;
            }
            if ((button_status & AXP2101_PKEY_SHORT_MASK) != 0) {
                *event = BUTTON_INPUT_PWR_SHORT;
                return true;
            }
        }
    }

    return false;
}
