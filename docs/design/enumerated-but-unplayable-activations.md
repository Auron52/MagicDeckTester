# Enumerated-but-unplayable activations (2026-08-23)

Found while making board activations clickable in the play viewer (the `activate: true` / `verb`
work in `src/main.cpp`'s plan-action JSON). One is **fixed**; one is **open**.

---

## 1. Wirewood Lodge's `{G}, {T}: Untap target Elf` — FIXED (human-play gate)

**The ability works.** This section originally claimed it never did; that claim was wrong and the
measurement behind it was broken. Recording both, because the bad method is the reusable lesson.

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

### The real (much narrower) defect

Everything upstream of the apply reasons about `AvailableManaPool` (over-approximate); the trailing
apply pays through `TapForCostDirect` (exact). On StompySurprise s2 t6 the pool reported **5 green**
while the only untapped green sources were an Arbor Elf whose "untap target Forest" had no untapped
Forest left and an Elvish Mystic that had entered off Call of the Wild that turn. So the enumeration
offered an activation the apply could not pay — harmless in autonomous play (the search just scores
a no-op line) but a **dead button** in the viewer: measured 127 offers across 6 seeds, 0 fires.

**Fix (shipped).** The Lodge enumeration's existing `HumanPlayActive()` branch already narrows to
"a matching Elf is actually tapped" under the rule *a plan menu must never contain a silent no-op
option*. It now also trial-pays the cost on a `GameState` copy and drops the action when that fails.
Human-play-only → autonomous enumeration is byte-identical and keeps the looser gate it needs (its
trailing apply runs after the plan's casts, which enumeration cannot see). `UntapCreature` is
therefore in the clickable set. Pinned by `stompy_lodge_untap_offered.json` (payable → offered) and
`stompy_lodge_untap_hidden.json` (unpayable → hidden).

**Still open, deliberately not fixed here:** the pool over-approximation itself. It is the same
conditional-source / summoning-sickness blindness the affordability arc has been chipping at
(`mana-affordability-arc-handoff.md`), it is GT-moving, and it is not Lodge-specific — the trial-pay
above is a viewer-side containment, not a cure.

### Method lesson (this is the part worth keeping)

The false "6105/6105 attempts, never pays" number came from a sweep that re-invoked the binary once
per step with an accumulating `--choices` prefix. Every invocation **replays the whole game from
scratch**, so one starved frame re-emitted its counter line on every subsequent step: the sample was
a handful of distinct frames counted hundreds of times, dressed up as a large-N result. It was also
`--depth 0 --claude-play` throughout, so it never executed the autonomous path it was used to
indict. **A stateless-replay protocol makes per-step instrumentation counts meaningless — count
inside ONE process, and check the deck's existing fixtures before declaring a card inert.**

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
