#!/usr/bin/env bash
# Autoroute the board with Freerouting (the standard KiCad autoroute path —
# KiCad has no built-in headless autorouter). Net classes from place_and_outline.py
# (Power 0.5mm, IR 0.4mm) are carried into the DSN so power routes wide.
#
#   .kicad_pcb --pcbnew ExportSpecctraDSN--> .dsn --freerouting--> .ses --pcbnew ImportSpecctraSES--> routed .kicad_pcb
#
# Needs: java + freerouting.jar (https://github.com/freerouting/freerouting/releases).
# Set FREEROUTING_JAR or drop freerouting.jar next to this script.
set -euo pipefail
cd "$(dirname "$0")"
JAR="${FREEROUTING_JAR:-./freerouting.jar}"
BRD=espir_slave_pcb.kicad_pcb
WORK=$(mktemp -d)

[ -f "$JAR" ] || { echo "freerouting.jar not found (set FREEROUTING_JAR)"; exit 1; }

echo ">> export Specctra DSN"
python - "$BRD" "$WORK/board.dsn" <<'PY'
import sys, pcbnew
b = pcbnew.LoadBoard(sys.argv[1]); pcbnew.ExportSpecctraDSN(b, sys.argv[2])
PY

echo ">> Freerouting autoroute (headless)"
java -Djava.awt.headless=true -jar "$JAR" -de "$WORK/board.dsn" -do "$WORK/board.ses" -mp 30 \
  2>&1 | grep -iE 'session completed' || true

echo ">> import Specctra SES back into the board"
python - "$BRD" "$WORK/board.ses" <<'PY'
import sys, pcbnew
b = pcbnew.LoadBoard(sys.argv[1]); pcbnew.ImportSpecctraSES(b, sys.argv[2])
pcbnew.SaveBoard(sys.argv[1], b)
print("imported; tracks:", len([t for t in b.GetTracks()]))
PY

echo ">> DRC"
kicad-cli pcb drc --exit-code-violations "$BRD" -o /tmp/route_drc.rpt 2>/dev/null || true
grep -E 'Found .* violations|Found .* unconnected' /tmp/route_drc.rpt || true
rm -rf "$WORK"
echo ">> done. Remaining ratsnest + edge/keepout cleanup is the GUI finishing pass."
