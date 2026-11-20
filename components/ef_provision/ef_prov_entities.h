#pragma once

// button / binary_sensor / text_sensor platforms of ef_provision.

#include "esphome/core/component.h"
#include "esphome/core/defines.h"
#include "esphome/core/helpers.h"
#include "ef_provision.h"

#ifdef USE_BUTTON
#include "esphome/components/button/button.h"
#endif
#ifdef USE_BINARY_SENSOR
#include "esphome/components/binary_sensor/binary_sensor.h"
#endif
#ifdef USE_TEXT_SENSOR
#include "esphome/components/text_sensor/text_sensor.h"
#endif

namespace esphome {
namespace ef_provision {

#ifdef USE_BUTTON
enum class EfButtonType : uint8_t { START_SETUP, EXIT_SETUP, FORGET_WIFI };

class EfProvisionButton : public button::Button, public Component, public Parented<EfProvision> {
 public:
  void set_type(EfButtonType t) { this->type_ = t; }
  void dump_config() override;

 protected:
  void press_action() override;
  EfButtonType type_{EfButtonType::START_SETUP};
};
#endif

#ifdef USE_BINARY_SENSOR
class EfProvisionBinarySensor : public binary_sensor::BinarySensor, public Component, public Parented<EfProvision> {
 public:
  void setup() override { this->publish_initial_state(this->parent_->is_active()); }
  void loop() override { this->publish_state(this->parent_->is_active()); }
  void dump_config() override;
};
#endif

#ifdef USE_TEXT_SENSOR
enum class EfTextType : uint8_t { STATUS, IP };

class EfProvisionTextSensor : public text_sensor::TextSensor, public Component, public Parented<EfProvision> {
 public:
  void set_type(EfTextType t) { this->type_ = t; }
  void loop() override;
  void dump_config() override;

 protected:
  std::string value_() const;
  EfTextType type_{EfTextType::STATUS};
  uint32_t last_check_{0};
  bool published_{false};
};
#endif

}  // namespace ef_provision
}  // namespace esphome
