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

## Where the cost actually is now (2026-08-08) — lookahead BOTTOMING, not the play search

Re-measured every deck on the current build (40 games, seed 2002, d3/b10, one thread, pooled):

| deck | s/game | rollout calls | backtracker entries |
|---|---|---|---|
| burn | 0.027 | 33,445 | 39,094 |
| Knights | 0.036 | 61,980 | 43,596 |
| slivers_vial | 0.041 | 69,142 | 60,341 |
| treasure_hunt | 0.052 | 30,460 | 26,866 |
| Dragonstorm | 0.053 | 25,178 | 56,338 |
| Auras | 0.072 | 33,673 | 49,600 |
| Anti-Lifegain | 0.100 | 32,511 | 37,323 |
| Goblins | 0.153 | 161,923 | 111,316 |
| Hinata2 | 0.233 | 75,024 | 185,476 |
| **FiveColour** | **0.880** | **824,487** | **1,042,497** |

FiveColour is still **3.8x the next-heaviest deck**. But the earlier framing ("the mana backtracker
is the top cost") was reading EXCLUSIVE self-cost. Inclusively (callgrind, 3 games, contention-free
— the metric to trust on this box):

```
21,760,137,271 (78.43%)  AIEngine::BottomCards(GameState&, int, int)   [2 calls in 3 games]
```

**Bottoming is 78% of the runtime.** `BottomCards` falls through to the lookahead path whenever the
deck has no exhaustive table with `bottoming_enabled` (FiveColour has no mulligan profile at all),
and that path rolls out **a full game per candidate card, per bottom step** — 7 rollouts for a
one-mulligan game, 13 for two. One bottoming decision costs ~5.5x an entire game of play. The
backtracker work the earlier sections chased is mostly *underneath* this.

A cross-deck wall check corroborates (`--force-mulligan "0:"`, FiveColour 22.3s -> 9.5s over 30
games, Goblins 5.2s -> 1.7s) but is NOT a clean attribution: forcing a keep also changes which
hands are played, which is why several decks come out negative. Trust the callgrind number.

### Levers, sized

1. **A mulligan profile with `bottoming_enabled` turns this into an O(1) table lookup** — the
   architecture already does this (`ExhaustiveKeep::DecideBottom`). This is the single biggest win
   available for FiveColour, and it is user-gated work (generation is expensive and commit-bound).
   Note the mulligan skill ships `bottoming_enabled` OFF until a validated high-R run, so a profile
   alone does not automatically collect it. Goblins has a profile being generated now.
2. **Duplicate-candidate collapse** (lossless-ish): two copies of the same card are the same
   removal. Expected distinct names in a 7-card hand — burn 4.57/7 (**35%** fewer rollouts),
   Goblins 5.24/7 (**25%**), FiveColour 6.47/7 (**8%**). A good general win, but small on the deck
   that needs it most, and not strictly byte-identical (which physical `m_number` stays in hand can
   move a downstream tie-break).
3. **Pre-filter candidates by the heuristic before rolling out** — today all 7 get a full-game
   rollout and the heuristic only tiebreaks among win-optimal removals. Restricting to the K worst
   heuristic candidates would cut rollouts ~57% at K=3. This CHANGES PLAY for every deck, so it
   needs its own sweep + accept cycle; it is not a free win.
4. **Two-stage mana/payoff gating** — `MTG_ENUM_STATS` reports that **76.6%** of FiveColour's
   enumerated payoff lines are unaffordable under EVERY mana line (Hinata2 74.4%, Goblins 74.8%,
   Dragonstorm 62.6%), with an estimated 1.4–1.8x fewer visits. This is a general engine win, not a
   FiveColour one, and is a known deliberately-unimplemented design (see the ceiling instrument at
   `TurnSolver.cpp` "two-stage gating potential").

### What is NOT the differentiator

FiveColour runs essentially the SAME number of mana-side enumerations as Hinata2 (85,663 vs 84,735)
but 5.6x the backtracker entries — so its mana problems are individually ~5.6x harder (five colours,
and its mana-side symmetry collapse is 1.00x vs Hinata2's 1.05x: with five colours almost no two
sources are interchangeable, so there is nothing to dedup). That is inherent to the archetype, not
an obvious bug.

### Tail reproducers to investigate (captured 2026-08-08)

The value-row dump (2500 games, shipped play d5/b20, K=3 searched labels) finished 2498 games and
then spent **over 109 minutes** on the last two, with 23 of 24 cores idle. Rows carry `(seed, turn)`,
so the unfinished games are exactly the seeds that produced none:

| game | repro |
|---|---|
| A | `--seed 920937 --game-index 187 --games 1` |
| B | `--seed 922346 --game-index 96  --games 1` |

Full command (the labelling regime they were slow in — K=3 searched labels is 3 full searches per
position, which is NOT the same regime as normal play):

```bash
MTG_DUMP_VALUE_ROWS=/tmp/x.rows MTG_EVAL_ROWS_K=3 MTG_EVAL_ROWS_ROLLOUT=0 \
build/Release/mtg decks/FiveColour/FiveColour.cod \
  --profile decks/FiveColour/FiveColour.profile.json \
  --seed 920937 --game-index 187 --games 1 --threads 1
```

Both `--seed` and `--game-index` matter: the runner uses `base_seed + gi` for the shuffle and
`base_game_index + gi` for opponent spawns, so dropping either replays a different game.

**Worth checking first:** whether they are also slow in PLAIN play (no labeller env). If yes this is
the same tail as the 235 s gi=61 game above and belongs to the mana backtracker; if they are slow
only under labelling, the cost is in the K=3 label path and is offline-only. That distinction decides
whether fixing it helps the shipped engine or only generation.

### Why the value-leaf matrix is so expensive here (2026-08-08)

The H arm dwarfs the V arm. Measured per-cell, 400 games x 4 seeds:

| depth | H s/game | V s/game | leaf advantage |
|---|---|---|---|
| 2 | 5.9 | 0.1 | 59x |
| 3 | 30.7 | 0.4 | 77x |
| 4 | 113.4 | 2.1 | 54x |
| 5 | 190.4 | 5.9 | 32x |

The whole 8-deep V ladder cost ~14 core-hours; the H ladder to d5 costs ~151 — about 11x the entire
V side. That is why 8 cells hit the tractability guard and ALL of them were H (H4 on every seed, H5
on three, one with zero games) while all 20 V cells finished.

**The ladder IS engaged — verified, not assumed.** `MTG_LADDER_VALUE_LEAF=1` with the staged model
attached, committed pass at `MTG_VALUE_MODEL=0`, H5/seed 8008, 2 games:

| ladder | rollout calls | wall | avg |
|---|---|---|---|
| ON | 807,129 | 38.9 s | 4.5000 |
| OFF | 1,646,972 | 76.2 s | 4.5000 |

Half the rollout calls vanish and the result is identical — passes 1..d-1 on the leaf, only the
committed pass on the heuristic, exactly as designed.

**But it is only 2.0x here, against 84.8x on Knights and 39.5x on Anti-Lifegain at d5.** The reason
is in the residual: 807k rollout calls REMAIN with the ladder on, and those are the committed d5
pass, which is irreducible because that pass *is* the H measurement. Where a deck's ladder is mostly
warm-up, the mode is enormous; FiveColour's cost is concentrated in the committed pass, so there is
little to skip. Do not expect the published 15-85x range to transfer to an expensive deck.

**Sequencing lesson.** Bottoming is 78% of this deck's runtime (section above) and the matrix pays it
in every game of every cell, with the bottoming rollouts themselves running at the cell's depth. The
78% was measured BEFORE this pipeline was started and then written up as a "lever" rather than acted
on. Fixing it first would plausibly have made the H arm ~4x cheaper and turned an 8-hour job into a
2-hour one. Optimize the deck, then measure it.

**The tractability guard must protect the d<=5 ladder.** `--never-condemn-at-or-below` defaults to 0
and the queue driver never passed it; it now defaults to 5. The H cells ARE the crossover — they
decide which evaluator escalation uses at each rung it climbs to — so condemning one leaves a hole in
the answer rather than saving cost. **Since resolved by removing the choice:** condemnation at d<=5 is now impossible
(`scripts/valueleaf.sh` clamps the guard and the matrix refuses a value below 5), and the recovery
helpers were deleted because there is nothing left to recover from. The trap they existed for is
worth remembering: a capped cell sat at `games == reference_target`, so `needs()` was false, so it
was never rescheduled, so the condemnation check never re-ran -- raising the guard on a resume did
nothing by itself.

## Optimization work-list — the 17 pathological games of the matrix run (2026-08-08)

The user asked for every terrible-performance game to be kept for a later optimization pass. These
are the games the value-leaf matrix reported over the 30 s threshold. They are the whole input to
that pass: this deck's cost is concentrated in a handful of games, so a fix that only moves the mean
is not the fix worth making.

Every one is on the **H arm** (pure heuristic, fixed depth) — the V arm never produced a slow game at
any depth, which is the same story the cost table above tells, seen from the tail instead of the mean.
All finish (`wt` 4-6); they are expensive, not hung.

Reproduce with the matrix's own invocation — the repro line from the log plus the cell's depth:

```
build/Release/mtg decks/FiveColour/FiveColour.cod --seed <S> --game-index <GI> --games 1 \
  --max-turns 8 --threads 1 --ignore-play-profile --depth <D> \
  --profile decks/FiveColour/FiveColour.profile.json
```

| cell | ms | repro seed / gi | win turn |
|---|---|---|---|
| H3 s8008 | **218,685** | 8122 / 14 | 5 |
| H4 s9009 | 152,668 | 9078 / 19 | 5 |
| H3 s11011 | 151,311 | 11079 / 18 | 5 |
| H4 s9009 | 117,415 | 9075 / 16 | 5 |
| H3 s11011 | 91,410 | 11077 / 16 | 5 |
| H4 s9009 | 81,146 | 9065 / 6 | 5 |
| H3 s8008 | 78,462 | 8167 / 9 | 5 |
| H4 s9009 | 55,284 | 9074 / 15 | 6 |
| H4 s9009 | 49,190 | 9077 / 18 | 5 |
| H3 s8008 | 48,419 | 8124 / 16 | 5 |
| H3 s11011 | 44,922 | 11099 / 13 | 5 |
| H3 s8008 | 40,415 | 8161 / 3 | 5 |
| H4 s9009 | 37,570 | 9062 / 3 | 5 |
| H3 s11011 | 38,182 | 11085 / 24 | 4 |
| H3 s11011 | 35,192 | 11074 / 13 | 5 |
| H3 s8008 | 33,121 | 8179 / 21 | 4 |
| H3 s11011 | 32,295 | 11103 / 17 | 5 |

The worst is 3.6 minutes for ONE game at depth 3 — where the cell mean is 55 s and the deck's shipped
play is ~1.4 s. Two open questions to settle before optimizing, both cheap:

1. **Are they slow in PLAIN play, or only under this fixed-depth arm?** Re-run each without
   `--ignore-play-profile --depth`. If they are fast at shipped play, the tail belongs to deep fixed
   depth and matters for generation cost only; if they are slow both ways, it is a play-cost bug.
2. **Is it the same atom as the earlier tail work?** Sections above traced the tail to the mana
   backtracker and to Lightning Greaves. Check `MTG_ENUM_STATS` / backtracker entries on the worst
   game before assuming a new cause.

**Instrument bug fixed while collecting these.** `SLOW_GAME_LOG` was derived from `VLQ` at the top of
`scripts/valueleaf.sh`, before single-deck mode reassigns `VLQ` to `logs/vlq_<deck>` — so every
single-deck run appended to the shared fleet file. The FiveColour run's slow games were in
`logs/vlq/slow_games.log` while its own queue dir showed none, which reads as "no slow games" rather
than "wrong file". The derivation now happens after `VLQ` is final, next to `MATRIX_TXT`, which was
already correct for the same reason.

### ANSWERED (2026-08-08) — the tail belongs to UNBOUNDED search, not to these games

Question 1 above turned out to be the whole question, and the answer changes what the work-list is.

The matrix H cells run **unbounded**: `--depth N` with no `--budget-ms` (its own header says
"DEPTH MATRIX (UNBOUNDED, ...)"). The regression suite never does that — every case carries a
per-decision budget (d3=10, d5=20), and this deck's standing **1.403 s/game is a d3/b10 number**. So a
"333 s d3 game" and a "1.4 s d3 game" were never the same configuration.

Each of the six worst games, re-run on one core in both arms (`logs/fc_tail/probe.log`):

| seed / gi | d | matrix ms | unbounded ms | budgeted ms | ratio |
|---|---|---|---|---|---|
| 8139 / 6 | 3 | 333,190 | 351,956 | **659** | **534x** |
| 8133 / 0 | 3 | 306,590 | 347,558 | 2,311 | 150x |
| 8122 / 14 | 3 | 218,685 | 253,519 | 2,131 | 119x |
| 8152 / 19 | 3 | 155,980 | 172,206 | 3,125 | 55x |
| 9078 / 19 | 4 | 152,668 | 173,920 | 3,930 | 44x |
| 11079 / 18 | 3 | 151,311 | 160,290 | 4,962 | 32x |
| **total** | | | **1,459 s** | **17.1 s** | **85x** |

Every unbounded reproduction lands within 6-16% of the matrix's own figure, so the repro is faithful
and the effect is not load. And **every budgeted arm is under 5 s** — the single worst game of the
entire run costs 0.66 s at the configuration the deck actually ships and is tested at, *below* the
1.403 s deck mean. Under a budget these games are not a tail at all.

**So this is a GENERATION-cost item, not a play-cost bug.** Nothing here argues for an engine fix:
optimizing the deck would not have made phase C meaningfully cheaper, because what phase C is paying
for is the absence of a budget, not slow play. The earlier "optimize the deck, then measure it"
sequencing lesson was aimed at the wrong target.

**The H arm stays unbounded — that is settled** (user, 2026-08-08): the table has to say how useful
that depth *is* for escalation, and under a budget the comparison would confound evaluator quality
with throughput (the cheap leaf buys more nodes for the same budget and would win partly for that
reason). The real question is the one the user asked next: other decks measure the same unbounded
ladder far faster, so why is this one so expensive?

### Why FiveColour's unbounded ladder costs what it does

Unbounded heuristic ms/game, every deck that has a matrix log:

| deck | H1 | H2 | H3 | H4 | H5 | H5/H3 |
|---|---|---|---|---|---|---|
| burn | 0.3 | 1.4 | 6.7 | 12.3 | 21.1 | 3.1x |
| TH | 2.2 | 4.0 | 10.5 | 25.8 | 56.1 | 5.3x |
| knights | 1.1 | 2.7 | 11.3 | 38.2 | 36.8 | 3.3x |
| slivers | 0.5 | 3.9 | 19.7 | 50.8 | 73.3 | 3.7x |
| antilife | 10.2 | 11.7 | 21.4 | 53.2 | 115.6 | 5.4x |
| dragonstorm | 250.9 | 1,493.9 | 4,445.8 | 16,025.3 | 37,538.7 | 8.4x |
| creature_giving | 132.3 | 1,120.2 | 7,963.4 | 29,022.9 | 61,979.6 | 7.8x |
| **fivecolour** | **542.0** | **5,925.7** | **36,489.2** | **161,447.4** | **324,585.2** | **8.9x** |

Two tiers, not a continuum: five decks finish H3 in 7-21 **ms**, three take 4.4-36.5 **s**. FiveColour
is the worst, but only 4.6x creature_giving and 8.2x dragonstorm — the worst of a group, not a
category of its own.

> **CORRECTION (same day).** The cheap-tier columns of that table are **not comparable** to the
> FiveColour row, and the cross-deck ratios first derived from it (1,807x at H1, "97% of the gap is
> per-decision") were wrong. Those logs predate the 2026-07-31 change that started attaching
> `--profile` at all, so they measure a deck with no keep model and no card scores. Re-measured on
> ONE binary with the profile attached for both decks, burn's H3 is **193.8 ms**, not 6.7 — a 29x
> difference in the baseline alone. Never mix matrix logs across that change.

### The same comparison, measured properly

Same binary, same flags, `--profile` attached to both (`logs/fc_prof/ladder.txt`):

| deck | H1 | H2 | H3 | H3/H1 |
|---|---|---|---|---|
| burn | 7.2 ms | 41.8 ms | 193.8 ms | 27x |
| fivecolour | 265.6 ms | 1,846.6 ms | 18,550 ms | 70x |
| **fivecolour / burn** | **37x** | **44x** | **96x** | |

So the gap is **both** things, and it *grows* with depth: 37x of it is per-decision cost, already
present at depth 1 where there is barely any search; the remaining 2.6x by H3 is FiveColour's search
growing faster (70x vs 27x from H1 to H3). The earlier claim that depth contributed almost nothing
came entirely from the contaminated baseline.

**Callgrind says the per-decision half is not a hotspot — it is everything, uniformly.** At H1, with
build/Profile (Release codegen + symbols), FiveColour runs 943 M Ir/game against burn's 32 M. The
biggest single self-cost contributor to that gap is `TurnSolver::Solve` at 6.6% of it; nothing else
reaches 3%. The whole search does ~30x more work:

| function | x burn | share of gap |
|---|---|---|
| `TurnSolver::Solve` | 49x | 6.6% |
| `CollectActions` | 22x | 2.5% |
| **`TapForCostBacktrack`** | **387x** | 2.4% |
| `EffectiveSpellCost` | 33x | 1.9% |
| `operator new` | 23x | 1.8% |
| `EffectiveProduces` | 48x | 1.2% |
| `BuildSimKey` | 11x | 1.4% |

A flat profile where every function scales together is the signature of **more candidate plans**, not
of a slow routine — five colours of differently-restricted sources multiplied against the castable
set. `TapForCostBacktrack` is the one component whose ratio (387x) is far above the ~30x background,
which is what §3 already identified, but at H1 it is only 2.4% of the gap: fixing it alone will not
deliver 30x. The lever that would is **enumerating fewer plans**, and it moves generation and play
together (this deck is 1.403 s/game at d3/b10 where burn is 0.021 — a 67x ratio in the shipped
configuration, consistent with the 37-96x measured here).

## The optimization target, measured (2026-08-08) — 90.7% of enumerated lines can never be paid for

`MTG_ENUM_STATS` already instruments this; it just had not been pointed at the question. FiveColour,
depth 1, 3 games, profile attached:

```
enumeration calls          : 40480   (with a mana side: 6250)
odometer positions         : 2127694
mana-side combos KEPT      : 12998   (after the existing symmetry prunes)
  distinct exact           : 12740   (collapse 1.02x)
--- two-stage gating potential ---
payoff-side lines          : 197526   (unaffordable under EVERY mana line: 179103 = 90.7%)
(mana x payoff) pairs      : 406314
  pairs that are payable   : 24823   (6.1%)
  => two-stage visits      : 235347   vs flat 406314   (1.73x fewer)
```

Two findings, and the second is the big one.

**1. The mana side is FiveColour's alone.** Same instrument, same depth, `--profile` attached:

| deck | enumeration calls | with a mana side |
|---|---|---|
| burn | 19,716 | **0** |
| knights | 16,008 | **0** |
| fivecolour | 40,480 | **6,250** |

Burn and Knights never enter the mana odometer at all. So this is not "FiveColour does more of what
every deck does" — it is a code path only this deck takes, which is why `TapForCostBacktrack` shows a
387x ratio against a ~30x background.

**2. Nine tenths of the payoff side is enumerated and then thrown away.** 179,103 of 197,526
payoff-side lines (90.7%) are unaffordable under *every* mana line, and only 6.1% of (mana x payoff)
pairs are payable. The two-stage design the instrument was written to size is scored at 1.73x — but
that figure is dominated by the 197,526 payoff lines it still walks. Skip the 90.7% *before* pairing
and the visit count goes from 235,347 to roughly 56,000: **~7x rather than 1.73x**.

**The prune looks byte-identical, which is why it is the right first move.** Compute an OPTIMISTIC
upper bound on mana available across all mana lines (per colour and total), then drop any payoff line
that bound cannot pay for. A bound that never under-states available mana can only remove lines that
were genuinely unplayable, so play is unchanged and only speed moves. That also respects the standing
warning in `dragonstorm-ritual-afford-optimism-loadbearing`: the optimism the pre-scorer relies on is
*preserved* here, because we prune only what even the optimistic bound rejects — the opposite of
tightening the afford test, which measurably hurt Dragonstorm.

Depth compounds it: this is per enumeration call, and the same enumeration runs at every node, so a
7x at the leaf is why H3 is 96x burn while H1 is 37x.

**Measure it with counters, not the clock.** `MTG_ENUM_STATS` and callgrind `Ir` are both
load-independent, so this whole loop can be run while the box is busy with something else — which is
how it was done here, alongside a 20-worker matrix.

### Implementation note — and why this is NOT the refuted colour-aware gate

The row-skip test the instrument measures is already written out at `src/ai/TurnSolver.cpp:4469-4494`:

```cpp
int max_avail = pool_total;
for (int a : m_avail) { max_avail = std::max(max_avail, a); }   // best mana line, over ALL lines
...
if (need > max_avail) { ++p_rowskip; continue; }                // whole row dies in ONE test
```

`need` is a plain sum of `cost.ManaValue()` over the chosen payoff actions and `max_avail` is the
maximum total any mana line can produce, so the test is colour-blind and can only reject lines that
are unpayable under *every* assignment. Lossless by construction.

The 1.73x ceiling exists because the diagnostic still *walks* all 197,526 rows to test them. `need`
is monotone as actions are added, so the same bound prunes whole subtrees if the payoff side is
enumerated depth-first instead of as a flat odometer: the moment a partial line's cumulative
`ManaValue` exceeds `max_avail`, every extension of it is unpayable and never needs generating. That
is what turns "test 179,103 dead rows" into "never build them", and it is where the ~7x lives.

**This is a different lever from the refuted colour-aware B&B gate above.** That one added a
*colour-feasibility* check at the ROOT of `TapForCostBacktrack` and measured 0.28%, because the
backtracker is entered precisely when the colours are available and only the assignment is hard. Its
own conclusion was "any future attack has to prune INSIDE the tree, not at its root" — which is
exactly what this is: a *total-mana* bound pruning inside the PAYOFF odometer, a different structure
and a different quantity, with a measured 90.7% fire rate rather than a hypothesis.

Gate it the usual way (`MTG_PAYOFF_BB`, `EnvOn(..., true)` once measured) and verify byte-identity on
per-game win turns with `MTG_DUMP_WINS`, not on aggregate averages.

### "Enumerate the mana combinations up front" — already designed, and already measured HERE

That idea is `docs/design/exact-mana-enumeration.md` (deferred 2026-07-29): precompute the Pareto
frontier of achievable `ManaPool` vectors once per decision, then answer each plan's affordability by
lookup. It is **not implemented**, and it was deferred *because it measured as not a performance
lever* — 0.44-1.36% of runtime in the per-plan affordability lookup, with the conclusion "no richer
precomputation can beat 0.5%".

**But that measurement never included FiveColour** (treasure_hunt d3, Hinata2 d5, Dragonstorm d5), and
FiveColour is the only deck here that enters the mana odometer at all. So the ceiling it established
was measured on decks where the thing being optimised does not happen.

The good news is we do not have to re-litigate it: `MTG_ENUM_STATS` already simulates the up-front
design on this deck, and it scores the idea's two halves separately.

**The COLLAPSE half is refuted for FiveColour.** Deduplicating the up-front mana lines by what they
hand the payoff side:

| signature | distinct | collapse |
|---|---|---|
| raw combos | 12,998 | — |
| after existing symmetry prunes | 12,998 | **1.00x (they remove nothing here)** |
| exact (mana+storm+cards+colour) | 12,740 | 1.02x |
| + colours capped at demand | 12,740 | 1.02x |
| cards-spent identity dropped | 12,576 | 1.03x |
| storm dropped too | 12,500 | 1.04x |

Even the loosest signature collapses 1.04x. FiveColour's mana lines are genuinely distinct — different
cards spent, different colours produced — not duplicates waiting to be merged. Building a frontier to
deduplicate them buys at most 4%. (Same shape as the `MTG_SPARE_COPY_BAND` result: 2.08x on the deck
it was built for, 0.2% here.)

**The GATING half is the whole prize, and it does not need the enumeration.** The 90.7% row-skip needs
exactly one number — `max_avail`, an upper bound on the total mana any line can produce. An
*over*-estimate is sound (it only prunes fewer lines, never more), so it can be computed by maximising
each term independently instead of enumerating the lines and taking the max. You get the ~7x without
building the frontier at all.

So: right neighbourhood, and the instrument has already told us which half pays. Worth keeping the
frontier on the deferred list for the correctness reasons its own doc gives (it deletes `SubsetPayable`
and `SubsetPayableWithFilters`, net-negative LOC) — just not as the speed fix.

## RETRACTION (2026-08-09) — the two-stage gate is already SHIPPED; the ~7x was not real

User: *"Don't we already have a maximum mana prune?"* Yes. Both of them, and the whole-row skip too.

`EnumeratePlanPositions` (`src/ai/TurnSolver.cpp:4557+`) already implements exactly the design the
sections above proposed:

- **Stage A** enumerates the mana side once into `mlines`, then computes `best_head` — the most spare
  mana any single line leaves after paying for itself.
- **Stage B** enumerates the payoff side and applies the whole-row skip, in the code and commented as
  such: *"if the most generous mana line cannot fund this payoff line, no pair can, so drop it here
  instead of re-rejecting it against every mana line"* —
  `if (!any_block && L.agg.cost > gate->pool_total + best_head) { continue; }`
- **Pairing** then applies the exact per-selection gate `payable(M, P)`.

And the bound itself is the selection-exact `ManaGateIndex` (`MTG_SEL_MANA_GATE`, default ON since
2026-07-30), which is strictly better than the whole-list `ManaPruneBound` scalar it superseded.

**So the error was reading the diagnostic as a description of the engine.** `MTG_ENUM_STATS`'s
two-stage block measures a *flat odometer* baseline — its header says so ("Measures the CEILING of the
two-stage design **before committing to it** … delete once the design lands"). The design landed. The
"179,103 payoff lines (90.7%) unaffordable under every mana line" is therefore **what the shipped
row-skip already catches**, not waste sitting in the hot path, and the 1.73x is banked, not available.
`MTG_PAYOFF_BB` should not be implemented; it exists.

The one residual is negligible: Stage B still *builds* a row before dropping it (push digits, fold the
agg, test, `continue`). A DFS with an incremental bound would skip the fold, worth roughly 3M of
943M Ir/game — 0.3%. Not a lever.

**The diagnostic should be deleted or relabelled**, per its own instruction. Leaving a measurement of a
superseded baseline in the tree is what produced this mistake.

### What the numbers actually say, with that removed

| | fivecolour | burn | ratio |
|---|---|---|---|
| Ir / game (H1) | 943 M | 32 M | **29x** |
| rollout calls / game | 2,871 | 179 | **16x** |
| turn steps / game | 5,141 | 305 | **17x** |
| ⇒ work per node | | | **~1.8x** |

The deck runs **16x more search nodes** and each node costs ~1.8x. That is the whole gap, and it is
why the callgrind profile is flat: `Solve` 49x, `CollectActions` 22x, `EffectiveSpellCost` 33x,
`EffectiveProduces` 48x — everything scales with the node count. Even zeroing BOTH mana-side entries
(`TapForCostBacktrack` 2.4% + `EffectiveProduces` 1.2%) removes ~4% of the gap.

**There is no hotspot left to fix.** A large win has to come from running fewer nodes, which is a
search-shape change and therefore play-affecting — not the byte-identical freebie the previous
sections claimed. The honest remaining candidates, in order of measured support:

1. **`DomainColors` memoisation** (§3, never refuted): Faeburrow Elder / Bloom Tender / Deathrite make
   `EffectiveProduces` rescan the battlefield inside the backtracker's recursion. Byte-identical, but
   sized at ~1-3% by the profile above — worth doing, not worth expecting much from.
2. **Fewer nodes.** 16x is the number that matters. Play-affecting, needs the full A/B and a
   rebaseline, and would invalidate a value-leaf table generated before it.

## Which branches are heaviest (2026-08-09) — one card is 72% of the odometer

`MTG_BRANCH_STATS`, depth 1, 3 games, profile attached. By driver card (biggest option-group):

| driver | calls | sum_odo | share | avg_odo | max_odo |
|---|---|---|---|---|---|
| **Unite the Coalition** | 1,831 | **146,482** | **72%** | 80.0 | 672 |
| Mana Cannons | 987 | 29,908 | 15% | 30.3 | 256 |
| Two-Headed Hellkite | 277 | 7,968 | 4% | 28.8 | 756 |
| Deathrite Shaman | 107 | 6,496 | 3% | 60.7 | 128 |
| Faeburrow Elder | 119 | 4,640 | 2% | 39.0 | 64 |
| everything else | — | 7,532 | 4% | — | — |

**Unite the Coalition is `{2}{W}{U}{B}{R}{G}` — 7 mana, one of every colour** — and it is modelled as a
searched split S in [0..5] (the user-approved 2026-08-06 collapse), so it contributes a **6-option
group**: a 7x multiplier on the whole odometer product.

The user's intuition was right, and the shapes confirm it directly. This is the **first enumeration of
the game**, on an opening board (Birds of Paradise, Lightning Greaves):

```
[enum-stats] bound=112 groups=5 [g1 Oko] [g6 Unite the Coalition] [g1 Birds of Paradise]
                                [g1 Lightning Greaves] [g1 Lightning Greaves(equip)]
```

112 = 2 x **7** x 2 x 2 x 2. Without the Unite digit it is 16. A seven-mana five-colour instant is
multiplying turn-one branching by 7x, on a board that cannot produce two colours yet.

### The available trim, honestly sized

A candidate whose OWN `ManaValue` already exceeds `pool_total + best_head` (the most generous budget
any mana line can produce) can never appear in a payable selection — costs are additive and
non-negative — so it should never become an odometer digit. That shrinks the **product**, where the
existing whole-row skip only avoids re-testing a row against every mana line. Soundness guards are the
ones the row-skip already uses: apply only to payoff-side actions with `gain == 0, gy == 0, block == 0`.

**But it is worth ~0.3%, not 7x.** Stage B row-building is cheap: 65,842 rows/game at roughly 50
instructions each is ~3.3 M of 943 M Ir. Dropping six sevenths of them saves about what the DFS variant
saves, and for the same reason — the rows being dropped were already being dropped, just slightly later.
Do it because it is free and obvious, not because it moves the number.

### Where the 16x actually is

| per game | fivecolour | burn | ratio |
|---|---|---|---|
| EnumeratePlans calls (decision points) | 1,239 | 178 | **7.0x** |
| final plans | 5,695 | 304 | **18.7x** |
| final plans per call | 4.6 | 1.7 | 2.7x |
| rollout calls | 2,871 | 179 | 16.0x |

The cost is 7x more decision points, each yielding 2.7x more **payable** plans. Those plans are not
waste — they pass the exact gate, and each one gets rolled out. Trimming them means dominance pruning
or better ordering among genuinely castable lines, which is **play-affecting**: a full A/B, a
rebaseline, and it invalidates any value-leaf table generated before it. There is no version of this
that is byte-identical.

### Correction to that table — everything below Unite the Coalition is an attribution artifact

`MTG_BRANCH_STATS` attributes a call's ENTIRE odometer to one card (`TurnSolver.cpp:10077-10082`):

```cpp
int max_opts = 0; std::string driver = "(casts<=1)";
for (const std::vector<int>& gp : groups)
    if ((int)gp.size() > max_opts) { max_opts = gp.size(); driver = cands[gp[0]].card_name; }
```

The comparison is strict `>`, so **the FIRST group of maximal size wins**. When every group has one
option — the normal case — the "driver" is just whichever card happens to be enumerated first. It is
not the cause of anything.

**Mana Cannons cannot be a branching driver at all.** It is a `{2}{R}` enchantment whose only parameter
is `multicolor_cast_damage_per_color`; it has no modal split, no X, no target choice, so it emits
exactly ONE `CastFromHand` action and its group size is always 1. Its 29,908 sum_odo is 100%
misattribution: it is cheap and castable early, so it is a frequent candidate and often lands first in
`groups`.

The giveaway is in the number itself — avg_odo 30.3 ~= 2^5, exactly the product of five independent
size-1 groups. That is the combinatorics of "five castable things", not anything Mana Cannons does.
Two-Headed Hellkite (28.8), Faeburrow Elder (39.0) and Deathrite Shaman (60.7) read the same way.

**Only the Unite the Coalition row survives**, because g6 wins the driver slot on merit (6 > 1). So the
corrected picture is:

- **Unite the Coalition contributes a x7 factor** — real, and it is the only card-specific one.
- **Everything else contributes x2 each**, and the rest of the branching is `2^n` in the number of
  simultaneously castable options. That is the irreducible combinatorics of a toolbox deck, and it is
  exactly why burn — which rarely has five different castable things at once — is cheap.

Fix the instrument before quoting it again: record ties as ambiguous, or attribute the product to the
groups that actually contribute a factor > 1. As written it invites precisely the misreading above.

## Why an uncastable card is still a decision (2026-08-09)

User: *"we usually can't cast Unite the Coalition until later in the game and it should be immediately
pruned"* — *"I just don't understand why this is even something to think about when we can't cast it."*

**There is no affordability filter at candidate emission.** `CollectActions` emits a `CastFromHand`
action for every hand card regardless of cost; affordability is handled downstream by the mana gate,
which prunes **selections**, not **digits**. So an uncastable card still becomes an odometer digit and
still multiplies the product. That is a design choice — push the test to one exact place — not an
oversight, but it leaves provably dead digits in the odometer.

The turn-one shapes show how far it goes. Second enumeration of the game:

```
[enum-stats] bound=672 groups=7 [g6 Unite the Coalition] [g1 Two-Headed Hellkite]
             [g1 Maelstrom Archangel] [g1 Nicol Bolas, Planeswalker] [g1 Bloom Tender]
             [g1 Lightning Greaves(equip)] [g2 Oko, Thief of Crowns]
```

Unite is 7 mana WUBRG; Nicol Bolas is 8; Maelstrom Archangel 5; Two-Headed Hellkite 6+. On an opening
board none of them can be cast. Drop those four digits and the product is `2 x 2 x 3 = 12` instead of
`7 x 2 x 2 x 2 x 2 x 2 x 3 = 672` — **56x on that call**, and it is not Unite alone: it is x7 from
Unite times x2 for each uncastable fatty.

### The fix, and its honest ceiling

In `CollectActions`, skip a hand cast whose `EffectiveCost(def, state).ManaValue()` exceeds an
OPTIMISTIC bound on the mana the board could produce this turn. Optimistic keeps it sound: it can only
drop cards that were unplayable under every line, so play is unchanged. It generalises past Unite to
every expensive card in every deck.

**But it will not deliver 56x of runtime, and the earlier "~0.3%" was a lower bound measured on the
wrong term.** That figure counted only Stage B's row walk; the fix also removes 6-9 `Action`
constructions per call (each with a `card_name` string copy — `operator new` is 1.83% of total and 23x
burn's), their `ManaGateIndex` terms, and their group partitioning. Low single digits is the realistic
range, and it needs measuring rather than estimating — twice tonight an estimate from an instrument's
framing has been wrong.

The reason it CANNOT be large is structural, and it is the same reason the two-stage retraction landed:
`sum_final` (5,695 plans/game) and rollout calls (2,871/game) are **unaffected** by pruning lines that
were never payable. Those dominate, and they are where the 16x node gap lives. Pruning dead digits
makes the cheap part cheaper.

### The separate, larger lever: decide the split instead of searching it

The user's other suggestion — *"we could probably come up with a heuristic for Unite the Coalition"* —
is the one with real headroom, and this file already contains the precedent. **Ponder was the #1
branching source at ~47% of all enumeration**, and the fix was to emit ONE variant chosen by heuristic
instead of the searched alternatives (`TurnSolver.cpp:2932+`, and the 2026-08-01 cost fix below it).

Unite's six variants all carry the SAME cost and are strictly ordered by static eval
(300/440/580/720/860/1000 for S=0..5), so on the turns it IS castable the search is rolling out six
same-cost lines that differ only in a damage-vs-draw trade against a goldfish. Measure which S the
search actually commits to; if it concentrates at the ends, emit two variants (all-damage, all-draw) or
one heuristic pick. That cuts x7 to x3 or x2 exactly where the payable plans are — which is the half
that pruning cannot touch. Play-affecting: full A/B, rebaseline, and it invalidates a value-leaf table
generated before it.

### The precondition holds for this deck — verified

User: *"If there are no cost reducers or rituals our model should be much simpler. If we can't cast a
spell on its own, we cannot cast any plan that includes it."*

That is exactly the soundness argument, and it is the one the existing gate already relies on. Scanned
all 32 distinct cards in `decks/FiveColour/FiveColour.cod` against `cards.json` for anything that adds
mana or reduces cost:

| class | present? |
|---|---|
| rituals (`ritual_floating_mana`, `ritual_float_gy_self_bonus`) | **none** |
| cost reducers (medallion / affinity / delve / convoke / improvise) | **none** |
| mana rocks | **one** — Ancient Cornucopia (`mana_rock`) |

So the guard conditions are trivially satisfied here: `block` (per-subset discount) never fires because
no card has one, and the single `gain` source is already credited by `best_head`, which is the maximum
`gain + Tri(gy) - cost` over all mana lines. `cost.ManaValue() > pool_total + best_head` therefore
proves unplayable-in-any-plan for this deck, exactly as stated.

Costs are additive and non-negative, so the theorem generalises: it needs only that no *selected*
action can raise the budget (`gain`/`gy`) or lower another's cost (`block`) — which is why the check
must stay guarded rather than assumed, for decks like Dragonstorm that are all rituals.

**Where it does and does not pay.** Per game the enumeration walk is ~4,333 mana lines + ~65,842 payoff
rows + ~8,274 payable pairs = ~78k visits, against 943 M Ir/game total and **5,141 simulated turn
steps**. Even at 300 Ir per visit the whole walk is ~2.5%. The model is *already* simple where it
counts — the gate keeps 5,695 payable plans out of 67,675 odometer positions — and what costs 943 M is
simulating those 5,695 plans' futures. Pruning dead digits cannot change that number, by construction:
the plans it removes were never payable and were never rolled out.

So: implement it (sound, general, and the deck's precondition is verified), expect low single digits,
and keep the rollout count as the number to attack for anything larger.

### The correct predicate is NET mana, and the user's four cases map onto the engine's two guards

User's refinement: a mana source or reducer can only enable an otherwise-uncastable spell if it adds
**more than it costs**. Their exhaustive list of what actually reduces a turn's total cost:

1. **Skirk Prospector with goblin fodder** — repeatable, no tap, sacrifices bodies already on board
2. **Sol Ring** — `{1}` for `{C}{C}`, net +1 the turn it lands
3. **Rituals** — Dark Ritual and friends
4. **Aether Vial + cost-reducing creatures** — cheats the reducer in without paying for it

Checked against the code, and the mapping is exact — the engine already models all four with two terms:

| case | engine term | verified |
|---|---|---|
| Skirk Prospector | `ritual_float` (`TurnSolver.cpp:3597`, "credited by Solve") | `IsManaSideAction` = `ritual_float > 0 \|\| rock_mana.Total() > 0` |
| Sol Ring | `rock_mana` (`produces_amount: 2`, cost `{1}` → net +1) | same |
| rituals | `ritual_float` | same |
| Vial + reducers | `block` (per-subset discount disables the gate) | `payable()` returns true when `block > 0` |

So the prune needs **no new machinery** and no new escape hatch: the guards it must respect already
exist and already cover every case on the list. Worth noting Skirk is neither a rock nor a ritual —
it is `sac_creature_outlet` — so a guard keyed on "is it a ritual or a mana rock" would have missed it.
The net-mana framing catches it; the parameter-name framing does not.

**The refinement the code does NOT yet exploit.** `IsManaSideAction` tests `gain > 0`, not
`gain > cost`. Ancient Cornucopia is `{2}{G}` and taps for **one** mana of any colour — **net −2** on
the turn it lands, so it can never fund anything that turn — yet it is classified mana-side and gets a
mana-side group. Classify by net instead and **FiveColour has zero budget-raising actions**, so the
strongest form of the prune applies unconditionally to this deck.

(The *bound* is already net-correct: `best_head = max over lines of gain + Tri(gy) - cost`. Only the
*classification* is gross rather than net. Reclassifying moves a card between stages and so changes
enumeration order — verify byte-identity on per-game win turns, do not assume it.)

### The hole in "provably lossless": domain dorks scale with what you PLAY this turn

User: *"the dorks on the field (Bloom Tender, Faeburrow Elder) can add more mana based on what we
play."* This is the case that breaks the naive form of the prune, and it is not covered by any term
above — it is neither `ritual_float` nor `rock_mana` nor `block`, but `EffectiveProduces` /
`DomainColors` recomputed live from the battlefield (§3).

**And it is net positive with ONE dork, not several — because the land drop is free.** With a Bloom
Tender already on board and untapped, playing a land that adds a colour it did not already see gives
`+1` from the land plus `+1` from the dork's widened domain: **+2 this turn, for no mana**. In a deck
with 11 fetchlands and a pile of duals/triomes, that is not a corner case, it is the normal curve. So
the guard cannot be "do we have multiple dorks"; it has to be "can a same-turn PLAY widen a domain
source".

Consequence for the prune: the bound must be computed over **the plan's land drop as well**, not just
the current pool. `ManaGateIndex.pool_total` is `AvailableManaPool(state).Total()` — a pre-land
snapshot — so an emission-time prune keyed on it would drop a spell that the land-then-tap line can
actually cast. Optimism keeps it sound: bound with the BEST available land drop and the largest domain
widening it could produce, and only prune what even that cannot afford.

**This bears on correctness, not just speed** — the user's second point: *"sometimes the play is to
play a rainbow permanent and then cast another with one of these dorks."* Both questions have the same
root, and it is a single testable one:

> Does the enumerator's mana projection account for a same-turn land drop's effect on domain sources?

If yes, the prune just needs the wider bound. If no, then the engine is *already* missing the
land-then-tap line, which is a play bug worth more than any of the performance work in this file —
this deck's whole manabase is built to enable it. Construct the state directly (Bloom Tender untapped,
a land in hand adding a new colour, a spell castable only with the extra mana) and check whether the
line is offered, before implementing any prune on top of it.
