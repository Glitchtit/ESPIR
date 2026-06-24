# Wiring — Slave (Seeed XIAO ESP32-C6)

Battery-powered (LiPo on the BAT pads, charges over USB-C). Transmit-only — no receiver.
There is **no 5 V rail on battery**, so the IR transmitter runs at battery voltage
(~3.7–4.2 V), which reduces range. Add a boost converter only if needed.

## Pin assignments (defaults — override in `menuconfig` → ESPIR Configuration)

| Signal | XIAO pad | C6 GPIO | Connects to |
|--------|----------|---------|-------------|
| IR TX data | **D10** | GPIO18 | SZHJW `DAT` |
| Power for IR TX | `BAT+` (battery) or `3V3`/`5V` (USB) | — | SZHJW `VCC` |
| GND | `GND` | — | SZHJW `GND` |
| **Battery sense** | **A0 / D0** | GPIO0 | divider mid-point (see below) |
| LiPo + | `BAT+` | — | battery + |
| LiPo − | `BAT−` | — | battery − |

```
XIAO ESP32-C6                     SZHJW IR TX (2× 940nm)
  BAT+ ──────────────────────────► VCC   (≈3.7–4.2V on battery; reduced range)
  D10 (GPIO18) ──────────────────► DAT
  GND ───────────────────────────► GND

  BAT+ / BAT− ◄──────────────────  3.7V LiPo cell
  USB-C ............................ charges the LiPo, also powers the unit when plugged

Battery sense (1:2 divider — required for the battery indicator):

  BAT+ ──[ R_top 55kΩ ]──┬──► D0 (= A0 = GPIO0)
                         │
                      [ R_bottom 55kΩ ]
                         │
                        GND
```

> On the XIAO ESP32-C6 the pin is silkscreened **D0** — it is the same pin as **A0 / GPIO0**.


## Battery indicator

The firmware reads `A0`/GPIO0, undoes the divider, maps 3.30 V→0 % … 4.20 V→100 %, and reports
**battery %** and **voltage** over the standard Zigbee Power Configuration cluster (0x0001)
every ~10 minutes. Z2M then shows a battery entity (the `ESPIR-SLAVE` converter exposes
`battery` + `battery_voltage`).

- **Recommended: two 55 kΩ** (a 1:2 divider → the default ÷2). Low impedance (~27 kΩ Thévenin),
  so the ESP32 ADC reads it accurately with **no cap needed**. Idle drain ~38 µA — negligible
  for a LiPo. This is what works reliably in practice on GPIO0.
- **Avoid 1 MΩ:1 MΩ here.** Tested on hardware it under-reads even with a 100 nF cap: a 4.0 V
  cell gave 1.68 V at D0 instead of 2.0 V. The pin's tiny DC leakage current loads a divider
  that stiff, dragging the node down; the cap only fixes AC settling, not the DC droop. Keep the
  divider impedance low (tens of kΩ).
- **Different/unequal resistors?** Set the ratio in `menuconfig` → ESPIR Configuration →
  *Battery divider ratio ×100* (`CONFIG_ESPIR_BATTERY_DIV_X100`):
  `(R_top + R_bottom) * 100 / R_bottom`. Example: 55 kΩ:55 kΩ → **200**. Put the *larger*
  resistor on top and keep the divided voltage under ~2.5 V at 4.2 V full so the ADC doesn't clip.
- The ADC pin is `CONFIG_ESPIR_BATTERY_ADC_GPIO` (default 0 = A0); must be an ADC1 pin (GPIO0–6).

## Notes

- A 38–100 µF cap across the SZHJW `VCC`/`GND` is more important here than on the master —
  the LED pulses sag a small LiPo.
- If range is too short for the slave's appliance, add a 3.3 V→5 V boost feeding the SZHJW
  `VCC` (the GPIO still drives `DAT` at 3.3 V — fine for the module's transistor).
- Sleepy End Device: sleep is join-safe and enabled (`CONFIG_PM_ENABLE`). Commands/sends are
  delivered on the next parent poll, so a held send fires a fraction of a second after the master.
- XIAO antenna: GPIO3/GPIO14 select the onboard vs external antenna (handled in firmware;
  see `CONFIG_ESPIR_XIAO_EXT_ANTENNA`). Onboard is the default.

## Custom PCB (XIAO ESP32-C6 as an SMD part)

For a permanent slave you can drop the jumper wires and the SZHJW module entirely: reflow the
XIAO onto a small carrier PCB and build the IR transmitter from discrete parts driven **straight
from GPIO18**. A logic-level N-MOSFET switches two 940 nm LEDs; the SZHJW's internal transistor
is just moved onto your board. The IR/battery **pins are unchanged** — GPIO18 still emits the
38 kHz `DAT` envelope (now into the MOSFET gate), GPIO0 still reads the battery divider. The board
also adds a discrete RGB status LED (see below), so it has its own firmware target, **`slave-pcb/`**
— identical to `slave/` plus the LED. Everything else matches
[the module section above](#pin-assignments-defaults--override-in-menuconfig--espir-configuration).

### Components

| Ref | Part | Value / notes |
|-----|------|---------------|
| U1 | Seeed XIAO ESP32-C6 | reflowed on castellations; supplies `BAT+`, `GND`, drives GPIO18/GPIO0 |
| Q1 | N-channel logic-level MOSFET, SOT-23 | **AO3400A** (or SI2302 / DMN2075U): Vgs(th) ≤ ~1.5 V, Id ≥ 0.5 A, low Rds(on) @ Vgs = 3.3 V |
| LED1, LED2 | 940 nm IR LED | same emitters as the SZHJW; in parallel, each via its own ballast |
| R1, R2 | Ballast resistor | **18 Ω, ≥ ¼ W** (1206 or axial) — one per LED |
| Rg | Gate series resistor | **100 Ω** (0603) — tames the gate edge into GPIO18 |
| R_pd | Gate pulldown | **100 kΩ** (0603) — holds Q1 off while GPIO18 is hi-Z |
| R_top, R_bot | Battery divider | **2× 55 kΩ** (or 56 kΩ E12), 1 % — `CONFIG_ESPIR_BATTERY_DIV_X100 = 200` |
| C_res | Reservoir cap | **100 µF** low-ESR (47–220 µF, MLCC/tantalum) at the LED anodes |
| C_dec | Decoupling | **100 nF** (0603) across `VBAT`/GND |
| J1 | (optional) JST-PH 2.0 | LiPo connector to the XIAO `BAT+`/`BAT−` pads |

### Connections

Only four signals leave the XIAO; `3V3` is not needed and GPIO3/GPIO14 antenna-select stay
on-module.

| XIAO pad | Net | Goes to |
|----------|-----|---------|
| `BAT+` | `VBAT` | R1/R2 (LED anodes), C_res +, C_dec +, R_top (divider) |
| `D10` (GPIO18) | `IR_DRIVE` | Rg → Q1 gate (R_pd gate→GND) |
| `A0`/`D0` (GPIO0) | `BATT_SENSE` | divider mid-point (R_top / R_bot) |
| `GND` | `GND` | Q1 source, C_res −, C_dec −, R_bot, LED return (via Q1 drain) |

```
 VBAT (BAT+) ──┬──[ R1 18Ω ]──▶|─ LED1 ┐
               │                        ├──┐
               ├──[ R2 18Ω ]──▶|─ LED2 ┘  │ drain
               │                          ┌┴┐
               ├──[ C_res 100µF ]── GND   │Q│ Q1  AO3400 (N-MOSFET, low-side)
               ├──[ C_dec 100nF ]── GND   └┬┘
               │                           │ source
               └──[ R_top 55kΩ ]──┬──► A0/GPIO0 (batt sense)
                                  │        │
                              [ R_bot 55kΩ ]
                                  │       GND
                                 GND

 D10/GPIO18 ──[ Rg 100Ω ]──┬──────────────► Q1 gate
                           │
                       [ R_pd 100kΩ ]
                           │
                          GND
```

### Status RGB LED

The `slave-pcb/` firmware drives a discrete RGB LED straight from three GPIOs (LEDC PWM) to show
device state:

| State | LED |
|-------|-----|
| Searching for the Zigbee network | amber, slow blink |
| Joined / connected, idle | green, solid — **only while USB-powered** |
| Sending an IR frame | blue, rapid blink |
| On battery, idle | off (green suppressed — see below) |

**Why green is USB-gated:** a steady LED draws a few mA — ~100× the sleepy-ED idle budget — so on
battery the persistent green is suppressed to keep the months-long battery life. The amber-search
and blue-send blinks still run on battery (the CPU is already awake while commissioning or sending).
USB presence is read on a VBUS-sense GPIO; omit that divider (or set `ESPIR_LED_VBUS_GPIO = -1`) to
force the LED always-on at the cost of battery life.

| Ref | Part | Value / notes |
|-----|------|---------------|
| LED3 | RGB LED, common-cathode (or -anode) | 4-pin; set `ESPIR_LED_COMMON_ANODE` to match |
| R_r | Red series resistor | ~**330 Ω** |
| R_g, R_b | Green/blue series resistors | ~**150 Ω** (green/blue Vf ≈ 3 V leaves little 3.3 V headroom) |
| R_v1, R_v2 | VBUS-sense divider | **100 kΩ** (top, from `5V`) : **150 kΩ** (bottom) → ~3.0 V |

Connections (defaults — `menuconfig` → ESPIR Configuration → *RGB status LED*):

| XIAO pad | C6 GPIO | Net | Goes to |
|----------|---------|-----|---------|
| D7 | GPIO17 | LED_R | R_r → LED red |
| D8 | GPIO19 | LED_G | R_g → LED green |
| D9 | GPIO20 | LED_B | R_b → LED blue |
| D1 | GPIO1 | VBUS_SENSE | divider mid-point (`5V` ÷ ~1.67) |
| — | — | LED common | → GND (common-cathode) **or** `3V3` (common-anode) |

```
  XIAO 5V ──[ R_v1 100kΩ ]──┬──► D1/GPIO1   (USB present → HIGH; 0 V on battery)
                            │
                        [ R_v2 150kΩ ]
                            │
                           GND

  D7/GPIO17 ──[ R_r 330Ω ]──►|─┐
  D8/GPIO19 ──[ R_g 150Ω ]──►|─┤  common-cathode RGB LED
  D9/GPIO20 ──[ R_b 150Ω ]──►|─┘
                               │ common
                              GND       (tie common to 3V3 for a common-anode part)
```

> Pins recap (custom PCB): IR = D10/GPIO18, batt sense = D0/GPIO0, LED = D7/D8/D9, VBUS = D1.
> D2–D6 stay free. At 3.3 V the blue/green legs are indicator-bright, not floodlights — pick an
> efficient/low-Vf RGB LED, or drive the LEDs from `VBAT` via small transistors if you want punch.

### Build notes

- **Gate pulldown is not optional on a battery device.** While the XIAO boots or deep-sleeps,
  GPIO18 is high-impedance; R_pd (100 kΩ) keeps Q1 off so the LEDs can't sit half-on and quietly
  drain the cell.
- **Ballast sizing:** `R = (VBAT − Vf − Vds(on)) / I`. With Vf ≈ 1.5 V @ 150 mA and Vds(on) ≈ 0,
  `(4.2 − 1.5) / 0.15 ≈ 18 Ω`. Current tapers to ~100 mA at a 3.3 V cell — range falls gracefully
  with charge. Burst dissipation ≈ 0.15²·18 ≈ 0.4 W peak, ~0.13 W average at 33 % duty, and only
  during the ~300 ms `CONFIG_ESPIR_SEND_HOLD_MS` send window, so a ¼ W part runs cool. To rescale:
  pick your per-LED current `I`, recompute `R`, and confirm `R`'s wattage ≥ `I²·R`. For more LEDs,
  add a parallel `R + LED` leg each (Q1 carries the sum — AO3400A is good for well over 1 A).
- **Reservoir cap** supplies the ~300 mA pulse bursts (2 LEDs × 150 mA) so the small LiPo doesn't
  sag and brown out the XIAO — same reason the SZHJW build wants a cap, more important here.
- **XIAO as SMD:** the module has 2×7 castellated edge pads (2.54 mm pitch) plus two `BAT+`/`BAT−`
  pads on the underside. Reflow on the castellations and add matching pads (or a cutout) under the
  BAT pads on your carrier. Keep the USB-C edge clear of the board outline for charging/flashing.
  Seeed publishes a KiCad/EAGLE "XIAO" footprint you can drop in.
- **Still no 5 V on battery**, so the LEDs run at `VBAT` and range is shorter than the 5 V master.
  If that isn't enough, the optional 3.3 V→5 V boost from the Notes above can feed `VBAT` of the
  LED leg (the GPIO/MOSFET gate stays at 3.3 V) — resize R1/R2 for the higher rail.
