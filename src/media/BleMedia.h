#pragma once
#include "AmsDecoder.h"
#include "HelperProtocol.h"
#include <BLECharacteristic.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <atomic>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <host/ble_gap.h>
#include <host/ble_gatt.h>
namespace nova {
class BleMedia final : private BLECharacteristicCallbacks {
public:
  bool begin();
  void tick(MediaSession &session, uint32_t now);
  void suspend(bool suspend);
  bool control(uint8_t command);
  bool canControl(uint8_t command) const;
  int controlResult() const { return commandResult_; }
  bool connected() const { return connection_ != BLE_HS_CONN_HANDLE_NONE; }
  bool ready() const { return stage_ == Stage::Ready || helperActive_; }
  const char *status() const;

private:
  enum class Stage {
    Off,
    Advertising,
    Securing,
    Service,
    Characteristics,
    Descriptors,
    Subscribe,
    SubscribeRemote,
    Track,
    Player,
    Queue,
    Ready,
    Helper,
    Failed
  };
  enum class Kind : uint8_t {
    Connect,
    Disconnect,
    Notification,
    Service,
    Characteristic,
    Descriptor,
    Done,
    AttributeSelected,
    AttributeData,
    AttributeDone,
    CommandDone,
    Helper
  };
  struct Event {
    Kind kind{};
    uint16_t connection = 0, handle = 0, end = 0, length = 0;
    uint32_t token = 0, at = 0;
    int status = 0;
    uint8_t bytes[246]{};
  };
  void enqueue(const Event &event);
  void discover(uint32_t now);
  void process(const Event &event, MediaSession &session, uint32_t now);
  void advance(uint32_t now);
  void onWrite(BLECharacteristic *characteristic, ble_gap_conn_desc *desc) override;
  static int gap(ble_gap_event *event, void *);
  static int service(uint16_t, const ble_gatt_error *, const ble_gatt_svc *, void *);
  static int characteristic(uint16_t, const ble_gatt_error *, const ble_gatt_chr *, void *);
  static int descriptor(uint16_t, const ble_gatt_error *, uint16_t, const ble_gatt_dsc *, void *);
  static int written(uint16_t, const ble_gatt_error *, ble_gatt_attr *, void *);
  static int attributeSelected(uint16_t, const ble_gatt_error *, ble_gatt_attr *, void *);
  static int attributeRead(uint16_t, const ble_gatt_error *, ble_gatt_attr *, void *);
  static int commandWritten(uint16_t, const ble_gatt_error *, ble_gatt_attr *, void *);
  void clearPeer();
  void readAttribute(uint32_t now);
  static BleMedia *instance_;
  QueueHandle_t queue_ = nullptr;
  BLEAdvertising *advertising_ = nullptr;
  BLECharacteristic *commands_ = nullptr;
  std::atomic<bool> overflow_{false};
  uint16_t connection_ = BLE_HS_CONN_HANDLE_NONE, serviceStart_ = 0, serviceEnd_ = 0, entity_ = 0,
           cccd_ = 0, remote_ = 0, remoteCccd_ = 0, attribute_ = 0;
  uint32_t token_ = 0, since_ = 0, retry_ = 0, lastDiscover_ = 0;
  Stage stage_ = Stage::Off;
  bool initialized_ = false, enabled_ = true, helperActive_ = false;
  uint32_t commandMask_ = 0, revision_ = 0, readRevision_ = 0, extraToken_ = 0, extraSince_ = 0;
  uint8_t pendingAttributes_ = 0, readingAttribute_ = 0;
  uint16_t attributeBytes_ = 0;
  uint8_t attributeText_[243]{};
  bool extraBusy_ = false;
  int commandResult_ = 0;
  AmsDecoder ams_;
  HelperDecoder helper_;
};
} // namespace nova
