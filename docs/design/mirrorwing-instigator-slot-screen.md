# The Goblin Instigator slot in Mirrorwing Dragon (screen, 2026-08-26)

User question: what should replace the 4 Goblin Instigator? Candidates named: Frontline Heroism,
Undercellar Myconid, Nest Invader (promising), Young Pyromancer (expected to lose). Expectation
stated up front: *"It is expected for Goblin Instigator to lose to Nest Invader because of the extra
sac option."* Constraint stated up front: **no new mulligan profiles** — reuse the shipped one.

## Answer

**4 Frontline Heroism.** −0.4431 ± 0.0062 avg win turn vs the 4-Instigator base (t = −71.60, 20,000
paired games at seed 930000), reproduced on held-out seeds at −0.4531. Survives 30 life (−0.4420),
50x budget (−0.4107 at d7/b1000), and both non-clairvoyant modes (−0.4338 honest, −0.4067 true NC).

> **2026-08-30 — two later findings bear on everything below; see
> `mirrorwing-final-results.md`.** (1) The engine support for all four candidate cards was
> **never committed** — it lived in an uncommitted stash, and `frontline_copy_tokens` /
> `created_token_haste` were unread by any source file, silently ignored by `params.value()`.
> Re-applied and verified 2026-08-30; the Frontline conclusion here is unaffected in direction
> (the missing clause is pure upside for Frontline) but the *magnitudes* were measured with the
> stash applied and are only reproducible with it. (2) A depth/budget ladder showed the shipped
> `d5/20 ms` setting is **search-starved** for this deck; the Oracle-4-vs-Anger-4 recommendation
> flipped to **4 Ancestral Anger** once measured at converged depth.

Stage-1 pure 4-ofs @20 life: frontline **−0.4431**, nest_invader −0.1376, pyromancer −0.1092,
myconid **+0.0391** (the one arm slower than the incumbent — see the stratification section; it is a
mixture, not a verdict).

No mix beats the pure 4-of. The mixes ladder (12,000 paired games, seed 940000) is **monotonic in
Frontline Heroism count** — every trade of a Heroism for anything else costs speed, in strict order:

| arm | delta | arm | delta |
|---|---|---|---|
| fl4 | **−0.4339** | fl2_ni1_yp1 | −0.2970 |
| fl3_ni1 | −0.3757 | fl2_gi2 | −0.2468 |
| fl3_gi1 | −0.3523 | fl1_ni3 | −0.2301 |
| fl2_ni2 | −0.3056 | ni4 | −0.1302 |

Closest pair is fl4 vs fl3_ni1 = −0.0583 ± 0.0033 (t = −17.52): the 4th Heroism is worth more than
the best card that could replace it, and the ladder never crosses.

> All numbers above are under the **corrected heuristics** (`MwBodyCount`, cast-order rank 6 —
> see "The cast-order gap"). An earlier ladder measured with Heroism stuck at cast-order rank 20
> ranked the arms identically but understated every Heroism-bearing arm by ~0.05–0.10; it is
> superseded and not reproduced here.

## PER-CARD REPORT

Keep this section current as more is learned about these cards — it is the per-card view; the rest
of the document is the per-screen narrative. All deltas are avg-win-turn vs the 4-Instigator base,
negative = faster, under the table-less screening apparatus (see Caveats).

### Frontline Heroism `{2}{R}` — Enchantment — **ADOPT (4-of)**
ETB: 1/1 red Soldier with haste. *Whenever you cast a spell that targets only a single creature you
control, create a 1/1 red Soldier with haste, then copy that spell; the copy targets that token.*

| 20 life | 30 life | d7/b1000 | NC honest | NC true |
|---|---|---|---|---|
| **−0.4431** (held-out −0.4531) | **−0.4420** | −0.4107 | −0.4338 | −0.4067 |

* **Not a Zada.** It gives **1 extra copy, not a board-wide fan** — user-corrected and then
  instance-counted (`cast_lifegain` fires once per payload): Mystic alone 1 -> with Heroism 2;
  Zada+2 creatures 3 -> with Heroism 5. The magnet case is +2 because the Soldier is a NEW BODY the
  magnet also fans onto — the same +1 any extra creature gives.
* **Trigger order: Heroism first, Zada second** (user ruling 2026-08-27; CR 603.3b controller's
  choice). `MTG_FRONTLINE_FIRST=0` takes the other order (4 instances) — attribution lever only,
  never used to decide anything.
* **Its cast-order rank is load-bearing.** Rank **6** (after magnets, before every trick). Shipped at
  the generic 20 originally, which cost 0.05 (20 life) / 0.10 (30 life) — see the cast-order gap.
* Modelled by `frontline_copy_tokens` in `ResolveSoloTargetTrick` (shared executor/rollout) +
  `etb_self_creates_tokens` + `created_token_haste`.
* **Open:** no archetype heuristic names it beyond the body/cast-order rules; it is otherwise played
  generically. A provider tweak is the likely next gain if adopted.

### Nest Invader `{1}{G}` 2/2 — **BEST OF THE REST**
ETB 0/1 Eldrazi Spawn with "Sacrifice this creature: Add {C}."

| 20 life | 30 life | d7/b1000 | NC honest | NC true |
|---|---|---|---|---|
| **−0.1376** | −0.1212 | −0.1240 | −0.1363 | −0.1440 |

* Beats Goblin Instigator by ~0.13 across four independent seed blocks — the user's prediction
  ("the extra sac option") confirmed. At 3 Heroism the 4th slot still prefers it over Instigator.
* **The Spawn's `{C}` is COLOUR-PINNED** (`sac_for_mana_color: "C"`). Modelling it as the engine's
  existing any-colour sac-mana would have let it pay `{R}`/`{G}` pips in a two-colour deck —
  flattering the very card the user predicted would win. Verified both halves with a discriminating
  scenario pair (`logs/cardcheck/scen/`): cannot pay `{G}`, can pay generic.
* Token is a NAMED DEF (`"0/1 Eldrazi Spawn Token"`, the Treasure Token precedent) — that is what
  makes the sac ability live.

### Young Pyromancer `{1}{R}` 2/1 — **scales with race length**
Cast an instant/sorcery -> 1/1 red Elemental.

| 20 life | 30 life | d7/b1000 | NC honest | NC true |
|---|---|---|---|---|
| −0.1092 | **−0.1417** | −0.0940 | −0.1075 | **−0.1587** |

* **Overtakes Nest Invader at 30 life AND under true NC.** Its output scales with spells cast, so a
  longer or less-informed game pays it; Nest Invader's ramp matters most when the game turns on
  landing the magnet a turn early. Same swap shows in the mixes (`fl2_ni1_yp1` trails `fl2_ni2` at
  20 life, ties it at 30).
* Not eliminated as the user expected — it is clearly ahead of the incumbent, just never better than
  one more Heroism.
* **Copies do not trigger it** (CR 707.10): one Fists into a five-creature Zada fan makes ONE
  Elemental. `FireOnCastTriggers` runs once per real cast, so this is correct without a guard.

### Undercellar Myconid `{2}{G}` 1/2 — **conditional ramp, NOT simply worse**
ETB *or dies* -> 1/1 green Saproling. `{T}: Add one mana of any color.`

| 20 life | 30 life | d7/b1000 | NC honest | NC true |
|---|---|---|---|---|
| +0.0391 | +0.0059 | +0.0093 | +0.0300 | **−0.0187** |

* **The unconditional number is a MIXTURE — do not quote it alone.** Stratified (below): better with
  Mirrorwing at both life totals, better on land-light hands, worse only when the manabase bites
  (tapped land on T2) or when NO magnet is ever cast. That last slice — 13% of games at 20 life —
  carries +0.41 and contributes more than the whole measured effect.
* **It casts MORE magnets than the Instigator arm** (89.8% vs 87.0% at 20 life), so its ramp does
  real work; the stratum is not an artefact of the card.
* **Mechanism of the loss:** it is a THREE-drop. In no-magnet games it lands 0.77 turns later than
  Instigator (3.75 vs 2.98) with nothing for its mana to do, and 5.7pp more of those games fail to
  win at all (18.3% vs 12.6% unwon). In magnet games the deploy gap shrinks to 0.25 turns and the
  win-turn difference vanishes (4.775 vs 4.784).
* **Clairvoyance was hiding its value** — it is the only card that improves under blinded search.

### Goblin Instigator `{1}{R}` 1/1 (incumbent) — **cut**
ETB 1/1 red Goblin. Loses to all three above; only Myconid fails to beat it, and only on the
unconditional average.

## Myconid's +0.0391 is a MIXTURE, not a verdict — stratified 2026-08-26

The user challenged the Myconid loss with card-level reasoning: *"Myconid is advantageous when you
have Mirrorwing rather than Zada or insufficient land in hand"*, and *"the only cases I can see for
[Instigator winning] are where our manabase bites and we have a tapped land on 2."* Per the
`never-report-a-null-unstratified` rule, a ~zero card result is usually two large opposite effects
averaged together — and **all three of the user's mechanisms are confirmed.**

6,000 paired games, base vs myconid, 20-life AND 30-life in ONE pooled queue (`starting_life` is a
per-job field for exactly this), full traces, `scripts/myconid_stratify.py`. Negative = Myconid faster.

| stratum | 20 life | 30 life |
|---|---|---|
| Mirrorwing cast | **−0.0359** (t −3.92) | **−0.0372** (t −3.34) |
| Zada cast | +0.0104 (t 1.50, ns) | **−0.0219** (t −2.34) |
| **no magnet cast** | **+0.4087** (t 10.93) | **+0.3911** (t 8.97) |
| tapped land on T2 | +0.0715 (t 7.03) | +0.0594 (t 5.40) |
| no tapped land on T2 | +0.0330 (t 3.56) | **−0.0247** (t −2.56) |
| opening hand 0–1 lands | +0.0390 (ns) | −0.0864 (ns) |
| opening hand 4+ lands | +0.0908 (t 5.31) | +0.0457 (t 2.19) |
| **ALL** | +0.0455 | **+0.0015 (t 0.20 — a true null)** |

**The headline is driven by one stratum: the 13% of games that never cast a magnet (+0.41).** That
slice alone contributes ~+0.053 to the 20-life average — more than the entire measured effect. In
those games Myconid's ramp has no payoff and a 3-mana 1/2 simply loses a creature race to a 2-mana
1/1-plus-token. Condition on the deck doing what it is built to do and Myconid is neutral-to-better,
and at 30 life the tapped-land-on-2 axis **changes sign** exactly as the user predicted.

Read: Myconid is not "worse than Goblin Instigator". It is a **ramp card whose value is conditional
on reaching a magnet**, and the unconditional average buries that. It still loses to Frontline
Heroism and Nest Invader by margins far larger than any of these conditional swings, so it does not
change the slot recommendation — but "myconid +0.0391, dropped" was the wrong thing to report, and
the stratified table is the right one.

Secondary readings: at 3 Heroism the last slot prefers **Nest Invader over Goblin Instigator**
(−0.3384 vs −0.3162), which reproduces the user's expectation independently of the Heroism result.
Young Pyromancer is not dead weight (`fl2_ni1_yp1` ~ `fl2_ni2`), it is just never better than one
more Heroism.

Apparatus stability check: the stage-2 anchors reproduced stage 1 to within 0.009 (`fl4` −0.3857 vs
−0.3943, `ni4` −0.1302 vs −0.1376) under a *different* pooled `card_scores` union — which is the
reason both stages re-ran the pure arms rather than quoting stage 1's numbers across a spec edit.

## 30 life (user request, 2026-08-26) — same answer, one rank swap

`MTG_START_LIFE=30` with **`max_turns` raised 8 -> 12**: at 30 life the deck needs ~1.5x the damage,
and leaving max_turns at 8 would score most games unwon (max_turns+1) and collapse the metric.
12,000 paired games, seed 960000; held-out confirm at 1460000.

| arm | 20 life | 30 life |
|---|---|---|
| frontline | −0.3943 (held-out −0.4062, pooled **−0.4003**) | −0.3467 (held-out −0.3303, pooled **−0.3385**) |
| nest_invader | −0.1376 | −0.1212 |
| pyromancer | −0.1092 | −0.1417 |
| myconid | +0.0391 | +0.0059 |

* **Frontline Heroism wins at both life totals** by ~3x the next candidate, and the held-out block
  reproduces at both (shrinkage t = −1.34 at 20 life, +1.18 at 30). The recommendation is robust to
  the format.
* Its edge **shrinks slightly** at 30 life (−0.347 vs −0.394) — NOT grows. A 300-game probe had said
  −0.5033 and was reported that way mid-run; its se was ±0.0751 and the direction was noise. Read
  ms/game off a probe, never a delta.
* **Young Pyromancer and Nest Invader SWAP ranks**: NI ahead at 20 life (−0.1376 vs −0.1092), YP
  ahead at 30 (−0.1417 vs −0.1212). Mechanically sensible — Pyromancer's output scales with spells
  cast, so a longer race pays it, while Nest Invader's ramp matters most when the game is decided by
  landing the magnet a turn early. Same effect shows in the mixes: `fl2_ni1_yp1` trails `fl2_ni2` at
  20 life and ties it at 30.
* **Myconid is a true null at 30 life** (+0.0059, t = 1.05), consistent with the stratification
  above: more games reach a magnet at 30 life (no-magnet share 13.0% -> 7.1%), so the one stratum
  that was dragging it down shrinks.

30-life mixes (12,000 paired, seed 970000) reproduce the 20-life ladder **in the same strict order** —
fl4 −0.3362, fl3_ni1 −0.3017, fl3_gi1 −0.2702, fl2_ni1_yp1 −0.2517, fl2_ni2 −0.2509, fl2_gi2 −0.1943,
fl1_ni3 −0.1935, ni4 −0.1187. Monotonic in Heroism count at both life totals; no mix beats the pure
4-of in either. (These 30-life mixes were measured **pre**-cast-order-fix and so understate every
Heroism arm, exactly as the 20-life pre-fix ladder did; the ORDER is what they establish, and the
order is unchanged. They were not re-run post-fix — the 20-life ladder was, and the pure 4-ofs were
at both life totals, which is what the conclusion rests on.)

## Caveats that must travel with these numbers

* **Table-less apparatus.** "No new mulligan profiles" + Rule 0a (the driver's automatic pool table
  is a union DECKLIST) left `"pool_table": false`, so the exhaustive keep table is dropped
  **symmetrically from every arm** (`MTG_EXHAUSTIVE_PROFILE=none`; the play profile is still
  attached). That is ~0.063t of play quality off every arm and ~2,000 ms/game. The RANKING is
  sound; the absolute avg-win-turns are **not** what the deck posts with its shipped table.
* **Floor not measured.** No `--floor` bracket was run (it would generate throwaway R=10 tables,
  against the stated constraint). Margins are quoted against the skill's predicted 0.005–0.01 floor:
  ~40x for the headline, ~5x for the `fl4` vs `fl3_ni1` gap. Both clear the 3x "resolved" bar, and
  the monotonic 8-arm ladder is hard to explain as apparatus noise — but it is a prediction, not a
  measurement.
* **Frontline Heroism's value rests on mechanics implemented the same day** (below). Verified against
  a real game, but it is newer code than the incumbent's.

## Robustness: budget and clairvoyance (user 2026-08-27, "general good practice")

The headline is not an artifact of the search setup. Same specs, four configurations:

| arm | d5/b20 (20,000) | d7/b1000 (1,500) | NC honest-play (6,000) | NC true search K=4/d1 (1,500) |
|---|---|---|---|---|
| **frontline** | **−0.4431** | **−0.4107** | **−0.4338** | **−0.4067** |
| nest_invader | −0.1376 | −0.1240 | −0.1363 | −0.1440 |
| pyromancer | −0.1092 | −0.0940 | −0.1075 | **−0.1587** |
| myconid | +0.0391 | +0.0093 | +0.0300 | **−0.0187** |

Frontline Heroism holds a −0.41..−0.44 band across a **50x budget change** and two flavours of
blinded search. Nothing flips at the top.

**NC modes.** `MTG_HONEST_PLAY=1` is a 1-sample draw-decoupled proxy (~1x cost) and tracks
clairvoyant closely (base 5.0150 vs 5.0273). `MTG_NC_SEARCH=1` with **`MTG_NC_K=4 MTG_NC_DEPTH=1`**
is the true reshuffle-averaged mode at ~5x. Do NOT use the NC defaults (K=8, depth=2):
`ReshuffleAvgChoosePlan` is called WITHOUT the SearchBudget, so depth 2 is unbounded — measured
200–350 s per game (~250x) in the 2026-08-19 NC campaign. True NC is a real handicap: base slows
5.0273 -> 5.3660 and `%ident` falls to 61%.

**CLAIRVOYANCE UNDERVALUES INSURANCE — the prediction was backwards.** The expectation recorded
before the run was that clairvoyance FLATTERS ramp cards (ramping only pays if you know what you are
ramping into), so blinding should hurt Myconid and Nest Invader. The measurement says the reverse:
blinding **helps** Myconid (+0.0391 -> −0.0187, t = −1.13, no longer significantly worse) and lifts
Young Pyromancer past Nest Invader (−0.1587 vs −0.1440, the same swap 30 life produces). The reading
that survives: a mana source is INSURANCE, and a clairvoyant engine systematically undervalues
insurance because it can already see it will not need it. Expect this for any
redundancy-flavoured card measured on this engine — it is not a Myconid quirk.

## Mirrorwing has NO adopted play policy, and that is a pipeline bug

`decks/Mirrorwing Dragon/Mirrorwing Dragon.value.json` has `value_play` with only `mull_gen_*` — no
`target_depth`/`budget_ms`. It is the **only deck of 14** in that state (8 record d5/b20, 4 record
d6/b20–40, Creature Giving records 0/0).

Cause: phase E measured d4/d5/d6 against the shipped default and found all three within noise
(+0.00000/+0.00150/+0.00000), so **nothing was adopted**; phase F then refused to derive `mull_gen_*`
because it had "nothing to reference against". **The pipeline treats "the default won" as "no
policy", where every other deck recorded the winner even when it was the default.** A tie with the
default should still be written down.

Consequences: benign for everything measured here (`deck_compare` pins `depth`/`budget_ms` into every
job, so all screens ran d5/b20 regardless), but NOT benign generally — with `present()` false the
`--depth` conflict guard never arms for this deck, and `MullGenDepth` falls through a different
branch.

### RESOLVED 2026-08-27: recorded as d5/b20, `enabled: false`

User: *"if there are not clearly better settings indicated by the matrix we should write d5 b20."*
The condition holds — phase E (4 seeds x 500 paired games, vs the EMPTY block):

| arm | mean avg | vs dflt | mean cost |
|---|---|---|---|
| dflt (empty) | 4.9170 | — | 692k ms |
| d4 | 4.9170 | +0.0000 | 717k |
| d5 | 4.9185 | +0.0015 | 741k |
| d6 | 4.9170 | +0.0000 | 727k |

Written: `target_depth: 5, budget_ms: 20, enabled: false`. Verified byte-identical (smoke 42/42, all
three mirrorwing digests unchanged).

**`escalation_cap` deliberately omitted.** The sweep's `pd5` arm carried `escalation_cap: 5`, and
that — not depth — is why its play digest differed from the empty arm. `BuiltinDefaultPlay()` IS
d5/b20, so the deck was always playing at depth 5; the label was never wrong. That arm measured
marginally worse (+0.0015) and ~7% costlier, so the cap is not adopted.

**`enabled: false` is deliberate, and `true` was TRIED and rejected.** It is not byte-identical:

```
AIEngine.cpp:2097   const bool vp_beam = m_profile.value_play.drives();
```

The escalation BEAM fires whenever a block DRIVES — **including at the off-policy depths the
regression suite pins for its d3/d0 sanity cases** (those pass `ignore_play_profile: true`, which
bypasses the depth LOCK but not this). Smoke moved `mirrorwing_smoke_d3_s1001` 4.6733 -> 4.7000
(3 games slower, 1 play-changed) while d0/d5 stayed identical. Since the matrix says no depth beats
the default, enabling would buy a GT rebaseline and an adverse off-policy play change for no gain.
Flip to `true` only with a deliberate GT re-accept of the d3 keys.

### OPEN, REPO-WIDE: the off-policy escalation beam

The comment at that line justifies firing the beam off-policy as *"at a shallow off-policy depth it
switches to the wide static leaf beam (measured neutral+faster)"*. On Mirrorwing it was **slower**.
**All 11 other decks ship `enabled: true`**, so every one of them is running that beam on its d3/d0
regression sanity cases. If "neutral+faster" is as deck-dependent as this one data point suggests,
those sanity cases may be measuring a different search than intended. Not investigated — it is a
repo-wide question, independent of this slot test.

It also means pinning d7/b1000 above is *not* the confounded "sweep depth alone" the
deck-screening skill warns about — that warning is about decks whose `value_play` is a jointly
fitted (depth, budget, escalation_cap, value_trust_depth) unit. Mirrorwing has no such fit to break.

## Card implementations

All four candidates were absent from `cards.json`. Oracle text came from XMage via `gh api`
(Scryfall 429-blocks this container — see the `card-data-via-xmage-not-scryfall` note). **Two
candidates were materially different from naive recall**, which changed what the test means:

* **Frontline Heroism** `{2}{R}` — an enchantment by card type, but **functionally a creature
  source**: it produces bodies, which is the role this slot is being asked to fill (user, 2026-08-26:
  *"Frontline heroism is a 'creature' because it produces them."*). ETB: 1/1 red Soldier with haste.
  *Whenever you cast a spell that targets only a single creature you control, create a 1/1 red
  Soldier with haste, then copy that spell; the copy targets that token.*

  **It is NOT a Zada** (user, 2026-08-26: *"It isn't a Zada." / "It does give you 1 extra, not the
  full board."*). A magnet copies the spell for EVERY other creature you control; Heroism makes ONE
  token and ONE copy. Verified by instance count (`cast_lifegain` fires once per payload instance,
  so Oracle's Restoration counts them exactly — fixtures `logs/cardcheck/scen/cnt_*.spec`):

  | board | payload instances |
  |---|---|
  | Elvish Mystic alone | 1 |
  | Elvish Mystic + Heroism | **2** (exactly +1) |
  | Zada + 2 creatures | 3 |
  | Zada + 2 creatures + Heroism | **5** |

  The magnet case is +2 rather than +1 because the Soldier is a NEW BODY that the magnet then fans
  onto as well — +1 copy from Heroism, +1 fan target from the extra creature. That depends on
  trigger order (CR 603.3b, controller's choice), and **the user ruled Heroism-first / Zada-second
  is correct** (2026-08-26), which is the shipped default. `MTG_FRONTLINE_FIRST=0` takes the other
  order (4 instances) and is retained purely as an attribution lever; it was never used to decide
  anything.
* **Undercellar Myconid** `{2}{G}` 1/2 — a **three-drop**, ETB *or dies* → 1/1 green Saproling, plus
  `{T}: Add one mana of any color`.
* **Nest Invader** `{1}{G}` 2/2 — ETB 0/1 Eldrazi Spawn with "Sacrifice this creature: Add {C}".
* **Young Pyromancer** `{1}{R}` 2/1 — cast an instant/sorcery → 1/1 red Elemental.

New params, all additive and default-off (`cast_trigger_instant_sorcery_tokens`,
`sac_for_mana_color`, `created_token_haste`, `frontline_copy_tokens`, plus a `haste` arg on
`CreateToken`). Three implementation decisions worth recording:

1. **The Eldrazi Spawn is a NAMED TOKEN DEF** (`"0/1 Eldrazi Spawn Token"`, the Treasure Token
   precedent): `CreateToken` names tokens `"<P>/<T> <first subtype> Token"`, and `LookupCached`
   resolves that name, which is what makes the sac ability live.
2. **`sac_for_mana_color: "C"` pins the Spawn to colourless.** The pre-existing pay-sac machinery
   models "add one mana of ANY colour" (Treasure/Lotus). Left unpinned, a Spawn would pay `{R}` and
   `{G}` pips in a two-colour deck — materially **flattering the very card the user expected to
   win**. Pinned at three sites: `EffectiveProduces` (pay-sac path), the enumeration fan, and
   `SacFloatColorFor` (which already honours a pin). Verified by a discriminating scenario pair in
   `logs/cardcheck/scen/`: it **cannot** pay `{G}`, and it **can** pay a generic pip.
3. **Rollout lockstep for noncreature ETBs.** `TurnSolver`'s non-creature permanent branch fired
   `OnDragonEnters` but not `OnGoblinEnters`, so an enchantment's ETB token would exist in the real
   game and not in the projection (an fd-diverge of the Puresteel shape). Now fires both. Provably
   inert for existing decks: all 8 cards carrying a cascade param are instants/sorceries, which never
   reach a permanent-enter branch.

## The heuristic update, and which part of it actually mattered

User, 2026-08-27: *"let's try to update all of the heuristics to take the new cards into account.
They should probably all go into the creature bucket, counting as 2 critters."* Implemented as ONE
property-keyed helper, `MwBodyCount()` (DecisionProviders.cpp), used at five sites — cast-order
camp, cast-order rank, discard census, discard weight, discard keep-rank. Every candidate scores
exactly 2; nothing else in the deck moves; smoke 42/42 play-changed=0.

**Of the five sites, ONE did all the work: cast order.** Re-measured on the original specs/seeds:

| arm | before | after | Δ |
|---|---|---|---|
| frontline, 20 life | −0.3943 | **−0.4431** | +0.0488 |
| frontline, 30 life | −0.3467 | **−0.4420** | +0.0953 |
| nest_invader / myconid / pyromancer | — | **byte-identical** | 0 |

### The cast-order gap

`MirrorwingProvider::CastEnablerFirst` and `CastOrderRank` both key on `IsCreature()`. Frontline
Heroism is an ENCHANTMENT, so it matched no Mirrorwing rule and fell through to
`GenericProvider::CastOrderRank`, which buckets "other noncreature spells" at **20 — behind every
trick in the deck**. Heroism only produces for spells cast AFTER it resolves, so every trick ahead
of it was one Soldier and one copy discarded. Measured incidence on 120 logged games: 52% of games
cast Heroism, and 22 of those turns cast a trick alongside it (36 tricks stranded, ~0.35 affected
turns per Heroism game) — i.e. the gap could bite on ~18% of games, which is the right order of
magnitude for the 0.05–0.10 it turned out to be worth.

Ranked **6** (user 2026-08-27: *"before all tricks, but it can go after magnets"*). An intermediate
fix at 11 — Luxurious Libation's body-maker slot — was WRONG and was caught by the user: Libation
(11) and Twinflame (12) are themselves tricks, so Heroism would still have forfeited those two. Only
the magnet (5) may precede it.

**Two earlier claims in this document were wrong and are retracted:**

* *"Frontline Heroism's edge shrinks at 30 life (−0.347 vs −0.394)"* — that was the cast-order gap
  biting HARDER in longer games (more turns, more tricks stranded), not a format effect. Corrected,
  the edge is flat: −0.4431 at 20 life, −0.4420 at 30. Any story about the effect being
  race-length-sensitive should be discarded.
* *The name-keyed `"Goblin Instigator"` discard bias "would have rigged the comparison"* — it would
  not have. See below: that decision is nearly inert for this deck.

### The discard rules are inert — three independent confirmations

`body_weight` / `keep_rank` / the census live in `MirrorwingProvider::CleanupDiscardCandidates`,
which is the CLEANUP DISCARD (end-of-turn, hand over seven) — **not** mulligan bottoming, as an
earlier revision of this document called it. Its own comment says why: *"The deck rarely discards
(0.13 mean label regret in the discard-analysis stage)."* Confirmed three ways:

1. `MTG_FRONTLINE_BODY` A/B, 12,000 paired games: **identical to the digit** (base 5.0312, fl4
   4.6373, −0.3939, 69.0% ident both arms). Zero games changed.
2. Young Pyromancer's weight went 1 -> 2 in the update above and it measured **byte-identical**.
3. The de-naming fix itself was byte-identical on every shipped deck (smoke, twice).

So the de-naming was right in principle and worth keeping — it removes a real name-keyed bias — but
it was not load-bearing. The lesson is that the site worth auditing for a new card is the one that
runs every turn (cast order), not the one that reads well in review.

> **CORRECTION 2026-08-27 (user: "did we not do the same games already?").** The heading above
> over-claims and should be read narrowly. All three confirmations varied something *inside* the
> hook — `MTG_FRONTLINE_BODY` (body weight 1 vs 2), Pyromancer's weight, the de-naming fix — and all
> three are on the **body-census** side. **None of them turned the hook off**, and none touched the
> `kOneIsEnough` clause (`{ Twinflame, Luxurious Libation }`, which sheds extra copies as dead).
> What was actually established is that three specific edits were byte-identical, which is
> *evidence* the hook rarely fires but is not a measurement of it. The direct test is
> `MTG_MW_BUCKET_DISCARD=0`, which drops the whole hook to `GenericProvider` — and it had never
> been run against any comparison until the Libation ladder's stage 3.

## The name-keyed bias that would have rigged the comparison

The Mirrorwing provider's bottoming heuristic hardcoded `"Goblin Instigator"` twice —
`body_weight` (Instigator counts as 2 bodies toward the 4-weighted-bodies target) and `keep_rank`
(preferred over other non-dork bodies when shedding to 2). Both encode **"this card brings a second
body"** — a property Nest Invader, Undercellar Myconid and Frontline Heroism *also* have. Left alone,
the screen would have measured the NAME, not the card, and marked every newcomer down to weight 1.

Both are now keyed on `etb_self_creates_tokens` instead. Byte-identical on the shipped decklist
(Instigator is a creature (1) whose ETB makes one token (+1) = 2, exactly the old constant), and
Instigator is the only ETB-token maker in it.

Separately, Frontline Heroism being an enchantment fell into **no hand-census bucket at all** — not
magnet, not mana, not body, and `pumps` is a named list. The code's own `gi295` note says an
unlisted card hands the decision to the shared highest-MV fallback, which sheds the magnet. The
census now admits a noncreature permanent whose ETB makes a token, so Heroism is named in the shed
list.

`bash test/regression.sh --smoke` after all of the above: **42/42 pass, play-changed = 0.**

# FINAL SCREEN RESULTS (17 cells, 20,000 paired, seed 6,500,000 — landed 2026-08-28 18:51)

| arm | E | L | A | Or | GR | delta |
|---|---|---|---|---|---|---|
| **`ie0_an4`** | **0** | 0 | **4** | 3 | 4 | **−0.5422** |
| `or4_ie1_an2` | 1 | 0 | 2 | **4** | 4 | −0.5390 |
| `ie1_an3` | 1 | 0 | 3 | 3 | 4 | −0.5316 |
| `or4_ie1_lib1_an1` | 1 | 1 | 1 | 4 | 4 | −0.5272 |
| `or4_ie2_an1` | 2 | 0 | 1 | 4 | 4 | −0.5249 |
| `or4_ie3` | 3 | 0 | 0 | 4 | 4 | −0.5225 |
| `ie2_an2` | 2 | 0 | 2 | 3 | 4 | −0.5214 |
| `ie2_lib1_or4` | 2 | 1 | 0 | 4 | 4 | −0.5211 |
| `or4_ie1_lib2` | 1 | 2 | 0 | 4 | 4 | −0.5148 |
| `an1_ie3` | 3 | 0 | 1 | 3 | 4 | −0.5140 |
| `ie3_lib1` | 3 | 1 | 0 | 3 | 4 | −0.5050 |
| `cur` (anchor) | 4 | 0 | 0 | 3 | 4 | −0.5050 |
| `ie2_lib2` | 2 | 2 | 0 | 3 | 4 | −0.5043 |
| `or4_gr3` | 4 | 0 | 0 | 4 | **3** | −0.5042 |
| `an1_gr3` | 4 | 0 | 1 | 3 | **3** | −0.4924 |
| `gr2_an2` | 4 | 0 | 2 | 3 | **2** | −0.4847 |

Winner `ie0_an4` held out: screen −0.5422, held-out (seed 7,000,000) −0.5357, shrinkage +0.0065 ±
0.0103 (t = +0.63). **Pooled −0.5390 over 40,000 games.**

## Three pre-registered questions, three clean answers

**1. Gold Rush — the premise, VINDICATED.** The user predicted *"I would be shocked if any of our
marginal cards could beat it."* Every Gold Rush cut loses, and `gr2_an2` is the **worst arm in the
screen**: GR3+Oracle4 +0.0008, GR3+Anger1 +0.0126, GR2+Anger2 +0.0203. After being an untested
premise of fifteen screens, Gold Rush 4 is now measured and correct.

**2. Libation — the decision rule fires.** `ie3_lib1` is **−0.5050 against `cur`'s −0.5050**: dead
parity with Impolite Entrance, to the digit. `ie2_lib2` is +0.0007. The pre-registered rule was
*"if libation is very close to or worse than Entrance/Anger, it should probably be dropped, because
it lacks trample"* — parity, and Entrance itself loses to Anger. **Drop confirmed**, and on the
weaker of the two comparisons rather than the stronger.

**3. Entrance → Anger — the prediction FAILED, monotonically.**

| rung | delta | vs `cur` |
|---|---|---|
| E4/A0 (`cur`) | −0.5050 | — |
| E3/A1 | −0.5140 | −0.0090 |
| E2/A2 | −0.5214 | −0.0164 |
| E1/A3 | −0.5316 | −0.0266 |
| **E0/A4** | **−0.5422** | **−0.0372** (t = −15.5) |

Straight line to **zero Entrance**. The pre-registered prediction was an interior optimum at 1–3,
reasoned from Entrance's diminishing returns (one cast hastes the whole board) against Anger's
graveyard escalation. The *diminishing* half was right — but it turns out Entrance's haste is worth
so little in this deck that even the first copy loses to a 4th Anger.

**And the pre-registered escape hatch does not apply.** This document said in advance that a monotone
run to `ie0_an4` should make one *"suspect the measurement, since it would mean haste is worth nothing
at all."* The **board-exposure proxy was run precisely to test that**, and it says haste really is
worth ~nothing here:

| arm | exposure turns | peak exposed | nowhere-kill |
|---|---|---|---|
| `cur` (Entrance 4) | 2.60 | 2.47 | 19.9% |
| `ie0_an4` (Entrance 0) | **2.57** | 2.44 | **19.9%** |

Removing *every* haste source changes board exposure by 0.03 turns and the out-of-nowhere kill rate
**not at all**. The mechanism is the one already established: Frontline Heroism's Soldiers enter
hasted and are now the deck's entire token supply, so Entrance's haste had already lost its consumer.
The suspicion was pre-registered, tested, and did not survive.

# ADOPTION (user 2026-08-28: adopt as the next version, retire the current as `v2-<name>`)

## Card modelling is VERIFIED — three new fixtures, all passing

> USER: *"I would also like to run some games with it to ensure newer cards are modeled correctly."*

This is also what mulligan-profile **Rule 0** requires before spending rollout hours ("cards
implemented, reviewed, and play validated"). Frontline Heroism had **no committed scenario test** —
it is worth +0.45 avg win turn, 89% of the whole campaign, and nothing in `test/scenarios/` guarded
it. Three fixtures now do, each with *exactly-lethal* math so no failure mode can pass:

| fixture | guards | why it cannot pass by accident |
|---|---|---|
| `frontline_etb_soldier_haste` | ETB token **and its haste** | board is three lands and **no creatures**; the ETB token is the only possible damage. No token, or a token without haste → 0 damage |
| `frontline_cast_token_copy` | cast trigger makes a Soldier **and copies the spell onto it** | Hierarch 1 + Soldier 2 = 3 = `opponent_life`. No trigger → 1; token but no copy → 2; copy but no haste → 1 |
| `frontline_magnet_double_payload` | under a magnet the Soldier takes **TWO** payloads | Zada 4 + Soldier 3 = 7 exactly. One payload → 6; no Heroism → 4; Heroism wrongly a creature → overshoots |

All three **PASS**, and the `active_life` assertions independently pin the *instance counts* rather
than just the damage: **22** = exactly two Oracle instances resolved (original + Heroism's copy),
**23** = exactly three (original + Heroism's copy + Zada's copy). That is the double-payload claim
confirmed by a second, independent quantity.

They live in `test/scenarios/`, which `test/regression.sh` already runs as its scenario sanity gate —
so they now guard every future smoke and regression run, not just this adoption.

## Adoption checklist — what is done, and what needs the user

| step | state |
|---|---|
| candidate list staged at `decks/Mirrorwing Dragon/v3-heroism-draught/` | **done**, 60 cards verified |
| card-modelling fixtures written + passing | **done** (3 above) |
| rebuild + smoke after the queue drains | **queued** (`logs/deckcmp/postqueue.sh`) |
| commit the engine work (3 commits) | ready to stage; **not committed** |
| push + watch Windows CI | **needs user** — outward-facing |
| swap the primary `.cod`, archive current as `v2-instigator-libation` | **needs user** — see below |
| ground-truth rebaseline | **needs user** — never an agent's call |
| mulligan profile + value leaf on the new list | after the commit freeze |

**Why the swap is not an agent action.** `test/regression_cases.sh:31` and `:47` point at
`decks/Mirrorwing Dragon/Mirrorwing Dragon.cod` and its `.profile.json`, so replacing that file
changes what the regression harness measures and forces a **GT rebaseline** — which the regression
skill reserves for the user, and which would also mask any real regression from the engine changes
if done before the smoke.

**Two existing fixtures break on the swap** (checked, not discovered later):

| fixture | missing card | fix |
|---|---|---|
| `draught_magnet_escalation.json` | Goblin Instigator | repoint at the archived `v2-` deck; it guards Draught's escalation, and Draught is still a 4-of, so a new-list version is also worth having |
| `libation_x_lands_not_dorks.json` | Luxurious Libation | repoint at the archived `v2-` deck — it guards a card the primary list no longer plays, but the card remains in `cards.json` |

## Reproduce

```
logs/deckcmp/mirrorwing_instigator_slot.json    # stage 1: pure 4-ofs, seed 930000
logs/deckcmp/mirrorwing_instigator_mixes.json   # stage 2: mixes,      seed 940000
python3 scripts/deck_compare.py <spec>                     # screen
python3 scripts/deck_compare.py <spec> --confirm frontline # held-out block
```

## Open

* The floor is predicted, not measured (above).
* Nothing here is adopted. A chosen combination still owes its own mulligan table and value leaf via
  `mulligan-profile.md` / `value-leaf.md`, and its own regression ground truth — the screen's number
  is a RANKING, not that deck's measured strength.
* Frontline Heroism has no archetype heuristic naming it (the pre-flight says so). It is played
  generically; a provider tweak is the likely next gain if it is adopted.
* Not tested: whether MORE than 4 Heroism is better, which would mean cutting outside the slot.
  Monotonicity up to 4 makes that the obvious next question, but it was outside the asked scope.

---

# The spell suite on top of 4 Heroism (screen queued 2026-08-27)

The deck owner's follow-up, in their words: **"Do we need more draw, and is Fists of Flame doing
its part in the current list?"** — with the named swaps (Fists → Draught/Oracle, Draught → Oracle/
Anger) explicitly *falling out of* those two questions rather than being the list to run.

Spec `logs/deckcmp/mirrorwing_spells.json`, 16,000 paired games, seed 1,500,000, confirm at
2,000,000. Chained behind the slot run's `finish.sh` (`logs/deckcmp/spells.sh`, waits on the PID —
`deck_compare` holds a per-deck lock, so overlapping would abort anyway).

Every arm plays Goblin Instigator 0 / Frontline Heroism 4, so `fl4` is the anchor and the pairwise
matrix reads arm-vs-arm directly. `base` (the shipped Instigator list) is still measured, which
**re-checks the slot result on a third seed block for free** — so if the running confirm were to
move, this screen would say so independently.

The flex spells, and which of them draw:

| card | cost | MV | cantrip? | current |
|---|---|---|---|---|
| Fists of Flame | `{1}{R}` | 2 | **yes** | 4 |
| Impolite Entrance | `{R}` | 1 | **yes** | 4 |
| Oracle's Restoration | `{G}` | 1 | **yes** | 2 |
| Ancestral Anger | `{R}` | 1 | **yes** | 0 |
| Gold Rush | `{1}{G}` | 2 | no | 4 |
| Luxurious Libation | `{X}{G}` | 1 | no | 3 |
| Fortifying Draught | `{G}` | 1 | no | 2 |

Draught is the only non-cantrip the owner named, which is why it is the draw axis's donor.

## Q2 — a Fists ladder under three fillers

4 → 3 → 2, three times, so the answer cannot be an artifact of what replaced it:

| filler | arms | cards drawn | what it isolates |
|---|---|---|---|
| Oracle's Restoration (cantrip) | `fi3_or3`, `fi2_or4` | **held at 10** | Fists' pump quality alone |
| Ancestral Anger (cantrip) | `fi3_an1`, `fi2_an2` | **held at 10** | ditto, at 1 mana instead of 2 |
| Fortifying Draught (non-cantrip) | `fi3_dr3`, `fi2_dr4` | falls 10 → 9 → 8 | Fists as a draw engine |

If Fists is earning its slot, all three ladders slope the same way. If only the Draught ladder
slopes, Fists is being paid for its *cantrip*, not its pump — and a cheaper cantrip would do.

## Q1 — a draw ladder at Fists 4

| arm | Draught | Oracle | Anger | cards drawn |
|---|---|---|---|---|
| `fi2_dr4` (other direction) | 4 | 2 | 0 | **8** |
| `fl4` (current list) | 2 | 2 | 0 | **10** |
| `dr1_or3` | 1 | 3 | 0 | **11** |
| `dr0_or4` / `dr0_an2` / `dr0_or3_an1` | 0 | 4 / 2 / 3 | 0 / 2 / 1 | **12** |
| `draw_max` | 0 | 4 | 1 | **13** (also Libation 3 → 2) |

`draw_max` is the one arm that goes past the named swaps — one step further on the same axis, since
the question is about the axis and not the examples. The three ways of reaching 12 separate "more
draw" from "more *green* draw": `dr0_an2` buys it in red, which matters on a `{G}`-heavy mana base.

**The two axes interact by design.** Fists pumps `+1/+0` per card drawn *this turn*, so every cantrip
added makes each remaining Fists bigger. Cutting Fists for cantrips therefore trades payoff size for
payoff frequency, and the ladder is what prices that trade.

## Apparatus: this screen is name-blind, and that is checkable rather than assumed

The slot screen had to caveat a *predicted* bias floor. For these edits the caveat is much
weaker, and for a reason that can be read straight off the profile and the engine:

* `hand_score_threshold` is **`-1e18`** (`NO_GATE`), so `ComputeHandScore`'s result is compared
  against negative infinity and every hand passes. **The `card_scores` keep gate is dead on this
  deck** — which also means the pooled entry for Ancestral Anger cannot move the keep decision, and
  the Fists second-copy score (`+0.0401`, the only non-clamped entry among the edited cards) is
  inert.
* `required_pieces` is `[]` and `min_color_sources` is `{}` — **no profile field names any card.**
* `curve_check: two_drop` requires `land_count >= 2 && count_mv2 >= 1`, and `count_mv2` counts
  **MV ≤ 2, which includes MV ≤ 1** (`AIEngine.cpp:684`). Fists is MV 2; Oracle, Anger and Draught
  are all MV 1. **Every swap in this screen stays inside the same MV class, so the curve check
  cannot distinguish any arm from any other.** (Contrast the slot screen, where Instigator MV 2 →
  Heroism MV 3 did leave the class — a small real effect there, exactly zero here.)
* All arms keep 21 lands.

What is left is `bottom_order: count_first`, which does read `CardScore` — and that is the one place
the pooled Anger entry matters. Bottoming only runs after a mulligan (`stop_at: 4`).

## Cast order was checked BEFORE launching, not after

The lesson from the slot screen was that Frontline Heroism sat at the generic rank-20 fallback,
behind every trick it existed to copy, and that cost 0.05–0.10 — found only after the numbers were
in. So the ranks for every card in this screen were traced through `MirrorwingProvider::
CastOrderRank` (`DecisionProviders.cpp:9358`) first:

| card | rank | via | correct? |
|---|---|---|---|
| Fists of Flame | **16** | `pump_per_cards_drawn_power` | yes — after every draw it counts |
| Oracle's Restoration | **14** | `cast_draw` | yes — draws precede Fists |
| Ancestral Anger | **14** | `cast_draw` | yes — named in the comment |
| Fortifying Draught | **18** | `pump_per_life_gained_power` | yes — last, so X counts prior gains |

No card falls to the generic 20. The order already encodes both synergies the screen is about:
cantrips resolve before Fists (so Fists counts them) and Draught resolves last (so it counts
Oracle's life riders). The arms are being measured under a policy that plays them properly.

`PromoteCantripsInCastOrder` (`:9429`) is a draws-first promotion that would bear directly on the
draw question — it is **superseded and returns false** under `MTG_MW_ORDERED`, which is on. Its own
comment records why the mv-1 bar matters: at mv 2 the promotion also catches Fists, "the deck's
PAYOFF", and mirrorwing d0 went −0.0160 → +0.0020. That is prior evidence, on this deck, that
**casting Fists early is worth about 0.018** — a useful sanity anchor for Q2.

## The one thing to check when the numbers land

`MirrorwingProvider::CleanupDiscardCandidates` (`:9547`) ranks the flex spells by name — Gold Rush,
then **Draught 2nd** ("our second-best spell", the owner's call), Twinflame, Libation, **Fists 5th**,
**Anger 6th**, **Oracle 7th**. It was proven inert three ways for the *current* list, because the
deck empties its hand and cleanup discard needs hand > 7.

**But the arms that add draw are precisely the ones most likely to wake it up.** An inertness proof
on a 10-cantrip list does not transfer to a 13-cantrip one. If the draw ladder turns non-monotonic
at the top end, this rule is the first suspect and not the deck's opinion about draw. Do not read
`draw_max` without checking it.

## Modelling notes that must travel with the result

* **Trample is inert** — the goldfish opponent never blocks, so Fists' and Anger's "gains trample"
  is not modelled. Both sides of the Fists ladder lose it equally, but it means the screen prices
  Fists purely as *cantrip + escalating `+1/+0`*. Against real blockers Fists is worth more than
  this screen can see, and that is a one-directional understatement of the incumbent.
* **Oracle enables Draught.** Oracle's per-copy `+1` life feeds `life_gained_this_turn`, so under a
  magnet fan-out N Oracle instances start Draught's X at N+2 instead of 2. `dr0_or4` is therefore
  not simply "more draw" — it removes the payoff that Oracle's life rider was feeding. The split
  arms exist to catch that.
* **Anger scales with its own graveyard** (X = 1 + copies in yard), so at 1–2 copies it is close to
  a bare 1-mana cantrip `+1/+0`. It is not being measured at its ceiling.
* Sorcery-vs-instant is unmodelled (everything resolves in MAIN_1), which flatters the two sorceries
  here — Oracle's Restoration and Ancestral Anger — against the instant Fists and Draught.

## RESULTS (16,000 paired, seed 1500000; landed 2026-08-27 16:31)

| arm | Fi | Dr | Or | An | Lib | drawn | delta | Fi+Dr |
|---|---|---|---|---|---|---|---|---|
| `fi2_dr4` | 2 | 4 | 2 | 0 | 3 | 8 | **−0.4504** | 6 |
| `fi3_dr3` | 3 | 3 | 2 | 0 | 3 | 9 | **−0.4474** | 6 |
| `fl4` (current) | 4 | 2 | 2 | 0 | 3 | 10 | **−0.4436** | 6 |
| `dr1_or3` | 4 | 1 | 3 | 0 | 3 | 11 | −0.4369 | 5 |
| `fi3_or3` | 3 | 2 | 3 | 0 | 3 | 10 | −0.4340 | 5 |
| `draw_max` | 4 | 0 | 4 | 1 | 2 | 13 | −0.4330 | 4 |
| `fi3_an1` | 3 | 2 | 2 | 1 | 3 | 10 | −0.4271 | 5 |
| `dr0_or4` | 4 | 0 | 4 | 0 | 3 | 12 | −0.4213 | 4 |
| `fi2_or4` | 2 | 2 | 4 | 0 | 3 | 10 | −0.4207 | 4 |
| `dr0_an2` | 4 | 0 | 2 | 2 | 3 | 12 | −0.4153 | 4 |
| `dr0_or3_an1` | 4 | 0 | 3 | 1 | 3 | 12 | −0.4151 | 4 |
| `fi2_an2` | 2 | 2 | 2 | 2 | 3 | 10 | −0.4079 | 4 |

Winner `fi2_dr4` held out: screen −0.4504 ± 0.0074, held out (seed 2,000,000) −0.4531 ± 0.0073,
shrinkage −0.0027 ± 0.0104 (t = −0.27). Pooled −0.4517 over 32,000 games.

**Scale first: the whole 12-arm spread is 0.0425, an order of magnitude below the Heroism slot's
0.44.** No spell configuration tested is anywhere near as important as the slot decision was.

### Q1 — do we need more draw? NO, and the ladder is monotone

At Fists 4, walking Draught 2 → 1 → 0 into cantrips:

| cards drawn | arm | delta |
|---|---|---|
| 10 | `fl4` | **−0.4436** |
| 11 | `dr1_or3` | −0.4369 |
| 12 | `dr0_or4` | −0.4213 |

−0.0223 over two added cantrips (t = −10.6) — comfortably the largest clean effect in the screen and
far above any plausible floor. All three routes to 12 draws land together (−0.4213 / −0.4153 /
−0.4151), so it is not about *which* cantrip: green, red, or split, more draw is worse. And the two
fastest arms in the whole screen are the two with the **least** draw (9 and 8).

The mechanism is not that draw is bad, it is that **the only thing we could cut to buy draw was
Fortifying Draught, and Draught is one of the two best cards in the deck.** Draw is not this deck's
constraint; at 10 cantrips in 39 spells it already has plenty.

### Q2 — is Fists doing its part? YES — and the naive read says the opposite

Regressing delta on Fists count **alone** gives R² = 0.000 (coef +0.0002/copy) — "Fists doesn't
matter". That is a suppression artifact, and it is exactly why the ladder was run under three
fillers: cutting Fists *always* added something, so the univariate coefficient measures the swap, not
the card. Control for the filler and the picture inverts:

| variable | coef per copy | reading |
|---|---|---|
| Fists of Flame | **−0.0163** | faster |
| Fortifying Draught | **−0.0144** | faster |
| Oracle's Restoration | +0.0074 | slower |
| Ancestral Anger | +0.0122 | slower |

(`Fists + Draught` two-variable fit, R² = 0.807, resid sd 0.0067.)

Both **cantrip-filler ladders — the ones that hold cards-drawn constant at 10 — slope against
cutting Fists**, which is the designed test:

| filler | Fists 4 → 3 → 2 | total |
|---|---|---|
| Oracle | −0.4436 → −0.4340 → −0.4207 | **−0.0229** (t = −10.9) |
| Anger | −0.4436 → −0.4271 → −0.4079 | **−0.0357** (t = −17.0) |
| Draught | −0.4436 → −0.4474 → −0.4504 | +0.0068 (t = +3.2) |

At equal draw, **Fists' pump beats both Oracle's flat +1/+1 and Anger's +X/+0.** The single card that
can take a Fists slot without loss is Fortifying Draught — because Draught is the other best card,
not because Fists is replaceable.

### The organizing variable is Fists + Draught count

One variable, R² = **0.792**, −0.0141 per copy — and it orders the tiers exactly:

* **Fi+Dr = 6** → the top three arms, and they are a **three-way statistical tie**
  (spread 0.0068; best-two `fi2_dr4` vs `fi3_dr3` = −0.0029 ± 0.0021, t = −1.41)
* **Fi+Dr = 5** → the middle
* **Fi+Dr = 4** → the bottom

The deck does not care how the six slots are split between Fists and Draught. It cares that there are
six of them.

### Recommendation: leave it at Fists 4 / Draught 2

`fi2_dr4` beat `fl4` by 0.0068 (t = 3.2), but that sits **inside the unmeasured apparatus floor
scale** — brackets on other decks measure 0.005–0.010, and the reweighted (cheap, deterministic)
bracket route is unavailable here because the arms introduce cards. `fl4` vs `fi3_dr3` is t = 1.8 and
`fi3_dr3` vs `fi2_dr4` is t = 1.4. The current list is one of three tied configurations, so there is
**no resolved reason to change it**. If the owner wants to move anyway, the only direction with any
support is *more Draught*, never *more draw*.

**Ancestral Anger is out of contention.** It is the strongest single-variable predictor in the screen
(R² = 0.495) and it points the wrong way: +0.0122 per copy. `fi2_an2` is the slowest arm tested.

### Incidental — the 3rd Luxurious Libation may be the real weak card, and it was not the target

`draw_max` beat `dr0_or4` by **+0.0117 (t = 5.6)**. The two differ by *two* changes: Libation 3 → 2
and Anger 0 → 1. Anger costs ≈ +0.0122/copy, so backing that out puts the Libation cut at roughly
**−0.024** — larger than anything this screen was built to find. It agrees with two independent prior
statements: `CleanupDiscardCandidates` already treats Libation copies beyond the first as **dead**
("bought for the extra bodies... a second copy adds more of what the first already supplied"), and
the owner's own 2026-08-18 note that Libation is "mainly good for extra creatures, but you don't
really need multiple".

This is **a hypothesis, not a result** — one unreplicated contrast, inferred through a fitted
coefficient. It is the obvious next screen (a clean Libation 3 → 2 → 1 ladder, nothing else moving).

**It also marks a design error of mine:** `draw_max` changed the Libation count as well as the draw
count, so as a point on the draw ladder it is unusable. The draw conclusion rests on
`fl4`/`dr1_or3`/`dr0_or4`, which are clean; `draw_max` should have been two arms.

### The cleanup-discard risk did not materialise (but is not fully excluded)

The pre-registered worry was that the 12–13-cantrip arms would push hands over 7 at cleanup and wake
the name-keyed discard ranking, which was only ever proven inert on the 10-cantrip list. Predicted
symptom: non-monotonicity at the top of the draw ladder. Non-monotonicity **did** appear — `draw_max`
(13 draws) beat all three 12-draw arms — but it is fully accounted for by that arm's Libation cut, so
it is not evidence the rule fired. Because `draw_max` confounds two changes it cannot *exclude* the
rule either. The clean 10/11/12 ladder is monotone, which is the reassuring signal.

### Oracle's Restoration beats Ancestral Anger — all four matched pairs

The owner expected this (2026-08-27). Confirmed, in every pair that differs *only* by the
Oracle/Anger swap (negative = Oracle faster):

| matched pair | edge | t |
|---|---|---|
| Fists 3, 1 slot: `fi3_or3` vs `fi3_an1` | −0.0069 | −3.3 |
| Fists 2, 2 slots: `fi2_or4` vs `fi2_an2` | −0.0128 | −6.1 |
| Draught 0, 2 slots: `dr0_or4` vs `dr0_an2` | −0.0060 | −2.9 |
| Draught 0, 1 slot: `dr0_or4` vs `dr0_or3_an1` | −0.0062 | −3.0 |

Consistent in sign and roughly proportional to the number of slots swapped. Neither card is *good*
here — both are positive-coefficient, i.e. both are worse than Fists or Draught — but between the
two, Oracle is clearly the better card.

---

# Determining the Libation case reliably (screen queued 2026-08-27 20:52)

Spec `logs/deckcmp/mirrorwing_libation.json`, 10 arms x 20,000 paired, seed 2,500,000, confirm at
3,000,000. Chain `logs/deckcmp/libation.sh`.

The spell screen's Libation number was **inferred, not measured**: `draw_max − dr0_or4` = +0.0117,
but those arms differ by *two* changes (Libation 3 → 2 and Anger 0 → 1), so the Libation half only
came out by subtracting Anger's fitted coefficient. Three things fix that.

**1. One change per arm, three fillers of known and differing sign.** The filler is the confound —
that is what the Fists ladder taught. So the ladder runs three times, against cards whose values the
previous screen already measured:

| filler | value/copy | what a win means |
|---|---|---|
| Fortifying Draught | −0.0144 (good) | weakest evidence — the gain could be the filler |
| Oracle's Restoration | +0.0074 (mildly bad) | Libation is worse than a card we know is bad |
| Ancestral Anger | +0.0122 (worst tested) | **strongest** — Libation loses to the worst card in the pool |

Cutting Libation winning under the Oracle *and* Anger fillers cannot be a filler artifact, because
both fillers are known to be net-negative themselves. Anger is also the only filler with headroom
(0 → 4) to carry a full Libation 3 → 0 ladder without mixing fillers.

**2. Direct replication of the originating contrast.** `dr0_or4` and `draw_max` are re-run on the new
seed block. Better, `draw_max − dr0_or4` and `lib2_an1 − lib3` are **the same contrast** (Libation
−1, Anger +1) measured at two different Draught/Oracle backgrounds, so they cross-check each other
within one screen.

**3. A circularity check, which the pre-launch audit turned up.**
`MirrorwingProvider::CleanupDiscardCandidates` holds a name-keyed `kOneIsEnough` list —
`{ Twinflame, Luxurious Libation }` — that sheds Libation copies beyond the first as **dead cards**,
step 1 of the shed order, on the owner's own 2026-08-18 judgement. It is the **only Libation-count-
aware rule in the engine**: `CastEnablerFirst` and `CastOrderRank` key on the `trick_token_power`
*param*, so they treat every copy alike.

So "extra Libations are dead" is a belief the engine already holds, and measuring that extra
Libations are dead is partly circular. The direction that matters is specific:

* if the belief is **wrong** and extra copies are good, the engine discards good cards → the
  Libation-3 arm measures worse than truth → **the finding is inflated**;
* if the belief is **right**, correctly shedding a dead card makes Libation-3 play better than a
  naive Libation-3 deck → the finding is *understated*, which is safe.

`MTG_MW_BUCKET_DISCARD=0` drops the whole hook back to `GenericProvider`, taking `kOneIsEnough` with
it. Stage 3 re-measures `lib2_or3 − lib3` and `lib2_an1 − lib3` with the rule off, at the **same seed
and same games**, making it a fully paired difference-in-differences. It runs last so it cannot
perturb the apparatus stages 1 and 2 share.

Prior expectation: the rule needs hand > 7 at cleanup, and three edits *inside* it measured
byte-identical, so the DiD should come out near zero. If it does not, the spell screen's Libation
lead is the heuristic agreeing with itself and must be withdrawn.

## The rule-off check now runs against EVERY comparison

> USER 2026-08-27: *"We should be doing that for all of the comparisons."*

Correct, and it follows directly from the correction above: the earlier "inert" finding tested edits
*within* the hook, never the hook itself, so **no comparison in this campaign has been checked
against it.** `CleanupDiscardCandidates` is the engine's one name-keyed hook on this deck — it ranks
Gold Rush, Draught, Twinflame, Libation, Fists, Anger, Oracle, Entrance, Expedite and Scale **by
name**, and sheds Twinflame and Libation copies beyond the first as dead. Every screen so far ran
with it on.

Because a spec is a complete description of an apparatus, the check needs no new specs: re-running
each spec **verbatim** under `MTG_MW_BUCKET_DISCARD=0` reproduces the same seed, games, arms and
apparatus, so each contrast pairs exactly against the number already in hand.

### Scope, narrowed on evidence

> USER, on seeing the slot screen queued for a re-run: *"Wait, we aren't done with the Heroism case
> yet?"*

Right — and the scope was too wide. The hook has **two** asymmetric parts, and they are not in the
same evidential position:

| part of the hook | tested? |
|---|---|
| the `kOneIsEnough` / pump **name list** (Gold Rush, Draught, Twinflame, Libation, Fists, Anger, Oracle, Entrance, Expedite, Scale) | **never directly** |
| the **body census** (`MwBodyCount`) | **twice** — `MTG_FRONTLINE_BODY` weight 1 vs 2 over 12,000 paired games was identical to the digit (zero games changed); Pyromancer's weight 1 → 2 was byte-identical |

The slot screen changes **only the creature slot**, so every one of its arms carries the same
Gold Rush 4 / Libation 3 / Fists 4 / Draught 2 / Oracle 2 / Entrance 4 — checked mechanically
against the spec, and the answer is that it touches **none** of the ten named cards. So the untested
half is *identical across its arms and cannot bias it*, and the only asymmetric half is the one
already A/B'd to zero games changed. Re-running it buys nothing, and Heroism is settled on its own
terms anyway (−0.4481 pooled over 40,000 games, held-out reproduced). **Dropped before it started.**

The spell screen changes **five of the ten named cards**, which is exactly where the untested half
bites. That is the one worth running:

| spec | seed | cells |
|---|---|---|
| `mirrorwing_spells.json` | 1,500,000 | 12 arms x 16,000 |

With the Libation ladder's own stage 3, both comparisons that can be affected are covered. What
matters
is not whether the *levels* move — the hook is a real part of the shipped policy, and it is allowed
to make every arm faster — but whether the **contrasts and the ranking** move. A level shift that is
equal across arms cancels and changes no conclusion; a differential one means the rule was doing
some of the deciding.

## LIBATION RESULTS (20,000 paired, seed 2500000; landed 2026-08-27 23:36)

**The inference held, and it was understated.** Anchor `lib3` (the current list) = −0.4456.

| filler | Lib 3 → 2 | → 1 | → 0 |
|---|---|---|---|
| Draught (−0.0144/copy, **good**) | −0.0234 (t = −8.7) | **−0.0421** (t = −15.6) | — (4-of cap) |
| Oracle (+0.0074/copy, mildly bad) | −0.0176 (t = −6.5) | −0.0329 (t = −12.2) | — (4-of cap) |
| Anger (+0.0122/copy, **worst tested**) | −0.0106 (t = −3.9) | −0.0183 (t = −6.8) | −0.0234 (t = −8.7) |

**All three fillers agree, monotonically, and the design's decisive case fires:** cutting Libation
wins even when the replacement is Ancestral Anger — the worst card in the pool, a card that *costs*
+0.0122 every time it is added. `lib0_an3` beats `lib3` by 0.0234 (t = −8.7) while trading three
Libations for three copies of a known-bad card. No filler artifact can produce that.

Winner `lib1_dr4` (Libation 1, Draught 4): screen −0.4877 ± 0.0069, held out (seed 3,000,000)
−0.4989 ± 0.0069, shrinkage −0.0111 ± 0.0097 (t = −1.14). **Pooled −0.4933 over 40,000 games.**

### The originating contrast replicated three ways

`draw_max − dr0_or4` was the single contrast that raised the hypothesis. Re-measured on a new seed
block, and re-measured again at a different background via `lib2_an1 − lib3` (the same shape:
Libation −1, Anger +1):

| measurement | seed | value |
|---|---|---|
| spell screen `draw_max − dr0_or4` | 1,500,000 | −0.0117 |
| this screen `draw_max − dr0_or4` | 2,500,000 | −0.0080 |
| this screen `lib2_an1 − lib3` (other background) | 2,500,000 | −0.0106 |

Same sign, same order of magnitude, across two seed blocks and two Draught/Oracle backgrounds.

### Libation's own per-copy value

Backing each filler's measured value out of its ladder:

| via | Libation per copy |
|---|---|
| Anger | +0.0200 |
| Oracle | +0.0238 |
| Draught | +0.0067 |

Oracle and Anger agree closely; the Draught route is the loosest (it leans hardest on a coefficient
imported from a different screen's seed block). Call it **+0.02 per copy** — Libation is the worst
card measured in this deck, worse than Ancestral Anger.

**Libation 1 vs 0 is NOT resolved.** Only the Anger filler has the headroom to reach 0, and
`lib1_an2 → lib0_an3` is −0.0051 (t = −1.9). Cutting to 1 is solid; cutting the last one is
suggestive at best.

### The circularity check came back clean — NOT circular (landed 2026-08-28 01:16)

`MTG_MW_BUCKET_DISCARD=0`, same spec, same seed, same 20,000 games, same arms. The first *direct*
measurement of this hook on any comparison:

| contrast | hook ON | hook OFF | **DiD** |
|---|---|---|---|
| `lib2_or3 − lib3` | −0.0176 | −0.0181 | **−0.0005** |
| `lib2_an1 − lib3` | −0.0106 | −0.0109 | **−0.0003** |
| `lib2_or3 − lib2_an1` | −0.0070 | −0.0072 | **−0.0002** |

Levels moved 0.0005–0.0010; `% identical` moved ≤ 0.1 pp. The hook is very nearly inert in play,
which is now measured rather than inferred from three edits inside it.

**And the sign is the benign one.** Every DiD is negative — with the hook off, cutting Libation looks
*slightly better*. That is the conservative direction predicted in advance: the hook was mildly
*helping* `lib3` by correctly shedding its dead extra Libations, so removing it makes `lib3` a touch
worse. The dangerous direction — the engine discarding good cards and inflating the finding — did
not occur. At −0.0005 against an effect of −0.0421, the hook accounts for **about 1%** of it.

### A drift caveat that now applies to the whole campaign

Counted exactly against the shipped 60 (both totals check at 60):

| card | shipped → current best | |
|---|---|---|
| Goblin Instigator | 4 → 0 | −4 |
| Luxurious Libation | 3 → 0 | −3 |
| Frontline Heroism | 0 → 4 | +4 |
| Fortifying Draught | 2 → 4 | +2 |
| Oracle's Restoration | 2 → 3 | +1 |

**7 cards out, 7 in — a 7-card change.** (An earlier revision said eight, then nine; both were
arithmetic slips. The right way to count is cards *out*, which equals cards *in* at a fixed 60.)

All of it was measured against one apparatus — play profile, pooled card scores, no keep table —
fit to the *original* deck. That is sound for ranking arms within a screen, which is what a screen is
for. It is progressively less sound as a description of the deck that would actually be played. The
skill's rule stands: a screen number is a RANKING, not the deck's measured strength, and an adopted
combination owes its own mulligan table, value leaf and ground truth. At a 7-card change that is no
longer a formality.

## Can the LAST Libation go? — queued 2026-08-28 03:47 (`logs/deckcmp/lib0.sh`)

> USER: *"What if we replaced the remaining Libation? Is there a strong argument to do that?"* →
> *"Say, replacing it with 1 Oracle and 2 Draught?"*

**On the evidence in hand: no, and it leans the other way.** The Anger ladder is the only one that
reaches Libation 0 — Draught and Oracle both hit the 4-of cap at Libation 1 — and each of its steps
removes exactly one Libation and adds exactly one Anger, so Anger's cost is constant across steps
and all curvature belongs to Libation:

| step | gain | t | implied cost of *that* copy |
|---|---|---|---|
| 3 → 2 | −0.0106 | −3.9 | +0.0228 |
| 2 → 1 | −0.0077 | −2.9 | +0.0199 |
| 1 → 0 | −0.0051 | **−1.9** | +0.0173 |

The gains **decelerate**. Each remaining Libation is worth more than the one before it — ordinary
diminishing returns, and it means the last copy is the best copy and the cheapest to keep. On top of
that the 1 → 0 step is −0.0051, which is *at or below* the 0.005–0.010 apparatus floor measured on
other decks. It is **unresolved, not established**, and it comes from the worst filler in the pool.

Two arguments do point at cutting it, and they are worth stating:

* **Its job has been taken.** Libation's distinctive role is the X = 0 cast — a `{G}` body-maker
  that widens the board before a fan-out (`XCandidates`: *"X=0 is a 'generate more creatures' play
  and is cast early, whereas X=maximum ... is cast late as a final pump"*). The deck now runs **4
  Frontline Heroism**, which makes a hasted body per qualifying cast. That is a direct substitute,
  and it means **this result is conditional on Heroism** — it is not evidence Libation was bad in
  the shipped list.

  > **NOT A GAP — a deliberate scope decision (user, 2026-08-28).** Cutting Libation was never
  > measured on an Instigator list, because every arm after the slot test ran Instigator 0 /
  > Heroism 4. That counterfactual was considered and declined: *"we never measured removing
  > libation without Frontline Heroism, but we won't bother because it is irrelevant to getting the
  > best version of this deck."* The same applies to the 89%/11% attribution split between Heroism
  > and the spell changes — it is path-dependent, and the other path leads to a list nobody will
  > play. Record the conditionality; do not spend games on it. It matters only if the creature base
  > is ever revisited.
* Even the first copy prices out worse than an Oracle in a direct, trample-clean contrast.

Against that: cutting to 0 removes a *mode*, not just a copy — Libation is the deck's only sink for
surplus mana (X = max as a closer), and a goldfish average-win-turn metric is weakest exactly on
closing lines.

### The deciding arm, which the ladder could not reach

The cap is broken with a **mixed filler**: Libation 3 → 0 frees three slots, and Draught 2 → 4 plus
Oracle 2 → 3 fills them with the two best available cards. That is the user's proposal exactly.

| arm | Lib | Dr | Or | An | why |
|---|---|---|---|---|---|
| `lib3` | 3 | 2 | 2 | 0 | anchor |
| `lib1_dr4` | 1 | 4 | 2 | 0 | reigning 20-life champion (pooled −0.4933) |
| **`lib0_dr4_or3`** | **0** | **4** | **3** | 0 | **the proposal — Lib 0 under GOOD fillers** |
| `lib0_or4_dr3` | 0 | 3 | 4 | 0 | mirror split: which card takes the 4th slot |
| `lib0_dr4_an1` | 0 | 4 | 2 | 1 | odd slot to Anger instead |
| `lib0_an3` | 0 | 2 | 2 | 3 | old conservative point, re-measured in THIS apparatus |

6 arms x 20,000, seed 5,500,000, confirm at 6,000,000. It runs **before** the 30-life battery — settle
what the 20-life list is, then test that list at 30 life — and `lib0_dr4_or3` has been added to the
30-life ladder too, so the question is answered at both life totals either way.

### RESULT (landed 2026-08-28 06:01): cut it. The "keep the last one" read was WRONG.

| arm | Lib | Dr | Or | An | delta |
|---|---|---|---|---|---|
| **`lib0_dr4_or3`** | 0 | 4 | 3 | 0 | **−0.5093** |
| `lib0_or4_dr3` | 0 | 3 | 4 | 0 | −0.5082 |
| `lib0_dr4_an1` | 0 | 4 | 2 | 1 | −0.4988 |
| `lib1_dr4` (prior champion) | 1 | 4 | 2 | 0 | −0.4981 |
| `lib0_an3` | 0 | 2 | 2 | 3 | −0.4733 |
| `lib3` (anchor) | 3 | 2 | 2 | 0 | −0.4523 |

`lib0_dr4_or3` beats `lib1_dr4` by **−0.0112 (t = −3.6)** and the anchor by −0.0570. Held out:
screen −0.5093 ± 0.0072, held-out (seed 6,000,000) −0.5028 ± 0.0072, shrinkage +0.0065 ± 0.0102
(t = +0.65). **Pooled −0.5060 over 40,000 games.**

The two splits are a dead heat — `lib0_dr4_or3` vs `lib0_or4_dr3` = −0.0011 (t = −0.35). Whether
Draught or Oracle takes the fourth slot does not matter; **cutting the Libation does.**

**Why the earlier read failed, and the lesson.** The per-copy cost of the last Libation was already
computed correctly from the Anger ladder at **+0.0173**. The mistake was then deferring to the *arm
difference* under that filler — −0.0051, t = −1.9, "below the floor" — instead of to the coefficient.
Anger costs +0.0122, so it ate 70% of the gain; the same card measured against a good filler shows
up as −0.0112 at t = −3.6. Confirming the arithmetic: `lib1_dr4 → lib0_dr4_or3` swaps one Libation
for one Oracle, and Oracle = +0.0074, so the last Libation prices at **+0.0186** — matching the
+0.0173 from the other ladder.

> **A card's value is its coefficient, not the arm difference.** An arm difference is
> `value(added) − value(cut)`, so a bad filler can hide a real effect entirely — and "it did not
> clear the floor" says nothing about the card when the floor was consumed by what replaced it.

### The rule-off check on the spell screen: clean (landed 2026-08-28 03:49)

The other half of *"we should be doing that for all of the comparisons"*, on the screen where five
of the ten name-listed cards actually move:

| contrast | ON | OFF | DiD |
|---|---|---|---|
| Fists ladder, Oracle filler (Q2) | +0.0229 | +0.0229 | **+0.0000** |
| Fists ladder, Anger filler (Q2) | +0.0357 | +0.0355 | −0.0002 |
| draw 10 → 12 (Q1) | +0.0223 | +0.0216 | −0.0007 |
| best arm vs current | −0.0068 | −0.0070 | −0.0002 |

Level shifts −0.0002 to −0.0010, and the **12-arm ranking is identical, position for position**. The
name-keyed discard hook decides nothing in either screen. Both comparisons are now cleared.

## The 4th Oracle — LAST screen, queued 2026-08-28 08:40 (`logs/deckcmp/oracle4.sh`)

> USER: *"whether we should have the fourth Oracle over one Impolite Entrance ... because we have
> more Draught and ... we have less need for the Haste with Libation gone."* And: *"I think that's
> the extent of what I would like to test."*

**Both reasons check out mechanically.**

*More Draught.* Oracle's per-copy `+1` life feeds `life_gained_this_turn`, so N Oracle instances
start Draught's X at N+2 instead of 2. Draught is now a **4-of**, so Oracle's rider is worth more
than when it priced at +0.0074/copy against Draught 2. Its own coefficient is stale by construction.

*Less need for haste.* Impolite Entrance's modelled value is cantrip + `grants_temp_haste`. Checked
against the current list — the **only creature-token maker left is Frontline Heroism, whose Soldiers
carry `created_token_haste = true`**. The two makers that produced summoning-sick bodies, Luxurious
Libation (Citizens) and Goblin Instigator (Goblins), are both cut. Entrance's haste has lost its main
consumer; what remains is hasting fresh mana dorks (CR 302.6, modelled) and the magnets.

| arm | change from `cur` | asks |
|---|---|---|
| `or4_ie3` | Oracle 4, Entrance 3 | **the proposal** |
| `an1_ie3` | Anger 1, Entrance 3 | is cutting Entrance good *per se*, or is this Oracle being good? |
| `or4_ie2_an1` | Oracle 4, Anger 1, Entrance 2 | does it go further? |
| `or4_gr3` | Oracle 4, Gold Rush 3 | **the alternative donor** |
| `an1_gr3` | Anger 1, Gold Rush 3 | same bad-filler control on Gold Rush |

Every donor cut runs under a **good filler (Oracle) and a known-bad one (Anger, +0.0122/copy)** —
the Libation lesson applied in advance: an arm difference is `value(added) − value(cut)`, so a bad
filler can hide a real effect and a good one can manufacture the look of one.

**Gold Rush and Impolite Entrance are the only two cards in this deck that have sat at 4-of in every
arm of every screen** — neither has ever been priced. This finally does that.

> **Modelling asymmetry the number will not show.** Impolite Entrance's *"gains trample"* is **not
> modelled** (the goldfish never blocks) while Oracle's `+1/+1` is fully modelled, so this screen
> **flatters Oracle, one-directionally**. Colour cuts the same way but legitimately: `{G}` has 16/21
> sources against `{R}`'s 13/21, and that *is* modelled.

**Floor honoured:** user 2026-08-28, *"I would always leave Entrance at 2 at minimum"* — the deepest
cut in the spec is `or4_ie2_an1` at exactly 2. Nothing goes below.

### Trample audit — which arms are actually compromised, and which are not

> USER: *"it's unlikely to be chosen because we aren't taking the trample into account."*

Right about the mechanism, but it does **not** apply uniformly — and the reason is that **Ancestral
Anger grants trample too.** Three spells in the pool do, all three unmodelled:

| grants trample | not |
|---|---|
| Fists of Flame, Impolite Entrance, **Ancestral Anger** | Gold Rush, Oracle's Restoration, Fortifying Draught, Luxurious Libation |

Counting sources per arm (Fists is a 4-of in every arm, so **half the supply never varies**):

| arm | trample sources | reading |
|---|---|---|
| `cur` | 8 | baseline |
| `an1_ie3` | **8** | **trample-neutral** — Entrance → Anger trades one trampler for another |
| `or4_gr3` | **8** | **trample-neutral** — the Oracle comes from Gold Rush, Entrance stays at 4 |
| `an1_gr3` | **9** | *gains* a trampler — engine understates it |
| `or4_ie3` | 7 | loses one — discount accordingly |
| `or4_ie2_an1` | 7 | loses one — discount accordingly |

So the screen contains a **trample-safe route to the 4th Oracle**: `or4_gr3` takes it from Gold Rush
and keeps every trampler. If the 4th Oracle is genuinely good, that arm can be believed as it stands;
only `or4_ie3` and `or4_ie2_an1` need the trample discount applied by hand.

`an1_ie3` becomes the cleanest card-vs-card test in the whole campaign: Entrance and Anger are both
1-mana, both cantrips, both tramplers — so it isolates **Anger's escalating `+X/+0` against
Entrance's haste**, with everything else held.

### The Entrance → Anger ladder runs its full length (user: *"we can freely compare them"*)

Because the swap is trampler-for-trampler, the Entrance-2 floor — which existed *because* of trample
— does not bind on this axis. User: *"We could even try down to 1 or 0 entrance when replaced by
anger."* So the ladder goes all the way, **trample-neutral at every rung**:

| rung | Entrance | Anger | trample sources |
|---|---|---|---|
| `cur` | 4 | 0 | 8 |
| `an1_ie3` | 3 | 1 | 8 |
| `ie2_an2` | 2 | 2 | 8 |
| `ie1_an3` | 1 | 3 | 8 |
| `ie0_an4` | 0 | 4 | 8 |

Nine arms + base = 10 cells; every arm verified at 60 cards.

### What haste is still *for* — audited, because `ie0_an4` removes all of it

> USER: *"Normally I would say we absolutely need to keep some haste, but very little needs it
> anymore, so that may no longer be the case."*

**Impolite Entrance is the deck's only external haste source.** At `ie0_an4` the list has none at
all. Auditing supply and demand in the current list:

| | |
|---|---|
| **Supply** | Impolite Entrance (`grants_temp_haste`) — the only one. Frontline Heroism's Soldiers enter hasted **on their own** (`created_token_haste`). |
| **Demand — gone** | The sick-token makers are both cut: Libation's Citizens and Instigator's Goblins. Heroism is now the deck's *entire* token supply, and it needs no help. |
| **Demand — remains** | **Mana dorks** (Ignoble Hierarch 4, Elvish Mystic 4, both `template: mana_dork`): hasted, a fresh dork taps for mana the turn it lands (CR 302.6 — **modelled**, read by `CanTapNow`). **Magnets** (Zada 4, Mirrorwing 4): haste adds *their own* attack. |

**The load-bearing point: the combo does not need haste.** A magnet's copy ability triggers on
**casting a spell**, not on attacking — so a summoning-sick Zada or Mirrorwing fans out copies
perfectly well the turn it lands. Haste never gates the combo; it only adds the magnet's own body to
the swing and accelerates a dork.

So the user's read is right in mechanism, and the remaining uses are real but narrow and both
modelled — which is exactly why the ladder can price them instead of anyone having to guess.

### Gold Rush was a PREMISE of all fifteen prior screens, never a measurement

> USER: *"I think Gold Rush might be the best card in the deck because it produces net mana while
> heavily pumping creatures"* … *"best instant/sorcery. Obviously they don't work on their own"* …
> and the sharp part: ***"that is why we haven't tested the others against it."***

Correct, and it is the one structural hole left. **Gold Rush sat at 4-of in all fifteen prior
specs.** Impolite Entrance was in the same position until this one. Every "X beats Y" conclusion in
this document is therefore conditional on Gold Rush 4 being right, and that has never been checked.

The engine does model it faithfully: Treasure created *first*, then the pump counts Treasures,
**recomputed per copy** — so a magnet fan-out of N copies makes N Treasures and escalates +2/+2,
+4/+4, +6/+6 — and the Treasures are real permanents whose sac-for-mana is searched, so the net-mana
half is live rather than cosmetic.

`an1_gr3` (Gold Rush 3, Anger 1) plus **`gr2_an2`** (Gold Rush 2, Anger 2) now form a **two-rung
ladder under a single known-worse filler** — the design that settled Libation. Anger loses to Oracle
by 0.0046–0.0067/copy in direct matched pairs, so if cutting Gold Rush wins even against *that*, the
premise was wrong; if it loses on both rungs, the premise is vindicated on its own evidence for the
first time.

> Unmodelled direction is **opposite to the Libation ladder's**: Gold Rush grants no trample while
> Anger does, so these arms *gain* tramplers (8 → 9 → 10). The engine cannot see trample, so the
> measured number is unaffected — but a Gold-Rush-cut arm that merely *ties* is worth slightly more
> in real play than it measures.

**Why two rungs is enough to settle the decision.** Fists, Draught and Oracle are all capped at 4 in
this family, so a slot freed from Gold Rush has nowhere to go except the residual trio — the weakest
cards in the pool. Combined with `or4_gr3` (Gold Rush 3, Oracle 4), which covers the one better
destination that exists, this is a *complete* test of the decision-relevant question, not a sample of
it.

> **PRE-REGISTERED (user):** *"realistically I would be shocked if any of our marginal cards could
> beat it."* Expected outcome: `cur` beats both rungs. Running it anyway costs one cell (~15 min) and
> converts an assumption underlying fifteen screens into a measurement — cheap at the price whether
> or not it surprises.

### The residual-3 family — maxed lifegain, and the arithmetic is exact

> USER: *"we finally max the lifegain cards, which seems to be where this is going and beyond that
> can choose the best of Entrance, Libation and Anger."* And, honestly: *"Maxing the lifegain cards
> is a guess, but everything so far has pointed toward that."*

The guess has a clean structure behind it. With **Draught 4 + Oracle 4** on top of the untouched
4-ofs (magnets 8, dorks 8, Fists 4, Gold Rush 4, Heroism 4) and 21 lands, the deck is at **57** —
leaving **exactly 3 slots** for Entrance / Libation / Anger. "2 Entrance, 1 Libation, 1 Oracle" *is*
that family.

| split (E/L/A) | arm |
|---|---|
| 3 / 0 / 0 | `or4_ie3` |
| 2 / 1 / 0 | `ie2_lib1_or4` — the user's shape |
| 2 / 0 / 1 | `or4_ie2_an1` |
| 1 / 1 / 1 | `or4_ie1_lib1_an1` *(added)* |
| 1 / 0 / 2 | `or4_ie1_an2` *(added)* |
| 1 / 2 / 0 | `or4_ie1_lib2` *(added)* |

Six splits — every one with Entrance ≥ 1, which is where the pre-registered 1–3 prediction puts the
answer. Fifteen arms + base = 16 cells.

**It is carried as a hypothesis, not a premise.** `cur` (Oracle 3) and `or4_gr3` (Oracle 4 paid out
of Gold Rush instead of the trio) both stay in, so *"is the 4th Oracle worth a slot at all"* is
measured rather than assumed.

**And the guess is already evidence-backed.** Oracle's own coefficient was **+0.0074/copy — i.e.
bad** — but that was fitted at **Draught 2**. At Draught 4, the `lib0` screen found `lib0_dr4_or3`
and `lib0_or4_dr3` a **dead heat** (−0.0011, t = −0.35): trading a Draught for an Oracle is now
*neutral*, so Oracle's marginal copy has risen to match the best card in the deck. That is the
enabler synergy — Oracle's `+1` life starting Draught's X higher — showing up in the numbers rather
than only in the oracle text.

### Libation vs Entrance — a hole nobody had noticed

> USER: *"We probably should also compare Libation against Entrance just in case. If it beats anger,
> then perhaps our best bet could be something like 2 Entrance 1 Libation 1 Oracle or something?"*

This closes a real gap. **Impolite Entrance has sat at 4-of in every arm of every screen**, so it has
never been priced against *anything* — and Libation was only ever cut in favour of Draught, Oracle
and Anger, never Entrance. "Libation is the worst card in the deck" is therefore a statement about
those three comparisons only.

| arm | Entrance | Libation | Oracle | asks |
|---|---|---|---|---|
| `ie3_lib1` | 3 | 1 | 3 | the clean 1-for-1 — prices Libation directly against Entrance |
| `ie2_lib2` | 2 | 2 | 3 | second rung, so the ladder has curvature like the Anger one |
| `ie2_lib1_or4` | 2 | 1 | 4 | **the user's proposed shape** |

> **This ladder runs the OPPOSITE way to the Anger one on the unmodelled factors.** Libation does
> **not** grant trample, so Entrance → Libation *loses* a trampler at every rung (8 → 7 → 6), where
> Entrance → Anger held it at 8. And cutting Entrance also sheds the unmodelled haste **resilience**.
> Both unmodelled factors therefore favour **Entrance** here — so **a Libation win is a WEAK result**
> (the true gap is smaller, possibly reversed) while **an Entrance win is REINFORCED**.

#### PRE-REGISTERED DECISION RULE: a tie resolves AGAINST Libation

> USER, before the data: *"if libation is very close to or worse than Entrance/Anger in this case,
> then it should probably be dropped, because it lacks trample."*

Recorded in advance so the reading is not chosen after the fact. The decision boundary is
**asymmetric**: Libation must beat Entrance/Anger by a *margin* to survive, because everything the
engine cannot see runs against it. Measured parity ⇒ drop.

#### Libation vs ORACLE is already settled, and it was trample-CLEAN

Worth separating, because it narrows what the new ladder actually has to decide. **Oracle grants no
trample either**, so the Libation ladder's Oracle rungs were a fair fight all along:

| slots swapped | 20 life | per copy | 30 life | per copy |
|---|---|---|---|---|
| 1 | −0.0176 | −0.0176 | −0.0225 | −0.0225 |
| 2 | −0.0329 | −0.0164 | −0.0334 | −0.0167 |

**Oracle beats Libation by ~0.017 per slot, consistent at both life totals, with no unmodelled
keyword on either side.**

Those rungs were measured at **Draught 2**, where the Oracle→Draught synergy runs at *lower
intensity* — it operates whenever Draught is in the deck, just more weakly with fewer copies.

#### CORRECTION: what a per-copy coefficient is measured AGAINST

> USER: *"+0.0074/copy — what is it replacing here?"*

A necessary correction to how this document has been using those numbers. **The per-copy figures are
REGRESSION coefficients, not absolute card values.** Every arm is 60 cards, so "one more Oracle"
necessarily means "one fewer of something else", and the coefficient is relative to whatever actually
varied inversely with it across that screen's arms. In the spell screen that was **mostly Fists and
Draught — the two best cards in the deck**:

| card | range across the spell screen's arms |
|---|---|
| Fists of Flame | 2, 3, 4 |
| Fortifying Draught | 0, 1, 2, 3, 4 |
| Oracle's Restoration | 2, 3, 4 |
| Ancestral Anger | 0, 1, 2 |
| Gold Rush / Impolite Entrance | 4, fixed |

So **+0.0074/copy means "an Oracle *instead of a Fists or a Draught*"** — not "Oracle against an empty
slot", and **not "Oracle is a bad card"**. Two claims made earlier in this document are therefore
withdrawn:

* ~~"Libation lost to a card that was itself net-negative"~~ — Oracle is not net-negative in absolute
  terms; it is *worse than Fists/Draught*. The bad-filler framing does not apply to Oracle.
* ~~"Libation = +0.0250/copy" (0.0074 + 0.0176)~~ — that **adds a regression coefficient to a direct
  arm contrast**, which are on different baselines. The same objection voids the other two
  "independent routes" (+0.0200 via Anger's coefficient, +0.0186 via Oracle's): all three inherit a
  baseline they do not share. **Libation's absolute per-copy cost is not well identified across
  screens, and this document should stop quoting one.**

#### What survives — the direct arm contrasts, which need no baseline

| contrast | value | note |
|---|---|---|
| Oracle − Libation | **−0.0176/copy** (20 life), **−0.0225** (30 life) | trample-clean, within-screen |
| Oracle − Anger | −0.0046/copy at Draught 0, −0.0067 at Draught 2 | matched pairs |

Both are within-screen swaps with everything else fixed, so they mean exactly what they say. And the
second rescues the bad-filler argument by a different route: **Anger is measurably worse than
Oracle**, so `lib0_an3` beating `lib3` by 0.0234 while adding three Angers *is* a genuine
win-against-a-known-worse-replacement — which is what the Libation conclusion actually rests on.

So Libation's case never rested on Oracle: it is already lost there on clean, trample-neutral ground,
at both life totals. Its case rests *entirely* on beating Entrance or Anger, both of which grant
trample. Combined with the decision rule above, Libation must win the one comparison where the scales
are tilted against it, having already lost the one where they were level.

Twelve arms + base = 13 cells. *Caught at pre-flight:* the `replace` map is expressed against the
**shipped** base, where Libation is 3 and therefore *decreases* to 1 — naming it as a recipient was
wrong and the driver said so. Pairing only affects precision, not the estimate, but it is the kind of
error that reads as fine and quietly costs ~20% more games.

### The Entrance ↔ Anger axis is the most controlled comparison in the campaign

Entrance and Anger are identical on every axis the engine models except one:

| | Impolite Entrance | Ancestral Anger |
|---|---|---|
| mana cost | `{R}` | `{R}` — same |
| type | Sorcery | Sorcery — same |
| `cast_draw` | 1 | 1 — same |
| `solo_target_trick` | yes | yes — same |
| trample (unmodelled) | grants | grants — **and equal in count at every rung** |
| **the difference** | `grants_temp_haste` | `gy_self_power_bonus` (X = 1 + copies in yard) |

Same cost, colour, type, cantrip, and trample. The ladder isolates **haste against an escalating
`+X/+0`** and nothing else — colour-neutral and trample-neutral throughout.

**But "controlled" applies to the comparison, not to the VALUATION of the variable under test.**

> USER 2026-08-28: *"The haste actually has a little advantage that we don't really test. The 'kill
> your opponent out of nowhere' advantage that especially is good against decks with sorcery-speed
> sweepers."*

This is correct and it is the one gap the ladder's symmetry does **not** close. Haste has two kinds
of value and this harness sees only the first:

| value of haste | modelled? |
|---|---|
| **Tempo** — attack the turn a creature lands; tap a fresh dork for mana (CR 302.6) | **yes** — `temp_haste` is read by `CanAttackFull` and `CanTapNow` |
| **Resilience** — deploy and kill in ONE turn, so the board is never exposed to a sorcery-speed sweeper | **no, and structurally so** |

The second is invisible *by construction*, not by omission: the opponent is a goldfish, so there is
no sweeper, no interaction, and **no cost whatsoever to exposing a board for a turn**. A deck that
must deploy on turn N and swing on turn N+1 is penalised zero here, when in a real game that extra
turn is exactly the window a Wrath-style effect uses. Average win turn cannot represent it.

Note how this differs from the trample gap. Trample is unmodelled too, but along this ladder it
**cancels** — Anger grants it as well, in equal count at every rung. Haste's resilience value does
**not** cancel: Anger offers none of it. So on the one axis that is otherwise perfectly controlled,
this is the single remaining one-directional bias, and it runs **against Entrance**.

**Consequence for reading the result:** the ladder's number is a *lower bound* on Entrance's worth.
Wherever the measured optimum falls, the honest recommendation should sit at or above it — which
happens to reinforce the pre-registered 1–3 prediction rather than undercut it, and argues against
`ie0_an4` even if it measures well.

*Measured as a proxy* — `scripts/board_exposure.py`, queued behind the screen at **3 arms x 1,500
games (~3 min)**, deliberately tiny (user: *"a set of files that is not too huge... I don't want to
extend the length of the run"*). It reuses the screen's **own manifest**, so the apparatus is
identical and the seed unchanged — these games are a strict *prefix* of the measured ones. Arms
bracket the haste axis: `cur` (Entrance 4), the ladder winner, `ie0_an4` (no external haste at all).

| metric | what it says |
|---|---|
| `exposure_turns` | turns ending with a creature on board before the win — each one a window a Wrath could use |
| `peak_exposed` | largest end-of-turn creature count before the win — how much is standing there to be swept |
| `nowhere_kill` | share of wins where the previous turn ended with ≤1 creature — the lethal board appeared on the turn it killed |

It does **not** price sweeper risk; nothing here can. It ranks how much exposure each arm buys, which
is the input that risk would act on. Two schema traps were verified against a real corpus rather than
assumed: the trace has `result.turn` (not `winTurn`) with one entry **per phase**, and creature
tokens like `1/1 Soldier Token` have **no `cards.json` entry**, so a name-set lookup alone scores
every token as zero — counting them moved `peak_exposed` 3.18 → 3.73 on the test corpus. (`Treasure
Token` correctly fails the pattern: no P/T prefix.)

*The original framing of this option:*
board-exposure — per arm, how many turns the deck holds a threatening board before the winning turn,
and how often the kill comes from a board that was empty at the start of that turn (a true
"out of nowhere" kill). That is computable from `--game-trace-dir` output, the same route the Myconid
stratification used. It would not price the sweeper risk, but it would rank the arms on how much
exposure they buy. Offered, not run — outside the screening scope the user set.

### PRE-REGISTERED PREDICTION (user, before the data): the optimum is INTERIOR, at 1–3 Entrance

> USER: *"I still expect 1-3 entrance to be the recommendation, since it becomes more valuable as we
> get less of it."*

The two cards' returns curve in **opposite directions**, which is what makes an interior optimum the
mechanically-expected shape rather than a hedge:

* **Entrance has strongly DIMINISHING returns**, and the magnet is why. Entrance is a
  `solo_target_trick`, so under a Zada/Mirrorwing fan-out **one cast hastes the whole board**. The
  haste need is fully satisfied by a single copy per turn; copies 2–4 contribute only their cantrip
  and trample. Against that, going too low risks not *drawing* one — a consistency-of-access
  argument that pushes the floor up from 1 toward 2–3.
* **Anger has INCREASING returns.** X = 1 + copies of Anger in your graveyard, so the 4th copy is
  worth more than the 1st — the reverse of every other card here, and the reverse of the Libation
  curve. Its +0.0122/copy penalty was fitted at **1–2 copies**, where the escalation barely fires, so
  that coefficient **systematically understates a 4-of build**.

So the ladder should be **concave with an interior peak**, not monotonic. That makes it falsifiable
in a useful way:

| observed shape | reading |
|---|---|
| peak at 1–3 Entrance | both curvatures confirmed; the prediction holds |
| monotonic down to `ie0_an4` | contradicts the "one Entrance hastes the board" mechanism — **suspect the measurement**, since it would mean haste is worth nothing at all |
| monotonic up to `cur` (4/0) | Anger's escalation never pays inside this deck's game length |

A linear per-copy coefficient cannot describe either card. Read the ladder rung by rung.

## 30 life, queued 2026-08-28 02:45 (`logs/deckcmp/life30.sh`)

> USER: *"Don't forget to test at 30 life as well."*

Not a routine robustness pass here — **30 life is the condition most likely to move both results,
and in a direction predictable from the cards themselves**:

* **Luxurious Libation is `{X}{G}`** and its pump scales with the X you can pay, so more turns of
  mana make it strictly better. It is the card the 20-life ladder ranked *worst*.
* **Ancestral Anger's X = 1 + copies in your graveyard**, so it grows with game length too. It is
  the card the 20-life spell screen ranked *worst*.
* **"Do we need more draw" is a question about how long the game runs.** A 30-life race is where a
  deck would want card flow if it ever does.

So both open findings get their most hostile fair test. If cutting Libation is still right at 30
life — and still right under the Anger filler — game length is eliminated as the explanation.

| stage | spec | seed | cells |
|---|---|---|---|
| 1 | `mirrorwing_libation_30life.json` | 3,500,000 | 10 arms x 12,000 |
| 2 | `--confirm <winner>` | 4,000,000 | 2 arms |
| 3 | `mirrorwing_spells_30life.json` | 4,500,000 | 12 arms x 12,000 |

`max_turns: 12` in both (at 30 life the deck needs ~1.5x the damage; leaving it at 8 would score most
games unwon at `max_turns+1` and collapse the metric). Starting life via `MTG_START_LIFE=30` —
`GameSetup::StartingLife` resolves per-job → env → 20.

### 30-LIFE LIBATION RESULT (landed 2026-08-28 09:42): same answer, smaller margin

`lib0_dr4_or3` wins again — −0.4535 ± 0.0112 against `lib3`'s −0.4188. Held out (seed 4,000,000)
−0.4405 ± 0.0111, shrinkage +0.0130 ± 0.0158 (t = +0.82). **Pooled −0.4470 over 24,000 games.**

Contrasts against `lib3` at both life totals (contrast-of-contrasts se ≈ 0.0044):

| arm | 20 life | 30 life | shrink |
|---|---|---|---|
| **`lib0_dr4_or3`** | **−0.0570** | **−0.0347** | **+0.0223** (t ≈ 5.1) |
| `lib1_dr4` | −0.0421 | −0.0279 | +0.0142 (t ≈ 3.2) |
| `lib0_an3` | −0.0234 | −0.0107 | +0.0127 (t ≈ 2.9) |
| `lib2_dr3` | −0.0234 | −0.0181 | +0.0053 |
| `lib1_or4` | −0.0329 | −0.0334 | −0.0005 |
| `lib2_or3` | −0.0176 | −0.0225 | −0.0049 |

**The predicted mechanism is confirmed.** Libation is `{X}{G}` and its pump scales with the mana it
can pay, so longer games should flatter it — and they do: the winner's edge falls **39%**, from
0.0570 to 0.0347, at t ≈ 5.1. That was the a-priori reason to run 30 life at all, and it fired.

**Sharpest single point — the Anger ladder's bottom rung reverses:**

| rung | 20 life | 30 life |
|---|---|---|
| 3 → 2 | −0.0106 | −0.0093 |
| 2 → 1 | −0.0077 | −0.0042 |
| **1 → 0** | **−0.0051** | **+0.0028** ← sign flip |

At 30 life, cutting the *last* Libation for a third Ancestral Anger is actively **worse**. Treat this
as suggestive rather than settled — the change is +0.0079 against se ≈ 0.0044, t ≈ 1.8 — but it is
the exact shape the mechanism predicts, and it aligns with the 20-life deceleration.

**What survives, and what to take from it.** Under the *good* fillers the cut is life-total-robust:
the Oracle ladder is flat across life totals (`lib1_or4` −0.0329 → −0.0334). It is only when the
replacement is weak that Libation's last copy earns its slot back in long games. So:

* **The recommendation is unchanged at both life totals — `lib0_dr4_or3`.**
* But the *reason* it is safe is that the freed slots go to Draught and Oracle. The `lib0_an3`-style
  "cut Libation for anything" reading does not survive 30 life.

This also carries a warning into the Entrance ladder still running: **Ancestral Anger's graveyard
escalation should likewise improve with game length**, so the 30-life spell suite (stage 3, in
flight) may rank Anger higher than the 20-life screen did.

**A cheaper universal answer exists and should replace this eventually.** Re-running every screen
under the flag costs a screen per screen. Instrumenting how often `CleanupDiscardCandidates`
actually *fires* (a counter behind a debug flag) would settle it once for all comparisons, present
and future: if cleanup discard reaches ~0% of games, every name-keyed clause in it is provably
inert and no re-run is ever needed. That needs a rebuild, which must not happen while these batches
are running — the running chains resolve `build/Release/mtg`. Deferred to when the box is free.

---

# THE ORACLE-4 ROW — Oracle 4 × Entrance/Anger (user 2026-08-28: *"let's get numbers for Oracle 4 and Entrance/Anger combinations before reporting"*)

The deck owner's open question is how many **Impolite Entrance** the list should keep. That question
cannot be answered from the Entrance→Anger ladder alone, because the ladder was run at **Oracle 3**
and the 4th Oracle is the other thing competing for those same slots.

**The grid is small and closed.** The residual budget is exactly 7 cards:

```
Oracle's Restoration + Impolite Entrance + Ancestral Anger + Luxurious Libation = 7
```

(Derivation, against the shipped 60: Instigator 4→0 and Heroism 0→4 cancel; Libation 3→0 frees 3;
Draught 2→4 spends 2; so Oracle + Entrance + Anger must total 7. Every arm in every screen below is
verified at 7, which is why they are all legal 60-card lists.)

With Libation at 0 — settled earlier in this document, three ways plus a circularity check — the
Oracle-4 row is therefore exactly **four cells**, and the Oracle-3 row exactly five.

## The two rows, 20 life

20,000 paired games, seed 6,500,000, d5 / 20 ms. Negative delta = **faster kill** = better.
Pairwise standard error ≈ 0.0024.

| Oracle | Entrance | Anger | tag | delta |
|---|---|---|---|---|
| 4 | 3 | 0 | `or4_ie3` | −0.5225 |
| 4 | 2 | 1 | `or4_ie2_an1` | −0.5249 |
| 4 | 1 | 2 | `or4_ie1_an2` | **−0.5390** |
| 4 | 0 | 3 | `or4_ie0_an3` | *pending — corner screen, seed 8,000,000* |

| Oracle | Entrance | Anger | tag | delta |
|---|---|---|---|---|
| 3 | 4 | 0 | `cur` | −0.5050 |
| 3 | 3 | 1 | `an1_ie3` | −0.5140 |
| 3 | 2 | 2 | `ie2_an2` | −0.5214 |
| 3 | 1 | 3 | `ie1_an3` | −0.5316 |
| 3 | 0 | 4 | `ie0_an4` | **−0.5422** |

## The 4th Oracle pays at every Anger count

Holding Anger fixed and swapping **Oracle +1 / Entrance −1** — the cleanest available read on the
4th Oracle, because only two counts move and the pair is measured on the same games:

```
Anger 0   cur          -> or4_ie3         -0.0175   (~7 se)
Anger 1   an1_ie3      -> or4_ie2_an1     -0.0109   (~4.5 se)
Anger 2   ie2_an2      -> or4_ie1_an2     -0.0176   (~7 se)
Anger 3   ie1_an3      -> or4_ie0_an3     pending
```

Three of three positive, none near the apparatus floor (0.005–0.010 on other decks). **The 4th
Oracle is a real gain, and it is not conditional on the Anger count.** This is consistent with the
mechanism established earlier in this document: Oracle's +1 life rider is a *Fortifying Draught
enabler*, so its value rose when Draught went to 4 — and Draught is at 4 in every arm here.

## The finding the deck owner needs: one Entrance is FREE

**`or4_ie1_an2` and `ie0_an4` are statistically tied.**

```
best two: ie0_an4 vs or4_ie1_an2 = -0.0032 +- 0.0024   (t = -1.32)
```

t = −1.32 does not distinguish them. Two very different-looking lists — one with zero Entrance and
four Anger at Oracle 3, one with a single Entrance and two Anger at Oracle 4 — are the same deck as
far as 20,000 paired games can tell.

So the answer to *"we may still want some number of Entrance"* is: **the data already permits it.**
Taking the 4th Oracle buys back one Entrance at no measurable cost. What the data does *not*
support is two or three Entrance — those cells sit 0.0141 and 0.0165 behind, which is 6–7 se and
well clear of the floor.

## The caveat that must travel with this recommendation

**Impolite Entrance's trample is not modelled.** The goldfish opponent never blocks
(`DecisionProviders`: *"attack with everything that can attack (no blockers)"*), so trample changes
no damage in this engine. Under these measurements Impolite Entrance is **parameter-identical to
Expedite**, and a screen cannot distinguish the two.

Every number in the tables above is therefore a **lower bound** on Entrance. The bias falls on
exactly the card the deck owner is deciding whether to keep, and it is a *modelling* limit, not a
sample-size one — more games will not reduce it.

Read together with the tie, the honest statement is:

> The screen cannot distinguish Or4/E1/A2 from Or3/E0/A4. The unmodelled trample breaks that tie
> toward keeping the single Entrance.

That is a recommendation the measurement supports; it is not a measurement.

The same audit ran earlier on haste, the *other* thing Entrance provides, and there the answer was
clean rather than blocked: the board-exposure proxy found removing every haste source moved
exposure by 0.03 turns and the "kill from nowhere" rate not at all (2.60 → 2.57 turns, 19.9% →
19.9%). **Haste is not the reason to keep Entrance. Trample might be.**

## Why the corner cell was missing, and the lesson

`or4_ie0_an3` was absent from a 16-cell screen because the arm set had been scoped to *"splits with
Entrance ≥ 1, which is where the pre-registered 1–3 prediction puts the answer."* When that
prediction failed — the Entrance→Anger ladder ran monotonically to **zero** — the one corner
excluded *by the prediction* became the most promising point on the board. The user caught it.

**Never scope the arm set to the predicted answer. A failed prediction leaves exactly the untested
region you need.**

Three independent measured swaps triangulate the missing cell:

```
Oracle +1 / Anger    -1   (ie1_an3 -> or4_ie1_an2)   -0.0074
Oracle +1 / Entrance -1   (cur     -> or4_ie3)       -0.0175
Anger  +1 / Entrance -1   (ie1_an3 -> ie0_an4)       -0.0106

or4_ie0_an3 predicts to about -0.549, vs the current leader's -0.5422
```

If it lands there it is the new recommendation; if it does not, the two-way tie above stands and
the trample argument decides. Either way the row is closed after it — there is no fifth cell.

## 30 life — the row is being completed there too

At 30 life only **one** Oracle-4 cell exists (`or4_ie1_an2`, in the seed-7,500,000 ladder). That is
not enough to hand over an Oracle-4 recommendation, for a reason this campaign has already been
bitten by once: **life total moved the Libation result by 39%**, and the mechanism generalises —
Ancestral Anger's graveyard escalation and Draught's lifegain accumulation both compound with game
length, so a longer game is not a neutral re-test of this axis.

Queued: all four Oracle-4 cells plus both Oracle-3 anchors (`ie1_an3`, `ie0_an4`), 12,000 games,
seed 8,500,000, `MTG_START_LIFE=30`, `max_turns 12`. Preflight clean. The anchors are **in-spec on
purpose** — deltas do not carry across specs, because every arm shares one apparatus and the pool
composition is part of that apparatus.

## Run state

Three screens chained, one pooled batch at a time, each waiting on the prior run's **PID** (never
`pgrep -f`, which self-matches):

| # | run | spec | seed | games | life | status |
|---|---|---|---|---|---|---|
| 1 | Entrance→Anger ladder | `mirrorwing_entanger_30life.json` | 7,500,000 | 12,000 | 30 | in flight, 32/32 workers |
| 2 | missing corner + confirm | `mirrorwing_corner.json` | 8,000,000 | 20,000 | 20 | queued |
| 3 | Oracle-4 grid + confirm | `mirrorwing_or4_grid_30life.json` | 8,500,000 | 12,000 | 30 | queued |

All four seeds (6.5M / 7.5M / 8.0M / 8.5M) are disjoint, so nothing here is confirmed on the seeds
that selected it.

**Standing caveat on every delta in this document:** these are SCREEN numbers. Every arm shares one
apparatus, so a delta is a **ranking**, not the deck's measured strength. The absolute win-turn of
the adopted list is only known after its own mulligan profile and value leaf are generated.

---

# LIBATION vs ANGER — the direct question (user 2026-08-28: *"Did we check libation against the others as well? In particular against Anger?"*)

Yes. **Four times, at 20 life, as a strict 1:1 swap with every other count held fixed. Anger won all
four.** These are not regression coefficients against a mixed background — each pair below differs
in exactly two numbers, Libation down and Ancestral Anger up, and both arms are scored on the same
paired games.

## The four matched swaps

```
Oracle 4 / Entrance 1        <- the sharpest read in the screen: a 3-point ladder
  A0 L2   or4_ie1_lib2        -0.5148
  A1 L1   or4_ie1_lib1_an1    -0.5272     step  -0.0124
  A2 L0   or4_ie1_an2         -0.5390     step  -0.0118
  => -0.0242 over 2 copies = -0.0121 per copy

Oracle 3 / Entrance 3   ie3_lib1     -0.5050 -> an1_ie3      -0.5140   = -0.0090 / copy
Oracle 3 / Entrance 2   ie2_lib2     -0.5043 -> ie2_an2      -0.5214   = -0.0086 / copy
Oracle 4 / Entrance 2   ie2_lib1_or4 -0.5211 -> or4_ie2_an1  -0.5249   = -0.0038 / copy
```

**The Oracle-4 / Entrance-1 ladder is the strongest single piece of evidence in the campaign on this
axis.** It is not one contrast but two independent one-copy steps in the same direction, and they
come back nearly the same size (−0.0124, −0.0118). A linear response to removing copies one at a
time is what a real effect looks like; noise does not usually arrange itself into an even staircase.
At se ≈ 0.0024 the full two-copy swap is roughly 10 se.

Three of the four land at −0.009 to −0.012 per copy. The outlier is Oracle 4 / Entrance 2 at
−0.0038 (~1.6 se), which taken alone is consistent with zero — **one weak read against three strong
ones, all pointing the same way.** Stated honestly rather than averaged away.

## Where that puts Libation in the ordering

Against the *other* competitor for the same slots, Libation is not merely behind — it never leads
anything:

* **vs Entrance at Oracle 3:** `ie3_lib1` −0.5050 vs `cur` −0.5050 — dead parity to the digit.
* **vs Entrance at Oracle 4:** `ie2_lib1_or4` −0.5211 vs `or4_ie3` −0.5225 — Libation **+0.0014
  behind**.
* **vs Anger:** loses all four swaps above.

So the grid-wide ordering is **Anger > Entrance ≥ Libation**, and this is what fires the user's
stated decision rule — *"if Libation is very close to or worse than Entrance/Anger, it should
probably be dropped, because it lacks trample."* Libation is level with Entrance and behind Anger,
which is the exact condition the rule names.

## The gap, and it is being closed

**None of the four swaps is at 30 life — and that is the one place the answer could turn.**

This is not a hypothetical worry. Earlier in this campaign the 30-life run materially narrowed the
Libation recommendation: the last copy *does* earn its slot back in long games when the replacement
is weak, which is why the surviving recommendation is "cut Libation **for Draught and Oracle**"
rather than "cut Libation for anything." The leading lists are now Anger-heavy (A2–A4), so
Libation-vs-Anger at 30 life is load-bearing and unmeasured.

It is also genuinely unpredictable from the 20-life data, because **both cards get better in long
games**:

* **Luxurious Libation** — X scales with available mana, and each resolved instance leaves a 1/1
  Citizen that is itself a copy target for the next cast.
* **Ancestral Anger** — X is `1 + graveyard copies of Ancestral Anger`, so its per-copy payload
  *rises as the game goes on and copies accumulate in the yard*.

Two escalating cards, both compounding with length. The sign of the difference at 30 life does not
follow from the sign at 20.

**Queued (spec amended before the run started).** The 30-life Oracle-4 grid now carries
`or4_ie1_lib2` (A0 L2) and `or4_ie1_lib1_an1` (A1 L1) alongside `or4_ie1_an2` (A2 L0) — reproducing
the sharpest 20-life ladder *exactly*, at the life total where Libation looks best. Eight arms,
12,000 games, seed 8,500,000, `MTG_START_LIFE=30`, preflight clean, all eight verified at residual 7.

If the ladder stays monotone at 30 life, the Libation cut is settled at both life totals against
both competitors and the question is closed. If it inverts, the recommendation has to be
re-opened — and that is exactly why it is being run rather than argued.

---

# CONSOLIDATED RESULTS — the hand-off (2026-08-29)

All four screens have landed and both winners are held-out confirmed. This section supersedes the
per-screen sections above where they disagree; read it alone if you read nothing else.

## The recommendation

**Cut Impolite Entrance to ZERO. Cut Luxurious Libation to ZERO. Max the lifegain package.**

The two leading lists are statistically indistinguishable at **both** life totals:

| | Oracle | Entrance | Anger | Libation | Draught | 20 life | 30 life |
|---|---|---|---|---|---|---|---|
| `or4_ie0_an3` | 4 | 0 | 3 | 0 | 4 | −0.5399 | −0.4768 |
| `ie0_an4` | 3 | 0 | 4 | 0 | 4 | −0.5396 | −0.4764 |
| | | | | | *t =* | *−0.16* | *−0.20* |

Held-out confirms of `or4_ie0_an3`, on seeds disjoint from those that selected it:

```
20 life   screen (8,000,000) -0.5399   held out (8,500,000) -0.5332   pooled -0.5365 / 40,000
30 life   screen (8,500,000) -0.4768   held out (9,000,000) -0.4799   pooled -0.4784 / 24,000
```

Both reproduce within noise (shrinkage t = +0.64 and −0.19). The choice between the two lists is
free; `or4_ie0_an3` is the one carrying held-out confirmation at both life totals, so it is the one
to ship.

## CORRECTION — "one Entrance is free" was wrong

An earlier section of this document reported that a single Impolite Entrance costs nothing, on the
strength of `or4_ie1_an2` vs `ie0_an4` measuring t = −1.32 in the 16-cell screen. **Two later,
independent blocks contradict that**, and the tie does not survive:

```
seed 6,500,000  (20 life)   or4_ie1_an2 behind by  0.0032   t = -1.32   tie
seed 8,000,000  (20 life)   or4_ie1_an2 behind by  0.0050   ~3.3 se     behind
seed 8,500,000  (30 life)   or4_ie1_an2 behind by  0.0089   ~4 se       behind
```

Three blocks, all with the same sign, two of them clear. The first was the smallest effect and the
one that happened to land under the significance line — reporting it as "free" gave a single
non-significant block more weight than it could carry. **The current read is that even one Entrance
costs about 0.005–0.009 turns.**

**The trample caveat still stands and still points the other way.** Impolite Entrance's trample is
unmodelled (the goldfish never blocks), so every Entrance number here is a *lower bound* and no
sample size fixes it. The honest statement to the deck owner is: the engine measures Entrance as
costly at any count, and the one thing it cannot see is the card's real merit. That is a judgment
call for the owner, not a measurement — but it is now a judgment call against a measured cost of
~0.005–0.009, not against a tie.

## Luxurious Libation — settled, with a mechanism

Libation loses on every axis tested, and at 30 life the log evidence explains why the one apparent
exception was not one.

**20 life — four matched 1:1 swaps into Ancestral Anger, Anger won all four:**

```
Or4/E1   A0 L2 -0.5148 -> A1 L1 -0.5272 -> A2 L0 -0.5390    -0.0121/copy, monotone ladder
Or3/E3                                                       -0.0090/copy
Or3/E2                                                       -0.0086/copy
Or4/E2                                                       -0.0038/copy  (weak, ~1.6 se)
```

**30 life — the ladder inverts, and the inversion is NOT Libation.** The mix `or4_ie1_lib1_an1`
(−0.4742) beat all-Anger `or4_ie1_an2` (−0.4679) by 0.0063, which looked like the last Libation
earning its slot back in long games. Replaying 500 unbiased game pairs with full logs and
conditioning on whether Libation was actually cast:

```
UNBIASED sample, 500 pairs (every 24th of 12,000), 30 life
stratum                  pairs   mix    all-Anger    diff
A cast Libation            110   5.482    5.482     +0.000
A never cast Libation      390   4.956    4.964     -0.008
ALL                        500   5.072    5.078     -0.006     (screen said -0.0063)
```

The sample reproduces the screen delta (−0.006 vs −0.0063), so it is a fair window on the effect.
**In the games where Libation was actually cast, the two lists are dead level — +0.000.** The whole
of the mix's advantage sits in games where Libation never appeared, i.e. it is not Libation doing
anything. The 30-life "exception" is not evidence for keeping the card.

Note the *stratified* sample said +0.214 for the same stratum. That sample was drawn from divergent
games balanced across both tails, so its conditional means are **selected, not estimated**. The
unbiased number is the one to believe; the discrepancy is a good illustration of why.

## Other pre-registered questions

* **Gold Rush — VINDICATED.** Every cut loses; `gr2_an2` was the worst of 16 arms. It had been an
  untested premise of fifteen prior screens and is now measured.
* **Entrance → Anger runs monotone to ZERO at both life totals.** 20 life: −0.0090 / −0.0164 /
  −0.0266 / −0.0372 (t = −15.5). 30 life: −0.0049 / −0.0119 / −0.0213 / −0.0316. The
  pre-registered "optimum is interior at 1–3 Entrance" prediction fails at both.
* **The 4th Oracle pays**, though it is interchangeable with the 4th Anger at the E0 corner
  (t = −0.16 / −0.20).
* **Haste is not the reason to keep Entrance.** Board-exposure proxy: removing every haste source
  moves exposure 2.60 → 2.57 turns and "kill from nowhere" 19.9% → 19.9%.

## Confidence, and what would change it

The headline results are not close: Heroism is +0.45 (≈70 se) and the Entrance→Anger ladder is
t = −15.5. The genuinely close calls (`or4_ie0_an3` vs `ie0_an4`) are reported as ties rather than
resolved. Every winner is confirmed on seeds disjoint from those that selected it, and all four
blocks (6.5M / 7.5M / 8.0M / 8.5M) are mutually disjoint.

The one thing no amount of measurement here can settle is the **unmodelled trample** on Impolite
Entrance, which biases against the single card the deck owner most wants to keep.
