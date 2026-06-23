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

  BAT+ ──[ R1 200kΩ ]──┬──► A0 (GPIO0)
                       │
                     [ R2 200kΩ ]
                       │
                      GND
```

## Battery indicator

The firmware reads `A0`/GPIO0, multiplies by 2 (the 1:2 divider), maps 3.30 V→0 % …
4.20 V→100 %, and reports **battery %** and **voltage** over the standard Zigbee Power
Configuration cluster (0x0001) every ~10 minutes. Z2M then shows a battery entity (the
`ESPIR-SLAVE` converter exposes `battery` + `battery_voltage`).

- Use **two equal resistors** (200 kΩ each shown; 100 kΩ–1 MΩ all work — higher = less drain
  but noisier). Equal values give the ÷2 the firmware assumes; change `ESPIR_BATT_MV_*` or the
  ratio in `espir_device.c` if you use a different ratio.
- The ADC pin is set by `menuconfig` → ESPIR Configuration → *Battery sense ADC GPIO*
  (`CONFIG_ESPIR_BATTERY_ADC_GPIO`, default 0 = A0). Must be an ADC1 pin (GPIO0–6).
- Tip: a high-value divider leaks a little current continuously; 200 kΩ–1 MΩ keeps it small.
  If you want zero idle drain, gate the divider with a MOSFET — not done here for simplicity.

## Notes

- A 38–100 µF cap across the SZHJW `VCC`/`GND` is more important here than on the master —
  the LED pulses sag a small LiPo.
- If range is too short for the slave's appliance, add a 3.3 V→5 V boost feeding the SZHJW
  `VCC` (the GPIO still drives `DAT` at 3.3 V — fine for the module's transistor).
- Sleepy End Device: sleep is join-safe and enabled (`CONFIG_PM_ENABLE`). Commands/sends are
  delivered on the next parent poll, so a held send fires a fraction of a second after the master.
- XIAO antenna: GPIO3/GPIO14 select the onboard vs external antenna (handled in firmware;
  see `CONFIG_ESPIR_XIAO_EXT_ANTENNA`). Onboard is the default.
```
