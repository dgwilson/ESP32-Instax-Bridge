/**
 * @file instax_protocol.c
 * @brief Instax BLE Protocol Implementation
 */

#include "instax_protocol.h"
#include <string.h>

// Model information table
static const instax_model_info_t model_info[] = {
    [INSTAX_MODEL_MINI]   = { .width = 600,  .height = 800, .chunk_size = 900,  .max_file_size = 105 * 1024 },
    [INSTAX_MODEL_SQUARE] = { .width = 800,  .height = 800, .chunk_size = 1808, .max_file_size = 105 * 1024 },
    [INSTAX_MODEL_WIDE]   = { .width = 1260, .height = 840, .chunk_size = 900,  .max_file_size = 105 * 1024 },
};

const instax_model_info_t* instax_get_model_info(instax_model_t model) {
    if (model >= INSTAX_MODEL_UNKNOWN) {
        return NULL;
    }
    return &model_info[model];
}

instax_model_t instax_detect_model(uint16_t width, uint16_t height) {
    if (width == 600 && height == 800) return INSTAX_MODEL_MINI;
    if (width == 800 && height == 800) return INSTAX_MODEL_SQUARE;
    if (width == 1260 && height == 840) return INSTAX_MODEL_WIDE;
    return INSTAX_MODEL_UNKNOWN;
}

uint8_t instax_calculate_checksum(const uint8_t *data, size_t len) {
    uint8_t sum = 0;
    for (size_t i = 0; i < len; i++) {
        sum += data[i];
    }
    return (255 - sum) & 0xFF;
}

/**
 * Create a packet with header, length, event type, payload, and checksum
 */
static size_t create_packet(uint8_t function, uint8_t operation,
                            const uint8_t *payload, size_t payload_len,
                            uint8_t *buffer, size_t buffer_size) {
    // Total packet length = header(2) + length(2) + opcode(2) + payload + checksum(1) = 7 + payload_len
    size_t packet_len = 7 + payload_len;

    if (buffer_size < packet_len) {
        return 0;
    }

    // Header
    buffer[0] = INSTAX_HEADER_TO_DEVICE_0;
    buffer[1] = INSTAX_HEADER_TO_DEVICE_1;

    // Length (big-endian) - includes entire packet length
    buffer[2] = (packet_len >> 8) & 0xFF;
    buffer[3] = packet_len & 0xFF;

    // Function and operation
    buffer[4] = function;
    buffer[5] = operation;

    // Payload
    if (payload_len > 0 && payload != NULL) {
        memcpy(&buffer[6], payload, payload_len);
    }

    // Checksum (over everything except checksum itself)
    buffer[packet_len - 1] = instax_calculate_checksum(buffer, packet_len - 1);

    return packet_len;
}

size_t instax_create_info_query(instax_info_type_t info_type, uint8_t *buffer, size_t buffer_size) {
    uint8_t payload[1] = { (uint8_t)info_type };
    return create_packet(INSTAX_FUNC_INFO, INSTAX_OP_SUPPORT_FUNCTION_INFO,
                         payload, sizeof(payload), buffer, buffer_size);
}

size_t instax_create_print_start(uint32_t image_size, uint8_t *buffer, size_t buffer_size) {
    uint8_t payload[8] = {
        0x02, 0x00, 0x00, 0x00,  // Fixed prefix
        (image_size >> 24) & 0xFF,
        (image_size >> 16) & 0xFF,
        (image_size >> 8) & 0xFF,
        image_size & 0xFF
    };
    return create_packet(INSTAX_FUNC_PRINT, INSTAX_OP_PRINT_START,
                         payload, sizeof(payload), buffer, buffer_size);
}

size_t instax_create_print_data(uint32_t chunk_index, const uint8_t *data, size_t data_len,
                                 uint8_t *buffer, size_t buffer_size) {
    // Payload = 4 bytes chunk index + data
    size_t payload_len = 4 + data_len;

    if (buffer_size < 7 + payload_len) {
        return 0;
    }

    // Build payload: chunk index (big-endian) + data
    uint8_t *payload_start = &buffer[6];
    payload_start[0] = (chunk_index >> 24) & 0xFF;
    payload_start[1] = (chunk_index >> 16) & 0xFF;
    payload_start[2] = (chunk_index >> 8) & 0xFF;
    payload_start[3] = chunk_index & 0xFF;

    if (data_len > 0 && data != NULL) {
        memcpy(&payload_start[4], data, data_len);
    }

    // Now build the header
    size_t packet_len = 7 + payload_len;

    buffer[0] = INSTAX_HEADER_TO_DEVICE_0;
    buffer[1] = INSTAX_HEADER_TO_DEVICE_1;
    buffer[2] = (packet_len >> 8) & 0xFF;
    buffer[3] = packet_len & 0xFF;
    buffer[4] = INSTAX_FUNC_PRINT;
    buffer[5] = INSTAX_OP_PRINT_DATA;
    // Payload already in place
    buffer[packet_len - 1] = instax_calculate_checksum(buffer, packet_len - 1);

    return packet_len;
}

size_t instax_create_print_end(uint8_t *buffer, size_t buffer_size) {
    return create_packet(INSTAX_FUNC_PRINT, INSTAX_OP_PRINT_END, NULL, 0, buffer, buffer_size);
}

size_t instax_create_led_pattern(uint8_t *buffer, size_t buffer_size) {
    return create_packet(INSTAX_FUNC_LED, INSTAX_OP_LED_PATTERN, NULL, 0, buffer, buffer_size);
}

size_t instax_create_print_execute(uint8_t *buffer, size_t buffer_size) {
    return create_packet(INSTAX_FUNC_PRINT, INSTAX_OP_PRINT_EXECUTE, NULL, 0, buffer, buffer_size);
}

bool instax_parse_response(const uint8_t *data, size_t len,
                           uint8_t *function, uint8_t *operation,
                           const uint8_t **payload, size_t *payload_len) {
    // Minimum packet size: header(2) + length(2) + opcode(2) + checksum(1) = 7
    if (len < 7) {
        return false;
    }

    // Check header
    if (data[0] != INSTAX_HEADER_FROM_DEVICE_0 || data[1] != INSTAX_HEADER_FROM_DEVICE_1) {
        return false;
    }

    // Parse length
    uint16_t packet_len = ((uint16_t)data[2] << 8) | data[3];
    if (len < packet_len) {
        return false;
    }

    // Extract function and operation
    *function = data[4];
    *operation = data[5];

    // Calculate payload length
    // packet_len = 7 + payload_len, so payload_len = packet_len - 7
    *payload_len = packet_len - 7;
    *payload = (*payload_len > 0) ? &data[6] : NULL;

    return true;
}

bool instax_parse_image_support_info(const uint8_t *payload, size_t payload_len,
                                      uint16_t *width, uint16_t *height) {
    // Response format: [0-1: response header] [2-3: width] [4-5: height]
    if (payload_len < 6) {
        return false;
    }

    *width = ((uint16_t)payload[2] << 8) | payload[3];
    *height = ((uint16_t)payload[4] << 8) | payload[5];

    return true;
}

bool instax_parse_battery_info(const uint8_t *payload, size_t payload_len,
                                uint8_t *state, uint8_t *percentage) {
    // Response format: [0-1: response header] [2: state] [3: percentage]
    if (payload_len < 4) {
        return false;
    }

    *state = payload[2];
    *percentage = payload[3];

    return true;
}

bool instax_parse_printer_function_info(const uint8_t *payload, size_t payload_len,
                                         uint8_t *photos_remaining, bool *is_charging) {
    // Response format: [0-1: response header] [2: function byte]
    // function byte: low nibble = photos remaining, bit 7 = charging
    if (payload_len < 3) {
        return false;
    }

    uint8_t func_byte = payload[2];
    *photos_remaining = func_byte & 0x0F;
    *is_charging = (func_byte & 0x80) != 0;

    return true;
}

bool instax_parse_print_history_info(const uint8_t *payload, size_t payload_len,
                                      uint32_t *print_count) {
    // Response format: [0-1: response header] [2-5: print count (big-endian)]
    if (payload_len < 6) {
        return false;
    }

    *print_count = ((uint32_t)payload[2] << 24) |
                   ((uint32_t)payload[3] << 16) |
                   ((uint32_t)payload[4] << 8) |
                   payload[5];

    return true;
}
