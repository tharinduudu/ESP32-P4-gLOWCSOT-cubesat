#include "app_common.h"

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

// Print the small USB-serial command list for bench bring-up.
static void print_help(void)
{
    printf("commands: help | status | i2c scan | counts | sd status | sd list | sd tail <file|current|env> [lines] | fpga | dac zero | dac <ch 0-7> <hex16> | hv off | hv <hex8>\n");
}

// Return the filename portion of a full path.
static const char *console_basename(const char *path)
{
    const char *base = strrchr(path, '/');
    return base ? base + 1 : path;
}

// Keep serial SD diagnostics limited to normal log filenames.
static bool console_sd_name_allowed(const char *name)
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

// Build a full SD path after filename validation.
static bool console_make_sd_path(const char *name, char *path, size_t path_len)
{
    const char *prefix = SD_MOUNT_POINT "/";
    size_t prefix_len = strlen(prefix);
    size_t name_len = strlen(name);
    if (prefix_len + name_len + 1 > path_len) {
        return false;
    }
    memcpy(path, prefix, prefix_len);
    memcpy(path + prefix_len, name, name_len + 1);
    return true;
}

// Print SD mount state and the active file names over USB serial.
static void print_sd_status(void)
{
    bool mounted = false;
    char muon[sizeof(s_log_path)] = "";
    char env[sizeof(s_env_log_path)] = "";
    if (s_sd_mutex) {
        xSemaphoreTake(s_sd_mutex, portMAX_DELAY);
        mounted = s_sd_mounted;
        snprintf(muon, sizeof(muon), "%s", s_log_path);
        snprintf(env, sizeof(env), "%s", s_env_log_path);
        xSemaphoreGive(s_sd_mutex);
    }
    printf("sd,status,mounted=%d,muon=%s,env=%s\n",
           mounted ? 1 : 0,
           muon[0] ? console_basename(muon) : "none",
           env[0] ? console_basename(env) : "none");
}

// List CSV/log files on the SD card without needing the Wi-Fi server.
static void print_sd_list(void)
{
    if (!s_sd_mutex) {
        printf("sd,list,error=not_initialized\n");
        return;
    }
    xSemaphoreTake(s_sd_mutex, portMAX_DELAY);
    bool mounted = s_sd_mounted;
    char current_muon[sizeof(s_log_path)] = "";
    char current_env[sizeof(s_env_log_path)] = "";
    snprintf(current_muon, sizeof(current_muon), "%s", console_basename(s_log_path));
    snprintf(current_env, sizeof(current_env), "%s", console_basename(s_env_log_path));
    if (!mounted) {
        xSemaphoreGive(s_sd_mutex);
        printf("sd,list,error=not_mounted\n");
        return;
    }

    DIR *dir = opendir(SD_MOUNT_POINT);
    if (!dir) {
        xSemaphoreGive(s_sd_mutex);
        printf("sd,list,error=open_dir_failed\n");
        return;
    }

    size_t count = 0;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (!console_sd_name_allowed(entry->d_name)) {
            continue;
        }
        size_t name_len = strlen(entry->d_name);
        if (name_len > SD_LOG_NAME_MAX) {
            continue;
        }
        char name[SD_LOG_NAME_MAX + 1];
        memcpy(name, entry->d_name, name_len + 1);
        char path[128];
        if (!console_make_sd_path(name, path, sizeof(path))) {
            continue;
        }
        struct stat st;
        if (stat(path, &st) != 0 || !S_ISREG(st.st_mode)) {
            continue;
        }
        bool current = strcmp(name, current_muon) == 0 || strcmp(name, current_env) == 0;
        printf("sd,file,name=%s,size=%lld,mtime=%lld,current=%d\n",
               name, (long long)st.st_size, (long long)st.st_mtime, current ? 1 : 0);
        count++;
    }
    closedir(dir);
    xSemaphoreGive(s_sd_mutex);
    printf("sd,list,count=%u\n", (unsigned)count);
}

// Resolve a diagnostic filename or alias into a full SD path.
static bool resolve_sd_tail_path(const char *name, char *path, size_t path_len)
{
    if (strcmp(name, "current") == 0) {
        if (!s_log_path[0]) {
            return false;
        }
        snprintf(path, path_len, "%s", s_log_path);
        return true;
    }
    if (strcmp(name, "env") == 0) {
        if (!s_env_log_path[0]) {
            return false;
        }
        snprintf(path, path_len, "%s", s_env_log_path);
        return true;
    }
    if (!console_sd_name_allowed(name)) {
        return false;
    }
    return console_make_sd_path(name, path, path_len);
}

// Print the last few lines of an SD log over USB serial.
static void print_sd_tail(const char *args)
{
    char name[SD_LOG_NAME_MAX + 1] = "";
    unsigned lines_requested = 10;
    if (sscanf(args, "%80s %u", name, &lines_requested) < 1) {
        printf("sd,tail,error=usage\n");
        return;
    }
    if (lines_requested == 0) {
        lines_requested = 10;
    }
    if (lines_requested > 20) {
        lines_requested = 20;
    }

    if (!s_sd_mutex) {
        printf("sd,tail,error=not_initialized\n");
        return;
    }
    xSemaphoreTake(s_sd_mutex, portMAX_DELAY);
    if (!s_sd_mounted) {
        xSemaphoreGive(s_sd_mutex);
        printf("sd,tail,error=not_mounted\n");
        return;
    }

    char path[128];
    if (!resolve_sd_tail_path(name, path, sizeof(path))) {
        xSemaphoreGive(s_sd_mutex);
        printf("sd,tail,error=bad_name\n");
        return;
    }

    FILE *f = fopen(path, "r");
    if (!f) {
        xSemaphoreGive(s_sd_mutex);
        printf("sd,tail,error=open_failed,file=%s\n", console_basename(path));
        return;
    }

    static char lines[20][192];
    for (unsigned i = 0; i < 20; i++) {
        lines[i][0] = '\0';
    }
    unsigned seen = 0;
    static char line[192];
    while (fgets(line, sizeof(line), f) != NULL) {
        snprintf(lines[seen % lines_requested], sizeof(lines[0]), "%s", line);
        seen++;
    }
    fclose(f);
    xSemaphoreGive(s_sd_mutex);

    unsigned out_count = seen < lines_requested ? seen : lines_requested;
    printf("sd,tail,file=%s,lines=%u,total_seen=%u\n", console_basename(path), out_count, seen);
    unsigned start = seen > out_count ? seen - out_count : 0;
    for (unsigned i = 0; i < out_count; i++) {
        unsigned index = (start + i) % lines_requested;
        printf("%s", lines[index]);
        if (lines[index][0] && lines[index][strlen(lines[index]) - 1] != '\n') {
            printf("\n");
        }
    }
}

// Probe a single I2C address by sending only the address byte. This is less
// invasive than reading an arbitrary register from every device on the bus.
static esp_err_t i2c_probe_addr(uint8_t addr)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    if (!cmd) {
        return ESP_ERR_NO_MEM;
    }
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_WRITE, true);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(BME280_I2C_PORT, cmd, pdMS_TO_TICKS(50));
    i2c_cmd_link_delete(cmd);
    return ret;
}

// Print a compact I2C address scan over USB serial for board bring-up.
static void print_i2c_scan(void)
{
    esp_err_t ret = init_i2c_bus();
    if (ret != ESP_OK) {
        printf("i2c,init,%s\n", esp_err_to_name(ret));
        return;
    }

    printf("i2c,scan");
    bool any = false;
    for (uint8_t addr = 0x08; addr <= 0x77; addr++) {
        if (i2c_probe_addr(addr) == ESP_OK) {
            printf(",0x%02x", addr);
            any = true;
        }
    }
    if (!any) {
        printf(",none");
    }
    printf("\n");
}

// Handle simple USB-serial maintenance commands without requiring the Wi-Fi UI.
void console_task(void *arg)
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
            printf("status,fpga_done=%d,hv=0x%02x,sd=%d,bme=%d,wifi_clients=%u,ip=http://192.168.4.1\n",
                   gpio_get_level(PIN_FPGA_DONE), s_hv_byte, s_sd_mounted, s_bme280_ok, s_wifi_clients);
        } else if (strcmp(cmd, "i2c scan") == 0) {
            print_i2c_scan();
        } else if (strcmp(cmd, "counts") == 0) {
            print_and_reset_counts();
        } else if (strcmp(cmd, "sd status") == 0) {
            print_sd_status();
        } else if (strcmp(cmd, "sd list") == 0) {
            print_sd_list();
        } else if (strncmp(cmd, "sd tail ", 8) == 0) {
            print_sd_tail(cmd + 8);
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
