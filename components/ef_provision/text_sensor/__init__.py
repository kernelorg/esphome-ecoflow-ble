"""ef_provision text sensor: status (setup progress) and ip (setup page address)."""

import esphome.codegen as cg
from esphome.components import text_sensor
import esphome.config_validation as cv
from esphome.const import CONF_TYPE

from .. import EfProvision, ef_provision_ns

DEPENDENCIES = ["ef_provision"]
CONF_EF_PROVISION_ID = "ef_provision_id"

EfProvisionTextSensor = ef_provision_ns.class_("EfProvisionTextSensor", text_sensor.TextSensor, cg.Component)
EfTextType = ef_provision_ns.enum("EfTextType", is_class=True)
TYPES = {"status": EfTextType.STATUS, "ip": EfTextType.IP}

CONFIG_SCHEMA = (
    text_sensor.text_sensor_schema(EfProvisionTextSensor, icon="mdi:cog-sync")
    .extend(
        {
            cv.GenerateID(CONF_EF_PROVISION_ID): cv.use_id(EfProvision),
            cv.Required(CONF_TYPE): cv.enum(TYPES, lower=True),
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
)


async def to_code(config):
    var = await text_sensor.new_text_sensor(config)
    await cg.register_component(var, config)
    await cg.register_parented(var, config[CONF_EF_PROVISION_ID])
    cg.add(var.set_type(config[CONF_TYPE]))
