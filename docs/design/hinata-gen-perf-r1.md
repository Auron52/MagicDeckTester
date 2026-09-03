# Hinata mulligan-gen performance — value.json (partial) + combo-line cost (R=1 diagnosis)

**Status:** deferred investigation, not being worked on. Captured so the secondary machine doesn't burn
another fortnight. Read alongside `docs/design/hinata-profile-generation.md` (the frozen chunked recipe),
`.claude/skills/mulligan-profile.md` (authoritative protocol), and `docs/design/learned-d0-policy.md`
(the value-leaf model).

## The problem

The secondary box estimates the Hinata2 exhaustive keep+bottom profile at **R=40 ≈ 2+ weeks** — worse than
the recipe's own "5–9 day" figure and clearly not survivable to repeat on play-logic changes.

## Root cause: Hinata is the ONLY tabled deck with no `value.json`

**(Updated 2026-09-03: the premise is gone — `decks/Hinata2/Hinata2.value.json` shipped at
490745c5, 2026-07-14, and the R=22 gen completed and was adopted at 22947513, 2026-07-25.)**

The 2-week number is a **per-rollout rate** problem, not a hand-count problem:
- Hinata: K=20 → ~700k cells (7/6/5/4), comparable to antilife's 520k. Hand count is fine.
- Rate: **~40 rollouts/s on 24 threads** (d3/b10, 0.59 s/game single-thread) vs antilife's ~275/s — Hinata
  is **~7× slower per rollout**. At R=40: ~700k × 2pd × 40 ≈ 56M rollouts ÷ 40/s ≈ **~16 days**.

The 7× gap is because every full-depth search leaf in the rollout falls back to the slow `SimulateToEnd`
horizon rollout — because **Hinata ships no `decks/Hinata2.value.json`**. All five other tabled decks
(antilife, burn, knights, slivers, treasure_hunt) got a value model on 2026-07-11; Hinata was skipped.

The engine already resolved the design question here. `AIEngine::PlayOut`'s rollout setup
([src/ai/AIEngine.cpp](../../src/ai/AIEngine.cpp) ~846-855) attaches the deck's `value_model` to the
rollout state specifically so the keep/bottom generator plays like the shipped deck; the comment states
that NOT doing so is both "the dominant cost of the exhaustive-keep generator (measured: the value leaf was
100% inert)" **and** "a ~train/serve MISMATCH (the gen played a value-less policy the deck never serves)."
A deck WITH a value.json already gets the O(1) value leaf in generation; Hinata, without one, pays the full
horizon rollout on every leaf.

**But value-leaf is only a PARTIAL fix for Hinata (do not over-count it).** The value leaf gives the full
O(1) speedup only for **verified-win-dominated** decks, which set `value_trust_depth=5` (knights, slivers —
which is exactly why those are fast at d5). Hinata is a **combo** deck: its combo lines are not
verified-win-dominated, so the hybrid value-leaf ESCALATES those uncertain lines to real search
([AIEngine.cpp](../../src/ai/AIEngine.cpp) ~1398). The heuristic / full-rollout fallback therefore still runs
on precisely the expensive combo lines that dominate Hinata's cost. Expect value-leaf to help "somewhat"
(the cheaply-decidable non-combo leaves) but NOT to close the 7× gap. The real bottleneck is the
**combo-line search cost itself** (plan enumeration + `GameState` deep-copy), which is a separate axis from
value-leaf.

## The confound question (and why train/serve consistency dissolves most of it)

Concern: using value-leaf to generate the mulligan profile evaluates *potential mulligan hands* — including
bad/atypical hands a mulligan policy would toss — which are under-represented in the value model's training
data (Hinata has no profile, so training games use default mulligans). Doesn't that confound the result?

Reframing via the engine's own train/serve rule:
- The mulligan profile's job is "given how this deck **actually plays**, which hands do I keep?" If the
  shipped Hinata plays with value-leaf, then generating the profile **with** value-leaf is *correct*, not
  confounded — if value-leaf plays a hand weakly, the profile should (correctly) mulligan it more.
- The genuinely confounded option is the CURRENT one: generating with `SimulateToEnd`, a full-rollout policy
  Hinata will never serve. So "value-leaf → worse" is only worse vs an idealized full-rollout Hinata that
  does not exist; vs the real (value-leaf) Hinata it is right.

The residual, real distribution-shift is one level up: the `value.json` is trained on games that (with no
profile) use **default mulligans**, so it under-sees the bad hands the gen probes. But:
1. That training distribution **matches current shipped play** (Hinata also uses default mulligans today),
   so the first profile is consistent *now*.
2. Once a profile ships, play shifts → retrain value.json → regenerate. This is a proper **bootstrap** and
   each round is train/serve consistent, so it converges.
3. It is less "worse" than it feels: clearly-bad hands (e.g. 7 lands) get mulliganed regardless of model
   error, so the confound concentrates on the *marginal* cells — and the hybrid value-leaf already escalates
   uncertain lines to real search ([AIEngine.cpp](../../src/ai/AIEngine.cpp) ~1398) instead of blindly
   trusting the model.

## The path: value.json-first, then bootstrap (NOT mulligan-first)

Doing the mulligan profile "first" to get a good hand distribution is circular — fast mull-gen *needs*
value-leaf. Break the circle:

1. **Train a Hinata `value.json`** — the actual missing piece. Play a batch of full-search Hinata games with
   `MTG_DUMP_VALUE_ROWS=<file>` (label = the deep-search win turn from each position, see
   [AIEngine.cpp](../../src/ai/AIEngine.cpp) ~66-133) and fit the ~40 KB model, exactly as the other five
   decks were done on 2026-07-11. Cost is a few hundred–thousand games (hours), orders of magnitude below
   the 56M-rollout mull-gen. Install at `decks/Hinata2.value.json`; set `value_trust_depth` per the
   learned-d0-policy method (combo decks likely want the hybrid escalation, not full trust).
2. **Generate the mulligan profile WITH value-leaf** — now fast (the ~7× rollout speedup) and train/serve
   consistent. Follow the frozen chunked recipe otherwise.
3. **Bootstrap once more (optional)** — retrain value.json on the profile-shifted play distribution, then
   regenerate the profile. Do this only if a check shows it moves keeps.

## R=1 diagnostic (for the SECONDARY machine — the whole task is handed to it)

The recipe's estimate is what's overrunning; measure, don't assume. Order:
1. **Profile one rollout FIRST** (perf sampling) to locate the real hotspot: combo-line plan enumeration
   (Magma Opus / Crackle with Power / Reality Spasm lines) vs `GameState` deep-copy/node vs the search
   recursion. This decides whether value-leaf even helps materially before investing in it.
2. **value.json actual speedup, not assumed:** train a Hinata `value.json` (cost = a few hundred–thousand
   full-search games, hours), then measure the value-leaf **escalation rate** and the resulting rollout
   rate vs the recipe's 40/s. If most combo lines escalate, the jump is small — quantify it.
3. **R=1 (or floor-only) chunk timing** → honest extrapolation to R=30/40, with and without value.json.
4. **Confound magnitude, cheaply**: on a small cell subset, A/B value-leaf-gen vs `SimulateToEnd`-gen keep
   decisions; count marginal-hand flips. If few, the value-leaf first pass is fine (train/serve consistent).

## Levers, by likely impact (the combo cost is primary, not value-leaf)

- **Combo-line over-generation** — the primary lever. Fewer enumerated combo plans → cheaper search on
  every rollout. See `docs/design/crackle-reality-spasm-overgeneration.md`,
  `crackle-hinata-declared-targets.md`, `spectacle-and-invigorate-combo-enumeration.md`.
- **`GameState` deep-copy/node** — the per-node cost the search pays regardless of value-leaf (arena /
  shrink-Permanent). Larger engine project but hits the dominant term.
- **value.json** — partial speedup (non-combo leaves) + train/serve consistency; worth doing but not the
  silver bullet (see the escalation caveat above).
- **K reduction:** force-merge {Preordain, Ponder} (and possibly basics) if a fidelity check says they are
  rollout-equivalent → ~25–40% fewer hands.
- **R target:** keep plateaus ~R40; R30 ≈ 95%, R20 ≈ 90%. R30 is −25% cost for ~5% quality.
- **Pruning/adaptive discipline:** the junk-hand prune-set (skip confident mulls after chunk 1) is the
  biggest floor saver; confirm the chunks carry it (`MTG_KEEP_PRUNE_SET` + `MTG_KEEP_CARRY_MODE=skip`).

## Bottom line

value-leaf does not *confound* the Hinata mulligan profile — it is the train/serve-consistent policy, and its
only real approximation (a value model trained on the default-mulligan distribution) is bootstrap-improvable.
But it is **not** the performance silver bullet: Hinata's combo lines escalate past the value leaf to real
search, so the dominant cost is combo-line enumeration + deep-copy. Train the missing `Hinata2.value.json` for
consistency and the partial win, but expect the big performance gains to come from reducing combo-line
over-generation and the per-node deep-copy — the secondary should profile a rollout first to confirm where the
time actually goes.
