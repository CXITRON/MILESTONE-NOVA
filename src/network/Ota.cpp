#include "Ota.h"
#include "../logging/Log.h"
#include <ArduinoOTA.h>
#include <WiFi.h>
#include <cstring>
#include <esp_ota_ops.h>
#include <mbedtls/pk.h>
namespace nova {
void Ota::beginBootCheck() {
  esp_ota_img_states_t state;
  bootPending_ = esp_ota_get_state_partition(esp_ota_get_running_partition(), &state) == ESP_OK &&
                 state == ESP_OTA_IMG_PENDING_VERIFY;
  bootStarted_ = millis();
  bootLoops_ = 0;
  if (bootPending_)
    log("OTA", "pending image; 60-second runtime confirmation required");
}
void Ota::checkBoot(uint32_t now) {
  if (!bootPending_)
    return;
  ++bootLoops_;
  if (now - bootStarted_ < 60000 || bootLoops_ < 500 || ESP.getFreeHeap() < 16384)
    return;
  if (esp_ota_mark_app_valid_cancel_rollback() == ESP_OK) {
    bootPending_ = false;
    log("OTA", "runtime confirmed; rollback cancelled");
  } else {
    bootStarted_ = now;
    log("OTA", "confirmation failed; retry in 60 seconds");
  }
}
bool Ota::begin(const Secrets &secrets) {
  if (strlen(secrets.otaPassword) < 12 || !secrets.otaPublicKey[0])
    return false;
  mbedtls_pk_context key;
  mbedtls_pk_init(&key);
  const bool valid =
      mbedtls_pk_parse_public_key(&key, reinterpret_cast<const uint8_t *>(secrets.otaPublicKey),
                                  strlen(secrets.otaPublicKey) + 1) == 0 &&
      mbedtls_pk_can_do(&key, MBEDTLS_PK_RSA) && mbedtls_pk_get_bitlen(&key) >= 2048;
  mbedtls_pk_free(&key);
  if (!valid)
    return false;
  secrets_ = secrets;
  state_ = State::Closed;
  configured_ = xTaskCreate(task, "nova-ota", 12288, this, 1, nullptr) == pdPASS;
  if (!configured_)
    state_ = State::Unconfigured;
  return configured_;
}
bool Ota::open() {
  if (bootPending_ || !configured_ || WiFi.status() != WL_CONNECTED || state() == State::Success)
    return false;
  if (!quiescent_)
    return false;
  quiescent_ = false;
  requested_ = true;
  return true;
}
void Ota::close() { requested_ = false; }
void Ota::task(void *self) { static_cast<Ota *>(self)->run(); }
void Ota::run() {
  uint32_t transferStarted = 0;
  UpdaterRSAVerifier signature(reinterpret_cast<const uint8_t *>(secrets_.otaPublicKey),
                               strlen(secrets_.otaPublicKey) + 1, HASH_SHA256);
  ArduinoOTA.setHostname("milestone-nova");
  ArduinoOTA.setPassword(secrets_.otaPassword);
  ArduinoOTA.setSignature(&signature);
  ArduinoOTA.setRebootOnSuccess(false);
  ArduinoOTA.setTimeout(1500);
  ArduinoOTA.onStart([this, &transferStarted] {
    if (!requested_ || ArduinoOTA.getCommand() != U_FLASH) {
      Update.abort();
      state_ = State::Failed;
      error_ = -1;
      requested_ = false;
      return;
    }
    state_ = State::Receiving;
    transferStarted = millis();
    progress_ = 0;
  });
  ArduinoOTA.onProgress([this, &transferStarted](unsigned done, unsigned total) {
    if (!requested_ || millis() - transferStarted > 120000) {
      Update.abort();
      state_ = State::Failed;
      requested_ = false;
      error_ = -2;
      return;
    }
    if (total)
      progress_ = uint64_t(done) * 100 / total;
  });
  ArduinoOTA.onEnd([this] {
    state_ = State::Success;
    requested_ = false;
  });
  ArduinoOTA.onError([this](ota_error_t e) {
    error_ = e;
    state_ = State::Failed;
    requested_ = false;
  });
  bool listening = false;
  uint32_t opened = 0;
  for (;;) {
    if (requested_ && !listening) {
      ArduinoOTA.begin();
      listening = true;
      opened = millis();
      state_ = State::Listening;
      error_ = 0;
    }
    if (listening) {
      if (!requested_ || WiFi.status() != WL_CONNECTED || millis() - opened > 600000) {
        ArduinoOTA.end();
        listening = false;
        requested_ = false;
        if (state() != State::Failed && state() != State::Success)
          state_ = State::Closed;
        quiescent_ = true;
      } else
        ArduinoOTA.handle();
    } else if (!requested_)
      quiescent_ = true;
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}
const char *Ota::status() const {
  switch (state()) {
  case State::Unconfigured:
    return "Set password + RSA key";
  case State::Closed:
    return "Closed";
  case State::Listening:
    return "Ready (10 minutes)";
  case State::Receiving:
    return "Receiving signed image";
  case State::Success:
    return "Verified; restarting";
  case State::Failed:
    return "Failed; current app kept";
  }
  return "Unknown";
}
} // namespace nova
