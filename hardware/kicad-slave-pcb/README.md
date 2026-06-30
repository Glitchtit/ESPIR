# ESPIR slave PCB — KiCad

The KiCad realization of the ESPIR fully-custom slave board. The original is
captured in EasyEDA Pro (see [`../pcb-fully-custom.md`](../pcb-fully-custom.md));
that board file "lives in the EasyEDA workspace, not in git." This directory is
the same circuit rebuilt **in KiCad**, fully in-repo and reproducible from source.

Schematic (connect-by-name net labels) and board placement:

![schematic preview](schematic-preview.png)
![placement preview](placement-preview.png)

## What this is

The circuit is described **as code** with [SKiDL](https://github.com/devbisme/skidl)
(`espir_slave_pcb.py`) — a Python netlist HDL that looks up real KiCad library
symbols and emits an authoritative KiCad netlist. From that one netlist:
`make_schematic.py` generates an openable **`.kicad_sch`** (every part + a net
label on each pin — connect-by-name), and `kinet2pcb` + a `pcbnew` script produce
the **`.kicad_pcb`** (footprints, nets, functional placement, board outline).

```
                    ┌─ make_schematic.py ─▶  espir_slave_pcb.kicad_sch  (schematic)
espir_slave_pcb.py ─┤  (+ SKiDL ERC)
   (the circuit)    └─ espir_slave_pcb.net ─ kinet2pcb ─▶ espir_slave_pcb.kicad_pcb ─ place_and_outline.py
```

| File | What |
|------|------|
| `espir_slave_pcb.py`     | **The circuit** — SKiDL source (parts + nets). The schematic-of-record. |
| `espir_slave_pcb.net`    | Generated KiCad netlist (authoritative connectivity). |
| `make_schematic.py`      | netlist → `.kicad_sch` (real symbols embedded; connect-by-name labels). |
| `espir_slave_pcb.kicad_sch` | KiCad schematic: 38 components, 25 nets — connectivity is an **exact match** to the netlist. |
| `place_and_outline.py`   | `pcbnew` pass: functional placement + Edge.Cuts outline. |
| `espir_slave_pcb.kicad_pcb` | KiCad board: 38 footprints, all nets, 52×47 mm outline. |
| `espir_slave_pcb.kicad_dru` | Critical-signal **routing rules** (power width, IR-pulse width, sense-vs-IR clearance, USB width) — DRC auto-enforces them. |
| `espir_slave_pcb.kicad_pro` | KiCad project (open this). |
| `build.sh`               | Reproduces everything from source. |

> The schematic uses **connect-by-name net labels** (a label on every pin) rather
> than drawn wires — the standard scriptable way to express a netlist graphically.
> KiCad re-extracts the identical 25-net netlist from it (verified). Its ERC items
> (off-grid label endpoints, floating NC pins, power-pin-not-driven, lib-symbol
> cache mismatch) are cosmetic, not connectivity errors. Drawing wires / tidy
> placement is GUI finishing work.

## Adjustment vs the EasyEDA discrete-C6 board

Per request this build uses the **ESP32-C6-MINI-1 module** instead of the bare
ESP32-C6 QFN-40. The module integrates the SPI flash, 40 MHz crystal + load caps,
RF chip antenna + π-match, and the EP grounding — so the circuit **drops** those
discrete parts entirely:

| Dropped (now internal to the module) | Was in the discrete design |
|---|---|
| `U4` W25Q32 flash + decoupling | external SPI flash |
| `X1` 40 MHz crystal + `C9`/`C10` | external crystal + load caps |
| `AE1` chip antenna, `RF1` u.FL, `R2`/`R3`/`R4` π-match | external antenna chain |

Everything else carries over: USB-C + load-share + LiPo-charger power tree,
AP2112K 3V3 LDO, 2-LED discrete IR driver, addressable RGB status LED,
battery/VBUS sense dividers, reset/boot buttons, battery slide switch.

**GPIO remap:** the MINI-1 module does not bring out GPIO11, so the WS2812
status-LED data line moves **GPIO11 → GPIO7** (a free, non-strapping exposed pin).
All other pins are unchanged: IR on GPIO2, battery/VBUS sense on GPIO4/GPIO5,
USB on GPIO12/GPIO13, boot on GPIO9, strap pull-up on GPIO8.

> The `slave-pcb/` firmware would need the same one-line pin change
> (`WS_GPIO 11 → 7`) it already needs for this board family.

## Bill of materials (38 parts)

ESP32-C6-MINI-1 module · AP2112K-3.3 LDO · MCP73831 LiPo charger · AO3401A
load-share P-FET · B5819W (1N5819) OR-ing Schottky · AO3400A IR-driver N-FET ·
2× IR333C 940 nm IR LED · WS2812 RGB status LED · charge LED · USB-C (HRO
TYPE-C-31-M-12) · JST-SH LiPo connector · SS12D07VG6 slide switch · 2× tactile
buttons · 17 resistors · 7 capacitors. Full values + LCSC numbers are in
[`../pcb-fully-custom.md`](../pcb-fully-custom.md).

## Build / reproduce

```bash
sudo pacman -S --needed kicad kicad-library
python -m venv --system-site-packages .venv && . .venv/bin/activate
pip install skidl kinet2pcb
./build.sh
```

## Verification

- **ERC (SKiDL): 0 errors.** The 18 warnings are all *intentional* unconnected
  pins — the module's spare GPIOs (IO0/1/3/6/14/15/18–23, RXD0/TXD0), the USB-C
  SBU pins, the slide-switch OFF throw, and the WS2812 DOUT (no daisy-chain).
  Every wired pin lands on the right net.
- **Schematic (`kicad-cli sch export netlist`):** KiCad loads the generated
  `.kicad_sch` and re-extracts the **exact same 25 multi-pin nets** as the SKiDL
  netlist (verified set-equal). The schematic is a faithful graphical view of the
  circuit.
- **Board (`kicad-cli pcb drc`):** the only "errors" are the **114 unconnected
  ratsnest items** (the board is placed but *not yet routed*) plus silkscreen-text
  overlaps, the module's antenna keep-out zone, and edge-connector clearances.
  **No connectivity/circuit errors.**

## Remaining work (the human finishing pass)

Same division of labour the EasyEDA workflow uses — the circuit + a clean,
function-grouped starting layout are done; the fab finishing is left to a human:

1. **Route** the 114 ratsnest connections (KiCad has no headless autorouter;
   route interactively or via the Freerouting plugin). Route critical signals
   first — the rules in `espir_slave_pcb.kicad_dru` (power ≥0.5 mm, IR-pulse
   ≥0.4 mm, sense taps ≥1 mm from the IR drive, USB width) are DRC-enforced. For
   a true 90 Ω USB pair, rename `USB_DM`/`USB_DP` → `USB_D-`/`USB_D+`, set the
   stackup, and use Route → Differential Pair + Tune Skew.
2. **GND copper pours** top + bottom, stitched with vias.
3. Nudge silkscreen, confirm the **antenna keep-out** is clear of copper and the
   module antenna overhangs the top edge, verify **edge connectors** (USB-C bottom,
   JST left, buttons right) sit flush at their edges.
4. Set net-class track widths (Power/IR already declared in the project) and
   export Gerber/BOM/CPL.
