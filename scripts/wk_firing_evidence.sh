#!/usr/bin/env bash
# FIRING EVIDENCE for every mechanism added for WhiteKnights.
#
# This repo's rule: a new mechanism must be shown to FIRE, not merely to build. A feature that
# compiles but never executes produces digests identical to the baseline, and "identical" then reads
# as "proven neutral" when it actually means "proven dead" (the digest-equality-can-mean-BROKEN
# lesson). The engine-neutrality harness proves the change does not disturb OTHER decks; this proves
# it does something in THIS one.
#
# Method, per mechanism: take the shipped cards.json and STRIP exactly that mechanism's param, then
# compare per-game play digests on WhiteKnights. A NONZERO difference means the mechanism is live --
# the deck plays differently without it. A ZERO means it never fires, which for every row here would
# be a bug, not a neutrality result.
#
# Read alongside: the win-turn column says whether it also MATTERS (a mechanism can fire and be
# worth ~nothing, which is a finding to disclose, not a failure).
set -euo pipefail
cd /workspaces/MagicDeckTester
OUT=$PWD/logs/wk_firing; mkdir -p "$OUT"
DECK=$PWD/decks/WhiteKnights/WhiteKnights.cod
PROF=$PWD/decks/WhiteKnights/WhiteKnights.profile.json

python3 - "$OUT" "$DECK" "$PROF" <<'PY'
import json, sys
out, deck, prof = sys.argv[1], sys.argv[2], sys.argv[3]

# Two jobs: normal play, and a 2HG job -- the ONLY place Hero of Bladehold's flat-vs-per-opponent
# token gate is observable at all, so without this row that mechanism cannot be shown to fire.
#
# EVERY job attaches the deck's PROFILE. This was missing until 2026-09-26 and it mattered twice
# over: the win-turn column was measuring a deck we do not ship (no card scores, no mulligan
# policy), and the Stage-4 "baseline speed" line in the ledger was quoting this harness's base
# arm, so the headline number for the deck was profile-less too. The digest comparison itself was
# never wrong -- both arms shared the same apparatus -- but a firing row's second question ("does
# it also MATTER?") is answered in win turns, and those have to be the shipped deck's win turns.
jobs = [
    {"name": "wk",     "deck": deck, "profile": prof, "games": 400, "seed": 1001, "depth": 0, "budget_ms": 0},
    {"name": "wk_d3",  "deck": deck, "profile": prof, "games": 150, "seed": 1001, "depth": 3, "budget_ms": 10},
    {"name": "wk_2hg", "deck": deck, "profile": prof, "games": 200, "seed": 1001, "depth": 0, "budget_ms": 0,
     "starting_life": 30, "opponent_heads": 2},
]
json.dump({"jobs": jobs}, open(f'{out}/manifest.json', 'w'), indent=1)

# The BASE arm additionally carries the Stage-5b/5c depth ladder. Those jobs need no cards.json
# variation, so pooling them here costs one shared tail instead of standing up a seventh batch
# (CLAUDE.md: one pooled queue, and a separate batch per arm/variant is the "waves are a loop"
# defect). Distinct names -- a bare "wk_d3" would collide with the firing job above and the two
# would overwrite each other's .wins.
ladder = [
    {"name": "wk_lad_d0",       "deck": deck, "profile": prof, "games": 600, "seed": 4401, "depth": 0, "budget_ms": 0},
    {"name": "wk_lad_d3_b10",   "deck": deck, "profile": prof, "games": 300, "seed": 4401, "depth": 3, "budget_ms": 10},
    {"name": "wk_lad_d5_b20",   "deck": deck, "profile": prof, "games": 300, "seed": 4401, "depth": 5, "budget_ms": 20},
    {"name": "wk_lad_d5_b2000", "deck": deck, "profile": prof, "games": 150, "seed": 4401, "depth": 5, "budget_ms": 2000},
]
json.dump({"jobs": jobs + ladder}, open(f'{out}/manifest_base.json', 'w'), indent=1)

def load(): return json.load(open('src/cards/data/cards.json'))
def by(db, name):
    for c in db['cards']:
        if c['name'] == name: return c
    raise SystemExit(f'card not found: {name}')

def write(tag, mutate):
    db = load()
    mutate(db)
    json.dump(db, open(f'{out}/cards.{tag}.json', 'w'), indent=1)

# 1. battle cry -- both carriers at once (stripping one leaves the other's pump in place)
def no_bc(db):
    for n in ('Accorder Paladin', 'Hero of Bladehold'):
        by(db, n)['parameters']['battle_cry_power'] = 0
write('nobc', no_bc)

# 2. soulbond (Silverblade Paladin)
def no_sb(db):
    by(db, 'Silverblade Paladin')['parameters']['soulbond'] = False
write('nosb', no_sb)

# 3. Valiant Knight's activated team double-strike grant
def no_ds(db):
    by(db, 'Valiant Knight')['parameters']['team_pump_grants_double_strike'] = False
write('nods', no_ds)

# 4. Hero's FLAT token count -- flip it back to the old unconditional per-opponent scaling. Only the
#    2HG job can see this; at one head the two are arithmetically identical by construction.
def scale_hero(db):
    by(db, 'Hero of Bladehold')['parameters']['attack_tokens_per_opponent'] = True
write('heroscale', scale_hero)

# 5. Acclaimed Contender's WIDENED dig -- back to the inherited Knight-only filter. This is the
#    Stage-1 fix whose justification ("no deck holds an Aura or Equipment") was false for THIS deck:
#    it runs Lightning Greaves. If this arm is identical, the widening never mattered here and the
#    fix would need re-justifying.
def narrow_dig(db):
    by(db, 'Acclaimed Contender')['parameters']['etb_dig_subtypes'] = ['Knight']
write('narrowdig', narrow_dig)
PY

arm () {   # arm <label> [cards-json]
  local label="$1" cj="${2:-}"
  rm -rf "$OUT/gl_$label"; mkdir -p "$OUT/gl_$label"
  local extra=() mf="$OUT/manifest.json"
  [ "$label" = base ] && mf="$OUT/manifest_base.json"
  [ -n "$cj" ] && extra=(--cards-json "$cj")
  ./build/Release/mtg --batch "$mf" "${extra[@]}" \
      --game-log-dir "$OUT/gl_$label" > "$OUT/$label.out" 2> "$OUT/$label.err"
}

echo "-- baseline (shipped cards.json) --"
arm base
for a in nobc nosb nods heroscale narrowdig; do
  echo "-- strip: $a --"
  arm "$a" "$OUT/cards.$a.json"
done

wt () {  # mean win turn from a .wins file (field 2; unwon rows are already max_turns+1)
  awk '{s+=$2; n++} END {if (n) printf "%.3f", s/n; else printf "n/a"}' "$1"
}

echo
printf '%-11s %-9s %8s %10s %10s   %s\n' MECHANISM CASE 'DIFFER' 'wt(base)' 'wt(strip)' verdict
for a in nobc nosb nods heroscale narrowdig; do
  for case in wk wk_d3 wk_2hg; do
    b="$OUT/gl_base/$case.wins"; s="$OUT/gl_$a/$case.wins"
    if [ ! -s "$b" ] || [ ! -s "$s" ]; then
      printf '%-11s %-9s %8s %10s %10s   %s\n' "$a" "$case" MISSING - - 'NO DIGESTS'
      continue
    fi
    n=$(wc -l < "$b")
    d=$(diff <(sort -n "$b") <(sort -n "$s") | grep -c '^<' || true)
    v='FIRES'
    [ "$d" = "0" ] && v='inert in this config'
    printf '%-11s %-9s %4s/%-4s %10s %10s   %s\n' "$a" "$case" "$d" "$n" "$(wt "$b")" "$(wt "$s")" "$v"
  done
done
echo
echo "Every row except heroscale should FIRE on wk / wk_d3."
echo "heroscale is expected inert at 1 head (arithmetically identical) and MUST fire on wk_2hg."

# ---- Stage 5b/5c: the depth ladder, pooled into the base arm ------------------------------
# Read for MONOTONICITY (never worse deeper) and for budget starvation (d5 b20 vs b2000 on the
# same 150 games: if those two differ, the suite budget is constraining this deck).
echo
# NOTE on the last column: a .wins row for an UNWON game carries max_turns+1, which is
# indistinguishable from a genuine win on that turn without knowing the job's max_turns. So report
# the SLOWEST win turn instead of a wins count -- if it sits far below any plausible max_turns,
# every game was won, and the reader can see that for themselves rather than trusting a threshold
# baked into this script.
printf '%-16s %7s %14s %10s\n' CONFIG GAMES 'avg win turn' 'slowest'
for j in wk_lad_d0 wk_lad_d3_b10 wk_lad_d5_b20 wk_lad_d5_b2000; do
  f="$OUT/gl_base/$j.wins"
  [ -s "$f" ] || { printf '%-16s %7s %14s %10s\n' "$j" MISSING - -; continue; }
  printf '%-16s %7s %14s %10s\n' "$j" "$(wc -l < "$f")" "$(wt "$f")" \
      "$(awk '{if ($2 > m) m = $2} END {print m}' "$f")"
done
b20="$OUT/gl_base/wk_lad_d5_b20.wins"; b2k="$OUT/gl_base/wk_lad_d5_b2000.wins"
if [ -s "$b20" ] && [ -s "$b2k" ]; then
  # Compare only the games b2000 actually ran (it is the shorter job), by game index.
  echo
  join -j1 <(sort -k1,1 "$b2k" | awk '{print $1, $2}') <(sort -k1,1 "$b20" | awk '{print $1, $2}') \
    | awk '{if ($2 < $3) e++; else if ($2 > $3) l++; else s++}
           END {printf "budget check (d5 b2000 vs b20, paired): %d earlier, %d later, %d identical\n", e+0, l+0, s+0}'
fi
