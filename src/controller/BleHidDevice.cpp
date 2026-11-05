#include "BleHidDevice.h"

#include <BLECharacteristic.h>
#include <BLEDescriptor.h>
#include <BLEDevice.h>
#include <BLEHIDDevice.h>
#if defined(CONFIG_BLUEDROID_ENABLED)
#include <BLE2902.h>
#endif
#include <BLEServer.h>
#include <BLEService.h>
#include <BLESecurity.h>

#include <atomic>
#include <cstring>
#include <new>
#include <utility>

#include "BleMicAudio.h"

namespace controller {
namespace {

constexpr uint8_t kModifierLeftControl = 0x01;
constexpr char kOtaServiceUuid[] =
    "e5d20001-3c9b-4f6a-9b2d-2a7f4510a002";
constexpr char kOtaCredentialsUuid[] =
    "e5d20002-3c9b-4f6a-9b2d-2a7f4510a002";

uint8_t kKeyboardReportMap[] = {
    0x05, 0x01,        // Usage Page (Generic Desktop)
    0x09, 0x06,        // Usage (Keyboard)
    0xA1, 0x01,        // Collection (Application)
    0x85, 0x01,        //   Report ID (1)
    0x05, 0x07,        //   Usage Page (Keyboard/Keypad)
    0x19, 0xE0,        //   Usage Minimum (Left Control)
    0x29, 0xE7,        //   Usage Maximum (Right GUI)
    0x15, 0x00,        //   Logical Minimum (0)
    0x25, 0x01,        //   Logical Maximum (1)
    0x75, 0x01,        //   Report Size (1)
    0x95, 0x08,        //   Report Count (8)
    0x81, 0x02,        //   Input (Data, Variable, Absolute)
    0x95, 0x01,        //   Report Count (1)
    0x75, 0x08,        //   Report Size (8)
    0x81, 0x01,        //   Input (Constant)
    0x95, 0x06,        //   Report Count (6)
    0x75, 0x08,        //   Report Size (8)
    0x15, 0x00,        //   Logical Minimum (0)
    0x25, 0x65,        //   Logical Maximum (101)
    0x05, 0x07,        //   Usage Page (Keyboard/Keypad)
    0x19, 0x00,        //   Usage Minimum (0)
    0x29, 0x65,        //   Usage Maximum (101)
    0x81, 0x00,        //   Input (Data, Array)
    0xC0,              // End Collection
};

uint8_t kMouseReportMap[] = {
    0x05, 0x01,        // Usage Page (Generic Desktop)
    0x09, 0x02,        // Usage (Mouse)
    0xA1, 0x01,        // Collection (Application)
    0x85, 0x02,        //   Report ID (2)
    0x09, 0x01,        //   Usage (Pointer)
    0xA1, 0x00,        //   Collection (Physical)
    0x05, 0x09,        //     Usage Page (Button)
    0x19, 0x01,        //     Usage Minimum (Button 1)
    0x29, 0x03,        //     Usage Maximum (Button 3)
    0x15, 0x00,        //     Logical Minimum (0)
    0x25, 0x01,        //     Logical Maximum (1)
    0x95, 0x03,        //     Report Count (3)
    0x75, 0x01,        //     Report Size (1)
    0x81, 0x02,        //     Input (Data, Variable, Absolute)
    0x95, 0x01,        //     Report Count (1)
    0x75, 0x05,        //     Report Size (5)
    0x81, 0x03,        //     Input (Constant, Variable, Absolute)
    0x05, 0x01,        //     Usage Page (Generic Desktop)
    0x09, 0x30,        //     Usage (X)
    0x09, 0x31,        //     Usage (Y)
    0x15, 0x81,        //     Logical Minimum (-127)
    0x25, 0x7F,        //     Logical Maximum (127)
    0x75, 0x08,        //     Report Size (8)
    0x95, 0x02,        //     Report Count (2)
    0x81, 0x06,        //     Input (Data, Variable, Relative)
    0x09, 0x38,        //     Usage (Wheel)
    0x15, 0x81,        //     Logical Minimum (-127)
    0x25, 0x7F,        //     Logical Maximum (127)
    0x75, 0x08,        //     Report Size (8)
    0x95, 0x01,        //     Report Count (1)
    0x81, 0x06,        //     Input (Data, Variable, Relative)
    0xC0,              //   End Collection
    0xC0,              // End Collection
};

class HidReportDiagnostics final : public BLECharacteristicCallbacks {
 public:
  explicit HidReportDiagnostics(const char* label) : _label(label) {}

  void onSubscribe(BLECharacteristic*, ble_gap_conn_desc*,
                   uint16_t subValue) override {
    _subscribed.store(subValue != 0, std::memory_order_release);
    Serial.printf("[hid] %s notifications %s\n", _label,
                  subValue != 0 ? "enabled" : "disabled");
  }

  void onStatus(BLECharacteristic*, Status status, uint32_t code) override {
    if (status == SUCCESS_NOTIFY) {
      if (!_loggedNotification.exchange(true, std::memory_order_acq_rel)) {
        Serial.printf("[hid] %s reports are being sent\n", _label);
      }
      return;
    }
    Serial.printf("[hid] %s report status=%u code=%lu\n", _label,
                  static_cast<unsigned>(status),
                  static_cast<unsigned long>(code));
  }

  bool isSubscribed() const {
    return _subscribed.load(std::memory_order_acquire);
  }

  void resetSubscription() {
    _subscribed.store(false, std::memory_order_release);
    _loggedNotification.store(false, std::memory_order_release);
  }

 private:
  const char* _label;
  std::atomic<bool> _subscribed{false};
  std::atomic<bool> _loggedNotification{false};
};

#if defined(CONFIG_BLUEDROID_ENABLED)
class CccdDiagnostics final : public BLEDescriptorCallbacks {
 public:
  explicit CccdDiagnostics(const char* label) : _label(label) {}

  void onWrite(BLEDescriptor* descriptor) override {
    auto* cccd = static_cast<BLE2902*>(descriptor);
    Serial.printf("[hid] %s CCCD notifications %s\n", _label,
                  cccd->getNotifications() ? "enabled" : "disabled");
  }

 private:
  const char* _label;
};
#endif

HidReportDiagnostics kKeyboardDiagnostics("keyboard");
HidReportDiagnostics kMouseDiagnostics("mouse");
#if defined(CONFIG_BLUEDROID_ENABLED)
CccdDiagnostics kKeyboardCccdDiagnostics("keyboard");
CccdDiagnostics kMouseCccdDiagnostics("mouse");
#endif

bool notificationsEnabled(BLECharacteristic* characteristic,
                          const HidReportDiagnostics& diagnostics) {
#if defined(CONFIG_BLUEDROID_ENABLED)
  if (characteristic != nullptr) {
    auto* descriptor = static_cast<BLE2902*>(
        characteristic->getDescriptorByUUID(
            BLEUUID(static_cast<uint16_t>(0x2902))));
    if (descriptor != nullptr) {
      return descriptor->getNotifications();
    }
  }
#else
  (void)characteristic;
#endif
  return diagnostics.isSubscribed();
}

class ServerDiagnostics final : public BLEServerCallbacks {
 public:
  void onDisconnect(BLEServer*) override {
    kKeyboardDiagnostics.resetSubscription();
    kMouseDiagnostics.resetSubscription();
    resetBleMicSubscription();
  }
};

class OtaCredentialsCallbacks final : public BLECharacteristicCallbacks {
 public:
  explicit OtaCredentialsCallbacks(
      BleHidDevice::OtaCredentialsProvider* provider)
      : _provider(provider) {}

  void onRead(BLECharacteristic* characteristic) override {
    char credentials[80] = {};
    if (_provider == nullptr || !*_provider ||
        !(*_provider)(credentials, sizeof(credentials))) {
      characteristic->setValue("WAIT_WIFI");
      return;
    }
    characteristic->setValue(credentials);
    memset(credentials, 0, sizeof(credentials));
  }

 private:
  BleHidDevice::OtaCredentialsProvider* _provider;
};

ServerDiagnostics kServerDiagnostics;

}  // namespace

bool BleHidDevice::begin(BleMicAudio& micAudio,
                         OtaCredentialsProvider otaCredentialsProvider) {
  _otaCredentials = std::move(otaCredentialsProvider);
  BLEDevice::init("StickS3 BLE Controller");
  BLEDevice::setMTU(185);

  _server = BLEDevice::createServer();
  if (_server == nullptr) {
    Serial.println("[controller] BLE server creation failed");
    _errorMessage = "BLE INIT FAILED";
    return false;
  }
  _server->setCallbacks(&kServerDiagnostics);

  _hid = new BLEHIDDevice(_server);
  if (_hid == nullptr) {
    Serial.println("[controller] BLE HID device creation failed");
    _errorMessage = "BLE HID INIT FAILED";
    return false;
  }

  // Both HID input reports use UUID 0x2a4d; the controller build patches
  // NimBLE's service map to retain and register both characteristic instances.
  _keyboardInputReport = _hid->inputReport(1);
  _mouseInputReport = _hid->inputReport(2);
  if (_keyboardInputReport == nullptr || _mouseInputReport == nullptr) {
    Serial.println("[controller] BLE HID report creation failed");
    _errorMessage = "HID REPORT FAILED";
    return false;
  }
  _keyboardInputReport->setCallbacks(&kKeyboardDiagnostics);
  _mouseInputReport->setCallbacks(&kMouseDiagnostics);
#if defined(CONFIG_BLUEDROID_ENABLED)
  BLEDescriptor* keyboardCccd = _keyboardInputReport->getDescriptorByUUID(
      BLEUUID(static_cast<uint16_t>(0x2902)));
  if (keyboardCccd != nullptr) {
    keyboardCccd->setCallbacks(&kKeyboardCccdDiagnostics);
  }
  BLEDescriptor* mouseCccd = _mouseInputReport->getDescriptorByUUID(
      BLEUUID(static_cast<uint16_t>(0x2902)));
  if (mouseCccd != nullptr) {
    mouseCccd->setCallbacks(&kMouseCccdDiagnostics);
  }
#endif

  if (_hid->manufacturer() == nullptr) {
    Serial.println(
        "[controller] BLE manufacturer characteristic creation failed");
    _errorMessage = "BLE HID INIT FAILED";
    return false;
  }
  _hid->manufacturer("M5Stack");
  BLECharacteristic* pnpId =
      _hid->deviceInfo()->getCharacteristic(
          BLEUUID(static_cast<uint16_t>(0x2A50)));
  if (pnpId == nullptr) {
    Serial.println("[controller] BLE PnP ID characteristic is missing");
    _errorMessage = "BLE HID INIT FAILED";
    return false;
  }
  const uint8_t pnpValue[] = {0x02, 0x3A, 0x30, 0x01, 0x00, 0x00, 0x01};
  pnpId->setValue(pnpValue, sizeof(pnpValue));
  _hid->hidInfo(0x00, 0x01);
  uint8_t combinedReportMap[sizeof(kKeyboardReportMap) +
                            sizeof(kMouseReportMap)];
  memcpy(combinedReportMap, kKeyboardReportMap, sizeof(kKeyboardReportMap));
  memcpy(combinedReportMap + sizeof(kKeyboardReportMap), kMouseReportMap,
         sizeof(kMouseReportMap));
  _hid->reportMap(combinedReportMap, sizeof(combinedReportMap));
  _hid->setBatteryLevel(100);

  if (!micAudio.begin(_server)) {
    Serial.println("[controller] BLE microphone service initialization failed");
    _errorMessage = "BLE MIC INIT FAILED";
    return false;
  }

  BLEService* otaService = _server->createService(BLEUUID(kOtaServiceUuid), 4);
  if (otaService == nullptr) {
    Serial.println("[controller] BLE OTA service creation failed");
    _errorMessage = "BLE OTA INIT FAILED";
    return false;
  }
  BLECharacteristic* otaCredentials = otaService->createCharacteristic(
      BLEUUID(kOtaCredentialsUuid), BLECharacteristic::PROPERTY_READ);
  if (otaCredentials == nullptr) {
    Serial.println("[controller] BLE OTA credential creation failed");
    _errorMessage = "BLE OTA INIT FAILED";
    return false;
  }
  otaCredentials->setAccessPermissions(ESP_GATT_PERM_READ_ENCRYPTED);
  OtaCredentialsCallbacks* otaCallbacks =
      new (std::nothrow) OtaCredentialsCallbacks(&_otaCredentials);
  if (otaCallbacks == nullptr) {
    Serial.println("[controller] BLE OTA callback allocation failed");
    _errorMessage = "BLE OTA INIT FAILED";
    return false;
  }
  otaCredentials->setCallbacks(otaCallbacks);
  otaService->start();

  const uint8_t idleKeyboardReport[8] = {};
  const uint8_t idleMouseReport[4] = {};
  _keyboardInputReport->setValue(idleKeyboardReport,
                                 sizeof(idleKeyboardReport));
  _mouseInputReport->setValue(idleMouseReport, sizeof(idleMouseReport));
  _hid->startServices();

  static BLESecurity security;
  security.setAuthenticationMode(ESP_LE_AUTH_BOND);
  security.setCapability(ESP_IO_CAP_NONE);
  security.setInitEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);

  BLEAdvertising* advertising = _server->getAdvertising();
  advertising->setAppearance(GENERIC_HID);
  advertising->addServiceUUID(_hid->hidService()->getUUID());
  advertising->start();

  Serial.println(
      "[controller] BLE HID advertising; pair with StickS3 BLE Controller");
  return true;
}

bool BleHidDevice::updateConnection(uint32_t now) {
  if (now - _lastConnectionCheckAt < 250) {
    return false;
  }
  _lastConnectionCheckAt = now;
  const bool connected = _server->getConnectedCount() > 0;
  if (connected == _connected) {
    return false;
  }

  _connected = connected;
  if (_connected) {
    Serial.println("[controller] BLE host connected");
  } else {
    kKeyboardDiagnostics.resetSubscription();
    kMouseDiagnostics.resetSubscription();
    Serial.println("[controller] BLE host disconnected; advertising again");
    _server->startAdvertising();
  }
  return true;
}

bool BleHidDevice::reportsReady() const {
  return _connected &&
         notificationsEnabled(_keyboardInputReport, kKeyboardDiagnostics) &&
         notificationsEnabled(_mouseInputReport, kMouseDiagnostics);
}

void BleHidDevice::sendKey(uint8_t key) {
  sendKeyboardReport(0, key);
  delay(8);
  sendKeyboardReport(0, 0);
}

void BleHidDevice::sendHotkey(uint8_t key) {
  sendKeyboardReport(kModifierLeftControl, key);
  delay(8);
  sendKeyboardReport(kModifierLeftControl, 0);
  delay(8);
  sendKeyboardReport(0, 0);
}

void BleHidDevice::sendKeyboardReport(uint8_t modifier, uint8_t key) {
  if (_keyboardInputReport == nullptr ||
      !notificationsEnabled(_keyboardInputReport, kKeyboardDiagnostics)) {
    return;
  }
  const uint8_t report[8] = {modifier, 0, key, 0, 0, 0, 0, 0};
  _keyboardInputReport->setValue(report, sizeof(report));
  _keyboardInputReport->notify();
}

void BleHidDevice::sendMouseReport(uint8_t buttons, int8_t dx, int8_t dy,
                                   int8_t wheel) {
  if (_mouseInputReport == nullptr ||
      !notificationsEnabled(_mouseInputReport, kMouseDiagnostics)) {
    return;
  }
  const uint8_t report[4] = {buttons, static_cast<uint8_t>(dx),
                             static_cast<uint8_t>(dy),
                             static_cast<uint8_t>(wheel)};
  _mouseInputReport->setValue(report, sizeof(report));
  _mouseInputReport->notify();
}

}  // namespace controller
