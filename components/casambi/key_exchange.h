#pragma once

#include "packet.h"

#define MBEDTLS_ALLOW_PRIVATE_ACCESS
#include <mbedtls/ecp.h>
#include <vector>

namespace esphome::casambi {

class ECDHKeyExchange {
 public:
  ECDHKeyExchange();
  ~ECDHKeyExchange();

  bool generate_key_pair();
  bool set_device_public_key(const uint8_t *x, const uint8_t *y);
  std::vector<uint8_t> get_public_key_x() const;
  std::vector<uint8_t> get_public_key_y() const;
  std::vector<uint8_t> derive_transport_key();

 protected:
  mbedtls_ecp_group grp_{};
  mbedtls_mpi d_{};
  mbedtls_ecp_point Q_{};
  mbedtls_ecp_point Qp_{};
  bool key_pair_generated_{false};
  bool device_key_set_{false};
};

}  // namespace esphome::casambi
