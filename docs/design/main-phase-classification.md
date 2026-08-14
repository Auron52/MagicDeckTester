# Main-phase classification (MAIN1 / MAIN2 / BOTH)

**Status: USER-designed architecture (2026-08-14); ENGINE MACHINERY BUILT (same day, see
"Implementation (as built)" below), measurement in flight, NOT yet adopted — every gate
default-off, suite byte-identical (smoke 36/36 post-build).** Origin: the
KittyEquipment branching work (`analysis-KittyEquipment.md`) — the residual search cost is
tree volume, and the USER rejected lossy truncation (see `heuristic-optimization.md` Rule 0b);
this is the heuristic answer for the two-main share of that volume. The USER named the greedy
second main's existence as one of the REASONS for this design — killing it is a design goal
(see the directive below), not an afterthought.

## The rule (USER, verbatim intent)

Classify every spell/ability a deck can cast in a turn as **MAIN1**, **MAIN2**, or **BOTH**,
by one question:

> Does this spell help me, or potentially help me, deal damage in the attack phase?
> If it does, you cast it first main. If not, you cast it in the second.

- **MAIN1:** lords, haste creatures, things that grant haste, situations where a creature
  *can* gain haste, things that pump creatures (including equipment) — anything that adds to
  or unlocks this turn's attack.
- **MAIN2 (the default for everything else):** "as a Magic player I play everything that
  doesn't help [the attack] in the second main — primarily to make the opponent respond to my
  attack before I do anything else." Worked examples: vigilance-attacking Faeburrow Elder
  into Unite the Coalition (FiveColour); attack with Hinata, then Crackle targeting her;
  Light Up the Stage after damage (Burn).
- **BOTH (narrow exception classes):**
  * **Draw spells** — can serve either phase; only worth double-listing when the deck has
    effects that take advantage of main 1 (otherwise MAIN2).
  * **Rituals** — may need to power a spell in either phase; again only if main-1 effects
    exist in the deck.
  * **Floating mana** — you can't fully separate the phases when mana would be lost across
    them; rare in practice.
- A deck may still be listed as using only one phase (the existing `DeckUsesSecondMain`
  gate is unchanged for single-main decks).
- **A deck with NO main-1 effects classifies EVERYTHING Main2 — draws included (USER
  clarification 2026-08-14, verbatim):** "everything in Hinata is second main. The reason is
  because there is literally no attacking creature beyond Hinata herself and she never gets
  haste or is pumped. So, by my rule everything should be cast second main including all
  draw." The BOTH classes exist only to feed a deck's main-1 effects; with none, they
  collapse to MAIN2 and main 1 is just the land drop + attack. Corollary (USER): "if we are
  considering main 1 for Hinata, there is a bug" — under a total doctrine, a game that
  measures worse is an ENGINE defect in the post-combat path, never a reason to soften the
  classification.

## Why this is the right shape

- It **partitions** the candidate space per phase instead of enumerating most cards in both
  mains: main-1's odometer runs over MAIN1 ∪ BOTH, main-2's over MAIN2 ∪ (BOTH still in
  hand) ∪ combat-acquired resources ∪ repositioning (e.g. the Greaves→Kemba park). The
  joint space shrinks multiplicatively without removing any *line* — a non-combat cast
  moves phases, it doesn't disappear.
- **Main-2 weak dominance (USER correction 2026-08-14 — NOT mere equivalence):** for a
  spell that does not feed this turn's attack, casting in main 2 weakly DOMINATES casting
  in main 1 — never worse, sometimes strictly better — because combat can only ADD options
  between the mains, never remove them: damage already dealt (spectacle costs — Light Up
  the Stage), attackers surviving untapped (vigilant Faeburrow Elder attacks AND still taps
  for Unite the Coalition; tapping it for mana in main 1 would cost the attack), targets in
  their post-attack state (Crackle wants Hinata to have attacked before it consumes her).
  The dependence is BIDIRECTIONAL — the cast can affect combat AND combat affects the cast
  (cost / resources / targets) — and an earlier "outcome-identical" framing that considered
  only the first direction was wrong. The only places the inequality can flip — a main-1
  cast indirectly feeding the attack — are exactly the BOTH classes (draws, rituals,
  floating mana): the exception list IS the boundary of the dominance argument.
- **Measurement implication:** MAIN2 moves should measure neutral-or-BETTER per game; any
  per-game regression is a MISCLASSIFICATION signal (a hidden attack-feeding role), not an
  acceptable trade.
- It also aligns with correct play for the eventual real-opponent phase (force a response
  to the attack before committing more).

## Implementation sketch (sites, not yet built)

- **Where:** `CollectActions(state, is_pre_combat)` already carries the phase; filter
  candidates by classification there. Executor and rollout share it → lockstep by
  construction. Enforced as a provider-owned policy with an open switch (`MTG_UNPRUNE`
  gate / human play opens both phases fully — never narrow the viewer).
- **Classifier:** param-driven base (haste / grants-haste / pump / equipment / lord
  statics / spectacle / combat-triggered payoffs...), plus the conditional class: a fresh
  creature is MAIN1 only when haste access exists this turn (granter on board or castable —
  the "cast Greaves + creature, equip, attack NOW" lines must stay reachable); otherwise
  creatures default MAIN2. **When in doubt, classify MAIN1** — that reproduces current
  behavior and is never wrong, only wider; MAIN2-only is the assertive claim.
- **BOTH handling:** the card appears in both phases' candidate sets; no bookkeeping needed
  for exclusivity (main-2's Solve sees the post-main-1 state — a card cast in main 1 is no
  longer in hand).
- **Mana coupling:** the search already arbitrates reservation structurally — a main-1 plan
  that strands the main-2 payoff rolls out worse.
- **KILL THE GREEDY SECOND MAIN (USER directive 2026-08-14 — the point of this design, not a
  side effect):** "The greedy enumeration is something I want to kill with my new second-main
  design... I really don't like that we have greedy logic in the search window. I want it all
  removed." The searched second main (`MTG_SEARCH_SECOND_MAIN`) measured worse at full width
  only because each phase searched the whole hand twice (budget dilution,
  `second-main-greedy.md`); classification makes each phase a PARTITION, which is what makes
  the searched second main affordable. Once classification lands and measures, the greedy
  second-main path inside the search (`SolveSecondMainInSearch`'s greedy branch and the
  in-rollout greedy `Solve(state, false)` sites) is to be REMOVED — searched becomes the only
  path, completing the standing USER BAR "no greedy steps except attack decisions and mana
  allocation" (2026-08-09). The principle, verbatim: "search should be truly search at every
  level. Greedy is simply too unreliable to be part of it."
- **Per-deck payoff:** biggest for decks whose second main carries real decisions over many
  castables (FiveColour, Hinata, Burn staging, Goblins post-Lackey). KittyEquipment gains
  modestly (USER assessment: its second main is only the Kemba park + casting cards drawn
  off a Puresteel/Skyhunter attack) — most of its hand is equipment/pumps = MAIN1 anyway.

## Implementation (as built, 2026-08-14)

- **Filter site:** ONE pass at the tail of `TurnSolver::CollectActions` (after the def-resolution
  loop), pre-combat only: Main2-classified `CastFromHand` actions are erased (`remove_if`, order
  of survivors preserved for the odometer). The post-combat pass is NEVER filtered, so a Main2
  class moves a line, it cannot delete one. Both enumerators (Solve + EnumeratePlans), the
  full-search second main (`FSLineTail`'s `EnumeratePlans(state, false)`), and the executor all
  share this funnel — lockstep by construction. Generalises the Goblins
  `DeferSacOutletPreCombat` precedent from sac outlets to the whole cast enumeration.
- **Gates:** provider opt-in `DecisionProvider::ClassifiesMainPhases()` (default false =
  byte-identical everywhere; ONLY valid on a `DeckUsesSecondMain` deck — a single-main deck
  would LOSE the cast, not defer it) or the `MTG_PHASE_CLASSIFY=1` A/B lever (same second-main
  caveat — never set it on a suite-wide run, single-main decks would be corrupted).
  `MTG_NO_PHASE_CLASSIFY=1` kills; `MTG_UNPRUNE=mainphase` (UnprunedGate::MainPhase) and human
  play keep the full pre-combat set (the viewer is never narrowed).
- **Base template rules** (engine-side, `ClassifyMainPhase` in TurnSolver.cpp) — Main2 is
  asserted only where the engine can SEE the cast cannot feed the attack:
  * `spectacle_cost` present → Main2 (Light Up the Stage — the USER's named example).
  * `DirectDamage` template with no pump rider → Main2 (and Hinata's Crackle/Soulfire are
    actively anti-Main1: they can destroy their own discount targets).
  * `DrawSpell`/`DrawX`/`DrawUntilNonland` → Both (can dig into a Main1 card — the boundary
    class; kept in both phases).
  * `VanillaCreature`/`ManaDork` (statics-free by definition) → Main2 iff summoning-sick with
    NO route to haste (own keyword, on-board haste lord via `HasHasteFromLords`, on-board
    `equip_grants_haste` equipment, or a `grants_haste`/`equip_grants_haste`/`grants_temp_haste`
    card in hand — affordability-blind on purpose: over-detecting only keeps a cast pre-combat)
    AND no board-scaling attacker (`domain_self_pump` — a new-colour permanent pumps a live
    Faeburrow Elder; `domain_mana` — mana coupling; `power_equals_creature_count`;
    `scales_per_matching`). Guard is presence-only — conservative by design.
  * Everything else (Tier-3 `None`, pumps, lords, removal, planeswalkers) → Main1-by-doubt.
- **Per-card doctrine:** `DecisionProvider::MainPhaseOverride(state, def)` consulted before the
  base rules (deck knowledge lives in the provider, like the discard doctrines).
  `FiveColourProvider` ships the first one: Unite the Coalition (param-keyed: damage modal) and
  Mana Cannons (cast-trigger damage) → Main2; Nicol Bolas + Oko (name-keyed; a cast-turn Oko can
  never produce an attacker) → Main2; Jared deliberately NOT listed (his −3 is a real pre-combat
  pump). Hinata needs no override — its whole Main2 set is `direct_damage` template.
- **First probe (5 games/deck, d3):** digests diverge (filter live), win turns identical
  10/10 (lossless so far), wall −2.1x (FiveColour) / −3.0x (Hinata) — later shown to be
  contention-inflated; the full battery's core-ms deltas are far smaller (see below).

## Measurement round 1 (2026-08-14, battery: FiveColour + Hinata2, 100 games/job,
## CRN seeds 300001+gi, jobs = shipping-config + d3 + d5, arms A control / B classify /
## C classify+searched-2nd / D searched-2nd only; logs/phase_classify/)

- **Arm D (MTG_SEARCH_SECOND_MAIN=1 alone) is NEUTRAL here**: hinata all 300 games
  byte-identical to control; 5c one game (ship) +1. The historical "searched measured worse"
  (antilife+hinata seed 1001) does NOT reproduce on these decks/seeds — relevant to the
  kill-the-greedy directive; needs the suite-wide re-check on the other second-main decks.
  **Suite-wide re-check (smoke s1001 at SHIPPING config, same session): the historical result
  DOES still reproduce there** — antilife d3 +0.0040, fivecolour d3 +0.0067, hinata d3
  +0.0267, digest-only churn at d5 (burn / mirrorwing / goblins-d3 byte-identical). The
  battery's neutrality was at explicit d3/d5 with a fixed 200 ms budget; at the decks'
  value-play depths/budgets the searched second main still costs a little. CONCLUSION: the
  greedy second main cannot be removed on today's measurement; it stays blocked on the
  classification parity work below, exactly as this design intends ("classification makes
  the searched second main affordable").
- **First classify round: hinata +0.15 avg at ALL of ship/d3/d5** (13–15 recurring games +1,
  gi=87 win-at-8 → unwon; depth-independent = a lost LINE). ROOT-CAUSED via gi=6 log diff:
  **Soulfire Eruption was Main2'd by the bare DirectDamage rule, but it is an impulse engine**
  (damage_equals_top_mv reveals + stages the top; the T4 win casts it MAIN1 and plays
  Crackle+Spasms out of its exile across both mains — deferred, the deploy no longer fits).
  FIX: DirectDamage with card-flow riders (draw / cast_draw / stages_cards /
  damage_equals_top_mv) → Both, not Main2 (Magma Opus included). Crackle (pure damage)
  stays Main2 — the USER's example.
- **After the fix: hinata +0.00 (ship) / +0.02 (d3) / +0.02 (d5), mixed directions**
  (gains gi=15,36,38; losses gi=71 all-configs, gi=95, gi=21/46 d3-only).
- **FiveColour: +0.01 (ship) / +0.04 (d3) / +0.01 (d5), mixed directions** (its per-card
  doctrine was already correct; no DirectDamage cards, so the Soulfire fix is a no-op here).
- **Residual failure mode is SECOND-ORDER, not per-card doctrine** — two dissected cases:
  * 5c d3 gi=10: the SEARCHED post-combat root casts ONE Birds instead of two on T2
    (control casts both main-1) → T3 is a mana short for Maelstrom Archangel → whole curve
    slips +1. Persists under arm C, so it is not greedy-vs-searched; the lookahead through
    classified future turns undervalues the double-ramp plan.
  * hinata d3 gi=71: same Ponder cast, but the searched keep/shuffle DISPOSITION flips
    (classify arm shuffles away the Ornithopter the winning line needed) — downstream
    evaluation through filtered turns re-judged the branch.
  Diagnosis: rollout/lookahead paths through classified turns under-value deferred casts
  (main-2 path is not yet at parity with the main-1-tuned machinery). This is the predicted
  hard part of the design; per its own bar (MAIN2 moves neutral-or-better per game) the
  filter is NOT adoptable for these decks until the parity work lands.
- **Perf (battery core-ms, B vs A): 5c d3 −19%, hi d3 −22%, d5 ≈0/−1%, ship ≈0/+9%.**
  Modest — these two decks' enumeration volume is not where the design's big win lives
  (that is the equipment/tribal powerset decks); the value-leaf hybrid already absorbs
  much of the d5/ship cost here.
- **Bonus pre-existing bug found by the A/B log diff — FIX DEFERRED (needs GT rebaseline)**:
  `FullSearchLineHybrid` has NO RevealLogPause — its single-pass escalation's FSLineWin calls
  run planning with the reveal logger/human choosers live. Three consequences: phantom
  planning reveals in --log-dir games (10 turn-1 Ponder reveals in the hinata gi=6 log);
  those phantom REVEALs are FOLDED into every play digest (GameLogger `FoldStr("R")`), so the
  committed GT is baselined WITH the pollution — adding the obvious pause moves 4 d5 smoke
  digests (goblins/antilife/hinata/th) at byte-identical per-game win turns; and under
  --claude-play the planning sims CONSULT THE HUMAN CHOOSERS (likely implicated in viewer
  weirdness on value-leaf decks). The fix is one `RevealLogPause` at the function top **plus
  a scheduled full GT rebaseline** (smoke/regression/overnight digest-only moves) — land it
  with the viewer HumanPlaySuppress work. TRAP for that change, measured this session:
  `g_real_resolution` must KEEP the caller's value (real-only game logic exists despite the
  flag's diagnostic-only contract — Lackey's `s_lackey_pref`); a blanket pause that clears it
  also changes the escalation's planning CHOICES. A deferred-fix comment sits at the function
  head in TurnSolver.cpp.

## Measurement round 2 — TOTAL Hinata doctrine (2026-08-14) and the main-2 parity work list

With the USER's total doctrine encoded (HinataProvider: every cast Main2, draws included):
**+0.26 (ship) / +0.35 (d3) / +0.31 (d5), ~30 games/config +1 turn, 3 unwon at d3; searched
second main does not recover it (arm C ≈ B); hinata core-ms drops ~11x** — the main-2 path
does far less work than main 1 did, i.e. it under-enumerates. Per the USER these are ENGINE
defects, and the dissections (gi=22, gi=99, plus round 1's gi=6) name them precisely. All
three capabilities below EXIST for human play and are merely gated off for the autonomous
search — the legacy "the second main is cast-only / combat creates no new resources"
assumption, which a Main2-partitioned deck invalidates:

1. **Mid-phase resource acquisition needs an in-phase re-solve in main 2.** A Gamble tutor
   (gi=22: Gamble→Spasm+Crackle same turn only works with Gamble in main 1), a draw chain,
   or a staged exile (gi=6 Soulfire) resolved in main 2 is invisible to the
   already-enumerated plan. Main 1 always got this re-solve FREE at the phase boundary —
   which is exactly why "a tutor is NOT a breakpoint site" (TurnSolver.cpp ~13694) was sound
   until now. Parity = extend breakpoint capture/re-solve to the post-combat main
   (ApplyPlanDirect's `is_pre_combat || s_human_play` gate, ~8204) AND make tutor-to-hand a
   breakpoint class there.
2. **The land drop must be offered in the post-combat main** (rules-legal; human play
   already has it): `drop_available = (is_pre_combat || s_human_play_drop)` at
   EnumeratePlansWithLand (~13138) plus the land-execution gate in ApplyPlanDirect (~8204).
   gi=99: a land drawn during main 2 could not be played that turn. Also lets main 1
   deliberately DEFER the drop past a main-2 draw. `lands_played_this_turn` bookkeeping
   already prevents double drops.
3. Gate all of it on the SAME activation as the filter (provider opt-in / lever), so every
   current autonomous digest is untouched until a deck classifies.

Sequencing note: these gates interact with the Karoo drop-reservation and the executor
lockstep (AIEngine fold_land) — implement with the usual per-change digest checks.

## Measurement plan

Per second-main deck: the standard battery (train seeds, d3+d5, per-game win-turn diff — a
MAIN2 misclassification shows up as a specific game losing a turn, not as an average drift),
wall-time, fd oracle, full smoke. Adoption per archetype provider on user approval, like
every prune.
