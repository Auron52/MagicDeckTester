#!/usr/bin/env bash
# IS THE CONFOUNDED BOTTOMING GATE FAIR? -- a property test of the MEASURING INSTRUMENT.
#
# MTG_CONFOUND_BOTTOM is the adoption gate for every deck's bottoming table (mulligan-profile.md,
# exhaustive-keep-policy.md 214, scripts/mullgen.sh). It is supposed to make the clairvoyant lookahead
# bottomer blind, so that "blind table >= blinded lookahead" is a fair comparison. If it leaves the
# lookahead ANY residual advantage, the gate is biased against every table it judges: a table that
# passes is still safe (the bar was too high), but a table that FAILS may be fine, and a deck can be
# sent for an expensive regeneration it never needed. FiveColour cost a week of exactly that.
#
# THE PROPERTY. At m=1 the bottoming decision removes ONE card, so no legal-subset table is built
# (MTG_BOTTOM_LEGAL needs count>1) and these two arms are the SAME procedure at the SAME sample count
# -- one rollout per candidate removal:
#     lookahead   MTG_EXHAUSTIVE_BOTTOM=0                      evaluates the REAL library order
#     blindK1     ... + MTG_NC_BLIND_BOTTOM=1 K=1 SALT=<s>     evaluates a FRESH reshuffle
# Under a working confound the game plays neither order, so both are single draws from the same
# distribution and NEITHER knows anything about the played game. Their expected performance is equal.
# Any reproducible gap is information leaking from the pre-decision evaluation into the played game.
#
# THE NOISE FLOOR. blindK1 is run twice with different salts. That is the same estimator sampling
# different futures, so it bounds how much of any gap is luck. The gap must clear it to count.
#
# WHEN TO RUN. Not in smoke -- it is ~35 min on FiveColour and needs the game count for power. Run it
# when anything touches the bottomer, the confound, the shuffle, or the salts; and before trusting a
# NEW "the table loses its confounded A/B" verdict, because that is the verdict this can invalidate.
#
#   bash test/confound_gate_check.sh                          # FiveColour (most fetchlands = most exposed)
#   DECK=decks/Goblins/Goblins.cod GAMES=2000 bash test/confound_gate_check.sh
#   MODE=3 bash test/confound_gate_check.sh                   # check a candidate fix
set -uo pipefail
cd "$(dirname "$0")/.."
BIN=${BIN:-./build/Release/mtg}
DECK=${DECK:-decks/FiveColour/FiveColour.cod}
STEM=$(basename "$DECK"); STEM=${STEM%.*}
PROF=${PROF:-$(dirname "$DECK")/$STEM.profile.json}
GAMES=${GAMES:-1000}
SEED=${SEED:-1004004}
MODE=${MODE:-1}
THREADS=${THREADS:-0}
TOL=${TOL:-0.010}          # turns of slack ON TOP of the measured noise floor
OUT=${OUT:-logs/confound_gate_check/$STEM.m$MODE}
mkdir -p "$OUT"
[ -e "$BIN" ] || { echo "missing $BIN -- run ./build.sh"; exit 1; }
[ -e "$DECK" ] || { echo "missing deck $DECK"; exit 1; }

ARGS="$DECK --profile $PROF --cards-json src/cards/data/cards.json --games $GAMES --seed $SEED --threads $THREADS"
echo "=== confound gate check: $STEM  mode=$MODE games=$GAMES seed=$SEED ==="

arm(){ local tag=$1; shift
  [ -s "$OUT/$tag.out" ] && { echo "  $tag (cached)"; return; }
  echo "  running $tag ..."
  env MTG_CONFOUND_BOTTOM=$MODE "$@" $BIN $ARGS --log-dir "$OUT/games_$tag" > "$OUT/$tag.out" 2> "$OUT/$tag.err"
}
BLIND="MTG_EXHAUSTIVE_BOTTOM=0 MTG_NC_BLIND_BOTTOM=1 MTG_NC_BLIND_BOTTOM_K=1"
arm lookahead MTG_EXHAUSTIVE_BOTTOM=0
arm blind_a   $BLIND MTG_NC_BLIND_BOTTOM_SALT=8675309
arm blind_b   $BLIND MTG_NC_BLIND_BOTTOM_SALT=19260817

python3 - "$OUT" "$TOL" <<'PY'
import json, glob, os, sys, math
OUT, TOL = sys.argv[1], float(sys.argv[2])
def load(tag):
    g = {}
    for f in glob.glob(os.path.join(OUT, 'games_' + tag, '*.json')):
        j = json.load(open(f)); k = j['mulliganSequence'][-1]
        g[j['gameNumber']] = (k['attempt'],
                              (j['result']['turn'] if j['result']['winner'] == 'player' else 9) or 9)
    return g
A, B, C = load('lookahead'), load('blind_a'), load('blind_b')
common = sorted(set(A) & set(B) & set(C))
m1 = [g for g in common if A[g][0] == 1]
if len(m1) < 50:
    print(f"INCONCLUSIVE: only {len(m1)} m=1 games -- raise GAMES"); raise SystemExit(2)
def st(xs):
    n = len(xs); m = sum(xs) / n
    return m, math.sqrt(sum((x - m) ** 2 for x in xs) / (n - 1)) / math.sqrt(n)
gap, gse = st([A[g][1] - B[g][1] for g in m1])          # lookahead - blind  (negative = lookahead better)
flr, fse = st([B[g][1] - C[g][1] for g in m1])          # blind - blind      (pure luck)
print(f"\n  m=1 games: {len(m1)}")
print(f"  lookahead - blind : {gap:+.4f}t (se {gse:.4f})   <- must be ~0 if the gate is fair")
print(f"  blind - blind     : {flr:+.4f}t (se {fse:.4f})   <- noise floor (same estimator, 2 salts)")
bar = abs(flr) + TOL
print(f"  bar = |noise| + TOL = {bar:.4f}")
if gap < -bar:
    print(f"\n  FAIL: the gate FAVOURS THE LOOKAHEAD by {-gap:.4f}t beyond the noise floor.")
    print("  Every 'the table loses its confounded A/B' verdict measured this way is biased against")
    print("  the table by about this much. Do NOT send a deck for regeneration on such a verdict.")
    raise SystemExit(1)
if gap > bar:
    print(f"\n  FAIL (other direction): the gate PENALISES the lookahead by {gap:.4f}t.")
    raise SystemExit(1)
print("\n  PASS: seeing the real order buys nothing under this confound mode -- the gate is fair.")
PY
rc=$?
echo "=== confound gate check exit=$rc ==="
exit $rc
