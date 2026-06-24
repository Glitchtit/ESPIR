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

LCSC/JLCPCB part numbers are listed where confirmed; passives marked **Basic** are standard JLC
Basic values — let the JLCPCB BOM tool match the C-number (no part fee). Live stock changes daily,
so confirm quantities at checkout.

| Qty | Part | LCSC | Notes |
|-----|------|------|-------|
| 1 | Seeed XIAO ESP32-C6 | C9900124963 | reflowed on castellations; JLC Assembly (hand-place / module) |
| 1 | AO3400A N-MOSFET (SOT-23) | C20917 | IR driver, logic-level low-side switch (Basic) |
| 2 | 940 nm IR LED — M3030E1IRS6G45-940 | *confirm on LCSC* | TCWIN; in parallel, each via its own 18 Ω ballast |
| 3 | 2N7002 N-MOSFET (SOT-23) | C8545 | RGB colour low-side switches (Basic) |
| 1 | RGB LED, **common-anode** — Everlight 22-23C/R6GHBHW-C01/2C | C181865 | status: amber=searching, green=joined, blue=sending; anode → `VBAT`; Vf R 1.85 / G,B 2.9 V, 5 mA |
| 1 | JST 1.25 mm (MX1.25) 2P header — ZX-MX1.25-2PWT | C7430468 | LiPo connector; match the cell's plug, verify polarity vs `BAT+` (or your SHOU HAN `SH-MX1.25-2PWT`) |
| 2 | 18 Ω ¼ W resistor | Basic | IR ballast, one per IR LED |
| 1 | 470 Ω resistor | Basic | red LED ballast (R_r) |
| 2 | 220 Ω resistor | Basic | green/blue LED ballast (R_g/R_b) |
| 1 | 100 Ω resistor | Basic | IR gate series |
| 1 | 100 kΩ resistor | Basic | IR gate pulldown |
| 2 | 56.9 kΩ 1 % resistor | Basic | battery divider → GPIO0 (`CONFIG_ESPIR_BATTERY_DIV_X100 = 200`; 56 kΩ also fine) |
| 2 | 100 kΩ + 150 kΩ resistor | Basic | VBUS-sense divider → GPIO1 |
| 2 | 100 µF cap (≥6.3 V) | Basic | `VBAT` reservoir for the IR pulse bursts |
| 1 | 100 nF cap | Basic | decoupling |

The RGB status LED is driven by the dedicated `slave-pcb/` firmware (LEDC PWM on three GPIOs). Each
colour is switched low-side by a 2N7002 with the LED's common anode on `VBAT`, because the green/blue
dice (`Vf` ≈ 2.9 V) won't light reliably from a 3.3 V pin. The FETs keep the drive active-high, so
`CONFIG_ESPIR_LED_COMMON_ANODE` stays **n**. The solid green only lights while USB is present so it
can't drain the LiPo; omit the VBUS divider (set `ESPIR_LED_VBUS_GPIO = -1`) to disable that gating.
Optional: 3× 100 kΩ gate→GND pulldowns on the RGB FETs kill boot-flicker.

## Notes

- **Power the VS1838B at 3.3 V** (master) — its `OUT` drives a GPIO directly; never 5 V.
- The SZHJW has its own transistor driver; the 3.3 V GPIO drives `DAT` fine. Run `VCC` at 5 V
  (master) for full range; at battery voltage on the slave (reduced range).
- A `~38–100 µF` cap across the SZHJW `VCC`/`GND` helps the LED current bursts.
