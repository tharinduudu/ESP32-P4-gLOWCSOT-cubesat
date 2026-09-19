#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_err.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_st7789.h"
#include "esp_log.h"
#include "esp_pm.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "host/ble_gap.h"
#include "host/ble_hs.h"
#include "host/ble_hs_adv.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "nvs_flash.h"
#include "sdkconfig.h"

#define LCD_HOST                SPI2_HOST
#define LCD_H_RES               240
#define LCD_V_RES               135
#define PIN_LCD_CS              GPIO_NUM_10
#define PIN_LCD_MOSI            GPIO_NUM_11
#define PIN_LCD_SCLK            GPIO_NUM_12
#define PIN_LCD_DC              GPIO_NUM_8
#define PIN_LCD_RST             GPIO_NUM_9
#define PIN_LCD_BL              GPIO_NUM_7

#define COLOR_BLACK             0x0000
#define COLOR_WHITE             0xffff
#define COLOR_GREEN             0x07e0
#define COLOR_CYAN              0x07ff
#define COLOR_YELLOW            0xffe0
#define COLOR_RED               0xf800
#define COLOR_DIM               0x7bef

#define COUNT_CHANNELS          7
#define BLE_COMPANY_ID          0xFFFF
#define BLE_PAYLOAD_VERSION     1
#define BLE_PAYLOAD_LEN         26
#define DISPLAY_REFRESH_MS      1000
#define STALE_PACKET_MS         10000
#define BLE_SCAN_INTERVAL_MS    1000
#define BLE_SCAN_WINDOW_MS      250

#define BLE_STATUS_TIME_SET     BIT0
#define BLE_STATUS_COUNTING     BIT1
#define BLE_STATUS_FPGA_OK      BIT2
#define BLE_STATUS_SD_READY     BIT3

typedef struct {
    bool valid;
    uint32_t epoch;
    uint16_t counts[COUNT_CHANNELS];
    uint8_t status;
    uint8_t hv;
    uint8_t seq;
    int8_t rssi;
    int64_t last_seen_ms;
} ble_count_packet_t;

static const char *TAG = "s3_ble_tail";
static esp_lcd_panel_handle_t s_panel;
static uint16_t s_framebuffer[LCD_H_RES * LCD_V_RES];
static portMUX_TYPE s_latest_mux = portMUX_INITIALIZER_UNLOCKED;
static ble_count_packet_t s_latest;
static uint8_t s_ble_own_addr_type;

void ble_store_config_init(void);
static int ble_gap_event(struct ble_gap_event *event, void *arg);

// A tiny built-in 5x7 font keeps this display firmware self-contained. The LCD
// is only a quick field readout, so text needs to be reliable more than pretty.
static const uint8_t *glyph_for(char c)
{
    static const uint8_t blank[7] = {0, 0, 0, 0, 0, 0, 0};
    static const uint8_t glyphs[][7] = {
        ['0'] = {0x0e, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0e},
        ['1'] = {0x04, 0x0c, 0x04, 0x04, 0x04, 0x04, 0x0e},
        ['2'] = {0x0e, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1f},
        ['3'] = {0x1e, 0x01, 0x01, 0x0e, 0x01, 0x01, 0x1e},
        ['4'] = {0x02, 0x06, 0x0a, 0x12, 0x1f, 0x02, 0x02},
        ['5'] = {0x1f, 0x10, 0x10, 0x1e, 0x01, 0x01, 0x1e},
        ['6'] = {0x0e, 0x10, 0x10, 0x1e, 0x11, 0x11, 0x0e},
        ['7'] = {0x1f, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08},
        ['8'] = {0x0e, 0x11, 0x11, 0x0e, 0x11, 0x11, 0x0e},
        ['9'] = {0x0e, 0x11, 0x11, 0x0f, 0x01, 0x01, 0x0e},
        ['A'] = {0x0e, 0x11, 0x11, 0x1f, 0x11, 0x11, 0x11},
        ['B'] = {0x1e, 0x11, 0x11, 0x1e, 0x11, 0x11, 0x1e},
        ['C'] = {0x0f, 0x10, 0x10, 0x10, 0x10, 0x10, 0x0f},
        ['D'] = {0x1e, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1e},
        ['E'] = {0x1f, 0x10, 0x10, 0x1e, 0x10, 0x10, 0x1f},
        ['F'] = {0x1f, 0x10, 0x10, 0x1e, 0x10, 0x10, 0x10},
        ['G'] = {0x0f, 0x10, 0x10, 0x13, 0x11, 0x11, 0x0f},
        ['H'] = {0x11, 0x11, 0x11, 0x1f, 0x11, 0x11, 0x11},
        ['I'] = {0x0e, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0e},
        ['J'] = {0x01, 0x01, 0x01, 0x01, 0x11, 0x11, 0x0e},
        ['K'] = {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11},
        ['L'] = {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1f},
        ['M'] = {0x11, 0x1b, 0x15, 0x15, 0x11, 0x11, 0x11},
        ['N'] = {0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11},
        ['O'] = {0x0e, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0e},
        ['P'] = {0x1e, 0x11, 0x11, 0x1e, 0x10, 0x10, 0x10},
        ['Q'] = {0x0e, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0d},
        ['R'] = {0x1e, 0x11, 0x11, 0x1e, 0x14, 0x12, 0x11},
        ['S'] = {0x0f, 0x10, 0x10, 0x0e, 0x01, 0x01, 0x1e},
        ['T'] = {0x1f, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04},
        ['U'] = {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0e},
        ['V'] = {0x11, 0x11, 0x11, 0x11, 0x11, 0x0a, 0x04},
        ['W'] = {0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0a},
        ['X'] = {0x11, 0x11, 0x0a, 0x04, 0x0a, 0x11, 0x11},
        ['Y'] = {0x11, 0x11, 0x0a, 0x04, 0x04, 0x04, 0x04},
        ['Z'] = {0x1f, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1f},
        [':'] = {0, 0x04, 0x04, 0, 0x04, 0x04, 0},
        ['-'] = {0, 0, 0, 0x1f, 0, 0, 0},
        ['.'] = {0, 0, 0, 0, 0, 0x0c, 0x0c},
        [','] = {0, 0, 0, 0, 0, 0x04, 0x08},
        ['/'] = {0x01, 0x01, 0x02, 0x04, 0x08, 0x10, 0x10},
        ['_'] = {0, 0, 0, 0, 0, 0, 0x1f},
        ['='] = {0, 0, 0x1f, 0, 0x1f, 0, 0},
        [' '] = {0, 0, 0, 0, 0, 0, 0},
    };
    if (c >= 'a' && c <= 'z') {
        c -= 32;
    }
    if ((unsigned char)c < sizeof(glyphs) / sizeof(glyphs[0])) {
        for (int i = 0; i < 7; i++) {
            if (glyphs[(unsigned char)c][i]) {
                return glyphs[(unsigned char)c];
            }
        }
    }
    if (c == ' ') {
        return glyphs[(unsigned char)c];
    }
    return blank;
}

// Fill the whole framebuffer before composing the next screen.
static void fb_clear(uint16_t color)
{
    for (size_t i = 0; i < LCD_H_RES * LCD_V_RES; i++) {
        s_framebuffer[i] = color;
    }
}

// Set one framebuffer pixel if it lands inside the panel bounds.
static void fb_pixel(int x, int y, uint16_t color)
{
    if (x >= 0 && x < LCD_H_RES && y >= 0 && y < LCD_V_RES) {
        s_framebuffer[y * LCD_H_RES + x] = color;
    }
}

// Draw a simple scaled text string into the framebuffer.
static void fb_text(int x, int y, const char *text, int scale, uint16_t color)
{
    int cursor = x;
    while (*text && cursor < LCD_H_RES - 4) {
        const uint8_t *glyph = glyph_for(*text++);
        for (int row = 0; row < 7; row++) {
            for (int col = 0; col < 5; col++) {
                if (glyph[row] & (1 << (4 - col))) {
                    for (int yy = 0; yy < scale; yy++) {
                        for (int xx = 0; xx < scale; xx++) {
                            fb_pixel(cursor + col * scale + xx, y + row * scale + yy, color);
                        }
                    }
                }
            }
        }
        cursor += 6 * scale;
    }
}

// Send the composed framebuffer to the ST7789 panel.
static void lcd_flush(void)
{
    esp_lcd_panel_draw_bitmap(s_panel, 0, 0, LCD_H_RES, LCD_V_RES, s_framebuffer);
}

// Draw a short status screen for boot/scanning/error states.
static void draw_message(const char *title, const char *line1, const char *line2, uint16_t accent)
{
    fb_clear(COLOR_BLACK);
    fb_text(4, 4, title, 2, accent);
    fb_text(4, 28, line1, 1, COLOR_WHITE);
    fb_text(4, 42, line2, 1, COLOR_DIM);
    lcd_flush();
}

// Bring up the SPI LCD and leave it in landscape orientation for the wrist-size
// "tail -f" detector readout.
static esp_err_t lcd_init(void)
{
    gpio_config_t bl_cfg = {
        .pin_bit_mask = 1ULL << PIN_LCD_BL,
        .mode = GPIO_MODE_OUTPUT,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&bl_cfg), TAG, "backlight gpio");
    gpio_set_level(PIN_LCD_BL, 1);

    spi_bus_config_t buscfg = {
        .sclk_io_num = PIN_LCD_SCLK,
        .mosi_io_num = PIN_LCD_MOSI,
        .miso_io_num = -1,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = LCD_H_RES * LCD_V_RES * sizeof(uint16_t),
    };
    ESP_RETURN_ON_ERROR(spi_bus_initialize(LCD_HOST, &buscfg, SPI_DMA_CH_AUTO), TAG, "lcd spi bus");

    esp_lcd_panel_io_handle_t io = NULL;
    esp_lcd_panel_io_spi_config_t io_config = {
        .dc_gpio_num = PIN_LCD_DC,
        .cs_gpio_num = PIN_LCD_CS,
        .pclk_hz = 40 * 1000 * 1000,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .spi_mode = 0,
        .trans_queue_depth = 10,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST, &io_config, &io), TAG, "lcd io");

    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = PIN_LCD_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR,
        .bits_per_pixel = 16,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_st7789(io, &panel_config, &s_panel), TAG, "st7789 panel");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(s_panel), TAG, "lcd reset");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(s_panel), TAG, "lcd init");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_invert_color(s_panel, true), TAG, "lcd invert");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_swap_xy(s_panel, true), TAG, "lcd swap xy");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_mirror(s_panel, false, true), TAG, "lcd mirror");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_set_gap(s_panel, 40, 52), TAG, "lcd gap");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(s_panel, true), TAG, "lcd on");

    draw_message("MUON BLE", "BOOTING DISPLAY", "PASSIVE SCAN", COLOR_CYAN);
    return ESP_OK;
}

// Return a little-endian 16-bit value from the BLE payload.
static uint16_t get_u16(const uint8_t *src)
{
    return (uint16_t)src[0] | ((uint16_t)src[1] << 8);
}

// Return a little-endian 32-bit value from the BLE payload.
static uint32_t get_u32(const uint8_t *src)
{
    return (uint32_t)src[0] |
           ((uint32_t)src[1] << 8) |
           ((uint32_t)src[2] << 16) |
           ((uint32_t)src[3] << 24);
}

// Decode the compact live-count packet emitted by the P4.
static bool parse_muon_payload(const uint8_t *data, uint8_t len, ble_count_packet_t *out)
{
    size_t off = 0;

    if (len < BLE_PAYLOAD_LEN || get_u16(&data[0]) != BLE_COMPANY_ID) {
        return false;
    }
    off += 2;
    if (data[off++] != 'M' || data[off++] != 'U') {
        return false;
    }
    if (data[off++] != BLE_PAYLOAD_VERSION) {
        return false;
    }

    memset(out, 0, sizeof(*out));
    out->valid = true;
    out->seq = data[off++];
    out->epoch = get_u32(&data[off]);
    off += 4;
    for (size_t i = 0; i < COUNT_CHANNELS; i++) {
        out->counts[i] = get_u16(&data[off]);
        off += 2;
    }
    out->status = data[off++];
    out->hv = data[off++];
    out->last_seen_ms = esp_timer_get_time() / 1000;
    return true;
}

// Copy a newly received packet into shared state for the display task.
static void store_latest_packet(const ble_count_packet_t *packet)
{
    portENTER_CRITICAL(&s_latest_mux);
    s_latest = *packet;
    portEXIT_CRITICAL(&s_latest_mux);
}

// Copy the latest packet out of shared state so rendering never holds a lock.
static ble_count_packet_t load_latest_packet(void)
{
    ble_count_packet_t packet;
    portENTER_CRITICAL(&s_latest_mux);
    packet = s_latest;
    portEXIT_CRITICAL(&s_latest_mux);
    return packet;
}

// Start a passive, low-duty scan. Duplicate filtering is disabled because the
// P4 updates the manufacturer data while using the same advertiser address.
static void start_ble_scan(void)
{
    struct ble_gap_disc_params params = {0};
    params.passive = 1;
    params.filter_duplicates = 0;
    params.itvl = BLE_GAP_SCAN_ITVL_MS(BLE_SCAN_INTERVAL_MS);
    params.window = BLE_GAP_SCAN_WIN_MS(BLE_SCAN_WINDOW_MS);
    params.filter_policy = 0;
    params.limited = 0;

    int rc = ble_gap_disc(s_ble_own_addr_type, BLE_HS_FOREVER, &params, ble_gap_event, NULL);
    if (rc != 0 && rc != BLE_HS_EALREADY) {
        ESP_LOGW(TAG, "BLE scan start failed rc=%d", rc);
    }
}

// Handle BLE discovery packets from NimBLE and keep only the detector payloads.
static int ble_gap_event(struct ble_gap_event *event, void *arg)
{
    (void)arg;

    switch (event->type) {
    case BLE_GAP_EVENT_DISC: {
        struct ble_hs_adv_fields fields = {0};
        ble_count_packet_t packet;
        int rc = ble_hs_adv_parse_fields(&fields, event->disc.data, event->disc.length_data);
        if (rc == 0 &&
            fields.mfg_data != NULL &&
            parse_muon_payload(fields.mfg_data, fields.mfg_data_len, &packet)) {
            packet.rssi = event->disc.rssi;
            store_latest_packet(&packet);
        }
        return 0;
    }

    case BLE_GAP_EVENT_DISC_COMPLETE:
        start_ble_scan();
        return 0;

    default:
        return 0;
    }
}

// Restart passive scanning when the BLE host and controller are synchronized.
static void ble_on_sync(void)
{
    int rc = ble_hs_util_ensure_addr(0);
    if (rc != 0) {
        ESP_LOGW(TAG, "BLE ensure address failed rc=%d", rc);
        return;
    }

    rc = ble_hs_id_infer_auto(0, &s_ble_own_addr_type);
    if (rc != 0) {
        ESP_LOGW(TAG, "BLE address type failed rc=%d", rc);
        return;
    }

    start_ble_scan();
    ESP_LOGI(TAG, "BLE passive scan started");
}

// Log a hosted/controller reset. NimBLE will call sync again when it recovers.
static void ble_on_reset(int reason)
{
    ESP_LOGW(TAG, "BLE host reset reason=%d", reason);
}

// FreeRTOS task that runs the NimBLE host loop.
static void ble_host_task(void *arg)
{
    (void)arg;
    nimble_port_run();
    nimble_port_freertos_deinit();
}

// Initialize the BLE observer role used by the screen readout.
static esp_err_t ble_init(void)
{
    esp_err_t ret = nimble_port_init();
    if (ret != ESP_OK) {
        return ret;
    }

    ble_hs_cfg.reset_cb = ble_on_reset;
    ble_hs_cfg.sync_cb = ble_on_sync;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;
    ble_store_config_init();
    nimble_port_freertos_init(ble_host_task);
    return ESP_OK;
}

// Format the P4's timestamp for display. If the P4 has not been time-synced,
// the status bits make that obvious.
static void format_time(uint32_t epoch, char *out, size_t out_len)
{
    time_t t = (time_t)epoch;
    struct tm tm = {0};
    gmtime_r(&t, &tm);
    strftime(out, out_len, "%H:%M:%S", &tm);
}

// Draw the most recent BLE live-count packet as a compact detector dashboard.
static void render_packet(const ble_count_packet_t *packet)
{
    char row[64];
    char time_text[16];
    int64_t now_ms = esp_timer_get_time() / 1000;
    bool stale = !packet->valid || (now_ms - packet->last_seen_ms) > STALE_PACKET_MS;
    bool counting = packet->status & BLE_STATUS_COUNTING;
    bool sd_ready = packet->status & BLE_STATUS_SD_READY;
    bool fpga_ok = packet->status & BLE_STATUS_FPGA_OK;
    bool time_set = packet->status & BLE_STATUS_TIME_SET;

    if (!packet->valid) {
        draw_message("MUON BLE", "SCANNING", "WAITING FOR P4", COLOR_YELLOW);
        return;
    }

    format_time(packet->epoch, time_text, sizeof(time_text));
    fb_clear(COLOR_BLACK);
    fb_text(4, 2, "LIVE COINC", 1, stale ? COLOR_RED : COLOR_CYAN);
    snprintf(row, sizeof(row), "RSSI %d", packet->rssi);
    fb_text(168, 2, row, 1, COLOR_DIM);

    snprintf(row, sizeof(row), "01 %5u", packet->counts[0]);
    fb_text(4, 20, row, 2, COLOR_YELLOW);
    snprintf(row, sizeof(row), "02 %5u", packet->counts[1]);
    fb_text(124, 20, row, 2, COLOR_YELLOW);
    snprintf(row, sizeof(row), "12 %5u", packet->counts[2]);
    fb_text(4, 48, row, 2, COLOR_WHITE);
    snprintf(row, sizeof(row), "012%5u", packet->counts[3]);
    fb_text(124, 48, row, 2, COLOR_WHITE);

    snprintf(row, sizeof(row), "G6 %u  G5 %u  G16 %u", packet->counts[4], packet->counts[5], packet->counts[6]);
    fb_text(4, 78, row, 1, COLOR_DIM);
    snprintf(row, sizeof(row), "HV %02X  %s  %s", packet->hv, counting ? "COUNT" : "WAIT", sd_ready ? "SD OK" : "NO SD");
    fb_text(4, 94, row, 1, counting && sd_ready ? COLOR_GREEN : COLOR_YELLOW);
    snprintf(row, sizeof(row), "%s  %s  T %s  SEQ %u",
             fpga_ok ? "FPGA OK" : "FPGA BAD",
             time_set ? "TIME OK" : "NO TIME",
             time_text,
             packet->seq);
    fb_text(4, 110, row, 1, (fpga_ok && time_set) ? COLOR_WHITE : COLOR_YELLOW);
    if (stale) {
        fb_text(4, 126, "STALE BLE PACKET", 1, COLOR_RED);
    } else {
        snprintf(row, sizeof(row), "UPDATED %" PRId64 "S AGO", (now_ms - packet->last_seen_ms) / 1000);
        fb_text(4, 126, row, 1, COLOR_DIM);
    }
    lcd_flush();
}

// Periodically redraw the LCD from the latest received packet.
static void display_task(void *arg)
{
    (void)arg;
    while (true) {
        ble_count_packet_t packet = load_latest_packet();
        render_packet(&packet);
        vTaskDelay(pdMS_TO_TICKS(DISPLAY_REFRESH_MS));
    }
}

// Use a conservative CPU profile. The LCD backlight dominates this little board,
// but lower CPU speed and tickless idle still help when it is just listening.
static void configure_power_profile(void)
{
#if CONFIG_PM_ENABLE
    esp_pm_config_t pm_config = {
        .max_freq_mhz = 80,
        .min_freq_mhz = 40,
        .light_sleep_enable = true,
    };
    esp_err_t ret = esp_pm_configure(&pm_config);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "power profile not applied: %s", esp_err_to_name(ret));
    }
#endif
}

// Prepare NVS for NimBLE storage and recover if the flash layout changed.
static esp_err_t init_nvs(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_RETURN_ON_ERROR(nvs_flash_erase(), TAG, "erase nvs");
        ret = nvs_flash_init();
    }
    return ret;
}

void app_main(void)
{
    ESP_ERROR_CHECK(init_nvs());
    configure_power_profile();
    ESP_ERROR_CHECK(lcd_init());
    ESP_ERROR_CHECK(ble_init());
    xTaskCreatePinnedToCore(display_task, "display_task", 4096, NULL, 4, NULL, 1);
}
