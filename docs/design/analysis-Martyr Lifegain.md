# Analysis ledger — Martyr Lifegain

Deck: `decks/Martyr Lifegain/Martyr Lifegain.cod` (60, mono-white + 2 Orzhov Basilica; Martyr of
Sands / Serra Ascendant / Speaker of the Heavens life-total payoffs, Abiding Grace + Lurrus
recursion, Ranger of Eos / Ranger-Captain tutors; no sideboard, so no wish reach).
Started 2026-11-10 on branch `phase-1-2-deck-analyzer` (base 5db229fa6).

## Stage 1 — coverage (2026-11-10)

Already implemented (12): Ranger of Eos, Ranger-Captain of Eos, Serra Ascendant, Martyr of Sands,
Swords to Plowshares, Unexpectedly Absent, Speaker of the Heavens, Orzhov Basilica, Plains,
Abiding Grace, Mother of Runes, Umezawa's Jitte. No fetchlands (`fetch_hold_review.required=false`).

Missing (6): **Lurrus of the Dream-Den, Squadron Hawk, Kami of False Hope, Thraben Inspector,
Curious Farm Animals, Enduring Innocence.**

## Stage 2 — cards

Research fan-out (2026-11-10): four Opus agents, read-only (Hawk + Thraben Inspector and Kami + Curious Farm Animals paired);
integration serial.

| card | tier | model | viewer bucket | status |
|---|---|---|---|---|
| Lurrus of the Dream-Den | 3 | `gy_cast_permanent_max_mv 2`: once-per-turn per-object graveyard CAST permission (reuses `once_per_turn_used`), `CastFromGraveyard` actions with `gy_cast_source_id`, executor gy-cast path through `CastSpellFromHand`, site-10 re-solve on a new legal gy card / Lurrus entry, `GyR_GyCastNames` dominance bit; Companion inert tag | B: `gycast=` verb + clickable graveyard | drafted (integrator B) |
| Squadron Hawk | 1 | `etb_tutor_hand_count 3` + `wish_requires_name` (Ranger of Eos multi path) | A: `sac_tutor` | drafted (integrator A) |
| Kami of False Hope | 2/3 | self-only sac outlet + `sac_outlet_prevent_combat_damage` (`GameState::combat_damage_prevented_turn`, read by combat core + PendingAttackDamage) — a pure cost vs the goldfish | A: `sacout=` | drafted (integrator A) |
| Thraben Inspector | 2 | `etb_investigate 1` -> `CreateClueTokens` in `FireOwnEtbTriggers` | A: Clue SacDraw | drafted (integrator A) |
| Curious Farm Animals | 2 | `dies_trigger_self_gain 3` + `{2}` self-only sac outlet + optional destroy artifact/enchantment (zero-target dominance) | A: `sacout=`, `dig` (may destroy) | drafted (integrator A) |
| Enduring Innocence | 2 + small 3 | own-creature-enter draw, `creature_enters_max_power 2`, once each turn per copy; dies-if-creature -> returns as noncreature Enchantment | none (mandatory) | drafted (integrator A) |

Drafts (full): `logs/martyr-lifegain/drafts/0{1..4}-*.md` (gitignored scratch; the table above is the durable record).

### Open questions raised by research (provider stage; USER-owned ones marked)
- **[USER] Second main.** Kami / Farm Animals / Martyr+Lurrus want "attack, then sacrifice"; Speaker of
  the Heavens can cross life 27 via lifelink combat damage. Card integration is first-main only.
- **[USER] Cast order.** Enduring Innocence's draw is lost if a power<=2 creature is cast before it the same turn.
- Jitte -1/-1 on our own creature is pruned in autonomous search; here it can pay (Farm Animals 3 life, Grace/Lurrus re-buy).
- "Cast Abiding Grace, then sacrifice in the same plan" may need a re-collect breakpoint.
- Clue created at END STEP (Grace returning Inspector) cannot be cracked that turn (no instant-speed window).

## Provider

(Stage 4a — pending)

## Verification

(Stage 5 — pending)

## Provisional decisions awaiting the user

(none yet)

## Approved deferrals

(none)
