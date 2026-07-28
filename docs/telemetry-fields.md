# PowerPack 1500 — BLE telemetry field guide

**The whole game:** the unit streams its complete state as **plaintext JSON over a BLE
notify characteristic** — no encryption, no auth, no protobuf. Subscribe and parse.

- **Transport:** BLE GATT notify.
- **Characteristic:** in the `6d1b33f8-…-11eb-a8b3-0242ac130003` family, notify property,
  second UUID group `0010` (confirm exact handle on next connect — several chars share the
  short id; the JSON-streaming one is the notify char). The device also exposes standard
  **Battery Service `0x2a19`** = SOC as a plain 0–100% byte (trivial fallback for SOC alone).
- **Framing:** the JSON object is split across ~180-byte notify packets; reassemble by
  concatenating fragments and parsing from `{` to matching `}`. Pushed ~1–2×/sec.
- Captured live from the `6d1b33f8-0010` notify characteristic.
  100% SOC).

## Key fields for Home Assistant
Captured example values in parentheses (unit discharging ~418 W into an AC load).

### Battery
| field | meaning | example | unit |
|---|---|---|---|
| `soc` | State of charge | 100 | % |
| `soh` | State of health | 100 | % |
| `storage_W` | Battery power (charge +/discharge) | 443 | W |
| `uiBatP` / `uiP` | Battery power as app shows it | 418 | W |
| `bat_v` | Pack voltage | 62.3 | V |
| `bat_i` | Pack current | 7.1 | A |
| `bat_avg_temp` / `bat_min_temp` / `bat_max_temp` | Cell temps | 41 / 41 / 42 | °C |
| `bat_avg_v` / `bat_min_v` / `bat_max_v` | Cell voltages | 3.283 / 3.281 / 3.285 | V |
| `battery_cycles` | Lifetime cycles | 21 | count |
| `backupTime_s` | Est. runtime at current load | 10249 | s |
| `timeToCharge_s` | Est. time to full | 300 | s |

### Loads / outputs
| field | meaning | example | unit |
|---|---|---|---|
| `load_ac_W` | AC output load | 418 | W |
| `load_dc_usb_W` | USB output load | 0 | W |
| `load_dc_aux_W` | DC/aux output load | 0 | W |
| `ac_out_v` / `ac_out_i` / `ac_out_f` | AC output V / A / Hz | 123.3 / 3.6 / 59.5 | V/A/Hz |
| `usb_v` / `usb_i` | USB rail | -3.2 / -0.036 | V/A |

### Sources / inputs
| field | meaning | unit |
|---|---|---|
| `grid_import_W` | Power in from grid/wall | W |
| `pv_dc_produce_W` | Solar in | W |
| `ext_ac_produce_W` | External AC in | W |
| `grid_v` / `grid_i` / `grid_f` | Grid/wall metering | V/A/Hz |
| `pv_v` / `pv_i` | Solar metering | V/A |

### Microinverters (IQ8, 3 present — `pcu_counts`:3)
| field | meaning |
|---|---|
| `pcu_output_W` | Total PCU output (W) |
| `pcu_ac_p[]` / `pcu_ac_v[]` / `pcu_ac_i[]` | Per-micro AC power/volt/current |
| `pcu_dc_p[]` / `pcu_dc_v[]` / `pcu_dc_i[]` | Per-micro DC side |
| `pcu_temps[]` | Per-micro temperature (°C) |

### Thermal / fans
`fan1_duty_cycle`, `fan2_duty_cycle`, `fan3_duty_cycle`, `*_setpoint`.

### Modes / status (enums — decode later)
`*_m` = mode, `*_s` = status/fault, e.g. `op_m`(operating), `bat_m`, `ac_m`, `cell_m`,
`ble_m`, `conn_m`. `uiBat2Load:1` etc. are the app's flow-arrow booleans.

### Identity / connectivity
| field | meaning | example |
|---|---|---|
| `dsn` | Device serial | <SERIAL> |
| `gateway.gwid` | Gateway id | <GATEWAY-ID> |
| `gateway.cn` | Cellular carrier | <CARRIER> |
| `gateway.CRSSI` | Cell signal | -75 |
| `gateway.SSID` / `RSSI` | WiFi (N/A here) | — |

### settings{} (writable config — control surface, later)
`dbri` display brightness, `gicl`/`aicl` grid/AC current limits, `fchg` fast-charge,
`usoe`/`axoe` USB/aux output enable, `dmse`/`slse` modes, `ttps`/`ttpo` schedule.
Writing to the `021aff…` protocomm endpoints (encrypted) is how the app changes these —
control (vs read-only monitoring) would need that encrypted channel. **Monitoring needs none.**

## How the firmware maps these
`firmware/esphome/enphase-powerpack.yaml` subscribes to the `6d1b33f8-0010` notify
characteristic, reassembles the fragmented JSON in a lambda, parses it with ESPHome's
`json::parse_json`, and publishes a curated subset as `template` sensors (SOC, battery power,
load, grid/solar in, inverter output, battery V/A/temps, AC out V/A/Hz, backup time, cycles,
cellular signal). SOC is also read directly from the standard Battery Level characteristic
(`0x2A19`) as a simple, always-works fallback.
