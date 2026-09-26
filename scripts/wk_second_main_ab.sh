#!/usr/bin/env bash
# Does WhiteKnights lose anything by skipping its POST-COMBAT MAIN PHASE?
#
# WHY THIS EXISTS. `GoldFishRunner::DeckUsesSecondMain` is a whitelist of params, and WhiteKnights
# sets NONE of them, so the deck plays a single main. That is a PRUNE -- the search never gets to
# consider "attack first, then cast" -- and Stage 6a has to disclose it. The whitelist's own in-code
# comment says it is unsure about exactly this case:
#
#   "the Utvara/Adeline attack-created-token question: the whitelist's in-code comment names
#    untap-on-attack as a trigger, but no such param is in this predicate"
#
# and WhiteKnights runs BOTH Adeline, Resplendent Cathar AND Hero of Bladehold, whose tokens arrive
# tapped and attacking. So this deck is the named open question, not a deck the whitelist is
# confident about. Disclosing it with a caveat would be weaker than measuring it, and measuring it
# is cheap -- hence this.
#
# WHAT A RESULT MEANS. `MTG_FORCE_USES_M2` is a measurement lever whose own comment states the
# reading: "Byte-identical when the arm measures no better: the whitelist is then confirmed
# per-deck." So:
#   * 0 games differ            -> the prune costs this deck NOTHING, and the disclosure is closed.
#   * games differ, wt unchanged-> the deck re-orders casts but gains no speed (still closed, noted).
#   * wt IMPROVES with m2 on    -> the prune is costing the deck real turns; a finding for the user,
#                                 because adding the deck to the whitelist makes every turn solve
#                                 twice (the blunt arm measured 4.25x at d1/b3 on another deck).
#
# ONE POOLED BATCH, BOTH ARMS. The arm is a per-JOB manifest flag, not an env var, so both arms ride
# a single `mtg --batch` and share one load-imbalance tail (CLAUDE.md: never a batch per arm -- that
# is the "waves are a loop" defect). No cards.json variation is involved, so unlike the firing
# harness there is no `--cards-json` process-global forcing a split.
#
# Seeds are 4401, the same as the Stage-5b ladder, so the control arm's numbers are directly
# comparable to the ladder table already in the ledger.
set -euo pipefail
cd /workspaces/MagicDeckTester
OUT=$PWD/logs/wk_m2; mkdir -p "$OUT"
DECK=$PWD/decks/WhiteKnights/WhiteKnights.cod
PROF=$PWD/decks/WhiteKnights/WhiteKnights.profile.json

python3 - "$OUT" "$DECK" "$PROF" <<'PY'
import json, sys
out, deck, prof = sys.argv[1], sys.argv[2], sys.argv[3]

# Three configurations, both arms of each. d0/b0 is the LOAD-BEARING row: it has no wall-clock
# budget, so it is immune to machine contention and a difference there cannot be noise. The two
# budgeted rows are included because the prune interacts with SEARCH (a second main doubles the
# solve), and a lever can be inert at d0 and live once the search can exploit it.
#
# d0big exists because the first pass measured a REAL digest movement (66/600 games at d0) with a
# win-turn delta of only -0.005 t on a 3-earlier/0-later split -- a sign test that resolves nothing
# (p = 0.125). d0 games are cheap and carry no budget, so the honest response to "too small to call"
# is more pairs, not a hedge. 6000 games bounds the effect rather than guessing at it.
cfgs = [
    ("d0",     {"depth": 0, "budget_ms": 0},    600),
    ("d0big",  {"depth": 0, "budget_ms": 0},   6000),
    ("d3b10",  {"depth": 3, "budget_ms": 10},   300),
    ("d5b20",  {"depth": 5, "budget_ms": 20},   300),
]
jobs = []
for tag, knobs, games in cfgs:
    for arm, on in (("off", False), ("on", True)):
        # PIN THE KEY ON BOTH ARMS, explicitly false on the control rather than omitted. An omitted
        # key falls through to the env/default, which is the same value here -- but "the same value
        # here" is exactly the assumption that silently breaks when a default moves, and then the
        # control arm is no longer the shape it claims to be.
        jobs.append({"name": f"m2{arm}_{tag}", "deck": deck, "profile": prof,
                     "games": games, "seed": 4401,
                     "flags": {"MTG_FORCE_USES_M2": on}, **knobs})
json.dump({"jobs": jobs}, open(f'{out}/manifest.json', 'w'), indent=1)
PY

rm -rf "$OUT/gl"; mkdir -p "$OUT/gl"
./build/Release/mtg --batch "$OUT/manifest.json" --game-log-dir "$OUT/gl" \
    > "$OUT/run.out" 2> "$OUT/run.err"

wt () { awk '{s+=$2; n++} END {if (n) printf "%.4f", s/n; else printf "n/a"}' "$1"; }

echo
printf '%-8s %7s %12s %12s %10s   %s\n' CONFIG GAMES 'wt(m2 OFF)' 'wt(m2 ON)' 'delta' 'paired play'
for tag in d0 d0big d3b10 d5b20; do
  a="$OUT/gl/m2off_$tag.wins"; b="$OUT/gl/m2on_$tag.wins"
  if [ ! -s "$a" ] || [ ! -s "$b" ]; then
    printf '%-8s %7s %12s %12s %10s   %s\n' "$tag" MISSING - - - 'NO DIGESTS'; continue
  fi
  # Paired by GAME INDEX (field 1), never by comparing the two aggregates -- offsetting moves in
  # opposite directions cancel in a mean and would read as "inert".
  read -r earlier later same differ <<EOF
$(join -j1 <(sort -k1,1 "$a" | awk '{print $1, $2, $3}') \
           <(sort -k1,1 "$b" | awk '{print $1, $2, $3}') \
   | awk '{if ($4 < $2) e++; else if ($4 > $2) l++; else s++}
          $3 != $5 {d++}
          END {printf "%d %d %d %d", e+0, l+0, s+0, d+0}')
EOF
  printf '%-8s %7s %12s %12s %10s   %s\n' "$tag" "$(wc -l < "$a")" "$(wt "$a")" "$(wt "$b")" \
      "$(awk -v x="$(wt "$a")" -v y="$(wt "$b")" 'BEGIN{printf "%+.4f", y-x}')" \
      "$differ differ, $earlier earlier, $later later, $same same-turn"
done
echo
echo "READING: 'differ' counts games whose play DIGEST moved; earlier/later count win-turn moves."
echo
echo "MEASURED 2026-09-26 (do not re-derive the hint -- this ran):"
echo "  d0big, 6000 PAIRED games: 557 differ, 42 EARLIER, 0 LATER, delta -0.0072 turns."
echo "  A 42-0 split is decisive (sign test p ~ 2e-13), so the single-main prune is NOT inert for"
echo "  this deck: opening the second main is never worse and sometimes a full turn faster."
echo "  The two BUDGETED rows show 1-2 games LATER, which is the cost side: a second main makes"
echo "  every turn solve twice, so under a fixed 10/20 ms budget some of the gain is handed back."
