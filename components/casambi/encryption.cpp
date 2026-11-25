#include "encryption.h"

#include "esphome/core/log.h"

#include <mbedtls/aes.h>

namespace esphome::casambi {

static const char *const TAG_ENC = "casambi.enc";

CasambiEncryption::CasambiEncryption(const uint8_t *key) { memcpy(this->key_, key, AES_KEY_SIZE); }

CasambiEncryption::~CasambiEncryption() { memset(this->key_, 0, AES_KEY_SIZE); }

std::vector<uint8_t> CasambiEncryption::encrypt_then_mac(const std::vector<uint8_t> &packet,
                                                         const std::vector<uint8_t> &nonce, size_t header_len) {
  if (nonce.size() != NONCE_SIZE || packet.size() < header_len) {
    ESP_LOGE(TAG_ENC, "encrypt_then_mac: invalid args");
    return {};
  }

  std::vector<uint8_t> header(packet.begin(), packet.begin() + header_len);
  std::vector<uint8_t> payload(packet.begin() + header_len, packet.end());
  std::vector<uint8_t> encrypted_payload = this->encrypt_internal_(payload, nonce);

  std::vector<uint8_t> ciphertext = header;
  ciphertext.insert(ciphertext.end(), encrypted_payload.begin(), encrypted_payload.end());

  std::vector<uint8_t> mac = this->compute_cmac_(ciphertext);
  ciphertext.insert(ciphertext.end(), mac.begin(), mac.end());
  return ciphertext;
}

std::vector<uint8_t> CasambiEncryption::decrypt_and_verify(const std::vector<uint8_t> &packet,
                                                           const std::vector<uint8_t> &nonce, size_t header_len) {
  if (nonce.size() != NONCE_SIZE || packet.size() < header_len + CMAC_SIZE) {
    ESP_LOGE(TAG_ENC, "decrypt_and_verify: invalid args");
    return {};
  }

  size_t ciphertext_len = packet.size() - CMAC_SIZE;
  std::vector<uint8_t> ciphertext(packet.begin(), packet.begin() + ciphertext_len);
  std::vector<uint8_t> received_mac(packet.begin() + ciphertext_len, packet.end());
  std::vector<uint8_t> computed_mac = this->compute_cmac_(ciphertext);

  if (memcmp(received_mac.data(), computed_mac.data(), CMAC_SIZE) != 0) {
    ESP_LOGW(TAG_ENC, "CMAC verification failed");
    return {};
  }

  std::vector<uint8_t> encrypted_payload(ciphertext.begin() + header_len, ciphertext.end());
  return this->encrypt_internal_(encrypted_payload, nonce);
}

std::vector<uint8_t> CasambiEncryption::encrypt_internal_(const std::vector<uint8_t> &data,
                                                          const std::vector<uint8_t> &nonce) {
  std::vector<uint8_t> result;
  result.reserve(data.size());

  mbedtls_aes_context aes_ctx;
  mbedtls_aes_init(&aes_ctx);
  mbedtls_aes_setkey_enc(&aes_ctx, this->key_, AES_KEY_SIZE * 8);

  uint32_t counter = 0;
  size_t offset = 0;
  while (offset < data.size()) {
    uint8_t counter_block[16];
    memcpy(counter_block, nonce.data(), 12);
    counter_block[12] = counter & 0xFF;
    counter_block[13] = (counter >> 8) & 0xFF;
    counter_block[14] = (counter >> 16) & 0xFF;
    counter_block[15] = (counter >> 24) & 0xFF;

    uint8_t encrypted_block[16];
    mbedtls_aes_crypt_ecb(&aes_ctx, MBEDTLS_AES_ENCRYPT, counter_block, encrypted_block);

    size_t remaining = data.size() - offset;
    size_t block_size = remaining < 16 ? remaining : 16;
    for (size_t i = 0; i < block_size; i++) {
      result.push_back(data[offset + i] ^ encrypted_block[i]);
    }
    offset += block_size;
    counter++;
  }

  mbedtls_aes_free(&aes_ctx);
  return result;
}

std::vector<uint8_t> CasambiEncryption::compute_cmac_(const std::vector<uint8_t> &data) {
  std::vector<uint8_t> mac(CMAC_SIZE);

  mbedtls_aes_context aes_ctx;
  mbedtls_aes_init(&aes_ctx);
  mbedtls_aes_setkey_enc(&aes_ctx, this->key_, AES_KEY_SIZE * 8);

  uint8_t L[16] = {0};
  uint8_t K1[16], K2[16];
  mbedtls_aes_crypt_ecb(&aes_ctx, MBEDTLS_AES_ENCRYPT, L, L);

  left_shift_(L, K1);
  if (L[0] & 0x80) {
    K1[15] ^= 0x87;
  }
  left_shift_(K1, K2);
  if (K1[0] & 0x80) {
    K2[15] ^= 0x87;
  }

  uint8_t M_last[16] = {0};
  bool complete_block = (!data.empty() && data.size() % 16 == 0);

  if (complete_block) {
    size_t last_block_offset = data.size() - 16;
    for (int i = 0; i < 16; i++) {
      M_last[i] = data[last_block_offset + i] ^ K1[i];
    }
  } else {
    size_t remaining = data.size() % 16;
    size_t last_block_offset = data.size() - remaining;
    for (size_t i = 0; i < remaining; i++) {
      M_last[i] = data[last_block_offset + i];
    }
    M_last[remaining] = 0x80;
    for (int i = 0; i < 16; i++) {
      M_last[i] ^= K2[i];
    }
  }

  uint8_t X[16] = {0};
  size_t num_blocks = (data.size() + 15) / 16;
  for (size_t i = 0; i + 1 < num_blocks; i++) {
    for (int j = 0; j < 16; j++) {
      X[j] ^= data[i * 16 + j];
    }
    mbedtls_aes_crypt_ecb(&aes_ctx, MBEDTLS_AES_ENCRYPT, X, X);
  }
  for (int i = 0; i < 16; i++) {
    X[i] ^= M_last[i];
  }
  mbedtls_aes_crypt_ecb(&aes_ctx, MBEDTLS_AES_ENCRYPT, X, X);
  memcpy(mac.data(), X, CMAC_SIZE);

  mbedtls_aes_free(&aes_ctx);
  return mac;
}

void CasambiEncryption::left_shift_(const uint8_t *input, uint8_t *output) {
  uint8_t overflow = 0;
  for (int i = 15; i >= 0; i--) {
    output[i] = (input[i] << 1) | overflow;
    overflow = (input[i] & 0x80) ? 1 : 0;
  }
}

}  // namespace esphome::casambi
