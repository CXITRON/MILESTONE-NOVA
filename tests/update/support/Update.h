#pragma once
#include "SHA2Builder.h"
#include <openssl/pem.h>
#include <openssl/rsa.h>
#include <vector>
inline constexpr int HASH_SHA256 = 0, U_FLASH = 0;
inline std::vector<uint8_t> testFlash;
inline unsigned testFlashBegins = 0;
class UpdaterRSAVerifier {
  EVP_PKEY *key_ = nullptr;
public:
  UpdaterRSAVerifier(const uint8_t *pem, size_t size, int) {
    BIO *input = BIO_new_mem_buf(pem, int(size));
    key_ = PEM_read_bio_PUBKEY(input, nullptr, nullptr, nullptr);
    BIO_free(input);
  }
  ~UpdaterRSAVerifier() { EVP_PKEY_free(key_); }
  bool verify(SHA256Builder *hash, const uint8_t *signature, size_t size) {
    if (!key_ || size != 512) return false;
    uint8_t digest[32];
    hash->getBytes(digest);
    auto *ctx = EVP_PKEY_CTX_new(key_, nullptr);
    const bool ok = ctx && EVP_PKEY_verify_init(ctx) > 0 &&
                    EVP_PKEY_CTX_set_rsa_padding(ctx, RSA_PKCS1_PSS_PADDING) > 0 &&
                    EVP_PKEY_CTX_set_signature_md(ctx, EVP_sha256()) > 0 &&
                    EVP_PKEY_CTX_set_rsa_pss_saltlen(ctx, RSA_PSS_SALTLEN_AUTO) > 0 &&
                    EVP_PKEY_verify(ctx, signature, EVP_PKEY_get_size(key_), digest, 32) == 1;
    EVP_PKEY_CTX_free(ctx);
    return ok;
  }
};
// Flash is an in-memory adapter; both preflight and final signature checks use OpenSSL.
class UpdateClass {
  UpdaterRSAVerifier *verifier_ = nullptr;
  size_t size_ = 0;
public:
  bool installSignature(UpdaterRSAVerifier *verifier) { verifier_ = verifier; return true; }
  bool begin(size_t size, int) { ++testFlashBegins; size_ = size; testFlash.clear(); return true; }
  size_t write(const uint8_t *bytes, size_t size) {
    testFlash.insert(testFlash.end(), bytes, bytes + size);
    return size;
  }
  void abort() { testFlash.clear(); }
  bool end() {
    if (testFlash.size() != size_ || size_ < 512) return false;
    SHA256Builder hash;
    hash.begin(); hash.add(testFlash.data(), size_ - 512); hash.calculate();
    return verifier_ && verifier_->verify(&hash, testFlash.data() + size_ - 512, 512);
  }
};
