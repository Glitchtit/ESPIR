#!/usr/bin/env python3
"""
Expand the board on X (symmetric, stays centred), round all four corners, and add
four M3 mounting holes in the new left/right margins (clear of the packed
components). Run on the routed board; re-pour GND afterwards (pour_gnd.py). The
routing is untouched — only the outline + holes change.
"""
import pcbnew, math

BRD = "espir_slave_pcb.kicad_pcb"
MM = pcbnew.FromMM
TOMM = pcbnew.ToMM
R = 3.0                      # corner radius (mm)
HOLE_FP = ("/usr/share/kicad/footprints/MountingHole.pretty", "MountingHole_3.2mm_M3")


def main():
    b = pcbnew.LoadBoard(BRD)
    e = b.GetBoardEdgesBoundingBox()
    cx = (TOMM(e.GetLeft()) + TOMM(e.GetRight())) / 2          # keep centred
    # new envelope: X expanded so each side has a ~6mm clear strip for an M3 hole
    x1, x2 = cx - 33.0, cx + 33.0                              # 66mm wide (was 54)
    y1, y2 = TOMM(e.GetTop()), TOMM(e.GetBottom())             # Y unchanged

    # --- replace Edge.Cuts with a rounded rectangle -----------------------
    for d in list(b.GetDrawings()):
        if d.GetLayer() == pcbnew.Edge_Cuts:
            b.Remove(d)
    def V(x, y): return pcbnew.VECTOR2I(MM(x), MM(y))
    def seg(a, c):
        s = pcbnew.PCB_SHAPE(b); s.SetShape(pcbnew.SHAPE_T_SEGMENT); s.SetLayer(pcbnew.Edge_Cuts)
        s.SetStart(a); s.SetEnd(c); s.SetWidth(MM(0.15)); b.Add(s)
    def arc(start, mid, end):
        s = pcbnew.PCB_SHAPE(b); s.SetShape(pcbnew.SHAPE_T_ARC); s.SetLayer(pcbnew.Edge_Cuts)
        s.SetArcGeometry(start, mid, end); s.SetWidth(MM(0.15)); b.Add(s)
    k = R * 0.70710678
    seg(V(x1 + R, y1), V(x2 - R, y1))        # top
    seg(V(x2, y1 + R), V(x2, y2 - R))        # right
    seg(V(x2 - R, y2), V(x1 + R, y2))        # bottom
    seg(V(x1, y2 - R), V(x1, y1 + R))        # left
    arc(V(x1, y1 + R), V(x1 + R - k, y1 + R - k), V(x1 + R, y1))        # TL
    arc(V(x2 - R, y1), V(x2 - R + k, y1 + R - k), V(x2, y1 + R))        # TR
    arc(V(x2, y2 - R), V(x2 - R + k, y2 - R + k), V(x2 - R, y2))        # BR
    arc(V(x1 + R, y2), V(x1 + R - k, y2 - R + k), V(x1, y2 - R))        # BL

    # --- four M3 mounting holes in the left/right margins -----------------
    # An Edge.Cuts circle is a milled board cutout = a non-plated M3 mounting hole.
    hx_l, hx_r = x1 + 3.0, x2 - 3.0
    hy_t, hy_b = y1 + 4.0, y2 - 4.0
    HR = 1.6                                  # M3 clearance hole = 3.2mm dia
    for x, y in [(hx_l, hy_t), (hx_l, hy_b), (hx_r, hy_t), (hx_r, hy_b)]:
        c = pcbnew.PCB_SHAPE(b); c.SetShape(pcbnew.SHAPE_T_CIRCLE); c.SetLayer(pcbnew.Edge_Cuts)
        c.SetCenter(V(x, y)); c.SetEnd(V(x + HR, y)); c.SetWidth(MM(0.15)); b.Add(c)

    pcbnew.SaveBoard(BRD, b)
    print(f"board X expanded to [{x1:.1f},{x2:.1f}] ({x2-x1:.0f}mm), r={R}mm corners, 4x M3 holes")


if __name__ == "__main__":
    main()
