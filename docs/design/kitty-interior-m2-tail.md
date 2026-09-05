# Kitty's interior-m2 tail: root-caused, remedy designed, deferred

**Status: DIAGNOSED 2026-09-05 (deletion follow-up #2, searched-second-main-unconditional.md);
the remedy is designed but NOT built.** Raw data: `logs/kitty_m2tail/`. Everything here is from
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

## The remedy (allowed class — deletes no line), when someone builds it

**Coarsen `SearchedSecondMainMemoized`'s key to the m2 plan's real dependency set** (today:
whole-state `BuildBreakpointKey` + depth). An interior m2 plan can depend on untapped mana, hand,
the cast-relevant battlefield multiset (incl. artifact count for metalcraft), and lethal-relevant
life — NOT on which creature each equipment is attached to, attacker tap state, or damage dealt.
Every searched m2 still enumerates its full candidate set; equivalent states just stop
re-searching. Expected ~3.7x cut on gi=231's extra (63.5% → ~90% hit rate). **Soundness gate:
the existing `solvememo::SamePlan` / `MTG_SOLVE_MEMO_VERIFY` harness — a missed dependency means
wrong plan reuse, so the verify run is mandatory, not optional.**

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
