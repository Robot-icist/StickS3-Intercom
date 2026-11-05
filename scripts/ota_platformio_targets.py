"""Register authenticated OTA deploy actions in PlatformIO's project tasks."""

import subprocess
import os
from pathlib import Path


def _companion_python():
    configured = os.environ.get("COMPANION_PYTHON")
    if configured:
        interpreter = Path(configured).expanduser().resolve()
        if interpreter.is_file():
            return interpreter
        raise RuntimeError(f"COMPANION_PYTHON does not exist: {interpreter}")

    relative = (
        Path(".venv") / "Scripts" / "python.exe"
        if os.name == "nt"
        else Path(".venv") / "bin" / "python"
    )
    interpreter = ROOT / relative
    if interpreter.is_file():
        return interpreter
    raise RuntimeError(
        "Companion virtual environment is missing. Create .venv and install "
        "companion/requirements.txt, or set COMPANION_PYTHON."
    )


def _deploy_action(component=None, environment=None):
    def run(source, target, env):
        command = [str(_companion_python()), "-u", str(DEPLOYER)]
        if component is not None:
            command.extend(("--component", component))
        target_environment = environment or (
            PIO_ENVIRONMENT if component is not None else None
        )
        if target_environment is not None:
            command.extend(("--environment", target_environment))
        task_description = target_environment or "interactive environment"
        print(
            "[OTA] Starting authenticated OTA deployer for "
            f"{task_description}...",
            flush=True,
        )
        result = subprocess.call(command, cwd=str(ROOT))
        if result:
            print(f"[OTA] Deployment failed with exit code {result}.", flush=True)
        else:
            print("[OTA] Deployment task finished successfully.", flush=True)
        return result

    return run


Import("env")

PROJECT_DIR = Path(env.subst("$PROJECT_DIR")).resolve()
ROOT = (
    PROJECT_DIR
    if (PROJECT_DIR / "companion" / "ota_deploy.py").is_file()
    else PROJECT_DIR.parent / "StickS3-Intercom"
)
DEPLOYER = ROOT / "companion" / "ota_deploy.py"
PIO_ENVIRONMENT = env.subst("$PIOENV")

env.AddCustomTarget(
    "ota_deploy",
    None,
    _deploy_action("firmware", "sticks3_ble_hid"),
    title="OTA: BLE HID firmware (default)",
    description="Build and upload StickS3 BLE HID firmware over authenticated OTA.",
)
env.AddCustomTarget(
    "ota_choose",
    None,
    _deploy_action(),
    title="OTA: choose environment and image",
    description="Interactively choose a StickS3 environment and image to upload.",
)
env.AddCustomTarget(
    "ota_firmware",
    None,
    _deploy_action("firmware"),
    title="OTA: firmware",
    description="Build and upload this PlatformIO environment's firmware.",
)
env.AddCustomTarget(
    "ota_littlefs",
    None,
    _deploy_action("filesystem"),
    title="OTA: LittleFS",
    description="Build and upload this PlatformIO environment's LittleFS image.",
)
env.AddCustomTarget(
    "ota_firmware_littlefs",
    None,
    _deploy_action("both"),
    title="OTA: firmware and LittleFS",
    description="Build and upload both images for this PlatformIO environment.",
)
