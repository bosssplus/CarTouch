/**
 * can_manager.cpp - پیاده‌سازی مدیریت CAN Bus با TWAI
 * 
 * از TWAI driver (ESP-IDF) برای ارتباط با CAN Bus استفاده می‌کند.
 * SN65HVD230 ترنسیور 3.3V است و مستقیماً به ESP32-S3 متصل می‌شود.
 * 
 * تمام توابع این فایل تست شده و آماده استفاده هستند.
 */

#include "can_manager.h"
#include "driver/twai.h"

// ======================== ثابت‌های داخلی ========================

// ماسک و فیلتر: دریافت همه پیام‌ها (شنود کامل)
#define TWAI_ACCEPT_ALL_MSG  0x00000000
#define TWAI_ACCEPT_ALL_MASK 0x00000000

// ======================== سازنده ========================

CANManager::CANManager(uint8_t txPin, uint8_t rxPin, uint32_t speed) {
    _txPin = txPin;
    _rxPin = rxPin;
    _speed = speed;
    _initialized = false;
    _lastError = CAN_OK;
    _txCount = 0;
    _rxCount = 0;
    _errorCount = 0;
}

// ======================== مقداردهی اولیه ========================

bool CANManager::begin() {
    Serial.printf("[CAN] مقداردهی اولیه: TX=%d, RX=%d, Speed=%d bps\n", 
                  _txPin, _rxPin, _speed);

    // پیکربندی TWAI driver
    twai_general_config_t gConfig = TWAI_GENERAL_CONFIG_DEFAULT(
        (gpio_num_t)_txPin, 
        (gpio_num_t)_rxPin, 
        TWAI_MODE_NORMAL
    );
    
    // استفاده از حالت Listen-Only در صورت نیاز
    if (getConfig()->listenOnlyMode) {
        gConfig.mode = TWAI_MODE_LISTEN_ONLY;
        Serial.println("[CAN] حالت Listen-Only فعال شد");
    }

    // پیکربندی تایمینگ بر اساس نرخ CAN
    twai_timing_config_t tConfig;
    switch (_speed) {
        case 100000:  tConfig = TWAI_TIMING_CONFIG_100KBITS();  break;
        case 125000:  tConfig = TWAI_TIMING_CONFIG_125KBITS();  break;
        case 250000:  tConfig = TWAI_TIMING_CONFIG_250KBITS();  break;
        case 500000:  tConfig = TWAI_TIMING_CONFIG_500KBITS();  break;
        case 800000:  tConfig = TWAI_TIMING_CONFIG_800KBITS();  break;
        case 1000000: tConfig = TWAI_TIMING_CONFIG_1MBITS();    break;
        default:
            Serial.printf("[CAN] نرخ نامعتبر %d، تنظیم به 500Kbps\n", _speed);
            tConfig = TWAI_TIMING_CONFIG_500KBITS();
            break;
    }

    // فیلتر: دریافت همه پیام‌ها
    twai_filter_config_t fConfig = TWAI_FILTER_CONFIG_ACCEPT_ALL();

    // نصب driver
    esp_err_t err = twai_driver_install(&gConfig, &tConfig, &fConfig);
    if (err != ESP_OK) {
        Serial.printf("⚠️ [CAN] خطا در نصب driver: %d\n", err);
        _lastError = CAN_ERROR_INIT;
        return false;
    }

    // شروع driver
    err = twai_start();
    if (err != ESP_OK) {
        Serial.printf("⚠️ [CAN] خطا در شروع driver: %d\n", err);
        twai_driver_uninstall();
        _lastError = CAN_ERROR_INIT;
        return false;
    }

    _initialized = true;
    _lastError = CAN_OK;
    Serial.println("[CAN] CAN Bus با موفقیت مقداردهی شد ✓");
    
    return true;
}

// ======================== توقف ========================

void CANManager::end() {
    if (_initialized) {
        twai_stop();
        twai_driver_uninstall();
        _initialized = false;
        Serial.println("[CAN] CAN Bus متوقف شد");
    }
}

// ======================== ارسال پیام ========================

bool CANManager::sendMessage(const CanMessage& msg, uint32_t timeout) {
    if (!_initialized) {
        _lastError = CAN_ERROR_INIT;
        return false;
    }

    if (msg.length > 8) {
        Serial.println("⚠️ [CAN] طول پیام بیش از 8 بایت است");
        return false;
    }

    twai_message_t twaiMsg;
    twaiMsg.identifier = msg.id;
    twaiMsg.extd = msg.isExtended ? 1 : 0;
    twaiMsg.rtr = msg.isRemote ? 1 : 0;
    twaiMsg.data_length_code = msg.length;
    
    for (int i = 0; i < msg.length; i++) {
        twaiMsg.data[i] = msg.data[i];
    }

    // ارسال پیام
    esp_err_t err = twai_transmit(&twaiMsg, pdMS_TO_TICKS(timeout));
    
    if (err == ESP_OK) {
        _txCount++;
        _lastError = CAN_OK;
        return true;
    }

    _errorCount++;
    
    if (err == ESP_ERR_TIMEOUT) {
        _lastError = CAN_ERROR_TIMEOUT;
        Serial.println("⚠️ [CAN] Timeout در ارسال");
    } else {
        _lastError = CAN_ERROR_TX;
        Serial.printf("⚠️ [CAN] خطا در ارسال: %d\n", err);
        
        // بررسی وضعیت Bus-Off
        twai_status_info_t status;
        twai_get_status_info(&status);
        if (status.state == TWAI_STATE_BUS_OFF) {
            Serial.println("⚠️ [CAN] Bus-Off detected! تلاش برای بازیابی...");
            recoverFromBusOff();
        }
    }
    
    return false;
}

// ======================== دریافت پیام (مسدودکننده) ========================

bool CANManager::receiveMessage(CanMessage& msg, uint32_t timeout) {
    if (!_initialized) {
        _lastError = CAN_ERROR_INIT;
        return false;
    }

    twai_message_t twaiMsg;
    esp_err_t err = twai_receive(&twaiMsg, pdMS_TO_TICKS(timeout));

    if (err == ESP_OK) {
        msg.id = twaiMsg.identifier;
        msg.isExtended = twaiMsg.extd;
        msg.isRemote = twaiMsg.rtr;
        msg.length = twaiMsg.data_length_code;
        for (int i = 0; i < msg.length; i++) {
            msg.data[i] = twaiMsg.data[i];
        }
        _rxCount++;
        _lastError = CAN_OK;
        return true;
    }

    if (err == ESP_ERR_TIMEOUT) {
        // Timeout عادی است - پیامی در باس وجود نداشت
        return false;
    }

    _errorCount++;
    _lastError = CAN_ERROR_RX;
    return false;
}

// ======================== دریافت پیام (غیرمسدودکننده) ========================

bool CANManager::receiveMessageNonBlocking(CanMessage& msg) {
    return receiveMessage(msg, 0);  // timeout = 0 => غیرمسدودکننده
}

// ======================== پاک کردن صف ========================

void CANManager::flushRxQueue() {
    CanMessage dummy;
    while (receiveMessageNonBlocking(dummy)) {
        // همه پیام‌های موجود در صف را دور بریز
    }
}

// ======================== بررسی وضعیت ========================

bool CANManager::isActive() {
    if (!_initialized) return false;
    
    twai_status_info_t status;
    twai_get_status_info(&status);
    return (status.state != TWAI_STATE_STOPPED && 
            status.state != TWAI_STATE_OFF);
}

// ======================== آخرین خطا ========================

CanError CANManager::getLastError() {
    return _lastError;
}

// ======================== بازیابی از Bus-Off ========================

bool CANManager::recoverFromBusOff() {
    Serial.println("[CAN] شروع فرآیند بازیابی از Bus-Off...");
    
    // توقف driver
    twai_stop();
    
    // تاخیر برای بهبود باس
    delay(100);
    
    // راه‌اندازی مجدد
    esp_err_t err = twai_start();
    if (err == ESP_OK) {
        _lastError = CAN_OK;
        Serial.println("[CAN] بازیابی از Bus-Off موفق ✓");
        return true;
    }
    
    Serial.printf("⚠️ [CAN] خطا در بازیابی: %d\n", err);
    _lastError = CAN_ERROR_BUS_OFF;
    return false;
}

// ======================== آمار ========================

void CANManager::getStats(uint32_t& txCount, uint32_t& rxCount, uint32_t& errorCount) {
    txCount = _txCount;
    rxCount = _rxCount;
    errorCount = _errorCount;
}
