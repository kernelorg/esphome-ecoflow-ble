#include "ef_prov_entities.h"

#include "esphome/core/hal.h"
#include "esphome/core/log.h"

namespace esphome {
namespace ef_provision {

static const char *const TAG = "ef_provision.entity";

#ifdef USE_BUTTON
void EfProvisionButton::press_action() {
  switch (this->type_) {
    case EfButtonType::START_SETUP:
      this->parent_->start_setup(true);  // reboots into setup mode
      break;
    case EfButtonType::EXIT_SETUP:
      this->parent_->exit_setup();  // reboots into normal mode
      break;
    case EfButtonType::FORGET_WIFI:
      EfProvision::forget_wifi();
      break;
  }
}
void EfProvisionButton::dump_config() { LOG_BUTTON("", "EcoFlow setup button", this); }
#endif

#ifdef USE_BINARY_SENSOR
void EfProvisionBinarySensor::dump_config() { LOG_BINARY_SENSOR("", "EcoFlow setup mode", this); }
#endif

#ifdef USE_TEXT_SENSOR
std::string EfProvisionTextSensor::value_() const {
  auto *p = this->parent_;
  if (this->type_ == EfTextType::IP)
    return p->is_active() && p->wifi_up() ? p->ip() : std::string();
  if (!p->is_active())
    return "normal mode";
  return p->message();
}

void EfProvisionTextSensor::loop() {
  uint32_t now = millis();
  if (this->published_ && now - this->last_check_ < 1000)
    return;
  this->last_check_ = now;
  std::string v = this->value_();
  if (!this->published_ || v != this->state) {
    this->published_ = true;
    this->publish_state(v);
  }
}
void EfProvisionTextSensor::dump_config() { LOG_TEXT_SENSOR("", "EcoFlow setup text", this); }
#endif

}  // namespace ef_provision
}  // namespace esphome
