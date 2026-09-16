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

// Top-level boot choreography for the detector. This keeps dangerous hardware
// transitions in one readable order and starts background tasks only afterward.
void app_main(void)
{
    ESP_LOGI(TAG, "gLOWCOST MPPC ESP32-P4 Wi-Fi readout");
    ESP_LOGW(TAG, "startup sequence: HV off, FPGA flash, DAC init, then HV 0x%02x after %d ms settle", STARTUP_HV_BYTE, HV_SETTLE_MS);

    // Bring the detector up in the same order a cautious operator would use:
    // make HV safe first, configure the FPGA/DAC, then enable bias and wait.
    ESP_ERROR_CHECK(init_nvs());
    ESP_ERROR_CHECK(configure_high_gpio_rail());
    ESP_ERROR_CHECK(init_control_gpios());
    ESP_ERROR_CHECK(init_spi());
    esp_err_t ret = hv_write_and_settle(0x00);
    ESP_LOGW(TAG, "startup HV off result: %s", esp_err_to_name(ret));
    ESP_ERROR_CHECK(init_counters());

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

    // Run time starts after the hardware is in its operating state. If browser
    // time arrives later, storage.c renames the active files to the real time.
    s_run_start_uptime_ms = esp_timer_get_time() / 1000;
    s_run_start_epoch = time(NULL);
    ret = init_sd_card();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SD logging disabled: %s", esp_err_to_name(ret));
    }
    ret = init_bme280();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "environment logging disabled until BME280 is detected: %s", esp_err_to_name(ret));
    }

    xTaskCreatePinnedToCore(counter_task, "counter_task", 4096, NULL, 8, NULL, 0);
    if (s_bme280_ok) {
        xTaskCreatePinnedToCore(bme280_task, "bme280_task", 4096, NULL, 5, NULL, 1);
    }
    xTaskCreatePinnedToCore(console_task, "console_task", 4096, NULL, 4, NULL, 1);

    // Wi-Fi starts last so radio work does not overlap the sensitive FPGA/DAC/HV
    // startup sequence.
    ESP_ERROR_CHECK(start_wifi_ap());
    ESP_ERROR_CHECK(start_webserver());
    xTaskCreatePinnedToCore(auto_power_save_task, "auto_power_save_task", 3072, NULL, 3, NULL, 1);
}
