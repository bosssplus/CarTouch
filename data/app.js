/**
 * app.js - کنترل خودرو از طریق WebSocket
 * 
 * این اسکریپت از WebSocket برای ارتباط با ESP32-S3 استفاده می‌کند.
 * تمام توابع تست شده و آماده استفاده هستند.
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
let vehicleData = {};

// ======================== DOM references ========================

const connectionStatus = document.getElementById('connection-status');
const notification = document.getElementById('notification');
let notifTimeout = null;

// ======================== اتصال WebSocket ========================

function connectWebSocket() {
    if (ws && ws.readyState === WebSocket.OPEN) return;
    
    try {
        ws = new WebSocket(WS_URL);
    } catch (e) {
        console.error('WebSocket error:', e);
        scheduleReconnect();
        return;
    }
    
    ws.onopen = function() {
        console.log('✅ WebSocket متصل شد');
        isConnected = true;
        connectionStatus.textContent = 'وصل';
        connectionStatus.classList.add('connected');
        showNotification('✅ به CarTouch متصل شدید');
        clearTimeout(reconnectTimer);
        
        // شروع ping
        startPing();
    };
    
    ws.onclose = function() {
        console.log('❌ WebSocket قطع شد');
        isConnected = false;
        connectionStatus.textContent = 'قطع';
        connectionStatus.classList.remove('connected');
        showNotification('❌ اتصال قطع شد');
        stopPing();
        scheduleReconnect();
    };
    
    ws.onerror = function(err) {
        console.error('WebSocket error:', err);
        // onclose بعداً فراخوانی می‌شود
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
        if (ws && ws.readyState === WebSocket.OPEN) {
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
        case 'welcome':
            console.log('CarTouch:', data.message);
            break;
            
        case 'pong':
            // پاسخ ping دریافت شد
            break;
            
        case 'vehicle_data':
            vehicleData = data;
            updateDashboard(data);
            break;
            
        case 'status':
            showNotification(data.message);
            break;
            
        case 'ack':
            // تأیید دریافت فرمان
            break;
            
        default:
            console.log('پیام ناشناخته:', data);
    }
}

// ======================== به‌روزرسانی داشبورد ========================

function updateDashboard(data) {
    // سرعت
    const speedEl = document.getElementById('speed-display');
    if (speedEl) {
        speedEl.innerHTML = `${data.speed || 0} <small>km/h</small>`;
    }
    
    // RPM
    const rpmEl = document.getElementById('dash-rpm');
    if (rpmEl) rpmEl.textContent = data.rpm || 0;
    
    // دما
    const tempEl = document.getElementById('dash-temp');
    if (tempEl) tempEl.textContent = `${data.coolantTemp ?? '--'} °C`;
    
    // ولتاژ
    const batteryEl = document.getElementById('dash-battery');
    if (batteryEl) batteryEl.textContent = `${data.battery ?? '--'} V`;
    
    // سوخت
    const fuelEl = document.getElementById('dash-fuel');
    if (fuelEl) fuelEl.textContent = `${data.fuel ?? '--'}%`;
}

// ======================== ارسال فرمان ========================

function sendCommand(command) {
    if (!isConnected || !ws || ws.readyState !== WebSocket.OPEN) {
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

// کلیک روی دکمه‌های کنترل
document.querySelectorAll('.ctrl-btn[data-cmd]').forEach(btn => {
    btn.addEventListener('click', function() {
        const cmd = this.getAttribute('data-cmd');
        sendCommand(cmd);
        
        // افکت بصری
        this.style.transform = 'scale(0.92)';
        setTimeout(() => {
            this.style.transform = '';
        }, 150);
    });
});

// تغییر تب
document.querySelectorAll('.tab-btn').forEach(btn => {
    btn.addEventListener('click', function() {
        // به‌روزرسانی کلاس active دکمه‌ها
        document.querySelectorAll('.tab-btn').forEach(b => b.classList.remove('active'));
        this.classList.add('active');
        
        // نمایش تب مربوطه
        const tab = this.getAttribute('data-tab');
        document.querySelectorAll('.tab-content').forEach(el => el.classList.remove('active'));
        document.getElementById(`tab-${tab}`).classList.add('active');
    });
});

// ======================== شروع ========================

document.addEventListener('DOMContentLoaded', function() {
    console.log('CarTouch Web UI loaded');
    showNotification('🚗 در حال اتصال...');
    connectWebSocket();
});
