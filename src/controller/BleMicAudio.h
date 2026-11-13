#pragma once

#include <Arduino.h>

class BLECharacteristic;
class BLEServer;
class BLEService;

namespace controller
{

  void resetBleMicSubscription();

  enum class MicAudioStatus : uint8_t
  {
    Unavailable,
    Idle,
    Live,
    Recording,
    Sending,
    Error
  };

  class BleMicAudio
  {
  public:
    bool begin(BLEServer *server);
    void update(uint32_t now);
    bool toggleLive();
    bool toggleClipRecording();
    void cancel();
    bool subscribed() const;

    // External (web) audio input: PCM16 samples at 16 kHz, 320 per frame.
    bool externalLiveStart();
    bool externalLiveStop();
    bool externalLiveFrame(const int16_t* samples);
    bool externalClipStart();
    bool externalClipFrame(const int16_t* samples);
    bool externalClipFinish();
    MicAudioStatus status() const { return _status; }
    uint16_t clipFrameCount() const { return _clipFrameCount; }

  private:
    // 16 kHz wideband speech in 20 ms frames. Keeping the 20 ms cadence of the
    // original 8 kHz stream preserves the microphone queue and main-loop timing
    // margins; the frame grows to 164 encoded bytes (170-byte packet), which
    // still fits the 185-byte MTU requested in BleHidDevice.
    static constexpr size_t kSamplesPerFrame = 320;
    static constexpr size_t kEncodedFrameBytes = 164;
    static constexpr uint32_t kSampleRate = 16000;
    static constexpr uint16_t kMaxClipFrames = 400;
    static constexpr uint32_t kFrameIntervalMs = 20;

    bool startCapture();
    void stopCapture();
    void processCapturedFrame(uint32_t now);
    void encodeFrame(uint8_t *output);
    bool sendFrame(const uint8_t *frame, uint16_t sequence, uint8_t flags);
    bool sendEndMarker(uint16_t sequence, uint8_t flags);

    BLEService *_service = nullptr;
    BLEServer *_server = nullptr;
    BLECharacteristic *_audioStream = nullptr;
    uint8_t *_clipFrames = nullptr;
    int16_t _samples[kSamplesPerFrame] = {};
    uint8_t _encodedFrame[kEncodedFrameBytes] = {};
    uint16_t _clipFrameCount = 0;
    uint16_t _sendFrameIndex = 0;
    uint16_t _sequence = 0;
    uint32_t _lastFrameSentAt = 0;
    uint8_t _stepIndex = 0;
    bool _recordPending = false;
    bool _micRunning = false;
    bool _externalAudio = false;
    MicAudioStatus _status = MicAudioStatus::Unavailable;
  };

} // namespace controller
