#pragma once

#include "packet.h"

#include <vector>

namespace esphome {
namespace casambi {

class CasambiEncryption {
 public:
  explicit CasambiEncryption(const uint8_t *key);
  ~CasambiEncryption();

  std::vector<uint8_t> encrypt_then_mac(const std::vector<uint8_t> &packet, const std::vector<uint8_t> &nonce,
                                        size_t header_len = PACKET_HEADER_LEN);

  std::vector<uint8_t> decrypt_and_verify(const std::vector<uint8_t> &packet, const std::vector<uint8_t> &nonce,
                                          size_t header_len = PACKET_HEADER_LEN);

 protected:
  uint8_t key_[AES_KEY_SIZE]{};

  std::vector<uint8_t> encrypt_internal_(const std::vector<uint8_t> &data, const std::vector<uint8_t> &nonce);
  std::vector<uint8_t> compute_cmac_(const std::vector<uint8_t> &data);
  static void left_shift_(const uint8_t *input, uint8_t *output);
};

}  // namespace casambi
}  // namespace esphome
