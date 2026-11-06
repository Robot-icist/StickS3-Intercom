"""Build and authenticate firmware and LittleFS OTA uploads for M5Stack devices."""

from __future__ import annotations

import argparse
import asyncio
import os
import shutil
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path

from interactive_prompts import prompt_choice
from ota_ble import OtaDevice, OtaDiscoveryError, discover_ota_devices


ROOT = Path(__file__).resolve().parents[1]


def _kill_companion_processes() -> None:
    """Kill any running StickS3Companion processes (exe or python launcher)."""
    killed = False
    if os.name == "nt":
        # Kill the packaged executable (try multiple possible names)
        for proc_name in ["StickS3Companion.exe", "StickS3Companion", "stickS3Companion.exe"]:
            try:
                result = subprocess.run(
                    ["taskkill", "/F", "/IM", proc_name],
                    capture_output=True,
                    text=True,
                )
                if result.returncode == 0:
                    print(f"[OTA] Killed existing {proc_name} process(es).", flush=True)
                    killed = True
            except OSError:
                pass

        # Kill Python processes running launcher.py or live_transcriber.py using wmic
        # This matches the PowerShell approach in build_windows.ps1
        try:
            result = subprocess.run(
                ["wmic", "process", "where", "name='python.exe' AND (commandline like '%launcher.py%' OR commandline like '%live_transcriber.py%')", "get", "ProcessId"],
                capture_output=True,
                text=True,
                timeout=10,
            )
            if result.returncode == 0 and result.stdout.strip():
                lines = result.stdout.strip().split('\n')
                for line in lines[1:]:  # Skip header
                    pid = line.strip()
                    if pid and pid.isdigit():
                        subprocess.run(["taskkill", "/F", "/PID", pid], capture_output=True)
                print("[OTA] Killed existing companion Python processes via wmic.", flush=True)
                killed = True
        except (OSError, subprocess.TimeoutExpired):
            pass

        # Fallback: use PowerShell to enumerate and kill (most reliable)
        try:
            ps_script = (
                "Get-Process -Name 'StickS3Companion' -ErrorAction SilentlyContinue | Stop-Process -Force; "
                "Get-WmiObject Win32_Process -Filter \"Name = 'python.exe' AND (CommandLine LIKE '%launcher.py%' OR CommandLine LIKE '%live_transcriber.py%')\" "
                "-ErrorAction SilentlyContinue | ForEach-Object { Stop-Process -Id $_.ProcessId -Force }"
            )
            result = subprocess.run(
                ["powershell.exe", "-NoProfile", "-Command", ps_script],
                capture_output=True,
                text=True,
                timeout=10,
            )
            if result.returncode == 0:
                print("[OTA] Killed companion processes via PowerShell.", flush=True)
                killed = True
        except (OSError, subprocess.TimeoutExpired):
            pass
    else:
        # On Linux/macOS, use pkill
        for proc_name in ["StickS3Companion"]:
            try:
                result = subprocess.run(
                    ["pkill", "-f", proc_name],
                    capture_output=True,
                    text=True,
                )
                if result.returncode == 0:
                    print(f"[OTA] Killed existing {proc_name} process(es).", flush=True)
                    killed = True
            except OSError:
                pass
        # Kill python scripts
        for script in ["launcher.py", "live_transcriber.py"]:
            try:
                result = subprocess.run(
                    ["pkill", "-f", script],
                    capture_output=True,
                    text=True,
                )
                if result.returncode == 0:
                    print(f"[OTA] Killed existing companion {script} processes.", flush=True)
                    killed = True
            except OSError:
                pass

    # Also try psutil as a fallback
    try:
        result = subprocess.run(
            [sys.executable, "-c", "import psutil; [p.kill() for p in psutil.process_iter() if 'launcher.py' in ' '.join(p.cmdline()) or 'live_transcriber.py' in ' '.join(p.cmdline())]"],
            capture_output=True,
            text=True,
            timeout=5,
        )
        if result.returncode == 0:
            print("[OTA] Killed existing companion Python processes via psutil.", flush=True)
            killed = True
    except (OSError, subprocess.TimeoutExpired, ImportError):
        pass

    if not killed:
        print("[OTA] No existing companion processes found.", flush=True)


@dataclass(frozen=True)
class Target:
    name: str
    project: Path
    environment: str
    family: str


TARGETS = (
    Target("StickS3 intercom", ROOT, "sticks3_intercom", "sticks3"),
    Target("StickS3 BLE HID", ROOT, "sticks3_ble_hid", "sticks3"),
)


def _choose_target(environment: str | None = None) -> Target | None:
    if environment is not None:
        for target in TARGETS:
            if target.environment == environment:
                return target
        raise ValueError(f"Unknown PlatformIO environment: {environment}")

    labels = tuple(f"{target.name} ({target.environment})" for target in TARGETS)
    selected = prompt_choice(
        "Choose firmware environment",
        "Select which firmware this device should run.",
        labels,
        default=1,
        voice_aliases=tuple(target.name for target in TARGETS),
    )
    return TARGETS[selected] if selected is not None else None


def _find_pio() -> str:
    configured = os.environ.get("PIO_EXECUTABLE")
    if configured:
        if Path(configured).is_file():
            return configured
        raise RuntimeError(f"PIO_EXECUTABLE does not exist: {configured}")

    executable = shutil.which("pio")
    if executable:
        return executable
    candidates = (
        Path.home() / ".platformio" / "penv" / "Scripts" / "pio.exe",
        Path.home() / ".platformio" / "penv" / "bin" / "pio",
    )
    for candidate in candidates:
        if candidate.is_file():
            return str(candidate)
    raise RuntimeError(
        "PlatformIO CLI was not found. Set PIO_EXECUTABLE to its full path."
    )


def _find_espota() -> Path:
    core = Path(
        os.environ.get("PLATFORMIO_CORE_DIR", Path.home() / ".platformio")
    )
    uploader = (
        core
        / "packages"
        / "framework-arduinoespressif32"
        / "tools"
        / "espota.py"
    )
    if uploader.is_file():
        return uploader
    raise RuntimeError(f"ESP32 OTA uploader was not found at {uploader}")


def _run(
    command: list[str],
    cwd: Path,
    display_command: list[str] | None = None,
) -> None:
    print("\n> " + subprocess.list2cmdline(display_command or command), flush=True)
    result = subprocess.run(command, cwd=cwd, check=False)
    if result.returncode:
        raise RuntimeError(
            f"Command failed with exit code {result.returncode}: "
            f"{subprocess.list2cmdline(display_command or command)}"
        )


def _build(target: Target, component: str, pio: str) -> Path:
    print(
        f"\nBuilding {component} for {target.name} ({target.environment})...",
        flush=True,
    )
    command = [pio, "run", "-e", target.environment]
    if component == "filesystem":
        command.extend(("-t", "buildfs"))
        image_name = "littlefs.bin"
    else:
        image_name = "firmware.bin"
    _run(command, target.project)
    image = target.project / ".pio" / "build" / target.environment / image_name
    if not image.is_file() or image.stat().st_size == 0:
        raise RuntimeError(f"Build did not create a non-empty image: {image}")
    print(f"Built image: {image}", flush=True)
    return image


def _upload(
    image: Path,
    device: OtaDevice,
    uploader: Path,
    filesystem: bool,
    cwd: Path,
) -> None:
    component = "LittleFS" if filesystem else "firmware"
    print(
        f"\nUploading {component} image to {device.name} at {device.host}...",
        flush=True,
    )
    command = [
        sys.executable,
        "-u",
        str(uploader),
        "--progress",
        "-i",
        device.host,
        "--auth",
        device.password,
        "-f",
        str(image),
    ]
    if filesystem:
        command.append("--spiffs")
    safe_command = command.copy()
    password_index = safe_command.index("--auth") + 1
    safe_command[password_index] = "********"
    _run(command, cwd, safe_command)
    print(f"{component.capitalize()} upload completed.", flush=True)


async def _discover_with_retries(
    family: str, retry_count: int
) -> list[OtaDevice]:
    for attempt in range(retry_count):
        try:
            # Same timeout logic as firmware target - no per-attempt kills
            return await asyncio.wait_for(discover_ota_devices(family), timeout=6.0)
        except asyncio.TimeoutError:
            if attempt + 1 == retry_count:
                raise OtaDiscoveryError("BLE device discovery timed out after 6s")
            print(
                f"BLE OTA access attempt {attempt + 1}/{retry_count} timed out, retrying...",
                flush=True,
            )
            await asyncio.sleep(1)
        except OtaDiscoveryError as error:
            if attempt + 1 == retry_count:
                raise
            print(
                f"BLE OTA access attempt {attempt + 1}/{retry_count} failed: "
                f"{error}\nRetrying device discovery...",
                flush=True,
            )
            await asyncio.sleep(1)
    raise RuntimeError("BLE device discovery ended without a result.")


def _choose_device(target: Target, retry_count: int = 8) -> OtaDevice | None:
    print(
        f"Searching for {target.name} and requesting temporary OTA access "
        "over BLE...",
        flush=True,
    )
    devices = asyncio.run(_discover_with_retries(target.family, retry_count))
    if len(devices) == 1:
        device = devices[0]
        print(f"Found {device.name} at {device.host} over BLE.", flush=True)
        return device

    choices = tuple(device.label for device in devices)
    selected = prompt_choice(
        "Choose device",
        "Select the compatible device found over Bluetooth.",
        choices,
        voice_aliases=tuple(device.name for device in devices),
    )
    return devices[selected] if selected is not None else None


def _parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Build and upload a selected M5Stack image over authenticated OTA."
    )
    parser.add_argument(
        "--environment",
        choices=tuple(target.environment for target in TARGETS),
        help="select an environment directly; otherwise show the environment popup",
    )
    parser.add_argument(
        "--component",
        choices=("firmware", "filesystem", "both"),
        help="select the image directly; otherwise show the image popup",
    )
    parser.add_argument(
        "--restart-companion",
        action="store_true",
        help="restart the StickS3Companion app after OTA update completes (tries exe first)",
    )
    parser.add_argument(
        "--restart-companion-source",
        action="store_true",
        help="restart the StickS3Companion app from source after OTA update completes",
    )
    return parser.parse_args()


def _restart_companion(prefer_source: bool = False) -> bool:
    """Restart the StickS3Companion app from source or packaged executable."""
    print("[OTA] Restarting companion app...", flush=True)
    python = Path(sys.executable)
    launcher = ROOT / "companion" / "launcher.py"
    exe_path = ROOT / "dist" / "StickS3Companion" / "StickS3Companion.exe"

    if prefer_source:
        # Try source first
        if launcher.is_file():
            try:
                subprocess.Popen([str(python), "-u", str(launcher)], cwd=str(ROOT))
                print(f"[OTA] Started companion from source: {launcher}", flush=True)
                return True
            except OSError as e:
                print(f"[OTA] Failed to start from source: {e}", flush=True)
        # Fall back to exe
        if exe_path.is_file():
            try:
                subprocess.Popen([str(exe_path)], cwd=str(ROOT))
                print(f"[OTA] Started {exe_path}", flush=True)
                return True
            except OSError as e:
                print(f"[OTA] Failed to start packaged app: {e}", flush=True)
    else:
        # Try exe first (default)
        if exe_path.is_file():
            try:
                subprocess.Popen([str(exe_path)], cwd=str(ROOT))
                print(f"[OTA] Started {exe_path}", flush=True)
                return True
            except OSError as e:
                print(f"[OTA] Failed to start packaged app: {e}", flush=True)
        # Fall back to source
        if launcher.is_file():
            try:
                subprocess.Popen([str(python), "-u", str(launcher)], cwd=str(ROOT))
                print(f"[OTA] Started companion from source: {launcher}", flush=True)
                return True
            except OSError as e:
                print(f"[OTA] Failed to start from source: {e}", flush=True)

    print("[OTA] Could not find companion app to restart.", flush=True)
    return False


def _run_interactive() -> tuple[str, tuple[str, ...]] | None:
    """Run interactive prompts and return (environment, components) or None if cancelled."""
    target = _choose_target(None)
    if target is None:
        print("OTA update cancelled.")
        return None

    image_choices = (
        "Firmware only",
        "LittleFS filesystem only",
        "Firmware and filesystem",
    )
    component_index = prompt_choice(
        "Choose update contents",
        "Select the image or images to build and upload.",
        image_choices,
        default=0,
        voice_aliases=("firmware", "littlefs", "firmware and filesystem"),
    )
    if component_index is None:
        print("OTA update cancelled.")
        return None
    components = (
        ("firmware",)
        if component_index == 0
        else ("filesystem",)
        if component_index == 1
        else ("filesystem", "firmware")
    )
    return target.environment, components


def main() -> bool:
    args = _parse_args()

    # If running interactively (no --environment/--component), run prompts then
    # re-execute with the chosen args to use the exact same working code path
    # as the non-interactive ota_deploy/ota_firmware/ota_littlefs targets.
    if args.environment is None or args.component is None:
        interactive_result = _run_interactive()
        if interactive_result is None:
            return False
        chosen_env, chosen_components = interactive_result

        # Map components tuple back to --component value
        if chosen_components == ("firmware",):
            component_arg = "firmware"
        elif chosen_components == ("filesystem",):
            component_arg = "filesystem"
        else:
            component_arg = "both"

        # Re-execute with the chosen arguments
        print(
            f"\n[OTA] Re-running with --environment {chosen_env} --component {component_arg}...",
            flush=True,
        )
        command = [
            sys.executable,
            "-u",
            str(Path(__file__).resolve()),
            "--environment",
            chosen_env,
            "--component",
            component_arg,
        ]
        if args.restart_companion_source:
            command.append("--restart-companion-source")
        elif args.restart_companion:
            command.append("--restart-companion")

        result = subprocess.run(command, cwd=ROOT)
        return result.returncode == 0

    # Non-interactive path: exact same logic as firmware target (proven working)
    target = _choose_target(args.environment)
    if target is None:
        print("OTA update cancelled.")
        return False

    components = (
        ("firmware",)
        if args.component == "firmware"
        else ("filesystem",)
        if args.component == "filesystem"
        else ("filesystem", "firmware")
    )
    if target.environment == "sticks3_ble_hid" and "filesystem" in components:
        print(
            "\nNote: BLE HID does not currently use LittleFS. Its image is "
            "built from the project data directory and has no controller "
            "runtime effect."
        )

    # On Windows, tkinter (used by prompts) may leave asyncio event loop
    # and COM apartment state that interferes with bleak/Bluetooth.
    # Force full cleanup before kill/scan.
    if os.name == "nt":
        try:
            import asyncio
            import gc
            # Force garbage collection to destroy any lingering tkinter objects
            gc.collect()
            # Reset event loop to clean state
            try:
                loop = asyncio.get_event_loop()
                if not loop.is_closed():
                    loop.close()
            except Exception:
                pass
            asyncio.set_event_loop(asyncio.new_event_loop())
        except Exception:
            pass

    # NOW kill companion (after prompts, before scan) - matches firmware target order
    _kill_companion_processes()
    import time
    time.sleep(3.0)

    pio = _find_pio()
    uploader = _find_espota()
    images = [
        (component, _build(target, component, pio))
        for component in components
    ]

    device: OtaDevice | None = None
    for index, (component, image) in enumerate(images):
        device = _choose_device(target, retry_count=10 if index else 8)
        if device is None:
            print("OTA update cancelled.")
            return False
        _upload(
            image,
            device,
            uploader,
            filesystem=component == "filesystem",
            cwd=target.project,
        )

    print(f"\nOTA update completed for {target.name} at {device.host}.")
    if args.restart_companion_source:
        _restart_companion(prefer_source=True)
    elif args.restart_companion:
        _restart_companion(prefer_source=False)
    return True


if __name__ == "__main__":
    try:
        if not main():
            raise SystemExit(2)
    except (OSError, RuntimeError, ValueError) as error:
        print(f"OTA update failed: {error}", file=sys.stderr)
        raise SystemExit(1) from error
