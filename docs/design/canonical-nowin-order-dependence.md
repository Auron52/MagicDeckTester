# Canonical no-win sharing is not order-free (found 2026-10-29; fix `MTG_NOWIN_ORDER_EXACT` + `MTG_NOWIN_TWIN_WAVE`; 2026-10-30 rebased onto e3725d76a, the residual churn root-caused, `MTG_NOWIN_TWIN_EST` + `MTG_NOWIN_TWIN_HEQ` added -- all default ON; held-out bar: half (1) met, half (2) NOT met for Alchemist +1 / Hinata +3 per 1,600 -- adoption is the USER's call, see Open)

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

### `MTG_NOWIN_TWIN_EST` (heurarm `NOWIN_TWIN_EST`, DEFAULT ON 2026-10-30; `=0` -> the 2026-10-29 wave)

A twin reuse is booked as a truncation, so a node whose subtree took one is not a refutation and was stored
NOWHERE: every later query of that node (the next pass of the ladder at the same turn + depth, a twin from another
branch, the next decision through the game line cache) searched it again at full price, its own children answered by
the same reuses again. That is the BOOKING half of the residual below (`MTG_NOWIN_TWIN_BOOK=0`, the diagnostic that
stores such a node as a proof, recovered exactly the games this lever recovers).

Under the lever, a no-win whose ONLY cuts beneath were twin reuses (its truncation delta equals the delta of
`nwtwin::t_book_ev`, the reuses' own bookings; no budget bind, no decision-ceiling stop, no truncated line) still
stores no refutation, but leaves the twin hint, in both caches, holding its bound and its own measured units. A hint is
exactly the kind of thing such a node is (an order's no-win under its cutoff, with what its search cost), and a hint
is only ever served through the same gate (`units > share * Remaining`, never at an unlimited budget, never in a
verify re-search) and booked as a cut. So nothing resting on a twin is ever stored or served as a proof; what changes
is that an expensive estimate is reused instead of rebuilt. At an unlimited budget no reuse happens, so no such node
exists: inert by construction.

The WIN half. The per-decision cache keeps a found win whatever was cut beneath it; the game line cache keeps one only
from a complete search ("best win at or before the cutoff"). So a win found under twin reuses was lost to every later
DECISION, which searched the node again (Hinata s1610001 gi1154 below: T2's pass 4 cost 5 units under the canonical
key, where T1's pass 5 had stored the turn-2 nodes, and 10,847 without them). Under the lever such a win is kept in the
game line cache as a WIN ESTIMATE: its line (under the ORDER-EXACT win key plus a private salt, so the index-encoded
line is replayable by construction) and its search's units. A later query takes it only through the same gate, only
when the line beats its cutoff, and books it as a cut: the line is real, but whether it is the BEST is exactly what
the reuses beneath it left open.

### `MTG_NOWIN_TWIN_HEQ` (heurarm `NOWIN_TWIN_HEQ`, DEFAULT ON 2026-10-30; `=0` -> raw warm-up units)

The EMULATED ladder (`value_play.ladder: "emulated"`: Landfall, Melira Pod, KittyEquipment, Niv-Mizzet) plays its
warm-up passes on the value leaf (or no leaf) and gates the next pass on RECONSTRUCTED heuristic costs, `ch(k) =
cost(k) + R x leaves(k)`, on the premise that *"the tree of a pass is identical under both leaves"*. The twin gate
broke that premise: in a warm-up a twin costs its interior units only (a value leaf charges none), so the gate stays
shut and the twin is re-searched, where the committing heuristic pass at the same depth would price the same node at
its rollouts and reuse it. The re-searched twins add LEAVES, which the reconstruction multiplies by R, so a few dozen
units of warm-up work read as hundreds of heuristic ones and the ladder calls its committing pass a depth early (the
Landfall games below: +18 to +50 units and the same number of leaves at pass 3).

Under the lever, while an emulated warm-up pass runs (bounded budgets only), a twin hint stored in it prices its node
in the committing leaf's currency, `units + R_at(d) x leaves` (`nwtwin::t_upl`, set by `run_pass`), so the gate makes
the call it would make for the committing pass's identical node. It re-prices; it admits nothing at an unlimited
budget (the gate never opens there). Every other pass, and every other ladder, prices in raw units, which is their
own gating currency (a value ladder gates on value-pass units).

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
| rebased onto e3725d76a (2026-10-30): `=0` / fix | **T3 / T3** -- upstream's `MTG_COMBAT_STABLE_SOURCES` (damaging Lackeys resolved by identity) and `MTG_LACKEY_DECLINE` (the decline searched) removed the order-dependent site this witness hit; 2 game-cache no-win hits, 0 twin reuses |

## Held-out A/B #1 (the 2026-10-29 shape: order-exact + twin wave, no EST / HEQ)

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

## 2026-10-30: rebase, the residual root-caused, `MTG_NOWIN_TWIN_EST` + `MTG_NOWIN_TWIN_HEQ`

**Rebase.** Onto e3725d76a (branch `wip/nowin-order-exact-2`; `wip/nowin-order-exact` keeps the 2026-10-29 commit).
One conflict, the `HeuristicArm.h` slot tail (both sides' slots kept, in order, matching `kNames`). With
`MTG_NOWIN_ORDER_EXACT=0` the rebased tree is byte-identical to the tip: 39 decks x 10 games (seed 1600001), every
digest and every game's units equal.

**The adoption bar, restated by the USER:** *"The no win cache should be tested as with every other such option."*
So (1) every slower game recovers at `--depth 8 --budget-ms 0` (or is CLEAR clairvoyance) AND (2) every deck's net is
<= 0 vs the committed tree on a large held-out sample.

### Every residual slower game, traced

Single-game repros at the shipped play setting (`MTG_TRACE=search` prints each ladder pass's cost; the diagnostic
counters added to that line count the twin reuses taken and the twins RE-SEARCHED because a covering hint existed
but cost <= share x Remaining -- "sub-gate twins" -- with their hint units and their real re-search units, which agree
to within a few units in every game). The old A/B's ten slower games, re-run on the rebased tree (Elementals
s1490001 gi95 is no longer slower there), plus every slower game of the measurements below:

| game | canon | 10-29 wave | where the units went | +EST | +EST+HEQ |
|---|---|---|---|---|---|
| Elementals s1490001 gi249 | 6 | 7 | T2: T1's reuses left the turn-2 nodes unstored; pass 3 53 -> 112, pass 4 2,199 -> 4,994 units, pass 5 refused (BOOKING) | 6 | 6 |
| Elementals s1530001 gi665 | 5 | 6 | T1 pass 4 7,423 -> 7,715, pass 5 refused; `MTG_NOWIN_TWIN_BOOK=0` 7,479, admitted (BOOKING) | 5 | 5 |
| Melira s1490001 gi214 | 6 | 7 | booking (recovered by BOOK=0) | 6 | 6 |
| Landfall s1530001 gi1069 | 5 | 6 | emulated V warm-up pass 3: 7 sub-gate twins = +50 units AND +48 leaves; reconstructed `ch` 847 -> 1,118, so pass 4 is called the committing (H) pass and the d5 H pass misses the T5 line (CURRENCY) | 6 | 5 |
| Landfall s1490001 gi139 | 5 | 6 | the same: V pass 3, 3 sub-gate twins, +18 units / +18 leaves, `ch` 631 -> 732 | 6 | 5 |
| Landfall s1530001 gi648 | 5 | 6 | the same: 3 sub-gate twins, +20 / +20, `ch` 857 -> 969 | 6 | 5 |
| Landfall s1530001 gi729 | 6 | 8 | the same, at T1 / T2 | 8 | 6 |
| Hinata s1610001 gi1154 | 7 | -- | T2 pass 4: 5 units canonical (turn-2 nodes stored by T1's pass 5) vs 10,847; the missing entries are WINS found under reuses, which the game cache keeps only from a complete search (BOOKING, win half) | 8 (no-win EST) | 7 (with the win estimate) |
| Elementals s1530001 gi492 | 4 | 5 | value ladder T1 pass 3: 23 sub-gate twins = +96 units (8,189 -> 8,285, +1.2%); pass 4's start gate (alpha 8.8 under the value leaf) admits 77.7k <= 78.4k in canonical and refuses 79.5k > 77.5k (SUB-GATE) | 5 | 5 |
| Elementals s1530001 gi1098 | 5 | 6 | T1 pass 4 9,170 -> 9,870: 114 sub-gate re-searches (424 units) + booking; pass 5 refused (SUB-GATE + BOOKING) | 6 | 6 |
| Melira s1490001 gi47 | 4 | -- | H pass 3: 2 sub-gate twins = +92 units (9,500 -> 9,592); pass 4 refused (canonical's pass 4 found the T4 line) (SUB-GATE) | 5 | 5 |
| Elementals s1610001 gi1280 | 5 | -- | T1 pass 4: 192 sub-gate re-searches, +665 units; pass 5 refused (SUB-GATE) | -- | 6 |
| Melira s1610001 gi592 | 4 | -- | H pass 3: 9 sub-gate twins, +354 units; pass 4 refused (SUB-GATE) | -- | 5 |
| Hinata s1630001 gi150 | 5 | -- | T1 pass 4: 164 sub-gate re-searches, +791 units (842 metered); pass 5 refused (SUB-GATE) | -- | 6 |
| Hinata s1630001 gi371 | 5 | -- | T1 pass 4: 31 sub-gate re-searches, +187 units (219 metered); pass 5 refused (SUB-GATE) | -- | 6 |
| Alchemist s1630001 gi140 | 6 | -- | T1 passes 1-5 complete in both arms, pass 5 +270 units (97 sub-gate re-searches); the escalation that follows starts on 12,212 instead of 12,539 units left (SUB-GATE) | -- | 7 |
| Hinata s1630001 gi1228 | 6 | -- | T3 pass 3 3,457 -> 3,621 units (12 sub-gate re-searches, 144 units); pass 4 refused where canonical's found the T6 line (SUB-GATE) | -- | 8 |

So there are exactly three mechanisms, and every slower game is one of them:

1. **BOOKING** (no-win and win). A reuse is booked as a cut, so every ancestor of one was stored nowhere and was
   rebuilt at full price by the next pass, the next twin and the next decision. Fixed soundly by `MTG_NOWIN_TWIN_EST`
   (above): the ancestor becomes the same kind of gated, booked estimate as the reuse itself.
2. **CURRENCY** (emulated ladder only). The twin gate priced a warm-up's twin in value-leaf units, breaking the
   "same tree under both leaves" premise the emulated gate's reconstruction rests on; the extra leaves are multiplied
   by R. Fixed soundly by `MTG_NOWIN_TWIN_HEQ` (above).
3. **SUB-GATE twins.** A twin whose re-search costs <= share x Remaining is re-searched -- by design: that is the
   order-exact soundness price the gate decides to pay. Each costs a few units (4-50), a pass carries a few to a
   couple of hundred of them (+1 to +5% of the pass), and the ladder's start gate extrapolates the next pass's cost
   QUADRATICALLY from the last two (`c_k^2 / c_{k-1}`) against a fixed remaining budget, so a 1% heavier pass k moves
   pass k+1's admission by ~2%. Games sitting within that margin of the gate lose the deeper pass. The gate is priced
   in the currency its ladder gates on in every one of these games (raw units in a value or heuristic ladder, the
   committing leaf's after HEQ), so there is no mispricing left to correct: the re-search is the cost of the fix.

**On the old A/B seeds** (NOT held out for these levers: EST and HEQ were built on their slower games; seeds 1490001 x
400 + 1530001 x 1,200 per deck, the three decks the 10-29 A/B left positive), vs canonical: EST + HEQ (no win half
yet) Elementals +2 (gi492, gi1098: SUB-GATE), Landfall 0, Melira +1 (gi47: SUB-GATE), 0 faster; EST alone (`HEQ=0`)
leaves Landfall at +5 -- the currency defect is the whole of Landfall's residual.

### Held-out A/B #2 (seed 1610001; EST without the win half, + HEQ)

One pooled batch, every suite deck (39) at its shipped play setting, 400 paired games per deck, three arms (canonical
`MTG_NOWIN_ORDER_EXACT=0`; the new shape; the 10-29 shape `MTG_NOWIN_TWIN_EST=0 MTG_NOWIN_TWIN_HEQ=0`), `MTG_PGO=0`,
`MTG_RSS_CAP_GB=43`, 0 abandoned. New vs canonical: **0 faster / 0 slower in all 39 decks**, units 1.0049x. The 10-29
shape on the same games: 2 slower (Dragons, Prevent Damage) / 2 faster (Alchemist, Landfall), units 1.021x. Sized up to
1,600 (seed 1610001, gi 400-1599) on every deck that changed play at all (Elementals, Hinata, KittyEquipment, Landfall,
Melira, Colossus): Elementals +1 (gi1280), Hinata +1 (gi1154), Melira +1 (gi592), the other three 0; 0 faster. Hinata
gi1154 is what found the win half of BOOKING (the game cache's win estimate, now part of `MTG_NOWIN_TWIN_EST`), which
recovers it; the other two are SUB-GATE. That made 1610001 a development seed, so the shipped shape was measured again
on a fresh one.

### Held-out A/B #3 -- the shipped shape (seed 1630001)

The final tree (order-exact + twin wave + EST incl. the win estimate + HEQ, all default ON) vs canonical, one pooled
batch, 39 decks x 400 paired games, same settings, 0 abandoned; the six decks marked 1,600 were sized up in a second
pooled batch (below):

| deck | games | faster | slower | play changed | net | units new / canon |
|---|---|---|---|---|---|---|
| alchemist | 1600 | 0 | 1 | 2 | **+1** | 1.001 |
| angels | 400 | 0 | 0 | 0 | +0 | 1.044 |
| antilife | 400 | 0 | 0 | 0 | +0 | 1.001 |
| auras | 400 | 0 | 0 | 0 | +0 | 1.008 |
| breaching | 400 | 0 | 0 | 0 | +0 | 1.026 |
| bruna | 1600 | 0 | 0 | 1 | +0 | 1.001 |
| burn | 400 | 0 | 0 | 0 | +0 | 1.001 |
| colossus | 400 | 0 | 0 | 0 | +0 | 1.013 |
| creature_giving | 400 | 0 | 0 | 0 | +0 | 1.001 |
| critter | 400 | 0 | 0 | 0 | +0 | 1.114 |
| dragons | 400 | 0 | 0 | 0 | +0 | 0.967 |
| dragonstorm | 400 | 0 | 0 | 0 | +0 | 1.035 |
| edf | 400 | 0 | 0 | 0 | +0 | 1.004 |
| elementals | 1600 | 0 | 0 | 2 | +0 | 1.002 |
| fivecolour | 400 | 0 | 0 | 0 | +0 | 1.000 |
| fluctuator | 400 | 0 | 0 | 0 | +0 | 0.988 |
| fungusv3 | 400 | 0 | 0 | 0 | +0 | 1.002 |
| galadriel | 400 | 0 | 0 | 0 | +0 | 1.018 |
| giants | 400 | 0 | 0 | 0 | +0 | 1.000 |
| goblins | 400 | 0 | 0 | 0 | +0 | 1.011 |
| hinata | 1600 | 1 | 3 | 6 | **+3** | 1.001 |
| kittyv3 | 400 | 0 | 0 | 0 | +0 | 1.000 |
| knights | 400 | 0 | 0 | 0 | +0 | 1.011 |
| landfall | 400 | 0 | 0 | 0 | +0 | 1.002 |
| lhc | 400 | 0 | 0 | 0 | +0 | 1.031 |
| melira | 1600 | 0 | 0 | 21 | +0 | 1.000 |
| minotaur | 400 | 0 | 0 | 0 | +0 | 1.014 |
| mirrorwing | 400 | 0 | 0 | 0 | +0 | 1.010 |
| niv | 400 | 0 | 0 | 0 | +0 | 1.000 |
| pd | 400 | 0 | 0 | 0 | +0 | 1.000 |
| pirates | 400 | 0 | 0 | 0 | +0 | 1.011 |
| selesnyav2 | 400 | 0 | 0 | 0 | +0 | 1.028 |
| slivers | 400 | 0 | 0 | 0 | +0 | 1.003 |
| snow | 400 | 0 | 0 | 0 | +0 | 1.000 |
| soldiers | 400 | 0 | 0 | 0 | +0 | 1.066 |
| stompy | 400 | 0 | 0 | 0 | +0 | 1.020 |
| th | 400 | 0 | 0 | 0 | +0 | 1.012 |
| vampires | 1600 | 0 | 0 | 2 | +0 | 1.015 |
| whiteknights | 400 | 0 | 0 | 0 | +0 | 1.048 |
| **TOTAL** | 22800 | 1 | 4 | 34 | +4 | 1.0019 |

At 400 games per deck the 39-deck total was 0 faster / 3 slower (Alchemist gi140, Hinata gi150, gi371), units
**1.0048x**. Sized up to 1,600 (gi 400-1599) on the two nonzero decks and on every deck that changed play at all
(Bruna, Elementals, Melira, Vampires): one more Hinata game slower (gi1228, 6 -> 8) and one faster (gi1024, 6 -> 5);
the other four stay at 0. **Final: 37 decks net 0, Alchemist +1 / 1,600, Hinata +3 / 1,600** (+0.0006 and +0.0019 t
per game). Every one of the four slower games is SUB-GATE (table above; Hinata gi1228: T3 pass 3 3,457 -> 3,621 units,
12 sub-gate re-searches = 144 of them, pass 4 refused where canonical's found the T6 line).

**Recovery at `--depth 8 --budget-ms 0`** (abandon 20M units, both arms): all ten slower games found on these samples
-- the four above plus Elementals s1610001 gi1280, Hinata s1610001 gi1154, Melira s1610001 gi592, Elementals
s1530001 gi492 / gi1098, Melira s1490001 gi47 -- reach canonical's shipping win turn or better, and the two arms play
the SAME game at d8 b0 (identical digests):

| game | shipping canon / new | d8 b0 canon = new |
|---|---|---|
| Alchemist s1630001 gi140 | 6 / 7 | 6 |
| Hinata s1630001 gi150 | 5 / 6 | 5 |
| Hinata s1630001 gi371 | 5 / 6 | 5 |
| Hinata s1630001 gi1228 | 6 / 8 | 5 |
| Elementals s1610001 gi1280 | 5 / 6 | 5 |
| Hinata s1610001 gi1154 | 7 / 8 (before the win estimate; 7 after) | 7 |
| Melira s1610001 gi592 | 4 / 5 | 4 |
| Elementals s1530001 gi492 | 4 / 5 | 4 |
| Elementals s1530001 gi1098 | 5 / 6 | 5 |
| Melira s1490001 gi47 | 4 / 5 | 4 |

So **half (1) of the bar is met** (every slower game is budget churn and recovers at d8 b0; none needs the
clairvoyance check) and **half (2) is not, for Alchemist (+1) and Hinata (+3)**, every slower game of the SUB-GATE kind.

**Unlimited budget** (`--depth 8 --budget-ms 0`, `abandon_units` 3M, seed 1620001, the 24 cheaper decks x 5 games,
three arms): the new shape and the 10-29 shape are **byte-identical in all 120 games, units included** (EST and HEQ
cannot act without a finite budget), and new vs canonical has **0 play changes** at **1.74x** units.

The witness (Goblins s1330145 gi144, d8 b0) now wins T3 under BOTH arms on the rebased tree (row above).

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

`TurnSolver::TwinWaveProbeForTest` (search `first` unlimited, optionally drop the root's own twin hint so only its
descendants' remain, then search `second` twice under a finite budget, same caches) covers the two sub-levers:

* EST: under 2,000 units `second`'s first search reuses its children's twins, leaves an estimate hint at the root, and
  its second search is answered by it (`MTG_NOWIN_TWIN_EST=0 mtg-test` fails it); control: pinned OFF, no hint and the
  root is searched again; at an unlimited budget no hint is ever left and the twin's own search completes as a proof;
* HEQ: a hint stored with `t_upl = 1000` prices its node at `units + 1000 x leaves`, so under a budget sized to put
  the gate between the two prices the twin is reused, and with raw units (or `NOWIN_TWIN_HEQ` pinned OFF) it is
  searched (`MTG_NOWIN_TWIN_HEQ=0 mtg-test` fails it);
* EST's win half: three DECISIONS of one game (a fresh per-decision cache each, the game line cache kept; opponent at
  12, a turn-7 kill): the second decision finds the win while its no-win siblings are twin reuses, keeps it as a
  win estimate, and the third is answered by it at 0 units; control: pinned OFF, no estimate and the third decision
  searches the win again.

Each of `MTG_NOWIN_ORDER_EXACT=0`, `MTG_NOWIN_TWIN_WAVE=0`, `MTG_NOWIN_TWIN_EST=0`, `MTG_NOWIN_TWIN_HEQ=0` fails its own
cases; the full suite is 2168/2168 under the defaults (rebased tree, 2026-10-30).

## Open

* **The USER's decision: adopt or not -- half (2) of the bar is NOT met, and cannot be met soundly as it stands.**
  The shipped shape (order-exact + twin wave + EST + HEQ, all default ON) is sound at every budget the completeness
  invariant names (identical to the order-exact search at an unlimited budget; every slower game recovers at d8 b0)
  and costs +0.5% units at shipping. On the final held-out sample (seed 1630001) it is net 0 on 37 of 39 decks and
  **Alchemist +1 / 1,600, Hinata +3 / 1,600** (0 / 1 faster). Before EST + HEQ the same bar read Elementals +5,
  Landfall +5, Melira +1 (10-29 shape, seeds 1490001 + 1530001). What is left is the SUB-GATE mechanism (above): twins
  the gate re-searches because each costs <= share x Remaining, i.e. the soundness price itself, tipping the ladder's
  quadratic start gate in the games that sit within ~1-2% of it. Removing it means spending LESS on exactness at the
  shipped budgets, which no re-pricing can do; the shapes that would, all the USER's call:
  1. accept the residual (Hinata +3 / Alchemist +1 per 1,600, ~0.002 t / game) as the order-exact price;
  2. the share (`MTG_NOWIN_TWIN_SHARE`, not tuned here by direction): as share -> 0 every twin with a hint is reused at
     any finite budget (canonical cost at shipping, still exact at an unlimited budget, where the gate never opens);
  3. budgets (settings): the decks that move (Hinata, Alchemist; Elementals / Melira on other samples) a little more;
  4. a PASS-LEVEL fill-in wave instead of the per-node gate: reuse every twin in every pass (the ladder's admissions
     become canonical's), then re-search the committed pass with reuse off when budget remains -- exact at an
     unlimited or depth-capped budget, canonical-like whenever the ladder is budget-bound, and it spends on depth
     before within-turn exactness, i.e. NOT turn-complete (the USER's stated preference). Unmeasured.
* **Is the fix still needed on the rebased tree?** The witness no longer reproduces under the canonical key (upstream's
  Lackey fixes removed its site). Yes. The instrument at shipping settings on the rebased tree (seed 1640001, all 39 decks x 40 games per arm,
`MTG_NOWIN_FSL_VERIFY=1`): canonical **177,068** game-cache no-win hits, 109,926 (62%) cross-order, **16 WRONG** (all
Hinata, all cross-order, 1-ply nodes at turns 4-5 whose fresh search wins inside the cutoff -- the beyond-horizon
rollout order dependence of the first census), 1 unverified; the new shape 65,722 hits, **0 WRONG** (6 classed cross,
all on Stompy and all agreeing: attribute permutations among same-named copies that `FsOrderSig` cannot see, the
item below). The per-decision cache took no hit in either arm.
* **Unlimited-budget cost: 1.74x units** on the d8 b0 sample (24 decks x 5, seed 1620001; it was 1.95x on the 10-29
  sample, Dragons 7.6x). Inherent: it IS the unsound sharing. Anything that searches at `--budget-ms 0` -- the
  unbudgeted backstop rows, unbounded label search for value-leaf generation -- pays it.
* **Residual wrong hits under the fix: 2 of 691,005** (both one node of one Hinata game: seed 1480164 gi163, turn 6,
  depth 1, cutoff 7, fresh T7 beyond the horizon), game line cache only, SAME order on both sides, and the stored
  state's own fresh search also finds T7. Attribution (`MTG_NOWIN_FSL_VERIFY_MODE`): the disagreement disappears when
  the re-search may READ the game cache (mode 4) and not when it gets the query's TT (1) or budget (2), so an entry
  stored by an EARLIER DECISION beneath this node hides the win: a cross-decision context hole in
  `MTG_GAME_LINE_CACHE` (an answer that depends on per-decision state its Tag does not fold), not order dependence.
  Not reproduced at d8 b0 (79,400 hits on that game unbudgeted, 0 wrong) or in the per-decision cache; past-cutoff
  win reuse (`MTG_FSL_PASTCUT_PROBE`) is not it (0 wider hits). Not root-caused.
* **The witness's own site** (`FireCombatDamageCheatIntoPlay`'s stale positions; the Lackey decline never searched)
  was fixed upstream by `MTG_COMBAT_STABLE_SOURCES` and `MTG_LACKEY_DECLINE`; on the rebased tree both arms win T3.
* **`FsOrderSig`'s resolution** (hand names, battlefield name + copy id, in order) cannot see an attribute permutation
  among same-named copies (which of two identical Lackeys is tapped). WIN lines rely on it already and the order-exact
  no-win key inherits it; the instrument's finer signature found no such cross hit under the fix in the 2026-10-29
  samples, and 6 on seed 1640001 (Stompy), all agreeing with their fresh re-search.
* **The instrument does not verify twin REUSES** (`MTG_NOWIN_FSL_VERIFY` checks cache hits on the node's own key); a
  reuse is an estimate by construction and never served at an unlimited budget.
* **Not done here:** smoke / regression / overnight ground truth is not re-accepted (the shipped levers move play: 34
  changed games in the final 22,800, so those tiers will report moved digests until the lander accepts them); the
  legacy single-key path (`MTG_FSL_SPLIT_KEYS=0`) gets the order-exact key but not the wave, EST or HEQ (A/B hatch
  only); not pushed, so no Windows CI.
