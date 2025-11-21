/**
 * @file ble_scanner.c
 * @brief BLE scanner and Instax printer connection management using NimBLE
 */

#include "ble_scanner.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "host/ble_hs.h"
#include "host/ble_gap.h"
#include "host/ble_gatt.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "services/gap/ble_svc_gap.h"

static const char *TAG = "ble_scanner";

// BLE state
static ble_state_t s_state = BLE_STATE_IDLE;
static SemaphoreHandle_t s_state_mutex;
static ble_discovered_device_t s_discovered[MAX_DISCOVERED_PRINTERS];
static int s_discovered_count = 0;

// Connection handle
static uint16_t s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
static uint16_t s_write_handle = 0;
static uint16_t s_notify_handle = 0;

// Callbacks
static ble_scan_result_callback_t s_scan_callback = NULL;
static ble_connection_callback_t s_connection_callback = NULL;
static ble_data_callback_t s_data_callback = NULL;

// Instax service UUID (128-bit)
static const ble_uuid128_t instax_service_uuid = BLE_UUID128_INIT(
    0x73, 0x52, 0x0d, 0xd2, 0x1d, 0x81, 0x5f, 0x9e,
    0x3d, 0x47, 0x83, 0x2d, 0x82, 0x47, 0x95, 0x70
);

// Instax write characteristic UUID (128-bit)
static const ble_uuid128_t instax_write_char_uuid = BLE_UUID128_INIT(
    0x73, 0x52, 0x0d, 0xd2, 0x1d, 0x81, 0x5f, 0x9e,
    0x3d, 0x47, 0x83, 0x2d, 0x83, 0x47, 0x95, 0x70
);

// Instax notify characteristic UUID (128-bit)
static const ble_uuid128_t instax_notify_char_uuid = BLE_UUID128_INIT(
    0x73, 0x52, 0x0d, 0xd2, 0x1d, 0x81, 0x5f, 0x9e,
    0x3d, 0x47, 0x83, 0x2d, 0x84, 0x47, 0x95, 0x70
);

static void set_state(ble_state_t new_state) {
    xSemaphoreTake(s_state_mutex, portMAX_DELAY);
    s_state = new_state;
    xSemaphoreGive(s_state_mutex);

    if (s_connection_callback) {
        s_connection_callback(new_state);
    }
}

// Check if a device name suggests it's an Instax printer
static bool is_instax_device(const char *name) {
    if (name == NULL || strlen(name) == 0) return false;

    // Instax printer names typically contain "INSTAX" or specific model names
    if (strcasestr(name, "INSTAX") != NULL) return true;
    if (strcasestr(name, "Link") != NULL) return true;
    if (strcasestr(name, "Share") != NULL) return true;

    return false;
}

// GAP event handler
static int gap_event_handler(struct ble_gap_event *event, void *arg) {
    switch (event->type) {
        case BLE_GAP_EVENT_DISC: {
            // Device discovered during scan
            struct ble_hs_adv_fields fields;
            int rc = ble_hs_adv_parse_fields(&fields, event->disc.data,
                                              event->disc.length_data);
            if (rc != 0) break;

            char name[32] = {0};
            if (fields.name != NULL && fields.name_len > 0) {
                size_t len = fields.name_len < sizeof(name) - 1 ?
                             fields.name_len : sizeof(name) - 1;
                memcpy(name, fields.name, len);
            }

            // Check if it's an Instax device
            bool is_instax = is_instax_device(name);

            // Add to discovered list if not already present
            if (s_discovered_count < MAX_DISCOVERED_PRINTERS) {
                bool already_found = false;
                for (int i = 0; i < s_discovered_count; i++) {
                    if (memcmp(s_discovered[i].address, event->disc.addr.val, 6) == 0) {
                        already_found = true;
                        break;
                    }
                }

                if (!already_found) {
                    ble_discovered_device_t *dev = &s_discovered[s_discovered_count];
                    strncpy(dev->name, name, sizeof(dev->name) - 1);
                    memcpy(dev->address, event->disc.addr.val, 6);
                    dev->rssi = event->disc.rssi;
                    dev->is_instax = is_instax;
                    s_discovered_count++;

                    ESP_LOGI(TAG, "Discovered: %s [%02x:%02x:%02x:%02x:%02x:%02x] RSSI=%d %s",
                             name,
                             dev->address[0], dev->address[1], dev->address[2],
                             dev->address[3], dev->address[4], dev->address[5],
                             dev->rssi,
                             is_instax ? "(Instax)" : "");

                    if (s_scan_callback) {
                        s_scan_callback(dev);
                    }
                }
            }
            break;
        }

        case BLE_GAP_EVENT_DISC_COMPLETE:
            ESP_LOGI(TAG, "Scan complete");
            set_state(BLE_STATE_IDLE);
            break;

        case BLE_GAP_EVENT_CONNECT:
            if (event->connect.status == 0) {
                ESP_LOGI(TAG, "Connected, handle=%d", event->connect.conn_handle);
                s_conn_handle = event->connect.conn_handle;
                set_state(BLE_STATE_CONNECTED);

                // Start service discovery
                // TODO: Implement GATT service/characteristic discovery
            } else {
                ESP_LOGE(TAG, "Connection failed, status=%d", event->connect.status);
                set_state(BLE_STATE_ERROR);
            }
            break;

        case BLE_GAP_EVENT_DISCONNECT:
            ESP_LOGI(TAG, "Disconnected, reason=%d", event->disconnect.reason);
            s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
            s_write_handle = 0;
            s_notify_handle = 0;
            set_state(BLE_STATE_DISCONNECTED);
            break;

        case BLE_GAP_EVENT_NOTIFY_RX:
            // Data received from printer
            ESP_LOGI(TAG, "Notification received, len=%d", event->notify_rx.om->om_len);
            if (s_data_callback) {
                s_data_callback(event->notify_rx.om->om_data, event->notify_rx.om->om_len);
            }
            break;

        default:
            break;
    }

    return 0;
}

static void ble_host_task(void *param) {
    ESP_LOGI(TAG, "BLE host task started");
    nimble_port_run();
    nimble_port_freertos_deinit();
}

static void on_sync(void) {
    ESP_LOGI(TAG, "BLE host synced");

    // Set device name
    ble_svc_gap_device_name_set("ESP32-Instax-Bridge");

    // Ready to scan/connect
    set_state(BLE_STATE_IDLE);
}

static void on_reset(int reason) {
    ESP_LOGE(TAG, "BLE host reset, reason=%d", reason);
}

esp_err_t ble_scanner_init(void) {
    s_state_mutex = xSemaphoreCreateMutex();
    if (s_state_mutex == NULL) {
        return ESP_ERR_NO_MEM;
    }

    // Initialize NimBLE
    esp_err_t ret = nimble_port_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init NimBLE: %s", esp_err_to_name(ret));
        return ret;
    }

    // Configure host callbacks
    ble_hs_cfg.sync_cb = on_sync;
    ble_hs_cfg.reset_cb = on_reset;

    // Initialize GAP service
    ble_svc_gap_init();

    // Start host task
    nimble_port_freertos_init(ble_host_task);

    ESP_LOGI(TAG, "BLE scanner initialized");
    return ESP_OK;
}

esp_err_t ble_scanner_start_scan(uint32_t duration_sec) {
    if (s_state == BLE_STATE_SCANNING) {
        return ESP_OK; // Already scanning
    }

    s_discovered_count = 0;
    memset(s_discovered, 0, sizeof(s_discovered));

    struct ble_gap_disc_params disc_params = {
        .filter_duplicates = 1,
        .passive = 0,
        .itvl = 0,
        .window = 0,
        .filter_policy = 0,
        .limited = 0,
    };

    int32_t duration_ms = (duration_sec == 0) ? BLE_HS_FOREVER : (duration_sec * 1000);

    int rc = ble_gap_disc(BLE_OWN_ADDR_PUBLIC, duration_ms, &disc_params,
                          gap_event_handler, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to start scan: %d", rc);
        return ESP_FAIL;
    }

    set_state(BLE_STATE_SCANNING);
    ESP_LOGI(TAG, "Scan started (duration=%lu sec)", duration_sec);
    return ESP_OK;
}

esp_err_t ble_scanner_stop_scan(void) {
    int rc = ble_gap_disc_cancel();
    if (rc != 0 && rc != BLE_HS_EALREADY) {
        ESP_LOGE(TAG, "Failed to stop scan: %d", rc);
        return ESP_FAIL;
    }

    set_state(BLE_STATE_IDLE);
    return ESP_OK;
}

int ble_scanner_get_discovered(ble_discovered_device_t *devices, int max_devices) {
    int count = (s_discovered_count < max_devices) ? s_discovered_count : max_devices;
    memcpy(devices, s_discovered, count * sizeof(ble_discovered_device_t));
    return count;
}

void ble_scanner_clear_discovered(void) {
    s_discovered_count = 0;
    memset(s_discovered, 0, sizeof(s_discovered));
}

esp_err_t ble_scanner_connect(const uint8_t *address) {
    if (s_state == BLE_STATE_SCANNING) {
        ble_scanner_stop_scan();
    }

    ble_addr_t addr = {
        .type = BLE_ADDR_PUBLIC,
    };
    memcpy(addr.val, address, 6);

    set_state(BLE_STATE_CONNECTING);

    int rc = ble_gap_connect(BLE_OWN_ADDR_PUBLIC, &addr, 30000, NULL,
                             gap_event_handler, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to initiate connection: %d", rc);
        set_state(BLE_STATE_ERROR);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Connecting to %02x:%02x:%02x:%02x:%02x:%02x",
             address[0], address[1], address[2],
             address[3], address[4], address[5]);
    return ESP_OK;
}

esp_err_t ble_scanner_disconnect(void) {
    if (s_conn_handle == BLE_HS_CONN_HANDLE_NONE) {
        return ESP_OK;
    }

    int rc = ble_gap_terminate(s_conn_handle, BLE_ERR_REM_USER_CONN_TERM);
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to disconnect: %d", rc);
        return ESP_FAIL;
    }

    return ESP_OK;
}

ble_state_t ble_scanner_get_state(void) {
    ble_state_t state;
    xSemaphoreTake(s_state_mutex, portMAX_DELAY);
    state = s_state;
    xSemaphoreGive(s_state_mutex);
    return state;
}

bool ble_scanner_is_connected(void) {
    return ble_scanner_get_state() == BLE_STATE_CONNECTED;
}

esp_err_t ble_scanner_write(const uint8_t *data, size_t len) {
    if (!ble_scanner_is_connected() || s_write_handle == 0) {
        return ESP_ERR_INVALID_STATE;
    }

    int rc = ble_gattc_write_flat(s_conn_handle, s_write_handle, data, len, NULL, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to write: %d", rc);
        return ESP_FAIL;
    }

    return ESP_OK;
}

void ble_scanner_register_scan_callback(ble_scan_result_callback_t callback) {
    s_scan_callback = callback;
}

void ble_scanner_register_connection_callback(ble_connection_callback_t callback) {
    s_connection_callback = callback;
}

void ble_scanner_register_data_callback(ble_data_callback_t callback) {
    s_data_callback = callback;
}

esp_err_t ble_scanner_query_printer_info(instax_printer_info_t *info) {
    // TODO: Implement full info query sequence
    // 1. Send image support info query
    // 2. Send battery info query
    // 3. Send printer function info query
    // 4. Parse responses and populate info struct
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t ble_scanner_print_image(const uint8_t *image_data, size_t image_len,
                                   instax_model_t model,
                                   instax_print_progress_callback_t progress_callback) {
    if (!ble_scanner_is_connected()) {
        return ESP_ERR_INVALID_STATE;
    }

    const instax_model_info_t *model_info = instax_get_model_info(model);
    if (model_info == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    instax_print_progress_t progress = {
        .status = INSTAX_PRINT_STARTING,
        .total_bytes = image_len,
        .bytes_sent = 0,
        .percent_complete = 0,
        .error_message = {0}
    };

    uint8_t packet_buffer[INSTAX_MAX_BLE_PACKET_SIZE + 10];
    size_t packet_len;

    // Send print start
    packet_len = instax_create_print_start(image_len, packet_buffer, sizeof(packet_buffer));
    if (packet_len == 0 || ble_scanner_write(packet_buffer, packet_len) != ESP_OK) {
        strcpy(progress.error_message, "Failed to send print start");
        progress.status = INSTAX_PRINT_ERROR;
        if (progress_callback) progress_callback(&progress);
        return ESP_FAIL;
    }

    vTaskDelay(pdMS_TO_TICKS(100));

    // Send image data in chunks
    progress.status = INSTAX_PRINT_SENDING_DATA;
    size_t offset = 0;
    uint32_t chunk_index = 0;
    size_t chunk_size = model_info->chunk_size;

    while (offset < image_len) {
        size_t remaining = image_len - offset;
        size_t this_chunk = (remaining < chunk_size) ? remaining : chunk_size;

        packet_len = instax_create_print_data(chunk_index, &image_data[offset], this_chunk,
                                               packet_buffer, sizeof(packet_buffer));
        if (packet_len == 0 || ble_scanner_write(packet_buffer, packet_len) != ESP_OK) {
            strcpy(progress.error_message, "Failed to send image data");
            progress.status = INSTAX_PRINT_ERROR;
            if (progress_callback) progress_callback(&progress);
            return ESP_FAIL;
        }

        offset += this_chunk;
        chunk_index++;
        progress.bytes_sent = offset;
        progress.percent_complete = (offset * 100) / image_len;

        if (progress_callback) {
            progress_callback(&progress);
        }

        // Delay between packets (75ms for Link 3 compatibility)
        vTaskDelay(pdMS_TO_TICKS(75));
    }

    // Send print end
    progress.status = INSTAX_PRINT_FINISHING;
    if (progress_callback) progress_callback(&progress);

    packet_len = instax_create_print_end(packet_buffer, sizeof(packet_buffer));
    if (packet_len == 0 || ble_scanner_write(packet_buffer, packet_len) != ESP_OK) {
        strcpy(progress.error_message, "Failed to send print end");
        progress.status = INSTAX_PRINT_ERROR;
        if (progress_callback) progress_callback(&progress);
        return ESP_FAIL;
    }

    vTaskDelay(pdMS_TO_TICKS(100));

    // Send LED pattern (required for Link 3)
    packet_len = instax_create_led_pattern(packet_buffer, sizeof(packet_buffer));
    ble_scanner_write(packet_buffer, packet_len);

    vTaskDelay(pdMS_TO_TICKS(1000)); // Link 3 needs 1 second delay

    // Send print execute
    progress.status = INSTAX_PRINT_EXECUTING;
    if (progress_callback) progress_callback(&progress);

    packet_len = instax_create_print_execute(packet_buffer, sizeof(packet_buffer));
    if (packet_len == 0 || ble_scanner_write(packet_buffer, packet_len) != ESP_OK) {
        strcpy(progress.error_message, "Failed to send print execute");
        progress.status = INSTAX_PRINT_ERROR;
        if (progress_callback) progress_callback(&progress);
        return ESP_FAIL;
    }

    progress.status = INSTAX_PRINT_COMPLETE;
    progress.percent_complete = 100;
    if (progress_callback) progress_callback(&progress);

    return ESP_OK;
}
