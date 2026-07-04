# Burn — Searing Blaze no-landfall (cast-for-1) audit

Games to double-check: Searing Blaze cast **without landfall** (1 damage instead of 3) even though
a land was available on an adjacent turn. Generated against the **threshold-3** land-banking code
(commit `fd5e768`), sample = `logs/burn/m500.json` (deck `decks/burn.txt`, **seed 2002, 500 games,
depth 5, budget 0/unbounded**).

## Reproduce one game (faithful single game)

```
build/Release/mtg decks/burn.txt --profile decks/burn.profile.json \
  --games 1 --seed <SEED> --game-index <GI> --depth 5 --budget-ms 0 --log-dir /tmp/g
# or a full decision trace:
build/Release/mtg --batch logs/burn/m500.json --game-trace-dir <dir>   # then read a_gi<GI>.json
```
(SEED column below already = 2002+GI, the explain_game.py convention.)

## The 23 no-landfall Blaze casts (20 avoidable, 3 land-screwed)

| gi | blaze turn | win turn | seed | class |
|----|-----------|----------|------|-------|
| 47  | T4 | T4 | 2049 | avoidable (land prev) |
| 59  | T4 | T4 | 2061 | avoidable (land prev) |
| 76  | T4 | T4 | 2078 | avoidable (land prev) |
| 85  | T4 | T5 | 2087 | avoidable (land prev) |
| 142 | T4 | T4 | 2144 | land-screwed |
| 146 | T5 | T5 | 2148 | avoidable (land prev) |
| 165 | T3 | T5 | 2167 | avoidable (land prev) |
| 172 | T3 | T5 | 2174 | avoidable (land prev) |
| 188 | T5 | T5 | 2190 | land-screwed |
| 255 | T3 | T4 | 2257 | avoidable (land prev) |
| 258 | T3 | T4 | 2260 | avoidable (land prev) |
| 262 | T4 | T5 | 2264 | avoidable (land prev) |
| 293 | T4 | T4 | 2295 | avoidable (land prev) |
| 349 | T4 | T4 | 2351 | avoidable (land prev) |
| 353 | T4 | T4 | 2355 | avoidable (land prev) |
| 355 | T4 | T4 | 2357 | avoidable (land prev) |
| 392 | T4 | T4 | 2394 | avoidable (land prev) |
| 429 | T4 | T4 | 2431 | avoidable (land prev) |
| 436 | T5 | T6 | 2438 | avoidable (land next) |
| 438 | T4 | T4 | 2440 | avoidable (land prev) |
| 467 | T5 | T5 | 2469 | avoidable (land prev) |
| 468 | T5 | T5 | 2470 | land-screwed |
| 475 | T4 | T5 | 2477 | avoidable (land prev) |

"avoidable (land prev)" = a land was played the turn **before** the Blaze (it could have been deferred
to the Blaze turn for landfall). "land-screwed" = no land available around the Blaze — leave at 1
(per the design decision, we don't chase these).

## Interpretation / open lever

Land-banking (threshold ≥3, `BurnProvider::PreferHoldLandDrop`) holds the spare land, but in these ~20
avoidable cases the search **still** casts Blaze on a landless turn — because it spent the land drop +
mana on a *different* noncreature spell (Bolt / Light Up / Skullcrack) that turn and cast Blaze later
with no land. The remaining lever (discussed, **not** implemented) is a **cast-priority tiebreak**:
on a land-drop turn, among equal-value plans, prefer casting the **landfall** spell (Blaze) over an
equal-value non-landfall noncreature spell (which doesn't care about landfall and can go on a landless
turn). **Below creatures** (the clock > +2). It's a cross-turn plan-selection tiebreak, equal-value
only, so it never delays development or holds a land. Most of these win on the same turn anyway (+2
rarely moves the integer kill turn) — gi=436 (T6, would be T5 with landfall) and the T3-cast /
late-win ones (165, 172, 262, 475, 85) are the best candidates where the +2 might actually matter.

Do NOT re-derive the land-hold heuristic here — that path is settled (bank at ≥3, no speculative
holds). This audit is specifically for the cast-priority lever.

## RESULT (2026-07-04) — cast-priority tiebreak BUILT, MEASURED, REJECTED

Implemented the cast-priority tiebreak as scaffolding (`MTG_BLAZE_CASTPRIO`, uncommitted): in
`EnumeratePlansWithLand`'s plan comparator, among EQUAL wins AND EQUAL value, prefer the plan that
**realises landfall this turn** (plays a land AND casts a `landfall_damage` spell) over one casting
only landfall-agnostic spells. A/B on the same 500-game unbounded (seed 2002, d5, budget 0) sample:

- Aggregate **byte-identical**: both arms `won=499/500, avg=4.32866`.
- Per-game: **9 games changed line** (digest differs: gi 47, 59, 293, 337, 353, 355, 392, 429, 475
  — Blaze now cast for 3 with landfall instead of 1), **0 games changed win turn**, 0 regressions.

**Why zero win-turn effect:** the two failure modes the audit table lists both defeat this lever —
(1) on the Blaze-cast turn there is usually **no land in hand at all** (e.g. gi=172: both Mountains
played T1/T2, Blaze cast T3 landless), so no land is playable to realise landfall; and (2) the
prior-turn alternative (cast Blaze-with-landfall vs a different 2-drop) is a **value** decision, not
an equal-value tie, and the game wins on the same integer turn either way. The +2 face never crosses
a lethal threshold in the goldfish. gi=436 (the best "land next" candidate) is likewise unfixable —
its land arrives the turn *after* Blaze, so no lever that acts on the Blaze turn can help.

**Decision: REJECT for shipping** (net-neutral on win turn per the heuristic-optimization skill's
"measured-safe-but-marginal is a reject" rule). The scaffolding was reverted; the tree is clean.
**Nuance flagged for the user:** the change is strictly-better-or-neutral and makes the deck play
*more correctly* (bigger Blaze in 9/500 games) — irrelevant to the goldfish win-turn metric, but real
reach against a live opponent and in the benchmark reference JSONs. If that correctness is wanted for
its own sake, it can ship gated behind `BurnProvider` (0 regressions) — a veto call, not a measured win.
