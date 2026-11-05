"""Dispatch the desktop companion, BLE receiver, or transcriber."""

import os
import sys


def _configure_worker_logging() -> None:
    log_path = os.environ.get("STICKS3_COMPANION_LOG")
    if not log_path:
        return
    log_file = open(log_path, "a", encoding="utf-8", buffering=1)
    sys.stdout = log_file
    sys.stderr = log_file


def main() -> None:
    if len(sys.argv) > 1 and sys.argv[1] == "--internal-transcriber":
        del sys.argv[1]
        _configure_worker_logging()
        from live_transcriber import main as run_transcriber

        run_transcriber()
    elif len(sys.argv) > 1 and sys.argv[1] == "--internal-receiver":
        del sys.argv[1]
        _configure_worker_logging()
        from ble_mic_receiver import main as run_receiver

        run_receiver()
    elif len(sys.argv) == 1 or sys.argv[1] == "--gui":
        from desktop_app import main as run_desktop_app

        run_desktop_app()
    else:
        from ble_mic_receiver import main as run_receiver

        run_receiver()


if __name__ == "__main__":
    main()
