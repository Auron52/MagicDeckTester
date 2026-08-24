# Enumerated-but-unplayable activations (2026-08-23)

Found while making board activations clickable in the play viewer (the `activate: true` / `verb`
work in `src/main.cpp`'s plan-action JSON). One is **fixed**; one is **open**.

---

## 1. Wirewood Lodge's `{G}, {T}: Untap target Elf` — CLOSED (nothing was wrong with the pool)

**The ability works, and the mana pool is not over-approximate.** This section has now been wrong
twice about the same card, in two different ways. Both are recorded, because the reusable content
here is entirely methodological.

### What is actually true

Instrumenting both apply sites (`AIEngine::TakeTurn`'s trailing pass and `apply_one`'s) and running
plain autonomous StompySurprise games — one process, no replay:

| seed | rollout can / paid | executed can / paid | fires |
|---|---|---|---|
| 1001 | 100814 / 74002 | 25 / 20 | 74022 |
| 2002 | 70150 / 42321 | 22 / 13 | 42334 |
| 3003 | 2249 / 1220 | 4 / 0 | 1220 |

Fires == total paid in every seed, i.e. **every successful payment untaps a creature**. The untap
also carries its own scenario fixture and always has: `test/scenarios/stompy_lodge_burst.json` pins
the burst line (tap out for 9, then `{G}` + tap Lodge → untap Priest of Titania → tap again) as the
only way to reach Worldspine Wurm's 11th mana. Looking at that fixture first would have settled the
question in a minute.

### Retraction 2: there is no pool over-approximation (2026-08-24)

The revision above claimed a "real (much narrower) defect": that everything upstream of the apply
reasons about the over-approximate `AvailableManaPool` while the trailing apply pays through the
exact `TapForCostDirect`, so the viewer offered a Lodge untap that could never fire — "127 offers
across 6 seeds, 0 fires", blamed on an Arbor Elf with no untapped Forest and a Call-of-the-Wild
Elvish Mystic. **None of that survived measurement.** `MTG_POOL_AUDIT` (added with this retraction,
`TurnSolver.cpp`) compares, board by board, the largest pip count the pool CLAIMS it can pay against
the largest the exact payer actually produces on a copy:

| measurement | result |
|---|---|
| Lodge offers, autonomous d0, 200 games s2002 | 122 offers, pool payable 122, exact payer pays 122 — **0 over-claims** |
| Lodge offers, human play, all 4 committed StompySurprise references | 4 offers, 2 payable both ways, 2 unpayable **both ways** |
| every colour + total, 14 suite decks × 150 games, d0 | **0 gaps / 16,527 probes** |
| every colour + total, 4 decks × 10 games, d3 (rollouts included) | **0 gaps / 906,474 probes** |

Both named causes are in fact already gated in the pool: the Arbor Elf conditional by
`ManaSubtypeGateLive` (through `PermanentManaYield`) and summoning sickness by `CanTapNow`. The two
genuinely unpayable human-play offers are ones the pool ALSO called unpayable — a board with only an
untapped Lodge and a Worldspine Wurm, no green source and no land in hand. The pool was right.

The only looseness the audit does find is the **already-known and already-owned** one: a
multi-colour source is booked as one `ManaPool::wild` and `CanPayFlat` lets a wild pay any pip
(53.5% of probes with `MTG_POOL_AUDIT_WILD=1`). That is the colour-blindness the adopted
`ColorFeasibility` gate (`MTG_COLOR_EXACT`) contains at the subset sites, and per the affordability
arc's LAW (`mana-affordability-arc-handoff.md`) enumeration is *supposed* to stay optimistic there,
because every plan is rollout-scored. It is not a Lodge defect and it is not open work.

### So why was the untap ever offered unpayably? And what happened to the fix?

Because the emission carries no affordability gate of its own — deliberately. It does not need one:
the action carries its `{G}` like any other, `EnumeratePlans` prices it into the subset, and the
plan is dropped when the board cannot pay. The c4153b0e human-play trial-pay was therefore a **dead
check**, and this was measurable three ways:

* toggling it changed **nothing** in all four reference replays (byte-identical stdout);
* `stompy_lodge_untap_hidden.json` returns `legal_not_enumerated` with the trial-pay **disabled**,
  with or without `MTG_UNPRUNED` — the fixture never discriminated;
* it is also unsound in principle in the one direction that matters for a viewer: it runs on the
  pre-cast state, so a plan whose own earlier cast funds the `{G}` would have the option hidden.
  The land drop is safe — `EnumeratePlansWithLand` plays the land before `CollectActions`, now
  pinned by the new `stompy_lodge_untap_after_land.json` — so what remains is a same-plan mana
  spell, unreachable for StompySurprise (Sol Ring makes `{C}`, not `{G}`). It is nonetheless the
  exact looseness the comment itself gives as the reason autonomous enumeration must keep the wider
  gate, applied to human play without noticing.

It has been removed. The **tapped-Elf** half of the human-play gate stays: that one is load-bearing
(it fixed the s9101 gi2/gi3 re-prompt hang). Both fixtures stay as behaviour pins with corrected
comments naming the mechanism that actually enforces them, and the land-drop leg above is added —
so any affordability gate a future change puts back at this emission has three boards to satisfy.

### Method lesson (this is the part worth keeping)

The false "6105/6105 attempts, never pays" number came from a sweep that re-invoked the binary once
per step with an accumulating `--choices` prefix. Every invocation **replays the whole game from
scratch**, so one starved frame re-emitted its counter line on every subsequent step: the sample was
a handful of distinct frames counted hundreds of times, dressed up as a large-N result. It was also
`--depth 0 --claude-play` throughout, so it never executed the autonomous path it was used to
indict. **A stateless-replay protocol makes per-step instrumentation counts meaningless — count
inside ONE process, and check the deck's existing fixtures before declaring a card inert.**

The second wrong diagnosis has its own lesson, and it is not about mana:

* **"The model here is over-approximate" is a hypothesis, not an observation.** It was written into
  a shipped comment, a commit message and a design doc without one board being compared against the
  payer. Two of the three named causes were code that already existed and already worked.
* **A fix whose test passes with the fix disabled is not a fix.** The same commit that shipped this
  one led with *"the dead check that let one ship"* — and shipped another. Toggling the mechanism
  and re-running its own fixture is a ten-second check.
* **Pair a pool with the payment mode it is built for.** The first version of `MTG_POOL_AUDIT`
  compared the full pool against a *noncreature* payment and reported 166 gaps, every one of them
  Ancient Ziggurat — a creature-only source that `BuildNonCreaturePool` already drops. The
  instrument was measuring its own miscalibration, which is how both earlier claims happened too.

---

## 2. Umezawa's Jitte's `gain 2 life` mode — OPEN

`Action::Kind::JitteModeAbility` with `gy_exile_mode == 2` (KittyEquipment). On a hand-built board
(charged Jitte, a Kor Duelist, two Plains, human-play enumeration) the action IS emitted — verified
by instrumenting the push — and the family's other two members both become plans:

```
[DBGL] 3 plans
  [0] cast: equip Umezawa's Jitte      | Equip
  [1] cast: Umezawa's Jitte: -1/-1     | JitteModeAbility m1
  [2] cast: (nothing)
```

The mode-2 action is dropped between `CollectActions` and `EnumerateMainPlans`. Not the plan
signature (`JITTE#<src>m<mode>><victim>` already separates the modes) and not
`SubsetHasDuplicateSacSource` (no Jitte clause). Remaining suspects: the activation-family group
odometer (`ActivationFamilyKey` buckets Equip and both Jitte modes on the same `sac_source_id`) and
the equip-specific collapses that walk those groups.

Impact is small — gaining 2 life against a passive goldfish is worthless, and USER doctrine
(2026-08-14) already prunes the non-combat Jitte modes from autonomous search — but `jittemode=2`
grades **legal · not enumerated**, i.e. a legal play a human cannot make. `jittemode=1` works and is
pinned by `test/scenarios/kitty_jitte_mode_line.json`.

**Shape of a fix.** Instrument the group odometer on that board (three members in one family, two
reach a plan) and find which predicate rejects the digit. If it is human-play-only, the fix is too;
if it is a general group-collapse bug it may touch other decks and needs the suite.
