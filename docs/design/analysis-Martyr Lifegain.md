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
| Lurrus of the Dream-Den | 3 | `gy_cast_permanent_max_mv 2`: once-per-turn per-object graveyard CAST permission (reuses `once_per_turn_used`), `CastFromGraveyard` actions with `gy_cast_source_id`, executor gy-cast path through `CastSpellFromHand`, site-10 re-solve on a new legal gy card / Lurrus entry, `GyR_GyCastNames` dominance bit; Companion inert tag | B: `gycast=` verb + clickable graveyard | integrating (integrator B) |
| Squadron Hawk | 1 | `etb_tutor_hand_count 3` + `wish_requires_name` (Ranger of Eos multi path) | A: `sac_tutor` | IMPLEMENTED f402e5aeb |
| Kami of False Hope | 2/3 | self-only sac outlet + `sac_outlet_prevent_combat_damage` (`GameState::combat_damage_prevented_turn`, read by combat core + PendingAttackDamage) — a pure cost vs the goldfish | A: `sacout=` | IMPLEMENTED e419123be |
| Thraben Inspector | 2 | `etb_investigate 1` -> `CreateClueTokens` in `FireOwnEtbTriggers` | A: Clue SacDraw | IMPLEMENTED f402e5aeb |
| Curious Farm Animals | 2 | `dies_trigger_self_gain 3` + `{2}` self-only sac outlet + optional destroy artifact/enchantment (zero-target dominance) | A: `sacout=`, `dig` (may destroy) | IMPLEMENTED e419123be |
| Enduring Innocence | 2 + small 3 | own-creature-enter draw, `creature_enters_max_power 2`, once each turn per copy; dies-if-creature -> returns as noncreature Enchantment | none (mandatory) | IMPLEMENTED 539fe952d |

Drafts (full): `logs/martyr-lifegain/drafts/0{1..4}-*.md` (gitignored scratch; the table above is the durable record).

Integrator A notes (2026-11-10): 15 unit cases (`test/unit/test_martyr_lifegain_cards.cpp`) + 7
`test/scenarios/martyr_*.json` PASS (scenario suite 452/452); smoke 182/183, the one mismatch
(`extraturns2hg_smoke_d5_s1001`) is PRE-EXISTING (GT rewritten by f7cafbc15 with no source change; our binary
reproduces the pre-f7cafbc15 value) — not ours, nothing accepted. Deviations: Angels signature now also requires
`wish_from_sideboard` (Hawk's `wish_requires_name` would otherwise route the deck to AngelsProvider; Legion Angel
has both, Angels unchanged); Kami flag is a bool reset at both untap sites (`combat_damage_prevented_this_turn`),
folded into every key; Farm Animals destroy resolves after its own dies-gain; a destroyed token now ceases to
exist (human-play only); self-sac gate counts the source's own dies payload. Deck routes to **EquipmentProvider**
today (with or without the five cards) — provider stage. `test_pirates_provider.cpp` routing table needs the
new deck pinned.
PROVISIONAL deferrals: end-step Clue not crackable that turn; Hawk takes full count; Kami / Farm Animals
instant-speed activation not modelled.

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
