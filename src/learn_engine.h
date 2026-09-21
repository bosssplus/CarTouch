/**
 * learn_engine.h - موتور یادگیری فرمان از روی CAN Bus واقعی
 * 
 * بخشی از CarTouch v2.0 (به CarTouch_V2_SPEC.md بخش ۴ مراجعه کنید).
 * 
 * ⚠️⚠️⚠️ قانون ایمنی سخت‌گیرانه و غیرقابل‌مذاکره ⚠️⚠️⚠️
 * این کلاس هرگز، تحت هیچ شرایطی، نباید متد sendMessage روی
 * CANManager صدا بزند. تمام کاری که این کلاس می‌کند خواندن/شنود
 * (receiveMessageNonBlocking) است. اگر در آینده کسی این فایل را
 * تغییر می‌دهد، این قانون باید حفظ شود - نقض آن یعنی ارسال فرمان
 * ناشناخته/آزمایشی به باس زنده‌ی یک خودروی واقعی که می‌تواند به
 * سیستم‌های ایمنی (ترمز، ایربگ، فرمان) آسیب برساند.
 * 
 * ⚠️ محدودیت شناخته‌شده (صادقانه مستند شده، نه پنهان‌شده):
 * طبق بخش ۳.۳ سند، ایده‌آل این بود که هنگام ورود به هر یک از
 * حالت‌های LEARN_BASELINE_CAPTURE/LEARN_ACTION_CAPTURE، درایور TWAI
 * واقعاً با پرچم TWAI_MODE_LISTEN_ONLY نصب مجدد شود (نه فقط تکیه بر
 * اینکه این کلاس sendMessage صدا نمی‌زند) تا حتی در برابر یک باگ
 * نرم‌افزاری احتمالی در آینده هم تضمین سخت‌افزاری وجود داشته باشد.
 * این کار در نسخه‌ی فعلی پیاده‌سازی نشده چون CANManager::begin/end
 * synchronous و نسبتاً کند است (چند صد میلی‌ثانیه) و انجام آن در
 * وسط یک بازه‌ی ۲ ثانیه‌ای زمان‌حساس (مثلاً بلافاصله بعد از
 * confirmReadyForAction) می‌تواند خودش باعث از دست رفتن دقیقاً همان
 * پیام‌هایی شود که قرار است ضبط شوند. راه‌حل درست، افزودن یک مسیر
 * "reconfigure سریع بدون uninstall/install کامل" به CANManager است -
 * این یک آیتم مشخص برای توسعه‌ی بعدی است (بخش ۱۲ سند)، نه چیزی که
 * در این نسخه حدس زده و ناقص پیاده‌سازی شود. تا آن زمان، ایمنی این
 * بخش صرفاً بر تضمین سطح کد (این فایل هرگز sendMessage ندارد) متکی
 * است، که خودش با بازبینی این فایل (که کوتاه و متمرکز نگه داشته شده)
 * قابل تأیید است.
 * 
 * الگوریتم (خلاصه، جزئیات کامل در بخش ۴.۱ سند):
 *   1. IDLE -> شروع با انتخاب برچسب فرمان توسط کاربر
 *   2. BASELINE_CAPTURE: چند ثانیه پیام‌های زمینه (قبل از فشردن دکمه)
 *      در یک جدول <canId, lastData, count> ذخیره می‌شود
 *   3. WAITING_ACTION: به کاربر گفته می‌شود دکمه فیزیکی را بزند
 *   4. ACTION_CAPTURE: چند ثانیه پیام‌های جدید گرفته و با baseline
 *      مقایسه (diff) می‌شوند
 *   5. CANDIDATES_READY: نتایج (حداکثر CANDIDATE_MAX مورد) آماده‌ی
 *      نمایش و انتخاب دستی توسط کاربرند
 *   6. کاربر یکی را انتخاب و ذخیره می‌کند (خارج از این کلاس، در
 *      CustomVehicleStore - این کلاس فقط کاندید تولید می‌کند)
 */

#ifndef LEARN_ENGINE_H
#define LEARN_ENGINE_H

#include <Arduino.h>
#include "config.h"
#include "can_manager.h"

// ======================== حالت‌های state machine ========================

enum LearnModeState : uint8_t {
    LEARN_IDLE = 0,
    LEARN_BASELINE_CAPTURE = 1,
    LEARN_WAITING_ACTION = 2,
    LEARN_ACTION_CAPTURE = 3,
    LEARN_CANDIDATES_READY = 4,
    LEARN_ERROR = 5
};

// ======================== یک ورودی در جدول baseline ========================

struct BaselineEntry {
    uint32_t canId = 0;
    uint8_t lastData[8] = {0};
    uint8_t length = 0;
    uint16_t seenCount = 0;   // چند بار در بازه‌ی baseline دیده شد
    bool valid = false;
};

// ======================== یک کاندید نتیجه ========================

struct LearnCandidate {
    uint32_t canId = 0;
    uint8_t data[8] = {0};
    uint8_t length = 0;
    bool isExtended = false;
    bool isNewMessage = false;   // true اگر این CAN ID اصلاً در baseline نبود
    uint16_t seenCountInAction = 0; // چند بار در بازه‌ی action دیده شد
    uint16_t seenCountInBaseline = 0; // برای اولویت‌بندی: هرچه بیشتر در baseline دیده شده بود، احتمال نویز/پیام دوره‌ای بیشتر است
};

class LearnEngine {
public:
    LearnEngine(CANManager& canManager);
    
    /**
     * شروع فرآیند یادگیری برای یک برچسب مشخص. این متد فقط وضعیت
     * داخلی را ریست می‌کند؛ ضبط واقعی با startBaselineCapture شروع
     * می‌شود.
     */
    void beginLearning(const char* label, const char* displayName);
    
    /**
     * شروع ضبط پیش‌زمینه (baseline). فرض بر این است که کاربر هنوز
     * دکمه فیزیکی خودرو را نزده. غیرمسدودکننده است - باید در حلقه‌ی
     * اصلی update() به‌طور مکرر صدا زده شود تا زمان‌بندی پیش برود.
     */
    void startBaselineCapture();
    
    /**
     * باید در هر تکرار از loop() اصلی صدا زده شود (مثل سایر update
     * های موجود در پروژه مانند tftUI.update()). این متد:
     *   - در حالت BASELINE_CAPTURE / ACTION_CAPTURE: پیام‌های جدید
     *     CAN را از CANManager می‌خواند (غیرمسدودکننده) و جدول را
     *     به‌روزرسانی می‌کند
     *   - وقتی زمان بازه تمام شود، خودکار به حالت بعدی می‌رود
     */
    void update();
    
    /**
     * کاربر تأیید کرده که آماده است دکمه فیزیکی را بزند. باعث
     * می‌شود بازه‌ی ACTION_CAPTURE (به مدت LEARN_ACTION_CAPTURE_MS،
     * حداکثر LEARN_ACTION_CAPTURE_MAX_MS) شروع شود.
     */
    void confirmReadyForAction();
    
    /**
     * لغو کامل فرآیند یادگیری فعلی و بازگشت به IDLE (بدون ذخیره).
     */
    void cancel();
    
    /**
     * وضعیت فعلی state machine
     */
    LearnModeState getState();
    
    /**
     * تعداد کاندیدهای آماده (فقط معتبر وقتی state == LEARN_CANDIDATES_READY)
     */
    uint8_t getCandidateCount();
    
    /**
     * دریافت یک کاندید با ایندکس (مرتب‌شده بر اساس اولویت: پیام
     * جدید کامل اول، سپس بر اساس کمترین seenCountInBaseline - یعنی
     * کمتر شبیه پیام دوره‌ای/نویز)
     */
    bool getCandidate(uint8_t index, LearnCandidate& outCandidate);
    
    /**
     * برچسب و نام نمایشی فعلی که در حال یادگیری آن هستیم
     */
    const char* getCurrentLabel();
    const char* getCurrentDisplayName();
    
    /**
     * درصد پیشرفت بازه‌ی فعلی (۰-۱۰۰) - برای نوار پیشرفت در UI
     */
    uint8_t getProgressPercent();

private:
    CANManager& _can;
    
    LearnModeState _state;
    char _currentLabel[32];
    char _currentDisplayName[48];
    
    uint32_t _phaseStartTime;
    uint32_t _phaseDurationMs;
    
    BaselineEntry _baseline[BASELINE_MAX_IDS];
    uint8_t _baselineCount;
    
    LearnCandidate _candidates[CANDIDATE_MAX];
    uint8_t _candidateCount;
    
    // پیدا کردن یا افزودن یک ورودی در جدول baseline
    BaselineEntry* _findOrAddBaseline(uint32_t canId);
    
    // بررسی یک پیام دریافتی حین ACTION_CAPTURE و افزودن به کاندیدها
    // در صورت متفاوت بودن از baseline
    void _processActionMessage(const CanMessage& msg);
    
    // مرتب‌سازی کاندیدها بر اساس اولویت (پیام جدید > کمترین نویز baseline)
    void _rankCandidates();
};

#endif // LEARN_ENGINE_H
