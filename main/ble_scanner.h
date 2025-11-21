/**
 * @file ble_scanner.h
 * @brief BLE scanner and Instax printer connection management
 */

#ifndef BLE_SCANNER_H
#define BLE_SCANNER_H

#include <stdbool.h>
#include "esp_err.h"
#include "instax_protocol.h"

// Maximum number of discovered printers
#define MAX_DISCOVERED_PRINTERS     10

// Discovered printer info
typedef struct {
    char name[32];
    uint8_t address[6];
    int8_t rssi;
    bool is_instax;
} ble_discovered_device_t;

// BLE connection state
typedef enum {
    BLE_STATE_IDLE = 0,
    BLE_STATE_SCANNING,
    BLE_STATE_CONNECTING,
    BLE_STATE_CONNECTED,
    BLE_STATE_DISCONNECTED,
    BLE_STATE_ERROR
} ble_state_t;

// Callbacks
typedef void (*ble_scan_result_callback_t)(const ble_discovered_device_t *device);
typedef void (*ble_connection_callback_t)(ble_state_t state);
typedef void (*ble_data_callback_t)(const uint8_t *data, size_t len);

/**
 * Initialize BLE subsystem
 * @return ESP_OK on success
 */
esp_err_t ble_scanner_init(void);

/**
 * Start scanning for Instax printers
 * @param duration_sec Scan duration in seconds (0 = continuous)
 * @return ESP_OK on success
 */
esp_err_t ble_scanner_start_scan(uint32_t duration_sec);

/**
 * Stop scanning
 * @return ESP_OK on success
 */
esp_err_t ble_scanner_stop_scan(void);

/**
 * Get list of discovered Instax printers
 * @param devices Array to fill with discovered devices
 * @param max_devices Maximum number of devices to return
 * @return Number of discovered devices
 */
int ble_scanner_get_discovered(ble_discovered_device_t *devices, int max_devices);

/**
 * Clear discovered devices list
 */
void ble_scanner_clear_discovered(void);

/**
 * Connect to an Instax printer by address
 * @param address 6-byte BLE address
 * @return ESP_OK if connection started
 */
esp_err_t ble_scanner_connect(const uint8_t *address);

/**
 * Disconnect from current printer
 * @return ESP_OK on success
 */
esp_err_t ble_scanner_disconnect(void);

/**
 * Get current BLE state
 * @return Current state
 */
ble_state_t ble_scanner_get_state(void);

/**
 * Check if connected to a printer
 * @return true if connected
 */
bool ble_scanner_is_connected(void);

/**
 * Write data to the Instax write characteristic
 * @param data Data to write
 * @param len Length of data
 * @return ESP_OK on success
 */
esp_err_t ble_scanner_write(const uint8_t *data, size_t len);

/**
 * Register callback for scan results
 * @param callback Function to call when device found
 */
void ble_scanner_register_scan_callback(ble_scan_result_callback_t callback);

/**
 * Register callback for connection state changes
 * @param callback Function to call on state change
 */
void ble_scanner_register_connection_callback(ble_connection_callback_t callback);

/**
 * Register callback for received data (notifications)
 * @param callback Function to call when data received
 */
void ble_scanner_register_data_callback(ble_data_callback_t callback);

/**
 * Query printer info (sends info query packets)
 * @param info Pointer to store printer info
 * @return ESP_OK on success
 */
esp_err_t ble_scanner_query_printer_info(instax_printer_info_t *info);

/**
 * Send image data to printer
 * @param image_data JPEG image data
 * @param image_len Length of image data
 * @param model Printer model for chunk size
 * @param progress_callback Optional callback for progress updates
 * @return ESP_OK on success
 */
esp_err_t ble_scanner_print_image(const uint8_t *image_data, size_t image_len,
                                   instax_model_t model,
                                   instax_print_progress_callback_t progress_callback);

#endif // BLE_SCANNER_H
