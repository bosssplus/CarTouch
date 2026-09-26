/**
 * tft_ui.h - Touchscreen UI, built on LVGL (CarTouch v2.0)
 *
 * The project's primary user interface, using LVGL 8.
 *
 * Four main tabs:
 *   1. Control   - large control buttons
 *   2. Dashboard - live vehicle data
 *   3. Settings  - device settings
 *   4. Learn     - learn a command from live CAN traffic, manual entry,
 *                  and the custom-profile list with verification status
 *                  (see CarTouch_SPEC.md section 7.4)
 *
 * Learn Mode wiring: to keep layers decoupled (matching the existing
 * UIControlCallback pattern), this class does not depend directly on
 * LearnEngine/CustomVehicleStore - it holds pointers injected by
 * main.cpp instead, via attachLearnModules(). This keeps tft_ui.h free
 * of the full Learn Mode header set.
 */

#ifndef TFT_UI_H
#define TFT_UI_H

#include <Arduino.h>
#include <lvgl.h>
#include "config.h"

typedef void (*UIControlCallback)(const char* command);

// Forward declarations - full definitions in main.cpp / learn_engine.h /
// custom_vehicle_store.h
class LearnEngine;
class CustomVehicleStore;
class ActiveProfileManager;
class VehicleControl;

class TFT_UI {
public:
    TFT_UI();

    /** Initializes the display and LVGL. */
    void begin();

    /** Call every loop() iteration. */
    void update();

    void setControlCallback(UIControlCallback cb);

    /**
     * Attaches the Learn Mode modules. Must be called before begin()
     * (same pattern as WebServerManager::attachLearnModules).
     */
    void attachLearnModules(LearnEngine* learnEngine,
                             CustomVehicleStore* customStore,
                             ActiveProfileManager* profileManager,
                             VehicleControl* vehicleControl);

    void updateVehicleData(const VehicleData& data);
    void setCANStatus(bool connected);
    void setWiFiStatus(bool connected);
    void setTheme(ThemeMode mode);
    void showNotification(const char* message);
    void setDeviceMode(DeviceMode mode);

    /**
     * Runs the interactive touch calibration flow (5 points, via
     * TFT_eSPI's calibrateTouch()). The result is stored in AppConfig
     * (touchCalData/touchCalibrated) so it isn't needed again on
     * subsequent boots. This call blocks the caller - though not the
     * whole board, see tft_ui.cpp - for up to ~20s while it waits for
     * the user to touch 5 points, and must only be invoked from setup()
     * or from a settings menu the user explicitly triggered, never
     * automatically mid-operation from the main loop.
     *
     * Returns true if calibration completed and was saved, false if the
     * touch panel never responded (e.g. not wired up yet) and the
     * attempt timed out - in which case touchCalibrated is left false
     * and the caller should treat touch as unavailable for now.
     */
    bool runTouchCalibration();

private:
    bool               _initialized;
    UIControlCallback  _controlCallback;
    VehicleData        _vehicleData;
    DeviceMode         _currentMode;

    // -- Learn Mode module pointers ----------------------------------------
    LearnEngine*           _learnEngine;
    CustomVehicleStore*    _customStore;
    ActiveProfileManager*  _profileManager;
    VehicleControl*        _vehicleControl;

    // -- LVGL objects: tabs ------------------------------------------------------
    lv_obj_t* _tabView;
    lv_obj_t* _tabControl;
    lv_obj_t* _tabDashboard;
    lv_obj_t* _tabSettings;
    lv_obj_t* _tabLearn;

    // -- Dashboard labels -------------------------------------------------------
    lv_obj_t* _labelSpeed;
    lv_obj_t* _labelRPM;
    lv_obj_t* _labelTemp;
    lv_obj_t* _labelVolt;
    lv_obj_t* _labelFuel;

    // -- Status indicators --------------------------------------------------------
    lv_obj_t* _statusCAN;
    lv_obj_t* _statusWiFi;
    lv_obj_t* _notification;

    // -- Password change screen --------------------------------------------------
    lv_obj_t* _passwordScreen;
    lv_obj_t* _passwordWarningLabel;
    lv_obj_t* _taNewPass;
    lv_obj_t* _taConfirmPass;
    lv_obj_t* _passwordErrorLabel;
    lv_obj_t* _keyboard;   // Shared virtual keyboard (password screen + Learn Mode)

    // -- Learn tab: main view ------------------------------------------------------
    lv_obj_t* _learnMainContainer;
    lv_obj_t* _learnVehicleList;    // Custom profile list, with status
    lv_obj_t* _learnActiveVehicleLabel;

    // -- Learn tab: wizard modal ----------------------------------------------------
    lv_obj_t* _learnWizardScreen;
    lv_obj_t* _learnWizardTitle;
    lv_obj_t* _learnWizardStatusLabel;
    lv_obj_t* _learnWizardProgressBar;
    lv_obj_t* _learnWizardCandidateList;  // Candidates found after capture
    lv_obj_t* _learnWizardActionBtn;      // Label changes: Start/Continue/Confirm
    lv_obj_t* _learnWizardCancelBtn;
    lv_obj_t* _learnLabelDropdown;        // Command label picker
    int       _selectedCandidateIndex;    // -1 = none selected

    // -- Learn tab: manual entry modal -----------------------------------------------
    lv_obj_t* _manualEntryScreen;
    lv_obj_t* _taManualCanId;
    lv_obj_t* _taManualDataHex;
    lv_obj_t* _manualLabelDropdown;
    lv_obj_t* _manualExtendedCheckbox;
    lv_obj_t* _manualErrorLabel;

    // -- Learn tab: verification modal -------------------------------------------------
    // Shows the exact CAN ID/bytes before a one-shot test send.
    lv_obj_t* _verifyScreen;
    lv_obj_t* _verifyInfoLabel;
    lv_obj_t* _verifyResultLabel;
    char      _verifyProfileId;
    char      _verifyLabel[32];

    uint8_t _selectedProfileForLearning;  // Profile currently being learned/entered

    // -- Screen builders --------------------------------------------------------------
    void _buildTabControl();
    void _buildTabDashboard();
    void _buildTabSettings();
    void _buildPasswordScreen();
    void _openPasswordScreen();
    void _closePasswordScreen();
    void _submitPasswordChange();
    void _refreshPasswordWarning();

    void _buildTabLearn();
    void _refreshLearnVehicleList();
    void _buildLearnWizardScreen();
    void _openLearnWizard();
    void _closeLearnWizard();
    void _refreshLearnWizardUI();   // Redraws based on learnEngine->getState()
    void _onLearnWizardActionPressed();
    void _onLearnCandidateSelected(int index);
    void _saveLearnedCommand();

    void _buildManualEntryScreen();
    void _openManualEntryScreen();
    void _closeManualEntryScreen();
    void _submitManualEntry();

    void _buildVerifyScreen();
    void _openVerifyScreen(uint8_t profileId, const char* label, uint32_t canId,
                            const uint8_t* data, uint8_t length);
    void _closeVerifyScreen();
    void _onVerifySendPressed();
    void _onVerifyResultPressed(bool success);

    // -- Event handlers: Control/Settings tabs ---------------------------------------
    static void _btnLockEventHandler(lv_event_t* e);
    static void _btnUnlockEventHandler(lv_event_t* e);
    static void _btnWindowUpEventHandler(lv_event_t* e);
    static void _btnWindowDownEventHandler(lv_event_t* e);
    static void _btnSunroofEventHandler(lv_event_t* e);
    static void _btnTrunkEventHandler(lv_event_t* e);
    static void _btnMirrorEventHandler(lv_event_t* e);
    static void _btnAlarmEventHandler(lv_event_t* e);
    static void _btnThemeEventHandler(lv_event_t* e);
    static void _btnListenOnlyEventHandler(lv_event_t* e);
    static void _btnVehicleSelectEventHandler(lv_event_t* e);
    static void _btnChangePasswordEventHandler(lv_event_t* e);
    static void _btnPasswordSaveEventHandler(lv_event_t* e);
    static void _btnPasswordCancelEventHandler(lv_event_t* e);
    static void _taFocusEventHandler(lv_event_t* e);
    static void _btnRecalibrateTouchEventHandler(lv_event_t* e);

    // -- Event handlers: Learn tab ---------------------------------------------------
    static void _btnStartLearnEventHandler(lv_event_t* e);
    static void _btnManualEntryEventHandler(lv_event_t* e);
    static void _btnLearnWizardActionEventHandler(lv_event_t* e);
    static void _btnLearnWizardCancelEventHandler(lv_event_t* e);
    static void _learnCandidateSelectEventHandler(lv_event_t* e);
    static void _btnManualSubmitEventHandler(lv_event_t* e);
    static void _btnManualCancelEventHandler(lv_event_t* e);
    static void _btnVerifySendEventHandler(lv_event_t* e);
    static void _btnVerifySuccessEventHandler(lv_event_t* e);
    static void _btnVerifyFailEventHandler(lv_event_t* e);
    static void _vehicleListItemEventHandler(lv_event_t* e);

    // -- LVGL display driver -------------------------------------------------------
    static lv_disp_draw_buf_t _dispBuf;
    static lv_color_t          _buf1[LVGL_BUF_SIZE];
    static lv_color_t          _buf2[LVGL_BUF_SIZE];

    static void _lvglDisplayFlush(lv_disp_drv_t* drv, const lv_area_t* area, lv_color_t* colorMap);
    static void _lvglTouchRead(lv_indev_drv_t* drv, lv_indev_data_t* data);
};

#endif // TFT_UI_H
