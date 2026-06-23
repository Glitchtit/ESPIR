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
| 1 | SZHJW dual-LED IR transmitter module | 5 V, `DAT/VCC/GND`, 940 nm, no internal carrier; runs at battery voltage (reduced range) |
| 1 | 3.7 V LiPo cell | with JST connector to match the XIAO BAT pads |
| 1 | (optional) 3.3 V→5 V boost converter | only if slave IR range proves insufficient |

The slave transmits with the SZHJW emitter driven by the C6's RMT peripheral (software
38 kHz carrier). It replays the codes the master learned.

## Notes

- **Power the VS1838B at 3.3 V** (master) — its `OUT` drives a GPIO directly; never 5 V.
- The SZHJW has its own transistor driver; the 3.3 V GPIO drives `DAT` fine. Run `VCC` at 5 V
  (master) for full range; at battery voltage on the slave (reduced range).
- A `~38–100 µF` cap across the SZHJW `VCC`/`GND` helps the LED current bursts.
