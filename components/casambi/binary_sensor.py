import esphome.codegen as cg
from esphome.components import binary_sensor
import esphome.config_validation as cv
from esphome.const import (
    DEVICE_CLASS_CONNECTIVITY,
    ENTITY_CATEGORY_DIAGNOSTIC,
)
from esphome.types import ConfigType

from . import CONF_CASAMBI_ID, CasambiComponent

DEPENDENCIES = ["casambi"]

CONFIG_SCHEMA = binary_sensor.binary_sensor_schema(
    device_class=DEVICE_CLASS_CONNECTIVITY,
    entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
).extend(
    {
        cv.GenerateID(CONF_CASAMBI_ID): cv.use_id(CasambiComponent),
    }
)


async def to_code(config: ConfigType) -> None:
    hub = await cg.get_variable(config[CONF_CASAMBI_ID])
    sens = await binary_sensor.new_binary_sensor(config)
    cg.add(hub.set_connected_sensor(sens))
