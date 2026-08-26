# Analysis ledger — Dragons

Per-deck ledger for `decks/Dragons/Dragons.cod` (analyze-deck skill, Stage-2 fan-in +
Stage-5 verdicts). Durable across compaction/handoff; this file is the memory, context is
disposable.

**Deck:** mono-red Dragons ramp/tribal, 60 cards.
18 Mountain, 3 Gruul Turf, 2 Haven of the Spirit Dragon / 4 Urza's Incubator, 2 Mind Stone,
1 Fire Diamond, 1 Sol Ring, 2 Dragonspeaker Shaman / 4 Scourge of Valkas, 3 Utvara Hellkite,
3 Lathliss, 3 Atsushi, 3 Inferno of the Star Mounts, 2 Glorybringer / 4 Dragon Tempest,
4 Lightning Bolt, 1 Lightning Greaves.

**Started:** 2026-08-26, on `phase-1-2-deck-analyzer` @ `0f427add`.

## Stage 1 — coverage (first run)

Already implemented (shared with Dragonstorm / Mirrorwing): Scourge of Valkas, Lightning Bolt,
Mountain, Gruul Turf, Utvara Hellkite, Lathliss, Lightning Greaves, Sol Ring. All eight
bracket notes re-read and confirmed as previously-approved Tier-4/inert deferrals.

`missing` (9): Atsushi, Inferno of the Star Mounts, Dragon Tempest, Urza's Incubator,
Mind Stone, Fire Diamond, Dragonspeaker Shaman, Glorybringer, Haven of the Spirit Dragon.

### Scryfall corrections vs. recall (2a — why you always fetch)

| card | recall | Scryfall (authoritative) |
|---|---|---|
| Inferno of the Star Mounts | 4/6 | **6/6** |
| Dragonspeaker Shaman | `{1}{R}` 2/1 Human Shaman | **`{1}{R}{R}` 2/2 Human Barbarian Shaman** |
| Atsushi | two Treasures | **three** Treasures |
| Haven of the Spirit Dragon | `{T}`, sac | **`{2}`, `{T}`, sac** |

## Stage 2 — implementation plan (tier per card)

| card | tier | mechanism |
|---|---|---|
| Fire Diamond | 2 | `mana_rock` + `enters_tapped` **honored for cast non-lands** (new) |
| Mind Stone | 1 | `mana_rock` + `produces:[C]` + existing `sacrifice_draw_cost` (Fiery Islet) |
| Dragonspeaker Shaman | 2 | `reduces_spell_subtype:"Dragon"` + new `reduces_spell_subtype_amount:2` |
| Urza's Incubator | 2 | same + new `reduces_spell_subtype_creature_only` |
| Dragon Tempest | 2 | reuse `dragon_ping_on_enter` (exactly Scourge's clause) + new `haste_on_flying_enter` |
| Inferno of the Star Mounts | 2 | `firebreathing_*` + new `firebreathing_threshold_power/_damage` (the "power becomes 20" kill) |
| Glorybringer | 1 | `vanilla_creature`, Flying+Haste keywords |
| Atsushi | 1 | `vanilla_creature`, Flying |
| Haven of the Spirit Dragon | 1 | `produces:[C,R]` + `colored_creature_only` (Unclaimed Territory precedent) |

## Engine facts established this run (do not re-derive)

* **Opponent creatures never block or attack** (`GoldFishRunner::PopulateOpponentSpawns` —
  "their purpose is to provide targets"). So trample/flying are inert, *and* nothing in this
  deck can kill our own creatures.
* `OnDragonEnters` (SpellEffects.h:2777) is the universal enter cascade; STEP 2 scans the
  battlefield for `dragon_ping_on_enter` on **any** permanent, so an enchantment pinger works
  with no new plumbing.
* `EffectHandler::EnterBattlefield` never applies `enters_tapped` — that flag is honored only
  on the land-drop path. A cast artifact that enters tapped needs it wired in both worlds.
* Legend rule is a real SBA at three sites; this deck runs 3× of three different legends, so
  it is **live** here (unlike the Lathliss bracket note's "inert, deck runs 1 copy").
* `sacrifice_draw_cost` is matched by permanent NAME, not land-gated → reusable for Mind Stone.

## Approved deferrals

USER, 2026-08-26 — approved options 1–2 as deferrals; ruled that Haven's graveyard return be
**built for real**, and separately that Urza's Incubator **must not be hard-coded** ("there will
be other decks that use it, but it should always choose dragon for this deck").

Machine-readable sign-off keys (read by `scripts/verify_deck.py`'s `## Approved deferrals` parser):

- `coverage:Atsushi, the Blazing Sky` — the modal dies-trigger, unreachable (see item 2 below).

1. **Glorybringer — exert.** "You may exert as it attacks. When you do, it deals 4 damage to
   target non-Dragon creature an opponent controls." Exert is *optional*; its cost (doesn't
   untap next turn) is strictly negative, and its payload kills a goldfish creature that never
   blocks or attacks and that no card in this deck cares about. Never-exert is therefore the
   *optimal* line, so modelling Glorybringer as a vanilla 4/4 flier with haste is byte-identical
   to a faithful implementation played correctly.
2. **Atsushi — the entire dies-trigger.** Nothing in this deck or the goldfish opponent can
   kill our creatures (no blocks, no attacks, no removal aimed at us, no sacrifice outlet),
   so the trigger is **unreachable**, not merely weak. (This is also why the coverage tool's
   one remaining `partial` — "Staged exile in oracle text but stages_cards not set" — is a
   signed-off deferral rather than a gap.)
3. **Haven's `{2}`,`{T}`, sac: return a Dragon from the graveyard — BUILT** (not deferred).
   New `Action::Kind::GraveyardReturnAbility`, one plan variant per distinct legal graveyard
   name so the target is a *searched* decision, surfaced in the viewer as `gyreturn=<card>`.
4. **Urza's Incubator — GENERIC** (not deferred). `chooses_creature_type` + the shared
   `DominantCreatureSubtypeId`: the type is read off the deck at ETB and stored as an interned
   id on `Permanent::chosen_subtype_id`. Pinned by unit tests — the same cards.json entry
   chooses Dragon next to Dragons and Sliver next to Slivers.

Still disclosed (established codebase collapses, not new decisions):
* **Haven's coloured mana** uses `colored_creature_only` (the Cavern of Souls / Unclaimed
  Territory model), so the "Dragon" narrowing on the coloured half is not modelled — it could
  also pay for the deck's 2 Dragonspeaker Shamans.
* **Inferno's power-becomes-20 rider** is implemented, but the combat pump loop is a greedy
  damage-per-mana ranker, so it never *aims* for the threshold; it fires only if the greedy
  ordering lands exactly on 20. Near-inert regardless: a 20-power attacker into an empty board
  is already 20 damage.
* Flying (all), trample (Atsushi), "can't be countered" (Inferno) — inert vs a passive opponent.

## Engine changes made this run (all byte-identical to existing decks — smoke 42/42 with digests)

| change | why |
|---|---|
| `reduces_spell_subtype_amount` / `_creature_only` | the reducer was a flat "-1 per copy"; both new reducers say "{2} less", and Incubator is creature-spells-only |
| `chooses_creature_type` + `Permanent::chosen_subtype_id` + `DominantCreatureSubtypeId` | keeps Urza's Incubator generic across decks (USER) |
| `firebreathing_threshold_power/_damage` | Inferno's "when its power becomes 20 this way" |
| `haste_on_flying_enter` | Dragon Tempest clause 1 |
| token `keywords` on `CreateToken` + `etb_created_token_keywords` / `attack_per_token_keywords` | Lathliss/Utvara tokens are printed "with flying"; the keyword was dropped as inert until Dragon Tempest made it READABLE |
| `enters_tapped` honored on the CAST path (both worlds) + gated out of the same-turn rock-ramp credit | Fire Diamond; previously a cast rock with the flag entered untapped and the enumerator funded casts off mana it could not make |
| `Action::Kind::GraveyardReturnAbility` (+ `gyreturn=` verb, registry row, auditor manifest row) | Haven's rebuy |
| `"graveyard"` staging in the `--scenario` harness | no seed-driven goldfish reaches a graveyard-reading state here, so the ability was untestable |

**Bug caught by the new fixtures:** a line consisting only of `gyreturn=` short-circuited to
"pass" at CheckLine stage 0 (the pass test enumerates every activation verb and did not know the
new one) — so an *illegal* rebuy graded `accept`. Fixed; the four `dragons_haven_rebuy_*`
scenarios pin both legal targets and two illegal ones.

## Stage 4 — baseline profile

`decks/Dragons/Dragons.profile.json` (defaults/static keep, `bottoming_enabled` off — the
expensive exhaustive mulligan stage is the USER's to kick off later, per the pipeline-ordering
policy). Notable card scores: Sol Ring **0.99**, Dragonspeaker Shaman 0.257, Inferno 0.223,
Scourge 0.145; Utvara Hellkite **−0.284** (an 8-drop), Lightning Greaves −0.158.
`min_playable = 0`, matching the other mono-colour decks (burn, Goblins).

## Stage 5 — verification

`python3 scripts/verify_deck.py decks/Dragons/Dragons.cod --no-network` → **GATE PASS**.

| check | result |
|---|---|
| coverage | DEFER — Atsushi's dies-trigger, signed off above |
| card_costs | `audit_card_costs.py`: "All mana costs match Scryfall" over the 203 that resolved; 83 unresolved were all HTTP-429/timeout transients, so the 9 new cards were closed against the freshly-refreshed snapshot instead (cost + P/T verified card-by-card, all 9 OK) |
| card_fields | all hard fields (cost, P/T, types, keywords) match the Scryfall snapshot, 266 cards, exit 0. One HARD flag resolved: Scryfall lists `exert` on Glorybringer, allowlisted in `scryfall_divergences.json` as the approved deferral (Progenitus/Piledriver precedent) |
| viewer | PASS — self-guard + surface sweep clean |
| viewer_wiring | PASS — 2 types wired end-to-end (`bounce`, `target`) |
| mismatch (5a) | **PASS — zero `[nonconv]`, zero `[fd-diverge]`** over seeds 7001/7002 × 60 games |
| play_invariants | PASS — 8 games / 1120 decisions: determinism + integrity + progress hold |
| claude_sweep (5d) | NOT RUN — needs the Sonnet subagent fan-out; this session was instructed not to spawn agents |

**5b multi-depth sanity** (200 games, seed 2002, profile attached): d0 **6.240** → d3 **5.735**
→ d5 **5.735**. Monotonic (non-increasing with depth) and plausible for a deck whose payoffs
cost 5–8 mana. d3 == d5 exactly: the search has converged at this budget.

**5c2 horizon-honest tie-break** (`leaf_tiebreak_check.py`) — run at BOTH sizes, and the small
one would have misled:

| sample | paired games | binding | net turns | worse / better |
|---|---|---|---|---|
| default | 12,000 | 1 (0.008%) | **−1** | 0 / 1 |
| `--blocks 121` | 121,000 | 9 (0.007%) | **+5** | 7 / 2 |

**The sign FLIPPED between the two** — the 1-event sample said "helps", the 9-event sample says
"hurts". That is the underpowered-sample trap, and it is exactly why the script refuses to call a
direction below 20 changed games; a one-event read is worth nothing.

**DECISION: keep the default (ON).** Not because the sample cleared the bar — it did not, the
verdict is "NO SIGN AT THIS SAMPLE" both times — but because the lever demonstrably almost never
fires here, and the skill's own escape clause covers exactly this case: *"If it stays unbindable at
a large sample, the default (ON) is fine by default: a lever that never fires costs the deck
nothing."* Supporting numbers:

* **Binding is stable at ~0.007%** across a 10x sample increase, so this is a property of the deck,
  not undersampling: Dragons wins well inside the horizon (avg 5.7), so a rollout rarely reaches
  the horizon *without* a win and the no-win leaf is rarely consulted at all.
* **The effect is negligible even taken at face value:** +5 turns over 121,000 games is
  **+0.000041 turns/game**.
* **It is not statistically distinguishable from noise:** 7-of-9 events in one direction is
  two-sided binomial p = 0.18 against a 50/50 null.

Reaching the script's 20-event bar needs roughly `--blocks 270` (~8 h) to resolve an effect of
4e-5 turns/game. Not worth the compute; flagged to the user rather than decided silently. If the
deck is ever added to the regression suite, revisit — the suite's pinned d3/d5 gate cells bind
differently from PLAY settings (`--gate-cells`).

**Suite regressions after all engine changes:** smoke 42/42 and regression 70/70, byte-identical
*including play digests*, plus reference reproducibility 224 refs / 0 play-drift / 0 enum-gap.

## Stage 6a — provider disclosure

`SelectDecisionProvider` routes Dragons to **`GenericProvider`** — it overrides nothing, so there
are **no deck-specific narrowing heuristics**; play is pure search within the global assumptions
(single passive opponent that never blocks/casts/gains life, clairvoyant search over a
deterministically-shuffled library, first main only — `DeckUsesSecondMain` does not fire, no
spectacle or combat-generated resources in this list).

Detection was checked against every archetype signature: no `tutor_to_battlefield` (Dragonstorm),
no `landfall_damage` (burn), no `domain_mana` (FiveColour), none of the equipment params (Lightning
Greaves carries only `is_equipment`/`equip_*`), and Mind Stone's `sacrifice_draw_cost` no longer
trips Treasure-Hunt detection.

## Open convergence loops

* **5d claude-play sweep — NOT RUN.** The one gate item still open. It needs the ~15–20 game
  Sonnet subagent fan-out from `.claude/skills/claude-play.md`; this session was instructed not to
  spawn agents, so it is deferred to the user. `play_invariants` (determinism / integrity /
  progress over 1120 decisions) already guards the protocol mechanically, but it is not a
  substitute — the sweep's value is a second opinion on *legality and missed lines*.
* **5c2 CLOSED** at 121,000 paired games — default kept ON, see the table above. Re-open only if
  someone wants the ~8 h `--blocks 270` run to resolve a 4e-5 turns/game effect.
* **Not in the regression suite.** Adding Dragons to `test/regression_cases.sh` costs shared
  per-mode time budget, so it is the user's call (same status as FiveColour and Creature Giving
  when they were analyzed).
* **Mulligan generation not run** — deliberately: it is the LAST stage and the user kicks it off
  (`.claude/skills/mulligan-profile.md`), after perf and viewer testing settle.
