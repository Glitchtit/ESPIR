# Bill of Materials

## Master unit

| Qty | Part | Notes |
|-----|------|-------|
| 1 | ESP32-C6-DevKitC-1 (or clone, 4 MB+ flash) | USB-powered; provides 5 V rail |
| 1 | **YS-IRTM** NEC IR codec module (`GND/RXD/TXD/5V`) | self-contained emitter + receiver, UART 9600 8N1; does learn **and** transmit |
| 2 | resistors (~10 kΩ + 20 kΩ) | divider for YS-IRTM `TXD` (5 V) → C6 RX (3.3 V) |
| — | jumper wires | — |

The YS-IRTM only handles **NEC-family** remotes — that is the master's learning limitation.

## Slave unit (per extra coverage point)

| Qty | Part | Notes |
|-----|------|-------|
| 1 | Seeed XIAO ESP32-C6 | onboard LiPo charging + battery pads |
| 1 | SZHJW dual-LED IR transmitter module | 5 V, `DAT/VCC/GND`, 940 nm, no internal carrier; runs at battery voltage (reduced range) |
| 1 | 3.7 V LiPo cell | with JST connector to match the XIAO BAT pads |
| 1 | (optional) 3.3 V→5 V boost converter | only if slave IR range proves insufficient |

The slave transmits with the SZHJW emitter driven by the C6's RMT peripheral (software
38 kHz carrier). It re-encodes the NEC codes the master learned.

## Notes

- **Level-shift the YS-IRTM `TXD`** into the C6 RX pin — the C6 is not 5 V tolerant. The C6's
  3.3 V TX into the YS-IRTM `RXD` is usually fine as-is.
- The SZHJW module (slave) has its own transistor driver; the 3.3 V GPIO drives the base fine.
- A `~38–100 µF` cap across the SZHJW `VCC`/`GND` helps the LED current bursts, especially on
  battery.
