/**
 * @file main.c
 * @brief ESP32 Instax Bridge - Main entry point
 *
 * This application provides a bridge between WiFi and Instax printers via BLE.
 * Features:
 * - Serial console for WiFi configuration
 * - Web interface for file upload and printer control
 * - BLE scanning and connection to Instax printers
 * - SPIFFS storage for JPEG images
 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_event.h"
#include "nvs_flash.h"

#include "wifi_manager.h"
#include "spiffs_manager.h"
#include "web_server.h"
#include "console.h"
#include "printer_emulator.h"

static const char *TAG = "main";

// Event group for WiFi events
static EventGroupHandle_t s_wifi_event_group;
#define WIFI_CONNECTED_BIT BIT0

// WiFi event callback
static void wifi_event_callback(wifi_status_t status) {
    switch (status) {
        case WIFI_STATUS_CONNECTED:
            ESP_LOGI(TAG, "WiFi connected - starting web server and BLE");
            xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);

            // Start web server when WiFi connects
            if (!web_server_is_running()) {
                web_server_start();
            }

            // Auto-start BLE advertising
            if (!printer_emulator_is_advertising()) {
                ESP_LOGI(TAG, "Auto-starting BLE advertising");
                printer_emulator_start_advertising();
            }
            break;

        case WIFI_STATUS_DISCONNECTED:
            ESP_LOGI(TAG, "WiFi disconnected");
            xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT);

            // Stop web server when WiFi disconnects
            if (web_server_is_running()) {
                web_server_stop();
            }
            break;

        case WIFI_STATUS_CONNECTING:
            ESP_LOGI(TAG, "WiFi connecting...");
            break;

        case WIFI_STATUS_FAILED:
            ESP_LOGW(TAG, "WiFi connection failed");
            xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
            break;
    }
}

void app_main(void) {
    ESP_LOGI(TAG, "=================================");
    ESP_LOGI(TAG, "  ESP32 Instax Bridge Starting");
    ESP_LOGI(TAG, "=================================");

    // Initialize NVS (required for WiFi and BLE)
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS partition was truncated, erasing...");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ESP_LOGI(TAG, "NVS initialized");

    // Create event group
    s_wifi_event_group = xEventGroupCreate();

    // Initialize default event loop
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // Initialize SPIFFS for file storage
    ret = spiffs_manager_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize SPIFFS");
    } else {
        ESP_LOGI(TAG, "SPIFFS initialized");
    }

    // Initialize WiFi manager
    ret = wifi_manager_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize WiFi manager");
    } else {
        ESP_LOGI(TAG, "WiFi manager initialized");

        // Register WiFi callback
        wifi_manager_register_callback(wifi_event_callback);
    }

    // Add delay to reduce power spike when initializing BLE
    vTaskDelay(pdMS_TO_TICKS(500));

    // Initialize printer emulator (includes BLE peripheral)
    ret = printer_emulator_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize printer emulator");
    } else {
        ESP_LOGI(TAG, "Printer emulator initialized");
    }

    // Initialize serial console
    ret = console_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize console");
    } else {
        ESP_LOGI(TAG, "Console initialized");
    }

    // Auto-connect to WiFi if credentials are stored
    if (wifi_manager_has_credentials()) {
        ESP_LOGI(TAG, "Found stored WiFi credentials, attempting to connect...");
        wifi_manager_connect();
    } else {
        ESP_LOGI(TAG, "No WiFi credentials stored. Use console to configure:");
        ESP_LOGI(TAG, "  wifi_set <ssid> <password>");
        ESP_LOGI(TAG, "  wifi_connect");
    }

    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "System ready. Type 'help' for available commands.");
    ESP_LOGI(TAG, "");

    // Main loop - monitor system status
    while (1) {
        // Check memory periodically (for debugging)
        static int counter = 0;
        if (++counter >= 60) {  // Every 60 seconds
            counter = 0;
            ESP_LOGI(TAG, "Free heap: %d bytes", esp_get_free_heap_size());
        }

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
