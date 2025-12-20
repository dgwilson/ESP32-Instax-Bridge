/**
 * @file console.c
 * @brief Serial console for WiFi configuration and system control
 */

#include "console.h"
#include "wifi_manager.h"
#include "spiffs_manager.h"
#include "printer_emulator.h"
#include <string.h>
#include <stdio.h>
#include "esp_console.h"
#include "esp_log.h"
#include "esp_system.h"
#include "driver/uart.h"
#include "driver/uart_vfs.h"
#include "linenoise/linenoise.h"
#include "argtable3/argtable3.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

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

// Command: printer_status
static int cmd_printer_status(int argc, char **argv) {
    const instax_printer_info_t *info = printer_emulator_get_info();

    printf("\n");
    printf("Printer Status:\n");
    printf("  Model: %s\n", printer_emulator_model_to_string(info->model));
    printf("  Dimensions: %dx%d\n", info->width, info->height);
    printf("  Battery: %d%% (%s)\n", info->battery_percentage, info->is_charging ? "Charging" : "Not Charging");
    printf("  Prints remaining: %d\n", info->photos_remaining);
    printf("  Lifetime prints: %lu\n", (unsigned long)info->lifetime_print_count);
    printf("  BLE Status: %s\n", printer_emulator_is_advertising() ? "Advertising" : "Stopped");
    printf("\n");

    return 0;
}

// Command: printer_model <mini|wide|square>
static struct {
    struct arg_str *model;
    struct arg_end *end;
} printer_model_args;

static int cmd_printer_model(int argc, char **argv) {
    int nerrors = arg_parse(argc, argv, (void **)&printer_model_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, printer_model_args.end, argv[0]);
        return 1;
    }

    const char *model_str = printer_model_args.model->sval[0];
    instax_model_t model;

    if (strcmp(model_str, "mini") == 0) {
        model = INSTAX_MODEL_MINI;
    } else if (strcmp(model_str, "wide") == 0) {
        model = INSTAX_MODEL_WIDE;
    } else if (strcmp(model_str, "square") == 0) {
        model = INSTAX_MODEL_SQUARE;
    } else {
        printf("Invalid model. Use: mini, wide, or square\n");
        return 1;
    }

    esp_err_t ret = printer_emulator_set_model(model);
    if (ret == ESP_OK) {
        const instax_printer_info_t *info = printer_emulator_get_info();
        printf("Printer model set to %s (%dx%d)\n",
               printer_emulator_model_to_string(model),
               info->width, info->height);

        // Countdown before reboot to apply new MAC address
        printf("\n⚠️  Rebooting in ");
        for (int i = 10; i > 0; i--) {
            printf("%d... ", i);
            fflush(stdout);
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
        printf("\n\n🔄 Rebooting to apply new BLE MAC address...\n");
        fflush(stdout);
        vTaskDelay(pdMS_TO_TICKS(100));  // Brief delay to flush output
        esp_restart();
    } else {
        printf("Failed to set model: %s\n", esp_err_to_name(ret));
    }

    return ret == ESP_OK ? 0 : 1;
}

// Command: printer_battery <0-100>
static struct {
    struct arg_int *percentage;
    struct arg_end *end;
} printer_battery_args;

static int cmd_printer_battery(int argc, char **argv) {
    int nerrors = arg_parse(argc, argv, (void **)&printer_battery_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, printer_battery_args.end, argv[0]);
        return 1;
    }

    int percentage = printer_battery_args.percentage->ival[0];
    if (percentage < 0 || percentage > 100) {
        printf("Battery percentage must be 0-100\n");
        return 1;
    }

    esp_err_t ret = printer_emulator_set_battery(percentage);
    if (ret == ESP_OK) {
        printf("Battery set to %d%%\n", percentage);
    } else {
        printf("Failed to set battery: %s\n", esp_err_to_name(ret));
    }

    return ret == ESP_OK ? 0 : 1;
}

// Command: printer_prints <n>
static struct {
    struct arg_int *count;
    struct arg_end *end;
} printer_prints_args;

static int cmd_printer_prints(int argc, char **argv) {
    int nerrors = arg_parse(argc, argv, (void **)&printer_prints_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, printer_prints_args.end, argv[0]);
        return 1;
    }

    int count = printer_prints_args.count->ival[0];
    if (count < 0 || count > 255) {
        printf("Prints count must be 0-255\n");
        return 1;
    }

    esp_err_t ret = printer_emulator_set_prints_remaining(count);
    if (ret == ESP_OK) {
        printf("Prints remaining set to %d\n", count);
    } else {
        printf("Failed to set prints: %s\n", esp_err_to_name(ret));
    }

    return ret == ESP_OK ? 0 : 1;
}

// Command: printer_charging <on|off>
static struct {
    struct arg_str *state;
    struct arg_end *end;
} printer_charging_args;

static int cmd_printer_charging(int argc, char **argv) {
    int nerrors = arg_parse(argc, argv, (void **)&printer_charging_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, printer_charging_args.end, argv[0]);
        return 1;
    }

    const char *state_str = printer_charging_args.state->sval[0];
    bool is_charging;

    if (strcasecmp(state_str, "on") == 0 || strcmp(state_str, "1") == 0) {
        is_charging = true;
    } else if (strcasecmp(state_str, "off") == 0 || strcmp(state_str, "0") == 0) {
        is_charging = false;
    } else {
        printf("Invalid state. Use 'on' or 'off'\n");
        return 1;
    }

    esp_err_t ret = printer_emulator_set_charging(is_charging);
    if (ret == ESP_OK) {
        printf("Charging status set to %s\n", is_charging ? "ON" : "OFF");
    } else {
        printf("Failed to set charging: %s\n", esp_err_to_name(ret));
    }

    return ret == ESP_OK ? 0 : 1;
}

// Command: printer_cover <open|close>
static struct {
    struct arg_str *state;
    struct arg_end *end;
} printer_cover_args;

static int cmd_printer_cover(int argc, char **argv) {
    int nerrors = arg_parse(argc, argv, (void **)&printer_cover_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, printer_cover_args.end, argv[0]);
        return 1;
    }

    const char *state_str = printer_cover_args.state->sval[0];
    bool is_open;

    if (strcasecmp(state_str, "open") == 0 || strcmp(state_str, "1") == 0) {
        is_open = true;
    } else if (strcasecmp(state_str, "close") == 0 || strcasecmp(state_str, "closed") == 0 || strcmp(state_str, "0") == 0) {
        is_open = false;
    } else {
        printf("Invalid state. Use 'open' or 'close'\n");
        return 1;
    }

    esp_err_t ret = printer_emulator_set_cover_open(is_open);
    if (ret == ESP_OK) {
        printf("Cover set to %s (error 179: %s)\n", is_open ? "OPEN" : "CLOSED", is_open ? "ACTIVE" : "disabled");
    } else {
        printf("Failed to set cover state: %s\n", esp_err_to_name(ret));
    }

    return ret == ESP_OK ? 0 : 1;
}

// Command: printer_busy <on|off>
static struct {
    struct arg_str *state;
    struct arg_end *end;
} printer_busy_args;

static int cmd_printer_busy(int argc, char **argv) {
    int nerrors = arg_parse(argc, argv, (void **)&printer_busy_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, printer_busy_args.end, argv[0]);
        return 1;
    }

    const char *state_str = printer_busy_args.state->sval[0];
    bool is_busy;

    if (strcasecmp(state_str, "on") == 0 || strcmp(state_str, "1") == 0) {
        is_busy = true;
    } else if (strcasecmp(state_str, "off") == 0 || strcmp(state_str, "0") == 0) {
        is_busy = false;
    } else {
        printf("Invalid state. Use 'on' or 'off'\n");
        return 1;
    }

    esp_err_t ret = printer_emulator_set_busy(is_busy);
    if (ret == ESP_OK) {
        printf("Printer busy set to %s (error 181: %s)\n", is_busy ? "ON" : "OFF", is_busy ? "ACTIVE" : "disabled");
    } else {
        printf("Failed to set busy state: %s\n", esp_err_to_name(ret));
    }

    return ret == ESP_OK ? 0 : 1;
}

// Command: accel_x <-1000 to 1000>
static struct {
    struct arg_int *x;
    struct arg_end *end;
} accel_x_args;

static int cmd_accel_x(int argc, char **argv) {
    int nerrors = arg_parse(argc, argv, (void **)&accel_x_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, accel_x_args.end, argv[0]);
        return 1;
    }

    int16_t x = (int16_t)accel_x_args.x->ival[0];
    if (x < -1000 || x > 1000) {
        printf("X-axis value should be between -1000 and 1000\n");
    }

    esp_err_t ret = printer_emulator_set_accel_x(x);
    if (ret == ESP_OK) {
        printf("Accelerometer X-axis set to %d\n", x);
    } else {
        printf("Failed to set X-axis: %s\n", esp_err_to_name(ret));
    }

    return ret == ESP_OK ? 0 : 1;
}

// Command: accel_y <-1000 to 1000>
static struct {
    struct arg_int *y;
    struct arg_end *end;
} accel_y_args;

static int cmd_accel_y(int argc, char **argv) {
    int nerrors = arg_parse(argc, argv, (void **)&accel_y_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, accel_y_args.end, argv[0]);
        return 1;
    }

    int16_t y = (int16_t)accel_y_args.y->ival[0];
    if (y < -1000 || y > 1000) {
        printf("Y-axis value should be between -1000 and 1000\n");
    }

    esp_err_t ret = printer_emulator_set_accel_y(y);
    if (ret == ESP_OK) {
        printf("Accelerometer Y-axis set to %d\n", y);
    } else {
        printf("Failed to set Y-axis: %s\n", esp_err_to_name(ret));
    }

    return ret == ESP_OK ? 0 : 1;
}

// Command: accel_z <-1000 to 1000>
static struct {
    struct arg_int *z;
    struct arg_end *end;
} accel_z_args;

static int cmd_accel_z(int argc, char **argv) {
    int nerrors = arg_parse(argc, argv, (void **)&accel_z_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, accel_z_args.end, argv[0]);
        return 1;
    }

    int16_t z = (int16_t)accel_z_args.z->ival[0];
    if (z < -1000 || z > 1000) {
        printf("Z-axis value should be between -1000 and 1000\n");
    }

    esp_err_t ret = printer_emulator_set_accel_z(z);
    if (ret == ESP_OK) {
        printf("Accelerometer Z-axis set to %d\n", z);
    } else {
        printf("Failed to set Z-axis: %s\n", esp_err_to_name(ret));
    }

    return ret == ESP_OK ? 0 : 1;
}

// Command: accel_orientation <0-255>
static struct {
    struct arg_int *orientation;
    struct arg_end *end;
} accel_orientation_args;

static int cmd_accel_orientation(int argc, char **argv) {
    int nerrors = arg_parse(argc, argv, (void **)&accel_orientation_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, accel_orientation_args.end, argv[0]);
        return 1;
    }

    int orientation = accel_orientation_args.orientation->ival[0];
    if (orientation < 0 || orientation > 255) {
        printf("Orientation value must be 0-255\n");
        return 1;
    }

    esp_err_t ret = printer_emulator_set_accel_orientation((uint8_t)orientation);
    if (ret == ESP_OK) {
        printf("Accelerometer orientation set to %d\n", orientation);
    } else {
        printf("Failed to set orientation: %s\n", esp_err_to_name(ret));
    }

    return ret == ESP_OK ? 0 : 1;
}

// Command: accel_status
static int cmd_accel_status(int argc, char **argv) {
    const instax_printer_info_t *info = printer_emulator_get_info();

    printf("\n");
    printf("Accelerometer Status (Link 3):\n");
    printf("  X-axis (tilt left/right): %d\n", info->accelerometer.x);
    printf("  Y-axis (tilt forward/back): %d\n", info->accelerometer.y);
    printf("  Z-axis (rotation): %d\n", info->accelerometer.z);
    printf("  Orientation: %d\n", info->accelerometer.orientation);
    printf("\n");

    return 0;
}

// Command: ble_start
static int cmd_ble_start(int argc, char **argv) {
    esp_err_t ret = printer_emulator_start_advertising();
    if (ret == ESP_OK) {
        printf("BLE advertising started\n");
    } else if (ret == ESP_ERR_NOT_SUPPORTED) {
        printf("BLE peripheral mode not yet implemented\n");
    } else {
        printf("Failed to start advertising: %s\n", esp_err_to_name(ret));
    }
    return ret == ESP_OK ? 0 : 1;
}

// Command: ble_stop
static int cmd_ble_stop(int argc, char **argv) {
    esp_err_t ret = printer_emulator_stop_advertising();
    if (ret == ESP_OK) {
        printf("BLE advertising stopped\n");
    } else {
        printf("Failed to stop advertising: %s\n", esp_err_to_name(ret));
    }
    return ret == ESP_OK ? 0 : 1;
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
    printf("=== ESP32 Instax Printer Emulator ===\n");
    printf("\n");
    printf("MISSION: Emulate an Instax printer (mini/wide/square) over BLE.\n");
    printf("Accept print jobs from your Moments app, store received images,\n");
    printf("and provide them via web interface for inspection.\n");
    printf("\n");
    printf("Printer Commands:\n");
    printf("  printer_status              - Show printer state (battery, prints, model)\n");
    printf("  printer_model <mini|wide|square> - Set printer model type\n");
    printf("  printer_battery <0-100>     - Set battery level percentage\n");
    printf("  printer_prints <n>          - Set remaining prints count\n");
    printf("  printer_charging <on|off>   - Set charging status\n");
    printf("\n");
    printf("Error Simulation Commands:\n");
    printf("  printer_cover <open|close>  - Simulate cover open (error 179)\n");
    printf("  printer_busy <on|off>       - Simulate printer busy (error 181)\n");
    printf("  Note: Error 178 (no film) = set prints to 0\n");
    printf("        Error 180 (battery low) = set battery below 20%%\n");
    printf("\n");
    printf("Accelerometer Commands (Link 3):\n");
    printf("  accel_status                - Show accelerometer values\n");
    printf("  accel_x <-1000 to 1000>     - Set X-axis (tilt left/right)\n");
    printf("  accel_y <-1000 to 1000>     - Set Y-axis (tilt forward/back)\n");
    printf("  accel_z <-1000 to 1000>     - Set Z-axis (rotation)\n");
    printf("  accel_orientation <0-255>   - Set orientation value\n");
    printf("\n");
    printf("WiFi Commands:\n");
    printf("  wifi_set <ssid> <password>  - Set WiFi credentials\n");
    printf("  wifi_connect                - Connect to stored WiFi network\n");
    printf("  wifi_disconnect             - Disconnect from WiFi\n");
    printf("  wifi_status                 - Show WiFi connection status\n");
    printf("  wifi_clear                  - Clear stored WiFi credentials\n");
    printf("\n");
    printf("BLE Commands:\n");
    printf("  ble_start                   - Start advertising as Instax printer\n");
    printf("  ble_stop                    - Stop BLE advertising\n");
    printf("\n");
    printf("Storage Commands:\n");
    printf("  files                       - List received print files\n");
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

    // printer_model command
    printer_model_args.model = arg_str1(NULL, NULL, "<mini|wide|square>", "Printer model");
    printer_model_args.end = arg_end(1);

    const esp_console_cmd_t printer_model_cmd = {
        .command = "printer_model",
        .help = "Set printer model",
        .hint = NULL,
        .func = &cmd_printer_model,
        .argtable = &printer_model_args
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&printer_model_cmd));

    // Add shorter 'model' alias for convenience
    const esp_console_cmd_t model_cmd = {
        .command = "model",
        .help = "Set printer model (alias for printer_model)",
        .hint = NULL,
        .func = &cmd_printer_model,
        .argtable = &printer_model_args
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&model_cmd));

    // printer_battery command
    printer_battery_args.percentage = arg_int1(NULL, NULL, "<0-100>", "Battery percentage");
    printer_battery_args.end = arg_end(1);

    const esp_console_cmd_t printer_battery_cmd = {
        .command = "printer_battery",
        .help = "Set battery percentage",
        .hint = NULL,
        .func = &cmd_printer_battery,
        .argtable = &printer_battery_args
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&printer_battery_cmd));

    // printer_prints command
    printer_prints_args.count = arg_int1(NULL, NULL, "<n>", "Prints remaining");
    printer_prints_args.end = arg_end(1);

    const esp_console_cmd_t printer_prints_cmd = {
        .command = "printer_prints",
        .help = "Set prints remaining",
        .hint = NULL,
        .func = &cmd_printer_prints,
        .argtable = &printer_prints_args
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&printer_prints_cmd));

    // printer_charging command
    printer_charging_args.state = arg_str1(NULL, NULL, "<on|off>", "Charging state");
    printer_charging_args.end = arg_end(1);

    const esp_console_cmd_t printer_charging_cmd = {
        .command = "printer_charging",
        .help = "Set charging status",
        .hint = NULL,
        .func = &cmd_printer_charging,
        .argtable = &printer_charging_args
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&printer_charging_cmd));

    // printer_cover command
    printer_cover_args.state = arg_str1(NULL, NULL, "<open|close>", "Cover state");
    printer_cover_args.end = arg_end(1);

    const esp_console_cmd_t printer_cover_cmd = {
        .command = "printer_cover",
        .help = "Set cover open/close (error 179)",
        .hint = NULL,
        .func = &cmd_printer_cover,
        .argtable = &printer_cover_args
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&printer_cover_cmd));

    // printer_busy command
    printer_busy_args.state = arg_str1(NULL, NULL, "<on|off>", "Busy state");
    printer_busy_args.end = arg_end(1);

    const esp_console_cmd_t printer_busy_cmd = {
        .command = "printer_busy",
        .help = "Set printer busy state (error 181)",
        .hint = NULL,
        .func = &cmd_printer_busy,
        .argtable = &printer_busy_args
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&printer_busy_cmd));

    // accel_x command
    accel_x_args.x = arg_int1(NULL, NULL, "<-1000 to 1000>", "X-axis value");
    accel_x_args.end = arg_end(1);

    const esp_console_cmd_t accel_x_cmd = {
        .command = "accel_x",
        .help = "Set accelerometer X-axis (tilt left/right)",
        .hint = NULL,
        .func = &cmd_accel_x,
        .argtable = &accel_x_args
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&accel_x_cmd));

    // accel_y command
    accel_y_args.y = arg_int1(NULL, NULL, "<-1000 to 1000>", "Y-axis value");
    accel_y_args.end = arg_end(1);

    const esp_console_cmd_t accel_y_cmd = {
        .command = "accel_y",
        .help = "Set accelerometer Y-axis (tilt forward/back)",
        .hint = NULL,
        .func = &cmd_accel_y,
        .argtable = &accel_y_args
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&accel_y_cmd));

    // accel_z command
    accel_z_args.z = arg_int1(NULL, NULL, "<-1000 to 1000>", "Z-axis value");
    accel_z_args.end = arg_end(1);

    const esp_console_cmd_t accel_z_cmd = {
        .command = "accel_z",
        .help = "Set accelerometer Z-axis (rotation)",
        .hint = NULL,
        .func = &cmd_accel_z,
        .argtable = &accel_z_args
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&accel_z_cmd));

    // accel_orientation command
    accel_orientation_args.orientation = arg_int1(NULL, NULL, "<0-255>", "Orientation value");
    accel_orientation_args.end = arg_end(1);

    const esp_console_cmd_t accel_orientation_cmd = {
        .command = "accel_orientation",
        .help = "Set accelerometer orientation",
        .hint = NULL,
        .func = &cmd_accel_orientation,
        .argtable = &accel_orientation_args
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&accel_orientation_cmd));

    // Simple commands without arguments
    const esp_console_cmd_t cmds[] = {
        { .command = "printer_status", .help = "Show printer status", .func = &cmd_printer_status },
        { .command = "accel_status", .help = "Show accelerometer status", .func = &cmd_accel_status },
        { .command = "wifi_connect", .help = "Connect to WiFi", .func = &cmd_wifi_connect },
        { .command = "wifi_disconnect", .help = "Disconnect from WiFi", .func = &cmd_wifi_disconnect },
        { .command = "wifi_status", .help = "Show WiFi status", .func = &cmd_wifi_status },
        { .command = "wifi_clear", .help = "Clear WiFi credentials", .func = &cmd_wifi_clear },
        { .command = "ble_start", .help = "Start BLE advertising", .func = &cmd_ble_start },
        { .command = "ble_stop", .help = "Stop BLE advertising", .func = &cmd_ble_stop },
        { .command = "files", .help = "List stored files", .func = &cmd_files },
        { .command = "reboot", .help = "Reboot device", .func = &cmd_reboot },
        { .command = "help", .help = "Show help", .func = &cmd_help },
    };

    for (int i = 0; i < sizeof(cmds) / sizeof(cmds[0]); i++) {
        ESP_ERROR_CHECK(esp_console_cmd_register(&cmds[i]));
    }
}

// Filter out escape sequences and non-printable characters
static bool is_valid_input(const char *line) {
    if (line == NULL || strlen(line) == 0) {
        return false;
    }

    // Check for escape sequences (starts with ESC or contains control chars)
    for (int i = 0; line[i] != '\0'; i++) {
        unsigned char c = (unsigned char)line[i];
        // Allow only printable ASCII and space
        if (c < 32 || c > 126) {
            return false;
        }
        // Reject lines starting with '[' (escape sequences)
        if (i == 0 && c == '[') {
            return false;
        }
    }
    return true;
}

static void console_task(void *arg) {
    const char *prompt = "instax> ";

    // Wait for system to finish initialization before showing prompt
    vTaskDelay(pdMS_TO_TICKS(2000));

    printf("\n");
    printf("========================================\n");
    printf("  ESP32 Instax Bridge Console Ready\n");
    printf("  Type 'help' for available commands\n");
    printf("========================================\n");
    printf("\n");

    while (true) {
        // Use linenoise for line editing with echo support
        // This provides proper terminal echo, line editing, and history
        char *line = linenoise(prompt);

        // linenoise returns NULL on EOF or error
        if (line == NULL) {
            continue;
        }

        // Skip empty lines
        if (strlen(line) == 0) {
            linenoiseFree(line);
            continue;
        }

        // Skip invalid input (escape sequences)
        if (!is_valid_input(line)) {
            linenoiseFree(line);
            continue;
        }

        int ret;
        esp_err_t err = esp_console_run(line, &ret);
        if (err == ESP_ERR_NOT_FOUND) {
            printf("Unknown command: %s\n", line);
        } else if (err == ESP_ERR_INVALID_ARG) {
            // Command was empty or invalid
        } else if (err != ESP_OK) {
            printf("Error: %s\n", esp_err_to_name(err));
        }

        // Free the line buffer allocated by linenoise
        linenoiseFree(line);
    }
}

esp_err_t console_init(void) {
    // Configure UART for console - disable buffering for immediate I/O
    setvbuf(stdin, NULL, _IONBF, 0);
    setvbuf(stdout, NULL, _IONBF, 0);

    // Install UART driver for console with echo enabled
    ESP_ERROR_CHECK(uart_driver_install(CONFIG_ESP_CONSOLE_UART_NUM,
                                         256, 0, 0, NULL, 0));

    // Enable VFS UART driver with proper line endings for screen/minicom
    // RX: CR only (screen sends CR when Enter is pressed)
    // TX: CRLF for proper newline display
    uart_vfs_dev_port_set_rx_line_endings(CONFIG_ESP_CONSOLE_UART_NUM, ESP_LINE_ENDINGS_CR);
    uart_vfs_dev_port_set_tx_line_endings(CONFIG_ESP_CONSOLE_UART_NUM, ESP_LINE_ENDINGS_CRLF);

    // Enable blocking UART reads for proper console operation
    // This makes linenoise() and other I/O functions work correctly
    uart_vfs_dev_use_driver(CONFIG_ESP_CONSOLE_UART_NUM);

    // Initialize console
    esp_console_config_t console_config = {
        .max_cmdline_args = 8,
        .max_cmdline_length = 256,
    };
    ESP_ERROR_CHECK(esp_console_init(&console_config));

    // Configure linenoise for line editing and echo
    linenoiseSetMultiLine(0);  // Single line mode
    linenoiseSetMaxLineLen(256);  // Match console config
    linenoiseHistorySetMaxLen(50);  // Keep last 50 commands
    linenoiseAllowEmpty(false);  // Don't return empty lines

    // Register commands
    register_commands();

    // Start console task
    xTaskCreate(console_task, "console", 4096, NULL, 5, NULL);

    ESP_LOGI(TAG, "Console initialized");
    return ESP_OK;
}
