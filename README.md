# 🔋 Enphase PowerPack ESPHome Components

**Local, cloud-free monitoring and control of the Enphase IQ PowerPack 1500 over BLE, from ESPHome and Home Assistant.**

> ⚠️ *Use at your own risk.*
> This project interfaces with hardware via a reverse-engineered protocol not meant for third-party control. You are responsible for any damage or warranty issues.

## Overview

The PowerPack streams its full telemetry as JSON over a BLE notify characteristic, 1–2 frames/sec, and accepts plaintext JSON setting writes on a control characteristic — no bonding, no encryption, no cloud. This component connects with a standard ESP32 and turns that stream into ~38 Home Assistant sensors plus working output controls.

Why a custom component instead of stock `ble_client` sensors: the device exposes a huge GATT database and tolerates only short connection windows, which defeats ESPHome's full GATT discovery. `powerpack_ble` subscribes **directly** to the telemetry characteristic by UUID and parses frames as they arrive.

The protocol was reverse-engineered from the official `myenlighten` app (`PesBleDevice`); see [docs/ble-protocol.md](docs/ble-protocol.md) and [docs/telemetry-fields.md](docs/telemetry-fields.md).

## Quick start

```yaml
substitutions:
  powerpack_ble_mac: "AA:BB:CC:DD:EE:FF"   # your unit's BLE MAC (advertises as PES_<serial>)

external_components:
  - source: github://poindexter12/enphase-powerpack-1500
    components: [powerpack_ble]

esp32_ble_tracker:
  scan_parameters:
    interval: 320ms
    window: 30ms
    active: false

ble_client:
  - mac_address: ${powerpack_ble_mac}
    id: pp_client

powerpack_ble:
  id: pp_ctl
  ble_client_id: pp_client
  battery: {name: "Battery"}
  battery_power: {name: "Battery Power"}
  grid_input: {name: "Grid Input"}
  solar_input: {name: "Solar Input"}
  # ...see examples/powerpack.yaml for the full sensor set
```

A complete config with all sensors, throttling, and controls is in [`examples/powerpack.yaml`](examples/powerpack.yaml).

> **Pin a commit.** `github://poindexter12/enphase-powerpack-1500` tracks the default branch, so with
> `refresh: 0s` every build picks up whatever was pushed last. For a device you rely on, pin a reviewed
> commit, `source: github://poindexter12/enphase-powerpack-1500@<sha>`, and bump the sha on purpose.

## Link health

Two optional diagnostic entities report whether telemetry is flowing. Both are polled every
10 s, never per frame:

```yaml
powerpack_ble:
  connected: {name: "PowerPack BLE Connected"}       # binary_sensor, connectivity
  last_frame_age: {name: "PowerPack Last Frame Age"}  # seconds since the last parsed frame
```

When the BLE link drops, every field sensor is published as NAN, which HA shows as
`unknown`. Text sensors cannot hold NAN, so every `raw_fields` text sensor is published as
the literal `unknown` instead. Values return with the first frame after reconnect.
`last_frame_age` keeps counting while the link is down, so alert on it, not on the
field sensors.

## Sensors

Every scalar field in the telemetry frame is available as an optional sensor — declare only the ones you want. Highlights:

| Config key | Telemetry | What it is |
|---|---|---|
| `battery` | `soc` | State of charge (%) |
| `state_of_health` | `soh` | Battery health (%) |
| `battery_power` | `uiBatP` | Battery charge/discharge power (W) |
| `grid_input` | `grid_import_W` | Wall draw (W) |
| `solar_input` | `pv_dc_produce_W` | PV input (W) |
| `ac_load` / `usb_load` / `aux_load` | `load_*_W` | Per-output loads (W) |
| `backup_time` / `time_to_full` | `backupTime_s` / `timeToCharge_s` | Runtime estimates (s) |
| `battery_voltage/current/temp*`, `cell_voltage_*`, `grid_*`, `ac_output_*`, `fan*_duty`, `battery_cycles`, `cellular_signal` | … | Full electrical/thermal detail |

Full field-by-field notes: [docs/telemetry-fields.md](docs/telemetry-fields.md).

### Raw fields (undecoded modes and states)

Some fields are undecoded enums, and some may be strings, which a numeric sensor would read as 0. `raw_fields` publishes chosen keys as text sensors holding the raw JSON text (strings keep their quotes), on change only:

```yaml
powerpack_ble:
  raw_fields:
    - {key: op_m, name: "Operating Mode (raw)"}
    - {key: conn_m, name: "Connection Mode (raw)"}
```

There is no wildcard: ESPHome entities are fixed at compile time, so each key needs its own sensor. To find candidate keys, watch the INFO log. Every frame's keys ending in `_s` or `_m` are logged as one sorted `RAW _s/_m: key=value ...` line, and that line is logged again only when a value changes.

## Controls

Settings are written as plaintext JSON to the control characteristic via `send_setting(key, value)` — expose them as template switches/numbers (see the example yaml):

| Key | Meaning | Notes |
|---|---|---|
| `usoe` | USB outputs on/off | Confirmed live — physically switches the ports |
| `axoe` | 12 V (aux) output on/off | Confirmed live |
| `fchg` | Fast charge on/off | |
| `gicl` | Grid input current limit (0–150 ≈ 0–15 A) | Throttles total wall draw (charge + passthrough) on a weak circuit |

> **There is no "battery vs grid" mode.** The PowerPack is an automatic UPS: grid present → passthrough + charge; grid lost → battery in <10 ms. To force battery operation, physically disconnect AC.

## Hardware

Any plain ESP32 (e.g. WROOM devkit) within BLE range of the unit. Built and tested against the ESP-IDF framework — see the `esp32:` block in the example.

## Credits

- Protocol reverse-engineered from the official Enphase `myenlighten` Android app.
- Built for/with [ESPHome](https://esphome.io)'s external component architecture.
