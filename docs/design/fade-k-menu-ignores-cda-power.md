# The Saproling Burst k-menu reads a `*/*` creature as ZERO POWER

**Status: FOUND 2026-10-15, NOT YET FIXED.** Found while answering the user's question: *"One thing
we should also be checking is whether The Mycotyrant is handled in the heuristics. Particularly I am
thinking for Saproling Burst it's damage scaling should be considered in the current and future
turns."* The answer is **no, in two separate ways**, and the user's framing names the second one
exactly.

This is a defect in a **heuristic's board read**, not in the rules engine. Actual combat damage and
the main board evaluation read the characteristic-defining ability correctly. Only the Saproling
Burst activation-count menu does not — and that menu **restricts what the search may consider**, so
what it misreads it also makes unreachable.

## The two defects

`ReadFadeBoard` (`src/ai/TurnSolver.cpp`) builds the board read that `FadeKLandmarks` prices every
candidate activation count `k` against. Its per-permanent power term is:

```cpp
const int pw = std::max(0, p.EffectivePower() + pb);
if (CanAttackFull(p, state.battlefield, me, &bs.haste)) { r.ready_power += pw; ++r.ready_attackers; }
```

and `Permanent::EffectivePower()` (`src/core/Permanent.h:687`) is, in full:

```cpp
int p = card.m_power.value_or(0) + temp_power_bonus;
for (const Counter& c : counters) { /* +1/+1 and -1/-1 counters only */ }
```

### 1. STATIC MIS-READ: the card counts as 0 power

The Mycotyrant's printed P/T is `*/*`, modelled as **power 0, toughness 0** plus
`pt_equals_creatures_you_control_subtypes: [Fungus, Saproling]`. The CDA is applied through
`DynamicBasePower` / `DynamicBaseToughness` (`src/core/SpellEffects.h:15930`, `:15949`), **not**
through `EffectivePower()` — a deliberate performance decision recorded on the Saproling Burst card
note (*"threading a battlefield lookup into EffectivePower to serve one card would be paid by every
permanent of every deck on every walk"*).

`DynamicBasePower` has call sites in combat (`src/ai/Combat.cpp:25`, `:159`), in the solver's attack
projection (`src/ai/TurnSolver.cpp:8216`, `:8361`), in board/card evaluation
(`src/ai/DecisionProviders.cpp:5010`, `:17356`, `:17646`) and in display (`src/main.cpp:2710`).

**It has none in `ReadFadeBoard`.** So `r.ready_power` and `r.next_power` count The Mycotyrant as
contributing **zero**, for a creature its own card note describes as *"routinely an 8/8 or larger"*
in this deck.

### 2. SCALING NOT MODELLED: the term the user asked about

This is the sharper half. Every objective in `FadeKLandmarks` prices `k` as *bodies x body size*:

```cpp
auto bodies    = [&](int k) { return own + k * n; };
auto size_now  = [&](int k) { return C - k * c + A_p; };        // this turn
auto size_next = [&](int k) { return C - k * c - 1 + A_p; };    // after the upkeep tick
```

Each activation mints `n` Saprolings. Under a Mycotyrant **each of those bodies permanently raises
the Mycotyrant's own power by one**, for as long as it lives. There is no term for this anywhere in
`k_pow_now`, `k_pow_next`, `k_lifetime` or `dmg_now`.

**And it does not decay.** The tokens' own size falls by one every upkeep (`size_next`), which is
what makes the current `k_lifetime` stream converge to the user's hand-derived "2 or 3". The
Mycotyrant's bonus is **per body, flat, and permanent** until the Burst expires and takes its tokens
with it. So the missing lifetime term is approximately

```
bodies(k) * (R - 1)          where R = C - k*c  (counters left)
```

— **linear in bodies and independent of body size**, which is precisely the shape that shifts the
argmax toward WIDE activations. The existing objective penalises wide-and-small because small bodies
deal little; with a Mycotyrant out, small bodies still each add a full point of permanent power to an
8/8+ attacker. The user's *"current and future turns"* is the whole of it: the current turn via
`ready_power` (defect 1), the future turns via this stream term (defect 2).

## Why it bites rather than merely being imprecise

`MTG_FADE_K_WINDOW` is **DEFAULT ON** (`src/ai/TurnSolver.cpp:~27400`) and replaces the full `k`
ladder with the landmark menu. A `k` absent from the menu is not merely deprioritised — it is never
emitted as an `Action`, so it is **unfindable at any budget**, including `--budget-ms 0`. The menu's
adoption rests on a measured A/B (*"-0.00417 turns at 2.04x"* at d1/b3, *"-0.00400 at t = -3.24,
1.50x"* at d5/b20, `docs/design/fade-k-axis-landmarks.md`), and that A/B is not in question — but it
was run on lists where the board read was accurate. Its soundness argument is explicitly that a
landmark is only suppressed when the base menu *provably* wins; a board read that scores an 8/8 as a
0/0 breaks that proof.

The menu's own comment states the one direction it must not err in: *"an over-reject would delete a
line the deck can really play."* Both defects err in exactly that direction.

## What it cost, and why it matters NOW

The Mycotyrant **won round 12's twelve-arm add-check in BOTH formats** — std **-0.0465**, 2hg
**-0.0500**, beating the runner-up (`a_thallid`) by **-0.0105 +- 0.0049, t = -2.14** — and it was
carried in the list through rounds 14, 15 and 16. It is `{1}{B}{G}`, so it was dropped as
**collateral when black was dropped** in the rounds 17-21 archetype change, never on its own merits.

Every one of those measurements had `Saproling Burst 4` in the list, so every one of them priced the
Burst's `k` with the Mycotyrant read as a 0-power creature. **The best-measured addition in the whole
screen was measured by a heuristic that cannot see most of its power**, which makes `-0.0465` a
FLOOR rather than an estimate.

That is why `v_rule_myco1` is in round 31 (`logs/fungus_screen/build_spec_r31.py`): the user reopened
black, and testing black only through Deathspore Thallid — whose payoff the apparatus scores at
exactly zero — tests black through its weakest card. A win for that arm is real; a loss is
inconclusive until this is fixed.

## WHICH PIECE CARRIES THE WEIGHT (worked out 2026-10-15, after implementing both)

I expected the scaling term to be the important half, because it is the one the user named. The
arithmetic says otherwise, and it is worth recording so nobody re-derives it.

**The scaling term is narrow.** Taking a Fading 7 at `C=7, c=1, n=1, own=0, A_p=0`, the lifetime
objective with and without the CDA term:

| k | tokens only | + CDA term |
|---|-------------|------------|
| 1 | 15          | 20         |
| 2 | **20**      | **28**     |
| 3 | 18          | 27         |
| 4 | 12          | 20         |

The argmax stays at `k=2`. The token stream is **quadratic** in the counters left (`~R^2/2` per
body) while the CDA gain is **linear** (`R-1` per body), so the CDA term is worth roughly `2/R` of
the objective — about 25-30% at R=7, which shifts the *value* without moving the *argmax*.

It does move under a Doubling Season, where the counter pool doubles to `C=14` and `n=2`:
`k=4` and `k=5` tie at 360 on tokens alone (the argmax takes the smaller), and the CDA term breaks
the tie the other way, 432 vs 440, moving the menu to `k=5`. So the term fires mainly **when a
Season is out** — relevant, since the list now runs Season 2 under the user's ruling, but not
common.

Measured: 200 games on the `v_rule_myco1` list, `MTG_FADE_K_CDA_SCALING` on vs off, gave
**identical** 4.8500 avg. That is consistent with the arithmetic above (rare arming: a Mycotyrant
resolved, a Burst landing, *and* an argmax that actually shifts), not with the term being broken.

**The static mis-read is the heavy half**, and it reaches further than the menu. `r.ready_power` and
`r.next_power` feed `dmg_now` / `dmg_next` and their conservative `_lo` twins, which are the KILL
projections that decide whether a landmark is **suppressed** because the base menu already wins.
Reading our own board as 8-12 power weaker than it is therefore distorts suppression as well as
selection. Correcting it is not an over-credit — it replaces an under-credit with the truth — but it
does mean the static fix changes play on more nodes than the scaling term does, which is why the
pre-fix-vs-fixed comparison is the measurement that matters.

## MEASURED: THE FIX IS LATENT, AND THE REASON IS NOT THE HEURISTIC

Pre-fix and post-fix binaries on the `v_rule_myco1` list, same deck, same seeds, same profile,
d7/b1000, 2000 games per format:

| format | pre-fix | fixed |
|--------|---------|-------|
| std    | 4.8760  | 4.8760 |
| 2hg    | 5.1560  | 5.1560 |

Identical to four decimals in both formats: **not one game of 8,000 changed.** Per
[[digest-equality-can-mean-broken]] that must not be accepted as "clean" without separating "latent"
from "unwired", so `test/tools/fungus_census/mycotyrant_arming_census.py` measures the arming
condition directly (800 games, seed 1290000):

```
  The Mycotyrant reached the battlefield            76  (9.5%)
  ...with a Saproling Burst out the same turn       11  (1.4%)   <- UPPER BOUND
  ...and a Burst LANDED while it was out             9  (1.1%)   <- the `base` menu's rate
  first turn on the battlefield: T2:3, T3:25, T4:29, T5:17, T6:2
```

**The defect was armed in ~1% of games.** A null over 2000 games is the expected observation, not a
suspicious one: roughly 22 games armed, and within those the argmax only moves when the counter pool
is wide (the Season case above). So the fix is correct and latent, and -- the part that matters for
the screen's integrity -- **the heuristic was never distorting rounds 12-16 or 27-31 at a scale that
could change a verdict.**

### The real finding is the copy count, not the bug

The Mycotyrant reaches the battlefield in **9.5%** of games. At one copy in sixty, ~17% of games have
drawn it by turn 4, so it is being CAST in most games where it is seen -- the binding constraint is
simply that the deck almost never has it. Whatever it is worth, it is worth that in under a tenth of
games, which means its round-12 add-check win of -0.0465 implies a large per-game effect. That
argues toward a second copy rather than away from it.

The counter-argument is the legend rule (CR 704.5j, `EnforceLegendRule` live): a second copy is dead
on the battlefield and only pays by FINDING the first sooner. The screen already has a measured
precedent for exactly that shape -- **Shroofus Sproutsire #2 was a null** (+0.0010 in round 28,
+0.0023 in round 29) for this reason. So "1 vs 2" is a real question with evidence on both sides and
is measured by round 31's `v_rule_myco1` / `v_rule_myco2` pair rather than argued.

## The fix, in two pieces with different adoption bars

1. **The static mis-read is a BUG, not a heuristic choice.** A board read whose stated job is
   *"power that can attack THIS turn, lord/anthem folded in"* returning 0 for an 8/8 is a modelling
   error in an estimator. Add the CDA at the one site:
   `p.EffectivePower() + DynamicBasePower(*def, state, me)`, guarded so decks without a CDA card
   stay byte-identical (the `pt_equals_*` vector is empty for every other card, and
   `ReadFadeBoard` already does a definition lookup per permanent, so this is not a new walk).
2. **The scaling term is a genuine heuristic design question** and earns its place by measurement.
   Add a per-body, non-decaying term to `k_lifetime` / `dmg_now` for each
   `pt_equals_creatures_you_control_subtypes` creature we control whose subtype list intersects
   `fade_token_subtypes`, and A/B it. Note it must count only bodies that MATCH the CDA's subtype
   filter: the Burst's tokens are Saprolings (they count), while the Mycotyrant's own end-step
   tokens are **Fungus** (they also count, but no Saproling sac outlet can eat them).

Both are Fungus-only in effect but live in shared code, so the byte-identity check across the
regression suite is the gate, and any play change for Fungus needs GT regeneration.

## What is NOT affected

* **Actual combat damage is correct.** `Combat.cpp` adds `DynamicBasePower`, so a Mycotyrant that
  attacks deals its true power.
* **The main board/card evaluation is correct** (`DecisionProviders.cpp:5010` and the two attack
  projections).
* **Rounds 27-31 of the Fungus screen are unaffected in their current arms except the new
  `v_rule_myco1`**: The Mycotyrant is at 0 copies in every other arm, and the defect is inert
  without a `pt_equals_*` creature on the battlefield. So this does not invalidate the Crossroads,
  Season, Shroofus, Beastmaster or Deathspore readings.
* One thing I noticed but did **not** verify: `ReadFadeBoard` adds `pb` (the lord/anthem bonus
  computed for a fresh *Saproling token*) to **every** permanent's power in that loop, not only to
  ones the lord actually buffs. In this deck nearly every creature is a Fungus or Saproling so it
  may be deliberate, but it is worth a look while the file is open.
