#!/usr/bin/env python3
"""
Swap one tactile button (THT -> SMD KMR2) on the routed board, preserving its
position/ref/value and net connectivity: assign the new pads' nets, BRIDGE the
signal track that ended at the old pad to the new pad, and drop the dangling GND
stubs (the GND pour reconnects the new GND pads). Run once per button in its OWN
process (repeated FootprintLoad + heavy ops corrupt pcbnew's SWIG state).

  python swap_smd_button.py SW1 EN
  python swap_smd_button.py SW2 BOOT
Then re-pour (pour_gnd.py).
"""
import sys, pcbnew

BRD = "espir_slave_pcb.kicad_pcb"
MM = pcbnew.FromMM
LIB, NAME = "/usr/share/kicad/footprints/Button_Switch_SMD.pretty", "SW_Push_1P1T_NO_CK_KMR2"


def main(ref, sig):
    b = pcbnew.LoadBoard(BRD)
    old = next(f for f in b.GetFootprints() if f.GetReference() == ref)
    xs = [p.GetPosition().x for p in old.Pads()]
    ys = [p.GetPosition().y for p in old.Pads()]
    center = pcbnew.VECTOR2I((min(xs) + max(xs)) // 2, (min(ys) + max(ys)) // 2)
    val = old.GetValue()
    nets = {p.GetNumber(): p.GetNetCode() for p in old.Pads()}
    old_sig = [p.GetPosition() for p in old.Pads() if p.GetNetname() == sig]
    old_gnd = [p.GetPosition() for p in old.Pads() if p.GetNetname() == "GND"]

    nf = pcbnew.FootprintLoad(LIB, NAME)
    nf.SetPosition(center); nf.SetOrientationDegrees(0); nf.SetReference(ref); nf.SetValue(val)
    sig_code = nets["1"]
    b.Remove(old); b.Add(nf)
    new_sig = []
    for pad in nf.Pads():                       # assign nets AFTER Add (Add clears them otherwise)
        pad.SetNetCode(nets.get(pad.GetNumber(), 0))
        if pad.GetNumber() == "1":
            new_sig.append(pad.GetPosition())

    def close(pt, lst, tol=0.5):
        return any(abs(pt.x - q.x) < MM(tol) and abs(pt.y - q.y) < MM(tol) for q in lst)

    # drop dangling GND stubs at the old GND pads (pour reconnects new GND pads)
    for tr in list(b.GetTracks()):
        if tr.Type() == pcbnew.PCB_TRACE_T and tr.GetNetname() == "GND" \
                and (close(tr.GetStart(), old_gnd) or close(tr.GetEnd(), old_gnd)):
            b.Remove(tr)
    # bridge ONCE per old signal-pad position that has a signal track ending at it,
    # to the nearest new signal pad (the doubled pad -> 2 short aligned bridges, no cross).
    bridged = 0
    for opos in old_sig:
        has_track = any(
            tr.Type() == pcbnew.PCB_TRACE_T and tr.GetNetname() == sig
            and (close(tr.GetStart(), [opos]) or close(tr.GetEnd(), [opos]))
            for tr in b.GetTracks())
        if not has_track:
            continue
        tgt = min(new_sig, key=lambda q: (q.x - opos.x) ** 2 + (q.y - opos.y) ** 2)
        # the old THT pad was a through-hole, so signal tracks on inner/bottom layers
        # connected there too — add a via at the old position to keep them on-net.
        v = pcbnew.PCB_VIA(b)
        v.SetPosition(opos); v.SetDrill(MM(0.3)); v.SetWidth(MM(0.6))
        v.SetNetCode(sig_code); v.SetViaType(pcbnew.VIATYPE_THROUGH); b.Add(v)
        br = pcbnew.PCB_TRACK(b)
        br.SetStart(opos); br.SetEnd(tgt); br.SetWidth(MM(0.25))
        br.SetLayer(pcbnew.F_Cu); br.SetNetCode(sig_code); b.Add(br)
        bridged += 1
    pcbnew.SaveBoard(BRD, b)
    print(f"{ref} -> {NAME}: {bridged} {sig} bridge(s), GND stubs dropped, saved")


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
