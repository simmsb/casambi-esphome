#ifdef USE_ESP32

#include "casambi_light.h"

#include "esphome/core/log.h"

namespace esphome {
namespace casambi {

static const char *const TAG_LIGHT = "casambi.light";

void CasambiLightOutput::dump_config() {
  ESP_LOGCONFIG(TAG_LIGHT, "Casambi Light:");
  ESP_LOGCONFIG(TAG_LIGHT, "  Unit ID: %u", this->unit_id_);
}

light::LightTraits CasambiLightOutput::get_traits() {
  auto traits = light::LightTraits();
  traits.set_supported_color_modes({light::ColorMode::RGB});
  return traits;
}

void CasambiLightOutput::write_state(light::LightState *state) {
  if (this->parent_ == nullptr) {
    return;
  }

  // Use remote_values (commanded target), not current_values (ESPHome fade steps).
  // Casambi fixtures apply their own transition from a single command.
  float brightness;
  state->remote_values.as_brightness(&brightness);

  uint8_t level = 0;
  if (brightness >= 0.001f) {
    level = static_cast<uint8_t>(brightness * 255.0f + 0.5f);
    if (level == 0) {
      level = 1;
    }
  }

  uint8_t r = static_cast<uint8_t>(state->remote_values.get_red() * 255.0f + 0.5f);
  uint8_t g = static_cast<uint8_t>(state->remote_values.get_green() * 255.0f + 0.5f);
  uint8_t b = static_cast<uint8_t>(state->remote_values.get_blue() * 255.0f + 0.5f);
  bool colored = (level > 0) && (r != g || g != b);

  if (this->has_last_cmd_ && this->last_level_ == level && this->last_r_ == r && this->last_g_ == g &&
      this->last_b_ == b && this->last_colored_ == colored) {
    return;
  }
  this->has_last_cmd_ = true;
  this->last_level_ = level;
  this->last_r_ = r;
  this->last_g_ = g;
  this->last_b_ = b;
  this->last_colored_ = colored;

  this->parent_->set_unit_level(this->unit_id_, level);
  if (colored) {
    this->parent_->set_unit_color(this->unit_id_, r, g, b);
  }
}

}  // namespace casambi
}  // namespace esphome

#endif  // USE_ESP32
