#include "app_common.h"

#include <stdarg.h>

// The CH343 USB-UART is deliberately separate from ESP-IDF's logging console.
// That keeps routine log traffic out of the flight monitor protocol and lets a
// laptop reconnect without changing the detector's counter integration state.

static void uart_reply(const char *format, ...)
{
    char buf[512];
    va_list args;
    va_start(args, format);
    int len = vsnprintf(buf, sizeof(buf), format, args);
    va_end(args);
    if (len <= 0) {
        return;
    }
    size_t send_len = (size_t)len < sizeof(buf) ? (size_t)len : sizeof(buf) - 1;
    uart_write_bytes(UART_MONITOR_PORT, buf, send_len);
}

static const char *uart_basename(const char *path)
{
    const char *base = strrchr(path, '/');
    return base ? base + 1 : path;
}

static bool uart_sd_name_allowed(const char *name)
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

static void uart_send_status(void)
{
    char *json = malloc(4096);
    if (!json) {
        uart_reply("@error\tout_of_memory\n");
        return;
    }
    size_t len = format_status_json(json, 4096);
    uart_write_bytes(UART_MONITOR_PORT, "@json\t", 6);
    uart_write_bytes(UART_MONITOR_PORT, json, len);
    uart_write_bytes(UART_MONITOR_PORT, "\n", 1);
    free(json);
}

static void uart_send_file_list(void)
{
    if (!s_sd_mutex) {
        uart_reply("@error\tsd_not_initialized\n@end\n");
        return;
    }

    xSemaphoreTake(s_sd_mutex, portMAX_DELAY);
    bool mounted = s_sd_mounted;
    char current_muon[sizeof(s_log_path)] = "";
    char current_env[sizeof(s_env_log_path)] = "";
    snprintf(current_muon, sizeof(current_muon), "%s", uart_basename(s_log_path));
    snprintf(current_env, sizeof(current_env), "%s", uart_basename(s_env_log_path));
    if (!mounted) {
        xSemaphoreGive(s_sd_mutex);
        uart_reply("@error\tsd_not_mounted\n@end\n");
        return;
    }

    DIR *dir = opendir(SD_MOUNT_POINT);
    if (!dir) {
        xSemaphoreGive(s_sd_mutex);
        uart_reply("@error\tsd_open_failed\n@end\n");
        return;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (!uart_sd_name_allowed(entry->d_name)) {
            continue;
        }
        size_t name_len = strlen(entry->d_name);
        char name[SD_LOG_NAME_MAX + 1];
        memcpy(name, entry->d_name, name_len + 1);
        char path[128];
        snprintf(path, sizeof(path), SD_MOUNT_POINT "/%s", name);
        struct stat st;
        if (stat(path, &st) != 0 || !S_ISREG(st.st_mode)) {
            continue;
        }
        bool current = strcmp(name, current_muon) == 0 ||
                       strcmp(name, current_env) == 0;
        uart_reply("@file\t%s\t%lld\t%lld\t%d\n", name,
                   (long long)st.st_size, (long long)st.st_mtime, current ? 1 : 0);
    }
    closedir(dir);
    xSemaphoreGive(s_sd_mutex);
    uart_reply("@end\n");
}

static void uart_send_file(const char *name)
{
    if (!uart_sd_name_allowed(name)) {
        uart_reply("@error\tbad_file_name\n");
        return;
    }
    size_t name_len = strlen(name);
    char safe_name[SD_LOG_NAME_MAX + 1];
    memcpy(safe_name, name, name_len + 1);
    if (!s_sd_mutex) {
        uart_reply("@error\tsd_not_initialized\n");
        return;
    }

    xSemaphoreTake(s_sd_mutex, portMAX_DELAY);
    bool mounted = s_sd_mounted;
    xSemaphoreGive(s_sd_mutex);
    if (!mounted) {
        uart_reply("@error\tsd_not_mounted\n");
        return;
    }

    char path[128];
    snprintf(path, sizeof(path), SD_MOUNT_POINT "/%s", safe_name);
    FILE *file = fopen(path, "rb");
    if (!file) {
        uart_reply("@error\tfile_open_failed\n");
        return;
    }
    struct stat st;
    if (fstat(fileno(file), &st) != 0 || st.st_size < 0) {
        fclose(file);
        uart_reply("@error\tfile_stat_failed\n");
        return;
    }

    // The byte count makes the transfer binary-safe. The host reads exactly
    // this many bytes before looking for the final protocol line.
    uart_reply("@data\t%s\t%lld\n", safe_name, (long long)st.st_size);
    char chunk[512];
    off_t remaining = st.st_size;
    while (remaining > 0) {
        size_t want = remaining < (off_t)sizeof(chunk) ? (size_t)remaining : sizeof(chunk);
        size_t got = fread(chunk, 1, want, file);
        if (got == 0) {
            break;
        }
        uart_write_bytes(UART_MONITOR_PORT, chunk, got);
        remaining -= (off_t)got;
    }
    fclose(file);
    uart_write_bytes(UART_MONITOR_PORT, "\n@end\n", 6);
}

static void uart_handle_command(char *line)
{
    while (isspace((unsigned char)*line)) {
        line++;
    }
    char *end = line + strlen(line);
    while (end > line && isspace((unsigned char)end[-1])) {
        *--end = '\0';
    }

    if (strcmp(line, "@status") == 0) {
        uart_send_status();
    } else if (strcmp(line, "@files") == 0) {
        uart_send_file_list();
    } else if (strncmp(line, "@get ", 5) == 0) {
        uart_send_file(line + 5);
    } else if (strncmp(line, "@time ", 6) == 0) {
        long long epoch;
        int tz_offset;
        if (sscanf(line + 6, "%lld %d", &epoch, &tz_offset) == 2) {
            esp_err_t ret = apply_browser_time((time_t)epoch, true, tz_offset);
            uart_reply(ret == ESP_OK ? "@ok\ttime\n" : "@error\ttime_%s\n", esp_err_to_name(ret));
        } else {
            uart_reply("@error\ttime_usage\n");
        }
    } else if (strncmp(line, "@label ", 7) == 0) {
        esp_err_t ret = update_run_label(line + 7);
        uart_reply(ret == ESP_OK ? "@ok\tlabel\n" : "@error\tlabel_%s\n", esp_err_to_name(ret));
    } else if (strcmp(line, "@label") == 0) {
        esp_err_t ret = update_run_label("");
        uart_reply(ret == ESP_OK ? "@ok\tlabel\n" : "@error\tlabel_%s\n", esp_err_to_name(ret));
    } else if (strcmp(line, "@hv off") == 0) {
        esp_err_t ret = hv_write_and_settle(0x00);
        uart_reply(ret == ESP_OK ? "@ok\thv_00\n" : "@error\thv_%s\n", esp_err_to_name(ret));
    } else if (strncmp(line, "@hv ", 4) == 0) {
        unsigned value;
        if (sscanf(line + 4, "%x", &value) == 1 && value <= 0xff) {
            esp_err_t ret = hv_write_and_settle((uint8_t)value);
            if (ret == ESP_OK) {
                uart_reply("@ok\thv_%02x\n", value);
            } else {
                uart_reply("@error\thv_%s\n", esp_err_to_name(ret));
            }
        } else {
            uart_reply("@error\thv_usage\n");
        }
    } else if (strncmp(line, "@dac ", 5) == 0) {
        unsigned channel;
        unsigned value;
        if (sscanf(line + 5, "%u %x", &channel, &value) == 2 && channel < 8 && value <= 0xffff) {
            esp_err_t ret = dac_set_channel((uint8_t)channel, (uint16_t)value);
            if (ret == ESP_OK) {
                uart_reply("@ok\tdac_%u_%04x\n", channel, value);
            } else {
                uart_reply("@error\tdac_%s\n", esp_err_to_name(ret));
            }
        } else {
            uart_reply("@error\tdac_usage\n");
        }
    } else if (strcmp(line, "@fpga") == 0) {
        esp_err_t ret = safe_reflash_fpga();
        uart_reply(ret == ESP_OK ? "@ok\tfpga\n" : "@error\tfpga_%s\n", esp_err_to_name(ret));
    } else if (strcmp(line, "@wifi off") == 0) {
        uart_reply("@ok\twifi_off_started\n");
        esp_err_t ret = enter_power_save_mode();
        if (ret != ESP_OK) {
            uart_reply("@error\twifi_off_%s\n", esp_err_to_name(ret));
        }
    } else if (strcmp(line, "@ping") == 0) {
        uart_reply("@ok\tpong\n");
    } else {
        uart_reply("@error\tunknown_command\n");
    }
}

void uart_monitor_task(void *arg)
{
    (void)arg;
    const uart_config_t config = {
        .baud_rate = UART_MONITOR_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    esp_err_t ret = uart_driver_install(UART_MONITOR_PORT, 2048, 0, 0, NULL, 0);
    if (ret == ESP_OK) {
        ret = uart_param_config(UART_MONITOR_PORT, &config);
    }
    if (ret == ESP_OK) {
        ret = uart_set_pin(UART_MONITOR_PORT, UART_MONITOR_TX_PIN, UART_MONITOR_RX_PIN,
                           UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    }
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "UART flight monitor unavailable: %s", esp_err_to_name(ret));
        vTaskDelete(NULL);
        return;
    }

    uart_flush_input(UART_MONITOR_PORT);
    uart_reply("@ready\tbaud=%d\n", UART_MONITOR_BAUD);
    char line[192];
    size_t used = 0;
    while (true) {
        uint8_t byte;
        int read = uart_read_bytes(UART_MONITOR_PORT, &byte, 1, pdMS_TO_TICKS(250));
        if (read <= 0) {
            continue;
        }
        if (byte == '\r') {
            continue;
        }
        if (byte == '\n') {
            line[used] = '\0';
            if (used > 0) {
                uart_handle_command(line);
            }
            used = 0;
            continue;
        }
        if (used + 1 < sizeof(line)) {
            line[used++] = (char)byte;
        } else {
            used = 0;
            uart_reply("@error\tcommand_too_long\n");
        }
    }
}
