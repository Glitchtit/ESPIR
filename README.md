# ESPIR — Zigbee IR Blaster for Home Assistant

A Home Assistant–controlled IR blaster built on the **ESP32-C6**, connected over
**Zigbee** to a **Zigbee2MQTT (Z2M)** coordinator. It **learns** IR codes from your
existing physical remotes and **replays** them on command, so HA buttons map 1:1 to
"press button → appliance fires the learned signal."

## Topology

```
[remote] --IR--> [YS-IRTM] --UART--> MASTER (DevKitC-1, USB, Zigbee Router)
                                          |  NVS slots, custom ZCL cluster 0xFC00
                                          v
                       Zigbee <--> Z2M (z2m/espir.js) <--> Home Assistant
                                          |                       |
                              learn / send / read code     buttons + scripts
                                          v
        replication (HA script): copy master's learned code -> each SLAVE
                                          v
                       SLAVE (XIAO C6, LiPo, Sleepy End Device) --IR--> local appliance
```

- **Master** (ESP32-C6-DevKitC-1, USB-powered Zigbee **Router**): learns and transmits NEC
  codes via a **YS-IRTM** UART codec module (its own emitter + receiver), stores them in
  NVS. Always-on, so it responds instantly and extends the Zigbee mesh.
- **Slave** (Seeed XIAO ESP32-C6, LiPo **Sleepy End Device**): transmit-only repeater for
  coverage in another spot, using the SZHJW dual-LED emitter (RMT). Receives learned codes
  from the master via a replication path.

> **Protocol note:** the YS-IRTM only handles **NEC-family** remotes, so the master learns
> NEC only. Codes are stored as NEC `{address, command}`, which the RMT slaves re-encode and
> blast — master and slaves stay interoperable.

## Why this design

A battery IR blaster that must answer HA instantly is a contradiction — Zigbee on the C6
only sips power if it sleeps deeply. The master/slave split sidesteps it: the master is
mains/USB-powered (snappy, full range, does the learning); slaves are battery and only need
to fire eventually at their own nearby appliance.

## Repo layout

| Path | What |
|------|------|
| `components/espir_irtm`  | YS-IRTM UART NEC codec driver (master IR backend) |
| `components/espir_ir`    | RMT IR transmit (software 38 kHz carrier) + receive (slave IR backend) |
| `components/espir_codec` | NEC decode/encode + raw↔compact helpers |
| `components/espir_store` | NVS slot store (save/load/clear, chunked program) |
| `components/espir_zcl`   | Custom ZCL cluster `0xFC00` contract (`espir_proto.h`) |
| `components/espir_device`| Zigbee device: endpoint, cluster server, learn FSM (shared) |
| `master/`                | ESP-IDF app: YS-IRTM learn + transmit (Router) |
| `slave/`                 | ESP-IDF app: SZHJW transmit + program/send (Sleepy End Device) |
| `z2m/espir.js`           | Zigbee2MQTT external converter (mirrors the cluster contract) |
| `homeassistant/`         | Replication script + example button entities |
| `hardware/`              | BOM and wiring diagrams |
| `docs/specs/`            | Design spec |

## Wiring & pinout

Pins are defaults — override in `idf.py menuconfig → ESPIR Configuration`. All grounds must
be common. Full notes (caps, boost, battery sense) in
[`hardware/wiring-master.md`](hardware/wiring-master.md) and
[`hardware/wiring-slave.md`](hardware/wiring-slave.md).

### Master — ESP32-C6-DevKitC-1 (USB powered) + YS-IRTM

The master uses a **YS-IRTM** module: a self-contained NEC codec with its own IR emitter
and receiver, talking **UART** (9600 8N1). It does both learn and transmit — no SZHJW or
VS1838B on the master. (NEC-only; non-NEC remotes can't be learned here.)

| Signal | C6 GPIO | Connects to |
|--------|---------|-------------|
| UART TX | **GPIO5** | YS-IRTM `RXD` |
| UART RX | **GPIO4** | YS-IRTM `TXD` — **via a divider** (5 V → 3.3 V) |
| 5 V | `5V` pin | YS-IRTM `5V` |
| GND | any `GND` | YS-IRTM `GND` |

```
DevKitC-1                         YS-IRTM (NEC codec, 5 V)
  5V    ──────────────────────────► 5V
  GPIO5 (TX) ────────────────────► RXD
  GPIO4 (RX) ◄───[ 10k ]──┬──────── TXD   (divider: TXD-10k-RX, RX-20k-GND)
                        [ 20k ]
  GND ───────────────────┴────────► GND
```

The C6 is **not 5 V tolerant** — YS-IRTM `TXD` (5 V) must go through the divider into
`GPIO4`. The C6's 3.3 V `TX` into YS-IRTM `RXD` is usually accepted directly. Avoid
GPIO12/13 (USB-Serial-JTAG) and strapping pins GPIO8/9/15.

### Slave — Seeed XIAO ESP32-C6 (LiPo powered, transmit-only)

No 5 V rail on battery, so the transmitter runs at battery voltage (~3.7–4.2 V → reduced
range). No receiver.

| Signal | XIAO pad | C6 GPIO | Connects to |
|--------|----------|---------|-------------|
| IR TX data | **D2** | GPIO2 | SZHJW `DAT` |
| Power for IR TX | `BAT+` (or `5V` on USB) | — | SZHJW `VCC` |
| GND | `GND` | — | SZHJW `GND` |
| LiPo ± | `BAT+` / `BAT−` | — | 3.7 V LiPo cell (charges over USB-C) |

```
XIAO ESP32-C6                     SZHJW IR TX (2× 940nm emitters)
  BAT+ ──────────────────────────► VCC   (≈3.7–4.2 V; reduced range)
  D2 (GPIO2) ────────────────────► DAT
  GND ───────────────────────────► GND
  BAT+ / BAT− ◄──────────────────── 3.7 V LiPo cell
```

## Build & flash

Requires ESP-IDF v5.4+ (`. ~/esp/esp-idf/export.sh` to load the environment).
See [`AGENTS.md`](AGENTS.md) for the full command reference.

```sh
. ~/esp/esp-idf/export.sh

# Master (on the DevKitC-1)
cd master && idf.py set-target esp32c6 && idf.py build flash monitor

# Slave (on the XIAO C6)
cd slave  && idf.py set-target esp32c6 && idf.py build flash monitor
```

## Pairing & usage

1. Drop `z2m/espir.js` into your Z2M `external_converters/` and restart Z2M.
2. Enable joining in Z2M; power on the device; it joins and appears with learn/send entities.
3. To learn: set the target slot, trigger **learn**, point the remote at the master's
   receiver, press the remote key. The captured code is stored in that slot.
4. Bind an HA button to **send slot N**. To cover another room, replicate the code to a
   slave (see `homeassistant/replicate-codes.yaml`).

## Status

Firmware and host integration are written and **both apps build clean** for esp32c6
(ESP-IDF v5.4, esp-zigbee-lib 1.6.8):

- ✅ Shared components (`espir_ir` RMT TX/RX, `espir_codec` NEC+raw, `espir_store` NVS slots)
- ✅ Master app — Zigbee Router, custom cluster `0xFC00`, learn FSM
- ✅ Slave app — Zigbee Sleepy End Device, transmit-only
- ✅ Z2M converter, HA replication script, hardware docs

**Not yet verified on hardware** (needs the physical boards + your Z2M/HA): flashing,
pairing, IR capture/replay, and code replication. See the verification steps in
[`docs/specs/2026-06-23-espir-design.md`](docs/specs/2026-06-23-espir-design.md). Wire the
master per [`hardware/wiring-master.md`](hardware/wiring-master.md) (YS-IRTM over UART, with
the TXD divider), flash, and run `idf.py monitor` to watch it learn.
