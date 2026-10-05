#pragma once
#include "Runtime.h"
#include <cstddef>
#include <string>
inline constexpr int HASH_SHA256 = 0, U_FLASH = 0, U_SPIFFS = 100;
using ota_error_t = int;
struct UpdaterRSAVerifier {
  UpdaterRSAVerifier(const uint8_t *, size_t, int) {}
};
struct TestUpdate {
  unsigned aborted = 0;
  void abort() { ++aborted; }
};
inline TestUpdate Update;
struct TestArduinoOTA {
  std::function<void()> start, complete, handleAction;
  std::function<void(unsigned, unsigned)> progress;
  std::function<void(int)> error;
  std::string hostname, password;
  bool signature = false, reboot = true;
  unsigned starts = 0, stops = 0;
  int timeout = 0, command = U_FLASH;
  void setHostname(const char *value) { hostname = value; }
  void setPassword(const char *value) { password = value; }
  void setSignature(UpdaterRSAVerifier *) { signature = true; }
  void setRebootOnSuccess(bool value) { reboot = value; }
  void setTimeout(int value) { timeout = value; }
  void onStart(std::function<void()> fn) { start = fn; }
  void onEnd(std::function<void()> fn) { complete = fn; }
  void onProgress(std::function<void(unsigned, unsigned)> fn) { progress = fn; }
  void onError(std::function<void(int)> fn) { error = fn; }
  int getCommand() { return command; }
  void begin() { ++starts; }
  void end() { ++stops; }
  void handle() { if (handleAction) handleAction(); }
};
inline TestArduinoOTA ArduinoOTA;
