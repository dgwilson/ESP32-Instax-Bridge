/**
 * @file console.c
 * @brief Serial console for WiFi configuration and system control
 */

#include "console.h"
#include "wifi_manager.h"
#include "ble_scanner.h"
#include "spiffs_manager.h"
#include <string.h>
#include <stdio.h>
#include "esp_console.h"
#include "esp_log.h"
#include "esp_system.h"
#include "driver/uart.h"
#include "linenoise/linenoise.h"
#include "argtable3/argtable3.h"

static const char *TAG = "console";

// Command: wifi_set <ssid> <password>
static struct {
    struct arg_str *ssid;
    struct arg_str *password;
    struct arg_end *end;
} wifi_set_args;

static int cmd_wifi_set(int argc, char **argv) {
    int nerrors = arg_parse(argc, argv, (void **)&wifi_set_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, wifi_set_args.end, argv[0]);
        return 1;
    }

    const char *ssid = wifi_set_args.ssid->sval[0];
    const char *password = wifi_set_args.password->sval[0];

    esp_err_t ret = wifi_manager_set_credentials(ssid, password);
    if (ret == ESP_OK) {
        printf("WiFi credentials saved. Use 'wifi_connect' to connect.\n");
    } else {
        printf("Failed to save credentials: %s\n", esp_err_to_name(ret));
    }

    return ret == ESP_OK ? 0 : 1;
}

// Command: wifi_connect
static int cmd_wifi_connect(int argc, char **argv) {
    if (!wifi_manager_has_credentials()) {
        printf("No WiFi credentials stored. Use 'wifi_set <ssid> <password>' first.\n");
        return 1;
    }

    esp_err_t ret = wifi_manager_connect();
    if (ret == ESP_OK) {
        printf("Connecting to WiFi...\n");
    } else {
        printf("Failed to start connection: %s\n", esp_err_to_name(ret));
    }

    return ret == ESP_OK ? 0 : 1;
}

// Command: wifi_disconnect
static int cmd_wifi_disconnect(int argc, char **argv) {
    wifi_manager_disconnect();
    printf("WiFi disconnected.\n");
    return 0;
}

// Command: wifi_status
static int cmd_wifi_status(int argc, char **argv) {
    wifi_status_t status = wifi_manager_get_status();

    printf("WiFi Status: ");
    switch (status) {
        case WIFI_STATUS_DISCONNECTED:
            printf("Disconnected\n");
            break;
        case WIFI_STATUS_CONNECTING:
            printf("Connecting...\n");
            break;
        case WIFI_STATUS_CONNECTED: {
            char ip[16];
            if (wifi_manager_get_ip(ip) == ESP_OK) {
                printf("Connected (IP: %s)\n", ip);
            } else {
                printf("Connected\n");
            }
            break;
        }
        case WIFI_STATUS_FAILED:
            printf("Connection Failed\n");
            break;
    }

    char ssid[WIFI_SSID_MAX_LEN + 1];
    char password[WIFI_PASSWORD_MAX_LEN + 1];
    if (wifi_manager_get_credentials(ssid, password) == ESP_OK) {
        printf("Stored SSID: %s\n", ssid);
    } else {
        printf("No credentials stored.\n");
    }

    return 0;
}

// Command: wifi_clear
static int cmd_wifi_clear(int argc, char **argv) {
    wifi_manager_clear_credentials();
    printf("WiFi credentials cleared.\n");
    return 0;
}

// Command: ble_scan
static int cmd_ble_scan(int argc, char **argv) {
    printf("Starting BLE scan for 5 seconds...\n");
    ble_scanner_start_scan(5);
    return 0;
}

// Command: ble_devices
static int cmd_ble_devices(int argc, char **argv) {
    ble_discovered_device_t devices[MAX_DISCOVERED_PRINTERS];
    int count = ble_scanner_get_discovered(devices, MAX_DISCOVERED_PRINTERS);

    if (count == 0) {
        printf("No devices found. Run 'ble_scan' first.\n");
        return 0;
    }

    printf("Discovered devices:\n");
    for (int i = 0; i < count; i++) {
        printf("  %d: %s [%02x:%02x:%02x:%02x:%02x:%02x] RSSI=%d %s\n",
               i, devices[i].name,
               devices[i].address[0], devices[i].address[1], devices[i].address[2],
               devices[i].address[3], devices[i].address[4], devices[i].address[5],
               devices[i].rssi,
               devices[i].is_instax ? "(Instax)" : "");
    }

    return 0;
}

// Command: ble_status
static int cmd_ble_status(int argc, char **argv) {
    ble_state_t state = ble_scanner_get_state();

    printf("BLE Status: ");
    switch (state) {
        case BLE_STATE_IDLE:
            printf("Idle\n");
            break;
        case BLE_STATE_SCANNING:
            printf("Scanning\n");
            break;
        case BLE_STATE_CONNECTING:
            printf("Connecting\n");
            break;
        case BLE_STATE_CONNECTED:
            printf("Connected\n");
            break;
        case BLE_STATE_DISCONNECTED:
            printf("Disconnected\n");
            break;
        case BLE_STATE_ERROR:
            printf("Error\n");
            break;
    }

    return 0;
}

// Command: files
static int cmd_files(int argc, char **argv) {
    spiffs_file_info_t files[SPIFFS_MAX_FILES];
    int count = spiffs_manager_list_files(files, SPIFFS_MAX_FILES);

    if (count == 0) {
        printf("No JPEG files stored.\n");
    } else {
        printf("Stored files:\n");
        for (int i = 0; i < count; i++) {
            printf("  %s (%d bytes)\n", files[i].filename, files[i].size);
        }
    }

    size_t total, used;
    if (spiffs_manager_get_stats(&total, &used) == ESP_OK) {
        printf("Storage: %d / %d bytes used (%.1f%%)\n", used, total, (float)used / total * 100);
    }

    return 0;
}

// Command: reboot
static int cmd_reboot(int argc, char **argv) {
    printf("Rebooting...\n");
    esp_restart();
    return 0;
}

// Command: help
static int cmd_help(int argc, char **argv) {
    console_print_help();
    return 0;
}

void console_print_help(void) {
    printf("\n");
    printf("=== ESP32 Instax Bridge Console ===\n");
    printf("\n");
    printf("WiFi Commands:\n");
    printf("  wifi_set <ssid> <password>  - Set WiFi credentials\n");
    printf("  wifi_connect                - Connect to stored WiFi network\n");
    printf("  wifi_disconnect             - Disconnect from WiFi\n");
    printf("  wifi_status                 - Show WiFi connection status\n");
    printf("  wifi_clear                  - Clear stored WiFi credentials\n");
    printf("\n");
    printf("BLE Commands:\n");
    printf("  ble_scan                    - Scan for Instax printers (5 seconds)\n");
    printf("  ble_devices                 - List discovered devices\n");
    printf("  ble_status                  - Show BLE connection status\n");
    printf("\n");
    printf("Storage Commands:\n");
    printf("  files                       - List stored JPEG files\n");
    printf("\n");
    printf("System Commands:\n");
    printf("  help                        - Show this help\n");
    printf("  reboot                      - Reboot the device\n");
    printf("\n");
}

static void register_commands(void) {
    // wifi_set command
    wifi_set_args.ssid = arg_str1(NULL, NULL, "<ssid>", "WiFi SSID");
    wifi_set_args.password = arg_str1(NULL, NULL, "<password>", "WiFi password");
    wifi_set_args.end = arg_end(2);

    const esp_console_cmd_t wifi_set_cmd = {
        .command = "wifi_set",
        .help = "Set WiFi credentials",
        .hint = NULL,
        .func = &cmd_wifi_set,
        .argtable = &wifi_set_args
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&wifi_set_cmd));

    // Simple commands without arguments
    const esp_console_cmd_t cmds[] = {
        { .command = "wifi_connect", .help = "Connect to WiFi", .func = &cmd_wifi_connect },
        { .command = "wifi_disconnect", .help = "Disconnect from WiFi", .func = &cmd_wifi_disconnect },
        { .command = "wifi_status", .help = "Show WiFi status", .func = &cmd_wifi_status },
        { .command = "wifi_clear", .help = "Clear WiFi credentials", .func = &cmd_wifi_clear },
        { .command = "ble_scan", .help = "Scan for BLE devices", .func = &cmd_ble_scan },
        { .command = "ble_devices", .help = "List discovered devices", .func = &cmd_ble_devices },
        { .command = "ble_status", .help = "Show BLE status", .func = &cmd_ble_status },
        { .command = "files", .help = "List stored files", .func = &cmd_files },
        { .command = "reboot", .help = "Reboot device", .func = &cmd_reboot },
        { .command = "help", .help = "Show help", .func = &cmd_help },
    };

    for (int i = 0; i < sizeof(cmds) / sizeof(cmds[0]); i++) {
        ESP_ERROR_CHECK(esp_console_cmd_register(&cmds[i]));
    }
}

static void console_task(void *arg) {
    const char *prompt = "instax> ";

    printf("\n");
    printf("ESP32 Instax Bridge Console\n");
    printf("Type 'help' for available commands.\n");
    printf("\n");

    while (true) {
        char *line = linenoise(prompt);
        if (line == NULL) {
            continue;
        }

        if (strlen(line) > 0) {
            linenoiseHistoryAdd(line);

            int ret;
            esp_err_t err = esp_console_run(line, &ret);
            if (err == ESP_ERR_NOT_FOUND) {
                printf("Unknown command: %s\n", line);
            } else if (err == ESP_ERR_INVALID_ARG) {
                // Command was empty or invalid
            } else if (err != ESP_OK) {
                printf("Error: %s\n", esp_err_to_name(err));
            }
        }

        linenoiseFree(line);
    }
}

esp_err_t console_init(void) {
    // Configure UART for console
    setvbuf(stdin, NULL, _IONBF, 0);
    setvbuf(stdout, NULL, _IONBF, 0);

    // Install UART driver for console
    ESP_ERROR_CHECK(uart_driver_install(CONFIG_ESP_CONSOLE_UART_NUM,
                                         256, 0, 0, NULL, 0));

    // Initialize console
    esp_console_config_t console_config = {
        .max_cmdline_args = 8,
        .max_cmdline_length = 256,
    };
    ESP_ERROR_CHECK(esp_console_init(&console_config));

    // Configure linenoise
    linenoiseSetMultiLine(1);
    linenoiseHistorySetMaxLen(20);
    linenoiseAllowEmpty(false);

    // Register commands
    register_commands();

    // Start console task
    xTaskCreate(console_task, "console", 4096, NULL, 5, NULL);

    ESP_LOGI(TAG, "Console initialized");
    return ESP_OK;
}
