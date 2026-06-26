# Fully-custom slave PCB — discrete ESP32-C6 (Board2 / "PCB2")

The most integrated slave build: a **fully custom board around a bare ESP32-C6 (QFN-40)** — no
XIAO module. It folds everything the [XIAO-carrier custom PCB](wiring-slave.md#custom-pcb-xiao-esp32-c6-as-an-smd-part)
got from the module (USB-C, LiPo charging, antenna, 3.3 V regulation, flash, crystal) onto the
board itself, plus the 2-LED IR driver and an addressable RGB status LED.

> **Design intent:** a drop-in functional replacement for the *XIAO-ESP32-C6 + 2-diode IR sender*
> prototype — same SoC family, same IR-sender behaviour, **Zigbee** (the C6's native 802.15.4
> radio), **battery powered with charging**, and **USB-C** for flashing / connection / alt power /
> charging. (See the verification at the bottom.)

Designed in **EasyEDA Pro** (4-layer: Sig / GND / GND / Sig, chip-antenna keep-out, GND pours
stitched). Board file lives in the project's EasyEDA workspace, not in git.

> ⚠️ **Firmware status:** the `slave-pcb/` firmware currently targets the **XIAO-carrier** pinout
> (IR on GPIO18, a 3-channel LEDC common-anode RGB on GPIO17/19/20, battery sense on GPIO0). This
> board moves things (IR → **GPIO2**, status LED → a single **GPIO11** addressable-LED data line,
> sense on **GPIO4/GPIO5**) and flashes over **native USB**. So this board needs a firmware
> variant: remap the IR pin, **swap the 3-channel RGB driver for a WS2812-style single-wire driver**,
> and point battery/VBUS sense at GPIO4/GPIO5. The hardware is done; that firmware delta is the
> open work item.

## Block diagram

```
                USB-C (USBC1)  ──VBUS(5V)──┬────────────────┐
                  │  D+/D- (CC1/CC2 5.1k pulldowns)          │
                  │   │                                      ▼
                  │   │                              MCP73831 charger (U8) ──VBAT──► battery (U7, SH-MX1.25)
                  │   │                                      │                         │
                  │   │                                      └── STAT → LED2           SW3 (slide on/off)
                  │   │                                                                 │
   V5 ──D1(Schottky)──┐                                          VBAT ──────────────────┘
                      ▼                                            │
   VBAT ─Q4(P-FET load-share, gate=V5)─► VSYS ─► AP2112K LDO (U6) ─► 3V3 ─► ESP32-C6 (U5) + flash (U4)
                                                                              │
   ESP32-C6 (U5, QFN-40) ── SPI ──► W25Q32 flash (U4)                         │
        │  XTAL_P/N ── 40 MHz crystal (X1) + load caps                        │
        │  ANT ──R2──► π-match ──R3──► chip antenna (AE1)   (R4──► u.FL RF1)   │
        │  GPIO12/13 ◄── USB D-/D-                                            (decoupling C1..C8)
        │  GPIO2 ──► Q2 gate ──► 2× IR LED (U2,U3 parallel, VSYS via R5/R6) ──► GND
        │  GPIO11 ──R1──► WS2812-style RGB status LED (LED1)
        │  GPIO4 ◄── battery divider (R15/R16)     GPIO5 ◄── VBUS divider (R17/R18)
        │  EN ◄── reset (SW1)   GPIO9 ◄── boot (SW2)
```

## U5 GPIO map (verified from the schematic netlist)

| ESP32-C6 pin | Net | Function |
|---|---|---|
| **GPIO2** | `IR1` | IR drive → Q2 gate (both IR LEDs) |
| **GPIO11** | `WS_GPIO` | Addressable RGB status LED data (→ R1 → LED1.DIN) |
| **GPIO12 / GPIO13** | `USB_DM` / `USB_DP` | Native USB Serial/JTAG → USB-C (firmware upload) |
| **GPIO4** (MTMS) | `BATT_SENSE` | Battery-voltage divider (ADC1) |
| **GPIO5** (MTDI) | `VBUS_SENSE` | USB-present divider (ADC1) |
| **GPIO9** | strap | Boot button (SW2) + pull-up; download mode |
| **GPIO8** | strap | Boot strap + pull-up |
| **EN** (CHIP_PU) | `EN` | Reset button (SW1) + R10 pull-up + C11 |
| **XTAL_P / XTAL_N** | `XIN` / `XOUT` | 40 MHz crystal X1 + load caps C9/C10 |
| **SPICLK/SPICS0/SPID/SPIQ/SPIWP/SPIHD** | SPI | External flash U4 (W25Q32) |
| `ANT` | `ANTRF` | RF feed → R2 → π-match |

> GPIO3 (`IR2`) and GPIO10 (`IR3`) are leftover single-pin nets from an earlier 3-channel IR draft —
> unused on this board (the IR sender is **2 LEDs on one FET**, driven by GPIO2 only). Free for future use.
>
> Battery/VBUS sense sit on **GPIO4/GPIO5 = the JTAG MTMS/MTDI pins** — fine as ADC1 inputs, at the
> cost of hardware-JTAG debug (USB Serial/JTAG still works for flashing/console).

## Bill of materials (verified LCSC parts; passive values per schematic)

**Core / actives**

| Ref | Part | LCSC | Notes |
|-----|------|------|-------|
| U5 | **ESP32-C6** (bare, QFN-40-EP) | `C5364646` | the SoC; needs ext. flash + 40 MHz crystal; ≥9 GND vias in the EPAD |
| U4 | **W25Q32** SPI flash, SOIC-8 | `C82317` | bare C6 has the SPI-flash bus bonded out |
| U6 | **AP2112K-3.3** LDO, SOT-23-5 | `C51118` | VSYS → 3.3 V (low-dropout — works from a ~3.4 V cell; AMS1117 can't) |
| U8 | **MCP73831** LiPo charger, SOT-23-5 | `C42432883` | VBUS → VBAT single-cell charge; STAT → LED2; PROG sets current |
| X1 | **40 MHz** crystal | `C5210647` | required by the C6 |
| Q4 | **AO3401A** P-MOSFET, SOT-23 | `C15127` | load-share (battery → VSYS, gate = V5) |
| D1 | **B5819W** Schottky | `C8598` | USB 5 V → VSYS (load-share OR-ing) |
| Q2 | **AO3400A** N-MOSFET, SOT-23 | `C20917` | low-side IR LED driver (both LEDs) |
| U2, U3 | **IR333C** 5 mm 940 nm IR LED (THT) | `C5200795` | ×2, in parallel — each via its own ballast (R5/R6) |
| LED1 | **WS2812-style addressable RGB** (TCWIN, SMD) | `C48982517` | single-wire status LED on GPIO11 (1=DO,2=GND,3=DIN,4=VDD) |
| LED2 | charge-status LED (SMD) | — | MCP73831 `STAT` indicator |

**Connectors / switches**

| Ref | Part | LCSC | Notes |
|-----|------|------|-------|
| USBC1 | **USB-C** receptacle, 16P | `C165948` | data (D±) + VBUS + CC; flashing, power, charging |
| U7 | **SH-MX1.25-2PWT** battery connector (1.25 mm 2P) | `C53477378` | LiPo cell; verify polarity before first plug-in |
| RF1 | **u.FL / IPEX** RF connector | `C434808` | external-antenna option |
| AE1 | **Johanson 2450AT18A100E** 2.4 GHz chip antenna | `C89334` | on-board antenna (needs copper keep-out) |
| SW1 | tactile button (TS-1187A) | `C318884` | **reset** (EN) |
| SW2 | tactile button (TS-1187A) | `C318884` | **boot** (GPIO9, download mode) |
| SW3 | **SS12D07VG6** SPDT slide switch | `C2939728` | battery on/off (in the VBAT line) |

**Passives (by function — confirm exact values against the schematic / EasyEDA BOM export)**

| Refs | Function | Typical |
|------|----------|---------|
| R5, R6 | IR-LED ballast (VSYS → anode), one per LED | ~18 Ω (see [sizing](wiring-slave.md#build-notes)) |
| R1 | WS2812 data series | ~330 Ω |
| R2 / R3 / R4 | RF feed / π-match / antenna-select (chip vs u.FL) | 0 Ω select — **populate R3 *xor* R4** |
| R10 | EN pull-up | 100 kΩ |
| R8, R9 | GPIO9 / GPIO8 strap pull-ups | 100 kΩ |
| R11, R12 | USB-C CC1/CC2 pulldowns | 5.1 kΩ |
| R13 | charger PROG (sets charge current) | e.g. 2 kΩ ≈ 500 mA |
| R14 | charge-status LED ballast | per LED |
| R15, R16 | battery divider → GPIO4 | per `ESPIR_BATTERY_DIV_X100` |
| R17, R18 | VBUS divider → GPIO5 | ~100 k / 150 k |
| C1–C8 | V3V3 decoupling (one per C6 power pin) | 100 nF |
| C9, C10 | crystal load caps | ~10 pF (per crystal CL) |
| C11 | EN cap | 100 nF–1 µF |
| C12 / C13 / C14 / C15 | V5 bulk / VBAT / VSYS / V3V3 reservoirs | 1–10 µF |

> The authoritative, value-complete BOM is the **EasyEDA `Export → Bill of Materials`** for PCB2 —
> regenerate it at order time (live LCSC stock changes daily).

## Power architecture

- **USB present:** VBUS(5 V) → D1 (Schottky) → VSYS, *and* → MCP73831 charges the LiPo. Q4 (P-FET,
  gate tied to V5) is **off** with USB present, so the battery doesn't back-feed VSYS.
- **On battery:** V5 = 0 → Q4 turns **on** → VBAT → VSYS. SW3 is the battery disconnect (storage
  off; charging only happens with SW3 on, since charge current flows through it).
- **Regulation:** VSYS → AP2112K LDO → 3.3 V for the C6 + flash. (Low-dropout so it still regulates
  near end-of-charge ~3.4 V.)
- **Telemetry:** battery divider → GPIO4, VBUS-present divider → GPIO5 (both ADC1).

## RF / Zigbee

ESP32-C6 has a native **IEEE 802.15.4** radio → Zigbee/Thread. Feed: `U5.ANT → R2 → ANTFEED`,
then **R3 → AE1** (Johanson chip antenna) **or R4 → RF1** (u.FL). **Populate only one of R3/R4.**
The chip antenna needs a copper keep-out (no pour/plane under or beside it) on all layers — done on
the layout. 40 MHz crystal X1 is mandatory.

## IR sender (matches the 2-diode prototype)

Two 940 nm IR LEDs (U2, U3) in **parallel**, each with its own ballast (R5/R6) from VSYS, cathodes
tied to **Q2** (AO3400A low-side N-FET) drain; **GPIO2** drives the gate. One signal fires both —
the same topology as the original XIAO 2-diode sender. Ballast/range sizing carries over from
[wiring-slave.md → Build notes](wiring-slave.md#build-notes).

## Requirements check (BOM + netlist verified)

| Requirement | Met? | Evidence |
|---|---|---|
| Drop-in replacement for XIAO-C6 + 2-diode IR sender | ✅ (functional) | U5 = ESP32-C6; `IRD1 = U2.C, U3.C, Q2.D` (both LED cathodes + FET drain), gate `IR1 = U5.GPIO2` — 2 LEDs on one FET. *(Functional, not the XIAO footprint.)* |
| Zigbee | ✅ | ESP32-C6 802.15.4 radio; `ANTRF→R2→R3→AE1` (u.FL alt via R4); 40 MHz X1 present |
| Battery + charging | ✅ | U7 battery, U8 MCP73831 charger, Q4/D1 load-share, U6 LDO, SW3 disconnect, R15/16 sense |
| USB-C connect / flash / alt-power / charge | ✅ | `USB_DM→GPIO12`, `USB_DP→GPIO13` (native USB Serial/JTAG); VBUS → charger + load-share; CC 5.1 k pulldowns |

**Open items:** firmware variant for this pinout (IR GPIO2, WS2812 RGB on GPIO11, sense GPIO4/5,
USB-native flashing); populate one antenna (R3 *xor* R4); the human finishing pass on the layout
(minor nudging, pour touch-ups, EP via array — see below).

## Notes from layout

- **EP grounding:** the QFN exposed pad must tie to GND with a **via array in the pad** (Espressif
  spec ≥9 ground vias in the C6 EPAD), small (~0.3 mm) and plugged/tented to avoid reflow
  solder-wicking — done on the footprint, not just a single stitching via.
- 4-layer stack with inner GND; chip-antenna copper keep-out before pouring; net-class widths for
  power/IR/RF; auto-routed then hand-finished.
