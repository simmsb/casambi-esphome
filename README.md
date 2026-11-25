# Casambi ESPHome External Component

> [!CAUTION]
> This entire repo is slopware, it seems to work for me, might not for you. If it doesn't work you're shit outta luck.

Requires ESPHome 2026.11.0 or newer (built against ESP-IDF 5.5).

## Quick start

### 1. Generate config

```bash
cd tools/casambi-config
cargo run --release -- --address AA:BB:CC:DD:EE:FF --password 'your-network-password'
```

Copy the printed YAML into your ESPHome device config.

The gateway MAC is the BLE address of any unit advertising the Casambi Evolution service (often shown in the Casambi app). On macOS, BLE MAC addresses are often unavailable via normal APIs — use another machine or the Casambi app if needed.

### 2. Wire ESPHome

```yaml
external_components:
  - source: github://simmsb/casambi-esphome
    components: [casambi]

esp32_ble_tracker:

ble_client:
  - mac_address: AA:BB:CC:DD:EE:FF
    id: casambi_ble

casambi:
  ble_client_id: casambi_ble
  key_id: 1
  key: "0123456789abcdef0123456789abcdef"

light:
  - platform: casambi
    name: "Living Room"
    unit_id: 1
```

See [`example.yaml`](example.yaml) for a full skeleton.

Connectivity (optional):

```yaml
binary_sensor:
  - platform: casambi
    name: "Casambi Network"
```

ON means the ESP is authenticated to the Casambi Evolution network over BLE.

## Light capabilities (v1)

- On / off
- Brightness
- RGB color

