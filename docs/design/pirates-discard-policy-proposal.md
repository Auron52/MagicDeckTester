# Pirates — cleanup-discard BUCKET policy (PROPOSAL, 2026-09-26)

Authored from `docs/design/discard-bucket-authoring-brief.md` during the Pirates onboarding (chunk 3).
**Status: USER-REVIEWED 2026-09-27 and revised (see "USER revision" below); implemented default-on.** Unlike the 2026-09-23 fleet sweep (proposal
first, integration later), this deck is being onboarded fresh, so the policy was implemented in the
same change to give `verify_deck.py`'s blocking `discard_policy` gate something real to check and to
keep the rollout off max-MV from the first measurement. Adoption is still a user review — the same
gate as cast order — and every choice below is open to amendment.

* Code: `PiratesProvider::CleanupDiscardCandidates` (`src/ai/DecisionProviders.cpp`, search for
  `---- PiratesProvider::CleanupDiscardCandidates`).
* Flag: `MTG_PIRATES_BUCKET_DISCARD` — default ON via `EnvOn(..., true)`; `=0` restores
  `GenericProvider::CleanupDiscardCandidates` (the max-MV fallback) as the A/B hatch.
* Tests: `test/unit/test_pirates_provider.cpp` (7 discard cases).

---

## 1. The deck, and therefore its shape

U/R/b Pirate tribal aggro on Aether Vial: 24 lands, 4 Aether Vial, 31 creatures, 1 Lightning Bolt.
**The curve tops at 3** — nothing costs more — and the damage is combat plus two leaks: Forerunner of
the Coalition drains 1 per other Pirate entering, and the lone Bolt. To function the deck needs (a)
land drops up to three, with a fourth for Daring Buccaneer's unrevealed `{2}`, a Fiery Islet sac or a
Vial-plus-3-drop turn; (b) blue for Staunch Crewmate / Corsair Captain / Kitesail Larcenist / Siren
Stormtamer / Malcolm, red for the 1-drops and Bolt, black for Dire Fleet Captain / Forerunner; (c)
bodies, preferably the ones that multiply other bodies (two lords, Metallic Mimic's counters, Dire
Fleet Captain's per-attacker pump).

**Shape: simple aggro → TWO buckets (MANA, THREATS), with the Aether Vial as a sub-role of MANA.**
There is no combo to split, no ramp, and no dig engine beyond Staunch Crewmate's one-shot ETB (which
is a body first). I considered a separate ENABLERS bucket for Mimic / Automaton (they are what make
the tribe bigger) and rejected it: both are also bodies that attack, and the value order below
already keeps them above every plain body — a bucket boundary would add a quota with no card it
could protect that the order does not.

## 2. Card-by-card role table

All 19 names read from `src/cards/data/cards.json` (Rule 0).

| card | n | cost | P/T | key `params` (the classification keys) | bucket / value |
|---|---|---|---|---|---|
| Corsair Captain | 4 | {2}{U} | 2/2 | `power_bonus 1`, `subtypes_affected [Pirate]`, `etb_creates_treasures 1` | THREAT — lord **104** |
| Adaptive Automaton | 4 | {3} | 2/2 | `power_bonus 1`, `lord_affects_chosen_subtype`, `chosen_type_added_to_self` | THREAT — lord **100** |
| Metallic Mimic | 4 | {2} | 2/1 | `other_chosen_subtype_enters_counters 1`, `chosen_type_added_to_self` | THREAT — **92** |
| Dire Fleet Captain | 4 | {B}{R} | 2/2 | `attack_pump_power_per_other_matching 1` (+tough) | THREAT — **86** |
| Forerunner of the Coalition | 2 | {2}{B} | 2/2 | `own_creature_enters_opp_life_loss 1`, `tutor_to_top` | THREAT — **82** |
| Malcolm, the Eyes | 1 | {U}{R} | 2/2 flying haste, legendary | `nth_spell_investigate 1` | THREAT — **75** |
| Kitesail Larcenist | 2 | {2}{U} | 2/3 flying ward | `etb_treasurify_each_player` | THREAT — **72** |
| Staunch Crewmate | 4 | {1}{U} | 2/1 | `etb_dig_count 4` | THREAT — **62** |
| Lightning Bolt | 1 | {R} | — | `damage 3` (non-creature) | THREAT — **56** |
| Goblin Tomb Raider | 4 | {R} | 1/2 | `static_artifact_threshold 1` (+1/+0, haste) | THREAT — **38**, **46** with an artifact on board or in hand |
| Daring Buccaneer | 4 | {R} | 2/2 | `reveal_or_pay_subtype Pirate`, `reveal_or_pay_cost {2}` | THREAT — **42** |
| Siren Stormtamer | 2 | {U} | 1/1 flying | none (activation unmodelled — ledger deferral) | THREAT — **40** |
| Aether Vial | 4 | {1} | — | `upkeep_adds_charge` | MANA / VIAL |
| Secluded Courtyard | 4 | land | — | `produces` WUBRGC, `colored_creature_only` | MANA / LANDS (all colours, creatures only) |
| Unclaimed Territory | 4 | land | — | `produces` WUBRGC, `colored_creature_only` | MANA / LANDS (all colours, creatures only) |
| Spirebluff Canal | 4 | land | — | `produces` UR, `fastland_max_other_lands 2` | MANA / LANDS |
| Fiery Islet | 2 | land | — | `produces` UR, `sacrifice_draw_cost {1}` | MANA / LANDS (kept last among surplus) |
| Blackcleave Cliffs | 1 | land | — | `produces` BR, fastland | MANA / LANDS |
| Mountain | 5 | land | — | `produces` R | MANA / LANDS (narrowest — shed first) |

Generic creature value = `30 + 4·power + 2·toughness + 4·flying`, plus the conditional static
(`4·static_artifact_power + 4 if static_artifact_haste`) only while an artifact is available.

**Genuinely ambiguous:** Lightning Bolt (reach vs body — see Doubts), Goblin Tomb Raider vs Daring
Buccaneer vs Siren Stormtamer (three near-equal 1-drops; the order among them is the most likely thing
to be amended), and Kitesail Larcenist (a 2/3 flier whose ETB is mostly a formality in a goldfish).

## 3. The buckets, with quotas (all net of board)

| bucket | quota | filled from the battlefield by |
|---|---|---|
| MANA / LANDS | reach **4 sources**: `max(0, 4 − board_sources)` owed by the hand. The **first two** land slots are filled BEFORE the threat floor, the rest after it | every land we control + any `mana_rock` permanent |
| MANA / VIAL | **1**, only while NO Vial is on the battlefield AND the board has **≤ 2 lands**; otherwise 0 | a Vial we control zeroes it outright |
| THREATS | catch-all; **hard floor 3** (taken before the late land slots) | — (threats are not netted: a body on board does not make a body in hand redundant in an aggro deck) |

Sub-quotas are fungible upward in the one place that matters: the Vial does NOT count toward the
land quota (it is not a land drop), so a Vial-heavy hand keeps its lands.

## 4. Within-bucket order and distance-to-playable

* **Lands (keep order):** colour coverage first — greedily, the land that adds the most colours the
  hand needs and board + already-kept lands lack (creature pips can be met by a creature-only land;
  Bolt's `{R}` cannot). Then breadth (distinct colours produced ×2), with `sacrifice_draw_cost` (Fiery
  Islet) as the +1 tie-break: a surplus Islet is still a card later.
* **Threats (keep order):** NEAR before FAR, then value (table above), then hand order.
* **Distance-to-playable:** reach = board sources + every land in hand. A threat's distance =
  `max(0, effective cost − reach)` + 1 if a colour it needs has no source on board or in hand. FAR =
  distance ≥ 2; FAR threats shed ahead of every near threat. Distance 1 (one land drop away) does not
  demote. Two erasers: a **board Aether Vial** whose counters + 1 reach the creature's MV sets its
  distance to 0 (Vial is colour-blind); **Daring Buccaneer's** effective cost is 1 with another
  Pirate CARD in hand (the reveal — Mimic/Automaton in hand do not count, they are Pirates only on
  the battlefield), 3 otherwise.

## 5. The total order over a hand (index 0 first)

1. **S0** a legendary whose name is already on our battlefield or earlier in hand (a dead Malcolm).
2. **S1** surplus lands, reverse keep order (Mountain first, Fiery Islet last).
3. **S2** surplus Vials (any Vial beyond the quota — every Vial once one is on board, or once the board
   has 3+ lands).
4. **S3** overflow threats, weakest first: FAR before near, then ascending value.
5. **Tail** every quota-kept card, last-acquired first (so: late lands, then the Vial, then the threat
   floor worst-to-best, then the first two lands).

Every non-staged hand card appears exactly once, so the shared fallback's max-MV tier B never decides
anything. The list goes through `CleanupDiscardRankingWithOrder`, so staged-card and required-piece
protections stay engine-enforced.

## 6. Param-level predicates (as implemented)

* land → `CleanupDiscardIsLand(card)`; colours from `params.produces`, split by `colored_creature_only`.
* vial → `params.upkeep_adds_charge`.
* dead legend → `card.HasSupertype(Legendary)` and the name already seen.
* lord → `IsCreature && power_bonus > 0 && (!subtypes_affected.empty() || lord_affects_chosen_subtype)`
  (+4 for `etb_creates_treasures`). Dire Fleet Captain carries `subtypes_affected` but `power_bonus 0`,
  so it does not match.
* counters engine → `other_chosen_subtype_enters_counters > 0`.
* attack pump → `attack_pump_power_per_other_matching > 0`.
* drain / tutor body → `own_creature_enters_opp_life_loss > 0 || tutor_to_top`.
* value flier → `Flying && (nth_spell_investigate > 0 || etb_treasurify_each_player)` (+3 `Haste`).
* dig body → `etb_dig_count > 0`.
* burn → non-creature with `damage > 0`.
* conditional static → `static_artifact_threshold > 0` with an artifact on board or held.
* reveal cost → `reveal_or_pay_subtype` / `reveal_or_pay_cost`.

No card name appears in the policy. A screening arm that swaps, say, a lord for another lord keeps
its tier.

## 7. State promotions

Adopted (cleanup can act on them honestly): the Vial-on-board eraser, the Buccaneer reveal, the
artifact-available switch for Tomb Raider, and the dead-legend demotion.

**Rejected as search-owned:** "Bolt is lethal next turn" and "a lord makes next turn's alpha strike
lethal" — both are damage projections, which a cleanup ranking cannot compute soundly and the search
already prices. Also rejected: a Mimic-before-more-Pirates promotion (Mimic is worth more when more
Pirates will follow it) — that is a CAST-ORDER question, and cast order is a separate user review.

## 8. Doubts, flagged for the user

1. **Land target 4 vs 3.** The curve tops at 3; the fourth source is for Buccaneer's `{2}`, Islet,
   and Vial + 3-drop turns. 3 would shed lands a turn earlier.
2. **Vial rule.** "Kept only while no Vial on board and ≤ 2 lands" sheds a lone Vial over a Siren
   Stormtamer from land 3 on. Minotaur's user-revised doctrine ("Vial sheds with the mana, later than
   turn 1") points the same way; confirm it holds for a deck whose creatures are all MV 1–3.
3. **1-drop order** (Stormtamer 40 < Buccaneer 42 < Tomb Raider 38/46). Stormtamer's counterspell
   ability is unmodelled, so the engine sees a 1/1 flier; if it ever gets modelled it moves up.
4. **Lightning Bolt at 56** — above the 1-drop bodies, below Crewmate. In a goldfish Bolt is 3
   unconditional face damage; a body attacks repeatedly. Could reasonably sit either side of Crewmate.
5. **Threat floor 3 before lands 3–4.** A hand with 2 lands on board, 2 in hand and 4 threats sheds a
   threat before the 4th source; the alternative (all lands first) would pitch a real card for a
   land the curve does not need.
6. **Fastlands are not demoted** once the board has 3+ lands (they would enter tapped). Judged too
   small to earn a rule; flag if the viewer shows otherwise.
7. **Treasures are not counted as sources** (one-shot). Corsair Captain / Larcenist Treasures can
   make a surplus land look needed; the effect is at most one land slot.

## USER revision (2026-09-27) -- supersedes sections 3-5 and doubts 1, 2, 5 where they differ

The user's words, then what changed in `PiratesProvider::CleanupDiscardCandidates`:

* *"I recommend keeping enough for 3 lands total."* -> land quota = 3 lands in total, battlefield first
  (was 4 sources). Colour-coverage keep order unchanged. Every kept land is shed LAST.
* *"Metallic Mimic should be dropped below Dire Fleet captain unless we are turn 1."* -> Mimic 85 (Dire
  Fleet Captain 86), 92 on turn 1.
* *"if we have too few lands out to reliably play card x next turn I would only keep enough other
  threats to curve out. The only case we want to drop the cards entirely is if we think they cannot be
  played. So, if we had 1 land total and no Aether Vial out you would drop 3-drops for sure. On the other
  hand, if you had a land out and lands 2 and 3 in hand you would want to keep a 2 and 3 drop of
  sufficient value according to the chart."* -> the fixed threat floor of 3 is REPLACED by a curve-out
  plan: mana on our next turn k = board lands + min(k, kept hand lands) for k = 1..3, a board Vial at c
  counters puts one creature of MV c+k free on turn k; the value-maximising assignment of threats to those
  turns is the PLAN. A threat that cannot be cast even with every hand land (and no Vial can put it), or
  whose colour has no source on board or in hand, is UNPLAYABLE and goes first.
* *"I would probably discard Vial if it was end of turn 2. It's too slow to play at that point. Unless we
  are in bad shape on the land front? (if we are mana-starved otherwise keeping it would be an
  option)"* -> one Vial is kept only on turn 1, or when mana-starved (board + hand lands < 3), and never
  with a Vial already on the battlefield.
* Context the user gave for all of it: *"these situations should only happen in less ideal branches ...
  The main reason for the discard heuristic is for cases where we are mana screwed or to correctly act in
  suboptimal branches."*

Doubts 3 (1-drop order), 4 (Bolt at 56), 6 (fastlands) and 7 (Treasures) had no comment and keep their
authored defaults.

**Shed order now:** dead legend -> threats that cannot be played -> surplus lands (narrowest first, Fiery
Islet last) -> surplus Vials -> off-plan threats, lowest value first -> the kept cards backwards (plan
threats worst-first, then the kept Vial, then the kept lands).

Unit cover: `test/unit/test_pirates_provider.cpp` -- the four "USER review" cases are the user's own
examples.

## Evidence still owed (not blocking the review, but required before calling it adopted)

* `scripts/analyze_deck.py decks/Pirates/Pirates.cod --discard-analysis` (evidence pass: shed rate,
  tie rate, rule-vs-searched regret). Needs the Stage 4 profile first; not run in chunk 3 because the
  box was held by another container (no batch runs allowed).
* Non-inferiority vs `MTG_PIRATES_BUCKET_DISCARD=0` once the deck is in the suite. The provider does
  not narrow (it returns the full hand), so no fan lever is needed for the searched trace.
