# Analysis ledger — Pirates

Per-deck ledger for the `analyze-deck` workflow (see `.claude/skills/analyze-deck.md`, "The per-deck
ledger"). This file is the durable state a resumed session or a second machine reads to continue;
context is disposable, this is not.

**Deck:** `decks/Pirates/Pirates.cod` — U/R/b Aether Vial Pirates tribal, 60 cards, no sideboard.
**Started:** 2026-09-26. **Branch:** `phase-1-2-deck-analyzer`.

## The list

| n | card | status at Stage 1 |
|---|---|---|
| 4 | Daring Buccaneer | missing |
| 4 | Corsair Captain | missing |
| 2 | Kitesail Larcenist | missing |
| 1 | Malcolm, the Eyes | missing |
| 4 | Staunch Crewmate | missing |
| 4 | Adaptive Automaton | missing |
| 4 | Goblin Tomb Raider | missing |
| 4 | Metallic Mimic | missing |
| 4 | Dire Fleet Captain | missing |
| 2 | Forerunner of the Coalition | missing |
| 2 | Siren Stormtamer | missing |
| 4 | Spirebluff Canal | missing |
| 1 | Blackcleave Cliffs | missing |
| 4 | Aether Vial | implemented |
| 5 | Mountain | implemented |
| 4 | Secluded Courtyard | implemented |
| 4 | Unclaimed Territory | implemented |
| 2 | Fiery Islet | implemented |
| 1 | Lightning Bolt | implemented |

Every "choose a creature type" in the deck (Metallic Mimic, Adaptive Automaton, Secluded Courtyard,
Unclaimed Territory) is **Pirate**.

## Stage 1 — coverage (2026-09-26)

13 missing, 6 `full`. Bracket notes on the implemented cards:

| card | bracket note | classification |
|---|---|---|
| Aether Vial | AI heuristic for charge/activation | not a simplification — the shared root Vial policy (`GenericProvider::WantVialCharge`); disclosed in 6a |
| Secluded Courtyard | type choice simplified to "any creature" | exact here: every coloured-cost creature is a Pirate; the colourless Mimic/Automaton need only generic |
| Unclaimed Territory | "ETB choice and color restriction not modelled" | **note is stale/wrong**: `colored_creature_only` IS set. Exact for this deck (see Stage 2) — note reworded, behaviour unchanged |

## Stage 2 — implementation

Per-card research fanned out to 11 Opus agents (one per card / pair) on 2026-09-26; drafts kept under
`logs/pirates/drafts/` (gitignored scratch). Integration is serial in a scratch worktree off the origin
tip (the main tree held another session's uncommitted discard-gate WIP).

| card | tier | mechanism (new params in **bold**) |
|---|---|---|
| Spirebluff Canal, Blackcleave Cliffs | 1 | existing fastland (`fastland_max_other_lands`) |
| Siren Stormtamer | 1 | vanilla 1/1 flier; activation unmodelled (PROVISIONAL, see below) |
| Staunch Crewmate | 2 | existing `etb_dig_*`; dig filter now matched by `CardMatchesTypeName` so `"Artifact"` works |
| Dire Fleet Captain | 2 | Piledriver attack pump + **`attack_pump_tough_per_other_matching`** |
| Corsair Captain | 2 | `lord_effect` + **`etb_creates_treasures`** |
| Goblin Tomb Raider | 2 | **`static_artifact_threshold/power/tough/haste`** conditional static + conditional haste |
| Forerunner of the Coalition | 2 | Giant Harbinger tutor-to-top + **`own_creature_enters_opp_life_loss`** |
| Malcolm, the Eyes | 2 | **`nth_spell_trigger_n` / `nth_spell_investigate`** (Clue) |
| Metallic Mimic, Adaptive Automaton | 3 | **`chosen_type_added_to_self`**, **`other_chosen_subtype_enters_counters`**, **`lord_affects_chosen_subtype`**: the chosen type (DominantCreatureSubtypeId = Pirate) is appended to the BATTLEFIELD permanent's subtypes only |
| Daring Buccaneer | 2 | **`reveal_or_pay_subtype/cost`** in EffectiveSpellCost + an order-aware per-subset surcharge in the enumerator |
| Kitesail Larcenist | 3 | **`etb_treasurify_each_player`**: in-place rewrite to a Treasure, searched own-side target, new `treasurify` viewer decision |

### Pre-existing bugs found in OTHER decks during research (verified 2026-09-26)

1. **Keyword mask overflow** — `Keyword` has 35 values, `Card::m_keyword_mask` was `uint32_t` with
   `1u << k`: Persist/Evoke/Convoke were UB and on x86 aliased Haste/Flying/Trample (probe: adding
   Persist stores mask `0x1`). **Kitchen Finks and Murderous Redcap (Melira Pod) had phantom haste.**
   Pirates needs `Ward`, so the fix is on the critical path. Own commit; Melira Pod GT re-accepted.
2. **Felidar Guardian decline divergence** — `AIEngine` only stores `chosen_x` on the stack entry when
   `> 0`, so a searched DECLINE (0) resolves in the executor as `-1` -> provider `FlickerTarget`, while
   the rollout declines. Executor/rollout divergence (Melira Pod).
3. **Oko's Elk keeps its old definition** — `elk_transform` renames to "Elk" but never clears the cached
   `Card::m_def`, so `LookupCached` still returns e.g. Birds of Paradise's definition (FiveColour).

## Stage 4 — baseline profile (2026-09-26)

`analyze_deck.py --no-rebuild`: 19 card scores (1000 games, d5), default static keep (min_lands 1,
max 5, no hand-score gate), no mulligan flags. Cost diagnostic NO_COST_INTERACTIONS. Discard analysis
**DISCARD_INERT** — over 400 d3 games no cleanup shed is reached by either the real game or the rollout,
so the authored bucket policy (see "Discard policy") is currently unexercised; it exists for the gate
and for off-curve hands. Provider routing: Pirates -> `PiratesProvider` (chunk 3).

## Stage 5 — verification (2026-09-26)

* **Box note:** a sibling container held the box at load ~24/24 all session; budgets are VIRTUAL
  (`SearchBudget::FromVirtualMs`), so results are unaffected — every run here was capped at 12 threads
  and serialized one at a time.
* **verify_deck:** coverage PASS (19/19 full), card_fields PASS, viewer PASS, viewer_wiring PASS (dig,
  target, treasurify, vial_charge), mismatch PASS (no nonconv / fd-diverge, seeds 7001-7002 x 60, both
  arms), play_invariants PASS. card_costs FAIL is **pre-existing and not Pirates**: two Fungus cards
  (Brightcap Badger, Fungus Frolic) whose Scryfall cost is a two-part `{3}{G} // {2}{G}` — left for
  the Fungus owner; NOT signed off here. claude_sweep: recorded below.
* **Depth sweep (5b), paired per game from the suite runs:** d5 vs d3 — 0 worse / 1 better over 225
  paired games (smoke s1001 + regression s2002/s3003); d0 ≈ +0.3 turns vs d3. Monotone; clock ~T4.5
  is plausible for this curve.
* **Horizon-honest tie-break (5c2):** 0 changed games of 60,000 paired at PLAY settings (d5 b20,
  provider Pirates; 12 blocks + 48 fresh blocks). The leaf publishes and flips ties (1.86M publishes,
  90k flips in the Stage 4 run) but never moves a win turn — the deck wins well inside the horizon.
  Verdict: unbindable -> **keep default ON** (costs nothing).
* **Fresh-hold A/B (Corsair's same-turn Treasure):** `MTG_PAYSAC_FRESH_HOLD=0` vs default, 3000
  paired games at play settings: **delta 0.0000** (0 better / 0 worse). The lever fires (1 of 300
  games plays differently at the action level, same win turn) but a same-turn Treasure never buys
  a turn here — within a plan an ETB-minted Treasure is not counted anyway, and by the next decision
  the leftover hand rarely has a 1-drop that changes the clock. The narrowing is measured-inert for
  Pirates; the PROVISIONAL deferral stands on that evidence.
* **Suite:** Pirates added to smoke (d0 1000 / d3 150 b10 / d5 75 b20 @1001 + pirates2hg d3 50) and
  regression (same counts @2002/@3003). Measured in the tier: d3 ~0.73 s/game, d5 ~1.3 s/game, no
  SLOW-GAME. Overnight tier NOT added yet (adding rows without baselining that tier strands NEW keys —
  the Fungus precedent); sizing would mirror Giants.
* **GT movement from this branch's engine fixes:** only FiveColour moved (Oko Elk `Card::Rename`
  fix). Searched: slower 2 / faster 2 in regression, 0 / 1 in smoke; d0 slower 2. The slower
  searched game (s3003 gi53, 5->6 at d3 AND d5) was traced: the OLD binary Elked Deathrite Shaman on
  T3 and still cast Cosmic Spider-Man ({W}{U}{B}{R}{G}) on T5 from 4 lands — the fifth mana came from
  the Elk tapping via Deathrite's retained definition, i.e. the phantom the fix removes. The new
  binary's T6 holds at b10/b100/b1000/b5000 (structural, not churn). Verdict: correctness, accepted.
  Melira Pod (keyword-mask + Felidar fixes) stayed byte-identical at suite seeds; both fixes are
  pinned by unit tests that fail without them.
* **Viewer protocol check:** 23 ok / 301 repaired -> 20 ok / 304 repaired (0 play-drift, 0 enum-gap,
  0 contract-fail). The 3 moved refs are all Giants (`claude_s1_gi0`, `claude_s3_gi2`, `claude_s8_gi7`):
  same win turn, one main-phase menu index shifted +1/+2 each — sweep fix A's new decline plan for an
  optional tutor (Giants' Harbinger) inserted into the human plan list. Content-anchored repair, not drift.
* **GT ACCEPTED** (smoke + regression, binary built from `62f8f5ac`): FiveColour keys moved per the
  verdict above; Pirates keys NEW. `check_gt_logs.py`: 515 consistent, 0 stale.


## Approved deferrals

USER review 2026-09-26 ("D5 is okay if there is something to reveal. D3 is definitely wrong and should
be fixed. D6 is also wrong ... The other deferrals are okay."):

APPROVED:
- **D1 Siren Stormtamer** -- "{U}, Sacrifice: counter target spell or ability that targets you or a
  creature you control" unmodelled: never legally activatable (the passive opponent never targets), and
  nothing in the 60 has a death payoff.
- **D2 Kitesail Larcenist** -- the "for as long as it remains" revert (it cannot leave the battlefield in
  this list); opponent-side target auto-declined in autonomous play (spawns never block, nothing reads
  them; human can still pick); rename to "Treasure Token" (rules keep the name; inert here); flying/ward inert.
- **D4 Chosen creature type** (Mimic / Automaton) not surfaced in the viewer: deck-constant Pirate weakly
  dominates every alternative (every other creature is a Pirate).
- **D5 Daring Buccaneer** reveal-vs-pay auto-resolves to reveal **only when a reveal is possible**
  (the user's condition) -- verified in code: `CanRevealForAdditionalCost` (SpellEffects.h) requires
  another Pirate CARD in hand (Mimic/Automaton in hand do not count), otherwise `{2}` joins the raw
  cost (ManaPayment.cpp) at every cast site.

REJECTED -> FIXED:
- **D3 Corsair Captain's same-turn Treasure** -- the global `MTG_PAYSAC_FRESH_HOLD` doctrine (a USER
  ruling about mana-negative Gold Rush) banked Corsair's free ETB Treasure, and the enumerator never
  credited an ETB Treasure to later casts in the same plan. Being fixed: see "D3 / D6 fixes" below.
- **D6 (gi11) ETB-dug card cast the same turn** -- the user's understanding was right: the put-in-hand
  breakpoint should open. It did not, because `ParamKeyedDrawClass` claimed `etb_dig_count` (site 10
  stood down) while no param-keyed site armed. Fixed by `MTG_BP_ETB_DIG` (default ON); see below.

## D3 / D6 fixes (2026-09-26, after the user's deferral review)

* **D6 -- `MTG_BP_ETB_DIG` (default ON), `d4ef1f81`.** `TurnSolver::ParamKeyedDrawClass` claimed
  `etb_dig_count`, so the general put-in-hand rule (site 10) stood down after an ETB dig in both worlds,
  while the only param-keyed dig site had its arming removed on 2026-08-19 -- claimed, never armed.
  Removed from the claim list (site 10's outcome-keyed arming now fires, rollout + executor in lockstep)
  and named in site 10's `PlanOpensBreakpoint` routes so the continuation is fanned. gi11 (s777011): T5
  -> T4 on Claude's line. Canon audit, 40 Pirates games d3 b10: 428 site-10 canon defaults, 0
  unchallengeable. Unit: `test_etb_dig_breakpoint.cpp` (fails with `=0`). Open edge: a Vial-put digger
  (the `MTG_BP_HAND_ENTRY` hole, default OFF). Affects Knights (Acclaimed Contender) and Pirates only.
* **D3 -- `MTG_ETB_TREASURE_SPEND` (default ON), `3d9508c5`.** Two halves: (a) `Permanent::
  fresh_hold_exempt`, set only on an `etb_creates_treasures` Treasure and a Larcenist-converted
  permanent, honoured by `PaySacSpendableNow`; folded into memo/fungibility/dominance/mana-cache keys
  only when set; cleared at both untap sites. The Gold Rush doctrine (`FreshMintSpendableNow`) is
  untouched -- Mirrorwing byte-identical (20 games d3, flag on/off/parent). (b) the ETB Treasure is
  stamped as `Action::rock_mana.wild` on the Corsair cast (and a Vial put), so every rock-credit
  consumer (Solve/EnumeratePlans twins, odometer gates, `FirstUnpayablePos`, the real-payment
  simulations with an exempt scratch Treasure) credits it to LATER casts, never its own cost; a new
  `ApplyEtbTreasureFundingOrder` hoists the Corsair only when the sorted order cannot pay, at every
  cast-order sort site in both worlds. Unit: `test_etb_treasure_spend.cpp` (6 cases; ablation-checked).
  Probe: 200 games s5000 4.52 -> 4.48 (8 faster / 0 slower), `MTG_FD_ORACLE` 0 divergences. Open:
  viewer summary order may list the 1-drop before the Corsair; puts-last Vial variants in searched-order
  plans are not mana-pruned (only labelled); the puts-last total-mana check is colour-blind.

* **Exposed by D6 -- `MTG_BP_RECORD_VIAL` (default ON), `7ef0734b`.** The first D6 suite run moved Knights
  WORSE (smoke d3 +0.012, d5 +0.013, 2hg +0.013; regression +0.007..+0.012), every slower game persisting
  at 16x. Root cause (subagent trace, `MTG_FD_ORACLE`): `apply_vial` never recorded a Vial put applied
  inside a breakpoint CONTINUATION into the committed script, so the rollout credited a creature the
  executor's replay never deployed. Contender's continuation usually has only the Vial left to deploy
  the dug Knight, so opening its breakpoint made the phantom common (gi114/gi215/gi88/2hg gi29, each
  predicted T4 realised T5). Fixed by recording it; `MTG_FD_ORACLE` on 150 Knights games d3: 2
  divergences -> 0; all four games recover.
* **Suite, final binary (`7ef0734b`), vs the `bdf1df5e` GT:** smoke searched 1 slower (pirates2hg gi21,
  churn: recovers at 4x) / 12 faster; regression searched **0 slower / 35 faster**. Pirates better at
  every key (regression d3 4.547->4.480 / 4.540->4.487, d5 4.547->4.480 / 4.560->4.453, d0
  4.907->4.872); Knights equal or better (knights2hg regression 5.010->4.980); Goblins d3/d5
  play-changed at identical aggregates (the Vial record; no slower game). d0 (lighter bar): smoke 4
  slower / 52 faster, regression 5 / 40 -- the sampled slower d0 games are variance (draws diverge) and a
  d0 mispricing of spending Corsair's Treasure while Goblin Tomb Raider needs an artifact (gi87) -- a
  legal line the greedy d0 undervalues, not a rules defect. GT accepted on this binary.

## Discard policy

**Status: USER-REVIEWED and revised 2026-09-27 (see "Discard policy -- USER-REVISED" below)** (adoption is a user review — the same
gate as cast order; nothing here records a confirmation). Full proposal:
`docs/design/pirates-discard-policy-proposal.md`. Code: `PiratesProvider::CleanupDiscardCandidates`
behind `MTG_PIRATES_BUCKET_DISCARD` (default ON, `=0` restores the generic max-MV ranking).

* Shape: simple aggro → **two buckets**, MANA (lands + a Vial sub-role) and THREATS (catch-all).
* MANA / LANDS: reach **4 sources** net of board; the first two land slots before the threat floor,
  the rest after. Colour coverage first (creature-only lands cover creature pips, never Bolt's),
  then breadth; a Fiery Islet is the last surplus land shed.
* MANA / VIAL: **1** only while no Vial is on board and the board has ≤ 2 lands; else surplus.
* THREATS: hard floor **3**; overflow shed FAR-first (distance ≥ 2 incl. a missing colour; a board Vial
  or Buccaneer's reveal erases it), then by a param-derived value: lords 100–104 > Mimic 92 > Dire
  Fleet Captain 86 > Forerunner 82 > Malcolm 75 / Larcenist 72 > Crewmate 62 > Bolt 56 > 1-drops ~38–46.
* Shed order: dead legend → surplus lands → surplus Vials → overflow threats → quota tail (every hand
  card named; no max-MV fall-through).
* Gate: the main tree's `scripts/discard_policy_audit.py` classifier (run against this worktree via a
  symlinked scratch root under `logs/daudit/`) reports **Pirates → OK** (not MISSING / PATCH-ONLY /
  INHERITED).
* Doubts for the user (§8 of the proposal): land target 4 vs 3; the Vial rule; the 1-drop order; Bolt's
  slot; floor-before-late-lands; fastlands not demoted; Treasures not counted.
* Evidence: `--discard-analysis` ran after Stage 4 -> **DISCARD_INERT** (no cleanup shed reached in 400 d3
  games, real or rollout; see Stage 4). Non-inferiority vs `=0` is therefore vacuous (no game can differ).

## Cast order (USER review 2026-09-27) -- ADOPTED (default ON), with the payable-order fallback

The user reviewed proposals 2a-2d and dictated a full order (quotes in the `PiratesProvider::CastOrderRank`
header note): Mimic 1 ("Mimic should be before Buccaneer"), Malcolm 2 ("after the mimic ... the +1/+1
counter with haste is worth the cost"), Corsair 3 when Buccaneer keeps a reveal else 5 ("before the
rest as the treasure needs to be usable ... even before Buccaneer if there is another pirate in hand"),
Buccaneer (reveal live) 4, Forerunner 6, Staunch Crewmate 7 ("after all of those that care about the
order"), then a full order "for the sake of it": Automaton 8, Dire Fleet Captain 9, Larcenist 10, Aether
Vial 11, Buccaneer without reveal 12, Goblin Tomb Raider 13 (after the Vial, so it enters hasty),
Stormtamer 14, Bolt 20. **2d dropped**: every creature a Vial can put is its own searched ActivateVial
action, so there was no heuristic to change. Lever `MTG_PIRATES_CAST_ORDER` (default OFF), `1d353030`.
Train (smoke + regression, Pirates cases): searched 0 slower / 1 faster, d0 0 / 11. Held-out s60000:
d0 4.8325 -> 4.8305 (2000 g), d3 4.4517 -> 4.4500 (600), d5 4.4467 -> 4.4450 (600). Non-inferior at every
depth. Recommendation: adopt (flip default ON) BEFORE the value leaf freezes a commit.

**Revision + adoption (2026-09-27).** Malcolm follows Corsair's reveal rule, walked in rank order (USER:
"malcolm, the eyes should also do the same things as Corsair Captain ..."; with {Malcolm, Corsair,
Buccaneer} Malcolm goes early and the Corsair waits). `MTG_PAYABLE_ORDER` (USER: "it really is just for the
'fail to pay' case"; "If we have mana for all of them, then there is no need to change the order"): the
default order is kept unless it projects unpayable (reveal-aware), then the Corsair hoist, then the nearest
payable permutation; it also removed an over-pricing in `SameSubsetRevealSurcharge`. Held-out s60000+s80000:
no-order >= order >= order+fallback in all 12 cells. Both levers default ON. Suite vs the previous GT: only
Pirates moved; searched 0 slower / 1 faster; d0 2 slower / 16 faster -- the 2 d0 games are PRE-EXISTING d0
scorer gaps the (correct) fallback exposes (no same-turn lord credit, no Forerunner drain credit):
`docs/design/d0-greedy-scorer-lord-and-drain-credit.md`. GT accepted.

## Discard policy -- USER-REVISED 2026-09-27 (adopted)

Lands to 3 total; Mimic below Dire Fleet Captain except turn 1; a 3-turn CURVE-OUT plan replaces the threat
floor (only unplayable cards go first); the Vial is a turn-1 keep unless mana-starved. Full text:
`docs/design/pirates-discard-policy-proposal.md` ("USER revision"). Unit cases = the user's own examples.

## Generation sizing (2026-09-27)

* **Value leaf.** The matrix = 13 unbounded cells (H1-5, V1-8) x 4 seeds x 400 games. H-arm probe (40
  games/cell, seed 8008, lazy leaf armed as phase C does, box shared with a sibling container so wall is
  inflated): core-s/game H1 0.15, H2 1.3, H3 8.3, H4 17.9, H5 20.5 (after the lazy fix below) -> ~48
  core-s per game-row -> ~21 core-h for the H arm. V cells cannot be timed before a model exists.
* **Found on the way: the lazy-leaf probe was unsound** (committed a later in-window win): H5 read 4.775
  vs the true 4.425. Fixed (`MTG_LAZY_LEAF_LADDER`, `50cb00c1`); fleet follow-ups in
  `docs/design/cold-fslinewin-horizon-exit-audit.md`.
* **Mulligan.** Discovery merged only Courtyard+Territory and Islet+Canal: K = 17 -> 172,666 distinct
  7-card hands (252,769 incl. mulligan sizes). Its wall-clock needs the value leaf first (the generator
  reads its depth/budget from `value_play`; a projection without it is invalid by repo rule).

## Open questions for the user

(collected here; never blocking — defaults taken are stated)

1. Sign off the PROVISIONAL deferrals above.
2. **Cast order (USER review — NOTHING ADOPTED).** `PiratesProvider` does NOT override
   `CastOrderRank`; the deck runs the ROOT default (`GenericProvider::CastOrderRank`: creatures 10,
   other non-creatures 20, mana rocks 5, accelerant tiers 15/16/18) and the search decides every
   ordering the canonical line leaves open. Proposals, each to be measured behind a default-OFF
   `MTG_PIRATES_*` lever only after sign-off:
   * **2a — Metallic Mimic before Malcolm (and before every other Pirate).** Mimic's +1/+1 counter
     is permanent and accrues to every Pirate cast after it; Malcolm's Clue fires on the SECOND spell
     of the turn regardless of which spells, so Malcolm need only not be the turn's first-and-only
     spell. Proposed: Mimic rank 8 (< creatures 10) whenever another Pirate is cast the same turn;
     Malcolm stays at 10. No counter-case found: Malcolm's haste attack happens in combat after both
     have resolved, and cast first he would himself receive Mimic's counter only if Mimic preceded him.
   * **2b — Daring Buccaneer FIRST among Pirate casts.** Its cost is `{R}` only while another Pirate
     CARD is in hand to reveal; casting the other Pirates first can strand it at `{2}{R}`. The
     enumerator already prices this per subset (chunk 2's order-aware surcharge), so the rank only
     matters for the canonical line. Proposed: Buccaneer rank 9 when a reveal is available.
   * **2c — Forerunner of the Coalition FIRST among the non-Buccaneer Pirates.** Its drain is
     "whenever ANOTHER Pirate you control enters", so every Pirate cast before it drains nothing.
     Tension with 2a (Mimic first grows the Forerunner too) and 2b. Proposed total: Buccaneer 7 (cheap,
     reveal-dependent) → Mimic 8 → Forerunner 9 → other creatures 10. If the mana only allows two,
     the search decides which.
   * **2d — Vial-put pick.** Which creature a Vial at N counters puts in is today the root Vial policy
     plus the search. Proposal: prefer Forerunner / a lord at N=3 over Kitesail Larcenist, and Mimic
     at N=2 over Crewmate/Malcolm/Dire Fleet Captain (same reasoning as 2a/2c). This is a *put-order*
     heuristic, not a charge policy — the charge policy (`WantVialCharge`) stays root.
   Default taken: root order, pending review.
3. OK to classify the new params in `audit_viewer_decisions.py` INERT_PARAMS (script self-guard)? **APPROVED 2026-09-27** (USER: "3 is approved") -- all 14 rows un-tagged PROVISIONAL.
4. Corsair's same-turn Treasure: lift the fresh-hold for non-trick Treasures (A/B-measured)? Default: measure.

## Value leaf — GENERATED AND ADOPTED (2026-09-28)

**Authorisation and one caveat, stated first.** USER 2026-09-28 (after cancelling the Snow value leaf and
closing WhiteKnights' round P): *"If you finish all of that before morning you can feel free to generate
artifacts for Pirates, since those are missing."* That supersedes the 2026-09-27 sequencing note below
("no earlier than Monday, alone on the box") — but the same note recorded a PREREQUISITE the user set:
*"the deck should have some references before the value-leaf anyway"*, and `references/Pirates/` is
**still empty**. I proceeded on the newer instruction and say so here: the leaf is fitted to the engine's
own play (K=3 searched labels at shipped play), not to any hand-played line, and the analyze-deck 6a
reference bench has not been run for this deck. If the user's references later show a systematic
misplay, the leaf is regenerated with `valueleaf.sh run` (incremental; the rows are the cost).

**Run:** `bash scripts/valueleaf.sh run decks/Pirates` at 10:12 UTC on `5bb340e1` (the hand-entry
adoption), alone on the box. Phase A 3.5 min (10 jobs, **11,141 rows**, held-out RMSE **0.4008**); phase C
**49 min** (52 cells × 400 games, 32/32 workers, 14 degenerate games abandoned at the unit ceiling and
backfilled, 0 games over 30 s); C.5 CLEAN; D; phase E 17 min (48 jobs). Total A→E **71 min** — the
2026-09-27 sizing (~2–4 h) was pessimistic by 2–3x.

**Depth matrix** (unbounded, 4 seeds × ~394 paired games, loss-penalised avg win turn / ms per game):

| rung | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 |
|---|---|---|---|---|---|---|---|---|
| H (heuristic) | 4.4590 / 219 | 4.4445 / 2,186 | **4.4439** / 11,513 | 4.4439 / 25,949 | 4.4439 / 7,800 | | | |
| V (value leaf) | 4.6772 / 4 | 4.5612 / 48 | 4.4723 / 295 | 4.4445 / 1,059 | 4.4445 / 1,562 | **4.4439** / 1,863 | 4.4439 / 2,178 | 4.4439 / 2,116 |

`h_conv` = 4.4439 at **H3** (the search saturates early — H4/H5 buy nothing); the leaf matches it within
tol 0.002 from **V4** and exactly from V6. Crossover (per committed depth c → take the heuristic at
h ≥): 1→1, 2→1, 3→1, 4→3, 5→3, 6→6, 7→6, 8→6. Trust candidate d4.

**Phase E, the adoption gate (8 held-out seeds × 1,000 games, the live arm = NO sidecar, this deck's
first model):**

| arm | avg | delta | paired t | better/worse/tied seeds | core-s | vs base |
|---|---|---|---|---|---|---|
| live (no sidecar) | 4.46675 | — | — | — | 12,344 | — |
| **staged** | **4.46125** | **−0.00550** | **−4.92** | **8/0/0** | 5,332 | **0.43x** |

Trust acceptance (ON vs OFF at d4, 8 seeds × 1,000): **+0.00000** (one-sided 95% upper +0.00062 ≤ the
halved tol 0.0010), 7/8 seeds byte-identical, **0.82x** core-s → **ACCEPTED, `value_trust_depth = 4`**.
Target-depth sweep (d4/d5/d6/dflt, 4 seeds × 500): every arm the same average on every seed (the
committed play is identical; only cost moves, d6 cheapest) → no play-policy override written.

**Verdict: a CLEAN WIN on both axes** — better on 8/8 seeds and 0.43x the per-game cost, plus a trust
depth that skips escalation at 0.82x for zero quality — so it is adopted without a stage-and-ask, per the
standing rule that no-drawback wins are pre-approved. Installed by copying
`logs/eval/Pirates.value.STAGED.json` → `decks/Pirates/Pirates.value.json` (the WhiteKnights precedent,
`721fe1b6`), then `valueleaf.sh run` again for **phase F** (the mulligan contract: `mull_gen_depth` /
`mull_gen_budget_ms` by measurement, and the discovered K recorded as `expected_buckets` — a record of
discovered K, listed for the user's sign-off as it was on WhiteKnights). Then the standing gate: smoke +
regression + overnight (trust depth moved), accept, commit.

This is also the measurement the 2026-09-27 note asked for on the **Pirates d5 tail** (the overnight's
~6 s/game, 100+ games over 30 s, "compares the unmodelled path to modelled decks"): the overnight tier
under the adopted leaf re-measures those rows directly.

## RESUME STATE (2026-09-27, third compaction) — SUPERSEDED for the value leaf by the section above

* Worktree `/tmp/pirates-wt`, branch `pirates-analysis`; pushed through `6c34573b` (CI green incl.
  Windows). Main tree holds another session's WIP -- never touch it.
* DONE + pushed: deferral review (D1/D2/D4/D5 approved; D3, D6 fixed + MTG_BP_RECORD_VIAL), lazy-leaf
  fix, cast order + MTG_PAYABLE_ORDER ADOPTED, discard USER-revised, GT (smoke+regression) accepted.
* DONE: every suite deck is now in ALL THREE tiers with GT (USER: "add to the regression test in all
  modes or none"). One pooled overnight run added Pirates (+pirates2hg), Fungus, Giants and baselined
  Snow's rows: 49 new keys, makespan 31m under another container's load, 24/24 workers busy. Core-s:
  pirates 26,251 (d5 5.97 s/game), pirates2hg 5,305, snow 8,191 (d5 11.04), giants 3,106, fungus
  1,290. Enforced by `verify_deck.py` `regression_tiers` (partial = never sign-off-able); the fleet
  passes it.
* PIRATES d5 TAIL: in the pooled overnight run, Pirates d5 b40 cost ~6 s/game (under ~2x load from
  another container) with 100+ games over 30 s (worst ~82 s, ~1.9M units) -- vs the 2026-09-11 fleet
  overnight, where only fivecolour (57) and melira (9) had slow games. BUT Pirates and Snow are the
  only suite decks with NO value leaf, so this compares the unmodelled path to modelled decks. USER:
  "there is the question of how much they just need a value-leaf or mulligan profile." PLAN: value
  leaf first, re-measure the Pirates overnight rows, optimise only if still unusual vs modelled decks
  (slow-game repros are in that run's batch.err). Overnight rows stay as added regardless.
* NEXT -- USER 2026-09-27: "we'll wait until I free up on the box on Monday. This isn't that urgent."
  So the value leaf starts no earlier than Monday 2026-09-28, alone on the box. PREREQUISITE (USER
  2026-09-27: "the deck should have some references before the value-leaf anyway"): references/Pirates/
  is EMPTY -- the user hand-plays reference games in the viewer first (user-owned; commit-only). Then: (`bash scripts/valueleaf.sh run decks/Pirates`, ~2-4 h,
  nothing else on the box), then mulligan (K=17, 172,666 hands; size only after the value leaf).
* MONDAY QUEUE (USER 2026-09-27: not before Monday; box is shared until then), one batch at a time:
  (1) Giants value-leaf follow-up. The AUDIT is DONE (2026-09-27, docs/design/cold-fslinewin-horizon-
  exit-audit.md): the bug is in its matrix (15/400 H5 games T4->T5), the adoption still stands. Monday:
  re-run the H arm at freeze ac4b8965 with the lazy leaf OFF, recompute phase D, A/B the revised sidecar
  vs shipped; also check which matrix produced Fungus's crossover.
  (2) Pirates value leaf, once the user's Pirates references exist.
  (3) d0 scorer lord/drain credit -- QUEUED, not top priority (USER 2026-09-27);
  docs/design/d0-greedy-scorer-lord-and-drain-credit.md.
* card_costs audit: FIXED by another agent (5cd3e0c1 + ff561bff), from the evidence in
  docs/design/card-costs-audit-adventure-and-false-green.md.
* Open: Giants value-leaf re-audit (docs/design/cold-fslinewin-horizon-exit-audit.md), d0 scorer gaps
  (docs/design/d0-greedy-scorer-lord-and-drain-credit.md), viewer INERT_PARAMS question, Fungus
  card_costs sign-off.

## Chunk 3 landed (2026-09-26) — provider, routing, certificate, tutor width, discard policy

Integrated serially in `/tmp/pirates-wt` (branch `pirates-analysis`, local, NOT pushed).

| commit | what |
|---|---|
| `097e913f` | `PiratesProvider : DeckProvider` — routing above `anti`, `Certificate()` NotAssessed, `TutorSearchWidth` 9; routing/width unit tests |
| `d5d6220e` | authored bucketed cleanup-discard policy behind `MTG_PIRATES_BUCKET_DISCARD` (default ON) + `docs/design/pirates-discard-policy-proposal.md` + 7 discard unit tests |
| (this commit) | ledger: `## Discard policy`, cast-order proposals 2a–2d, this section |

**Routing.** Before: Pirates → **AntiLifegain** (Forerunner's `tutor_to_top`; also Mill and Unpredictable
Cyclone → AntiLifegain). Signature = `reveal_or_pay_subtype` | `etb_treasurify_each_player` |
`attack_pump_tough_per_other_matching` | `nth_spell_investigate` | `own_creature_enters_opp_life_loss`
(five cards; each param carried by no other card in `cards.json`). Deliberately excluded: `tutor_to_top`,
the colourless Mimic/Automaton chosen-type params, `etb_creates_treasures`, `static_artifact_*`.
Verification:
* `scripts/provider_audit.py --check` over all 25 profiled decks: **identical** to the pre-change run
  (only the "not assessed" certificate count moves 22 → 23 for the new class). Saved:
  `logs/pirates_chunk3/provider_audit_{before,after}.txt`.
* `--batch` probe of the three unprofiled lists: Pirates → **Pirates**; Mill, Unpredictable Cyclone →
  AntiLifegain (unchanged).
* Unit test pins the provider of EVERY folder in `decks/` (and fails if a new folder appears without an
  entry), plus the signature's survival of any single-card cut.
* Byte-identical play for every other deck holds **by construction** (the signature params exist only
  on Pirates cards; nothing else changed for another deck). Smoke NOT run (box hold) — see PROVISIONAL.

**Why DeckProvider and not the AntiLifegain it rode.** The skill's "derive from what the deck actually
routes to today" protects play-neutrality for an already-measured deck; Pirates was never measured on
AntiLifegain, which was a misroute, and deriving from it would import `TutorSearchWidth` 2 (hiding 7 of
9 Forerunner targets) and another deck's buckets. PROVISIONAL-by-default only in that the user has not
seen it; the RESUME STATE already prescribed DeckProvider.

**Tutor width.** 9 distinct Pirate names in the library (Mimic/Automaton are Pirates only on the
battlefield); generic candidates are in shuffle order, base width 6 → 3 names unreachable per game. Width
only feeds the TurnSolver tutor axis (`TutorAxisWidth`, additive, one rollout per extra target) and
GoblinsProvider internals; `MTG_TUTOR_WIDTH` still overrides.

**Results.**
* `./build.sh` clean; `build/Release/mtg-test` **207/207 pass** (10 new Pirates-provider cases).
* `python3 scripts/audit_viewer_decisions.py decks/Pirates/Pirates.cod --no-sweep`: exit 0; expected
  decisions `dig, target, treasurify, vial_charge`; oracle cross-check clean.
* `analyze_deck.py --coverage-only`: **19/19 full**.
* Discard gate (main tree's `discard_policy_audit.py` classifier run against this tree): **Pirates → OK**.
* Sanity: `build/Release/mtg decks/Pirates/Pirates.cod --games 10 --threads 2 --seed 4242` → avg 5.10,
  per-game win turns `-,4,4,5,5,4,5,5,5,5` — **identical** to the pre-chunk-3 run of the same seed
  (`logs/pirates_sanity`). Game 0 is unwon in both: two Mountains, no blue/black, and the Aether Vial
  **never charges** — because there is no `.profile.json` yet, so `vial_target_mv` = 0 (the known
  profile-less behaviour, `src/analyzer/main.cpp` note). NOT a play bug; it disappears with the Stage 4
  profile. The three cleanup sheds in that game match the policy (FAR Forerunner/Larcenist shed first).

**PROVISIONAL (awaiting the user):**
* The discard policy itself (default ON, user review pending) and its seven doubts.
* Cast-order proposals 2a–2d (nothing adopted; root order in force).
* Smoke / regression byte-identity NOT run — box held by a sibling container (one-batch rule). Owed
  before push, together with the Melira Pod / FiveColour verdicts from chunks 1–2.

**Found in another deck (not fixed — out of scope):** `GiantsProvider` does not override
`TutorSearchWidth`, and Giants holds **7** distinct Giant names for Giant Harbinger (Inferno Titan,
Sunrise Sovereign, Giant Harbinger, Borderland Behemoth, Hamletback Goliath, Surtland Flinger, Tectonic
Giant) against the base width 6 — the same shuffle-order coverage hole fixed here for Pirates, one name
unreachable per game. A width-7 override would widen (not narrow); it changes Giants' play, so it needs
its own measurement and GT verdict.

**Next:** unchanged from RESUME STATE after chunk 3 — smoke (when the box is quiet), Stage 4 profile
(`analyze_deck.py --no-rebuild`), then `--discard-analysis`, Stage 5 (`verify_deck.py` incl. the
`discard_policy` gate once the main tree's gate lands), claude-play sweep, fresh-hold A/B, suite entry,
rebase + push + CI watch.

## Sweep fixes (2026-09-26) — Stage 5d claude-play findings A–G

Integrated in `/tmp/pirates-wt` (branch `pirates-analysis`, local, NOT pushed). Sweep: 20 Opus games,
base 777000, findings in `logs/pirates_sweep/results.md`. `build/Release/mtg-test` **216/216 pass**
(9 new cases across `test_pirates.cpp` / `test_pirates_provider.cpp`). No smoke/regression run by the
integrator (the orchestrator runs the suite).

| finding | verdict | commit |
|---|---|---|
| **G** SLOW-GAME repro printed `--game-index 0` under `--game-index N` | real (output only). `GoldFishRunner`'s SLOW-GAME line and main.cpp's "Unwon games" list printed the LOCAL index; the replay needs the GLOBAL one (spawn pattern `PATTERNS[gi % 10]`). Now `base_game_index + gi`, matching the batch twin. Log FILE names keep the local index on purpose (tools key them `<base_seed>_game_<local gi>`). | `c241c49c` |
| **C** Fiery Islet plans 6x in the human menu | real, human-menu only. The 5 twins are `AppendBreakpointVariants`' bp_choice variants (`MTG_BP_DIG_SELF_SOURCE`: a plan whose own land drop is a sac-draw land is fanned out, W=2 × 2 indices + the empty arm). Under human play NO breakpoint continuation is ever re-solved (`!s_human_play` everywhere), so every bp variant realises its base plan — for every deck and site. Fix: hidden from the capped menu via the existing `hide_bundle` (indices stay real; uncapped protocol checker and chosen-extra path unchanged). **Search side: by design, not changed** — the self-source fan-out is deliberately state-independent (its note accepts duplicate variants when the dig is unaffordable); decks with Fiery Islet: Pirates, treasure_hunt. Measured Pirates gi11 d5/b200 with `MTG_BP_DIG_SELF_SOURCE=0` vs `1`: identical game, no measurable wall difference. | `ba4da21f` |
| **D** Vial-put Crewmate digs before Mimic's as-enters counter | real (rules order). Both Vial twins (`AIEngine` deploy_via_vial / `TurnSolver` apply_vial) ran `PerformEtbDig` before `FireEtbWatchers`/`FireOwnEtbTriggers`; every cast path runs the cascade first (CR 614 replacement precedes the CR 603.6a trigger). Both now dig after the cascade, addressing the entrant by slot (the cascade can push tokens). | `dc33e254` |
| **A** Forerunner "you may search" had no decline plan in human play | real. The search's decline is the index axis (`kTutorDeclineChoice`, autonomous-only); a PERMANENT's ETB tutor has no resolution frame, so its named menu variants were the human's only say. Human play now appends a `(decline search)` variant (`kTutorDeclineTarget`), resolved as a decline by the one shared `PerformTutor` (executor and rollout lockstep for free). Tutor spells already re-ask with -1; the Vial/put route already asks `tutor_etb` with -1 = decline (checked, no change). Also applies to Giant Harbinger (Giants human menu gains one variant). DECISIONS.md updated. | `b54557b6` |
| **E** main-1 drain to 0, game continued into combat (0 → -17) | **harmless, documented** — not changed. `GameEngine` checks the win after combat and at end of turn, so the win turn is the same turn; every search site tests `OpponentHasLost` right after `ApplyPlanDirect`, so a main-phase drain kill scores as a this-turn win (main 2 likewise; the engine never Vials at instant speed on the opponent's turn). An SBA check after main 1 would only truncate post-lethal combat in the log and move the play digest of every deck with a main-phase kill for no win-turn change. Unit test pins the search half. | `d6cfbb2a` |
| **B** Vial puts always resolve before casts | real expressiveness gap. New `Plan::vial_after_casts`: `AppendVialOrderVariants` adds one casts-first clone per base plan where `TurnSolver::VialOrderMatters` (a cast carrying `other_chosen_subtype_enters_counters` / `own_creature_enters_opp_life_loss` / `reveal_or_pay_cost` alongside a Vial put), on all three enumeration exits. Both orders are scored; the SEARCH picks (no order dominates: Vial-put Forerunner + cast Mimic trades a drain for a counter). Lockstep: rollout defers the top-level `apply_vial` loop to right after its graveyard casts (arm-once, continuations unaffected); executor defers `deploy_via_vial` to right after its graveyard-cast loop. Menu summaries now list entries in RESOLUTION order where the predicate holds; plan JSON `vial_after_casts`; CheckLine `Aether Vial timing` sub (viewer regex updated). `MTG_VIAL_ORDER_AXIS=0` disables. Verified: gi17 variant → Siren Stormtamer enters with Mimic's +1/+1 (executor path); gi5/7/13/17 d5/b200 benches still T4 with `MTG_FD_ORACLE=1` and no `[fd-diverge]`. | `9ede989b` |
| **F / gi14** Crewmate dig took Aether Vial over a 2nd Goblin Tomb Raider | real expressiveness gap (budget-invariant T5 at b200/b2000/b20000). The dig axis scores the first 3 of the provider ranking; the base ranking is every legal match in look order, so two Aether Vial COPIES filled two slots and the Raider (4th) was unreachable. `PiratesProvider::EtbDigCandidates` folds same-name copies (exact: the rest go to the bottom in random order) and new provider-owned `EtbDigSearchWidth` = 4 (the look count). gi14 hand-off now wins **T4** at b200. | `a68a97ac` |
| **F / gi11** Crewmate dig → same-turn Daring Buccaneer | real expressiveness gap, **DEFERRED** (`docs/design/etb-dig-same-turn-cast.md`). An ETB dig never opens a breakpoint at searched depths: the rollout arming is a recorded rejection (`MTG_ACQ_DIG`, 2026-08-19), and the general put-in-hand rule stands down because `ParamKeyedDrawClass` claims `etb_dig_count`. Hand-off T5 at b200 and b20000 (Claude T4). Fix is engine-wide, moves Knights, needs its own A/B. | (doc, this commit) |

**Which other decks' play can move (for the suite check):**
* **None by construction** for G, C, A (human-play / output only), D (only Vial-put `etb_dig` creatures are Staunch Crewmate and Acclaimed Contender; no Knights card carries an enter watcher or own-ETB trigger, so the reordered cascade is a no-op there), B (gate params exist only on Pirates cards; `VialOrderMatters` short-circuits on "no ActivateVial"), F/gi14 (base ranking and width untouched). E is a test only.
* **Pirates** play changes (B, D, F/gi14) — Pirates GT is being created now, so there is no baseline to move.
* Human-play menus change for Pirates (A, B, C), Giants (A: Giant Harbinger decline), and every deck showing bp_choice twins (C: fewer menu entries, indices unchanged).

**B follow-up (confirmation sweep gi7, same day):**
* *searched_order plans had no puts-last twin.* With >= 2 reorderable casts the cast-ordering expansion (human play / `MTG_SEARCH_ORDER` / Dragonstorm) replaces the base plan with `searched_order` variants, which `AppendVialOrderVariants` skips — so "Metallic Mimic, Siren Stormtamer, then Staunch Crewmate (vial)" was not in the viewer menu. The expansion now builds a puts-last twin for EVERY ordering (before its end-state dedup, since orderings that collide puts-first can differ puts-last), gated on `VialOrderMatters`, applied and deduped like any ordering. gi7 T3 (prefix `1,1,0,0,1,55,0,0,1`, full menu): the twin is offered and the Crewmate enters with **2** counters.
* *residual closed: a Buccaneer subset affordable ONLY puts-last.* `SameSubsetRevealSurcharge` gained a puts-last pricing mode (thread-local, RAII `RevealVialsLastScope`: Vial-put cards stay in hand for the reveal walk). `EnumeratePlans`' `eval_and_push` now retries a subset the body rejected, under puts-last pricing, when `RevealCheaperVialsLast` (a Vial put AND a reveal-cost cast selected, and puts-last strictly cheaper), and flags the plan it emits `vial_after_casts` — so pricing always matches the order both apply worlds realise (every inner payability walk reads the same mode). gi7 T4 (prefix `...,4,2,0,0,0`): "Daring Buccaneer, Metallic Mimic, Dire Fleet Captain (vial)" offered and resolves with all three on board. The whole-turn prepay needs no change (it walks the live hand, which still holds the deferred put cards). Solve (d0 greedy) stays puts-first.
* Other decks: the retry costs nothing unless `any_reveal_cost` (Daring Buccaneer is the only `reveal_or_pay` card) and the ordering twin is gated on `VialOrderMatters` → byte-identical by construction.
* Tests: the old "Vial-putting the only other Pirate leaves the Buccaneer at {2}{R}" case now expects the combined plan, flagged puts-last; new cases for the puts-last-only Buccaneer plan (enumerated + applied) and a searched_order + deferred-Vial apply (Crewmate 2 counters). `mtg-test` 218/218. gi5/7/13/17 d5/b200 still T4, no `[fd-diverge]`.

**PROVISIONAL / residual risks:**
* ~~B's pricing gap~~ closed by the follow-up above.
* B's lockstep scope excludes plans that open a breakpoint (the deferred puts are not threaded through continuations); such a plan keeps puts-first only.
* gi13's autonomous bench still ends with a counterless Buccaneer: the casts-first variant IS enumerated, but both orders win T4 and the win-turn tie keeps the base (puts-first) plan. The search's metric does not price the +1/+1; not a defect.
* Found, NOT fixed (pre-existing, other decks): the ROLLOUT's whole-turn batch prepay (`BatchPrepayMainCasts`) runs BEFORE its Vial loop (inside `apply_plan_actions`) while the EXECUTOR deploys Vial puts first and prepays after. Payment-quality only (casts reprice live), but it means the two worlds prepay from different boards on Vial turns (a Vial-put cost reducer such as Goblin Warchief, or a Buccaneer reveal). Worth an fd-diverge look on Goblins/Knights.
* F/gi14's name-dedup is Pirates-only; Acclaimed Contender (Knights) keeps copy-occupied slots under the base ranking — the same reach hole, left for a Knights-measured change.

## Claude-play sweep
- commit: `62f8f5ac` (confirmation pass at `6576822c`; the B follow-up only adds plan variants, re-verified by the gi7 repro)
- seeds: 777000 games: 24 (gi 0-23; 20 first pass + 12 confirmation replays incl. 4 fresh)
- flags: 0 unresolved
- gi11 (Claude T4 vs search T5): FIXED after user review (D6) -- the put-in-hand breakpoint never armed after an ETB dig (`ParamKeyedDrawClass` claimed `etb_dig_count`); `MTG_BP_ETB_DIG` (`d4ef1f81`) makes the search play Claude's line and win T4.
- gi20 (search unwon vs Claude T5): baseline static keep kept an uncastable hand — mulligan-stage evidence, not a play defect.

### Sweep detail
#### First pass
- commit: `94b4582e` + chunk 3 (`4016c6c4` tip), profile from Stage 4
- seeds: base 777000, games 0-19 (20), Opus player agents, d5/b200 benchmark
- outcomes: 18 ties; Claude faster in 2 (gi11 T4 vs T5, gi14 T4 vs T5); Claude slower in 0
- flags raised (all verified against code/cards.json; fixes in "Sweep fixes"):
  A. human play: Forerunner optional tutor has no decline plan (gi4, gi10, gi18)
  B. one plan always resolves Vial puts before casts -> Mimic-then-Vial inexpressible (gi5, gi7, gi13, gi17); gi13 bench also counterless
  C. Fiery Islet plans duplicated 6x in the human menu (gi4, 5, 7, 9, 11, 13, 16)
  D. Vial-put Crewmate: ETB dig before Mimic's as-enters counters (gi7)
  E. main-phase drain to 0 does not end the game before combat (gi10)
  F. misplay candidates gi11 (dig -> same-turn Buccaneer), gi14 (dig took Vial over hasty Raider)
  G. SLOW-GAME repro label prints --game-index 0 under --game-index N
- dismissed (cosmetic / documented): Courtyard shows taps "C"; global mdfc_backs list; Courtyard-paid Bolt offered then dropped (documented MTG_CCO_NONCREATURE_POOL narrowing); own Treasure offered as a no-op treasurify pick in human play

### Confirmation pass (post-fix, HEAD `6576822c`)
- games: 12 (gi 4,5,7,10,11,13,14,17 re-played + fresh gi 20-23), Opus players
- outcomes: 10 ties; Claude faster in 2 — gi11 (deferred: ETB-dig card cast same turn, docs/design/etb-dig-same-turn-cast.md), gi20 (baseline static keep kept an uncastable 2-Mountain 7 -> mulligan-stage evidence, not play)
- fixes confirmed live: A (Forerunner decline, library unshuffled), B (casts-first copy, 2-entry plans), C (Islet twins hidden), D (Vial-put Crewmate dig sees Mimic counters), F/gi14 (search now digs Raider, wins T4 vs T5 before)
- new: B residual — casts-first copy missing for plans with >=2 reorderable hand casts (gi7) -> fixed in follow-up
- cosmetic, not fixed: summary order for Vial plans outside VialOrderMatters (Vial put listed last though it resolves first; fleet-wide, pre-existing, touching it would perturb other decks' reference replays); human dig heuristic_default ignores the searched pick; payer taps Fiery Islet (1 life) when a painless source is free (life inert in goldfish)

