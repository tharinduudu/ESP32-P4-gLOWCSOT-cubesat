idf_component_register(
    SRCS
        "main.c"
        "app_state.c"
        "hardware.c"
        "counters.c"
        "storage.c"
        "environment.c"
        "console.c"
        "ble_broadcast.c"
        "web.c"
    INCLUDE_DIRS "."
    REQUIRES bt driver fatfs sdmmc esp_driver_sdspi esp_driver_gpio esp_driver_i2c esp_driver_ledc esp_driver_spi esp_http_server esp_wifi esp_netif esp_event esp_timer esp_hw_support nvs_flash
    EMBED_FILES "fpga.bin"
)
