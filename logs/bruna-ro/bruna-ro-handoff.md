# bruna-ro handoff (7a/7b amendments + searched-focused A/B) -- for a Codex agent with no context

Worktree: /workspaces/MagicDeckTester/.claude/worktrees/bruna-ro, branch wip/bruna-rule-override (push ONLY this
branch; never phase-1-2-deck-analyzer). Read /workspaces/MagicDeckTester/CLAUDE.md first. Scratch:
/workspaces/MagicDeckTester/build/scratch/claude_bruna_ro/.

## USER rulings (verbatim)
- "7a. Okay, that makes sense along with the amendment. How does this do in searched?"
- "7b. That seems fine, except I'm more interested in the impact on searched than d0."
- (Earlier) Both levers break standing rules, so the USER decides adoption. Report numbers; do NOT adopt or flip a default.
- USER 12:35 (2026-11-22): "Claude usage is at 5%, so keep running until it runs out, but keep a Codex handoff current."

## Done
1. Rebased wip/bruna-rule-override onto origin/phase-1-2-deck-analyzer (94cd1e06b). One conflict (HeuristicArm.h slot
   lists, union-resolved). Levers stay default OFF.
2. Amendments (src/ai/ManaPayment.cpp `crit_demote` / `crit_excluded` / `crit_base_key`; flags in EngineFlags.h +
   HeuristicArm.h slots), both default OFF, measurement-only:
   - MTG_PAY_LINE_CRIT_UNTAPPER_LAST (7a-amended): a creature with `untap_target_land` (Voyaging Satyr) does not win a
     pip over a line-critical land the crit rule moved back.
   - MTG_PAY_LINE_CRIT_HASTE_SAC_LAST (7b-amended): a `sac_for_mana_grants_haste` pay-sac source (Generator Servant)
     paying a NONCREATURE cost (rider lost) does not win over such a land. General test = "the sac-mana carries a
     creature-spell rider and this payment is noncreature"; no sound general "line needs the enabler next turn" test.
   - Mechanism: during selection, track the best HISTORICAL key (crit_hold ignored) of any crit-held land; if the winner
     is an excluded source and that historical key beats it, rerun the pip's selection (goto dh_reselect) with excluded
     sources keyed {499 or 0, 1<<20, rank, 0} (behind the crit land's tier). Inert unless a crit lever AND an
     amendment are on. Byte-identical with amendments off.
   (commit hash: see `git log` -- "WIP crit amendments" commit)

## Running
- build1.sh (setsid; waits for claude_box.lock, then ./build.sh at full cores): log build/scratch/claude_bruna_ro/build1.log,
  DONE marker = line "DONE"; copies binaries to build/scratch/claude_bruna_ro/bin1/.
- rep1.sh: ro2 (old binary) repros in the lane: rep/j1447_{off,a}.* (Jumbo d0 seed 1311447 gi1447 T3),
  rep/n532_{off,b}.* (Niv d0 seed 1310532 gi532 T3). Output: build/scratch/claude_bruna_ro/rep/, logs in rep/<tag>.logs/.

## Next, in order
1. From the repro logs, get the T3 boards; write unit tests in test/unit/test_bruna.cpp next to
   "Bruna: line-critical land variants" (TEST_CASE pattern there: ArmPin + BoardBr + LineUnpaidCostScope +
   TapForCostShared): gi1447 board with 7a on: amendment ON keeps Satyr untapped / OFF reproduces; gi532 board with 7b:
   amendment ON does not sac Servant for Ponder / OFF reproduces. Add the two new slots to ArmPin.
2. Wait ~200 s after src/test edits (date +%s > build/scratch/claude_bruna_ro/edit_ts), rebuild under the lock, run units
   (build/Release/mtg-test -tc="Bruna*"), check repros on the new binary.
3. Probe: off arm, 25 games per searched shape, full cores under the lock, compare ms/game to the ro2 numbers.
4. A/B: `cd worktree && python3 build/scratch/claude_bruna_ro/mk_ab.py 1810000 1000 400 2000 <manifest>` (estimate:
   add --estimate; ~81 cpu-h, ~2.5 h wall at 32 cores). Run ONE batch under the lock:
   `exec 9>/workspaces/MagicDeckTester/build/scratch/claude_box.lock; flock 9; MTG_PGO=0 MTG_RSS_CAP_GB=40 <bin>/mtg --batch <manifest> --threads $(nproc) --game-log-dir <dir>/wins > <dir>/ab.out 2> <dir>/ab.err; echo rc=$? > <dir>/DONE`
   (cwd = worktree; binary copied out of build/ so later edits do not touch it).
   Compare with `python3 /workspaces/MagicDeckTester/build/scratch/bad/cmp_arms.py <dir>/wins <dir>/ab.out <arm> off`
   for arms a, aa, b, ba (and aa vs a, ba vs b). aa runs only on jumbo, ba only on niv/niv2hg in searched shapes
   (inert elsewhere by construction); all d0 shapes run all 5 arms.
5. Every slower game: replay at --depth 8 --budget-ms 0 in the lane (scripts/box_lane.sh --slots 1 --), escalate budget;
   unrecovered -> MTG_NC_SEARCH=1 clairvoyance check.
6. Doc section (docs/design/, e.g. bruna-line-critical-amendments.md) with the tables; commit + push wip/bruna-rule-override.
