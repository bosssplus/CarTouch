<div align="center">

<h1 dir="rtl" id="-cartouch">🚗 CarTouch</h1>

**کنترل و مانیتورینگ خودرو با ESP32-S3 و CAN Bus — لمسی، وب، و یادگیرنده**

![CarTouch](https://img.shields.io/badge/CarTouch-v2.0-blue)
![ESP32-S3](https://img.shields.io/badge/ESP32--S3-N16R8-green)
![CAN Bus](https://img.shields.io/badge/CAN-500Kbps-orange)
![LVGL](https://img.shields.io/badge/LVGL-8.4-purple)
![License](https://img.shields.io/badge/License-MIT-lightgrey)

</div>

<div dir="rtl">

<div class="markdown-alert markdown-alert-warning" dir="rtl">
<p class="markdown-alert-title">Warning</p>
<p>‏CarTouch روی <b>CAN Bus زنده‌ی خودرو</b> کار می‌کند. یک فرمان اشتباه می‌تواند رفتار غیرمنتظره‌ای در خودرو ایجاد کند. اول روی میز تست کار کنید، حالت <b>Listen-Only</b> را فعال نگه دارید تا مطمئن شوید، و فقط روی خودروی خودتان و با مسئولیت خودتان از آن استفاده کنید.</p>
</div>

<h2 dir="rtl" id="-فهرست">📑 فهرست</h2>

<ol dir="rtl">
<li><a href="#-معرفی">معرفی</a></li>
<li><a href="#-وضعیت-و-محدودیتها">وضعیت و محدودیت‌ها</a></li>
<li><a href="#-learn-mode--یادگیری-فرمان-از-خودرو">Learn Mode — یادگیری فرمان از خودرو</a></li>
<li><a href="#-قطعات-مورد-نیاز">قطعات مورد نیاز</a></li>
<li><a href="#-سیمبندی">سیم‌بندی</a></li>
<li><a href="#-نصب-و-راهاندازی">نصب و راه‌اندازی</a></li>
<li><a href="#-اولین-اجرا">اولین اجرا</a></li>
<li><a href="#-ساختار-پروژه">ساختار پروژه</a></li>
<li><a href="#-مستندات-بیشتر">مستندات بیشتر</a></li>
</ol>

---

<h2 dir="rtl" id="-معرفی">✨ معرفی</h2>

‏**CarTouch** یک دستگاه کوچک با **ESP32-S3** است که از طریق OBD-II یا مستقیم به CAN Bus خودرو وصل می‌شود و اطلاعات خودرو را نشان می‌دهد و (در صورت تأیید شما) فرمان‌هایی مثل قفل و شیشه را ارسال می‌کند.

<table dir="rtl">
<tr>
<td align="center"><b></b></td>
<td align="center"><b>قابلیت</b></td>
</tr>
<tr>
<td align="center">🎮 <b>کنترل</b></td>
<td align="center">صفحه‌ی لمسی TFT داخل خودرو (روش اصلی) + Web Dashboard از طریق WiFi (روش مکمل)</td>
</tr>
<tr>
<td align="center">🔐 <b>فرمان‌ها</b></td>
<td align="center">قفل/باز کردن ۴ درب و صندوق، ۴ شیشه + سانروف، آینه‌ها، دزدگیر</td>
</tr>
<tr>
<td align="center">📊 <b>مانیتورینگ</b></td>
<td align="center">سرعت، RPM، دمای موتور، ولتاژ باطری، سطح سوخت + خواندن کدهای خطا (DTC) — به‌صورت <b>غیرمسدودکننده</b> (نگاه کنید به وضعیت زیر)</td>
</tr>
<tr>
<td align="center">🎓 <b>یادگیری</b></td>
<td align="center">ضبط فرمان از دکمه‌های فیزیکی واقعی خودرو، یا ورود دستی CAN ID/بایت</td>
</tr>
<tr>
<td align="center">✅ <b>چرخه‌ی تأیید</b></td>
<td align="center">هر فرمان یادگرفته/دستی تا تأیید صریح شما «تأییدنشده» است و اجرا نمی‌شود</td>
</tr>
<tr>
<td align="center">💾 <b>دیتابیس شخصی</b></td>
<td align="center">ذخیره، بارگذاری و اشتراک‌گذاری پروفایل خودروهای دست‌ساز به‌صورت JSON</td>
</tr>
<tr>
<td align="center">🛡️ <b>ایمنی</b></td>
<td align="center">حالت Listen-Only (پیش‌فرض روشن)، محدودیت نرخ فرمان، اجبار تغییر رمز پیش‌فرض، Watchdog، توکن نشست WebSocket با همگام‌سازی بین TFT و وب</td>
</tr>
<tr>
<td align="center">🔋 <b>انرژی</b></td>
<td align="center">Auto Sleep بعد از ۱۰ دقیقه بی‌فعالیتی + بیدار شدن با پیام CAN</td>
</tr>
<tr>
<td align="center">🌗 <b>نمایش</b></td>
<td align="center">حالت شب/روز خودکار یا دستی + کالیبراسیون واقعی تاچ‌اسکرین</td>
</tr>
<tr>
<td align="center">⬆️ <b>به‌روزرسانی</b></td>
<td align="center">OTA (Over-The-Air) از طریق وب — بدون نیاز به اتصال کابل مجدد</td>
</tr>
</table>

---

<h2 dir="rtl" id="-وضعیت-و-محدودیتها">🧭 وضعیت و محدودیت‌ها</h2>

قبل از شروع بدانید چه چیزی آماده است و چه چیزی نه:

<table dir="rtl">
<tr>
<td align="center"><b>موضوع</b></td>
<td align="center"><b>وضعیت</b></td>
</tr>
<tr>
<td align="center">خواندن OBD-II استاندارد (RPM، سرعت، دما، …)</td>
<td align="center">✅ آماده — پیاده‌سازی <b>غیرمسدودکننده</b> (state machine در <code>OBD2Reader::update()</code>)؛ <code>loop()</code> اصلی هرگز روی پاسخ ECU مسدود نمی‌شود</td>
</tr>
<tr>
<td align="center">Task Watchdog</td>
<td align="center">✅ فعال (پیش‌فرض ۸ ثانیه) — در صورت گیر کردن هر بخشی از <code>loop()</code>، دستگاه به‌جای هنگ دائمی ری‌ست می‌شود</td>
</tr>
<tr>
<td align="center">Learn Mode و ورود دستی</td>
<td align="center">✅ پیاده‌سازی شده — روی خودروهای دارای <b>rolling code</b> (معمولاً بعد از ۲۰۱۸ و برندهای پریمیوم) کار نمی‌کند</td>
</tr>
<tr>
<td align="center">فرمان قفل/شیشه از روی فایل DBC</td>
<td align="center">❌ <b>پشتیبانی نمی‌شود.</b> فایل‌های OpenDBC عمدتاً «خواندنی» هستند؛ برای فرمان از Learn Mode یا ورود دستی استفاده کنید</td>
</tr>
<tr>
<td align="center">فایل‌های DBC</td>
<td align="center">۵۷ فایل همراه پروژه است؛ <b>۳۸ مدل</b> (تویوتا، بی‌ام‌و، هوندا/اکورا، جی‌ام/کادیلاک، کرایسلر/FCA، فورد، هیوندای، مزدا، مرسدس، نیسان، اوپل، PSA، ولوو، فولکس‌واگن، تسلا، ریویان و چند برند دیگر) + حالت عمومی OBD-II در لیست انتخاب وصل شده‌اند. فهرست کامل مدل‌های وصل‌نشده و دلیل هرکدام در کامنت‌های ابتدای <code>VehicleDB::begin()</code> (<code>src/vehicle_db.cpp</code>) مستند است</td>
</tr>
<tr>
<td align="center">پارسر DBC</td>
<td align="center">سیگنال‌های <b>Intel (<code>@1</code>) و Motorola (<code>@0</code>) هر دو پشتیبانی می‌شوند</b> (نگاشت بیت جداگانه برای هرکدام). سقف <b>۱۵۰ پیام</b> برای هر فایل (چند فایل بسیار حجیم مثل <code>ford_lincoln_base_pt.dbc</code> هنوز به‌عمد به لیست وصل نشده‌اند — به همان کامنت بالا مراجعه کنید)</td>
</tr>
<tr>
<td align="center">Listen-Only</td>
<td align="center">پیش‌فرض <b>روشن</b> است (<code>listenOnlyMode = true</code>). این حالت فقط <b>نرم‌افزاری</b> است (فیلتر سطح برنامه، نه enforcement سخت‌افزاری واقعی درایور TWAI)</td>
</tr>
<tr>
<td align="center">کالیبراسیون تاچ</td>
<td align="center">✅ کالیبراسیون <b>واقعی و تعاملی</b> (۵ نقطه، با <code>TFT_eSPI::calibrateTouch()</code>) در اولین بوت اجرا می‌شود و نتیجه در حافظه‌ی غیرفرار ذخیره می‌شود؛ از تب تنظیمات هم قابل تکرار است</td>
</tr>
<tr>
<td align="center">Session وب/TFT</td>
<td align="center">✅ تغییر رمز از هر مسیری (صفحه‌ی لمسی یا وب) بلافاصله تمام نشست‌های وب (HTTP و WebSocket) را باطل می‌کند</td>
</tr>
<tr>
<td align="center">OTA (به‌روزرسانی بی‌سیم)</td>
<td align="center">✅ پیاده‌سازی شده — مسیر <code>/update</code> در وب‌سرور، آپلود فایل <code>.bin</code> فریمویر یا ایمیج SPIFFS</td>
</tr>
<tr>
<td align="center">حافظه‌ی داخلی (DRAM)</td>
<td align="center">آبجکت‌های حجیم (<code>vehicleDB</code> و وابسته‌هایش) به‌صورت <b>پوینتر با <code>new</code> در <code>setup()</code></b> ساخته می‌شوند تا از سرریز DRAM هنگام لینک جلوگیری شود؛ روی هسته‌های Arduino-ESP32 با PSRAM فعال، تخصیص‌های بزرگ به‌طور خودکار از PSRAM سرو می‌شوند</td>
</tr>
<tr>
<td align="center">HTTPS</td>
<td align="center">ندارد — ترافیک وب رمزنگاری نمی‌شود</td>
</tr>
<tr>
<td align="center">Secure Boot / Flash Encryption</td>
<td align="center">ندارد</td>
</tr>
<tr>
<td align="center">تست روی سخت‌افزار واقعی</td>
<td align="center">تغییرات اخیر (لیست بالا) به‌صورت منطقی/دستی بررسی شده‌اند، ولی هنوز با toolchain واقعی PlatformIO کامپایل و روی خودروی واقعی تست نشده‌اند</td>
</tr>
</table>

> کارکرد نهایی روی خودروی واقعی فقط با تست خودتان روی سخت‌افزار قابل تأیید است.
> فهرست کامل و به‌روز مشکلات شناخته‌شده: [`CHANGELOG.md`](./CHANGELOG.md)
> وضعیت گام‌به‌گام اصلاحات نسخه‌ی تجاری: [`PROGRESS_CHECKLIST.md`](./PROGRESS_CHECKLIST.md)

---

<h2 dir="rtl" id="-learn-mode--یادگیری-فرمان-از-خودرو">🎓 Learn Mode — یادگیری فرمان از خودرو</h2>

هر خودرو فرمان‌های کنترلی مخصوص خودش را دارد که در هیچ دیتابیس عمومی نیست. Learn Mode با شنود امن این مشکل را حل می‌کند:

```mermaid
flowchart LR
    A["انتخاب برچسب<br/>مثلاً قفل همه درب‌ها"] --> B["۲ ثانیه شنود پیش‌زمینه<br/>بدون ارسال هیچ فرمانی"]
    B --> C["دکمه‌ی واقعی خودرو<br/>را می‌زنید"]
    C --> D["۲ ثانیه ضبط<br/>و مقایسه با پیش‌زمینه"]
    D --> E["انتخاب کاندید درست"]
    E --> F["ذخیره ⚠️<br/>تأییدنشده"]
    F --> G["تأیید صریح<br/>و یک بار آزمایش"]
    G --> H["✅ فعال در تب کنترل"]
```

<ol dir="rtl">
<li>دستگاه را به CAN Bus وصل کنید (بخش <a href="#-سیمبندی">سیم‌بندی</a>).</li>
<li>از تب «یادگیری» (روی TFT یا وب) یک برچسب فرمان انتخاب کنید.</li>
<li>دستگاه ۲ ثانیه پیام‌های پیش‌زمینه را می‌شنود — <b>بدون ارسال هیچ فرمانی</b>.</li>
<li>دکمه‌ی فیزیکی خودرو را بزنید؛ دستگاه ۲ ثانیه بعدی را ضبط می‌کند.</li>
<li>از میان کاندیدهای یافت‌شده، درست را انتخاب کنید.</li>
<li>فرمان با وضعیت «تأییدنشده» (⚠️) ذخیره می‌شود و <b>قابل اجرا نیست</b>.</li>
<li>با یک تأیید جداگانه (که CAN ID و بایت‌های واقعی را نشان می‌دهد) آن را یک‌بار آزمایش و نتیجه را دستی تأیید می‌کنید.</li>
<li>فقط بعد از این مرحله، فرمان در تب کنترل اصلی فعال می‌شود.</li>
</ol>

> تشخیص کاندیدها نیمه‌خودکار است و تأیید نهایی همیشه با شماست. دستگاه هرگز فرمان آزمایشی یا تصادفی روی باس نمی‌فرستد.
> معماری، الگوریتم و قوانین ایمنی: [`CarTouch_SPEC.md`](./CarTouch_SPEC.md)

---

<h2 dir="rtl" id="-قطعات-مورد-نیاز">🔩 قطعات مورد نیاز</h2>

<table dir="rtl">
<tr>
<td align="center"><b>قطعه</b></td>
<td align="center"><b>تعداد</b></td>
<td align="center"><b>توضیحات</b></td>
</tr>
<tr>
<td align="center"><b>ESP32-S3 DevKit</b> (ماژول N16R8)</td>
<td align="center">۱</td>
<td align="center">فلش ۱۶MB + PSRAM هشت مگابایتی؛ دارای کنترلر CAN (TWAI)</td>
</tr>
<tr>
<td align="center"><b>SN65HVD230</b></td>
<td align="center">۱</td>
<td align="center">ترنسیور CAN ۳٫۳ ولت</td>
</tr>
<tr>
<td align="center"><b>نمایشگر TFT با کنترلر ILI9341</b></td>
<td align="center">۱</td>
<td align="center">SPI، رزولوشن ۲۴۰×۳۲۰، ۲٫۴ تا ۲٫۸ اینچ، همراه تاچ XPT2046 (روی همان ماژول)</td>
</tr>
<tr>
<td align="center"><b>مبدل 12V به 3.3V</b></td>
<td align="center">۱</td>
<td align="center">حداقل ۱ آمپر خروجی</td>
</tr>
<tr>
<td align="center"><b>کابل / کانکتور OBD-II</b></td>
<td align="center">۱</td>
<td align="center">اختیاری (اگر مستقیم به سیم‌های CAN وصل نمی‌شوید)</td>
</tr>
<tr>
<td align="center">سیم jumper و محفظه</td>
<td align="center">—</td>
<td align="center">برای اتصال و نصب داخل خودرو</td>
</tr>
</table>

<div class="markdown-alert markdown-alert-important" dir="rtl">
<p class="markdown-alert-title">Important</p>
<p>پروژه برای پنل <b>ILI9341</b> تنظیم شده است (<code>-DILI9341_DRIVER=1</code> در <code>platformio.ini</code>). اگر پنل شما ST7796 (یا کنترلر دیگری) است، درایور را در <code>platformio.ini</code> عوض کنید و ابعاد رابط کاربری را هم با آن تطبیق دهید — این دو کنترلر با هم سازگار نیستند.</p>
</div>

---

<h2 dir="rtl" id="-سیمبندی">🔌 سیم‌بندی</h2>

<details open>
<summary><b>نمایشگر TFT و تاچ ← ESP32-S3</b></summary>

<table dir="rtl">
<tr>
<td align="center"><b>پین ماژول</b></td>
<td align="center"><b>پین ESP32-S3</b></td>
<td align="center"><b>توضیحات</b></td>
</tr>
<tr>
<td align="center">CS</td>
<td align="center">GPIO10</td>
<td align="center">Chip Select</td>
</tr>
<tr>
<td align="center">DC</td>
<td align="center">GPIO7</td>
<td align="center">Data/Command</td>
</tr>
<tr>
<td align="center">RST</td>
<td align="center">GPIO4</td>
<td align="center">Reset</td>
</tr>
<tr>
<td align="center">MOSI</td>
<td align="center">GPIO11</td>
<td align="center">SPI Data In</td>
</tr>
<tr>
<td align="center">SCLK</td>
<td align="center">GPIO12</td>
<td align="center">SPI Clock</td>
</tr>
<tr>
<td align="center">MISO</td>
<td align="center">GPIO13</td>
<td align="center">SPI Data Out</td>
</tr>
<tr>
<td align="center">BL</td>
<td align="center">GPIO21</td>
<td align="center">Backlight (PWM)</td>
</tr>
<tr>
<td align="center">T_CS</td>
<td align="center">GPIO14</td>
<td align="center">Touch Chip Select</td>
</tr>
<tr>
<td align="center">T<i>DIN / T</i>DOUT / T_CLK</td>
<td align="center">GPIO11 / GPIO13 / GPIO12</td>
<td align="center">اشتراکی با SPI نمایشگر (فقط CS تاچ جداست)</td>
</tr>
<tr>
<td align="center">VCC / GND</td>
<td align="center">3.3V / GND</td>
<td align="center">تغذیه و زمین مشترک</td>
</tr>
</table>

</details>

<details open>
<summary><b>ماژول CAN (SN65HVD230) ← ESP32-S3</b></summary>

<table dir="rtl">
<tr>
<td align="center"><b>پین ماژول</b></td>
<td align="center"><b>پین ESP32-S3</b></td>
<td align="center"><b>توضیحات</b></td>
</tr>
<tr>
<td align="center">VCC / GND</td>
<td align="center">3.3V / GND</td>
<td align="center">تغذیه و زمین مشترک</td>
</tr>
<tr>
<td align="center">CTX</td>
<td align="center">GPIO9</td>
<td align="center">CAN Transmit</td>
</tr>
<tr>
<td align="center">CRX</td>
<td align="center">GPIO6</td>
<td align="center">CAN Receive (به‌جای GPIO10 تا با TFT_CS تداخل نکند)</td>
</tr>
</table>

> باس خودرو از قبل ترمینیت (۱۲۰Ω) شده است؛ مقاومت ترمینیشن ماژول را فعال **نکنید** (فقط روی میز تست دو-نودی لازم است).

</details>

<details open>
<summary><b>اتصال به CAN Bus خودرو</b></summary>

**روش اول — از طریق OBD-II (توصیه‌شده):**

<table dir="rtl">
<tr>
<td align="center"><b>پین OBD-II</b></td>
<td align="center"><b>علامت</b></td>
<td align="center"><b>اتصال</b></td>
</tr>
<tr>
<td align="center">6</td>
<td align="center">CAN-H</td>
<td align="center">CAN-H ماژول SN65HVD230</td>
</tr>
<tr>
<td align="center">14</td>
<td align="center">CAN-L</td>
<td align="center">CAN-L ماژول SN65HVD230</td>
</tr>
<tr>
<td align="center">4</td>
<td align="center">GND</td>
<td align="center">زمین</td>
</tr>
<tr>
<td align="center">16</td>
<td align="center">+12V</td>
<td align="center">ورودی مبدل ولتاژ (تغذیه)</td>
</tr>
</table>

پورت OBD-II معمولاً زیر فرمان یا کنار جعبه‌فیوز است. پین ۱۶ همیشه برق دارد؛ Auto Sleep برای همین در نظر گرفته شده است.

**روش دوم — اتصال مستقیم به سیم‌های CAN (فقط برای متخصصان):** رنگ سیم‌های CAN بین خودروها فرق می‌کند؛ حتماً از نقشه‌ی سیم‌کشی همان مدل استفاده کنید.

<div class="markdown-alert markdown-alert-caution" dir="rtl">
<p class="markdown-alert-title">Caution</p>
<p>قبل از قطع یا لحیم‌کاری روی سیم‌ها باتری خودرو را جدا کنید. جابه‌جا وصل کردن CAN-H و CAN-L می‌تواند به ماژول آسیب بزند.</p>
</div>

</details>

<details>
<summary><b>تغذیه از 12V خودرو</b></summary>

مبدل باید حداقل **۱ آمپر** جریان خروجی 3.3V داشته باشد (نمایشگر و بک‌لایت مصرف قابل‌توجهی دارند).

</details>

---

<h2 dir="rtl" id="-نصب-و-راهاندازی">🚀 نصب و راه‌اندازی</h2>

**پیش‌نیازها:** [VS Code](https://code.visualstudio.com/) + افزونه‌ی PlatformIO + Git

```bash
git clone https://github.com/bosssplus/CarTouch.git
cd CarTouch

pio run -t upload      # کامپایل و آپلود فریمویر
pio run -t uploadfs    # آپلود فایل‌های وب و DBC به SPIFFS
pio device monitor     # مانیتور سریال (115200)
```

<div class="markdown-alert markdown-alert-note" dir="rtl">
<p class="markdown-alert-title">Note</p>
<p>پوشه‌ی <code>data/</code> حدود <b>۳٫۵ مگابایت</b> است. جدول پارتیشن پروژه (<code>default_16MB.csv</code>) برای این حجم روی فلش ۱۶ مگابایتی طراحی شده. اگر <code>uploadfs</code> خطای «حجم بیش‌ازحد» داد، DBC های غیرضروری را از <code>data/dbc/</code> حذف کنید (فقط فایل‌های وایرشده در <code>VehicleDB::begin()</code> — نگاه کنید به <code>src/vehicle_db.cpp</code> — واقعاً لازم‌اند) یا جدول پارتیشن را با فضای SPIFFS بزرگ‌تر تعریف کنید.</p>
</div>

<div class="markdown-alert markdown-alert-note" dir="rtl">
<p class="markdown-alert-title">Note</p>
<p>‏⚠️ <b>وضعیت کامپایل:</b> آخرین دور اصلاحات (بازنویسی OBD2Reader، پارسر DBC، مدیریت حافظه‌ی <code>vehicleDB</code>) به‌صورت دستی/منطقی بررسی شده، اما با toolchain واقعی PlatformIO کامپایل نشده است. قبل از فلش روی سخت‌افزار واقعی، حتماً یک‌بار <code>pio run</code> را خودتان اجرا کنید.</p>
</div>

**بیلد خودکار:** فایل `.github/workflows/CarTouch-build.yml` با هر push و Pull Request فریمویر و ایمیج SPIFFS را می‌سازد و فایل‌های `.bin` را ۳۰ روز به‌عنوان Artifact نگه می‌دارد.

<details>
<summary><b>کتابخانه‌ها (خودکار توسط PlatformIO نصب می‌شوند)</b></summary>

<table dir="rtl">
<tr>
<td align="center"><b>کتابخانه</b></td>
<td align="center"><b>نسخه</b></td>
<td align="center"><b>مجوز</b></td>
<td align="center"><b>کاربرد</b></td>
</tr>
<tr>
<td align="center">TFT_eSPI</td>
<td align="center">≥ 2.5.43</td>
<td align="center">BSD</td>
<td align="center">درایور نمایشگر + کالیبراسیون تاچ</td>
</tr>
<tr>
<td align="center">lvgl</td>
<td align="center">8.4.x</td>
<td align="center">MIT</td>
<td align="center">رابط گرافیکی</td>
</tr>
<tr>
<td align="center">ESPAsyncWebServer</td>
<td align="center">≥ 3.7.0</td>
<td align="center">LGPL</td>
<td align="center">وب سرور Async + OTA</td>
</tr>
<tr>
<td align="center">AsyncTCP</td>
<td align="center">≥ 3.3.0</td>
<td align="center">LGPL</td>
<td align="center">TCP Async</td>
</tr>
<tr>
<td align="center">ArduinoJson</td>
<td align="center">≥ 7.2.0</td>
<td align="center">MIT</td>
<td align="center">کار با JSON</td>
</tr>
</table>

</details>

---

<h2 dir="rtl" id="-اولین-اجرا">🔑 اولین اجرا</h2>

<ol dir="rtl">
<li>دستگاه را روشن کنید و به WiFi با نام <b><code>CarTouch</code></b> وصل شوید (رمز موقت: <code>12345678</code>).</li>
<li>در همین اولین بوت، اگر تاچ‌اسکرین قبلاً کالیبره نشده باشد، یک ویزارد کالیبراسیون ۵-نقطه‌ای روی صفحه ظاهر می‌شود — نقاط چشمک‌زن را لمس کنید.</li>
<li>در مرورگر آدرسی که در Serial Monitor چاپ می‌شود را باز کنید (معمولاً <code>192.168.4.1</code>).</li>
<li>با نام کاربری <code>admin</code> و رمز موقت <code>cartouch</code> وارد شوید.</li>
<li><b>همین اول رمز را عوض کنید</b> (حداقل ۸ کاراکتر، متفاوت از رمز پیش‌فرض) — از تب تنظیمات وب یا از صفحه‌ی لمسی. تا تغییر ندهید، دستگاه هر بار هشدار نشان می‌دهد. تغییر رمز از هر مسیر (وب یا لمسی)، بلافاصله نشست‌های باز در مسیر دیگر را هم باطل می‌کند.</li>
</ol>

> رمزهای موقت در ریپوی عمومی دیده می‌شوند؛ آن‌ها را رمز نهایی حساب نکنید. رمز WiFi دستگاه (Access Point) را می‌توانید قبل از فلش با `WIFI_AP_PASSWORD` در `src/config.h` عوض کنید.

پیام‌های WebSocket روی همان پورت ۸۰ (مسیر `/ws`) و بعد از احراز هویت با توکن نشست کار می‌کنند.

---

<h2 dir="rtl" id="-ساختار-پروژه">📂 ساختار پروژه</h2>

<div dir="ltr">

```
CarTouch/
├── platformio.ini                # تنظیمات PlatformIO، پین‌ها و کتابخانه‌ها
├── README.md                     # همین فایل
├── CHANGELOG.md                  # تاریخچه تغییرات + مشکلات شناخته‌شده
├── CarTouch_SPEC.md              # مشخصات فنی کامل v2.0 (مرجع توسعه)
├── PROGRESS_CHECKLIST.md         # وضعیت گام‌به‌گام اصلاحات نسخه‌ی تجاری
├── .github/workflows/            # بیلد خودکار در GitHub Actions
├── src/
│   ├── main.cpp                  # setup + loop
│   ├── config.cpp/.h             # پین‌ها، ثابت‌ها، تنظیمات، کالیبراسیون تاچ
│   ├── can_manager.cpp/.h        # مدیریت CAN (TWAI)
│   ├── obd2_reader.cpp/.h        # خواندن OBD-II (غیرمسدودکننده)
│   ├── vehicle_control.cpp/.h    # اجرای فرمان‌ها (از ActiveProfileManager)
│   ├── vehicle_db.cpp/.h         # پارسر DBC (Intel + Motorola)
│   ├── tft_ui.cpp/.h             # رابط لمسی LVGL — ۴ تب + کالیبراسیون تاچ
│   ├── webserver.cpp/.h          # وب سرور + WebSocket + OTA
│   ├── wifi_manager.cpp/.h       # WiFi (AP/STA)
│   ├── custom_vehicle.h          # ساختار پروفایل‌های سفارشی
│   ├── custom_vehicle_store.*    # ذخیره JSON در SPIFFS
│   ├── learn_engine.*            # موتور یادگیری (capture/diff)
│   └── active_profile_manager.*  # یکپارچه‌ساز DBC + سفارشی
└── data/                         # محتوای SPIFFS
    ├── index.html, style.css, app.js   # وب (۴ تب: کنترل/داشبورد/یادگیری/تنظیمات)
    ├── dbc/                            # ۵۷ فایل DBC از OpenDBC (۳۸ مدل وصل‌شده)
    └── custom_vehicles/                # در زمان اجرا ساخته می‌شود (نه در ریپو)
```

</div>

---

<h2 dir="rtl" id="-مستندات-بیشتر">📚 مستندات بیشتر</h2>

<table dir="rtl">
<tr>
<td align="center"><b>فایل</b></td>
<td align="center"><b>چه چیزی در آن هست</b></td>
</tr>
<tr>
<td align="center"><a href="./CarTouch_SPEC.md"><code>CarTouch_SPEC.md</code></a></td>
<td align="center">معماری Learn Mode، الگوریتم، قوانین ایمنی، ترتیب توسعه — مرجع کامل برای ادامه‌ی توسعه</td>
</tr>
<tr>
<td align="center"><a href="./CHANGELOG.md"><code>CHANGELOG.md</code></a></td>
<td align="center">تغییرات هر نسخه، اصلاحات امنیتی و باگ‌ها، و مشکلات شناخته‌شده</td>
</tr>
<tr>
<td align="center"><a href="./PROGRESS_CHECKLIST.md"><code>PROGRESS_CHECKLIST.md</code></a></td>
<td align="center">وضعیت دقیق هر مورد از چک‌لیست اصلاحات نسخه‌ی تجاری (کامل‌شده / باقی‌مانده)</td>
</tr>
</table>

**مجوز:** MIT

</div>
