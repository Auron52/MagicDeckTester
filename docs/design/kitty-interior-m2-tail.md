# Kitty's interior-m2 tail: root-caused, remedy designed, deferred

**Status: DIAGNOSED 2026-09-05 (deletion follow-up #2, searched-second-main-unconditional.md);
remedy BUILT 2026-09-06 as `MTG_M2_KEY_COARSE` and CLOSED the same day — mode 1 refuted by
the verify gate, mode 2 sound but merges zero states (see the remedy section).** Raw data: `logs/kitty_m2tail/`. Everything here is from
unbudgeted (b0, unit-counted, load-independent) runs at the §3c config: KittyEquipment d5,
max_turns=8, seed 400001+gi with `--game-index gi`.

## The finding

§3c's "+73.7% with 77.7% in two games" reproduces exactly: of 1000 games, only gi=231 (4.90x,
interior m2 = 79.6% of the game's 29.1M units) and gi=470 (1.62x, 38.3% of 49.4M) blow up —
median game is 3,290 units. On BOTH games, skipping the interior m2 entirely
(`MTG_NO_M2_SOLVE=1`) is digest-identical at the same win turn: **the whole 42.1M units buys
nothing on these two games.** (At the SHIPPED d5/b20 both games are unremarkable and
budget-bound — this tail is a generation-time/unbudgeted problem, not a shipped-quality one.)

## The mechanism: VOLUME of distinct post-combat states — everything else excluded by number

* 100% of Kitty's branch-site interior m2 already runs at ONE PLY (d<=0 calls: 1.29M / 9.5M;
  deeper branch calls: 0 / 0). `MTG_M2_CAP1` is an exact no-op to the unit on both games — same
  null, same reason, as hinata (that ladder rung cannot touch a d<=0-dominated profile).
* Epochs are tiny (13 and 11 decisions/game) — not epoch churn.
* At a 1M memo cap (zero clears), gi=231 still has **366,606 irreducibly distinct keys** (72.3%
  hit rate vs controls' ~90%); each individual search is small (~48 units). It is volume.
* Site attribution (searched minus skipped): every `fs_*` site is 0 delta — `FullSearchLine` is
  untouched. The mass is on the `SolveWithLookahead` fallback: gi=231 `la_bp_wave` +13.5M
  (Puresteel draw-breakpoint waves: 2x Paladin + 5 free-equip artifacts under metalcraft),
  gi=470 `la_cand` +12.0M (Kemba + 2 Cats + Stoneforge + 3 equipment = attachment permutations).
  The outer loop permutes attachment/tap detail the m2 answer cannot depend on, and each
  permutation keys distinctly under `BuildBreakpointKey`.

## The remedy — BUILT 2026-09-06 (`MTG_M2_KEY_COARSE`), mode 1 REFUTED by its own gate

**Coarsen `SearchedSecondMainMemoized`'s key to the m2 plan's real dependency set** (today:
whole-state `BuildBreakpointKey` + depth). Built as an int-mode lever (`g_simkey_m2coarse`
consumed inside `BuildSimKey`'s battlefield loop; `BuildM2CoarseKey` folds the mode into a
distinct key namespace; an m2-specific verify pass added to `SearchedSecondMainMemoized` under
`MTG_SOLVE_MEMO_VERIFY` recomputes the solve on every coarse hit and `SamePlan`-compares).

* **Mode 1 (drop attachment wiring + marked damage + temp pump): UNSOUND — refuted.** Units
  win was real (gi=231 −7.6%, gi=470 −21.7%, same win turns), but the verify arm printed
  repeated `[m2-search-memo] MISMATCH t7 d1: cached 6 actions vs fresh 5` on gi=231. The
  diagnosis above was wrong on one axis: attachment wiring IS a dependency for d≥1 interior
  m2 solves, because their lookahead rolls into COMBAT and who holds the equipment decides
  it. The verify gate did exactly the job it was declared for.
* **Mode 2 (keep attachments; drop only marked damage + temp pump): SOUND but WORTHLESS.**
  Verify clean — 839,490 coarse m2 hits recomputed, 0 mismatches (plus 0 over 10.2M general
  solve-memo hits in the same run). But on BOTH tail games mode 2 is identical to the off
  arm to the last digit: same hits (839,490 / 5,451,190), same misses, same clears, same
  `units_total` (29,063,027 / 49,371,580). Dropping damage + temp pump merges ZERO states —
  no two m2-solve states ever differ only on those axes (damage co-varies with the attack
  line, which already keys differently elsewhere). The distinct-key volume was entirely the
  attachment permutations, i.e. the one axis mode 1 proved is a real dependency.

**Verdict: the coarse-key remedy is CLOSED.** The only coarsening with value is unsound; the
sound coarsening has no value. The lever stays OFF (the tail is generation-time only, and the
bar was "free win or nothing" — this is nothing). The `MTG_BIG_SOLVE_MEMO` finding below
remains the only real mitigation for unbudgeted kitty work; a future attack on this tail must
key on something smarter than field-dropping (e.g. canonicalizing WHICH creature holds which
equipment when the holders are combat-equivalent), which is a different, harder project.

**Soundness gate (unchanged): the `solvememo::SamePlan` / `MTG_SOLVE_MEMO_VERIFY` harness — a
missed dependency means wrong plan reuse, so the verify run is mandatory, not optional.** It
has now rejected one mode; that is the system working.

Cheaper adjacent findings, recorded so they are not rediscovered:

* **`MTG_BIG_SOLVE_MEMO=1` (262k cap) for UNBUDGETED work** (generation, depth matrices):
  −16.9% / −12.6% on the two games, play-identical; 262k is the knee (1M adds −0.9%). At shipped
  d5/b20 it is byte-identical over 1000 pooled games (memo never fills) — zero shipped value.
* **`MTG_COST_REFRAME` (default OFF)** already contains a post-apply state dedup that would
  remove m2 calls without removing states; it changes tie-winners, so it needs a digest A/B, not
  an identity argument. Un-measured.
* 28.0% / 7.9% of interior m2 solves return an EMPTY plan; `SkipsUnproductiveSecondMain()` would
  skip them but is a skip-the-solve gate — the DISALLOWED class, not a budget cut. Do not reach
  for it as a "remedy" here.

## The bigger shipped-quality lead (open, surfaced 2026-09-05)

At shipped budget, 50–67% of a Kitty decision's units go to `units.fs_main2` —
`FullSearchLine`'s OWN second-main loop, a different cost centre the deletion never touched. If
the goal is shipped avg-win-turn (rather than generation tractability), that is probably the
larger target. Unowned; measure before building.
