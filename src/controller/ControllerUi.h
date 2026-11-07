#pragma once

#include <Arduino.h>
#include <M5Unified.h>

#include "BleMicAudio.h"

namespace controller
{

  struct ControllerViewState
  {
    bool navigationMode = false;
    bool dpiMode = false;
    bool micMode = false;
    bool airMouseMode = false;
    bool magnifierMode = false;
    bool magnifierOpen = false;
    bool modePickerActive = false;
    uint8_t selectedMode = 0;
    bool connected = false;
    bool hidReady = false;
    bool micSubscribed = false;
    MicAudioStatus micStatus = MicAudioStatus::Unavailable;
    bool demoMode = false;
    bool orientationSaved = true;
    bool dpiSaved = true;
    bool motionSaved = true;
    bool motionFlipSaved = true;
    bool flipX = false;
    bool flipY = false;
    bool motionFlipX = false;
    bool motionFlipY = false;
    uint8_t dpiLevel = 3;
    uint8_t motionLevel = 3;
    int8_t batteryLevel = -1;
    bool joystickError = false;
    char deviceName[32] = {};
    char ipAddress[16] = {};
    char mdnsName[40] = {};
    char otaPassword[64] = {};
    uint8_t pendingBaseRotation = 0;
    float roll = 0.0f;
    float pitch = 0.0f;
    float yaw = 0.0f;
    uint32_t lastValidInputAt = 0;
  };

  class ControllerUi
  {
  public:
    bool begin();
    void showError(const char *message);
    void render(const ControllerViewState &state, uint32_t now);

  private:
    void renderController(const ControllerViewState &state, uint32_t now);
    void renderModePicker(const ControllerViewState &state, uint32_t now);
    void renderImuCube(const ControllerViewState &state);

    LGFX_Sprite _screen{&M5.Display};
  };

} // namespace controller
