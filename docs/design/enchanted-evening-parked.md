# Enchanted Evening: PARKED (2026-11-26)

Enchanted Evening (EE) is a GW enchantress / lock deck (`decks/Enchanted Evening/Enchanted Evening.cod`, 60 cards). Its
analysis ran on branch `ee-analysis` from 2026-11-25 to 2026-11-26. The deck is now **parked**: its card definitions and
the rules-correct card behaviour have landed on the shared branch. The deck is marked **goldfish-disabled**, so it has
no regression cases, no generated resources and no viewer entry. The lock modelling stays on a backup branch.

## Why (USER, verbatim, 2026-11-26)

Ruling 26 (ledger): *"I'm starting to have second thoughts about introducing Enchanted Evening for goldfishing, so we
might want to park it in some form. The deck is very good in real play, but pretty poor even with attempts to model the
lock."*

Ruling 27 and the park decision:

* *"I would park it one it is in a state that is useful. Even the card definitions and such are probably useful to have
  in there. The main thing I probably do not want yet is to have it in the regression tester or generate any resources
  for it. We probably do not need a goldfishing heuristic engine either, though maybe we can leave that in there for
  now. Ideally it shouldn't show up in any viewers."*
* *"Maybe just add a way to mark something as disabled for goldfish rather than moving it into another worktree?
  Realistically we'll need this anyway as decks like this one don't want to be doing goldfishing."*
* *"We might want to drop the weird logic with the fake permanent stuff from goldfish arguably. The stuff we did for
  Worldfire is good, though."*
* *"Some free turn machinery does come from worldfire."*
* *"Essentially Worldfire is easy to consider because it wipes everything and leaves no trace. The other lock stuff we
  did here was super complex and becoming more than golfishing really should be."*
* *"So, Worldfire can remain in goldfish because even though it doesn't fit 100% perfectly, it is close enough.
  Enchanted Evening should only use the new mode and 1v1 once they is built."*

## Where the full work is

* **Backup branch `backup/2026-11-26/ee-analysis-lock`, tip `a83bec735`** (= branch `ee-analysis`, worktree
  `.claude/worktrees/enchanted-evening`). Merge-base with the shared branch: `05e2e0030`. This branch holds all 54
  commits: the lock modelling, the virtual opponent board, the lock-only folds, the ruling-22 lock playout and every
  measurement. A future control-scoring mode or 1v1 should start from it.
* The analysis ledger (`docs/design/analysis-Enchanted Evening.md`: USER rulings 1-27 verbatim, every step's
  measurements), the lock design (`docs/design/enchanted-evening-lock-design.md`) and the discard-bucket draft
  (`docs/design/enchanted-evening-discard-buckets.md`, AI-drafted with the USER's amendments, NOT built) landed with
  this park. They describe code that is only on the backup branch from lock step 6 on.

## What landed (branch `ee-park`, from `origin/phase-1-2-deck-analyzer` 34bd5960c)

The kept commits were cherry-picked in their original order, then squashed (see "Commits" at the end).

**(a) Card definitions and rules-correct card behaviour, with their unit tests** (from f986f6e19, 61e5bc848,
b5b8dd3cb, a9d866afe, aea0e800e, 67ab77dfe, 3cd4e4c8f, 471a97cab, 21a2cb821, 38c56beb8, 89c1f70f0, f855925a7,
eb746055a, fe22e545f):

* `cards.json` + `scryfall_reference.json` entries for the deck's new cards: Sunpetal Grove, Oblivion Ring,
  Enchantress's Presence, Sterling Grove, Enchanted Evening, Opalescence, Calming Verse, Force of Vigor, Parallax Wave.
* Oblivion Ring: a searched target axis. Sterling Grove: its shroud grant (`deck_has_enchantment_shroud`) and its lead
  sacrifice-tutor activation. Enchantress's Presence: its on-cast draw.
* Enchanted Evening + Opalescence type statics (layer 4 / 7b; `RefreshTypeStatics`, `deck_has_type_statics`): every
  permanent becomes an enchantment, and the other non-Aura enchantments become MV/MV creatures, so lands become 0/0
  and die to SBA.
* Lock step 2's pre-float. Lands that are about to die to EE + Opalescence are tapped for mana first. This is a sound
  dominance fold, and it applies whenever the two statics complete in a real game (`deck_float_wild_exact`).
* Calming Verse (its KEEP / SWEEP-SELF axis). Force of Vigor's HARD cast (a searched target set). The no-op Verse / FoV
  cast fold (a sound dominance fold in the provider, also kept out of the playout).
* Parallax Wave. Fading counters on every enter route, a searched exile activation, the linked exile list with its
  leave-the-battlefield return, the self-exile loop, and Grove shroud legality. Also USER ruling 7's end-step sweep with
  ruling 17's rule: no counter is spent when the self-exile loop is impossible.
* Unit tests: `test_enchanted_evening_simple.cpp`, `_lock1.cpp` (type statics), `_lock2.cpp` (pre-float),
  `_lock35.cpp` (Verse / FoV), `_lock4.cpp` (Wave), `_grove_prune.cpp`. The "lockN" files are named after the build
  step. They test card behaviour, not lock credit.
* The `.claude/skills/mtg-rules.md` Fading row (CR 702.32a), and `audit_viewer_decisions.py` classifications for the
  new params.

**(b) Engine fixes that are not EE-specific.** None needed separating. The general mechanics above (type statics,
fading, the Wave loop, Verse, FoV) are generic card mechanics, keyed on params and not on card names. No commit in
the range was a stand-alone engine fix outside the EE cards.

**(c) `EnchantedEveningProvider`, non-lock heuristics only** (0e80072ed, 6840da137, de901bc9e, and the
second-Opalescence part of 2df2cebb1):

* `MTG_EE_USER_PRUNE` (heurarm `EE_USER_PRUNE`, **DEFAULT ON, USER ruling 23**). There are two parts. The first allows
  no second Sterling Grove while one stays, unless that window sacrifices the first. The second orders and prunes the
  Grove / Idyllic Tutor targets: missing lock pieces first, then one mana Aura while mana is needed, then Presence. It
  works on the real board only, with no lock credit.
* `MTG_EE_GROVE_PRELOCK` (the amendment arm, **DEFAULT OFF, USER ruling 23 "stays default OFF"**), with its
  second-Opalescence extension. "Pre-lock" here means "Enchanted Evening is not on our battlefield", a board test. The
  USER owns this default.
* `WaveOpalSweepRule` (ruling 7's deck default), and the no-op Verse / FoV fold.
* One fix made while porting. Origin's c1206ef6b restored a static `produces` hint (W/B/R/G) on Windswept Heath and
  Wooded Foothills. The prune's mana census (`eeprune::CensusOf`) read that hint instead of what the fetch finds, so
  `fetch_forest` was never set (unit B4 failed). It now counts a fetch by its `fetch_land_types` only.

**(d) Docs:** the ledger, the lock-design doc, the discard-bucket draft, and this file.

## What was dropped (it is on `backup/2026-11-26/ee-analysis-lock`)

| commit(s) | what | why dropped |
|---|---|---|
| 4a676a477, ff0c0014a, 9571ebde7 (lock step 6) | the EE lock trait (`DeckHasEeLock`; free-turn credit for EE + Opalescence and for Calming Verse + EE), no reach certificate, the `MTG_FREE_TURN_EE_FAIR` virtual opponent board (`eefair`: record, Verse + sweep + FoV clear, veto clauses 1/2/4), the umbra-armor virtual-Verse rule (`aura_umbra_armor`), `[ee-fair]` log tags | the lock modelling / "fake permanent stuff" (ruling 27) |
| a26942ea0, c69cb9da9, 1ad7ca2da (lock step 7) | Force of Vigor's off-turn pitch in the opponent's end-step window ("EOT FoV under the lock") | it only had targets on the virtual board; so FoV's pitch clause is NOT modelled (FoV is hard-cast only) |
| e0e74e894, 44303575e (lock step 8a) | `MTG_FREE_TURN_STAND` (see below) and the FoV own-target fold | STAND fired only for EE; the fold is lock-only |
| fd439c7af, e51f75196 (lock step 8b) | the Utopia Sprawl {G, W} colour fold and the Parallax Wave own-target Exploration fold (the dead-land fold was already withdrawn there) | lock-only folds (brief) |
| 2df2cebb1 (rulings 19-21, except the second-Opalescence extension) | veto clause 3 deleted / clause 4 (`kHexproofEvasive`), O-Ring virtual targets (`opp_virtual_oring_link`), the `aura_grants_flying` / `aura_grants_protection_from_creatures` params and their `cards.json` entries | virtual board |
| 0508a9c11 (ruling 22) | the lock playout (`MTG_EE_LOCK_PLAYOUT`), `GreedySite::LockPlayout` and its no-greedy doc entry, the step-8c trace lines | needs the lock credit; ruling 27 |
| lock unit tests | `test_enchanted_evening_lock6/7/8a/8b/22.cpp` | they test the dropped code |

`GameState` grows only by the kept deck stamps (`sizeof` 1744, `Dominance.h`). None of `opp_virtual_board` /
`opp_virtual_spent` / `opp_virtual_oring_link` landed.

### `MTG_FREE_TURN_STAND` (EE lock step 8a): DROPPED

The flag adds a pass-0 ceiling (25x the budget) for free-turn decks at a finite budget. When a pass trips the ceiling,
it is re-run with free turns counted as plies. The ledger's step-8a section measured it. Smoke with it ON had
play-changed=0 and configs changed 0 / 247, including the three free-turn decks in the suite (Worldfire, Cyclone,
Shadowborn): *"no smoke game's pass 0 reached the 25x ceiling"*. On EE's own 224-game A/B, `=0` was unit-for-unit
identical to ON. The only shapes it changed were EE's degenerate free-turn x end-step-FoV-fan nests (ho1o gi=9 > 3.3 h
-> 13.8 s) and its own unit case. Both come from the EE lock credit and the step-7 off-turn FoV, which are both
dropped. Since it fires only for EE, it was dropped, as the brief says. It is not inert in principle, though. A
Worldfire pass 0 that ever reached 25x its budget would be re-ranked. Regression and overnight were never measured with
it. If a free-turn deck ever shows an unbounded pass 0 at a finite budget, recover it from e0e74e894 + 44303575e (it is
budget-inert at b0 by construction).

The Worldfire free-turn machinery on origin (`src/core/FreeTurns.h`, `docs/design/opponent-lockdown-free-turns.md`) is
untouched.

## Key measurements (from the ledger; d3 b10, no profile, avg counts a loss as 9)

* **Without lock credit** (lock steps 1-4 plus the prune, which is the code that landed): step 1 sanity (24 games,
  seed 7000): 23 won, avg 6.625. Step 4 (Wave real): 22/24, avg 6.7083, units +27%.
* **The free-turn lock (step 6, fairness check ON)**, on top of the prune: sanity 24/24, avg 5.7083 (was 22/24
  6.7083). Held-out 5.86 / 5.85 (was 6.81 / 6.84), units -30..-43%. Compared with the fairness check OFF, the check
  cost 0.67-1.00 turn. Real vetoes: 11 turns in 9 of 224 games, all clause 1. Steps 7-8b then reached about 5.58-5.89
  per family (step-8a table). The USER's reading of these numbers: *"pretty poor even with attempts to model the
  lock"*.
* **The tutor prune (`MTG_EE_USER_PRUNE`)**, pre-lock (rulings 15/16, 2026-11-25). Sanity 22/24 6.7083 in both arms,
  units -63%. Held-out seed 8000: ON 93 / 6.81 vs OFF 95 / 6.75, units -50%. Seed 10000: ON 95 / 6.84 vs OFF 94 / 6.80,
  units -58%. The re-measure with the lock credit ON (224 games, 2026-11-26) gave OFF 5.848, ON 5.799 (net -11, units
  -59.7%), and ON + amendment 5.799 (net -11, units -57.8%). Every slower game recovers at d3 b0 except two
  clairvoyant ones (8000 gi=84, 10000 gi=24). USER ruling 23 adopted it ON. **Not re-measured on the parked
  (lock-free) code.**
* **The ruling-22 lock playout (m22, 224 games):** with the playout 5.8080, without 5.7991. Net +2 (1 faster, 2
  slower, both recover at d3 b0), units -1.4%. Nearly all search happens before the lock is credited.

## The goldfish-disabled marker

`decks/<Name>/<Name>.modes.json`, for example
`{"goldfish": false, "note": "parked 2026-11-26: lock/control deck; scored only by the planned control-scoring mode or
1v1 (USER)"}`.

* **A missing file means goldfish is enabled**, so every other deck is unchanged. The file is meant to grow: a future
  control-scoring mode will be switched on per deck in it. Keys other than `goldfish` are passed through to readers.
* **An unreadable marker reads as DISABLED** (with an `error` field). A typo in the file must never switch goldfish
  back on. A variant folder (`decks/<Name>/<Variant>/`) inherits its parent's marker.
* **The ONE readers:** Python `scripts/deck_registry.py` (`deck_modes`, `goldfish_enabled`, `goldfish_disabled_reason`,
  CLI `--goldfish-check <path>...`, which exits 4 when disabled) and JS `tools/lib/deck_modes.js` (`deckModes`,
  `goldfishEnabled`). `test/deck_modes_test.py` checks the two agree.

**Consumers:**

| consumer | behaviour |
|---|---|
| `scripts/deck_registry.py` `discover()` | skips the deck and its variants (so does everything built on the registry: `esc_skip_stage.py`, `mana_clean.py`, `mana_oracle.py`, `play_setting_stage.py`, `ref_bench.py`, `ref_bench_arms.py`, `ref_review.py`, `trust_recheck_stage.py`, `deck_avg_arms.py`, `valueleaf.sh`'s DECK_TABLE) |
| `tools/play/server.js` `listDecks()` | hides it (and its archived lists); `resolveDeck` still opens an explicit id |
| `tools/mulligan/build_deck_list.js` -> `decks.js` | hides it |
| `scripts/suite_gate.py` `--require` / `--cost` / `--measure` | REFUSED, exit 3, with the marker's note |
| `scripts/mullgen.sh run` | refuses (exit 3) before anything runs; NOT overridable by `MTG_ALLOW_UNTESTED_DECK` |
| `scripts/valueleaf.sh run` | refuses (exit 3) before the registry lookup; NOT overridable by `MTG_ALLOW_UNTESTED_DECK` |
| `scripts/provider_audit.py` (no-args walk) | skips (otherwise reported as an "unprofiled" hole); an explicit path still audits |
| `scripts/keep_margin.py --all` | skips (it needs a keep table anyway) |
| `scripts/analyze_deck.py decks/<Name>/...` | runs, after a one-line WARNING |
| `mtg` with an explicit deck path | runs, with no warning (the C++ side does not read the marker) |

**The other `decks/` walkers, all checked.** No other non-one-off script enumerates `decks/` by itself:

* Registry-based: listed above.
* Suite-list only (`test/regression_cases.sh`): `canon_audit.sh`, `pgo_train_manifest.py`, `verify_deck.py`,
  `fungus_regression_add.sh`, and the `bp_open/`, `burn_order_ab/`, `critter_order_ab/`, `dig_choice_ab/`,
  `burn_volley_ab/` and `depletion_hold_ab/` makers (`ALL_DECKS`).
* Hardcoded deck subsets: `gen_*_gate.py`, `hand_entry_census.sh`, `mullgen_isocost.sh`, `mullgen_cost_probe.sh`,
  `stage1_census.sh`, `stage1_collect.py`, `units_probe.sh`, `beam_growth_merged_ab.sh`, `wk_engine_neutrality.sh`,
  `depletion_hold_ab/report.py`, and `generate_opponent_deck_pool.py` (its fixed 50-name pool does not hold EE).
* Explicit path only: `analyze_deck.py`, `deck_compare.py`, `stompy_*`, `shape_menu.py`, `shape_probe.py`,
  `wall_probe.sh`, `tourney_*`, `ref_handoff_sweep.py`, `keep_delta.py`, `vlq_*`, `win_divergence.py`, the per-deck
  chain scripts, and `bp_open/stage_data.sh`.
* One-off that walks `decks/`: `wk_coverage_gap_ab.sh` (a historical coverage A/B). It is left as is.
* Browser: `tools/play/index.html` only shows `/api/decks` (filtered). `tools/mulligan/index.html` has a
  client-side folder picker (`walkHandle` -> `keepcore.js discoverDecks`). It lists only decks with a keep table, which
  a goldfish-disabled deck never gets (mullgen.sh refuses), and it does not read the marker.

## The deck today

`decks/Enchanted Evening/` holds only `Enchanted Evening.cod` and `Enchanted Evening.modes.json`. There is no
profile, no references, no value leaf, no keep table, and it must not get any while it is parked (the generators refuse
it). `analyze_deck.py --coverage-only` reports every card `full`, none missing. Force of Vigor's off-turn pitch is a
bracketed NOT-MODELLED note in its `cards.json` entry (dropped with step 7), so FoV is hard-cast only. An 8-game
explicit-path sanity run on the parked code (seed 7000, d3 b10, no profile, no lock credit) averaged 6.25 turns. Parallax Wave's end-step sweep finds no target in a goldfish game unless the opponent has a
permanent. `EnchantedEveningProvider` still routes the deck when it is run on an explicit path.

## What would un-park the deck

1. **A control-scoring mode or 1v1 exists** (USER: *"Enchanted Evening should only use the new mode and 1v1 once they
   is built"*). That mode is switched on in the deck's `.modes.json`. `"goldfish": false` stays, because the deck is
   not to be goldfish-scored again.
2. Then, from `backup/2026-11-26/ee-analysis-lock` (a83bec735), recover what that mode needs. The candidates are the
   lock modelling (step 6), FoV's off-turn pitch (step 7, a real card clause: under a real opponent it has real
   targets), the lock playout (ruling 22, which needs a new `GreedySite` and USER sign-off under the no-greedy rule),
   and `MTG_FREE_TURN_STAND` if free turns are still the vehicle. The virtual board (`eefair`) is goldfish-only
   scaffolding; a 1v1 has a real opponent board instead.
3. The remaining analysis stages, as the ledger's CHECKPOINT listed them: step 8c (gi=70 / gi=3 b0 tractability,
   gi=90, the gi=31 Verse fold, gi=84), the discard buckets (draft built behind a default-OFF flag and A/B'd), step 9
   (viewer `eot_cast`, ruling 14, DECISIONS.md), the profile and provider audit, the suite addition (all three tiers,
   the 3x Hinata gate), and Stage 5 verification.
4. Removing the marker, or switching goldfish back on, is the USER's decision alone.

## Commits

See the `ee-park` branch log: the card/behaviour + provider port, the marker, and the docs.

## USER rulings after parking (2026-11-26)

* `MTG_EE_GROVE_PRELOCK` (second-Opalescence amendment, default OFF). USER, verbatim: "We can leave it off for now,
  but in the future we may delete it as a clean-up."
* The deck's play heuristics (EnchantedEveningProvider, tutor prune `MTG_EE_USER_PRUNE` ON). USER, verbatim: "Let's
  keep them. Many of them may still be useful in the new mode."
