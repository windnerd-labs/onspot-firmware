# WindNerd Transfer Protocol — Binary (WTP-bin)

## Introduction

WTP-bin is a compact binary variant of the WindNerd Transfer Protocol designed for **bandwidth-constrained transports**.

It carries only **high-frequency wind samples** plus a minimal device-health block (battery voltage, signal RSSI, optional atmospheric pressure, optional temperature, optional free-form metadata). Aggregated reports are **not** transported here, they have to be constructed by server based on samples.

## Endpoint

```
POST  https://wtp.windnerd.net/bin
POST  http://wtp.windnerd.net/bin
```

**Content-Type:** `application/octet-stream`
**Responses:** `200 Request accepted`, `400 Bad Request`, `401 Wrong Authentication`, `409 Replay Detected`

## Authentication & integrity

- The secret key is **never transmitted**. The server looks it up using the `device_id` field.
- The 32-character hex secret key is decoded into **16 raw bytes** and used as the key of an **HMAC-Blake2s** that signs the entire header and body.
- The Unix timestamp field acts as the **nonce**: the server may reject messages whose timestamp are too old.

## Conventions

- All multi-byte integers are **big-endian** (network order).
- Wind speed is always in **m/s**, wind direction in **degrees**, atmospheric pressure in **hPa**, temperature in **°C**. No unit field.
- Samples are ordered **most recent first**.

---

## Message layout

```
+--------------+----------------+-------------------+-----------------+---------------+
| Header (14B) | Samples (N×3B) | Extras (optional) | Meta (optional) |  HMAC (16 B)  |
+--------------+----------------+-------------------+-----------------+---------------+
```

### Header (14 bytes)

| Offset | Size | Field          | Description                                                                 |
| ------ | ---- | -------------- | --------------------------------------------------------------------------- |
| 0      | 1    | `version`      | Protocol version. This document defines version `0x01`.                     |
| 1      | 1    | `flags`        | bit 0 = meta block present, bit 1 = pressure present, bit 2 = temperature present. Bits 3–7 reserved, must be `0`. |
| 2      | 4    | `device_id`    | Station identifier (uint32). Issued in the station management console.      |
| 6      | 4    | `timestamp`    | Unix epoch in seconds (uint32) of the most recent sample. Also the nonce.   |
| 10     | 1    | `sample_count` | Number of samples in the body, 1–200.                                       |
| 11     | 2    | `voltage_cv`   | Power-supply voltage in centivolts (uint16), range 0–65535 → 0.00–655.35 V. |
| 13     | 1    | `rssi`         | Signal strength in dBm (int8), range −128 to 127.                           |

Samples are always spaced **3 seconds** apart, so the timestamp of sample `i` (0 = most recent) is `timestamp - 3 × i`.

`voltage_cv` and `rssi` are mandatory and reported once per message. Devices that cannot measure one of them must send `0xFFFF` / `0x7F` respectively (treated as "unknown" by the server).

### Sample (3 bytes / 24 bits)

Each sample is packed big-endian, MSB-first:

| Bits    | Width | Field        | Encoding                                                        |
| ------- | ----- | ------------ | --------------------------------------------------------------- |
| 23 – 14 | 10    | `wind_speed` | `round(m/s × 10)`, range 0 – 990 → 0.0 – 99.0 m/s, 0.1 m/s step |
| 13 – 5  | 9     | `wind_dir`   | degrees, range 0 – 359                                          |
| 4 – 0   | 5     | reserved     | must be `0` (room for future use)              |

```
 byte 0           byte 1           byte 2
 SSSS SSSS        SS DD DDDD       DDD R RRRR
 |--- speed[9:2]|sp|----dir[8:3]||dir[2:0]|rsv|
```

### Extras block (optional, variable length)

Holds optional, once-per-message device telemetry. Present only when one or more of the extras flag bits (`flags` bits 1–2) is set. Fields appear in **flag-bit order, lowest bit first**; absent flags contribute no bytes.

| Flag bit | Size | Field            | Encoding                                                                                                  |
| -------- | ---- | ---------------- | --------------------------------------------------------------------------------------------------------- |
| 1        | 2    | `pressure_dhpa`  | Atmospheric pressure (uint16, big-endian). `round(hPa × 10)`, range 0–6553.5 hPa, 0.1 hPa step.           |
| 2        | 2    | `temperature_dc` | Temperature (int16, big-endian, two's complement). `round(°C × 10)`, ±3276.7 °C.                          |

Unlike `voltage_cv` / `rssi`, these fields are fully optional — a device that cannot measure them simply leaves the corresponding flag bit clear and emits no bytes (no sentinel, no padding).

### Meta block (optional, variable length)

Present only when `flags` bit 0 is set.

| Offset | Size | Field      | Description                                                                                          |
| ------ | ---- | ---------- | ---------------------------------------------------------------------------------------------------- |
| 0      | 1    | `meta_len` | Length of the meta string in bytes, 1–255.                                                           |
| 1      | N    | `meta`     | Free-form ASCII payload. Allowed characters: printable ASCII (0x20–0x7E) except `,` `;` `=`.        |

When the bit is clear, no bytes are inserted (no length byte either).

### HMAC footer (16 bytes)

```
tag = HMAC-Blake2s(key = secret_16B, message = header || samples || meta)[0:16]
```

- Algorithm: **HMAC** construction using **Blake2s-256** as the underlying hash.
- The 32-byte HMAC output is truncated to its **first 16 bytes**.
- The tag covers everything from offset 0 of the header up to and including the last meta byte

---

## Sizes

Without meta block:

| Samples | Total bytes |
| ------- | ----------- |
| 1       | 33          |
| 20      | 90          |
| 60      | 210         |
| 200     | 630         |

`size = 14 + 3 × sample_count + extras_size + 16`

where `extras_size = 2 × popcount(flags & 0b00000110)` — 0, 2, or 4 bytes.

With meta block: add `1 + meta_len` bytes.

---

## Server validation order

1. `version == 0x01` and reserved flag bits are zero, otherwise → `400`.
2. Length matches `14 + 3 × sample_count + extras_size + (meta block, if flag set) + 16`, otherwise → `400`.
3. `device_id` is known, otherwise → `401`.
4. HMAC tag matches, otherwise → `401`.
5. `timestamp` within accepted window AND `(device_id, timestamp)` not already used, otherwise → `409`.
6. Each sample's reserved bits are zero and `wind_speed ≤ 990`, `wind_dir ≤ 359`, otherwise → `400`.
7. If present, `meta` characters are inside the allowed ASCII set, otherwise → `400`.

---

## Rationale

- **3 bytes per sample** keeps full 0.1 m/s speed resolution and 1° direction resolution without bit-packing across sample boundaries.
- **uint32 timestamp** doubles as the replay nonce (good until year 2106).
- **HMAC-Blake2s** Blake2s is far cheaper to run on a Cortex-M0 than SHA-256.
- A 128-bit truncated tag offers strong forgery resistance while saving 16 bytes per message versus a full 256-bit tag.
- **Versioning** in the first byte lets the protocol evolve without breaking deployed devices.
