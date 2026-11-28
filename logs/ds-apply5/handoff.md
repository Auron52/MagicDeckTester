# Dragonstorm APPLY-DROP round 5 (ds-apply5) -- handoff 1 (2026-11-28)

Worktree `/workspaces/MagicDeckTester/.claude/worktrees/ds-apply5`, branch `wip/ds-apply5` (off origin/phase-1-2-deck-analyzer
524d7c303, src tree 7a0919815369). Push ONLY `wip/ds-apply5`; never the shared branch (the parent lands it with GT).
Scratch `S=/workspaces/MagicDeckTester/build/scratch/ds-apply5/`. Never touch references/.

## Binaries
- `$S/bin_base/mtg` = origin src (copied from build/wt/land-1130/Release, built from 0311a6c44 = same src tree).
- `$S/bin_dbg/mtg` = origin + the MTG_DS2_PAYDBG print snippet only (logs/ds-apply4/dbg.snippet; play identical).
- Worktree build (`build/` -> build/wt/ds-apply5) is being rebuilt with the new lever (see Running).

## Census (step 1) -- done on events, gate slice still running
- Round-4 gate `on` arm (build/scratch/ds-apply4/gate_out, play-identical to origin modulo cut 2): need-later Karrthus
  274 flags on 30 (seed, turn) events; `logs/ds-apply5/census.py` -> `census_r4on.txt`. Shapes (plan Apex count +
  Territory/Bloom on board): apex1+terr+bloom 10 ev / 100 flags; apex1+bloom 10 / 85; apex0+terr+bloom 5 / 32;
  apex2+terr+bloom 2 / 24; apex2+bloom 2 / 24; apex0+bloom 1 / 9. Almost every flagged plan ENDS at Apex of Power
  (a breakpoint) with Karrthus in hand; Karrthus is cast by the continuation and its payment fails.
- Fresh repro of those 30 events on the CURRENT origin binary (`logs/ds-apply5/evrun.sh`, oracle on the event turn):
  `census_base.txt`: 169 need-later Karrthus flags on 19 events (11 events now 0: oracle changes eb39bc311 /
  play moved). Win turn > event turn (candidate lost turn) in 5: s30092745 T6->7, s30095186 T6->7, s30101776 T6->7,
  s30113323 T5->6, s30129223 T4->5.
- A fresh gate slice on the base binary is RUNNING (it doubles as the gate's OFF arm; see Running).

## Mechanisms (step 2) -- traced (MTG_DS2_PAYDBG + MTG_TAPDBG, `$S/rep_dbg_<seed>.err`, `logs/ds-apply5/dropblk.py
<err>` prints the engine-apply block behind a flag). All five turn-losers fail at the payment of the OPENER (Apex of
Power, the last cast of the named line; `unseen=1`, hold == Apex's cost), which reads no later demand:
- **D (float drain):** s30095186 T6, s30129223 T4: an earlier payment (the visible-closer lump rule) cracked Lotus
  Bloom for {B} and {B} floats; Apex's {7}{R}{R}{R} spends that {B} on generic while red is left floating
  (s30095186: before B2 R5 -> after B0 R3). Karrthus then has the other Bloom for one of {B}{G} only.
- **T (creature-only land on generic):** s30092745 T6, s30113323 T5, s30087571 T6: Apex's generic taps Unclaimed
  Territory for {C} (s30087571 with a Mountain left UNTAPPED); Karrthus then has the Bloom for one of {B}{G} and no
  source for the other. The oracle's payment cracks the Bloom for {B} at Apex and floats one {B} (or taps the Mountain).
- Not yet traced: s30101776 (two-Apex plan + single-Apex plans, Territory), s30145787 (42 flags, capped), the rest.
- Verdict so far: payment choices (which source pays / which float unit pays), not oracle false positives, not search.

## Fix (step 3) -- WRITTEN, COMPILES, committed as WIP on wip/ds-apply5, NOT yet measured
`MTG_VISIBLE_CLOSER_KEEP` (heurarm VISIBLE_CLOSER_KEEP at the enum tail + Name table; reader VisibleCloserKeepOn in
src/ai/EngineFlags.h; DEFAULT ON). `VisibleCloserKeepPay` in src/ai/ManaPayment.cpp just above TapForCostShared,
gated at the top of TapForCostShared (outermost, not refining / oracle float-only / hook target / human; own busy flag
t_visible_closer_busy). Applies only when g_line_opens_unseen and the line hold == this cost (the opener ends the
named line), and exactly one creature card in hand has a colour this cost lacks (the visible-contrib "sole closer").
Plain payment (in place, the KeepOwedFloatPay route) judged by LumpAwareBillPayableOpener against the closer's
EffectiveSpellCost; only if it strands the closer, alternatives in order: (1) closer-colour float set aside, (2) +
creature-only lands held (PayHoldScope), (3) each lump cracked in each closer colour with its units set aside, without
/ with the hold. First one that pays and has a witness replaces the plain payment. Trace: MTG_VISIBLE_CLOSER_TRACE=1.
The DS2-TEMP-DEBUG snippet is STILL in ManaPayment.cpp (after the SwapLeftoverForLine call) -- REMOVE before landing.
Exactness note for the report: it replaces only a payment that provably strands the visible closer by one with a
witness (the VISIBLE_LUMP_EXACT standard: the closer counts as later demand); it cannot prove the new board strands no
unseen continuation the old one kept -- say so.

## Remaining, in order
1. Build (running) -> copy to `$S/bin_l5/`; run `logs/ds-apply5/evrun.sh <bin> on $S/events.txt` and
   `... off $S/events.txt MTG_VISIBLE_CLOSER_KEEP=0` (box_lane --slots 2 --cap-gb 3); compare flags + win turns
   (adapt logs/ds-apply4/evcmp.py: Karrthus need-later = 'not declared' and last dropped cast Karrthus). `=0` logs must
   equal `$S/rep_base_*` game logs (byte-identity; diff the json minus runId). Use MTG_VISIBLE_CLOSER_TRACE=1 to see
   which alternative fired. Trace s30101776 / s30145787 if still flagged.
2. Unit tests in test/unit/test_dragonstorm_line_keep.cpp (model: the s30120119 / s30093026 cases): s30095186 Apex
   board (float B2 R5, Bloom#21 + Bazaar#22 + Needle#49 untapped, Karrthus in hand; on: Karrthus payable after Apex's
   ten, off: not) and s30113323 / s30087571 Territory board. Header comment line for the lever.
3. Gate ON arm: `python3 logs/ds-apply5/mk_gate.py build/scratch/ds-apply4/gate.json $S/gate_on.json on` then
   `bash scripts/box_lane.sh --slots 2 --cap-gb 3 -- bash logs/ds-apply5/run_gate.sh $S/bin_l5/mtg $S/gate_on.json
   $S/gate_on 2` AFTER the base arm finishes (~12 h each at 2 threads). Compare with logs/ds-apply4/attrib.py-style
   counts (arm names base / on) + census.py.
4. If play moves: all-deck held-out A/B (logs/ds-apply4/mk_ab.py --n 400 --hg 200 --wf 3000 --base 5410000, lever
   pinned off/on; ab_report.py), in the lane at 2 slots (long). Slower games -> d8 b0 replay both arms.
5. CPU: perf stat -e instructions:u, DS 200 games + 45-deck set, lever on vs off (logs/ds-apply4/cpu_cut.txt method).
6. Remove the DS2 debug snippet; build mtg-test once (./build.sh release -- --target mtg-test, lane 2 slots, cap 4);
   full units with this worktree's scripts/unit_par.sh.
7. Round 5 section in docs/design/dragonstorm-apply-drop-2026-11-26.md, logs/ds-apply5/report.md (git add -f),
   commit, `python3 scripts/check_references_safe.py`, push wip/ds-apply5.

## Running jobs
- Base gate arm (fresh census slice + OFF arm): PID 2255418 (box_lane wrapper; batch is its child), manifest
  `$S/gate_base.json` (362 jobs, 72,000 games, Dragonstorm gi 87000-158999, seed 30000000+gi, MTG_MANA_ORACLE=2,
  2 threads), outputs `$S/gate_base/{batch.out,flags.err}`, DONE marker `$S/gate_base/DONE`. Started 01:36 UTC
  2026-11-28; ~12 h expected. Waiter: `while kill -0 2255418; do sleep 60; done` or the DONE file.
- Lever build: DONE, compiled clean (exit 0). Binary copied to `$S/bin_l5/mtg` (lever + the DS2 debug snippet; built
  from the WIP commit below). Step 1 of Remaining starts from it.

## USER rulings (verbatim from the brief)
- Dragonstorm "spend the counters if they are required for the line".
- Adoption bar: the average at play settings; adopt if many decks gain, mitigate the few worse; ignore seed noise and
  clairvoyant losses; d0 barely counts.
- "cut CPU" -- suite headroom never justifies CPU.
- A sound collapse defaults ON; a lossy narrowing needs its A/B; everything findable at unlimited budget.
- NO GREEDY PICK INSIDE THE SEARCH WINDOW (heuristics only in a DecisionProvider, only to restrict/order).
- USER-owned decisions are surfaced, never settled.
- HARD RULES: never push phase-1-2-deck-analyzer; no GT accept; no smoke/regression tiers; do NOT take
  claude_box.lock (a mana_clean gate owns the big lane); every run via `bash scripts/box_lane.sh --slots N [--cap-gb G]
  -- <cmd>` with N <= 2 (--cap-gb 3 engine batches, 4 builds); ./build.sh only; wait ~200 s after edits on 9p before
  building; never `timeout`; waiters on PID / DONE, never pgrep -f; scratch under build/scratch/ds-apply5/;
  context handoff past ~250K.

## Measurements so far
- census_r4on.txt (274 flags / 30 events), census_base.txt (169 flags / 19 events on origin; 5 lose a turn).
- No lever measurement yet.
