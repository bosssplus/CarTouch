# Commercial Readiness Checklist - Progress

Tracks the 20-item commercial-readiness checklist: what's done, what's
in progress, and what's out of scope for code changes entirely
(hardware, legal certification, field testing).

> **Compile status (updated):** This project has been pushed to GitHub
> and `.github/workflows/CarTouch-build.yml` has run successfully -
> i.e. `pio run` and `pio run -t buildfs` compile cleanly with the real
> PlatformIO/ESP-IDF toolchain. **This confirms compilation only.**
> Actual hardware behavior (touch calibration, Learn Mode against a
> real vehicle, `reconfigureMode()` against a real TWAI driver, etc.)
> has still not been verified on any real ESP32-S3 - only the user,
> by flashing an actual device, can confirm that part.

---

## Done

| # | Item | Files | Summary |
|---|------|-------|---------|
| 2  | Task watchdog | `main.cpp` | `esp_task_wdt`, 8s timeout; `esp_task_wdt_reset()` at the top of `loop()`. Version-gated for both Arduino-ESP32 2.x and 3.x |
| 3  | Hardware-enforced Listen-Only (global toggle + Learn Mode) | `can_manager.h/.cpp`, `main.cpp`, `learn_engine.h/.cpp` | `reconfigureMode()` performs a real TWAI driver uninstall/reinstall to switch mode at runtime; wired into the `listen_only` command handler. **v2.2:** also wired into Learn Mode entry — `LearnEngine::beginLearning()` now forces real `TWAI_MODE_LISTEN_ONLY` before any capture window opens (remembering the prior mode), `startBaselineCapture()`/`confirmReadyForAction()` each re-check `isListenOnlyActive()` before opening their window and refuse into `LEARN_ERROR` otherwise, and `cancel()` (called on every learn-session exit path) restores the prior mode, staying in Listen-Only (fail-safe) if the restore itself fails. See `CarTouch_SPEC.md` §3.3 and `CHANGELOG.md` v2.2 |
| 4  | Non-blocking OBD-II | `obd2_reader.h/.cpp`, `main.cpp` | Rewritten as a state machine; zero `delay()` on the main path |
| 6  | Motorola/Intel endianness | `vehicle_db.h/.cpp` | Real bit-mapping for both signal byte orders; verified with an independent simulation |
| 7  | DBC vehicle list | `vehicle_db.h/.cpp` | 4 -> 38 vehicles wired (of 57 bundled files); omissions documented with reasons |
| 8  | DBC message cap (was 50) | `vehicle_db.h`, `main.cpp` | Raised to 150; `vehicleDB` (~460 KB at this size) now heap-allocated to be served from PSRAM |
| 9  | Touch calibration | `tft_ui.h/.cpp`, `config.h/.cpp` | Real 5-point `calibrateTouch()` wizard on first boot, persisted to NVS, re-run button in Settings |
| 19 | Mechanical duty-cycle | `vehicle_control.h/.cpp` | Per-actuator-class cumulative activation limit + cooldown, on top of the existing flat rate limit |
| 20 | TFT/web session sync | `config.h/.cpp`, `webserver.h/.cpp` | Global callback: a password change from either interface invalidates all web sessions (HTTP + WebSocket) |
| 16 | Error logging / telemetry | `error_log.h/.cpp` (new), wired into `can_manager.cpp`, `wifi_manager.cpp`, `webserver.cpp`, `learn_engine.cpp`, `main.cpp` | RAM ring buffer of the last 40 events (`GET /api/logs`, WebSocket `get_logs`) plus NVS-persisted cumulative counters, saved at most every 5 minutes to limit flash wear. See `CarTouch_SPEC.md` §13 and `CHANGELOG.md` v2.3 |

## Notable discoveries during review

- **v2.3 doc/code mismatch found and fixed:** `learn_engine.h`,
  `CarTouch_SPEC.md` §3.3, and the v2.2 `CHANGELOG.md` entry all
  claimed `LearnEngine::cancel()` runs on "save success, save
  failure, or explicit cancel." The actual code (`webserver.cpp`,
  `tft_ui.cpp`) never called it on save failure - by design, so the
  user can retry without recapturing. Not a safety issue (staying in
  forced Listen-Only longer is always the safe direction), but the
  docs were wrong about what the code does. Comments/docs corrected
  to match the code; the code itself was not changed. This is exactly
  the kind of drift `HANDOFF_NEXT_AI.md`'s "golden rule" warns about -
  worth a second look at other cross-references next session too.

- **Item 11 (OTA)** was already fully implemented (`/update` endpoint,
  firmware.bin/spiffs.bin upload) - older docs claiming "no OTA" were
  simply wrong/stale, and have been corrected.

- **DRAM overflow linker bug (fixed during development):** an earlier
  attempt at item 8 used `EXT_RAM_BSS_ATTR` on a global object. This
  attribute only actually places data in PSRAM when
  `CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY` is enabled in sdkconfig,
  which the prebuilt Arduino-ESP32 core PlatformIO downloads does not
  enable - so the attribute was silently ignored and the full ~460 KB
  stayed in internal DRAM, overflowing it at link time. **Fix:**
  `vehicleDB` and its dependents (`activeProfileManager`,
  `vehicleControl`) were converted to heap-allocated pointers created
  with `new` in `setup()`; with PSRAM enabled, the Arduino-ESP32 core
  automatically serves allocations over 4 KB from PSRAM with no special
  attribute required.

- **TFT_UI compile bug (fixed during development):** the static
  `pThisUI` pointer that every touch callback (buttons, password
  screen, Learn Wizard) depends on had been dropped during the v1.0 ->
  v2.0 conversion. Restored.

## Not yet done

| # | Item | Status |
|---|------|--------|
| 1  | HTTPS/TLS | **Investigated, deliberately deferred** - see note below |
| 5  | Rolling code / newer-vehicle security | **Out of scope for code** - requires per-vehicle ECU reverse engineering |
| 10 | Documenting the DBC write-command limitation | Docs only, no code needed (covered in README/SPEC) |
| 12 | Secure Boot / Flash Encryption | Not started |
| 13 | Secure device provisioning/pairing | Not started |
| 14 | Legacy protocol auto-detect (ISO9141/KWP2000) | Not started |
| 15 | EMC / regulatory certification | **Out of scope for code** - requires a certification lab |
| 17 | Legal liability / disclaimer | **Out of scope for code** - requires legal counsel |
| 18 | Field testing on a real fleet | **Out of scope without hardware and vehicles** |

### Item 1 (HTTPS) - why it's deferred, not just "not started"

Researched three options; none fit a safe, incremental change in this
codebase:

- **AsyncTCP_SSL** (the option the project's own `platformio.ini`
  comment pointed to) has been unmaintained since 2022 and does not
  compile against the mbedTLS version shipped with current ESP32-S3
  cores - a known, open upstream issue.
- **ESPAsyncTCP / AsyncSSLWebServer** are archived and ESP8266-only, not
  ESP32.
- The only actively-working option, **esp32_https_server**, is a
  synchronous (non-async) library with a completely different API.
  Adopting it means rewriting `webserver.cpp` (600+ lines: routes,
  WebSocket, OTA) around a different architecture - too large and risky
  a change to attempt safely in a single limited session.

Realistic paths forward: (a) terminate TLS in front of the device with a
reverse proxy (e.g. on a home router/Raspberry Pi) rather than on the
ESP32 itself; (b) a dedicated future session to rewrite `webserver.cpp`
against `esp32_https_server`. Both are legitimate; neither is a small
patch.

## Documentation status

- `README.md` - rewritten to match current code; the stale "no OTA"
  claim removed.
- `CHANGELOG.md` - v2.1/v2.2/v2.3 sections added; known-issues list
  reconciled each time.
- `CarTouch_SPEC.md` - stale sections patched (7.2, 11.2, 12); §3.3
  corrected in v2.3 (see "Notable discoveries" above); new §13 added
  for the error-log module, old §13 renumbered to §14.
- `PROGRESS_CHECKLIST.md` - this file.
- `HANDOFF_NEXT_AI.md` - superseded by a v2.3 rewrite; see that file's
  own history note at the top.
- Source comments (`src/*.cpp`, `src/*.h`) - **complete**. All 24
  source files (every `.h`/`.cpp` pair in `src/`) have been translated
  to concise English and re-synced with current behavior, including
  files with no logic changes this round (`wifi_manager.*`,
  `custom_vehicle.h`, `custom_vehicle_store.*`, `learn_engine.*`,
  `active_profile_manager.*`) for project-wide language consistency.
  User-facing strings (LVGL button labels, JSON error messages shown in
  the web UI, the embedded OTA HTML page) were deliberately left in
  Persian throughout, since that is the product's actual UI language -
  every such string was individually verified present and unchanged
  after translation (see verification notes below). Only code comments
  and `Serial` log messages (developer-facing only) were translated.

### Verification performed on the translation pass

- Brace/parenthesis balance checked on all 24 files - zero mismatches.
- All 79 LVGL `lv_label_set_text` string literals in `tft_ui.cpp`
  diffed against the pre-translation version - zero missing.
- All JSON `"error"` fields returned by `webserver.cpp`'s API routes
  diffed against the pre-translation version - exact match.
- The embedded OTA update HTML page in `webserver.cpp` diffed
  byte-for-byte against the pre-translation version - identical.
- Project-wide scan for comment lines still containing Persian
  characters - zero remaining.
- `learn_engine.cpp`/`.h` specifically checked for any `sendMessage`
  call (the file's core safety invariant - it must only ever listen,
  never transmit) - confirmed zero actual calls; the only match is the
  reminder comment describing the rule itself.
