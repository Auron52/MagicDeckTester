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

**Antilife stack-vs-base digs COMPLETE (2026-08-15 overnight session). Every worse game in
the 300g d3 pairs attributed; three base-affecting fixes landed:**

* **gi=184 s2002 (4→5) — FIXED: verse-blind alt-payload lethal test.** Both arms reach an
  identical T4 (Remedy live, fresh Aria, opp 7); the stack's searched m2 skipped Reverent
  Silence because the per-card lethal formula (`opp_life <= alt + ReadyAttackPower`) missed
  the verse trigger the cast itself fires (6 alt + 1 verse = exactly 7). Fix:
  `VerseDamageFromCast` counted into all three lethal checks (auto-fire close-out, emission
  gate, cast-time re-check). Base benefits too (the greedy m2 harvest uses the same gate).
* **gi=230 s3003 (4→5, surfaced BY the verse fix improving base) — FIXED, two layers:**
  (1) `ReadyAttackPower` read a lone exalted Ignoble Hierarch as 0 power — now counts the
  ALONE shape (`best_single + CountExalted`, the gi=87 lethal-projection precedent);
  (2) `HoldManaSourceForCollapsedMain` benched the lone exalted dork at opp 8 = 1 swing +
  6 Silence alt + 1 verse — the single-exalted release now fires when the chip is
  lethal-relevant (`opp_life <= exalted + FreePayloadKillCeiling`), leaving the motivating
  gi=9 hold (healthy opp, ceiling unreachable) untouched. gi=9/76/36/84/184 all verified
  unchanged.
**HELD-OUT CENSUS DUG (2026-08-16, USER: "evaluate the lost games"). Bucketing all 322
regressed games by severity + transition SEPARATED churn from real defects, and found TWO
base bugs the aggregate had hidden:**

* **Census method (reusable):** per-deck buckets of (+1/+2/+3+) vs (−1/−2/−3+) plus
  win→loss / loss→win, then the base→stack win-turn TRANSITION table. Churn is
  near-symmetric per bucket; a real defect is one-sided. Verdicts: **hinata = churn**
  (5→6:102 vs 6→5:106, 6→7:67 vs 64, 8→9:15 vs 9→8:18, win→loss 20 vs loss→win 27; only
  6→8 is skewed 13:4); **antilife = REAL** (139w/45b ≈ 3:1, dominated by 4→5:53 and
  5→6:33, and d0 is byte-identical on all 8000 games = purely search-side); **goblins =
  REAL** (14 of 18 the same 3→4, at BOTH d3 and d5 with identical game indices =
  depth-independent ⇒ a play rule, not search churn).
* **BUG 1 — phase-blind `pending_atk` → phantom board-lethal short-circuit (FIXED,
  fc08cce, BASE):** `PendingAttackDamage` counts every creature that COULD attack; in the
  POST-combat main the attack already happened, and combat itself can ADD a creature
  (Goblin Lackey cheats a body in from hand, Goblin Chieftain gives it haste). Both
  enumerators fed that phantom to the board-lethal short-circuit, which emits ONLY the
  do-nothing plan and skips the odometer. goblins gi=187: opponent at 1, lethal Lightning
  Bolt in hand, untapped Mountain — the second main enumerated exactly one plan (empty,
  flagged `wins_this_turn`) and passed; T3 became T4. Fix: `pending_atk = is_pre_combat ?
  PendingAttackDamage(state) : 0` at both sites. 6 of 7 dug goblins games closed. Base:
  goblins d0 4.101→4.097 (smoke), 4.140→4.136 (regression). The greedy `Solve` path
  carried it too, so this was never classify-specific — classification only EXPOSED it by
  deferring casts into main 2.
* **BUG 2 — drip sweep spent mana the second main needed (FIXED, 04b13b0, BASE):**
  `TapDripLandsIfUseful` swept leftover Groves in the PRE-combat main; with payloads
  deferred post-combat that stole the {R}/{G} the second main needed for Fiery Justice.
  gi=454: base taps Grove AS PAYMENT and lands exactly-lethal 20 on T4 (10+3+6+1 drip);
  deferred, T4→T5. Now sweeps at the turn's LAST main (both rollout and executor). Base
  neutral-to-better (regression −0.0066). Closes 3 of 10 dug antilife 4→5 games.
* **BUG 3 — the exalted hold-release ignored what the held mana BUYS (FIXED, 6dce43c,
  lever-gated):** `HoldManaSourceForCollapsedMain` released the dorks whenever the board had
  ≥2 exalted and no other attacker, deciding BEFORE looking at the hand. Under the collapsed
  main the attack runs first, so releasing taps the whole mana base and the post-combat dump
  cannot be paid. gi=530: two Hierarchs attacking is worth 2 (and attacking with BOTH
  forfeits exalted entirely) while the same mana casts Fiery Justice for the T4 kill —
  measured at the identical node, base scores `Tainted Remedy` **tail=4** and classify scored
  it **tail=5**. Fix: compute `needs_creature_mana` / `needed_deals_damage` first and gate the
  ≥2 release on the needed cards not being damage payloads. gi=76's release stands (its
  creature-mana-dependent card was a Birds — ramp); gi=9/230/184 unchanged. Base byte-identical
  (all 36 smoke keys).
* **BUG 4 — the dork hold priced a FREE-alt card at its printed mana value (FIXED, lever-gated):**
  `HoldManaSourceForCollapsedMain` decides "is some hand spell affordable only with creature
  mana?" by comparing each card's printed mana value against the non-creature sources. But
  Reverent Silence / Skyshroud Cutter / Invigorate each carry an alternative cost ("rather than
  pay this spell's mana cost, an opponent gains N") that is payable **with zero mana** whenever
  the required Forest is on board — so their printed cost says nothing about what the deferred
  main needs. gi=531: Reverent Silence's printed `{3}{G}` pinned BOTH dorks on T2 **and** T3;
  the classify arm forfeited two exalted swings, cast nothing at all on T3, and its T4 dump
  landed the opponent on exactly **2** life — the two forfeited chips — while base closed at 0
  (4→5). Fix: skip a hand card from the affordability scan when its alt cost is payable now.
  This is a statement about AFFORDABILITY, not about whether firing the alt is wise — the
  payload's own gates (`CanAutoFireAltPayload` / `ShouldEmitRiskyAltPayload`) still decide that.
* **BUG 5 — the dork hold was ALL-OR-NOTHING (FIXED, lever-gated):** any creature-mana-dependent
  hand card pinned *every* dork, even when the deferred cast needed only one of them. Under the
  collapsed main the attack is declared BEFORE the deferred casts, so the blanket hold simply
  deletes the exalted swing. gi=174 T4: opponent on 11, two lands covering `{G}{W}`, two dorks,
  and Fiery Justice (`{R}{G}{W}`, 10 damage under the Remedy) needing exactly ONE dork for its
  `{R}` — the hold pinned both, the turn ended at 1 life instead of 0, and the win slipped 4→5.
  Bisected with a temporary `MTG_TMP_M1` classifier override: forcing Fiery Justice to Main1
  restored the T4 kill, which located the cost in the m2 attack/cast ordering rather than in the
  classification itself. Fix: track `need_creature_src = max(mv − noncreature)` over the
  creature-mana-dependent cards and release the hold once `creature_src − 1 >= need_creature_src`.
  Releasing several dorks is safe because `ShouldAttackWith` gives the lone-attacker slot to the
  lowest-index eligible body, so exalted is never broken. Colour-blind like the count it extends;
  the colour-aware form is the open source tie-break item (see gi=38 / 5c gi=97).
  Both fixes preserve every motivating guard: gi=9 (single-dork chip vs Plague Drone tempo),
  gi=76 (exalted ≥2 release), gi=184, gi=230 (lethal-relevant chip), gi=530 (BUG 3's node).
  Base byte-identical — 36/36 smoke and 60/60 regression keys, 0 play changes — because the
  whole hold path is unreachable unless `CollapsedMainActive` (no provider sets
  `ClassifiesMainPhases()`, so it needs `MTG_PHASE_CLASSIFY`).
* **BUG 6 — an unbacked lifegain RIDER was valued as free damage (FIXED, BASE-affecting):** the
  biggest of the census bugs, and the only one that is a genuine *modelling* error rather than a
  heuristic mis-gate. Fiery Justice reads "deals 5 damage divided as you choose... **target
  opponent gains 5 life**". Both the ranking (`EvalCard`) and the reach/lethality term
  (`Action::direct_damage`) read the printed `damage` alone, so with no lifegain→loss enabler live
  the engine scored it a full **+5 face** when its true swing is **ZERO** — and worse than zero
  once a Grove of the Burnwillows pip pays for it. antilife gi=839: the second main cast it on T2
  with no enabler and the opponent went **20 → 21**, burning the card that made base's T3 exactly
  lethal (3→4). Three coupled changes:
  1. `NetOpponentSwing` — one helper, enabler-aware in BOTH directions: backed, the rider converts
     and the spell swings `damage + gift`; unbacked it swings `damage − gift`. Fed to `EvalCard`
     and to `direct`. Note the correction cuts both ways — a **backed** Fiery Justice was also
     *under*-valued at 5 when it really swings 10.
  2. `SubsetHasUnbackedGiftDamage` — the missing THIRD member of the unbacked-payload family
     (alongside the alt-payload gate and the Swords gate). A subset casting such a spell with no
     enabler live and none in the subset is invalid when the net swing is ≤ 0. It prunes only a
     **strictly dominated** cast (net ≤ 0 can never be reach and can never be lethal), so it
     cannot drop a winning line. Verse damage is counted — casting any instant/sorcery under an
     Aria pings for (counters+1), which can justify the cast on its own — and counting it can only
     make the gate fire LESS, the safe direction for a validity gate.
  3. The hold's affordability scan drops such a card too: once the gate refuses the cast, holding
     a dork to keep it affordable is holding for a cast that provably will not happen. Same for an
     unbacked `etb_opponent_lifegain` permanent (Aria, gifts 10) — gi=215's lone Hierarch was
     pinned on T2 for exactly that, and base's swing-for-1 made its T3 exactly lethal.

  **Measured (BASE, no levers): antilife d0 `5.0880 → 4.9060` on smoke and `5.2390 → 4.9890` on
  regression — and the per-game audit is one-sided, `167 faster vs 2 slower`.** The searched
  depths score IDENTICALLY (4.2800/4.2067/4.3040/4.2120 unchanged, 83 games play-changed at the
  same score): the search could already see past the bad valuation, so this is precisely a
  greedy/leaf-policy fix. Every other deck byte-identical (only antilife carries
  `opponent_lifegain`). GT accepted at smoke + regression.
* **`MTG_MAIN2_DROP` RESOLVED — it is CHURN, not a defect.** The "17 antilife games break on
  m2d" reading was **selection bias**: it counted only games that got worse. Measured properly
  (m2d arm vs cs arm, same binary, all overnight keys): antilife `+0.0090` with **44 worse / 47
  better**, fivecolour `+0.0333` 37w/30b, goblins `−0.0020` 3w/5b. Every large bucket is
  symmetric (antilife 5→4:21 vs 4→5:15; 5→6:11 vs 6→5:9), and the one ONE-SIDED bucket is a
  **win** — 4→3 in 10 games with 0 reverse. Lesson worth keeping: a one-sided-looking family
  extracted from a loser list proves nothing until the reverse direction is counted.
* **CORRECTION (USER, 2026-08-16): "not point-fixable" was too quick — one of them WAS a card
  bug.** See the Oko entry below: `elk_transform` was hardcoded to Food tokens, so the engine
  could not express "turn a 0/1 dork into a 3/3 attacker" at all, and the fivecolour 6→7 family I
  had written off as a search tie-break was a *symptom* of that. **The first question for any
  residual family must be "can the engine even EXPRESS the line base takes?" — before any
  tie-break or churn attribution.** Applying that lens to the antilife residue immediately split
  it three ways (below), one of which converges on an already-open item.
* **THE LAND-CHOICE TIE-BREAK NOW HAS THREE INDEPENDENT EXEMPLARS AND ONE COMMON FIX.** The
  sharpest is **antilife gi=519 (4→5)**: the arms diverge at the T1 LAND. Base plays Windswept
  Heath (a fetch → a green source) and casts Birds of Paradise; the classify arm plays Godless
  Shrine (W/B) and then **cannot cast Birds at all — it has no green source**. The mechanism is
  precise: Birds is a ManaDork, so classification defers it to Main2 and the main-1 pool has
  nothing to cast; with no cast to enable, every land option ties and the choice falls to
  `GreedyLandChoiceIndex`, whose four passes (`untapped+multi`, `untapped+any`, `tapped+multi`,
  `tapped+any`) are **colour-blind about what is in hand** and so prefer the shock dual over the
  fetch. antilife gi=367 is the same shape (Temple Garden vs Windswept Heath at T1).
  This is the SAME defect as the two tie-break candidates already logged — 5c gi=97 (an all-tie
  T1 land pick that colour-starves the green dorks for three turns) and antilife gi=38 (Grove
  gifting a life on a coloured pip) — plus a fourth variant, the hinata d0 `tapped-on-a-free-turn`
  case. One fix addresses all four: **make the land tie-break aware of the WHOLE hand, not the
  phase-filtered pool** — prefer a land that makes an otherwise-uncastable hand card castable,
  then prefer not gifting life, then prefer the tapped land when the untapped one buys nothing.
  Two caveats for whoever implements it: a FETCHLAND produces nothing directly (its colour value
  is its fetch targets, so the test must look through `FetchCandidates`), and this changes BASE
  play for every deck — it is a `heuristic-optimization.md` job (propose variants → sweep train
  seeds → validate on overnight), not a point fix.
  The rest of the antilife residue is NOT this: gi=554 is a same-land different-cast choice, and
  gi=963 is benign (the deferred Birds IS cast in T1 main 2 and the board is identical by
  end of turn — a pure phase-order difference that re-rolls downstream).
* **THE RESIDUAL FAMILIES ARE NOT POINT-FIXABLE BUGS (2026-08-16, every open game dug).** After
  BUG 6 the surviving stack-vs-base losers were traced individually, and they collapse into three
  causes, none of which is a mechanical defect. Recording this so the next session does not
  re-dig them one game at a time — further point-fixing here would be fitting noise.
  1. **Two mana pools solved SEQUENTIALLY (the structural one).** A card base casts in main 1 is
     deferred to main 2; main 1 commits its mana first without knowing what main 2 wants, so the
     deployment drifts a phase — and for a permanent whose value accrues over turns (a mana dork,
     a planeswalker) a phase becomes a turn. antilife gi=8: main 1 spends all three sources on
     Remedy+Invigorate, so the deferred Birds waits until T3. fivecolour gi=41 is the sharpest —
     **CS leaves 2 of 3 mana unused on T3**, casting a 1-mana Deathrite in main 2 when a 3-mana
     Oko was available, and the whole line finishes exactly one turn later. This is the limitation
     already named in the `MTG_DOUBT_MAIN2` comment ("it SPLITS the turn's mana into two pools
     solved separately"); the honest fix is JOINT m1+m2 mana allocation, an architectural change,
     not another gate.
  2. **T1 tie re-rolls.** goblins gi=403 is a first-turn one-drop pick (Goblin Lackey vs Skirk
     Prospector) and fivecolour gi=320 a first-turn land pick; everything downstream is a
     different game. Same family as the land tie-break candidates.
  3. **Leaf preference inside a main-2-only deck.** fivecolour's `deck_feeds_combat` is false, so
     the default branch sends essentially the whole deck to Main2 and the m1 plan pool holds land
     drops only — verified in the `[fsw]` trace, where every T3 main-1 plan is `p=land=...;` with
     no cast at all. That is BY DESIGN and matches the USER's ruling that an empty Main1 on a
     non-combat deck is expected, not a symptom. The 6→7 bucket (7 vs 2) is then the m2 search
     preferring a different card from a differently-composed pool — a valuation preference, not a
     gate misjudgment, and small against fivecolour's overall −0.187 stack gain.
* **STILL OPEN, and now split by cause:**
  - **hinata gi=1061 / gi=1987 (d0, MTG_ACQ_RESOLVE, 6→8) — a THIRD land-choice tie-break
    exemplar, and the sharpest one:** these are **d0** games (no search at all), so per the
    d0-only rule the cause is the executor/greedy path, and both seeds fail identically. The
    lever is not the defect — it only changes *when* Preordain is cast (T2 instead of T3), which
    changes which lands are in hand at the T3 drop. With BOTH a Forbidden Orchard (untapped) and
    a Mystic Monastery (`enters_tapped`) in hand on a turn that casts nothing, the greedy ranker
    played the **Orchard**; T4 then wanted 4 mana for Hinata and the freshly-played tapped
    Monastery supplied 3, so Hinata slipped a turn and the win went 6→8. `GreedyLandChoiceIndex`
    (`src/ai/LandPlay.cpp`) ranks untapped over tapped **unconditionally** — passes are
    `0 untapped+multi, 1 untapped+any, 2 tapped+multi, 3 tapped+any` — so it cannot express
    "play the tapped land on a turn the extra mana buys nothing", which is a standard play rule.
    Deliberately NOT point-fixed: the obvious cheap tests do not work here. The turn was **not**
    idle by mana value (Remand `{1}{U}` and three X-spells at X=0 were nominally affordable), so
    a "nothing castable" gate never fires, and a "does the extra mana make something new
    affordable" gate is tripped by Distorting Wake at X=0. Getting it right needs "would the
    policy actually TAP that source this turn" — the same *what does the mana buy* shape as
    BUG 3/5, but on the land drop. That belongs in the heuristic-optimization loop (propose
    variants → sweep → validate), not a point fix, and it would change BASE play at every depth,
    so it needs a full GT rebaseline. Note both `AIEngine::TryPlayLand` and TurnSolver's
    `greedy_land_name` share this one ranker, so a single edit keeps them in lockstep.
  - **gi=38 — drip-land TIE (not a bug, a tie-break candidate):** the arms diverge at T1,
    base playing Bloodstained Mire while classify plays Grove of the Burnwillows and taps it
    for a {G} pip — **gifting the opponent 1 life** with no Remedy live to reverse it (opp 21).
    A specific coloured pip must tap Grove's coloured mode, so the gift is unavoidable ONCE
    the land is chosen; the fix belongs in the land-choice tie-break ("prefer a source that
    does not gift life when the alternatives score equal"). Same shape as 5c gi=97's
    colour-aware tie-break candidate. The fetch-vs-Grove choice also reshuffles, so everything
    downstream is a different game (clairvoyance family).
  - ~~**gi=174 / gi=531**~~ — ROOT-CAUSED and FIXED as BUG 5 and BUG 4 above. Both now match
    base at classify (4/4); gi=174 matches under the full stack too, and gi=41 came along.
  - ~~**gi=514 / gi=545 / gi=531-at-stack**~~ — CLOSED: the "later levers" family was
    MTG_MAIN2_DROP, now measured as churn (see above), and all three match base at classify.
  - ~~**hinata's skewed 6→8 bucket (13 vs 4)**~~ — RESOLVED: 2 of the 13 are the d0 land-ranker
    exemplar above; the other 11 have NON-MONOTONE lever ladders (6/8/7/7/8, 6/7/7/6/8,
    6/8/6/6/8), the churn signature — a real defect does not un-break and re-break as levers
    stack. Consistent with the earlier hinata verdict, and with hinata's w/b being byte-identical
    across three separate stack measurements.
  - ~~**goblins gi=403**~~ — a T1 one-drop tie (Lackey vs Prospector); see the residual-families
    note above.

  **Method note (reusable): `MTG_UNPRUNE=mainphase` is the first question to ask.** For both
  gi=174 and gi=531 the regression survived a **100× budget increase** (10 → 1000 virtual-ms,
  identical result), which rules out search truncation and says a *prune* is dropping the line —
  exactly the unbounded test the USER's no-lossy-truncation bar calls for. `MTG_UNPRUNE=mainphase`
  then recovered both, and the follow-up that forcing *every nonland* to Main1 recovered only
  gi=174 is what split the two causes: gi=174 was the per-card classification's downstream
  ordering, gi=531 was one of the OTHER things the same gate switches on (the collapsed-main
  dork hold), which no amount of re-classifying could reach.

**FINAL HELD-OUT A/B (2026-08-16, after BUG 4/5/6 — GT rebaselined AND the stack arm re-run, so
both arms are the same post-BUG-6 binary). NET `−0.1443` over 144 keys, 95 byte-identical.**
The arc's trajectory on this one number: `+0.0330` → `−0.0148` (BUG 1/2/3) → `−0.1333`
(BUG 4/5) → **`−0.1443`** (BUG 6).

| deck | sum gap | reading |
|------|---------|---------|
| fivecolour | −0.187 | 219w/280b |
| burn | −0.120 | 15w/135b — one-sided WIN (5→4 in 118 games vs 10 reverse) |
| creature | −0.075 | 7w/65b — one-sided WIN (5→4 in 40 vs 3) |
| goblins | +0.002 | 6w/4b |
| antilife | +0.053 | 81w/40b (was +0.064, and +0.110 at the start of the census) |
| hinata | +0.182 | 255w/254b — w/b byte-identical across FOUR separate measurements = churn |

Remaining one-sided LOSS buckets, all characterised above and none point-fixable: antilife 4→5
(31 v 12) and 5→7 (7 v 1), fivecolour 6→7 (8 v 2), hinata 6→8 (13 v 4).

**HELD-OUT A/B AFTER BUG 4 + BUG 5 (2026-08-16, stack arm re-run over all 144 overnight keys;
base arm unchanged because both fixes are lever-gated and the regression suite measured 60/60
keys byte-identical). NET `−0.1333` (was `−0.0148`) — a further `0.119` improvement on held-out
seeds, 95/144 keys byte-identical. No deck got worse:**

| deck | sum gap | (was) | games w/b | reading |
|------|---------|-------|-----------|---------|
| fivecolour | **−0.187** | −0.089 | 219w/280b | **biggest winner** — 5→4 in 181 games vs 4→5 in 108 |
| burn | −0.120 | −0.120 | 15w/135b | untouched |
| creature | −0.075 | −0.075 | 7w/65b | untouched |
| goblins | +0.002 | +0.002 | 6w/4b | untouched |
| antilife | **+0.064** | +0.085 | 94w/45b | the motivating deck — 18 worse games closed |
| hinata | +0.182 | +0.182 | 255w/254b | byte-identical w/b — confirms it is pure churn |

**The headline is that an ANTILIFE dig improved FIVECOLOUR ~5× more.** `HoldManaSourceForCollapsedMain`
is a `DecisionProvider` base-class rule, not an archetype override, so it governs every deck that
plays a collapsed main with mana creatures — and fivecolour, with the most dorks and the most
colour-hungry casts, was paying the blanket hold on far more turns than the deck the bug was found
in. Worth remembering when triaging: a lever-gated regression that looks archetype-specific may
sit in shared code.

Residual one-sided buckets (the next digs, both NEW to this table):
* **antilife 4→5: 32 vs 14 reverse** — still ~2:1, so a third cause remains in the antilife family.
* **antilife 5→7: 7 vs 2**, **fivecolour 6→7: 8 vs 2** — small but one-sided.
The big fivecolour 5↔6 (101/94) and antilife 5↔6 (23/15) buckets are symmetric = churn.

**HELD-OUT A/B RE-RUN AFTER THE THREE FIXES (2026-08-16, both arms rebuilt): the stack is now
NET BETTER on held-out seeds — `−0.0148` summed per-key gap over 144 keys (was `+0.0330`),
95 keys byte-identical (was 92). The two BASE fixes also improve the base arm itself: 24 of
144 keys moved vs the accepted overnight GT, net `−0.0075`.**

| deck | sum gap (was) | games w/b (was) | reading |
|------|---------------|-----------------|---------|
| burn | −0.120 (−0.120) | 15w/135b | unchanged one-sided win |
| fivecolour | −0.089 (−0.078) | 241w/269b | churn, net better |
| creature | −0.075 (−0.075) | 7w/65b | unchanged one-sided win |
| goblins | **+0.002** (+0.014) | **6w/4b** (18w/4b) | phantom board-lethal fix landed |
| antilife | **+0.085** (+0.110) | **112w/41b** (139w/45b) | drip + hold fixes landed |
| hinata | +0.182 (+0.182) | 255w/254b | untouched — pure churn, as diagnosed |
| 6 others | 0 | 0w/0b | filter stands down |

Antilife's remaining +0.085 is still ~3:1 one-sided, so more of it is real; hinata's exact
non-movement is itself confirmation that its 255w/254b really is symmetric churn and not a
defect the fixes would have touched.

**HELD-OUT A/B (pre-fix baseline, 2026-08-16): the full stack (`MTG_PHASE_CLASSIFY + MTG_SEARCH_
SECOND_MAIN + MTG_MAIN2_DROP + MTG_ACQ_RESOLVE + MTG_BP_SITES=63`) measured on ALL 144
overnight keys (base arm = the accepted overnight GT under the same binary; stack arm =
one pooled 144-job batch, 20/20 workers, ~40 min). NET +0.0330 summed per-key gap over
144 keys, 92 keys byte-identical — essentially neutral, and consistent with the train-seed
attribution (clairvoyance churn, no systematic defect):**

| deck | sum gap | games w/b | reading |
|------|---------|-----------|---------|
| burn | **−0.120** | 15w/135b | one-sided REAL improvement |
| creature | **−0.075** | 7w/65b | one-sided REAL improvement |
| fivecolour | −0.078 | 243w/267b | churn, net better |
| hinata | +0.182 | 255w/254b | symmetric churn, slight net worse |
| antilife | +0.110 | 139w/45b | the known clairvoyance families |
| goblins | +0.014 | 18w/4b | small residual |
| auras/dragonstorm/knights/mirrorwing/slivers/th | 0 | 0w/0b | filter stands down (uses_second_main) |

The burn and creature improvements are one-sided (9:1 and 9:1 better:worse), i.e. genuine
searched-second-main wins, not tie re-rolls. Adoption of the stack remains the USER's call;
this table is the held-out evidence base.

**5c + hinata churn VERIFIED (2026-08-16, "let's verify those remaining games"). Re-measured
all 8 hinata/5c d3+d5 stack-vs-base keys under the post-fix binary (two pooled batches):
5c d3 now net BETTER both seeds (−0.010/−0.005, 12w/15b + 17w/18b); 5c d5 +0.02/+0.02
(9w/7b, 7w/6b); hinata d3 +0.02/+0.03; hinata d5 +0.06/−0.02. Sum +0.115 over 8 keys.
Every big mover and a 3-game sample of the 1-turn flips dug — ALL land in the
clairvoyance / re-roll family, no heuristic-gate misjudgment found:**

* **hinata gi=33 d5 s2002 (6→LOSS under full stack):** lever ladder 6→7(cs)→7→8(acq)→9(bp63)
  = per-lever re-rolls. At the cs T1 root's m2 node the search STRICTLY prefers holding
  Ponder (pass sub=5 vs Ponder sub=6 — determinized clairvoyant preference, no gate
  involved); the unverified win-5 projection drifts on re-solve (MTG_FLAG_NONCONV silent:
  projection was heuristic-leaf, not a verified win). The stack's 9 is a re-rolled tail
  outcome, mechanically identical to antilife gi=245.
* **5c gi=97 d5 s3003 (5→7, m2drop trigger):** T1 land choice Blood Crypt vs Zagoth Triome
  is an ALL-TIE at the deepest fsw pass (every land + pass tail=5); the committed Crypt
  line colour-starves the green dorks for 3 turns (Tender/Birds/Elder held, no G source,
  T3 heath uncracked) and drifts to 7. Tie-break could be colour-aware ("play the
  colour-completing tapped land on the free turn") — noted as a candidate heuristic-
  optimization item, NOT a defect (the search scored the options equal).
* **hinata gi=40 d3 s3003 (4→6):** non-monotone ladder (4→5→5→4→6) = re-roll churn.
* **Samples 5c gi=11 d5, hinata gi=140 d3, hinata gi=37 d3:** same-turn m1→m2 deferral is
  neutral on its own; the 1-turn losses come from a later clairvoyant hold (gi=11: Oko held
  T2, cast T3-m2, fetch-crack timing re-rolls the library) or are non-monotone (gi=37).

* **gi=245 s3003 (6→LOSS) — rollout-mediated shuffle clairvoyance, accepted:** hand of
  three fetches; at the stack's T1 root EVERY option ties at 9 (the levered rollout's own
  fetch timing shuffles differently and finds no win) while base's rollout finds a
  clairvoyant win-5 via holding all lands two turns. The post-divergence loss (two
  gift-Arias, no Remedy/instants drawn) is an artifact of the divergent shuffle, not a
  play defect. Same family: gi=29/58 (T1 land tie-flips), gi=118, gi=254, gi=3, gi=96
  (tutor-timing flip). gi=3 closed itself under the verse fix (tie landscape shifted).
