# Card dependency map — analysis-derived, driving classification AND cast order

**USER doctrine (2026-08-15):** the doubt-flip residual (gi=28 / gi=149,
`midphase-ordering-audit.md`) "is entirely a dependency problem. If we can map those out and
preferably create them properly during the analysis process it should help with this problem
and cast order. These two problems are very very similar." Earlier form (session 5g): "bake in
the dependencies between cards (which needs to be part of the ordering anyway)".

Also USER: **unbounded should be 0** — an unbounded-budget search with correct emission never
loses to base. The doubt gap measured +0.067 at budget 10 / 40 / 1,000,000, so the unbounded
gap IS the blocked-line meter. Acceptance bar for this work: unbounded doubt gap → ~0 on the
antilife 300g apparatus; only then do bounded budgets measure practical cost.

## The insight

A card's main-phase class and its cast position are not intrinsic properties — both are
consequences of the same dependency graph. The m1/m2 boundary used to carry these semantics
implicitly (enabler cast in m1 is live by m2); the collapse must carry them explicitly.

Worked example (antilife): Invigorate is Main1 (pump). Its alt cost makes the opponent gain
life → it depends on a lifegain→loss enabler → **Tainted Remedy is pulled to Main1** (an
enabler must be considerable in the phase of its payloads). Aria of Flame's ETB ("opponent
gains 10") is itself a Remedy payload, and its verse trigger is a payoff for instant/sorcery
casts → **Aria is pulled to Main1** (payoff resolves before the casts that feed it). That
closure alone fixes gi=149 (Aria castable off the exalted Hierarchs BEFORE they attack —
no creature-mana special case needed) and orders gi=28's T3 line correctly (Aria before
Silence; Silence last).

## Edge types (derived mechanically from CardParams — no per-deck hand code)

1. **ENABLES (A → B):** `A.lifegain_to_loss` and B gives the opponent life
   (`alt_lifegain_cost > 0`, `opponent_lifegain > 0`, `etb_opponent_lifegain > 0`,
   `tap_opponent_lifegain > 0` (Grove), `controller_lifegain_equals_power` (Swords)).
   B's payload pays off only if A resolved first (or is live).
2. **CAST-PAYOFF (A ⇐ casts of class C):** `A.verse_damage` ⇒ A benefits from every
   instant/sorcery cast AFTER it resolves. (Prowess is the board-resident sibling, already
   handled as the pump/attack-feeding class.) A wants to precede C-class casts.
3. **DESTROYS / CONFLICTS (A ✗ B):** `A.destroy_all_enchantments` kills enchantment enablers
   and payoffs (Remedy, Aria). A orders LAST within a subset, and a subset containing A is
   valid only with a surviving (creature) enabler or subset-level lethality.

## Consumers

* **Phase classification closure (`ClassifyMainPhase`):** start from the explicit classes
  (attack-helping → Main1, damage templates → Main2, ...). Propagate to fixpoint:
  - if any payload B of enabler A classifies Main1/Both → A pulls to Main1 (or Both);
  - if cast-payoff A's trigger class C has any Main1 member → A pulls to Main1;
  - a DESTROYS card never pulls anything forward.
  The graph is per-deck and tiny; the fixpoint is trivial.
* **Cast order:** generalized enabler-first = topological order on ENABLES edges (Remedy
  before Invigorate-alt / Fiery Justice / Aria-ETB), CAST-PAYOFF nodes before their trigger
  casts (Aria before instants/sorceries), DESTROYS last. This subsumes the per-provider
  `CastEnablerFirst` / `CastOrderRank` hand rules for these classes and is exactly the
  "dependencies are part of the ordering anyway" doctrine.
* **Subset validity / emission:** a payload is backed iff its enabler is live or in-subset
  (exists: `SubsetHasUnbackedAltPayload`); still needed from the audit: the wipe-lethality
  test must be SUBSET-level (sum of in-subset converted damage + attack), not per-card.
  **The tight provider gate (`ShouldEmitRiskyAltPayload`, built to protect the greedy m2 —
  gi=36/84): USER 2026-08-15 — try DROPPING it outright first** (one emission rule for both
  consumers; the greedy m2 is slated for removal anyway, so a heuristic protecting it is
  polish on a path to delete). Be careful: measure the greedy-exposed configs (d0 + rollout
  leaves) for re-bricking; ONLY if dropping causes real issues, FIX the heuristic (give it
  the subset-level lethality) rather than resurrect the stale per-card gate.

## Where derived

At deck load (provider/CardDatabase init), from params — automatically correct for new decks.
The **analyzer** (`scripts/analyze_deck.py` / the analyze-deck skill flow) should PRINT the
derived map as part of coverage/review output so a human sees the edges the engine inferred
(a missing edge is an implementation-review item, same as a coverage gap).

## Status

**IMPLEMENTED 2026-08-15 (commits 2409fb1 + 14487b0). Acceptance bar met exactly.**

* 2409fb1 — the map itself: `GoldFishRunner::DeriveDependencyPulls` (per-deck fixpoint,
  stamped on GameState), `ClassifyMainPhase` pull-forward, generic `CastOrderRank` tiers
  (lifegain_to_loss → 0 subsuming the antilife overrides; verse_damage → 19; destroy_all was
  already 30), analyzer prints the edge list. **Both exemplars hit target from this commit
  alone**: gi=149 (seed 2151) 5→4; gi=28 (seed 2030) 4→3 — Aria's rank-19 converted ETB
  resolves before the m2 harvest, so the per-card lethality gate sees the reduced life total.
  Base impact: 3 antilife smoke keys are the ordering improvement itself (d0 5.5660→5.5650,
  d3/d5 avg unchanged, lines realize MORE damage in-turn); GT accepted.
* 14487b0 — the emission heuristic. The USER's first choice (drop the tight gate outright)
  was tried and REFUTED by measurement: greedy re-bricks (smoke d0 5.5650→5.9270 with
  outright losses — the gi=36/84 class exactly). Per the fallback, the heuristic was fixed
  instead: SEARCH nodes bypass the provider gate (`search_risky_live`: Remedy live → emit,
  search judges), `risky_in_hand` loses its per-card narrowing, and
  `SubsetHasUnbackedAltPayload`'s wipe-lethality is SUBSET-level (in-subset direct damage +
  converted ETB/riders + pending attack). Greedy keeps the tight gate; the cast-time re-check
  stays accurate because the wipe casts LAST (rank 30) after the subset's damage has landed.
  Smoke: d0 byte-identical, d3/d5 digest-only at identical avg.

**Doubt-flip measurement with the map (antilife 300g, seed 2002 d3):** budget 10 — doubt
4.3200 vs nodoubt 4.3233 (was +0.067 WORSE, now marginally better); **unbounded — 4.2833 ==
4.2833 with 0/300 games diverged** (the arms play identical games; the USER's "unbounded
should be 0" bar met exactly).

**Suite-wide round 3 (regression mode, 60 keys, both arms vs a fresh base control under this
binary; results envs backed up as `regression.env.bak_depmap_{base,stackdoubt,stacknodoubt}`):**

* Stack WITHOUT doubt vs base: near-neutral — net +0.066, 12 better / 8 worse / 40 same.
  Residual costs antilife (+0.037 d3_s2002, +0.020/+0.012 d5) + hinata mixed + 5c d5 (+0.02);
  gains burn (−0.01 all four) + creature_giving (−0.01..−0.02) + 5c d3.
* Doubt vs nodoubt WITHIN the stack: **antilife clean** (−0.003/+0.007/+0.004 — the +0.067
  arc this doc exists for is resolved), but the flip still costs **fivecolour**
  (+0.04..+0.09, all four configs) and **goblins** (+0.04, all four) — the same dependency /
  m2-emission class on decks not yet dug. `MTG_DOUBT_MAIN2` therefore stays PARKED
  default-off; the antilife method (per-game lever bisect → FSLineTail trace → derive the
  missing edge/emission) is the route for the 5c and goblins digs.

**5c/goblins digs (2026-08-15, commit 1e6ef5e).** Both decks' exemplars fix ONLY under
`MTG_UNPRUNE=mainphase` and are budget-independent — classification/model semantics, not
starvation. Two mechanisms found:

* **Goblins outlet asymmetry (FIXED — mana-infrastructure Main1 pull):** the m2 enumeration
  collects activations from the battlefield only, so a sac-outlet producer (Skirk Prospector)
  cast in the same m2 funds nothing in that plan — the put-token-sac → Goblin King line was
  invisible to every tail (gi=153 3→4). Rule: an activated mana producer usable while
  summoning-sick (`sac_creature_outlet` + `sac_outlet_add_mana_amount > 0`) classifies Main1.
  Measured: gi=153/267 fixed, none regressed, goblins doubt gap +0.0433 → +0.0367; smoke
  36/36 byte-identical.
* **SYSTEMIC (RESOLVED 2026-08-15 — shape (c) won; the early-stop hypothesis is REFUTED as
  the driver): the first-verified-win early-stop's ladder premise breaks under the collapse
  ONLY when a model gap makes shallower-pass verification fail.** `FSLineWin`/`FSLineTail`
  commit the FIRST verified win at the pass horizon edge, justified by "every shallower pass
  was a complete refutation" — a real lock-out was read off `MTG_M2T_TRACE` (goblins gi=153
  at d4: Prospector's win-4 locked out Lackey's win-3). But digging the remaining 11 goblins
  + 15 fivecolour doubt-worse games found each was DOWNSTREAM of a classification gap; with
  the gaps repaired the ladder premise holds again and NO search change was needed (the
  early-stop — wall-clock load-bearing everywhere — is untouched). The two gaps, both
  ClassifyMainPhase, both the dependency-map family:
  - **Creature default skipped the haste-now test (goblins gi=42, closed ALL 11 games):**
    custom-template creatures (token makers — Mogg War Marshal) fell to `default:` → doubt →
    Main2, skipping the vanilla/dork case's "can the body attack NOW" test. With a live
    haste lord (Goblin Chieftain) a pre-combat Marshal is two hasty lord-pumped bodies;
    deferred, the kill slips a turn. Fix: creature-typed cards in `default:` get the same
    scaling-attacker + haste-now test before the doubt deferral.
  - **Loyalty form of the battlefield-only-activation asymmetry (5c gi=1, closed 14 of 15):**
    plan-collected activations come from the battlefield, so a planeswalker cast within an
    m2 plan can never activate in that plan — Jared Carthalion's same-turn +1 (a 3/3 Kavu)
    slips a whole turn, and doubt's T3-m2 Jared line verified win-5 while nodoubt's m1 Jared
    → m2 +1 Kavu verified win-4 (Spider-Man-first then locked in at the d3 horizon edge).
    Fix: `loyalty_abilities` non-empty → Main1 (mirror of the sac-outlet pull).
  **Measured (300g seed 2002 d3 b10 pairs): goblins doubt gap +0.0367 → 0.0000 with 0/300
  diverged; fivecolour +0.0433 → 0.0000 (1 worse / 1 better — gi=15, a T1 fetch-crack
  shuffle-clairvoyance tie-flip, the documented architecture-level class — net zero);
  antilife unchanged (b10 −0.0033, unbounded exactly 0, 0/300 diverged — the USER bar
  stays met). Both nodoubt arms 0/300 diverged vs the pre-fix binary; smoke ALL PASS
  (base byte-identical — both pulls are lever-gated).** Dig instruments:
  `MTG_M2T_TRACE`/`MTG_FSW_TRACE` (+`_TURN`; m2t now tags non-cast action kinds, e.g.
  `k7<X>` = SacForMana).

**Residual-key analysis COMPLETE (2026-08-15, "have we analyzed all of the remaining
losses?"). Every remaining doubt-worse game across the whole suite is attributed:**

* **goblins gi=219 s3003 (3→4 both depths) — FIXED, third battlefield-visibility pull:**
  a creature whose ETB mass-puts creatures (Muxus `etb_reveal_put_subtypes`) classifies
  Main1 — the put board can attack the same turn (the reveal held the haste lord; the m1
  Muxus attacked for 43 on the spot) and is visible to the m2 plans. Deferring can never
  help. Goblins s2002 stays 0/300 under the pull; nodoubt arms unchanged.
* **goblins gi=149 s3003 (3→5 both depths) — FIXED 2026-08-15: ECHO TIMING lockstep bug
  (rules fix, base-affecting), NOT classification and NOT payment optimism (first attribution
  superseded by the deep dig).** Chain of evidence: `MTG_FD_ORACLE` flags predicted 3 @T1 /
  realized 5; `MTG_BP_TRACE`+`MTG_FD_TRACE` show the committed T3 `Muxus` cast FAILING by
  exactly 1 mana in the executor while paying in the apply replay; `MTG_SAC_TRACE` (new
  instrument) shows IDENTICAL sac victims both worlds; the 12-card libtop diff isolates a
  single one-card draw transposition (#19 Warchief ↔ #29 Mountain). Root: the ROLLOUT
  resolves echo (CR 702.29) in the upkeep BEFORE the draw, the EXECUTOR resolved it at the
  top of the pre-combat main AFTER the draw ("functionally the upkeep timing" — true until a
  lapse's death trigger consumes library cards: Mogg's declined echo dies into a live
  Rundvelt Hordemaster whose impulse exile eats the library top). Pre-draw the exile ate the
  Warchief and the draw was the Mountain (3rd land + staged-Muxus win-3); post-draw the
  Warchief was drawn and Muxus came up one mana short. Fix: `AIEngine::ResolveEchoUpkeep`
  called from `GameEngine::UpkeepTail` (pre-draw, mirroring the rollout's position);
  idempotent backstop kept in TakeTurn for upkeep-skipping resume paths. gi=149 doubt 5→3
  (now matching nodoubt's 3). Base impact goblins-only: smoke d3 3.6533→3.6467 (better),
  d0/d5 digest-only; GT accepted. Side finding fixed the same day: `MTG_FLAG_NONCONV`'s
  verified gate required the full-depth pass, so every shallower-ladder commit evaded the
  detector — relaxed to "win within the committing pass's own horizon".
* **fivecolour gi=15 + gi=29 d5 s2002 (5→6 each; gi=15 also the d3 1w) — the fetch-crack
  shuffle-clairvoyance tie-flip class** (midphase-ordering-audit.md, root-caused 2026-08-15):
  T1 crack-vs-hold at a value-flat node; the arms' libraries diverge from the first fetch.
  Architecture-level (clairvoyant crack comparison); not addressable by classification.
* **antilife gi=112 s3003 (3→4 both depths) — converted-package sequencing tie:** doubt
  spends Invigorate-alt at T2 for 4 damage; nodoubt holds it and assembles the T3
  Aria+Swords lethal. m2-ordering family, 1 game. **antilife gi=36 s3003 (4→5 d3 only) —
  T1 Enlightened-Tutor timing tie-flip** (tutor-to-top reorders the library = the same
  clairvoyance family). Offset by gi=122 s2002 where doubt is 1 game BETTER.

**Post-analysis suite state: doubt-vs-nodoubt residual ≈ +0.04 net over 60 keys before the
Muxus pull, ≈ +0.025 after (goblins s3003 +0.0067 = gi=149 alone; 5c d5 +0.02 = the two
tie-flips; antilife ±0.007 = two ties minus one gain). No unexplained games remain.**
