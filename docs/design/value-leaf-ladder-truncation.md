# Value-leaf depth-table truncation — a latent-bug class (and its guard)

## The bug that motivated this (goblins, 2026-07 → fixed 2026-08-01)

Each value deck's `value_leaf_table` (in `<deck>.value.json`) records a by-depth
loss-penalized win-turn ladder for the heuristic (`heuristic_lp` over `hdepths`) and the
pure value leaf (`value_leaf_lp` over `vdepths`). From it the writer
(`scripts/attic/valueleaf_table_to_metadata.py`) derives `value_trust_depth` — the
shallowest value-leaf depth whose LP is within `tol` of the converged heuristic floor
`h_conv`. The engine reads it as the escalation gate: an unverified line committed **below**
`value_trust_depth` is escalated to the exact heuristic leaf; at/above it the leaf is trusted.

Goblins shipped with the ladder measured only to **V5** while its play `target_depth` is
**6**, and the shallow cells were noisy (400 games). The leaf had not yet reached `tol` at
V5, so `value_trust_depth` derived as **UNSET**. With it unset, `AIEngine` defaults
`escalate_below = target_depth + 1 = 7`, so **every** unverified line committed at the play
depth 6 escalated to a deep heuristic re-search whose result the fallback crossover's
keep-leaf sentinel then **discarded** — pure wasted work (measured: `d6 redid 38` of 72
escalations per 150 games; play −23% wall once fixed). The real V6/V7/V8 cells *had* been
measured (in separate high-N logs), but the writer was hand-fed the stale partial log and
never re-run on the union.

**Two structural causes, both now closed:**

1. **Measure and write were separate manual steps that drifted.** The depth matrix
   (`valueleaf_depth_matrix.py`) wrote a gitignored scratch log; a human later hand-ran the
   metadata writer on *some* log. Fix: `valueleaf_depth_matrix.py --write-profile` folds the
   run's freshly-measured table straight into the play profile in one atomic command.

2. **Nothing enforced ladder completeness.** The writer emitted whatever cells the log
   held. Fix: a completeness guard (`completeness_error`) **refuses to write** (exit 2) when
   the ladder is inconclusive. `--allow-partial` downgrades to a warning for a deck whose
   UNSET trust is deliberate. The scalar cap now also **auto-derives** from
   `value_play.target_depth` (was a fixed default of 5, which silently under-capped
   `target_depth=6` decks).

The guard's discriminator is **coverage of the in-play depth range**, i.e. depths
`<= scalar_cap == target_depth` — the only depths the escalation gate reads. A table is
conclusive when that range is fully measured:

- `trust_depth` SET ⇒ conclusive (leaf trusted from that depth).
- `no_fallback` True ⇒ conclusive (leaf provably worse everywhere ⇒ always escalate-and-take).
- `trust` UNSET but `max(vdepths) >= target_depth` ⇒ **conclusive UNSET**: the leaf at the
  play depth is genuinely `> tol` worse than the heuristic, so escalate-and-**take** is
  correct. Whether the leaf keeps descending at OUT-OF-RANGE depths (`d > target_depth`) is
  irrelevant — it can't move the in-play gate.

It refuses ONLY when `trust` UNSET *and* not `no_fallback` *and* `max(vdepths) < target_depth`
— the ladder stops **below** the play depth, so an unmeasured in-range cell could still cross
`tol`. That is exactly goblins-pre (V5, target 6: V6 was in-range and unmeasured).

> An earlier version of this guard also fired on a merely *still-descending* deepest cell
> that sat AT the cap. That was a **false positive**: it flagged hinata2/dragonstorm
> (measured to V5 == target 5), whose UNSET is correct. Corrected to the coverage rule above.

## Are other decks affected? — no confirmed bug; goblins was unique

The whole point of the escalation gate is whether the leaf reaches `tol` of `h_conv` **within
the in-play depth range** (`<= target_depth`). Goblins was the only deck whose ladder stopped
BELOW that range (V5, target 6) *and* whose crossover discarded the escalations it triggered.
Every other value deck is conclusive:

| deck | target | max V | trust | `take@target` | verdict |
|------|--------|-------|-------|---------------|---------|
| goblins (fixed) | 6 | 8 | **6** | 4 (take) | trust set — no escalation at play depth |
| hinata2 | 5 | **5** | UNSET | **2 (take)** | escalate-and-**take** — UNSET is correct |
| dragonstorm | 5 | **5** | UNSET | **3 (take)** | escalate-and-**take** — UNSET is correct |
| antilife | 5 | 7 | UNSET | 3 (take) | leaf far worse; escalate-and-take |
| TH | 5 | 8 | UNSET | 3 (take) | leaf converges out-of-range; escalate-and-take |
| slivers / knights / auras | 5 | 5–8 | 5 (set) | — | trust set |
| burn | 6 | 8 | 6 (set) | — | trust set |

The critical contrast is **`take@target`**: goblins-pre carried a keep-leaf sentinel (`9`) at
its target depth, so its escalations were run **and discarded** (pure waste — the −23% we
recovered). hinata2/dragonstorm have a LOW `take@target` (2–3), so their escalations are
**taken** — productive work buying the ~0.006–0.009 LP by which the heuristic beats the leaf at
depth 5. Their UNSET is the correct config, not a latent goblins bug.

Two genuinely-open (minor) items, neither a correctness bug:

- **Sampling confidence.** hinata2's table is 200 games, dragonstorm's 400 (goblins is now
  3000). The ~0.006–0.009 leaf-vs-heuristic gap at the play depth is within plausible noise. If
  we ever want to *firm up* that these should stay UNSET (vs. the leaf actually being at parity,
  which would let us drop the escalations for a speed win), re-measure at high N via
  `valueleaf_depth_matrix.py --decks hinata --vdepths 1 2 3 4 5 --games 3000 --seeds <held-out>
  --write-profile` (the guard passes either way; only the numbers firm up). Precision, not a fix.
- **No-op escalations.** For combo decks most escalations re-search to the *same* win turn (a
  known ~82% on hinata). Skipping those predicted no-ops is the **escalation confidence-gate**
  (`MTG_ESCALATION_GATE`, `docs/design/escalation-and-rollout-cost.md`) — a separate speed lever,
  unrelated to the table/trust question here.

## CORRECTION 2026-08-02: the tables were NOT measured profile-less

`3668b6b` ("the depth matrix never passed `--profile` -- every value-leaf depth table in this repo
was measured on a deck with NO profile") is **factually wrong**, and the conclusion drawn from it --
that every table needs regenerating for this reason -- does not follow. Three independent checks:

1. **The engine auto-detects the profile from the deck path.** `src/main.cpp`:
   ```cpp
   // Auto-detect deckname.profile.json if no explicit --profile was given.
   if (profile_path.empty())
   { profile_path = deck_path.parent_path() / (deck_path.stem().string() + ".profile.json"); }
   ```
   In place since `e71f51f` (2026-06-03) -- months before any of these tables were built. The
   per-deck folder layout (`decks/<name>/<name>.profile.json` beside `decks/<name>/<name>.cod`)
   guarantees the derived path exists for every deck.
2. **The new `PROFILES` map is byte-identical to what auto-resolution already picks** -- all eight
   entries, same paths. The fix threads through a value the engine was already deriving itself.
3. **Measured**: same deck/seed/games with and without `--profile` gives the same avg (4.1533) and
   the same unwon game, and BOTH print `Loaded profile from decks/Auras/Auras.profile.json`.
   Verified fleet-general (Hinata2, slivers_vial, Anti-Lifegain all auto-load).

The commit is harmless -- passing the flag explicitly resolves to the same file -- but do **not**
regenerate a table on this rationale, and do not attribute table drift to it. If Hinata's numbers
really moved when regenerated, the cause is elsewhere (the engine moving underneath it, or its
exhaustive keep model landing between the two runs) and is worth finding rather than mis-attributing.

**The real invalidator is the engine.** A table is only comparable to the engine it was measured on;
the 2026-08-02 searched-decisions merge changed ~3,900 lines across TurnSolver / DecisionProviders /
AIEngine / SpellEffects, so every table now predates it -- including goblins' `70515df` regeneration
(the source of `value_trust_depth: 6` and `hc*[6] = 4`). That, not `--profile`, is the reason to
re-measure. And when re-measuring, re-anchor an existing cell rather than appending: mixing engine
states inside one table is the antilife defect documented above.
