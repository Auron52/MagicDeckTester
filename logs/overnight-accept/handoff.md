# Handoff: overnight accept 2026-11-20 attribution (agent worktree agent-a1088baf277f40b58)

Branch `wip/overnight-accept-1120` (based on `c86a86333`). Draft doc:
`docs/design/overnight-accept-2026-11-20.md`. It still contains placeholders BISECT_RESULT, COUNTS_NOTE and
ACCEPT_NOTE, and the "~7 pending" row in its table needs updating.
Scripts are in `logs/overnight-accept/` (force-added). All scratch is under
`/workspaces/MagicDeckTester/build/scratch/claude_ovn1120/agent/` (= `$A` below).

## Rulings from the brief (verbatim, binding)
- "DO NOT run `--accept` and do NOT modify anything in /workspaces/MagicDeckTester (the primary) -- the parent accepts there. Do NOT push phase-1-2-deck-analyzer."
- "a big pooled batch (more than ~4 cores) runs under `flock /workspaces/MagicDeckTester/build/scratch/claude_box.lock <cmd>`; small work through `scripts/box_lane.sh --slots N [--cap-gb G] -- <cmd>` (max 4 slots; --threads <= N). Use MTG_RSS_CAP_GB=40 for box-lock runs."
- Coordinator: never run two 32-thread processes at once. Pool both arms into ONE batch, or run them serially.
- "Never `timeout`; never `pkill -f`/`pgrep -f`; waiters on PID/DONE via run_in_background."
- Hand off when context passes ~250K tokens; never exceed 400K.

## DONE (findings are in the draft doc)
- Old binary built at `1443eeba0`: `/workspaces/MagicDeckTester/build/wt/ovn1120-old/Release/mtg`. Run it with
  cwd `/workspaces/MagicDeckTester/.claude/worktrees/ovn1120-old` (its old cards.json and decks). The old arm
  reproduces the old GT turn on every game checked.
- Gods: the USER strict-dredge rule (`c39c1da1a` lever, adopted `6cb9a7c85`). `MTG_GODS_RETRIEVAL_DREDGE=0`
  restores 27/27 at case settings, and 25 of 27 at d8 b0 (the rest go faster). Not a regression.
- Hinata: the depth-5 restore `da17c0bbf`. The new binary with the old `Hinata.value.json` reproduces the old
  GT and is 0.01-0.04 faster. The d5 setting costs +0.06 to +0.11 per game (2HG +0.02 to +0.14). USER-owned.
  2 d8 b0-unrecoverable games are unattributed (s5005 gi384, s6006 gi156).
- Cyclone: 51/51 finished PERSISTS recover at d8 b0. Rows with Ponder-first off are -8 turn-games vs new.
  This is churn.
- Midrange Burn: the opponent-lands model `c9022b11d` (Price of Progress vs real nonbasics). The
  `legacy_passive_opponent_lands` job field restores 25/25.
- Spirits: the keep table `a05ac4395` (KEPT-DIFF 10/10). The artifact swap reproduces the old GT.
- REAL: PD s6006 gi47 T8 -> unwon (also at d8 b0) is caused by `MTG_PD_LATER_HAND_HOLD` (`636910330`). The new
  line skips the T4/T5 land drops with lands in hand. The flag alone =0 restores it.
- Five Colour d0: T1 plays the dual/basic and holds the fetch (gi192: Deathrite left uncastable). Not the
  opponent lands, not HELD_FETCH_CRACK_FIRST, GREEDY_SELF_HASTE, GREEDY_TUTOR_TARGET_TIE or
  UNSEEN_KEEP_SCARCE. The bisect is running (below).

## RUNNING JOBS
1. **Five Colour d0 bisect.** `bisect_fc.sh`, PID 3971800 (child 3974930). Log: `$A/bisect/log`; DONE marker:
   `$A/bisect/DONE`. **It runs IN THIS WORKTREE:** each step does
   `git restore --source=<commit> --worktree -- src test/unit CMakeLists.txt cmake`, waits 240 s for the 9p
   clock lag, then builds `--target mtg`. At the end it restores those paths to HEAD.
   - **Do NOT commit or build in this worktree until `$A/bisect/DONE` exists**, then check
     `git status --short` is clean apart from logs/.
   - Result line: `FIRST BAD: <hash>`. Step 1 (`dbba1cd54`, index 159) was bad, so the culprit is in the first
     160 src commits after 1443eeba0. About 8 steps of ~10 min each.
   - "good" = s4004 gi67 AND gi192 both win T5 (old); new = T7.
2. **d8 b0 two-arm run.** `run_d8b0.sh` under flock (holds the box lock), PIDs: new arm 3960634, old arm 3960635.
   Coordinator's resumer 3962202 is harmless. DONE markers: `$A/new/DONE`, `$A/old/DONE`, `$A/ALL_DONE`.
   Pending: new arm cyclone d3 s6006 gi234, gods/gods2hg d5 s4004 gi3, hinata d5 s6006 gi208; old arm
   hinata gi208.

## REMAINING (in order)
1. When the d8 b0 run finishes, re-run `python3 logs/overnight-accept/classify_d8b0.py` (writes
   `$A/d8b0.classified`) and `python3 logs/overnight-accept/trdiff.py` (kept-hand / draw split from the
   case-setting traces). Update the classification table. Ignore the 6 `fivecolour` rows in d8b0.idx (they are
   d0 trace rows, no d8 b0 job).
2. When the bisect finishes, put the commit into BISECT_RESULT (2 places) with its subject/mechanism.
   Optional: test whether the same commit explains the searched T1 fetch-hold games (Antilife s6006 gi117,
   Minotaur s6006 gi937, Creature Giving x20). Build the culprit and its parent, then compare them on those
   case jobs (`mk_flip.py <out> case creature antilife minotaur`).
3. Fill in COUNTS_NOTE and ACCEPT_NOTE (a draft accept note is below) and commit.
4. Push the branch: `git push origin wip/overnight-accept-1120`.

## Reading results
- `cmpflip.py <dir>`: per deck/kind, restored (= old GT turn) / still-new / other.
- `rowcmp.py <dir>`: row means: old GT, new run, variant, plus same-as-old counts.
- `show.py <stem> <gi> [T]`: the per-turn old/new case trace.
- `runnew.sh <manifest-stem> <threads> [VAR=VAL..]` runs the NEW binary with `--cards-json $A/cards_c86.json`.
  Use that file while the bisect is changing this worktree's src.

## Draft accept note
Periodic overnight 2026-11-20 (since 1443eeba0, 320 src commits). Every searched PERSISTS game was replayed on
both arms at d8 b0, and every slower one is attributed:
- Gods 27: USER strict dredge MTG_GODS_RETRIEVAL_DREDGE (c39c1da1a/6cb9a7c85; blind -0.13). Lever=0 restores all.
- Hinata: d5 floor restore da17c0bbf. New binary + old value.json = old GT; the d5 setting is +0.08/game at b30.
- Cyclone 53: all recover at d8 b0 (Ponder-first 7b95d828b and churn).
- Midrange Burn: USER opponent-lands model c9022b11d (Price of Progress); legacy lands restores 25/25.
- Spirits: keep table a05ac4395.
- Five Colour d0 +0.005 and the searched Antilife/Minotaur/Creature Giving T1 fetch-hold: <bisect commit>.
- Owed fix: PD s6006 gi47 T8 -> unwon, from MTG_PD_LATER_HAND_HOLD (636910330) skipping land drops.
- Unattributed but d8 b0-stable line changes in net-faster decks: Hinata 2, Galadriel 5.
See docs/design/overnight-accept-2026-11-20.md.

## UPDATE (bisect stopped)
The bisect STOPPED without a final answer. `FIRST BAD: d6a7ce1a6` in its log is only the current upper
bound. Index 59 (`fb3517beb`) is good and index 79 (`d6a7ce1a6`) is bad, so the first bad commit is
index 60..79 of `git rev-list --reverse 1443eeba0..c86a86333 -- src`. Index 69 (`9547ea31b`, "Add reviewed
Boars heuristic candidates and Stromkirk continuation") does NOT BUILD: TurnSolver.cpp uses
AppendHeldFetchCrackFirstPlans, HeldFetchCrackFirstOn and CrackPutsUntappedLand before they are declared,
which means the held-fetch work landed across several commits there.
Next step: test index 68 or 70 (skip 69), using the loop in bisect_fc.sh with lo=59 hi=79. Prime suspects
are the held-fetch / Boars fetch commits in that window. Note that MTG_HELD_FETCH_CRACK_FIRST=0 alone did
not restore the d0 games, so also try MTG_BOARS_FETCH_PIPS / MTG_BOARS_FETCH_PRUNE / other levers added
there.
