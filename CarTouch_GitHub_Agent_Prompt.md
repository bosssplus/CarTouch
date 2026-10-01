# CarTouch — Comprehensive ESP32-S3 Core Functionality, Reliability & Architecture Improvement

Act as a senior embedded systems engineer specializing in ESP32-S3, ESP-IDF/PlatformIO, automotive CAN/OBD-II, embedded storage, and reliable firmware architecture.

Your task is to thoroughly audit, repair, and improve the existing CarTouch repository, prioritizing real functionality, reliability, and compatibility over unnecessary refactoring or cosmetic changes.

## 1. Operating Rules and Priorities

- Work directly on the existing `main` branch of this repository. The user has explicitly authorized direct changes and commits to `main`; no separate branch or pull request is required.
- The original project is preserved independently elsewhere. This repository is a separate working copy.
- Inspect the actual source code, configuration, dependencies, build system, tests, and documentation before modifying anything.
- Preserve existing working functionality. Do not remove, disable, or simplify core features merely to make implementation easier.
- Prioritize defects affecting actual device operation. Fix straightforward, safe issues first, followed by necessary architectural corrections.
- Avoid unrelated large-scale refactoring.
- Do not stop after identifying problems or producing recommendations. Implement the necessary fixes and validate them.

## 2. Supported Hardware: ESP32-S3 Only

Support ESP32-S3 boards with different Flash capacities, PSRAM configurations, pin layouts, and peripheral combinations.

Examples include:
- ESP32-S3 boards with 4MB Flash and no PSRAM.
- ESP32-S3 boards with 4MB Flash and 2MB PSRAM.
- Other ESP32-S3 variants with larger Flash or different PSRAM capacities.

ESP32-C6 is explicitly out of scope.

Implement hardware abstraction and appropriate build, memory, and partition configurations. Never assume that every board has PSRAM, identical GPIO assignments, or sufficient space for a large filesystem or OTA partition.

## 3. Optional Hardware and Independent Operation

Display, touch, five-way buttons, and SD card must be optional peripherals, not mandatory application dependencies.

Support these configurations:
- Display with touch.
- Display without touch.
- Five-way physical buttons.
- Display and buttons together.
- Headless operation without display, touch, or buttons.
- Operation with or without an SD card.

Support different display controllers, resolutions, orientations, and interfaces through modular drivers and configuration.

Five-way buttons must support suitable GPIO or ADC-based implementations where hardware permits.

Failure or absence of an optional peripheral must not stop unrelated application functions, cause reboot loops, or prevent normal startup.

## 4. Runtime GPIO and Hardware Configuration

Allow users to configure supported GPIO assignments after firmware installation, without recompilation whenever technically feasible.

Configuration must be accessible through Web UI, BLE, or USB Serial, and persist safely in NVS.

Implement:
- GPIO validation and conflict detection.
- Protection against reserved or unavailable pins.
- Safe configuration changes and rollback.
- Recovery and factory-reset mechanisms.
- Clear feedback when a requested pin assignment is invalid.

## 5. Preserve and Verify Core Functionality

Audit and preserve the existing CarTouch capabilities, including:

- CAN communication and OBD-II.
- Vehicle monitoring and signal decoding.
- DBC-based vehicle support.
- Learn, manual CAN capture, recording, and verification.
- Diagnostics and vehicle configuration.
- Web, BLE, and Serial interfaces.

Keep these interfaces independent of the display and usable simultaneously where hardware resources permit.

Review CAN learning and verification workflows carefully. Preserve safe pending transactions, explicit verification, and appropriate protection against unverified data being treated as confirmed. Audit legacy override paths, including `_legacyOverrideActive`, `setCustomCANIDs()`, and `_sendLegacyCommand()`, and safely remove or migrate them where necessary without breaking valid functionality.

## 6. DBC Database and Regional Optimization

Audit the complete DBC collection for duplicates, obsolete definitions, unnecessary size, and actual usefulness.

Prioritize vehicles relevant to Iran and its regional market, including domestic, Chinese, imported, and shared vehicle platforms.

Do not delete useful definitions solely because a vehicle originates from another country.

Separate essential databases from optional extended collections. Support importing, updating, exporting, and managing vehicle databases without reflashing firmware whenever feasible.

Do not load the entire database into RAM unnecessarily. Preserve existing vehicle decoding and Learn functionality.

## 7. Flexible Storage, SD Card, and User Data Protection

Implement a unified storage abstraction supporting internal ESP32-S3 storage and optional SD card storage.

**SD is optional. CarTouch must boot and operate its core functions without an SD card.**

Allow users to choose where databases and other data are stored:

- Essential and frequently used databases may reside in internal Flash.
- External DBC collections may reside internally or on SD.
- Users may move, copy, import, export, or maintain selected backup copies.
- Manually captured CAN recordings and learned profiles must be protected from firmware and database updates.

When SD is unavailable, use internal storage for databases, recordings, and user data as far as available capacity safely permits.

When storage is insufficient, warn the user and offer alternatives. Never silently discard or overwrite user-generated data.

Provide backup, restore, integrity validation, safe file replacement, and recovery mechanisms. Handle missing, full, corrupted, or unexpectedly removed SD cards without stopping unrelated functionality.

Support firmware updates from SD where compatible with the actual bootloader, partition layout, and hardware. Preserve user settings and data across updates.

Expose storage capacity, free space, database status, backup status, and errors through available Web, BLE, and Serial interfaces.

## 8. Memory, Reliability, and Recovery

Audit RAM, PSRAM, Flash, filesystem, OTA, web assets, DBC loading, BLE, and CAN buffer usage.

Ensure appropriate behavior across supported ESP32-S3 memory configurations.

Use safe initialization, non-blocking peripheral failure handling, bounded memory usage, and recovery mechanisms. Avoid unnecessary dynamic allocations and unsafe storage operations.

Do not apply a large-Flash partition layout to a 4MB board without verifying feasibility.

## 9. Documentation and Repository Cleanup

Update documentation to reflect the actual implementation, supported hardware, configuration, installation, storage behavior, limitations, and recovery procedures.

Remove or consolidate duplicate, obsolete, contradictory documentation, outdated roadmaps, broken references, and descriptions of already-fixed bugs.

Do not claim a feature is supported unless the implementation and available verification justify that claim.

## 10. Build, Testing, and Completion Criteria

Run all available relevant builds, tests, static checks, and validation procedures.

Verify supported ESP32-S3 configurations and, where possible, both SD-present and SD-absent operation.

Test failure scenarios such as invalid GPIO assignments, unavailable optional peripherals, insufficient storage, corrupted database files, and interrupted updates where test infrastructure permits.

Distinguish clearly between:
- Successfully compiled.
- Software-tested.
- Verified on physical hardware.

Never claim physical hardware testing that was not performed.

## Final Deliverable

Complete the necessary repository changes rather than returning only an audit or another prompt.

At completion, provide a concise report covering:
1. Major defects found and fixed.
2. Files and architectural areas changed.
3. Build and test results.
4. Supported ESP32-S3 configurations.
5. Remaining limitations and hardware tests still required.
6. Confirmation that user-generated data and existing core functionality were preserved to the extent verified.

The primary objective is a stable, genuinely usable CarTouch core on supported ESP32-S3 boards, with optional hardware and storage capabilities, while keeping future commercial development possible without making it the immediate priority.
