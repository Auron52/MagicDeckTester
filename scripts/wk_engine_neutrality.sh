#!/usr/bin/env bash
# ENGINE NEUTRALITY PROOF for the WhiteKnights integration -- does landing it move any SHIPPED
# deck's play? Supersedes wk_stage_a_neutrality.sh (same method, wider deck set, two liveness arms).
#
# WHAT IS BEING PROVED, and why each arm is here:
#
# Stage A (battle cry) touched three shared paths:
#   A1. ApplyBattleCry, called in both worlds right after ApplyAttackSelfPumps. Gated on
#       battle_cry_power > 0, which only the two new cards set -> expected inert everywhere.
#   A2. FireAttackCreateTokens' OpponentHeads() multiply is now gated on
#       attack_tokens_per_opponent, whose DEFAULT IS TRUE. Adeline must stay byte-identical -- and
#       that multiply is observable ONLY in a 2HG job, so a 1-head-only A/B would prove nothing
#       about the edit actually made. The *2hg rows are load-bearing, not padding.
#   A3. PendingAttackDamage (the search's damage projection) now builds two extra per-attacker
#       vectors and runs a second pass. Behaviourally gated on bc_total > 0, but it is on the hot
#       path for EVERY deck, so it is the edit most able to move play by accident.
#
# Stage B replaced THREE open-coded double-strike expressions with one shared oracle
# (CreatureHasDoubleStrike). Claimed byte-identical -- and this is the arm that is easy to fake:
#   ONLY TWO DECKS IN THE REPO CONTAIN A DOUBLE-STRIKE SOURCE AT ALL (KittyEquipment: Kor Duelist
#   + Balan, the two Equipment paths; slivers_vial: Thrumming Hivepool, the grants_double_strike
#   lord path and hence the bs.ds prefilter). Run without those two, a "0 differ" result on Stage B
#   means the oracle returned false everywhere and nothing was tested. They are mandatory.
#
# Method: per-game PLAY DIGEST equality (digest-equality-beats-a-sign-test), at SHIPPED settings --
# no --depth, so each deck's own profile/value sidecar drives, which is the play we ship. Each arm
# is ONE pooled `mtg --batch` over every deck at once (one queue, one tail). Four processes is
# forced by needing two BINARIES and two mutated card files -- a genuine data dependency, not the
# forbidden per-arm wave.
#
# READ THE LIVENESS COLUMNS FIRST. Digest equality proves neutrality only once the harness is shown
# able to see a difference; equality can equally mean BROKEN. LIVE-T flips Adeline's token gate
# (must move knights2hg); LIVE-D strips every double-strike grant (must move kitty and slivers).
set -euo pipefail
cd /workspaces/MagicDeckTester
# ABSOLUTE: the HEAD build runs inside the worktree, where a relative logs/ path points at the
# worktree's own (nonexistent) directory.
OUT=$PWD/logs/wk_neutrality; mkdir -p "$OUT"
WT=/tmp/wk_head_worktree

# ---- arm HEAD: the pre-change engine, built in a worktree at HEAD -------------------------------
# A worktree rather than logs/snapshots: a snapshot baseline is whatever last ran that tier, not HEAD.
if [ ! -d "$WT" ]; then git worktree add --detach "$WT" HEAD > /dev/null 2>&1; fi
( cd "$WT" && ./build.sh > "$OUT/head_build.log" 2>&1 )
test -x "$WT/build/Release/mtg"

python3 - "$OUT" <<'PY'
import json, os, sys
out = sys.argv[1]
root = os.path.abspath('.')
# (label, deck dir, stem, games, 2HG?)
DECKS = [
    ("knights",     "Knights",        "Knights",        400, False),  # Adeline + Acclaimed Contender
    ("knights2hg",  "Knights",        "Knights",        200, True),   # the ONLY heads-gate observer
    ("kitty",       "KittyEquipment", "KittyEquipment", 300, False),  # STAGE B: both Equipment paths
    ("kitty2hg",    "KittyEquipment", "KittyEquipment", 150, True),
    ("slivers",     "slivers_vial",   "slivers_vial",   300, False),  # STAGE B: the ds LORD + bs.ds
    ("slivers2hg",  "slivers_vial",   "slivers_vial",   150, True),
    ("angels",      "Angels",         "Angels",         300, False),  # quest counters/attack triggers
    ("angels2hg",   "Angels",         "Angels",         150, True),
    ("goblins",     "Goblins",        "Goblins",        300, False),  # ApplyAttackSelfPumps' twin
    ("goblins2hg",  "Goblins",        "Goblins",        150, True),
    ("minotaur",    "Minotaur",       "Minotaur",       300, False),  # attack_pump_matching_power
    ("minotaur2hg", "Minotaur",       "Minotaur",       150, True),
    ("giants",      "Giants",         "Giants",         200, False),  # attack_trigger_damage_any
    ("fungus",      "Fungus",         "Fungus",         200, False),  # widest attacker lists
    ("melira",      "Melira Pod",     "Melira Pod",     200, False),  # keyword-mask baseline
]
jobs = []
for label, d, stem, games, twohg in DECKS:
    deck = None
    for ext in ('.cod', '.txt'):
        cand = os.path.join(root, 'decks', d, stem + ext)
        if os.path.exists(cand): deck = cand; break
    assert deck, (d, stem)
    prof = os.path.join(root, 'decks', d, stem + '.profile.json')
    j = {"name": label, "deck": deck, "games": games, "seed": 1001}
    if os.path.exists(prof): j["profile"] = prof
    if twohg: j.update({"starting_life": 30, "opponent_heads": 2})
    jobs.append(j)          # NO depth/budget_ms -> the deck's own profile drives (shipped settings)
json.dump({"jobs": jobs}, open(f'{out}/manifest.json', 'w'), indent=1)
print(f'manifest: {len(jobs)} jobs, {sum(j["games"] for j in jobs)} games per arm')

def load(): return json.load(open('src/cards/data/cards.json'))

# LIVE-T: Adeline's per-opponent token becomes FLAT. In a 2HG job her output must fall 2 -> 1.
db = load()
for c in db['cards']:
    if c['name'] == 'Adeline, Resplendent Cathar':
        c['parameters']['attack_tokens_per_opponent'] = False
json.dump(db, open(f'{out}/cards.live_tokens.json', 'w'), indent=1)

# LIVE-D: strip EVERY double-strike grant in the repo. kitty and slivers must move; nothing else
# should, since no other deck holds one of these three cards.
db = load()
n = 0
for c in db['cards']:
    p = c.get('parameters', {})
    for k in ('grants_double_strike', 'double_strike_while_equipped'):
        if p.get(k): p[k] = False; n += 1
    if p.get('double_strike_min_equipment'): p['double_strike_min_equipment'] = 0; n += 1
json.dump(db, open(f'{out}/cards.live_ds.json', 'w'), indent=1)
print(f'LIVE-D stripped {n} double-strike grant params')
PY

arm () {   # arm <label> <binary> [cards-json]
  local label="$1" bin="$2" cj="${3:-}"
  rm -rf "$OUT/gl_$label"; mkdir -p "$OUT/gl_$label"
  local extra=()
  [ -n "$cj" ] && extra=(--cards-json "$cj")
  "$bin" --batch "$OUT/manifest.json" "${extra[@]}" \
      --game-log-dir "$OUT/gl_$label" > "$OUT/$label.out" 2> "$OUT/$label.err"
}

# Every arm runs from the REPO ROOT so sidecars resolve off the manifest's absolute deck paths
# identically; only the binary and the cards.json differ.
echo "-- arm HEAD  (pre-change engine + HEAD cards.json) --"
arm head "$WT/build/Release/mtg" "$WT/src/cards/data/cards.json"
echo "-- arm NEW   (working tree engine + working tree cards.json) --"
arm new  ./build/Release/mtg
echo "-- arm LIVE-T (working tree engine, Adeline's heads gate flipped) --"
arm livet ./build/Release/mtg "$OUT/cards.live_tokens.json"
echo "-- arm LIVE-D (working tree engine, every double-strike grant stripped) --"
arm lived ./build/Release/mtg "$OUT/cards.live_ds.json"

echo
printf '%-13s %10s %8s %8s   %s\n' CASE 'HEADvNEW' 'LIVE-T' 'LIVE-D' reading
tot=0; miss=0
for case in $(python3 -c "
import json;print(' '.join(j['name'] for j in json.load(open('$OUT/manifest.json'))['jobs']))"); do
  a="$OUT/gl_head/$case.wins"; b="$OUT/gl_new/$case.wins"
  t="$OUT/gl_livet/$case.wins"; e="$OUT/gl_lived/$case.wins"
  if [ ! -s "$a" ] || [ ! -s "$b" ]; then
    printf '%-13s %10s %8s %8s   %s\n' "$case" MISSING - - 'NO DIGESTS -- harness broken'
    miss=$((miss+1)); continue
  fi
  n=$(wc -l < "$a")
  d1=$(diff <(sort -n "$a") <(sort -n "$b") | grep -c '^<' || true)
  d2=0; [ -s "$t" ] && d2=$(diff <(sort -n "$a") <(sort -n "$t") | grep -c '^<' || true)
  d3=0; [ -s "$e" ] && d3=$(diff <(sort -n "$a") <(sort -n "$e") | grep -c '^<' || true)
  tot=$((tot+d1))
  note=neutral; [ "$d1" != "0" ] && note='*** PLAY MOVED ***'
  printf '%-13s %5s/%-4s %8s %8s   %s\n' "$case" "$d1" "$n" "$d2" "$d3" "$note"
done
echo
echo "games whose play differs, HEAD vs NEW: $tot   (missing cases: $miss)"
echo "LIVENESS REQUIRED: LIVE-T nonzero on knights2hg; LIVE-D nonzero on kitty AND slivers."
echo "If either liveness column is all zeros, the 0 in HEADvNEW proves nothing."
