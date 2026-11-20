"""ecoflow_ble binary sensor: found (advertisement heard), connected,
authenticated; ac_plugged_in from the telemetry (Delta 3 / River 3 families)."""

import esphome.codegen as cg
from esphome.components import binary_sensor
import esphome.config_validation as cv
from esphome.const import CONF_TYPE, DEVICE_CLASS_CONNECTIVITY

from .. import EcoFlowBLE, ecoflow_ble_ns

DEPENDENCIES = ["ecoflow_ble"]
CONF_ECOFLOW_BLE_ID = "ecoflow_ble_id"

EcoFlowBinarySensor = ecoflow_ble_ns.class_("EcoFlowBinarySensor", binary_sensor.BinarySensor, cg.Component)
EcoFlowBinaryType = ecoflow_ble_ns.enum("EcoFlowBinaryType", is_class=True)
TYPES = {
    "found": EcoFlowBinaryType.FOUND,
    "connected": EcoFlowBinaryType.CONNECTED,
    "authenticated": EcoFlowBinaryType.AUTHENTICATED,
    "ac_plugged_in": EcoFlowBinaryType.AC_PLUGGED_IN,
}

CONFIG_SCHEMA = (
    binary_sensor.binary_sensor_schema(EcoFlowBinarySensor, device_class=DEVICE_CLASS_CONNECTIVITY)
    .extend(
        {
            cv.GenerateID(CONF_ECOFLOW_BLE_ID): cv.use_id(EcoFlowBLE),
            cv.Required(CONF_TYPE): cv.enum(TYPES, lower=True),
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
)


async def to_code(config):
    var = await binary_sensor.new_binary_sensor(config)
    await cg.register_component(var, config)
    await cg.register_parented(var, config[CONF_ECOFLOW_BLE_ID])
    cg.add(var.set_type(config[CONF_TYPE]))
