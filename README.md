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

M5Unified config used by the controller firmware:

```cpp
cfg.internal_mic = true;
cfg.internal_spk = true;
M5.begin(cfg);
M5.Speaker.end();  // disable the AW8737 PA; the controller never plays audio
```

The StickS3 has separate I2S ports for the microphone and speaker, but both are
wired to the same **ES8311** codec. M5Stack's StickS3 microphone example
explicitly turns the speaker off before starting the mic because the codec
cannot run capture and playback at the same time. The M5PM1 also exposes the
speaker amplifier power as a GPIO (`M5.Speaker.begin()` enables the AW8737 PA,
`M5.Speaker.end()` disables it). M5Stack documents the same requirement for the
IR receiver, which also needs the amplifier off.

M5Stack's StickS3 product page notes that **early batches may produce slight
abnormal noise after startup**. The controller does not play audio at all, so it
keeps the PA disabled from boot and again before every capture; this removes
the idle hiss/whistle that the shared codec and amplifier can otherwise inject
into the recording.

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

## Wi-Fi OTA updates

The intercom and standalone BLE HID environments support authenticated
firmware OTA; the intercom also supports LittleFS image updates. AtomS3R
intercom firmware uses the same OTA service from `IntercomShared`. OTA needs
Wi-Fi and does not replace the first USB flash: install an OTA-enabled build
over USB once on each board before using this workflow. The BLE HID firmware
does not currently use LittleFS. Its partition can accept an update, but the
current LittleFS image comes from the project data directory and has no
controller runtime effect.

The intercom joins Wi-Fi using its existing setup and saved credentials. BLE
HID reuses the same `intercom` NVS Wi-Fi settings. If it has no saved network
or cannot connect, it starts a protected `STICK-XXXX-Setup` access point; use
the OTA password printed to serial on first boot, connect to that AP, and open
`http://192.168.4.1/` to save the Wi-Fi network. The board restarts after
saving. Keep the per-device OTA password printed to serial on its first
OTA-enabled boot; it is generated once and stored in NVS as `ota_pass`, and
the same password is used by intercom and BLE HID firmware on that board.
The StickS3 hostname is `STICK-XXXX`; the AtomS3R hostname is `ATOM-XXXX`.

Run the interactive deployer from this repository:

```powershell
.\.venv\Scripts\python.exe .\companion\ota_deploy.py
```

Start with StickS3 BLE HID firmware running; it exposes the encrypted,
read-only BLE characteristic used to hand the deployer a short-lived OTA
password. Select `sticks3_ble_hid` or `sticks3_intercom` as the build target.
The deployer only offers StickS3 targets because the AtomS3R intercom does not
provide this BLE credential service. The environment chooser defaults to
`sticks3_ble_hid`; the selected environment determines the build and matching
image path. Firmware only is the default; choose LittleFS/both when needed.
For both images, LittleFS is uploaded first and the deployer reconnects over
BLE for fresh credentials after the device restarts before uploading firmware.
It never exposes the permanent NVS password. If multiple compatible devices
are found, a device-selection popup appears. The device must be connected to
Wi-Fi and the computer and board must be on a network that permits ESP32 OTA
traffic.

If you install StickS3 intercom firmware, BLE credential handoff is no longer
available afterward; use the intercom's Wi-Fi OTA workflow for later updates.

Pair the device with the computer when prompted; Windows can reuse an existing
BLE HID pairing even when the StickS3 is already connected as a keyboard and
mouse. The firmware uses Just Works BLE pairing, which encrypts the link but
does not verify a peer with a passkey, so run OTA only in a trusted area.
Left/right gestures or arrow keys change popup selections; Front/Enter accepts,
back/Escape cancels. While a choice popup is open, say an option to highlight
it, then use the StickS3 to accept it. Never expose the OTA service or setup
access point to an untrusted network.

The companion's exact-name action list can launch the deployer interactively
after its normal confirmation prompt. Copy or merge the `ota update` entry
from [companion/actions.example.json](./companion/actions.example.json) into
`%LOCALAPPDATA%\StickS3Intercom\actions.json`, then say “Run action: ota
update”. Edit the Python, project, and PlatformIO paths in the action for your
checkout. An OTA-enabled device can still be recovered or initially flashed
over USB with PlatformIO.

The PlatformIO **Project Tasks** tree provides one-click OTA targets under
**Custom** in both the StickS3 and AtomS3R projects:

- `OTA: BLE HID firmware (default)` builds and uploads the recommended
  StickS3 BLE HID firmware without chooser prompts.
- `OTA: choose environment and image` opens the environment and image prompts,
  with StickS3 BLE HID preselected.
- `OTA: firmware`, `OTA: LittleFS`, or `OTA: firmware and LittleFS` preselects
  the image type and prompts for the environment. BLE discovery chooses a
  unique matching device automatically, or shows a device picker when needed.

Open the PlatformIO sidebar, expand **Project Tasks**, then expand the active
environment and **Custom**. Use an **OTA:** task, not the standard **Build** or
**Upload** task: the standard Build task only compiles and the standard Upload
task uses USB. The default firmware task builds StickS3 BLE HID, requests
temporary credentials over BLE, and runs the Wi-Fi OTA transfer without a
chooser. The fixed image tasks use the PlatformIO environment under which
they appear. For example, from a terminal:

```powershell
pio run -e sticks3_ble_hid -t ota_deploy
```

Use `OTA: choose environment and image` (or `-t ota_choose`) when you want to
select a different StickS3 firmware or image interactively:

```powershell
pio run -e sticks3_ble_hid -t ota_choose
```

The task output reports BLE discovery, device/IP selection, uploader
authentication, and transfer progress; it returns a failure status if any
stage fails. `pio run -t upload` remains a serial/USB upload; no OTA protocol
is set as the default.

Both the intercom and BLE HID screens rotate their header through the device
name, current IP address, mDNS name, and OTA password. The password is shown
on-device only, so anyone who can view the display can read it. Its display
phase lasts about 10 seconds to make the full password easier to read.

## Controls

| Action | Result |
| --- | --- |
| Hold **BtnA** (Live mode) | Transmit while held; incoming audio plays on release. |
| Click **BtnA** (Clip mode) | Start recording; click again to stop and send. |
| Click **BtnB** | Toggle Live / Clip mode when idle. |
| Double-click **BtnA** | Toggle Live / Clip mode, or cancel an active recording. |

## UI

- Header: rotating device/network/OTA details alongside the peer name (or
  `SEARCH`).
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
start the intercom application or the shared library. It starts Wi-Fi only
for protected setup and authenticated OTA; it does not start ESP-NOW.

Build and flash it with:

```powershell
C:\Users\Gille\.platformio\penv\Scripts\pio.exe run -e sticks3_ble_hid
C:\Users\Gille\.platformio\penv\Scripts\pio.exe run -e sticks3_ble_hid -t upload
```

Pair the computer with **StickS3 BLE Controller**. The default mouse mode maps
the joystick to pointer movement, a click to left-click, a double-click to
right-click, BtnA/BtnB to Ctrl+C/Ctrl+V, and IMU gestures to menu navigation:
left/right moves to the previous/next option, front accepts (Enter), and back
cancels (Escape). These gestures work in the companion's interactive popups
and in other applications with keyboard-selectable menus. The controller
cannot detect which application is active; keep the intended prompt focused
and verify its current selection before gesturing.
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

Hold the joystick button for about one second to open the mode picker. Move
the stick up/down to choose Mouse, Air Mouse, Magnifier, Navigation, Voice
Action, Microphone, Pointer/Motion Speed, or IMU Orientation; click to select
and BtnB to cancel. The selected mode is saved in NVS. Hold BtnA for about 0.7
seconds to jump directly between Microphone mode and the previous mode without
cycling through the modes in between. The joystick hold opens the mode picker
from every mode except Microphone, where holding the stick streams live audio.
Navigation maps joystick up/down to scrolling, a click/double-click to
Enter/Escape, and BtnA/BtnB to Page Up/Page Down. Approval-menu IMU gestures
work in Mouse, Navigation, Microphone, Voice Action, and Pointer/Motion Speed
modes, but not in IMU Orientation, Air Mouse, or Magnifier.

Air Mouse mode turns the StickS3 into a gyro pointer. Tilt the controller to
move the mouse; yaw drives horizontal motion and pitch drives vertical motion.
The pointer uses a deadzone plus an exponential filter so small hand tremor
does not jitter the cursor while fast movements stay responsive, and fast
flicks get a gentle speed boost so a normal wrist turn can cross the whole
desktop. Move the stick up/down to change the seven motion levels; the level is
saved to NVS immediately. Joystick click and double-click are left and right
click. BtnA flips the gyro X axis and BtnB flips the gyro Y axis; both flips
are saved and are also used by Magnifier mode. The saved base rotation applies
to the gyro axes, and the joystick X/Y flips from IMU Orientation stay
independent. In Pointer/Motion Speed mode, move the joystick left/right for
joystick pointer speed and up/down for gyro motion speed. Only angular velocity
is used, so the pointer does not drift like an integrated orientation.

Magnifier mode controls the built-in Windows Magnifier through normal HID
keyboard shortcuts, so no companion or overlay process is needed. Click the
joystick to open or close it (Win+Plus / Win+Esc), double-click to cycle
full-screen, lens, and docked views (Ctrl+Alt+M), move the stick up/down to
zoom, and move it left/right or tilt the controller to pan (Ctrl+Alt+arrows).
BtnA and BtnB also zoom in and out. Gyro panning reuses the Air Mouse motion
filter, deadzone, X/Y flips, and seven-level sensitivity, so tune the feel in
Air Mouse mode. The screen shows the controller's local open/closed state,
which can drift if the Magnifier is closed from the keyboard; click once to
clear the stale state and again to reopen. These are standard Windows Magnifier
shortcuts, so the feature works with the stock Magnifier application.

Voice Action mode uses the same BLE microphone stream for command-only speech.
Click the joystick or press BtnA to start and stop a recording; BtnB cancels.
The audio is tagged as a command rather than dictation, so the companion routes
it through the voice-action matcher and never pastes it into the focused field.
Bare action names work ("magnifier", "git status"), as do the usual explicit
prefixes such as "Run action: ...". This mode needs the companion running with
`--voice-commands` (the Windows desktop companion enables it by default). See
the action examples below for a ready-made "magnifier" action.

In IMU Orientation mode, move the joystick left/right to rotate the
controller's base orientation in 90-degree steps. Press BtnA to flip joystick
X and BtnB to flip joystick Y; the flips are independent of base rotation and
apply to joystick pointer movement and scrolling. Air Mouse and Magnifier use
their own saved X/Y motion flips (BtnA/BtnB in Air Mouse mode). Click the
joystick to save all three calibration settings to NVS. The saved rotation is
applied to the joystick axes and IMU gestures, so controls can be aligned with
how the StickS3 is held. The cube fuses gyro motion with the accelerometer for
stable roll and pitch; yaw is integrated from the gyro and will gradually drift
because this board has no magnetometer.

Microphone mode exposes a custom BLE GATT audio stream (not a standard
Bluetooth microphone profile). Connect the companion receiver below and
subscribe to its audio characteristic. Click the joystick to toggle live
streaming; press BtnA to start a clip and press it again to stop recording and
send it; BtnB cancels the current operation. Hold BtnA to return to the mode
you came from. The joystick is reserved for live audio in this mode, so open
the mode picker from another mode. If the companion disconnects during audio,
capture and transmission stop and the controller returns to its ready state.
Clips are limited to 8 seconds. Live and clip capture are mutually exclusive.
Audio is mono, 16 kHz IMA ADPCM; the StickS3 sends a framed notification every
20 ms. The frame grows from 84 to 164 encoded bytes (a 170-byte packet), so the
BLE link must negotiate an ATT MTU of at least 173 bytes; the firmware already
requests 185, giving a 182-byte notification payload. Doubling the sample rate
doubles the audio bandwidth (wideband speech) while keeping the same 20 ms
cadence, packet rate, HID reports, OTA service, and heartbeat. Audio is delivered
only to a companion that has connected and subscribed; Windows will not list this
custom service as a microphone, so the Python companion below does the
recording, transcription, and text injection.
The companion refreshes a BLE heartbeat every second; without one for four
seconds the StickS3 stops showing `AUDIO READY`, even if Windows retains a
stale GATT subscription.

The starter Python companion at `companion/ble_mic_receiver.py` finds the
StickS3, subscribes to the custom service, decodes the frames, and saves each
live session, clip, or voice command as a WAV file. It first checks whether the
StickS3 is already connected to Windows, then falls back to an advertisement
scan and, if needed, other paired devices. This matters because a StickS3 that
is connected to the PC as a HID device stops advertising: the companion reuses
that existing Windows BLE connection for the microphone GATT service, so mouse
and keyboard input keep working at the same time. On Windows, pair the StickS3
first, then run:

```powershell
py -m venv .venv
.\.venv\Scripts\python.exe -m pip uninstall -y onnxruntime
.\.venv\Scripts\python.exe -m pip install -r companion\requirements.txt
.\.venv\Scripts\python.exe -c "import bleak; print(bleak.__file__)"
.\.venv\Scripts\python.exe companion\ble_mic_receiver.py
```

The companion installs NVIDIA's CUDA-enabled ONNX Runtime and its CUDA/cuDNN
runtime libraries. Automatic device selection prefers CUDA when available and
falls back to CPU otherwise. A recent NVIDIA driver is required; this does not
require the full CUDA Toolkit to be installed separately.

Add `--transcribe` to chain the receiver to the speech-to-text companion
described in the next section, so one command records and dictates.

### One-line launchers

After installing the dependencies above, each of these is a complete command:

```powershell
# 1. Receiver only: save live streams and clips as WAV files.
.\.venv\Scripts\python.exe companion\ble_mic_receiver.py

# 2. Transcriber only: dictate recordings produced by another receiver.
.\.venv\Scripts\python.exe companion\live_transcriber.py

# 3. Receiver plus transcriber: paste once after each capture finishes.
.\.venv\Scripts\python.exe companion\ble_mic_receiver.py --transcribe

# 4. Same, plus partial text while you are still talking (see below).
.\.venv\Scripts\python.exe companion\ble_mic_receiver.py --transcribe --transcribe-arg=--partial-live

# 5. Two terminals for partial live: start the receiver, then this.
.\.venv\Scripts\python.exe companion\live_transcriber.py --partial-live

# 6. One dictation, English-only model, automatically uses CUDA when available.
.\.venv\Scripts\python.exe companion\live_transcriber.py --model nemo-parakeet-tdt-0.6b-v2

# 7. Full companion: BLE receiver, transcription, partial live text, and voice commands.
.\.venv\Scripts\python.exe companion\ble_mic_receiver.py --voice-commands --transcribe-arg=--partial-live
```

Focus the input field and leave the caret where the text should go before
recording. With `--transcribe`, `--transcribe-method type|none` sends keystrokes
instead of pasting, or only prints to the console.

The full companion command above keeps the terminal open for confirmation
before agent prompts and local actions are run. On the first transcription,
Parakeet is downloaded to the Hugging Face cache (about 640 MB); subsequent
runs reuse the cache.

#### Windows desktop companion

The Windows companion opens a small dashboard for BLE connection status,
transcription activity, recordings, and voice-action setup. It automatically
starts BLE audio reception, partial live transcription, and confirmed voice
command routing. From the repository root, build it with the project virtual
environment:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\companion\build_windows.ps1
```

The script installs the build-only PyInstaller dependency into `.venv` and
creates a windowed onedir bundle at `dist\StickS3Companion`. Launch it with:

```powershell
.\dist\StickS3Companion\StickS3Companion.exe
```

The same operations are available in the PlatformIO Tasks view for either
environment, or from the repository root. For example:

```powershell
pio run -e sticks3_ble_hid -t companion_build_windows
pio run -e sticks3_ble_hid -t companion_run
```

| Task | Operation |
| --- | --- |
| `companion_build_windows` | Build the packaged Windows app |
| `companion_run` | Launch the packaged executable from `dist` |
| `companion_run_source` | Launch the desktop app from the project Python environment |
| `companion_tests` | Run the companion Python test suite |

Build the executable before using `companion_run`.

The dashboard starts listening automatically and shows connection and
transcription status. Select **Configure actions** to add, edit, or remove
allow-listed commands. Each action has a spoken name, executable, arguments
(one per line), optional working directory, description, and a confirmation
setting. After saving an action, say “run action: action name”; changes are
picked up while the companion is running. Say “OpenCode …” or “OpenCode: …”
to send a confirmed request to OpenCode, or say “Claude …” / “Copilot …” for
those CLIs. Both “OpenCode” and “open code” are recognized. These requests
open desktop confirmation/session dialogs instead of waiting for terminal
input.

Unprefixed speech is transcribed and pasted into the focused input field.
Partial transcription updates the text while recording, then replaces it with
the final result. Press the StickS3 joystick to start a live utterance or BtnA
to send a clip. The model is downloaded on first use to the current Windows
user's Hugging Face cache and reused on later launches.

The executable bundle includes the application and its Python dependencies,
but not the Parakeet model; the first transcription still needs internet access
to download the model. This is an onedir build, so distribute the whole
`dist\StickS3Companion` folder rather than copying the `.exe` alone. For
command-line diagnostics such as `--list`, use the Python receiver commands
above; double-click the executable with no arguments to open the dashboard.

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
choose another directory. Each receiver startup removes previous StickS3
recording WAV files from that output directory before connecting; unrelated
files and subdirectories are left untouched. The receiver runs as a Python CLI
and is deliberately separate from speech-to-text:
`companion/live_transcriber.py` watches the same
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
several times real time without PyTorch, Transformers, or FFmpeg. Current
firmware streams 16 kHz wideband audio, matching the model's native rate, so
`onnx-asr` no longer needs to upsample. Recordings made with older 8 kHz
firmware still work and are resampled automatically.

All capture modes of the receiver are handled, because each produces a complete
WAV in the same folder:

- `stickS3_0001_live.wav` - the streamed push-to-talk audio. The firmware sends
  an end marker when the stream stops, the receiver closes the file, and the
  whole live session is transcribed at once. Add `--partial-live` to see text
  appear while you are still talking (see below); otherwise the transcript
  appears when the stream ends.
- `stickS3_0001_clip.wav` - audio recorded on the StickS3 and replayed at 1x
  real time. It is transcribed once the clip transfer completes.
- `stickS3_0001_command.wav` - a Voice Action mode utterance. It is transcribed
  and routed as a command instead of pasted as dictation. Command recordings
  are processed even when `--modes live` or `--modes clip` limits dictation.

Install the extra dependencies once, then use one of the
[one-line launchers](#one-line-launchers) above (`--transcribe` chains the two
processes):

```powershell
.\.venv\Scripts\python.exe -m pip uninstall -y onnxruntime
.\.venv\Scripts\python.exe -m pip install -r companion\requirements.txt
```

or run the two processes in separate terminals (the receiver owns the BLE link,
the transcriber only watches the folder, so either can be restarted alone):

```powershell
.\.venv\Scripts\python.exe companion\ble_mic_receiver.py
.\.venv\Scripts\python.exe companion\live_transcriber.py
```

With `--transcribe` or `--voice-commands`, the receiver starts the transcriber
as a child process before it begins its BLE connection. The ASR model is
downloaded if needed, loaded, and warmed up at companion startup, so it is
ready before the first recording; the first-ever startup can take longer while
the roughly 640 MB model is downloaded. Inference never blocks the BLE event
loop, and the receiver stops the transcriber on exit; the child also watches
the receiver's pid and exits if the receiver dies, so a crash cannot leave a
second transcriber behind. In the packaged executable, the child runs the
transcriber from the same executable. Do not combine it with another
transcriber on the same folder, or every recording is pasted twice.
`--transcribe-arg=...` forwards extra flags, for example
`--transcribe-arg=--modes --transcribe-arg=live`.

Focus the text field you want to dictate into and record on the StickS3. A
capture is transcribed roughly 0.3 s after the receiver closes its file; the
first run downloads the model into the Hugging Face cache and loads it in a few
seconds, later runs are ready almost immediately. The transcript is printed to
the console, copied to the clipboard, and sent with Ctrl+V via `SendInput`, so
the text also stays on the clipboard for a second paste.

#### Partial live text

`--partial-live` reads the live WAV while the receiver is still writing it and
recognises the audio so far every `--partial-interval` seconds (default 1.5 s),
so words appear while the button is still held. The field is updated in place:
the longest common prefix with the previous result is kept, and only the
changed tail is replaced with backspaces plus a paste (or keystrokes with
`--method type`). When the stream ends, the whole session is transcribed once
more and the field is corrected without duplicating text.

Leave the caret at the end of the partial transcript while talking. Updates
edit the text at the caret, so moving it or typing in the same field during a
stream makes the next update overwrite the wrong characters. Only live streams
are affected; clips and the default behaviour without the flag are unchanged.
Each update needs at least `--partial-min-audio` seconds of new audio (default
0.6 s) and passes get less frequent as a stream grows, because every pass
re-transcribes the session from the start; beyond 28 s that uses Silero VAD.
Partial text is a preview, and the final pass after the end marker is
authoritative.

#### Voice commands for OpenCode, Claude Code, and Copilot

Voice command routing is opt-in for command-line use. The Windows desktop
companion enables it by default. Start the receiver and transcriber together:

```powershell
.\.venv\Scripts\python.exe companion\ble_mic_receiver.py --voice-commands
```

Or add `--voice-commands` to `live_transcriber.py` if the receiver is already
running separately. At the end of a capture, use one of these explicit spoken
prefixes:

- **“Dictate: …”** pastes the remaining words into the focused field.
- **“OpenCode: …”** sends a prompt to the selected OpenCode session.
- **“Claude: …”** (or “Claude Code: …”) runs the Claude Code CLI prompt.
- **“Copilot: …”** (or “GitHub Copilot: …”) runs a GitHub Copilot CLI prompt.
- **“Run action: …”** runs a named, locally allowlisted action.

The controller's Voice Action mode marks its recordings as commands. That lets
you say a bare action name such as “magnifier” or “git status” without a
prefix; unmatched command speech is reported and is never pasted as dictation.
If voice routing is not enabled, command recordings are skipped.

Unprefixed speech remains ordinary dictation. With `--partial-live`, partial
transcripts are never sent to an agent or action; routing only happens after
the final transcript is ready. Agent prompts and non-interactive local actions
show their destination and require confirmation. The Windows desktop app uses
confirmation/session dialogs; the command-line companion asks for `y` or
`yes` in its terminal. A cancelled prompt, an unknown action, a missing CLI,
or a command failure does not fall back to pasting the command into the
focused field. The agent CLIs may also request their own tool permissions;
review and approve those in the CLI window. The companion never adds automatic
“allow all tools” flags.

**OpenCode:** Install and sign in to the `opencode` CLI, then launch the
companion from the project directory you want OpenCode to work in. The
companion lists that project's sessions with `opencode session list --format
json`, asks which session to use, and sends the confirmed prompt with
`opencode run --session`. This uses OpenCode's CLI/background service and does
not require manually starting `opencode serve` or keeping port 4096 open.
Set `OPENCODE_SESSION_ID` to skip session selection, or
`OPENCODE_CLI_COMMAND` if the CLI has a different name. When running
`live_transcriber.py` directly, `--opencode-command` is also available. The
companion never passes `--auto`; OpenCode's own permission prompts remain
visible in the terminal.

**Claude Code:** Install and authenticate the `claude` CLI, and run the
companion from the project directory you want Claude to work in. The companion
starts a confirmed `claude -p` invocation and leaves Claude's own permission
prompts visible. By default this is a one-shot CLI run; set
`CLAUDE_CODE_SESSION_ID` to resume a specific CLI session, or
`CLAUDE_CODE_COMMAND` / `--claude-command` if the executable is named
differently. `--claude-cwd PATH` selects its working directory.

**GitHub Copilot CLI:** Install and sign in to [GitHub Copilot CLI](https://docs.github.com/en/copilot/how-tos/copilot-cli/set-up-copilot-cli/install-copilot-cli)
(on Windows, `winget install GitHub.Copilot`), then use “Copilot: …”. The
companion starts a one-shot request using the CLI's documented programmatic
`--prompt` interface. It does not enable automatic tool approval. Use
`COPILOT_CLI_COMMAND` or `--copilot-command` to select a non-default
executable, and `--copilot-cwd PATH` to select the project directory.
Copilot CLI may use its own login, repository trust, and tool permission
prompts; follow those prompts in the companion terminal.

Local actions are an exact-name allowlist, not shell commands derived from
speech. The example includes read-only Git checks, companion tests, BLE device
listing, controller firmware build, Windows companion packaging, a Windows
Magnifier launcher, and an interactive authenticated OTA deployer. It is
tailored to the current Windows checkout path; edit its `cwd` and executable
paths if your repository or PlatformIO installation is elsewhere. Install it
to the per-user location:

```powershell
$actionsDirectory = "$env:LOCALAPPDATA\StickS3Intercom"
$actionsPath = Join-Path $actionsDirectory "actions.json"
New-Item -ItemType Directory -Force $actionsDirectory
if (-not (Test-Path $actionsPath)) {
    Copy-Item companion\actions.example.json $actionsPath
} else {
    Write-Host "Keeping existing actions.json; merge selected template entries manually."
}
```

If `actions.json` exists, merge the desired entries into it instead of
overwriting your actions.
Say “Run action: git status”, “Run action: run companion tests”, or one of the
other exact configured names. Edit `actions.json` to add fixed
executable/argument arrays and optional working directories. Relative working
directories are resolved relative to that JSON file. Speech only selects the
exact configured action name; it is never appended to the executable
arguments, and commands are launched without a shell. Each action still asks
for confirmation before it runs. Interactive actions use a desktop popup;
other actions ask in the terminal. Use only scripts you trust.
Actions marked `interactive` run in the background so the transcriber remains
available. The OTA action uses desktop dialogs for environment/image choices
and address/password entry; spoken choice names highlight an option, but
speech is never entered into text fields.

These integrations are intentionally separate adapters: OpenCode uses its
local API, Claude Code and GitHub Copilot use their supported CLIs, and local
actions use explicit fixed argument lists. This keeps the BLE receiver and
local Parakeet transcription usable without agent tools installed. See the
[OpenCode SDK/server documentation](https://opencode.ai/docs/sdk/) and the
[Claude Agent SDK / headless CLI documentation](https://code.claude.com/docs/en/agent-sdk/overview)
for future deeper, session-aware integrations.

Useful options:

- `--partial-live` enables live updates, `--partial-interval SECONDS` changes
  how often they are attempted (default 1.5), and `--partial-min-audio SECONDS`
  sets how much new audio is needed (default 0.6).
- `--modes all|live|clip|command` picks which capture mode is dictated (default
  `all`), and `--watch PATH` / `--pattern TEXT` change the folder and file name
  filter (default `recordings` and `stickS3`).
- `--model nemo-parakeet-tdt-0.6b-v2` switches to the English-only Parakeet,
  `--quantization fp32` to full precision, and `--device auto|cuda|directml|cpu`
  selects an execution provider. `auto` prefers NVIDIA CUDA on supported
  systems and otherwise uses DirectML or CPU.
- `--method type` sends keystrokes instead of using the clipboard (slower, but
  works in apps that block pasted input), `--method none` only prints, and
  `--include-existing` transcribes files that are already in the folder.
- `--trailing-space` / `--no-trailing-space` controls the space appended after
  each paste, `--save-text` writes a `.txt` next to each recording, and
  `--beep` signals that the text was sent.

A WAV is only considered finished once its RIFF sizes account for the whole
file. Python's `wave` writer (used by the receiver) puts the first chunk's
length in the header and patches the real sizes on close, so a growing live
file is normally treated as unfinished. The file must also stop changing for
`--settle` seconds, and clips shorter than `--min-duration` or quieter than
`--min-peak` are ignored. Partial live scanning bypasses `wave.open` and reads
the bytes after the data chunk header directly, which is what lets it follow a
file that is still open. A live session that runs longer than 28 s is split
with Silero VAD before recognition, and a live file that is cancelled or
interrupted by a disconnect is still closed and transcribed.

Text injection is platform specific: Windows uses `SendInput` (a normal process
cannot send keys to an elevated one, so run both at the same integrity level),
macOS uses `pbcopy` plus `osascript` and needs Accessibility permission, and
Linux uses `wl-copy`/`xclip` plus `ydotool`/`xdotool`.

The custom audio service UUID is
`e5d10001-3c9b-4f6a-9b2d-2a7f4510a001`; its notify characteristic is
`e5d10002-3c9b-4f6a-9b2d-2a7f4510a001`. Each notification starts with a
6-byte little-endian header: protocol version (2), flags (live `0x01`, clip
`0x02`, voice command `0x04`, wideband `0x08`, end `0x80`), 16-bit sequence
number, and 16-bit payload length. Audio
frames contain an IMA ADPCM block: signed 16-bit initial predictor, step index,
reserved byte, then low-nibble-first ADPCM samples. Version 2 carries 320
samples (20 ms at 16 kHz) in 164 bytes; version 1 carried 160 samples (20 ms at
8 kHz) in 84 bytes. End packets have a zero payload length. The receiver accepts
both versions and uses the wideband flag to write a 16 kHz WAV; the firmware and
companion implement this protocol together, and it is not a standardized BLE
audio format.

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
  BtnB cancels; holding BtnA returns to the previous mode. The joystick hold
  is reserved for live audio in Microphone mode, so the mode picker is opened
  from other modes. Do not return early from microphone-mode joystick handling
  before processing click/release and hold transitions. If the GATT companion
  disconnects during live capture, recording, or clip transmission, stop the
  operation and return the mic to idle so the user can still switch modes.
- Keep the AW8737 speaker amplifier disabled whenever the microphone is
  capturing. The StickS3 shares its ES8311 codec between capture and playback,
  and M5Stack requires `M5.Speaker.end()` before `M5.Mic.begin()`. The
  controller never plays audio, so it disables the PA at boot and before every
  capture; this also suppresses the idle whistle/hiss noted for early StickS3
  batches. Do not start the speaker just to "initialize" the codec.
- Voice Action mode is command-only: a joystick click or BtnA toggles a
  recording, BtnB cancels, and the frames carry the voice-command flag `0x04`
  instead of the clip flag. The receiver must keep writing these as
  `stickS3_*_command.wav`, and the transcriber must route them through the
  voice-action matcher without ever pasting unmatched text. Do not reuse the
  clip flag for commands.
- Air Mouse mode derives pointer velocity from gyro yaw and pitch. Keep the
  deadzone and exponential smoothing, keep fractional pixel accumulation so
  slow motion still moves the cursor, and keep the fast-flick boost plus
  pixels-per-degree scale table so a wrist turn can cross a large desktop. Do
  not run approval-menu gestures in this mode. The seven-level motion
  sensitivity is stored in NVS as `motion`; the joystick up/down axis adjusts it
  live, and BtnA/BtnB toggle the independent gyro X/Y flips stored as `mflipX`
  and `mflipY`.
- Magnifier mode uses the standard Windows Magnifier shortcuts: Win+keypad
  plus/minus to zoom, Ctrl+Alt+arrows to pan, Win+Esc to close, and Ctrl+Alt+M
  to cycle views. Gyro panning must reuse the Air Mouse motion filter, X/Y
  flips, and `motion` level so both modes feel the same. Keep the HID report's
  GUI/Control/Alt modifiers and the keypad plus/minus usages intact; do not
  replace this with a companion-only overlay unless the mode is redesigned.
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
4. In Voice Action mode, record "magnifier" and confirm the companion runs the
   configured action instead of pasting text. Confirm an unknown command is
   reported and not pasted.
5. In Air Mouse mode, confirm slow and fast tilts move the pointer smoothly,
   stick up/down changes the level, A/B flip X/Y, and a normal wrist turn can
   reach every edge of the desktop. Confirm the level and flips persist after a
   restart.
6. In Magnifier mode, confirm zoom, pan, view cycling, and open/close use the
   stock Windows Magnifier shortcuts, and that gyro panning follows the Air
   Mouse level and X/Y flips.
7. Disconnect the companion during a mic operation; confirm the operation
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
The controller initializes the onboard microphone and keeps the speaker
amplifier disabled because it never plays audio; this avoids the shared-codec
idle noise documented for the StickS3. The custom BLE microphone sender and WAV
receiver stream 16 kHz wideband IMA ADPCM in 20 ms frames. Speaker
reception/playback is not implemented, while transcription and focused-text
injection are provided by `companion/live_transcriber.py`. The ESP32-S3 does not
provide Bluetooth Classic A2DP/HFP, so any PC audio endpoint still requires
companion software. The next steps are to confirm BLE notification throughput
and audio quality on hardware, then add speaker reception as a separate mode if
desired. Wi-Fi audio can be evaluated later while keeping BLE HID active,
followed by coexistence tests with the fixed-channel ESP-NOW intercom.
