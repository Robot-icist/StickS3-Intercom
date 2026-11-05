#pragma once

#include <Arduino.h>

class BLECharacteristic;
class BLEServer;
class BLEService;

namespace controller {

void resetBleMicSubscription();

enum class MicAudioStatus : uint8_t {
  Unavailable,
  Idle,
  Live,
  Recording,
  Sending,
  Error
};

class BleMicAudio {
 public:
  bool begin(BLEServer* server);
  void update(uint32_t now);
  bool toggleLive();
  bool toggleClipRecording();
  void cancel();
  bool subscribed() const;
  MicAudioStatus status() const { return _status; }
  uint16_t clipFrameCount() const { return _clipFrameCount; }

 private:
  static constexpr size_t kSamplesPerFrame = 160;
  static constexpr size_t kEncodedFrameBytes = 84;
  static constexpr uint32_t kSampleRate = 8000;
  static constexpr uint16_t kMaxClipFrames = 400;
  static constexpr uint32_t kFrameIntervalMs = 20;

  bool startCapture();
  void stopCapture();
  void processCapturedFrame(uint32_t now);
  void encodeFrame(uint8_t* output) const;
  bool sendFrame(const uint8_t* frame, uint16_t sequence, uint8_t flags);
  bool sendEndMarker(uint16_t sequence, uint8_t flags);

  BLEService* _service = nullptr;
  BLEServer* _server = nullptr;
  BLECharacteristic* _audioStream = nullptr;
  uint8_t* _clipFrames = nullptr;
  int16_t _samples[kSamplesPerFrame] = {};
  uint8_t _encodedFrame[kEncodedFrameBytes] = {};
  uint16_t _clipFrameCount = 0;
  uint16_t _sendFrameIndex = 0;
  uint16_t _sequence = 0;
  uint32_t _lastFrameSentAt = 0;
  bool _recordPending = false;
  bool _micRunning = false;
  MicAudioStatus _status = MicAudioStatus::Unavailable;
};

}  // namespace controller
