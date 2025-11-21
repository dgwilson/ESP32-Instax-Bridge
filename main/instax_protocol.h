/**
 * @file instax_protocol.h
 * @brief Instax BLE Protocol Implementation
 *
 * Based on https://github.com/javl/InstaxBLE
 * Implements the Fujifilm Instax printer BLE protocol
 */

#ifndef INSTAX_PROTOCOL_H
#define INSTAX_PROTOCOL_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// BLE Service and Characteristic UUIDs
#define INSTAX_SERVICE_UUID             "70954782-2d83-473d-9e5f-81e1d02d5273"
#define INSTAX_WRITE_CHAR_UUID          "70954783-2d83-473d-9e5f-81e1d02d5273"
#define INSTAX_NOTIFY_CHAR_UUID         "70954784-2d83-473d-9e5f-81e1d02d5273"

// Device Information Service
#define DEVICE_INFO_SERVICE_UUID        "180A"
#define MODEL_NUMBER_CHAR_UUID          "2A24"

// Packet Headers
#define INSTAX_HEADER_TO_DEVICE_0       0x41
#define INSTAX_HEADER_TO_DEVICE_1       0x62
#define INSTAX_HEADER_FROM_DEVICE_0     0x61
#define INSTAX_HEADER_FROM_DEVICE_1     0x42

// Maximum BLE packet size
#define INSTAX_MAX_BLE_PACKET_SIZE      182

// Event Types (function codes)
#define INSTAX_FUNC_INFO                0x00
#define INSTAX_FUNC_DEVICE_CONTROL      0x01
#define INSTAX_FUNC_PRINT               0x10
#define INSTAX_FUNC_LED                 0x30

// Info Operations
#define INSTAX_OP_SUPPORT_FUNCTION_INFO 0x02

// Print Operations
#define INSTAX_OP_PRINT_START           0x00
#define INSTAX_OP_PRINT_DATA            0x01
#define INSTAX_OP_PRINT_END             0x02
#define INSTAX_OP_PRINT_CANCEL          0x03
#define INSTAX_OP_PRINT_EXECUTE         0x80

// LED Operations
#define INSTAX_OP_LED_PATTERN           0x03

// Info Types (payload for info queries)
typedef enum {
    INSTAX_INFO_IMAGE_SUPPORT = 0,
    INSTAX_INFO_BATTERY = 1,
    INSTAX_INFO_PRINTER_FUNCTION = 2,
    INSTAX_INFO_PRINT_HISTORY = 3
} instax_info_type_t;

// Printer Models
typedef enum {
    INSTAX_MODEL_MINI = 0,
    INSTAX_MODEL_SQUARE = 1,
    INSTAX_MODEL_WIDE = 2,
    INSTAX_MODEL_UNKNOWN = 255
} instax_model_t;

// Model Dimensions
typedef struct {
    uint16_t width;
    uint16_t height;
    uint16_t chunk_size;
    uint32_t max_file_size;
} instax_model_info_t;

// Printer Info Structure
typedef struct {
    instax_model_t model;
    uint16_t width;
    uint16_t height;
    uint8_t battery_state;
    uint8_t battery_percentage;
    uint8_t photos_remaining;
    bool is_charging;
    uint32_t lifetime_print_count;
    bool connected;
    char device_name[32];
    uint8_t device_address[6];
} instax_printer_info_t;

// Print Status
typedef enum {
    INSTAX_PRINT_IDLE = 0,
    INSTAX_PRINT_STARTING,
    INSTAX_PRINT_SENDING_DATA,
    INSTAX_PRINT_FINISHING,
    INSTAX_PRINT_EXECUTING,
    INSTAX_PRINT_COMPLETE,
    INSTAX_PRINT_ERROR
} instax_print_status_t;

// Print Progress Info
typedef struct {
    instax_print_status_t status;
    uint32_t total_bytes;
    uint32_t bytes_sent;
    uint8_t percent_complete;
    char error_message[64];
} instax_print_progress_t;

// Callback types
typedef void (*instax_scan_callback_t)(const char *name, const uint8_t *address);
typedef void (*instax_connect_callback_t)(bool success);
typedef void (*instax_info_callback_t)(const instax_printer_info_t *info);
typedef void (*instax_print_progress_callback_t)(const instax_print_progress_t *progress);

/**
 * Get model info for a specific model
 */
const instax_model_info_t* instax_get_model_info(instax_model_t model);

/**
 * Detect model from dimensions
 */
instax_model_t instax_detect_model(uint16_t width, uint16_t height);

/**
 * Create an info query packet
 * @param info_type The type of info to query
 * @param buffer Output buffer for packet data
 * @param buffer_size Size of output buffer
 * @return Length of packet, or 0 on error
 */
size_t instax_create_info_query(instax_info_type_t info_type, uint8_t *buffer, size_t buffer_size);

/**
 * Create print start packet
 * @param image_size Size of the image data in bytes
 * @param buffer Output buffer for packet data
 * @param buffer_size Size of output buffer
 * @return Length of packet, or 0 on error
 */
size_t instax_create_print_start(uint32_t image_size, uint8_t *buffer, size_t buffer_size);

/**
 * Create print data packet
 * @param chunk_index Index of this chunk (0-based)
 * @param data Pointer to image data chunk
 * @param data_len Length of image data chunk
 * @param buffer Output buffer for packet data
 * @param buffer_size Size of output buffer
 * @return Length of packet, or 0 on error
 */
size_t instax_create_print_data(uint32_t chunk_index, const uint8_t *data, size_t data_len,
                                 uint8_t *buffer, size_t buffer_size);

/**
 * Create print end packet
 * @param buffer Output buffer for packet data
 * @param buffer_size Size of output buffer
 * @return Length of packet, or 0 on error
 */
size_t instax_create_print_end(uint8_t *buffer, size_t buffer_size);

/**
 * Create LED pattern packet (required before print execute on Link 3)
 * @param buffer Output buffer for packet data
 * @param buffer_size Size of output buffer
 * @return Length of packet, or 0 on error
 */
size_t instax_create_led_pattern(uint8_t *buffer, size_t buffer_size);

/**
 * Create print execute packet
 * @param buffer Output buffer for packet data
 * @param buffer_size Size of output buffer
 * @return Length of packet, or 0 on error
 */
size_t instax_create_print_execute(uint8_t *buffer, size_t buffer_size);

/**
 * Parse a response packet
 * @param data Raw packet data
 * @param len Length of packet data
 * @param function Output: function code from packet
 * @param operation Output: operation code from packet
 * @param payload Output: pointer to payload start (within data buffer)
 * @param payload_len Output: length of payload
 * @return true if packet parsed successfully
 */
bool instax_parse_response(const uint8_t *data, size_t len,
                           uint8_t *function, uint8_t *operation,
                           const uint8_t **payload, size_t *payload_len);

/**
 * Parse image support info response
 * @param payload Payload data from response packet
 * @param payload_len Length of payload
 * @param width Output: image width
 * @param height Output: image height
 * @return true if parsed successfully
 */
bool instax_parse_image_support_info(const uint8_t *payload, size_t payload_len,
                                      uint16_t *width, uint16_t *height);

/**
 * Parse battery info response
 * @param payload Payload data from response packet
 * @param payload_len Length of payload
 * @param state Output: battery state
 * @param percentage Output: battery percentage
 * @return true if parsed successfully
 */
bool instax_parse_battery_info(const uint8_t *payload, size_t payload_len,
                                uint8_t *state, uint8_t *percentage);

/**
 * Parse printer function info response
 * @param payload Payload data from response packet
 * @param payload_len Length of payload
 * @param photos_remaining Output: number of photos remaining
 * @param is_charging Output: charging status
 * @return true if parsed successfully
 */
bool instax_parse_printer_function_info(const uint8_t *payload, size_t payload_len,
                                         uint8_t *photos_remaining, bool *is_charging);

/**
 * Parse print history info response
 * @param payload Payload data from response packet
 * @param payload_len Length of payload
 * @param print_count Output: lifetime print count
 * @return true if parsed successfully
 */
bool instax_parse_print_history_info(const uint8_t *payload, size_t payload_len,
                                      uint32_t *print_count);

/**
 * Calculate checksum for packet
 * @param data Packet data (without checksum)
 * @param len Length of packet data
 * @return Calculated checksum byte
 */
uint8_t instax_calculate_checksum(const uint8_t *data, size_t len);

#endif // INSTAX_PROTOCOL_H
