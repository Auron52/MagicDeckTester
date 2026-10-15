# Every breakpoint continuation open to the search, "cast nothing more" included

Status (2026-10-14, WIP): **audited; fixed behind five default-ON levers; identity, targeted cases and
the 33-deck movers check done; the per-deck A/B PARTIAL** -- stopped for the night, see section 5
(RESUME). The integrator owns smoke / regression GT. Started as the root cause of Hinata2 shipped gi 137
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

Every run on this worktree's Release build, `MTG_PGO=0`, `--threads 6`, decks from the primary checkout
(as every A/B here does, so the machine-local keep-table caches are reused). Loss = 9. Builders and
analysers: `logs/bp_open/make_ab.py` (worktree-local scratch, gitignored).

### 4.1 Targeted cases

| case | committed tree | all five on | which lever alone suffices |
|---|---|---|---|
| Hinata2 gi 137 (seed 6320138), shipped b20 | T8 | **T7** | F1 alone T7, F3 alone T7 (F2 alone T8, F4 alone T8); each one OFF alone still T7 |
| Hinata2 gi 137, `--depth 8 --budget-ms 0` | T8 | **T7** | F1 off T7, F3 off T7 |
| Hinata2 gi 137, shipped, with `MTG_BP_WAVE_NEW_LAST=1` | -- | T7 | -- |
| `test/scenarios/mirrorwing_bp_demoted_anger_before_oracle.json` (b0) | no win (84 left) | **T4** | F5; `MTG_BP_NEW_ONLY=0` also T4 |
| `test/scenarios/mirrorwing_bp_demoted_fists_before_goldrush.json` (b0) | no win (46 left) | **T4** | F5; `MTG_BP_NEW_ONLY=0` also T4 |

Unit tests: `test/unit/test_bp_wave_empty.cpp` (9 cases: the walker contract through
`TurnSolver::WaveWalkerTraceForTest` -- EMPTY last in a slot, none for an empty list or a wave-0 slot,
the node-turn mask by host, hosted nested slots, the demoted wave, `NEW_LAST` ordering); full
`mtg-test` 1042/1042 on build v3 (the final build: see section 5). Scenarios 162/162 on v3 (the two new ones included).

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

### 4.4 Per-deck held-out A/B on the nine movers -- PARTIAL (stopped for the night)

FRESH seeds 10400001+, `seed = 10400001 + 20000*deck_index + 5000*cell_index + gi` (deck_index =
position in `scripts/bp_open/make_ab.py` ALL_DECKS). Arms: `on` (all five), `off` (all five =0, the
committed tree's play), `nodemote` (F1-F4 on, F5 off). Shipped b20 (value_play depth), d3 b10. Raw:
`docs/design/bp-open-data/ab_partial_vs_off.txt`. **net = summed win-turn change, lower is better.**

| deck | cell | games/arm | on: fast / slow / net | F1-F4 only: fast / slow / net | units x (on / F1-F4) |
|---|---|---|---|---|---|
| hinata | shipped | 175 | 0 / 2 / **+2** | 0 / 2 / +2 | 1.086 / 1.022 |
| hinata | d3 | 250 | 5 / 0 / **-5** | 3 / 0 / -3 | 1.040 / 1.027 |
| creature_giving | shipped | 200 | 0 / 1 / **+1** | 0 / 0 / 0 | 1.097 / 1.026 |
| creature_giving | d3 | 300 | 0 / 1 / **+1** | 0 / 0 / 0 | 1.041 / 1.007 |
| mirrorwing | shipped | 200 | 0 / 1 / **+1** | 0 / 0 / 0 | 1.240 / 1.024 |
| melira | shipped / d3 | 200 / 249 | 0 / 0 / 0 both | 0 / 0 / 0 both | 1.029 / 1.009 ; 1.012 / 1.004 |
| fluctuator | shipped / d3 | 200 / 300 | 1 / 0 / -1 each | 1 / 0 / -1 each | 1.002 / 1.002 ; 1.024 / 1.024 |
| pirates | shipped | 200 | 0 / 0 / 0 | 0 / 0 / 0 | 1.287 / 1.060 |
| pd | shipped | 200 | 0 / 0 / 0 | 0 / 0 / 0 | 1.022 / 1.016 |
| th | shipped | 200 | 1 / 0 / -1 | 0 / 0 / 0 | 0.975 / 1.019 |
| bruna | shipped / d3 | 50 / 100 | 0 / 0 / 0 ; 0 / 1 / **+1** | 0 / 0 / 0 both | 1.009 / 1.009 ; 1.071 / 1.010 |
| (not run yet) | d3 of pd, mirrorwing, pirates, th; d0 of all nine | -- | -- | -- | -- |

**Per deck so far:** F1-F4 alone are net <= 0 on all nine (hinata -1 = +2 shipped -3 d3, fluctuator
-2, the rest 0). All five: hinata -3, fluctuator -2, th -1, melira / pirates / pd 0, but
**creature_giving +2, mirrorwing +1, bruna +1 -- every one of those four slower games is F5's**
(on vs nodemote).

**Slower-game recovery (stage 1, `--depth W --budget-ms 100`, W = the faster arm's win turn, lever arm)
-- PARTIAL, 4 of 6 run, all 4 recovered:** creature_giving d3 gi 298 (6 -> 5), creature_giving shipped
gi 196 (7 -> 6), bruna d3 gi 5 (6 -> 5), hinata shipped gi 85 (7 -> 5). Not yet run: hinata shipped
gi 79 (off T4, on T5), mirrorwing shipped gi 141 (off T4, on T5). Raw:
`docs/design/bp-open-data/recovery_stage1_partial.txt`.

### 4.5 Not yet run

* The rest of the A/B (d3 of pd / mirrorwing / pirates / th at 100 games x 3 arms; d0 of all nine at
  100 x 2 -- d0 cannot move, no search there, and the movers check confirms 0 of 33 decks move at d0).
* `onnl` (all five + `MTG_BP_WAVE_NEW_LAST=1`) on hinata / creature_giving / mirrorwing / bruna, same
  seeds: does ordering the new options after EVERY slot's ranks remove F5's churn?
* Exactness at `--budget-ms 0` (d3 b0): fluctuator / th / mirrorwing / pirates / dragonstorm / auras /
  critter / goblins 20 games x on/off; hinata / creature_giving 10. (A first pass at 25 games per deck on
  the expensive decks never started a game in 70 minutes on this box: drop bruna, melira, pd, kitty
  from it.)
* Stage 2 (`--depth 8 --budget-ms 0`) for any stage-1 miss.

### 4.6 Identity detail

`docs/design/bp-open-data/identity.txt`: 68 jobs, 1,700 games, play digests 68 / 68 identical. The 4 jobs
whose UNITS differ (all KittyEquipment, <= 0.14%) ran in batches of different composition: the
breakpoint-enumeration memo is thread-local and survives a batch worker's job switch
(`ClearPerGameCaches` does not clear it -- its own note: it moves the work meter, never play), so a
job's units depend on which jobs shared its worker. A same-manifest rerun of the 4 jobs on both binaries
is the clean check; not run.

## 5. RESUME (2026-10-14 night; stopped by the integrator for a value-leaf run)

* **Base:** 77e094a2 (phase-1-2-deck-analyzer). Branch `worktree-agent-a9469f45bff622cfb`.
* **Built:** all five levers + `MTG_BP_WAVE_NEW_LAST` (ordering only, default OFF), unit tests, two
  scenario fixtures. The measured binary of every number above is this commit's source (scratch build
  `v5` for the A/B and recovery; `v3` = the same minus the `NEW_LAST` lever and minus the
  `AddHostedDemoted` same-slot guard, for the movers check; `base` = 77e094a2 + the slot names).
* **Unit tests:** `v3` full suite 1042 / 1042; `v5` `test_bp_wave_empty` 9 / 9; the `v5` full-suite run
  was killed by the wrap-up after 440 / 440 passing (the one "failure" in its log is the SIGTERM).
  **Run `build/Release/mtg-test` in full before treating this commit as final.** Scenarios 162 / 162 on
  `v3`; rerun on the final build.
* **Scripts:** `scripts/bp_open/` (copies of the worktree-local `logs/bp_open/`). Every batch:
  `bash scripts/bp_open/run_batch.sh <bin-tag> <dir> ""` with the binary at
  `build/scratch/bp_open/bin/<tag>/mtg` (`bash scripts/bp_open/build.sh <tag>` builds and copies it;
  edit the two paths at its top if the worktree moved). Analyse with
  `python3 scripts/bp_open/make_ab.py analyse <dir> [--base off|nodemote]`.
* **To finish the A/B (one pooled batch):**
  1. `make_ab.py build --out <D>/ab2 --seed0 10400001 --decks pd,mirrorwing,pirates,th --cells d3
     --games d3=100 --arms on,off,nodemote --chunk 25`
  2. `make_ab.py build --out <D>/ab2d0 --seed0 10400001 --decks hinata,creature_giving,fluctuator,bruna,th,mirrorwing,melira,pirates,pd --cells d0 --games d0=100 --arms on,off --chunk 25`
  3. `make_ab.py build --out <D>/abnl --seed0 10400001 --decks hinata,creature_giving,mirrorwing,bruna
     --cells ship,d3 --games ship=200,d3=300 --arms onnl --chunk 25 --scale-expensive`
  4. exactness: `make_ab.py build --out <D>/exact2 --seed0 10400001 --decks fluctuator,th,mirrorwing,pirates,dragonstorm,auras,critter,goblins --cells x3 --games x3=20 --arms on,off --chunk 10`
     and `--out <D>/exact3 --decks hinata,creature_giving --games x3=10 --chunk 5`
  5. recovery: `make_recovery.py build <D>/ab <D>/rec1 --stage 1 --arm on` (6 jobs; 4 already done --
     results above), then pool them all with `merge_plain.py <D>/fu/manifest.json <the others>` (the
     manifests from tonight are still in `build/scratch/bp_open/{ab2,ab2d0,abnl,exact2,exact3,rec1,fu}`)
     and run once. Combine with tonight's `build/scratch/bp_open/ab` via `combine.sh` before analysing.
* **The finding the USER should hear now:** F1-F4 (the walker's EMPTY, the un-hosted mask, the m2
  walker's fixpoint, the node's nested slots) measure clean so far -- net <= 0 on all nine movers,
  identical play on the other 24 decks, units +0-6%. F5 (keeping `MTG_BP_NEW_ONLY`'s dropped
  continuations as the walker's last wave) is the only lever that fixes the Mirrorwing order cases, but
  it costs +20-30% units on Mirrorwing / Pirates / Stompy at the shipped budget and is +1 / +2 on three
  decks so far (its slower games recover at stage 1, i.e. budget churn). It also modifies a rule the
  USER adopted and re-admits the "pull-order" lines the 2026-09-21 ruling declined to preserve. Its
  default is the USER's call; the recommendation is ON if `onnl` (or a narrower demoted set) takes the
  churn out, OFF otherwise.

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
