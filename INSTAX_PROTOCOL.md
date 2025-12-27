# Instax Printer Protocol Documentation

This document describes the protocols used to communicate with Fujifilm Instax printers, covering both Bluetooth LE (Link series) and WiFi (SP series) models.

**Primary Implementation Based on:** [javl/InstaxBLE](https://github.com/javl/InstaxBLE) - Bluetooth protocol for Link printers
**Additional Reference:** [jpwsutton/instax_api](https://github.com/jpwsutton/instax_api) - WiFi protocol for SP-2/SP-3 printers
**Last Updated:** December 27, 2025 (Wide Link Official App Printing - COMPLETE SUCCESS! Print START ACK fix, status query fix)

---

## Table of Contents

1. [Overview](#overview)
2. [Protocol Comparison: Bluetooth vs WiFi](#protocol-comparison-bluetooth-vs-wifi)
3. [Bluetooth Services & Characteristics](#bluetooth-services--characteristics)
4. [Packet Structure](#packet-structure)
5. [Event Types](#event-types)
6. [Info Types](#info-types)
7. [Command Sequences](#command-sequences)
8. [Model-Specific Details](#model-specific-details)
9. [Link 3 Specifics](#link-3-specifics)
10. [Error Codes](#error-codes)
11. [Missing Features & Future Work](#missing-features--future-work)
12. [Examples](#examples)
13. [Official Instax App Compatibility](#official-instax-app-compatibility)

---

## Overview

Fujifilm manufactures two distinct lines of wireless Instax printers, each using a different communication protocol:

### **Link Series (Bluetooth LE) - THIS IMPLEMENTATION**
- **Models:** Instax Mini Link (1/2/3), Instax Square Link, Instax Wide Link
- **Communication:** Bluetooth Low Energy (BLE) via GATT characteristics
- **Connection:** Direct device-to-device pairing
- **Implementation:** This project (Moments Print)
- **Reference:** [javl/InstaxBLE](https://github.com/javl/InstaxBLE)

### **SP Series (WiFi)**
- **Models:** Instax SP-2, Instax SP-3
- **Communication:** WiFi TCP socket (IP: 192.168.0.251, Port: 8080)
- **Connection:** Connect to printer's WiFi hotspot (INSTAX-XXXXXXXX)
- **Implementation:** [jpwsutton/instax_api](https://github.com/jpwsutton/instax_api) (Python)
- **Note:** Completely different protocol and command set

**⚠️ Important:** These are fundamentally incompatible protocols. BLE Link commands do not work on WiFi SP printers and vice versa. This document primarily covers the **Bluetooth LE protocol** used by Link printers.

### Bluetooth Protocol Characteristics

The Instax Link printers use a packet-based command/response system where:

- Client sends commands to the printer via the **Write Characteristic**
- Printer sends responses via the **Notify Characteristic**
- All multi-byte integers use **big-endian** byte order
- Packets include a checksum for data integrity

---

## Protocol Comparison: Bluetooth vs WiFi

| Feature | Link (Bluetooth) | SP (WiFi) |
|---------|------------------|-----------|
| **Communication** | BLE GATT | TCP Socket |
| **Connection** | Bluetooth pairing | WiFi hotspot |
| **Packet Header** | `0x41 0x62` (to device)<br>`0x61 0x42` (from device) | `0x24` (command)<br>`0x2A` (response) |
| **Command Format** | Function + Operation bytes | Command type byte |
| **Reset Command** | `0x01 0x01` (may not be fully implemented) | `0x50` (fully supported) |
| **Lock Commands** | Not documented | `0xB0`, `0xB3`, `0xB6` (supported) |
| **Battery Query** | `0x00 0x01` (0-100% scale) | Header byte 15 (0-7 scale) |
| **Print Count** | Not yet implemented | `0xC1` (lifetime count) |
| **Firmware Version** | Not yet implemented | `0xC0` (version string) |
| **Model Name** | BLE characteristic read | `0xC2` (model string) |
| **Image Rotation** | Vertical flip required | 90° rotation in encoding |
| **Max MTU** | 182 bytes (BLE) | Larger (TCP) |

**Key Takeaway:** Commands from the WiFi SP protocol cannot be directly ported to the Bluetooth Link protocol. However, the WiFi protocol reveals features (like reset, lock, print count) that may have Bluetooth equivalents that need to be discovered through packet sniffing.

---

## Bluetooth Services & Characteristics

### Standard Instax Print Service (All Models)

| Type | UUID | Properties |
|------|------|------------|
| Service | `70954782-2d83-473d-9e5f-81e1d02d5273` | - |
| Write Characteristic | `70954783-2d83-473d-9e5f-81e1d02d5273` | Write Without Response |
| Notify Characteristic | `70954784-2d83-473d-9e5f-81e1d02d5273` | Notify |

### Device Information Service (Standard BLE)

| Type | UUID | Properties |
|------|------|------------|
| Service | `180A` | - |
| Model Number | `2A24` | Read |

**Model Number Values:**
- Mini Link 3: `"FI033"`
- Mini Link 2: `"FI032"` (unconfirmed)
- Mini Link 1: `"FI031"` (unconfirmed)
- Square Link: `"FI017"`
- Wide Link: **`"BO-22"`** ⚠️ (not "FI022" - verified Dec 27, 2025 via real packet capture)

**Model-Specific Service Summary:**

| Model | Print Service | Model-Specific Services | DIS | Notes |
|-------|--------------|-------------------------|-----|-------|
| **Mini Link 3** | ✅ `70954782...` | ✅ Link 3 Info (`0000D0FF...`)<br>✅ Link 3 Status (`00006287...`) | ✅ | Requires both Link 3 services for official app |
| **Mini Link 1/2** | ✅ `70954782...` | ❌ None | ✅ | Uses standard services only |
| **Square Link** | ✅ `70954782...` | ❌ None | ✅ | Uses standard services only |
| **Wide Link** | ✅ `70954782...` | ✅ Wide Service (`0000E0FF...`) | ✅ | Uses different service UUID than Link 3 |

> **Key Insight:** Each printer model has a unique BLE profile. Mini Link 3 and Wide Link require model-specific services for official app compatibility, while Square and older Mini models use only the standard print service.

### Link 3-Specific Services

**CRITICAL:** These services are REQUIRED for official INSTAX Mini Link app compatibility. Without them, the app will not discover or connect to the device.

**Link 3 Status Service:**
- Service UUID: `00006287-3C17-D293-8E48-14FE2E4DA212`
- Control Characteristic: `00006387-3C17-D293-8E48-14FE2E4DA212` (Read/Write)
- Status Characteristic: `00006487-3C17-D293-8E48-14FE2E4DA212` (Notify)

**Link 3 Info Service (Battery/Film/Device Data):**
- Service UUID: `0000D0FF-3C17-D293-8E48-14FE2E4DA212`

**Link 3 Info Service Characteristics:**

All characteristics use the custom base UUID: `0000XXXX-3C17-D293-8E48-14FE2E4DA212`

| Characteristic UUID | Properties | Size | Description | Real Mini Link 3 Value |
|---------------------|------------|------|-------------|------------------------|
| `0000FFD1-3C17...` | Read | 4 bytes | Unknown identifier | `00 00 00 00` |
| `0000FFD2-3C17...` | Read/Notify | 12 bytes | Device identifier | `88 B4 36 86 18 4E 00 00 00 00 00 00` |
| `0000FFD3-3C17...` | - | - | Not supported (returns ATT error) | - |
| `0000FFD4-3C17...` | - | - | Not supported (returns ATT error) | - |
| `0000FFE0-3C17...` | Read | 20 bytes | Unknown device info | `00 00 00 00 02 40 25 00 02 00 E0 EE 33 65 00 00 33 65 00 00` |
| `0000FFE1-3C17...` | Read | 8 bytes | Unknown device info | `CE 63 00 00 12 00 00 01` |
| `0000FFF1-3C17...` | Read | 12 bytes | **Battery & Film Count**<br>Byte 0: Photos remaining (0-10)<br>Byte 8: Battery (0-200 scale)<br>Byte 9: Charging (0xFF = not charging) | Dynamic based on printer state |
| `0000FFF3-3C17...` | Read | 2 bytes | Unknown status | `10 00` |
| `0000FFF4-3C17...` | Read | 20 bytes | Possibly accelerometer data | `00 30 00 00 00 C0 01 00 00 F0 04 00 00 B0 00 00 00 50 01 00` |
| `0000FFF5-3C17...` | Read | 8 bytes | Unknown device info | `00 30 00 00 00 40 01 00` |

**Key Findings:**
- The Link 3 Info Service provides an alternative method to query battery/film count (via FFF1 characteristic)
- Unlike standard protocol queries (Function 0x00, Operation 0x01/0x02), these values are exposed as BLE characteristics
- The official INSTAX Mini Link app reads these characteristics during connection validation
- Most characteristic values are static device identifiers; only FFF1 contains dynamic printer state

> **Note:** Link 3 uses the standard print service for actual printing operations. The Link 3-specific services appear to be for status monitoring, device identification, and app validation only.

### Wide-Specific Service

**CRITICAL:** Wide Link printers use a DIFFERENT service UUID than Mini Link 3. The Wide service is REQUIRED for official INSTAX Wide Link app compatibility.

**Wide Service:**
- Service UUID: `0000E0FF-3C17-D293-8E48-14FE2E4DA212`

**Wide Service Characteristics:**

All characteristics use the custom base UUID: `0000XXXX-3C17-D293-8E48-14FE2E4DA212`

| Characteristic UUID | Properties | Data Size | Description |
|---------------------|------------|-----------|-------------|
| `0000FFE1-3C17...` | **Write/WriteNoResponse/Notify** | 12 bytes | **Battery & Film Count** (Status Request)<br>App writes to request status, printer responds via notification<br>Byte 0: Photos remaining (0-10)<br>Byte 1: Ready status (0x01 = ready, 0x00 = busy)<br>Bytes 2-7: Status bytes (0x00, 0x15, 0x00, 0x00, 0x4F, 0x00)<br>Byte 8: Battery level (0-200 scale, 0-100%)<br>Byte 9: Charging status (0xFF = not charging, 0x00 = charging)<br>Bytes 10-11: Status bytes (0x0F, 0x00) |
| `0000FFE9-3C17...` | Write | Variable | **Control/Command** (Printer Ready Check)<br>App writes to check printer readiness before printing<br>When written, printer should send FFEA notification to confirm ready status<br>Without this response, official app shows "Printer Busy (1)" error |
| `0000FFEA-3C17...` | Notify | 11 bytes | **Printer Ready Status** (CRITICAL)<br>Must send notification when client subscribes<br>Data pattern: `02 09 B9 00 11 01 00 80 84 1E 00`<br>Without this notification, official app shows "Printer Busy (1)" error |

**⚠️ CRITICAL: FFE1 is NOT a Read characteristic!**

The FFE1 characteristic uses Write/WriteNoResponse for **requests** and Notify for **responses**. This is a common misunderstanding:
- ❌ **WRONG:** `BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY` - Will cause "Printer Busy" errors
- ✅ **CORRECT:** `BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP | BLE_GATT_CHR_F_NOTIFY`

When the app writes to FFE1 (any data), the printer must respond with a 12-byte notification containing current status.

**FFEA Notification Details:**

The FFEA characteristic is **CRITICAL** for official INSTAX Wide app compatibility. When the app subscribes to FFEA notifications, the printer must immediately send an 11-byte status notification:

```
Hex Data: 02 09 B9 00 11 01 00 80 84 1E 00

Byte Breakdown (preliminary analysis):
  [0] = 0x02 - Message type identifier
  [1] = 0x09 - Unknown
  [2-3] = 0xB9 0x00 - Unknown (uint16 little-endian = 185)
  [4] = 0x11 - Unknown
  [5] = 0x01 - Printer ready status (0x01 = ready)
  [6] = 0x00 - Unknown
  [7] = 0x80 - Status flags (bit pattern: 1000 0000)
  [8] = 0x84 - Status flags (bit pattern: 1000 0100)
  [9] = 0x1E - Unknown (decimal 30)
  [10] = 0x00 - Terminator
```

**Implementation Note:** The exact meaning of each byte in the FFEA notification is not yet fully understood. The data pattern above is captured from a real Wide Link printer and must be sent exactly as shown. Without this notification, the official INSTAX Wide app will refuse to print and display "Printer Busy (1)" error, even if FFE1 shows the printer as ready.

**Key Differences from Link 3:**
- Wide uses service `0000E0FF` (NOT `0000D0FF` like Mini Link 3)
- Wide has only 3 characteristics (vs 10+ for Link 3)
- Wide does NOT advertise the Link 3 Status Service (`00006287`)
- Wide requires **FFEA notification** for app compatibility (Link 3 does not have equivalent)
- Wide-specific characteristic data needs to be captured from real device during active use

**Implementation Status:**
- ✅ Service and characteristic UUIDs confirmed via nRF Connect scan
- ✅ **FFEA notification implemented** from real printer packet capture (December 2025)
- ✅ FFE1 battery/film count data confirmed from real printer
- ✅ Simulator advertises Wide service when model is set to Wide
- ✅ Official INSTAX Wide app compatibility confirmed (connects and shows printer ready)

> **Note:** Like Link 3 and Square, Wide uses the standard print service (`70954782-2d83-473d-9e5f-81e1d02d5273`) for actual printing operations. The Wide-specific service appears to be for device identification and app validation.

---

## Packet Structure

### Packet Format

All packets follow this structure:

```
+--------+--------+--------+--------+--------+--------+-----------+-----------+
| Header | Header | Length | Length | OpCode | OpCode |  Payload  | Checksum  |
|  (H1)  |  (H2)  | (High) | (Low)  | (Func) | (Op)   | (0-N bytes)|  (1 byte) |
+--------+--------+--------+--------+--------+--------+-----------+-----------+
  0x41     0x62     MSB      LSB      Byte     Byte    Variable      1 byte
  (2 bytes)        (2 bytes)         (2 bytes)
```

### Field Details

| Field | Size | Description | Example |
|-------|------|-------------|---------|
| **Header** | 2 bytes | Fixed header `0x41 0x62` ("Ab") for client→printer<br>`0x61 0x42` ("aB") for printer→client | `41 62` |
| **Length** | 2 bytes | Total packet size (big-endian)<br>**CRITICAL:** Length = 7 + payload length | `00 07` (empty payload) |
| **OpCode** | 2 bytes | Function byte + Operation byte | `00 00` (info query) |
| **Payload** | Variable | Command-specific data | (varies) |
| **Checksum** | 1 byte | Calculated checksum | (varies) |

### Length Calculation

The length field represents the **total packet size**, not just the payload:

```
Length = Header(2) + Length(2) + OpCode(2) + Payload(N) + Checksum(1)
       = 7 + N
```

**Example:**
- Empty payload: Length = `0x0007` (7 bytes total)
- 4-byte payload: Length = `0x000B` (11 bytes total)
- 900-byte payload: Length = `0x038B` (907 bytes total)

### Checksum Calculation

The checksum ensures packet integrity:

```swift
// Calculate checksum
var sum: UInt8 = 0
sum = sum &+ header[0]      // 0x41
sum = sum &+ header[1]      // 0x62
sum = sum &+ lengthHigh     // MSB of length
sum = sum &+ lengthLow      // LSB of length
sum = sum &+ function       // Function byte
sum = sum &+ operation      // Operation byte
for byte in payload {
    sum = sum &+ byte
}
checksum = (255 - sum) & 255
```

**Validation:** A valid packet's bytes should sum to `255` (mod 256).

---

## Event Types

Event types are 2-byte operation codes representing different commands.

### Info Query Commands (Function = 0x00)

| Command | Bytes | Description |
|---------|-------|-------------|
| `SUPPORT_FUNCTION_AND_VERSION_INFO` | `00 00` | Get printer version info |
| `DEVICE_INFO_SERVICE` | `00 01` | Get battery info |
| `SUPPORT_FUNCTION_INFO` | `00 02` | Get photos remaining/charging status |
| `IDENTIFY_INFORMATION` | `00 10` | Get device identification |

### Print Commands (Function = 0x10)

| Command | Bytes | Description |
|---------|-------|-------------|
| `PRINT_IMAGE_DOWNLOAD_START` | `10 00` | Begin image upload, send image size |
| `PRINT_IMAGE_DOWNLOAD_DATA` | `10 01` | Upload image data chunk |
| `PRINT_IMAGE_DOWNLOAD_END` | `10 02` | End image upload |
| `PRINT_IMAGE_DOWNLOAD_CANCEL` | `10 03` | Cancel current upload |
| `PRINT_IMAGE` | `10 80` | Execute print command |
| `REJECT_FILM_COVER` | `10 81` | Eject film (no print) |

### Device Control Commands (Function = 0x01)

| Command | Bytes | Description |
|---------|-------|-------------|
| `SHUT_DOWN` | `01 00` | Power off printer |
| `RESET` | `01 01` | Reset printer |
| `AUTO_SLEEP_SETTINGS` | `01 02` | Configure auto-sleep |
| `BLE_CONNECT` | `01 03` | BLE connection management |

### LED & Sensor Commands (Function = 0x30)

| Command | Bytes | Description |
|---------|-------|-------------|
| `XYZ_AXIS_INFO` | `30 00` | Get accelerometer data |
| `LED_PATTERN_SETTINGS` | `30 01` | Set LED pattern (single) |
| `AXIS_ACTION_SETTINGS` | `30 02` | Configure motion actions |
| `LED_PATTERN_SETTINGS_DOUBLE` | `30 03` | Set LED pattern (double) |
| `POWER_ONOFF_LED_SETTING` | `30 04` | Configure power LED |
| `AR_LED_VIBRATION_SETTING` | `30 06` | AR mode LED/vibration |
| `ADDITIONAL_PRINTER_INFO` | `30 10` | Get extended printer info |

### Firmware Commands (Function = 0x20)

| Command | Bytes | Description |
|---------|-------|-------------|
| `FW_DOWNLOAD_START` | `20 00` | Start firmware upload |
| `FW_DOWNLOAD_DATA` | `20 01` | Upload firmware chunk |
| `FW_DOWNLOAD_END` | `20 02` | End firmware upload |
| `FW_UPGRADE_EXIT` | `20 03` | Exit firmware mode |
| `FW_PROGRAM_INFO` | `20 10` | Get firmware version |
| `FW_DATA_BACKUP` | `20 80` | Backup firmware |
| `FW_UPDATE_REQUEST` | `20 81` | Request firmware update |

---

## Info Types

When querying device information using `DEVICE_INFO_SERVICE` (0x00, 0x01), the **operation byte in the response** indicates the info type:

| Info Type | Operation | Payload Format | Description |
|-----------|-----------|----------------|-------------|
| `IMAGE_SUPPORT_INFO` | `0x00` | `[W_H, W_L, H_H, H_L]` | Image dimensions (16-bit width, 16-bit height) |
| `BATTERY_INFO` | `0x01` | `[State, Percentage]` | Battery state and charge percentage (0-100)<br>**⚠️ Model-specific:** Byte 8 = `0x01` (Wide), `0x03` (Mini/Square) |
| `PRINTER_FUNCTION_INFO` | `0x02` | `[FunctionByte]` | Photos remaining (bits 0-3), charging flag (bit 7)<br>**⚠️ Model-specific:** Square/Wide use capability byte nibble, older Mini uses payload[5] - see parsing section below |
| `PRINT_HISTORY_INFO` | `0x03` | (varies) | Print history data |

### Parsing Info Responses

#### Image Support Info (Operation 0x00)
```swift
let width = (UInt16(payload[0]) << 8) | UInt16(payload[1])
let height = (UInt16(payload[2]) << 8) | UInt16(payload[3])
// Mini: width=600, height=800
// Square: width=800, height=800
// Wide: width=1260, height=840
```

#### Battery Info (Operation 0x01)
```swift
let batteryState = payload[0]    // Charging state (0=not charging, 1=charging)
let batteryLevel = payload[1]    // Percentage 0-100
```

#### Printer Function Info (Operation 0x02)

**⚠️ CRITICAL: Model-Specific Film Count Encoding (Updated December 2025)**

Film count location varies by printer model:

**Square Link (FI017) & Wide Link (FI022):**
- Film count stored in **capability byte lower nibble** (bits 0-3)
- Payload[5] contains 0x0C (12) - purpose unknown, NOT film count
- Verified with real printers

**Older Mini Link 1/2 (FI031/FI032):**
- Film count stored in **payload[5]** (full byte)
- Capability byte lower nibble unused for film count
- Not verified with real printer

**Detection Method:**
```swift
let capabilityByte = payload[0]

// Detect model by capability byte range:
// - Square Link: 0x20-0x2F (capability byte lower nibble = film count) ✅ VERIFIED
// - Wide Link: 0x30-0x3F (capability byte lower nibble = film count) ✅ VERIFIED
// - Older Mini: 0x10-0x1F (payload[5] = film count, unverified)
let isSquareOrWideLink = (capabilityByte >= 0x20 && capabilityByte < 0x40)

let photosRemaining: Int
if isSquareOrWideLink {
    // Square/Wide: Extract lower 4 bits (nibble) of capability byte
    photosRemaining = Int(capabilityByte & 0x0F)
} else {
    // Older Mini: Full byte at payload[5]
    photosRemaining = Int(payload[5])
}

let isCharging = (capabilityByte & 0x80) != 0  // Bit 7: charging status
```

**Example Response - Square Link with 6 films:**
```
Payload: 00 02 26 00 00 0C 00 00 00 00
              ^^          ^^
Capability: 0x26 = 0010 0110
  Upper bits: 0x20 (Square Link model identifier)
  Lower nibble: 0x06 = 6 photos remaining ✅
Payload[5]: 0x0C = 12 (NOT film count - purpose unknown)
```

**Example Response - Wide Link with 4 films:**
```
Payload: 00 02 34 00 00 0C 00 00 00 00
              ^^          ^^
Capability: 0x34 = 0011 0100
  Upper bits: 0x30 (Wide Link model identifier)
  Lower nibble: 0x04 = 4 photos remaining ✅
Payload[5]: 0x0C = 12 (NOT film count - purpose unknown)
```

**Capability Byte Format:**
```
Bit 7: Charging status (1 = charging, 0 = not charging)
Bits 4-6: Model identifier
  - 001 (0x10): Older Mini Link 1/2
  - 010 (0x20): Square Link
  - 011 (0x30): Wide Link / Mini Link 3
Bits 0-3: Film count for Square/Wide/Mini3 (0-10)
          Unused for older Mini (uses payload[5])
```

---

## Command Sequences

### Basic Print Sequence

1. **Query Printer Info** (optional but recommended)
   ```
   Send: DEVICE_INFO_SERVICE (0x00, 0x01) with empty payload
   Receive: Image dimensions response

   Send: DEVICE_INFO_SERVICE (0x00, 0x01) with empty payload
   Receive: Battery info response

   Send: SUPPORT_FUNCTION_INFO (0x00, 0x02) with empty payload
   Receive: Photos remaining response
   ```

2. **Upload Image**
   ```
   Send: PRINT_IMAGE_DOWNLOAD_START (0x10, 0x00)
         Payload: [0x02, 0x00, 0x00, 0x00, size_H, size_HM, size_LM, size_L]

   Send: PRINT_IMAGE_DOWNLOAD_DATA (0x10, 0x01) [repeated for each chunk]
         Payload: [chunk_H, chunk_HM, chunk_LM, chunk_L, ...image_data...]

   Send: PRINT_IMAGE_DOWNLOAD_END (0x10, 0x02)
         Payload: empty
   ```

3. **Execute Print**
   ```
   Send: PRINT_IMAGE (0x10, 0x80)
         Payload: empty
   ```

4. **Wait for Completion**
   - Keep connection alive for 2-5 minutes
   - Monitor notify characteristic for status updates
   - Printer may disconnect when done

### Print Start Payload Format

```
Byte 0-3:  [0x02, 0x00, 0x00, 0x00]  // Fixed header
Byte 4-7:  [Size MSB → LSB]          // 32-bit big-endian image size
```

**Example:** For 45,678 bytes (0x0000B26E)
```
Payload: 02 00 00 00 00 00 B2 6E
```

### Print Data Payload Format

```
Byte 0-3:  [Chunk Index MSB → LSB]   // 32-bit big-endian chunk number
Byte 4+:   [...image data...]        // JPEG chunk data
```

**Example:** Chunk 0 with 900 bytes of data
```
Payload: 00 00 00 00 [900 bytes of JPEG data]
```

**Example:** Chunk 5 with 900 bytes of data
```
Payload: 00 00 00 05 [900 bytes of JPEG data]
```

### Chunking Rules

- **Chunk size** varies by model (see Model-Specific Details)
- **Last chunk** must be zero-padded to chunk size
- **Chunk index** starts at 0 and increments for each packet
- Each chunk is sent in a separate `PRINT_IMAGE_DOWNLOAD_DATA` packet

**Example Chunking:**
```
Image size: 2100 bytes
Chunk size: 900 bytes
Result: 3 chunks

Chunk 0: bytes 0-899 (900 bytes)
Chunk 1: bytes 900-1799 (900 bytes)
Chunk 2: bytes 1800-2099 + 600 bytes of 0x00 padding (900 bytes)
```

---

## Model-Specific Details

### File Size Limit Discussion

**⚠️ Important Note on File Size Limits**

The IMAGE_SUPPORT_INFO protocol response reports theoretical maximum file sizes in the last 4 bytes:
- **Wide**: `0x52800` = 337,920 bytes (330 KB) - [24Nov - actual max size is believed to be 105KB - also see note below]
- **Square**: `0x064000` = 409,600 bytes (400 KB) - [24Nov - actual max size is believed to be 105KB - also see note below]
- **Mini Link 1/2**: 105 KB (approximate)
- **Mini Link 3**: 55 KB (firmware-limited)

**However, this implementation uses 105 KB uniformly for all models (except Link 3 at 55 KB) based on:**

1. **Reference Implementation:** The [javl/InstaxBLE](https://github.com/javl/InstaxBLE) repository uses a hardcoded 105 KB limit for all models in production code
2. **Proven Reliability:** Extensive testing confirms 105 KB works consistently across all printer models
3. **Unvalidated Higher Limits:** While the protocol claims higher limits, community testing has not fully validated these work reliably in practice
4. **Implementation Bugs:** [Issue #18](https://github.com/javl/InstaxBLE/issues/18) reports a buffer corruption bug that may affect images above 105 KB

**Research References:**
- [InstaxBLE Issue #18](https://github.com/javl/InstaxBLE/issues/18) - Discussion of IMAGE_SUPPORT_INFO max_size_kb field
- [InstaxBLE Issue #15](https://github.com/javl/InstaxBLE/issues/15) - JPEG file size transfer limits

**Decision:** We are holding at **105 KB** until concrete evidence emerges that higher file sizes work reliably without corruption or transmission issues. The quality at 105 KB has proven sufficient for all tested use cases.

---

### Instax Mini Link

| Parameter | Value |
|-----------|-------|
| **Image Dimensions** | 600 × 800 pixels |
| **Chunk Size** | 900 bytes |
| **Max File Size** | **105 KB** (Link 1/2)<br>**55 KB** (Link 3) |
| **Theoretical Max** | ~105 KB (protocol-reported) |
| **Packet Delay** | **50ms** (Link 1/2)<br>**75ms** (Link 3) |
| **Film Size** | 62 × 46 mm |
| **Photos per Pack** | 10 |
| **BLE Device Name** | `INSTAX-XXXXXXXX(BLE)` |
| **BLE Model Number** | `FI033` (Link 3)<br>`FI032` (Link 2, unconfirmed)<br>`FI031` (Link 1, unconfirmed) |
| **Firmware Revision** | `0101` |

**BLE Services (Link 3 only):**
- Standard Print Service: `70954782-2d83-473d-9e5f-81e1d02d5273`
- Link 3 Info Service: `0000D0FF-3C17-D293-8E48-14FE2E4DA212` (10 characteristics)
- Link 3 Status Service: `00006287-3C17-D293-8E48-14FE2E4DA212` (2 characteristics)
- Device Information Service (DIS)

**Implementation Note:** Link 3's reduced file size limit (55 KB vs 105 KB) is enforced in firmware. The smaller buffer requires more aggressive JPEG compression. Link 3 also requires slower packet transmission (75ms vs 50ms).

**⚠️ Device Name Format:** Mini Link printers advertise with a `(BLE)` suffix in the device name (e.g., `INSTAX-70423278(BLE)`), unlike Square and Wide printers which use just `INSTAX-XXXXXXXX`. The official INSTAX Mini Link app may filter devices by this naming pattern.

**Link 3-Specific BLE Profile:** Mini Link 3 requires two additional BLE services (`0000D0FF` and `00006287`) for official app compatibility. These services expose battery/film status and device identification. See [Link 3-Specific Services](#link-3-specific-services) section for complete characteristic details.

### Instax Square Link

| Parameter | Value |
|-----------|-------|
| **Image Dimensions** | 800 × 800 pixels |
| **Chunk Size** | **1808 bytes** (2x larger than Mini/Wide) |
| **Max File Size** | **105 KB** (working limit) |
| **Theoretical Max** | 400 KB (protocol-reported, unvalidated) |
| **Packet Delay** | **150ms** (physical printer, same as Wide) |
| **Execute Delay** | **1 second** (required before EXECUTE command) |
| **Film Size** | 62 × 62 mm |
| **Photos per Pack** | 10 |
| **BLE Device Name** | `INSTAX-XXXXXXXX` (no suffix) |
| **BLE Model Number** | `FI017` |
| **Firmware Revision** | `0101` |

**BLE Services:**
- Standard Print Service: `70954782-2d83-473d-9e5f-81e1d02d5273`
- Device Information Service (DIS)
- ⚠️ Square does NOT use Link 3-specific services or Wide-specific services

**Implementation Note:** While IMAGE_SUPPORT_INFO reports 400 KB capacity, this implementation uses 105 KB based on proven reliability from reference implementations. Square uses the largest packet size (1808 bytes vs 900 bytes for Mini/Wide), requiring both a longer packet delay (150ms, same as Wide) to allow the printer adequate time to process each chunk, AND a 1-second delay before the EXECUTE command to allow the printer to finish processing the uploaded data.

**⚠️ CRITICAL: Square Link Status Polling Issue**

Square Link **MUST NOT** be polled with status queries during print processing. Sending info queries while the printer is processing causes the firmware to hang/crash, requiring a hard reset. After sending the EXECUTE command, wait silently for 2 minutes without sending any commands. The printer will complete processing and may disconnect automatically when done. This behavior matches the Python reference implementation which uses a simple 60-second sleep after printing.

**Success Response Code:** Square Link returns error code **12 (0x000C)** after successful EXECUTE commands. This is NOT an error - it indicates the print completed successfully. This is similar to Wide Link returning code 15 and Link 3 returning code 16.

**🔍 Additional Settings (Not Yet Implemented):**

Square Link supports several additional settings available in the official Instax app:

**Print Modes:**
- **INSTAX-Rich Mode (Default):** Enhanced contrast and saturation for vivid colors
- **INSTAX-Natural Mode:** More natural, subdued color reproduction

**Power-On LED Light:**
- **Rainbow:** Multi-color rainbow gradient animation
- **Warm color graduation:** Warm tones (red/orange/yellow) gradient
- **Cool color graduation:** Cool tones (blue/cyan/purple) gradient
- **White:** Simple white light

The Bluetooth commands to query and set these modes are currently unknown. The current implementation always uses the default modes (Rich for print, and whatever LED pattern was last set in the official app). See the [Square Link Print Modes Discovery](#square-link-print-modes-discovery-needed) and [Power-On LED Light Settings Discovery](#power-on-led-light-settings-discovery-needed) sections for investigation methodology.

Higher file size limits remain unvalidated.

### Instax Wide Link

| Parameter | Value |
|-----------|-------|
| **Image Dimensions** | 1260 × 840 pixels |
| **Chunk Size** | 900 bytes |
| **Max File Size** | **~225 KB** (verified from print START ACK) |
| **Theoretical Max** | 330 KB (protocol-reported) |
| **Packet Delay** | **150ms** (physical printer) |
| **Film Size** | 99 × 62 mm |
| **Photos per Pack** | 10 |
| **BLE Device Name** | `INSTAX-XXXXXXXX` (no suffix) |
| **BLE Model Number** | **`BO-22`** ⚠️ (not "FI022" - verified Dec 2025) |
| **Firmware Revision** | `0100` (vs `0101` for Mini/Square) |

> **⚠️ Model Code Update (December 27, 2025):** Real iPhone packet capture confirmed Wide Link reports model code **"BO-22"**, not "FI022" as previously documented. The Wide Link is an older model (micro USB vs USB-C on newer printers). The "BO" prefix may be an earlier Fujifilm naming convention before standardizing on "FI0xx".

**BLE Services:**
- Standard Print Service: `70954782-2d83-473d-9e5f-81e1d02d5273`
- Wide-Specific Service: `0000E0FF-3C17-D293-8E48-14FE2E4DA212` (3 characteristics)
- Device Information Service (DIS)

**Implementation Note (Updated December 27, 2025):** Real iPhone packet capture of successful Wide print shows:
- Print START ACK is **12 bytes**: `61 42 00 0C 10 00 00 00 00 03 84 B9`
- **CRITICAL:** `03 84` is the **chunk size** (900 bytes), NOT max file size!
- `B9` is the **checksum**, NOT part of a 3-byte size field!
- Real print jobs successfully transfer ~200 KB images
- Official app tested with 200,208 byte image - complete success

Wide requires the longest packet delay (150ms) despite using the same 900-byte packet size as Mini, likely due to the larger overall image size requiring more processing time.

**Print START ACK Format (Wide Link - Verified December 2025):**
```
Byte:   0  1  2  3  4  5  6  7  8  9 10 11
Hex:   61 42 00 0C 10 00 00 00 00 03 84 B9
       └──┘ └──┘ └──┘ └──────┘ └──┘ └──┘
       Hdr  Len  F Op  Status  Chunk Chk
                       +Pad    Size
```
- **Length (0x000C = 12):** Total packet length
- **Function (0x10):** PRINT
- **Operation (0x00):** START (response)
- **Status (0x00):** Success
- **Padding (0x00 0x00):** Two zero bytes
- **Chunk Size (0x0384 = 900):** Bytes per DATA packet
- **Checksum (0xB9):** (255 - sum of bytes 0-10) & 0xFF

**Wide-Specific BLE Profile:** Unlike Mini Link 3 (which uses service `0000D0FF`), Wide Link uses service `0000E0FF` with only 3 characteristics (FFE1, FFE9, FFEA). Wide does NOT advertise the Link 3 Status Service. See [Wide-Specific Service](#wide-specific-service) section for complete characteristic details.

**🔍 Critical Protocol Differences (Updated December 27, 2025):**

Wide Link has **model-specific protocol responses** that differ from Mini and Square:

1. **Ping Response (Function 0x00, Operation 0x00) - Bytes 7 & 9:**
   - Wide Link: Byte 7 = `0x01`, Byte 9 = `0x01` (mirrors byte 7)
   - Mini/Square: Byte 7 = `0x02`, Byte 9 = `0x02`
   - Real Wide (BO-22): `61 42 00 10 00 00 00 [01] 00 [01] 00 00 00 00 00 4A`
   - Mini Link 3: `61 42 00 10 00 00 00 [02] 00 [02] 00 00 00 00 00 49`

2. **Dimensions Response (Function 0x00, Operation 0x02, Type 0x00):**
   - Wide Link: **19 bytes total** (length = 0x13)
   - Mini/Square: 23 bytes total (length = 0x17)
   - Wide-specific capability bytes at positions 12-17: `02 7B 00 05 28 00`
   - Real Wide: `61 42 00 13 00 02 00 00 04 EC 03 48 02 7B 00 05 28 00 62`
     - Width: 0x04EC = 1260
     - Height: 0x0348 = 840
     - Capability bytes differ from Mini/Square

3. **Battery Info Response (Function 0x00, Operation 0x02, Type 0x01):**
   - **⚠️ CRITICAL (Fixed Dec 27, 2025):** Byte 8 is the **BUSY FLAG**
   - Wide Link: Byte 8 = `0x02` (READY), `0x01` = BUSY!
   - Mini Link: Byte 8 = `0x03`
   - Real Wide (ready): `61 42 00 0D 00 02 00 01 [02] 41 00 10 F9`
   - Mini Link 3: `61 42 00 0D 00 02 00 01 [03] 50 00 10 E9`
   - Byte 9: Battery percentage (0x41 = 65%, 0x50 = 80%)

4. **Printer Function Response (Function 0x00, Operation 0x02, Type 0x02):**
   - **⚠️ UPDATED December 27, 2025:** Wide Link uses base `0x20` (same as Square)
   - Capability byte (byte 8 in full packet / payload[2]) encodes film count in lower nibble
   - Real Wide printer response (4 films): `61 42 00 11 00 02 00 02 [24] 00 00 0D 00 00 00 00 16`
     - Capability: `0x24` = `0x20` (Wide base) + `0x04` (4 films) ✅ VERIFIED
     - Payload[5] (byte 11): `0x0D` (13) - NOT film count, purpose unknown
   - Compare to Square (6 films): `61 42 00 11 00 02 00 02 [26] 00 00 0C 00 00 00 00 XX`
     - Capability: `0x26` = `0x20` (Square base) + `0x06` (6 films) ✅ VERIFIED
   - Compare to Mini: `61 42 00 11 00 02 00 02 [38] 00 00 08 00 00 00 00 13`
     - Capability: `0x38` = `0x30` base + `0x08` photos (encoded)

5. **Additional Info Type 0x01 Response (Function 0x30, Operation 0x10, Type 0x01):**
   - Wide Link: Specific bytes at positions 11-15: `1E 00 01 01 00`
   - Mini/Square: Different pattern at these positions
   - Real Wide: `61 42 00 15 30 10 00 01 00 00 00 29 1E 00 01 01 00 00 00 00 XX`
   - These bytes may indicate Wide-specific hardware capabilities

**Official App Compatibility Notes (Updated December 2025):**
- ✅ **FFE1 characteristic fixed:** Must be Write/WriteNoResponse/Notify (NOT Read/Notify)
- ✅ Official app writes to FFE1 to request status, expects notification response
- ✅ Third-party apps (Moments Print) work perfectly with same protocol implementation
- 🔧 Previous "Printer Busy (1)" error was caused by incorrect FFE1 characteristic configuration
- All standard protocol responses have been verified byte-for-byte against real Wide printer

### Image Processing Requirements

All models require:
- **Format:** JPEG
- **Color Space:** RGB
- **EXIF:** Stripped (to reduce file size)
- **Quality:** Reduced as needed to meet file size limit
- **Dimensions:** Exact pixel dimensions for target model

---

## Link 3 Specifics

The Instax Mini Link 3 has several differences from earlier models:

### Protocol Differences

1. **File Size Limit:** Maximum 55 KB (vs 105 KB for Link 1/2)
   - Reason: Unknown, possibly due to smaller memory buffer
   - Workaround: More aggressive JPEG compression required

2. **Processing Time:** 2-5 minutes after upload
   - Keep connection alive during this period
   - Monitor status notifications for errors
   - Printer may disconnect automatically when done

3. **Standard Protocol Support:**
   - Link 3 **DOES** support standard print commands (0x10, 0x00-0x80)
   - Link 3 **MAY NOT** support standard info queries (0x00, 0x00-0x02)
   - If info queries timeout, use manual model selection

4. **LED Pattern Command:**
   - `LED_PATTERN_SETTINGS_DOUBLE` (0x30, 0x03) is **NOT** required for printing
   - Initially thought necessary, but later found to be unrelated
   - Can be safely omitted from print sequence

### Link 3 Detection

**Method 1:** Read Model Number characteristic (0x2A24)
```swift
if modelNumber.contains("FI033") {
    // This is a Link 3
}
```

**Method 2:** Check for Link 3-specific services
```swift
if peripheral.services?.contains(where: {
    $0.uuid.uuidString.contains("6287")
}) == true {
    // This is a Link 3
}
```

### Link 3 Timing Requirements (CRITICAL)

**Upload Speed:**
- Link 3 requires **50ms delay** between data packets (vs 10ms for older models)
- Too fast = yellow blinking light error (film jam indication)
- Packet sequence: START → DATA (50ms) → DATA (50ms) → ... → END

**Pre-Execute Delay:**
- **1 second delay required** after PRINT_IMAGE_DOWNLOAD_END before PRINT_IMAGE (execute)
- This allows printer to process uploaded data
- Reference: GitHub issue #21 mentions 300ms minimum, 1s is conservative

**Implementation:**
```swift
// For Link 3
if isLink3Detected {
    delay = 0.05  // 50ms between packets
}

// Before EXECUTE command
if nextPacket == PRINT_IMAGE {
    delay = 1.0  // 1 second before execute
}
```

**Without proper timing:**
- Yellow blinking light (error)
- No print output
- Printer appears to jam

**With proper timing:**
- Green processing lights
- Successful print output
- 2-5 minute processing time

**ESP32 Simulator Note:** The ESP32 Instax Bridge simulator requires much longer delays (130ms for Mini) compared to physical Link 3 printers (50ms). See [Timing Considerations](#timing-considerations) section for ESP32-specific values.

### Link 3 Image Orientation (CRITICAL)

**Discovered Issue:** Link 3 requires images to be **vertically flipped** before upload.

**Transform Required:**
```swift
// In Core Graphics rendering context
ctx.scaleBy(x: 1.0, y: -1.0)  // Vertical flip only
```

**Why:**
- Printer's internal coordinate system is inverted
- Without flip: image prints upside down
- Horizontal flip is NOT needed (causes mirroring)

**Verified Results:**
- ✅ Vertical flip (y: -1.0) = correct orientation
- ❌ No flip = upside down
- ❌ Horizontal flip (x: -1.0) = mirrored left-right
- ❌ Both flips (x: -1.0, y: -1.0) = correct vertical, but mirrored horizontal

### Link 3 Status Monitoring

The Link 3 provides additional status through its dedicated services:

**Status Service (00006287):**
- Provides error notifications during print
- Monitor characteristic 00006487 for status updates
- Status codes indicate film jams, cover open, low battery, etc.

**Info Service (0000D0FF) - Characteristic FFF1:**
The FFF1 characteristic contains printer status information:

| Byte | Description | Format | Example |
|------|-------------|--------|---------|
| 0 | Photos remaining | 0-10 | `0x06` = 6 photos left |
| 8 | Battery level (raw) | 0-200 | `0xC0` = 192 = 96% |
| 9 | Charging status | 0xFF when NOT charging | `0xFF` = NOT charging |

**Battery Conversion:**
```swift
let batteryRaw = data[8]
let batteryPercentage = (batteryRaw * 100) / 200  // 0-200 → 0-100%
```

**Charging Status:**
```swift
let isCharging = data[9] != 0xFF  // 0xFF = NOT charging
```

**Example FFF1 Data:**
```
06 01 00 15 00 00 4f 00 c0 ff 0f 00
```
- Byte 0: `0x06` = **6 photos remaining**
- Byte 8: `0xC0` = 192 → **(192 × 100) / 200 = 96% battery**
- Byte 9: `0xFF` = **NOT charging**

This matches the Python implementation output:
```
Photos left:         6/10
Battery level:       96% (rounded to 95%)
Charging:            False (byte 9 = 0xFF)
```

### Link 3 Accelerometer & Camera Integration

The Instax Mini Link 3 includes an integrated accelerometer that enables gesture-based camera controls when used with the official Instax app.

#### Accelerometer Data Query

**Command:** `XYZ_AXIS_INFO` (Function=0x30, Operation=0x00)

**Request Packet:**
```
Header:   41 62
Length:   00 07
OpCode:   30 00  (XYZ_AXIS_INFO)
Payload:  (empty)
Checksum: XX
```

**Response Format:**
```
Header:   61 42
Length:   00 0D (13 bytes total)
OpCode:   30 00
Payload:  [x_low] [x_high] [y_low] [y_high] [z_low] [z_high] [orientation]
Checksum: XX
```

**Data Structure:**
- **X Axis**: Signed 16-bit integer (little-endian) - tilt left/right
- **Y Axis**: Signed 16-bit integer (little-endian) - tilt forward/backward
- **Z Axis**: Signed 16-bit integer (little-endian) - rotation
- **Orientation**: Unsigned 8-bit value - device orientation state

**Value Ranges:**
- X, Y, Z axes: Typical range **-1000 to +1000**
- Orientation: **0-255**

**Captured Sample from Physical Mini Link:**
```
Hex: 01 0b 0b 00 19 00 bf fe 2d fe db ff 7b 87
     [Header.......] [X....][Y....][Z....][O][CK]

Decoded Values:
  X-axis: 0xFEBF = -321 (tilted left)
  Y-axis: 0xFE2D = -467 (tilted forward)
  Z-axis: 0xFFDB = -37 (slight rotation)
  Orientation: 0x7B = 123
  Checksum: 0x87 = 135
```

**Python Unpacking:**
```python
import struct
x, y, z, o = struct.unpack_from('<hhhB', response_payload)
```

**C Structure:**
```c
typedef struct {
    int16_t x;           // X-axis tilt (left/right)
    int16_t y;           // Y-axis tilt (forward/backward)
    int16_t z;           // Z-axis rotation
    uint8_t orientation; // Orientation state
} instax_accelerometer_data_t;
```

#### Camera Mode Features (iOS/Android App Integration)

**⚠️ Note:** These features are implemented in the official Instax app's camera mode, NOT in the BLE protocol itself. The app reads accelerometer data from the printer and interprets it to control the phone's camera.

##### Tilt-to-Zoom

<img src="/Users/dgwilson/Downloads/instax ios camera support 1 Medium.jpeg" width="300px" alt="Tilt to zoom diagram">

**Usage:**
1. Hold printer vertically
2. Tilt printer **inward** (toward phone) → zoom in
3. Tilt printer **outward** (away from phone) → zoom out

**Implementation:** App polls XYZ_AXIS_INFO repeatedly and maps Y-axis tilt angle to camera zoom level.

##### Shutter Button Trigger

<img src="/Users/dgwilson/Downloads/instax ios camera support 2 Medium.jpeg" width="300px" alt="Shutter button diagram">

**Usage:**
1. Press printer **power button once** → triggers camera shutter
2. App captures photo using phone camera
3. Photo can then be printed to the Instax printer

**Implementation:** The power button press likely generates a BLE notification or state change that the app monitors, though the specific characteristic/event has not been reverse-engineered yet.

**⚠️ Command 0x23 - Shutter/Trigger Status (Newly Discovered - Dec 2025)**

**Response Format:**
```
Hex: 01 23 0d 00 19 00 01 02 0b 00 00 00 00 00 00 00
     [Header.......] [Data payload...................]

Packet Structure:
  Command ID: 0x23
  Length: 13 bytes (0x0D)
  Parameter: 0x0019
  Payload: 01 02 0b 00 00 00 00 00 00 00
```

**Analysis:**
- Appears in capture after accelerometer queries and before print commands
- Byte 8 (0x0b) may reference accelerometer command
- Purpose likely related to shutter button capability or trigger status
- May be used for camera mode readiness check

**Status:** Command identified but exact purpose unclear. Further investigation needed to determine:
- GET request structure for command 0x23
- Whether this is a status query or configuration command
- Relationship to power button press event
- Exact timing relative to shutter trigger

#### Research Status

**✅ Documented (Updated December 2025):**
- XYZ_AXIS_INFO command structure (Function=0x30, Operation=0x00)
- Accelerometer data format (3x int16 + 1x uint8, little-endian)
- **Accelerometer value ranges (-1000 to +1000 for X/Y/Z)** ✅
- **Actual captured accelerometer values from physical printer** ✅
- Tilt-to-zoom user interaction pattern
- Shutter button user interaction pattern
- **Command 0x23 identified in shutter-related context** ✅

**❓ Unknown / Needs Investigation:**
- **Command 0x23 exact purpose and GET request structure**
- **Shutter button trigger event mechanism**
- Orientation byte interpretation (0x00-0xFF meaning)
- AXIS_ACTION_SETTINGS (0x30, 0x02) payload format and purpose
- Accelerometer polling rate recommendations
- Whether shutter button uses standard HID characteristic or custom notification

**📚 References:**
- [javl/InstaxBLE](https://github.com/javl/InstaxBLE) - Python implementation with `get_printer_orientation()` method
- [InstaxBLE Types.py](https://github.com/javl/InstaxBLE/blob/main/Types.py) - Event type definitions
- Instax Mini Link 3 User Manual - Camera mode feature descriptions
- **Bluetooth Packet Capture:** `iPhone_INSTAX_capture-4.pklg` (Mini Link + accelerometer + shutter button, Dec 2025)

**🔬 Future Work:**
1. ~~Capture BLE packets during shutter button press~~ ✅ Captured - command 0x23 identified
2. ~~Determine accelerometer value ranges~~ ✅ Confirmed: -1000 to +1000 typical range
3. Decode command 0x23 payload structure and purpose
4. Identify GET request for command 0x23
5. Map accelerometer value ranges to real-world angles (degrees of tilt)
6. Determine orientation byte state meanings (portrait/landscape/upside-down)
7. Investigate AXIS_ACTION_SETTINGS configuration options
8. ~~Implement simulator support for virtual accelerometer values~~ ✅ Already implemented in web interface

---

## Error Codes

Error codes are returned in the response payload. The format varies by response type:

| Code | Hex | Description | User Message |
|------|-----|-------------|--------------|
| **0** | `0x0000` | Success | (no error) |
| **1** | `0x0001` | Success with Info | Command acknowledged successfully. Some printers return this instead of 0. |
| **12** | `0x000C` | Success (Square Link) | Success response from Square Link printers. Print completes successfully. |
| **15** | `0x000F` | Success (Wide Link) | Success response from Wide Link printers. Print completes successfully. |
| **16** | `0x0010` | Success (Link 3) | Success response from Link 3 printers. Print completes successfully. |
| **256** | `0x0100` | Success (Alternative) | Success response variant seen in some printer models. |
| **178** | `0x00B2` | No Film | "There is no film in the printer. Please load a pack of film and try again." |
| **179** | `0x00B3` | Cover Open | "The printer cover is open. Please close it and try again." |
| **180** | `0x00B4` | Low Battery | "The printer battery is too low. Please charge the printer." |
| **181** | `0x00B5` | Printer Busy | "The printer is busy. Please wait and try again." |

**Error Response Format:**

Response codes are encoded differently depending on the command:

**Standard Responses (2-byte format):**
```
Payload: [Error_High, Error_Low, ...]
Example: [00 00] = Success (0)
Example: [00 B2] = Error 178 (No Film)
```

**EXECUTE Command Response (1-byte format):**
```
Payload: [Error_Code]
Example: [00] = Success (0)
Example: [B2] = Error 178 (No Film)

Full packet example (No Film error):
61 42 00 08 10 80 B2 12
         │  │  │  └─ Checksum
         │  │  └─ Error code (178 = No Film)
         │  └─ Operation (128 = EXECUTE)
         └─ Function (16 = Print)
```

**Success Code Details:**
- **Code 0 (0x0000):** Standard success response
- **Code 1 (0x0001):** Success variant - some printer models return this to acknowledge command success
- **Code 12 (0x000C):** Success response from Square Link printers - confirmed that prints complete successfully
- **Code 15 (0x000F):** Success response from Wide Link printers - confirmed that prints complete successfully
- **Code 16 (0x0010):** Success response from Link 3 printers - confirmed that prints complete successfully
- **Code 256 (0x0100):** Alternative success response seen in certain printer firmware versions
- All six codes indicate successful command execution and should NOT be treated as errors

**Code 12 Notes (Square Link Specific):**
- Appears after successful upload and EXECUTE command on Square Link printers
- Initially appeared to be an error code, but confirmed that prints complete successfully
- Should be treated as a success code, not an error
- Not documented in official materials or Python implementation
- Requires slower packet timing (150ms) and 1-second pre-EXECUTE delay
- CRITICAL: Square Link firmware crashes if polled for status during print processing

**Code 15 Notes (Wide Link Specific):**
- Appears after successful upload and EXECUTE command on Wide Link printers
- Initially appeared to be an error code, but confirmed that prints complete successfully
- Should be treated as a success code, not an error
- Not documented in official materials or Python implementation
- Requires slower packet timing (150ms) compared to other models

**Code 16 Notes (Link 3 Specific):**
- Appears after successful upload and EXECUTE command on Link 3 printers
- Initially appeared to be a warning/error code, but confirmed that prints complete successfully
- Should be treated as a success code, not an error
- Not documented in official materials or Python implementation

---

## Missing Features & Future Work

This section documents features found in the WiFi protocol or official Instax app that are not yet implemented in the Bluetooth Link protocol.

### ✅ Implemented Features

| Feature | Status | Notes |
|---------|--------|-------|
| Battery Level Query | ✅ Implemented | Via `DEVICE_INFO_SERVICE` (0x00, 0x01) - Returns 0-100% |
| Photos Remaining | ✅ Implemented | Via `SUPPORT_FUNCTION_INFO` (0x00, 0x02) - Returns 0-10 count |
| Charging Status | ✅ Implemented | Via `SUPPORT_FUNCTION_INFO` (0x00, 0x02) - Bit 7 |
| Image Dimensions | ✅ Implemented | Via `SUPPORT_FUNCTION_AND_VERSION_INFO` (0x00, 0x00) |
| Print Operations | ✅ Implemented | START, DATA, END, EXECUTE commands |
| Link 3 Compatibility | ✅ Implemented | 50ms delays, 55KB limit, vertical flip |

### ⚠️ Partially Implemented Features

| Feature | Status | Notes |
|---------|--------|-------|
| **Reset Command** | ⚠️ Defined but untested | Command exists (`RESET` 0x01, 0x01) but not verified on Link printers |
| **Power Off Command** | ⚠️ Defined but untested | Command exists (`SHUT_DOWN` 0x01, 0x00) but not verified |

### ❌ Missing Features (Found in WiFi Protocol)

These features exist in the WiFi SP-2/SP-3 protocol but have not been discovered or documented for Bluetooth Link printers:

| Feature | WiFi Command | Bluetooth Equivalent | Priority |
|---------|--------------|---------------------|----------|
| **Lifetime Print Count** | `0xC1` | Unknown | Medium |
| **Firmware Version Query** | `0xC0` | Unknown | Low |
| **Model Name Query** | `0xC2` | Use BLE characteristic | N/A |
| **Lock Printer** | `0xB3` | Unknown | Medium |
| **Query Lock State** | `0xB0` | Unknown | Medium |
| **Change PIN/Password** | `0xB6` | Unknown | Low |
| **Print Status Monitoring** | `0xC3` (Type 195) | Unknown | Medium |

### 🔍 Square Link Print Modes Discovery Needed

**Critical Finding:** Square Link printers support two print modes that affect color output and contrast, but the Bluetooth commands to query and set these modes are currently unknown.

#### What We Know:
- **INSTAX-Rich Mode (Default):** Enhanced contrast and saturation for vivid colors
- **INSTAX-Natural Mode:** More natural, subdued color reproduction
- **Official app:** Allows switching between modes before printing
- **Current implementation:** Always uses default mode (Rich) - no way to change it

#### Print Mode Commands (Unknown)

The Bluetooth protocol commands to control print modes have not been discovered:

```
Query Current Mode:    Unknown command
Set Mode to Natural:   Unknown command
Set Mode to Rich:      Unknown command
```

**Suspected Command Locations:**
1. **Device Control Function (0x01):** May have a mode-setting operation
2. **LED/Sensor Function (0x30):** Possibly under "additional printer info"
3. **New Function:** May use an undiscovered function code specific to Square Link
4. **Characteristic Write:** Could be controlled via a Square Link-specific BLE characteristic

#### Investigation Needed:

**Option 1: Bluetooth Packet Sniffing**
1. Use nRF Connect or Wireshark with Bluetooth adapter
2. Connect official Instax app to Square Link printer
3. Switch between Natural and Rich modes in the app
4. Capture packets sent to printer
5. Identify function/operation bytes and payload format

**Option 2: Brute Force Command Discovery**
1. Test known info query commands (0x00, 0x00 through 0x00, 0xFF)
2. Test device control commands (0x01, 0x03 through 0x01, 0xFF)
3. Test LED/sensor commands (0x30, 0x10 through 0x30, 0xFF)
4. Monitor for responses that indicate mode information

**Option 3: Characteristic Exploration**
1. Read all Square Link-specific BLE characteristics
2. Compare values before/after mode change in official app
3. Identify characteristic that stores/controls mode setting

**Expected Results:**
- Command format for querying current print mode (Natural vs Rich)
- Command format for setting print mode before print job
- Whether mode setting persists across power cycles or resets per print
- If mode affects JPEG compression or is a printer-side post-processing setting

#### Implementation Priority: **HIGH**

Print mode significantly affects output quality and user preference. Many users prefer Natural mode for portraits and Rich mode for landscapes. This is a key feature for professional photo booth use cases.

---

### 🔍 Power-On LED Light Settings Discovery Needed

**Critical Finding:** Square Link (and possibly other Link printers) support configurable LED light patterns that glow when the printer powers on, but the Bluetooth command payload format is currently unknown.

#### What We Know:

**Available LED Patterns in Official App:**
- **Rainbow:** Multi-color rainbow gradient animation
- **Warm color graduation:** Warm tones (red/orange/yellow) gradient
- **Cool color graduation:** Cool tones (blue/cyan/purple) gradient
- **White:** Simple white light

**Confirmed on Models:**
- Square Link ✅ (confirmed 4 pattern options)
- Link 3 (unknown - needs verification)
- Mini Link 1/2 (unknown - needs verification)
- Wide Link (unknown - needs verification)

**Protocol Information:**
- **Likely command:** `POWER_ONOFF_LED_SETTING` (Function=0x30, Operation=0x04)
- **Alternative:** `LED_PATTERN_SETTINGS` (0x30, 0x01) or `LED_PATTERN_SETTINGS_DOUBLE` (0x30, 0x03)
- Payload format is unknown

#### POWER_ONOFF_LED_SETTING Command (0x30, 0x04)

This command is defined in the LED & Sensor commands but its payload format is not documented:

```
Function:  0x30 (LED & Sensor)
Operation: 0x04 (POWER_ONOFF_LED_SETTING)
Payload:   Unknown format
```

**Suspected Payload Format (needs verification):**
```
Based on 4 known patterns:

Likely formats:
1. Single byte enum:
   0x00 = Rainbow
   0x01 = Warm color graduation
   0x02 = Cool color graduation
   0x03 = White

2. Pattern ID + Parameters:
   [pattern_id, brightness, speed]
   [0x00, 0xFF, 0x50] = Rainbow, full brightness, medium speed

3. RGB color values:
   [R, G, B, mode] = Custom color with gradient mode
```

#### Investigation Needed:

**Option 1: Bluetooth Packet Sniffing (RECOMMENDED)**
1. Use nRF Connect or Wireshark with Bluetooth adapter
2. Connect official Instax app to Square Link printer
3. Change LED power-on setting through each of the 4 patterns
4. Capture packets sent to printer for each pattern change
5. Identify `POWER_ONOFF_LED_SETTING` (0x30, 0x04) command and payload
6. Test on other printer models to verify if feature exists

**Option 2: Brute Force Command Testing**
1. Send `POWER_ONOFF_LED_SETTING` command with various payloads:
   - `[0x00]` through `[0x03]` (pattern enum?)
   - `[0x00, 0xFF, 0x50]` (pattern + brightness + speed?)
   - RGB combinations with mode bytes
2. Power cycle printer after each command
3. Observe LED pattern on power-up to verify change

**Option 3: Explore Other LED Commands**
1. Test `LED_PATTERN_SETTINGS` (0x30, 0x01) with various payloads
2. Test `LED_PATTERN_SETTINGS_DOUBLE` (0x30, 0x03)
3. Monitor printer response to identify correct command

**Expected Results:**
- Command format for `POWER_ONOFF_LED_SETTING` (0x30, 0x04)
- Query command to read current LED setting (if exists)
- Payload format mapping each byte to pattern selection
- Whether setting persists across power cycles
- Confirmation of which printer models support this feature

#### Implementation Priority: **MEDIUM**

This is a cosmetic feature that enhances user experience but doesn't affect print functionality. Useful for:
- **Event branding** - Match LED colors to event theme
- **User preference** - Some users may prefer subtle white vs colorful rainbow
- **Photo booth aesthetics** - Coordinated lighting during operation

**Use Case Example:** At a corporate event with specific brand colors (e.g., blue/white), setting "Cool color graduation" matches the printer's LED to the event branding.

---

### 🔍 Link 3 Camera Features Discovery Needed

**Critical Finding:** The Instax Mini Link 3 includes an integrated accelerometer and shutter button functionality for camera control when used with the official Instax app, but several protocol details remain unknown.

#### What We Know:

**Accelerometer Integration:**
- **XYZ_AXIS_INFO Command** (Function=0x30, Operation=0x00) ✅ Documented
- Returns accelerometer data: 3x int16 (x, y, z axes) + 1x uint8 (orientation)
- Used by official app for tilt-to-zoom camera control
- Y-axis tilt angle mapped to camera zoom level

**Shutter Button Feature:**
- Press printer **power button once** → triggers camera shutter
- Official app polls for shutter button press events
- Actual BLE notification mechanism is unknown

**Confirmed on Models:**
- Link 3 ✅ (all features confirmed)
- Other models: No accelerometer/camera features

#### What Needs Investigation:

**❓ Unknown / Needs Discovery:**

1. **Shutter Button BLE Notification Mechanism** (Priority: HIGH)
   - How does the printer notify the app when power button is pressed?
   - Is it a standard HID characteristic or custom notification?
   - What is the notification payload format?
   - Does it use the Instax service characteristic or a separate one?

2. **Accelerometer Value Ranges** (Priority: MEDIUM)
   - Exact min/max values for each axis (x, y, z)
   - Mapping of accelerometer values to real-world angles (degrees of tilt)
   - Calibration data or scaling factors

3. **Orientation Byte Interpretation** (Priority: MEDIUM)
   - What do orientation byte values (0x00-0xFF) represent?
   - Portrait/landscape/upside-down detection?
   - Rotation angle encoding?

4. **AXIS_ACTION_SETTINGS Command** (0x30, 0x02) (Priority: LOW)
   - Payload format unknown
   - Configuration options for accelerometer behavior
   - Sensitivity settings or axis remapping?

5. **Accelerometer Polling Rate** (Priority: LOW)
   - Optimal polling frequency for smooth tilt-to-zoom
   - Does printer buffer readings or provide latest only?

#### Investigation Needed:

**Option 1: Bluetooth Packet Sniffing (RECOMMENDED)**
1. Use nRF Connect or Wireshark with Bluetooth adapter
2. Connect official Instax app to Link 3 printer in camera mode
3. **For shutter button:**
   - Press power button while monitoring BLE traffic
   - Identify notification characteristic and payload
4. **For accelerometer:**
   - Tilt printer to extreme angles while monitoring responses
   - Map accelerometer values to tilt angles
   - Rotate printer to determine orientation byte meanings

**Option 2: Characteristic Exploration**
1. Monitor all Link 3 BLE characteristics during camera mode
2. Press power button and observe which characteristic sends notification
3. Test direct subscription to identified characteristic

**Option 3: Reference Implementation Analysis**
- Study [javl/InstaxBLE](https://github.com/javl/InstaxBLE) Python implementation
- Examine `get_printer_orientation()` method for value interpretation
- Check event type definitions for shutter button events

**Expected Results:**
- Shutter button notification characteristic UUID and payload format
- Accelerometer value ranges: X [min, max], Y [min, max], Z [min, max]
- Orientation byte state machine (0x00 = portrait up, 0x01 = landscape left, etc.)
- AXIS_ACTION_SETTINGS payload format and configuration options
- Recommended polling rate (likely 10-30 Hz for smooth zoom)

#### Implementation Priority: **HIGH**

Camera control is a key differentiator for Link 3 and enables unique user experiences:
- **Hands-free selfie mode** - Tilt to zoom, button press to capture
- **Instant photo booth** - Physical printer as camera remote
- **Creative photography** - Printer-as-controller for composition

**Use Case Example:** At a photo booth event, users hold the Link 3 printer and tilt it to zoom the camera on their phone, then press the printer's power button to take the photo and print it instantly.

**Simulator Support:** The ESP32 simulator already has UI controls for virtual accelerometer values and a "Press Shutter" button (web interface). Once the notification mechanism is discovered, full simulation of camera mode will be possible.

---

### 🔍 Automatic Power-Off Settings Discovery Needed

**Critical Finding:** All Instax Link printers support configurable automatic power-off timeout settings through the official Instax app, but the Bluetooth commands to query and set these values are currently unknown.

#### What We Know:

**Available Settings in Official App:**
- **5 minutes (Default):** Printer powers off after 5 minutes of inactivity
- **Do not power off:** Printer stays on indefinitely until manually powered off

**Confirmed on Models:**
- Square Link ✅ (confirmed working settings)
- Link 3 (likely - needs verification)
- Mini Link 1/2 (likely - needs verification)
- Wide Link (likely - needs verification)

**Protocol Information:**
- **WiFi protocol:** Does NOT have documented power-off configuration commands
- **Bluetooth protocol:** Has `AUTO_SLEEP_SETTINGS` command (0x01, 0x02) defined but format unknown

#### AUTO_SLEEP_SETTINGS Command (0x01, 0x02)

This command is defined in the Bluetooth protocol event types but its payload format is not documented:

```
Function:  0x01 (Device Control)
Operation: 0x02 (AUTO_SLEEP_SETTINGS)
Payload:   Unknown format
```

**Suspected Payload Format (needs verification):**
```
Based on known settings (5 minutes or disabled):

Likely formats:
1. Single byte enum:
   0x00 = Do not power off (disabled)
   0x05 = 5 minutes (default)

2. Two bytes (timeout + enable):
   [0x05, 0x01] = 5 minutes enabled
   [0x00, 0x00] = Disabled

3. Boolean flag only:
   0x00 = Disabled (do not power off)
   0x01 = Enabled (5 minutes, hardcoded)
```

#### Investigation Needed:

**Option 1: Bluetooth Packet Sniffing (RECOMMENDED)**
1. Use nRF Connect or Wireshark with Bluetooth adapter
2. Connect official Instax app to Square Link printer
3. Toggle between "5 minutes" and "Do not power off" settings
4. Capture packets sent to printer during each change
5. Identify `AUTO_SLEEP_SETTINGS` (0x01, 0x02) command and payload
6. Test on other printer models to verify consistency

**Option 2: Brute Force Command Testing**
1. Send `AUTO_SLEEP_SETTINGS` command with various payloads:
   - `[0x00]` (disabled?)
   - `[0x01]` (enabled?)
   - `[0x05]` (5 minutes?)
   - `[0x00, 0x00]` (disabled, two-byte format?)
   - `[0x05, 0x01]` (5 min enabled, two-byte format?)
2. Monitor printer behavior after 5+ minutes
3. Query printer info to check if setting was accepted

**Option 3: Characteristic Discovery**
1. Read all printer characteristics before/after changing setting
2. Compare values to identify which characteristic stores power-off mode
3. Test direct characteristic write to change setting

**Expected Results:**
- Command format for `AUTO_SLEEP_SETTINGS` (0x01, 0x02)
- Query command to read current power-off setting (if exists)
- Payload format: `[0x05]` for 5 minutes, `[0x00]` for disabled (likely)
- Whether setting persists across power cycles
- Confirmation that all Link models support same values

#### Implementation Priority: **HIGH**

This is a critical feature for photo booth scenarios where the printer should:
- **Stay awake during long events** - "Do not power off" prevents mid-event disconnections
- **Conserve battery between sessions** - "5 minutes" saves power when not in use
- **Avoid constant re-pairing** - Staying powered on maintains Bluetooth connection

**Use Case Example:** At a 4-hour wedding reception, setting "Do not power off" ensures the printer stays connected for the entire event without requiring manual power-on or re-pairing.

### 📋 Recommendations for Implementation

#### High Priority:

1. **Investigate AUTO_SLEEP_SETTINGS Command**
   - Sniff official app Bluetooth traffic
   - Test different payload formats
   - Document working command structure
   - Add to Swift implementation

2. **Test RESET Command**
   - Verify `RESET` (0x01, 0x01) works on Link printers
   - Document behavior (clears queue, resets settings, etc.)
   - Add to Swift implementation as recovery feature

3. **Implement Print Count Query**
   - If Bluetooth equivalent exists, add to info queries
   - Useful for maintenance tracking (film jams after X prints, etc.)

#### Medium Priority:

4. **Lock/Security Commands**
   - Investigate if Bluetooth Link printers support PIN locking
   - Useful for photo booth security (prevent unauthorized printing)
   - May require packet sniffing of official app

5. **Enhanced Error Handling**
   - Map all error codes from WiFi protocol to Bluetooth equivalents
   - Create comprehensive error enum
   - Provide user-friendly error messages

#### Low Priority:

6. **Firmware Version Query**
   - Nice-to-have for device info display
   - Not critical for printing functionality

---

## Examples

### Example 1: Query Image Support Info

**Request Packet:**
```
Header:   41 62
Length:   00 07        (7 bytes total)
OpCode:   00 00        (SUPPORT_FUNCTION_AND_VERSION_INFO)
Payload:  (empty)
Checksum: F7           (calculated)

Complete: 41 62 00 07 00 00 F7
```

**Response Packet (Mini):**
```
Header:   61 42
Length:   00 0B        (11 bytes total, 4-byte payload)
OpCode:   00 00        (IMAGE_SUPPORT_INFO)
Payload:  02 58 03 20  (width=600, height=800)
Checksum: XX

Parse:
  Width  = (0x02 << 8) | 0x58 = 600
  Height = (0x03 << 8) | 0x20 = 800
  Model  = Mini (600×800)
```

### Example 2: Query Battery Info

**Request Packet:**
```
Header:   41 62
Length:   00 07
OpCode:   00 01        (DEVICE_INFO_SERVICE for battery)
Payload:  (empty)
Checksum: F6

Complete: 41 62 00 07 00 01 F6
```

**Response Packet:**
```
Header:   61 42
Length:   00 09        (9 bytes total, 2-byte payload)
OpCode:   00 01        (BATTERY_INFO)
Payload:  01 64        (charging=yes, level=100%)
Checksum: XX

Parse:
  State      = 0x01 = Charging
  Percentage = 0x64 = 100%
```

### Example 3: Start Print Upload (10KB image)

**Request Packet:**
```
Header:   41 62
Length:   00 0F        (15 bytes total, 8-byte payload)
OpCode:   10 00        (PRINT_IMAGE_DOWNLOAD_START)
Payload:  02 00 00 00  (header)
          00 00 27 10  (size = 10,000 bytes = 0x2710)
Checksum: XX

Complete: 41 62 00 0F 10 00 02 00 00 00 00 00 27 10 XX

Payload breakdown:
  Bytes 0-3: 02 00 00 00 (fixed header)
  Bytes 4-7: 00 00 27 10 (image size in bytes, big-endian)
```

### Example 4: Upload First Data Chunk

**Request Packet:**
```
Header:   41 62
Length:   03 8B        (907 bytes total, 900-byte payload)
OpCode:   10 01        (PRINT_IMAGE_DOWNLOAD_DATA)
Payload:  00 00 00 00  (chunk index = 0)
          [896 bytes of JPEG data]
Checksum: XX

Chunk index breakdown:
  Bytes 0-3: 00 00 00 00 = Chunk 0
  Bytes 4-899: JPEG data
```

### Example 5: End Print Upload

**Request Packet:**
```
Header:   41 62
Length:   00 07
OpCode:   10 02        (PRINT_IMAGE_DOWNLOAD_END)
Payload:  (empty)
Checksum: EF

Complete: 41 62 00 07 10 02 EF
```

### Example 6: Execute Print

**Request Packet:**
```
Header:   41 62
Length:   00 07
OpCode:   10 80        (PRINT_IMAGE)
Payload:  (empty)
Checksum: 6F

Complete: 41 62 00 07 10 80 6F
```

---

## BLE Packet Chunking

Bluetooth LE has a maximum transmission unit (MTU) for each write operation, typically **182 bytes** for Instax printers.

### Splitting Large Packets

If a packet exceeds the BLE MTU, it must be split into multiple BLE writes:

```swift
let maxBLESize = 182
var offset = 0

while offset < packetData.count {
    let chunkSize = min(maxBLESize, packetData.count - offset)
    let chunk = packetData.subdata(in: offset..<offset + chunkSize)

    peripheral.writeValue(chunk, for: writeCharacteristic, type: .withoutResponse)

    offset += chunkSize
}
```

**Important:** Only the Instax protocol packets (image data chunks) need padding. The BLE transmission chunks should **NOT** be padded.

**Example:**
- Instax packet size: 907 bytes (900-byte data chunk + 7-byte protocol overhead)
- BLE MTU: 182 bytes
- Result: 5 BLE writes (182 + 182 + 182 + 182 + 179 bytes)

---

## Connection Management

### Connection Flow

1. **Scan** for devices advertising service `70954782-2d83-473d-9e5f-81e1d02d5273`
2. **Connect** to selected peripheral
3. **Discover** services and characteristics
4. **Enable notifications** on notify characteristic `70954784-2d83-473d-9e5f-81e1d02d5273`
5. **Query** printer info (optional)
6. **Print** image
7. **Wait** for completion (2-5 minutes)
8. **Disconnect** or wait for printer to disconnect

### Timing Considerations

#### Physical Instax Printers

- **Delay between packets:** 10ms recommended for standard models
- **Link 3 delay:** 50ms between packets (see Link 3 Specifics section)
- **Response timeout:** 5 seconds for info queries
- **Print completion:** 2-5 minutes after upload
- **Connection timeout:** Keep alive for full print duration

#### ESP32 Simulator

The ESP32 Instax Bridge simulator requires significantly longer packet delays due to hardware throughput limitations. These values were empirically tested to achieve 100% packet delivery reliability.

**Tested Packet Delays for ESP32 Simulator:**

| Printer Model | Optimal Delay | Minimum Working | Failure Threshold | Total Packets | Packet Size |
|--------------|---------------|-----------------|-------------------|---------------|-------------|
| **Mini Link** | **130ms** | 120ms | 110ms (30% packet loss) | ~60 | 900 bytes |
| **Square Link** | **220ms** | 210ms | 200ms (marginal) | ~60 | 1808 bytes |
| **Wide Link** | **210ms** | 200ms | 180ms (ESP32 crash) | ~115 | 900 bytes |

**Key Findings:**
- **Packet count matters more than packet size:** Wide requires higher delay (210ms) than Mini (130ms) despite identical packet sizes (900 bytes) because Wide transmits nearly twice as many packets (115 vs 60)
- **Square's larger packets:** Square uses the largest packet size (1808 bytes) but only ~60 total packets, requiring 220ms delay
- **ESP32 crash behavior:** Wide at 180ms caused complete ESP32 failure (stopped BLE advertising, required restart)
- **Safety margins:** All optimal delays include 10ms margin above failure threshold for reliability

**Testing Methodology:**
- Start at high delay (250ms) and work downward until packet loss occurs
- Monitor ESP32 web interface for packet ACK counts (sent vs acknowledged)
- Success = 100% packet delivery (all DATA packets acknowledged)
- Failure = packet loss >0% or ESP32 crash

**Implementation Note:** iOS app automatically detects "INSTAX-55550000" or "Instax-Simulator" in device name and applies ESP32 timing defaults. Physical printer defaults remain at 50ms (75ms for Link 3). The ESP32 simulator now uses "INSTAX-55550000" to match official Instax naming patterns (allows testing with official Instax apps), but legacy "Instax-Simulator" name is still supported for backward compatibility.

---

## References

### Bluetooth Protocol (Link Series)
- **Primary Implementation:** [javl/InstaxBLE](https://github.com/javl/InstaxBLE) - Python implementation for Bluetooth Link printers
- **Link 3 Compatibility Discussion:** [Issue #21](https://github.com/javl/InstaxBLE/issues/21)
- **Protocol Reverse Engineering:** Based on Bluetooth packet capture of official Instax app

### WiFi Protocol (SP Series)
- **Python Implementation:** [jpwsutton/instax_api](https://github.com/jpwsutton/instax_api) - WiFi protocol for SP-2/SP-3 printers
- **Protocol Documentation:** [Packet.py source](https://github.com/jpwsutton/instax_api/blob/main/instax/packet.py)
- **Power Management Issue:** [Issue #15](https://github.com/jpwsutton/instax_api/issues/15) - Auto-shutdown cannot be disabled in WiFi protocol

### Tools for Investigation
- **nRF Connect:** [iOS](https://apps.apple.com/us/app/nrf-connect-for-mobile/id1054362403) / [Android](https://play.google.com/store/apps/details?id=no.nordicsemi.android.mcp) - BLE packet capture and characteristic exploration
- **Wireshark:** [Download](https://www.wireshark.org/) - Full Bluetooth/BLE packet capture (requires compatible adapter)
- **PacketLogger (macOS):** Built into Xcode Additional Tools - Bluetooth packet capture on Mac

---

## Official Instax App Compatibility

**Status:** ✅ **FULL COMPATIBILITY ACHIEVED** (December 2025) - All three printer models working!

**Note:** This section documents ESP32 Instax Bridge simulator compatibility with official Instax apps, not core protocol behavior.

### Current Status

The ESP32 Instax Bridge simulator has achieved **complete compatibility** with all official Fujifilm Instax apps:

#### ✅ Mini Link 3 App - FULLY WORKING
- Official app discovers and connects successfully
- Correctly identifies as **Mini Link 3** (not Link 1)
- All protocol queries work (battery, film count, print history)
- Ready for print job testing

#### ✅ Wide Link App - FULLY WORKING (December 27, 2025)
- ✅ **PRINTING WORKS!** Official app successfully prints to ESP32 simulator
- ✅ All protocol responses verified byte-for-byte against real Wide printer
- ✅ Battery info byte 8 = `0x02` (READY), `0x01` = BUSY
- ✅ **Film count encoding same as Square:** Capability byte lower nibble (VERIFIED)
- ✅ Dimensions response = 19 bytes for Wide (vs 23 bytes for Mini/Square)
- ✅ Ping response byte 9 = `0x01` for Wide (vs `0x02` for Mini/Square)
- ✅ Additional Info type 0x01 = Wide-specific bytes at positions 11-15
- ✅ **Third-party apps work perfectly** (Moments Print confirmed working)
- ✅ **FFE1 characteristic: Write/WriteNoResponse/Notify (NOT Read/Notify)**
- ✅ **Print START ACK: 12 bytes, sent as NOTIFICATION (not indication)**
- ✅ **Status queries: Respond during printing (Wide does NOT suppress queries)**
- **Test Results:** 200,208 byte image, 224 DATA packets, 0 failures, viewable in web UI

#### ⚠️ Square Link App - CRASHES DURING CONNECTION
- ❌ **Official app crashes** during or after connection (similar to Mini Link 3 behavior)
- ✅ Protocol implementation verified correct
- ✅ **Third-party apps work perfectly** (Moments Print, nRF Connect confirmed working)
- 🔍 Official app likely uses undocumented validation or has stability issues with non-authentic devices
- **Conclusion:** Protocol implementation is correct; official app has additional requirements or crashes on emulator
- Ready for print job testing

---

## Connection Requirements (CRITICAL)

After extensive debugging with official INSTAX apps, the following requirements are **MANDATORY** for app compatibility:

### 1. BLE Address Whitelist (MOST CRITICAL)

**Discovery:** Each printer model uses a **DIFFERENT** MAC pattern for app filtering.

**⚠️ CRITICAL: Wide Uses Different Pattern Than Mini/Square!**

**Model-Specific MAC Patterns:**

| Model | 4th Byte | Example MAC | Real Hardware |
|-------|----------|-------------|---------------|
| Mini Link 3 | `0x86` | `fa:ab:bc:86:55:00` | `fa:ab:bc:86:18:4e` ✅ Verified |
| Square Link | `0x87` | `fa:ab:bc:87:55:02` | Tested working ✅ |
| Wide Link | `0x55` | `fa:ab:bc:55:55:01` | `fa:ab:bc:55:dd:c2` ✅ Verified (FI022) |

**Required Pattern Components:**
- First 3 bytes: **MUST** be `fa:ab:bc` (INSTAX manufacturer prefix)
- 4th byte: **Model-specific** (see table above)
- Last 2 bytes: Can be anything (use unique values to avoid iOS cache conflicts)

**Common Mistakes:**
- ❌ `fa:ab:bc:87:55:00` for Wide - Uses Mini/Square pattern, Wide app won't discover
- ❌ `fa:ab:bc:8X:XX:XX` for Wide - Wrong! Wide requires 0x55, not 0x8X range
- ❌ `fa:ab:bc:75:00:00` - Invalid pattern for any model

**Testing Results (December 2025):**
- Mini with `fa:ab:bc:86:55:00` → Mini app discovers ✅
- Square with `fa:ab:bc:87:55:02` → Square app discovers ✅
- Wide with `fa:ab:bc:55:55:01` → Wide app discovers ✅
- Wide with `fa:ab:bc:87:55:00` → Wide app FAILS ❌ (wrong pattern)

**Note on MAC Suffixes:**
Each model uses a unique last byte (:00, :02, :01) to prevent iOS BLE cache
conflicts when switching between models during development and testing.

**Why This Matters:**
Each official app filters on specific MAC patterns. Using the wrong 4th byte means
the app will **never discover** your device, even if all other parameters are perfect.
Wide specifically requires 0x55 - this was confirmed via Wireshark capture of real
FI022 hardware.

### 2. Serial Number Patterns (Model Detection)

**Discovery:** Apps use serial number patterns to distinguish between printer models and versions.

**Required Patterns:**

| Model | Pattern | Example | Detection Result |
|-------|---------|---------|------------------|
| Mini Link 3 | `70XXXXXX` | 70555555 | Displays Link 3 interface |
| Mini Link 1 | `75XXXXXX` | 75550000 | Displays Link 1 interface (older) |
| Wide Link | `20XXXXXX` | 20555555 | Displays Wide interface |
| Square Link | `50XXXXXX` | 50555555 | Displays Square interface |

**Testing Results:**
- Serial `75550000` on Mini → App showed "Link 1" interface
- Serial `70555555` on Mini → App showed "Link 3" interface
- First two digits determine the model variant

**Important:** Serial number must match device name (e.g., `INSTAX-70555555`)

### 3. Device Name Suffixes (App Filtering)

**Discovery:** Apps filter devices based on name suffix patterns.

**Required Suffixes:**

| Model | Suffix | Example | Why |
|-------|--------|---------|-----|
| Mini Link (all) | `(BLE)` | `INSTAX-70555555(BLE)` | Mini app requirement |
| Wide Link | `(IOS)` | `INSTAX-20555555(IOS)` | Wide app requirement |
| Square Link | `(IOS)` | `INSTAX-50555555(IOS)` | Square app requirement |

**Format:** `INSTAX-[8-digit-serial]([suffix])`

**Wrong suffixes will prevent connection or cause incorrect model detection.**

### 4. Discoverable Mode (Critical)

**Required:** Limited Discoverable Mode (flags = `0x05`)

```c
// Advertising flags
0x02 0x01 0x05
```

**Breakdown:**
- Byte 1 (0x02): Length
- Byte 2 (0x01): Type = Flags
- Byte 3 (0x05): Flags value
  - Bit 0: LE Limited Discoverable Mode = 1
  - Bit 1: LE General Discoverable Mode = 0
  - Bit 2: BR/EDR Not Supported = 1

**Testing:**
- General Discoverable (0x06) may work but not confirmed
- Limited Discoverable (0x05) confirmed working with all apps

### 5. GATT Services Requirements

**Mini Link (Both Link 1 and Link 3):**

Required services:
1. Main INSTAX Service: `70954782-2D83-473D-9E5F-81E1D02D5273`
2. Device Information Service (0x180A)
3. **Link 3 Info Service: `0000D0FF-3C17-D293-8E48-14FE2E4DA212`** (MANDATORY)
4. **Link 3 Status Service: `00006287-3C17-D293-8E48-14FE2E4DA212`** (MANDATORY)

**Critical:** Removing D0FF or 6287 services breaks Mini app detection entirely. Both Link 1 and Link 3 require these services.

**Wide Link:**

Required services:
1. Main INSTAX Service: `70954782-2D83-473D-9E5F-81E1D02D5273`
2. Device Information Service (0x180A)
3. **Wide Service: `0000E0FF-3C17-D293-8E48-14FE2E4DA212`**

**Square Link:**

Required services:
1. Main INSTAX Service: `70954782-2D83-473D-9E5F-81E1D02D5273`
2. Device Information Service (0x180A)
3. No additional model-specific services

### 6. Advertising Data Requirements

**ADV_IND packet structure:**

```
Flags: 0x05 (Limited Discoverable)
128-bit Service UUID: 70954782-2D83-473D-9E5F-81E1D02D5273
TX Power Level: Model-specific (see below)
Manufacturer Data: Model-specific (see below)
```

**SCAN_RSP packet structure:**

```
Complete Local Name: INSTAX-[serial][suffix]
Length: Exact byte count (NO null terminator in length field)
```

**TX Power Levels:**

| Model | TX Power | Raw Value |
|-------|----------|-----------|
| Mini Link | 6 dBm | `0x02 0x0A 0x06` |
| Wide Link | 0 dBm | `0x02 0x0A 0x00` |
| Square Link | 3 dBm | `0x02 0x0A 0x03` |

**Manufacturer Data:**

| Model | Data | Breakdown |
|-------|------|-----------|
| Mini | `D8 04 07 00` | Company: 0x04D8 (Fujifilm), Model: 0x0007 |
| Wide | `D8 04 02 00` | Company: 0x04D8 (Fujifilm), Model: 0x0002 |
| Square | `D8 04 05 00` | Company: 0x04D8 (Fujifilm), Model: 0x0005 |

Format: `[Length] [Type=0xFF] [Company ID LSB] [Company ID MSB] [Model Code LSB] [Model Code MSB]`

### 7. Device Information Service (DIS) Values

**Mini Link 3:**
```
Model Number: "FI033"
Serial Number: "70XXXXXX" pattern
Firmware: "0101"
Hardware: "0000"
Software: "0003"
Manufacturer: "FUJIFILM"
```

**Mini Link 1:**
```
Model Number: "FI032" or "FI033" (TBD)
Serial Number: "75XXXXXX" pattern
Firmware: "0101"
Hardware: "0000"
Software: "0003"
Manufacturer: "FUJIFILM"
```

**Wide Link:**
```
Model Number: "FI022"
Serial Number: "20XXXXXX" pattern
Firmware: "0100"
Hardware: "0001"
Software: "0002"
Manufacturer: "FUJIFILM"
```

**Square Link:**
```
Model Number: "FI017"
Serial Number: "50XXXXXX" pattern
Firmware: "0101"
Hardware: "0001"
Software: "0002"
Manufacturer: "FUJIFILM"
```

**Important:** All strings are null-terminated in characteristics but NOT in advertising data length calculations.

---

## Complete Working Configurations

### Mini Link 3 (Fully Tested)

```c
// BLE Configuration
BLE Address: fa:ab:bc:87:55:00 (Random, Static)
Advertising Flags: 0x05 (Limited Discoverable)
TX Power: 6 dBm
Device Name: "INSTAX-70555555(BLE)"
Manufacturer Data: D8 04 07 00

// Device Information Service
Model: "FI033"
Serial: "70555555"
Firmware: "0101"
Hardware: "0000"
Software: "0003"
Manufacturer: "FUJIFILM"

// GATT Services
- 70954782-2D83-473D-9E5F-81E1D02D5273 (Main INSTAX)
- 0x180A (Device Information)
- 0000D0FF-3C17-D293-8E48-14FE2E4DA212 (Link 3 Info)
- 00006287-3C17-D293-8E48-14FE2E4DA212 (Link 3 Status)
```

### Wide Link (Fully Tested)

```c
// BLE Configuration
BLE Address: fa:ab:bc:55:55:01 (Random, Static) - CRITICAL: 0x55, NOT 0x87!
Advertising Flags: 0x05 (Limited Discoverable)
TX Power: 0 dBm
Device Name: "INSTAX-205555"
Manufacturer Data: D8 04 02 00

// Device Information Service
Model: "FI022"
Serial: "20555555"
Firmware: "0100"
Hardware: "0001"
Software: "0002"
Manufacturer: "FUJIFILM"

// GATT Services
- 70954782-2D83-473D-9E5F-81E1D02D5273 (Main INSTAX)
- 0x180A (Device Information)
- 0000E0FF-3C17-D293-8E48-14FE2E4DA212 (Wide Service)
```

### Square Link (Fully Tested)

```c
// BLE Configuration
BLE Address: fa:ab:bc:87:55:02 (Random, Static)
Advertising Flags: 0x05 (Limited Discoverable)
TX Power: 3 dBm
Device Name: "INSTAX-50555555(IOS)"
Manufacturer Data: D8 04 05 00

// Device Information Service
Model: "FI017"
Serial: "50555555"
Firmware: "0101"
Hardware: "0001"
Software: "0002"
Manufacturer: "FUJIFILM"

// GATT Services
- 70954782-2D83-473D-9E5F-81E1D02D5273 (Main INSTAX)
- 0x180A (Device Information)
```

---

## Debugging Checklist

If official app doesn't discover your device:

1. ✅ **BLE Address:** Is 4th byte in 0x80-0x8F range? (`fa:ab:bc:8X:XX:XX`)
2. ✅ **Discoverable Mode:** Using Limited (0x05), not General (0x06)?
3. ✅ **Device Name Suffix:** Mini uses `(BLE)`, Wide/Square use `(IOS)`?
4. ✅ **Serial Number Pattern:** Starts with correct digits (70/20/50)?
5. ✅ **Manufacturer Data:** Correct model code (07 00/02 00/05 00)?
6. ✅ **GATT Services:** All required services registered at boot?

If app discovers but shows wrong model:

1. ✅ **Serial Number First Digits:** 70=Link3, 75=Link1, 20=Wide, 50=Square
2. ✅ **Device Name Matches Serial:** Name contains same serial as DIS?

If app discovers but won't connect:

1. ✅ **GATT Services:** Mini requires D0FF+6287, Wide requires E0FF
2. ✅ **DIS Values:** All characteristics readable and return correct strings?

---

## Testing Notes

**Date Tested:** December 12, 2024
**Devices:** All three printer types fully tested with official apps
**Result:** Complete compatibility achieved

**Protocol Command Differences:**
The official Instax app uses **different operation codes** than third-party apps:

| Query Type | Third-Party Apps | Official App |
|------------|------------------|--------------|
| Ping/Identify | Not used | `func=0x00 op=0x00` |
| Battery | `func=0x00 op=0x02 payload=0x01` | `func=0x00 op=0x01 payload=0x01` |
| Film Count | `func=0x00 op=0x02 payload=0x02` | `func=0x00 op=0x01 payload=0x02` |
| Print History | `func=0x00 op=0x02 payload=0x03` | `func=0x00 op=0x01 payload=0x03` |

The ESP32 simulator supports **both** protocol variants simultaneously.

---

## Detailed Documentation

For complete investigation details, see the ESP32 Instax Bridge project documentation:

1. **[OFFICIAL_APP_COMPATIBILITY.md](../../../Projects/ESP32-Instax-Bridge/OFFICIAL_APP_COMPATIBILITY.md)**
   - Complete protocol investigation log
   - Command/response packet format specifications
   - Theories about UI integration issues
   - Next steps for future investigation

2. **[SQUARE_LINK_FINDINGS.md](../../../Projects/ESP32-Instax-Bridge/SQUARE_LINK_FINDINGS.md)**
   - Real Square Link BLE profile captured with nRF Connect
   - All critical differences between real device and simulator
   - Testing instructions for verification

3. **[SESSION_SUMMARY_2025-11-30.md](../../../Projects/ESP32-Instax-Bridge/SESSION_SUMMARY_2025-11-30.md)**
   - Comprehensive session overview of investigation
   - All achievements, issues, and code changes
   - Statistics and lessons learned

### Impact on Protocol Understanding

This investigation revealed critical requirements for official app compatibility:

**Mini Link 3 Breakthrough (December 2025):**
- Link 3-specific services (Info and Status) are **MANDATORY** for Mini app discovery
- Without these services, the app will not show the device in the list at all
- All Link 3 Info Service characteristics must return exact values matching real device
- Advertising channel map must be set to `7` (all channels) or device won't advertise
- System ID and PnP ID in DIS are required (previously thought optional)
- Mini-specific manufacturer data (`07 00` vs Square's `05 00`) is critical
- TX Power differs between models (6 dBm for Mini, 3 dBm for Square)

**General Findings:**
- Official Instax apps filter devices heavily on manufacturer data (single wrong byte prevents discovery)
- Multiple protocol command variants exist for the same queries (op 0x01 vs 0x02)
- Connection stability ≠ UI recognition (Square app still shows "not connected" despite working protocol)
- Model number validation occurs both in advertising and Device Information Service
- Each printer model has specific DIS values (Hardware/Software revision differs between models)

**Square vs Mini Differences:**
- Mini requires Link 3 services; Square does not (Square may use different validation method)
- Different manufacturer data bytes indicate model type to the app
- Different TX power levels advertised
- Different DIS hardware/software revision values

### Future Investigation

**For Square Link Full Compatibility:**
1. **Investigate why Square app UI shows "not connected"** despite successful protocol exchange
2. **Compare Link 3 services** - does Square app expect different characteristics?
3. **Bluetooth packet sniffing** of real Square device during active printing
4. **UI behavior testing** to determine if status shows during printing vs idle screen

**For Wide Link Compatibility:**
1. ✅ **FFEA notification implemented** - Fixed "Printer Busy (1)" error by sending required status notification
2. **Identify Wide-specific manufacturer data** (likely different from Mini's `07 00` and Square's `05 00`)
3. ✅ **Wide uses E0FF service** (confirmed - does NOT require Link 3 D0FF/6287 services)
4. **Verify TX Power** for Wide model
5. **Decode FFEA data format** - determine exact meaning of all 11 bytes in FFEA notification

---

## Connection Exchange Sequence

This section documents the complete connection handshake sequence between the official INSTAX Square Link app and a real INSTAX printer, based on Bluetooth packet capture analysis (iPhone_INSTAX_capture.pklg).

### Overview

The INSTAX app follows a specific multi-stage validation sequence during connection:

1. **BLE Connection** - Hardware-level pairing
2. **Service Discovery** - GATT service and characteristic enumeration
3. **Device Information Service Validation** - Read and verify manufacturer, model, firmware
4. **Protocol Handshake** - Initial command/response exchange
5. **Sensor/Capability Queries** - Device capability verification
6. **Ready State** - Printer ready for print jobs

Each stage has specific requirements that must be met or the app will reject the connection.

---

### Stage 1: BLE Advertisement & Connection

**Purpose:** Allow the INSTAX app to discover and filter compatible devices.

**Advertisement Requirements:**
```
Service UUID:        70954782-2d83-473d-9e5f-81e1d02d5273
Manufacturer Data:   D8 04 05 00 (Company ID 0x04D8 = Fujifilm)
TX Power:            3 dBm (advertised)
Device Name:         INSTAX-XXXXXXXX (8-digit serial number)
```

**Critical:** A single byte mismatch in manufacturer data prevents discovery.

---

### Stage 2: GATT Service Discovery

**Purpose:** Enumerate available services and characteristics.

**Required Services:**

1. **Standard Instax Print Service:**
   - Service UUID: `70954782-2d83-473d-9e5f-81e1d02d5273`
   - Write Characteristic: `70954783-2d83-473d-9e5f-81e1d02d5273` (Write Without Response)
   - Notify Characteristic: `70954784-2d83-473d-9e5f-81e1d02d5273` (Notify)

2. **Device Information Service (DIS):**
   - Service UUID: `180A` (standard BLE service)
   - Model Number (0x2A24): Read
   - Serial Number (0x2A25): Read
   - Firmware Revision (0x2A26): Read
   - Hardware Revision (0x2A27): Read
   - Software Revision (0x2A28): Read
   - Manufacturer Name (0x2A29): Read

**Critical:** The app reads ALL DIS characteristics and validates values.

---

### Stage 3: Device Information Service Validation

**Purpose:** Verify the device is an authentic INSTAX printer with expected firmware.

**Real INSTAX Square Link Values (from packet capture):**

| Characteristic | UUID | Real Device Value | Notes |
|----------------|------|-------------------|-------|
| Manufacturer Name | 0x2A29 | `"FUJIFILM"` | **NOT** "FUJIFILM Corporation" |
| Model Number | 0x2A24 | `"FI017"` | Internal model code, **NOT** "SQ20" |
| Serial Number | 0x2A25 | `"70423278"` | 8-digit numeric string |
| Firmware Revision | 0x2A26 | `"0101"` | No dots: "0101" not "01.01" |
| Hardware Revision | 0x2A27 | `"0001"` | 4-digit version |
| Software Revision | 0x2A28 | `"0002"` | 4-digit version, **NOT** "01.01" |

**Critical Findings:**
- Manufacturer must be exactly `"FUJIFILM"` (8 characters)
- Model Number uses internal code `"FI017"` not marketing name
- Version strings have NO separators (dots/dashes)
- Even whitespace differences cause rejection

**Reference:** Packet capture frames 6658-6671 (DIS reads during connection)

---

### Stage 4: Protocol Handshake

**Purpose:** Execute initial command/response exchange to verify protocol compatibility.

**Handshake Sequence (from packet capture):**

#### Command 1: Device Identification/Ping
```
Request:  41 62 00 07 00 00 F7
          ^^^^^^ ^^^^^ ^^^^^ ^^
          Header Length Func  Checksum
                        Op=00

Response: 61 42 00 10 00 00 00 01 00 02 00 00 00 00 00 49
          ^^^^^^ ^^^^^ ^^^^^ ^^^^^^^^^^^^^^^^^^^^^^^ ^^
          Header Length Func  Payload (9 bytes)      Checksum
                        Op=00
```

**Payload Breakdown:**
- Bytes 0-1: `00 01` - Device ID field 1
- Bytes 2-3: `00 02` - Device ID field 2
- Bytes 4-8: `00 00 00 00 00` - Reserved/status bytes

**Length:** 16 bytes total (not 8 bytes generic ACK)

**Critical:** App expects 16-byte response. 8-byte ACK causes timeout/rejection.

---

#### Command 2: Model/Firmware Query (Type 1)
```
Request:  41 62 00 08 00 01 01 F5
          ^^^^^^ ^^^^^ ^^^^^ ^^ ^^
          Header Length Func  P  Checksum
                        Op=01 0x01

Response: 61 42 00 0F 00 01 00 01 05 46 49 30 31 37 1F
          ^^^^^^ ^^^^^ ^^^^^ ^^^^^ ^^ ^^^^^^^^^^^^ ^^
          Header Length Func  Hdr  Len String      Checksum
                        Op=01
```

**Payload Breakdown:**
- Bytes 0-1: `00 01` - Payload header (echoes query type)
- Byte 2: `05` - String length (5 characters)
- Bytes 3-7: `46 49 30 31 37` - ASCII "FI017" (model string)

**Length:** 15 bytes total (9-byte header + 5-char string + 1 checksum)

**Critical:** Must include length prefix before string data.

---

#### Command 3: Serial Number Query (Type 2)
```
Request:  41 62 00 08 00 01 02 F4
                              ^^ Query type 2

Response: 61 42 00 12 00 01 00 02 08 35 30 31 39 36 35 36 33 A4
          ^^^^^^ ^^^^^ ^^^^^ ^^^^^ ^^ ^^^^^^^^^^^^^^^^^^^^^^^ ^^
          Header Length Func  Hdr  Len Serial (8 chars)      Checksum
                        Op=01
```

**Payload Breakdown:**
- Bytes 0-1: `00 02` - Payload header (echoes query type 2)
- Byte 2: `08` - String length (8 characters)
- Bytes 3-10: `35 30 31 39 36 35 36 33` - ASCII "50196563" (serial)

**Length:** 18 bytes total

---

#### Command 4: Additional Info Query (Type 3)
```
Request:  41 62 00 08 00 01 03 F3
                              ^^ Query type 3

Response: 61 42 00 0E 00 01 00 03 04 30 30 30 30 86
          ^^^^^^ ^^^^^ ^^^^^ ^^^^^ ^^ ^^^^^^^^^ ^^
          Header Length Func  Hdr  Len String   Checksum
                        Op=01
```

**Payload Breakdown:**
- Bytes 0-1: `00 03` - Payload header (echoes query type 3)
- Byte 2: `04` - String length (4 characters)
- Bytes 3-6: `30 30 30 30` - ASCII "0000" (additional info)

**Length:** 14 bytes total

**Pattern:** All operation 0x01 responses follow format:
```
[Payload Header (2 bytes)] [String Length (1 byte)] [String Data] [Checksum]
```

---

#### Command 5: Image Dimensions/Capabilities Query
```
Request:  41 62 00 08 00 02 00 F5
          ^^^^^^ ^^^^^ ^^^^^ ^^ ^^
          Header Length Func  P  Checksum
                        Op=02 0x00

Response: 61 42 00 17 00 02 00 00 03 20 03 20 02 4B 00 00 1C 00 00 06 40 00 E3
          ^^^^^^ ^^^^^ ^^^^^ ^^^^^ ^^^^^^^^^ ^^^^^ ^^^^^^^^^^^^^^^^^^^^^^^^^^ ^^
          Header Length Func  Hdr  Width     Height Capabilities (10 bytes)  Checksum
                        Op=02      (800px)   (800px)
```

**Payload Breakdown:**
- Bytes 0-1: `00 00` - Payload header
- Bytes 2-3: `03 20` - Width = 800 pixels (big-endian)
- Bytes 4-5: `03 20` - Height = 800 pixels
- Bytes 6-15: Capability bytes (format/chunk size/max file size)

**Length:** 23 bytes total

**Critical:** Must return full dimensions response, not 8-byte ACK.

---

#### Command 6: Battery Status Query
```
Request:  41 62 00 08 00 02 01 F4
                              ^^ Query type 1

Response: 61 42 00 0D 00 02 00 01 02 32 00 00 18
          ^^^^^^ ^^^^^ ^^^^^ ^^^^^ ^^ ^^ ^^^^^ ^^
          Header Length Func  Hdr  ?? %  Pad   Checksum
                        Op=02
```

**Payload Breakdown:**
- Bytes 0-1: `00 01` - Payload header (echoes query type)
- Byte 2: `02` - Data length/status
- Byte 3: `32` - Battery percentage (50% = 0x32)
- Bytes 4-5: `00 00` - Padding/reserved

**Length:** 13 bytes total (not 14)

**Critical:** Response length must match real device exactly.

---

#### Command 7: Film Count/Printer Function Query
```
Request:  41 62 00 08 00 02 02 F3
                              ^^ Query type 2

Response: 61 42 00 11 00 02 00 02 28 00 00 0C 00 00 00 00 13
          ^^^^^^ ^^^^^ ^^^^^ ^^^^^ ^^ ^^^^^ ^^ ^^^^^^^^^^^^ ^^
          Header Length Func  Hdr  Cap Pad   Count Padding  Checksum
                        Op=02
```

**Payload Breakdown:**
- Bytes 0-1: `00 02` - Payload header (echoes query type)
- Byte 2: `28` - Capability byte
- Bytes 3-4: `00 00` - Reserved
- Byte 5: `0C` - Photos remaining (12 = 0x0C)
- Bytes 6-9: `00 00 00 00` - Padding

**Length:** 17 bytes total (not 18)

---

#### Command 8: Print History Query
```
Request:  41 62 00 08 00 02 03 F2
                              ^^ Query type 3

Response: 61 42 00 0D 00 02 00 03 00 00 00 XX YY
          ^^^^^^ ^^^^^ ^^^^^ ^^^^^ ^^^^^^^^^^^^ ^^
          Header Length Func  Hdr  Count (4B)  Checksum
                        Op=02
```

**Payload Breakdown:**
- Bytes 0-1: `00 03` - Payload header (MUST match query type)
- Bytes 2-5: Print count (32-bit big-endian)

**Length:** 13 bytes total

**Critical:** Payload header byte must echo query type (was 0x00, must be 0x03).

---

### Stage 5: Sensor/Capability Verification

**Purpose:** Query device sensors and extended capabilities.

#### Command 9: Additional Info Request (Sensor Data)
```
Request:  41 62 00 08 30 10 00 14
          ^^^^^^ ^^^^^ ^^^^^ ^^ ^^
          Header Length Func  P  Checksum
                        Op=10 0x00

Response: 61 42 00 11 30 10 00 00 C3 80 00 BE 00 00 00 00 0A
          ^^^^^^ ^^^^^ ^^^^^ ^^^^^ ^^^^^^^^^^^^^^^^^^^^^^^ ^^
          Header Length Func  Hdr  Sensor Data (10 bytes) Checksum
                        Op=10
```

**Payload Breakdown:**
- Bytes 0-1: `00 00` - Payload header (echoes query type 0)
- Bytes 2-11: Sensor/device data (format TBD)

**Length:** 17 bytes total

**Critical:** Must respond with specific sensor data, not generic 8-byte ACK.

---

#### Command 10: Additional Info Request (Extended Data)
```
Request:  41 62 00 08 30 10 01 13
          ^^^^^^ ^^^^^ ^^^^^ ^^ ^^
          Header Length Func  P  Checksum
                        Op=10 0x01

Response: 61 42 00 15 30 10 00 01 00 00 00 02 FF 00 01 02 00 00 00 00 02
          ^^^^^^ ^^^^^ ^^^^^ ^^^^^ ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^ ^^
          Header Length Func  Hdr  Extended Info Data (14 bytes)         Checksum
                        Op=10
```

**Payload Breakdown:**
- Bytes 0-1: `00 01` - Payload header (echoes query type 1)
- Bytes 2-15: Extended device/capability info

**Length:** 21 bytes total

**Critical:** Without this response, the app polls repeatedly (hundreds of times), treating device as "not ready."

---

### Stage 6: Connection Complete

**Result:** All queries answered successfully, connection remains stable, app displays printer as ready.

**Expected Behavior:**
- App stops sending repeated queries
- Connection remains quiet until print job submitted
- Battery/film count displayed in app UI
- Print button becomes active

---

### Connection Sequence Summary

**Complete Handshake Timeline:**

| Stage | Command | Function | Operation | Payload | Response Length | Purpose |
|-------|---------|----------|-----------|---------|-----------------|---------|
| 4.1 | Ping | 0x00 | 0x00 | - | 16 bytes | Device identification |
| 4.2 | Info Query | 0x00 | 0x01 | 0x01 | 15 bytes | Model string |
| 4.3 | Info Query | 0x00 | 0x01 | 0x02 | 18 bytes | Serial number |
| 4.4 | Info Query | 0x00 | 0x01 | 0x03 | 14 bytes | Additional info |
| 4.5 | Capability | 0x00 | 0x02 | 0x00 | 23 bytes | Dimensions |
| 4.6 | Status | 0x00 | 0x02 | 0x01 | 13 bytes | Battery level |
| 4.7 | Status | 0x00 | 0x02 | 0x02 | 17 bytes | Film count |
| 4.8 | Status | 0x00 | 0x02 | 0x03 | 13 bytes | Print history |
| 5.1 | Sensor | 0x30 | 0x10 | 0x00 | 17 bytes | Sensor data |
| 5.2 | Sensor | 0x30 | 0x10 | 0x01 | 21 bytes | Extended info |

**Total Commands:** 10
**Total Time:** < 2 seconds on successful connection

---

### Implementation Fixes Applied (ESP32 Simulator)

To achieve compatibility with the official INSTAX app, the following fixes were required:

#### Round 1: Protocol Response Formats
**Problem:** Simulator sending wrong response lengths and formats.

**Fixes Applied:**
- Operation 0x00: Changed 8-byte ACK → 16-byte device info response
- Operation 0x01: Added length prefixes to string responses (model, serial, info)
- Operation 0x02 payload=0x00: Changed simple ACK → 23-byte dimensions response

**File:** `/Users/dgwilson/Projects/ESP32-Instax-Bridge/main/ble_peripheral.c` (lines 173-292)

**Result:** Initial handshake completes successfully.

---

#### Round 2: Device Information Service Corrections
**Problem:** DIS values didn't match real INSTAX device exactly.

**Fixes Applied:**
- Manufacturer: `"FUJIFILM Corporation"` → `"FUJIFILM"`
- Model Number: `"SQ20"` → `"FI017"`
- Firmware Revision: `"01.01"` → `"0101"`
- Software Revision: `"01.01"` → `"0002"`

**File:** `/Users/dgwilson/Projects/ESP32-Instax-Bridge/main/ble_peripheral.c` (lines 972, 1020-1025)

**Result:** App recognizes device as authentic INSTAX printer.

---

#### Round 3: Response Length Consistency
**Problem:** Response lengths off by 1 byte due to checksum calculation error.

**Fixes Applied:**
- Battery response: `response_len = 13` → `12` (checksum added separately)
- Printer function: `response_len = 17` → `16` (checksum added separately)
- Print history: Payload header `0x00` → `0x03` (must echo query type)

**File:** `/Users/dgwilson/Projects/ESP32-Instax-Bridge/main/ble_peripheral.c` (lines 329, 347, 355)

**Result:** All response lengths match real device byte-for-byte.

---

#### Round 4: Additional Info Operation Handler
**Problem:** Operation 0x10 (INSTAX_OP_ADDITIONAL_INFO) not implemented, causing excessive polling.

**Fixes Applied:**
- Added handler for function 0x30, operation 0x10
- Query type 0x00 → 17-byte sensor data response
- Query type 0x01 → 21-byte extended info response

**File:** `/Users/dgwilson/Projects/ESP32-Instax-Bridge/main/ble_peripheral.c` (lines 644-714)

**Result:** App stops excessive polling, connection remains stable and quiet.

---

### Common Implementation Errors

**❌ Error 1: Generic ACK for All Commands**
```c
// WRONG: Sending 8-byte ACK for everything
response_len = 8;
response[6] = 0x00; // Status OK
response[7] = checksum;
```
**Why it fails:** App expects specific payload formats, not generic acknowledgments.

---

**❌ Error 2: Missing Length Prefix on Strings**
```c
// WRONG: String without length prefix
response[6] = 0x00;
response[7] = 0x01;
memcpy(&response[8], "FI017", 5); // No length byte
```
**Why it fails:** App cannot parse variable-length strings without length prefix.

**✅ Correct:**
```c
response[6] = 0x00;
response[7] = 0x01;
response[8] = 0x05;  // Length prefix
memcpy(&response[9], "FI017", 5);
```

---

**❌ Error 3: Wrong Payload Header**
```c
// WRONG: Hardcoded 0x00 for all responses
response[6] = 0x00;
response[7] = 0x00; // Should echo query type!
```
**Why it fails:** App validates payload header matches query type.

**✅ Correct:**
```c
response[6] = 0x00;
response[7] = query_type; // Echo the query type from payload
```

---

**❌ Error 4: Response Length Includes Checksum Twice**
```c
// WRONG: Counting checksum in response_len
response_len = 13; // Including checksum
// ... later ...
response[response_len] = checksum; // Adds 1 more!
```
**Why it fails:** Total packet becomes 14 bytes instead of 13.

**✅ Correct:**
```c
response_len = 12; // Excluding checksum
// ... later ...
response[response_len] = checksum; // Now total is 13
```

---

### Debugging Connection Issues

**Symptoms and Causes:**

| Symptom | Likely Cause | Solution |
|---------|--------------|----------|
| App doesn't discover device | Wrong manufacturer data | Verify `D8 04 05 00` exactly |
| Connects but shows "Not Connected" | DIS values wrong | Match all 6 DIS strings exactly |
| Times out during handshake | Response too short | Check all response lengths |
| Excessive polling (hundreds of commands) | Missing operation 0x10 handler | Implement ADDITIONAL_INFO response |
| Disconnects immediately | Wrong checksum | Verify checksum calculation |
| Battery shows "???" | Wrong payload format | Check payload header bytes |

---

### Testing Checklist

**✅ Pre-Connection Tests:**
- [ ] Manufacturer data = `D8 04 05 00`
- [ ] Device name = `INSTAX-XXXXXXXX` (8 digits)
- [ ] Service UUID = `70954782-2d83-473d-9e5f-81e1d02d5273`

**✅ DIS Validation Tests:**
- [ ] Manufacturer Name = `"FUJIFILM"` (no suffix)
- [ ] Model Number = `"FI017"` (internal code)
- [ ] Firmware = `"0101"` (no dots)
- [ ] Software = `"0002"` (not "01.01")

**✅ Protocol Handshake Tests:**
- [ ] Operation 0x00 returns 16 bytes
- [ ] Operation 0x01 strings have length prefix
- [ ] Operation 0x02 payload=0x00 returns 23 bytes
- [ ] Battery response = 13 bytes (not 14)
- [ ] Film count response = 17 bytes (not 18)
- [ ] Print history payload header echoes query type

**✅ Sensor Query Tests:**
- [ ] Operation 0x10 payload=0x00 returns 17 bytes
- [ ] Operation 0x10 payload=0x01 returns 21 bytes
- [ ] No excessive polling after handshake completes

---

### Reference Materials

**Packet Capture Analysis:**
- File: `/Users/dgwilson/Projects/ESP32-Instax-Bridge/Bluetooth Packet Capture/iPhone_INSTAX_capture.pklg`
- Real Device: INSTAX-50196562 (Square Link, IOS)
- Frames Analyzed: 6439-7190 (full connection sequence)

**Implementation Documentation:**
- `/Users/dgwilson/Desktop/Projects/Moments Project Suite/Simulator_Fixes_Applied.md` - Round 1 fixes
- `/Users/dgwilson/Desktop/Projects/Moments Project Suite/DIS_Fixes_Applied.md` - Round 2 fixes
- `/Users/dgwilson/Desktop/Projects/Moments Project Suite/Response_Length_Fixes.md` - Round 3 fixes
- `/Users/dgwilson/Desktop/Projects/Moments Project Suite/Additional_Info_Fix.md` - Round 4 fixes

**Code Implementation:**
- `/Users/dgwilson/Projects/ESP32-Instax-Bridge/main/ble_peripheral.c` - Main connection handler
- `/Users/dgwilson/Projects/ESP32-Instax-Bridge/main/instax_protocol.h` - Protocol constants

---

## Advanced Features & Settings (Discovered December 2025)

This section documents advanced printer features discovered through packet capture analysis of the official INSTAX app with a real INSTAX Square Link printer.

**Capture File:** `iPhone_INSTAX_capture-2.pklg`
**Test Date:** December 4, 2025
**Test Sequence:** Charging state changes, auto-sleep settings, print mode selection, complete print job

### Charging Status Detection ✅ VERIFIED

**Discovery:** Bit 7 of the capability byte definitively indicates charging status.

**Query:** Function `0x00`, Operation `0x02`, Payload `0x02` (Printer Function Query)

**Response Format:**
```
61 42 00 11 00 02 00 02 [capability] 00 00 [count] 00 00 00 00 [checksum]
                         ^^^^^^^^^^^
                      Capability byte
```

**Capability Byte Bit Map:**
| Bit | Mask | Meaning | Values |
|-----|------|---------|--------|
| 7 | 0x80 | Charging status | 0 = Not charging, 1 = Charging |
| 6 | 0x40 | Unknown | Always 0 observed |
| 5 | 0x20 | Unknown | Always 1 observed (capability flag?) |
| 4 | 0x10 | Charging phase | 0 = Just started, 1 = Actively charging |
| 0-3 | 0x0F | Unknown | Always 0x08 observed |

**Real-World Evidence:**

| Time | Capability Byte | Binary | Status |
|------|----------------|--------|--------|
| Initial | `0x28` | `00101000` | Not charging |
| Plug in | `0xA8` | `10101000` | Charging started (bit 7=1, bit 4=0) |
| Charging | `0xB8` | `10111000` | Actively charging (bit 7=1, bit 4=1) |
| Unplug | `0x28` | `00101000` | Not charging (bit 7=0) |
| Plug again | `0xA8` | `10101000` | Charging started |
| Charging | `0xB8` | `10111000` | Actively charging |

**Implementation:**
```c
uint8_t capability_byte = response[8];  // payload[2]
bool is_charging = (capability_byte & 0x80) != 0;
bool charge_active = (capability_byte & 0x10) != 0;
```

---

### Auto-Sleep (Power-Off Timeout) Settings ✅ DISCOVERED

**Discovery:** Printers can be configured to auto-shutdown after a timeout, or never shutdown.

**Command:** Function `0x01`, Operation `0x02` (AUTO_SLEEP_SETTINGS)

**Packet Structure:**
```
41 62 00 13 01 02 [timeout] [00 x 12] [checksum]
                  ^^^^^^^^^
              Timeout in minutes
```

**Total Length:** 19 bytes (0x13)
**Payload:** 13 bytes
- Byte 0: Timeout value (0-255 minutes)
- Bytes 1-12: All zeros (padding/reserved)

**Timeout Values:**

| Value | Meaning | Use Case |
|-------|---------|----------|
| `0x00` | Never shutdown | Photo booth events, continuous operation |
| `0x05` | 5 minutes (default) | Normal use, battery conservation |
| `0x01`-`0xFF` | Custom timeout | Any value 1-255 minutes supported |

**Examples:**

**Set to Never Shutdown:**
```
Command:  41 62 00 13 01 02 00 00 00 00 00 00 00 00 00 00 00 00 00 46
                            ^^
                         0x00 = never
Response: 61 42 00 08 01 02 00 [checksum]
          (ACK with status 0x00 = success)
```

**Set to 5 Minutes:**
```
Command:  41 62 00 13 01 02 05 00 00 00 00 00 00 00 00 00 00 00 00 41
                            ^^
                         0x05 = 5 minutes
Response: 61 42 00 08 01 02 00 [checksum]
```

**Official App Behavior:**

The official INSTAX app only provides two options:
- "Do not power off" → Sends `0x00`
- "5 minutes" → Sends `0x05`

However, the protocol supports any timeout from 1-255 minutes!

**Implementation Notes:**
- Setting persists across disconnections (stored in printer NVRAM)
- Can be sent anytime after connection established
- No query command observed - write-only setting
- Printer responds with simple ACK

**Use Case - Photo Booth:**
```
1. Event starts at 6pm, runs until 11pm (5 hours)
2. Connect printer
3. Send AUTO_SLEEP_SETTINGS with 0x00 (never)
4. Printer stays on for entire event
5. Guests can print continuously without intervention
6. No manual power button presses required

Without this: Printer shuts off every 5 minutes, requiring manual restart.
```

---

### Print Mode (Rich/Natural) Selection ✅ DISCOVERED

**Discovery:** Print mode is embedded in the color correction table command sent before each print.

**Command:** Function `0x30`, Operation `0x01` (Color Correction Table Upload)

**Packet Structure:**
```
41 62 [length] 30 01 [mode] [color_table_data...] [checksum]
                      ^^^^^^
                  Print mode selector
```

**Print Modes:**

| Mode Value | Print Mode | Description | Table Size |
|------------|------------|-------------|------------|
| `0x00` | Rich (Default) | Enhanced contrast, vivid colors, high saturation | 311 bytes |
| `0x03` | Natural | Subdued colors, natural reproduction, lower saturation | 251 bytes |

**Rich Mode Example:**
```
Command: 41 62 01 37 30 01 00 64 05 ff 33 33 1e 36 36 1f ...
                         ^^
                      Rich mode
Length: 0x0137 = 311 bytes total
Payload: 00 64 05 ff [color LUT data...]
```

**Natural Mode Example:**
```
Command: 41 62 00 fb 30 01 03 50 08 00 00 00 00 15 00 0e 2a ...
                         ^^
                   Natural mode
Length: 0x00fb = 251 bytes total
Payload: 03 50 08 00 00 00 00 15 [color LUT data...]
```

**Color Correction Tables:**

The payload contains:
- **Byte 0:** Print mode selector (`0x00` = Rich, `0x03` = Natural)
- **Bytes 1+:** Color lookup table (LUT) for RGB transformation
- Tables are model-specific (values shown are for Square Link)
- Rich mode boosts saturation/contrast for vivid output
- Natural mode provides neutral mapping for realistic colors

**Timing & Persistence:**
- ⚠️ **NOT persistent** - does NOT survive power cycle or disconnection
- **MUST be sent before EVERY print job**
- Sent after status queries, before PRINT_IMAGE_DOWNLOAD_START
- No response from printer - fire and forget

**Implementation Notes:**

To implement print mode selection:
1. Extract color tables from packet capture frames:
   - Rich mode: Frame 4939 (311 bytes starting with `00 64 05 ff...`)
   - Natural mode: Frame 7629 (251 bytes starting with `03 50 08 00...`)
2. Store tables as hex arrays in code
3. Select table based on user preference
4. Send entire table before print START command

**Simplification:**
- Could always use smallest table (Natural, 251 bytes) for both modes
- First byte determines behavior regardless of table content
- Full tables provide optimal color reproduction

---

### Complete Print Sequence (7-Phase Protocol)

**Based on packet capture frames 7629-10182 (actual print job)**

#### Phase 1: Pre-Print Status Queries

```
Query battery status
Command:  41 62 00 08 00 02 01 51
Response: 61 42 00 0d 00 02 00 01 02 [percentage] 00 00 [checksum]

Query dimensions/capabilities
Command:  41 62 00 08 00 02 00 52
Response: 61 42 00 17 00 02 00 00 [width] [height] [capabilities...] [checksum]

Query film count
Command:  41 62 00 08 00 02 02 50
Response: 61 42 00 11 00 02 00 02 [capability] 00 00 [count] 00 00 00 00 [checksum]
```

**Purpose:** Verify printer is ready (has battery, film, correct dimensions)

---

#### Phase 2: Print Setup (Color Correction)

```
Send color correction table (includes print mode)
Command:  41 62 [length] 30 01 [mode: 0x00 or 0x03] [table_data...] [checksum]
```

**Purpose:** Set Rich or Natural print mode for this print job

---

#### Phase 3: BLE Connection Management

```
Command:  41 62 00 0f 01 03 00 0c 00 0c 00 00 01 90 a0
          ^^^^^^ ^^^^^ ^^^^^ ^^^^^^^^^^^^^^^^^^^^^^ ^^
          Header Length Func  Payload (8 bytes)     Checksum
                        Op=03

Payload breakdown:
  00 0c - Unknown (possibly MTU negotiation?)
  00 0c - Repeated
  00 00 - Reserved
  01 90 - Unknown (possibly max packet size?)
```

**Purpose:** Prepare BLE connection for large data transfer

---

#### Phase 4: Print Image Download START

```
Command:  41 62 00 0f 10 00 02 00 00 00 00 01 7b 90 2f
                      ^^^^^ ^^ ^^^^^^^^^^^ ^^^^^^^^^ ^^
                      Func  Format  Rsvd    Size      Checksum
                      Op=00

Payload breakdown:
  02 - Format (0x02 = JPEG, possibly 0x01 = raw?)
  00 00 00 00 - Reserved
  01 7b 90 - File size (big-endian 24-bit)
             = (0x01 << 16) + (0x7b << 8) + 0x90
             = 65536 + 31488 + 144
             = 97,168 bytes (~95KB)

Response: 61 42 00 0C 10 00 00 00 00 03 84 B9 00
          ^^^^^^ ^^^^^ ^^^^^ ^^^^^^^^^^^^^^^^^^^^^ ^^
          Header Length Func  Payload (6 bytes)     Checksum
                        Op=00

Response payload breakdown (UPDATED December 27, 2025):
  Byte 6: 00 - Status (0x00 = OK, 0xB1 = error)
  Byte 7-8: 00 00 - Padding
  Byte 9-11: 03 84 B9 - Max buffer size (230,585 bytes = ~225KB)

⚠️ CRITICAL: Response MUST be 12 bytes, not 8 bytes!
   Old (wrong): 61 42 00 08 10 00 00 XX (8 bytes - causes app crash)
   New (correct): 61 42 00 0C 10 00 00 00 00 03 84 B9 XX (12 bytes)
   The extra bytes contain max buffer size confirmation.
```

**Purpose:** Tell printer total file size and format, prepare to receive data

---

#### Phase 5: Print Image Download DATA (Chunked Transfer)

```
Format: 41 62 [length] 10 01 [chunk_index: 4 bytes] [chunk_data] [checksum]
                       ^^^^^ ^^^^^^^^^^^^^^^^^^^^^ ^^^^^^^^^^^ ^^
                       Func  Chunk number          Image data  Checksum
                       Op=01

Example - First chunk (Frame 7745):
41 62 07 1b 10 01 00 00 00 00 ff d8 ff e0 ...
      ^^^^^ ^^^^^ ^^^^^^^^^^^ ^^^^^^^^^^^^
      1819  Func  Chunk 0     JPEG SOI
      bytes Op=01             (FF D8 FF E0)

Example - Last chunk (Frame 10173):
41 62 [len] 10 01 00 00 00 35 [data...] [checksum]
                  ^^^^^^^^^^^
                  Chunk 53 (0x35)

Response (per chunk): 61 42 00 0c 10 01 00 00 00 [chunk#] [checksum]
                      (ACK for each chunk received)
```

**Chunk Details:**
- Total chunks: 54 (for 97,168 byte image)
- Chunk size: ~1808 bytes for Square (900 for Mini/Wide)
- Chunk index: 32-bit big-endian, starts at 0
- Last chunk may be smaller than chunk size
- Printer ACKs each chunk individually

**Image Data:**
- First bytes are JPEG header: `FF D8 FF E0 00 10 4A 46 49 46 ...` (JFIF)
- Standard JPEG format, no special encoding
- Must fit within printer size limits (105KB for Square, 55KB for Link 3)

---

#### Phase 6: Print Image Download END

```
Command:  41 62 00 07 10 02 43
                      ^^^^^ ^^
                      Func  Checksum
                      Op=02

Response: 61 42 00 08 10 02 00 42
                      ^^^^^ ^^ ^^
                      Func  Status Checksum
                      Op=02 0x00 = success
```

**Purpose:** Signal end of data transfer, verify all chunks received

---

#### Phase 7a: Final Status Check (CRITICAL)

```
Query film count again (verify film still available)
Command:  41 62 00 08 00 02 02 50
Response: 61 42 00 11 00 02 00 02 b8 00 00 0c 00 00 00 00 83
                                              ^^
                                          12 photos (0x0c)
```

**Purpose:** Ensure film hasn't run out during upload

**⚠️ IMPORTANT:** This query between END and EXECUTE appears to be **REQUIRED** by the official app. Testing needed to determine if printer will EXECUTE without this query.

---

#### Phase 7b: EXECUTE Print (Trigger Physical Print)

```
Command:  41 62 00 07 10 80 c5
                      ^^^^^ ^^
                      Func  Checksum
                      Op=80

Response: 61 42 00 09 10 80 00 0c b7
                      ^^^^^ ^^^^^ ^^
                      Func  Payload Checksum
                      Op=80

Response payload: 00 0c (meaning unclear - possibly:
  - Print time estimate in seconds (12 seconds?)
  - Confirmation code
  - Film count after print starts)
```

**Purpose:** Commit to print, trigger physical printing process

**⚠️ CRITICAL:** EXECUTE is a **separate command** from END. Printer will NOT automatically print after END. You MUST send EXECUTE command.

---

### Complete Print Sequence Summary

```
Phase 1: Pre-Print Queries
  ├─ Query battery → Verify power available
  ├─ Query dimensions → Verify printer model/settings
  └─ Query film count → Verify film available

Phase 2: Print Setup
  └─ Send color correction table → Set Rich/Natural mode

Phase 3: BLE Connection
  └─ Function 0x01 Op 0x03 → Prepare for large transfer

Phase 4: START
  ├─ Send PRINT_IMAGE_DOWNLOAD_START → File size + format
  └─ Receive ACK

Phase 5: DATA (repeated 54 times for 95KB image)
  ├─ Send chunk N
  ├─ Receive ACK for chunk N
  ├─ Send chunk N+1
  └─ ...

Phase 6: END
  ├─ Send PRINT_IMAGE_DOWNLOAD_END
  └─ Receive ACK

Phase 7: EXECUTE
  ├─ Query film count (final check) ⚠️ LIKELY REQUIRED
  ├─ Send PRINT_IMAGE (EXECUTE)
  └─ Receive ACK → Printer begins physical printing

Total sequence: 7 phases, ~60+ commands/responses
Time to execute: ~3-10 seconds depending on image size and BLE speed
```

---

### Key Implementation Findings

**1. Film Query Between END and EXECUTE:**
- Official app always queries film count after END, before EXECUTE
- Ensures film hasn't run out during upload
- **Testing needed:** Can EXECUTE be sent without this query? Likely NO.

**2. Color Correction Table is NOT Persistent:**
- Must be sent before EVERY print job
- Does not survive power cycle or disconnect
- If omitted, printer uses last-sent table or default (Rich mode)
- **Recommendation:** Always send table before print for predictable results

**3. EXECUTE is NOT Automatic:**
- Sending END does NOT trigger printing
- EXECUTE command is separate and required
- Allows app to:
  - Upload image speculatively (prepare multiple prints)
  - Cancel print after upload (don't send EXECUTE)
  - Verify conditions before committing to physical print

**4. Chunk Size is Model-Specific:**
- Square: 1808 bytes (observed in capture)
- Mini/Wide: 900 bytes (from previous analysis)
- Link 3: Same as model type (Square Link 3 = 1808, Mini Link 3 = 900)

**5. File Size Limits:**
- Square/Mini/Wide (non-Link 3): ~105KB max
- Link 3: ~55KB max (requires more aggressive compression)

---

## Changelog

### December 27, 2025 (Wide Link Official App Printing - COMPLETE SUCCESS!)

**BREAKTHROUGH: Official INSTAX Wide app now successfully prints to ESP32 simulator!**

Three critical fixes were required to enable printing with the official Fujifilm INSTAX Wide app:

1. **Print START ACK must be sent as NOTIFICATION (not Indication)**
   - Real Wide printer sends ALL Instax protocol responses (`61 42...`) as **NOTIFICATIONS** on handle 0x002a
   - Indications (opcode 0x1d) on handle 0x0008 are a separate service (Service Changed: `0a00ffff`)
   - Previous code incorrectly used BLE indications for Wide Print START ACK

2. **Print START ACK packet is 12 bytes (not 13)**
   - Previous code sent 13 bytes with checksum byte duplicated
   - Real Wide printer response: `61 42 00 0C 10 00 00 00 00 03 84 B9` (12 bytes)
   - Breakdown:
     - `61 42` = Header (from device)
     - `00 0C` = Length (12 = total packet)
     - `10 00` = Function/Operation (PRINT START response)
     - `00 00 00` = Status OK + padding
     - `03 84` = Chunk size (0x0384 = 900 bytes) - **2 bytes, NOT 3!**
     - `B9` = Checksum
   - Previous code incorrectly treated `B9` as part of a 3-byte "max size" field and added another checksum

3. **Wide printer responds to ALL status queries during printing**
   - Previous code suppressed status query responses during print upload (to prevent bandwidth saturation)
   - Real Wide printer responds to every status query during printing (verified via packet capture)
   - Suppression must be model-specific: only apply to Mini/Square, NOT Wide

**Verification via packet capture analysis:**
- Analyzed `Real_iPhone_to_Wide_print_b.pklg` using tshark
- Confirmed all protocol responses use ATT opcode 0x1b (Notification) on handle 0x002a
- Confirmed status query responses continue during DATA transfer phase
- Print START ACK immediately followed by DATA packets after app receives ACK

**Test Results:**
- ✅ Official INSTAX Wide app connects and shows printer ready
- ✅ Print job initiates successfully (Print START ACK accepted)
- ✅ All 224 DATA packets received (200,700 bytes)
- ✅ 0 retries, 0 failures during upload
- ✅ Print END and EXECUTE commands processed
- ✅ Image saved successfully to SPIFFS
- ✅ Image viewable via web interface

**Code Changes:**
- `ble_peripheral.c`: Print START ACK now 12 bytes, sent as notification
- `ble_peripheral.c`: Status query suppression bypassed for Wide model
- Comments updated with correct packet format documentation

### December 27, 2024 (Wide Link "Printer Busy" Fix - BREAKTHROUGH!)

**ROOT CAUSE IDENTIFIED:** Real iPhone packet capture of Wide printer print revealed critical protocol differences.

- **CRITICAL: Model code is "BO-22" not "FI022"**
  - Real Wide Link (micro USB model) reports "BO-22" in model query response
  - The "BO" prefix may be an earlier naming convention before "FI0xx" standardization
  - Changed `printer_emulator.c` to use "BO-22" for Wide model

- **CRITICAL: Battery byte 8 is the BUSY FLAG**
  - Byte 8 = `0x02` means READY
  - Byte 8 = `0x01` means BUSY (this was causing "Printer Busy (1)" error!)
  - Changed battery response from `0x01` to `0x02` for Wide

- **Ping response bytes 7 and 9 should match**
  - Wide: Both byte 7 and byte 9 = `0x01`
  - Mini/Square: Both byte 7 and byte 9 = `0x02`
  - Previously byte 9 was hardcoded to `0x02` for all models

- **Capability byte base is 0x20 (not 0x30)**
  - Real Wide (4 films) sends `0x24` = `0x20` + `0x04`
  - Wide uses same encoding as Square Link
  - Changed capability base from `0x30` to `0x20`

- **RESULT: "Printer Busy (1)" error is now FIXED ✅**
  - Official INSTAX Wide app now proceeds to print
  - However, all official apps (Mini, Square, Wide) crash during print sequence
  - Crash occurs ~120ms after print START ACK, before DATA transfer

- **New packet captures added:**
  - `Real_iPhone_to_Wide_print_a.pklg` - Full print sequence from real Wide
  - `Real_iPhone_to_Wide_print_b.pklg` - Latest capture (smaller)

### December 2025 (Wide Link MAC Address Discovery)
- **CRITICAL DISCOVERY: Wide Link uses DIFFERENT MAC pattern than Mini/Square**
  - Real Wide FI022 hardware uses `fa:ab:bc:55:dd:c2` (captured via Wireshark)
  - 4th byte is **0x55**, NOT 0x8X range like Mini/Square
  - Previous documentation incorrectly stated 0x8X was required for Wide app filtering
  - Wide app specifically filters for 0x55 pattern - using 0x87 causes discovery failure
- **Updated model-specific MAC patterns:**
  - Mini Link 3: `fa:ab:bc:86:55:00` (0x86 matches real hardware) ✅ VERIFIED
  - Square Link: `fa:ab:bc:87:55:02` (0x87 tested working) ✅ VERIFIED
  - Wide Link: `fa:ab:bc:55:55:01` (0x55 from real FI022) ✅ VERIFIED
  - Each model uses unique suffix to prevent iOS cache conflicts
- **Device name updated:** "WIDE-205555" → "INSTAX-205555" (matches real printer format)
- **Added detailed MAC pattern table** with common mistakes section
- **iOS cache handling:** Use unique MAC suffix for each model to prevent cache conflicts

### December 2025 (Square Link Film Count Discovery)
- **CRITICAL DISCOVERY: Square Link uses capability byte nibble encoding** (same as Wide Link)
  - Previous documentation incorrectly stated Square uses payload[5] for film count
  - Verified with real FI017 printer: capability byte 0x26 = 0x20 (Square) | 0x06 (6 films)
  - Payload[5] contains 0x0C (12) on both Square and Wide - purpose unknown, NOT film count
  - Updated all code examples and parsing logic to reflect correct behavior
- **Updated model detection ranges:**
  - Square Link (0x20-0x2F): Film count in capability byte lower nibble ✅ VERIFIED
  - Wide Link (0x30-0x3F): Film count in capability byte lower nibble ✅ VERIFIED
  - Older Mini (0x10-0x1F): Film count in payload[5] (unverified)
- **Updated Mini Link 3 FFF1 characteristic parsing:**
  - Fixed off-by-one error: byte 0 contains photos USED, not remaining
  - Calculation: 10 - photos_used = photos_remaining
  - Added logic to prevent standard query from overwriting FFF1 data for Link 3
- **Added comprehensive film count parsing section** with model-specific examples
- **Documented 0x0C mystery value** appearing at payload[5] for Square and Wide printers

### December 2025 (Wide Link FFE1 Fix)
- **CRITICAL: Fixed Wide FFE1 characteristic properties** - FFE1 must be Write/WriteNoResponse/Notify (NOT Read/Notify)
  - Real Wide printer uses write-to-request, notify-to-respond pattern
  - Previous "Printer Busy (1)" error was caused by incorrect characteristic configuration
  - Added warning to documentation: `BLE_GATT_CHR_F_READ` is WRONG for FFE1
- **Documented Wide-specific ping response** - Byte 9 must be `0x01` for Wide (vs `0x02` for Mini/Square)
- **Documented Wide-specific dimensions response** - 19 bytes total (vs 23 bytes for Mini/Square)
  - Wide capability bytes: `02 7B 00 05 28 00`
- **Documented Wide Additional Info type 0x01** - Bytes 11-15 = `1E 00 01 01 00` (Wide-specific)
- **Updated Official App Compatibility section** - Status now reflects FFE1 fix testing

### December 2025 (Earlier Updates)
- **Added Mini Link device name format documentation** - Discovered `(BLE)` suffix required for Mini printers (e.g., `INSTAX-70423278(BLE)`)
- **Documented accelerometer value ranges** - Confirmed -1000 to +1000 typical range for X/Y/Z axes from physical printer capture
- **Added real accelerometer data sample** from physical Mini Link printer showing X=-321, Y=-467, Z=-37, Orientation=123
- **Discovered Command 0x23** - New command identified in shutter button context, purpose TBD
- **Added C structure definition** for accelerometer data (int16_t x/y/z, uint8_t orientation)
- **Updated Link 3 Accelerometer section** with packet capture analysis and hex dumps
- **Cross-referenced new Bluetooth capture** - `iPhone_INSTAX_capture-4.pklg` (Mini Link with accelerometer and shutter button usage)
- **Added Connection Exchange Sequence section** - Complete documentation of INSTAX app connection handshake (10-stage protocol validation)
- **Documented all four rounds of ESP32 simulator fixes** with before/after code examples and specific line references
- **Added Common Implementation Errors section** with ❌/✅ examples showing wrong vs correct implementations
- **Created Testing Checklist** for validating connection compatibility (pre-connection, DIS, handshake, sensor queries)
- **Added Debugging Connection Issues table** mapping symptoms to causes and solutions
- **Documented Stage 5: Sensor/Capability Verification** including operation 0x10 (INSTAX_OP_ADDITIONAL_INFO) responses
- **Packet-level documentation** of all 10 handshake commands with hex dumps, payload breakdowns, and critical notes

### November 2025
- Initial documentation created
- Fixed critical packet length bug (length should be 7 + payload, not just payload)
- Fixed response parsing bug in payload extraction
- Documented Link 3 specific services and behavior
- Added comprehensive examples and error codes
- **Added WiFi protocol comparison section** documenting SP-2/SP-3 differences
- **Added missing features section** identifying commands to investigate
- **Documented AUTO_SLEEP_SETTINGS** as high-priority feature to reverse engineer
- **Added investigation methodology** for discovering power management commands
- **Cross-referenced jpwsutton/instax_api** for WiFi protocol features
- **Added Official Instax App Compatibility section** documenting ESP32 simulator compatibility with official Fujifilm apps
- **Documented protocol command differences** between Moments Print and official Instax app (op=0x02 vs op=0x01)
- **Cross-referenced ESP32 investigation documentation** (OFFICIAL_APP_COMPATIBILITY.md, SQUARE_LINK_FINDINGS.md, SESSION_SUMMARY_2025-11-30.md)

---

**Note:** This is a reverse-engineered protocol. Fujifilm has not officially documented this protocol, and it may change in future printer models or firmware updates. The WiFi (SP series) and Bluetooth (Link series) protocols are fundamentally different and incompatible.
