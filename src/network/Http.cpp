#include "Http.h"
#include <cstring>
#include <esp_crt_bundle.h>
namespace nova {
Http::~Http() {
  if (client_) {
    esp_http_client_close(client_);
    esp_http_client_cleanup(client_);
  }
}
bool Http::open(const char *url, const char *form, unsigned timeoutMs) {
  if (client_ || !url || strncmp(url, "https://", 8))
    return false;
  esp_http_client_config_t config{};
  config.url = url;
  config.timeout_ms = timeoutMs;
  config.crt_bundle_attach = esp_crt_bundle_attach;
  config.disable_auto_redirect = true;
  config.buffer_size = 4096;
  config.buffer_size_tx = 2048;
  config.user_agent = "MILESTONE-NOVA/1";
  client_ = esp_http_client_init(&config);
  if (!client_)
    return false;
  const size_t n = form ? strlen(form) : 0;
  if (form) {
    esp_http_client_set_method(client_, HTTP_METHOD_POST);
    esp_http_client_set_header(client_, "Content-Type", "application/x-www-form-urlencoded");
  }
  if (esp_http_client_open(client_, n) != ESP_OK)
    return false;
  size_t at = 0;
  while (at < n) {
    const int written = esp_http_client_write(client_, form + at, n - at);
    if (written <= 0)
      return false;
    at += written;
  }
  length_ = esp_http_client_fetch_headers(client_);
  status_ = esp_http_client_get_status_code(client_);
  return status_ == 200;
}
int Http::read(uint8_t *p, size_t n) {
  return client_ ? esp_http_client_read(client_, reinterpret_cast<char *>(p), n) : -1;
}
bool Http::complete() const {
  return client_ && esp_http_client_is_complete_data_received(client_);
}
} // namespace nova
