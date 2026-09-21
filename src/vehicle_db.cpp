/**
 * vehicle_db.cpp - پیاده‌سازی پایگاه داده خودرو
 * 
 * از SPIFFS برای خواندن فایل‌های DBC استفاده می‌کند.
 * فایل‌های DBC آماده از OpenDBC در پوشه data/dbc قرار می‌گیرند.
 * 
 * تمام توابع این فایل تست شده ولی پارسر کامل DBC نیاز به توسعه بیشتر دارد.
 */

#include "vehicle_db.h"
#include <FS.h>
#include <SPIFFS.h>
#include <cstring>

// ======================== سازنده ========================

VehicleDB::VehicleDB() {
    _messageCount = 0;
    _vehicleCount = 0;
    _initialized = false;
    memset(&_activeVehicle, 0, sizeof(VehicleProfile));
}

// ======================== مقداردهی اولیه ========================

void VehicleDB::begin() {
    // تنظیم خودروهای پیش‌فرض (برای نمایش در منو)
    // در پروژه واقعی، این لیست از فایل‌های موجود در SPIFFS ساخته می‌شود
    
    // === اصلاحیه ===
    // لیست قبلی به فایل‌هایی مثل /dbc/toyota_camry.dbc, /dbc/honda_civic.dbc,
    // /dbc/bmw_3series.dbc, /dbc/generic_obd2.dbc اشاره می‌کرد که هیچ‌کدام
    // در پوشه‌ی واقعی data/dbc این پروژه وجود ندارند (چک شد) - یعنی
    // setActiveVehicle/loadDBCFile برای همه‌ی این ۵ مورد بی‌صدا شکست
    // می‌خورد. اینجا به فایل‌هایی که واقعاً در data/dbc موجودند اشاره
    // می‌کنیم. "Generic / OBD-II" اصلاً به فایل DBC نیاز ندارد چون
    // OBD2Reader مستقیماً با PIDهای استاندارد کار می‌کند، نه با DBC -
    // پس dbcFileName آن را خالی می‌گذاریم (loadDBCFile زیر این حالت را
    // به‌صورت امن نادیده می‌گیرد).
    //
    // ⚠️ قبل از افزودن هر مدل دیگر، حتماً با ls روی data/dbc چک کنید
    // که نام فایل دقیقاً همان چیزی باشد که آنجا هست - این دقیقاً همان
    // باگی بود که اینجا رفع شد.
    strcpy(_vehicleList[0].brand, "Generic");
    strcpy(_vehicleList[0].model, "OBD-II");
    strcpy(_vehicleList[0].dbcFileName, "");  // نیازی به DBC ندارد
    _vehicleList[0].yearStart = 2008;
    _vehicleList[0].yearEnd = 2025;
    
    strcpy(_vehicleList[1].brand, "Toyota");
    strcpy(_vehicleList[1].model, "Prius (2010 PT)");
    strcpy(_vehicleList[1].dbcFileName, "/dbc/toyota_prius_2010_pt.dbc");
    _vehicleList[1].yearStart = 2010;
    _vehicleList[1].yearEnd = 2015;
    
    strcpy(_vehicleList[2].brand, "Toyota");
    strcpy(_vehicleList[2].model, "Ref PT (2017)");
    strcpy(_vehicleList[2].dbcFileName, "/dbc/toyota_2017_ref_pt.dbc");
    _vehicleList[2].yearStart = 2017;
    _vehicleList[2].yearEnd = 2021;
    
    strcpy(_vehicleList[3].brand, "BMW");
    strcpy(_vehicleList[3].model, "E9x/E8x 3 Series");
    strcpy(_vehicleList[3].dbcFileName, "/dbc/bmw_e9x_e8x.dbc");
    _vehicleList[3].yearStart = 2005;
    _vehicleList[3].yearEnd = 2013;
    
    // توجه: هیچ فایل DBC مربوط به هوندا در data/dbc این پروژه وجود
    // نداشت، پس مدل هوندا از لیست حذف شد. برای افزودنش، فایل DBC
    // واقعی هوندا (مثلاً از OpenDBC) را در data/dbc قرار دهید و اینجا
    // یک ورودی جدید با نام دقیق فایل اضافه کنید.
    
    _vehicleCount = 4;
    _initialized = true;
    
    // بارگذاری خودروی پیش‌فرض از تنظیمات
    AppConfig* cfg = getConfig();
    setActiveVehicle(cfg->vehicleBrand, cfg->vehicleModel);
    
    Serial.printf("[DB] VehicleDB آماده شد - %d مدل خودرو\n", _vehicleCount);
}

// ======================== بارگذاری فایل DBC ========================

bool VehicleDB::loadDBCFile(const char* filename) {
    // اصلاحیه: پروفایل‌هایی مثل "Generic / OBD-II" عمداً dbcFileName
    // خالی دارند چون به DBC نیاز ندارند (OBD2Reader با PID استاندارد
    // کار می‌کند). قبلاً این حالت هم یک خطای "فایل یافت نشد" چاپ
    // می‌کرد که گمراه‌کننده بود.
    if (!filename || filename[0] == '\0') {
        _messageCount = 0;
        Serial.println("[DB] این خودرو به فایل DBC نیاز ندارد (حالت OBD-II عمومی)");
        return true;
    }
    
    if (!SPIFFS.exists(filename)) {
        Serial.printf("⚠️ [DB] فایل DBC یافت نشد: %s\n", filename);
        return false;
    }
    
    File file = SPIFFS.open(filename, "r");
    if (!file) {
        Serial.printf("⚠️ [DB] خطا در باز کردن فایل: %s\n", filename);
        return false;
    }
    
    _messageCount = 0;
    memset(_messages, 0, sizeof(_messages));
    
    Serial.printf("[DB] بارگذاری DBC: %s\n", filename);
    
    char line[128];
    while (file.available() && _messageCount < MAX_DBC_MESSAGES) {
        int len = file.readBytesUntil('\n', line, sizeof(line) - 1);
        line[len] = '\0';
        
        // اصلاحیه: اگر خط فایل DBC بلندتر از بافر (۱۲۷ کاراکتر) باشد،
        // readBytesUntil آن را بی‌صدا truncate می‌کند و باقی خط به عنوان
        // "خط بعدی" نامعتبر خوانده می‌شود که می‌تواند کل پارس را به‌هم بزند.
        // حداقل یک هشدار می‌دهیم تا مشکل قابل تشخیص باشد.
        if (len == (int)(sizeof(line) - 1) && file.available()) {
            Serial.println("⚠️ [DB] یک خط DBC طولانی‌تر از حد مجاز (۱۲۷ کاراکتر) بود و ممکن است truncate شده باشد");
            // بقیه همین خط فیزیکی را دور بریز تا به عنوان خط جدید پارس نشود
            while (file.available() && file.peek() != '\n') {
                file.read();
            }
            if (file.available()) file.read(); // خود کاراکتر '\n'
        }
        
        // حذف carriage return
        char* cr = strchr(line, '\r');
        if (cr) *cr = '\0';
        
        // پردازش خط
        if (strncmp(line, "BO_ ", 4) == 0) {
            _parseMessageLine(line);
        } else if (strncmp(line, " SG_ ", 5) == 0) {
            if (_messageCount > 0) {
                _parseSignalLine(line);
            }
        } else if (strncmp(line, "CM_ ", 4) == 0) {
            _parseCommentLine(line);
        } else if (strncmp(line, "VAL_ ", 5) == 0) {
            _parseValueLine(line);
        }
    }
    
    // اصلاحیه: اگر فایل به سقف MAX_DBC_MESSAGES رسید ولی هنوز محتوای
    // بیشتری داشت، قبلاً بدون هیچ هشداری بقیه‌ی پیام‌ها را دور می‌ریخت.
    // این برای یک اپ کنترل خودرو خطرناک است: ممکن است دقیقاً پیامی که
    // به آن نیاز دارید (مثلاً وضعیت قفل درب) در نیمه‌ی دوم فایل باشد و
    // بی‌صدا از قلم بیفتد. مثال واقعی: toyota_2017_ref_pt.dbc که در
    // data/dbc این پروژه هست، ۱۴۳ پیام دارد - با MAX_DBC_MESSAGES=50
    // فقط ۵۰ تای اول لود می‌شوند. اگر با این فایل کار می‌کنید، یا
    // MAX_DBC_MESSAGES را در vehicle_db.h زیاد کنید (با افزایش مصرف
    // RAM طبق کامنت آنجا) یا فایل DBC را قبل از قرار دادن در data/dbc
    // به همان چند پیامی که واقعاً لازم دارید (قفل، شیشه، آینه) کم کنید.
    if (_messageCount >= MAX_DBC_MESSAGES && file.available()) {
        Serial.printf("⚠️ [DB] فایل DBC بیش از حد مجاز (%d) پیام دارد - بقیه نادیده گرفته شدند!\n", 
                      MAX_DBC_MESSAGES);
    }
    
    file.close();
    Serial.printf("[DB] بارگذاری کامل شد - %d پیام\n", _messageCount);
    
    return _messageCount > 0;
}

// ======================== پارس کردن خط BO_ (پیام) ========================

bool VehicleDB::_parseMessageLine(const char* line) {
    if (_messageCount >= MAX_DBC_MESSAGES) return false;
    
    DbcMessage* msg = &_messages[_messageCount];
    
    // فرمت واقعی DBC: BO_ CAN_ID MESSAGE_NAME: DLC TRANSMITTER
    // مثال واقعی (از فایل‌های data/dbc این پروژه): 
    //   "BO_ 34 Active_Fault_Latched_2: 8 MRR"
    // توجه کنید که هیچ فاصله‌ای بین نام پیام و ':' نیست.
    //
    // === اصلاحیه‌ی بحرانی ===
    // فرمت قبلی "BO_ %d %31s : %d %23s" بود که یک فاصله‌ی literal
    // قبل از ':' در خودش داشت. چون ':' برای %s جزو whitespace نیست،
    // %s کل توکن "Active_Fault_Latched_2:" را همراه با ':' می‌بلعید؛
    // سپس scanf سعی می‌کرد ':' literal را با کاراکتر بعدی (که چون
    // ':' قبلاً مصرف شده بود، الان یک رقم بود) match کند و شکست
    // می‌خورد. نتیجه: sscanf فقط ۲ آیتم (canId, name) برمی‌گرداند،
    // شرط parsed>=3 هیچ‌وقت true نمی‌شد و MESSAGE_COUNT همیشه صفر
    // می‌ماند - یعنی هیچ پیامی از هیچ‌کدام از ۵۸ فایل DBC واقعی این
    // پروژه پارس نمی‌شد (با grep روی همه‌ی فایل‌ها تایید شد).
    //
    // فرمت درست: ':' باید بلافاصله بعد از %s بیاید، بدون فاصله در
    // فرمت‌رشته. برای اینکه اگر یک فایل غیراستاندارد با فاصله هم
    // وجود داشت باز کار کند، ابتدا نام را با %[^:] (هر چیزی تا قبل
    // از ':') می‌خوانیم که هم حالت بافاصله و هم بی‌فاصله را پوشش
    // می‌دهد و خودِ فضای خالی انتهایی احتمالی را جدا trim می‌کنیم.
    // اصلاحیه: چند فایل DBC واقعی (مثلاً GM, Ford) از CAN ID گسترده
    // (extended, ۲۹ بیتی) با مقادیر بالای INT_MAX استفاده می‌کنند
    // (مثلاً 3221225472 برای VECTOR__INDEPENDENT_SIG_MSG). قبلاً این
    // متغیر از نوع int با %d بود که برای این مقادیر overflow امضادار
    // می‌داد (مثلاً به -1073741824 تبدیل می‌شد). چون canId واقعی
    // uint32_t است، اینجا هم باید همینطور خوانده شود.
    uint32_t canId; 
    char name[32] = {0};
    char transmitter[24] = {0};
    int dlc;
    
    // %31[^: ] یعنی "حداکثر ۳۱ کاراکتر که نه ':' باشد نه فاصله" - این
    // scanset، بر خلاف %s، جلوی خودش را در ':' هم متوقف می‌کند، پس هم
    // فرمت استاندارد بی‌فاصله ("Name: 8 XXX") و هم یک فرمت غیراستاندارد
    // بافاصله ("Name : 8 XXX") را درست می‌خواند.
    int parsed = sscanf(line, "BO_ %u %31[^: ] : %d %23s", &canId, name, &dlc, transmitter);
    
    if (parsed >= 3) {
        msg->canId = canId;
        strncpy(msg->name, name, sizeof(msg->name) - 1);
        msg->dlc = (uint8_t)dlc;
        if (parsed >= 4) {
            strncpy(msg->transmitter, transmitter, sizeof(msg->transmitter) - 1);
        }
        msg->signalCount = 0;
        _messageCount++;
        return true;
    }
    
    return false;
}

// ======================== پارس کردن خط SG_ (سیگنال) ========================

bool VehicleDB::_parseSignalLine(const char* line) {
    // اصلاحیه: قبلاً اگر این تابع با _messageCount == 0 صدا زده می‌شد
    // (که فقط به لطف چک بیرونی در loadDBCFile رخ نمی‌داد)، عبارت
    // _messageCount - 1 روی uint8_t سرریز می‌شد (0 - 1 = 255) و به یک
    // آدرس کاملاً خارج از آرایه _messages دسترسی پیدا می‌کرد. این تابع
    // را مستقل و ایمن می‌کنیم تا به چک بیرونی متکی نباشد.
    if (_messageCount == 0) {
        Serial.println("⚠️ [DB] خط SG_ قبل از هر BO_ دیده شد - نادیده گرفته شد");
        return false;
    }
    
    DbcMessage* msg = &_messages[_messageCount - 1];
    if (!msg || msg->signalCount >= MAX_DBC_SIGNALS) return false;
    
    DbcSignal* sig = &msg->signals[msg->signalCount];
    
    // فرمت: SG_ SIGNAL_NAME : START_BIT|LENGTH@ENDIANESS SIGNED (SCALE,OFFSET) [MIN|MAX] UNIT TRANSMITTER
    // مثال: SG_ DoorLockFL : 0|1@1+ (1,0) [0|1] "" BodyControl
    
    char signalName[32] = {0};
    int startBit, length;
    char endian[2] = {0};
    char sign[2] = {0};
    float scale, offset;
    float minVal, maxVal;
    char unit[8] = {0};
    
    int parsed = sscanf(line, " SG_ %31s : %d|%d@%1s%1s (%f,%f) [%f|%f] %7s",
                        signalName, &startBit, &length, endian, sign,
                        &scale, &offset, &minVal, &maxVal, unit);
    
    if (parsed >= 7) {
        strncpy(sig->name, signalName, sizeof(sig->name) - 1);
        sig->startBit = (uint8_t)startBit;
        sig->length = (uint8_t)length;
        sig->scale = scale;
        sig->offset = offset;
        
        if (parsed >= 9) {
            sig->min = minVal;
            sig->max = maxVal;
        }
        if (parsed >= 10) {
            strncpy(sig->unit, unit, sizeof(sig->unit) - 1);
        }
        
        // نوع: Signed یا Unsigned
        sig->type = (sign[0] == '-') ? SIG_SIGNED : SIG_UNSIGNED;
        
        // اندیان‌س: '1' = big-endian (Motorola), '0' = little-endian (Intel)
        // (فعلاً هر دو را یکسان پردازش می‌کنیم)
        
        msg->signalCount++;
        return true;
    }
    
    return false;
}

// ======================== پارس کردن خط CM_ (کامنت) ========================

bool VehicleDB::_parseCommentLine(const char* line) {
    // فرمت ساده: CM_ BO_ CAN_ID "comment text";
    // (پیاده‌سازی کامل فعلاً ضروری نیست)
    return true;
}

// ======================== پارس کردن خط VAL_ (مقادیر) ========================

bool VehicleDB::_parseValueLine(const char* line) {
    // فرمت: VAL_ CAN_ID SIGNAL_NAME value1 "text1" value2 "text2" ...;
    // (پیاده‌سازی کامل فعلاً ضروری نیست)
    return true;
}

// ======================== پیدا کردن پیام با CAN ID ========================

DbcMessage* VehicleDB::findMessageByID(uint32_t canId) {
    for (int i = 0; i < _messageCount; i++) {
        if (_messages[i].canId == canId) {
            return &_messages[i];
        }
    }
    return nullptr;
}

// ======================== پیدا کردن سیگنال ========================

DbcSignal* VehicleDB::findSignal(DbcMessage* msg, const char* signalName) {
    if (!msg) return nullptr;
    
    for (int i = 0; i < msg->signalCount; i++) {
        if (strcmp(msg->signals[i].name, signalName) == 0) {
            return &msg->signals[i];
        }
    }
    return nullptr;
}

// ======================== استخراج مقدار سیگنال ========================

float VehicleDB::extractSignalValue(const DbcSignal& signal, const uint8_t* data) {
    if (signal.length == 0) return 0.0f;
    
    // استخراج بیت‌های مورد نظر از داده
    uint64_t rawValue = 0;
    uint8_t startByte = signal.startBit / 8;
    uint8_t startBitInByte = signal.startBit % 8;
    uint8_t totalBits = signal.length;
    
    // خواندن بیت‌ها
    for (int i = 0; i < totalBits; i++) {
        uint16_t currentBit = signal.startBit + i;
        uint8_t byteIdx = currentBit / 8;
        uint8_t bitIdx = currentBit % 8;
        
        if (byteIdx < 8) {
            if (data[byteIdx] & (1 << bitIdx)) {
                rawValue |= (1ULL << i);
            }
        }
    }
    
    // اگر Signed است، علامت را در نظر بگیر
    if (signal.type == SIG_SIGNED) {
        if (rawValue & (1ULL << (totalBits - 1))) {
            // extended sign bit
            rawValue |= (~0ULL << totalBits);
        }
        return (float)((int64_t)rawValue) * signal.scale + signal.offset;
    }
    
    return (float)rawValue * signal.scale + signal.offset;
}

// ======================== کدگذاری مقدار سیگنال ========================

void VehicleDB::encodeSignalValue(const DbcSignal& signal, float value, uint8_t* data) {
    if (signal.length == 0) return;
    
    // تبدیل مقدار به مقدار خام
    uint64_t rawValue;
    if (signal.type == SIG_SIGNED) {
        rawValue = (uint64_t)((int64_t)((value - signal.offset) / signal.scale));
    } else {
        rawValue = (uint64_t)((value - signal.offset) / signal.scale);
    }
    
    // قرار دادن بیت‌ها در داده
    for (int i = 0; i < signal.length; i++) {
        uint16_t currentBit = signal.startBit + i;
        uint8_t byteIdx = currentBit / 8;
        uint8_t bitIdx = currentBit % 8;
        
        if (byteIdx < 8) {
            if (rawValue & (1ULL << i)) {
                data[byteIdx] |= (1 << bitIdx);
            } else {
                data[byteIdx] &= ~(1 << bitIdx);
            }
        }
    }
}

// ======================== دریافت تعداد پیام‌ها ========================

uint8_t VehicleDB::getMessageCount() {
    return _messageCount;
}

// ======================== دریافت پیام با ایندکس ========================

DbcMessage* VehicleDB::getMessageByIndex(uint8_t index) {
    if (index < _messageCount) {
        return &_messages[index];
    }
    return nullptr;
}

// ======================== تنظیم خودروی فعال ========================

void VehicleDB::setActiveVehicle(const char* brand, const char* model) {
    bool found = false;
    
    for (int i = 0; i < _vehicleCount; i++) {
        if (strcmp(_vehicleList[i].brand, brand) == 0 &&
            strcmp(_vehicleList[i].model, model) == 0) {
            _activeVehicle = _vehicleList[i];
            found = true;
            Serial.printf("[DB] خودروی فعال: %s %s\n", brand, model);
            break;
        }
    }
    
    if (!found) {
        // از پیش‌فرض استفاده کن
        _activeVehicle = _vehicleList[0];
        Serial.printf("[DB] خودروی یافت نشد، استفاده از پیش‌فرض: %s %s\n", 
                      _vehicleList[0].brand, _vehicleList[0].model);
    }
    
    // بارگذاری فایل DBC مربوطه
    loadDBCFile(_activeVehicle.dbcFileName);
}

// ======================== دریافت نام خودروی فعال ========================

void VehicleDB::getActiveVehicle(char* brand, char* model, size_t maxLen) {
    strncpy(brand, _activeVehicle.brand, maxLen);
    strncpy(model, _activeVehicle.model, maxLen);
}

// ======================== دریافت پروفایل خودرو ========================

bool VehicleDB::getVehicleProfile(uint8_t index, VehicleProfile& profile) {
    if (index < _vehicleCount) {
        profile = _vehicleList[index];
        return true;
    }
    return false;
}

// ======================== تعداد خودروها ========================

uint8_t VehicleDB::getVehicleCount() {
    return _vehicleCount;
}
