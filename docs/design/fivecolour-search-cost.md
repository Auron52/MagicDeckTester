# FiveColour search cost — why it is 25× the suite, and what to do before registering it

**Status: PARTLY RESOLVED.** Section 6 (provider misroute) FIXED + adopted; section 7 (tooling gap)
CLOSED. Sections 3-5 (the mana backtracker and the straggler tail) remain OPEN and still block
registering FiveColour in the tiers -- but the cost is down ~40% from where this started. See
"Where it stands now" at the bottom for the current numbers and the refuted levers.
Measured on 840ba15 and later, single-thread throughout.

## 1. The measurement

Apples-to-apples: every deck at `--depth 3 --budget-ms 10 --games 100 --seed 2002
--ignore-play-profile --lookahead-bottoming --threads 1`, run serially (no contention).

| deck | s/game | vs burn |
|---|---|---|
| burn | 0.021 | 1× |
| knights | 0.027 | 1.3× |
| auras | 0.035 | 1.7× |
| slivers | 0.035 | 1.7× |
| dragonstorm | 0.040 | 1.9× |
| th | 0.042 | 2.0× |
| antilife | 0.048 | 2.3× |
| goblins | 0.111 | 5.3× |
| hinata | 0.160 | 7.6× |
| creature_giving | 0.222 | 10.6× |
| **fivecolour** | **5.578** | **265×** |

FiveColour is **25× the next-heaviest deck**. It is also strongly seed-dependent: the same
config at seed 1001 is 1.81 s/game, so seed 2002 is ~3× worse than seed 1001.

## 2. It is NOT the branching factor

The obvious hypothesis is wrong. `MTG_BRANCH_STATS=1`, 6 games each:

| deck | EnumeratePlans calls | sum_odo | s/game |
|---|---|---|---|
| creature_giving | 25,194 | **1,552,046** | 0.222 |
| fivecolour | 33,328 | **1,474,960** | 5.578 |
| hinata | 17,394 | 379,998 | 0.160 |
| burn | 7,444 | 132,202 | 0.021 |

**creature_giving enumerates MORE odometer positions than FiveColour and runs 25× faster.**
The plan count is normal; the cost is *per position*.

For the record, the odometer composition (FiveColour, by driver card, `sum_odo` share):
Unite the Coalition 62% (avg 122, max 2688 — the S∈[0..5] modal split × 2 copies),
Lightning Greaves 17% (avg 169, max 1024), everything else ≤ 7%.
Worth revisiting later, but it is not what makes this deck slow.

## 3. It IS the mana backtracker

`callgrind` (build/Profile, 2 games, seed 2002, d3/b10) — 24.82e9 Ir total:

| component | Ir | share |
|---|---|---|
| **`TapForCostBacktrack` (all attributions incl. its memo hashtable + `CanPayFlat`)** | **8.30e9** | **33.4%** |
| `ComputeLordBonus` | 0.91e9 | 3.7% |
| `EffectiveProduces` / `DomainColors` | 0.36e9 | 1.5% |

One third of every instruction is spent assigning colours to mana sources. `MTG_TAP_STATS`
top-level entries over 6 games: fivecolour 116,202, hinata 36,510, creature_giving 27,151,
burn 7,880 — and the *entry count* is only 4× creature_giving's while the *cost* is 25×, so
each entry is also far more expensive.

That is exactly what this deck's mana base predicts: 5 colours, 11 fetchlands, 7 duals/triomes,
plus **two dynamic domain sources** (Faeburrow Elder, Bloom Tender) that produce one mana of
*each* colour among controlled permanents, and Deathrite Shaman which produces *any* colour.
The backtracker's per-node branching is the product of those choices, and `DomainColors`
rescans the whole battlefield on every `EffectiveProduces` call inside that recursion.
`ComputeLordBonus` at 3.7% is the same story (Faeburrow's `domain_self_pump` recomputed live).

## 4. The straggler tail is still severe

`-DMTG_PROFILE=ON` counters build, 60 games, seed 2002, d3/b10:

```
top  5% of games hold 59.7% of nodes
top 10% of games hold 74.5% of nodes
median game nodes: 28,914     max/median: 186x
heaviest games (index : nodes : ms):
  #42 : 5,379,758 : 234,932      <- 3.9 MINUTES in one game, 33% of all nodes
  #36 : 2,153,128 : 171,254
  #14 : 2,165,376 :  51,532
```

Reproducer for the worst: `--seed 2002 --game-index 42 --games 1 --depth 3 --budget-ms 10
--ignore-play-profile --lookahead-bottoming`.

This is a *different* shape from the 3.4 h Stage-4 atom that was fixed earlier (that one was a
single 7.4e7-position enumeration). Game #42 crosses no 1e6 odometer watermark at all — it is
978,211 positions spread over *many* moderately large enumerations, with 59,730 backtracker
entries in that one game (half of what six average games use). So the atom fix did not address
this; it is the ordinary cost of the deck, concentrated on unlucky seeds.

## 5. Sizing verdict — do NOT register yet

Smoke-style sizing probe at seed 1001 (the *favourable* seed), single-thread:

| case | s/game | wall for the sizing used by th/hinata/dragonstorm |
|---|---|---|
| d0, 1000 games | 0.0007 | 0.7 s — fine |
| d3 b10, 150 games | 1.81 | **272 s** |
| d5 b20, 75 games | 2.85 | **214 s** |

That is **~8.1 min added to a 15-min smoke budget**, from one deck, at its best seed. At the
regression seeds it is worse: d3 at seed 2002 is 5.58 s/game, so a 300-game case is ~28 min on
its own — the whole regression budget is 45 min for ten decks.

Registering FiveColour at any useful game count is not affordable until §3 is addressed. A d0-only
entry (0.0007 s/game) is affordable today and would still cover the new card implementations, but
d0 exercises no search, so it would not protect the subsystems this deck actually added.

## 6. Also found: FiveColour is on the WRONG decision provider

`SelectDecisionProvider` (`src/ai/DecisionProviders.cpp:5618`) sets `anti = true` for any deck
with `!p.fetch_land_types.empty()`. FiveColour's 11 fetchlands (Misty Rainforest, Scalding Tarn,
Verdant Catacombs, Windswept Heath, Wooded Foothills) trip it, and the deck carries **no other**
archetype signature — so it falls through to `AntiLifegainProvider`, whose fetch/tutor tiebreaks
are tuned for a specific 4-colour anti-lifegain shell. Confirmed in the callgrind profile:
`AntiLifegainProvider::FetchCandidates` is 242M Ir (~1%) of a FiveColour run.

This is the third deck to hit this trap — Goblins and Creature Giving each needed an explicit
escape hatch *above* the `anti` check (the code comments at 5630 and 5680 document both). Nobody
added one for FiveColour, because the whole deck was analyzed without anyone checking which
provider it rides.

**Consequence:** the shipped analysis (avg 5.3340) was measured under the wrong provider. Fixing
it is a behaviour change that moves FiveColour's numbers and requires re-verification, so it is
recorded here rather than done silently. The likely fix mirrors the other two: detect a
FiveColour signature (e.g. `domain_mana`) and return `g_generic` before the `anti` check — or,
better, narrow the `anti` signature itself so a bare fetchland stops implying anti-lifegain.

## 7. Tooling gap worth closing

There is **no per-game wall-time or straggler report in a Release build**. Every timing line in
`GoldFishRunner.cpp:305-313` is `#ifdef MTG_PROFILE`, so a normal run cannot say which game is
slow. The only straggler instrument in the tree is the keep generator's (`MTG_KEEP_SLOW_MS`,
default 30 s, `ExhaustiveKeep.cpp:1422`) — nothing equivalent for goldfish games. That is why the
3.4 h Stage-4 atom presented as "the analyzer is hung" rather than "game N is slow".

Proposal: an env-gated `MTG_SLOW_GAME_MS` in `GoldFishRunner` mirroring `MTG_KEEP_SLOW_MS` —
inert when unset, prints `[goldfish] SLOW-GAME <ms>ms gi=<i> seed=<s>` to stderr past the
threshold. Cheap (one `steady_clock` read per game), and it turns every future degenerate deck
from a mystery hang into a one-line reproducer.

## Suggested order of work

1. Close the tooling gap (§7) — small, inert, makes everything below observable.
2. Decide the provider question (§6) — it changes FiveColour's numbers, so do it before any
   re-baselining, not after.
3. Attack the backtracker (§3). Cheapest first: memoise `DomainColors` per (battlefield, controller)
   instead of rescanning inside the recursion, and confirm with a paired callgrind Ir A/B.
4. Re-measure §1/§5 and only then size the regression entries.


## Resolution of section 6 — FiveColourProvider ADOPTED (2026-08-07)

`FiveColourProvider` (archetype signature `domain_mana`, detected ABOVE the `anti` check exactly
like the Goblins and Creature Giving escapes) now owns `FetchCandidates`. The policy is
user-directed: get the colours that let us cast early ACCELERATION first, spread the five colours
over different sources, then build toward two sources of each. Encoded as a strict lexicographic
key (accel_new > spell_new > breadth > untapped > depth > colours > name) so the ordering is total
and deterministic, with no float weights to re-tune. It returns the full ordered list; the engine's
`FetchSearchCap` (2) decides how much of it the search branches on.

**Quality — improves on every seed set tried, including held-out:**

| seed set | old (AntiLifegain) | new (FiveColour) | delta |
|---|---|---|---|
| 4200000 (500 games) | 5.3400 | 5.1280 | **−0.212** |
| 90001 (500 games) | 5.3300 | 5.0880 | **−0.242** |
| 555000 (500 games, held out) | 5.3180 | 5.0980 | **−0.220** |

**Cost — it is also ~2x CHEAPER.** Deterministic, contention-immune counters (20 games, seed 2002,
one thread), which is the metric to trust on this box:

| counter | old | new | change |
|---|---|---|---|
| rollout calls | 990,802 | 524,203 | −47% |
| rollout turn steps | 1,136,565 | 580,015 | −49% |
| `TapForCostBacktrack` entries | 1,882,968 | 670,306 | **−64%** |

Wall clock agrees (100 games, seed 2002, one thread: 10.44 → 4.97 s/game) but the counters are the
evidence; absolute wall on this box drifts between runs.

The cost win and the quality win are the same effect: fetching for **coverage** means the greedy
scarcity-first payer finds a legal payment far more often, so the exponential backtracker is
entered a third as much. That is the mechanism section 3 predicted — the backtracker is expensive
*because* the mana base is under-fixed, and fixing the fetch policy attacks it at the source.

Smoke 30/30 + regression 50/50 byte-identical, 0 play-changed: no other deck is on this provider.

**Still open:** even at 4.97 s/game FiveColour is ~22x the next-heaviest deck, so §3/§4/§5 stand
and registration stays deferred. Next cheapest lever is still memoising `DomainColors` out of the
backtracker recursion.


## Resolution of section 7 — MTG_SLOW_GAME_MS (44285cd)

`GoldFishRunner` now times every game and streams any that exceeds `MTG_SLOW_GAME_MS` (ON by
default at 30 s, `=0` disables), mirroring the keep generator's `MTG_KEEP_SLOW_MS`:

```
[goldfish] SLOW-GAME 161159ms  gi=42 wt=5  repro: --seed 2002 --game-index 42 --games 1
```

Validated end-to-end: run blind over 100 games at seed 2002 it independently flagged **exactly**
gi=36 and gi=42 — the same two games the `-DMTG_PROFILE=ON` counters build had identified in §4,
without needing that separate build. One `steady_clock` read per game; nothing feeds a decision.

## Also fixed: the backtracker never exiled Deathrite's graveyard land (44285cd)

`TapForCostBacktrack` carries its own inline copy of `ExileGraveyardLandForMana`, and it read
`gy[g].IsLand()` on a graveyard placeholder — always false, so the loop exiled nothing. The fuel
gate said "live" and the tap took the mana while the land stayed in the graveyard, so **N Deathrites
could all tap off ONE graveyard land in a single payment** (this deck runs four). Same placeholder
class as 333ab17; a repo-wide sweep for raw `.IsLand()/.IsCreature()/.ManaValue()/.IsMulticolored()`
on hand/library/graveyard cards now finds none left in the engine. Play-neutral (+0.001 avg over
3x500 games), as expected for removing an advantage that was never legal.

## REFUTED lever — colour-aware B&B gate (do NOT retry)

Hypothesis: the total-mana gate has no colour awareness, so a `{W}{U}{B}{R}{G}` probe on a board
with five lands but three colours passes it (5 >= 5) and then walks the whole tap tree to prove
what one colour count already knew. Implemented as a lossless top-level per-colour capacity check
(each source's full max net credited to every colour it can make — a deliberate over-count;
colours read through `ProducesForPayment`).

**Measured: 0.28%** (paired callgrind Ir, 2 games seed 2002: 24,177,852,448 -> 24,109,381,014).
Inside the noise band this repo already burned itself on (see `shard-volley-hold.md`, where a
+0.14% Ir claim was retracted). REVERTED rather than shipped.

*Why it failed, so the next person does not re-derive it:* the greedy scarcity-first payer already
handles the colour-easy cases, so `TapForCostBacktrack` is entered precisely when the colours ARE
available and only the assignment is hard. A colour-feasibility gate therefore almost never fires
at the point it is checked. Any future attack has to prune INSIDE the tree, not at its root.

## Where it stands now

Single-thread, 100 games, d3/b10, current HEAD:

| seed | before (start of this work) | now | change |
|---|---|---|---|
| 2002 | 5.578 s/game | **3.318** | −40% |
| 3003 | — | **2.423** | |
| 1001 | 1.81 (at 150 games) | **1.039** | −43% |

Still ~15x the next-heaviest deck (creature_giving 0.222), and **the tail still dominates**: at
seed 2002 two games out of a hundred (gi 36 at 44 s, gi 42 at 161 s) account for 205 s of the
331.8 s total — 62% of the run in 2% of the games.

**Sizing verdict — still deferred, but closer.** A 300-game d3 regression case would be ~12-17 min
at the regression seeds, against a 45-min budget shared by ten decks. Options when this is revisited:
1. Attack the tail directly (it is 62% of the cost; the two reproducers above are cheap to profile).
2. Register d0-only now (0.0007 s/game) — affordable immediately, but exercises no search.
3. Register a SMALL d3/d5 case (~50 games) and accept ~2-3 min, after checking the chosen seed
   range has no straggler (`MTG_SLOW_GAME_MS` now makes that a one-line check).


## The tail atom: Lightning Greaves (2026-08-08) — FIXED

Profiling the two real stragglers (the earlier §4 repro line was WRONG -- see below) found the tail
is an ENUMERATION atom after all, not backtracker cost:

| game | wall | odometer positions | rollout calls |
|---|---|---|---|
| 42 (seed 2044) | 161 s | **2,550,027,672** | 1,251,137 |
| 36 (seed 2038) | 44 s | 877,079,390 | 380,103 |
| 0 (seed 2002, median) | 0.90 s | 929,433 | 37,494 |
| 1 (seed 2003, median) | 1.22 s | 3,089,558 | 31,049 |

~1000x the positions of a normal game. `MTG_BRANCH_STATS` named the driver: **Lightning Greaves,
84% of game 42's odometer** (avg 1032, max 43,008), and `MTG_ENUM_STATS` showed the shape --
`[g13 k13 Lightning Greaves]`, a **13-option Equip group multiplying the ENTIRE odometer by 14**.
One equipment on a wide board made every controlled creature plus every hand creature an option in
one mutually-exclusive group. The stranded-equip subset guard could not help: it rejects a position
*after* the odometer has produced it.

**Fix (user-directed).** Two parts:

1. **Only offer an equip that does something, and only to ONE host.** A pair whose host is not
   summoning-sick or already hasty is a literal no-op (haste is the only modeled equipment effect).
   Among the hosts that DO benefit, a heuristic picks a single one: highest `power + mana-if-tapped`,
   ties by lowest card number. User's framing: "the largest power and/or effect creature that
   doesn't already have haste and is summoning sick -- most of the time this will be exactly one or
   zero creatures." `MTG_EQUIP_ALL_HOSTS=1` restores emit-every-host for A/B.
2. **`CanTapNow`** -- equip-granted haste now unlocks {T} ABILITIES, not just attacks (CR 302.6).
   `HasHasteFromEquip` was consulted by `CanAttackFull` alone, so a Greaves'd fresh mana dork could
   attack but not tap. This retires the limitation disclosed in `CardParams::is_equipment` and is
   what makes the "or effect" half of the ranking real. Applied at the ~16 mana-source `CanTap()`
   sites; `KeepModel` deliberately untouched (trained-model features).

**Measured.** Stragglers: game 42 176 s -> 45 s (3.9x), game 36 44 s -> 6.3 s (7.0x), same win turns.
Whole-deck cost, single-thread 100 games d3/b10: **seed 2002 3.318 -> 1.403 s/game (2.4x), seed 3003
2.423 -> 1.670**. Quality essentially unchanged: 5.1280/5.0900/5.1000 -> 5.1320/5.0920/5.1060
(+0.004 avg -- the price of collapsing a searched host set to one heuristic pick, offset by the
CanTapNow gain). Smoke 30/30 + regression 50/50 byte-identical.

### Instrument bug found and fixed

`MTG_SLOW_GAME_MS` printed `--seed <base_seed>` instead of `--seed <base_seed + gi>`. The runner
shuffles on `base_seed + gi` while spawns use `base_game_index + gi`, so the printed line silently
replayed game 0 -- which is why an early profiling pass had gi=36 and gi=0 producing identical
statistics. Corrected to match the "Unwon games" convention in main.cpp. **Any repro line captured
before 2026-08-08 is suspect.**

## Cost standing after all of this

| deck | s/game (d3/b10, seed 2002, 1 thread) |
|---|---|
| burn | 0.021 |
| creature_giving | 0.222 |
| **fivecolour** | **1.403**  (was 5.578 at the start of this work -- **4.0x faster**) |

Now ~6x the next-heaviest deck rather than 25x. §3's mana backtracker is still the largest single
component and §4's tail still exists (one game over 30 s per 100 at each regression seed, down from
two), but a smoke-sized d3 case is now roughly 2-4 min single-thread rather than 8+, so registration
is worth re-sizing next.


## RESOLVED (2026-08-08) — the +0.012 was a ranking bug, not a width trade

The Greaves commit (aff6768) bundled three changes and reported their NET as "+0.004". Both the
sign and the size were re-measured here, and the attribution turned out to be the opposite of the
assumption in the old NEXT UP section: the loss is real, it is bigger than the net, and it is
**fixable without giving back any of the cost win**.

### Method

All arms ran on ONE binary behind temporary levers, so no A/B ever straddled a `build.sh` (the
trap that invalidated the earlier cost-reframe runs). Metric is the sum of avg-win-turn over three
500-game seed sets (4200000 / 90001 / 555000-held-out), 1500 games per arm; lower is better.
`max_turns` is 8 here, so an unwon game scores 9 — confirmed by rebuilding each arm's reported
average from its own per-game `MTG_DUMP_WINS` stream.

The **control arm** (`MTG_EQUIP_ALL_HOSTS=1` + equip-tap disabled) reproduced the pre-commit
baseline exactly — 5.1280 / 5.0900 / 5.1000 — which is what licenses reading the other arms.

### Attribution of aff6768

| arm | 4200000 | 90001 | 555000 | sum | vs pre |
|---|---|---|---|---|---|
| pre (control, reconstructed) | 5.1280 | 5.0900 | 5.1000 | 15.3180 | — |
| no-op filter + equip-tap, all hosts | 5.1240 | 5.0860 | 5.0980 | 15.3080 | −0.0100 |
| shipped (adds the single host pick) | 5.1320 | 5.0920 | 5.1060 | 15.3300 | +0.0120 |
| single pick, equip-tap reverted | 5.1320 | 5.0940 | 5.1080 | 15.3340 | +0.0160 |

So, isolated: the **no-op filter gained** ~0.006, **equip-tap gained** 0.004 (as predicted — more
mana available), and the **single host pick lost 0.022**. The pick alone is the whole regression.

### Root cause — an unaffordable host wins the slot

Only 7 games in 1000 differ between width 1 and all-hosts, every one by exactly one turn, so the
mechanism had to be found by inspection rather than aggregates. `MTG_EQUIP_PICK_STATS` dumps the
chosen host and the pool it was chosen from. On seed 4200000 gi119 (width 1 wins T6, all-hosts T5)
it shows the same pick in nearly every state:

```
[equip-pick] Lightning Greaves -> host 41 (score 10) | pool: 27(hand,s=5) 28(hand,s=5) 41(hand,s=10)
```

Host 41 is **Progenitus** — `{W}{W}{U}{U}{B}{B}{R}{R}{G}{G}`, uncastable on turn 3 — and it beat
host 27, **Maelstrom Archangel**, on printed power. The single Equip slot went to a host the plan
could never produce, so the Archangel line (attack with haste → `combat_damage_free_cast` → free
Two-Headed Hellkite) was never even offered. Two distinct defects in one ranking:

1. a HAND host was ranked on printed power with no regard for whether it can be cast this turn;
2. the score was power + mana only — the "**and/or effect creature**" half of the rule was never
   encoded, so an attack-triggered payoff counted for nothing.

### Fix — both halves of the ranking, cost-neutral

* `MTG_EQUIP_HOST_AFFORD` (default on): a hand host must satisfy `ManaValue <=
  SpareUntappedMana(...)`. The bound is the turn's whole untapped pool, i.e. an over-estimate, so
  it can never exclude a host the plan could actually reach.
* `MTG_EQUIP_HOST_EFFECT` (default on): add the payoff haste pulls a turn forward —
  `combat_damage_free_cast` +5 (a free spell in this deck is a ~5-drop, so ≈ one extra body),
  `attack_draw_cards` +1/card. Scale is "power points", matching the existing power + mana term.

| arm (width 1 unless noted) | 4200000 | 90001 | 555000 | sum | backtracker entries |
|---|---|---|---|---|---|
| shipped | 5.1320 | 5.0920 | 5.1060 | 15.3300 | 1,027,057 |
| + afford gate | 5.1300 | 5.0860 | 5.1040 | 15.3200 | — |
| **+ afford + effect (ADOPTED)** | 5.1260 | 5.0860 | 5.1020 | **15.3140** | 1,042,497 |
| all hosts (play ceiling) | 5.1240 | 5.0860 | 5.0980 | 15.3080 | 1,246,708 |

The adopted ranking recovers **73% of the width-0 play at +1.5% backtracker work**, improves all
three seed sets including the held-out one, and lands *below the original pre-aff6768 play*
(15.3140 vs 15.3180) while keeping the commit's ~31% cost win. Widening instead would cost +21%
backtracker and +17–19% wall on the deck whose cost is the very thing blocking registration.

Note `MTG_EQUIP_HOST_WIDTH=0` measures the same 15.3080 with or without the ranking fix — once
every host is offered the search does the choosing, which is exactly why the ranking only matters
at width 1.

**Honest limit on the constants:** +5 / +1 were read off gi119, a single game. They are validated
only by the held-out seed set moving the right way (5.1060 → 5.1020). If a future deck leans on
equipment, re-sweep them rather than trusting these values.

### Also fixed — the {T}-ability haste gap was SYMMETRIC

`CanTapNow` is now the single predicate for "may this permanent use a {T} ability now", covering
own-keyword / lord / equipment haste, and the three inline gates (Krenko's token tap, Deathrite's
two graveyard-exile modes) now call it instead of re-deriving the condition. The engine previously
had the gap in *both* directions — mana-source sites saw equipment but not lords, the {T}-value
sites saw lords but not equipment — which nothing distinguishes at the rules level (CR 302.6 is one
restriction lifted by haste from any source).

**Both granted-haste paths are LIVE in the deck pool** (`MTG_HASTE_TAP_STATS`, 60 games, seed 2002 —
counts a summoning-sick permanent rescued into a {T} ability, at a gate that actually emits):

| deck | rescued by lord | rescued by equipment |
|---|---|---|
| Goblins | **2,199** | 0 |
| FiveColour | 0 | **1,020,138** |
| slivers_vial | 0 | 0 |
| Dragonstorm | 0 | 0 |

Goblins is Krenko, Mob Boss tapping the turn it lands under Goblin Chieftain / Goblin Warchief —
a heavily-exercised line, NOT a hypothetical. FiveColour is Greaves on a fresh mana dork (the
aff6768 win). So this is not a latent fix in general: **it is byte-identical only because each live
pairing was already covered by the site that needed it** — the Krenko gate already called
`HasHasteFromLords`, and the mana sites already called `CanTapNow` with the equipment term.

What unification actually ADDS is the two CROSS terms, and only those are latent today:
* lords at mana-source sites — no deck pairs a haste lord with a mana creature of the granted
  subtype, so it is currently unreachable;
* equipment at the {T}-value gates — only FiveColour has both (Greaves + Deathrite Shaman), and it
  never changed an outcome across 1500 games or the counter run.

An earlier note in this doc claimed the whole thing was latent and that lord haste "moves
slivers/goblins ground truth". Both were wrong, in opposite directions, and both came from
filtering the card data instead of counting what the engine actually does. The counter above is
the check to run before making that kind of claim again.

While wiring this up, both {T}-value gates were reordered to test their cheap param predicate
BEFORE the battlefield-scanning haste predicate. That is behaviour-identical and strictly less
work: on Goblins the lord-rescue count at those gates fell from 271,426 to 2,199, i.e. ~99% of the
successful lord scans were being done for permanents with no {T} ability at all.

Smoke 30/30 and regression 50/50 byte-identical, 0 play-changed.

### Still open

The tail is untouched and is now the dominant cost: at width 1 the worst single game in a 500-game
seed-4200000 run is **235 s** (gi=61), and 60-game probes at seeds 2002/2044 show worst games of
115 s / 62 s. That is the mana backtracker (§3), not Greaves — the Greaves atom is closed. Sizing
for regression registration (§5) still depends on that tail, so registration stays deferred.
