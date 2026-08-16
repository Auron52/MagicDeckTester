# Tight mana bounds for pruning — what is SHIPPED, what is BROKEN, and the order to fix it

Status: the flow oracle is DONE — every bail clause is gone and 12 of 12 suite decks bail 0%. The
emit ceiling is sound and always finite. What remains is TIGHTNESS, and §5 measures exactly how much
is on the table. Direction set by the user 2026-08-16: *"we should be looking for a relatively tight
bound that is not highly computationally expensive and using that to prune. Dropping individual cards
from consideration based on these is another way we can prune many plans."* Self-contained.

| # | change | commit | measured effect |
|---|---|---|---|
| 1 | domain sources modelled | `160c44e` | fivecolour backtracker nodes HALVED |
| 2 | free-cast + Treasure under-credits | `4d59cf2` `f81936d` | ceiling became outcome-lossless |
| 3 | `{X}` bail removed | `fac7d4b` | hinata bail 16.4% → 15.8% |
| 4 | filter lands modelled | `3276311` | hinata unpayable-proving nodes −34.5%; **th's 87.6% bail was worth nothing** |
| 5 | storage lands modelled | `042a097` | dragonstorm nodes −16.3% |
| 6 | Three Tree City safe cap | `c41880c` | nothing today; oracle now UNCONDITIONAL |
| 7 | `mana_rock` is not growth | this commit | ~900k declines → applications, zero play change |

The recurring lesson across 4, 6 and 7: **a percentage of switched-off-ness is an upper bound on the
prize, and it is often entirely slack.** th bailed 87.6% and gave up nothing; the rock fix converted
~900,000 "decline to prune" decisions into real ceiling applications and changed no play at all.
Measure the prize, not the symptom.

## 1. SHIPPED: the flow oracle no longer bails on domain sources

The infeasibility oracle used to list `domain_mana` (Bloom Tender, Faeburrow Elder) as unmodellable
and **switch the whole oracle off** whenever one was on board. Domain is not unmodellable — it is
*more* constrained than an ordinary source: "for each color among permanents you control, add one
mana of that color" is not a choice, so total yield is `|domain set|` with at most **one** mana into
any single colour. Splitting the source capacity into `amt` (tap total) and `per_col` (per-colour
cap) expresses both shapes exactly; `per_col == amt` keeps every other source byte-identical.

Measured on FiveColour, 20 games at the gen settings (d3/b3):

| | before | after |
|---|---|---|
| backtracker nodes | 1,922,459 (66.5/entry) | **959,350 (33.2/entry)** |
| nodes spent proving UNPAYABLE | 1,069,147 = 51.2% | **36,208 = 3.6%** (83.6 → 2.8 per entry) |
| oracle bailed | 13,072 = 45.2% | **0** |
| oracle pruned | 2,458 = 8.5% | 4,665 = 16.1% |

Lossless, verified: smoke 36 passed / 0 failed, 0 searched and 0 d0 play-changed, 0 play-drift over
196 references.

## 2. FIXED: the ceiling was LOSSY — two under-credits, both now credited

`OptimisticTurnMana` is the enumeration-side ceiling (drop a subset whose total MV exceeds the
turn's optimistic mana). It ships **default OFF**, and the reason is not conservatism — it is wrong:

```
                       prune OFF          prune ON (HEAD)
fivecolour d0          6.1850             6.2340
fivecolour d3          5.1200             5.1667
fivecolour d5          5.2133             5.2133 (digest differs)
mirrorwing d0          6.3480             6.4070
mirrorwing d3          5.1867             5.2133
mirrorwing d5          5.0800             5.0933
```

Every difference was WORSE, so the ceiling **under-credited** and dropped legal plays.

**Both causes found and fixed (2026-08-16).** Neither was a mana source, which is why a total-mana
ceiling missed them:

* **Banked free cast** (`GameState::free_casts_available`, Maelstrom Archangel) — a cost BYPASS, not
  a source, so the bound cannot express it at all. Repro `fivecolour d0 gi1`: T5 connects, banks a
  charge, and the post-combat main casts Unite the Coalition — **mana value seven** — for free. The
  ceiling saw 7 > budget and pruned it, costing a turn (6 → 7). 53 of 1000 d0 games moved, all worse.
  Bounded, not declined: each charge frees exactly one spell, so crediting the N LARGEST hand mana
  values for N charges is a sound over-credit. Gated on the BANK, not the card, so the pre-combat
  enumeration (bank = 0) keeps its full-strength bound. fivecolour d0: **53 → 0**.
* **Sac-for-mana** (`sac_for_mana_amount`: Treasure, Lotus Bloom, Black Lotus) — modelled as an
  ACTION, so `AvailableManaPool` deliberately does not count it. Mirrorwing's Gold Rush mints
  Treasures and the enumeration then could not afford what they paid for. Credited exactly (a crack
  yields `sac_for_mana_amount` once), plus `creates_treasures` gross from hand.

Result: **all 36 smoke configs are OUTCOME-IDENTICAL** with the prune on vs off (every avg matches;
`fivecolour d0` matches on digest too). Residual digest differences on the other five affected
configs are the enumeration set changing — which is the prune's purpose — with no outcome effect
over 1000/150/75-game samples at d0/d3/d5.

That was exactly the failure its own comment records ("a bound built from them broke 12 smoke cases
across four decks") — and the lesson holds for everything built on top: **verify the bound before
tightening it**, because a tightening on an unsound bound only fires the unsoundness more often.

The A/B that settles it is cheap and should be the acceptance test for every later change here —
a lossless prune must be BYTE-IDENTICAL, not merely "close":

```bash
./build/Release/mtg --batch <manifest> --threads 0 | grep -oP '^\S+: played=\d+ avg=[\d.]+ digest=\w+' | sort > off.fp
MTG_EMIT_PRUNE=1 ./build/Release/mtg --batch <manifest> --threads 0 | grep -oP '...' | sort > on.fp
diff off.fp on.fp     # must be empty.  NB: strip ms= -- wall time differs per run and is not a result
```

### 2a. SHIPPED: a mana rock is not automatically growth

`budget_can_grow` used to decline the prune whenever any permanent or hand card had `mana_rock`.
Both sides of that were wrong (user: *"that's silly, Ancient Cornucopia is not a net mana adder"*):

* **on the battlefield** a rock is already counted — `AvailableManaPool` credits `mana_rock`
  unconditionally — so it cannot grow the budget at all;
* **from hand** it costs its mana value and returns `ManaProducedPerTap`, so it grows the budget only
  when NET POSITIVE (Sol Ring, {1} for two). Ancient Cornucopia is `{2}{G}` for one mana of any
  colour: **net −2**. It does not raise the ceiling, it means a line containing it needs two MORE
  mana. Crediting it as growth is backwards.

Fixed with a `from_hand` parameter plus `ManaProducedPerTap(d) > d.card.m_mana_cost.ManaValue()`.

The earlier reading of this fix ("on FiveColour it makes the prune fire more, d0 6.2340 → 6.2500")
was an artefact of the LOSSY ceiling it was measured against; with §2 fixed it is outcome-lossless
like everything else. What it does do is remove the last source of an INFINITE ceiling: on FiveColour
`inf-ceiling` went from the dominant case to **exactly zero**.

It is also the clearest example of the "measure the prize" lesson. A temporary counter in the branch
shows it is reached **385k times on the battlefield and 510k times from hand in 60 FiveColour games**
— nearly a million decisions flipped from "decline to prune" to "apply the ceiling" — and *not one
play changed*, on 1000 d0 + 150 d3 + 75 d5 games. Correct, live, and worth nothing on its own,
because an optimistic ceiling that is finally applied is still an optimistic ceiling. §5 is the
follow-on.

**Trap for anyone re-running these probes:** `--depth N` is REFUSED on a deck whose profile enables
`value_play` ("omit --depth to use it"). A run that hits that error produces no games and every
instrument reads zero — which looks exactly like "the branch is dead code". Omit `--depth` and use
the profile's own depth.

## 3. AGREED DESIGN: per-card feasibility as an odometer-dimension prune

The user's framing: *"can I play this single card"* — if a card cannot be cast on its own using the
whole untapped board, it cannot be cast in any subset either (the other members only consume mana),
so it can be dropped from the candidate pool entirely. That removes a **dimension** from the
powerset rather than rejecting subsets one at a time, which is where the leverage is.

Sound except where a set member can genuinely ADD mana, and those are enumerable:

| exception | how to bound it |
|---|---|
| rituals | credit the float they add (the params already carry it, incl. the per-copy escalation) |
| net-positive rocks (Sol Ring) | credit `ManaProducedPerTap − ManaValue`; ordinary rocks are ≤ 0 and need no credit (§2a) |
| sac-for-mana / Treasures | credit the sacrificeable bodies |
| cost reducers / alt costs | a discount is equivalent to extra mana — credit the max discount |
| **domain widening** | see below — this is the one worth being tight about |

### 3a. The tight domain bound

Loose version (what `OptimisticTurnMana` does today): credit every live domain dork the full
headroom, `live_dorks * (5 - have)`. On a two-colour board with two dorks that is **+6 mana of pure
fiction**, and a ceiling that loose prunes almost nothing.

The user's tightening, which is both cheaper and much tighter:

* A domain source's own colours are always in its domain (it is a permanent you control), so
  Bloom Tender `{1}{G}` guarantees ≥1 and can gain **at most 4**; Faeburrow Elder `{1}{G}{W}`
  guarantees ≥2 and can gain **at most 3**.
* Widening requires a colour-adding permanent to actually RESOLVE this turn — which requires paying
  for it. *"Most of the time you need to use your scaling sources to play your first 5-colour
  permanent, so you cannot tap them for 5 mana."* The two uses are mutually exclusive: if the only
  way to cast the colour-adder is by tapping the dorks, the widened yield is unreachable.
* Therefore: **the headroom is ZERO unless some colour-adding hand permanent is castable using
  non-domain sources alone** (with `D` untapped dorks, using at most `D−1` of them). One flow-oracle
  call per colour-adding hand permanent, restricted to non-domain sources — cheap, and the oracle is
  now domain-aware.
* And once the board is at five colours the headroom is zero by definition — *"once that permanent
  is out, your scaling sources no longer do additional scaling."*

The rare line this must not break is real but narrow, and the user named it: Birds + 4 lands +
Bloom Tender + Faeburrow Elder → cast a rainbow permanent **without tapping either dork** →
Progenitus. The `D−1` formulation keeps it.

Note `ComputeAvailableColors` already has a widen for the COLOUR gate (credit colours a hand
permanent would add once it resolves, `fivecolour_domain_widen.json`) and it is deliberately loose —
"no castability test: a looser necessary condition is still sound". The same tightening applies
there, and it is the same one flow call.

### 3b. Why the domain tightening is designed but NOT shipped

The soundness obligation is "no colour can join the domain this turn that the bound did not
anticipate". One load-bearing fact makes that tractable, and it is worth recording because it is not
obvious: **tokens in this engine are colourless.** `CreateToken` (`SpellEffects.h`) builds a `Card`
with a name, types, subtypes and P/T and *never a mana cost*, so `HasColor()` is false for every
colour and `DomainColors` — which reads the permanents' own colours, per the rules — can never see a
token. So the whole `*_creates_tokens` param surface (a dozen-plus fields) is irrelevant here, and
"which colours could arrive" reduces to "which permanent CARDS could enter".

What is left open is the residual hole: a permanent card entering from the GRAVEYARD or LIBRARY
rather than from hand — Garth One-Eye's activated abilities, Muxus's `etb_mass_put`, reanimation.
Those are driven by a battlefield permanent or a hand card, so a hand-only colour union is NOT a
superset, and closing it means enumerating put-onto-battlefield params. That is precisely the chase
`OptimisticTurnMana`'s own comment warns against ("rather than chase every mechanism with a slack
constant — fragile, and wrong once more than the next one is missed"), and getting it wrong drops
legal plays silently on any deck outside the suite.

Note also that the *first* half of the user's tightening is already implied and needs no code:
Bloom Tender can gain at most 4 and Faeburrow at most 3 **because a dork's own colours are its own
permanent's colours**, so they are already inside `have` and `5 - have` is already ≤ 4 / ≤ 3.

So the shippable part is the castability gate alone, and it needs the graveyard/library question
answered first. Do not ship a hand-only version: it is unsound on FiveColour itself (Garth).

## 5. WHERE THE REMAINING PRIZE IS — the ceiling is loose, and by exactly how much

With every bail gone and the ceiling always finite, the question is no longer "is the prune switched
on" but "does the bound bind". Measured on FiveColour (60 games, profile depth, a temporary counter
at the emit filter recording `mana_ceiling - EffectiveCost.ManaValue()` for every candidate):

```
seen=14,000,000   dropped=5,187,795 (37.1%)   inf-ceiling=0
slack:   0        1        2        3        4       5       6       7      8 ...
     1,469,795 1,371,050 1,342,375 1,284,214 1,006,627 776,233 581,582 378,354 213,491
```

Two readings, both actionable:

* the prune already **drops 37% of all candidate emissions** — this is not a marginal mechanism;
* the slack distribution is front-loaded, so tightening the bound converts almost linearly into
  drops. Tightening by 1 adds ~1.47M, by 2 ~2.84M, by 3 ~4.18M, by 4 ~5.47M — i.e. **a 4-mana
  tightening roughly DOUBLES the number of candidates pruned.**

Four mana is exactly the size of the domain fiction (§3a): `live_dorks * (5 - have)` is +4 with one
dork on a one-colour board and +6 with two. That is what makes §3a the highest-value item left, and
also why it must be got right rather than got quickly.

The same probe is the honest way to price any future bound change: slack histogram before, slack
histogram after, then the ON-vs-OFF acceptance test in §2.

### 5a. But the drops are FREE TODAY — the prune saves no work at all

Before treating "37% of emissions dropped" as a speedup, price it. Two measurements, both saying the
same thing:

**Deterministic work counters, prune ON vs OFF (15 games each, profile depth, single-threaded):**

| deck | rollout calls | rollout steps | interior nodes | backtracker nodes |
|---|---|---|---|---|
| fivecolour | identical | identical | identical | identical |
| mirrorwing | identical | 208,941 → 208,977 (+0.017%) | identical | identical |
| hinata | identical | identical | identical | identical |
| goblins | identical | identical | identical | identical |

The search tree is byte-identical. The pruned candidates never became tree nodes in the first place
— they were going to be rejected downstream as unaffordable at no search cost — so the prune is not
removing work, it is removing work that was already free.

**Paired CPU time (FiveColour, 12 games, single-threaded, arm order alternated):**

| arm | measurements (user CPU s) | min | median |
|---|---|---|---|
| ON | 16.74, 16.59, 16.47, 16.55, 17.10 | 16.47 | 16.59 |
| OFF | 16.98, 16.54, 16.62, 16.63, 16.88 | 16.54 | 16.63 |

**~0.4% apart: noise.** `MTG_EMIT_PRUNE` is performance-neutral today.

**METHOD WARNING, and it nearly produced a false result.** The first pass of this same A/B read
OFF 46.42s / ON 37.10s — a "20% win" — and a second read gave ON 48.93 / OFF 29.80, which reverses
it. Both were contention: the identical workload runs in **16.5s** on a quiet box, so the machine
was inflating user CPU by up to 3x. USER CPU IS NOT IMMUNE TO LOAD (cache and memory-bandwidth
contention buy more cycles for the same instructions). Alternate the arm order, repeat until the
numbers stop moving, and compare MINIMA — a single paired sample here would have shipped a
fabricated speedup, in either direction.

The consequence for the open decision: enabling this prune by default buys nothing measurable *now*.
Its value is entirely contingent on §3a — a bound that actually binds is what turns the 37% into
saved work.

## 6. The bail-outs: CLOSED (2026-08-16)

Every clause is gone. **All 12 suite decks now bail 0%** (20 games each at profile depth, `MTG_TAP_STATS=1`):

| deck | bailed before | bailed now | pruned now | total nodes |
|---|---|---|---|---|
| th | 87.6% | **0%** | 0.6% | 4,227 |
| goblins | 74.4% | **0%** | 0.0% | 8,102 |
| hinata | 13.4% | **0%** | 7.4% | 16,374 |
| dragonstorm | 0.7% | **0%** | 4.2% | 993 |
| slivers / antilife / creature_giving / mirrorwing / fivecolour | 0% | 0% | 10–13.6% | 13.8k / 7.1k / 413k / 80.8k / 2.17M |
| burn / auras / knights | 0% | 0% | 0–1.9% | 843 / 4,529 / 13,168 |

How each was closed, and what it was actually worth:

* **`{X}` spells** (`fac7d4b`) — one-line removal. X only ADDS to the cost, so the FIXED part that
  `ManaValue()` returns is a valid lower bound on demand: if the board cannot satisfy the fixed part,
  no value of X helps. (The per-card ceiling already reasoned this way.)
* **Filter lands** (`3276311`) — model the GROSS output and ignore the input. A filter is a gain edge
  and max-flow conserves flow, so exactness is unavailable; over-crediting is always sound here.
  `is_filter` → `amt = 2, per_col = 2` (both mana may be the same colour), plus Colorless so a strict
  `{C}` pip is not lost; `ramp_filter` → `amt = |produces|, per_col = 1`. **The tempting net-1 model
  is unsound**: Cascade Bluffs + Forest really does pay `{U}{R}` (tap Forest for `{G}`, feed it), and
  a net-1 model sees a `{G}` reaching neither pip and prunes it. Worth −34.5% unpayable-proving nodes
  on hinata and **nothing at all on th**, whose 87.6% bail was entirely slack.
* **Storage lands** (`042a097`) — exact, not an over-credit: the worker bursts
  `min(storage_counters, shortfall)`, so the live counter count is a tight cap, and
  `StorageSourceLive` has already excluded an uncharged/held land. Dragonstorm nodes −16.3%.
* **Scaled mana land** (`c41880c`) — the user's own second option, *"or to bound it to some safe
  cap"*. Ignore the `{2}` feeder (another gain edge), credit gross N over every colour plus Colorless.
  Goblins: bail 74.4% → 0%, pruned 0 → 0, nodes 8,102 → 8,102. Buys nothing today — a land making
  N-of-any-colour for free makes nearly every cost look feasible — but it is strictly no-worse than
  the bail (identical node counts prove it) and it makes the oracle UNCONDITIONAL, so no source type
  can silently switch the pruner off for a future deck.

The prioritisation lesson, since the audit that drove it got the order exactly backwards: **bail% is
an upper bound on the prize and it is frequently all slack.** th led the table at 87.6% and delivered
zero; hinata, at a sixth of that rate, gave up a third of its unpayable-proving work. The
correlation held on FiveColour's domain fix and nowhere else. Rank by measured prize next time —
the bail counter tells you where the pruner is *absent*, not where it would have *helped*.

## 7. What is left

1. **§3a domain tightening** — the highest-value item (§5 prices it at roughly double the drops), and
   blocked on the graveyard/library question in §3b, not on effort.
2. **§3 per-card feasibility prune** — note it needs a COLOUR-aware supply model, not the MV ceiling:
   a plan may play a land and then cast, so testing a card against only the currently-untapped board
   under-credits and would be unsound.
3. **USER CALL: ship `MTG_EMIT_PRUNE` on by default? RECOMMENDATION: not yet.** It is now sound
   (all 36 smoke configs outcome-identical ON vs OFF, five differing on digest only — the
   enumeration set changing, which is the point), so there is no correctness reason to keep it off.
   But §5a measures it as performance-NEUTRAL: identical search work on four decks and ~0.4% CPU,
   which is noise. Flipping the default would take on the risk of a prune with no measured benefit.
   Revisit the moment §3a lands — that is the change that turns the 37% of dropped emissions into
   work actually saved, and the same two measurements will price it.

## 8. THE BOTTOM LINE: the tap backtracker is ~1% of engine cost (2026-08-16)

Everything above optimises the mana backtracker. After closing every bail clause, the next question
was where the backtracker's own time goes, and the answer was clear: on FiveColour, **98.3% of its
nodes are spent on payments that SUCCEED** (37,203 entries, 71.3 nodes each), against just 1.7% on
proving costs unpayable (29,556 entries, 1.6 nodes each). The infeasibility work is finished — 1.6
nodes to prove a cost unpayable is the floor.

So the successful-payment path was attacked directly, and successfully: the oracle already computes
a max-flow assignment for every feasible payment and discards it, so `0b222dc` publishes it and uses
it to order the source AND colour loops. Nodes fall **9.5–13.6x** and a payable entry drops to 4.3
nodes for a 4.5-source answer — the backtracker stops searching and walks straight to the answer.

**And the total does not move.** Paired CPU (minima, arm order alternated): 16.64 vs 16.72s at play
depth, 7.78 vs 7.72s at gen settings, 80.47 vs 79.77s over 300 gen-settings games during which nodes
fell 13.9M → 1.46M. The 300-game run existed specifically to let the slow tail show up in the total.
It did not.

**Therefore: the tap backtracker is roughly 1% of engine cost, at play depth and at mulligan-gen
settings alike, and no further work on it can pay.** That retires this whole line of investigation —
including §3a and §5, whose remaining prize was measured against a mechanism (the emit ceiling) that
saves no work at all. Anyone arriving here wanting the engine faster should start from a profile,
not from this document; the prior measurement on this deck put PLAN-ENUM at 24.2% of mulligan gen
and the value leaf at 7.9%, which is where the time actually is.

The one thing worth carrying forward is the method, which caught three separate mirages in a day:
a percentage of switched-off-ness (bail%), a percentage of dropped candidates (37% of emissions),
and a large ratio on a real counter (13.6x nodes) each looked like a win and none of them was one.
Price the change in the currency you actually care about — total time — before believing any of them.
