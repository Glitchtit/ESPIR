# Wiring — Master (ESP32-C6-DevKitC-1 + YS-IRTM)

The master's IR front end is a **YS-IRTM** module — a self-contained NEC codec with its own
IR emitter and receiver that talks **UART** (default 9600 8N1). It handles both learning and
transmitting, so the master needs neither the SZHJW emitter nor a VS1838B.

USB-powered; the YS-IRTM runs from the board's 5 V pin.

## Pin assignments (defaults — override in `menuconfig` → ESPIR Configuration)

| Signal | C6 GPIO | YS-IRTM pin |
|--------|---------|-------------|
| UART TX | **GPIO5** | `RXD` |
| UART RX | **GPIO4** | `TXD` (through a level divider) |
| 5 V | `5V` pin | `5V` |
| GND | any `GND` | `GND` |

```
DevKitC-1                         YS-IRTM (NEC codec module, 5 V)
  5V   ──────────────────────────► 5V
  GPIO5 (UART TX) ───────────────► RXD
  GPIO4 (UART RX) ◄───[ 10k ]──┬─── TXD
                             [ 20k ]
  GND ────────────────────────┴──► GND
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
- **Range:** the YS-IRTM has a single emitter LED. Aim it at the appliance; for other rooms
  use a slave.
- The module's default UART address is `0xA1` (the firmware uses it); `0xFA` is the failsafe
  address. Baud is configurable on the module (4800–57600) but the firmware expects 9600.
