/**
 * learn_engine.cpp - پیاده‌سازی موتور یادگیری فرمان
 * 
 * ⚠️ یادآوری: این فایل هرگز نباید _can.sendMessage() صدا بزند.
 * فقط _can.receiveMessageNonBlocking() و _can.flushRxQueue().
 * به هدر learn_engine.h و بخش ۴/۸ سند CarTouch_V2_SPEC.md مراجعه کنید.
 */

#include "learn_engine.h"

// ======================== سازنده ========================

LearnEngine::LearnEngine(CANManager& canManager) : _can(canManager) {
    _state = LEARN_IDLE;
    _currentLabel[0] = '\0';
    _currentDisplayName[0] = '\0';
    _phaseStartTime = 0;
    _phaseDurationMs = 0;
    _baselineCount = 0;
    _candidateCount = 0;
}

// ======================== شروع یادگیری ========================

void LearnEngine::beginLearning(const char* label, const char* displayName) {
    strncpy(_currentLabel, label, sizeof(_currentLabel) - 1);
    _currentLabel[sizeof(_currentLabel) - 1] = '\0';
    
    strncpy(_currentDisplayName, displayName ? displayName : label, sizeof(_currentDisplayName) - 1);
    _currentDisplayName[sizeof(_currentDisplayName) - 1] = '\0';
    
    _baselineCount = 0;
    _candidateCount = 0;
    for (int i = 0; i < BASELINE_MAX_IDS; i++) {
        _baseline[i].valid = false;
    }
    
    _state = LEARN_IDLE;
    
    Serial.printf("[LEARN] شروع یادگیری برای برچسب: %s (%s)\n", label, _currentDisplayName);
}

// ======================== شروع ضبط baseline ========================

void LearnEngine::startBaselineCapture() {
    // ⚠️ اطمینان از خالی بودن صف قبل از شروع تا داده‌ی قدیمی با
    // داده‌ی این جلسه‌ی یادگیری قاطی نشود
    _can.flushRxQueue();
    
    _baselineCount = 0;
    for (int i = 0; i < BASELINE_MAX_IDS; i++) {
        _baseline[i].valid = false;
    }
    
    _phaseStartTime = millis();
    _phaseDurationMs = LEARN_BASELINE_MS;
    _state = LEARN_BASELINE_CAPTURE;
    
    Serial.println("[LEARN] ضبط پیش‌زمینه (baseline) شروع شد - لطفاً هنوز دکمه‌ای نزنید");
}

// ======================== پیدا کردن/افزودن baseline ========================

BaselineEntry* LearnEngine::_findOrAddBaseline(uint32_t canId) {
    for (int i = 0; i < _baselineCount; i++) {
        if (_baseline[i].canId == canId) {
            return &_baseline[i];
        }
    }
    
    if (_baselineCount >= BASELINE_MAX_IDS) {
        // ظرفیت جدول پر شده - این CAN ID جدید نادیده گرفته می‌شود.
        // برای یک باس شلوغ ممکن است BASELINE_MAX_IDS کم باشد؛ در
        // این صورت کاربر باید بازه‌ی زمانی کوتاه‌تری امتحان کند یا
        // این ثابت در config.h افزایش یابد.
        return nullptr;
    }
    
    BaselineEntry* entry = &_baseline[_baselineCount];
    entry->canId = canId;
    entry->seenCount = 0;
    entry->valid = true;
    _baselineCount++;
    return entry;
}

// ======================== پردازش پیام حین ACTION_CAPTURE ========================

void LearnEngine::_processActionMessage(const CanMessage& msg) {
    // پیدا کردن این CAN ID در baseline
    BaselineEntry* baseEntry = nullptr;
    for (int i = 0; i < _baselineCount; i++) {
        if (_baseline[i].canId == msg.id) {
            baseEntry = &_baseline[i];
            break;
        }
    }
    
    bool isNew = (baseEntry == nullptr);
    bool isChanged = false;
    
    if (!isNew) {
        // مقایسه بایت‌های داده با آخرین مقدار دیده‌شده در baseline
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
        // این پیام دقیقاً همان چیزی است که در baseline هم بود -
        // احتمالاً بی‌ربط به دکمه‌ی فشرده‌شده است، نادیده گرفته می‌شود.
        return;
    }
    
    // پیدا کردن یا افزودن این پیام به لیست کاندیدها
    LearnCandidate* candidate = nullptr;
    for (int i = 0; i < _candidateCount; i++) {
        if (_candidates[i].canId == msg.id) {
            candidate = &_candidates[i];
            break;
        }
    }
    
    if (!candidate) {
        if (_candidateCount >= CANDIDATE_MAX) {
            // ظرفیت کاندید پر است. کاندیدهای موجود در پایان با
            // _rankCandidates اولویت‌بندی می‌شوند؛ اینجا فقط از
            // سرریز بافر جلوگیری می‌کنیم.
            return;
        }
        candidate = &_candidates[_candidateCount];
        _candidateCount++;
        candidate->canId = msg.id;
        candidate->isExtended = msg.isExtended;
        candidate->isNewMessage = isNew;
        candidate->seenCountInBaseline = baseEntry ? baseEntry->seenCount : 0;
        candidate->seenCountInAction = 0;
    }
    
    // همیشه آخرین مقدار داده را نگه می‌داریم (نزدیک‌ترین به لحظه‌ی فشردن دکمه)
    candidate->length = msg.length;
    memcpy(candidate->data, msg.data, msg.length);
    candidate->seenCountInAction++;
}

// ======================== به‌روزرسانی (باید در loop صدا زده شود) ========================

void LearnEngine::update() {
    if (_state == LEARN_BASELINE_CAPTURE) {
        CanMessage msg;
        // خالی کردن تمام پیام‌های در دسترس در این تیک (غیرمسدودکننده)
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
            Serial.printf("[LEARN] ضبط پیش‌زمینه تمام شد - %d پیام متمایز دیده شد. حالا دکمه فیزیکی را بزنید\n",
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
            Serial.printf("[LEARN] ضبط اقدام تمام شد - %d کاندید یافت شد\n", _candidateCount);
        }
        return;
    }
    
    // در سایر حالت‌ها (IDLE, WAITING_ACTION, CANDIDATES_READY, ERROR)
    // کاری برای انجام نیست - منتظر اقدام بعدی کاربر هستیم.
}

// ======================== تأیید آماده بودن برای اقدام ========================

void LearnEngine::confirmReadyForAction() {
    if (_state != LEARN_WAITING_ACTION) {
        Serial.println("⚠️ [LEARN] confirmReadyForAction در حالت نامعتبر صدا زده شد");
        return;
    }
    
    // صف را دوباره خالی می‌کنیم تا فقط پیام‌های *بعد از* این لحظه
    // (یعنی واقعاً هم‌زمان با فشردن دکمه) گرفته شوند.
    _can.flushRxQueue();
    
    _candidateCount = 0;
    _phaseStartTime = millis();
    _phaseDurationMs = LEARN_ACTION_CAPTURE_MS;
    _state = LEARN_ACTION_CAPTURE;
    
    Serial.println("[LEARN] در حال ضبط اقدام - همین الان دکمه فیزیکی خودرو را بزنید");
}

// ======================== مرتب‌سازی کاندیدها ========================

void LearnEngine::_rankCandidates() {
    // مرتب‌سازی ساده‌ی درج (insertion sort) - تعداد عناصر خیلی کم
    // است (حداکثر CANDIDATE_MAX=10) پس کارایی اهمیتی ندارد.
    // اولویت: 
    //   1. پیام‌های کاملاً جدید (isNewMessage=true) قبل از پیام‌های تغییریافته
    //   2. در بین پیام‌های تغییریافته، هرچه seenCountInBaseline کمتر
    //      باشد (یعنی پیام دوره‌ای پرتکرار نبوده) اولویت بالاتر -
    //      چون احتمال بیشتری دارد نتیجه‌ی مستقیم فشردن دکمه باشد نه
    //      نویز یک سیگنال دوره‌ای مثل RPM
    for (int i = 1; i < _candidateCount; i++) {
        LearnCandidate key = _candidates[i];
        int j = i - 1;
        
        auto priority = [](const LearnCandidate& c) -> int {
            // عدد کمتر = اولویت بالاتر
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

// ======================== لغو ========================

void LearnEngine::cancel() {
    _state = LEARN_IDLE;
    _baselineCount = 0;
    _candidateCount = 0;
    Serial.println("[LEARN] فرآیند یادگیری لغو شد");
}

// ======================== دریافت وضعیت ========================

LearnModeState LearnEngine::getState() {
    return _state;
}

// ======================== تعداد کاندیدها ========================

uint8_t LearnEngine::getCandidateCount() {
    return _candidateCount;
}

// ======================== دریافت یک کاندید ========================

bool LearnEngine::getCandidate(uint8_t index, LearnCandidate& outCandidate) {
    if (index >= _candidateCount) return false;
    outCandidate = _candidates[index];
    return true;
}

// ======================== getterهای برچسب ========================

const char* LearnEngine::getCurrentLabel() {
    return _currentLabel;
}

const char* LearnEngine::getCurrentDisplayName() {
    return _currentDisplayName;
}

// ======================== درصد پیشرفت ========================

uint8_t LearnEngine::getProgressPercent() {
    if (_state != LEARN_BASELINE_CAPTURE && _state != LEARN_ACTION_CAPTURE) {
        return 0;
    }
    
    uint32_t elapsed = millis() - _phaseStartTime;
    if (elapsed >= _phaseDurationMs) return 100;
    
    return (uint8_t)((elapsed * 100) / _phaseDurationMs);
}
