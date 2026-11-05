#!/usr/bin/env python3
"""Watch StickS3 recordings, transcribe them, and paste the text.

The script polls a folder for finished WAV files, runs each one through
NVIDIA Parakeet TDT (via onnx-asr, ONNX Runtime, CPU) and then pastes the
transcript into whatever text field currently has the caret.

Run the BLE receiver in another terminal to produce the files:

    .\\.venv\\Scripts\\python.exe companion\\ble_mic_receiver.py
    .\\.venv\\Scripts\\python.exe companion\\live_transcriber.py

Then focus the text field you want to dictate into, record a clip on the
StickS3, and the transcript appears at the caret a moment after the clip
ends.

Notes
-----
* A WAV file is only picked up once it is complete. The receiver writes the
  RIFF header sizes when it closes the file, so an in-progress recording
  reports zero frames and is skipped until the next poll.
* onnx-asr resamples the 8 kHz StickS3 audio to the 16 kHz the model wants
  and only needs numpy + onnxruntime (no PyTorch, no FFmpeg).
* The transcript is copied to the clipboard before Ctrl+V is sent, so the
  text also stays on the clipboard for a second paste.
"""

from __future__ import annotations

import argparse
import array
import queue
import sys
import threading
import time
import wave
from dataclasses import dataclass
from pathlib import Path
from typing import Optional

DEFAULT_MODEL = "nemo-parakeet-tdt-0.6b-v3"
LONG_AUDIO_SECONDS = 28.0


# ---------------------------------------------------------------------------
# Text injection
# ---------------------------------------------------------------------------

class InjectionError(RuntimeError):
    """Raised when the transcript cannot be delivered to the focused window."""


if sys.platform == "win32":
    import ctypes
    from ctypes import wintypes

    _CF_UNICODETEXT = 13
    _GMEM_MOVEABLE = 0x0002
    _INPUT_KEYBOARD = 1
    _KEYEVENTF_KEYUP = 0x0002
    _KEYEVENTF_UNICODE = 0x0004
    _VK_CONTROL = 0x11
    _VK_V = 0x56
    _VK_RETURN = 0x0D
    _VK_TAB = 0x09

    class _KeybdInput(ctypes.Structure):
        _fields_ = (
            ("wVk", wintypes.WORD),
            ("wScan", wintypes.WORD),
            ("dwFlags", wintypes.DWORD),
            ("time", wintypes.DWORD),
            ("dwExtraInfo", ctypes.POINTER(ctypes.c_ulong)),
        )

    class _MouseInput(ctypes.Structure):
        _fields_ = (
            ("dx", wintypes.LONG),
            ("dy", wintypes.LONG),
            ("mouseData", wintypes.DWORD),
            ("dwFlags", wintypes.DWORD),
            ("time", wintypes.DWORD),
            ("dwExtraInfo", ctypes.POINTER(ctypes.c_ulong)),
        )

    class _HardwareInput(ctypes.Structure):
        _fields_ = (
            ("uMsg", wintypes.DWORD),
            ("wParamL", wintypes.WORD),
            ("wParamH", wintypes.WORD),
        )

    class _InputUnion(ctypes.Union):
        _fields_ = (
            ("ki", _KeybdInput),
            ("mi", _MouseInput),
            ("hi", _HardwareInput),
        )

    class _Input(ctypes.Structure):
        _anonymous_ = ("u",)
        _fields_ = (("type", wintypes.DWORD), ("u", _InputUnion))

    class _WindowsInjector:
        """Clipboard + Ctrl+V and direct Unicode typing through SendInput."""

        def __init__(self) -> None:
            self.user32 = ctypes.WinDLL("user32", use_last_error=True)
            self.kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)

            self.user32.OpenClipboard.argtypes = (wintypes.HWND,)
            self.user32.OpenClipboard.restype = wintypes.BOOL
            self.user32.EmptyClipboard.argtypes = ()
            self.user32.EmptyClipboard.restype = wintypes.BOOL
            self.user32.CloseClipboard.argtypes = ()
            self.user32.CloseClipboard.restype = wintypes.BOOL
            self.user32.SetClipboardData.argtypes = (
                wintypes.UINT, wintypes.HANDLE
            )
            self.user32.SetClipboardData.restype = wintypes.HANDLE
            self.kernel32.GlobalAlloc.argtypes = (
                wintypes.UINT, ctypes.c_size_t
            )
            self.kernel32.GlobalAlloc.restype = wintypes.HGLOBAL
            self.kernel32.GlobalLock.argtypes = (wintypes.HGLOBAL,)
            self.kernel32.GlobalLock.restype = wintypes.LPVOID
            self.kernel32.GlobalUnlock.argtypes = (wintypes.HGLOBAL,)
            self.kernel32.GlobalUnlock.restype = wintypes.BOOL
            self.kernel32.GlobalFree.argtypes = (wintypes.HGLOBAL,)
            self.kernel32.GlobalFree.restype = wintypes.HGLOBAL
            self.user32.SendInput.argtypes = (
                wintypes.UINT, ctypes.POINTER(_Input), ctypes.c_int
            )
            self.user32.SendInput.restype = wintypes.UINT

        def _send(self, *inputs: _Input) -> None:
            if not inputs:
                return
            array_type = _Input * len(inputs)
            sent = self.user32.SendInput(
                len(inputs), array_type(*inputs), ctypes.sizeof(_Input)
            )
            if sent != len(inputs):
                raise InjectionError(
                    "SendInput was blocked; Windows only lets an elevated "
                    "process send keys to another elevated process."
                )

        def _key(self, vk: int, *, keyup: bool = False) -> _Input:
            flags = _KEYEVENTF_KEYUP if keyup else 0
            return _Input(
                type=_INPUT_KEYBOARD,
                u=_InputUnion(
                    ki=_KeybdInput(
                        wVk=vk,
                        wScan=0,
                        dwFlags=flags,
                        time=0,
                        dwExtraInfo=None,
                    )
                ),
            )

        def _char(self, char: str) -> list[_Input]:
            if char == "\n":
                return [
                    self._key(_VK_RETURN),
                    self._key(_VK_RETURN, keyup=True),
                ]
            if char == "\t":
                return [self._key(_VK_TAB), self._key(_VK_TAB, keyup=True)]
            scan = ord(char) & 0xFFFF
            return [
                _Input(
                    type=_INPUT_KEYBOARD,
                    u=_InputUnion(
                        ki=_KeybdInput(
                            wVk=0,
                            wScan=scan,
                            dwFlags=_KEYEVENTF_UNICODE,
                            time=0,
                            dwExtraInfo=None,
                        )
                    ),
                ),
                _Input(
                    type=_INPUT_KEYBOARD,
                    u=_InputUnion(
                        ki=_KeybdInput(
                            wVk=0,
                            wScan=scan,
                            dwFlags=_KEYEVENTF_UNICODE | _KEYEVENTF_KEYUP,
                            time=0,
                            dwExtraInfo=None,
                        )
                    ),
                ),
            ]

        def _set_clipboard(self, text: str) -> None:
            data = text.encode("utf-16-le") + b"\x00\x00"
            for attempt in range(10):
                if self.user32.OpenClipboard(None):
                    break
                time.sleep(0.02)
            else:
                raise InjectionError(
                    "Could not open the clipboard; another application is "
                    "holding it."
                )
            try:
                if not self.user32.EmptyClipboard():
                    raise InjectionError("Could not clear the clipboard.")
                handle = self.kernel32.GlobalAlloc(_GMEM_MOVEABLE, len(data))
                if not handle:
                    raise InjectionError(
                        "Could not allocate clipboard memory."
                    )
                pointer = self.kernel32.GlobalLock(handle)
                if not pointer:
                    self.kernel32.GlobalFree(handle)
                    raise InjectionError("Could not lock clipboard memory.")
                try:
                    ctypes.memmove(pointer, data, len(data))
                finally:
                    self.kernel32.GlobalUnlock(handle)
                if not self.user32.SetClipboardData(_CF_UNICODETEXT, handle):
                    self.kernel32.GlobalFree(handle)
                    raise InjectionError("Could not fill the clipboard.")
            finally:
                self.user32.CloseClipboard()

        def paste(self, text: str) -> None:
            self._set_clipboard(text)
            time.sleep(0.05)
            self._send(
                self._key(_VK_CONTROL),
                self._key(_VK_V),
                self._key(_VK_V, keyup=True),
                self._key(_VK_CONTROL, keyup=True),
            )

        def type_text(self, text: str) -> None:
            batch: list[_Input] = []
            for char in text:
                batch.extend(self._char(char))
                if len(batch) >= 64:
                    self._send(*batch)
                    batch.clear()
                    time.sleep(0.005)
            if batch:
                self._send(*batch)

    def _make_injector() -> "_WindowsInjector":
        return _WindowsInjector()

elif sys.platform == "darwin":
    import subprocess

    class _MacInjector:
        def paste(self, text: str) -> None:
            subprocess.run(["pbcopy"], input=text.encode("utf-8"), check=True)
            time.sleep(0.05)
            subprocess.run(
                [
                    "osascript",
                    "-e",
                    'tell application "System Events" to keystroke "v" '
                    "using command down",
                ],
                check=True,
            )

        def type_text(self, text: str) -> None:
            escaped = text.replace("\\", "\\\\").replace('"', '\\"')
            subprocess.run(
                [
                    "osascript",
                    "-e",
                    'tell application "System Events" to keystroke "%s"'
                    % escaped,
                ],
                check=True,
            )

    def _make_injector() -> "_MacInjector":
        return _MacInjector()

else:
    import shutil
    import subprocess

    class _LinuxInjector:
        def _copy(self, text: str) -> None:
            for command in (
                ["wl-copy"],
                ["xclip", "-selection", "clipboard"],
                ["xsel", "--clipboard", "--input"],
            ):
                if shutil.which(command[0]):
                    subprocess.run(
                        command, input=text.encode("utf-8"), check=True
                    )
                    return
            raise InjectionError(
                "No clipboard tool found; install wl-clipboard or xclip."
            )

        def _send_key(self, combo: str) -> None:
            if shutil.which("ydotool"):
                subprocess.run(["ydotool", "key", combo], check=True)
            elif shutil.which("xdotool"):
                subprocess.run(["xdotool", "key", combo], check=True)
            else:
                raise InjectionError(
                    "No key injection tool found; install ydotool or xdotool."
                )

        def paste(self, text: str) -> None:
            self._copy(text)
            time.sleep(0.05)
            self._send_key("ctrl+v")

        def type_text(self, text: str) -> None:
            if shutil.which("ydotool"):
                subprocess.run(["ydotool", "type", "--", text], check=True)
            elif shutil.which("xdotool"):
                subprocess.run(
                    ["xdotool", "type", "--clearmodifiers", "--", text],
                    check=True,
                )
            else:
                raise InjectionError(
                    "No key injection tool found; install ydotool or xdotool."
                )

    def _make_injector() -> "_LinuxInjector":
        return _LinuxInjector()


# ---------------------------------------------------------------------------
# WAV inspection
# ---------------------------------------------------------------------------

@dataclass
class ClipInfo:
    duration: float
    peak: Optional[int]
    channels: int
    sample_rate: int


class NotReadyError(RuntimeError):
    """The file exists but is not a complete WAV yet."""


def inspect_wav(path: Path) -> ClipInfo:
    """Return duration and peak level, or raise if the file is incomplete.

    ``ble_mic_receiver.py`` (like Python's ``wave`` module in general) writes
    the real chunk sizes only when it closes the file, so a WAV that is still
    being recorded reports zero frames.
    """
    try:
        with wave.open(str(path), "rb") as wav:
            channels = max(wav.getnchannels(), 1)
            width = max(wav.getsampwidth(), 1)
            sample_rate = max(wav.getframerate(), 1)
            declared = wav.getnframes()
            if declared <= 0:
                raise NotReadyError("WAV header not finalized")
            data = wav.readframes(declared)
    except NotReadyError:
        raise
    except (wave.Error, EOFError) as error:
        raise NotReadyError(str(error)) from error

    frames = len(data) // (width * channels)
    if frames <= 0:
        raise NotReadyError("no audio frames yet")

    peak: Optional[int] = None
    if width == 2:
        samples = array.array("h")
        samples.frombytes(data[: frames * channels * 2])
        if sys.byteorder == "big":
            samples.byteswap()
        peak = max(max(samples), -min(samples))
    return ClipInfo(
        duration=frames / sample_rate,
        peak=peak,
        channels=channels,
        sample_rate=sample_rate,
    )


# ---------------------------------------------------------------------------
# Transcription
# ---------------------------------------------------------------------------

class Transcriber:
    def __init__(
        self,
        model_name: str,
        quantization: Optional[str],
        device: str,
        model_path: Optional[Path],
    ) -> None:
        import onnx_asr
        import onnxruntime

        self._onnx_asr = onnx_asr
        providers = self._providers(
            device, onnxruntime.get_available_providers()
        )
        label = model_name if model_path is None else str(model_path)
        print(f"Loading {label} (quantization={quantization or 'fp32'}, "
              f"providers={', '.join(providers)})...", flush=True)
        started = time.perf_counter()
        self.model = onnx_asr.load_model(
            model_name,
            model_path,
            quantization=quantization,
            providers=providers,
        )
        print(f"Model ready in {time.perf_counter() - started:.1f}s.",
              flush=True)

        self._vad = None
        self._warm_up()

    @staticmethod
    def _providers(device: str, available: list[str]) -> list[str]:
        if device == "cpu":
            return ["CPUExecutionProvider"]
        if device == "cuda":
            return ["CUDAExecutionProvider", "CPUExecutionProvider"]
        if device == "directml":
            return ["DmlExecutionProvider", "CPUExecutionProvider"]
        for provider in (
            "CUDAExecutionProvider",
            "DmlExecutionProvider",
            "CoreMLExecutionProvider",
        ):
            if provider in available:
                return [provider, "CPUExecutionProvider"]
        return ["CPUExecutionProvider"]

    def _warm_up(self) -> None:
        import numpy

        started = time.perf_counter()
        try:
            self.model.recognize(
                numpy.zeros(16000, dtype=numpy.float32), sample_rate=16000
            )
        except Exception as error:  # noqa: BLE001 - warm-up is best effort
            print(f"Warm-up skipped: {error}", flush=True)
            return
        elapsed = time.perf_counter() - started
        print(f"Warm-up took {elapsed:.2f}s.", flush=True)

    def transcribe(self, path: Path, duration: float) -> str:
        if duration > LONG_AUDIO_SECONDS:
            if self._vad is None:
                self._vad = self._onnx_asr.load_vad("silero")
            parts = [
                segment.text.strip()
                for segment in self.model.with_vad(self._vad).recognize(path)
            ]
            return " ".join(part for part in parts if part)
        return self.model.recognize(path).strip()


# ---------------------------------------------------------------------------
# Folder watching
# ---------------------------------------------------------------------------

@dataclass
class PendingFile:
    signature: tuple[int, int]
    changed_at: float
    attempts: int = 0


@dataclass
class Job:
    path: Path
    info: ClipInfo


class Watcher:
    def __init__(
        self,
        folder: Path,
        settle: float,
        poll: float,
        include_existing: bool,
        pattern: str,
    ) -> None:
        self.folder = folder
        self.settle = settle
        self.poll = poll
        self.include_existing = include_existing
        self.pattern = pattern
        self.pending: dict[str, PendingFile] = {}
        self.done: dict[str, tuple[int, int]] = {}
        self._stopped = threading.Event()

    def _signatures(self) -> dict[str, tuple[int, int]]:
        signatures: dict[str, tuple[int, int]] = {}
        try:
            entries = list(self.folder.iterdir())
        except OSError as error:
            print(f"Cannot read {self.folder}: {error}")
            return signatures
        for entry in entries:
            try:
                if not entry.is_file():
                    continue
            except OSError:
                continue
            if entry.suffix.lower() != ".wav":
                continue
            if self.pattern and self.pattern.lower() not in entry.name.lower():
                continue
            try:
                stat = entry.stat()
            except OSError:
                continue
            signatures[entry.name] = (stat.st_mtime_ns, stat.st_size)
        return signatures

    def prime(self) -> None:
        self.folder.mkdir(parents=True, exist_ok=True)
        existing = self._signatures()
        if self.include_existing:
            print(f"Watching {self.folder} "
                  f"(including {len(existing)} existing file(s)).")
            return
        self.done = dict(existing)
        print(f"Watching {self.folder} for new recordings "
              f"({len(existing)} existing file(s) ignored).")

    def stop(self) -> None:
        self._stopped.set()

    def poll_once(self) -> list[Job]:
        now = time.monotonic()
        signatures = self._signatures()

        for name, signature in signatures.items():
            if self.done.get(name) == signature:
                continue
            pending = self.pending.get(name)
            if pending is None or pending.signature != signature:
                self.pending[name] = PendingFile(signature, now)

        for name in list(self.pending):
            if name not in signatures:
                del self.pending[name]

        jobs: list[Job] = []
        for name, pending in list(self.pending.items()):
            if now - pending.changed_at < self.settle:
                continue
            path = self.folder / name
            try:
                info = inspect_wav(path)
            except NotReadyError:
                pending.attempts += 1
                pending.changed_at = now
                if pending.attempts > 40:
                    print(f"Giving up on incomplete file {name}.")
                    self.done[name] = pending.signature
                    del self.pending[name]
                continue
            except OSError as error:
                print(f"Cannot read {name}: {error}")
                self.done[name] = pending.signature
                del self.pending[name]
                continue
            self.done[name] = pending.signature
            del self.pending[name]
            jobs.append(Job(path, info))
        return jobs

    def run(self, handle_job) -> None:
        while not self._stopped.is_set():
            for job in self.poll_once():
                handle_job(job)
            self._stopped.wait(self.poll)


# ---------------------------------------------------------------------------
# Main loop
# ---------------------------------------------------------------------------

def format_duration(seconds: float) -> str:
    return f"{seconds:.1f}s"


def main() -> None:
    parser = argparse.ArgumentParser(
        description=(
            "Transcribe new StickS3 recordings with NVIDIA Parakeet and paste "
            "the text into the focused input field."
        )
    )
    parser.add_argument(
        "--watch",
        type=Path,
        default=Path("recordings"),
        help="folder to watch (default: ./recordings)",
    )
    parser.add_argument(
        "--model",
        default=DEFAULT_MODEL,
        help=(f"onnx-asr model name (default: {DEFAULT_MODEL}); "
              "nemo-parakeet-tdt-0.6b-v2 is English-only"),
    )
    parser.add_argument(
        "--model-path",
        type=Path,
        help="local model directory instead of the Hugging Face cache",
    )
    parser.add_argument(
        "--quantization",
        choices=("int8", "fp32"),
        default="int8",
        help="model precision (default: int8, fastest)",
    )
    parser.add_argument(
        "--device",
        choices=("auto", "cpu", "cuda", "directml"),
        default="auto",
        help="execution provider to prefer (default: auto)",
    )
    parser.add_argument(
        "--method",
        choices=("paste", "type", "none"),
        default="paste",
        help=("paste uses the clipboard and Ctrl+V, type sends keystrokes; "
              "none only prints (default: paste)"),
    )
    parser.add_argument(
        "--settle",
        type=float,
        default=0.3,
        help=("seconds a file must stop changing before it is read "
              "(default: 0.3)"),
    )
    parser.add_argument(
        "--poll",
        type=float,
        default=0.2,
        help="folder polling interval in seconds (default: 0.2)",
    )
    parser.add_argument(
        "--min-duration",
        type=float,
        default=0.25,
        help="skip clips shorter than this (default: 0.25s)",
    )
    parser.add_argument(
        "--min-peak",
        type=int,
        default=150,
        help="skip clips quieter than this 16-bit peak (default: 150)",
    )
    parser.add_argument(
        "--pattern",
        default="stickS3",
        help="only watch files containing this text (default: stickS3)",
    )
    parser.add_argument(
        "--include-existing",
        action="store_true",
        help="also transcribe files already in the folder at startup",
    )
    parser.add_argument(
        "--trailing-space",
        action=argparse.BooleanOptionalAction,
        default=True,
        help="append a space after pasted text (default: true)",
    )
    parser.add_argument(
        "--save-text",
        action="store_true",
        help="write a .txt file next to each recording",
    )
    parser.add_argument(
        "--beep",
        action="store_true",
        help="beep after pasting",
    )
    args = parser.parse_args()

    try:
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    except (AttributeError, OSError):
        pass

    if not args.watch.is_dir():
        try:
            args.watch.mkdir(parents=True, exist_ok=True)
        except OSError as error:
            parser.error(
                f"cannot use {args.watch} as the watch folder: {error}"
            )

    try:
        transcriber = Transcriber(
            args.model,
            None if args.quantization == "fp32" else args.quantization,
            args.device,
            args.model_path,
        )
    except ImportError:
        print(
            "onnx-asr is not installed. Run:\n"
            "  .\\.venv\\Scripts\\python.exe -m pip install -r "
            "companion\\requirements.txt",
            file=sys.stderr,
        )
        raise SystemExit(1)
    except Exception as error:  # noqa: BLE001 - model download/load failure
        print(f"Could not load the model: {error}", file=sys.stderr)
        raise SystemExit(1)

    injector = None
    if args.method != "none":
        try:
            injector = _make_injector()
        except Exception as error:  # noqa: BLE001 - unsupported platform
            print(f"Text injection unavailable ({error}); printing only.",
                  file=sys.stderr)

    watcher = Watcher(
        folder=args.watch,
        settle=args.settle,
        poll=args.poll,
        include_existing=args.include_existing,
        pattern=args.pattern,
    )
    watcher.prime()

    jobs: queue.Queue[Optional[Job]] = queue.Queue()
    stop = threading.Event()

    def worker() -> None:
        while True:
            job = jobs.get()
            if job is None:
                return
            if stop.is_set():
                continue
            process_job(job, transcriber, injector, args)

    thread = threading.Thread(target=worker, name="transcriber", daemon=True)
    thread.start()

    print(
        "Ready. Focus the text field you want to dictate into, then record "
        "on the StickS3."
    )
    if args.method == "paste":
        print("The transcript is pasted at the caret with Ctrl+V.")
    try:
        watcher.run(jobs.put)
    except KeyboardInterrupt:
        print("\nStopping...")
    finally:
        stop.set()
        watcher.stop()
        jobs.put(None)
        thread.join(timeout=5)


def process_job(job: Job, transcriber: Transcriber, injector, args) -> None:
    name = job.path.name
    if job.info.duration < args.min_duration:
        print(f"[{time.strftime('%H:%M:%S')}] {name}: skipped "
              f"({format_duration(job.info.duration)}, too short)")
        return
    if job.info.peak is not None and job.info.peak < args.min_peak:
        print(f"[{time.strftime('%H:%M:%S')}] {name}: skipped (silent)")
        return

    started = time.perf_counter()
    try:
        text = transcriber.transcribe(job.path, job.info.duration)
    except Exception as error:  # noqa: BLE001 - report and keep watching
        print(f"[{time.strftime('%H:%M:%S')}] {name}: transcription failed: "
              f"{error}", file=sys.stderr)
        return
    elapsed = time.perf_counter() - started
    speed = job.info.duration / elapsed if elapsed > 0 else 0.0

    if not text:
        print(f"[{time.strftime('%H:%M:%S')}] {name}: no speech detected "
              f"({elapsed:.2f}s)")
        return

    line = (
        f"[{time.strftime('%H:%M:%S')}] {name} "
        f"({format_duration(job.info.duration)}, {elapsed:.2f}s, "
        f"{speed:.0f}x real time)"
    )
    print(f"{line}\n  {text}")

    if args.save_text:
        try:
            job.path.with_suffix(".txt").write_text(
                text + "\n", encoding="utf-8"
            )
        except OSError as error:
            print(f"  could not save text: {error}")

    if injector is None:
        return

    payload = text + (" " if args.trailing_space else "")
    try:
        if args.method == "type":
            injector.type_text(payload)
        else:
            injector.paste(payload)
    except Exception as error:  # noqa: BLE001 - clipboard/keystroke failure
        print(f"  could not send the text: {error}", file=sys.stderr)
        return
    print(f"  -> {'typed' if args.method == 'type' else 'pasted'} "
          f"into the focused field")
    if args.beep:
        print("\a", end="", flush=True)


if __name__ == "__main__":
    main()
