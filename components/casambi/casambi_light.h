#pragma once

#ifdef USE_ESP32

#include "casambi.h"

#include "esphome/components/light/light_output.h"
#include "esphome/core/component.h"

namespace esphome {
namespace casambi {

class CasambiLightOutput : public light::LightOutput, public Component {
 public:
  void set_parent(CasambiComponent *parent) { this->parent_ = parent; }
  void set_unit_id(uint8_t unit_id) { this->unit_id_ = unit_id; }

  void dump_config() override;
  light::LightTraits get_traits() override;
  void write_state(light::LightState *state) override;

 protected:
  CasambiComponent *parent_{nullptr};
  uint8_t unit_id_{0};

  // Last commanded target — Casambi handles fades; ignore ESPHome transition steps.
  bool has_last_cmd_{false};
  uint8_t last_level_{0};
  uint8_t last_r_{0};
  uint8_t last_g_{0};
  uint8_t last_b_{0};
  bool last_colored_{false};
};

}  // namespace casambi
}  // namespace esphome

#endif  // USE_ESP32
