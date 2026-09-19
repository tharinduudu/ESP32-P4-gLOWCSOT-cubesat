#pragma once

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <inttypes.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#include "driver/gpio.h"
#include "driver/i2c.h"
#include "driver/ledc.h"
#include "driver/sdspi_host.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_ldo_regulator.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_pm.h"
#include "esp_timer.h"
#include "esp_vfs_fat.h"
#include "esp_wifi.h"
#include "esp_clock_output.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "sdmmc_cmd.h"

// Waveshare ESP32-P4-Module-DEV-KIT 40-pin header mapping, matched to the Pi
// physical pins used by the gLOWCOST MPPC interface schematic.
#define PIN_I2C_SDA             GPIO_NUM_7   // Pi physical 3
#define PIN_I2C_SCL             GPIO_NUM_8   // Pi physical 5
#define PIN_FPGA_CLK            GPIO_NUM_23  // Pi physical 7 / BCM4_GPCLK0
#define PIN_COUNT_CH12          GPIO_NUM_21  // Pi physical 11 / BCM17
#define PIN_COUNT_CH02          GPIO_NUM_22  // Pi physical 12 / BCM18
#define PIN_COUNT_CH01          GPIO_NUM_20  // Pi physical 13 / BCM27
#define PIN_FPGA_RST            GPIO_NUM_6   // Pi physical 15 / BCM22
#define PIN_FPGA_DONE           GPIO_NUM_5   // Pi physical 16 / BCM23
#define PIN_SPI_MOSI            GPIO_NUM_3   // Pi physical 19 / BCM10_MOSI
#define PIN_SPI_MISO            GPIO_NUM_2   // Pi physical 21 / BCM9_MISO
#define PIN_SPI_SCLK            GPIO_NUM_0   // Pi physical 23 / BCM11_SCLK
#define PIN_CS_FPGA             GPIO_NUM_4   // Pi physical 18 / BCM24
#define PIN_CS_DAC              GPIO_NUM_1   // Pi physical 22 / BCM25
#define PIN_COUNT_CH012         GPIO_NUM_1   // Pi physical 22 / BCM25, Oct-2025 triple coincidence
#define PIN_COUNT_GPIO5         GPIO_NUM_33  // Pi physical 29 / BCM5
#define PIN_COUNT_GPIO6         GPIO_NUM_26  // Pi physical 31 / BCM6
#define PIN_CS_HV               GPIO_NUM_48  // Pi physical 33 / BCM13_PWM1
#define PIN_COUNT_GPIO16        GPIO_NUM_46  // Pi physical 36 / BCM16
#define PIN_SD_SPI_MISO         GPIO_NUM_39  // Onboard microSD D0
#define PIN_SD_SPI_D1           GPIO_NUM_40  // Onboard microSD D1, unused in SPI mode
#define PIN_SD_SPI_D2           GPIO_NUM_41  // Onboard microSD D2, unused in SPI mode
#define PIN_SD_SPI_CS           GPIO_NUM_42  // Onboard microSD D3 / CS in SPI mode
#define PIN_SD_SPI_SCLK         GPIO_NUM_43  // Onboard microSD CLK
#define PIN_SD_SPI_MOSI         GPIO_NUM_44  // Onboard microSD CMD / MOSI in SPI mode

#define READOUT_PROFILE_OCT2025 0

// The Oct-2025 PCB moved the threshold DAC onto I2C and expects a faster
// FPGA configuration path than the older Pi HAT setup.
#if READOUT_PROFILE_OCT2025
#define FPGA_SPI_HZ             (4 * 1000 * 1000)
#define FPGA_SPI_MODE           0
#else
#define FPGA_SPI_HZ             (1000 * 1000)
#define FPGA_SPI_MODE           3
#endif
#define DAC_SPI_HZ              (500 * 1000)
#define HV_SPI_HZ               (1000 * 1000)
#define SD_SPI_HZ               (10000)
#define FPGA_CLK_HZ             (9600 * 1000)
#define FPGA_RUNTIME_CLK_HZ     (50 * 1000 * 1000)
#define COUNTER_PERIOD_MS       60000
#define HV_SETTLE_MS            10000
#define BME280_SAMPLE_MS        10000
#define BME280_AVG_PERIOD_MS    (5 * 60 * 1000)
#define BME280_I2C_PORT         I2C_NUM_0
#define BME280_I2C_HZ           100000
#define BME280_ADDR_PRIMARY     0x76
#define BME280_ADDR_SECONDARY   0x77
#define TEMP_COMP_REF_C         20.0
#define TEMP_COMP_V_PER_C       0.054
#define TEMP_COMP_DAC_VOFF      0.0005
#define TEMP_COMP_DAC_SPAN      2.9968
#define TEMP_COMP_MIN_STEP_CODES 5
#define TEMP_COMP_MIN_STEP_V    0.012
#define TEMP_COMP_MAX_DT_C      10.0
#define TEMP_COMP_MAX_CODES_PER_BLOCK 120
#define TEMP_COMP_MAX_STD_C     0.20
#define TEMP_COMP_MIN_SAMPLES   24
#define ICE40_PI_BITSTREAM_LEN  0x1CA6
#if READOUT_PROFILE_OCT2025
#define COUNT_CHANNELS          7
// These are the latest bench-tested startup values. The threshold is usable
// for field checks, but waveform analysis is still needed before final tuning.
#define STARTUP_HV_BYTE         0xEA
#define STARTUP_DAC_CODE        0x2F1
#define STARTUP_DAC_THRESHOLD_CODE 0x070
#define DACX578_ADDR            0x47
#else
#define COUNT_CHANNELS          6
#define STARTUP_HV_BYTE         0xE5
#define STARTUP_DAC_CODE        0x000
#endif
#ifndef DACX578_ADDR
#define DACX578_ADDR            0x47
#endif
#ifndef STARTUP_DAC_THRESHOLD_CODE
#define STARTUP_DAC_THRESHOLD_CODE STARTUP_DAC_CODE
#endif

#define LOG_RECORDS             256
#define SD_MOUNT_POINT          "/sdcard"
#define RUN_LABEL_MAX           32
#define SD_LOG_NAME_MAX         80

#define DAC60508_NOP            0x00
#define DAC60508_DAC0           0x08

#define WIFI_AP_SSID            "MuonReadout"
#define WIFI_AP_PASS            "glowcost"
#define WIFI_AP_CHANNEL         6
#define WIFI_AP_MAX_CONN        4
#define WIFI_AUTO_OFF_MS        (7 * 1000)
#define FIELD_MIN_CPU_FREQ_MHZ  40
#define BLE_ADV_INTERVAL_MS     1000
#define BLE_PAYLOAD_UPDATE_MS   3000
#define BLE_COUNT_SLOTS         7
#define BLE_COMPANY_ID          0xFFFF
#define BLE_PAYLOAD_VERSION     1
#define BLE_DEVICE_NAME         "MuonP4"

extern const uint8_t fpga_bin_start[] asm("_binary_fpga_bin_start");
extern const uint8_t fpga_bin_end[] asm("_binary_fpga_bin_end");

// A minute record is kept in RAM for the live web page and also written to SD.
// Uptime is not written to the SD CSV, but keeping it here is useful while the
// detector waits for browser time sync.
typedef struct {
    int64_t uptime_ms;
    time_t epoch;
    uint32_t counts[COUNT_CHANNELS];
} count_record_t;

typedef struct {
    bool valid;
    double temp_c;
    double pressure_hpa;
    double humidity_pct;
} bme280_reading_t;

typedef struct {
    uint16_t dig_T1;
    int16_t dig_T2;
    int16_t dig_T3;
    uint16_t dig_P1;
    int16_t dig_P2;
    int16_t dig_P3;
    int16_t dig_P4;
    int16_t dig_P5;
    int16_t dig_P6;
    int16_t dig_P7;
    int16_t dig_P8;
    int16_t dig_P9;
    uint8_t dig_H1;
    int16_t dig_H2;
    uint8_t dig_H3;
    int16_t dig_H4;
    int16_t dig_H5;
    int8_t dig_H6;
    double t_fine;
} bme280_calib_t;

// Shared state is deliberately centralized. The detector tasks are split into
// modules, but the ESP32 still behaves like one small firmware image, not a
// large service with ownership layers.
extern const char *TAG;
extern spi_device_handle_t s_fpga;
#if !READOUT_PROFILE_OCT2025
extern spi_device_handle_t s_dac;
#endif
extern spi_device_handle_t s_hv;
extern esp_ldo_channel_handle_t s_ldo_vo4;
extern SemaphoreHandle_t s_spi_mutex;
extern SemaphoreHandle_t s_sd_mutex;
extern httpd_handle_t s_httpd;
extern esp_netif_t *s_ap_netif;
extern sdmmc_card_t *s_sd_card;
extern portMUX_TYPE s_count_mux;
extern portMUX_TYPE s_state_mux;
extern volatile uint32_t s_counts[COUNT_CHANNELS];
extern uint32_t s_last_counts[COUNT_CHANNELS];
extern uint32_t s_live_counts[COUNT_CHANNELS];
extern uint64_t s_totals[COUNT_CHANNELS];
extern count_record_t s_log[LOG_RECORDS];
extern size_t s_log_head;
extern size_t s_log_count;
extern uint8_t s_hv_byte;
extern volatile bool s_counting_enabled;
extern int64_t s_hv_settle_until_ms;
extern bool s_fpga_ok;
extern bool s_time_set;
extern bool s_sd_mounted;
extern bool s_power_save_mode;
extern bool s_wifi_keep_on;
extern bool s_fpga_clock_on;
extern bool s_fpga_clock_is_clkout;
extern esp_clock_output_mapping_handle_t s_fpga_clkout;
extern uint8_t s_wifi_clients;
extern uint16_t s_dac_codes[8];
extern int64_t s_run_start_uptime_ms;
extern time_t s_run_start_epoch;
extern char s_run_label[RUN_LABEL_MAX + 1];
extern char s_log_path[128];
extern char s_env_log_path[128];
extern bme280_calib_t s_bme280_calib;
extern uint8_t s_bme280_addr;
extern bool s_bme280_ok;
extern bme280_reading_t s_bme280_latest;
extern bool s_i2c_ready;
extern const gpio_num_t s_count_pins[COUNT_CHANNELS];
extern const char *s_count_names[COUNT_CHANNELS];

esp_err_t configure_high_gpio_rail(void);
esp_err_t init_control_gpios(void);
esp_err_t init_spi(void);
esp_err_t init_i2c_bus(void);
esp_err_t program_fpga(void);
esp_err_t dac_set_channel(uint8_t ch, uint16_t value);
esp_err_t dac_zero_channels(void);
esp_err_t hv_write_byte(uint8_t value);
esp_err_t hv_write_and_settle(uint8_t value);
void clear_live_counts(void);
bool counting_is_enabled(void);
esp_err_t init_counters(void);
void snapshot_counts(uint32_t out[COUNT_CHANNELS], bool reset);
void print_and_reset_counts(void);
void counter_task(void *arg);
void console_task(void *arg);
void sanitize_run_label(const char *input, char *output, size_t output_len);
void url_decode_in_place(char *s);
void csv_time_string(time_t epoch, char *out, size_t out_len);
void count_csv_header(char *out, size_t out_len);
esp_err_t sd_refresh_log_path_locked(void);
esp_err_t sd_refresh_log_path(void);
esp_err_t init_sd_card(void);
void sd_append_record(const count_record_t *record);
void sd_append_env_average(time_t epoch, uint32_t samples, double temp_c, double pressure_hpa, double humidity_pct);
esp_err_t init_bme280(void);
void bme280_task(void *arg);
esp_err_t init_ble_broadcast(void);
esp_err_t start_wifi_ap(void);
esp_err_t start_webserver(void);
void auto_power_save_task(void *arg);
