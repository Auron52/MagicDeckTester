# Vampires search cost, round 2 (2026-10-21)

Three agents on one brief (`build/scratch/vampires/bench/BRIEF_PERF.md`): P (per-node CPU), B (branch cuts),
R (the pass / overrun structure). Base `1a1cf9b9e`. One section per agent.

## Agent P -- per-node CPU (byte-identical speedups)

Branch `vampires-perf-p` (backup `backup/2026-10-21/vampires-perf-p`), scratch
`build/scratch/vampires/perf-p/`. Every change here is BYTE-IDENTICAL by construction: the same selections
reach the same predicates in the same order, the same keys are built, the same plans come out -- only fewer
instructions are spent getting there. Units are therefore unchanged on every row, and the digests are the
proof. Each change has an `EnvOn(..., true)` hatch (`=0` restores the old path); none changes play, so
none has a heurarm slot.

### P.1 Where the CPU goes (callgrind Ir, Profile build, single thread, base `1a1cf9b9e`)

Three reference games, one per row shape (`scripts/cg.sh`; seeds are `base + gi`):

| game | units | Ir | Ir / unit |
|---|---|---|---|
| std d3 b10 s3003 gi48 (seed 3051) | 63,081 | 10.54 G | 167 k |
| 2HG d5 b20 s2002 gi10 (seed 2012) | 234,051 | 25.35 G | 108 k |
| std d5 b20 s3003 gi23 (seed 3026) | 247,604 | 55.77 G | 225 k |

The profile is flat in the generic engine (the simulation itself -- ApplyPlanDirect, combat, mana payment --
is ~30% and spread over hundreds of functions), but four Vampires-shaped costs stand out:

1. **The subset walks' per-position bookkeeping** (EnumeratePlans' and the greedy Solve's odometer):
   self cost 7.3% / 2.3% (2HG), 12.0% / 4.8% (d3), 1.2% / 13.4% (d5). Falkenrath Pit Fighter's ~100-member
   activation group multiplies every other digit; the walk summed the selected options' mana values per
   position (an O(groups) loop), scanned the digits again for the radix cut on every refused position, and
   rebuilt `sel` from scratch per independent bitmask.
2. **SubsetVampOutletInvalid**, the Vampires subset rules: **29.1% of the d5 game** (10.9M calls, 10.4M of
   them from the greedy rollout walk), 10.3% of d3, 2.3% of 2HG. Almost none of it was the rules'
   arithmetic: rule 8 copied every selected discard outlet into a `vector<Action>` and stable-sorted the
   copies (12.5% of d5 by itself), rule 3 resolved the provider and both actions' ranks per PAIR through
   TrailingRunsBefore and copied the subtype string per call (7.0%), rule 1 built a vector of heap strings
   per selection, rules 7 / 10 re-walked the battlefield and re-looked-up madness pins by name.
3. **SubsetHasDuplicateSacSource**: 5.3% of d5, 2.6% of d3 -- nine clause-opening tests per selected
   action on scattered Action fields, for a deck where only one clause (the {T}-ability one) can fire.
4. **CollectActions' Vampires emission**: 25.6% of the 2HG game inclusive. Per discard-cost source it re-ran
   the provider's cleanup ranking (ActivationDiscardClasses, 2.5%), per pin it copied the whole GameState
   for the madness fan's probe and re-derived the turn's optimistic mana (MadnessFanFor 3.3%), interned
   the pin name once per (victim class x pin) variant (1.4M interns, 0.7%), looked definitions up by string
   (1M hashes, 0.7%), copied every madness variant twice (PushWithMadnessFan 2.6%), and read one env flag
   per call (0.2%). Captivating Vampire's steal fan recursed through a `std::function` (3.5% of d3).

Plus three generic ones the Vampires games exposed: BpEnumBuildKey re-hashed the 175-slot heurarm array
through its TLS wrapper on every key (29M `__tls_init` calls, 1.8% of 2HG); ApplyPlanDirect re-derived two
pre-plan activation snapshots for every sibling candidate of one root (3.5% of 2HG); MoveOrderPlans
shuffled whole Plans through a merge sort (~1.5% of 2HG in Plan moves).

### P.2 The changes (all byte-identical; flag `=0` restores the old path)

All in `src/ai/TurnSolver.cpp` unless named; the soundness argument is in the code comment at each site.

| # | change | flag | why it is the same computation |
|---|---|---|---|
| 1 | **The plain subset walk as a pruned depth-first walk** (`PlainOdometerDfs`, both walkers' `!(any_ritual \|\| any_rock)` branch) | `MTG_WALK_DFS` | The carry walk visits positions in lexicographic order, last digit most significant; a DFS that fixes digit G-1 first visits them in the same order. The high part's sum is kept per depth (O(1) per step) and a subtree whose high part already exceeds the bound is skipped -- every option costs >= 0, so every position in it is refused (exactly what the radix cut jumps over or refuses one at a time). Surviving positions run the carry walk's independents loop verbatim (same `sel` layout). Only the plain scalar walk; any per-position predicate (equip copy / piece dependency / fold prefix / metalcraft bound / verify or diagnostic mode) keeps the carry walk. The greedy walk stops once its budget is spent (every later consider() returned at `walk_exhausted`). Unit test: 400 random instances, identical sequences. |
| 2 | **VampRulePre**: SubsetVampOutletInvalid's frozen-board facts cached per enumeration (one `VampCandFacts` per candidate: def, trailing rank / band, outlet draws / cost, pin hand counts, per-subtype arrival deltas, rule-4 fold keys; rule 7's board bodies per colour), the rules evaluated on them | `MTG_VAMP_RULE_PRE`, `MTG_VAMP_RULE_PRE_LAZY=K` (8), `MTG_VAMP_RULE_PRE_VERIFY` | Every rule is a pure predicate of (state, cands, sel) and the filter returns their OR, so evaluation order and where the inputs come from cannot change the answer; each cached field names the exact expression it caches (rule 8's stable sort of Action copies becomes a stable insertion sort of indices on the same keys). Built lazily on the K+1-th call of an enumeration (a 2HG rollout's greedy Solve averages ~3 calls, where the build would cost more than it saves). VERIFY runs both paths on every selection; unit tests compare 24k random selections over real CollectActions lists. |
| 3 | **SubsetHasDuplicateSacSource entry skip**: per-candidate bits for each clause's opening test, built lazily | `MTG_DUP_SRC_SKIP` | A candidate that opens no clause is skipped; clauses, their order and their `dupclause::Hit` attribution are unchanged. Built during the walk, it reads the tags after RenumberFoldOrds (which only clears tags). |
| 4 | **BpEnumBuildKey's arm hash cached per thread** on the arm's bytes | `MTG_BP_ARM_HASH_CACHE` | The same fold of the same bytes; the loop had read the TLS-wrapped `heurarm::t_arm` 175 times per key. |
| 5 | **CollectMemo** (per-CollectActions-call): ActivationDiscardClasses per source definition, OptimisticTurnMana, the madness fan per (pin, outlet cost, source, sacrifice, scope), MadnessFanFor's probe board copied once per call (erase / re-insert per pin) | `MTG_COLLECT_MEMO` | Pure functions of the call's const state and their arguments; a helper uses the memo only when handed that call's state object; `g_axis_madness_vars` is bumped exactly as the uncached path bumps it. |
| 6 | **By-name lookups on the interned index** (`CollectedActionDef`) | `MTG_LOOKUP_INTERNED` | `LookupInterned` is the pointer-keyed index over the same `m_cards`, rebuilt at every load / Register; interning is canonical, so it returns exactly `Lookup(name)`. |
| 7 | Falkenrath Pit Fighter's pins / victims interned once per source, not per variant | -- | The same canonical pointers. |
| 8 | **EmitStealVariants**: the count-distribution recursion as a direct lambda (was `std::function`) that skips a subtree whose remaining classes cannot absorb `left` bodies | `MTG_STEAL_REC_PRUNE` | Every pruned leaf fails `left == 0`; leaves and their order are unchanged. |
| 9 | `stealspec::Intern` memoised per thread in front of the shared mutex (`src/core/SpellEffects.h`) | -- | A spec's id never changes once assigned; keys fold the spec CONTENT. |
| 10 | `PushWithMadnessFan` builds the LAST variant in place and moves it | -- | The same actions in the same order, one copy fewer per fan. |
| 11 | **The pre-plan facts share**: ApplyPlanDirect's two pre-plan activation snapshots computed once per sibling loop (lookahead pre/post-combat, FSLineTail main 2, FSLineWin) | `MTG_PREPLAN_SHARE`, `MTG_PREPLAN_SHARE_VERIFY` | The siblings are fresh copies of one root board; a loop arms the share only for the copy it is about to apply, the apply takes it at entry, and each fact is computed by the first sibling at the point it always was. VERIFY recomputes and aborts on a difference. |
| 12 | **MoveOrderPlans sorts (keys, index)**, then moves each Plan once (not at all when already in order) | `MTG_MOVE_ORDER_KEYS` | The comparator reads five scalars, so the stable sort of keys is the stable sort's permutation. |
| 13 | small: `enumstats::Enabled()` read once per walk; `MTG_EQUIP_ALL_HOSTS` cached; CreatureConvokeArms' vectors reserved; Action-name lookups (`Lookup(x.card_name)`, 149 sites) on the interned index; VampiresProvider's discard tiers computed once per name before its stable sort (`src/ai/DecisionProviders.cpp`) | -- | Process-constant reads; capacity only; `LookupInterned` == `Lookup` (row 6); the same stable permutation. |
| 14 | **The walkers' Vampires prefilter**: once an enumeration's VampRulePre is built, both DFS walks ask SubsetVampOutletInvalid themselves and do not hand a refused selection on | `MTG_VAMP_PREFILTER` | The receivers' chains are pure rejection predicates (an order-free OR) that this rule belongs to; what consider() does before reaching a refusal is replayed (its abandon-poll cadence; the greedy walker sorts first exactly as consider() does) or fenced off (walk budget charge, per-decision meter, every census / trace / funnel instrument, the payoff prune, human play, the VERIFY mode). |

### P.3 Evidence

**Byte identity.** Every round re-ran, on the round's own binary: `mtg-test` (1,660 cases, including the new
`test/unit/test_vampires_perf_p.cpp`: 400 random walks DFS == carry walk; 24k random selections over real
CollectActions lists, fast rules == original rules); the `quick.json` manifest (110 games) with
`MTG_VAMP_RULE_PRE_VERIFY=1 MTG_PREPLAN_SHARE_VERIFY=1 MTG_COLLECT_MEMO_VERIFY=1` (every selection / sibling
apply / memo hit checked against a recompute -- no abort); the seven `regression_rows.json` rows (digests,
units AND the per-game `.wins` files identical to the brief's baseline); and `test/regression.sh --smoke`
against the committed GT with the binary pinned (`MTG_BIN`): **128 passed, 0 failed** -- the smoke tier is
the check that the generic changes (the DFS walk, the dup-source path, the arm hash, the pre-plan share,
MoveOrderPlans, the interned lookups) leave every other deck byte-identical too.

**Instructions per game (callgrind Ir, Profile build, single thread; units identical in every column).**

| game (row shape) | units | base `1a1cf9b9e` | v3b `b1eccb0d9` | v4 `77f338450` | v5 `a70065613` | v6 `dcb857488` |
|---|---|---|---|---|---|---|
| std d3 b10 s3003 gi48 | 63,081 | 10.54 G | 8.61 G (-18.3%) | 8.39 G (-20.4%) | 8.19 G (-22.3%) | **8.12 G (-23.0%)** |
| 2HG d5 b20 s2002 gi10 | 234,051 | 25.35 G | 22.15 G (-12.6%) | 22.10 G (-12.8%) | 21.98 G (-13.3%) | **21.92 G (-13.5%)** |
| std d5 b20 s3003 gi23 | 247,604 | 55.77 G | 41.54 G (-25.5%) | 38.60 G (-30.8%) | 37.52 G (-32.7%) | **36.88 G (-33.9%)** |

Where the d5 game's instructions went (self Ir, base -> v5): the greedy walk 7.48 G -> 0.38 G (+ the DFS and its leaf
4.6 G, which now also carries the prefilter); SubsetVampOutletInvalid and its helpers ~16.2 G inclusive -> 4.2 G (SubsetVampOutletInvalidPre);
SubsetHasDuplicateSacSource 2.95 G -> 0.29 G (the compact path, and the prefilter now refuses most selections before it);
consider() 10.4M calls -> 1.6M (the prefilter). The 2HG game moved least because its cost is the generic
engine: BuildSimKey (7.5% of it), CollectActions' own body, the simulation -- see P.4.

**Per row** (units/game are unchanged by construction; the Ir change is the reference game of the row's
shape -- one game per shape, so read it as an estimate for the row; expected wall change ~ the Ir change,
since nothing here trades instructions for memory traffic):

| row | units/game | digest (unchanged) | Ir, reference game | est. speedup |
|---|---|---|---|---|
| vampires2hg_regression_d5_s2002 (the worst) | 248,047 | 45ce2008fa2885e3 | -13.5% (2HG d5 gi10) | ~1.16x |
| vampires2hg_regression_d3_s2002 | 140,298 | 81b004286de687c8 | ~-13% (2HG shape) | ~1.15x |
| vampires_regression_d5_s3003 | 124,147 | da10a7163b53fc9a | -33.9% (std d5 gi23) | ~1.5x |
| vampires_regression_d5_s2002 | 82,078 | 2cdc42ec48de0ce6 | ~-30% (std d5 shape) | ~1.4x |
| vampires_regression_d3_s3003 | 71,370 | 20c06430dede6629 | -23.0% (std d3 gi48) | ~1.3x |
| vampires_regression_d3_s2002 | 57,609 | c1dcca8497cb0507 | ~-23% (std d3 shape) | ~1.3x |

A paired WALL A/B (base and v6 binaries running the seven rows concurrently, 10 threads each, the box shared
with another container) gave per-row core-ms ratios from 0.85 to 1.17 with no consistent sign -- the
brief's warning, measured: wall is not a measurement on this box, Ir is.

### P.4 What remains, and what was not done

- **2HG is now generic-engine bound.** After the changes the 2HG d5 game's top items are BuildSimKey (7.5%,
  the TT / memo / dedup keys -- a 128-bit fold chain whose key values must not change), CollectActions' own
  body (3.6%), FinalizeFoldTags + ActionFoldSig (2.3%), CreatureConvokeArms (1.3%), the simulation itself.
  No Vampires-shaped cost above 1.6% is left there.
- **The provider's discard-pin ORDER costs ~2.7% of a 2HG game.** ActivationDiscardClasses asks
  VampiresProvider::ActivationDiscardCandidates, which starts from VialProvider's cleanup ranking -- the
  Slivers bucket policy (`vialcurve::Fill`, a three-turn curve fill) inherited through VialProvider -- once
  per discard-cost source per CollectActions call. It only ORDERS the pins (every pin is still branched), so
  a cheaper order is a provider decision (USER-owned) and was not touched; see the question below.
- **The greedy walk's refused leaves (d5).** A d5 game's rollout walk still builds ~10M selections, 86% of
  which the Vampires rules refuse (the prefilter refuses them before consider(), but each still costs the
  selection build, a sort and the rule pass, ~1,000 instructions). The next step would be to not generate
  them: rule 1 (over-pinned discards) is monotone in the selection -- a high part of the odometer that
  already over-pins a card refuses every completion -- so the DFS could skip that subtree, with the same
  fencing as the prefilter. Not built (time; it is the one change here that alters the walk's shape).
- `MTG_VAMP_RULE_PRE_LAZY` (K = 8) was set from the call-per-enumeration counts (2HG greedy ~3, d5 greedy
  ~125, search walks 10-85), not swept.

### P.5 Commits (branch `vampires-perf-p`, backup `backup/2026-10-21/vampires-perf-p`)

Checkpoint commits on top of `1a1cf9b9e`, each built and verified as a round (P.3): `bbb5574ee` + `46a26262d`
+ `cc3418dcb` (round v2: DFS walk, VampRulePre, dup skip, arm hash, collect memo, steal recursion, fan
moves, pre-plan share), `fa13b35e1` .. `b1eccb0d9` (v3b: MoveOrderPlans keys, fan memo, lazy pre, interned
lookups, tier keys, memo verify), `77f338450` (v4: compact dup path, single-pass fast rules, lazy rule 4,
DFS leaf by index), `a70065613` (v5: the walkers' prefilter, high digits cached), `dcb857488` (v6: put-name
filter, high part by index), then this document. Every round: digests, units and per-game `.wins` identical on all seven rows,
`quick.json` identical with every VERIFY mode on, `mtg-test` 1,660/1,660, smoke 128/128 PASS.
