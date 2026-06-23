# Wiring — Master (ESP32-C6-DevKitC-1)

USB-powered. Has a 5 V pin (from USB) for the IR transmitter, and 3.3 V for the receiver.

## Pin assignments (defaults — override in `menuconfig` → ESPIR Configuration)

| Signal | C6 GPIO | Connects to |
|--------|---------|-------------|
| IR TX data | **GPIO5** | SZHJW `DAT` |
| IR RX data | **GPIO4** | VS1838B `OUT` |
| 5 V | `5V` pin | SZHJW `VCC` |
| 3.3 V | `3V3` pin | VS1838B `VCC` |
| GND | any `GND` | SZHJW `GND` **and** VS1838B `GND` (common ground) |

```
DevKitC-1                         SZHJW IR TX (2× 940nm)
  5V  ───────────────────────────► VCC
  GPIO5 ─────────────────────────► DAT
  GND ──────────────┬────────────► GND
                    │
DevKitC-1           │              VS1838B receiver
  3V3 ──────────────┼────────────► VCC   (3.3V ONLY — output drives a GPIO)
  GPIO4 ◄───────────┼──────────────  OUT
  GND ──────────────┴────────────► GND
```

## Notes

- Avoid GPIO12/13 (USB-Serial-JTAG), and the strapping pins GPIO8/9/15. GPIO4/5 are clear.
- DevKitC-1 has an addressable RGB LED on GPIO8 — usable later as a learn-status indicator,
  but it is a strapping pin, so leave it floating at boot.
- Optional: 38–100 µF cap across the SZHJW `VCC`/`GND` to stiffen the LED current pulses.
- Aim the VS1838B toward where you will hold the remote; aim the SZHJW LEDs at the appliance.
