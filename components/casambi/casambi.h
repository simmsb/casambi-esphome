#pragma once

#ifdef USE_ESP32

#include "encryption.h"
#include "key_exchange.h"
#include "packet.h"

#include "esphome/components/ble_client/ble_client.h"
#include "esphome/components/esp32_ble_tracker/esp32_ble_tracker.h"
#include "esphome/core/component.h"
#include "esphome/core/defines.h"

#ifdef USE_BINARY_SENSOR
#include "esphome/components/binary_sensor/binary_sensor.h"
#endif

#include <deque>
#include <esp_gattc_api.h>
#include <memory>
#include <vector>

namespace esphome::casambi {

namespace espbt = esphome::esp32_ble_tracker;

enum class ConnectionPhase : uint8_t {
  DISCONNECTED = 0,
  WAITING_DEVICE_INFO,
  WAITING_PEER_KEY,
  WAITING_KEY_ACK,
  WAITING_AUTH,
  AUTHENTICATED,
  ERROR,
};

class CasambiComponent final : public ble_client::BLEClientNode, public Component {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::AFTER_BLUETOOTH; }

  void gattc_event_handler(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if,
                           esp_ble_gattc_cb_param_t *param) override;

  void set_key_id(uint8_t key_id) { this->key_id_ = key_id; }
  void set_key(const std::vector<uint8_t> &key);
#ifdef USE_BINARY_SENSOR
  void set_connected_sensor(binary_sensor::BinarySensor *sensor) { this->connected_sensor_ = sensor; }
#endif

  bool is_authenticated() const { return this->phase_ == ConnectionPhase::AUTHENTICATED; }

  void set_unit_level(uint8_t unit_id, uint8_t level);
  void set_unit_color(uint8_t unit_id, uint8_t r, uint8_t g, uint8_t b);

 protected:
  static const size_t MAX_TX_QUEUE = 12;
  static const uint32_t TX_INTERVAL_MS = 50;

  uint8_t key_id_{0};
  uint8_t session_key_[AES_KEY_SIZE]{};

  uint16_t char_handle_{0};
  ConnectionPhase phase_{ConnectionPhase::DISCONNECTED};
#ifdef USE_BINARY_SENSOR
  binary_sensor::BinarySensor *connected_sensor_{nullptr};
#endif

  std::unique_ptr<ECDHKeyExchange> key_exchange_;
  std::unique_ptr<CasambiEncryption> encryption_;
  std::vector<uint8_t> transport_key_;

  uint8_t nonce_[NONCE_SIZE]{};
  uint32_t out_packet_count_{2};
  uint32_t in_packet_count_{1};
  uint16_t origin_{1};

  std::deque<std::vector<uint8_t>> tx_queue_;
  uint32_t last_tx_ms_{0};

  void reset_session_();
  void start_handshake_();
  void request_device_info_();
  void enable_notifications_();
  void send_our_public_key_();
  void send_authentication_();
  void write_raw_(const uint8_t *data, size_t len);
  void enqueue_tx_(std::vector<uint8_t> packet);
  void drain_tx_queue_();
  void publish_connected_(bool connected);

  void handle_device_info_(const uint8_t *data, uint16_t len);
  void handle_notification_(const uint8_t *data, uint16_t len);
  void handle_peer_key_(const uint8_t *data, uint16_t len);
  void handle_auth_response_(const uint8_t *data, uint16_t len);

  void send_operation_(uint8_t opcode, uint16_t target, const std::vector<uint8_t> &payload);
  std::vector<uint8_t> build_operation_(uint8_t opcode, uint16_t target, const std::vector<uint8_t> &payload);
  void send_encrypted_packet_(const std::vector<uint8_t> &packet, uint32_t counter, bool immediate);
  std::vector<uint8_t> outgoing_nonce_(uint32_t counter);
};

}  // namespace esphome::casambi

#endif  // USE_ESP32
