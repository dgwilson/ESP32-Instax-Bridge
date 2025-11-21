# ESP32 Instax Bridge

ESP32 firmware that provides a WiFi-to-BLE bridge for Fujifilm Instax Link printers.

## Features

- Serial console for WiFi configuration
- Web interface for file upload and printer control
- BLE scanning and connection to Instax printers
- SPIFFS storage for JPEG images
- Support for Mini, Square, and Wide Instax printers
- Real-time print progress display
- Battery level and film count monitoring

## Building

This project uses **PlatformIO with ESP-IDF framework**.

#### Install PlatformIO

**VS Code:**
1. Install VS Code
2. Install PlatformIO IDE extension
3. Open this folder in VS Code
4. PlatformIO will automatically detect the project

**Command Line:**
```bash
# Install PlatformIO Core
pip install platformio

# Build the project
cd ESP32-Instax-Bridge
pio run

# Upload to ESP32
pio run -t upload

# Monitor serial output
pio device monitor
```

#### VS Code Setup on Mac

If you're having issues with VS Code + PlatformIO on Mac:

1. Install VS Code from https://code.visualstudio.com/
2. Open VS Code
3. Go to Extensions (⌘+Shift+X)
4. Search for "PlatformIO IDE" and install
5. Restart VS Code
6. Open this folder: File → Open Folder → select `ESP32-Instax-Bridge`
7. PlatformIO will initialize (watch bottom status bar)
8. Click the checkmark icon (✓) in the bottom bar to build
9. Click the arrow icon (→) to upload to your ESP32


## Usage

### Serial Console Commands

After flashing and connecting via serial monitor (115200 baud):

```
wifi_set <ssid> <password>  - Set WiFi credentials
wifi_connect                - Connect to stored WiFi network
wifi_disconnect             - Disconnect from WiFi
wifi_status                 - Show WiFi connection status
wifi_clear                  - Clear stored WiFi credentials

ble_scan                    - Scan for Instax printers (5 seconds)
ble_devices                 - List discovered devices
ble_status                  - Show BLE connection status

files                       - List stored JPEG files
reboot                      - Reboot the device
help                        - Show help
```

### Web Interface

Once connected to WiFi, the ESP32 will start a web server. Access it by:

1. Get the IP address: Type `wifi_status` in serial console
2. Open browser to `http://<ip-address>/`
3. The web interface allows you to:
   - Scan for Instax printers
   - Upload JPEG files
   - View stored files
   - Print images with progress display
   - View printer status (battery, film count)

## Project Structure

```
ESP32-Instax-Bridge/
├── platformio.ini          # PlatformIO configuration
├── partitions.csv          # Partition table (NVS, SPIFFS, app)
└── main/
    ├── main.c              # Entry point
    ├── wifi_manager.c/h    # WiFi connection + NVS storage
    ├── ble_scanner.c/h     # BLE scanning + Instax connection
    ├── console.c/h         # Serial console commands
    ├── web_server.c/h      # HTTP server + web UI
    ├── spiffs_manager.c/h  # SPIFFS file operations
    └── instax_protocol.c/h # Instax BLE protocol
```

## Dependencies

All dependencies are automatically managed by PlatformIO:
- ESP-IDF framework v5.x
- NimBLE (BLE stack)
- HTTP server
- SPIFFS
- NVS (Non-Volatile Storage)
- Console (linenoise, argtable3)

## Instax Protocol

This project implements the Instax BLE protocol based on:
- [Python Instax API](https://github.com/jpwsutton/instax_api)

Supported printer models:
- **Instax Mini Link** - 600×800px, 105KB max file size
- **Instax Square Link** - 800×800px, 105KB max file size
- **Instax Wide Link** - 1260×840px, 105KB max file size

## Troubleshooting

### PlatformIO not detected in VS Code
- Restart VS Code
- Check PlatformIO extension is installed and enabled
- Wait for PlatformIO to initialize (watch status bar)

### Build errors with ESP-IDF
- Clean the build: `pio run -t clean`
- Delete `.pio` folder and rebuild
- Update PlatformIO: `pio upgrade`

### Upload fails
- Check USB cable is connected
- Check correct serial port is selected
- Hold BOOT button on ESP32 while uploading
- Check ESP32 drivers are installed

### Serial console not responding
- Check baud rate is 115200
- Press Enter a few times
- Type `help` to see commands
- Reboot ESP32 if needed

## License

This project is part of the Moments Print suite.
