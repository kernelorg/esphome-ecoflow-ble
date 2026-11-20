// Shared NVS accessors for EcoFlow provisioning (esp-idf nvs API).
// Namespace "ecoflow": keys user_id (str), serial (str), force_prov (u8).
// Shipped inside the ef_provision component; ESPHome force-includes
// component headers, so YAML lambdas can call ef_nvs::* directly. NVS flash is already
// initialised by ESPHome's preferences layer, so we just open the ns.
#pragma once

#include "esphome/core/defines.h"

#ifdef USE_ESP32

#include <nvs.h>
#include <string>

namespace ef_nvs {

inline std::string get_str(const char *key) {
  std::string out;
  nvs_handle_t h;
  if (nvs_open("ecoflow", NVS_READONLY, &h) != ESP_OK)
    return out;
  size_t len = 0;
  if (nvs_get_str(h, key, nullptr, &len) == ESP_OK && len > 0) {
    out.resize(len);
    if (nvs_get_str(h, key, &out[0], &len) == ESP_OK && len > 0)
      out.resize(len - 1);  // nvs length includes the trailing NUL
    else
      out.clear();
  }
  nvs_close(h);
  return out;
}

inline void set_str(const char *key, const std::string &v) {
  nvs_handle_t h;
  if (nvs_open("ecoflow", NVS_READWRITE, &h) != ESP_OK)
    return;
  nvs_set_str(h, key, v.c_str());
  nvs_commit(h);
  nvs_close(h);
}

inline uint8_t get_u8(const char *key, uint8_t def = 0) {
  nvs_handle_t h;
  uint8_t v = def;
  if (nvs_open("ecoflow", NVS_READONLY, &h) != ESP_OK)
    return def;
  nvs_get_u8(h, key, &v);
  nvs_close(h);
  return v;
}

inline void set_u8(const char *key, uint8_t v) {
  nvs_handle_t h;
  if (nvs_open("ecoflow", NVS_READWRITE, &h) != ESP_OK)
    return;
  nvs_set_u8(h, key, v);
  nvs_commit(h);
  nvs_close(h);
}

inline bool get_bool(const char *key) { return get_u8(key) != 0; }

inline void set_bool(const char *key, bool v) {
  nvs_handle_t h;
  if (nvs_open("ecoflow", NVS_READWRITE, &h) != ESP_OK)
    return;
  nvs_set_u8(h, key, v ? 1 : 0);
  nvs_commit(h);
  nvs_close(h);
}

inline bool provisioned() {
  return !get_str("user_id").empty() && !get_str("serial").empty();
}

inline bool want_provisioning() { return !provisioned() || get_bool("force_prov"); }

}  // namespace ef_nvs

#endif  // USE_ESP32
