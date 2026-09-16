#include "app_common.h"

// GPIO interrupt hook for the FPGA counter outputs. It only increments a RAM
// counter so pulse handling stays fast and deterministic.
static void IRAM_ATTR count_isr(void *arg)
{
    // Keep this ISR boring: one bounds check and one increment. Anything slower
    // belongs in the minute task so Wi-Fi/web traffic cannot stretch dead time.
    uintptr_t index = (uintptr_t)arg;
    if (s_counting_enabled && index < COUNT_CHANNELS) {
        portENTER_CRITICAL_ISR(&s_count_mux);
        s_counts[index]++;
        portEXIT_CRITICAL_ISR(&s_count_mux);
    }
}


// Configure the FPGA output pins as rising-edge interrupt inputs and attach the
// small ISR used for live counting.
esp_err_t init_counters(void)
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


// Copy the live interrupt counters into a caller buffer, optionally clearing
// them for the next integration window.
void snapshot_counts(uint32_t out[COUNT_CHANNELS], bool reset)
{
    // The reset option gives the one-minute task an atomic "read and clear"
    // operation while the web page can still take non-destructive snapshots.
    portENTER_CRITICAL(&s_count_mux);
    for (size_t i = 0; i < COUNT_CHANNELS; i++) {
        out[i] = s_counts[i];
        if (reset) {
            s_counts[i] = 0;
        }
    }
    portEXIT_CRITICAL(&s_count_mux);
}

// Store a completed minute into the RAM ring buffer, update totals, and hand the
// same record to the SD logger.
static void append_log_record(const uint32_t counts[COUNT_CHANNELS])
{
    count_record_t record = {
        .uptime_ms = esp_timer_get_time() / 1000,
        .epoch = time(NULL),
    };
    memcpy(record.counts, counts, sizeof(record.counts));

    // RAM keeps only the recent tail for the web UI. The SD card remains the
    // long-term record, so losing old RAM entries is expected.
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

// End the current minute window, write the result, and print it over USB unless
// the detector has entered quiet/power-saving mode.
void print_and_reset_counts(void)
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

// Background integration loop. It waits for HV to be stable, then records one
// detector row every COUNTER_PERIOD_MS.
void counter_task(void *arg)
{
    (void)arg;
    while (true) {
        // Do not start the one-minute integration window until HV has settled.
        // Otherwise the first row after startup would include bias ramp noise.
        while (!counting_is_enabled()) {
            clear_live_counts();
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
        vTaskDelay(pdMS_TO_TICKS(COUNTER_PERIOD_MS));
        print_and_reset_counts();
    }
}
