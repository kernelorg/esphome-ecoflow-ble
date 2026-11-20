#include "ef_provision.h"
#include "ef_nvs.h"

#ifdef USE_ESP32

#include "esphome/core/application.h"
#include "esphome/core/log.h"

#include "esphome/components/network/ip_address.h"
#include "esphome/components/wifi/wifi_component.h"
#ifdef USE_ESP32_BLE
#include "esphome/components/esp32_ble/ble.h"
#endif
#ifdef USE_ESP32_BLE_TRACKER
#include "esphome/components/esp32_ble_tracker/esp32_ble_tracker.h"
#endif
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_system.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <nvs.h>
#include <algorithm>
#include <cstring>

namespace esphome {
namespace ef_provision {

static const char *const TAG = "ef_provision";

// EcoFlow regional API hosts tried in order during login.
static const char *const HOSTS[] = {"api", "api-e", "api-a"};

// ── small helpers ───────────────────────────────────────────────────────
static std::string base64_encode(const std::string &in) {
  static const char *T = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string out;
  out.reserve(((in.size() + 2) / 3) * 4);
  size_t i = 0;
  while (i + 3 <= in.size()) {
    uint32_t n = ((uint8_t) in[i] << 16) | ((uint8_t) in[i + 1] << 8) | (uint8_t) in[i + 2];
    out += T[(n >> 18) & 63];
    out += T[(n >> 12) & 63];
    out += T[(n >> 6) & 63];
    out += T[n & 63];
    i += 3;
  }
  size_t rem = in.size() - i;
  if (rem == 1) {
    uint32_t n = (uint8_t) in[i] << 16;
    out += T[(n >> 18) & 63];
    out += T[(n >> 12) & 63];
    out += "==";
  } else if (rem == 2) {
    uint32_t n = ((uint8_t) in[i] << 16) | ((uint8_t) in[i + 1] << 8);
    out += T[(n >> 18) & 63];
    out += T[(n >> 12) & 63];
    out += T[(n >> 6) & 63];
    out += '=';
  }
  return out;
}

static std::string html_escape(const std::string &s) {
  std::string o;
  for (char c : s) {
    switch (c) {
      case '&': o += "&amp;"; break;
      case '<': o += "&lt;"; break;
      case '>': o += "&gt;"; break;
      case '"': o += "&quot;"; break;
      default: o += c;
    }
  }
  return o;
}

static int hexval(char h) {
  if (h >= '0' && h <= '9') return h - '0';
  if (h >= 'a' && h <= 'f') return h - 'a' + 10;
  if (h >= 'A' && h <= 'F') return h - 'A' + 10;
  return 0;
}
static std::string url_decode(const std::string &s) {
  std::string o;
  for (size_t i = 0; i < s.size(); i++) {
    char c = s[i];
    if (c == '+') {
      o += ' ';
    } else if (c == '%' && i + 2 < s.size()) {
      o += (char) (hexval(s[i + 1]) * 16 + hexval(s[i + 2]));
      i += 2;
    } else {
      o += c;
    }
  }
  return o;
}
// All values of a (repeated) field, e.g. several checked checkboxes.
static std::vector<std::string> form_fields(const std::string &body, const std::string &key) {
  std::vector<std::string> out;
  std::string prefix = key + "=";
  size_t p = 0;
  while (p < body.size()) {
    size_t amp = body.find('&', p);
    std::string pair = body.substr(p, amp == std::string::npos ? std::string::npos : amp - p);
    if (pair.rfind(prefix, 0) == 0)
      out.push_back(url_decode(pair.substr(prefix.size())));
    if (amp == std::string::npos)
      break;
    p = amp + 1;
  }
  return out;
}

static std::string trim(std::string s) {
  while (!s.empty() && (s.front() == ' ' || s.front() == '\t'))
    s.erase(s.begin());
  while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r' || s.back() == '\n'))
    s.pop_back();
  return s;
}

// Pull one field from an application/x-www-form-urlencoded body.
static std::string form_field(const std::string &body, const std::string &key) {
  std::string prefix = key + "=";
  size_t p = 0;
  while (p < body.size()) {
    size_t amp = body.find('&', p);
    std::string pair = body.substr(p, amp == std::string::npos ? std::string::npos : amp - p);
    if (pair.rfind(prefix, 0) == 0)
      return url_decode(pair.substr(prefix.size()));
    if (amp == std::string::npos)
      break;
    p = amp + 1;
  }
  return "";
}
// Extract a string-valued JSON field by key (first match; good enough for the
// small flat login response). Returns "" if not a string value / not found.
static std::string json_str(const std::string &s, const std::string &key) {
  std::string pat = "\"" + key + "\"";
  size_t p = s.find(pat);
  if (p == std::string::npos)
    return "";
  p = s.find(':', p + pat.size());
  if (p == std::string::npos)
    return "";
  p++;
  while (p < s.size() && (s[p] == ' ' || s[p] == '\t'))
    p++;
  if (p >= s.size() || s[p] != '"')
    return "";
  p++;
  size_t e = s.find('"', p);
  if (e == std::string::npos)
    return "";
  return s.substr(p, e - p);
}

static std::string read_body(httpd_req_t *req) {
  std::string body;
  size_t remaining = req->content_len;
  char buf[256];
  while (remaining > 0) {
    int r = httpd_req_recv(req, buf, remaining < sizeof(buf) ? remaining : sizeof(buf));
    if (r <= 0)
      break;
    body.append(buf, r);
    remaining -= r;
  }
  return body;
}

// ── component lifecycle ─────────────────────────────────────────────────
// Wi-Fi itself is ESPHome's `wifi:` component. Its credentials come from
// wherever the config provides them — typically Improv BLE (`improv_ble:`),
// which lets a phone send the SSID/password of its own internet hotspot.
void EfProvision::setup() {
  ESP_LOGCONFIG(TAG, "EcoFlow provisioning (active=%d)", this->active_);
}

void EfProvision::loop() {
  if (this->auto_start_ && !this->auto_started_) {
    // First loop after setup: setup mode (Improv + setup page) when no
    // credentials are stored or setup was requested, otherwise normal mode.
    this->auto_started_ = true;
    bool setup = ef_nvs::want_provisioning();
    ESP_LOGI(TAG, "starting in %s", setup ? "setup mode" : "normal mode (BLE)");
#ifdef USE_ESP32_BLE_TRACKER
    // With continuous scanning set before BLE comes up, the tracker starts
    // the scan by itself as soon as the controller is ready.
    if (esp32_ble_tracker::global_esp32_ble_tracker != nullptr)
      esp32_ble_tracker::global_esp32_ble_tracker->set_scan_continuous(true);
#endif
#ifdef USE_ESP32_BLE
    if (esp32_ble::global_ble != nullptr)
      esp32_ble::global_ble->enable();
#endif
    if (setup)
      this->active_ = true;
  }
  if (!this->active_)
    return;
#ifdef USE_EF_PROVISION_ECOFLOW
  // Listen for EcoFlow advertisements so the serial page can list the
  // units nearby (BLE is already up for Improv in setup mode).
  if (this->ecoflow_ != nullptr && !this->scan_started_ && esp32_ble::global_ble != nullptr &&
      esp32_ble::global_ble->is_active() && esp32_ble_tracker::global_esp32_ble_tracker != nullptr) {
    // Continuous scanning (set at start) makes the tracker scan by itself
    // once BLE is up; start it here only if it is still idle after 2 s.
    auto *tracker = esp32_ble_tracker::global_esp32_ble_tracker;
    if (this->ble_up_at_ == 0)
      this->ble_up_at_ = millis() | 1u;
    this->ecoflow_->set_collect_nearby(true);
    tracker->set_scan_continuous(true);
    if (tracker->get_scanner_state() != esp32_ble_tracker::ScannerState::IDLE) {
      this->scan_started_ = true;
    } else if (millis() - this->ble_up_at_ > 2000) {
      this->scan_started_ = true;
      tracker->start_scan();
    }
  }
  if (this->scan_started_ && this->ble_up_at_ != 0) {
    this->ble_up_at_ = 0;
    ESP_LOGW(TAG, "scanning for EcoFlow units nearby");
  }
#endif
  auto *wifi = wifi::global_wifi_component;
  if (wifi == nullptr)
    return;
  if (!this->wifi_enabled_) {
    this->wifi_enabled_ = true;
    if (wifi->is_disabled()) {
      // Forget the previous network *before* the wifi component starts: it
      // loads stored credentials in start(), and Improv BLE only runs when
      // none are stored. The phone then sends the hotspot to use now.
      if (this->forget_wifi_on_setup_)
        EfProvision::forget_wifi();
      ESP_LOGW(TAG, "enabling Wi-Fi for provisioning");
      wifi->enable();
    }
    this->step_ = ProvStep::WAIT_WIFI;
    this->message_ = "Waiting for Wi-Fi (send credentials via Improv BLE)";
  }
  bool up = wifi->is_connected();
  if (up && !this->got_ip_) {
    for (auto &ip : wifi->wifi_sta_ip_addresses()) {
      if (ip.is_set() && ip.is_ip4()) {
        char buf[network::IP_ADDRESS_BUFFER_SIZE];
        this->ip_ = ip.str_to(buf);
        this->got_ip_ = true;
        ESP_LOGW(TAG, "Wi-Fi up, IP %s", this->ip_.c_str());
        break;
      }
    }
  } else if (!up && this->got_ip_) {
    this->got_ip_ = false;
    ESP_LOGW(TAG, "Wi-Fi lost");
  }
  if (this->got_ip_ && !this->server_started_)
    this->start_server_();
}

// ESPHome stores credentials received over Improv (save_wifi_sta) in the
// "esphome" NVS namespace under a fixed key when the config has no static
// `networks:` (wifi_component.cpp: hash 88491487; fast-connect cache +1).
static const char *const WIFI_PREF_KEYS[] = {"88491487", "88491488"};

void EfProvision::forget_wifi() {
  nvs_handle_t h;
  if (nvs_open("esphome", NVS_READWRITE, &h) != ESP_OK)
    return;
  for (const char *k : WIFI_PREF_KEYS)
    nvs_erase_key(h, k);
  nvs_commit(h);
  nvs_close(h);
  ESP_LOGW(TAG, "stored Wi-Fi network erased");
}

void EfProvision::start_setup(bool forget_wifi) {
  ef_nvs::set_bool("force_prov", true);
  if (forget_wifi && !this->forget_wifi_on_setup_)
    EfProvision::forget_wifi();  // otherwise done when setup mode starts
  ESP_LOGW(TAG, "rebooting into setup mode");
  App.safe_reboot();
}

void EfProvision::exit_setup() {
  ef_nvs::set_bool("force_prov", false);
  ESP_LOGW(TAG, "leaving setup mode");
  App.safe_reboot();
}

void EfProvision::dump_config() {
  ESP_LOGCONFIG(TAG, "EcoFlow provisioning: active=%d", this->active_);
}

// ── web server ──────────────────────────────────────────────────────────
static esp_err_t h_root(httpd_req_t *r) { return ((EfProvision *) r->user_ctx)->on_root(r); }
static esp_err_t h_login(httpd_req_t *r) { return ((EfProvision *) r->user_ctx)->on_login(r); }
static esp_err_t h_serial(httpd_req_t *r) { return ((EfProvision *) r->user_ctx)->on_serial(r); }
static esp_err_t h_reboot(httpd_req_t *r) { return ((EfProvision *) r->user_ctx)->on_reboot(r); }

void EfProvision::start_server_() {
  httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
  cfg.server_port = 80;
  cfg.lru_purge_enable = true;
  cfg.stack_size = 8192;
  if (httpd_start(&this->server_, &cfg) != ESP_OK) {
    ESP_LOGW(TAG, "httpd_start failed");
    return;
  }
  httpd_uri_t u = {};
  u.user_ctx = this;
  u.uri = "/"; u.method = HTTP_GET; u.handler = h_root;
  httpd_register_uri_handler(this->server_, &u);
  u.uri = "/login"; u.method = HTTP_POST; u.handler = h_login;
  httpd_register_uri_handler(this->server_, &u);
  u.uri = "/serial"; u.method = HTTP_POST; u.handler = h_serial;
  httpd_register_uri_handler(this->server_, &u);
  u.uri = "/reboot"; u.method = HTTP_POST; u.handler = h_reboot;
  httpd_register_uri_handler(this->server_, &u);
  this->server_started_ = true;
  this->step_ = ProvStep::AWAIT_LOGIN;
  this->message_ = "http://" + this->ip_ + "/";
  ESP_LOGW(TAG, "web server up at %s", this->message_.c_str());
}

static esp_err_t send_html(httpd_req_t *req, const std::string &body) {
  // Explicit charset: without it browsers may guess Windows-1251 etc.
  httpd_resp_set_type(req, "text/html; charset=utf-8");
  return httpd_resp_send(req, body.c_str(), body.size());
}

esp_err_t EfProvision::on_root(httpd_req_t *req) {
  switch (this->step_) {
    case ProvStep::DONE:         return send_html(req, this->render_done_());
    case ProvStep::AWAIT_DEVICE: return send_html(req, this->render_serial_(""));
    default:                     return send_html(req, this->render_login_(""));
  }
}

esp_err_t EfProvision::on_login(httpd_req_t *req) {
  std::string body = read_body(req);
  std::string email = form_field(body, "email");
  std::string pass = form_field(body, "password");
  if (email.empty() || pass.empty())
    return send_html(req, this->render_login_("Enter email and password"));
  ESP_LOGW(TAG, "login attempt for %s", email.c_str());
  if (this->cloud_login_(email, pass)) {
    nvs_handle_t h;
    if (nvs_open("ecoflow", NVS_READWRITE, &h) == ESP_OK) {
      nvs_set_str(h, "user_id", this->user_id_.c_str());
      nvs_commit(h);
      nvs_close(h);
    }
    this->step_ = ProvStep::AWAIT_DEVICE;
    return send_html(req, this->render_serial_(""));
  }
  return send_html(req, this->render_login_("Login failed — check credentials/region"));
}

size_t EfProvision::slot_count() const {
#ifdef USE_EF_PROVISION_ECOFLOW
  size_t n = ecoflow_ble::EcoFlowBLE::instance_count();
  return n > 0 ? std::min<size_t>(n, 8) : 1;
#else
  return 1;
#endif
}

static std::string slot_key(size_t slot) { return slot == 0 ? "serial" : "serial" + std::to_string(slot); }

esp_err_t EfProvision::on_serial(httpd_req_t *req) {
  std::string body = read_body(req);
  // Checked units first (in list order), then a manually typed serial.
  std::vector<std::string> serials;
  for (auto &v : form_fields(body, "sn")) {
    v = trim(v);
    if (!v.empty() && std::find(serials.begin(), serials.end(), v) == serials.end())
      serials.push_back(v);
  }
  std::string manual = trim(form_field(body, "serial"));
  for (auto &c : manual)
    c = (char) toupper((unsigned char) c);
  if (!manual.empty() && std::find(serials.begin(), serials.end(), manual) == serials.end())
    serials.push_back(manual);
  if (serials.empty())
    return send_html(req, this->render_serial_("Choose at least one unit"));
  for (auto &v : serials)
    if (v.size() < 6)
      return send_html(req, this->render_serial_("Serial looks too short"));
  size_t slots = this->slot_count();
  if (serials.size() > slots) {
    char why[96];
    snprintf(why, sizeof(why), "This firmware controls up to %u unit(s) — uncheck some", (unsigned) slots);
    return send_html(req, this->render_serial_(why));
  }
  nvs_handle_t h;
  if (nvs_open("ecoflow", NVS_READWRITE, &h) == ESP_OK) {
    for (size_t i = 0; i < 8; i++) {
      std::string key = slot_key(i);
      if (i < serials.size())
        nvs_set_str(h, key.c_str(), serials[i].c_str());
      else if (i > 0)
        nvs_erase_key(h, key.c_str());  // slot no longer used
    }
    if (!this->user_id_.empty())
      nvs_set_str(h, "user_id", this->user_id_.c_str());
    nvs_set_u8(h, "force_prov", 0);  // clear the re-provision flag
    nvs_commit(h);
    nvs_close(h);
  }
  this->saved_ = serials;
  for (size_t i = 0; i < serials.size(); i++)
    ESP_LOGW(TAG, "saved slot %u serial=%s", (unsigned) i, serials[i].c_str());
  ESP_LOGW(TAG, "user_id=%s", this->user_id_.c_str());
  this->step_ = ProvStep::DONE;
  return send_html(req, this->render_done_());
}

esp_err_t EfProvision::on_reboot(httpd_req_t *req) {
  send_html(req, "<html><body><h3>Rebooting...</h3></body></html>");
  vTaskDelay(pdMS_TO_TICKS(400));
  esp_restart();
  return ESP_OK;
}

// ── EcoFlow cloud login ─────────────────────────────────────────────────
static esp_err_t http_evt(esp_http_client_event_t *e) {
  if (e->event_id == HTTP_EVENT_ON_DATA && e->user_data != nullptr && e->data_len > 0)
    ((std::string *) e->user_data)->append((const char *) e->data, e->data_len);
  return ESP_OK;
}

bool EfProvision::cloud_login_(const std::string &email, const std::string &password) {
  // TLS needs ~30-40 KB of contiguous heap; log it to tell memory trouble
  // apart from network / certificate trouble.
  ESP_LOGW(TAG, "heap before TLS: free=%u largest=%u", (unsigned) heap_caps_get_free_size(MALLOC_CAP_8BIT),
           (unsigned) heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
  std::string b64 = base64_encode(password);
  std::string body = std::string("{\"scene\":\"IOT_APP\",\"appVersion\":\"1.0.0\",") +
                     "\"password\":\"" + b64 + "\"," +
                     "\"oauth\":{\"bundleId\":\"com.ef.EcoFlow\"}," +
                     "\"userType\":\"ECOFLOW\",\"email\":\"" + email + "\"}";

  for (const char *h : HOSTS) {
    std::string resp;
    std::string url = std::string("https://") + h + ".ecoflow.com/auth/login";
    esp_http_client_config_t cfg = {};
    cfg.url = url.c_str();
    cfg.crt_bundle_attach = esp_crt_bundle_attach;
    cfg.event_handler = http_evt;
    cfg.user_data = &resp;
    cfg.timeout_ms = 15000;
    cfg.method = HTTP_METHOD_POST;
    esp_http_client_handle_t c = esp_http_client_init(&cfg);
    esp_http_client_set_header(c, "Content-Type", "application/json");
    esp_http_client_set_header(c, "lang", "en_US");
    esp_http_client_set_post_field(c, body.c_str(), body.size());
    esp_err_t err = esp_http_client_perform(c);
    int status = esp_http_client_get_status_code(c);
    esp_http_client_cleanup(c);
    ESP_LOGW(TAG, "login %s -> err=%d HTTP %d (%d bytes)", h, (int) err, status,
             (int) resp.size());
    if (err != ESP_OK) {
      ESP_LOGW(TAG, "  %s, heap now free=%u largest=%u", esp_err_to_name(err),
               (unsigned) heap_caps_get_free_size(MALLOC_CAP_8BIT),
               (unsigned) heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
      continue;
    }
    std::string code = json_str(resp, "code");
    if (code != "0") {
      ESP_LOGW(TAG, "login rejected (code=%s)", code.c_str());
      continue;
    }
    std::string uid = json_str(resp, "userId");
    if (uid.empty())
      continue;
    this->user_id_ = uid;
    this->host_ = h;
    ESP_LOGW(TAG, "login OK host=%s userId=%s", h, uid.c_str());
    return true;
  }
  return false;
}

// ── HTML ────────────────────────────────────────────────────────────────
static const char *CSS =
    "<style>body{font-family:sans-serif;max-width:520px;margin:24px auto;padding:0 12px}"
    "input{font-size:1.1em;padding:8px;width:100%;box-sizing:border-box;margin:6px 0}"
    "button{font-size:1.1em;padding:10px 16px;margin-top:10px}"
    "label.u{display:block;border:1px solid #ccc;border-radius:6px;padding:8px;margin:6px 0}.n{color:#b00;font-weight:bold}.ok{color:#080}h2{margin-bottom:4px}</style>";
static const char *HEAD =
    "<html><head><meta charset='utf-8'><meta name=viewport content='width=device-width,initial-scale=1'>";

std::string EfProvision::render_login_(const char *note) {
  std::string n = (note && note[0]) ? std::string("<p class=n>") + note + "</p>" : "";
  return std::string(HEAD) + CSS +
         "</head><body><h2>EcoFlow setup</h2>"
         "<p>Step 1/2 — log in to your EcoFlow account.</p>" +
         n +
         "<form method=POST action=/login>"
         "<label>Email</label><input name=email type=email autocapitalize=off>"
         "<label>Password</label><input name=password type=password>"
         "<button>Log in</button></form></body></html>";
}

std::string EfProvision::render_serial_(const char *note) {
  std::string n = (note && note[0]) ? std::string("<p class=n>") + note + "</p>" : "";
  size_t slots = this->slot_count();
  char lim[96];
  snprintf(lim, sizeof(lim), "<p>This firmware controls up to <b>%u</b> unit(s).</p>", (unsigned) slots);
  std::string found;
#ifdef USE_EF_PROVISION_ECOFLOW
  if (this->ecoflow_ != nullptr) {
    auto units = this->ecoflow_->nearby();
    if (units.empty()) {
      found = "<p>No EcoFlow unit heard yet — make sure it is on and nearby, then "
              "<a href=/>refresh</a>.</p>";
    } else {
      found = "<p>Units nearby (strongest signal first):</p>";
      for (const auto &u : units) {
        char rssi[16];
        snprintf(rssi, sizeof(rssi), "%d dBm", u.rssi);
        found += "<label class=u><input type=checkbox name=sn value='" + html_escape(u.serial) + "'> " +
                 html_escape(u.model) + "<br><small>" + html_escape(u.serial) + " &middot; " + rssi +
                 "</small></label>";
      }
      found += "<p><a href=/>Refresh list</a></p>";
    }
  }
#endif
  return std::string(HEAD) + CSS + "</head><body><h2>EcoFlow setup</h2><p class=ok>Logged in (user " +
         html_escape(this->user_id_) + ").</p><p>Step 2/2 — choose the unit(s) to control.</p>" + lim + n +
         "<form method=POST action=/serial>" + found +
         "<label>Serial (optional, typed by hand)</label><input name=serial autocapitalize=characters "
         "placeholder='e.g. P351XXXXXXXXXXXX'>"
         "<button>Save</button></form></body></html>";
}

std::string EfProvision::render_done_() {
  std::string list;
  for (size_t i = 0; i < this->saved_.size(); i++)
    list += "<li>Unit " + std::to_string(i + 1) + ": " + html_escape(this->saved_[i]) + "</li>";
  return std::string(HEAD) + CSS + "</head><body><h2 class=ok>Done</h2><ul>" + list +
         "</ul><p>Credentials saved. Reboot the device to start the BLE stack "
         "and begin controlling your EcoFlow.</p>"
         "<form method=POST action=/reboot><button>Reboot now</button></form>"
         "</body></html>";
}

}  // namespace ef_provision
}  // namespace esphome

#endif  // USE_ESP32
