# The free-action dominance collapse, and why KittyEquipment v2 is intractable

**Status: BUILT, flag `MTG_FREE_EQUIP_MANDATORY`, DEFAULT OFF, measurement in progress (2026-10-01).**
Lever slot `heurarm::FREE_EQUIP_MANDATORY`, so a pooled A/B pins either arm in one batch.

USER, 2026-10-01: *"We need to optimize further here... I think we need to make a dominance
argument. The only cases that would potentially dominate dropping all of the 0-mana cards to draw
with one of our drawing creatures is doing it with multiple out."* And on the class of problem:
*"There are a lot of similarities between this case, Fluctuator and EDF in that there is a long
chain of events that we need to evaluate, but don't really have complicated decisions at each
step."* With the distinction that matters: *"this case is slightly different in that the other decks
win when they go through this process... This deck might be able to win, but it is not guaranteed at
all. Instead it might attack for a lot and win next turn. Or even not be able to attack at all this
turn. But pile a bunch of artifacts on. And finish things next turn."*

Companion docs: `analysis-KittyEquipment-v2-puresteel-hammer.md` (the deck),
`label-goff-tractability.md` (the EDF go-off cuts), `per-decision-work-census.md` (the instrument).

---

## 0. Why the EDF / Fluctuator precedent only half-transfers

EDF and Fluctuator have the same surface shape — a long chain of individually-trivial steps — and
the engine already collapses both. But every one of those cuts leans on the chain ending in a
**win**: `MTG_LABEL_GOFF` cut 2 is literally a floor short-circuit ("a this-turn win is unbeatable,
so..."), and `EdfAutoGoOffAfterCasts` recognises a combo, sizes it, and applies `ApplyBlinkLoop`
because the result is a certificate, not a position.

Kitty v2's chain ends in a **position**. It piles artifacts on, draws cards, and usually kills a
turn or two later — and sometimes cannot attack at all on the turn it goes off. So the half of the
precedent that transfers is the *apply-side macro* (fix the apply, not the search). The half that
does not is "stop searching, we won": the landing state still has to be evaluated, with combat, by
the ordinary search.

One piece of standing doctrine points the same way. USER 2026-09-08: *"The more important thing is
that we don't have extra mains to search"* — never paper over an expressibility hole with more
searched phases; **fix the APPLY instead**, with `EdfAutoGoOffAfterCasts` named as the precedent.

## 1. Four hypotheses, three of them refuted by measurement

The prior lead in the deck's own ledger was *cast-order permutations over interchangeable free
Equipment* — "717 plans on turn 1, 120 outcome-identical orderings". Three of the four candidate
levers that follow from that reading are dead, and it matters that they are recorded as dead.

| hypothesis | verdict | evidence |
|---|---|---|
| cast-ORDER permutations are the cost | **refuted for the autonomous search** | `OrderingSearchEnabled` is false unless `MTG_SEARCH_ORDER`, `DecisionUnpruned(SearchOrder)` (viewer only) or `WantsCastOrderingSearch` (Dragonstorm only). The 120 orderings were a **viewer-path** measurement; the search never pays them. |
| the identical-copy fold is missing | **refuted** | `FungibleEquipCopyViolated` already folds identical Equipment copies and is NOT gated on `MTG_FOLD_SEARCH_ODO`. |
| wiring the prefix fold to the search walk (`MTG_FOLD_SEARCH_ODO`) is the lever | **refuted as a lever** (but a free, sound collapse) | 12 seeds pooled, arms innermost: **−1.59% units, 12/12 digests identical.** Snow's measured 35.2% does not transfer, because the copy fold above already does that work here. |
| free, pure-upside **equip** digits are enumerated as take/skip | **CONFIRMED — this is the shape** | below |

The lesson worth keeping is the first row: a plan-count measured under `--claude-play` /
`MTG_UNPRUNED` describes the **viewer's** menu, not the search's. The viewer opens
`UnprunedGate::SearchOrder`; the autonomous search does not. Reading one as the other attributes the
deck's cost to a term it never pays.

## 2. Where the cost actually is

Per-decision census (`MTG_TURN_CENSUS`), seed 2002, d3/b250, single-threaded, 45 rows, `contended=0`:

```
u_la_bp_wave        2,128,216   51.6%      <- the lookahead breakpoint wave
u_rollout_step        728,005   17.7%
u_greedy_fallback     710,652   17.2%
u_la_cand             497,918   12.1%
u_fs_bp_node           42,469    1.0%
```

And the branching funnel, same run:

```
sub_entered    36,672,476
sub_passed     36,672,476        <- 0.00% rejected, at EVERY turn
dupc_fold_prefix        0        <- no clause fires at all
space_odo      24,579,438  ->  space_plans 7,272,732  ->  space_dedup 7,242,592  (0.4%)
walk_enter == walk_pass_rules == walk_pass_mana == walk_pass_payable == walk_scored
cand_scored         702,451      <- 52 subset visits per scored candidate
```

Turns 5–7 hold 98.5% of the subset visits. **Nothing is pruned anywhere**, by any predicate, at any
stage — and the actions are free, so even the mana filters pass everything.

The group-size histogram says what the odometer is made of: turn 6 is **457,279 size-1 groups**
against 28,905 size-2 and 10,005 size-3. The odometer on this deck is a product of **binary
take/skip digits**.

`MTG_BRANCH_HEAVY=4` names them. The heaviest decisions are not big-hand go-off frames at all —
`hand=3`:

```
HEAVY rank=1 odo=2048 turn=6 board=17 hand=3 groups=10 plans=791 reenumerated=2
  FUNNEL visits=1151 ... PASSED=1151 rejectPct=0.0
  GROUP size=1 card=Puresteel Paladin    variants=[cast]
  GROUP size=1 card=Dwalin, Weaponmaster variants=[cast]
  GROUP size=1 card=Bone Saw             variants=[cast]
  GROUP size=1 card=Colossus Hammer      variants=[equip:victim=57]
  GROUP size=1 card=Bone Saw             variants=[equip:victim=57]
  GROUP size=3 card=Skateboard           variants=[equip:57 | equip:42 | equip:24]
  GROUP size=1 card=Bone Saw             variants=[equip:victim=57]
  GROUP size=1 card=Bone Saw             variants=[equip:victim=57]
  GROUP size=1 card=Colossus Hammer      variants=[equip:victim=57]
  GROUP size=1 card=Bone Saw             variants=[equip:victim=57]
```

**Seven of ten groups are single-variant equips onto the SAME host**, each its own binary digit:
2⁷ = 128 of the 2048 positions are the powerset of "which of my free equips do I bother to make".
That is the deck's cost, and it is pure waste.

## 3. The dominance argument

For an Equipment whose equip cost is `{0}` right now, whose host is **already on the battlefield**,
and whose attach carries no downside, **taking the attach weakly dominates declining it**:

* **Equipment do not compete for a host.** A creature holds unlimited Equipment, so taking one
  attach never forecloses another. This is what separates it from every resource axis the engine
  already folds — a sac outlet's fodder, a mana source's `{T}`, a splice's spell.
* **Nothing prices an attached Equipment as a cost.** It still counts for metalcraft and for the
  artifact count; and Golem-Skin Gauntlets reads "+1/+0 for each Equipment attached to **it**", so
  piling more onto the same host strictly *raises* the Gauntlets' own bonus.
* **The attach is not spent.** Re-equipping later is legal, and if metalcraft switches off in the
  meantime the attachment is already paid for while an unattached copy becomes dear again. So
  attaching now weakly dominates attaching later and strictly dominates never.

So the whole 2^k subset over k such digits collapses to the single all-taken position. This is the
same shape and the same reasoning as `MTG_SPORE_POP_ALL` (USER: *"there is no benefit to waiting"*),
and like it an **exact collapse, not a heuristic narrowing**.

**It is airtight on this decklist specifically**, because no Equipment in it has a drawback:

| Equipment | effect | drawback? |
|---|---|---|
| Bone Saw, Skateboard | +1/+0 (+haste) | none |
| Cathar's Shield, Accorder's Shield | +0/+3, vigilance | none |
| Kite Shield, Spidersilk Net | +0/+3, +0/+2 reach | none |
| Colossus Hammer | +10/+10, **loses flying** | modelled by no param, and inert — nothing blocks anywhere in this engine, so flying buys no evasion to lose |
| Golem-Skin Gauntlets, Shadowspear, Dragonfire Blade, Deconstruction Hammer | +1/+0 each, +1/+1 trample/lifelink, +2/+2, +1/+1 | none |

### The three carve-outs, enforced rather than argued

`BuildFungibleEquipClasses` sets the house standard here — *"keep it exhaustive rather than argue
reachability"* — so each is a hard gate in `BuildMandatoryFreeEquip`, not a note:

* **(a) The host must ALREADY be on the battlefield.** A host that only a *cast* digit brings in
  makes the forced position **illegal**, which would delete real plans rather than fold them. That
  is exactly the dependency `MTG_EQUIP_PIECE_DEPS` / `SubsetHasStrandedEquip` models, and forcing a
  digit on top of it would fight that guard instead of composing with it.
* **(b) `equip_grants_shroud` anywhere in the frame disarms the collapse entirely.** Equip *targets*
  ("attach to target creature you control"), so a Lightning Greaves granting shroud to its own host
  makes every **later** equip onto that host an illegal target. Forcing digits could then manufacture
  a selection with no legal order at all. Frame-wide rather than per-group, because the illegality is
  created by one digit and suffered by the others.
* **(c) `equip_sacrifices_prior_host`, and any negative bonus**, are excluded outright
  (`equip_power_bonus`, `equip_tough_bonus`, `equip_scale_power_per_equipment`,
  `equip_scale_tough_per_equipment`). `EquipAttachPureUpside` is exhaustive over `CardParams`'
  whole `equip_*` surface as of 2026-10-01; a param added later that can hurt the host must be
  added there.

## 3a. MEASURED: the collapse is sound but near-VACUOUS on this deck

Built, flag off = byte-identical (`test/scenarios.sh` **125 passed / 0 failed**; the `base` arm's
digests and units match the pre-change baseline exactly on 12/12 seeds). With it ON, 12 seeds pooled
arms-innermost: **units move ~0.0%** and every digest is unchanged.

That is not a wiring bug, and the trace proves it rather than assuming it (memory
`digest-equality-can-mean-broken`: a collapse with identical digests is ambiguous between INERT and
NEVER FIRED). `MTG_FREE_EQUIP_DIAG=1`, seed 2002:

```
calls=749932  armed=2667  mandatory_groups=2667
equip_groups_seen=2794592
rej: cost=2599689 (93.0%)  attached=161051 (5.8%)  host=31185 (1.1%)  params=0
disarm_shroud=0
```

**93.0% of equip groups are rejected because the equip cost is not zero**, and only 0.095% of equip
groups qualify as mandatory. Puresteel Paladin's metalcraft (`equip {0}`) is simply not live in most
of the searched subtree — it needs a Puresteel *on the battlefield*, and the heaviest frames do not
have one (it is the group being *cast*). So those 2^k equip subsets are a genuine **affordability
knapsack**, not symmetry, and there was nothing there for a dominance rule to remove.

The lever is kept (default OFF, carve-outs enforced, diag included) because it is correct and will
bite on a board where metalcraft *is* live — but it is not this deck's answer, and it must not be
reported as one.

## 3b. Four levers, four refutations — and why that is the finding

Every candidate collapse was measured on the same 12 seeds, pooled, arms innermost, gating on
`units` per `pooled-ab-needs-arms-innermost`:

| lever | units | play | read |
|---|---|---|---|
| `MTG_FOLD_SEARCH_ODO` | **−1.59%** | 12/12 digests identical | sound and free, but tiny: `FungibleEquipCopyViolated` already does the identical-copy work here. Snow's 35.2% does not transfer |
| `MTG_FREE_EQUIP_MANDATORY` (new) | **~0.0%** | identical | fires on 0.095% of equip groups (above) |
| `MTG_EQUIP_COPY_COLLAPSE` | **+1.3% net** | identical | −17.0% subset visits on seed 2002 but **+985%** on seed 7007 |
| `MTG_SOLVE_CHARGE` W=1/8/16 | **+13.9% / −0.5% / +3.1%** | **MOVES** on 3 seeds (avg moved on 2) | not a win, and not play-neutral |

The `MTG_EQUIP_COPY_COLLAPSE` row is the instructive one, and it needed the *subset-visit* counter
rather than `units` to read at all (a budget is a work CAP, so freed work is re-spent — the
`SNOW_LOOK_COLOR` lesson):

```
seed 2002  subset visits  36,672,476 -> 30,455,448   -17.0%
seed 7007  subset visits     286,660 ->  3,112,234   +985%     (digest SAME both)
```

Removing odometer positions caused a **10.9x increase** in walk work on 7007 for the same play. The
mechanism is that this deck's greedy subset walk **charges nothing against the budget**
(`MTG_SOLVE_CHARGE` is a per-deck opt-in and KittyEquipment has not opted in), so a collapse that
frees budget lets the search escalate into far more *unpriced* walking. A collapse can therefore
make an uncharged deck dearer without the budget noticing — which is a general trap worth naming,
not a kitty quirk.

## 3c. So this is NOT a branching-redundancy problem

The repo's own termination test for this kind of work (CLAUDE.md, "COLLAPSE WASTED SEARCH
UNCONDITIONALLY") is: *"Once all of those are gone the expectation would be that whatever remains is
not branching related"*, at which point *"allocation/evaluation profiling is the right next move,
and not before."* Four independent pieces of evidence say this deck is already at that point:

* the shared-resource funnel rejects **0.00% of 36,672,476** subset visits, at every turn — there is
  no redundancy left for a predicate to find;
* `space_dedup` removes **0.4%** (7,272,732 → 7,242,592), so the post-apply states really are
  distinct;
* the group-size histogram is almost entirely **size-1** (457,279 vs 28,905 at turn 6), i.e. each
  digit is a genuine binary choice rather than a symmetric family;
* **93%** of equip groups carry a real mana cost, so the subsets are an affordability knapsack over
  ~10 cheap actions with 5–6 mana — legitimately different lines, not permutations.

### And depth is not the escape either

12 seeds, b250: d1 **5.67 s/game** (avg 5.2727), d2 16.35 s (5.1818), d3 18.57 s (5.0909). **10 of 11
seeds have identical `avg` at d1 and d3** — the whole quality gap is seed 4004 alone (8 → 7 → 6). So
d1 is 3.3x cheaper for almost no measured quality, which by the "depth economics are a **settings**
question" doctrine would be the cheap answer.

It does not reach the gate, because the worst games are slow at **every** depth:

```
seed 1001   d1 19.8s   d2 26.3s   d3 26.6s
seed 2002   d1 31.8s   d2 82.7s   d3 78.8s
```

At d1 there is almost no lookahead, so ~20–30 s there is **per-frame enumeration volume at the
root**, not search depth and not branching redundancy. That is the residual, and it is the per-node
class the doctrine points at.

## 4. What this does NOT address

* **The free-CAST axis.** The user's original framing — dump every 0-mana Equipment to draw — is a
  *separate* collapse, and the measurement says it is not where this deck's search cost is (the
  heaviest frames have `hand=3`). Its own dominance argument is sound but narrower: with a draw
  watcher on board a free Equipment cast is **hand-neutral** (−1 card, +1 draw) and board-positive,
  so the full dump dominates every proper subset; *without* a watcher it is hand-negative and must
  stay searched. Two couplings would have to be documented before shipping it: dumping reduces
  cleanup discards and so starves Cid's graveyard fuel, and its soundness depends on the engine's
  draw-on-empty-library **skip** (an approved deferral) — if CR 104.3c is ever modelled, dumping
  could deck you and the dominance fails.
* **The viewer's plan explosion.** `HumanEnumSaturated` is triple-blocked on this deck:
  `!HumanPlayActive()` excludes the search by design, `floating < MTG_HUMAN_SAT_FLOOR` (24) never
  clears because this deck's go-off runs on **zero** mana, and `any_credit` bails on metalcraft/Cid
  anyway. Every gate is denominated in a *ritual* combo's currency. The clean fix is a
  **zero-demand** branch: the predicate's own soundness condition ("the pool can pay the maximal
  demand simultaneously") is satisfied trivially and exactly when the demand is **zero**, with no
  floor and no credit question — a cost reducer cannot make a 0-cost spell unaffordable. That is a
  strictly more general, still exact predicate, and it is the right fix for the viewer valve
  deleting attach-equipment actions (ledger open item 2) — better than the ranking change that item
  currently proposes, because it shrinks the space instead of re-ordering a space that is too big.
* **The `u_la_bp_wave` 51.6%** is not attacked directly. It is expected to fall *proportionally*,
  since the wave fans out per base plan and this collapse cuts base plans — but that is a prediction
  to be checked against `units`, not a claim.

## 5. Open: an exact emission twin at the root

The root-frame dump (`MTG_PLAN_DUMP`, seed 2002 turn 4) shows 44 plans over **16** distinct
post-apply states — 28 of 44 reach a board a sibling already reached — and among them pairs that are
identical in every displayed field:

```
[ 12] PlayLand:Plains + Cast:Skateboard + Equip:Bone Saw + Equip:Skateboard bp_choice=rank 0 bp_at=0  [STATE-DUP of #0]
[ 14] PlayLand:Plains + Cast:Skateboard + Equip:Bone Saw + Equip:Skateboard bp_choice=rank 0 bp_at=0  [STATE-DUP of #0]
```

The header says `positions=1` and `W=2`, which accounts for two variants per plan, not four. So
either the wave runs at two sites per decision or a field not shown differs. **Not yet diagnosed,
and deliberately not claimed as a defect** — `duplicate-state-is-not-removable-work` is the standing
caution that a duplicate discovered *after* an apply is already paid for. What would make it real is
a duplicate at **emission**, which is what the identical descriptors suggest and what a `bp_site`
column would settle.
