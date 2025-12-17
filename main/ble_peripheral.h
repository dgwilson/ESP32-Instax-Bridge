/**
 * @file ble_peripheral.h
 * @brief BLE Peripheral for Instax Printer Emulation
 *
 * Implements a GATT server that emulates an Instax printer
 */

#ifndef BLE_PERIPHERAL_H
#define BLE_PERIPHERAL_H

#include "esp_err.h"
#include "instax_protocol.h"
#include <stdint.h>
#include <stdbool.h>

/**
 * Callback for when a print job starts
 * @param image_size Expected size of the print image in bytes
 * @return true if print job can be accepted, false if error (e.g., out of memory)
 */
typedef bool (*ble_peripheral_print_start_callback_t)(uint32_t image_size);

/**
 * Callback for print data received
 * @param chunk_index Index of this data chunk
 * @param data Pointer to chunk data
 * @param len Length of chunk data
 */
typedef void (*ble_peripheral_print_data_callback_t)(uint32_t chunk_index, const uint8_t *data, size_t len);

/**
 * Callback for when print job completes
 */
typedef void (*ble_peripheral_print_complete_callback_t)(void);

/**
 * Initialize BLE peripheral as Instax printer
 */
esp_err_t ble_peripheral_init(void);

/**
 * Start advertising as Instax printer
 * @param device_name Name to advertise (e.g., "Instax-Mini Link")
 */
esp_err_t ble_peripheral_start_advertising(const char *device_name);

/**
 * Stop advertising
 */
esp_err_t ble_peripheral_stop_advertising(void);

/**
 * Check if currently advertising
 */
bool ble_peripheral_is_advertising(void);

/**
 * Check if a client is connected
 */
bool ble_peripheral_is_connected(void);

/**
 * Get the BLE MAC address being used for advertising
 * @param mac_out Buffer to receive 6-byte MAC address (must be at least 6 bytes)
 */
void ble_peripheral_get_mac_address(uint8_t *mac_out);

/**
 * Register callback for print start event
 */
void ble_peripheral_register_print_start_callback(ble_peripheral_print_start_callback_t callback);

/**
 * Register callback for print data event
 */
void ble_peripheral_register_print_data_callback(ble_peripheral_print_data_callback_t callback);

/**
 * Register callback for print complete event
 */
void ble_peripheral_register_print_complete_callback(ble_peripheral_print_complete_callback_t callback);

/**
 * Update the advertised model number in Device Information Service
 * @param model Printer model (INSTAX_MODEL_MINI, INSTAX_MODEL_SQUARE, INSTAX_MODEL_WIDE)
 */
void ble_peripheral_update_device_name_with_ip(const char *ip_address);

/**
 * Update BLE advertising name to include IP address (last two octets)
 * @param ip_address IP address string (e.g., "192.168.1.101")
 */
void ble_peripheral_update_model_number(instax_model_t model);

/**
 * Update all Device Information Service values from current printer info
 * Call this when printer model changes to refresh DIS
 */
void ble_peripheral_update_dis_from_printer_info(void);

#endif // BLE_PERIPHERAL_H
