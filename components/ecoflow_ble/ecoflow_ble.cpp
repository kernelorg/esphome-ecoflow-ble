#include "ecoflow_ble.h"

#ifdef USE_ESP32

#include "esphome/core/log.h"
#include "keydata.h"

#include "uECC.h"
// esp-idf guaranteed-linked crypto: ROM MD5 (always present) and the
// hardware AES the TLS stack uses. The software mbedtls_md5_*/mbedtls_aes_*
// API is replaced by *_ALT (hardware) on esp-idf and never linked.
#include "esp_rom_md5.h"
#include "aes/esp_aes.h"
#include <esp_random.h>
#include <nvs.h>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace {
constexpr int EF_AES_ENCRYPT = 1;  // mbedtls/esp_aes convention
constexpr int EF_AES_DECRYPT = 0;
}  // namespace

namespace esphome {
namespace ecoflow_ble {

static const char *const TAG = "ecoflow_ble";

// ── Models ───────────────────────────────────────────────────────────────
// Serial-number prefixes and output commands follow rabits/ha-ef-ble.
#define O(x) (1u << (x))
static const uint8_t GEN3_STD = O(OUT_AC) | O(OUT_USB) | O(OUT_DC);
static const uint8_t GEN3_AC2 = GEN3_STD | O(OUT_AC2);
static const uint8_t GEN3_RIVER = O(OUT_AC) | O(OUT_DC);  // USB not switchable
static const uint8_t GEN3_PRO = O(OUT_AC) | O(OUT_AC_HV) | O(OUT_AC_LV) | O(OUT_USB) | O(OUT_DC);
static const uint8_t GEN2_DELTA = O(OUT_AC) | O(OUT_USB) | O(OUT_DC);
static const uint8_t GEN2_RIVER = O(OUT_AC) | O(OUT_DC);  // no USB command
#undef O

static const ModelInfo MODELS[] = {
    // ── Delta 3 / River 3 / Delta Pro 3: protobuf ConfigWrite ──
    {"P231", "Delta 3", EfFamily::GEN3, GEN3_STD, 0, 0},
    {"P351", "Delta 3 Plus", EfFamily::GEN3, GEN3_STD, 0, 0},
    {"P321", "Delta 3 Classic", EfFamily::GEN3, GEN3_STD, 0, 0},
    {"PR11", "Delta 3 Air", EfFamily::GEN3, GEN3_STD, 0, 0},
    {"PR12", "Delta 3 Air", EfFamily::GEN3, GEN3_STD, 0, 0},
    {"PR21", "Delta 3 Air", EfFamily::GEN3, GEN3_STD, 0, 0},
    {"D3N1", "Delta 3 Max", EfFamily::GEN3, GEN3_STD, 0, 0},
    {"D3M1", "Delta 3 Max Plus", EfFamily::GEN3, GEN3_AC2, 0, 0},
    {"D751", "Delta 3 Ultra", EfFamily::GEN3, GEN3_STD, 0, 0},
    {"D511", "Delta 3 Ultra Plus", EfFamily::GEN3, GEN3_AC2, 0, 0},
    {"R651", "River 3", EfFamily::GEN3, GEN3_RIVER, 0, 0},
    {"R653", "River 3", EfFamily::GEN3, GEN3_RIVER, 0, 0},
    {"R654", "River 3", EfFamily::GEN3, GEN3_RIVER, 0, 0},
    {"R655", "River 3", EfFamily::GEN3, GEN3_RIVER, 0, 0},
    {"R631", "River 3 Plus", EfFamily::GEN3, GEN3_RIVER, 0, FEAT_LIGHT},
    {"R634", "River 3 Plus", EfFamily::GEN3, GEN3_RIVER, 0, FEAT_LIGHT},
    {"R635", "River 3 Plus", EfFamily::GEN3, GEN3_RIVER, 0, FEAT_LIGHT},
    {"MR51", "Delta Pro 3", EfFamily::GEN3, GEN3_PRO, 0, 0},
    {"MR54", "Delta Pro 3", EfFamily::GEN3, GEN3_PRO, 0, 0},
    // ── Delta 2 / River 2: TLV commands, packet version 2 ──
    {"R331", "Delta 2", EfFamily::GEN2, GEN2_DELTA, 0x05, 0},
    {"R335", "Delta 2", EfFamily::GEN2, GEN2_DELTA, 0x05, 0},
    {"R351", "Delta 2 Max", EfFamily::GEN2, GEN2_DELTA, 0x04, 0},
    {"R354", "Delta 2 Max", EfFamily::GEN2, GEN2_DELTA, 0x04, 0},
    {"D361", "Delta 3 1500", EfFamily::GEN2, GEN2_DELTA, 0x05, 0},
    {"R601", "River 2", EfFamily::GEN2, GEN2_RIVER, 0x05, 0},
    {"R603", "River 2", EfFamily::GEN2, GEN2_RIVER, 0x05, 0},
    {"R611", "River 2 Max", EfFamily::GEN2, GEN2_RIVER, 0x05, 0},
    {"R613", "River 2 Max", EfFamily::GEN2, GEN2_RIVER, 0x05, 0},
    {"R621", "River 2 Pro", EfFamily::GEN2, GEN2_RIVER, 0x05, 0},
    {"R623", "River 2 Pro", EfFamily::GEN2, GEN2_RIVER, 0x05, 0},
};

// ConfigWrite (GEN3) protobuf field numbers — identical in pd335_sys,
// pr705 and mr521 ConfigWrite messages.
static const uint16_t FIELD_AC = 76;
static const uint16_t FIELD_AC2 = 377;
static const uint16_t FIELD_AC_HV = 15;
static const uint16_t FIELD_AC_LV = 16;
static const uint16_t FIELD_USB = 19;
static const uint16_t FIELD_DC = 18;
static const uint16_t FIELD_LED_MODE = 89;  // River 3 Plus light: 0 off, 1 dim, 2 bright, 3 SOS
static const uint16_t ECOFLOW_COMPANY_ID = 0xB5B5;

static const uint8_t AUTH_DST = 0x35;

// ── Nordic UART 128-bit UUIDs as little-endian byte arrays (ESP-IDF order) ──
static const uint8_t NUS_WRITE_128[16] = {0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0,
                                          0x93, 0xf3, 0xa3, 0xb5, 0x02, 0x00, 0x40, 0x6e};
static const uint8_t NUS_NOTIFY_128[16] = {0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0,
                                           0x93, 0xf3, 0xa3, 0xb5, 0x03, 0x00, 0x40, 0x6e};

// ── CRCs (match ha-ef-ble: crc8 CCITT poly 0x07, crc16 ARC poly 0xA001) ──
static uint8_t crc8(const uint8_t *d, size_t n) {
  uint8_t c = 0;
  for (size_t i = 0; i < n; i++) {
    c ^= d[i];
    for (int b = 0; b < 8; b++)
      c = (c & 0x80) ? (uint8_t) ((c << 1) ^ 0x07) : (uint8_t) (c << 1);
  }
  return c;
}
static uint16_t crc16(const uint8_t *d, size_t n) {
  uint16_t c = 0;
  for (size_t i = 0; i < n; i++) {
    c ^= d[i];
    for (int b = 0; b < 8; b++)
      c = (c & 1) ? (uint16_t) ((c >> 1) ^ 0xA001) : (uint16_t) (c >> 1);
  }
  return c;
}

static int ef_rng(uint8_t *dest, unsigned size) {
  esp_fill_random(dest, size);
  return 1;
}

static const char *auth_error_name(uint8_t code) {
  switch (code) {
    case 1: return "need refresh token";
    case 2: return "device internal error";
    case 3: return "device already bound to another account";
    case 4: return "device must be set up in the EcoFlow app first";
    case 5: return "app send data error";
    case 6: return "wrong key (check user_id / serial number)";
    case 7: return "maximum number of devices reached";
    default: return "unknown auth error";
  }
}

// ── lifecycle ────────────────────────────────────────────────────────────
static std::string nvs_str(nvs_handle_t h, const char *key) {
  size_t len = 0;
  if (nvs_get_str(h, key, nullptr, &len) != ESP_OK || len <= 1)
    return "";
  std::string out(len, '\0');
  if (nvs_get_str(h, key, &out[0], &len) != ESP_OK)
    return "";
  out.resize(len - 1);  // drop the terminating NUL
  return out;
}

// Set in on_shutdown() when a link was open; teardown() then waits a moment so
// the controller can send LL_TERMINATE_IND before the radio goes down.
static constexpr uint32_t DISCONNECT_GRACE_MS = 300;

void EcoFlowBLE::on_shutdown() {
  auto *client = this->parent();
  if (client == nullptr)
    return;
  bool linked = client->state() == espbt::ClientState::CONNECTING ||
                client->state() == espbt::ClientState::CONNECTED ||
                client->state() == espbt::ClientState::ESTABLISHED;
  client->set_enabled(false);  // no auto-reconnect; closes the GATT connection
  if (!linked)
    return;
  esp_err_t err = esp_ble_gap_disconnect(client->get_remote_bda());  // terminate the ACL link itself
  ESP_LOGI(TAG, "shutdown: disconnecting %s (%s)", this->dev_sn_.c_str(), esp_err_to_name(err));
  this->shutdown_at_ = millis();
  if (this->shutdown_at_ == 0)
    this->shutdown_at_ = 1;
}

bool EcoFlowBLE::teardown() {
  return this->shutdown_at_ == 0 || millis() - this->shutdown_at_ >= DISCONNECT_GRACE_MS;
}

void EcoFlowBLE::setup() {
  uECC_set_rng(&ef_rng);
  this->rx_buf_.reserve(256);
  if (this->slot_ < 0)
    return;
  // Credentials written by ef_provision. Not loaded while a re-provision is
  // pending (force_prov), so the unit stays idle during setup mode.
  nvs_handle_t h;
  if (nvs_open("ecoflow", NVS_READONLY, &h) != ESP_OK)
    return;
  uint8_t force = 0;
  nvs_get_u8(h, "force_prov", &force);
  if (!force) {
    std::string uid = nvs_str(h, "user_id");
    std::string sn = nvs_str(h, serial_key((uint8_t) this->slot_).c_str());
    if (!uid.empty() && !sn.empty()) {
      this->set_user_id(uid);
      this->set_device_sn(sn);
    }
  }
  nvs_close(h);
}

void EcoFlowBLE::dump_config() {
  ESP_LOGCONFIG(TAG,
                "EcoFlow BLE:\n"
                "  Slot:         %d\n"
                "  Serial:       %s\n"
                "  Model:        %s\n"
                "  Outputs:      %s\n"
                "  Auto connect: %s",
                this->slot_, this->dev_sn_.empty() ? "(not set)" : this->dev_sn_.c_str(), this->model_name().c_str(),
                this->outputs_str().c_str(), YESNO(this->auto_connect_));
}

static const ModelInfo *find_model(const std::string &sn) {
  for (const auto &m : MODELS) {
    size_t n = strlen(m.prefix);
    if (sn.size() >= n && sn.compare(0, n, m.prefix) == 0)
      return &m;
  }
  return nullptr;
}

std::string EcoFlowBLE::model_for_serial(const std::string &sn) {
  const ModelInfo *m = find_model(sn);
  if (m != nullptr)
    return m->name;
  return "Unknown (" + sn.substr(0, 4) + ")";
}

void EcoFlowBLE::set_device_sn(const std::string &sn) {
  this->dev_sn_ = sn;
  this->found_ = false;
  this->model_ = find_model(sn);
  if (!sn.empty() && this->model_ == nullptr)
    ESP_LOGW(TAG, "serial %s: unsupported model (prefix %.4s)", sn.c_str(), sn.c_str());
}

std::string EcoFlowBLE::model_name() const {
  if (this->model_ != nullptr)
    return this->model_->name;
  if (this->dev_sn_.empty())
    return "";
  return "Unknown (" + this->dev_sn_.substr(0, 4) + ")";
}

const char *EcoFlowBLE::state_name() const {
  switch (this->state_) {
    case EfState::IDLE: return "idle";
    case EfState::DISCOVERING: return "discovering";
    case EfState::ENABLING_NOTIFY: return "enabling-notify";
    case EfState::PUBKEY_SENT: return "pubkey-sent";
    case EfState::KEYINFO_SENT: return "keyinfo-sent";
    case EfState::AUTHSTATUS_SENT: return "authstatus-sent";
    case EfState::AUTH_SENT: return "auth-sent";
    case EfState::AUTHENTICATED: return "authenticated";
    case EfState::FAILED: return "failed";
  }
  return "?";
}

// ── discovery ────────────────────────────────────────────────────────────
bool EcoFlowBLE::parse_device(const espbt::ESPBTDevice &device) {
  if (this->collect_nearby_) {
    static const auto COMPANY = espbt::ESPBTUUID::from_uint16(ECOFLOW_COMPANY_ID);
    for (const auto &md : device.get_manufacturer_datas()) {
      const auto &d = md.data;
      if (!(md.uuid == COMPANY) || d.size() < 17)
        continue;
      std::string sn(d.begin() + 1, d.begin() + 17);
      bool ok = true;
      for (char c : sn)
        ok &= (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z');
      if (!ok)
        continue;
      NearbyUnit *u = nullptr;
      for (auto &n : this->nearby_)
        if (n.serial == sn)
          u = &n;
      if (u == nullptr) {
        if (this->nearby_.size() >= 8)
          break;
        this->nearby_.push_back({sn, model_for_serial(sn), 0, 0});
        u = &this->nearby_.back();
        ESP_LOGI(TAG, "nearby: %s (%s) rssi %d", sn.c_str(), u->model.c_str(), device.get_rssi());
      }
      u->rssi = device.get_rssi();
      u->last_seen = millis();
      break;
    }
  }
  if (this->dev_sn_.size() < 4)
    return false;
  for (const auto &md : device.get_manufacturer_datas()) {
    const auto &d = md.data;
    // Layout: [0] proto version, [1..16] serial (ASCII), [17] status,
    // [18] product type, …, [22] capability flags (bits 3..5 = encrypt type).
    if (d.size() < 1 + this->dev_sn_.size() ||
        memcmp(d.data() + 1, this->dev_sn_.data(), this->dev_sn_.size()) != 0)
      continue;
    this->rssi_ = device.get_rssi();
    std::string name = device.get_name().str();
    if (!name.empty())
      this->adv_name_ = name;
    uint8_t et = d.size() > 22 ? (uint8_t) ((d[22] >> 3) & 0x07) : 7;
    if (!this->found_) {
      this->found_ = true;
      this->address_ = device.address_uint64();
      this->encrypt_type_ = et;
      char mac[18];
      ESP_LOGI(TAG, "found %s (%s) at %s, encryption type %u", this->dev_sn_.c_str(), this->model_name().c_str(),
               device.address_str_to(mac), et);
      if (!this->is_supported())
        ESP_LOGW(TAG, "%s is not a supported model; outputs cannot be switched", this->model_name().c_str());
      if (this->auto_connect_) {
        this->parent()->set_address(this->address_);
        this->parent()->set_enabled(true);
      }
    } else if (this->state_ == EfState::IDLE) {
      this->encrypt_type_ = et;
    }
    return true;
  }
  return false;
}

// ── crypto helpers ───────────────────────────────────────────────────────
void EcoFlowBLE::md5_(const uint8_t *in, size_t len, uint8_t out[16]) {
  md5_context_t c;
  esp_rom_md5_init(&c);
  esp_rom_md5_update(&c, in, (uint32_t) len);
  esp_rom_md5_final(out, &c);
}

void EcoFlowBLE::aes_cbc_(bool encrypt, const uint8_t *key, const uint8_t *iv, const uint8_t *in, size_t len,
                          uint8_t *out) {
  esp_aes_context a;
  esp_aes_init(&a);
  esp_aes_setkey(&a, key, 128);
  uint8_t iv_copy[16];
  memcpy(iv_copy, iv, 16);
  esp_aes_crypt_cbc(&a, encrypt ? EF_AES_ENCRYPT : EF_AES_DECRYPT, len, iv_copy, in, out);
  esp_aes_free(&a);
}

// ── GATT event handling ──────────────────────────────────────────────────
void EcoFlowBLE::gattc_event_handler(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if,
                                     esp_ble_gattc_cb_param_t *param) {
  switch (event) {
    case ESP_GATTC_DISCONNECT_EVT:
    case ESP_GATTC_CLOSE_EVT: {
      if (this->state_ != EfState::IDLE)
        ESP_LOGI(TAG, "Link down (was %s)", this->state_name());
      this->reset_link_();
      break;
    }
    case ESP_GATTC_SEARCH_CMPL_EVT: {
      this->state_ = EfState::DISCOVERING;
      uint16_t conn = this->parent()->get_conn_id();
      uint16_t count = 0;
      esp_ble_gattc_get_attr_count(gattc_if, conn, ESP_GATT_DB_CHARACTERISTIC, 0x0001, 0xFFFF, 0, &count);
      if (count == 0) {
        this->fail_("no characteristics");
        break;
      }
      auto *elems = (esp_gattc_char_elem_t *) malloc(sizeof(esp_gattc_char_elem_t) * count);
      if (elems == nullptr) {
        this->fail_("oom char list");
        break;
      }
      uint16_t got = count;
      esp_ble_gattc_get_all_char(gattc_if, conn, 0x0001, 0xFFFF, elems, &got, 0);
      this->write_handle_ = 0;
      this->notify_handle_ = 0;
      for (uint16_t i = 0; i < got; i++) {
        const auto &e = elems[i];
        bool is_w = false, is_n = false;
        if (e.uuid.len == ESP_UUID_LEN_16) {
          if (e.uuid.uuid.uuid16 == 0x0002) is_w = true;
          if (e.uuid.uuid.uuid16 == 0x0003) is_n = true;
        } else if (e.uuid.len == ESP_UUID_LEN_128) {
          if (memcmp(e.uuid.uuid.uuid128, NUS_WRITE_128, 16) == 0) is_w = true;
          if (memcmp(e.uuid.uuid.uuid128, NUS_NOTIFY_128, 16) == 0) is_n = true;
        }
        ESP_LOGD(TAG, "  char handle=0x%04X props=0x%02X%s%s", e.char_handle, e.properties,
                 is_w ? " <WRITE>" : "", is_n ? " <NOTIFY>" : "");
        if (is_w) {
          this->write_handle_ = e.char_handle;
          this->write_props_ = e.properties;
        }
        if (is_n) this->notify_handle_ = e.char_handle;
      }
      // Fallback: pick by property if the known UUIDs weren't present
      // (e.g. Delta 3's own UART-style service).
      if (this->write_handle_ == 0 || this->notify_handle_ == 0) {
        for (uint16_t i = 0; i < got; i++) {
          const auto &e = elems[i];
          if (this->notify_handle_ == 0 &&
              (e.properties & (ESP_GATT_CHAR_PROP_BIT_NOTIFY | ESP_GATT_CHAR_PROP_BIT_INDICATE)))
            this->notify_handle_ = e.char_handle;
          if (this->write_handle_ == 0 &&
              (e.properties & (ESP_GATT_CHAR_PROP_BIT_WRITE | ESP_GATT_CHAR_PROP_BIT_WRITE_NR))) {
            this->write_handle_ = e.char_handle;
            this->write_props_ = e.properties;
          }
        }
      }
      free(elems);
      if (this->write_handle_ == 0 || this->notify_handle_ == 0) {
        this->fail_("write/notify characteristic not found");
        break;
      }
      auto *cccd = this->parent()->get_config_descriptor(this->notify_handle_);
      this->cccd_handle_ = (cccd != nullptr) ? cccd->handle : 0;
      ESP_LOGI(TAG, "chars: write=0x%04X notify=0x%04X cccd=0x%04X", this->write_handle_, this->notify_handle_,
               this->cccd_handle_);
      esp_ble_gattc_register_for_notify(gattc_if, this->parent()->get_remote_bda(), this->notify_handle_);
      break;
    }
    case ESP_GATTC_REG_FOR_NOTIFY_EVT: {
      this->state_ = EfState::ENABLING_NOTIFY;
      if (this->cccd_handle_ == 0) {
        // No CCCD found; some stacks still deliver notifications. Proceed.
        this->start_session_();
        break;
      }
      uint8_t v[2] = {0x01, 0x00};
      esp_ble_gattc_write_char_descr(gattc_if, this->parent()->get_conn_id(), this->cccd_handle_, sizeof(v), v,
                                     ESP_GATT_WRITE_TYPE_RSP, ESP_GATT_AUTH_REQ_NONE);
      break;
    }
    case ESP_GATTC_WRITE_DESCR_EVT: {
      if (this->state_ == EfState::ENABLING_NOTIFY)
        this->start_session_();
      break;
    }
    case ESP_GATTC_NOTIFY_EVT: {
      if (param->notify.handle != this->notify_handle_)
        break;
      if (this->encrypt_type_ == 7)
        this->on_notify_enc_(param->notify.value, param->notify.value_len);
      else
        this->on_notify_raw_(param->notify.value, param->notify.value_len);
      break;
    }
    default:
      break;
  }
}

// ── session setup ────────────────────────────────────────────────────────
void EcoFlowBLE::start_session_() {
  this->node_state = espbt::ClientState::ESTABLISHED;
  this->rx_buf_.clear();
  this->have_session_ = false;
  this->last_error_.clear();
  if (this->dev_sn_.empty() || this->user_id_.empty()) {
    this->fail_("serial number / user_id not set");
    return;
  }
  if (!this->is_supported())
    ESP_LOGW(TAG, "%s: unknown model — trying to authenticate anyway", this->model_name().c_str());
  ESP_LOGI(TAG, "session: %s, encryption type %u, packet v%u", this->model_name().c_str(), this->encrypt_type_,
           this->packet_version_());
  switch (this->encrypt_type_) {
    case 7:
      this->start_ecdh_();
      break;
    case 1: {
      // Static key: AES-128-CBC with key = md5(serial), iv = md5(reversed serial).
      this->md5_((const uint8_t *) this->dev_sn_.data(), this->dev_sn_.size(), this->session_key_);
      std::string rev(this->dev_sn_.rbegin(), this->dev_sn_.rend());
      this->md5_((const uint8_t *) rev.data(), rev.size(), this->iv_);
      this->have_session_ = true;
      this->send_auth_status_();
      this->send_auto_auth_();
      break;
    }
    case 0:
      // Plain packets, no session encryption.
      this->send_auth_status_();
      this->send_auto_auth_();
      break;
    default: {
      char why[48];
      snprintf(why, sizeof(why), "unsupported encryption type %u", this->encrypt_type_);
      this->fail_(why);
      break;
    }
  }
}

void EcoFlowBLE::start_ecdh_() {
  // Our SECP160r1 keypair; send the public key in an unencrypted frame.
  if (!uECC_make_key(this->pub_key_, this->priv_key_, uECC_secp160r1())) {
    this->fail_("uECC_make_key");
    return;
  }
  uint8_t payload[42];
  payload[0] = 0x01;
  payload[1] = 0x00;
  memcpy(payload + 2, this->pub_key_, 40);
  if (this->write_char_(this->frame_command_(payload, sizeof(payload)))) {
    this->state_ = EfState::PUBKEY_SENT;
    ESP_LOGI(TAG, "handshake: sent public key");
  }
}

void EcoFlowBLE::handle_pubkey_response_(const uint8_t *payload, size_t len) {
  if (len < 43) {
    this->fail_("short public-key response");
    return;
  }
  uint8_t secret[20];
  if (!uECC_shared_secret(payload + 3, this->priv_key_, secret, uECC_secp160r1())) {
    this->fail_("uECC_shared_secret");
    return;
  }
  // iv = md5(shared_secret); temporary session key = shared_secret[:16]
  this->md5_(secret, 20, this->iv_);
  memcpy(this->session_key_, secret, 16);
  uint8_t cmd = 0x02;  // getKeyInfo → sRand + seed
  if (this->write_char_(this->frame_command_(&cmd, 1))) {
    this->state_ = EfState::KEYINFO_SENT;
    ESP_LOGI(TAG, "handshake: sent getKeyInfo");
  }
}

void EcoFlowBLE::handle_keyinfo_response_(const uint8_t *payload, size_t len) {
  if (len < 1 || payload[0] != 0x02) {
    this->fail_("bad key-info response");
    return;
  }
  size_t elen = len - 1;
  size_t aligned = elen - (elen % 16);
  if (aligned < 32) {
    this->fail_("key-info response too short");
    return;
  }
  std::vector<uint8_t> dec(aligned);
  this->aes_cbc_(false, this->session_key_, this->iv_, payload + 1, aligned, dec.data());
  // sRand = dec[0:16], seed = dec[16:18]
  uint8_t seed0 = dec[16], seed1 = dec[17];
  uint32_t pos = (uint32_t) seed0 * 0x10 + (uint32_t) ((uint8_t) (seed1 - 1)) * 0x100;
  if (pos + 16 > EF_KEYDATA_LEN) {
    this->fail_("key seed out of range");
    return;
  }
  uint8_t md5in[32];
  memcpy(md5in, &EF_KEYDATA[pos], 16);
  memcpy(md5in + 16, dec.data(), 16);
  this->md5_(md5in, 32, this->session_key_);
  this->have_session_ = true;
  ESP_LOGI(TAG, "handshake: session key derived");
  this->send_auth_status_();
}

void EcoFlowBLE::send_auth_status_() {
  auto inner = this->build_packet_(0x21, AUTH_DST, 0x35, 0x89, nullptr, 0, this->packet_version_());
  if (this->send_packet_(inner)) {
    this->state_ = EfState::AUTHSTATUS_SENT;
    ESP_LOGI(TAG, "handshake: sent getAuthStatus");
  }
}

void EcoFlowBLE::send_auto_auth_() {
  // md5(user_id + serial) as UPPERCASE hex.
  std::string concat = this->user_id_ + this->dev_sn_;
  uint8_t digest[16];
  this->md5_((const uint8_t *) concat.data(), concat.size(), digest);
  uint8_t payload[32];
  static const char kHex[] = "0123456789ABCDEF";
  for (int i = 0; i < 16; i++) {
    payload[i * 2] = kHex[digest[i] >> 4];
    payload[i * 2 + 1] = kHex[digest[i] & 0x0F];
  }
  auto inner = this->build_packet_(0x21, AUTH_DST, 0x35, 0x86, payload, sizeof(payload), this->packet_version_());
  if (this->send_packet_(inner)) {
    this->state_ = EfState::AUTH_SENT;
    ESP_LOGI(TAG, "handshake: sent autoAuth");
  }
}

// ── outputs ──────────────────────────────────────────────────────────────
const char *EcoFlowBLE::output_name(EfOutput out) {
  switch (out) {
    case OUT_AC: return "ac";
    case OUT_AC2: return "ac2";
    case OUT_AC_HV: return "ac_hv";
    case OUT_AC_LV: return "ac_lv";
    case OUT_USB: return "usb";
    case OUT_DC: return "dc";
    default: return "?";
  }
}

bool EcoFlowBLE::supports(EfOutput out) const {
  return this->model_ != nullptr && out < OUT_COUNT && (this->model_->outputs & (1u << out)) != 0;
}

std::string EcoFlowBLE::outputs_str() const {
  std::string s;
  for (uint8_t o = 0; o < OUT_COUNT; o++) {
    if (!this->supports((EfOutput) o))
      continue;
    if (!s.empty())
      s += ", ";
    s += output_name((EfOutput) o);
  }
  return s.empty() ? std::string("none") : s;
}

bool EcoFlowBLE::set_output(EfOutput out, bool on) {
  if (!this->supports(out)) {
    ESP_LOGW(TAG, "%s has no output '%s'", this->model_name().c_str(), output_name(out));
    return false;
  }
  if (!this->is_authenticated()) {
    ESP_LOGW(TAG, "set_output %s: not authenticated (state=%s)", output_name(out), this->state_name());
    return false;
  }
  ESP_LOGI(TAG, "output %s -> %s", output_name(out), on ? "ON" : "OFF");
  if (this->family() == EfFamily::GEN3) {
    switch (out) {
      case OUT_AC:
        // Delta Pro 3 has no single AC switch: drive both banks.
        if (this->supports(OUT_AC_HV))
          return this->send_config(FIELD_AC_HV, on) && this->send_config(FIELD_AC_LV, on);
        return this->send_config(FIELD_AC, on);
      case OUT_AC2: return this->send_config(FIELD_AC2, on);
      case OUT_AC_HV: return this->send_config(FIELD_AC_HV, on);
      case OUT_AC_LV: return this->send_config(FIELD_AC_LV, on);
      case OUT_USB: return this->send_config(FIELD_USB, on);
      case OUT_DC: return this->send_config(FIELD_DC, on);
      default: return false;
    }
  }
  // GEN2: one TLV command per output (src 0x21, cmd_set 0x20, version 2).
  switch (out) {
    case OUT_AC: {
      // [enabled, xboost, …] — 0xFF leaves the other settings unchanged.
      uint8_t p[7] = {(uint8_t) (on ? 1 : 0), 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
      return this->send_packet_(this->build_packet_(0x21, this->model_->ac_dst, 0x20, 0x42, p, sizeof(p), 0x02));
    }
    case OUT_USB: {
      uint8_t p = on ? 1 : 0;
      return this->send_packet_(this->build_packet_(0x21, 0x02, 0x20, 0x22, &p, 1, 0x02));
    }
    case OUT_DC: {
      uint8_t p = on ? 1 : 0;
      return this->send_packet_(this->build_packet_(0x21, 0x05, 0x20, 0x51, &p, 1, 0x02));
    }
    default:
      return false;
  }
}

bool EcoFlowBLE::send_config_varint(uint16_t field, uint32_t value) {
  if (this->family() != EfFamily::GEN3) {
    ESP_LOGW(TAG, "send_config: ConfigWrite is not supported by %s", this->model_name().c_str());
    return false;
  }
  if (!this->is_authenticated()) {
    ESP_LOGW(TAG, "send_config: not authenticated (state=%s)", this->state_name());
    return false;
  }
  // protobuf3: one varint field (bool / uint32) => varint tag + varint value.
  uint8_t payload[12];
  size_t n = 0;
  uint32_t tag = (uint32_t) field << 3;  // wire type 0 (varint)
  while (tag >= 0x80) {
    payload[n++] = (uint8_t) (tag | 0x80);
    tag >>= 7;
  }
  payload[n++] = (uint8_t) tag;
  uint32_t v = value;
  while (v >= 0x80) {
    payload[n++] = (uint8_t) (v | 0x80);
    v >>= 7;
  }
  payload[n++] = (uint8_t) v;
  bool ok = this->send_packet_(this->build_packet_(0x20, 0x02, 0xFE, 0x11, payload, n, 0x13));
  ESP_LOGD(TAG, "ConfigWrite field=%u value=%u -> %s", field, (unsigned) value, ok ? "sent" : "write failed");
  return ok;
}

const char *EcoFlowBLE::light_mode_name(EfLightMode mode) {
  switch (mode) {
    case LIGHT_OFF: return "off";
    case LIGHT_DIM: return "dim";
    case LIGHT_BRIGHT: return "bright";
    case LIGHT_SOS: return "sos";
    default: return "?";
  }
}

bool EcoFlowBLE::set_light(EfLightMode mode) {
  if (!this->supports_light()) {
    ESP_LOGW(TAG, "%s has no controllable light", this->model_name().c_str());
    return false;
  }
  if (!this->is_authenticated()) {
    ESP_LOGW(TAG, "set_light: not authenticated (state=%s)", this->state_name());
    return false;
  }
  ESP_LOGI(TAG, "light -> %s", light_mode_name(mode));
  return this->send_config_varint(FIELD_LED_MODE, (uint32_t) mode);
}

std::vector<NearbyUnit> EcoFlowBLE::nearby() const {
  std::vector<NearbyUnit> v(this->nearby_);
  std::sort(v.begin(), v.end(), [](const NearbyUnit &a, const NearbyUnit &b) { return a.rssi > b.rssi; });
  return v;
}

// ── wire builders ─────────────────────────────────────────────────────────
std::vector<uint8_t> EcoFlowBLE::build_packet_(uint8_t src, uint8_t dst, uint8_t cmd_set, uint8_t cmd_id,
                                               const uint8_t *payload, size_t payload_len, uint8_t version) {
  std::vector<uint8_t> d;
  d.reserve(20 + payload_len);
  d.push_back(0xAA);
  d.push_back(version);
  d.push_back((uint8_t) (payload_len & 0xFF));
  d.push_back((uint8_t) ((payload_len >> 8) & 0xFF));
  d.push_back(crc8(d.data(), 4));  // header crc8
  d.push_back(0x0D);               // product byte (product_id >= 0)
  for (int i = 0; i < 4; i++)
    d.push_back(0x00);  // seq
  d.push_back(0x00);
  d.push_back(0x00);  // static zeroes
  d.push_back(src);
  d.push_back(dst);
  if ((version & 0x0F) >= 3) {
    d.push_back(0x01);  // dsrc
    d.push_back(0x01);  // ddst
  }
  d.push_back(cmd_set);
  d.push_back(cmd_id);
  for (size_t i = 0; i < payload_len; i++)
    d.push_back(payload[i]);
  uint16_t c = crc16(d.data(), d.size());
  d.push_back((uint8_t) (c & 0xFF));
  d.push_back((uint8_t) ((c >> 8) & 0xFF));
  return d;
}

std::vector<uint8_t> EcoFlowBLE::frame_command_(const uint8_t *payload, size_t len) {
  std::vector<uint8_t> d;
  d.reserve(8 + len);
  d.push_back(0x5A);
  d.push_back(0x5A);
  d.push_back(0x00);  // frame_type 0 (command) << 4
  d.push_back(0x01);
  uint16_t L = (uint16_t) (len + 2);
  d.push_back((uint8_t) (L & 0xFF));
  d.push_back((uint8_t) ((L >> 8) & 0xFF));
  d.insert(d.end(), payload, payload + len);
  uint16_t c = crc16(d.data(), d.size());
  d.push_back((uint8_t) (c & 0xFF));
  d.push_back((uint8_t) ((c >> 8) & 0xFF));
  return d;
}

bool EcoFlowBLE::send_packet_(const std::vector<uint8_t> &inner) {
  switch (this->encrypt_type_) {
    case 7: {
      // 5A5A protocol frame, AES-128-CBC with PKCS7 padding.
      size_t rem = inner.size() % 16;
      uint8_t padval = (uint8_t) (16 - rem);
      std::vector<uint8_t> buf(inner);
      buf.insert(buf.end(), padval, padval);
      std::vector<uint8_t> enc(buf.size());
      this->aes_cbc_(true, this->session_key_, this->iv_, buf.data(), buf.size(), enc.data());
      std::vector<uint8_t> d;
      d.reserve(8 + enc.size());
      d.push_back(0x5A);
      d.push_back(0x5A);
      d.push_back(0x10);  // frame_type 1 (protocol) << 4
      d.push_back(0x01);
      uint16_t L = (uint16_t) (enc.size() + 2);
      d.push_back((uint8_t) (L & 0xFF));
      d.push_back((uint8_t) ((L >> 8) & 0xFF));
      d.insert(d.end(), enc.begin(), enc.end());
      uint16_t c = crc16(d.data(), d.size());
      d.push_back((uint8_t) (c & 0xFF));
      d.push_back((uint8_t) ((c >> 8) & 0xFF));
      return this->write_char_(d);
    }
    case 1: {
      // 5-byte plaintext header + AES-128-CBC body, zero padded.
      std::vector<uint8_t> body(inner.begin() + 5, inner.end());
      body.resize((body.size() + 15) / 16 * 16, 0x00);
      std::vector<uint8_t> d(inner.begin(), inner.begin() + 5);
      d.resize(5 + body.size());
      this->aes_cbc_(true, this->session_key_, this->iv_, body.data(), body.size(), d.data() + 5);
      return this->write_char_(d);
    }
    default:
      return this->write_char_(inner);
  }
}

bool EcoFlowBLE::write_char_(const std::vector<uint8_t> &data) {
  if (this->parent() == nullptr || this->write_handle_ == 0)
    return false;
  // Write with response when the characteristic allows it (the stack then
  // splits long writes); otherwise write-without-response.
  esp_gatt_write_type_t type =
      (this->write_props_ & ESP_GATT_CHAR_PROP_BIT_WRITE) ? ESP_GATT_WRITE_TYPE_RSP : ESP_GATT_WRITE_TYPE_NO_RSP;
  esp_err_t err = esp_ble_gattc_write_char(this->parent()->get_gattc_if(), this->parent()->get_conn_id(),
                                           this->write_handle_, data.size(), (uint8_t *) data.data(), type,
                                           ESP_GATT_AUTH_REQ_NONE);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "write_char failed: %d", err);
    return false;
  }
  return true;
}

// ── inbound ──────────────────────────────────────────────────────────────
void EcoFlowBLE::on_notify_enc_(const uint8_t *data, size_t len) {
  this->rx_buf_.insert(this->rx_buf_.end(), data, data + len);
  while (this->rx_buf_.size() >= 8) {
    size_t start = 0;
    bool found = false;
    for (; start + 1 < this->rx_buf_.size(); start++) {
      if (this->rx_buf_[start] == 0x5A && this->rx_buf_[start + 1] == 0x5A) {
        found = true;
        break;
      }
    }
    if (!found) {
      this->rx_buf_.clear();
      return;
    }
    if (start > 0)
      this->rx_buf_.erase(this->rx_buf_.begin(), this->rx_buf_.begin() + start);
    if (this->rx_buf_.size() < 8)
      return;
    uint16_t plen = this->rx_buf_[4] | (this->rx_buf_[5] << 8);  // includes 2-byte crc
    if (plen > 4096) {
      this->rx_buf_.erase(this->rx_buf_.begin(), this->rx_buf_.begin() + 2);
      continue;
    }
    size_t frame_end = 6 + plen;
    if (this->rx_buf_.size() < frame_end)
      return;
    uint16_t want = crc16(this->rx_buf_.data(), frame_end - 2);
    uint16_t have = this->rx_buf_[frame_end - 2] | (this->rx_buf_[frame_end - 1] << 8);
    if (want != have) {
      this->rx_buf_.erase(this->rx_buf_.begin(), this->rx_buf_.begin() + 2);
      continue;
    }
    std::vector<uint8_t> payload(this->rx_buf_.begin() + 6, this->rx_buf_.begin() + frame_end - 2);
    this->rx_buf_.erase(this->rx_buf_.begin(), this->rx_buf_.begin() + frame_end);
    this->dispatch_enc_payload_(payload.data(), payload.size());
  }
}

void EcoFlowBLE::dispatch_enc_payload_(const uint8_t *payload, size_t len) {
  switch (this->state_) {
    case EfState::PUBKEY_SENT:
      this->handle_pubkey_response_(payload, len);
      return;
    case EfState::KEYINFO_SENT:
      this->handle_keyinfo_response_(payload, len);
      return;
    default:
      break;
  }
  if (!this->have_session_)
    return;
  // Session frames carry an AES-CBC encrypted inner packet; only whole
  // blocks are decrypted (as the firmware does), then PKCS7 is stripped.
  size_t aligned = len - len % 16;
  if (aligned == 0)
    return;
  std::vector<uint8_t> dec(aligned);
  this->aes_cbc_(false, this->session_key_, this->iv_, payload, aligned, dec.data());
  uint8_t pad = dec.back();
  if (pad >= 1 && pad <= 16 && pad <= dec.size())
    dec.resize(dec.size() - pad);
  this->handle_inner_packet_(dec.data(), dec.size());
}

void EcoFlowBLE::on_notify_raw_(const uint8_t *data, size_t len) {
  this->rx_buf_.insert(this->rx_buf_.end(), data, data + len);
  while (!this->rx_buf_.empty()) {
    auto it = std::find(this->rx_buf_.begin(), this->rx_buf_.end(), (uint8_t) 0xAA);
    if (it == this->rx_buf_.end()) {
      this->rx_buf_.clear();
      return;
    }
    this->rx_buf_.erase(this->rx_buf_.begin(), it);
    if (this->rx_buf_.size() < 5)
      return;
    const uint8_t *d = this->rx_buf_.data();
    if (crc8(d, 4) != d[4]) {
      this->rx_buf_.erase(this->rx_buf_.begin());
      continue;
    }
    uint16_t plen = d[2] | (d[3] << 8);
    uint8_t ver = d[1];
    size_t overhead = ver == 4 ? 5 : ((ver & 0x0F) >= 3 ? 15 : 13);
    size_t inner_len = overhead + plen;
    size_t frame_len;
    if (this->encrypt_type_ == 1)
      frame_len = 5 + (inner_len + 15) / 16 * 16;
    else
      frame_len = ver == 4 ? 8 + plen + 2 : 5 + inner_len;
    if (this->rx_buf_.size() < frame_len)
      return;
    std::vector<uint8_t> pkt(this->rx_buf_.begin(), this->rx_buf_.begin() + 5);
    if (this->encrypt_type_ == 1) {
      std::vector<uint8_t> dec(frame_len - 5);
      this->aes_cbc_(false, this->session_key_, this->iv_, this->rx_buf_.data() + 5, dec.size(), dec.data());
      pkt.insert(pkt.end(), dec.begin(), dec.begin() + inner_len);
    } else {
      pkt.insert(pkt.end(), this->rx_buf_.begin() + 5, this->rx_buf_.begin() + frame_len);
    }
    this->rx_buf_.erase(this->rx_buf_.begin(), this->rx_buf_.begin() + frame_len);
    this->handle_inner_packet_(pkt.data(), pkt.size());
  }
}

void EcoFlowBLE::handle_inner_packet_(const uint8_t *d, size_t n) {
  if (n < 5 || d[0] != 0xAA)
    return;
  uint8_t ver_byte = d[1];
  uint8_t ver = ver_byte & 0x0F;
  bool parsed = false;
  uint8_t src = 0, cmd_set = 0, cmd_id = 0, first = 0;
  uint16_t plen = d[2] | (d[3] << 8);
  if (ver_byte != 4 && (ver == 2 || ver == 3)) {
    size_t payload_start = ver == 2 ? 16 : 18;
    if (n >= payload_start) {
      src = d[12];
      cmd_set = d[payload_start - 2];
      cmd_id = d[payload_start - 1];
      if (plen > 0 && n > payload_start) {
        first = d[payload_start];
        // High-nibble frames (e.g. 0x13) may XOR the payload with seq[0].
        if ((ver_byte & 0x10) && d[6] != 0)
          first ^= d[6];
      }
      parsed = true;
    }
  }
  switch (this->state_) {
    case EfState::AUTHSTATUS_SENT:
      // (type 7) reply to getAuthStatus → send the credentials.
      this->send_auto_auth_();
      break;
    case EfState::AUTH_SENT:
      if (parsed && src == AUTH_DST && cmd_set == 0x35 && cmd_id == 0x86) {
        if (plen > 0 && first != 0) {
          char why[96];
          snprintf(why, sizeof(why), "authentication rejected: %s (code %u)", auth_error_name(first), first);
          this->fail_(why);
          return;
        }
        ESP_LOGI(TAG, "AUTHENTICATED (%s)", this->model_name().c_str());
      } else if (parsed && cmd_set == 0x35 && cmd_id == 0x89) {
        break;  // late reply to getAuthStatus (type 0/1 send both at once)
      } else {
        ESP_LOGI(TAG, "AUTHENTICATED (first data packet, %s)", this->model_name().c_str());
      }
      this->state_ = EfState::AUTHENTICATED;
      break;
    case EfState::AUTHENTICATED:
      if (parsed && plen > 0) {
        size_t payload_start = ver == 2 ? 16 : 18;
        if (n < payload_start + plen)
          break;
        std::vector<uint8_t> payload(d + payload_start, d + payload_start + plen);
        // The unit XORs the payload with seq[0] (ha-ef-ble: xor_payload).
        if (d[6] != 0)
          for (auto &b : payload)
            b ^= d[6];
        if ((ver_byte & 0x10) && payload.size() >= 2 && payload[payload.size() - 1] == 0xBB &&
            payload[payload.size() - 2] == 0xBB)
          payload.resize(payload.size() - 2);
        if (src == 0x02 && cmd_set == 0xFE && cmd_id == 0x15) {
          this->on_telemetry_(payload.data(), payload.size());
        } else {
          ESP_LOGV(TAG, "rx src=0x%02X cmd=0x%02X/0x%02X len=%u", src, cmd_set, cmd_id, (unsigned) plen);
        }
      }
      break;
    default:
      break;
  }
}

// ── telemetry ──────────────────────────────────────────────────────────
// DisplayPropertyUpload fields kept (same numbers in pr705 / pd335_sys /
// mr521): see the accessors in ecoflow_ble.h.
static const uint16_t TM_FIELD_IDS[] = {1,   3,   4,   9,   11,  13,  33,  37,  48,  49,  54,  202,
                                        259, 262, 268, 269, 270, 271, 357, 361, 367, 368, 971};
static constexpr size_t TM_COUNT = sizeof(TM_FIELD_IDS) / sizeof(TM_FIELD_IDS[0]);
static_assert(TM_COUNT <= 24, "TM_FIELDS too small");

static int tm_index(uint16_t field) {
  for (size_t i = 0; i < TM_COUNT; i++)
    if (TM_FIELD_IDS[i] == field)
      return (int) i;
  return -1;
}

static bool read_varint(const uint8_t *&p, const uint8_t *end, uint64_t &out) {
  out = 0;
  for (int shift = 0; shift < 64 && p < end; shift += 7) {
    uint8_t b = *p++;
    out |= (uint64_t) (b & 0x7F) << shift;
    if (!(b & 0x80))
      return true;
  }
  return false;
}

void EcoFlowBLE::on_telemetry_(const uint8_t *payload, size_t len) {
  const uint8_t *p = payload, *end = payload + len;
  unsigned stored = 0;
  while (p < end) {
    uint64_t key;
    if (!read_varint(p, end, key))
      break;
    uint16_t field = key >> 3;
    uint8_t wt = key & 7;
    uint32_t v32 = 0;
    bool scalar = false;
    if (wt == 0) {
      uint64_t v;
      if (!read_varint(p, end, v))
        break;
      v32 = (uint32_t) v;  // int32 negatives keep their low 32 bits
      scalar = true;
    } else if (wt == 5) {
      if (end - p < 4)
        break;
      v32 = (uint32_t) p[0] | ((uint32_t) p[1] << 8) | ((uint32_t) p[2] << 16) | ((uint32_t) p[3] << 24);
      p += 4;
      scalar = true;
    } else if (wt == 1) {
      if (end - p < 8)
        break;
      p += 8;
    } else if (wt == 2) {
      uint64_t l;
      if (!read_varint(p, end, l) || (uint64_t) (end - p) < l)
        break;
      p += l;
    } else {
      break;  // groups: not used by these messages
    }
    if (!scalar)
      continue;
    int i = tm_index(field);
    if (i < 0)
      continue;
    this->tm_val_[i] = v32;
    this->tm_have_ |= 1u << i;
    stored++;
  }
  this->tm_last_ = millis() | 1u;
  bool first = this->tm_packets_++ == 0;
  auto f = [](optional<float> v) { return v.has_value() ? *v : NAN; };
  auto onoff = [](optional<bool> v) { return !v.has_value() ? "?" : *v ? "on" : "off"; };
  ESP_LOGD(TAG, "telemetry: %u bytes, %u fields kept", (unsigned) len, stored);
  if (first)
    ESP_LOGI(TAG, "telemetry: battery %.1f%%, in %.1f W, out %.1f W, AC %s, DC %s, USB %s", f(this->battery_level()),
             f(this->input_power()), f(this->output_power()), onoff(this->output_state(OUT_AC)),
             onoff(this->output_state(OUT_DC)), onoff(this->output_state(OUT_USB)));
}

optional<uint32_t> EcoFlowBLE::telemetry_raw(uint16_t field) const {
  int i = tm_index(field);
  if (i < 0 || !(this->tm_have_ & (1u << i)))
    return {};
  return this->tm_val_[i];
}

optional<float> EcoFlowBLE::tm_float_(uint16_t field) const {
  auto raw = this->telemetry_raw(field);
  if (!raw.has_value())
    return {};
  float f;
  uint32_t r = *raw;
  memcpy(&f, &r, 4);
  return f;
}

optional<float> EcoFlowBLE::tm_abs_(uint16_t field) const {
  auto f = this->tm_float_(field);
  if (!f.has_value())
    return {};
  return std::fabs(*f);  // outputs are reported as negative power
}

optional<int32_t> EcoFlowBLE::cell_temperature() const {
  auto raw = this->telemetry_raw(259);
  if (!raw.has_value())
    return {};
  return (int32_t) *raw;
}

optional<bool> EcoFlowBLE::ac_plugged_in() const {
  auto raw = this->telemetry_raw(202);
  if (!raw.has_value())
    return {};
  return *raw != 0;
}

optional<bool> EcoFlowBLE::output_state(EfOutput out) const {
  static const uint16_t FLOW[OUT_COUNT] = {367, 971, 48, 49, 13, 33};  // ac ac2 ac_hv ac_lv usb dc
  if (out >= OUT_COUNT)
    return {};
  auto raw = this->telemetry_raw(FLOW[out]);
  if (!raw.has_value())
    return {};
  // ha-ef-ble flow_is_on(): low two bits 0b10 / 0b11 = on.
  return (*raw & 0b11) >= 0b10;
}

void EcoFlowBLE::reset_link_() {
  this->state_ = EfState::IDLE;
  this->tm_have_ = 0;
  this->tm_last_ = 0;
  this->tm_packets_ = 0;
  this->have_session_ = false;
  this->write_handle_ = 0;
  this->notify_handle_ = 0;
  this->cccd_handle_ = 0;
  this->write_props_ = 0;
  this->rx_buf_.clear();
}

void EcoFlowBLE::fail_(const char *why) {
  ESP_LOGW(TAG, "%s (state=%s)", why, this->state_name());
  this->last_error_ = why;
  this->state_ = EfState::FAILED;
}

}  // namespace ecoflow_ble
}  // namespace esphome

#endif  // USE_ESP32
