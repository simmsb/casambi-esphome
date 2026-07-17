#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace esphome {
namespace casambi {

static const size_t AES_KEY_SIZE = 16;
static const size_t CMAC_SIZE = 16;
static const size_t NONCE_SIZE = 16;
static const size_t ECDH_KEY_SIZE = 32;
static const size_t PACKET_HEADER_LEN = 4;
static const uint8_t OPERATION_LIFETIME = 5;

static const char *const CASAMBI_SERVICE_UUID = "0000fe4d-0000-1000-8000-00805f9b34fb";
static const char *const CASAMBI_AUTH_CHAR_UUID = "c9ffde48-ca5a-0001-ab83-8f519b482f77";

enum class OpCode : uint8_t {
  SET_LEVEL = 1,
  SET_COLOR = 7,
};

static const uint8_t TARGET_TYPE_UNIT = 0x01;

inline uint16_t encode_target(uint8_t id, uint8_t type) {
  return (static_cast<uint16_t>(id) << 8) | type;
}

void rgb_to_hs(uint8_t r, uint8_t g, uint8_t b, uint16_t *hue, uint8_t *sat);

}  // namespace casambi
}  // namespace esphome
