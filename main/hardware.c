#include "app_common.h"

// Take the shared SPI mutex before talking to FPGA, HV, or legacy SPI DAC.
static void spi_lock(void)
{
    if (s_spi_mutex) {
        xSemaphoreTake(s_spi_mutex, portMAX_DELAY);
    }
}

// Release the shared SPI mutex after a short peripheral transaction.
static void spi_unlock(void)
{
    if (s_spi_mutex) {
        xSemaphoreGive(s_spi_mutex);
    }
}

static void stop_fpga_clock(void);

// Enable the ESP32-P4 IO rail needed by the high-numbered pins used for the
// readout board.
esp_err_t configure_high_gpio_rail(void)
{
    // GPIO46/GPIO48 live on the high-voltage-capable IO rail on the P4 module.
    // Without this, the HV chip select line can look fine in software but not
    // actually swing to the level the readout board expects.
    esp_ldo_channel_config_t ldo_cfg = {
        .chan_id = 4,
        .voltage_mv = 3300,
        .flags = {
            .adjustable = 1,
        },
    };
    esp_err_t ret = esp_ldo_acquire_channel(&ldo_cfg, &s_ldo_vo4);
    if (ret == ESP_ERR_INVALID_STATE || ret == ESP_ERR_NOT_FOUND) {
        ESP_LOGW(TAG, "LDO_VO4 not acquired (%s); continuing", esp_err_to_name(ret));
        return ESP_OK;
    }
    ESP_RETURN_ON_ERROR(ret, TAG, "set LDO_VO4 to 3.3V");
    ESP_LOGI(TAG, "LDO_VO4 set to 3.3V for GPIO46/GPIO48 domain");
    return ESP_OK;
}

// Start the FPGA clock at either the programming frequency or the Oct-2025
// runtime frequency.
static esp_err_t start_fpga_clock_hz(uint32_t hz)
{
    if (s_fpga_clock_on) {
        stop_fpga_clock();
    }

#if READOUT_PROFILE_OCT2025
    if (hz == FPGA_RUNTIME_CLK_HZ) {
        // The Oct-2025 FPGA image expects a real runtime clock after flashing.
        // LEDC is fine for the programming clock, but CLKOUT is cleaner here.
        esp_err_t ret = esp_clock_output_start(CLKOUT_SIG_CPLL, PIN_FPGA_CLK, &s_fpga_clkout);
        ESP_RETURN_ON_ERROR(ret, TAG, "route CPLL to FPGA clock pin");
        ret = esp_clock_output_set_divider(s_fpga_clkout, 8);
        if (ret != ESP_OK) {
            esp_clock_output_stop(s_fpga_clkout);
            s_fpga_clkout = NULL;
            ESP_RETURN_ON_ERROR(ret, TAG, "set FPGA runtime clock divider");
        }
        s_fpga_clock_on = true;
        s_fpga_clock_is_clkout = true;
        ESP_LOGI(TAG, "FPGA runtime clock requested on GPIO%d using CPLL/8 for ~50 MHz", PIN_FPGA_CLK);
        return ESP_OK;
    }
#endif

    ledc_timer_config_t timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_1_BIT,
        .timer_num = LEDC_TIMER_0,
        .freq_hz = hz,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_RETURN_ON_ERROR(ledc_timer_config(&timer), TAG, "configure FPGA clock");

    ledc_channel_config_t channel = {
        .gpio_num = PIN_FPGA_CLK,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_0,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = LEDC_TIMER_0,
        .duty = 1,
        .hpoint = 0,
    };
    ESP_RETURN_ON_ERROR(ledc_channel_config(&channel), TAG, "start FPGA clock output");
    s_fpga_clock_on = true;
    s_fpga_clock_is_clkout = false;
    ESP_LOGI(TAG, "FPGA clock started on GPIO%d at %" PRIu32 " Hz", PIN_FPGA_CLK, hz);
    return ESP_OK;
}

// Start the programming clock used while shifting the iCE40 bitstream.
static esp_err_t start_fpga_clock(void)
{
    return start_fpga_clock_hz(FPGA_CLK_HZ);
}

// Stop whatever FPGA clock source is currently active.
static void stop_fpga_clock(void)
{
    if (!s_fpga_clock_on) {
        return;
    }
    if (s_fpga_clock_is_clkout) {
        esp_err_t ret = esp_clock_output_stop(s_fpga_clkout);
        if (ret == ESP_OK) {
            s_fpga_clock_on = false;
            s_fpga_clock_is_clkout = false;
            s_fpga_clkout = NULL;
            ESP_LOGI(TAG, "FPGA clock-output stopped");
        } else {
            ESP_LOGW(TAG, "FPGA clock-output stop failed: %s", esp_err_to_name(ret));
        }
        return;
    }
    esp_err_t ret = ledc_stop(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 0);
    if (ret == ESP_OK) {
        s_fpga_clock_on = false;
        ESP_LOGI(TAG, "FPGA clock stopped to save power");
    } else {
        ESP_LOGW(TAG, "FPGA clock stop failed: %s", esp_err_to_name(ret));
    }
}

// Send a blocking SPI transaction to one device on the shared bus.
static esp_err_t spi_send(spi_device_handle_t dev, const uint8_t *data, size_t len)
{
    spi_transaction_t transaction = {
        .length = len * 8,
        .tx_buffer = data,
    };
    return spi_device_transmit(dev, &transaction);
}

// Configure the Pi-header SPI bus and attach the FPGA/HV devices used by this
// readout profile.
esp_err_t init_spi(void)
{
    s_spi_mutex = xSemaphoreCreateMutex();
    ESP_RETURN_ON_FALSE(s_spi_mutex, ESP_ERR_NO_MEM, TAG, "create SPI mutex");

    // FPGA programming and HV control share the Pi-header SPI pins. Serialize
    // transfers so a web-triggered HV/DAC action cannot collide with reflash.
    spi_bus_config_t buscfg = {
        .mosi_io_num = PIN_SPI_MOSI,
        .miso_io_num = PIN_SPI_MISO,
        .sclk_io_num = PIN_SPI_SCLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 8192,
    };
    ESP_RETURN_ON_ERROR(spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO), TAG, "init SPI bus");

    spi_device_interface_config_t fpga_cfg = {
        .clock_speed_hz = FPGA_SPI_HZ,
        .mode = FPGA_SPI_MODE,
        .spics_io_num = -1,
        .queue_size = 1,
    };
    ESP_RETURN_ON_ERROR(spi_bus_add_device(SPI2_HOST, &fpga_cfg, &s_fpga), TAG, "add FPGA SPI device");

#if !READOUT_PROFILE_OCT2025
    spi_device_interface_config_t dac_cfg = {
        .clock_speed_hz = DAC_SPI_HZ,
        .mode = 1,
        .spics_io_num = PIN_CS_DAC,
        .queue_size = 1,
    };
    ESP_RETURN_ON_ERROR(spi_bus_add_device(SPI2_HOST, &dac_cfg, &s_dac), TAG, "add DAC SPI device");
#endif

    spi_device_interface_config_t hv_cfg = {
        .clock_speed_hz = HV_SPI_HZ,
        .mode = 0,
        .spics_io_num = PIN_CS_HV,
        .queue_size = 1,
    };
    ESP_RETURN_ON_ERROR(spi_bus_add_device(SPI2_HOST, &hv_cfg, &s_hv), TAG, "add MAX1932 SPI device");
    return ESP_OK;
}

// Set up FPGA reset, chip-select, and DONE pins before any programming attempt.
esp_err_t init_control_gpios(void)
{
    gpio_config_t outputs = {
        .pin_bit_mask = (1ULL << PIN_FPGA_RST) | (1ULL << PIN_CS_FPGA),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&outputs), TAG, "configure FPGA reset");
    gpio_set_level(PIN_FPGA_RST, 0);
    gpio_set_level(PIN_CS_FPGA, 1);

    gpio_config_t done = {
        .pin_bit_mask = (1ULL << PIN_FPGA_DONE),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&done), TAG, "configure FPGA DONE");
    return ESP_OK;
}

// Put the iCE40 into a clean SPI-configuration state while the caller owns the
// SPI bus.
static esp_err_t ice40_clear_locked(void)
{
    // iCE40 SPI configuration starts with CRESET low while CS is held low, then
    // releases reset and waits before the bitstream is clocked in.
    spi_device_acquire_bus(s_fpga, portMAX_DELAY);
    gpio_set_level(PIN_CS_FPGA, 0);
    gpio_set_level(PIN_FPGA_RST, 0);
    esp_rom_delay_us(200);
    gpio_set_level(PIN_FPGA_RST, 1);
    esp_rom_delay_us(1200);
    gpio_set_level(PIN_CS_FPGA, 1);
    spi_device_release_bus(s_fpga);
    return ESP_OK;
}

// Clock the embedded bitstream into the iCE40 and verify that DONE rises.
esp_err_t program_fpga(void)
{
    const size_t embedded_len = fpga_bin_end - fpga_bin_start;
#if READOUT_PROFILE_OCT2025
    const size_t bit_len = embedded_len;
#else
    const size_t bit_len = embedded_len < ICE40_PI_BITSTREAM_LEN ? embedded_len : ICE40_PI_BITSTREAM_LEN;
#endif
    ESP_LOGI(TAG, "programming iCE40: embedded=%u bytes, sending=%u bytes", (unsigned)embedded_len, (unsigned)bit_len);
    ESP_RETURN_ON_ERROR(start_fpga_clock(), TAG, "start FPGA clock for programming");

    // Keep the programming transaction as one uninterrupted SPI sequence. If
    // this is broken up, DONE may stay low even though most bytes were sent.
    spi_lock();
    esp_err_t ret = ice40_clear_locked();
    if (ret == ESP_OK) {
        uint8_t dummy = 0xAA;
        spi_device_acquire_bus(s_fpga, portMAX_DELAY);
        gpio_set_level(PIN_CS_FPGA, 1);
        ret = spi_send(s_fpga, &dummy, 1);
        if (ret == ESP_OK) {
            gpio_set_level(PIN_CS_FPGA, 0);
            ret = spi_send(s_fpga, fpga_bin_start, bit_len);
        }
        gpio_set_level(PIN_CS_FPGA, 1);
        if (ret == ESP_OK) {
            uint8_t trailing[20] = {0};
            ret = spi_send(s_fpga, trailing, sizeof(trailing));
        }
        spi_device_release_bus(s_fpga);
    }
    spi_unlock();
    if (ret != ESP_OK) {
        gpio_set_level(PIN_FPGA_RST, 0);
        stop_fpga_clock();
        ESP_RETURN_ON_ERROR(ret, TAG, "send FPGA bitstream");
    }

    int64_t deadline = esp_timer_get_time() + 1000000;
    while (!gpio_get_level(PIN_FPGA_DONE) && esp_timer_get_time() < deadline) {
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    if (!gpio_get_level(PIN_FPGA_DONE)) {
        s_fpga_ok = false;
        gpio_set_level(PIN_FPGA_RST, 0);
        stop_fpga_clock();
        return ESP_ERR_TIMEOUT;
    }
    s_fpga_ok = true;
    ESP_LOGW(TAG, "iCE40 DONE is high");
#if READOUT_PROFILE_OCT2025
    esp_err_t clock_ret = start_fpga_clock_hz(FPGA_RUNTIME_CLK_HZ);
    if (clock_ret != ESP_OK) {
        ESP_LOGW(TAG, "50 MHz FPGA runtime clock failed: %s", esp_err_to_name(clock_ret));
    }
#else
    stop_fpga_clock();
#endif
    return ESP_OK;
}

#if !READOUT_PROFILE_OCT2025
// Write a register on the older SPI DAC profile.
static esp_err_t dac_write(uint8_t reg, uint16_t value)
{
    uint8_t data[3] = {reg, (uint8_t)(value >> 8), (uint8_t)value};
    spi_lock();
    esp_err_t ret = spi_send(s_dac, data, sizeof(data));
    spi_unlock();
    return ret;
}
#endif

// Write one channel on the Oct-2025 I2C DAC, accepting the same 10-bit value
// scale used by the older firmware.
static esp_err_t dacx578_write_channel(uint8_t ch, uint16_t code10)
{
    ESP_RETURN_ON_ERROR(init_i2c_bus(), TAG, "init I2C for DACx578");
    if (ch > 7 || code10 > 0x3FF) {
        return ESP_ERR_INVALID_ARG;
    }

    // The legacy control path and web UI use 10-bit codes. Scale to the
    // DACx578's 12-bit left-aligned data format at the last possible moment.
    uint16_t raw12 = (uint16_t)(((uint32_t)code10 * 4095U + 511U) / 1023U);
    uint16_t aligned = raw12 << 4;
    uint8_t data[3] = {
        (uint8_t)(0x30 | (ch & 0x0F)),
        (uint8_t)(aligned >> 8),
        (uint8_t)aligned,
    };
    return i2c_master_write_to_device(BME280_I2C_PORT, DACX578_ADDR, data, sizeof(data), pdMS_TO_TICKS(100));
}

// Set one DAC channel and mirror the value in RAM for status and compensation.
esp_err_t dac_set_channel(uint8_t ch, uint16_t value)
{
    esp_err_t ret;
#if READOUT_PROFILE_OCT2025
    ret = dacx578_write_channel(ch, value);
#else
    ret = dac_write(DAC60508_DAC0 + ch, value);
#endif
    if (ret == ESP_OK && ch < 8) {
        portENTER_CRITICAL(&s_state_mux);
        s_dac_codes[ch] = value & 0x03ff;
        portEXIT_CRITICAL(&s_state_mux);
    }
    return ret;
}

// Load the startup DAC values for SiPM bias and threshold channels.
esp_err_t dac_zero_channels(void)
{
#if READOUT_PROFILE_OCT2025
    ESP_RETURN_ON_ERROR(init_i2c_bus(), TAG, "init I2C for DACx578");
    uint8_t reset[3] = {0x50, 0x00, 0x00};
    esp_err_t ret = i2c_master_write_to_device(BME280_I2C_PORT, DACX578_ADDR, reset, sizeof(reset), pdMS_TO_TICKS(100));
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "DACx578 reset failed: %s", esp_err_to_name(ret));
    }
    uint8_t config[2] = {0x10, 0x00};
    ret = i2c_master_write_to_device(BME280_I2C_PORT, DACX578_ADDR, config, sizeof(config), pdMS_TO_TICKS(100));
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "DACx578 external-reference config failed: %s", esp_err_to_name(ret));
    }
    for (uint8_t ch = 0; ch < 4; ch++) {
        ESP_RETURN_ON_ERROR(dacx578_write_channel(ch, STARTUP_DAC_CODE), TAG, "set DACx578 startup channel");
    }
    for (uint8_t ch = 4; ch < 8; ch++) {
        ESP_RETURN_ON_ERROR(dacx578_write_channel(ch, STARTUP_DAC_THRESHOLD_CODE), TAG, "set DACx578 threshold channel");
    }
    ESP_LOGI(TAG, "DACx578 channels 0-3 set to 0x%03x, channels 4-7 set to 0x%03x",
             STARTUP_DAC_CODE, STARTUP_DAC_THRESHOLD_CODE);
    return ESP_OK;
#else
    ESP_RETURN_ON_ERROR(dac_write(DAC60508_NOP, 0), TAG, "DAC NOP");
    for (uint8_t ch = 0; ch < 4; ch++) {
        ESP_RETURN_ON_ERROR(dac_set_channel(ch, STARTUP_DAC_CODE), TAG, "set DAC channel");
    }
    ESP_LOGI(TAG, "DAC channels 0-3 set to 0x%04x", STARTUP_DAC_CODE);
    return ESP_OK;
#endif
}

// Send the raw control byte to the MAX1932 HV controller and update status.
esp_err_t hv_write_byte(uint8_t value)
{
    spi_lock();
    esp_err_t ret = spi_send(s_hv, &value, 1);
    spi_unlock();
    ESP_RETURN_ON_ERROR(ret, TAG, "write MAX1932 byte");
    portENTER_CRITICAL(&s_state_mux);
    s_hv_byte = value;
    portEXIT_CRITICAL(&s_state_mux);
    ESP_LOGW(TAG, "MAX1932/HV byte set to 0x%02x", value);
    return ESP_OK;
}

// Clear only the live interrupt counters. Historical minute records are left
// alone.
void clear_live_counts(void)
{
    portENTER_CRITICAL(&s_count_mux);
    for (size_t i = 0; i < COUNT_CHANNELS; i++) {
        s_counts[i] = 0;
    }
    portEXIT_CRITICAL(&s_count_mux);
}

// Read the current counting gate under the shared state lock.
bool counting_is_enabled(void)
{
    bool enabled;
    portENTER_CRITICAL(&s_state_mux);
    enabled = s_counting_enabled;
    portEXIT_CRITICAL(&s_state_mux);
    return enabled;
}

// Change HV safely and enforce the quiet settling interval before counting is
// allowed again.
esp_err_t hv_write_and_settle(uint8_t value)
{
    // Counts taken while the bias is moving are mostly noise, so every HV change
    // gates counting off, clears the live counters, and waits before recording.
    portENTER_CRITICAL(&s_state_mux);
    s_counting_enabled = false;
    s_hv_settle_until_ms = 0;
    portEXIT_CRITICAL(&s_state_mux);
    clear_live_counts();

    ESP_RETURN_ON_ERROR(hv_write_byte(value), TAG, "write HV byte");
    if (value == 0) {
        clear_live_counts();
        ESP_LOGW(TAG, "HV is off; counting disabled");
        return ESP_OK;
    }

    portENTER_CRITICAL(&s_state_mux);
    s_hv_settle_until_ms = (esp_timer_get_time() / 1000) + HV_SETTLE_MS;
    portEXIT_CRITICAL(&s_state_mux);
    ESP_LOGW(TAG, "waiting %d ms for HV to settle before counting", HV_SETTLE_MS);
    vTaskDelay(pdMS_TO_TICKS(HV_SETTLE_MS));
    clear_live_counts();
    portENTER_CRITICAL(&s_state_mux);
    s_hv_settle_until_ms = 0;
    s_counting_enabled = true;
    portEXIT_CRITICAL(&s_state_mux);
    ESP_LOGI(TAG, "HV settled; counters cleared and counting enabled");
    return ESP_OK;
}
