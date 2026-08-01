#!/usr/bin/env bash
# MECHANISM probe for the Treasure Hunt discard/pitch rules -- WHICH cards each arm actually
# throws away, not what the games scored.
#
# WHY THIS EXISTS. Avg win turn cannot resolve these rules: MTG_TRACE=lepitch measured that only
# ~2% of Land's Edge pitches are even a strict subset of the lands in hand (pitch them all and the
# set is the same set whatever order names it), and the cleanup shed is bimodal -- most games shed
# nothing at all. An outcome null over such a rate is a BOUND, not a verdict, and reading it as
# "no effect" is a trap this repo has fallen into twice.
#
# The card names, though, are measurable at any sample size. The claim under test -- "we pitch the
# cycling lands we needed, and we save a Reliquary Tower that is already dead" -- is a claim about
# WHICH CARDS LEAVE THE HAND, so it can be checked directly instead of inferred from a null. If an
# arm changes the outcome by nothing but sheds materially fewer diggers, that is a real finding: it
# says the rule does what it claims and that this deck's win turn is simply insensitive to it.
#
# Classes are derived from cards.json parameters, never from a card-name table.
#
#   bash test/th_mechanism_probe.sh [games]
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.." || exit 1
. test/lib/harness.sh
BIN=$(harness_bin) || exit 1
GAMES=${1:-4000}
OUT=logs/th_mechanism
mkdir -p "$OUT"

DECK=$(h_deck treasure_hunt) || exit 1
PROF=$(h_profile treasure_hunt) || exit 1
SEED=24024        # fresh again: not in any suite mode, not in the power A/B's 16016..23023

# One process per ARM -- the arms differ only by process-level MTG_* env, which is the one thing a
# pooled manifest cannot express.
run_arm() {  # <label> <le> <rung> <tower>
    printf '   %-12s ' "$1"
    MTG_TRACE=discard,lepitch MTG_LE_RANKED_PITCH=$2 MTG_TH_DISCARD=$3 MTG_TH_TOWER_SPARE=$4 \
        "$BIN" "$DECK" --profile "$PROF" --games "$GAMES" --seed "$SEED" --depth 0 \
        --ignore-play-profile >/dev/null 2>"$OUT/$1.trace"
    echo "$(grep -c '^\[discard\]' "$OUT/$1.trace") cleanup, $(grep -c '^\[lepitch\]' "$OUT/$1.trace") pitches"
}
echo "=== collecting ($GAMES d0 games/arm, seed $SEED) ==="
run_arm A_shipped   0 1 0
run_arm D_le1r9     1 9 0
run_arm E_le1r9_t1  1 9 1
run_arm F_tower     0 1 1

python3 - "$OUT" "$GAMES" <<'PY'
import sys, json, re, os, collections
out, games = sys.argv[1], int(sys.argv[2])

# ---- classify every land in the deck from its PARAMETERS (no card-name table) ----------------
db = json.load(open('src/cards/data/cards.json'))
cards = {c['name']: c for c in (db['cards'] if isinstance(db, dict) and 'cards' in db else db)}
def klass(name):
    c = cards.get(name)
    if c is None: return 'unknown'
    p = c.get('parameters', {})
    if 'Land' not in c.get('types', []):        return 'NONLAND'
    if p.get('no_max_hand_size'):               return 'tower'
    if p.get('cycling_cost') or p.get('sacrifice_draw_cost'): return 'digger-cycle'
    if p.get('etb_scry') or p.get('etb_surveil'):             return 'digger-scry'
    if p.get('enters_tapped_with_depletion'):   return 'depletion'
    if p.get('enters_tapped'):                  return 'tapped'
    return 'untapped'
ORDER = ['NONLAND','tower','digger-cycle','digger-scry','depletion','tapped','untapped']

ARMS = ['A_shipped','D_le1r9','E_le1r9_t1','F_tower']
cleanup, pitch, sub, diffset = {}, {}, {}, {}
# A cleanup discard that happened while a Reliquary Tower sat in hand AND a land drop was still
# available is not a ranking mistake -- it is a LAND-DROP mistake, and no discard order can fix it.
# Counted here because this probe is already reading every discard, and because a rule that cannot
# be measured is worth much less than a defect that can.
avoidable = {}
for a in ARMS:
    f = f"{out}/{a}.trace"
    if not os.path.exists(f): continue
    cl, pi, s, d = collections.Counter(), collections.Counter(), 0, 0
    av = 0
    for ln in open(f, errors='replace'):
        if ln.startswith('[discard]'):
            if ' tower=1 ' in ln and ' dropopen=1' in ln: av += 1
            m = re.search(r'-> (.+?)\s*$', ln)
            if m: cl[klass(m.group(1))] += 1
        elif ln.startswith('[lepitch]'):
            if ' sub=1' in ln: s += 1
            if ' diffset=1' in ln: d += 1
            m = re.search(r'burned=(.*?)\s*$', ln)
            if m and m.group(1):
                for nm in m.group(1).split(','): pi[klass(nm)] += 1
    cleanup[a], pitch[a], sub[a], diffset[a], avoidable[a] = cl, pi, s, d, av

def table(title, data, base='A_shipped'):
    print(f"\n{title}  (cards per 1000 games; delta vs A)")
    hdr = f"   {'class':<14}" + "".join(f"{a:>22}" for a in ARMS if a in data)
    print(hdr); print('   ' + '-'*(len(hdr)-3))
    for k in ORDER:
        if not any(data[a][k] for a in data): continue
        row = f"   {k:<14}"
        for a in ARMS:
            if a not in data: continue
            r = 1000.0*data[a][k]/games
            rb = 1000.0*data[base][k]/games
            row += f"{r:>12.1f}" + ("" if a == base else f"{r-rb:>+10.1f}")
        print(row)
    row = f"   {'TOTAL':<14}"
    for a in ARMS:
        if a not in data: continue
        r  = 1000.0*sum(data[a].values())/games
        rb = 1000.0*sum(data[base].values())/games
        row += f"{r:>12.1f}" + ("" if a == base else f"{r-rb:>+10.1f}")
    print(row)

table("CLEANUP DISCARDS", cleanup)
table("LAND'S EDGE PITCHES", pitch)
print("\nLand's Edge decision rate (a pitch can only matter when it is a STRICT SUBSET):")
for a in ARMS:
    if a in sub:
        tot = sum(pitch[a].values())
        print(f"   {a:<12} strict-subset calls={sub[a]:<6} set-differs-from-hand-order={diffset[a]:<6}"
              f"  cards burned={tot}")
print("\nAVOIDABLE discards -- a Tower sat in HAND with a land drop still open while we shed:")
for a in ARMS:
    if a in avoidable:
        tot = sum(cleanup[a].values())
        pct = 100.0*avoidable[a]/tot if tot else 0.0
        print(f"   {a:<12} {avoidable[a]:>6} of {tot:<6} discards ({pct:4.1f}%)")
print("   Any nonzero count here is a LAND-DROP defect, not a discard-ranking one: playing the")
print("   Tower would have ended the decision outright. No ordering rule can recover it.")

print("\nREADING IT: 'digger-cycle' is the class the cycling-land hypothesis is about. A rule that")
print("sheds fewer of those while burning the same TOTAL is doing exactly what it claims, whatever")
print("the win turn says; a rule that changes nothing here cannot have changed the win turn either.")
PY
echo ALLDONE
