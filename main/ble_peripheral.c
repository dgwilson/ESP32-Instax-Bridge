/**
 * @file ble_peripheral.c
 * @brief BLE Peripheral Implementation for Instax Printer Emulation
 */

#include "ble_peripheral.h"
#include "instax_protocol.h"
#include "printer_emulator.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "host/ble_hs.h"
#include "host/ble_gap.h"
#include "host/ble_gatt.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "services/dis/ble_svc_dis.h"

static const char *TAG = "ble_peripheral";

// Instax packet header constants (for packet reassembly)
#define INSTAX_HEADER_TO_DEVICE_0       0x41
#define INSTAX_HEADER_TO_DEVICE_1       0x62

// BLE state
static bool s_advertising = false;
static bool s_connected = false;
static uint16_t s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
static uint16_t s_notify_handle = 0;

// Print state
static uint32_t s_print_image_size = 0;
static uint32_t s_print_bytes_received = 0;
static uint32_t s_print_chunk_index = 0;

// Packet reassembly buffer for handling fragmented BLE writes
#define PACKET_BUFFER_SIZE 4096
static uint8_t s_packet_buffer[PACKET_BUFFER_SIZE];
static size_t s_packet_buffer_len = 0;
static uint16_t s_expected_packet_len = 0;

// Callbacks
static ble_peripheral_print_start_callback_t s_print_start_callback = NULL;
static ble_peripheral_print_data_callback_t s_print_data_callback = NULL;
static ble_peripheral_print_complete_callback_t s_print_complete_callback = NULL;

// Instax service UUID: 70954782-2d83-473d-9e5f-81e1d02d5273
static const ble_uuid128_t instax_service_uuid = BLE_UUID128_INIT(
    0x73, 0x52, 0x2d, 0xd0, 0xe1, 0x81, 0x5f, 0x9e,
    0x3d, 0x47, 0x83, 0x2d, 0x82, 0x47, 0x95, 0x70
);

// Instax write characteristic UUID: 70954783-2d83-473d-9e5f-81e1d02d5273
static const ble_uuid128_t instax_write_char_uuid = BLE_UUID128_INIT(
    0x73, 0x52, 0x2d, 0xd0, 0xe1, 0x81, 0x5f, 0x9e,
    0x3d, 0x47, 0x83, 0x2d, 0x83, 0x47, 0x95, 0x70
);

// Instax notify characteristic UUID: 70954784-2d83-473d-9e5f-81e1d02d5273
static const ble_uuid128_t instax_notify_char_uuid = BLE_UUID128_INIT(
    0x73, 0x52, 0x2d, 0xd0, 0xe1, 0x81, 0x5f, 0x9e,
    0x3d, 0x47, 0x83, 0x2d, 0x84, 0x47, 0x95, 0x70
);

// Forward declarations
static int gatt_svr_chr_access(uint16_t conn_handle, uint16_t attr_handle,
                               struct ble_gatt_access_ctxt *ctxt, void *arg);
static const char* get_model_number_for_printer(instax_model_t model);

// GATT service definition
static const struct ble_gatt_svc_def gatt_svr_svcs[] = {
    {
        // Instax Service
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &instax_service_uuid.u,
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                // Write Characteristic (for commands and print data from app)
                .uuid = &instax_write_char_uuid.u,
                .access_cb = gatt_svr_chr_access,
                .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP,
            },
            {
                // Notify Characteristic (for responses to app)
                .uuid = &instax_notify_char_uuid.u,
                .access_cb = gatt_svr_chr_access,
                .val_handle = &s_notify_handle,
                .flags = BLE_GATT_CHR_F_NOTIFY | BLE_GATT_CHR_F_READ,
            },
            {
                0, // No more characteristics
            }
        },
    },
    {
        0, // No more services
    },
};

/**
 * Send a notification to the connected client
 */
static esp_err_t send_notification(const uint8_t *data, size_t len) {
    if (!s_connected || s_conn_handle == BLE_HS_CONN_HANDLE_NONE) {
        ESP_LOGW(TAG, "Cannot send notification: not connected");
        return ESP_ERR_INVALID_STATE;
    }

    struct os_mbuf *om = ble_hs_mbuf_from_flat(data, len);
    if (om == NULL) {
        ESP_LOGE(TAG, "Failed to allocate mbuf for notification");
        return ESP_ERR_NO_MEM;
    }

    int rc = ble_gatts_notify_custom(s_conn_handle, s_notify_handle, om);
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to send notification: %d", rc);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "📤 Sent response (%d bytes)", len);
    // Log first 16 bytes of response for debugging
    ESP_LOGI(TAG, "   First bytes: %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X",
            len > 0 ? data[0] : 0, len > 1 ? data[1] : 0,
            len > 2 ? data[2] : 0, len > 3 ? data[3] : 0,
            len > 4 ? data[4] : 0, len > 5 ? data[5] : 0,
            len > 6 ? data[6] : 0, len > 7 ? data[7] : 0,
            len > 8 ? data[8] : 0, len > 9 ? data[9] : 0,
            len > 10 ? data[10] : 0, len > 11 ? data[11] : 0,
            len > 12 ? data[12] : 0, len > 13 ? data[13] : 0,
            len > 14 ? data[14] : 0, len > 15 ? data[15] : 0);
    return ESP_OK;
}

/**
 * Handle Instax protocol packet
 */
static void handle_instax_packet(const uint8_t *data, size_t len) {
    uint8_t function, operation;
    const uint8_t *payload;
    size_t payload_len;

    ESP_LOGI(TAG, "🔍 Parsing packet (%d bytes)...", len);

    // Parse command packet (from app to device)
    if (!instax_parse_command(data, len, &function, &operation, &payload, &payload_len)) {
        ESP_LOGE(TAG, "❌ Failed to parse Instax command!");
        // Log first few bytes for debugging
        ESP_LOGE(TAG, "Packet hex: %02x %02x %02x %02x %02x %02x...",
                 len > 0 ? data[0] : 0, len > 1 ? data[1] : 0, len > 2 ? data[2] : 0,
                 len > 3 ? data[3] : 0, len > 4 ? data[4] : 0, len > 5 ? data[5] : 0);
        return;
    }

    ESP_LOGI(TAG, "✅ Parsed: func=0x%02x op=0x%02x payload_len=%d",
             function, operation, payload_len);

    // Create response buffer
    uint8_t response[256];
    size_t response_len = 0;

    // Handle based on function code
    switch (function) {
        case INSTAX_FUNC_INFO: {
            // Info request
            const instax_printer_info_t *info = printer_emulator_get_info();

            // Official Instax app sends func=0x00 op=0x00 as a general "ping/identify" command
            if (operation == 0x00) {
                ESP_LOGI(TAG, "General identify/ping command - sending device info");
                response[0] = INSTAX_HEADER_FROM_DEVICE_0;
                response[1] = INSTAX_HEADER_FROM_DEVICE_1;
                response_len = 16; // Header(2) + Length(2) + Func(1) + Op(1) + Payload(9) + Checksum(1)
                response[2] = 0x00; // Length high byte
                response[3] = 0x10; // Length low byte (16 decimal)
                response[4] = function;
                response[5] = operation;
                // Device identification payload (9 bytes) - matches real INSTAX Square Link
                response[6] = 0x00;
                response[7] = 0x01;
                response[8] = 0x00;
                response[9] = 0x02;
                response[10] = 0x00;
                response[11] = 0x00;
                response[12] = 0x00;
                response[13] = 0x00;
                response[14] = 0x00;
                response[15] = instax_calculate_checksum(response, 15);
                send_notification(response, response_len);
            } else if (operation == 0x01) {
                // Official Instax app uses op=0x01 to query printer info (similar to op=0x02)
                // Payload byte indicates what info is requested
                if (payload_len > 0) {
                    uint8_t info_query = payload[0];
                    ESP_LOGI(TAG, "Info query op=0x01 (query type: 0x%02x)", info_query);

                    response[0] = INSTAX_HEADER_FROM_DEVICE_0;
                    response[1] = INSTAX_HEADER_FROM_DEVICE_1;
                    response[4] = function;
                    response[5] = operation;

                    switch (info_query) {
                        case 0x01: {
                            // Model/Firmware info - matches real device format
                            // Real device sends: [00 01] [05] "FI017"
                            ESP_LOGI(TAG, "Sending model/firmware info");
                            const char *model_str = "FI017"; // Real Square Link uses FI017
                            uint8_t model_len = strlen(model_str);
                            response[6] = 0x00; response[7] = 0x01; // Payload header (matches query type)
                            response[8] = model_len; // Length of string
                            memcpy(&response[9], model_str, model_len);
                            response_len = 9 + model_len + 1; // Header(2) + Length(2) + Func(1) + Op(1) + PayloadHdr(2) + Len(1) + String + Checksum(1)
                            break;
                        }
                        case 0x02: {
                            // Serial number - matches real device format
                            // Real device sends: [00 02] [08] "50196562"
                            ESP_LOGI(TAG, "Sending serial number");
                            const char *serial = "50196563"; // Our device serial
                            uint8_t serial_len = strlen(serial);
                            response[6] = 0x00; response[7] = 0x02; // Payload header (matches query type)
                            response[8] = serial_len; // Length of string
                            memcpy(&response[9], serial, serial_len);
                            response_len = 9 + serial_len + 1; // Header(2) + Length(2) + Func(1) + Op(1) + PayloadHdr(2) + Len(1) + String + Checksum(1)
                            break;
                        }
                        case 0x03: {
                            // Additional device info - matches real device format
                            // Real device sends: [00 03] [04] "0000"
                            ESP_LOGI(TAG, "Sending additional device info");
                            const char *info_str = "0000"; // Real device sends this
                            uint8_t info_len = strlen(info_str);
                            response[6] = 0x00; response[7] = 0x03; // Payload header (matches query type)
                            response[8] = info_len; // Length of string
                            memcpy(&response[9], info_str, info_len);
                            response_len = 9 + info_len + 1; // Header(2) + Length(2) + Func(1) + Op(1) + PayloadHdr(2) + Len(1) + String + Checksum(1)
                            break;
                        }
                        default:
                            ESP_LOGW(TAG, "Unknown info query: 0x%02x - sending ACK", info_query);
                            response[6] = 0x00; // Status: OK
                            response_len = 7;
                            break;
                    }

                    // Fill in packet length (total bytes)
                    response[2] = (response_len >> 8) & 0xFF;
                    response[3] = response_len & 0xFF;
                    // Add checksum (last byte before response_len position)
                    response[response_len - 1] = instax_calculate_checksum(response, response_len - 1);
                    send_notification(response, response_len);
                }
            } else if (operation == INSTAX_OP_SUPPORT_FUNCTION_INFO) {
                // Return supported functions info
                ESP_LOGI(TAG, "Info request: operation=0x%02x, payload_len=%d", operation, payload_len);

                // Official Instax app sends this command with payload=0x00 to query image dimensions
                // Moments Print app includes payload byte to specify info type
                if (payload_len == 0 || (payload_len == 1 && payload[0] == 0x00)) {
                    // Query with payload 0x00 - send image dimensions (real device behavior)
                    // Real device sends: [61 42 00 17 00 02 00 00] [03 20 03 20 02 4b 00 06 40 00 01 00 00 00] [69]
                    ESP_LOGI(TAG, "Image dimensions query (payload=0x00) - sending dimensions");
                    response[0] = INSTAX_HEADER_FROM_DEVICE_0;
                    response[1] = INSTAX_HEADER_FROM_DEVICE_1;
                    response_len = 23; // Header(2) + Length(2) + Func(1) + Op(1) + Payload(16) + Checksum(1)
                    response[2] = 0x00; // Length high byte
                    response[3] = 0x17; // Length low byte (23 decimal)
                    response[4] = function;
                    response[5] = operation;
                    response[6] = 0x00; // Payload header byte 0
                    response[7] = 0x00; // Payload header byte 1
                    // Image dimensions and capabilities (matches real Square Link)
                    response[8] = (info->width >> 8) & 0xFF;   // Width high byte (0x03 for 800)
                    response[9] = info->width & 0xFF;          // Width low byte (0x20 for 800)
                    response[10] = (info->height >> 8) & 0xFF; // Height high byte (0x03 for 800)
                    response[11] = info->height & 0xFF;        // Height low byte (0x20 for 800)
                    response[12] = 0x02; // Capability byte 1
                    response[13] = 0x4b; // Capability byte 2
                    response[14] = 0x00; // Capability byte 3
                    response[15] = 0x06; // Capability byte 4
                    response[16] = 0x40; // Capability byte 5
                    response[17] = 0x00; // Capability byte 6
                    response[18] = 0x01; // Capability byte 7
                    response[19] = 0x00; // Capability byte 8
                    response[20] = 0x00; // Capability byte 9
                    response[21] = 0x00; // Capability byte 10
                    response[22] = instax_calculate_checksum(response, 22);
                    send_notification(response, response_len);
                } else if (payload_len > 0) {
                    uint8_t info_type = payload[0];
                    ESP_LOGI(TAG, "Info type: %d", info_type);

                    // Build packet structure: [0-1: header] [2-3: length] [4: func] [5: op] [6+: payload] [last: checksum]
                    response[0] = INSTAX_HEADER_FROM_DEVICE_0;
                    response[1] = INSTAX_HEADER_FROM_DEVICE_1;
                    // Length will be filled in later (bytes 2-3)
                    response[4] = function;
                    response[5] = operation;

                    // Build payload based on info type (starting at byte 6)
                    switch (info_type) {
                        case INSTAX_INFO_IMAGE_SUPPORT:
                            // Payload: [0-1: header] [2-3: width] [4-5: height] [6-15: capabilities]
                            // Real Square device sends: [61 42 00 17 00 02 00 00] [03 20 03 20 02 4b 00 06 40 00 01 00 00 00] [69]
                            ESP_LOGI(TAG, "Sending image support: %dx%d", info->width, info->height);
                            response[6] = 0x00; // Payload header byte 0
                            response[7] = 0x00; // Payload header byte 1 (matches query type)
                            response[8] = (info->width >> 8) & 0xFF;  // Width high byte
                            response[9] = info->width & 0xFF;         // Width low byte
                            response[10] = (info->height >> 8) & 0xFF; // Height high byte
                            response[11] = info->height & 0xFF;        // Height low byte
                            // Extended capabilities (discovered Dec 2024 - required by official app)
                            response[12] = 0x02; // Max file size high byte
                            response[13] = 0x4B; // Max file size low byte (0x024B = 587 KB)
                            response[14] = 0x00; // Unknown
                            response[15] = 0x06; // Unknown (maybe color mode count)
                            response[16] = 0x40; // Unknown high byte
                            response[17] = 0x00; // Unknown low byte (0x4000 = 16384)
                            response[18] = 0x01; // Boolean flag (maybe supports color tables)
                            response[19] = 0x00; // Padding
                            response[20] = 0x00; // Padding
                            response[21] = 0x00; // Padding
                            response_len = 22; // Header(2) + Length(2) + Func(1) + Op(1) + Payload(16) = 22, checksum added later
                            break;

                        case INSTAX_INFO_BATTERY:
                            // Payload: [0-1: header] [2: data_len] [3: state] [4: percentage] [5-6: extra]
                            // Real device sends: [61 42 00 0d 00 02 00 01] [02 32 00 00] [18]
                            ESP_LOGI(TAG, "Sending battery info: state=%d, %d%%",
                                    info->battery_state, info->battery_percentage);
                            response[6] = 0x00; // Payload header byte 0
                            response[7] = 0x01; // Payload header byte 1 (matches query type)
                            response[8] = 0x02; // Data length byte (or status?)
                            response[9] = info->battery_percentage; // Battery percentage (0x32 = 50%)
                            response[10] = 0x00; // Extra byte 1
                            response[11] = 0x00; // Extra byte 2
                            response_len = 12; // Header(2) + Length(2) + Func(1) + Op(1) + Payload(6) = 12, checksum added later
                            break;

                        case INSTAX_INFO_PRINTER_FUNCTION:
                            // Payload: [0-1: header] [2-9: printer data]
                            // Real device sends: [61 42 00 11 00 02 00 02] [28 00 00 0c 00 00 00 00] [13]
                            ESP_LOGI(TAG, "Sending printer function: %d photos, charging=%d",
                                    info->photos_remaining, info->is_charging);
                            response[6] = 0x00; // Payload header byte 0
                            response[7] = 0x02; // Payload header byte 1 (matches query type)

                            // EXPERIMENT 2: Revert to 0x26 (correct physical printer value)
                            // Test with is_charging=false to see if charging state matters
                            // Bit 7 (0x80): Charging status (1 = charging, 0 = not charging)
                            // Bit 5 (0x20): Always 1 (printer type flag)
                            // Bits 2,1 (0x06): Set on physical printer (capability flags)
                            uint8_t capability = 0x26;  // Base: 0x26 = 00100110 (matches physical INSTAX Square capture-3)
                            if (info->is_charging) {
                                capability |= 0x80;  // Set bit 7: 0xA6 = 10100110 (charging)
                            }
                            // EXPERIMENT 2: With is_charging forced to false, this will always be 0x26
                            response[8] = capability;

                            response[9] = 0x00;  // Byte 2
                            response[10] = 0x00; // Byte 3
                            response[11] = info->photos_remaining; // Film count (0x0C = 12 photos)
                            response[12] = 0x00; // Extra byte 1
                            response[13] = 0x00; // Extra byte 2
                            response[14] = 0x00; // Extra byte 3
                            response[15] = 0x00; // Extra byte 4
                            response_len = 16; // Header(2) + Length(2) + Func(1) + Op(1) + Payload(10) = 16, checksum added later
                            break;

                        case INSTAX_INFO_PRINT_HISTORY:
                            // Payload: [0-1: header] [2-5: print count (big-endian)]
                            ESP_LOGI(TAG, "Sending print history: %lu prints",
                                    (unsigned long)info->lifetime_print_count);
                            response[6] = 0x00; // Payload header byte 0
                            response[7] = 0x03; // Payload header byte 1 (MUST match query type 3!)
                            response[8] = (info->lifetime_print_count >> 24) & 0xFF;
                            response[9] = (info->lifetime_print_count >> 16) & 0xFF;
                            response[10] = (info->lifetime_print_count >> 8) & 0xFF;
                            response[11] = info->lifetime_print_count & 0xFF;
                            response_len = 12; // Header(2) + Length(2) + Func(1) + Op(1) + Payload(6)
                            break;

                        default:
                            // Unknown info type, send simple ACK
                            ESP_LOGW(TAG, "Unknown info type: %d", info_type);
                            response[6] = 0x00; // Status: OK
                            response_len = 7; // Header(2) + Length(2) + Func(1) + Op(1) + Status(1)
                            break;
                    }

                    // Fill in packet length (total bytes including checksum)
                    uint16_t packet_len = response_len + 1; // +1 for checksum
                    response[2] = (packet_len >> 8) & 0xFF;
                    response[3] = packet_len & 0xFF;

                    // Add checksum
                    response[response_len] = instax_calculate_checksum(response, response_len);
                    response_len++;

                    send_notification(response, response_len);
                }
            }
            break;
        }

        case INSTAX_FUNC_DEVICE_CONTROL: {
            // Device control operations (newly discovered - Dec 2024)
            ESP_LOGI(TAG, "Device control operation: 0x%02x", operation);

            switch (operation) {
                case INSTAX_OP_AUTO_SLEEP_SETTINGS: {
                    // Auto-sleep settings (function 0x01, operation 0x02)
                    // Payload: [timeout_minutes] [12 bytes padding]
                    // 0x00 = never shutdown, 0x01-0xFF = timeout in minutes
                    if (payload_len >= 1) {
                        uint8_t timeout_minutes = payload[0];
                        printer_emulator_set_auto_sleep(timeout_minutes);
                        ESP_LOGI(TAG, "Auto-sleep timeout set to %d minutes (%s)",
                                timeout_minutes, timeout_minutes == 0 ? "never" : "enabled");
                    } else {
                        ESP_LOGW(TAG, "Auto-sleep command with insufficient payload (%d bytes)", payload_len);
                    }

                    // Send ACK
                    response[0] = INSTAX_HEADER_FROM_DEVICE_0;
                    response[1] = INSTAX_HEADER_FROM_DEVICE_1;
                    response_len = 8;
                    response[2] = (response_len >> 8) & 0xFF;
                    response[3] = response_len & 0xFF;
                    response[4] = function;
                    response[5] = operation;
                    response[6] = 0x00; // Status: OK
                    response[7] = instax_calculate_checksum(response, 7);
                    send_notification(response, response_len);
                    break;
                }

                case INSTAX_OP_BLE_CONNECT: {
                    // BLE connection management (function 0x01, operation 0x03)
                    // Observed in packet captures before print operations
                    ESP_LOGI(TAG, "BLE connection management command (payload: %d bytes)", payload_len);

                    // Send ACK
                    response[0] = INSTAX_HEADER_FROM_DEVICE_0;
                    response[1] = INSTAX_HEADER_FROM_DEVICE_1;
                    response_len = 8;
                    response[2] = (response_len >> 8) & 0xFF;
                    response[3] = response_len & 0xFF;
                    response[4] = function;
                    response[5] = operation;
                    response[6] = 0x00; // Status: OK
                    response[7] = instax_calculate_checksum(response, 7);
                    send_notification(response, response_len);
                    break;
                }

                default:
                    ESP_LOGI(TAG, "Unknown device control operation: 0x%02x - sending ACK", operation);
                    // Send generic ACK for unknown operations
                    response[0] = INSTAX_HEADER_FROM_DEVICE_0;
                    response[1] = INSTAX_HEADER_FROM_DEVICE_1;
                    response_len = 8;
                    response[2] = (response_len >> 8) & 0xFF;
                    response[3] = response_len & 0xFF;
                    response[4] = function;
                    response[5] = operation;
                    response[6] = 0x00; // Status: OK
                    response[7] = instax_calculate_checksum(response, 7);
                    send_notification(response, response_len);
                    break;
            }
            break;
        }

        case INSTAX_FUNC_PRINT: {
            // Print operation
            switch (operation) {
                case INSTAX_OP_PRINT_START: {
                    // Check printer error conditions
                    const instax_printer_info_t *printer_info = printer_emulator_get_info();
                    uint8_t error_code = 0;
                    const char *error_msg = NULL;

                    // Error 178 (0xB2): No film
                    if (printer_info->photos_remaining == 0) {
                        error_code = 0xB2;
                        error_msg = "No film";
                    }
                    // Error 179 (0xB3): Cover open
                    else if (printer_info->cover_open) {
                        error_code = 0xB3;
                        error_msg = "Cover open";
                    }
                    // Error 180 (0xB4): Battery low (< 20%)
                    else if (printer_info->battery_percentage < 20) {
                        error_code = 0xB4;
                        error_msg = "Battery low";
                    }
                    // Error 181 (0xB5): Printer busy
                    else if (printer_info->printer_busy) {
                        error_code = 0xB5;
                        error_msg = "Printer busy";
                    }

                    // Send error response if any error detected
                    if (error_code != 0) {
                        ESP_LOGW(TAG, "Print start rejected: %s (error %d = 0x%02X)", error_msg, error_code, error_code);

                        response[0] = INSTAX_HEADER_FROM_DEVICE_0;
                        response[1] = INSTAX_HEADER_FROM_DEVICE_1;
                        response_len = 8; // Header(2) + Length(2) + Func(1) + Op(1) + Status(1) + Checksum(1)
                        response[2] = (response_len >> 8) & 0xFF; // Length high byte
                        response[3] = response_len & 0xFF;         // Length low byte
                        response[4] = function;
                        response[5] = operation;
                        response[6] = error_code;
                        response[7] = instax_calculate_checksum(response, 7);

                        send_notification(response, response_len);
                        break;
                    }

                    // Extract image size from payload
                    // Payload format: [0-3: header 0x02 0x00 0x00 0x00] [4-7: size big-endian]
                    if (payload_len >= 8) {
                        // Skip first 4 bytes (header), read bytes 4-7 as big-endian uint32
                        s_print_image_size = ((uint32_t)payload[4] << 24) |
                                           ((uint32_t)payload[5] << 16) |
                                           ((uint32_t)payload[6] << 8) |
                                           payload[7];
                        s_print_bytes_received = 0;
                        s_print_chunk_index = 0;

                        ESP_LOGI(TAG, "Print start: size=%lu bytes", (unsigned long)s_print_image_size);

                        if (s_print_start_callback) {
                            s_print_start_callback(s_print_image_size);
                        }

                        // Send ACK with proper packet structure
                        response[0] = INSTAX_HEADER_FROM_DEVICE_0;
                        response[1] = INSTAX_HEADER_FROM_DEVICE_1;
                        response_len = 8; // Header(2) + Length(2) + Func(1) + Op(1) + Status(1) + Checksum(1)
                        response[2] = (response_len >> 8) & 0xFF; // Length high byte
                        response[3] = response_len & 0xFF;         // Length low byte
                        response[4] = function;
                        response[5] = operation;
                        response[6] = 0x00; // Status: OK
                        response[7] = instax_calculate_checksum(response, 7);

                        send_notification(response, response_len);
                    }
                    break;
                }

                case INSTAX_OP_PRINT_DATA: {
                    // Print data chunk
                    // Payload format: [0-3: chunk index] [4+: actual image data]
                    // Logging disabled to prevent BLE packet processing delays
                    // if (s_print_chunk_index % 20 == 0) {
                    //     ESP_LOGD(TAG, "Print data: chunk=%lu len=%d",
                    //             (unsigned long)s_print_chunk_index, payload_len);
                    // }

                    if (s_print_data_callback && payload_len > 4) {
                        // Skip first 4 bytes (chunk index) and only pass the actual image data
                        const uint8_t *image_data = payload + 4;
                        size_t image_data_len = payload_len - 4;
                        s_print_data_callback(s_print_chunk_index, image_data, image_data_len);
                        s_print_bytes_received += image_data_len;
                    }

                    s_print_chunk_index++;

                    // Send ACK with proper packet structure
                    response[0] = INSTAX_HEADER_FROM_DEVICE_0;
                    response[1] = INSTAX_HEADER_FROM_DEVICE_1;
                    response_len = 8; // Header(2) + Length(2) + Func(1) + Op(1) + Status(1) + Checksum(1)
                    response[2] = (response_len >> 8) & 0xFF; // Length high byte
                    response[3] = response_len & 0xFF;         // Length low byte
                    response[4] = function;
                    response[5] = operation;
                    response[6] = 0x00; // Status: OK
                    response[7] = instax_calculate_checksum(response, 7);

                    send_notification(response, response_len);
                    break;
                }

                case INSTAX_OP_PRINT_END: {
                    ESP_LOGI(TAG, "Print end: received %lu/%lu bytes",
                            (unsigned long)s_print_bytes_received,
                            (unsigned long)s_print_image_size);

                    // Send ACK with proper packet structure
                    response[0] = INSTAX_HEADER_FROM_DEVICE_0;
                    response[1] = INSTAX_HEADER_FROM_DEVICE_1;
                    response_len = 8; // Header(2) + Length(2) + Func(1) + Op(1) + Status(1) + Checksum(1)
                    response[2] = (response_len >> 8) & 0xFF; // Length high byte
                    response[3] = response_len & 0xFF;         // Length low byte
                    response[4] = function;
                    response[5] = operation;
                    response[6] = 0x00; // Status: OK
                    response[7] = instax_calculate_checksum(response, 7);

                    send_notification(response, response_len);
                    break;
                }

                case INSTAX_OP_PRINT_EXECUTE: {
                    ESP_LOGI(TAG, "Print execute");

                    // Check printer error conditions (double-check before executing)
                    const instax_printer_info_t *printer_info = printer_emulator_get_info();
                    uint8_t error_code = 0;
                    const char *error_msg = NULL;

                    // Error 178 (0xB2): No film
                    if (printer_info->photos_remaining == 0) {
                        error_code = 0xB2;
                        error_msg = "No film";
                    }
                    // Error 179 (0xB3): Cover open
                    else if (printer_info->cover_open) {
                        error_code = 0xB3;
                        error_msg = "Cover open";
                    }
                    // Error 180 (0xB4): Battery low (< 20%)
                    else if (printer_info->battery_percentage < 20) {
                        error_code = 0xB4;
                        error_msg = "Battery low";
                    }
                    // Error 181 (0xB5): Printer busy
                    else if (printer_info->printer_busy) {
                        error_code = 0xB5;
                        error_msg = "Printer busy";
                    }

                    // Send error response if any error detected
                    if (error_code != 0) {
                        ESP_LOGW(TAG, "Print execute rejected: %s (error %d = 0x%02X)", error_msg, error_code, error_code);

                        response[0] = INSTAX_HEADER_FROM_DEVICE_0;
                        response[1] = INSTAX_HEADER_FROM_DEVICE_1;
                        response_len = 8; // Header(2) + Length(2) + Func(1) + Op(1) + Status(1) + Checksum(1)
                        response[2] = (response_len >> 8) & 0xFF; // Length high byte
                        response[3] = response_len & 0xFF;         // Length low byte
                        response[4] = function;
                        response[5] = operation;
                        response[6] = error_code;
                        response[7] = instax_calculate_checksum(response, 7);

                        send_notification(response, response_len);

                        // Reset print state
                        s_print_image_size = 0;
                        s_print_bytes_received = 0;
                        s_print_chunk_index = 0;
                        break;
                    }

                    if (s_print_complete_callback) {
                        s_print_complete_callback();
                    }

                    // Send ACK with proper packet structure
                    response[0] = INSTAX_HEADER_FROM_DEVICE_0;
                    response[1] = INSTAX_HEADER_FROM_DEVICE_1;
                    response_len = 8; // Header(2) + Length(2) + Func(1) + Op(1) + Status(1) + Checksum(1)
                    response[2] = (response_len >> 8) & 0xFF; // Length high byte
                    response[3] = response_len & 0xFF;         // Length low byte
                    response[4] = function;
                    response[5] = operation;
                    response[6] = 0x00; // Status: OK (print accepted)
                    response[7] = instax_calculate_checksum(response, 7);

                    send_notification(response, response_len);

                    // Reset print state
                    s_print_image_size = 0;
                    s_print_bytes_received = 0;
                    s_print_chunk_index = 0;
                    break;
                }

                default:
                    ESP_LOGW(TAG, "Unknown print operation: 0x%02x", operation);
                    break;
            }
            break;
        }

        case INSTAX_FUNC_LED: {
            // LED & Sensor operations
            switch (operation) {
                case INSTAX_OP_XYZ_AXIS_INFO: {
                    ESP_LOGI(TAG, "XYZ Axis Info request");

                    // Get current accelerometer values from printer emulator
                    const instax_printer_info_t *printer_info = printer_emulator_get_info();

                    // Build response packet with accelerometer data
                    // Payload: [x_low] [x_high] [y_low] [y_high] [z_low] [z_high] [orientation]
                    response[0] = INSTAX_HEADER_FROM_DEVICE_0;
                    response[1] = INSTAX_HEADER_FROM_DEVICE_1;
                    response_len = 14; // Header(2) + Length(2) + Func(1) + Op(1) + Payload(7) + Checksum(1)
                    response[2] = (response_len >> 8) & 0xFF; // Length high byte
                    response[3] = response_len & 0xFF;         // Length low byte
                    response[4] = function;
                    response[5] = operation;

                    // Pack accelerometer data as little-endian int16
                    response[6] = printer_info->accelerometer.x & 0xFF;        // X low byte
                    response[7] = (printer_info->accelerometer.x >> 8) & 0xFF; // X high byte
                    response[8] = printer_info->accelerometer.y & 0xFF;        // Y low byte
                    response[9] = (printer_info->accelerometer.y >> 8) & 0xFF; // Y high byte
                    response[10] = printer_info->accelerometer.z & 0xFF;       // Z low byte
                    response[11] = (printer_info->accelerometer.z >> 8) & 0xFF; // Z high byte
                    response[12] = printer_info->accelerometer.orientation;    // Orientation state

                    response[13] = instax_calculate_checksum(response, 13);

                    ESP_LOGI(TAG, "Accelerometer data: x=%d, y=%d, z=%d, o=%d",
                            printer_info->accelerometer.x,
                            printer_info->accelerometer.y,
                            printer_info->accelerometer.z,
                            printer_info->accelerometer.orientation);

                    send_notification(response, response_len);
                    break;
                }

                case INSTAX_OP_COLOR_CORRECTION: {
                    // Color correction table upload (function 0x30, operation 0x01)
                    // Newly discovered Dec 2024 - includes print mode in first byte
                    // Payload: [mode] [color_correction_table...]
                    // mode: 0x00 = Rich (311 bytes total), 0x03 = Natural (251 bytes total)

                    if (payload_len >= 1) {
                        uint8_t print_mode = payload[0];
                        printer_emulator_set_print_mode(print_mode);

                        const char *mode_str = (print_mode == 0x00) ? "Rich" :
                                              (print_mode == 0x03) ? "Natural" : "Unknown";
                        ESP_LOGI(TAG, "Color correction table: mode=0x%02x (%s), table_size=%d bytes",
                                print_mode, mode_str, payload_len - 1);
                    } else {
                        ESP_LOGW(TAG, "Color correction command with no payload");
                    }

                    // Send ACK
                    response[0] = INSTAX_HEADER_FROM_DEVICE_0;
                    response[1] = INSTAX_HEADER_FROM_DEVICE_1;
                    response_len = 8;
                    response[2] = (response_len >> 8) & 0xFF;
                    response[3] = response_len & 0xFF;
                    response[4] = function;
                    response[5] = operation;
                    response[6] = 0x00; // Status: OK
                    response[7] = instax_calculate_checksum(response, 7);
                    send_notification(response, response_len);
                    break;
                }

                case INSTAX_OP_ADDITIONAL_INFO: {
                    // Additional sensor/device info query
                    // Packet capture shows payload byte indicates query type:
                    //   payload=0x00 → 17-byte response with sensor data
                    //   payload=0x01 → 21-byte response with different sensor data

                    uint8_t query_type = (payload_len > 0) ? payload[0] : 0x00;
                    ESP_LOGI(TAG, "Additional info request: query_type=0x%02x", query_type);

                    response[0] = INSTAX_HEADER_FROM_DEVICE_0;
                    response[1] = INSTAX_HEADER_FROM_DEVICE_1;
                    response[4] = function;
                    response[5] = operation;

                    if (query_type == 0x00) {
                        // 17-byte response with sensor data
                        // Real device: 61 42 00 11 30 10 00 00 c3 80 00 be 00 00 00 00 0a
                        response_len = 17;
                        response[2] = 0x00; // Length high byte
                        response[3] = 0x11; // Length low byte (17)
                        response[6] = 0x00; // Payload header 1
                        response[7] = 0x00; // Payload header 2 (matches query type)
                        response[8] = 0xc3; // Sensor data from real device
                        response[9] = 0x80;
                        response[10] = 0x00;
                        response[11] = 0xbe;
                        response[12] = 0x00;
                        response[13] = 0x00;
                        response[14] = 0x00;
                        response[15] = 0x00;
                        response[16] = instax_calculate_checksum(response, 16);

                        ESP_LOGI(TAG, "📤 Sent additional info type 0 (17 bytes)");

                    } else if (query_type == 0x01) {
                        // 21-byte response with different sensor data
                        // Real device: 61 42 00 15 30 10 00 01 00 00 00 02 ff 00 01 02 00 00 00 00 02
                        response_len = 21;
                        response[2] = 0x00; // Length high byte
                        response[3] = 0x15; // Length low byte (21)
                        response[6] = 0x00; // Payload header 1
                        response[7] = 0x01; // Payload header 2 (matches query type)
                        response[8] = 0x00; // Sensor/info data from real device
                        response[9] = 0x00;
                        response[10] = 0x00;
                        response[11] = 0x02;
                        response[12] = 0xff;
                        response[13] = 0x00;
                        response[14] = 0x01;
                        response[15] = 0x02;
                        response[16] = 0x00;
                        response[17] = 0x00;
                        response[18] = 0x00;
                        response[19] = 0x00;
                        response[20] = instax_calculate_checksum(response, 20);

                        ESP_LOGI(TAG, "📤 Sent additional info type 1 (21 bytes)");

                    } else {
                        // Unknown query type - send minimal response
                        ESP_LOGW(TAG, "Unknown additional info query type: 0x%02x", query_type);
                        response_len = 8;
                        response[2] = 0x00;
                        response[3] = 0x08;
                        response[6] = 0x00; // Status OK
                        response[7] = instax_calculate_checksum(response, 7);
                    }

                    send_notification(response, response_len);
                    break;
                }

                default: {
                    // For other LED/sensor operations, send simple ACK
                    ESP_LOGI(TAG, "LED/Sensor control: operation=0x%02x", operation);

                    response[0] = INSTAX_HEADER_FROM_DEVICE_0;
                    response[1] = INSTAX_HEADER_FROM_DEVICE_1;
                    response_len = 8; // Header(2) + Length(2) + Func(1) + Op(1) + Status(1) + Checksum(1)
                    response[2] = (response_len >> 8) & 0xFF; // Length high byte
                    response[3] = response_len & 0xFF;         // Length low byte
                    response[4] = function;
                    response[5] = operation;
                    response[6] = 0x00; // Status: OK
                    response[7] = instax_calculate_checksum(response, 7);

                    send_notification(response, response_len);
                    break;
                }
            }
            break;
        }

        default:
            ESP_LOGW(TAG, "Unknown function code: 0x%02x", function);
            break;
    }
}

/**
 * GATT characteristic access callback
 */
static int gatt_svr_chr_access(uint16_t conn_handle, uint16_t attr_handle,
                               struct ble_gatt_access_ctxt *ctxt, void *arg) {
    const ble_uuid_t *uuid = ctxt->chr->uuid;

    if (ble_uuid_cmp(uuid, &instax_write_char_uuid.u) == 0) {
        // Write characteristic
        if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
            uint16_t chunk_len = OS_MBUF_PKTLEN(ctxt->om);
            ESP_LOGD(TAG, "Write characteristic: %d bytes (buffer has %d/%d)",
                     chunk_len, s_packet_buffer_len, s_expected_packet_len);

            // Copy chunk to temporary buffer
            uint8_t chunk[512];
            if (chunk_len > sizeof(chunk)) {
                ESP_LOGW(TAG, "Chunk too large: %d bytes", chunk_len);
                return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
            }

            int rc = ble_hs_mbuf_to_flat(ctxt->om, chunk, chunk_len, NULL);
            if (rc != 0) {
                ESP_LOGE(TAG, "Failed to copy mbuf: %d", rc);
                return BLE_ATT_ERR_UNLIKELY;
            }

            // Check if this is the start of a new packet (has Instax header)
            if (chunk_len >= 4 && chunk[0] == INSTAX_HEADER_TO_DEVICE_0 && chunk[1] == INSTAX_HEADER_TO_DEVICE_1) {
                // New packet starting - extract expected length
                s_expected_packet_len = ((uint16_t)chunk[2] << 8) | chunk[3];
                s_packet_buffer_len = 0;
                ESP_LOGI(TAG, "📥 Received command packet (%d bytes)", chunk_len);
                // Log first 16 bytes of packet for debugging official app compatibility
                ESP_LOGI(TAG, "   First bytes: %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X",
                        chunk_len > 0 ? chunk[0] : 0, chunk_len > 1 ? chunk[1] : 0,
                        chunk_len > 2 ? chunk[2] : 0, chunk_len > 3 ? chunk[3] : 0,
                        chunk_len > 4 ? chunk[4] : 0, chunk_len > 5 ? chunk[5] : 0,
                        chunk_len > 6 ? chunk[6] : 0, chunk_len > 7 ? chunk[7] : 0,
                        chunk_len > 8 ? chunk[8] : 0, chunk_len > 9 ? chunk[9] : 0,
                        chunk_len > 10 ? chunk[10] : 0, chunk_len > 11 ? chunk[11] : 0,
                        chunk_len > 12 ? chunk[12] : 0, chunk_len > 13 ? chunk[13] : 0,
                        chunk_len > 14 ? chunk[14] : 0, chunk_len > 15 ? chunk[15] : 0);
                ESP_LOGD(TAG, "New packet starting, expecting %d bytes total", s_expected_packet_len);
            }

            // Append chunk to packet buffer
            if (s_packet_buffer_len + chunk_len > PACKET_BUFFER_SIZE) {
                ESP_LOGE(TAG, "Packet buffer overflow! Resetting.");
                s_packet_buffer_len = 0;
                s_expected_packet_len = 0;
                return BLE_ATT_ERR_UNLIKELY;
            }

            memcpy(&s_packet_buffer[s_packet_buffer_len], chunk, chunk_len);
            s_packet_buffer_len += chunk_len;

            // Check if we have a complete packet
            if (s_expected_packet_len > 0 && s_packet_buffer_len >= s_expected_packet_len) {
                ESP_LOGI(TAG, "✅ Complete packet received: %d bytes - processing", s_packet_buffer_len);
                // Process the complete packet
                handle_instax_packet(s_packet_buffer, s_packet_buffer_len);
                // Reset for next packet
                s_packet_buffer_len = 0;
                s_expected_packet_len = 0;
            }

            return 0;
        }
        return BLE_ATT_ERR_UNLIKELY;
    }

    if (ble_uuid_cmp(uuid, &instax_notify_char_uuid.u) == 0) {
        // Notify characteristic
        if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
            ESP_LOGD(TAG, "Read notify characteristic");
            // Return empty data for read
            uint8_t empty_val = 0;
            int rc = os_mbuf_append(ctxt->om, &empty_val, sizeof(empty_val));
            return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
        }
        return 0; // Allow subscribe/unsubscribe operations
    }

    return BLE_ATT_ERR_UNLIKELY;
}

/**
 * GAP event handler
 */
static int gap_event_handler(struct ble_gap_event *event, void *arg) {
    switch (event->type) {
        case BLE_GAP_EVENT_CONNECT:
            ESP_LOGI(TAG, "Connection %s; status=%d",
                     event->connect.status == 0 ? "established" : "failed",
                     event->connect.status);

            if (event->connect.status == 0) {
                // Connection successful - advertising stops automatically
                s_advertising = false;  // Clear flag since BLE stack stopped advertising
                s_connected = true;
                s_conn_handle = event->connect.conn_handle;
            } else {
                // Connection failed, resume advertising
                s_advertising = false;  // Clear flag before restarting
                ble_peripheral_start_advertising(NULL);
            }
            break;

        case BLE_GAP_EVENT_DISCONNECT:
            ESP_LOGI(TAG, "Disconnect; reason=%d", event->disconnect.reason);
            s_connected = false;
            s_conn_handle = BLE_HS_CONN_HANDLE_NONE;

            // Clear advertising flag and resume advertising
            s_advertising = false;  // CRITICAL: Clear flag before restarting
            ble_peripheral_start_advertising(NULL);
            break;

        case BLE_GAP_EVENT_ADV_COMPLETE:
            ESP_LOGI(TAG, "Advertising complete; reason=%d", event->adv_complete.reason);
            s_advertising = false;
            break;

        case BLE_GAP_EVENT_SUBSCRIBE:
            ESP_LOGI(TAG, "Subscribe event; conn_handle=%d attr_handle=%d",
                     event->subscribe.conn_handle,
                     event->subscribe.attr_handle);
            break;

        default:
            break;
    }

    return 0;
}

/**
 * Start advertising
 */
esp_err_t ble_peripheral_start_advertising(const char *device_name) {
    if (s_advertising) {
        ESP_LOGW(TAG, "Already advertising");
        return ESP_OK;
    }

    // Use printer emulator name if not provided
    if (device_name == NULL) {
        const instax_printer_info_t *info = printer_emulator_get_info();
        device_name = info->device_name;
    }

    // Set device name
    int rc = ble_svc_gap_device_name_set(device_name);
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to set device name: %d", rc);
        return ESP_FAIL;
    }

    // Set advertising data (main packet)
    // CRITICAL: iOS Core Bluetooth filtering requires service UUID in MAIN packet, not scan response!
    // Include Fujifilm manufacturer data for additional app filtering
    static uint8_t mfg_data[] = {
        0xD8, 0x04,  // Company ID: 0x04D8 (Fujifilm - captured from real Square Link)
        0x05, 0x00   // Additional data from real Square Link
    };

    struct ble_hs_adv_fields fields = {0};
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    // Put service UUID in MAIN advertising packet for iOS filtering
    fields.uuids128 = (ble_uuid128_t[]) { instax_service_uuid };
    fields.num_uuids128 = 1;
    fields.uuids128_is_complete = 1;
    fields.mfg_data = mfg_data;
    fields.mfg_data_len = sizeof(mfg_data);
    // Add TX Power (real Square Link advertises 3 dBm)
    fields.tx_pwr_lvl = 3;
    fields.tx_pwr_lvl_is_present = 1;

    ESP_LOGI(TAG, "Setting advertising data: service UUID + mfg_data (len=%d)", sizeof(mfg_data));
    rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to set advertising data: %d (BLE_HS_EMSGSIZE=%d)", rc, BLE_HS_EMSGSIZE);
        // Try without manufacturer data, keep service UUID (most critical)
        ESP_LOGW(TAG, "Retrying without manufacturer data in main advertising packet...");
        fields.mfg_data = NULL;
        fields.mfg_data_len = 0;
        // Keep service UUID - it's CRITICAL for iOS filtering
        rc = ble_gap_adv_set_fields(&fields);
        if (rc != 0) {
            ESP_LOGE(TAG, "Failed to set advertising data (2nd try): %d", rc);
            return ESP_FAIL;
        }
    }

    // Set scan response data (device name)
    struct ble_hs_adv_fields rsp_fields = {0};
    rsp_fields.name = (uint8_t *)device_name;
    rsp_fields.name_len = strlen(device_name);
    rsp_fields.name_is_complete = 1;

    ESP_LOGI(TAG, "Setting scan response data: name='%s' (len=%d)", device_name, strlen(device_name));
    rc = ble_gap_adv_rsp_set_fields(&rsp_fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to set scan response data: %d", rc);
        // Not critical if scan response fails - service UUID is in main packet
        ESP_LOGW(TAG, "Scan response failed, continuing without device name in scan response");
    }

    // Start advertising
    struct ble_gap_adv_params adv_params = {0};
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    adv_params.itvl_min = 160;  // 100ms (in 0.625ms units)
    adv_params.itvl_max = 240;  // 150ms (in 0.625ms units)

    rc = ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, NULL, BLE_HS_FOREVER,
                           &adv_params, gap_event_handler, NULL);
    if (rc != 0) {
        // Error 2 (BLE_HS_EALREADY) means advertising is already running - treat as success
        if (rc == 2) {
            ESP_LOGW(TAG, "Advertising already running (BLE_HS_EALREADY) - continuing");
            s_advertising = true;  // Set flag since advertising is active
            return ESP_OK;
        }
        ESP_LOGE(TAG, "Failed to start advertising: %d", rc);
        return ESP_FAIL;
    }

    s_advertising = true;
    ESP_LOGI(TAG, "Started advertising as '%s'", device_name);
    ESP_LOGI(TAG, "Advertising with service UUID: 70954782-2d83-473d-9e5f-81e1d02d5273");
    ESP_LOGI(TAG, "Using BLE random address, connectable, general discoverable");
    return ESP_OK;
}

/**
 * Stop advertising
 */
esp_err_t ble_peripheral_stop_advertising(void) {
    if (!s_advertising) {
        return ESP_OK;
    }

    int rc = ble_gap_adv_stop();
    if (rc != 0 && rc != BLE_HS_EALREADY) {
        ESP_LOGE(TAG, "Failed to stop advertising: %d", rc);
        return ESP_FAIL;
    }

    s_advertising = false;
    ESP_LOGI(TAG, "Stopped advertising");
    return ESP_OK;
}

/**
 * BLE host task
 */
static void ble_host_task(void *param) {
    ESP_LOGI(TAG, "BLE Host Task Started");
    nimble_port_run();
    nimble_port_freertos_deinit();
}

/**
 * On sync callback
 */
static void on_sync(void) {
    ESP_LOGI(TAG, "BLE host synced");

    // Get and log our public BLE address (derived from ESP32 MAC)
    uint8_t addr_val[6];
    int rc = ble_hs_id_copy_addr(BLE_ADDR_PUBLIC, addr_val, NULL);
    if (rc == 0) {
        ESP_LOGI(TAG, "Device public BLE address: %02x:%02x:%02x:%02x:%02x:%02x",
                 addr_val[5], addr_val[4], addr_val[3],
                 addr_val[2], addr_val[1], addr_val[0]);
    }

    // Services were already registered in ble_peripheral_init()
    ESP_LOGI(TAG, "GATT server ready");
    ESP_LOGI(TAG, "Instax service UUID: 70954782-2d83-473d-9e5f-81e1d02d5273");
    ESP_LOGI(TAG, "Write char UUID: 70954783-2d83-473d-9e5f-81e1d02d5273");
    ESP_LOGI(TAG, "Notify char UUID: 70954784-2d83-473d-9e5f-81e1d02d5273");
}

/**
 * On reset callback
 */
static void on_reset(int reason) {
    ESP_LOGW(TAG, "BLE host reset: reason=%d", reason);
}

/**
 * Get model number string for a printer model
 */
static const char* get_model_number_for_printer(instax_model_t model) {
    switch (model) {
        case INSTAX_MODEL_MINI:
            return "FI033";  // Mini Link 3
        case INSTAX_MODEL_SQUARE:
            return "FI017";  // Square Link - MUST match packet capture exactly!
        case INSTAX_MODEL_WIDE:
            return "FI022";  // Wide Link
        default:
            return "FI033";  // Default to Mini Link 3
    }
}

/**
 * Initialize BLE peripheral
 */
esp_err_t ble_peripheral_init(void) {
    ESP_LOGI(TAG, "Initializing BLE peripheral");

    // Initialize NimBLE host
    nimble_port_init();

    // Configure host callbacks
    ble_hs_cfg.sync_cb = on_sync;
    ble_hs_cfg.reset_cb = on_reset;
    ble_hs_cfg.gatts_register_cb = NULL;

    // Initialize GATT, GAP services
    ble_svc_gap_init();
    ble_svc_gatt_init();

    // Register custom GATT services BEFORE starting the stack
    // This must happen here, not in on_sync() callback
    ESP_LOGI(TAG, "Counting GATT services...");
    int rc = ble_gatts_count_cfg(gatt_svr_svcs);
    if (rc != 0) {
        ESP_LOGE(TAG, "GATT count cfg failed: %d", rc);
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "GATT count cfg successful");

    ESP_LOGI(TAG, "Adding GATT services...");
    rc = ble_gatts_add_svcs(gatt_svr_svcs);
    if (rc != 0) {
        ESP_LOGE(TAG, "GATT add services failed: %d", rc);
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "GATT services registered successfully");

    // Initialize Device Information Service
    // Get current printer model from printer emulator
    const instax_printer_info_t *printer_info = printer_emulator_get_info();
    const char *model_number = get_model_number_for_printer(printer_info->model);
    ble_svc_dis_model_number_set(model_number);  // Set model number based on current printer model
    ble_svc_dis_serial_number_set("70423278");  // Match BLE address suffix
    ble_svc_dis_firmware_revision_set("0101");  // Firmware version - MUST match real device exactly (no dots!)
    ble_svc_dis_hardware_revision_set("0001");  // Hardware version
    ble_svc_dis_software_revision_set("0002");  // Software version - MUST match real device exactly
    ble_svc_dis_manufacturer_name_set("FUJIFILM");  // MUST match real device exactly (no "Corporation"!)
    ble_svc_dis_init();
    ESP_LOGI(TAG, "Device Information Service initialized with model: %s (fw: 01.01, sw: 01.01)", model_number);

    // Start the host task
    nimble_port_freertos_init(ble_host_task);

    ESP_LOGI(TAG, "BLE peripheral initialized");
    return ESP_OK;
}

bool ble_peripheral_is_advertising(void) {
    return s_advertising;
}

bool ble_peripheral_is_connected(void) {
    return s_connected;
}

void ble_peripheral_register_print_start_callback(ble_peripheral_print_start_callback_t callback) {
    s_print_start_callback = callback;
}

void ble_peripheral_register_print_data_callback(ble_peripheral_print_data_callback_t callback) {
    s_print_data_callback = callback;
}

void ble_peripheral_register_print_complete_callback(ble_peripheral_print_complete_callback_t callback) {
    s_print_complete_callback = callback;
}

void ble_peripheral_update_model_number(instax_model_t model) {
    const char *model_number = get_model_number_for_printer(model);
    ble_svc_dis_model_number_set(model_number);
    ESP_LOGI(TAG, "Updated Device Information Service model number to: %s", model_number);
}

void ble_peripheral_update_device_name_with_ip(const char *ip_address) {
    if (ip_address == NULL) {
        return;
    }

    // Extract last two octets for shortened name
    // Example: "192.168.1.101" -> "1.101"
    const char *third_octet = NULL;
    const char *fourth_octet = NULL;
    int dots = 0;

    for (const char *p = ip_address; *p != '\0'; p++) {
        if (*p == '.') {
            dots++;
            if (dots == 2) {
                third_octet = p + 1;
            } else if (dots == 3) {
                fourth_octet = p + 1;
            }
        }
    }

    if (third_octet && fourth_octet) {
        // Extract the last two octets
        char short_ip[16];
        int third_num = 0, fourth_num = 0;
        sscanf(third_octet, "%d", &third_num);
        sscanf(fourth_octet, "%d", &fourth_num);
        snprintf(short_ip, sizeof(short_ip), "%d.%d", third_num, fourth_num);

        ESP_LOGI(TAG, "WiFi connected with IP: %s (shortened: %s)", ip_address, short_ip);
        ESP_LOGI(TAG, "Note: BLE device name will be 'Instax-Simulator (%s)' when advertising restarts", short_ip);

        // Note: The actual device name update happens in ble_peripheral_start_advertising()
        // when the user restarts advertising from the web interface
    }
}
