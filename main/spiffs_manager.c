/**
 * @file spiffs_manager.c
 * @brief SPIFFS file system management for storing JPEG images
 */

#include "spiffs_manager.h"
#include <string.h>
#include <stdio.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include "esp_spiffs.h"
#include "esp_log.h"

static const char *TAG = "spiffs_manager";

#define SPIFFS_BASE_PATH    "/spiffs"
#define SPIFFS_PARTITION    "spiffs"

static bool s_initialized = false;

esp_err_t spiffs_manager_init(void) {
    if (s_initialized) {
        return ESP_OK;
    }

    esp_vfs_spiffs_conf_t conf = {
        .base_path = SPIFFS_BASE_PATH,
        .partition_label = SPIFFS_PARTITION,
        .max_files = 10,
        .format_if_mount_failed = true
    };

    esp_err_t ret = esp_vfs_spiffs_register(&conf);
    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "Failed to mount or format filesystem");
        } else if (ret == ESP_ERR_NOT_FOUND) {
            ESP_LOGE(TAG, "Failed to find SPIFFS partition");
        } else {
            ESP_LOGE(TAG, "Failed to initialize SPIFFS (%s)", esp_err_to_name(ret));
        }
        return ret;
    }

    size_t total = 0, used = 0;
    ret = esp_spiffs_info(SPIFFS_PARTITION, &total, &used);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "SPIFFS initialized: %d bytes total, %d bytes used", total, used);
    }

    s_initialized = true;
    return ESP_OK;
}

esp_err_t spiffs_manager_get_stats(size_t *total_bytes, size_t *used_bytes) {
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    return esp_spiffs_info(SPIFFS_PARTITION, total_bytes, used_bytes);
}

int spiffs_manager_list_files(spiffs_file_info_t *files, int max_files) {
    if (!s_initialized || files == NULL || max_files <= 0) {
        return 0;
    }

    DIR *dir = opendir(SPIFFS_BASE_PATH);
    if (dir == NULL) {
        ESP_LOGE(TAG, "Failed to open directory");
        return 0;
    }

    int count = 0;
    struct dirent *entry;
    struct stat st;
    char filepath[280];  // SPIFFS_BASE_PATH (8) + '/' (1) + max filename (255) + null (1) + padding

    while ((entry = readdir(dir)) != NULL && count < max_files) {
        // Skip directories
        if (entry->d_type == DT_DIR) {
            continue;
        }

        // Check if it's a JPEG file
        const char *ext = strrchr(entry->d_name, '.');
        if (ext == NULL || (strcasecmp(ext, ".jpg") != 0 && strcasecmp(ext, ".jpeg") != 0)) {
            continue;
        }

        // Get file size
        snprintf(filepath, sizeof(filepath), "%s/%s", SPIFFS_BASE_PATH, entry->d_name);
        if (stat(filepath, &st) != 0) {
            continue;
        }

        strncpy(files[count].filename, entry->d_name, SPIFFS_MAX_FILENAME - 1);
        files[count].filename[SPIFFS_MAX_FILENAME - 1] = '\0';
        files[count].size = st.st_size;
        count++;
    }

    closedir(dir);
    ESP_LOGI(TAG, "Found %d JPEG files", count);
    return count;
}

esp_err_t spiffs_manager_save_file(const char *filename, const uint8_t *data, size_t len) {
    if (!s_initialized || filename == NULL || data == NULL || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    char filepath[280];  // SPIFFS_BASE_PATH (8) + '/' (1) + max filename (255) + null (1) + padding
    snprintf(filepath, sizeof(filepath), "%s/%s", SPIFFS_BASE_PATH, filename);

    FILE *f = fopen(filepath, "wb");
    if (f == NULL) {
        ESP_LOGE(TAG, "Failed to create file: %s", filepath);
        return ESP_FAIL;
    }

    size_t written = fwrite(data, 1, len, f);
    fclose(f);

    if (written != len) {
        ESP_LOGE(TAG, "Failed to write all data: %d of %d bytes", written, len);
        unlink(filepath);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Saved file: %s (%d bytes)", filename, len);
    return ESP_OK;
}

esp_err_t spiffs_manager_read_file(const char *filename, uint8_t *buffer,
                                    size_t buffer_size, size_t *file_size) {
    if (!s_initialized || filename == NULL || file_size == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    char filepath[280];  // SPIFFS_BASE_PATH (8) + '/' (1) + max filename (255) + null (1) + padding
    snprintf(filepath, sizeof(filepath), "%s/%s", SPIFFS_BASE_PATH, filename);

    struct stat st;
    if (stat(filepath, &st) != 0) {
        ESP_LOGE(TAG, "File not found: %s", filepath);
        return ESP_ERR_NOT_FOUND;
    }

    *file_size = st.st_size;

    // If buffer is NULL, just return the size
    if (buffer == NULL) {
        return ESP_OK;
    }

    if (buffer_size < st.st_size) {
        ESP_LOGE(TAG, "Buffer too small: %d < %d", buffer_size, (int)st.st_size);
        return ESP_ERR_INVALID_SIZE;
    }

    FILE *f = fopen(filepath, "rb");
    if (f == NULL) {
        ESP_LOGE(TAG, "Failed to open file: %s", filepath);
        return ESP_FAIL;
    }

    size_t read = fread(buffer, 1, st.st_size, f);
    fclose(f);

    if (read != st.st_size) {
        ESP_LOGE(TAG, "Failed to read all data: %d of %d bytes", read, (int)st.st_size);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Read file: %s (%d bytes)", filename, read);
    return ESP_OK;
}

esp_err_t spiffs_manager_delete_file(const char *filename) {
    if (!s_initialized || filename == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    char filepath[64];
    snprintf(filepath, sizeof(filepath), "%s/%s", SPIFFS_BASE_PATH, filename);

    if (unlink(filepath) != 0) {
        ESP_LOGE(TAG, "Failed to delete file: %s", filepath);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Deleted file: %s", filename);
    return ESP_OK;
}

bool spiffs_manager_file_exists(const char *filename) {
    if (!s_initialized || filename == NULL) {
        return false;
    }

    char filepath[64];
    snprintf(filepath, sizeof(filepath), "%s/%s", SPIFFS_BASE_PATH, filename);

    struct stat st;
    return stat(filepath, &st) == 0;
}

esp_err_t spiffs_manager_format(void) {
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGW(TAG, "Formatting SPIFFS...");

    esp_err_t ret = esp_spiffs_format(SPIFFS_PARTITION);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Format failed: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "Format complete");
    }

    return ret;
}
