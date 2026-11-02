# Mana-issue program: carry-over to the week of 2026-11-02

Checkpoint written 2026-10-31, when the weekly budget ran out. Each item gives its branch, its current state
and the exact next step. The method has not changed: fix by mechanism and dominance, never by a heuristic lever,
and hold the NO-GREEDY rule. The adoption bar: every slower held-out game recovers at
`--depth 8 --budget-ms 0 --ignore-play-profile`, or is CLEAR clairvoyance (equal on both arms under
`MTG_NC_SEARCH=1`); and each deck's net is <= 0 on held-out games (suite metric, unwon = max_turns+1).

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

## USER decisions outstanding (measurements are input; nothing here is settled)

1. **`MTG_DORK_ATK_PROVIDER_HOLD`:** FiveColour 31 faster / 8 slower, every slower game recovers, at 1.35x
   units. Default OFF until the USER decides.
2. **Whether a value-leaf tie should be adopted.**
3. **No-win options (a)/(b)/(c)** (`MTG_NOWIN_TWIN_FREE`, net 0, changes budget semantics). The recommendation
   is (a), leave both off until the USER's budget-fairness work lands.
4. **`MTG_EF_SCALAR_REFUND`:** OFF because gi552 does not recover. The question is whether to chase gi552 first
   or adopt it with gi552 unexplained (the shipped four measured EDF -9 / -13 on two held-out seeds).
