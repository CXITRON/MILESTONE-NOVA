#pragma once
#include <algorithm>
#include <cassert>
#include <cstring>
#include <string>
#include <utility>
#include <vector>
using esp_err_t = int;
inline constexpr int ESP_OK = 0, HTTP_EVENT_ON_HEADER = 1, HTTP_METHOD_POST = 1;
struct esp_http_client_event_t {
  int event_id;
  void *user_data;
  char *header_key, *header_value;
};
struct esp_http_client_config_t {
  const char *url = nullptr, *user_agent = nullptr;
  int timeout_ms = 0, buffer_size = 0, buffer_size_tx = 0;
  bool disable_auto_redirect = false;
  int (*crt_bundle_attach)(void *) = nullptr;
  int (*event_handler)(esp_http_client_event_t *) = nullptr;
  void *user_data = nullptr;
};
struct Response {
  int status = 200;
  std::vector<std::pair<std::string, std::string>> headers;
  std::string body;
};
inline std::vector<Response> responses;
inline std::vector<std::string> requestedUrls;
inline size_t responseAt = 0;
struct Client {
  esp_http_client_config_t config;
  std::string url;
  Response response;
  size_t offset = 0;
};
using esp_http_client_handle_t = Client *;
inline Client *esp_http_client_init(const esp_http_client_config_t *config) {
  assert(config->crt_bundle_attach && config->disable_auto_redirect);
  return new Client{*config, config->url, {}, 0};
}
inline void esp_http_client_cleanup(Client *c) { delete c; }
inline void esp_http_client_close(Client *) {}
inline int esp_http_client_set_url(Client *c, const char *url) { c->url = url; return ESP_OK; }
inline int esp_http_client_open(Client *c, size_t) {
  assert(responseAt < responses.size());
  requestedUrls.push_back(c->url);
  c->response = responses[responseAt++];
  c->offset = 0;
  return ESP_OK;
}
inline int esp_http_client_write(Client *, const char *, size_t size) { return int(size); }
inline void esp_http_client_set_method(Client *, int) {}
inline void esp_http_client_set_header(Client *, const char *, const char *) {}
inline int64_t esp_http_client_fetch_headers(Client *c) {
  for (auto &[key, value] : c->response.headers) {
    esp_http_client_event_t event{HTTP_EVENT_ON_HEADER, c->config.user_data, key.data(), value.data()};
    c->config.event_handler(&event);
  }
  return c->response.body.size();
}
inline int esp_http_client_get_status_code(Client *c) { return c->response.status; }
inline int esp_http_client_get_errno(Client *) { return 0; }
inline int esp_http_client_get_and_clear_last_tls_error(Client *, int *tls, int *flags) {
  *tls = *flags = 0;
  return ESP_OK;
}
inline int esp_http_client_read(Client *c, char *out, size_t size) {
  size = std::min(size, c->response.body.size() - c->offset);
  memcpy(out, c->response.body.data() + c->offset, size);
  c->offset += size;
  return int(size);
}
inline bool esp_http_client_is_complete_data_received(Client *c) {
  return c->offset == c->response.body.size();
}
