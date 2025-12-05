# Square Link Real Device Analysis - RESOLVED ✅

**Investigation Date**: 2025-11-30
**Device**: Real Fujifilm Instax Square Link Printer
**Tools Used**: nRF Connect for iOS
**Outcome**: All critical differences identified and fixed

---

## Summary

The official Instax app was not detecting the ESP32 simulator because of **4 critical mismatches** in the BLE advertising data and Device Information Service. All issues have been resolved.

---

## Differences Found Between Real Device and Simulator

### 1. ❌ Manufacturer Data (CRITICAL - NOW FIXED)

**Real Square Link**:
```
Company ID: 0x04D8
Additional bytes: 0x05 0x00
Full data: <04D8> 0500
```

**ESP32 Simulator (BEFORE FIX)**:
```
Company ID: 0x04DB (WRONG!)
Additional bytes: 0x07 0x00 (WRONG!)
Full data: <04DB> 0700
```

**FIX APPLIED**: Updated manufacturer data in `ble_peripheral.c:688-690`:
```c
static uint8_t mfg_data[] = {
    0xD8, 0x04,  // Company ID: 0x04D8 (Fujifilm - captured from real Square Link)
    0x05, 0x00   // Additional data from real Square Link
};
```

---

### 2. ❌ TX Power Not Advertised (NOW FIXED)

**Real Square Link**:
- Advertises **TX Power: 3 dBm**

**ESP32 Simulator (BEFORE FIX)**:
- Did NOT advertise TX power

**FIX APPLIED**: Added TX power advertising in `ble_peripheral.c:701-703`:
```c
// Add TX Power (real Square Link advertises 3 dBm)
fields.tx_pwr_lvl = 3;
fields.tx_pwr_lvl_is_present = 1;
```

---

### 3. ❌ Model Number Wrong (NOW FIXED)

**Real Square Link**:
- Device Information Service → Model Number String: **"SQ20"**

**ESP32 Simulator (BEFORE FIX)**:
- Model Number String: **"FI020"** (WRONG!)

**FIX APPLIED**: Changed model number in `ble_peripheral.c:821`:
```c
case INSTAX_MODEL_SQUARE:
    return "SQ20";   // Square Link (captured from real device)
```

---

### 4. ❌ Firmware/Software Revision Format (NOW FIXED)

**Real Square Link**:
- Firmware Revision String: **"01.01"** (with period)
- Software Revision String: **"01.01"** (with period)

**ESP32 Simulator (BEFORE FIX)**:
- Firmware Revision String: **"0101"** (no period)
- Software Revision String: **"0101"** (no period)

**FIX APPLIED**: Updated revision strings in `ble_peripheral.c:871-873`:
```c
ble_svc_dis_firmware_revision_set("01.01");  // Firmware version (from real Square Link)
ble_svc_dis_software_revision_set("01.01");  // Software version (from real Square Link)
```

---

## Real Square Link - Complete BLE Profile

### Advertising Data (Before Connection)

| Field | Value |
|-------|-------|
| **Device Name** | INSTAX-50196562 (IOS) |
| **Connectable** | Yes |
| **Manufacturer Data** | `<04D8> 0500` |
| **Services** | `70954782-2D83-473D-9E5F-81E1D02D5273` |
| **TX Power** | 3 dBm |
| **RSSI** | ~-52 to -54 dBm |

### Device Information Service (After Connection)

| Characteristic | Value |
|----------------|-------|
| **Manufacturer Name String** | FUJIFILM Corporation |
| **Model Number String** | SQ20 |
| **Firmware Revision String** | 01.01 |
| **Software Revision String** | 01.01 |

### GATT Services (After Connection)

1. **Device Information Service** (Standard BLE Service)
   - Manufacturer Name String (Read)
   - Model Number String (Read)
   - Firmware Revision String (Read)
   - Software Revision String (Read)

2. **70954782-2D83-473D-9E5F-81E1D02D5273** (Instax Custom Service)
   - **70954783-2D83-473D-9E5F-81E1D02D5273** (Write characteristic, no descriptors)
   - **70954784-2D83-473D-9E5F-81E1D02D5273** (Notify characteristic, has CCCD descriptor)

---

## Why Official App Wasn't Detecting Simulator

The official Instax Square Link app likely filters devices using **multiple criteria**:

1. **Service UUID** ✅ (simulator had this correct)
2. **Manufacturer Data** ❌ (CRITICAL - wrong company ID and bytes)
3. **TX Power** ❌ (not advertised)
4. **Model Number** ❌ (validated after connection - "FI020" vs "SQ20")

The **manufacturer data mismatch** was almost certainly the primary reason for rejection. Official apps typically whitelist specific manufacturer data patterns to avoid connecting to non-Fujifilm devices.

---

## Result After Fixes

With all 4 fixes applied:

1. ✅ Manufacturer data now matches: `<04D8> 0500`
2. ✅ TX Power now advertised: 3 dBm
3. ✅ Model number now correct: "SQ20"
4. ✅ Firmware/Software revision format matches: "01.01"

**UPDATE (2025-11-30)**: The official Instax Square Link app now **discovers and connects** to the ESP32 simulator successfully! However, the app's UI still shows "Device not connected" and "???" for film count/battery, even though all protocol queries are answered correctly. See `OFFICIAL_APP_COMPATIBILITY.md` for detailed investigation notes.

---

## Testing Instructions

1. **Rebuild and flash ESP32**:
   ```bash
   cd /Users/dgwilson/Projects/ESP32-Instax-Bridge
   idf.py build
   idf.py flash
   ```

2. **Set simulator to Square Link mode** (via web interface):
   - Navigate to http://instax-simulator.local
   - Set model to "Square"
   - Restart BLE advertising

3. **Test with official Instax app**:
   - Open official Instax Square Link app on iOS
   - Look for "INSTAX-55550000" in device list
   - App should now detect and allow connection

4. **Verify with nRF Connect** (optional):
   - Scan for "INSTAX-55550000"
   - Expand advertising data
   - Confirm: Manufacturer Data = `<04D8> 0500`, TX Power = 3 dBm
   - Connect and check Model Number = "SQ20"

---

## Files Modified

All changes made to: `/Users/dgwilson/Projects/ESP32-Instax-Bridge/main/ble_peripheral.c`

- Line 688-690: Manufacturer data bytes (04D8, 0500)
- Line 701-703: TX Power advertising (3 dBm)
- Line 821: Model number for Square Link ("SQ20")
- Line 871: Firmware revision format ("01.01")
- Line 873: Software revision format ("01.01")

---

## Notes for Other Printer Models

These findings are specific to **Square Link** only. Other models (Mini Link 3, Wide Link) may have different:
- Manufacturer data bytes
- Model number strings
- TX power levels

If investigating other models, use the same methodology:
1. Scan with nRF Connect (capture advertising data)
2. Connect and read Device Information Service
3. Compare with simulator
4. Apply fixes

---

## Additional Findings

### Device Naming Pattern
Real Square Link uses pattern: `INSTAX-XXXXXXXX (IOS)`
- `INSTAX-` prefix (all caps)
- 8-digit number (appears to be unique per device)
- ` (IOS)` suffix when connected from iOS device

### Advertising Interval
Real device advertises approximately every 27-49ms based on packet timestamps in nRF Connect.

### Connection Parameters
Real device accepts connection immediately with no pairing/bonding required.
