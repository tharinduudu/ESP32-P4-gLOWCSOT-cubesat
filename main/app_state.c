#include "app_common.h"

const char *TAG = "p4_muon";
spi_device_handle_t s_fpga;
#if !READOUT_PROFILE_OCT2025
spi_device_handle_t s_dac;
#endif
spi_device_handle_t s_hv;
esp_ldo_channel_handle_t s_ldo_vo4;
SemaphoreHandle_t s_spi_mutex;
SemaphoreHandle_t s_sd_mutex;
httpd_handle_t s_httpd;
esp_netif_t *s_ap_netif;
sdmmc_card_t *s_sd_card;

portMUX_TYPE s_count_mux = portMUX_INITIALIZER_UNLOCKED;
portMUX_TYPE s_state_mux = portMUX_INITIALIZER_UNLOCKED;
volatile uint32_t s_counts[COUNT_CHANNELS];
uint32_t s_last_counts[COUNT_CHANNELS];
uint32_t s_live_counts[COUNT_CHANNELS];
uint64_t s_totals[COUNT_CHANNELS];
count_record_t s_log[LOG_RECORDS];
size_t s_log_head;
size_t s_log_count;
uint8_t s_hv_byte = 0x00;
volatile bool s_counting_enabled;
int64_t s_hv_settle_until_ms;
bool s_fpga_ok;
bool s_time_set;
bool s_sd_mounted;
bool s_power_save_mode;
bool s_wifi_keep_on;
bool s_fpga_clock_on;
bool s_fpga_clock_is_clkout;
esp_clock_output_mapping_handle_t s_fpga_clkout;
uint8_t s_wifi_clients;
uint16_t s_dac_codes[8] = {
    STARTUP_DAC_CODE, STARTUP_DAC_CODE, STARTUP_DAC_CODE, STARTUP_DAC_CODE,
    STARTUP_DAC_THRESHOLD_CODE, STARTUP_DAC_THRESHOLD_CODE, STARTUP_DAC_THRESHOLD_CODE, STARTUP_DAC_THRESHOLD_CODE,
};
int64_t s_run_start_uptime_ms;
time_t s_run_start_epoch;
char s_run_label[RUN_LABEL_MAX + 1];
char s_log_path[128];
char s_env_log_path[128];
bme280_calib_t s_bme280_calib;
uint8_t s_bme280_addr;
bool s_bme280_ok;
bme280_reading_t s_bme280_latest;
bool s_i2c_ready;

const gpio_num_t s_count_pins[COUNT_CHANNELS] = {
    PIN_COUNT_CH01,
    PIN_COUNT_CH02,
    PIN_COUNT_CH12,
#if READOUT_PROFILE_OCT2025
    PIN_COUNT_CH012,
#endif
    PIN_COUNT_GPIO6,
    PIN_COUNT_GPIO5,
    PIN_COUNT_GPIO16,
};

const char *s_count_names[COUNT_CHANNELS] = {
    "ch01_p13",
    "ch02_p12",
    "ch12_p11",
#if READOUT_PROFILE_OCT2025
    "ch012_p22",
#endif
    "gpio6_p31",
    "gpio5_p29",
    "gpio16_p36",
};
