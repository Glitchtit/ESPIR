# ESPIR — Zigbee IR Blaster for Home Assistant

A Home Assistant–controlled IR blaster built on the **ESP32-C6**, connected over
**Zigbee** to a **Zigbee2MQTT (Z2M)** coordinator. It **learns** IR codes from your
existing physical remotes and **replays** them on command, so HA buttons map 1:1 to
"press button → appliance fires the learned signal."

## Topology

```
[remote] --IR--> [VS1838B] --RMT RX--> MASTER (DevKitC-1, USB, Zigbee Router)
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

- **Master** (ESP32-C6-DevKitC-1, USB-powered Zigbee **Router**): learns codes via a
  VS1838B receiver, stores them in NVS, and transmits at full 5 V range. Always-on, so it
  responds instantly and extends the Zigbee mesh.
- **Slave** (Seeed XIAO ESP32-C6, LiPo **Sleepy End Device**): transmit-only repeater for
  coverage in another spot. Receives learned codes from the master via a replication path.

## Why this design

A battery IR blaster that must answer HA instantly is a contradiction — Zigbee on the C6
only sips power if it sleeps deeply. The master/slave split sidesteps it: the master is
mains/USB-powered (snappy, full range, does the learning); slaves are battery and only need
to fire eventually at their own nearby appliance.

## Repo layout

| Path | What |
|------|------|
| `components/espir_ir`    | RMT IR transmit (software 38 kHz carrier) + raw receive |
| `components/espir_codec` | NEC decode/encode + raw↔compact helpers |
| `components/espir_store` | NVS slot store (save/load/clear, chunked program) |
| `components/espir_zcl`   | Custom ZCL cluster `0xFC00` — the shared device/host contract |
| `master/`                | ESP-IDF app: receiver + transmitter + learn FSM (Router) |
| `slave/`                 | ESP-IDF app: transmitter + program/send subset (Sleepy End Device) |
| `z2m/espir.js`           | Zigbee2MQTT external converter (mirrors the cluster contract) |
| `homeassistant/`         | Replication script + example button entities |
| `hardware/`              | BOM and wiring diagrams |
| `docs/specs/`            | Design spec |

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

Greenfield. See [`docs/specs/2026-06-23-espir-design.md`](docs/specs/2026-06-23-espir-design.md)
for the design and current build phase.
