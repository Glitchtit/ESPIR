# Bill of Materials

## Master unit

| Qty | Part | Notes |
|-----|------|-------|
| 1 | ESP32-C6-DevKitC-1 (or clone, 4 MB+ flash) | USB-powered; provides 5 V rail |
| 1 | **SZHJW** dual-LED IR transmitter (5 V, `DAT/VCC/GND`) | **transmitting** (RMT, software 38 kHz carrier) |
| 1 | **VS1838B** / TSOP38238 38 kHz receiver | **learning** (RMT raw capture); power at 3.3 V |
| — | jumper wires | — |

Master = SZHJW (send) + VS1838B (learn), both on the C6 RMT peripheral. Raw capture learns
**any** remote protocol; NEC is auto-compacted. (The YS-IRTM module is no longer used.)

## Slave unit (per extra coverage point)

| Qty | Part | Notes |
|-----|------|-------|
| 1 | Seeed XIAO ESP32-C6 | onboard LiPo charging + battery pads |
| 1 | SZHJW dual-LED IR transmitter module | 5 V, `DAT/VCC/GND`, 940 nm, no internal carrier; runs at battery voltage (reduced range) — **breadboard/jumper-wire build** |
| 1 | 3.7 V LiPo cell | JST 1.25 mm (MX1.25) 2P plug; breadboard build solders the leads to the XIAO `BAT+`/`BAT−` pads (mind polarity) |
| 1 | (optional) 3.3 V→5 V boost converter | only if slave IR range proves insufficient |

The slave transmits with the SZHJW emitter driven by the C6's RMT peripheral (software
38 kHz carrier). It replays the codes the master learned.

### Custom-PCB build (discrete IR driver)

Replaces the SZHJW module with a discrete driver so the LEDs run **straight off GPIO18** (no
module). See `hardware/wiring-slave.md` → *Custom PCB* for the schematic and sizing.

| Qty | Part | Notes |
|-----|------|-------|
| 1 | Seeed XIAO ESP32-C6 | reflowed on castellations as an SMD part |
| 1 | AO3400A N-MOSFET (SOT-23) | logic-level, low-side switch; SI2302 / DMN2075U equivalent |
| 2 | 940 nm IR LED | in parallel, each via its own ballast |
| 2 | 18 Ω, ¼ W resistor | ballast, one per LED (~150 mA pulse) |
| 1 | 100 Ω resistor | gate series |
| 1 | 100 kΩ resistor | gate pulldown (keeps LEDs off while GPIO18 is hi-Z) |
| 2 | 55 kΩ (or 56 kΩ), 1 % | battery divider → GPIO0 (`CONFIG_ESPIR_BATTERY_DIV_X100 = 200`) |
| 1 | 100 µF low-ESR cap | reservoir for the LED pulse bursts |
| 1 | 100 nF cap | decoupling |
| 1 | JST 1.25 mm (MX1.25) 2P header | LiPo connector — match the cell's plug; verify polarity vs `BAT+` |
| 1 | RGB LED (common-cathode or -anode) | status: amber=searching, green=joined, blue=sending |
| 3 | LED series resistors | ~330 Ω (red), ~150 Ω (green/blue) — `LED_R/G/B` on GPIO17/19/20 |
| 2 | VBUS-sense divider (100 kΩ + 150 kΩ) | USB-present sense on GPIO1; gates the steady green off on battery |

The RGB status LED is driven by the dedicated `slave-pcb/` firmware (LEDC PWM straight from the
GPIOs). The solid green only lights while USB is present so it can't drain the LiPo; omit the VBUS
divider (set `ESPIR_LED_VBUS_GPIO = -1`) to disable that gating.

## Notes

- **Power the VS1838B at 3.3 V** (master) — its `OUT` drives a GPIO directly; never 5 V.
- The SZHJW has its own transistor driver; the 3.3 V GPIO drives `DAT` fine. Run `VCC` at 5 V
  (master) for full range; at battery voltage on the slave (reduced range).
- A `~38–100 µF` cap across the SZHJW `VCC`/`GND` helps the LED current bursts.
