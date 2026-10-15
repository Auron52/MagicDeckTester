#!/usr/bin/env bash
# ONE hold of the box lock, three binaries on the SAME manifest (all 33 suite decks, shipped b20 + d3 b10,
# every lever at its shipped default, fresh seeds 10700001+):
#   r2 vs r3 = the MTG_BP_WAVE_NEW_LAST deletion  -> must be byte-identical
#   r3 vs r4 = the executor lockstep fix (pod twin bp_all + chain, site-9 twin chain) -> the movers
# Usage: lock3.sh build | run | compare <tagA> <tagB>
set -u
W=/workspaces/MagicDeckTester/.claude/worktrees/agent-a9469f45bff622cfb
B=/workspaces/MagicDeckTester/build/scratch/bp_open
D=$B/data
OUT=$B/lock3
case "$1" in
build)
  export BP_DATA_ROOT=$D
  mkdir -p "$OUT"
  python3 "$W/scripts/bp_open/make_ab.py" build --out "$OUT/raw" --seed0 10700001 --cells ship,d3 \
    --games ship=40,d3=40 --arms ship --chunk 20
  cp -f "$OUT/raw/manifest.json" "$OUT/manifest.json"
  ;;
run)
  cd "$D"
  for T in r2 r3 r4; do
    echo "$T start $(date -u +%FT%TZ)" >> "$OUT/run.info"
    env MTG_DUMP_WINS=1 "$B/bin/$T/mtg" --batch "$OUT/manifest.json" --threads 20 --cards-json "$D/src/cards/data/cards.json" \
      > "$OUT/$T.out" 2> "$OUT/$T.err"
    echo "$T exit $? $(date -u +%FT%TZ)" >> "$OUT/run.info"
  done
  ;;
compare)
  python3 - "$OUT" "$2" "$3" <<'EOF'
import re, sys
from collections import defaultdict
out, ta, tb = sys.argv[1:4]
jp = re.compile(r"^(\S+): played=(\d+) avg=(\S+) digest=(\S+) ms=\d+ units=(\d+)")
wp = re.compile(r"\[win\] job=(\S+) gi=(\d+) wt=(-?\d+)")
def load(t):
    jobs, wins = {}, {}
    for l in open("%s/%s.out" % (out, t)):
        m = jp.match(l.strip())
        if m: jobs[m.group(1)] = (m.group(4), int(m.group(5)))
    for l in open("%s/%s.err" % (out, t), errors="ignore"):
        m = wp.search(l)
        if m:
            w = int(m.group(3)); wins[(m.group(1), int(m.group(2)))] = 9 if w < 0 else w
    return jobs, wins
ja, wa = load(ta); jb, wb = load(tb)
common = sorted(set(ja) & set(jb))
dig = [k for k in common if ja[k][0] != jb[k][0]]
uni = [k for k in common if ja[k][1] != jb[k][1]]
print("%s vs %s: jobs %d/%d common; play digests differ in %d, units differ in %d" % (ta, tb, len(common), len(ja), len(dig), len(uni)))
net = defaultdict(int); moved = defaultdict(list)
for (job, gi), w in wa.items():
    if (job, gi) in wb and wb[(job, gi)] != w:
        deck = job.split("_ship_")[0].split("_d3_")[0]
        net[deck] += wb[(job, gi)] - w
        moved[deck].append("%s gi%d %d->%d" % (job.split(".")[0], gi, w, wb[(job, gi)]))
for k in dig: print("  DIGEST", k)
for d in sorted(moved): print("  %-16s net %+d  %s" % (d, net[d], "; ".join(moved[d])))
EOF
  ;;
esac
