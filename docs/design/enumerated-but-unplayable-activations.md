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

## 2. Umezawa's Jitte's `gain 2 life` mode — CLOSED (2026-08-26)

`Action::Kind::JitteModeAbility` with `gy_exile_mode == 2` (KittyEquipment) graded **legal · not
enumerated**: a legal play a human could not make. Closed by a one-line guard in
`BuildEquipPieceDeps` ([TurnSolver.cpp](../../src/ai/TurnSolver.cpp)), with two new fixtures.

### Root cause: an equip-only predicate applied to a whole activation FAMILY

`ActivationFamilyKey` buckets seven activation kinds that share a source permanent into one
odometer group — including `Equip` and `JitteModeAbility`. An Equipment is the source of *both* its
Equip actions and its counter modes, so **Umezawa's Jitte is the one card where those two kinds land
in the same group.**

`BuildEquipPieceDeps` hoists the stranded-equip rejection from the subset to the odometer digit. It
decided a group was "an equip group" by testing its **first member only**:

```cpp
if (groups[g].empty() || cands[groups[g][0]].kind != Action::Kind::Equip) { continue; }
```

…and then stamped equip-piece requirements on **every** member of that group. A member with no
target took the `nums[k] <= 0` branch and got `PieceReq{required = true, groups = 0}` — "needs a
piece that no group can ever cast", i.e. a permanently dead digit.

Modes 2 and 3 have no target. "You gain 2 life" affects *you*; "equipped creature gets +2/+2"
affects the *equipped creature*, which is not a target either (that is why mode 3 is gated on being
attached rather than on a legal target). Both carry `sac_victim_id == 0` — so both were pruned
before `eval_and_push` ever saw them. Mode 1 targets a creature, so it survived and masked the bug.

The fix restores the invariant the predicate's own comment claims — that it "rejects a subset of
what `SubsetHasStrandedEquip` rejects", a guard which likewise considers only `Kind::Equip`:

```cpp
if (a.kind != Action::Kind::Equip) { continue; }   // only an Equip has pieces
```

### It was not only mode 2 — the +2/+2 mode was dead too, in the commoner board

The defect fires only when an `Equip` action for that same Equipment is *also* enumerable, because
otherwise the group is JitteMode-only and its first member is not an Equip. That is why
`kitty_jitte_pump_line.json` never caught it: one creature, an already-attached Jitte, so there is no
legal equip target and no Equip action to head the group. Add a second creature — a legal "move the
Jitte onto Kemba" — and pre-fix:

| board | `jittemode=2` | `jittemode=3` |
|---|---|---|
| attached Jitte, 1 creature (existing fixture) | n/a | `choose` ✓ |
| attached Jitte, 2 creatures (Equip heads the group) | `legal_not_enumerated` ✗ | `legal_not_enumerated` ✗ |
| …same board, after the fix | `accept` ✓ | `choose` ✓ |

Mode 3 is the mode the USER explicitly asked for on 2026-08-24 ("Pump for Umezawa's Jitte should be
handled like the others. I should be able to activate them any time counters are present."). It had
been silently unreachable in exactly the situation a real game reaches most — more than one creature
on the board. `choose` is the correct verdict there, not `accept`: mode 3 rides `chosen_x`, so
`CheckLine` offers an `activations` sub for how many counters to spend.

### Blast radius: none outside the Jitte

An `Equip`'s `sac_source_id` is the *Equipment's* permanent number. The other family kinds are
sourced on creatures — `PutFromHandAbility` on the Stoneforge, `AttachAllEquipment` on Balan,
`GraveyardExileAbility` on the Deathrite — so they can never share a source with an Equip and never
formed a mixed group. Only an Equipment that is itself an activation source can, and Umezawa's Jitte
is the only one implemented. All three Jitte modes are human-play-only besides (USER doctrine
2026-08-14 prunes them from autonomous search), so ground truth is untouched: smoke 42/42 and
regression 70/70 with **0 configs changed**, 224 references with 0 enum-gap.

### Fixtures

* `test/scenarios/kitty_jitte_lifegain_line.json` — mode 2 on the section-2 board, expects `accept`.
* `test/scenarios/kitty_jitte_pump_group_line.json` — mode 3 in the **group-collision** shape
  (attached Jitte + second creature), expects `choose`. This is the shape the older pump fixture
  could not reach.

### Method lesson

The investigation twice recorded "the odometer position IS walked" — inferred from an
`[enum-stats]` line reporting a group of size 3 and a position bound of 4. That inference was wrong,
and it sent the search downstream into `eval_and_push`'s guard chain and `CheckLine`'s matching,
neither of which had anything to do with it. Two `fprintf`s — one at `eval_and_push` entry, one at
the plan push — settled it in a single run: **only two of the three members ever entered the
lambda.** A bound is a count of positions the odometer *may* visit, not evidence that any particular
one was passed on. Same lesson as [[measure-the-behaviour-not-just-the-outcome]]: instrument the
step you are actually claiming, not an aggregate that is merely consistent with it.
