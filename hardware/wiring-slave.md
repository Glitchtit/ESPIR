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
| U2, U3 | 940 nm IR LED — M3030E1IRS6G45-940 (`Vf` 1.4–2.0 V @ 350 mA, `If` 350 mA) | TCWIN; in parallel, each via its own ballast — confirm the C# on its LCSC page |
| R1, R2 | Ballast resistor | **18 Ω, ≥ ¼ W** (1206 or axial) — one per LED |
| R3 | Gate series resistor | **100 Ω** (0603) — tames the gate edge into GPIO18 |
| R4 | Gate pulldown | **100 kΩ** (0603) — holds Q1 off while GPIO18 is hi-Z |
| R5, R6 | Battery divider | **2× 56.9 kΩ** (or 56 kΩ), 1 % — `CONFIG_ESPIR_BATTERY_DIV_X100 = 200` |
| C1, C2 | Reservoir caps | **100 µF** low-ESR (47–220 µF, MLCC/tantalum) at the LED anodes |
| C3 | Decoupling — 100 nF 0603 X7R (LCSC `C14663`) | across `VBAT`/GND, at the XIAO power pin |
| CN1 | JST 1.25 mm (MX1.25) 2P header — ZX-MX1.25-2PWT (LCSC `C7430468`) | LiPo connector to the XIAO `BAT+`/`BAT−` pads — **match the cell's plug & verify polarity** |
| SW1 | SPDT slide switch, SMD — SS12D07VG6 (LCSC `C2939728`) | on/off: in series in the `BAT+` line (common + one throw) — see *Build notes* |

### Connections

Only four signals leave the XIAO; `3V3` is not needed and GPIO3/GPIO14 antenna-select stay
on-module.

| XIAO pad | Net | Goes to |
|----------|-----|---------|
| `BAT+` | `VBAT` | R1/R2 (LED anodes), C1/C2 +, C3 +, R5 (divider) — fed via SW1 from CN1 |
| `D10` (GPIO18) | `IR_DRIVE` | R3 → Q1 gate (R4 gate→GND) |
| `A0`/`D0` (GPIO0) | `BATT_SENSE` | divider mid-point (R5 / R6) |
| `GND` | `GND` | Q1 source, C1/C2 −, C3 −, R6, LED return (via Q1 drain) |

```
 CN1 (cell +) ──[ SW1 on/off ]──● VBAT  (= XIAO BAT+ pad)

 VBAT ─┬──[ R1 18Ω ]──▶|─ U2 ─┐
       ├──[ R2 18Ω ]──▶|─ U3 ─┴─► Q1 drain   (Q1 = AO3400 low-side N-MOSFET;
       │                                         source → GND, gate ← R3)
       ├──[ C1 100µF ]── GND
       ├──[ C2 100µF ]── GND
       ├──[ C3 100nF ]── GND      ← decoupling, place at the XIAO power pin
       │
       └──[ R5 56.9kΩ ]──┬──► A0/GPIO0 (batt sense)
                         │
                     [ R6 56.9kΩ ]
                         │
                        GND

 D10/GPIO18 ──[ R3 100Ω ]──┬──► Q1 gate
                           │
                       [ R4 100kΩ ]
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
| LED1 | RGB LED, **common-anode** — Everlight 22-23C/R6GHBHW-C01/2C (LCSC `C181865`) | anode → `VBAT`; Vf R 1.85 / G,B 2.9 V, 5 mA; verify the colour-cathode pad order |
| Q2, Q3, Q4 | N-MOSFET, SOT-23 — 2N7002 (LCSC `C8545`) | low-side switch, one per colour (R/G/B); gate ← D7/D8/D9 |
| R7 | Red ballast | **470 Ω** — `(4.0−1.85)/470 ≈ 4.6 mA` |
| R8, R9 | Green/blue ballast | **220 Ω** — `(4.0−2.9)/220 ≈ 5.0 mA` (≈ the LED's 5 mA nominal) |
| R10, R11 | VBUS-sense divider | **100 kΩ** (top, from `5V`) : **150 kΩ** (bottom) → ~3.0 V |

Connections (defaults — `menuconfig` → ESPIR Configuration → *RGB status LED*):

| XIAO pad | C6 GPIO | Net | Goes to |
|----------|---------|-----|---------|
| D7 | GPIO17 | LED_R | Q2 gate (drain → R7 → LED1 red cathode) |
| D8 | GPIO19 | LED_G | Q3 gate (drain → R8 → LED1 green cathode) |
| D9 | GPIO20 | LED_B | Q4 gate (drain → R9 → LED1 blue cathode) |
| D1 | GPIO1 | VBUS_SENSE | divider mid-point (`5V` ÷ ~1.67) |
| `BAT+` | — | VBAT | LED1 common anode |
| `GND` | — | GND | Q2/Q3/Q4 sources |

```
  XIAO 5V ──[ R10 100kΩ ]──┬──► D1/GPIO1   (USB present → HIGH; 0 V on battery)
                           │
                       [ R11 150kΩ ]
                           │
                          GND

  VBAT ──● LED1 common anode
         ├─ red   ──[ R7 470Ω ]──┐
         ├─ green ──[ R8 220Ω ]──┼─┐
         └─ blue  ──[ R9 220Ω ]──┼─┼─┐   (drains)
                                 │ │ │
  D7/GPIO17 ──gate─ Q2 ──────────┘ │ │
  D8/GPIO19 ──gate─ Q3 ────────────┘ │
  D9/GPIO20 ──gate─ Q4 ──────────────┘
                    └── all sources → GND
```

> Pins recap (custom PCB): IR = D10/GPIO18, batt sense = D0/GPIO0, LED gates = D7/D8/D9, VBUS = D1.
> D2–D6 stay free. Gate pulldowns on Q2/Q3/Q4 are optional — LEDC drives the gates low within a
> few ms of boot and holds them low in light sleep; add 100 kΩ gate→GND only to kill boot-flicker.

### Build notes

- **Gate pulldown is not optional on a battery device.** While the XIAO boots or deep-sleeps,
  GPIO18 is high-impedance; R4 (100 kΩ) keeps Q1 off so the LEDs can't sit half-on and quietly
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
  ballasts and larger `C1/C2` to feed the bigger (2× peak) bursts without sagging the cell.

  | Ballast | ~Peak / LED | Resistor | Trade-off |
  |---------|-------------|----------|-----------|
  | **18 Ω** | ~150 mA | ¼ W | default — longest battery life |
  | ~10 Ω | ~250 mA | ½ W | more range |
  | ~8.2 Ω | ~300 mA | ½ W | near the 350 mA rating; size `C1/C2` for the bursts |
- **Reservoir cap** supplies the ~300 mA pulse bursts (2 LEDs × 150 mA) so the small LiPo doesn't
  sag and brown out the XIAO — same reason the SZHJW build wants a cap, more important here.
- **XIAO as SMD:** the module has 2×7 castellated edge pads (2.54 mm pitch) plus two `BAT+`/`BAT−`
  pads on the underside. Reflow on the castellations and add matching pads (or a cutout) under the
  BAT pads on your carrier. Keep the USB-C edge clear of the board outline for charging/flashing.
  Seeed publishes a KiCad/EAGLE "XIAO" footprint you can drop in.
- **Battery connector (CN1):** these cells use a **JST 1.25 mm (MX1.25) 2P** plug — a 1.25 mm-pitch
  part, *not* the 2.0 mm JST-PH. Fit the matching MX1.25 header and **check polarity before first
  plug-in**: 1.25 mm LiPo plugs aren't polarity-standardised between vendors, and the XIAO `BAT+`
  pad feeds its charger directly, so a reversed cell can damage the board. Confirm the header's
  `BAT+` pin lines up with the cell's positive lead.
- **On/off switch (SW1):** a slide switch in series in the `BAT+` line, between CN1 and the XIAO
  `BAT+` pad. OFF disconnects the cell, so there's **zero battery drain in storage**. It's a battery
  *disconnect*, not a hard kill: the XIAO still runs from USB whenever it's plugged in (its own
  USB-C feeds the regulator), and **charging only happens with SW1 ON** (charge current flows through
  it). A ~300 mA-rated switch is plenty — `C1/C2` buffers the IR peaks, so SW1 only carries the burst
  *average* (~100–150 mA) plus the XIAO's charge current. Wire common + one throw; leave the third
  pin NC. Put SW1 on the cell side so `C1/C2`/`C3` and the divider all sit on the switched `VBAT`.
- **Still no 5 V on battery**, so the LEDs run at `VBAT` and range is shorter than the 5 V master.
  If that isn't enough, the optional 3.3 V→5 V boost from the Notes above can feed `VBAT` of the
  LED leg (the GPIO/MOSFET gate stays at 3.3 V) — resize R1/R2 for the higher rail.

### Flashing (sleepy end device — mind the wake window)

The XIAO C6 exposes only the **native USB-Serial-JTAG** (no UART bridge), and once the firmware
runs it's a Zigbee **sleepy end device** — the serial only answers for a brief window right after
a reset. A plain `idf.py -p /dev/ttyACM0 flash` usually fails with `OSError: [Errno 71] Protocol
error` (esptool's `default_reset` toggles RTS on the CDC-ACM link, and/or the chip is already
asleep); changing `--before` does not help.

Reliable method — hammer `write_flash` in a loop, then reset the board so an attempt lands in the
wake window (usually within a few dozen tries). From `slave-pcb/build`:

```sh
. ~/esp/esp-idf/export.sh
while true; do
  python -m esptool --chip esp32c6 -p /dev/ttyACM0 -b 460800 \
    --before default_reset --after hard_reset --connect-attempts 1 \
    write_flash @flash_args && break
  sleep 0.3
done
```

Holding **BOOT** while tapping **RESET** forces ROM download mode = unlimited window (cleanest if
the loop keeps missing). This wired flash is a one-time step to get onto the OTA dual-slot layout;
afterwards updates go wireless via Zigbee OTA (`AGENTS.md` → *OTA release*).

### Netlist (refdes + nets)

A full connection list for laying out / ERC-checking the board, using the same refdes as the
schematic: U1 = XIAO, U2/U3 = IR LEDs, Q1 = IR FET, Q2/Q3/Q4 = RGB colour FETs, LED1 = RGB LED,
R1/R2 = IR ballast, R3/R4 = IR gate series/pulldown, R5/R6 = battery divider, R7/R8/R9 = RGB
ballasts, R10/R11 = VBUS divider, C1/C2 = reservoir, C3 = decoupling, CN1 = battery connector,
SW1 = on/off. The RGB LED is **common-anode** (anode on `VBAT`), switched low-side by Q2/Q3/Q4.
SW1 is in series in the `BAT+` line (`VCELL` → `VBAT`). XIAO pin numbers follow the 24-pin module
symbol (`+`/`−` = the bottom `BAT+`/`BAT−` pads).

| Net | Pins |
|-----|------|
| **GND** | U1.GND(13), U1.GND(16), U1.−(24), CN1.2, Q1.S, Q2.S, Q3.S, Q4.S, R4.2, R6.2, R11.2, C1.−, C2.−, C3.− |
| **VCELL** (cell + before the switch) | CN1.1, SW1.2 (common) |
| **VBAT** (switched BAT+; IR + RGB + XIAO) | U1.+(23), SW1.1 (throw), R1.1, R2.1, R5.1, LED1.A, C1.+, C2.+, C3.+ |
| **V5** (USB 5 V rail) | U1.5V(14), R10.1 |
| **3V3** (regulator out; unused) | U1.3V3(12), U1.3V3(18) |
| **IR_DRIVE** | U1.D10(11), R3.1 |
| **GATE** | R3.2, Q1.G, R4.1 |
| **IR_U2_A** | R1.2, U2.A |
| **IR_U3_A** | R2.2, U3.A |
| **IR_RTN** (drain) | Q1.D, U2.K, U3.K |
| **BATT_SENSE** | R5.2, R6.1, U1.D0(1) |
| **VBUS_SENSE** | R10.2, R11.1, U1.D1(2) |
| **LEDR_G** (red gate) | U1.D7(8), Q2.G |
| **RGB_R** | LED1.R, R7.1 |
| **QR_D** | R7.2, Q2.D |
| **LEDG_G** (green gate) | U1.D8(9), Q3.G |
| **RGB_G** | LED1.G, R8.1 |
| **QG_D** | R8.2, Q3.D |
| **LEDB_G** (blue gate) | U1.D9(10), Q4.G |
| **RGB_B** | LED1.B, R9.1 |
| **QB_D** | R9.2, Q4.D |

**No-connect** (flag NC for ERC): U1 D2(3), D3(4), D4(5), D5(6), D6(7), MTCK(17), MTDI(15),
MTDI(19), EN(20), MTMS(21), BOOT(22); SW1.3 (unused throw). SW1 pin 2 = common — verify against the
SS12D07VG6 footprint.

> Per IR leg: `VBAT → R(18 Ω) → LED.A → LED.K → Q1.D → Q1.S → GND` (ballast may sit either side of
> the LED). Per RGB colour: `VBAT → LED1 anode → colour die → Rx → Qx.D → Qx.S → GND`, gate from the
> D-pin. The FETs make the LED **active-high**, so keep `CONFIG_ESPIR_LED_COMMON_ANODE=n` despite the
> common-anode package. Optional 100 kΩ gate→GND pulldowns on Q2/Q3/Q4 kill any boot-flicker.
