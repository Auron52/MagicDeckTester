#!/usr/bin/env bash
# Deterministic COST comparison of a staged value-leaf sidecar against the live one.
#
#   bash test/valueleaf_cost_ab.sh decks/<Deck> [logs/eval/<Stem>.value.STAGED.json]
#
# WHY NOT WALL CLOCK. A regenerated model can be play-neutral and still cost more -- a bigger tree
# ensemble is more work per leaf evaluation -- and that is an adoption-relevant regression. But on a
# heavy-tailed deck wall clock is not evidence: a ladder measurement once reported the same artifact
# as both 0.57x slower and 26x faster, and a pooled batch hands whichever arm got the quieter cores a
# free win. So this measures DETERMINISTIC COUNTERS (search nodes, GameState clones, enumerations)
# from a -DMTG_PROFILE=ON build. Same games, same engine, counters that do not care how busy the box
# was.
#
# WHY NOT --batch, despite the repo's pool-everything rule. `--batch` RETURNS at main.cpp:3623,
# roughly 400 lines before the PROF_REPORT at main.cpp:4007 that lives on the single-run path -- so a
# batch run under an MTG_PROFILE build emits NO counters at all. It does not warn; you get a clean
# run and an empty table, which is how this script's first version reported nothing. Counters are
# also process-global (prof::Report at exit on stderr), so aggregating means summing across
# processes either way. Hence: single-run invocations, chunked so the cores are still used.
#
# (The real fix is a PROF_REPORT before the batch return -- one line, a no-op in any non-instrumented
# build. Worth doing: the repo mandates pooled batches for long runs, so the sanctioned perf
# instrument being unavailable in exactly that mode is a standing trap.)
#
# Requires the instrumented tree, which is deliberately separate from build/ (see CMakeLists):
#   cmake -S . -B build-instr -DMTG_PROFILE=ON -DCMAKE_BUILD_TYPE=Release && cmake --build build-instr -j
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.." || exit 1
. test/lib/harness.sh

DECK_DIR=${1:?usage: valueleaf_cost_ab.sh decks/<Deck> [staged.value.json]}
STEM=$(basename "$DECK_DIR")
KEY=$(echo "$STEM" | tr '[:upper:]' '[:lower:]' | tr -c 'a-z0-9\n' '_')
STAGED=${2:-logs/eval/$STEM.value.STAGED.json}

BIN=${BIN:-build-instr/mtg}
# Fresh again, and disjoint from the scale A/B's 3.0M-6.2M window: a seed reused from the play
# measurement would make the cost read on games the model has effectively been selected on.
SEED0=${SEED0:-7000000}
SEEDS=${SEEDS:-16}
GAMES=${GAMES:-2000}          # per seed per arm; SPACING == GAMES, so bases tile with no replay
OUT=${OUT:-logs/vlq_goblins_cost}

[ -x "$BIN" ] || { echo "no instrumented binary at $BIN -- see the header for the cmake line"; exit 2; }
[ -s "$STAGED" ] || { echo "no staged sidecar at $STAGED"; exit 2; }
deck_file() { ls "$1/$2".cod "$1/$2".txt 2>/dev/null | head -1; }

mkdir -p "$OUT"
log() { echo "[$(date '+%m-%d %H:%M:%S')] $*" | tee -a "$OUT/cost.log"; }

make_variant_deck() {   # dest src-dir stem value.json  (same construction as valueleaf.sh)
    local dest=$1 src=$2 stem=$3 val=$4 f b
    rm -rf "$dest"; mkdir -p "$dest"
    for f in "$src"/*; do
        b=$(basename "$f"); [ "$b" = "$stem.value.json" ] && continue
        ln -sf "$(realpath "$f")" "$dest/$b"
    done
    [ -s "$val" ] && cp "$val" "$dest/$stem.value.json"
    return 0
}

VROOT=$OUT/variants
make_variant_deck "$VROOT/live"   "$DECK_DIR" "$STEM" "$DECK_DIR/$STEM.value.json"
make_variant_deck "$VROOT/staged" "$DECK_DIR" "$STEM" "$STAGED"
cmp -s "$VROOT/live/$STEM.value.json" "$VROOT/staged/$STEM.value.json" && {
    echo "ABORT: live and staged sidecars are IDENTICAL -- there is nothing to measure."; exit 2; }

log "=== $KEY value-leaf COST A/B (deterministic counters, $BIN) ==="
log "  $SEEDS seeds from $SEED0 x $GAMES games = $(( SEEDS * GAMES )) games per arm, sequential arms"

# Both arms' chunks launch together: they are single-threaded processes and the box has cores to
# spare, and since the verdict is a COUNTER ratio rather than a duration, contention cannot bias it
# -- which is the whole reason for preferring counters here.
rm -f "$OUT"/*.counters "$OUT"/*.out
for arm in live staged; do
    for i in $(seq 0 $(( SEEDS - 1 ))); do
        s=$(( SEED0 + i * GAMES ))
        "$BIN" "$(deck_file "$VROOT/$arm" "$STEM")" --profile "$VROOT/$arm/$STEM.profile.json" \
               --seed "$s" --games "$GAMES" \
               > "$OUT/$arm.$s.out" 2> "$OUT/$arm.$s.counters" &
    done
    log "  arm $arm: launched $SEEDS chunks x $GAMES games"
done
wait
for arm in live staged; do cat "$OUT/$arm".*.counters > "$OUT/$arm.counters"; cat "$OUT/$arm".*.out > "$OUT/$arm.log"; done

log "=== counters: live vs staged (same $(( SEEDS * GAMES )) games) ==="
python3 - "$OUT/live.counters" "$OUT/staged.counters" "$OUT/live.log" "$OUT/staged.log" <<'PY'
import re, sys

# prof::Report writes human-readable labels ("GameState deep copies : 12345"), several per line
# ("TT lookups : N   hits: M"). Pull every `label: integer` pair, qualifying the secondary ones with
# the line's primary label so `hits` on the TT line cannot collide with `hits` on another. The
# integer guard rejects the derived floats ("copies / node : 0.53"), which are ratios of counters
# already captured and would otherwise parse as two bogus keys.
# The label class must admit '(' -- without it "Search nodes (steps) : N", the headline counter,
# parses as nothing and the table silently omits the one number the comparison is about. Digits stay
# OUT of the class so a label can never swallow the value it precedes.
PAIR = re.compile(r"([A-Za-z][A-Za-z /()\[\]_-]*?)\s*:\s*(?<![\d.])(\d+)(?![\d.])")

# ACCUMULATE, never assign: the file is every chunk's report concatenated, so each key appears once
# per chunk. Overwriting would silently report the LAST chunk's counters as the whole arm -- a
# plausible-looking table computed from a fraction of the games.
def counters(path):
    out = {}
    for ln in open(path, errors="replace"):
        pairs = PAIR.findall(ln)
        if not pairs:
            continue
        primary = pairs[0][0].strip()
        for i, (label, val) in enumerate(pairs):
            key = primary if i == 0 else f"{primary}/{label.strip()}"
            out[key] = out.get(key, 0) + int(val)
    return out

# Single-run mode prints "Games played  : N" and "avg (turns)   : X" on separate lines, once per
# chunk; weight each chunk's avg by its own game count.
def avg(path):
    tot = n = 0.0
    games = None
    for ln in open(path, errors="replace"):
        m = re.search(r"Games played\s*:\s*(\d+)", ln)
        if m:
            games = int(m.group(1)); continue
        m = re.search(r"avg \(turns\)\s*:\s*([\d.]+)", ln)
        if m and games:
            tot += games * float(m.group(1)); n += games; games = None
    return (tot / n if n else float("nan")), int(n)

a, b = counters(sys.argv[1]), counters(sys.argv[2])
aa, an = avg(sys.argv[3]); ba, bn = avg(sys.argv[4])
print(f"play: live avg={aa:.5f} ({an} games)   staged avg={ba:.5f} ({bn} games)   delta={ba-aa:+.5f}")
keys = [k for k in a if k in b and (a[k] or b[k])]
if not keys:
    print("NO COUNTERS PARSED -- is this an MTG_PROFILE build? (stderr shown below)")
    print(open(sys.argv[1], errors="replace").read()[:1500]); sys.exit(1)
print(f"\n{'counter':<24} {'live':>16} {'staged':>16} {'ratio':>9}")
for k in sorted(keys, key=lambda k: -max(a[k], b[k])):
    r = (b[k] / a[k]) if a[k] else float("inf")
    print(f"{k:<24} {a[k]:>16,} {b[k]:>16,} {r:>8.3f}x")
print("\nratio > 1 = the staged model does MORE work for the same games.")
PY
log "=== COMPLETE -- nothing adopted ==="
