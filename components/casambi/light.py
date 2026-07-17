import esphome.codegen as cg
from esphome.components import light
import esphome.config_validation as cv
from esphome.const import CONF_OUTPUT_ID

from . import CONF_CASAMBI_ID, CONF_UNIT_ID, CasambiComponent, casambi_ns

DEPENDENCIES = ["casambi"]

CasambiLightOutput = casambi_ns.class_(
    "CasambiLightOutput", light.LightOutput, cg.Component
)

CONFIG_SCHEMA = light.RGB_LIGHT_SCHEMA.extend(
    {
        cv.GenerateID(CONF_OUTPUT_ID): cv.declare_id(CasambiLightOutput),
        cv.GenerateID(CONF_CASAMBI_ID): cv.use_id(CasambiComponent),
        cv.Required(CONF_UNIT_ID): cv.int_range(min=1, max=255),
    }
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_OUTPUT_ID])
    await cg.register_component(var, config)
    await light.register_light(var, config)

    hub = await cg.get_variable(config[CONF_CASAMBI_ID])
    cg.add(var.set_parent(hub))
    cg.add(var.set_unit_id(config[CONF_UNIT_ID]))
