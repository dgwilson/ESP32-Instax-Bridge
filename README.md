# ESP32 Instax Printer Emulator

ESP32 firmware that **emulates** a Fujifilm Instax Link printer over Bluetooth LE. Acts as a virtual Instax printer that your photo apps can connect to, capturing and storing the print jobs sent to it.

> **Development:** This project was developed with assistance from **Anthropic Claude** (Sonnet 4.5 / Opus 4.5), leveraging AI-assisted protocol analysis and firmware implementation.

## What This Does

Your ESP32 **pretends to be** an Instax printer (Mini/Wide/Square). When your app (or any Instax-compatible app) tries to print a photo, it connects to the ESP32 instead of a real printer. The printer simulator was developed as a development aid to prove the print abilities of an application without wasting film continuously. **All three official Fujifilm INSTAX apps (Mini, Square, Wide) now print successfully to this simulator**, demonstrating complete protocol compatibility.

The ESP32:
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

### ✅ All Three Official INSTAX Apps Now Print Successfully!

After extensive protocol reverse-engineering and iterative refinement, the ESP32 simulator achieves **full compatibility** with all official Fujifilm INSTAX apps:

| Official App | Compatibility | Notes |
|--------------|---------------|-------|
| **INSTAX Mini Link** | ✅ **Working** | Full print support, connects and prints successfully |
| **INSTAX Square Link** | ✅ **Working** | Full print support, connects and prints successfully |
| **INSTAX Wide Link** | ✅ **Working** | Full print support, connects and prints successfully |

This represents a significant milestone - the protocol implementation is now accurate enough that official Fujifilm apps cannot distinguish the ESP32 simulator from a real INSTAX printer.

**All Compatible Apps:**
- ✅ **INSTAX Mini Link** (official) - Full print support
- ✅ **INSTAX Square Link** (official) - Full print support
- ✅ **INSTAX Wide Link** (official) - Full print support
- ✅ **Moments Print** (custom app) - Works perfectly with all models
- ✅ **nRF Connect** - Full BLE characteristic access for testing

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
model mini|wide|square             # Set printer model (short form)
printer_model mini|wide|square     # Set printer model (long form)
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

### BLE Advertising Names

When the ESP32 advertises, it uses model-specific device names that match real INSTAX printers:

| Model | BLE Device Name | Notes |
|-------|-----------------|-------|
| **Mini** | `INSTAX-70555555(BLE)` | 8-digit ID with (BLE) suffix |
| **Square** | `INSTAX-50555555(IOS)` | 8-digit ID with (IOS) suffix |
| **Wide** | `INSTAX-205555` | 6-digit ID, no suffix |

These names appear in your phone's Bluetooth scanner and in the official INSTAX apps. The naming format matches real printers so apps recognize the simulator as a genuine device.

### 4. Connect from Your App

1. Run `ble_start` in the console
2. Open the official INSTAX app or any Instax-compatible photo app
3. Scan for printers - you'll see the device name (e.g., `INSTAX-70555555(BLE)` for Mini)
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
- [jpwsutton/instax_api](https://github.com/jpwsutton/instax_api/issues/21#issuecomment-1352639100) - protocol discovery chat
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
- Wide: ~225 KB (verified from protocol, 200+ KB prints tested successfully)

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

- **AI Development:** Developed with [Anthropic Claude](https://www.anthropic.com/) (Sonnet 4.5 / Opus 4.5)
- Instax protocol research: [javl/InstaxBLE](https://github.com/javl/InstaxBLE)
- Python implementation reference: [jpwsutton/instax_api](https://github.com/jpwsutton/instax_api)
- ESP-IDF framework: [Espressif](https://github.com/espressif/esp-idf)
- Protocol testing: Real INSTAX Mini Link 3, Square Link, and Wide Link printers

---

## Project Status

**Built with:** ESP-IDF v6.1 on macOS
**Hardware:** ESP32-WROOM-32 (4MB flash)
**Development:** Anthropic Claude (Sonnet 4.5 / Opus 4.5)

**Implementation Status:**
- ✅ **Mini Link 1/2:** Full protocol support, prints work perfectly
- ✅ **Mini Link 3:** Complete including Link 3-specific services (`0000D0FF`, `00006287`)
- ✅ **Square Link:** Full protocol support, prints work perfectly
- ✅ **Wide Link:** Complete including Wide-specific service (`0000E0FF`)
- ✅ **Official Apps:** All three official INSTAX apps (Mini, Square, Wide) connect and print successfully!
- ✅ **Third-Party Apps:** Full compatibility (Moments Print, nRF Connect work perfectly on all models)

---

## Help - I Want to Write My Own Printing Application

This section provides guidance for developers who want to build their own application that prints to INSTAX printers.

### Essential Documentation

1. **[INSTAX_PROTOCOL.md](INSTAX_PROTOCOL.md)** - Complete protocol specification including:
   - BLE service and characteristic UUIDs
   - Packet structure and checksums
   - Connection handshake sequence (10 stages)
   - Print job flow (START → DATA → END → EXECUTE)
   - Model-specific parameters and quirks

2. **Reference Implementations:**
   - [javl/InstaxBLE](https://github.com/javl/InstaxBLE) - Python implementation (great for understanding the protocol)
   - [jpwsutton/instax_api](https://github.com/jpwsutton/instax_api) - Another Python reference
   - This ESP32 firmware - C implementation in `main/ble_peripheral.c` and `main/instax_protocol.c`

### Key Concepts

**1. Bluetooth LE Connection**
```
Service UUID: 70954782-2d83-473d-9e5f-81e1d02d5273
Write Characteristic: 70954783-2d83-473d-9e5f-81e1d02d5273  (send commands here)
Notify Characteristic: 70954784-2d83-473d-9e5f-81e1d02d5273 (receive responses here)
```

**2. Packet Structure**
Every packet follows this format:
```
[Header: 41 62] [Length: 2 bytes] [Function: 1 byte] [Operation: 1 byte] [Payload: N bytes] [Checksum: 1 byte]
```
- Header is always `0x41 0x62` ("Ab")
- Length is little-endian, includes function + operation + payload + checksum
- Checksum is XOR of all bytes from function through end of payload

**3. Connection Handshake**
Before printing, you must complete the connection sequence:
1. Subscribe to notify characteristic
2. Query device info (function 0x00, various operations)
3. Query battery, film count, dimensions
4. Printer responds with capabilities

**4. Print Job Flow**
```
1. PRINT_START (0x10, 0x00)  → Send image size, receive ACK
2. PRINT_DATA  (0x10, 0x01)  → Send image chunks, wait for ACK after each
3. PRINT_END   (0x10, 0x02)  → Signal upload complete, receive ACK
4. PRINT_EXEC  (0x10, 0x80)  → Trigger actual printing
```

**5. ACK-Based Flow Control (Recommended)**

The printer sends an acknowledgement after each DATA packet. Wait for this ACK before sending the next packet - this automatically adapts to any printer speed and eliminates timing issues:
```
App: Send DATA packet 1
Printer: ACK (0x10, 0x01 response)
App: Send DATA packet 2
Printer: ACK
... repeat until all chunks sent ...
```

This is more reliable than fixed timing delays and works with both physical printers and the ESP32 simulator.

**6. Image Preparation**
- Convert image to JPEG at printer's native resolution
- Mini: 600×800px, Square: 800×800px, Wide: 1260×840px
- Compress to fit within file size limits (see below)
- Link 3 only: Flip image vertically before sending

### Model-Specific Requirements

| Model | Resolution | Chunk Size | Max File Size | Special Notes |
|-------|-----------|------------|---------------|---------------|
| Mini Link 1/2 | 600×800 | 900 bytes | 105 KB | Standard protocol |
| Mini Link 3 | 600×800 | 900 bytes | 55 KB | Vertical flip required |
| Square Link | 800×800 | 1808 bytes | 105 KB | 1-second delay before EXECUTE |
| Wide Link | 1260×840 | 900 bytes | ~225 KB | Uses additional service `0000E0FF` |

**Flow Control:** Use ACK-based flow control (wait for printer acknowledgement after each DATA packet) rather than fixed timing delays. This automatically adapts to printer speed and works reliably across all models.

### Development Workflow

**Step 1: Use This Simulator**

Don't waste film during development! Use the ESP32 simulator:
1. Flash this firmware to an ESP32
2. Run `model mini` (or square/wide) and `ble_start`
3. Connect your app to the simulator
4. View received prints at `http://instax-simulator.local`
5. Iterate until prints look correct

**Step 2: Test with Real Printer**

Once your app works with the simulator:
1. Connect to a real INSTAX printer
2. Start with a few test prints
3. Verify colors, orientation, and framing

### Common Pitfalls

1. **Forgetting the checksum** - Every packet needs a valid XOR checksum
2. **Wrong byte order** - Length fields are little-endian
3. **Not waiting for ACKs** - Always wait for printer acknowledgement before sending the next DATA packet
4. **Wrong image orientation** - Link 3 requires vertical flip; others don't
5. **File too large** - Compress JPEG to fit within model's limit
6. **Missing handshake** - Must query printer info before printing
7. **Skipping the pre-EXECUTE delay** - Square Link needs a 1-second pause before EXECUTE command

### Platform-Specific Notes

**iOS/macOS (CoreBluetooth):**
- Use `CBCentralManager` to scan and connect
- Write to characteristic with `.withResponse` for reliable delivery
- Handle background modes if app needs to print while backgrounded

**Android (Android BLE):**
- Request `BLUETOOTH_CONNECT` and `BLUETOOTH_SCAN` permissions (Android 12+)
- Use `BluetoothGattCallback` for responses
- Consider using a BLE library like RxAndroidBle for easier async handling

**Python (bleak):**
- Great for prototyping and testing
- See [javl/InstaxBLE](https://github.com/javl/InstaxBLE) for working example
- Can run on Raspberry Pi for embedded projects

**Web (Web Bluetooth):**
- Limited browser support (Chrome/Edge on desktop)
- Good for quick demos and testing tools
- See [Web Bluetooth API](https://developer.mozilla.org/en-US/docs/Web/API/Web_Bluetooth_API)

### Getting Help

- **Protocol questions:** Check [INSTAX_PROTOCOL.md](INSTAX_PROTOCOL.md) first
- **Implementation issues:** Study the reference implementations
- **Simulator problems:** Open an issue on this repository
- **Community discussion:** See [jpwsutton/instax_api issues](https://github.com/jpwsutton/instax_api/issues) for protocol discovery history
