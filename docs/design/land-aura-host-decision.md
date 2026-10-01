# Which land an "Enchant land" Aura should enchant

Status: **the sound fold is SHIPPED and default ON. The heuristic pick is BUILT and default OFF,
pending a user decision on re-accepting ground truth.** 2026-10-01.

Cards in scope (`params.is_land_aura`): Wild Growth, Fertile Ground, Overgrowth, Trace of Abundance.
Decks that run them: `Fungus` (2x Wild Growth) and `EldraziDisplacerFlicker` (all four).

## The problem

A land Aura emits **one `CastFromHand` variant per legal host** — Wild Growth's card note says so
outright: *"WHICH land to enchant is a searched plan variant per legal host."* On a deck running 19
Forests that is 4 variants per copy at turn 4, and 2 copies in hand multiply: 25 odometer positions.

Measured on `fungus d3 s2002` (200 games, `--threads 1`), the heaviest such decision:

```
HEAVY rank=2  odo=400 turn=4  plans=341 dedup=41
  GROUP size=4 Wild Growth [cast:host=Forest | cast:host=Forest | cast:host=Forest | cast:host=Forest]
  GROUP size=4 Wild Growth [cast:host=Forest | cast:host=Forest | cast:host=Forest | cast:host=Forest]
```

**88% of the plans at that one decision are duplicates.** `LandAuraHostCandidates` exists to narrow
the host set and is documented as *"the only legal home for a narrowing"* — but the base returns
empty (= no narrowing) and only `EldraziFlickerProvider` overrides it. `FungusProvider` does not.

## The USER's rule (2026-10-01), verbatim

> "To be fair, where you put Wild Growth is often not that important except: usually depletion lands
> are a bad idea, sometimes bounce lands can cause issues and which lands you tap to cast the wild
> growth is quite important. In decks where there are lands that don't produce green the non-green
> producing lands are often a good choice because you might have to pay for it with the other lands."

> "So Secluded Couryard -> Green producing non-depletions -> depletion lands would be the best to
> worst order."

> "There is a very small consideration to put a wild growth on Peat Bog, because it doesn't produce
> green. That might be worth actually searching if you don't have secluded courtyard."

> "In other words, choose secluded courtyard if available, search on one green land or peat bog if
> not. Never put it on Hickory unless that is your only land."

> "Actually for peat bog we can skip searching it if there are 2 green lands out." / "We can put it
> on one of them and pay with the other."

> "So we only search when we have 1 green land, peat bog and maybe hickory woodlot out." / "So the
> search is very very limited. Usually we just pick." / "usually we have 1 choice. Occasionally we
> have 2."

### Restated card-agnostically

Two properties of the HOST, relative to the **Aura's own mana cost**:

* `PAYS` — the land produces a colour that cost needs (a "green land", for Wild Growth's `{G}`).
* `DEPLET` — `enters_tapped_with_depletion > 0` (Peat Bog, Hickory Woodlot).

| class | PAYS | DEPLET | example | rule |
|---|---|---|---|---|
| **A** | no | no | Secluded Courtyard | **best** — enchanting it spends nothing you needed to CAST the aura with. Take it, stop. |
| **B** | yes | no | Forest, Brushland | the ordinary pick |
| **C** | no | yes | Peat Bog | a GENUINE tie against B — keep as a 2nd branch, but only while **fewer than two B lands are out** |
| **D** | yes | yes | Hickory Woodlot | dominated by B on both axes — offer only if nothing else exists |

Output is **one** host, except the single case {exactly one B out} x {a C present} → **two**.

## What shipped: the sound identity fold (default ON)

`FoldInterchangeableAuraHosts`, `MTG_LAND_AURA_HOST_FOLD` (`=0` disables).

Collapses hosts that are **the same card in the same state** — one representative per
`(name, tapped)` class. Soundness is bought by being narrow, not by enumerating what might differ:

* A host folds only if **provably featureless** — no counters of any kind (charge/verse/lore/storage/
  ice/age/spore/fade/quest), nothing attached (aura or Equipment), and not `entered_this_turn`. Any
  decorated land keeps its own slot and is never folded with a plain one of the same name. The
  failure direction is "kept a redundant variant", never "deleted a distinct line".
* Different NAMES never fold. Peat Bog, Secluded Courtyard, Hickory Woodlot and Simic Growth Chamber
  each keep their own branch automatically — which is exactly why the user's "occasionally 2" case
  survives the fold without the fold knowing anything about it.
* **One representative per class, not one per aura.** A first cut kept
  `min(class_size, land-Auras-in-hand)`, reasoning that two auras on the same land differ from two on
  different lands. They do not differ in the direction that matters: each aura adds its bonus
  whenever ITS land is tapped, so tapping every land yields the same total either way (2 Wild Growth
  over 4 Forests = 6 mana, stacked or spread), and when only a subset is tapped, stacking is weakly
  better. Spreading is never strictly better.

### THE KAROO EXCLUSION — inherited, and measurably load-bearing

The plan SIGNATURE already folds aura hosts (`MTG_EDF_AURA_HOST_SIG`) and was deliberately narrowed
away from one branch: *"The fold is only WRONG when this plan's land drop is a karoo — the bounce is
what takes the folded host (s12 T3)."* A karoo returns a land to hand on ETB, so which land carries
the aura stops being interchangeable exactly there. **Fungus runs 3 Simic Growth Chamber**, so this
deck has that hazard live. Folding at the CANDIDATE level is earlier than the signature, so it must
honour the same exclusion — `g_enum_karoo_drop`, whose scope wraps the `EnumeratePlans` call and so
is readable from inside `CollectActions`.

**This was not a precaution, it was a bug fix.** The first build of the fold omitted the exclusion
and dropped **11,538 distinct plans** (`dedup` 750,223 → 738,685). With the exclusion in place
`dedup` is **exactly** 750,223 — byte-equal to the unfolded arm.

### Measured, `fungus d3 s2002`, 200 games, `--threads 1`

| arm | odo | plans | **dedup** | avg turns |
|---|---:|---:|---:|---:|
| fold=0 pick=0 (baseline) | 4,263,313 | 941,287 | **750,223** | 5.4050 |
| **fold=1 pick=0 (shipped)** | 4,118,633 | 879,307 | **750,223** | 5.4050 |

**61,980 plans removed (6.6%), 144,680 odometer positions removed (1.035x), and the distinct-plan set
is bit-for-bit the same size.** That is the signature of a pure identity collapse: same answers,
less work to reach them. Gates: unit 343/343, scenarios 118/118, smoke 101/101 `play-changed=0`.

## What did NOT ship: the heuristic pick (default OFF)

`PickLandAuraHosts`, `MTG_LAND_AURA_HOST_PICK` (`=1` enables). Implements the table above.

Two reasons it is off:

1. **It moves ground truth.** With it on, smoke shows `fungus_smoke_d0_s1001` and
   `fungus_smoke_d3_s1001` changing digest at an **identical average** (5.6810 → 5.6810,
   5.3800 → 5.3800), `[searched] play-changed=1` at the same score, and
   `[d0] slower=1 faster=1 play-changed=27`. So it is **net-neutral on quality and removes work** —
   a good trade under the collapse doctrine — but adopting it means re-accepting GT, and that is a
   user decision, not an agent's.
2. **Its main beneficiary is unmeasured.** Fungus's land base is 19 Forest + 3 Simic Growth Chamber,
   i.e. **every land is class B** — the A/C/D rows never fire there, so Fungus can only ever show the
   tie-break, never the rule. The deck whose land base actually exercises A/C/D is
   `EldraziDisplacerFlicker`, and **it is not in `test/regression_cases.sh` at all.**

### THE PROVIDER WINS — a guard found by a failing fixture

The generic pick runs **only when the deck's provider did not narrow**. Without that guard it
overruled `EldraziFlickerProvider`'s yield-based ranking and failed
`test/scenarios/edf_shroud_blocks_second_aura.json`, which pins the resulting attachment
(`Overgrowth` → `Brushland`) and exists precisely because *"no other assertion in this harness can
see this class of defect"*. A deck that supplies its own host ranking has justified it and may have
it pinned; a generic heuristic must defer.

Within a class, a non-bounce land is ordered ahead of a bounce land (*"sometimes bounce lands can
cause issues"*). That is an ordering inside an already-tied bucket, so it narrows nothing extra — it
only stops `front()` from handing the aura to a karoo on battlefield order alone.

## THE KAROO TURN IS A WIDTH HOLE, not just a fold hazard (USER, 2026-10-01)

> "If we have the Karoo this doesn't change much except that the Karoo is a reasonable target on the
> turn when it bounces a land."

> "Karoo otherwise is just a green land, but obviously the plans that play Wild Growth T1 can be
> impacted by the Karoo dropping on T2. On T2 the Karoo is 100% a valid enchant target if it bounces
> a green land from which you float mana."

Two separate statements, and both matter:

**(a) Off its drop turn, a karoo is just a class-B green land.** Simic Growth Chamber produces `{G}`,
does not deplete, and ties with Forest. The identity fold still keeps it in its own class because it
keys on card NAME — strictly conservative, and it costs one extra branch rather than risking one.

**(b) On its drop turn the karoo is a valid host — "100%" — under a stated condition:** *if it bounces
a green land from which you float mana.* That condition is not incidental, and the engine **already
arranges exactly it**. The deferral's own comment: *"the apply plays a Karoo AFTER the main casts, so
the bounce returns a land that was already TAPPED for this turn's casts."* Float first, bounce the
spent land, and the karoo is left carrying the aura with nothing lost. So the sequencing is right and
the host is sound — the engine simply **cannot offer it**.

**But the karoo is not a legal host on its own drop turn.** `EnumeratePlansWithLand`'s karoo branch
does:

```cpp
karoo_drop = true;
copy = state;        // the PlayLandByName above was the legality probe only
// ...then erases the karoo from copy's HAND
```

so the karoo is neither in hand nor on the battlefield while `CollectActions` runs, and
`LegalEnchantTargets` therefore cannot offer it. The deferral is deliberate and well-argued — it
exists so that "karoo + cast spending the bounced land's mana" is enumerable, a line that was
previously rejected at any budget (viewer artifact s1 T2) — but it was written about CASTS, and the
aura-host consequence looks unconsidered.

So on a karoo turn the engine currently:
* cannot enchant the karoo (a host the user rates at "100%"), and
* must keep every other land distinct (the fold stands down), paying full width for a set of hosts
  among which the user says the choice barely matters.

**And the hazard is CROSS-TURN, which the fold's within-turn soundness argument does not cover:**
*"the plans that play Wild Growth T1 can be impacted by the Karoo dropping on T2."* A T1 aura on a
Forest can be undone by a T2 karoo bouncing that Forest. The fold is still sound here — it collapses
only hosts that are interchangeable in the T1 state, and the T2 bounce TARGET is its own searched
decision, so the search can bounce an unenchanted Forest instead. But it means the fold's correctness
rests on the bounce target staying a real decision, which is a dependency worth writing down rather
than rediscovering: if the bounce target is ever narrowed to a single heuristic pick, this fold's
argument has to be re-checked against it.

**Both halves point the same way, which is what makes this worth doing.** Offering the karoo as a
host on its drop turn would ADD the dominant line and let the fold collapse the rest.

Not attempted here, because the fix touches the deferral's own invariant (the karoo enters TAPPED and
"never funds this turn", so adding it to the enumeration board must not let its mana be spent, and
must not re-break the s1 T2 line). That needs its own fixture before any code —
`test/scenarios/` has the right shape for it, and `edf_shroud_blocks_second_aura.json` is the
precedent for asserting an ATTACHMENT rather than a life total.

## Open

* **Does the user want `MTG_LAND_AURA_HOST_PICK=1` adopted** (one `regression.sh --smoke --accept`
  plus the regression tier, given the net-neutral score)?
* **Offer the karoo as a host on its own drop turn** (previous section) — adds the dominant line AND
  lets the fold collapse the rest. Needs a fixture first.
* **`EldraziDisplacerFlicker` is not in the regression suite.** Until it is, the A/C/D rows of the
  rule have no ground truth anywhere, and the 3x cost rule
  (`scripts/suite_gate.py --cost decks/EldraziDisplacerFlicker`) has not been checked for it.
* A measurement caveat worth keeping: once `pick` changes which land is enchanted, the arms play
  different games, so plan/call counts stop being a work metric (calls went *up*, 201,894 → 204,551).
  Only the play-identical fold admits a clean before/after on those counters.
