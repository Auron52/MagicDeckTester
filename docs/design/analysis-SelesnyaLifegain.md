# Analysis ledger — SelesnyaLifegain

Per-deck durable state for the `analyze-deck` workflow (survives compaction and handoff).
Deck: `decks/SelesnyaLifegain/SelesnyaLifegain.cod` (60 cards, GW lifegain-into-fatties).

Started 2026-09-29.

## Decklist

| n | card | role |
|---|---|---|
| 5 | Forest | mana |
| 4 | Brushland | mana (painland, implemented) |
| 4 | Branchloft Pathway | mana (MDFC land, implemented) |
| 2 | Selesnya Sanctuary | mana (Karoo) |
| 2 | Blossoming Sands | mana (gain land) |
| 2 | Blighted Steppe | mana + lifegain sac |
| 1 | Wirewood Lodge | mana (untap Elf, implemented) |
| 4 | Llanowar Elves | ramp (implemented) |
| 3 | Elvish Mystic | ramp (implemented) |
| 4 | Priest of Titania | ramp, scaled (implemented) |
| 3 | Elvish Archdruid | ramp + Elf lord (implemented) |
| 3 | Accomplished Alchemist | ramp off lifegain |
| 2 | Wellwisher | lifegain engine (per Elf) |
| 4 | Ageless Entity | lifegain payoff (counters) |
| 4 | Nykthos Paragon | lifegain payoff |
| 4 | Verdant Sun's Avatar | lifegain engine (creature ETB) |
| 1 | Blossoming Bogbeast | lifegain → team pump |
| 3 | Feed the Clan | lifegain (instant, ferocious) |
| 4 | Genesis Wave | payoff (X permanents from library) |
| 1 | Craterhoof Behemoth | finisher (implemented) |

## Stage 1 — coverage (run 2026-09-29)

`missing` (11): Accomplished Alchemist, Feed the Clan, Ageless Entity, Blossoming Bogbeast,
Nykthos Paragon, Genesis Wave, Verdant Sun's Avatar, Selesnya Sanctuary, Blighted Steppe,
Blossoming Sands, Wellwisher.

No `partial` statuses. Sideboard empty (`reachable: false` — correct, no wish effect).
Pre-existing bracket notes on the 9 already-implemented cards were read and classified:
all are accepted Tier-4/provably-inert items previously reviewed on other decks
(Craterhoof trample — no blockers; Priest's both-players Elf count; Branchloft in-hand
front-colour; Wirewood Lodge dual model; Brushland both modes). None re-opened.

## Stage 2 — implementation

Research fanned out one agent per card (drafts only, no writes); integration serial.

All 11 implemented. Research fanned out one Opus agent per card (drafts only, no writes); integration
was strictly serial — four integrator agents in sequence, because `cards.json`, `CardParams`,
`SpellEffects.h` and `TurnSolver.cpp` cannot take concurrent edits.

| card | tier | new params / state | notes |
|---|---|---|---|
| Verdant Sun's Avatar | 1 | *(none)* | `own_creature_enters_lifegain_toughness` + `creature_enters_includes_self` already existed |
| Selesnya Sanctuary | 1 | *(none)* | Karoo; `produces_amount 2` = one of EACH colour, not 2 wild |
| Blossoming Sands | 1 | *(none)* | `etb_lifegain` routes through the shared `GainLife`, so its 1 life is a real EVENT |
| Ageless Entity | 2 | `lifegain_self_counters_that_many` | the AMOUNT form of the fixed-N watcher |
| Feed the Clan | 2 | `cast_lifegain_ferocious`, `ferocious_min_power` | ferocious read at RESOLUTION on full effective power |
| Nykthos Paragon | 2 | `lifegain_each_own_creature_counters_that_many`, `lifegain_counters_once_each_turn`, `Permanent::lifegain_counters_used_this_turn` | once-per-turn is PER PERMANENT; new `lifegain_counters` viewer decision type (Bucket B) |
| Blossoming Bogbeast | 2 | `attack_trigger_lifegain`, `attack_team_pump_per_life_gained` | **no boast** — it is Commander 2021, a mandatory attack trigger |
| Accomplished Alchemist | 3 | `mana_per_life_gained` | first scaled mana yield that is not a board count |
| Blighted Steppe | 3 | `sac_lifegain_per_creature_cost`, `sac_lifegain_per_creature`, `PermAbilityMode::SacLifePerCreature` | sacrificed as a COST, so it is in the graveyard before resolution |
| Wellwisher | 3 | `tap_lifegain_per_subtype`, `tap_lifegain_count_all`, `tap_lifegain_cost`, `PermAbilityMode::TapLifegain` | new `taplife=` viewer verb (`cast=Wellwisher` was ambiguous with a hand cast) |
| Genesis Wave | 3 | `reveal_x_put_permanents` | two-phase SIMULTANEOUS put; `EnterLand(resolve_full_etb)` |

### Four engine bugs found and fixed along the way

The last two were found by the Stage 5d claude-play sweep and are the same defect class as each other
— a searched plan axis that the `--claude-play` decision menu did not disclose. `SummarizePlan`'s own
`rad_mode` comment states the standard both violated: two menu entries that read exactly the same but
play differently are "the one thing a decision menu must never do."

3. **MDFC face was enumerated but unlabelled.** Branchloft Pathway's two faces appeared as
   byte-identical plan entries (same `summary`, same `land`), so picking the wrong twin silently
   committed the manabase to the wrong COLOUR. Load-bearing here: 4 of 17 lands are Pathways, the
   commitment is irreversible, and the only white costs in the list are Nykthos Paragon's `{4}{W}{W}`
   and Blighted Steppe's `{3}{W}`. **Seven independent sweep agents reported it.** Fixed by annotating
   the back face (`land=Branchloft Pathway -> Boulderloft Pathway (back face)`); only the back is
   annotated, since in every zone but the battlefield a DFC *is* its front face (CR 712.2), so
   front-face plans and every non-MDFC deck are byte-identical. The browser viewer was never affected
   — it routes `land_face` through `CheckLine` as an explicit `face` sub-choice.
4. **`Action::Kind::UntapCreature` had no case in the summary switch**, so Wirewood Lodge's
   `{G},{T}: Untap target Elf` rendered as `Wirewood Lodge (other)` — which reads as *casting* a land
   and says nothing about untapping. It had been left behind when the two sibling land activations
   (`AnimateLand`, `TapForTokenPay`) were given cases for exactly this reason. Now reads
   `Wirewood Lodge: untap an Elf`. The target is deliberately not named: it is an auto-target with a
   human chooser listed as DEFERRED, so naming it would overstate the decision.

### The first two engine bugs

1. **MDFC back-face lock-in on a Karoo bounce.** `BounceKarooLand` returned the synthesized
   *Boulderloft Pathway* to hand — a face, not a card — permanently losing the {G} option
   (CR 712.2). New `CardParams::mdfc_front_name`, set only by the back-face synthesis, restores the
   front face before `EnterHand`. Unreachable for every shipped deck (none of the 7 existing Karoo
   decks runs an MDFC), so byte-identical; this deck is the first to pair them (4 Pathways, 2
   Sanctuaries).
2. **`CheckLine` stage-0 pass test.** A line consisting only of a newly added verb graded
   `accept / plan -1 / "pass / cast nothing"` — an accept for a line the engine then does not play.
   Found live while wiring `taplife=`. **Any future verb must be added to that conjunction.**

### Cross-cutting engine work

* `life_gained_this_turn` was folded into `BuildSimKey` only when the HAND held a
  `pump_per_life_gained_power` card. This deck's readers are on the BATTLEFIELD (the Alchemist's
  mana, Bogbeast's pump), so two states with the same life total and different gains-this-turn
  collided on one memo key while having different available mana — 4 Brushland reach the same life
  either way. Gate widened via `GameState::deck_reads_lifegain_in_play`, folding the VALUE.
* `OptimisticTurnMana` declines to prune while a tappable Alchemist is out (its yield can grow
  after the bound was computed).
* Simultaneity: Genesis Wave puts permanents in ONE event, so pass 1 appends them all and pass 2
  fires the enter cascades. Verified against an independent Python model of the cascade — a
  sequential loop would have shipped a **10x undercount** of the deck's payoff.

## Provider (Stage 4a) — `SelesnyaLifegainProvider`, routed at the top

Signature OR-ed across **nine params from eight different cards**, each verified carried by no other
card in `cards.json`. Empty `DeckProvider` derivation (play-neutral by construction); pinned in
`test/unit/test_pirates_provider.cpp`.

It had to clear **two** existing signatures, not one:
* **`stompy`** — Craterhoof's `etb_team_pump_per_creature` and Elvish Archdruid's creature-side
  `mana_per_creature_subtype` EACH set it alone. The deck was **measurably** riding
  `StompyProvider` (confirmed by `provider_audit.py`) from the moment its first card landed,
  inheriting another deck's cast order and cleanup-discard buckets.
* **`critter`** — avoided at the source too: Ageless Entity and Nykthos Paragon use explicit
  `*_that_many` params rather than the legacy ints, so the critter signature is never set.

## Second main — measured, and flipped OFF

`MTG_SL_SECOND_MAIN` was adopted default-ON on a reachability argument (the Alchemist's mana from
combat lifegain is only spendable post-combat). Re-measured with Bogbeast in — one pooled batch,
2 arms x 120 paired games, seed 990001, d3:

| arm | avg win turn | core-ms | units |
|---|---|---|---|
| m2 ON | 5.3833 | 645,936 | 48.7 M |
| m2 OFF | 5.3833 | 300,834 | 31.0 M |

Identical win turns in **120 of 120** games — a zero paired difference — for 2.15x the CPU. Play did
change (10/120 digests differ); it never changed the turn the game was won. The honest reason is the
clock: at a 5.4-turn average the deck races past its own five-drop, so Bogbeast lands in only 3 of 20
sampled games. **Default flipped to OFF** as a clean win (identical quality, less than half the
cost), lever kept to re-arm.

## Cost (Stage 5j) — the deck FITS, and the first measurement was a methodology error

Budget for a new deck is **3,100 ms/game** (3x the fivecolour reference at 1,033).

**Measured on an idle box, with the profile attached, at the suite's own depth/budget cells**
(single-threaded, so wall-ms per game IS core-ms per game):

| cell | ms/game |
|---|---|
| d0 | 1 |
| d3 b10 (gated) | **433** |
| d3 b20 (overnight) | 533 |
| d5 b20 (gated, worst searched REGRESSION case) | **814** |
| d5 b40 (overnight) | 988 |

So the gated metric is **814 ms/game vs a 3,100 budget — ~0.79x the reference deck.** The 3x rule is
not close to binding, and the deck is *cheaper per game than Pirates* (~0.7 / ~1.3 s at the same two
cells), which is why it took Pirates' counts.

**An earlier probe read ~12,270 ms/game and concluded the deck was ~4x OVER budget. That conclusion
was wrong, and the reason is worth keeping:** those runs omitted `--budget-ms`, so the search ran
effectively unbounded per decision, while every real suite case carries a per-decision virtual-ms
budget (10/20/40 — the fifth field of a case row). The per-decision budget is what bounds this deck.
**Never size or gate this deck from an unbudgeted run.**

Two real observations from that probe survive and are worth keeping, because they describe the
deck's *shape* even though they mis-priced its suite cost:
* **The cost is a heavy tail, not a mean.** Unbudgeted at d5 one game ran 16.2 minutes — 47.7% of its
  whole 40-game arm. A per-decision budget is exactly what truncates that tail, which is why the
  budgeted numbers are two orders of magnitude tamer.
* **Part of the expense is baseline, not Genesis Wave.** Several of the slowest unbudgeted games had
  byte-identical `units` with the Waves swapped out, i.e. the lifegain-watcher board walk itself is
  costly on a wide board. And Genesis Wave's play is **depth-sensitive** (digests differ d3 vs d5)
  where the no-Wave arm's are identical — so a future search-shape change will move this cost.

## Provider routing (Stage 4a) — decided before anything is measured

This deck **must not** ride `CritterLifegainProvider`. Its signature
(`DecisionProviders.cpp:11429`) is OR-ed across `lifegain_self_counters`,
`lifegain_each_own_creature_counters` and `lifegain_target_own_counter` — and a naive Ageless
Entity / Nykthos Paragon modelling would set the first two, landing this deck on another deck's
`CastOrderRank` and `LegendKeepIndex`. That is the archetype-neutral-param misroute class
(Mirrorwing, StompySurprise, Minotaur, Dragons, Melira Pod, Pirates, Knights).

Two independent mitigations, both applied:
1. The new payoff params are **separate** from Critter's (`*_that_many` forms), so they do not set
   its signature at all.
2. A `SelesnyaLifegainProvider` is added and routed **above** `critter`, exactly as `angels` is
   (`DecisionProviders.cpp:11797`), with a signature OR-ed across several of this deck's own new
   params so no single deckbuilding swap loses the routing.

Per the Angels precedent the provider is an **empty `DeckProvider` derivation** (play-neutral by
construction — every hook inherited byte-for-byte), verified by smoke `play-changed=0`. Deriving
from `CritterLifegainProvider` was considered and rejected: the skill forbids deriving from a
thematically-related deck's provider because it imports unmeasured narrowing.

## Stage 5j — suite membership: PASS, added to all three tiers

`suite_gate.py --cost decks/SelesnyaLifegain` (idle box, real filtered tier run):

```
selesnya / fivecolour = 1843.640 / 1033.460 = 1.78x   (limit 3x)
VERDICT: PASS -- selesnya may be added to the suite.
worst searched case: selesnya_regression_d5_s3003    total 554.5 core-s
```

**The gate's 1,844 ms/game supersedes my own single-threaded probe estimate of 814** for the same
cell. Two reasons the probe read low and both are worth remembering: the gate runs the *expensive*
seed (s3003) over 75 games where the probe used 40 on s2002 — and this deck has a heavy tail — and
the gate counts core-ms under parallelism. Probe to decide whether a measurement is worth making;
quote the gate.

Its **total** (554.5 core-s) makes it the most expensive deck in the regression tier by total time
(snow is 415.5), which is a consequence of the counts chosen, not of intrinsic cost — per-game it
sits mid-pack. Sized at Pirates' counts deliberately.

Rows added to `SMOKE_CASES`, `REGRESSION_CASES` and `OVERNIGHT_CASES` + `DECK_FILE`/`DECK_PROF`.
No `<deck>2hg` row, and the reason is a card-level fact rather than a judgement: nothing in the 60
reads opponent heads or starting life — every gain is "you gain", every pump is "creatures you
control" — so a 2HG arm differs only in race LENGTH, not in any card's behaviour.

### Ground truth accepted

| tier | keys | fingerprints |
|---|---|---|
| regression | 5 | d0 6.2300 · d3 5.1733/5.4267 · d5 5.0667/5.4400 |
| smoke | 3 | d0 6.2770 · d3 5.4800 · d5 5.3733 |
| overnight | 12 | (see below) |

Each accepted with the SAME `--deck=selesnya` filter. Verified after each: total key count rose by
exactly the number of new keys (594 → 599 → 602), **both pre-existing
`accepted-with-regressions` provenance notes survived**, and `check_gt_logs.py` reports
`STALE: 0  missing: 0`.

### The play-neutrality proof

The smoke run's per-game audit is the load-bearing evidence that none of this work touched another
deck: **`configs changed: 0   unchanged: 101`, `[searched] play-changed=0`, `[d0] play-changed=0`.**
That covers, in one check, the new `SelesnyaLifegainProvider` routing, the `FireLifegainWatchers`
widening, the `EnterLand(resolve_full_etb)` parameter, the MDFC front-face restore, the `BuildSimKey`
`life_gained_this_turn` gate widening, and the `MTG_SL_SECOND_MAIN` default flip — every one of which
is param- or deck-gated, as claimed, rather than merely asserted to be.

## Approved deferrals

Nothing is bracket-noted as a Tier-4 deferral: every oracle clause of all 11 cards is implemented.
The disclosures below are `[PARTIAL:]` items — under-models in the *conservative* direction, or
heuristics deliberately left unmeasured — not unimplemented clauses. They are listed for sign-off
because the skill requires every deferral to be user-approved, and **they are PROVISIONAL until then.**

1. **Wirewood Lodge → Wellwisher second activation is unreachable.** "Tap Wellwisher for life, then
   {G}+tap the Lodge to untap it, tap again" is a real line the engine cannot find:
   `ApplyUntapCreature` auto-targets the highest-mana-yield tapped Elf and Wellwisher's yield is 0,
   so any mana dork outranks it. Deferred because the fix changes a shipped, GT-bound heuristic that
   StompySurprise also runs and touches four committed scenario fixtures — a rebaseline, not a card
   implementation.
2. **Neither Wellwisher's nor Blighted Steppe's life can feed Accomplished Alchemist's mana in the
   same turn.** Both are *trailing* activations applied after every cast's payment, and
   `life_gained_this_turn` resets each untap — so that mana is permanently unreachable, not merely
   mis-sequenced. A deck-level ordering question (`Plan::vial_after_casts` is the searched-ordering
   precedent), not a card defect.
3. **Trigger-order fidelity on simultaneous ETBs.** `FireEtbWatchers` runs before
   `FireOwnEtbTriggers`, so a Craterhoof entering with Avatars out gains each Avatar its *unpumped*
   toughness 5. In paper the controller orders these (CR 603.3b) and would take 5+X. Engine-wide
   ordering doctrine; the error is conservative (under-gains).
4. **Genesis Wave's pass-2 trigger order is reveal order.** Deterministic and disclosed. It is a real
   decision because the Paragon's once-per-turn look should land on the biggest single gain.
5. **Nykthos Paragon's "you may" uses a greedy default** (spend every unused copy). Its one
   systematic loss: a 1-life gain-land resolving first can eat all four uses on a turn that later
   gains 10. The provider hook is wired, so the fix is a `SelesnyaLifegainProvider` override with
   zero engine churn.
6. **Mana from a land Genesis Wave puts untapped is credited by neither world's plan accounting.**
   Symmetric, so no `fd-diverge`; a missed line, in the safe direction.
7. **Unmeasured ordering priors deliberately NOT shipped**, to be proposed and swept per
   `heuristic-optimization.md` rather than asserted: the Alchemist's `ManaSourceRank` (tap the
   life-scaled dork last, since its yield can only grow within the turn), a `CastOrderRank` putting
   the lifegain ENGINE before the PAYOFFS, and `EvalCard` credit for Feed the Clan / the Paragon.

## Separate pre-existing bugs found, NOT fixed here

1. **Crop Rotation gifts a free Karoo.** `PerformLandTutorToBattlefield` calls `EnterLand` without
   the new `resolve_full_etb`, so a Crop-Rotated Azorius Chancery enters **without bouncing**.
   `decks/Creature Giving` runs 4 Crop Rotation + 1 Azorius Chancery, so this over-models that deck's
   mana. The fix is now one argument (`, true`) but it moves that deck's GT — its own item.
2. **`Basri, Tomorrow's Champion` fails `audit_card_fields.py`** (`keywords local=[] scryfall=['exert']`).
   Pre-existing at HEAD from the WhiteKnights work; needs either the keyword or an allowlisted
   divergence with a reason.
3. **`colored_cast_lifegain_used_this_turn` is folded nowhere in `BuildSimKey`.** Benign for Ancient
   Cornucopia (1–2 goldfish-inert life); left alone. The analogous new flag
   (`lifegain_counters_used_this_turn`) IS folded, because for the Paragon it is not benign.

## RESUME HERE (state as of 2026-09-30, nothing committed)

### Done
Stages 1–4 complete, 4a (provider) complete, 4-bis / 5j complete (all three tiers + accepted GT +
cost gate PASS at 1.78x). `verify_deck.py decks/SelesnyaLifegain/SelesnyaLifegain.cod --no-network`
reports: coverage PASS (20/20 full), regression_tiers PASS, viewer PASS, viewer_wiring PASS
(bounce, dragon, lifegain_counters), **mismatch PASS — zero nonconv and zero fd-diverge over seeds
7001+7002 x 60 games**, play_invariants PASS (8 games / 224 decisions), suite PASS.

**Invoke verify_deck with the DECKLIST FILE, not the folder.** Passing `decks/SelesnyaLifegain`
makes coverage, viewer, viewer_wiring, mismatch and play_invariants all fail spuriously
(`IsADirectoryError`, `No cards parsed from:`) and reports `regression_tiers: not in the suite`.

### Still to do
1. ~~**`card_costs` is a DID-NOT-RUN, not a pass.**~~ — **PASS 2026-09-30.** Re-run with
   `python3 scripts/audit_card_costs.py --throttle 0.5` (the throttle is what the first attempt was
   missing; unthrottled it 429s and leaves ~95 costs uncompared, which is `rc=2`, neither pass nor
   fail). Result: **357 costed cards, 356 compared, all match Scryfall**; the one holdout,
   `Pyrohemia`, timed out and was then verified by hand — local `{2}{R}{R}` = Scryfall `{2}{R}{R}`.
   So 357/357. Still true and worth keeping: never record a rate-limited run as a pass.
2. **`card_fields` FAIL is pre-existing and not this deck's**: `Basri, Tomorrow's Champion`
   `keywords local=[] scryfall=['exert']`, red at HEAD from the WhiteKnights work. Needs either the
   keyword or an allowlisted divergence with a reason. All 11 new cards are clean on every hard field.
3. ~~**`claude_sweep` gate needs its record.**~~ — **RECORDED 2026-09-30**, see the `## Claude-play
   sweep` section below: `flags: 1 unresolved`. The gate therefore reports a **FAIL**, honestly: the
   one survivor is the false `drops` label (seed 78137), which is human-facing and GT-neutral but
   genuinely un-root-caused. Do not zero that line to make the gate green.
4. ~~**5b multi-depth sanity sweep not yet run.**~~ — **DONE 2026-09-30, and it is clean.** 300
   identical games (seed 55055) at each depth, `MTG_DUMP_WINS=1`, d0 at b0 / d3+d5 at b20:

   | depth | avg win turn |
   |---|---|
   | d0 | 6.2267 |
   | d3 | 5.2867 |
   | d5 | 5.2833 |

   Per-game monotonicity is **perfect — not one game where deeper played worse**: d0→d3 is 207
   better / **0 worse** (net −1542 turns over 300 games), d3→d5 is 1 better / 0 worse, d0→d5 is 208
   better / 0 worse. Plausibility also holds: ramp on T1–T2 into a T4–T5 payoff is this deck's real
   clock, and 5.29 at searched depth sits exactly there — no starvation, so 5c needs no probe.
   The suite's own rows still cannot serve as this check (its d3 cases use 150 games and its d5 cases
   75, so cross-depth fingerprints are different SAMPLES, not a depth comparison) — that is why this
   was run on one shared game set.
5. ~~5c2 horizon-honest tie-break check~~ — **DONE, KEEP THE DEFAULT (ON).** 24,000 games
   (12,000 paired) at the deck's play settings, both arms in one pooled batch:

   | split | games | net turns | worse | better |
   |---|---|---|---|---|
   | half A | 6000 | −8 | 3 | 11 |
   | half B | 6000 | −6 | 2 | 8 |
   | **ALL** | **12000** | **−14** | **5** | **19** |

   Binding rate 24 of 12,000 paired (0.200%) — so this is a real signal, not the "NO SIGN AT THIS
   SAMPLE" non-result, and it replicates across both halves in sign and direction. The tie-break
   lowers this deck's average, so no `GradesNoWinLeaf` opt-out. Nothing to do.
6. Stage 5e/5f/5g heuristic work is all still open — see the unmeasured priors in Approved deferrals.
7. ~~Root-cause the dropped-trailing-activation bug~~ — **DONE 2026-09-30**, and it turned out to be
   an engine-wide defect with its own doc and its own instrument:
   **[`trailing-activation-payment-hole.md`](trailing-activation-payment-hole.md)**. Fix =
   `MTG_ACT_LINE_HOLD`, default OFF; audit = `MTG_ACT_DROP_AUDIT`.
   **UPDATED 2026-09-30: the "one open decision for the user" framing there was withdrawn.** The
   provider-gate recommendation rested on a searched-depth cost that decoded to 7 discordant games in
   1,000, the lever itself had two defects (both now fixed), and the residual d0 cost is a *separate*
   pre-existing defect — the greedy projection over-values a trailing activation. Read that doc's
   corrected verdict; this is engineering work, not a sign-off.

### The Stage 5d sweep, and a methodology error worth not repeating
The FIRST sweep round fanned 18 agents over `--game-index 0..17` at one seed. **`--game-index` does
not vary the game under `--claude-play`** (claude-play.md line 108 says so explicitly; the library and
opening hand come from the seed alone, and `game_index` only populates the passive opponent's board).
So all 18 agents played ONE game — ~1 game of coverage, not 18, and the deck's whole payoff core
(Genesis Wave, Nykthos Paragon, Verdant Sun's Avatar, Feed the Clan, Bogbeast, the Karoo) went
untested. The trap is documented *because* an earlier version of that section put `--game-index` in
the sweep recipe — and the recipe further down the same file still shows it. **Sweep over SEEDS.**

That round was not wasted: 18 independent readings of one game verified the deck's load-bearing
arithmetic very thoroughly (every gain one event of the full amount per CR 119.10; Ageless Entity's
"that many" as 3/8/4 counters not loops of 1s; Blighted Steppe one event of 2xN counting own
creatures only, sacrificed as a cost, unable to fund its own `{3}{W}`; Wellwisher counting itself and
a same-turn Alchemist; Craterhoof's X counted after it enters), and it found both labelling bugs. But
its coverage claim is one game. Round two re-ran over 12 distinct seeds (77101…78241) on the fixed
binary; its results were still arriving at handoff.

## BLOCKING BUG found by the Stage 5d sweep — ROOT-CAUSED AND FIXED behind a lever (seed 77617)

> **STATUS 2026-09-30: fixed, measured on all three tiers, and DEFAULT OFF pending the user's call.**
> The fix is `MTG_ACT_LINE_HOLD` and it is **engine-wide, not this deck's** — it lands the repro
> exactly (life 25 -> 31, Steppe sacrificed) and cuts this deck's measured drop count 82%. The full
> write-up, the `MTG_ACT_DROP_AUDIT` instrument, the cross-deck exposure (Snow 29%, EDF **43.5%**,
> this deck 4.9%) and the three-tier measurement live in
> **[`trailing-activation-payment-hole.md`](trailing-activation-payment-hole.md)** — read that, not
> this section, for the current state. The diagnosis below is kept verbatim because it is correct and
> it is what located the mechanism; only its "not yet fixed" framing is superseded.
>
> The headline the sweep could not have known: at **searched depths this deck is NEUTRAL** with the
> fix. With the two defects in the lever repaired and a properly-seeded 11,000-game paired run across
> d1/d2/d3/d5, it is better than neutral: **111 better / 78 worse, net −33 turns, sign p = 0.020.**
> Blighted Steppe's clause is not merely free to make available, it is worth making available.
>
> **CORRECTION 2026-09-30 to an earlier version of this note, which said "Snow pays +0.0071 and that
> cost is real, not budget churn, so it is a trade" and recommended a provider gate.** That was wrong
> three ways: Snow's searched-depth figure decodes to **7 discordant games in 1,000** at the metric's
> quantum (5 went the other way, sign p = 0.14); the "not budget churn" sweep was 1 game → 2 games of
> 75; and the lever had **two defects of its own** (`ActLineHoldMask` subtracted the cast's own pips
> from the activation's need, holding the `{T}` but not the `{G}`; and `MTG_ACT_DROP_AUDIT` was blind
> to `Action::Kind::UntapCreature`, which is why this deck's drop rate read 4.9% when it is **13.8%**).
> Both are fixed. What survives is a genuine d0-only regression that is **not** this lever's fault —
> the greedy projection over-values a trailing activation, and the payment hole had been deleting the
> plans that exposed it. See that doc's corrected verdict.

**Blighted Steppe's `{T}` activation is SILENTLY DROPPED whenever the same plan also contains a cast
with a GENERIC pip.** The mana payer taps the Steppe for its `{C}` toward that cast, which nullifies
the `{T}` half of the activation's own cost; the activation then no-ops with **no `drops` disclosure,
no error, and no life gained**. An enumeration-vs-execution divergence, not a mana shortage.

Blocking: per the skill a 5d legality/invariant flag is root-caused, never averaged away. What it
costs is the deck's whole engine — a lost life-gain EVENT is lost Ageless Entity counters and a lost
Nykthos Paragon once-per-turn wave.

**It contradicts its own card note, twice.** The Blighted Steppe entry asserts (a) "the `{T}` half of
the cost is paid BEFORE the mana half via SetPermTapped -- this land can never tap for mana toward its
own activation; that ordering is the Botanical Conservatory fix, CR 602.2a", and (b) as the stated
reason for leaving it at the generic rank-5 tier, "a plan carrying the activation PRE-TAPS THE SOURCE
BEFORE PAYING ANY CAST, SO THE PAYER CANNOT STEAL ITS `{C}`". The payer demonstrably does steal it, so
the `ManaSourceRank` justification fails too, and the note must be corrected with the code.

### Minimal repro (turn 4)
```
./build/Release/mtg decks/SelesnyaLifegain/SelesnyaLifegain.cod \
  --profile decks/SelesnyaLifegain/SelesnyaLifegain.profile.json \
  --claude-play --seed 77617 --max-turns 8 --reveal 6 \
  --choices "1,1,0,0,0,0,0,2,0,0,0,60"
```
Plan 60 = `land=Forest; cast: Feed the Clan, Llanowar Elves, Blighted Steppe: sacrifice it: ...`.
* **Expected:** a second event `Blighted Steppe: sacrificed -- gain 6 life (3 creatures)`, Steppe in
  the graveyard.
* **Actual:** only `Feed the Clan -- gain 5 life`. Steppe `tapped=true` and **still on the
  battlefield**, life 25 not 31, `floating_mana {W:1}`, Priest of Titania + Elvish Archdruid both
  UNTAPPED with 6 unused mana — `{3}{W}` was trivially payable. Lost to the `{T}`, not to mana.

### Controls that PASS, and the pattern
`...,84` and `...,68` both fire it. Plan 68 is the diagnostic: its only co-cast is Llanowar Elves,
whose sole pip is `{G}`, which a `{C}`-only Steppe cannot pay — so it is not stolen and the ability
survives. **Fails iff the plan holds a cast with a generic pip** (`{1}{G}`, `{3}{G}{G}`, `{5}{G}{G}`);
passes when every co-cast is all-coloured. Second repro at T5 (`...,9`); continuing that line
(`...,9,-1`, `...,9,-1,-1`) proves the activation **never fires later — lost, not deferred.** Ruled
out "pre-tapped then its own payment failed": four land mana were produced and three spent, leaving
1 W floating, which can only happen if the Steppe did contribute its `{C}`.

### CONFIRMED INDEPENDENTLY on a second seed (78241, turn 6), which also located the mechanism

Same defect, different seed and different co-cast, plus the three things the first report could not
settle:

* **Mechanism, exactly.** The plan's cast loop runs first and the payer taps the Steppe for `{C}`
  toward the cast's generic pips. The trailing `ActivatePermAbility` arm then fails
  `PermAbilitySourceLive` (`src/core/SpellEffects.h` ~17414, `return !p.tapped && p.CanTap();`) and
  the whole activation is **skipped with no log, no error and no else-branch**
  (`src/ai/AIEngine.cpp` ~5509-5517). The unrealisable plan is emitted in the first place because the
  enumeration guard (`src/ai/TurnSolver.cpp` ~20722) tests the source's tap state **pre-plan**.
* **This RECONCILES the zero-`fd-diverge` result — the rollout twin (`TurnSolver.cpp` ~33064) is
  identical, so executor and rollout ARE in lockstep.** The search does not over-predict; it simply
  can never reach the better line. So this is a reachability/quality defect, not a divergence, and
  follow-up 2 below is answered: nothing was missed by the mismatch harness.
* **A fix already exists and is proven on the repro: `MTG_ACT_TAP_RESERVE=1`** (default OFF,
  `src/ai/EngineFlags.h` ~889-915), which reserves an activation's source before paying casts. On the
  identical plan it takes life 47 -> 67 and Ageless Entity counters 27/12 -> 47/32, and fires
  `gain 20 life (10 creatures)`. That flag's own note argues it "can only ever change WHICH sources
  pay, never whether a cost is payable -- and therefore can never drop an action". It is held off
  only because it moves ground truth, so **turning it on is a rebaseline decision for the USER.**
* **Severity is worse than "one lost activation".** Because the rank-5 "`{C}`-only, spend FIRST" tier
  makes the payer *prefer* the Steppe, the shipped engine can essentially never activate it on a turn
  it also casts a spell — i.e. on almost every turn. A 2-of's entire payoff clause is unreachable in
  practice, on a deck with 4 Ageless Entity, 4 Nykthos Paragon and 3 Accomplished Alchemist reading
  the gain. In the 78241 repro the two plans (with and without the activation) were **byte-identical
  in outcome**, so the activation half contributed literally nothing.

### THIRD confirmation (seed 77513) generalises it — there are TWO manifestations, and the real
### root cause is that trailing activations sit OUTSIDE the joint payment solve

* **A — the source is tapped for mana** (the `{C}` is stolen). `PermAbilitySourceLive` then fails and
  the branch no-ops. The source ends **TAPPED**.
* **B — a COLOURED pip is stranded, and no note covers this one at all.** On seed 77513 T5 the payer
  spent all three white-capable sources (Blossoming Sands, Brushland, and the Selesnya Sanctuary whose
  `{G}{W}` bundle put its W on a generic pip) to pay Genesis Wave, leaving no W for the Steppe's
  `{3}{W}`. `TapForCost` then failed and rolled back with no log. The source ends **UNTAPPED**.
  Ironically this is the Sanctuary note's own design intent turned against it — "every generic pip
  absorbs whichever half is not needed" — except the W half *was* needed, by a trailing activation the
  payer cannot see.
* **The final tap state is how you tell the two apart**: tapped = stolen for mana; untapped =
  `TapForCost` failed and rolled back.
* **ROOT CAUSE, and it is not Steppe-specific.** `BatchPrepayMainCasts` jointly solves the turn's
  combined MAIN-CAST cost (`docs/design/mana-source-reservation.md`), but **trailing activations are
  outside that solve.** Both repros were jointly payable — on 77513 T4, Feed the Clan off one Priest's
  2 G leaves Sands(W) + Brushland + the other Priest for `{3}{W}` — so the line is legal and the
  engine simply cannot find it. The natural fix is therefore to **fold trailing-activation costs into
  that joint solve** (or reserve the activation's source *and its coloured pips* before the casts
  pay), which is precisely the follow-up already scoped in that design doc and never extended from
  casts to activations. Prefer this over `MTG_ACT_TAP_RESERVE` if it is tractable: the reserve flag
  fixes manifestation A only, while the joint solve covers B as well.

### FOURTH confirmation (seed 77719) — it is not even limited to the source's own tap
The trailing-activation payer **will not tap Accomplished Alchemist** (a creature mana source) for a
trailing activation's coloured pip, and the failure is **monotone in the plan's AGGREGATE white
demand**: 1 white pip works; 2 pips (two Steppe sacs) fire **one of two**; 3 pips (Nykthos Paragon
`{W}{W}` + Steppe `{3}{W}`) drop the activation entirely — every time with 4 green floating and the
Alchemist left untapped. The CAST path does use the Alchemist for `{W}`, so the defect is localised to
the trailing-activation payment path. **And a dropped ACTIVATION emits no `drops` field**, though the
engine does emit `drops` for unaffordable casts — so it is invisible in the protocol. Cost in that
game: a 20-life event with two unused Paragons, i.e. +40/+40 on ten creatures instead of +14/+14.

### A SEPARATE, OPPOSITE bug: a FALSE `drops` label — NOW ROOT-CAUSED (2026-09-30)

> **The label is not a labelling bug. It faithfully reports what `ApplyPlanDirect` did — and
> `ApplyPlanDirect` is what is wrong.** It is an executor-vs-rollout PAYMENT asymmetry on a
> scaled-dork growth line, and the rollout errs in the conservative direction.
>
> **Minimal repro** (the seed-78137 report was the same shape; this is the frame that bisects cleanly):
> ```
> ./build/Release/mtg decks/SelesnyaLifegain/SelesnyaLifegain.cod \
>   --profile decks/SelesnyaLifegain/SelesnyaLifegain.profile.json \
>   --claude-play --seed 77513 --max-turns 9 --reveal 4 --choices "1,0,0,0,0,0,0"
> ```
> Board: Blossoming Sands (GW), Brushland (GWC), **Priest of Titania** (G). Hand holds a second
> Priest of Titania `{1}{G}` and Feed the Clan `{1}{G}`. Land drop still open.
>
> | plan | summary | `drops` | executor actually |
> |---|---|---|---|
> | 2 | `land=none; cast: Feed the Clan, Priest of Titania` | `["Priest of Titania"]` | drops it — **label TRUE** |
> | 3 | `land=none; cast: Priest of Titania, Feed the Clan` | `["Feed the Clan"]` | **makes BOTH — label FALSE** |
>
> Execute `...,3` and the board reads two Priests (one tapped), Feed the Clan in the graveyard, life
> 21 -> 26, and an empty float: **four mana realised from three sources.** That is the whole mechanism
> — `mana_per_creature_count_all` counts Elves *on the battlefield*, so paying Priest #2 off the two
> LANDS leaves Priest #1 up, and once the second Elf has entered Priest #1 taps for **2** G, which
> covers Feed the Clan. The ordering probe's apply realises only 3 and drops the second cast; the
> executor realises 4. Same plan, same board, same order — different payer.
>
> Plan 2's label is correct by the same arithmetic (Feed the Clan first takes both lands, the lone
> Priest then makes 1 where `{1}{G}` needs 2), which is why only *some* `land=none` variants are
> labelled. A rigorous sweep over four seeds (77203, 77513, 77101, 78137) found **2 false labels in 7
> labelled plans checked** — the other confirmed one is seed 77203 t3 plan 2 (`land=none; cast: Elvish
> Archdruid, Priest of Titania`), prefix `--choices "0,0,1,3,2,0,0,0,0,0,0"`.
>
> **THE METHOD IS THE REUSABLE PART, so it is written out rather than left in a scratch script.** To
> test a `drops` label, execute that exact plan index and **COUNT COPIES** of the named card across
> hand + battlefield + graveyard, before and after. The label is false iff hand lost one *and*
> battlefield+graveyard gained one. Checking mere membership ("is it on the battlefield after?") gave
> **3 false positives here**, because a Priest of Titania was already out — and a second copy of a
> card this deck runs four of is exactly the case a `drops` label is most likely to concern.
> Do not read the NEXT decision's board without pinning its `turn` either: executing a plan often
> yields another main phase on the same turn, but it can also carry you into a later one.
>
> **Ruled out, each with a one-command arm** (rerun the repro above with the env var set and re-read
> plan 3's `drops`): `MTG_PAY_BOUND=0`, `MTG_NO_BATCH_PAY=1`, `MTG_SCALER_PLAN_BIAS` both ways (it is
> default ON and does not help here), `MTG_DORK_TAP_LAST=1`. All four leave the label in place.
> `MTG_DBG_ORDER_PROBE=3` prints the per-ordering verdicts, which is how the two `land=none` variants
> were separated. The remaining difference between the two payers is the one that matters:
> the executor pays with an **accounting pool** (`available != nullptr`), the rollout through
> `TapForCostDirect` (`available == nullptr`, and `honor_legacy_cco` differs with it).
>
> **NOT a systematic rate divergence — checked, because it would be the scarier reading.**
> `MTG_AFFORD_AUDIT=1` over 40 autonomous games at d3 gives rollout 69,341/2,972,251 = 2.33% against
> real 23/999 = 2.30% on this deck, and 2.02% vs 2.11% on Snow. The two worlds drop casts at the same
> RATE; what diverges is this specific growth-dependent allocation. (The claude-play frame's
> `rollout: fails=3/17  real: fails=0/0` is not evidence either way — the denominators are one
> committed apply.)
>
> **Why it still matters, and it is more than a menu warning.** The rollout IS the search. If
> `TapForCostDirect` cannot realise a line whose payment depends on a scaler growing mid-turn, the
> search never scores that line — and this deck runs 4 Priest of Titania and 3 Elvish Archdruid, so
> "cast an Elf, then spend the grown scaler" is its central mana pattern. `fd-diverge` cannot catch it
> because it compares realised vs predicted WIN TURN and this error is conservative.
>
> Fixing it is a **rollout payment change and therefore GT-moving on every deck with a scaled dork**
> (this deck, StompySurprise, Goblins). It is scoped, not done.

### The original report (seed 78137)
Turn-3 plan 2 (`cast: Feed the Clan, Feed the Clan`, `land=none`) carries `drops:["Feed the Clan"]`,
claiming the ordering provably cannot make that cast — but executing that exact index **makes both
casts** (two 5-life events, both copies in the graveyard, 4 untapped green sources for a 4-mana cost).
`MTG_DBG_ORDER_PROBE=3` shows three identical orderings, two with `drops=[]` and only the `land=none`
variant labelled. Search-neutral (`would_drop` is carried, not acted on) so GT is unaffected, but its
live consumers are human-facing: the menu warning and `CheckLine`'s `drops_declared` disqualifier.
Harmless here only because all three matching variants differ by LAND and the "if EVERY matching
variant drops, keep them all" fallback applies — the same false positive on a frame with a clean
sibling would silently steer a human off a fully payable ordering, the exact inverse of the defect the
label was built to fix. Root cause not isolated; the ordering pass's scoring copy failed
`TapForCostDirect` where the executor paid off the same four sources.

### IMPORTANT REFINEMENT (seed 77101): two DIFFERENT cases wear the same symptom
Seed 77101's instance was genuinely **UNPAYABLE** — Nykthos Paragon `{4}{W}{W}` plus the Steppe's
`{3}{W}` need three white pips against two white sources, so no allocation works. There the no-op is
rules-correct (no part of the cost is paid, nothing is rewound, the board matches the
no-activation plan exactly) and the defect is only that the menu **advertises it without a `drops`
disclosure**. That agent also established `drops` structurally *cannot* cover it: `Plan::would_drop`
is populated only in the cast-ORDERING expansion (`TurnSolver.cpp` ~38455, gated on >=2 reorderable
`CastFromHand` actions), so an unaffordable ACTIVATION is never labelled.

**So triage every instance before fixing:** if the plan is genuinely unpayable, the fix is
*disclosure* (extend `drops` to activations, or colour-check the activation at enumeration). If the
plan IS jointly payable — which seeds 77617, 77719 and 77513 each demonstrated with explicit
allocations — the fix is the *joint solve*, because a legal line is being lost. Both fixes are wanted;
they address different halves.

### The sweep's one AI-MISPLAY CANDIDATE (seed 77101, Claude T5 vs search T6)
Bisected with `--choices-then-auto` to exactly one decision: handing the search the board at T3
pre-main gives turn 6, at T4 pre-main turn 6, at T4 **post**-main turn **5**. From the identical T3
board the search casts **Elvish Archdruid alone** on T4; the human plan casts **Llanowar Elves THEN
Elvish Archdruid** — enumerated, no `drops`, and payable only in that order (the Elf entering first
lifts Priest of Titania's tap from 1 G to 2 G). That free one-drop makes T5 need 8 of 12 mana instead
of 9, which is the whole turn. The search instead builds to a T6 Genesis Wave X=10 for 45 damage — a
bigger but slower line. Per the skill a win-turn delta is a weak signal, but this one is localised to a
single decision with a mechanism, so it is a genuine **5b/5e cast-order candidate**, not noise.

### Follow-ups this opens — all four RESOLVED or superseded 2026-09-30
1. ~~**Wirewood Lodge is the same shape** and is UNVERIFIED~~ — **CONFIRMED and covered.** The fix's
   `PlanTraits` builder takes `Action::Kind::UntapCreature` alongside `ActivatePermAbility` precisely
   because the Lodge always taps its source; and `stompy` (StompySurprise, which also runs the Lodge)
   moved its play digest in the regression tier at an **identical** average, which is the Lodge's
   exposure showing up on another deck. The prediction that the bug is engine-wide was right, and
   understated: **Snow 29.4% and EldraziDisplacerFlicker 43.5% of enumerated activations dropped**,
   against 4.9% here.
2. ~~Reconcile with the mismatch harness~~ — **answered**: executor and rollout are in lockstep, so
   the absence of `fd-diverge` is correct rather than a missed detection.
3. ~~Decide between the two fixes~~ — **BOTH were wrong, and measurement is why.**
   `MTG_ACT_TAP_RESERVE=1` does **not** fix seed 77617 (nor does `MTG_NO_BATCH_PAY=1`, nor
   `MTG_LINE_C_HOLD=1`, nor all three together — the matrix is a one-loop script written out in
   [`trailing-activation-payment-hole.md`](trailing-activation-payment-hole.md)): it is installed in
   `AIEngine::TakeTurn` *after* the `BatchPrepayMainCasts` call, so it cannot stop the prepay from
   stealing the source. And tightening the enumeration guard would have removed a legal line. The
   actual fix is a new mask wired into BOTH payment routes — `MTG_ACT_LINE_HOLD`.
4. **Still true and still owed**: GT for all three tiers was accepted BEFORE the fix. If the lever is
   adopted (globally or provider-gated), this deck's keys need re-running and re-accepting. With the
   lever OFF — today's default — the accepted GT is exact: the lever-off smoke arm is
   `configs changed: 0 / unchanged: 104, play-changed=0`.

## Claude-play sweep

commit: `4736ec40` (+ uncommitted onboarding tree)
seeds: 77101 77203 77307 77411 77513 77617 77719 77823 77927 78031 78137 78241
games: 12 (round 2, one per seed) + 18 replays of one game (round 1 — see the methodology note)
flags: 1 unresolved

**The one unresolved flag is the false `drops` label** (below) — **root-caused 2026-09-30, not fixed.**
It turned out not to be a labelling bug at all: the label faithfully reports what the rollout apply
did, and the rollout under-realises a legal payment that depends on a Priest of Titania GROWING when
the plan casts another Elf first. So the visible symptom is human-facing and GT-neutral
(`would_drop` is carried, not acted on), but the underlying defect is a real executor-vs-rollout
payment asymmetry that also costs the SEARCH this deck's central mana pattern. Repro, the four ruled-out
levers, and the reason `fd-diverge` cannot see it are all in that section. Fixing it is a rollout
payment change and therefore GT-moving on every scaled-dork deck, so it stays open rather than being
patched at the label.

The sweep's OTHER flag — the dropped trailing activation — is **resolved**: root-caused to an
engine-wide payment hole, fixed behind `MTG_ACT_LINE_HOLD`, verified on the repro and measured on all
three tiers ([`trailing-activation-payment-hole.md`](trailing-activation-payment-hole.md)). The
remaining item there is an **adoption decision for the user**, not an unresolved defect, so it is not
counted above. The single AI-misplay candidate (seed 77101) is a 5b/5e cast-order candidate, not a
legality or invariant flag, and is likewise not counted.

- commit: `4736ec40` (`HEAD:src` = `a6a21f55`) for round 1; round 2 ran on the binary with both
  labelling fixes in
- round 1: seed 77001, 18 agents — **but over `--game-index`, so this is ONE game, 18 times** (see
  the methodology note above). Retained for its verification value, not for coverage.
- round 2: seeds 77101, 77203, 77307, 77411, 77513, 77617, 77719, 77823, 77927, 78031, 78137, 78241
  — one game per seed. **3 of 12 reported at handoff; the other 9 were still running.**

**The seed fan-out demonstrably works:** the reported games differ in outcome (seed 77927 wins on
turn **4** via a Priest-of-Titania/Archdruid ramp into Craterhoof, where every round-1 game won on
turn 5 via Ageless Entity), which is exactly the variation `--game-index` failed to produce.

### Confirmed so far
* **Zero unresolved correctness flags.** No illegal plan, no missing legal play, no wrong state
  transition, across 19 reported games. Claude tied the search at turn 5 in every single one.
* Both flags the sweep raised were decision-menu **labelling** defects, both now fixed (see the
  engine-bugs section). The MDFC fix is verified in a live game by an independent agent, which
  reported the back face as distinctly labelled and "no two indistinguishable land plans anywhere".
* **`lifegain_counters` (Nykthos Paragon's Bucket-B "you may") is now EXERCISED** — seed 78031 cast
  the Paragon and the chooser fired with `amount:8, copies_unused:1` offering decline-vs-spend. The
  arithmetic is right in the way that matters: ONE gain event of 8 produced ONE "that many" trigger
  each, so Ageless Entity ended on **16** counters (8 from its own trigger + 8 from the Paragon's
  team wave) rather than 8 triggers of 1, and the Paragon correctly took counters from its own wave.
* Round 1's 18 readings independently confirmed every load-bearing arithmetic claim in the card
  notes: each gain is one event of the full amount (CR 119.10); Ageless Entity's counters merge into
  a single `+1/+1` entry; Blighted Steppe is one event of 2xN, counts own creatures only, is in the
  graveyard as a cost, and cannot fund its own `{3}{W}`; Wellwisher counts itself and a same-turn
  Accomplished Alchemist; Craterhoof's X is counted after it enters.

* Round 2 has additionally exercised, all clean: **Priest of Titania** (yield read LIVE — 3 the turn
  a fresh Llanowar Elves entered, summoning-sick bodies included in the count, which is correct),
  **Elvish Archdruid**'s `lord_excludes_self`, **Craterhoof Behemoth** (X counted after it enters,
  self-inclusive, and correctly getting NO Archdruid bonus because it is a Beast),
  **Blossoming Sands**' enters-tapped + 1 life, and floating mana carried across two frames of one
  pre-main with no extra source tapped.

* **THE PAYOFF CORE IS NOW VERIFIED END TO END (seed 77823, turn-4 win, 0 flags).** This one game
  exercised everything the earlier rounds could not, and every claim held:
  - **Genesis Wave at X=5 with a SIMULTANEOUS five-permanent put**, and the `dragon` put-chooser
    fired. The full legal X range was offered (0–13 alone, 0–7 alongside the Paragon — exactly
    `mana-3` and `mana-9`).
  - **Blossoming Sands entered VIA the Wave and its ETB life fired as a real gain event in pass 2** —
    i.e. the `EnterLand(resolve_full_etb)` + two-phase design is confirmed in live play, including
    the deliberate choice to resolve land ETB extras in pass 2 rather than pass 1.
  - **Ageless Entity ended on 15 counters = 1 (the Sands' ETB) + 7 (its own trigger) + 7 (the
    Paragon's wave)**, merged into one counter entry. That is the simultaneity-plus-compounding
    arithmetic the whole deck is built on, checked by hand against cards.json.
  - **The Paragon's "you may" behaved exactly as the Scryfall ruling requires**: the chooser fired
    TWICE, and **declining at `amount:1` did NOT consume the use**, which was then spent on a 7-life
    gain for +7/+7 across ten creatures. Its wave correctly reached tapped and summoning-sick bodies
    and itself.
  - Wellwisher counted 7 Elves at resolution including the two the Wave had just put in.

* **Seed 77307 (win T6, 0 flags) closed Verdant Sun's Avatar and the Karoo**, and gave the
  simultaneity its sharpest OBSERVABLE proof yet. A Genesis Wave at X=5 put four permanents at once,
  and **Wellwisher's entry gained 2, not 1 — because the Elvish Archdruid that entered in the SAME
  Wave was already on the battlefield when the trigger resolved** (CR 603.6d, reading live toughness
  including the lord bonus). A sequential put would have gained 1. Also verified there: the MV cap
  correctly BINNED a revealed MV-7 Avatar while putting the other four; a Branchloft Pathway put by
  the Wave entered FRONT face (CR 712.12); a Brushland put by the Wave entered untapped; the
  Sanctuary's bounce fired as mandatory (`allow_decline: false`) with itself among the legal options;
  and four gains stayed four separate events. This game also exercised the `bottom`, `bounce` and
  `dragon` decision types.

### Still unexercised by any reported game
Feed the Clan's ferocious gate, Blossoming Bogbeast's attack trigger, Blighted Steppe + Wirewood
Lodge together, and — the most interesting remaining gap — a board with **more than one** Nykthos
Paragon, so the "four copies = four independent once-per-turn uses" path, which is the deck's
ceiling and its whole reason for running four, is still untested by the sweep.

### One observation worth a follow-up (not filed as a flag; already disclosed in code)
A plan summary still LISTS a cast that the plan's own `drops` field says cannot be made in that cast
order (the Wave's payment eats the only two white sources, dropping the Paragon). Disclosed at
`main.cpp:1624`, but it means picking by summary alone silently loses a cast — the same
read-the-menu-wrong class as the two labelling bugs fixed above, and worth the same treatment.
Genesis Wave's X axis and colour gating are confirmed correct under human play
(`X = 0..mana-3`, no back-face variant offered for `{G}{G}{G}`), but no agent has yet cast it, so the
two-phase simultaneous multi-put and `GenesisWavePutPicks` remain unverified by the sweep. The
integrator's own 40-game probe did cover it (X up to 14, 20 life-gain events, validated against an
independent Python model of the cascade), so it is not unverified overall — just not by the sweep.
Also unexercised: Verdant Sun's Avatar, Feed the Clan's ferocious gate, Blossoming Bogbeast, and the
Selesnya Sanctuary Karoo bounce. **If round 2 does not reach them, a few targeted seeds are worth
more than more games.**

## Open questions for the user

(carried to the closing report; none blocked any work)
