# Matron combat mismatch (wip/mc-matron): DONE, validated, awaiting landing (2026-11-04 ~12:30 UTC, successor 2)

USER ruling (verbatim): "Sure, let's fix the matron comba mismatch."
Brief rules carried over: do not push (coordinator lands); never accept GT; THREADS <= 8; MTG_RSS_CAP_GB=43;
no timeouts; never pgrep -f/pkill -f; never touch references/; never regenerate mulligan profiles; adoption bar =
every slower game recovers at --depth 8 --budget-ms 0 (only clear clairvoyance exempt, test with MTG_NC_SEARCH=1)
and held-out A/B net <= 0 per moved deck (>= 1,500 games/deck, fresh seeds, ONE pooled batch), report units and
instructions:u (perf stat), justify anything above ~1%. STOP RULE: two failed attempts at one sub-problem -> report.

## ROOT CAUSE: FOUND
Not the Lackey->Matron trigger order and not tapped attackers. It is STAGED (impulse-exiled) cards:
* The executor holds unplayed staged cards OUT of hand between TakeTurn passes (src/ai/AIEngine.cpp:2924
  ReturnStagedFromHand at each pass's tail, :2904 MergeStagedIntoHand at the next pass's head), so the REAL
  combat's hand has none of them. The rollout's combat (src/ai/TurnSolver.cpp SimulateOneCombat) kept them in
  hand flagged m_is_staged.
* GoblinsProvider::TutorCandidates (src/ai/DecisionProviders.cpp) reads the hand without skipping staged cards:
  goblins_in_hand (:17145) -> buff_targets (:17216); the tutor-source scan (:17333, and :18035's swing_atk src_pow)
  -> entering_fodder -> skirk_ramp (:17283) -> mana_now / mana_next; hand lords for swing_atk (:18035).
* gi4834 (seed 2514835, d8 b0, goblins-lr binary), T3 combat, SAME board (Matron put first; G=5 sick=4
  ready_atk=2), from build/scratch/mc-goblins/a10/g4834_tut.err:
    search l.1630: swing_atk=19 now=11 next=9 buff_targets=8 skirk_ramp=5 -> Goblin Warchief
    real   l.2108: swing_atk=18 now=10 next=8 buff_targets=6 skirk_ramp=4 -> Goblin King
  Staged at that moment (realised game log): Twinshot Sniper, Goblin King, Siege-Gang, Goblin Matron#14, Krenko,
  Muxus (Rundvelt Hordemaster impulses). Siege-Gang + Muxus = the +2 hand Goblins; the staged Matron#14 reads as
  the "entering tutor source" (+1 skirk_ramp, +1 src_pow). The handoff's "G=6 ready_atk=6" sim dump was a SIBLING
  put order (Chieftain first) the search also explored, not the committed line.

## FIX (final series on wip/mc-matron, rebased onto origin/phase-1-2-deck-analyzer; backup branch wip/mc-matron-backup)
* diag commit (kept; default-off): MTG_TUTOR_RANK_DUMP=2 (board/hand behind the ranking, real resolutions always
  print), MTG_LACKEY_TRACE=2 (each put's ordinal rank and pick). Extensions of existing default-off traces; the
  tr-board dump is the general instrument for executor-vs-rollout input mismatches at a tutor.
* fix commit: CombatStagedOutScope (src/ai/TurnSolver.cpp, used at the head of SimulateOneCombat) holds staged cards
  out of hand for the combat and re-appends them after it (the executor's main-2-head layout). UNCONDITIONAL: the
  A/B lever MTG_COMBAT_STAGED_OUT and its HeuristicArm slot were deleted after the A/B (NO-GREEDY "delete, don't
  lever"; this is a lockstep fix). test/unit/test_combat_staged_out.cpp pins the gi4834 board (rollout ==
  executor: Goblin King; staged cards re-appended in order); the lever-off case was deleted with the lever.

## VALIDATION (lever binary, pre-rebase tree 19d9d85e8 + diag + fix)
* Smoke: 169/169 byte-identical to GT (0 moved rows).
* Scoped regression (goblins, goblins2hg, fungusv3, fungusv32hg, burn, burn2hg, midrangeburn, midrangeburn2hg,
  hinata, hinata2hg -- every suite deck holding staged cards): ONE moved row, hinata_regression_d0_s2002
  6.8480/09e1f03dfb2fc880 -> 6.8470/8773fd2849120167 (gi60 unwon -> T8, FASTER, d0). Searched: 0 slower, 0 faster.
  References: 0 play-drift / 0 board-diverged / 0 enum-gap (747 refs); REQUIRE-BEAT 2/2.
* Slower games: none anywhere (smoke, regression, A/B), so no d8 b0 recovery runs were owed.
* Held-out A/B, ONE pooled batch, both arms via the per-job lever, fresh seeds 2810001+, shipped play settings
  (value_play owns the depth; budgets goblins 60, hinata 30), 2,000 games per deck per arm:
    goblins     0 moved, net 0, units 17,370,078 -> 17,370,067
    goblins2hg  0 moved, net 0, units 36,960,176 -> 36,960,191
    hinata      0 moved, net 0, units 158,253,516 -> 158,253,516
  instructions:u, pooled (both arms) 61.72e12; goblins+goblins2hg each arm alone: 3.97499e12 -> 3.97526e12 (+0.007%),
  per-game outcomes byte-identical.
* gi4834 (seed 2514835, d8 b0) on the goblins-lr line + fix (.claude/worktrees/mc-matron-lr): T3 (digest
  7713af3edf2a5afa, units 288,111); lever off reproduces T4 (c27ddc3c0759fb6b, units 31,788).
* Scratch: build/scratch/mc-matron/{smoke,reg}.log, ab/ (abcmp2.py), g4834/.

## REMAINING
* Landing (coordinator). GT is untouched: smoke is byte-identical; the one regression d0 row (hinata s2002) needs
  the normal accept flow when landed. The scratch worktree .claude/worktrees/mc-matron-lr (detached, uncommitted
  lr+fix edits, no references) can be removed with scripts/safe_worktree_remove.sh.
