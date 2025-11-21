/**
 * @file console.h
 * @brief Serial console for WiFi configuration and system control
 */

#ifndef CONSOLE_H
#define CONSOLE_H

#include "esp_err.h"

/**
 * Initialize and start the serial console
 * @return ESP_OK on success
 */
esp_err_t console_init(void);

/**
 * Print help message
 */
void console_print_help(void);

#endif // CONSOLE_H
