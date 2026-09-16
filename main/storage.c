#include "app_common.h"

// Convert a user-entered run label into a short filename-safe token.
void sanitize_run_label(const char *input, char *output, size_t output_len)
{
    // Labels end up in filenames, so keep them friendly to FAT filesystems and
    // easy to type later in analysis scripts.
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

// Decode the small subset of URL encoding used by query strings from the
// built-in web form.
void url_decode_in_place(char *s)
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

// Format an epoch timestamp for CSV output, using "unset" while the device has
// not received browser time yet.
void csv_time_string(time_t epoch, char *out, size_t out_len)
{
    if (epoch > 1600000000) {
        struct tm tm;
        localtime_r(&epoch, &tm);
        strftime(out, out_len, "%Y-%m-%dT%H:%M:%S", &tm);
    } else {
        snprintf(out, out_len, "unset");
    }
}

// Build the muon-count CSV header from the active channel list.
void count_csv_header(char *out, size_t out_len)
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

// Construct the full SD path for either the muon or environment file, including
// optional run label and collision suffix.
static void sd_build_candidate_path(const char *prefix, char *path, size_t path_len, unsigned suffix)
{
    char stem[80];
    // Before browser time sync, create an "unsynced" file. Once time arrives,
    // the active files are renamed so the final run has a real start timestamp.
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

// Create a new CSV and write its header, leaving existing files untouched.
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

// Refresh one active SD filename while the caller holds the SD mutex. This also
// handles renaming an unsynced file after the clock becomes valid.
static esp_err_t sd_refresh_one_path_locked(char *path, size_t path_len, const char *prefix, const char *header)
{
    if (!s_sd_mounted) {
        return ESP_ERR_INVALID_STATE;
    }

    char new_path[128];
    // Never append to an older run by accident. If a filename already exists,
    // walk suffixes until this boot gets a fresh file.
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

// Refresh both active run files under the SD lock: the muon count file and the
// slower environment-average file.
esp_err_t sd_refresh_log_path_locked(void)
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

// Public wrapper for renaming/creating SD files when a web request changes time
// or the run label.
esp_err_t sd_refresh_log_path(void)
{
    if (!s_sd_mutex) {
        return ESP_ERR_INVALID_STATE;
    }
    xSemaphoreTake(s_sd_mutex, portMAX_DELAY);
    esp_err_t ret = sd_refresh_log_path_locked();
    xSemaphoreGive(s_sd_mutex);
    return ret;
}

// Mount the onboard SD card over its dedicated SPI pins and create fresh output
// files for this boot.
esp_err_t init_sd_card(void)
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

// Append one completed one-minute detector record to the active muon CSV file.
void sd_append_record(const count_record_t *record)
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
    // Keep the SD CSV compatible with the Pi-era log style: epoch, ISO time,
    // then the detector channels. Uptime stays internal only.
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

// Append one five-minute BME280 average to the active environment CSV file.
void sd_append_env_average(time_t epoch, uint32_t samples, double temp_c, double pressure_hpa, double humidity_pct)
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
