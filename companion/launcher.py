"""Dispatch the bundled companion executable to its receiver or transcriber."""

import sys


def main() -> None:
    if len(sys.argv) > 1 and sys.argv[1] == "--internal-transcriber":
        del sys.argv[1]
        from live_transcriber import main as run_transcriber

        run_transcriber()
    else:
        from ble_mic_receiver import main as run_receiver

        run_receiver()


if __name__ == "__main__":
    main()
