#include "app_common.h"

// Prepare non-volatile storage for Wi-Fi/ESP-IDF internals, recovering from
// stale NVS pages if the flash layout changed between builds.
static esp_err_t init_nvs(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_RETURN_ON_ERROR(nvs_flash_erase(), TAG, "erase NVS");
        ret = nvs_flash_init();
    }
    return ret;
}

// The USB-JTAG console can print logs with only the ROM path, but stdin needs
// the real driver installed before fgets() can receive bench commands.
static void init_usb_console(void)
{
#if CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
    usb_serial_jtag_vfs_set_rx_line_endings(ESP_LINE_ENDINGS_CR);
    usb_serial_jtag_vfs_set_tx_line_endings(ESP_LINE_ENDINGS_CRLF);
    fcntl(fileno(stdout), F_SETFL, 0);
    fcntl(fileno(stdin), F_SETFL, 0);

    usb_serial_jtag_driver_config_t config = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
    esp_err_t ret = usb_serial_jtag_driver_install(&config);
    if (ret == ESP_OK || ret == ESP_ERR_INVALID_STATE) {
        usb_serial_jtag_vfs_use_driver();
    } else {
        ESP_LOGW(TAG, "USB console RX disabled: %s", esp_err_to_name(ret));
    }
#endif
}

// Top-level boot choreography for the detector. This keeps dangerous hardware
// transitions in one readable order and starts background tasks only afterward.
void app_main(void)
{
    init_usb_console();
    ESP_LOGI(TAG, "gLOWCOST MPPC ESP32-P4 Wi-Fi readout");
    ESP_LOGW(TAG, "startup sequence: HV off, FPGA flash, DAC init, then HV 0x%02x after %d ms settle", STARTUP_HV_BYTE, HV_SETTLE_MS);

    ESP_ERROR_CHECK(init_nvs());
    s_run_start_uptime_ms = esp_timer_get_time() / 1000;
    s_run_start_epoch = time(NULL);

    // Bring the detector up in the same order a cautious operator would use:
    // make HV safe first, then expose the setup page while heavier startup work
    // continues. The web UI can show progress instead of making the operator
    // stare at a missing AP during FPGA/SD/HV bring-up.
    ESP_ERROR_CHECK(configure_high_gpio_rail());
    ESP_ERROR_CHECK(init_control_gpios());
    ESP_ERROR_CHECK(init_spi());
    esp_err_t ret = hv_write_and_settle(0x00);
    ESP_LOGW(TAG, "startup HV off result: %s", esp_err_to_name(ret));
    ESP_ERROR_CHECK(init_counters());

    ESP_ERROR_CHECK(start_wifi_ap());
    ESP_ERROR_CHECK(start_webserver());
    xTaskCreatePinnedToCore(sd_startup_task, "sd_startup_task", 4096, NULL, 4, NULL, 1);
    xTaskCreatePinnedToCore(console_task, "console_task", 4096, NULL, 4, NULL, 1);

    ret = program_fpga();
    ESP_LOGW(TAG, "FPGA program result: %s", esp_err_to_name(ret));
    ret = dac_zero_channels();
    ESP_LOGW(TAG, "DAC zero result: %s", esp_err_to_name(ret));
    if (ret == ESP_OK) {
        ret = hv_write_and_settle(STARTUP_HV_BYTE);
        ESP_LOGW(TAG, "startup HV enable result: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGW(TAG, "startup HV remains off because DAC init failed");
    }

    xTaskCreatePinnedToCore(counter_task, "counter_task", 4096, NULL, 8, NULL, 0);
    xTaskCreatePinnedToCore(bme280_startup_task, "bme_startup_task", 4096, NULL, 5, NULL, 1);

    // BLE starts after the hosted Wi-Fi path is alive and the detector hardware
    // is configured, so the quick display sees meaningful status bits.
    ret = init_ble_broadcast();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "BLE display broadcast disabled: %s", esp_err_to_name(ret));
    }
    xTaskCreatePinnedToCore(auto_power_save_task, "auto_power_save_task", 3072, NULL, 3, NULL, 1);
}
