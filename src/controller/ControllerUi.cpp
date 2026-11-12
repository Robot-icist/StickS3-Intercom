#include "ControllerUi.h"

#include <cmath>
#include <cstdio>

namespace controller
{

constexpr float kRadToDeg = 57.2957795f;

constexpr Palette kPalettes[] = {
    // Default (original)
    {0x10A2, 0x2126, 0x39C9, 0xD6DA, 0x8C71, 0x5D99, 0x6E89, 0xCDB2, 0xC986},
    // Dark
    {0x0000, 0x1082, 0x2104, 0xE71C, 0x7BEF, 0xF800, 0x07E0, 0xFFE0, 0xF800},
    // Light
    {0xFFFF, 0xE73C, 0xBDF7, 0x0000, 0x4208, 0x18E3, 0x0400, 0x8410, 0xC000},
    // High Contrast
    {0x0000, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFE0, 0x07FF, 0xFFE0, 0xF800},
    // Cyberpunk
    {0x0000, 0x0841, 0xF81F, 0xF81F, 0x07FF, 0xFFE0, 0x07FF, 0xF81F, 0xF800},
    // Monochrome
    {0x0000, 0x3186, 0x630C, 0xFFFF, 0x8C71, 0xAD55, 0x630C, 0xCE79, 0xFFFF},
    // Sepia
    {0x8C63, 0x6B4A, 0x4208, 0x1082, 0x8C63, 0xAD55, 0x4208, 0xB5B6, 0x2104},
    // Forest
    {0x0000, 0x0320, 0x07E0, 0x07E0, 0x03E0, 0x87E0, 0x07E0, 0x87E0, 0x07E0},
};

const Palette &getPalette(uint8_t paletteIndex)
{
  if (paletteIndex >= sizeof(kPalettes) / sizeof(kPalettes[0]))
  {
    paletteIndex = 0;
  }
  return kPalettes[paletteIndex];
}

namespace
{
void drawChargingBolt(LGFX_Sprite &screen, int32_t x, int32_t y, uint16_t color)
{
  screen.fillTriangle(x + 4, y, x, y + 5, x + 3, y + 5, color);
  screen.fillTriangle(x + 1, y + 9, x + 6, y + 4, x + 3, y + 4, color);
}

void drawUsbPlug(LGFX_Sprite &screen, int32_t x, int32_t y, uint16_t color)
{
  // Compact plug: two prongs, a rounded body, and a short lead.
  screen.drawRoundRect(x + 2, y + 3, 8, 6, 2, color);
  screen.drawFastVLine(x + 4, y, 3, color);
  screen.drawFastVLine(x + 8, y, 3, color);
  screen.drawFastHLine(x + 10, y + 5, 2, color);
}

void drawBatteryGauge(LGFX_Sprite &screen, int32_t x, int32_t y, int8_t level,
                      uint16_t outline, uint16_t fillColor)
{
  constexpr int32_t kWidth = 20;
  constexpr int32_t kHeight = 10;
  screen.drawRect(x, y, kWidth, kHeight, outline);
  screen.fillRect(x + kWidth, y + 3, 2, kHeight - 6, outline);

  constexpr int32_t kInnerWidth = kWidth - 4;
  int32_t fill = kInnerWidth * static_cast<int32_t>(level) / 100;
  if (fill < 0)
  {
    fill = 0;
  }
  if (fill > kInnerWidth)
  {
    fill = kInnerWidth;
  }
  if (fill > 0)
  {
    screen.fillRect(x + 2, y + 2, fill, kHeight - 4, fillColor);
  }
}
} // namespace

void drawBatteryStatus(LGFX_Sprite &screen, int8_t level, bool charging,
                       int32_t y, const Palette &p)
{
  const int32_t right = screen.width() - 8;
  const bool hasLevel = level >= 0;
  const uint16_t color = !hasLevel
                             ? p.secondary
                             : (level <= 20 && !charging) ? p.error : p.primary;

  char text[8];
  if (hasLevel)
  {
    snprintf(text, sizeof(text), "%d%%", static_cast<int>(level));
  }
  else
  {
    snprintf(text, sizeof(text), "USB");
  }

  screen.setFont(&fonts::Font0);
  screen.setTextDatum(lgfx::textdatum_t::top_right);
  screen.setTextColor(color, p.background);

  if (hasLevel)
  {
    constexpr int32_t kIconWidth = 20;
    const int32_t iconX = right - kIconWidth - 2;
    const int32_t textRight = iconX - (charging ? 12 : 3);
    screen.drawString(text, textRight, y);
    if (charging)
    {
      drawChargingBolt(screen, iconX - 9, y + 1, p.accent);
    }
    drawBatteryGauge(screen, iconX, y, level, color,
                     charging ? p.connected : color);
  }
  else
  {
    constexpr int32_t kIconWidth = 12;
    const int32_t iconX = right - kIconWidth - 2;
    const int32_t textRight = iconX - (charging ? 12 : 3);
    screen.drawString(text, textRight, y);
    if (charging)
    {
      drawChargingBolt(screen, iconX - 9, y + 1, p.accent);
    }
    drawUsbPlug(screen, iconX, y, charging ? p.connected : p.secondary);
  }
}

void drawClippedMarquee(LGFX_Sprite &screen, const char *text, int32_t x,
                        int32_t y, int32_t width, int32_t height,
                        uint16_t foreground, uint16_t background,
                        uint32_t now)
{
  const int32_t textWidth = screen.textWidth(text);
  screen.setClipRect(x, y, width, height);
  screen.setTextColor(foreground, background);
  if (textWidth <= width)
  {
    screen.drawString(text, x, y);
  }
  else
  {
    constexpr uint32_t kScrollStepMs = 45;
    constexpr int32_t kScrollGap = 20;
    const int32_t cycleWidth = textWidth + kScrollGap;
    const int32_t offset = now < 1200 ? 0 : (now / kScrollStepMs) % cycleWidth;
    screen.drawString(text, x - offset, y);
    screen.drawString(text, x - offset + cycleWidth, y);
  }
  screen.clearClipRect();
}

void drawNetworkIdentity(LGFX_Sprite &screen,
                         const ControllerViewState &state, uint32_t now, const Palette &p)
{
  const char *prefix = "NAME ";
  const char *value = state.deviceName;
  const uint32_t phase = now % 19000;
  if (phase >= 3000 && phase < 6000)
  {
    prefix = "IP ";
    value = state.ipAddress[0] ? state.ipAddress : "WIFI WAIT";
  }
  else if (phase >= 6000 && phase < 9000)
  {
    prefix = "MDNS ";
    value = state.mdnsName;
  }
  else if (phase >= 9000)
  {
    prefix = "OTA ";
    value = state.otaPassword[0] ? state.otaPassword : "OTA OFF";
  }

  char identity[80];
  snprintf(identity, sizeof(identity), "%s%s", prefix,
           value[0] ? value : "UNKNOWN");
  screen.setFont(&fonts::Font0);
  screen.setTextDatum(lgfx::textdatum_t::top_left);
  drawClippedMarquee(screen, identity, 8, 22, screen.width() - 16, 10,
                     p.secondary, p.background, now);
}

bool ControllerUi::begin()
{
  if (M5.Display.width() <= 0)
  {
    Serial.println("[controller] display initialization failed");
    return false;
  }

  M5.Display.setRotation(0);
  M5.Display.setBrightness(180);
  _screen.setColorDepth(16);
  if (!_screen.createSprite(M5.Display.width(), M5.Display.height()))
  {
    Serial.println("[controller] display buffer allocation failed");
    const Palette &p = getPalette(0);
    M5.Display.fillScreen(p.background);
    M5.Display.setTextColor(p.error, p.background);
    M5.Display.setTextDatum(lgfx::textdatum_t::middle_center);
    M5.Display.setFont(&fonts::Font0);
    M5.Display.drawString("DISPLAY MEMORY ERROR", M5.Display.width() / 2,
                          M5.Display.height() / 2);
    return false;
  }
  return true;
}

void ControllerUi::showError(const char *message)
{
  const Palette &p = getPalette(0);
  M5.Display.fillScreen(p.background);
  M5.Display.setTextColor(TFT_RED, p.background);
  M5.Display.setTextDatum(lgfx::textdatum_t::middle_center);
  M5.Display.setFont(&fonts::Font0);
  M5.Display.drawString(message, M5.Display.width() / 2,
                        M5.Display.height() / 2);
}

void ControllerUi::render(const ControllerViewState &state, uint32_t now)
{
  const Palette &p = getPalette(state.colorPalette);
  if (state.modePickerActive)
  {
    renderModePicker(state, now, p);
  }
  else if (state.demoMode)
  {
    renderImuCube(state, p);
  }
  else
  {
    renderController(state, now, p);
  }

  // Shared chrome for every mode: draw the battery once and present the frame.
  drawBatteryStatus(_screen, state.batteryLevel, state.batteryCharging, 2, p);
  _screen.pushSprite(0, 0);

  if (state.otaInProgress)
  {
    const int w = M5.Display.width();
    const int h = M5.Display.height();
    auto &d = M5.Display;
    d.fillRoundRect(6, h / 2 - 62, w - 12, 124, 8, p.panel);
    d.drawRoundRect(6, h / 2 - 62, w - 12, 124, 8, p.border);
    d.setTextDatum(lgfx::textdatum_t::top_center);
    d.setFont(&fonts::Font2);
    d.setTextColor(p.accent, p.panel);
    d.drawString("OTA UPDATE", w / 2, h / 2 - 54);
    d.setFont(&fonts::Font0);
    d.setTextColor(p.primary, p.panel);
    const char *status = state.otaStatus[0] ? state.otaStatus : "IN PROGRESS";
    d.drawString(status, w / 2, h / 2 - 30);
    const int bx = 16;
    const int by = h / 2 - 6;
    const int bw = w - 32;
    const int bh = 12;
    d.drawRect(bx, by, bw, bh, p.border);
    const int fill = bw * static_cast<int>(state.otaProgress) / 100;
    if (fill > 0)
    {
      d.fillRect(bx + 1, by + 1, fill - 2, bh - 2, p.connected);
    }
    char percent[16];
    snprintf(percent, sizeof(percent), "%u%%", static_cast<unsigned>(state.otaProgress));
    d.drawString(percent, w / 2, by + 16);
    d.setTextColor(p.warning, p.panel);
    d.drawString("DO NOT POWER OFF", w / 2, by + 34);
    const int cx = w / 2;
    const int cy = h / 2 - 38;
    const int radius = 14;
    const int activeSegment = static_cast<int>(now / 120) % 8;
    for (int i = 0; i < 8; ++i)
    {
      const float angle =
          (static_cast<float>(i) * 45.0f + now / 8.0f) * 0.0174533f;
      const int x = cx + static_cast<int>(radius * std::cos(angle));
      const int y = cy + static_cast<int>(radius * std::sin(angle));
      d.fillCircle(x, y, 2,
                   (i == activeSegment) ? p.accent : p.secondary);
    }
  }

  M5.Display.waitDisplay();
}

void ControllerUi::renderModePicker(const ControllerViewState &state,
                                     uint32_t now, const Palette &p)
{
  static const char *const modeNames[] = {
      "MOUSE", "AIR MOUSE", "NAVIGATION", "MAGNIFIER",
      "MICROPHONE", "SPEED", "ORIENTATION", "STYLE",
      "BRIGHTNESS", "SLEEP"};
  constexpr uint8_t kModeCount =
      sizeof(modeNames) / sizeof(modeNames[0]);
  constexpr int16_t kFirstRowY = 34;
  constexpr int16_t kRowHeight = 21;
  constexpr uint8_t kVisibleRows = 8;
  constexpr uint8_t kMaxStartRow = kModeCount > kVisibleRows
                                      ? kModeCount - kVisibleRows
                                      : 0;

  // Keep the selected mode inside a compact scrollable viewport so modes near
  // the end (especially Sleep) do not run into the footer/help text.
  uint8_t startRow = 0;
  if (state.selectedMode >= 3)
  {
    startRow = state.selectedMode - 3;
  }
  if (startRow > kMaxStartRow)
  {
    startRow = kMaxStartRow;
  }

  _screen.fillScreen(p.background);
  _screen.setTextDatum(lgfx::textdatum_t::top_center);
  _screen.setFont(&fonts::Font2);
  _screen.setTextColor(p.primary, p.background);
  _screen.drawString("SELECT MODE", _screen.width() / 2, 8);
  _screen.setFont(&fonts::Font0);

  if (startRow > 0)
  {
    _screen.drawString("^", _screen.width() - 16, kFirstRowY - 12);
  }
  if (startRow + kVisibleRows < kModeCount)
  {
    _screen.drawString("v", _screen.width() - 16,
                      kFirstRowY + kVisibleRows * kRowHeight - 4);
  }

  for (uint8_t slot = 0; slot < kVisibleRows; ++slot)
  {
    const uint8_t index = startRow + slot;
    if (index >= kModeCount)
    {
      break;
    }
    const int16_t y = kFirstRowY + slot * kRowHeight;
    const bool selected = index == state.selectedMode;
    if (selected)
    {
      _screen.fillRoundRect(12, y - 3, _screen.width() - 24, 18, 4,
                            p.panel);
    }
    _screen.setTextColor(selected ? p.accent : p.secondary,
                        selected ? p.panel : p.background);
    _screen.drawString(modeNames[index], _screen.width() / 2, y);
  }
  _screen.setTextColor(p.primary, p.background);
  drawClippedMarquee(_screen, "Move stick: choose", 8, 208,
                     _screen.width() - 16, 12, p.primary,
                     p.background, now);
  drawClippedMarquee(_screen, "Front/Enter: accept | Back/Esc: cancel", 8, 223,
                     _screen.width() - 16, 12, p.secondary,
                     p.background, now);
}

void ControllerUi::renderController(const ControllerViewState &state,
                                      uint32_t now, const Palette &p)
{
  _screen.fillScreen(p.background);
  _screen.setTextDatum(lgfx::textdatum_t::top_left);
  _screen.setFont(&fonts::Font2);
  _screen.setTextColor(p.primary, p.background);
  const char *modeName =
      state.micMode          ? "MIC"
      : state.airMouseMode   ? "AIR"
      : state.magnifierMode  ? "MAGN"
      : state.dpiMode        ? "SPEED"
      : state.brightnessMode ? "BRT"
      : state.sleepMode      ? "SLP"
      : state.navigationMode ? "NAV"
                             : "MOUSE";
  _screen.drawString(modeName, 8, 8);

  _screen.setFont(&fonts::Font0);
  _screen.setTextDatum(lgfx::textdatum_t::top_right);
  _screen.setTextColor(state.hidReady ? p.connected : p.warning,
                       p.background);
  const char *connectionStatus =
      !state.connected ? "PAIR" : state.hidReady ? "HID READY"
                                                 : "HID WAIT";
  _screen.drawString(connectionStatus,
                     _screen.width() - 8, 12);
  drawNetworkIdentity(_screen, state, now, p);
  _screen.drawFastHLine(8, 34, _screen.width() - 16, p.border);

  if (state.micMode)
  {
    renderMicMode(state, now, p);
    return;
  }

  if (state.magnifierMode)
  {
    renderMagnifierMode(state, now, p);
    return;
  }

  if (state.airMouseMode)
  {
    renderAirMouseMode(state, now, p);
    return;
  }

  if (state.dpiMode)
  {
    renderDpiMode(state, now, p);
    return;
  }

  if (state.styleMode)
  {
    renderStyleMode(state, now, p);
    return;
  }

  if (state.brightnessMode)
  {
    renderBrightnessMode(state, now, p);
    return;
  }

  if (state.sleepMode)
  {
    renderSleepMode(state, now, p);
    return;
  }

  renderMouseMode(state, now, p);
}



} // namespace controller