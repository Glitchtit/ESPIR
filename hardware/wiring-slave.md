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
| U1 | Seeed XIAO ESP32-C6 (LCSC `C9900124963`) | reflowed on castellations; supplies `BAT+`, `GND`, drives GPIO18/GPIO0 |
| Q1 | **AO3400A** N-MOSFET, SOT-23 (LCSC `C20917`) | logic-level: Vgs(th) ≤ ~1.5 V, Id ≥ 0.5 A, low Rds(on) @ Vgs = 3.3 V (SI2302 / DMN2075U equiv.) |
| LED1, LED2 | 940 nm IR LED — M3030E1IRS6G45-940 (`Vf` 1.4–2.0 V @ 350 mA, `If` 350 mA) | TCWIN; in parallel, each via its own ballast — confirm the C# on its LCSC page |
| R1, R2 | Ballast resistor | **18 Ω, ≥ ¼ W** (1206 or axial) — one per LED |
| Rg | Gate series resistor | **100 Ω** (0603) — tames the gate edge into GPIO18 |
| R_pd | Gate pulldown | **100 kΩ** (0603) — holds Q1 off while GPIO18 is hi-Z |
| R_top, R_bot | Battery divider | **2× 55 kΩ** (or 56 kΩ E12), 1 % — `CONFIG_ESPIR_BATTERY_DIV_X100 = 200` |
| C_res | Reservoir cap | **100 µF** low-ESR (47–220 µF, MLCC/tantalum) at the LED anodes |
| C_dec | Decoupling | **100 nF** (0603) across `VBAT`/GND |
| J1 | JST 1.25 mm (MX1.25) 2P header — ZX-MX1.25-2PWT (LCSC `C7430468`) | LiPo connector to the XIAO `BAT+`/`BAT−` pads — **match the cell's plug & verify polarity** |

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

The `slave-pcb/` firmware drives an RGB LED (LEDC PWM on three GPIOs) to show device state:

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

**Driven from `VBAT`, not the 3.3 V GPIO.** Green/blue LED dice have `Vf ≈ 2.9 V` (and up to ~3.4 V
across RGB parts) — at or near a 3.3 V pin, so direct drive leaves them dark or barely lit. Instead each colour is switched
low-side by a small N-MOSFET with the LED's common anode on `VBAT` (~4 V), which gives real
headroom. The MOSFETs keep the firmware **active-high** (GPIO high → FET on → colour on), so
`CONFIG_ESPIR_LED_COMMON_ANODE` stays **n** even though the LED package is common-anode.

| Ref | Part | Value / notes |
|-----|------|---------------|
| LED3 | RGB LED, **common-anode** — Everlight 22-23C/R6GHBHW-C01/2C (LCSC `C181865`) | anode → `VBAT`; Vf R 1.85 / G,B 2.9 V, 5 mA; verify the colour-cathode pad order |
| Q_r, Q_g, Q_b | N-MOSFET, SOT-23 — 2N7002 (LCSC `C8545`) | low-side switch, one per colour; gate ← D7/D8/D9 |
| R_r | Red ballast | **470 Ω** — `(4.0−1.85)/470 ≈ 4.6 mA` |
| R_g, R_b | Green/blue ballast | **220 Ω** — `(4.0−2.9)/220 ≈ 5.0 mA` (≈ the LED's 5 mA nominal) |
| R_v1, R_v2 | VBUS-sense divider | **100 kΩ** (top, from `5V`) : **150 kΩ** (bottom) → ~3.0 V |

Connections (defaults — `menuconfig` → ESPIR Configuration → *RGB status LED*):

| XIAO pad | C6 GPIO | Net | Goes to |
|----------|---------|-----|---------|
| D7 | GPIO17 | LED_R | Q_r gate (drain → R_r → LED red cathode) |
| D8 | GPIO19 | LED_G | Q_g gate (drain → R_g → LED green cathode) |
| D9 | GPIO20 | LED_B | Q_b gate (drain → R_b → LED blue cathode) |
| D1 | GPIO1 | VBUS_SENSE | divider mid-point (`5V` ÷ ~1.67) |
| `BAT+` | — | VBAT | LED common anode |
| `GND` | — | GND | Q_r/Q_g/Q_b sources |

```
  XIAO 5V ──[ R_v1 100kΩ ]──┬──► D1/GPIO1   (USB present → HIGH; 0 V on battery)
                            │
                        [ R_v2 150kΩ ]
                            │
                           GND

  VBAT ──● LED3 common anode
         ├─ red   ──[ R_r 470Ω ]──┐
         ├─ green ──[ R_g 220Ω ]──┼─┐
         └─ blue  ──[ R_b 220Ω ]──┼─┼─┐   (drains)
                                  │ │ │
  D7/GPIO17 ──gate─ Q_r ──────────┘ │ │
  D8/GPIO19 ──gate─ Q_g ────────────┘ │
  D9/GPIO20 ──gate─ Q_b ──────────────┘
                     └── all sources → GND
```

> Pins recap (custom PCB): IR = D10/GPIO18, batt sense = D0/GPIO0, LED gates = D7/D8/D9, VBUS = D1.
> D2–D6 stay free. Gate pulldowns on Q_r/Q_g/Q_b are optional — LEDC drives the gates low within a
> few ms of boot and holds them low in light sleep; add 100 kΩ gate→GND only to kill boot-flicker.

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
- **More range if needed:** the 940 nm emitters are rated 350 mA continuous, so 18 Ω (~150 mA)
  is deliberately conservative for battery life. If the appliance doesn't respond reliably, drop
  the ballast for more reach — ≈10 Ω → ~250 mA or ≈8.2 Ω → ~300 mA per LED — but then use ½ W
  ballasts and a larger `C_res` to feed the bigger (2× peak) bursts without sagging the cell.

  | Ballast | ~Peak / LED | Resistor | Trade-off |
  |---------|-------------|----------|-----------|
  | **18 Ω** | ~150 mA | ¼ W | default — longest battery life |
  | ~10 Ω | ~250 mA | ½ W | more range |
  | ~8.2 Ω | ~300 mA | ½ W | near the 350 mA rating; size `C_res` for the bursts |
- **Reservoir cap** supplies the ~300 mA pulse bursts (2 LEDs × 150 mA) so the small LiPo doesn't
  sag and brown out the XIAO — same reason the SZHJW build wants a cap, more important here.
- **XIAO as SMD:** the module has 2×7 castellated edge pads (2.54 mm pitch) plus two `BAT+`/`BAT−`
  pads on the underside. Reflow on the castellations and add matching pads (or a cutout) under the
  BAT pads on your carrier. Keep the USB-C edge clear of the board outline for charging/flashing.
  Seeed publishes a KiCad/EAGLE "XIAO" footprint you can drop in.
- **Battery connector (J1):** these cells use a **JST 1.25 mm (MX1.25) 2P** plug — a 1.25 mm-pitch
  part, *not* the 2.0 mm JST-PH. Fit the matching MX1.25 header and **check polarity before first
  plug-in**: 1.25 mm LiPo plugs aren't polarity-standardised between vendors, and the XIAO `BAT+`
  pad feeds its charger directly, so a reversed cell can damage the board. Confirm the header's
  `BAT+` pin lines up with the cell's positive lead.
- **Still no 5 V on battery**, so the LEDs run at `VBAT` and range is shorter than the 5 V master.
  If that isn't enough, the optional 3.3 V→5 V boost from the Notes above can feed `VBAT` of the
  LED leg (the GPIO/MOSFET gate stays at 3.3 V) — resize R1/R2 for the higher rail.

### Netlist (refdes + nets)

A full connection list for laying out / ERC-checking the board. Refdes use the schematic scheme;
the functional names from the tables above map as: `R1/R2`=IR ballast, `R3`=`Rg`, `R4`=`R_pd`,
`R5/R6`=battery divider, `R7`=`R_r`, `R8`=`R_g`, `R9`=`R_b`, `R10`=`R_v1`, `R11`=`R_v2`,
`C1/C2`=`C_res`, `C3`=`C_dec`, `Q2/Q3/Q4`=`Q_r/Q_g/Q_b` (RGB colour FETs). The RGB LED is
**common-anode** (anode on `VBAT`), switched low-side by Q2/Q3/Q4. XIAO pin numbers follow the
24-pin module symbol (`+`/`−` = the bottom `BAT+`/`BAT−` pads).

| Net | Pins |
|-----|------|
| **GND** | U1.GND(13), U1.GND(16), U1.−(24), CN1.2, Q1.S, Q2.S, Q3.S, Q4.S, R4.2, R6.2, R11.2, C1.−, C2.−, C3.− |
| **VBAT** (BAT+; IR + RGB + XIAO supply) | U1.+(23), CN1.1, R1.1, R2.1, R5.1, RGB1.A, C1.+, C2.+, C3.+ |
| **V5** (USB 5 V rail) | U1.5V(14), R10.1 |
| **3V3** (regulator out; unused) | U1.3V3(12), U1.3V3(18) |
| **IR_DRIVE** | U1.D10(11), R3.1 |
| **GATE** | R3.2, Q1.G, R4.1 |
| **IRLED1_A** | R1.2, LED1.A |
| **IRLED2_A** | R2.2, LED2.A |
| **IR_RTN** (drain) | Q1.D, LED1.K, LED2.K |
| **BATT_SENSE** | R5.2, R6.1, U1.D0(1) |
| **VBUS_SENSE** | R10.2, R11.1, U1.D1(2) |
| **LEDR_G** (red gate) | U1.D7(8), Q2.G |
| **RGB_R** | RGB1.R, R7.1 |
| **QR_D** | R7.2, Q2.D |
| **LEDG_G** (green gate) | U1.D8(9), Q3.G |
| **RGB_G** | RGB1.G, R8.1 |
| **QG_D** | R8.2, Q3.D |
| **LEDB_G** (blue gate) | U1.D9(10), Q4.G |
| **RGB_B** | RGB1.B, R9.1 |
| **QB_D** | R9.2, Q4.D |

**No-connect** (flag NC for ERC): U1 D2(3), D3(4), D4(5), D5(6), D6(7), MTCK(17), MTDI(15),
MTDI(19), EN(20), MTMS(21), BOOT(22).

> Per IR leg: `VBAT → R(18 Ω) → LED.A → LED.K → Q1.D → Q1.S → GND` (ballast may sit either side of
> the LED). Per RGB colour: `VBAT → RGB1 anode → colour die → Rx → Qx.D → Qx.S → GND`, gate from the
> D-pin. The FETs make the LED **active-high**, so keep `CONFIG_ESPIR_LED_COMMON_ANODE=n` despite the
> common-anode package. Optional 100 kΩ gate→GND pulldowns on Q2/Q3/Q4 kill any boot-flicker.
