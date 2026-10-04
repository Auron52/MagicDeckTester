# A death trigger can save a 0/0 in this engine. Under the rules it cannot.

**Status: BUG, not yet fixed. Fixing it is a PLAY CHANGE owed an A/B and a GT re-accept.**

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

## 2. What the engine does instead

`SweepDeadFadeTokens` walks the battlefield downward and evaluates its toughness verdict **per
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

**`ApplyMassDeathBulk` is already that structure.** It was built as a performance fix and guarded
so it could not change play — but it is the rules-correct model, and the interleaved loop it falls
back to is the incorrect one. The guard currently has it backwards: it declines the correct path
precisely when the incorrect path would differ.

## 4. Why it is not simply switched on

Making bulk unconditional changes play on any board that can reach `GainLife` from a death trigger,
so it is subject to the repo's normal discipline rather than an identity gate:

* a play A/B with digests, and a GT re-accept for every affected tier;
* `python3 test/check_gt_logs.py` afterwards, since GT would move.

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

That is an argument about today's decklists, not about the engine, and
[[collapse-silently-dead-from-decklist]] is the standing warning against resting on it: a
three-card list change can make a dead path live with no commit and no digest movement.

## 6. The meta-lesson, which is the reason this file exists

I wrote a rules claim into a load-bearing code comment as the *justification for a guard*, and it
was wrong. The guard happened to still be necessary — for engine byte-identity — so no test could
have caught it: every gate was green, and a future reader would have inherited a confident,
incorrect statement about CR 704 sitting next to code that depends on it. CLAUDE.md's instruction
is explicit and I did not follow it: *"After implementing any MTG logic — read the skill and review
the code for rule-violation bugs before committing."* Reading `mtg-rules.md` **before** writing that
comment would have inverted the whole finding from "a trigger can save a token" to "this engine
lets a trigger save a token, and that is a bug."

Related: `keepgen-mass-death-bulk-removal.md` (the guard and the 363x),
`.claude/skills/mtg-rules.md` (CR 704), `mulligan-rollout-performance-floor.md`.
