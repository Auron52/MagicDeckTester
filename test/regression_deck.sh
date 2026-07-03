#!/usr/bin/env bash
# Run the regression suite for ONE deck across modes -- the per-deck counterpart to
# regression.sh. Thin wrapper over `regression.sh <mode> --deck=<name>`; each mode still
# writes its own results/ground-truth and prints the per-game audit (incl. the play-digest
# "play-changed" analysis), so you inspect and `--accept` per mode as usual.
#
# Usage:
#   bash test/regression_deck.sh <deck> [mode ...]
#     <deck>   a deck key from regression_cases.sh (slivers|burn|th|knights|antilife|hinata|...)
#     mode     one or more of: smoke regression overnight   (default: smoke regression overnight)
#
# Examples:
#   bash test/regression_deck.sh antilife                 # all three modes for antilife
#   bash test/regression_deck.sh antilife smoke regression # skip the 8h overnight
#
# Accept is deliberately NOT run here: inspect each mode's audit first, then
#   bash test/regression.sh <mode> --deck=<deck> --accept
# to promote that mode's ground truth (win turns + play digests) for this deck only.
set -uo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

deck="${1:-}"
[ -z "$deck" ] && { echo "usage: regression_deck.sh <deck> [smoke|regression|overnight ...]" >&2; exit 2; }
shift
modes=("$@"); [ ${#modes[@]} -eq 0 ] && modes=(smoke regression overnight)

rc=0
for m in "${modes[@]}"; do
  case "$m" in
    smoke)      flag="--smoke" ;;
    regression) flag="" ;;
    overnight)  flag="--overnight" ;;
    *) echo "unknown mode: $m (want smoke|regression|overnight)" >&2; exit 2 ;;
  esac
  echo ""
  echo "############################################################"
  echo "## regression_deck: $deck / $m"
  echo "############################################################"
  # shellcheck disable=SC2086
  bash "$HERE/regression.sh" $flag --deck="$deck" || rc=1
done

echo ""
if [ "$rc" -eq 0 ]; then
  echo "regression_deck($deck): all modes PASS. Accept per mode after inspecting:"
  for m in "${modes[@]}"; do
    case "$m" in smoke) f="--smoke";; overnight) f="--overnight";; *) f="";; esac
    echo "  bash test/regression.sh $f --deck=$deck --accept"
  done
else
  echo "regression_deck($deck): one or more modes FAILED -- inspect the per-mode audit above."
fi
exit "$rc"
