#include "ControllerUi.h"

#include <cmath>
#include <cstdio>

namespace controller
{
  void ControllerUi::renderAirMouseMode(const ControllerViewState &state, uint32_t now, const Palette &p)
  {
    _screen.fillRoundRect(6, 48, _screen.width() - 12, 100, 7, p.panel);
    _screen.setTextDatum(lgfx::textdatum_t::top_center);
    _screen.setFont(&fonts::Font0);
    _screen.setTextColor(p.accent, p.panel);
    _screen.drawString("GYRO POINTER", _screen.width() / 2, 60);
    char level[24];
    snprintf(level, sizeof(level), "LEVEL %u / 7",
             static_cast<unsigned>(state.motionLevel + 1));
    _screen.setFont(&fonts::Font2);
    _screen.setTextColor(p.primary, p.panel);
    _screen.drawString(level, _screen.width() / 2, 82);
    _screen.setFont(&fonts::Font0);
    snprintf(level, sizeof(level), "X:%s  Y:%s",
             state.motionFlipX ? "FLIP" : "NORM",
             state.motionFlipY ? "FLIP" : "NORM");
    _screen.setTextColor(p.secondary, p.panel);
    _screen.drawString(level, _screen.width() / 2, 104);
    _screen.setTextColor(state.motionFlipSaved ? p.secondary : p.warning,
                         p.panel);
    _screen.drawString(state.motionFlipSaved ? "AXIS SAVED" : "AXIS NOT SAVED",
                       _screen.width() / 2, 122);
    drawClippedMarquee(_screen, "Tilt to move the pointer", 8, 160,
                       _screen.width() - 16, 12, p.primary,
                       p.background, now);
    drawClippedMarquee(_screen, "Stick U/D: level", 8, 178,
                       _screen.width() - 16, 12, p.primary,
                       p.background, now);
    drawClippedMarquee(_screen, "Click/dbl: left/right", 8, 196,
                       _screen.width() - 16, 12, p.primary,
                       p.background, now);
    drawClippedMarquee(_screen, "A: flip X  B: flip Y", 8, 214,
                       _screen.width() - 16, 12, p.secondary,
                       p.background, now);
    drawClippedMarquee(_screen, "Hold stick: select mode", 8, 232,
                       _screen.width() - 16, 12, p.secondary,
                       p.background, now);
    return;
  
  }
}
