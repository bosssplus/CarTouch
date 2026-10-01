# CarTouch BLE and BLE OTA

CarTouch starts BLE independently of Wi-Fi and advertises as `CarTouch-XXXX`.

## Characteristics
- **Status** — read/notify status and OTA progress.
- **Command** — status and OTA control commands.
- **Data** — firmware bytes during authenticated OTA.

## OTA sequence
1. Connect to the device.
2. Send `START:<web-password>:<firmware-size>` (the size is always taken after the last `:`, so the password may contain `:`).
3. Wait for `OTA_STARTED`.
4. Write firmware bytes to Data.
5. Send `END`.
6. The device verifies size, finalizes the image and reboots.

`ABORT` cancels a transfer; disconnecting also aborts an active OTA.

## Security
BLE link security is enabled. BLE OTA additionally requires the current Web password at the application layer. Do not expose the password over an untrusted BLE environment.

BLE provides status/control transport and firmware OTA; the authenticated Web UI remains the full configuration surface.

## Hardening notes
- Command/Data characteristics require an encrypted (paired) link.
- OTA is refused while the default web password is still in use (`OTA_CHANGE_DEFAULT_PASSWORD`).
- 5 wrong passwords lock BLE OTA for 60 seconds.
- Pairing uses Just Works (no passkey, no MITM protection). The link is encrypted, but an attacker present during first pairing could impersonate the device or the phone and capture the OTA password. Pair only in a trusted place, and change the Web password if pairing may have been observed.
