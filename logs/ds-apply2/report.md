# DS agent 3 report (from the agent's final message, 2026-11-27 ~01:00 UTC; handed off at 266K)

WIP eae78830d on wip/ds-apply2; handoff logs/ds-apply2/handoff.md. Nothing landable yet (no gate, no A/B).

- Debug-build line-keep units 7/8 (the s30129376 test fails). The temporary debug print was removed from
  ManaPayment.cpp after that build; the committed tree is NOT yet compiled.
- (a) TWO UNCLAIMED TERRITORIES -- root-caused, lever MTG_CCO_RESERVE_JOINT (default ON). Not the line-keep rung: the
  greedy's CCO line reserve (MTG_CCO_RESERVE_OTHER refinement) releases a Territory when the OTHER Territory makes that
  colour, judged per colour, so with Karrthus owing {B} and {G} each Territory counts the other for both and neither is
  held; the hand-demand rung then taps one early and Desperate Ritual's generic takes its {C}. Fix: the other lands
  must cover the shortfall jointly (only adds holds; the payer's complete-search fallback is intact). s30096797: 2 -> 0
  flags (2 with the lever off). Unit test on/off + identical-arms control pass (fixture needs the game's hand). The
  other four repros (s30112157, s30142602, s30155973, s30089733) not re-run on the new binary.
- (c) EXACT SWAP TEST -- lever MTG_LEFTOVER_SWAP_EXACT written, repro NOT fixed. "rest_creature as in
  LineReducerKeepHold" cannot judge this board (that flag compares the line's total noncreature MV, so Karrthus reads
  unpayable at Song's payment either way); a loose/strict bracketed test was written instead. s30129376 still 1 flag
  with both levers (1 base): no {B} floats before Song's payment, so the {B} is lost EARLIER (Ruby Medallion's
  payment), not at Song's generic. Its unit test fails in both arms (the swap needs a debug pass). Fix or delete the
  lever before the gate. The lever also fixes the old s30087280 board, so that older test pins it off.
- Next, in order: decide (c), rebuild, full unit suite, event mini-gate, pooled gate ($S2/gate.json built), all-deck
  held-out A/B at base 5220000 (the reserve rule is shared payer code: other decks with creature-only lands are
  reached), d8 b0 replays of slower games, design doc.
- No jobs running; no USER-owned questions.
