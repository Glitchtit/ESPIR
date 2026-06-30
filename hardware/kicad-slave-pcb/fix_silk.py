#!/usr/bin/env python3
"""
Tidy silkscreen designators after layout: a collision-aware placer that moves any
ref flagged by DRC (silk_over_copper / silk_overlap) to the nearest position clear
of all pads, part bodies and other refs. Refs with no clear spot in a saturated
cluster are hidden (still in BOM/CPL). Edge-overhang body silk (antenna/connector)
is left — fab clips it. Run after routing/pours; re-run is idempotent.
"""
import json, re, math, subprocess, pcbnew

BRD = "espir_slave_pcb.kicad_pcb"
MM, TOMM = pcbnew.FromMM, pcbnew.ToMM


def boxes(b):
    pads, bodies = [], []
    for f in b.GetFootprints():
        for p in f.Pads():
            bb = p.GetBoundingBox()
            pads.append((TOMM(bb.GetLeft()), TOMM(bb.GetTop()), TOMM(bb.GetRight()), TOMM(bb.GetBottom())))
        bb = f.GetBoundingBox(False, False)
        bodies.append((TOMM(bb.GetLeft()), TOMM(bb.GetTop()), TOMM(bb.GetRight()), TOMM(bb.GetBottom())))
    return pads, bodies


def refbb(f):
    bb = f.Reference().GetBoundingBox()
    return [TOMM(bb.GetLeft()), TOMM(bb.GetTop()), TOMM(bb.GetRight()), TOMM(bb.GetBottom())]


def ov(a, c, m=0.15):
    return not (a[2] + m < c[0] or c[2] + m < a[0] or a[3] + m < c[1] or c[3] + m < a[1])


def flagged_refs():
    subprocess.run(["kicad-cli", "pcb", "drc", "--format", "json", "-o", "/tmp/_silk.json", BRD],
                   capture_output=True)
    d = json.load(open("/tmp/_silk.json"))
    refs = set()
    for x in d["violations"]:
        if x["type"] in ("silk_over_copper", "silk_overlap"):
            for i in x["items"]:
                m = re.search(r"Reference field of (\w+)", i["description"])
                if m:
                    refs.add(m.group(1))
    return refs


def main():
    b = pcbnew.LoadBoard(BRD)
    fps = {f.GetReference(): f for f in b.GetFootprints()}
    pads, bodies = boxes(b)
    e = b.GetBoardEdgesBoundingBox()
    EX1, EY1, EX2, EY2 = TOMM(e.GetLeft()), TOMM(e.GetTop()), TOMM(e.GetRight()), TOMM(e.GetBottom())
    otherrefs = {r: refbb(fps[r]) for r in fps}

    for _ in range(4):
        flagged = flagged_refs()
        if not flagged:
            break
        moved = 0
        for ref in flagged:
            f = fps[ref]
            c = f.GetPosition(); cx, cy = TOMM(c.x), TOMM(c.y)
            rb = refbb(f); w, h = rb[2] - rb[0], rb[3] - rb[1]
            best = None
            for dist in (1.6, 2.2, 2.8, 3.4, 4.0, 4.8, 5.6):
                for k in range(16):
                    a = k * math.pi / 8
                    nx, ny = cx + dist * math.cos(a), cy + dist * math.sin(a)
                    cand = [nx - w / 2, ny - h / 2, nx + w / 2, ny + h / 2]
                    if cand[0] < EX1 + 0.3 or cand[2] > EX2 - 0.3 or cand[1] < EY1 + 0.3 or cand[3] > EY2 - 0.3:
                        continue
                    if any(ov(cand, p) for p in pads) or any(ov(cand, bd) for bd in bodies):
                        continue
                    if any(ov(cand, otherrefs[o]) for o in otherrefs if o != ref):
                        continue
                    best = (nx, ny); break
                if best:
                    break
            if best:
                f.Reference().SetPosition(pcbnew.VECTOR2I(MM(best[0]), MM(best[1])))
                otherrefs[ref] = [best[0] - w / 2, best[1] - h / 2, best[0] + w / 2, best[1] + h / 2]
                moved += 1
        pcbnew.SaveBoard(BRD, b)
        if moved == 0:
            break

    # hide any designators that still overlap (saturated cluster) — keep in BOM/CPL
    stuck = flagged_refs()
    for ref in stuck:
        fps[ref].Reference().SetVisible(False)
    if stuck:
        pcbnew.SaveBoard(BRD, b)
    print("placed clear; hid (saturated):", sorted(stuck))


if __name__ == "__main__":
    main()
