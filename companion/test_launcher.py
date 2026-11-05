import sys
import unittest
from unittest.mock import patch

import launcher


class LauncherTests(unittest.TestCase):
    def test_packaged_app_starts_transcription_when_launched_without_options(
        self,
    ) -> None:
        argv = ["StickS3Companion.exe"]
        with (
            patch.object(sys, "argv", argv),
            patch.object(sys, "frozen", True, create=True),
            patch("ble_mic_receiver.main") as receiver,
        ):
            launcher.main()

        self.assertEqual(argv, ["StickS3Companion.exe", "--transcribe"])
        receiver.assert_called_once_with()

    def test_explicit_receiver_only_option_is_not_overridden(self) -> None:
        argv = ["StickS3Companion.exe", "--receiver-only"]
        with (
            patch.object(sys, "argv", argv),
            patch.object(sys, "frozen", True, create=True),
            patch("ble_mic_receiver.main") as receiver,
        ):
            launcher.main()

        self.assertEqual(argv, ["StickS3Companion.exe", "--receiver-only"])
        receiver.assert_called_once_with()

    def test_source_launch_without_options_keeps_receiver_only_behavior(
        self,
    ) -> None:
        argv = ["launcher.py"]
        with (
            patch.object(sys, "argv", argv),
            patch.object(sys, "frozen", False, create=True),
            patch("ble_mic_receiver.main"),
        ):
            launcher.main()

        self.assertEqual(argv, ["launcher.py"])


if __name__ == "__main__":
    unittest.main()
