"""ef_provision binary sensor: on while the device is in setup mode."""

import esphome.codegen as cg
from esphome.components import binary_sensor
import esphome.config_validation as cv

from .. import EfProvision, ef_provision_ns

DEPENDENCIES = ["ef_provision"]
CONF_EF_PROVISION_ID = "ef_provision_id"

EfProvisionBinarySensor = ef_provision_ns.class_(
    "EfProvisionBinarySensor", binary_sensor.BinarySensor, cg.Component
)

CONFIG_SCHEMA = (
    binary_sensor.binary_sensor_schema(EfProvisionBinarySensor, icon="mdi:cog-sync")
    .extend(
        {
            cv.GenerateID(CONF_EF_PROVISION_ID): cv.use_id(EfProvision),
            cv.Optional("type", default="setup_mode"): cv.one_of("setup_mode", lower=True),
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
)


async def to_code(config):
    var = await binary_sensor.new_binary_sensor(config)
    await cg.register_component(var, config)
    await cg.register_parented(var, config[CONF_EF_PROVISION_ID])
