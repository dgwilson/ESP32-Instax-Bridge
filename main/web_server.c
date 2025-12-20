/**
 * @file web_server.c
 * @brief HTTP web server for Instax bridge control interface
 */

#include "web_server.h"
#include "wifi_manager.h"
#include "ble_peripheral.h"
#include "printer_emulator.h"
#include "spiffs_manager.h"
#include "instax_protocol.h"
#include <string.h>
#include <errno.h>
#include "esp_http_server.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "cJSON.h"

static const char *TAG = "web_server";
static httpd_handle_t s_server = NULL;

// HTML template for the main page
static const char *HTML_TEMPLATE =
"<!DOCTYPE html>\n"
"<html>\n"
"<head>\n"
"    <title>ESP32 Instax Printer Emulator</title>\n"
"    <meta charset=\"UTF-8\">\n"
"    <meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n"
"    <style>\n"
"        body { font-family: Arial, sans-serif; max-width: 800px; margin: 0 auto; padding: 20px; }\n"
"        h1 { color: #333; }\n"
"        .offline-banner { display: none; background: #f44336; color: white; padding: 15px; margin: -20px -20px 20px -20px; text-align: center; font-weight: bold; font-size: 1.1em; border-bottom: 3px solid #d32f2f; }\n"
"        .offline-banner.visible { display: block; }\n"
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
"        .dis-input { width: 150px; font-size: 14px; }\n"
"        .countdown-overlay { display: none; position: fixed; top: 0; left: 0; width: 100%%; height: 100%%; background: rgba(0,0,0,0.8); z-index: 9999; }\n"
"        .countdown-overlay.visible { display: flex; align-items: center; justify-content: center; }\n"
"        .countdown-content { background: white; padding: 40px; border-radius: 10px; text-align: center; }\n"
"        .countdown-number { font-size: 72px; font-weight: bold; color: #4CAF50; margin: 20px 0; }\n"
"        .countdown-message { font-size: 18px; color: #666; margin-bottom: 10px; }\n"
"    </style>\n"
"</head>\n"
"<body>\n"
"    <div id=\"offline-banner\" class=\"offline-banner\">\n"
"        ESP32 OFFLINE - Device not responding. Check power and connections.\n"
"    </div>\n"
"\n"
"    <div id=\"countdown-overlay\" class=\"countdown-overlay\">\n"
"        <div class=\"countdown-content\">\n"
"            <div class=\"countdown-message\">Rebooting ESP32 to apply new GATT services...</div>\n"
"            <div class=\"countdown-number\" id=\"countdown-number\">10</div>\n"
"            <div class=\"countdown-message\">Page will reload automatically</div>\n"
"        </div>\n"
"    </div>\n"
"\n"
"    <h1>ESP32 Instax Printer Emulator</h1>\n"
"\n"
"    <div class=\"section\">\n"
"        <h2>System Information</h2>\n"
"        <div class=\"printer-info\">\n"
"            <div>Uptime: <span id=\"uptime\">Loading...</span></div>\n"
"            <div>Reset Reason: <span id=\"reset-reason\">Loading...</span></div>\n"
"            <div>IP Address: <span id=\"ip-address\">Not connected</span></div>\n"
"            <div>mDNS: <a href=\"http://instax-simulator.local\">instax-simulator.local</a></div>\n"
"        </div>\n"
"        <p style=\"font-size: 0.9em; color: #666; margin-top: 10px;\"><strong>Tip:</strong> Bookmark <code>http://instax-simulator.local</code> to access this page without needing the IP address. The IP is also shown in the BLE device name when scanning from iOS.</p>\n"
"    </div>\n"
"\n"
"    <div class=\"section\">\n"
"        <h2>BLE Status & Failures</h2>\n"
"        <div class=\"printer-info\">\n"
"            <div>BLE Resets: <span id=\"ble-resets\">0</span></div>\n"
"            <div>Last Reset: <span id=\"ble-last-reset\">None</span></div>\n"
"            <div>Disconnects: <span id=\"ble-disconnects\">0</span></div>\n"
"            <div>Last Disconnect: <span id=\"ble-last-disconnect\">None</span></div>\n"
"        </div>\n"
"    </div>\n"
"\n"
"    <div class=\"section\">\n"
"        <h2>BLE Advertising</h2>\n"
"        <div id=\"ble-advertising-status\" class=\"status\">Checking...</div>\n"
"        <button onclick=\"startBLE()\">Start Advertising</button>\n"
"        <button onclick=\"stopBLE()\" class=\"danger\">Stop Advertising</button>\n"
"        <button onclick=\"dumpConfig()\" style=\"background:#2196F3;\">📋 Dump Config to Monitor</button>\n"
"        <div id=\"printer-info\" class=\"printer-info\" style=\"margin-top:15px;\"></div>\n"
"        <div id=\"device-info\" class=\"printer-info\" style=\"margin-top:15px;display:none;\">\n"
"            <h3 style=\"grid-column: 1 / -1; margin:0 0 10px 0; color:#666;\">Device Information Service (BLE)</h3>\n"
"        </div>\n"
"    </div>\n"
"\n"
"    <div class=\"section\">\n"
"        <h2>Printer Settings</h2>\n"
"\n"
"        <!-- Printer Model Reference Table -->\n"
"        <div style=\"margin-bottom:20px; padding:15px; background:#f8f9fa; border-radius:5px;\">\n"
"            <h3 style=\"margin:0 0 10px 0; color:#666;\">Printer Model Reference</h3>\n"
"            <table style=\"width:100%; border-collapse:collapse; font-size:13px;\">\n"
"                <thead>\n"
"                    <tr style=\"background:#e9ecef;\">\n"
"                        <th style=\"padding:8px; text-align:left; border:1px solid #dee2e6;\">Model</th>\n"
"                        <th style=\"padding:8px; text-align:left; border:1px solid #dee2e6;\">BLE Model #</th>\n"
"                        <th style=\"padding:8px; text-align:left; border:1px solid #dee2e6;\">Resolution</th>\n"
"                        <th style=\"padding:8px; text-align:left; border:1px solid #dee2e6;\">Print Size</th>\n"
"                        <th style=\"padding:8px; text-align:left; border:1px solid #dee2e6;\">Film Type</th>\n"
"                    </tr>\n"
"                </thead>\n"
"                <tbody>\n"
"                    <tr>\n"
"                        <td style=\"padding:8px; border:1px solid #dee2e6;\"><strong>Mini Link</strong></td>\n"
"                        <td style=\"padding:8px; border:1px solid #dee2e6; font-family:monospace;\">FI033</td>\n"
"                        <td style=\"padding:8px; border:1px solid #dee2e6; font-family:monospace;\">600x800</td>\n"
"                        <td style=\"padding:8px; border:1px solid #dee2e6;\">62mm × 46mm</td>\n"
"                        <td style=\"padding:8px; border:1px solid #dee2e6;\">Instax Mini</td>\n"
"                    </tr>\n"
"                    <tr>\n"
"                        <td style=\"padding:8px; border:1px solid #dee2e6;\"><strong>Square Link</strong></td>\n"
"                        <td style=\"padding:8px; border:1px solid #dee2e6; font-family:monospace;\">FI017</td>\n"
"                        <td style=\"padding:8px; border:1px solid #dee2e6; font-family:monospace;\">800x800</td>\n"
"                        <td style=\"padding:8px; border:1px solid #dee2e6;\">62mm × 62mm</td>\n"
"                        <td style=\"padding:8px; border:1px solid #dee2e6;\">Instax Square</td>\n"
"                    </tr>\n"
"                    <tr>\n"
"                        <td style=\"padding:8px; border:1px solid #dee2e6;\"><strong>Wide Link</strong></td>\n"
"                        <td style=\"padding:8px; border:1px solid #dee2e6; font-family:monospace;\">FI022</td>\n"
"                        <td style=\"padding:8px; border:1px solid #dee2e6; font-family:monospace;\">1260x840</td>\n"
"                        <td style=\"padding:8px; border:1px solid #dee2e6;\">99mm × 62mm</td>\n"
"                        <td style=\"padding:8px; border:1px solid #dee2e6;\">Instax Wide</td>\n"
"                    </tr>\n"
"                </tbody>\n"
"            </table>\n"
"            <p style=\"margin:10px 0 0 0; font-size:12px; color:#666;\"><strong>Note:</strong> Official apps filter by BLE Model Number. Set the correct model to ensure your app can discover the printer.</p>\n"
"        </div>\n"
"\n"
"        <div style=\"margin-bottom:15px;\">\n"
"            <label>Model: </label>\n"
"            <select id=\"model-select\" onchange=\"setModel(this.value)\">\n"
"                <option value=\"mini\">Mini (600x800)</option>\n"
"                <option value=\"square\">Square (800x800)</option>\n"
"                <option value=\"wide\">Wide (1260x840)</option>\n"
"            </select>\n"
"        </div>\n"
"        <div style=\"margin-bottom:15px;\">\n"
"            <label>Device Name: </label>\n"
"            <input type=\"text\" id=\"device-name-input\" value=\"INSTAX-50196563\" maxlength=\"32\" style=\"width:200px;\">\n"
"            <button onclick=\"setDeviceName()\">Apply</button>\n"
"            <div style=\"margin-top:5px; font-size:12px; color:#666;\">\n"
"                Quick presets: \n"
"                <button onclick=\"document.getElementById('device-name-input').value='INSTAX-50196563'; setDeviceName();\" style=\"font-size:11px;\">Numeric</button>\n"
"                <button onclick=\"document.getElementById('device-name-input').value='INSTAX-Simulator'; setDeviceName();\" style=\"font-size:11px;\">Simulator</button>\n"
"            </div>\n"
"        </div>\n"
"        <div style=\"margin-bottom:15px;\">\n"
"            <label>Battery: <span id=\"battery-value\">85</span>%</label><br>\n"
"            <input type=\"range\" id=\"battery-slider\" min=\"0\" max=\"100\" value=\"85\" \n"
"                   oninput=\"document.getElementById('battery-value').textContent=this.value\" \n"
"                   onchange=\"setBattery(this.value)\" style=\"width:200px;\">\n"
"        </div>\n"
"        <div style=\"margin-bottom:15px;\">\n"
"            <label>Prints Remaining: </label>\n"
"            <button onclick=\"adjustPrints(-1)\">-</button>\n"
"            <input type=\"number\" id=\"prints-input\" value=\"10\" min=\"0\" max=\"15\" style=\"width:60px;\">\n"
"            <button onclick=\"adjustPrints(1)\">+</button>\n"
"            <button onclick=\"setPrints(document.getElementById('prints-input').value)\">Set</button>\n"
"            <p style=\"font-size:0.85em;color:#666;margin:5px 0 0 0;\">Maximum 15 (protocol uses 4-bit field)</p>\n"
"        </div>\n"
"        <div style=\"margin-bottom:15px;\">\n"
"            <label>\n"
"                <input type=\"checkbox\" id=\"charging-checkbox\" onchange=\"setCharging(this.checked)\">\n"
"                Charging\n"
"            </label>\n"
"        </div>\n"
"        <div style=\"margin-bottom:15px;\">\n"
"            <label>\n"
"                <input type=\"checkbox\" id=\"suspend-decrement-checkbox\" onchange=\"setSuspendDecrement(this.checked)\">\n"
"                Suspend Print Count Decrement (Unlimited Testing)\n"
"            </label>\n"
"            <p style=\"font-size:0.85em;color:#666;margin:5px 0 0 0;\">When enabled, print count won't decrease after printing</p>\n"
"        </div>\n"
"        <div style=\"margin-top:20px;margin-bottom:15px;border-top:2px solid #FF9800;padding-top:15px;\">\n"
"            <h3 style=\"margin-bottom:10px;color:#FF9800;\">🔐 BLE Security & Bonding</h3>\n"
"            <div style=\"margin-bottom:15px;padding:12px;background:#fff3e0;border-left:4px solid #FF9800;border-radius:4px;\">\n"
"                <p style=\"margin:0 0 8px 0;font-size:0.9em;\"><strong>What is Bonding?</strong></p>\n"
"                <p style=\"margin:0 0 8px 0;font-size:0.85em;color:#666;\">\n"
"                    Bonding stores encryption keys so your iPhone \"remembers\" the printer in Bluetooth Settings.\n"
"                    Real INSTAX printers use bonding for persistent pairing.\n"
"                </p>\n"
"                <p style=\"margin:0 0 8px 0;font-size:0.9em;\"><strong>🛠️ Development Mode (Bonding OFF):</strong></p>\n"
"                <p style=\"margin:0 0 8px 0;font-size:0.85em;color:#666;\">\n"
"                    • Use this when <strong>testing/developing</strong><br>\n"
"                    • <strong>Why?</strong> Every time you reflash the ESP32, its bonding database is cleared<br>\n"
"                    • <strong>Problem:</strong> iOS still has old keys → connection fails with error 531<br>\n"
"                    • <strong>Solution:</strong> Disable bonding = no pairing required = instant reconnect<br>\n"
"                    • <strong>Drawback:</strong> Printer won't appear in iPhone Bluetooth Settings list\n"
"                </p>\n"
"                <p style=\"margin:0;font-size:0.9em;\"><strong>🔒 Real Printer Mode (Bonding ON):</strong></p>\n"
"                <p style=\"margin:0;font-size:0.85em;color:#666;\">\n"
"                    • Use this to <strong>test real printer behavior</strong><br>\n"
"                    • Printer appears in iPhone Bluetooth Settings<br>\n"
"                    • <strong>Note:</strong> If you reflash ESP32, you must \"Forget This Device\" on iPhone first\n"
"                </p>\n"
"            </div>\n"
"            <div style=\"margin-bottom:15px;\">\n"
"                <label>\n"
"                    <input type=\"checkbox\" id=\"bonding-checkbox\" onchange=\"setBonding(this.checked)\">\n"
"                    <strong>Enable BLE Bonding</strong> (requires ESP32 restart)\n"
"                </label>\n"
"                <p style=\"font-size:0.85em;color:#666;margin:5px 0 0 0;\">Current mode: <span id=\"bonding-status\" style=\"font-weight:bold;\">Loading...</span></p>\n"
"            </div>\n"
"            <div style=\"margin-bottom:15px;\">\n"
"                <button onclick=\"clearBonds()\" style=\"background:#f44336;color:white;padding:8px 16px;border:none;border-radius:4px;cursor:pointer;\">\n"
"                    Clear Bonding Database & Restart\n"
"                </button>\n"
"                <p style=\"font-size:0.85em;color:#666;margin:5px 0 0 0;\">\n"
"                    Use this if connections fail with \"error 531\" or if iPhone can't forget the device.\n"
"                    <br><strong>Important:</strong> Also go to iPhone Settings → Bluetooth → Forget This Device after clicking this button.\n"
"                </p>\n"
"            </div>\n"
"        </div>\n"
"        <div style=\"margin-top:20px;margin-bottom:15px;border-top:2px solid #4CAF50;padding-top:15px;\">\n"
"            <h3 style=\"margin-bottom:10px;color:#4CAF50;\">Newly Discovered Features (Dec 2025)</h3>\n"
"            <div style=\"margin-bottom:15px;padding:10px;background:#e8f5e9;border-radius:4px;\">\n"
"                <strong>Protocol Status:</strong> Auto-sleep timeout: <span id=\"auto-sleep-display\" style=\"color:#2e7d32;font-weight:bold;\">5 minutes</span><br>\n"
"                <strong>Print Mode:</strong> <span id=\"print-mode-display\" style=\"color:#2e7d32;font-weight:bold;\">Rich (0x00)</span>\n"
"                <p style=\"font-size:0.85em;color:#666;margin:8px 0 0 0;\">These values are set by the app (Moments Print) when connecting/printing. Changes are logged in real-time.</p>\n"
"            </div>\n"
"        </div>\n"
"        <div style=\"margin-top:20px;margin-bottom:15px;border-top:2px solid #f44336;padding-top:15px;\">\n"
"            <h3 style=\"margin-bottom:10px;color:#f44336;\">Error Simulation</h3>\n"
"            <div style=\"margin-bottom:10px;\">\n"
"                <label>\n"
"                    <input type=\"checkbox\" id=\"cover-open-checkbox\" onchange=\"setCoverOpen(this.checked)\">\n"
"                    Cover Open (Error 179)\n"
"                </label>\n"
"            </div>\n"
"            <div style=\"margin-bottom:10px;\">\n"
"                <label>\n"
"                    <input type=\"checkbox\" id=\"printer-busy-checkbox\" onchange=\"setPrinterBusy(this.checked)\">\n"
"                    Printer Busy (Error 181)\n"
"                </label>\n"
"            </div>\n"
"            <div style=\"margin-bottom:10px;color:#666;font-size:0.9em;\">\n"
"                Note: Error 178 (No Film) = Set prints to 0<br>\n"
"                Error 180 (Battery Low) = Set battery below 20%\n"
"            </div>\n"
"        </div>\n"
"        <div style=\"margin-top:20px;margin-bottom:15px;\">\n"
"            <h3 style=\"margin-bottom:10px;\">Accelerometer (Link 3)</h3>\n"
"            <div style=\"margin-bottom:10px;\">\n"
"                <label>X-Axis (tilt left/right): <span id=\"accel-x-value\">0</span></label><br>\n"
"                <input type=\"range\" id=\"accel-x-slider\" min=\"-1000\" max=\"1000\" value=\"0\" \n"
"                       oninput=\"document.getElementById('accel-x-value').textContent=this.value\" \n"
"                       onchange=\"setAccelerometer()\" style=\"width:200px;\">\n"
"            </div>\n"
"            <div style=\"margin-bottom:10px;\">\n"
"                <label>Y-Axis (tilt forward/back): <span id=\"accel-y-value\">0</span></label><br>\n"
"                <input type=\"range\" id=\"accel-y-slider\" min=\"-1000\" max=\"1000\" value=\"0\" \n"
"                       oninput=\"document.getElementById('accel-y-value').textContent=this.value\" \n"
"                       onchange=\"setAccelerometer()\" style=\"width:200px;\">\n"
"            </div>\n"
"            <div style=\"margin-bottom:10px;\">\n"
"                <label>Z-Axis (rotation): <span id=\"accel-z-value\">0</span></label><br>\n"
"                <input type=\"range\" id=\"accel-z-slider\" min=\"-1000\" max=\"1000\" value=\"0\" \n"
"                       oninput=\"document.getElementById('accel-z-value').textContent=this.value\" \n"
"                       onchange=\"setAccelerometer()\" style=\"width:200px;\">\n"
"            </div>\n"
"            <div style=\"margin-bottom:10px;\">\n"
"                <label>Orientation: </label>\n"
"                <input type=\"number\" id=\"orientation-input\" min=\"0\" max=\"255\" value=\"0\" \n"
"                       onchange=\"setAccelerometer()\" style=\"width:60px;\">\n"
"            </div>\n"
"            <button onclick=\"resetAccelerometer()\">Reset to Neutral</button>\n"
"        </div>\n"
"        <div style=\"margin-top:20px;margin-bottom:15px;border-top:1px solid #ccc;padding-top:15px;\">\n"
"            <h3 style=\"margin-bottom:10px;\">Camera Control (Link 3)</h3>\n"
"            <p style=\"font-size:0.9em;color:#666;margin-bottom:10px;\">Simulate pressing the power button to trigger camera shutter</p>\n"
"            <button onclick=\"triggerShutter()\" style=\"padding:10px 20px;font-size:1.1em;\">Press Shutter</button>\n"
"            <div id=\"shutter-status\" style=\"margin-top:10px;font-style:italic;color:#666;\"></div>\n"
"        </div>\n"
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
"        <h2>Received Prints</h2>\n"
"        <button onclick=\"refreshFiles()\">Refresh</button>\n"
"        <button onclick=\"deleteAllFiles()\" style=\"background-color: #dc3545;\">Delete All</button>\n"
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
"    <div class=\"section\">\n"
"        <h2>Documentation</h2>\n"
"        <p style=\"color:#666;margin-bottom:15px;\">Project documentation and guides:</p>\n"
"        <div style=\"display:flex;flex-direction:column;gap:10px;\">\n"
"            <a href=\"/docs/protocol\" target=\"_blank\" style=\"padding:12px;background:#fff;border:1px solid #ddd;border-radius:4px;text-decoration:none;color:#333;display:block;\">\n"
"                <strong style=\"color:#4CAF50;\">INSTAX Protocol Documentation</strong><br>\n"
"                <span style=\"font-size:0.9em;color:#666;\">Complete BLE protocol specification with packet formats and sequences</span>\n"
"            </a>\n"
"            <a href=\"/docs/install\" target=\"_blank\" style=\"padding:12px;background:#fff;border:1px solid #ddd;border-radius:4px;text-decoration:none;color:#333;display:block;\">\n"
"                <strong style=\"color:#4CAF50;\">ESP-IDF Installation Guide</strong><br>\n"
"                <span style=\"font-size:0.9em;color:#666;\">Step-by-step instructions for installing ESP-IDF development environment</span>\n"
"            </a>\n"
"            <a href=\"/docs/readme\" target=\"_blank\" style=\"padding:12px;background:#fff;border:1px solid #ddd;border-radius:4px;text-decoration:none;color:#333;display:block;\">\n"
"                <strong style=\"color:#4CAF50;\">Project README</strong><br>\n"
"                <span style=\"font-size:0.9em;color:#666;\">Project overview, features, setup instructions, and testing guide</span>\n"
"            </a>\n"
"        </div>\n"
"    </div>\n"
"\n"
"    <script>\n"
"        function startBLE() {\n"
"            document.getElementById('ble-advertising-status').textContent = 'Starting...';\n"
"            document.getElementById('ble-advertising-status').className = 'status scanning';\n"
"            fetch('/api/ble-start', {method: 'POST'})\n"
"                .then(r => r.json())\n"
"                .then(d => {\n"
"                    if(d.success) {\n"
"                        document.getElementById('ble-advertising-status').textContent = 'Advertising';\n"
"                        document.getElementById('ble-advertising-status').className = 'status connected';\n"
"                        getPrinterInfo();\n"
"                    } else {\n"
"                        document.getElementById('ble-advertising-status').textContent = 'Failed to start';\n"
"                        document.getElementById('ble-advertising-status').className = 'status disconnected';\n"
"                    }\n"
"                });\n"
"        }\n"
"\n"
"        function stopBLE() {\n"
"            fetch('/api/ble-stop', {method: 'POST'})\n"
"                .then(r => r.json())\n"
"                .then(d => {\n"
"                    if(d.success) {\n"
"                        document.getElementById('ble-advertising-status').textContent = 'Stopped';\n"
"                        document.getElementById('ble-advertising-status').className = 'status disconnected';\n"
"                    }\n"
"                });\n"
"        }\n"
"\n"
"        function dumpConfig() {\n"
"            fetch('/api/dump-config', {method: 'POST'})\n"
"                .then(r => r.json())\n"
"                .then(d => {\n"
"                    if(d.success) {\n"
"                        alert('✅ Configuration dumped to serial monitor!\\n\\nCheck your serial monitor output to see complete configuration details.');\n"
"                    } else {\n"
"                        alert('❌ Failed to dump configuration');\n"
"                    }\n"
"                })\n"
"                .catch(e => alert('❌ Error: ' + e));\n"
"        }\n"
"\n"
"        function getPrinterInfo() {\n"
"            fetch('/api/printer-info')\n"
"                .then(r => r.json())\n"
"                .then(d => {\n"
"                    const info = document.getElementById('printer-info');\n"
"                    info.innerHTML = '<div><strong>Device:</strong> ' + d.device_name + '</div>' +\n"
"                        '<div>Model: ' + d.model + '</div>' +\n"
"                        '<div>Battery: ' + d.battery + '%' + (d.charging ? ' (Charging)' : '') + '</div>' +\n"
"                        '<div>Photos: ' + d.photos_remaining + ' remaining</div>' +\n"
"                        '<div>Resolution: ' + d.width + 'x' + d.height + '</div>' +\n"
"                        '<div>Lifetime: ' + d.lifetime_prints + ' prints</div>' +\n"
"                        '<div><strong>BLE MAC:</strong> <code>' + (d.ble_mac || 'Unknown') + '</code></div>';\n"
"                    info.style.display = 'grid';\n"
"\n"
"                    // Update UI controls to match current state\n"
"                    document.getElementById('model-select').value = d.model;\n"
"                    // Only update device name if user isn't currently editing it\n"
"                    const deviceNameInput = document.getElementById('device-name-input');\n"
"                    if (document.activeElement !== deviceNameInput) {\n"
"                        deviceNameInput.value = d.device_name;\n"
"                    }\n"
"                    document.getElementById('battery-slider').value = d.battery;\n"
"                    document.getElementById('battery-value').textContent = d.battery;\n"
"                    document.getElementById('prints-input').value = d.photos_remaining;\n"
"                    document.getElementById('charging-checkbox').checked = d.charging;\n"
"                    document.getElementById('suspend-decrement-checkbox').checked = d.suspend_decrement || false;\n"
"\n"
"                    // Update accelerometer controls if present\n"
"                    if (d.accelerometer) {\n"
"                        document.getElementById('accel-x-slider').value = d.accelerometer.x;\n"
"                        document.getElementById('accel-x-value').textContent = d.accelerometer.x;\n"
"                        document.getElementById('accel-y-slider').value = d.accelerometer.y;\n"
"                        document.getElementById('accel-y-value').textContent = d.accelerometer.y;\n"
"                        document.getElementById('accel-z-slider').value = d.accelerometer.z;\n"
"                        document.getElementById('accel-z-value').textContent = d.accelerometer.z;\n"
"                        document.getElementById('orientation-input').value = d.accelerometer.orientation;\n"
"                    }\n"
"\n"
"                    // Update error simulation states\n"
"                    document.getElementById('cover-open-checkbox').checked = d.cover_open || false;\n"
"                    document.getElementById('printer-busy-checkbox').checked = d.printer_busy || false;\n"
"\n"
"                    // Update bonding status\n"
"                    if (d.bonding_enabled !== undefined) {\n"
"                        document.getElementById('bonding-checkbox').checked = d.bonding_enabled;\n"
"                        const statusText = d.bonding_enabled ? \n"
"                            '🔒 Enabled (Real Printer Mode)' : \n"
"                            '🛠️ Disabled (Development Mode)';\n"
"                        const statusColor = d.bonding_enabled ? '#d32f2f' : '#FF9800';\n"
"                        document.getElementById('bonding-status').textContent = statusText;\n"
"                        document.getElementById('bonding-status').style.color = statusColor;\n"
"                    }\n"
"\n"
"                    // Update newly discovered protocol features display (Dec 2025)\n"
"                    if (d.auto_sleep_timeout !== undefined) {\n"
"                        const timeoutText = d.auto_sleep_timeout === 0 ? 'Never' : d.auto_sleep_timeout + ' minutes';\n"
"                        document.getElementById('auto-sleep-display').textContent = timeoutText;\n"
"                    }\n"
"                    if (d.print_mode !== undefined) {\n"
"                        const modeText = d.print_mode === 0x00 ? 'Rich (0x00)' : \n"
"                                        d.print_mode === 0x03 ? 'Natural (0x03)' : \n"
"                                        'Unknown (0x' + d.print_mode.toString(16).toUpperCase() + ')';\n"
"                        document.getElementById('print-mode-display').textContent = modeText;\n"
"                    }\n"
"\n"
"                    // Update Device Information Service (DIS) display with editable fields\n"
"                    if (d.device_info) {\n"
"                        const deviceInfo = document.getElementById('device-info');\n"
"                        // Only create HTML structure if it doesn't exist yet\n"
"                        if (!document.getElementById('dis-model-number')) {\n"
"                            deviceInfo.innerHTML = '<h3 style=\"grid-column: 1 / -1; margin:0 0 10px 0; color:#666;\">Device Information Service (BLE GATT)</h3>' +\n"
"                                '<div><strong>Model Number:</strong><br><input type=\"text\" id=\"dis-model-number\" class=\"dis-input\"></div>' +\n"
"                                '<div><strong>Serial Number:</strong><br><input type=\"text\" id=\"dis-serial-number\" class=\"dis-input\"></div>' +\n"
"                                '<div><strong>Firmware Revision:</strong><br><input type=\"text\" id=\"dis-firmware\" class=\"dis-input\"></div>' +\n"
"                                '<div><strong>Hardware Revision:</strong><br><input type=\"text\" id=\"dis-hardware\" class=\"dis-input\"></div>' +\n"
"                                '<div><strong>Software Revision:</strong><br><input type=\"text\" id=\"dis-software\" class=\"dis-input\"></div>' +\n"
"                                '<div><strong>Manufacturer Name:</strong><br><input type=\"text\" id=\"dis-manufacturer\" class=\"dis-input\"></div>' +\n"

"                                '<div style=\"grid-column: 1 / -1;\"><button onclick=\"saveDIS()\">Save DIS Values</button><button onclick=\"resetDISDefaults()\">Reset to Model Defaults</button></div>';\n"
"                        }\n"
"                        // Update values only if fields don't have focus\n"
"                        const modelInput = document.getElementById('dis-model-number');\n"
"                        if (document.activeElement !== modelInput) modelInput.value = d.device_info.model_number;\n"
"                        const serialInput = document.getElementById('dis-serial-number');\n"
"                        if (document.activeElement !== serialInput) serialInput.value = d.device_info.serial_number;\n"
"                        const firmwareInput = document.getElementById('dis-firmware');\n"
"                        if (document.activeElement !== firmwareInput) firmwareInput.value = d.device_info.firmware_revision;\n"
"                        const hardwareInput = document.getElementById('dis-hardware');\n"
"                        if (document.activeElement !== hardwareInput) hardwareInput.value = d.device_info.hardware_revision;\n"
"                        const softwareInput = document.getElementById('dis-software');\n"
"                        if (document.activeElement !== softwareInput) softwareInput.value = d.device_info.software_revision;\n"
"                        const manufacturerInput = document.getElementById('dis-manufacturer');\n"
"                        if (document.activeElement !== manufacturerInput) manufacturerInput.value = d.device_info.manufacturer_name;\n"
"                        deviceInfo.style.display = 'grid';\n"
"                    }\n"
"                });\n"
"        }\n"
"\n"
"        function setModel(model) {\n"
"            fetch('/api/set-model', {\n"
"                method: 'POST',\n"
"                headers: {'Content-Type': 'application/json'},\n"
"                body: JSON.stringify({model: model})\n"
"            }).then(r => r.json())\n"
"              .then(d => {\n"
"                  if(d.success) {\n"
"                      console.log('Model updated to: ' + model);\n"
"                      // Show countdown and reboot to apply new GATT services\n"
"                      startRebootCountdown();\n"
"                  }\n"
"              });\n"
"        }\n"
"\n"
"        function startRebootCountdown() {\n"
"            const overlay = document.getElementById('countdown-overlay');\n"
"            const numberEl = document.getElementById('countdown-number');\n"
"            overlay.classList.add('visible');\n"
"            \n"
"            let count = 10;\n"
"            numberEl.textContent = count;\n"
"            \n"
"            const interval = setInterval(() => {\n"
"                count--;\n"
"                if (count > 0) {\n"
"                    numberEl.textContent = count;\n"
"                } else {\n"
"                    clearInterval(interval);\n"
"                    numberEl.textContent = 'Rebooting...';\n"
"                    // Trigger ESP32 reboot\n"
"                    fetch('/api/reboot', {method: 'POST'})\n"
"                        .catch(() => {})  // Ignore errors as device is rebooting\n"
"                        .finally(() => {\n"
"                            // Wait 5 seconds then reload page\n"
"                            setTimeout(() => {\n"
"                                window.location.reload();\n"
"                            }, 5000);\n"
"                        });\n"
"                }\n"
"            }, 1000);\n"
"        }\n"
"\n"
"        function setBattery(percentage) {\n"
"            fetch('/api/set-battery', {\n"
"                method: 'POST',\n"
"                headers: {'Content-Type': 'application/json'},\n"
"                body: JSON.stringify({percentage: parseInt(percentage)})\n"
"            }).then(r => r.json())\n"
"              .then(d => {\n"
"                  if(d.success) {\n"
"                      console.log('Battery updated to: ' + percentage + '%%');\n"
"                      getPrinterInfo();\n"
"                  }\n"
"              });\n"
"        }\n"
"\n"
"        function setDeviceName() {\n"
"            const name = document.getElementById('device-name-input').value.trim();\n"
"            if(!name) {\n"
"                alert('Device name cannot be empty');\n"
"                return;\n"
"            }\n"
"            if(name.length > 32) {\n"
"                alert('Device name too long (max 32 characters)');\n"
"                return;\n"
"            }\n"
"            fetch('/api/set-name', {\n"
"                method: 'POST',\n"
"                headers: {'Content-Type': 'application/json'},\n"
"                body: JSON.stringify({name: name})\n"
"            }).then(r => r.json())\n"
"              .then(d => {\n"
"                  if(d.success) {\n"
"                      console.log('Device name updated to: ' + name);\n"
"                      alert('Device name changed to: ' + name + '\\n\\nBLE advertising has been restarted.');\n"
"                      getPrinterInfo();\n"
"                  } else {\n"
"                      alert('Failed to update device name');\n"
"                  }\n"
"              });\n"
"        }\n"
"\n"
"        function setPrints(count) {\n"
"            // Cap at 15 (protocol limitation - 4-bit field)\n"
"            const cappedCount = Math.max(0, Math.min(15, parseInt(count)));\n"
"            fetch('/api/set-prints', {\n"
"                method: 'POST',\n"
"                headers: {'Content-Type': 'application/json'},\n"
"                body: JSON.stringify({count: cappedCount})\n"
"            }).then(r => r.json())\n"
"              .then(d => {\n"
"                  if(d.success) {\n"
"                      console.log('Prints remaining updated to: ' + cappedCount);\n"
"                      getPrinterInfo();\n"
"                  }\n"
"              });\n"
"        }\n"
"\n"
"        function adjustPrints(delta) {\n"
"            const input = document.getElementById('prints-input');\n"
"            const newValue = Math.max(0, Math.min(15, parseInt(input.value) + delta));\n"
"            input.value = newValue;\n"
"            setPrints(newValue);\n"
"        }\n"
"\n"
"        function setCharging(is_charging) {\n"
"            fetch('/api/set-charging', {\n"
"                method: 'POST',\n"
"                headers: {'Content-Type': 'application/json'},\n"
"                body: JSON.stringify({charging: is_charging})\n"
"            }).then(r => r.json())\n"
"              .then(d => {\n"
"                  if(d.success) {\n"
"                      console.log('Charging status updated to: ' + is_charging);\n"
"                      getPrinterInfo();\n"
"                  }\n"
"              });\n"
"        }\n"
"\n"
"        function setSuspendDecrement(suspend) {\n"
"            fetch('/api/set-suspend-decrement', {\n"
"                method: 'POST',\n"
"                headers: {'Content-Type': 'application/json'},\n"
"                body: JSON.stringify({suspend: suspend})\n"
"            }).then(r => r.json())\n"
"              .then(d => {\n"
"                  if(d.success) {\n"
"                      console.log('Suspend decrement updated to: ' + suspend);\n"
"                      getPrinterInfo();\n"
"                  }\n"
"              });\n"
"        }\n"
"\n"
"        function setBonding(enabled) {\n"
"            fetch('/api/set-bonding', {\n"
"                method: 'POST',\n"
"                headers: {'Content-Type': 'application/json'},\n"
"                body: JSON.stringify({enabled: enabled})\n"
"            }).then(r => r.json())\n"
"              .then(d => {\n"
"                  if(d.success) {\n"
"                      console.log('Bonding setting saved: ' + enabled);\n"
"                      alert('Bonding ' + (enabled ? 'ENABLED' : 'DISABLED') + '\\n\\nESP32 will restart now to apply changes.\\n\\n' + \n"
"                            (enabled ? '⚠️ Important: If you reflash the ESP32 later, you must \"Forget This Device\" on iPhone first!' : \n"
"                                       '✅ Development mode active. Reconnect without needing to forget device.'));\n"
"                      // ESP32 will restart automatically\n"
"                      setTimeout(() => { window.location.reload(); }, 3000);\n"
"                  } else {\n"
"                      alert('Failed to update bonding setting');\n"
"                  }\n"
"              });\n"
"        }\n"
"\n"
"        function clearBonds() {\n"
"            if(!confirm('This will:\\n' +\n"
"                        '1. Clear all bonding keys from ESP32\\n' +\n"
"                        '2. Restart the ESP32\\n' +\n"
"                        '3. You MUST also \"Forget This Device\" on iPhone\\n\\n' +\n"
"                        'Continue?')) {\n"
"                return;\n"
"            }\n"
"            fetch('/api/clear-bonds', {\n"
"                method: 'POST',\n"
"                headers: {'Content-Type': 'application/json'},\n"
"                body: JSON.stringify({})\n"
"            }).then(r => r.json())\n"
"              .then(d => {\n"
"                  if(d.success) {\n"
"                      alert('Bonding database cleared!\\n\\nESP32 will restart now.\\n\\n⚠️ Go to iPhone Settings → Bluetooth → \"Forget This Device\" NOW!');\n"
"                      setTimeout(() => { window.location.reload(); }, 3000);\n"
"                  } else {\n"
"                      alert('Failed to clear bonding database');\n"
"                  }\n"
"              });\n"
"        }\n"
"\n"
"        function setCoverOpen(is_open) {\n"
"            fetch('/api/set-cover-open', {\n"
"                method: 'POST',\n"
"                headers: {'Content-Type': 'application/json'},\n"
"                body: JSON.stringify({cover_open: is_open})\n"
"            }).then(r => r.json())\n"
"              .then(d => {\n"
"                  if(d.success) {\n"
"                      console.log('Cover open status updated to: ' + is_open);\n"
"                      getPrinterInfo();\n"
"                  }\n"
"              });\n"
"        }\n"
"\n"
"        function setPrinterBusy(is_busy) {\n"
"            fetch('/api/set-printer-busy', {\n"
"                method: 'POST',\n"
"                headers: {'Content-Type': 'application/json'},\n"
"                body: JSON.stringify({printer_busy: is_busy})\n"
"            }).then(r => r.json())\n"
"              .then(d => {\n"
"                  if(d.success) {\n"
"                      console.log('Printer busy status updated to: ' + is_busy);\n"
"                      getPrinterInfo();\n"
"                  }\n"
"              });\n"
"        }\n"
"\n"
"        function setAccelerometer() {\n"
"            const x = parseInt(document.getElementById('accel-x-slider').value);\n"
"            const y = parseInt(document.getElementById('accel-y-slider').value);\n"
"            const z = parseInt(document.getElementById('accel-z-slider').value);\n"
"            const orientation = parseInt(document.getElementById('orientation-input').value);\n"
"\n"
"            fetch('/api/set-accelerometer', {\n"
"                method: 'POST',\n"
"                headers: {'Content-Type': 'application/json'},\n"
"                body: JSON.stringify({x: x, y: y, z: z, orientation: orientation})\n"
"            }).then(r => r.json())\n"
"              .then(d => {\n"
"                  if(d.success) {\n"
"                      console.log('Accelerometer updated: x=' + x + ', y=' + y + ', z=' + z + ', o=' + orientation);\n"
"                  }\n"
"              });\n"
"        }\n"
"\n"
"        function resetAccelerometer() {\n"
"            document.getElementById('accel-x-slider').value = 0;\n"
"            document.getElementById('accel-y-slider').value = 0;\n"
"            document.getElementById('accel-z-slider').value = 0;\n"
"            document.getElementById('orientation-input').value = 0;\n"
"            document.getElementById('accel-x-value').textContent = '0';\n"
"            document.getElementById('accel-y-value').textContent = '0';\n"
"            document.getElementById('accel-z-value').textContent = '0';\n"
"            setAccelerometer();\n"
"        }\n"
"\n"
"        function triggerShutter() {\n"
"            // TODO: Implement actual BLE notification mechanism once protocol is discovered\n"
"            // For now, this is a placeholder that logs the action\n"
"            const statusDiv = document.getElementById('shutter-status');\n"
"            statusDiv.textContent = 'Shutter triggered! (Protocol mechanism pending)';\n"
"            statusDiv.style.color = '#28a745';\n"
"            \n"
"            console.log('Shutter button pressed - BLE notification mechanism TBD');\n"
"            \n"
"            // Clear status after 2 seconds\n"
"            setTimeout(() => {\n"
"                statusDiv.textContent = '';\n"
"            }, 2000);\n"
"        }\n"
"\n"
"        function saveDIS() {\n"
"            const disData = {\n"
"                model_number: document.getElementById('dis-model-number').value,\n"
"                serial_number: document.getElementById('dis-serial-number').value,\n"
"                firmware_revision: document.getElementById('dis-firmware').value,\n"
"                hardware_revision: document.getElementById('dis-hardware').value,\n"
"                software_revision: document.getElementById('dis-software').value,\n"
"                manufacturer_name: document.getElementById('dis-manufacturer').value\n"
"            };\n"
"\n"
"            fetch('/api/set-dis', {\n"
"                method: 'POST',\n"
"                headers: {'Content-Type': 'application/json'},\n"
"                body: JSON.stringify(disData)\n"
"            }).then(r => r.json())\n"
"              .then(d => {\n"
"                  if(d.success) {\n"
"                      alert('Device Information Service values saved successfully!');\n"
"                      getPrinterInfo();\n"
"                  } else {\n"
"                      alert('Failed to save DIS values');\n"
"                  }\n"
"              }).catch(e => {\n"
"                  alert('Error saving DIS values: ' + e);\n"
"              });\n"
"        }\n"
"\n"
"        function resetDISDefaults() {\n"
"            if (!confirm('Reset all Device Information Service values to model-specific defaults?')) {\n"
"                return;\n"
"            }\n"
"\n"
"            fetch('/api/reset-dis-defaults', {\n"
"                method: 'POST'\n"
"            }).then(r => r.json())\n"
"              .then(d => {\n"
"                  if(d.success) {\n"
"                      alert('Device Information Service reset to defaults!');\n"
"                      getPrinterInfo();\n"
"                  } else {\n"
"                      alert('Failed to reset DIS values');\n"
"                  }\n"
"              }).catch(e => {\n"
"                  alert('Error resetting DIS values: ' + e);\n"
"              });\n"
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
"                            '<span><button onclick=\"viewFile(\\'' + f.name + '\\')\">View</button>' +\n"
"                            '<button onclick=\"downloadFile(\\'' + f.name + '\\')\">Download</button>' +\n"
"                            '<button class=\"danger\" onclick=\"deleteFile(\\'' + f.name + '\\')\">Delete</button></span>';\n"
"                        list.appendChild(li);\n"
"                    });\n"
"                });\n"
"        }\n"
"\n"
"        function viewFile(name) {\n"
"            window.open('/api/files/' + name, '_blank');\n"
"        }\n"
"\n"
"        function downloadFile(name) {\n"
"            const a = document.createElement('a');\n"
"            a.href = '/api/files/' + name;\n"
"            a.download = name;\n"
"            document.body.appendChild(a);\n"
"            a.click();\n"
"            document.body.removeChild(a);\n"
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
"                fetch('/api/files?file=' + encodeURIComponent(name), {method: 'DELETE'})\n"
"                    .then(() => refreshFiles());\n"
"            }\n"
"        }\n"
"\n"
"        function deleteAllFiles() {\n"
"            if(confirm('Delete ALL files? This cannot be undone!')) {\n"
"                fetch('/api/files-delete-all', {method: 'POST'})\n"
"                    .then(r => r.json())\n"
"                    .then(d => {\n"
"                        if(d.success) {\n"
"                            alert('All files deleted successfully');\n"
"                            refreshFiles();\n"
"                        } else {\n"
"                            alert('Failed to delete files');\n"
"                        }\n"
"                    })\n"
"                    .catch(() => alert('Error deleting files'));\n"
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
"        // Update system info periodically\n"
"        let consecutiveFailures = 0;\n"
"\n"
"        // Timeout wrapper for fetch (fails after 3 seconds)\n"
"        function fetchWithTimeout(url, timeout = 3000) {\n"
"            return Promise.race([\n"
"                fetch(url, { cache: 'no-cache' }),\n"
"                new Promise((_, reject) => \n"
"                    setTimeout(() => reject(new Error('Timeout')), timeout)\n"
"                )\n"
"            ]);\n"
"        }\n"
"\n"
"        function updateSystemInfo() {\n"
"            fetchWithTimeout('/api/status')\n"
"                .then(r => {\n"
"                    if (!r.ok) throw new Error('HTTP error');\n"
"                    return r.json();\n"
"                })\n"
"                .then(d => {\n"
"                // Connection successful - hide offline banner\n"
"                consecutiveFailures = 0;\n"
"                document.getElementById('offline-banner').classList.remove('visible');\n"
"\n"
"                // System information\n"
"                document.getElementById('uptime').textContent = d.uptime || 'Unknown';\n"
"                document.getElementById('reset-reason').textContent = d.reset_reason || 'Unknown';\n"
"                document.getElementById('ip-address').textContent = d.ip || 'Not connected';\n"
"\n"
"                // BLE failure info\n"
"                if(d.ble_failures) {\n"
"                    const bf = d.ble_failures;\n"
"                    document.getElementById('ble-resets').textContent = bf.reset_count || 0;\n"
"                    if(bf.last_reset_reason && bf.last_reset_reason !== 'None') {\n"
"                        const ago = bf.last_reset_seconds_ago ? ' (' + bf.last_reset_seconds_ago + 's ago)' : '';\n"
"                        document.getElementById('ble-last-reset').textContent = bf.last_reset_reason + ago;\n"
"                    } else {\n"
"                        document.getElementById('ble-last-reset').textContent = 'None';\n"
"                    }\n"
"\n"
"                    document.getElementById('ble-disconnects').textContent = bf.disconnect_count || 0;\n"
"                    if(bf.last_disconnect_reason && bf.last_disconnect_reason !== 'None') {\n"
"                        const ago = bf.last_disconnect_seconds_ago ? ' (' + bf.last_disconnect_seconds_ago + 's ago)' : '';\n"
"                        document.getElementById('ble-last-disconnect').textContent = bf.last_disconnect_reason + ago;\n"
"                    } else {\n"
"                        document.getElementById('ble-last-disconnect').textContent = 'None';\n"
"                    }\n"
"                }\n"
"\n"
"                // BLE advertising status\n"
"                if(d.ble_advertising) {\n"
"                    document.getElementById('ble-advertising-status').textContent = 'Advertising';\n"
"                    document.getElementById('ble-advertising-status').className = 'status connected';\n"
"                    getPrinterInfo();\n"
"                } else {\n"
"                    document.getElementById('ble-advertising-status').textContent = 'Stopped';\n"
"                    document.getElementById('ble-advertising-status').className = 'status disconnected';\n"
"                }\n"
"            })\n"
"            .catch(err => {\n"
"                // Connection failed - increment failure counter\n"
"                consecutiveFailures++;\n"
"                console.error('Failed to fetch status:', err);\n"
"\n"
"                // Show offline banner after 2 consecutive failures (10 seconds)\n"
"                if (consecutiveFailures >= 2) {\n"
"                    document.getElementById('offline-banner').classList.add('visible');\n"
"                    document.getElementById('uptime').textContent = 'Offline';\n"
"                    document.getElementById('ip-address').textContent = 'Offline';\n"
"                }\n"
"            });\n"
"        }\n"
"\n"
"        // Initialize page - load current printer info and system status\n"
"        getPrinterInfo();\n"
"        updateSystemInfo();\n"
"        setInterval(updateSystemInfo, 5000); // Update every 5 seconds\n"
"        refreshFiles();\n"
"    </script>\n"
"</body>\n"
"</html>\n";

// Handler for main page
static esp_err_t root_handler(httpd_req_t *req)
{
    // Ensure UTF-8 so characters like “×” display correctly
    httpd_resp_set_type(req, "text/html; charset=UTF-8");
    
    // Send the HTML page
    httpd_resp_send(req, HTML_TEMPLATE, HTTPD_RESP_USE_STRLEN);
    
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
    bool is_advertising = printer_emulator_is_advertising();
    cJSON_AddBoolToObject(root, "ble_advertising", is_advertising);
    cJSON_AddStringToObject(root, "ble_state", is_advertising ? "advertising" : "stopped");

    // SPIFFS status
    size_t total, used;
    if (spiffs_manager_get_stats(&total, &used) == ESP_OK) {
        cJSON_AddNumberToObject(root, "storage_total", total);
        cJSON_AddNumberToObject(root, "storage_used", used);
    }

    // System info - uptime
    int64_t uptime_ms = esp_log_timestamp();
    int64_t uptime_sec = uptime_ms / 1000;
    int hours = uptime_sec / 3600;
    int minutes = (uptime_sec % 3600) / 60;
    int seconds = uptime_sec % 60;
    char uptime_str[32];
    snprintf(uptime_str, sizeof(uptime_str), "%dh %dm %ds", hours, minutes, seconds);
    cJSON_AddStringToObject(root, "uptime", uptime_str);
    cJSON_AddNumberToObject(root, "uptime_seconds", (int)uptime_sec);

    // Reset reason
    esp_reset_reason_t reset_reason = esp_reset_reason();
    const char *reset_reason_str = "Unknown";
    switch (reset_reason) {
        case ESP_RST_POWERON:   reset_reason_str = "Power-on"; break;
        case ESP_RST_SW:        reset_reason_str = "Software reset"; break;
        case ESP_RST_PANIC:     reset_reason_str = "Exception/panic"; break;
        case ESP_RST_INT_WDT:   reset_reason_str = "Interrupt watchdog"; break;
        case ESP_RST_TASK_WDT:  reset_reason_str = "Task watchdog"; break;
        case ESP_RST_WDT:       reset_reason_str = "Other watchdog"; break;
        case ESP_RST_DEEPSLEEP: reset_reason_str = "Deep sleep wakeup"; break;
        case ESP_RST_BROWNOUT:  reset_reason_str = "Brownout"; break;
        case ESP_RST_SDIO:      reset_reason_str = "SDIO reset"; break;
        default: break;
    }
    cJSON_AddStringToObject(root, "reset_reason", reset_reason_str);

    // BLE failure information (placeholder - will be implemented in ble_peripheral.c)
    cJSON *ble_info = cJSON_CreateObject();
    cJSON_AddNumberToObject(ble_info, "reset_count", 0);
    cJSON_AddNumberToObject(ble_info, "disconnect_count", 0);
    cJSON_AddStringToObject(ble_info, "last_reset_reason", "None");
    cJSON_AddStringToObject(ble_info, "last_disconnect_reason", "None");
    cJSON_AddItemToObject(root, "ble_failures", ble_info);

    char *json = cJSON_Print(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, strlen(json));

    free(json);
    cJSON_Delete(root);
    return ESP_OK;
}

// Handler for printer info API
static esp_err_t api_printer_info_handler(httpd_req_t *req) {
    const instax_printer_info_t *info = printer_emulator_get_info();

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "device_name", info->device_name);
    cJSON_AddStringToObject(root, "model", printer_emulator_model_to_string(info->model));
    cJSON_AddNumberToObject(root, "width", info->width);
    cJSON_AddNumberToObject(root, "height", info->height);
    cJSON_AddNumberToObject(root, "battery", info->battery_percentage);
    cJSON_AddBoolToObject(root, "charging", info->is_charging);
    cJSON_AddBoolToObject(root, "suspend_decrement", printer_emulator_get_suspend_decrement());
    cJSON_AddNumberToObject(root, "photos_remaining", info->photos_remaining);
    cJSON_AddNumberToObject(root, "lifetime_prints", info->lifetime_print_count);
    cJSON_AddBoolToObject(root, "advertising", ble_peripheral_is_advertising());
    cJSON_AddBoolToObject(root, "connected", ble_peripheral_is_connected());

    // Add BLE MAC address (useful for packet tracing)
    uint8_t mac[6];
    ble_peripheral_get_mac_address(mac);
    char mac_str[18];
    snprintf(mac_str, sizeof(mac_str), "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    cJSON_AddStringToObject(root, "ble_mac", mac_str);

    // Add accelerometer data
    cJSON *accel = cJSON_CreateObject();
    cJSON_AddNumberToObject(accel, "x", info->accelerometer.x);
    cJSON_AddNumberToObject(accel, "y", info->accelerometer.y);
    cJSON_AddNumberToObject(accel, "z", info->accelerometer.z);
    cJSON_AddNumberToObject(accel, "orientation", info->accelerometer.orientation);
    cJSON_AddItemToObject(root, "accelerometer", accel);

    // Add error simulation states
    cJSON_AddBoolToObject(root, "cover_open", info->cover_open);
    cJSON_AddBoolToObject(root, "printer_busy", info->printer_busy);

    // Add bonding status (read from NVS)
    nvs_handle_t nvs_handle;
    uint8_t bonding_enabled = 1;  // Default: enabled (matches real printer)
    esp_err_t err = nvs_open("storage", NVS_READONLY, &nvs_handle);
    if (err == ESP_OK) {
        nvs_get_u8(nvs_handle, "ble_bonding", &bonding_enabled);
        nvs_close(nvs_handle);
    }
    cJSON_AddBoolToObject(root, "bonding_enabled", bonding_enabled != 0);

    // Add newly discovered protocol features (Dec 2025)
    cJSON_AddNumberToObject(root, "auto_sleep_timeout", info->auto_sleep_timeout);
    cJSON_AddNumberToObject(root, "print_mode", info->print_mode);

    // Add Device Information Service (DIS) characteristics
    // Uses values from printer_info (model-specific defaults or user-configured)
    cJSON *dis = cJSON_CreateObject();
    cJSON_AddStringToObject(dis, "model_number", info->model_number);
    cJSON_AddStringToObject(dis, "serial_number", info->serial_number);
    cJSON_AddStringToObject(dis, "firmware_revision", info->firmware_revision);
    cJSON_AddStringToObject(dis, "hardware_revision", info->hardware_revision);
    cJSON_AddStringToObject(dis, "software_revision", info->software_revision);
    cJSON_AddStringToObject(dis, "manufacturer_name", info->manufacturer_name);
    cJSON_AddItemToObject(root, "device_info", dis);

    char *json = cJSON_Print(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, strlen(json));

    free(json);
    cJSON_Delete(root);
    return ESP_OK;
}

// Handler for file download
static esp_err_t api_file_download_handler(httpd_req_t *req) {
    ESP_LOGI(TAG, "File download request, URI: %s", req->uri);

    // Extract filename from URI (e.g., /api/files/print_12345.jpg)
    const char *filename = req->uri + strlen("/api/files/");

    if (strlen(filename) == 0) {
        ESP_LOGE(TAG, "No filename in URI");
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "No filename");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Downloading file: %s", filename);

    // Read file from SPIFFS
    char filepath[64];
    snprintf(filepath, sizeof(filepath), "/spiffs/%s", filename);

    FILE *f = fopen(filepath, "rb");
    if (f == NULL) {
        ESP_LOGE(TAG, "File not found: %s", filepath);
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "File not found");
        return ESP_FAIL;
    }

    // Get file size
    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    fseek(f, 0, SEEK_SET);

    // Set content type to JPEG
    httpd_resp_set_type(req, "image/jpeg");

    // Send file in chunks
    char *chunk = malloc(1024);
    if (chunk == NULL) {
        fclose(f);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
        return ESP_FAIL;
    }

    size_t total_sent = 0;
    while (total_sent < file_size) {
        size_t to_read = (file_size - total_sent > 1024) ? 1024 : (file_size - total_sent);
        size_t read_bytes = fread(chunk, 1, to_read, f);

        if (read_bytes > 0) {
            if (httpd_resp_send_chunk(req, chunk, read_bytes) != ESP_OK) {
                free(chunk);
                fclose(f);
                return ESP_FAIL;
            }
            total_sent += read_bytes;
        } else {
            break;
        }
    }

    httpd_resp_send_chunk(req, NULL, 0); // End response
    free(chunk);
    fclose(f);

    ESP_LOGI(TAG, "File download complete: %ld bytes", file_size);
    return ESP_OK;
}

// Handler for file deletion
static esp_err_t api_file_delete_handler(httpd_req_t *req) {
    ESP_LOGI(TAG, "File delete request, URI: %s", req->uri);

    // Try to extract filename from query parameter first (?file=xxx)
    char filename_param[64] = {0};
    if (httpd_req_get_url_query_str(req, filename_param, sizeof(filename_param)) == ESP_OK) {
        char filename[64] = {0};
        if (httpd_query_key_value(filename_param, "file", filename, sizeof(filename)) == ESP_OK) {
            ESP_LOGI(TAG, "Deleting file from query param: %s", filename);

            char filepath[80];
            snprintf(filepath, sizeof(filepath), "/spiffs/%s", filename);

            if (remove(filepath) != 0) {
                ESP_LOGE(TAG, "Failed to delete file: %s", filepath);
                httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "File not found or delete failed");
                return ESP_FAIL;
            }

            ESP_LOGI(TAG, "File deleted successfully: %s", filename);
            httpd_resp_set_status(req, "200 OK");
            httpd_resp_send(req, NULL, 0);
            return ESP_OK;
        }
    }

    // Fallback: try to extract from path (if wildcard works)
    const char *filename = req->uri + strlen("/api/files/");
    if (strlen(filename) > 0 && strcmp(filename, "*") != 0) {
        ESP_LOGI(TAG, "Deleting file from path: %s", filename);

        char filepath[80];
        snprintf(filepath, sizeof(filepath), "/spiffs/%s", filename);

        if (remove(filepath) != 0) {
            ESP_LOGE(TAG, "Failed to delete file: %s", filepath);
            httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "File not found or delete failed");
            return ESP_FAIL;
        }

        ESP_LOGI(TAG, "File deleted successfully: %s", filename);
        httpd_resp_set_status(req, "200 OK");
        httpd_resp_send(req, NULL, 0);
        return ESP_OK;
    }

    ESP_LOGE(TAG, "No filename provided");
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "No filename provided");
    return ESP_FAIL;
}

// Handler for delete all files
static esp_err_t api_delete_all_handler(httpd_req_t *req) {
    ESP_LOGI(TAG, "Delete all files request");

    // Format SPIFFS (deletes all files)
    esp_err_t ret = spiffs_manager_format();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to format SPIFFS: %s", esp_err_to_name(ret));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to delete all files");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "All files deleted successfully");
    httpd_resp_set_status(req, "200 OK");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"success\":true,\"message\":\"All files deleted\"}", HTTPD_RESP_USE_STRLEN);
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

// Handler for BLE start API
static esp_err_t api_ble_start_handler(httpd_req_t *req) {
    ESP_LOGI(TAG, "BLE start requested");

    esp_err_t ret = printer_emulator_start_advertising();

    cJSON *root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "success", ret == ESP_OK);
    if (ret != ESP_OK) {
        cJSON_AddStringToObject(root, "error", "Failed to start BLE advertising");
    }

    char *json = cJSON_Print(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, strlen(json));

    free(json);
    cJSON_Delete(root);
    return ESP_OK;
}

// Handler for BLE stop API
static esp_err_t api_ble_stop_handler(httpd_req_t *req) {
    ESP_LOGI(TAG, "BLE stop requested");

    esp_err_t ret = printer_emulator_stop_advertising();

    cJSON *root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "success", ret == ESP_OK);
    if (ret != ESP_OK) {
        cJSON_AddStringToObject(root, "error", "Failed to stop BLE advertising");
    }

    char *json = cJSON_Print(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, strlen(json));

    free(json);
    cJSON_Delete(root);
    return ESP_OK;
}

// Handler for dumping configuration to serial monitor
static esp_err_t api_dump_config_handler(httpd_req_t *req) {
    ESP_LOGI(TAG, "Configuration dump requested via web interface");

    // Call the dump function which will print to serial monitor
    printer_emulator_dump_config();

    // Return success response
    cJSON *root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "success", true);
    cJSON_AddStringToObject(root, "message", "Configuration dumped to serial monitor");

    char *json = cJSON_Print(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, strlen(json));

    free(json);
    cJSON_Delete(root);
    return ESP_OK;
}

// Handler for setting printer model
static esp_err_t api_set_model_handler(httpd_req_t *req) {
    char buf[100];
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid request");
        return ESP_FAIL;
    }
    buf[ret] = '\0';

    cJSON *json = cJSON_Parse(buf);
    if (!json) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }

    cJSON *model_item = cJSON_GetObjectItem(json, "model");
    if (!model_item || !cJSON_IsString(model_item)) {
        cJSON_Delete(json);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing or invalid model");
        return ESP_FAIL;
    }

    const char *model_str = model_item->valuestring;
    instax_model_t model;
    if (strcmp(model_str, "mini") == 0) {
        model = INSTAX_MODEL_MINI;
    } else if (strcmp(model_str, "square") == 0) {
        model = INSTAX_MODEL_SQUARE;
    } else if (strcmp(model_str, "wide") == 0) {
        model = INSTAX_MODEL_WIDE;
    } else {
        cJSON_Delete(json);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid model name");
        return ESP_FAIL;
    }

    esp_err_t result = printer_emulator_set_model(model);

    cJSON *response = cJSON_CreateObject();
    cJSON_AddBoolToObject(response, "success", result == ESP_OK);
    char *response_str = cJSON_Print(response);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, response_str, strlen(response_str));

    free(response_str);
    cJSON_Delete(response);
    cJSON_Delete(json);
    return ESP_OK;
}

// HTML template for markdown viewer
static const char *MARKDOWN_VIEWER_TEMPLATE_START =
"<!DOCTYPE html>\n"
"<html>\n"
"<head>\n"
"    <meta charset=\"UTF-8\">\n"
"    <meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n"
"    <title>%s</title>\n"  // Document title
"    <script src=\"https://cdn.jsdelivr.net/npm/marked/marked.min.js\"></script>\n"
"    <style>\n"
"        body { font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Helvetica, Arial, sans-serif; max-width: 900px; margin: 0 auto; padding: 20px; line-height: 1.6; }\n"
"        pre { background: #f6f8fa; padding: 16px; border-radius: 6px; overflow-x: auto; }\n"
"        code { background: #f6f8fa; padding: 2px 6px; border-radius: 3px; font-family: 'Courier New', monospace; font-size: 0.9em; }\n"
"        pre code { background: none; padding: 0; }\n"
"        table { border-collapse: collapse; width: 100%%; margin: 16px 0; }\n"
"        th, td { border: 1px solid #ddd; padding: 8px 12px; text-align: left; }\n"
"        th { background: #f6f8fa; font-weight: bold; }\n"
"        h1 { border-bottom: 1px solid #eaecef; padding-bottom: 8px; }\n"
"        h2 { border-bottom: 1px solid #eaecef; padding-bottom: 6px; margin-top: 24px; }\n"
"        a { color: #0366d6; text-decoration: none; }\n"
"        a:hover { text-decoration: underline; }\n"
"        .back-link { display: inline-block; margin-bottom: 20px; padding: 8px 16px; background: #f6f8fa; border-radius: 6px; }\n"
"    </style>\n"
"</head>\n"
"<body>\n"
"    <a href=\"/\" class=\"back-link\">← Back to Main Page</a>\n"
"    <div id=\"content\">Loading...</div>\n"
"    <script>\n"
"        fetch('%s')\n"  // Markdown file path
"            .then(r => r.text())\n"
"            .then(md => { document.getElementById('content').innerHTML = marked.parse(md); })\n"
"            .catch(e => { document.getElementById('content').innerHTML = '<p style=\"color:red;\">Error loading document: ' + e + '</p>'; });\n"
"    </script>\n"
"</body>\n"
"</html>\n";

// Handler for serving documentation files with markdown rendering
static esp_err_t docs_protocol_handler(httpd_req_t *req) {
    ESP_LOGI(TAG, "docs_protocol_handler called");

    httpd_resp_set_type(req, "text/html; charset=UTF-8");

    char html[2048];
    int len = snprintf(html, sizeof(html), MARKDOWN_VIEWER_TEMPLATE_START,
                      "INSTAX Protocol Documentation", "/docs/protocol/raw");
    httpd_resp_send(req, html, len);

    ESP_LOGI(TAG, "Protocol documentation viewer sent");
    return ESP_OK;
}

static esp_err_t docs_install_handler(httpd_req_t *req) {
    ESP_LOGI(TAG, "docs_install_handler called");

    httpd_resp_set_type(req, "text/html; charset=UTF-8");

    char html[2048];
    int len = snprintf(html, sizeof(html), MARKDOWN_VIEWER_TEMPLATE_START,
                      "ESP-IDF Installation Guide", "/docs/install/raw");
    httpd_resp_send(req, html, len);

    ESP_LOGI(TAG, "Install guide viewer sent");
    return ESP_OK;
}

static esp_err_t docs_readme_handler(httpd_req_t *req) {
    ESP_LOGI(TAG, "docs_readme_handler called");

    httpd_resp_set_type(req, "text/html; charset=UTF-8");

    char html[2048];
    int len = snprintf(html, sizeof(html), MARKDOWN_VIEWER_TEMPLATE_START,
                      "ESP32 INSTAX Bridge - README", "/docs/readme/raw");
    httpd_resp_send(req, html, len);

    ESP_LOGI(TAG, "README viewer sent");
    return ESP_OK;
}

// Raw markdown handlers for fetching actual content
static esp_err_t docs_protocol_raw_handler(httpd_req_t *req) {
    FILE *f = fopen("/spiffs/INSTAX_PROTOCOL.md", "r");
    if (f == NULL) {
        ESP_LOGE(TAG, "Failed to open /spiffs/INSTAX_PROTOCOL.md: %s", strerror(errno));
        httpd_resp_send_404(req);
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "text/markdown; charset=UTF-8");

    char buffer[1024];
    size_t read_bytes;
    while ((read_bytes = fread(buffer, 1, sizeof(buffer), f)) > 0) {
        if (httpd_resp_send_chunk(req, buffer, read_bytes) != ESP_OK) {
            fclose(f);
            return ESP_FAIL;
        }
    }

    fclose(f);
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

static esp_err_t docs_install_raw_handler(httpd_req_t *req) {
    FILE *f = fopen("/spiffs/INSTALL_ESP_IDF.md", "r");
    if (f == NULL) {
        ESP_LOGE(TAG, "Failed to open /spiffs/INSTALL_ESP_IDF.md: %s", strerror(errno));
        httpd_resp_send_404(req);
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "text/markdown; charset=UTF-8");

    char buffer[1024];
    size_t read_bytes;
    while ((read_bytes = fread(buffer, 1, sizeof(buffer), f)) > 0) {
        if (httpd_resp_send_chunk(req, buffer, read_bytes) != ESP_OK) {
            fclose(f);
            return ESP_FAIL;
        }
    }

    fclose(f);
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

static esp_err_t docs_readme_raw_handler(httpd_req_t *req) {
    FILE *f = fopen("/spiffs/README.md", "r");
    if (f == NULL) {
        ESP_LOGE(TAG, "Failed to open /spiffs/README.md: %s", strerror(errno));
        httpd_resp_send_404(req);
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "text/markdown; charset=UTF-8");

    char buffer[1024];
    size_t read_bytes;
    while ((read_bytes = fread(buffer, 1, sizeof(buffer), f)) > 0) {
        if (httpd_resp_send_chunk(req, buffer, read_bytes) != ESP_OK) {
            fclose(f);
            return ESP_FAIL;
        }
    }

    fclose(f);
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

static esp_err_t api_reboot_handler(httpd_req_t *req) {
    // Send success response before rebooting
    cJSON *response = cJSON_CreateObject();
    cJSON_AddBoolToObject(response, "success", true);
    char *response_str = cJSON_Print(response);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, response_str, strlen(response_str));

    free(response_str);
    cJSON_Delete(response);

    ESP_LOGI(TAG, "Reboot requested via web interface");

    // Delay to allow response to be sent
    vTaskDelay(pdMS_TO_TICKS(100));

    // Trigger software reset
    esp_restart();

    return ESP_OK;  // Never reached
}

// Handler for setting battery level
static esp_err_t api_set_battery_handler(httpd_req_t *req) {
    char buf[100];
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid request");
        return ESP_FAIL;
    }
    buf[ret] = '\0';

    cJSON *json = cJSON_Parse(buf);
    if (!json) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }

    cJSON *percentage_item = cJSON_GetObjectItem(json, "percentage");
    if (!percentage_item || !cJSON_IsNumber(percentage_item)) {
        cJSON_Delete(json);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing or invalid percentage");
        return ESP_FAIL;
    }

    int percentage = percentage_item->valueint;
    if (percentage < 0 || percentage > 100) {
        cJSON_Delete(json);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Percentage must be 0-100");
        return ESP_FAIL;
    }

    esp_err_t result = printer_emulator_set_battery(percentage);

    cJSON *response = cJSON_CreateObject();
    cJSON_AddBoolToObject(response, "success", result == ESP_OK);
    char *response_str = cJSON_Print(response);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, response_str, strlen(response_str));

    free(response_str);
    cJSON_Delete(response);
    cJSON_Delete(json);
    return ESP_OK;
}

// Handler for setting device name
static esp_err_t api_set_name_handler(httpd_req_t *req) {
    char buf[128];
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid request");
        return ESP_FAIL;
    }
    buf[ret] = '\0';

    cJSON *json = cJSON_Parse(buf);
    if (!json) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }

    cJSON *name_item = cJSON_GetObjectItem(json, "name");
    if (!name_item || !cJSON_IsString(name_item)) {
        cJSON_Delete(json);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing or invalid name");
        return ESP_FAIL;
    }

    const char *name = name_item->valuestring;
    if (strlen(name) == 0 || strlen(name) > 32) {
        cJSON_Delete(json);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Name must be 1-32 characters");
        return ESP_FAIL;
    }

    esp_err_t result = printer_emulator_set_device_name(name);

    cJSON *response = cJSON_CreateObject();
    cJSON_AddBoolToObject(response, "success", result == ESP_OK);
    char *response_str = cJSON_Print(response);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, response_str, strlen(response_str));

    free(response_str);
    cJSON_Delete(response);
    cJSON_Delete(json);
    return ESP_OK;
}

// Handler for setting prints remaining
static esp_err_t api_set_prints_handler(httpd_req_t *req) {
    char buf[100];
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid request");
        return ESP_FAIL;
    }
    buf[ret] = '\0';

    cJSON *json = cJSON_Parse(buf);
    if (!json) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }

    cJSON *count_item = cJSON_GetObjectItem(json, "count");
    if (!count_item || !cJSON_IsNumber(count_item)) {
        cJSON_Delete(json);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing or invalid count");
        return ESP_FAIL;
    }

    int count = count_item->valueint;
    if (count < 0 || count > 255) {
        cJSON_Delete(json);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Count must be 0-255");
        return ESP_FAIL;
    }

    esp_err_t result = printer_emulator_set_prints_remaining(count);

    cJSON *response = cJSON_CreateObject();
    cJSON_AddBoolToObject(response, "success", result == ESP_OK);
    char *response_str = cJSON_Print(response);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, response_str, strlen(response_str));

    free(response_str);
    cJSON_Delete(response);
    cJSON_Delete(json);
    return ESP_OK;
}

// Handler for setting charging status
static esp_err_t api_set_charging_handler(httpd_req_t *req) {
    char buf[100];
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid request");
        return ESP_FAIL;
    }
    buf[ret] = '\0';

    cJSON *json = cJSON_Parse(buf);
    if (!json) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }

    cJSON *charging_item = cJSON_GetObjectItem(json, "charging");
    if (!charging_item || !cJSON_IsBool(charging_item)) {
        cJSON_Delete(json);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing or invalid charging status");
        return ESP_FAIL;
    }

    bool is_charging = cJSON_IsTrue(charging_item);
    esp_err_t result = printer_emulator_set_charging(is_charging);

    cJSON *response = cJSON_CreateObject();
    cJSON_AddBoolToObject(response, "success", result == ESP_OK);
    char *response_str = cJSON_Print(response);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, response_str, strlen(response_str));

    free(response_str);
    cJSON_Delete(response);
    cJSON_Delete(json);
    return ESP_OK;
}

// Handler for setting suspend decrement mode
static esp_err_t api_set_suspend_decrement_handler(httpd_req_t *req) {
    char buf[100];
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid request");
        return ESP_FAIL;
    }
    buf[ret] = '\0';

    cJSON *json = cJSON_Parse(buf);
    if (!json) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }

    cJSON *suspend_item = cJSON_GetObjectItem(json, "suspend");
    if (!suspend_item || !cJSON_IsBool(suspend_item)) {
        cJSON_Delete(json);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing or invalid suspend status");
        return ESP_FAIL;
    }

    bool suspend = cJSON_IsTrue(suspend_item);
    esp_err_t result = printer_emulator_set_suspend_decrement(suspend);

    cJSON *response = cJSON_CreateObject();
    cJSON_AddBoolToObject(response, "success", result == ESP_OK);
    char *response_str = cJSON_Print(response);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, response_str, strlen(response_str));

    free(response_str);
    cJSON_Delete(response);
    cJSON_Delete(json);
    return ESP_OK;
}

// Handler for setting BLE bonding enabled/disabled
static esp_err_t api_set_bonding_handler(httpd_req_t *req) {
    char buf[100];
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid request");
        return ESP_FAIL;
    }
    buf[ret] = '\0';

    cJSON *json = cJSON_Parse(buf);
    if (!json) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }

    cJSON *enabled_item = cJSON_GetObjectItem(json, "enabled");
    if (!enabled_item || !cJSON_IsBool(enabled_item)) {
        cJSON_Delete(json);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing or invalid enabled status");
        return ESP_FAIL;
    }

    bool enabled = cJSON_IsTrue(enabled_item);

    // Save bonding preference to NVS
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open("storage", NVS_READWRITE, &nvs_handle);
    if (err == ESP_OK) {
        nvs_set_u8(nvs_handle, "ble_bonding", enabled ? 1 : 0);
        nvs_commit(nvs_handle);
        nvs_close(nvs_handle);
        ESP_LOGI("WEB", "Bonding preference saved: %s", enabled ? "ENABLED" : "DISABLED");
    } else {
        ESP_LOGE("WEB", "Failed to open NVS for bonding preference");
    }

    cJSON *response = cJSON_CreateObject();
    cJSON_AddBoolToObject(response, "success", err == ESP_OK);
    char *response_str = cJSON_Print(response);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, response_str, strlen(response_str));

    free(response_str);
    cJSON_Delete(response);
    cJSON_Delete(json);

    // Restart ESP32 to apply bonding changes
    if (err == ESP_OK) {
        ESP_LOGI("WEB", "Restarting ESP32 to apply bonding changes...");
        vTaskDelay(pdMS_TO_TICKS(1000));  // Give time for response to send
        esp_restart();
    }

    return ESP_OK;
}

// Handler for clearing bonding database
static esp_err_t api_clear_bonds_handler(httpd_req_t *req) {
    char buf[100];
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid request");
        return ESP_FAIL;
    }
    buf[ret] = '\0';

    ESP_LOGI("WEB", "Clearing bonding database...");

    // Clear NimBLE bond storage
    extern int ble_store_clear(void);
    int rc = ble_store_clear();

    esp_err_t result = (rc == 0) ? ESP_OK : ESP_FAIL;
    if (result == ESP_OK) {
        ESP_LOGI("WEB", "Bonding database cleared successfully");
    } else {
        ESP_LOGE("WEB", "Failed to clear bonding database: %d", rc);
    }

    cJSON *response = cJSON_CreateObject();
    cJSON_AddBoolToObject(response, "success", result == ESP_OK);
    char *response_str = cJSON_Print(response);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, response_str, strlen(response_str));

    free(response_str);
    cJSON_Delete(response);

    // Restart ESP32 after clearing bonds
    if (result == ESP_OK) {
        ESP_LOGI("WEB", "Restarting ESP32 after clearing bonds...");
        vTaskDelay(pdMS_TO_TICKS(1000));  // Give time for response to send
        esp_restart();
    }

    return ESP_OK;
}

// Handler for setting cover open/closed state
static esp_err_t api_set_cover_open_handler(httpd_req_t *req) {
    char buf[100];
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid request");
        return ESP_FAIL;
    }
    buf[ret] = '\0';

    cJSON *json = cJSON_Parse(buf);
    if (!json) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }

    cJSON *cover_item = cJSON_GetObjectItem(json, "cover_open");
    if (!cover_item || !cJSON_IsBool(cover_item)) {
        cJSON_Delete(json);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing or invalid cover_open status");
        return ESP_FAIL;
    }

    bool is_open = cJSON_IsTrue(cover_item);
    esp_err_t result = printer_emulator_set_cover_open(is_open);

    cJSON *response = cJSON_CreateObject();
    cJSON_AddBoolToObject(response, "success", result == ESP_OK);
    char *response_str = cJSON_Print(response);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, response_str, strlen(response_str));

    free(response_str);
    cJSON_Delete(response);
    cJSON_Delete(json);
    return ESP_OK;
}

// Handler for setting printer busy state
static esp_err_t api_set_printer_busy_handler(httpd_req_t *req) {
    char buf[100];
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid request");
        return ESP_FAIL;
    }
    buf[ret] = '\0';

    cJSON *json = cJSON_Parse(buf);
    if (!json) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }

    cJSON *busy_item = cJSON_GetObjectItem(json, "printer_busy");
    if (!busy_item || !cJSON_IsBool(busy_item)) {
        cJSON_Delete(json);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing or invalid printer_busy status");
        return ESP_FAIL;
    }

    bool is_busy = cJSON_IsTrue(busy_item);
    esp_err_t result = printer_emulator_set_busy(is_busy);

    cJSON *response = cJSON_CreateObject();
    cJSON_AddBoolToObject(response, "success", result == ESP_OK);
    char *response_str = cJSON_Print(response);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, response_str, strlen(response_str));

    free(response_str);
    cJSON_Delete(response);
    cJSON_Delete(json);
    return ESP_OK;
}

// Handler for setting accelerometer values
static esp_err_t api_set_accelerometer_handler(httpd_req_t *req) {
    char buf[200];
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid request");
        return ESP_FAIL;
    }
    buf[ret] = '\0';

    cJSON *json = cJSON_Parse(buf);
    if (!json) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }

    // Get accelerometer values (optional - only set if provided)
    cJSON *x_item = cJSON_GetObjectItem(json, "x");
    cJSON *y_item = cJSON_GetObjectItem(json, "y");
    cJSON *z_item = cJSON_GetObjectItem(json, "z");
    cJSON *orientation_item = cJSON_GetObjectItem(json, "orientation");

    esp_err_t result = ESP_OK;

    if (x_item && cJSON_IsNumber(x_item)) {
        int16_t x = (int16_t)x_item->valueint;
        result = printer_emulator_set_accel_x(x);
        if (result != ESP_OK) goto error;
    }

    if (y_item && cJSON_IsNumber(y_item)) {
        int16_t y = (int16_t)y_item->valueint;
        result = printer_emulator_set_accel_y(y);
        if (result != ESP_OK) goto error;
    }

    if (z_item && cJSON_IsNumber(z_item)) {
        int16_t z = (int16_t)z_item->valueint;
        result = printer_emulator_set_accel_z(z);
        if (result != ESP_OK) goto error;
    }

    if (orientation_item && cJSON_IsNumber(orientation_item)) {
        uint8_t orientation = (uint8_t)orientation_item->valueint;
        result = printer_emulator_set_accel_orientation(orientation);
        if (result != ESP_OK) goto error;
    }

    cJSON *response = cJSON_CreateObject();
    cJSON_AddBoolToObject(response, "success", true);
    char *response_str = cJSON_Print(response);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, response_str, strlen(response_str));

    free(response_str);
    cJSON_Delete(response);
    cJSON_Delete(json);
    return ESP_OK;

error:
    cJSON_Delete(json);
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to set accelerometer");
    return ESP_FAIL;
}

// Handler for updating Device Information Service values
static esp_err_t api_set_dis_handler(httpd_req_t *req) {
    char buf[512];
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid request");
        return ESP_FAIL;
    }
    buf[ret] = '\0';

    cJSON *json = cJSON_Parse(buf);
    if (!json) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }

    esp_err_t result = ESP_OK;

    // Update each field if provided
    cJSON *item = cJSON_GetObjectItem(json, "model_number");
    if (item && cJSON_IsString(item)) {
        result = printer_emulator_set_model_number(item->valuestring);
        if (result != ESP_OK) goto error;
    }

    item = cJSON_GetObjectItem(json, "serial_number");
    if (item && cJSON_IsString(item)) {
        result = printer_emulator_set_serial_number(item->valuestring);
        if (result != ESP_OK) goto error;
    }

    item = cJSON_GetObjectItem(json, "firmware_revision");
    if (item && cJSON_IsString(item)) {
        result = printer_emulator_set_firmware_revision(item->valuestring);
        if (result != ESP_OK) goto error;
    }

    item = cJSON_GetObjectItem(json, "hardware_revision");
    if (item && cJSON_IsString(item)) {
        result = printer_emulator_set_hardware_revision(item->valuestring);
        if (result != ESP_OK) goto error;
    }

    item = cJSON_GetObjectItem(json, "software_revision");
    if (item && cJSON_IsString(item)) {
        result = printer_emulator_set_software_revision(item->valuestring);
        if (result != ESP_OK) goto error;
    }

    item = cJSON_GetObjectItem(json, "manufacturer_name");
    if (item && cJSON_IsString(item)) {
        result = printer_emulator_set_manufacturer_name(item->valuestring);
        if (result != ESP_OK) goto error;
    }

    cJSON *response = cJSON_CreateObject();
    cJSON_AddBoolToObject(response, "success", true);
    char *response_str = cJSON_Print(response);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, response_str, strlen(response_str));

    free(response_str);
    cJSON_Delete(response);
    cJSON_Delete(json);
    return ESP_OK;

error:
    cJSON_Delete(json);
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to update DIS values");
    return ESP_FAIL;
}

// Handler for resetting DIS to model defaults
static esp_err_t api_reset_dis_defaults_handler(httpd_req_t *req) {
    esp_err_t result = printer_emulator_reset_dis_to_defaults();

    cJSON *response = cJSON_CreateObject();
    cJSON_AddBoolToObject(response, "success", result == ESP_OK);
    char *response_str = cJSON_Print(response);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, response_str, strlen(response_str));

    free(response_str);
    cJSON_Delete(response);
    return ESP_OK;
}

esp_err_t web_server_start(void) {
    if (s_server != NULL) {
        return ESP_OK; // Already running
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 34;  // Increased for printer settings, DIS endpoints, bonding control, and documentation
    config.stack_size = 8192;
    config.uri_match_fn = httpd_uri_match_wildcard;  // Enable wildcard matching for /api/files/*

    esp_err_t ret = httpd_start(&s_server, &config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start server: %s", esp_err_to_name(ret));
        return ret;
    }

    // Register URI handlers
    httpd_uri_t root_uri = { .uri = "/", .method = HTTP_GET, .handler = root_handler };
    httpd_uri_t status_uri = { .uri = "/api/status", .method = HTTP_GET, .handler = api_status_handler };
    httpd_uri_t printer_info_uri = { .uri = "/api/printer-info", .method = HTTP_GET, .handler = api_printer_info_handler };
    httpd_uri_t files_uri = { .uri = "/api/files", .method = HTTP_GET, .handler = api_files_handler };
    httpd_uri_t file_download_uri = { .uri = "/api/files/*", .method = HTTP_GET, .handler = api_file_download_handler };
    httpd_uri_t file_delete_uri = { .uri = "/api/files", .method = HTTP_DELETE, .handler = api_file_delete_handler };  // Use query param
    httpd_uri_t delete_all_uri = { .uri = "/api/files-delete-all", .method = HTTP_POST, .handler = api_delete_all_handler };
    httpd_uri_t upload_uri = { .uri = "/api/upload", .method = HTTP_POST, .handler = api_upload_handler };
    httpd_uri_t ble_start_uri = { .uri = "/api/ble-start", .method = HTTP_POST, .handler = api_ble_start_handler };
    httpd_uri_t ble_stop_uri = { .uri = "/api/ble-stop", .method = HTTP_POST, .handler = api_ble_stop_handler };
    httpd_uri_t dump_config_uri = { .uri = "/api/dump-config", .method = HTTP_POST, .handler = api_dump_config_handler };
    httpd_uri_t set_model_uri = { .uri = "/api/set-model", .method = HTTP_POST, .handler = api_set_model_handler };
    httpd_uri_t set_battery_uri = { .uri = "/api/set-battery", .method = HTTP_POST, .handler = api_set_battery_handler };
    httpd_uri_t set_name_uri = { .uri = "/api/set-name", .method = HTTP_POST, .handler = api_set_name_handler };
    httpd_uri_t set_prints_uri = { .uri = "/api/set-prints", .method = HTTP_POST, .handler = api_set_prints_handler };
    httpd_uri_t set_charging_uri = { .uri = "/api/set-charging", .method = HTTP_POST, .handler = api_set_charging_handler };
    httpd_uri_t set_suspend_decrement_uri = { .uri = "/api/set-suspend-decrement", .method = HTTP_POST, .handler = api_set_suspend_decrement_handler };
    httpd_uri_t set_bonding_uri = { .uri = "/api/set-bonding", .method = HTTP_POST, .handler = api_set_bonding_handler };
    httpd_uri_t clear_bonds_uri = { .uri = "/api/clear-bonds", .method = HTTP_POST, .handler = api_clear_bonds_handler };
    httpd_uri_t set_cover_open_uri = { .uri = "/api/set-cover-open", .method = HTTP_POST, .handler = api_set_cover_open_handler };
    httpd_uri_t set_printer_busy_uri = { .uri = "/api/set-printer-busy", .method = HTTP_POST, .handler = api_set_printer_busy_handler };
    httpd_uri_t set_accel_uri = { .uri = "/api/set-accelerometer", .method = HTTP_POST, .handler = api_set_accelerometer_handler };
    httpd_uri_t set_dis_uri = { .uri = "/api/set-dis", .method = HTTP_POST, .handler = api_set_dis_handler };
    httpd_uri_t reset_dis_defaults_uri = { .uri = "/api/reset-dis-defaults", .method = HTTP_POST, .handler = api_reset_dis_defaults_handler };
    httpd_uri_t reboot_uri = { .uri = "/api/reboot", .method = HTTP_POST, .handler = api_reboot_handler };
    httpd_uri_t docs_protocol_uri = { .uri = "/docs/protocol", .method = HTTP_GET, .handler = docs_protocol_handler };
    httpd_uri_t docs_install_uri = { .uri = "/docs/install", .method = HTTP_GET, .handler = docs_install_handler };
    httpd_uri_t docs_readme_uri = { .uri = "/docs/readme", .method = HTTP_GET, .handler = docs_readme_handler };
    httpd_uri_t docs_protocol_raw_uri = { .uri = "/docs/protocol/raw", .method = HTTP_GET, .handler = docs_protocol_raw_handler };
    httpd_uri_t docs_install_raw_uri = { .uri = "/docs/install/raw", .method = HTTP_GET, .handler = docs_install_raw_handler };
    httpd_uri_t docs_readme_raw_uri = { .uri = "/docs/readme/raw", .method = HTTP_GET, .handler = docs_readme_raw_handler };

    httpd_register_uri_handler(s_server, &root_uri);
    httpd_register_uri_handler(s_server, &status_uri);
    httpd_register_uri_handler(s_server, &printer_info_uri);
    httpd_register_uri_handler(s_server, &files_uri);  // Register exact match first
    httpd_register_uri_handler(s_server, &file_download_uri);  // Then wildcard GET
    httpd_register_uri_handler(s_server, &file_delete_uri);  // DELETE handler
    httpd_register_uri_handler(s_server, &delete_all_uri);  // Delete all handler
    httpd_register_uri_handler(s_server, &upload_uri);
    httpd_register_uri_handler(s_server, &ble_start_uri);
    httpd_register_uri_handler(s_server, &ble_stop_uri);
    httpd_register_uri_handler(s_server, &dump_config_uri);
    httpd_register_uri_handler(s_server, &set_model_uri);
    httpd_register_uri_handler(s_server, &set_battery_uri);
    httpd_register_uri_handler(s_server, &set_name_uri);
    httpd_register_uri_handler(s_server, &set_prints_uri);
    httpd_register_uri_handler(s_server, &set_charging_uri);
    httpd_register_uri_handler(s_server, &set_suspend_decrement_uri);
    httpd_register_uri_handler(s_server, &set_bonding_uri);
    httpd_register_uri_handler(s_server, &clear_bonds_uri);
    httpd_register_uri_handler(s_server, &set_cover_open_uri);
    httpd_register_uri_handler(s_server, &set_printer_busy_uri);
    httpd_register_uri_handler(s_server, &set_accel_uri);
    httpd_register_uri_handler(s_server, &set_dis_uri);
    httpd_register_uri_handler(s_server, &reset_dis_defaults_uri);
    httpd_register_uri_handler(s_server, &reboot_uri);

    // Register documentation handlers with error checking
    ret = httpd_register_uri_handler(s_server, &docs_protocol_uri);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register docs_protocol_uri: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "Registered /docs/protocol handler");
    }

    ret = httpd_register_uri_handler(s_server, &docs_install_uri);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register docs_install_uri: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "Registered /docs/install handler");
    }

    ret = httpd_register_uri_handler(s_server, &docs_readme_uri);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register docs_readme_uri: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "Registered /docs/readme handler");
    }

    // Register raw markdown handlers
    httpd_register_uri_handler(s_server, &docs_protocol_raw_uri);
    httpd_register_uri_handler(s_server, &docs_install_raw_uri);
    httpd_register_uri_handler(s_server, &docs_readme_raw_uri);

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
