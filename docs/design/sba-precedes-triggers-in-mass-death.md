# A death trigger can save a 0/0 in this engine. Under the rules it cannot.

**Status: FIXED in `9a4fb2ec`.** Covered by `test/unit/test_mass_death_sba.cpp`. The suite measured
the fix as byte-identical (`configs changed: 0`, `play-changed=0` on both the searched and d0 halves
of all 107 smoke configs, scenarios 118/0/0), so **no GT re-accept was owed** — see §4, which
originally predicted the opposite.

Found 2026-10-04 by USER correction while reviewing the mass-death bulk-removal guard
(`keepgen-mass-death-bulk-removal.md`). I had justified that guard by saying a lifegain trigger
would "save a 0/0 token that was about to die". The user's objection:

> *"+1/+1 counters being put on 0/0s would save them. That is actually not correct if the +1/+1
> counters are added as a trigger because the 0/0s would die before they received the counter. The
> only case where that is true is when they enter with the counters already on them or receive the
> counters before becoming 0/0."*

**That is right, and it means the guard is preserving a bug rather than a behaviour.**

## 1. The rule

* **CR 704.5f** — a creature with toughness 0 or less is put into its owner's graveyard. This is a
  state-based action, not a trigger; nothing goes on the stack.
* **CR 704.3** — whenever a player would get priority, the game first checks for and performs *all*
  applicable state-based actions simultaneously, **and only then are triggered abilities put on the
  stack.**

So the ordering is: toughness hits 0 -> SBA kills it -> *then* the death triggers go on the stack ->
*then* they resolve. A +1/+1 counter arriving from a triggered ability is always too late. The only
counters that save a body are ones it entered with, or received while its toughness was still above
0. (`.claude/skills/mtg-rules.md`, "State-Based Actions (CR 704)", lists the same thing: *"Checked
continuously, before any player receives priority: A creature with 0 or less toughness dies"*, and
the review checklist carries *"SBAs must be checked after every event before any player receives
priority"*.)

## 2. What the engine did instead (before `9a4fb2ec`)

`SweepDeadFadeTokens` walked the battlefield downward and evaluated its toughness verdict **per
body, lazily**:

```cpp
for (std::size_t i = state.battlefield.size(); i-- > 0; )
{
    ...
    if (!is_dead(q)) { continue; }          // evaluated HERE, as the loop descends
    state.battlefield.erase(...);
    OnCreatureDies(state, ctrl, dead, tok, minus, dwatch);   // fires triggers NOW
}
```

So the body at index 5 has its toughness tested *after* the deaths at indices 60, 59, … have
already fired. If one of those triggers routes through `GainLife ->
FireLifegainWatchers`'s `lifegain_each_own_creature_counters` loop (Nykthos Paragon and friends: a
+1/+1 counter on each own creature), the counter lands on body 5 **before** body 5 is ever tested —
and body 5 survives a sweep it should not have survived.

There is no SBA pass in the rollout at all (`PerformDamageAllCreatures`' own comment: *"the rollout
runs no SBA pass, so this helper is the only killer there"*), so this sweep *is* the SBA, and a
lazy per-body sweep is the wrong shape for one.

## 3. The engine already knows the right shape, two screens up

`PerformDamageAllCreatures` does it correctly and says why:

> *"damage is dealt simultaneously (CR 608.2), and only afterwards is lethality checked once, so
> every creature that ends up with lethal damage dies together. A one-pass walk would let an
> earlier death's trigger change a later creature's toughness mid-sweep."*

That last sentence describes this bug exactly. The fix is to give the fade sweep the same two-pass
structure: decide the whole dying set first, remove it, then fire triggers.

**`ApplyMassDeathBulk` was already that structure.** It was built as a performance fix and guarded
so it could not change play — but it is the rules-correct model, and the interleaved loop it fell
back to was the incorrect one. The guard had it backwards: it declined the correct path precisely
when the incorrect path would differ. §4 records how that was resolved.

## 4. What the fix was, and what it cost (this section predicted wrong — kept for the record)

**The fix was to DELETE a condition, not to add one.** The bulk guard
`MassDeathBodiesAreUnobservable` had five conditions; condition **(d)** required that no watcher on
the dying player's side could put counters on creatures — i.e. it declined the bulk path **exactly
when the lazy path's answer would have differed**. That is the inversion named in §3: (d) was not a
safety condition, it was the bug's preservation clause. Removing it makes the correct path the only
path on precisely those boards.

So there were two findings, not one:

| | |
|---|---|
| **rules** | the sweep now matches the executor and CR 704.3; `test_mass_death_sba.cpp` case 1 pins it |
| **perf** | **1.76x** on the degenerate rollout (227 s -> 129 s), because (d) had been declining the fast path on most candidate-b lines — Slimefoot carries `dies_trigger_self_gain` |

A fix that is simultaneously a rules correction and the single largest win of the four is worth
noting as a pattern: **a guard written to preserve byte-identity will preserve a bug if the bug is
what the other path does.** "Byte-identical" is a statement about two code paths agreeing, never
about either one being right.

### What this section originally said, and why it was wrong

> *"Making bulk unconditional changes play on any board that can reach `GainLife` from a death
> trigger, so it is subject to the repo's normal discipline rather than an identity gate: a play A/B
> with digests, and a GT re-accept for every affected tier; `python3 test/check_gt_logs.py`
> afterwards, since GT would move."*

GT did **not** move: `play-changed=0` across all 107 smoke configs. The prediction confused
*reachable in the engine* with *reachable in the suite's 39 decklists* — §5 had already established
that no committed decklist combines a lifegain death watcher with a
`lifegain_each_own_creature_counters` permanent, which is the same fact seen from the other side. The
A/B was still the right thing to run; it is the forecast of its outcome that was unfounded. And the
zero is a fact about today's lists and nothing more — a three-card decklist change can make this path
live with no commit and no digest movement, and then the unit test is the only thing standing between
us and the regression.

### THE EXECUTOR IS ALREADY CORRECT, so this is an fd-diverge and the fix direction is not a judgement call

`GameEngine::CheckStateBasedActions` does the simultaneous model properly — checked, not assumed:

```cpp
while (changed) {
    std::vector<DeadCreature> died;
    for (auto it = state.battlefield.begin(); it != state.battlefield.end(); ) {
        ... if (destroy) { died.push_back(...); it = state.battlefield.erase(it); changed = true; }
    }
    for (const DeadCreature& d : died)       // AFTER the erase loop, never inside it
    { OnCreatureDies(state, d.controller, d.card, d.was_token, d.minus_counters); }
}
```

Its own comment gives the reason as iterator invalidation rather than CR 704, but the structure is
right either way, and the `while (changed)` fixpoint even re-checks after the triggers.

So the two worlds **disagree**: on a board where a death trigger gains life and a
`lifegain_each_own_creature_counters` permanent is out, the executor kills every 0/0 and the
rollout's fade sweep lets one live. That is a prediction divergence of exactly the `[fd-diverge]`
class these files keep warning about, and it means there is no "which side is right" question —
the rollout must be brought to the executor's shape. `ApplyMassDeathBulk` already *is* that shape.

`TurnSolver.cpp`'s inline post-burn SBA (~:33757) is clean by a different route: it erases per body
but never calls `OnCreatureDies`, so no trigger can fire mid-loop and no toughness can move. Only
the fade sweeps interleave.

## 5. Reachability — low today, which is why it has survived

The trigger-saves-a-0/0 window needs, on one side, a death watcher that gains life or whose
subtype-keyed reaction gains life, *and* a `lifegain_each_own_creature_counters` permanent, *and* a
simultaneous mass death. No deck in the repo is known to combine all three — Fungus candidate-b has
Slimefoot (`dies_trigger_damage`, no lifegain) and no Paragon, which is why the bulk path's guard
passes for it and the 363x measurement is unaffected either way.

That is an argument about today's decklists, not about the engine, and it is not a reason to rest:
a three-card list change can make a dead path live with no commit and no digest movement, which is
why the fix went in on the rules argument alone rather than waiting for a deck to expose it.

## 6. The meta-lesson, which is the reason this file exists

I wrote a rules claim into a load-bearing code comment as the *justification for a guard*, and it
was wrong. **No test could have caught it, and it is worth being precise about why:** every gate was
green because the two code paths agreed with each other. Byte-identity compares the engine to
itself, so it is structurally incapable of detecting a shared misreading of the rules — and the
wrong comment was the thing that made the wrong path look deliberate. A future reader would have
inherited a confident, incorrect statement about CR 704 sitting next to the code it licensed.
CLAUDE.md's instruction
is explicit and I did not follow it: *"After implementing any MTG logic — read the skill and review
the code for rule-violation bugs before committing."* Reading `mtg-rules.md` **before** writing that
comment would have inverted the whole finding from "a trigger can save a token" to "this engine
lets a trigger save a token, and that is a bug."

Related: `keepgen-mass-death-bulk-removal.md` (the guard and the 363x),
`.claude/skills/mtg-rules.md` (CR 704), `mulligan-rollout-performance-floor.md`.
