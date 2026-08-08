#!/usr/bin/env bash
# Un-condemn tractability-capped cells so an incremental re-run tops them up.
#
# WHY THIS IS NEEDED. A condemned cell is capped at --reference-target, and the scheduler's
# needs() is `games < target(cell)` -- so once a cell is capped AND has reached the cap it is never
# scheduled again, which means the condemnation check never re-runs either. Raising
# --never-condemn-at-or-below on a resume therefore does nothing by itself: the flag has to be
# cleared in the state file first. (New runs are fine -- the flag prevents condemnation up front.)
#
#   bash scripts/valueleaf_uncondemn.sh <cells.json> [max-depth]
set -uo pipefail
CELLS=${1:?path to <matrix>.cells.json}
MAXD=${2:-5}
python3 - "$CELLS" "$MAXD" <<'PY'
import json, sys
p, maxd = sys.argv[1], int(sys.argv[2])
cells = json.load(open(p))
n = 0
for c in cells:
    if c.get("intractable") and c.get("depth", 0) <= maxd:
        c["intractable"] = False
        n += 1
        print("  un-condemned %s%d seed %s (%d games so far)" % (c.get("arm"), c["depth"], c.get("seed"), c.get("games", 0)))
json.dump(cells, open(p, "w"))
print("%d cell(s) un-condemned at depth <= %d -- re-run the matrix with --never-condemn-at-or-below %d" % (n, maxd, maxd))
PY
