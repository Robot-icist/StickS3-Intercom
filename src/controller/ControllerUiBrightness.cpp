#include "ControllerUi.h"
#include "ControllerConstants.h"

#include <cmath>
#include <cstdio>

namespace controller
{
  namespace
  {
    const char *const kIdleColorNames[] = {
        "WHITE", "RED", "GREEN", "BLUE",
        "YELLOW", "CYAN", "MAGENTA", "ORANGE"};
  }

  void ControllerUi::renderBrightnessMode(const ControllerViewState &state, uint32_t now, const Palette &p)
  {
    _screen.fillRoundRect(6, 48, _screen.width() - 12, 112, 7, p.panel);
    _screen.setTextDatum(lgfx::textdatum_t::top_center);
    _screen.setFont(&fonts::Font0);
    _screen.setTextColor(p.accent, p.panel);
    _screen.drawString("BRIGHTNESS", _screen.width() / 2, 56);

    const char *const labels[] = {"SCREEN", "LED", "IDLE COLOR", "CYCLE"};
    char values[4][16];
    snprintf(values[0], sizeof(values[0]), "%u", static_cast<unsigned>(state.brightness));
    snprintf(values[1], sizeof(values[1]), "%u", static_cast<unsigned>(state.ledBrightness));
    snprintf(values[2], sizeof(values[2]), "%s",
             kIdleColorNames[state.ledIdleColorIndex % kJoystickRgbIdleColorCount]);
    snprintf(values[3], sizeof(values[3]), "%s", state.ledColorCycle ? "ON" : "OFF");

    for (uint8_t i = 0; i < kBrightnessItemCount; ++i)
    {
      const int16_t y = 76 + i * 15;
      const bool selected = i == state.brightnessItem;
      _screen.setTextColor(selected ? p.accent : p.secondary, p.panel);
      char line[40];
      snprintf(line, sizeof(line), "%s %-10s %s", selected ? ">" : " ",
               labels[i], values[i]);
      _screen.drawString(line, _screen.width() / 2, y);
    }

    _screen.setTextColor(state.brightnessSaved ? p.secondary : p.warning, p.panel);
    _screen.drawString(state.brightnessSaved ? "SAVED" : "NOT SAVED",
                       _screen.width() / 2, 138);
    drawClippedMarquee(_screen, "Stick U/D: select  L/R: change", 8, 165,
                       _screen.width() - 16, 12, p.primary, p.background, now);
    drawClippedMarquee(_screen, "BtnA: screen max  BtnB: eco", 8, 181,
                       _screen.width() - 16, 12, p.primary, p.background, now);
    drawClippedMarquee(_screen, "Click: save | hold stick: select mode", 8, 199,
                       _screen.width() - 16, 12, p.secondary, p.background, now);
  }
} // namespace controller
