# ESP32 Instax Printer Emulator

ESP32 firmware that **emulates** a Fujifilm Instax Link printer over Bluetooth LE. Acts as a virtual Instax printer that your photo apps can connect to, capturing and storing the print jobs sent to it.

> **Development:** This project was developed with assistance from **Anthropic Claude** (primarily Sonnet 4.5), leveraging AI-assisted protocol analysis and firmware implementation.

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

## Documentation

📄 **[ESP32 Instax Printer Emulator.pdf](ESP32%20Instax%20Printer%20Emulator.pdf)** - Complete technical documentation including hardware setup, protocol analysis, and implementation details.

Additional documentation:
- [INSTAX_PROTOCOL.md](INSTAX_PROTOCOL.md) - Complete Instax BLE protocol specification
- [INSTALL_ESP_IDF.md](INSTALL_ESP_IDF.md) - ESP-IDF installation guide
- [STATUS.md](STATUS.md) - Project status and development history

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
- ✅ **Model-Specific BLE Services** - Link 3 services (`0000D0FF`, `00006287`), Wide services (`0000E0FF`)
- ✅ NVS persistence for settings

## Official App Compatibility Status

**Goal:** Connecting with official Fujifilm INSTAX apps is a stretch goal - a measure of protocol accuracy, though not required for primary use cases.

### Current Status:

| Official App | Compatibility | Notes |
|--------------|---------------|-------|
| **INSTAX Square** | ❌ **Crashes** | App crashes during/after connection (protocol verified correct) |
| **INSTAX Mini Link** | ❌ **Crashes** | App crashes during/after connection in Link 3 mode |
| **INSTAX Wide** | ❌ **Printer Busy Error** | Shows "Printer Busy (1)" despite correct protocol responses |

### Why Official Apps Fail:

The official INSTAX apps crash or show errors despite **100% correct protocol implementation** (verified byte-for-byte against real printers). Possible causes:

1. **MAC Address OUI Filtering** - Apps may require Fujifilm-registered MAC address prefix (`1C:7D:22`)
2. **Advertising Data Validation** - Apps may check manufacturer data or TX power in BLE advertising
3. **Undocumented Protocol Extensions** - Apps may use features not present in third-party protocol documentation
4. **Device Authentication** - Apps may perform cryptographic validation or certificate checks
5. **App Stability Issues** - Apps may have bugs when connecting to non-authentic hardware

**Alternative Apps (Full Compatibility):**
- ✅ Moments Print (custom app) - Works perfectly with all models
- ✅ nRF Connect - Full BLE characteristic access for testing
- ✅ Any third-party apps that don't filter by MAC address

### Bluetooth MAC Address Configuration

**Current Behavior:** ESP32 uses its factory-assigned MAC address by default.

**Why Change MAC Address?**
Official INSTAX apps may filter devices by MAC address OUI (Organizationally Unique Identifier). Setting a Fujifilm-registered MAC may improve discoverability with official apps.

#### Step-by-Step: Changing the MAC Address

1. **Open the configuration file:**
   ```bash
   nano main/ble_peripheral.c
   # Or use your preferred editor
   ```

2. **Find the MAC configuration section** (near the top, around line 30):
   ```c
   // ============================================================================
   // MAC Address Configuration
   // ============================================================================
   // Set to 1 to use a custom MAC address, 0 to use factory default
   #define USE_CUSTOM_MAC 0

   // Custom MAC address (edit this when you find the real Fujifilm MAC)
   static uint8_t custom_mac[6] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
   ```

3. **Enable custom MAC:**
   ```c
   #define USE_CUSTOM_MAC 1  // Change 0 to 1
   ```

4. **Set your custom MAC address:**
   ```c
   // Fujifilm OUI: 1C:7D:22 (already configured by default)
   // Device ID: 55:55:00 (matches simulated serial INSTAX-55550000)
   static uint8_t custom_mac[6] = {0x1C, 0x7D, 0x22, 0x55, 0x55, 0x00};
   ```

   **Note:** The Fujifilm MAC is already pre-configured in the code. You only need to enable `USE_CUSTOM_MAC`.

5. **Rebuild and flash:**
   ```bash
   idf.py build flash monitor
   ```

6. **Verify on startup** - Look for console output:
   ```
   Factory BT MAC: [original ESP32 MAC]
   Custom BT MAC set: 1C:7D:22:55:55:00
   ⚠️  Using custom MAC address (research/development only)
   ```

#### Finding Real Printer MAC Addresses

**Method 1: BLE Scanner (nRF Connect, LightBlue)**
- Connect to a real INSTAX printer
- Note: Many printers use **Random Privacy Addresses** for discovery
- The public MAC may only be visible during pairing

**Method 2: Bluetooth Packet Capture (Most Reliable)**
- Capture Bluetooth traffic during real printer connection
- Look for the public address in pairing/connection events
- See `Bluetooth Packet Capture/` folder for example captures

**Method 3: Fujifilm Registered OUI**
- **Fujifilm Corporation OUI: `1C:7D:22`**
- All Fujifilm Bluetooth devices use this prefix for their MAC addresses
- Last 3 bytes are device-specific identifiers
- Source: IEEE OUI database

#### Important Notes

**⚠️ Legal Warning:** Using Fujifilm's registered MAC OUI without authorization may violate trademark and MAC address registration rules. This feature is intended for:
- Personal research and development only
- Testing protocol compatibility
- Educational purposes

**Technical Details:**
- MAC is set via `esp_base_mac_addr_set()` before BLE initialization
- Changes are RAM-only (not persisted to NVS)
- Requires reboot to change MAC again
- Factory MAC is always logged for reference

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
├── INSTAX_PROTOCOL.md             # Complete protocol specification
├── INSTALL_ESP_IDF.md             # ESP-IDF installation guide
├── STATUS.md                      # Project status and history
├── CMakeLists.txt                 # Root CMake configuration
├── partitions.csv                 # Partition table (NVS, SPIFFS, app)
├── sdkconfig.defaults             # ESP-IDF default configuration
│
├── Bluetooth Packet Capture/      # Reference packet traces
│   └── iPhone_INSTAX_capture-5.pklg  # Real Mini Link 3 print session
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
- Real device packet captures (see `Bluetooth Packet Capture/` folder)

**📖 Complete Protocol Documentation:** See [INSTAX_PROTOCOL.md](INSTAX_PROTOCOL.md) for comprehensive protocol specification including:
- Model-specific BLE service UUIDs
- Link 3-specific services (`0000D0FF`, `00006287`)
- Wide-specific service (`0000E0FF`)
- Packet structure and checksums
- Complete command reference
- Connection sequences and timing requirements

### Supported Printer Models

| Model | Resolution | BLE Model | Firmware | Model-Specific Services |
|-------|-----------|-----------|----------|------------------------|
| **Instax Mini Link 3** | 600×800px | FI033 | 0101 | Link 3 Info (`0000D0FF`)<br>Link 3 Status (`00006287`) |
| **Instax Mini Link 1/2** | 600×800px | FI031/FI032 | 0101 | None (standard only) |
| **Instax Square Link** | 800×800px | FI017 | 0101 | None (standard only) |
| **Instax Wide Link** | 1260×840px | FI022 | 0100 | Wide Service (`0000E0FF`) |

**File Size Limits:**
- Mini Link 1/2: 105 KB
- Mini Link 3: 55 KB (firmware-limited)
- Square: 105 KB (conservative, protocol reports 400 KB)
- Wide: 105 KB (conservative, protocol reports 330 KB)

See [INSTAX_PROTOCOL.md](INSTAX_PROTOCOL.md) for detailed model specifications and protocol differences.

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

## Known Issues & Limitations

### Accelerometer Values
- Web interface allows setting accelerometer orientation values
- ⚠️ These values may not be properly transmitted to connected devices (iPhone)
- Affects Link 3 print mode selection based on device orientation
- Needs further testing and validation

### Bluetooth Packet Captures

Reference packet captures are available in the `Bluetooth Packet Capture/` folder:

**`iPhone_INSTAX_capture-5.pklg`**
- iPhone Bluetooth trace to real INSTAX Mini Link 3 printer
- Includes: Print job sequence, accelerometer movement responses
- Can be viewed with: Wireshark, Packet Logger (macOS)
- Useful for: Protocol validation, timing analysis, characteristic value verification

**Capturing Your Own Traces (iOS):**
1. Install "Bluetooth Packet Logger" from Xcode Additional Tools
2. Run on macOS while iPhone is connected via cable
3. Perform print operations on iPhone
4. Export `.pklg` file for analysis

## Future Enhancements

### PlatformIO Support
**Desired:** Port this project (or create a parallel version) to work with [PlatformIO](https://platformio.org/) in Visual Studio Code.

**Benefits:**
- Modern IDE with better code navigation and IntelliSense
- Simplified dependency management
- Cross-platform development (Windows, macOS, Linux)
- Integrated debugging and serial monitor
- Library ecosystem compatibility

**Current Status:** Project uses ESP-IDF native build system (`idf.py`)

**Implementation Notes:**
- Would require `platformio.ini` configuration
- May need adjustments for ESP-IDF component structure
- NimBLE stack compatibility verification needed
- See [PlatformIO ESP-IDF platform](https://docs.platformio.org/en/latest/platforms/espressif32.html)

**Contribution Welcome:** This would be a valuable addition for developers more comfortable with VS Code than command-line ESP-IDF workflow.

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

See [LICENSE.md](LICENSE.md) for full license details.

This project is part of the Moments Print suite. Based on reverse-engineered Instax protocol - for educational and archival purposes only.

## Credits

- **AI Development:** Developed with [Anthropic Claude](https://www.anthropic.com/) (Sonnet 4.5)
- Instax protocol research: [javl/InstaxBLE](https://github.com/javl/InstaxBLE)
- Python implementation reference: [jpwsutton/instax_api](https://github.com/jpwsutton/instax_api)
- ESP-IDF framework: [Espressif](https://github.com/espressif/esp-idf)
- Protocol testing: Real INSTAX Mini Link 3, Square Link, and Wide Link printers

---

## Project Status

**Built with:** ESP-IDF v6.1 on macOS
**Hardware:** ESP32-WROOM-32 (4MB flash)
**Development:** Anthropic Claude (Sonnet 4.5)

**Implementation Status:**
- ✅ **Mini Link 1/2:** Full protocol support (protocol verified correct)
- ✅ **Mini Link 3:** Complete including Link 3-specific services (`0000D0FF`, `00006287`)
- ✅ **Square Link:** Full protocol support (protocol verified correct)
- ✅ **Wide Link:** Complete including Wide-specific service (`0000E0FF`)
- ❌ **Official Apps:** All official apps crash or show errors (Mini/Square crash, Wide shows "Printer Busy")
- ✅ **Third-Party Apps:** Full compatibility (Moments Print, nRF Connect work perfectly on all models)
