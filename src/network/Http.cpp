#include "Http.h"
#include <cstring>
#include <esp_crt_bundle.h>
#include <new>
#include <strings.h>
namespace nova {
Http::~Http() {
  if (client_) {
    esp_http_client_close(client_);
    esp_http_client_cleanup(client_);
  }
}
esp_err_t Http::event(esp_http_client_event_t *event) {
  auto &self = *static_cast<Http *>(event->user_data);
  if (event->event_id == HTTP_EVENT_ON_HEADER && self.location_ && event->header_key &&
      !strcasecmp(event->header_key, "Location") && event->header_value) {
    if (self.location_[0] || strlen(event->header_value) > 2048)
      self.invalidLocation_ = true;
    else
      strcpy(self.location_.get(), event->header_value);
  }
  return ESP_OK;
}
bool Http::open(const char *url, const char *form, unsigned timeoutMs, RedirectPolicy redirect) {
  if (client_ || !url || strncmp(url, "https://", 8) ||
      (redirect && (form || !redirect(url))))
    return false;
  if (redirect) {
    location_.reset(new (std::nothrow) char[2049]{});
    if (!location_)
      return false;
  }
  esp_http_client_config_t config{};
  config.url = url;
  config.timeout_ms = timeoutMs;
  config.crt_bundle_attach = esp_crt_bundle_attach;
  config.disable_auto_redirect = true;
  // These buffers live in internal RAM (allocations of 4 KiB or less never go to PSRAM). Only
  // the release download follows long CDN redirects; small gateway replies need far less.
  config.buffer_size = redirect ? 4096 : 1024;
  config.buffer_size_tx = redirect ? 2048 : 1024;
  config.user_agent = "MILESTONE-NOVA/1";
  config.event_handler = event;
  config.user_data = this;
  client_ = esp_http_client_init(&config);
  if (!client_)
    return false;
  const size_t n = form ? strlen(form) : 0;
  if (form) {
    esp_http_client_set_method(client_, HTTP_METHOD_POST);
    esp_http_client_set_header(client_, "Content-Type", "application/x-www-form-urlencoded");
  }
  const esp_err_t opened = esp_http_client_open(client_, n);
  if (opened != ESP_OK) {
    last_ = -int(opened);
    int tls = 0, flags = 0;
    esp_http_client_get_and_clear_last_tls_error(client_, &tls, &flags);
    lastTls_ = tls;
    lastErrno_ = esp_http_client_get_errno(client_);
    return false;
  }
  size_t at = 0;
  while (at < n) {
    const int written = esp_http_client_write(client_, form + at, n - at);
    if (written <= 0)
      return false;
    at += written;
  }
  for (unsigned hop = 0;; ++hop) {
    length_ = esp_http_client_fetch_headers(client_);
    status_ = esp_http_client_get_status_code(client_);
    last_ = status_ > 0 ? status_ : -1;
    if (status_ == 200)
      return true;
    const bool moved = status_ == 301 || status_ == 302 || status_ == 303 ||
                       status_ == 307 || status_ == 308;
    if (!redirect || !moved || hop == 4 || invalidLocation_ || !location_[0] ||
        !redirect(location_.get()))
      return false;
    esp_http_client_close(client_);
    if (esp_http_client_set_url(client_, location_.get()) != ESP_OK)
      return false;
    location_[0] = 0;
    if (esp_http_client_open(client_, 0) != ESP_OK)
      return false;
  }
}
int Http::read(uint8_t *p, size_t n) {
  return client_ ? esp_http_client_read(client_, reinterpret_cast<char *>(p), n) : -1;
}
bool Http::complete() const {
  return client_ && esp_http_client_is_complete_data_received(client_);
}
} // namespace nova
