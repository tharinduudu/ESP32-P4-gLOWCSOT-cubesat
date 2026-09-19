#include "app_common.h"

// Read a little-endian unsigned 16-bit value from a BME280 calibration block.
static uint16_t u16_le(const uint8_t *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

// Read a little-endian signed 16-bit value from a BME280 calibration block.
static int16_t s16_le(const uint8_t *p)
{
    return (int16_t)u16_le(p);
}

// Bring up the shared I2C bus used by the BME280 and the Oct-2025 DAC.
esp_err_t init_i2c_bus(void)
{
    // The BME280 and Oct-2025 DAC share this I2C bus. Initialize it lazily so
    // either subsystem can come up first without caring about boot order.
    if (s_i2c_ready) {
        return ESP_OK;
    }

    i2c_config_t cfg = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = PIN_I2C_SDA,
        .scl_io_num = PIN_I2C_SCL,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = BME280_I2C_HZ,
    };
    ESP_RETURN_ON_ERROR(i2c_param_config(BME280_I2C_PORT, &cfg), TAG, "configure I2C");
    esp_err_t ret = i2c_driver_install(BME280_I2C_PORT, I2C_MODE_MASTER, 0, 0, 0);
    if (ret != ESP_ERR_INVALID_STATE) {
        ESP_RETURN_ON_ERROR(ret, TAG, "install I2C driver");
    }
    s_i2c_ready = true;
    return ESP_OK;
}

// Read one or more bytes from a BME280 register.
static esp_err_t bme280_read_reg(uint8_t reg, uint8_t *data, size_t len)
{
    return i2c_master_write_read_device(BME280_I2C_PORT, s_bme280_addr, &reg, 1, data, len, pdMS_TO_TICKS(100));
}

// Write a single BME280 configuration register.
static esp_err_t bme280_write_reg(uint8_t reg, uint8_t value)
{
    uint8_t data[2] = {reg, value};
    return i2c_master_write_to_device(BME280_I2C_PORT, s_bme280_addr, data, sizeof(data), pdMS_TO_TICKS(100));
}

// Check one possible BME280 I2C address and remember it if the chip ID matches.
static esp_err_t bme280_probe_addr(uint8_t addr)
{
    uint8_t reg = 0xD0;
    uint8_t id = 0;
    esp_err_t ret = i2c_master_write_read_device(BME280_I2C_PORT, addr, &reg, 1, &id, 1, pdMS_TO_TICKS(100));
    if (ret == ESP_OK && id == 0x60) {
        s_bme280_addr = addr;
        return ESP_OK;
    }
    return ret == ESP_OK ? ESP_ERR_NOT_FOUND : ret;
}

// Load factory calibration coefficients from the BME280 so raw ADC values can
// be compensated into real temperature, pressure, and humidity.
static esp_err_t bme280_load_calibration(void)
{
    uint8_t calib1[26];
    uint8_t calib2[7];
    ESP_RETURN_ON_ERROR(bme280_read_reg(0x88, calib1, sizeof(calib1)), TAG, "read BME280 calibration 1");
    ESP_RETURN_ON_ERROR(bme280_read_reg(0xE1, calib2, sizeof(calib2)), TAG, "read BME280 calibration 2");

    // Bosch stores the calibration coefficients packed in little-endian fields.
    // Keep the unpacking close to the datasheet so it is easy to audit.
    s_bme280_calib.dig_T1 = u16_le(&calib1[0]);
    s_bme280_calib.dig_T2 = s16_le(&calib1[2]);
    s_bme280_calib.dig_T3 = s16_le(&calib1[4]);
    s_bme280_calib.dig_P1 = u16_le(&calib1[6]);
    s_bme280_calib.dig_P2 = s16_le(&calib1[8]);
    s_bme280_calib.dig_P3 = s16_le(&calib1[10]);
    s_bme280_calib.dig_P4 = s16_le(&calib1[12]);
    s_bme280_calib.dig_P5 = s16_le(&calib1[14]);
    s_bme280_calib.dig_P6 = s16_le(&calib1[16]);
    s_bme280_calib.dig_P7 = s16_le(&calib1[18]);
    s_bme280_calib.dig_P8 = s16_le(&calib1[20]);
    s_bme280_calib.dig_P9 = s16_le(&calib1[22]);
    s_bme280_calib.dig_H1 = calib1[25];
    s_bme280_calib.dig_H2 = s16_le(&calib2[0]);
    s_bme280_calib.dig_H3 = calib2[2];
    s_bme280_calib.dig_H4 = (int16_t)(((int16_t)calib2[3] << 4) | (calib2[4] & 0x0F));
    s_bme280_calib.dig_H5 = (int16_t)(((int16_t)calib2[5] << 4) | (calib2[4] >> 4));
    s_bme280_calib.dig_H6 = (int8_t)calib2[6];
    return ESP_OK;
}

// Detect and configure the BME280 in forced mode with high oversampling.
esp_err_t init_bme280(void)
{
    ESP_RETURN_ON_ERROR(init_i2c_bus(), TAG, "init I2C for BME280");

    esp_err_t ret = bme280_probe_addr(BME280_ADDR_PRIMARY);
    if (ret != ESP_OK) {
        ret = bme280_probe_addr(BME280_ADDR_SECONDARY);
    }
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "BME280 not found on I2C GPIO%d/GPIO%d", PIN_I2C_SDA, PIN_I2C_SCL);
        return ret;
    }

    ESP_RETURN_ON_ERROR(bme280_write_reg(0xE0, 0xB6), TAG, "reset BME280");
    vTaskDelay(pdMS_TO_TICKS(5));
    ESP_RETURN_ON_ERROR(bme280_load_calibration(), TAG, "load BME280 calibration");
    ESP_RETURN_ON_ERROR(bme280_write_reg(0xF2, 0x05), TAG, "set BME280 humidity oversampling");
    ESP_RETURN_ON_ERROR(bme280_write_reg(0xF5, 0x10), TAG, "set BME280 filter");
    s_bme280_ok = true;
    ESP_LOGI(TAG, "BME280 ready at 0x%02x, forced mode x16 oversampling", s_bme280_addr);
    return ESP_OK;
}

// Trigger one forced BME280 measurement and convert the raw result into
// engineering units using the stored calibration coefficients.
static esp_err_t bme280_read_forced(bme280_reading_t *out)
{
    if (!s_bme280_ok) {
        return ESP_ERR_INVALID_STATE;
    }
    ESP_RETURN_ON_ERROR(bme280_write_reg(0xF4, (0x05 << 5) | (0x05 << 2) | 0x01), TAG, "trigger BME280 forced read");
    vTaskDelay(pdMS_TO_TICKS(125));

    uint8_t raw[8];
    ESP_RETURN_ON_ERROR(bme280_read_reg(0xF7, raw, sizeof(raw)), TAG, "read BME280 raw data");
    int32_t adc_P = ((int32_t)raw[0] << 12) | ((int32_t)raw[1] << 4) | (raw[2] >> 4);
    int32_t adc_T = ((int32_t)raw[3] << 12) | ((int32_t)raw[4] << 4) | (raw[5] >> 4);
    int32_t adc_H = ((int32_t)raw[6] << 8) | raw[7];
    if (adc_T == 0x80000 || adc_P == 0x80000 || adc_H == 0x8000) {
        return ESP_ERR_INVALID_RESPONSE;
    }

    double var1 = (((double)adc_T / 16384.0) - ((double)s_bme280_calib.dig_T1 / 1024.0)) * (double)s_bme280_calib.dig_T2;
    // Floating point is slower, but the BME path runs every 10 seconds and the
    // formulas are much easier to review than fixed-point compensation code.
    double var2 = ((((double)adc_T / 131072.0) - ((double)s_bme280_calib.dig_T1 / 8192.0)) *
                   (((double)adc_T / 131072.0) - ((double)s_bme280_calib.dig_T1 / 8192.0))) *
                  (double)s_bme280_calib.dig_T3;
    s_bme280_calib.t_fine = var1 + var2;
    double temp_c = s_bme280_calib.t_fine / 5120.0;

    var1 = (s_bme280_calib.t_fine / 2.0) - 64000.0;
    var2 = var1 * var1 * (double)s_bme280_calib.dig_P6 / 32768.0;
    var2 = var2 + var1 * (double)s_bme280_calib.dig_P5 * 2.0;
    var2 = (var2 / 4.0) + ((double)s_bme280_calib.dig_P4 * 65536.0);
    var1 = (((double)s_bme280_calib.dig_P3 * var1 * var1 / 524288.0) +
            ((double)s_bme280_calib.dig_P2 * var1)) / 524288.0;
    var1 = (1.0 + var1 / 32768.0) * (double)s_bme280_calib.dig_P1;
    if (var1 == 0.0) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    double pressure = 1048576.0 - (double)adc_P;
    pressure = ((pressure - (var2 / 4096.0)) * 6250.0) / var1;
    var1 = (double)s_bme280_calib.dig_P9 * pressure * pressure / 2147483648.0;
    var2 = pressure * (double)s_bme280_calib.dig_P8 / 32768.0;
    pressure = pressure + (var1 + var2 + (double)s_bme280_calib.dig_P7) / 16.0;

    double humidity = s_bme280_calib.t_fine - 76800.0;
    humidity = (adc_H - ((double)s_bme280_calib.dig_H4 * 64.0 + (double)s_bme280_calib.dig_H5 / 16384.0 * humidity)) *
               ((double)s_bme280_calib.dig_H2 / 65536.0 *
                (1.0 + (double)s_bme280_calib.dig_H6 / 67108864.0 * humidity *
                 (1.0 + (double)s_bme280_calib.dig_H3 / 67108864.0 * humidity)));
    humidity = humidity * (1.0 - (double)s_bme280_calib.dig_H1 * humidity / 524288.0);
    if (humidity > 100.0) {
        humidity = 100.0;
    } else if (humidity < 0.0) {
        humidity = 0.0;
    }

    out->valid = true;
    out->temp_c = temp_c;
    out->pressure_hpa = pressure / 100.0;
    out->humidity_pct = humidity;
    return ESP_OK;
}

#if READOUT_PROFILE_OCT2025
// Clamp a floating-point value into a closed range.
static double clamp_double(double value, double lo, double hi)
{
    if (value < lo) {
        return lo;
    }
    if (value > hi) {
        return hi;
    }
    return value;
}

// Clamp an integer value into a closed range.
static int clamp_int(int value, int lo, int hi)
{
    if (value < lo) {
        return lo;
    }
    if (value > hi) {
        return hi;
    }
    return value;
}

// Convert a stored 10-bit DAC code into the low-side voltage used by the
// temperature compensation calculation.
static double dac_code_to_vlow(uint16_t code)
{
    double frac = (double)(code & 0x03ff) / 1023.0;
    double value = TEMP_COMP_DAC_VOFF + TEMP_COMP_DAC_SPAN * frac;
    return value < 0.0 ? 0.0 : value;
}

// Convert the desired low-side voltage back to the 10-bit code used throughout
// the firmware and web API.
static uint16_t dac_vlow_to_code(double vlow)
{
    double clipped = vlow < 0.0 ? 0.0 : vlow;
    int code = (int)lrint(((clipped - TEMP_COMP_DAC_VOFF) / TEMP_COMP_DAC_SPAN) * 1023.0);
    return (uint16_t)clamp_int(code, 0, 1023);
}
#endif

// Apply a slow temperature correction to the SiPM bias DACs after a stable
// BME280 averaging window.
static void temp_compensate_dac(double temp_avg_c, double temp_std_c, uint32_t samples)
{
#if READOUT_PROFILE_OCT2025
    // Only nudge the SiPM bias after a stable five-minute temperature window.
    // A bad sensor read should not kick the detector threshold around.
    if (samples < TEMP_COMP_MIN_SAMPLES) {
        ESP_LOGW(TAG, "temp compensation skipped: only %" PRIu32 " BME samples", samples);
        return;
    }
    if (temp_std_c > TEMP_COMP_MAX_STD_C) {
        ESP_LOGW(TAG, "temp compensation skipped: temp std %.3f C too high", temp_std_c);
        return;
    }

    double dtemp = clamp_double(temp_avg_c - TEMP_COMP_REF_C, -TEMP_COMP_MAX_DT_C, TEMP_COMP_MAX_DT_C);
    double dvlow = -TEMP_COMP_V_PER_C * dtemp;

    for (uint8_t ch = 0; ch < 4; ch++) {
        uint16_t prev;
        portENTER_CRITICAL(&s_state_mux);
        prev = s_dac_codes[ch];
        portEXIT_CRITICAL(&s_state_mux);

        double ref_vlow = dac_code_to_vlow(STARTUP_DAC_CODE);
        uint16_t absolute_target = dac_vlow_to_code(ref_vlow + dvlow);
        int delta = (int)absolute_target - (int)prev;
        // Bound each correction block so temperature compensation cannot make
        // a large jump while the detector is running unattended.
        delta = clamp_int(delta, -TEMP_COMP_MAX_CODES_PER_BLOCK, TEMP_COMP_MAX_CODES_PER_BLOCK);
        uint16_t target = (uint16_t)clamp_int((int)prev + delta, 0, 1023);

        if (abs((int)target - (int)prev) < TEMP_COMP_MIN_STEP_CODES) {
            continue;
        }
        if (fabs(dac_code_to_vlow(target) - dac_code_to_vlow(prev)) < TEMP_COMP_MIN_STEP_V) {
            continue;
        }

        esp_err_t ret = dac_set_channel(ch, target);
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "temp compensation CH%u: T=%.3fC dT=%.3fC 0x%03x -> 0x%03x",
                     ch, temp_avg_c, dtemp, prev, target);
        } else {
            ESP_LOGW(TAG, "temp compensation CH%u failed: %s", ch, esp_err_to_name(ret));
        }
        vTaskDelay(pdMS_TO_TICKS(500));
    }
#else
    (void)temp_avg_c;
    (void)temp_std_c;
    (void)samples;
#endif
}

// Periodically sample the BME280, publish the latest reading for the web page,
// and write five-minute averages to the environment file.
void bme280_task(void *arg)
{
    (void)arg;
    double temp_sum = 0.0;
    double temp_sq_sum = 0.0;
    double pressure_sum = 0.0;
    double humidity_sum = 0.0;
    uint32_t samples = 0;
    int64_t window_start_ms = esp_timer_get_time() / 1000;

    while (true) {
        // Forced mode keeps the sensor asleep between samples and gives a clean
        // point measurement for the five-minute average file.
        bme280_reading_t reading = {0};
        esp_err_t ret = bme280_read_forced(&reading);
        if (ret == ESP_OK && reading.valid) {
            portENTER_CRITICAL(&s_state_mux);
            s_bme280_latest = reading;
            portEXIT_CRITICAL(&s_state_mux);
            temp_sum += reading.temp_c;
            temp_sq_sum += reading.temp_c * reading.temp_c;
            pressure_sum += reading.pressure_hpa;
            humidity_sum += reading.humidity_pct;
            samples++;
        } else {
            ESP_LOGW(TAG, "BME280 read failed: %s", esp_err_to_name(ret));
        }

        int64_t now_ms = esp_timer_get_time() / 1000;
        if (now_ms - window_start_ms >= BME280_AVG_PERIOD_MS) {
            if (samples > 0) {
                double inv = 1.0 / (double)samples;
                double temp_avg = temp_sum * inv;
                double temp_var = (temp_sq_sum * inv) - (temp_avg * temp_avg);
                double temp_std = temp_var > 0.0 ? sqrt(temp_var) : 0.0;
                sd_append_env_average(time(NULL), samples, temp_avg, pressure_sum * inv, humidity_sum * inv);
                ESP_LOGI(TAG, "environment 5 min avg: n=%" PRIu32 " temp=%.3fC pressure=%.3fhPa humidity=%.3f%%",
                         samples, temp_avg, pressure_sum * inv, humidity_sum * inv);
                temp_compensate_dac(temp_avg, temp_std, samples);
            }
            temp_sum = 0.0;
            temp_sq_sum = 0.0;
            pressure_sum = 0.0;
            humidity_sum = 0.0;
            samples = 0;
            window_start_ms = now_ms;
        }

        vTaskDelay(pdMS_TO_TICKS(BME280_SAMPLE_MS));
    }
}
