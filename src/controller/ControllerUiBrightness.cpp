#include "ControllerUi.h"

#include <cmath>
#include <cstdio>

namespace controller
{
  void ControllerUi::renderBrightnessMode(const ControllerViewState &state, uint32_t now, const Palette &p)
  {
    _screen.fillRoundRect(6, 48, _screen.width() - 12, 112, 7, p.panel);
    _screen.setTextDatum(lgfx::textdatum_t::top_center);
    _screen.setFont(&fonts::Font0);
    _screen.setTextColor(p.accent, p.panel);
    _screen.drawString("BRIGHTNESS", _screen.width() / 2, 58);
    char line[32];
    snprintf(line, sizeof(line), "LEVEL %u", static_cast<unsigned>(state.brightness));
    _screen.setFont(&fonts::Font2);
    _screen.setTextColor(p.primary, p.panel);
    _screen.drawString(line, _screen.width() / 2, 82);
    _screen.setFont(&fonts::Font0);
    _screen.setTextColor(state.brightnessSaved ? p.secondary : p.warning, p.panel);
    _screen.drawString(state.brightnessSaved ? "SAVED" : "NOT SAVED", _screen.width() / 2, 106);
    drawClippedMarquee(_screen, "Stick L/R: adjust", 8, 165, _screen.width() - 16, 12, p.primary, p.background, now);
    drawClippedMarquee(_screen, "BtnA: high  BtnB: eco", 8, 181, _screen.width() - 16, 12, p.primary, p.background, now);
    drawClippedMarquee(_screen, "Click: save | hold stick: select mode", 8, 199, _screen.width() - 16, 12, p.secondary, p.background, now);
    _screen.pushSprite(0, 0);
    return;
  
  }
}
