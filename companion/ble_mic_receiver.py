import argparse
import asyncio
import struct
import wave
from pathlib import Path

from bleak import BleakClient, BleakScanner


DEVICE_NAME = "StickS3 BLE Controller"
MIC_SERVICE_UUID = "e5d10001-3c9b-4f6a-9b2d-2a7f4510a001"
MIC_STREAM_UUID = "e5d10002-3c9b-4f6a-9b2d-2a7f4510a001"
SAMPLE_RATE = 8000
SAMPLES_PER_FRAME = 160
HEADER_SIZE = 6
FLAG_LIVE = 0x01
FLAG_CLIP = 0x02
FLAG_END = 0x80

STEP_TABLE = (
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37,
    41, 45, 50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130, 143, 157, 173,
    190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658,
    724, 796, 876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066,
    2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358, 5894, 6484,
    7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500,
    20350, 22385, 24623, 27086, 29794, 32767,
)
INDEX_TABLE = (-1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8)


def decode_frame(encoded: bytes) -> bytes:
    if len(encoded) != 84:
        raise ValueError(f"expected 84 ADPCM bytes, got {len(encoded)}")

    predictor = struct.unpack_from("<h", encoded)[0]
    step_index = min(encoded[2], 88)
    samples = [predictor]

    for nibble_index in range((SAMPLES_PER_FRAME - 1)):
        packed = encoded[4 + nibble_index // 2]
        code = (packed >> 4) & 0x0F if nibble_index & 1 else packed & 0x0F
        step = STEP_TABLE[step_index]
        difference = step >> 3
        if code & 4:
            difference += step
        if code & 2:
            difference += step >> 1
        if code & 1:
            difference += step >> 2
        predictor += -difference if code & 8 else difference
        predictor = max(-32768, min(32767, predictor))
        step_index = max(0, min(88, step_index + INDEX_TABLE[code]))
        samples.append(predictor)

    return struct.pack("<160h", *samples)


class Receiver:
    def __init__(self, output_dir: Path) -> None:
        self.output_dir = output_dir
        self.output_dir.mkdir(parents=True, exist_ok=True)
        self.segment = 0
        self.audio_file: wave.Wave_write | None = None
        self.last_sequence: int | None = None
        self.mode: str | None = None

    def _close_segment(self) -> None:
        if self.audio_file is not None:
            self.audio_file.close()
            self.audio_file = None
            print("Audio segment saved.")
        self.last_sequence = None
        self.mode = None

    def _start_segment(self, mode: str) -> None:
        self._close_segment()
        self.segment += 1
        path = self.output_dir / f"stickS3_{self.segment:04d}_{mode}.wav"
        self.audio_file = wave.open(str(path), "wb")
        self.audio_file.setnchannels(1)
        self.audio_file.setsampwidth(2)
        self.audio_file.setframerate(SAMPLE_RATE)
        self.mode = mode
        print(f"Receiving {mode} audio -> {path}")

    def on_notification(self, _characteristic: object, data: bytearray) -> None:
        if len(data) < HEADER_SIZE:
            print(f"Ignoring short packet ({len(data)} bytes).")
            return

        version, flags, sequence, frame_size = struct.unpack_from("<BBHH", data)
        if version != 1:
            print(f"Ignoring unsupported audio protocol version {version}.")
            return

        if flags & FLAG_END:
            self._close_segment()
            print("Audio transmission ended.")
            return

        mode = "live" if flags & FLAG_LIVE else "clip" if flags & FLAG_CLIP else None
        payload = bytes(data[HEADER_SIZE:])
        if mode is None or frame_size != len(payload):
            print("Ignoring malformed audio frame.")
            return

        if self.audio_file is None or self.mode != mode:
            self._start_segment(mode)
        if self.last_sequence is not None:
            expected = (self.last_sequence + 1) & 0xFFFF
            if sequence != expected:
                print(f"Audio packet gap: expected {expected}, received {sequence}.")
        self.last_sequence = sequence

        try:
            self.audio_file.writeframesraw(decode_frame(payload))
        except ValueError as error:
            print(f"Ignoring invalid ADPCM frame: {error}")


async def run(output_dir: Path) -> None:
    print(f"Scanning for {DEVICE_NAME!r}...")
    device = await BleakScanner.find_device_by_filter(
        lambda candidate, advertisement: (
            advertisement.local_name or candidate.name
        ) == DEVICE_NAME,
        timeout=20,
    )
    if device is None:
        raise RuntimeError(
            "StickS3 not found. Enter Microphone mode and pair/connect it first."
        )

    receiver = Receiver(output_dir)
    async with BleakClient(device) as client:
        services = client.services
        if services.get_service(MIC_SERVICE_UUID) is None:
            raise RuntimeError(
                "The connected StickS3 does not expose the BLE microphone service."
            )
        await client.start_notify(MIC_STREAM_UUID, receiver.on_notification)
        print("Connected. Use joystick click for live audio, BtnA for a clip, "
              "or Ctrl+C to quit.")
        try:
            await asyncio.Event().wait()
        finally:
            await client.stop_notify(MIC_STREAM_UUID)
            receiver._close_segment()


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Receive StickS3 custom BLE microphone audio as WAV files."
    )
    parser.add_argument(
        "--output",
        type=Path,
        default=Path("recordings"),
        help="directory for received WAV files (default: ./recordings)",
    )
    args = parser.parse_args()
    try:
        asyncio.run(run(args.output))
    except KeyboardInterrupt:
        print("\nStopped.")


if __name__ == "__main__":
    main()
