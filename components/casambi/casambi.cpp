#ifdef USE_ESP32

#include "casambi.h"

#include "esphome/core/log.h"

#include <mbedtls/sha256.h>

namespace esphome {
namespace casambi {

static const char *const TAG = "casambi";

void CasambiComponent::setup() {
  ESP_LOGI(TAG, "Casambi Evolution hub setup");
  this->publish_connected_(false);
}

void CasambiComponent::loop() { this->drain_tx_queue_(); }

void CasambiComponent::dump_config() {
  ESP_LOGCONFIG(TAG, "Casambi:");
  ESP_LOGCONFIG(TAG, "  Key id: %u", this->key_id_);
}

void CasambiComponent::set_key(const std::vector<uint8_t> &key) {
  if (key.size() != AES_KEY_SIZE) {
    ESP_LOGE(TAG, "Session key must be %u bytes, got %u", (unsigned) AES_KEY_SIZE, (unsigned) key.size());
    return;
  }
  memcpy(this->session_key_, key.data(), AES_KEY_SIZE);
}

void CasambiComponent::gattc_event_handler(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if,
                                           esp_ble_gattc_cb_param_t *param) {
  switch (event) {
    case ESP_GATTC_OPEN_EVT: {
      if (param->open.status == ESP_GATT_OK) {
        ESP_LOGI(TAG, "BLE connected");
      }
      break;
    }
    case ESP_GATTC_DISCONNECT_EVT: {
      ESP_LOGW(TAG, "BLE disconnected");
      this->reset_session_();
      break;
    }
    case ESP_GATTC_SEARCH_CMPL_EVT: {
      ESP_LOGI(TAG, "Service discovery complete");
      this->start_handshake_();
      break;
    }
    case ESP_GATTC_READ_CHAR_EVT: {
      if (param->read.status != ESP_GATT_OK) {
        ESP_LOGE(TAG, "Read failed: %d", param->read.status);
        this->phase_ = ConnectionPhase::ERROR;
        break;
      }
      if (param->read.handle == this->char_handle_ && this->phase_ == ConnectionPhase::WAITING_DEVICE_INFO) {
        this->handle_device_info_(param->read.value, param->read.value_len);
      }
      break;
    }
    case ESP_GATTC_REG_FOR_NOTIFY_EVT: {
      if (param->reg_for_notify.status != ESP_GATT_OK) {
        ESP_LOGE(TAG, "Register notify failed: %d", param->reg_for_notify.status);
        this->phase_ = ConnectionPhase::ERROR;
        break;
      }
      auto *descr = this->parent()->get_descriptor(espbt::ESPBTUUID::from_raw(CASAMBI_SERVICE_UUID),
                                                   espbt::ESPBTUUID::from_raw(CASAMBI_AUTH_CHAR_UUID),
                                                   espbt::ESPBTUUID::from_uint16(0x2902));
      uint16_t cccd_handle = (descr != nullptr) ? descr->handle : (this->char_handle_ + 1);
      uint8_t cccd_val[2] = {0x01, 0x00};
      esp_ble_gattc_write_char_descr(gattc_if, this->parent()->get_conn_id(), cccd_handle, sizeof(cccd_val), cccd_val,
                                     ESP_GATT_WRITE_TYPE_RSP, ESP_GATT_AUTH_REQ_NONE);
      ESP_LOGI(TAG, "Notifications enabled");
      this->phase_ = ConnectionPhase::WAITING_PEER_KEY;
      break;
    }
    case ESP_GATTC_NOTIFY_EVT: {
      if (param->notify.handle == this->char_handle_) {
        this->handle_notification_(param->notify.value, param->notify.value_len);
      }
      break;
    }
    case ESP_GATTC_WRITE_CHAR_EVT: {
      if (param->write.status != ESP_GATT_OK) {
        ESP_LOGW(TAG, "Write failed: %d", param->write.status);
      }
      break;
    }
    default:
      break;
  }
}

void CasambiComponent::reset_session_() {
  this->phase_ = ConnectionPhase::DISCONNECTED;
  this->char_handle_ = 0;
  this->key_exchange_.reset();
  this->encryption_.reset();
  this->transport_key_.clear();
  this->tx_queue_.clear();
  this->last_tx_ms_ = 0;
  this->out_packet_count_ = 2;
  this->in_packet_count_ = 1;
  this->origin_ = 1;
  memset(this->nonce_, 0, NONCE_SIZE);
  this->publish_connected_(false);
}

void CasambiComponent::start_handshake_() {
  this->reset_session_();

  auto *chr = this->parent()->get_characteristic(espbt::ESPBTUUID::from_raw(CASAMBI_SERVICE_UUID),
                                                 espbt::ESPBTUUID::from_raw(CASAMBI_AUTH_CHAR_UUID));
  if (chr == nullptr) {
    ESP_LOGE(TAG, "Casambi auth characteristic not found (need Evolution service fe4d)");
    this->phase_ = ConnectionPhase::ERROR;
    return;
  }
  this->char_handle_ = chr->handle;

  this->key_exchange_ = std::make_unique<ECDHKeyExchange>();
  if (!this->key_exchange_->generate_key_pair()) {
    ESP_LOGE(TAG, "ECDH key generation failed");
    this->phase_ = ConnectionPhase::ERROR;
    return;
  }

  this->phase_ = ConnectionPhase::WAITING_DEVICE_INFO;
  this->request_device_info_();
}

void CasambiComponent::request_device_info_() {
  auto status = esp_ble_gattc_read_char(this->parent()->get_gattc_if(), this->parent()->get_conn_id(),
                                        this->char_handle_, ESP_GATT_AUTH_REQ_NONE);
  if (status != ESP_OK) {
    ESP_LOGE(TAG, "Read device info failed: %d", status);
    this->phase_ = ConnectionPhase::ERROR;
  }
}

void CasambiComponent::handle_device_info_(const uint8_t *data, uint16_t len) {
  if (len < 7 + NONCE_SIZE) {
    ESP_LOGE(TAG, "Device info too short: %u", len);
    this->phase_ = ConnectionPhase::ERROR;
    return;
  }
  if (data[0] != 0x01) {
    ESP_LOGE(TAG, "Unexpected device info type 0x%02x", data[0]);
    this->phase_ = ConnectionPhase::ERROR;
    return;
  }

  uint8_t version = data[1];
  memcpy(this->nonce_, data + 7, NONCE_SIZE);
  ESP_LOGI(TAG, "Device info OK (node_protocol=%u mtu=%u unit=%u)", version, data[2],
           (unsigned) ((data[3] << 8) | data[4]));
  this->enable_notifications_();
}

void CasambiComponent::enable_notifications_() {
  auto status =
      esp_ble_gattc_register_for_notify(this->parent()->get_gattc_if(), this->parent()->get_remote_bda(), this->char_handle_);
  if (status != ESP_OK) {
    ESP_LOGE(TAG, "Register notify failed: %d", status);
    this->phase_ = ConnectionPhase::ERROR;
  }
}

void CasambiComponent::handle_notification_(const uint8_t *data, uint16_t len) {
  if (len == 0) {
    return;
  }

  switch (this->phase_) {
    case ConnectionPhase::WAITING_PEER_KEY:
      this->handle_peer_key_(data, len);
      break;
    case ConnectionPhase::WAITING_KEY_ACK:
      // Device acknowledges our public key with a single 0x03 byte before auth may proceed.
      if (data[0] == 0x03) {
        ESP_LOGI(TAG, "Key exchange acknowledged");
        this->send_authentication_();
      } else {
        ESP_LOGW(TAG, "Unexpected key-ack notify type=0x%02x len=%u", data[0], len);
      }
      break;
    case ConnectionPhase::WAITING_AUTH:
      this->handle_auth_response_(data, len);
      break;
    case ConnectionPhase::AUTHENTICATED:
      // Unit state sync not implemented in v1
      ESP_LOGV(TAG, "Ignoring data notify (%u bytes)", len);
      break;
    default:
      ESP_LOGD(TAG, "Notify in unexpected phase %u (%u bytes)", (unsigned) this->phase_, len);
      break;
  }
}

void CasambiComponent::handle_peer_key_(const uint8_t *data, uint16_t len) {
  if (len < 65 || data[0] != 0x02) {
    ESP_LOGE(TAG, "Invalid peer key packet (len=%u type=0x%02x)", len, len ? data[0] : 0);
    this->phase_ = ConnectionPhase::ERROR;
    return;
  }

  if (!this->key_exchange_->set_device_public_key(data + 1, data + 33)) {
    ESP_LOGE(TAG, "Failed to accept peer public key");
    this->phase_ = ConnectionPhase::ERROR;
    return;
  }

  std::vector<uint8_t> transport_key = this->key_exchange_->derive_transport_key();
  if (transport_key.size() != AES_KEY_SIZE) {
    ESP_LOGE(TAG, "Failed to derive transport key");
    this->phase_ = ConnectionPhase::ERROR;
    return;
  }
  this->transport_key_ = std::move(transport_key);
  this->encryption_ = std::make_unique<CasambiEncryption>(this->transport_key_.data());
  this->send_our_public_key_();
  // Wait for device 0x03 ack before authenticating (casambi-bt exchangeKey flow).
  this->phase_ = ConnectionPhase::WAITING_KEY_ACK;
}

void CasambiComponent::send_our_public_key_() {
  std::vector<uint8_t> pub_x = this->key_exchange_->get_public_key_x();
  std::vector<uint8_t> pub_y = this->key_exchange_->get_public_key_y();
  uint8_t packet[66];
  packet[0] = 0x02;
  memcpy(packet + 1, pub_x.data(), 32);
  memcpy(packet + 33, pub_y.data(), 32);
  packet[65] = 0x01;
  this->write_raw_(packet, sizeof(packet));
  ESP_LOGI(TAG, "Sent our ECDH public key");
}

void CasambiComponent::send_authentication_() {
  if (this->transport_key_.size() != AES_KEY_SIZE) {
    ESP_LOGE(TAG, "Transport key unavailable for auth");
    this->phase_ = ConnectionPhase::ERROR;
    return;
  }

  uint8_t hash_input[AES_KEY_SIZE + NONCE_SIZE + AES_KEY_SIZE];
  memcpy(hash_input, this->session_key_, AES_KEY_SIZE);
  memcpy(hash_input + AES_KEY_SIZE, this->nonce_, NONCE_SIZE);
  memcpy(hash_input + AES_KEY_SIZE + NONCE_SIZE, this->transport_key_.data(), AES_KEY_SIZE);

  uint8_t auth_digest[32];
  mbedtls_sha256(hash_input, sizeof(hash_input), auth_digest, 0);

  std::vector<uint8_t> auth_packet;
  auth_packet.push_back(this->in_packet_count_ & 0xFF);
  auth_packet.push_back((this->in_packet_count_ >> 8) & 0xFF);
  auth_packet.push_back((this->in_packet_count_ >> 16) & 0xFF);
  auth_packet.push_back((this->in_packet_count_ >> 24) & 0xFF);
  auth_packet.push_back(0x04);
  auth_packet.push_back(this->key_id_);
  auth_packet.insert(auth_packet.end(), auth_digest, auth_digest + 32);

  this->phase_ = ConnectionPhase::WAITING_AUTH;
  this->send_encrypted_packet_(auth_packet, this->in_packet_count_, true);
  this->in_packet_count_++;
  ESP_LOGI(TAG, "Sent authentication");
}

void CasambiComponent::handle_auth_response_(const uint8_t *data, uint16_t len) {
  if (this->encryption_ == nullptr || len < CMAC_SIZE + 5) {
    ESP_LOGW(TAG, "Auth response unexpected (len=%u)", len);
    return;
  }

  std::vector<uint8_t> nonce(NONCE_SIZE);
  memcpy(nonce.data(), data, 4);
  memcpy(nonce.data() + 4, this->nonce_ + 4, 12);

  std::vector<uint8_t> packet(data, data + len);
  std::vector<uint8_t> plaintext = this->encryption_->decrypt_and_verify(packet, nonce, 4);
  if (plaintext.empty()) {
    ESP_LOGE(TAG, "Auth response decrypt failed");
    this->phase_ = ConnectionPhase::ERROR;
    return;
  }

  if (plaintext[0] == 0x05) {
    this->phase_ = ConnectionPhase::AUTHENTICATED;
    this->node_state = espbt::ClientState::ESTABLISHED;
    this->publish_connected_(true);
    ESP_LOGI(TAG, "Authenticated with Casambi network");
  } else if (plaintext[0] == 0x06) {
    ESP_LOGE(TAG, "Authentication rejected by device (wrong network key?)");
    this->phase_ = ConnectionPhase::ERROR;
  } else {
    ESP_LOGE(TAG, "Unexpected auth response type 0x%02x", plaintext[0]);
    this->phase_ = ConnectionPhase::ERROR;
  }
}

void CasambiComponent::write_raw_(const uint8_t *data, size_t len) {
  if (this->char_handle_ == 0 || this->parent() == nullptr) {
    return;
  }
  // Write without response — write-with-response piles up and freezes WiFi/web under load.
  auto status = esp_ble_gattc_write_char(this->parent()->get_gattc_if(), this->parent()->get_conn_id(),
                                        this->char_handle_, len, const_cast<uint8_t *>(data),
                                        ESP_GATT_WRITE_TYPE_NO_RSP, ESP_GATT_AUTH_REQ_NONE);
  if (status != ESP_OK) {
    ESP_LOGW(TAG, "GATT write failed: %d", status);
  } else {
    this->last_tx_ms_ = millis();
  }
}

void CasambiComponent::enqueue_tx_(std::vector<uint8_t> packet) {
  if (this->tx_queue_.size() >= MAX_TX_QUEUE) {
    ESP_LOGW(TAG, "TX queue full, dropping oldest command");
    this->tx_queue_.pop_front();
  }
  this->tx_queue_.push_back(std::move(packet));
}

void CasambiComponent::drain_tx_queue_() {
  if (this->tx_queue_.empty() || !this->is_authenticated() || this->char_handle_ == 0) {
    return;
  }
  uint32_t now = millis();
  if (this->last_tx_ms_ != 0 && (now - this->last_tx_ms_) < TX_INTERVAL_MS) {
    return;
  }
  std::vector<uint8_t> packet = std::move(this->tx_queue_.front());
  this->tx_queue_.pop_front();
  this->write_raw_(packet.data(), packet.size());
}

void CasambiComponent::publish_connected_(bool connected) {
  if (this->connected_sensor_ != nullptr) {
    this->connected_sensor_->publish_state(connected);
  }
}

void CasambiComponent::set_unit_level(uint8_t unit_id, uint8_t level) {
  if (!this->is_authenticated()) {
    ESP_LOGW(TAG, "Not authenticated; cannot set level");
    return;
  }
  uint16_t target = encode_target(unit_id, TARGET_TYPE_UNIT);
  std::vector<uint8_t> payload = {level};
  this->send_operation_(static_cast<uint8_t>(OpCode::SET_LEVEL), target, payload);
}

void CasambiComponent::set_unit_color(uint8_t unit_id, uint8_t r, uint8_t g, uint8_t b) {
  if (!this->is_authenticated()) {
    ESP_LOGW(TAG, "Not authenticated; cannot set color");
    return;
  }
  uint16_t hue = 0;
  uint8_t sat = 0;
  rgb_to_hs(r, g, b, &hue, &sat);
  uint16_t target = encode_target(unit_id, TARGET_TYPE_UNIT);
  std::vector<uint8_t> payload = {static_cast<uint8_t>(hue & 0xFF), static_cast<uint8_t>((hue >> 8) & 0xFF), sat};
  this->send_operation_(static_cast<uint8_t>(OpCode::SET_COLOR), target, payload);
}

void CasambiComponent::send_operation_(uint8_t opcode, uint16_t target, const std::vector<uint8_t> &payload) {
  std::vector<uint8_t> op = this->build_operation_(opcode, target, payload);
  std::vector<uint8_t> full;
  full.push_back(this->out_packet_count_ & 0xFF);
  full.push_back((this->out_packet_count_ >> 8) & 0xFF);
  full.push_back((this->out_packet_count_ >> 16) & 0xFF);
  full.push_back((this->out_packet_count_ >> 24) & 0xFF);
  full.push_back(0x07);
  full.insert(full.end(), op.begin(), op.end());
  this->send_encrypted_packet_(full, this->out_packet_count_, false);
  this->out_packet_count_++;
}

std::vector<uint8_t> CasambiComponent::build_operation_(uint8_t opcode, uint16_t target,
                                                        const std::vector<uint8_t> &payload) {
  std::vector<uint8_t> packet;
  uint16_t flags = (OPERATION_LIFETIME << 11) | payload.size();
  packet.push_back((flags >> 8) & 0xFF);
  packet.push_back(flags & 0xFF);
  packet.push_back(opcode);
  packet.push_back((this->origin_ >> 8) & 0xFF);
  packet.push_back(this->origin_ & 0xFF);
  this->origin_++;
  packet.push_back((target >> 8) & 0xFF);
  packet.push_back(target & 0xFF);
  packet.push_back(0x00);
  packet.push_back(0x00);
  packet.insert(packet.end(), payload.begin(), payload.end());
  return packet;
}

void CasambiComponent::send_encrypted_packet_(const std::vector<uint8_t> &packet, uint32_t counter, bool immediate) {
  if (this->encryption_ == nullptr) {
    ESP_LOGE(TAG, "Encryption not ready");
    return;
  }
  std::vector<uint8_t> nonce = this->outgoing_nonce_(counter);
  std::vector<uint8_t> encrypted = this->encryption_->encrypt_then_mac(packet, nonce);
  if (encrypted.empty()) {
    ESP_LOGE(TAG, "Encrypt failed");
    return;
  }
  if (immediate) {
    this->write_raw_(encrypted.data(), encrypted.size());
  } else {
    this->enqueue_tx_(std::move(encrypted));
  }
}

std::vector<uint8_t> CasambiComponent::outgoing_nonce_(uint32_t counter) {
  std::vector<uint8_t> nonce(NONCE_SIZE);
  memcpy(nonce.data(), this->nonce_, 4);
  nonce[4] = counter & 0xFF;
  nonce[5] = (counter >> 8) & 0xFF;
  nonce[6] = (counter >> 16) & 0xFF;
  nonce[7] = (counter >> 24) & 0xFF;
  memcpy(nonce.data() + 8, this->nonce_ + 8, 8);
  return nonce;
}

}  // namespace casambi
}  // namespace esphome

#endif  // USE_ESP32
