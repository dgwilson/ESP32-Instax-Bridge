# Official INSTAX App Compatibility Experiments

**Baseline Commit:** `8fb9f0a` - Capability byte 0x26 matches capture-3 physical printer

**Goal:** Identify why official INSTAX app rejects simulator despite correct protocol implementation

---

## Experiment 1: Capability Byte 0x28 (capture-2 pattern)

**Date:** December 5, 2024

**Hypothesis:** Official app might expect capability pattern from capture-2 (0x28 = bits 5,3) instead of capture-3 (0x26 = bits 5,2,1)

**Rationale:**
- capture-2 shows successful prints with capability 0x28
- capture-3 shows capability 0x26 but only initial connection (no visible print in ATT frames)
- Different printers or firmware versions may use different bit patterns
- Official app might whitelist specific capability patterns

**Changes:**
- `ble_peripheral.c:358` - Change capability from 0x26 to 0x28
- When charging: 0xA8 instead of 0xA6

**Testing:**
1. Flash firmware with 0x28 capability
2. Connect official INSTAX app
3. Attempt to print
4. Monitor serial output for any changes in app behavior

**Result:** _[To be filled after testing]_

---

## Experiment 2: Set Charging Flag to False

**Hypothesis:** Official app might reject printers that are currently charging

**Changes:**
- `printer_emulator.c` - Set `is_charging = false`
- Capability will be 0x26 (not charging)

**Result:** _[To be filled after testing]_

---

## Experiment 3: Firmware Version Variation

**Hypothesis:** Official app might whitelist/blacklist specific firmware versions

**Changes:**
- `ble_peripheral.c:1219` - Try different firmware versions:
  - Option A: "0100" (one version older)
  - Option B: "0102" (one version newer)
  - Option C: "0001" (much older)

**Result:** _[To be filled after testing]_

---

## Experiment 4: Response Timing Delays

**Hypothesis:** Official app is sensitive to response timing

**Changes:**
- Add small delays before sending notifications
- Match physical printer response timing from packet captures

**Result:** _[To be filled after testing]_

---

## Revert Instructions

To return to baseline:
```bash
git checkout 8fb9f0a
. ~/esp/esp-idf/export.sh
idf.py build
idf.py flash
```
