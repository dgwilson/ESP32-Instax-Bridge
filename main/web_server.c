/**
 * @file web_server.c
 * @brief HTTP web server for Instax bridge control interface
 */

#include "web_server.h"
#include "wifi_manager.h"
#include "ble_scanner.h"
#include "spiffs_manager.h"
#include "instax_protocol.h"
#include <string.h>
#include "esp_http_server.h"
#include "esp_log.h"
#include "cJSON.h"

static const char *TAG = "web_server";
static httpd_handle_t s_server = NULL;

// HTML template for the main page
static const char *HTML_TEMPLATE =
"<!DOCTYPE html>\n"
"<html>\n"
"<head>\n"
"    <title>ESP32 Instax Bridge</title>\n"
"    <meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n"
"    <style>\n"
"        body { font-family: Arial, sans-serif; max-width: 800px; margin: 0 auto; padding: 20px; }\n"
"        h1 { color: #333; }\n"
"        .section { background: #f5f5f5; padding: 15px; margin: 10px 0; border-radius: 8px; }\n"
"        .section h2 { margin-top: 0; color: #666; }\n"
"        button { background: #4CAF50; color: white; padding: 10px 20px; border: none; border-radius: 4px; cursor: pointer; margin: 5px; }\n"
"        button:hover { background: #45a049; }\n"
"        button.danger { background: #f44336; }\n"
"        button.danger:hover { background: #da190b; }\n"
"        button:disabled { background: #ccc; cursor: not-allowed; }\n"
"        input, select { padding: 8px; margin: 5px; border: 1px solid #ddd; border-radius: 4px; }\n"
"        .status { padding: 10px; margin: 10px 0; border-radius: 4px; }\n"
"        .status.connected { background: #dff0d8; color: #3c763d; }\n"
"        .status.disconnected { background: #f2dede; color: #a94442; }\n"
"        .status.scanning { background: #fcf8e3; color: #8a6d3b; }\n"
"        .device-list { list-style: none; padding: 0; }\n"
"        .device-list li { padding: 10px; margin: 5px 0; background: #fff; border: 1px solid #ddd; border-radius: 4px; cursor: pointer; }\n"
"        .device-list li:hover { background: #e8f4ea; }\n"
"        .device-list li.instax { border-left: 4px solid #4CAF50; }\n"
"        .file-list { list-style: none; padding: 0; }\n"
"        .file-list li { padding: 10px; margin: 5px 0; background: #fff; border: 1px solid #ddd; border-radius: 4px; display: flex; justify-content: space-between; align-items: center; }\n"
"        .progress { width: 100%%; height: 20px; background: #ddd; border-radius: 10px; overflow: hidden; }\n"
"        .progress-bar { height: 100%%; background: #4CAF50; transition: width 0.3s; }\n"
"        .printer-info { display: grid; grid-template-columns: repeat(2, 1fr); gap: 10px; }\n"
"        .printer-info div { background: #fff; padding: 10px; border-radius: 4px; }\n"
"    </style>\n"
"</head>\n"
"<body>\n"
"    <h1>ESP32 Instax Bridge</h1>\n"
"\n"
"    <div class=\"section\">\n"
"        <h2>Printer Connection</h2>\n"
"        <div id=\"ble-status\" class=\"status disconnected\">Not connected</div>\n"
"        <button onclick=\"startScan()\">Scan for Printers</button>\n"
"        <button onclick=\"disconnect()\" class=\"danger\">Disconnect</button>\n"
"        <ul id=\"device-list\" class=\"device-list\"></ul>\n"
"        <div id=\"printer-info\" class=\"printer-info\" style=\"display:none;\"></div>\n"
"    </div>\n"
"\n"
"    <div class=\"section\">\n"
"        <h2>Upload Image</h2>\n"
"        <form id=\"upload-form\" enctype=\"multipart/form-data\">\n"
"            <input type=\"file\" id=\"file-input\" name=\"file\" accept=\".jpg,.jpeg\">\n"
"            <button type=\"submit\">Upload</button>\n"
"        </form>\n"
"        <div id=\"upload-progress\" style=\"display:none;\">\n"
"            <div class=\"progress\"><div class=\"progress-bar\" id=\"upload-bar\"></div></div>\n"
"        </div>\n"
"    </div>\n"
"\n"
"    <div class=\"section\">\n"
"        <h2>Stored Images</h2>\n"
"        <button onclick=\"refreshFiles()\">Refresh</button>\n"
"        <ul id=\"file-list\" class=\"file-list\"></ul>\n"
"    </div>\n"
"\n"
"    <div class=\"section\">\n"
"        <h2>Print Status</h2>\n"
"        <div id=\"print-status\">Ready</div>\n"
"        <div id=\"print-progress\" style=\"display:none;\">\n"
"            <div class=\"progress\"><div class=\"progress-bar\" id=\"print-bar\"></div></div>\n"
"        </div>\n"
"    </div>\n"
"\n"
"    <script>\n"
"        function startScan() {\n"
"            document.getElementById('device-list').innerHTML = '';\n"
"            document.getElementById('ble-status').textContent = 'Scanning...';\n"
"            document.getElementById('ble-status').className = 'status scanning';\n"
"            fetch('/api/scan', {method: 'POST'})\n"
"                .then(r => r.json())\n"
"                .then(d => {\n"
"                    if(d.success) setTimeout(getDevices, 3000);\n"
"                });\n"
"        }\n"
"\n"
"        function getDevices() {\n"
"            fetch('/api/devices')\n"
"                .then(r => r.json())\n"
"                .then(d => {\n"
"                    const list = document.getElementById('device-list');\n"
"                    list.innerHTML = '';\n"
"                    d.devices.forEach(dev => {\n"
"                        const li = document.createElement('li');\n"
"                        li.className = dev.is_instax ? 'instax' : '';\n"
"                        li.textContent = dev.name + ' (' + dev.rssi + ' dBm)';\n"
"                        li.onclick = () => connectDevice(dev.address);\n"
"                        list.appendChild(li);\n"
"                    });\n"
"                    document.getElementById('ble-status').textContent = 'Found ' + d.devices.length + ' devices';\n"
"                    document.getElementById('ble-status').className = 'status';\n"
"                });\n"
"        }\n"
"\n"
"        function connectDevice(addr) {\n"
"            document.getElementById('ble-status').textContent = 'Connecting...';\n"
"            document.getElementById('ble-status').className = 'status scanning';\n"
"            fetch('/api/connect', {\n"
"                method: 'POST',\n"
"                headers: {'Content-Type': 'application/json'},\n"
"                body: JSON.stringify({address: addr})\n"
"            }).then(r => r.json())\n"
"              .then(d => {\n"
"                  if(d.success) {\n"
"                      document.getElementById('ble-status').textContent = 'Connected';\n"
"                      document.getElementById('ble-status').className = 'status connected';\n"
"                      getPrinterInfo();\n"
"                  } else {\n"
"                      document.getElementById('ble-status').textContent = 'Connection failed';\n"
"                      document.getElementById('ble-status').className = 'status disconnected';\n"
"                  }\n"
"              });\n"
"        }\n"
"\n"
"        function disconnect() {\n"
"            fetch('/api/disconnect', {method: 'POST'});\n"
"            document.getElementById('ble-status').textContent = 'Disconnected';\n"
"            document.getElementById('ble-status').className = 'status disconnected';\n"
"            document.getElementById('printer-info').style.display = 'none';\n"
"        }\n"
"\n"
"        function getPrinterInfo() {\n"
"            fetch('/api/printer-info')\n"
"                .then(r => r.json())\n"
"                .then(d => {\n"
"                    const info = document.getElementById('printer-info');\n"
"                    info.innerHTML = '<div>Model: ' + d.model + '</div>' +\n"
"                        '<div>Battery: ' + d.battery + '%%' + (d.charging ? ' (Charging)' : '') + '</div>' +\n"
"                        '<div>Photos: ' + d.photos_remaining + ' remaining</div>' +\n"
"                        '<div>Resolution: ' + d.width + 'x' + d.height + '</div>';\n"
"                    info.style.display = 'grid';\n"
"                });\n"
"        }\n"
"\n"
"        function refreshFiles() {\n"
"            fetch('/api/files')\n"
"                .then(r => r.json())\n"
"                .then(d => {\n"
"                    const list = document.getElementById('file-list');\n"
"                    list.innerHTML = '';\n"
"                    d.files.forEach(f => {\n"
"                        const li = document.createElement('li');\n"
"                        li.innerHTML = '<span>' + f.name + ' (' + (f.size/1024).toFixed(1) + ' KB)</span>' +\n"
"                            '<span><button onclick=\"printFile(\\'' + f.name + '\\')\">Print</button>' +\n"
"                            '<button class=\"danger\" onclick=\"deleteFile(\\'' + f.name + '\\')\">Delete</button></span>';\n"
"                        list.appendChild(li);\n"
"                    });\n"
"                });\n"
"        }\n"
"\n"
"        function printFile(name) {\n"
"            document.getElementById('print-status').textContent = 'Printing...';\n"
"            document.getElementById('print-progress').style.display = 'block';\n"
"            document.getElementById('print-bar').style.width = '0%%';\n"
"            fetch('/api/print', {\n"
"                method: 'POST',\n"
"                headers: {'Content-Type': 'application/json'},\n"
"                body: JSON.stringify({filename: name})\n"
"            }).then(r => r.json())\n"
"              .then(d => {\n"
"                  if(d.success) {\n"
"                      pollPrintProgress();\n"
"                  } else {\n"
"                      document.getElementById('print-status').textContent = 'Print failed: ' + d.error;\n"
"                  }\n"
"              });\n"
"        }\n"
"\n"
"        function pollPrintProgress() {\n"
"            fetch('/api/print-status')\n"
"                .then(r => r.json())\n"
"                .then(d => {\n"
"                    document.getElementById('print-bar').style.width = d.percent + '%%';\n"
"                    document.getElementById('print-status').textContent = d.status;\n"
"                    if(d.status !== 'Complete' && d.status !== 'Error') {\n"
"                        setTimeout(pollPrintProgress, 500);\n"
"                    } else {\n"
"                        setTimeout(() => {\n"
"                            document.getElementById('print-progress').style.display = 'none';\n"
"                        }, 2000);\n"
"                    }\n"
"                });\n"
"        }\n"
"\n"
"        function deleteFile(name) {\n"
"            if(confirm('Delete ' + name + '?')) {\n"
"                fetch('/api/files/' + name, {method: 'DELETE'})\n"
"                    .then(() => refreshFiles());\n"
"            }\n"
"        }\n"
"\n"
"        document.getElementById('upload-form').onsubmit = function(e) {\n"
"            e.preventDefault();\n"
"            const file = document.getElementById('file-input').files[0];\n"
"            if(!file) return;\n"
"            const formData = new FormData();\n"
"            formData.append('file', file);\n"
"            document.getElementById('upload-progress').style.display = 'block';\n"
"            const xhr = new XMLHttpRequest();\n"
"            xhr.upload.onprogress = function(e) {\n"
"                if(e.lengthComputable) {\n"
"                    document.getElementById('upload-bar').style.width = (e.loaded/e.total*100) + '%%';\n"
"                }\n"
"            };\n"
"            xhr.onload = function() {\n"
"                document.getElementById('upload-progress').style.display = 'none';\n"
"                refreshFiles();\n"
"            };\n"
"            xhr.open('POST', '/api/upload');\n"
"            xhr.send(formData);\n"
"        };\n"
"\n"
"        refreshFiles();\n"
"        fetch('/api/status').then(r => r.json()).then(d => {\n"
"            if(d.ble_connected) {\n"
"                document.getElementById('ble-status').textContent = 'Connected';\n"
"                document.getElementById('ble-status').className = 'status connected';\n"
"                getPrinterInfo();\n"
"            }\n"
"        });\n"
"    </script>\n"
"</body>\n"
"</html>\n";

// Handler for main page
static esp_err_t root_handler(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, HTML_TEMPLATE, strlen(HTML_TEMPLATE));
    return ESP_OK;
}

// Handler for status API
static esp_err_t api_status_handler(httpd_req_t *req) {
    cJSON *root = cJSON_CreateObject();

    // WiFi status
    wifi_status_t wifi_status = wifi_manager_get_status();
    cJSON_AddBoolToObject(root, "wifi_connected", wifi_status == WIFI_STATUS_CONNECTED);

    char ip[16] = "";
    if (wifi_manager_get_ip(ip) == ESP_OK) {
        cJSON_AddStringToObject(root, "ip", ip);
    }

    // BLE status
    ble_state_t ble_state = ble_scanner_get_state();
    cJSON_AddBoolToObject(root, "ble_connected", ble_state == BLE_STATE_CONNECTED);
    cJSON_AddStringToObject(root, "ble_state",
        ble_state == BLE_STATE_CONNECTED ? "connected" :
        ble_state == BLE_STATE_SCANNING ? "scanning" :
        ble_state == BLE_STATE_CONNECTING ? "connecting" : "disconnected");

    // SPIFFS status
    size_t total, used;
    if (spiffs_manager_get_stats(&total, &used) == ESP_OK) {
        cJSON_AddNumberToObject(root, "storage_total", total);
        cJSON_AddNumberToObject(root, "storage_used", used);
    }

    char *json = cJSON_Print(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, strlen(json));

    free(json);
    cJSON_Delete(root);
    return ESP_OK;
}

// Handler for BLE scan API
static esp_err_t api_scan_handler(httpd_req_t *req) {
    esp_err_t ret = ble_scanner_start_scan(5); // 5 second scan

    cJSON *root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "success", ret == ESP_OK);

    char *json = cJSON_Print(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, strlen(json));

    free(json);
    cJSON_Delete(root);
    return ESP_OK;
}

// Handler for devices list API
static esp_err_t api_devices_handler(httpd_req_t *req) {
    ble_discovered_device_t devices[MAX_DISCOVERED_PRINTERS];
    int count = ble_scanner_get_discovered(devices, MAX_DISCOVERED_PRINTERS);

    cJSON *root = cJSON_CreateObject();
    cJSON *arr = cJSON_CreateArray();

    for (int i = 0; i < count; i++) {
        cJSON *dev = cJSON_CreateObject();
        cJSON_AddStringToObject(dev, "name", devices[i].name);

        char addr_str[18];
        snprintf(addr_str, sizeof(addr_str), "%02x:%02x:%02x:%02x:%02x:%02x",
                 devices[i].address[0], devices[i].address[1], devices[i].address[2],
                 devices[i].address[3], devices[i].address[4], devices[i].address[5]);
        cJSON_AddStringToObject(dev, "address", addr_str);
        cJSON_AddNumberToObject(dev, "rssi", devices[i].rssi);
        cJSON_AddBoolToObject(dev, "is_instax", devices[i].is_instax);

        cJSON_AddItemToArray(arr, dev);
    }

    cJSON_AddItemToObject(root, "devices", arr);

    char *json = cJSON_Print(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, strlen(json));

    free(json);
    cJSON_Delete(root);
    return ESP_OK;
}

// Handler for connect API
static esp_err_t api_connect_handler(httpd_req_t *req) {
    char buf[128];
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "No data");
        return ESP_FAIL;
    }
    buf[ret] = '\0';

    cJSON *root = cJSON_Parse(buf);
    if (root == NULL) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }

    cJSON *addr_json = cJSON_GetObjectItem(root, "address");
    if (addr_json == NULL || !cJSON_IsString(addr_json)) {
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing address");
        return ESP_FAIL;
    }

    // Parse address string "xx:xx:xx:xx:xx:xx"
    uint8_t address[6];
    sscanf(addr_json->valuestring, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx",
           &address[0], &address[1], &address[2],
           &address[3], &address[4], &address[5]);

    cJSON_Delete(root);

    esp_err_t err = ble_scanner_connect(address);

    cJSON *response = cJSON_CreateObject();
    cJSON_AddBoolToObject(response, "success", err == ESP_OK);

    char *json = cJSON_Print(response);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, strlen(json));

    free(json);
    cJSON_Delete(response);
    return ESP_OK;
}

// Handler for disconnect API
static esp_err_t api_disconnect_handler(httpd_req_t *req) {
    ble_scanner_disconnect();

    cJSON *root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "success", true);

    char *json = cJSON_Print(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, strlen(json));

    free(json);
    cJSON_Delete(root);
    return ESP_OK;
}

// Handler for files list API
static esp_err_t api_files_handler(httpd_req_t *req) {
    spiffs_file_info_t files[SPIFFS_MAX_FILES];
    int count = spiffs_manager_list_files(files, SPIFFS_MAX_FILES);

    cJSON *root = cJSON_CreateObject();
    cJSON *arr = cJSON_CreateArray();

    for (int i = 0; i < count; i++) {
        cJSON *file = cJSON_CreateObject();
        cJSON_AddStringToObject(file, "name", files[i].filename);
        cJSON_AddNumberToObject(file, "size", files[i].size);
        cJSON_AddItemToArray(arr, file);
    }

    cJSON_AddItemToObject(root, "files", arr);

    char *json = cJSON_Print(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, strlen(json));

    free(json);
    cJSON_Delete(root);
    return ESP_OK;
}

// Handler for file upload
static esp_err_t api_upload_handler(httpd_req_t *req) {
    ESP_LOGI(TAG, "File upload, content length: %d", req->content_len);

    // Allocate buffer for file data
    size_t buf_size = req->content_len;
    if (buf_size > 120 * 1024) { // Max 120KB for safety
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "File too large");
        return ESP_FAIL;
    }

    uint8_t *buf = malloc(buf_size);
    if (buf == NULL) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
        return ESP_FAIL;
    }

    // Receive file data
    size_t received = 0;
    while (received < buf_size) {
        int ret = httpd_req_recv(req, (char *)buf + received, buf_size - received);
        if (ret <= 0) {
            free(buf);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Receive failed");
            return ESP_FAIL;
        }
        received += ret;
    }

    // TODO: Parse multipart form data to extract filename and actual file content
    // For now, generate a filename based on timestamp
    char filename[32];
    snprintf(filename, sizeof(filename), "image_%lu.jpg", (unsigned long)esp_log_timestamp());

    esp_err_t ret = spiffs_manager_save_file(filename, buf, buf_size);
    free(buf);

    cJSON *root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "success", ret == ESP_OK);
    if (ret == ESP_OK) {
        cJSON_AddStringToObject(root, "filename", filename);
    }

    char *json = cJSON_Print(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, strlen(json));

    free(json);
    cJSON_Delete(root);
    return ESP_OK;
}

esp_err_t web_server_start(void) {
    if (s_server != NULL) {
        return ESP_OK; // Already running
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 16;
    config.stack_size = 8192;

    esp_err_t ret = httpd_start(&s_server, &config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start server: %s", esp_err_to_name(ret));
        return ret;
    }

    // Register URI handlers
    httpd_uri_t root_uri = { .uri = "/", .method = HTTP_GET, .handler = root_handler };
    httpd_uri_t status_uri = { .uri = "/api/status", .method = HTTP_GET, .handler = api_status_handler };
    httpd_uri_t scan_uri = { .uri = "/api/scan", .method = HTTP_POST, .handler = api_scan_handler };
    httpd_uri_t devices_uri = { .uri = "/api/devices", .method = HTTP_GET, .handler = api_devices_handler };
    httpd_uri_t connect_uri = { .uri = "/api/connect", .method = HTTP_POST, .handler = api_connect_handler };
    httpd_uri_t disconnect_uri = { .uri = "/api/disconnect", .method = HTTP_POST, .handler = api_disconnect_handler };
    httpd_uri_t files_uri = { .uri = "/api/files", .method = HTTP_GET, .handler = api_files_handler };
    httpd_uri_t upload_uri = { .uri = "/api/upload", .method = HTTP_POST, .handler = api_upload_handler };

    httpd_register_uri_handler(s_server, &root_uri);
    httpd_register_uri_handler(s_server, &status_uri);
    httpd_register_uri_handler(s_server, &scan_uri);
    httpd_register_uri_handler(s_server, &devices_uri);
    httpd_register_uri_handler(s_server, &connect_uri);
    httpd_register_uri_handler(s_server, &disconnect_uri);
    httpd_register_uri_handler(s_server, &files_uri);
    httpd_register_uri_handler(s_server, &upload_uri);

    ESP_LOGI(TAG, "Web server started");
    return ESP_OK;
}

esp_err_t web_server_stop(void) {
    if (s_server == NULL) {
        return ESP_OK;
    }

    esp_err_t ret = httpd_stop(s_server);
    s_server = NULL;
    return ret;
}

bool web_server_is_running(void) {
    return s_server != NULL;
}
