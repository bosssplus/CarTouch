# CarTouch — مشخصات فنی

این سند مرجع فنی پروژه است. اگر رفتاری در این سند با کد واقعی مغایر بود، کد و تست قابل بازتولید باید بررسی و این سند پس از تعیین رفتار واقعی به‌روزرسانی شود.

## 1. هدف

CarTouch یک firmware برای ESP32-S3 است که از CAN Bus خودرو داده دریافت می‌کند و در شرایط تأییدشده می‌تواند فرمان‌های کنترلی را ارسال کند. دو رابط اصلی دارد:

1. TFT لمسی با LVGL.
2. Web Dashboard روی Wi‑Fi.

طراحی باید در برابر ارسال ناخواسته‌ی CAN محافظه‌کار باشد.

## 2. اجزای اصلی

### `main.cpp`

نقطه‌ی ورود سیستم است و lifecycle ماژول‌ها را در `setup()` و اجرای دوره‌ای آن‌ها را در `loop()` هماهنگ می‌کند.

وظایف اصلی:

- ایجاد objectهای بزرگ روی heap.
- راه‌اندازی watchdog.
- بارگذاری configuration.
- راه‌اندازی SPIFFS.
- راه‌اندازی CAN.
- راه‌اندازی OBD-II.
- راه‌اندازی VehicleDB و پروفایل‌های سفارشی.
- اتصال Learn Mode و ActiveProfileManager به UI و Web.
- اجرای LVGL/WebSocket/Learn/OBD در حلقه‌ی اصلی.
- مدیریت sleep/wake؛ پس از بیدار شدن از طریق CAN، ارسال درخواست‌های OBD برای ۱ ثانیه متوقف می‌شود تا wake event باعث TX فوری ناخواسته نشود.

`loop()` نباید روی عملیات شبکه یا OBD به‌صورت طولانی مسدود شود.

### `config.cpp/.h`

منبع مرکزی:

- پین‌ها
- CAN speed
- OBD settings
- TFT/LVGL settings
- Wi‑Fi/web settings
- power-management settings
- vehicle selection
- Learn Mode settings
- credential/session helpers

Configuration پایدار در NVS نگهداری می‌شود.

### `can_manager.cpp/.h`

لایه‌ی انتزاع TWAI:

- نصب و شروع driver
- stop/uninstall
- تغییر Listen-Only/Normal
- ارسال
- دریافت blocking در APIهای مخصوص
- دریافت non-blocking برای loop/Learn Mode
- flush صف
- status/diagnostics

تغییر mode باید lifecycle واقعی driver را رعایت کند؛ تغییر یک flag در برنامه به‌تنهایی enforcement سخت‌افزاری Listen-Only نیست.

### `vehicle_db.cpp/.h`

مسئول:

- فهرست پروفایل‌های built-in
- بارگذاری فایل DBC
- parse پیام‌ها (`BO_`) و سیگنال‌ها (`SG_`)؛ `CM_` و `VAL_` تفسیر نمی‌شوند
- پیدا کردن پیام/سیگنال
- استخراج و encode مقدار سیگنال
- نگهداری سقف تعداد پیام‌ها

هر فایل DBC یک منبع مستقل است؛ merge خودکار چند DBC در یک profile در معماری پروژه پشتیبانی نمی‌شود.

### `obd2_reader.cpp/.h`

دو سطح API دارد:

- APIهای خواندن مستقیم PID که می‌توانند blocking باشند و فقط برای مسیرهای مشخص استفاده می‌شوند.
- state machine غیرمسدودکننده برای استفاده‌ی اصلی firmware.

مسیر اصلی `loop()` باید از `update()` و `getLatestData()` استفاده کند.

### `vehicle_control.cpp/.h`

لایه‌ی اجرای فرمان:

- دریافت label فرمان
- resolve کردن فرمان از profile فعال
- محدودیت زمانی بین فرمان‌ها
- محدودیت duty-cycle actuatorهای مکانیکی
- مسیر verification
- wrapperهای فرمان‌های شناخته‌شده
- گزارش خطا

فرمان نباید از یک CAN ID حدسی ساخته شود وقتی profile فعال اطلاعات دقیق‌تری در اختیار دارد.

### `custom_vehicle.h` و `custom_vehicle_store.cpp/.h`

مدل و persistence پروفایل‌های سفارشی:

- اطلاعات خودرو
- فرمان‌های learned/manual
- وضعیت verification
- منبع فرمان
- serialization به JSON
- index
- import/export
- ایجاد/ویرایش/حذف

SPIFFS در این معماری برای فایل‌های JSON پروفایل استفاده می‌شود.

### `learn_engine.cpp/.h`

state machine یادگیری:

1. شروع session.
2. اطمینان از Listen-Only سخت‌افزاری.
3. capture baseline.
4. capture action.
5. استخراج candidateها.
6. ranking.
7. آماده‌سازی برای verification.
8. cancel و بازگرداندن mode قبلی.

در هیچ مرحله‌ای قبل از تأیید صریح کاربر نباید فرمان کنترل واقعی از Learn Mode ارسال شود.

### `active_profile_manager.cpp/.h`

منبع فعال فرمان را یکپارچه می‌کند:

- هیچ profile انتخاب نشده
- built-in DBC
- custom profile

VehicleControl باید از این manager برای resolve فرمان استفاده کند.

### `tft_ui.cpp/.h`

مسئول:

- initialization نمایشگر و touch
- calibration
- Control
- Dashboard
- Learn
- Settings
- Manual Entry
- Verify
- password UI
- status indicators
- theme
- notifications
- power state

منطق CAN و business logic نباید در callbackهای UI تکثیر شود.

### `webserver.cpp/.h`

مسئول:

- HTTP
- authentication
- session token
- WebSocket
- custom profile REST API
- Learn Mode messages
- control/status API
- OTA
- broadcasting data/status

تمام endpointهای حساس باید قبل از عملیات state-changing احراز هویت و authorization لازم را بررسی کنند.

### `wifi_manager.cpp/.h`

مدیریت AP/STA، اتصال، قطع، scan و status شبکه.

### `ble_manager.cpp/.h`

BLE مستقل از Wi‑Fi:

- Status (read/notify)، Command و Data با نوشتن فقط روی لینک رمزنگاری‌شده
- BLE OTA با رمز فعلی وب در لایه‌ی برنامه؛ OTA با رمز پیش‌فرض رد می‌شود
- قفل ۶۰ ثانیه‌ای پس از ۵ رمز اشتباه
- Pairing از نوع Just Works است (بدون حفاظت MITM)؛ جزئیات در `BLE_OTA.md`

### `module_status.cpp/.h`

وضعیت runtime ماژول‌های Wi-Fi، Web Server، CAN، OBD-II، Touch، Display، BLE و Storage را برای TFT و Web نگهداری می‌کند. چرخه‌ی وضعیت‌ها `DETECTED`، `INITIALIZING`، `READY`، `NOT_PRESENT`، `ERROR` و `DISABLED` است؛ این وضعیت runtime به‌تنهایی تشخیص الکتریکی حضور سخت‌افزار را تضمین نمی‌کند.

### `error_log.cpp/.h`

- ring buffer رویدادها
- category/severity
- شمارنده‌های persistent
- serialization JSON
- ثبت رویدادهای subsystemها

## 3. جریان داده

### دریافت CAN

```text
CAN transceiver
      │
      ▼
TWAI
      │
      ▼
CANManager
   ┌──┴───────────────┐
   │                  │
   ▼                  ▼
OBD2Reader        LearnEngine
   │                  │
   └───────┬──────────┘
           ▼
     UI / Web / Logs
```

### ارسال فرمان

```text
TFT/Web
   │
   ▼
authentication / UI rules
   │
   ▼
ActiveProfileManager
   │
   ▼
VehicleControl
   │
   ├── rate limit
   ├── duty-cycle
   ├── verification state
   └── Listen-Only guard
   │
   ▼
CANManager
   │
   ▼
TWAI → CAN transceiver
```

## 4. Learn Mode و verification

### baseline

در ابتدای session، پیام‌های موجود روی bus در Listen-Only ثبت می‌شوند تا تغییرات ناشی از فشردن دکمه با background traffic مقایسه شود.

### action capture

پس از آماده‌شدن baseline، پیام‌های جدید/تغییریافته ثبت و برای candidate generation استفاده می‌شوند.

### verification

هر candidate با وضعیت تأییدنشده ذخیره می‌شود. UI باید داده‌ی لازم برای بررسی انسانی، از جمله CAN ID و payload مربوط، را نشان دهد.

تنها مسیر verification مجاز است که یک فرمان unverified را برای آزمایش ارسال کند؛ این ارسال باید صریحاً توسط کاربر آغاز شود.

پس از تأیید:

```text
UNVERIFIED → VERIFIED
```

و فقط فرمان verified می‌تواند در مسیر عادی control فعال شود.

## 5. Listen-Only

Listen-Only باید در دو سطح رعایت شود:

1. **سخت‌افزار/TWAI:** driver در mode دریافت-only واقعی قرار گیرد.
2. **منطق برنامه:** مسیرهای ارسال فرمان و OBD request نیز هنگام فعال بودن Listen-Only مسدود باشند.

هیچ‌کدام جای دیگری را به‌تنهایی کافی نمی‌کند.

## 6. OBD-II

OBD polling اصلی باید غیرمسدودکننده باشد.

هر چرخه‌ی polling فقط مقدار محدودی کار انجام می‌دهد و state machine به فراخوانی بعدی `update()` ادامه می‌یابد.

PIDهای استاندارد باید با طول پاسخ و پشتیبانی ECU بررسی شوند و در نبود داده‌ی معتبر مقدار ساختگی تولید نشود.

ولتاژ نمایش‌داده‌شده از OBD PID `0x42` است، نه ADC مستقیم باتری. فقط مقادیر ۶ تا ۳۶ ولت معتبرند؛ timeout، شکست درخواست یا مقدار خارج از محدوده UI را به `N/A` می‌برد. اندازه‌گیری مستقل ولتاژ/درصد شارژ نیازمند ورودی سنسور و کالیبراسیون سخت‌افزاری است و پشتیبانی نمی‌شود.

خواندن DTC و پاک‌کردن DTC عملیات حساس هستند و باید جدا از polling عادی و با authorization مناسب انجام شوند.

## 7. DBC

parser:

- پیام‌های `BO_`
- سیگنال‌های `SG_` با indentationهای متداول DBC

را در حد grammar مورد استفاده‌ی پروژه پردازش می‌کند و سیگنال‌های هر پیام را به‌صورت پویا نگهداری می‌کند. `CM_` و `VAL_` تفسیر نمی‌شوند و parser آن‌ها را به‌عنوان داده‌ی قابل استفاده ثبت نمی‌کند.

parser تا `MAX_DBC_MESSAGES` پیام در هر فایل را نگه می‌دارد و سیگنال‌های هر پیام را به‌صورت پویا ذخیره می‌کند. سقف برای پروفایل‌های مستقیم منوی خودرو 400 پیام است؛ فایل‌های بزرگ‌تر یا چندمنبعی همچنان نیازمند معماری چندفایلی/ذخیره‌سازی گسترده‌تر هستند.

Intel و Motorola باید با mapping بیت صحیح پردازش شوند. تغییر در این قسمت بدون test vector خطرناک است.

برای CAN ID، parser شناسه‌های استاندارد 11 بیتی را بدون تغییر نگه می‌دارد و شناسه‌های Extended را به arbitration ID 29 بیتی به‌همراه پرچم `isExtended` تبدیل می‌کند. DBC استاندارد معمولاً Extended را با bit 31 مشخص می‌کند؛ برخی فایل‌های vendor/OpenDBC شناسه‌ی 29 بیتی را بدون این marker ذخیره می‌کنند و parser آن‌ها را نیز به‌عنوان Extended تشخیص می‌دهد. شناسه‌ی pseudo-message با نام `VECTOR__INDEPENDENT_SIG_MSG` روی CAN بارگذاری نمی‌شود.

## 8. حافظه

`VehicleDB` و objectهای وابسته‌ی بزرگ روی heap ایجاد می‌شوند تا `.bss` داخلی بیش از حد بزرگ نشود.

هر تغییر در:

- تعداد messageها
- تعداد signalها
- اندازه‌ی profileها
- bufferهای UI
- WebSocket client table
- DBC parser

باید با اندازه‌گیری heap/PSRAM بررسی شود.

هیچ فرضی درباره‌ی PSRAM نباید جای check واقعی allocation را بگیرد.

## 9. Web و احراز هویت

سطح حمله‌ی اصلی:

- login
- session token
- WebSocket
- profile import
- command execution
- OTA

است.

الزامات:

- endpointهای حساس بدون authentication قابل استفاده نباشند.
- session token تاریخ مصرف/اعتبار داشته باشد.
- تغییر password نشست‌های قبلی را invalidate کند.
- داده‌ی ورودی JSON با اندازه و ساختار محدود پردازش شود.
- import profile نباید باعث overflow، path traversal یا مصرف بی‌نهایت حافظه شود.
- OTA بدون authentication ممنوع باشد.
- پیام WebSocket قبل از dispatch اعتبارسنجی شود.

HTTPS پشتیبانی نمی‌شود و مستندات نباید خلاف آن ادعا کنند.

## 10. OTA و filesystem

پروژه برای flash شانزده مگابایتی از partition table اختصاصی `cartouch_16MB.csv` استفاده می‌کند.

Partition table باید فضای کافی برای:

- NVS
- OTA metadata
- دو app partition
- SPIFFS

فراهم کند.

**محدودیت طول مسیر SPIFFS:** هر مسیر (شامل `/` ابتدایی) حداکثر ۳۱ نویسه است. این محدودیت برای فایل‌های `data/` (در CI بررسی می‌شود) و برای فایل‌های زمان اجرا نیز صادق است؛ به‌ویژه فایل‌های journal با پسوند `.tmp` و `.bak` که به نام فایل اصلی اضافه می‌شوند. پروفایل‌های سفارشی به‌صورت `/custom_vehicles/pN.json` ذخیره می‌شوند و این طول با `static_assert` در `custom_vehicle_store.cpp` در زمان کامپایل کنترل می‌شود.

در صورت شکست mount، firmware بدون format خودکار boot را ادامه می‌دهد، خطا را ثبت و Storage را `ERROR` اعلام می‌کند؛ عملیات پروفایل سفارشی fail-closed می‌شوند تا از پاک‌شدن داده برای بازیابی موقت جلوگیری شود.

CI پس از ساخت filesystem، اندازه‌ی `spiffs.bin` تولیدشده را مستقیماً با اندازه‌ی پارتیشن `spiffs` در `cartouch_16MB.csv` مقایسه می‌کند تا افزایش DBCها باعث overflow پنهان یا شکست دیرهنگام upload نشود.

## 11. CI و کیفیت

Workflow اصلی باید حداقل این مراحل را اجرا کند:

1. checkout
2. نصب PlatformIO
3. firmware build
4. filesystem build
5. static analysis
6. اجرای native unit tests با `pio test -e native`
7. بررسی اندازه‌ی `spiffs.bin` در برابر پارتیشن واقعی
8. artifact upload

هر خطای build باید باعث شکست workflow شود.

## 12. قراردادهای ایمنی

هر تغییر کدی که می‌تواند مسیر CAN TX را تغییر دهد باید این موارد را بررسی کند:

- آیا Listen-Only واقعاً جلوی ارسال را می‌گیرد؟
- آیا command verification bypass نشده؟
- آیا rate limit باقی مانده؟
- آیا duty-cycle باقی مانده؟
- آیا profile فعال همان profile مورد انتظار است؟
- آیا payload از داده‌ی معتبر profile آمده؟
- آیا ورودی UI/Web می‌تواند مستقیماً به CAN frame تبدیل شود؟

هیچ refactor زیبایی‌شناختی نباید این guardها را حذف یا دور بزند.

## 13. تست‌های خودکار و محدودیت اعتبارسنجی

تست native منطق مستقل از سخت‌افزار را پوشش می‌دهد، از جمله:

- Listen-Only و CAN TX admission guard
- timerهای wrap-safe
- parser مشترک hex برای Web/TFT
- OBD Single-Frame PCI/DLC/service/PID validation
- DTC pair-length validation
- DBC signal/DLC boundary validation برای Intel و Motorola
- verification transaction و rejectionهای profile/label/token/timeout
- verification fingerprint برای تغییر command/profile revision
- validation سخت فیلدهای JSON import

موارد زیر در CI (GitHub Actions) اجرا می‌شوند و نتیجه‌ی همان اجرا معیار است: build کامل firmware، filesystem image و اندازه‌ی آن، static analysis. موارد زیر نیازمند اجرای محیط/سخت‌افزار مناسب هستند و نباید بدون اجرای واقعی به‌عنوان pass گزارش شوند:

- Learn baseline/action روی CAN واقعی
- duty-cycle و rate limiter روی firmware واقعی
- session/login behavior در runtime WebSocket/HTTP
- bench/vehicle validation

تست روی میز باید پیش از اتصال به CAN زنده انجام شود.

## 14. قراردادهای یکپارچگی runtime

- مسیر فایل‌های DBC داخلی با فایل‌های موجود در `data/dbc/` یکسان است.
- انتخاب پین CAN در زمان اجرا، پین‌های ثابت پروژه و محدوده‌ی GPIO مربوط به Octal Flash/PSRAM ماژول ESP32-S3 N16R8 را پیش از نصب TWAI رد می‌کند.
- وضعیت ماژول‌های Wi-Fi، Web Server، CAN، OBD-II، Touch، Display، BLE و Storage روی TFT و از طریق Web API/WebSocket احراز‌شده در دسترس است.
- شمارنده‌های CAN diagnostics و وضعیت bus هر ثانیه برای کلاینت‌های WebSocket احراز‌شده broadcast می‌شود.
- جایگزینی JSON پروفایل سفارشی با فایل‌های journal موقت/پشتیبان انجام می‌شود و هنگام راه‌اندازی بازیابی دارد.
- متن رابط وب برای اپراتور فقط انگلیسی است.
- نسخه‌ی firmware فقط یک منبع دارد: `CAR_TOUCH_FIRMWARE_VERSION`.

## 15. پیکربندی CAN در زمان اجرا

صفحه‌ی Settings در Web UI (پس از احراز هویت) پین‌های TX/RX، bitrate و حالت Listen-Only را در NVS ذخیره می‌کند. پس از ذخیره‌ی موفق دستگاه reboot می‌شود تا درایور TWAI فقط یک بار و با پیکربندی اعتبارسنجی‌شده نصب شود. اعتبارسنجی GPIO پین‌های سخت‌افزار پروژه، پین‌های Octal Flash/PSRAM در ESP32-S3 N16R8 و سایر پین‌های رزروشده یا حساس به strapping را رد می‌کند. bitrateهای classic CAN پشتیبانی‌شده: 100، 125، 250، 500، 800 و 1000 kbps.

## 16. Dual CAN

- `CANService` فراخوانی‌های موجود برنامه را برای سازگاری به CAN0/TWAI هدایت می‌کند و انتخاب صریح CAN1 را فراهم می‌کند؛ شکست راه‌اندازی MCP2515 مانع کار CAN0 نمی‌شود.
- CAN1 از MCP2515 با نوسان‌ساز 8 MHz و SPI مشترک با TFT/Touch (SCLK 12، MOSI 11، MISO 13) استفاده می‌کند. CS پیش‌فرض GPIO15 و INT پیش‌فرض GPIO16 است. هر باس bitrate، Listen-Only، شمارنده‌ها، وضعیت، diagnostics و bus-off recovery مستقل دارد.
- CAN1 به‌صورت پیش‌فرض Listen-Only است. OBD-II، Learn Mode، wake detection و فرمان‌های خودرو روی CAN0 می‌مانند. ارسال روی CAN1 فقط از فراخوانی صریح CAN1 service و در تست کنترل‌شده روی میز انجام شود.
- Settings احراز‌شده‌ی Web هر دو رابط را ذخیره می‌کند، تداخل GPIO را بررسی می‌کند و پیش از اعمال تغییر، reboot درخواست می‌کند. فیلدهای جدید به انتهای blob در NVS اضافه شده‌اند و پیکربندی با اندازه‌ی قدیمی، مقادیر امن پیش‌فرض CAN1 را می‌گیرد.
- درایور MCP2515 با نوسان‌ساز 8 MHz فقط bitrateهای 100، 125، 250، 500 و 1000 kbps را می‌پذیرد. فیلتر سخت‌افزاری per-ID و CAN log/replay ارائه نمی‌شود.
- باس فیزیکی، پلاریته‌ی interrupt، فرکانس کریستال و سطح منطقی ترنسیور باید روی ماژول واقعی تأیید شوند. GPIOهای ESP32-S3 مقاوم در برابر 5V نیستند؛ اگر برد MCP2515/TJA1050 سیگنال‌های SPI/INT با سطح 5V دارد یا RXD ترنسیور TJA1051 با 3.3V سازگار نیست، از level shifter استفاده کنید.
