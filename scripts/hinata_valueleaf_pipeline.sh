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

# ---- STAGE 1: CHUNKED, SELF-RESUMING ROW DUMP ------------------------------------------------
# Rows are appended as they are produced, so an interruption never loses ROWS -- but it does lose
# the RESUME POINT, and that is the dangerous half. Per-game seed is `base_seed + gi` with gi
# RESTARTING AT 0 every invocation (GoldFishRunner: SetupGame(deck, base_seed+gi); --game-index only
# shifts the opponent-spawn pattern), so relaunching without advancing the seed REPLAYS the same
# games and silently duplicates positions into the training set. Deriving the offset by hand from
# the game seeds in the rows is exactly the step that should not exist.
#
# So: run in CHUNKS of games, one file per chunk, written to <chunk>.part and renamed to <chunk>.rows
# only on a clean exit. Resume = "start after the highest COMPLETE chunk"; any .part left by a kill
# is discarded and that chunk is simply redone. Zero loss, zero duplication, no bookkeeping to get
# wrong -- the same shape as the matrix's <out>.cells.json and the mulligan generator's chunks.
# Safe to Ctrl-C or kill at any moment; just re-run the same command.
#
# Usage: dumpB [target_rows] [games_per_chunk]   (chunk defaults: B 100 games ~42 min,
# A 50 games ~48 min -- that is the MOST an interruption can cost).
#   dumpB : K=3 volume run   ~823 rows/hr on 24 threads -> ~11k (the knee) in ~12 h
#   dumpA : K=8 fidelity run ~352 rows/hr on 24 threads -> ~12.5k in ~34 h
run_chunked_dump() {
  local arm=$1 K=$2 base=$3 start_gi=$4 chunk=$5 target=${6:-11000}
  local dir=logs/eval/rows_$arm
  mkdir -p "$dir"
  rm -f "$dir"/*.part                       # discard the chunk an interruption left half-written
  while :; do
    # resume point = end of the highest COMPLETE chunk (filenames are zero-padded start indices)
    local gi=$start_gi
    for f in "$dir"/chunk_*.rows; do
      [ -e "$f" ] || continue
      local b=${f##*/chunk_}; b=${b%.rows}
      local endgi=$(( 10#${b%%_*} + 10#${b##*_} ))
      [ "$endgi" -gt "$gi" ] && gi=$endgi
    done
    # bash has no "$FOO_$bar" indirection -- resolve the legacy filename via ${!name}
    local lvar="LEGACY_ROWS_$arm"; local legacy="${!lvar:-}"
    # cat first so grep sees ONE stream (grep -c over several files prints one count PER FILE)
    local have=$( { cat "$dir"/chunk_*.rows "$legacy" 2>/dev/null || true; } | grep -vc '^#' )
    have=${have:-0}
    echo "[$(date +%H:%M:%S)] arm=$arm rows=$have next_gi=$gi (target $target)"
    [ "$have" -ge "$target" ] && { echo "target reached"; break; }
    local tag=$(printf "%07d_%05d" "$gi" "$chunk")
    MTG_DUMP_VALUE_ROWS="$dir/chunk_$tag.part" MTG_EVAL_ROWS_K=$K \
      ./build/Release/mtg "$DECK" --profile "$PROF" \
      --seed $(( base + gi )) --game-index "$gi" --games "$chunk" --threads 24 > /dev/null 2>&1
    if [ $? -eq 0 ] && [ -s "$dir/chunk_$tag.part" ]; then
      mv "$dir/chunk_$tag.part" "$dir/chunk_$tag.rows"      # commit: this chunk is now durable
    else
      echo "chunk $tag did not complete cleanly -- discarding and stopping"; rm -f "$dir/chunk_$tag.part"; break
    fi
  done
}
# Legacy (pre-chunking) row files -- folded into the totals and into training.
LEGACY_ROWS_B=logs/eval/hinata_value_v2b.rows
LEGACY_ROWS_A=logs/eval/hinata_value_v2.rows

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
  SRC=${2:-logs/eval/hinata_rows_B.all.rows}   # produced by `collect B`
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



dumpB) run_chunked_dump B 3 20020 207 "${3:-100}" "${2:-11000}" ;;
dumpA) run_chunked_dump A 8  8008 140 "${3:-50}"  "${2:-11000}" ;;

# Concatenate legacy + every complete chunk into one training file (repeated '#' headers are
# harmless -- read_rows just re-reads the same feature names).
collect)
  arm=${2:-B}; out=logs/eval/hinata_rows_$arm.all.rows
  eval "legacy=\$LEGACY_ROWS_$arm"
  cat $(ls logs/eval/rows_$arm/chunk_*.rows 2>/dev/null) "$legacy" 2>/dev/null > "$out"
  echo "$out: $(grep -vc '^#' "$out") rows from $(ls logs/eval/rows_$arm/chunk_*.rows 2>/dev/null | wc -l) chunk(s) + legacy"
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
