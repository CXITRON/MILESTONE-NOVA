#pragma once
#include <cstddef>
#include <cstdint>
#include <esp_http_client.h>
#include <atomic>
#include <memory>
namespace nova {
// Background task only. TLS uses the ESP-IDF trust bundle; redirects require an explicit policy.
class Http {
public:
  ~Http();
  using RedirectPolicy = bool (*)(const char *url);
  bool open(const char *url, const char *form = nullptr, unsigned timeoutMs = 4000,
            RedirectPolicy redirect = nullptr);
  int read(uint8_t *bytes, size_t capacity);
  int status() const { return status_; }
  int64_t length() const { return length_; }
  bool complete() const;
  // Outcome of the latest open() on any task: HTTP status, or -esp_err_t when no response came.
  static int lastResult() { return last_; }

private:
  static inline std::atomic<int> last_{0};
  static esp_err_t event(esp_http_client_event_t *event);
  esp_http_client_handle_t client_ = nullptr;
  std::unique_ptr<char[]> location_;
  bool invalidLocation_ = false;
  int status_ = 0;
  int64_t length_ = -1;
};
} // namespace nova
