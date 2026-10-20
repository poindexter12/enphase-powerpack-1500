#pragma once

#include "esphome/core/component.h"
#include "esphome/components/ble_client/ble_client.h"
#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/components/esp32_ble/ble_uuid.h"

#include <string>
#include <vector>
#include <utility>
#include <cmath>

#ifdef USE_ESP32

namespace esphome {
namespace powerpack_ble {

// Reads the Enphase IQ PowerPack 1500's plaintext-JSON telemetry over BLE by subscribing
// DIRECTLY to the one known notify characteristic — instead of the stock ble_client's full
// GATT enumeration, which the device's huge (100+ char) database and short connection tolerance
// defeat. Data starts within ~1s, which keeps the link alive.
class PowerPackBLE : public Component, public ble_client::BLEClientNode {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::AFTER_BLUETOOTH; }

  void gattc_event_handler(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if,
                           esp_ble_gattc_cb_param_t *param) override;
  void gap_event_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param) override;

  void set_service_uuid(const std::string &s) { this->service_uuid_str_ = s; }
  void set_characteristic_uuid(const std::string &s) { this->char_uuid_str_ = s; }
  void set_control_characteristic_uuid(const std::string &s) { this->control_uuid_str_ = s; }
  void add_field(const std::string &json_key, sensor::Sensor *s) {
    this->fields_.emplace_back(json_key, s);
  }
  // Publish a key's value as the raw JSON text (strings keep their quotes, numbers
  // their exact formatting), on change only. For undecoded fields that may not be numeric.
  void set_connected_sensor(binary_sensor::BinarySensor *s) { this->connected_sensor_ = s; }
  void set_last_frame_age_sensor(sensor::Sensor *s) { this->last_frame_age_sensor_ = s; }
  void add_raw_field(const std::string &json_key, text_sensor::TextSensor *s) {
    this->raw_fields_.push_back(RawField{json_key, s, std::string(), false});
  }

  // --- Control (write) surface ---
  // The app writes plaintext JSON settings to a sibling characteristic on the same
  // service. `t:8` is the setting shape: {"i":1,"v":1,"t":8,"s":<key>,"n":<value>}.
  // Reverse-engineered from com.enphaseenergy.myenlighten (class PesBleDevice).
  void send_setting(const std::string &key, int value);
  // gicl = AC/grid input current limit. 0 = take nothing from the wall (run on
  // battery); restoring the device's own last non-zero value re-enables grid.
  void enable_battery_mode();
  void enable_grid_mode();

 protected:
  void subscribe_();
  void process_buffer_();
  void publish_frame_(const std::string &json);
  void send_command_(const std::string &json);
  void publish_unavailable_();

  std::string service_uuid_str_;
  std::string char_uuid_str_;
  std::string control_uuid_str_;
  uint16_t char_handle_{0};
  uint16_t control_handle_{0};
  float last_gicl_{NAN};  // device's last-seen non-zero grid input limit (for restore)
  std::string mode_sig_;  // last logged mode/settings signature (log only on change)
  std::string raw_sig_;   // last logged *_s / *_m raw signature (log only on change)
  std::string buf_;
  std::vector<std::pair<std::string, sensor::Sensor *>> fields_;

  struct RawField {
    std::string key;
    text_sensor::TextSensor *sensor;
    std::string last;
    bool published;
  };
  std::vector<RawField> raw_fields_;

  // Link health. last_frame_age is polled from loop(), never per frame.
  binary_sensor::BinarySensor *connected_sensor_{nullptr};
  sensor::Sensor *last_frame_age_sensor_{nullptr};
  uint32_t last_frame_ms_{0};
  uint32_t last_poll_ms_{0};
  bool have_frame_{false};
  // True while the data sensors already read unknown (at boot, and after a disconnect),
  // so the paired DISCONNECT + CLOSE events publish the unknown state only once.
  bool unavailable_published_{true};
};

}  // namespace powerpack_ble
}  // namespace esphome

#endif  // USE_ESP32
