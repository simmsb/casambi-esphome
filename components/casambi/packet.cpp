#include "packet.h"

namespace esphome {
namespace casambi {

void rgb_to_hs(uint8_t r, uint8_t g, uint8_t b, uint16_t *hue, uint8_t *sat) {
  float rf = r / 255.0f;
  float gf = g / 255.0f;
  float bf = b / 255.0f;

  float max = rf > gf ? (rf > bf ? rf : bf) : (gf > bf ? gf : bf);
  float min = rf < gf ? (rf < bf ? rf : bf) : (gf < bf ? gf : bf);
  float delta = max - min;

  float s = (max == 0.0f) ? 0.0f : (delta / max);

  float h = 0.0f;
  if (delta != 0.0f) {
    if (max == rf) {
      h = (gf - bf) / delta + (gf < bf ? 6.0f : 0.0f);
    } else if (max == gf) {
      h = (bf - rf) / delta + 2.0f;
    } else {
      h = (rf - gf) / delta + 4.0f;
    }
    h /= 6.0f;
  }

  *hue = static_cast<uint16_t>(h * 1023.0f);
  *sat = static_cast<uint8_t>(s * 255.0f);
}

}  // namespace casambi
}  // namespace esphome
