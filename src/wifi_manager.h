/**
 * wifi_manager.h - WiFi connectivity management
 *
 * Handles connecting the ESP32 to a WiFi network, or creating its own
 * Access Point. In AP mode the device is reachable directly; in STA
 * mode it joins an existing home/office network.
 */

#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <Arduino.h>
#include <WiFi.h>
#include "config.h"

// WiFi state. Note: the CT_ prefix is used because WIFI_AP and
// WIFI_STA are already defined as macros for WIFI_MODE_AP/WIFI_MODE_STA
// in the ESP32 Arduino core - reusing those names caused a "conflicts
// with a previous declaration" compile error.
enum WiFiState : uint8_t {
    CT_WIFI_DISABLED = 0,
    CT_WIFI_AP       = 1,   // Access Point mode
    CT_WIFI_STA      = 2,   // Station mode (connected to a router)
    CT_WIFI_STA_FAIL = 3    // Station mode failed to connect
};

class WiFiManager {
public:
    WiFiManager();

    /**
     * Starts WiFi.
     * @param mode 0 = off, 1 = AP, 2 = STA (attempts to connect)
     */
    void begin(uint8_t mode = 1);

    void disconnect();

    /**
     * Scans for available networks.
     * @param networks [out] array to store SSIDs
     * @param maxCount  max entries to return
     * @return number of networks found
     */
    uint8_t scanNetworks(char networks[][32], uint8_t maxCount = 10);

    /** Connects to a specific network. Returns true on success. */
    bool connectToNetwork(const char* ssid, const char* password);

    WiFiState getState();

    /** Returns the IP address (AP or STA mode). */
    IPAddress getIP();

    bool isConnected();
    bool isEnabled();
    void setEnabled(bool enabled);

private:
    WiFiState _state;
    bool       _enabled;

    void _startAP();
    void _startSTA();
};

#endif // WIFI_MANAGER_H
