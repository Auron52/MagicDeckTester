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
     declined to preserve. Ships OFF until the USER rules.
  2. **A3 (6.1),** the one-deviation-per-apply representation: a project, proposal only.
  3. **The executor lockstep gaps (6.2):** the pod twin and the site-9 twin do not honour `bp_all` / the
     chain sentinel. Pre-existing, not a closure; a lockstep fixture would pin it.

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

### 6.2 Smaller things seen, not changed

* **Executor lockstep gaps, pre-existing.** The pod-chain twin (`AIEngine`, site 7) honours neither
  `bp_all` nor the chain sentinel, and the site-9 twin not the chain sentinel, while
  `bp_searched_plan` resolves both at those sites. A variant scored that way realises a different line.
  Not a search closure; worth a lockstep fixture.
* **The SolveWithLookahead candidate loop has no main-2 fixpoint for any line**, base plans included.
  Consistent within that host (the executor's own fixpoint covers real play), so F3 leaves it.
* **Node children do not get the main-2 fixpoint.** Their siblings are the full re-decision of the
  breakpoint, so this is not a closure of the breakpoint's options; it is one more place where only
  the one-deviation limit (5.1) bites.
* **The main-2 walker dedups only against `node_child_seen`**, not against the plan loop's own
  `m2_seen_states`, so a walker line equal to a scored plan is re-scored (waste, not loss).
