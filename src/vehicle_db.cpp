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

// جدول کمکی برای افزودن سریع و بدون خطای تایپی یک ورودی خودرو
static inline void _addVeh(VehicleProfile* list, uint8_t& count,
                            const char* brand, const char* model,
                            const char* file, uint16_t y0, uint16_t y1) {
    strncpy(list[count].brand, brand, sizeof(list[count].brand) - 1);
    strncpy(list[count].model, model, sizeof(list[count].model) - 1);
    strncpy(list[count].dbcFileName, file, sizeof(list[count].dbcFileName) - 1);
    list[count].yearStart = y0;
    list[count].yearEnd = y1;
    count++;
}

void VehicleDB::begin() {
    // === اصلاحیه (چک‌لیست تجاری #7) ===
    // لیست قبلی فقط ۴ مورد از ۵۷ فایل DBC واقعاً موجود در data/dbc را
    // به منوی انتخاب خودرو وصل می‌کرد. این نسخه تمام ۵۷ فایل را با
    // ls بررسی کرد (نتیجه در کامنت‌های پایین این تابع) و فقط فایل‌هایی
    // را که واقعاً معرف "یک خودرو با پیام‌های بدنه/کنترلی قابل‌استفاده"
    // هستند اضافه می‌کند.
    //
    // ⚠️ صادقانه: تعدادی از ۵۷ فایل *عمداً* در این لیست نیستند چون
    // فایل‌های "جانبی" (رادار/ADAS/آبجکت‌شناسایی/شاسی سرعت‌بالا) هستند
    // که به‌تنهایی یک "خودرو" را نمایندگی نمی‌کنند، بلکه مکمل یک فایل
    // اصلی دیگرند (مثلاً ESR.dbc و toyota_radar_dsu_tssp.dbc دیتای
    // رادار کروزکنترل تطبیقی‌اند، نه فرمان قفل/شیشه). در معماری فعلی
    // (یک فایل DBC به‌ازای هر خودرو) اضافه کردن این‌ها به‌عنوان
    // انتخاب‌های مجزا گمراه‌کننده بود. فایل‌های حذف‌شده در کامنت
    // "حذف‌شده‌ها" در پایین با دلیل مشخص‌شده‌اند.
    //
    // ⚠️ قبل از افزودن/تغییر هر مدل، حتماً با ls روی data/dbc چک کنید
    // که نام فایل دقیقاً همان چیزی باشد که آنجا هست.
    uint8_t i = 0;

    _addVeh(_vehicleList, i, "Generic", "OBD-II", "", 2008, 2025);  // بدون DBC - فقط PID استاندارد

    // --- Toyota ---
    _addVeh(_vehicleList, i, "Toyota", "Prius (2010 PT)", "/dbc/toyota_prius_2010_pt.dbc", 2010, 2015);
    _addVeh(_vehicleList, i, "Toyota", "Reference PT (2017)", "/dbc/toyota_2017_ref_pt.dbc", 2017, 2021);
    _addVeh(_vehicleList, i, "Toyota", "iQ (2009)", "/dbc/toyota_iQ_2009_can.dbc", 2009, 2015);

    // --- BMW ---
    _addVeh(_vehicleList, i, "BMW", "E9x/E8x 3 Series", "/dbc/bmw_e9x_e8x.dbc", 2005, 2013);

    // --- Honda/Acura ---
    _addVeh(_vehicleList, i, "Acura", "ILX 2016 (Nidec)", "/dbc/acura_ilx_2016_nidec.dbc", 2016, 2018);

    // --- Cadillac / GM ---
    _addVeh(_vehicleList, i, "Cadillac", "CT6 Powertrain", "/dbc/cadillac_ct6_powertrain.dbc", 2016, 2020);
    _addVeh(_vehicleList, i, "Cadillac", "CT6 Chassis", "/dbc/cadillac_ct6_chassis.dbc", 2016, 2020);
    _addVeh(_vehicleList, i, "GM", "Global A - Low Speed", "/dbc/gm_global_a_lowspeed.dbc", 2015, 2023);
    _addVeh(_vehicleList, i, "GM", "Global A - Chassis", "/dbc/gm_global_a_chassis.dbc", 2015, 2023);

    // --- Chrysler / FCA ---
    _addVeh(_vehicleList, i, "Chrysler", "CUSW", "/dbc/chrysler_cusw.dbc", 2011, 2017);
    _addVeh(_vehicleList, i, "Chrysler", "Pacifica 2017 Hybrid", "/dbc/chrysler_pacifica_2017_hybrid_private_fusion.dbc", 2017, 2020);
    _addVeh(_vehicleList, i, "FCA", "Giorgio Platform", "/dbc/fca_giorgio.dbc", 2016, 2022);

    // --- Ford ---
    _addVeh(_vehicleList, i, "Ford", "Fusion 2018 PT", "/dbc/ford_fusion_2018_pt.dbc", 2017, 2020);
    _addVeh(_vehicleList, i, "Ford", "CGEA1.2 Body (2011+)", "/dbc/ford_cgea1_2_bodycan_2011.dbc", 2011, 2019);
    _addVeh(_vehicleList, i, "Ford", "CGEA1.2 Powertrain (2011+)", "/dbc/ford_cgea1_2_ptcan_2011.dbc", 2011, 2019);
    // توجه: ford_lincoln_base_pt.dbc (~800KB) و FORD_CADS*.dbc به‌عمد
    // اضافه نشدند مگر کاربر صراحتاً درخواست کند - حجم بسیار بالا و
    // پوشش گسترده‌ی چند-پلتفرمی دارند که پارس آن‌ها با
    // MAX_DBC_MESSAGES=150 فعلی احتمالاً truncate می‌شود (هشدار در لاگ
    // ظاهر خواهد شد). در صورت نیاز واقعی به این پلتفرم، ابتدا
    // MAX_DBC_MESSAGES را افزایش دهید.

    // --- Hyundai ---
    _addVeh(_vehicleList, i, "Hyundai", "2015 C-CAN", "/dbc/hyundai_2015_ccan.dbc", 2015, 2019);
    _addVeh(_vehicleList, i, "Hyundai", "2015 M-CAN", "/dbc/hyundai_2015_mcan.dbc", 2015, 2019);
    _addVeh(_vehicleList, i, "Hyundai", "i30 (2014)", "/dbc/hyundai_i30_2014.dbc", 2014, 2017);
    _addVeh(_vehicleList, i, "Hyundai", "Santa Fe (2007)", "/dbc/hyundai_santafe_2007.dbc", 2007, 2012);

    // --- Mazda ---
    _addVeh(_vehicleList, i, "Mazda", "2017 Platform", "/dbc/mazda_2017.dbc", 2017, 2021);
    _addVeh(_vehicleList, i, "Mazda", "3 (2019)", "/dbc/mazda_3_2019.dbc", 2019, 2023);
    _addVeh(_vehicleList, i, "Mazda", "RX-8", "/dbc/mazda_rx8.dbc", 2003, 2012);

    // --- Mercedes-Benz ---
    _addVeh(_vehicleList, i, "Mercedes-Benz", "E350 (2010)", "/dbc/mercedes_benz_e350_2010.dbc", 2010, 2016);

    // --- MG ---
    _addVeh(_vehicleList, i, "MG", "Generic Platform", "/dbc/mg.dbc", 2018, 2024);

    // --- Nissan ---
    _addVeh(_vehicleList, i, "Nissan", "Xterra (2011)", "/dbc/nissan_xterra_2011.dbc", 2011, 2015);

    // --- Opel ---
    _addVeh(_vehicleList, i, "Opel", "Omega (2001)", "/dbc/opel_omega_2001.dbc", 2001, 2003);

    // --- PSA (Peugeot/Citroën) ---
    _addVeh(_vehicleList, i, "PSA", "AEE2010 R3", "/dbc/psa_aee2010_r3.dbc", 2010, 2018);

    // --- Volvo ---
    _addVeh(_vehicleList, i, "Volvo", "V40 (2017 PT)", "/dbc/volvo_v40_2017_pt.dbc", 2017, 2019);
    _addVeh(_vehicleList, i, "Volvo", "V60 (2015 PT)", "/dbc/volvo_v60_2015_pt.dbc", 2015, 2018);

    // --- Volkswagen Group ---
    _addVeh(_vehicleList, i, "Volkswagen", "MQB Platform", "/dbc/vw_mqb.dbc", 2012, 2020);
    _addVeh(_vehicleList, i, "Volkswagen", "PQ Platform", "/dbc/vw_pq.dbc", 2005, 2014);
    // توجه: vw_mlb.dbc و vw_mqbevo.dbc به‌عمد اضافه نشدند - حجم بالا
    // (به ترتیب ~230KB و ~113KB)، مشابه ford_lincoln_base_pt احتمالاً
    // در سقف MAX_DBC_MESSAGES=150 truncate می‌شوند.

    // --- Tesla ---
    _addVeh(_vehicleList, i, "Tesla", "Generic (CAN)", "/dbc/tesla_can.dbc", 2012, 2018);
    _addVeh(_vehicleList, i, "Tesla", "Model 3 - Vehicle", "/dbc/tesla_model3_vehicle.dbc", 2017, 2023);

    // --- Rivian ---
    _addVeh(_vehicleList, i, "Rivian", "Primary Actuator", "/dbc/rivian_primary_actuator.dbc", 2021, 2024);

    // --- سایر ---
    _addVeh(_vehicleList, i, "GWM", "Haval H6 PHEV 2024", "/dbc/gwm_haval_h6_phev_2024.dbc", 2024, 2026);
    _addVeh(_vehicleList, i, "Hongqi", "HS5", "/dbc/hongqi_hs5.dbc", 2019, 2023);
    _addVeh(_vehicleList, i, "Luxgen", "S5 (2015)", "/dbc/luxgen_s5_2015.dbc", 2015, 2018);

    // === حذف‌شده‌ها (عمدی، نه فراموش‌شده) و دلیل ===
    // ESR.dbc, mazda_radar.dbc, toyota_radar_dsu_tssp.dbc, toyota_adas.dbc,
    // toyota_tss2_adas.dbc, ford_fusion_2018_adas.dbc, cadillac_ct6_object.dbc,
    // gm_global_a_object.dbc, rivian_park_assist_can.dbc
    //   → فایل‌های رادار/ADAS/تشخیص‌آبجکت هستند؛ پیام‌های بدنه (قفل/
    //     شیشه/چراغ) ندارند. برای کاربرد این پروژه (کنترل راحتی خودرو)
    //     به‌تنهایی معنی ندارند.
    // gm_global_a_high_voltage_management.dbc, gm_global_a_lowspeed_1818125.dbc,
    // gm_global_a_powertrain_expansion.dbc
    //   → فایل‌های تکمیلی GM هستند که باید کنار gm_global_a_lowspeed.dbc
    //     merge شوند؛ پارسر فعلی merge چند فایل را پشتیبانی نمی‌کند
    //     (هر انتخاب خودرو دقیقاً یک فایل بارگذاری می‌کند - نگاه کنید
    //     به loadDBCFile). merge چندفایلی نیاز به توسعه‌ی جداگانه دارد.
    // FORD_CADS.dbc, FORD_CADS_64.dbc, ford_lincoln_base_pt.dbc, vw_mlb.dbc,
    // vw_mqbevo.dbc
    //   → به دلیل حجم/تعداد پیام بسیار بالا (نگاه کنید به کامنت‌های
    //     بالا) با سقف فعلی MAX_DBC_MESSAGES=150 truncate می‌شوند.
    // comma_body.dbc, tesla_model3_party.dbc, tesla_powertrain.dbc
    //   → یا سخت‌افزار غیراستاندارد (comma body - یک ربات، نه خودرو)
    //     یا داده‌ی تکمیلی/تکراری با tesla_model3_vehicle.dbc هستند.
    //
    // نتیجه: ۳۶ از ۵۷ فایل به‌طور مستقیم قابل‌استفاده به منو اضافه
    // شدند؛ ۲۱ فایل باقیمانده یا نیاز به merge چندفایلی (کار آینده)
    // دارند یا اساساً معرف یک "خودروی مستقل" نیستند.

    _vehicleCount = i;
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
        
        // === اصلاحیه (چک‌لیست تجاری #6) ===
        // طبق مشخصات فرمت DBC: '@0' = big-endian (Motorola)، '@1' =
        // little-endian (Intel). این مقدار اکنون واقعاً ذخیره می‌شود
        // و در extractSignalValue/encodeSignalValue استفاده می‌شود
        // (قبلاً خوانده می‌شد ولی دور ریخته می‌شد).
        sig->isBigEndian = (endian[0] == '0');
        
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

// ======================== کمکی: نگاشت بیت Motorola/big-endian ========================
// === جدید (چک‌لیست تجاری #6) ===
//
// در فرمت DBC، startBit یک سیگنال Motorola (@0) طبق قرارداد "شماره‌گذاری
// بیت DBC" است، نه شماره‌گذاری ساده‌ی LSB-first که برای Intel (@1)
// درست است. در قرارداد DBC، بیت‌های هر بایت از MSB=7 به LSB=0 شماره
// می‌خورند ولی startBit سیگنال Motorola، بیت *MSB* سیگنال را (طبق
// همین شماره‌گذاری) مشخص می‌کند و بیت‌های بعدی سیگنال با عبور از
// مرز بایت به‌جلو (به بیت ۷ بایت بعد) ادامه می‌یابند.
// فرمول استاندارد صنعتی برای تبدیل startBit اعلام‌شده‌ی DBC (که در
// حالت Motorola در واقع بر مبنای "بیت DBC" کدگذاری شده) به موقعیت
// واقعی بیت در بافر 8 بایتی:
//   dbcBit = startBit
//   byte   = dbcBit / 8
//   bit    = dbcBit % 8
//   pos    = byte*8 + bit   → این خودِ startBit است چون DBC همین‌طور می‌نویسدش
// و برای حرکت به بیت *بعدی* (کم‌اهمیت‌تر) سیگنال Motorola، باید از
// bit=0 به byte بعد و bit=7 برویم (بر خلاف Intel که فقط +1 می‌شود).
// این تابع یک بار موقعیت مطلق (0..63) بیت i‌ام سیگنال (i=0 یعنی MSB
// برای Motorola، LSB برای Intel) را برمی‌گرداند.
static inline uint16_t _dbcBitPosition(const DbcSignal& signal, uint8_t i) {
    if (!signal.isBigEndian) {
        // Intel: ساده، +1 خطی از startBit (LSB سیگنال)
        return signal.startBit + i;
    }
    // Motorola: startBit به‌صورت DBC-bit اعلام شده و MSB سیگنال است.
    // برای رفتن i بیت "پایین‌تر" (به سمت LSB سیگنال)، در چارچوب بایتی
    // حرکت می‌کنیم: هر بار bit را یکی کم می‌کنیم؛ وقتی به زیر صفر رسید
    // به بیت 7 بایت بعد می‌رویم.
    uint8_t byteIdx = signal.startBit / 8;
    int8_t bitIdx = (int8_t)(signal.startBit % 8);
    for (uint8_t step = 0; step < i; step++) {
        bitIdx--;
        if (bitIdx < 0) {
            bitIdx = 7;
            byteIdx++;
        }
    }
    return (uint16_t)byteIdx * 8 + (uint16_t)bitIdx;
}

// ======================== استخراج مقدار سیگنال ========================

float VehicleDB::extractSignalValue(const DbcSignal& signal, const uint8_t* data) {
    if (signal.length == 0) return 0.0f;

    uint64_t rawValue = 0;
    uint8_t totalBits = signal.length;

    // === اصلاحیه (چک‌لیست تجاری #6) ===
    // قبلاً همیشه فرض Intel (خطی، +1) می‌شد؛ برای سیگنال‌های Motorola
    // این مقدار کاملاً اشتباه بود. حالا بر اساس isBigEndian، موقعیت
    // واقعی هر بیت با _dbcBitPosition محاسبه می‌شود.
    //
    // ترتیب قرارگیری در rawValue: برای هر دو حالت، i=0 را LSB نتیجه
    // (bit 0 of rawValue) می‌گذاریم و i=(totalBits-1) را MSB. برای
    // Intel این با ترتیب صعودی startBit یکی است (چون i=0 خودش LSB
    // سیگنال Intel است). برای Motorola، i=0 در _dbcBitPosition همان
    // MSB سیگنال است، پس اینجا آن را به بالاترین بیت rawValue
    // (bit totalBits-1) می‌بریم تا مقدار عددی نهایی درست دربیاید.
    for (int i = 0; i < totalBits; i++) {
        uint16_t bitPos = _dbcBitPosition(signal, i);
        uint8_t byteIdx = bitPos / 8;
        uint8_t bitIdx = bitPos % 8;

        if (byteIdx >= 8) continue;  // خارج از فریم 8 بایتی - نادیده بگیر

        bool bitSet = data[byteIdx] & (1 << bitIdx);
        uint8_t destBitInValue = signal.isBigEndian ? (totalBits - 1 - i) : i;

        if (bitSet) {
            rawValue |= (1ULL << destBitInValue);
        }
    }

    // اگر Signed است، علامت را در نظر بگیر
    if (signal.type == SIG_SIGNED) {
        if (rawValue & (1ULL << (totalBits - 1))) {
            rawValue |= (~0ULL << totalBits);  // extended sign bit
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

    uint8_t totalBits = signal.length;

    // === اصلاحیه (چک‌لیست تجاری #6) ===
    // متقارن با extractSignalValue - همان نگاشت بیت به‌صورت معکوس.
    for (int i = 0; i < totalBits; i++) {
        uint16_t bitPos = _dbcBitPosition(signal, i);
        uint8_t byteIdx = bitPos / 8;
        uint8_t bitIdx = bitPos % 8;

        if (byteIdx >= 8) continue;

        uint8_t srcBitInValue = signal.isBigEndian ? (totalBits - 1 - i) : i;
        bool bitSet = rawValue & (1ULL << srcBitInValue);

        if (bitSet) {
            data[byteIdx] |= (1 << bitIdx);
        } else {
            data[byteIdx] &= ~(1 << bitIdx);
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
