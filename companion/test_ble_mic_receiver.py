import asyncio
import struct
import tempfile
import unittest
import wave
from pathlib import Path
from unittest.mock import patch

from ble_mic_receiver import (
    FLAG_LIVE,
    FLAG_VOICE_COMMAND,
    FLAG_WIDEBAND,
    FRAME_SAMPLES,
    MIC_COMPANION_UUID,
    MIC_STREAM_UUID,
    SAMPLE_RATE_NARROWBAND,
    SAMPLE_RATE_WIDEBAND,
    Receiver,
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
                "stickS3_13_command.wav",
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

            self.assertEqual(removed, 3)
            self.assertTrue(all(not (output / name).exists() for name in recordings))
            self.assertTrue(unrelated.exists())
            self.assertTrue(transcript.exists())
            self.assertTrue((nested / "stickS3_0002_clip.wav").exists())

    def test_creates_missing_output_directory(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary) / "recordings"

            self.assertEqual(clear_existing_recordings(output), 0)
            self.assertTrue(output.is_dir())


class VoiceCommandFrameTests(unittest.TestCase):
    def test_command_flag_creates_command_recording(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary)
            receiver = Receiver(output)
            packet = struct.pack("<BBHH", 1, FLAG_VOICE_COMMAND, 0, 84)
            receiver.on_notification(None, bytearray(packet + bytes(84)))
            receiver._close_segment()

            self.assertTrue(
                (output / "stickS3_0001_command.wav").is_file()
            )


class AudioProtocolTests(unittest.TestCase):
    def _record(self, version: int, flags: int, frame_bytes: int) -> tuple[int, int]:
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary)
            receiver = Receiver(output)
            packet = struct.pack("<BBHH", version, flags, 0, frame_bytes)
            receiver.on_notification(
                None, bytearray(packet + bytes(frame_bytes))
            )
            receiver.on_notification(
                None,
                bytearray(struct.pack("<BBHH", version, flags | 0x80, 1, 0)),
            )

            recording = next(output.glob("stickS3_*.wav"))
            with wave.open(str(recording), "rb") as audio_file:
                return audio_file.getframerate(), audio_file.getnframes()

    def test_legacy_v1_packet_stays_narrowband(self) -> None:
        sample_rate, frames = self._record(1, FLAG_LIVE, 84)
        self.assertEqual(sample_rate, SAMPLE_RATE_NARROWBAND)
        self.assertEqual(frames, FRAME_SAMPLES[84])

    def test_v2_wideband_packet_is_16k(self) -> None:
        sample_rate, frames = self._record(
            2, FLAG_LIVE | FLAG_WIDEBAND, 164
        )
        self.assertEqual(sample_rate, SAMPLE_RATE_WIDEBAND)
        self.assertEqual(frames, FRAME_SAMPLES[164])

    def test_v2_packet_without_wideband_flag_stays_narrowband(self) -> None:
        sample_rate, _ = self._record(2, FLAG_LIVE, 84)
        self.assertEqual(sample_rate, SAMPLE_RATE_NARROWBAND)

    def test_frame_size_must_match_sample_rate(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary)
            receiver = Receiver(output)
            packet = struct.pack(
                "<BBHH", 2, FLAG_LIVE | FLAG_WIDEBAND, 0, 84
            )
            receiver.on_notification(
                None, bytearray(packet + bytes(84))
            )
            receiver._close_segment()

            self.assertFalse(any(output.glob("stickS3_*.wav")))


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
