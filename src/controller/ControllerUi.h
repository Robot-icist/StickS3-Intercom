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
    bool brightnessMode = false;
    bool brightnessSaved = true;
    uint8_t brightness = 180;
    uint8_t brightnessItem = 0;
    uint8_t ledBrightness = 64;
    uint8_t ledIdleColorIndex = 0;
    bool ledColorCycle = true;
    bool sleepMode = false;
    uint32_t sleepTimeoutMs = 60000;
    bool flipX = false;
    bool flipY = false;
    bool motionFlipX = false;
    bool motionFlipY = false;
    uint8_t dpiLevel = 3;
    uint8_t motionLevel = 3;
    uint8_t colorPalette = 0;
    uint8_t selectedPaletteIndex = 0;
    int8_t batteryLevel = -1;
    bool batteryCharging = false;
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

  void drawBatteryStatus(LGFX_Sprite &screen, int8_t level, bool charging,
                         int32_t y, const Palette &p);
  void drawClippedMarquee(LGFX_Sprite &screen, const char *text, int32_t x, int32_t y,
                          int32_t width, int32_t height, uint16_t foreground,
                          uint16_t background, uint32_t now);
  void drawNetworkIdentity(LGFX_Sprite &screen, const ControllerViewState &state,
                           uint32_t now, const Palette &p);

  class ControllerUi
  {
  public:
    bool begin();
    void showError(const char *message);
    void render(const ControllerViewState &state, uint32_t now);

  private:
    void renderController(const ControllerViewState &state, uint32_t now, const Palette &p);
    void renderMicMode(const ControllerViewState &state, uint32_t now, const Palette &p);
    void renderMagnifierMode(const ControllerViewState &state, uint32_t now, const Palette &p);
    void renderAirMouseMode(const ControllerViewState &state, uint32_t now, const Palette &p);
    void renderDpiMode(const ControllerViewState &state, uint32_t now, const Palette &p);
    void renderStyleMode(const ControllerViewState &state, uint32_t now, const Palette &p);
    void renderBrightnessMode(const ControllerViewState &state, uint32_t now, const Palette &p);
    void renderSleepMode(const ControllerViewState &state, uint32_t now, const Palette &p);
    void renderMouseMode(const ControllerViewState &state, uint32_t now, const Palette &p);
    void renderModePicker(const ControllerViewState &state, uint32_t now, const Palette &p);
    void renderImuCube(const ControllerViewState &state, const Palette &p);

    LGFX_Sprite _screen{&M5.Display};
  };

} // namespace controller
