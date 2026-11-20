"""ef_provision button: start_setup / exit_setup (both reboot), forget_wifi."""

import esphome.codegen as cg
from esphome.components import button
import esphome.config_validation as cv
from esphome.const import CONF_TYPE, ENTITY_CATEGORY_CONFIG

from .. import EfProvision, ef_provision_ns

DEPENDENCIES = ["ef_provision"]
CONF_EF_PROVISION_ID = "ef_provision_id"

EfProvisionButton = ef_provision_ns.class_("EfProvisionButton", button.Button, cg.Component)
EfButtonType = ef_provision_ns.enum("EfButtonType", is_class=True)
TYPES = {
    "start_setup": EfButtonType.START_SETUP,
    "exit_setup": EfButtonType.EXIT_SETUP,
    "forget_wifi": EfButtonType.FORGET_WIFI,
}

CONFIG_SCHEMA = (
    button.button_schema(EfProvisionButton, entity_category=ENTITY_CATEGORY_CONFIG, icon="mdi:cog")
    .extend(
        {
            cv.GenerateID(CONF_EF_PROVISION_ID): cv.use_id(EfProvision),
            cv.Required(CONF_TYPE): cv.enum(TYPES, lower=True),
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
)


async def to_code(config):
    var = await button.new_button(config)
    await cg.register_component(var, config)
    await cg.register_parented(var, config[CONF_EF_PROVISION_ID])
    cg.add(var.set_type(config[CONF_TYPE]))
