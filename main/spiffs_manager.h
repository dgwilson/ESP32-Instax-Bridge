/**
 * @file spiffs_manager.h
 * @brief SPIFFS file system management for storing JPEG images
 */

#ifndef SPIFFS_MANAGER_H
#define SPIFFS_MANAGER_H

#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

// Maximum filename length
#define SPIFFS_MAX_FILENAME     32

// Maximum number of files to list
#define SPIFFS_MAX_FILES        20

// File info structure
typedef struct {
    char filename[SPIFFS_MAX_FILENAME];
    size_t size;
} spiffs_file_info_t;

/**
 * Initialize SPIFFS filesystem
 * @return ESP_OK on success
 */
esp_err_t spiffs_manager_init(void);

/**
 * Get filesystem statistics
 * @param total_bytes Output: total filesystem size
 * @param used_bytes Output: used space
 * @return ESP_OK on success
 */
esp_err_t spiffs_manager_get_stats(size_t *total_bytes, size_t *used_bytes);

/**
 * List all JPEG files in filesystem
 * @param files Array to fill with file info
 * @param max_files Maximum files to return
 * @return Number of files found
 */
int spiffs_manager_list_files(spiffs_file_info_t *files, int max_files);

/**
 * Save a JPEG file
 * @param filename Name of file (without path)
 * @param data JPEG data
 * @param len Length of data
 * @return ESP_OK on success
 */
esp_err_t spiffs_manager_save_file(const char *filename, const uint8_t *data, size_t len);

/**
 * Read a JPEG file
 * @param filename Name of file (without path)
 * @param buffer Buffer to store data (can be NULL to just get size)
 * @param buffer_size Size of buffer
 * @param file_size Output: actual file size
 * @return ESP_OK on success
 */
esp_err_t spiffs_manager_read_file(const char *filename, uint8_t *buffer,
                                    size_t buffer_size, size_t *file_size);

/**
 * Delete a file
 * @param filename Name of file (without path)
 * @return ESP_OK on success
 */
esp_err_t spiffs_manager_delete_file(const char *filename);

/**
 * Check if a file exists
 * @param filename Name of file (without path)
 * @return true if file exists
 */
bool spiffs_manager_file_exists(const char *filename);

/**
 * Format the filesystem (deletes all files)
 * @return ESP_OK on success
 */
esp_err_t spiffs_manager_format(void);

#endif // SPIFFS_MANAGER_H
