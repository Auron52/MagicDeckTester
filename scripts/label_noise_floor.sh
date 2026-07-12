#!/bin/bash
# Measure the IRREDUCIBLE label-noise floor of pick-regret. The teacher label is a K=8 reshuffle-average
# win-turn; sampling noise means the "teacher-best" plan is partly noise-determined. Dump the SAME games
# TWICE with independent reshuffle streams (MTG_LABEL_SALT 1 vs 2), then: for each decision, take dump-A's
# argmin-label plan and grade it under dump-B's labels -> the win-turn regret the TEACHER pays against its
# OWN independent relabeling = the floor below which NO model can rank better. If the model's pick-regret
# (~0.10-0.17) is near this floor, the model is already near-optimal and the "gap" is largely label noise.
set -u
cd /workspaces/MagicDeckTester2
OUT=logs/model_improve
dump () {  # deck mt salt outfile
  : > "$4"
  for s in $(seq 40001 40020); do
    MTG_DUMP_RSVALUE_ROWS="$OUT/_ln.rows" MTG_EVAL_ROWS_K=8 MTG_LABEL_SALT="$3" \
      MTG_EVAL_ROWS_ROLLOUT=1 MTG_EVAL_ROWS_HONEST=1 MTG_EVAL_ROLLOUT_DEPTH=2 \
      build/Release/mtg "$1" --games 6 --seed "$s" --depth 0 --max-turns "$2" --threads 12 >/dev/null 2>&1
    if [ ! -s "$4" ]; then cat "$OUT/_ln.rows" >> "$4"; else tail -n +2 "$OUT/_ln.rows" >> "$4"; fi; rm -f "$OUT/_ln.rows"
  done
}
analyze () {  # deck-name A B mt
python3 - "$1" "$2" "$3" "$4" <<'PY'
import sys
nm,fa,fb,mt=sys.argv[1],sys.argv[2],sys.argv[3],int(sys.argv[4])
def load(f):
    rows=[]; hdr=None
    for ln in open(f):
        t=ln.split()
        if ln[0]=='#': hdr=t; continue
        lab=float(t[0]); seed=int(t[-2]); turn=int(t[-1]); rows.append((seed,turn,lab))
    return rows
A,B=load(fa),load(fb)
# group by (seed,turn) preserving candidate order (enumeration deterministic -> A[i] matches B[i])
from collections import defaultdict, OrderedDict
def group(R):
    g=OrderedDict()
    for seed,turn,lab in R: g.setdefault((seed,turn),[]).append(lab)
    return g
GA,GB=group(A),group(B)
agree=n=0; cross_reg=0.0; noise=0.0; nc=0; selfreg=0.0
for k in GA:
    if k not in GB or len(GA[k])!=len(GB[k]) or len(GA[k])<2: continue
    a,b=GA[k],GB[k]; n+=1
    ia=min(range(len(a)),key=lambda i:a[i]); ib=min(range(len(b)),key=lambda i:b[i])
    if ia==ib: agree+=1
    # take A's pick, grade under B: regret vs B's own best
    cross_reg += b[ia]-b[ib]
    # A's pick graded under A (0 by construction) vs the floor; also symmetric self-consistency
    selfreg += (a[ib]-a[ia])   # B's pick graded under A
    for i in range(len(a)): noise += abs(a[i]-b[i]); nc+=1
print(f"[{nm}] decisions={n}  argmin-agree={100.0*agree/max(1,n):.0f}%  "
      f"NOISE-FLOOR pick-regret={cross_reg/max(1,n):.4f} (symmetric {selfreg/max(1,n):.4f})  "
      f"mean|labelA-labelB|/cand={noise/max(1,nc):.3f}")
PY
}
echo "START $(date +%H:%M)"
for d in "antilife decks/Anti-Lifegain.cod 10" "TH decks/treasure_hunt.txt 8"; do
  set -- $d; NM=$1; DECK=$2; MT=$3
  echo "== dumping $NM twice (salt 1, salt 2) $(date +%H:%M) =="
  dump "$DECK" "$MT" 1 "$OUT/ln_${NM}_A.rows"
  dump "$DECK" "$MT" 2 "$OUT/ln_${NM}_B.rows"
  analyze "$NM" "$OUT/ln_${NM}_A.rows" "$OUT/ln_${NM}_B.rows" "$MT"
done
echo "=== LABEL-NOISE-FLOOR DONE $(date +%H:%M) ==="
