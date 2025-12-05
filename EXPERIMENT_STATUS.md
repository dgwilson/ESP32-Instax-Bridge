# Current Experiment Status

**Last Updated:** December 5, 2024

## Active Experiment

**Experiment 2: Force Charging = False**

### Current Firmware State
- **Capability byte:** 0x26 (always, charging forced false)
- **Bit pattern:** 00100110 = bits 5,2,1
- **Charging flag:** Forced to `false` in `printer_emulator.c`
- **Source:** Testing if official app rejects charging printers
- **Commit:** `2335a80`

### Changes from Baseline
```diff
+ // In printer_emulator.c after NVS load:
+ s_printer_info.is_charging = false;  // Force not charging
+
+ // Capability byte remains 0x26 but will never have bit 7 set
```

### Testing Instructions
1. Power cycle ESP32 (wait for it to advertise)
2. Open official Fujifilm INSTAX app on iPhone
3. Connect to "INSTAX-50196563"
4. Select photo and tap Print
5. Observe behavior and monitor serial output

### Expected Serial Output
Look for:
1. Charging forced to false on boot:
```
I (xxxxx) printer_emulator: EXPERIMENT 2: Forcing is_charging = false
```

2. Capability byte 0x26 (not charging) in printer function response:
```
I (xxxxx) ble_peripheral: Sending printer function: XX photos, charging=0
...
Response: 61 42 00 11 00 02 00 02 26 00 00 XX ...
                                    ^^
                                    Should be 0x26 (not charging, never 0xA6)
```

## Baseline for Comparison

**Commit:** `8fb9f0a` - Capability byte 0x26 matches capture-3 physical printer

### Revert to Baseline
```bash
cd /Users/dgwilson/Projects/ESP32-Instax-Bridge
git checkout 8fb9f0a
. ~/esp/esp-idf/export.sh
idf.py build
idf.py flash
```

## Next Experiments (if Experiment 1 fails)

### Experiment 2: Set Charging Flag to False
- Modify `printer_emulator.c` to set `is_charging = false`
- Test if official app rejects charging printers

### Experiment 3: Firmware Version Variations
- Try firmware versions: "0100", "0102", "0001"
- Test if official app whitelists specific firmware versions

### Experiment 4: Response Timing Delays
- Add delays before sending notifications
- Match physical printer response timing from packet captures

## Quick Reference

### Git Commits
- Baseline: `8fb9f0a` (capability 0x26, charging from NVS)
- Experiment 1: `15c6f8b` (capability 0x28) - ❌ FAILED
- Experiment 2: `2335a80` (capability 0x26, charging forced false) - ⏳ TESTING

### Key Files
- Protocol implementation: `main/ble_peripheral.c`
- Printer state: `main/printer_emulator.c`
- Documentation: `CLAUDE.md`, `INSTAX_PROTOCOL.md`
- Experiment log: `EXPERIMENTATION_LOG.md`

### Serial Monitor
```bash
. ~/esp/esp-idf/export.sh
idf.py monitor
```

Press `Ctrl+]` to exit monitor.
