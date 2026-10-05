#pragma once
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <cstddef>
#include <cstdint>
// Real host public-key parsing. Flash, transport and scheduler are separate test doubles.
inline constexpr int MBEDTLS_PK_RSA = 1;
struct mbedtls_pk_context { EVP_PKEY *key = nullptr; };
inline void mbedtls_pk_init(mbedtls_pk_context *key) { key->key = nullptr; }
inline void mbedtls_pk_free(mbedtls_pk_context *key) { EVP_PKEY_free(key->key); }
inline int mbedtls_pk_parse_public_key(mbedtls_pk_context *key, const uint8_t *data, size_t size) {
  BIO *input = BIO_new_mem_buf(data, int(size));
  key->key = PEM_read_bio_PUBKEY(input, nullptr, nullptr, nullptr);
  BIO_free(input);
  return key->key ? 0 : -1;
}
inline bool mbedtls_pk_can_do(const mbedtls_pk_context *key, int) {
  return key->key && EVP_PKEY_is_a(key->key, "RSA");
}
inline size_t mbedtls_pk_get_bitlen(const mbedtls_pk_context *key) {
  return key->key ? EVP_PKEY_get_bits(key->key) : 0;
}
