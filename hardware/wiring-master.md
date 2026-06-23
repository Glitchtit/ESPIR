# Wiring — Master (ESP32-C6-DevKitC-1 + YS-IRTM + SZHJW)

The master uses two IR parts:
- **YS-IRTM** — a self-contained NEC codec over **UART** (9600 8N1). Used for **learning**
  (its receiver). Its own emitter is weak, so it is **not** used for transmitting.
- **SZHJW** dual-LED transmitter on the **RMT** peripheral — used for **transmitting**
  (software 38 kHz carrier, 5 V, much stronger / longer range).

USB-powered; both modules run from the board's 5 V pin.

## Pin assignments (defaults — override in `menuconfig` → ESPIR Configuration)

| Signal | C6 GPIO | Connects to |
|--------|---------|-------------|
| UART TX | **GPIO5** | YS-IRTM `RXD` |
| UART RX | **GPIO4** | YS-IRTM `TXD` (through a level divider) |
| IR TX data | **GPIO6** | SZHJW `DAT` |
| 5 V | `5V` pin | YS-IRTM `5V` **and** SZHJW `VCC` |
| GND | any `GND` | YS-IRTM `GND` **and** SZHJW `GND` |

```
DevKitC-1                         YS-IRTM (NEC codec, 5 V) — LEARN
  GPIO5 (UART TX) ───────────────► RXD
  GPIO4 (UART RX) ◄───[ 10k ]──┬─── TXD
                             [ 20k ]
  5V ─────────────────┬───────────► 5V
  GND ──────────┬──────┼──────────► GND
                │      │
DevKitC-1       │      └──────────► VCC   SZHJW dual-LED TX (5 V) — SEND
  GPIO6 ────────┼─────────────────► DAT
  GND ──────────┘─────────────────► GND
```

## Important notes

- **Level shift on TXD.** The C6 is **not 5 V tolerant**. The YS-IRTM `TXD` idles/drives at
  5 V, so put a divider between `TXD` and `GPIO4`: `TXD —10kΩ— GPIO4 —20kΩ— GND` gives ~3.3 V.
  (A proper level-shifter works too.)
- **C6 TX → YS-IRTM RXD** at 3.3 V is normally read as logic-high by the module; if learning
  is flaky, add a level shifter here as well.
- **UART port:** default `UART_NUM_1` (UART0 is the USB console — don't use it). GPIO4/5 are
  free; avoid GPIO12/13 (USB-Serial-JTAG) and strapping pins GPIO8/9/15.
- **NEC-only:** the YS-IRTM decodes/encodes NEC-family protocols (uPD6121, TC9012, …). Remotes
  using RC5/RC6/Sony/AC protocols cannot be learned on the master.
- **Transmit = SZHJW** (`CONFIG_ESPIR_MASTER_USE_SZHJW`, default on). Aim its two LEDs at the
  appliance. To fall back to the YS-IRTM emitter instead, disable that option in `menuconfig`.
  Only one transmitter fires per send (firing both would risk a double-press, e.g. a power
  toggle cancelling itself).
- A `~38–100 µF` cap across the SZHJW `VCC`/`GND` stiffens the LED current pulses.
- The module's default UART address is `0xA1` (the firmware uses it); `0xFA` is the failsafe
  address. Baud is configurable on the module (4800–57600) but the firmware expects 9600.
