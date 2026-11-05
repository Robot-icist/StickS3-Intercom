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
            return await discover_ota_devices(family)
        except OtaDiscoveryError as error:
            if attempt + 1 == retry_count:
                raise
            print(
                f"BLE OTA access attempt {attempt + 1}/{retry_count} failed: "
                f"{error}\nRetrying device discovery...",
                flush=True,
            )
            await asyncio.sleep(3)
    raise RuntimeError("BLE device discovery ended without a result.")


def _choose_device(target: Target, retry_count: int = 4) -> OtaDevice | None:
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
    return parser.parse_args()


def main() -> bool:
    args = _parse_args()
    target = _choose_target(args.environment)
    if target is None:
        print("OTA update cancelled.")
        return False

    if args.component is not None:
        components = (
            ("firmware",)
            if args.component == "firmware"
            else ("filesystem",)
            if args.component == "filesystem"
            else ("filesystem", "firmware")
        )
    else:
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
            return False
        components = (
            ("firmware",)
            if component_index == 0
            else ("filesystem",)
            if component_index == 1
            else ("filesystem", "firmware")
        )
    if target.environment == "sticks3_ble_hid" and "filesystem" in components:
        print(
            "\nNote: BLE HID does not currently use LittleFS. Its image is "
            "built from the project data directory and has no controller "
            "runtime effect."
        )

    pio = _find_pio()
    uploader = _find_espota()
    images = [
        (component, _build(target, component, pio))
        for component in components
    ]

    device: OtaDevice | None = None
    for index, (component, image) in enumerate(images):
        device = _choose_device(target, retry_count=12 if index else 4)
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
    return True


if __name__ == "__main__":
    try:
        if not main():
            raise SystemExit(2)
    except (OSError, RuntimeError, ValueError) as error:
        print(f"OTA update failed: {error}", file=sys.stderr)
        raise SystemExit(1) from error
