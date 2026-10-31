# Every breakpoint continuation open to the search, "cast nothing more" included

Status (2026-10-15): **audited; fixed behind five levers -- F1-F4 default ON and through the whole
adoption bar on the rebased build, F5 default OFF pending the USER (section 5)**; identity, targeted
cases, the 33-deck movers check, the per-deck A/B, recovery and the d3 b0 exactness sample done. The integrator owns smoke / regression GT. Started as the root cause of Hinata2 shipped gi 137
(docs/design/final-ab-2026-10-13.md); widened to an audit of every place the breakpoint machinery
produces or chooses a post-breakpoint continuation, because the USER's ask was general:

> *"I'm not sure why this is still the case. All options should be open to the search except those
> that heuristics in the provider close. So yeah, let's dig in."* (2026-10-14)

and the standing rules it enforces: "done with this phase" is always a scored option (2026-09-30), and
a budget may re-price work but may not make a line unfindable -- *"everything should be findable at
unlimited or large budgets"* (2026-10-06).

## 1. The game that found it (Hinata2 shipped gi 137)

Hinata2, shipped settings (value_play depth 5, `--budget-ms 20`), seed 6320138, game index 137. The tip
wins T8 where T7 is legal, and T8 at `--depth 8 --budget-ms 0` too.

At the T7 node -- the deepest turn of the pass, a turn the breakpoint node does not host, its site 3 kept
out of wave 0 by `MTG_BP_SITE3_DEFER` -- the {Soulfire} plan's continuation is `MTG_BP_BASE_CANON`'s
single pick, the FIRST entry of the continuation list. On the committed tree that entry was the phantom
`{Irencrag + Magma Opus}` (payment fails, all four sources stay untapped, and the main-2 fixpoint
re-searched and found Ponder -> Preordain -> {Reality Spasm, Irencrag, Crackle}); once
`MTG_RESTRICTOR_ORDER_EXACT` correctly rejected the phantom, the first entry became a real `{Magma Opus}`
that spends all four mana and T7 was dead. Nothing else ever SCORED "cast nothing more after Soulfire":
wave 0 adds that option only for the sites it covers, both wave walkers stopped at the end of the list
(their comment said the past-the-end rank was a copy of the base plan -- untrue since BASE_CANON), and
the main-2 walker lacked the fixpoint step. The restrictor itself is right (both rejected subsets need
Irencrag's own red to pay for Irencrag).

## 2. The audit: every place a continuation is produced or chosen

"Complete at b0?" asks whether, at `--budget-ms 0`, every legal continuation of that breakpoint -- each
candidate of the list AND the EMPTY ("cast nothing more") arm -- is eventually SCORED by some route.
Provider = a deck heuristic allowed to restrict or order; engine = not allowed to refuse at unlimited
budget, only to order or price.

| # | Site | What it produces / chooses | Complete at b0 before? | Owner | Now |
|---|---|---|---|---|---|
| 1 | `bp_searched_plan`, targeted resolution | the option the plan carries: rank k, chain slot, EMPTY sentinel, overrun -> EMPTY | n/a (it IS the option) | -- | + demoted sentinel (#15) |
| 2 | `BpUnbranchedCanon`, `MTG_BP_BASE_CANON=1` | a BASE plan plays `cands.front()` at every class-on breakpoint of a searched apply | a one-option default, legitimate only while #8/#10/#13 reach the rest -- they did not (#10, #11) | engine | **first-in-order**: the base plan is scored first, every alternative is reachable (F1, F2) |
| 3 | `BpUnbranchedCanon`, `MTG_BP_NESTED_CANON` | a variant at an index it does not target plays `cands.front()` | un-hosted plans: ranks via the walker's nested slots, EMPTY never (#10); node-hosted plans: nothing at all (#13); two non-front choices in one apply: never (#19) | engine | F1, F4; #19 open |
| 4 | `BpUnbranchedCanon`, `MTG_BP_ENUM_CANON` (off) | derivation default | n/a (ranks a list, decides nothing) | -- | unchanged |
| 5 | fall-through EMPTY inside a playout (`g_rollout_nest > 0`) | base plans and untargeted indices play EMPTY; variants still rank | the horizon leaf (permitted greedy zone); EMPTY IS the base line there | permitted | unchanged |
| 6 | fall-through EMPTY inside a derivation (`g_bp_enum_depth > 0`) | values a candidate for ranking | not a decision | -- | unchanged |
| 7 | equip-route DFS (`EquipRouteDfsActive`) | EMPTY | the DFS re-enumerates the whole plan space after every link: it is the continuation search | -- | unchanged |
| 8 | wave 0 (`AppendBreakpointVariants`) | ranks 0..W-1 (W=2) x `bp_at` < D (D=1), the EMPTY arm x `bp_at` < D, uniform k < W, chain slots -- for the first `MTG_BP_MAXBASE`=16 plans opening a site in `BpWave0SiteMask` (site 3 out: `SITE3_DEFER`; the node's sites out on its turns) | an ORDER (the first wave); everything it leaves is owed by #10/#13 | engine (order) | unchanged; uniform/chain for deferred sites: part of #19 |
| 9 | wave-0 collapses: `W0_NOBP`, `W0_UNIF_COLLAPSE`, `W0_CHAIN_COLLAPSE`, ArmNew stillborn, bounce / dig folds, post-apply state dedup | decline a candidate proven identical to one already scored | yes (exact identities, each with a VERIFY mode) | engine (fold) | unchanged |
| 10 | deferred wave walker (`BpWaveWalker`: FSLineWin m1, SolveWithLookahead, FSLineTail m2, both group-wave tranches) | ranks k0.. of each (base, `bp_at`) slot, nested slots by discovery | **no**: a slot retired at the end of its list, so its EMPTY was at most applied as a probe and never scored -- for every slot wave 0 did not touch (a deferred site, a `MAXBASE` overflow plan, EVERY nested index) | engine | **F1** `MTG_BP_WAVE_EMPTY` |
| 11 | walker site mask (`BpWaveSiteMask`) | drops the node's sites on the node's turns | **no**: turn-keyed, it cannot see the host. In hosts that never pend (the SolveWithLookahead candidate loop = the executor's no-committed-line fallback and every re-solve root; both group-wave tranches) a site-3 plan on those turns had no wave 0, no walker and no node | engine | **F2** `MTG_BP_WAVE_UNHOSTED_SITES` |
| 12 | walker slot skips (`NSKIP`, `NOBP`, `NSKIP_GLOBAL` off) | decline a slot that can only re-score | yes (NSKIP needs `max_k >= n`) | engine (fold) | NSKIP also requires "no demoted wave owed" (F5) |
| 13 | breakpoint node (FSLineWin, FSLineTail; root turn and root+1, sites 3 and 10) | the full list + an explicit EMPTY child at the pend index; exact EMPTY pre-skip, prefix and child dedup | the pend index yes; **a breakpoint inside a child's continuation: no** -- nested canon only, and the walker had no slots for a hosted plan | engine | **F4** `MTG_BP_NODE_NESTED_SLOTS` |
| 14 | main-2 fixpoint (`M2FixModeFor`; Hinata's provider opt-in, mode 2) | kill-scan + gated re-entry after a main-2 line that fired a breakpoint | the mechanism is the provider's; but its host applied it to main 2's own plans only -- a walker continuation (the EMPTY above all) went straight to end of turn | engine (host gap) | **F3** `MTG_BP_M2_WAVE_FIXPOINT` |
| 15 | `MTG_BP_NEW_ONLY` (engine-wide, adopted 2026-09-22) | deletes an old-cards-only continuation as the sibling `P u {X}`'s duplicate | **no** when the sibling's realised cast ORDER differs from "P, then X" (a rank, or a tie broken by hand order): Mirrorwing Anger-before-Oracle 88 vs 82, Fists-before-Gold Rush 50 vs 46, both unfindable at b0 (the 2026-10-14 cast-order draft) | engine | **F5** `MTG_BP_NEW_ONLY_DEMOTE` |
| 16 | breakpoint condemnation (provider opt-in `CondemnsConsideredAtBreakpoint`, `MTG_BP_CONDEMN_*`) | deletes candidates re-offering a card the section declined | a provider prune; its sibling-coverage premise needs the sibling's EMPTY scored, which F1 now guarantees at deferred / nested slots | provider | unchanged |
| 17 | `MTG_BP_MAXBASE` = 16 | wave-0 cap; overflow plans start at rank 0 in the walker | an order; their EMPTY was #10 | engine (order) | F1 |
| 18 | `MTG_BP_SITE3_DEFER` | site 3 out of wave 0 | an order for ranks; EMPTY was #10; uniform / chain slices: never (#19) | engine (order) | F1 |
| 19 | `Plan::bp_choice` / `bp_at`: one deviation per apply (A3 of no-greedy-in-search-window.md) | a line needing non-front choices at TWO breakpoints of one apply | **no**, except the uniform / chain slices on wave-0 sites and the main-2 fixpoint's single re-entry | engine (representation) | **NOT fixed -- USER** (section 6.1) |
| 20 | executor (`AIEngine` resolve_draw_breakpoint, pod twin, site-9 twin) | replays what was scored; an unverified commit re-solves searched | not a search closure | -- | demoted sentinel mirrored at all three |
| 21 | escalation beam (near-leaf, escalation passes only) | caps plans per node | a general, counted truncation, not breakpoint-specific | -- | unchanged |

## 3. The fix: five levers, each a later wave; F1-F4 default ON, F5 default OFF (USER decision)

All five are completeness fixes. Each orders its new options LAST, so a finite budget spends first on
exactly what it spent on before (the budget doctrine's GOOD shape: inert as a gate, a price only), and an
unlimited budget reaches them. `=0` on each is byte-identical to the committed tree. Searched applies
only (outside a playout and a derivation): inside a playout EMPTY already is the base plan's line.

* **F1 `MTG_BP_WAVE_EMPTY`** -- `BpWaveWalker` hands out each slot's explicit EMPTY arm (the sentinel,
  not rank n: the same line, no enumeration, and the executor plays it as EMPTY whether or not the line
  verified) after the slot's last in-list rank, and every host SCORES it. Owed only by a slot wave 0
  never touched (k0 == 0) with a non-empty list; a wave-0 slot's EMPTY is wave 0's own arm.
* **F2 `MTG_BP_WAVE_UNHOSTED_SITES`** -- a walker whose host never passes the node a capture walks the
  full `BpSiteMask`. The node's stand-down applies only where it really hosts.
* **F3 `MTG_BP_M2_WAVE_FIXPOINT`** -- main 2's fixpoint step moved verbatim into one lambda
  (`m2_fixpoint`) used by the plan loop AND the wave walker.
* **F4 `MTG_BP_NODE_NESTED_SLOTS`** -- the node records what child 0 (= `cands.front()`, what the nested
  canon hands every untargeted index) reached past the pend; the walker opens those nested slots (ranks,
  then EMPTY) for the hosted plan, and stops re-walking the pend index the node already searched.
* **F5 `MTG_BP_NEW_ONLY_DEMOTE`** -- `MTG_BP_NEW_ONLY`'s dropped entries are kept in the same memo entry
  (`BpEnumEntry::demoted`) and reachable only as each slot's LAST wave (after its ranks and EMPTY), via a
  new sentinel `kBpDemotedChoice + j` mirrored in the executor. Nothing the filtered list feeds changes:
  the canon's `cands.front()`, wave 0, ArmNew's empty-list test, NSKIP's length, the node's children.
  A demoted entry whose sibling DOES realise its order lands on the sibling's state and dies in the
  post-apply dedup for one apply. **Ships OFF (2026-10-15) until the USER rules** -- it modifies
  `MTG_BP_NEW_ONLY`, a rule the USER adopted, and it is the only lever with a deck net above 0 on the
  A/B (section 4). The two Mirrorwing scenarios pin it on in their `env`.

`MTG_BP_BASE_CANON` is now a first-in-order choice, not an only choice: at every breakpoint a base
plan resolves by the canon, wave 0, the walker (ranks, EMPTY, demoted) or the node offer every
alternative at unlimited budget, within the one-deviation representation (#19).

**The prototype** (`MTG_BP_EMPTY_ARM_DEFERRED`: an eager wave-0 EMPTY arm for every un-fanned plan) was
not ported. It covered only #10's `bp_at` < D case -- not nested indices, not #11, #13, #14 or #15 -- and
placed the new option FIRST, spending budget on it at every node (+2.6-3.5% units on Hinata). F1-F3
fix gi 137 at the shipped budget too, with the EMPTY ordered last (section 4).

## 4. Measurements

Sections 4.1-4.3 ran on this worktree's Release builds at `--threads 6` with the primary checkout's
inputs (base 77e094a2); 4.4-4.5 on the rebased build `r2` (16dfb7e4 + this branch) at `--threads 20`
under the box lock, inputs staged onto the build volume. `MTG_PGO=0`, loss = 9 throughout. Builders and
analysers: `scripts/bp_open/`.

### 4.1 Targeted cases

| case | committed tree | all five on | which lever alone suffices |
|---|---|---|---|
| Hinata2 gi 137 (seed 6320138), shipped b20 | T8 | **T7** | F1 alone T7, F3 alone T7 (F2 alone T8, F4 alone T8); each one OFF alone still T7 |
| Hinata2 gi 137, `--depth 8 --budget-ms 0` | T8 | **T7** | F1 off T7, F3 off T7 |
| Hinata2 gi 137, shipped, with `MTG_BP_WAVE_NEW_LAST=1` (since deleted) | -- | T7 | -- |
| `test/scenarios/mirrorwing_bp_demoted_anger_before_oracle.json` (b0) | no win (84 left) | **T4** | F5; `MTG_BP_NEW_ONLY=0` also T4 |
| `test/scenarios/mirrorwing_bp_demoted_fists_before_goldrush.json` (b0) | no win (46 left) | **T4** | F5; `MTG_BP_NEW_ONLY=0` also T4 |

Unit tests: `test/unit/test_bp_wave_empty.cpp` (8 cases: the walker contract through
`TurnSolver::WaveWalkerTraceForTest` -- EMPTY last in a slot, none for an empty list or a wave-0 slot,
the node-turn mask by host, hosted nested slots, the demoted wave); full
`mtg-test` 1062/1062 and scenarios 162/162 (the two new ones included) on the rebased build `r2`.

### 4.2 Identity (all five `=0` vs the committed tree)

The committed tree's binary (77e094a2 + the new slot names, which nothing reads) against this build with
every lever `=0`, 12 decks x shipped / d3 b10 / d0, 1,700 games, seeds 10100001+: **play digests
identical in 68 of 68 jobs**; units identical in 64, and in the 4 others (all KittyEquipment) differ by
0.01-0.14% with identical play -- see 4.6 for why that is job history in the pooled batch, not the
levers.

### 4.3 Movers (all 33 suite decks, on vs all-off, seeds 10200001+)

Shipped 100 games (25-75 on the costliest decks), d3 b10 100, d0 50, per arm, one pooled batch. A deck
"moves" if any chunk's play digest differs. **Nine movers: th, hinata, creature_giving, mirrorwing,
melira, fluctuator, pirates, pd, bruna. No deck moves at d0.** The rest play byte-identically; some pay
units for it (budget the fixed per-decision budget had left over, now spent on the new options):

| deck (non-mover) | units x shipped | units x d3 |
|---|---|---|
| stompy | 1.20 | 1.00 |
| auras | 1.16 | 1.09 |
| critter | 1.15 | 1.00 |
| soldiers | 1.08 | 0.98 |
| kitty / kittyv2 / kittyv3 | 1.04 / 1.04 / 1.02 | 1.02 / 1.02 / 1.01 |
| everything else | <= 1.04 | <= 1.02 |

Most of that is F5: with F5 off (the `nodemote` arm) stompy 1.00, auras 1.05, critter 1.03 at shipped,
and of the movers mirrorwing 1.01 (1.30 with F5), pirates 1.10 (1.35 with F5).

### 4.4 Per-deck held-out A/B on the nine movers (rebased build r2, complete)

FRESH seeds 10400001+, `seed = 10400001 + 20000*deck_index + 5000*cell_index + gi` (deck_index =
position in `scripts/bp_open/make_ab.py` ALL_DECKS). Binary: `r2` = 16dfb7e4 (work/integ-2026-10-14) +
this branch, inputs staged onto the build volume (`scripts/bp_open/stage_data.sh`), ONE pooled batch
(767 jobs, 19,175 games, 22 min at `--threads 20` under the box lock). Every arm names
`MTG_BP_NEW_ONLY_DEMOTE` explicitly. Arms: `off` (all five =0, the committed tree's play), `nodemote`
(**F1-F4 = the shipped default**), `on` (all five), `onnl` (all five + `MTG_BP_WAVE_NEW_LAST`), `nl14`
(F1-F4 + `NEW_LAST`). Shipped b20 (value_play depth), d3 b10, d0. Raw:
`docs/design/bp-open-data/ab_r2_vs_{off,nodemote,on}.txt`. **net = summed win-turn change vs off, lower
is better.**

| deck | cell | games/arm | F1-F4: fast / slow / net | all five: fast / slow / net | units x vs off (F1-F4 / all five) |
|---|---|---|---|---|---|
| th | shipped | 200 | 0 / 0 / 0 | 1 / 0 / -1 | 1.019 / 0.975 |
| th | d3 | 100 | 0 / 0 / 0 | 1 / 0 / -1 | 1.016 / 1.009 |
| hinata | shipped | 175 | 0 / 2 / **+2** | 0 / 2 / **+2** | 1.022 / 1.086 |
| hinata | d3 | 250 | 3 / 0 / -3 | 5 / 0 / -5 | 1.027 / 1.040 |
| creature_giving | shipped | 200 | 0 / 0 / 0 | 0 / 1 / **+1** | 1.026 / 1.097 |
| creature_giving | d3 | 300 | 0 / 0 / 0 | 0 / 1 / **+1** | 1.007 / 1.041 |
| mirrorwing | shipped | 200 | 0 / 0 / 0 | 0 / 1 / **+1** | 1.024 / 1.240 |
| mirrorwing | d3 | 300 | 1 / 0 / -1 | 1 / 0 / -1 | 1.002 / 1.099 |
| melira | shipped / d3 | 200 / 300 | 0 / 0 / 0 both | 0 / 0 / 0 both | 1.009 / 1.028 ; 1.005 / 1.007 |
| fluctuator | shipped / d3 | 200 / 300 | 1 / 0 / -1 each | 1 / 0 / -1 each | 1.002 / 1.002 ; 1.024 / 1.024 |
| pirates | shipped / d3 | 200 / 100 | 0 / 0 / 0 both | 0 / 0 / 0 both | 1.060 / 1.285 ; 1.002 / 1.031 |
| pd | shipped / d3 | 200 / 100 | 0 / 0 / 0 both | 0 / 0 / 0 both | 1.016 / 1.024 ; 1.001 / 1.043 |
| bruna | shipped / d3 | 50 / 100 | 0 / 0 / 0 both | 0 / 0 / 0 ; 0 / 1 / **+1** | 1.009 / 1.009 ; 1.010 / 1.071 |
| all nine | d0 | 100 | -- | identical play in 36 / 36 chunks (no search at d0) | -- |

**Per deck, F1-F4 (shipped): net <= 0 on all nine** -- th 0, hinata -1 (+2 shipped, -3 d3), creature_giving
0, mirrorwing -1, melira 0, fluctuator -2, pirates 0, pd 0, bruna 0. Two slower games, both Hinata2
shipped (gi 79 T4 -> T5, gi 85 T6 -> T7). **All five: creature_giving +2 and bruna +1 are above 0** (hinata
-3, th -2, fluctuator -2, mirrorwing 0, the rest 0); the four extra slower games (creature_giving shipped
gi 196, d3 gi 298; mirrorwing shipped gi 141; bruna d3 gi 5) are every one F5's.

**`MTG_BP_WAVE_NEW_LAST` does nothing measurable.** `onnl` vs `on` and `nl14` vs `nodemote`: identical
per-game win turns in all 18 deck-cells, units within 0.0000-0.0025 (melira shipped 0.978 the one
outlier), one chunk's digest differs (hinata shipped, same win turns). So ordering the new options after
EVERY slot's ranks instead of after their own slot's does not move the churn: F5's slower games are not
the walker handing a demoted option out early. **`NEW_LAST` is DELETED** (2026-10-15, after this
measurement: a measured-inert lever is not integrated); the `onnl` / `nl14` arms ran on r2, which still had it.

**The rebase moved none of these games.** Each of `off`, `nodemote` and `on` plays per game identically on
r2 and on yesterday's v5 (77e094a2 + the levers, primary-checkout inputs), over every deck-cell both ran
(14 deck-cells, 2,824 games per arm; 2,831 for `on`): `docs/design/bp-open-data/rebase_vs_v5.txt`.

**Slower-game recovery, stage 1 (`--depth W --budget-ms 100`, W = the faster arm's win turn, the lever
arm) and stage 2 (`--depth 8 --budget-ms 0`):**

| arm | game | off | lever arm | stage 1 | verdict |
|---|---|---|---|---|---|
| F1-F4 | hinata shipped gi 79 | T4 | T5 | d4 b100: **T4** | recovered |
| F1-F4 | hinata shipped gi 85 | T6 | T7 | d6 b100: **T5** | recovered |
| all five | hinata shipped gi 79 | T4 | T5 | d4 b100: T4 | recovered |
| all five | hinata shipped gi 85 | T6 | T7 | d6 b100: T5 | recovered |
| all five | creature_giving shipped gi 196 | T6 | T7 | d6 b100: T6 | recovered; b0 T6 |
| all five | creature_giving d3 gi 298 | T5 | T6 | d5 b100: T5 | recovered; d3 b0 T5 |
| all five | mirrorwing shipped gi 141 | T4 | T5 | d4 b100: T4 | recovered; b0 T4 |
| all five | bruna d3 gi 5 | T5 | T6 | d5 b100: T5 | recovered; d3 b0 T5 |

F1-F4's two (`scripts/bp_open/f14_stage1_singles.sh`, r2, single games) both recover at stage 1, so
**F1-F4 meet the whole adoption bar**: every deck net <= 0, every slower game recovered, no stage 2
needed. All six of the all-five rows ran on r2 in batch 2 (`docs/design/bp-open-data/recovery_stage1_f5_r2.txt`):
**every slower game of either arm recovers at stage 1.**

**F5's slower games at an UNLIMITED budget, their own cell's depth** (all five on, `--budget-ms 0`, one game
each, `scripts/bp_open/f5_b0_singles.sh`): **every one plays at the off arm's turn** -- Mirrorwing shipped
gi 141 T4 (was T5 at b20), creature_giving shipped gi 196 T6 (was T7), creature_giving d3 gi 298 T5 (was
T6), bruna d3 gi 5 T5 (was T6). So F5 loses nothing at a large budget (the doctrine's invariant holds);
what it costs at b20 / b10 is pure budget churn.

**Why `NEW_LAST` cannot help, and what would.** Mirrorwing gi 141 diverges on turn 2: off casts Elvish
Mystic, Ancestral Anger (drawing Game Trail) and attacks for 2, wins T4; all five cast Mystic, Fists of
Flame (drawing the same Game Trail), no attack, and win T5 for 66: a different turn-2 base plan, not a
different post-draw continuation. The inference (from b0 restoring it and `NEW_LAST` not moving it): what
moved the root's choice is the budget the demoted waves spent INSIDE the lookahead -- every walker of
every node below the root hands its demoted wave out, last in its own slot, and those few applies per
node add up to +21% units on Mirrorwing shipped (on vs F1-F4) -- so the root decision's per-decision
budget covered less of the tree the T4 line lives in. "LAST" in each walker is local; it does not make the demoted work later than the
REST of the tree. The ordering that would is global: a **decision-level second pass** -- search the
decision with the demoted waves closed, and only when that search completes inside the budget re-open
them (iterative widening). At a binding budget that plays exactly the F5-off line; at an unlimited one it
reaches every demoted continuation. The same shape would take F1-F4's two Hinata2 shipped slower games
out if they are churn too (stage 1 above). Not built: it is a search-architecture change and F5's
default is the USER's.

### 4.5 Exactness at an unlimited budget (d3 b0)

Batch 2 on r2 (`build/scratch/bp_open/b2`, read by `scripts/bp_open/analyse_b2.sh`; raw
`docs/design/bp-open-data/exact_r2_vs_off.txt`). Depth 3, `--budget-ms 0`, same seeds per arm. The test:
at an unlimited budget the levers only ADD options, so no game may be slower than off.

| deck | games/arm | F1-F4 vs off: fast / slow | all five vs off: fast / slow | units x at b0 (F1-F4 / all five) |
|---|---|---|---|---|
| auras | 50 | 0 / 0 | 0 / 0 | 1.006 / 1.071 |
| critter | 50 | 0 / 0 | 0 / 0 | 1.015 / 1.041 |
| dragonstorm | 50 | 0 / 0 | 0 / 0 | 1.000 / 1.000 |
| fluctuator | 50 | 0 / 0 | 0 / 0 | 1.049 / 1.049 |
| goblins | 50 | 0 / 0 | 0 / 0 | 1.002 / 1.005 |
| creature_giving | 20 | 0 / 0 | 0 / 0 | 1.026 / 1.072 |
| mirrorwing | 20 | 0 / 0 | 0 / 0 | 1.011 / 1.128 |
| pirates | 20 | 0 / 0 | 0 / 0 | 1.032 / 1.249 |
| th | 20 | 0 / 0 | 0 / 0 | 1.003 / 1.003 |
| hinata | 3 | (not run) | 0 / 0 | -- / 1.230 |

**No game is slower, and none faster, on any deck: 330 games per arm, plus 3 Hinata2 games for all
five.** The fourth Hinata2 game pair (gi 0) was the batch's whole tail, 2 of 20 workers for its last
stretch, and I stopped it rather than hold the box lock for one game; it is not in the table. Every play
digest matches off except one Hinata2 chunk, which has the same win turns.

### 4.6 Identity detail

`docs/design/bp-open-data/identity.txt`: 68 jobs, 1,700 games, play digests 68 / 68 identical. The 4 jobs
whose UNITS differ (all KittyEquipment, <= 0.14%) ran in batches of different composition: the
breakpoint-enumeration memo is thread-local and survives a batch worker's job switch
(`ClearPerGameCaches` does not clear it -- its own note: it moves the work meter, never play), so a
job's units depend on which jobs shared its worker. A same-manifest rerun of the 4 jobs on both binaries
is the clean check; not run.

### 4.7 The NEW_LAST deletion and the executor lockstep fix

One manifest, three binaries back to back in one hold of the box lock (`scripts/bp_open/lock3.sh`): all 33
suite decks, shipped b20 + d3 b10, 40 games per cell, every lever at its shipped default, fresh seeds
10700001+. r2 vs r3 = the deletion (must be byte-identical); r3 vs r4 = the lockstep fix (the movers).
Ran 12:53-13:04 UTC (2026-10-15), 132 jobs / 2,640 games per binary
(`docs/design/bp-open-data/lock3_r2_vs_r3.txt`, `lock3_r3_vs_r4.txt`):

* **The deletion is play-identical:** r2 vs r3, play digests identical in **132 / 132** jobs.
* **The lockstep fix moves no game in the sample:** r3 vs r4, play digests identical in **132 / 132** jobs,
  so no slower game and nothing to recover. It is a correctness fix for a path these 2,640 games did not
  reach (Melira Pod, the one pod deck, is in the sample at 80 games); no lever.
* **Units** differ in 13 jobs r2 vs r3 and 9 jobs r3 vs r4 -- the same jobs (all three KittyEquipment
  lists, Snow, one Hinata2 chunk), in both directions (kittyv2 d3 c0: 204,233 -> 194,440 -> 204,180), with
  identical play. The executor change cannot move search work at all, so this is the run-to-run job
  history of the thread-local enumeration memo (4.6), not either change.

Both r3 and r4: `mtg-test` 1061 / 1061, scenarios 162 / 162. Rebased onto a39ec088: `mtg-test`
1077 / 1077, scenarios 163 / 163.

## 5. State (2026-10-15) and what the USER decides

* **Base:** rebased onto 16dfb7e4 (work/integ-2026-10-14). Branch `worktree-agent-a9469f45bff622cfb`.
  Conflicts in the rebase: only the two append-only lists (`HeuristicArm.h`'s enum tail + `kNames`, and
  `CMakeLists.txt`'s test list), resolved by keeping both sides, integration's entries first.
* **Shipped default:** F1-F4 ON, F5 OFF (`MTG_BP_WAVE_NEW_LAST` deleted). The two Mirrorwing scenarios set
  `MTG_BP_NEW_ONLY_DEMOTE=1` in their `env` (at `=0` neither line is found: opponent at 84 / 46).
* **Measured binary:** `r2` (= this branch's source). Full `mtg-test` 1062 / 1062, scenarios 162 / 162
  on `r2`.
* **Scripts:** `scripts/bp_open/`. `build.sh <tag>` builds and copies the binary to
  `build/scratch/bp_open/bin/<tag>/`; `stage_data.sh` copies the inputs onto the build volume;
  `run_boxed.sh <tag> <dir>` runs `<dir>/manifest.json` through the box lock; `plan_r2.sh` / `plan_b2.sh`
  build the two batches above; `make_ab.py analyse <dir> --base off|nodemote|on`,
  `make_recovery.py analyse <dir>`, `cmp_bases.py` read them.
* **The USER decides:**
  1. **F5 (`MTG_BP_NEW_ONLY_DEMOTE`).** The only lever that makes the Mirrorwing order lines findable,
     and the only one with a deck net above 0 (creature_giving +2, bruna +1) at +24-29% units on
     Mirrorwing / Pirates shipped. Every one of its slower games plays at the off arm's turn at an
     unlimited budget (pure budget churn), and `NEW_LAST` does not take the churn out (4.4); a
     decision-level second pass would (4.4, proposal). It modifies
     `MTG_BP_NEW_ONLY`, a USER-adopted rule, and re-admits the "pull-order" lines the 2026-09-21 ruling
     declined to preserve. Ships OFF until the USER rules. It is the same question as the cast-order
     walk-through's Q140 (Mirrorwing: keep an old-cards-only continuation when the rank order cannot
     express it).
  2. **A3 (6.1),** the one-deviation-per-apply representation: a project, proposal only.

## 6. Not fixed

### 6.1 One deviation per apply (#19; A3 of no-greedy-in-search-window.md) -- needs the USER

`Plan::bp_choice` / `bp_at` carry ONE deviation per apply. At every other class-on breakpoint of that
apply the line plays the nested canon (`cands.front()`) -- or, for a `bp_all` variant, the same rank. So
a line that needs a non-front choice at TWO breakpoints of one main phase (Ponder: keep rank 3, then
Preordain's continuation rank 2) is unfindable at every budget, except where (a) the uniform / chain
arms happen to express it -- and wave 0 emits those only for the sites it fans, so never for site 3 or a
`MAXBASE` overflow plan; (b) the main-1 / main-2 boundary splits it across phases; or (c) Hinata's
main-2 fixpoint re-entry supplies the second decision. F1-F5 make every SINGLE deviation reachable,
including EMPTY and the demoted entries, at every index; they do not touch this.

Two designs, both budget-staged so the cross product is a price and never a gate:
1. **A deviation SEQUENCE.** `Plan` carries up to K `(bp_at, choice)` pairs. The walker's nested
   discovery opens slots off every SCORED variant whose apply reached further class-on breakpoints, not
   only off base plans, so a second deviation is a later wave than the first (wave = deviation count).
   Complete at unlimited budget for any finite breakpoint count. Executor: `resolve_draw_breakpoint`
   matches its index against the sequence (a verified committed line already replays its recorded
   script). Recommended.
2. **Recursive node hosting.** A node child that reaches a nested breakpoint pends again and the plan
   loop hosts the nested node; each level's continuation is recorded in the committed line.

Either is a representation change touching every `bp_choice` reader (about 30 sites, three executor
twins). It is a project, not a lever, so it waits for the USER.

**Design 1 implemented 2026-10-29 as `MTG_BP_DEV_SEQ` (heurarm `BP_DEV_SEQ`, default ON; `=0` restores one
deviation per apply), SHIPPED SCOPE = the LATENT occurrences only; the full cross product is
`MTG_BP_DEV_SEQ_ALL` (default OFF, see below).** Found by Goblins seed 1330145 gi144 under `MTG_SUBSET_SAC_OUTLETS`: the T3 kill's
main 2 needs non-front continuations at two site-9 occurrences (Skirk Prospector's sacrifice bursts, then
Siege-Gang activations at the re-open). Without the outlets the uniform arm expressed it by luck (rank 1 at
both); the outlets admitted more continuations ahead of the three-sacrifice burst (rank 1 -> rank 8), and
the search lost the T3 kill at `--depth 8 --budget-ms 0` (seed 1343448 gi3447 is the same mechanism).
* `Plan::bp_pre` holds the sequence's earlier entries (a choice per class-on occurrence before `bp_at`, -1 =
  untargeted); `TurnSolver::BpChoiceAt` is the one reader, used by `bp_searched_plan`, the site-9 re-open,
  `BpUnbranchedCanon` (an overrun prefix entry is EMPTY, like an overrun target), `ClassifyBpOccurrence`,
  and the executor's three twins (main site, pod chain, site 9 and its re-open).
* The walker opens a COMPOUND slot off every variant it hands out, and off every variant a host applied
  itself (wave-0 ranks and node children in the two FSLine hosts, seeded through
  `BpWaveWalker::SeedDevSeq`), that reached a LATENT occurrence: site 9's re-open counts its next occurrence
  only when targeted -- an untargeted re-open applies NOTHING, not the canon -- so one deviation can reach
  no continuation there at all; the apply reports it (`g_bp_seen_latent`) instead of counting it. At every
  other class-on occurrence an untargeted one plays the canon, the one-deviation coverage this section
  describes; `MTG_BP_DEV_SEQ_ALL` opens sequences there too. Compound slots
  wait in `m_deferred` until every slot of fewer deviations retired (wave = deviation count), so a finite
  budget spends on exactly what it spent on before first. Searched applies only (a playout's untargeted
  occurrence is EMPTY). The prefix-resume cache keys a compound slot by the slot, and the cross-node length
  memo never records one (its list depends on the earlier deviations).
* **Priced (rebased 2026-10-29 onto the billed canon wave).** A compound variant meets its later occurrences on
  mid-turn states no other line reached, so its apply derives continuation lists the cache has never seen, and a
  derivation is an enumeration that bills no unit (`MTG_ENUM_CHARGE` / `MTG_BP_DERIVE_METER`, the open USER
  decisions for every apply). The first version billed them itself (`DevSeqPriceScope`, one unit per 4 visits);
  it is gone: a compound hand-out now bills through `MTG_BP_CANON_WAVE`'s `CanonWaveBillScope` exactly as a
  canon-wave base does (`MTG_BP_CANON_WAVE_W` visits per unit, default 1), nullptr at an unlimited root, so
  `--budget-ms 0` never pays it.
* **Two passes (2026-10-29): what bounds the walk at an unlimited budget.** Nothing in the price does, and the
  compound walk was long there: Goblins seed 1380001 gi2586 at d8 b0 took 19-29 min / 207,741 units with the
  lever, 1.2 s / 1,351 units without (both win T3). The cost was not duplicate states -- a transposition on the
  sequence's decision state would have removed ~1.6x -- but ORDER: at turn-3 main-2 nodes with no turn-3 kill the
  walker built 1,281 compound slots off site-9 lists of up to 874 entries and handed out 206,390 variants, each
  deriving canon continuations at boards no other line reached (~86% of the time, 59k distinct boards), all before
  the depth-first search reached the one-deviation turn-3 kill that cuts every one of them. So
  `TurnSolver::FullSearchLine` now runs iterative deepening on the DEVIATION COUNT, with branch and bound:
  1. pass 1 with every compound wave suppressed (`g_devseq_suppress`; the walker and the host seeds open none);
  2. only if pass 1 met a latent occurrence -- the only thing a compound slot is built from, so without one the
     full tree IS pass 1's tree -- pass 2 over the full tree at pass 1's committed depth, its root cut at pass 1's
     win turn - 1, keeping its line only if strictly earlier.
  Every line winning before pass 1's lies inside pass 2's window, so the win turn is the one-pass search's at
  every budget pass 2 completes. Pass 2 does not repeat pass 1: it takes pass 1's line memo minus every node whose
  walk met a latent occurrence (`DvsDirty` names the node's keys on exit, `FslEraseDirty` drops them before pass 2;
  such a node answered for the smaller tree), so it re-walks only the paths to compound slots. A clean node that
  skipped a dirty subtree (a memo hit, a dedup) is not dirty, and need not be: that subtree's own path was walked
  and is dirty, so pass 2 re-walks it there. The two caches that outlive a pass keep the passes apart: the
  game-line cache files a dirty pass-1 node under a second tag (`gamelc::SupTag`) that only a first pass reads,
  and a hit there counts as meeting a latent occurrence (its subtree holds one); the searched-m2 memo folds the
  suppression into its key. The first version simply did not store dirty nodes in the game-line cache, and that
  starved later decisions: Melira seed 1440491 gi490 committed a depth-2 T6 line at T3 where the lever-off arm
  committed a depth-3 T5 one (pass 1 missed the entries the lever-off search reuses). With the tag, pass 1 reads
  what the lever-off search would read, plus the full-tree answers earlier second passes stored (gi490 now plays
  the lever-off arm's game exactly, digest and all).
  `MTG_DEV_SEQ_VERIFY=1` runs the one-pass search beside the two passes and counts every decision where it wins
  earlier (`TWO_PASS_LATER`, must read 0 at an unlimited budget); `MTG_DEV_SEQ_STATS=1` prints the pass counts.
  d8 b0: gi2586 1,469 units (0.85 s, T3), gi144 13,049 (T3; the one-pass search 2,731 -- its compound kill is
  found early and cuts the rest, where pass 1 must first prove the one-deviation tree only reaches T4), gi3447
  9,787 (T3; lever off T4).
* **Why `_ALL` is off (measured 2026-10-29, partial pooled run, outlets on).** It is the whole cross product,
  and without a breakpoint-level TRANSPOSITION it is intractable on a long chain: the regression tier's
  unbudgeted backstop abandoned Fluctuator games at the 2,000,000-unit ceiling at d3 AND d5 (seeds 30009,
  30018; ~5-9 min each, every other backstop row finished), Hinata's backstop units rose 2.4x, and Goblins at
  its play setting spent +35-39% units (1.7-3.4x wall) over the lever-off arm for -0.0007 / -0.0012 avg (1,500 /
  4,000 games: 3.7780 -> 3.7773, 3.7765 -> 3.7753). The
  transposition has to key a decision on the apply's whole RESUME context, not the board: the dig loop's
  counters and drawn lands, the deferred-site flags, the pin cursors, `g_line_unpaid_cost` -- i.e. a
  `BpPrefixSnap` at every site, which today exists only at the deferred ones. That is what `_ALL` needs
  before it can be the default; the one-deviation gap at counted occurrences stays open until then.
* The `SolveWithLookahead` host seeds its own wave-0 variants too (2026-10-29; `la_dvs_seeds`, read off each
  variant's own apply and handed to its walker like the FSLine hosts' seeds). A variant whose apply is shared or
  folded (a combat-pin share, the put-bounce / ETB / ETB-counter folds) is not read: it repeats a sibling's apply,
  whose seed already covers it.
* Unit test: `test/unit/test_bp_dev_seq.cpp` (the reader, the compound waves and their order, the latent
  route, a seeded variant).
* **Measured (2026-10-29, pooled held-out A/B, play settings, `MTG_PAY_REFINE` on; every suite deck at seed
  1370001 + Goblins / Fungus 4,000 each at 1380001; arms: c = outlets off, a = `MTG_SUBSET_SAC_OUTLETS` on,
  p = outlets on + this lever, priced).** a -> p: 11 faster / 6 slower; Melira and LHC moved digests
  with no win-turn change, every other deck's outcomes are identical. AccomplishedAlchemist 4 / 0
  (4.8594 -> 4.8281), FiveColour 1 / 0, Goblins 1 / 1, Goblins 4,000 5 / 3, Landfall 0 / 1, EDF 0 / 1
  (gi452 5 -> 7, the price's budget churn).
  c -> p: 52 / 11. Every slower game of p vs c wins on c's turn at `--depth 8 --budget-ms 0`. p's units
  include the new price (Goblins 2.1-5.0x a's, which never bills derivations); wall a -> p 1.1-2.1x on
  Goblins, flat elsewhere. The unbudgeted backstop moved no row (units +0-2.5%, Melira +34-53%).
* **Cost fold (2026-10-31): `MTG_DEV_SEQ_TAIL_SKIP` (heurarm `BP_DEV_SEQ_TAIL_SKIP`, default ON; `=0` restores the full
  second-pass re-walk).** USER 2026-10-31: *"the devseq situation could be okay if it improves quality notably and only
  costs a little more."* Measured on the stack (`wip/goblins-stack`, 41 suite cases, seed 1580001, play settings), the
  lever cost x1.077 units for 8 faster / 1 slower; Melira Pod +27% (12.93M -> 16.40M) with **no game moved**.
  * **Where it went** (`MTG_DEV_SEQ_STATS=1`, which now also prints units per pass, per unit site inside the second
    pass, per host for compound hand-outs and a hand-out outcome census). On Melira the whole cost was the SECOND
    PASS (pass 1 = 12.79M, the lever-off tree; pass 2 = 3.48M over 254 of 484 decisions, `pass2_better=0`), and
    ~70% of pass 2 was not compound lines at all but the RE-WALK of dirty nodes: a node whose subtree met a latent
    occurrence anywhere is erased from the carry, and its re-walk re-ran every main-1 option's tail -- the main-2
    loop applying every plan (`fs_main2` 1.52M) after re-applying every main-1 plan (`fs_pre` 0.66M) -- although
    the latent occurrence sat under one option and the others had walked trees the second pass sees unchanged.
    The compound hand-outs themselves were 0.66M: 368k of them, only 18k landing on a new state (42k past the
    end, 33k on a wave-0 / node state, 79k on an ordinary walker hand-out's, 139k on another compound slot's, 7k
    within their own slot). On Goblins the balance is the other way: pass 2 2.76M, of which compound main-2
    hand-outs 1.53M (178k hand-outs, 55k fresh; most of their units are the billed derivations of
    `MTG_BP_CANON_WAVE`'s price, `enum_charge` 1.36M) and the re-walk ~1.1M.
  * **The folds** (all identities within the second pass's window; the second pass runs under root cut R - 1, R =
    the first pass's win turn, so every cutoff in it is <= R - 1):
    1. *Tail skip* (`DvsTailGate`, at the five FSLineTail sites of FSLineWin). The first pass records, per (node,
       post-combat tail state, combat kind), the tail's cutoff, its answer and the EARLIEST turn at which anything
       under it met a latent occurrence (or read a dirty node's answer, which carries that node's earliest turn:
       `DvsMltFrame`), for a COMPLETE tail only (no truncation, no budget bind, no first-win horizon exit, so its
       answer is the minimum within its cutoff). The second pass skips the same tail under `cut` iff the first
       pass's cutoff was at least as loose, its answer is later than `cut`, and its earliest latent turn is later
       than `cut`: re-walking it would be the first pass's walk under a tighter cutoff (any compound line under it
       starts after `cut` and is cut at once), so its answer is "no win by `cut`", which is what the skip returns.
    2. *Node keep* (`FslEraseDirty`). A dirty node whose walk was complete, whose earliest latent turn is past the
       root cut and whose win entry is not a line found past its own cutoff keeps its first-pass entry.
    3. *Apply skip* (main-1 plan loop). A plan whose apply met no latent occurrence, took no canon record, was not
       dork-contested, and whose tail is skippable as in 1 is skipped before its unit and its apply; the NSKIP length
       the walker reads is replayed, and a base plan is skipped only at a node with no wave-0 variant and no fold
       armed (a fold or collapse that does not fire just evaluates its candidate in full). Its identity across the
       passes is its static index in the node's list (the id-order hint's rotation undone), guarded by a content hash.
    What changes is only what lies PAST the window -- values >= R that the second pass discards anyway (`take2` needs
    < R) and that the game-line cache files as the same refutation either way (a past-cutoff line is stored only as
    "no win by the cutoff"). Nodes and tails that record per-option values for a later pass (`rec_vals`: the probe's
    beam / oracle order) are never skipped or kept; `DomActive` disarms all three. At `--budget-ms 0` the second pass
    returns exactly what it returned; under a budget it spends fewer units, so only downstream budget dynamics could
    move. Unit test: `test/unit/test_dev_seq_tail_skip.cpp` (the skip predicate's truth table, `=0`).
  * **Measured (same manifest, same binary, `MTG_DEV_SEQ_TAIL_SKIP` false vs true per job, one pooled batch, 8
    threads).** Every one of the 41 decks' digests is IDENTICAL (0 moved of 18,117 games); the lever-false arm
    reproduces the stack binary's digests and units exactly. Units, ratio to the `MTG_BP_DEV_SEQ=0` arm:

    | deck | dev seq off | on (before) | on + fold | before | after | fold / before |
    |---|---|---|---|---|---|---|
    | Melira Pod | 12,934,517 | 16,401,303 | 15,399,615 | 1.2680 | 1.1906 | 0.9389 |
    | Goblins (4,000) | 20,551,327 | 23,582,515 | 23,378,482 | 1.1475 | 1.1376 | 0.9913 |
    | Goblins 2HG (2,000) | 28,588,666 | 32,341,388 | 32,204,220 | 1.1313 | 1.1265 | 0.9958 |
    | Landfall | 8,307,689 | 8,811,570 | 8,747,050 | 1.0607 | 1.0529 | 0.9927 |
    | Minotaur | 746,055 | 780,917 | 780,728 | 1.0467 | 1.0465 | 0.9998 |
    | Vampires | 1,372,217 | 1,401,638 | 1,398,196 | 1.0214 | 1.0189 | 0.9975 |
    | FiveColour | 6,199,237 | 6,253,255 | 6,247,051 | 1.0087 | 1.0077 | 0.9990 |
    | EDF | 903,777 | 826,170 | 825,027 | 0.9141 | 0.9129 | 0.9986 |
    | **all 41** | 140,465,806 | 151,226,777 | 149,807,183 | 1.0766 | 1.0665 | 0.9906 |

    Wall (core-ms, same batch) 0.97x. Goblins seed 1330145 gi144 / 1343448 gi3447 / 1382587 gi2586 at `--depth 8
    --budget-ms 0 --ignore-play-profile`: T3 / T3 / T3 with the fold, digests identical to the fold-off arm. Melira's
    remaining second pass (2.48M): compound hand-outs 0.64M, main-1 re-applies the apply skip may not take 0.56M
    (bases at nodes with wave-0 variants), and the main-2 loops of the tails that DO hold a latent occurrence inside
    the window or a compound subtree 0.79M.
  * **What is left, and why it is not folded here.** Over all 41 cases the remaining second pass is 9.02M units (of
    the lever's 9.34M); compound hand-outs are 4.50M of it, and the ones that land on an already-reached state cost
    1.70M (main 1 0.52M of 1.02M, main 2 1.18M of 3.49M -- the rest is the search under the 189k that are new).
    (a) Compound hand-outs that land on a state some other hand-out
    already reached (Melira 139k of 368k on ANOTHER compound slot's state, Goblins main 2 59k of 167k): two compound
    slots of one base plan whose different first deviations converge (activate X then Y vs Y then X). Folding them
    needs a TRANSPOSITION on the slot's decision state at the latent occurrence -- the board plus the apply's whole
    resume context there (section windows, deferred-site flags, the re-open's activation snapshot, pin cursors), i.e.
    a `BpPrefixSnap` at site 9 -- which is the same missing piece `MTG_BP_DEV_SEQ_ALL` needs. A key on the board alone
    is not provably complete, so it is not done. (b) A compound slot's own EMPTY arm is not the parent's line: it
    counts the occurrence (bp_searched_plan's section-window reset, `g_bp_fired_last` for the main-2 fixpoint), and
    1.8k of Goblins' 17.5k main-2 compound EMPTY arms do reach new states. (c) Re-applying a dirty node's main-1 base
    plans where wave-0 variants depend on them: skipping such an apply would un-fire the wave-0 collapses and ArmNew
    declines that their variants rely on, so it would cost more than it saves.
  * **A completeness gap found on the way (not fixed: it changes play, so it is the USER's call).** A first-pass node
    that met no latent occurrence itself but READ a dirty node's memo answer is not marked dirty: it is carried into
    the second pass as a full-tree answer, and filed in the game-line cache under the full-tree tag, although it rests
    on a smaller-tree answer. Within one decision the win turn survives (the dirty node is re-walked on its own path),
    but the game-line entry can hand a later decision the smaller tree's answer. `MTG_DEV_SEQ_STATS` counts these
    nodes ("clean nodes that read a dirty answer"): 333 over the 41 cases' 22,718 first passes, so rare. The fix is to treat such a read as dirtying (the earliest-latent
    scope already propagates it), at the price of more second-pass work.

### 6.2 Smaller things seen, not changed

* **Executor lockstep gaps -- FIXED (2026-10-15, no lever).** The pod-chain twin (`AIEngine`, site 7)
  tested `pod_bp_idx == plan.bp_at` and indexed the list with the raw `bp_choice`; the site-9 twin
  indexed with the raw `bp_choice` too. `bp_searched_plan` makes a `bp_all` (uniform) variant eligible
  at EVERY class-on occurrence and resolves `kBpChainChoice + j` through `BpChainCandIndex`. So a uniform
  variant's pod occurrence at any other index, and a chain variant at site 7 or 9, were scored with the
  variant's continuation and then played with the committed mirror's canon (or the re-solve) at site 7,
  or with nothing at site 9: the committed line was not the line the search ranked. Both twins now
  resolve exactly as the main executor site and the rollout do (eligibility `bp_all || idx == bp_at`;
  chain via `BpChainCandIndex`; a chain scan with no entry falls through as the rollout's does).
  Measured in section 4.7.
* **The SolveWithLookahead candidate loop has no main-2 fixpoint for any line**, base plans included.
  Consistent within that host (the executor's own fixpoint covers real play), so F3 leaves it.
* **Node children do not get the main-2 fixpoint.** Their siblings are the full re-decision of the
  breakpoint, so this is not a closure of the breakpoint's options; it is one more place where only
  the one-deviation limit (5.1) bites.
* **The main-2 walker dedups only against `node_child_seen`**, not against the plan loop's own
  `m2_seen_states`, so a walker line equal to a scored plan is re-scored (waste, not loss).
