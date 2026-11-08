#pragma once

#include <Arduino.h>
#include <M5Unified.h>

#include "BleMicAudio.h"

namespace controller
{

  struct Palette
  {
    uint16_t background;
    uint16_t panel;
    uint16_t border;
    uint16_t primary;
    uint16_t secondary;
    uint16_t accent;
    uint16_t connected;
    uint16_t warning;
    uint16_t error;
  };

  const Palette &getPalette(uint8_t paletteIndex);

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
    bool styleMode = false;
    bool styleSaved = true;
    bool flipX = false;
    bool flipY = false;
    bool motionFlipX = false;
    bool motionFlipY = false;
    uint8_t dpiLevel = 3;
    uint8_t motionLevel = 3;
    uint8_t colorPalette = 0;
    uint8_t selectedPaletteIndex = 0;
    int8_t batteryLevel = -1;
    bool joystickError = false;
    bool otaInProgress = false;
    uint8_t otaProgress = 0;
    char otaStatus[32] = {};
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
    void renderController(const ControllerViewState &state, uint32_t now, const Palette &p);
    void renderModePicker(const ControllerViewState &state, uint32_t now, const Palette &p);
    void renderImuCube(const ControllerViewState &state, const Palette &p);

    LGFX_Sprite _screen{&M5.Display};
  };

} // namespace controller
