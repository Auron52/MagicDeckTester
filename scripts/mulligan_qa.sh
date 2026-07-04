#!/usr/bin/env bash
# mulligan_qa.sh -- QA the mulligan-profile GENERATOR on already-analyzed decks.
#
# For each deck key, REGENERATES its profile with the current analyzer, then measures the
# regenerated profile against the COMMITTED one. The committed profile is what the regression
# ground truth was built with, so running the suite with the regenerated profile and reading
# the per-case got-vs-exp delta IS the A/B (the suite is the A/B harness; see the
# regression-testing skill). Lower avg-win-turn / higher won = the new generator's profile is
# better; identical = the generator is stable (byte-identical analyzer games -> same profile).
#
# Safe: each deck's committed profile is restored with `git checkout` afterwards (profiles are
# committed). New profiles are archived under logs/mulligan_qa/ for inspection/diff.
#
# Usage: scripts/mulligan_qa.sh [deckkey ...]   (default: all five suite decks)
set -uo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$HERE"
OUT="$HERE/logs/mulligan_qa"; mkdir -p "$OUT"

declare -A DECK_FILE=(
  [burn]=decks/burn.txt [slivers]=decks/slivers_vial.txt
  [th]=decks/treasure_hunt.txt [knights]=decks/Knights.cod [antilife]=decks/Anti-Lifegain.cod)
declare -A DECK_PROF=(
  [burn]=decks/burn.profile.json [slivers]=decks/slivers_vial.profile.json
  [th]=decks/treasure_hunt.profile.json [knights]=decks/Knights.profile.json
  [antilife]=decks/Anti-Lifegain.profile.json)

KEYS=("${@:-burn slivers th knights antilife}"); KEYS=(${KEYS[@]})

for key in "${KEYS[@]}"; do
  deck="${DECK_FILE[$key]:-}"; prof="${DECK_PROF[$key]:-}"
  [ -n "$deck" ] || { echo "[qa] unknown deck key '$key'"; continue; }
  echo "=== [qa] $key : regenerating profile ($deck) ==="
  ts=$(date +%s 2>/dev/null || echo 0)
  python3 scripts/analyze_deck.py "$deck" --no-rebuild > "$OUT/$key.analyze.log" 2>&1
  rc=$?
  if [ $rc -ne 0 ]; then echo "[qa] $key analyze FAILED (rc=$rc); see $OUT/$key.analyze.log"; git checkout -- "$prof" 2>/dev/null; continue; fi
  # Archive the regenerated profile, then diff vs committed.
  cp "$prof" "$OUT/$key.regenerated.json"
  if git diff --quiet -- "$prof"; then
    echo "[qa] $key: regenerated profile == committed (generator STABLE / byte-identical)."
  else
    echo "[qa] $key: regenerated profile DIFFERS from committed -- diff saved:"
    git --no-pager diff -- "$prof" > "$OUT/$key.profile.diff" 2>&1
    echo "    (see $OUT/$key.profile.diff)"
  fi
  # Restore the committed profile so the tree stays clean and other tools see the baseline.
  git checkout -- "$prof" 2>/dev/null
  echo "[qa] $key done."
done
echo "=== [qa] complete. Regenerated profiles + diffs under $OUT/ ==="
echo "Next: for any deck whose profile DIFFERS, A/B it via the suite (put the regenerated"
echo "profile in place, run test/regression.sh --smoke/--regression, read got-vs-exp delta)."
