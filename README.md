# ESP32 Instax Printer Emulator

ESP32 firmware that **emulates** a Fujifilm Instax Link printer over Bluetooth LE. Acts as a virtual Instax printer that your photo apps can connect to, capturing and storing the print jobs sent to it.

## What This Does

Your ESP32 **pretends to be** an Instax printer (Mini/Wide/Square). When your Moments app (or any Instax-compatible app) tries to print a photo, it connects to the ESP32 instead of a real printer. The ESP32:

1. Advertises as "Instax-Mini Link" (or Wide/Square) over Bluetooth LE
2. Accepts connections from photo apps using the authentic Instax BLE protocol
3. Receives the print job data (JPEG image)
4. Saves the image to SPIFFS storage with timestamp
5. Tracks printer state (battery %, prints remaining, lifetime print count)
6. Provides a web interface to view/download captured prints

**Use Cases:**
- Preview/capture photos before printing to real Instax paper
- Archive all prints sent to your Instax printer
- Test Instax integration without wasting film
- Debug photo formatting and color processing
- Develop Instax-compatible applications

## Features

### BLE Peripheral (Instax Printer Emulation)
- ✅ Emulates Instax Mini Link (800×600px)
- ✅ Emulates Instax Square Link (800×800px)
- ✅ Emulates Instax Wide Link (1260×840px)
- ✅ Authentic Instax BLE service UUIDs
- ✅ Full protocol implementation (info, print, LED, status)
- ✅ Battery level reporting (configurable)
- ✅ Film count reporting (configurable)
- ✅ Lifetime print count tracking

### File Storage
- ✅ Saves received prints to SPIFFS as JPEG files
- ✅ Automatic timestamped filenames
- ✅ Web interface for viewing/downloading
- ✅ Persistent storage across reboots

### Management Interface
- ✅ Serial console for configuration
- ✅ WiFi connectivity for web access
- ✅ Web UI for viewing captured prints
- ✅ Runtime configuration of printer model/battery/film

### Monitoring
- ✅ Real-time print progress logging
- ✅ Connection status tracking
- ✅ Automatic state updates
- ✅ **System uptime display** (hours/minutes/seconds)
- ✅ **ESP32 reset reason tracking** (power-on, watchdog, panic, brownout, etc.)
- ✅ **BLE failure diagnostics** (stack resets and disconnects with reasons)

### Network Discovery
- ✅ **mDNS/Bonjour Support** - Access at `http://instax-simulator.local` without IP lookup
- ✅ **IP-in-BLE-Name** - Device advertises as "Instax-Simulator (192.168.x.y)" showing IP octets
- ✅ **HTTP Service Advertisement** - Auto-discovered by browsers with Bonjour support

### Protocol Compliance
- ✅ **15 Photo Maximum** - Prints remaining capped at 15 (protocol's 4-bit field limitation)
- ✅ **Tested Packet Delays** - Optimal BLE timing documented for each printer model
- ✅ NVS persistence for settings

## Quick Start

### 1. Install ESP-IDF

See [INSTALL_ESP_IDF.md](INSTALL_ESP_IDF.md) for detailed instructions.

```bash
# Install ESP-IDF v5.1+ or v6.1+
mkdir -p ~/esp
cd ~/esp
git clone --recursive https://github.com/espressif/esp-idf.git
cd esp-idf
./install.sh esp32

# Source environment (do this in every terminal)
. ~/esp/esp-idf/export.sh
```

### 2. Build and Flash

```bash
# Clone this project
cd ~/Projects
git clone <this-repo>
cd ESP32-Instax-Bridge

# Build
. ~/esp/esp-idf/export.sh  # Load ESP-IDF environment
idf.py build

# Flash to ESP32
idf.py -p /dev/cu.usbserial-* flash

# Monitor console
idf.py monitor
```

### 3. Configure via Console

Connect to serial console (115200 baud):

```bash
# Using ESP-IDF monitor
idf.py monitor

# Or using screen
screen /dev/cu.usbserial-* 115200
```

Available commands:

```
printer_status                     # View current printer state
printer_model mini|wide|square     # Set printer model type
printer_battery 85                 # Set battery to 85%
printer_prints 10                  # Set 10 prints remaining

ble_start                          # Start advertising as printer
ble_stop                           # Stop advertising

wifi_set <ssid> <password>         # Configure WiFi
wifi_connect                       # Connect to WiFi
wifi_status                        # Show connection status

files                              # List received print files
help                               # Show all commands
reboot                             # Restart ESP32
```

### 4. Connect from Your App

1. Run `ble_start` in the console
2. Open your Moments app or Instax-compatible photo app
3. Scan for printers - you'll see "Instax-Mini Link" (or Wide/Square)
4. Connect and send a print job
5. Watch the ESP32 console for progress
6. Image is saved to `/spiffs/print_<timestamp>.jpg`

### 5. View Captured Prints (Optional)

If WiFi is configured:

1. Run `wifi_status` to get the ESP32's IP address
2. Open browser to `http://<ip-address>/`
3. See the "Received Prints" section
4. Click "View" to see images, "Download" to save them

## Project Structure

```
ESP32-Instax-Bridge/
├── README.md                       # This file
├── INSTALL_ESP_IDF.md             # ESP-IDF installation guide
├── STATUS.md                      # Project status and history
├── CMakeLists.txt                 # Root CMake configuration
├── partitions.csv                 # Partition table (NVS, SPIFFS, app)
├── sdkconfig.defaults             # ESP-IDF default configuration
│
└── main/
    ├── CMakeLists.txt             # Component CMake config
    ├── idf_component.yml          # Component dependencies
    │
    ├── main.c                     # Entry point and initialization
    │
    ├── printer_emulator.c/h       # Core printer emulator logic
    ├── ble_peripheral.c/h         # BLE GATT server (printer role)
    ├── instax_protocol.c/h        # Instax protocol implementation
    │
    ├── ble_scanner.c/h            # BLE scanner (legacy, not used)
    ├── wifi_manager.c/h           # WiFi connection + NVS storage
    ├── web_server.c/h             # HTTP server + web UI
    ├── spiffs_manager.c/h         # SPIFFS file operations
    └── console.c/h                # Serial console commands
```

### Core Files

**Printer Emulation:**
- `printer_emulator.c/h` - Main emulator state machine, handles print jobs, manages state (battery, prints, model)
- `ble_peripheral.c/h` - BLE GATT server, advertises as printer, handles characteristic reads/writes
- `instax_protocol.c/h` - Packet encoding/decoding, protocol constants, response generation

**Supporting Systems:**
- `spiffs_manager.c/h` - File storage for received prints
- `wifi_manager.c/h` - WiFi configuration and connection
- `web_server.c/h` - Web interface for viewing prints
- `console.c/h` - Serial console for configuration

### Configuration Files

- `partitions.csv` - Flash layout: 16KB NVS + 1MB SPIFFS + 2MB App
- `sdkconfig.defaults` - ESP-IDF settings (NimBLE enabled, console config)
- `idf_component.yml` - External dependencies (cJSON)

## How It Works

### BLE Advertising

When you run `ble_start`, the ESP32:

1. Initializes NimBLE stack in peripheral mode
2. Registers GATT service with Instax UUIDs:
   - Service: `70954782-2d83-473d-9e5f-81e1d02d5273`
   - Write: `70954783-2d83-473d-9e5f-81e1d02d5273`
   - Notify: `70954784-2d83-473d-9e5f-81e1d02d5273`
3. Starts advertising with device name (e.g., "Instax-Mini Link")
4. Waits for connections

### Print Job Flow

1. **App connects** → ESP32 accepts connection, stops advertising
2. **App queries info** → ESP32 responds with battery, film count, dimensions
3. **App sends print start** → ESP32 creates `/spiffs/print_<timestamp>.jpg`
4. **App sends data chunks** → ESP32 writes chunks to file
5. **App sends print end** → ESP32 closes file
6. **App sends execute** → ESP32 increments lifetime count, decrements remaining prints
7. **App disconnects** → ESP32 resumes advertising

### State Persistence

All settings are saved to NVS (Non-Volatile Storage):
- Printer model (mini/wide/square)
- Battery percentage
- Prints remaining
- Lifetime print count
- WiFi credentials

State survives reboots and power cycles.

## Instax Protocol

This project implements the complete Instax BLE protocol based on:
- [javl/InstaxBLE](https://github.com/javl/InstaxBLE) - Protocol reverse engineering
- [jpwsutton/instax_api](https://github.com/jpwsutton/instax_api) - Python implementation

### Supported Printer Models

| Model | Resolution | Max File Size | Service Name |
|-------|-----------|---------------|--------------|
| **Instax Mini Link** | 800×600px | 105KB (Link 1/2)<br>55KB (Link 3) | Instax-Simulator |
| **Instax Square Link** | 800×800px | 400KB (409,600 bytes) | Instax-Simulator |
| **Instax Wide Link** | 1260×840px | 330KB (337,920 bytes) | Instax-Simulator |

**Note:** File size limits verified via IMAGE_SUPPORT_INFO response structure. See [INSTAX_PROTOCOL.md](../Moments%20Print/INSTAX_PROTOCOL.md) for details.

### Protocol Operations

**Info Queries (0x00):**
- Image support info (dimensions)
- Battery info (percentage, state)
- Printer function info (film count, charging)
- Print history (lifetime count)

**Print Operations (0x10):**
- Print start (image size)
- Print data (chunks)
- Print end (finalize)
- Print execute (commit)

**LED Control (0x30):**
- LED pattern (status indicators)

## Dependencies

All managed automatically by ESP-IDF and component manifest:

- **ESP-IDF** - v5.1+ or v6.1+ framework
- **NimBLE** - BLE stack (included in ESP-IDF)
- **HTTP Server** - Web interface (esp_http_server)
- **SPIFFS** - File storage
- **NVS** - Non-Volatile Storage for settings
- **Console** - Command line (linenoise, argtable3)
- **cJSON** - JSON parsing (via idf_component.yml)

## Troubleshooting

### ESP32 not detected
```bash
# List serial ports
ls /dev/cu.*

# Install USB drivers if needed (CP210x or CH340)
# Check cable is data-capable (not charge-only)
```

### Build fails
```bash
# Make sure ESP-IDF environment is loaded
. ~/esp/esp-idf/export.sh

# Clean and rebuild
idf.py fullclean
idf.py build
```

### Console not responding
- Check baud rate is 115200
- Press Enter a few times to wake it up
- Type `help` to verify commands
- Check you're using blocking mode (CR line endings)

### BLE not advertising
```bash
# Check BLE is initialized
printer_status

# Start advertising
ble_start

# Check advertising state
printer_status  # Look for "BLE Status: Advertising"
```

### App can't find printer
- Make sure `ble_start` has been run
- Check ESP32 logs for "Started advertising as"
- Try restarting Bluetooth on your device
- Move devices closer together (<10 meters)

### Web interface not accessible
```bash
# Verify WiFi is connected
wifi_status

# Reconnect if needed
wifi_connect

# Check web server is running (logs show "Web server started")
```

## Development Notes

### Adding New Printer Models

To add support for additional Instax models:

1. Add model constant to `instax_protocol.h`:
   ```c
   #define INSTAX_MODEL_NEWMODEL 3
   ```

2. Add dimensions in `instax_protocol.c`:
   ```c
   {1234, 567, 182, 120000}  // NewModel
   ```

3. Update `printer_emulator.c` device name generation

### Modifying Protocol Behavior

All protocol handlers are in `ble_peripheral.c`:
- `handle_instax_packet()` - Main dispatcher
- Individual case handlers for each function code
- Response construction with checksums

### Debugging BLE Issues

Enable verbose BLE logging in `sdkconfig`:
```bash
idf.py menuconfig
# Component config → Bluetooth → NimBLE Options → Log Level → Debug
```

## License

This project is part of the Moments Print suite.

Based on reverse-engineered Instax protocol - for educational and archival purposes only.

## Credits

- Instax protocol research: [javl/InstaxBLE](https://github.com/javl/InstaxBLE)
- Python implementation reference: [jpwsutton/instax_api](https://github.com/jpwsutton/instax_api)
- ESP-IDF framework: [Espressif](https://github.com/espressif/esp-idf)

---

**Built with:** ESP-IDF v6.1 on macOS
**Hardware:** ESP32-WROOM-32 (4MB flash)
**Status:** ✅ Production ready, fully tested
