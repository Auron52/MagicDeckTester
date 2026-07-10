# NC search: land-SELECTION (colour/curve enabling) — deferred lever

**Status:** deferred follow-up. Identified 2026-07-10 while fixing the NC land-*drop* tempo bug (see
`learned-d0-policy.md`, "NC SEARCH QUALITY" sections + `DecisionProvider::NcLandDropTempoBonus`, Hook 22). This note
is the standalone record so the issue is not lost.

## The problem (gi9)

The non-clairvoyant reshuffle-averaged search (`TurnSolver::ReshuffleAvgChoosePlan`, behind `MTG_NC_SEARCH`) already
has a land-*drop* tempo bonus that makes it develop mana on curve. But it still picks the **wrong land** when colour
matters.

Reference game Anti-Lifegain `claude_s10_gi9` (reproduce: `--seed 10 --game-index 9 --games 1 --force-mulligan
"1:51"`), NC vs clairvoyant on the identical hand:

- **Clairvoyant / human:** T1 play **Windswept Heath** (fetches a green source) → cast **Ignoble Hierarch** (a `{G}`
  mana dork) T1 → ramp → win **T4**.
- **NC (even with the tempo bonus on):** T1 play **Godless Shrine** — a `{W}{B}` shockland that **cannot** produce
  green — so it can't pay `{G}` for Ignoble Hierarch, stalls the dork to T3, and wins **T7**.

So NC makes *a* land drop (tempo bonus satisfied) but the **wrong-coloured** one: it doesn't pick the land that
enables its turn-1 play. Card facts (from `src/cards/data/cards.json`): Ignoble Hierarch `{G}`; Godless Shrine
produces `W,B`; Windswept Heath can get `G`.

## Why the averaging misses it (same root as the land-drop bug)

The reshuffle averaging shuffles the true library away, so across the sampled futures **colour access averages out**
(you'll draw other sources on average). The search therefore sees no win-turn benefit from the specific T1 colour fix,
even though in the *actual* (colour-light) game it unlocks the whole ramp. This is the land-*selection* analogue of the
mana-optimism that made it skip land drops — but on the axis of *which* land, not *whether* to play one. It is
**tempo-bonus-invariant** (the bonus rewards any land drop equally), which is why gi9 did not move under any tempo
value in the sweeps.

## Proposed lever (when revisited)

A curve/colour-coverage heuristic: among land-drop plans that tie (or nearly tie) on the averaged objective, prefer
the land that **maximises what you can cast this turn / this-and-next turn** — i.e. the colour that enables an
otherwise-uncastable on-curve play (here, green for the T1 dork). Natural home is the **provider layer**, alongside the
existing `NcLandDropTempoBonus` (Hook 22) and `PreferHoldLandDrop` — e.g. a `ChooseTempoLand` / land-value hook that
the archetype provider can inform (Anti-Lifegain: green-source-first to power dorks/enablers, mirroring its existing
green-top `FetchCandidates`). Keep it a tie/near-tie breaker so it never overrides a real win-turn difference, and
validate with the same tooling: `scripts/ref_bench.py` (per-game vs references) then `scripts/nc_tempo_bigsweep.py`
(broad autonomous, no-regression gate — especially confirm it doesn't distort land-pitch decks).

## Scope / expectations

Same as the tempo bonus: real but **modest on random games**, larger on the concentrated hand-played reference
blunders. gi9 alone is +3 turns; how often the colour-block recurs in random play is unmeasured. Measure before
investing — it may be a small residual best left until models are explored.
