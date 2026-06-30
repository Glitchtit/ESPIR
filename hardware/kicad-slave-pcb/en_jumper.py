#!/usr/bin/env python3
"""
Tie the AP2112K LDO's EN pin to VIN (both VSYS). EN(3) and VIN(1) are the outer
pins of the left SOT-23-5 column with GND(2) between them; Freerouting can't tie
them. Jumper on B.Cu (via-in-pad at each, short track underneath the GND pin) —
safe because nothing of another net runs under those pads on the inner/bottom
layers (verify with DRC). Run after routing, before the GND pour fill (so the
pour clears around the new vias/track). Idempotent.
"""
import pcbnew

BRD = "espir_slave_pcb.kicad_pcb"
MM = pcbnew.FromMM


def main():
    b = pcbnew.LoadBoard(BRD)
    u6 = next(f for f in b.GetFootprints() if f.GetReference() == "U6")
    vin = u6.FindPadByNumber("1").GetPosition()
    en = u6.FindPadByNumber("3").GetPosition()
    code = u6.FindPadByNumber("1").GetNetCode()         # VSYS

    # remove any prior jumper (idempotent)
    for t in list(b.GetTracks()):
        if t.GetNetCode() != code:
            continue
        if t.Type() == pcbnew.PCB_VIA_T and t.GetPosition() in (vin, en):
            b.Remove(t)
        elif t.Type() == pcbnew.PCB_TRACE_T and t.GetLayer() == pcbnew.B_Cu \
                and {t.GetStart(), t.GetEnd()} == {vin, en}:
            b.Remove(t)

    for pos in (vin, en):
        v = pcbnew.PCB_VIA(b)
        v.SetPosition(pos); v.SetDrill(MM(0.3)); v.SetWidth(MM(0.6))
        v.SetNetCode(code); v.SetViaType(pcbnew.VIATYPE_THROUGH); b.Add(v)
    t = pcbnew.PCB_TRACK(b)
    t.SetStart(vin); t.SetEnd(en); t.SetWidth(MM(0.4)); t.SetLayer(pcbnew.B_Cu)
    t.SetNetCode(code); b.Add(t)
    pcbnew.SaveBoard(BRD, b)
    print("EN->VIN B.Cu jumper added")


if __name__ == "__main__":
    main()
