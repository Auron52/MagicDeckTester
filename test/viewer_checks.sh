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
#   * deck maturity (frontend) -- viewer_deck_beta_check.js pins the alpha/beta/stable grading: a deck
#     missing a piece of its apparatus, or not yet green on 30+ references, must say so in the picker.
#     Silent when it rots (an unfinished deck simply reads "stable"), hence a gate.
#     Static, milliseconds, no binary.
#   * line-build (frontend) -- viewer_linebuild_check.js drives the REAL browser queue logic
#     (tools/play/linebuild.js): can the GUI still rebuild every line the user actually played?
#     Sub-second, needs node, no binary.
#   * client (frontend, in a DOM) -- viewer_client_check.js loads index.html's own script in jsdom
#     and plays games through the GUI's entry points. The only layer that RENDERS the decision
#     panels, so it is the only one that can see a panel that throws (a dead modal = a frozen
#     game). Needs node + jsdom + the binary; ~2 min.
#   * human line order (engine) -- human_line_order_check.py commits one line under several
#     --cast-order pins and asserts the engine applied the human's DECLARED sequence, casts and
#     board activations interleaved. Needs python3 + the binary; seconds. The only layer that can
#     see the realised order at all (see its header).
#   * manual tap/pay (engine) -- manual_tap_check.py commits one line with and without hand-forced
#     `tap=` tokens and asserts the human's allocation is performed exactly as declared, that no
#     pre-tap means the ENGINE still allocates, and that an impossible tap is rejected with a
#     reason. Needs python3 + the binary; seconds. Nothing else can see it: the fallback is
#     human-play-only and default-inert, and the float it makes is spent by the next payment (see
#     its header). SKIPS itself when the board it drives is not reachable.
#   * SAVE parity (server<->engine) -- viewer_save_parity_check.js drives real games through
#     server.js's own step entry point and then SAVES them, asserting the save's fresh full-stream
#     replay reproduces the live session decision-for-decision. The only layer that runs /api/save
#     at all: a save re-runs the whole choice stream in a NEW process, so a mid-session rebuild (or
#     a side channel that stops threading through) publishes a game nobody played, under the right
#     name, with no error -- which is exactly what happened on 2026-09-10. Needs node + the binary;
#     ~15 s.
#   * line macros (engine) -- viewer_line_macros_check.py asserts that a `need=<COLOURS>` declaration
#     (the colours the human's still-queued continuation wants) diverts exactly ONE ETB-untap pick,
#     does nothing when the demand is already served, and is a real off switch; plus that a fused
#     "Investigate & crack" really makes a Clue and that the deferred crack is reachable on the frame
#     the investigate produces. Needs python3 + the binary; seconds. Nothing else can see it: the
#     declaration is human-play-only and default-inert, and yield order and demand order usually
#     agree, so the end-of-turn board cannot tell a steered untap from a lucky one (see its header).
#     SKIPS itself when the board shape it drives is not reachable.
#   * pay line colour (engine) -- pay_line_color_check.py commits the user's seed-6 turn-4 line and
#     asserts its last segment's generic pips respect the LINE's remaining coloured demand: the
#     choice source taps the colour a later cast still owes, and the generic pip eats the surplus
#     colour. Both halves are human-play-only, so nothing else in the suite can witness them, and no
#     SAVED reference plays this line. Pins both `=0` hatches -- either defect alone reproduces the
#     bug. Needs python3 + the binary; ~3 s. SKIPS itself when the board is not reachable.
#   * protocol (engine<->GUI) -- viewer_protocol_check.py replays each reference's chosen plan
#     indices through the binary and asserts the decision-JSON contract holds (well-formed,
#     valid index, clean terminal). Needs python3 + the binary. FULL sweep ~35 min; --sample
#     runs one reference per deck (fast contract sanity across every archetype).
#     Runs --strict (2026-09-11): a reference that replays to a DIFFERENT outcome (play-drift) or
#     whose recorded plan the engine stopped offering on an identical hand (ENUM-GAP) FAILS this
#     script, exactly as test/regression.sh already runs it. A saved reference is the user's own
#     hand-played game and its win turn is invariant: the same clicks replaying to a different turn
#     means the ENGINE changed under them, never that the file needs re-saving. It was informational
#     before, and EDF claude_s9_gi8 sat at T4->T8 through several green runs of this script until
#     two agents found it by hand (BankableMana ignoring the floating pool, Session 15e).
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

# 0c) Deck maturity / alpha-beta-stable grading (node). Static, milliseconds, no binary. Every
#     failure mode here reads as GOOD news -- a rule that stops firing silently promotes an
#     unfinished deck to the top tier -- so it is a gate rather than an informational print. Also
#     prints the current split, which is the fastest way to see where each deck stands.
if command -v node >/dev/null 2>&1 && [ -f "$HERE/viewer_deck_beta_check.js" ]; then
  echo "--- viewer deck maturity (alpha / beta / stable) ---"
  if node "$HERE/viewer_deck_beta_check.js"; then :; else
    echo "FAIL: the viewer's alpha/beta/stable grading disagrees with the decks' actual artifacts."
    rc=1
  fi
fi

# 0b) DECK SELECTION check (node + jsdom). Sub-second, no binary. The deck list is two controls --
#     Deck holds one row per deck, archived lists live in their own Version select -- and the pair
#     they resolve to travels on EVERY request. Getting the pair wrong does not throw: it plays the
#     shipping list while the UI says otherwise. Deck-agnostic; skips when no deck has a variant.
if command -v node >/dev/null 2>&1 && [ -f "$HERE/viewer_deck_select_check.js" ]; then
  echo "--- viewer deck select (one row per deck; archived lists resolve) ---"
  if node "$HERE/viewer_deck_select_check.js"; then :; else
    echo "FAIL: the viewer's deck/version selection does not resolve to the right decklist."
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

# 1b) Headless CLIENT check (node + jsdom + binary): loads index.html's REAL script in a DOM and
#     plays games through the GUI's own entry points, so it is the ONLY layer that executes the
#     decision-PANEL renderers and the undo/history bookkeeping. Skipped in --line-only (it drives
#     the binary) and SKIPPED-not-failed when jsdom is absent (exit 2 = setup, 1 = a real break).
#     WIRED IN 2026-08-23. It was written months earlier and never run from here, and meanwhile
#     ce487708 removed index.html's #maxturns input while the check still wrote to it -- so it died
#     at setup on every invocation. Net effect: a `ReferenceError: aiPick is not defined` shipped in
#     lackeyPanelHtml (92c7ce07) and froze the viewer the moment Goblin Lackey connected, with every
#     other check green, because no other check renders a panel. Same lesson as layer 3 below: an
#     unrun check is not a check.
if [ "$MODE" != line ]; then
  if command -v node >/dev/null 2>&1 && [ -f "$HERE/viewer_client_check.js" ]; then
    if [ ! -f "$BIN" ]; then
      echo "FAIL: viewer client check needs the binary but '$BIN' is missing."
      rc=1
    else
      echo "--- viewer client check (index.html in jsdom: panels + undo bookkeeping) ---"
      MTG_BIN="$BIN" node "$HERE/viewer_client_check.js"
      crc=$?
      if   [ $crc -eq 0 ]; then :
      elif [ $crc -eq 2 ]; then echo "SKIP: viewer client check (jsdom not installed -- 'npm i' in test/)."
      else
        echo "FAIL: viewer client check (a decision panel cannot render, or undo corrupts history)."
        rc=1
      fi
    fi
  fi
fi

# 1c) HUMAN LINE ORDER (python + binary). ~10 short claude-play invocations, a few seconds.
#     The realised order of a committed line is invisible to every other layer here -- the protocol
#     check replays plan INDICES and compares win turns, the client check sees the GUI's
#     bookkeeping but never the apply, and the regression suite covers autonomous play, which this
#     feature is gated out of by construction. So the pin losing its marker, a kind falling out of
#     TurnSolver::IsTrailingActivation, or the trailing pass running twice would be silent
#     everywhere while the player's declared order quietly reverted to enumerator order.
#     SKIPS itself (exit 0) when the board shape it drives is not reachable.
if [ "$MODE" != line ]; then
  if command -v python3 >/dev/null 2>&1 && [ -f "$HERE/human_line_order_check.py" ]; then
    if [ ! -f "$BIN" ]; then
      echo "FAIL: human line order check needs the binary but '$BIN' is missing."
      rc=1
    else
      echo "--- human line order (the queued sequence is applied as-is) ---"
      if MTG_BIN="$BIN" python3 "$HERE/human_line_order_check.py"; then :; else
        echo "FAIL: a committed line no longer applies in the order the human declared."
        rc=1
      fi
    fi
  fi
fi

# 1d) MANUAL TAP/PAY (python + binary). ~15 short claude-play invocations, a few seconds.
#     The viewer's hand-tap fallback (docs/design/viewer-manual-tap-pay.md) is human-play-only and
#     inert unless a line carries `tap=` tokens, so GT, the scenarios and the reference sweep all
#     stay green whatever it does -- and the float a forced tap makes is spent by the very next
#     payment, so the end-of-turn board cannot tell a human's tap from the allocator's. The failure
#     that matters is SILENT REPAIR (the engine quietly re-allocating around an awkward forced tap),
#     which is invisible to every outcome-based test there is.
#     SKIPS itself (exit 0) when the board shape it drives is not reachable.
if [ "$MODE" != line ]; then
  if command -v python3 >/dev/null 2>&1 && [ -f "$HERE/manual_tap_check.py" ]; then
    if [ ! -f "$BIN" ]; then
      echo "FAIL: manual tap/pay check needs the binary but '$BIN' is missing."
      rc=1
    else
      echo "--- manual tap/pay (the human's taps are honoured, and only the human's) ---"
      if MTG_BIN="$BIN" python3 "$HERE/manual_tap_check.py"; then :; else
        echo "FAIL: a hand-forced mana tap is no longer performed as declared (or is no longer rejected with a reason)."
        rc=1
      fi
    fi
  fi
fi

# 1e) SAVE PARITY (node + binary). ~15 s. The save path is the one seam nothing else here touches:
#     every other layer either steps the game or reads a file that already exists, while /api/save
#     RE-RUNS the entire accumulated choice stream in a fresh process and publishes the result as the
#     game the human played. On 2026-09-10 that re-run picked up a binary rebuilt two minutes earlier,
#     veered at decision 27 of a turn-4 go-off, and wrote a won-on-turn-7 log for a game still in
#     turn 4 -- no error, right filename. This check drives real sessions and saves them, and would
#     have caught it twice over (out-of-range pick at 27, board mismatch at 28).
if [ "$MODE" != line ]; then
  if command -v node >/dev/null 2>&1 && [ -f "$HERE/viewer_save_parity_check.js" ]; then
    if [ ! -f "$BIN" ]; then
      echo "FAIL: save parity check needs the binary but '$BIN' is missing."
      rc=1
    else
      echo "--- viewer save parity (a saved log IS the game that was played) ---"
      if MTG_BIN="$BIN" node "$HERE/viewer_save_parity_check.js"; then :; else
        echo "FAIL: a saved game log does not reproduce the session it was saved from."
        rc=1
      fi
    fi
  fi
fi

# 1f) LINE MACROS (python + binary). ~15 short claude-play invocations, a few seconds.
#     The "repeat xN" macro and the fused "Investigate & crack" are deliberately pure QUEUE edits --
#     they expand into ordinary segments and commit through the existing chain -- so the frontend
#     half is covered by viewer_linebuild_check.js above. What lives HERE is the one piece that had
#     to be new engine behaviour: `need=<COLOURS>` steering an ETB-untap pick toward the colours the
#     human's REMAINING queue wants. That is human-play-only and default-inert, so GT, the scenarios
#     and the reference sweep all stay green whatever it does -- and the yield order and the demand
#     order agree most of the time, so the board alone cannot tell a diverted pick from a lucky one.
#     The failure that matters is SILENT REVERSION (the promotion quietly not firing), which no
#     outcome-based test can see. SKIPS itself (exit 0) when the board it drives is not reachable.
if [ "$MODE" != line ]; then
  if command -v python3 >/dev/null 2>&1 && [ -f "$HERE/viewer_line_macros_check.py" ]; then
    if [ ! -f "$BIN" ]; then
      echo "FAIL: line macros check needs the binary but '$BIN' is missing."
      rc=1
    else
      echo "--- line macros (a queued continuation steers the untap; a fused gesture's halves land) ---"
      if MTG_BIN="$BIN" python3 "$HERE/viewer_line_macros_check.py"; then :; else
        echo "FAIL: a declared queued continuation no longer steers the untap pick (or a fused half no longer lands)."
        rc=1
      fi
    fi
  fi
fi

#   * pay line colour (engine) -- pay_line_color_check.py commits the user's seed-6 turn-4 line and
#     asserts that the last segment's generic pips respect the LINE's own remaining coloured demand:
#     a choice source taps for the colour a later cast still owes, and a generic pip eats the colour
#     most in surplus. Both halves are HumanPlayActive-gated and default-inert to every other layer,
#     so GT, the scenarios and the autonomous reference digests stay green whatever they do -- and no
#     SAVED reference plays this line (the user hand-repaired it with `tap=` tokens, which is exactly
#     what a reference would then not contain). The witnesses are a life total, a tapped bit and
#     which colour survived, so those are asserted, not the outcome. Both `=0` hatches are pinned
#     because EITHER defect alone reproduces the bug. Needs python3 + the binary; ~3 s.
#     SKIPS itself (exit 0) when the board it drives is not reachable.
if [ "$MODE" != line ]; then
  if command -v python3 >/dev/null 2>&1 && [ -f "$HERE/pay_line_color_check.py" ]; then
    if [ ! -f "$BIN" ]; then
      echo "FAIL: pay line colour check needs the binary but '$BIN' is missing."
      rc=1
    else
      echo "--- pay line colour (a line's later cast keeps the colour it still owes) ---"
      if MTG_BIN="$BIN" python3 "$HERE/pay_line_color_check.py"; then :; else
        echo "FAIL: a committed line's generic pips again spent the mana a later cast in that same line needed."
        rc=1
      fi
    fi
  fi
fi

# 2) Engine<->GUI protocol contract (python + binary). Skipped in --line-only.
if [ "$MODE" != line ]; then
  if command -v python3 >/dev/null 2>&1 && [ -f "$HERE/viewer_protocol_check.py" ]; then
    if [ ! -f "$BIN" ]; then
      echo "FAIL: protocol check needs the binary but '$BIN' is missing (build Release first, or set MTG_BIN)."
      rc=1
    else
      PROTO_ARGS="--strict"; [ "$MODE" = sample ] && PROTO_ARGS="--strict --sample"
      echo "--- viewer protocol check (engine<->GUI contract, $PROTO_ARGS) ---"
      if MTG_BIN="$BIN" python3 "$HERE/viewer_protocol_check.py" $PROTO_ARGS; then :; else
        echo "FAIL: viewer protocol check reported a CONTRACT failure (malformed/invalid decision)"
        echo "      or, under --strict, a PLAY-DRIFT / ENUM-GAP on a saved reference (the engine"
        echo "      replays a user's recorded clicks to a different outcome -- an engine defect)."
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
