# Wiring — Master (ESP32-C6-DevKitC-1 + SZHJW + VS1838B)

The master uses two simple IR parts straight on the ESP32-C6 **RMT** peripheral:
- **SZHJW** dual-LED transmitter — **sending** (software 38 kHz carrier, 5 V, strong).
- **VS1838B** (or TSOP38238) 38 kHz receiver — **learning** (raw capture). Powered at 3.3 V
  so its output is GPIO-safe; no level shifter needed.

Raw capture learns essentially **any** remote; NEC is auto-compacted for cheap storage/replication.

USB-powered. (The YS-IRTM module is no longer used.)

## Pin assignments (defaults — override in `menuconfig` → ESPIR Configuration)

| Signal | C6 GPIO | Connects to |
|--------|---------|-------------|
| IR TX data | **GPIO6** | SZHJW `DAT` |
| IR RX data | **GPIO4** | VS1838B `OUT` |
| 5 V | `5V` pin | SZHJW `VCC` |
| 3.3 V | `3V3` pin | VS1838B `VCC` — **3.3 V only** (output drives a GPIO) |
| GND | any `GND` | SZHJW `GND` **and** VS1838B `GND` |

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

## Notes

- **Power the VS1838B at 3.3 V**, never 5 V — its `OUT` goes straight to `GPIO4`.
- The SZHJW has its own transistor driver; the 3.3 V GPIO drives `DAT` fine. Put `VCC` at 5 V
  for full ~1–3 m range; a `~38–100 µF` cap across `VCC`/`GND` stiffens the LED pulses.
- Aim the VS1838B toward where you'll hold the remote; aim the SZHJW LEDs at the appliance.
- Avoid GPIO12/13 (USB-Serial-JTAG) and strapping pins GPIO8/9/15. GPIO4/6 are clear.
- **Learns any protocol** (raw envelope); NEC/RC5/etc. all work. No NEC-only limitation.
- Logs/flashing go over the DevKitC's CH343 USB-UART port (console = UART0), not the native
  USB-Serial-JTAG port — see `AGENTS.md`.
