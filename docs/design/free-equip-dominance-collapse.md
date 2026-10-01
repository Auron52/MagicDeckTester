# The free-action dominance collapse, and why KittyEquipment v2 is intractable

> ## ⚠ SUPERSEDED IN PART — 2026-10-01, later the same day
>
> **§3a, §3b and especially §3c below are WRONG, and they are kept only because the reason they
> were wrong is the useful part.** §3c concluded *"this is NOT a branching-redundancy problem …
> the residual is per-node"*. It **is** a branching-redundancy problem. Removing the redundancy
> properly is **−54% units on 40 held-out seeds** at neutral-to-better play.
>
> `MTG_FREE_EQUIP_MANDATORY` has been **RETIRED and deleted** (it was never defaulted on). It
> measured ~0% for two reasons, both of them properties of the instrument rather than of the deck:
>
> 1. **A reject predicate is not a collapse.** It refused a dominated group's "skip" position, but
>    the odometer still *walks* every position it rejects — the radix is `groups[g].size() + 1`, and
>    only removing the digit removes work. The repo already had a mechanism with the right shape:
>    the **AUTO-EQUIP collapse**, which `groups.erase()`s the group and pushes the action into
>    `auto_sel` so it rides every subset. I built a weaker twin of something that existed.
> 2. **It read a stale cost stamp.** The Equip candidate block bakes
>    `a.cost.generic = EquipCostGenericNow(state)` — the artifact count at *enumeration*. On the
>    go-off turn metalcraft has not flipped yet, so every Colossus Hammer equip is stamped `{8}`.
>    The "93% of equip groups rejected because the cost is not zero" in §3a was measuring that
>    stamp, not the price paid. `SameTurnMetalcraftEquipCredit` already undoes the same stamp at the
>    affordability gate; I did not check for the precedent before concluding.
>
> It also carried a latent correctness trap that measurement never reached: `IsManaSideAction` is
> `ritual_float > 0 || rock_mana > 0`, so an Equip group is **always** on the payoff side of the
> two-stage split. Stage A judges a vector holding only mana-side digits, so a mandatory group read
> `full[g] == 0` on every mana line, the predicate rejected all of them, and `mlines.empty()` made
> the split path emit **no plans at all**. `copy_class` and `equip_deps` both carry explicit
> straddle guards for exactly this; the comment I wrote argued mine needed none.
>
> **What replaced it: §6.**

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

---

## 6. What actually collapses it (2026-10-01, later the same day)

Two levers, both **DEFAULT OFF**, both lever slots so a pooled A/B pins either arm in one batch.
Both work by the only mechanism that removes odometer work — **erasing the group** and pushing the
action into `auto_sel`, where it rides every enumerated subset.

### 6a. `MTG_FREE_CAST_HOIST` — the `{0}`-cast axis. A clean win.

This is the lever for the USER's original framing: *"It might require a kind of jump of some sort to
evaluate with all of the 0-mana cards out."* The v2 list holds **14** of them — Cathar's Shield ×4,
Bone Saw ×4, Kite Shield, Accorder's Shield, Spidersilk Net, … — and hand casts group by hand
**slot**, so a mid-game hand powersets take/skip over every one independently.

**It is exactly mana-neutral**, which is what separates it from 6b: a `{0}` cast contributes nothing
to the odometer's mana term, so forcing it into every subset mis-prices *nothing*. There is no
optimism to bound — only the dominance claim, and for an Equipment every axis the engine models is
monotone upward. The card data says so itself (Bone Saw's note in `cards.json`): *"a free artifact
toward Puresteel's 3, a free Sram draw on cast, a free Puresteel draw on enter, and one more
Equipment for a Golem-Skin Gauntlets host to count."* Add Dwalin's hone counters, which accrue while
**unattached** and cash in on a later equip — `SpellEffects.h` calls that *"what makes the hone axis
monotone"* — and Sigarda's Aid, under which the entrant enters **attached**, bypassing the equip cost
entirely. Nothing here prices a battlefield permanent as a cost, an Equipment has no body so can
never be forced to attack, and casting *reduces* hand size so it cannot cause a cleanup discard.

Carve-outs enforced rather than argued: `equip_grants_shroud` (with Sigarda's Aid the entrant
auto-attaches, and a shrouded host makes every **later** equip an illegal target — the same hazard
§3 disarms frame-wide, reached by another route), `equip_sacrifices_prior_host`, any negative bonus,
`{X}` costs, and multi-variant groups (a mode choice is a real decision; this argues about *whether*
to cast, not *how*).

| | v2 train (20 seeds) | v2 **held-out** (40 seeds) | v1 — **the suite deck** |
|---|---|---|---|
| units | 0.5868 | **0.4619** (−53.8%) | **1.0000** |
| ms | 0.7180 | **0.4804** | — |
| avg turn-to-win | 4.90 → 4.85 | 4.8250 → **4.8000** | unchanged |
| games worse / better | 0 / 1 | **0 / 1** | 0 / 0 |
| digests | 3/20 same | 12/40 same | **4/4 byte-identical** |

Better on every axis, and **byte-identical on the regression suite's kitty case** (v1 holds no
`{0}`-cost equipment, so the lever is structurally inert there) — so adopting it moves no ground
truth.

**It required a fix to `BuildEquipPieceDeps`.** This is the first collapse to erase a **cast** group,
and `cast_groups_of` returns mask `0` for a hoisted one, stamping the dependent equip
`{required, groups = 0}` — "needs a piece no group can ever cast", a permanently dead digit. That is
the `jittemode=2` enumerated-but-unplayable class recorded in that function's own comment and in
`enumerated-but-unplayable-activations.md`. The builder now takes `auto_sel` and treats a force-cast
piece exactly like one already on the battlefield.

### 6b. `MTG_METALCRAFT_EQUIP_HOIST` — the stale stamp. A trade, not a win.

Extends the AUTO-EQUIP collapse across the **same-turn metalcraft flip**, testing whether the frame's
own casts will make an equip free instead of whether it is free at the enumeration stamp. This is
`SameTurnMetalcraftEquipCredit`'s correction applied one gate earlier, at the enumeration *shape*.
It fires hard — **2,543,978 hoisted groups across 617,403 frames** on one v2 game.

Unlike 6a it **is** optimistic, and the bound is exact: a hoisted equip rides every subset without
contributing to the mana term, so in a subset that does *not* flip metalcraft it is scored free while
really costing its printed generic. The **play** is never wrong — casts are applied before the
trailing equip pass in both worlds, so the equip's own recompute pays out of what the casts left or
declines outright, making the realised line exactly the un-equipped plan. The error is confined to
that plan's **score**.

| | v2 (20 seeds, b250) | v2 churn control (b4000) | v1 — the suite deck |
|---|---|---|---|
| units | 0.8812 | — (budget-bound) | 0.7277 |
| avg | 4.90 → 4.95 | **4.2222 → 4.2222** | +1.0 turn / 70 games |
| digests | 11/20 same | 1/9 same | 0/4 same |

**The churn control is the method point.** At the 250 ms operating budget seed 4004 lost a turn
(6→7). At **16x budget that game is equal on both arms**, and across all 9 formerly-divergent seeds
the avg is identical with zero worse and zero better. So the turn loss was *freed budget re-spent
differently*, not the collapse mis-scoring — exactly what CLAUDE.md's collapse doctrine predicts.
Re-run divergent seeds at a large budget before calling a collapse lossy.

**`FrameHasDoubleStrikeSource` is the carve-out, and the USER supplied it.** *"There is no
double-strike in the deck, so we can just choose one creature and put all equipment on it"* names the
hoist's own precondition: forcing a group's best variant is dominance only while the choice of
**host** does not change the rider's value, and a double striker doubles it. Un-gated, the hoist cost
v1 — Kor Duelist + Balan — **3 turns in 70 games**; gated, **1**. v2 has no double-strike source, so
it is affected not at all (every v2 digest and unit count is unchanged by the gate).

So 6b stays OFF pending the user's call. v1 and v2 share `EquipmentProvider`, so a provider opt-in
cannot separate them; the honest options are the double-strike gate as it stands (1 turn / 70 on v1,
and a GT rebaseline on all four v1 cells), or a sharper gate, or leaving it as an env/manifest lever
for v2 only.

### 6c. Three of the requests were already implemented

Worth recording, because the right answer was to *check* rather than build:

* **"we can just choose one creature and put all equipment on it"** — `ConsolidatesEquips()`
  (`EquipmentProvider`), the 2026-08-14 doctrine. The rider set already collapses to
  {top ds-potential host, Kemba}, or a single best host when neither exists.
* **"if we have haste prioritize those with summoning sickness, if not put it on one that can
  attack"** — the `ranked` (haste hosts: `fresh && !haste`) / `ranked_rider` split, plus
  `UnsickEquipHostEnabled`, whose comment quotes the user's own 2026-08-22 phrasing verbatim.
* **"no point equipping anything that doesn't add power until they are free"** — a paid equip is
  emitted only via `ranked` (grants haste) or `ranked_rider` (`rd > 0`). A `+0/+3` shield onto a bare
  creature reads `rd == 0` and is never offered. And the exception is handled correctly:
  `EquipAttachDeltaFor` sums `equip_scale_power_per_equipment` over equipment **already on the
  host**, so the same shield onto a **Golem-Skin Gauntlets** host reads `+1` and *is* offered —
  which is precisely why piling matters in this deck.

### Verification

Unit tests **2,641,296 / 2,641,296**. Scenarios **125/125** at the default *and* **125/125** with
both flags forced on. Default-OFF builds byte-identical to the pre-change binary on 5/5 v2 digests,
before and after the `FREE_EQUIP_MANDATORY` deletion.

## 7. The 0-power ruling, and the disarm the census found instead (2026-10-01, later still)

**USER:** *"we want to equip those 0 power equipment only when they are free or I suppose if we have
Golem-Skin Gauntlets and nothing else to do with the mana. Overall they are pretty poor to use for
anything but draws and free equips in goldfishing."*

v2 plays **sixteen** such cards — Cathar's Shield x4, Accorder's Shield x4, Kite Shield x4,
Spidersilk Net x4 — all `{0}` to cast with equip `{2}`/`{3}`.

### 7a. The ruling was already in force, and here is the frame evidence

§6c argued this from the code; this is the measurement. Across the **10 heaviest decisions** of a v2
game (`MTG_BRANCH_SHAPE` + `MTG_BRANCH_HEAVY=10`, seed 13001), the option groups are:

```
30 Colossus Hammer      [cast]      30 Colossus Hammer      [equip]
 9 Golem-Skin Gauntlets  [cast]     10 Golem-Skin Gauntlets  [equip]
 9 Dwalin, Weaponmaster  [cast]
```

**Zero shield casts and zero shield equips.** The casts are gone because `MTG_FREE_CAST_HOIST`
force-takes them (they are free, and they are the "draws" half of the user's sentence: an artifact
toward metalcraft plus a Sram/Puresteel trigger). The equips are gone because `rd == 0`.

### 7b. `MTG_ZERO_POWER_EQUIP_FREE_ONLY` — built, measured, REFUTED

The one case the engine *does* offer is a **Golem-Skin Gauntlets host**, where `EquipAttachDeltaFor`
correctly prices the attach at +1 power per Gauntlets. The lever withholds exactly that pair when it
costs mana (carve-outs: free at the stamp, freeable by the frame's own casts via
`MetalcraftWillFreeEquips`, own power bonus or scaler, haste, lifelink, Jitte charges, re-host
sacrifice, hone counters on the equipment, a Kemba host, or an attach that flips double strike).

| | units | ms | avg | digests | worse / better |
|---|---|---|---|---|---|
| v2, held-out 40 seeds (24001-24040) | **0.9957** | 0.9679 | 4.8250 → 4.8500 | 38/40 identical | **1 / 0** |
| v1 — THE SUITE DECK, 20 seeds | 1.0000 | 1.0075 | unchanged | **20/20 identical** | 0 / 0 |

**That near-null is noise, not a small win.** The lever touched **2 of 40 games** (7.3% of units) and
on *those two* it was worse on both axes: units x1.0504, and seed 24010 finished a turn **later**
(5 → 6). The **churn control** settles which kind of loss that is — re-run at **16x budget the zp arm
still finishes on turn 6**, so the line it removed was real, not freed budget re-spent.

**The finding is that the user's own exception is load-bearing.** v2 plays **three** Gauntlets, so on
a loaded host every further Equipment is **+3** power and "poor" stops being true. The `rd > 0` rule
already implements the rest of the ruling, so there was nothing left to collapse here. Default OFF;
the slot is kept as the record of the measurement so it is not rebuilt.

**And the sharper diagnostic point:** the 7.9M-unit tail game carrying **38%** of the sample's units
was never touched. A frame census says which decision is *heaviest*, not where the volume *is*.

### 7c. Where the volume actually is (`MTG_TURN_CENSUS`, seed 13019 — the worst game)

| | |
|---|---|
| census rows (real-play decision roots) | **27**, `contended=0` |
| their units | **7,561,008 — all of them** |
| odometer positions | **313,288,102** |
| subsets reaching `consider()` | **30,332,517** |
| plans emitted | 3,517,009 (distinct 3,258,848) |
| rollout calls | 1,667,488 |

Two things this settles. First, **the missing apparatus is not the explanation**: every unit is
attributed to a real-play root, so v2's absent keep table and value leaf are not hiding mulligan
trial games (the Snow failure mode). Second, the cost is a **tail** — in a 20-seed sample, **5 games
carry 88.8% of all units and two carry 76%**, so any lever must be priced on those games.

### 7d. A hypothesis of mine, refuted before it was built

`metalcraft_bound` adds *the summed generic of every Equip candidate* to the scalar prune bound (an
upper bound on what `SameTurnMetalcraftEquipCredit` can forgive), which on a go-off frame is +24 or
more and looked like it must be nullifying mana pruning frame-wide. **Measured:** turning the whole
credit family off (`MTG_METALCRAFT_CREDIT=0`) is **0.9440** units over 20 seeds — so the tightening
it suggested (credit only the *selected* equips, per position) could not be worth more than ~5%, and
8 of 20 games were byte-identical with it off at all. Not built.

### 7e. THE DISARM: Dwalin's hone counters silently disabled `MTG_EQUIP_COPY_COLLAPSE`

The heaviest frames of the heavy games are made of **interchangeable copies**:

```
HEAVY rank=1 odo=4096 turn=4 groups=12   (board: Sram, Bone Saw x3, Accorder's Shield, Gauntlets)
  [cast]  Dwalin | Shadowspear | Colossus Hammer | Shadowspear
  [equip:victim=58]  Bone Saw x3 | Accorder's Shield | Kite Shield | Shadowspear x2 | Colossus Hammer
```

`BuildFungibleEquipClasses` exists to fold exactly this, and the branch-shape instrument prices the
full interchangeable-copy fold at **2.349x of the whole search's odometer**. Yet the
`MTG_EQUIP_COPY_COLLAPSE` arm had measured **0.990x units with digests identical 20/20** — which I
had read as "the symmetry is absent".

It was not absent. The class builder's eligibility list refused any source carrying a counter, and
**Dwalin, Weaponmaster puts a hone counter on every Equipment**
(`hone_counters_on_enter_or_attack`). From the turn Dwalin lands, every equipment permanent on the
board carries a nonzero count and the builder returned **0 classes for the rest of the game**.

The refusal proved a narrower thing than it implemented — the same defect, and the same fix, as the
sibling act-source fold's spore/quest clause (`MTG_FOLD_COUNTER_SOURCES`, written for Fungus): *"a
Bone Saw with 2 hone counters and one with 3 are not interchangeable"* is an argument about
**unequal** counts. Equal counts are interchangeable (the counter's only semantic reader is
`EquipBonusFor`'s `pw += e.hone_counters`), so the count belongs **in the signature**, exactly as
`tapped` and `entered_this_turn` already are. One `snprintf` field.

| after the fix | units | ms | avg | digests | worse / better |
|---|---|---|---|---|---|
| v2, held-out 40 seeds | **0.9670** | **0.9384** | unchanged | **40/40 identical** | 0 / 0 |
| v1 — THE SUITE DECK, 20 seeds | 0.9942 | 1.0567 | unchanged | **20/20 identical** | 0 / 0 |

Per-game on the tail: s24004 **0.772**, s24006 **0.830**, s24005 (the 7.9M-unit game) 0.962.

No flag of its own: the whole collapse is already behind `MTG_EQUIP_COPY_COLLAPSE` (default OFF) and
already documented as digest-moving in general, so widening what it catches cannot touch a default
run. **It is now worth adopting on its own merits** — play-identical on both decks, cheaper on both.

### 7f. The lesson worth carrying

A default-OFF collapse that measures ~0 has **two** possible causes, and they are not distinguishable
from the ratio: the symmetry is absent, or **the mechanism never armed**. §3c got this wrong once
already (a reject predicate that walked what it rejected); this is the same mistake wearing a
different hat — an eligibility guard that refused the very boards the symmetry lives on. Before
concluding "absent", **count the classes the builder actually found**.

### Verification (§7)

Unit tests **2,641,296 / 2,641,296** (359/359 cases). Scenarios **125/125**. Default-OFF builds
byte-identical to the pre-change binary on **5/5** v2 digests *and* units, re-checked after each of
the two changes.

### 7g. `MTG_EQUIP_COPY_SKIP` — the radix cut, and what it proves about the walk

The collapse adopted in §7e is still a **reject predicate**: the odometer walks every position it
refuses, so `space_odo` does not move and the instrument still prices the full interchangeable-copy
fold at **2.378x of the whole search's odometer**. This lever closes that gap *without* the
count-valued-group surgery first proposed (merge N groups into one, `2^N -> N+1`, which needs a
multi-action digit and touches ~10 hot read sites of `groups[g][choice[g]-1]` across both twins).

**The jump, and why it is exact.** Canonical form = a class's digits are non-increasing in group
order, so a violation is a pair `(prev, g)` with `choice[prev] < choice[g]`. Every position until
digit `prev` next *changes* holds both digits fixed and therefore violates the same pair, so raising
`prev` to `choice[g]` and zeroing below it passes over exactly those. `prev` is the **last** class
member before `g` — the largest stride below `g`, hence the furthest provably safe jump; a
higher-indexed digit would pass over positions in which `prev` IS raised, some of them canonical.

| | digests | units per game | ms |
|---|---|---|---|
| v2, held-out 40 seeds | **identical 40/40** | **identical 40/40** | 1.0067 |
| v1 — THE SUITE DECK, 20 | **identical 20/20** | **identical 20/20** | 0.9982 |
| smoke | 104/104 PASS | configs changed **0** | play-changed **0** |

The base arm also reproduced §7e's arm **exactly** 40/40 and 20/20, so the predicate reordering the
patch required changed nothing.

**It removes ~58% of this deck's odometer positions and no measurable wall, and those two facts
together are the finding.** A position the predicate already refuses costs only its own predicate
walk and its carry — so *walking* it was never the expense. What §7e's 0.967x units actually buys is
not walking the duplicates but **not evaluating** them. Same family as
`duplicate-state-is-not-removable-work`: price the loop you mean to change.

**Adopted default-ON** under the user's collapse doctrine (*"wasted work is wasted work regardless of
the situation... judge a collapse on work removed and soundness, never on whether wall fell"*), with
the wall reported rather than used as a verdict. One safety property worth naming: **units identical
per game** means budget units are charged per `consider`/rollout and not per position, so unlike a
memo cap this cannot shift play under budget pressure.

**And it closes the odometer axis for this deck.** The remaining priced 2.378x is now *walked* at
`N+1` per equip class rather than `2^N`; what is left on the hand-CAST side goes through
`equiv_tag` / `FoldPrefixViolated`, whose own digit test is gated behind the reserved
`MTG_FOLD_SEARCH_ODO` decision and measured at -0.8%. Given this result — walking is free, evaluating
is not — the next lever should target **`consider()` call volume** (30.3M calls for 3.5M plans in one
game), not the odometer.
