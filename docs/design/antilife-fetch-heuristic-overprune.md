# Anti-Lifegain fetch heuristic mis-values colour multiplicity (0a1172d)

**Status:** FIXED (2026-07-09) via a ranking fix; smoke+regression validated,
overnight rebaseline pending. History kept below. Self-contained.

**TL;DR (root cause):** `AntiLifegainProvider::FetchCandidates` (added in commit
**0a1172d**) returns a **single** fetch target ranked by colour coverage. Its
`dup_pref` cosmetic tiebreak (Green > Black > White > Red) is a LOW-priority
tie-break that was allowed to override a **genuine colour-multiplicity need**, so
on the flagged games it fetched a redundant green/red dual instead of a
**needed second white source** (Fiery Justice `{W}` + Swords `{W}` want white
twice; one white land is not enough). It also double-secured black by fetching it
when the **in-hand Ignoble Hierarch dork about to be cast already makes black**.
Because it prunes to one candidate the search couldn't override it, and
`MTG_UNPRUNED=1` recovered the faster win on every one — a real heuristic bug, not
variance (the framing was originally "over-prunes a strictly-better target"; the
precise cause is the colour-multiplicity mis-valuation described here).

**The fix (ranking, no search fallback):** two additions to `FetchCandidates`,
antilife-only, keeping the single-pick design:
1. Count **in-hand mana dorks/rocks** toward the per-colour source counts (so
   black is seen as secured when the Hierarch about to resolve makes it —
   `n_black_src` now includes the in-hand dork).
2. New coverage key **`s_short`** — a candidate produces a colour we want *more
   times this turn* than we have sources for (`demand_turn[c] > src_cnt[c]`) —
   ranked **above** the cosmetic `dup_pref` but below the distinct-colour keys, so
   the needed 2nd white beats a redundant green/red dual while genuine ties still
   fall to `dup_pref`.

Result: all three target games win their GT turn **at default** (no
`MTG_UNPRUNED`, d3+d5); **default == unpruned** on the whole antilife smoke d5 set
(the byte-identical bar holds — the heuristic strands nothing); every non-antilife
deck byte-identical; antilife searched depths net-faster/neutral with **0
win→loss**; the remaining searched turn-later games are fetch-shuffle variance
(draws diverge). Code: `src/ai/DecisionProviders.cpp::AntiLifegainProvider::FetchCandidates`.

This was **upstream** work, independent of the committed Land's Edge fix
(`c8c4a53`); the Land's Edge change itself is clean (TH-only, 0 regressions).

## How it was found (the whole-set audit)

The stale overnight GT was re-run on the current binary and showed net-positive
drift, but with 256 antilife searched **slowdowns** (games that still win, one
turn later). To be sure they were all benign, every one was classified by diffing
the **draw sequence** of the pre-drift binary (commit `6d3ce16`, matches the GT)
vs HEAD:

- **DIVERGE (245):** the coverage-aware fetch grabs a *different* land, and under
  the engine's **no-shuffle fetch model** that reorders the library, so the
  **draws differ → a physically different game**. Benign variance (net-positive:
  the deck just draws a different sequence). This is the expected, documented
  effect (0a1172d's message: "fetch-shuffle variance").
- **SAME_DRAWS (11 rows = 6 distinct games):** identical draws, played a turn
  slower. Of these:
  - **1 game (s6006 gi571):** recovers at `d7`/16× budget → benign shallow-search
    truncation.
  - **5 games (below):** the bug. Stable at `d7`/16× budget, `MTG_DORK_RAMP=0`
    does **not** recover them (so it is *not* the dork-ramp change f197730), but
    **`MTG_UNPRUNED=1` recovers every one** → 0a1172d's fetch pick is the culprit.

Full per-game classification: `logs/antilife_fetch_bug/CLASSIFICATION_256.txt`
(lines are `DIVERGE | SAME_DRAWS`, with seed/gi/GT/run).

## The problem games (user-triaged: 3 real, 2 variance)

Deck `decks/Anti-Lifegain.cod`, profile `decks/Anti-Lifegain.profile.json`.
Per-game seed = `base_seed + gi` (so `--seed <base+gi> --game-index <gi> --games 1`).
On inspection the user triaged the original five: the **first three are real fetch
mis-valuations** (fixed by the ranking change above — all now win their GT turn at
default); the **last two are genuine draw divergences** (variance, not fixed —
their `MTG_UNPRUNED` "recovery" was a different physical game, left as-is).

| case | gi | seed | GT | pre-fix | post-fix (default) | verdict |
|------|----|------|----|---------|--------------------|---------|
| s4004 d3/d5 | 235 | 4239 | T3 | T4 | **T3** | FIXED (2nd white) |
| s4004 d3/d5 | 422 | 4426 | T4 | T5 | **T4** | FIXED (dork-black / white) |
| s6006 d3/d5 | 6   | 6012 | T3 | T4 | **T3** | FIXED (2nd white) |
| s6006 d3/d5 | 355 | 6361 | T4 | T5 | T5 | variance (draws diverge) |
| s6006 d3/d5 | 395 | 6401 | T4 | T5 | T5 | variance (draws diverge) |

(Each reproduces at both d3 and d5. `MTG_DORK_RAMP=0` leaves them slow; `d7`
+16× budget leaves them slow; only fetch-unpruned recovers.)

### Reproduce one (default slow vs unpruned fast)

```bash
# default (heuristic fetch) -> T4
MTG_DUMP_WINS=1 ./build/Release/mtg decks/Anti-Lifegain.cod \
  --profile decks/Anti-Lifegain.profile.json --games 1 --seed 4239 --game-index 235 \
  --depth 5 --budget-ms 20 --threads 1 --lookahead-bottoming 2>&1 | grep '^\[win\]'

# fetch unpruned (search all targets) -> T3
MTG_UNPRUNED=1 MTG_DUMP_WINS=1 ./build/Release/mtg decks/Anti-Lifegain.cod \
  --profile decks/Anti-Lifegain.profile.json --games 1 --seed 4239 --game-index 235 \
  --depth 5 --budget-ms 160 --threads 1 --lookahead-bottoming 2>&1 | grep '^\[win\]'
```

## Look at the games (for a human)

Pre-generated per-game logs are in `logs/antilife_fetch_bug/` (gitignored but on
disk; regenerable with the commands below):

- `s<base>_gi<N>_default.json`  — the slow line (heuristic fetch).
- `s<base>_gi<N>_unpruned.json` — the fast line (search picks the fetch).

**View them:** open `tools/replay/index.html` in a browser (no server; `file://`
works) and **drag a `.json` onto the page**. Step through with ←/→. Compare the
`default` (slow) vs `unpruned` (fast) log of the same game to see where the
heuristic's fetch cost a turn. Regenerate any log with `--log-dir <dir>` on the
repro commands above.

### The line diffs (default → unpruned)

Draws are identical between the two; only the fetched land / mana sequencing
differs, delaying a payoff a turn. Examples:

```
s4004 gi235:  default T4:  T1 Forest+Ignoble Hierarch; T2 Marsh Flats + Fiery Justice;
                            T3 Windswept Heath + Tainted Remedy + Swords + Skyshroud Cutter; T4 Aria
              unpruned T3: T1 Forest+Ignoble Hierarch; T2 Marsh Flats + Tainted Remedy + Skyshroud Cutter;
                            T3 Windswept Heath + Fiery Justice + Swords   (kills a turn earlier)

s6006 gi6:    default T4:  ...T4 Wooded Foothills + Plague Drone + Swords + Reverent Silence
              unpruned T3: ...T3 Windswept Heath + Fiery Justice + Swords + Reverent Silence
```

(Full lines for all 5 are recoverable by re-running with `--log-dir` or reading
the committed logs in the replay viewer.)

## Fix direction (for the next session — measure, don't assume)

The rule this breaks: a pruning heuristic may only drop options that are
**equal-or-worse**; it must never hide a strictly-better line from the search
(`MTG_UNPRUNED` byte-identical bar, see `search-primary-architecture`). Today
`AntiLifegainProvider::FetchCandidates` returns exactly one target.

Candidate fixes:

1. **Offer more than one candidate when the choice isn't dominant.** The search
   already branches over fetch candidates (`EnumeratePlansWithLand` emits one
   land-variant per returned candidate, capped by `FetchSearchCap`). Returning the
   top *k* coverage-ranked targets (instead of 1) lets the search pick the
   turn-earlier line while still pruning the clearly-irrelevant targets. Cheapest;
   check `FetchSearchCap` allows ≥2.
2. **Fix the ranking** so it stops demoting the target these games need — but the
   5 games disagree on which colour, so a static rank is unlikely to catch all;
   (1) is more robust.

**Validation checklist (before adopting):**
- Each of the 5 games wins its GT turn at default (no `MTG_UNPRUNED`), d3 and d5.
- `MTG_UNPRUNED=1` becomes **byte-identical** to default on antilife (the bar).
- Re-run the whole-set classifier: **0** SAME_DRAWS games remain non-recoverable.
- Suite A/B: antilife smoke+regression — confirm no new regressions; other decks
  byte-identical. Then re-run the overnight and rebaseline (see below).

### Reproduce the whole-set classification

`logs/antilife_fetch_bug/classify_one_game.sh` runs old-vs-new draw diff for one
game; drive it over all slowdowns. To rebuild the "old" (pre-drift) binary:
`git worktree add --detach /tmp/wt_base 6d3ce16 && cmake -S /tmp/wt_base -B
/tmp/wt_base/build -DCMAKE_BUILD_TYPE=Release -G Ninja && cmake --build
/tmp/wt_base/build` (binary lands at `/tmp/wt_base/build/mtg`, not
`build/Release/`).

## Overnight rebaseline: gated on this

The overnight audit otherwise passed: net **+76** searched (355 improve / 279
regress), **8 new wins vs 3 win→loss** (all 3 Hinata, budget/depth-recoverable),
`nonconv=0`. The drift traces to two intentional, regression-validated commits —
**f197730** (`credit mana-dork ramp in rollout eval`, deck-agnostic → Hinata) and
**0a1172d** (this fetch heuristic → antilife). Once the 5 fetch over-prune games
are fixed and re-verified, re-run `bash test/regression.sh --overnight` and
`--accept` (acknowledge the 3 Hinata win→loss as budget/depth-edge). Do NOT
`--accept` the overnight while these 5 non-recoverable regressions stand.

## Perf caveat discovered here

The **changed** antilife games are the pathological deep tail: a single one at d5
+ lookahead-bottoming peaks at **~3.5 GB RSS and ~44 s** (deep-copy + interior-node
memo tables, amplified by lookahead-bottoming rolling out whole games per mulligan
decision). Running many concurrently blows the 32 GB WSL cap → OOM kills. Cap
parallel antilife sims at **≤6**. Worth a separate perf pass (bound the memo
tables / see `search-perf-investigation`, `per-deck-profiling`).
