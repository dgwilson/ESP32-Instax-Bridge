# BLE Advertising Analysis - ESP32 Instax Simulator

## Current ESP32 Simulator Advertising Structure

This document details what the ESP32 Instax Bridge currently advertises over Bluetooth LE and provides guidance for making it compatible with the official Instax apps.

### Main Advertising Packet

The ESP32 currently advertises the following in the main packet:

- **Flags**: `0x06` (General Discoverable + BR/EDR Not Supported)
  - BLE_HS_ADV_F_DISC_GEN (0x02) - General discoverable mode
  - BLE_HS_ADV_F_BREDR_UNSUP (0x04) - BR/EDR not supported

- **Complete 128-bit Service UUID List**:
  - `70954782-2d83-473d-9e5f-81e1d02d5273` (Instax service)
  - Marked as "complete" (all services listed)

- **Manufacturer Specific Data**:
  - Company ID: `0x04DB` (Fujifilm Corporation) - stored as `{0xDB, 0x04}` (little-endian)
  - Additional bytes: `{0x07, 0x00}` (2 bytes - observed from real device)
  - Total: 4 bytes `{0xDB, 0x04, 0x07, 0x00}`

### Scan Response Packet

- **Complete Local Name**:
  - `"INSTAX-55550000"` (matches official Instax naming pattern)
  - Marked as "complete name"

### Advertising Parameters

- **Interval**: 100-150ms
  - Min: 160 units (100ms in 0.625ms units)
  - Max: 240 units (150ms in 0.625ms units)

- **Connection Mode**: Undirected connectable (`BLE_GAP_CONN_MODE_UND`)

- **Discovery Mode**: General discoverable (`BLE_GAP_DISC_MODE_GEN`)

- **Address Type**: Public (`BLE_OWN_ADDR_PUBLIC`)
  - Derived from ESP32 MAC address
  - Example: `70:04:1d:6f:2f:78` (varies per ESP32 device)

### GATT Services Available After Connection

#### Instax Custom Service
- **Service UUID**: `70954782-2d83-473d-9e5f-81e1d02d5273`
- **Write Characteristic**: `70954783-2d83-473d-9e5f-81e1d02d5273`
  - Properties: Write, Write Without Response
- **Notify Characteristic**: `70954784-2d83-473d-9e5f-81e1d02d5273`
  - Properties: Read, Notify

#### Device Information Service (Standard BLE Service)
- **Model Number**: Model-specific
  - Mini Link 3: `"FI033"`
  - Square Link: `"FI020"`
  - Wide Link: `"FI022"`
- **Serial Number**: `"70423278"`
- **Firmware Revision**: `"0101"`
- **Hardware Revision**: `"0001"`
- **Manufacturer Name**: `"FUJIFILM Corporation"`

#### GAP Service (Standard)
- Device name, appearance, etc.

#### GATT Service (Standard)
- Service changed characteristic

---

## Investigation: Official Instax App Compatibility

### Problem Statement
The official Instax Square Link app does not detect the ESP32 simulator configured as a Square Link printer, despite:
- ✅ Using official naming pattern `"INSTAX-XXXXXXXX"`
- ✅ Advertising Fujifilm manufacturer data (0x04DB)
- ✅ Advertising correct service UUID
- ✅ Exposing Device Information Service with model number FI020

### Potential Missing Factors

The official app may filter on additional criteria not currently implemented:

#### 1. **BLE Address Pattern**
- **Current**: ESP32 uses MAC-derived address (e.g., `70:04:1d:6f:2f:78`)
- **Real Devices**: May use specific Fujifilm OUI (Organizationally Unique Identifier)
- **Investigation**: Check if real Square Link uses specific address prefix
- **Fix Difficulty**: **HARD** - Cannot easily change ESP32 MAC OUI without hardware modifications

#### 2. **Manufacturer Data Format**
- **Current**: 4 bytes `{0xDB, 0x04, 0x07, 0x00}`
- **Real Devices**: May include additional model-specific bytes
- **Investigation**: Capture real Square Link manufacturer data
- **Fix Difficulty**: **EASY** - Simple byte array change

#### 3. **Advertising Flags**
- **Current**: `0x06` (General Discoverable + BR/EDR Not Supported)
- **Real Devices**: May use different flag combinations
- **Investigation**: Check exact flag byte from real device
- **Fix Difficulty**: **EASY** - Single parameter change

#### 4. **Service Data (vs Manufacturer Data)**
- **Current**: Only manufacturer data in advertising packet
- **Real Devices**: May include service data (UUID + associated data)
- **Investigation**: Check if service data is present in real device advertising
- **Fix Difficulty**: **MEDIUM** - Requires code changes to add service data

#### 5. **TX Power Level**
- **Current**: Not advertised
- **Real Devices**: May include TX power level in advertising packet
- **Investigation**: Check if TX power is advertised
- **Fix Difficulty**: **EASY** - Add TX power to advertising fields

#### 6. **Advertising Interval**
- **Current**: 100-150ms
- **Real Devices**: May use different interval for faster/slower discovery
- **Investigation**: Measure real device advertising interval
- **Fix Difficulty**: **EASY** - Simple parameter change

#### 7. **Connection Parameters**
- **Current**: Default NimBLE stack parameters
- **Real Devices**: May advertise preferred connection parameters
- **Investigation**: Check if connection parameters are in advertising packet
- **Fix Difficulty**: **MEDIUM** - Requires adding connection parameter field

#### 8. **Device Information Service Completeness**
- **Current**: Model, Serial, Firmware, Hardware, Manufacturer
- **Real Devices**: May expose additional DIS characteristics (PnP ID, System ID, etc.)
- **Investigation**: Connect to real device and enumerate all DIS characteristics
- **Fix Difficulty**: **EASY** - Add missing characteristics

#### 9. **Additional GATT Services**
- **Current**: Instax + DIS + GAP + GATT
- **Real Devices**: May expose additional standard services (Battery Service, etc.)
- **Investigation**: Full GATT enumeration of real device
- **Fix Difficulty**: **MEDIUM** - Requires implementing additional services

---

## Recommended Investigation Steps

### Step 1: Capture Real Square Link Advertising Data

Use **nRF Connect** (iOS/Android app) or **Bluetooth Explorer** (macOS) to scan while the real Square Link is powered on:

1. Open nRF Connect and start scanning
2. Find the real Square Link printer in the list
3. **DO NOT CONNECT** - just capture advertising data
4. Screenshot or note:
   - Complete advertising packet hex dump
   - All advertising fields (Flags, Service UUIDs, Manufacturer Data, etc.)
   - Device address (MAC)
   - RSSI and TX Power (if shown)
   - Scan response data

**Key Information to Capture:**
- Flags byte value
- Manufacturer data (all bytes)
- Service data (if present)
- Advertised TX power
- Device address pattern
- Advertising interval (time between packets)

### Step 2: Connect and Enumerate Services

After capturing advertising data, connect to the real device with nRF Connect:

1. Tap on the Square Link to connect
2. Expand all services and characteristics
3. Screenshot the complete service tree
4. Note any services beyond: Instax Custom, DIS, GAP, GATT
5. Read all Device Information Service characteristics
6. Note any additional characteristics not currently implemented

**Key Information to Capture:**
- Complete service list (UUIDs)
- All characteristic UUIDs and properties for each service
- Device Information Service: read ALL characteristics (especially PnP ID, System ID)
- Any battery, scanning, or other standard services

### Step 3: Compare with ESP32 Simulator

Create a comparison table:

| Field | Real Square Link | ESP32 Simulator | Match? |
|-------|------------------|-----------------|--------|
| Device Name | ? | INSTAX-55550000 | ? |
| Flags | ? | 0x06 | ? |
| Service UUID | ? | 70954782... | ? |
| Manufacturer Data | ? | DB 04 07 00 | ? |
| Service Data | ? | None | ? |
| TX Power | ? | Not advertised | ? |
| Device Address | ? | 70:04:1d:... | ? |
| Model Number | ? | FI020 | ? |
| Serial Number | ? | 70423278 | ? |
| Additional Services | ? | None | ? |

### Step 4: Implement Missing Fields

Based on the comparison, update ESP32 code:

1. **Easy Fixes** (1-2 lines of code):
   - Manufacturer data bytes
   - Advertising flags
   - TX power level
   - Advertising interval
   - Additional DIS characteristics

2. **Medium Fixes** (10-50 lines of code):
   - Service data in advertising packet
   - Connection parameter advertising
   - Additional GATT services (Battery, etc.)

3. **Hard Fixes** (may not be possible):
   - BLE address OUI matching (requires hardware change)
   - Proprietary authentication (if device uses pairing/bonding)

---

## Code Locations for Fixes

All advertising code is in: `/Users/dgwilson/Projects/ESP32-Instax-Bridge/main/ble_peripheral.c`

### Advertising Data (Main Packet)
**Function**: `ble_peripheral_start_advertising()` (line 666)

**Current code** (lines 688-716):
```c
static uint8_t mfg_data[] = {
    0xDB, 0x04,  // Company ID: 0x04DB (Fujifilm Corporation) - little-endian
    0x07, 0x00   // Additional data observed from real device
};

struct ble_hs_adv_fields fields = {0};
fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
fields.uuids128 = (ble_uuid128_t[]) { instax_service_uuid };
fields.num_uuids128 = 1;
fields.uuids128_is_complete = 1;
fields.mfg_data = mfg_data;
fields.mfg_data_len = sizeof(mfg_data);
```

**To add TX Power**:
```c
fields.tx_pwr_lvl = 0;  // 0 dBm (adjust based on real device)
fields.tx_pwr_lvl_is_present = 1;
```

**To add Service Data**:
```c
static uint8_t svc_data[] = { /* service data bytes */ };
fields.svc_data_uuid128 = instax_service_uuid;
fields.svc_data_uuid128_len = sizeof(svc_data);
fields.svc_data_uuid128 = svc_data;
```

### Advertising Parameters
**Current code** (lines 733-737):
```c
struct ble_gap_adv_params adv_params = {0};
adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;
adv_params.itvl_min = 160;  // 100ms (in 0.625ms units)
adv_params.itvl_max = 240;  // 150ms (in 0.625ms units)
```

**To change interval**: Adjust `itvl_min` and `itvl_max` based on real device

### Device Information Service
**Function**: `ble_peripheral_init()` (line 829)

**Current code** (lines 862-872):
```c
ble_svc_dis_model_number_set("FI020");  // For Square
ble_svc_dis_serial_number_set("70423278");
ble_svc_dis_firmware_revision_set("0101");
ble_svc_dis_hardware_revision_set("0001");
ble_svc_dis_manufacturer_name_set("FUJIFILM Corporation");
ble_svc_dis_init();
```

**To add PnP ID** (example):
```c
struct ble_svc_dis_pnp_id pnp = {
    .vendor_id_src = 0x01,  // Bluetooth SIG assigned
    .vendor_id = 0x04DB,     // Fujifilm
    .product_id = 0x0020,    // Square Link (FI020)
    .product_version = 0x0101 // Firmware 0101
};
ble_svc_dis_pnp_id_set(&pnp);
```

---

## Most Likely Causes (Ranked by Probability)

1. **Manufacturer Data Mismatch** (90% likely)
   - Real device probably has additional bytes in manufacturer data
   - May include model identifier, firmware version, etc.
   - **FIX**: Capture real device manufacturer data and update array

2. **Missing Service Data** (70% likely)
   - Official app may filter on service data instead of/in addition to manufacturer data
   - Service data associates data directly with the service UUID
   - **FIX**: Capture and add service data to advertising packet

3. **BLE Address Pattern** (60% likely)
   - Fujifilm devices may use consistent OUI prefix
   - Official app may whitelist only specific address ranges
   - **FIX**: Difficult - may require MAC address spoofing (not recommended)

4. **Missing DIS Characteristics** (40% likely)
   - Real device may expose PnP ID with specific vendor/product IDs
   - Official app may query DIS after connection to validate device
   - **FIX**: Add missing DIS characteristics based on real device

5. **Advertising Flags or Interval** (20% likely)
   - Less likely to be the sole cause, but could contribute
   - **FIX**: Match exact values from real device

6. **Additional GATT Services** (10% likely)
   - Unlikely to prevent discovery, but may cause connection failures
   - **FIX**: Implement any missing services found on real device

---

## Next Steps

1. **User Action Required**: Use nRF Connect to capture:
   - Complete advertising packet from real Square Link
   - All GATT services and characteristics after connection

2. **Analysis**: Compare captured data with this document

3. **Implementation**: Update ESP32 code based on findings

4. **Testing**: Verify official app now detects simulator

5. **Documentation**: Update this file with findings and final working configuration
