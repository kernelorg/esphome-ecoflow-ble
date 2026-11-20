"""ecoflow_ble switch: one output of the unit (ac, ac2, ac_hv, ac_lv, usb, dc).

The state comes from the unit's telemetry (Delta 3 / River 3 / Delta Pro 3
families); until the unit reports the output — and on other models — it is
the last command sent (assumed state). Nothing is switched at boot.
"""

import esphome.codegen as cg
from esphome.components import switch
import esphome.config_validation as cv

from .. import OUTPUTS, CONF_OUTPUT
from .. import EcoFlowBLE, ecoflow_ble_ns

DEPENDENCIES = ["ecoflow_ble"]
CONF_ECOFLOW_BLE_ID = "ecoflow_ble_id"

EcoFlowSwitch = ecoflow_ble_ns.class_("EcoFlowSwitch", switch.Switch, cg.Component)

CONFIG_SCHEMA = (
    switch.switch_schema(EcoFlowSwitch, default_restore_mode="DISABLED")
    .extend(
        {
            cv.GenerateID(CONF_ECOFLOW_BLE_ID): cv.use_id(EcoFlowBLE),
            cv.Required(CONF_OUTPUT): cv.enum(OUTPUTS, lower=True),
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
)


async def to_code(config):
    var = await switch.new_switch(config)
    await cg.register_component(var, config)
    await cg.register_parented(var, config[CONF_ECOFLOW_BLE_ID])
    cg.add(var.set_output(config[CONF_OUTPUT]))
