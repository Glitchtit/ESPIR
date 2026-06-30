#!/usr/bin/env python3
"""
Generate a KiCad schematic (.kicad_sch) from the SKiDL-generated netlist.

SKiDL's own generate_schematic() hangs, and KiCad has no netlist->schematic
importer, so this builds the .kicad_sch directly: it places every component
(real library symbols, embedded in lib_symbols) and expresses connectivity the
connect-by-name way — a net LABEL on each pin (same net name = one net, no wires
to route). The netlist (espir_slave_pcb.net) is the source of truth, so the
schematic is guaranteed to match it; verify by re-netlisting the .kicad_sch.

Run:  python make_schematic.py   ->  espir_slave_pcb.kicad_sch
"""
import re, uuid, math, os

NET = "espir_slave_pcb.net"
OUT = "espir_slave_pcb.kicad_sch"
SYMDIR = "/usr/share/kicad/symbols"

# ---------------------------------------------------------------------------
# Minimal S-expression parser
# ---------------------------------------------------------------------------
def parse_sexp(s):
    toks = re.findall(r'\(|\)|"(?:[^"\\]|\\.)*"|[^\s()]+', s)
    pos = 0
    def rd():
        nonlocal pos
        t = toks[pos]; pos += 1
        if t == '(':
            lst = []
            while toks[pos] != ')':
                lst.append(rd())
            pos += 1
            return lst
        if t.startswith('"'):
            return t[1:-1].replace('\\"', '"')
        return t
    return rd()

def find_all(node, head):
    """All sub-lists whose first element == head (recursive)."""
    out = []
    if isinstance(node, list):
        if node and node[0] == head:
            out.append(node)
        for x in node:
            out.extend(find_all(x, head))
    return out

def first(node, head):
    for x in node:
        if isinstance(x, list) and x and x[0] == head:
            return x
    return None

# ---------------------------------------------------------------------------
# Read the netlist: components + pin->net map
# ---------------------------------------------------------------------------
net_sexp = parse_sexp(open(NET).read())
comps = {}                      # ref -> {value, footprint, lib, part}
for c in find_all(net_sexp, 'comp'):
    ref = first(c, 'ref')[1]
    val = (first(c, 'value') or ['value', ''])[1]
    fp  = (first(c, 'footprint') or ['footprint', ''])[1]
    ls  = first(c, 'libsource')
    lib = first(ls, 'lib')[1]; part = first(ls, 'part')[1]
    comps[ref] = dict(value=val, footprint=fp, lib=lib, part=part)

pin_net = {}                    # (ref, pin) -> net name
for n in find_all(net_sexp, 'net'):
    name = first(n, 'name')[1]
    for nd in find_all(n, 'node'):
        pin_net[(first(nd, 'ref')[1], first(nd, 'pin')[1])] = name

# ---------------------------------------------------------------------------
# Read library symbols: raw block (for lib_symbols) + pin geometry
# ---------------------------------------------------------------------------
def extract_symbol_block(lib, part):
    txt = open(os.path.join(SYMDIR, lib + ".kicad_sym")).read()
    i = txt.index('(symbol "%s"' % part)
    depth = 0
    for k in range(i, len(txt)):
        if txt[k] == '(': depth += 1
        elif txt[k] == ')':
            depth -= 1
            if depth == 0:
                return txt[i:k+1]
    raise ValueError("unterminated symbol %s:%s" % (lib, part))

def base_block_and_name(lib, part):
    """Follow (extends ...) to the ancestor that actually defines the pins/graphics.
    Returns (raw_block_text, ancestor_part_name). extends keeps the same pinout."""
    block = extract_symbol_block(lib, part)
    sx = parse_sexp(block)
    ext = first(sx, 'extends')
    if ext:
        return base_block_and_name(lib, ext[1])
    return block, part

def flattened_block(lib, part):
    """A self-contained lib_symbols entry for lib:part, with extends resolved.
    KiCad's embedded format names the TOP symbol "Lib:Part" but the child UNIT
    symbols keep the bare part name "Part_0_1"/"Part_1_1" (NO library prefix).
    The v10 schematic format (version 20260306) takes the v10 .kicad_sym property
    tokens verbatim, so no token stripping is needed."""
    base, base_name = base_block_and_name(lib, part)
    # child units: "<base>_N_M" -> "<part>_N_M"  (do first; bare, no lib prefix)
    flat = re.sub(r'\(symbol "%s(_\d+_\d+)"' % re.escape(base_name),
                  r'(symbol "%s\1"' % part, base)
    # top symbol: "<base>" -> "Lib:Part"  (only the exact top id)
    flat = re.sub(r'\(symbol "%s"' % re.escape(base_name),
                  '(symbol "%s:%s"' % (lib, part), flat, count=1)
    return flat, base

def pins_of(block, lib, part):
    """list of (number, x, y) connection points in symbol-local coords."""
    sx = parse_sexp(block)
    pins = []
    for p in find_all(sx, 'pin'):
        at = first(p, 'at'); num = first(p, 'number')
        if at and num:
            pins.append((num[1], float(at[1]), float(at[2])))
    return pins

libkey = {}                     # (lib,part) -> "lib:part"
lib_blocks = {}                 # "lib:part" -> embedded block text
pin_geo = {}                    # (lib,part) -> {num:(x,y)}
for ref, c in comps.items():
    key = (c['lib'], c['part'])
    if key in pin_geo:
        continue
    full = "%s:%s" % key
    flat, base = flattened_block(*key)        # extends resolved -> self-contained
    lib_blocks[full] = flat
    libkey[key] = full
    pin_geo[key] = {num: (x, y) for num, x, y in pins_of(base, *key)}

# ---------------------------------------------------------------------------
# Lay out components, grouped roughly like the PCB, on a generous grid
# ---------------------------------------------------------------------------
ORDER = (  # functional grouping, just for readability
    ["U5"] +
    ["U2","U3","R5","R6","Q2","R7","R19"] +
    ["LED1","R1"] +
    ["U7","SW3","C13"] +
    ["Q4","D1","U6","C14","C15"] +
    ["USBC1","R11","R12","U8","R13","LED2","R14"] +
    ["C1","C2","C12"] +
    ["R10","C11","SW1","R8","R9","SW2"] +
    ["R15","R16","R17","R18"]
)
ordered = ORDER + [r for r in comps if r not in ORDER]

COLS = 6
PITCH_X, PITCH_Y = 63.5, 63.5   # mm, large enough that pins of neighbours never coincide
X0, Y0 = 50.0, 50.0
pos = {}
for idx, ref in enumerate(ordered):
    col, row = idx % COLS, idx // COLS
    pos[ref] = (X0 + col * PITCH_X, Y0 + row * PITCH_Y)

def uid():
    return str(uuid.uuid4())

SHEET_UUID = uid()

# ---------------------------------------------------------------------------
# Emit the .kicad_sch
# ---------------------------------------------------------------------------
def label(net, x, y):
    return ('\t(label "%s"\n\t\t(at %.4f %.4f 0)\n\t\t(effects\n\t\t\t(font\n'
            '\t\t\t\t(size 1.27 1.27)\n\t\t\t)\n\t\t\t(justify left bottom)\n\t\t)\n'
            '\t\t(uuid "%s")\n\t)\n') % (net, x, y, uid())

def instance(ref, c):
    sx, sy = pos[ref]
    key = (c['lib'], c['part']); lid = libkey[key]
    out = []
    out.append('\t(symbol\n\t\t(lib_id "%s")\n\t\t(at %.4f %.4f 0)\n\t\t(unit 1)\n'
               '\t\t(exclude_from_sim no)\n\t\t(in_bom yes)\n\t\t(on_board yes)\n\t\t(dnp no)\n'
               '\t\t(uuid "%s")\n' % (lid, sx, sy, uid()))
    out.append('\t\t(property "Reference" "%s"\n\t\t\t(at %.4f %.4f 0)\n\t\t\t(effects\n'
               '\t\t\t\t(font\n\t\t\t\t\t(size 1.27 1.27)\n\t\t\t\t)\n\t\t\t)\n\t\t)\n'
               % (ref, sx, sy - 2.0))
    out.append('\t\t(property "Value" "%s"\n\t\t\t(at %.4f %.4f 0)\n\t\t\t(effects\n'
               '\t\t\t\t(font\n\t\t\t\t\t(size 1.27 1.27)\n\t\t\t\t)\n\t\t\t)\n\t\t)\n'
               % (c['value'], sx, sy + 2.0))
    out.append('\t\t(property "Footprint" "%s"\n\t\t\t(at %.4f %.4f 0)\n\t\t\t(effects\n'
               '\t\t\t\t(font\n\t\t\t\t\t(size 1.0 1.0)\n\t\t\t\t)\n\t\t\t\t(hide yes)\n\t\t\t)\n\t\t)\n'
               % (c['footprint'], sx, sy))
    # pin uuids
    for num in pin_geo[key]:
        out.append('\t\t(pin "%s"\n\t\t\t(uuid "%s")\n\t\t)\n' % (num, uid()))
    out.append('\t\t(instances\n\t\t\t(project "espir_slave_pcb"\n\t\t\t\t(path "/%s"\n'
               '\t\t\t\t\t(reference "%s")\n\t\t\t\t\t(unit 1)\n\t\t\t\t)\n\t\t\t)\n\t\t)\n'
               % (SHEET_UUID, ref))
    out.append('\t)\n')
    return ''.join(out)

parts = []
parts.append('(kicad_sch\n\t(version 20260306)\n\t(generator "make_schematic.py")\n'
             '\t(generator_version "10.0")\n\t(uuid "%s")\n\t(paper "A1")\n' % SHEET_UUID)
# lib_symbols
parts.append('\t(lib_symbols\n')
for lid, blk in lib_blocks.items():
    parts.append('\t\t' + blk.replace('\n', '\n\t\t') + '\n')
parts.append('\t)\n')
# component instances + their pin labels
for ref in ordered:
    c = comps[ref]
    parts.append(instance(ref, c))
    sx, sy = pos[ref]
    for num, (px, py) in pin_geo[(c['lib'], c['part'])].items():
        net = pin_net.get((ref, num))
        if net is None:
            continue                      # intentionally-unconnected pin: no label
        parts.append(label(net, sx + px, sy - py))   # rot-0 transform: x+px, y-py
parts.append('\t(sheet_instances\n\t\t(path "/"\n\t\t\t(page "1")\n\t\t)\n\t)\n'
             '\t(embedded_fonts no)\n)\n')

open(OUT, 'w').write(''.join(parts))
print("wrote %s: %d components, %d nets, %d unique symbols"
      % (OUT, len(comps), len({v for v in pin_net.values()}), len(lib_blocks)))
