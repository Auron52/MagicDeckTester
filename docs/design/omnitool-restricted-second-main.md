# Adaptive Omnitool: a RESTRICTED second main phase

**Status: DESIGNED, NOT BUILT. Parked behind round 5 of the kitty v2 swap screen and behind a user
ruling.** Nothing in this document is adopted. The Omnitool currently ships with **no** second main,
which is a disclosed cost decision recorded in its `cards.json` bracket note and which **biases the
screening measurement against the card** (its attack-dig card is effectively delayed a turn).

Owner of the decision: the USER. This document is input to that decision, not the decision.

## The card and the problem

`Adaptive Omnitool` ({2} Artifact — Equipment, equip {3}) carries
`equip_scale_power_per_artifact: 1` / `equip_scale_tough_per_artifact: 1` and
`attack_dig_hand_count: 6` / `attack_dig_hand_types: ["Artifact"]` — when the equipped creature
attacks, look at the top 6 cards and take an Artifact into hand.

That card arrives **during combat**, i.e. after the pre-combat main has already been searched and
spent. With no post-combat main the card cannot be used until the next turn's main 1, so the deck
pays a full turn of delay on every dig. A full second main phase fixes that, and the measured
precedent for one is bad: on SelesnyaLifegain (2026-09-30, comments in `GoldFishRunner.cpp`) m2 ON
vs OFF gave **identical win turns in 120 of 120 games for 2.15x CPU**, and the blunt
`MTG_FORCE_USES_M2` arm measured **4.25x** on Fungus.

## The rule (user, 2026-10-07)

> "those are 2 narrow cases: Equipment = Only consider casting and equipping it and Ancient Den with
> remaining land drop = consider all plans that play it."

Plus, same session:

> "if the attack kills the opponent there is no point in running the second main."

So the gate is three conditions, and the admitted option set depends on which case fires:

| # | Condition | Admitted options |
|---|---|---|
| 0 | The attack was **lethal** | **none** — do not open a second main at all |
| 1 | Dug card is an **Equipment** | cast it and equip it; nothing else |
| 2 | Dug card is **Ancient Den** and the **land drop is unused** | **all** plans that play it |
| 3 | Dug card is Ancient Den and the land drop is spent | none (it is just a card in hand) |

## Why the case split is EXHAUSTIVE

Every artifact the dig can legally return in the `omnitool_for_cid` arm, enumerated from the
decklist: **Ancient Den** (the deck's only artifact *land*) and twelve Equipment — Cathar's Shield,
Accorder's Shield, Dragonfire Blade, Bone Saw, Colossus Hammer, Kite Shield, Deconstruction Hammer,
Golem-Skin Gauntlets, Skateboard, Spidersilk Net, Shadowspear, Adaptive Omnitool.

There is **no third category**. Remote Farm and Plains are not artifacts, so the dig cannot see them;
Sol Ring — the only non-Equipment, non-land artifact in the card pool — is sideboard and not in that
arm. **If Sol Ring (or any other non-Equipment non-land artifact) is ever maindecked, this case
analysis must be re-derived**, because such a card is a mana source and so belongs with case 2, not
case 1.

## Why case 1 is a SOUND IDENTITY FOLD, not a lossy gate

This is the load-bearing argument, and it is the one that distinguishes this design from
`MTG_EQUIP_ROUTE_TOPMAIN` (which CLAUDE.md names as the *bad* shape — a gate predicate consulted at
every budget, making a line unfindable at unlimited budget).

Case 1 refuses every second-main option except casting and equipping the dug Equipment. That is only
legitimate if the refused lines are **reachable anyway**, and they are. The user's argument:

> "There is no reason to change your other plans based on having a new equipment. Those things can
> all be done in main 1." / "The only thing that actually changes things is if we haven't played a
> land and now have an Ancient's Den in hand."

Expanded: **equipping an already-controlled permanent post-combat is strictly deferrable in this
goldfish.** An equip's benefit lands in combat, and next turn's main 1 precedes next combat, so
deferring it to next turn's main 1 reaches the same board at the same combat. The three things that
would break that equivalence all require an opponent who *acts*:

1. creature removal (the host dies before next turn),
2. artifact removal (metalcraft flips off between now and then),
3. not getting a next turn.

None exists in the goldfish. **The land drop is the only genuinely use-it-or-lose-it per-turn
resource** — which is exactly why case 2 is the one case that needs the full plan space.

**This soundness is goldfish-specific and must be revisited for phase 2 (a real opponent).** Under
an opponent who removes things, case 1 becomes a genuine (if small) loss and would need to be either
widened or re-justified. Record this in the code comment at the gate, not only here.

Case 2 admits the unrestricted plan space, so it raises no completeness question at all.

**Case 0 (lethal attack) is the cheapest sound fold of the three**: if the attack wins the game,
nothing after it can change the outcome. This is a pure win, zero quality cost — and per CLAUDE.md's
"COLLAPSE WASTED SEARCH UNCONDITIONALLY" it is worth doing on its own merits regardless of what it
buys in wall-clock.

Net: at unlimited budget, nothing this design refuses is unfindable. It re-prices; it does not remove.
Compare the sanctioned test — "a lever that changes what the engine can find at `--budget-ms 0` is
lossy and is out."

## Why this is NOT a 2.15x change

The user's framing is right: *"Technically it is still a searched main phase, but we just greatly
restrict what we let the search go over in the second main."* It is an **option restriction in the
provider**, which is precisely the kind of heuristic CLAUDE.md permits ("Only heuristics in the
provider are allowed to interfere with the search and there just to prune options"), and the search
still branches over what is admitted. It is the same class of thing as the existing main-1/main-2
split heuristics.

The Selesnya 2.15x and Fungus 4.25x numbers priced a **full second main on every turn**. This one is
gated, and the gate is a product of small factors:

```
cost ≈ 1 + (ceiling − 1) × trigger_rate            [first-order only]
trigger_rate = P(Omnitool equipped on an attacker)
             × P(attack is NOT lethal)
             × P(dug card is actionable: castable Equipment, or Ancient Den + unused land drop)
```

**The second factor is anti-correlated with the first, which is the strongest cost argument here.**
The user: *"that could be a common result... given that the deck plays a lot of equipment and
Omnitool hits pretty hard as a result."* The Omnitool's bonus is +1/+1 **per artifact controlled**,
in a deck that is mostly artifacts — so by the time an Omnitool-equipped creature is attacking, the
board state that opens the second main is the same board state that tends to make the attack lethal.
The condition that triggers the gate largely suppresses it.

Two caveats that cut the other way, stated because the cost argument should not be taken at the
flattering reading:

- **The denominator is SEARCHED FRAMES, not played turns.** The search explores many hypothetical
  turn plans, including non-lethal attacks that the played line never takes. The gate fires over
  those too, so the trigger rate over search frames exceeds the rate over played games. (This is the
  "count at the loop you mean to change" discipline — a played-game rate would be measuring the
  wrong thing.) Mitigating this: the search's job is finding the fastest win, so on big-board turns
  the lethal lines dominate and the non-lethal Omnitool frames concentrate on *earlier*, smaller
  boards.
- **A widened leaf set compounds over depth.** A second main multiplies that turn's distinct
  outcomes, and each outcome is a root for the next turn's search, so the true scaling sits
  somewhere above linear in trigger rate. Pushing back the other way, the restricted main is far
  cheaper per firing than the full main the ceiling measured. These two errors partly cancel; do not
  present the formula as precise.

## The mana-reservation tension DISSOLVES

An earlier version of this idea raised a real problem: `DripManaWantedLaterThisTurn`
(`SpellEffects.h:1234`) returns false unless `uses_second_main`, so a second main whose flag is off
gets no mana held back for it — and `DeckUsesSecondMain` is **per-DECK**, stamped once from the
decklist (`GoldFishRunner.cpp:1117` → `GameState::uses_second_main`), so it cannot express a
per-turn gate.

Under this design **neither case needs mana reserved**:

- case 1's Equipment are {0}–{1} and the equip is paid from whatever is actually spare;
- case 2's Ancient Den has `params = {"produces": ["W"]}` with **no `enters_tapped`**, so it enters
  untapped and supplies its own {W} the turn it lands.

So `uses_second_main` stays **FALSE** and this goes in as a separate narrow hook. That keeps every
non-triggering turn byte-identical and avoids the reservation side-effect entirely — a strictly
better shape than flipping the per-deck flag.

## THE DESIGN FORK: searched vs executor-only

The existing second-main machinery (`M2FixModeFor`, `DecisionProviders.h:2598`;
`M2FixpointMode` in `EngineFlags.h`) is **executor-only**. `AIEngine.cpp:2256`:

```cpp
if (!is_pre_combat_main && !m_in_rollout && M2FixModeFor(state) >= 2)
```

`!m_in_rollout` means the rollout never pays for it. That makes two shapes available:

**Shape A — executor-only**, mirroring the gate above. Near-zero search cost. **Rejected as the
primary route**, because the search would then never see the second main's benefit and would
therefore **undervalue the Omnitool** when choosing lines and when the screen scores the card. The
played line gets the upside while the plan that chose it was computed in a world without it — a
searched-plan/played-line divergence, which is its own defect class.

**Shape B — searched**, branching over the admitted options inside the search. Matches the user's
stated intent and CLAUDE.md's no-greedy rule. **This is the recommended route.**

Keep A documented as a cheap fallback if B measures badly, but only with the undervaluation
disclosed — it is not a free win.

Implementation note for B: **evaluate the gate post-combat and build the options lazily.** Then case
0 needs no lethality *prediction* — by the time the gate is reached the combat is resolved and the
win is already known — and a lethal turn pays literally nothing. Do **not** fold the second main's
actions into a pre-combat plan cross-product, which would pay enumeration cost before lethality is
known. Mode 2's existing *kill-scan* is the precedent for "a cheap thing in the second main" and is
the right cost class to aim at, as against mode 2's full re-solve.

## What to MEASURE before writing any of it

Both are cheap and neither needs this feature implemented.

1. **The gate-rate counter — the more informative of the two, and the one to build first.**
   Instrument the three gate conditions at **searched-frame** granularity (not played games, per the
   caveat above) on the Omnitool arm, behind an inert counter flag. This yields `trigger_rate`
   directly, which is the coefficient the whole cost argument turns on. If it comes back at a few
   percent, the feature is affordable and the rest is implementation.
2. **The `MTG_FORCE_USES_M2` ceiling.** It is a heurarm slot (`HeuristicArm.h:210`), so it **varies
   per job inside one pooled manifest** and needs **no new code** — a full-second-main arm on the
   Omnitool list bounds the entire idea today. Note it is a *loose* upper bound: it pays on every
   turn, including the lethal ones case 0 excludes and the turns cases 1–3 exclude. Note also that
   the ceiling on **kitty specifically is unmeasured**, and kitty is the branchiest deck in the
   suite — do not assume Selesnya's 2.15x transfers.

## THE DIG-TARGET HEURISTIC (user, 2026-10-07)

**Today the Omnitool's dig has NO ranking at all.** The base rule is one line
(`DecisionProviders.cpp:966`):

```cpp
std::vector<int> DecisionProvider::EtbDigCandidates(..., const std::vector<int>& legal) const
{
    return legal;
}
```

`legal` is in **look order, which is library order, i.e. shuffle order** — the base rule's own
comment says it "cannot rank them ... which is precisely why this decision is handed to the search".
But this fire site is not a searched axis (disclosed PARTIAL in the Omnitool's `cards.json` note), so
**the dig currently takes an effectively RANDOM artifact from the top six.** A ranker here is not a
tie-break refinement; it replaces a coin flip.

### How often the choice is live

Main deck (60 cards) holds **37 artifacts**, of which **20 are {0}-cost Equipment** (4 each Cathar's
Shield, Accorder's Shield, Bone Saw, Kite Shield, Spidersilk Net), 4 Colossus Hammer, 3 Golem-Skin
Gauntlets, 4 Ancient Den. Over six cards looked at:

| Top six contains | P |
|---|---|
| no artifact at all | 0.2% |
| ≥1 Colossus Hammer | **35.1%** |
| ≥1 hard hitter (Hammer or Gauntlets) | **54.1%** |
| ≥1 Ancient Den | 35.1% |

So a hard hitter is on offer in **over half** of all digs, while **54% of artifacts in the deck are
{0} shields** — exactly the pool a shuffle-order rule keeps taking instead.

### The rule

Context: the dig fires only when the Omnitool is attached to a **declared attacker**, so the user's
qualifier *"if omnitool is already equipped"* is **true at every firing** and its 1–2 hard-hitter cap
is therefore unconditional in practice.

Predicates:

- `free_equip_now` — Puresteel Paladin on the battlefield **and** ≥3 artifacts
  (`metalcraft_equip_zero_artifacts: 3`), **or** Sigarda's Aid on the battlefield.
- `free_equip_soon` — either in hand and deployable next turn.
- `draw_engine` — **Sram, Senior Edificer** (draws on Equipment **cast**) or **Puresteel Paladin**
  (draws on Equipment **ETB**) on the battlefield.
- `hard_in_hand` — Colossus Hammer + Golem-Skin Gauntlets (+ Omnitool) already in hand.
- `land_light` — the user's *"really low on land in hand and on the battlefield"*.

Ranking, best first:

1. **Ancient Den** when `land_light` — and per the user, *widen the searched axis* here instead of
   hard-picking (see below).
2. **Colossus Hammer** when `(free_equip_now || free_equip_soon) && hard_in_hand < 2`.
3. **Golem-Skin Gauntlets** when `hard_in_hand < 2`. Equip is **{2}**, payable without a free
   equipper, so it is *not* gated on one.
4. **{0} Equipment** when `draw_engine` — cast for free, draw a card.
5. Everything else by power delta on the **actual host**.
6. **The blank class, ranked LAST** (see the correction below).

### Correction: the "free equipment" branch needs a draw engine, or it is the WORST pick

The user's *"otherwise free equipment would be the best to draw more cards"* holds **only while Sram
or Puresteel is on the battlefield.** With neither out, a {0} Equipment draws nothing and is a +0/+2
or +0/+3 shield on a creature that is already attacking — a near-blank. It is then the **worst**
available target, not the best. This matters more than a corner case would, because {0} shields are
54% of the deck's artifacts, so this is the *majority* branch by volume.

Symmetrically, and as the user already noted, **Colossus Hammer with no free equipper anywhere is
also a blank** — {1} to cast and {8} to equip means it is never equipped. So the bottom of the
ranking is two conditional classes: *Hammer without an equipper* and *{0} shields without a draw
engine*.

With none of Paladin / Sram / Sigarda's Aid available, the genuinely best targets are the
payable-equip bonuses: Golem-Skin Gauntlets (equip {2}, scales per Equipment on the host),
Deconstruction Hammer ({1}), Skateboard ({1}, haste), Shadowspear ({2}), Dragonfire Blade ({4}).

### Ancient Den's condition is BROADER than second-main case 2

Case 2 of the second main requires the land drop to be **unused this turn**. The dig's Ancient Den
condition does not: if we are low on land in hand, the Den is valuable as **next** turn's land drop
even when this turn's is already spent. Rank it on `land_light`; gate the second main on the unused
drop. Two different predicates — do not collapse them.

### "Should probably be searched in that case" is SAFE BY CONSTRUCTION

The user's instinct maps onto the existing `EtbDigSearchWidth` / `MTG_ETBDIG_AXIS` machinery: raise
the searched width when `land_light`, so the Den-vs-Equipment call is branched rather than ranked.
This **only ever widens** the axis relative to today's effective width of 1, so it cannot make any
line unfindable — it is monotone in admitted options, which is the invariant the budget rule cares
about. It also sidesteps the disclosed greedy in this fire site rather than deepening it.

### IMPLEMENTATION TRAP: do not reuse the flat-power scorer

`DecisionProvider::AttackDigPutCandidates` (Armored Skyhunter's put pick,
`DecisionProviders.cpp:978`) ranks candidates on **flat `equip_power_bonus` only**. That scorer is
**blind to scalers**: Golem-Skin Gauntlets (`equip_scale_power_per_equipment: 1`, flat 0) and the
Adaptive Omnitool (`equip_scale_power_per_artifact: 1`, flat 0) both score **0 — tied with the blank
{0} shields and ranked below them only by index.** This is the same scaler-blindness that the
Omnitool build had to carve out of four other sites (`PaidPowerlessEquipWithheld`, the equip-route
`PayloadScore`, its `ChainTerms` waste term, and `EquipAttachDeltaFor`); `AttackDigPutCandidates` is
a **fifth site with the same defect and it was not carved out**.

**Scope: LATENT, not live on the shipped 60.** Armored Skyhunter is **sideboard**, so nothing in the
shipped list reaches that scorer. It is live for any arm that plays the Skyhunter, and it is a
standing trap for the next scaler.

So score the dig with **`EquipAttachDeltaFor` against the real host**, which was already fixed for
scalers during the Omnitool build — reusing the fix rather than repeating the bug. (Note its
documented conservatism: for a card still in hand it under-counts the artifact term by one, in the
safe direction and never down to 0.)

### The no-draw-engine case is ONE SCORER, not a second decision tree

User, 2026-10-07: *"if we have no draw we might look for a mix of castability + equippability and
damage. Gauntlets would still be great to keep there."*

That unifies the ranking, but the obvious scorer is **missing a term**, and the omission is
load-bearing enough that it inverts a conclusion recorded earlier in this document.

### `EquipAttachDeltaFor` alone is NOT enough: it deliberately omits the cast

The helper (`SpellEffects.h:2996`) prices an **attach**. Reading it:

```cpp
++k;  s_pw += d->params.equip_scale_power_per_equipment;        // per Equipment already on host
...
int pw = incoming.equip_power_bonus + s_pw
       + incoming.equip_scale_power_per_equipment * (k + 1);
```

So it **does** credit an incoming Equipment for raising an existing Golem-Skin Gauntlets on the same
host (the `s_pw` term) — that is the deck's engine and it is modelled. But the Adaptive Omnitool's
per-artifact term fires **only when the INCOMING card carries the scaler**, and the comment says why:

> "an artifact scaler ALREADY on this host gains nothing from an **attach**, because attaching does
> not change how many artifacts you control"

That is correct *for an attach*, and it means the helper cannot see the thing that actually matters
here: **casting any artifact raises `CountControlledArtifacts`, so the Omnitool's host gains +1/+1 —
without equipping anything.** The benefit is on the cast side, which `EquipAttachDeltaFor` does not
model. A scorer built on that helper alone misses the Omnitool synergy entirely.

Corrected scorer:

```
score(card) = EquipAttachDeltaFor(real_host, card)          // attach: own bonus + Gauntlets gain
            + cast_side_artifact_term(card)                 // MISSING TERM: +1/+1 per artifact-
                                                            // scaler host, if the card is an artifact
            + draw_term                                     // +1 card if Sram/Puresteel out
```

ranked on **absolute realisable delta given mana we would otherwise not spend**, *not* on delta per
mana — see the Bone Saw arithmetic below for why the rate form picks wrong.

### CORRECTION: in the Omnitool arm, NOTHING the dig returns is a blank

Two claims made earlier in this document are **wrong in the only arm where this dig exists**, and the
cast-side term is why:

- *"a {0} Equipment draws nothing and is a near-blank ... the WORST target"* — **no.** Casting it for
  {0} is **+1/+1 on the attacking creature, free**, draw engine or not.
- *"Colossus Hammer with no equipper is also a blank"* — **too strong.** Cast {1} is still +1/+1. A
  poor rate, not zero. It belongs at the bottom for being a *bad rate*, not for being worthless.

Ancient Den is an artifact land, so even playing it is +1/+1 on top of being a land.

**So the ranking is not a good/blank classification at all — it is purely a rate-and-magnitude
ordering.** The Omnitool's own scaler puts a floor under every artifact in the deck. The user's
"mix of castability + equippability and damage" framing was the right shape and the classification
was the error.

### Bone Saw is the best of the {0} class and should not be lumped with it

User, 2026-10-07: *"Bone Saw is not amazing, but not terrible either even with no draw or
free-equip. It only costs 1 to equip, so it can help with Gauntlets and Omnitool."*

Exactly right, and it is the case that exposed the missing term. Bone Saw is `{0}` cast,
`equip_cost_generic: 1`, `equip_power_bonus: 1` — **power, not toughness**, and the cheapest equip in
the deck. With the Omnitool on the attacker (always true at the dig), `g` = Gauntlets on that host:

| dug card | cast | equip | attach Δ | cast-side Δ | total for the mana |
|---|---|---|---|---|---|
| **Bone Saw**, g=0 | {0} | {1} | +1/+0 | +1/+1 | **+2/+1 for 1 mana** |
| **Bone Saw**, g=1 | {0} | {1} | +2/+0 | +1/+1 | **+3/+1 for 1 mana** |
| Spidersilk Net, g=1 | {0} | {2} | +1/+2 | +1/+1 | +2/+3 for 2 mana |
| Gauntlets (first), k=1 | {1} | {2} | +2/+0 | +1/+1 | +3/+1 for 3 mana |
| Gauntlets (2nd), k=2, g=1 | {1} | {2} | +4/+0 | +1/+1 | +5/+1 for 3 mana |
| Colossus Hammer, no equipper | {1} | — | unreachable | +1/+1 | +1/+1 for 1 mana |

Note the toughness shields are not dead weight either once a Gauntlets is on the host — each attach
feeds `s_pw` for +1 power apiece. That is the deck's engine, and it is why 20 {0} Equipment is a
sensible count rather than filler.

### With a draw engine, Bone Saw wins the {0} class — and the draw term must NOT be the tie-break

User: *"And with draw out, Bone Saw is quite strong, beating the other 0-mana equipment."* Correct,
and the mechanism is worth stating because it constrains the implementation.

Both draw engines trigger on **any** Equipment — Sram on the **cast**, Puresteel on the **ETB** — so
every {0} Equipment earns exactly the same `+1 card`. **The draw term is a constant across the whole
{0} class.** It raises the class as a block; it cannot order it. Bone Saw therefore wins the class on
its own merits, which are the two that survive in a goldfish:

- **power, not toughness** (+1/+0 vs +0/+2 … +0/+3) — toughness is near-inert against a passive
  opponent, and
- **equip {1}** against the shields' {2}–{3}.

Fully counted, Bone Saw with a draw engine and one Gauntlets on the host is **+3/+1 and a card for 1
mana** (cast {0}: +1 card, +1/+1 from the Omnitool's artifact count; equip {1}: +1 own, +1 Gauntlets).
With both Sram and Puresteel out it is two cards.

**The implementation constraint:** a naive scorer that simply adds `+draw` to every {0} candidate
leaves the entire class **tied** — at which point the tie-break falls back to candidate order, which
is library order, which is **the original shuffle-order bug**. The within-class ordering must come
from the power/equip-cost terms, and the final tie-break must be deterministic and meaningful (lowest
effective equip cost, then lowest card number), never positional.

### …but EFFECTIVELY EQUIPPABLE hard hitters still outrank it, and the reason is SCARCITY, not rate

User: *"So it's a pretty good option. But beaten out by hard hitters in that situation. (hard hitters
that we can effectively equip)"* — and that parenthetical is the load-bearing part, because it closes
the loop with the free-equipper rule above. **"Hard hitter" is not a card list, it is a card plus a
board state.** In the no-enabler branch a Colossus Hammer is *not* a hard hitter at all (it is
+1/+1 for {1}, the cast-side term only), so it does **not** beat Bone Saw there. The ordering is
therefore state-dependent in both directions:

| board | ordering |
|---|---|
| free equipper (Puresteel metalcraft, or Sigarda's Aid) | **Hammer** > Gauntlets > Bone Saw > toughness shields |
| no free equipper | **Gauntlets** > Bone Saw > toughness shields > **Hammer** |

The Gauntlets sit above Bone Saw in *both* rows because equip {2} is always payable — they are the
only hard hitter that is unconditionally "effectively equippable". Agreed on the ranking itself —
and worth being careful about *why*, because the rate form of the scorer gets this backwards: Bone
Saw is 2–3 power per mana against the Gauntlets' ~1–1.7, so **ranking on delta-per-mana would
promote Bone Saw over the Gauntlets.** Three reasons it should not:

1. **The dig takes ONE card, and mana is usually not the binding constraint.** With 16 lands against
   a curve of {0}s and {1}s, spare mana is common; cards are scarcer. So the right metric is the best
   **absolute** improvement this one card can buy, not its efficiency.
2. **The Gauntlets compound and Bone Saw does not.** Gauntlets' bonus rises with *every* future
   Equipment attached to that host, in a deck holding 20 more of them. Bone Saw is a static +1.
3. **Bone Saw's class is the one we will draw anyway.** 20 of the deck's 37 artifacts are {0}
   Equipment; Golem-Skin Gauntlets is a **3-of** and Colossus Hammer a **4-of**. Digging for the
   abundant class wastes the dig's selectivity. This is the same instinct as the user's original
   `hard_in_hand < 2` cap, pointed the other way.

Reason 3 is the sharpest and generalises past this card: **the dig should prefer the scarce class**,
because its whole value is selection. A scorer that is purely board-state-driven will miss that, so
the ranker wants a scarcity (copies-remaining) weight alongside the delta.

### Net placement: Bone Saw DOMINATES the {0} class — which makes it a prune, not a preference

User: *"So it always beats the other 0-mana cards. It only loses to heavy hitters. Pretty much,
anyway."* That is a stronger claim than a ranking preference, and on the shipped 60 it holds as
**sound dominance**. Checking every axis a {0} Equipment can differ on:

| axis | Bone Saw vs the other {0} Equipment |
|---|---|
| cast cost | **equal** ({0}) |
| cast-side Omnitool / metalcraft artifact count | **equal** (+1 artifact) |
| draw trigger (Sram on cast, Puresteel on ETB) | **equal** (+1 card; fires on any Equipment) |
| Gauntlets `s_pw` when attached | **equal** (+1 power per Gauntlets) |
| Kor Duelist / Balan equipment-count thresholds | **equal** (any Equipment counts) |
| own bonus | **BETTER** — +1/+0 **power**; the others are +0/+2 … +0/+3 toughness |
| equip cost | **BETTER** — {1} vs {2}–{3} |
| vigilance (Cathar's / Accorder's Shield) | **inert** — nothing blocks or attacks back |
| toughness | **inert** — nothing on the shipped 60 deals damage to our creatures or gates on toughness |

Equal on every shared axis, strictly better on the two that are live. So this is **dominance, not a
heuristic**, and per CLAUDE.md's collapse rule that difference is exactly what licenses a free
collapse: whenever a Bone Saw is among the legal candidates, the other {0} Equipment can be **pruned
from the candidate list entirely** rather than merely ranked below it. That removes 3–4 candidates
from a typical dig, which also shrinks the searched axis if one is ever enabled here.

Bone Saw is also the best **case 1** target for the second main, for the same reason: {0} cast plus
equip {1} is deployable post-combat on one mana, where the {2}–{3} shields frequently are not.

**The "pretty much" is the two inertness rows, and they are the same caveat as case 1's identity
fold:** toughness and vigilance are inert *because the opponent is passive*. Under a real opponent
(phase 2) both become live and the dominance degrades to a preference. Gate it on the goldfish
assumption and say so at the call site — do not let a later opponent model silently inherit a prune
that was only ever valid against a goldfish.

Against everything else, Bone Saw is a **floor, not a ceiling**: it loses only to an effectively
equippable hard hitter, per the two-row table above.

### USER RULING: take Bone Saw UNCONDITIONALLY, accepting one named corner

User, 2026-10-07: *"I suppose in a situation where we had only that equipment in hand and nothing
else to cast a +2/+2 equipment would be very very marginally better, but I think we can take the bone
saw there. (unconditionally)"*

**This is the user's decision, recorded with its exact cost so it stays revisitable.** The corner is
**Dragonfire Blade** — the deck's only +2/+2, a 1-of, `{1}` cast with `equip_cost_generic: 4` and
`equip_cost_less_per_target_color: 1` (so {3} against a mono-white host). Note it is **not** in the
{0} class, so the dominance above is untouched; this is about the broader "only loses to hard
hitters" claim.

What the corner costs, fully counted:

| | cast | equip | attach Δ | cast-side Δ | mana |
|---|---|---|---|---|---|
| Bone Saw | {0} | {1} | +1/+0 | +1/+1 | **1** |
| Dragonfire Blade | {1} | {3} | +2/+2 | +1/+1 | **4** |

Both gain the same +1/+1 from the Omnitool's artifact count, and the toughness is inert, so the true
difference is **one point of power on one attack** — bought for 3 extra mana. The Blade wins only
when that mana is genuinely free, which requires *all* of: 4+ untapped mana, nothing else in hand to
cast **or equip**, and the dig's choice actually being between those two cards.

That premise is close to self-defeating in this deck: with 20 {0} Equipment in 60 cards, a hand with
nothing else castable is rare, and the dig has just added a card to it. So the ruling buys an
unconditional rule and gives up **≤1 power, on one attack, in a rare state** — and avoids a
conditional branch that would otherwise need its own measurement to justify. Reasonable, and it is
the user's call either way.

## Skateboard: the card that proves the scorer must price LINES, not cards

User, 2026-10-07, across four messages: *"we should price Skateboard slightly differently if we have
a creature to haste the next turn"* / *"the cost of the creature should be included in that line"* /
*"(and it cannot be a duplicate Sram)"* / *"Essentially it can do either option. Play the creature
with haste if we have nothing else to do or just equip to our existing attacker."*

Skateboard is `{1}` cast, `equip_cost_generic: 1`, `equip_power_bonus: 1`, `equip_grants_haste: true`.

### It is never dead, because it has two lines

```
score(Skateboard) = cast_side_omnitool            // +1/+1, always (it is an artifact)
                  + max( equip_existing_attacker, // FLOOR: +1/+0 own, +1 per Gauntlets on that host
                         haste_a_new_body )        // UPSIDE: gated, and much more expensive
```

The floor is always available and cheap ({1} cast + {1} equip), which is why the haste term is
*upside* rather than the card's reason for existing. **The scorer therefore needs a `max()` over
realisable lines, not one value per card.**

### The haste line, priced in full

Per the user, the creature's own cost belongs **inside** the line:

| | mana |
|---|---|
| cast the creature | {1}{W} or {W}{W} = **2** |
| cast Skateboard | **1** |
| equip | **1** (0 under Puresteel metalcraft) |
| **total** | **4** (3 under metalcraft) |

Benefit: a **3/2** (2/2 body + Skateboard's +1/+0) attacking **one turn earlier**.

Two things that narrow it further:

1. **Haste is worth nothing to a creature already on the battlefield** — it sheds summoning sickness
   at our next untap for free. So the gate is strictly *"a creature we will **cast** next turn"*, not
   "a creature we have".
2. **The 3 damage only pays if it crosses the lethal line.** Otherwise the line's entire value is
   zero, because the deck's clock is set by its one big equipped attacker, not by a 3/2. And at 4
   mana on a 16-land deck, the line competes directly with equipping that main attacker.

### "It cannot be a duplicate Sram" — verified, and it is uniquely a Sram problem

The gate is *a **new** attacking body*, which a duplicate legend does not provide: casting the second
copy triggers the legend rule, one goes to the graveyard, and the board ends with the same single
body. Checked against the deck:

| creature | copies | legendary? | duplicate adds a body? |
|---|---|---|---|
| **Sram, Senior Edificer** | **4** | **yes** (`supertypes: ['Legendary']`) | **NO — the live case** |
| Puresteel Paladin | 4 | no | yes |
| Cid, Freeflier Pilot | 1 | yes | n/a (1-of) |
| Dwalin, Weaponmaster | 1 | yes | n/a (1-of) |

So the user's instinct is exactly scoped: of the deck's 10 creatures, **Sram is the only legend with
multiple copies**, so "duplicate legend in hand" is a Sram-and-only-Sram condition. (`LegendKeepIndex`
keeps the oldest, which is the right choice here — the older copy is not summoning sick — though the
status table notes that pick is unbranched.)

### The architectural consequence: hand the lines to the SEARCH, not to the provider

Taken together, the scorer this conversation has converged on is: *enumerate the lines a candidate
enables, price each one against actually-available mana including every card in the line, take the
max.* **That is a search.** Re-implementing it inside `EtbDigCandidates` would mean building a
mana-aware line evaluator in a provider hook — duplicating the enumerator and risking exactly the
desync `engine-heuristics-to-providers.md` warns about for `LandEntersUntapped`:

> "enumeration prices a plan's mana off that same function. If the two disagreed, the planned mana
> and the realised mana would diverge."

So the recommendation sharpens: **the search prices the lines, not a hand-written evaluator in the
provider.** The user's own instinct for Ancient Den (*"should probably be searched in that case"*)
turns out to be the right answer for the **whole** decision, not just the land case.

### USER RULING on the shape: searched BY DEFAULT, PRUNED by heuristics — not width-cut

User, 2026-10-07: *"I don't think we should fully search this or rather it should be searched by
default, but we should prune it with heuristics."*

This supersedes an earlier draft of this section, which proposed that the provider merely **order**
candidates and let the axis's **width-3 positional cut** take the top three. The user's shape is
better on both halves, and the difference is not cosmetic:

| | positional width cut (rejected) | heuristic prune (adopted) |
|---|---|---|
| what decides | **rank position** — "why 3?" is arbitrary | **a reason per candidate** (dominated / unplayable / abundant) |
| failure mode | a mis-ordering silently drops a good candidate at rank 4 | a candidate is dropped only by a stated rule, which is reviewable and measurable |
| what the search sees | an arbitrary prefix | everything that survived for a reason |
| default | axis opt-in per site | **axis ON by default at this site** |

So: **prune for reasons, then search everything that survives**, with the axis on by default. This is
exactly the "searched-with-heuristics" shape CLAUDE.md insists on: the provider may *restrict or
order*, and the search branches over what is admitted.

**The rationale, and it is the collapse rule's own:** *"Because too many cases are obviously
unnecessary."* Branching over a candidate the deck would never take is wasted search, and
CLAUDE.md's collapse rule says to remove wasted search unconditionally — judged on work removed and
soundness, never on whether wall-clock fell.

**The sizing target, from the user:** *"If we need to search 2 options occasionally this is fine. But
I wouldn't fully search it."* So the goal is **1 candidate usually, 2 occasionally, never the full
legal set.** That is much tighter than "cover whatever survives" and it gives the prune a concrete
bar to hit: the heuristic must confidently discard the obvious, leaving the search only for the
genuinely ambiguous.

**When is 2 justified? Keep the Pareto front.** Drop everything dominated (tier 1 below), then among
the survivors admit more than one only when they are genuinely *incomparable* on the deck's live
axes. In practice that is a short list of real pairs:

| the ambiguous pair | why neither dominates |
|---|---|
| Colossus Hammer vs Gauntlets, free equipper out | +10/+10 now against a bonus that compounds with every future attach |
| Ancient Den vs a hard hitter, land-light | a land drop against damage — different resources (the user's case 2) |
| Gauntlets vs Bone Saw, mana tight | 3 mana for +3/+1 against 1 mana for +2/+1 |

Everything else resolves to one candidate: which {0} shield (dominance), a Hammer with no equipper
(dominance), a third hard hitter (the cap).

**This also defuses the original deferral rationale.** The Omnitool's `cards.json` note deferred the
searched axis because *"a searched axis here would multiply combat branching by the number of legal
artifacts (typically 2-5)"*. That figure assumed the **full** legal set on **every** dig. Pruned to
1–2 and only occasionally 2, the multiplier is ~1.0 most turns and ~2.0 on the ambiguous ones —
which is a different cost class from the 2–5x-always the deferral was priced against. The note's
deferral should be revisited on those terms rather than carried forward.

### Tier the prunes by soundness — they are not all the same kind

The collapse rule's own distinction applies (sound dominance / identity fold → collapse freely,
default ON; projection / heuristic narrowing → earn it, default OFF until measured). The prunes
specced in this document split cleanly:

**Tier 1 — sound dominance. Unconditional, default ON, nothing to measure.**

- **Bone Saw dominates the other {0} Equipment** (the axis table above): equal on every shared axis,
  strictly better on power and equip cost.
- **Bone Saw also dominates a Colossus Hammer with no free equipper.** Both reduce to their cast-side
  +1/+1, but the Hammer costs {1} to Bone Saw's {0} *and* its equip is unreachable, while Bone Saw's
  {1} equip adds more. So that Hammer is strictly worse — which means the free-equipper check is not
  merely a ranking preference, it feeds a *sound* prune.
- Both carry the **goldfish condition** (toughness and vigilance inert against a passive opponent) and
  must be disclosed at the call site so a later opponent model cannot inherit them silently.

**Tier 2 — heuristic narrowing. Default OFF until measured.**

- **The scarcity weight** (prefer the class with fewer copies left). Genuinely lossy: "we will draw
  one anyway" is not "this one is worse".
- **The `hard_in_hand < 2` cap.** A third hard hitter is worth *less*, not nothing.

Keeping these two tiers apart is what stops a measured, reviewable narrowing from being smuggled in
behind a free one.

### What "axis on by default here" actually costs to build

Worth stating plainly because it is not a flag flip. The existing axis is driven by a **pin carried
in a main-phase plan** (`TurnSolver.h:995`), and this dig fires **in combat**, deliberately wrapped in
`ScriptedEtbDig no_inherited_pin(-1)` so it cannot consume a main-phase plan's pin. Enabling it here
therefore needs a **combat-phase dig pin**: a field on the plan that declares attackers, branched
over the pruned candidate set, threaded into `FireAttackEquipDigHand`. Bounded, but real work —
and `AttackDigPutCandidates` (Armored Skyhunter) would want the same treatment.

The user's read on the Gauntlets also stands, and is worth stating as the reason: it is the only hard
hitter whose value depends on **nothing external** — cast {1}, equip {2} is always payable, and its
scaler reads Equipment already attached to the host, which at the dig is **at least the Omnitool
itself**. Best pick in the no-enabler branch, top-2 in the enabler branch, so it is the ranking's
safe default and the right tie-break target.

## ROOT CAUSE: why the default was shuffle order, and why it survived

The user: *"Why did we have shuffle-order originally? That seems super bad and I would like to cut
those kinds of issues off at the pass."* It is super bad, and the repo had already measured how bad —
the history is worth stating precisely, because the failure is not the one it looks like.

**It was never chosen as a ranking.** Pre-hook, `PerformEtbDig` took the first legal match inline.
When the decision was extracted to a provider hook (`45eff8a6 refactor(providers): port the Goblin
Lackey put + ETB dig picks to provider hooks`) the default was kept **byte-identical on purpose**, so
that the refactor moved no numbers. That is correct refactor practice. The hook's own docstring
(`DecisionProvider.h:2284`) is explicit and unflattering about what it left behind:

> "The base implementation is the historical rule: the FIRST legal match in look order. **That rule
> is poor and is retained only for byte-identity** — look order is library order, i.e. shuffle order,
> so which Knight you take is effectively random among the matches. It is the same defect as 'first
> land in hand order' in the cleanup-discard rule: **a ranking that is really insertion order.**
> Providers should override."

**And the site WAS then fixed, properly, with a measurement.** From
`docs/design/engine-heuristics-to-providers.md`'s status table:

> | ETB dig pick | yes (`EtbDigCandidates`) | first-match-in-shuffle-order, **94% of digs have a choice** | **yes — `910a234`, 7/7 seeds, −0.0599** |

The resolution was not a better ranking — it was **handing the decision to the search** (the ETB-dig
axis, `EtbDigSearchWidth` default 3, the Knights-measured knee). Worth **−0.0599 avg win turn on
7/7 seeds**, which for scale is larger than the best deckbuilding change found in two full rounds of
the kitty swap screen (`g4_cid`, −0.0366).

**So why does the Omnitool get the bad behaviour?** Because the fix was attached to a **call site**,
not to the **hook**. `FireAttackEquipDigHand` is a *new fire site* for the same ranking hook, firing
in combat, and it is deliberately **not** on the searched axis — it is wrapped in
`ScriptedEtbDig no_inherited_pin(-1)` precisely so a main-phase plan's dig pin cannot be consumed
there. It therefore inherited the known-poor default **without inheriting the fix**, and the same is
true of `AttackDigPutCandidates` (Armored Skyhunter), the other combat-phase dig.

**That is the generalisable defect: a new call site for an existing mechanic silently regresses to
the mechanic's pre-fix default.**

### Cutting it off at the pass

**Disclosure has already been tried here and it did not work.** The Omnitool's `cards.json` note
*does* state the gap ("which artifact the dig takes is provider-ranked and human-choosable but NOT a
searched axis"), and the hook docstring *does* say "providers should override", and the status table
*does* list the arbitrary rules. None of that caused anyone to act. This is the exact pattern
CLAUDE.md already records for suite membership — `verify_deck.py` disclosed "deck is NOT a
regression case" as a note nobody had to act on, it was duly skipped, and the remedy was converting
it into a **hard gate** (exit 3). The same remedy applies.

Recommended, cheapest first:

1. **Instrument, don't infer (do this first).** Have each known-poor base default call a shared
   inert helper — `NotePoorDefault("EtbDigCandidates", n_legal)` — that counts only when the audit
   flag is on, and only when `n_legal > 1` (a forced pick is not a decision) **and** no searched-axis
   pin covered it. Then run the suite once with the flag on: **any shipped deck with a nonzero
   count is a flag.** This is empirical rather than static, so it needs no reachability analysis and
   cannot be fooled by a card param it does not know about. It would have caught the Omnitool on its
   first game, and the Knights 94% case years earlier.
2. **Then gate it.** Once the counter is clean, assert it stays clean: a deck whose play resolves a
   poor default on an unforced, unsearched decision fails the audit rather than printing a note.
3. **Fix the hook, not the site.** Where a mechanic's decision has a searched axis, new fire sites
   for that mechanic should default *into* the axis and opt out explicitly, rather than defaulting
   out of it. That inverts the direction a mistake falls.

### The standing list this applies to

The status table already names the other decisions sitting at arbitrary defaults with no branching —
these are the rest of the user's "those kinds of issues", available without any new analysis:

| rule | current default | branched |
|---|---|---|
| **retrace discard** | first land in **hand order — arbitrary** | no |
| **legend-rule keep** | keeps **oldest**; CR 704.5j makes it a real choice | no |
| **cleanup discard** | provider-owned, but **two standing objections** | no |
| Shard Volley sac-land | tapped-first sound; **fallback arbitrary** | no |
| Goblin Lackey put | ranked, **axis not yet emitted** | partial |

Note the cleanup-discard row is the same item already open on kitty v2 (its buckets still name v1
cards, so Sram, Cid and Dwalin are unbucketed and shed before surplus Equipment) — and the dig
hook's own docstring names cleanup-discard as the sibling defect. Same class, same remedy.

## Open

- **The user's ruling on whether to build this at all.** Until then the Omnitool ships with no second
  main, and every Omnitool screening arm is a **lower bound** on the card.
- Parked behind round 5 (user, 2026-10-07: *"For now we'll wait on Round 5."*).
- **The dig ranker is independently adoptable and probably worth more than the second main.** It
  needs no second main, no new param and no gate — it replaces a shuffle-order pick in a decision
  that is live on >50% of digs. Worth measuring as its own screening arm, ahead of the second main.
- **`AttackDigPutCandidates`'s scaler blindness** (latent, sideboard-only, described above). Fixing
  it is a ranking-quality change, not a rules fix, so it wants a measurement on an arm that actually
  plays Armored Skyhunter.
- **The poor-default audit** (counter → gate → default-into-the-axis), and the standing list of other
  arbitrary rules it would surface. This is a repo-wide change, not a kitty one; it is listed here
  only because the Omnitool is the instance that exposed it. **User-owned: whether to build it, and
  in what order against the deck work.**
