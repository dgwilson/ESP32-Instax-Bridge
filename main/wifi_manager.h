/**
 * @file wifi_manager.h
 * @brief WiFi connection management with NVS credential storage
 */

#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <stdbool.h>
#include "esp_err.h"

#define WIFI_SSID_MAX_LEN       32
#define WIFI_PASSWORD_MAX_LEN   64

// WiFi connection status
typedef enum {
    WIFI_STATUS_DISCONNECTED = 0,
    WIFI_STATUS_CONNECTING,
    WIFI_STATUS_CONNECTED,
    WIFI_STATUS_FAILED
} wifi_status_t;

// Callback for WiFi events
typedef void (*wifi_event_callback_t)(wifi_status_t status);

/**
 * Initialize WiFi manager
 * @return ESP_OK on success
 */
esp_err_t wifi_manager_init(void);

/**
 * Set WiFi credentials and save to NVS
 * @param ssid WiFi SSID
 * @param password WiFi password
 * @return ESP_OK on success
 */
esp_err_t wifi_manager_set_credentials(const char *ssid, const char *password);

/**
 * Get stored WiFi credentials
 * @param ssid Buffer for SSID (min WIFI_SSID_MAX_LEN+1 bytes)
 * @param password Buffer for password (min WIFI_PASSWORD_MAX_LEN+1 bytes)
 * @return ESP_OK if credentials found, ESP_ERR_NOT_FOUND otherwise
 */
esp_err_t wifi_manager_get_credentials(char *ssid, char *password);

/**
 * Clear stored WiFi credentials
 * @return ESP_OK on success
 */
esp_err_t wifi_manager_clear_credentials(void);

/**
 * Connect to WiFi using stored credentials
 * @return ESP_OK if connection started
 */
esp_err_t wifi_manager_connect(void);

/**
 * Disconnect from WiFi
 * @return ESP_OK on success
 */
esp_err_t wifi_manager_disconnect(void);

/**
 * Get current WiFi status
 * @return Current connection status
 */
wifi_status_t wifi_manager_get_status(void);

/**
 * Get IP address (only valid when connected)
 * @param ip_str Buffer for IP string (min 16 bytes)
 * @return ESP_OK if connected and IP available
 */
esp_err_t wifi_manager_get_ip(char *ip_str);

/**
 * Register callback for WiFi events
 * @param callback Function to call on WiFi events
 */
void wifi_manager_register_callback(wifi_event_callback_t callback);

/**
 * Check if WiFi credentials are stored
 * @return true if credentials exist
 */
bool wifi_manager_has_credentials(void);

#endif // WIFI_MANAGER_H
