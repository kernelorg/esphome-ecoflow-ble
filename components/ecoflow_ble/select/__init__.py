"""ecoflow_ble select: the built-in light (River 3 Plus) — Off / Dim / Bright / SOS."""

import esphome.codegen as cg
from esphome.components import select
import esphome.config_validation as cv

from .. import EcoFlowBLE, ecoflow_ble_ns

DEPENDENCIES = ["ecoflow_ble"]
CONF_ECOFLOW_BLE_ID = "ecoflow_ble_id"

EcoFlowLightSelect = ecoflow_ble_ns.class_("EcoFlowLightSelect", select.Select, cg.Component)

CONFIG_SCHEMA = (
    select.select_schema(EcoFlowLightSelect, icon="mdi:lightbulb")
    .extend({cv.GenerateID(CONF_ECOFLOW_BLE_ID): cv.use_id(EcoFlowBLE)})
    .extend(cv.COMPONENT_SCHEMA)
)


async def to_code(config):
    var = await select.new_select(config, options=["Off", "Dim", "Bright", "SOS"])
    await cg.register_component(var, config)
    await cg.register_parented(var, config[CONF_ECOFLOW_BLE_ID])
