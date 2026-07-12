#!/bin/bash
# REAL MODEL ROUTE test: are the model's residual (mana-sequencing) ranking failures driven by LABEL
# NOISE? The teacher labels were dumped at K=8 reshuffle-averaging (+-0.5 turn wobble corrupts marginal
# picks). Re-dump the SAME games at K=32 (4x cleaner), retrain identically, and compare on STANDALONE
# land-fold play (MTG_D0_LANDFOLD, no NC rollout = the pure model) -- which is itself label-noise-free,
# so any LP gain is a genuinely better model. Isolates the training-label-quality lever.
set -u
cd /workspaces/MagicDeckTester2
OUT=logs/model_improve; DT=/tmp/dyntrain
DECK=decks/Anti-Lifegain.cod; MT=10
DUMPSEEDS="40001 40002 40003 40004 40005 40006"   # training games (disjoint from held-out play seeds)
GAMES=12
HELD="1111 2222 3333 5555 6666 8888"              # held-out play seeds
g++ -O2 -std=c++17 -o $DT tools/dyntrain/main.cpp 2>/dev/null

dump () {  # $1=K  $2=outfile
  : > "$2"
  for s in $DUMPSEEDS; do
    MTG_DUMP_RSVALUE_ROWS="$OUT/_tmp_dump.rows" MTG_EVAL_ROWS_K="$1" \
      MTG_EVAL_ROWS_ROLLOUT=1 MTG_EVAL_ROWS_HONEST=1 MTG_EVAL_ROLLOUT_DEPTH=2 \
      build/Release/mtg "$DECK" --games "$GAMES" --seed "$s" --depth 0 --max-turns "$MT" --threads 12 >/dev/null 2>&1
    if [ ! -s "$2" ]; then cat "$OUT/_tmp_dump.rows" >> "$2"; else tail -n +2 "$OUT/_tmp_dump.rows" >> "$2"; fi
    rm -f "$OUT/_tmp_dump.rows"
  done
  echo "  K=$1 -> $(($(wc -l < "$2")-1)) rows"
}

playlp () {  # $1=model -> standalone land-fold LP on held-out seeds
  python3 - "$1" <<PY
import os,re,subprocess,sys
model=sys.argv[1]; lps=[]
for s in "$HELD".split():
    env={k:v for k,v in os.environ.items() if not k.startswith("MTG_")}
    env.update({"MTG_D0_LANDFOLD":"1","MTG_D0LF_K":"16","MTG_DYN_MODEL":model})
    out=subprocess.run(["build/Release/mtg","$DECK","--games","40","--seed",s,"--depth","0",
        "--max-turns","$MT","--threads","12"],capture_output=True,text=True,env=env).stdout
    p=int(re.search(r'played\s*:\s*(\d+)',out).group(1));w=int(re.search(r'won\s*:\s*(\d+)',out).group(1))
    a=float(re.search(r'Avg win turn\s*:\s*([\d.]+)',out).group(1)); lps.append((w*a+(p-w)*($MT+1))/p)
print("%.3f"%(sum(lps)/len(lps)))
PY
}

echo "[1] dump K=8 (control)  $(date +%H:%M)"; dump 8  "$OUT/lq_k8.rows"
echo "[2] dump K=32 (treat)   $(date +%H:%M)"; dump 32 "$OUT/lq_k32.rows"
echo "[3] train + recall/regret  $(date +%H:%M)"
for KR in 8 32; do
  $DT "$OUT/lq_k${KR}.rows" --T 0 --H 96 --epochs 80 --lr 2e-3 --out "$OUT/lq_k${KR}_dyn.json" 2>"$OUT/lq_k${KR}_train.log"
  echo "  K=$KR trained: $(grep '^\[test' "$OUT/lq_k${KR}_train.log")"
done
echo "[4] STANDALONE land-fold play LP (held-out, label-noise-free)  $(date +%H:%M)"
echo "  K=8  model standalone LP = $(playlp "$OUT/lq_k8_dyn.json")"
echo "  K=32 model standalone LP = $(playlp "$OUT/lq_k32_dyn.json")"
echo "=== LABEL-QUALITY EXP DONE  $(date +%H:%M) ==="
