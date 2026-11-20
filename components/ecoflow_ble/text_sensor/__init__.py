"""ecoflow_ble text sensor: model, serial, state, error, address, outputs."""

import esphome.codegen as cg
from esphome.components import text_sensor
import esphome.config_validation as cv
from esphome.const import CONF_TYPE, ENTITY_CATEGORY_DIAGNOSTIC

from .. import EcoFlowBLE, ecoflow_ble_ns

DEPENDENCIES = ["ecoflow_ble"]
CONF_ECOFLOW_BLE_ID = "ecoflow_ble_id"

EcoFlowTextSensor = ecoflow_ble_ns.class_("EcoFlowTextSensor", text_sensor.TextSensor, cg.Component)
EcoFlowTextType = ecoflow_ble_ns.enum("EcoFlowTextType", is_class=True)
TYPES = {
    "model": EcoFlowTextType.MODEL,
    "serial": EcoFlowTextType.SERIAL_NUMBER,
    "state": EcoFlowTextType.STATE,
    "error": EcoFlowTextType.LAST_ERROR,
    "address": EcoFlowTextType.ADDRESS,
    "outputs": EcoFlowTextType.OUTPUTS,
}

CONFIG_SCHEMA = (
    text_sensor.text_sensor_schema(EcoFlowTextSensor, entity_category=ENTITY_CATEGORY_DIAGNOSTIC)
    .extend(
        {
            cv.GenerateID(CONF_ECOFLOW_BLE_ID): cv.use_id(EcoFlowBLE),
            cv.Required(CONF_TYPE): cv.enum(TYPES, lower=True),
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
)


async def to_code(config):
    var = await text_sensor.new_text_sensor(config)
    await cg.register_component(var, config)
    await cg.register_parented(var, config[CONF_ECOFLOW_BLE_ID])
    cg.add(var.set_type(config[CONF_TYPE]))
