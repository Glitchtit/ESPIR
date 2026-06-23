# Wiring — Slave (Seeed XIAO ESP32-C6)

Battery-powered (LiPo on the BAT pads, charges over USB-C). Transmit-only — no receiver.
There is **no 5 V rail on battery**, so the IR transmitter runs at battery voltage
(~3.7–4.2 V), which reduces range. Add a boost converter only if needed.

## Pin assignments (defaults — override in `menuconfig` → ESPIR Configuration)

| Signal | XIAO pad | C6 GPIO | Connects to |
|--------|----------|---------|-------------|
| IR TX data | **D2** | GPIO2 | SZHJW `DAT` |
| Power for IR TX | `BAT+` (or `5V` when on USB) | — | SZHJW `VCC` |
| GND | `GND` | — | SZHJW `GND` |
| LiPo + | `BAT+` | — | battery + |
| LiPo − | `BAT−` | — | battery − |

```
XIAO ESP32-C6                     SZHJW IR TX (2× 940nm)
  BAT+ ──────────────────────────► VCC   (≈3.7–4.2V on battery; reduced range)
  D2 (GPIO2) ────────────────────► DAT
  GND ───────────────────────────► GND

  BAT+ / BAT− ◄──────────────────  3.7V LiPo cell
  USB-C ............................ charges the LiPo, also powers the unit when plugged
```

## Notes

- **Battery sense (optional, deferred):** the XIAO C6 has no dedicated battery-ADC divider.
  To report battery % via the Zigbee Power Configuration cluster, add an external divider to a
  free ADC pin (e.g. A0/GPIO0) and enable it in `menuconfig`. Omitted in v1.
- A 38–100 µF cap across the SZHJW `VCC`/`GND` is more important here than on the master —
  the LED pulses sag a small LiPo.
- If range is too short for the slave's appliance, add a 3.3 V→5 V boost feeding the SZHJW
  `VCC` (the GPIO still drives `DAT` at 3.3 V — fine for the module's transistor).
- Sleepy End Device: the firmware polls its parent on an interval (default ~1.5 s, set in
  `menuconfig`). Shorter = snappier sends but more battery drain.
