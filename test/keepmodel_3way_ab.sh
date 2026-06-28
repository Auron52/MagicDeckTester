#!/bin/bash
# 3-way A/B: committed vs gini-keepmodel vs regret-keepmodel, turn-regret (loss=9) at given depths.
# Usage: DECK=decks/X.txt COMMITTED=... GINI=... REGRET=... SEEDS="4004 5005 6006 7007" DEPTHS="0 3" GAMES=800 bash test/keepmodel_3way_ab.sh
set -uo pipefail
cd "$(dirname "$0")/.."
DECK=${DECK:?}; COMMITTED=${COMMITTED:?}; GINI=${GINI:?}; REGRET=${REGRET:?}
SEEDS=${SEEDS:-"4004 5005 6006 7007"}; DEPTHS=${DEPTHS:-"0 3"}; GAMES=${GAMES:-800}; MAXT=${MAXT:-8}
BIN=./build/Release/mtg
OUT=logs/keepmodel_overnight/regret_compare/$(basename "$DECK" | sed 's/\..*//')
mkdir -p "$OUT"
run(){ # $1=label $2=prof $3=depth $4=seed
  local bud=20; [ "$3" = 0 ] && bud=0
  MTG_DUMP_WINS=1 "$BIN" "$DECK" --profile "$2" --seed "$4" --games "$GAMES" --depth "$3" \
    --budget-ms $bud --max-turns "$MAXT" --lookahead-bottoming --threads 0 2>&1 1>/dev/null \
    | grep -oE 'gi=[0-9]+ wt=-?[0-9]+' | sed -E 's/gi=([0-9]+) wt=(-?[0-9]+)/\1 \2/' | sort -n > "$OUT/${1}_d${3}_s${4}.wins"
}
for d in $DEPTHS; do for s in $SEEDS; do
  run committed "$COMMITTED" "$d" "$s"; run gini "$GINI" "$d" "$s"; run regret "$REGRET" "$d" "$s"
done; done
python3 - "$OUT" "$GAMES" $((MAXT+1)) "$DEPTHS" "$SEEDS" <<'PY'
import sys,os
out,games,loss=sys.argv[1],int(sys.argv[2]),int(sys.argv[3])
depths=sys.argv[4].split(); seeds=sys.argv[5].split()
def load(fn):
    m={}
    if os.path.exists(fn):
        for ln in open(fn):
            a=ln.split()
            if len(a)>=2: m[int(a[0])]=int(a[1])
    return m
def reg(m,gis): return sum((m[g] if m[g]>0 else loss) for g in gis)/len(gis)
def won(m,gis): return sum(1 for g in gis if m[g]>0)
print(f"== 3-way turn-regret (loss={loss}; lower=better)  [{os.path.basename(out)}] ==")
for d in depths:
    tc=tg=tr=n=0; wl_g=wl_r=0
    for s in seeds:
        c=load(f"{out}/committed_d{d}_s{s}.wins"); g=load(f"{out}/gini_d{d}_s{s}.wins"); r=load(f"{out}/regret_d{d}_s{s}.wins")
        gis=sorted(set(c)&set(g)&set(r))
        if not gis: continue
        rc,rg,rr=reg(c,gis),reg(g,gis),reg(r,gis)
        tc+=rc*len(gis); tg+=rg*len(gis); tr+=rr*len(gis); n+=len(gis)
        wl_g+=sum(1 for x in gis if c[x]>0 and g[x]<0); wl_r+=sum(1 for x in gis if c[x]>0 and r[x]<0)
        print(f"  d{d} s{s}: committed {rc:.4f} | gini {rg:.4f} ({rg-rc:+.4f}) | regret {rr:.4f} ({rr-rc:+.4f}) | won {won(c,gis)}/{won(g,gis)}/{won(r,gis)}")
    if n: print(f"  d{d} ALL: committed {tc/n:.4f} | gini {tg/n:.4f} ({(tg-tc)/n:+.4f}) | regret {tr/n:.4f} ({(tr-tc)/n:+.4f})  [W->L gini={wl_g} regret={wl_r}; n={n}]")
PY
