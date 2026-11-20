#pragma once

// switch / select / sensor / binary_sensor / text_sensor platforms of ecoflow_ble.
// Status entities check their value once a second and publish on change.

#include "esphome/core/component.h"
#include "esphome/core/defines.h"
#include "esphome/core/helpers.h"
#include "ecoflow_ble.h"

#ifdef USE_SWITCH
#include "esphome/components/switch/switch.h"
#endif
#ifdef USE_SELECT
#include "esphome/components/select/select.h"
#endif
#ifdef USE_SENSOR
#include "esphome/components/sensor/sensor.h"
#endif
#ifdef USE_BINARY_SENSOR
#include "esphome/components/binary_sensor/binary_sensor.h"
#endif
#ifdef USE_TEXT_SENSOR
#include "esphome/components/text_sensor/text_sensor.h"
#endif

namespace esphome {
namespace ecoflow_ble {

static constexpr uint32_t EF_ENTITY_CHECK_MS = 1000;

#ifdef USE_SWITCH
/// One output (AC / USB / DC / …). The state comes from the unit's
/// telemetry; until the unit reports this output it is the last command
/// sent (assumed state).
class EcoFlowSwitch : public switch_::Switch, public Component, public Parented<EcoFlowBLE> {
 public:
  void set_output(EfOutput o) { this->output_ = o; }
  void setup() override;
  void loop() override;
  void dump_config() override;
  bool assumed_state() override { return !this->parent_->output_state(this->output_).has_value(); }

 protected:
  void write_state(bool state) override;
  EfOutput output_{OUT_AC};
  uint32_t last_check_{0};
  uint32_t command_at_{0};  // ignore telemetry briefly after a command
};
#endif

#ifdef USE_SELECT
/// Built-in light mode (River 3 Plus): Off / Dim / Bright / SOS.
class EcoFlowLightSelect : public select::Select, public Component, public Parented<EcoFlowBLE> {
 public:
  void loop() override;
  void dump_config() override;

 protected:
  void control(size_t index) override;
  uint32_t last_check_{0};
  uint32_t command_at_{0};
};
#endif

#ifdef USE_SENSOR
enum class EcoFlowSensorType : uint8_t {
  RSSI,
  BATTERY_LEVEL,
  INPUT_POWER,
  OUTPUT_POWER,
  AC_INPUT_POWER,
  AC_OUTPUT_POWER,
  DC_OUTPUT_POWER,
  USB_C_OUTPUT_POWER,
  USB_A_OUTPUT_POWER,
  SOLAR_INPUT_POWER,
  CHARGE_REMAINING,
  DISCHARGE_REMAINING,
  CELL_TEMPERATURE,
  CHARGE_LIMIT,
  DISCHARGE_LIMIT,
};

class EcoFlowSensor : public sensor::Sensor, public Component, public Parented<EcoFlowBLE> {
 public:
  void set_type(EcoFlowSensorType t) { this->type_ = t; }
  void loop() override;
  void dump_config() override;

 protected:
  optional<float> value_() const;
  EcoFlowSensorType type_{EcoFlowSensorType::RSSI};
  uint32_t last_check_{0};
};
#endif

#ifdef USE_BINARY_SENSOR
enum class EcoFlowBinaryType : uint8_t { FOUND, CONNECTED, AUTHENTICATED, AC_PLUGGED_IN };

class EcoFlowBinarySensor : public binary_sensor::BinarySensor, public Component, public Parented<EcoFlowBLE> {
 public:
  void set_type(EcoFlowBinaryType t) { this->type_ = t; }
  void loop() override;
  void dump_config() override;

 protected:
  optional<bool> value_() const;
  EcoFlowBinaryType type_{EcoFlowBinaryType::AUTHENTICATED};
};
#endif

#ifdef USE_TEXT_SENSOR
enum class EcoFlowTextType : uint8_t { MODEL, SERIAL_NUMBER, STATE, LAST_ERROR, ADDRESS, OUTPUTS };

class EcoFlowTextSensor : public text_sensor::TextSensor, public Component, public Parented<EcoFlowBLE> {
 public:
  void set_type(EcoFlowTextType t) { this->type_ = t; }
  void loop() override;
  void dump_config() override;

 protected:
  std::string value_() const;
  EcoFlowTextType type_{EcoFlowTextType::STATE};
  uint32_t last_check_{0};
  bool published_{false};
};
#endif

}  // namespace ecoflow_ble
}  // namespace esphome
