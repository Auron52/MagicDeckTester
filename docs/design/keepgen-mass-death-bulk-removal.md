# Remove a mass death's bodies in ONE pass instead of one erase per death

**Status: PROPOSED. Not implemented. It needs the §3 audit before anyone may call it byte-identical.**

This is the last item on the Fungus candidate-b straggler's critical path. After `ae97edbf` (two
board scans per creature) and `39686e79` (the death-watcher prefilter), that rollout is
**82,678 s -> 259 s = 319x**, and the profile is otherwise FLAT: one symbol is left.

## 1. The measurement

Replaying the generation's own worst rollout — `size6 draw r=20
seed=18054082520470146318`, the one that held the floor barrier for 14.8 h and cost 22.97 h — on
the fixed binary, single-threaded, Profile build:

```
79.71%  __memmove_avx_unaligned_erms      <- caller: PerformUpkeepFading
 2.20%  EffectiveSpellCost
 1.50%  CollectActions
 0.91%  PermanentManaYield
 0.85%  GatherBoardSources                <- the prefilter's own cost: cheap, as intended
```

Nothing else clears 1%. **CAVEAT, and it matters: that is a ~70 s window of a ~260 s run, not a
whole-run share.** `perf` was attached mid-rollout, and this game's turns differ enormously in cost
(turn 6 was 2,204 s before any fix while turns 1–5 together were 2.7 s). Treat 79.71% as "this
symbol dominates the window sampled", not as a priced share of the job. Price it with a
before/after wall measurement, which is the discipline
[[average-understates-a-width-scaled-defect]] and `profile-is-scoped-to-one-cell.md` both exist to
enforce.

## 2. The defect

`SweepDeadFadeTokens` (`SpellEffects.h`) and its sibling `DestroyTokensCreatedBy` both have this
shape:

```cpp
for (std::size_t i = state.battlefield.size(); i-- > 0; )
{
    ...
    state.battlefield.erase(state.battlefield.begin() + i);   // memmoves every element above i
    OnCreatureDies(state, ctrl, dead, tok, minus, dwatch);
}
```

`sizeof(Permanent) == 296` (asserted in `Dominance.h`). So each erase moves
`296 * (permanents above i)` bytes, and the loop pays that once **per death**. A Saproling Burst
under three or four Doubling Seasons has made dozens of tokens on a board of hundreds; when its
last fade counter goes, `RefreshFadeTokens` takes every token to 0/0 and all of them die in one
sweep. Order `deaths x board x 296 B` per sweep — and the search re-simulates that end-step on
every line it explores.

This is the same cost *source* as the rest of the family (296-byte stride cache traffic, see
[[bulk-token-creation-doubler-quadratic]]'s `perf annotate`) but it is NOT the same defect: there is
no board-level question being recomputed here. It is the container operation itself.

## 3. Why the obvious fix is NOT yet byte-identical — the audit required

The obvious fix is the one `PerformDamageAllCreatures` already uses two screens up: collect the
dead, remove them in ONE stable compaction, then fire the triggers.

**That changes what each `OnCreatureDies` observes.** Today:

| | board seen by `OnCreatureDies(token_i)` |
|---|---|
| current | survivors + every **not-yet-processed** dying token |
| bulk | survivors only |

So bulk removal is byte-identical **only if a dying token's presence on the battlefield is
invisible to `OnCreatureDies`.** There is a real argument that it is — the tokens are vanilla, so
`def_absent` is true, so `LookupCached` returns null and every watcher loop `continue`s on them —
but it is not yet checked end to end. Before implementing, verify each of these:

1. **`DevotionTo`** — reached via `RefreshDevotionCreatures`. Does it contribute 0 for a
   `def_absent` permanent, or does it read something off the token? (It should be 0: a token has no
   mana cost. Confirm, don't assume.) Note the walk is additionally gated on
   `state.deck_has_devotion_creature`, so for Fungus it returns immediately — which makes it easy
   to "verify" on this deck and still be wrong for another.
2. **Nothing in the `reactions` application loop may read board SIZE or a creature COUNT.** A
   Blood-Artist-style "whenever a creature dies, each opponent loses 1" is per-death and safe; a
   card that reads "the number of creatures you control" on each death would NOT be. Audit the
   whole tail of `OnCreatureDies`, including `CreateToken`/`CreateTokens` -> `DoublerShift` (counts
   doublers only, `def_absent` skipped -> safe) and `CreateTokenCopyOfCard`.
3. **A dying body with a DEFINITION breaks the argument outright.** `DestroyTokensCreatedBy`
   filters on `q.is_token`, and a token can be a *copy of a real card* (Twinflame). The fast path
   must therefore be gated on `q.def_absent`, with a fall back to the current interleaved loop if
   any dying body has a definition. Do not gate it on `is_token`.
4. **Appended bodies.** `OnCreatureDies` can add permanents (Tukatongue Thallid's replacement
   Saproling, a persist return). A stable compaction preserves the relative order of survivors and
   appends still land last, so positional enumeration order is unchanged; absolute indices shift,
   which is fine because attachments key on `m_number`, not index. Re-confirm that no consumer
   caches a battlefield index across the sweep.

**If any of 1–3 fails, this is a PLAY CHANGE, not a collapse** — and then it is subject to a
quality A/B rather than an identity gate, and almost certainly not worth it. Note that
`PerformDamageAllCreatures` documents the two-phase shape as the *more* rules-correct one
(CR 608.2, simultaneous deaths), and "destroy all tokens created with this enchantment" is likewise
simultaneous — so the bulk form may well be the better model. That is an argument for doing it
deliberately with an A/B, not for smuggling it in as a performance fix.

## 4. The alternative that needs no audit at all

Shrink `Permanent`. Every finding in this family — the haste scans, `DoublerShift`'s two field
loads, `OnCreatureDies`' two argument loads, and this memmove — has been *the same underlying
cost*: a 296-byte stride means one cache line per permanent to read one field, and `GameState.h:465`
already says so in as many words ("every permanent costs a fresh cache line to read one byte").

A smaller `Permanent` pays off on all of them at once and cannot change behaviour. It is also the
only option here that is risk-free by construction. `static_assert(sizeof(Permanent) == 296)` in
`Dominance.h` is the tripwire to update, and it exists precisely so this is a deliberate change.
**Measure the field sizes before picking between §3 and this** — if the struct can lose a third of
its bytes, that is a third off this memmove *and* a broad win on every rollout, with no soundness
argument to make.

## 5. Verification, whichever route

* The rollout above is the benchmark: `MTG_KEEP_REPLAY="Vitaspore Thallid (+1) x2; Utopia Mycon x1;
  Secluded Courtyard x1; Peat Bog (+1) x1; Doubling Season x1"`, `_R=20`, `_PD=0`, run in a
  directory holding the deck's `buckets.json` **and** `gencache.json` (without both, `policy_fp`
  misses and the probe burns cores re-running depth-5 discovery multithreaded). It is journal-safe:
  the replay block returns before `journal_path` is ever assigned. Current time: **259 s**,
  `win_turn=7`.
* Identity: play digest, scenarios, unit, and smoke's `play-changed: 0` / `configs changed: 0`.
  Per-decision `units` from `MTG_TURN_CENSUS` is the sharpest check — it is thread-local and
  therefore valid even on contended rows, and it caught the earlier fixes as exactly identical.
* **Do not price this on the straggler alone.** The precedent is explicit: a 1.53x on the worst
  cell was 1.05x on the job. Measure the aggregate with a fixed-window, same-odometer-region
  journal rate comparison, the method used for the haste/bulk-token rounds.

Related: `mulligan-rollout-performance-floor.md` (the case set and this straggler),
`keepgen-straggler-task-splitting.md` (what to do when a rollout is *still* slow),
`per-deck-folder-layout.md` (where the probe artifacts live).
