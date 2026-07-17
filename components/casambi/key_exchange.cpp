#include "key_exchange.h"

#include "esphome/core/log.h"

#include <mbedtls/ctr_drbg.h>
#include <mbedtls/ecdh.h>
#include <mbedtls/entropy.h>
#include <mbedtls/sha256.h>

namespace esphome {
namespace casambi {

static const char *const TAG_ECDH = "casambi.ecdh";

ECDHKeyExchange::ECDHKeyExchange() {
  mbedtls_ecp_group_init(&this->grp_);
  mbedtls_mpi_init(&this->d_);
  mbedtls_ecp_point_init(&this->Q_);
  mbedtls_ecp_point_init(&this->Qp_);
}

ECDHKeyExchange::~ECDHKeyExchange() {
  mbedtls_ecp_group_free(&this->grp_);
  mbedtls_mpi_free(&this->d_);
  mbedtls_ecp_point_free(&this->Q_);
  mbedtls_ecp_point_free(&this->Qp_);
}

bool ECDHKeyExchange::generate_key_pair() {
  mbedtls_entropy_context entropy;
  mbedtls_ctr_drbg_context ctr_drbg;
  mbedtls_entropy_init(&entropy);
  mbedtls_ctr_drbg_init(&ctr_drbg);

  const char *pers = "casambi_ecdh";
  int ret = mbedtls_ctr_drbg_seed(&ctr_drbg, mbedtls_entropy_func, &entropy,
                                  reinterpret_cast<const unsigned char *>(pers), strlen(pers));
  if (ret != 0) {
    ESP_LOGE(TAG_ECDH, "RNG seed failed: %d", ret);
    mbedtls_ctr_drbg_free(&ctr_drbg);
    mbedtls_entropy_free(&entropy);
    return false;
  }

  ret = mbedtls_ecp_group_load(&this->grp_, MBEDTLS_ECP_DP_SECP256R1);
  if (ret != 0) {
    ESP_LOGE(TAG_ECDH, "Group load failed: %d", ret);
    mbedtls_ctr_drbg_free(&ctr_drbg);
    mbedtls_entropy_free(&entropy);
    return false;
  }

  ret = mbedtls_ecdh_gen_public(&this->grp_, &this->d_, &this->Q_, mbedtls_ctr_drbg_random, &ctr_drbg);
  mbedtls_ctr_drbg_free(&ctr_drbg);
  mbedtls_entropy_free(&entropy);
  if (ret != 0) {
    ESP_LOGE(TAG_ECDH, "Key generation failed: %d", ret);
    return false;
  }

  this->key_pair_generated_ = true;
  ESP_LOGD(TAG_ECDH, "Key pair generated");
  return true;
}

bool ECDHKeyExchange::set_device_public_key(const uint8_t *x, const uint8_t *y) {
  if (!this->key_pair_generated_) {
    return false;
  }

  mbedtls_mpi x_mpi, y_mpi;
  mbedtls_mpi_init(&x_mpi);
  mbedtls_mpi_init(&y_mpi);

  int ret = mbedtls_mpi_read_binary_le(&x_mpi, x, ECDH_KEY_SIZE);
  if (ret != 0) {
    ESP_LOGE(TAG_ECDH, "Failed to read X: %d", ret);
    mbedtls_mpi_free(&x_mpi);
    mbedtls_mpi_free(&y_mpi);
    return false;
  }
  ret = mbedtls_mpi_read_binary_le(&y_mpi, y, ECDH_KEY_SIZE);
  if (ret != 0) {
    ESP_LOGE(TAG_ECDH, "Failed to read Y: %d", ret);
    mbedtls_mpi_free(&x_mpi);
    mbedtls_mpi_free(&y_mpi);
    return false;
  }

  mbedtls_mpi_lset(&this->Qp_.Z, 1);
  mbedtls_mpi_copy(&this->Qp_.X, &x_mpi);
  mbedtls_mpi_copy(&this->Qp_.Y, &y_mpi);

  ret = mbedtls_ecp_check_pubkey(&this->grp_, &this->Qp_);
  mbedtls_mpi_free(&x_mpi);
  mbedtls_mpi_free(&y_mpi);
  if (ret != 0) {
    ESP_LOGE(TAG_ECDH, "Invalid public key: %d", ret);
    return false;
  }

  this->device_key_set_ = true;
  ESP_LOGD(TAG_ECDH, "Device public key set");
  return true;
}

std::vector<uint8_t> ECDHKeyExchange::get_public_key_x() const {
  std::vector<uint8_t> result(ECDH_KEY_SIZE, 0);
  if (!this->key_pair_generated_) {
    return result;
  }
  mbedtls_mpi_write_binary_le(&this->Q_.X, result.data(), ECDH_KEY_SIZE);
  return result;
}

std::vector<uint8_t> ECDHKeyExchange::get_public_key_y() const {
  std::vector<uint8_t> result(ECDH_KEY_SIZE, 0);
  if (!this->key_pair_generated_) {
    return result;
  }
  mbedtls_mpi_write_binary_le(&this->Q_.Y, result.data(), ECDH_KEY_SIZE);
  return result;
}

std::vector<uint8_t> ECDHKeyExchange::derive_transport_key() {
  std::vector<uint8_t> result;
  if (!this->key_pair_generated_ || !this->device_key_set_) {
    ESP_LOGE(TAG_ECDH, "Cannot derive key — not ready");
    return result;
  }

  // ESP-IDF mbedtls requires a non-NULL RNG for ecp_mul (returns ECP_BAD_INPUT_DATA otherwise).
  mbedtls_entropy_context entropy;
  mbedtls_ctr_drbg_context ctr_drbg;
  mbedtls_entropy_init(&entropy);
  mbedtls_ctr_drbg_init(&ctr_drbg);
  const char *pers = "casambi_ecdh_shared";
  int ret = mbedtls_ctr_drbg_seed(&ctr_drbg, mbedtls_entropy_func, &entropy,
                                  reinterpret_cast<const unsigned char *>(pers), strlen(pers));
  if (ret != 0) {
    ESP_LOGE(TAG_ECDH, "RNG seed failed: %d", ret);
    mbedtls_ctr_drbg_free(&ctr_drbg);
    mbedtls_entropy_free(&entropy);
    return result;
  }

  mbedtls_mpi shared_secret;
  mbedtls_mpi_init(&shared_secret);
  ret = mbedtls_ecdh_compute_shared(&this->grp_, &shared_secret, &this->Qp_, &this->d_, mbedtls_ctr_drbg_random,
                                    &ctr_drbg);
  mbedtls_ctr_drbg_free(&ctr_drbg);
  mbedtls_entropy_free(&entropy);
  if (ret != 0) {
    ESP_LOGE(TAG_ECDH, "Compute shared secret failed: %d", ret);
    mbedtls_mpi_free(&shared_secret);
    return result;
  }

  uint8_t secret_bytes[ECDH_KEY_SIZE];
  mbedtls_mpi_write_binary(&shared_secret, secret_bytes, ECDH_KEY_SIZE);
  mbedtls_mpi_free(&shared_secret);

  uint8_t secret_reversed[ECDH_KEY_SIZE];
  for (size_t i = 0; i < ECDH_KEY_SIZE; i++) {
    secret_reversed[i] = secret_bytes[ECDH_KEY_SIZE - 1 - i];
  }

  uint8_t hash[32];
  mbedtls_sha256(secret_reversed, ECDH_KEY_SIZE, hash, 0);
  result.assign(AES_KEY_SIZE, 0);
  for (size_t i = 0; i < AES_KEY_SIZE; i++) {
    result[i] = hash[i] ^ hash[16 + i];
  }

  ESP_LOGD(TAG_ECDH, "Transport key derived");
  return result;
}

}  // namespace casambi
}  // namespace esphome
