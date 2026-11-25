import esphome.codegen as cg
from esphome.components import ble_client, esp32
import esphome.config_validation as cv
from esphome.const import CONF_ID, CONF_KEY
from esphome.types import ConfigType

CODEOWNERS = ["@casambi-esphome"]
DEPENDENCIES = ["ble_client"]
MULTI_CONF = False

CONF_CASAMBI_ID = "casambi_id"
CONF_KEY_ID = "key_id"
CONF_UNIT_ID = "unit_id"

casambi_ns = cg.esphome_ns.namespace("casambi")
CasambiComponent = casambi_ns.class_(
    "CasambiComponent", ble_client.BLEClientNode, cg.Component
)


def validate_key_hex(value):
    value = cv.string_strict(value)
    value = value.replace(" ", "").replace(":", "").lower()
    if len(value) != 32:
        raise cv.Invalid("key must be 32 hex characters (16 bytes)")
    try:
        bytes.fromhex(value)
    except ValueError as err:
        raise cv.Invalid(f"Invalid hex key: {err}") from err
    return value


CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(CasambiComponent),
            cv.Required(CONF_KEY_ID): cv.int_range(min=0, max=255),
            cv.Required(CONF_KEY): cv.sensitive(validate_key_hex),
        }
    )
    .extend(ble_client.BLE_CLIENT_SCHEMA)
    .extend(cv.COMPONENT_SCHEMA),
    cv.require_esphome_version(2026, 11, 0),
)


async def to_code(config: ConfigType) -> None:
    # The key exchange uses mbedTLS ECDH, which ESPHome trims unless requested.
    esp32.require_mbedtls_ecp()

    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await ble_client.register_ble_node(var, config)

    cg.add(var.set_key_id(config[CONF_KEY_ID]))

    key_bytes = list(bytes.fromhex(config[CONF_KEY]))
    cg.add(var.set_key(key_bytes))
