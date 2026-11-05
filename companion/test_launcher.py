import sys
import unittest
from unittest.mock import patch

import launcher


class LauncherTests(unittest.TestCase):
    def test_packaged_app_opens_dashboard_by_default(self) -> None:
        argv = ["StickS3Companion.exe"]
        with (
            patch.object(sys, "argv", argv),
            patch.object(sys, "frozen", True, create=True),
            patch("desktop_app.main") as desktop,
        ):
            launcher.main()

        self.assertEqual(argv, ["StickS3Companion.exe"])
        desktop.assert_called_once_with()

    def test_explicit_cli_options_still_launch_receiver(self) -> None:
        argv = ["StickS3Companion.exe", "--receiver-only"]
        with (
            patch.object(sys, "argv", argv),
            patch.object(sys, "frozen", True, create=True),
            patch("ble_mic_receiver.main") as receiver,
        ):
            launcher.main()

        self.assertEqual(argv, ["StickS3Companion.exe", "--receiver-only"])
        receiver.assert_called_once_with()

    def test_source_launch_without_options_opens_dashboard(self) -> None:
        argv = ["launcher.py"]
        with (
            patch.object(sys, "argv", argv),
            patch.object(sys, "frozen", False, create=True),
            patch("desktop_app.main") as desktop,
        ):
            launcher.main()

        self.assertEqual(argv, ["launcher.py"])
        desktop.assert_called_once_with()

    def test_internal_receiver_option_is_removed_before_dispatch(self) -> None:
        argv = ["StickS3Companion.exe", "--internal-receiver", "--transcribe"]
        with (
            patch.object(sys, "argv", argv),
            patch.object(sys, "frozen", True, create=True),
            patch("ble_mic_receiver.main") as receiver,
        ):
            launcher.main()

        self.assertEqual(argv, ["StickS3Companion.exe", "--transcribe"])
        receiver.assert_called_once_with()


if __name__ == "__main__":
    unittest.main()
