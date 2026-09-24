# Sac-for-mana fodder created in the SAME line

**Status: ADOPTED 2026-09-24, `MTG_SAC_FODDER_SAME_LINE` DEFAULT ON.** The diagnosis below is
unchanged and still accurate; the fix, the measurement and the adoption are described at the end.

**USER 2026-09-24, on scope:** *"We should probably not restrict it at all."* then *"But, you can
finish adopting as-is and then widen it."* — so the adopted cut keeps the two restrictions below
(mana outlets only; counter-costed fodder makers only) and **widening them is the next step, taken
as its own measured change.** See "Widening" at the end.

**Third report is what moved it.** The user hit the same shape again on 2026-09-24, on the candidate-B
Fungus list (`logs/play/rejections/Fungus_cod_candidate-b-2026-09_s1_gi0_t4.json`), turn 4:

> `land=Peat Bog;cast=Doubling Season;cast=Utopia Mycon;cast=Sporecrown Thallid`
> — *"can't pay {1}{G} for 'Sporecrown Thallid' with the mana available this phase"*

The board makes **6** and the line needs **7**. The engine's own plan 17 (`Peat Bog; Doubling Season,
Tukatongue Thallid`) proves it counts 6, and the missing one is exactly this: Utopia Mycon's spore
ability makes a Saproling for three idle counters, and Mycon's own sac outlet eats it for a mana.
Neither ability taps, so Mycon still taps for `{G}` afterwards under the Brightcap Badger grant, and
the token being summoning-sick is irrelevant — sacrificing needs no untapped, unsick body.

**The 2026-09-18 deferral reason has expired.** It was *"landing it unvalidated immediately before an
overnight value-leaf generation would fit the learned evaluator to a barely-tested engine"*. That
generation finished long ago and the Fungus mulligan was adopted 2026-09-24.

## The report

USER, 2026-09-17, on `logs/play/rejections/Fungus_cod_s5_gi4_t4.json`:

> "Should be able to activate and sac in same line ideally, to avoid extra breakpoints."

and again 2026-09-18, seed 9 / T5, a three-item line:

> ```
> 1 Thallid Shell-Dweller ⟳ ability
> 2 Utopia Mycon ⟳ sacrifice
> 3 Sporesower Thallid
> ```
> "Only commits the Sporesower Thallid"

Both are one shape. The human wants:

1. remove three spore counters → **create a Saproling** (free, no mana, no {T}),
2. **sacrifice that Saproling** to Utopia Mycon → add one mana of any colour,
3. spend that mana, with the lands, on a cast the lands alone cannot afford.

## Reproduction

`logs/s9probe/*.json` (scratch) — the minimal board is three Forests, a Thallid
Shell-Dweller on three spore counters, a Utopia Mycon, and a Sporesower Thallid
({2}{G}{G}) in hand. Three Forests are three mana; the line needs four.

| line | verdict |
|---|---|
| `cast=Thallid Shell-Dweller` (the spore activation alone) | **accept** |
| `cast=Thallid Shell-Dweller;cast=Sporesower Thallid` (4 lands, no sac needed) | **accept** |
| `cast=Sporesower Thallid` (4 lands) | **accept** |
| `cast=Thallid Shell-Dweller;sacout=Utopia Mycon;cast=Sporesower Thallid` (3 lands) | **illegal** — "can't pay {2}{G}{G}" |
| `cast=Thallid Shell-Dweller;sacout=Utopia Mycon` (3 lands, no cast) | legal_not_enumerated |

So activations and casts combine fine in general. What does not exist is the
**dependency**: fodder that the line itself creates.

### The decisive pair

Two boards identical in every respect except **where the Saproling comes from**
(both shipped as fixtures — `test/scenarios/fungus_sac_fodder_on_board.json` and
`fungus_sac_fodder_same_line_GAP.json`):

| fodder | line | verdict |
|---|---|---|
| a Saproling token already on the battlefield | `sacout=Utopia Mycon;cast=Sporesower Thallid` | **accept** |
| the line makes it (`cast=Thallid Shell-Dweller` = remove three spores) | `cast=Thallid Shell-Dweller;sacout=Utopia Mycon;cast=Sporesower Thallid` | **illegal** |

The sac-for-mana machinery — the outlet, the any-colour fan, the payment coupling,
the plan match — is entirely sound. The single missing capability is *fodder created
within the same line*.

Writing that pair required adding **token staging** to the scenario harness
(`"token": true` on a battlefield entry). Tokens have no `cards.json` entry, so
`make_card` could not build one and a token board was previously inexpressible as a
fixture — which is why Fungus, a Saproling-token deck, had **zero** fixtures.

`MTG_SAC_OUTLET_PAY` (§2b) does **not** fix it — verified directly, same verdict
with the lever on. §2b makes existing fodder a payment source; it reads the
battlefield at payment time, and at payment time the Saproling has not been made.

## Root cause — three sites, not one

**1. The action is never emitted.** `TurnSolver.cpp` (the sac-outlet candidate
loop, ~line 16290):

```cpp
const int victim_id = CanonicalSacVictim(state, state.active_player_index,
                                         src.card.m_number, need_sub, ...);
if (victim_id < 0) { continue; }   // no legal victim to sacrifice
```

Candidates are collected against the board **as it stands**. With no Saproling out,
`CanonicalSacVictim` returns -1 and the `SacForMana` action is never emitted at all —
so no subset can contain it, and nothing downstream gets a chance to notice that a
co-selected spore activation would have supplied a victim.

Note `SubsetOversubscribesSacFodder` (~line 7599) *already* reasons about this
correctly in the other direction: its `plan_can_add` lambda explicitly credits
"any token the action creates that carries the filter subtype", including
`spore_token_subtypes`. The subset guard understands mid-plan replenishment; the
candidate emitter does not.

**2. The apply order is wrong even if it were emitted.** `apply_continuation_precasts`
(~line 23393) applies `SacForMana` in a **pre-pass, before the casts** — which is
correct and load-bearing for the ordinary case (the mana must float before anything
pays with it). A spore activation is not in that pre-pass, so the sacrifice would run
*before* its victim exists.

**3. CheckLine's affordability walk does not apply board activations.** The
declared-order walk (~line 48110) re-creates a freshly-cast mana **rock** on its
scratch battlefield so later casts can tap it, and untaps lands for an ETB untapper —
but a `board_act` entry that creates a token contributes nothing. So even with 1 and 2
fixed, the viewer would still reject the line.

## Why it was not fixed on 2026-09-18

A correct fix touches the candidate emitter, the apply ordering, and CheckLine —
three sites in the most load-bearing part of the engine — and it is not
Fungus-specific: `state.deck_has_sac_mana_outlet` is also true for Goblins
(Skirk Prospector), where Krenko's tap makes Goblins in the same line. That is a
play change for a deck in the regression suite and needs its own full cycle.

Landing it unvalidated immediately before an overnight value-leaf generation would
fit the learned evaluator to a barely-tested engine. The generation was the user's
stated priority for that night, so the line stayed broken and this was written
instead.

**Staleness is not a reason to hold the generation.** A value leaf fitted to the
engine without this line stays usable after the line lands (the Hinata regen
precedent: staleness is neutral).

## Severity, stated honestly

* **In the viewer it is ergonomic, not a lost play — VERIFIED, not assumed.** The
  human can commit the spore activation on its own (verdict `accept`), and the second
  half then validates `accept` against the board that produces (probe `j_workaround`:
  same three lands, spores spent, Saproling out). So the play is reachable in two
  commits. That is exactly the "extra breakpoints" the user is asking to avoid — the
  cost is clicks, not lines.
* **In autonomous play it is a genuinely missing line.** The search cannot reach it
  either, for the same reason 1 above. How much that costs Fungus is UNMEASURED.

## The shape of the fix

Emit the `SacForMana` candidate when a *co-enumerable* action creates a
filter-matching creature, reusing `SubsetOversubscribesSacFodder::plan_can_add`'s
existing token-subtype scan rather than writing a second one; resolve the victim at
apply time instead of baking `sac_victim_id`; hoist token-creating `PermAbility`
activations ahead of `SacForMana` in the pre-pass; and teach CheckLine's walk to push
the created token onto its scratch battlefield, in the same place it already pushes a
freshly-cast rock.

This is the "fuse create+spend, payability-gated" shortcut doctrine
(`docs/design/`, the Clue-fusion strand) applied to a sac outlet.

## What was actually built (2026-09-24)

**FUSION, not reordering — and that is the whole design choice.** The shape above proposes four
edits: emit the candidate, resolve the victim at apply time, *hoist token-creating activations ahead
of `SacForMana` in the pre-pass*, and teach CheckLine's walk. The hoist is the expensive one: the
sacrifice is applied at **six** sites (the rollout's `apply_continuation_precasts` plus five in
`AIEngine.cpp`), and any one of them drifting desynchronises executor from rollout silently.

Fusing the create INTO the spend, inside `ApplySacForMana`, makes lockstep **structural** — every one
of those six sites already calls it — and removes the ordering problem instead of solving it.
CheckLine needs no change either, because it accepts by **matching an enumerated plan**; once the
emitter produces the plan, the affordability walk is never reached.

Three pieces, all in the default-OFF arm:

| where | what |
|---|---|
| `core/SpellEffects.h` | `kSameLineSacVictim` (INT_MAX) + `SameLineSacFodderSource` / `MakeSameLineSacFodder` |
| `core/SpellEffects.h` | `ApplySacForMana` resolves the sentinel: re-check board, make fodder, re-check, else no-op |
| `ai/TurnSolver.cpp` | the emitter bakes the sentinel instead of bailing on `victim_id < 0` |

**Why INT_MAX and not 0.** Token ids count up from 1000, so no permanent can carry it, and the
existing stale-victim walks already treat `id >= 1000` as a fungible token victim and re-pick — so a
path that somehow bypassed the fusion degrades to today's re-pick rather than to a wrong sacrifice.
`0` would have been wrong twice over: it is the legacy no-victim value **and** it routes around the
`s_no_phantom_float` guard, floating mana for a sacrifice that never happened.

**Two deliberate restrictions.** Only **mana** outlets (a value outlet manufacturing a body to eat is
a real judgement call and a much wider enumeration; every report is about mana), and **never pooled**
— `MTG_SAC_OUTLET_POOL` proves its members interchangeable by checking they resolve the same canonical
victim, and with no victim on the board every member answers -1, so that proof is vacuous and a count
axis would promise N mana off fodder that must be manufactured N times.

### Verified

| check | result |
|---|---|
| `fungus_sac_fodder_same_line_GAP`, lever ON | **accept** — `cast: Sporesower Thallid, Thallid Shell-Dweller: remove three spore counters: create a Saproling, Utopia Mycon: sac 1 creature` |
| `fungus_sac_fodder_same_line_GAP`, lever OFF | illegal (unchanged) |
| `fungus_sac_fodder_on_board`, both arms | accept (unchanged) |
| candidate B, the user's board, lever ON | the enumerator now produces `land=Peat Bog; cast: Doubling Season, Sporecrown Thallid, Utopia Mycon: sacrifice for {G}x1` |
| lever OFF | 161/161 unit, 103/103 scenarios, smoke 93 passed, `play-changed=0` both tiers |

**The user's line still needs the sacrifice NAMED.** `land=Peat Bog;cast=Doubling Season;cast=Sporecrown
Thallid;sacout=Utopia Mycon` → **accept**. The originally-typed form, which named the spore pop but not
the sacrifice, stays `illegal` — correctly, because as typed it contains no mana-producing action.

### Known limitation of this cut — the DOUBLE POP

`apply_continuation_precasts` runs before the trailing activation pass, so a plan carrying **both** a
same-line sac and its own spore/fade pop pops twice: the sac fuses one, then the plan's own fires.

* On a source holding **exactly** the activation cost — the common case, and what the GAP fixture
  declares — the second pop finds no counters and no-ops, so the line is exactly as declared.
* With counters to **spare** it really does pop twice. That is a legal play and it is correctly
  *evaluated* (the search grades the state the apply produces, so nothing is mis-scored), but it is
  not the line a human typed — a reference-fidelity gap, not a soundness one.

Hoisting the plan's activation into the pre-pass is **not** the fix: the trailing pass would apply it
a second time, and `sp` is a continuation plan whose actions that pass does not own. The fix is to
teach the trailing pass which activations were already spent. Deliberately kept out of the adoption.

## Adoption (2026-09-24) — default ON, ground truth rebaselined

**Every config that moved, moved the right way, and only Fungus moved at all.**

| tier / config | before | after | delta |
|---|---|---|---|
| `fungus_regression_d3_s2002` | 5.4300 | 5.4050 | **−0.0250** |
| `fungus_regression_d3_s3003` | 5.4850 | 5.4550 | **−0.0300** |
| `fungus_regression_d5_s2002` | 5.4900 | 5.4600 | **−0.0300** |
| `fungus_regression_d5_s3003` | 5.4700 | 5.4500 | **−0.0200** |
| `fungus_regression_d0_s2002` | 5.7670 | 5.7620 | −0.0050 |
| `fungus_smoke_d3_s1001` | 5.4133 | 5.3800 | **−0.0333** |
| `fungus_smoke_d5_s1001` | 5.4267 | 5.3867 | **−0.0400** |
| `fungus_smoke_d0_s1001` | 5.6820 | 5.6820 | 0 (same score, different line) |

129 regression configs: **5 changed, 124 unchanged**. Per-game audit `[searched] slower=1 faster=17`.
Consistent across two seeds and all three depths, which is what distinguishes this from churn.

**References still replay, and that was the gate that mattered** — adoption widens the enumeration, and
a reference must stay matchable by the search. Full corpus, 335 refs: **0 play-drift, 0 ENUM-GAP,
0 CONTRACT-FAIL** (the three `--strict` gating classes). The 1 `board-diverged` is NOT in Fungus:
re-running the Fungus refs alone gives 0 board-diverged with the lever **both on and off**, and no
non-Fungus fingerprint moved, so it predates this change. The Fungus corpus goes 6 ok / 1 repaired →
4 ok / 3 repaired, which is the expected re-anchoring when more plans are offered, not a failure.

Accepted into `regression_gt.txt` for **both** tiers via `regression.sh [--smoke] --accept`
(`gt_logs consistent: 129` then `93`, STALE 0 in both). Post-accept smoke: 93 passed,
`play-changed=0`.

**⚠ THE OVERNIGHT TIER IS NOW STALE.** It was not re-run, so its Fungus fingerprints still describe
the pre-adoption engine and its next run will report those configs as changed. That is expected, not a
regression — accept it after inspecting, exactly as here.

## Widening — the next step, NOT yet done

USER 2026-09-24: *"We should probably not restrict it at all."* The adopted cut keeps two
restrictions, and each needs its own measured change:

1. **Mana outlets only.** Extending to VALUE outlets (Psychotrope Thallid's `{1}`, sac a Saproling:
   draw; Deathspore/Vitaspore's sac payloads; Siege-Gang's damage) needs the same fusion in the
   *creature-sac-outlet* apply path, which is a different function from `ApplySacForMana`.
2. **Counter-costed fodder makers only** (spore/fade). Widening to other FREE makers brings in
   `{T}`-costed ones — Krenko, Mob Boss (`tap_creates_tokens_per_controlled_subtype`), which is
   exactly the Skirk Prospector line the original write-up expected to be affected. **This one really
   will move Goblins**, so it needs its own rebaseline.

**A MANA-COSTED maker (Slimefoot's `{4}`: create a Saproling) is a different problem and must not be
swept in with the rest.** The fusion runs inside the apply, after the enumerator has already credited
the sacrifice's mana — so paying `{4}` there would debit mana the subset's accounting never charged.
That is the phantom-mana shape `MTG_SAC_NO_PHANTOM_FLOAT` was just fixed to remove. Widening to
mana-costed makers requires the COST to be visible to the enumerator, not just to the apply.
