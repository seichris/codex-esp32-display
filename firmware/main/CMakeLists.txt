idf_component_register(
    SRCS
        "main.c"
        "wifi_manager.c"
        "attention_client.c"
        "attention_ui.c"
        "button_input.c"
    INCLUDE_DIRS "."
    REQUIRES
        esp_driver_gpio
        esp_driver_i2c
        esp_event
        esp_http_client
        esp_netif
        esp_wifi
        json
        nvs_flash
)

target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror)
