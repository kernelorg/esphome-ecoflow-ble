#pragma once

#include "esphome/core/component.h"
#include "esphome/core/defines.h"

#ifdef USE_ESP32

#include "esp_http_server.h"
#ifdef USE_EF_PROVISION_ECOFLOW
#include "esphome/components/ecoflow_ble/ecoflow_ble.h"
#endif
#include <string>
#include <vector>
#include <cstdint>

namespace esphome {
namespace ef_provision {

// Provisioning flow steps (surfaced to the display).
enum class ProvStep : uint8_t {
  WAIT_WIFI = 0,    // waiting for Wi-Fi (credentials via Improv BLE / wifi: networks)
  AWAIT_LOGIN = 1,  // web server up; waiting for email/password
  AWAIT_DEVICE = 2, // logged in; waiting for device serial choice
  DONE = 3,         // user_id + serial saved; ask user to reboot
};

class EfProvision : public Component {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::LATE; }

  // Only act when in provisioning mode (set from on_boot). Enables the
  // ESPHome wifi component if it was left off (enable_on_boot: false).
  void set_active(bool a) { this->active_ = a; }
  bool is_active() const { return this->active_; }
  /// Erase the stored Wi-Fi network whenever setup mode starts (default),
  /// so Improv BLE always asks for the network to use.
  void set_forget_wifi_on_setup(bool v) { this->forget_wifi_on_setup_ = v; }
  /// Decide the mode at boot (setup / normal), enable BLE and start the scan.
  void set_auto_start(bool v) { this->auto_start_ = v; }
  /// Reboot into setup mode. With forget_wifi the Wi-Fi network stored by
  /// Improv is erased first, so Improv BLE starts right away.
  void start_setup(bool forget_wifi = true);
  /// Leave setup mode: clear the re-provision flag and reboot (BLE mode if
  /// credentials are stored, otherwise back to setup).
  void exit_setup();
  /// Erase the Wi-Fi network saved by Improv / save_wifi_sta().
  static void forget_wifi();
#ifdef USE_EF_PROVISION_ECOFLOW
  /// Offer the EcoFlow units heard over BLE on the serial page.
  void set_ecoflow(ecoflow_ble::EcoFlowBLE *ef) { this->ecoflow_ = ef; }
#endif

  // ── display accessors ──
  ProvStep step() const { return this->step_; }
  bool wifi_up() const { return this->got_ip_; }
  std::string ip() const { return this->ip_; }
  const std::string &message() const { return this->message_; }

  // ── httpd request handlers (called from static trampolines) ──
  esp_err_t on_root(httpd_req_t *req);
  esp_err_t on_login(httpd_req_t *req);
  esp_err_t on_serial(httpd_req_t *req);
  esp_err_t on_reboot(httpd_req_t *req);

 protected:
  void start_server_();
  /// Units this firmware can control (number of ecoflow_ble blocks).
  size_t slot_count() const;

  // EcoFlow cloud login. Returns true and fills user_id_/host_ on success.
  bool cloud_login_(const std::string &email, const std::string &password);

  std::string render_login_(const char *note);
  std::string render_serial_(const char *note);
  std::string render_done_();

  bool active_{false};
  bool auto_start_{true};
  bool auto_started_{false};
  uint32_t ble_up_at_{0};  // when BLE was first seen active in setup mode
  bool wifi_enabled_{false};
  bool forget_wifi_on_setup_{true};
  bool got_ip_{false};
  bool server_started_{false};
  ProvStep step_{ProvStep::WAIT_WIFI};

  std::string message_;
  std::string ip_;
  std::string user_id_;
  std::string host_;  // api host that accepted the login
  std::vector<std::string> saved_;  // serials stored by the last Save

  httpd_handle_t server_{nullptr};
#ifdef USE_EF_PROVISION_ECOFLOW
  ecoflow_ble::EcoFlowBLE *ecoflow_{nullptr};
  bool scan_started_{false};
#endif
};

}  // namespace ef_provision
}  // namespace esphome

#endif  // USE_ESP32
