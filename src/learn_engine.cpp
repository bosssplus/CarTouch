/**
 * learn_engine.cpp - Command-learning engine implementation
 *
 * Reminder: this file must never call _can.sendMessage(). Only
 * _can.receiveMessageNonBlocking() and _can.flushRxQueue(). See
 * learn_engine.h and CarTouch_SPEC.md.
 */

#include "learn_engine.h"
#include "error_log.h"
#include "ct_tx_guard.h"

// ■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■
// □□□□□□□□□□ Constructor
// ■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■

LearnEngine::LearnEngine(CanInterface& canInterface) : _can(canInterface) {
    _state                    = LEARN_IDLE;
    _currentLabel[0]            = '\0';
    _currentDisplayName[0]         = '\0';
    _phaseStartTime                   = 0;
    _phaseDurationMs                     = 0;
    _baselineCount                          = 0;
    _candidateCount                            = 0;
    _forcedListenOnly                             = false;
    _previousListenOnlyMode                          = true;
    _targetProfileId = 255;
    _sessionId = 0;
}

// ○○○○○○○○○○○○○○○○○○○○○○○○○○○○○○
// ○○○○○○○○○○ Begin learning
// ○○○○○○○○○○○○○○○○○○○○○○○○○○○○○○

void LearnEngine::beginLearning(const char* label, const char* displayName, uint8_t targetProfileId) {
    strncpy(_currentLabel, label, sizeof(_currentLabel) - 1);
    _currentLabel[sizeof(_currentLabel) - 1] = '\0';

    strncpy(_currentDisplayName, displayName ? displayName : label, sizeof(_currentDisplayName) - 1);
    _currentDisplayName[sizeof(_currentDisplayName) - 1] = '\0';

    _baselineCount   = 0;
    _candidateCount    = 0;
    _targetProfileId = targetProfileId;
    ++_sessionId;
    if (_sessionId == 0) ++_sessionId;
    for (int i = 0; i < BASELINE_MAX_IDS; i++) {
        _baseline[i].valid = false;
    }

    // Hardware-level enforcement (see CarTouch_SPEC.md): make sure the TWAI
    // driver is actually running in listen-only before any capture
    // window can open, not just relying on this class never calling
    // sendMessage(). Remember whatever mode was active so cancel() can
    // restore it once the learning session ends.
    // If a previous session already forced Listen-Only and was never cancelled,
    // keep the ORIGINAL mode. Overwriting it here made the next cancel() think
    // Listen-Only was the user's own choice and never restore Normal mode.
    AppConfig* cfg = getConfig();
    if (!_forcedListenOnly) {
        _previousListenOnlyMode = cfg->listenOnlyMode;
    }
    _baselineOverflowed = false;

    if (!_can.isListenOnlyActive()) {
        Serial.println("[LEARN] Forcing CAN driver into hardware Listen-Only for this learning session...");
        if (!_can.reconfigureMode(true)) {
            // Fail-safe: the driver is left uninitialized by a failed
            // reconfigure (see CANManager::reconfigureMode), which
            // itself blocks sendMessage() - but we still refuse to
            // proceed into a capture window rather than trust that.
            getErrorLog()->log(LOG_CAT_LEARN, LOG_ERROR, "Failed to force hardware Listen-Only - aborting learning session");
            _state = LEARN_ERROR;
            return;
        }
        cfg->listenOnlyMode = true;
        _forcedListenOnly       = true;
    }

    _state = LEARN_IDLE;

    Serial.printf("[LEARN] Starting learning for label: %s (%s)\n", label, _currentDisplayName);
}

// ○○○○○○○○○○○○○○○○○○○○○○○○○○○○○○
// ○○○○○○○○○○ Baseline capture
// ○○○○○○○○○○○○○○○○○○○○○○○○○○○○○○

void LearnEngine::startBaselineCapture() {
    // Defense in depth (see CarTouch_SPEC.md): beginLearning() already
    // forces hardware Listen-Only, but re-check the driver's actual
    // state here too, in case beginLearning() failed, was skipped, or
    // something else on the device flipped the mode in between.
    if (!_can.isListenOnlyActive()) {
        getErrorLog()->log(LOG_CAT_LEARN, LOG_ERROR, "startBaselineCapture() refused - CAN driver not in hardware Listen-Only");
        _state = LEARN_ERROR;
        return;
    }

    // Ensure the queue is empty before starting, so stale data doesn't
    // mix with this learning session's data.
    _can.flushRxQueue();

    _baselineCount = 0;
    _baselineOverflowed = false;
    for (int i = 0; i < BASELINE_MAX_IDS; i++) {
        _baseline[i].valid = false;
    }

    _phaseStartTime    = millis();
    _phaseDurationMs      = LEARN_BASELINE_MS;
    _state                   = LEARN_BASELINE_CAPTURE;

    Serial.println("[LEARN] Baseline capture started - please don't press any button yet");
}

// ■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■
// □□□□□□□□□□ Baseline lookup / insert
// ■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■

BaselineEntry* LearnEngine::_findOrAddBaseline(uint32_t canId, bool isExtended) {
    for (int i = 0; i < _baselineCount; i++) {
        if (ctSameCanFrameId(_baseline[i].canId, _baseline[i].isExtended,
                             canId, isExtended)) {
            return &_baseline[i];
        }
    }

    if (_baselineCount >= BASELINE_MAX_IDS) {
        // Table capacity reached - this CAN ID is not tracked. It would later
        // look like a brand-new message during the action phase, so remember
        // the overflow and warn (see update()).
        _baselineOverflowed = true;
        return nullptr;
    }

    BaselineEntry* entry = &_baseline[_baselineCount];
    entry->canId       = canId;
    entry->isExtended  = isExtended;
    entry->seenCount      = 0;
    entry->valid             = true;
    _baselineCount++;
    return entry;
}

// Lower value = more likely to be the frame produced by the button press:
// brand-new IDs first, then rarely-seen-in-baseline IDs, then IDs that were
// transmitted only a few times during the action window (a button press sends
// a handful of frames; rolling counters send one per cycle).
static int candidatePriority(const LearnCandidate& c) {
    int b = c.seenCountInBaseline > 999 ? 999 : (int)c.seenCountInBaseline;
    int a = c.seenCountInAction  > 99  ? 99  : (int)c.seenCountInAction;
    return (c.isNewMessage ? 0 : 100000) + b * 100 + a;
}

// ○○○○○○○○○○○○○○○○○○○○○○○○○○○○○○
// ○○○○○○○○○○ Message processing during ACTION_CAPTURE
// ○○○○○○○○○○○○○○○○○○○○○○○○○○○○○○

void LearnEngine::_processActionMessage(const CanMessage& msg) {
    // Find this CAN ID in the baseline
    BaselineEntry* baseEntry = nullptr;
    for (int i = 0; i < _baselineCount; i++) {
        if (ctSameCanFrameId(_baseline[i].canId, _baseline[i].isExtended,
                             msg.id, msg.isExtended)) {
            baseEntry = &_baseline[i];
            break;
        }
    }

    bool isNew     = (baseEntry == nullptr);
    bool isChanged = false;

    if (!isNew) {
        // Compare data bytes against the last value seen in the baseline
        if (baseEntry->length != msg.length) {
            isChanged = true;
        } else {
            for (int i = 0; i < msg.length; i++) {
                if (baseEntry->lastData[i] != msg.data[i]) {
                    isChanged = true;
                    break;
                }
            }
        }
    }

    if (!isNew && !isChanged) {
        // This message is identical to what was already in the
        // baseline - likely unrelated to the pressed button, ignored.
        return;
    }

    // Find or add this message in the candidate list
    LearnCandidate* candidate = nullptr;
    for (int i = 0; i < _candidateCount; i++) {
        if (ctSameCanFrameId(_candidates[i].canId, _candidates[i].isExtended,
                             msg.id, msg.isExtended)) {
            candidate = &_candidates[i];
            break;
        }
    }

    if (!candidate) {
        if (_candidateCount >= CANDIDATE_MAX) {
            // Table full. Periodic frames with rolling counters change on every
            // cycle and used to fill all slots within milliseconds, so the real
            // button frame arriving later was silently dropped. Instead, replace
            // the least likely candidate if this one ranks better.
            LearnCandidate probe;
            probe.isNewMessage = isNew;
            probe.seenCountInBaseline = baseEntry ? baseEntry->seenCount : 0;
            probe.seenCountInAction = 1;
            int worstIdx = 0;
            for (int i = 1; i < _candidateCount; i++) {
                if (candidatePriority(_candidates[i]) > candidatePriority(_candidates[worstIdx])) worstIdx = i;
            }
            if (candidatePriority(probe) >= candidatePriority(_candidates[worstIdx])) return;
            candidate = &_candidates[worstIdx];
            *candidate = LearnCandidate();
            candidate->canId                  = msg.id;
            candidate->isExtended               = msg.isExtended;
            candidate->isNewMessage                = isNew;
            candidate->seenCountInBaseline             = probe.seenCountInBaseline;
            candidate->seenCountInAction                   = 0;
        } else {
        candidate = &_candidates[_candidateCount];
        _candidateCount++;
        candidate->canId                = msg.id;
        candidate->isExtended               = msg.isExtended;
        candidate->isNewMessage                = isNew;
        candidate->seenCountInBaseline             = baseEntry ? baseEntry->seenCount : 0;
        candidate->seenCountInAction                   = 0;
        }
    }

    // Always keep the latest data value (closest to the moment the button was pressed)
    candidate->length = msg.length;
    memcpy(candidate->data, msg.data, msg.length);
    candidate->seenCountInAction++;
}

// ○○○○○○○○○○○○○○○○○○○○○○○○○○○○○○
// ○○○○○○○○○○ update() - must be called from the main loop
// ○○○○○○○○○○○○○○○○○○○○○○○○○○○○○○

void LearnEngine::update() {
    if (_state == LEARN_BASELINE_CAPTURE) {
        CanMessage msg;
        // Drain everything currently available this tick (non-blocking)
        while (_can.receiveMessageNonBlocking(msg)) {
            if (msg.isRemote) continue;    // RTR frames carry no data and cannot be learned as commands
            BaselineEntry* entry = _findOrAddBaseline(msg.id, msg.isExtended);
            if (entry) {
                entry->length = msg.length;
                memcpy(entry->lastData, msg.data, msg.length);
                entry->seenCount++;
            }
        }

        if (millis() - _phaseStartTime >= _phaseDurationMs) {
            _state = LEARN_WAITING_ACTION;
            if (_baselineOverflowed) {
                getErrorLog()->log(LOG_CAT_LEARN, LOG_WARN,
                    "Baseline table full (%d IDs) - unrelated frames may appear as candidates", BASELINE_MAX_IDS);
            }
            Serial.printf("[LEARN] Baseline capture complete - %d distinct message(s) seen. Now press the physical button\n",
                          _baselineCount);
        }
        return;
    }

    if (_state == LEARN_ACTION_CAPTURE) {
        CanMessage msg;
        while (_can.receiveMessageNonBlocking(msg)) {
            if (msg.isRemote) continue;
            _processActionMessage(msg);
        }

        if (millis() - _phaseStartTime >= _phaseDurationMs) {
            _rankCandidates();
            _state = LEARN_CANDIDATES_READY;
            Serial.printf("[LEARN] Action capture complete - %d candidate(s) found\n", _candidateCount);
        }
        return;
    }

    // Nothing to do in the other states (IDLE, WAITING_ACTION,
    // CANDIDATES_READY, ERROR) - waiting for the next user action.
}

// ■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■
// □□□□□□□□□□ Confirm ready for action
// ■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■

void LearnEngine::confirmReadyForAction() {
    if (_state != LEARN_WAITING_ACTION) {
        Serial.println("[LEARN] confirmReadyForAction called in an invalid state");
        return;
    }

    // Defense in depth (see CarTouch_SPEC.md) - same re-check as
    // startBaselineCapture(). This is the other capture window the
    // SPEC calls out as needing the hardware guarantee.
    if (!_can.isListenOnlyActive()) {
        getErrorLog()->log(LOG_CAT_LEARN, LOG_ERROR, "confirmReadyForAction() refused - CAN driver not in hardware Listen-Only");
        _state = LEARN_ERROR;
        return;
    }

    // Flush the queue again so only messages *after* this moment
    // (truly concurrent with the button press) are captured.
    _can.flushRxQueue();

    _candidateCount   = 0;
    _phaseStartTime      = millis();
    _phaseDurationMs        = LEARN_ACTION_CAPTURE_MS;
    _state                     = LEARN_ACTION_CAPTURE;

    Serial.println("[LEARN] Capturing action - press the vehicle's physical button now");
}

// ○○○○○○○○○○○○○○○○○○○○○○○○○○○○○○
// ○○○○○○○○○○ Candidate ranking
// ○○○○○○○○○○○○○○○○○○○○○○○○○○○○○○

void LearnEngine::_rankCandidates() {
    // Simple insertion sort - element count is tiny (at most
    // CANDIDATE_MAX=10), so efficiency doesn't matter.
    // Priority:
    //   1. Brand-new messages (isNewMessage=true) rank before changed messages
    //   2. Among changed messages, a lower seenCountInBaseline (i.e. not
    //      a frequently-repeating periodic message) ranks higher - more
    //      likely to be a direct result of the button press rather than
    //      noise from a periodic signal like RPM
    for (int i = 1; i < _candidateCount; i++) {
        LearnCandidate key = _candidates[i];
        int            j   = i - 1;

        int keyPriority = candidatePriority(key);
        while (j >= 0 && candidatePriority(_candidates[j]) > keyPriority) {
            _candidates[j + 1] = _candidates[j];
            j--;
        }
        _candidates[j + 1] = key;
    }
}

// ○○○○○○○○○○○○○○○○○○○○○○○○○○○○○○
// ○○○○○○○○○○ Cancel
// ○○○○○○○○○○○○○○○○○○○○○○○○○○○○○○

void LearnEngine::cancel() {
    _state             = LEARN_IDLE;
    _baselineCount        = 0;
    _candidateCount          = 0;
    _targetProfileId = 255;

    // Restore whatever CAN driver mode was active before beginLearning()
    // forced hardware Listen-Only (see CarTouch_SPEC.md). Called on every
    // exit path (save success, save failure, explicit cancel), not
    // just here.
    if (_forcedListenOnly) {
        if (!_previousListenOnlyMode) {
            if (_can.reconfigureMode(false)) {
                getConfig()->listenOnlyMode = false;
                Serial.println("[LEARN] Restored CAN driver to Normal mode after learning session");
            } else {
                // Fail-safe: if the switch back fails, deliberately stay
                // in Listen-Only rather than risk an unknown driver
                // state. cfg->listenOnlyMode is left as true (already
                // set by beginLearning()) so the UI reflects reality.
                getErrorLog()->log(LOG_CAT_LEARN, LOG_WARN, "Failed to restore Normal mode after learning - staying in Listen-Only (fail-safe)");
            }
        }
        _forcedListenOnly = false;
    }

    Serial.println("[LEARN] Learning process cancelled");
}

// ■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■
// □□□□□□□□□□ Accessors
// ■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■

LearnModeState LearnEngine::getState() {
    return _state;
}

uint8_t LearnEngine::getCandidateCount() {
    return _candidateCount;
}

bool LearnEngine::getCandidate(uint8_t index, LearnCandidate& outCandidate) {
    if (index >= _candidateCount) return false;
    outCandidate = _candidates[index];
    return true;
}

const char* LearnEngine::getCurrentLabel() {
    return _currentLabel;
}

const char* LearnEngine::getCurrentDisplayName() {
    return _currentDisplayName;
}

uint8_t LearnEngine::getProgressPercent() {
    if (_state != LEARN_BASELINE_CAPTURE && _state != LEARN_ACTION_CAPTURE) {
        return 0;
    }

    uint32_t elapsed = millis() - _phaseStartTime;
    if (elapsed >= _phaseDurationMs) return 100;

    return (uint8_t)((elapsed * 100) / _phaseDurationMs);
}
