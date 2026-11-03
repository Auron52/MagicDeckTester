# Mana-issue program: carry-over to the week of 2026-11-02

Checkpoint written 2026-10-31, when the weekly budget ran out. Each item gives its branch, its current state
and the exact next step. The method has not changed: fix by mechanism and dominance, never by a heuristic lever,
and hold the NO-GREEDY rule. The adoption bar: every slower held-out game recovers at
`--depth 8 --budget-ms 0 --ignore-play-profile`, or is CLEAR clairvoyance (equal on both arms under
`MTG_NC_SEARCH=1`); and each deck's net is <= 0 on held-out games (suite metric, unwon = max_turns+1).

## NOW (2026-11-02): the per-deck mana-clean pass

USER, 2026-11-02: analyze every deck one at a time with the mana analyzer, fix every issue found, and mark each
deck (`docs/design/mana-clean-gate.md`, `scripts/mana_clean.py`, `test/mana_clean.json`). Runs are sized per deck,
5,000-100,000 games. Loop per deck: `python3 scripts/mana_clean.py run <deck>` -> read
`logs/mana_clean/<run>/report.md` -> fix by mechanism -> land -> run the deck again on its next block. Progress is
`python3 scripts/mana_clean.py status`. Order: cheapest decks first (census3 cost), Melira last.

- Landed: `MTG_MC_FEED_ALT` default ON (48382a7bb; the mana cache is transparent); the gate (3397a7502 + sizing).

## Landed this week (phase-1-2-deck-analyzer)

- The sac-outlet payer stack (`MTG_SAC_OUTLET_PAY` default ON), the Goblins GT (22544d5f4), and the featurizer
  version (547e6fe8b).
- `MTG_REAL_PAY_ATTACK_PIN`, plus `MTG_DORK_ATK_PROVIDER_HOLD` (default OFF) (81148026f).
- `MTG_DEV_SEQ_TAIL_SKIP` and `MTG_DEV_SEQ_DIRTY_READ`, both byte-identical (138d2dbea; CI green on ubuntu,
  windows and parity).
- The EDF payment shapes (`wip/edf-pay-rescue`; mana-completeness-oracle.md 13.8): `MTG_ENERGY_AURA_PIP`,
  `MTG_TAPAHEAD_ENERGY_COVER`, `MTG_PAINLAND_TAPAHEAD_CAST_AUTO` and `MTG_ETB_BOUND_AURA` default ON;
  `MTG_EF_SCALAR_REFUND` default OFF. This lands once its smoke/regression pass. If this line is still here
  without a GT commit for edf after it, the landing did not finish: re-run the validation in
  `.claude/worktrees/edfx`, accept `--deck=edf`, and push.

## 1. Niv rock order: `wip/niv-rock-order` (not landed)

- **Commits:**
  - 0e76a161d `MTG_RAMP_FILTER_FEED_ORDER`
  - 1dfc9a121 `MTG_SERVANT_HASTE_KEEP`
  - 2ce3eb06d `MTG_NIV_CAST_ORDER` (the USER-approved order: Brainstorm first, Q1/Q2/Q4/Q5 as ruled)
  - 9a65f9264 docs
- **Branch state:** 20+ commits behind origin. Its doc section is "13.7" in mana-completeness-oracle.md, which
  collides with the landed 13.7 (ramp-filter feed); renumber it 13.9 at rebase. HeuristicArm.h conflicts are
  resolved by keeping both sides.
- **Held-out results (package vs old):**

  | seed | games | slower | faster | net |
  |---|---|---|---|---|
  | 1640001 | 2,000 | 29 | 35 | -6 |
  | 1650001 | 2,000 | 43 | 46 | -2 |

  Feed fix alone: +3 over 4,000 games. It is byte-identical inside the package, because Sol Ring ranks first
  there anyway. Hinata2, Dragons and Giants are byte-identical.
- **The slower-game bar is not cleared yet.**
  - s1640001: all 32 slower games are cleared (31 recover; gi1950 is clairvoyance).
  - s1650001: 28 of 46 recover.
  - gi774 (T3->T4) and gi887 (T4->T5) do not recover at d8 b0; their `MTG_NC_SEARCH=1` check was running.
  - 16 games (gi877..gi1992) were still running at d8 b0.
  - Logs: `build/scratch/nivorder/d8/run2b.log` and `nc2.log`, on the build volume. If those processes died
    with the container, re-run those games.
- **Next:** read the logs. If every game recovers or is clairvoyance, rebase, validate, accept `--deck=niv`
  (plus niv2hg if it moves), and push.
- **Open follow-ups:**
  - The enumeration-side Signet phantom: the enumeration credits the Signet's +1 whenever a feeder exists,
    so {Signet, Preordain} on two lands reaches the menu.
  - Arcum's Astrolabe (Snow) needs the same feed treatment.

## 2. Payment script default ON: `MTG_REAL_PAY_SCRIPT` (USER: "2 we should turn on")

- **The flip is shelved:** `build/scratch/script_ab/script_on.patch` (EngineFlags.h `false` -> `true`).
- **Why:**
  - On the seed-1630001 41-case A/B it was 0 slower / 0 faster (18,117 paired, units 1.000).
  - On the suite it exposed d0 defects:
    - Breaching s2023 gi21: unwon instead of T4.
    - Alchemist d0: smoke gi16, gi818, gi977; regression gi546.
- **`wip/realpay-safe` e7369a22d** (status in `docs/design/realpay-safe-status.md` on that branch):
  - **gi21 root cause:** the script's dry run fills the per-game mana cache. A cached FAILURE did not carry the
    flag that starts the payer's second ramp-filter pass, so the real cast hit the cached failure on every
    retry. Fixed under `MTG_MC_FEED_ALT` (default ON). `MTG_MANA_CACHE_VERIFY` checks the flag too.
  - Also fixed: a failed scripted sac-land payment restores the board, and a failed scripted tap-ahead hands
    back to the engine.
  - New scenario: `breaching_script_ramp_feed_hit.json`.
  - **The cache hole is live in searched play too.** `MTG_MC_FEED_ALT` therefore likely changes default play on
    the ramp-filter decks (Breaching, Niv, TreasureHunt, Bruna, Hinata2). Treat it as a play change: run a
    held-out A/B on those decks.
  - **Alchemist d0 is still open.** The dry run's combo search on the copy concludes "no route" and stores it
    per board; real play reuses it and never goes off (gi818). Find what the copy's search reads differently,
    using `MTG_AA_ROUTE_TRACE=1`.
- **Next:**
  1. Finish the Alchemist d0 issue.
  2. Run unit tests, scenarios, smoke, and an A/B for `MTG_MC_FEED_ALT`.
  3. Apply `script_on.patch` and re-run smoke/regression. The last run moved dragons, dragonstorm, giants,
     selesnyav2/selesnyav22hg, angels d5 s3003, and many d0 rows.
  4. Accept only the moved decks, and push.

## 3. Smaller open items

- **EDF gi552 (s1640001, T3->T4) under `MTG_EF_SCALAR_REFUND`.** It does not recover at d8 b0, which suggests a
  search defect. Its `MTG_NC_SEARCH=1` check did not finish. Also still owed: the refund gates and the site-1
  retry.
- **Lotus / Garth crack colour at spend time** (USER ruling: no per-colour branch; commit the colour when the
  mana is spent). This follows the floatseq work. The h1370 floatseq d8 b0 run was
  `build/scratch/floatseq/h1370/out.txt`.
- **The gi3031 haste-dork PHANTOM.**

## USER rulings, 2026-11-03 (these replace the "outstanding" list that was here)

1. **`MTG_DORK_ATK_PROVIDER_HOLD`** (FiveColour 31 faster / 8 slower, all recover, 1.35x units): **trim the cost
   first.** USER: "I agree with the suggestion to trim the cost. We probably do want it on in some form in the
   future" -- its quality gain outweighs the budget losses, but 1.35x units is too much branching as it stands.
   Owed: find what the extra units buy and cut the waste, then re-measure. Stays OFF until then.
2. **Value-leaf ties: adopt on a tie** when the goal is to move the kept-incumbent decks to the new featurizer
   (USER: "adopting on a tie is okay if our goal is to switch them over to the new type"). Owed: re-run the
   kept-incumbent decks' leaves with tie => adopt when the box is free (never alongside another generation).
3. **No-win options: wait** for the USER's budget-fairness agent, which is far along. Both stay OFF.
4. **`MTG_EF_SCALAR_REFUND`: chase EDF gi552 (s1640001, T3 -> T4, does not recover at d8 b0) before adoption is
   even considered** (USER: "This shouldn't even be a question"). Stays OFF.
5. **FiveColour d7b40** (was d7b40 -0.0050 turns at 1.15x): adopt **if it retains that performance** on the
   current engine (USER: "somewhat marginal ... not a slam-dunk"). Owed: a fresh held-out re-measure, then adopt or
   report.
6. **NivMizzetCombo cast order** (gi887 fails the bar): **approved** ("The new order seems okay"). The implemented
   order is the USER's (Brainstorm first among the draws; Curiosity/Lookout ahead of the draws only with a Niv).
   Owed before landing: root-cause gi887's T4 Signet + Lookout over Signet + Generator Servant valuation.
7. **UNMODELED and CAPPED fail the mana-clean gate** (USER: "Even if this means an issue in our instrument it means
   something to fix"). `scripts/mana_clean.py` ISSUE set; `docs/design/mana-clean-gate.md`.
8. **Slivers cast order: approved as a FULL order, every card on its own row** (USER: "Making it a full order is
   ideal. But yeah, only Hatchery and Thrumming Hivepool care about the order here"). Draft:
   `docs/design/cast-order-drafts-2026-10-14/slivers_vial.md` section 3.
9. **The mana rollback repairs mistakes; it never exploits clairvoyance.** USER: "I would use more hedge rules
   rather than actual clairvoyance in the mana engine ... I would rather build this engine in a more general way. I
   think we should be using the mana rollback idea, but that is more to compensate for mistakes rather than take
   advantage of clairvoyance." So `LINE_REPAY` may re-make a payment only against cards known at that payment (in
   hand, or in the plan), never against a card drawn after it. A drop that only a drawn card's knowledge would avoid
   is a candidate for a **hedge rule**: a provider hold for what to keep open when the next card is unknown (e.g.
   Dragonstorm's "hold 1 {B} when red is plentiful", Fluctuator's ordered {B} / {1}{R} / {W} holds).
