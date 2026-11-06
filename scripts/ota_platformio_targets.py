"""Register companion and authenticated OTA actions in PlatformIO tasks."""

import subprocess
import os
import sys
from pathlib import Path


def _kill_companion_processes():
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
                    print(f"[Companion] Killed existing {proc_name} process(es).", flush=True)
                    killed = True
            except OSError:
                pass

        # Kill Python processes running launcher.py or live_transcriber.py using wmic
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
                print("[Companion] Killed existing companion Python processes via wmic.", flush=True)
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
                print("[Companion] Killed companion processes via PowerShell.", flush=True)
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
                    print(f"[Companion] Killed existing {proc_name} process(es).", flush=True)
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
                    print(f"[Companion] Killed existing companion {script} processes.", flush=True)
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
            print("[Companion] Killed existing companion Python processes via psutil.", flush=True)
            killed = True
    except (OSError, subprocess.TimeoutExpired, ImportError):
        pass

    if not killed:
        print("[Companion] No existing companion processes found.", flush=True)


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


def _companion_tests_action(source, target, env):
    _kill_companion_processes()
    command = [
        str(_companion_python()),
        "-m",
        "unittest",
        "discover",
        "-s",
        "companion",
        "-p",
        "test_*.py",
    ]
    print("[Companion] Running Python tests...", flush=True)
    return subprocess.call(command, cwd=str(ROOT))


def _companion_build_action(source, target, env):
    _kill_companion_processes()
    if os.name != "nt":
        raise RuntimeError("The Windows companion build requires Windows.")
    script = ROOT / "companion" / "build_windows.ps1"
    command = [
        "powershell.exe",
        "-NoProfile",
        "-ExecutionPolicy",
        "Bypass",
        "-File",
        str(script),
    ]
    print("[Companion] Building the Windows desktop app...", flush=True)
    return subprocess.call(command, cwd=str(ROOT))


def _companion_run_action(source, target, env):
    _kill_companion_processes()
    executable = ROOT / "dist" / "StickS3Companion" / "StickS3Companion.exe"
    if not executable.is_file():
        raise RuntimeError(
            f"Companion executable not found at {executable}. "
            "Run the companion_build_windows task first."
        )
    print(f"[Companion] Starting {executable}...", flush=True)
    return subprocess.call([str(executable)], cwd=str(ROOT))


def _companion_source_action(source, target, env):
    _kill_companion_processes()
    command = [str(_companion_python()), "-u", "companion/launcher.py"]
    print("[Companion] Starting the desktop app from source...", flush=True)
    return subprocess.call(command, cwd=str(ROOT))


Import("env")

PROJECT_DIR = Path(env.subst("$PROJECT_DIR")).resolve()
ROOT = (
    PROJECT_DIR
    if (PROJECT_DIR / "companion" / "ota_deploy.py").is_file()
    else PROJECT_DIR.parent / "StickS3-Intercom"
)
DEPLOYER = ROOT / "companion" / "ota_deploy.py"
PIO_ENVIRONMENT = env.subst("$PIOENV")

# Use "General" group (built-in, appears first in PlatformIO task explorer)
GROUP = "General"

env.AddTarget(
    group=GROUP,
    name="companion_run_source",
    dependencies=None,
    actions=_companion_source_action,
    title="Companion: run from source",
    description="Launch the desktop companion with the project Python environment.",
)
env.AddTarget(
    group=GROUP,
    name="companion_run",
    dependencies=None,
    actions=_companion_run_action,
    title="Companion: run packaged app",
    description="Launch the packaged StickS3Companion.exe from dist.",
)
env.AddTarget(
    group=GROUP,
    name="companion_build_windows",
    dependencies=None,
    actions=_companion_build_action,
    title="Companion: build Windows app",
    description="Build the packaged StickS3Companion Windows executable.",
)
env.AddTarget(
    group=GROUP,
    name="companion_tests",
    dependencies=None,
    actions=_companion_tests_action,
    title="Companion: run Python tests",
    description="Run the companion Python unittest suite.",
)
env.AddTarget(
    group=GROUP,
    name="ota_choose",
    dependencies=None,
    actions=_deploy_action(),
    title="OTA: choose environment and image",
    description="Interactively choose a StickS3 environment and image to upload.",
)
env.AddTarget(
    group=GROUP,
    name="ota_firmware",
    dependencies=None,
    actions=_deploy_action("firmware"),
    title="OTA: firmware",
    description="Build and upload this PlatformIO environment's firmware.",
)
env.AddTarget(
    group=GROUP,
    name="ota_littlefs",
    dependencies=None,
    actions=_deploy_action("filesystem"),
    title="OTA: LittleFS",
    description="Build and upload this PlatformIO environment's LittleFS image.",
)
env.AddTarget(
    group=GROUP,
    name="ota_firmware_littlefs",
    dependencies=None,
    actions=_deploy_action("both"),
    title="OTA: firmware and LittleFS",
    description="Build and upload both images for this PlatformIO environment.",
)
env.AddTarget(
    group=GROUP,
    name="ota_deploy",
    dependencies=None,
    actions=_deploy_action("firmware", "sticks3_ble_hid"),
    title="OTA: BLE HID firmware (default)",
    description="Build and upload StickS3 BLE HID firmware over authenticated OTA.",
)
