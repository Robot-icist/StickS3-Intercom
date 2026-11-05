import asyncio
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from ble_mic_receiver import (
    MIC_COMPANION_UUID,
    MIC_STREAM_UUID,
    clear_existing_recordings,
    receive,
)


class ClearExistingRecordingsTests(unittest.TestCase):
    def test_removes_only_receiver_wav_recordings(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary)
            recordings = (
                "stickS3_0001_live.wav",
                "stickS3_12_clip.wav",
            )
            for name in recordings:
                (output / name).write_bytes(b"recording")
            unrelated = output / "keep.wav"
            unrelated.write_bytes(b"keep")
            transcript = output / "stickS3_0001_live.txt"
            transcript.write_text("keep", encoding="utf-8")
            nested = output / "nested"
            nested.mkdir()
            (nested / "stickS3_0002_clip.wav").write_bytes(b"keep")

            removed = clear_existing_recordings(output)

            self.assertEqual(removed, 2)
            self.assertTrue(all(not (output / name).exists() for name in recordings))
            self.assertTrue(unrelated.exists())
            self.assertTrue(transcript.exists())
            self.assertTrue((nested / "stickS3_0002_clip.wav").exists())

    def test_creates_missing_output_directory(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary) / "recordings"

            self.assertEqual(clear_existing_recordings(output), 0)
            self.assertTrue(output.is_dir())


class CompanionHeartbeatTests(unittest.IsolatedAsyncioTestCase):
    async def test_receiver_sends_heartbeat_after_subscribing(self) -> None:
        class FakeClient:
            is_connected = True

            def __init__(self) -> None:
                self.disconnected = asyncio.Event()
                self.writes: list[tuple[str, bytes, bool]] = []
                self.notification_uuid: str | None = None
                self.notification_callback: object | None = None
                self.stopped_uuid: str | None = None
                self.disconnected_cleanly = False

            async def start_notify(self, uuid: str, callback: object) -> None:
                self.notification_uuid = uuid
                self.notification_callback = callback

            async def write_gatt_char(
                self, uuid: str, data: bytes, *, response: bool
            ) -> None:
                self.writes.append((uuid, data, response))
                if len(self.writes) == 2:
                    self.disconnected.set()

            async def stop_notify(self, uuid: str) -> None:
                self.stopped_uuid = uuid

            async def disconnect(self) -> None:
                self.disconnected_cleanly = True
                self.is_connected = False

        with tempfile.TemporaryDirectory() as temporary:
            client = FakeClient()
            with patch(
                "ble_mic_receiver.COMPANION_HEARTBEAT_SECONDS", 0.0
            ):
                await receive(client, Path(temporary), client.disconnected)

        self.assertEqual(client.notification_uuid, MIC_STREAM_UUID)
        self.assertTrue(callable(client.notification_callback))
        self.assertEqual(
            client.writes,
            [
                (MIC_COMPANION_UUID, b"\x01", False),
                (MIC_COMPANION_UUID, b"\x01", False),
            ],
        )
        self.assertEqual(client.stopped_uuid, MIC_STREAM_UUID)
        self.assertTrue(client.disconnected_cleanly)


if __name__ == "__main__":
    unittest.main()
