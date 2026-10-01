# CarTouch — Current Roadmap

This document contains current status and remaining work only; historical patch lists are intentionally omitted.

## Current baseline — 1.0.0

Core CAN, OBD-II, TFT/LVGL, Wi-Fi/Web, BLE/BLE OTA, built-in DBC profiles, custom profiles, Learn Mode, verification, runtime CAN pin configuration, Extended CAN ID handling, power management, error logging and firmware/filesystem OTA are implemented.

## Remaining work

### Hardware integration
- Assign and validate physical RGB LED GPIOs from the actual board wiring.
- Validate CAN transceiver, TFT/touch and power behavior on hardware.

### DBC expansion
- Add multi-file DBC composition without breaking current profiles.
- Add CAN-FD only when hardware and driver support it.

### Verification depth
- Add hardware-in-the-loop tests for CAN, bus-off recovery, touch, Wi-Fi, BLE and OTA.
- Exercise each direct-menu DBC profile on representative hardware before treating mappings as vehicle-specific guarantees.

### Productization
- Finalize enclosure, wiring, manufacturing provisioning and release/update procedures.

## Priority rule

Core functionality and safety come before commercial packaging. Existing capabilities must be preserved while defects are corrected.
