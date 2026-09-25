/**
 * learn_engine.h - Command-learning engine, driven from live CAN Bus traffic
 *
 * Part of CarTouch v2.0 (see CarTouch_SPEC.md section 4).
 *
 * !!! STRICT, NON-NEGOTIABLE SAFETY RULE !!!
 * This class must NEVER, under any circumstances, call sendMessage on
 * CANManager. Everything this class does is read/listen
 * (receiveMessageNonBlocking) only. If this file is modified in the
 * future, this rule must be preserved - violating it means sending an
 * unknown/untested command onto a real vehicle's live bus, which can
 * affect safety-critical systems (brakes, airbags, steering).
 *
 * Known limitation (documented honestly, not hidden):
 * Per SPEC section 3.3, the ideal design would have the TWAI driver
 * actually reinstalled with TWAI_MODE_LISTEN_ONLY when entering
 * LEARN_BASELINE_CAPTURE/LEARN_ACTION_CAPTURE (rather than relying
 * solely on this class never calling sendMessage), so a hardware-level
 * guarantee exists even against a hypothetical future software bug.
 * This isn't implemented in the current version because
 * CANManager::begin/end is synchronous and relatively slow (a few
 * hundred milliseconds), and doing it in the middle of a time-sensitive
 * 2-second window (e.g. right after confirmReadyForAction) could itself
 * cause exactly the messages meant to be captured to be missed. The
 * correct fix is a "fast reconfigure without a full uninstall/install"
 * path in CANManager - a specific item for future work (SPEC section
 * 12), not something to guess at and half-implement here. Until then,
 * safety in this area rests entirely on the code-level guarantee (this
 * file never calls sendMessage), which is verifiable by review since
 * the file is kept short and focused.
 *
 * Algorithm (summary; full detail in SPEC section 4.1):
 *   1. IDLE -> starts when the user picks a command label
 *   2. BASELINE_CAPTURE: a few seconds of background traffic (before
 *      the button is pressed) recorded into a <canId, lastData, count> table
 *   3. WAITING_ACTION: the user is told to press the physical button
 *   4. ACTION_CAPTURE: a few seconds of new traffic captured and
 *      diffed against the baseline
 *   5. CANDIDATES_READY: results (up to CANDIDATE_MAX) are ready for
 *      the user to review and pick manually
 *   6. The user picks one and it's saved (outside this class, in
 *      CustomVehicleStore - this class only produces candidates)
 */

#ifndef LEARN_ENGINE_H
#define LEARN_ENGINE_H

#include <Arduino.h>
#include "config.h"
#include "can_manager.h"

// ============================================================================
// State machine states
// ============================================================================

enum LearnModeState : uint8_t {
    LEARN_IDLE              = 0,
    LEARN_BASELINE_CAPTURE  = 1,
    LEARN_WAITING_ACTION    = 2,
    LEARN_ACTION_CAPTURE    = 3,
    LEARN_CANDIDATES_READY  = 4,
    LEARN_ERROR             = 5
};

// ============================================================================
// A baseline table entry
// ============================================================================

struct BaselineEntry {
    uint32_t canId       = 0;
    uint8_t   lastData[8]   = {0};
    uint8_t    length          = 0;
    uint16_t    seenCount        = 0;    // Times seen during the baseline window
    bool         valid              = false;
};

// ============================================================================
// A result candidate
// ============================================================================

struct LearnCandidate {
    uint32_t canId               = 0;
    uint8_t   data[8]               = {0};
    uint8_t    length                  = 0;
    bool        isExtended                = false;
    bool         isNewMessage                = false;  // true if this CAN ID wasn't in the baseline at all
    uint16_t      seenCountInAction             = 0;   // Times seen during the action window
    uint16_t       seenCountInBaseline             = 0; // For ranking: the more it appeared in baseline, the more likely it's noise/periodic
};

class LearnEngine {
public:
    LearnEngine(CANManager& canManager);

    /**
     * Starts the learning process for a given label. Only resets
     * internal state; actual capture begins with startBaselineCapture().
     */
    void beginLearning(const char* label, const char* displayName);

    /**
     * Starts baseline capture. Assumes the user has not yet pressed the
     * vehicle's physical button. Non-blocking - update() must be called
     * repeatedly from the main loop for the timing to advance.
     */
    void startBaselineCapture();

    /**
     * Must be called every main loop() iteration (like the project's
     * other update() methods, e.g. tftUI.update()). This method:
     *   - In BASELINE_CAPTURE / ACTION_CAPTURE: reads new CAN messages
     *     from CANManager (non-blocking) and updates the table
     *   - Automatically advances to the next state once the window elapses
     */
    void update();

    /**
     * User has confirmed they're ready to press the physical button.
     * Starts the ACTION_CAPTURE window (LEARN_ACTION_CAPTURE_MS,
     * capped at LEARN_ACTION_CAPTURE_MAX_MS).
     */
    void confirmReadyForAction();

    /** Cancels the current learning process entirely, back to IDLE (no save). */
    void cancel();

    LearnModeState getState();

    /** Number of ready candidates (only valid when state == LEARN_CANDIDATES_READY). */
    uint8_t getCandidateCount();

    /**
     * Retrieves a candidate by index (ranked: brand-new messages
     * first, then by lowest seenCountInBaseline - i.e. least likely to
     * be periodic/noise traffic).
     */
    bool getCandidate(uint8_t index, LearnCandidate& outCandidate);

    /** The label/display name currently being learned. */
    const char* getCurrentLabel();
    const char* getCurrentDisplayName();

    /** Current window's progress percentage (0-100) - for the UI progress bar. */
    uint8_t getProgressPercent();

private:
    CANManager& _can;

    LearnModeState _state;
    char             _currentLabel[32];
    char              _currentDisplayName[48];

    uint32_t _phaseStartTime;
    uint32_t _phaseDurationMs;

    BaselineEntry _baseline[BASELINE_MAX_IDS];
    uint8_t         _baselineCount;

    LearnCandidate _candidates[CANDIDATE_MAX];
    uint8_t          _candidateCount;

    /** Finds or adds a baseline table entry. */
    BaselineEntry* _findOrAddBaseline(uint32_t canId);

    /** Checks a message received during ACTION_CAPTURE and adds it as a candidate if it differs from the baseline. */
    void _processActionMessage(const CanMessage& msg);

    /** Ranks candidates by priority (new message > lowest baseline noise). */
    void _rankCandidates();
};

#endif // LEARN_ENGINE_H
