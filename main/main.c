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

#define READOUT_PROFILE_OCT2025 1

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
#define WIFI_AUTO_OFF_MS        (20 * 1000)
#define FIELD_MIN_CPU_FREQ_MHZ  40

extern const uint8_t fpga_bin_start[] asm("_binary_fpga_bin_start");
extern const uint8_t fpga_bin_end[] asm("_binary_fpga_bin_end");

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

static const char *TAG = "p4_muon";
static spi_device_handle_t s_fpga;
#if !READOUT_PROFILE_OCT2025
static spi_device_handle_t s_dac;
#endif
static spi_device_handle_t s_hv;
static esp_ldo_channel_handle_t s_ldo_vo4;
static SemaphoreHandle_t s_spi_mutex;
static SemaphoreHandle_t s_sd_mutex;
static httpd_handle_t s_httpd;
static esp_netif_t *s_ap_netif;
static sdmmc_card_t *s_sd_card;

static portMUX_TYPE s_count_mux = portMUX_INITIALIZER_UNLOCKED;
static portMUX_TYPE s_state_mux = portMUX_INITIALIZER_UNLOCKED;
static volatile uint32_t s_counts[COUNT_CHANNELS];
static uint32_t s_last_counts[COUNT_CHANNELS];
static uint32_t s_live_counts[COUNT_CHANNELS];
static uint64_t s_totals[COUNT_CHANNELS];
static count_record_t s_log[LOG_RECORDS];
static size_t s_log_head;
static size_t s_log_count;
static uint8_t s_hv_byte = 0x00;
static volatile bool s_counting_enabled;
static int64_t s_hv_settle_until_ms;
static bool s_fpga_ok;
static bool s_time_set;
static bool s_sd_mounted;
static bool s_power_save_mode;
static bool s_wifi_keep_on;
static bool s_fpga_clock_on;
static bool s_fpga_clock_is_clkout;
static esp_clock_output_mapping_handle_t s_fpga_clkout;
static uint8_t s_wifi_clients;
static uint16_t s_dac_codes[8] = {
    STARTUP_DAC_CODE, STARTUP_DAC_CODE, STARTUP_DAC_CODE, STARTUP_DAC_CODE,
    STARTUP_DAC_THRESHOLD_CODE, STARTUP_DAC_THRESHOLD_CODE, STARTUP_DAC_THRESHOLD_CODE, STARTUP_DAC_THRESHOLD_CODE,
};
static int64_t s_run_start_uptime_ms;
static time_t s_run_start_epoch;
static char s_run_label[RUN_LABEL_MAX + 1];
static char s_log_path[128];
static char s_env_log_path[128];
static bme280_calib_t s_bme280_calib;
static uint8_t s_bme280_addr;
static bool s_bme280_ok;
static bme280_reading_t s_bme280_latest;
static bool s_i2c_ready;

static const gpio_num_t s_count_pins[COUNT_CHANNELS] = {
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

static const char *s_count_names[COUNT_CHANNELS] = {
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

static const char s_index_html[] =
"<!doctype html><html><head><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
"<title>Muon Readout</title><style>"
":root{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',sans-serif;color:#182026;background:#f7f5ef}"
"body{margin:0}.bar{display:flex;gap:12px;align-items:center;justify-content:space-between;padding:14px 18px;background:#182026;color:white}"
"main{padding:16px;max-width:1180px;margin:auto}.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(270px,1fr));gap:12px}"
"section{background:white;border:1px solid #d9d5ca;border-radius:8px;padding:14px;margin-top:12px}section:first-child{margin-top:0}h1{font-size:20px;margin:0}h2{font-size:16px;margin:0 0 10px}"
"table{border-collapse:collapse;width:100%;font-size:13px}td,th{border-bottom:1px solid #ece7dc;padding:6px;text-align:right}td:first-child,th:first-child{text-align:left}"
"button,input{font:inherit;border:1px solid #aaa;border-radius:6px;padding:8px;background:white}button{cursor:pointer;background:#1f6feb;color:white;border-color:#1f6feb}"
"button.secondary{background:white;color:#182026}.row{display:flex;gap:8px;flex-wrap:wrap;align-items:center;margin:8px 0}.mono{font-family:ui-monospace,SFMono-Regular,Menlo,monospace}"
".topStatus{display:grid;grid-template-columns:repeat(auto-fit,minmax(220px,1fr));gap:10px;margin-bottom:12px}.badge{border:1px solid #d9d5ca;border-radius:8px;padding:12px;background:white}.badgeLabel{font-size:12px;color:#5f675a}.badgeValue{font-size:24px;font-weight:750;margin-top:4px}.badge.okBg{border-color:#8ac7a2;background:#f3fff7}.badge.badBg{border-color:#e39b95;background:#fff5f3}"
".ok{color:#18794e}.bad{color:#c0342b}.hero{border-color:#aabf98;background:#fbfff7}.countCards{display:grid;grid-template-columns:repeat(auto-fit,minmax(150px,1fr));gap:10px}.countCard{border:1px solid #cddfc0;border-radius:8px;padding:12px;background:white}.countName{font-size:13px;color:#5b6754}.countValue{font-size:36px;font-weight:750;line-height:1.1;margin:6px 0}.countMeta{font-size:12px;color:#5f675a}.filesSection{margin-top:18px}@media(max-width:540px){.bar{display:block}.grid{grid-template-columns:1fr}.countValue{font-size:30px}}"
"</style></head><body><div class=\"bar\"><h1>gLOWCOST Muon Readout</h1><div class=\"mono\" id=\"clock\">--</div></div><main><div class=\"topStatus\"><div id=\"sdBadge\" class=\"badge\"><div class=\"badgeLabel\">SD Card</div><div class=\"badgeValue\">--</div><div class=\"mono\" id=\"sdBadgeFile\">--</div></div></div>"
"<div class=\"grid\"><section><h2>Status</h2><div id=\"status\">Loading...</div></section>"
"<section><h2>Time</h2><div class=\"row\"><button onclick=\"syncTime()\">Sync Browser Time</button></div>"
"<div class=\"row\"><input id=\"manualTime\" type=\"datetime-local\"><button class=\"secondary\" onclick=\"setManualTime()\">Set</button></div></section>"
"<section><h2>SD Log</h2><div id=\"sdlog\">--</div><div class=\"row\"><input id=\"runLabel\" maxlength=\"32\" placeholder=\"flight/location\"><button onclick=\"setRunLabel()\">Set Label</button></div><div class=\"row\"><a href=\"/api/sd.csv\">Download Current File</a></div></section>"
"<section><h2>Environment</h2><div id=\"envlog\">--</div><div class=\"row\"><a href=\"/api/env.csv\">Download Environment File</a></div></section>"
"<section><h2>High Voltage</h2><p>Startup sequence: HV off, FPGA flash, DAC init, then profile HV after settle.</p><div class=\"row\"><input id=\"hvByte\" class=\"mono\" value=\"ea\" maxlength=\"2\" size=\"4\"><button onclick=\"setHv()\">Set HV Byte</button><button class=\"secondary\" onclick=\"hvOff()\">Off</button></div></section>"
"<section><h2>FPGA / DAC</h2><div class=\"row\"><button onclick=\"flashFpga()\">HV Off + Flash FPGA + HV On</button><button class=\"secondary\" onclick=\"dacZero()\">Set Startup DAC</button></div>"
"<div class=\"row\"><input id=\"dacCh\" type=\"number\" min=\"0\" max=\"7\" value=\"0\"><input id=\"dacVal\" class=\"mono\" value=\"0000\" maxlength=\"4\" size=\"6\"><button class=\"secondary\" onclick=\"setDac()\">Set DAC</button></div></section></div>"
"<section><h2>Power</h2><p>For quiet data, sync/download/check briefly, then turn Wi-Fi off. HV cycles off during Wi-Fi shutdown, then counting resumes after settle.</p><div class=\"row\"><button onclick=\"keepWifiOn()\">Keep Wi-Fi On</button><button class=\"secondary\" onclick=\"allowWifiAutoOff()\">Allow Auto-Off</button><button onclick=\"powerSave()\">Turn Wi-Fi Off</button></div></section>"
"<section class=\"hero\"><h2>Live Minute Coincident Counts</h2><div id=\"countCards\" class=\"countCards\"></div></section>"
"<section><h2>Counts Detail</h2><table id=\"counts\"></table></section>"
"<section><h2>Minute Log</h2><div class=\"row\"><a href=\"/api/log.csv\">Download CSV</a></div><table id=\"log\"></table></section>"
"<section class=\"filesSection\"><h2>Previous SD Files</h2><div class=\"row\"><button class=\"secondary\" onclick=\"loadSdFiles()\">Refresh Files</button></div><div id=\"sdFiles\" class=\"mono\">--</div></section>"
"</main><script>"
"async function api(u){let r=await fetch(u); if(!r.ok) throw new Error(await r.text()); return r}"
"function esc(s){return String(s).replace(/[&<>]/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;'}[c]))}"
"function fmtEpoch(e){return e>1600000000?new Date(e*1000).toLocaleString():'not set'}"
"async function refresh(){try{let j=await (await api('/api/status')).json(); document.getElementById('clock').textContent=fmtEpoch(j.epoch)+' / '+Math.round(j.uptime_ms/1000)+'s';"
"let sdBadge=document.getElementById('sdBadge'); sdBadge.className='badge '+(j.sd_mounted?'okBg':'badBg'); sdBadge.querySelector('.badgeValue').innerHTML=j.sd_mounted?'<span class=\"ok\">Ready</span>':'<span class=\"bad\">Not Ready</span>'; document.getElementById('sdBadgeFile').textContent=j.sd_mounted?(j.log_file||'mounted, no file'):'check card / slot';"
"document.getElementById('status').innerHTML='<p>AP <span class=\"mono\">'+j.ssid+'</span> at <span class=\"mono\">http://192.168.4.1</span></p><p>Power saving: '+(j.power_save_mode?'on':'off')+'</p><p>Wi-Fi auto-off: '+(j.wifi_keep_on?'disabled':'enabled')+'</p><p>FPGA DONE: <b class=\"'+(j.fpga_done?'ok':'bad')+'\">'+j.fpga_done+'</b></p><p>HV byte: <span class=\"mono\">0x'+j.hv_byte+'</span></p><p>Counting: '+(j.counting_enabled?'enabled':'settling/off')+'</p><p>HV settle remaining: '+Math.max(0,Math.ceil(j.hv_settle_remaining_ms/1000))+'s</p><p>Time: '+(j.time_set?'set':'not set')+'</p>';"
"document.getElementById('sdlog').innerHTML='<p>SD: '+(j.sd_mounted?'mounted':'not mounted')+'</p><p>File: <span class=\"mono\">'+esc(j.log_file||'none')+'</span></p><p>Label: <span class=\"mono\">'+esc(j.run_label||'')+'</span></p>';"
"document.getElementById('envlog').innerHTML='<p>BME280: '+(j.bme280_ok?'ready':'not found')+'</p><p>File: <span class=\"mono\">'+esc(j.env_log_file||'none')+'</span></p>'+((j.env_latest&&j.env_latest.valid)?'<p>Latest: '+j.env_latest.temp_c.toFixed(2)+' C, '+j.env_latest.pressure_hpa.toFixed(2)+' hPa, '+j.env_latest.humidity_pct.toFixed(2)+' %RH</p>':'<p>Latest: waiting</p>');"
"let cards=''; j.channels.forEach(c=>cards+='<div class=\"countCard\"><div class=\"countName\">'+esc(c.name)+'</div><div class=\"countValue\">'+c.last+'</div><div class=\"countMeta\">current '+c.current+' / total '+c.total+'</div></div>'); document.getElementById('countCards').innerHTML=cards;"
"let h='<tr><th>channel</th><th>current</th><th>last minute</th><th>total</th></tr>'; j.channels.forEach(c=>h+='<tr><td>'+esc(c.name)+'</td><td>'+c.current+'</td><td>'+c.last+'</td><td>'+c.total+'</td></tr>'); document.getElementById('counts').innerHTML=h;"
"let l='<tr><th>time</th>'; j.channels.forEach(c=>l+='<th>'+esc(c.name)+'</th>'); l+='</tr>'; j.records.forEach(r=>{l+='<tr><td>'+fmtEpoch(r.epoch)+'</td>'; r.counts.forEach(v=>l+='<td>'+v+'</td>'); l+='</tr>'}); document.getElementById('log').innerHTML=l;"
"}catch(e){document.getElementById('status').textContent=e.message}}"
"async function syncTime(){await api('/api/time?epoch='+Math.floor(Date.now()/1000)); refresh()}"
"async function autoSyncTime(){let e=Math.floor(Date.now()/1000); if(e>1600000000){try{await api('/api/time?epoch='+e)}catch(err){}} refresh()}"
"async function setManualTime(){let v=document.getElementById('manualTime').value; if(v) {await api('/api/time?epoch='+Math.floor(new Date(v).getTime()/1000)); refresh()}}"
"async function setRunLabel(){await api('/api/run_label?label='+encodeURIComponent(document.getElementById('runLabel').value)); refresh()}"
"async function loadSdFiles(){try{let j=await (await api('/api/sd_files')).json(); let h='<table><tr><th>file</th><th>modified</th><th>bytes</th><th></th></tr>'; j.files.forEach(f=>{h+='<tr><td>'+esc(f.name)+(f.current?' *':'')+'</td><td>'+esc(f.modified||'--')+'</td><td>'+f.size+'</td><td><a href=\"/api/sd_file?name='+encodeURIComponent(f.name)+'\">download</a></td></tr>'}); h+='</table>'; document.getElementById('sdFiles').innerHTML=h}catch(e){document.getElementById('sdFiles').textContent=e.message}}"
"async function setHv(){await api('/api/hv?byte='+encodeURIComponent(document.getElementById('hvByte').value)); refresh()}"
"async function hvOff(){await api('/api/hv?off=1'); refresh()}"
"async function flashFpga(){if(confirm('This will turn HV off, flash the FPGA, reload startup DAC values, then turn HV back on after the normal settle.')){await api('/api/fpga'); refresh()}}"
"async function dacZero(){for(let i=0;i<4;i++) await api('/api/dac?ch='+i+'&value=02f1'); for(let i=4;i<8;i++) await api('/api/dac?ch='+i+'&value=0070'); refresh()}"
"async function setDac(){await api('/api/dac?ch='+document.getElementById('dacCh').value+'&value='+document.getElementById('dacVal').value); refresh()}"
"async function keepWifiOn(){await api('/api/wifi_keep_on?enable=1'); refresh()}"
"async function allowWifiAutoOff(){await api('/api/wifi_keep_on?enable=0'); refresh()}"
"async function powerSave(){if(confirm('Turn Wi-Fi off until next reboot? HV will cycle off briefly and counting resumes after settle.')){try{await syncTime()}catch(e){} await api('/api/power_save'); document.body.innerHTML='<main><section><h2>Power saving mode enabled</h2><p>Wi-Fi is shutting down. HV will restart after 3 seconds, then counting resumes after settle.</p></section></main>'}}"
"autoSyncTime(); loadSdFiles(); setInterval(refresh,2000);</script></body></html>";

static void IRAM_ATTR count_isr(void *arg)
{
    uintptr_t index = (uintptr_t)arg;
    if (s_counting_enabled && index < COUNT_CHANNELS) {
        portENTER_CRITICAL_ISR(&s_count_mux);
        s_counts[index]++;
        portEXIT_CRITICAL_ISR(&s_count_mux);
    }
}

static void spi_lock(void)
{
    if (s_spi_mutex) {
        xSemaphoreTake(s_spi_mutex, portMAX_DELAY);
    }
}

static void spi_unlock(void)
{
    if (s_spi_mutex) {
        xSemaphoreGive(s_spi_mutex);
    }
}

static void stop_fpga_clock(void);
static esp_err_t init_i2c_bus(void);

static esp_err_t configure_high_gpio_rail(void)
{
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

static esp_err_t start_fpga_clock_hz(uint32_t hz)
{
    if (s_fpga_clock_on) {
        stop_fpga_clock();
    }

#if READOUT_PROFILE_OCT2025
    if (hz == FPGA_RUNTIME_CLK_HZ) {
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

static esp_err_t start_fpga_clock(void)
{
    return start_fpga_clock_hz(FPGA_CLK_HZ);
}

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

static esp_err_t spi_send(spi_device_handle_t dev, const uint8_t *data, size_t len)
{
    spi_transaction_t transaction = {
        .length = len * 8,
        .tx_buffer = data,
    };
    return spi_device_transmit(dev, &transaction);
}

static esp_err_t init_spi(void)
{
    s_spi_mutex = xSemaphoreCreateMutex();
    ESP_RETURN_ON_FALSE(s_spi_mutex, ESP_ERR_NO_MEM, TAG, "create SPI mutex");

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

static esp_err_t init_control_gpios(void)
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

static esp_err_t ice40_clear_locked(void)
{
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

static esp_err_t program_fpga(void)
{
    const size_t embedded_len = fpga_bin_end - fpga_bin_start;
#if READOUT_PROFILE_OCT2025
    const size_t bit_len = embedded_len;
#else
    const size_t bit_len = embedded_len < ICE40_PI_BITSTREAM_LEN ? embedded_len : ICE40_PI_BITSTREAM_LEN;
#endif
    ESP_LOGI(TAG, "programming iCE40: embedded=%u bytes, sending=%u bytes", (unsigned)embedded_len, (unsigned)bit_len);
    ESP_RETURN_ON_ERROR(start_fpga_clock(), TAG, "start FPGA clock for programming");

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
static esp_err_t dac_write(uint8_t reg, uint16_t value)
{
    uint8_t data[3] = {reg, (uint8_t)(value >> 8), (uint8_t)value};
    spi_lock();
    esp_err_t ret = spi_send(s_dac, data, sizeof(data));
    spi_unlock();
    return ret;
}
#endif

static esp_err_t dacx578_write_channel(uint8_t ch, uint16_t code10)
{
    ESP_RETURN_ON_ERROR(init_i2c_bus(), TAG, "init I2C for DACx578");
    if (ch > 7 || code10 > 0x3FF) {
        return ESP_ERR_INVALID_ARG;
    }

    uint16_t raw12 = (uint16_t)(((uint32_t)code10 * 4095U + 511U) / 1023U);
    uint16_t aligned = raw12 << 4;
    uint8_t data[3] = {
        (uint8_t)(0x30 | (ch & 0x0F)),
        (uint8_t)(aligned >> 8),
        (uint8_t)aligned,
    };
    return i2c_master_write_to_device(BME280_I2C_PORT, DACX578_ADDR, data, sizeof(data), pdMS_TO_TICKS(100));
}

static esp_err_t dac_set_channel(uint8_t ch, uint16_t value)
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

static esp_err_t dac_zero_channels(void)
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

static esp_err_t hv_write_byte(uint8_t value)
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

static void clear_live_counts(void)
{
    portENTER_CRITICAL(&s_count_mux);
    for (size_t i = 0; i < COUNT_CHANNELS; i++) {
        s_counts[i] = 0;
    }
    portEXIT_CRITICAL(&s_count_mux);
}

static bool counting_is_enabled(void)
{
    bool enabled;
    portENTER_CRITICAL(&s_state_mux);
    enabled = s_counting_enabled;
    portEXIT_CRITICAL(&s_state_mux);
    return enabled;
}

static esp_err_t hv_write_and_settle(uint8_t value)
{
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

static uint16_t u16_le(const uint8_t *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static int16_t s16_le(const uint8_t *p)
{
    return (int16_t)u16_le(p);
}

static esp_err_t init_i2c_bus(void)
{
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

static esp_err_t bme280_read_reg(uint8_t reg, uint8_t *data, size_t len)
{
    return i2c_master_write_read_device(BME280_I2C_PORT, s_bme280_addr, &reg, 1, data, len, pdMS_TO_TICKS(100));
}

static esp_err_t bme280_write_reg(uint8_t reg, uint8_t value)
{
    uint8_t data[2] = {reg, value};
    return i2c_master_write_to_device(BME280_I2C_PORT, s_bme280_addr, data, sizeof(data), pdMS_TO_TICKS(100));
}

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

static esp_err_t bme280_load_calibration(void)
{
    uint8_t calib1[26];
    uint8_t calib2[7];
    ESP_RETURN_ON_ERROR(bme280_read_reg(0x88, calib1, sizeof(calib1)), TAG, "read BME280 calibration 1");
    ESP_RETURN_ON_ERROR(bme280_read_reg(0xE1, calib2, sizeof(calib2)), TAG, "read BME280 calibration 2");

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

static esp_err_t init_bme280(void)
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

static esp_err_t init_counters(void)
{
    uint64_t mask = 0;
    for (size_t i = 0; i < COUNT_CHANNELS; i++) {
        mask |= (1ULL << s_count_pins[i]);
    }
    gpio_config_t inputs = {
        .pin_bit_mask = mask,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_POSEDGE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&inputs), TAG, "configure counter inputs");
    esp_err_t ret = gpio_install_isr_service(ESP_INTR_FLAG_IRAM);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_RETURN_ON_ERROR(ret, TAG, "install GPIO ISR service");
    }
    for (size_t i = 0; i < COUNT_CHANNELS; i++) {
        ESP_RETURN_ON_ERROR(gpio_isr_handler_add(s_count_pins[i], count_isr, (void *)i), TAG, "add counter ISR");
    }
    ESP_LOGI(TAG, "counter inputs armed");
    return ESP_OK;
}

static void sanitize_run_label(const char *input, char *output, size_t output_len)
{
    size_t j = 0;
    for (size_t i = 0; input[i] && j + 1 < output_len; i++) {
        unsigned char c = (unsigned char)input[i];
        if (isalnum(c) || c == '-' || c == '_') {
            output[j++] = (char)c;
        } else if ((c == ' ' || c == '.' || c == ',') && j > 0 && output[j - 1] != '_') {
            output[j++] = '_';
        }
    }
    while (j > 0 && output[j - 1] == '_') {
        j--;
    }
    output[j] = '\0';
}

static void url_decode_in_place(char *s)
{
    char *r = s;
    char *w = s;
    while (*r) {
        if (r[0] == '%' && isxdigit((unsigned char)r[1]) && isxdigit((unsigned char)r[2])) {
            char hex[3] = {r[1], r[2], '\0'};
            *w++ = (char)strtol(hex, NULL, 16);
            r += 3;
        } else if (*r == '+') {
            *w++ = ' ';
            r++;
        } else {
            *w++ = *r++;
        }
    }
    *w = '\0';
}

static void csv_time_string(time_t epoch, char *out, size_t out_len)
{
    if (epoch > 1600000000) {
        struct tm tm;
        localtime_r(&epoch, &tm);
        strftime(out, out_len, "%Y-%m-%dT%H:%M:%S", &tm);
    } else {
        snprintf(out, out_len, "unset");
    }
}

static void count_csv_header(char *out, size_t out_len)
{
    size_t off = snprintf(out, out_len, "epoch,iso");
    for (size_t i = 0; i < COUNT_CHANNELS && off < out_len; i++) {
        int written = snprintf(out + off, out_len - off, ",%s", s_count_names[i]);
        if (written > 0) {
            off += (size_t)written;
        }
    }
    if (off + 1 < out_len) {
        snprintf(out + off, out_len - off, "\n");
    }
}

static void sd_build_candidate_path(const char *prefix, char *path, size_t path_len, unsigned suffix)
{
    char stem[80];
    if (s_run_start_epoch > 1600000000) {
        struct tm tm;
        localtime_r(&s_run_start_epoch, &tm);
        char when[32];
        strftime(when, sizeof(when), "%Y%m%d_%H%M%S", &tm);
        snprintf(stem, sizeof(stem), "%s_%s", prefix, when);
    } else {
        snprintf(stem, sizeof(stem), "%s_unsynced_%" PRId64, prefix, s_run_start_uptime_ms / 1000);
    }

    if (s_run_label[0]) {
        if (suffix) {
            snprintf(path, path_len, SD_MOUNT_POINT "/%s_%s_%02u.csv", stem, s_run_label, suffix);
        } else {
            snprintf(path, path_len, SD_MOUNT_POINT "/%s_%s.csv", stem, s_run_label);
        }
    } else if (suffix) {
        snprintf(path, path_len, SD_MOUNT_POINT "/%s_%02u.csv", stem, suffix);
    } else {
        snprintf(path, path_len, SD_MOUNT_POINT "/%s.csv", stem);
    }
}

static esp_err_t sd_write_header_if_new(const char *path, const char *header)
{
    if (access(path, F_OK) == 0) {
        return ESP_OK;
    }
    FILE *f = fopen(path, "w");
    if (!f) {
        ESP_LOGE(TAG, "create SD log failed: %s errno=%d", path, errno);
        return ESP_FAIL;
    }
    fputs(header, f);
    fflush(f);
    fsync(fileno(f));
    fclose(f);
    return ESP_OK;
}

static esp_err_t sd_refresh_one_path_locked(char *path, size_t path_len, const char *prefix, const char *header)
{
    if (!s_sd_mounted) {
        return ESP_ERR_INVALID_STATE;
    }

    char new_path[128];
    for (unsigned suffix = 0; suffix < 100; suffix++) {
        sd_build_candidate_path(prefix, new_path, sizeof(new_path), suffix);
        if (path[0] && strcmp(path, new_path) == 0) {
            return ESP_OK;
        }
        if (access(new_path, F_OK) != 0) {
            break;
        }
        if (suffix == 99) {
            return ESP_ERR_NO_MEM;
        }
    }

    if (path[0]) {
        if (rename(path, new_path) != 0) {
            ESP_LOGW(TAG, "could not rename SD log %s -> %s; continuing with old file", path, new_path);
            return ESP_FAIL;
        }
        snprintf(path, path_len, "%s", new_path);
        ESP_LOGI(TAG, "SD log renamed to %s", path);
        return ESP_OK;
    }

    ESP_RETURN_ON_ERROR(sd_write_header_if_new(new_path, header), TAG, "create SD log header");
    snprintf(path, path_len, "%s", new_path);
    ESP_LOGI(TAG, "SD log file: %s", path);
    return ESP_OK;
}

static esp_err_t sd_refresh_log_path_locked(void)
{
    char count_header[192];
    count_csv_header(count_header, sizeof(count_header));
    esp_err_t ret = sd_refresh_one_path_locked(
        s_log_path, sizeof(s_log_path), "muon", count_header);
    if (ret != ESP_OK) {
        return ret;
    }
    return sd_refresh_one_path_locked(
        s_env_log_path, sizeof(s_env_log_path), "env",
        "epoch,iso,samples,temp_c_avg,pressure_hpa_avg,humidity_pct_avg\n");
}

static esp_err_t sd_refresh_log_path(void)
{
    if (!s_sd_mutex) {
        return ESP_ERR_INVALID_STATE;
    }
    xSemaphoreTake(s_sd_mutex, portMAX_DELAY);
    esp_err_t ret = sd_refresh_log_path_locked();
    xSemaphoreGive(s_sd_mutex);
    return ret;
}

static esp_err_t init_sd_card(void)
{
    s_sd_mutex = xSemaphoreCreateMutex();
    ESP_RETURN_ON_FALSE(s_sd_mutex, ESP_ERR_NO_MEM, TAG, "create SD mutex");

    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 6,
        .allocation_unit_size = 16 * 1024,
    };

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = SPI3_HOST;
    host.max_freq_khz = SD_SPI_HZ;

    spi_bus_config_t bus_cfg = {
        .mosi_io_num = PIN_SD_SPI_MOSI,
        .miso_io_num = PIN_SD_SPI_MISO,
        .sclk_io_num = PIN_SD_SPI_SCLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4096,
    };
    ESP_RETURN_ON_ERROR(spi_bus_initialize(host.slot, &bus_cfg, SPI_DMA_CH_AUTO), TAG, "init SD SPI bus");

    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = PIN_SD_SPI_CS;
    slot_config.host_id = host.slot;

    ESP_LOGI(TAG, "Mounting SD card over SPI: CLK=%d CMD/MOSI=%d D0/MISO=%d D3/CS=%d",
             PIN_SD_SPI_SCLK, PIN_SD_SPI_MOSI, PIN_SD_SPI_MISO, PIN_SD_SPI_CS);
    esp_err_t ret = esp_vfs_fat_sdspi_mount(SD_MOUNT_POINT, &host, &slot_config, &mount_config, &s_sd_card);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SD mount failed: %s", esp_err_to_name(ret));
        return ret;
    }

    xSemaphoreTake(s_sd_mutex, portMAX_DELAY);
    s_sd_mounted = true;
    ret = sd_refresh_log_path_locked();
    xSemaphoreGive(s_sd_mutex);
    if (ret == ESP_OK) {
        sdmmc_card_print_info(stdout, s_sd_card);
    }
    return ret;
}

static void sd_append_record(const count_record_t *record)
{
    if (!s_sd_mutex) {
        return;
    }
    xSemaphoreTake(s_sd_mutex, portMAX_DELAY);
    if (!s_sd_mounted || !s_log_path[0]) {
        xSemaphoreGive(s_sd_mutex);
        return;
    }

    FILE *f = fopen(s_log_path, "a");
    if (!f) {
        ESP_LOGE(TAG, "open SD log failed: %s", s_log_path);
        xSemaphoreGive(s_sd_mutex);
        return;
    }

    char iso[32];
    csv_time_string(record->epoch, iso, sizeof(iso));
    fprintf(f, "%lld,%s", (long long)record->epoch, iso);
    for (size_t i = 0; i < COUNT_CHANNELS; i++) {
        fprintf(f, ",%" PRIu32, record->counts[i]);
    }
    fputc('\n', f);
    fflush(f);
    fsync(fileno(f));
    fclose(f);
    xSemaphoreGive(s_sd_mutex);
}

static void sd_append_env_average(time_t epoch, uint32_t samples, double temp_c, double pressure_hpa, double humidity_pct)
{
    if (!s_sd_mutex || samples == 0) {
        return;
    }
    xSemaphoreTake(s_sd_mutex, portMAX_DELAY);
    if (!s_sd_mounted || !s_env_log_path[0]) {
        xSemaphoreGive(s_sd_mutex);
        return;
    }

    FILE *f = fopen(s_env_log_path, "a");
    if (!f) {
        ESP_LOGE(TAG, "open environment SD log failed: %s", s_env_log_path);
        xSemaphoreGive(s_sd_mutex);
        return;
    }

    char iso[32];
    csv_time_string(epoch, iso, sizeof(iso));
    fprintf(f, "%lld,%s,%" PRIu32 ",%.3f,%.3f,%.3f\n",
            (long long)epoch, iso, samples, temp_c, pressure_hpa, humidity_pct);
    fflush(f);
    fsync(fileno(f));
    fclose(f);
    xSemaphoreGive(s_sd_mutex);
}

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

static double dac_code_to_vlow(uint16_t code)
{
    double frac = (double)(code & 0x03ff) / 1023.0;
    double value = TEMP_COMP_DAC_VOFF + TEMP_COMP_DAC_SPAN * frac;
    return value < 0.0 ? 0.0 : value;
}

static uint16_t dac_vlow_to_code(double vlow)
{
    double clipped = vlow < 0.0 ? 0.0 : vlow;
    int code = (int)lrint(((clipped - TEMP_COMP_DAC_VOFF) / TEMP_COMP_DAC_SPAN) * 1023.0);
    return (uint16_t)clamp_int(code, 0, 1023);
}

static void temp_compensate_dac(double temp_avg_c, double temp_std_c, uint32_t samples)
{
#if READOUT_PROFILE_OCT2025
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

static void bme280_task(void *arg)
{
    (void)arg;
    double temp_sum = 0.0;
    double temp_sq_sum = 0.0;
    double pressure_sum = 0.0;
    double humidity_sum = 0.0;
    uint32_t samples = 0;
    int64_t window_start_ms = esp_timer_get_time() / 1000;

    while (true) {
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

static void snapshot_counts(uint32_t out[COUNT_CHANNELS], bool reset)
{
    portENTER_CRITICAL(&s_count_mux);
    for (size_t i = 0; i < COUNT_CHANNELS; i++) {
        out[i] = s_counts[i];
        if (reset) {
            s_counts[i] = 0;
        }
    }
    portEXIT_CRITICAL(&s_count_mux);
}

static void append_log_record(const uint32_t counts[COUNT_CHANNELS])
{
    count_record_t record = {
        .uptime_ms = esp_timer_get_time() / 1000,
        .epoch = time(NULL),
    };
    memcpy(record.counts, counts, sizeof(record.counts));

    portENTER_CRITICAL(&s_state_mux);
    memcpy(s_last_counts, counts, sizeof(s_last_counts));
    for (size_t i = 0; i < COUNT_CHANNELS; i++) {
        s_totals[i] += counts[i];
    }
    s_log[s_log_head] = record;
    s_log_head = (s_log_head + 1) % LOG_RECORDS;
    if (s_log_count < LOG_RECORDS) {
        s_log_count++;
    }
    portEXIT_CRITICAL(&s_state_mux);
    sd_append_record(&record);
}

static void print_and_reset_counts(void)
{
    if (!counting_is_enabled()) {
        clear_live_counts();
        return;
    }

    uint32_t snapshot[COUNT_CHANNELS];
    snapshot_counts(snapshot, true);
    append_log_record(snapshot);

    if (!s_power_save_mode) {
        printf("counts,epoch=%lld,uptime_ms=%" PRId64, (long long)time(NULL), esp_timer_get_time() / 1000);
        for (size_t i = 0; i < COUNT_CHANNELS; i++) {
            printf(",%s=%" PRIu32, s_count_names[i], snapshot[i]);
        }
        printf("\n");
        fflush(stdout);
    }
}

static void counter_task(void *arg)
{
    (void)arg;
    while (true) {
        while (!counting_is_enabled()) {
            clear_live_counts();
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
        vTaskDelay(pdMS_TO_TICKS(COUNTER_PERIOD_MS));
        print_and_reset_counts();
    }
}

static char *trim(char *line)
{
    while (isspace((unsigned char)*line)) {
        line++;
    }
    char *end = line + strlen(line);
    while (end > line && isspace((unsigned char)end[-1])) {
        *--end = '\0';
    }
    return line;
}

static void print_help(void)
{
    printf("commands: help | status | counts | fpga | dac zero | dac <ch 0-7> <hex16> | hv off | hv <hex8>\n");
}

static void console_task(void *arg)
{
    (void)arg;
    char line[96];
    print_help();
    while (fgets(line, sizeof(line), stdin) != NULL) {
        char *cmd = trim(line);
        if (cmd[0] == '\0') {
            continue;
        } else if (strcmp(cmd, "help") == 0) {
            print_help();
        } else if (strcmp(cmd, "status") == 0) {
            printf("status,fpga_done=%d,hv=0x%02x,ip=http://192.168.4.1\n",
                   gpio_get_level(PIN_FPGA_DONE), s_hv_byte);
        } else if (strcmp(cmd, "counts") == 0) {
            print_and_reset_counts();
        } else if (strcmp(cmd, "fpga") == 0) {
            printf("fpga,%s\n", esp_err_to_name(program_fpga()));
        } else if (strcmp(cmd, "dac zero") == 0) {
            printf("dac_zero,%s\n", esp_err_to_name(dac_zero_channels()));
        } else if (strncmp(cmd, "dac ", 4) == 0) {
            unsigned ch;
            unsigned value;
            if (sscanf(cmd + 4, "%u %x", &ch, &value) == 2 && ch < 8 && value <= 0xffff) {
                printf("dac,%u,0x%04x,%s\n", ch, value, esp_err_to_name(dac_set_channel(ch, value)));
            } else {
                printf("bad dac command\n");
            }
        } else if (strcmp(cmd, "hv off") == 0) {
            printf("hv,0x00,%s\n", esp_err_to_name(hv_write_and_settle(0x00)));
        } else if (strncmp(cmd, "hv ", 3) == 0) {
            unsigned value;
            if (sscanf(cmd + 3, "%x", &value) == 1 && value <= 0xff) {
                printf("hv,0x%02x,%s\n", value, esp_err_to_name(hv_write_and_settle((uint8_t)value)));
            } else {
                printf("bad hv command\n");
            }
        } else {
            printf("unknown command: %s\n", cmd);
            print_help();
        }
        fflush(stdout);
    }
    vTaskDelete(NULL);
}

static bool query_value(httpd_req_t *req, const char *key, char *out, size_t out_len)
{
    char query[128];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK) {
        return false;
    }
    return httpd_query_key_value(query, key, out, out_len) == ESP_OK;
}

static esp_err_t text_response(httpd_req_t *req, const char *text)
{
    httpd_resp_set_type(req, "text/plain");
    return httpd_resp_sendstr(req, text);
}

static esp_err_t root_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, s_index_html, HTTPD_RESP_USE_STRLEN);
}

static void json_record_append(char *buf, size_t buf_len, size_t *offset, const count_record_t *record)
{
    int written = snprintf(buf + *offset, buf_len - *offset,
                           "{\"epoch\":%lld,\"uptime_ms\":%" PRId64 ",\"counts\":[",
                           (long long)record->epoch, record->uptime_ms);
    if (written > 0) {
        *offset += (size_t)written;
    }
    for (size_t i = 0; i < COUNT_CHANNELS && *offset < buf_len; i++) {
        written = snprintf(buf + *offset, buf_len - *offset, "%s%" PRIu32, i ? "," : "", record->counts[i]);
        if (written > 0) {
            *offset += (size_t)written;
        }
    }
    if (*offset < buf_len) {
        written = snprintf(buf + *offset, buf_len - *offset, "]}");
        if (written > 0) {
            *offset += (size_t)written;
        }
    }
}

static void csv_record_line(const count_record_t *record, char *line, size_t line_len)
{
    char iso[32];
    csv_time_string(record->epoch, iso, sizeof(iso));
    size_t off = snprintf(line, line_len, "%lld,%s", (long long)record->epoch, iso);
    for (size_t i = 0; i < COUNT_CHANNELS && off < line_len; i++) {
        int written = snprintf(line + off, line_len - off, ",%" PRIu32, record->counts[i]);
        if (written > 0) {
            off += (size_t)written;
        }
    }
    if (off + 1 < line_len) {
        snprintf(line + off, line_len - off, "\n");
    }
}

static esp_err_t status_handler(httpd_req_t *req)
{
    uint32_t live[COUNT_CHANNELS];
    uint32_t last[COUNT_CHANNELS];
    uint64_t totals[COUNT_CHANNELS];
    count_record_t records[8];
    size_t record_count;
    uint8_t hv_byte;
    bool time_set;
    bool fpga_ok;
    bool counting_enabled;
    bool power_save_mode;
    bool wifi_keep_on;
    int64_t settle_remaining_ms;
    bool sd_mounted = false;
    char log_path[sizeof(s_log_path)] = "";
    char env_log_path[sizeof(s_env_log_path)] = "";
    char run_label[sizeof(s_run_label)] = "";
    bool bme280_ok;
    bme280_reading_t env_latest;

    snapshot_counts(live, false);
    portENTER_CRITICAL(&s_state_mux);
    memcpy(s_live_counts, live, sizeof(s_live_counts));
    memcpy(last, s_last_counts, sizeof(last));
    memcpy(totals, s_totals, sizeof(totals));
    hv_byte = s_hv_byte;
    time_set = s_time_set;
    fpga_ok = s_fpga_ok;
    counting_enabled = s_counting_enabled;
    power_save_mode = s_power_save_mode;
    wifi_keep_on = s_wifi_keep_on;
    bme280_ok = s_bme280_ok;
    env_latest = s_bme280_latest;
    settle_remaining_ms = s_hv_settle_until_ms ? s_hv_settle_until_ms - (esp_timer_get_time() / 1000) : 0;
    record_count = s_log_count < 8 ? s_log_count : 8;
    for (size_t i = 0; i < record_count; i++) {
        size_t newest = (s_log_head + LOG_RECORDS - 1 - i) % LOG_RECORDS;
        records[i] = s_log[newest];
    }
    portEXIT_CRITICAL(&s_state_mux);
    if (s_sd_mutex) {
        xSemaphoreTake(s_sd_mutex, portMAX_DELAY);
        sd_mounted = s_sd_mounted;
        snprintf(log_path, sizeof(log_path), "%s", s_log_path);
        snprintf(env_log_path, sizeof(env_log_path), "%s", s_env_log_path);
        snprintf(run_label, sizeof(run_label), "%s", s_run_label);
        xSemaphoreGive(s_sd_mutex);
    }

    char buf[4096];
    size_t off = 0;
    off += snprintf(buf + off, sizeof(buf) - off,
                    "{\"ssid\":\"%s\",\"uptime_ms\":%" PRId64 ",\"epoch\":%lld,\"time_set\":%s,\"power_save_mode\":%s,\"wifi_keep_on\":%s,\"fpga_done\":%s,\"fpga_ok\":%s,\"hv_byte\":\"%02x\",\"counting_enabled\":%s,\"hv_settle_remaining_ms\":%" PRId64 ",\"sd_mounted\":%s,\"log_file\":\"%s\",\"env_log_file\":\"%s\",\"run_label\":\"%s\",\"bme280_ok\":%s,\"env_latest\":{\"valid\":%s,\"temp_c\":%.3f,\"pressure_hpa\":%.3f,\"humidity_pct\":%.3f},\"channels\":[",
                    WIFI_AP_SSID, esp_timer_get_time() / 1000, (long long)time(NULL),
                    time_set ? "true" : "false",
                    power_save_mode ? "true" : "false",
                    wifi_keep_on ? "true" : "false",
                    gpio_get_level(PIN_FPGA_DONE) ? "true" : "false",
                    fpga_ok ? "true" : "false",
                    hv_byte,
                    counting_enabled ? "true" : "false",
                    settle_remaining_ms > 0 ? settle_remaining_ms : 0,
                    sd_mounted ? "true" : "false",
                    log_path,
                    env_log_path,
                    run_label,
                    bme280_ok ? "true" : "false",
                    env_latest.valid ? "true" : "false",
                    env_latest.temp_c,
                    env_latest.pressure_hpa,
                    env_latest.humidity_pct);
    for (size_t i = 0; i < COUNT_CHANNELS; i++) {
        off += snprintf(buf + off, sizeof(buf) - off,
                        "%s{\"name\":\"%s\",\"current\":%" PRIu32 ",\"last\":%" PRIu32 ",\"total\":%" PRIu64 "}",
                        i ? "," : "", s_count_names[i], live[i], last[i], totals[i]);
    }
    off += snprintf(buf + off, sizeof(buf) - off, "],\"records\":[");
    for (size_t i = 0; i < record_count; i++) {
        if (i) {
            off += snprintf(buf + off, sizeof(buf) - off, ",");
        }
        json_record_append(buf, sizeof(buf), &off, &records[i]);
    }
    off += snprintf(buf + off, sizeof(buf) - off, "]}");

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, buf, off);
}

static esp_err_t log_csv_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/csv");
    httpd_resp_set_hdr(req, "Content-Disposition", "attachment; filename=muon_log.csv");
    char header[192];
    count_csv_header(header, sizeof(header));
    httpd_resp_sendstr_chunk(req, header);

    count_record_t records[LOG_RECORDS];
    size_t count;
    portENTER_CRITICAL(&s_state_mux);
    count = s_log_count;
    for (size_t i = 0; i < count; i++) {
        size_t oldest = (s_log_head + LOG_RECORDS - count + i) % LOG_RECORDS;
        records[i] = s_log[oldest];
    }
    portEXIT_CRITICAL(&s_state_mux);

    char line[192];
    for (size_t i = 0; i < count; i++) {
        csv_record_line(&records[i], line, sizeof(line));
        httpd_resp_sendstr_chunk(req, line);
    }
    return httpd_resp_sendstr_chunk(req, NULL);
}

static esp_err_t latest_txt_handler(httpd_req_t *req)
{
    count_record_t record = {0};
    bool have_record;

    portENTER_CRITICAL(&s_state_mux);
    have_record = s_log_count > 0;
    if (have_record) {
        record = s_log[(s_log_head + LOG_RECORDS - 1) % LOG_RECORDS];
    }
    portEXIT_CRITICAL(&s_state_mux);

    httpd_resp_set_type(req, "text/plain");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    if (!have_record) {
        return httpd_resp_sendstr(req, "waiting for first minute record\n");
    }

    char line[192];
    csv_record_line(&record, line, sizeof(line));
    return httpd_resp_sendstr(req, line);
}

static esp_err_t time_handler(httpd_req_t *req)
{
    char value[24];
    if (!query_value(req, "epoch", value, sizeof(value))) {
        return text_response(req, "missing epoch");
    }
    long long epoch = atoll(value);
    if (epoch < 1600000000LL) {
        return text_response(req, "epoch too small");
    }
    struct timeval tv = {
        .tv_sec = (time_t)epoch,
        .tv_usec = 0,
    };
    ESP_RETURN_ON_ERROR(settimeofday(&tv, NULL), TAG, "set time");
    portENTER_CRITICAL(&s_state_mux);
    s_time_set = true;
    portEXIT_CRITICAL(&s_state_mux);
    if (s_run_start_epoch <= 1600000000 && s_run_start_uptime_ms > 0) {
        int64_t elapsed_s = ((esp_timer_get_time() / 1000) - s_run_start_uptime_ms) / 1000;
        s_run_start_epoch = (time_t)(epoch - elapsed_s);
        esp_err_t ret = sd_refresh_log_path();
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "SD log rename after time sync failed: %s", esp_err_to_name(ret));
        }
    }
    return text_response(req, "ok");
}

static esp_err_t run_label_handler(httpd_req_t *req)
{
    char label_raw[80] = "";
    char label_clean[RUN_LABEL_MAX + 1] = "";
    if (query_value(req, "label", label_raw, sizeof(label_raw))) {
        url_decode_in_place(label_raw);
        sanitize_run_label(label_raw, label_clean, sizeof(label_clean));
    }

    if (!s_sd_mutex) {
        return text_response(req, "sd not initialized");
    }
    xSemaphoreTake(s_sd_mutex, portMAX_DELAY);
    snprintf(s_run_label, sizeof(s_run_label), "%s", label_clean);
    esp_err_t ret = sd_refresh_log_path_locked();
    xSemaphoreGive(s_sd_mutex);
    if (ret != ESP_OK) {
        httpd_resp_set_status(req, "500 SD rename failed");
        return text_response(req, esp_err_to_name(ret));
    }
    return text_response(req, "ok");
}

static esp_err_t sd_csv_handler(httpd_req_t *req)
{
    char path[sizeof(s_log_path)] = "";
    if (!s_sd_mutex) {
        httpd_resp_set_status(req, "404 SD not initialized");
        return text_response(req, "sd not initialized");
    }
    xSemaphoreTake(s_sd_mutex, portMAX_DELAY);
    bool mounted = s_sd_mounted;
    snprintf(path, sizeof(path), "%s", s_log_path);
    xSemaphoreGive(s_sd_mutex);
    if (!mounted || !path[0]) {
        httpd_resp_set_status(req, "404 No SD log");
        return text_response(req, "no sd log");
    }

    FILE *f = fopen(path, "r");
    if (!f) {
        httpd_resp_set_status(req, "404 SD file open failed");
        return text_response(req, "open failed");
    }
    httpd_resp_set_type(req, "text/csv");
    httpd_resp_set_hdr(req, "Content-Disposition", "attachment; filename=muon_sd_log.csv");
    char chunk[512];
    size_t n;
    while ((n = fread(chunk, 1, sizeof(chunk), f)) > 0) {
        if (httpd_resp_send_chunk(req, chunk, n) != ESP_OK) {
            fclose(f);
            return ESP_FAIL;
        }
    }
    fclose(f);
    return httpd_resp_send_chunk(req, NULL, 0);
}

static esp_err_t env_csv_handler(httpd_req_t *req)
{
    char path[sizeof(s_env_log_path)] = "";
    if (!s_sd_mutex) {
        httpd_resp_set_status(req, "404 SD not initialized");
        return text_response(req, "sd not initialized");
    }
    xSemaphoreTake(s_sd_mutex, portMAX_DELAY);
    bool mounted = s_sd_mounted;
    snprintf(path, sizeof(path), "%s", s_env_log_path);
    xSemaphoreGive(s_sd_mutex);
    if (!mounted || !path[0]) {
        httpd_resp_set_status(req, "404 No environment SD log");
        return text_response(req, "no environment sd log");
    }

    FILE *f = fopen(path, "r");
    if (!f) {
        httpd_resp_set_status(req, "404 Environment SD file open failed");
        return text_response(req, "open failed");
    }
    httpd_resp_set_type(req, "text/csv");
    httpd_resp_set_hdr(req, "Content-Disposition", "attachment; filename=environment_sd_log.csv");
    char chunk[512];
    size_t n;
    while ((n = fread(chunk, 1, sizeof(chunk), f)) > 0) {
        if (httpd_resp_send_chunk(req, chunk, n) != ESP_OK) {
            fclose(f);
            return ESP_FAIL;
        }
    }
    fclose(f);
    return httpd_resp_send_chunk(req, NULL, 0);
}

static const char *path_basename(const char *path)
{
    const char *base = strrchr(path, '/');
    return base ? base + 1 : path;
}

static bool is_log_filename_allowed(const char *name)
{
    size_t len = strlen(name);
    if (len == 0 || len > SD_LOG_NAME_MAX || name[0] == '.') {
        return false;
    }
    for (size_t i = 0; name[i]; i++) {
        unsigned char c = (unsigned char)name[i];
        if (!(isalnum(c) || c == '_' || c == '-' || c == '.')) {
            return false;
        }
    }
    const char *dot = strrchr(name, '.');
    return dot && (strcmp(dot, ".csv") == 0 || strcmp(dot, ".log") == 0);
}

#define SD_FILE_LIST_MAX 128

typedef struct {
    char name[SD_LOG_NAME_MAX + 1];
    off_t size;
    time_t mtime;
    bool current;
} sd_file_info_t;

static int compare_sd_file_info_newest_first(const void *a, const void *b)
{
    const sd_file_info_t *fa = (const sd_file_info_t *)a;
    const sd_file_info_t *fb = (const sd_file_info_t *)b;
    if (fa->mtime > fb->mtime) {
        return -1;
    }
    if (fa->mtime < fb->mtime) {
        return 1;
    }
    return strcmp(fa->name, fb->name);
}

static esp_err_t sd_files_handler(httpd_req_t *req)
{
    if (!s_sd_mutex) {
        httpd_resp_set_status(req, "404 SD not initialized");
        return text_response(req, "sd not initialized");
    }

    char current_muon_name[sizeof(s_log_path)] = "";
    char current_env_name[sizeof(s_env_log_path)] = "";
    xSemaphoreTake(s_sd_mutex, portMAX_DELAY);
    bool mounted = s_sd_mounted;
    snprintf(current_muon_name, sizeof(current_muon_name), "%s", path_basename(s_log_path));
    snprintf(current_env_name, sizeof(current_env_name), "%s", path_basename(s_env_log_path));
    xSemaphoreGive(s_sd_mutex);
    if (!mounted) {
        httpd_resp_set_status(req, "404 SD not mounted");
        return text_response(req, "sd not mounted");
    }

    DIR *dir = opendir(SD_MOUNT_POINT);
    if (!dir) {
        httpd_resp_set_status(req, "500 SD open dir failed");
        return text_response(req, "open dir failed");
    }

    sd_file_info_t *files = calloc(SD_FILE_LIST_MAX, sizeof(sd_file_info_t));
    if (!files) {
        closedir(dir);
        httpd_resp_set_status(req, "500 Out of memory");
        return text_response(req, "out of memory");
    }
    size_t file_count = 0;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (!is_log_filename_allowed(entry->d_name)) {
            continue;
        }

        char name[SD_LOG_NAME_MAX + 1];
        size_t name_len = strlen(entry->d_name);
        memcpy(name, entry->d_name, name_len + 1);
        char path[sizeof(s_log_path)];
        snprintf(path, sizeof(path), SD_MOUNT_POINT "/%s", name);
        struct stat st;
        if (stat(path, &st) != 0 || !S_ISREG(st.st_mode)) {
            continue;
        }

        sd_file_info_t *file = NULL;
        if (file_count < SD_FILE_LIST_MAX) {
            file = &files[file_count++];
        } else {
            size_t oldest = 0;
            for (size_t i = 1; i < file_count; i++) {
                if (files[i].mtime < files[oldest].mtime) {
                    oldest = i;
                }
            }
            if (st.st_mtime <= files[oldest].mtime) {
                continue;
            }
            file = &files[oldest];
        }
        snprintf(file->name, sizeof(file->name), "%s", name);
        file->size = st.st_size;
        file->mtime = st.st_mtime;
        file->current = strcmp(name, current_muon_name) == 0 || strcmp(name, current_env_name) == 0;
    }
    closedir(dir);

    qsort(files, file_count, sizeof(files[0]), compare_sd_file_info_newest_first);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_sendstr_chunk(req, "{\"files\":[");

    for (size_t i = 0; i < file_count; i++) {
        char modified[32] = "unknown";
        if (files[i].mtime > 1600000000) {
            csv_time_string(files[i].mtime, modified, sizeof(modified));
        }
        char item[320];
        snprintf(item, sizeof(item),
                 "%s{\"name\":\"%s\",\"modified\":\"%s\",\"mtime\":%lld,\"size\":%lld,\"current\":%s}",
                 i == 0 ? "" : ",",
                 files[i].name,
                 modified,
                 (long long)files[i].mtime,
                 (long long)files[i].size,
                 files[i].current ? "true" : "false");
        httpd_resp_sendstr_chunk(req, item);
    }
    free(files);
    httpd_resp_sendstr_chunk(req, "]}");
    return httpd_resp_sendstr_chunk(req, NULL);
}

static esp_err_t sd_file_handler(httpd_req_t *req)
{
    char name_raw[96];
    if (!query_value(req, "name", name_raw, sizeof(name_raw))) {
        return text_response(req, "missing name");
    }
    url_decode_in_place(name_raw);
    if (!is_log_filename_allowed(name_raw)) {
        httpd_resp_set_status(req, "400 Bad file name");
        return text_response(req, "bad file name");
    }

    if (!s_sd_mutex) {
        httpd_resp_set_status(req, "404 SD not initialized");
        return text_response(req, "sd not initialized");
    }
    xSemaphoreTake(s_sd_mutex, portMAX_DELAY);
    bool mounted = s_sd_mounted;
    xSemaphoreGive(s_sd_mutex);
    if (!mounted) {
        httpd_resp_set_status(req, "404 SD not mounted");
        return text_response(req, "sd not mounted");
    }

    char path[sizeof(s_log_path)];
    snprintf(path, sizeof(path), SD_MOUNT_POINT "/%s", name_raw);
    FILE *f = fopen(path, "r");
    if (!f) {
        httpd_resp_set_status(req, "404 SD file open failed");
        return text_response(req, "open failed");
    }

    char disposition[160];
    snprintf(disposition, sizeof(disposition), "attachment; filename=%s", name_raw);
    httpd_resp_set_type(req, "text/csv");
    httpd_resp_set_hdr(req, "Content-Disposition", disposition);
    char chunk[512];
    size_t n;
    while ((n = fread(chunk, 1, sizeof(chunk), f)) > 0) {
        if (httpd_resp_send_chunk(req, chunk, n) != ESP_OK) {
            fclose(f);
            return ESP_FAIL;
        }
    }
    fclose(f);
    return httpd_resp_send_chunk(req, NULL, 0);
}

static esp_err_t hv_handler(httpd_req_t *req)
{
    char value[16];
    if (query_value(req, "off", value, sizeof(value))) {
        ESP_RETURN_ON_ERROR(hv_write_and_settle(0x00), TAG, "HV off");
        return text_response(req, "ok");
    }
    if (!query_value(req, "byte", value, sizeof(value))) {
        return text_response(req, "missing byte");
    }
    unsigned byte;
    if (sscanf(value, "%x", &byte) != 1 || byte > 0xff) {
        return text_response(req, "bad byte");
    }
    ESP_RETURN_ON_ERROR(hv_write_and_settle((uint8_t)byte), TAG, "HV set");
    return text_response(req, "ok");
}

static esp_err_t dac_handler(httpd_req_t *req)
{
    char ch_s[8];
    char value_s[16];
    if (!query_value(req, "ch", ch_s, sizeof(ch_s)) || !query_value(req, "value", value_s, sizeof(value_s))) {
        return text_response(req, "missing ch or value");
    }
    unsigned ch;
    unsigned value;
    if (sscanf(ch_s, "%u", &ch) != 1 || sscanf(value_s, "%x", &value) != 1 || ch > 7 || value > 0xffff) {
        return text_response(req, "bad dac parameters");
    }
    ESP_RETURN_ON_ERROR(dac_set_channel((uint8_t)ch, (uint16_t)value), TAG, "DAC write");
    return text_response(req, "ok");
}

static esp_err_t fpga_handler(httpd_req_t *req)
{
    ESP_LOGW(TAG, "FPGA reflash requested: turning HV off before programming");
    esp_err_t ret = hv_write_and_settle(0x00);
    if (ret != ESP_OK) {
        httpd_resp_set_status(req, "500 HV off failed");
        return text_response(req, esp_err_to_name(ret));
    }

    clear_live_counts();
    ret = program_fpga();
    if (ret != ESP_OK) {
        httpd_resp_set_status(req, "500 FPGA program failed");
        return text_response(req, esp_err_to_name(ret));
    }
    ret = dac_zero_channels();
    if (ret != ESP_OK) {
        httpd_resp_set_status(req, "500 DAC startup failed");
        return text_response(req, esp_err_to_name(ret));
    }
    ret = hv_write_and_settle(STARTUP_HV_BYTE);
    if (ret != ESP_OK) {
        httpd_resp_set_status(req, "500 HV restart failed");
        return text_response(req, esp_err_to_name(ret));
    }
    return text_response(req, "ok: FPGA flashed, DAC startup values loaded, HV restarted");
}

static void set_runtime_power_profile(void)
{
#if CONFIG_PM_ENABLE
    esp_pm_config_t pm_config = {
        .max_freq_mhz = CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ,
        .min_freq_mhz = FIELD_MIN_CPU_FREQ_MHZ,
        .light_sleep_enable = false,
    };
    esp_err_t ret = esp_pm_configure(&pm_config);
    if (ret == ESP_OK) {
        ESP_LOGW(TAG, "PM allows idle CPU scaling down to %d MHz for power saving", FIELD_MIN_CPU_FREQ_MHZ);
    } else {
        ESP_LOGW(TAG, "CPU frequency limit failed: %s", esp_err_to_name(ret));
    }
#else
    ESP_LOGW(TAG, "CPU frequency limit unavailable; CONFIG_PM_ENABLE is disabled");
#endif
}

static void power_save_task(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(1500));

    uint8_t restore_hv;
    portENTER_CRITICAL(&s_state_mux);
    restore_hv = s_hv_byte;
    portEXIT_CRITICAL(&s_state_mux);

    ESP_LOGW(TAG, "power saving mode: turning HV off before stopping Wi-Fi");
    esp_err_t ret = hv_write_and_settle(0x00);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "HV off before Wi-Fi shutdown failed: %s", esp_err_to_name(ret));
    }

    ESP_LOGW(TAG, "power saving mode: stopping HTTP server and Wi-Fi until next reboot");
    if (s_httpd) {
        httpd_handle_t httpd = s_httpd;
        s_httpd = NULL;
        ret = httpd_stop(httpd);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "HTTP server stop failed: %s", esp_err_to_name(ret));
        }
    }

    ret = esp_wifi_stop();
    if (ret != ESP_OK && ret != ESP_ERR_WIFI_NOT_INIT) {
        ESP_LOGW(TAG, "Wi-Fi stop failed: %s", esp_err_to_name(ret));
    }
    ret = esp_wifi_deinit();
    if (ret != ESP_OK && ret != ESP_ERR_WIFI_NOT_INIT) {
        ESP_LOGW(TAG, "Wi-Fi deinit failed: %s", esp_err_to_name(ret));
    }

    ESP_LOGW(TAG, "Wi-Fi is off; keeping HV off for 3 seconds");
    vTaskDelay(pdMS_TO_TICKS(3000));
    if (restore_hv != 0) {
        ESP_LOGW(TAG, "restoring HV byte 0x%02x after Wi-Fi shutdown", restore_hv);
        ret = hv_write_and_settle(restore_hv);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "HV restore after Wi-Fi shutdown failed: %s", esp_err_to_name(ret));
        }
    } else {
        ESP_LOGW(TAG, "HV was already off before Wi-Fi shutdown; leaving it off");
    }

    set_runtime_power_profile();
    esp_log_level_set("*", ESP_LOG_WARN);
    ESP_LOGW(TAG, "power saving mode active; Wi-Fi is off and detector logging continues");
    vTaskDelete(NULL);
}

static esp_err_t enter_power_save_mode(void)
{
    portENTER_CRITICAL(&s_state_mux);
    bool already_enabled = s_power_save_mode;
    s_power_save_mode = true;
    portEXIT_CRITICAL(&s_state_mux);

    if (already_enabled) {
        return ESP_OK;
    }

    BaseType_t ok = xTaskCreatePinnedToCore(power_save_task, "power_save_task", 4096, NULL, 5, NULL, 1);
    return ok == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}

static esp_err_t power_save_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/plain");
    ESP_RETURN_ON_ERROR(httpd_resp_sendstr(req, "ok: Wi-Fi shutting down until next reboot"), TAG, "send power-save response");

    esp_err_t ret = enter_power_save_mode();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "could not enter power saving mode: %s", esp_err_to_name(ret));
    }
    return ESP_OK;
}

static esp_err_t wifi_keep_on_handler(httpd_req_t *req)
{
    char value[8] = "1";
    query_value(req, "enable", value, sizeof(value));
    bool keep_on = strcmp(value, "0") != 0 && strcasecmp(value, "false") != 0;

    portENTER_CRITICAL(&s_state_mux);
    s_wifi_keep_on = keep_on;
    portEXIT_CRITICAL(&s_state_mux);

    ESP_LOGW(TAG, "Wi-Fi auto-off %s", keep_on ? "disabled" : "enabled");
    return text_response(req, keep_on ? "ok: Wi-Fi will stay on" : "ok: Wi-Fi auto-off enabled");
}

static void auto_power_save_task(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(WIFI_AUTO_OFF_MS));

    while (true) {
        portENTER_CRITICAL(&s_state_mux);
        bool active = s_power_save_mode;
        bool keep_on = s_wifi_keep_on;
        uint8_t clients = s_wifi_clients;
        portEXIT_CRITICAL(&s_state_mux);

        if (active) {
            break;
        }
        if (keep_on) {
            vTaskDelay(pdMS_TO_TICKS(30000));
            continue;
        }
        if (clients == 0) {
            ESP_LOGW(TAG, "no Wi-Fi client after setup window; entering power saving mode");
            esp_err_t ret = enter_power_save_mode();
            if (ret != ESP_OK) {
                ESP_LOGW(TAG, "auto power saving failed: %s", esp_err_to_name(ret));
            }
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(30000));
    }
    vTaskDelete(NULL);
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    (void)arg;
    (void)event_base;
    (void)event_data;
    if (event_id == WIFI_EVENT_AP_STACONNECTED) {
        portENTER_CRITICAL(&s_state_mux);
        if (s_wifi_clients < UINT8_MAX) {
            s_wifi_clients++;
        }
        portEXIT_CRITICAL(&s_state_mux);
        ESP_LOGI(TAG, "Wi-Fi client connected");
    } else if (event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        portENTER_CRITICAL(&s_state_mux);
        if (s_wifi_clients > 0) {
            s_wifi_clients--;
        }
        portEXIT_CRITICAL(&s_state_mux);
        ESP_LOGI(TAG, "Wi-Fi client disconnected");
    }
}

static esp_err_t start_wifi_ap(void)
{
    ESP_LOGI(TAG, "Wi-Fi init: netif");
    ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "netif init");
    ESP_LOGI(TAG, "Wi-Fi init: event loop");
    ESP_RETURN_ON_ERROR(esp_event_loop_create_default(), TAG, "event loop");
    ESP_LOGI(TAG, "Wi-Fi init: AP netif");
    s_ap_netif = esp_netif_create_default_wifi_ap();
    ESP_LOGI(TAG, "Wi-Fi init: event handler");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, NULL), TAG, "wifi event");

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_LOGI(TAG, "Wi-Fi init: esp_wifi_init");
    ESP_RETURN_ON_ERROR(esp_wifi_init(&cfg), TAG, "wifi init");
    ESP_LOGI(TAG, "Wi-Fi init: storage");
    ESP_RETURN_ON_ERROR(esp_wifi_set_storage(WIFI_STORAGE_RAM), TAG, "wifi storage");

    wifi_config_t wifi_config = {0};
    snprintf((char *)wifi_config.ap.ssid, sizeof(wifi_config.ap.ssid), "%s", WIFI_AP_SSID);
    snprintf((char *)wifi_config.ap.password, sizeof(wifi_config.ap.password), "%s", WIFI_AP_PASS);
    wifi_config.ap.ssid_len = strlen(WIFI_AP_SSID);
    wifi_config.ap.channel = WIFI_AP_CHANNEL;
    wifi_config.ap.max_connection = 1;
    wifi_config.ap.beacon_interval = 1000;
    wifi_config.ap.authmode = strlen(WIFI_AP_PASS) ? WIFI_AUTH_WPA_WPA2_PSK : WIFI_AUTH_OPEN;

    ESP_LOGI(TAG, "Wi-Fi init: AP mode");
    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_AP), TAG, "wifi mode AP");
    ESP_LOGI(TAG, "Wi-Fi init: AP config");
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_AP, &wifi_config), TAG, "wifi AP config");
    ESP_LOGI(TAG, "Wi-Fi init: AP bandwidth");
    ESP_RETURN_ON_ERROR(esp_wifi_set_bandwidth(WIFI_IF_AP, WIFI_BW_HT20), TAG, "wifi AP bandwidth");
    ESP_LOGI(TAG, "Wi-Fi init: start AP");
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "wifi start");
    ESP_LOGI(TAG, "Wi-Fi AP started: SSID=%s password=%s URL=http://192.168.4.1", WIFI_AP_SSID, WIFI_AP_PASS);
    return ESP_OK;
}

static esp_err_t start_webserver(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.core_id = 1;
    config.task_priority = tskIDLE_PRIORITY + 3;
    config.stack_size = 8192;
    config.max_uri_handlers = 16;
    config.lru_purge_enable = true;

    ESP_RETURN_ON_ERROR(httpd_start(&s_httpd, &config), TAG, "start HTTP server");
    const httpd_uri_t handlers[] = {
        {.uri = "/", .method = HTTP_GET, .handler = root_handler},
        {.uri = "/api/status", .method = HTTP_GET, .handler = status_handler},
        {.uri = "/api/latest.txt", .method = HTTP_GET, .handler = latest_txt_handler},
        {.uri = "/api/log.csv", .method = HTTP_GET, .handler = log_csv_handler},
        {.uri = "/api/sd.csv", .method = HTTP_GET, .handler = sd_csv_handler},
        {.uri = "/api/env.csv", .method = HTTP_GET, .handler = env_csv_handler},
        {.uri = "/api/sd_files", .method = HTTP_GET, .handler = sd_files_handler},
        {.uri = "/api/sd_file", .method = HTTP_GET, .handler = sd_file_handler},
        {.uri = "/api/time", .method = HTTP_GET, .handler = time_handler},
        {.uri = "/api/run_label", .method = HTTP_GET, .handler = run_label_handler},
        {.uri = "/api/hv", .method = HTTP_GET, .handler = hv_handler},
        {.uri = "/api/dac", .method = HTTP_GET, .handler = dac_handler},
        {.uri = "/api/fpga", .method = HTTP_GET, .handler = fpga_handler},
        {.uri = "/api/power_save", .method = HTTP_GET, .handler = power_save_handler},
        {.uri = "/api/wifi_keep_on", .method = HTTP_GET, .handler = wifi_keep_on_handler},
    };
    for (size_t i = 0; i < sizeof(handlers) / sizeof(handlers[0]); i++) {
        ESP_RETURN_ON_ERROR(httpd_register_uri_handler(s_httpd, &handlers[i]), TAG, "register HTTP handler");
    }
    ESP_LOGI(TAG, "HTTP server ready on http://192.168.4.1");
    return ESP_OK;
}

static esp_err_t init_nvs(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_RETURN_ON_ERROR(nvs_flash_erase(), TAG, "erase NVS");
        ret = nvs_flash_init();
    }
    return ret;
}

void app_main(void)
{
    ESP_LOGI(TAG, "gLOWCOST MPPC ESP32-P4 Wi-Fi readout");
    ESP_LOGW(TAG, "startup sequence: HV off, FPGA flash, DAC init, then HV 0x%02x after %d ms settle", STARTUP_HV_BYTE, HV_SETTLE_MS);

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

    ESP_ERROR_CHECK(start_wifi_ap());
    ESP_ERROR_CHECK(start_webserver());
    xTaskCreatePinnedToCore(auto_power_save_task, "auto_power_save_task", 3072, NULL, 3, NULL, 1);
}
