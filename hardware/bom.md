# Bill of Materials

## Master unit

| Qty | Part | Notes |
|-----|------|-------|
| 1 | ESP32-C6-DevKitC-1 (or clone, 4 MB+ flash) | USB-powered; provides 5 V rail |
| 1 | SZHJW dual-LED IR transmitter module | 5 V, `DAT/VCC/GND`, 940 nm, no internal carrier |
| 1 | **VS1838B** or TSOP38238 38 kHz IR receiver | **to buy** — the learning sensor |
| — | jumper wires | — |

## Slave unit (per extra coverage point)

| Qty | Part | Notes |
|-----|------|-------|
| 1 | Seeed XIAO ESP32-C6 | onboard LiPo charging + battery pads |
| 1 | SZHJW dual-LED IR transmitter module | runs at battery voltage (reduced range) |
| 1 | 3.7 V LiPo cell | with JST connector to match the XIAO BAT pads |
| 1 | (optional) 3.3 V→5 V boost converter | only if slave IR range proves insufficient |

## On hand but fallback only

| Part | Why fallback |
|------|--------------|
| UART IR decoder/encoder module (own MCU, RXD/TXD) | decodes only a fixed protocol set (likely NEC); opaque firmware. The VS1838B + RMT raw-capture path learns essentially any remote, so it is primary. |

## Notes

- **Power the VS1838B at 3.3 V**, never 5 V — its output drives a GPIO directly and must stay
  at 3.3 V logic levels.
- The SZHJW module has its own transistor driver; the 3.3 V GPIO drives the base fine. Put VCC
  at 5 V (master) for full ~1–3 m range.
- A `~38–100 µF` cap across the IR transmitter's VCC/GND helps the LED current bursts,
  especially on battery.
