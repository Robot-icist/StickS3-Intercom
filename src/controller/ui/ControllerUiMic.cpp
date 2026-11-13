#include "../ControllerUi.h"

#include <cmath>
#include <cstdio>

namespace controller
{
  void ControllerUi::renderMicMode(const ControllerViewState &state, uint32_t now, const Palette &p)
  {
    const char *status = "MIC UNAVAILABLE";
    uint16_t statusColor = p.warning;
    switch (state.micStatus)
    {
    case MicAudioStatus::Idle:
      status = state.micSubscribed ? "AUDIO READY" : "CONNECT COMPANION";
      statusColor = state.micSubscribed ? p.connected : p.warning;
      break;
    case MicAudioStatus::Live:
      status = "LIVE STREAM";
      statusColor = p.connected;
      break;
    case MicAudioStatus::Recording:
      status = "RECORDING";
      statusColor = p.warning;
      break;
    case MicAudioStatus::Sending:
      status = "SENDING CLIP";
      statusColor = p.connected;
      break;
    case MicAudioStatus::Error:
      status = "MIC ERROR";
      statusColor = p.error;
      break;
    case MicAudioStatus::Unavailable:
      break;
    }

    _screen.fillRoundRect(6, 48, _screen.width() - 12, 72, 7, p.panel);
    _screen.setTextDatum(lgfx::textdatum_t::top_center);
    _screen.setFont(&fonts::Font0);
    _screen.setTextColor(p.accent, p.panel);
    _screen.drawString("BLE MICROPHONE", _screen.width() / 2, 60);
    _screen.setFont(&fonts::Font2);
    _screen.setTextColor(statusColor, p.panel);
    _screen.drawString(status, _screen.width() / 2, 82);
    _screen.setFont(&fonts::Font0);
    drawClippedMarquee(_screen, "Hold stick: live stream", 8, 145,
                       _screen.width() - 16, 12, p.primary,
                       p.background, now);
    drawClippedMarquee(_screen, "Click: left | double-click: right", 8, 163,
                       _screen.width() - 16, 12, p.secondary,
                       p.background, now);
    drawClippedMarquee(_screen, "A: record / send clip", 8, 181,
                       _screen.width() - 16, 12, p.secondary,
                       p.background, now);
    drawClippedMarquee(_screen, "Front/Enter: accept | Back/Esc: cancel", 8, 199,
                       _screen.width() - 16, 12, p.secondary,
                       p.background, now);
    drawClippedMarquee(_screen, "Requires BLE audio companion", 8, 217,
                       _screen.width() - 16, 12, p.secondary,
                       p.background, now);
    return;
  
  }
}
