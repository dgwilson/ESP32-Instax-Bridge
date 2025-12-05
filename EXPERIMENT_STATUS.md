# Current Experiment Status

**Last Updated:** December 5, 2024

## Active Experiment

**Experiment 1: Capability Byte 0x28 (capture-2 pattern)**

### Current Firmware State
- **Capability byte:** 0x28 (not charging) / 0xA8 (charging)
- **Bit pattern:** 00101000 = bits 5,3
- **Source:** capture-2 packet analysis (successful prints)
- **Commit:** `15c6f8b`

### Changes from Baseline
```diff
- uint8_t capability = 0x26;  // capture-3 pattern (bits 5,2,1)
+ uint8_t capability = 0x28;  // capture-2 pattern (bits 5,3)
```

### Testing Instructions
1. Power cycle ESP32 (wait for it to advertise)
2. Open official Fujifilm INSTAX app on iPhone
3. Connect to "INSTAX-50196563"
4. Select photo and tap Print
5. Observe behavior and monitor serial output

### Expected Serial Output
Look for capability byte in printer function response:
```
I (xxxxx) ble_peripheral: Sending printer function: XX photos, charging=1
...
Response: 61 42 00 11 00 02 00 02 A8 00 00 XX ...
                                    ^^
                                    Should be 0xA8 (charging) or 0x28 (not charging)
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
- Baseline: `8fb9f0a` (capability 0x26)
- Experiment 1: `15c6f8b` (capability 0x28)

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
