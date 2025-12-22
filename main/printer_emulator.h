/**
 * @file printer_emulator.h
 * @brief Instax Printer Emulator
 *
 * Emulates an Instax printer (mini/wide/square) as a BLE peripheral.
 * Accepts print jobs, stores received images, and reports printer state.
 */

#ifndef PRINTER_EMULATOR_H
#define PRINTER_EMULATOR_H

#include "instax_protocol.h"
#include "esp_err.h"

/**
 * Initialize the printer emulator
 * Loads saved state from NVS
 */
esp_err_t printer_emulator_init(void);

/**
 * Start BLE advertising as an Instax printer
 */
esp_err_t printer_emulator_start_advertising(void);

/**
 * Stop BLE advertising
 */
esp_err_t printer_emulator_stop_advertising(void);

/**
 * Get current printer info
 */
const instax_printer_info_t* printer_emulator_get_info(void);

/**
 * Set printer model (mini/wide/square)
 */
esp_err_t printer_emulator_set_model(instax_model_t model);

/**
 * Set battery percentage (0-100)
 */
esp_err_t printer_emulator_set_battery(uint8_t percentage);

/**
 * Set remaining prints count
 */
esp_err_t printer_emulator_set_prints_remaining(uint8_t count);

/**
 * Set charging status
 */
esp_err_t printer_emulator_set_charging(bool is_charging);

/**
 * Set device name (BLE advertising name)
 * @param name Device name (e.g., "INSTAX-50196563" or "INSTAX-Simulator")
 */
esp_err_t printer_emulator_set_device_name(const char *name);

/**
 * Set cover open/closed state (for error 179 simulation)
 */
esp_err_t printer_emulator_set_cover_open(bool is_open);

/**
 * Set printer busy state (for error 181 simulation)
 */
esp_err_t printer_emulator_set_busy(bool is_busy);

/**
 * Set accelerometer X-axis value (tilt left/right)
 * @param x Signed 16-bit value (typical range: -1000 to +1000)
 */
esp_err_t printer_emulator_set_accel_x(int16_t x);

/**
 * Set accelerometer Y-axis value (tilt forward/backward)
 * @param y Signed 16-bit value (typical range: -1000 to +1000)
 */
esp_err_t printer_emulator_set_accel_y(int16_t y);

/**
 * Set accelerometer Z-axis value (rotation)
 * @param z Signed 16-bit value (typical range: -1000 to +1000)
 */
esp_err_t printer_emulator_set_accel_z(int16_t z);

/**
 * Set accelerometer orientation state
 * @param orientation Unsigned 8-bit orientation state (0-255)
 */
esp_err_t printer_emulator_set_accel_orientation(uint8_t orientation);

/**
 * Set suspend decrement mode (for unlimited testing)
 * When enabled, print count won't decrease after printing
 */
esp_err_t printer_emulator_set_suspend_decrement(bool suspend);

/**
 * Get suspend decrement mode status
 */
bool printer_emulator_get_suspend_decrement(void);

/**
 * Set auto-sleep timeout (newly discovered protocol feature - Dec 2024)
 * @param timeout_minutes Timeout in minutes (0 = never, 1-255 = minutes)
 */
esp_err_t printer_emulator_set_auto_sleep(uint8_t timeout_minutes);

/**
 * Set print mode (newly discovered protocol feature - Dec 2024)
 * @param mode Print mode: 0x00 = Rich, 0x03 = Natural
 */
esp_err_t printer_emulator_set_print_mode(uint8_t mode);

/**
 * Set Device Information Service values (per-model configurable)
 * These override the default values for the current model
 */
esp_err_t printer_emulator_set_model_number(const char *model_number);
esp_err_t printer_emulator_set_serial_number(const char *serial_number);
esp_err_t printer_emulator_set_firmware_revision(const char *firmware_revision);
esp_err_t printer_emulator_set_hardware_revision(const char *hardware_revision);
esp_err_t printer_emulator_set_software_revision(const char *software_revision);
esp_err_t printer_emulator_set_manufacturer_name(const char *manufacturer_name);

/**
 * Reset Device Information Service values to model defaults
 * Call this when switching printer models or to restore defaults
 */
esp_err_t printer_emulator_reset_dis_to_defaults(void);

/**
 * Get printer model as string
 */
const char* printer_emulator_model_to_string(instax_model_t model);

/**
 * Check if BLE is advertising
 */
bool printer_emulator_is_advertising(void);

/**
 * Dump complete configuration to serial monitor
 * Useful for debugging and diagnostics
 */
void printer_emulator_dump_config(void);

/**
 * Abort current print job and cleanup resources
 * Called on: disconnect, error, timeout
 * Frees print buffer and closes file without updating counters
 */
void printer_emulator_abort_print(void);

#endif // PRINTER_EMULATOR_H
