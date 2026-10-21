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
| 13 | small: `enumstats::Enabled()` read once per walk; `MTG_EQUIP_ALL_HOSTS` cached; CreatureConvokeArms' vectors reserved | -- | Process-constant reads; capacity only. |

### P.3 Evidence

(filled in below)
