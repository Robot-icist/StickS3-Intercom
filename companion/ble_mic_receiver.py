import argparse
import asyncio
import json
import os
import re
import struct
import subprocess
import sys
import wave
from dataclasses import dataclass
from pathlib import Path
from typing import Optional

from bleak import BleakClient, BleakScanner, BLEDevice
from bleak.exc import BleakError


DEVICE_NAME = "StickS3 BLE Controller"
MIC_SERVICE_UUID = "e5d10001-3c9b-4f6a-9b2d-2a7f4510a001"
MIC_STREAM_UUID = "e5d10002-3c9b-4f6a-9b2d-2a7f4510a001"
MIC_COMPANION_UUID = "e5d10003-3c9b-4f6a-9b2d-2a7f4510a001"
COMPANION_HEARTBEAT_SECONDS = 1.0
SAMPLE_RATE = 8000
SAMPLES_PER_FRAME = 160
HEADER_SIZE = 6
FLAG_LIVE = 0x01
FLAG_CLIP = 0x02
FLAG_END = 0x80
RECORDING_NAME = re.compile(r"stickS3_\d+_(?:live|clip)\.wav\Z")

STEP_TABLE = (
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37,
    41, 45, 50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130, 143, 157, 173,
    190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658,
    724, 796, 876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066,
    2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358, 5894, 6484,
    7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500,
    20350, 22385, 24623, 27086, 29794, 32767,
)
INDEX_TABLE = (-1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8)


def decode_frame(encoded: bytes) -> bytes:
    if len(encoded) != 84:
        raise ValueError(f"expected 84 ADPCM bytes, got {len(encoded)}")

    predictor = struct.unpack_from("<h", encoded)[0]
    step_index = min(encoded[2], 88)
    samples = [predictor]

    for nibble_index in range((SAMPLES_PER_FRAME - 1)):
        packed = encoded[4 + nibble_index // 2]
        code = (packed >> 4) & 0x0F if nibble_index & 1 else packed & 0x0F
        step = STEP_TABLE[step_index]
        difference = step >> 3
        if code & 4:
            difference += step
        if code & 2:
            difference += step >> 1
        if code & 1:
            difference += step >> 2
        predictor += -difference if code & 8 else difference
        predictor = max(-32768, min(32767, predictor))
        step_index = max(0, min(88, step_index + INDEX_TABLE[code]))
        samples.append(predictor)

    return struct.pack("<160h", *samples)


class Receiver:
    def __init__(self, output_dir: Path) -> None:
        self.output_dir = output_dir
        self.output_dir.mkdir(parents=True, exist_ok=True)
        self.segment = 0
        self.audio_file: wave.Wave_write | None = None
        self.last_sequence: int | None = None
        self.mode: str | None = None

    def _close_segment(self) -> None:
        if self.audio_file is not None:
            self.audio_file.close()
            self.audio_file = None
            print("Audio segment saved.")
        self.last_sequence = None
        self.mode = None

    def _start_segment(self, mode: str) -> None:
        self._close_segment()
        self.segment += 1
        path = self.output_dir / f"stickS3_{self.segment:04d}_{mode}.wav"
        self.audio_file = wave.open(str(path), "wb")
        self.audio_file.setnchannels(1)
        self.audio_file.setsampwidth(2)
        self.audio_file.setframerate(SAMPLE_RATE)
        self.mode = mode
        print(f"Receiving {mode} audio -> {path}")

    def _flush(self) -> None:
        """Push buffered frames out so the transcriber can follow live audio.

        wave.Wave_write wraps a buffered file, so without this a live stream
        would sit in an 8 KB buffer and partial transcription would lag by up
        to half a second.
        """
        handle = getattr(self.audio_file, "_file", None)
        try:
            handle.flush()
        except (AttributeError, OSError):
            pass

    def on_notification(self, _characteristic: object, data: bytearray) -> None:
        if len(data) < HEADER_SIZE:
            print(f"Ignoring short packet ({len(data)} bytes).")
            return

        version, flags, sequence, frame_size = struct.unpack_from("<BBHH", data)
        if version != 1:
            print(f"Ignoring unsupported audio protocol version {version}.")
            return

        if flags & FLAG_END:
            self._close_segment()
            print("Audio transmission ended.")
            return

        mode = "live" if flags & FLAG_LIVE else "clip" if flags & FLAG_CLIP else None
        payload = bytes(data[HEADER_SIZE:])
        if mode is None or frame_size != len(payload):
            print("Ignoring malformed audio frame.")
            return

        if self.audio_file is None or self.mode != mode:
            self._start_segment(mode)
        if self.last_sequence is not None:
            expected = (self.last_sequence + 1) & 0xFFFF
            if sequence != expected:
                print(f"Audio packet gap: expected {expected}, received {sequence}.")
        self.last_sequence = sequence

        try:
            self.audio_file.writeframesraw(decode_frame(payload))
            self._flush()
        except ValueError as error:
            print(f"Ignoring invalid ADPCM frame: {error}")


@dataclass
class Candidate:
    address: str
    name: str
    ble_device: Optional[BLEDevice] = None
    source: str = "scan"
    connected: bool = False


@dataclass
class PairedDevice:
    address: str
    name: str
    connected: bool


def name_matches(candidate_name: Optional[str], wanted: str) -> bool:
    return bool(candidate_name) and wanted.casefold() in candidate_name.casefold()


def cache_path() -> Path:
    base = os.environ.get("LOCALAPPDATA") or os.environ.get("XDG_CONFIG_HOME")
    root = Path(base) if base else Path.home() / ".config"
    return root / "StickS3Intercom" / "companion.json"


def load_cached_candidate() -> Optional[Candidate]:
    path = cache_path()
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return None
    address = data.get("address")
    if not isinstance(address, str) or not address:
        return None
    name = data.get("name")
    return Candidate(
        address=address,
        name=name if isinstance(name, str) and name else DEVICE_NAME,
        source=f"cache ({path})",
    )


def save_cached_candidate(candidate: Candidate) -> None:
    path = cache_path()
    try:
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(
            json.dumps({"address": candidate.address, "name": candidate.name},
                       indent=2),
            encoding="utf-8",
        )
    except OSError as error:
        print(f"Could not save the device address cache: {error}")


def format_winrt_address(value: int) -> str:
    return ":".join(f"{byte:02X}" for byte in value.to_bytes(6, "big"))


class _WinRtRawAdvData:
    """Minimal stand-in for Bleak's WinRT advertisement details.

    A peripheral that is already connected (for example as a HID device) stops
    advertising, so Bleak's normal scan-then-connect path cannot find it.
    Passing this object as ``BLEDevice.details`` lets the WinRT backend build a
    ``BluetoothLEDevice`` straight from the known Bluetooth address instead.
    """

    def __init__(self, address: int) -> None:
        self.bluetooth_address = address
        self.adv = self
        self.scan = None


def winrt_ble_device(address: str, name: str) -> BLEDevice:
    address_int = int(address.replace(":", "").replace("-", ""), 16)
    details = _WinRtRawAdvData(address_int)
    return BLEDevice(address.upper(), name, details)


async def list_paired_ble_devices() -> list[PairedDevice]:
    """Return paired BLE devices using the Windows device enumeration API."""
    try:
        from winrt.windows.devices.bluetooth import BluetoothLEDevice
        from winrt.windows.devices.enumeration import DeviceInformation
    except ImportError:
        try:
            # Bleak 0.22.x on Python < 3.12 ships this bundled projection.
            from bleak_winrt.windows.devices.bluetooth import BluetoothLEDevice
            from bleak_winrt.windows.devices.enumeration import DeviceInformation
        except ImportError:
            return []

    try:
        selector = BluetoothLEDevice.get_device_selector_from_pairing_state(True)
        # The Python/WinRT projection expects the property list as a second
        # argument; an empty list returns only the default properties.
        infos = await DeviceInformation.find_all_async(selector, [])
    except Exception as error:  # noqa: BLE001 - report any WinRT failure
        print(f"Could not enumerate paired Bluetooth devices: {error}")
        return []

    devices: list[PairedDevice] = []
    for info in infos:
        try:
            device = await BluetoothLEDevice.from_id_async(info.id)
        except Exception:  # noqa: BLE001 - skip entries that cannot be opened
            continue
        if device is None:
            continue
        try:
            devices.append(
                PairedDevice(
                    address=format_winrt_address(device.bluetooth_address),
                    name=info.name or "",
                    connected=device.connection_status == 1,
                )
            )
        finally:
            device.close()
    devices.sort(key=lambda device: not device.connected)
    return devices


async def paired_candidates(name: str) -> list[Candidate]:
    candidates: list[Candidate] = []
    for device in await list_paired_ble_devices():
        if name_matches(device.name, name):
            candidates.append(
                Candidate(
                    address=device.address,
                    name=device.name or name,
                    source="connected" if device.connected else "paired",
                    connected=device.connected,
                )
            )
    return candidates


def deduplicate(candidates: list[Candidate]) -> list[Candidate]:
    seen: set[str] = set()
    unique: list[Candidate] = []
    for candidate in candidates:
        key = candidate.address.casefold()
        if key in seen:
            continue
        seen.add(key)
        unique.append(candidate)
    return unique


async def known_candidates(
    args: argparse.Namespace,
) -> tuple[list[Candidate], list[Candidate]]:
    """Return (connected/known, paired-but-idle) candidates."""
    connected: list[Candidate] = []
    fallback: list[Candidate] = []
    paired: list[Candidate] = []

    if sys.platform == "win32":
        print("Checking already connected/paired devices before scanning...")
        paired = await paired_candidates(args.name)

    paired_addresses = {candidate.address.casefold() for candidate in paired}
    for candidate in paired:
        if candidate.connected:
            connected.append(candidate)
        else:
            fallback.append(candidate)

    cached = load_cached_candidate()
    if cached is not None and name_matches(cached.name, args.name):
        if cached.address.casefold() not in paired_addresses:
            # Its connection state is unknown; on non-Windows this cache is the
            # only known path, so try it before scanning.
            connected.append(cached)

    return (
        sorted(deduplicate(connected), key=lambda candidate: not candidate.connected),
        deduplicate(fallback),
    )


async def scan_candidate(args: argparse.Namespace) -> Optional[Candidate]:
    print(f"Scanning for {args.name!r} ({args.scan_timeout:g}s)...")
    device = await BleakScanner.find_device_by_filter(
        lambda candidate, advertisement: name_matches(
            advertisement.local_name or candidate.name, args.name
        ),
        timeout=args.scan_timeout,
    )
    if device is None:
        return None
    return Candidate(
        address=device.address,
        name=device.name or args.name,
        ble_device=device,
        source="advertising",
    )


def create_client(
    candidate: Candidate,
    timeout: float,
    disconnected_callback=None,
) -> BleakClient:
    target: BLEDevice | str = candidate.ble_device or candidate.address
    kwargs: dict[str, object] = {}
    if sys.platform == "win32":
        # Reuse the existing Windows BLE connection, and do not rely on the
        # service cache (a firmware update may have added the mic service).
        kwargs["winrt"] = {"use_cached_services": False}
        if candidate.ble_device is None:
            target = winrt_ble_device(candidate.address, candidate.name)
    return BleakClient(
        target,
        timeout=timeout,
        disconnected_callback=disconnected_callback,
        **kwargs,
    )


def clear_existing_recordings(output_dir: Path) -> int:
    """Remove prior WAV recordings created by this receiver from output_dir."""
    output_dir.mkdir(parents=True, exist_ok=True)
    removed = 0
    for path in output_dir.iterdir():
        if path.is_file() and RECORDING_NAME.fullmatch(path.name):
            path.unlink()
            removed += 1
    print(f"Cleared {removed} existing StickS3 recording(s) from {output_dir}.")
    return removed


async def connect_to_candidates(
    candidates: list[Candidate],
    timeout: float,
    attempts: int = 2,
) -> tuple[Candidate, BleakClient, asyncio.Event]:
    failures: list[str] = []
    for candidate in candidates:
        last_error: Optional[Exception] = None
        for attempt in range(1, attempts + 1):
            suffix = "" if attempt == 1 else f" (attempt {attempt})"
            print(f"Connecting to {candidate.name} [{candidate.address}] "
                  f"via {candidate.source}{suffix}...")
            disconnected = asyncio.Event()

            def on_disconnect(_client: BleakClient,
                              event: asyncio.Event = disconnected) -> None:
                event.set()

            client: Optional[BleakClient] = None
            try:
                client = create_client(candidate, timeout, on_disconnect)
                await client.connect()
                # Windows occasionally returns a partial initial service list
                # when the link is already up for HID; a reconnect fixes it.
                if client.services.get_service(MIC_SERVICE_UUID) is None:
                    raise RuntimeError(
                        "the device does not expose the BLE microphone service"
                    )
                print(f"Connected to {candidate.address}.")
                save_cached_candidate(candidate)
                return candidate, client, disconnected
            except Exception as error:  # noqa: BLE001 - try again/next candidate
                last_error = error
                print(f"  Connection failed: {error}")
                if client is not None:
                    try:
                        await client.disconnect()
                    except Exception:  # noqa: BLE001 - best-effort cleanup
                        pass
                if attempt < attempts:
                    await asyncio.sleep(0.75)
        failures.append(f"{candidate.address}: {last_error}")

    detail = "\n  ".join(failures)
    raise RuntimeError(f"Could not connect to the StickS3.\n  {detail}")


async def receive(client: BleakClient, output_dir: Path,
                  disconnected: asyncio.Event) -> None:
    receiver = Receiver(output_dir)
    try:
        await client.start_notify(MIC_STREAM_UUID, receiver.on_notification)
        await client.write_gatt_char(
            MIC_COMPANION_UUID, b"\x01", response=False
        )
        print(
            "Companion connected and microphone ready. Use joystick click "
            "for live audio, BtnA for a clip, or Ctrl+C to quit."
        )
        while not disconnected.is_set():
            try:
                await asyncio.wait_for(
                    disconnected.wait(),
                    timeout=COMPANION_HEARTBEAT_SECONDS,
                )
            except asyncio.TimeoutError:
                await client.write_gatt_char(
                    MIC_COMPANION_UUID, b"\x01", response=False
                )
        print("StickS3 disconnected.")
    finally:
        if client.is_connected:
            try:
                await client.stop_notify(MIC_STREAM_UUID)
            except Exception:  # noqa: BLE001 - best-effort cleanup
                pass
        receiver._close_segment()
        if client.is_connected:
            try:
                await client.disconnect()
            except Exception:  # noqa: BLE001 - best-effort cleanup
                pass


async def run(args: argparse.Namespace) -> None:
    if args.list:
        cached = load_cached_candidate()
        if cached is not None:
            print(f"Cached device: {cached.address}  {cached.name}")
        if sys.platform == "win32":
            devices = await list_paired_ble_devices()
            print("Paired BLE devices:")
            for device in devices:
                state = "connected" if device.connected else "not connected"
                print(f"  {device.address}  {device.name or '(unnamed)'}  ({state})")
        else:
            print("Paired-device discovery is only implemented on Windows.")
        return

    if args.address:
        candidates = [Candidate(args.address, args.name, source="command line")]
        _, client, disconnected = await connect_to_candidates(
            candidates, args.connect_timeout
        )
        await receive(client, args.output, disconnected)
        return

    errors: list[str] = []
    connected, fallback = await known_candidates(args)
    if connected:
        try:
            _, client, disconnected = await connect_to_candidates(
                connected, args.connect_timeout
            )
        except RuntimeError as error:
            errors.append(str(error))
            print(f"{error}")
        else:
            await receive(client, args.output, disconnected)
            return

    scanned = await scan_candidate(args)
    if scanned is not None:
        try:
            _, client, disconnected = await connect_to_candidates(
                [scanned], args.connect_timeout
            )
        except RuntimeError as error:
            errors.append(str(error))
            print(f"{error}")
        else:
            await receive(client, args.output, disconnected)
            return

    # Last resort: paired devices that were not connected during discovery.
    if fallback:
        try:
            _, client, disconnected = await connect_to_candidates(
                fallback, args.connect_timeout
            )
        except RuntimeError as error:
            errors.append(str(error))
        else:
            await receive(client, args.output, disconnected)
            return

    message = (
        "StickS3 not found. If it is already connected to this PC for HID, "
        "run with --list to find its address, then pass --address AA:BB:CC:DD:EE:FF."
    )
    if errors:
        message += "\n\nAlready tried:\n" + "\n".join(errors)
    raise RuntimeError(message)


def start_transcriber(
    args: argparse.Namespace,
) -> "subprocess.Popen[bytes] | None":
    """Launch the transcriber as a child process on the same output folder.

    The transcriber is a separate process so that model inference never
    blocks the BLE event loop; it picks up both live streams and clips as
    soon as the receiver closes each WAV file.
    """
    transcriber_args = [
        "--watch",
        str(args.output),
        "--method",
        args.transcribe_method,
        "--exit-with",
        str(os.getpid()),
        *args.transcribe_arg,
    ]
    if getattr(sys, "frozen", False):
        command = [
            sys.executable,
            "--internal-transcriber",
            *transcriber_args,
        ]
    else:
        script = Path(__file__).resolve().with_name("live_transcriber.py")
        if not script.is_file():
            print(f"Transcriber not found next to the receiver: {script}",
                  file=sys.stderr)
            return None
        command = [
            sys.executable,
            "-u",
            str(script),
            *transcriber_args,
        ]
    if args.voice_commands:
        command.append("--voice-commands")
    if args.transcribe or args.voice_commands:
        print("Starting the ASR model now, before BLE connection begins.")
    print("Starting transcriber: " + " ".join(command), flush=True)
    try:
        output = sys.stdout if sys.stdout is not None else subprocess.DEVNULL
        errors = sys.stderr if sys.stderr is not None else subprocess.DEVNULL
        return subprocess.Popen(command, stdout=output, stderr=errors)
    except OSError as error:
        print(f"Could not start the transcriber: {error}", file=sys.stderr)
        return None


def stop_transcriber(process: "subprocess.Popen[bytes] | None") -> None:
    if process is None or process.poll() is not None:
        return
    process.terminate()
    try:
        process.wait(timeout=5)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait(timeout=5)


def main() -> None:
    parser = argparse.ArgumentParser(
        description=(
            "Receive StickS3 custom BLE microphone audio as WAV files. "
            "Works with an advertising StickS3 and with one already connected "
            "to the PC as a HID device."
        )
    )
    parser.add_argument(
        "--output",
        type=Path,
        default=Path("recordings"),
        help="directory for received WAV files (default: ./recordings)",
    )
    parser.add_argument(
        "--address",
        help=("skip scanning and connect to this Bluetooth address, "
              "for example AA:BB:CC:DD:EE:FF"),
    )
    parser.add_argument(
        "--name",
        default=DEVICE_NAME,
        help=f"device name to look for (default: {DEVICE_NAME!r})",
    )
    parser.add_argument(
        "--scan-timeout",
        type=float,
        default=10.0,
        help="seconds to scan for advertisements (default: 10)",
    )
    parser.add_argument(
        "--connect-timeout",
        type=float,
        default=20.0,
        help="seconds to wait for a connection (default: 20)",
    )
    parser.add_argument(
        "--list",
        action="store_true",
        help="list paired BLE devices (Windows) and exit",
    )
    parser.add_argument(
        "--transcribe",
        action="store_true",
        help=(
            "also start live transcription, which transcribes each finished "
            "live stream or clip and pastes the text into the focused input"
        ),
    )
    parser.add_argument(
        "--receiver-only",
        action="store_true",
        help="disable automatic transcription when starting the packaged app",
    )
    parser.add_argument(
        "--voice-commands",
        action="store_true",
        help=(
            "also start transcription and enable confirmed OpenCode, Claude "
            "Code, and configured action routing"
        ),
    )
    parser.add_argument(
        "--transcribe-method",
        choices=("paste", "type", "none"),
        default="paste",
        help="how the transcriber delivers text (default: paste)",
    )
    parser.add_argument(
        "--transcribe-arg",
        action="append",
        default=[],
        metavar="ARG",
        help=(
            "extra live_transcriber.py argument, repeatable; write options "
            "with an equals sign, for example --transcribe-arg=--modes "
            "--transcribe-arg=live"
        ),
    )
    args = parser.parse_args()
    if args.receiver_only and (args.transcribe or args.voice_commands):
        parser.error("--receiver-only cannot be combined with transcription options")
    if not args.list:
        clear_existing_recordings(args.output)
    transcriber = None
    if (
        (args.transcribe or args.voice_commands)
        and not args.receiver_only
        and not args.list
    ):
        transcriber = start_transcriber(args)
    try:
        asyncio.run(run(args))
    except KeyboardInterrupt:
        print("\nStopped.")
    except (RuntimeError, BleakError) as error:
        print(f"\n{error}", file=sys.stderr)
        raise SystemExit(1)
    finally:
        stop_transcriber(transcriber)


if __name__ == "__main__":
    main()
