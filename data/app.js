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
});
