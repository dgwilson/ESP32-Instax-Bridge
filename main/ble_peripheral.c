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
#include "esp_system.h"
#include "esp_mac.h"
#include "nvs_flash.h"
#include "host/ble_hs.h"
#include "host/ble_gap.h"
#include "host/ble_gatt.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "services/dis/ble_svc_dis.h"
#include "host/ble_sm.h"

// Bond storage function - defined in NimBLE store/config
extern void ble_store_config_init(void);

static const char *TAG = "ble_peripheral";

// ============================================================================
// MAC Address Configuration
// ============================================================================
// Set to 1 to use a custom MAC address, 0 to use factory default
#define USE_CUSTOM_MAC 1

// Custom MAC address using Fujifilm's registered OUI: 1C:7D:22
// Format: {OUI_0, OUI_1, OUI_2, DEVICE_0, DEVICE_1, DEVICE_2}
// Fujifilm OUI: 1C:7D:22 (first 3 bytes)
// Device ID: 55:55:00 (last 3 bytes - matches simulated serial INSTAX-55550000)
// LEGAL WARNING: Only use Fujifilm's registered OUI for personal testing/development
static uint8_t custom_mac[6] = {0x1C, 0x7D, 0x22, 0x55, 0x55, 0x00};
// ============================================================================

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

// ACK delay for data packets (milliseconds)
// Slows down sender to prevent buffer overflow during print data transfer
// Square printer: 50ms delay reduces packet rate from ~300ms to ~350ms between packets
#define DATA_PACKET_ACK_DELAY_MS 50

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

// =====================================================
// Link 3-Specific Services (required for Mini Link 3 app compatibility)
// =====================================================

// Link 3 Info Service UUID: 0000D0FF-3C17-D293-8E48-14FE2E4DA212
static const ble_uuid128_t link3_info_service_uuid = BLE_UUID128_INIT(
    0x12, 0xa2, 0x4d, 0x2e, 0xfe, 0x14, 0x48, 0x8e,
    0x93, 0xd2, 0x17, 0x3c, 0xff, 0xd0, 0x00, 0x00
);

// Link 3 Info Service Characteristics - MUST use the SAME custom base UUID as the service!
// Base UUID: 0000XXXX-3C17-D293-8E48-14FE2E4DA212
// NOT the standard Bluetooth base UUID (0000XXXX-0000-1000-8000-00805F9B34FB)
static const ble_uuid128_t link3_ffd1_uuid = BLE_UUID128_INIT(
    0x12, 0xa2, 0x4d, 0x2e, 0xfe, 0x14, 0x48, 0x8e,
    0x93, 0xd2, 0x17, 0x3c, 0xd1, 0xff, 0x00, 0x00  // 0000FFD1-3C17-D293-8E48-14FE2E4DA212
);
static const ble_uuid128_t link3_ffd2_uuid = BLE_UUID128_INIT(
    0x12, 0xa2, 0x4d, 0x2e, 0xfe, 0x14, 0x48, 0x8e,
    0x93, 0xd2, 0x17, 0x3c, 0xd2, 0xff, 0x00, 0x00  // 0000FFD2-3C17-D293-8E48-14FE2E4DA212
);
static const ble_uuid128_t link3_ffd3_uuid = BLE_UUID128_INIT(
    0x12, 0xa2, 0x4d, 0x2e, 0xfe, 0x14, 0x48, 0x8e,
    0x93, 0xd2, 0x17, 0x3c, 0xd3, 0xff, 0x00, 0x00  // 0000FFD3-3C17-D293-8E48-14FE2E4DA212
);
static const ble_uuid128_t link3_ffd4_uuid = BLE_UUID128_INIT(
    0x12, 0xa2, 0x4d, 0x2e, 0xfe, 0x14, 0x48, 0x8e,
    0x93, 0xd2, 0x17, 0x3c, 0xd4, 0xff, 0x00, 0x00  // 0000FFD4-3C17-D293-8E48-14FE2E4DA212
);
static const ble_uuid128_t link3_fff1_uuid = BLE_UUID128_INIT(
    0x12, 0xa2, 0x4d, 0x2e, 0xfe, 0x14, 0x48, 0x8e,
    0x93, 0xd2, 0x17, 0x3c, 0xf1, 0xff, 0x00, 0x00  // 0000FFF1-3C17-D293-8E48-14FE2E4DA212
);
static const ble_uuid128_t link3_ffe0_uuid = BLE_UUID128_INIT(
    0x12, 0xa2, 0x4d, 0x2e, 0xfe, 0x14, 0x48, 0x8e,
    0x93, 0xd2, 0x17, 0x3c, 0xe0, 0xff, 0x00, 0x00  // 0000FFE0-3C17-D293-8E48-14FE2E4DA212
);
static const ble_uuid128_t link3_ffe1_uuid = BLE_UUID128_INIT(
    0x12, 0xa2, 0x4d, 0x2e, 0xfe, 0x14, 0x48, 0x8e,
    0x93, 0xd2, 0x17, 0x3c, 0xe1, 0xff, 0x00, 0x00  // 0000FFE1-3C17-D293-8E48-14FE2E4DA212
);
static const ble_uuid128_t link3_fff3_uuid = BLE_UUID128_INIT(
    0x12, 0xa2, 0x4d, 0x2e, 0xfe, 0x14, 0x48, 0x8e,
    0x93, 0xd2, 0x17, 0x3c, 0xf3, 0xff, 0x00, 0x00  // 0000FFF3-3C17-D293-8E48-14FE2E4DA212
);
static const ble_uuid128_t link3_fff4_uuid = BLE_UUID128_INIT(
    0x12, 0xa2, 0x4d, 0x2e, 0xfe, 0x14, 0x48, 0x8e,
    0x93, 0xd2, 0x17, 0x3c, 0xf4, 0xff, 0x00, 0x00  // 0000FFF4-3C17-D293-8E48-14FE2E4DA212
);
static const ble_uuid128_t link3_fff5_uuid = BLE_UUID128_INIT(
    0x12, 0xa2, 0x4d, 0x2e, 0xfe, 0x14, 0x48, 0x8e,
    0x93, 0xd2, 0x17, 0x3c, 0xf5, 0xff, 0x00, 0x00  // 0000FFF5-3C17-D293-8E48-14FE2E4DA212
);

// Link 3 Status Service UUID: 00006287-3C17-D293-8E48-14FE2E4DA212
static const ble_uuid128_t link3_status_service_uuid = BLE_UUID128_INIT(
    0x12, 0xa2, 0x4d, 0x2e, 0xfe, 0x14, 0x48, 0x8e,
    0x93, 0xd2, 0x17, 0x3c, 0x87, 0x62, 0x00, 0x00
);

// Link 3 Status Service Characteristics
// Control: 00006387-3C17-D293-8E48-14FE2E4DA212
static const ble_uuid128_t link3_control_char_uuid = BLE_UUID128_INIT(
    0x12, 0xa2, 0x4d, 0x2e, 0xfe, 0x14, 0x48, 0x8e,
    0x93, 0xd2, 0x17, 0x3c, 0x87, 0x63, 0x00, 0x00
);
// Status: 00006487-3C17-D293-8E48-14FE2E4DA212
static const ble_uuid128_t link3_status_char_uuid = BLE_UUID128_INIT(
    0x12, 0xa2, 0x4d, 0x2e, 0xfe, 0x14, 0x48, 0x8e,
    0x93, 0xd2, 0x17, 0x3c, 0x87, 0x64, 0x00, 0x00
);

// Handle for Link 3 status notify characteristic
static uint16_t s_link3_status_notify_handle = 0;

// =====================================================
// Wide-Specific Service (required for Wide Link app compatibility)
// =====================================================

// Wide Service UUID: 0000E0FF-3C17-D293-8E48-14FE2E4DA212
static const ble_uuid128_t wide_service_uuid = BLE_UUID128_INIT(
    0x12, 0xa2, 0x4d, 0x2e, 0xfe, 0x14, 0x48, 0x8e,
    0x93, 0xd2, 0x17, 0x3c, 0xff, 0xe0, 0x00, 0x00
);

// Wide Service Characteristics - using same custom base UUID
// FFE1: 0000FFE1-3C17-D293-8E48-14FE2E4DA212
static const ble_uuid128_t wide_ffe1_uuid = BLE_UUID128_INIT(
    0x12, 0xa2, 0x4d, 0x2e, 0xfe, 0x14, 0x48, 0x8e,
    0x93, 0xd2, 0x17, 0x3c, 0xe1, 0xff, 0x00, 0x00
);
// FFE9: 0000FFE9-3C17-D293-8E48-14FE2E4DA212
static const ble_uuid128_t wide_ffe9_uuid = BLE_UUID128_INIT(
    0x12, 0xa2, 0x4d, 0x2e, 0xfe, 0x14, 0x48, 0x8e,
    0x93, 0xd2, 0x17, 0x3c, 0xe9, 0xff, 0x00, 0x00
);
// FFEA: 0000FFEA-3C17-D293-8E48-14FE2E4DA212
static const ble_uuid128_t wide_ffea_uuid = BLE_UUID128_INIT(
    0x12, 0xa2, 0x4d, 0x2e, 0xfe, 0x14, 0x48, 0x8e,
    0x93, 0xd2, 0x17, 0x3c, 0xea, 0xff, 0x00, 0x00
);

// Handles for Wide notify characteristics
static uint16_t s_wide_ffe1_notify_handle = 0;
static uint16_t s_wide_ffea_notify_handle = 0;

// Forward declarations
static int gatt_svr_chr_access(uint16_t conn_handle, uint16_t attr_handle,
                               struct ble_gatt_access_ctxt *ctxt, void *arg);
static int link3_info_chr_access(uint16_t conn_handle, uint16_t attr_handle,
                                 struct ble_gatt_access_ctxt *ctxt, void *arg);
static int link3_status_chr_access(uint16_t conn_handle, uint16_t attr_handle,
                                   struct ble_gatt_access_ctxt *ctxt, void *arg);
static int wide_chr_access(uint16_t conn_handle, uint16_t attr_handle,
                           struct ble_gatt_access_ctxt *ctxt, void *arg);
static const char* get_model_number_for_printer(instax_model_t model);
static esp_err_t send_wide_ffe1_notification(void);
static esp_err_t send_wide_ffea_notification(void);

// =====================================================
// GATT Service Definitions - Model-Specific
// =====================================================

// Square Link: Main Instax service only (no Link 3, no Wide)
static const struct ble_gatt_svc_def gatt_svr_svcs_square[] = {
    {
        // Instax Service
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &instax_service_uuid.u,
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                .uuid = &instax_write_char_uuid.u,
                .access_cb = gatt_svr_chr_access,
                .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP,
            },
            {
                .uuid = &instax_notify_char_uuid.u,
                .access_cb = gatt_svr_chr_access,
                .val_handle = &s_notify_handle,
                .flags = BLE_GATT_CHR_F_NOTIFY | BLE_GATT_CHR_F_READ,
            },
            {0}
        },
    },
    {0}
};

// Mini Link 1: Main Instax service + D0FF + 6287 services (older model)
// Preserved for future use - app detects this as Link 1
__attribute__((unused)) static const struct ble_gatt_svc_def gatt_svr_svcs_mini_link1[] = {
    {
        // Instax Service
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &instax_service_uuid.u,
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                .uuid = &instax_write_char_uuid.u,
                .access_cb = gatt_svr_chr_access,
                .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP,
            },
            {
                .uuid = &instax_notify_char_uuid.u,
                .access_cb = gatt_svr_chr_access,
                .val_handle = &s_notify_handle,
                .flags = BLE_GATT_CHR_F_NOTIFY | BLE_GATT_CHR_F_READ,
            },
            {0}
        },
    },
    // Link 3 Info Service (0000D0FF)
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &link3_info_service_uuid.u,
        .characteristics = (struct ble_gatt_chr_def[]) {
            {.uuid = &link3_ffd1_uuid.u, .access_cb = link3_info_chr_access, .flags = BLE_GATT_CHR_F_READ},
            {.uuid = &link3_ffd2_uuid.u, .access_cb = link3_info_chr_access, .flags = BLE_GATT_CHR_F_READ},
            {.uuid = &link3_ffd3_uuid.u, .access_cb = link3_info_chr_access, .flags = BLE_GATT_CHR_F_READ},
            {.uuid = &link3_ffd4_uuid.u, .access_cb = link3_info_chr_access, .flags = BLE_GATT_CHR_F_READ},
            {.uuid = &link3_fff1_uuid.u, .access_cb = link3_info_chr_access, .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY},
            {.uuid = &link3_ffe0_uuid.u, .access_cb = link3_info_chr_access, .flags = BLE_GATT_CHR_F_READ},
            {.uuid = &link3_ffe1_uuid.u, .access_cb = link3_info_chr_access, .flags = BLE_GATT_CHR_F_READ},
            {.uuid = &link3_fff3_uuid.u, .access_cb = link3_info_chr_access, .flags = BLE_GATT_CHR_F_READ},
            {.uuid = &link3_fff4_uuid.u, .access_cb = link3_info_chr_access, .flags = BLE_GATT_CHR_F_READ},
            {.uuid = &link3_fff5_uuid.u, .access_cb = link3_info_chr_access, .flags = BLE_GATT_CHR_F_READ},
            {0}
        },
    },
    // Link 3 Status Service (00006287)
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &link3_status_service_uuid.u,
        .characteristics = (struct ble_gatt_chr_def[]) {
            {.uuid = &link3_control_char_uuid.u, .access_cb = link3_status_chr_access, .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE},
            {.uuid = &link3_status_char_uuid.u, .access_cb = link3_status_chr_access, .val_handle = &s_link3_status_notify_handle, .flags = BLE_GATT_CHR_F_NOTIFY},
            {0}
        },
    },
    {0}
};

// Mini Link 3: Main Instax service only (newer model, simpler GATT)
// Testing showed this breaks detection - D0FF/6287 services are required
__attribute__((unused)) static const struct ble_gatt_svc_def gatt_svr_svcs_mini_link3[] = {
    {
        // Instax Service
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &instax_service_uuid.u,
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                .uuid = &instax_write_char_uuid.u,
                .access_cb = gatt_svr_chr_access,
                .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP,
            },
            {
                .uuid = &instax_notify_char_uuid.u,
                .access_cb = gatt_svr_chr_access,
                .val_handle = &s_notify_handle,
                .flags = BLE_GATT_CHR_F_NOTIFY | BLE_GATT_CHR_F_READ,
            },
            {0}
        },
    },
    {0}
};

// Wide Link: Main Instax service + Wide service
static const struct ble_gatt_svc_def gatt_svr_svcs_wide[] = {
    {
        // Instax Service
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &instax_service_uuid.u,
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                .uuid = &instax_write_char_uuid.u,
                .access_cb = gatt_svr_chr_access,
                .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP,
            },
            {
                .uuid = &instax_notify_char_uuid.u,
                .access_cb = gatt_svr_chr_access,
                .val_handle = &s_notify_handle,
                .flags = BLE_GATT_CHR_F_NOTIFY | BLE_GATT_CHR_F_READ,
            },
            {0}
        },
    },
    // Wide Service (0000E0FF)
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &wide_service_uuid.u,
        .characteristics = (struct ble_gatt_chr_def[]) {
            // FFE1: Write/Notify ONLY - READ flag causes "Printer Busy" errors per protocol doc!
            {.uuid = &wide_ffe1_uuid.u, .access_cb = wide_chr_access, .val_handle = &s_wide_ffe1_notify_handle,
             .flags = BLE_GATT_CHR_F_NOTIFY | BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP},
            // FFE9: Write for commands
            {.uuid = &wide_ffe9_uuid.u, .access_cb = wide_chr_access, .flags = BLE_GATT_CHR_F_WRITE},
            // FFEA: Read/Notify for ready status
            {.uuid = &wide_ffea_uuid.u, .access_cb = wide_chr_access, .val_handle = &s_wide_ffea_notify_handle,
             .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY},
            {0}
        },
    },
    {0}
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

    // Skip verbose logging for data packet ACKs (func=0x10, op=0x01)
    bool is_data_ack = (len >= 6 && data[4] == 0x10 && data[5] == 0x01);

    if (!is_data_ack) {
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
    }
    return ESP_OK;
}

/**
 * Send Wide FFEA characteristic notification
 * FFEA is a Wide-specific status characteristic that must be sent for the official app to recognize printer as ready
 * Data pattern from real Wide printer: 02 09 B9 00 11 01 00 80 84 1E 00
 */
static esp_err_t send_wide_ffea_notification(void) {
    if (!s_connected || s_conn_handle == BLE_HS_CONN_HANDLE_NONE) {
        ESP_LOGW(TAG, "Cannot send Wide FFEA notification: not connected");
        return ESP_ERR_INVALID_STATE;
    }

    if (s_wide_ffea_notify_handle == 0) {
        ESP_LOGW(TAG, "Cannot send Wide FFEA notification: handle not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    // FFEA notification data from real Wide printer packet capture
    // Byte breakdown (preliminary analysis):
    //   [0] = 0x02 - Unknown, possibly message type
    //   [1] = 0x09 - Unknown
    //   [2-3] = 0xB9 0x00 - Unknown (possibly uint16 little-endian = 185)
    //   [4] = 0x11 - Unknown
    //   [5] = 0x01 - Unknown, possibly ready status
    //   [6] = 0x00 - Unknown
    //   [7] = 0x80 - Unknown (bit pattern: 1000 0000)
    //   [8] = 0x84 - Unknown (bit pattern: 1000 0100)
    //   [9] = 0x1E - Unknown (decimal 30)
    //   [10] = 0x00 - Unknown
    uint8_t ffea_data[11] = {
        0x02, 0x09, 0xB9, 0x00, 0x11, 0x01, 0x00, 0x80, 0x84, 0x1E, 0x00
    };

    struct os_mbuf *om = ble_hs_mbuf_from_flat(ffea_data, sizeof(ffea_data));
    if (om == NULL) {
        ESP_LOGE(TAG, "Failed to allocate mbuf for Wide FFEA notification");
        return ESP_ERR_NO_MEM;
    }

    int rc = ble_gatts_notify_custom(s_conn_handle, s_wide_ffea_notify_handle, om);
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to send Wide FFEA notification: %d", rc);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "📤 Sent Wide FFEA notification (11 bytes)");
    ESP_LOGI(TAG, "   Data: %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X",
            ffea_data[0], ffea_data[1], ffea_data[2], ffea_data[3],
            ffea_data[4], ffea_data[5], ffea_data[6], ffea_data[7],
            ffea_data[8], ffea_data[9], ffea_data[10]);

    return ESP_OK;
}

/**
 * Handle Instax protocol packet
 */
static void handle_instax_packet(const uint8_t *data, size_t len) {
    uint8_t function, operation;
    const uint8_t *payload;
    size_t payload_len;

    // Parse command packet (from app to device)
    if (!instax_parse_command(data, len, &function, &operation, &payload, &payload_len)) {
        ESP_LOGE(TAG, "❌ Failed to parse Instax command!");
        // Log first few bytes for debugging
        ESP_LOGE(TAG, "Packet hex: %02x %02x %02x %02x %02x %02x...",
                 len > 0 ? data[0] : 0, len > 1 ? data[1] : 0, len > 2 ? data[2] : 0,
                 len > 3 ? data[3] : 0, len > 4 ? data[4] : 0, len > 5 ? data[5] : 0);
        return;
    }

    // Skip verbose logging for data packets (0x10, 0x01) to improve performance
    bool is_data_packet = (function == 0x10 && operation == 0x01);

    if (!is_data_packet) {
        ESP_LOGI(TAG, "🔍 Parsing packet (%d bytes)...", len);
        ESP_LOGI(TAG, "✅ Parsed: func=0x%02x op=0x%02x payload_len=%d",
                 function, operation, payload_len);
    }

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
                // Device identification payload (9 bytes)
                // Real Wide sends: 00 01 00 01 00 00 00 00 00
                // Real Mini/Square sends: 00 01 00 02 00 00 00 00 00
                // Byte 9 is model-specific: Wide=0x01, Mini/Square=0x02
                response[6] = 0x00;
                response[7] = 0x01;
                response[8] = 0x00;
                response[9] = (info->model == INSTAX_MODEL_WIDE) ? 0x01 : 0x02;
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
                            // Model/Firmware info - read from current printer configuration
                            // Format: [00 01] [length] "FI033" (Mini) or "FI017" (Square) or "FI022" (Wide)
                            ESP_LOGI(TAG, "Sending model/firmware info");
                            const char *model_str = info->model_number; // Use actual configured model
                            uint8_t model_len = strlen(model_str);
                            response[6] = 0x00; response[7] = 0x01; // Payload header (matches query type)
                            response[8] = model_len; // Length of string
                            memcpy(&response[9], model_str, model_len);
                            response_len = 9 + model_len + 1; // Header(2) + Length(2) + Func(1) + Op(1) + PayloadHdr(2) + Len(1) + String + Checksum(1)
                            ESP_LOGI(TAG, "  Model: %s (length: %d)", model_str, model_len);
                            break;
                        }
                        case 0x02: {
                            // Serial number - read from current printer configuration
                            // Format: [00 02] [length] "70555555" (Mini) or "50196563" (Square) or "20555555" (Wide)
                            ESP_LOGI(TAG, "Sending serial number");
                            const char *serial = info->serial_number; // Use actual configured serial
                            uint8_t serial_len = strlen(serial);
                            response[6] = 0x00; response[7] = 0x02; // Payload header (matches query type)
                            response[8] = serial_len; // Length of string
                            memcpy(&response[9], serial, serial_len);
                            response_len = 9 + serial_len + 1; // Header(2) + Length(2) + Func(1) + Op(1) + PayloadHdr(2) + Len(1) + String + Checksum(1)
                            ESP_LOGI(TAG, "  Serial: %s (length: %d)", serial, serial_len);
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
                        case 0x04: {
                            // Firmware revision - new query type discovered Dec 2024
                            // Format: [00 04] [length] "0101" (Mini/Square) or "0100" (Wide)
                            ESP_LOGI(TAG, "Sending firmware revision");
                            const char *fw_str = info->firmware_revision; // Use actual configured firmware
                            uint8_t fw_len = strlen(fw_str);
                            response[6] = 0x00; response[7] = 0x04; // Payload header (matches query type)
                            response[8] = fw_len; // Length of string
                            memcpy(&response[9], fw_str, fw_len);
                            response_len = 9 + fw_len + 1; // Header(2) + Length(2) + Func(1) + Op(1) + PayloadHdr(2) + Len(1) + String + Checksum(1)
                            ESP_LOGI(TAG, "  Firmware: %s (length: %d)", fw_str, fw_len);
                            break;
                        }
                        case 0x05: {
                            // Hardware revision - query type 0x05
                            // Format: [00 05] [length] "0000" (Mini) or "0001" (Square/Wide)
                            ESP_LOGI(TAG, "Sending hardware revision");
                            const char *hw_str = info->hardware_revision;
                            uint8_t hw_len = strlen(hw_str);
                            response[6] = 0x00; response[7] = 0x05;
                            response[8] = hw_len;
                            memcpy(&response[9], hw_str, hw_len);
                            response_len = 9 + hw_len + 1;
                            ESP_LOGI(TAG, "  Hardware: %s (length: %d)", hw_str, hw_len);
                            break;
                        }
                        case 0x06: {
                            // Software revision - query type 0x06
                            // Format: [00 06] [length] "0003" (Mini) or "0002" (Square)
                            ESP_LOGI(TAG, "Sending software revision");
                            const char *sw_str = info->software_revision;
                            uint8_t sw_len = strlen(sw_str);
                            response[6] = 0x00; response[7] = 0x06;
                            response[8] = sw_len;
                            memcpy(&response[9], sw_str, sw_len);
                            response_len = 9 + sw_len + 1;
                            ESP_LOGI(TAG, "  Software: %s (length: %d)", sw_str, sw_len);
                            break;
                        }
                        case 0x07: {
                            // Manufacturer name - query type 0x07
                            // Format: [00 07] [length] "FUJIFILM"
                            ESP_LOGI(TAG, "Sending manufacturer name");
                            const char *mfr_str = info->manufacturer_name;
                            uint8_t mfr_len = strlen(mfr_str);
                            response[6] = 0x00; response[7] = 0x07;
                            response[8] = mfr_len;
                            memcpy(&response[9], mfr_str, mfr_len);
                            response_len = 9 + mfr_len + 1;
                            ESP_LOGI(TAG, "  Manufacturer: %s (length: %d)", mfr_str, mfr_len);
                            break;
                        }
                        case 0x08: {
                            // Device name - query type 0x08 (speculation)
                            // Format: [00 08] [length] "INSTAX-70555555"
                            ESP_LOGI(TAG, "Sending device name");
                            const char *name_str = info->device_name;
                            uint8_t name_len = strlen(name_str);
                            response[6] = 0x00; response[7] = 0x08;
                            response[8] = name_len;
                            memcpy(&response[9], name_str, name_len);
                            response_len = 9 + name_len + 1;
                            ESP_LOGI(TAG, "  Device Name: %s (length: %d)", name_str, name_len);
                            break;
                        }
                        case 0x09: {
                            // Query type 0x09 - Version/capability info (from real printer capture)
                            // Real Mini Link 3 returns: "00010012" (8 bytes)
                            ESP_LOGI(TAG, "Sending version/capability info (query 0x09)");
                            const char *ver_str = "00010012"; // From real printer capture
                            uint8_t ver_len = strlen(ver_str);
                            response[6] = 0x00; response[7] = 0x09;
                            response[8] = ver_len;
                            memcpy(&response[9], ver_str, ver_len);
                            response_len = 9 + ver_len + 1;
                            ESP_LOGI(TAG, "  Version info: %s (length: %d)", ver_str, ver_len);
                            break;
                        }
                        case 0x0a: {
                            // Query type 0x0a - Additional version info (from real printer capture)
                            // Real Mini Link 3 returns: "00000001" (8 bytes)
                            ESP_LOGI(TAG, "Sending additional version info (query 0x0a)");
                            const char *ver2_str = "00000001"; // From real printer capture
                            uint8_t ver2_len = strlen(ver2_str);
                            response[6] = 0x00; response[7] = 0x0a;
                            response[8] = ver2_len;
                            memcpy(&response[9], ver2_str, ver2_len);
                            response_len = 9 + ver2_len + 1;
                            ESP_LOGI(TAG, "  Version info 2: %s (length: %d)", ver2_str, ver2_len);
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
                    // Response format varies by model!
                    ESP_LOGI(TAG, "Image dimensions query (payload=0x00) - model=%d (WIDE=%d), dimensions=%dx%d",
                            info->model, INSTAX_MODEL_WIDE, info->width, info->height);
                    response[0] = INSTAX_HEADER_FROM_DEVICE_0;
                    response[1] = INSTAX_HEADER_FROM_DEVICE_1;
                    // Bytes 2-3 (length) filled below based on model
                    response[4] = function;
                    response[5] = operation;
                    response[6] = 0x00; // Payload header byte 0
                    response[7] = 0x00; // Payload header byte 1
                    // Image dimensions (model-specific)
                    response[8] = (info->width >> 8) & 0xFF;   // Width high byte
                    response[9] = info->width & 0xFF;          // Width low byte
                    response[10] = (info->height >> 8) & 0xFF; // Height high byte
                    response[11] = info->height & 0xFF;        // Height low byte

                    if (info->model == INSTAX_MODEL_WIDE) {
                        // Real Wide: 61 42 00 13 00 02 00 00 04 ec 03 48 02 7b 00 05 28 00 62
                        // Total 19 bytes = Header(2) + Length(2) + Func(1) + Op(1) + Payload(12) + Checksum(1)
                        ESP_LOGI(TAG, "Sending WIDE-specific dimensions response (19 bytes)");
                        response_len = 19;
                        response[2] = 0x00; // Length high byte
                        response[3] = 0x13; // Length low byte (19 decimal)
                        response[12] = 0x02; // Max file size high byte
                        response[13] = 0x7B; // Max file size low byte (0x027B = 635 KB)
                        response[14] = 0x00;
                        response[15] = 0x05; // Wide-specific (not 0x06)
                        response[16] = 0x28; // Wide-specific (not 0x40)
                        response[17] = 0x00;
                        response[18] = instax_calculate_checksum(response, 18);
                    } else {
                        // Square/Mini: 23 byte response
                        // Real Square: [61 42 00 17 00 02 00 00] [03 20 03 20 02 4b 00 06 40 00 01 00 00 00] [69]
                        ESP_LOGI(TAG, "Sending Square/Mini dimensions response (23 bytes)");
                        response_len = 23;
                        response[2] = 0x00; // Length high byte
                        response[3] = 0x17; // Length low byte (23 decimal)
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
                    }
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
                            // Real Mini Link 3 sends: [61 42 00 17 00 02 00 00] [02 58 03 20 02 7b 00 02 58 00 00 00 00 00] [ef]
                            // Real Square sends: [61 42 00 17 00 02 00 00] [03 20 03 20 02 4b 00 06 40 00 01 00 00 00] [69]
                            ESP_LOGI(TAG, "Sending image support: %dx%d, model enum=%d (WIDE=%d, MINI=%d, SQUARE=%d)",
                                    info->width, info->height, info->model, INSTAX_MODEL_WIDE, INSTAX_MODEL_MINI, INSTAX_MODEL_SQUARE);
                            response[6] = 0x00; // Payload header byte 0
                            response[7] = 0x00; // Payload header byte 1 (matches query type)
                            response[8] = (info->width >> 8) & 0xFF;  // Width high byte
                            response[9] = info->width & 0xFF;         // Width low byte
                            response[10] = (info->height >> 8) & 0xFF; // Height high byte
                            response[11] = info->height & 0xFF;        // Height low byte

                            // Extended capabilities - MODEL SPECIFIC (from real printer captures)
                            if (info->model == INSTAX_MODEL_WIDE) {
                                // Wide Link specific values from real printer capture:
                                // Real Wide: 61 42 00 13 00 02 00 00 04 ec 03 48 02 7b 00 05 28 00 62
                                // Total 19 bytes = Header(2) + Length(2) + Func(1) + Op(1) + Payload(12) + Checksum(1)
                                response[12] = 0x02; // Max file size high byte
                                response[13] = 0x7B; // Max file size low byte (0x027B = 635 KB, same as Mini)
                                response[14] = 0x00; // Unknown byte 1
                                response[15] = 0x05; // Unknown byte 2 (Wide-specific: 0x05, not 0x06)
                                response[16] = 0x28; // Unknown byte 3 (Wide-specific: 0x28, not 0x40)
                                response[17] = 0x00; // Unknown byte 4
                                // Wide has 6 capability bytes, not 7 - no byte 18!
                                response_len = 18; // Header(2) + Length(2) + Func(1) + Op(1) + Payload(12) = 18, checksum added later
                            } else if (info->model == INSTAX_MODEL_MINI) {
                                // Mini Link 3 specific values
                                response[12] = 0x02; // Max file size high byte
                                response[13] = 0x7B; // Max file size low byte (0x027B = 635 KB)
                                response[14] = 0x00; // Unknown byte 1
                                response[15] = 0x02; // Unknown byte 2 (was 0x06 for Square)
                                response[16] = 0x58; // Unknown byte 3 (was 0x40 for Square)
                                response[17] = 0x00; // Unknown byte 4
                                response[18] = 0x00; // Unknown byte 5 (was 0x01 for Square)
                                response[19] = 0x00; // Padding
                                response[20] = 0x00; // Padding
                                response[21] = 0x00; // Padding
                                response_len = 22; // Header(2) + Length(2) + Func(1) + Op(1) + Payload(16) = 22, checksum added later
                            } else {
                                // Square values (original)
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
                            }
                            break;

                        case INSTAX_INFO_BATTERY:
                            // Payload: [0-1: header] [2: data_len] [3: state] [4: percentage] [5-6: extra]
                            // Real Mini Link 3 sends: [61 42 00 0d 00 02 00 01] [03 50 00 10] [e9]
                            // Real Wide Link sends: [61 42 00 0d 00 02 00 01] [01 05 00 10] [36]
                            // Byte 8 is model-specific: Mini=0x03, Wide=0x01 (possibly ready/busy indicator)
                            // Byte 11 is ALWAYS 0x10 (16) on real printer - not film count! (voltage? capacity indicator?)
                            ESP_LOGI(TAG, "Sending battery info: state=%d, %d%%",
                                    info->battery_state, info->battery_percentage);
                            response[6] = 0x00; // Payload header byte 0
                            response[7] = 0x01; // Payload header byte 1 (matches query type)
                            // Byte 8 varies by model - Wide uses 0x01 (ready), Mini uses 0x03
                            response[8] = (info->model == INSTAX_MODEL_WIDE) ? 0x01 : 0x03;
                            response[9] = info->battery_percentage; // Battery percentage (0x50 = 80%)
                            response[10] = 0x00; // Extra byte 1
                            response[11] = 0x10; // ALWAYS 0x10 (16) - matches real printer exactly!
                            response_len = 12; // Header(2) + Length(2) + Func(1) + Op(1) + Payload(6) = 12, checksum added later
                            break;

                        case INSTAX_INFO_PRINTER_FUNCTION:
                            // Payload: [0-1: header] [2-9: printer data]
                            // Real device sends: [61 42 00 11 00 02 00 02] [28 00 00 0c 00 00 00 00] [13]
                            ESP_LOGI(TAG, "Sending printer function: %d photos, charging=%d",
                                    info->photos_remaining, info->is_charging);
                            ESP_LOGI(TAG, "  → Capability byte will be at payload[2], photos at payload[5]");
                            response[6] = 0x00; // Payload header byte 0
                            response[7] = 0x02; // Payload header byte 1 (matches query type)

                            // Capability byte encoding:
                            // All models encode film count in lower 4 bits!
                            // The "constant 0x15" for Wide was captured when the printer had 5 films (0x10 | 5)
                            // Mini/Square/Wide: Bits 0-3: Photos remaining (0-10), Bits 4-6: Model flags, Bit 7: Charging
                            uint8_t capability;
                            uint8_t film_count = info->photos_remaining;
                            if (film_count > 10) film_count = 10;

                            if (info->model == INSTAX_MODEL_WIDE) {
                                // Wide Link base flags: 0001 0000 (0x10)
                                // Real printer with 5 films showed 0x15 = 0x10 | 5
                                capability = 0x10 | (film_count & 0x0F);
                            } else if (info->model == INSTAX_MODEL_MINI) {
                                // Mini Link 3 base flags: 0011 0000 (0x30)
                                capability = 0x30 | (film_count & 0x0F);
                            } else if (info->model == INSTAX_MODEL_SQUARE) {
                                // Square Link base flags: 0010 0000 (0x20)
                                capability = 0x20 | (film_count & 0x0F);
                            } else {
                                // Default to Square pattern
                                capability = 0x20 | (film_count & 0x0F);
                            }

                            // Apply charging bit for all models
                            if (info->is_charging) {
                                capability |= 0x80;  // Set bit 7 for charging
                            }
                            response[8] = capability;

                            response[9] = 0x00;  // Byte 2
                            response[10] = 0x00; // Byte 3
                            response[11] = info->photos_remaining; // Legacy film count byte (for Moments Print)
                            response[12] = 0x00; // Extra byte 1
                            response[13] = 0x00; // Extra byte 2
                            response[14] = 0x00; // Extra byte 3
                            response[15] = 0x00; // Extra byte 4
                            response_len = 16; // Header(2) + Length(2) + Func(1) + Op(1) + Payload(10) = 16, checksum added later

                            // Debug: Log the exact payload bytes
                            ESP_LOGI(TAG, "  Payload bytes: [0-1]=0x%02x%02x [2]=0x%02x [3-4]=0x%02x%02x [5]=0x%02x [6-9]=0x%02x%02x%02x%02x",
                                    response[6], response[7], response[8], response[9], response[10],
                                    response[11], response[12], response[13], response[14], response[15]);
                            ESP_LOGI(TAG, "  → Capability byte 0x%02x = film count %d in lower nibble",
                                    capability, capability & 0x0F);
                            ESP_LOGI(TAG, "  → Moments Print reads payload[5] = %d",
                                    response[11]);
                            break;

                        case INSTAX_INFO_PRINT_HISTORY:
                            // Payload: [0-1: header] [2-5: lifetime count] [6-9: current pack info]
                            // Real Mini Link 3 sends: [61 42 00 11 00 02 00 03] [00 00 00 23 00 00 00 07] [1c]
                            // Byte 15 might be what the app reads for film count!
                            ESP_LOGI(TAG, "Sending print history: %lu lifetime, %d current pack",
                                    (unsigned long)info->lifetime_print_count, info->photos_remaining);
                            response[6] = 0x00; // Payload header byte 0
                            response[7] = 0x03; // Payload header byte 1 (MUST match query type 3!)
                            response[8] = (info->lifetime_print_count >> 24) & 0xFF;
                            response[9] = (info->lifetime_print_count >> 16) & 0xFF;
                            response[10] = (info->lifetime_print_count >> 8) & 0xFF;
                            response[11] = info->lifetime_print_count & 0xFF;
                            response[12] = 0x00; // Unknown byte 1
                            response[13] = 0x00; // Unknown byte 2
                            response[14] = 0x00; // Unknown byte 3
                            // Real printer sends 0x07 here when film count is 0x0b (11)
                            // Relationship unclear - maybe pack generation, error count, or calibration offset?
                            response[15] = 0x07; // Testing: use real printer's exact value
                            response_len = 16; // Header(2) + Length(2) + Func(1) + Op(1) + Payload(10) = 16, checksum added later

                            // For Wide printers, send status notification on standard service after history query
                            // This may signal "initialization complete, ready for printing"
                            if (info->model == INSTAX_MODEL_WIDE) {
                                ESP_LOGI(TAG, "Wide: Sending status notification after print history");
                                // Brief delay to let the history response send first
                                vTaskDelay(pdMS_TO_TICKS(100));
                                // Send status in INSTAX protocol format
                                uint8_t status_notify[12];
                                status_notify[0] = 0x61; // Header
                                status_notify[1] = 0x42;
                                status_notify[2] = 0x00; // Length high
                                status_notify[3] = 0x0C; // Length low (12)
                                status_notify[4] = 0x00; // Function
                                status_notify[5] = 0x00; // Operation (status)
                                status_notify[6] = 0x00; // Status OK
                                status_notify[7] = 0x01; // Ready flag
                                status_notify[8] = 0x00;
                                status_notify[9] = 0x00;
                                status_notify[10] = 0x00;
                                status_notify[11] = instax_calculate_checksum(status_notify, 11);
                                struct os_mbuf *om = ble_hs_mbuf_from_flat(status_notify, sizeof(status_notify));
                                if (om) {
                                    int rc = ble_gatts_notify_custom(s_conn_handle, s_notify_handle, om);
                                    ESP_LOGI(TAG, "Wide: Sent ready status notification on standard service, rc=%d", rc);
                                }
                            }
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

                        // Print start banner for easy log identification
                        ESP_LOGI(TAG, "");
                        ESP_LOGI(TAG, "╔════════════════════════════════════════════════════════════════╗");
                        ESP_LOGI(TAG, "║            🖨️  PRINT JOB STARTED                               ║");
                        ESP_LOGI(TAG, "╠════════════════════════════════════════════════════════════════╣");
                        ESP_LOGI(TAG, "║  Image Size:   %6lu bytes                                    ║", (unsigned long)s_print_image_size);
                        ESP_LOGI(TAG, "║  Timestamp:    %lu ms                                    ║", (unsigned long)esp_log_timestamp());
                        ESP_LOGI(TAG, "║  Print Number: %d                                             ║", printer_emulator_get_info()->lifetime_print_count + 1);
                        ESP_LOGI(TAG, "╚════════════════════════════════════════════════════════════════╝");
                        ESP_LOGI(TAG, "");

                        // Call print start callback and check if it succeeded
                        bool print_start_ok = true;
                        if (s_print_start_callback) {
                            print_start_ok = s_print_start_callback(s_print_image_size);
                        }

                        // Send response (ACK if successful, error if failed)
                        response[0] = INSTAX_HEADER_FROM_DEVICE_0;
                        response[1] = INSTAX_HEADER_FROM_DEVICE_1;
                        response_len = 8; // Header(2) + Length(2) + Func(1) + Op(1) + Status(1) + Checksum(1)
                        response[2] = (response_len >> 8) & 0xFF; // Length high byte
                        response[3] = response_len & 0xFF;         // Length low byte
                        response[4] = function;
                        response[5] = operation;

                        if (print_start_ok) {
                            response[6] = 0x00; // Status: OK
                            ESP_LOGI(TAG, "🚀 Sending print start ACK (timestamp: %lu ms)", (unsigned long)esp_log_timestamp());
                        } else {
                            response[6] = 0xB1; // Status: Error 177 (out of memory)
                            ESP_LOGE(TAG, "❌ Sending print start ERROR (out of memory)");
                        }

                        response[7] = instax_calculate_checksum(response, 7);
                        send_notification(response, response_len);

                        if (print_start_ok) {
                            ESP_LOGI(TAG, "✅ Print start ACK sent (timestamp: %lu ms)", (unsigned long)esp_log_timestamp());
                        }
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

                    // Delay before ACK to slow down sender and prevent buffer overflow
                    // This gives ESP32 time to process data (RAM buffer + SPIFFS writes)
                    vTaskDelay(pdMS_TO_TICKS(DATA_PACKET_ACK_DELAY_MS));

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

                    // Print completion banner
                    ESP_LOGI(TAG, "");
                    ESP_LOGI(TAG, "╔════════════════════════════════════════════════════════════════╗");
                    ESP_LOGI(TAG, "║            ✅ PRINT JOB COMPLETED                             ║");
                    ESP_LOGI(TAG, "╠════════════════════════════════════════════════════════════════╣");
                    ESP_LOGI(TAG, "║  Received:     %6lu bytes                                    ║", (unsigned long)s_print_bytes_received);
                    ESP_LOGI(TAG, "║  Expected:     %6lu bytes                                    ║", (unsigned long)s_print_image_size);
                    ESP_LOGI(TAG, "║  Timestamp:    %lu ms                                    ║", (unsigned long)esp_log_timestamp());
                    ESP_LOGI(TAG, "║  Print Number: %d                                             ║", printer_emulator_get_info()->lifetime_print_count);
                    ESP_LOGI(TAG, "╚════════════════════════════════════════════════════════════════╝");
                    ESP_LOGI(TAG, "");

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
                        // Real Wide:  61 42 00 15 30 10 00 01 00 00 00 1e 00 01 01 00 00 00 00 00 XX
                        // Real Mini:  61 42 00 15 30 10 00 01 00 00 00 02 ff 00 01 02 00 00 00 00 XX
                        const instax_printer_info_t *info = printer_emulator_get_info();
                        response_len = 21;
                        response[2] = 0x00; // Length high byte
                        response[3] = 0x15; // Length low byte (21)
                        response[6] = 0x00; // Payload header 1
                        response[7] = 0x01; // Payload header 2 (matches query type)
                        response[8] = 0x00;
                        response[9] = 0x00;
                        response[10] = 0x00;

                        if (info->model == INSTAX_MODEL_WIDE) {
                            // Wide-specific sensor data from real Wide printer capture
                            response[11] = 0x1e; // Wide: 0x1e (30 decimal)
                            response[12] = 0x00; // Wide: 0x00
                            response[13] = 0x01; // Wide: 0x01
                            response[14] = 0x01; // Wide: 0x01
                            response[15] = 0x00; // Wide: 0x00
                        } else {
                            // Mini/Square sensor data
                            response[11] = 0x02;
                            response[12] = 0xff;
                            response[13] = 0x00;
                            response[14] = 0x01;
                            response[15] = 0x02;
                        }
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

                // Check if this is a data packet (func=0x10, op=0x01) - skip verbose logging for these
                bool is_data_packet = (chunk_len >= 6 && chunk[4] == 0x10 && chunk[5] == 0x01);

                if (!is_data_packet) {
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
                }
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
                // Check if this is a data packet - skip verbose logging
                bool is_data_packet = (s_packet_buffer_len >= 6 &&
                                      s_packet_buffer[4] == 0x10 && s_packet_buffer[5] == 0x01);

                if (!is_data_packet) {
                    ESP_LOGI(TAG, "✅ Complete packet received: %d bytes - processing", s_packet_buffer_len);
                }

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

                // Send security request to initiate bonding (matches real INSTAX printer)
                // Real printer sends this ~90ms after connection
                int rc = ble_gap_security_initiate(event->connect.conn_handle);
                if (rc == 0) {
                    ESP_LOGI(TAG, "Security request sent (bonding initiation)");
                } else {
                    ESP_LOGW(TAG, "Failed to send security request: %d", rc);
                }
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
            ESP_LOGI(TAG, "📌 Subscribe event; conn_handle=%d attr_handle=%d",
                     event->subscribe.conn_handle,
                     event->subscribe.attr_handle);
            ESP_LOGI(TAG, "   Previous state: notify=%d indicate=%d",
                     event->subscribe.prev_notify,
                     event->subscribe.prev_indicate);
            ESP_LOGI(TAG, "   Current state:  notify=%d indicate=%d",
                     event->subscribe.cur_notify,
                     event->subscribe.cur_indicate);

            // Determine what action is being taken
            if (event->subscribe.cur_notify && !event->subscribe.prev_notify) {
                ESP_LOGI(TAG, "   ✅ Client SUBSCRIBED to notifications on handle %d", event->subscribe.attr_handle);

                // Check if this is the Wide FFEA characteristic
                if (event->subscribe.attr_handle == s_wide_ffea_notify_handle) {
                    ESP_LOGI(TAG, "   🎯 Wide FFEA subscription detected - sending initial notification");
                    // Send FFEA notification immediately when client subscribes
                    send_wide_ffea_notification();
                }
                // Wide FFE1: Real printer does NOT send notifications on subscribe or write
                // The nRF Connect capture shows FFEA sends notifications but FFE1 does not
                // Commenting out FFE1 notification to match real printer behavior
                else if (event->subscribe.attr_handle == s_wide_ffe1_notify_handle) {
                    ESP_LOGI(TAG, "   📝 Wide FFE1 subscription detected - NOT sending notification (matches real printer)");
                    // Real Wide printer does NOT send FFE1 notifications - do nothing
                }
            } else if (!event->subscribe.cur_notify && event->subscribe.prev_notify) {
                ESP_LOGI(TAG, "   ❌ Client UNSUBSCRIBED from notifications on handle %d", event->subscribe.attr_handle);
            } else if (event->subscribe.cur_notify && event->subscribe.prev_notify) {
                ESP_LOGW(TAG, "   🔄 Client RE-SUBSCRIBED to notifications on handle %d (already subscribed!)", event->subscribe.attr_handle);
            }

            if (event->subscribe.cur_indicate && !event->subscribe.prev_indicate) {
                ESP_LOGI(TAG, "   ✅ Client SUBSCRIBED to indications on handle %d", event->subscribe.attr_handle);
            } else if (!event->subscribe.cur_indicate && event->subscribe.prev_indicate) {
                ESP_LOGI(TAG, "   ❌ Client UNSUBSCRIBED from indications on handle %d", event->subscribe.attr_handle);
            }

            // Log which characteristic this is
            const char *char_name = "Unknown";
            if (event->subscribe.attr_handle == 8) {
                char_name = "Command characteristic";
            } else if (event->subscribe.attr_handle == 18) {
                char_name = "Status/Notification characteristic";
            } else if (event->subscribe.attr_handle == s_wide_ffea_notify_handle) {
                char_name = "Wide FFEA status characteristic";
            } else if (event->subscribe.attr_handle == s_wide_ffe1_notify_handle) {
                char_name = "Wide FFE1 battery/film characteristic";
            }
            ESP_LOGI(TAG, "   Characteristic: %s", char_name);
            // Debug: show Wide handles for comparison
            if (s_wide_ffea_notify_handle != 0) {
                ESP_LOGI(TAG, "   (Wide handles: FFE1=%d, FFEA=%d)",
                         s_wide_ffe1_notify_handle, s_wide_ffea_notify_handle);
            }
            break;

        case BLE_GAP_EVENT_ENC_CHANGE:
            ESP_LOGI(TAG, "🔐 Encryption change event; status=%d", event->enc_change.status);
            if (event->enc_change.status == 0) {
                struct ble_gap_conn_desc desc;
                ble_gap_conn_find(event->enc_change.conn_handle, &desc);
                ESP_LOGI(TAG, "   Encryption %s, authenticated: %s, bonded: %s",
                         desc.sec_state.encrypted ? "ON" : "OFF",
                         desc.sec_state.authenticated ? "YES" : "NO",
                         desc.sec_state.bonded ? "YES" : "NO");
            }
            break;

        case BLE_GAP_EVENT_PASSKEY_ACTION:
            ESP_LOGI(TAG, "🔑 Passkey action event");
            // Real INSTAX printer uses Just Works pairing (no passkey needed)
            // This event shouldn't occur with NoInputNoOutput I/O capability
            struct ble_sm_io pkey = {0};
            pkey.action = event->passkey.params.action;
            int rc = ble_sm_inject_io(event->passkey.conn_handle, &pkey);
            ESP_LOGI(TAG, "   ble_sm_inject_io result: %d", rc);
            break;

        case BLE_GAP_EVENT_NOTIFY_TX:
            // Notification transmitted successfully
            break;

        case BLE_GAP_EVENT_MTU:
            ESP_LOGI(TAG, "📏 MTU update event; conn_handle=%d mtu=%d",
                     event->mtu.conn_handle,
                     event->mtu.value);
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

    // Update Device Information Service to match current printer model
    // CRITICAL: Must do this before advertising so DIS model number matches manufacturer data
    ble_peripheral_update_dis_from_printer_info();

    // Set advertising data (main packet)
    // CRITICAL: iOS Core Bluetooth filtering requires service UUID in MAIN packet, not scan response!
    // Include Fujifilm manufacturer data for additional app filtering
    // NOTE: Manufacturer data differs between models - select dynamically based on current model
    const instax_printer_info_t *printer_info = printer_emulator_get_info();
    uint8_t mfg_data[6];  // Max size needed
    size_t mfg_data_len;

    mfg_data[0] = 0xD8;  // Company ID low byte
    mfg_data[1] = 0x04;  // Company ID high byte (0x04D8 = Fujifilm)

    switch (printer_info->model) {
        case INSTAX_MODEL_MINI:
            // Mini Link 3 manufacturer data - from nRF Connect scan of real device
            // Real device shows: <04D8> 0700 → D8 04 07 00 (4 bytes)
            mfg_data[2] = 0x07;
            mfg_data[3] = 0x00;
            mfg_data_len = 4;
            ESP_LOGI(TAG, "Using Mini Link 3 manufacturer data: D8 04 07 00");
            break;
        case INSTAX_MODEL_SQUARE:
            // Square Link manufacturer data (captured from physical Square printer)
            mfg_data[2] = 0x05;
            mfg_data[3] = 0x00;
            mfg_data_len = 4;
            ESP_LOGI(TAG, "Using Square Link manufacturer data: D8 04 05 00");
            break;
        case INSTAX_MODEL_WIDE:
            // Wide Link manufacturer data (captured from physical Wide printer)
            mfg_data[2] = 0x02;
            mfg_data[3] = 0x00;
            mfg_data_len = 4;
            ESP_LOGI(TAG, "Using Wide Link manufacturer data: D8 04 02 00");
            break;
        default:
            // Default to Mini Link 3
            mfg_data[2] = 0x07;
            mfg_data[3] = 0x00;
            mfg_data_len = 4;
            ESP_LOGI(TAG, "Using default (Mini Link 3) manufacturer data: D8 04 07 00");
            break;
    }

    struct ble_hs_adv_fields fields = {0};
    // Use LIMITED discoverable mode to match real INSTAX printers (not GENERAL)
    // Real printers: LE Limited Discoverable = true, LE General Discoverable = false
    // This is critical - Mini/Wide apps filter for LIMITED mode, Square accepts GENERAL
    fields.flags = BLE_HS_ADV_F_DISC_LTD | BLE_HS_ADV_F_BREDR_UNSUP;  // = 0x05
    // Put standard print service UUID in MAIN advertising packet for iOS filtering
    fields.uuids128 = (ble_uuid128_t[]) { instax_service_uuid };
    fields.num_uuids128 = 1;
    fields.uuids128_is_complete = 1;
    ESP_LOGI(TAG, "Advertising standard Instax Service UUID: 70954782-2d83-473d-9e5f-81e1d02d5273");
    fields.mfg_data = mfg_data;
    fields.mfg_data_len = mfg_data_len;
    // Add TX Power - model specific (from real printer captures)
    // Mini: 6 dBm, Square: 3 dBm, Wide: 0 dBm
    if (printer_info->model == INSTAX_MODEL_MINI) {
        fields.tx_pwr_lvl = 6;
    } else if (printer_info->model == INSTAX_MODEL_WIDE) {
        fields.tx_pwr_lvl = 0;
    } else {
        fields.tx_pwr_lvl = 3;  // Square
    }
    fields.tx_pwr_lvl_is_present = 1;

    // Log the complete manufacturer data for verification
    char hex_str[32];
    snprintf(hex_str, sizeof(hex_str), "%02X %02X %02X %02X%s",
             mfg_data[0], mfg_data[1], mfg_data[2], mfg_data[3],
             mfg_data_len > 4 ? " 00" : "");
    ESP_LOGI(TAG, "Advertising manufacturer data [%d bytes]: %s", mfg_data_len, hex_str);
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

    // Set scan response data (device name + Wide E0FF service for Wide model)
    struct ble_hs_adv_fields rsp_fields = {0};
    rsp_fields.name = (uint8_t *)device_name;
    // CRITICAL: Do NOT include null terminator in scan response length
    // Real Mini: length=21 for "INSTAX-70423278(BLE)" (no \0)
    // We were incorrectly adding +1, causing length=22 with \0 byte
    rsp_fields.name_len = strlen(device_name);
    rsp_fields.name_is_complete = 1;

    // For Wide model, add E0FF service UUID to scan response
    // This helps the Wide app recognize it should subscribe to the Wide service
    if (printer_info->model == INSTAX_MODEL_WIDE) {
        rsp_fields.uuids128 = (ble_uuid128_t[]) { wide_service_uuid };
        rsp_fields.num_uuids128 = 1;
        rsp_fields.uuids128_is_complete = 0;  // Incomplete list (main service is in adv packet)
        ESP_LOGI(TAG, "Scan response includes Wide E0FF service UUID");
    }

    ESP_LOGI(TAG, "Setting scan response data: name='%s' (len=%d, NO null terminator)", device_name, (int)strlen(device_name));
    rc = ble_gap_adv_rsp_set_fields(&rsp_fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to set scan response data: %d", rc);
        // Try without Wide service UUID if it doesn't fit
        if (printer_info->model == INSTAX_MODEL_WIDE) {
            ESP_LOGW(TAG, "Retrying scan response without Wide service UUID...");
            rsp_fields.uuids128 = NULL;
            rsp_fields.num_uuids128 = 0;
            rc = ble_gap_adv_rsp_set_fields(&rsp_fields);
        }
        if (rc != 0) {
            ESP_LOGW(TAG, "Scan response failed, continuing without device name in scan response");
        }
    }

    // Start advertising
    struct ble_gap_adv_params adv_params = {0};
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    // Use LIMITED discoverable mode to match real INSTAX printers
    adv_params.disc_mode = BLE_GAP_DISC_MODE_LTD;
    adv_params.itvl_min = 160;  // 100ms (in 0.625ms units)
    adv_params.itvl_max = 240;  // 150ms (in 0.625ms units)
    // CRITICAL: Set channel map to use all 3 advertising channels (37, 38, 39)
    // Value 7 = 0b111 = channels 37, 38, and 39
    // Without this, adv_channel_map defaults to 0 and NO channels are used!
    adv_params.channel_map = 7;

    // Use random address to match real INSTAX printer (TxAdd: Random)
    rc = ble_gap_adv_start(BLE_OWN_ADDR_RANDOM, NULL, BLE_HS_FOREVER,
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
    ESP_LOGI(TAG, "Using BLE random address, connectable, LIMITED discoverable (matches real INSTAX)");
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

    // Set a static random BLE address to match real INSTAX printer behavior
    // Real INSTAX printers use the pattern: fa:ab:bc:XX:YY:ZZ
    // where fa:ab:bc is the fixed prefix and XX:YY:ZZ is device-specific
    // (likely derived from serial number or device ID)
    ble_addr_t addr;

    // Use fa:ab:bc:87:55:00 to match Mini address range
    // Real Mini uses fa:ab:bc:86:XX:XX - Mini/Wide apps filter on 0x8X range
    // Format: fa:ab:bc:87:55:00
    addr.val[0] = 0x00;  // Least significant byte (rightmost in display)
    addr.val[1] = 0x55;
    addr.val[2] = 0x87;  // Changed from 0x55 to 0x87 to match Mini whitelist range
    addr.val[3] = 0xbc;
    addr.val[4] = 0xab;
    addr.val[5] = 0xfa;  // Most significant byte (leftmost in display)

    int rc = ble_hs_id_set_rnd(addr.val);
    if (rc == 0) {
        ESP_LOGI(TAG, "Device random BLE address: %02x:%02x:%02x:%02x:%02x:%02x (INSTAX pattern)",
                 addr.val[5], addr.val[4], addr.val[3],
                 addr.val[2], addr.val[1], addr.val[0]);
    } else {
        ESP_LOGE(TAG, "Failed to set random address: %d", rc);
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

// =====================================================
// Link 3 Info Service Characteristic Access Callback
// =====================================================
static int link3_info_chr_access(uint16_t conn_handle, uint16_t attr_handle,
                                 struct ble_gatt_access_ctxt *ctxt, void *arg) {
    const ble_uuid_t *uuid = ctxt->chr->uuid;

    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        // Get printer state for battery/film info
        const instax_printer_info_t *printer_info = printer_emulator_get_info();

        if (ble_uuid_cmp(uuid, &link3_ffd2_uuid.u) == 0) {
            // FFD2 - Real device returns: 88 B4 36 86 18 4E 00 00 00 00 00 00
            // This appears to be some kind of device identifier
            uint8_t ffd2_data[] = {0x88, 0xB4, 0x36, 0x86, 0x18, 0x4E, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
            ESP_LOGI(TAG, "Link3 Info: FFD2 read");
            return os_mbuf_append(ctxt->om, ffd2_data, sizeof(ffd2_data));
        }
        else if (ble_uuid_cmp(uuid, &link3_fff1_uuid.u) == 0) {
            // FFF1 - Contains battery and film count for Link 3
            // Format from INSTAX_PROTOCOL.md:
            // Byte 0: Photos remaining (0-10)
            // Byte 8: Battery level (raw, 0-200 scale)
            // Byte 9: Charging status (0xFF = NOT charging)
            uint8_t fff1_data[12] = {0};
            fff1_data[0] = (uint8_t)printer_info->photos_remaining;  // Photos left
            fff1_data[1] = 0x01;
            fff1_data[3] = 0x15;  // Some status byte
            fff1_data[6] = 0x4F;  // Some status byte
            fff1_data[8] = (uint8_t)((printer_info->battery_percentage * 200) / 100);  // Battery (0-200 scale)
            fff1_data[9] = printer_info->is_charging ? 0x00 : 0xFF;  // 0xFF = NOT charging
            fff1_data[10] = 0x0F;
            ESP_LOGI(TAG, "Link3 Info: FFF1 read - %d photos, %d%% battery",
                     printer_info->photos_remaining, printer_info->battery_percentage);
            return os_mbuf_append(ctxt->om, fff1_data, sizeof(fff1_data));
        }
        else if (ble_uuid_cmp(uuid, &link3_ffd1_uuid.u) == 0) {
            ESP_LOGI(TAG, "Link3 Info: FFD1 read");
            uint8_t empty_data[4] = {0};
            return os_mbuf_append(ctxt->om, empty_data, sizeof(empty_data));
        }
        else if (ble_uuid_cmp(uuid, &link3_ffd3_uuid.u) == 0) {
            // FFD3 returns "attribute not found" on real device
            ESP_LOGW(TAG, "Link3 Info: FFD3 read (not supported on real device)");
            return BLE_ATT_ERR_ATTR_NOT_FOUND;
        }
        else if (ble_uuid_cmp(uuid, &link3_ffd4_uuid.u) == 0) {
            // FFD4 also returns "attribute not found" on real device
            ESP_LOGW(TAG, "Link3 Info: FFD4 read (not supported on real device)");
            return BLE_ATT_ERR_ATTR_NOT_FOUND;
        }
        else if (ble_uuid_cmp(uuid, &link3_ffe0_uuid.u) == 0) {
            // FFE0 - Real Mini Link 3 returns this exact data
            // Captured from real device via nRF Connect
            uint8_t ffe0_data[] = {
                0x00, 0x00, 0x00, 0x00, 0x02, 0x40, 0x25, 0x00,
                0x02, 0x00, 0xE0, 0xEE, 0x33, 0x65, 0x00, 0x00,
                0x33, 0x65, 0x00, 0x00
            };
            ESP_LOGI(TAG, "Link3 Info: FFE0 read (20 bytes)");
            return os_mbuf_append(ctxt->om, ffe0_data, sizeof(ffe0_data));
        }
        else if (ble_uuid_cmp(uuid, &link3_ffe1_uuid.u) == 0) {
            // FFE1 - Real Mini Link 3 returns this exact data
            uint8_t ffe1_data[] = {0xCE, 0x63, 0x00, 0x00, 0x12, 0x00, 0x00, 0x01};
            ESP_LOGI(TAG, "Link3 Info: FFE1 read (8 bytes)");
            return os_mbuf_append(ctxt->om, ffe1_data, sizeof(ffe1_data));
        }
        else if (ble_uuid_cmp(uuid, &link3_fff3_uuid.u) == 0) {
            // FFF3 - Real Mini Link 3 returns this exact data
            uint8_t fff3_data[] = {0x10, 0x00};
            ESP_LOGI(TAG, "Link3 Info: FFF3 read (2 bytes)");
            return os_mbuf_append(ctxt->om, fff3_data, sizeof(fff3_data));
        }
        else if (ble_uuid_cmp(uuid, &link3_fff4_uuid.u) == 0) {
            // FFF4 - Real Mini Link 3 returns this exact data (accelerometer?)
            uint8_t fff4_data[] = {
                0x00, 0x30, 0x00, 0x00, 0x00, 0xC0, 0x01, 0x00,
                0x00, 0xF0, 0x04, 0x00, 0x00, 0xB0, 0x00, 0x00,
                0x00, 0x50, 0x01, 0x00
            };
            ESP_LOGI(TAG, "Link3 Info: FFF4 read (20 bytes)");
            return os_mbuf_append(ctxt->om, fff4_data, sizeof(fff4_data));
        }
        else if (ble_uuid_cmp(uuid, &link3_fff5_uuid.u) == 0) {
            // FFF5 - Real Mini Link 3 returns this exact data
            uint8_t fff5_data[] = {0x00, 0x30, 0x00, 0x00, 0x00, 0x40, 0x01, 0x00};
            ESP_LOGI(TAG, "Link3 Info: FFF5 read (8 bytes)");
            return os_mbuf_append(ctxt->om, fff5_data, sizeof(fff5_data));
        }
        else {
            // All other characteristics return empty data
            ESP_LOGI(TAG, "Link3 Info: Other characteristic read");
            uint8_t empty_data[4] = {0};
            return os_mbuf_append(ctxt->om, empty_data, sizeof(empty_data));
        }
    }

    return 0;
}

// =====================================================
// Link 3 Status Service Characteristic Access Callback
// =====================================================
static int link3_status_chr_access(uint16_t conn_handle, uint16_t attr_handle,
                                   struct ble_gatt_access_ctxt *ctxt, void *arg) {
    const ble_uuid_t *uuid = ctxt->chr->uuid;

    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        if (ble_uuid_cmp(uuid, &link3_control_char_uuid.u) == 0) {
            ESP_LOGI(TAG, "Link3 Status: Control read");
            uint8_t control_data[4] = {0};
            return os_mbuf_append(ctxt->om, control_data, sizeof(control_data));
        }
    }
    else if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
        if (ble_uuid_cmp(uuid, &link3_control_char_uuid.u) == 0) {
            ESP_LOGI(TAG, "Link3 Status: Control write (%d bytes)", OS_MBUF_PKTLEN(ctxt->om));
            // Accept the write but don't process it
            return 0;
        }
    }

    return 0;
}

/**
 * Send Wide FFE1 status notification
 * FFE1 is Write/Notify - app writes to request status, printer responds with notification
 */
static esp_err_t send_wide_ffe1_notification(void) {
    if (!s_connected || s_conn_handle == BLE_HS_CONN_HANDLE_NONE) {
        ESP_LOGW(TAG, "Cannot send Wide FFE1 notification: not connected");
        return ESP_ERR_INVALID_STATE;
    }

    if (s_wide_ffe1_notify_handle == 0) {
        ESP_LOGW(TAG, "Cannot send Wide FFE1 notification: handle not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    const instax_printer_info_t *info = printer_emulator_get_info();

    // Build FFE1 status notification - format derived from FFEA pattern
    // This provides printer status in response to app queries
    uint8_t ffe1_data[12] = {0};
    ffe1_data[0] = (uint8_t)info->photos_remaining;  // Photos remaining
    ffe1_data[1] = info->printer_busy ? 0x00 : 0x01;  // Ready status (0x01 = ready)
    ffe1_data[2] = 0x00;  // Unknown
    ffe1_data[3] = 0x15;  // Capability/status byte (Wide uses 0x15)
    ffe1_data[4] = 0x00;  // Unknown
    ffe1_data[5] = 0x00;  // Unknown
    ffe1_data[6] = 0x4F;  // Status byte
    ffe1_data[7] = 0x00;  // Unknown
    ffe1_data[8] = (uint8_t)((info->battery_percentage * 200) / 100);  // Battery (0-200 scale)
    ffe1_data[9] = info->is_charging ? 0x00 : 0xFF;  // 0xFF = NOT charging
    ffe1_data[10] = 0x0F;  // Status byte
    ffe1_data[11] = 0x00;  // Unknown

    struct os_mbuf *om = ble_hs_mbuf_from_flat(ffe1_data, sizeof(ffe1_data));
    if (om == NULL) {
        ESP_LOGE(TAG, "Failed to allocate mbuf for Wide FFE1 notification");
        return ESP_ERR_NO_MEM;
    }

    int rc = ble_gatts_notify_custom(s_conn_handle, s_wide_ffe1_notify_handle, om);
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to send Wide FFE1 notification: %d", rc);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "📤 Sent Wide FFE1 notification (12 bytes): %d photos, %d%% battery, ready=%d",
            info->photos_remaining, info->battery_percentage, !info->printer_busy);

    return ESP_OK;
}

/**
 * Wide service characteristic access handler
 * Handles FFE1, FFE9, FFEA characteristics for Wide Link printer
 *
 * FFE1: Write/Notify - App writes commands, printer responds with notifications
 * FFE9: Write - Command/control characteristic
 * FFEA: Notify - Status notifications (sent on subscription)
 */
static int wide_chr_access(uint16_t conn_handle, uint16_t attr_handle,
                           struct ble_gatt_access_ctxt *ctxt, void *arg) {
    const ble_uuid_t *uuid = ctxt->chr->uuid;

    if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
        if (ble_uuid_cmp(uuid, &wide_ffe1_uuid.u) == 0) {
            // FFE1 - App writes to request status, we MUST respond with notification
            // Protocol doc: "When the app writes to FFE1 (any data), the printer must respond
            // with a 12-byte notification containing current status"
            uint16_t len = OS_MBUF_PKTLEN(ctxt->om);
            uint8_t write_data[32] = {0};
            uint16_t copy_len = (len < sizeof(write_data)) ? len : sizeof(write_data);
            os_mbuf_copydata(ctxt->om, 0, copy_len, write_data);

            ESP_LOGI(TAG, "Wide: FFE1 write (%d bytes) - sending notification response", len);
            if (len > 0) {
                ESP_LOGI(TAG, "  Write data: %02X %02X %02X %02X...",
                        write_data[0], write_data[1], write_data[2], write_data[3]);
            }

            // Send FFE1 notification response with current status
            send_wide_ffe1_notification();
            return 0;
        }
        else if (ble_uuid_cmp(uuid, &wide_ffe9_uuid.u) == 0) {
            // FFE9 - Command/control characteristic
            ESP_LOGI(TAG, "Wide: FFE9 write (%d bytes)", OS_MBUF_PKTLEN(ctxt->om));
            // Accept the write but don't process it for now
            return 0;
        }
    }
    else if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        // Handle READ operations
        if (ble_uuid_cmp(uuid, &wide_ffe1_uuid.u) == 0) {
            // FFE1 READ - Return current status (same format as notification)
            const instax_printer_info_t *info = printer_emulator_get_info();
            uint8_t ffe1_data[12] = {0};
            ffe1_data[0] = (uint8_t)info->photos_remaining;
            ffe1_data[1] = info->printer_busy ? 0x00 : 0x01;  // 0x01 = ready
            ffe1_data[2] = 0x00;
            ffe1_data[3] = 0x15;
            ffe1_data[4] = 0x00;
            ffe1_data[5] = 0x00;
            ffe1_data[6] = 0x4F;
            ffe1_data[7] = 0x00;
            ffe1_data[8] = (uint8_t)((info->battery_percentage * 200) / 100);
            ffe1_data[9] = info->is_charging ? 0x00 : 0xFF;
            ffe1_data[10] = 0x0F;
            ffe1_data[11] = 0x00;

            ESP_LOGI(TAG, "Wide: FFE1 READ - returning status (12 bytes): %d photos, ready=%d",
                    info->photos_remaining, !info->printer_busy);
            return os_mbuf_append(ctxt->om, ffe1_data, sizeof(ffe1_data));
        }
        else if (ble_uuid_cmp(uuid, &wide_ffea_uuid.u) == 0) {
            // FFEA READ - Return "printer ready" status (same format as notification)
            uint8_t ffea_data[11] = {
                0x02, 0x09, 0xB9, 0x00, 0x11, 0x01, 0x00, 0x80, 0x84, 0x1E, 0x00
            };

            ESP_LOGI(TAG, "Wide: FFEA READ - returning ready status (11 bytes)");
            return os_mbuf_append(ctxt->om, ffea_data, sizeof(ffea_data));
        }
    }

    return 0;
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

    // ========================================================================
    // MAC Address Setup (MUST happen before BLE initialization)
    // ========================================================================
    uint8_t factory_mac[6];
    esp_err_t ret = esp_read_mac(factory_mac, ESP_MAC_BT);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Factory BT MAC: %02X:%02X:%02X:%02X:%02X:%02X",
                 factory_mac[0], factory_mac[1], factory_mac[2],
                 factory_mac[3], factory_mac[4], factory_mac[5]);

#if USE_CUSTOM_MAC
        // Custom MAC is enabled - use the hardcoded value from top of file
        // To use a Fujifilm MAC, edit the custom_mac array at the top of this file
        ret = esp_base_mac_addr_set(custom_mac);
        if (ret == ESP_OK) {
            ESP_LOGW(TAG, "Custom BT MAC set: %02X:%02X:%02X:%02X:%02X:%02X",
                     custom_mac[0], custom_mac[1], custom_mac[2],
                     custom_mac[3], custom_mac[4], custom_mac[5]);
            ESP_LOGW(TAG, "⚠️  Using custom MAC address (research/development only)");
        } else {
            ESP_LOGE(TAG, "Failed to set custom MAC address: %d", ret);
        }
#else
        ESP_LOGI(TAG, "Using factory MAC address (custom MAC disabled)");
#endif
    } else {
        ESP_LOGE(TAG, "Failed to read factory MAC address: %d", ret);
    }
    // ========================================================================

    // Initialize NimBLE host
    nimble_port_init();

    // Configure host callbacks
    ble_hs_cfg.sync_cb = on_sync;
    ble_hs_cfg.reset_cb = on_reset;
    ble_hs_cfg.gatts_register_cb = NULL;

    // Configure Security Manager (matches real INSTAX printer behavior)
    // From packet capture: AuthReq = 0x01 (Bonding, no MITM, no SC)
    ble_hs_cfg.sm_bonding = 1;              // Enable bonding
    ble_hs_cfg.sm_mitm = 0;                 // No Man-in-the-Middle protection
    ble_hs_cfg.sm_sc = 0;                   // No Secure Connections (use legacy pairing)
    ble_hs_cfg.sm_io_cap = BLE_SM_IO_CAP_NO_IO;  // No input/output (Just Works pairing)
    ble_hs_cfg.sm_our_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;

    ESP_LOGI(TAG, "Security Manager configured: bonding=1, mitm=0, sc=0, io_cap=NoIO");

    // Initialize bond storage - required for iOS to remember device in Bluetooth settings
    ble_store_config_init();
    ESP_LOGI(TAG, "Bond storage initialized (NVS persistence enabled)");

    // Initialize GATT, GAP services
    ble_svc_gap_init();
    ble_svc_gatt_init();

    // Register custom GATT services BEFORE starting the stack
    // This must happen here, not in on_sync() callback
    // Get current printer model to select appropriate service definitions
    const instax_printer_info_t *printer_info = printer_emulator_get_info();

    // Select model-specific GATT service array
    const struct ble_gatt_svc_def *gatt_svr_svcs;
    switch (printer_info->model) {
        case INSTAX_MODEL_MINI:
            ESP_LOGI(TAG, "Using Mini GATT services (main + D0FF + 6287 - required for detection)");
            gatt_svr_svcs = gatt_svr_svcs_mini_link1;
            break;
        case INSTAX_MODEL_WIDE:
            ESP_LOGI(TAG, "Using Wide Link GATT services (with Wide service)");
            gatt_svr_svcs = gatt_svr_svcs_wide;
            break;
        case INSTAX_MODEL_SQUARE:
        default:
            ESP_LOGI(TAG, "Using Square Link GATT services (main service only)");
            gatt_svr_svcs = gatt_svr_svcs_square;
            break;
    }

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

    // Log Wide service characteristic handles for debugging
    if (printer_info->model == INSTAX_MODEL_WIDE) {
        ESP_LOGI(TAG, "Wide service handles: FFE1=%d, FFEA=%d",
                 s_wide_ffe1_notify_handle, s_wide_ffea_notify_handle);
    }

    // Initialize Device Information Service

    // Use DIS values from printer_info (model-specific defaults or user-configured)
    ble_svc_dis_model_number_set(printer_info->model_number);
    ble_svc_dis_serial_number_set(printer_info->serial_number);
    ble_svc_dis_firmware_revision_set(printer_info->firmware_revision);
    ble_svc_dis_hardware_revision_set(printer_info->hardware_revision);
    ble_svc_dis_software_revision_set(printer_info->software_revision);
    ble_svc_dis_manufacturer_name_set(printer_info->manufacturer_name);

    // System ID (2A23) - from real Mini Link 3: 00 01 02 00 00 03 04 05
    static const uint8_t system_id[] = {0x00, 0x01, 0x02, 0x00, 0x00, 0x03, 0x04, 0x05};
    ble_svc_dis_system_id_set((const char*)system_id);

    // PnP ID (2A50) - from real Mini Link 3: 01 5D 00 00 00 00 01
    // Format: Vendor ID Source (1), Vendor ID (2), Product ID (2), Product Version (2)
    static const uint8_t pnp_id[] = {0x01, 0x5D, 0x00, 0x00, 0x00, 0x00, 0x01};
    ble_svc_dis_pnp_id_set((const char*)pnp_id);

    ble_svc_dis_init();
    ESP_LOGI(TAG, "Device Information Service initialized:");
    ESP_LOGI(TAG, "  Model: %s, Serial: %s", printer_info->model_number, printer_info->serial_number);
    ESP_LOGI(TAG, "  FW: %s, HW: %s, SW: %s", printer_info->firmware_revision,
             printer_info->hardware_revision, printer_info->software_revision);
    ESP_LOGI(TAG, "  Manufacturer: %s", printer_info->manufacturer_name);
    ESP_LOGI(TAG, "  System ID and PnP ID set for Mini Link 3 compatibility");

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

void ble_peripheral_get_mac_address(uint8_t *mac_out) {
    if (mac_out == NULL) return;

    // Get the actual BLE address being used for advertising
    // INSTAX printers use random addresses, so we get BLE_ADDR_RANDOM
    uint8_t addr[6];
    int rc = ble_hs_id_copy_addr(BLE_ADDR_RANDOM, addr, NULL);
    if (rc == 0) {
        // Copy in reverse order (BLE uses little-endian)
        for (int i = 0; i < 6; i++) {
            mac_out[i] = addr[5 - i];
        }
    } else {
        // Fallback to public address if random address not available
        rc = ble_hs_id_copy_addr(BLE_ADDR_PUBLIC, addr, NULL);
        if (rc == 0) {
            for (int i = 0; i < 6; i++) {
                mac_out[i] = addr[5 - i];
            }
        }
    }
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

/**
 * Update all DIS values from printer info
 * Call this when printer model changes to ensure DIS matches current configuration
 */
void ble_peripheral_update_dis_from_printer_info(void) {
    const instax_printer_info_t *printer_info = printer_emulator_get_info();

    ble_svc_dis_model_number_set(printer_info->model_number);
    ble_svc_dis_serial_number_set(printer_info->serial_number);
    ble_svc_dis_firmware_revision_set(printer_info->firmware_revision);
    ble_svc_dis_hardware_revision_set(printer_info->hardware_revision);
    ble_svc_dis_software_revision_set(printer_info->software_revision);
    ble_svc_dis_manufacturer_name_set(printer_info->manufacturer_name);

    ESP_LOGI(TAG, "Updated DIS: Model=%s, Serial=%s, FW=%s, HW=%s, SW=%s",
             printer_info->model_number, printer_info->serial_number,
             printer_info->firmware_revision, printer_info->hardware_revision,
             printer_info->software_revision);
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
