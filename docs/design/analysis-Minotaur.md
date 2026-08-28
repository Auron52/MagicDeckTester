# Analysis ledger — Minotaur

Per-deck ledger for the `analyze-deck` run on `decks/Minotaur/Minotaur.cod` (2026-08-23, run
autonomously overnight at the user's request; user unavailable until Monday 2026-08-24).

Deck: 60 cards, Rakdos (B/R) Minotaur tribal aggro. 18 lands + 2 Aether Vial, 40 spells.

```
2 Boros Reckoner        3 Fanatic of Mogis      4 Rageblood Shaman     4 Kragma Warcaller
4 Deathbellow Raider    4 Ragemonger            4 Gnarled Scarhide     3 Neheb, the Worthy
4 Burning-Fist Minotaur 4 Slaughter-Priest of Mogis                    2 Sethron, Hurloon General
2 Aether Vial
5 Mountain  2 Blood Crypt  2 Rakdos Carnarium  4 Unclaimed Territory  4 Secluded Courtyard
1 Cavern of Souls  2 Bloodstained Mire
```

## Headline

| | |
|---|---|
| **Average turn-to-win (the metric)** | **d0 5.51 · d3 5.02 / 4.97 · d5 5.03 / 4.96** (regression tier, seeds 2002/3003, loss = 9) |
| Loss rate | ~1.2% (all reproduced: genuine mana screw, no play bugs) |
| Provider | `GenericProvider` — **no deck-specific heuristics**, pure search |
| Cards implemented | 12, with **no Tier 1–3 clause left unmodelled** |
| Engine bugs found and fixed | **2** (a viewer hang, and a provider misroute that deleted a legal branch) |

## Stage 1 — coverage

12 missing, 0 partial-with-gaps. Reused as-is: Blood Crypt, Mountain, Unclaimed Territory,
Secluded Courtyard, Cavern of Souls, Bloodstained Mire, Aether Vial.

All 12 oracle texts fetched from Scryfall (`logs/minotaur_scryfall/`). **Slaughter-Priest of Mogis
was cross-checked through three independent Scryfall routes** because the agent's recall disagreed
with the API. The API was right and the recall was wrong — the printed card is the
sacrifice-watcher / first-strike one, not an ETB pinger. (Skill Rule 0, working as intended.)

## Stage 2 — implementation

| Card | Tier | How it is modelled |
|---|---|---|
| Rakdos Carnarium | 1 | Karoo: `enters_tapped` + `etb_bounce_land`, produces {B}{R} ×2 |
| Rageblood Shaman | 1 | `lord_effect` +1/+1 to other Minotaurs |
| Boros Reckoner | 1 | vanilla 3/3; real hybrid `{R/W}{R/W}{R/W}` → 3 red devotion |
| Deathbellow Raider | 2 | `must_attack` (CR 508.1a restriction, ahead of every provider hold) |
| Kragma Warcaller | 2 | `grants_haste` + `attack_pump_matching_power` (team attack trigger) |
| Ragemonger | 2 | `reduces_subtype_colored_*` — coloured-only reduction, hybrid-safe |
| Fanatic of Mogis | 2 | `etb_damage_devotion_color` + a new `DevotionTo()` (CR 700.5) |
| Neheb, the Worthy | 2 | `hand_size_anthem_*` (conditional anthem) + `combat_damage_each_discards` |
| Burning-Fist Minotaur | 2 | `firebreathing_*` + `firebreathing_discard` → searched `ActivatePump` |
| Sethron, Hurloon General | 2 | `etb_token_includes_self` + `team_pump_*` with `team_pump_grants_haste` |
| Slaughter-Priest of Mogis | 2 | `sacrifice_watch_pump_power` + a widened sac outlet |
| Gnarled Scarhide | 3 | `bestow_cost` → a synthesized "(Bestowed)" aura face; both modes searched |

Two design choices worth the user's attention:

* **Burning-Fist's pump is deliberately NOT in the greedy combat-mana converter.** Its cost includes
  a CARD, which a damage-per-mana ratio cannot price — and in this deck an emptied hand is itself a
  payoff (Neheb's anthem). It is enumerated as a searched main-phase action instead. That is the
  repo's no-greedy-step-inside-the-searched-window rule applied to a new card.
* **Bestow is a real, searched choice.** The creature mode is 3 mana cheaper and is itself a
  lord-buffed Minotaur that triggers Sethron; the aura mode costs more but its +2 power lands on a
  creature that can attack *this* turn. Neither dominates, so the search picks.

## Deferrals — D1–D11 USER-APPROVED (2026-08-28), D12 FIXED

USER (2026-08-28): *"the rest of the deferrals seem okay as they are only relevant with an
opponent"* — D1–D11 stand approved as-is. D12 is FIXED the same day: Secluded Courtyard gained
`colored_creature_ability_ok` (its coloured mana is payment-legal for an activated ability of a
creature source, per its oracle text), threaded via the `CreatureAbilityPayScope` context the
`ActivatePump` payment sites install — so Burning-Fist's and Sethron's activations now pay off the
tribal lands. Cast-only lands (Cavern of Souls / Unclaimed Territory / Sliver Hive) are unchanged,
pinned by unit test. Measured: Minotaur FASTER in every arm (smoke d0 −0.0100 / d3 −0.0080 /
d5 −0.0134; regression 5 rows −0.0040..−0.0120, searched slower=0), all other decks byte-identical,
247-reference sweep 0 play-drift. Original proposal table kept below for the record.

Per the skill, every deferral needs explicit sign-off. The user was asleep, so each is implemented
as an inert collapse and listed here for review — **not** treated as approved. All belong to the one
class the skill names as legitimate: this engine's opponent never blocks, casts, deals damage, or
targets our permanents (`src/ai/Combat.cpp` has no blocker code path at all).

| # | Card | Clause | Why inert |
|---|---|---|---|
| D1 | Boros Reckoner | "whenever this is dealt damage, it deals that much to any target" | nothing in the sim ever damages our creatures — the trigger has no event that can fire it |
| D2 | Boros Reckoner | "{R/W}: gains first strike" | first strike is only observable against blockers |
| D3 | Rageblood Shaman | trample (own + granted) | trample only matters when a blocker absorbs damage |
| D4 | Neheb | first strike (own + granted) | as D2 |
| D5 | Burning-Fist Minotaur | first strike | as D2 |
| D6 | Gnarled Scarhide | both "can't block" clauses | we never block |
| D7 | Sethron | the "menace" rider | menace only restricts blockers |
| D8 | Deathbellow Raider | "{2}{B}: Regenerate" | regeneration replaces DESTRUCTION; nothing destroys our creatures |
| D9 | Slaughter-Priest | the outlet's first-strike payload | as D2 — the outlet is still implemented, because its *cost* fires the card's live +2/+0 watcher |
| D10 | Neheb | the OPPONENT's half of "each player discards" | the passive opponent never casts and no decision reads their hand |
| D11 | Boros Reckoner / Sethron | hybrid pips render as their first colour in the play digest | pre-existing engine-wide convention (`ManaCost::ToString`); payment is hybrid-correct |

**D12 — a real (small) fidelity gap, NOT a collapse.** Secluded Courtyard's oracle text allows its
coloured mana to "activate an ability of a creature source of the chosen type", but the engine
models all three tribal lands with one flag (`colored_creature_only`) that permits coloured mana
only for a creature SPELL. So Burning-Fist's and Sethron's activated abilities are slightly harder
to pay than they should be. This is **conservative** (never permits an illegal play), **pre-existing**
(shared with every other deck running these lands), and fixing it means threading a third mode
through the mana solver — which touches every deck's hot path. Deliberately not attempted
unattended; flagged for the user to schedule.

## Stage 5 — verification results

| Check | Result |
|---|---|
| Coverage (Stage 3 re-run) | CLEAN — 0 missing, 0 partial-with-gaps |
| 2d-bis `audit_card_costs.py` | **All mana costs match Scryfall**; all 11 costed Minotaur cards resolved and matched |
| **Byte-identity, smoke** | **42/42 PASS**, 0 play changes for the 13 existing decks |
| **Byte-identity, regression** | **65/65 PASS**; viewer-protocol references clean (0 play-drift, 0 enum-gap) |
| 5a nonconv (d3 ×300, d5 ×200) | **0 flagged** |
| 5a fd-diverge (d5 ×300, both seeds) | **0 flagged** |
| 5b multi-depth | d0 5.464 → d3 4.996 → d5 4.994 — monotone; on MATCHED samples d3 and d5 are identical game-for-game (search converged at d3, games end ~T5) |
| 5b outliers | every unwon game reproduced and classified — all genuine mana screw, none a play bug |
| 5c2 horizon-honest tie-break | binds on **1 game in 12,000** (0.008%); that one is BETTER. Deck wins well inside the horizon; default ON is correct and there is nothing to opt out of |
| 5d claude-play legality sweep | **0 legality/invariant violations** over 6 driven games (every plan's casts in hand, every activation's source on the battlefield, no over-budget plan, no land-drop violation) — and it found the viewer hang below |
| 5h viewer decision surface | **PASS** — `bounce`, `discard`, `land_entry`, `sacrifice`, `vial_charge` all surfaced |
| 5g heuristic mining (600 games, 2 seeds, 2,969 decisions) | **no rule clears the bar** — a useful negative result, detailed below |
| 5i discard analysis | `STATUS_QUO_OK`; only **10 cleanup sheds in 400 games**. Bucket policy authored as a proposal → `minotaur-discard-policy-proposal.md` |
| Devotion arithmetic | hand-checked on 8 Fanatic casts against red pips — exact every time, incl. the self-count and same-turn cast ORDER |
| Ragemonger cost reduction | verified against the ACTUAL `manaPaid` in 200 games of logs, across every Minotaur and 0/1/2/3 Ragemongers in play (table below) |

#### Ragemonger verification (real `manaPaid` from game logs)

| card | printed | 0 Ragemonger | 1 | 2 | 3 |
|---|---|---|---|---|---|
| Fanatic of Mogis | `{3}{R}` | `{3}{R}` | `{3}` | `{3}` | — |
| Kragma Warcaller | `{3}{B}{R}` | `{3}{B}{R}` | `{3}` | `{3}` | `{3}` |
| Neheb, the Worthy | `{1}{B}{R}` | `{1}{B}{R}` | `{1}` | `{1}` | — |
| Rageblood Shaman | `{1}{R}{R}` | `{1}{R}{R}` | `{1}{R}` | `{1}` | — |
| Sethron | `{3}{R}{R}` | `{3}{R}{R}` | `{3}{R}` | — | — |
| **Boros Reckoner** | `{R/W}{R/W}{R/W}` | `{R}{R}{R}` | **`{R}{R}`** | — | — |

Every cell is correct: the GENERIC is never reduced (the reminder text's own `{2}{R}` → `{2}`
example is the Fanatic row), each colour floors at 0 rather than going negative (Kragma at 2 and 3
Ragemongers is still `{3}`), the reduction STACKS per copy (Rageblood `{1}{R}{R}` → `{1}{R}` → `{1}`),
and the HYBRID case consumes exactly one `{R/W}` pip (Boros Reckoner 3 pips → 2). The `{R}` rendering
of a hybrid pip is deferral D11, not a payment error.

### 5g heuristic mining — nothing to encode, and that is the right answer

`mine_heuristics.sh` over 600 games / 2 seeds / 2,969 real decisions:

* **ORDER rules: none with enough support.** Expected, not under-sampling — the skill notes order
  rules stay sparse on lord/anthem decks, and that is exactly what this deck is.
* **INCLUSION:** Kragma Warcaller is the standout at **-0.63 (113 help / 0 hurt)** — but a strongly
  negative delta means "it already gets cast", so per the skill's encoding table there is no code to
  write. Ragemonger (-0.12), Fanatic (-0.10) and Gnarled Scarhide (-0.08) are the same shape.
  Sethron is **+0.56 with help=0** — consistently slower early, which is simply a 5-drop being
  outclassed by the curve in a deck that wins on turn 5; the search already deprioritises it and the
  table says gate only on a CONFIRMED misplay, which there is none of. Neheb (+0.15) is the
  interesting one and is explicitly **not** gateable: its anthem wants an EMPTY hand, so casting it
  early is bad and casting it late is good — textbook situational, leave it to the search.
* **LAND/FETCH:** Mountain dominates both land plays (5933) and fetch targets (828 vs 238 Blood
  Crypt). A Mountain-first rule is available, but it is a NARROWING and would need the 5e
  with/without A/B plus user review; it is not taken.

So the mining corroborates the routing fix from the other direction: **this deck genuinely wants no
provider heuristics**, which is what `GenericProvider` gives it.

### Two engine bugs found and fixed

**1. Viewer hang (commit `27bd0f17`).** The Stage-5d sweep found seed 9403 turn 5 emitting **64
identical consecutive prompts** with the board frozen — the game could never advance. Burning-Fist's
pump costs `{1}{R}`; every untapped land was a tribal `colored_creature_only` land. The pool that
bounds the activation count books those as WILD, but `ProducesForPayment` strips their colours for a
non-creature cost, so the activation applied as a total no-op and the segment re-prompt re-offered
the identical menu forever. For a CAST that stranding is long-accepted engine behaviour (the
autonomous search just wastes a branch — which is why no fingerprint moved and every autonomous run
was clean); it is only fatal in the viewer. Fixed by probing the REAL payment on a scratch state at
emission time, human-play only, so autonomous play stays byte-identical. Same shape as the recorded
Wirewood Lodge phantom-option loop.

**2. Provider misroute (commit `093aa5f2`) — the more serious one.** Slaughter-Priest's
`sac_creature_outlet` param ALONE set the Goblins signature, so **Minotaur was running under
`GoblinsProvider`** (verified: `mtg --batch` printed `provider=Goblins`). `DeferSacOutletPreCombat`
then deferred Slaughter-Priest's outlet to the SECOND MAIN — which this deck does not have. So the
outlet was not delayed, it was **silently deleted from the search**: another deck's heuristic
removing a real decision branch, exactly what the core invariant forbids. Doubly wrong, because the
outlet's live effect is a COMBAT pump. This is the third instance of this misroute class (after
Mirrorwing/Goblin Instigator and StompySurprise/Hornet Queen). Fixed by detecting a Minotaur
signature and routing to `GenericProvider` above the goblin check. Measured: searched quality
EXACTLY neutral (slower=0, faster=0 over 1100 games / 2 seeds; all four averages byte-equal), d0
greedy churn only. GT rebaselined in both tiers.

### Not my defect, but you will hit it — the dragonstorm overnight tier

A full overnight run reports **7 dragonstorm failures**. These are **pre-existing and already
documented** by the previous session in `greedy-in-the-searched-window-status.md` ("READ THIS FIRST
IF YOU ARE ANOTHER AGENT"): commit `8dc20bdc` removed dragonstorm's tie-break opt-out with USER
approval and rebaselined smoke + regression but **not** overnight. The failing cell is exactly the
`dragonstorm d3 s5005` game 227 that note predicts. Nothing to do with Minotaur; the note leaves
rebaselining as an open choice, so it is **left for the user to decide**, not accepted here.

## Stage 6a — encoded heuristics & assumptions disclosure

**1. Global engine assumptions in force.** Single passive opponent that never blocks, casts, or
gains/prevents life (this is what makes D1–D10 inert). Clairvoyant search over a known library with
a deterministic shuffle. **First main only** — `DeckUsesSecondMain` does NOT fire for this deck (no
spectacle, no combat-generated resources), which is correct for it and is also what made the
misrouted sac-outlet deferral fatal. Results quoted at the suite's settings (d0; d3 budget 10; d5
budget 20, `--lookahead-bottoming`). The horizon-honest no-win leaf tie-break is ON (engine default)
and essentially never fires here.

**2. Card-modeling simplifications.** D1–D11 above, plus the three tribal lands' shared
"produces all five colours, chosen type not modelled" collapse — which is **exact** for this deck,
since every creature in it is a Minotaur — and D12, which is not exact and is flagged.

**3. Deck / archetype DecisionProvider heuristics.** **Minotaur routes to `GenericProvider` and
overrides NOTHING.** There are no deck-specific heuristics, no pruning, and no correctness
shortcuts: every decision resolves through the generic baseline within the global assumptions above.
(That is the *result* of the routing fix; before it, the deck was silently inheriting four
Goblins-tuned hooks.)

**4. Play-viewer auto-resolved decisions.** None. Every interactive choice the deck's cards create
is surfaced: cast/line choices and the bestow mode as `main_phase` plan variants, Karoo returns as
`bounce`, Aether Vial as `vial_charge`, shock/tribal land entry as `land_entry`, the sac-outlet
victim as `sacrifice`, and both non-cleanup discards (Burning-Fist's cost, Neheb's trigger) as
`discard`. **Viewer-ready, no auto-resolved decisions.**

## Open items for the user

1. ~~Approve or amend deferrals D1–D11, and schedule D12~~ — DONE 2026-08-28: D1–D11 approved
   ("only relevant with an opponent"), D12 fixed (see the deferrals section above).
2. **The Karoo mulligan rule** — the most valuable finding for play quality. Regression seed 1001
   gi=27 mulliganed to 5, kept two Rakdos Carnarium and no other land, and played **zero lands in
   eight turns**. A Karoo with no other land must bounce itself, so a hand whose only lands are
   Karoos is a 0-land hand. The keep rule counts them as lands. This belongs in the deferred,
   user-initiated mulligan stage — see `minotaur-discard-policy-proposal.md` for the rule text.
3. **The cleanup-discard bucket policy** — authored and reported, not implemented. My honest read is
   that its measurable upside here is ~zero (10 sheds in 400 games, status quo already 90% optimal);
   approve it for doctrine correctness or skip it.
4. **`UseLethalShortCircuit`** — routing to Generic dropped it. It is win-turn-invariant (every
   searched average was unchanged), so this is a PERF question only; Stompy preserved it deliberately
   in the same situation. Needs a quiet box to measure; not guessed at.
5. **`min_playable: 0`** in the generated profile. The skill suggests ≥1 for multi-colour decks, but
   the analyzer produced 0 and that matches 11 of the 13 other decks (including 5-colour FiveColour).
   Left as generated; it is mulligan-stage territory.
6. **Mulligan profile generation has NOT been run** — correctly, per the pipeline-ordering policy it
   is the last stage and the USER kicks it off.

## Verification log

- Stage 1 coverage; 12 missing; Scryfall fetch complete (with the Slaughter-Priest recall catch).
- All 12 implemented; build clean; coverage clean; cost audit clean.
- Smoke 39/39 and regression 65/65 byte-identical — the shared-code changes (ComputeLordBonus
  signature, the OnDragonEnters early-out, CanonicalSacVictim widening, EffectiveSpellCost,
  ApplyAttackSelfPumps, AttackWith, ResolveCombatDamage, PerformFetch) are provably inert for all
  13 existing decks.
- 200-game observational sweep: Sethron tokens, both ActivatePump shapes, hybrid payment and the
  cost reducer all fire in real games.
- Stage 5a/5b/5c2/5d/5h/5i complete; two engine bugs found, fixed, and re-verified.
- Suite integration + GT accepted (smoke, regression); overnight re-run under the fixed binary.
