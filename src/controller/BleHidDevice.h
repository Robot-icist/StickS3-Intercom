#pragma once

#include <Arduino.h>

#include <functional>

class BLECharacteristic;
class BLEHIDDevice;
class BLEServer;

namespace controller {

class BleMicAudio;

class BleHidDevice {
 public:
  using OtaCredentialsProvider = std::function<bool(char*, size_t)>;

  bool begin(BleMicAudio& micAudio, OtaCredentialsProvider otaCredentials);
  bool updateConnection(uint32_t now);
  bool connected() const { return _connected; }
  bool reportsReady() const;
  const char* errorMessage() const { return _errorMessage; }

  void sendKey(uint8_t key);
  void sendHotkey(uint8_t key);
  void sendHotkey(uint8_t modifier, uint8_t key);
  void sendKeyboardReport(uint8_t modifier, uint8_t key);
  void sendMouseReport(uint8_t buttons, int8_t dx, int8_t dy,
                       int8_t wheel = 0);

 private:
  BLEServer* _server = nullptr;
  BLEHIDDevice* _hid = nullptr;
  BLECharacteristic* _keyboardInputReport = nullptr;
  BLECharacteristic* _mouseInputReport = nullptr;
  OtaCredentialsProvider _otaCredentials;
  const char* _errorMessage = "BLE INIT FAILED";
  bool _connected = false;
  uint32_t _lastConnectionCheckAt = 0;
};

}  // namespace controller
