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

// Count updates happen in GPIO ISRs, while the web and SD tasks read snapshots.
// Keep the count lock separate from slower state/SD locks so interrupts stay tiny.
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
bool s_browser_tz_valid;
int32_t s_browser_tz_offset_min;
bool s_sd_mounted;
bool s_power_save_mode;
bool s_wifi_keep_on;
int64_t s_wifi_auto_off_deadline_ms;
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

// Channel order is the on-disk and web/API order. Change it carefully because
// old CSV files and analysis scripts will assume these names.
const gpio_num_t s_count_pins[COUNT_CHANNELS] = {
    PIN_COUNT_CH12,
    PIN_COUNT_CH13,
    PIN_COUNT_CH23,
    PIN_COUNT_CH1,
    PIN_COUNT_CH2,
    PIN_COUNT_CH3,
};

const char *s_count_names[COUNT_CHANNELS] = {
    "ch12_p13",
    "ch13_p12",
    "ch23_p11",
    "ch1_p31",
    "ch2_p29",
    "ch3_p36",
};
