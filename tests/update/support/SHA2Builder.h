#pragma once
#include "Runtime.h"
#include <array>
#include <cassert>
#include <openssl/evp.h>
#include <string>
class SHA256Builder {
  EVP_MD_CTX *ctx_ = EVP_MD_CTX_new();
  std::array<uint8_t, 32> digest_{};
public:
  ~SHA256Builder() { EVP_MD_CTX_free(ctx_); }
  void begin() { assert(EVP_DigestInit_ex(ctx_, EVP_sha256(), nullptr) == 1); }
  void add(const uint8_t *data, size_t size) { assert(EVP_DigestUpdate(ctx_, data, size) == 1); }
  void calculate() { assert(EVP_DigestFinal_ex(ctx_, digest_.data(), nullptr) == 1); }
  void getBytes(uint8_t *out) const { std::copy(digest_.begin(), digest_.end(), out); }
  std::string toString() const {
    std::string out;
    for (const auto byte : digest_) {
      out += "0123456789abcdef"[byte >> 4];
      out += "0123456789abcdef"[byte & 15];
    }
    return out;
  }
};
