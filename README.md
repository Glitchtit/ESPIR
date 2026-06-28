# ESPIR — Zigbee IR Blaster for Home Assistant

A Home Assistant–controlled IR blaster built on the **ESP32-C6**, connected over
**Zigbee** to a **Zigbee2MQTT (Z2M)** coordinator. It **learns** IR codes from your
existing physical remotes and **replays** them on command, so HA buttons map 1:1 to
"press button → appliance fires the learned signal."

## Topology

```
[remote] --IR--> [VS1838B] --RMT RX--> MASTER (DevKitC-1, USB, Zigbee Router)
                                          |  NVS slots, custom ZCL cluster 0xFC00
                                          |  +--RMT TX--> [SZHJW] --IR--> appliance
                                          v
                       Zigbee <--> Z2M (z2m/espir.js) <--> Home Assistant
                                          |                       |
                              learn / send / read code     buttons + scripts
                                          v
        replication (HA script): copy master's learned code -> each SLAVE
                                          v
                       SLAVE (XIAO C6, LiPo, Sleepy End Device) --IR--> local appliance
```

- **Master** (ESP32-C6-DevKitC-1, USB-powered Zigbee **Router**): **learns** by raw-capturing
  from a **VS1838B** receiver and **transmits** through an **SZHJW** dual-LED module — both on
  the RMT peripheral; stores codes in NVS. Always-on, so it responds instantly and extends the
  Zigbee mesh.
- **Slave** (Seeed XIAO ESP32-C6, LiPo **Sleepy End Device**): transmit-only repeater for
  coverage in another spot, using the SZHJW dual-LED emitter (RMT). Receives learned codes
  from the master via a replication path.

> **Protocol note:** raw capture learns essentially **any** remote; NEC is auto-compacted to
> `{address, command}` for cheap storage/replication. Master and slaves share the RMT backend,
> so codes are fully interoperable.

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
| `components/espir_zcl`   | Custom ZCL cluster `0xFC00` contract (`espir_proto.h`) |
| `components/espir_device`| Zigbee device: endpoint, cluster server, learn FSM (shared) |
| `components/espir_led`   | RGB status LED (LEDC PWM) for the custom-PCB slave |
| `components/espir_oled`  | 0.91″ SSD1306 128×32 I²C status display for the master |
| `master/`                | ESP-IDF app: VS1838B learn + SZHJW transmit (Router) |
| `slave/`                 | ESP-IDF app: SZHJW transmit + program/send (Sleepy End Device) |
| `slave-pcb/`             | Custom-PCB slave firmware: discrete MOSFET IR driver + RGB status LED (XIAO-carrier pinout) |
| `z2m/espir.js`           | Zigbee2MQTT external converter (mirrors the cluster contract) |
| `homeassistant/`         | Replication script + example button entities |
| `hardware/`              | BOM + wiring; incl. [`pcb-fully-custom.md`](hardware/pcb-fully-custom.md) — the fully-custom discrete-ESP32-C6 slave board |
| `docs/specs/`            | Design spec |

## Wiring & pinout

Pins are defaults — override in `idf.py menuconfig → ESPIR Configuration`. All grounds must
be common. Full notes (caps, boost, battery sense) in
[`hardware/wiring-master.md`](hardware/wiring-master.md) and
[`hardware/wiring-slave.md`](hardware/wiring-slave.md).

### Master — ESP32-C6-DevKitC-1 (USB powered)

The master uses an **SZHJW** dual-LED module (RMT) for **transmitting** and a **VS1838B**
38 kHz receiver (RMT raw capture) for **learning** — both straight on the C6. Raw capture
learns **any** remote protocol; NEC is auto-compacted.

| Signal | C6 GPIO | Connects to |
|--------|---------|-------------|
| IR TX data | **GPIO6** | SZHJW `DAT` |
| IR RX data | **GPIO4** | VS1838B `OUT` |
| OLED I²C SDA | **GPIO22** | SSD1306 `SDA` |
| OLED I²C SCL | **GPIO23** | SSD1306 `SCK` (= SCL) |
| 5 V | `5V` pin | SZHJW `VCC` |
| 3.3 V | `3V3` pin | VS1838B `VCC` (**3.3 V only**) **and** SSD1306 `VCC` |
| GND | any `GND` | SZHJW `GND`, VS1838B `GND` **and** SSD1306 `GND` |

An optional **0.91″ SSD1306 128×32 I²C OLED** (addr 0x3C) shows the active slot, whether it
holds a code (`STORED`/`EMPTY`), and learn/connection state. Set `ESPIR_OLED_ENABLE=n` to omit
it — the firmware runs normally without a panel.

```
DevKitC-1                         SZHJW dual-LED TX (5 V) — SEND
  5V    ──────────────────────────► VCC
  GPIO6 ──────────────────────────► DAT
  GND ──────────────┬─────────────► GND
                    │
  3V3 ──────────────┼─────────────► VCC   VS1838B receiver (3.3 V ONLY) — LEARN
  GPIO4 ◄───────────┼────────────── OUT
  GND ──────────────┴─────────────► GND
```

Power the VS1838B at **3.3 V** (its `OUT` drives a GPIO directly). Avoid GPIO12/13
(USB-Serial-JTAG) and strapping pins GPIO8/9/15. (The YS-IRTM module is no longer used.)

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

### Custom slave PCBs (permanent builds)

Two ways to turn the jumper-wire slave into a board:

1. **XIAO-carrier** — reflow a XIAO ESP32-C6 onto a small PCB + a discrete IR driver + RGB status
   LED. See [`hardware/wiring-slave.md` → Custom PCB](hardware/wiring-slave.md#custom-pcb-xiao-esp32-c6-as-an-smd-part).
2. **Fully custom (bare ESP32-C6)** — a complete standalone board: discrete C6 (QFN-40) with its own
   USB-C, MCP73831 LiPo charger, AP2112K LDO + load-share, chip antenna (+ u.FL), SPI flash and
   40 MHz crystal, the 2-LED IR driver, and an addressable RGB status LED. Verified to meet the
   slave requirements (Zigbee, battery+charging, USB-C flash/power). See
   [`hardware/pcb-fully-custom.md`](hardware/pcb-fully-custom.md). *(Hardware designed in EasyEDA
   Pro; it moves the IR pin to GPIO2 and the status LED to a single GPIO11 data line, so it needs a
   firmware variant — noted in that doc.)*

## Build & flash

Requires ESP-IDF v5.4+ (`. ~/esp/esp-idf/export.sh` to load the environment).
See [`AGENTS.md`](AGENTS.md) for the full command reference.

```sh
. ~/esp/esp-idf/export.sh

# Master (on the DevKitC-1)
cd master && idf.py set-target esp32c6 && idf.py build flash monitor

# Slave (on the XIAO C6)
cd slave  && idf.py set-target esp32c6 && idf.py build flash monitor

# Custom-PCB slave (XIAO C6 + discrete IR driver + RGB status LED)
cd slave-pcb && idf.py set-target esp32c6 && idf.py build flash monitor
```

## Pairing & usage

1. Drop `z2m/espir.js` into your Z2M `external_converters/` and restart Z2M.
2. Enable joining in Z2M; power on the device; it joins and appears with learn/send entities.
3. To learn: set the target slot, trigger **learn**, point the remote at the master's
   receiver, press the remote key. The captured code is stored in that slot.
4. Bind an HA button to **send slot N**. To cover another room, replicate the code to a
   slave (see `homeassistant/replicate-codes.yaml`).

The HA **Slot** selector drives the device over the `selectSlot` command (manufacturer-specific
ZCL attributes aren't OTA-writable on zboss). The device reports the active slot and whether it
holds a code back to HA (`slot_occupied`) and to the master OLED.

## Over-the-air updates (master)

The master supports Zigbee OTA firmware updates via Zigbee2MQTT.

**One-time migration:** the OTA-capable firmware uses a new dual-slot partition table,
so the first time you load it you must flash over USB (`cd master && idf.py flash`).
The device re-pairs to your Zigbee network once after this flash. All later updates are
wireless.

**Point Z2M at the image index** — in your Zigbee2MQTT `configuration.yaml`:

    ota:
      zigbee_ota_override_index_location: https://raw.githubusercontent.com/Glitchtit/ESPIR/main/z2m/ota/index.json

Restart Z2M. `ESPIR-MASTER` then appears in the OTA tab. Click "Check", then "Update"
when a newer version is offered. The device downloads in the background, reboots into the
new firmware, and confirms it; a bad image rolls back automatically on next boot.

## Status

Firmware and host integration are written and **both apps build clean** for esp32c6
(ESP-IDF v5.4, esp-zigbee-lib 1.6.8):

- ✅ Shared components (`espir_ir` RMT TX/RX, `espir_codec` NEC+raw, `espir_store` NVS slots)
- ✅ Master app — Zigbee Router, custom cluster `0xFC00`, learn FSM
- ✅ Master OLED (`espir_oled`) — active slot, `STORED`/`EMPTY`, learn/connection state
- ✅ Slave app — Zigbee Sleepy End Device, transmit-only
- ✅ Z2M converter, HA replication script, hardware docs

**Verified on hardware:** the master pairs to Zigbee2MQTT, exposes the custom cluster,
status fields populate live in HA, learn/send work end-to-end over the RMT path, and the OLED
tracks the active slot + `STORED`/`EMPTY` occupancy live. Wire the
master per [`hardware/wiring-master.md`](hardware/wiring-master.md) (SZHJW on GPIO6, VS1838B on
GPIO4 at 3.3 V, SSD1306 on GPIO22/23). **Remaining:** flash/pair the XIAO **slave** and exercise
the replication path (`homeassistant/replicate-codes.yaml`).
