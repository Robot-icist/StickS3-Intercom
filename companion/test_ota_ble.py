import unittest
from types import SimpleNamespace
from unittest.mock import AsyncMock, patch

from bleak.exc import BleakError
from ble_mic_receiver import Candidate
from ota_ble import (
    OtaDevice,
    OtaDiscoveryError,
    _read_access,
    discover_ota_devices,
    parse_credentials,
)


class OtaCredentialTests(unittest.TestCase):
    def test_parses_valid_temporary_credentials(self) -> None:
        device = parse_credentials(
            b"sticks3\n192.168.1.25\n0123456789ABCDEF01234567",
            "StickS3 BLE Controller",
            "AA:BB:CC:DD:EE:FF",
        )

        self.assertEqual(device.family, "sticks3")
        self.assertEqual(device.host, "192.168.1.25")
        self.assertEqual(device.password, "0123456789ABCDEF01234567")

    def test_rejects_malformed_credentials(self) -> None:
        with self.assertRaisesRegex(ValueError, "invalid IPv4"):
            parse_credentials(
                "sticks3\nnot-an-ip\npassword",
                "StickS3",
                "AA:BB:CC:DD:EE:FF",
            )
        with self.assertRaisesRegex(ValueError, "Unsupported"):
            parse_credentials(
                "unknown\n192.168.1.25\npassword",
                "Unknown",
                "AA:BB:CC:DD:EE:FF",
            )


class OtaDiscoveryTests(unittest.IsolatedAsyncioTestCase):
    async def test_pairs_and_waits_for_ota_service_readiness(self) -> None:
        class FakeClient:
            is_connected = False

            def __init__(self) -> None:
                self.services = SimpleNamespace(
                    get_service=lambda _uuid: object()
                )
                self.responses = [
                    BleakError("encrypted read requires pairing"),
                    bytearray(b"WAIT_WIFI"),
                    bytearray(b"sticks3\n192.168.1.25\n0123456789ABCDEF"),
                ]

            async def connect(self) -> None:
                self.is_connected = True

            async def read_gatt_char(self, _uuid: str) -> bytearray:
                response = self.responses.pop(0)
                if isinstance(response, Exception):
                    raise response
                return response

            async def pair(self) -> bool:
                return True

            async def disconnect(self) -> None:
                self.is_connected = False

        fake_client = FakeClient()
        candidate = Candidate(
            "AA:BB:CC:DD:EE:FF", "StickS3 BLE Controller"
        )
        with (
            patch("ota_ble.create_client", return_value=fake_client),
            patch("ota_ble.asyncio.sleep", new=AsyncMock()),
        ):
            device = await _read_access(candidate)

        self.assertEqual(device.family, "sticks3")
        self.assertEqual(device.host, "192.168.1.25")
        self.assertEqual(len(fake_client.responses), 0)
        self.assertFalse(fake_client.is_connected)

    async def test_filters_devices_by_selected_firmware_family(self) -> None:
        device = SimpleNamespace(
            name="StickS3 BLE Controller",
            address="AA:BB:CC:DD:EE:FF",
        )
        advertisement = SimpleNamespace(local_name=device.name)
        discovered = OtaDevice(
            device.name,
            device.address,
            "sticks3",
            "192.168.1.25",
            "temporary-password",
        )
        with (
            patch("ota_ble.sys.platform", "linux"),
            patch(
                "ota_ble.BleakScanner.discover",
                new=AsyncMock(return_value={"device": (device, advertisement)}),
            ),
            patch(
                "ota_ble._read_access",
                new=AsyncMock(return_value=discovered),
            ),
        ):
            matches = await discover_ota_devices("sticks3")

        self.assertEqual(matches, [discovered])

    async def test_rejects_device_from_the_wrong_firmware_family(self) -> None:
        device = SimpleNamespace(name="StickS3", address="AA:BB:CC:DD:EE:FF")
        advertisement = SimpleNamespace(local_name=device.name)
        discovered = OtaDevice(
            device.name, device.address, "sticks3", "192.168.1.25", "temporary"
        )
        with (
            patch("ota_ble.sys.platform", "linux"),
            patch(
                "ota_ble.BleakScanner.discover",
                new=AsyncMock(return_value={"device": (device, advertisement)}),
            ),
            patch(
                "ota_ble._read_access",
                new=AsyncMock(return_value=discovered),
            ),
        ):
            with self.assertRaisesRegex(OtaDiscoveryError, "atoms3r"):
                await discover_ota_devices("atoms3r")


if __name__ == "__main__":
    unittest.main()
