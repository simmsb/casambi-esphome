import esphome.codegen as cg
from esphome.components import light
import esphome.config_validation as cv
from esphome.types import ConfigType

from . import CONF_CASAMBI_ID, CONF_UNIT_ID, CasambiComponent, casambi_ns

DEPENDENCIES = ["casambi"]

CasambiLightOutput = casambi_ns.class_(
    "CasambiLightOutput", light.LightOutput, cg.Component
)

CONFIG_SCHEMA = light.light_schema(CasambiLightOutput, light.LightType.RGB).extend(
    {
        cv.GenerateID(CONF_CASAMBI_ID): cv.use_id(CasambiComponent),
        cv.Required(CONF_UNIT_ID): cv.int_range(min=1, max=255),
    }
)


async def to_code(config: ConfigType) -> None:
    var = await light.new_light(config)
    await cg.register_component(var, config)

    await cg.add_variable(var.set_parent, config[CONF_CASAMBI_ID])
    cg.add(var.set_unit_id(config[CONF_UNIT_ID]))
