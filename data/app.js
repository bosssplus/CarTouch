/**
 * app.js - کنترل خودرو از طریق WebSocket
 * 
 * === تغییر امنیتی ===
 * قبل از استفاده از WebSocket، ابتدا یک توکن session از
 * /api/session-token (که خودش Basic Auth دارد) گرفته می‌شود.
 * این توکن به‌عنوان اولین پیام روی WebSocket فرستاده می‌شود تا
 * سرور اتصال را معتبر بداند. بدون این مرحله، سرور هیچ فرمانی
 * (قفل/باز/صندوق و...) را از این کلاینت قبول نمی‌کند.
 */

// ======================== تنظیمات ========================

const WS_URL = `ws://${window.location.hostname}/ws`;
const RECONNECT_INTERVAL = 3000;
const PING_INTERVAL = 30000;

// ======================== وضعیت ========================

let ws = null;
let reconnectTimer = null;
let pingTimer = null;
let isConnected = false;
let isAuthenticated = false;
let sessionToken = null;
let vehicleData = {};

// ======================== DOM references ========================

const connectionStatus = document.getElementById('connection-status');
const notification = document.getElementById('notification');
let notifTimeout = null;

// ======================== دریافت توکن session ========================

async function fetchSessionToken() {
    try {
        const res = await fetch('/api/session-token', { credentials: 'same-origin' });
        if (!res.ok) {
            throw new Error('عدم دسترسی - لطفاً دوباره وارد شوید');
        }
        const data = await res.json();
        sessionToken = data.token;
        return true;
    } catch (e) {
        console.error('خطا در دریافت توکن session:', e);
        showNotification('⚠️ خطا در احراز هویت - صفحه را رفرش کنید');
        return false;
    }
}

// ======================== اتصال WebSocket ========================

async function connectWebSocket() {
    if (ws && ws.readyState === WebSocket.OPEN) return;
    
    // ابتدا توکن معتبر بگیر (اگر نداریم یا احتمالاً منقضی شده)
    const gotToken = await fetchSessionToken();
    if (!gotToken) {
        scheduleReconnect();
        return;
    }
    
    try {
        ws = new WebSocket(WS_URL);
    } catch (e) {
        console.error('WebSocket error:', e);
        scheduleReconnect();
        return;
    }
    
    ws.onopen = function() {
        console.log('🔌 WebSocket متصل شد - در حال احراز هویت...');
        isConnected = true;
        isAuthenticated = false;
        clearTimeout(reconnectTimer);
        
        // اولین پیام باید auth باشد
        ws.send(JSON.stringify({ type: 'auth', token: sessionToken }));
    };
    
    ws.onclose = function() {
        console.log('❌ WebSocket قطع شد');
        isConnected = false;
        isAuthenticated = false;
        connectionStatus.textContent = 'قطع';
        connectionStatus.classList.remove('connected');
        showNotification('❌ اتصال قطع شد');
        stopPing();
        scheduleReconnect();
    };
    
    ws.onerror = function(err) {
        console.error('WebSocket error:', err);
    };
    
    ws.onmessage = function(event) {
        try {
            const data = JSON.parse(event.data);
            handleMessage(data);
        } catch (e) {
            console.warn('خطا در پارس پیام:', e);
        }
    };
}

function scheduleReconnect() {
    clearTimeout(reconnectTimer);
    reconnectTimer = setTimeout(connectWebSocket, RECONNECT_INTERVAL);
}

function startPing() {
    stopPing();
    pingTimer = setInterval(() => {
        if (ws && ws.readyState === WebSocket.OPEN && isAuthenticated) {
            ws.send(JSON.stringify({ type: 'ping' }));
        }
    }, PING_INTERVAL);
}

function stopPing() {
    clearInterval(pingTimer);
}

// ======================== مدیریت پیام‌ها ========================

function handleMessage(data) {
    switch (data.type) {
        case 'need_auth':
            // سرور منتظر پیام auth است (یا توکن رد شده) - دوباره تلاش کن
            if (sessionToken) {
                ws.send(JSON.stringify({ type: 'auth', token: sessionToken }));
            }
            break;
            
        case 'auth_failed':
            console.warn('توکن session نامعتبر یا منقضی شده');
            isAuthenticated = false;
            sessionToken = null;
            showNotification('⚠️ نشست منقضی شده - دوباره تلاش می‌شود...');
            break;
            
        case 'welcome':
            console.log('CarTouch:', data.message);
            isAuthenticated = true;
            connectionStatus.textContent = 'وصل';
            connectionStatus.classList.add('connected');
            showNotification('✅ به CarTouch متصل شدید');
            startPing();
            break;
            
        case 'rate_limited':
            showNotification('⏳ لطفاً کمی صبر کنید');
            break;
            
        case 'pong':
            break;
            
        case 'vehicle_data':
            vehicleData = data;
            updateDashboard(data);
            break;
            
        case 'status':
            showNotification(data.message);
            break;
            
        case 'ack':
            break;
            
        // === جدید v2.0: پیام‌های Learn Mode ===
        case 'learn_state':
            handleLearnState(data);
            break;
            
        case 'learn_error':
            showNotification('⚠️ ' + (data.message || 'خطا در یادگیری'));
            break;
            
        case 'learn_saved':
            if (data.success) {
                showNotification('✅ فرمان ذخیره شد (هنوز تأییدنشده)');
                closeLearnWizard();
                refreshCustomVehicleList();
            } else {
                showNotification('❌ ذخیره ناموفق بود');
            }
            break;
            
        case 'verify_sent':
            handleVerifySent(data);
            break;
            
        case 'verify_error':
            showNotification('⚠️ ' + (data.message || 'خطا در ارسال آزمایشی'));
            break;
            
        case 'verify_confirmed':
            refreshCustomVehicleList();
            break;
            
        default:
            console.log('پیام ناشناخته:', data);
    }
}

// ======================== به‌روزرسانی داشبورد ========================

function updateDashboard(data) {
    const speedEl = document.getElementById('speed-display');
    if (speedEl) {
        speedEl.innerHTML = `${data.speed || 0} <small>km/h</small>`;
    }
    
    const rpmEl = document.getElementById('dash-rpm');
    if (rpmEl) rpmEl.textContent = data.rpm || 0;
    
    const tempEl = document.getElementById('dash-temp');
    if (tempEl) tempEl.textContent = `${data.coolantTemp ?? '--'} °C`;
    
    const batteryEl = document.getElementById('dash-battery');
    if (batteryEl) batteryEl.textContent = `${data.battery ?? '--'} V`;
    
    const fuelEl = document.getElementById('dash-fuel');
    if (fuelEl) fuelEl.textContent = `${data.fuel ?? '--'}%`;
}

// ======================== ارسال فرمان ========================

function sendCommand(command) {
    if (!isConnected || !isAuthenticated || !ws || ws.readyState !== WebSocket.OPEN) {
        showNotification('⚠️ اتصال برقرار نیست');
        return;
    }
    
    const msg = JSON.stringify({
        type: 'command',
        command: command
    });
    
    ws.send(msg);
    console.log('📤 فرمان ارسال شد:', command);
}

// ======================== نمایش نوتیفیکیشن ========================

function showNotification(message) {
    notification.textContent = message;
    notification.classList.add('show');
    
    clearTimeout(notifTimeout);
    notifTimeout = setTimeout(() => {
        notification.classList.remove('show');
    }, 2500);
}

// ======================== رویدادهای UI ========================

document.querySelectorAll('.ctrl-btn[data-cmd]').forEach(btn => {
    btn.addEventListener('click', function() {
        const cmd = this.getAttribute('data-cmd');
        sendCommand(cmd);
        
        this.style.transform = 'scale(0.92)';
        setTimeout(() => {
            this.style.transform = '';
        }, 150);
    });
});

document.querySelectorAll('.tab-btn').forEach(btn => {
    btn.addEventListener('click', function() {
        document.querySelectorAll('.tab-btn').forEach(b => b.classList.remove('active'));
        this.classList.add('active');
        
        const tab = this.getAttribute('data-tab');
        document.querySelectorAll('.tab-content').forEach(el => el.classList.remove('active'));
        document.getElementById(`tab-${tab}`).classList.add('active');
        
        // === جدید v2.0: هروقت وارد تب یادگیری شدیم، لیست پروفایل‌ها رفرش شود
        if (tab === 'learn') {
            refreshCustomVehicleList();
        }
    });
});

// ======================== تغییر رمز ========================

async function checkPasswordStatus() {
    try {
        const res = await fetch('/api/status', { credentials: 'same-origin' });
        if (!res.ok) return;
        const data = await res.json();
        const warningGroup = document.getElementById('password-warning-group');
        if (warningGroup) {
            warningGroup.style.display = data.usingDefaultPassword ? 'block' : 'none';
        }
        // بنر خودروی فعال هم از همین پاسخ رفرش می‌شود
        const banner = document.getElementById('active-vehicle-banner');
        if (banner && data.activeVehicle) {
            banner.textContent = 'خودروی فعال: ' + data.activeVehicle;
        }
    } catch (e) {
        console.warn('خطا در بررسی وضعیت رمز:', e);
    }
}

function setupPasswordForm() {
    const form = document.getElementById('password-form');
    if (!form) return;
    
    form.addEventListener('submit', async function(e) {
        e.preventDefault();
        
        const newUser = document.getElementById('new-user').value.trim();
        const newPass = document.getElementById('new-pass').value;
        const confirmPass = document.getElementById('confirm-pass').value;
        const msgEl = document.getElementById('password-form-msg');
        
        if (newPass.length < 8) {
            msgEl.textContent = 'رمز باید حداقل ۸ کاراکتر باشد';
            msgEl.style.color = '#e74c3c';
            return;
        }
        if (newPass !== confirmPass) {
            msgEl.textContent = 'تکرار رمز مطابقت ندارد';
            msgEl.style.color = '#e74c3c';
            return;
        }
        
        try {
            const body = new URLSearchParams();
            if (newUser) body.append('newUser', newUser);
            body.append('newPass', newPass);
            body.append('confirmPass', confirmPass);
            
            const res = await fetch('/api/change-password', {
                method: 'POST',
                credentials: 'same-origin',
                headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
                body: body.toString()
            });
            const data = await res.json();
            
            if (data.success) {
                msgEl.textContent = '✅ رمز با موفقیت تغییر کرد. لطفاً دوباره وارد شوید...';
                msgEl.style.color = '#2ecc71';
                form.reset();
                // چون سرور session قبلی را باطل کرد، بعد از چند ثانیه صفحه را رفرش کن
                // تا کاربر با رمز جدید دوباره لاگین کند
                setTimeout(() => { window.location.reload(); }, 2000);
            } else {
                msgEl.textContent = '⚠️ ' + (data.error || 'خطا در تغییر رمز');
                msgEl.style.color = '#e74c3c';
            }
        } catch (err) {
            console.error('خطا در تغییر رمز:', err);
            msgEl.textContent = '⚠️ خطا در ارتباط با سرور';
            msgEl.style.color = '#e74c3c';
        }
    });
}

// ======================== شروع ========================

document.addEventListener('DOMContentLoaded', function() {
    console.log('CarTouch Web UI loaded');
    showNotification('🚗 در حال اتصال...');
    connectWebSocket();
    checkPasswordStatus();
    setupPasswordForm();
    setupLearnMode();  // === جدید v2.0 ===
});

// ======================================================================
// === جدید v2.0: منطق کامل Learn Mode ===
// به CarTouch_V2_SPEC.md بخش ۴، ۵، ۷.۶ مراجعه کنید.
// ⚠️ این بخش هرگز مستقیماً به CAN Bus دسترسی ندارد - همه‌چیز از طریق
// پیام‌های WebSocket تأییدشده (که فقط بعد از auth کار می‌کنند) با
// دستگاه صحبت می‌شود، و دستگاه خودش تمام قوانین ایمنی (Listen-Only
// حین یادگیری، نیاز به VERIFIED برای اجرا) را اعمال می‌کند.
// ======================================================================

const LEARN_LABELS = [
    { value: 'lock_all', text: 'قفل همه درب‌ها' },
    { value: 'unlock_all', text: 'باز کردن همه درب‌ها' },
    { value: 'unlock_driver', text: 'باز کردن درب راننده' },
    { value: 'window_fl_up', text: 'شیشه جلو چپ بالا' },
    { value: 'window_fl_down', text: 'شیشه جلو چپ پایین' },
    { value: 'window_fr_up', text: 'شیشه جلو راست بالا' },
    { value: 'window_fr_down', text: 'شیشه جلو راست پایین' },
    { value: 'window_rl_up', text: 'شیشه عقب چپ بالا' },
    { value: 'window_rl_down', text: 'شیشه عقب چپ پایین' },
    { value: 'window_rr_up', text: 'شیشه عقب راست بالا' },
    { value: 'window_rr_down', text: 'شیشه عقب راست پایین' },
    { value: 'all_windows_up', text: 'همه شیشه‌ها بالا' },
    { value: 'all_windows_down', text: 'همه شیشه‌ها پایین' },
    { value: 'sunroof_open', text: 'باز کردن سانروف' },
    { value: 'sunroof_close', text: 'بستن سانروف' },
    { value: 'sunroof_tilt', text: 'کج کردن سانروف' },
    { value: 'trunk_open', text: 'باز کردن صندوق' },
    { value: 'trunk_lock', text: 'قفل صندوق' },
    { value: 'mirror_fold', text: 'تا کردن آینه‌ها' },
    { value: 'mirror_unfold', text: 'باز کردن آینه‌ها' },
    { value: 'alarm_arm', text: 'فعال کردن دزدگیر' },
    { value: 'alarm_disarm', text: 'غیرفعال کردن دزدگیر' },
];

let currentLearnCandidates = [];
let selectedCandidateIndex = -1;
let currentLearnLabel = null;
let currentLearnDisplayName = null;
let activeProfileId = null;  // پروفایلی که در حال یادگیری/ورود دستی برایش هستیم

function setupLearnMode() {
    // پر کردن dropdown های برچسب فرمان
    const wizardTitle = document.getElementById('learn-wizard-label-name');
    populateLabelSelect('learn-label-select');
    populateLabelSelect('manual-label-select');
    
    document.getElementById('btn-new-profile').addEventListener('click', createNewProfile);
    document.getElementById('btn-learn-start').addEventListener('click', startLearnWizard);
    document.getElementById('btn-manual-entry-open').addEventListener('click', openManualEntryModal);
    document.getElementById('btn-manual-save').addEventListener('click', submitManualEntry);
    document.getElementById('btn-manual-cancel').addEventListener('click', closeManualEntryModal);
    document.getElementById('btn-wizard-action').addEventListener('click', onWizardActionClick);
    document.getElementById('btn-wizard-cancel').addEventListener('click', closeLearnWizard);
    document.getElementById('btn-export-profile').addEventListener('click', exportSelectedProfile);
    document.getElementById('btn-import-profile').addEventListener('click', () => {
        document.getElementById('import-file-input').click();
    });
    document.getElementById('import-file-input').addEventListener('change', handleImportFile);
    
    document.getElementById('btn-verify-send').addEventListener('click', sendVerifyCommand);
    document.getElementById('btn-verify-success').addEventListener('click', () => confirmVerifyResult(true));
    document.getElementById('btn-verify-fail').addEventListener('click', () => confirmVerifyResult(false));
    document.getElementById('btn-verify-close').addEventListener('click', closeVerifyModal);
    
    refreshCustomVehicleList();
}

function populateLabelSelect(elementId) {
    const select = document.getElementById(elementId);
    if (!select) return;
    select.innerHTML = '';
    LEARN_LABELS.forEach(l => {
        const opt = document.createElement('option');
        opt.value = l.value;
        opt.textContent = l.text;
        select.appendChild(opt);
    });
}

// ======================== لیست پروفایل‌های سفارشی (REST) ========================

async function refreshCustomVehicleList() {
    try {
        const res = await fetch('/api/vehicles/custom', { credentials: 'same-origin' });
        if (!res.ok) return;
        const data = await res.json();
        
        const listEl = document.getElementById('learn-profile-list');
        const selectEl = document.getElementById('learn-active-profile-select');
        if (!listEl || !selectEl) return;
        
        listEl.innerHTML = '';
        selectEl.innerHTML = '<option value="">-- انتخاب پروفایل برای یادگیری/ورود دستی --</option>';
        
        (data.profiles || []).forEach(p => {
            const item = document.createElement('div');
            item.className = 'profile-item';
            item.innerHTML = `
                <div>
                    <div class="profile-name">${escapeHtml(p.name)}</div>
                    <div class="profile-meta">${escapeHtml(p.brand || '')} ${escapeHtml(p.model || '')} - ${p.commandCount} فرمان</div>
                </div>
                <div class="profile-item-actions">
                    <button data-action="select" data-id="${p.id}">فعال‌سازی</button>
                    <button data-action="commands" data-id="${p.id}">فرمان‌ها</button>
                    <button data-action="delete" data-id="${p.id}">حذف</button>
                </div>
            `;
            listEl.appendChild(item);
            
            const opt = document.createElement('option');
            opt.value = p.id;
            opt.textContent = `${p.name} (${p.commandCount} فرمان)`;
            selectEl.appendChild(opt);
        });
        
        listEl.querySelectorAll('button[data-action="select"]').forEach(btn => {
            btn.addEventListener('click', () => selectActiveVehicle(btn.dataset.id));
        });
        listEl.querySelectorAll('button[data-action="delete"]').forEach(btn => {
            btn.addEventListener('click', () => deleteProfile(btn.dataset.id));
        });
        listEl.querySelectorAll('button[data-action="commands"]').forEach(btn => {
            btn.addEventListener('click', () => showProfileCommands(btn.dataset.id));
        });
    } catch (e) {
        console.warn('خطا در دریافت لیست پروفایل‌ها:', e);
    }
}

function escapeHtml(str) {
    const div = document.createElement('div');
    div.textContent = str || '';
    return div.innerHTML;
}

async function createNewProfile() {
    const name = prompt('نام دلخواه برای این خودرو (مثلاً "پراید بابا"):');
    if (!name) return;
    
    try {
        const body = new URLSearchParams();
        body.append('name', name);
        const res = await fetch('/api/vehicles/custom/new', {
            method: 'POST',
            credentials: 'same-origin',
            headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
            body: body.toString()
        });
        const data = await res.json();
        if (data.success) {
            showNotification('✅ پروفایل جدید ساخته شد');
            refreshCustomVehicleList();
        } else {
            showNotification('❌ ' + (data.error || 'خطا در ساخت پروفایل'));
        }
    } catch (e) {
        console.error(e);
        showNotification('⚠️ خطا در ارتباط با سرور');
    }
}

async function selectActiveVehicle(id) {
    try {
        const body = new URLSearchParams();
        body.append('id', id);
        const res = await fetch('/api/vehicles/custom/select', {
            method: 'POST',
            credentials: 'same-origin',
            headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
            body: body.toString()
        });
        const data = await res.json();
        if (data.success) {
            showNotification('🚗 این خودرو به‌عنوان فعال انتخاب شد');
            const banner = document.getElementById('active-vehicle-banner');
            if (banner) checkPasswordStatus();  // status شامل activeVehicle هم هست، رفرش می‌کنیم
        } else {
            showNotification('❌ خطا در انتخاب پروفایل');
        }
    } catch (e) {
        console.error(e);
    }
}

async function deleteProfile(id) {
    if (!confirm('آیا مطمئنید؟ این پروفایل و تمام فرمان‌های یادگرفته‌شده‌اش حذف می‌شود.')) return;
    
    try {
        const body = new URLSearchParams();
        body.append('id', id);
        const res = await fetch('/api/vehicles/custom/delete', {
            method: 'POST',
            credentials: 'same-origin',
            headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
            body: body.toString()
        });
        const data = await res.json();
        if (data.success) {
            showNotification('🗑 پروفایل حذف شد');
            refreshCustomVehicleList();
        }
    } catch (e) {
        console.error(e);
    }
}

// نمایش لیست فرمان‌های یک پروفایل با وضعیت تأیید (و دکمه‌ی تأیید برای هرکدام)
async function showProfileCommands(id) {
    try {
        const res = await fetch('/api/vehicles/custom', { credentials: 'same-origin' });
        const data = await res.json();
        const profile = (data.profiles || []).find(p => String(p.id) === String(id));
        if (!profile) return;
        
        // برای دیدن فرمان‌های کامل (نه فقط خلاصه)، از export استفاده می‌کنیم
        const exportRes = await fetch(`/api/vehicles/custom/export?id=${id}`, { credentials: 'same-origin' });
        const fullProfile = await exportRes.json();
        
        let html = `<h3>فرمان‌های «${escapeHtml(fullProfile.name)}»</h3>`;
        (fullProfile.commands || []).forEach(cmd => {
            const isVerified = cmd.status === 'verified';
            html += `
                <div class="command-row">
                    <span>${escapeHtml(cmd.displayName || cmd.label)}
                        <span class="${isVerified ? 'command-status-verified' : 'command-status-unverified'}">
                            ${isVerified ? '✅ تأییدشده' : '⚠️ تأییدنشده'}
                        </span>
                    </span>
                    ${!isVerified ? `<button class="command-verify-btn" data-label="${escapeHtml(cmd.label)}" data-canid="${cmd.canId}" data-canidhex="0x${cmd.canId.toString(16).toUpperCase()}" data-data="${(cmd.data||[]).join(',')}">تأیید</button>` : ''}
                </div>
            `;
        });
        
        // نمایش در یک مودال ساده با استفاده از همان ساختار candidate list
        const listEl = document.getElementById('learn-wizard-candidates');
        document.getElementById('learn-wizard-label-name').textContent = fullProfile.name;
        document.getElementById('learn-wizard-status').innerHTML = html;
        document.getElementById('learn-wizard-progress-container').classList.add('hidden');
        listEl.classList.add('hidden');
        document.getElementById('btn-wizard-action').classList.add('hidden');
        document.getElementById('learn-wizard-modal').classList.remove('hidden');
        
        document.querySelectorAll('.command-verify-btn').forEach(btn => {
            btn.addEventListener('click', () => {
                closeLearnWizard();
                const dataArr = btn.dataset.data ? btn.dataset.data.split(',').map(Number) : [];
                openVerifyModal(id, btn.dataset.label, btn.dataset.canidhex, dataArr);
            });
        });
    } catch (e) {
        console.error(e);
        showNotification('⚠️ خطا در نمایش فرمان‌ها');
    }
}

// ======================== Export / Import ========================

async function exportSelectedProfile() {
    const selectEl = document.getElementById('learn-active-profile-select');
    const id = selectEl.value;
    if (!id) {
        showNotification('⚠️ ابتدا یک پروفایل را از لیست بالا انتخاب کنید');
        return;
    }
    
    window.location.href = `/api/vehicles/custom/export?id=${id}`;
}

function handleImportFile(e) {
    const file = e.target.files[0];
    if (!file) return;
    
    const reader = new FileReader();
    reader.onload = async function(evt) {
        try {
            const jsonText = evt.target.result;
            // اعتبارسنجی حداقلی سمت کلاینت قبل از ارسال
            JSON.parse(jsonText);
            
            const body = new URLSearchParams();
            body.append('json', jsonText);
            const res = await fetch('/api/vehicles/custom/import', {
                method: 'POST',
                credentials: 'same-origin',
                headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
                body: body.toString()
            });
            const data = await res.json();
            if (data.success) {
                showNotification('✅ پروفایل وارد شد (همه فرمان‌ها تأییدنشده علامت خوردند)');
                refreshCustomVehicleList();
            } else {
                showNotification('❌ ' + (data.error || 'فایل نامعتبر'));
            }
        } catch (err) {
            console.error(err);
            showNotification('⚠️ فایل JSON نامعتبر است');
        }
    };
    reader.readAsText(file);
    e.target.value = '';  // ریست input تا انتخاب دوباره‌ی همان فایل هم کار کند
}

// ======================== ویزارد یادگیری (WebSocket) ========================

function startLearnWizard() {
    const selectEl = document.getElementById('learn-active-profile-select');
    activeProfileId = selectEl.value;
    if (!activeProfileId) {
        showNotification('⚠️ ابتدا یک پروفایل را انتخاب یا بسازید');
        return;
    }
    
    const labelSelect = document.getElementById('learn-label-select');
    currentLearnLabel = labelSelect.value;
    currentLearnDisplayName = labelSelect.options[labelSelect.selectedIndex].text;
    
    selectedCandidateIndex = -1;
    currentLearnCandidates = [];
    
    document.getElementById('learn-wizard-label-name').textContent = currentLearnDisplayName;
    document.getElementById('learn-wizard-status').textContent =
        'در این حالت، دستگاه فقط پیام‌های CAN Bus را می‌شنود و هیچ فرمانی ارسال نمی‌کند.';
    document.getElementById('learn-wizard-progress-container').classList.add('hidden');
    document.getElementById('learn-wizard-candidates').classList.add('hidden');
    document.getElementById('btn-wizard-action').classList.remove('hidden');
    document.getElementById('btn-wizard-action').textContent = '▶ شروع ضبط پیش‌زمینه';
    document.getElementById('learn-wizard-modal').classList.remove('hidden');
    
    sendCommand2('learn_start', { label: currentLearnLabel, displayName: currentLearnDisplayName });
}

function closeLearnWizard() {
    document.getElementById('learn-wizard-modal').classList.add('hidden');
    document.getElementById('btn-wizard-action').classList.remove('hidden');
    sendCommand2('learn_cancel', {});
}

function onWizardActionClick() {
    const btn = document.getElementById('btn-wizard-action');
    const currentText = btn.textContent;
    
    if (currentText.includes('شروع ضبط پیش‌زمینه')) {
        sendCommand2('learn_capture_baseline', {});
    } else if (currentText.includes('شروع ضبط اقدام')) {
        sendCommand2('learn_confirm_action', {});
    } else if (currentText.includes('ذخیره')) {
        if (selectedCandidateIndex < 0) {
            showNotification('⚠️ ابتدا یک کاندید را از لیست انتخاب کنید');
            return;
        }
        sendCommand2('learn_save', { candidateIndex: selectedCandidateIndex, profileId: parseInt(activeProfileId) });
    }
}

// polling وضعیت هر ۳۰۰ میلی‌ثانیه حین باز بودن مودال (چون baseline/action
// capture زمان‌بر است و سرور به‌صورت push هم پیام learn_state می‌فرستد،
// این polling یک لایه‌ی اطمینان اضافی است)
let learnPollTimer = null;
function startLearnPolling() {
    stopLearnPolling();
    learnPollTimer = setInterval(() => {
        if (!document.getElementById('learn-wizard-modal').classList.contains('hidden')) {
            sendCommand2('learn_get_state', {});
        } else {
            stopLearnPolling();
        }
    }, 300);
}
function stopLearnPolling() {
    if (learnPollTimer) clearInterval(learnPollTimer);
    learnPollTimer = null;
}

function handleLearnState(data) {
    const statusEl = document.getElementById('learn-wizard-status');
    const progressContainer = document.getElementById('learn-wizard-progress-container');
    const progressFill = document.getElementById('learn-wizard-progress-fill');
    const candidatesEl = document.getElementById('learn-wizard-candidates');
    const actionBtn = document.getElementById('btn-wizard-action');
    
    switch (data.state) {
        case 'baseline_capture':
            progressContainer.classList.remove('hidden');
            progressFill.style.width = (data.progress || 0) + '%';
            candidatesEl.classList.add('hidden');
            statusEl.textContent = '⏺ در حال ضبط پیش‌زمینه... لطفاً هنوز دکمه فیزیکی خودرو را نزنید.';
            actionBtn.textContent = '⏳ در حال ضبط...';
            startLearnPolling();
            break;
            
        case 'waiting_action':
            progressContainer.classList.add('hidden');
            statusEl.textContent = '✅ ضبط پیش‌زمینه تمام شد. آماده باشید؛ وقتی «شروع ضبط اقدام» را می‌زنید، بلافاصله دکمه فیزیکی خودرو را فشار دهید.';
            actionBtn.textContent = '▶ شروع ضبط اقدام';
            stopLearnPolling();
            break;
            
        case 'action_capture':
            progressContainer.classList.remove('hidden');
            progressFill.style.width = (data.progress || 0) + '%';
            statusEl.textContent = '🔴 همین الان دکمه فیزیکی خودرو را بزنید!';
            actionBtn.textContent = '⏳ در حال ضبط...';
            startLearnPolling();
            break;
            
        case 'candidates_ready':
            progressContainer.classList.add('hidden');
            stopLearnPolling();
            currentLearnCandidates = data.candidates || [];
            statusEl.textContent = `${currentLearnCandidates.length} کاندید یافت شد. کاندیدی که با فشردن دکمه مطابقت داشت را انتخاب کنید:`;
            renderCandidates();
            actionBtn.textContent = '💾 ذخیره کاندید انتخاب‌شده';
            break;
            
        case 'idle':
            stopLearnPolling();
            break;
    }
}

function renderCandidates() {
    const candidatesEl = document.getElementById('learn-wizard-candidates');
    candidatesEl.innerHTML = '';
    candidatesEl.classList.remove('hidden');
    
    currentLearnCandidates.forEach((c, idx) => {
        const item = document.createElement('div');
        item.className = 'candidate-item' + (selectedCandidateIndex === idx ? ' selected' : '');
        const dataHex = (c.data || []).map(b => b.toString(16).padStart(2, '0').toUpperCase()).join(' ');
        item.innerHTML = `
            <div>${c.canIdHex} <span class="candidate-tag">${c.isNew ? '[جدید]' : '[تغییر]'} (x${c.seenCount})</span></div>
            <div>${dataHex}</div>
        `;
        item.addEventListener('click', () => {
            selectedCandidateIndex = idx;
            renderCandidates();
            showNotification('✅ کاندید انتخاب شد - حالا «ذخیره» را بزنید');
        });
        candidatesEl.appendChild(item);
    });
}

// ======================== ورود دستی ========================

function openManualEntryModal() {
    const selectEl = document.getElementById('learn-active-profile-select');
    activeProfileId = selectEl.value;
    if (!activeProfileId) {
        showNotification('⚠️ ابتدا یک پروفایل را انتخاب یا بسازید');
        return;
    }
    
    document.getElementById('manual-can-id').value = '';
    document.getElementById('manual-data-hex').value = '';
    document.getElementById('manual-extended').checked = false;
    document.getElementById('manual-entry-error').textContent = '';
    document.getElementById('manual-entry-modal').classList.remove('hidden');
}

function closeManualEntryModal() {
    document.getElementById('manual-entry-modal').classList.add('hidden');
}

async function submitManualEntry() {
    const errorEl = document.getElementById('manual-entry-error');
    const labelSelect = document.getElementById('manual-label-select');
    const label = labelSelect.value;
    const displayName = labelSelect.options[labelSelect.selectedIndex].text;
    const canIdStr = document.getElementById('manual-can-id').value.trim();
    const extended = document.getElementById('manual-extended').checked;
    const dataHex = document.getElementById('manual-data-hex').value.trim();
    
    if (!canIdStr) {
        errorEl.textContent = 'CAN ID را وارد کنید';
        return;
    }
    
    const canId = parseInt(canIdStr, 16);
    const maxId = extended ? 0x1FFFFFFF : 0x7FF;
    if (isNaN(canId) || canId > maxId || canId < 0) {
        errorEl.textContent = 'CAN ID نامعتبر یا خارج از محدوده مجاز است';
        return;
    }
    
    if (!dataHex) {
        errorEl.textContent = 'حداقل یک بایت داده وارد کنید';
        return;
    }
    
    try {
        const body = new URLSearchParams();
        body.append('profileId', activeProfileId);
        body.append('label', label);
        body.append('displayName', displayName);
        body.append('canId', canIdStr);
        body.append('extended', extended ? '1' : '0');
        body.append('dataHex', dataHex);
        
        const res = await fetch('/api/vehicles/custom/manual-add', {
            method: 'POST',
            credentials: 'same-origin',
            headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
            body: body.toString()
        });
        const data = await res.json();
        
        if (data.success) {
            closeManualEntryModal();
            showNotification('✅ فرمان دستی ذخیره شد (تأییدنشده)');
            refreshCustomVehicleList();
        } else {
            errorEl.textContent = data.error || 'ذخیره ناموفق بود';
        }
    } catch (e) {
        console.error(e);
        errorEl.textContent = 'خطا در ارتباط با سرور';
    }
}

// ======================== تأیید فرمان (بخش ۴.۲ سند) ========================

let verifyContext = { profileId: null, label: null };

function openVerifyModal(profileId, label, canIdHex, dataArr) {
    verifyContext = { profileId, label };
    
    const dataHexStr = (dataArr || []).map(b => b.toString(16).padStart(2, '0').toUpperCase()).join(' ');
    document.getElementById('verify-info-text').textContent =
        `با ارسال، پیام زیر مستقیماً روی CAN Bus خودرو فرستاده می‌شود:\n\n` +
        `CAN ID: ${canIdHex}\nData: ${dataHexStr}\n\n` +
        `این عمل ممکن است اثر فیزیکی فوری روی خودرو داشته باشد (مثلاً باز شدن قفل). ` +
        `لطفاً مطمئن شوید خودرو در وضعیت امنی است.\n\n` +
        `⚠️ توجه: بعضی خودروهای جدیدتر (rolling code) ممکن است با این روش کار نکنند.`;
    document.getElementById('verify-result-text').textContent = '';
    document.getElementById('verify-modal').classList.remove('hidden');
}

function closeVerifyModal() {
    document.getElementById('verify-modal').classList.add('hidden');
}

function sendVerifyCommand() {
    if (!verifyContext.profileId || !verifyContext.label) return;
    sendCommand2('verify_command', {
        vehicleId: parseInt(verifyContext.profileId),
        label: verifyContext.label
    });
}

function handleVerifySent(data) {
    const resultEl = document.getElementById('verify-result-text');
    if (data.success) {
        resultEl.textContent = '✅ فرمان ارسال شد. آیا خودرو واکنش درستی نشان داد؟';
    } else {
        resultEl.textContent = '❌ ارسال ناموفق: ' + (data.error || '');
    }
}

function confirmVerifyResult(success) {
    if (!verifyContext.profileId || !verifyContext.label) return;
    sendCommand2('verify_confirm', {
        vehicleId: parseInt(verifyContext.profileId),
        label: verifyContext.label,
        success: success
    });
    showNotification(success ? '✅ فرمان تأیید شد' : '⚠️ فرمان همچنان تأییدنشده باقی ماند');
    closeVerifyModal();
    refreshCustomVehicleList();
}

// ======================== ابزار مشترک ارسال پیام WebSocket ========================
// (sendCommand موجود در بالای فایل فقط برای پیام‌های نوع "command" با
// یک رشته‌ی ساده طراحی شده؛ این تابع جدید برای پیام‌های Learn Mode با
// payload دلخواه استفاده می‌شود، ولی از همان بررسی اتصال/auth استفاده می‌کند.)

function sendCommand2(type, payload) {
    if (!isConnected || !isAuthenticated || !ws || ws.readyState !== WebSocket.OPEN) {
        showNotification('⚠️ اتصال برقرار نیست');
        return;
    }
    
    const msg = Object.assign({ type: type }, payload);
    ws.send(JSON.stringify(msg));
}
