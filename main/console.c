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
    printf("commands: help | status | counts | fpga | dac zero | dac <ch 0-7> <hex16> | hv off | hv <hex8>\n");
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
