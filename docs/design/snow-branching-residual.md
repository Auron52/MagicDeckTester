# Snow's residual branching: what it is made of, and the one lever left

Self-contained. Written 2026-09-22, immediately after the payment class was closed
(`snow-payment-solver-cost.md` §§10-12). It answers the user's question of that evening:
*"why is the remaining branching so significant? Can we cut it further? There should be headroom in
the heaviest games."*

Companion to `snow-breakpoint-degeneracy.md`, which established that Snow's phase-A cost **is**
breakpoints. This doc prices the branching that survives every filter now shipped, and names which
of the plausible causes are dead.

Everything below is from counters-only censuses (`MTG_BF_CENSUS`, `MTG_DEDUP_CENSUS`,
`MTG_ROLLOUT_STATS`), so every armed run is byte-identical to a shipped one. Engine = HEAD
(`fe5ca05a`) plus the uncommitted payment correctness fix, i.e. the Coldsteel Heart colour lock is
honoured by the backtracker.

## 1. The user's premise is right, and here is the size of it

Two configurations, same deck, same profile:

| | decisions | candidates | mean width | max width | units |
|---|---|---|---|---|---|
| **K** — shipped GT key, d3 / budget 10 ms, **100 games** | 101,406 | 5,221,746 | 51.5 | 944 | 11,121,264 |
| **H** — one heavy game, d4 / **budget 0**, max-turns 8 (s8043 gi35) | 40,142 | 6,059,152 | **150.9** | 348 | **16,807,204** |

**One unbudgeted game costs 1.5x what a hundred budgeted games cost**, at three times the mean
decision width. That is the headroom the user pointed at, and it is not a tail *within* the shipped
key — it is a different regime.

Because a per-game unit count is deterministic, the shipped key's distribution can be measured
exactly (100 single-game runs, `--seed 1001 --game-index i`):

```
n=100  mean=109,353  median=116,909  p90=121,875  p99=122,377  max=122,377
top-10% of games = 11.2% of cost      top-20% = 22.3%
```

**At the shipped settings there is no heavy tail at all** — cost is budget-bound and therefore
almost perfectly flat (max/mean = 1.12), and several games land on the *same digit* because the
budget converts to a unit ceiling. So "the heaviest games" means the **unbudgeted** paths: the
value-leaf label ladder and phase A, which is exactly where the 40-80 h generation estimate lives.
Any branching work should be priced there, not at d3/b10.

## 2. Four candidate causes. Three are dead, measured.

### DEAD — interchangeable-copy symmetry (the 4 Sheets / 4 Augurs)

This was the obvious hypothesis and it is already collected. Both halves of the fold are adopted and
default-ON: `MTG_FOLD_ACT_SOURCES` and `MTG_FOLD_HAND_CASTS` (both 2026-09-09). What is left:

| | raw | distinct_exact | distinct_srcblind | repeat_share | copyaxis_share | collapse |
|---|---|---|---|---|---|---|
| K | 5,221,746 | 5,221,746 | 5,062,801 | **0** | 3.0% | **1.031x** |
| H | 6,059,152 | 6,059,152 | 5,534,489 | **0** | 8.7% | **1.095x** |

`repeat_share = 0` means the enumerator never emits the same plan twice. `collapse = 1.03x` means
erasing *every* source id and hand index — an upper bound no sound rule can reach, since two copies
only fold when `PermIsPlainForFold` proves it — would remove 3% of candidates. **There is no
symmetry prize left.** The fold works.

### DEAD — the search re-covering ground at the node (`MTG_BP_NEW_ONLY`)

Applied properly, default ON since `773e327f`. At K it drops **57.6%** of continuation entries
(936,479 of 1,626,486); at d4 unbudgeted, 73.9%. Survivors match the user's criterion by
construction: `kept_new` 76%, `kept_act` 20%, `kept_plan` 4%. And the continuation lists are clean —
`[bp-cands] lists=1,070,613 entries=2,442,534 distinct=2,442,534 duplicate=0 (0.0%)`.

### PARTLY ALIVE — subset optimism (CORRECTED 2026-09-23: 7.3%, not 0.74%)

**The verdict below was measured on the wrong event and is superseded.** `g_dropped_cast_count`
counts a dropped **cast**. An **activation** that cannot pay is a different event: ApplyPlanDirect's
`ActivatePermAbility` arm pre-taps the source, and if `TapForCostDirect` fails it silently untaps and
moves on — no cast is dropped, so every existing instrument read those plans as applying cleanly. On
Snow the activations *are* the cost, so the original cross-tab was blind to exactly the case that
matters. New counters (`g_stranded_activation_count`, `dedup_why_act`), 20 games at the GT key:

```
stranded_activations = 94,905     of_which_tapdraw = 94,905   (100%)
strand_dup = 71,734 (4.7% of dup)   strand_but_UNIQUE = 21,401
drop_dup   = 39,630 (2.6% of dup)   dropped_casts = 47,461
```

**Every activation the engine fails to pay on this deck is a tap-draw.** The user's affordability
argument is what found it: a plan such as `Skred + Frost Augur + activate Scrying Sheets`, against a
real board of 2 Snow-Covered Island + Boreal Druid + 2 Arcum's Astrolabe, bills 4 mana (activation 2,
Skred 1 via a filter, Augur 1) with **3** available — the two Astrolabes are `any_color_filter`
(`{1},{T}: add one of any colour`), net zero mana, and the activation taps the Sheets so it cannot
also produce. Such plans are in the candidate list and they strand.

Combined optimism is therefore **7.3%** of duplicates, not 0.74%. Real, worth fixing, and still not
the main driver — see below.

### (superseded) the original cast-only measurement

This was **my** hypothesis, and it had a good pedigree: the enumerator deliberately over-credits cost
(the `ritual_float` / blink-credit contract explicitly relies on "an over-credited plan's unpayable
follow-up is dropped by the pay path and scores honestly"), and `g_dropped_cast_count` exists
precisely to ask *"does applying it drop one of its own casts?"*. If a subset's extra cast is dropped,
its post-apply state is identical to the smaller subset's **by construction** — a duplicate the
enumerator manufactured and could losslessly refuse.

Cross-tabbed it (new `dedup_why` counters, same `MTG_DEDUP_CENSUS` gate). It is not the mechanism:

```
drop_dup=776   nodrop_dup=104,747   drop_dup_share_of_dup=0.0074
drop_but_UNIQUE=152   dropped_casts_total=928   (of 189,850 candidates)
```

**0.74%.** Apply-time cast drops are rare (0.5% of candidates) and when they do happen the candidate
often still lands somewhere new — so "dropped a cast" is not even a duplicate predicate. This agrees
with `snow-breakpoint-degeneracy.md`'s independent finding that Snow's affordability filters prune
**0 of 72,461**. No payability tightening at the enumerator reaches this cost.

### ALIVE — and it is a SELF-COLLISION, not a transposition (CORRECTED TWICE; see §9)

> **SECOND CORRECTION, 2026-09-23 (same day).** The "transposition" reading in this section is
> **also wrong**, and it is wrong for a measurable reason: the family cross-tab that produced it was
> keyed on `Plan::bp_base`, a field `AppendBreakpointVariants` stamps on the **rank variants only**.
> The uniform-deviation arm and the chain slot leave it at `-1` — 3 of the 5 variants emitted per
> base plan at the shipped settings — so 60% of variants were filed as their own family and every
> duplicate of theirs read as a cross-family transposition. Re-keyed on content, it is
> **72.8% SAME family / 27.2% different**, the exact reverse. **§9 is the corrected account and the
> one to read.** What survives from this section: the per-node width really is minimal, and the
> duplication really is between candidates rather than inside one.

**The "case (C) powerset" reading below is wrong and is retracted.** Dumping real decisions with
`MTG_BF_DUMP` refuted it: a turn-3 node reads `width=108, alphabet=5 distinct` and a turn-4 node
`width=154, alphabet=5` — 3-5x MORE than the 32 subsets that alphabet can form. The alphabet is
counted by *name*, but the odometer's digits are per-name **counts** (a 2-of in hand is 3-valued:
0, 1 or 2 copies — which is the already-folded form), and on top of that sits the **land drop**,
which is `Plan::land_to_play` and **not an Action at all**, so it was invisible to every alphabet
count and to the source-blind census alike. Measured: 2-3 distinct land choices per node
(`Scrying Sheets` / `Rimewood Falls` / `Snow-Covered Island` / none), worth 1.21-1.54x on the
printed sample, while hand-copy folding contributes only 1.00-1.14x (i.e. the fold works).

So the width is genuinely minimal per node. **The duplication is between nodes**, and it is
overwhelmingly a breakpoint transposition (20 games, GT key):

```
dup_bp = 1,418,409   dup_nobp = 104,870     ->  93.1% of duplicates are BREAKPOINT VARIANTS
                                                = 61.6% of every candidate the search scores
```

The same turn is reachable two ways: **(a)** a base plan that already casts the drawn card, because
the enumerator planned the whole turn ahead, and **(b)** a shorter base plan whose breakpoint
continuation casts that same card. Both are enumerated, applied and scored, and they land on the
same state.

This is **not** a failure of `MTG_BP_NEW_ONLY`, and the distinction matters: continuation lists are
1.7 (heavy) to 2.3 (GT key) entries and the filter drops 57.6-73.9% of what it sees. The duplication
is not *inside* a list — it is between the list and the base enumeration. `bp-node-partition.md`
measured the same quantity independently as its cross bucket, **1,192,267 of 1,541,982 dupe
children**, against 1,418,409 of 1,523,279 here: two unrelated instruments, same ratio.

Mass sits where the enumeration is widest — `bf[65-128]` alone carries **44.5%** of all candidate
mass, and 65.4% is in decisions >=65 wide.

The alphabet is the two repeatable tap-draws plus the hand:

```
bf_shape  cast_from_hand 78.0%   bp_variant 64.3%   chosen_x 63.7%   (overlapping)
bf_x_by_card   Frost Augur 2,184,316    Scrying Sheets 1,863,051   (= 77.5% of all mass)
bf_src         Frost Augur 4 distinct physical sources; Scrying Sheets 4
```

And the payoff number:

```
dedup_census  seen=5,150,257  dup=3,412,801  dup_rate=0.663
              copy_perm=2,016,361   copy_FALSE=732,280
dedup_exact   repeats=0
dedup_why     dup_bp=79,730   dup_nobp=25,793     -> 75.6% of duplicates are BREAKPOINT VARIANTS
```

**66.3% of scored candidates land on a post-apply state a sibling already reached, with both folds
on, with zero literal plan repeats, and three quarters of them are breakpoint variants.** So the
convergence is not repetition, not symmetry, and not payment optimism. It is distinct subsets of a
genuinely distinct alphabet meeting at the same state — and the breakpoint machinery is where they
meet.

## 3. Why that is the same finding `bp-node-partition.md` already root-caused

That doc's cross-dupe bucket is this: *"the cross bucket is the base-plan enumerator enumerating PAST
the breakpoint and the node then covering the same ground again. That is exactly what the USER's
direction forbids ('Enumerating ahead without the drawn or staged cards doesn't make sense either
way'): the doctrine was implemented for the node but **never enforced on the enumerator feeding
it**."* Sized there at cross 1,192,267 of 1,541,982 dupe children, ~10% of node units.

Two independent instruments now agree on one cause. `MTG_BP_NEW_ONLY` enforced the doctrine on the
**node**; the **enumerator** still plans the whole rest of the turn past a breakpoint it knows is
coming, and the node re-plans it. The named unbuilt fix is **truncate-at-emission** (truncate *after*
`eval_and_push`'s filters — a naive pre-filter drop is lossy and confirmed so).

**It is blocked, and the blocker is a real one**, recorded at the rejection of
`MTG_BP_PREFIX_PREPAY` (which failed on quality: +0.0200 hold, t 6.20, paired 5000/cell):

> *"Do not re-propose this without a payment model that KEEPS THE JOINT ALLOCATION while dropping
> only the truly-dead tail."*

The reason is the user's own principle: the float a base plan taps for its tail *is not waste* — the
allocation is chosen jointly across the whole turn and that joint choice is better for the node's
continuations than a prefix-only payment. So truncation cannot simply cut the tail; it must defer
commitment. This is a design task with a quality risk, not an optimization, and it changes what the
search computes — the user's call by the standing estimand rule.

## 4. What is NOT worth re-opening (checked this session, so nobody re-checks it)

* **Arming the enumeration memo under breakpoint continuations.** `snow-breakpoint-degeneracy.md`
  floats separating `g_bp_enum_depth`'s two jobs as "a small change worth measuring on its own". The
  enum-memo header refutes it directly: *"NO memo under the bp continuation enum: EnumerateBreakpointPlans
  already caches that context at its own level -- double storage for zero extra hits."* And the memo
  itself is a **measured negative** (2026-08-14): identity held, but wall inverted from -9% at d3 to
  **+14%** on a heavy d5 game, because the key walk costs as much as a small enumeration. Its own
  prescription is *"make the KEY incremental, not tune the cache."* Snow's measured hit rate today is
  6.0% (`hits=7,036 misses=110,271`), consistent with that record. Leave it.
* **A plan-signature skip instead of a post-apply-state skip.** Re-measured, still unsound:
  `copy_FALSE = 732,280` against `copy_perm = 2,016,361` — a 27% false rate. The plan does not
  determine the state. Refused by the no-lossy-truncation bar, as documented.
* **`MTG_FOLD_COUNTER_SOURCES`.** Built, default off, and irrelevant to Snow — no spore or quest
  counters in the deck. It is a Fungus lever.
* **`PermIsPlainForFold`'s `chosen_color != -1` refusal.** It refuses every Coldsteel Heart (all four
  carry a locked colour), and it is the same over-broad shape as the spore clause that
  `MTG_FOLD_COUNTER_SOURCES` fixes by moving the distinction into the tag — two Hearts that both
  chose Blue *are* interchangeable. **But it is inert here:** Coldsteel Heart carries no activated
  ability that `CollectActions` enumerates (it is a mana source, spent by the payment path, which
  ranks sources separately). Worth fixing for correctness-of-intent if a deck ever activates a
  colour-locked permanent; worth nothing on Snow.

## 5. A "card-data defect" that was not one — retracted

An earlier draft of this doc claimed `cards.json` gives **Scrying Sheets**' look ability a `{T}` that
the printed card lacks, and filed it as a rules-correctness item for the user. **That was wrong and
is withdrawn.** Scryfall's oracle text:

```
{T}: Add {C}.
{1}{S}, {T}: Look at the top card of your library. If that card is snow, you may reveal it and put
it into your hand. ({S} can be paid with one mana from a snow source.)
```

The tap **is** part of the cost, exactly as `cards.json` models it (`tap_draw_cost "{1}{S}"`, and
`PermAbilityTaps(TapDraw)` true), and the look ability genuinely does compete with the land's own
mana ability. The card data is correct; the claim came from unverified recall.

Recorded rather than deleted because it is a clean instance of the failure the claude-play skill's
Rule 0 exists for — *"always read the deck's cards from `src/cards/data/cards.json` … Claude's card
recall is unreliable, and unverified flags are usually card-data mistakes"* — committed in a session
that had already quoted that rule. The check costs one `curl` to Scryfall, which works from this
container.

## 6. Bottom line (REWRITTEN 2026-09-23 — the first version was wrong)

The first version of this section concluded the residual branching was "not redundant work" and a
"genuine powerset". **Both claims are retracted.** Real candidate dumps and the activation counters
say otherwise:

* **Per-node width is minimal.** Symmetry folds to 1.03x, hand copies to 1.00-1.14x, literal plan
  repeats are zero, continuation entries are zero-duplicate, and the extra width over the name
  alphabet is the per-name COUNT digits plus the land drop — all real choices.
* **Between-node duplication is enormous.** 66.1% of scored candidates land on a state a sibling
  already reached, and **93.1% of those are breakpoint variants** — 61.6% of everything the search
  scores. The mechanism is a transposition between the base-plan enumerator planning past a
  breakpoint and the continuation covering the same ground.
* **Subset optimism is real but secondary at 7.3%** (2.6% dropped casts + 4.7% stranded
  activations), and 100% of the stranded activations are tap-draws.

So the target is not the width and not the continuation lists. It is the **duplicate plans**, and the
named fix remains enumerator-side truncate-at-emission, gated on the deferred joint-allocation
payment model (§3).

**Open question to settle first (cheap):** are the duplicate bp variants duplicates of *their own*
base plan, or of a *different* one? `Plan::bp_base` is already on the plan, so it is one cross-tab.
Different base plans => truncate-at-emission is the right fix. Same base plan => the continuation is
adding nothing observable and a cheaper local test exists.

## 6a. Instrumentation added by this investigation (all counters-only, default OFF)

| what | gate | why it did not exist |
|---|---|---|
| `dedup_why` (drop_dup / nodrop_dup / dup_bp / dup_nobp) | `MTG_DEDUP_CENSUS` | nothing cross-tabbed duplicates against *why* |
| `dedup_why_act` + `g_stranded_activation_count` | `MTG_DEDUP_CENSUS` | **nothing counted an unpayable ACTIVATION at all** |
| `MTG_BF_DUMP`: game STATE (hand / board / mana / library top) | `MTG_BF_DUMP` | a width could be counted but not argued about |
| `MTG_BF_DUMP`: plan-level axes + all fingerprinted Action fields | `MTG_BF_DUMP` | the land drop is not an Action and was invisible |

The one real cut left is the enumerator-side half of the breakpoint doctrine, and it is gated on the
deferred payment model, not on measurement. The only *built* lever that collects any of the 66%
convergence is `MTG_CAND_DEDUP`, priced in §7.

## 7. `MTG_CAND_DEDUP`: the open "total cost NOT RESOLVED" verdict is now resolved

`MTG_CAND_DEDUP` skips a candidate whose post-apply state an earlier sibling of the same pass already
reached — i.e. it collects part of the 66% from §2. Its quality verdict has been clean since
2026-09-09 (regression `slower=0 faster=5`, smoke `slower=0 faster=7`, *"Nothing regressed
anywhere"*). It ships **default OFF** for one stated reason, worth quoting because it is the thing
being closed here:

> *"Snow 300 TOTAL COST — **NOT RESOLVED**. … The WITHIN-arm spread swamps the 0.8% between-arm
> difference. This box is shared with other agents, so neither wall nor CPU time can resolve an effect
> this small. An earlier 'wall-neutral' claim here was NOT supported and has been withdrawn; settling
> it needs a low-noise instrument … not more reps of the same kind."*

Three things let it be settled now: the box is **idle**; the duplicate rate is **higher** on today's
engine (66.3%); and measuring **unbudgeted** removes the play-change confound entirely, so the arms
are byte-identical and any difference is pure work.

**Design.** d3, budget 0, `--max-turns 5` (the horizon cap is what bounds cost — a *budget* cap would
re-introduce the confound, since freeing work changes what fits), 24 games, 12 threads, seed 1001.
Arms run strictly sequentially, two passes in **opposite order** so a drifting box shows up as
disagreement between passes rather than as a result. The instrument is **CPU time (user+sys)**, because
`units_total` already gives exact work and the one thing units cannot see is the `BuildDedupKey`
hashing this trades search for — which is the entire open question.

| arm | dedup | wall (s) | CPU (s) | units |
|---|---|---|---|---|
| Uoff  | 0 | 55.59 | 252.68 | 34,075,517 |
| Uon   | 1 | 50.48 | 236.42 | 32,318,475 |
| Uon2  | 1 | 49.61 | 235.53 | 32,318,475 |
| Uoff2 | 0 | 50.33 | 245.20 | 34,075,517 |

* **Play is byte-identical across all four arms** (full stdout diffs clean; avg 5.8333 in every arm).
  Lossless as claimed, asserted rather than assumed.
* **Units −5.16%**, and identical to the digit within each arm — deterministic, so this number needs
  no replication.
* **CPU −5.2% on means (248.94 → 235.98), and every OFF rep is slower than every ON rep.** That
  ordering property is the load-bearing claim, not the means: the off arm's own spread is 3.0%
  (252.68 vs 245.20, the first rep being the outlier, consistent with a cold start) against a 5.2%
  effect, so the means alone would be marginal. The same standard the fold adoption used.
* **Wall −5.5%**, same direction, secondary (with 24 games on 12 threads it is load-imbalance-sensitive).

**Verdict: the hashing does NOT eat the saving.** CPU moves the same way as units and at the same
magnitude, which is exactly the outcome the withdrawn "wall-neutral" claim could not establish. The
prior header's warning — *"do not price a change that adds NON-SEARCH work in units alone"* — is
satisfied: both instruments agree.

**Scope and what is NOT claimed.** This is one seed block in the *unbudgeted* regime, which is the
regime that matters for phase A / the label ladder but is not the shipped d3/b10 key. Under a budget
the arms are **not** byte-identical — freed work changes what fits, which is precisely why turning it
on moves 17 GT keys. So this resolves the *cost* question only; adoption is still a ground-truth
rebaseline and therefore the user's call. Recommendation: worth adopting on the combined evidence
(clean quality, −5% work, −5% CPU, lossless by construction), and the natural moment is the same
rebaseline that accepts the three Snow payment keys.

## 8. Method notes from this session, so they are not re-learned

* **I mis-sized the unbudgeted A/B three times** (20 games/10 threads = 22 min per arm; 6 games/6
  threads = still >29 min, because with games ≤ threads the wall is just the *slowest* game and Snow's
  unbudgeted tail game is enormous). The fix that worked was a **55-second calibration run** before
  committing to a 4-arm design. Calibrate first; unbudgeted Snow is not budgeted Snow.
* **`pgrep -f <script>` in a cleanup line matches its own command line** and kills the shell running
  it (exit 143/144). Kill by captured PID, or match on `-C mtg`. Already on record as a lesson; it
  recurred here.
* A `sleep` waiter is capped at 10 minutes per tool call, so long waits must be chunked — and the
  measured run must live in its own session (`setsid`) so a killed waiter cannot take it down.

---

# 9. THE DUPLICATES, ROOT-CAUSED (2026-09-23, session 4). Two shipped arms, ~99% redundant.

This section supersedes §2's "ALIVE" reading and §6's bottom line. It is the answer to the user's
question *"figure out how to prevent these duplicate plans"*, taking the **lossless** route only
(their instruction: *"It makes sense to start with the lossless lever which is deduplication. The
'prioritize casting spells' lever is something we can look at later when all lossless options have
been exhausted."*).

## 9.1 The instrument, and the bug in its first version

The question was: when a candidate duplicates an already-reached state, **whose** state does it
duplicate? `census_seen` became a map from `BuildDedupKey` to the first reacher's `{family, bp_choice}`
so a duplicate can name its partner.

**The first family key was `Plan::bp_base`, and that is wrong.** `AppendBreakpointVariants` stamps
`bp_base` in the rank-variant loop and **nowhere else** — the `BpUniformDevEnabled()` arm and the
chain-slot loop both leave it at its default `-1`. At the shipped settings (`MTG_BP_SEARCH`=2,
`MTG_BP_DEPTH`=1, `MTG_BP_CHAIN_SLOT`=1, `MTG_BP_EMPTY_ARM` off) wave 0 emits **5 variants per
selected base plan** — 2 rank, 2 uniform, 1 chain — so **3 of 5 carry no family at all**. Keyed on
`bp_base` they each become their own family and every duplicate of theirs is misread as a
cross-family transposition. That is exactly what happened, and it produced a confident
"74.9% transposition" that is the reverse of the truth.

The fix: key on the candidate's **content fingerprint with the three `bp_*` fields left out**. That
is precisely "the base plan this variant was cloned from", and it is safe to key on because
`dedup_exact` measures **zero** literal plan repeats, so distinct base plans have distinct content.

**Lesson, and it is the same one as the cast-only counter earlier in this document:** a cross-tab is
only as good as its key, and a key that is a *field on a struct* has to be checked at every site that
writes the struct — not just at the one site the reader has in mind.

## 9.2 Corrected: the duplication is a SELF-COLLISION

Snow, 100 games, shipped `d3/b10` key:

```
dedup_census  seen=5,150,257  dup=3,412,802  dup_rate=66.3%
dedup_why_fam same_base=2,484,124 (72.8% of dup)   diff_base=928,678 (27.2%)
              of_same: vs_OWN_base=2,138,355       first_was_variant=587,488
```

`vs_OWN_base` = 2,138,355 = 62.7% of duplicates = **41.5% of every candidate the node applies**
lands where its own base plan already landed. The unbudgeted regime agrees closely
(diff_base 27.6%, `vs_OWN_base` 10,355,573 of 15,857,610).

So **truncate-at-emission — the fix §2 and §6 name, and the one design-blocked on the deferred
joint-allocation payment model — was aimed at the smaller 27%.** It is not the lever.

## 9.3 Which emission arm produces them

`dedup_why_arm` splits every applied candidate by which arm of `AppendBreakpointVariants` emitted it:

| arm | applied | duplicate | rate | unbudgeted rate |
|---|---|---|---|---|
| `base` — an ordinary plan | 1,840,784 | 358,639 | 19.5% | 19.5% |
| `rank` — wave-0 rank variant | 1,323,843 | 1,088,901 | 82.3% | 87.1% |
| `unif` — `MTG_BP_UNIFORM_DEV` (`bp_all`) | 1,323,758 | 1,313,390 | **99.2%** | **99.6%** |
| `chain` — `MTG_BP_CHAIN_SLOT` | 661,872 | 651,872 | **98.5%** | **99.7%** |

`unif` + `chain` are **38.6% of every candidate applied**, at ~99% duplicate. The mechanism is
structural, not statistical, and each arm's own header explains it:

* **`bp_all` IS the plain rank variant whenever an apply reaches exactly ONE eligible breakpoint.**
  "Take candidate k at *every* breakpoint" and "take candidate k at breakpoint 0" are the same
  instruction when there is only a breakpoint 0. Snow's applies overwhelmingly reach one
  (2.95M breakpoint consultations against 2.75M interior nodes — about one per node).
* **the chain slot resolves to the j-th continuation that opens a FURTHER breakpoint.** When none
  does, `BpChainCandIndex` returns −1, nothing resolves it, and it falls through to EMPTY. Its own
  header says so: *"When no continuation opens a further breakpoint (every non-chain deck, and most
  turns of a chain deck) the scan finds nothing, the variant collapses onto its base plan."*

Both arms were built **for chain decks** — the chain slot was adopted on two Dragonstorm reference
games going 5→4, and `bp_all` is described as *"the only shape in which a turn whose payoff needs the
SAME decision repeated down a chain becomes expressible at all"*. Snow is not a chain deck. Nobody
re-costed wave 0's fan-out for a deck that opens one breakpoint per apply.

## 9.4 Measured: lossless, and worth 21%

Unbudgeted `d3` + `--max-turns 5` (a HORIZON cap, not a budget cap — a budget re-introduces the
re-spend confound), 24 games / 12 threads, via `--batch --game-log-dir` so every game yields a win
turn **and** a play digest. Eight arms, second half in reversed order so box drift shows up as a
disagreement between halves rather than as a result.

| arm | CPU (user+sys), rep 1 / rep 2 | units_total | vs default |
|---|---|---|---|
| default | 254.93 / 250.70 | 34,075,517 | — |
| `MTG_BP_UNIFORM_DEV=0` | 213.39 / 214.10 | 27,626,418 | −15.5% CPU, −18.9% units |
| `MTG_BP_CHAIN_SLOT=0` | 230.32 / 226.34 | 30,733,309 | −9.7% CPU, −9.8% units |
| **both off** | **203.14 / 195.21** | **24,282,582** | **−21.2% CPU, −28.7% units** |

**Play is byte-identical in all 7 non-default arms** — every game's win turn and play digest match
the default's. `units_total` is exact and repeats to the digit. Every arms-off rep is faster than
every arms-on rep, and the 21% effect dwarfs the 2-4% within-arm spread.

Two instruments are used deliberately: `units_total` is exact but **cannot see the saving**, because
a duplicate is already skipped from its rollout by `bp_seen_states` — what it costs is a `GameState`
copy plus an `ApplyPlanDirect`, neither of which is charged a unit. CPU is the instrument; units is
the control, and it moves *more* than CPU because the skipped candidates also each consumed one
`kLookaheadCand` unit.

## 9.5 The budgeted key: also inert on play, and the cost does not show

Same arms at the shipped `d3/b10`, 100 games, pooled with three chain decks (Dragonstorm,
BreachingDragonstorm, Mirrorwing) at their own profile settings, 60 games each:

```
snow:       n=100  moved_any=0  moved_turn=0
dstorm:     n=60   moved_any=0  moved_turn=0
breaching:  n=60   moved_any=0  moved_turn=0
mirrorwing: n=60   moved_any=0  moved_turn=0
```

**Zero play movement on 280 games across 4 decks, including the two Dragonstorm decks the chain slot
was adopted for.** That is the surprise, and it widens the question from "adopt for Snow" to "are
these arms earning their keep anywhere" — which 4 decks cannot answer and the suite can.

The same run has **no power on cost** and must not be read for it: 14 s wall per arm, and the three
reps came in at 155.0 / 159.2 / 162.3 CPU in *launch order*, so the drift between two runs of the
same arm exceeds any arm effect. That is expected rather than disappointing — at `d3/b10` the work is
**budget-bound**, so removing duplicate candidates frees units the pass has nothing else to spend on.
It is also why play does not move: nothing is truncated differently.

## 9.6 A hypothesis of mine that died before it was built — the rank-0 identity

Reading the resolver, `base` ≡ `rank(k=0)` ≡ `unif(k=0)` looked provable at the shipped canon
defaults (`MTG_BP_BASE_CANON`=1, `MTG_BP_NESTED_CANON`=1, both adopted 2026-09-17 with the greedy
deletion): a base plan takes `ncands.front()` at every breakpoint it reaches; rank (at, 0) takes
`cands[0]` at breakpoint `at` and `ncands.front()` elsewhere; uniform 0 takes `cands[0]` everywhere —
the same entry of the same memoised list in all three. That would be **2 redundant applies of every
6 candidates, on every deck**.

Measured before building anything:

```
dedup_why_k0 rank0 in-playout  n=657,637  own_base_share=0.588
dedup_why_k0 rank0 searched    n=4,318    own_base_share=0.715
dedup_why_k0 unif0 in-playout  n=657,563  own_base_share=0.585
dedup_why_k0 unif0 searched    n=4,317    own_base_share=0.715
```

**Refuted.** 11.6% of choice-0 variants reach a state nothing had reached. The obvious missing
condition — both canons stand down inside a playout (`g_rollout_nest == 0` is required by each) — is
refuted too: splitting on it gives 0.588 / 0.715, neither of them ~1, and 99.3% of these candidates
are in-playout anyway. So the reading is wrong somewhere else and **the skip was not written**.
This is the seventh hypothesis in this campaign to die to its own control. The counters stay
(`dedup_why_k0`) so nobody re-derives it from the resolver and believes it.

## 9.7 What to do about it — and what is still open

**The two-flag route (available now, measured, lossless on everything tested).** Adoption is
**per-deck**, because both arms are chain-deck machinery:
* `MTG_BP_UNIFORM_DEV` already has a `heurarm` slot (`heurarm::BP_UNIFORM_DEV`), so it is settable
  from Snow's profile with no code change.
* `BpChainSlots()` reads its env var **directly, with no arm**, so chain-off for one deck needs a
  slot added. It returns an `int`, so it wants a force-to-zero arm rather than a plain boolean.

**The general route, which is better and is the recommendation.** Do not turn the arms off; make them
**not emit the redundant member**. Both redundancies are decidable from an *earlier sibling's* apply
in the same pass, and wave 0's emission order already puts that sibling first:
* a uniform variant is the rank variant whenever the apply reached **≤1** class-on breakpoint;
* a chain slot is EMPTY-everywhere whenever **no** continuation opens a further breakpoint, which
  `BpChainCandIndex` already computes inside the apply.
Learn both from the rank-0 apply (one new per-apply counter for the class-on breakpoint count, one
bool for chainability), memo them per pass keyed on the base plan's content fingerprint, and skip the
redundant siblings **before** their `GameState` copy. That keeps the arms' full reachability on the
chain decks they exist for — including the turns of a chain deck where nothing chains, which their own
headers say is most of them — and drops them where they are provably redundant. It is deck-independent
and needs no payment model, which is what distinguishes it from truncate-at-emission.

**Still open, in order:**
1. The two-arm A/B across the whole **smoke suite** (20+ decks, committed per-game digests, paired
   seeds) — two sequential runs of the same binary, diffed against each other rather than against GT,
   because the working tree's payment fix moves 3 Snow keys on its own and would otherwise be
   confounded with the arm effect.
2. The chain slot's **own adoption evidence**: `references/Dragonstorm/claude_s1_gi0.json` and
   `claude_s26_gi25.json` (replay as `--seed 1 --game-index 0` / `--seed 26 --game-index 25`). If
   those two still land on the same win turn with the arm off, the arm's justification is gone and the
   question is global, not per-deck. If they break, the arm is load-bearing and Snow takes the
   per-deck route.
3. Only then: build the general sibling-learned skip, and re-measure.

## 9.8 BUILT AND MEASURED: `MTG_BP_W0_UNIF_COLLAPSE` (default OFF)

The recommendation in §9.7 is implemented for the uniform half. It is **not** "turn the arm off" —
§9.5's 280-game null was overturned by the suite, which is why the arm is left in place:

```
smoke, MTG_BP_UNIFORM_DEV=0 vs on, same binary (25 of 93 cases moved):
    WORSE  fluctuator_smoke_d3     3.5533 -> 3.6533  (+0.1000)
    WORSE  fluctuator_smoke_d5     3.5333 -> 3.6000  (+0.0667)
    WORSE  fluctuator2hg_smoke_d3  3.5467 -> 3.6000  (+0.0533)
    WORSE  th_smoke_d5             4.0267 -> 4.0400  (+0.0133)
    5 better, 16 digest-only          net +0.1993 turns
```

**On a deck whose applies open a second breakpoint, `bp_all` expresses a line nothing else can.** So
the arm stays and the *redundant member* goes.

**The mechanism.** Wave 0 emits the rank variants before the uniform ones, so by the time the
candidate loop reaches uniform k, rank k **of the same base plan** has already been applied and
reported how many enabled-class breakpoints it reached (`g_bp_classon_last`, a new monotonic
thread_local read as a delta, the same convention as `g_bp_any_last` and for the same re-entrancy
reason). At `<= 1` the two are the same candidate — and not approximately: for the same k both
applies take the same continuation at breakpoint 0, so the rank variant's count **is** the uniform
variant's count. The skip sits **before** the `ConsumeAt` and the `GameState` copy, because
`bp_seen_states` already keeps duplicates out of their rollouts and the copy + `ApplyPlanDirect` is
all that is left to save. A missing memo entry never skips.

Two supporting changes, both inert to play:
* `Plan::bp_base` is now **stamped by the uniform and chain arms**, where they left it at `-1`. Every
  reader except the FSLineWin remap (which only renumbers whatever is there) requires `!p.bp_all`,
  and both arms set `bp_all`. **Measured, not argued** — see below.
* `g_bp_classon_last`, described above.

**Results.**

| | measurement |
|---|---|
| fires | 5,711,684 skips on 24 unbudgeted Snow games (4,294,757 on the 7-deck pool) |
| cost, unbudgeted Snow | CPU **247.14 / 246.84 → 224.86 / 222.86 = −9.4%**; units 34,075,517 → 28,363,833 = **−16.8%** |
| play, unbudgeted Snow | byte-identical, 4 alternating arms |
| **soundness, 7 decks unbudgeted** | **byte-identical on every deck** — burn, fivecolour, fluctuator, kitty, mirrorwing, snow, treasure_hunt, per-game `.wins` and per-job digest alike, with the skip firing 4.29M times |
| play, budgeted smoke | 10 of 93 cases move: **8 digest-only, 2 better** (fivecolour 4.9467→4.9333, th2hg 4.5400→4.5200), **0 worse**; net −0.0334 turns |
| stamping isolated | smoke with the collapse OFF on the post-stamping binary is **byte-identical** to the pre-stamping baseline (90 passed / 3 failed = exactly the known Snow payment keys) |
| unit | 126/126, 2,634,506 assertions |

**The budgeted movement is re-spend, not loss, and that is now proved rather than inferred.** The
identity is exact, so the falsifiable prediction was "byte-identical unbudgeted on every deck" — and
that is what the 7-deck unbudgeted run returned, on precisely the decks that had moved in smoke.
Under a budget each skipped candidate no longer burns its `kLookaheadCand` unit, so the pass reaches
further before truncating and commits a different line. `MTG_BP_WAVE_NSKIP`'s own note says the same
thing about the same class of skip: *"under a budget, work this skip saves is work the budget spends
elsewhere, so the committed line moves and five GT keys churn"*.

**Adoption.** Default OFF as built. Turning it on is a **GT rebaseline across all tiers** (10 smoke
keys, plus whatever regression/overnight hold), and the suite evidence is 0 worse / 2 better — so by
this repo's own "no regression on ANY axis" test it qualifies as a clean win, and the natural moment
is the same rebaseline that accepts the three Snow payment keys. **It is left OFF pending the user's
call** because flipping it churns every deck's ground truth, which is not an agent's decision to take
on its own.

**Still not done — the chain half.** `MTG_BP_CHAIN_SLOT` is a separate 98.5%-duplicate arm worth a
further ~9.7% CPU on Snow, and it is **not** provably lossless: when no continuation chains,
`BpChainCandIndex` returns −1 and the slot resolves to EMPTY at every breakpoint, which — with
`MTG_BP_EMPTY_ARM` off by default — nothing else in wave 0 provides. So dropping it removes an option
rather than a duplicate. It measures inert on Snow (byte-identical, 280 budgeted games + unbudgeted)
but it is a per-deck judgement, and `BpChainSlots()` has no `heurarm` slot, unlike `BP_UNIFORM_DEV`.

**And the rank arm, for whoever picks this up.** `rank` is still 82.3% duplicate and is the largest
remaining block. One piece of it is provable on the same sibling-learned argument: rank k resolves to
`cands[k]` when `k < n` and to EMPTY otherwise, so two ranks both `>= n` are the same candidate. At
the shipped `W=2` that fires only when `n == 0` (the apply reached no eligible breakpoint at `bp_at`),
which is a smaller class than the uniform one. The rest of the 82.3% is `rank k=0` colliding with its
own base plan at 58.7% — the identity §9.6 refuted — and is not currently explained.

## 9.9 The CHAIN SLOT, and the bigger lever it exposed (`MTG_BP_W0_NOBP`)

### What the chain slot is

A **chain** is a turn that feeds itself: a dig/draw spell puts another dig/draw spell in hand and the
turn keeps going. Each arriving card is a breakpoint, and the continuation list at a breakpoint is
ranked by the static value heuristic, which scores **board**. A continuation that casts another dig
spell buys **options**, not board, so it ranks far down — and wave 0 only reaches ranks `0..W-1`
(`W = 2`). The evidence the slot was built on:

> Dragonstorm reference `claude_s1_gi0` (human T4, search T5): the Apex of Power breakpoint offers
> **47 continuations**, and the one casting the SECOND Apex from hand (+10 mana, 7 more exiles →
> Dragonstorm) sits at **rank 32**. Bisected W≤32 = T5, W≥33 = T4; invariant across budget
> 20 ms–120 s (6000x), depth 5–8, waves on/off, `MTG_BP_MAXBASE=256`, `MTG_UNPRUNED=1` — not
> starvation. Root-only widening does not fix it; W=33 with waves off does.

So instead of widening `W` for every deck, `bp_choice = kBpChainChoice + j` reserves capacity: a
sentinel that resolves at apply time to the j-th continuation which itself opens a breakpoint.

### Measured: its three outcomes are not one thing

New counter `g_bp_chain_ci_last` records where the scan landed (`dedup_why_chain`). Snow,
100 games d3/b10, over the 414,510 applies that reached a chain slot:

| cell | applies | duplicate | meaning |
|---|---|---|---|
| `covered` (ci < W) | 142,762 (34.4%) | **99.5%** | rank `ci` already scored that entry → losslessly skippable |
| `past_W` (ci >= W) | 11,461 (2.8%) | **20.8%** | the case the slot exists for → mostly genuinely new |
| `EMPTY` (ci < 0) | 260,287 (62.8%) | **99.9%** | resolves to EMPTY everywhere → see below |

Dragonstorm control: 51 / 14 / 994.

**SNOW DOES CHAIN — a claim of mine corrected by this measurement.** I had said Snow's continuations
do not chain. 2.8% of its chain applies find a chainable continuation at or past `W`, and those are
only 20.8% duplicate, i.e. mostly real lines (the Astrolabe-draws-an-Astrolabe shape). Disabling
`MTG_BP_CHAIN_SLOT` for Snow would have thrown that away.

**And the arm's own header is STALE, in the same way and from the same commit as NSKIP's.** It says
that when nothing chains "the variant collapses onto its base plan, and behaviour is unchanged".
That was true when an unresolved continuation fell back to a greedy `Solve`; since the greedy
deletion (2026-09-17) it resolves to **EMPTY**, and with `MTG_BP_EMPTY_ARM` off by default the chain
slot is the only wave-0 variant that produces EMPTY-everywhere. So the 62.8% cell removes an
**option**, not a duplicate, and stays out of the lossless bucket. Only `covered` is skippable, and
it needs `ci` reported out of the apply the way `g_bp_cands_last` already is — not built.

### The bigger lever the arithmetic exposed

The chain arm is 661,872 candidates but only 414,510 applies reached a chain slot: **247,362 (37.4%)
never reached an eligible breakpoint at all.** Selection is by `PlanOpensBreakpoint`, a PRE-APPLY
predicate, so a selected base plan can still reach none — the cast is dropped, the source is gone,
the site is masked.

**A variant whose base plan reached no breakpoint IS that base plan.** `bp_choice`, `bp_at` and
`bp_all` are read nowhere in an apply except at a breakpoint, so no occurrence means no read, which
means the same actions on the same state. This is the strongest identity in the file — unlike the
rank-0 one (§9.6) it depends on no canon flag, no rollout depth and no list ordering. It is also
already shipped **for the wave walker** as `MTG_BP_WAVE_NOBP` (default ON) on exactly this argument;
wave 0 simply never got it, and the candidate loop already computes `bp_nobp`.

`MTG_BP_W0_NOBP` (default OFF) skips every variant whose `bp_base` is in that set, before the unit
charge and the `GameState` copy. It covers all three arms at once, so it is tested before the
uniform collapse.

### Results, both skips

Unbudgeted Snow, 24 games, alternating arms:

| arm | CPU | units | vs base |
|---|---|---|---|
| base | 249.74 / 246.19 | 34,075,517 | — |
| `W0_NOBP` | 219.40 | 27,480,150 | −11.5% CPU, −19.4% units |
| `W0_UNIF_COLLAPSE` | 218.22 | 28,363,833 | −12.0% CPU, −16.8% units |
| **both** | **206.68 / 206.45** | **24,385,670** | **−16.7% CPU, −28.4% units** |

They largely add: NOBP fires 6,552,575 times, and the uniform collapse drops from 5,711,684 skips to
3,094,480 when NOBP runs first (≈2.6M overlap). For scale, deleting both ARMS outright was −21.2%
CPU / −28.7% units — so the two **lossless** skips recover essentially all of the units saving and
~80% of the CPU saving **while keeping every option the search had**.

| gate | result |
|---|---|
| soundness, 7 decks unbudgeted | **byte-identical on every deck**, NOBP firing 3,243,385 times |
| smoke, both skips on | 16 cases move: **0 worse, 3 better** (fivecolour d3 −0.0200, fivecolour d5 −0.0134, th2hg −0.0200), 13 digest-only, **net −0.0534 turns** |
| unit | 126/126, 2,634,506 assertions |

### ADOPTED 2026-09-23 — both default ON

Both flags now read `EnvOn("...", true)`; `MTG_BP_W0_NOBP=0` and `MTG_BP_W0_UNIF_COLLAPSE=0` remain as
A/B hatches. The user's bar: *"anything that is an overall win and doesn't introduce unrecoverably
slower games can be adopted"* — met on both counts (net −0.0534 turns, no searched-depth SLOWER game
classified PERSISTS). GT was rebaselined across the moved tiers in the same change, bundled with the
three Snow payment keys from the `CommitPaySacSacrifices` fix.

**A note worth keeping, because I got it wrong first.** I had left both OFF on the reasoning that
adoption "requires a GT rebaseline, which is the user's call". That is not a drawback and not a reason
to stage: ground truth is a *measurement of the engine*, not a thing to protect from improvement. The
rule is the user's two conditions, and a rebaseline is not one of them.
