#!/bin/bash
# Viewer sanity checks for the play GUI (tools/play) -- split OUT of the regression suite.
#
# These guard the engine<->GUI contract and the frontend line-rebuild logic. They are
# deck-agnostic and change INFREQUENTLY (only when tools/play/, the decision-JSON emitter,
# or the saved reference set changes), and the protocol layer is MULTI-MINUTE (~35 min full)
# because it re-invokes the binary once per replayed step. That cost + rarity makes them a
# poor fit for the per-commit smoke/regression flow (they dominated its budget and, worse,
# their binary-driven protocol replay could HANG the whole suite before the deck batch ran).
# So they live here and run ON DEMAND -- after touching the viewer, the emitter, or a
# reference -- not on every regression.
#
# Two layers (either can fail the run; a CONTRACT break exits non-zero so this can gate CI):
#   * decision-type coverage (frontend) -- viewer_decision_types_check.js pins the viewer's
#     SUBDECISIONS whitelist against every decision type src/main.cpp can emit. A type missing
#     there hides the panel outright, so the decision is unanswerable and the game stalls on a
#     dead board -- how lackey_put/echo/land_entry each shipped inert. Static, milliseconds.
#   * line-build (frontend) -- viewer_linebuild_check.js drives the REAL browser queue logic
#     (tools/play/linebuild.js): can the GUI still rebuild every line the user actually played?
#     Sub-second, needs node, no binary.
#   * protocol (engine<->GUI) -- viewer_protocol_check.py replays each reference's chosen plan
#     indices through the binary and asserts the decision-JSON contract holds (well-formed,
#     valid index, clean terminal). Needs python3 + the binary. FULL sweep ~35 min; --sample
#     runs one reference per deck (fast contract sanity across every archetype).
#     Behaviour drift (won/win_turn changed) is INFORMATIONAL -- re-save the reference when
#     satisfied; only a CONTRACT break (exit 1) fails this script.
#
# Usage (from repo root, after building Release):
#   bash test/viewer_checks.sh               # full: line-build + protocol (all refs, ~35 min)
#   bash test/viewer_checks.sh --sample      # line-build + protocol SAMPLE (one ref/deck, fast)
#   bash test/viewer_checks.sh --line-only   # frontend line-build only (sub-second, no binary)
#   MTG_BIN=<path> bash test/viewer_checks.sh   # override the binary the protocol check drives
set -u

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BIN="${MTG_BIN:-}"
if [ -z "$BIN" ]; then BIN=./build/Release/mtg.exe; [ -f "$BIN" ] || BIN=./build/Release/mtg; fi

MODE=full
for a in "$@"; do
  case "$a" in
    --sample)    MODE=sample ;;
    --line-only) MODE=line ;;
    -h|--help)   sed -n '2,26p' "$0"; exit 0 ;;
    *) echo "unknown arg: $a (see --help)" >&2; exit 2 ;;
  esac
done

rc=0

# 0) Decision-type coverage (node). Static, milliseconds, no binary -- run first so a
#    whole decision type being unanswerable is reported before the slower layers.
if command -v node >/dev/null 2>&1 && [ -f "$HERE/viewer_decision_types_check.js" ]; then
  echo "--- viewer decision-type coverage (frontend) ---"
  if node "$HERE/viewer_decision_types_check.js"; then :; else
    echo "FAIL: an engine decision type is missing from the viewer's SUBDECISIONS whitelist."
    rc=1
  fi
fi

# 0b) Reveal/log parity (node). Static, milliseconds -- pins "what the viewer history shows, the
#     log records". Broken once in each direction (see the check's header), hence a gate.
if command -v node >/dev/null 2>&1 && [ -f "$HERE/reveal_log_parity_check.js" ]; then
  echo "--- reveal/log parity (engine) ---"
  if node "$HERE/reveal_log_parity_check.js"; then :; else
    echo "FAIL: a reveal can reach the viewer without reaching the saved game log."
    rc=1
  fi
fi

# 1) Frontend line-build check (node). Sub-second, no binary.
if command -v node >/dev/null 2>&1 && [ -f "$HERE/viewer_linebuild_check.js" ]; then
  echo "--- viewer line-build check (frontend) ---"
  if node "$HERE/viewer_linebuild_check.js"; then :; else
    echo "FAIL: viewer line-build check (the GUI cannot rebuild a played reference line)."
    rc=1
  fi
elif ! command -v node >/dev/null 2>&1; then
  echo "SKIP: viewer line-build check (node not found)."
fi

# 2) Engine<->GUI protocol contract (python + binary). Skipped in --line-only.
if [ "$MODE" != line ]; then
  if command -v python3 >/dev/null 2>&1 && [ -f "$HERE/viewer_protocol_check.py" ]; then
    if [ ! -f "$BIN" ]; then
      echo "FAIL: protocol check needs the binary but '$BIN' is missing (build Release first, or set MTG_BIN)."
      rc=1
    else
      PROTO_ARGS=""; [ "$MODE" = sample ] && PROTO_ARGS="--sample"
      echo "--- viewer protocol check (engine<->GUI contract${PROTO_ARGS:+, $PROTO_ARGS}) ---"
      if MTG_BIN="$BIN" python3 "$HERE/viewer_protocol_check.py" $PROTO_ARGS; then :; else
        echo "FAIL: viewer protocol check reported a CONTRACT failure (malformed/invalid decision)."
        rc=1
      fi
    fi
  elif ! command -v python3 >/dev/null 2>&1; then
    echo "SKIP: viewer protocol check (python3 not found)."
  fi
fi

# 3) CheckLine / --validate-line (node + python + binary). Skipped in --line-only and --sample:
#    it validates EVERY main-phase line of EVERY reference, so it is the slowest layer (~5 min).
#    WIRED IN 2026-08-08 -- it existed for weeks outside this script and nobody ran it, so it
#    quietly accumulated 141 stale failures (docs/design/viewer-validate-stream-alignment.md).
#    An unrun check is not a check; keep it in the suite.
if [ "$MODE" = full ]; then
  if command -v node >/dev/null 2>&1 && command -v python3 >/dev/null 2>&1 \
     && [ -f "$HERE/viewer_validate_check.js" ]; then
    if [ ! -f "$BIN" ]; then
      echo "FAIL: validate-line check needs the binary but '$BIN' is missing."
      rc=1
    else
      echo "--- viewer validate-line check (engine CheckLine) ---"
      if MTG_BIN="$BIN" node "$HERE/viewer_validate_check.js"; then :; else
        echo "FAIL: a line a human actually played no longer validates (CheckLine regression)."
        rc=1
      fi
    fi
  fi
fi

if [ $rc -eq 0 ]; then echo "viewer checks: PASS"; else echo "viewer checks: FAIL"; fi
exit $rc
