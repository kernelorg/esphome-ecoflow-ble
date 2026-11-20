"""EcoFlow power stations over Bluetooth LE.

Finds the unit by the serial number in its advertisement, authenticates
(session encryption type 0, 1 or 7, read from the advertisement) and
switches outputs. Supported families: Delta 3 / River 3 / Delta Pro 3
(protobuf ConfigWrite) and Delta 2 / River 2 (per-output TLV commands).
"""

from esphome import automation
import esphome.codegen as cg
from esphome.components import ble_client, esp32_ble_tracker
import esphome.config_validation as cv
from esphome.const import CONF_ID, CONF_STATE

CODEOWNERS = ["@kernelorg"]
DEPENDENCIES = ["ble_client", "esp32_ble_tracker"]
MULTI_CONF = True  # one block per unit, each with its own ble_client

CONF_DEVICE_SN = "device_sn"
CONF_USER_ID = "user_id"
CONF_AUTO_CONNECT = "auto_connect"
CONF_SLOT = "slot"
CONF_OUTPUT = "output"
CONF_MODE = "mode"

ecoflow_ble_ns = cg.esphome_ns.namespace("ecoflow_ble")
EcoFlowBLE = ecoflow_ble_ns.class_(
    "EcoFlowBLE",
    cg.Component,
    ble_client.BLEClientNode,
    esp32_ble_tracker.ESPBTDeviceListener,
)
EfOutput = ecoflow_ble_ns.enum("EfOutput")
OUTPUTS = {
    "ac": EfOutput.OUT_AC,
    "ac2": EfOutput.OUT_AC2,
    "ac_hv": EfOutput.OUT_AC_HV,
    "ac_lv": EfOutput.OUT_AC_LV,
    "usb": EfOutput.OUT_USB,
    "dc": EfOutput.OUT_DC,
}
SetOutputAction = ecoflow_ble_ns.class_("SetOutputAction", automation.Action)
SetLightAction = ecoflow_ble_ns.class_("SetLightAction", automation.Action)
# River 3 Plus built-in light (ConfigWrite cfg_led_mode).
LIGHT_MODES = {"off": 0, "dim": 1, "bright": 2, "sos": 3}

CONFIG_SCHEMA = (
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(EcoFlowBLE),
            # Usually set at runtime (e.g. from NVS after provisioning);
            # any value here is only a compile-time default.
            cv.Optional(CONF_DEVICE_SN, default=""): cv.string,
            cv.Optional(CONF_USER_ID, default=""): cv.string,
            # Aim the ble_client at the unit and connect once it is found.
            cv.Optional(CONF_AUTO_CONNECT, default=True): cv.boolean,
            # Load serial (+ shared user_id) from the credentials slot that
            # ef_provision fills: slot 0 = first unit chosen, 1 = second, …
            cv.Optional(CONF_SLOT): cv.int_range(min=0, max=7),
        }
    )
    .extend(ble_client.BLE_CLIENT_SCHEMA)
    .extend(esp32_ble_tracker.ESP_BLE_DEVICE_SCHEMA)
    .extend(cv.COMPONENT_SCHEMA)
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await ble_client.register_ble_node(var, config)
    await esp32_ble_tracker.register_ble_device(var, config)
    cg.add(var.set_device_sn(config[CONF_DEVICE_SN]))
    cg.add(var.set_user_id(config[CONF_USER_ID]))
    cg.add(var.set_auto_connect(config[CONF_AUTO_CONNECT]))
    if CONF_SLOT in config:
        cg.add(var.set_slot(config[CONF_SLOT]))


@automation.register_action(
    "ecoflow_ble.set_output",
    SetOutputAction,
    cv.Schema(
        {
            cv.GenerateID(): cv.use_id(EcoFlowBLE),
            cv.Required(CONF_OUTPUT): cv.enum(OUTPUTS, lower=True),
            cv.Required(CONF_STATE): cv.templatable(cv.boolean),
        }
    ),
    synchronous=True,
)
async def set_output_to_code(config, action_id, template_arg, args):
    var = cg.new_Pvariable(action_id, template_arg)
    await cg.register_parented(var, config[CONF_ID])
    cg.add(var.set_output(config[CONF_OUTPUT]))
    cg.add(var.set_state(await cg.templatable(config[CONF_STATE], args, cg.bool_)))
    return var


@automation.register_action(
    "ecoflow_ble.set_light",
    SetLightAction,
    cv.Schema(
        {
            cv.GenerateID(): cv.use_id(EcoFlowBLE),
            cv.Required(CONF_MODE): cv.templatable(cv.enum(LIGHT_MODES, lower=True)),
        }
    ),
    synchronous=True,
)
async def set_light_to_code(config, action_id, template_arg, args):
    var = cg.new_Pvariable(action_id, template_arg)
    await cg.register_parented(var, config[CONF_ID])
    mode = config[CONF_MODE]
    if hasattr(mode, "enum_value"):  # constant: off / dim / bright / sos
        cg.add(var.set_mode(mode.enum_value))
    else:  # lambda returning 0..3
        cg.add(var.set_mode(await cg.templatable(mode, args, cg.uint8)))
    return var
