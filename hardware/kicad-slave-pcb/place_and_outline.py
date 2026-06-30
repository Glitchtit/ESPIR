#!/usr/bin/env python3
"""
Functional placement + board outline for the ESPIR slave PCB (KiCad / pcbnew).

kinet2pcb drops the footprints in a mechanical grid. This pass re-places them by
FUNCTION (the EasyEDA skill's master placement rule: group each part next to what
it wires to, fixed/edge parts first, signal flow across the board) and draws the
Edge.Cuts outline. Routing + GND pours are deliberately left as the human
finishing pass (same division of labour the skill describes).

Run:  python place_and_outline.py
"""
import pcbnew, json, os

BRD = "espir_slave_pcb.kicad_pcb"
PRO = "espir_slave_pcb.kicad_pro"
MM = pcbnew.FromMM

# Net classes so the autorouter + DRC widen the current-carrying nets (the skill's
# critical-signal widths; mirrors espir_slave_pcb.kicad_dru). kinet2pcb regenerates
# the .kicad_pro, so (re)apply these here in the post-kinet2pcb pcbnew pass.
NET_CLASSES = {
    "Power": {"track_width": 0.5, "via_diameter": 0.8, "via_drill": 0.4,
              "nets": ["GND", "V3V3", "V5", "VSYS", "VBAT", "VCELL"]},
    "IR":    {"track_width": 0.4, "nets": ["IRD"]},
}

def set_net_classes():
    if not os.path.exists(PRO):
        print("no .kicad_pro yet; skipping net classes"); return
    d = json.load(open(PRO))
    ns = d.setdefault("net_settings", {})
    classes = ns.setdefault("classes", [])
    default = next((c for c in classes if c.get("name") == "Default"), {})
    patterns = []
    classes = [c for c in classes if c.get("name") not in NET_CLASSES]   # rebuild ours
    for name, spec in NET_CLASSES.items():
        c = dict(default); c.update(name=name, track_width=spec["track_width"])
        for k in ("via_diameter", "via_drill"):
            if k in spec: c[k] = spec[k]
        c.pop("priority", None)
        classes.append(c)
        patterns += [{"netclass": name, "pattern": n} for n in spec["nets"]]
    ns["classes"] = classes
    ns["netclass_patterns"] = patterns
    json.dump(d, open(PRO, "w"), indent=2)
    print(f"net classes: {', '.join(NET_CLASSES)} + {len(patterns)} net assignments")

# Board envelope (mm). Origin top-left; +x right, +y down (KiCad screen coords).
# Width 54 (not 52) leaves a right margin so SW1/SW2 button pads clear the edge
# (copper_edge_clearance) without colliding with the strap resistors at x40.
BW, BH = 54.0, 47.0
# Offset so the board sits centred on the A4 (297x210mm) drawing sheet, not in the
# top-left corner. PLACE coords below stay board-local (0..BW, 0..BH); OX/OY shift them.
OX, OY = (297.0 - BW) / 2, (210.0 - BH) / 2   # = 122.5, 81.5

# Functional placement table:  ref -> (x_mm, y_mm, rotation_deg)
# The ESP32-C6-MINI-1 sits antenna-UP at the top edge: its built-in antenna
# keep-out (a large footprint courtyard) overhangs the top board edge off-board
# (standard module placement). Everything else is grouped by sub-circuit in the
# lower two thirds, with generous spacing so no part bodies collide. Routing,
# GND pours and final nudging are the human finishing pass.
PLACE = {
    # --- ESP32-C6-MINI-1 module: body at the TOP EDGE so the antenna + its
    #     `tracks not_allowed` keep-out (y -5.6..-26 from origin) hangs OFF the
    #     top edge (y<0). Origin y=6 → body y[1,11] on-board, keep-out off-board,
    #     so the top-corner parts (U2/U3/LED1/R1) are clear of it and routable.
    "U5":   (26.0, 6.0, 0),

    # --- IR sender (top-left corner: 5 mm IR LEDs fire up/off the board) -----
    "U2":   (6.0, 7.0, 0),       # IR LED 1
    "U3":   (6.0, 15.0, 0),      # IR LED 2
    "R5":   (12.5, 7.0, 0),      # ballast 1
    "R6":   (12.5, 15.0, 0),     # ballast 2
    "Q2":   (12.5, 22.0, 0),     # low-side N-FET (IR cathodes)
    "R7":   (8.0, 22.0, 0),      # gate series
    "R19":  (8.0, 25.0, 0),      # gate pulldown

    # --- Addressable status LED (top-right corner) --------------------------
    "LED1": (46.0, 8.0, 0),      # WS2812 status LED
    "R1":   (46.0, 13.0, 90),    # data series

    # --- Battery input + disconnect (left edge) -----------------------------
    "U7":   (5.0, 33.0, 0),      # LiPo JST at left edge
    "SW3":  (5.0, 41.0, 0),      # slide switch in the cell line
    "C13":  (12.0, 33.0, 90),    # VBAT bulk

    # --- Power tree: load-share + OR-ing + LDO (centre band) ----------------
    "Q4":   (15.0, 33.0, 0),     # load-share P-FET
    "D1":   (15.0, 37.0, 0),     # OR-ing Schottky (moved left, off the USB-C CC pads)
    "U6":   (27.0, 37.0, 0),     # AP2112K LDO
    "C14":  (11.0, 37.0, 90),    # VSYS bulk
    "C15":  (31.0, 33.0, 90),    # V3V3 bulk

    # --- USB-C + charger (bottom-centre, USB-C mouth off the bottom edge) ----
    "USBC1": (21.0, 44.0, 0),    # USB-C at bottom edge (port — belongs at the edge)
    # CC pulldowns directly above the USB-C CC pads (A5/B5 at local x19.75/22.75, y40)
    # so the CC1/CC2 nets route with a short vertical stub (else A5 is boxed in).
    "R11":  (19.75, 37.0, 90),   # CC1 pulldown above A5
    "R12":  (22.75, 37.0, 90),   # CC2 pulldown above B5
    "U8":   (32.0, 40.0, 0),     # MCP73831 charger (right of U6, clear of bottom edge)
    "R13":  (36.5, 40.0, 0),     # PROG
    "LED2": (32.0, 43.5, 0),     # charge status LED
    "R14":  (36.5, 43.5, 0),

    # --- 3V3 decoupling at the module power pin -----------------------------
    "C1":   (34.0, 33.0, 90),
    "C2":   (36.0, 33.0, 90),
    "C12":  (38.0, 33.0, 90),    # V5 bulk

    # --- Reset / boot / straps (right edge) ---------------------------------
    "R10":  (40.0, 18.0, 0),     # EN pull-up
    "C11":  (40.0, 21.0, 0),     # EN cap
    "SW1":  (45.0, 22.0, 0),     # RESET button, right edge
    "R8":   (40.0, 27.0, 0),     # GPIO8 strap pull-up
    "R9":   (40.0, 30.0, 0),     # GPIO9 strap pull-up
    "SW2":  (45.0, 32.0, 0),     # BOOT button, right edge

    # --- Telemetry dividers (lower-right, near the module sense pins) --------
    "R15":  (40.0, 40.0, 0),     # battery divider
    "R16":  (40.0, 43.0, 0),
    "R17":  (46.0, 40.0, 0),     # VBUS divider
    "R18":  (46.0, 43.0, 0),
}


def main():
    board = pcbnew.LoadBoard(BRD)
    board.SetCopperLayerCount(4)   # 4-layer: F / In1 / In2 / B (inner layers get GND pours)

    placed, missing = 0, []
    by_ref = {fp.GetReference(): fp for fp in board.GetFootprints()}
    for ref, (x, y, rot) in PLACE.items():
        fp = by_ref.get(ref)
        if fp is None:
            missing.append(ref)
            continue
        fp.SetPosition(pcbnew.VECTOR2I(MM(x + OX), MM(y + OY)))
        fp.SetOrientationDegrees(rot)
        placed += 1

    # Any footprint not in the table (shouldn't happen) -> park it below the board
    parked = [r for r in by_ref if r not in PLACE]
    for i, r in enumerate(parked):
        by_ref[r].SetPosition(pcbnew.VECTOR2I(MM(2 + 4 * i + OX), MM(BH + 6 + OY)))

    # Edge-margin enforcement: keep non-port / non-antenna parts (passives, discretes,
    # ICs) at least MARGIN from every board edge, so only the connectors/antenna/switches
    # sit at the edge. Pushes an offender inward by just enough to clear.
    EXEMPT = {"USBC1", "U7", "U5", "SW1", "SW2", "SW3"}   # USB-C, JST, module/antenna, switches
    MARGIN = MM(1.0)
    ex1, ey1, ex2, ey2 = MM(OX), MM(OY), MM(OX + BW), MM(OY + BH)
    for fp in board.GetFootprints():
        if fp.GetReference() in EXEMPT:
            continue
        cy = fp.GetCourtyard(pcbnew.F_CrtYd)
        bb = cy.BBox() if not cy.IsEmpty() else fp.GetBoundingBox(False, False)
        dx = dy = 0
        if bb.GetLeft() < ex1 + MARGIN:
            dx = (ex1 + MARGIN) - bb.GetLeft()
        elif bb.GetRight() > ex2 - MARGIN:
            dx = (ex2 - MARGIN) - bb.GetRight()
        if bb.GetTop() < ey1 + MARGIN:
            dy = (ey1 + MARGIN) - bb.GetTop()
        elif bb.GetBottom() > ey2 - MARGIN:
            dy = (ey2 - MARGIN) - bb.GetBottom()
        if dx or dy:
            p = fp.GetPosition()
            fp.SetPosition(pcbnew.VECTOR2I(p.x + dx, p.y + dy))
            print(f"  edge-margin: nudged {fp.GetReference()} by ({MM(dx) if False else round(dx/1e6,2)},{round(dy/1e6,2)})mm")

    # --- Edge.Cuts rectangle outline ---------------------------------------
    # Clear any existing edge segments first.
    for d in list(board.GetDrawings()):
        if d.GetLayer() == pcbnew.Edge_Cuts:
            board.Remove(d)
    corners = [(0, 0), (BW, 0), (BW, BH), (0, BH), (0, 0)]
    for (x1, y1), (x2, y2) in zip(corners, corners[1:]):
        seg = pcbnew.PCB_SHAPE(board)
        seg.SetShape(pcbnew.SHAPE_T_SEGMENT)
        seg.SetLayer(pcbnew.Edge_Cuts)
        seg.SetStart(pcbnew.VECTOR2I(MM(x1 + OX), MM(y1 + OY)))
        seg.SetEnd(pcbnew.VECTOR2I(MM(x2 + OX), MM(y2 + OY)))
        seg.SetWidth(MM(0.15))
        board.Add(seg)

    pcbnew.SaveBoard(BRD, board)
    print(f"placed={placed}  missing={missing}  parked={parked}")
    print(f"outline: {BW} x {BH} mm rectangle on Edge.Cuts")
    set_net_classes()


if __name__ == "__main__":
    main()
