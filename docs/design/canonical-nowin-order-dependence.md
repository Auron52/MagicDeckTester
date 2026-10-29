# Canonical no-win sharing is not order-free (found 2026-10-29; fix built and measured the same day -- `MTG_NOWIN_ORDER_EXACT` + `MTG_NOWIN_TWIN_WAVE`, both default ON -- adoption is the USER's call, see Open)

## What

`FSLineWin` stored and served NO-WIN answers under the CANONICAL sim key (`MTG_CANON_SIMKEY`: hand and
battlefield folded as multisets), in both the per-decision `FSLineCache` and the game-persistent line cache
(`MTG_GAME_LINE_CACHE`). Only WIN lines carried the order-exact key (`MTG_FSL_SPLIT_KEYS`: `key_win` folds
`FsOrderSig`). The lookup stated the premise: *"NO-WIN on the canonical key: order-free and sound (no line;
the bound is a property of the multiset)"*.

The premise is false, because the search that produced the refutation reads zone order: the enumeration
emits candidates in hand / battlefield order, apply-time picks break ties by vector position, triggers walk
the battlefield in order, a breakpoint continuation's RANK follows the same order, and the greedy horizon
rollout (`SimulateToEnd`) reads vector order at every tie-break. So a zone-permuted twin can have no win in
reach while this position has one, and the twin's refutation was served here: a line this position's own
search finds, pruned at EVERY budget, unlimited included.

## The witness

Goblins seed 1330145 gi144, `--depth 8 --budget-ms 0`, `MTG_SUBSET_SAC_OUTLETS=1 MTG_BP_DEV_SEQ=0`
(`MTG_PAY_REFINE=1`), on 17779dce0 (the commit that carries both levers): T4. Each of these alone restores T3:
`MTG_CANON_SIMKEY=0`; `MTG_GAME_LINE_CACHE=0 MTG_FS_NOWIN_CACHE=0` together (either alone: still T4 -- each
cache serves the same stale refutation); order-exact no-win keys (the fix below). (`MTG_BP_DEV_SEQ`, which ships
ON in that commit, recovers the game by another line; the hole is independent of it.)

### The order-dependent site, pinned

`MTG_NOWIN_FSL_VERIFY_DUMP=1` (below) prints both zone orders of a wrong hit and re-searches BOTH fresh. All
four cross-order wrong hits are the T3 node after T2's Lackey put Siege-Gang Commander in, and the two orders
differ only in where Skirk Prospector sits:

```
query : bf=[opp 1/1, Mountain#24, Lackey#9, Mountain#25, Lackey#10, Siege-Gang#52, 3 tokens, Skirk#56]   fresh T3
stored: bf=[opp 1/1, Mountain#24, Lackey#9, Mountain#25, Skirk#56, Lackey#10, Siege-Gang#52, 3 tokens]   fresh no-win
```

(hands identical: Goblin Chainwhirler, Siege-Gang Commander, Rundvelt Hordemaster, Mountain). The twin's own
fresh order-exact search really has no T3 win, so this is genuine order dependence, not a stale cache. The
fifth wrong hit is the T2 node above it, an exact transposition whose own refutation was built on the T3 twin's:
canonical sharing pollutes same-order entries one level up.

Both orders enumerate the same 10 main-1 plans in the same order; the one that kills is
`land=Mountain; Rundvelt Hordemaster` with the searched Lackey pick "put Goblin Chainwhirler" (`MTG_FSW_TRACE`,
`MTG_LACKEY_TRACE`, cut per re-search with `MTG_NOWIN_FSL_VERIFY_TRACE=1`). It realises differently:

* `FireCombatDamageCheatIntoPlay` (`SpellEffects.h`) walks `damaging_attacker_indices` -- battlefield POSITIONS
  captured before the loop. Lackey#9's trigger takes the searched pick and puts Goblin Chainwhirler; its ETB
  deals 1 to each opposing creature and kills the opponent's 1/1 at position 0, so every later position
  shifts down by one.
* Query order: Lackey#10's stale position now names Siege-Gang#52, which is not a Lackey, so the second
  trigger is silently dropped; Siege-Gang Commander stays in hand, and main 2 casts it off Skirk mana for the
  last 3 damage: T3.
* Twin order: Skirk sits ahead of Lackey#10, so the stale position lands on Lackey#10 and its trigger puts
  Siege-Gang Commander from hand (the provider's default): nothing to cast in main 2, no T3.

So the T3 line exists in BOTH positions in the real game (Lackey's put is a "may": decline the second one), and
the engine reaches it in one order only because a stale index happens to drop the second trigger. Two engine
defects surface here, both outside this fix (see Open): the stale positions (a damaging Lackey's trigger can be
lost, or fire for the wrong Lackey), and the Lackey's decline is not a searched option.

What it is NOT: `MTG_BP_UNIFORM_DEV=0` leaves both arms as they were (order-exact T3, canonical T4), and a
diagnostic that ranks Skirk Prospector after equal-power Goblins in `CanonicalSacVictim` (whose strict `<`
keeps the first battlefield position on a tie) also leaves canonical at T4 with the same 5 wrong hits.

## Why option 2 (an order-invariant search) cannot be made sound by construction

Option 2 asked for the search to be order-invariant wherever it feeds a stored no-win, so the canonical key
would become true. A no-win is the outcome of three order-reading layers, and every site in each would have to
change:

1. **The apply engine.** The witness is a trigger loop over battlefield positions; `CanonicalSacVictim` and
   `SacOutletVictimCandidates` break expendability ties by position; payment sources among equals, attach
   hosts and "first matching permanent" picks do the same. They are shared by the executor and the rollout,
   so each is a lockstep play change in its own right.
2. **The greedy horizon rollout.** Every leaf below a no-win (unless a value model is attached and not forced
   off) is a `SimulateToEnd` playout that reads vector order at every tie-break -- measured, not hypothesised:
   the rollout TT was made order-exact on 2026-08-14 when leaf-verify caught `cached=8 fresh=7` on Mirrorwing
   gi=363. A no-win built on such leaves inherits their order, and the instrument below finds exactly that
   on the suite: the shipping-settings wrong hits on Hinata and Bruna are wins BEYOND the node's horizon
   (turn 4, depth 1, fresh win T7 / T5), i.e. leaf rollouts that win in one order and not the other.
3. **Enumeration order and ranks.** Breakpoint continuation ranks, provider candidate order, rank-gated
   admission (`MTG_BP_MAXBASE`, the width W) follow hand / battlefield order.

Even a complete rewrite would only make twins CONSISTENT, not complete: on the witness a canonical order picks
one of the two realisations for both twins, and whether T3 survives is then a coin. So the fix is option 1
(sound by construction), plus the budget-staged twin reuse that recovers its cost at finite budgets, plus the
instrument (option 3) that measures what the canonical key was getting wrong.

## The instrument (option 3): `MTG_NOWIN_FSL_VERIFY`

DIAGNOSTIC, default OFF; never changes the answer the search returns. On every NO-WIN hit of either cache
(every non-root node) the position is re-searched FRESH at the same depth and cutoff: an empty `FSLineCache`, no
game line cache reads or stores, no rollout TT, every no-win in the re-search's own subtree keyed order-exactly
(so it cannot be answered by a twin either), its own budget object (unlimited when the query's is, otherwise a
limited-but-unreachable one so no unlimited-only path opens), and the decision / game meters disarmed. Every
counter the enclosing search reads back (truncations, binds, meters, leaf tallies) is restored afterwards and
the CACHED answer is returned as always, so the enclosing search is untouched: 8 decks x 10 games x 2 arms at
shipping settings, every play digest identical with the verify on and off.

A fresh WIN inside the query's cutoff and the horizon is a WRONG hit; a fresh no-win from a search nothing cut
is an agreement; anything else is unverified. Each hit is also classed CROSS (the entry was last stored by a
different zone order) or SAME, using a signature finer than `FsOrderSig` (it adds each position's tapped /
entered / damage / counter state), so under the fix a CROSS hit could only be an attribute permutation among
same-named copies that `FsOrderSig` cannot see (none occurred in the runs that used it). Knobs: `MTG_NOWIN_FSL_VERIFY_EVERY=N` (1 hit in
N, by key hash), `MTG_NOWIN_FSL_VERIFY_CAP=U` (a re-search stops at U units, counted unverified),
`MTG_NOWIN_FSL_VERIFY_DUMP=1` (both zone orders of a wrong hit and the stored twin's own fresh search;
single-game repros), `MTG_NOWIN_FSL_VERIFY_TRACE=1` (BEGIN/END markers around both re-searches, so the search's
trace streams can be cut out per order), `MTG_NOWIN_FSL_VERIFY_MODE` (attribution bitmask: 1 the query's TT,
2 a copy of the query's budget, 4 game-cache reads inside the re-search). Per-job totals print at exit as
`[nowin-verify] job=...`.

### Why `MTG_GAME_LINE_CACHE_VERIFY` had no power, and the fix

It verified by FALLING THROUGH past the hit and comparing at the end-of-node store. `FSLineWin` has a dozen
returns between the two, above all the per-decision `FSLineCache` lookup right below the game cache's, which
held the SAME canonical no-win -- so on the witness all 267 no-win hits returned early and were counted
"unverified". It now re-searches by RECURSION: the hit calls `FSLineWin` on the same node with both caches
bypassed at that node (`gamelc::t_bypass_node`; the subtree reads its caches normally), compares right there and
plays the fresh answer as before. On the witness (canonical keys): `verified=13 DISAGREE=1 MATERIAL=1` (kind 4:
cached no-win, fresh T3), and the game plays T3 because verify mode plays the fresh answer.

### Instrument numbers (upstream 1d78d3511 + the instrument)

**Shipping settings** (seed 1480001; every suite deck at its shipped play setting -- depth from its `value_play`
block, its shipped budget -- 300 games per arm; canonical arm = `MTG_NOWIN_ORDER_EXACT=0`, the upstream behaviour):

| | canonical (old) | order-exact (fix) |
|---|---|---|
| no-win hits | 1,366,399 | 691,005 |
| of which cross-order | 839,656 (61%) | 0 (by construction) |
| WRONG (fresh search wins inside the cutoff) | **290** (258 cross, 32 same) | **2** (same; see Open) |
| unverified (re-search cut) | 49 | 4 |

All hits are on the game line cache: at shipping settings the per-decision cache took none, because the game
cache is consulted first and holds the same answers. The canonical wrong hits by deck: Hinata 251 (219 cross,
32 same -- the same-order ones are pollution, a refutation built on a twin's beneath, like the witness's T2
node), Creature Giving 36, Bruna 1, Colossus Hammer 1, EDF 1; the other 34 decks 0. So **yes, upstream has
cases without the outlets lever**. The ones printed (Hinata, Bruna) are 1-ply nodes (turn 4, depth 1) whose fresh
win (T7 / T5) lies BEYOND the node's horizon, i.e. a leaf rollout that wins in one zone order and not in the other.
Rate: 0.021% of canonical no-win hits. (This run classed CROSS by `FsOrderSig`; the finer signature came later and
was used for every run after it.)

**Unlimited budget** (`--depth 8 --budget-ms 0`, `abandon_units` 3M, seed 1500001: 16 cheaper decks x 4 games
per arm): canonical 23,927 hits, 19,306 cross, **0 WRONG**; order-exact 24,019 hits, 0 WRONG. A larger run on the
rebased tree (seed 1510001: 24 decks x 25 games per arm, the same caps): canonical 36,372 hits, 26,103 cross, **0
WRONG**; order-exact 32,806 hits, 0 WRONG, and 0 play changes between the arms. (Giants was dropped from that run:
one of its games at d8 b0 spent over 50 minutes in capped re-searches.) At an unlimited budget the canonical key
was rarely wrong in this sample; the witness is the rare kind.

## The fix

### `MTG_NOWIN_ORDER_EXACT` (heurarm `NOWIN_ORDER_EXACT`, DEFAULT ON; `=0` restores the canonical key)

`FSLineWin` computes one no-win key, `key_nw`: the canonical `key` with the lever off, the order-exact key
(`key_win` under split keys; `key` folded with `FsOrderSig` on the legacy single-key path) with it on, and always
order-exact inside a verify re-search. Every searched no-win store and read in both caches uses `key_nw`: the
per-decision cache's end-of-node store and its lookup (both key paths); the game line cache's no-win store,
widen and lookup (with `key_nw == key_win` a node's win and refutation share one game-cache entry, so a completed
in-cutoff win supersedes the narrower refutation, exactly as `FSLineStoreWin` already does per decision); and the
go-off-dominance cut's memo (`MTG_LABEL_GOFF_DOM`: its refutation is the seeds' failure to kill, and the seeds are
applies). Deliberately left canonical: the winless CERTIFICATE's memo (a proof about the position, audited by
`MTG_WINLESS_AUDIT`, valid for every zone order; read after `key_nw`), the provisional dead ends and the
order-free wave hints (budget-starved estimates booked as truncations). Search-only: the executor never reads
these caches, so rollout/executor lockstep is untouched.

### `MTG_NOWIN_TWIN_WAVE` (heurarm `NOWIN_TWIN_WAVE`, DEFAULT ON; `=0` searches every twin at every budget)

The order-exact key alone is sound but expensive: it re-searches every twin the canonical key used to answer
(61% of all game-cache no-win hits at shipping settings are cross-order), and under a fixed budget that work
comes out of depth -- the ladder commits a shallower pass and a few games slow down (A/B below). The wave buys
that back the way `MTG_FSL_OF_WAVE` buys back order-free WIN reuse (docs/design/order-free-reuse-wave.md): every
completed order-exact refutation also leaves a hint under the CANONICAL key holding its bound and the units its
search cost (in the per-decision cache and in the game cache), and a node that misses its own order's
refutation takes a twin's hint ONLY when

    twin_units > share * Remaining()        (MTG_NOWIN_TWIN_SHARE, default 0.0005 -- the OF wave's measured floor)

booked as a truncation, so no refutation resting on a twin's is ever stored as a proof. `Remaining()` grows with
the budget, so the reuse rate decays continuously to zero, and at an unlimited budget the gate never opens: the
search is exactly the order-exact one. That is the sanctioned shape for a lossy lever's win ("a budget may
re-price work, it may not make a line unfindable"): at the shipped budgets it behaves like the canonical
sharing, and every line it skips is findable with more budget. Inert with `MTG_NOWIN_ORDER_EXACT=0`.

## Witness results (17779dce0 + the fix, `--depth 8 --budget-ms 0`, `MTG_SUBSET_SAC_OUTLETS=1 MTG_BP_DEV_SEQ=0`)

| run | result |
|---|---|
| no fix (17779dce0) | T4 (267 game-cache no-win hits) |
| fix (order-exact, wave on) | **T3** (3 no-win hits, 0 cross) |
| fix, wave off | **T3** |
| fix, wave on, `--budget-ms 60` | T3 |
| fix, `MTG_NOWIN_ORDER_EXACT=0` | T4, byte-identical to no fix |
| `=0` + `MTG_NOWIN_FSL_VERIFY=1` | T4; 267 hits, 169 cross, 262 agree, **5 WRONG** (4 cross: twin fresh no-win -> ORDER-DEPENDENT; 1 same: polluted) |
| `=0` + verify + `MTG_GAME_LINE_CACHE=0` | T4; the same 267 / 5 WRONG, on the per-decision cache |
| fix + `MTG_NOWIN_FSL_VERIFY=1` | T3; 3 hits, 0 cross, 0 WRONG |
| `=0` + `MTG_GAME_LINE_CACHE_VERIFY=1` (fixed) | T3 (plays the fresh answer); verified=13 DISAGREE=1 MATERIAL=1 |
| `MTG_BP_UNIFORM_DEV=0`, fix / `=0` | T3 / T4 |

## Held-out A/B

Rebased onto 33c9189c7 (the Goblins echo fix and the breakpoint canon wave). One pooled batch on held-out seed
1490001: every suite deck (39) at its shipped play setting, 400 paired games per arm (`seed`/`game_index`
paired), `MTG_PGO=0` on every arm, `MTG_RSS_CAP_GB=43`, the order-exact-only arm and the canonical arm in one
batch, the wave arm on the same manifest. Loss-penalised win turn (unwon = 9); `units` = the budget's own
deterministic work units. "slower"/"faster" are per-game against the canonical arm.

**Totals (15,600 paired games):**

| arm | faster | slower | net | avg | units vs canonical |
|---|---|---|---|---|---|
| order-exact only (`MTG_NOWIN_TWIN_WAVE=0`) | 2 | 25 | **+25** | +0.0016 | **1.097x** |
| order-exact + twin wave (shipped) | 0 | 4 | **+4** | +0.0003 | **1.010x** |

**Per deck** (net / units ratio; decks with no play change in either arm and units within 1.10x are folded):

| deck | order-exact only: slower / net / units | + twin wave: slower / net / units |
|---|---|---|
| elementals | 10 / +11 / 1.153 | 2 / +2 / 1.017 |
| edf | 3 / +3 / 1.139 | 0 / 0 / 1.002 |
| bruna | 2 / +2 / 1.239 | 0 / 0 / 1.064 |
| hinata | 1 / +2 / 0.992 | 0 / 0 / 1.002 |
| landfall | 2 / +2 / 1.139 | 1 / +1 / 0.963 |
| melira | 1 / +1 / 1.148 | 1 / +1 / 1.004 |
| fluctuator | 1 / +1 / 1.354 | 0 / 0 / 1.046 |
| fungusv3 | 1 / +1 / 1.451 | 0 / 0 / 1.040 |
| minotaur | 1 / +1 / 1.401 | 0 / 0 / 1.039 |
| snow | 2 (1 faster) / +1 / 0.988 | 0 / 0 / 0.985 |
| th | 1 / +1 / 1.255 | 0 / 0 / 1.036 |
| pd | 0 (1 faster) / -1 / 1.159 | 0 / 0 / 1.021 |
| vampires | 0 / 0 / 1.399 | 0 / 0 / 1.211 |
| dragons | 0 / 0 / 1.658 | 0 / 0 / 1.155 |
| soldiers | 0 / 0 / 1.544 | 0 / 0 / 1.043 |
| angels | 0 / 0 / 1.479 | 0 / 0 / 1.090 |
| colossus | 0 / 0 / 1.441 | 0 / 0 / 1.072 |
| critter | 0 / 0 / 1.357 | 0 / 0 / 1.072 |
| whiteknights | 0 / 0 / 1.346 | 0 / 0 / 1.069 |
| selesnyav2 | 0 / 0 / 1.338 | 0 / 0 / 1.039 |
| pirates | 0 / 0 / 1.303 | 0 / 0 / 1.022 |
| galadriel, goblins, stompy, dragonstorm, mirrorwing, giants, lhc, breaching | 0 / 0 / 1.08-1.22 | 0 / 0 / 0.96-1.06 |
| the other 10 decks | 0 / 0 / 0.95-1.03 | 0 / 0 / 0.98-1.01 |

**Every slower game recovers at `--depth 8 --budget-ms 0`** (the fix ON; at an unlimited budget the wave never
opens, so both arms are the same search there): all 25 of the order-exact-only arm's and all 4 of the wave arm's
reach the canonical arm's shipping win turn or better (Hinata gi3: 6 -> 8 at shipping, 5 at d8 b0). Each is
budget churn, the mechanism traced on Elementals gi27 / gi95: the order-exact search re-searches twins the
canonical key used to answer, the T1 ladder's d5 pass no longer fits the budget, and the decision commits d4.
None needs the clairvoyance check.

**Sized up** (seed 1530001, 1,200 more paired games per deck, canonical vs twin wave), the three decks whose wave
net the first sample left at one or two games:

| deck | games | slower | net | units |
|---|---|---|---|---|
| elementals | 1,200 | 3 | +3 | 1.007 |
| landfall | 1,200 | 3 | +4 | 0.954 |
| melira | 1,200 | 0 | 0 | 1.020 |

Combined 1,600 games each: **Elementals +5, Landfall +5, Melira Pod +1** (0 faster anywhere). All six
sized-up slower games also recover at d8 b0 (Landfall gi729: 6 -> 8 at shipping, 6 at d8 b0), so every one of the
39 slower games across both arms and both samples is budget churn. **The adoption bar's second half is not met**
for Elementals, Landfall and Melira under the shipped shape (net +0.003 / +0.003 / +0.0006 t per game), nor for
11 decks under the order-exact-only shape -- the USER's call (see Open).

What the wave's booking costs (diagnostic `MTG_NOWIN_TWIN_BOOK=0`, never a shipping setting: it lets a reuse be
stored as a proof above it): on the five Elementals games slower under the wave it recovers three (gi95, gi249,
gi665) and not two (gi492, gi1098), so the residual churn is partly the booking (ancestors of a reuse are not
memoised) and partly order-exact re-searches of twins too cheap for the gate.

**Unlimited budget** (`--depth 8 --budget-ms 0`, the d8 b0 instrument sample: 24 decks x 25 games): **0 play
changes** in 600 paired games, and the order-exact search costs **1.95x** the units (Dragons 7.6x, KittyEquipment
2.7x, Galadriel 2.4x, slivers 2.3x, Fungus 2.0x, Knights 2.0x). That is the canonical key's sharing at an
unlimited budget -- exactly the reuse that was unsound -- and the wave cannot buy it back there by design.

## Unit tests

`test/unit/test_nowin_order_exact.cpp`, through `TurnSolver::TwinNoWinCacheForTest` (searches `first`, then
`second` with the SAME per-decision and game caches, both one `FSLineWin` level deep, and reports both answers,
each search's units and whether the second one's root was a cache hit). The position: Vampires turn 6 with four
castable plans and no kill of a 40-life opponent inside the 2-turn horizon; its twin reverses the hand and
rotates the battlefield.

* a zone-permuted twin at an unlimited budget is SEARCHED, not answered by the other order's refutation --
  under the levers' defaults, so `MTG_NOWIN_ORDER_EXACT=0 mtg-test` fails it;
* under a starved budget (2,000 units) the twin's refutation IS reused -- under the defaults, so
  `MTG_NOWIN_TWIN_WAVE=0 mtg-test` fails it;
* with the wave pinned on, an unlimited budget still searches the twin (the soundness half of the wave);
* control: an exact transposition is still answered from the cache;
* control: with `NOWIN_ORDER_EXACT` pinned OFF the twin IS answered by the other order's refutation.

## Open

* **The USER's decision: adopt with the measured churn, or not.** The shipped shape (order-exact + twin wave) is
  sound at every budget the completeness invariant names (the witness wins T3 at d8 b0; 0 play changes and 0 wrong
  hits across 600 unlimited games) and costs +1.0% units at shipping, but leaves Elementals and Landfall at +5 /
  1,600 games and Melira at +1 / 1,600, every slower game churn that recovers at d8 b0. The order-exact key alone
  (`MTG_NOWIN_TWIN_WAVE=0`) is the simpler shape and costs +9.7% units and +25 / 15,600 (11 decks > 0). Ideas for
  the residual, unmeasured: store a reuse's ancestors as GATED estimates (served only under the same budget gate)
  instead of not at all -- the booking test above bounds what that can buy; raise those three decks' budgets
  (settings, not search).
* **Unlimited-budget cost: 1.95x units** (Dragons 7.6x). Inherent: it IS the unsound sharing. Anything that searches
  at `--budget-ms 0` -- the unbudgeted backstop rows, unbounded label search for value-leaf generation -- pays it.
* **Residual wrong hits under the fix: 2 of 691,005** (both one node of one Hinata game: seed 1480164 gi163, turn 6,
  depth 1, cutoff 7, fresh T7 beyond the horizon), game line cache only, SAME order on both sides, and the stored
  state's own fresh search also finds T7. Attribution (`MTG_NOWIN_FSL_VERIFY_MODE`): the disagreement disappears when
  the re-search may READ the game cache (mode 4) and not when it gets the query's TT (1) or budget (2), so an entry
  stored by an EARLIER DECISION beneath this node hides the win: a cross-decision context hole in
  `MTG_GAME_LINE_CACHE` (an answer that depends on per-decision state its Tag does not fold), not order dependence.
  Not reproduced at d8 b0 (79,400 hits on that game unbudgeted, 0 wrong) or in the per-decision cache; past-cutoff
  win reuse (`MTG_FSL_PASTCUT_PROBE`) is not it (0 wider hits). Not root-caused.
* **The witness's own site is a Goblins play defect, outside this fix:** `FireCombatDamageCheatIntoPlay` iterates
  stale battlefield POSITIONS, so an ETB that removes an earlier permanent (Goblin Chainwhirler killing the
  opponent's 1/1) drops or misattributes a later Lackey trigger depending on board order; and the Lackey's put is a
  "may" whose decline is never a searched option, which is the line the T3 kill actually needs. Each moves Goblins
  play and needs its own A/B.
* **`FsOrderSig`'s resolution** (hand names, battlefield name + copy id, in order) cannot see an attribute permutation
  among same-named copies (which of two identical Lackeys is tapped). WIN lines rely on it already and the order-exact
  no-win key inherits it; the instrument's finer signature found no such cross hit under the fix in any sample.
* **Not done here:** smoke / regression / overnight ground truth is not re-accepted (the shipped levers changed play
  in 45 of 15,600 A/B games, so those tiers will report moved digests until the lander accepts them); the legacy
  single-key path (`MTG_FSL_SPLIT_KEYS=0`) gets the order-exact key but not the wave (A/B hatch only); not pushed, so
  no Windows CI.
