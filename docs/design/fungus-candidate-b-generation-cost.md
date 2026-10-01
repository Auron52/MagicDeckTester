# Fungus candidate-B: what makes its MULLIGAN GENERATION cheaper (measured 2026-10-01)

Self-contained, for whoever is running the candidate-B mulligan generation. Everything here was
measured on **`decks/Fungus/candidate-b-2026-09/`** -- the modified list (adds Sol Ring + 4 Mycoloth,
drops Thallid Shell-Dweller / Sporesower Thallid / Thallid / Doubling Season / Beastmaster Ascension)
-- and **at its own generation cell**, which its sidecar pins at `mull_gen_depth=1`,
`mull_gen_budget_ms=3` (`expected_buckets=22`).

**Why the cell matters more than anything else here.** Generation cost is (number of trial games) x
(cost of one d1/b3 game): `AIEngine::RolloutWinTurnFrom` plays a WHOLE trial game per candidate
mulligan/bottoming subset -- up to 39 after name dedupe -- so the only thing that shortens a
generation is making a **d1/b3 game** cheaper. Numbers taken at the suite's d3/b10 or d5/b20 cells do
NOT transfer: the search is shallower here and several levers that pay at d5 are near-inert at d1.
Baseline measured cost of one game at this cell: **~1.0 s**.

## The answer in one table

Held-out seeds 2705000+1000i, **1,200 games per arm**, one pooled batch, arms innermost, with a NULL
arm (`MTG_FOLD_COUNTER_SOURCES`, counters-only) to calibrate the apparatus. Quality is quoted as
games-equivalent of the average-winning-turn delta; **one game of 1,200 is the metric's quantum**.

| arm | ms vs baseline | quality | play digests moved | verdict |
|---|---|---|---|---|
| null | 1.003 | +0.0 | 0/12 | the error bar |
| **`bp_chain_slot: 0`** | **0.990** | **+0.0** | **0/12** | **FREE -- take it** |
| `MTG_SOLVE_CHARGE` W=16 | 0.915 | +2.0 | 12/12 | trade |
| W=16 + chain | 0.899 | +1.0 | 12/12 | trade |
| W=8 | 0.862 | +5.0 | 10/12 | trade |
| `MTG_FOLD_SEARCH_ODO` | 1.001 | +0.0 | 0/12 | INERT (units exactly 1.0000) |

### 1. Drop the breakpoint chain arm -- free, and verified twice

`bp_chain_slot: 0` (per-job numeric field) or `BpChainSlotOptIn() -> 0` on the provider.
**-1.0% wall-clock, zero quality cost, and PLAY-IDENTICAL**: 0 of 8 digests moved in the first sample
(800 games) and 0 of 12 in the held-out replication (1,200 games). Play-identity is the property that
matters for a generation -- a lever that cannot change the play cannot invalidate a keep table being
fitted to that play, so it is safe to switch on at any point, including mid-run.

1% sounds dismissible and is not: on a multi-day generation it is **tens of minutes**, for free.

**Why it is only 1%, despite looking enormous in the census.** The arm is hugely active on this list
-- `MTG_DEDUP_CENSUS` at d1/b3 reads **98,933 chain applies in 25 games** (~3,957/game, against 21/game
on the shipped list, ~188x) and **99.5% of them are duplicates** (98,484/98,933). That is not a 99.5%
saving: the apply that discovers a candidate is a duplicate has already been paid for, so the only
recoverable work is what sits downstream of it. A duplicate STATE is not a removable CANDIDATE. The
same arm's `past_W` -- the case the arm actually exists for -- is 1.80% here (2.57% at d3/b10), which
is why it is worth measuring rather than assuming, and the measurement says the lines it removes do
not change the committed play at this cell.

### 2. The greedy-walk charge is a REAL saving and a REAL trade

`MTG_SOLVE_CHARGE` with `solve_charge_w`: **W=16 -> 0.915x, W=8 -> 0.862x** wall-clock. On a 3-day
generation that is roughly 6 hours (W=16) to 10 hours (W=8).

It costs quality: **+2.0 games of 1,200 at W=16, +5.0 at W=8.** Do not trust a single sample on this
-- the first 800-game sample read W=16 at 0.872x with quality **+0.0**, and the held-out replication
moved it to 0.915x with **+2.0**. The saving replicated in direction and weakened; the "free" part
did not replicate at all.

**And it changes the play (12/12 digests), which has a generation-specific consequence**: the keep
table is being FITTED to this play, so enabling the charge partway through means earlier completed R
values were fitted to a different play than later ones. Under the current rule (engine edits midway
are allowed, only fully-completed R values are used) that is not a correctness failure of any single
R, but it does mean the tiers are no longer fitted to one consistent policy. **If you want the charge,
turn it on at the start of a generation, not in the middle of one.**

Note the units column is NOT usable for this lever: the charged arm bills the greedy walk at
`unitsite::kGreedyWalk`, a site the uncharged arm does not count at all, so charged `units` reads
1.14-1.41x while the real work falls. Read ms against the null arm here, not units.

### 3. What does NOT help

* **`MTG_FOLD_SEARCH_ODO` is inert** -- 1.001x ms and `units` **exactly 1.0000**. It collapses
  interchangeable-copy axes in the search's subset walk, and this list has none it can reach. (It is
  also inert on the shipped Fungus and on 24 of 26 fleet decks; Snow is the deck it was built for.)
* **`MTG_BP_NOBP_SITE9` COSTS this archetype** -- +0.78%/+0.80% units on the shipped Fungus at
  d3/d5 for exactly zero quality. It is default OFF; leave it off for a generation.
* **`bottom_eval_units` / `MTG_BOTTOM_EVAL_UNITS` is the wrong tool**, despite being the largest
  bottoming-cost lever on record (-29.75% on Snow). `BottomEvalScope` lives in `AIEngine`'s IN-PLAY
  clairvoyant bottoming block and has no reference in the generator (`src/analyzer/ExhaustiveKeep.cpp`);
  it is for decks that pay the clairvoyant cost in play, and Fungus already ships a keep table.
* **`MTG_FOLD_ODO_SKIP`** (default ON, so you already have it) is byte-identical on this archetype.
  Its benefit lives inside the subset walk and does not show up in a play-identity test, so it is
  unmeasured here rather than proven useless.

## Where the cost actually is on this list

The per-arm dedup census at d1/b3 (25 games) says the chain arm is not the volume:

    base   931,680 applies / 357,905 dup (38.4%)   <-- the volume
    rank   197,866 / 158,610 dup (80.2%)
    unif    15,669 /  13,134 dup (83.8%)
    chain   98,933 /  98,484 dup (99.5%)           <-- highest dup RATE, 1/9th the volume

`base` is where the applies are. `rank` and `unif` are high-duplicate but **cannot** be saved by a
prune: `bp_choice` is a fixed-width positional index (`for at < BpSearchDepth(), for k < W`), so
filtering upstream only changes which duplicates occupy the slots. That leaves the base arm's 38.4%
and the devour ladder (4 Mycoloth on this list) as the open targets -- the latter is already being
worked (`MTG_DEVOUR_TRACE`, the landmark menu, the proven-kill devour collapse).

## Reproduce

    # reach: does the arm fire, and is past_W nonzero?
    MTG_DEDUP_CENSUS=1 MTG_ROLLOUT_STATS=1 build/Release/mtg \
      decks/Fungus/candidate-b-2026-09/Fungus.cod \
      --profile decks/Fungus/candidate-b-2026-09/Fungus.profile.json \
      --games 25 --seed 2405000 --depth 1 --budget-ms 3 --threads 1 --ignore-play-profile 2>&1 \
      | grep dedup_why

    # cost: logs/snowperf/fungcand{ab,2,3}.py (gitignored apparatus; arms innermost + null arm)

## THE 3x SUITE GATE BLOCKS THIS DECK (measured 2026-10-01)

**candidate-b cannot legally reach either generator yet, and the reason is a number, not an oversight.**
`scripts/mullgen.sh run` refuses (exit 3) for a deck with no regression-suite cases, and
`MTG_ALLOW_UNTESTED_DECK=1` is explicitly *"a USER decision, never an agent's"*. The sanctioned way in
is to add suite cases — which is gated on the **3x cost rule**.

Measured in ONE pooled batch holding BOTH lists at the suite's own `fungus` case shapes (d0 1000, d3
b10 150, d5 b20 75, seed 1001), so the comparison is contention-matched:

| cell | shipped ms/game | candidate-b ms/game | ratio | shipped avg | candidate-b avg |
|---|---|---|---|---|---|
| d0 | 0.1 | 0.1 | 0.4x | 5.6810 | 6.3220 |
| d3/b10 | 190.9 | 1993.2 | **10.4x** | 5.3800 | 5.3200 |
| d5/b20 | 180.1 | **4032.5** | **22.4x** | 5.3867 | 5.2533 |

The regime is comparable to `test/suite_cost.json` (it puts `fungus` at 224.49 ms/game against the
190.9 measured here for the same list's worst searched cell).

* Budget: **3 x fivecolour (1033.46) = 3100.4 ms/game**, fivecolour being the most expensive deck with
  BOTH a value leaf and a mulligan profile.
* candidate-b's worst searched cell is **4032.5 ms/game = 1.30x the budget — it FAILS**, and it is
  **21.1x the shipped Fungus list** it is meant to replace.

Per CLAUDE.md this settles the sequencing: *"If it is over 3x: do NOT add it and do NOT skip the
suite... getting the deck into that range becomes the first goal, ahead of both generators. An
intractable deck is a performance problem to fix, not a deck to quietly exempt."*

**How close the existing levers get it** (applied to that worst cell, using the ratios measured the
same day in `sac-mana-outlet-as-deferred-source.md`):

| state | ms/game | vs budget |
|---|---|---|
| today | 4032.5 | 1.30x — over |
| + `MTG_SAC_OUTLET_PAY` (0.85608x at cb_d5) | 3452.1 | 1.11x — over |
| + `MTG_SAC_POOL_TURN_COLOR` (0.97036x) | 3349.8 | **1.08x — over** |

So both of this session's levers together leave a **~7% gap**, and `MTG_SAC_OUTLET_PAY` is not adoptable
yet anyway (it still regresses at d0 on both lists and at shipped d5). The chain-arm drop documented
above is another ~1%. **Closing that last ~7% is the thing standing between this deck and its
generators** — which makes the cost strand the critical path, not a side quest.

Worth noting the quality side while this is open: candidate-b is **better than the shipped list at both
searched cells** (−0.060t at d3, −0.133t at d5) and clearly **worse at d0** (+0.641t). A deck whose
advantage only appears once the search is deep is exactly the shape that makes the cost gate bite.
