# Dragonstorm APPLY-DROP round 5 (ds-apply5) -- handoff 2 (2026-11-28 ~14:10 UTC)

(Handoff 1 is in git at 23b13dd8c.) Worktree `/workspaces/MagicDeckTester/.claude/worktrees/ds-apply5`, branch
`wip/ds-apply5` (off origin/phase-1-2-deck-analyzer 524d7c303). Push ONLY `wip/ds-apply5`; the parent lands the shared
branch with GT. Scratch `S=/workspaces/MagicDeckTester/build/scratch/ds-apply5/`. Never touch references/.

## Commits (wip/ds-apply5)
- 23b13dd8c WIP: MTG_VISIBLE_CLOSER_KEEP first version + census + traces + handoff 1.
- 2c510e524 WIP: the lever's judge credits Apex's ten in RED only for a red-only provider; exact pre-check; dominating
  closer; unit tests (4 new cases in test/unit/test_dragonstorm_line_keep.cpp).
- 513976445 WIP: DS2-TEMP-DEBUG and DS5-TEMP-AUDIT code REMOVED (src is final unless units fail); A/B + CPU apparatus.
- (this handoff commit) results files: repro_off_on2/on3.txt, gate_attrib.txt, ab_report.txt, attrib.py.

## The lever as it stands (src/ai/ManaPayment.cpp VisibleCloserKeepPay, top of TapForCostShared; EngineFlags.h
VisibleCloserKeepOn, DEFAULT ON; heurarm VISIBLE_CLOSER_KEEP)
Gate: g_line_opens_unseen, the line hold == this cost (the opener ENDS the named line: Apex of Power), generic > 0.
Closer = the creature in hand whose cost has a colour this cost lacks; with several, the one whose effective bill
DOMINATES all others (each colour and {C} >=, MV >=, no hybrid/X/phyrexian/twobrid), else no gate. Witness = floating
covers the bill, or LumpAwareBillPayable with the opener's ten credited in RED only when ImpulseFloatColorRedOnly (and
not MTG_UNPRUNED(SacColor)), else LumpAwareBillPayableOpener. Exact pre-check: no witness on the board BEFORE the
payment -> return -1 (plain payment, no copy). Plain payment in place; only if it strands the closer, alternatives
(closer-colour float set aside / creature-only lands held / each lump cracked in each closer colour, +-hold), first with
a witness replaces it. MTG_VISIBLE_CLOSER_TRACE=1 prints the alternative taken or `alt=none`.
Finding for the write-up/USER: LumpAwareBillPayableOpener (used by the round-3/4 rungs too) credits Apex's ten in EVERY
bill pip colour, but Dragonstorm only enumerates a RED Apex (TurnSolver Apex colour variants, ImpulseFloatColorRedOnly),
so that judge is optimistic for Dragonstorm (it called s30129223's stranding payment fine). Only THIS lever was changed;
fixing the shared judge would move round-3/4 play and needs its own A/B.

## Measurements (all done)
1. Event repros (30 events, oracle on the event turn, `logs/ds-apply5/evrun.sh`): with `MTG_VISIBLE_CLOSER_KEEP=0` all 30
   game logs equal the base (origin) binary's. Need-later Karrthus flags OFF 126 -> ON 63 (final lever, on3;
   `logs/ds-apply5/repro_off_on3.txt`); first version 92, red-only witness 66, + dominance 63. Win turns: s30101776 T7->T6,
   s30145787 T6->T5, none slower. s30134414 / s30145787 / s30149431 carry CAPPED-DECISION from the oracle's 1300
   thread-CPU-s backstop: their flag counts move with box load (base vs off differ there with identical game logs), so
   they are noise. Pre-check exactness audit (temporary MTG_VC_PRECHECK_AUDIT build): 0 PRECHECK-MISS, 30/30 game logs
   identical with and without the pre-check.
2. Gate (Dragonstorm slice, MTG_MANA_ORACLE=2, gi 87000-158999, 72,000 games/arm; base = origin binary $S/bin_base,
   on = $S/bin_l5c; `logs/ds-apply5/gate_attrib.txt`):
   ```
   arm  sl   games  turns   units      cpu_s ADdecl ADundc ADordr ENUM CAPdec KarNL evD evU
   base all  72000 313540 139442026 84610    134    175     13   25    231   169  17  21
   on   all  72000 313504 139170048 84231    129     74     13   22    225    65  17  13
   ```
   Need-later Karrthus 169 -> 65 (-62%), all undeclared 175 -> 74, declared 134 -> 129, ENUM 25 -> 22, CAPPED-DECISION
   231 -> 225, ORDER equal; ADundc Kolaghan 4 -> 7 (+3, untraced). Turns -36: 33 jobs faster, 0 jobs slower (job level;
   the gate has no per-game wins, so a +1/-1 pair inside one job would cancel -- the A/B below is per game).
3. All-deck held-out A/B (`mk_ab.py --n 400 --hg 200 --wf 3000 --base 5410000`, 130 shapes, 88,800 games, bin_l5c, big
   lane at 32 threads; `logs/ds-apply5/ab_report.txt`): net 0 on every shape except dragonstormplay -1 (ONE game faster,
   gi 1147 seed 5411147 T6 -> T5); 0 slower games anywhere. Units: Dragonstorm -0.47% / 2HG +0.04%, Mirrorwing -0.01%,
   Niv -0.01%, all else 0. No d8 b0 replays needed (no slower game).
   Verdict: passes the adoption bar (net <= 0 on every deck, no slower game, gate better) -> keep DEFAULT ON; the CPU
   overhead below must still be cut.

4. Final binary ($S/bin_final, src 513976445 = debug/audit removed): play-identical to the A/B binary bin_l5c (the CPU
   batches' 144 jobs -- DS 8 + 64 decks x 1, both arms -- have the same digests as the A/B's at 32 threads).
5. Units (`logs/ds-apply5/units.txt`): 4351 / 4351 PASS (scripts/unit_par.sh, 8 shards, 2 cores), the 4 new
   `Dragonstorm visible closer: ...` cases included.
6. CPU (`logs/ds-apply5/cpu_ins.txt`, perf stat -e instructions:u, --threads 2, bin_final, lever pinned in the manifest):
   ```
   Dragonstorm play, 200 games (gi 0-199, seed 5410000+)   off 181.586 G   on 182.023 G   +0.24%
   64 other decks x 25 games (A/B o0 blocks)              off 5323.058 G  on 5330.314 G  +0.136%
   ```
   The other decks' +0.136% is pure overhead (their play and units are identical in the A/B, except Mirrorwing / Niv
   units -0.01%), and the USER rule is "cut CPU". NOT YET CUT.

## Running jobs
None. Big lane released (chain_big.sh DONE); lane jobs all finished.

## Remaining, in order
1. CPU CUT (exact changes only; re-verify with the same ins.sh manifests $S/cpu/{ds,nd}_{off,on}.json and digest
   identity vs $S/ab_out/batch.out). Where the cost likely is, cheapest first: the gate runs at every outermost
   autonomous payment with g_line_opens_unseen && hold == cost; then the closer loop calls EffectiveSpellCost per
   candidate; then the exact pre-check `witness(state)` = LumpAwareBillPayable (a board copy per lump combo + the
   backtracker) runs on every such payment that has a closer, even in decks with no lump / creature-only land. Exact
   cuts to try: (a) a necessary-condition producibility filter before the witness: for each closer colour c, floating
   c + ritual c + any untapped permanent that can make c (EffectiveProduces, a lump from CollectOnDemandCracks, a
   colored_creature_only land) -- none -> return -1 (same monotonicity argument as the pre-check); (b) skip the gate
   when there are no alternatives possible at all (no closer-colour float, no CCO land, no lump) BEFORE the witness --
   alts.empty() is already a return -1, just move it ahead of the pre-check (exact: with no alternative the plain
   payment stands either way); (c) compute EffectiveSpellCost only for creatures that pass the colour test (already so)
   and avoid the std::vector `others`. (b) is free and probably most of the non-DS cost. Measure each.
2. Docs: Round 5 section at the end of docs/design/dragonstorm-apply-drop-2026-11-26.md (style of Round 4: census,
   mechanisms D/T, the lever, the three refinements, repro table, gate table, A/B, CPU, units, the exactness limit, the
   judge-optimism finding, the residual shapes); `logs/ds-apply5/report.md` (git add -f).
3. If src changed for the CPU cut: rebuild (lane, 2 slots, cap 4; mtg + mtg-test), units again, identity (digests) again.
4. Final commit message WITHOUT "WIP"; `python3 scripts/check_references_safe.py`; push wip/ds-apply5 only.

Residual need-later Karrthus shapes (not fixed, for the write-up): lines that END with a creature rather than an opener
(s30142742, s30097325: {.., Seething Song, Lathliss}; hold != cost, the gate never runs); Karrthus + Kolaghan where the
named line continues past Apex (s30142087: hold != cost); s30095186 / s30130693 / s30111660 / s30092745 T7-or-other
boards not traced; s30101776's two-Apex plan (the first Apex's hold is both Apexes, Territory {C} spent on generic) is not
covered by the gate either, but the game's play moved and it now wins a turn earlier.

Exactness limit (state plainly): the lever replaces only a payment that provably strands the visible closer (no
witness) by one that keeps it payable (a witness), so it never strands the closer where the old payment kept it; it
cannot prove the new board keeps every UNSEEN post-Apex line the old one kept (the continuation is re-solved after the
reveal). It is therefore a play change that earned adoption by measurement, not a sound fold.

## USER rulings (verbatim from the briefs)
- Dragonstorm "spend the counters if they are required for the line".
- Adoption bar: the average at play settings; adopt if many decks gain, mitigate the few worse; ignore seed noise and
  clairvoyant losses; d0 barely counts.
- "cut CPU" -- suite headroom never justifies CPU.
- A sound collapse defaults ON; a lossy narrowing needs its A/B; everything findable at unlimited budget.
- NO GREEDY PICK INSIDE THE SEARCH WINDOW (heuristics only in a DecisionProvider, only to restrict/order).
- USER-owned decisions are surfaced, never settled.
- HARD RULES: never push phase-1-2-deck-analyzer; no GT accept; no smoke/regression tiers; do NOT take
  claude_box.lock unless the coordinator frees it (it did, 2026-11-28 11:07, for the ON gate arm and the A/B only:
  "under flock on that lock with MTG_RSS_CAP_GB=38 and full threads (nproc)"); every other run via
  `bash scripts/box_lane.sh --slots N [--cap-gb G] -- <cmd>` with N <= 2 (--cap-gb 3 engine batches, 4 builds);
  ./build.sh only; wait ~200 s after edits on 9p before building; never `timeout`; waiters on PID / DONE, never
  pgrep -f; scratch under build/scratch/ds-apply5/; context handoff past ~250K.
- Parent ruling: the lever is NOT provably exact, so it is a PLAY CHANGE that must earn adoption (event repros with =0
  equal to the base logs, unit tests, the ON gate arm not worse, the all-deck held-out A/B at seed base 5410000 + gi);
  "If it fails the bar, say so and recommend default OFF or deletion (an agent's own dead lever is deleted). State the
  exactness limit plainly in the write-up."
