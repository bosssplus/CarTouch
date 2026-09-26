/**
 * wifi_manager.cpp - WiFi manager implementation
 */

#include "wifi_manager.h"
#include "error_log.h"

// ============================================================================
// Constructor
// ============================================================================

WiFiManager::WiFiManager() {
    _state    = CT_WIFI_DISABLED;
    _enabled  = true;
}

// ============================================================================
// begin()
// ============================================================================

void WiFiManager::begin(uint8_t mode) {
    if (!_enabled) {
        Serial.println("[WiFi] WiFi is disabled");
        _state = CT_WIFI_DISABLED;
        return;
    }

    Serial.println("[WiFi] Starting WiFi...");

    switch (mode) {
        case 0:
            WiFi.mode(WIFI_OFF);
            _state = CT_WIFI_DISABLED;
            Serial.println("[WiFi] WiFi turned off");
            break;

        case 1:
            _startAP();
            break;

        case 2:
            _startSTA();
            break;

        default:
            _startAP();
            break;
    }
}

// ============================================================================
// Access Point mode
// ============================================================================

void WiFiManager::_startAP() {
    WiFi.mode(WIFI_AP);

    bool result = WiFi.softAP(WIFI_AP_NAME, WIFI_AP_PASSWORD);

    if (result) {
        _state = CT_WIFI_AP;
        Serial.printf("[WiFi] Access Point: %s | IP: %s\n",
                      WIFI_AP_NAME, WiFi.softAPIP().toString().c_str());
        Serial.printf("[WiFi] Password: %s\n", WIFI_AP_PASSWORD);
    } else {
        _state = CT_WIFI_DISABLED;
        Serial.println("[WiFi] Failed to create Access Point");
    }
}

// ============================================================================
// Station mode
// ============================================================================

void WiFiManager::_startSTA() {
    AppConfig* cfg = getConfig();

    if (strlen(cfg->wifiSSID) == 0) {
        Serial.println("[WiFi] No saved SSID - falling back to AP mode");
        _startAP();
        return;
    }

    WiFi.mode(WIFI_STA);
    WiFi.begin(cfg->wifiSSID, cfg->wifiPassword);

    Serial.printf("[WiFi] Connecting to %s...\n", cfg->wifiSSID);

    int retry = 0;
    while (WiFi.status() != WL_CONNECTED && retry < WIFI_MAX_RETRY) {
        delay(500);
        Serial.print(".");
        retry++;
    }

    if (WiFi.status() == WL_CONNECTED) {
        _state = CT_WIFI_STA;
        Serial.printf("\n[WiFi] Connected! IP: %s\n", WiFi.localIP().toString().c_str());
    } else {
        _state = CT_WIFI_STA_FAIL;
        getErrorLog()->log(LOG_CAT_WIFI, LOG_WARN, "Connect failed - falling back to AP mode");
        _startAP();
    }
}

// ============================================================================
// Disconnect
// ============================================================================

void WiFiManager::disconnect() {
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    _state = CT_WIFI_DISABLED;
    Serial.println("[WiFi] WiFi disconnected");
}

// ============================================================================
// Network scan
// ============================================================================

uint8_t WiFiManager::scanNetworks(char networks[][32], uint8_t maxCount) {
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    delay(100);

    int count = WiFi.scanNetworks();
    if (count < 0) {
        Serial.println("[WiFi] Scan failed");
        return 0;
    }

    Serial.printf("[WiFi] Found %d networks\n", count);

    uint8_t result = 0;
    for (int i = 0; i < count && result < maxCount; i++) {
        String ssid = WiFi.SSID(i);
        if (ssid.length() > 0) {
            strncpy(networks[result], ssid.c_str(), 32);
            networks[result][31] = '\0';
            Serial.printf("  %d: %s\n", result + 1, networks[result]);
            result++;
        }
    }

    return result;
}

// ============================================================================
// Connect to a network
// ============================================================================

bool WiFiManager::connectToNetwork(const char* ssid, const char* password) {
    AppConfig* cfg = getConfig();
    strncpy(cfg->wifiSSID, ssid, sizeof(cfg->wifiSSID) - 1);
    strncpy(cfg->wifiPassword, password, sizeof(cfg->wifiPassword) - 1);
    saveConfig();

    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);

    int retry = 0;
    while (WiFi.status() != WL_CONNECTED && retry < WIFI_MAX_RETRY) {
        delay(500);
        retry++;
    }

    if (WiFi.status() == WL_CONNECTED) {
        _state = CT_WIFI_STA;
        Serial.printf("[WiFi] Connected to %s. IP: %s\n", ssid, WiFi.localIP().toString().c_str());
        return true;
    }

    getErrorLog()->log(LOG_CAT_WIFI, LOG_WARN, "Failed to connect to %s", ssid);
    _state = CT_WIFI_STA_FAIL;
    return false;
}

// ============================================================================
// Status accessors
// ============================================================================

WiFiState WiFiManager::getState() {
    return _state;
}

IPAddress WiFiManager::getIP() {
    if (_state == CT_WIFI_AP) {
        return WiFi.softAPIP();
    } else if (_state == CT_WIFI_STA) {
        return WiFi.localIP();
    }
    return IPAddress(0, 0, 0, 0);
}

bool WiFiManager::isConnected() {
    return (_state == CT_WIFI_STA && WiFi.status() == WL_CONNECTED) ||
           (_state == CT_WIFI_AP);
}

bool WiFiManager::isEnabled() {
    return _enabled;
}

void WiFiManager::setEnabled(bool enabled) {
    _enabled = enabled;
    if (!enabled) {
        disconnect();
    }
}
