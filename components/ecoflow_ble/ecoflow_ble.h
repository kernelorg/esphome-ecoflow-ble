#pragma once

#include "esphome/core/automation.h"
#include "esphome/core/component.h"
#include "esphome/core/defines.h"
#include "esphome/core/hal.h"
#include "esphome/core/optional.h"

#ifdef USE_ESP32

#include "esphome/components/ble_client/ble_client.h"
#include "esphome/components/esp32_ble_tracker/esp32_ble_tracker.h"

#include <esp_gattc_api.h>
#include <esp_gap_ble_api.h>
#include <string>
#include <vector>
#include <cstdint>

namespace esphome {
namespace ecoflow_ble {

namespace espbt = esphome::esp32_ble_tracker;

// Link / handshake state.
enum class EfState : uint8_t {
  IDLE = 0,        // not connected
  DISCOVERING,     // GATT connected, locating write/notify chars
  ENABLING_NOTIFY, // CCCD write in flight
  PUBKEY_SENT,     // (type 7) sent our public key, awaiting device public key
  KEYINFO_SENT,    // (type 7) sent getKeyInfo, awaiting sRand/seed
  AUTHSTATUS_SENT, // sent getAuthStatus, awaiting reply
  AUTH_SENT,       // sent autoAuth, awaiting confirmation
  AUTHENTICATED,   // ready for commands
  FAILED,          // handshake/auth error; resets on disconnect
};

/// Command protocol generation.
enum class EfFamily : uint8_t {
  UNKNOWN = 0,
  GEN3,  // Delta 3 / River 3 / Delta Pro 3: protobuf ConfigWrite (cmd FE 11, v0x13)
  GEN2,  // Delta 2 / River 2: one TLV command per output (cmd_set 0x20, v0x02)
};

/// Switchable outputs. Which ones exist depends on the model (see supports()).
enum EfOutput : uint8_t {
  OUT_AC = 0,  // main AC outlets (on Delta Pro 3: both HV and LV)
  OUT_AC2,     // second AC bank (Delta 3 Max Plus / Ultra Plus)
  OUT_AC_HV,   // Delta Pro 3 high-voltage AC
  OUT_AC_LV,   // Delta Pro 3 low-voltage AC
  OUT_USB,     // USB-A / USB-C
  OUT_DC,      // 12 V car / DC5521
  OUT_COUNT,
};

/// Built-in light (River 3 Plus): ConfigWrite cfg_led_mode.
enum EfLightMode : uint8_t { LIGHT_OFF = 0, LIGHT_DIM = 1, LIGHT_BRIGHT = 2, LIGHT_SOS = 3 };

static constexpr uint8_t FEAT_LIGHT = 0x01;

struct ModelInfo {
  const char *prefix;  // first characters of the serial number
  const char *name;
  EfFamily family;
  uint8_t outputs;     // bit mask of (1 << EfOutput)
  uint8_t ac_dst;      // GEN2: destination module of the AC command
  uint8_t features;    // FEAT_* bits
};

/// An EcoFlow unit heard nearby (for picking the serial during setup).
struct NearbyUnit {
  std::string serial;
  std::string model;
  int rssi{0};
  uint32_t last_seen{0};
};

class EcoFlowBLE : public Component, public ble_client::BLEClientNode, public espbt::ESPBTDeviceListener {
 public:
  EcoFlowBLE() { instances_().push_back(this); }

  // ── all units in the config (multi-unit UIs, setup page) ──────────
  static size_t instance_count() { return instances_().size(); }
  /// Unit by index (wraps around). Never null once one is configured.
  static EcoFlowBLE *instance(size_t i) {
    auto &v = instances_();
    return v.empty() ? nullptr : v[i % v.size()];
  }

  void setup() override;
  void loop() override {}
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::AFTER_BLUETOOTH; }
  // Reboot / deep sleep: drop the BLE link cleanly so the unit starts
  // advertising again at once instead of waiting for a supervision timeout.
  void on_shutdown() override;
  bool teardown() override;

  void gattc_event_handler(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if,
                           esp_ble_gattc_cb_param_t *param) override;
  /// Finds the unit by the serial in its advertisement, reads the session
  /// encryption type from it and (with auto_connect) aims the BLE client.
  bool parse_device(const espbt::ESPBTDevice &device) override;

  // ── configuration ──────────────────────────────────────────────────
  /// Serial number of the unit. Also selects the model by its prefix.
  void set_device_sn(const std::string &sn);
  void set_user_id(const std::string &uid) { this->user_id_ = uid; }
  void set_auto_connect(bool v) { this->auto_connect_ = v; }
  /// Credentials slot in the "ecoflow" NVS namespace (serial key "serial"
  /// for slot 0, "serial<N>" otherwise; user_id shared). -1 = not used.
  void set_slot(int8_t slot) { this->slot_ = slot; }
  int8_t slot() const { return this->slot_; }
  /// NVS key holding the serial of a slot.
  static std::string serial_key(uint8_t slot) { return slot == 0 ? "serial" : "serial" + std::to_string(slot); }

  // ── identity / discovery ───────────────────────────────────────────
  const std::string &device_sn() const { return this->dev_sn_; }
  /// "Delta 3 Plus", "River 2", … or "Unknown (P999)" / "" without serial.
  std::string model_name() const;
  EfFamily family() const { return this->model_ ? this->model_->family : EfFamily::UNKNOWN; }
  bool is_supported() const { return this->family() != EfFamily::UNKNOWN; }
  bool is_found() const { return this->found_; }
  uint64_t address() const { return this->address_; }
  int rssi() const { return this->rssi_; }
  const std::string &adv_name() const { return this->adv_name_; }
  /// Session encryption type advertised by the unit (0, 1 or 7).
  uint8_t encrypt_type() const { return this->encrypt_type_; }

  // ── link ───────────────────────────────────────────────────────────
  bool is_connected() const { return this->state_ != EfState::IDLE; }
  bool is_authenticated() const { return this->state_ == EfState::AUTHENTICATED; }
  EfState state() const { return this->state_; }
  const char *state_name() const;
  /// Reason of the last failure ("" if none).
  const std::string &last_error() const { return this->last_error_; }

  // ── outputs ────────────────────────────────────────────────────────
  bool supports(EfOutput out) const;
  /// Turn an output on/off. False if not authenticated or the model has
  /// no such output.
  bool set_output(EfOutput out, bool on);
  /// Built-in light (River 3 Plus). False if unsupported / not authenticated.
  bool supports_light() const { return this->model_ != nullptr && (this->model_->features & FEAT_LIGHT); }
  bool set_light(EfLightMode mode);
  static const char *light_mode_name(EfLightMode mode);
  static const char *output_name(EfOutput out);
  /// The model's outputs, e.g. "ac, usb, dc".
  std::string outputs_str() const;

  /// GEN3 only: raw ConfigWrite with a single bool / varint field.
  bool send_config(uint16_t field, bool value) { return this->send_config_varint(field, value ? 1 : 0); }
  bool send_config_varint(uint16_t field, uint32_t value);

  // ── nearby units (setup helper) ────────────────────────────────────
  /// Collect every EcoFlow unit heard (serial, model, RSSI), strongest first.
  // ── telemetry (Delta 3 / River 3 / Delta Pro 3) ────────────────────
  // The unit streams protobuf DisplayPropertyUpload packets (only changed
  // fields); the values below keep the latest of each. Empty optional =
  // not received yet (or another model family). Cleared on disconnect.
  bool has_telemetry() const { return this->tm_have_ != 0; }
  uint32_t telemetry_age_ms() const { return this->tm_last_ == 0 ? UINT32_MAX : millis() - this->tm_last_; }
  optional<float> battery_level() const { return this->tm_float_(262); }          // % (cms_batt_soc)
  optional<float> input_power() const { return this->tm_float_(3); }              // W total in
  optional<float> output_power() const { return this->tm_float_(4); }             // W total out
  optional<float> ac_input_power() const { return this->tm_float_(54); }          // W
  optional<float> ac_output_power() const { return this->tm_abs_(368); }          // W
  optional<float> dc_output_power() const { return this->tm_abs_(37); }           // W, 12 V
  optional<float> usb_c_output_power() const { return this->tm_abs_(11); }        // W, USB-C 1
  optional<float> usb_a_output_power() const { return this->tm_abs_(9); }         // W, USB-A 1
  optional<float> solar_input_power() const { return this->tm_float_(361); }      // W, PV / DC in
  optional<uint32_t> charge_remaining_min() const { return this->tm_uint_(269); }
  optional<uint32_t> discharge_remaining_min() const { return this->tm_uint_(268); }
  optional<int32_t> cell_temperature() const;                                     // °C (bms_max_cell_temp)
  optional<uint32_t> charge_limit() const { return this->tm_uint_(270); }         // % (cms_max_chg_soc)
  optional<uint32_t> discharge_limit() const { return this->tm_uint_(271); }      // % (cms_min_dsg_soc)
  optional<bool> ac_plugged_in() const;                                           // AC charger connected
  optional<uint32_t> error_code() const { return this->tm_uint_(1); }
  /// Real output state from the unit's flow info (empty = not known yet).
  optional<bool> output_state(EfOutput out) const;
  /// Current light mode (River 3 Plus), 0..3 = EfLightMode.
  optional<uint32_t> light_mode() const { return this->tm_uint_(357); }
  /// Raw 32-bit value of a tracked DisplayPropertyUpload field.
  optional<uint32_t> telemetry_raw(uint16_t field) const;

  void set_collect_nearby(bool v) { this->collect_nearby_ = v; }
  bool is_collecting_nearby() const { return this->collect_nearby_; }
  std::vector<NearbyUnit> nearby() const;
  /// Model name for any serial ("Unknown (XXXX)" if not in the table).
  static std::string model_for_serial(const std::string &sn);

 protected:
  // ── session setup ────────────────────────────────────────────────
  void start_session_();
  void start_ecdh_();
  void handle_pubkey_response_(const uint8_t *payload, size_t len);
  void handle_keyinfo_response_(const uint8_t *payload, size_t len);
  void send_auth_status_();
  void send_auto_auth_();

  // ── wire helpers ─────────────────────────────────────────────────
  std::vector<uint8_t> build_packet_(uint8_t src, uint8_t dst, uint8_t cmd_set, uint8_t cmd_id,
                                     const uint8_t *payload, size_t payload_len, uint8_t version);
  /// Unencrypted 5A5A command frame (type-7 key exchange).
  std::vector<uint8_t> frame_command_(const uint8_t *payload, size_t len);
  /// Encode an inner packet for the session's encryption type and write it.
  bool send_packet_(const std::vector<uint8_t> &inner);
  bool write_char_(const std::vector<uint8_t> &data);
  uint8_t packet_version_() const { return this->family() == EfFamily::GEN2 ? 0x02 : 0x03; }

  // ── inbound ──────────────────────────────────────────────────────
  void on_notify_enc_(const uint8_t *data, size_t len);  // type 7: 5A5A frames
  void on_notify_raw_(const uint8_t *data, size_t len);  // type 0/1: AA frames
  void dispatch_enc_payload_(const uint8_t *payload, size_t len);
  void handle_inner_packet_(const uint8_t *data, size_t len);

  // ── crypto ───────────────────────────────────────────────────────
  static void md5_(const uint8_t *in, size_t len, uint8_t out[16]);
  void aes_cbc_(bool encrypt, const uint8_t *key, const uint8_t *iv, const uint8_t *in, size_t len,
                uint8_t *out);

  void reset_link_();
  void on_telemetry_(const uint8_t *payload, size_t len);
  optional<float> tm_float_(uint16_t field) const;
  optional<float> tm_abs_(uint16_t field) const;
  optional<uint32_t> tm_uint_(uint16_t field) const { return this->telemetry_raw(field); }

  static constexpr size_t TM_FIELDS = 24;
  uint32_t tm_val_[TM_FIELDS]{};
  uint32_t tm_have_{0};  // bit i = tm_val_[i] received
  uint32_t tm_last_{0};
  uint32_t tm_packets_{0};
  void fail_(const char *why);

  // ── identity ─────────────────────────────────────────────────────
  std::string dev_sn_;
  std::string user_id_;
  const ModelInfo *model_{nullptr};
  bool auto_connect_{true};
  int8_t slot_{-1};

  uint32_t shutdown_at_{0};  // millis() of the shutdown disconnect, 0 = none
  bool found_{false};
  uint64_t address_{0};
  int rssi_{0};
  std::string adv_name_;
  uint8_t encrypt_type_{7};

  // ── GATT handles ─────────────────────────────────────────────────
  uint16_t write_handle_{0};
  uint16_t notify_handle_{0};
  uint16_t cccd_handle_{0};
  uint8_t write_props_{0};

  // ── session keys ─────────────────────────────────────────────────
  uint8_t priv_key_[21]{};
  uint8_t pub_key_[40]{};
  uint8_t session_key_[16]{};
  uint8_t iv_[16]{};
  bool have_session_{false};

  EfState state_{EfState::IDLE};
  std::string last_error_;
  std::vector<uint8_t> rx_buf_;

  bool collect_nearby_{false};
  std::vector<NearbyUnit> nearby_;

  static std::vector<EcoFlowBLE *> &instances_() {
    static std::vector<EcoFlowBLE *> v;
    return v;
  }
};

template<typename... Ts> class SetLightAction : public Action<Ts...>, public Parented<EcoFlowBLE> {
 public:
  TEMPLATABLE_VALUE(uint8_t, mode)
  void play(const Ts &...x) override { this->parent_->set_light((EfLightMode) this->mode_.value(x...)); }
};

template<typename... Ts> class SetOutputAction : public Action<Ts...>, public Parented<EcoFlowBLE> {
 public:
  void set_output(EfOutput out) { this->out_ = out; }
  TEMPLATABLE_VALUE(bool, state)
  void play(const Ts &...x) override { this->parent_->set_output(this->out_, this->state_.value(x...)); }

 protected:
  EfOutput out_{OUT_AC};
};

}  // namespace ecoflow_ble
}  // namespace esphome

#endif  // USE_ESP32
