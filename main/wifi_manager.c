/**
 * @file wifi_manager.c
 * @brief WiFi connection management with NVS credential storage
 */

#include "wifi_manager.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_netif.h"
#include "mdns.h"
#include "ble_peripheral.h"

static const char *TAG = "wifi_manager";

// NVS namespace and keys
#define NVS_NAMESPACE       "wifi_config"
#define NVS_KEY_SSID        "ssid"
#define NVS_KEY_PASSWORD    "password"

// Event group bits
#define WIFI_CONNECTED_BIT  BIT0
#define WIFI_FAIL_BIT       BIT1

static EventGroupHandle_t s_wifi_event_group;
static wifi_status_t s_wifi_status = WIFI_STATUS_DISCONNECTED;
static wifi_event_callback_t s_event_callback = NULL;
static esp_netif_t *s_sta_netif = NULL;
static int s_retry_count = 0;
static const int MAX_RETRY = 5;

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data) {
    if (event_base == WIFI_EVENT) {
        switch (event_id) {
            case WIFI_EVENT_STA_START:
                ESP_LOGI(TAG, "WiFi STA started, connecting...");
                s_wifi_status = WIFI_STATUS_CONNECTING;
                esp_wifi_connect();
                break;

            case WIFI_EVENT_STA_DISCONNECTED:
                if (s_retry_count < MAX_RETRY) {
                    esp_wifi_connect();
                    s_retry_count++;
                    ESP_LOGI(TAG, "Retrying connection (%d/%d)", s_retry_count, MAX_RETRY);
                } else {
                    xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
                    s_wifi_status = WIFI_STATUS_FAILED;
                    ESP_LOGE(TAG, "Connection failed after %d retries", MAX_RETRY);
                    if (s_event_callback) {
                        s_event_callback(WIFI_STATUS_FAILED);
                    }
                }
                break;

            default:
                break;
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_count = 0;
        s_wifi_status = WIFI_STATUS_CONNECTED;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);

        // Get IP address as string
        char ip_str[16];
        sprintf(ip_str, IPSTR, IP2STR(&event->ip_info.ip));

        // Initialize mDNS
        esp_err_t err = mdns_init();
        if (err == ESP_OK) {
            mdns_hostname_set("instax-simulator");
            mdns_instance_name_set("ESP32 Instax Printer Emulator");

            // Add HTTP service
            mdns_service_add(NULL, "_http", "_tcp", 80, NULL, 0);

            ESP_LOGI(TAG, "mDNS responder started: instax-simulator.local");
        } else {
            ESP_LOGW(TAG, "Failed to initialize mDNS: %s", esp_err_to_name(err));
        }

        // Update BLE device name with IP address
        ble_peripheral_update_device_name_with_ip(ip_str);

        if (s_event_callback) {
            s_event_callback(WIFI_STATUS_CONNECTED);
        }
    }
}

esp_err_t wifi_manager_init(void) {
    esp_err_t ret;

    // Initialize NVS
    ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Create event group
    s_wifi_event_group = xEventGroupCreate();

    // Initialize TCP/IP stack
    ESP_ERROR_CHECK(esp_netif_init());

    // Create default event loop if it doesn't exist
    ret = esp_event_loop_create_default();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "Failed to create event loop: %s", esp_err_to_name(ret));
        return ret;
    }

    s_sta_netif = esp_netif_create_default_wifi_sta();

    // Initialize WiFi with default config
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    // Register event handlers
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                         ESP_EVENT_ANY_ID,
                                                         &wifi_event_handler,
                                                         NULL,
                                                         NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                         IP_EVENT_STA_GOT_IP,
                                                         &wifi_event_handler,
                                                         NULL,
                                                         NULL));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));

    ESP_LOGI(TAG, "WiFi manager initialized");
    return ESP_OK;
}

esp_err_t wifi_manager_set_credentials(const char *ssid, const char *password) {
    nvs_handle_t handle;
    esp_err_t ret;

    ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Error opening NVS: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = nvs_set_str(handle, NVS_KEY_SSID, ssid);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Error saving SSID: %s", esp_err_to_name(ret));
        nvs_close(handle);
        return ret;
    }

    ret = nvs_set_str(handle, NVS_KEY_PASSWORD, password);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Error saving password: %s", esp_err_to_name(ret));
        nvs_close(handle);
        return ret;
    }

    ret = nvs_commit(handle);
    nvs_close(handle);

    ESP_LOGI(TAG, "WiFi credentials saved for SSID: %s", ssid);
    return ret;
}

esp_err_t wifi_manager_get_credentials(char *ssid, char *password) {
    nvs_handle_t handle;
    esp_err_t ret;
    size_t len;

    ret = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (ret != ESP_OK) {
        return ESP_ERR_NOT_FOUND;
    }

    len = WIFI_SSID_MAX_LEN + 1;
    ret = nvs_get_str(handle, NVS_KEY_SSID, ssid, &len);
    if (ret != ESP_OK) {
        nvs_close(handle);
        return ESP_ERR_NOT_FOUND;
    }

    len = WIFI_PASSWORD_MAX_LEN + 1;
    ret = nvs_get_str(handle, NVS_KEY_PASSWORD, password, &len);
    if (ret != ESP_OK) {
        nvs_close(handle);
        return ESP_ERR_NOT_FOUND;
    }

    nvs_close(handle);
    return ESP_OK;
}

esp_err_t wifi_manager_clear_credentials(void) {
    nvs_handle_t handle;
    esp_err_t ret;

    ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (ret != ESP_OK) {
        return ret;
    }

    nvs_erase_key(handle, NVS_KEY_SSID);
    nvs_erase_key(handle, NVS_KEY_PASSWORD);
    nvs_commit(handle);
    nvs_close(handle);

    ESP_LOGI(TAG, "WiFi credentials cleared");
    return ESP_OK;
}

esp_err_t wifi_manager_connect(void) {
    char ssid[WIFI_SSID_MAX_LEN + 1];
    char password[WIFI_PASSWORD_MAX_LEN + 1];

    if (wifi_manager_get_credentials(ssid, password) != ESP_OK) {
        ESP_LOGE(TAG, "No WiFi credentials stored");
        return ESP_ERR_NOT_FOUND;
    }

    wifi_config_t wifi_config = {0};
    strncpy((char *)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char *)wifi_config.sta.password, password, sizeof(wifi_config.sta.password) - 1);
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    s_retry_count = 0;
    s_wifi_status = WIFI_STATUS_CONNECTING;

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "Connecting to SSID: %s", ssid);
    return ESP_OK;
}

esp_err_t wifi_manager_disconnect(void) {
    s_wifi_status = WIFI_STATUS_DISCONNECTED;
    return esp_wifi_disconnect();
}

wifi_status_t wifi_manager_get_status(void) {
    return s_wifi_status;
}

esp_err_t wifi_manager_get_ip(char *ip_str) {
    if (s_wifi_status != WIFI_STATUS_CONNECTED) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_netif_ip_info_t ip_info;
    esp_err_t ret = esp_netif_get_ip_info(s_sta_netif, &ip_info);
    if (ret == ESP_OK) {
        sprintf(ip_str, IPSTR, IP2STR(&ip_info.ip));
    }
    return ret;
}

void wifi_manager_register_callback(wifi_event_callback_t callback) {
    s_event_callback = callback;
}

bool wifi_manager_has_credentials(void) {
    char ssid[WIFI_SSID_MAX_LEN + 1];
    char password[WIFI_PASSWORD_MAX_LEN + 1];
    return wifi_manager_get_credentials(ssid, password) == ESP_OK;
}
