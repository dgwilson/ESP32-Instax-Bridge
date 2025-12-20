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
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdio.h>
#include <time.h>

static const char *TAG = "printer_emulator";

// Current print job state
static FILE *s_current_print_file = NULL;
static char s_current_print_filename[64];
static uint32_t s_current_print_size = 0;

// RAM buffer for print data (reduces SPIFFS write overhead)
#define PRINT_BUFFER_SIZE (32 * 1024)  // 32KB RAM buffer (reduced to fit ESP32 available RAM)
static uint8_t *s_print_buffer = NULL;
static size_t s_print_buffer_pos = 0;

// NVS storage keys
#define NVS_NAMESPACE "printer"
#define NVS_KEY_MODEL "model"
#define NVS_KEY_BATTERY "battery"
#define NVS_KEY_PRINTS "prints"
#define NVS_KEY_LIFETIME "lifetime"
#define NVS_KEY_CHARGING "charging"
#define NVS_KEY_SUSPEND "suspend_dec"
#define NVS_KEY_DEVICE_NAME "device_name"
#define NVS_KEY_MODEL_NUMBER "model_num"
#define NVS_KEY_SERIAL_NUMBER "serial_num"
#define NVS_KEY_FIRMWARE_REV "firmware_rev"
#define NVS_KEY_HARDWARE_REV "hardware_rev"
#define NVS_KEY_SOFTWARE_REV "software_rev"
#define NVS_KEY_MANUFACTURER "manufacturer"

// Suspend decrement flag (for unlimited testing)
static bool s_suspend_decrement = false;

// Printer state
static instax_printer_info_t s_printer_info = {
    .model = INSTAX_MODEL_MINI,
    .width = 600,   // Mini Link 3 is portrait (600x800), not landscape!
    .height = 800,  // Corrected from real printer capture
    .battery_state = 3,  // Good
    .battery_percentage = 85,
    .photos_remaining = 8,  // Actual test value
    .is_charging = false,
    .lifetime_print_count = 35,  // Testing: match real printer (0x23 = 35)
    .connected = false,
    // Device name will be set by reset_dis_to_defaults() based on model
    // Mini: "INSTAX-XXXXXXXX(BLE)", Square/Wide: "INSTAX-XXXXXXXX(IOS)"
    .device_name = "INSTAX-55550000(IOS)",  // Default (will be updated based on model)
    .accelerometer = {
        .x = 0,           // Neutral position (no tilt left/right)
        .y = 0,           // Neutral position (no tilt forward/backward)
        .z = 0,           // Neutral position (no rotation)
        .orientation = 0  // Default orientation state
    },
    .cover_open = false,   // Cover closed (no error)
    .printer_busy = false, // Not busy (no error)
    .auto_sleep_timeout = 5,  // Default to 5 minutes (official app default)
    .print_mode = 0x00,       // Default to Rich mode (0x00)
    // Device Information Service - defaults for Mini (will be updated by reset_dis_to_defaults)
    .model_number = "FI033",
    .serial_number = "70423278",
    .firmware_revision = "0101",
    .hardware_revision = "0001",
    .software_revision = "0002",
    .manufacturer_name = "FUJIFILM"
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

    // Load device name (if saved, otherwise keep default)
    size_t name_len = sizeof(s_printer_info.device_name);
    nvs_get_str(nvs_handle, NVS_KEY_DEVICE_NAME, s_printer_info.device_name, &name_len);

    // Load Device Information Service values (if saved, otherwise keep defaults)
    size_t len;
    len = sizeof(s_printer_info.model_number);
    nvs_get_str(nvs_handle, NVS_KEY_MODEL_NUMBER, s_printer_info.model_number, &len);
    len = sizeof(s_printer_info.serial_number);
    nvs_get_str(nvs_handle, NVS_KEY_SERIAL_NUMBER, s_printer_info.serial_number, &len);
    len = sizeof(s_printer_info.firmware_revision);
    nvs_get_str(nvs_handle, NVS_KEY_FIRMWARE_REV, s_printer_info.firmware_revision, &len);
    len = sizeof(s_printer_info.hardware_revision);
    nvs_get_str(nvs_handle, NVS_KEY_HARDWARE_REV, s_printer_info.hardware_revision, &len);
    len = sizeof(s_printer_info.software_revision);
    nvs_get_str(nvs_handle, NVS_KEY_SOFTWARE_REV, s_printer_info.software_revision, &len);
    len = sizeof(s_printer_info.manufacturer_name);
    nvs_get_str(nvs_handle, NVS_KEY_MANUFACTURER, s_printer_info.manufacturer_name, &len);

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
    nvs_set_str(nvs_handle, NVS_KEY_DEVICE_NAME, s_printer_info.device_name);

    // Save Device Information Service values
    nvs_set_str(nvs_handle, NVS_KEY_MODEL_NUMBER, s_printer_info.model_number);
    nvs_set_str(nvs_handle, NVS_KEY_SERIAL_NUMBER, s_printer_info.serial_number);
    nvs_set_str(nvs_handle, NVS_KEY_FIRMWARE_REV, s_printer_info.firmware_revision);
    nvs_set_str(nvs_handle, NVS_KEY_HARDWARE_REV, s_printer_info.hardware_revision);
    nvs_set_str(nvs_handle, NVS_KEY_SOFTWARE_REV, s_printer_info.software_revision);
    nvs_set_str(nvs_handle, NVS_KEY_MANUFACTURER, s_printer_info.manufacturer_name);

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
    // Device name is now configurable and persisted in NVS
}

/**
 * Set Device Information Service values to model-specific defaults
 */
esp_err_t printer_emulator_reset_dis_to_defaults(void) {
    switch (s_printer_info.model) {
        case INSTAX_MODEL_MINI:
            // Mini Link 3 (FI033) - from iPhone_INSTAX_capture-4.pklg and BLE scanner
            strncpy(s_printer_info.model_number, "FI033", sizeof(s_printer_info.model_number) - 1);
            strncpy(s_printer_info.serial_number, "70555555", sizeof(s_printer_info.serial_number) - 1);
            strncpy(s_printer_info.firmware_revision, "0101", sizeof(s_printer_info.firmware_revision) - 1);
            strncpy(s_printer_info.hardware_revision, "0000", sizeof(s_printer_info.hardware_revision) - 1);  // Real device: 0000
            strncpy(s_printer_info.software_revision, "0003", sizeof(s_printer_info.software_revision) - 1);  // Real device: 0003
            strncpy(s_printer_info.manufacturer_name, "FUJIFILM", sizeof(s_printer_info.manufacturer_name) - 1);
            // CRITICAL: Mini uses (BLE) suffix, not (IOS) - this is how Mini app filters devices!
            strncpy(s_printer_info.device_name, "INSTAX-70555555(BLE)", sizeof(s_printer_info.device_name) - 1);
            break;

        case INSTAX_MODEL_SQUARE:
            // Square Link (FI017) - from physical printer capture
            strncpy(s_printer_info.model_number, "FI017", sizeof(s_printer_info.model_number) - 1);
            strncpy(s_printer_info.serial_number, "50555555", sizeof(s_printer_info.serial_number) - 1);  // Square pattern: 50XXXXXX
            strncpy(s_printer_info.firmware_revision, "0101", sizeof(s_printer_info.firmware_revision) - 1);
            strncpy(s_printer_info.hardware_revision, "0001", sizeof(s_printer_info.hardware_revision) - 1);
            strncpy(s_printer_info.software_revision, "0002", sizeof(s_printer_info.software_revision) - 1);
            strncpy(s_printer_info.manufacturer_name, "FUJIFILM", sizeof(s_printer_info.manufacturer_name) - 1);
            // Square and Wide use (IOS) suffix
            strncpy(s_printer_info.device_name, "INSTAX-50555555(IOS)", sizeof(s_printer_info.device_name) - 1);
            break;

        case INSTAX_MODEL_WIDE:
            // Wide Link (FI022) - from nRF Connect capture
            strncpy(s_printer_info.model_number, "FI022", sizeof(s_printer_info.model_number) - 1);
            strncpy(s_printer_info.serial_number, "20555555", sizeof(s_printer_info.serial_number) - 1);  // Wide pattern: 20XXXXXX
            strncpy(s_printer_info.firmware_revision, "0100", sizeof(s_printer_info.firmware_revision) - 1);  // Wide uses 0100, not 0101
            strncpy(s_printer_info.hardware_revision, "0001", sizeof(s_printer_info.hardware_revision) - 1);
            strncpy(s_printer_info.software_revision, "0002", sizeof(s_printer_info.software_revision) - 1);
            strncpy(s_printer_info.manufacturer_name, "FUJIFILM", sizeof(s_printer_info.manufacturer_name) - 1);
            // Shortened name for Wide (11 chars max) to fit E0FF UUID in scan response
            strncpy(s_printer_info.device_name, "WIDE-205555", sizeof(s_printer_info.device_name) - 1);
            break;

        default:
            return ESP_ERR_INVALID_ARG;
    }

    // Null-terminate all strings to be safe
    s_printer_info.model_number[sizeof(s_printer_info.model_number) - 1] = '\0';
    s_printer_info.serial_number[sizeof(s_printer_info.serial_number) - 1] = '\0';
    s_printer_info.firmware_revision[sizeof(s_printer_info.firmware_revision) - 1] = '\0';
    s_printer_info.hardware_revision[sizeof(s_printer_info.hardware_revision) - 1] = '\0';
    s_printer_info.software_revision[sizeof(s_printer_info.software_revision) - 1] = '\0';
    s_printer_info.manufacturer_name[sizeof(s_printer_info.manufacturer_name) - 1] = '\0';
    s_printer_info.device_name[sizeof(s_printer_info.device_name) - 1] = '\0';

    ESP_LOGI(TAG, "DIS reset to defaults: Model=%s, Serial=%s, FW=%s, HW=%s, SW=%s, Mfr=%s",
             s_printer_info.model_number,
             s_printer_info.serial_number,
             s_printer_info.firmware_revision,
             s_printer_info.hardware_revision,
             s_printer_info.software_revision,
             s_printer_info.manufacturer_name);
    ESP_LOGI(TAG, "Device name set to: %s", s_printer_info.device_name);

    save_state_to_nvs();
    return ESP_OK;
}

/**
 * Print start callback - called when print job starts
 * @return true if successful, false if error (out of memory, etc.)
 */
static bool on_print_start(uint32_t image_size) {
    ESP_LOGI(TAG, "Print job started: %lu bytes", (unsigned long)image_size);

    // Generate filename with timestamp
    time_t now = time(NULL);
    snprintf(s_current_print_filename, sizeof(s_current_print_filename),
             "/spiffs/print_%lu.jpg", (unsigned long)now);

    // Open file for writing
    s_current_print_file = fopen(s_current_print_filename, "wb");
    if (s_current_print_file == NULL) {
        ESP_LOGE(TAG, "Failed to open file for writing: %s", s_current_print_filename);
        return false;
    }

    // Allocate RAM buffer for incoming data
    s_print_buffer = malloc(PRINT_BUFFER_SIZE);
    if (s_print_buffer == NULL) {
        ESP_LOGE(TAG, "Failed to allocate %d byte RAM buffer!", PRINT_BUFFER_SIZE);
        fclose(s_current_print_file);
        s_current_print_file = NULL;
        return false;
    }
    s_print_buffer_pos = 0;

    s_current_print_size = image_size;
    ESP_LOGI(TAG, "Saving print to: %s (using %dKB RAM buffer)", s_current_print_filename, PRINT_BUFFER_SIZE / 1024);
    return true;
}

/**
 * Print data callback - called for each chunk of print data
 */
static void on_print_data(uint32_t chunk_index, const uint8_t *data, size_t len) {
    // Reduced logging to save stack space during rapid transfers
    if (chunk_index % 20 == 0) {
        ESP_LOGD(TAG, "Print data chunk %lu: %d bytes (buffer: %d/%d)",
                (unsigned long)chunk_index, len, s_print_buffer_pos, PRINT_BUFFER_SIZE);
    }

    if (s_current_print_file == NULL || s_print_buffer == NULL) {
        ESP_LOGW(TAG, "No open print file or buffer for data chunk");
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

    // Copy data to RAM buffer
    if (s_print_buffer_pos + len <= PRINT_BUFFER_SIZE) {
        memcpy(s_print_buffer + s_print_buffer_pos, data, len);
        s_print_buffer_pos += len;
    } else {
        ESP_LOGW(TAG, "Buffer overflow prevented: %d + %d > %d", s_print_buffer_pos, len, PRINT_BUFFER_SIZE);
        // Flush current buffer to make room
        if (s_print_buffer_pos > 0) {
            size_t written = fwrite(s_print_buffer, 1, s_print_buffer_pos, s_current_print_file);
            if (written != s_print_buffer_pos) {
                ESP_LOGE(TAG, "Failed to write buffer: wrote %d/%d bytes", written, s_print_buffer_pos);
            }
            s_print_buffer_pos = 0;
        }
        // Now copy the incoming data
        if (len <= PRINT_BUFFER_SIZE) {
            memcpy(s_print_buffer, data, len);
            s_print_buffer_pos = len;
        } else {
            // Chunk is larger than entire buffer - write directly
            ESP_LOGW(TAG, "Chunk larger than buffer, writing directly");
            size_t written = fwrite(data, 1, len, s_current_print_file);
            if (written != len) {
                ESP_LOGE(TAG, "Failed to write large chunk: wrote %d/%d bytes", written, len);
            }
        }
    }

    // Flush buffer to SPIFFS when it's getting full (reserve 2KB for next chunk)
    if (s_print_buffer_pos >= PRINT_BUFFER_SIZE - 2048) {
        ESP_LOGD(TAG, "Buffer near full (%d bytes), flushing to SPIFFS", s_print_buffer_pos);
        size_t written = fwrite(s_print_buffer, 1, s_print_buffer_pos, s_current_print_file);
        if (written != s_print_buffer_pos) {
            ESP_LOGE(TAG, "Failed to write buffer: wrote %d/%d bytes", written, s_print_buffer_pos);
        }
        s_print_buffer_pos = 0;
    }
}

/**
 * Print complete callback - called when print job finishes
 */
static void on_print_complete(void) {
    ESP_LOGI(TAG, "Print job complete!");

    // Flush any remaining buffered data to SPIFFS
    if (s_print_buffer != NULL && s_print_buffer_pos > 0 && s_current_print_file != NULL) {
        ESP_LOGI(TAG, "Flushing final %d bytes from RAM buffer to SPIFFS", s_print_buffer_pos);
        size_t written = fwrite(s_print_buffer, 1, s_print_buffer_pos, s_current_print_file);
        if (written != s_print_buffer_pos) {
            ESP_LOGE(TAG, "Failed to write final buffer: wrote %d/%d bytes", written, s_print_buffer_pos);
        }
        s_print_buffer_pos = 0;
    }

    // Free RAM buffer
    if (s_print_buffer != NULL) {
        free(s_print_buffer);
        s_print_buffer = NULL;
        ESP_LOGD(TAG, "Freed RAM buffer");
    }

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

    // CRITICAL: Reset DIS to match loaded model (fixes model/DIS mismatch after reboot)
    // Without this, DIS strings can be out of sync with the model enum
    // (e.g., model=Mini but DIS model_number="FI017" from previous Square selection)
    printer_emulator_reset_dis_to_defaults();

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

    // Reset Device Information Service values to model-specific defaults
    printer_emulator_reset_dis_to_defaults();

    // CRITICAL: Save updated device name and DIS values to NVS
    // Without this, old device name persists across reboots (e.g., WIDE-205555 when model is Square)
    save_state_to_nvs();

    ESP_LOGI(TAG, "Model set to %s (%dx%d)",
             printer_emulator_model_to_string(model),
             s_printer_info.width, s_printer_info.height);

    // Restart BLE advertising so official apps can discover the new model
    bool was_advertising = printer_emulator_is_advertising();
    if (was_advertising) {
        printer_emulator_stop_advertising();
        vTaskDelay(pdMS_TO_TICKS(100));  // Brief delay for clean restart
        printer_emulator_start_advertising();
        ESP_LOGI(TAG, "BLE advertising restarted with new model number and DIS values");
    }

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

esp_err_t printer_emulator_set_device_name(const char *name) {
    if (name == NULL || strlen(name) == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    if (strlen(name) >= sizeof(s_printer_info.device_name)) {
        return ESP_ERR_INVALID_ARG;
    }

    strncpy(s_printer_info.device_name, name, sizeof(s_printer_info.device_name) - 1);
    s_printer_info.device_name[sizeof(s_printer_info.device_name) - 1] = '\0';
    save_state_to_nvs();

    ESP_LOGI(TAG, "Device name set to: %s", s_printer_info.device_name);

    // Restart BLE advertising with new name
    bool was_advertising = printer_emulator_is_advertising();
    if (was_advertising) {
        printer_emulator_stop_advertising();
        vTaskDelay(pdMS_TO_TICKS(100));  // Brief delay for clean restart
        printer_emulator_start_advertising();
        ESP_LOGI(TAG, "BLE advertising restarted with new name");
    }

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
    if (mode != 0x00 && mode != 0x01 && mode != 0x02 && mode != 0x03) {
        ESP_LOGW(TAG, "Unknown print mode 0x%02x (expected 0x00=Rich, 0x01=Fun1, 0x02=Fun2, or 0x03=Natural)", mode);
        // Accept it anyway to support future modes
    }
    s_printer_info.print_mode = mode;
    const char *mode_str;
    switch (mode) {
        case 0x00: mode_str = "Rich"; break;
        case 0x01: mode_str = "Fun Mode 1"; break;
        case 0x02: mode_str = "Fun Mode 2"; break;
        case 0x03: mode_str = "Natural"; break;
        default: mode_str = "Unknown"; break;
    }
    ESP_LOGI(TAG, "Print mode set to 0x%02x (%s)", mode, mode_str);
    return ESP_OK;
}

esp_err_t printer_emulator_set_model_number(const char *model_number) {
    if (model_number == NULL || strlen(model_number) == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (strlen(model_number) >= sizeof(s_printer_info.model_number)) {
        return ESP_ERR_INVALID_ARG;
    }
    strncpy(s_printer_info.model_number, model_number, sizeof(s_printer_info.model_number) - 1);
    s_printer_info.model_number[sizeof(s_printer_info.model_number) - 1] = '\0';
    save_state_to_nvs();
    ESP_LOGI(TAG, "Model number set to: %s (BLE DIS will update on next advertising restart)", s_printer_info.model_number);
    return ESP_OK;
}

esp_err_t printer_emulator_set_serial_number(const char *serial_number) {
    if (serial_number == NULL || strlen(serial_number) == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (strlen(serial_number) >= sizeof(s_printer_info.serial_number)) {
        return ESP_ERR_INVALID_ARG;
    }
    strncpy(s_printer_info.serial_number, serial_number, sizeof(s_printer_info.serial_number) - 1);
    s_printer_info.serial_number[sizeof(s_printer_info.serial_number) - 1] = '\0';
    save_state_to_nvs();
    ESP_LOGI(TAG, "Serial number set to: %s (BLE DIS will update on next advertising restart)", s_printer_info.serial_number);
    return ESP_OK;
}

esp_err_t printer_emulator_set_firmware_revision(const char *firmware_revision) {
    if (firmware_revision == NULL || strlen(firmware_revision) == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (strlen(firmware_revision) >= sizeof(s_printer_info.firmware_revision)) {
        return ESP_ERR_INVALID_ARG;
    }
    strncpy(s_printer_info.firmware_revision, firmware_revision, sizeof(s_printer_info.firmware_revision) - 1);
    s_printer_info.firmware_revision[sizeof(s_printer_info.firmware_revision) - 1] = '\0';
    save_state_to_nvs();
    ESP_LOGI(TAG, "Firmware revision set to: %s (BLE DIS will update on next advertising restart)", s_printer_info.firmware_revision);
    return ESP_OK;
}

esp_err_t printer_emulator_set_hardware_revision(const char *hardware_revision) {
    if (hardware_revision == NULL || strlen(hardware_revision) == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (strlen(hardware_revision) >= sizeof(s_printer_info.hardware_revision)) {
        return ESP_ERR_INVALID_ARG;
    }
    strncpy(s_printer_info.hardware_revision, hardware_revision, sizeof(s_printer_info.hardware_revision) - 1);
    s_printer_info.hardware_revision[sizeof(s_printer_info.hardware_revision) - 1] = '\0';
    save_state_to_nvs();
    ESP_LOGI(TAG, "Hardware revision set to: %s (BLE DIS will update on next advertising restart)", s_printer_info.hardware_revision);
    return ESP_OK;
}

esp_err_t printer_emulator_set_software_revision(const char *software_revision) {
    if (software_revision == NULL || strlen(software_revision) == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (strlen(software_revision) >= sizeof(s_printer_info.software_revision)) {
        return ESP_ERR_INVALID_ARG;
    }
    strncpy(s_printer_info.software_revision, software_revision, sizeof(s_printer_info.software_revision) - 1);
    s_printer_info.software_revision[sizeof(s_printer_info.software_revision) - 1] = '\0';
    save_state_to_nvs();
    ESP_LOGI(TAG, "Software revision set to: %s (BLE DIS will update on next advertising restart)", s_printer_info.software_revision);
    return ESP_OK;
}

esp_err_t printer_emulator_set_manufacturer_name(const char *manufacturer_name) {
    if (manufacturer_name == NULL || strlen(manufacturer_name) == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (strlen(manufacturer_name) >= sizeof(s_printer_info.manufacturer_name)) {
        return ESP_ERR_INVALID_ARG;
    }
    strncpy(s_printer_info.manufacturer_name, manufacturer_name, sizeof(s_printer_info.manufacturer_name) - 1);
    s_printer_info.manufacturer_name[sizeof(s_printer_info.manufacturer_name) - 1] = '\0';
    save_state_to_nvs();
    ESP_LOGI(TAG, "Manufacturer name set to: %s (BLE DIS will update on next advertising restart)", s_printer_info.manufacturer_name);
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

void printer_emulator_dump_config(void) {
    // Get BLE MAC address
    uint8_t mac[6] = {0};
    ble_peripheral_get_mac_address(mac);

    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "  INSTAX SIMULATOR CONFIGURATION DUMP");
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "PRINTER MODEL:");
    ESP_LOGI(TAG, "  Model: %s", printer_emulator_model_to_string(s_printer_info.model));
    ESP_LOGI(TAG, "  Dimensions: %dx%d", s_printer_info.width, s_printer_info.height);
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "DEVICE INFO SERVICE (DIS):");
    ESP_LOGI(TAG, "  Device Name: %s", s_printer_info.device_name);
    ESP_LOGI(TAG, "  Model Number: %s", s_printer_info.model_number);
    ESP_LOGI(TAG, "  Serial Number: %s", s_printer_info.serial_number);
    ESP_LOGI(TAG, "  Firmware Rev: %s", s_printer_info.firmware_revision);
    ESP_LOGI(TAG, "  Hardware Rev: %s", s_printer_info.hardware_revision);
    ESP_LOGI(TAG, "  Software Rev: %s", s_printer_info.software_revision);
    ESP_LOGI(TAG, "  Manufacturer: %s", s_printer_info.manufacturer_name);
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "PRINTER STATUS:");
    ESP_LOGI(TAG, "  Battery: %d%% (state: %d)", s_printer_info.battery_percentage, s_printer_info.battery_state);
    ESP_LOGI(TAG, "  Charging: %s", s_printer_info.is_charging ? "YES" : "NO");
    ESP_LOGI(TAG, "  Photos Remaining: %d", s_printer_info.photos_remaining);
    ESP_LOGI(TAG, "  Lifetime Prints: %lu", (unsigned long)s_printer_info.lifetime_print_count);
    ESP_LOGI(TAG, "  Cover Open: %s", s_printer_info.cover_open ? "YES (ERROR)" : "NO");
    ESP_LOGI(TAG, "  Printer Busy: %s", s_printer_info.printer_busy ? "YES (ERROR)" : "NO");
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "ACCELEROMETER:");
    ESP_LOGI(TAG, "  X-axis: %d", s_printer_info.accelerometer.x);
    ESP_LOGI(TAG, "  Y-axis: %d", s_printer_info.accelerometer.y);
    ESP_LOGI(TAG, "  Z-axis: %d", s_printer_info.accelerometer.z);
    ESP_LOGI(TAG, "  Orientation: %d", s_printer_info.accelerometer.orientation);
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "SETTINGS:");
    ESP_LOGI(TAG, "  Auto-sleep: %d minutes", s_printer_info.auto_sleep_timeout);
    ESP_LOGI(TAG, "  Print Mode: 0x%02x", s_printer_info.print_mode);
    ESP_LOGI(TAG, "  Suspend Decrement: %s", s_suspend_decrement ? "YES" : "NO");
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "CONNECTION:");
    ESP_LOGI(TAG, "  BLE Advertising: %s", ble_peripheral_is_advertising() ? "YES" : "NO");
    ESP_LOGI(TAG, "  BLE Connected: %s", s_printer_info.connected ? "YES" : "NO");
    ESP_LOGI(TAG, "  BLE MAC Address: %02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "WIRESHARK FILTERS (copy/paste ready)");
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "All traffic to/from this device:");
    ESP_LOGI(TAG, "  btle.advertising_address == %02x:%02x:%02x:%02x:%02x:%02x",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "GATT operations only:");
    ESP_LOGI(TAG, "  (btle.advertising_address == %02x:%02x:%02x:%02x:%02x:%02x) && btatt",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "Writes from app (commands to printer):");
    ESP_LOGI(TAG, "  (btle.advertising_address == %02x:%02x:%02x:%02x:%02x:%02x) && (btatt.opcode == 0x12 || btatt.opcode == 0x52)",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "Notifications from printer (responses to app):");
    ESP_LOGI(TAG, "  (btle.advertising_address == %02x:%02x:%02x:%02x:%02x:%02x) && btatt.opcode == 0x1b",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "");
}
