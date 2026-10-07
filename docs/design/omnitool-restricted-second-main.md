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
