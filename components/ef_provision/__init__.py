import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import esp32
from esphome.const import CONF_ID

CODEOWNERS = ["@kernelorg"]
# Setup page (esp_http_server) + EcoFlow cloud login (esp_http_client with
# the CA bundle) on top of ESPHome's `wifi:` component. Use `wifi:` with
# `enable_on_boot: false` so normal (BLE) operation never initialises the
# Wi-Fi driver; credentials are typically provided by `improv_ble:`.
DEPENDENCIES = ["wifi"]

CONF_ECOFLOW_BLE_ID = "ecoflow_ble_id"
CONF_FORGET_WIFI_ON_SETUP = "forget_wifi_on_setup"
CONF_AUTO_START = "auto_start"
CONF_WIFI_SSID = "wifi_ssid"
CONF_WIFI_PASS = "wifi_pass"
_MOVED = (
    "Wi-Fi credentials are no longer part of ef_provision: configure the "
    "`wifi:` component (e.g. enable_on_boot: false + improv_ble:) instead"
)

ef_provision_ns = cg.esphome_ns.namespace("ef_provision")
EfProvision = ef_provision_ns.class_("EfProvision", cg.Component)
EcoFlowBLE = cg.esphome_ns.namespace("ecoflow_ble").class_("EcoFlowBLE")

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(EfProvision),
        # Optional: list the EcoFlow units heard over BLE on the serial page,
        # so the user can pick one instead of typing the serial.
        cv.Optional(CONF_ECOFLOW_BLE_ID): cv.use_id(EcoFlowBLE),
        # Erase the stored Wi-Fi network on entering setup mode, so Improv
        # BLE starts and the phone sends the hotspot to use this time.
        cv.Optional(CONF_FORGET_WIFI_ON_SETUP, default=True): cv.boolean,
        # Decide the mode at boot (setup when nothing is stored or setup was
        # requested, normal otherwise), enable BLE and start the scan — no
        # on_boot lambdas needed. false: call set_active() yourself.
        cv.Optional(CONF_AUTO_START, default=True): cv.boolean,
        cv.Optional(CONF_WIFI_SSID): cv.invalid(_MOVED),
        cv.Optional(CONF_WIFI_PASS): cv.invalid(_MOVED),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    # esp_http_client is excluded by ESPHome's esp-idf build by default.
    esp32.include_builtin_idf_component("esp_http_client")
    # ESPHome >= 2026.10 excludes esp_http_server too unless a component
    # (web_server on ESP32) asks for it.
    esp32.include_builtin_idf_component("esp_http_server")
    # HTTPS login: verify against the built-in Mozilla CA bundle.
    esp32.add_idf_sdkconfig_option("CONFIG_MBEDTLS_CERTIFICATE_BUNDLE", True)
    esp32.add_idf_sdkconfig_option("CONFIG_MBEDTLS_CERTIFICATE_BUNDLE_DEFAULT_FULL", True)
    # Setup mode runs Wi-Fi, BLE (Improv + scan) and often a display at the
    # same time; allocate TLS buffers on demand and drop CA/config data right
    # after the handshake so the login fits into the remaining heap.
    esp32.add_idf_sdkconfig_option("CONFIG_MBEDTLS_DYNAMIC_BUFFER", True)
    esp32.add_idf_sdkconfig_option("CONFIG_MBEDTLS_DYNAMIC_FREE_CONFIG_DATA", True)
    esp32.add_idf_sdkconfig_option("CONFIG_MBEDTLS_DYNAMIC_FREE_CA_CERT", True)
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    cg.add(var.set_forget_wifi_on_setup(config[CONF_FORGET_WIFI_ON_SETUP]))
    cg.add(var.set_auto_start(config[CONF_AUTO_START]))
    if CONF_ECOFLOW_BLE_ID in config:
        cg.add_define("USE_EF_PROVISION_ECOFLOW")
        ef = await cg.get_variable(config[CONF_ECOFLOW_BLE_ID])
        cg.add(var.set_ecoflow(ef))
