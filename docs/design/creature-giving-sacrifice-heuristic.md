# Crop Rotation sacrifice — search by default, heuristic narrows (deferred)

**Status:** deferred — implement **after** the current recommend/keep-gen floor run on
`decks/Creature Giving` completes. This changes in-play decision logic, so doing it mid-generation
would change `play_digest` and invalidate the in-flight raw chunk's pooling identity (generate on a
frozen commit; land it only once the run is banked).

## Design principle (revised 2026-08-11, user-directed)

Do **not** bake a deck-specific sacrifice heuristic that reasons about colours in hand/deck — that is
fragile ("truly general or not at all"), and the mana knowledge is exactly what the evaluation already
has. Instead follow the codebase's standard **search-primary, provider-narrows** bar (the same shape
the tutor axis uses):

- **Default: SEARCH the sacrifice.** Branch over the legal sac targets and let the rollout evaluation
  pick the best land to give up — no per-deck colour rules required. This is the general mechanism.
- **Optional: a provider heuristic may REPLACE the search** by returning a narrowed (or single-element)
  candidate list, exactly like `CreatureGivingProvider::TutorCandidates`' Orchard-first collapse. A deck
  supplies this only when the sacrifice is cleanly determined; otherwise the search decides.

It is a bit uglier than one clever ranking, but it is robust: correct-by-evaluation everywhere, with
narrowing as an opt-in speed/clarity lever rather than the primary decision-maker.

## What decision this is

Crop Rotation's additional cost is "**sacrifice a land**". In the rollout/executor path that choice is
made by `PerformSacrificeLandCost` (`src/core/SpellEffects.h:6069`), which takes `ranked.front()` from
the provider's `SacrificeLandCandidates` — a **single deterministic pick, not a search branch**. Which
land to sacrifice is therefore entirely a heuristic; it must stay one (no fan-out).

## Current state (the gap)

`CreatureGivingProvider` does **not** override `SacrificeLandCandidates`, so it inherits the base rule
(`DecisionProvider::SacrificeLandCandidates`, `src/ai/DecisionProvider.h:769` / impl `.cpp:720`):

> first **TAPPED** land (already spent this turn → no mana lost now), else the first land.

Tapped-first is a fine *tempo* tiebreak but it is **colour- and value-blind**: if Forbidden Orchard or
the Karoo bounce land (Azorius Chancery) happens to be the first tapped land, the base rule will
sacrifice it — which is clearly wrong.

## Work required

1. **Add a `sac_choice` search axis.** Mirror the tutor axis (`tutor_choice`, `TurnSolver.cpp`):
   collect the legal sac targets, fan out one plan variant per candidate, evaluate each in the rollout,
   and let the search pick — instead of `PerformSacrificeLandCost` forcing `ranked.front()`. Bound the
   width with a `SacSearchWidth` cap (mirror `TutorSearchWidth`), overridable via a `MTG_*` env.
2. **General default narrowing (hard filter, all decks).** Keep the candidate list small and sane
   before the search branches, using only *general* rules — no deck/colour knowledge:
   - never a **Karoo / bounce land** (`etb_bounce_land`; sacrificing it discards two lands of value);
   - the **just-fetched land** is already excluded by cost timing (paid before resolution, CR 601.2h).
   Retain **tapped-first** as the ordering tiebreak so the search's first/one branch stays sound.
3. **Optional provider narrowing (opt-in, per deck).** A provider MAY override the candidate hook to
   collapse the axis (e.g. return a single land) when the sacrifice is cleanly determined — the
   heuristic-replaces-search lever. For Creature Giving the *intuition* is: because we fetch Forbidden
   Orchard (rainbow), colour coverage is almost always intact, so a **redundant / duplicate** source is
   the sacrifice (a **green** source early), while **preserving a blue** source (the deck can need 2
   blue — verify against the blue-pip cards in `cards.json`). But only add this if it measurably beats
   letting the search decide; otherwise leave it to the search per the principle above.

## Cost tradeoff (this is not free)

Unlike today's single pick, searching the sac **adds branching** on plans that cast Crop Rotation
(width = legal sac targets after the hard filter). Crop Rotation is a small fraction of games and 1–2
casts each, so the cost is bounded — but it is a real add on big-board hands. The payoff is a
**correct-by-evaluation** sacrifice with no fragile per-deck code. Measure the throughput hit alongside
the win-quality gain.

## Validation & adoption

- Play changes → games are **not** byte-identical; this needs a ground-truth **rebaseline** after
  adoption (regression + smoke), not a byte-identity A/B.
- Treat as a heuristic-optimization change: A/B win% and avg-win-turn on the regression (train) seeds,
  validate on held-out (overnight) seeds, adopt in the **archetype provider** (`CreatureGivingProvider`),
  never the root. Report the measured deltas before adopting.
- Gate behind a standard `MTG_*` off-switch for the A/B (e.g. `MTG_CG_SAC_SMART`), read via `EnvOn`.
