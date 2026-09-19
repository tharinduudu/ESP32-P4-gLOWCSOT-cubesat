idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES bt esp_lcd esp_driver_gpio esp_driver_spi esp_pm esp_timer nvs_flash
)
