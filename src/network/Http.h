#pragma once
#include <cstddef>
#include <cstdint>
#include <esp_http_client.h>
namespace nova {
// Background task only. TLS uses the ESP-IDF trust bundle; redirects are not followed.
class Http {
public:
  ~Http();
  bool open(const char *url, const char *form = nullptr);
  int read(uint8_t *bytes, size_t capacity);
  int status() const { return status_; }
  int64_t length() const { return length_; }
  bool complete() const;

private:
  esp_http_client_handle_t client_ = nullptr;
  int status_ = 0;
  int64_t length_ = -1;
};
} // namespace nova
