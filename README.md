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

## BLE HID controller prototype

The separate `sticks3_ble_hid` PlatformIO environment builds a standalone BLE
keyboard/mouse prototype for the StickS3 and M5Stack Unit Joystick2
(I2C address `0x63`, external I2C pins SDA 9/SCL 10). It does not modify or
start the intercom application or the shared library, and it does not start
Wi-Fi or ESP-NOW.

Build and flash it with:

```powershell
C:\Users\Gille\.platformio\penv\Scripts\pio.exe run -e sticks3_ble_hid
C:\Users\Gille\.platformio\penv\Scripts\pio.exe run -e sticks3_ble_hid -t upload
```

Pair the computer with **StickS3 BLE Controller**. The default mouse mode maps
the joystick to pointer movement, a click to left-click, a double-click to
right-click, BtnA/BtnB to Ctrl+C/Ctrl+V, and a left/right gesture to `n` and a
front/back gesture to `y`. These `y`/`n` keystrokes can answer prompts in
OpenCode, Claude Code, Copilot, and similar harnesses.
If the controller was paired before a firmware update, remove/forget the old
Bluetooth device on the computer and pair it again so the host refreshes the
cached HID report descriptor.
Keyboard and mouse use separate application collections and standard input
reports (keyboard report ID 1, mouse report ID 2) in one HID service. Both
reports use the required `0x2A4D` characteristic UUID. The bundled NimBLE
service map originally discarded a second characteristic with the same UUID,
so the isolated controller build applies a small, version-checked patch to
retain distinct characteristic instances. Keep the report IDs distinct and
preserve both characteristics; combining the keyboard and mouse data into one
shared report caused a Windows driver error. The intercom environment does not
invoke this patch hook, and the shared intercom sources remain unchanged. The
screen reports `HID WAIT` until the computer enables
notifications for both reports, then changes to `HID READY`. At 115200 baud,
`[hid] keyboard notifications` and `[hid] mouse notifications` report their
subscription state. Startup retries the Joystick2 I2C probe and keeps running
if the stick is temporarily unavailable, allowing sensor reads to recover
once the bus is ready.

Hold the joystick button for about one second to cycle through Mouse,
Navigation, IMU Orientation, Pointer Speed, and Microphone modes. The selected
mode is saved in NVS. Hold BtnA for about 0.7 seconds to jump directly between
Microphone mode and the previous mode without cycling through the modes in
between; the joystick hold still cycles every mode.
Navigation maps joystick up/down to scrolling, a click/double-click to
Enter/Escape, and BtnA/BtnB to Page Up/Page Down. Yes/no IMU gestures work in
Mouse and Navigation modes.

In IMU Orientation mode, move the joystick left/right to rotate the
controller's base orientation in 90-degree steps. Press BtnA to flip joystick
X and BtnB to flip joystick Y; the flips are independent of base rotation and
apply to both pointer movement and scrolling. Click the joystick to save all
three calibration settings to NVS. The saved rotation is applied to the
joystick axes and IMU gestures, so controls can be aligned with how the
StickS3 is held. The cube fuses gyro motion with the accelerometer for stable
roll and pitch; yaw is integrated from the gyro and will gradually drift
because this board has no magnetometer.

In Pointer Speed mode, move the joystick left/right to choose one of seven
bounded pointer-speed levels. Changes are saved to NVS immediately; click the
joystick to retry saving if NVS reports an error. The default level preserves
the previous mouse sensitivity.

Microphone mode exposes a custom BLE GATT audio stream (not a standard
Bluetooth microphone profile). Connect the companion receiver below and
subscribe to its audio characteristic. Click the joystick to toggle live
streaming; press BtnA to start a clip and press it again to stop recording and
send it; BtnB cancels the current operation. Hold BtnA to return to the mode
you came from, or hold the joystick button to cycle to the next mode (Mouse). If the companion disconnects during audio, capture/transmission
stops and the controller returns to its ready state. Clips are limited to 8
seconds. Live and clip capture are mutually exclusive. Audio is mono, 8 kHz IMA ADPCM;
the StickS3 sends a framed notification every 20 ms. The BLE link must
negotiate an ATT MTU of at least 93 bytes for each frame to fit in one
notification. Audio is delivered only to a companion that has connected and
subscribed; Windows will not list this custom service as a microphone, so the
Python companion below does the recording, transcription, and text injection.

The starter Python companion at `companion/ble_mic_receiver.py` finds the
StickS3, subscribes to the custom service, decodes the frames, and saves each
live session or clip as a WAV file. It first checks whether the StickS3 is
already connected to Windows, then falls back to an advertisement scan and, if
needed, other paired devices. This matters because a StickS3 that is connected
to the PC as a HID device stops advertising: the companion reuses that existing
Windows BLE connection for the microphone GATT service, so mouse and keyboard
input keep working at the same time. On Windows, pair the StickS3 first, then
run:

```powershell
py -m venv .venv
.\.venv\Scripts\python.exe -m pip install -r companion\requirements.txt
.\.venv\Scripts\python.exe -c "import bleak; print(bleak.__file__)"
.\.venv\Scripts\python.exe companion\ble_mic_receiver.py
```

If the automatic lookup cannot find the controller (for example because its
Windows name differs), list the paired devices and connect by address:

```powershell
.\.venv\Scripts\python.exe companion\ble_mic_receiver.py --list
.\.venv\Scripts\python.exe companion\ble_mic_receiver.py --address AA:BB:CC:DD:EE:FF
```

`--name` changes the device name searched for, `--scan-timeout` controls how
long the initial advertisement scan runs, and `--connect-timeout` bounds each
connection attempt. A successful address is cached in
`%LOCALAPPDATA%\StickS3Intercom\companion.json` so later runs can reconnect
without scanning. On macOS and Linux the advertising scan is still the primary
path; pass `--address` to reuse a known address or UUID if the device is
already connected there.

Use that same `.venv\Scripts\python.exe` for installing dependencies and
running the receiver. A bare `pip` or `pip3` may install Bleak for a different
Python interpreter, which still leaves this script unable to import it.
If you already installed Bleak elsewhere, install it into this environment
with `.\.venv\Scripts\python.exe -m pip install bleak`.

WAV recordings are saved to `recordings` by default; pass `--output PATH` to
choose another directory. The receiver runs as a Python CLI and is deliberately
separate from speech-to-text: `companion/live_transcriber.py` watches the same
folder, so either side can be restarted without touching the other. Python with
Bleak is used because it provides a relatively straightforward BLE central API
on Windows, macOS, and Linux.

### Live transcription and dictation

`companion/live_transcriber.py` watches the recordings folder, transcribes each
finished WAV with NVIDIA **Parakeet TDT 0.6B v3** (int8, ~640 MB) through
[`onnx-asr`](https://github.com/istupakov/onnx-asr) on ONNX Runtime, and pastes
the text into whichever input field has the caret. Parakeet is used because it
is the fastest accurate open model on the HF Open ASR leaderboard for short
clips (about 6.3 % average WER, 25 European languages) and runs on the CPU at
several times real time without PyTorch, Transformers, or FFmpeg. The StickS3
stream is 8 kHz, so `onnx-asr` resamples it to the 16 kHz the model expects;
accuracy is therefore limited by the narrowband microphone, not by the model.

Install the extra dependencies once, then run the receiver in one terminal and
the transcriber in another:

```powershell
.\.venv\Scripts\python.exe -m pip install -r companion\requirements.txt
.\.venv\Scripts\python.exe companion\ble_mic_receiver.py
.\.venv\Scripts\python.exe companion\live_transcriber.py
```

Focus the text field you want to dictate into and record on the StickS3. A clip
is transcribed roughly 0.3 s after the receiver closes the file; the first run
downloads the model into the Hugging Face cache and loads it in a few seconds,
later runs are ready almost immediately. The transcript is printed to the
console, copied to the clipboard, and sent with Ctrl+V via `SendInput`, so the
text also stays on the clipboard for a second paste.

Useful options:

- `--watch PATH` watches another folder; `--pattern TEXT` narrows the file name
  filter (default `stickS3`).
- `--model nemo-parakeet-tdt-0.6b-v2` switches to the English-only Parakeet,
  `--quantization fp32` to full precision, and `--device cuda` / `--device
  directml` to a GPU provider when the installed ONNX Runtime supports one.
- `--method type` sends keystrokes instead of using the clipboard (slower, but
  works in apps that block pasted input), `--method none` only prints, and
  `--include-existing` transcribes files that are already in the folder.
- `--trailing-space` / `--no-trailing-space` controls the space appended after
  each paste, `--save-text` writes a `.txt` next to each recording, and
  `--beep` signals that the text was sent.

A WAV is only considered finished once it is a complete file: Python's `wave`
writer (used by the receiver) patches the RIFF sizes on close, so a recording
that is still in progress reports zero frames and is skipped. The file must
also stop changing for `--settle` seconds, and clips shorter than
`--min-duration` or quieter than `--min-peak` are ignored.

Text injection is platform specific: Windows uses `SendInput` (a normal process
cannot send keys to an elevated one, so run both at the same integrity level),
macOS uses `pbcopy` plus `osascript` and needs Accessibility permission, and
Linux uses `wl-copy`/`xclip` plus `ydotool`/`xdotool`.

The custom audio service UUID is
`e5d10001-3c9b-4f6a-9b2d-2a7f4510a001`; its notify characteristic is
`e5d10002-3c9b-4f6a-9b2d-2a7f4510a001`. Each notification starts with a
6-byte little-endian header: protocol version (1), flags (live `0x01`, clip
`0x02`, end `0x80`), 16-bit sequence number, and 16-bit payload length. Audio
frames contain an 84-byte IMA ADPCM block: signed 16-bit initial predictor,
step index, reserved byte, then low-nibble-first ADPCM samples. End packets
have a zero payload length. The receiver and firmware implement this initial
protocol together; it is not a standardized BLE audio format.

### HID and microphone regression guard

These are known-good integration requirements for the controller. Preserve
them when changing BLE setup, input handling, or companion behavior:

- Keep keyboard and mouse as separate HID application collections and separate
  input reports: keyboard report ID 1 is 8 bytes; mouse report ID 2 is 4
  bytes. Both use the standard `0x2A4D` input-report characteristic UUID.
  Do not combine them into one report or reuse one report ID; that composite
  layout caused a Windows driver error.
- The bundled NimBLE implementation normally drops a second characteristic
  with the same UUID from its service map. The HID-only PlatformIO environment
  uses `scripts/patch_nimble_hid_reports.py` to preserve both report
  characteristics. Keep this patch scoped to `sticks3_ble_hid`; do not patch
  the shared Intercom library or create duplicate `0x1812` HID services.
  The patch is deliberately version-checked and fails the build if the
  framework source no longer matches the expected code. Re-check the NimBLE
  service registration behavior before updating or replacing the patch.
- Preserve the HID report-map IDs, the `inputReport(1)` / `inputReport(2)`
  characteristics, and each characteristic's notification descriptor. HID
  is ready only after Windows subscribes to both reports. Confirm both
  `[hid] keyboard notifications enabled` and
  `[hid] mouse notifications enabled` in the serial log.
- Preserve the PnP ID's 7-byte little-endian layout:
  `{0x02, 0x3A, 0x30, 0x01, 0x00, 0x00, 0x01}`. The bundled
  `BLEHIDDevice::pnp()` helper writes multi-byte IDs in the wrong byte order
  for this device, so the controller sets the characteristic value directly.
  Also create the optional manufacturer characteristic with
  `_hid->manufacturer()` before setting its value with
  `_hid->manufacturer("M5Stack")`.
- Keep microphone controls independent of host HID readiness: in Microphone
  mode, a short joystick click toggles live audio; BtnA starts/stops a clip;
  BtnB cancels; holding BtnA returns to the previous mode; holding the joystick
  cycles to the next mode. Do not return
  early from microphone-mode joystick handling before processing click/release
  and hold transitions. If the GATT companion disconnects during live capture,
  recording, or clip transmission, stop the operation and return the mic to
  idle so the user can still switch modes.
- Audio starts only after a companion is connected and subscribed to the
  custom notification characteristic. The companion must keep that BLE
  connection open; its receiver saves the incoming audio as WAV and is not a
  Windows microphone endpoint.
- The microphone GATT service shares the same BLE link as the HID service.
  A StickS3 already connected to Windows for mouse/keyboard stops advertising,
  so the companion must check paired/connected-device discovery (or a known
  address) before scanning. Do not make the HID link and the companion audio
  link mutually exclusive.

Before changing these paths, read this section and inspect the HID report map,
NimBLE patch hook, report notification callbacks, and microphone-mode input
handling together. After a change, build with
`pio run -e sticks3_ble_hid`, then test on the hardware:

1. If the HID descriptor or PnP data changed, remove/forget the prior
   **StickS3 BLE Controller** pairing in Windows and pair it again.
2. Confirm Windows has no HID driver error, the screen reaches `HID READY`,
   and the serial log shows both keyboard and mouse notifications enabled.
   Verify pointer movement and a keyboard action, not just BLE connection.
3. Connect the Python companion and subscribe; test joystick-click live
   start/stop and BtnA clip record/send. Confirm audio arrives and is saved.
4. Disconnect the companion during a mic operation; confirm the operation
   stops and holding the joystick still leaves Microphone mode.

Compilation alone does not validate Windows HID enumeration, pairing, or BLE
audio delivery; those checks require the physical StickS3 and host.

The controller screen uses a compact layout and automatically scrolls any
instruction line that is wider than the display. The header shows the StickS3
battery icon and percentage read through M5Unified's `M5.Power`; it falls back
to `USB` when the battery level is unavailable.

The controller is isolated in `src/controller` so it can later be moved into a
shared-library module. Its coordinator, input/gesture features, BLE HID
transport, and display UI are separated into `BleHidController`,
`ControllerFeatures`, `BleHidDevice`, and `ControllerUi`, respectively.
The controller initializes the onboard microphone and speaker hardware. The
custom BLE microphone sender and WAV receiver are an initial prototype;
speaker reception/playback is not implemented, while transcription and
focused-text injection are provided by `companion/live_transcriber.py`. The ESP32-S3 does not provide Bluetooth Classic A2DP/HFP,
so any PC audio endpoint still requires companion software. The next steps
are to test BLE notification throughput and audio quality on hardware, then
add speaker reception as a separate mode if desired. Wi-Fi audio can be
evaluated later while keeping BLE HID active, followed by coexistence tests
with the fixed-channel ESP-NOW intercom.
