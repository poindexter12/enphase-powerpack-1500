# BLE protocol (reverse-engineered)

Reverse-engineered from an Android Bluetooth HCI snoop of the Enphase app doing a WiFi
provisioning session, plus direct GATT probing. **No encryption is broken** — the telemetry is
literally broadcast in the clear; only the *provisioning/control* path is encrypted.

## Advertisement & connection
- Advertises as **`PES_<12-digit-serial>`**, connectable, primary advertised service
  `021a9004-0382-4aea-bff4-6b3f1c5adfb4`.
- Address type is public/static — connect as **public**. **Connect unbonded**: reads/notifies
  are plaintext, and bonding is fragile (a device power-cycle invalidates the bond and breaks
  reconnects — e.g. macOS `CBError 15`).
- **One central at a time.** While a bridge holds the link, the phone app uses cloud/cellular.
- A client that connects but never runs the ESP provisioning handshake is **dropped after
  ~30 s** — that's the provisioning session timeout, not a fault.

## GATT layout
| Service | Purpose | Encryption |
|---|---|---|
| `021a9004-…` (chars `021aff4f`–`021aff53`) | ESP-IDF provisioning (protocomm) — WiFi setup + control | App-layer (Security1) |
| `6d1b33f8-000X-11eb-a8b3-0242ac130003` (X=0..3) | Custom data/telemetry registers | **none (plaintext)** |
| `6d1b33f8-0010-11eb-a8b3-0242ac130003` (notify) | **Live telemetry JSON stream** | **none (plaintext)** |
| `0x180F` Battery Service → `0x2A19` | SOC as a plain 0–100% byte | none |
| `0x180A` Device Information | Manufacturer / model / serial | none |

## Telemetry stream (what the firmware uses)
- **Subscribe** to the `6d1b33f8-0010-…` notify characteristic.
- The device pushes a full JSON object ~1–2×/sec, **fragmented** across ~180-byte notifications.
  Reassemble by concatenating fragments and parsing from `{` to the matching `}`.
- ~109 plaintext fields. See `telemetry-fields.md`.

## Provisioning / control channel (encrypted)
The `021a9004-…` service is **standard Espressif ESP-IDF Unified Provisioning** (`protocomm`):
**Security1** (X25519 ECDH → AES-256-CTR) with **`no_pop`** (no proof-of-possession secret — any
client in BLE range can complete the handshake). The `proto-ver` endpoint returns, in plaintext:
`{"prov":{"ver":"v1.1","sec_ver":1,"cap":["no_pop","wifi_scan"]}}`. Monitoring needs none of
this; control would ride this channel — a future phase.

## Security note
`no_pop` means the unit's provisioning is unauthenticated: anyone within BLE range during a
setup window can complete the crypto handshake, and all telemetry is readable with no auth at
all. Convenient for local integration; worth knowing as a property of the product.
