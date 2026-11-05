#include "ControllerUi.h"

#include <cmath>
#include <cstdio>

namespace controller {
namespace {

constexpr uint16_t kColorBackground = 0x10A2;
constexpr uint16_t kColorPanel = 0x2126;
constexpr uint16_t kColorBorder = 0x39C9;
constexpr uint16_t kColorPrimary = 0xD6DA;
constexpr uint16_t kColorSecondary = 0x8C71;
constexpr uint16_t kColorAccent = 0x5D99;
constexpr uint16_t kColorConnected = 0x6E89;
constexpr uint16_t kColorWarning = 0xCDB2;
constexpr uint16_t kColorError = 0xC986;
constexpr float kRadToDeg = 57.2957795f;

void drawBatteryStatus(LGFX_Sprite& screen, int8_t level, int32_t y) {
  const int32_t right = screen.width() - 8;
  char text[8];
  uint16_t color = kColorPrimary;
  if (level < 0) {
    snprintf(text, sizeof(text), "USB");
    color = kColorSecondary;
  } else {
    snprintf(text, sizeof(text), "%d%%", static_cast<int>(level));
    color = level <= 20 ? kColorError : kColorPrimary;
  }

  screen.setFont(&fonts::Font0);
  screen.setTextDatum(lgfx::textdatum_t::top_right);
  screen.setTextColor(color, kColorBackground);
  if (level < 0) {
    screen.drawString(text, right, y);
    return;
  }

  // Small battery outline with a proportional fill, followed by the level.
  constexpr int32_t kIconWidth = 17;
  constexpr int32_t kIconHeight = 9;
  const int32_t iconX = right - kIconWidth - 2;
  screen.drawString(text, iconX - 3, y);
  screen.drawRect(iconX, y, kIconWidth, kIconHeight, color);
  screen.fillRect(iconX + kIconWidth, y + 2, 2, kIconHeight - 4, color);
  const int32_t fill = (kIconWidth - 4) * level / 100;
  if (fill > 0) {
    screen.fillRect(iconX + 2, y + 2, fill, kIconHeight - 4, color);
  }
}

void drawClippedMarquee(LGFX_Sprite& screen, const char* text, int32_t x,
                        int32_t y, int32_t width, int32_t height,
                        uint16_t foreground, uint16_t background,
                        uint32_t now) {
  const int32_t textWidth = screen.textWidth(text);
  screen.setClipRect(x, y, width, height);
  screen.setTextColor(foreground, background);
  if (textWidth <= width) {
    screen.drawString(text, x, y);
  } else {
    constexpr uint32_t kScrollStepMs = 45;
    constexpr int32_t kScrollGap = 20;
    const int32_t cycleWidth = textWidth + kScrollGap;
    const int32_t offset = now < 1200 ? 0 : (now / kScrollStepMs) % cycleWidth;
    screen.drawString(text, x - offset, y);
    screen.drawString(text, x - offset + cycleWidth, y);
  }
  screen.clearClipRect();
}

}  // namespace

bool ControllerUi::begin() {
  if (M5.Display.width() <= 0) {
    Serial.println("[controller] display initialization failed");
    return false;
  }

  M5.Display.setRotation(0);
  M5.Display.setBrightness(180);
  _screen.setColorDepth(16);
  if (!_screen.createSprite(M5.Display.width(), M5.Display.height())) {
    Serial.println("[controller] display buffer allocation failed");
    M5.Display.fillScreen(kColorBackground);
    M5.Display.setTextColor(kColorError, kColorBackground);
    M5.Display.setTextDatum(lgfx::textdatum_t::middle_center);
    M5.Display.setFont(&fonts::Font0);
    M5.Display.drawString("DISPLAY MEMORY ERROR", M5.Display.width() / 2,
                          M5.Display.height() / 2);
    return false;
  }
  return true;
}

void ControllerUi::showError(const char* message) {
  M5.Display.fillScreen(kColorBackground);
  M5.Display.setTextColor(TFT_RED, kColorBackground);
  M5.Display.setTextDatum(lgfx::textdatum_t::middle_center);
  M5.Display.setFont(&fonts::Font0);
  M5.Display.drawString(message, M5.Display.width() / 2,
                        M5.Display.height() / 2);
}

void ControllerUi::render(const ControllerViewState& state, uint32_t now) {
  if (state.demoMode) {
    renderImuCube(state);
  } else {
    renderController(state, now);
  }
}

void ControllerUi::renderController(const ControllerViewState& state,
                                    uint32_t now) {
  _screen.fillScreen(kColorBackground);
  _screen.setTextDatum(lgfx::textdatum_t::top_left);
  _screen.setFont(&fonts::Font2);
  _screen.setTextColor(kColorPrimary, kColorBackground);
  const char* modeName =
      state.micMode ? "MIC"
                    : state.dpiMode ? "SPEED"
                                    : state.navigationMode ? "NAV" : "MOUSE";
  _screen.drawString(modeName, 8, 8);

  _screen.setFont(&fonts::Font0);
  _screen.setTextDatum(lgfx::textdatum_t::top_right);
  _screen.setTextColor(state.hidReady ? kColorConnected : kColorWarning,
                       kColorBackground);
  const char* connectionStatus =
      !state.connected ? "PAIR" : state.hidReady ? "HID READY" : "HID WAIT";
  _screen.drawString(connectionStatus,
                     _screen.width() - 8, 12);
  drawBatteryStatus(_screen, state.batteryLevel, 2);
  _screen.drawFastHLine(8, 34, _screen.width() - 16, kColorBorder);

  if (state.micMode) {
    const char* status = "MIC UNAVAILABLE";
    uint16_t statusColor = kColorWarning;
    switch (state.micStatus) {
      case MicAudioStatus::Idle:
        status = state.micSubscribed ? "AUDIO READY" : "CONNECT COMPANION";
        statusColor = state.micSubscribed ? kColorConnected : kColorWarning;
        break;
      case MicAudioStatus::Live:
        status = "LIVE STREAM";
        statusColor = kColorConnected;
        break;
      case MicAudioStatus::Recording:
        status = "RECORDING";
        statusColor = kColorWarning;
        break;
      case MicAudioStatus::Sending:
        status = "SENDING CLIP";
        statusColor = kColorConnected;
        break;
      case MicAudioStatus::Error:
        status = "MIC ERROR";
        statusColor = kColorError;
        break;
      case MicAudioStatus::Unavailable:
        break;
    }

    _screen.fillRoundRect(6, 48, _screen.width() - 12, 72, 7, kColorPanel);
    _screen.setTextDatum(lgfx::textdatum_t::top_center);
    _screen.setFont(&fonts::Font0);
    _screen.setTextColor(kColorAccent, kColorPanel);
    _screen.drawString("BLE MICROPHONE", _screen.width() / 2, 60);
    _screen.setFont(&fonts::Font2);
    _screen.setTextColor(statusColor, kColorPanel);
    _screen.drawString(status, _screen.width() / 2, 82);
    _screen.setFont(&fonts::Font0);
    drawClippedMarquee(_screen, "Stick click: live on / off", 8, 145,
                       _screen.width() - 16, 12, kColorPrimary,
                       kColorBackground, now);
    drawClippedMarquee(_screen, "A: record / send clip", 8, 163,
                       _screen.width() - 16, 12, kColorSecondary,
                       kColorBackground, now);
    drawClippedMarquee(_screen, "B: cancel | hold A: back", 8, 181,
                       _screen.width() - 16, 12, kColorSecondary,
                       kColorBackground, now);
    drawClippedMarquee(_screen, "Stick hold: next mode", 8, 199,
                       _screen.width() - 16, 12, kColorSecondary,
                       kColorBackground, now);
    drawClippedMarquee(_screen, "Requires BLE audio companion", 8, 217,
                       _screen.width() - 16, 12, kColorSecondary,
                       kColorBackground, now);
    _screen.pushSprite(0, 0);
    return;
  }

  if (state.dpiMode) {
    _screen.fillRoundRect(6, 48, _screen.width() - 12, 112, 7, kColorPanel);
    _screen.setTextDatum(lgfx::textdatum_t::top_center);
    _screen.setFont(&fonts::Font0);
    _screen.setTextColor(kColorAccent, kColorPanel);
    _screen.drawString("POINTER SPEED", _screen.width() / 2, 60);
    char level[20];
    snprintf(level, sizeof(level), "LEVEL %u / 7",
             static_cast<unsigned>(state.dpiLevel + 1));
    _screen.setFont(&fonts::Font2);
    _screen.setTextColor(kColorPrimary, kColorPanel);
    _screen.drawString(level, _screen.width() / 2, 82);
    _screen.setFont(&fonts::Font0);
    _screen.setTextColor(state.dpiSaved ? kColorSecondary : kColorWarning,
                         kColorPanel);
    _screen.drawString(state.dpiSaved ? "SAVED" : "NOT SAVED",
                       _screen.width() / 2, 120);
    drawClippedMarquee(_screen, "Stick L/R: slower / faster", 8, 173,
                       _screen.width() - 16, 12, kColorPrimary,
                       kColorBackground, now);
    drawClippedMarquee(_screen, "Click: save | stick hold: next", 8, 191,
                       _screen.width() - 16, 12, kColorSecondary,
                       kColorBackground, now);
    drawClippedMarquee(_screen, "A: copy  B: paste | hold A: mic", 8, 209,
                       _screen.width() - 16, 12, kColorSecondary,
                       kColorBackground, now);
    if (state.joystickError) {
      drawClippedMarquee(_screen, "Joystick I2C error", 8, 227,
                         _screen.width() - 16, 12, kColorError,
                         kColorBackground, now);
    }
    _screen.pushSprite(0, 0);
    return;
  }

  _screen.fillRoundRect(6, 42, _screen.width() - 12, 58, 7, kColorPanel);
  _screen.setTextDatum(lgfx::textdatum_t::top_left);
  _screen.setTextColor(kColorAccent, kColorPanel);
  _screen.drawString("JOYSTICK", 14, 47);
  if (state.navigationMode) {
    drawClippedMarquee(_screen, "Move: scroll", 14, 62,
                       _screen.width() - 28, 12, kColorPrimary, kColorPanel,
                       now);
    drawClippedMarquee(_screen, "Click: Enter / dbl: Esc", 14, 78,
                       _screen.width() - 28, 12, kColorPrimary, kColorPanel,
                       now);
  } else {
    drawClippedMarquee(_screen, "Move: pointer", 14, 62,
                       _screen.width() - 28, 12, kColorPrimary, kColorPanel,
                       now);
    drawClippedMarquee(_screen, "Click: left / dbl: right", 14, 78,
                       _screen.width() - 28, 12, kColorPrimary, kColorPanel,
                       now);
  }

  _screen.fillRoundRect(6, 108, _screen.width() - 12, 43, 7, kColorPanel);
  _screen.setTextColor(kColorAccent, kColorPanel);
  _screen.drawString("BUTTONS", 14, 114);
  drawClippedMarquee(
      _screen,
      state.navigationMode ? "A/B: page up / down" : "A: copy  B: paste", 14,
      130, _screen.width() - 28, 12, kColorPrimary, kColorPanel, now);

  drawClippedMarquee(_screen, "Gesture L/R: NO | F/B: YES", 8, 162,
                     _screen.width() - 16, 12, kColorSecondary,
                     kColorBackground, now);
  drawClippedMarquee(_screen, "Stick hold: mouse > nav > IMU > speed > mic", 8,
                     181, _screen.width() - 16, 12, kColorSecondary,
                     kColorBackground, now);
  drawClippedMarquee(_screen, "Hold A: mic <-> last mode", 8, 198,
                     _screen.width() - 16, 12, kColorSecondary,
                     kColorBackground, now);

  if (state.joystickError) {
    drawClippedMarquee(_screen, "Joystick I2C error", 8, 220,
                       _screen.width() - 16, 12, kColorError, kColorBackground,
                       now);
  } else if (now - state.lastValidInputAt > 1000) {
    drawClippedMarquee(_screen, "Waiting for joystick", 8, 220,
                       _screen.width() - 16, 12, kColorSecondary,
                       kColorBackground, now);
  }
  _screen.pushSprite(0, 0);
}

void ControllerUi::renderImuCube(const ControllerViewState& state) {
  constexpr float halfSize = 1.0f;
  constexpr float scale = 37.0f;
  constexpr float centerX = 67.5f;
  constexpr float centerY = 126.0f;
  const float sinRoll = sinf(state.roll);
  const float cosRoll = cosf(state.roll);
  const float sinPitch = sinf(state.pitch);
  const float cosPitch = cosf(state.pitch);
  const float sinYaw = sinf(state.yaw);
  const float cosYaw = cosf(state.yaw);

  _screen.fillScreen(kColorBackground);
  _screen.setTextColor(kColorPrimary, kColorBackground);
  _screen.setTextDatum(lgfx::textdatum_t::top_left);
  _screen.setFont(&fonts::Font0);
  _screen.drawString("ORIENTATION", 8, 8);
  drawBatteryStatus(_screen, state.batteryLevel, 8);
  char baseOrientation[32];
  snprintf(baseOrientation, sizeof(baseOrientation), "BASE %u DEG %s",
           static_cast<unsigned>(state.pendingBaseRotation * 90),
           state.orientationSaved ? "SAVED" : "UNSAVED");
  _screen.setTextDatum(lgfx::textdatum_t::top_center);
  _screen.setTextColor(state.orientationSaved ? kColorSecondary : kColorWarning,
                       kColorBackground);
  _screen.drawString(baseOrientation, _screen.width() / 2, 20);

  char orientation[40];
  snprintf(orientation, sizeof(orientation), "R %.0f   P %.0f",
           state.roll * kRadToDeg, state.pitch * kRadToDeg);
  _screen.setTextColor(kColorPrimary, kColorBackground);
  _screen.drawString(orientation, _screen.width() / 2, 31);
  _screen.drawRoundRect(8, 42, _screen.width() - 16, 158, 10, kColorBorder);

  int16_t points[8][2] = {};
  for (uint8_t i = 0; i < 8; ++i) {
    const float x = (i & 0x01) ? halfSize : -halfSize;
    const float y = (i & 0x02) ? halfSize : -halfSize;
    const float z = (i & 0x04) ? halfSize : -halfSize;
    const float rollX = x;
    const float rollY = y * cosRoll - z * sinRoll;
    const float rollZ = y * sinRoll + z * cosRoll;
    const float pitchX = rollX * cosPitch + rollZ * sinPitch;
    const float pitchY = rollY;
    const float yawX = pitchX * cosYaw - pitchY * sinYaw;
    const float yawY = pitchX * sinYaw + pitchY * cosYaw;
    points[i][0] = static_cast<int16_t>(centerX + scale * yawX);
    points[i][1] = static_cast<int16_t>(centerY - scale * yawY);
  }

  for (uint8_t i = 0; i < 8; ++i) {
    for (uint8_t bit = 0; bit < 3; ++bit) {
      const uint8_t j = i ^ (1U << bit);
      if (i < j) {
        _screen.drawLine(points[i][0], points[i][1], points[j][0],
                         points[j][1], bit == 2 ? kColorAccent : kColorPrimary);
      }
    }
  }

  _screen.setTextDatum(lgfx::textdatum_t::bottom_center);
  _screen.setTextColor(kColorSecondary, kColorBackground);
  _screen.setFont(&fonts::Font0);
  _screen.drawString("L/R: base rotation", _screen.width() / 2, 211);
  char flips[32];
  snprintf(flips, sizeof(flips), "A:X %s  B:Y %s",
           state.flipX ? "FLIP" : "NORM",
           state.flipY ? "FLIP" : "NORM");
  _screen.drawString(flips, _screen.width() / 2, 225);
  _screen.drawString("Click save | hold A: mic", _screen.width() / 2, 238);
  _screen.pushSprite(0, 0);
}

}  // namespace controller
