import tempfile
import unittest
from pathlib import Path

from ble_mic_receiver import clear_existing_recordings


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


if __name__ == "__main__":
    unittest.main()
