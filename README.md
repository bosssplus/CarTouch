# CarTouch

CarTouch یک سامانه‌ی embedded مبتنی بر ESP32-S3 برای پایش و کنترل خودرو از طریق CAN Bus است. رابط اصلی دستگاه TFT لمسی است و یک Web Dashboard نیز از طریق Wi‑Fi در دسترس است.

> ⚠️ **هشدار ایمنی:** این پروژه مستقیماً با CAN Bus خودرو کار می‌کند. ارسال یک فریم اشتباه می‌تواند باعث رفتار ناخواسته‌ی خودرو شود. توسعه و آزمایش را ابتدا روی میز و در شرایط کنترل‌شده انجام دهید، حالت Listen-Only را تا زمان اطمینان فعال نگه دارید و هر فرمان کنترلی را فقط روی خودرویی که اختیار آزمایش آن را دارید اجرا کنید.

## وضعیت فعلی — نسخه 1.0.0

| قابلیت | وضعیت فعلی |
|---|---|
| خواندن OBD-II | پیاده‌سازی state-machine دارد و مسیر اصلی `loop()` از polling غیرمسدودکننده استفاده می‌کند |
| Dual CAN | CAN0 با TWAI و CAN1 با MCP2515 (کریستال 8 MHz)، وضعیت مستقل، Listen-Only، diagnostics و recovery دارد |
| Learn Mode | ضبط baseline/action، تشخیص candidate، ذخیره و چرخه‌ی تأیید دارد |
| فرمان‌های خودرو | از `ActiveProfileManager` برای انتخاب DBC یا پروفایل سفارشی استفاده می‌کند |
| پروفایل سفارشی | JSON روی SPIFFS، فهرست، import/export و مدیریت فرمان‌ها |
| تأیید فرمان | فرمان‌های یادگرفته‌شده تا زمان تأیید صریح کاربر اجرا نمی‌شوند |
| رابط TFT | LVGL، تب‌های Control/Dashboard/Learn/Settings و کالیبراسیون لمسی |
| Web Dashboard | HTTP API، WebSocket، احراز هویت، مدیریت پروفایل و OTA |
| امنیت نشست | توکن نشست WebSocket و ابطال نشست‌ها هنگام تغییر رمز |
| محافظت actuator | محدودیت فاصله‌ی فرمان و duty-cycle برای actuatorهای مکانیکی |
| لاگ خطا | ring buffer حافظه‌ای + شمارنده‌های پایدار NVS |
| خواب/بیداری | Auto Sleep و بیداری بر اساس فعالیت CAN |
| DBC | parser برای Intel و Motorola، با ذخیره‌سازی پویا برای سیگنال‌ها و سقف ایمنی 400 پیام در هر فایل |
| OTA | آپلود firmware و filesystem از طریق وب، پس از احراز هویت |
| BLE | وضعیت و BLE OTA با احراز هویت؛ کنترل‌های مدیریتی BLE فقط در محدوده‌ی پروتکل تعریف‌شده |
| Module Status | وضعیت runtime ماژول‌های Wi-Fi، Web، CAN، OBD، Touch، Display، BLE و Storage روی TFT و Web |
| CAN Diagnostics | وضعیت مستقل CAN0/CAN1، Listen-Only، شمارنده‌های RX/TX و خطاها از طریق WebSocket احراز‌شده |
| Battery Voltage | ولتاژ ECU فقط از OBD PID 0x42؛ مقدار ناموجود یا منقضی‌شده به‌شکل N/A نمایش داده می‌شود |
| Runtime CAN Settings | تنظیم پین/bitrate/mode هر دو باس از Web؛ validation تداخل و اعمال پس از reboot |
| CI | GitHub Actions برای firmware، filesystem، static analysis، native tests و بررسی اندازه‌ی image واقعی SPIFFS |

### محدودیت‌های فعلی

- HTTPS در وب‌سرور فعال نیست؛ ترافیک شبکه رمزنگاری نمی‌شود.
- Secure Boot و Flash Encryption در تنظیمات پروژه فعال نشده‌اند.
- Pairing در BLE از نوع Just Works است (بدون حفاظت MITM)؛ لینک رمزنگاری می‌شود ولی هویت طرف مقابل در زمان pairing تأیید نمی‌شود. جزئیات در `BLE_OTA.md`.
- موفقیت `twai_driver_install()` یا `twai_start()` به‌تنهایی اتصال فیزیکی ترنسیور و سیم‌کشی صحیح CAN را ثابت نمی‌کند.
- ولتاژ ECU از پاسخ PID `0x42` می‌آید و فقط در بازه‌ی ۶ تا ۳۶ ولت نمایش داده می‌شود؛ ورودی ADC مستقل، calibration و محاسبه‌ی درصد شارژ در نسخه‌ی فعلی وجود ندارد.
- اگر SPIFFS mount نشود، سیستم آن را خودکار format نمی‌کند؛ فایل‌های سفارشی حفظ می‌شوند و قابلیت‌های وابسته به filesystem تا رفع مشکل غیرفعال خواهند بود.
- DBCهای Extended با پرچم `isExtended` نگهداری می‌شوند؛ parser علاوه بر DBC استاندارد، فایل‌هایی را که شناسه 29 بیتی را بدون bit 31 ذخیره کرده‌اند نیز تشخیص می‌دهد.
- Learn Mode برای خودروهایی که فرمان‌های کنترلی آن‌ها rolling code یا مکانیزم‌های مشابه دارند، تضمین‌شده نیست.
- parser فعلی DBC یک فایل را برای هر انتخاب خودرو بارگذاری می‌کند؛ فایل‌هایی که برای ترکیب چند DBC طراحی شده‌اند به‌صورت خودکار merge نمی‌شوند.
- تنظیمات CAN0/CAN1 از پین‌های رزروشده و تداخل بین GPIOها محافظت می‌کند؛ تغییرها پس از ذخیره‌سازی و reboot اعمال می‌شوند. MCP2515 فعلی clock برابر 8 MHz و bitrateهای 100، 125، 250، 500 و 1000 kbps را می‌پذیرد.
- برای فایل‌های DBC بسیار بزرگ‌تر از پروفایل‌های مستقیم، سقف ایمنی 400 پیام اعمال می‌شود؛ فایل‌های چندمنبعی همچنان نیازمند merge چندفایلی هستند.
- در MVP، `CM_` و `VAL_` در DBC تفسیر نمی‌شوند و parser آن‌ها را به‌عنوان داده‌ی قابل استفاده ثبت نمی‌کند.
- OBD در این MVP فقط پاسخ‌های ISO-TP Single Frame را برای مسیرهای فعلی پردازش می‌کند؛ Multi-Frame پشتیبانی نمی‌شود.
- DTC در لایه‌ی OBD وجود دارد اما UI برای نمایش/مدیریت DTC در Web/TFT ارائه نشده است.
- بیلد CI باید معیار اصلی صحت کامپایل باشد؛ اجرای واقعی روی خودرو همچنان نیازمند سخت‌افزار و آزمون کنترل‌شده است.

## سخت‌افزار هدف

- ESP32-S3 DevKitC-1 / ماژول N16R8
- Flash: 16 MB
- PSRAM: 8 MB
- ترنسیور CAN سه‌ولت مانند SN65HVD230
- TFT با ILI9341 و Touch Controller از نوع XPT2046
- ورودی تغذیه‌ی خودرو با مبدل مناسب به 3.3V

### سیم‌بندی پیش‌فرض

| سیگنال | GPIO |
|---|---:|
| CAN0 / TWAI TX | 9 |
| CAN0 / TWAI RX | 6 |
| TFT CS | 10 |
| TFT DC | 7 |
| TFT RST | 4 |
| SPI MOSI | 11 |
| SPI MISO | 13 |
| SPI SCLK | 12 |
| TFT Backlight | 21 |
| Touch CS | 14 |
| CAN1 / MCP2515 CS | 15 |
| CAN1 / MCP2515 INT | 16 |

پین‌های نهایی را همیشه از `src/config.h` و `platformio.ini` بررسی کنید.

CAN1 از کریستال 8 MHz روی MCP2515 و SPI مشترک با TFT/Touch استفاده می‌کند. CS و INT قابل تنظیم هستند و پیش‌فرض آن‌ها GPIO15 و GPIO16 است. زمین ماژول‌ها باید مشترک باشد. سطح منطقی TJA1051 به variant برد بستگی دارد؛ خروجی RXD نباید بیش از 3.3V به ESP32-S3 بدهد. بسیاری از بردهای MCP2515/TJA1050 با منطق 5V کار می‌کنند؛ در نبود level shifter روی خود برد، برای SCK/MOSI/CS و نیز MISO/INT مبدل سطح مناسب قرار دهید. هیچ سیگنال 5V را مستقیم به GPIO متصل نکنید.

## راه‌اندازی توسعه

پیش‌نیازها:

- Git
- VS Code
- PlatformIO

ساخت firmware:

```bash
pio run -e esp32-s3-devkitc-1
```

ساخت filesystem:

```bash
pio run -e esp32-s3-devkitc-1 -t buildfs
```

آپلود firmware:

```bash
pio run -e esp32-s3-devkitc-1 -t upload
```

آپلود filesystem:

```bash
pio run -e esp32-s3-devkitc-1 -t uploadfs
```

مانیتور سریال:

```bash
pio device monitor
```

بررسی static analysis:

```bash
pio check -e esp32-s3-devkitc-1 --skip-packages
```

اجرای تست‌های native: 

```bash
pio test -e native
```

## اولین راه‌اندازی

1. دستگاه را روشن کنید.
2. در صورت نیاز، کالیبراسیون لمسی را انجام دهید.
3. به Access Point دستگاه وصل شوید یا تنظیمات Station را انجام دهید.
4. آدرس وبی نمایش‌داده‌شده در Serial Monitor را باز کنید.
5. با حساب مدیریتی وارد شوید.
6. رمز پیش‌فرض را بلافاصله تغییر دهید.
7. برای آزمایش CAN ابتدا Listen-Only را نگه دارید.
8. قبل از فعال‌کردن فرمان‌های کنترلی، پروفایل خودرو را انتخاب یا ایجاد کنید و هر فرمان یادگرفته‌شده را جداگانه تأیید کنید.

## ساختار پروژه

```text
CarTouch/
├── .github/workflows/CarTouch-build.yml
├── .gitignore
├── data/
│   ├── index.html
│   ├── app.js
│   ├── style.css
│   └── dbc/
├── src/
│   ├── main.cpp
│   ├── config.cpp/.h
│   ├── can_manager.cpp/.h
│   ├── obd2_reader.cpp/.h
│   ├── vehicle_control.cpp/.h
│   ├── vehicle_db.cpp/.h
│   ├── tft_ui.cpp/.h
│   ├── webserver.cpp/.h
│   ├── wifi_manager.cpp/.h
│   ├── ble_manager.cpp/.h
│   ├── module_status.cpp/.h
│   ├── custom_vehicle.h
│   ├── custom_vehicle_store.cpp/.h
│   ├── learn_engine.cpp/.h
│   ├── active_profile_manager.cpp/.h
│   ├── error_log.cpp/.h
│   ├── ct_hex_parser.h
│   ├── ct_obd_parser.h
│   ├── ct_dbc_validation.h
│   ├── ct_json_validation.h
│   ├── ct_verify.h
│   ├── ct_time.h
│   └── ct_tx_guard.h
├── platformio.ini
├── cartouch_16MB.csv
├── CarTouch_SPEC.md
├── BLE_OTA.md
├── CHANGES.txt
├── ROADMAP.md
├── LICENSE
├── test/test_native/test_main.cpp
└── README.md
```

`data/` در زمان build به filesystem دستگاه تبدیل می‌شود. پروفایل‌های سفارشی در زمان اجرا ساخته می‌شوند و بخشی از سورس اولیه نیستند.

## معماری

جریان اصلی نرم‌افزار به‌صورت زیر است:

```text
TFT / Web
   │
   ├── Configuration
   ├── Vehicle selection
   ├── Learn / Verify
   └── Control
          │
          ▼
ActiveProfileManager
   ├── Built-in DBC profile
   └── Custom learned profile
          │
          ▼
VehicleControl
          │
          ▼
CANManager / TWAI
          │
          ▼
CAN Bus

CAN Bus ──► CANManager ──► OBD2Reader / LearnEngine / UI diagnostics
```

جزئیات معماری، قراردادهای بین ماژول‌ها و محدودیت‌های ایمنی در `CarTouch_SPEC.md` نگهداری می‌شود.

## DBC

فایل‌های DBC موجود در `data/dbc/` داده‌ی ورودی سیستم هستند و metadata داخلی خودشان را حفظ می‌کنند. `VehicleDB` فقط فایل‌هایی را که برای انتخاب مستقیم مناسب تشخیص داده شده‌اند به فهرست خودروها متصل می‌کند؛ فایل‌های دیگر ممکن است برای merge چندمنبعی، ADAS/radar یا ساختارهای خاص نگهداری شده باشند.

اطلاعات `VERSION` داخل یک فایل DBC بخشی از قالب داده‌ی همان DBC است و با نسخه‌ی پروژه یا release metadata اشتباه گرفته نشود.

## امنیت

- Listen-Only باید نقطه‌ی شروع آزمایش CAN باشد.
- فرمان‌های Learn Mode ابتدا به‌صورت تأییدنشده ذخیره می‌شوند.
- ارسال فرمان آزمایشی فقط از مسیر تأیید صریح انجام می‌شود.
- WebSocket بدون session token معتبر پذیرفته نمی‌شود.
- تغییر رمز باید نشست‌های قبلی را بی‌اعتبار کند.
- OTA فقط از مسیر احراز هویت‌شده در دسترس است.
- رمزهای پیش‌فرض در یک نصب واقعی باید فوراً تغییر کنند.
- نبود HTTPS یعنی session و سایر داده‌های وب در شبکه‌ی محلی محرمانگی TLS ندارند.

## وضعیت Runtime و ماژول‌ها

وضعیت runtime Wi-Fi، Web Server، CAN Bus، OBD-II، Touch، Display، BLE و Storage روی TFT و Web UI نمایش داده می‌شود. وضعیت‌ها شامل `INITIALIZING`، `READY`، `NOT DETECTED`، `ERROR` و `DISABLED` هستند. CAN diagnostics شامل شمارنده‌های RX/TX، خطا و وضعیت bus است. قطع یک ماژول نباید boot کل دستگاه را متوقف کند.

## OTA و BLE

OTA برای firmware و filesystem از Web UI فعال است. BLE مستقل از Wi-Fi اجرا می‌شود و status و BLE OTA دارد؛ کنترل مدیریتی BLE محدود به پروتکل تعریف‌شده است. نسخه firmware از `CAR_TOUCH_FIRMWARE_VERSION` در `src/config.h` می‌آید.

## مستندات

- `CarTouch_SPEC.md` — مشخصات فنی و معماری فعلی
- `ROADMAP.md` — تنها roadmap رسمی پروژه و بر اساس وضعیت فعلی
- `BLE_OTA.md` — پروتکل BLE و BLE OTA
- `CHANGES.txt` — خلاصه‌ی قابلیت‌های نسخه‌ی فعلی
- `README.md` — راه‌اندازی، وضعیت و محدودیت‌های فعلی

## مجوز

MIT — see `LICENSE`.
