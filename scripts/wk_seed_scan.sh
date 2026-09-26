#!/usr/bin/env bash
# Find claude-play seeds whose game actually CONTAINS the cards you want to verify.
#
# WHY THIS EXISTS. A claude-play sweep game is capped by --max-turns (8), and a fast aggro deck
# wins around turn 4. So a uniformly-seeded sweep of WhiteKnights spends most of its games never
# drawing the deck's 1-ofs at all: the first three agents back from the 2026-09-26 sweep all
# reported "battle cry verified, the other four mechanisms never appeared". Those are honest
# ties with zero flags, and they are ALSO near-zero coverage of the cards the sweep was run for.
#
# Fanning more uniform seeds does not fix that -- it buys more of the same early curve. What fixes
# it is CHOOSING seeds: a seed whose opening hand (plus the revealed top of library) already holds
# Hero of Bladehold / Silverblade Paladin / Valiant Knight gives an agent a game where the
# mechanism is reachable inside the turn budget.
#
# This is a SELECTION tool for verification coverage, not a measurement tool. Never quote win
# turns from a hand-picked seed set as the deck's speed -- picking seeds by content biases exactly
# that. Aggregate speed comes from the pooled batch (scripts/wk_firing_evidence.sh); this only
# decides which games a human/agent should look at.
#
# Usage:
#   bash scripts/wk_seed_scan.sh <first-seed> <last-seed> [card ...]
#   bash scripts/wk_seed_scan.sh 7900 7999 "Hero of Bladehold" "Silverblade Paladin"
set -euo pipefail
cd /workspaces/MagicDeckTester

FIRST="${1:?first seed}"
LAST="${2:?last seed}"
shift 2
CARDS=("$@")
if [ "${#CARDS[@]}" -eq 0 ]; then
  CARDS=("Hero of Bladehold" "Silverblade Paladin" "Valiant Knight" "Acclaimed Contender")
fi

DECK=decks/WhiteKnights/WhiteKnights.cod
PROF=decks/WhiteKnights/WhiteKnights.profile.json
BIN=./build/Release/mtg

# --reveal 8 so the scan sees the opening hand AND the next 8 draws; a card on draw 3 is reachable
# in an 8-turn game even though it is not in hand. Take the FIRST emitted decision only (the
# mulligan), which is why no --choices is needed and why one invocation per seed is enough.
echo "scanning seeds $FIRST..$LAST for: ${CARDS[*]}"
echo
for (( s = FIRST; s <= LAST; s++ )); do
  json=$("$BIN" "$DECK" --profile "$PROF" --claude-play --seed "$s" \
           --max-turns 8 --reveal 8 2>/dev/null \
         | sed -n '/<<<CLAUDE_DECISION>>>/,/<<<END_DECISION>>>/p') || true
  [ -n "$json" ] || continue
  hits=""
  for c in "${CARDS[@]}"; do
    case "$json" in *"$c"*) hits="$hits; $c" ;; esac
  done
  # `|| true`: without it, a final seed with no hits makes the loop's last command false and
  # `set -e` exits the whole script 1 -- a scan that worked perfectly reporting as a failure.
  [ -n "$hits" ] && printf 'seed %-6s %s\n' "$s" "${hits# ; }" || true
done
