#!/usr/bin/env bash
# Reproduce the ESPIR slave PCB in KiCad from the circuit-as-code source.
#
#   1. espir_slave_pcb.py   --(SKiDL)-->     espir_slave_pcb.net   (netlist + ERC)
#   2. espir_slave_pcb.net  --(kinet2pcb)--> espir_slave_pcb.kicad_pcb (footprints+nets)
#   3. place_and_outline.py --(pcbnew)-->    functional placement + Edge.Cuts outline
#
# Requirements (Arch):
#   sudo pacman -S --needed kicad kicad-library
#   python -m venv --system-site-packages .venv && . .venv/bin/activate
#   pip install skidl kinet2pcb
set -euo pipefail
cd "$(dirname "$0")"

SYM=/usr/share/kicad/symbols
FP=/usr/share/kicad/footprints
export KICAD9_SYMBOL_DIR=$SYM KICAD8_SYMBOL_DIR=$SYM KICAD7_SYMBOL_DIR=$SYM \
       KICAD6_SYMBOL_DIR=$SYM KICAD_SYMBOL_DIR=$SYM \
       KICAD9_FOOTPRINT_DIR=$FP KICAD10_FOOTPRINT_DIR=$FP

echo ">> 1. SKiDL: generate netlist + run ERC"
python espir_slave_pcb.py

echo ">> 1b. make_schematic: netlist -> .kicad_sch (connect-by-name labels)"
python make_schematic.py
kicad-cli sch export netlist -o /tmp/espir_sch_check.net espir_slave_pcb.kicad_sch >/dev/null 2>&1 \
  && echo "   schematic loads + netlists OK" || echo "   WARNING: schematic failed to load"

echo ">> 2. kinet2pcb: netlist -> .kicad_pcb"
kinet2pcb -i espir_slave_pcb.net -o espir_slave_pcb.kicad_pcb -w --nobackup \
          --libraries "$FP"/*.pretty

echo ">> 3. pcbnew: functional placement + board outline"
python place_and_outline.py

echo ">> 4. DRC (expect: unconnected ratsnest = unrouted; no connectivity errors)"
kicad-cli pcb drc --exit-code-violations espir_slave_pcb.kicad_pcb \
          -o /tmp/espir_slave_drc.rpt || true
grep -E 'Found .* violations|Found .* unconnected' /tmp/espir_slave_drc.rpt || true
echo ">> done. Open espir_slave_pcb.kicad_pro in KiCad to route/pour."
