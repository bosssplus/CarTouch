#include <unity.h>
#include <stdint.h>
#include <string.h>
#include "ct_tx_guard.h"
#include "ct_index_parser.h"
#include "ct_time.h"
#include "ct_hex_parser.h"
#include "ct_obd_parser.h"
#include "ct_dbc_validation.h"
#include "ct_verify.h"
#include "ct_json_validation.h"
#include "ct_battery.h"
#include "ct_can_config.h"
#include "can_service.h"

void setUp(void) {}
void tearDown(void) {}

class MockCanInterface : public CanInterface {
public:
    bool beginResult = true;
    bool active = false;
    bool listenOnly = true;
    bool lastReconfigure = false;
    uint8_t beginCalls = 0;
    uint8_t sendCalls = 0;
    uint8_t receiveCalls = 0;
    uint8_t reconfigureCalls = 0;
    uint32_t lastTxId = 0;

    bool begin() override {
        ++beginCalls;
        active = beginResult;
        return beginResult;
    }
    void end() override { active = false; }
    bool sendMessage(const CanMessage& msg, uint32_t) override {
        ++sendCalls;
        lastTxId = msg.id;
        return active && !listenOnly;
    }
    bool receiveMessage(CanMessage& msg, uint32_t) override {
        ++receiveCalls;
        msg.id = 0x123;
        return active;
    }
    bool receiveMessageNonBlocking(CanMessage& msg) override {
        ++receiveCalls;
        msg.id = 0x123;
        return active;
    }
    void flushRxQueue() override {}
    bool isActive() override { return active; }
    CanError getLastError() override { return CAN_OK; }
    bool recoverFromBusOff() override { return active; }
    void getStats(uint32_t& tx, uint32_t& rx, uint32_t& errors) override {
        tx = rx = errors = 0;
    }
    bool getDiagnostics(CanDiagnostics& out) override {
        memset(&out, 0, sizeof(out));
        out.driverReady = active;
        return active;
    }
    uint32_t getLastRxTime() const override { return 0; }
    bool reconfigureMode(bool mode) override {
        ++reconfigureCalls;
        lastReconfigure = mode;
        listenOnly = mode;
        return active;
    }
    bool isListenOnlyActive() override { return active && listenOnly; }
};

void test_tx_guard_listen_only(void) {
    TEST_ASSERT_EQUAL(CT_TX_ERR_LISTEN_ONLY, ctTxGuard(true, true, 8));
    TEST_ASSERT_EQUAL(CT_TX_ERR_LISTEN_ONLY, ctTxGuard(true, true, 0));
}
void test_tx_guard_initialization_and_dlc(void) {
    TEST_ASSERT_EQUAL(CT_TX_ERR_NOT_INITIALIZED, ctTxGuard(false, false, 8));
    TEST_ASSERT_EQUAL(CT_TX_ERR_LENGTH, ctTxGuard(true, false, 9));
    TEST_ASSERT_EQUAL(CT_TX_OK, ctTxGuard(true, false, 8));
}
void test_tx_id_validity(void) {
    TEST_ASSERT_TRUE(ctTxIdValid(0x7FF, false));
    TEST_ASSERT_FALSE(ctTxIdValid(0x800, false));
    TEST_ASSERT_FALSE(ctTxIdValid(0x1234, false));
    TEST_ASSERT_TRUE(ctTxIdValid(0x1FFFFFFF, true));
    TEST_ASSERT_FALSE(ctTxIdValid(0x20000000, true));
}
void test_battery_voltage_availability(void) {
    TEST_ASSERT_FALSE(ctBatteryVoltageAvailable(0.0f));
    TEST_ASSERT_FALSE(ctBatteryVoltageAvailable(-1.0f));
    TEST_ASSERT_FALSE(ctBatteryVoltageAvailable(5.9f));
    TEST_ASSERT_TRUE(ctBatteryVoltageAvailable(6.0f));
    TEST_ASSERT_TRUE(ctBatteryVoltageAvailable(12.6f));
    TEST_ASSERT_TRUE(ctBatteryVoltageAvailable(36.0f));
    TEST_ASSERT_FALSE(ctBatteryVoltageAvailable(36.1f));
}
void test_can_frame_identity_includes_format(void) {
    TEST_ASSERT_TRUE(ctSameCanFrameId(0x123, false, 0x123, false));
    TEST_ASSERT_TRUE(ctSameCanFrameId(0x123, true, 0x123, true));
    TEST_ASSERT_FALSE(ctSameCanFrameId(0x123, false, 0x123, true));
    TEST_ASSERT_FALSE(ctSameCanFrameId(0x123, true, 0x124, true));
}
void test_can_bus_pin_conflicts_are_rejected(void) {
    TEST_ASSERT_TRUE(ctCanPinsConflictFree(9, 6, 15, 16));
    TEST_ASSERT_FALSE(ctCanPinsConflictFree(15, 6, 15, 16));
    TEST_ASSERT_FALSE(ctCanPinsConflictFree(9, 16, 15, 16));
    TEST_ASSERT_FALSE(ctCanPinsConflictFree(9, 6, 16, 16));
}
void test_mcp2515_supported_bitrates(void) {
    TEST_ASSERT_TRUE(ctMcp2515BitrateValid(100000));
    TEST_ASSERT_TRUE(ctMcp2515BitrateValid(125000));
    TEST_ASSERT_TRUE(ctMcp2515BitrateValid(250000));
    TEST_ASSERT_TRUE(ctMcp2515BitrateValid(500000));
    TEST_ASSERT_TRUE(ctMcp2515BitrateValid(1000000));
    TEST_ASSERT_FALSE(ctMcp2515BitrateValid(800000));
    TEST_ASSERT_FALSE(ctMcp2515BitrateValid(0));
}
void test_can_service_initializes_each_bus_independently(void) {
    MockCanInterface can0;
    MockCanInterface can1;
    can0.beginResult = false;
    CANService service(can0, can1);
    TEST_ASSERT_FALSE(service.begin());
    TEST_ASSERT_EQUAL(1, can0.beginCalls);
    TEST_ASSERT_EQUAL(1, can1.beginCalls);
    TEST_ASSERT_FALSE(service.isActive(CAN_BUS_0));
    TEST_ASSERT_TRUE(service.isActive(CAN_BUS_1));
}
void test_can_service_routes_legacy_calls_to_can0(void) {
    MockCanInterface can0;
    MockCanInterface can1;
    can0.active = can1.active = true;
    can0.listenOnly = can1.listenOnly = false;
    CANService service(can0, can1);
    CanMessage message = {};
    message.id = 0x321;
    TEST_ASSERT_TRUE(service.sendMessage(message));
    TEST_ASSERT_EQUAL(1, can0.sendCalls);
    TEST_ASSERT_EQUAL(0, can1.sendCalls);
    TEST_ASSERT_TRUE(service.sendMessage(CAN_BUS_1, message));
    TEST_ASSERT_EQUAL(1, can0.sendCalls);
    TEST_ASSERT_EQUAL(1, can1.sendCalls);
}
void test_bounded_index_parser_rejects_wraparound(void) {
    uint8_t index = 0;
    TEST_ASSERT_TRUE(ctParseBoundedIndex("7", 8, index));
    TEST_ASSERT_EQUAL(7, index);
    TEST_ASSERT_FALSE(ctParseBoundedIndex("8", 8, index));
    TEST_ASSERT_FALSE(ctParseBoundedIndex("256", 8, index));
    TEST_ASSERT_FALSE(ctParseBoundedIndex("65543", 8, index));
    TEST_ASSERT_FALSE(ctParseBoundedIndex("-1", 8, index));
    TEST_ASSERT_FALSE(ctParseBoundedIndex("1x", 8, index));
    TEST_ASSERT_FALSE(ctParseBoundedIndex("", 8, index));
    TEST_ASSERT_TRUE(ctParseBoundedIndex("9", 10, index));
    TEST_ASSERT_EQUAL(9, index);
    TEST_ASSERT_FALSE(ctParseBoundedIndex("10", 10, index));
}
void test_strict_decimal_and_boolean_parsing(void) {
    uint32_t value = 0;
    bool flag = false;
    TEST_ASSERT_TRUE(ctParseUnsignedDecimal("500000", 1000000, value));
    TEST_ASSERT_EQUAL_UINT32(500000, value);
    TEST_ASSERT_FALSE(ctParseUnsignedDecimal("500000x", 1000000, value));
    TEST_ASSERT_FALSE(ctParseUnsignedDecimal("4294967296", UINT32_MAX, value));
    TEST_ASSERT_TRUE(ctParseBoolean("true", flag));
    TEST_ASSERT_TRUE(flag);
    TEST_ASSERT_TRUE(ctParseBoolean("false", flag));
    TEST_ASSERT_FALSE(flag);
    TEST_ASSERT_FALSE(ctParseBoolean("garbage", flag));
}
void test_wrap_safe_timer(void) {
    const uint32_t start = 0xFFFFFF00u;
    const uint32_t now = 0x00000100u;
    TEST_ASSERT_TRUE(ctElapsedAtLeast(now, start, 512));
    TEST_ASSERT_FALSE(ctElapsedAtLeast(now, start, 513));
    TEST_ASSERT_TRUE(ctElapsedMoreThan(now, start, 511));
    TEST_ASSERT_FALSE(ctElapsedMoreThan(now, start, 512));
}

void test_hex_standard_and_extended(void) {
    uint32_t v = 0;
    TEST_ASSERT_TRUE(ctParseHexUint32("7FF", v, 8)); TEST_ASSERT_EQUAL_HEX32(0x7FF, v);
    TEST_ASSERT_TRUE(ctParseHexUint32("0x1ABCDE", v, 8)); TEST_ASSERT_EQUAL_HEX32(0x1ABCDE, v);
    TEST_ASSERT_TRUE(ctParseHexUint32("ABCDEF01", v, 8)); TEST_ASSERT_EQUAL_HEX32(0xABCDEF01, v);
}
void test_hex_rejects_bad_input(void) {
    uint32_t v = 0; uint8_t b = 0;
    TEST_ASSERT_FALSE(ctParseHexUint32("", v, 8));
    TEST_ASSERT_FALSE(ctParseHexUint32("0x", v, 8));
    TEST_ASSERT_FALSE(ctParseHexUint32("12G4", v, 8));
    TEST_ASSERT_FALSE(ctParseHexUint32("123456789", v, 8));
    TEST_ASSERT_FALSE(ctParseHexUint32("7FFjunk", v, 8));
    TEST_ASSERT_TRUE(ctParseHexByteToken("FF", b)); TEST_ASSERT_EQUAL_HEX8(0xFF, b);
    TEST_ASSERT_FALSE(ctParseHexByteToken("100", b));
    TEST_ASSERT_FALSE(ctParseHexByteToken("GG", b));
}

void test_obd_valid_pid_response_uses_pci_length(void) {
    const uint8_t frame[] = {0x06, 0x41, 0x0C, 0x1A, 0xF8, 0xAA, 0xBB, 0xCC};
    CtObdSingleFrame p;
    TEST_ASSERT_TRUE(ctParseObdSingleFrame(frame, 8, 0x41, 0x0C, p));
    TEST_ASSERT_EQUAL(6, p.payloadLength);
    TEST_ASSERT_EQUAL(3, p.dataOffset);
}
void test_obd_reply_requires_standard_data_frame(void) {
    TEST_ASSERT_TRUE(ctIsObdReplyFrame(0x7E8, false, false));
    TEST_ASSERT_TRUE(ctIsObdReplyFrame(0x7E9, false, false));
    TEST_ASSERT_FALSE(ctIsObdReplyFrame(0x7E8, true, false));
    TEST_ASSERT_FALSE(ctIsObdReplyFrame(0x7E8, false, true));
    TEST_ASSERT_FALSE(ctIsObdReplyFrame(0x7EA, false, false));
}
void test_obd_rejects_inconsistent_dlc_and_pci(void) {
    CtObdSingleFrame p;
    const uint8_t shortFrame[] = {0x06, 0x41, 0x0C, 0x1A};
    const uint8_t multiFrame[] = {0x10, 0x06, 0x41, 0x0C, 0x1A, 0xF8, 0x00, 0x00};
    TEST_ASSERT_FALSE(ctParseObdSingleFrame(shortFrame, sizeof(shortFrame), 0x41, 0x0C, p));
    TEST_ASSERT_FALSE(ctParseObdSingleFrame(multiFrame, sizeof(multiFrame), 0x41, 0x0C, p));
}
void test_obd_rejects_wrong_service_or_pid(void) {
    CtObdSingleFrame p;
    const uint8_t frame[] = {0x06, 0x41, 0x0D, 0x40, 0, 0, 0, 0};
    TEST_ASSERT_FALSE(ctParseObdSingleFrame(frame, 8, 0x41, 0x0C, p));
    TEST_ASSERT_FALSE(ctParseObdSingleFrame(frame, 8, 0x43, 0xFF, p));
}
void test_dtc_response_and_padding(void) {
    const uint8_t frame[] = {0x07, 0x43, 0x01, 0x23, 0x00, 0x00, 0x00, 0x00};
    CtObdSingleFrame p;
    TEST_ASSERT_TRUE(ctParseObdSingleFrame(frame, 8, 0x43, 0xFF, p));
    TEST_ASSERT_EQUAL(2, p.dataOffset);
    TEST_ASSERT_EQUAL_HEX16(0x0123, (uint16_t)((frame[2] << 8) | frame[3]));
    TEST_ASSERT_EQUAL_HEX16(0x0000, (uint16_t)((frame[4] << 8) | frame[5]));
    TEST_ASSERT_TRUE(ctDtcPayloadHasValidPairLength(1));
    TEST_ASSERT_TRUE(ctDtcPayloadHasValidPairLength(3));
    TEST_ASSERT_FALSE(ctDtcPayloadHasValidPairLength(2));
}

void test_dbc_intel_boundaries(void) {
    TEST_ASSERT_TRUE(ctDbcSignalFitsDlc(0, 1, false, 1));
    TEST_ASSERT_TRUE(ctDbcSignalFitsDlc(0, 8, false, 1));
    TEST_ASSERT_TRUE(ctDbcSignalFitsDlc(7, 2, false, 2));
    TEST_ASSERT_FALSE(ctDbcSignalFitsDlc(7, 2, false, 1));
    TEST_ASSERT_TRUE(ctDbcSignalFitsDlc(0, 64, false, 8));
}
void test_dbc_motorola_boundaries(void) {
    TEST_ASSERT_TRUE(ctDbcSignalFitsDlc(7, 8, true, 1));
    TEST_ASSERT_TRUE(ctDbcSignalFitsDlc(7, 16, true, 2));
    TEST_ASSERT_FALSE(ctDbcSignalFitsDlc(7, 16, true, 1));
    TEST_ASSERT_TRUE(ctDbcSignalFitsDlc(63, 2, true, 8));
    TEST_ASSERT_FALSE(ctDbcSignalFitsDlc(0, 64, true, 8));
}
void test_dbc_invalid_lengths_and_start(void) {
    TEST_ASSERT_FALSE(ctDbcSignalFitsDlc(0, 0, false, 8));
    TEST_ASSERT_FALSE(ctDbcSignalFitsDlc(0, 65, false, 8));
    TEST_ASSERT_FALSE(ctDbcSignalFitsDlc(64, 1, false, 8));
}

void test_verification_fingerprint_changes_with_command(void) {
    const uint8_t a[] = {0x01, 0x02};
    const uint8_t b[] = {0x01, 0x03};
    uint32_t fa = ctVerifyFingerprint(1, 1, "lock", 0x123, false, 2, a);
    TEST_ASSERT_NOT_EQUAL(0, fa);
    TEST_ASSERT_NOT_EQUAL(fa, ctVerifyFingerprint(1, 1, "lock", 0x123, false, 2, b));
    TEST_ASSERT_NOT_EQUAL(fa, ctVerifyFingerprint(2, 1, "lock", 0x123, false, 2, a));
    TEST_ASSERT_NOT_EQUAL(fa, ctVerifyFingerprint(1, 1, "unlock", 0x123, false, 2, a));
}



void test_dbc_extended_id_decode(void) {
    uint32_t id = 0;
    bool extended = false;
    TEST_ASSERT_TRUE(ctDecodeDbcCanId(0x123, id, extended));
    TEST_ASSERT_EQUAL_HEX32(0x123, id);
    TEST_ASSERT_FALSE(extended);

    TEST_ASSERT_TRUE(ctDecodeDbcCanId(0x80000123UL, id, extended));
    TEST_ASSERT_EQUAL_HEX32(0x123, id);
    TEST_ASSERT_TRUE(extended);

    // Bundled GM vendor/OpenDBC files contain raw 29-bit IDs without the
    // DBC bit-31 marker; these must still become Extended CAN frames.
    TEST_ASSERT_TRUE(ctDecodeDbcCanId(0x10630000UL, id, extended));
    TEST_ASSERT_EQUAL_HEX32(0x10630000UL, id);
    TEST_ASSERT_TRUE(extended);
}

void test_json_command_validation(void) {
    JsonDocument doc;
    JsonObject item = doc.to<JsonObject>();
    item["label"] = "lock_all";
    item["displayName"] = "Lock";
    item["canId"] = 0x123;
    item["extended"] = false;
    item["length"] = 2;
    item["data"].to<JsonArray>().add(1);
    item["data"].as<JsonArray>().add(2);
    item["source"] = 2;
    item["status"] = "verified";
    CtJsonCommandFields f;
    TEST_ASSERT_TRUE(ctValidateImportedCommand(item, f));
    TEST_ASSERT_EQUAL_HEX32(0x123, f.canId);
    TEST_ASSERT_EQUAL(2, f.length);
}
void test_json_command_rejects_invalid_types_and_ranges(void) {
    JsonDocument doc;
    JsonObject item = doc.to<JsonObject>();
    item["label"] = "x"; item["displayName"] = "x"; item["canId"] = 0x800;
    item["extended"] = false; item["length"] = 1; item["data"].to<JsonArray>().add(1);
    item["source"] = 2; item["status"] = "unverified";
    CtJsonCommandFields f;
    TEST_ASSERT_FALSE(ctValidateImportedCommand(item, f));
    item["canId"] = 0x123; item["extended"] = "false";
    TEST_ASSERT_FALSE(ctValidateImportedCommand(item, f));
    item["extended"] = false; item["source"] = 99;
    TEST_ASSERT_FALSE(ctValidateImportedCommand(item, f));
    item["source"] = 2; char longName[49]; memset(longName, 'x', 48); longName[48] = '\0'; item["displayName"] = longName;
    TEST_ASSERT_FALSE(ctValidateImportedCommand(item, f));
}

void test_verification_transaction_rejections(void) {
    const char* label = "lock";
    TEST_ASSERT_TRUE(ctVerifyTransactionValid(true, 1, label, 77, 1000, 1, label, 77, 1500, 120000));
    TEST_ASSERT_FALSE(ctVerifyTransactionValid(false, 1, label, 77, 1000, 1, label, 77, 1500, 120000));
    TEST_ASSERT_FALSE(ctVerifyTransactionValid(true, 1, label, 77, 1000, 2, label, 77, 1500, 120000));
    TEST_ASSERT_FALSE(ctVerifyTransactionValid(true, 1, label, 77, 1000, 1, "unlock", 77, 1500, 120000));
    TEST_ASSERT_FALSE(ctVerifyTransactionValid(true, 1, label, 77, 1000, 1, label, 78, 1500, 120000));
    TEST_ASSERT_FALSE(ctVerifyTransactionValid(true, 1, label, 77, 1000, 1, label, 77, 121001, 120000));
}
int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_tx_guard_listen_only);
    RUN_TEST(test_tx_guard_initialization_and_dlc);
    RUN_TEST(test_tx_id_validity);
    RUN_TEST(test_battery_voltage_availability);
    RUN_TEST(test_can_frame_identity_includes_format);
    RUN_TEST(test_can_bus_pin_conflicts_are_rejected);
    RUN_TEST(test_mcp2515_supported_bitrates);
    RUN_TEST(test_can_service_initializes_each_bus_independently);
    RUN_TEST(test_can_service_routes_legacy_calls_to_can0);
    RUN_TEST(test_bounded_index_parser_rejects_wraparound);
    RUN_TEST(test_strict_decimal_and_boolean_parsing);
    RUN_TEST(test_wrap_safe_timer);
    RUN_TEST(test_hex_standard_and_extended);
    RUN_TEST(test_hex_rejects_bad_input);
    RUN_TEST(test_obd_valid_pid_response_uses_pci_length);
    RUN_TEST(test_obd_reply_requires_standard_data_frame);
    RUN_TEST(test_obd_rejects_inconsistent_dlc_and_pci);
    RUN_TEST(test_obd_rejects_wrong_service_or_pid);
    RUN_TEST(test_dtc_response_and_padding);
    RUN_TEST(test_dbc_intel_boundaries);
    RUN_TEST(test_dbc_motorola_boundaries);
    RUN_TEST(test_dbc_invalid_lengths_and_start);
    RUN_TEST(test_verification_fingerprint_changes_with_command);
    RUN_TEST(test_verification_transaction_rejections);
    RUN_TEST(test_dbc_extended_id_decode);
    RUN_TEST(test_json_command_validation);
    RUN_TEST(test_json_command_rejects_invalid_types_and_ranges);
    return UNITY_END();
}
