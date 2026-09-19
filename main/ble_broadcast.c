#include "app_common.h"

#include "host/ble_gap.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "services/gap/ble_svc_gap.h"

// Provided by NimBLE's storage helper component. We do not bond, but the host
// still wants its normal storage callbacks installed.
void ble_store_config_init(void);

#define BLE_STATUS_TIME_SET     BIT0
#define BLE_STATUS_COUNTING     BIT1
#define BLE_STATUS_FPGA_OK      BIT2
#define BLE_STATUS_SD_READY     BIT3
#define BLE_STATUS_BME_OK       BIT4

static uint8_t s_ble_own_addr_type;
static volatile bool s_ble_ready;
static uint8_t s_ble_seq;

// Store little-endian integers in the advertisement payload. BLE manufacturer
// data is just bytes, so making the byte order explicit keeps both firmwares in
// agreement if this packet gets decoded on a laptop later.
static void put_u16(uint8_t *dst, uint16_t value)
{
    dst[0] = (uint8_t)(value & 0xff);
    dst[1] = (uint8_t)(value >> 8);
}

// Store a 32-bit value in little-endian order for the display receiver.
static void put_u32(uint8_t *dst, uint32_t value)
{
    dst[0] = (uint8_t)(value & 0xff);
    dst[1] = (uint8_t)((value >> 8) & 0xff);
    dst[2] = (uint8_t)((value >> 16) & 0xff);
    dst[3] = (uint8_t)((value >> 24) & 0xff);
}

// Keep the BLE packet compact. The display is for live checking, while the SD
// card keeps the exact long-term record, so saturated values are acceptable.
static uint16_t clamp_count(uint32_t value)
{
    return value > UINT16_MAX ? UINT16_MAX : (uint16_t)value;
}

// Build the manufacturer data for one advertisement. The full legacy BLE
// advertising packet is only 31 bytes, so this intentionally omits names and
// text labels in favor of all seven live counters.
static size_t fill_manufacturer_payload(uint8_t payload[26])
{
    uint32_t counts[COUNT_CHANNELS];
    uint8_t status = 0;
    uint8_t hv = 0;
    bool time_set = false;
    bool counting = false;
    bool fpga_ok = false;
    bool sd_ready = false;
    bool bme_ok = false;
    time_t epoch = time(NULL);
    size_t off = 0;

    snapshot_counts(counts, false);

    portENTER_CRITICAL(&s_state_mux);
    time_set = s_time_set;
    counting = s_counting_enabled;
    fpga_ok = s_fpga_ok;
    sd_ready = s_sd_mounted;
    bme_ok = s_bme280_ok;
    hv = s_hv_byte;
    portEXIT_CRITICAL(&s_state_mux);

    if (time_set) {
        status |= BLE_STATUS_TIME_SET;
    }
    if (counting) {
        status |= BLE_STATUS_COUNTING;
    }
    if (fpga_ok) {
        status |= BLE_STATUS_FPGA_OK;
    }
    if (sd_ready) {
        status |= BLE_STATUS_SD_READY;
    }
    if (bme_ok) {
        status |= BLE_STATUS_BME_OK;
    }

    put_u16(&payload[off], BLE_COMPANY_ID);
    off += 2;
    payload[off++] = 'M';
    payload[off++] = 'U';
    payload[off++] = BLE_PAYLOAD_VERSION;
    payload[off++] = s_ble_seq++;
    put_u32(&payload[off], (uint32_t)epoch);
    off += 4;

    for (size_t i = 0; i < COUNT_CHANNELS; i++) {
        put_u16(&payload[off], clamp_count(counts[i]));
        off += 2;
    }

    payload[off++] = status;
    payload[off++] = hv;
    return off;
}

// Push the latest detector state into the BLE advertisement. Restarting the
// non-connectable advertisement is short, and avoids any connection traffic
// from the display ESP.
static void refresh_advertisement(void)
{
    struct ble_hs_adv_fields fields = {0};
    struct ble_gap_adv_params params = {0};
    uint8_t payload[26] = {0};
    int rc;

    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.mfg_data = payload;
    fields.mfg_data_len = fill_manufacturer_payload(payload);

    if (ble_gap_adv_active()) {
        rc = ble_gap_adv_stop();
        if (rc != 0) {
            ESP_LOGW(TAG, "BLE adv stop failed rc=%d", rc);
        }
    }

    rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        ESP_LOGW(TAG, "BLE adv field update failed rc=%d", rc);
        return;
    }

    params.conn_mode = BLE_GAP_CONN_MODE_NON;
    params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    params.itvl_min = BLE_GAP_ADV_ITVL_MS(BLE_ADV_INTERVAL_MS);
    params.itvl_max = BLE_GAP_ADV_ITVL_MS(BLE_ADV_INTERVAL_MS + 50);

    rc = ble_gap_adv_start(s_ble_own_addr_type, NULL, BLE_HS_FOREVER, &params, NULL, NULL);
    if (rc != 0) {
        ESP_LOGW(TAG, "BLE adv start failed rc=%d", rc);
    }
}

// Keep advertisements fresh without touching the one-minute integration window.
// This task only reads counters, so it cannot create detector dead time.
static void ble_payload_task(void *arg)
{
    (void)arg;
    while (!s_ble_ready) {
        vTaskDelay(pdMS_TO_TICKS(250));
    }

    while (true) {
        refresh_advertisement();
        vTaskDelay(pdMS_TO_TICKS(BLE_PAYLOAD_UPDATE_MS));
    }
}

// NimBLE calls this when the hosted Bluetooth controller resets.
static void ble_on_reset(int reason)
{
    s_ble_ready = false;
    ESP_LOGW(TAG, "BLE host reset reason=%d", reason);
}

// NimBLE calls this when the host/controller link is ready. From this point the
// P4 can advertise live detector packets through the hosted radio.
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

    s_ble_ready = true;
    refresh_advertisement();
    ESP_LOGW(TAG, "BLE live-count broadcaster started");
}

// FreeRTOS task that runs the NimBLE host loop.
static void ble_host_task(void *arg)
{
    (void)arg;
    nimble_port_run();
    nimble_port_freertos_deinit();
}

// Initialize hosted BLE advertising for the small-screen display ESP. If the
// hosted radio is unavailable the detector still keeps counting and logging.
esp_err_t init_ble_broadcast(void)
{
    esp_err_t ret = nimble_port_init();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "NimBLE init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ble_hs_cfg.reset_cb = ble_on_reset;
    ble_hs_cfg.sync_cb = ble_on_sync;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;

    int rc = ble_svc_gap_device_name_set(BLE_DEVICE_NAME);
    if (rc != 0) {
        ESP_LOGW(TAG, "BLE name set failed rc=%d", rc);
    }

    ble_store_config_init();
    nimble_port_freertos_init(ble_host_task);

    BaseType_t ok = xTaskCreatePinnedToCore(ble_payload_task, "ble_payload_task", 3072, NULL, 4, NULL, 1);
    return ok == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}
