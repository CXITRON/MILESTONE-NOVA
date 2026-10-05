#pragma once
// Artwork host tests replace HTTP transport; no ESP32 TLS/network is exercised.
using esp_http_client_handle_t = void *;
using esp_err_t = int;
struct esp_http_client_event_t;
