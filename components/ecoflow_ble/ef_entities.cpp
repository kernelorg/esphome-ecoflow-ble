#include "ef_entities.h"

#include "esphome/core/hal.h"
#include "esphome/core/log.h"

#include <cmath>

namespace esphome {
namespace ecoflow_ble {

static const char *const TAG = "ecoflow_ble.entity";

// ── switch ─────────────────────────────────────────────────────────────
#ifdef USE_SWITCH
void EcoFlowSwitch::setup() {
  // Never switch anything at boot: only show the saved state, if any.
  if (this->restore_mode & switch_::RESTORE_MODE_PERSISTENT_MASK) {
    auto saved = this->get_initial_state();
    if (saved.has_value())
      this->publish_state(*saved);
  }
}

void EcoFlowSwitch::loop() {
  uint32_t now = millis();
  if (now - this->last_check_ < EF_ENTITY_CHECK_MS)
    return;
  this->last_check_ = now;
  if (this->command_at_ != 0 && now - this->command_at_ < 3000)
    return;  // the unit has not reported the new state yet
  this->command_at_ = 0;
  auto s = this->parent_->output_state(this->output_);
  if (s.has_value() && (!this->has_state() || *s != this->state))
    this->publish_state(*s);
}

void EcoFlowSwitch::write_state(bool state) {
  auto *e = this->parent_;
  if (!e->supports(this->output_)) {
    ESP_LOGW(TAG, "'%s': %s has no %s output", this->get_name().c_str(), e->model_name().c_str(),
             EcoFlowBLE::output_name(this->output_));
    return;
  }
  if (!e->set_output(this->output_, state)) {
    ESP_LOGW(TAG, "'%s': not sent (%s)", this->get_name().c_str(), e->state_name());
    return;
  }
  this->command_at_ = millis() | 1u;
  this->publish_state(state);
}

void EcoFlowSwitch::dump_config() {
  LOG_SWITCH("", "EcoFlow output", this);
  ESP_LOGCONFIG(TAG, "  Output: %s", EcoFlowBLE::output_name(this->output_));
}
#endif

// ── select ─────────────────────────────────────────────────────────────
#ifdef USE_SELECT
void EcoFlowLightSelect::loop() {
  uint32_t now = millis();
  if (now - this->last_check_ < EF_ENTITY_CHECK_MS)
    return;
  this->last_check_ = now;
  if (this->command_at_ != 0 && now - this->command_at_ < 3000)
    return;
  this->command_at_ = 0;
  auto m = this->parent_->light_mode();
  if (!m.has_value() || *m > 3)
    return;
  if (!this->has_state() || this->active_index() != optional<size_t>(*m))
    this->publish_state((size_t) *m);
}

void EcoFlowLightSelect::control(size_t index) {
  auto *e = this->parent_;
  if (!e->supports_light()) {
    ESP_LOGW(TAG, "'%s': %s has no built-in light", this->get_name().c_str(), e->model_name().c_str());
    return;
  }
  if (!e->set_light((EfLightMode) index)) {
    ESP_LOGW(TAG, "'%s': not sent (%s)", this->get_name().c_str(), e->state_name());
    return;
  }
  this->command_at_ = millis() | 1u;
  this->publish_state(index);
}

void EcoFlowLightSelect::dump_config() { LOG_SELECT("", "EcoFlow light", this); }
#endif

// ── sensor: RSSI ───────────────────────────────────────────────────────
#ifdef USE_SENSOR
optional<float> EcoFlowSensor::value_() const {
  auto *e = this->parent_;
  auto u = [](optional<uint32_t> v) -> optional<float> {
    if (!v.has_value())
      return {};
    return (float) *v;
  };
  switch (this->type_) {
    case EcoFlowSensorType::RSSI:
      if (!e->is_found())
        return {};
      return (float) e->rssi();
    case EcoFlowSensorType::BATTERY_LEVEL: return e->battery_level();
    case EcoFlowSensorType::INPUT_POWER: return e->input_power();
    case EcoFlowSensorType::OUTPUT_POWER: return e->output_power();
    case EcoFlowSensorType::AC_INPUT_POWER: return e->ac_input_power();
    case EcoFlowSensorType::AC_OUTPUT_POWER: return e->ac_output_power();
    case EcoFlowSensorType::DC_OUTPUT_POWER: return e->dc_output_power();
    case EcoFlowSensorType::USB_C_OUTPUT_POWER: return e->usb_c_output_power();
    case EcoFlowSensorType::USB_A_OUTPUT_POWER: return e->usb_a_output_power();
    case EcoFlowSensorType::SOLAR_INPUT_POWER: return e->solar_input_power();
    case EcoFlowSensorType::CHARGE_REMAINING: return u(e->charge_remaining_min());
    case EcoFlowSensorType::DISCHARGE_REMAINING: return u(e->discharge_remaining_min());
    case EcoFlowSensorType::CELL_TEMPERATURE: {
      auto t = e->cell_temperature();
      if (!t.has_value())
        return {};
      return (float) *t;
    }
    case EcoFlowSensorType::CHARGE_LIMIT: return u(e->charge_limit());
    case EcoFlowSensorType::DISCHARGE_LIMIT: return u(e->discharge_limit());
  }
  return {};
}

void EcoFlowSensor::loop() {
  uint32_t now = millis();
  if (now - this->last_check_ < EF_ENTITY_CHECK_MS)
    return;
  this->last_check_ = now;
  auto v = this->value_();
  if (v.has_value() && (std::isnan(this->state) || *v != this->state))
    this->publish_state(*v);
}

void EcoFlowSensor::dump_config() { LOG_SENSOR("", "EcoFlow sensor", this); }
#endif

// ── binary sensor ──────────────────────────────────────────────────────
#ifdef USE_BINARY_SENSOR
optional<bool> EcoFlowBinarySensor::value_() const {
  switch (this->type_) {
    case EcoFlowBinaryType::FOUND: return this->parent_->is_found();
    case EcoFlowBinaryType::CONNECTED: return this->parent_->is_connected();
    case EcoFlowBinaryType::AUTHENTICATED: return this->parent_->is_authenticated();
    case EcoFlowBinaryType::AC_PLUGGED_IN: return this->parent_->ac_plugged_in();
  }
  return {};
}

void EcoFlowBinarySensor::loop() {
  auto v = this->value_();
  if (v.has_value())
    this->publish_state(*v);
}

void EcoFlowBinarySensor::dump_config() { LOG_BINARY_SENSOR("", "EcoFlow binary sensor", this); }
#endif

// ── text sensor ────────────────────────────────────────────────────────
#ifdef USE_TEXT_SENSOR
std::string EcoFlowTextSensor::value_() const {
  auto *e = this->parent_;
  switch (this->type_) {
    case EcoFlowTextType::MODEL: return e->model_name();
    case EcoFlowTextType::SERIAL_NUMBER: return e->device_sn();
    case EcoFlowTextType::STATE: return e->state_name();
    case EcoFlowTextType::LAST_ERROR: return e->is_authenticated() ? std::string() : e->last_error();
    case EcoFlowTextType::ADDRESS: {
      if (!e->is_found())
        return "";
      uint64_t a = e->address();
      char b[18];
      snprintf(b, sizeof(b), "%02X:%02X:%02X:%02X:%02X:%02X", (unsigned) (a >> 40) & 0xFF, (unsigned) (a >> 32) & 0xFF,
               (unsigned) (a >> 24) & 0xFF, (unsigned) (a >> 16) & 0xFF, (unsigned) (a >> 8) & 0xFF,
               (unsigned) a & 0xFF);
      return b;
    }
    case EcoFlowTextType::OUTPUTS: {
      std::string s = e->outputs_str();
      if (e->supports_light())
        s += s.empty() ? "light" : ", light";
      return s;
    }
  }
  return "";
}

void EcoFlowTextSensor::loop() {
  uint32_t now = millis();
  if (this->published_ && now - this->last_check_ < EF_ENTITY_CHECK_MS)
    return;
  this->last_check_ = now;
  std::string v = this->value_();
  if (!this->published_ || v != this->state) {
    this->published_ = true;
    this->publish_state(v);
  }
}

void EcoFlowTextSensor::dump_config() { LOG_TEXT_SENSOR("", "EcoFlow text sensor", this); }
#endif

}  // namespace ecoflow_ble
}  // namespace esphome
