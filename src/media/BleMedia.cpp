#include "BleMedia.h"
#include "../core/Text.h"
#include "../logging/Log.h"
#include <BLESecurity.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
namespace nova {
namespace {
const ble_uuid128_t amsService = BLE_UUID128_INIT(0xdc, 0xf8, 0x55, 0xad, 0x02, 0xc5, 0xf4, 0x8e,
                                                  0x3a, 0x43, 0x36, 0x0f, 0x2b, 0x50, 0xd3, 0x89);
const ble_uuid128_t amsEntity = BLE_UUID128_INIT(0x02, 0xc1, 0x96, 0xba, 0x92, 0xbb, 0x0c, 0x9a,
                                                 0x1f, 0x41, 0x8d, 0x80, 0xce, 0xab, 0x7c, 0x2f);
const ble_uuid128_t amsAttribute = BLE_UUID128_INIT(0xD7, 0xD5, 0xBB, 0x0B, 0x87, 0xA3, 0xAB, 0xA6,
                                                    0xD8, 0x46, 0xAB, 0x23, 0x8C, 0xF3, 0xB2, 0xC6);
const ble_uuid128_t amsRemote = BLE_UUID128_INIT(0xc2, 0x51, 0xca, 0xf7, 0x56, 0x0e, 0xdf, 0xb8,
                                                 0x8a, 0x4a, 0xb1, 0x57, 0xd8, 0x81, 0x3c, 0x9b);
} // namespace
BleMedia *BleMedia::instance_ = nullptr;
void BleMedia::enqueue(const Event &e) {
  if (queue_ && xQueueSend(queue_, &e, 0) != pdTRUE)
    overflow_ = true;
}
int BleMedia::gap(ble_gap_event *g, void *) {
  if (!instance_)
    return 0;
  Event e;
  e.at = millis();
  if (g->type == BLE_GAP_EVENT_CONNECT) {
    if (g->connect.status)
      return 0;
    e.kind = Kind::Connect;
    e.connection = g->connect.conn_handle;
  } else if (g->type == BLE_GAP_EVENT_DISCONNECT) {
    e.kind = Kind::Disconnect;
    e.connection = g->disconnect.conn.conn_handle;
  } else if (g->type == BLE_GAP_EVENT_NOTIFY_RX) {
    e.kind = Kind::Notification;
    e.connection = g->notify_rx.conn_handle;
    e.handle = g->notify_rx.attr_handle;
    const size_t n = OS_MBUF_PKTLEN(g->notify_rx.om);
    if (n > sizeof(e.bytes))
      return 0;
    e.length = n;
    if (os_mbuf_copydata(g->notify_rx.om, 0, n, e.bytes) != 0)
      return 0;
  } else
    return 0;
  instance_->enqueue(e);
  return 0;
}
int BleMedia::service(uint16_t conn, const ble_gatt_error *error, const ble_gatt_svc *svc,
                      void *arg) {
  if (!instance_ || !error)
    return 0;
  Event e;
  e.connection = conn;
  e.status = error->status;
  e.token = reinterpret_cast<uintptr_t>(arg);
  e.kind = Kind::Done;
  if (!error->status && svc) {
    e.kind = Kind::Service;
    e.handle = svc->start_handle;
    e.end = svc->end_handle;
  }
  instance_->enqueue(e);
  return 0;
}
int BleMedia::characteristic(uint16_t conn, const ble_gatt_error *error, const ble_gatt_chr *chr,
                             void *arg) {
  if (!instance_ || !error)
    return 0;
  Event e;
  e.connection = conn;
  e.status = error->status;
  e.token = reinterpret_cast<uintptr_t>(arg);
  e.kind = Kind::Done;
  if (!error->status && chr) {
    e.kind = Kind::Characteristic;
    e.handle = chr->val_handle;
    if (ble_uuid_cmp(&chr->uuid.u, &amsEntity.u) == 0)
      e.bytes[0] = 1;
    else if (ble_uuid_cmp(&chr->uuid.u, &amsRemote.u) == 0)
      e.bytes[0] = 2;
    else if (ble_uuid_cmp(&chr->uuid.u, &amsAttribute.u) == 0)
      e.bytes[0] = 3;
  }
  instance_->enqueue(e);
  return 0;
}
int BleMedia::descriptor(uint16_t conn, const ble_gatt_error *error, uint16_t chr,
                         const ble_gatt_dsc *dsc, void *arg) {
  if (!instance_ || !error)
    return 0;
  Event e;
  e.connection = conn;
  e.status = error->status;
  e.token = reinterpret_cast<uintptr_t>(arg);
  e.kind = Kind::Done;
  if (!error->status && dsc) {
    e.kind = Kind::Descriptor;
    e.handle = dsc->handle;
    e.end = chr;
    e.bytes[0] = ble_uuid_u16(&dsc->uuid.u) == 0x2902;
  }
  instance_->enqueue(e);
  return 0;
}
int BleMedia::written(uint16_t conn, const ble_gatt_error *error, ble_gatt_attr *, void *arg) {
  if (!instance_ || !error)
    return 0;
  Event e;
  e.kind = Kind::Done;
  e.connection = conn;
  e.status = error->status;
  e.token = reinterpret_cast<uintptr_t>(arg);
  instance_->enqueue(e);
  return 0;
}
void BleMedia::onWrite(BLECharacteristic *c, ble_gap_conn_desc *desc) {
  if (!desc || !desc->sec_state.encrypted)
    return;
  const auto value = c->getValue();
  if (value.length() > 246)
    return;
  Event e;
  e.kind = Kind::Helper;
  e.connection = desc->conn_handle;
  e.at = millis();
  e.length = value.length();
  memcpy(e.bytes, value.c_str(), e.length);
  enqueue(e);
}
bool BleMedia::begin() {
  queue_ = xQueueCreate(20, sizeof(Event));
  if (!queue_)
    return false;
  instance_ = this;
  if (!BLEDevice::init("NOVA") || BLEDevice::getBLEStack() != BLEStack::NIMBLE) {
    stage_ = Stage::Failed;
    return false;
  }
  BLEDevice::setMTU(250);
  BLESecurity::setAuthenticationMode(true, false, true);
  BLESecurity::setCapability(ESP_IO_CAP_NONE);
  BLESecurity::setForceAuthentication(true);
  BLEDevice::setCustomGapHandler(gap);
  auto *server = BLEDevice::createServer();
  if (!server)
    return false;
  auto *svc = server->createService(helperService);
  if (!svc)
    return false;
  auto *input = svc->createCharacteristic(helperInput, BLECharacteristic::PROPERTY_WRITE |
                                                           BLECharacteristic::PROPERTY_WRITE_ENC);
  commands_ = svc->createCharacteristic(helperControl, BLECharacteristic::PROPERTY_NOTIFY |
                                                           BLECharacteristic::PROPERTY_READ |
                                                           BLECharacteristic::PROPERTY_READ_ENC);
  if (!input || !commands_)
    return false;
  input->setCallbacks(this);
  svc->start();
  server->start();
  advertising_ = BLEDevice::getAdvertising();
  if (!advertising_)
    return false;
  BLEAdvertisementData adv, scan;
  adv.setFlags(0x06);
  adv.setName("NOVA");
  char solicitation[18];
  solicitation[0] = 17;
  solicitation[1] = 0x15;
  memcpy(solicitation + 2, amsService.value, 16);
  adv.addData(solicitation, sizeof(solicitation));
  scan.setName("NOVA");
  scan.setCompleteServices(BLEUUID(helperService));
  if (!advertising_->setAdvertisementData(adv) || !advertising_->setScanResponseData(scan))
    return false;
  advertising_->setScanResponse(true);
  advertising_->setAdvertisementType(BLE_GAP_CONN_MODE_UND);
  advertising_->setMinInterval(256);
  advertising_->setMaxInterval(384);
  initialized_ = true;
  stage_ = Stage::Advertising;
  retry_ = millis() - 5000;
  return true;
}
void BleMedia::discover(uint32_t now) {
  clearPeer();
  stage_ = Stage::Service;
  since_ = lastDiscover_ = now;
  const auto arg = reinterpret_cast<void *>(uintptr_t(++token_));
  if (ble_gattc_disc_svc_by_uuid(connection_, &amsService.u, service, arg) != 0) {
    stage_ = Stage::Helper;
    retry_ = now;
  }
}
void BleMedia::advance(uint32_t now) {
  since_ = now;
  const auto arg = reinterpret_cast<void *>(uintptr_t(++token_));
  int rc = -1;
  switch (stage_) {
  case Stage::Service:
    if (serviceStart_) {
      stage_ = Stage::Characteristics;
      rc = ble_gattc_disc_all_chrs(connection_, serviceStart_, serviceEnd_, characteristic, arg);
    }
    break;
  case Stage::Characteristics:
    if (entity_) {
      stage_ = Stage::Descriptors;
      rc = ble_gattc_disc_all_dscs(connection_, serviceStart_, serviceEnd_, descriptor, arg);
    }
    break;
  case Stage::Descriptors:
    if (cccd_) {
      const uint8_t enable[]{1, 0};
      stage_ = Stage::Subscribe;
      rc = ble_gattc_write_flat(connection_, cccd_, enable, 2, written, arg);
    }
    break;
  case Stage::Subscribe: {
    if (remote_ && remoteCccd_) {
      const uint8_t enable[]{1, 0};
      stage_ = Stage::SubscribeRemote;
      rc = ble_gattc_write_flat(connection_, remoteCccd_, enable, 2, written, arg);
      break;
    }
    stage_ = Stage::SubscribeRemote;
    advance(now);
    return;
  }
  case Stage::SubscribeRemote: {
    const uint8_t attrs[]{2, 0, 1, 2, 3};
    stage_ = Stage::Track;
    rc = ble_gattc_write_flat(connection_, entity_, attrs, sizeof(attrs), written, arg);
    break;
  }
  case Stage::Track: {
    const uint8_t attrs[]{0, 0, 1, 2};
    stage_ = Stage::Player;
    rc = ble_gattc_write_flat(connection_, entity_, attrs, sizeof(attrs), written, arg);
    break;
  }
  case Stage::Player: {
    const uint8_t attrs[]{1, 0, 1, 2, 3};
    stage_ = Stage::Queue;
    rc = ble_gattc_write_flat(connection_, entity_, attrs, sizeof(attrs), written, arg);
    break;
  }
  case Stage::Queue:
    stage_ = Stage::Ready;
    log("BLE", "AMS subscribed");
    return;
  default:
    return;
  }
  if (rc) {
    stage_ = Stage::Helper;
    retry_ = now;
  }
}
void BleMedia::process(const Event &e, MediaSession &session, uint32_t now) {
  if (e.kind == Kind::Connect) {
    if (!enabled_ || connected()) {
      ble_gap_terminate(e.connection, BLE_ERR_REM_USER_CONN_TERM);
      return;
    }
    connection_ = e.connection;
    clearPeer();
    stage_ = Stage::Securing;
    since_ = now;
    helperActive_ = false;
    ams_.reset();
    helper_.reset();
    ble_gap_security_initiate(connection_);
    return;
  }
  if (e.connection != connection_)
    return;
  if (e.kind == Kind::Disconnect) {
    connection_ = BLE_HS_CONN_HANDLE_NONE;
    clearPeer();
    ++token_;
    stage_ = enabled_ ? Stage::Advertising : Stage::Off;
    helperActive_ = false;
    ams_.reset();
    helper_.reset();
    session.disconnect(now);
    retry_ = now - 5000;
    return;
  }
  if (e.kind == Kind::Helper) {
    if (helper_.accept(e.bytes, e.length, now)) {
      helperActive_ = true;
      session.replace(helper_.track(), now);
      session.synchronize(helper_.position(), helper_.playing(), now);
    }
    return;
  }
  if (e.kind == Kind::Notification) {
    if (e.handle == remote_) {
      commandMask_ = 0;
      for (unsigned i = 0; i < e.length; ++i)
        if (e.bytes[i] < 14)
          commandMask_ |= 1U << e.bytes[i];
    }
    if (e.handle == entity_ && !helperActive_) {
      ams_.accept(e.bytes, e.length, e.at, session);
      if (e.length >= 3 && e.bytes[0] == 2 && e.bytes[1] < 4) {
        ++revision_;
        if (e.bytes[2] & 1)
          pendingAttributes_ |= 1U << e.bytes[1];
      }
    }
    return;
  }
  if (e.kind == Kind::AttributeSelected || e.kind == Kind::AttributeData ||
      e.kind == Kind::AttributeDone || e.kind == Kind::CommandDone) {
    if (e.token != extraToken_)
      return;
    if (e.kind == Kind::CommandDone) {
      commandResult_ = e.status ? -3 : 2;
      extraBusy_ = false;
      return;
    }
    if (e.kind == Kind::AttributeSelected) {
      if (e.status || ble_gattc_read_long(connection_, attribute_, 0, attributeRead,
                                          reinterpret_cast<void *>(uintptr_t(extraToken_))))
        extraBusy_ = false;
      return;
    }
    if (e.kind == Kind::AttributeData) {
      if (e.end < 240) {
        const size_t n = std::min<size_t>(e.length, 240 - e.end);
        memcpy(attributeText_ + 3 + e.end, e.bytes, n);
        attributeBytes_ = std::max<uint16_t>(attributeBytes_, e.end + n);
      }
      return;
    }
    if (!e.status || e.status == BLE_HS_EDONE) {
      if (readRevision_ == revision_) {
        attributeText_[0] = 2;
        attributeText_[1] = readingAttribute_;
        attributeText_[2] = 0;
        ams_.accept(attributeText_, attributeBytes_ + 3, now, session);
        pendingAttributes_ &= ~(1U << readingAttribute_);
      }
    } else
      pendingAttributes_ &= ~(1U << readingAttribute_);
    extraBusy_ = false;
    return;
  }
  if (e.token != token_)
    return;
  if (e.kind == Kind::Service) {
    serviceStart_ = e.handle;
    serviceEnd_ = e.end;
  } else if (e.kind == Kind::Characteristic) {
    if (e.bytes[0] == 1)
      entity_ = e.handle;
    else if (e.bytes[0] == 2)
      remote_ = e.handle;
    else if (e.bytes[0] == 3)
      attribute_ = e.handle;
  } else if (e.kind == Kind::Descriptor) {
    if (e.bytes[0] && e.end == entity_)
      cccd_ = e.handle;
    if (e.bytes[0] && e.end == remote_)
      remoteCccd_ = e.handle;
  } else if (e.kind == Kind::Done) {
    if (!e.status || e.status == BLE_HS_EDONE)
      advance(now);
    else {
      stage_ = Stage::Helper;
      retry_ = now;
    }
  }
}
void BleMedia::tick(MediaSession &session, uint32_t now) {
  if (!initialized_)
    return;
  if (overflow_.exchange(false)) {
    xQueueReset(queue_);
    helper_.reset();
    session.disconnect(now);
    if (connected())
      ble_gap_terminate(connection_, BLE_ERR_REM_USER_CONN_TERM);
    log("BLE", "queue overflow; resynchronizing");
  }
  Event event;
  for (unsigned n = 0; n < 20 && xQueueReceive(queue_, &event, 0) == pdTRUE; ++n)
    process(event, session, now);
  if (connected()) {
    ble_gap_conn_desc peer{};
    if (ble_gap_conn_find(connection_, &peer) != 0) {
      Event disconnected;
      disconnected.kind = Kind::Disconnect;
      disconnected.connection = connection_;
      process(disconnected, session, millis());
    }
  }
  // Use a fresh clock after callback processing; event anchors can be newer than tick entry.
  now = millis();
  if (!enabled_)
    return;
  if (!connected()) {
    if (now - retry_ >= 5000) {
      retry_ = now;
      if (!advertising_->isAdvertising())
        advertising_->start();
      stage_ = Stage::Advertising;
    }
    return;
  }
  if (stage_ == Stage::Securing) {
    ble_gap_conn_desc desc{};
    if (!ble_gap_conn_find(connection_, &desc) && desc.sec_state.encrypted)
      discover(now);
    else if (now - since_ > 15000)
      ble_gap_terminate(connection_, BLE_ERR_REM_USER_CONN_TERM);
  } else if (stage_ >= Stage::Service && stage_ <= Stage::Queue && now - since_ > 10000) {
    ble_gap_terminate(connection_, BLE_ERR_REM_USER_CONN_TERM);
    ++token_;
    stage_ = Stage::Helper;
    retry_ = now;
  } else if (!helperActive_ && !extraBusy_ &&
             ((stage_ == Stage::Helper && now - retry_ >= 30000) ||
              (stage_ == Stage::Ready && now - lastDiscover_ >= 60000)))
    discover(now);
  if (extraBusy_ && now - extraSince_ > 5000) {
    ++extraToken_;
    extraBusy_ = false;
    commandResult_ = -3;
    pendingAttributes_ = 0;
    ble_gap_terminate(connection_, BLE_ERR_REM_USER_CONN_TERM);
    return;
  }
  if (stage_ == Stage::Ready && !extraBusy_ && !helperActive_)
    readAttribute(now);
  if (!helperActive_)
    ams_.tick(now, session);
}
void BleMedia::suspend(bool suspend) {
  if (!initialized_ || enabled_ == !suspend)
    return;
  enabled_ = !suspend;
  if (suspend) {
    advertising_->stop();
    if (connected())
      ble_gap_terminate(connection_, BLE_ERR_REM_USER_CONN_TERM);
    stage_ = Stage::Off;
  } else {
    stage_ = Stage::Advertising;
    retry_ = millis() - 5000;
  }
}
bool BleMedia::canControl(uint8_t command) const {
  return enabled_ && connected() && stage_ == Stage::Ready && !helperActive_ && remote_ &&
         !extraBusy_ && command < 14 && (commandMask_ & (1U << command));
}
bool BleMedia::control(uint8_t command) {
  if (!canControl(command)) {
    commandResult_ = -1;
    return false;
  }
  extraBusy_ = true;
  extraSince_ = millis();
  commandResult_ = 1;
  const int rc = ble_gattc_write_flat(connection_, remote_, &command, 1, commandWritten,
                                      reinterpret_cast<void *>(uintptr_t(++extraToken_)));
  if (rc) {
    extraBusy_ = false;
    commandResult_ = -3;
    return false;
  }
  return true;
}
void BleMedia::clearPeer() {
  serviceStart_ = serviceEnd_ = entity_ = cccd_ = remote_ = remoteCccd_ = attribute_ = 0;
  commandMask_ = pendingAttributes_ = 0;
  extraBusy_ = false;
  ++extraToken_;
  commandResult_ = 0;
}
void BleMedia::readAttribute(uint32_t now) {
  if (!attribute_ || !pendingAttributes_)
    return;
  for (readingAttribute_ = 0; readingAttribute_ < 4; ++readingAttribute_)
    if (pendingAttributes_ & (1U << readingAttribute_))
      break;
  if (readingAttribute_ >= 4)
    return;
  const uint8_t select[]{2, readingAttribute_};
  readRevision_ = revision_;
  attributeBytes_ = 0;
  extraBusy_ = true;
  extraSince_ = now;
  if (ble_gattc_write_flat(connection_, attribute_, select, 2, attributeSelected,
                           reinterpret_cast<void *>(uintptr_t(++extraToken_)))) {
    extraBusy_ = false;
    pendingAttributes_ &= ~(1U << readingAttribute_);
  }
}
int BleMedia::commandWritten(uint16_t conn, const ble_gatt_error *error, ble_gatt_attr *,
                             void *arg) {
  if (!instance_ || !error)
    return 0;
  Event e;
  e.kind = Kind::CommandDone;
  e.connection = conn;
  e.status = error->status;
  e.token = reinterpret_cast<uintptr_t>(arg);
  instance_->enqueue(e);
  return 0;
}
int BleMedia::attributeSelected(uint16_t conn, const ble_gatt_error *error, ble_gatt_attr *,
                                void *arg) {
  if (!instance_ || !error)
    return 0;
  Event e;
  e.kind = Kind::AttributeSelected;
  e.connection = conn;
  e.status = error->status;
  e.token = reinterpret_cast<uintptr_t>(arg);
  instance_->enqueue(e);
  return 0;
}
int BleMedia::attributeRead(uint16_t conn, const ble_gatt_error *error, ble_gatt_attr *attr,
                            void *arg) {
  if (!instance_ || !error)
    return 0;
  Event e;
  e.kind = Kind::AttributeDone;
  e.connection = conn;
  e.status = error->status;
  e.token = reinterpret_cast<uintptr_t>(arg);
  if (!error->status && attr && attr->om) {
    e.kind = Kind::AttributeData;
    e.end = attr->offset;
    e.length = std::min<size_t>(sizeof(e.bytes), OS_MBUF_PKTLEN(attr->om));
    if (os_mbuf_copydata(attr->om, 0, e.length, e.bytes))
      return BLE_HS_EAPP;
  }
  instance_->enqueue(e);
  return 0;
}
const char *BleMedia::status() const {
  if (helperActive_)
    return "Helper ready";
  switch (stage_) {
  case Stage::Off:
    return "Off";
  case Stage::Advertising:
    return advertising_ && advertising_->isAdvertising() ? "Advertising" : "Retrying";
  case Stage::Securing:
    return "Pairing";
  case Stage::Ready:
    return "AMS ready";
  case Stage::Helper:
    return "Awaiting media";
  case Stage::Failed:
    return "Unavailable";
  default:
    return "Discovering AMS";
  }
}
} // namespace nova
