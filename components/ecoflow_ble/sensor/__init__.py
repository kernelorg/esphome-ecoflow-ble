"""ecoflow_ble sensors.

`type: rssi` (default) works for every model; the others come from the
unit's telemetry (Delta 3 / River 3 / Delta Pro 3 families) and stay empty
until the unit has reported them.
"""

import esphome.codegen as cg
from esphome.components import sensor
import esphome.config_validation as cv
from esphome.const import (
    CONF_TYPE,
    DEVICE_CLASS_BATTERY,
    DEVICE_CLASS_DURATION,
    DEVICE_CLASS_POWER,
    DEVICE_CLASS_SIGNAL_STRENGTH,
    DEVICE_CLASS_TEMPERATURE,
    ENTITY_CATEGORY_DIAGNOSTIC,
    STATE_CLASS_MEASUREMENT,
    UNIT_CELSIUS,
    UNIT_DECIBEL_MILLIWATT,
    UNIT_MINUTE,
    UNIT_PERCENT,
    UNIT_WATT,
)

from .. import EcoFlowBLE, ecoflow_ble_ns

DEPENDENCIES = ["ecoflow_ble"]
CONF_ECOFLOW_BLE_ID = "ecoflow_ble_id"

EcoFlowSensor = ecoflow_ble_ns.class_("EcoFlowSensor", sensor.Sensor, cg.Component)
T = ecoflow_ble_ns.enum("EcoFlowSensorType", is_class=True)

_POWER = dict(unit_of_measurement=UNIT_WATT, accuracy_decimals=0, device_class=DEVICE_CLASS_POWER,
              state_class=STATE_CLASS_MEASUREMENT)
_PERCENT = dict(unit_of_measurement=UNIT_PERCENT, accuracy_decimals=0, state_class=STATE_CLASS_MEASUREMENT)
_MINUTES = dict(unit_of_measurement=UNIT_MINUTE, accuracy_decimals=0, device_class=DEVICE_CLASS_DURATION,
                state_class=STATE_CLASS_MEASUREMENT)
TYPES = {
    "rssi": (T.RSSI, dict(unit_of_measurement=UNIT_DECIBEL_MILLIWATT, accuracy_decimals=0,
                          device_class=DEVICE_CLASS_SIGNAL_STRENGTH, state_class=STATE_CLASS_MEASUREMENT,
                          entity_category=ENTITY_CATEGORY_DIAGNOSTIC)),
    "battery_level": (T.BATTERY_LEVEL, dict(_PERCENT, accuracy_decimals=1, device_class=DEVICE_CLASS_BATTERY)),
    "input_power": (T.INPUT_POWER, _POWER),
    "output_power": (T.OUTPUT_POWER, _POWER),
    "ac_input_power": (T.AC_INPUT_POWER, _POWER),
    "ac_output_power": (T.AC_OUTPUT_POWER, _POWER),
    "dc_output_power": (T.DC_OUTPUT_POWER, _POWER),
    "usb_c_output_power": (T.USB_C_OUTPUT_POWER, _POWER),
    "usb_a_output_power": (T.USB_A_OUTPUT_POWER, _POWER),
    "solar_input_power": (T.SOLAR_INPUT_POWER, _POWER),
    "charge_remaining": (T.CHARGE_REMAINING, _MINUTES),
    "discharge_remaining": (T.DISCHARGE_REMAINING, _MINUTES),
    "cell_temperature": (T.CELL_TEMPERATURE, dict(unit_of_measurement=UNIT_CELSIUS, accuracy_decimals=0,
                                                  device_class=DEVICE_CLASS_TEMPERATURE,
                                                  state_class=STATE_CLASS_MEASUREMENT)),
    "charge_limit": (T.CHARGE_LIMIT, dict(_PERCENT, entity_category=ENTITY_CATEGORY_DIAGNOSTIC)),
    "discharge_limit": (T.DISCHARGE_LIMIT, dict(_PERCENT, entity_category=ENTITY_CATEGORY_DIAGNOSTIC)),
}

CONFIG_SCHEMA = cv.typed_schema(
    {
        name: sensor.sensor_schema(EcoFlowSensor, **kw)
        .extend({cv.GenerateID(CONF_ECOFLOW_BLE_ID): cv.use_id(EcoFlowBLE)})
        .extend(cv.COMPONENT_SCHEMA)
        for name, (_, kw) in TYPES.items()
    },
    key=CONF_TYPE,
    default_type="rssi",
    lower=True,
)


async def to_code(config):
    var = await sensor.new_sensor(config)
    await cg.register_component(var, config)
    await cg.register_parented(var, config[CONF_ECOFLOW_BLE_ID])
    cg.add(var.set_type(TYPES[config[CONF_TYPE]][0]))
