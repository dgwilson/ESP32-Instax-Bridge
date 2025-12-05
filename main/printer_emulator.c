/**
 * @file printer_emulator.c
 * @brief Instax Printer Emulator Implementation
 */

#include "printer_emulator.h"
#include "instax_protocol.h"
#include "spiffs_manager.h"
#include "ble_peripheral.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include <string.h>
#include <stdio.h>
#include <time.h>

static const char *TAG = "printer_emulator";

// Current print job state
static FILE *s_current_print_file = NULL;
static char s_current_print_filename[64];
static uint32_t s_current_print_size = 0;

// NVS storage keys
#define NVS_NAMESPACE "printer"
#define NVS_KEY_MODEL "model"
#define NVS_KEY_BATTERY "battery"
#define NVS_KEY_PRINTS "prints"
#define NVS_KEY_LIFETIME "lifetime"
#define NVS_KEY_CHARGING "charging"
#define NVS_KEY_SUSPEND "suspend_dec"

// Suspend decrement flag (for unlimited testing)
static bool s_suspend_decrement = false;

// Printer state
static instax_printer_info_t s_printer_info = {
    .model = INSTAX_MODEL_MINI,
    .width = 800,
    .height = 600,
    .battery_state = 3,  // Good
    .battery_percentage = 85,
    .photos_remaining = 10,
    .is_charging = false,
    .lifetime_print_count = 0,
    .connected = false,
    .device_name = "INSTAX-50196563",  // Match real device naming pattern (similar to 50196562)
    .accelerometer = {
        .x = 0,           // Neutral position (no tilt left/right)
        .y = 0,           // Neutral position (no tilt forward/backward)
        .z = 0,           // Neutral position (no rotation)
        .orientation = 0  // Default orientation state
    },
    .cover_open = false,   // Cover closed (no error)
    .printer_busy = false, // Not busy (no error)
    .auto_sleep_timeout = 5,  // Default to 5 minutes (official app default)
    .print_mode = 0x00        // Default to Rich mode (0x00)
};

/**
 * Load printer state from NVS
 */
static esp_err_t load_state_from_nvs(void) {
    nvs_handle_t nvs_handle;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (ret != ESP_OK) {
        ESP_LOGI(TAG, "No saved state found, using defaults");
        return ESP_OK;  // Not an error, just no saved state
    }

    uint8_t model;
    if (nvs_get_u8(nvs_handle, NVS_KEY_MODEL, &model) == ESP_OK) {
        s_printer_info.model = (instax_model_t)model;
    }

    nvs_get_u8(nvs_handle, NVS_KEY_BATTERY, &s_printer_info.battery_percentage);
    nvs_get_u8(nvs_handle, NVS_KEY_PRINTS, &s_printer_info.photos_remaining);
    nvs_get_u32(nvs_handle, NVS_KEY_LIFETIME, &s_printer_info.lifetime_print_count);

    uint8_t charging;
    if (nvs_get_u8(nvs_handle, NVS_KEY_CHARGING, &charging) == ESP_OK) {
        s_printer_info.is_charging = (charging != 0);
    }

    // EXPERIMENT 2: Force charging to false to test official app behavior
    s_printer_info.is_charging = false;
    ESP_LOGI(TAG, "EXPERIMENT 2: Forcing is_charging = false");

    uint8_t suspend;
    if (nvs_get_u8(nvs_handle, NVS_KEY_SUSPEND, &suspend) == ESP_OK) {
        s_suspend_decrement = (suspend != 0);
    }

    nvs_close(nvs_handle);
    ESP_LOGI(TAG, "Loaded state from NVS");
    return ESP_OK;
}

/**
 * Save printer state to NVS
 */
static esp_err_t save_state_to_nvs(void) {
    nvs_handle_t nvs_handle;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS: %s", esp_err_to_name(ret));
        return ret;
    }

    nvs_set_u8(nvs_handle, NVS_KEY_MODEL, (uint8_t)s_printer_info.model);
    nvs_set_u8(nvs_handle, NVS_KEY_BATTERY, s_printer_info.battery_percentage);
    nvs_set_u8(nvs_handle, NVS_KEY_PRINTS, s_printer_info.photos_remaining);
    nvs_set_u32(nvs_handle, NVS_KEY_LIFETIME, s_printer_info.lifetime_print_count);
    nvs_set_u8(nvs_handle, NVS_KEY_CHARGING, s_printer_info.is_charging ? 1 : 0);
    nvs_set_u8(nvs_handle, NVS_KEY_SUSPEND, s_suspend_decrement ? 1 : 0);

    ret = nvs_commit(nvs_handle);
    nvs_close(nvs_handle);

    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Saved state to NVS");
    }
    return ret;
}

/**
 * Update model-specific dimensions
 */
static void update_model_dimensions(void) {
    const instax_model_info_t *info = instax_get_model_info(s_printer_info.model);
    if (info) {
        s_printer_info.width = info->width;
        s_printer_info.height = info->height;
    }

    // Set device name (same for all models)
    // Use official Instax naming pattern: INSTAX-XXXXXXXX (all caps, 8 digits)
    // Pattern matches real device (50196562) with last digit changed
    strncpy(s_printer_info.device_name, "INSTAX-50196563", sizeof(s_printer_info.device_name) - 1);
}

/**
 * Print start callback - called when print job starts
 */
static void on_print_start(uint32_t image_size) {
    ESP_LOGI(TAG, "Print job started: %lu bytes", (unsigned long)image_size);

    // Generate filename with timestamp
    time_t now = time(NULL);
    snprintf(s_current_print_filename, sizeof(s_current_print_filename),
             "/spiffs/print_%lu.jpg", (unsigned long)now);

    // Open file for writing
    s_current_print_file = fopen(s_current_print_filename, "wb");
    if (s_current_print_file == NULL) {
        ESP_LOGE(TAG, "Failed to open file for writing: %s", s_current_print_filename);
        return;
    }

    s_current_print_size = image_size;
    ESP_LOGI(TAG, "Saving print to: %s", s_current_print_filename);
}

/**
 * Print data callback - called for each chunk of print data
 */
static void on_print_data(uint32_t chunk_index, const uint8_t *data, size_t len) {
    // Reduced logging to save stack space during rapid transfers
    if (chunk_index % 20 == 0) {
        ESP_LOGD(TAG, "Print data chunk %lu: %d bytes", (unsigned long)chunk_index, len);
    }

    if (s_current_print_file == NULL) {
        ESP_LOGW(TAG, "No open print file for data chunk");
        return;
    }

    // Log first chunk bytes to verify JPEG header
    if (chunk_index == 0 && len > 0) {
        ESP_LOGD(TAG, "First chunk (%d bytes): %02x %02x %02x %02x %02x %02x %02x %02x",
                len,
                len > 0 ? data[0] : 0, len > 1 ? data[1] : 0, len > 2 ? data[2] : 0, len > 3 ? data[3] : 0,
                len > 4 ? data[4] : 0, len > 5 ? data[5] : 0, len > 6 ? data[6] : 0, len > 7 ? data[7] : 0);

        // Check for JPEG header (should be FF D8 FF)
        if (len >= 3 && data[0] == 0xFF && data[1] == 0xD8 && data[2] == 0xFF) {
            ESP_LOGD(TAG, "Valid JPEG header detected");
        } else {
            ESP_LOGW(TAG, "WARNING: Expected JPEG header (FF D8 FF), got %02x %02x %02x",
                    len > 0 ? data[0] : 0, len > 1 ? data[1] : 0, len > 2 ? data[2] : 0);
        }
    }

    size_t written = fwrite(data, 1, len, s_current_print_file);
    if (written != len) {
        ESP_LOGE(TAG, "Failed to write all data: wrote %d/%d bytes", written, len);
    }

    // Flush occasionally to ensure data is written
    if (chunk_index % 10 == 0) {
        fflush(s_current_print_file);
    }
}

/**
 * Print complete callback - called when print job finishes
 */
static void on_print_complete(void) {
    ESP_LOGI(TAG, "Print job complete!");

    if (s_current_print_file != NULL) {
        fclose(s_current_print_file);
        s_current_print_file = NULL;

        ESP_LOGI(TAG, "Saved print file: %s", s_current_print_filename);

        // Increment lifetime counter
        s_printer_info.lifetime_print_count++;

        // Decrement remaining prints if not zero (unless suspend is enabled)
        if (!s_suspend_decrement && s_printer_info.photos_remaining > 0) {
            s_printer_info.photos_remaining--;
            ESP_LOGI(TAG, "Decremented print count to %d", s_printer_info.photos_remaining);
        } else if (s_suspend_decrement) {
            ESP_LOGI(TAG, "Print count decrement suspended - remaining unchanged at %d", s_printer_info.photos_remaining);
        }

        // Save updated state
        save_state_to_nvs();

        ESP_LOGI(TAG, "Lifetime prints: %lu, Remaining: %d",
                (unsigned long)s_printer_info.lifetime_print_count,
                s_printer_info.photos_remaining);
    }
}

esp_err_t printer_emulator_init(void) {
    ESP_LOGI(TAG, "Initializing printer emulator");

    // Load saved state
    load_state_from_nvs();

    // Update dimensions based on model
    update_model_dimensions();

    ESP_LOGI(TAG, "Printer emulator initialized:");
    ESP_LOGI(TAG, "  Model: %s", printer_emulator_model_to_string(s_printer_info.model));
    ESP_LOGI(TAG, "  Battery: %d%%", s_printer_info.battery_percentage);
    ESP_LOGI(TAG, "  Prints remaining: %d", s_printer_info.photos_remaining);
    ESP_LOGI(TAG, "  Lifetime prints: %lu", (unsigned long)s_printer_info.lifetime_print_count);

    // Initialize BLE peripheral
    esp_err_t ret = ble_peripheral_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize BLE peripheral");
        return ret;
    }

    // Register print callbacks
    ble_peripheral_register_print_start_callback(on_print_start);
    ble_peripheral_register_print_data_callback(on_print_data);
    ble_peripheral_register_print_complete_callback(on_print_complete);

    ESP_LOGI(TAG, "BLE peripheral initialized and callbacks registered");
    return ESP_OK;
}

esp_err_t printer_emulator_start_advertising(void) {
    ESP_LOGI(TAG, "Starting BLE advertising as %s", s_printer_info.device_name);
    return ble_peripheral_start_advertising(s_printer_info.device_name);
}

esp_err_t printer_emulator_stop_advertising(void) {
    ESP_LOGI(TAG, "Stopping BLE advertising");
    return ble_peripheral_stop_advertising();
}

const instax_printer_info_t* printer_emulator_get_info(void) {
    return &s_printer_info;
}

esp_err_t printer_emulator_set_model(instax_model_t model) {
    if (model != INSTAX_MODEL_MINI &&
        model != INSTAX_MODEL_SQUARE &&
        model != INSTAX_MODEL_WIDE) {
        return ESP_ERR_INVALID_ARG;
    }

    s_printer_info.model = model;
    update_model_dimensions();
    save_state_to_nvs();

    // Update the BLE advertised model number
    ble_peripheral_update_model_number(model);

    ESP_LOGI(TAG, "Model set to %s (%dx%d)",
             printer_emulator_model_to_string(model),
             s_printer_info.width, s_printer_info.height);

    return ESP_OK;
}

esp_err_t printer_emulator_set_battery(uint8_t percentage) {
    if (percentage > 100) {
        return ESP_ERR_INVALID_ARG;
    }

    s_printer_info.battery_percentage = percentage;

    // Update battery state based on percentage
    if (percentage > 75) {
        s_printer_info.battery_state = 3;  // Good
    } else if (percentage > 50) {
        s_printer_info.battery_state = 2;  // Medium
    } else if (percentage > 25) {
        s_printer_info.battery_state = 1;  // Low
    } else {
        s_printer_info.battery_state = 0;  // Critical
    }

    save_state_to_nvs();

    ESP_LOGI(TAG, "Battery set to %d%%", percentage);
    return ESP_OK;
}

esp_err_t printer_emulator_set_prints_remaining(uint8_t count) {
    s_printer_info.photos_remaining = count;
    save_state_to_nvs();

    ESP_LOGI(TAG, "Prints remaining set to %d", count);
    return ESP_OK;
}

esp_err_t printer_emulator_set_charging(bool is_charging) {
    s_printer_info.is_charging = is_charging;
    save_state_to_nvs();

    ESP_LOGI(TAG, "Charging status set to %s", is_charging ? "ON" : "OFF");
    return ESP_OK;
}

esp_err_t printer_emulator_set_cover_open(bool is_open) {
    s_printer_info.cover_open = is_open;
    ESP_LOGI(TAG, "Cover %s (error 179: %s)", is_open ? "OPEN" : "closed", is_open ? "ACTIVE" : "disabled");
    return ESP_OK;
}

esp_err_t printer_emulator_set_busy(bool is_busy) {
    s_printer_info.printer_busy = is_busy;
    ESP_LOGI(TAG, "Printer %s (error 181: %s)", is_busy ? "BUSY" : "ready", is_busy ? "ACTIVE" : "disabled");
    return ESP_OK;
}

esp_err_t printer_emulator_set_accel_x(int16_t x) {
    s_printer_info.accelerometer.x = x;
    ESP_LOGI(TAG, "Accelerometer X set to %d", x);
    return ESP_OK;
}

esp_err_t printer_emulator_set_accel_y(int16_t y) {
    s_printer_info.accelerometer.y = y;
    ESP_LOGI(TAG, "Accelerometer Y set to %d", y);
    return ESP_OK;
}

esp_err_t printer_emulator_set_accel_z(int16_t z) {
    s_printer_info.accelerometer.z = z;
    ESP_LOGI(TAG, "Accelerometer Z set to %d", z);
    return ESP_OK;
}

esp_err_t printer_emulator_set_accel_orientation(uint8_t orientation) {
    s_printer_info.accelerometer.orientation = orientation;
    ESP_LOGI(TAG, "Accelerometer orientation set to %d", orientation);
    return ESP_OK;
}

esp_err_t printer_emulator_set_suspend_decrement(bool suspend) {
    s_suspend_decrement = suspend;
    save_state_to_nvs();

    ESP_LOGI(TAG, "Suspend decrement %s", suspend ? "ENABLED" : "DISABLED");
    return ESP_OK;
}

bool printer_emulator_get_suspend_decrement(void) {
    return s_suspend_decrement;
}

esp_err_t printer_emulator_set_auto_sleep(uint8_t timeout_minutes) {
    s_printer_info.auto_sleep_timeout = timeout_minutes;
    ESP_LOGI(TAG, "Auto-sleep timeout set to %d minutes (%s)",
             timeout_minutes, timeout_minutes == 0 ? "never" : "enabled");
    return ESP_OK;
}

esp_err_t printer_emulator_set_print_mode(uint8_t mode) {
    if (mode != 0x00 && mode != 0x03) {
        ESP_LOGW(TAG, "Unknown print mode 0x%02x (expected 0x00=Rich or 0x03=Natural)", mode);
        // Accept it anyway to support future modes
    }
    s_printer_info.print_mode = mode;
    const char *mode_str = (mode == 0x00) ? "Rich" : (mode == 0x03) ? "Natural" : "Unknown";
    ESP_LOGI(TAG, "Print mode set to 0x%02x (%s)", mode, mode_str);
    return ESP_OK;
}

const char* printer_emulator_model_to_string(instax_model_t model) {
    switch (model) {
        case INSTAX_MODEL_MINI:
            return "mini";
        case INSTAX_MODEL_SQUARE:
            return "square";
        case INSTAX_MODEL_WIDE:
            return "wide";
        default:
            return "unknown";
    }
}

bool printer_emulator_is_advertising(void) {
    return ble_peripheral_is_advertising();
}
