/**
 * @file web_server.h
 * @brief HTTP web server for Instax bridge control interface
 */

#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include "esp_err.h"

/**
 * Initialize and start the web server
 * @return ESP_OK on success
 */
esp_err_t web_server_start(void);

/**
 * Stop the web server
 * @return ESP_OK on success
 */
esp_err_t web_server_stop(void);

/**
 * Check if web server is running
 * @return true if running
 */
bool web_server_is_running(void);

#endif // WEB_SERVER_H
