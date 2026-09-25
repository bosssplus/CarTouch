/**
 * learn_engine.cpp - Command-learning engine implementation
 *
 * Reminder: this file must never call _can.sendMessage(). Only
 * _can.receiveMessageNonBlocking() and _can.flushRxQueue(). See
 * learn_engine.h and CarTouch_SPEC.md sections 4/8.
 */

#include "learn_engine.h"

// ============================================================================
// Constructor
// ============================================================================

LearnEngine::LearnEngine(CANManager& canManager) : _can(canManager) {
    _state                    = LEARN_IDLE;
    _currentLabel[0]            = '\0';
    _currentDisplayName[0]         = '\0';
    _phaseStartTime                   = 0;
    _phaseDurationMs                     = 0;
    _baselineCount                          = 0;
    _candidateCount                            = 0;
}

// ============================================================================
// Begin learning
// ============================================================================

void LearnEngine::beginLearning(const char* label, const char* displayName) {
    strncpy(_currentLabel, label, sizeof(_currentLabel) - 1);
    _currentLabel[sizeof(_currentLabel) - 1] = '\0';

    strncpy(_currentDisplayName, displayName ? displayName : label, sizeof(_currentDisplayName) - 1);
    _currentDisplayName[sizeof(_currentDisplayName) - 1] = '\0';

    _baselineCount   = 0;
    _candidateCount    = 0;
    for (int i = 0; i < BASELINE_MAX_IDS; i++) {
        _baseline[i].valid = false;
    }

    _state = LEARN_IDLE;

    Serial.printf("[LEARN] Starting learning for label: %s (%s)\n", label, _currentDisplayName);
}

// ============================================================================
// Baseline capture
// ============================================================================

void LearnEngine::startBaselineCapture() {
    // Ensure the queue is empty before starting, so stale data doesn't
    // mix with this learning session's data.
    _can.flushRxQueue();

    _baselineCount = 0;
    for (int i = 0; i < BASELINE_MAX_IDS; i++) {
        _baseline[i].valid = false;
    }

    _phaseStartTime    = millis();
    _phaseDurationMs      = LEARN_BASELINE_MS;
    _state                   = LEARN_BASELINE_CAPTURE;

    Serial.println("[LEARN] Baseline capture started - please don't press any button yet");
}

// ============================================================================
// Baseline lookup / insert
// ============================================================================

BaselineEntry* LearnEngine::_findOrAddBaseline(uint32_t canId) {
    for (int i = 0; i < _baselineCount; i++) {
        if (_baseline[i].canId == canId) {
            return &_baseline[i];
        }
    }

    if (_baselineCount >= BASELINE_MAX_IDS) {
        // Table capacity reached - this new CAN ID is ignored. On a
        // busy bus, BASELINE_MAX_IDS may be too small; in that case the
        // user should try a shorter capture window, or this constant
        // should be raised in config.h.
        return nullptr;
    }

    BaselineEntry* entry = &_baseline[_baselineCount];
    entry->canId       = canId;
    entry->seenCount      = 0;
    entry->valid             = true;
    _baselineCount++;
    return entry;
}

// ============================================================================
// Message processing during ACTION_CAPTURE
// ============================================================================

void LearnEngine::_processActionMessage(const CanMessage& msg) {
    // Find this CAN ID in the baseline
    BaselineEntry* baseEntry = nullptr;
    for (int i = 0; i < _baselineCount; i++) {
        if (_baseline[i].canId == msg.id) {
            baseEntry = &_baseline[i];
            break;
        }
    }

    bool isNew         = (baseEntry == nullptr);
    bool isChanged        = false;

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
        if (_candidates[i].canId == msg.id) {
            candidate = &_candidates[i];
            break;
        }
    }

    if (!candidate) {
        if (_candidateCount >= CANDIDATE_MAX) {
            // Candidate capacity reached. Existing candidates are
            // ranked at the end by _rankCandidates(); this just
            // prevents a buffer overflow.
            return;
        }
        candidate = &_candidates[_candidateCount];
        _candidateCount++;
        candidate->canId                = msg.id;
        candidate->isExtended               = msg.isExtended;
        candidate->isNewMessage                = isNew;
        candidate->seenCountInBaseline             = baseEntry ? baseEntry->seenCount : 0;
        candidate->seenCountInAction                   = 0;
    }

    // Always keep the latest data value (closest to the moment the button was pressed)
    candidate->length = msg.length;
    memcpy(candidate->data, msg.data, msg.length);
    candidate->seenCountInAction++;
}

// ============================================================================
// update() - must be called from the main loop
// ============================================================================

void LearnEngine::update() {
    if (_state == LEARN_BASELINE_CAPTURE) {
        CanMessage msg;
        // Drain everything currently available this tick (non-blocking)
        while (_can.receiveMessageNonBlocking(msg)) {
            BaselineEntry* entry = _findOrAddBaseline(msg.id);
            if (entry) {
                entry->length = msg.length;
                memcpy(entry->lastData, msg.data, msg.length);
                entry->seenCount++;
            }
        }

        if (millis() - _phaseStartTime >= _phaseDurationMs) {
            _state = LEARN_WAITING_ACTION;
            Serial.printf("[LEARN] Baseline capture complete - %d distinct message(s) seen. Now press the physical button\n",
                          _baselineCount);
        }
        return;
    }

    if (_state == LEARN_ACTION_CAPTURE) {
        CanMessage msg;
        while (_can.receiveMessageNonBlocking(msg)) {
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

// ============================================================================
// Confirm ready for action
// ============================================================================

void LearnEngine::confirmReadyForAction() {
    if (_state != LEARN_WAITING_ACTION) {
        Serial.println("[LEARN] confirmReadyForAction called in an invalid state");
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

// ============================================================================
// Candidate ranking
// ============================================================================

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
        int j = i - 1;

        auto priority = [](const LearnCandidate& c) -> int {
            // Lower number = higher priority
            int base = c.isNewMessage ? 0 : 1000;
            return base + c.seenCountInBaseline;
        };

        int keyPriority = priority(key);
        while (j >= 0 && priority(_candidates[j]) > keyPriority) {
            _candidates[j + 1] = _candidates[j];
            j--;
        }
        _candidates[j + 1] = key;
    }
}

// ============================================================================
// Cancel
// ============================================================================

void LearnEngine::cancel() {
    _state             = LEARN_IDLE;
    _baselineCount        = 0;
    _candidateCount          = 0;
    Serial.println("[LEARN] Learning process cancelled");
}

// ============================================================================
// Accessors
// ============================================================================

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
