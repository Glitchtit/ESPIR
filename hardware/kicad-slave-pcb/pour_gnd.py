#!/usr/bin/env python3
"""
4-layer GND pours + via stitching. Pours a GND zone on every copper layer
(F / In1 / In2 / B) so the inner layers act as ground reference planes and all
GND pads/fragments tie together, then stitches the layers with a collision-checked
GND via grid.

Two modes:
  python pour_gnd.py                # full: pour + fill + stitch (run AFTER routing)
  python pour_gnd.py planes-only    # pour + fill only, NO stitching (run BEFORE routing)

Run `planes-only` BEFORE route.sh so the filled GND zones export to Freerouting as
`(plane GND ...)` — Freerouting then treats GND as already-connected and routes only
the signal nets (no redundant GND traces). Then run the full mode after routing to
re-fill around the new tracks and add stitching. See build.sh.
"""
import sys
import pcbnew

BRD = "espir_slave_pcb.kicad_pcb"
MM = pcbnew.FromMM
TOMM = pcbnew.ToMM


def main(planes_only=False):
    b = pcbnew.LoadBoard(BRD)
    gnd = b.FindNet("GND").GetNetCode()
    e = b.GetBoardEdgesBoundingBox()
    inset = MM(0.3)
    x1, y1 = e.GetLeft() + inset, e.GetTop() + inset
    x2, y2 = e.GetRight() - inset, e.GetBottom() - inset
    corners = [(x1, y1), (x2, y1), (x2, y2), (x1, y2)]

    # IMPORTANT: do all READS before any WRITES. A write (SetLocalZoneConnection)
    # during pad iteration corrupts SWIG state so a later b.GetTracks() returns a raw
    # SwigPyObject. So: (1) read track bboxes, (2) read pad bboxes, (3) then write.
    obst = []                                   # stitching obstacles: non-GND tracks + ALL vias
    for t in b.GetTracks():                     # (a new stitch via near any existing via = hole_to_hole)
        if t.Type() == pcbnew.PCB_VIA_T or t.GetNetCode() != gnd:
            bb = t.GetBoundingBox()
            obst.append((bb.GetLeft(), bb.GetTop(), bb.GetRight(), bb.GetBottom()))
    pads = []                                   # all pad bboxes; remember GND pads
    gnd_pads = []
    for fp in b.GetFootprints():
        for p in fp.Pads():
            bb = p.GetBoundingBox()
            pads.append((bb.GetLeft(), bb.GetTop(), bb.GetRight(), bb.GetBottom(), p.GetNetCode()))
            if p.GetNetCode() == gnd:
                gnd_pads.append(p)
    for p in gnd_pads:                          # solid GND-pad connection (no starved thermal)
        p.SetLocalZoneConnection(pcbnew.ZONE_CONNECTION_FULL)

    # remove any pre-existing zones (idempotent re-runs)
    for z in list(b.Zones()):
        b.Remove(z)

    layers = [pcbnew.F_Cu, pcbnew.In1_Cu, pcbnew.In2_Cu, pcbnew.B_Cu]
    for ly in layers:
        z = pcbnew.ZONE(b)
        z.SetLayer(ly)
        z.SetNetCode(gnd)
        z.SetAssignedPriority(0)
        z.SetPadConnection(pcbnew.ZONE_CONNECTION_THERMAL)
        # robust outline: SHAPE_LINE_CHAIN + AddPolygon (Outline().NewOutline() can
        # return a raw SwigPyObject after heavy pad iteration earlier in the script)
        chain = pcbnew.SHAPE_LINE_CHAIN()
        for x, y in corners:
            chain.Append(int(x), int(y))
        chain.SetClosed(True)
        z.AddPolygon(chain)
        b.Add(z)
    pcbnew.ZONE_FILLER(b).Fill(b.Zones())
    print(f"poured GND on {len(layers)} layers")

    if planes_only:                             # planes for the DSN; stitch after routing
        pcbnew.SaveBoard(BRD, b)
        print("planes-only: skipped stitching (run full pour_gnd after routing)")
        return

    # --- collision-checked GND via stitching grid (pads/obst cached above) ----
    via_r = MM(0.4)            # via OD 0.8 -> radius 0.4
    clr = MM(0.3)
    def clear(x, y):
        for L, T, R, B, nc in pads:
            if L - via_r - clr < x < R + via_r + clr and T - via_r - clr < y < B + via_r + clr:
                if nc != gnd:
                    return False
        for L, T, R, B in obst:
            if L - via_r - clr < x < R + via_r + clr and T - via_r - clr < y < B + via_r + clr:
                return False
        return True
    step = MM(6.0)
    nv = 0
    x = int(x1 + step)
    while x < x2 - step:
        y = int(y1 + step)
        while y < y2 - step:
            if clear(x, y):
                v = pcbnew.PCB_VIA(b)
                v.SetPosition(pcbnew.VECTOR2I(int(x), int(y)))
                v.SetDrill(MM(0.4)); v.SetWidth(MM(0.8)); v.SetNetCode(gnd)
                v.SetViaType(pcbnew.VIATYPE_THROUGH)
                b.Add(v); nv += 1
            y += int(step)
        x += int(step)
    print(f"stitched {nv} GND vias (6mm grid, collision-checked)")
    pcbnew.ZONE_FILLER(b).Fill(b.Zones())
    pcbnew.SaveBoard(BRD, b)


if __name__ == "__main__":
    main(planes_only=any(a in ("planes-only", "zones-only") for a in sys.argv[1:]))
