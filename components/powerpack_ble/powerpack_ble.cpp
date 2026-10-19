#include "powerpack_ble.h"

#ifdef USE_ESP32

#include "esphome/core/log.h"
#include <esp_gap_ble_api.h>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <map>

namespace esphome {
namespace powerpack_ble {

static const char *const TAG = "powerpack_ble";

void PowerPackBLE::setup() {
  // The telemetry char's data path demands an authenticated link (reads bounce with
  // ESP_GATT_INSUF_AUTHENTICATION 0x05, notifies never arrive). Desktop stacks auto-pair
  // when they see that; ESP-IDF must be configured for Just-Works bonding explicitly.
  uint8_t auth_req = ESP_LE_AUTH_BOND;
  uint8_t iocap = ESP_IO_CAP_NONE;
  uint8_t key_size = 16;
  uint8_t init_key = ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK;
  uint8_t rsp_key = ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK;
  esp_ble_gap_set_security_param(ESP_BLE_SM_AUTHEN_REQ_MODE, &auth_req, sizeof(auth_req));
  esp_ble_gap_set_security_param(ESP_BLE_SM_IOCAP_MODE, &iocap, sizeof(iocap));
  esp_ble_gap_set_security_param(ESP_BLE_SM_MAX_KEY_SIZE, &key_size, sizeof(key_size));
  esp_ble_gap_set_security_param(ESP_BLE_SM_SET_INIT_KEY, &init_key, sizeof(init_key));
  esp_ble_gap_set_security_param(ESP_BLE_SM_SET_RSP_KEY, &rsp_key, sizeof(rsp_key));
}

void PowerPackBLE::dump_config() {
  ESP_LOGCONFIG(TAG, "Enphase PowerPack BLE:");
  ESP_LOGCONFIG(TAG, "  service:        %s", this->service_uuid_str_.c_str());
  ESP_LOGCONFIG(TAG, "  characteristic: %s", this->char_uuid_str_.c_str());
  ESP_LOGCONFIG(TAG, "  control char:   %s", this->control_uuid_str_.c_str());
  ESP_LOGCONFIG(TAG, "  fields:         %u", (unsigned) this->fields_.size());
  ESP_LOGCONFIG(TAG, "  raw fields:     %u", (unsigned) this->raw_fields_.size());
}

// Find "key": in the JSON text and parse the number after it. Leading quote prevents
// suffix collisions (e.g. "ac_i" won't match inside "pcu_ac_i"). NAN if absent.
static float extract_num(const std::string &o, const std::string &key) {
  std::string pat = "\"" + key + "\":";
  size_t p = o.find(pat);
  if (p == std::string::npos)
    return NAN;
  p += pat.size();
  while (p < o.size() && o[p] == ' ')
    p++;
  return strtof(o.c_str() + p, nullptr);
}

// --- Raw JSON scanning (string-safe) ---
// Index just past the string starting at o[i] == '"' (handles backslash escapes).
static size_t skip_string(const std::string &o, size_t i) {
  size_t n = o.size();
  for (size_t j = i + 1; j < n; j++) {
    if (o[j] == '\\')
      j++;
    else if (o[j] == '"')
      return j + 1;
  }
  return n;
}

// Index just past the JSON value starting at o[j]: a string, a balanced object/array,
// or a bare token (number, true, false, null).
static size_t value_end(const std::string &o, size_t j) {
  size_t n = o.size();
  if (j >= n)
    return n;
  char c = o[j];
  if (c == '"')
    return skip_string(o, j);
  if (c == '{' || c == '[') {
    int depth = 0;
    while (j < n) {
      char d = o[j];
      if (d == '"') {
        j = skip_string(o, j);
        continue;
      }
      if (d == '{' || d == '[')
        depth++;
      else if (d == '}' || d == ']') {
        if (--depth == 0)
          return j + 1;
      }
      j++;
    }
    return n;
  }
  while (j < n && o[j] != ',' && o[j] != '}' && o[j] != ']' && o[j] != ' ' && o[j] != '\t' &&
         o[j] != '\r' && o[j] != '\n')
    j++;
  return j;
}

// Calls fn(key, raw_value_text) for every "key": value pair in the frame, at any depth.
// Raw text means string values keep their quotes and numbers keep their exact formatting.
template<typename F> static void for_each_kv(const std::string &o, F fn) {
  size_t i = 0, n = o.size();
  while (i < n) {
    if (o[i] != '"') {
      i++;
      continue;
    }
    size_t ke = skip_string(o, i);
    size_t j = ke;
    while (j < n && o[j] == ' ')
      j++;
    if (j >= n || o[j] != ':') {
      i = ke;  // a string value or array element, not a key
      continue;
    }
    j++;
    while (j < n && o[j] == ' ')
      j++;
    size_t ve = value_end(o, j);
    fn(o.substr(i + 1, ke - i - 2), o.substr(j, ve - j));
    // Step into objects/arrays so nested keys are visited too.
    i = (j < n && (o[j] == '{' || o[j] == '[')) ? j + 1 : ve;
  }
}

static bool is_mode_or_state_key(const std::string &k) {
  size_t n = k.size();
  return n > 2 && k[n - 2] == '_' && (k[n - 1] == 's' || k[n - 1] == 'm');
}

void PowerPackBLE::subscribe_() {
  auto svc = esp32_ble::ESPBTUUID::from_raw(this->service_uuid_str_);
  auto chr = esp32_ble::ESPBTUUID::from_raw(this->char_uuid_str_);
  auto *characteristic = this->parent()->get_characteristic(svc, chr);
  if (characteristic == nullptr) {
    ESP_LOGW(TAG, "telemetry characteristic %s not found in service %s",
             this->char_uuid_str_.c_str(), this->service_uuid_str_.c_str());
    return;
  }
  this->char_handle_ = characteristic->handle;
  ESP_LOGI(TAG, "found telemetry characteristic, handle 0x%04x — registering for notify",
           this->char_handle_);
  // Resolve the control (write) characteristic on the same service, if configured.
  if (!this->control_uuid_str_.empty()) {
    auto ctl_chr = esp32_ble::ESPBTUUID::from_raw(this->control_uuid_str_);
    auto *ctl = this->parent()->get_characteristic(svc, ctl_chr);
    if (ctl == nullptr) {
      ESP_LOGW(TAG, "control characteristic %s not found", this->control_uuid_str_.c_str());
    } else {
      this->control_handle_ = ctl->handle;
      ESP_LOGI(TAG, "found control characteristic, handle 0x%04x", this->control_handle_);
    }
  }
  // Kick off pairing/encryption in parallel; data only flows on an authenticated link.
  auto enc = esp_ble_set_encryption(this->parent()->get_remote_bda(), ESP_BLE_SEC_ENCRYPT_NO_MITM);
  ESP_LOGI(TAG, "requesting link encryption (status=%d)", enc);
  auto status = esp_ble_gattc_register_for_notify(this->parent()->get_gattc_if(),
                                                  this->parent()->get_remote_bda(),
                                                  this->char_handle_);
  if (status != ESP_OK)
    ESP_LOGW(TAG, "register_for_notify failed, status=%d", status);
}

void PowerPackBLE::gattc_event_handler(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if,
                                       esp_ble_gattc_cb_param_t *param) {
  switch (event) {
    case ESP_GATTC_SEARCH_CMPL_EVT: {
      // ESPHome's ble_client finished its (fast, cached) discovery; grab our one char.
      this->subscribe_();
      break;
    }
    case ESP_GATTC_REG_FOR_NOTIFY_EVT: {
      // BLEClientBase auto-writes the CCCD here; the write that actually sticks is the
      // post-pairing one in gap_event_handler (per-bond CCCD state resets on a fresh bond).
      ESP_LOGD(TAG, "REG_FOR_NOTIFY status=%d handle=0x%04x", param->reg_for_notify.status,
               param->reg_for_notify.handle);
      break;
    }
    case ESP_GATTC_WRITE_DESCR_EVT: {
      // The device's ACTUAL response to a CCCD write — a local queue-OK means nothing
      // if this reports e.g. insufficient authentication (0x05) or encryption (0x0f).
      ESP_LOGD(TAG, "WRITE_DESCR handle=0x%04x status=0x%02x", param->write.handle,
               param->write.status);
      if (param->write.status == ESP_GATT_OK && this->char_handle_ != 0) {
        // Some firmwares only start the notify stream after a read of the char.
        // NO_MITM auth req lets the stack escalate to encryption instead of failing 0x05.
        auto status = esp_ble_gattc_read_char(gattc_if, this->parent()->get_conn_id(),
                                              this->char_handle_, ESP_GATT_AUTH_REQ_NO_MITM);
        ESP_LOGD(TAG, "post-subscribe kick read queued, status=%d", status);
      }
      break;
    }
    case ESP_GATTC_READ_CHAR_EVT: {
      ESP_LOGD(TAG, "READ_CHAR handle=0x%04x status=0x%02x len=%d", param->read.handle,
               param->read.status, param->read.value_len);
      if (param->read.status == ESP_GATT_OK && param->read.handle == this->char_handle_ &&
          param->read.value_len > 0) {
        this->buf_.append((const char *) param->read.value, param->read.value_len);
        this->process_buffer_();
      }
      break;
    }
    case ESP_GATTC_WRITE_CHAR_EVT: {
      // The device's actual response to a control write — status 0 == accepted.
      // A non-zero status here means the unit rejected the setting (auth, format,
      // out-of-range value, …), which a local queue-OK would otherwise hide.
      ESP_LOGI(TAG, "control write result: handle=0x%04x status=0x%02x", param->write.handle,
               param->write.status);
      break;
    }
    case ESP_GATTC_NOTIFY_EVT: {
      ESP_LOGVV(TAG, "notify handle=0x%04x len=%d (want 0x%04x)", param->notify.handle,
               param->notify.value_len, this->char_handle_);
      if (param->notify.handle != this->char_handle_)
        break;
      this->buf_.append((const char *) param->notify.value, param->notify.value_len);
      if (this->buf_.size() > 16384)
        this->buf_.clear();
      this->process_buffer_();
      break;
    }
    case ESP_GATTC_DISCONNECT_EVT:
    case ESP_GATTC_CLOSE_EVT: {
      this->buf_.clear();
      this->char_handle_ = 0;
      this->control_handle_ = 0;
      break;
    }
    default:
      break;
  }
}

void PowerPackBLE::gap_event_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param) {
  switch (event) {
    case ESP_GAP_BLE_SEC_REQ_EVT: {
      // Peripheral asked for security — accept so Just-Works pairing can proceed.
      ESP_LOGI(TAG, "peer security request — accepting");
      esp_ble_gap_security_rsp(param->ble_security.ble_req.bd_addr, true);
      break;
    }
    case ESP_GAP_BLE_AUTH_CMPL_EVT: {
      auto &c = param->ble_security.auth_cmpl;
      if (!c.success) {
        ESP_LOGW(TAG, "pairing FAILED, reason=0x%02x", c.fail_reason);
        break;
      }
      ESP_LOGI(TAG, "pairing complete (bonded, auth_mode=%d) — re-writing CCCD on encrypted link",
               c.auth_mode);
      // CCCD state is per-bond: the fresh bond reset it to 0, wiping the pre-encryption
      // subscribe. Re-write it now; the WRITE_DESCR handler then kicks the read.
      if (this->char_handle_ != 0) {
        auto *descr = this->parent()->get_config_descriptor(this->char_handle_);
        if (descr == nullptr) {
          ESP_LOGW(TAG, "no CCCD for handle 0x%04x on re-subscribe", this->char_handle_);
          break;
        }
        uint16_t enable = 0x0001;
        esp_ble_gattc_write_char_descr(this->parent()->get_gattc_if(),
                                       this->parent()->get_conn_id(), descr->handle,
                                       sizeof(enable), (uint8_t *) &enable,
                                       ESP_GATT_WRITE_TYPE_RSP, ESP_GATT_AUTH_REQ_NO_MITM);
      }
      break;
    }
    default:
      break;
  }
}

void PowerPackBLE::process_buffer_() {
  // Extract every complete top-level {...} object from the growing buffer.
  while (true) {
    int depth = 0, start = -1, endp = -1;
    for (size_t i = 0; i < this->buf_.size(); i++) {
      char c = this->buf_[i];
      if (c == '{') {
        if (depth == 0)
          start = (int) i;
        depth++;
      } else if (c == '}') {
        depth--;
        if (depth == 0) {
          endp = (int) i;
          break;
        }
      }
    }
    if (endp < 0 || start < 0)
      break;
    std::string obj = this->buf_.substr(start, endp - start + 1);
    this->buf_.erase(0, endp + 1);
    this->publish_frame_(obj);
  }
}

void PowerPackBLE::publish_frame_(const std::string &json) {
  ESP_LOGV(TAG, "telemetry frame (%u bytes): %.60s...", (unsigned) json.size(), json.c_str());
  for (auto &field : this->fields_) {
    float v = extract_num(json, field.first);
    if (!std::isnan(v))
      field.second->publish_state(v);
  }
  // Remember the device's own non-zero grid input limit so "grid mode" can restore
  // it without us needing to know gicl's units or the correct "on" value.
  float gicl = extract_num(json, "gicl");
  if (!std::isnan(gicl) && gicl > 0)
    this->last_gicl_ = gicl;

  // Diagnostic: watch the candidate mode/setting fields and re-log only when one
  // changes. Toggling grid/battery on the unit's own display will flip exactly the
  // field(s) we need to write over BLE — this names them for us.
  static const char *const MODE_KEYS[] = {"op_m", "bat_m", "ac_m", "conn_m",
                                          "dmse", "slse", "gicl", "fchg", "usoe", "axoe"};
  std::string sig;
  for (auto *k : MODE_KEYS) {
    float v = extract_num(json, k);
    char part[24];
    if (std::isnan(v))
      snprintf(part, sizeof(part), "%s=? ", k);
    else
      snprintf(part, sizeof(part), "%s=%d ", k, (int) v);
    sig += part;
  }
  if (sig != this->mode_sig_) {
    ESP_LOGI(TAG, "MODE/SETTINGS: %s", sig.c_str());
    this->mode_sig_ = sig;
  }

  // Every key ending in _s or _m, as raw text so string values survive (strtof would
  // read them as 0). Sorted by key; first occurrence wins if a key repeats at depth.
  // Logged only when the signature changes. Also feeds the raw_fields text sensors.
  std::map<std::string, std::string> raw;
  for_each_kv(json, [&](const std::string &k, const std::string &v) {
    if (is_mode_or_state_key(k))
      raw.emplace(k, v);
    for (auto &rf : this->raw_fields_) {
      if (rf.key != k)
        continue;
      std::string text = v.size() > 255 ? v.substr(0, 255) : v;  // HA state limit
      if (!rf.published || text != rf.last) {
        rf.sensor->publish_state(text);
        rf.last = text;
        rf.published = true;
      }
    }
  });
  std::string raw_sig;
  for (auto &kv : raw) {
    if (!raw_sig.empty())
      raw_sig += ' ';
    raw_sig += kv.first;
    raw_sig += '=';
    raw_sig += kv.second;
  }
  if (raw_sig != this->raw_sig_) {
    ESP_LOGI(TAG, "RAW _s/_m: %s", raw_sig.empty() ? "(none)" : raw_sig.c_str());
    this->raw_sig_ = raw_sig;
  }
}

void PowerPackBLE::send_command_(const std::string &json) {
  if (this->control_handle_ == 0) {
    ESP_LOGW(TAG, "control write ignored — no control characteristic (not connected?)");
    return;
  }
  ESP_LOGI(TAG, "control write -> %s", json.c_str());
  auto status = esp_ble_gattc_write_char(
      this->parent()->get_gattc_if(), this->parent()->get_conn_id(), this->control_handle_,
      json.size(), (uint8_t *) json.data(), ESP_GATT_WRITE_TYPE_RSP, ESP_GATT_AUTH_REQ_NO_MITM);
  if (status != ESP_OK)
    ESP_LOGW(TAG, "control write queue failed, status=%d", status);
}

void PowerPackBLE::send_setting(const std::string &key, int value) {
  // Setting shape from the app: {"i": 1, "v": 1, "t": 8, "s": "<key>", "n": <value>}
  char buf[96];
  snprintf(buf, sizeof(buf), "{\"i\": 1, \"v\": 1, \"t\": 8, \"s\": \"%s\", \"n\": %d}",
           key.c_str(), value);
  this->send_command_(buf);
}

void PowerPackBLE::enable_battery_mode() {
  ESP_LOGI(TAG, "battery mode: gicl -> 0 (last grid limit seen: %.0f)", this->last_gicl_);
  this->send_setting("gicl", 0);
}

void PowerPackBLE::enable_grid_mode() {
  if (std::isnan(this->last_gicl_)) {
    ESP_LOGW(TAG, "grid mode: no non-zero gicl seen yet — cannot restore; skipping");
    return;
  }
  ESP_LOGI(TAG, "grid mode: gicl -> %.0f (restored)", this->last_gicl_);
  this->send_setting("gicl", (int) this->last_gicl_);
}

}  // namespace powerpack_ble
}  // namespace esphome

#endif  // USE_ESP32
