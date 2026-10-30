# Cache unit-neutrality audit (2026-10-30)

**The rule (USER 2026-10-30):** *"If we are adding caches we should make sure they don't change the units."* And: *"it
would be nice to fix it entirely if there are more caches that are inconsistent this way."*

**Why it matters.** Work units are the search budget (`SearchBudget::Consume`, `src/ai/SearchBudget.h:72`, which also
feeds the per-game meter `gamework::Add` and the per-decision meter `decisionwork::Add`). Suppose a cache hit skips
charges that its miss path would make. Then the search goes further on the same budget. That is a play change nobody
measured as one, and play starts to depend on cache state: capacity, eviction, clear-on-full, RAM, core count and thread
interleaving. The iterative-deepening start gate, the 25x overrun ceiling, the decision-work ceiling (armed in
generation) and the per-game abandon ceiling all read units.

**Scope and method.** Read-only audit of `src/ai` and `src/core` at `330cc85bf` (branch `phase-1-2-deck-analyzer`). No
builds and no runs. Every figure below is quoted from source comments or the ledger, not measured here. "Neutral" means
`units(game | this cache on) == units(game | this cache off)`, with everything else at shipped defaults.

## Summary table

Classes: **NEUTRAL** (the miss charges nothing, or the hit replays the identical charge); **DRIFTS** (the hit charges
less than the miss); **UNCLEAR** (the question that settles it is given in the details); **ORDER** (not a hit-skips-work
cache: a stored hint reorders search, so it moves units by design).

"Cap-dep" means what decides whether an entry is present:

- **G**: a global pool shared across threads, so it depends on thread interleaving.
- **R**: a cap derived from RAM or core count (`src/core/MemBudget.cpp`), so it depends on the machine.
- **F**: a fixed constant cap.
- **-**: no binding cap.

"Play chg" means whether the proposed fix changes today's budgeted play.

| # | Cache | Decl / hit site | Class | Cap-dep | Proposed fix | Play chg |
|---|---|---|---|---|---|---|
| 1 | **Breakpoint-continuation plan cache** `bpcache::t_plans` (`BpEnumEntryFor`) | `TurnSolver.cpp:4761`; hit `:97627`; derive `:97670`; clear/store `:97762-97795` | **DRIFTS** (the known instance, confirmed) | **G** + F (8192, clear-ALL on full or pool refusal) | Land the cyclone-mem `cachecharge` mechanism (mode 1 now), then mode 2 once #8/#9 replay | y (Kitty / equipment decks) |
| 2 | Canon-front memo `bpcanon::t_fronts` (`BpCanonFront`) | `:4770`; hit `:97876`; store `:97913-97921` | **DRIFTS** (same channel as #1) | F (16384, clear-all) | Same as #1 (cyclone-mem already adds `Cost` to it) | y (Kitty) |
| 3 | Enum memo, m1 host `enummemo::t_cache` (`EnumeratePlansWithLand`) | `:79305`; hit `:79524` | NEUTRAL at defaults; DRIFTS with `MTG_ENUM_CHARGE` | **G** (promotion admission) + F | Covered by `cachecharge` (cyclone-mem); content holes fixed there by `MTG_PLAN_CACHE_EXACT_KEY` | n at defaults |
| 4 | Enum memo, m2 host (`EnumeratePlansM2Memoized`) | `:79698`; hit `:79726` | as #3 | as #3 | as #3 | n |
| 5 | Greedy-Solve memo `solvememo::t_cache` (`TurnSolver::Solve`) | `:46772`; hit `:47387-47410` | **DRIFTS on Snow** (greedy-walk charge, W=16); NEUTRAL elsewhere | F (16384, clear-all) | Record greedy-budget units in the entry and replay them (`cachecharge` greedy lane); fix the W-residual (see details) | y (Snow) |
| 6 | Searched-m2 memo `solvememo::t_m2cache` (`SearchedSecondMainMemoized`) | `:46818`; hit `:95735-95770` | **DRIFTS by design** ("a hit skips the nested search's budget consumption") | F (16384, clear-all) | USER ruling (transposition class, see Recommendation) | y (all decks) |
| 7 | Rollout pre-combat memo `m1memo::t_cache` (`RolloutPreCombatSolve`) | `:95838`; hit `:95925-95955` | **DRIFTS by design** | **R** (byte cap from BudgetMb/Workers, clear-all) + F | Fix the cap to a constant now; replay is a USER ruling | y |
| 8 | Equip-route verdict memo `equiproute::TrialPolicy` DecisionMemo | `DecisionProviders.cpp:40894-40895`; hit `:40899-40912`; store `:40935` | **DRIFTS** (hit 0 vs miss 2 units × DFS links) | F (2^20, clear) | Store link units per verdict and replay on a hit (mode 2 for the route) | y (Kitty / equipment) |
| 9 | Equip-route DFS transposition `SeenForThisDecision` / DSeen | `DecisionProviders.cpp:40206-40233`; hit `:40488-40506` | **DRIFTS** ("Hits skip uncharged", `:40153`) | F (2^20, clear) | Store the subtree's link units per board and replay | y (Kitty) |
| 10 | AA route memo `aaroute::TheMemo` | `DecisionProviders.cpp:47573-47578`; hit `:47587-47591` | NEUTRAL at defaults (price 0); DRIFTS if `MTG_AA_ROUTE_NODE_UNITS > 0` (per GAME, so across decisions) | F (2^16, clear) | Replay `price × applies` if the price is ever turned on | n |
| 11 | SimulateToEnd TT, win half (`TranspositionTable::m_map`) | `TranspositionTable.h:54`; hit `TurnSolver.cpp:81735-81771` | **DRIFTS by design** ("no budget is consumed — avoided recompute is the point", `:81707`) | **R** (`DefaultTtCapEntries`, stop-storing) | Constant cap now; replay is a USER ruling (cutoff-dependent, see details) | y |
| 12 | SimulateToEnd TT, no-win half (`m_nowin`; `MTG_UNBUDGETED_LEAF_MEMO` ON at every budget) | hit `:81798-81880` | **DRIFTS by design** ("a budget only re-spends the rollouts it saves", `:81098`) | **R** | as #11 | y |
| 13 | Spent-budget no-win memo `xmmemo` (`MTG_EXH_LEAF_MEMO`) | `:81001-81030`; hit `:81905-81925` | **DRIFTS by design** ("a hit frees the units the recompute would have consumed", `:80993`) | F (2^20, clear) | USER ruling; also see the pass-identity question | y |
| 14 | FSL line cache `FSLineCache` (win / no-win / provisional / order-free wave) | `:82595`; hits `:85926-86020` | **DRIFTS by design** (a hit returns a whole searched subtree) | **G** (`FslPool`) + **R** (`DefaultFslCapEntries`) | Per-thread pool with a constant quota now; hit==miss is not a recorded quantity here (see details) | y |
| 15 | Bottoming shared TT `m_shared_tt` | `AIEngine.h:360`; `AIEngine.cpp:2055-2057` | **DRIFTS** (game-persistent leaf memo across a trial's decisions) | **R** | Per-decision tables in trials, or replay with #11 | y (bottoming) |
| 16 | Game leaf cache `m_leaf_cache` (`MTG_LEAF_CACHE`) | `AIEngine.h:373`; `AIEngine.cpp:3969-3971` | DRIFTS when on; **OFF by default** | **R** | none needed (off) | n |
| 17 | ID order hint `g_idh_map` (`MTG_ID_ORDER_HINT`) | `TurnSolver.cpp:83883`; read `:86644` | **ORDER** (units 0.928x by design) | F (262144, stop-admitting); its contents inherit #14's **G** | No replay (not a skip); goes deterministic once #14 is | n (once #14 is fixed) |
| 18 | Value-beam probe values `g_probe_plan_vals` | `:83841`; read `:86559` | **ORDER** | inherits #14 | as #17 | n |
| 19 | Hybrid learned R / G (`g_single_R_key`, `g_emul_key`, `HybridLearnedStateClear`) | `:83570`, `:83965`, `:83989` | not a memo, but **amplifies** any drift (learned from `Used()` deltas, it drives the start gate) | per game | none; goes clean as #1-#15 do | n |
| 20 | Prefix-resume snapshot `BpPrefixSnap prefix_cache` | `:85475`, `:88063`, `:94551` | **UNCLEAR** | F (min(256, 32 MB / state)) | Replay the bind / trunc / condemn deltas, and settle the charge question | ? |
| 21 | Combat-pin share `cp_cache` | `:92805`; hit `:93833`; store `:93903` | **UNCLEAR** | per pass | Guard on all budgets (decision budget, gamework, decisionwork, binds), as EtbcFold does | ? |
| 22 | Payable-mana cache `g_mana_cache` | `SpellEffects.cpp:5262`; hit `:6144` | NEUTRAL (no budget below it) | **R** (byte cap) + F (500k) | none for units (play-exactness depends on key completeness; `MTG_MANA_CACHE_VERIFY`) | n |
| 23 | Fluctuator keep-payable `t_cache[4]` | `DecisionProviders.cpp:27834`; hit `:27858` | NEUTRAL for units; **determinism hazard** (hash-only compare, never cleared) | - | Clear per game, compare the full key (play-exactness, not units) | n |
| 24-40 | Neutral rest (see below) | - | NEUTRAL | - | none | n |

**Counts.** DRIFTS at shipped defaults: **12**:

- #1, #2, #8, #9: plan and route caches, i.e. derivation and sub-search caches.
- #5: the Snow lane only.
- #6, #7, #11, #12, #13, #14, #15: the transposition class.

Another **5** drift only under a default-off lever: #3, #4 (`MTG_ENUM_CHARGE`), #10 (AA price), #16 (`MTG_LEAF_CACHE`)
and BpLenMemo (`MTG_BP_NSKIP_GLOBAL`). The rest:

- **ORDER: 2** (#17, #18), plus the amplifier #19.
- **UNCLEAR: 2** (#20, #21).
- **NEUTRAL: about 20** (#22, #23 and items 24-40).

**Capacity-dependent DRIFTS, the worst case:**

- #1 and #14 depend on threads, through their global pools.
- #7, #11, #12 and #15 depend on the machine, through caps derived from RAM and core count.
- #1 is both. It also wipes its whole cache when the global pool refuses one store.

## Where units are charged (the choke points)

Every unit enters through one of these:

- **`ConsumeAt` / `ConsumeAtN`** (`TurnSolver.cpp:1885-1897`). This covers the rollout step (`:81328`), the
  FullSearchLine / SolveWithLookahead plan-application loops (`:84660-94877`) and `ApplySecondMainInSearch` (`:81197`).
- **`TurnSolver::ChargeDecisionUnits`** (`:3944`). This is the equip-route DFS, `MTG_EQUIP_ROUTE_NODE_UNITS`. The
  default is **2 units per link** (`DecisionProviders.cpp:39799`), charged at `:40474` to the decision budget, which
  only exists at a finite decision root. Two comments still say 8: `TurnSolver.cpp:1408` and the cyclone-mem ledger.
- **`ChargeAaRouteUnits`** (`:3949`). Default 0, so off.
- **The greedy subset walk in `SolveUncached`** (`:48433-48478`). It charges only when `g_greedy_charge_budget` is
  installed. In practice that is **Snow** only (`SolveChargeWeightOptIn() = 16`, `DecisionProviders.h:2530`).
- **`ChargePutBackVariant`** (`:26029`) and **`MTG_ENUM_CHARGE`** (`:64705`). Both default off.
- **Direct `decisionwork::Add`, bypassing Consume**: the greedy walk (`:48494`, `:48500`), unbudgeted rollouts
  (`:81343`) and `MTG_BP_DERIVE_METER` (`:65866`, off). These matter whenever the per-decision meter is armed, which
  generation (`scripts/mullgen.sh`) does.

So a cache drifts if its miss path reaches one of these and its hit does not replay the charge.

## Details: the plan and route caches (#1-#4, #8-#10)

**#1 breakpoint-continuation plan cache: confirmed.** `BpEnumEntryFor` (`TurnSolver.cpp:97407`) serves a continuation
list from `bpcache::t_plans`. Relevant properties:

- It is keyed on `BuildBreakpointKey` and is **not** scoped to a decision. `MTG_BP_ENUM_EPOCH` is off by default
  (`:96317-96320`), so a hit can come from an earlier decision of the same game. The cache is cleared per game
  (`:79851-79858`).
- A miss runs `BpDeriveContinuationList`, which runs `EnumeratePlansWithLand`, then `CollectActions`, then the
  equip-route trial `TrialPolicy`. In a new decision that trial's verdict memo (#8) is empty, so the DFS runs and charges
  2 units per link to the decision budget.
- A hit returns `&it->second` (`:97627`) and charges nothing.

The source states the consequence twice:

- `ClearPerGameCaches` (`:79854-79857`): *"A hit saves the units a derivation costs … so its history moved a game's work
  units AND, through the start gate, play: KittyEquipment v2 d5 s2002 gi=34 is 48,188 units alone and 45,409 after games
  0-33 on the same thread."*
- `plancache` (`:79173-79180`): *"a refused store is recomputed, and a recompute can cost units … which the
  iterative-deepening start gate reads."*

**Capacity: the worst in the tree.** The store path is `if (cache.size() >= s_bp_enum_cap || !plancache::Fits(psz)) {
cache.clear(); … }` (`:97762-97766`). The **global** plan pool is `DefaultPlanCacheKb` = workers × 100 MB, capped at
cache_mb/4 (`MemBudget.cpp:127-138`). So when other threads have filled the pool, this thread wipes its **entire**
continuation cache. Every later lookup then re-derives, and on Kitty pays route units again. Units, and through them
play, depend on what the other workers hold. `DeterminismReport` (`:82545-82568`) prints a warning only after the fact.

**#2 canon front.** Same key, same derivation, same per-game scope and the same skipped route charge. It is count-capped
at 16384 with clear-all and sits outside the pool. On cyclone-mem, even after the fix, Kitty gi14 d5 read 10,340 units
with this memo against 10,348 with the bp cache fully off. The ledger calls that 8-unit residual "noted, not chased", so
it is still open.

**#3 / #4 enum memos.** These are per decision (`g_decision_epoch`), with promotion on the second visit into the global
pool. Their miss reaches the equip-route trial too, but always inside the same decision as #8. A recompute would
therefore be a #8 hit and charge 0, so the hit is neutral relative to the memo's own absence. They drift only under
`MTG_ENUM_CHARGE`. Their *content* was not exact on HEAD: physical-copy and m1-hand-mask holes are fixed on cyclone-mem
by `MTG_PLAN_CACHE_EXACT_KEY`. Pool binding then changes what is served (play), not units.

**#8 / #9 equip-route memos.** Both are scoped to one decision epoch (`TurnSolver::DecisionEpoch()`), with clear at
2^20 entries, so they are deterministic per game.

- **#8, the TrialPolicy verdict memo.** It returns a stored verdict at 0 units, where the miss runs the
  MostCasts → PayloadFirst → ChainFirst → Search ladder. The Search rung charges 2 units × links. The memo also stores a
  budget-cut or node-capped "no" (`memo[key] = r` regardless of `b.cut`), and the key does not record the regime it was
  computed in.
  - **Open question (completeness, not units):** can a "no" written by a 128-node rollout trial be served to an
    in-window trial at an unlimited root? It would need the writer's regime added to the `MEMO-WRITE` trace.
- **#9, DSeen.** It skips a whole winless subtree for free. Only the discovering link was charged, at `:40474`.

These two are also why prior-art mode 2 *overcharges* (below). They are the innermost charging memos, and they do not
replay.

**#10 AA route memo.** It is per game, so a hit crosses decisions. It is neutral only because the price defaults to 0.
Cut verdicts are never stored (`:47593-47600`).

## Details: the Snow lane (#5)

The greedy `Solve` memo (`TurnSolver.cpp:47334-47422`) is per epoch, capped at 16384 with clear-all. Its miss runs
`SolveUncached`, whose subset walk charges `g_greedy_charge_budget` once per W visits. Only Snow opts in (W=16). Snow's
heaviest decision is ~20 M visits, so ~1.25 M units at W=16 (`TurnSolver.cpp:3732`). The memo is reached inside
rollouts (`g_cs_solver_nest > 0`), so every hit on Snow skips greedy-walk units. On other decks the miss also reaches the
equip-route trial, but within the same epoch as #8, so it charges 0 there.

**A trap for any replay here.** The walk bills once per W visits through a residual accumulator (`g_greedy_charge_acc`,
or `decisionwork::t_charge_acc`, `:3754` and `DecisionWorkMeter.h:31`) that *carries in* from the caller. So the units
one `SolveUncached` call charges depend on the incoming residual, by up to 1 unit. An exact replay must record **visits**
and re-feed them through the accumulator, not record units.

## Details: the transposition class (#6, #7, #11-#15)

For these memos, skipping the work *is* the purpose, and most were adopted with the drift stated and A/B'd:

- **#11:** *"On a cache hit the rollout is skipped entirely (and no budget is consumed — avoided recompute is the
  point)"* (`TurnSolver.cpp:81707`).
- **#6:** *"Deliberately NOT byte-identical to the unmemoized searched mode: a hit skips the nested search's budget
  consumption, so the outer deepening fits more passes"* (`:95693`).
- **#7:** "a play change the tiers measure" (`:95812`).
- **#13:** "a play change, tested as one" (`:80993`).
- **#12:** `NoWinLeafMemoArmed`, "a budget only re-spends the rollouts it saves" (`:81098`).

What making them hit == miss would mean:

- **The budget would price the search TREE instead of the DAG.** At the same budget the search would be far shallower.
  That is a large play change, and probably a quality loss, until budgets are re-calibrated. It is inert at an unlimited
  budget, because nothing is charged there, so it never makes a line unfindable.
- **#11 / #12 (TT) replay is cutoff-dependent.** A cached win is served to any cutoff, but a recompute under a cutoff
  below the win aborts early and charges fewer steps. An exact replay would store the cumulative units at each simulated
  turn and charge the prefix up to the query's cutoff. A no-win entry is served only when `cutoff <= bound`, and its
  recompute charges up to the query's cutoff, not up to the bound, so the same per-turn prefix applies.
- **#14 (FSL) has no recorded "recompute cost".** The cyclone-mem ledger says why: *"an entry is reused across
  iterative-deepening passes, as a truncated line or a no-win bound, so what a recompute at the hit site would return or
  charge is not a recorded quantity."* Hit == miss here needs a design, not a field. Note that the order-free wave
  already stores a twin's unit cost (`order_sig` reused as `twin_units`, `:85985`), but only as a gate threshold, never
  as a charge.

**Capacity is the part that is a defect regardless of that ruling:**

- **#14** uses the global `FslPool` (`:82504-82540`) and the RAM-derived `DefaultFslCapEntries`. Per the comment at
  `:82515-82520`, EDF seed 900411 gi161 read 7.7 M / 14.3 M / 30.1 M units at the default / 300 MB / 50 MB pool.
- **#11 / #12** use `DefaultTtCapEntries` = (cache_mb / AffinityCpuCount) / 3 / 64 (`MemBudget.cpp:108-114`). It stops
  storing past the cap, so the same game charges more units on a smaller-RAM or higher-core box. `TranspositionTable.h:94`
  calls the cap "result-NEUTRAL"; that is true of an unbudgeted *answer* only.
- **#7** uses `m1memo::ByteCap` = min(96, max(16, BudgetMb/Workers/4)) MB (`:95849-95857`), with clear-all.
- **#15**, the bottoming shared TT, is used for one `BottomCards` pass. That is up to ~35 trial games, so it is the most
  likely place for the TT cap to bind. Its stated rationale (cross-trial reuse because "the key folds library
  size+front but not the tail", `AIEngine.h:346-359`) is stale: `BuildSimKeyBase` now folds the ordered library, so only
  cross-decision reuse within one trial remains.

These are machine-dependent today. The Linux and Windows CI parity check and the 48 GB / 24-core GT box only agree
because neither cap binds in their games. That is an accident of sizing, not a guarantee.

## Details: ORDER, amplifier, UNCLEAR

**#17, #18 and #19.**

- **#17 / #18 are ordering hints.** A hit rotates or reorders candidates; it does not skip a computation. They move
  units by design, as killer moves do, and do not fall under "a hit skips the charge". Their *contents* come only from
  nodes that completed, and FSL hits (#14) do not record, so they inherit #14's pool dependence. They become
  deterministic when #14 does.
- **#19 is the hybrid's learned cost model.** It learns R and G from `budget->Used()` deltas and feeds the start gate
  and the reserve for the rest of the game, so any drift above is carried forward into later budget allocation.

**#20, the prefix-resume snapshot.** The unit for the variant is charged *before* the snapshot test (`:85482`,
`:88073`, `:94571`), and the skipped prefix applies contain no `ConsumeAt`. So it is neutral, unless a prefix re-apply
can reach a charging sub-search: a breakpoint derivation that misses #1 and runs an equip-route trial that misses #8,
charged to `DecisionBudget()`. A snapshot hit does not replay the prefix's `t_binds` / `g_fs_trunc_events` /
`g_condemn_drops` deltas.

**Settles it:** with `MTG_ROLLOUT_STATS=1` on a Kitty game, does `u_equip_route` differ between prefix-resume on and
off at identical digests? And does any no-win store decision differ?

**#21, the combat-pin share.** It caches a base apply's state only if `budget->Used() == cp_units0` and the breakpoint
channels are unchanged. Unlike EtbcFold (`:27968`), it does not check the decision budget, `gamework`,
`decisionwork` or `t_binds`.

**Settles it:** inside that loop, can a pre-combat apply charge a budget other than `budget`, e.g. an equip-route DFS
billing `DecisionBudget()` while `budget` is a sub-budget? If so, the share skips that charge.

**One more scope question, on #8 / #9.** These rely on the epoch alone, are not cleared per game, and `g_decision_epoch`
is not bumped at game start. **Settles it:** can any `EquipRouteTrial` run in a game before that game's first epoch bump
(an executor-side or d0 call before the first search)? If so, it can hit the previous game's last-epoch entries on the
same thread.

## Neutral (no charging site beneath them)

**Core** (no `SearchBudget` anywhere below, and the provider callbacks reached are pure heuristics):

- `s_memo_tl` (backtracker failure memo, `SpellEffects.cpp:3783`)
- `LandAuraMemo` (`SpellEffects.h:23035`)
- stealspec intern `t_memo` (`SpellEffects.h:25775`)
- `cantriporder::RankCache` (`SpellEffects.h:35824`; exact key, verify mode, never cleared, wall only)
- `landsearchmemo::t_slots` (`LandSearch.h:359`)
- `NameRegistry` memo (`NameRegistry.cpp:29`)
- `CardDatabase::LookupCached`
- `swing_memo` (`SpellEffects.h:40912`)
- AIEngine `pref_memo` (`AIEngine.cpp:2144-2166`)
- the bottom-trial name dedupe (`AIEngine.cpp:2190-2200`, an identity fold)

**Provider:**

- EDF `comboroute::TrialMemo` / `AttemptCache` / `KdMemo` (`DecisionProviders.cpp:38656-38893`; `comboroute::Run`
  calls no charging entry point)
- `aaroute::Walk::failed_exact` (one walk)
- `t_cut_verdict`, `t_blind_seen`, `t_origin_board`, `s_seen`
- the CardDefinition* memos (`:41144`, `:42434`, `:42929`)
- function-local `score_memo` / `raw_memo` / `m3_memo` / `refund_cache`

**TurnSolver:**

- `paymemo` (`:8835`)
- `rh_memo` / `rhmemo` (`:64657`, `:69151`)
- `CollectMemo` (`:31604`)
- `PrePlanShare` (`:54740`)
- the in-loop sound folds: BounceFold, PutBackFold, DigFold, PutBounceFold, `w0collapse::Memo`, `pofold` and EtbcFold.
  Each is per loop, and its declines sit *before* the unit charge. These are collapses, not caches whose hit pattern
  varies.
- `routebptape` (`:3854`; neutral while the derivation charges nothing, i.e. at defaults)
- `bpallpaths::t_acc` (off)

**Diagnostic-only, behind default-off flags:**

- `t_nowin_stored_states`, `ttprobe`, `exhprobe`, castset `SeenMap`, `s_libcheck_served`, `s_full_seen` / `s_raw_seen`,
  paycensus `seen`, `cantripattr`, `g_oo_vals`
- verify recomputes (`MTG_*_VERIFY`). Some of these add to `decisionwork` when it is armed, which is acceptable for an
  instrument.

**BpLenMemo** (`:71927`, `MTG_BP_NSKIP_GLOBAL`, off): drifts if turned on.

## Prior art: `cachecharge`, MTG_CACHE_HIT_CHARGE (local branch `cyclone-mem`, NOT on this branch)

Commit `ab4fbc6a2` (*"determinism: plan caches move wall only -- exact keys, isolated derivation channels,
hit-equals-miss pricing, per-thread pools"*, 2026-10-29) is on the local branch `cyclone-mem` only (worktree
`.claude/worktrees/cyclone-mem`). It is not on origin and not in `phase-1-2-deck-analyzer`.

**The pattern.**

- **Attribution.** A thread-local `cachecharge::Rec` stack attributes every unit charged in `ConsumeAt` / `ConsumeAtN` to
  every enclosing cacheable computation (`CacheChargeNote`). It splits them by budget: the decision budget, the
  greedy-walk budget, and "other". A charge attempted with no budget (`ChargeDecisionUnits(nullptr)`) is recorded
  separately.
- **Storage.** Each entry stores a `Cost`: units per budget, `storable`, and the side counters (group-cap drop,
  condemnation drops, truncation events, budget binds). A computation cut by an exhausted budget is not storable.
- **Serving.** `CacheHitCharge(cost)` serves a hit only if a recompute here would run the same way: it charged no
  "other" budget; a budget absent then is absent now unless no charge was attempted; and `Used + recorded < Limit` on
  each budget, so the recompute would not be cut. Otherwise the lookup is treated as a miss.
- **Mode 1 (shipped default there).** Do not store any computation that charged a unit or reached a decision-memoising
  sub-search (`TurnSolver::NoteChargingSubsearch`, called from the equip DFS, `TrialPolicy` and the AA walk). It is exact
  because it *is* the recompute.
- **Mode 2.** Store and replay the recorded units. Measured to **overcharge**: Kitty seed 1015 gi14 read 20,443 units
  against 4,424, and lost a turn. The cause is that the equip route's own memos (#8, #9) make a recompute warm where the
  first run was cold.
- **Pools.** Per-thread plan-pool quota (`MTG_PLAN_CACHE_PER_THREAD`, 512 MB per thread) and per-thread FSL pool
  (`MTG_FSL_PER_THREAD`).
- **Proved invariant:** 168 games at 1 / 6 / 12 threads, tiny / shipped / unbounded pool, identical digests, win turns
  and units.
- **Smoke:** 149 / 149, play-changed 0.
- **Held-out A/B:** 1,300 games, identical quality.
- **Cost:** Kitty wall **+13% (d5) / +27% (d3)** thread-time, because its derivations are recomputed on every lookup.

**Q54 (open).** The ledger recorded a decision-scoped variant: keep such entries only for the rest of the decision, while
the route memo makes a recompute free. Its v2 patch (`logs/cyclone/mem/route_decision_scoped.v2.NOT_EXACT.patch`)
recovered the wall (-7.3% overall) but was not exact: one game in 600 (d5 gi105) charged +2 units for an extra cold
route trial. It was not committed.

**Can it be applied to each DRIFTS cache?**

- **#1, #2, #3, #4:** yes. They are already wired on cyclone-mem.
- **#5 (Snow):** yes, through the existing greedy lane, plus the W-residual fix (record visits).
- **#8, #9:** yes, and this is the key step. Store the link units charged under a verdict (#8) or a board's subtree
  (#9), and replay them on a hit (same `Affordable` test). Once the innermost charging memos replay, a recompute costs
  the same warm or cold. **Mode 2 then becomes exact for #1-#4 as well**, so the bp derivations can be cached again and
  the Kitty wall comes back. This removes both the overcharge and the Q54 trade.
- **#10:** yes, if the price is ever turned on.
- **#6, #7, #13:** mechanically yes: these are Solve / SolveWithLookahead calls whose units all pass through
  `ConsumeAt`. Mode 2 is exact once everything nested beneath them also replays: the TT, the route memos and their own
  nested memos. That is a large play change.
- **#11, #12:** only with per-turn prefix units, because of the cutoff.
- **#14:** needs a new design.

**Gaps the shared helper must close before it can be called complete:**

- **The direct `decisionwork::Add` sites** (`:48494`, `:48500`, `:65866`, `:81343`) bypass `ConsumeAt`, so the `Rec`
  never sees them. In generation, where the meter is armed, those units are still skipped on a hit. Route them through
  one charge function that notes the `Rec`.
- **gamework** is covered: it is fed by `Consume`, which replays through `ConsumeAtN`.

## Recommendation: order and scope

**One shared mechanism, not per-cache fixes.** The prior-art `cachecharge` is the right shape. It sits at the single
choke point every unit passes through, so attribution is complete by construction and nesting composes:

- an outer entry's cost includes whatever its inner computations, or their replayed hits, charged;
- a cache opts in with three lines (a `Scope` around the miss, a `Cost` in the entry, `CacheHitCharge` at the hit);
- the `Affordable` test keeps replay from crossing a budget cut, so it never manufactures an answer a recompute could
  not give.

Per-cache unit counters would reinvent the attribution each time and get the nesting wrong. That is exactly what mode 2
ran into, one level down.

The composition rule that makes it exact: **"hit charges the recorded units" equals "hit charges what a recompute
costs" if and only if every memo nested inside the recorded computation also replays.** So the mechanism has to be
adopted inside-out, innermost charging memo first.

**Order:**

1. **Land `ab4fbc6a2` from `cyclone-mem` onto this branch.** It is play-identical (smoke 149 / 149, A/B 1,300 / 1,300)
   and fixes #1-#4, including the known instance, plus the two global pools. It is currently local-only, so it has one
   copy. Pushing the branch is the first protective step. It must go through the normal rebase / smoke / GT flow.
2. **Make the equip-route memos #8 and #9 replay their link units.** This is a play change on equipment decks
   (KittyEquipment, Kitty v2, any `EquipmentProvider` deck): route trials become as expensive in units on a hit as on a
   cold miss, so budgets bind sooner. It needs a held-out A/B on those decks. It also resolves Q54 by enabling mode 2
   for #1-#4 exactly, recovering the +13-27% Kitty wall.
3. **Fold the direct `decisionwork::Add` sites into the helper.** Generation-only effect.
4. **Snow (#5).** Record greedy visits and replay them through the accumulator. This is a play change on Snow only, so
   it needs a held-out Snow A/B. Snow is also where the walk dominates cost (Snow's walk is ~20 M visits per heavy
   decision), so expect a real unit increase.
5. **Make every unit-relevant cap a constant**, independent of RAM, cores and other threads. This is mandatory whatever
   the USER rules on the transposition class.
   - Per-thread FSL pool (cyclone-mem `MTG_FSL_PER_THREAD`). Its quota there is still `MTG_FSL_POOL / machine
     workers`, i.e. still machine-derived, so make it a constant.
   - Constant TT cap, constant `m1memo` byte cap.
   - Per-decision tables in bottoming trials (#15), or accept #15 explicitly.

   Each is a play change only where the old cap bound, and on the GT box it mostly does not. Note that a
   unit-neutral cache makes capacity a wall-only question by construction. That is the deeper reason to do steps 1-4:
   after them, no pool size can move play.
6. **The transposition class (#6, #7, #11-#14): a USER ruling, surfaced, not decided.**
   - The rule as stated covers them.
   - Applying it means the budget pays tree cost for DAG work. It is a large fleet-wide play change and probably needs
     budget re-calibration (a `NODES_PER_VIRTUAL_MS`-scale question).
   - These memos were each adopted with their drift A/B'd. Whether the rule extends to them, or they stay a declared
     exception, is the USER's call; this audit takes no position.
   - Whichever way it goes, step 5 makes them deterministic functions of the game.
7. **Settle the UNCLEAR items** with the single-game probes named above: #20 and #21, and the #8 cross-regime "no" and
   cross-game epoch questions.

**Decks likely to move most:**

- Steps 1-2: KittyEquipment / Kitty v2 (equip route inside breakpoint derivations).
- Step 4: Snow.
- Step 5: the heaviest-memory decks where caps bind: EDF (the FSL pool example), AccomplishedAlchemist (giant
  continuation lists), Melira and Fungus candidate-b (large lists, long decisions), Galadriel (`m1memo` byte cap).
- Step 6, if ruled in: every deck, most on the breakpoint- and cantrip-heavy ones (Cyclone, Hinata, Fungus, Melira,
  Dragonstorm, Snow).

**Stale comments found** (fix with the code, not separately):

- `TurnSolver.cpp:1408`: equip route "default 8"; the default is 2.
- `TurnSolver.cpp:1406`: `kGreedyWalk` is labelled "EnumeratePlans' subset walk"; the charge is in `SolveUncached`.
- `TranspositionTable.h:94-100` and `MemBudget.h:29`: caps "result-NEUTRAL / decisions are unchanged"; true unbudgeted
  only.
- `plancache` `:79153`: "Result-neutral either way … play is identical at ANY budget". The same block corrects
  itself at `:79173`.
- `AIEngine.h:346-359`: the shared-TT "size+front" rationale.

## Plan (USER rulings, 2026-10-30)

**The governing rule.** USER: *"They should run the same statements if given the same budget and depth etc. How much
cache space or how many other threads are running should only be able to impact performance at most."* Play must be a
function of (deck, seed, depth, budget, settings) only. Cache capacity, thread count and machine may change wall time,
never a decision. Units drive the budget, so a capacity-bounded cache whose hit saves units breaks the rule.

**New caches:** a hit charges exactly what a miss would, always. No exceptions.

**Existing caches: build both designs and measure.** USER: *"we should probably build them both and measure. I can't see
a better way to be sure of the approach. That said, it's a tricky ask, since we would need to rebalance the budgeting
for both."*

1. **Always pay.** Every hit replays its miss's charge, so units price the full search tree.
2. **Floor + overflow (the USER's design).** Each thread gets a FIXED-size floor tier, identical on every machine, with
   deterministic eviction and per-game or per-decision scope. Hits in the floor are discounted, as today. Whatever the
   floor evicts goes to an overflow tier sized by available memory, and a hit there charges exactly what the miss would
   (a pure speed cache). Quality then depends only on the floor size, and extra memory only buys speed.

**Shared prerequisite.** Each entry carries its miss's cost and replays it on a hit. This is needed for every hit in
design 1 and for overflow hits in design 2. Build it on the `cachecharge` mechanism (`cyclone-mem` `ab4fbc6a2`).

- Replay must go innermost first: every memo nested inside a recorded computation must also replay. Otherwise a
  recompute is cheaper than the recorded cost and replay overcharges (Kitty gi14: 20,443 units against 4,424).
- The rollout TT (#11, #12) needs per-turn unit prefixes, because a recompute under an earlier cutoff charges less.
- The line cache (#14) needs a recorded recompute cost, which is a design question rather than a field.

**The measurement: compare at MATCHED COST, not matched budget.** Both designs charge more units than today for the same
search, so at the same budget each one searches shallower, and a same-budget A/B would mostly measure that.
Rebalancing by hand is the tricky part, so measure it instead: run each arm (today with constant caps, always pay, floor
at one or two floor sizes) at three or four budget multipliers on held-out seeds. Plot avg win turn against mean core-ms
per game. The arm with the better quality-vs-cost frontier wins, and the multiplier at which it matches today's cost IS
its rebalance. Start with one global units-per-ms factor and check the per-deck spread before considering per-deck
factors. The cost axis is wall time, so it needs a quiet box, the same thread count and the same build mode on every arm
(`MTG_PGO=0` both, or PGO both), with the arms interleaved.

**Required whatever the outcome (Rule A):** remove every RAM-, core- or worker-derived cap and every cross-thread pool
from the unit-relevant caches (#1, #7, #11, #12, #14, #15). Add a standing invariance check to the regression tier: the
same games at 1 vs N threads and tiny vs huge physical caches must give identical decisions and units.

**Timing.** Every step changes the engine, so it lands only after the Hinata2 mulligan stage finishes. The exception is
anything proven play-identical on Hinata2's keepgen play digest.

## Scope: one project with budget reliability (USER, 2026-10-30)

USER: *"Maybe it makes sense for this to be put together with the work to rebalance usage. We do need to have the
budget setting be somewhat reliable."* So this plan merges with the budget-rebalancing work: making a budget setting
mean roughly the same work, and roughly the same wall, on every deck and every position. There are three ways today
that units fail to track work. Fix them together, then calibrate once:

1. **Cache hits that skip charges** (this audit). The fix is the plan above.
2. **Work that is never billed.** Hinata2 keepgen (2026-10-30): one turn-1 decision took 107.7 s for 6,779 units, about
   16 ms per unit against the usual ~1 us. 60-75% of the slow rollouts' wall went to breakpoint-continuation
   derivations that charge nothing, and EDF had the same shape (`docs/design/edf-slow-rollout-tail-2026-10-17.md`).
   Two levers for this are built and default OFF, each a USER decision: `MTG_BP_DERIVE_METER` and `MTG_ENUM_CHARGE`.
   Diagnosis: `docs/design/keepgen-hours-long-rollouts.md` (branch `keepgen-slow-tail`).
3. **Units that are not shape-neutral.** Per-unit cost differs between work kinds (e.g. the model leaf costs 1.24x
   the instructions per unit of live play, by callgrind), so one units-per-ms factor misprices some decks.

Calibrate only after 1-3 are fixed: the matched-cost frontier above, measured per deck, gives the units-per-ms factor
and shows whether one global factor is enough. USER 2026-10-30, on the hours-long rollouts: *"Games that are in the
hours range should be practically nonexistent when we run with a budget."* That is the acceptance test for item 2.

## Hand-off note (2026-10-30, evening)

USER: *"there is an actual issue with the cache and threading. I thought we had addressed some of those issues already,
but from what you are saying there are more remaining. That will have to wait for another agent to work on it
tomorrow."*

- **What was addressed, and where.** The shared mechanism that makes the plan pool per thread is commit `ab4fbc6a2`.
  It is now on origin as branch **`cyclone-mem`** (pushed 2026-10-30 as a backup), but it has NOT landed on
  `phase-1-2-deck-analyzer`, so no shipped binary and no running generation has it.
- **Live evidence of the thread coupling (Hinata2 keepgen, generation src `7b22b6db3`).** One rollout charged 420,198
  units, then 420,194 and 420,196 in other 4-thread runs, and 420,204 when re-run alone on 1 thread. That is four values
  from the same inputs, with the same win turn. The prime suspect is #1: `plancache::Fits` reads a pool shared across
  worker threads.
- **Start here:** Rule A and step 1 of the recommendation, i.e. land `ab4fbc6a2` (play-identical on smoke and on the
  1,300-game A/B). Then add the standing 1-vs-N-thread / tiny-vs-huge-cache invariance check, so this class of defect
  fails a tier instead of being found by accident.
