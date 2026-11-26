# Elves -- Stage 5i cleanup-discard BUCKET policy (AGENT DRAFT, pending the USER)

Branch `wip/elves-discard` (base `ce2cddf5d`). Implementation: `ElvesBucketShedOrder` /
`ElvesProvider::CleanupDiscardCandidates` in `src/ai/DecisionProviders.cpp`, behind
`MTG_ELVES_BUCKET_DISCARD` (heurarm `ELVES_BUCKET_DISCARD`), **default OFF** -- the USER has not approved it.
Unit tests: `test/unit/test_elves_provider.cpp`, section 4.

## What Elves sheds with today

`ElvesProvider` derives from `SelesnyaLifegainProvider` and has no discard hook of its own, so it INHERITS
SelesnyaLifegain's USER-settled buckets (`MTG_SELESNYA_BUCKET_DISCARD`, default ON). Those buckets were settled
for a different list: LANDS 3 / ELVES as mana (Priest > Archdruid > Llanowar = Mystic) / LIFEGAIN / PAYOFFS
(Bogbeast, Paragon, Craterhoof, Genesis Wave, Ageless, Avatar). On the Elves list every card that is in none of
those roles -- the lords (Elvish Clancaller, Leaf-Crowned Visionary, Galadhrim Brigade), Joraga Warcaller, Ezuri,
Allosaurus Shepherd, Immaculate Magistrate, Concordant Crossroads, Taunting Elf -- lands in the inherited rule's
`other` list, which it sheds FIRST (right after a dead card), ahead of a surplus land. Genesis Wave is shed
unless the two-turn projection reaches 8 mana.

## The drafted buckets (every role keyed on card PARAMS, not names)

| bucket | cards | quota / rule |
|---|---|---|
| DEAD | a legend already ours on the battlefield or earlier in hand (Ezuri, Marwyn); Concordant Crossroads while one is ours in play; a 2nd Crossroads in hand | shed first |
| LANDS | Forest > Cavern of Souls (G for creatures only) > Wirewood Lodge ({C}) | `clamp(4 - board lands, 1, 3)`; +1 (cap 3) when no castable mana Elf is held and none is in play (fungible upward) |
| SCALED MANA | Priest of Titania > Elvish Archdruid > Marwyn | 1 in the ladder, rest in THE REST |
| SINK (one) | Genesis Wave, Ezuri, Joraga Warcaller, Allosaurus Shepherd | ONE kept (USER: "pick just one of those and scale it up fully"); nearest first, then Wave (only when the projection reaches 8, X=5) > Ezuri > Warcaller > Wave (short of 8) > Shepherd. A 2nd Shepherd while one is in play is a BODY |
| LORDS | Leaf-Crowned Visionary > Elvish Clancaller > Galadhrim Brigade | 1 in the ladder |
| DORKS | Llanowar Elves = Elvish Mystic | 1 in the ladder |
| ENABLER | Concordant Crossroads | 1, after the threat floor (sink + lord) |
| BODIES | Immaculate Magistrate, Taunting Elf (USER: its lure does nothing in goldfish) | THE REST |

**Keep ladder** (most kept first): land 1 > scaled 1 > sink 1 > land 2 > lord 1 > dork 1 > land 3 > enabler 1 >
THE REST (every other spell: nearest first, then one value rank across classes: Priest > Archdruid > Visionary >
Clancaller > Brigade > Marwyn > Ezuri > Warcaller > Llanowar = Mystic > Wave > Shepherd > Magistrate > Taunting Elf).

**Shed order** (overflow only): dead -> spells of THE REST not castable within two turns (farthest, then worst)
-> surplus lands (Lodge, then Cavern, then Forest) -> the rest of THE REST (worst first) -> the ladder backwards.
ONE index is returned (5i rule 5) through `CleanupDiscardRankingWithOrder`; `MTG_ELVES_DISCARD_FAN=1` (testing
only) returns the full order for the rule-vs-searched trace. Not prefix-stable (the land quota reads whether a
castable mana Elf is held), so the engine re-asks after every shed.

**Distance** = an effective cost vs a two-turn projection. Cost is mana value, except Genesis Wave 6 (X=3, the
least worth casting), Allosaurus Shepherd 7 (cast + its {4}{G}{G}), Joraga Warcaller 3 (one kick). M1 = board
lands + the live board dork yield + 1 for a kept land. M2 = board lands + up to two kept drops + the board dorks
re-counted over the hand mana Elves deployed next turn within M1 (cheapest first) + those Elves' own yields.

## Doubts (for the USER)

1. **Sink order.** Ezuri > Warcaller > Wave-short > Shepherd is an agent guess. The USER's scaling rule says one
   sink is scaled; it does not rank the sinks. Wave jumps to the front only when the projection reaches 8 (the
   SelesnyaLifegain threshold, reused).
2. **Land quota.** `clamp(4 - board lands, 1, 3)` keeps one land even late, because every sink scales with mana.
   A surplus land sheds before any castable spell, but AFTER a spell that is out of reach for two turns.
3. **Galadhrim Brigade is a LORD, not a sink**, though squad scales. Treating it as a sink would let a hand keep
   Brigade instead of a Wave or Ezuri.
4. **Crossroads is kept after the threat floor**, not as a payoff, and a 2nd copy is dead.
5. **Not modelled:** draws (including Visionary's), Wirewood Lodge's untap, colour (all G except the Lodge;
   Cavern's G is creature-only, so a Wave/Crossroads cannot use it -- ignored).

## Measurements

### Evidence: shed census at the suite setting (d5 b20, flag OFF = shipped), binary 29633f152

Batch `build/scratch/elves-discard/census.json` (summary copied to `logs/elves/discard/census.*`): std 400 games
seed 9700000 (avg 4.4125, digest f3c39ead7a34e7e5), 2HG 200 games seed 9800000 (avg 4.7150, digest d1979697ecb7a769),
`MTG_SHED_STATS=1 MTG_TRACE=discard`.

* **Real cleanup sheds: 2 in 600 games** (1 std, 1 2HG): **0.0025/game std, 0.005/game 2HG**. Both hands were
  8 cards with no land in hand; g9700126 T5 had ZERO lands in play (a screw game), g9800196 T7 two. The inherited
  rule shed Allosaurus Shepherd (std) and Galadhrim Brigade (2HG) -- both from its `other` list, as predicted.
  (Each decision traces twice: the ranking is built twice per real shed.) Earlier ledger run: 3 sheds / 400 games
  at d3 b10, STATUS_QUO_OK.
* **No non-cleanup discard sites** in this list (no discard costs or discard triggers among the 18 cards), so the
  MTG_NONCLEANUP_SHED_WORST bound is expected to be structurally inert -- still to be measured in the A/B batch.
* **Rollout cleanup sheds: 64,100 over 600 games (~107/game), 99.85% with under 4 lands in play**; hand size at
  entry 8 (63,989) or 9 (12). SHED POLICY: the inherited policy's index 0 differs from the generic max-MV
  ranking on 74.8% of 64,102 calls. So the axis lives almost entirely inside the rollouts' land-light futures.
* **Tie rate: NOT YET MEASURED.** With ~1 labelled decision per 400 games at d5, the label trace
  (`MTG_DISCARD_NODE=0 MTG_DISCARD_TRACE=1`, analyze_deck `--discard-analysis`) needs thousands of games to say
  anything; the bound (below) is the instrument that answers the question.

### The pooled A/B + bound batch (2026-11-26, binary 29633f152, d5 b20, 32 threads)

ONE batch, `logs/elves/discard/gen_manifest.py 400 9600000` (8 jobs x 400 games, seeds 9600000+gi, paired by
gi), `MTG_DUMP_WINS=1 MTG_RSS_CAP_GB=20`, under the heavy lock. Heartbeat 32/32 until the tail. Copies:
`logs/elves/discard/ab.json`, `ab.out` (per-job digests), `ab.pair.txt` (`pair.py ... off on worst ncw`),
`ab.err.summary` (everything except the `[win]` lines). Scratch: `build/scratch/elves-discard/ab.err`.

| arm | fmt | games | won | win-turn sum (unwon=9) | avg win turn | units vs off | faster | slower | digest |
|---|---|---|---|---|---|---|---|---|---|
| off (shipped: inherited Selesnya buckets) | std | 400 | 399 | 1739 | 4.3475 | -- | -- | -- | 86f60929e0e3513e |
| on (`MTG_ELVES_BUCKET_DISCARD=1`) | std | 400 | 399 | 1739 | 4.3475 | +0.42% | 0 | 0 | 435306882006576a |
| worst (`MTG_SHED_WORST`, BOUND) | std | 400 | 399 | 1739 | 4.3475 | -1.05% | 0 | 0 | 1618184c453e8a8a |
| ncw (`MTG_NONCLEANUP_SHED_WORST`) | std | 400 | 399 | 1739 | 4.3475 | +0.00% | 0 | 0 | 86f60929e0e3513e (= off) |
| off | 2hg | 400 | 399 | 1876 | 4.6900 | -- | -- | -- | 42a0a554cb5bac67 |
| on | 2hg | 400 | 399 | 1876 | 4.6900 | +3.08% | 0 | 0 | ecf05599ca4a84fa |
| worst (BOUND) | 2hg | 400 | 399 | 1876 | 4.6900 | -1.52% | 0 | 0 | 42a0a554cb5bac67 (= off) |
| ncw | 2hg | 400 | 399 | 1876 | 4.6900 | +0.00% | 0 | 0 | 42a0a554cb5bac67 (= off) |

Paired delta is **exactly 0.00000 in every arm and format**: not one of the 1,600 paired games moved its win
turn (same won/unwon game too). The arms DID engage: `on` and `worst` change the units searched and three of the
four digests, so the rollouts shed differently, and the search still lands on the same win turn every game.

* **ncw is byte-identical to off** (digest and units, both formats), confirming the list has no non-cleanup
  discard site, as predicted.
* **The BOUND is zero.** `worst` makes every rollout cleanup shed take the LAST-ranked card of the shipped full
  ranking (roughly 107 sheds per game by the census), i.e. the opposite of any sensible ranking, and it moves 0 of
  800 games. If the worst ranking plays exactly like the shipped one, no ranking of the rollout shed can buy
  anything measurable at the suite setting. Rule of three: under 3/800 = 0.375% of games (0.75% per format) can
  be moved by the rollout axis at all (95%).
* **The real-play axis is too rare to matter:** 2 real sheds in 600 census games (0.0025/game std, 0.005 2HG),
  and `on` (which also changes the real shed) moved none of these 800 games.

### Slower-game classification (d8 b0)

**Nothing to classify.** No arm has a slower game vs `off` (0 slower, 0 faster in all six comparisons), so the
d8 b0 recovery step has an empty input set. `logs/elves/discard/repro_manifest.py` (built for it) emits 0 jobs.

### Rule-vs-searched label check: MOOT

The label check asks whether the bucket rule's pick at a real cleanup shed matches the searched label. Its value
is bounded twice over, and both bounds come back zero or close to it:
1. The rollout bound above is 0 of 800 games: inverting the ranking in every rollout moves nothing.
2. A perfect oracle at the real shed can only change games that shed for real, about 1 in 200-400. The `on` arm
   already replaces that decision and moves 0 of 800 games.
So even a perfect label oracle would move about nothing (at most about 1-2 games per 400, and none seen), and
thousands of trace games (`MTG_DISCARD_NODE=0 MTG_DISCARD_TRACE=1`) would buy no decision. Not run.

### Recommendation (for the USER; the flag stays default OFF)

**Do not adopt `MTG_ELVES_BUCKET_DISCARD`; keep Elves on the inherited SelesnyaLifegain buckets.** At the suite
setting the cleanup-discard axis is inert for this list. The anti-heuristic bound moves 0 of 800 games, and the
drafted policy moves 0 of 800 while costing +0.42% (std) / +3.08% (2HG) units. Why it is inert: Elves empties its
hand fast, real sheds happen only in land-screw games (0.0025-0.005 per game), and 99.85% of rollout sheds sit in
land-light futures whose win turn the shed card does not decide. The draft is worth keeping on the branch as
documentation of the Elves roles (it reads more sensibly than "shed the lords first"), but it buys no measured play,
so its doubts 1-5 above need no answer unless the list changes (e.g. more card draw, or a higher curve that makes
8-card hands common).
