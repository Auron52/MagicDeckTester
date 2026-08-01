#!/usr/bin/env bash
# Hinata value-leaf regeneration driver (2026-07-31).
#
# WHY: Hinata's shipped value leaf (value_play in decks/Hinata2/Hinata2.value.json -- target_depth 5,
# budget_ms 20, heavy, beam W=3/leafdepth=2) is LIVE in every game, but its supporting numbers are
# stale in three separate ways:
#   1. its own note says "PROVISIONAL d5 default; revise after mulligan profile + depth table";
#   2. value_leaf_table covers hdepths [1,2,3] only (H4/H5 never measured) from 200 games x 2 seeds;
#   3. DECISIVE -- scripts/attic/valueleaf_depth_matrix.py never passed --profile, so every cell was
#      measured on a Hinata with NO mulligan/keep model and no card_scores. Fixed 2026-07-31.
# The antilife precedent (docs/design/antilife-valueleaf-deep-cells-overnight.md) shows how badly a
# table built on a different engine state misleads, so the whole thing is rebuilt on HEAD.
#
# Stage 1 (rows) is the long pole and is INTRINSICALLY slow on this deck: the label is K clairvoyant
# EnumerateEarliestWins searches per position, and Hinata mid-game boards make that explode. Measured
# sustained rates on a 24-core box:
#     K=8, default max-turns, 14 threads  ->  ~360 rows/hour
#     K=3, --max-turns 8,     10 threads  ->  ~450 rows/hour
# Both start fast (turn-1/2 positions are cheap) and collapse once games reach mid-game, so judge a
# dump by its SUSTAINED rate, never its first minute.
#
#   bash scripts/hinata_valueleaf_pipeline.sh train    # sort + train from the K=3 rows, STAGED
#   bash scripts/hinata_valueleaf_pipeline.sh matrix   # incremental H1-5 x V1-5 + deep cells
#   bash scripts/hinata_valueleaf_pipeline.sh status
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."

ROWS_B=logs/eval/hinata_value_v2b.rows          # K=3, --max-turns 8  (the volume run)
ROWS_A=logs/eval/hinata_value_v2.rows           # K=8, default horizon (the fidelity run)
STAGED=logs/eval/Hinata2.value.STAGED.json
DECK=decks/Hinata2/Hinata2.cod
PROF=decks/Hinata2/Hinata2.profile.json
LIVE=decks/Hinata2/Hinata2.value.json

case "${1:-status}" in

status)
  echo "rows  A(K=8): $(wc -l < "$ROWS_A" 2>/dev/null || echo 0)"
  echo "rows  B(K=3): $(wc -l < "$ROWS_B" 2>/dev/null || echo 0)"
  echo "mtg procs   : $(pgrep -c mtg 2>/dev/null || echo 0)"
  [ -f logs/eval/valueleaf_depth_hinata_v2.txt ] && cat logs/eval/valueleaf_depth_hinata_v2.txt
  ;;

# Sort the rows before training: the dump is MULTI-THREADED, so row order varies run to run and an
# unsorted file makes training irreproducible. Header line is preserved at the top.
train)
  SRC=${2:-$ROWS_B}
  ( head -1 "$SRC"; tail -n +2 "$SRC" | sort ) > "${SRC%.rows}.sorted.rows"
  N=$(( $(wc -l < "${SRC%.rows}.sorted.rows") - 1 ))
  echo "training on $N rows from ${SRC%.rows}.sorted.rows"
  python3 scripts/attic/train_eval_gbdt.py --rows "${SRC%.rows}.sorted.rows" \
      --out "$STAGED" --regression --trees 120 --depth 4 --lr 0.15 --min-leaf 20
  # Merge ONLY eval_model into a copy of the live sidecar, so value_play / the old table / the
  # crossover survive until the new table is measured and the adoption A/B has run.
  python3 - "$LIVE" "$STAGED" <<'PY'
import json, sys
live, staged = sys.argv[1], sys.argv[2]
L = json.load(open(live)); S = json.load(open(staged))
L["eval_model"] = S["eval_model"]
L.setdefault("provenance", {})["eval_model_note"] = (
    "retrained 2026-07-31 on value rows dumped at SHIPPED config (value_play d5/budget-20) WITH "
    "the deck profile, i.e. with the exhaustive keep/mulligan model live -- the previous model "
    "predates that profile. value_leaf_table/crossover here are still the OLD (no-profile) ones "
    "until the regenerated matrix lands.")
json.dump(L, open(staged, "w"), indent=1)
print("staged merged model ->", staged)
PY
  echo "NOT installed to $LIVE -- install only after the matrix + adoption A/B."
  ;;

# Incremental batched matrix: every cell in 25-game batches, round-robin, written as each lands and
# resumable via <out>.cells.json. Run this with the box to ITSELF -- the intractability cut-off is
# wall-clock based, so a loaded box misclassifies slow cells as intractable.
matrix)
  VALUE_JSON=${2:-$LIVE}
  python3 scripts/attic/valueleaf_depth_matrix.py --incremental --decks hinata \
    --hdepths 1 2 3 4 5 --vdepths 1 2 3 4 5 --seeds 8008 9009 10010 11011 \
    --target 400 --reference-target 50 --batch 25 --workers 20 \
    --value-min-depth 0 --intractable-sec-per-game 3.0 \
    --out logs/eval/valueleaf_depth_hinata_v2.txt
  ;;

# The "other cells", partially filled -- same incremental machinery, deeper depths, small target.
deep)
  python3 scripts/attic/valueleaf_depth_matrix.py --incremental --decks hinata \
    --hdepths 6 7 8 --vdepths 6 7 8 --seeds 8008 9009 10010 11011 \
    --target 100 --reference-target 25 --batch 25 --workers 20 \
    --value-min-depth 0 --intractable-sec-per-game 3.0 \
    --out logs/eval/valueleaf_depth_hinata_v2_deep.txt
  ;;

# ---- STAGE 1 RESUME ------------------------------------------------------------------------
# The dumps APPEND, and per-game seed is base_seed+gi with gi restarting at 0 EVERY invocation
# (GoldFishRunner: `SetupGame(deck, base_seed + gi)`; --game-index only shifts the opponent-spawn
# pattern). So a naive relaunch REPLAYS the same games and duplicates positions into the training
# set. Resume by advancing BOTH --seed and --game-index past the highest game index already dumped
# (a gap is harmless, an overlap is not). Offsets below were derived from the game seeds carried in
# the existing rows + a full thread-set of slack.
#   dumpB : the K=3 volume run  -- ~823 rows/hr on 24 threads, ~12 h to the ~11k knee
#   dumpA : the K=8 fidelity run -- ~352 rows/hr on 24 threads, ~34 h to ~12.5k
dumpB)
  MTG_DUMP_VALUE_ROWS=logs/eval/hinata_value_v2b.rows MTG_EVAL_ROWS_K=3 \
    ./build/Release/mtg "$DECK" --profile "$PROF" \
    --games 4000 --seed 20227 --game-index 207 --threads 24
  ;;

dumpA)
  MTG_DUMP_VALUE_ROWS=logs/eval/hinata_value_v2.rows MTG_EVAL_ROWS_K=8 \
    ./build/Release/mtg "$DECK" --profile "$PROF" \
    --games 4000 --seed 8148 --game-index 140 --threads 24
  ;;

# Head-to-head on ONE common test set -- the only sound way to compare models trained on different
# K (the trainer's own held-out RMSE scores each against its OWN noisier/cleaner labels).
compare)
  python3 - <<'PYEOF'
import random, subprocess, statistics as st
def load(p):
    L=[l.rstrip('\n') for l in open(p) if l.strip()]
    return [l for l in L if l.startswith('#')][0], [l for l in L if not l.startswith('#')]
hA,A=load('logs/eval/hinata_value_v2.rows'); hB,B=load('logs/eval/hinata_value_v2b.rows')
gid=lambda r:(r.split()[-2], r.split()[-1])
gids=sorted({gid(r) for r in A}); res={'A':[],'BN':[],'BC':[]}
for rep in range(6):
    rnd=random.Random(100+rep); g=list(gids); rnd.shuffle(g); held=set(g[:len(g)//4])
    te=[r for r in A if gid(r) in held]; tr=[r for r in A if gid(r) not in held]
    Bs=list(B); rnd.shuffle(Bs); N=len(tr)
    files={'A':tr,'BN':Bs[:N],'BC':Bs[:min(len(Bs),int(N*2.27))]}
    open('/tmp/cmp_te.rows','w').write(hA+"\n"+"\n".join(te)+"\n")
    for k,rows in files.items():
        open(f'/tmp/cmp_{k}.rows','w').write((hA if k=='A' else hB)+"\n"+"\n".join(rows)+"\n")
        subprocess.run(['python3','scripts/attic/train_eval_gbdt.py','--rows',f'/tmp/cmp_{k}.rows',
                        '--out',f'/tmp/cmp_{k}.json','--regression','--trees','60','--depth','4',
                        '--lr','0.15','--min-leaf','20'],capture_output=True)
        out=subprocess.run(['python3','scripts/attic/valueleaf_score_rows.py','--model',f'/tmp/cmp_{k}.json',
                            '--rows','/tmp/cmp_te.rows','--label',k],capture_output=True,text=True).stdout
        res[k].append(float(out.split('RMSE=')[1].split()[0]))
for k,lbl in (('A','A  K=8   (equal rows)'),('BN','B  K=3   (equal rows)'),('BC','B  K=3   (equal cost)')):
    print(f"{lbl:24s} mean RMSE={st.mean(res[k]):.4f}  sd={st.pstdev(res[k]):.4f}")
for k,lbl in (('BN','equal ROWS   B-A'),('BC','equal COST   B-A')):
    d=[b-a for a,b in zip(res['A'],res[k])]; m=st.mean(d); se=st.pstdev(d)/len(d)**.5
    print(f"paired {lbl}: {m:+.4f}  se={se:.4f}  t={m/se:+.2f}  ({sum(1 for x in d if x>0)}/6 favour A)")
PYEOF
  ;;

*) echo "usage: $0 {status|dumpB|dumpA|compare|train [rows]|matrix [value.json]|deep}"; exit 2 ;;
esac
