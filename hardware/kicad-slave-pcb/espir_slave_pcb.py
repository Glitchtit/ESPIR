#!/usr/bin/env python3
"""
ESPIR slave PCB — circuit-as-code for KiCad (SKiDL).

This is the KiCad realization of the ESPIR fully-custom slave board originally
captured in EasyEDA Pro (see ../pcb-fully-custom.md). It is built with SKiDL:
the circuit is described in Python, which generates an authoritative KiCad
netlist (espir_slave_pcb.net). The netlist is then turned into a .kicad_pcb
(footprints + nets + ratsnest) with kinet2pcb — see build.sh.

ADJUSTMENT vs the EasyEDA discrete-C6 board (per request: use ESP32-C6-MINI-1):
  The bare ESP32-C6 QFN-40 is replaced by the ESP32-C6-MINI-1 module, which
  already integrates the SPI flash (W25Q32), the 40 MHz crystal + load caps,
  the RF chip antenna + pi-match, and the QFN EP grounding. So this design
  DROPS those discrete parts entirely:
      - U4  W25Q32 flash + its decoupling          -> internal to module
      - X1  40 MHz crystal + C9/C10 load caps       -> internal to module
      - AE1 chip antenna, RF1 u.FL, R2/R3/R4 pi-match-> internal to module
  Everything else carries over: USB-C + load-share + LiPo charger power tree,
  AP2112K 3V3 LDO, 2-LED discrete IR driver, addressable RGB status LED,
  battery/VBUS sense dividers, reset/boot buttons, battery slide switch.

  GPIO REMAP: the module does not bring out GPIO11, so the WS2812 status-LED
  data line moves GPIO11 -> GPIO7 (IO7, a free non-strapping pin). IR stays on
  GPIO2; battery/VBUS sense on GPIO4/GPIO5; USB on GPIO12/13; boot on GPIO9.

Run:  python espir_slave_pcb.py   (emits espir_slave_pcb.net + runs ERC)
"""

from skidl import (Part, Net, TEMPLATE, generate_netlist, ERC, set_default_tool,
                   KICAD9, POWER)
import skidl

set_default_tool(KICAD9)

# ----------------------------------------------------------------------------
# Footprint short-hands
# ----------------------------------------------------------------------------
R0603  = "Resistor_SMD:R_0603_1608Metric"
C0603  = "Capacitor_SMD:C_0603_1608Metric"
C0805  = "Capacitor_SMD:C_0805_2012Metric"
SOT235 = "Package_TO_SOT_SMD:SOT-23-5"
SOT23  = "Package_TO_SOT_SMD:SOT-23"

# ----------------------------------------------------------------------------
# Nets (the rails / signals of the board)
# ----------------------------------------------------------------------------
GND   = Net('GND')
V5    = Net('V5')          # USB VBUS, 5 V
VSYS  = Net('VSYS')        # system rail after load-share (USB-or-battery)
VBAT  = Net('VBAT')        # battery rail (downstream of the slide switch)
VCELL = Net('VCELL')       # raw cell terminal (upstream of the slide switch)
V3V3  = Net('V3V3')        # AP2112K output, powers the module

IR_GATE = Net('IR_GATE')   # GPIO2 -> series R -> Q2 gate
IRD     = Net('IRD')       # IR-LED cathodes <-> Q2 drain (pulsed LED current)
WS_DATA = Net('WS_DATA')   # GPIO7 -> series R -> WS2812 DIN  (was GPIO11)
USB_DP  = Net('USB_DP')    # GPIO13
USB_DM  = Net('USB_DM')    # GPIO12
BATT_SENSE = Net('BATT_SENSE')  # GPIO4 (ADC1)
VBUS_SENSE = Net('VBUS_SENSE')  # GPIO5 (ADC1)
EN      = Net('EN')        # module reset
BOOT    = Net('BOOT')      # GPIO9 download-mode button
STAT    = Net('STAT')      # MCP73831 open-drain charge status
IR1     = Net('IR1')       # GPIO2 raw drive (pre series-R)
WS_GPIO = Net('WS_GPIO')   # GPIO7 raw drive (pre series-R)
STRAP8  = Net('STRAP8')    # GPIO8 strap pull-up node

# Mark the source rails as driven so ERC doesn't flag power-input-not-driven.
# (V3V3 is driven by the LDO VOUT and VBAT by the charger, so they don't need it.)
for rail in (GND, V5, VSYS):
    rail.drive = POWER

# ----------------------------------------------------------------------------
# U5 — ESP32-C6-MINI-1 module (MCU + radio + flash + crystal + antenna)
# ----------------------------------------------------------------------------
U5 = Part('RF_Module', 'ESP32-C6-MINI-1', value='ESP32-C6-MINI-1',
          footprint='RF_Module:ESP32-C6-MINI-1')
U5.ref = 'U5'
U5['3V3'] += V3V3
U5['GND'] += GND          # all GND pins (incl. module EP) -> GND
U5['EN']  += EN
U5['IO2'] += IR1          # GPIO2 IR drive (pre series-R)
U5['IO7'] += WS_GPIO      # GPIO7 status-LED drive (pre series-R)
U5['IO4'] += BATT_SENSE
U5['IO5'] += VBUS_SENSE
U5['IO12'] += USB_DM      # USB D-
U5['IO13'] += USB_DP      # USB D+
U5['IO9'] += BOOT         # boot/download button
# Strapping pin GPIO8 needs a pull-up for normal boot.
U5['IO8'] += STRAP8
# Module NC pins and spare GPIOs (IO0/1/3/6/14/15/18-23, RXD0/TXD0) are left
# intentionally unconnected (free for future use).

# ----------------------------------------------------------------------------
# Power input: USB-C receptacle (VBUS, CC pulldowns, D+/D-)
# ----------------------------------------------------------------------------
USBC1 = Part('Connector', 'USB_C_Receptacle_USB2.0_16P', value='USB-C',
             footprint='Connector_USB:USB_C_Receptacle_HRO_TYPE-C-31-M-12')
USBC1.ref = 'USBC1'
USBC1['VBUS'] += V5       # all 4 VBUS pins
USBC1['GND']  += GND      # all 4 GND pins
USBC1['SHIELD'] += GND
USBC1['D+'] += USB_DP     # A6 & B6 tied (USB2.0)
USBC1['D-'] += USB_DM     # A7 & B7 tied
# SBU1/SBU2 left unconnected (no alt-mode)

R11 = Part('Device', 'R', value='5.1k', footprint=R0603, ref='R11')  # CC1 pulldown
R12 = Part('Device', 'R', value='5.1k', footprint=R0603, ref='R12')  # CC2 pulldown
USBC1['CC1'] += R11[1]; R11[2] += GND
USBC1['CC2'] += R12[1]; R12[2] += GND

# ----------------------------------------------------------------------------
# LiPo charger — MCP73831 (VBUS -> VBAT), STAT LED, PROG sets current
# ----------------------------------------------------------------------------
U8 = Part('Battery_Management', 'MCP73831-2-OT', value='MCP73831-2',
          footprint=SOT235, ref='U8')
# Pin names carry LaTeX braces (V_{DD}); address by number: 1 STAT,2 VSS,3 VBAT,4 VDD,5 PROG
U8[4] += V5      # VDD
U8[2] += GND     # VSS
U8[3] += VBAT    # VBAT
U8[1] += STAT    # STAT
R13 = Part('Device', 'R', value='2k', footprint=R0603, ref='R13')    # PROG ~500 mA
U8[5] += R13[1]; R13[2] += GND

LED2 = Part('Device', 'LED', value='CHG', footprint="LED_SMD:LED_0603_1608Metric",
            ref='LED2')
R14 = Part('Device', 'R', value='1k', footprint=R0603, ref='R14')
V5 += R14[1]; R14[2] += LED2['A']; LED2['K'] += STAT  # lights while charging

# ----------------------------------------------------------------------------
# Power path: load-share P-FET (Q4) + OR-ing Schottky (D1), battery slide switch
# ----------------------------------------------------------------------------
# USB present: V5 -> D1 -> VSYS. On battery: V5=0 -> Q4 on -> VBAT -> VSYS.
D1 = Part('Diode', '1N5819', value='B5819W', footprint="Diode_SMD:D_SOD-123",
          ref='D1')
D1['A'] += V5; D1['K'] += VSYS

Q4 = Part('Transistor_FET', 'AO3401A', value='AO3401A', footprint=SOT23, ref='Q4')
Q4['S'] += VBAT          # source at battery
Q4['G'] += V5            # gate = VBUS -> off when USB present
Q4['D'] += VSYS          # drain to system rail

# Battery connector + slide-switch disconnect (cell -> SW3 -> VBAT)
U7 = Part('Connector_Generic', 'Conn_01x02', value='LiPo',
          footprint='Connector_JST:JST_SH_SM02B-SRSS-TB_1x02-1MP_P1.00mm_Horizontal',
          ref='U7')
U7[1] += VCELL; U7[2] += GND
SW3 = Part('Switch', 'SW_SPDT', value='SS12D07VG6',
           footprint='Button_Switch_SMD:SW_SPDT_Shouhan_MSK12C02', ref='SW3')
SW3[3] += VCELL          # common -> cell
SW3[1] += VBAT           # ON throw -> battery rail
# SW3[2] OFF throw left unconnected

# ----------------------------------------------------------------------------
# 3V3 regulation — AP2112K-3.3 LDO (VSYS -> 3V3)
# ----------------------------------------------------------------------------
U6 = Part('Regulator_Linear', 'AP2112K-3.3', value='AP2112K-3.3',
          footprint=SOT235, ref='U6')
U6['VIN'] += VSYS
U6['EN']  += VSYS        # always-on
U6['GND'] += GND
U6['VOUT'] += V3V3
# U6 NC pin left unconnected

# ----------------------------------------------------------------------------
# Bulk + decoupling
# ----------------------------------------------------------------------------
C12 = Part('Device', 'C', value='10uF', footprint=C0805, ref='C12'); V5   += C12[1]; C12[2] += GND
C13 = Part('Device', 'C', value='10uF', footprint=C0805, ref='C13'); VBAT += C13[1]; C13[2] += GND
C14 = Part('Device', 'C', value='10uF', footprint=C0805, ref='C14'); VSYS += C14[1]; C14[2] += GND
C15 = Part('Device', 'C', value='10uF', footprint=C0805, ref='C15'); V3V3 += C15[1]; C15[2] += GND
C1  = Part('Device', 'C', value='100nF', footprint=C0603, ref='C1');  V3V3 += C1[1];  C1[2]  += GND  # module 3V3 bypass
C2  = Part('Device', 'C', value='100nF', footprint=C0603, ref='C2');  V3V3 += C2[1];  C2[2]  += GND

# ----------------------------------------------------------------------------
# Reset (EN) + boot (GPIO9) buttons and straps
# ----------------------------------------------------------------------------
R10 = Part('Device', 'R', value='100k', footprint=R0603, ref='R10'); V3V3 += R10[1]; R10[2] += EN     # EN pull-up
C11 = Part('Device', 'C', value='100nF', footprint=C0603, ref='C11'); EN  += C11[1]; C11[2] += GND    # EN cap
SW1 = Part('Switch', 'SW_Push', value='RESET',
           footprint='Button_Switch_THT:SW_PUSH_6mm', ref='SW1')
SW1[1] += EN;   SW1[2] += GND
SW2 = Part('Switch', 'SW_Push', value='BOOT',
           footprint='Button_Switch_THT:SW_PUSH_6mm', ref='SW2')
SW2[1] += BOOT; SW2[2] += GND
R8 = Part('Device', 'R', value='100k', footprint=R0603, ref='R8'); V3V3 += R8[1]; R8[2] += STRAP8   # GPIO8 strap pull-up
R9 = Part('Device', 'R', value='100k', footprint=R0603, ref='R9'); V3V3 += R9[1]; R9[2] += BOOT     # GPIO9 strap pull-up

# ----------------------------------------------------------------------------
# IR sender — GPIO2 -> R7 -> Q2 gate (with pulldown); 2 IR LEDs on VSYS via R5/R6
# ----------------------------------------------------------------------------
R7  = Part('Device', 'R', value='100R', footprint=R0603, ref='R7')  # gate series
IR1 += R7[1]; R7[2] += IR_GATE
Rgpd = Part('Device', 'R', value='100k', footprint=R0603, ref='R19')  # gate pulldown
IR_GATE += Rgpd[1]; Rgpd[2] += GND
Q2 = Part('Transistor_FET', 'AO3400A', value='AO3400A', footprint=SOT23, ref='Q2')
Q2['G'] += IR_GATE; Q2['S'] += GND; Q2['D'] += IRD
R5 = Part('Device', 'R', value='18R', footprint=R0603, ref='R5')
R6 = Part('Device', 'R', value='18R', footprint=R0603, ref='R6')
U2 = Part('Device', 'LED', value='IR333C', footprint='LED_THT:LED_D5.0mm', ref='U2')
U3 = Part('Device', 'LED', value='IR333C', footprint='LED_THT:LED_D5.0mm', ref='U3')
VSYS += R5[1]; R5[2] += U2['A']; U2['K'] += IRD
VSYS += R6[1]; R6[2] += U3['A']; U3['K'] += IRD

# ----------------------------------------------------------------------------
# Addressable RGB status LED — GPIO7 -> R1 -> WS2812 DIN  (powered from 3V3)
# ----------------------------------------------------------------------------
R1 = Part('Device', 'R', value='330R', footprint=R0603, ref='R1')
WS_GPIO += R1[1]; R1[2] += WS_DATA
LED1 = Part('LED', 'WS2812', value='WS2812B',
            footprint='LED_SMD:LED_WS2812_PLCC6_5.0x5.0mm_P1.6mm', ref='LED1')
LED1['VDD'] += V3V3
LED1['VCC'] += V3V3
LED1['VSS'] += GND
LED1['DIN'] += WS_DATA
# DOUT (no daisy-chain) and NC left unconnected

# ----------------------------------------------------------------------------
# Telemetry dividers — battery -> GPIO4, VBUS-present -> GPIO5
# ----------------------------------------------------------------------------
R15 = Part('Device', 'R', value='100k', footprint=R0603, ref='R15')
R16 = Part('Device', 'R', value='100k', footprint=R0603, ref='R16')
VBAT += R15[1]; R15[2] += BATT_SENSE; BATT_SENSE += R16[1]; R16[2] += GND
R17 = Part('Device', 'R', value='100k', footprint=R0603, ref='R17')
R18 = Part('Device', 'R', value='150k', footprint=R0603, ref='R18')
V5 += R17[1]; R17[2] += VBUS_SENSE; VBUS_SENSE += R18[1]; R18[2] += GND

# ----------------------------------------------------------------------------
if __name__ == '__main__':
    ERC()
    generate_netlist()
