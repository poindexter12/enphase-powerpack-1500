import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import sensor, ble_client
from esphome.const import (
    CONF_ID,
    UNIT_PERCENT,
    UNIT_WATT,
    UNIT_VOLT,
    UNIT_AMPERE,
    UNIT_CELSIUS,
    UNIT_HERTZ,
    UNIT_SECOND,
    UNIT_DECIBEL_MILLIWATT,
    DEVICE_CLASS_BATTERY,
    DEVICE_CLASS_POWER,
    DEVICE_CLASS_VOLTAGE,
    DEVICE_CLASS_CURRENT,
    DEVICE_CLASS_TEMPERATURE,
    DEVICE_CLASS_FREQUENCY,
    DEVICE_CLASS_DURATION,
    DEVICE_CLASS_SIGNAL_STRENGTH,
    STATE_CLASS_MEASUREMENT,
    STATE_CLASS_TOTAL_INCREASING,
)

CODEOWNERS = ["@poindexter12"]
DEPENDENCIES = ["ble_client"]
AUTO_LOAD = ["sensor"]

powerpack_ble_ns = cg.esphome_ns.namespace("powerpack_ble")
PowerPackBLE = powerpack_ble_ns.class_(
    "PowerPackBLE", cg.Component, ble_client.BLEClientNode
)

CONF_SERVICE_UUID = "service_uuid"
CONF_CHARACTERISTIC_UUID = "characteristic_uuid"
CONF_CONTROL_CHARACTERISTIC_UUID = "control_characteristic_uuid"


def _s(unit, dclass, sclass, decimals):
    kw = {"state_class": sclass, "accuracy_decimals": decimals}
    if unit is not None:
        kw["unit_of_measurement"] = unit
    if dclass is not None:
        kw["device_class"] = dclass
    return sensor.sensor_schema(**kw)


# config key -> (telemetry JSON key, sensor schema)
SENSORS = {
    "battery": ("soc", _s(UNIT_PERCENT, DEVICE_CLASS_BATTERY, STATE_CLASS_MEASUREMENT, 0)),
    "state_of_health": ("soh", _s(UNIT_PERCENT, None, STATE_CLASS_MEASUREMENT, 0)),
    "battery_power": ("uiBatP", _s(UNIT_WATT, DEVICE_CLASS_POWER, STATE_CLASS_MEASUREMENT, 0)),
    "storage_power": ("storage_W", _s(UNIT_WATT, DEVICE_CLASS_POWER, STATE_CLASS_MEASUREMENT, 0)),
    "ac_load": ("load_ac_W", _s(UNIT_WATT, DEVICE_CLASS_POWER, STATE_CLASS_MEASUREMENT, 0)),
    "grid_input": ("grid_import_W", _s(UNIT_WATT, DEVICE_CLASS_POWER, STATE_CLASS_MEASUREMENT, 0)),
    "solar_input": ("pv_dc_produce_W", _s(UNIT_WATT, DEVICE_CLASS_POWER, STATE_CLASS_MEASUREMENT, 0)),
    "inverter_output": ("pcu_output_W", _s(UNIT_WATT, DEVICE_CLASS_POWER, STATE_CLASS_MEASUREMENT, 0)),
    "battery_voltage": ("bat_v", _s(UNIT_VOLT, DEVICE_CLASS_VOLTAGE, STATE_CLASS_MEASUREMENT, 1)),
    "battery_current": ("bat_i", _s(UNIT_AMPERE, DEVICE_CLASS_CURRENT, STATE_CLASS_MEASUREMENT, 2)),
    "battery_temp": ("bat_avg_temp", _s(UNIT_CELSIUS, DEVICE_CLASS_TEMPERATURE, STATE_CLASS_MEASUREMENT, 1)),
    "battery_temp_max": ("bat_max_temp", _s(UNIT_CELSIUS, DEVICE_CLASS_TEMPERATURE, STATE_CLASS_MEASUREMENT, 1)),
    "ac_output_voltage": ("ac_out_v", _s(UNIT_VOLT, DEVICE_CLASS_VOLTAGE, STATE_CLASS_MEASUREMENT, 1)),
    "ac_output_current": ("ac_out_i", _s(UNIT_AMPERE, DEVICE_CLASS_CURRENT, STATE_CLASS_MEASUREMENT, 2)),
    "ac_output_frequency": ("ac_out_f", _s(UNIT_HERTZ, DEVICE_CLASS_FREQUENCY, STATE_CLASS_MEASUREMENT, 1)),
    "backup_time": ("backupTime_s", _s(UNIT_SECOND, DEVICE_CLASS_DURATION, STATE_CLASS_MEASUREMENT, 0)),
    "time_to_full": ("timeToCharge_s", _s(UNIT_SECOND, DEVICE_CLASS_DURATION, STATE_CLASS_MEASUREMENT, 0)),
    "battery_cycles": ("battery_cycles", _s(None, None, STATE_CLASS_TOTAL_INCREASING, 0)),
    "cellular_signal": ("CRSSI", _s(UNIT_DECIBEL_MILLIWATT, DEVICE_CLASS_SIGNAL_STRENGTH, STATE_CLASS_MEASUREMENT, 0)),
    # Full scalar coverage (PCU per-micro arrays and mode enums excluded — the flat
    # extract_num parser only handles scalar numbers).
    "battery_temp_min": ("bat_min_temp", _s(UNIT_CELSIUS, DEVICE_CLASS_TEMPERATURE, STATE_CLASS_MEASUREMENT, 1)),
    "cell_voltage_avg": ("bat_avg_v", _s(UNIT_VOLT, DEVICE_CLASS_VOLTAGE, STATE_CLASS_MEASUREMENT, 3)),
    "cell_voltage_min": ("bat_min_v", _s(UNIT_VOLT, DEVICE_CLASS_VOLTAGE, STATE_CLASS_MEASUREMENT, 3)),
    "cell_voltage_max": ("bat_max_v", _s(UNIT_VOLT, DEVICE_CLASS_VOLTAGE, STATE_CLASS_MEASUREMENT, 3)),
    "usb_load": ("load_dc_usb_W", _s(UNIT_WATT, DEVICE_CLASS_POWER, STATE_CLASS_MEASUREMENT, 0)),
    "aux_load": ("load_dc_aux_W", _s(UNIT_WATT, DEVICE_CLASS_POWER, STATE_CLASS_MEASUREMENT, 0)),
    "usb_voltage": ("usb_v", _s(UNIT_VOLT, DEVICE_CLASS_VOLTAGE, STATE_CLASS_MEASUREMENT, 2)),
    "usb_current": ("usb_i", _s(UNIT_AMPERE, DEVICE_CLASS_CURRENT, STATE_CLASS_MEASUREMENT, 3)),
    "external_ac_input": ("ext_ac_produce_W", _s(UNIT_WATT, DEVICE_CLASS_POWER, STATE_CLASS_MEASUREMENT, 0)),
    "dc_12v_input": ("dc_12v_import_W", _s(UNIT_WATT, DEVICE_CLASS_POWER, STATE_CLASS_MEASUREMENT, 0)),
    "grid_voltage": ("grid_v", _s(UNIT_VOLT, DEVICE_CLASS_VOLTAGE, STATE_CLASS_MEASUREMENT, 1)),
    "grid_current": ("grid_i", _s(UNIT_AMPERE, DEVICE_CLASS_CURRENT, STATE_CLASS_MEASUREMENT, 2)),
    "grid_frequency": ("grid_f", _s(UNIT_HERTZ, DEVICE_CLASS_FREQUENCY, STATE_CLASS_MEASUREMENT, 1)),
    "solar_voltage": ("pv_v", _s(UNIT_VOLT, DEVICE_CLASS_VOLTAGE, STATE_CLASS_MEASUREMENT, 1)),
    "solar_current": ("pv_i", _s(UNIT_AMPERE, DEVICE_CLASS_CURRENT, STATE_CLASS_MEASUREMENT, 2)),
    "fan1_duty": ("fan1_duty_cycle", _s(UNIT_PERCENT, None, STATE_CLASS_MEASUREMENT, 0)),
    "fan2_duty": ("fan2_duty_cycle", _s(UNIT_PERCENT, None, STATE_CLASS_MEASUREMENT, 0)),
    "fan3_duty": ("fan3_duty_cycle", _s(UNIT_PERCENT, None, STATE_CLASS_MEASUREMENT, 0)),
}

CONFIG_SCHEMA = (
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(PowerPackBLE),
            cv.Optional(
                CONF_SERVICE_UUID, default="6d1b33f8-0000-11eb-a8b3-0242ac130003"
            ): cv.string,
            cv.Optional(
                CONF_CHARACTERISTIC_UUID, default="6d1b33f8-0010-11eb-a8b3-0242ac130003"
            ): cv.string,
            cv.Optional(
                CONF_CONTROL_CHARACTERISTIC_UUID,
                default="6d1b33f8-0014-11eb-a8b3-0242ac130003",
            ): cv.string,
            **{cv.Optional(k): schema for k, (_, schema) in SENSORS.items()},
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
    .extend(ble_client.BLE_CLIENT_SCHEMA)
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await ble_client.register_ble_node(var, config)
    cg.add(var.set_service_uuid(config[CONF_SERVICE_UUID]))
    cg.add(var.set_characteristic_uuid(config[CONF_CHARACTERISTIC_UUID]))
    cg.add(var.set_control_characteristic_uuid(config[CONF_CONTROL_CHARACTERISTIC_UUID]))
    for key, (json_key, _) in SENSORS.items():
        if key in config:
            sens = await sensor.new_sensor(config[key])
            cg.add(var.add_field(json_key, sens))
