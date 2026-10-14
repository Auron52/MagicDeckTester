# Dead reads of a cast candidate's `Action::def` inside `CollectActions`

Status: **site 3 fixed** by `87c40701` (`MTG_FREECAST_X_ZERO`, default ON); sites 1-2 fixed separately
by `60b82774` (`MTG_EQUIP_DS_GATE_CASTS`, branch `work/integ-2026-10-14`). Base commit `77e094a2`.

## The mechanism

`CollectActions` (src/ai/TurnSolver.cpp) builds the main-phase candidate list and, as its LAST step
before the main-phase filters, stamps every action's card definition:

```cpp
// Resolve each action's card definition ONCE ...
for (Action& a : actions) { a.def = ...LookupCached(ap.hand[a.hand_index]) / Lookup(a.card_name); }
```

No cast emitter sets `Action::def` (activations set it themselves; casts do not), so **for every
CastFromHand candidate `def == nullptr` everywhere above that pass**. Any block up there that reads
`a.def` (or a copy's `.def`) sees null, and the rule it guards silently never applies.

## Audit: every read of a candidate's `def` before the closing pass

Region: `CollectActions` from its first line to the closing pass (77e094a2: lines 22302-29859).
Method: (a) every `.def` / `->def` token in the region (`awk` over the line range); (b) every function
called in the region whose signature takes an `Action` or `std::vector<Action>`, checked for a `def`
read in its body (`logs/freecast-def/audit_calls.py`, a scratch script); (c) provider methods taking
an `Action` (`OrderedSubsetRealisable`, `CastOrderActionRank`, `ArrangeCastOrder`,
`CastOrderingCandidates`) -- none is called in the region; header-inline helpers -- none reads `def`.

| # | Site (77e094a2) | Reads | Rule it implements | Verdict |
|---|---|---|---|---|
| 1 | `MetalcraftWillFreeEquips(state, actions)`, the zero-power equip block (~25787) | each cast candidate's `def->params.metalcraft_equip_zero_artifacts` and artifact type | `MTG_ZERO_POWER_EQUIP_FREE_ONLY` (USER 2026-10-01, default OFF): a frame whose OWN casts will reach metalcraft frees the equips | **DEAD** (cast half). Fixed by `60b82774` via `CastCandidateDef` |
| 2 | `FrameHasDoubleStrikeSource(state, actions)`, the one-host rule (~26094) | each cast candidate's `def` (double strike / `double_strike_while_equipped`) | `MTG_EQUIP_ONE_HOST`'s premise ("There is no double strike ...") | **DEAD** (cast half). Fixed by `60b82774` (KittyEquipment v1 gi149 / gi738) |
| 3 | the Maelstrom Archangel free-cast bank (~29840): `f.card_mv > 0 && f.def && f.def->card.m_mana_cost.has_x` | a COPY (`Action f = actions[i]`) of a cast candidate's `def` | CR 107.3b: an {X} spell cast without paying its mana cost has X = 0 | **DEAD since written** (0d166914, 2026-08-07: the bank was inserted directly above the closing pass). Fixed here |
| -- | `HostStats::def` in the equip rider ranking (~25642) | a local struct filled by `LookupCached(card)` itself | -- | live (not an `Action::def`) |
| -- | `KembaLoopKind(state, probe, ...)` (~26391) | Equip actions only; never reads `def` | -- | n/a |
| -- | the activation emitters (`a.def = sd`, ~27472-29704) | WRITE their own def | -- | n/a |

The other callers of the two equip predicates (`MetalcraftHoistArmed` etc.) run after the pass and see a
resolved def. The pre-combat free-cast PROJECTION (`FreeCastCtx`, 2bd1762a) already resolves the def
itself (`a.def ? a.def : Lookup(a.card_name)`) and already prices a free {X} spell's damage at 0.

### Probe (temporary fprintf, never committed; `build/scratch/freecast-def/probe.patch`)

Built on 77e094a2 with three env-gated `fprintf`s, run over FiveColour d0 60 games + d3 b10 6 games and
KittyEquipment d0 20 games (seed 10800001):

| probe point | frames | cast candidates | `def != nullptr` |
|---|---|---|---|
| just before the closing pass | 50,334 | 171,434 | **0** |
| at the one-host DS gate | 50,334 | 168,680 | **0** |
| free-cast bank, per free variant built | -- | 2,754 | **0** (all `def=NULL`) |

So at all three sites the read is null in real play, every time. The bank is reached only by FiveColour
(Unite the Coalition 1,387 free variants, Faeburrow Elder 526, Bloom Tender 187, Progenitus 150, ...);
**no {X} spell reached it** -- FiveColour's list has none.

## Site 3: the free-cast bank's X = 0

**Rule.** CR 107.3b: if a spell with {X} in its mana cost is cast without paying its mana cost (and not
for an alternative cost that includes X), the only legal X is 0. A cost *reduction*, even to zero, does
not count (Hinata's discount leaves X the player's choice). The card's own oracle note in cards.json
(Maelstrom Archangel) says the same: *"X=0 for {X} spells per ruling"*.

**Defect.** With the check dead, every X > 0 candidate got a free twin that paid nothing and still
resolved at its X, in the rollout AND the executor (both thread `chosen_x` to the effect; the free cast
only skips payment). The viewer's free-cast chooser (AIEngine, human play) picks the first enumerated free
plan per card, so a human could be handed a free X > 0 cast too.

**Who can reach it.** The bank (`free_casts_available > 0`) is filled only by `combat_damage_free_cast`
(Maelstrom Archangel), held only by **FiveColour**, whose list has **no {X} spell**. No shipped deck holds
both (`logs/freecast-def/freecast_decks.py`: decks with an X spell are Angels, CritterLifegain, Hinata2,
KittyEquipment, Knights, Melira Pod, Prevent Damage, SelesnyaLifegain, Soldiers, WhiteKnights; none has an
Archangel). The other free-cast mechanisms are clean by construction: cascade, Creative Technique,
Breaching Dragonstorm's ETB and World War Hulk's saga cast through `apply_one(..., chosen_x = 0, ...)` in
the rollout and `PushFreeCast` (no `chosen_x`) in the executor. Aether Vial is a put, not a cast.

**Does it change play?** On every shipped deck, no (identity below). On a list that pairs the bank with an
{X} spell, yes. SCRATCH probe list `FiveColourX` (FiveColour with 2 Crackle with Power + 2 Rolling
Earthquake in place of 2 Cosmic Spider-Man, Garth One-Eye, Progenitus; under `build/scratch/`, never a
`decks/` folder), FiveColour's profile, seed 10800001:

* **d3 b10 gi42 (seed 10800043), OLD:** T4, 10 mana on board (4 lands, Birds, Bloom Tender at WUBRG). The
  Archangel connects (opp 15); main 2 casts Rolling Earthquake X=8 *and* Crackle X=2 -- 9 + 8 = 17 mana
  from 10, so the X=8 Earthquake was the FREE cast: an illegal free 8 to each player. **NEW:** Crackle X=2
  in main 1 (paid), Archangel connects (opp 5), Unite the Coalition free in main 2. Same T4, legal line.
* **gi95 OLD:** T5 main 2 casts Crackle X=2 twice plus Earthquake X=1 (one Crackle free at X=2). NEW:
  Crackle X=2 in main 1; same T5.
* **gi112:** the free-variant menu reaches the BOTTOMING trial games, so the London-mulligan bottom moved:
  OLD bottoms the Archangel (T5), NEW bottoms Scalding Tarn and wins T4 with a free second Archangel and a
  paid Earthquake X=8.

**Unit cover** `test/unit/test_freecast_x_zero.cpp` (6 Mountains, one banked charge, post-combat):

* Rolling Earthquake + Crackle in hand: fix arm -> the free Earthquake exists ONLY at X = 0, Crackle has
  no free twin; dead arm (=0) -> a free Earthquake at X > 0 and a free Crackle at X = 1 (77e094a2's menu);
  the binary default equals the fix arm.
* Unite the Coalition (a non-{X} card whose mode split S rides `chosen_x`): free variants identical in
  both arms, S > 0 present -- this is what keeps FiveColour byte-identical.
* Every cast candidate leaves `CollectActions` with `def == Lookup(card_name)` (the closing-pass refactor).

## The fix

* `CollectedActionDef(ap, a)` -- the closing pass's expression, as ONE helper; the closing pass now calls
  it (byte-identical), and so does the bank. A block above the pass that needs a definition must call it,
  never read `a.def`. (`60b82774`'s `CastCandidateDef` is the same expression behind its own flag; at
  integration it can delegate to this helper.)
* The bank, under `MTG_FREECAST_X_ZERO` (heurarm slot `FREECAST_X_ZERO`, appended at the end of the enum and
  `kNames`; `EnvOn(..., true)`): an {X} spell's free variants are exactly its **X = 0 candidates as
  enumerated**; an X > 0 candidate gets no free twin. The dead in-place line is deleted.
* Why not re-stamp the X > 0 twins as X = 0 (the literal reading of the dead line)? Every X-derived field
  the emitter stamped would still describe the old X (`direct_damage`, `eval`, `ritual_float`, a Chord /
  Zenith `tutor_target` above the X cap), and the re-stamped copies are effect-identical duplicates of one
  cast. Taking the enumerated X = 0 candidate is consistent in every field by construction and adds no
  branch. Whether an X = 0 cast is a line at all stays the emitter's / provider's call, exactly as for a
  paid cast: Rolling Earthquake and the tuck removal keep X = 0 (`XCandidates`), a Libation trick always
  offers it, Chord / Zenith offer it for a mana-value-0 target or as the fail-to-find; an X burn's emitter
  never emits X <= 0; Genesis Wave and Reality Spasm resolve to no-ops at X = 0 and are not offered.
* `=0` restores the dead behaviour and is byte-identical to 77e094a2 (measured below).

## Measurement

Binaries: `mtg.base` = 77e094a2, `mtg.fix` = this branch, both `./build.sh` Release. One pooled
`--batch` per binary (the new binary's batch also carries both probe-list arms), seed 10800001,
`--cards-json` pinned, no case at avg 9.

**Identity -- every suite deck** (`test/regression_cases.sh` DECK_FILE, 33 decks incl. fungusb / kittyv2 /
kittyv3) at d0 (40 games), d3 b10 (20), shipped (depth + budget omitted -> value_play; 10); FiveColour
at 400 / 100 / 40 plus FiveColour 2HG d3 b10 40: **100 jobs, 2,820 games, 0 differ** (per-game win turn
AND play digest).

**`=0` arm** (new binary, manifest `flags: {MTG_FREECAST_X_ZERO: false}`) on the probe list -- the one
place the code path is live -- vs the old binary: **1,200 games, 0 differ.**

**Default arm on the probe list vs the old binary** (not a shipped deck; a demonstration, not an adoption
A/B):

| job | games | moved | faster | slower | line only | net turns | avg old -> new |
|---|---|---|---|---|---|---|---|
| FiveColourX d0 | 1,000 | 1 | 0 | 0 | 1 (gi793) | 0 | 6.1600 -> 6.1600 |
| FiveColourX d3 b10 | 200 | 4 | 1 (gi112) | 0 | 3 (gi42, 95, 182) | -1 | 5.0200 -> 5.0150 |

**A/B on shipped decks: none owed** -- no suite deck's digest moved. **Slower games: none** (no shipped deck
moved; on the probe list every moved game is same-turn or faster).

## Found, not fixed (inert on every shipped deck)

* **A free {X} spell needs an AFFORDABLE X = 0 base candidate.** The emission prune stands down while a
  charge is live (USER 2026-08-16), but the X emitters carry their OWN affordability gates
  (`tmax < 0 -> continue`, `qmax < 0`, `c.ManaValue() > pool_total -> continue`) that never consult the
  charge. So an {X} spell whose fixed part is unaffordable (Earthquake with no red source) has no base
  candidate and therefore no free variant. Same family as the exemption the USER already approved; no
  deck reaches it.
* **Convoke counts ride into the free twin.** A free Chord of Calling copied from a convoke variant keeps
  `convoke_green/other`, and both worlds tap those bodies before the cast (`ApplyConvokeTaps`, AIEngine and
  the rollout apply, keyed on the counts alone) for a cast that pays nothing. No deck holds an Archangel
  and a convoke card.
* **`MTG_FREECAST_PRUNE` counts VARIANTS, not cards**, when it picks the top (charges + 2) candidates by
  mana value: a card with several variants (Unite's splits, an X spell's X values) can occupy several of
  the slots. Untouched here (it would move FiveColour).

## Open questions for the USER (none blocks anything)

1. Free X = 0 casts the enumeration does not offer -- a free Crackle at X = 0 (a pure cast trigger), a free
   Genesis Wave / Reality Spasm at X = 0 -- stay unoffered, by the same emitter / `XCandidates` judgement as
   their paid casts. The alternative is to have the bank synthesise one X = 0 free variant per {X} card.
   Recommended: leave as is until a deck pairs a free-cast source with an {X} spell whose X = 0 cast
   matters (no shipped list does).
2. Fix the convoke-on-free-cast and the affordable-base gap above now (both inert today), or when a deck
   first needs them?
