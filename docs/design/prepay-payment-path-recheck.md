# Payment-path re-check set — games to re-evaluate after the prepay work

**Status:** OPEN, and deliberately NOT being fixed here. The payment path is owned by another
agent with unpushed work; this document exists so their fix can be checked against a concrete,
pre-built set of games rather than a re-derivation.

**Owner of the defect:** `BatchPrepayMainCasts` / the prepaid-pool spend path in
`src/ai/TurnSolver.cpp`, gated by `MTG_PREPAY_TRUE_COLOURS` (default on).

**The list:** `test/prepay_recheck_cases.tsv` — 418 games, self-contained, with a per-case
`verdict` and `spend` column.
**Start with the 32 games in §2e.** Every one is a game where the pre-fix line needed no laundered
mana, or has a turn *proven* payable without the creature the current engine taps — usually both
(mirrorwing 19, hinata 6, slivers 4, creature_giving 3).
**The tool:** `test/prepay_recheck.py verify --defects` — run it on your tree, read the summary.

What has been established, in order: all 418 checked for the tap-order signature (§2b, 25 hits); all
25 adjudicated (§2c — 16 DEFECT, 1 WARRANTED, 8 lose no attacker at all); and all 418 checked for
whether the PRE-FIX payment was even legal (§2d — **118 LAUNDERED, 132 LEGAL, 168 unpriced**).
That last number is the one to internalise before restoring anything: **about a quarter of this list
is the fix working, not the fix costing.**

---

## 1. What went wrong

The 2026-08-25 colour-honesty batch (`90a537bd..c6743509`) shipped three mana fixes:

| fix | switch | what it does |
|---|---|---|
| ritual float colours | `MTG_LEGACY_RITUAL_WILD=1` | Irencrag Feat's seven mana are RED, not wild; Reality Spasm's untap-refloat carries the untapped sources' true colours |
| staged suspend | `MTG_LEGACY_STAGED_SUSPEND=1` | a card impulse-exiled by Apex of Power can no longer be Suspended |
| **prepaid pool true colours** | `MTG_PREPAY_TRUE_COLOURS=0` | `BatchPrepayMainCasts` pre-loads the pool with the colours actually produced instead of dumping everything unpinned into `ManaPool::wild` |

The first two are unambiguous rules fixes: they delete lines that were never legal. The third is
**also a real soundness fix** — before it, over-produced colourless folded into `wild` could pay a
coloured pip, so a Sol Ring's `{C}{C}` could fund a `{U}`. Nothing below argues for reverting it.

But it changed the **payment path**, and it has a side effect that is not a rules matter: paying for
the *same* line, the engine now sometimes taps a mana creature that the pre-fix path spared. A tapped
creature cannot attack, so the turn deals less damage and the game runs long.

Ground truth was rebaselined **with that side effect baked in** (commit `c6743509`). That is the debt
this document tracks.

## 2. The evidence — `mirrorwing_overnight_d3_s5005` gi309 (4 → 5)

Both arms of the same binary were forced down the recorded line, one with
`MTG_PREPAY_TRUE_COLOURS=0` and one without. **Every decision matched** — same plan indices, same
targets, every action accepted by both engines:

```
T1  land=Game Trail (cast nothing)
T2  land=Mountain;    cast Elvish Mystic; cast Impolite Entrance
T3  land=Forest;      cast Zada, Hedron Grinder
T4  land=Game Trail;  cast Oracle's Restoration; cast Fortifying Draught; cast Gold Rush
```

At the **pre-combat T4 decision**, both arms at 26 life, opponent at 20, identical battlefield
(Elvish Mystic, Zada, 2 Treasure tokens, 4 lands):

| arm | Elvish Mystic | outcome |
|---|---|---|
| `MTG_PREPAY_TRUE_COLOURS=0` | **untapped** → attacks | lethal, wins T4 |
| default (fix on) | **tapped for mana** | only Zada swings for 14 → opponent at 6 → wins T6 |

The T4 payment is `{G}` + `{G}` + `{1}{G}`. Three green pips are available from two Game Trails
(`produces: ["R","G"]`) plus a Forest, with the Mountain covering the generic — so the dork never
needed to be tapped. It is not a legality question; it is a source-selection question.

Reproduce:

```
# the losing arm (current default)
build/Release/mtg "decks/Mirrorwing Dragon/Mirrorwing Dragon.cod" \
  --profile "decks/Mirrorwing Dragon/Mirrorwing Dragon.profile.json" \
  --games 1 --seed 5314 --game-index 309 --depth 3 --budget-ms 10 --ignore-play-profile
#   -> avg (turns) : 5.0000

MTG_PREPAY_TRUE_COLOURS=0 <same command>          # -> avg (turns) : 4.0000

# the forced-line, both-arms comparison that proves the line is identical:
python3 test/gt_line_playable.py overnight --verbose --jobs 1 mirrorwing_overnight_d3_s5005 309
```

**Ruled out.** `MTG_DORK_HOLD_PARTIAL=1` and `MTG_DORK_TAP_LAST=1` do not recover it; only
`MTG_PREPAY_TRUE_COLOURS=0` does. There is no decline and no drop — `MTG_PREPAY_PROBE` reports zero
`PP_WILD`/`PP_UNPAYABLE` (only "<2 casts") and `MTG_AFFORD_AUDIT=2` reports `real drops: 0` on both
arms. So the batch prepay *succeeds*; it just leaves the phase unable to pay for what comes next.

### 2a. ROOT CAUSE — surplus prepaid mana loses its fungibility

The `floating_mana` in the per-decision dumps shows the whole thing. On gi309's turn 4 the accepted
plan is two casts, `Oracle's Restoration {G}` + `Fortifying Draught {G}` — a **two-mana** batch. Both
arms prepay it by tapping **all four lands** (Forest, Game Trail, Game Trail, Mountain), so two mana
of surplus is left floating. Then Oracle's Restoration *draws a card*, and that card is `Gold Rush
{1}{G}`, cast in a second segment:

| after the two casts | leftover float | can it pay `Gold Rush {1}{G}`? |
|---|---|---|
| `MTG_PREPAY_TRUE_COLOURS=0` | `{wild: 2}` | **yes** — wild pays any pip; nothing else is tapped |
| default (fix on) | `{R: 2}` | **no** — red cannot pay the `{G}`. Every land is already tapped, so the only green source left is the reserved **Elvish Mystic**, and it gets tapped. |

So the causal chain is:

1. prepay taps **more sources than the batch needs** and floats the surplus;
2. pre-fix, that surplus was `wild` — *fungible for any later cast in the phase*, which silently
   covered anything the rest of the turn wanted;
3. post-fix the surplus carries its true colour, so a later cast needing a different colour cannot
   use it;
4. the lands are already committed, so the payment falls through to the reserved mana creature;
5. the creature is now tapped and cannot attack — which is what actually costs the game.

**The colour fix is not the defect.** Step 2 was always unsound: it is the same laundering the fix
exists to stop. What the fix exposed is a *latent* defect in step 1 — prepay over-commits sources and
was relying on `wild` fungibility to make that harmless. Repairing the payment path means fixing the
over-tap (tap what the batch needs, or leave the surplus uncommitted), not restoring the laundering.

**The trigger is a cast that appears AFTER the batch is prepaid** — i.e. a mid-phase draw. That is
why Mirrorwing is the deck hit: it is a cantrip-heavy trick deck (`Oracle's Restoration`,
`Impolite Entrance` and `Fists of Flame` all draw), so a second segment with a fresh cast is routine.

Confirmed on all three of Mirrorwing's non-converging games, same signature each time:

| case | leftover, fix OFF | leftover, fix ON | consequence |
|---|---|---|---|
| `d3_s5005` gi309 | `{wild: 2}` | `{R: 2}` | taps Elvish Mystic, 4 → 5 |
| `d3_s7007` gi280 | `{wild: 1}` | `{R: 1}` | taps Elvish Mystic, 4 → 5 |
| `d5_s6006` gi136 | `{wild: 1}` | `{G: 1}` | forced walk fails to win at all |

**Signature to grep for** when triaging any other case: a turn with two or more main-phase segments,
non-empty `floating_mana` carried between them, and a non-land permanent that is tapped in the
fixed arm but not in the control at the pre-combat decision.

An earlier hypothesis — that the reserve ladder's `produced.wild == 0` gate rejects the
creature-holding rung on a dual-land manabase — is **not** what happens here: the dork *is* held
successfully during the batch, and is only tapped afterwards, by the second segment. Recorded so the
next person does not re-run it.

### 2b. Every case traceable to TAP ORDER (all 418 checked)

`test/prepay_recheck.py tapdiff` mechanises the adjudication done by hand for gi309: force both arms
down the recorded line, and at the **pre-combat decision** of each turn diff the set of tapped
NON-LAND permanents. Pre-combat matters — combat taps attackers, so an end-of-turn comparison would
flag every game. Validated against the hand analysis: gi309 reports `T4:Elvish Mystic`, the exact
permanent identified manually.

**All 418 cases were run. 25 show the signature; 393 do not; zero errored.**

| | |
|---|---|
| by deck | mirrorwing 13, creature_giving 6, hinata 6 |
| by tier | overnight 20, smoke 3, regression 2 |
| by attribution | prepay-colours 22, ritual-colours **2**, combination 1 |
| by `walk_class` | EXECUTION-DIFFERS 10, INCONCLUSIVE 7, CHOICE-ONLY 4, REFUSED 2, never walked 2 |
| lost the win entirely | 3 |
| permanents spent | Birds of Paradise 14, Elvish Mystic 9, Ignoble Hierarch 4, Ornithopter of Paradise 4, Sol Ring 2 |

Two things in that table matter more than the headline count:

- **15 of the 25 sit in classes the earlier triage would have dismissed** — 7 `INCONCLUSIVE`,
  4 `CHOICE-ONLY`, 2 `REFUSED`, 2 never walked. `walk_class` alone would have missed them, which is
  why every case was checked rather than the indicted subset.
- **2 are attributed to `ritual-colours`, not `prepay-colours`.** Flipping the ritual switch alone
  restores their score, yet a creature is still tapped differently — so the ritual refloat feeds the
  same pool and the tap-order effect is not exclusive to the prepay switch.

| case | gi | turns | attributed to | walk class | tapped in the FIXED arm only |
|---|---|---|---|---|---|
| `creature_giving_overnight_d0_s4004` | 67 | 6 → 7 | prepay-colours | INCONCLUSIVE | T3:Birds of Paradise; T4:Birds of Paradise; T5:Birds of Paradise; T6:Birds of Paradise; T7:Birds of Paradise |
| `creature_giving_overnight_d0_s6006` | 259 | 4 → 5 | prepay-colours | INCONCLUSIVE | T3:Birds of Paradise |
| `creature_giving_overnight_d0_s6006` | 501 | 5 → 6 | prepay-colours | INCONCLUSIVE | T5:Birds of Paradise |
| `creature_giving_overnight_d0_s8008` | 121 | 6 → 7 | prepay-colours | INCONCLUSIVE | T3:Birds of Paradise; T4:Birds of Paradise; T5:Birds of Paradise; T6:Birds of Paradise; T7:Birds of Paradise |
| `creature_giving_overnight_d0_s8008` | 1958 | 5 → 6 | prepay-colours | REFUSED | T5:Birds of Paradise |
| `creature_giving_smoke_d0_s1001` | 385 | 5 → 6 | prepay-colours | INCONCLUSIVE | T3:Birds of Paradise |
| `hinata_overnight_d0_s10010` | 1941 | 6 → no win | (combination) | INCONCLUSIVE | T4:Ornithopter of Paradise |
| `hinata_overnight_d0_s4004` | 107 | 6 → 7 | ritual-colours | — | T3:Ornithopter of Paradise |
| `hinata_overnight_d0_s6006` | 1370 | 5 → 6 | prepay-colours | CHOICE-ONLY | T4:Ornithopter of Paradise |
| `hinata_overnight_d0_s8008` | 1241 | 5 → 6 | prepay-colours | EXECUTION-DIFFERS | T3:Sol Ring |
| `hinata_overnight_d5_s6006` | 270 | 6 → 7 | prepay-colours | INCONCLUSIVE | T5:Ornithopter of Paradise |
| `hinata_regression_d0_s2002` | 934 | 8 → no win | ritual-colours | — | T7:Sol Ring |
| `mirrorwing_overnight_d0_s10010` | 1994 | 6 → 7 | prepay-colours | EXECUTION-DIFFERS | T3:Elvish Mystic |
| `mirrorwing_overnight_d0_s4004` | 245 | 5 → 7 | prepay-colours | EXECUTION-DIFFERS | T3:Elvish Mystic |
| `mirrorwing_overnight_d0_s4004` | 284 | 8 → no win | prepay-colours | REFUSED | T5:Elvish Mystic |
| `mirrorwing_overnight_d0_s4004` | 1946 | 4 → 5 | prepay-colours | EXECUTION-DIFFERS | T4:Ignoble Hierarch |
| `mirrorwing_overnight_d0_s6006` | 566 | 7 → 8 | prepay-colours | EXECUTION-DIFFERS | T4:Elvish Mystic |
| `mirrorwing_overnight_d0_s6006` | 852 | 5 → 6 | prepay-colours | CHOICE-ONLY | T4:Ignoble Hierarch |
| `mirrorwing_overnight_d0_s8008` | 173 | 5 → 6 | prepay-colours | EXECUTION-DIFFERS | T4:Ignoble Hierarch |
| `mirrorwing_overnight_d3_s5005` | 309 | 4 → 5 | prepay-colours | EXECUTION-DIFFERS | T4:Elvish Mystic |
| `mirrorwing_overnight_d3_s7007` | 280 | 4 → 5 | prepay-colours | EXECUTION-DIFFERS | T4:Elvish Mystic |
| `mirrorwing_overnight_d5_s7007` | 280 | 4 → 5 | prepay-colours | EXECUTION-DIFFERS | T4:Elvish Mystic |
| `mirrorwing_regression_d3_s3003` | 67 | 6 → 7 | prepay-colours | CHOICE-ONLY | T4:Ignoble Hierarch |
| `mirrorwing_smoke_d0_s1001` | 490 | 7 → 8 | prepay-colours | EXECUTION-DIFFERS | T6:Elvish Mystic |
| `mirrorwing_smoke_d0_s1001` | 921 | 7 → 8 | prepay-colours | CHOICE-ONLY | T4:Elvish Mystic |

**Caveat that must not be dropped — now DISCHARGED per case in §2c, but read it first.** A tap-order
diff proves the fixed arm spends a mana creature the control did not, on the same line. It does
**not** by itself prove the extra tap is wrong: if the control was paying with laundered mana it
should never have had, then the fixed arm *correctly* has to find a real source, and a mana dork is a
real source. Nor does it prove the tap cost anything — the table above is sampled at a claude-play
decision, before the turn's last segment pays, so a creature the control taps *late in its own main*
reads here as spared.

§2c settles both questions for all 25. Of these rows, **8 lose no attacker on the control's own line**
and **1 is a payment the control had no legal way to make** — so 16, not 25, are payment-path damage.
Read the two tables together: this one is the signature, §2c is the verdict.

### 2c. VERDICT per case — was the bill payable WITHOUT the creature? (all 25 settled)

`test/prepay_recheck.py adjudicate` answers, for every TAP-ORDER case, the question §2b left open and
that gi309 was settled by hand on:

> was the pre-combat bill payable, under TRUE colours, from sources the control also had, **without**
> the disputed creature?

It needs no engine change. The control's own game log carries the board per phase and each cast's
`manaPaid`, which is enough to reconstruct (a) the bill the turn's pre-combat mains actually paid and
(b) every mana source that stood untapped when the main opened, priced by `produces` /
`produces_amount` / `sac_for_mana_amount` out of `cards.json`. Payability is then a bipartite matching
of pips to sources — a `{G}` pip needs a source that makes green, a generic pip takes anything, and
each source pays once. **`_units_for` and `_payable` are the whole model; everything else is bookkeeping.**

Two questions are kept apart on purpose, because collapsing them is what made the first two passes of
this checker wrong:

| | question | what a "no" means |
|---|---|---|
| **avoidability** | was the bill payable from everything available, minus every copy of the disputed creature? | the tap was REQUIRED, not chosen — the fix is right |
| **spend legality** | was the control's OWN payment colour-legal? | the pre-fix score came off a laundered payment, so `pre_fix` is not automatically the number to restore |

They are independent: a turn can launder *and* still have had a creature-free legal payment (two cases
below do exactly that).

**Result: 25 of 25 settled, no case left unmodelled and no verdict resting on an assumption.**

| verdict | games | meaning |
|---|---|---|
| **DEFECT** | **16** | the bill was payable without the creature. The tap is source selection, not necessity — these are the games a repaired payment path should recover |
| **WARRANTED** | 1 | the available sources genuinely cannot cover the bill; a creature had to be tapped |
| **CONTROL-ALSO-TAPS** | 8 | the control reaches combat with no free copy either, so no attacker was lost — the signature does not survive on the line ground truth records |

| | |
|---|---|
| DEFECT by deck | mirrorwing 9, hinata 4, creature_giving 3 |
| DEFECT by tier | overnight 13, smoke 2, regression 1 |
| DEFECT by attribution | prepay-colours 14, ritual-colours 1, combination 1 |
| DEFECT that also laundered | 2 (`hinata d5_s6006` gi270, `mirrorwing smoke` gi921) |

Three things in that split are worth stating outright:

- **The headline count from §2b falls by a third.** 8 of the 25 lose no attacker at all. `tapdiff`
  samples at a claude-play decision — *before* the turn's last segment pays — so a creature the
  control taps late in its own main reads there as spared. Measured at the declare-attackers state
  instead, the control has no free copy either. Those 8 are not evidence of anything and should not
  be counted as payment-path damage.
- **The one WARRANTED case is a clean example of the fix working.** `mirrorwing_overnight_d0_s4004`
  gi245 T3 pays three `{G}` pips off a Forest, a Mountain and a second Forest — the Mountain makes
  only red, so the third green came out of the wild pool. Post-fix a real green source is required,
  the only ones left are the two Elvish Mystics, and one gets tapped. That is not a regression to
  repair; it is a line that was never legal.
- **Laundering and avoidability are not the same thing.** gi921 T4 pays `{1}{G}` + `{1}{R}` + `{G}`
  and takes one green out of a Sandstone Needle's red — illegal, so the fix is right to refuse it.
  But eight legal sources stood free of the Mystic (two Forests, two Ignoble Hierarchs, two Sandstone
  Needles), and two of those Hierarchs are 0-power and make `{G}`. The engine had to find real green;
  it did not have to find it in the one creature that was going to deal damage.

| case | gi | turns | attributed to | tapped | verdict | evidence |
|---|---|---|---|---|---|---|
| `creature_giving_overnight_d0_s4004` | 67 | 6 &rarr; 7 | prepay-colours | Birds of Paradise | **DEFECT** | T3 2 pips &larr; 3 free sources, colour-legal; T4 no free copy at combat; T5 no free copy at combat; T6 2 pips &larr; 5 free sources, colour-legal; T7 no pre-combat cast |
| `creature_giving_overnight_d0_s6006` | 501 | 5 &rarr; 6 | prepay-colours | Birds of Paradise | **DEFECT** | T5 3 pips &larr; 5 free sources, colour-legal |
| `creature_giving_overnight_d0_s8008` | 121 | 6 &rarr; 7 | prepay-colours | Birds of Paradise | **DEFECT** | T3 no free copy at combat; T4 2 pips &larr; 3 free sources, colour-legal; T5 1 pips &larr; 3 free sources, colour-legal; T6 no free copy at combat; T7 no pre-combat cast |
| `hinata_overnight_d0_s10010` | 1941 | 6 &rarr; no win | combination | Ornithopter of Paradise | **DEFECT** | T4 2 pips &larr; 5 free sources, colour-legal |
| `hinata_overnight_d0_s4004` | 107 | 6 &rarr; 7 | ritual-colours | Ornithopter of Paradise | **DEFECT** | T3 2 pips &larr; 3 free sources, colour-legal |
| `hinata_overnight_d0_s6006` | 1370 | 5 &rarr; 6 | prepay-colours | Ornithopter of Paradise | **DEFECT** | T4 4 pips &larr; 5 free sources, colour-legal |
| `hinata_overnight_d5_s6006` | 270 | 6 &rarr; 7 | prepay-colours | Ornithopter of Paradise | **DEFECT** | T5 4 pips &larr; 7 free sources, colour-legal **but the control laundered** |
| `mirrorwing_overnight_d0_s10010` | 1994 | 6 &rarr; 7 | prepay-colours | Elvish Mystic | **DEFECT** | T3 3 pips &larr; 4 free sources, colour-legal |
| `mirrorwing_overnight_d0_s4004` | 1946 | 4 &rarr; 5 | prepay-colours | Ignoble Hierarch | **DEFECT** | T4 3 pips &larr; 3 free sources, colour-legal |
| `mirrorwing_overnight_d0_s6006` | 566 | 7 &rarr; 8 | prepay-colours | Elvish Mystic | **DEFECT** | T4 4 pips &larr; 4 free sources, colour-legal |
| `mirrorwing_overnight_d3_s5005` | 309 | 4 &rarr; 5 | prepay-colours | Elvish Mystic | **DEFECT** | T4 4 pips &larr; 4 free sources, colour-legal |
| `mirrorwing_overnight_d3_s7007` | 280 | 4 &rarr; 5 | prepay-colours | Elvish Mystic | **DEFECT** | T4 4 pips &larr; 4 free sources, colour-legal |
| `mirrorwing_overnight_d5_s7007` | 280 | 4 &rarr; 5 | prepay-colours | Elvish Mystic | **DEFECT** | T4 4 pips &larr; 4 free sources, colour-legal |
| `mirrorwing_regression_d3_s3003` | 67 | 6 &rarr; 7 | prepay-colours | Ignoble Hierarch | **DEFECT** | T4 6 pips &larr; 6 free sources, colour-legal |
| `mirrorwing_smoke_d0_s1001` | 490 | 7 &rarr; 8 | prepay-colours | Elvish Mystic | **DEFECT** | T6 3 pips &larr; 5 free sources, colour-legal |
| `mirrorwing_smoke_d0_s1001` | 921 | 7 &rarr; 8 | prepay-colours | Elvish Mystic | **DEFECT** | T4 5 pips &larr; 8 free sources, colour-legal **but the control laundered** |
| `mirrorwing_overnight_d0_s4004` | 245 | 5 &rarr; 7 | prepay-colours | Elvish Mystic | **WARRANTED** | T3 3 pips &larr; 3 free sources |
| `creature_giving_overnight_d0_s6006` | 259 | 4 &rarr; 5 | prepay-colours | Birds of Paradise | **CONTROL-ALSO-TAPS** | T3 no free copy at combat |
| `creature_giving_overnight_d0_s8008` | 1958 | 5 &rarr; 6 | prepay-colours | Birds of Paradise | **CONTROL-ALSO-TAPS** | T5 no free copy at combat |
| `creature_giving_smoke_d0_s1001` | 385 | 5 &rarr; 6 | prepay-colours | Birds of Paradise | **CONTROL-ALSO-TAPS** | T3 no free copy at combat |
| `hinata_overnight_d0_s8008` | 1241 | 5 &rarr; 6 | prepay-colours | Sol Ring | **CONTROL-ALSO-TAPS** | T3 no free copy at combat |
| `hinata_regression_d0_s2002` | 934 | 8 &rarr; no win | ritual-colours | Sol Ring | **CONTROL-ALSO-TAPS** | T7 no free copy at combat |
| `mirrorwing_overnight_d0_s4004` | 284 | 8 &rarr; no win | prepay-colours | Elvish Mystic | **CONTROL-ALSO-TAPS** | T5 no free copy at combat |
| `mirrorwing_overnight_d0_s6006` | 852 | 5 &rarr; 6 | prepay-colours | Ignoble Hierarch | **CONTROL-ALSO-TAPS** | T4 no free copy at combat |
| `mirrorwing_overnight_d0_s8008` | 173 | 5 &rarr; 6 | prepay-colours | Ignoble Hierarch | **CONTROL-ALSO-TAPS** | T4 no free copy at combat |

**How the checker refuses to guess.** Four classes of turn are voided rather than scored, because
each under-counts one side of the ledger in the direction that could invent a verdict:

| guard | why |
|---|---|
| `{X}` spells | `manaPaid` is inconsistent about X — Crackle with Power records `{4}{R}{R}` with X folded in, Reality Spasm records `{U}{U}` for an X of 4. Under-counting pips makes a bill look payable |
| Reality Spasm's untap-refloat | a source taps twice in one main, so the source count is short |
| a `{T}` ability that is not a mana ability | booking its source as mana it never made |
| an uncoloured ritual float | mana this checker will not colour |

Three more subtleties it had to be taught, each of which produced a wrong verdict first:

- **Copies, not names.** Creature Giving holds two Birds of Paradise; a name-keyed "is it tapped"
  reads yes when one is tapped and the other is free to swing.
- **Summoning sickness.** gi284 casts a second Elvish Mystic on the turn it taps its first. The new
  copy is untapped and cannot attack, so keying on the name said an attacker survived a board where
  none did — it is matched by card NUMBER against the board as the main opened.
- **Sources that leave the battlefield.** A Treasure vanishes because it was sacrificed for mana,
  Sandstone Needle sacrifices itself as its last depletion counter comes off, and a Karoo land
  bounces a land that was tapped for mana first. Dropping those made four cases read "4 pips vs 1
  source". They are counted, and the count is FORCED rather than assumed whenever the surviving
  sources fall short of a bill that demonstrably got paid.

**What a DEFECT verdict does and does not claim.** It claims a legal creature-free payment existed
for that turn's pre-combat bill. It does not claim the engine's solver can *find* it (that is the
payment path's problem, which is the point), nor that the game recovers to exactly `pre_fix` — for
the two cases that also laundered, the pre-fix number came partly off an illegal payment, so the
right post-repair value may sit between `pre_fix` and `post_fix`. Treat DEFECT as "this game should
improve", not "this game should read N".

### 2d. Was the PRE-FIX line's own payment even LEGAL? (all 418, not just the 25)

§2c settles the 25 games that carry the tap-order signature. The other 393 do not — and "no
signature" is evidence they are not *this* defect, not evidence the regression is right. There is
one more question the same control logs answer for **every** case in the set:

> was the pre-fix line's own payment colour-legal, under true colours, on every turn?

`test/prepay_recheck.py legality` runs it. Same ledger as §2c, whole turn rather than pre-combat
only, and the test is deliberately one-sided: a turn is called **LAUNDERED only when the sources the
control tapped supply enough TOTAL mana but cannot supply the COLOURS**. If the sources fall short on
raw count, this model is missing production and says nothing.

| verdict | games | what it means for the rebaseline |
|---|---|---|
| **LAUNDERED** | **118** | `pre_fix` came off a payment the rules forbid. The game getting worse is the fix WORKING — do not restore the old number |
| **LEGAL** | **132** | every priceable turn balances under true colours. The old line was legitimate, so something else moved this game |
| UNPRICED | 168 | at least one turn this model refuses to price — `{X}` left unresolved in `manaPaid`, a Reality Spasm untap-refloat, or sources short on raw count |

Two examples, both checked by hand against the log:

- `mirrorwing_overnight_d0_s4004` gi452 T2 casts Elvish Mystic `{G}`, Oracle's Restoration `{G}` and
  Ignoble Hierarch `{G}` — three green pips — tapping a Forest, an Elvish Mystic **and a Mountain**.
  The Mountain makes only red.
- gi1688 T3 casts `{R}` + `{1}{R}` + `{R}` off an Ignoble Hierarch, a Gruul Turf and a Forest. Three
  red pips; the only red sources are the Hierarch and one half of the Karoo's `{R}{G}`. Two.

**By deck** — the picture is not the one §3 originally drew:

| deck | n | LAUNDERED | LEGAL | UNPRICED |
|---|---|---|---|---|
| hinata | 208 | 56 | 14 | 138 |
| mirrorwing | 84 | **36** | **30** | 18 |
| auras | 45 | 0 | 43 | 2 |
| dragonstorm | 37 | 24 | 5 | 8 |
| creature_giving | 19 | 2 | 17 | 0 |
| slivers | 19 | 0 | **19** | 0 |
| minotaur | 4 | 0 | 4 | 0 |
| fivecolour / goblins | 2 | 0 | 0 | 2 |

**By attribution** — this is the cleanest confirmation the switch bisect was telling the truth:

| attributed to | n | LAUNDERED | LEGAL | UNPRICED |
|---|---|---|---|---|
| prepay-colours | 217 | 82 | 74 | 61 |
| ritual-colours | 138 | 34 | 9 | 95 |
| aura-fetch-order | 45 | **0** | 43 | 2 |
| staged-suspend | 6 | 0 | 2 | 4 |
| bestow-signature | 4 | **0** | 4 | 0 |
| both mana fixes / combination / none | 8 | 2 | 0 | 6 |

Zero laundering under `aura-fetch-order` and `bestow-signature`, 43 + 4 outright LEGAL: those two are
policy and enumeration changes with no payment component at all, exactly as claimed — now measured
rather than asserted. The prepay-attributed set splits nearly in half, which is the substantive
finding: **about half of what the prepay fix "cost" was never legally ours.**

**Crossed with the forced-walk classes**, the indicted class stops being one thing:

| walk_class | n | LAUNDERED | LEGAL | UNPRICED |
|---|---|---|---|---|
| EXECUTION-DIFFERS | 44 | 13 | **21** | 10 |
| REFUSED | 59 | **32** | 15 | 12 |
| INCONCLUSIVE | 88 | 36 | 22 | 30 |
| CHOICE-ONLY | 33 | 3 | 16 | 14 |

`REFUSED` mostly resolves the way the fix wants: 32 of 59 refusals are the engine declining a line
whose payment was genuinely illegal. `EXECUTION-DIFFERS` goes the other way — 21 of 44 played an
identical forced line, got a worse result, and had a fully legal pre-fix payment. Those 21 are the
strongest recovery candidates in the whole set outside §2c.

### 2e. The priority list — 32 games

Union of §2c's 16 DEFECT and §2d's 21 EXECUTION-DIFFERS ∧ LEGAL (five games are in both):
**mirrorwing 19, hinata 6, slivers 4, creature_giving 3; overnight 25, regression 5, smoke 2.**

Each is a game where the pre-fix line either needed no laundering at all, or has a turn *proven*
payable without the creature the current engine taps — usually both. `slivers` is worth calling out:
19 of 19 slivers cases are LEGAL with zero laundering anywhere in the deck, so its four
EXECUTION-DIFFERS games have no colour-honesty explanation whatsoever.

## 3. Scope — what is and is not suspect

Superseded in one respect by §2d, and the correction is worth stating plainly: an earlier draft of
this table called mirrorwing **"NOT warranted"** on the strength of gi309. Measured across all 84 of
its cases, mirrorwing is **36 LAUNDERED / 30 LEGAL / 18 unpriced** — mixed, not clean. gi309 is a real
defect and so are 8 more of its games, but roughly as many mirrorwing regressions are the fix
correctly refusing a payment the deck never legally had. No deck in this set is all one thing except
auras, slivers and minotaur (zero laundering) and dragonstorm (almost all of it).

| deck family | attributed to | verdict |
|---|---|---|
| dragonstorm | ritual colours | **warranted**, and now measured: 24 of 37 LAUNDERED, 0 clean defects. T2 Karrthus `{4}{B}{R}{G}` off Unclaimed Territory + Mountain is the shape — two distinct off-red pips, one non-red source, the second came from Irencrag's wild. |
| hinata | ritual colours (+ prepay) | **mostly warranted**: 56 LAUNDERED vs 14 LEGAL, but 138 of 208 are unpriced (Reality Spasm's untap-refloat and unresolved `{X}`), so the split is the least settled in the set. 4 games are proven DEFECT in §2c. |
| mirrorwing | prepay colours | **MIXED** — 36 LAUNDERED, 30 LEGAL. 9 proven DEFECT (§2c) plus 15 EXECUTION-DIFFERS ∧ LEGAL (§2d). Both the defect and the correction are real here. |
| slivers | prepay colours | **NOT warranted** — 19 of 19 LEGAL, zero laundering anywhere in the deck, yet 4 games differ on an identical forced line. No colour-honesty explanation at all. |
| creature_giving | prepay colours | mostly clean: 17 LEGAL / 2 LAUNDERED, 3 proven DEFECT. |
| auras / minotaur | aura-fetch-order, bestow-signature | out of scope for the payment path, and now confirmed rather than assumed: **zero** laundered turns across 49 games. Policy/enumeration changes, not payment. |

**ANSWERED** (this section previously flagged it as open): whether hinata or dragonstorm movers also
lose a creature to the tap order. They do — §2b finds 6 hinata cases carrying the signature, of which
§2c proves 4 are DEFECT — so attribution to the ritual switch does not rule the symptom out. It also
does not rule it in: dragonstorm has none.

## 4. The case set

`test/prepay_recheck_cases.tsv` — every game whose outcome the batch made **worse**, versus
`90a537bd` (the commit before it). Columns:

```
key  gi  deck  profile  seed  game_index  depth  budget  pre_fix  post_fix  attribution  walk_class
```

`deck`/`profile`/`seed`/`game_index`/`depth`/`budget` are baked in, so the file is usable from a
checkout that does not share this repo's `gt_logs` or `explain_game.py` state. `pre_fix` is the win
turn before the batch, `post_fix` the rebaselined value now (`-1` = no win inside `max_turns`).

### Classified results (all 418, run 2026-08-26)

**Attribution** — which switch, flipped alone, restores the pre-batch score:

| attribution | games |
|---|---|
| `prepay-colours` | **217** |
| `ritual-colours` | 138 |
| `aura-fetch-order` | 45 |
| `staged-suspend` | 6 |
| both mana fixes | 5 |
| `bestow-signature` | 4 |
| combination / none | 3 |

**`walk_class`** — computed for the 224 prepay-attributed cases only (the other 194 are
`aura-fetch-order`, `staged-suspend` and `bestow-signature`, which are not payment-path changes):

| class | games | meaning |
|---|---|---|
| **EXECUTION-DIFFERS** | **44** | both arms forced down the identical recorded line, different result. Cannot be search, ranking or enumeration. **This is the indicted class.** |
| REFUSED | 59 | the fixed engine refuses an action of the old line and the control does not |
| INCONCLUSIVE | 88 | the control refuses the same turn — the forced walk drifted, not a finding |
| CHOICE-ONLY | 33 | the old line still executes fine; only the plan the search *picked* moved |

**EXECUTION-DIFFERS spans three decks and all three tiers** — mirrorwing 32, hinata 8, slivers 4;
five of them lost the win entirely (`post_fix = -1`). So this is **not** a Mirrorwing curiosity, which
is what an earlier draft of this document assumed.

### Reading the classes honestly

- **EXECUTION-DIFFERS is the triage class, not a verdict.** It proves the *execution* changed, not
  that the old outcome was legal. On hinata the same signature appears with the leftover reading
  `{R:1, C:1}` — and that `C` is a Sol Ring's colourless, which is exactly the laundering the fix
  exists to stop. So a hinata case here may be split between the legitimate correction and the
  over-tap side effect, and each still needs its legality answered before assuming it should recover.
  Mirrorwing gi309 is a defect *because* its T4 cost was independently shown payable without the dork.
- **REFUSED is likewise ambiguous** for the same reason: a refusal is correct when the old line truly
  needed laundered mana, and a defect when the line is legal but the over-committed surplus made it
  unpayable in that order. 59 cases, mirrorwing 29 / hinata 28 / creature_giving 2 — none individually
  adjudicated.
- **INCONCLUSIVE means the instrument could not speak**, not that the case is clean. Forcing a whole
  game re-derives every sub-decision, and a defaulted scry reorders the library; where the control
  hits the same wall, the walk drifted. 88 of 224 land here, so absence of evidence is common.

## 5. How to use it

On a tree with the payment-path work applied, after `./build.sh`:

```
python3 test/prepay_recheck.py verify --jobs 22
```

It runs every case autonomously and reports per game:

| class | meaning |
|---|---|
| `RECOVERED` | now at or better than `pre_fix` — the defect is gone for this game |
| `STILL-WORSE` | unchanged from `post_fix` |
| `MOVED` | a third value; inspect before concluding anything |

Control result, for comparison: on `c6743509` (the defective binary) every case reads
`STILL-WORSE` by construction, so any `RECOVERED` count above zero is signal.

`--filter mirrorwing` narrows it; `--bin PATH` points at a different build. To re-derive the set
itself (only needed if the baseline moves): `python3 test/prepay_recheck.py build --baseline <sha>`.
To recompute the `walk_class`/`attribution` columns: `classify --jobs N` (expensive — it forces both
arms down every prepay-attributed line).

**The 16 DEFECT games in §2c are the sharpest signal available here** — each one has a turn *proven*
payable without the creature the current engine taps, so a repaired payment path should move them.
The verdict is a committed column in the tsv, so:

```
python3 test/prepay_recheck.py verify --defects --jobs 22    # just the 16 proven ones
```

On the current binary all 16 read `STILL-WORSE`, which is the control result — any `RECOVERED` is
signal. (`verify` normalises the `-1` an unwon game carries in `gt_logs` to the loss-penalized
`max_turns+1` a run reports; comparing them raw made every lost game read `MOVED`.)

To regenerate the verdict columns from scratch:

```
python3 test/prepay_recheck.py tapdiff    --jobs 22     # 418 games -> the 25-case signature set
python3 test/prepay_recheck.py adjudicate --jobs 22     # those 25 -> the `verdict` column (fast)
python3 test/prepay_recheck.py legality   --jobs 22     # all 418  -> the `spend` column (~7 min)
```

Both run the CONTROL (all fixes off) and read its game log, so neither needs your fix to be finished
and neither re-derives anything from this repo's ground truth. Both write their column back into the
committed tsv.

## 6. What to do with the result

**The recovered games must be rebaselined back.** They are currently committed ground truth at their
*worse* values, so they will read as "improvements" on the next run and the harness will flag them:

1. re-run the FULL suite per tier — smoke, regression, overnight — never `--deck=X`
   (a scoped run promotes other decks' stale `.wins`);
2. run each tier **twice** and diff `test/results/<mode>.env` before accepting. This is not
   boilerplate: an overnight run during this batch disagreed with three other runs on exactly one
   deck's twelve cells, and `--accept` would have written that into ground truth silently. See
   `docs/design/viewer-feedback-2026-08-25.md`;
3. `bash test/regression.sh <mode> --accept` per tier;
4. push and watch CI — the Linux/Windows determinism-parity job is the one that matters.

## 7. Honest limits of this document

- The root cause in §2a is **established** for Mirrorwing (3 of 3 cases, same signature, read
  directly off `floating_mana`). What is NOT established is *why* prepay taps four lands for a
  two-mana batch — whether that is deliberate ("commit the turn's sources up front") or itself a
  bug. That question lives inside the payment path and was left alone on purpose.
- ANSWERED since the first draft: the legality adjudication — "was the bill payable without the
  creature?" — now stands **per case for all 25 tap-order rows** (§2c), not just gi309. 16 are proven
  DEFECT, 1 WARRANTED, 8 lose no attacker on the control's line at all.
- ANSWERED since the first draft: the 393 non-tap-order cases are no longer bare classification —
  §2d prices the pre-fix payment of all 418. What remains genuinely open is the **168 UNPRICED**,
  138 of them hinata, where a Reality Spasm untap-refloat or an unresolved `{X}` in `manaPaid` means
  the ledger cannot balance the turn either way. Hinata's split (56 LAUNDERED / 14 LEGAL / 138
  unpriced) is therefore the least settled part of this document.
- §2d is one-sided on purpose and only ever accuses: a turn is LAUNDERED only when the sources
  supply enough TOTAL mana but the wrong COLOURS. Where the count itself falls short the model is
  missing production and abstains. So 118 is a **lower** bound on laundering, and 132 LEGAL is the
  softer of the two numbers — it means "no laundering this model can see", not "audited clean".
- §2c's model is a bipartite matching over `cards.json` production, not the engine's solver. It prices
  what the CARDS allow. Where it says a creature-free payment existed, the engine may still fail to
  find it — that is the defect, not a disagreement. Where a turn contains an `{X}` cost, an
  untap-refloat, a non-mana `{T}` ability or an uncoloured ritual float, the turn is voided rather
  than scored; after the guards were added, no case needed voiding.
- ANSWERED since the first draft: the signature is **not** confined to Mirrorwing. EXECUTION-DIFFERS
  covers hinata (8) and slivers (4) as well, and 217 of the 418 cases attribute to `prepay-colours`;
  §2c's DEFECT set spans mirrorwing, hinata and creature_giving.
- dragonstorm has **no** prepay-attributed case at all: all 37 of its entries are `ritual-colours`,
  which is why it is the one deck whose regression is unambiguously warranted.
- The 418 cases are games that got **worse**. 125 got better over the same batch; those are not
  tracked here and some may be luck of the same re-ranking.
- `pre_fix` is not automatically the "right" answer. For the ritual-colour decks the pre-fix value
  was produced by an *illegal* line, so a case reading `STILL-WORSE` there is correct behaviour, not
  a failure. This holds inside the DEFECT set too: gi270 and gi921 are proven avoidable *and* the
  control laundered, so their repaired value may sit between `pre_fix` and `post_fix`. Read a DEFECT
  as "this game should improve", not "this game should read N".
