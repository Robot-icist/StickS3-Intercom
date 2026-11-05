#include "BleMicAudio.h"

#include <BLECharacteristic.h>
#include <BLEDescriptor.h>
#include <BLEServer.h>
#include <BLEService.h>
#include <M5Unified.h>
#include <esp_heap_caps.h>

#if defined(CONFIG_BLUEDROID_ENABLED)
#include <BLE2902.h>
#endif

#include <algorithm>
#include <atomic>
#include <cstring>

namespace controller {
namespace {

constexpr char kMicServiceUuid[] = "e5d10001-3c9b-4f6a-9b2d-2a7f4510a001";
constexpr char kMicStreamUuid[] = "e5d10002-3c9b-4f6a-9b2d-2a7f4510a001";
constexpr char kMicCompanionUuid[] = "e5d10003-3c9b-4f6a-9b2d-2a7f4510a001";
constexpr uint32_t kCompanionHeartbeatTimeoutMs = 4000;
constexpr uint8_t kProtocolVersion = 1;
constexpr uint8_t kFlagLive = 0x01;
constexpr uint8_t kFlagClip = 0x02;
constexpr uint8_t kFlagEnd = 0x80;
constexpr size_t kPacketHeaderBytes = 6;

constexpr int8_t kIndexTable[16] = {
    -1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8,
};

constexpr int16_t kStepTable[89] = {
    7,     8,     9,     10,    11,    12,    13,    14,    16,    17,
    19,    21,    23,    25,    28,    31,    34,    37,    41,    45,
    50,    55,    60,    66,    73,    80,    88,    97,    107,   118,
    130,   143,   157,   173,   190,   209,   230,   253,   279,   307,
    337,   371,   408,   449,   494,   544,   598,   658,   724,   796,
    876,   963,   1060,  1166,  1282,  1411,  1552,  1707,  1878,  2066,
    2272,  2499,  2749,  3024,  3327,  3660,  4026,  4428,  4871,  5358,
    5894,  6484,  7132,  7845,  8630,  9493,  10442, 11487, 12635, 13899,
    15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767,
};

std::atomic<bool> gMicSubscribed{false};
std::atomic<bool> gCompanionHeartbeatReceived{false};
std::atomic<uint32_t> gLastCompanionHeartbeatAt{0};

class MicSubscriptionCallbacks final : public BLECharacteristicCallbacks {
 public:
  void onSubscribe(BLECharacteristic*, ble_gap_conn_desc*,
                   uint16_t subValue) override {
    gMicSubscribed.store(subValue != 0, std::memory_order_release);
    Serial.printf("[mic] BLE audio notifications %s\n",
                  subValue != 0 ? "enabled" : "disabled");
  }
};

MicSubscriptionCallbacks gMicCallbacks;

class CompanionHeartbeatCallbacks final : public BLECharacteristicCallbacks {
 public:
  void onWrite(BLECharacteristic*) override {
    gLastCompanionHeartbeatAt.store(millis(), std::memory_order_release);
    gCompanionHeartbeatReceived.store(true, std::memory_order_release);
  }
};

CompanionHeartbeatCallbacks gCompanionHeartbeatCallbacks;

}  // namespace

void resetBleMicSubscription() {
  gMicSubscribed.store(false, std::memory_order_release);
  gCompanionHeartbeatReceived.store(false, std::memory_order_release);
}

bool BleMicAudio::begin(BLEServer* server) {
  if (server == nullptr) {
    Serial.println("[mic] BLE server is unavailable");
    return false;
  }
  _server = server;

  _clipFrames = static_cast<uint8_t*>(heap_caps_malloc(
      static_cast<size_t>(kMaxClipFrames) * kEncodedFrameBytes,
      MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (_clipFrames == nullptr) {
    Serial.println("[mic] failed to allocate clip buffer in PSRAM");
    _status = MicAudioStatus::Error;
    return false;
  }

  _service = server->createService(BLEUUID(kMicServiceUuid), 12);
  if (_service == nullptr) {
    Serial.println("[mic] BLE audio service creation failed");
    _status = MicAudioStatus::Error;
    return false;
  }

  _audioStream = _service->createCharacteristic(
      BLEUUID(kMicStreamUuid),
      BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);
  if (_audioStream == nullptr) {
    Serial.println("[mic] BLE audio characteristic creation failed");
    _status = MicAudioStatus::Error;
    return false;
  }
#if defined(CONFIG_BLUEDROID_ENABLED)
  auto* cccd = new BLE2902();
  if (cccd == nullptr) {
    Serial.println("[mic] BLE notification descriptor allocation failed");
    _status = MicAudioStatus::Error;
    return false;
  }
  _audioStream->addDescriptor(cccd);
#endif
  _audioStream->setCallbacks(&gMicCallbacks);
  BLECharacteristic* companion = _service->createCharacteristic(
      BLEUUID(kMicCompanionUuid),
      BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR);
  if (companion == nullptr) {
    Serial.println("[mic] BLE companion heartbeat creation failed");
    _status = MicAudioStatus::Error;
    return false;
  }
  companion->setCallbacks(&gCompanionHeartbeatCallbacks);
  _service->start();

  if (M5.Mic.isEnabled()) {
    auto mic = M5.Mic.config();
    mic.sample_rate = kSampleRate;
    mic.stereo = false;
    mic.dma_buf_len = kSamplesPerFrame / 2;
    mic.dma_buf_count = 4;
    mic.task_priority = 2;
    mic.task_pinned_core = 1;
    M5.Mic.config(mic);
    _status = MicAudioStatus::Idle;
  } else {
    Serial.println("[mic] onboard microphone is unavailable");
    _status = MicAudioStatus::Unavailable;
  }
  return true;
}

bool BleMicAudio::subscribed() const {
  if (_server == nullptr || _server->getConnectedCount() == 0 ||
      _audioStream == nullptr) {
    return false;
  }
  if (!gCompanionHeartbeatReceived.load(std::memory_order_acquire) ||
      millis() - gLastCompanionHeartbeatAt.load(std::memory_order_acquire) >
          kCompanionHeartbeatTimeoutMs) {
    return false;
  }
#if defined(CONFIG_BLUEDROID_ENABLED)
  auto* descriptor = static_cast<BLE2902*>(
      _audioStream->getDescriptorByUUID(
          BLEUUID(static_cast<uint16_t>(0x2902))));
  if (descriptor != nullptr) {
    return descriptor->getNotifications();
  }
#endif
  return gMicSubscribed.load(std::memory_order_acquire);
}

bool BleMicAudio::startCapture() {
  if (!subscribed()) {
    Serial.println("[mic] connect a BLE audio companion before recording");
    return false;
  }
  if (!M5.Mic.isEnabled() || !M5.Mic.begin()) {
    Serial.println("[mic] microphone startup failed");
    _status = MicAudioStatus::Error;
    return false;
  }
  _micRunning = true;
  _recordPending = false;
  _sequence = 0;
  return true;
}

void BleMicAudio::stopCapture() {
  if (_micRunning) {
    M5.Mic.end();
    _micRunning = false;
  }
  _recordPending = false;
}

bool BleMicAudio::toggleLive() {
  if (_status == MicAudioStatus::Live) {
    stopCapture();
    sendEndMarker(_sequence, kFlagLive);
    _status = MicAudioStatus::Idle;
    Serial.println("[mic] live stream stopped");
    return true;
  }
  if (_status != MicAudioStatus::Idle || !startCapture()) {
    return false;
  }
  _status = MicAudioStatus::Live;
  Serial.println("[mic] live stream started");
  return true;
}

bool BleMicAudio::toggleClipRecording() {
  if (_status == MicAudioStatus::Recording) {
    stopCapture();
    _sendFrameIndex = 0;
    _lastFrameSentAt = millis() - kFrameIntervalMs;
    _status = MicAudioStatus::Sending;
    Serial.printf("[mic] sending recorded clip (%u frames)\n",
                  static_cast<unsigned>(_clipFrameCount));
    return true;
  }
  if (_status != MicAudioStatus::Idle || !startCapture()) {
    return false;
  }
  _clipFrameCount = 0;
  _status = MicAudioStatus::Recording;
  Serial.println("[mic] clip recording started");
  return true;
}

void BleMicAudio::cancel() {
  const MicAudioStatus previousStatus = _status;
  stopCapture();
  if (previousStatus == MicAudioStatus::Live) {
    sendEndMarker(_sequence, kFlagLive);
  } else if (previousStatus == MicAudioStatus::Sending) {
    sendEndMarker(_sendFrameIndex, kFlagClip);
  }
  _clipFrameCount = 0;
  _sendFrameIndex = 0;
  _status = M5.Mic.isEnabled() ? MicAudioStatus::Idle
                               : MicAudioStatus::Unavailable;
  Serial.println("[mic] audio operation cancelled");
}

void BleMicAudio::encodeFrame(uint8_t* output) const {
  int32_t predictor = _samples[0];
  int32_t stepIndex = 0;
  output[0] = static_cast<uint8_t>(predictor & 0xff);
  output[1] = static_cast<uint8_t>((predictor >> 8) & 0xff);
  output[2] = 0;
  output[3] = 0;
  std::memset(output + 4, 0, kEncodedFrameBytes - 4);

  for (size_t sampleIndex = 1; sampleIndex < kSamplesPerFrame;
       ++sampleIndex) {
    const int32_t step = kStepTable[stepIndex];
    int32_t difference = static_cast<int32_t>(_samples[sampleIndex]) - predictor;
    uint8_t code = 0;
    if (difference < 0) {
      code = 8;
      difference = -difference;
    }

    int32_t delta = step >> 3;
    if (difference >= step) {
      code |= 4;
      difference -= step;
      delta += step;
    }
    if (difference >= (step >> 1)) {
      code |= 2;
      difference -= step >> 1;
      delta += step >> 1;
    }
    if (difference >= (step >> 2)) {
      code |= 1;
      delta += step >> 2;
    }

    predictor += (code & 8) != 0 ? -delta : delta;
    predictor = std::max<int32_t>(-32768, std::min<int32_t>(32767, predictor));
    stepIndex =
        std::max<int32_t>(0, std::min<int32_t>(88, stepIndex + kIndexTable[code]));

    const size_t nibbleIndex = sampleIndex - 1;
    uint8_t& packed = output[4 + nibbleIndex / 2];
    if ((nibbleIndex & 1) == 0) {
      packed = code;
    } else {
      packed |= static_cast<uint8_t>(code << 4);
    }
  }
}

bool BleMicAudio::sendFrame(const uint8_t* frame, uint16_t sequence,
                            uint8_t flags) {
  if (_audioStream == nullptr || !subscribed()) {
    return false;
  }
  uint8_t packet[kPacketHeaderBytes + kEncodedFrameBytes] = {};
  packet[0] = kProtocolVersion;
  packet[1] = flags;
  packet[2] = static_cast<uint8_t>(sequence & 0xff);
  packet[3] = static_cast<uint8_t>(sequence >> 8);
  packet[4] = static_cast<uint8_t>(kEncodedFrameBytes & 0xff);
  packet[5] = static_cast<uint8_t>(kEncodedFrameBytes >> 8);
  std::memcpy(packet + kPacketHeaderBytes, frame, kEncodedFrameBytes);
  _audioStream->setValue(packet, sizeof(packet));
  _audioStream->notify();
  return true;
}

bool BleMicAudio::sendEndMarker(uint16_t sequence, uint8_t flags) {
  if (_audioStream == nullptr || !subscribed()) {
    return false;
  }
  const uint8_t packet[kPacketHeaderBytes] = {
      kProtocolVersion,
      static_cast<uint8_t>(flags | kFlagEnd),
      static_cast<uint8_t>(sequence & 0xff),
      static_cast<uint8_t>(sequence >> 8),
      0,
      0,
  };
  _audioStream->setValue(packet, sizeof(packet));
  _audioStream->notify();
  return true;
}

void BleMicAudio::processCapturedFrame(uint32_t now) {
  encodeFrame(_encodedFrame);
  if (_status == MicAudioStatus::Live) {
    if (!sendFrame(_encodedFrame, _sequence++, kFlagLive)) {
      Serial.println("[mic] live audio notification could not be sent");
      stopCapture();
      _status = MicAudioStatus::Error;
      return;
    }
    _lastFrameSentAt = now;
  } else if (_status == MicAudioStatus::Recording) {
    if (_clipFrameCount >= kMaxClipFrames) {
      Serial.println("[mic] clip reached 8-second limit; sending");
      stopCapture();
      _sendFrameIndex = 0;
      _lastFrameSentAt = now - kFrameIntervalMs;
      _status = MicAudioStatus::Sending;
      return;
    }
    std::memcpy(_clipFrames +
                    static_cast<size_t>(_clipFrameCount) * kEncodedFrameBytes,
                _encodedFrame, kEncodedFrameBytes);
    ++_clipFrameCount;
  }
}

void BleMicAudio::update(uint32_t now) {
  if ((_status == MicAudioStatus::Live ||
       _status == MicAudioStatus::Recording ||
       _status == MicAudioStatus::Sending) &&
      !subscribed()) {
    stopCapture();
    _clipFrameCount = 0;
    _sendFrameIndex = 0;
    _status = M5.Mic.isEnabled() ? MicAudioStatus::Idle
                                 : MicAudioStatus::Unavailable;
    Serial.println("[mic] companion disconnected; audio operation stopped");
    return;
  }

  if (_status == MicAudioStatus::Live ||
      _status == MicAudioStatus::Recording) {
    if (_recordPending && M5.Mic.isRecording() == 0) {
      _recordPending = false;
      processCapturedFrame(now);
    }
    if ((_status == MicAudioStatus::Live ||
         _status == MicAudioStatus::Recording) &&
        !_recordPending) {
      if (!M5.Mic.record(_samples, kSamplesPerFrame, kSampleRate, false)) {
        Serial.println("[mic] failed to queue microphone samples");
        stopCapture();
        _status = MicAudioStatus::Error;
        return;
      }
      _recordPending = true;
    }
    return;
  }

  if (_status != MicAudioStatus::Sending ||
      now - _lastFrameSentAt < kFrameIntervalMs) {
    return;
  }
  if (_sendFrameIndex < _clipFrameCount) {
    const uint8_t* frame =
        _clipFrames + static_cast<size_t>(_sendFrameIndex) * kEncodedFrameBytes;
    if (!sendFrame(frame, _sendFrameIndex, kFlagClip)) {
      Serial.println("[mic] clip notification could not be sent");
      _status = MicAudioStatus::Error;
      return;
    }
    ++_sendFrameIndex;
    _lastFrameSentAt = now;
    return;
  }

  sendEndMarker(_clipFrameCount, kFlagClip);
  _status = MicAudioStatus::Idle;
  _clipFrameCount = 0;
  _sendFrameIndex = 0;
  Serial.println("[mic] clip transfer complete");
}

}  // namespace controller
