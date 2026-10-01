#include "ble_manager.h"

#include <NimBLEDevice.h>
#include <Update.h>
#include "config.h"

namespace {
static const char* BLE_SERVICE_UUID  = "6E400001-B5A3-F393-E0A9-E50E24DCCA9E";
static const char* BLE_STATUS_UUID   = "6E400003-B5A3-F393-E0A9-E50E24DCCA9E";
static const char* BLE_COMMAND_UUID  = "6E400004-B5A3-F393-E0A9-E50E24DCCA9E";
static const char* BLE_DATA_UUID     = "6E400005-B5A3-F393-E0A9-E50E24DCCA9E";

BLECharacteristic* gStatus = nullptr;
BLECharacteristic* gCommand = nullptr;
BLECharacteristic* gData = nullptr;
BLEServer* gServer = nullptr;
BLEManager* gManager = nullptr;
}

class BLEManager::ServerCallbacks : public NimBLEServerCallbacks {
    void onConnect(NimBLEServer*, NimBLEConnInfo&) override {
        if (gManager) gManager->_connected = true;
        if (gStatus) gStatus->setValue("CONNECTED");
    }

    void onDisconnect(NimBLEServer*, NimBLEConnInfo&, int) override {
        if (gManager) {
            gManager->_connected = false;
            if (gManager->_otaInProgress) gManager->_abortOta();
        }
        if (gServer) gServer->startAdvertising();
    }
};

class BLEManager::CommandCallbacks : public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic* characteristic, NimBLEConnInfo&) override {
        if (!gManager) return;
        gManager->_handleCommand(characteristic->getValue().c_str());
    }
};

class BLEManager::DataCallbacks : public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic* characteristic, NimBLEConnInfo&) override {
        if (!gManager || !gManager->_otaInProgress || !gManager->_otaAuthenticated) return;

        std::string value = characteristic->getValue();
        if (value.empty()) return;

        size_t written = Update.write(reinterpret_cast<uint8_t*>(const_cast<char*>(value.data())), value.size());
        if (written != value.size()) {
            gManager->_otaError = true;
            gManager->_abortOta();
            if (gStatus) gStatus->setValue("OTA_WRITE_ERROR");
            return;
        }

        gManager->_otaReceived += static_cast<uint32_t>(written);
        if (gStatus) {
            String status = "OTA_PROGRESS:" + String(gManager->_otaReceived) + ":" + String(gManager->_otaExpected);
            gStatus->setValue(status.c_str());
            gStatus->notify();
        }
    }
};

BLEManager bleManager;

BLEManager::BLEManager()
    : _started(false), _connected(false), _otaInProgress(false),
      _otaAuthenticated(false), _otaError(false), _otaExpected(0),
      _otaReceived(0), _rebootAt(0), _deviceName("CarTouch") {}

bool BLEManager::begin() {
    if (_started) return true;

    uint64_t chipId = ESP.getEfuseMac();
    char name[32];
    snprintf(name, sizeof(name), "CarTouch-%04X", static_cast<unsigned>(chipId & 0xFFFF));
    _deviceName = name;

    NimBLEDevice::init(_deviceName.c_str());
    NimBLEDevice::setPower(9);
    // Encrypt BLE links. Pairing uses Just Works because the device has no
    // dedicated keyboard/display for a pairing PIN; the OTA command still
    // requires the current Web password at the application layer.
    NimBLEDevice::setSecurityAuth(true, false, true);    // bonding, no MITM (Just Works), LE Secure Connections
    NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);

    gManager = this;
    gServer = NimBLEDevice::createServer();
    gServer->setCallbacks(new ServerCallbacks());

    NimBLEService* service = gServer->createService(BLE_SERVICE_UUID);
    gStatus = service->createCharacteristic(BLE_STATUS_UUID, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);
    gCommand = service->createCharacteristic(BLE_COMMAND_UUID, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR | NIMBLE_PROPERTY::WRITE_ENC);
    gData = service->createCharacteristic(BLE_DATA_UUID, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR | NIMBLE_PROPERTY::WRITE_ENC);

    gCommand->setCallbacks(new CommandCallbacks());
    gData->setCallbacks(new DataCallbacks());
    gStatus->setValue("READY");

    NimBLEAdvertising* advertising = NimBLEDevice::getAdvertising();
    advertising->addServiceUUID(BLE_SERVICE_UUID);
    advertising->setName(_deviceName.c_str());
    advertising->start();

    _started = true;
    Serial.printf("[BLE] Started: %s\n", _deviceName.c_str());
    return true;
}

void BLEManager::update() {
    if (_rebootAt != 0 && static_cast<int32_t>(millis() - _rebootAt) >= 0) {
        Serial.println("[BLE OTA] Rebooting to apply firmware update...");
        delay(100);
        ESP.restart();
    }
}

void BLEManager::_sendStatus(const char* status) {
    if (gStatus) {
        gStatus->setValue(status);
        if (_connected) gStatus->notify();
    }
}

void BLEManager::_handleCommand(const String& command) {
    String cmd = command;
    cmd.trim();

    if (cmd.equalsIgnoreCase("STATUS")) {
        String status = "READY:" + String(_otaInProgress ? "OTA" : "IDLE");
        _sendStatus(status.c_str());
        return;
    }

    if (cmd.equalsIgnoreCase("ABORT")) {
        _abortOta();
        _sendStatus("OTA_ABORTED");
        return;
    }

    if (cmd.equalsIgnoreCase("END")) {
        if (!_otaInProgress || !_otaAuthenticated) {
            _sendStatus("OTA_NOT_STARTED");
            return;
        }
        if (_otaReceived != _otaExpected) {
            _sendStatus("OTA_SIZE_MISMATCH");
            _abortOta();
            return;
        }
        if (!_finishOta()) {
            _sendStatus("OTA_FINALIZE_ERROR");
            return;
        }
        _sendStatus("OTA_OK_REBOOTING");
        _rebootAt = millis() + 1500;
        return;
    }

    if (cmd.startsWith("START:")) {
        int first = cmd.indexOf(':');
        int second = cmd.lastIndexOf(':');    // size is after the LAST ':' so passwords may contain ':'
        if (second <= first) second = -1;
        if (second < 0) {
            _sendStatus("OTA_BAD_COMMAND");
            return;
        }

        if (isUsingDefaultPassword()) {
            _sendStatus("OTA_CHANGE_DEFAULT_PASSWORD");
            return;
        }
        String password = cmd.substring(first + 1, second);
        uint32_t size = static_cast<uint32_t>(cmd.substring(second + 1).toInt());
        if (size == 0) {
            _sendStatus("OTA_BAD_SIZE");
            return;
        }
        if (!_startOta(size, password)) {
            _sendStatus(_otaError ? "OTA_AUTH_OR_START_ERROR" : "OTA_START_ERROR");
        } else {
            _sendStatus("OTA_STARTED");
        }
        return;
    }

    _sendStatus("UNKNOWN_COMMAND");
}

bool BLEManager::_startOta(uint32_t size, const String& password) {
    _otaError = false;
    _otaAuthenticated = false;
    _otaExpected = 0;
    _otaReceived = 0;

    AppConfig* cfg = getConfig();
    if (isUsingDefaultPassword()) {    // never allow OTA with the public default
        _otaError = true;
        return false;
    }
    if (_otaLockUntil != 0 && (int32_t)(millis() - _otaLockUntil) < 0) {
        _otaError = true;    // locked out after repeated failures
        return false;
    }
    {
        // constant-time comparison
        const char* expected = cfg->webPass;
        size_t el = strlen(expected), pl = password.length();
        uint8_t diff = (uint8_t)(el != pl);
        for (size_t i = 0; i < el; ++i) diff |= (uint8_t)(expected[i] ^ (i < pl ? password[i] : 0));
        if (diff != 0) {
            _otaError = true;
            if (++_otaFailCount >= 5) {
                _otaFailCount = 0;
                _otaLockUntil = millis() + 60000;
                if (_otaLockUntil == 0) _otaLockUntil = 1;
            }
            return false;
        }
        _otaFailCount = 0;
        _otaLockUntil = 0;
    }

    if (_otaInProgress || Update.isRunning()) {
        _abortOta();
    }

    if (!Update.begin(size, U_FLASH)) {
        _otaError = true;
        Serial.printf("[BLE OTA] Update.begin failed: %s\n", Update.errorString());
        return false;
    }

    _otaExpected = size;
    _otaReceived = 0;
    _otaAuthenticated = true;
    _otaInProgress = true;
    Serial.printf("[BLE OTA] Started: %u bytes\n", (unsigned)size);
    return true;
}

void BLEManager::_abortOta() {
    if (Update.isRunning()) Update.abort();
    _otaInProgress = false;
    _otaAuthenticated = false;
    _otaExpected = 0;
    _otaReceived = 0;
}

bool BLEManager::_finishOta() {
    if (!Update.isRunning()) return false;
    if (!Update.end(true)) {
        Serial.printf("[BLE OTA] Finalize failed: %s\n", Update.errorString());
        _otaError = true;
        _abortOta();
        return false;
    }

    _otaInProgress = false;
    _otaAuthenticated = false;
    Serial.printf("[BLE OTA] Finished successfully: %u bytes\n", (unsigned)_otaReceived);
    return true;
}

bool BLEManager::isEnabled() const { return _started; }
bool BLEManager::isConnected() const { return _connected; }
bool BLEManager::isOtaInProgress() const { return _otaInProgress; }
uint32_t BLEManager::otaBytesReceived() const { return _otaReceived; }
uint32_t BLEManager::otaExpectedBytes() const { return _otaExpected; }
const char* BLEManager::deviceName() const { return _deviceName.c_str(); }
