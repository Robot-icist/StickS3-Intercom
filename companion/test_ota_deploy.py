import contextlib
import io
import subprocess
import tempfile
import unittest
from pathlib import Path
from unittest.mock import AsyncMock, patch

import ota_deploy
from interactive_prompts import _voice_choice
from ota_ble import OtaDevice, OtaDiscoveryError


class OtaDeployTests(unittest.TestCase):
    def test_environment_menu_selects_ble_hid_environment(self) -> None:
        with patch("ota_deploy.prompt_choice", return_value=1) as choose:
            target = ota_deploy._choose_target()

        self.assertEqual(target.environment, "sticks3_ble_hid")
        self.assertEqual(choose.call_args.kwargs["default"], 1)
        self.assertIn("sticks3_ble_hid", choose.call_args.args[2][1])

    def test_cancelled_deploy_returns_failure_status(self) -> None:
        with (
            patch("sys.argv", ["ota_deploy.py"]),
            patch("ota_deploy._choose_target", return_value=None),
        ):
            self.assertFalse(ota_deploy.main())

    def test_platformio_task_options_select_component_and_environment(self) -> None:
        with patch(
            "sys.argv",
            [
                "ota_deploy.py",
                "--environment",
                "sticks3_ble_hid",
                "--component",
                "firmware",
            ],
        ):
            arguments = ota_deploy._parse_args()

        self.assertEqual(arguments.environment, "sticks3_ble_hid")
        self.assertEqual(arguments.component, "firmware")

    def test_voice_selects_popup_option_by_alias(self) -> None:
        selected = _voice_choice(
            "I choose Stick S three BLE HID",
            (
                "StickS3 intercom (sticks3_intercom)",
                "StickS3 BLE HID (sticks3_ble_hid)",
                "AtomS3R intercom (atoms3r_intercom)",
            ),
            ("StickS3 intercom", "StickS3 BLE HID", "AtomS3R intercom"),
        )

        self.assertEqual(selected, 1)

    def test_unique_device_is_selected_without_an_extra_prompt(self) -> None:
        device = OtaDevice(
            "StickS3 BLE Controller",
            "AA:BB:CC:DD:EE:FF",
            "sticks3",
            "192.168.1.25",
            "temporary-password",
        )
        with (
            patch(
                "ota_deploy.discover_ota_devices",
                new=AsyncMock(return_value=[device]),
            ),
            patch("ota_deploy.prompt_choice") as choose,
        ):
            selected = ota_deploy._choose_device(ota_deploy.TARGETS[1])

        self.assertEqual(selected, device)
        choose.assert_not_called()

    def test_device_discovery_retries_after_previous_image_upload(self) -> None:
        device = OtaDevice(
            "StickS3 BLE Controller",
            "AA:BB:CC:DD:EE:FF",
            "sticks3",
            "192.168.1.25",
            "temporary-password",
        )

        with (
            patch(
                "ota_deploy.discover_ota_devices",
                new=AsyncMock(side_effect=[OtaDiscoveryError("rebooting"), [device]]),
            ) as discover,
            patch("ota_deploy.asyncio.sleep", new=AsyncMock()),
        ):
            selected = ota_deploy._choose_device(
                ota_deploy.TARGETS[1], retry_count=2
            )

        self.assertEqual(selected, device)
        self.assertEqual(discover.await_count, 2)

    def test_multiple_devices_show_a_picker(self) -> None:
        devices = [
            OtaDevice("StickS3 A", "AA:BB:CC:DD:EE:01", "sticks3",
                      "192.168.1.25", "temporary-one"),
            OtaDevice("StickS3 B", "AA:BB:CC:DD:EE:02", "sticks3",
                      "192.168.1.26", "temporary-two"),
        ]
        with (
            patch(
                "ota_deploy.discover_ota_devices",
                new=AsyncMock(return_value=devices),
            ),
            patch("ota_deploy.prompt_choice", return_value=1) as choose,
        ):
            selected = ota_deploy._choose_device(ota_deploy.TARGETS[0])

        self.assertEqual(selected, devices[1])
        self.assertIn("StickS3 B", choose.call_args.args[2][1])

    def test_filesystem_upload_uses_spiffs_command_and_hides_password(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            image = Path(temporary) / "littlefs.bin"
            uploader = Path(temporary) / "espota.py"
            project = Path(temporary) / "project"
            image.touch()
            uploader.touch()
            project.mkdir()
            output = io.StringIO()
            with (
                patch("ota_deploy.subprocess.run") as run,
                contextlib.redirect_stdout(output),
            ):
                run.return_value = subprocess.CompletedProcess([], 0)
                ota_deploy._upload(
                    image,
                    OtaDevice(
                        "StickS3",
                        "AA:BB:CC:DD:EE:FF",
                        "sticks3",
                        "192.168.1.25",
                        "private-device-password",
                    ),
                    uploader,
                    filesystem=True,
                    cwd=project,
                )

            command = run.call_args.args[0]
            self.assertEqual(run.call_args.kwargs["cwd"], project)
            self.assertEqual(command[1], "-u")
            self.assertIn("--auth", command)
            self.assertEqual(
                command[command.index("--auth") + 1],
                "private-device-password",
            )
            self.assertIn("--spiffs", command)
            self.assertNotIn("private-device-password", output.getvalue())

    def test_failed_upload_error_does_not_reveal_password(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            image = Path(temporary) / "firmware.bin"
            uploader = Path(temporary) / "espota.py"
            image.touch()
            uploader.touch()
            with patch("ota_deploy.subprocess.run") as run:
                run.return_value = subprocess.CompletedProcess([], 1)
                with self.assertRaises(RuntimeError) as raised:
                    ota_deploy._upload(
                        image,
                        OtaDevice(
                            "StickS3",
                            "AA:BB:CC:DD:EE:FF",
                            "sticks3",
                            "192.168.1.25",
                            "private-device-password",
                        ),
                        uploader,
                        filesystem=False,
                        cwd=Path(temporary),
                    )

            self.assertNotIn("private-device-password", str(raised.exception))

    def test_main_builds_and_uploads_selected_firmware(self) -> None:
        target = ota_deploy.TARGETS[1]
        device = OtaDevice(
            "StickS3 BLE Controller",
            "AA:BB:CC:DD:EE:FF",
            "sticks3",
            "192.168.1.25",
            "temporary-password",
        )
        image = target.project / ".pio" / "build" / target.environment / "firmware.bin"
        uploader = Path("espota.py")
        with (
            patch(
                "sys.argv",
                [
                    "ota_deploy.py",
                    "--environment",
                    target.environment,
                    "--component",
                    "firmware",
                ],
            ),
            patch("ota_deploy._find_pio", return_value="pio"),
            patch("ota_deploy._find_espota", return_value=uploader),
            patch("ota_deploy._build", return_value=image) as build,
            patch("ota_deploy._choose_device", return_value=device) as choose_device,
            patch("ota_deploy._upload") as upload,
        ):
            ota_deploy.main()

        build.assert_called_once_with(target, "firmware", "pio")
        choose_device.assert_called_once_with(target, retry_count=4)
        upload.assert_called_once_with(
            image,
            device,
            uploader,
            filesystem=False,
            cwd=target.project,
        )

    def test_both_images_upload_filesystem_before_firmware(self) -> None:
        target = ota_deploy.TARGETS[1]
        device = OtaDevice(
            "StickS3 BLE Controller",
            "AA:BB:CC:DD:EE:FF",
            "sticks3",
            "192.168.1.25",
            "temporary-password",
        )
        images = {
            "filesystem": Path("littlefs.bin"),
            "firmware": Path("firmware.bin"),
        }
        with (
            patch(
                "sys.argv",
                [
                    "ota_deploy.py",
                    "--environment",
                    target.environment,
                    "--component",
                    "both",
                ],
            ),
            patch("ota_deploy._find_pio", return_value="pio"),
            patch("ota_deploy._find_espota", return_value=Path("espota.py")),
            patch(
                "ota_deploy._build",
                side_effect=lambda _target, component, _pio: images[component],
            ) as build,
            patch("ota_deploy._choose_device", return_value=device) as choose_device,
            patch("ota_deploy._upload") as upload,
        ):
            ota_deploy.main()

        self.assertEqual(
            [call.args[1] for call in build.call_args_list],
            ["filesystem", "firmware"],
        )
        self.assertEqual(
            [call.kwargs["filesystem"] for call in upload.call_args_list],
            [True, False],
        )
        self.assertEqual(
            [call.kwargs["retry_count"] for call in choose_device.call_args_list],
            [4, 12],
        )


if __name__ == "__main__":
    unittest.main()
