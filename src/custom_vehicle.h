/**
 * custom_vehicle.h - Data structures for custom vehicle profiles
 *
 * Part of CarTouch v2.0 (see CarTouch_SPEC.md).
 *
 * Defines two new command sources, alongside the built-in DBC files
 * (vehicle_db.h):
 *   1. Learned - a command captured from live CAN Bus traffic while the
 *                vehicle's physical button was pressed (see learn_engine.h)
 *   2. Manual  - a command the user entered directly (CAN ID + bytes)
 *
 * Both are stored in the same LearnedCommand struct, since from the
 * device's point of view they're identical: a fixed CAN ID + payload
 * that must not be executable until the user has explicitly verified it
 * (status == VERIFIED).
 *
 * Safety note: the mere presence of a LearnedCommand in this struct
 * does NOT mean it's cleared to send. VehicleControl::executeCommand()
 * only allows an actual send when status == CMD_VERIFIED.
 */

#ifndef CUSTOM_VEHICLE_H
#define CUSTOM_VEHICLE_H

#include <Arduino.h>
#include "config.h"

// ============================================================================
// Sizing
// ============================================================================
// Per SPEC section 4.3 - deliberately conservative to avoid repeating
// the earlier MAX_DBC_SIGNALS=200 bug (which cost ~1.2 MB of RAM).
// Each LearnedCommand is ~50 bytes -> 32 commands * 8 vehicles ~= 12.8 KB total.

#define MAX_CUSTOM_VEHICLES               8
#define MAX_LEARNED_COMMANDS_PER_VEHICLE  32

// ============================================================================
// Command verification status
// ============================================================================

enum CommandStatus : uint8_t {
    CMD_UNVERIFIED = 0,  // Saved but not yet user-confirmed - not executable
    CMD_VERIFIED   = 1   // User explicitly confirmed this works on the vehicle
};

// ============================================================================
// Command source
// ============================================================================

enum CommandSource : uint8_t {
    SOURCE_DBC     = 0,  // From a DBC file (vehicle_db) - a write signal (rare in practice)
    SOURCE_LEARNED = 1,  // Captured via Learn Mode from a real physical button press
    SOURCE_MANUAL  = 2   // Entered manually by the user
};

// Standard suggested command labels (SPEC section 4.1). These are just
// suggested strings - the user can also enter a free-form custom label,
// so they're defined as plain string constants (not a strict enum) here
// and reused by both the UI (TFT/web) and executeCommand() so all three
// stay in sync.
#define CMD_LABEL_LOCK_ALL         "lock_all"
#define CMD_LABEL_UNLOCK_ALL       "unlock_all"
#define CMD_LABEL_UNLOCK_DRIVER    "unlock_driver"
#define CMD_LABEL_WINDOW_FL_UP     "window_fl_up"
#define CMD_LABEL_WINDOW_FL_DOWN   "window_fl_down"
#define CMD_LABEL_WINDOW_FR_UP     "window_fr_up"
#define CMD_LABEL_WINDOW_FR_DOWN   "window_fr_down"
#define CMD_LABEL_WINDOW_RL_UP     "window_rl_up"
#define CMD_LABEL_WINDOW_RL_DOWN   "window_rl_down"
#define CMD_LABEL_WINDOW_RR_UP     "window_rr_up"
#define CMD_LABEL_WINDOW_RR_DOWN   "window_rr_down"
#define CMD_LABEL_ALL_WINDOWS_UP   "all_windows_up"
#define CMD_LABEL_ALL_WINDOWS_DOWN "all_windows_down"
#define CMD_LABEL_SUNROOF_OPEN     "sunroof_open"
#define CMD_LABEL_SUNROOF_CLOSE    "sunroof_close"
#define CMD_LABEL_SUNROOF_TILT     "sunroof_tilt"
#define CMD_LABEL_TRUNK_OPEN       "trunk_open"
#define CMD_LABEL_TRUNK_LOCK       "trunk_lock"
#define CMD_LABEL_MIRROR_FOLD      "mirror_fold"
#define CMD_LABEL_MIRROR_UNFOLD    "mirror_unfold"
#define CMD_LABEL_ALARM_ARM        "alarm_arm"
#define CMD_LABEL_ALARM_DISARM     "alarm_disarm"
// For a custom label, the user enters a free-form string that is stored
// directly as the label (no special prefix required).

// ============================================================================
// A single learned/manual command
// ============================================================================

struct LearnedCommand {
    char label[32]        = {0};   // Internal identifier (e.g. "lock_all" or a custom name)
    char displayName[48]   = {0};   // Persian display name shown to the driver (e.g. "قفل همه درب‌ها")

    uint32_t canId       = 0;
    bool      isExtended    = false;
    uint8_t    length          = 0;
    uint8_t     data[8]           = {0};

    CommandSource source = SOURCE_MANUAL;
    CommandStatus status    = CMD_UNVERIFIED;

    uint8_t  timesObserved = 0;   // Times seen during capture (Learned only)
    uint8_t  failCount        = 0;   // Failed verification attempts
    uint32_t createdAt           = 0;   // Creation time (millis) - for display/debug only; meaningless after reboot, session-local reference
};

// ============================================================================
// A custom vehicle profile
// ============================================================================

struct CustomVehicleProfile {
    uint8_t  id                  = 0;    // Index in the profile store (0..MAX_CUSTOM_VEHICLES-1)
    char      name[32]               = {0};  // User-chosen name, e.g. "Dad's Pride"
    char       brand[24]                 = {0};
    char        model[24]                    = {0};
    uint16_t     year                            = 0;

    LearnedCommand commands[MAX_LEARNED_COMMANDS_PER_VEHICLE];
    uint8_t          commandCount = 0;

    bool inUse = false;   // false = this slot is empty
};

#endif // CUSTOM_VEHICLE_H
