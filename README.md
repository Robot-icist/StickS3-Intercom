# StickS3 Intercom (portable unit)

ESP-NOW intercom firmware for the **M5Stack StickS3**. Pair it with the
`AtomS3R-Intercom` project running on an AtomS3R + Atomic Voice Base.

## Features

- Live push-to-talk audio between the two boards (16 kHz mono, IMA ADPCM).
- Clip mode: record up to 15 s, then transmit it at 1x real time.
- Automatic peer discovery and online/offline indication.
- 135x240 UI with peer name, mode, state, VU meter and battery.
- Audio buffers and the UI sprite live in PSRAM.

## Hardware

| Item | Detail |
| --- | --- |
| Board | M5Stack StickS3 |
| MCU | ESP32-S3-PICO-1-N8R8, 8 MB flash, 8 MB PSRAM |
| Codec | ES8311 + AW8737 PA |
| I2C | SDA 48, SCL 47 |
| Speaker I2S | MCLK 18, BCK 17, WS 15, DOUT 14, `I2S_NUM_0` |
| Mic I2S | MCLK 18, BCK 17, WS 15, DIN 16, `I2S_NUM_1` |
| Buttons | BtnA (G11), BtnB (G12) |

M5Unified config used by the firmware:

```cpp
cfg.internal_mic = true;
cfg.internal_spk = true;
```

The StickS3 uses separate I2S ports for mic and speaker, so the hardware can run
full duplex. The application still uses half-duplex push-to-talk to avoid
feedback, but the audio layer does not need to switch ports.

## Build and flash

Clone this repository with its shared-library submodule:

```sh
git clone --recurse-submodules https://github.com/Robot-icist/StickS3-Intercom.git
```

If it was cloned without submodules, run `git submodule update --init
--recursive` from the repository directory. Then, from this directory:

```powershell
C:\Users\Gille\.platformio\penv\Scripts\pio.exe run
C:\Users\Gille\.platformio\penv\Scripts\pio.exe run -t upload
```

Then build and flash `..\AtomS3R-Intercom` the same way.

## Controls

| Action | Result |
| --- | --- |
| Hold **BtnA** (Live mode) | Transmit while held; incoming audio plays on release. |
| Click **BtnA** (Clip mode) | Start recording; click again to stop and send. |
| Click **BtnB** | Toggle Live / Clip mode when idle. |
| Double-click **BtnA** | Toggle Live / Clip mode, or cancel an active recording. |

## UI

- Header: device name and peer name (or `SEARCH`).
- Large state: `READY`, `RX`, `TALK`, `REC`, `SEND`, `ERROR`.
- Two VU bars: microphone and received audio.
- Mode box, battery, RSSI and a short hint.

## Troubleshooting

- **Peer stays `SEARCH`**: both boards must be on channel 1 and within range.
  Flash both projects; they use the same protocol version.
- **No sound**: check the speaker is enabled in M5Unified and the volume is up.
- **Audio stutters**: keep the boards close; ESP-NOW throughput drops with
  distance. The jitter buffer tolerates short gaps.
- **Feedback/echo**: use Live mode as push-to-talk; do not place the two boards
  next to each other while both speakers are active.
