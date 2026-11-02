# Sac-for-mana outlets: model them as a LAST-RANKED MANA SOURCE, not a searched action

## STATUS 2026-10-30 (pm, wip/sacpay): the eight honest losses ROOT-CAUSED AND FIXED by mechanism; MTG_SUBSET_SAC_OUTLETS' d0 regression fixed. Lever still default OFF -- adoption is the USER's call.

### The mechanisms (each a defect, none a tuning constant)

1. **Fodder is the LAST source of the whole payment, not the last rank of the greedy** (`TapForCostSharedImpl`,
   fodder-last block; `g_pay_fodder_off`). The payer's greedy ranks a body behind every real source, but a
   CONVERSION source's fed mode (Three Tree City's "{2},{T}: one {R} per Goblin", filters) is reached only by
   the backtracker, which runs only when the greedy FAILS -- and with bodies in reach the greedy never fails,
   it eats. Goblins s4113506 gi3505 T3 main 2: six Siege-Gang shots cost {12}; the Mountains fed into the City
   make twelve {R} (the OFF arm's T3); the greedy paid Mountains + the City's {C} + nine Goblins, three
   victims left, T4. Now the FULL payer (greedy, hold rungs, backtracker) runs first with no fodder in reach --
   exactly the OFF arm's payer -- and only if that fails does a body get eaten. A failed attempt restores
   everything (the executor's accounting pool included). Also recovered: Fungus gi3780 / gi3847 / gi5550 /
   2HG gi743 (a Saproling eaten for a {G} the Brightcap Badger grant would have tapped).
2. **...and as FEW bodies as the payment can be made with** (`g_pay_fodder_cap`). The greedy's per-pip needs
   order prices a land as scarcer than an abundant body, so it ate bodies for the coloured pips and left lands
   up: Goblins d0 s4154256 gi4255 / s4155580 gi5579 T5 main 2, three shots {3}{R}{R}{R} off five Mountains --
   it ate THREE tokens and left two Mountains up, two victims, two shots (T6; OFF floats one Goblin, T5). The
   payment is retried with at most k bodies, k rising from the fewest the fodder-free supply could leave owed;
   the last k is every body (the old uncapped payment), so nothing payable before is refused. The backtracker
   is skipped inside the capped attempts (it never sees fodder, so it would re-run the solve that failed).
3. **An ORDER of the same casts that pays without a body is preferred** (`PayableCastOrderCore`, both apply
   worlds). With fodder credited in `AvailableManaPool`, an order payable only by eating projected payable and
   was kept, while another order of the same casts paid from the board: Fungus 2HG s4245015 gi5014 T3,
   Sporecrown Thallid then Brightcap Badger ate a Saproling for the Badger's {G}; Badger first pays off Hickory
   Woodlot + Ancient Tomb and its grant taps Mycon and a Saproling for the Thallid (OFF T5, ON T6). The decider
   now looks for a payable order judged WITHOUT fodder first (the OFF arm's decider), then with.
4. **Executor site-9 / pod continuations dropped Vial puts** (`AIEngine::TakeTurn`, `pe_cont_order` /
   `pod_cont_order` held CastFromHand only). The rollout's `apply_continuation_plan` sequences an
   ActivateVial like a cast; the executor skipped it, so Goblins s4113005 gi3004 T4 (Muxus -> Matron ->
   Goblin Chieftain, Vialed in for haste: 19 damage) realised the put in main 2 and won T5. Lockstep fix,
   lever-independent; scenario `test/scenarios/goblins_site9_vial_continuation.json`.
5. **Staged cards counted toward the Vial charge** (`WantVialCharge`, shared). A staged card (impulse-exiled,
   on an adventure) cannot be Vialed (CollectActions already skipped it), and the worlds hold it differently at
   upkeep (simulated: in `hand` with `m_is_staged`; executor: in `staged_cards`). The rollout climbed a Vial for
   a Rundvelt-staged Goblin Matron that the executor did not: Goblins 2HG s4323206 gi3205 T4 proven, T5 realised.
   The Vial put sites (`deploy_via_vial` / `apply_vial`) now also skip a staged copy. Lever-independent.
6. **The greedy charged eaten bodies one attack's power against a cast credited for every remaining attack**
   (`EatenBodyValue`; the d0 runner and the playout beyond the horizon only). `EvalCard` credits a creature
   cast power x attacks; the eat charge was power x 1 (x2 pre-combat), so eating the board for any castable
   creature read as profit. And the SEARCHED outlet's own sacrifices (`SacForMana`, eval 0) were not charged
   at all -- harmless while the colour-presence gate hid those subsets, exposed by `MTG_SUBSET_SAC_OUTLETS`
   (ab133f122): Goblins d0 s4530477 gi476 T2 main 2, two Piledrivers and Krenko off seven sacrificed Goblins
   (T3 -> unwon). Both now charge `EatenBodyValue` = power x the body's attacks still to come x DMG.

Mechanisms 1-3 are inert with the lever off or no outlet live (one null test). **4, 5 and 6 are
lever-independent and change OFF-arm play** (4 and 5 are executor/rollout lockstep fixes; 6 re-prices the
greedy's searched-outlet sacrifices), so smoke/regression ground truth for Goblins (and possibly other Vial
decks: Knights, Minotaur, Pirates, Soldiers, Vampires, WhiteKnights, slivers_vial, LandfallElementals) will
move -- NOT accepted here. (With 1-4 only, the OFF arm was byte-identical to 49facbae on 144 jobs / 48,000
games of the 41x seeds.)

### Witnesses and the eight losses (d8 b0, lever ON, final binary)

All four witnesses T3 (2HG s3212906 gi2905 with `MTG_LACKEY_DECLINE=0`, s3313242 gi3241, s3313591 gi3590,
2HG s3620038 gi37). The eight honest losses of the morning round now reach the OFF arm's turn: Fungus gi3780 /
gi3847 / gi5550 / 2HG gi743 T4, Goblins gi3004 T4, gi3505 T3, d0 gi4255 / gi5579 T5. Pashalik / Rundvelt /
Mogg War Marshal boards still keep the searched action (unit test "a death that PAYS keeps the searched action").

### The held-out A/B -- FRESH seeds 44x0001, ONE pooled batch, 96,000 games (48,000 per arm)

Goblins 4410001 / 2HG 4420001 / Fungus 4430001 / Fungus 2HG 4440001 / d0 4450001, 4460001 / d3 4470001,
4480001; `base` = lever OFF, `pay` = lever ON (`MTG_SAC_MANA_ACT_DEMAND` on in both), 8 threads on a box shared
with the artifact queue (so `ms` is noisy; `units` is deterministic).

| cell | games | `pay` d(avg) | faster / slower | net | units | ms |
|---|---|---|---|---|---|---|
| Goblins b60 | 8,000 | **-0.0090** | 72 / 0 | -72 | 0.959x | 1.022x |
| Goblins 2HG b60 | 6,000 | **-0.0093** | 58 / 2 | -56 | 0.975x | 1.040x |
| Goblins d3 | 3,000 | **-0.0083** | 25 / 0 | -25 | 0.990x | 1.023x |
| Goblins d0 | 8,000 | +0.0027 (se 0.0017) | 40 / 49 | +22 | -- | 1.008x |
| Fungus b20 | 6,000 | **-0.0015** | 10 / 1 | -9 | 0.958x | 0.978x |
| Fungus 2HG b20 | 6,000 | **-0.0015** | 11 / 2 | -9 | 0.970x | 1.007x |
| Fungus d3 | 3,000 | -0.0013 | 4 / 0 | -4 | 0.971x | 0.979x |
| Fungus d0 | 8,000 | +0.0005 (se 0.0004) | 2 / 6 | +4 | -- | 1.086x |

**Per deck: Goblins net -131, Fungus net -18.** (Morning round, 41x seeds: -71 / -49 with 8 honest losses.)

**The bar is MET.** All 60 slower `pay` games were replayed at `--depth 8 --budget-ms 0` with the lever on (one
pooled batch): **59 recover**. The one that does not -- Goblins 2HG s4422160 gi2159 (4 -> 5) -- is **CLEAR
clairvoyance**: the lever OFF reaches T4 at d8 b0, but at its own setting (b60) under `MTG_NC_SEARCH=1` it
too plays T5. **Honest losses: 0.**

Intermediate rounds (same method, superseded): 42x seeds after mechanisms 1, 2 and 4 -- Goblins -146, Fungus
-72, one honest loss (Fungus 2HG gi5014 -> mechanism 3); 43x seeds after 1-4 -- Goblins -105, Fungus -69, 159
slower, 156 recover, 2 clear clairvoyance, one honest (Goblins 2HG gi3205 -> mechanism 5).

### MTG_SUBSET_SAC_OUTLETS (ab133f122) on d0 -- the regression and its fix

Lever OFF, d0 (`--depth 0 --budget-ms 0 --ignore-play-profile`), fresh seeds Goblins 4550001 / Fungus 4560001,
8,000 games each, one batch per binary:

| | Goblins d0 | Fungus d0 |
|---|---|---|
| origin + outlets/devseq (land3 binary), outlets OFF | 4.0821 | 5.2690 |
| same, outlets ON | 4.1258 (+0.0436; 307 faster / 464 slower) | 5.2715 (+0.0025; 4 / 24) |
| wip/sacpay, outlets OFF | 4.0550 | 5.2645 |
| wip/sacpay, outlets ON | **4.0096** (-0.0454 vs OFF, 337 / 56; -0.0725 vs origin OFF, 477 / 81) | **5.2644** (-0.0001 vs OFF; -0.0046 vs origin OFF) |

Goblins smoke d0 seed 1001 (1000 games): origin 4.063 (outlets OFF) / 4.116 (ON); wip/sacpay 4.028 / 3.973.
The mechanism was 6 above: the gate was right to credit the outlet's float; the greedy priced the bodies a
`SacForMana` kills at zero, and the gate had been hiding those subsets from it.

### Open / USER decisions

* **Lever default** (`MTG_SAC_OUTLET_PAY`): the bar is met on fresh seeds and both decks are net-better, so the
  recommendation is ON -- the USER's call. Turning it on is a play change for Goblins and Fungus (the paused
  mulligan generations in the artifact queue would be affected).
* Ground truth: smoke/regression GT will move (mechanisms 4-6 move the OFF arm). Not accepted here.
* Goblins d0 cell: +22 net (se-level noise; it was +61 in the morning, +10 / +40 in the intermediate rounds);
  every one of its 49 slower games recovers at d8 b0, so it is the permitted d0 greedy's own choice. The
  remaining shape seen in the 43x round (the greedy casting Goblin King / Siege-Gang in main 1 by eating
  would-attack bodies, where main 2 would have kept the swing) is a main-1-vs-main-2 question for the greedy
  -- USER-owned, so not changed.


## STATUS 2026-10-30 (am, wip/sacpay): brought up to date, rebuilt as a COMPLETENESS fix. Still default OFF.

**Why it came back.** USER 2026-10-30: *"Shouldn't Skirk Prospector be counting Goblins like a mana source
now? That would mean that we are limited by the number of goblins and only spend as much as the line
requires."* The 2026-10-01 framing below ("a cost change, not a correctness fix") rested on the searched
count axis being right. It is not: Skirk reaches the search as landmark counts (none, one, the shortfall to
the costliest hand spell, and since `MTG_SAC_MANA_ACT_DEMAND` the activation landmarks), so a line needing
any other count -- a spell AND a Siege-Gang shot, two spells -- was unfindable at every budget
(gi2905 / gi3241 / gi3590 / gi37). The payer eats exactly what each cost needs, bounded by the bodies.

### What changed on the branch (all inert with the lever off: OFF arm byte-identical on 8 Goblins/Fungus cells)

1. **Two crashes the ON arm hit at once.** (a) `ManaPayment.cpp`: the creature-only-mana rank read
   `def->params` before testing `kind == 1`; a Goblin TOKEN offered as fodder has no definition -> SIGSEGV
   on the first ON game. (b) `AIEngine::CastSpellFromHand` held a `Card&` into the hand across the cast's
   payment; eating a Goblin under Rundvelt Hordemaster stages the impulse card in hand, reallocating it, and
   `LogCastSpell` read freed memory (`std::length_error` / `bad_alloc`, s3815127 gi5126). Now a pointer
   re-found by the copy's stable number after the payment -- the executor twin of the 09-18 `apply_one` fix.
   ASan sweep (991 games, lever on, both decks; re-run on the final code) clean afterwards.
2. **Human play keeps the explicit outlet** (`LiveSacPayOutlet` and the suppression both gate on
   `!HumanPlayActive()`): the viewer and every `validate_line` fixture click `sacout=` themselves.
3. **The searched action is kept where no body exists yet.** The 10-01 blocker
   (`fungus_sac_fodder_same_line_GAP`: pop a Saproling, eat it, cast Sporesower) was the suppression deleting
   the same-line fused action. A spore pop is a TRAILING activation, so its Saproling lands after the casts
   and no payment can reach it; with no body on the board there is no count axis to retire anyway. The
   suppression now fires only when `CanonicalSacVictim` finds a body. Unit-tested autonomously.
4. **Where a death PAYS, the board plays exactly as the OFF arm** (`SacOutletEatHasValue`, read by both
   `CollectActions` and `LiveSacPayOutlet`): Pashalik's ping, Rundvelt's impulse, Mogg War Marshal's token,
   persist, a sacrifice watcher, a watcher or self-death body in HAND (so the answer cannot flip mid-line).
   A line may sacrifice for the death alone, and only the searched action says that. Running the payer
   BESIDE it was tried and measured wrong twice over: (a) one body credited twice -- its searched float AND
   its pool fodder (Goblins d0 s3950036 gi35: a lone Skirk read as two mana, the prepay ate it, the cast
   failed); (b) under Rundvelt every eaten Goblin reveals a card inside a payment -- s3815127 gi5126 b60 took
   36 s / 3.9 GB against 11 s / 0.46 GB off.
5. **One set of bodies for mana AND sacrifice costs** (`PayFodderOverspent`, both enumerators): the flat
   test credited each Goblin as mana and again as Siege-Gang's victim (d0 gi215: a "lethal" four-shot burst
   ate seven Goblins for mana and fired once). Necessary condition `eaten + victims <= bodies` and
   `eaten <= bodies - activation sources`; not judged when the subset can add a body. (The first cut also
   counted the sources in the sum, which hid d0 gi1799's T3 kill whose last victim is a Siege-Gang.)
6. **An activation's payment never eats its own source or baked victim** (`PaySacSpareScope`, executor and
   rollout).
7. **The payer's order**: summoning-sick TOKENS first, then the shared `SacExpendabilityRank`; a body that
   would attack, or a creature CARD cast this turn, is deferred. (Pure `CanonicalSacVictim` order cost
   Fungus d0 +0.022t, 28 / 153; deferring just-cast cards fixed d0 gi2529's "cast Matron and Piledriver,
   then eat both for Chieftain".)
8. **The greedy (d0 runner + playout) charges the bodies a plan eats**: each body the subset's cost reaches,
   in the payer's order, at its board power in EvalCard's unit, doubled pre-combat for a would-attack body.
   Unbounded by landmarks, the greedy otherwise ate its board to cast its hand (d0 gi54: seven permanents to
   two, T4 -> T8). GoblinsProvider::PayEchoToKeep also declines a Stingscourger echo the lands cannot cover
   (the payer would eat Goblins to keep one; the echo axis still searches the other answer).

### Witnesses (d8 b0, `--ignore-play-profile`, binary from the worktree)

| witness | OFF | ON | ON + `MTG_SAC_MANA_ACT_DEMAND=0` | OFF + `ACT_DEMAND=0` |
|---|---|---|---|---|
| 2HG s3212906 gi2905 (`LACKEY_DECLINE=0`) | 3 | **3** | **3** | 4 |
| s3313242 gi3241 | 3 | **3** | **3** | 3 |
| s3313591 gi3590 | 3 | **3** | **3** | 3 |
| 2HG s3620038 gi37 | 3 | **3** | 4 | 4 |

gi37's kill is in a death-value state (item 4), where the board plays as the OFF arm, so it still needs the
activation landmark. **The landmark cannot be retired** while value states keep the searched axis -- the
`payna` arm below says the same on 8,000 games (Goblins -0.0029 against -0.0079 with it).

### The held-out A/B (ONE pooled batch, 432 jobs / 144,000 games -- 48,000 per arm --, arms pinned per job via `heurarm`)

Fresh seeds (Goblins 4110001 / 2HG 4120001 / Fungus 4130001 / Fungus 2HG 4140001 / d0 4150001, 4160001 / d3
4170001, 4180001), 12 threads, `MTG_PGO=0`, binary `wip/sacpay` with the lever pinned per arm. `base` = lever
OFF, `pay` = lever ON (activation landmark ON, the proposed setting), `payna` = lever ON with
`MTG_SAC_MANA_ACT_DEMAND=0`. Win turn LOWER is better; unwon = 9. Searched cells at their suite settings
(profile depth, b60 Goblins / b20 Fungus); d3 = `--depth 3 --budget-ms 10`; d0 = the greedy.

| cell | games | `pay` d(avg) | faster / slower | net | units | ms | `payna` d(avg) | f / s |
|---|---|---|---|---|---|---|---|---|
| Goblins b60 | 8,000 | **-0.0079** | 69 / 6 | -63 | 0.977x | 1.021x | -0.0029 | 70 / 47 |
| Goblins 2HG b60 | 6,000 | **-0.0078** | 55 / 8 | -47 | 0.983x | 1.064x | -0.0022 | 56 / 43 |
| Goblins d3 | 3,000 | **-0.0073** | 26 / 4 | -22 | 0.981x | 1.013x | -0.0017 | 26 / 21 |
| Goblins d0 | 8,000 | +0.0076 | 106 / 146 | +61 | -- | 0.942x | +0.0067 | 129 / 174 |
| Fungus b20 | 6,000 | -0.0002 | 7 / 6 | -1 | 0.968x | 0.985x | same | |
| Fungus 2HG b20 | 6,000 | -0.0008 | 12 / 7 | -5 | 0.960x | 0.968x | same | |
| Fungus d3 | 3,000 | +0.0003 | 3 / 4 | +1 | 0.979x | 0.987x | same | |
| Fungus d0 | 8,000 | **-0.0055** | 49 / 13 | -44 | -- | 1.056x | same | |

Per deck, all cells summed: **Goblins net -71, Fungus net -49.** Fungus has no death-value card, so
`payna` = `pay` there exactly. Goblins d0 is the one cell that still regresses (it was +0.131 before items
5, 7 and 8 above; the greedy is a crude policy and the rest of the gap is the same class: it sees bodies as
mana it can spend).

### Slower games at `--depth 8 --budget-ms 0` (the bar) -- NOT MET in the morning round (all eight fixed in the pm STATUS above)

All 194 slower `pay` games were replayed at d8 b0 with the lever on (one pooled batch): **173 recover.** Each of
the 21 that do not was replayed with the lever OFF at d8 b0 (all 21 reach the faster turn there) and with
the lever OFF at its own setting under `MTG_NC_SEARCH=1`:

| class | games |
|---|---|
| CLEAR clairvoyance (the OFF arm also misses the faster turn without seeing hidden information) | 13 -- Goblins 3, Goblins 2HG 3, Fungus 3, Fungus 2HG 3, Fungus d3 1 |
| **HONEST losses** (OFF finds the faster turn non-clairvoyantly; ON at d8 b0 does not) | **8** -- Fungus s4133781 gi3780, s4133848 gi3847, s4135551 gi5550 (4 -> 5); Fungus 2HG s4140744 gi743 (4 -> 5); Goblins s4113005 gi3004 (4 -> 5), s4113506 gi3505 (3 -> 4); Goblins d0 s4154256 gi4255, s4155580 gi5579 (5 -> 8 at d8 b0) |

**The Fungus class is LOCALIZED, not fixed.** Every Fungus honest loss inspected (also s4031532 gi1531 and the
2HG gi149 / gi4573 / gi5676 of the previous round) is the same line: T2 Utopia Mycon + Fungus Frolic (the
Brightcap Badger adventure), T3 Brightcap Badger twice off lands plus the Badger's GRANT tapping Mycon and the
Saprolings -- no sacrifice anywhere in it. ON plays T2 Badger as a creature instead. Bisected by throwaway
knobs on the d8 b0 replays: keeping the searched outlet action (payer + searched) -- no change; no greedy
charge -- no change; no fodder in BatchPrepay -- no change; no `PayFodderOverspent` -- no change; no fodder in
`BuildNonCreaturePool` or `AvailableManaPoolNoAttackers` -- no change; **no fodder credit in
`AvailableManaPool` -- three of the four 2HG losses recover** (but the completeness witnesses go back to T4,
so the credit itself is needed). So some consumer of the GENERAL pool, outside the subset-payability tests that
need the credit, misjudges that line once the Saprolings read as mana. Next step: credit fodder only in the
enumerators' own pools (SolveUncached / EnumeratePlans `pool`, the bound, the colour gates) and leave
`AvailableManaPool` fodder-free for every other reader; then re-run these eight.

The two Goblins searched losses and the two d0 games (T8 at d8 b0 where the OFF greedy wins T5) are not
root-caused.

### gi539 (Goblins s3510540 gi539 at `--budget-ms 60`, profile depth 7, alone on the box)

| arm | win | units | ms (two heavy jobs sharing the box) |
|---|---|---|---|
| lever OFF | T3 | 344,604 | 204,243 |
| lever ON | T3 (same digest) | 344,604 | 206,047 |
| lever OFF, `MTG_SAC_MANA_ACT_DEMAND=0` | T3 | 225,908 | 907 |
| lever ON, `MTG_SAC_MANA_ACT_DEMAND=0` | T3 | 225,908 | 910 |

The lever does not touch gi539: its board holds a death payoff, so it plays as the OFF arm (item 4). Its
wall is the activation landmark's unbilled continuation derivations -- the g2905 agent's billing work -- and
the lever neither causes nor cures it. (Before item 4, with the payer live there, it read 50 s / 195,955
units against 107 s / 344,604; the same payer-beside-the-axis shape is what made gi5126 36 s / 3.9 GB.)

### Other checks

* OFF arm byte-identical to the pre-branch binary on 8 cells (Goblins b60 / 2HG / d3 / d0, Fungus b20 / 2HG /
  d3 / d0); unit tests 2,175 / 2,175 (the six `MTG_SAC_MANA_ACT_DEMAND` cases now pin the lever off, since they
  test the searched axis); scenarios 339 / 340 OFF and ON (the one failure, `aa_weather_gw_galvanic_one_list_no_t4`,
  fails identically on the pre-branch binary).
* New unit file `test/unit/test_sac_outlet_pay.cpp` (8 cases): gi2905 with no activation landmark; the OFF
  control; the spare scope; three Bolts off three sacrifices (unfindable OFF); Krenko's tokens funding a
  Siege-Gang shot; the death-value keep; the payer-off states; the Fungus same-line fodder line autonomously.
* Fungus: a play change, so it matters for the mulligan generation paused in the artifact queue at the origin
  tip -- but only if the lever is turned on; default OFF moves nothing.

### Bodies created earlier in the plan -- what works and what does not

* Spore Saprolings (Fungus): the fused same-line action (item 3) when no body exists; otherwise the board's
  bodies, and a pop co-selected ahead of a cast is credited by `SacPayPlanFodderCount` in the bound. Works.
* Krenko's tokens: Krenko taps AFTER the main casts by design, so they fund the trailing activations (a
  Siege-Gang shot -- unit-tested) and the next phase, not a cast in the same section. Same as the OFF arm.
* Siege-Gang / Mogg War Marshal / Muxus / Lackey bodies: they exist for every LATER payment in the line at
  apply time (the payer sees the live board), but the enumerators' flat test counts plan-start bodies only, so
  a subset whose cost needs them is pruned before it is tried. The OFF arm could not express those lines
  either (its float happens before the casts). Crediting them is the follow-up, in the bound's direction
  (an over-count only keeps lines).
* Melira persist: Melira Pod holds no sac-for-mana outlet, so the lever never reaches it; the persist
  replenishment is read by `SacOutletEatHasValue` (a persist body makes the death pay -> OFF-arm behaviour).

### Recommendation (morning; superseded by the pm STATUS above)

**Not yet default ON.** Goblins and Fungus are each net-better summed over their cells (Goblins -71, Fungus
-49) and the searched cells are better-or-neutral with fewer units, but the bar is "every slower game
recovers at d8 b0 or is clear clairvoyance", and eight do not. Fix the Fungus general-pool consumer (above),
root-cause the four Goblins games, re-run this A/B, then propose ON. The activation landmark
(`MTG_SAC_MANA_ACT_DEMAND`) stays: death-value states keep the searched axis, and `payna` is worse.



---

# HISTORY (2026-09-18 .. 2026-10-01)

**Status (2026-10-01): MEASURED AT LAST, and the first measurement found a BUG rather than a verdict.
Still DEFAULT OFF, but the residual is now small, located, and named.** See §"WHY THIS WENT 13 DAYS
UNMEASURED" at the bottom for how it went unmeasured from 2026-09-18, and §"THE TOKEN-FODDER BUG" for
what the measurement actually turned up.

**A false start worth recording.** The first attempt at this A/B (`logs/victim_ab/pay_ab.json`, 72
jobs) **died one second after launch and produced zero games**, and I reported it as healthy and
saturating for the better part of an hour. Two causes, both avoidable:

* The manifest pinned `depth` on the **Goblins** jobs, and Goblins' `value_play` block LOCKS
  `target_depth=6`, which is a hard error (`MulliganProfile.h`: *"value_play depth is ENABLED for this
  deck ... omit --depth"*). One bad job aborts the WHOLE batch. The fix is to OMIT `depth`/`budget_ms`
  for a depth-locked deck and let the sidecar resolve them — `--ignore-play-profile` is the wrong
  escape here, because `AIEngine.cpp` deliberately disables the value_play levers off-policy, so it
  would have measured a configuration Goblins never ships.
* I read **loadavg** as evidence the run was busy. Loadavg is the HOST's, not the container's, so it
  showed 25 while our run was dead. The only instrument that answers "is MY batch busy" is the
  `[batch] heartbeat: N/M workers busy` line — and silence from it is itself the signal.

## THE RESULT (2026-10-01)

ONE pooled batch, 96 jobs / 69,600 games, three arms pinned PER JOB via `heurarm` so no arm inherits a
process default, 4 paired seed blocks per cell (same seeds across arms), seeds spaced by `games`.
`units` is the cost metric because units are DETERMINISTIC — the budget converts via
`NODES_PER_VIRTUAL_MS` and is never read off the clock — so the numbers are immune to the shared box.
Manifest `logs/victim_ab/pay3.json`, arms `base` / `pay` / `npf` (`pay` minus the plan-fodder credit).

| cell | games/arm | d(avg) BEFORE the bug fix | **d(avg) AFTER** | units | blocks better/worse |
|---|---|---|---|---|---|
| candidate-b d1/b3 *(the mulligan-generation cell)* | 2,000 | +0.0095 | **−0.0120** | **0.68888x** | 4 / 0 |
| candidate-b d3/b10 | 2,000 | +0.0110 | **−0.0130** | 0.84870x | 3 / 1 |
| candidate-b d5/b20 | 1,200 | +0.0075 | **−0.0175** | 0.85608x | 4 / 0 |
| Goblins (on-policy d6/b40) | 8,000 | −0.0014 | **−0.0250** | 0.98556x | 4 / 0 |
| shipped d3/b10 | 1,200 | +0.0216 | **−0.0000** | 0.95652x | 2 / 1 (1 tied) |
| shipped d5/b20 | 800 | +0.0388 | +0.0125 | 0.81942x | 0 / 2 (2 tied) |
| candidate-b d0 | 4,000 | +0.0232 | +0.0180 | n/a | 0 / 4 |
| shipped d0 | 4,000 | −0.0063 | +0.0177 | n/a | 0 / 4 |

So after the fix **every SEARCHED cell but `shipped d5` is neutral-or-better and 15–31% cheaper**, and
the generation cell is **0.689x**. The residual sits in two places, and they are different problems:

* **d0, both lists, 0-better/4-worse.** d0 is the greedy with no search at all, so the fodder RANK is
  fully exposed with nothing to recover a mis-ordering. Leading hypothesis, untested:
  `SacPayFodderCostsAttack` only asks whether a body would attack THIS turn, so eating a
  summoning-sick Saproling reads as free when it costs NEXT turn's attack. The searched arms do not
  care because the search sees the following turn; d0 cannot.
* **shipped d5 (+0.0125t, 0 better / 2 worse / 2 tied).** Halved from +0.0388 but not gone.

**The plan-fodder credit (`MTG_SAC_PAY_PLAN_FODDER`) measured near-INERT**: `npf` differs from `pay`
by ≤0.0015t and ≤1% units at every cell. It is kept default ON because it repairs a real, documented
blindness in the same class as SCALING SOURCE WIDENING, and it is very slightly positive at the three
candidate-b searched cells — but it is NOT what fixed this lever, and it should not be credited with
the table above.

## THE TOKEN-FODDER BUG — what the measurement actually found

The first numbers were "much cheaper and consistently worse", which is the signature of **deleting
work and the capability with it**. It was not the ranking and not the documented greedy/backtracker
split (the regression was present at d0, where the backtracker barely matters). It was this:

> **There is no `Saproling` entry in `cards.json`.** So every Saproling TOKEN has
> `Permanent::def_absent == true` and `LookupCached` returns `nullptr` — and the §2b payment path
> required a `CardDefinition` at three separate points. On Fungus essentially ALL the fodder is
> tokens, so the lever deleted the outlet's searched actions (the cost win) while the payment source
> meant to replace them **could almost never fire**.

The three points, all now fixed to judge the PERMANENT rather than demand a definition — exactly as
the SEARCHED victim pick already does (`q.card.IsCreature()` / `CardHasSubtype(q.card, ...)`, since
on-battlefield Cards carry their masks):

1. `ManaPayment.cpp` — `if (def == nullptr) { continue; }` skipped the body before fodder was ever
   considered. TAPPING needs a definition; EATING does not.
2. `ManaPayment.cpp` — `real_def != nullptr && fodder_ok(p, *real_def)` gated the fodder branch off.
3. `SpellEffects.h` — `SacPayFodderCount` opened with `if (p.def_absent) { continue; }`, so the
   payability BOUND under-counted too, which is what turned a payable cast into a "proof" of
   unpayability.

Reproduced to a single game before and after (shipped list, `--seed 620173 --game-index 173 --games 1
--depth 5 --budget-ms 20`): baseline wins T7, the lever did not win at all (T9), and after the fix it
wins T8. Per-game dumps on that cell went from 25 worse / 4 better / **2 games lost outright** to the
table above.

**BASELINE-SAFE, verified not assumed:** smoke was run before and after the fix and produced the SAME
7 changed configs with the SAME digests, so the repair is confined to the lever's ON arm. (It is
reachable only through a valid `SacPayOutlet`, and `LiveSacPayOutlet` returns none unless
`MTG_SAC_OUTLET_PAY` is on.)

**The transferable lesson is the one already in the tree:** *tokens have no `CardDefinition`, and
"bail on unknown" is how that becomes a silent capability hole.* It cost 1,900 of 5,106 sites once
before. Here it did not crash or log anything — it just made a lever look like a bad idea.

## WHY IT STILL SHIPS OFF

Not doubt about the cost prize, which is large and real. Two reasons:

1. The d0 and shipped-d5 regressions above are unresolved, and the USER's bar for THIS lever is their
   own: *"this is a cost change that we are aiming to not cost any quality."*
2. **It moves win turns, so it must be settled BEFORE any generation it will affect.** candidate-b has
   a 90.8-hour banked keepgen journal at 63.7%; a lever that changes play invalidates a keep table
   fitted to that play. This is the opposite case from `MTG_SAC_POOL_TURN_COLOR`, which is
   quality-neutral and so can be flipped after a generation lands.

USER, 2026-10-01, restating the original request and in effect re-asking for this:

> *"To be clear, Utopia Mycon mana should be able to produce black if needed."*
> *"But I'm not worried about presumptively holding black. We should be able to just do it on
> demand. (i.e. sacrifice a saproling or tap a land)"*

That is precisely what this lever does, and it is the reason it SUPERSEDES the colour-fan narrowing
in `sac-pool-colour-gate.md`: with `MTG_SAC_OUTLET_PAY` on there is no float colour to commit, hence
no colour fan, no singleton gate, and no speculative-black question at all — the payer makes black
when a cost needs black.

**The method that finally worked**, recorded because the first two attempts at this measurement both
failed for method reasons rather than engine reasons:

* ONE pooled batch, every arm pinned PER JOB via `heurarm`, so the process default is irrelevant and
  no per-arm wave is needed.
* Arms must be **ISOLATED**. The dead first manifest pinned `MTG_SAC_OUTLET_PAY` *and*
  `MTG_SAC_POOL_TURN_COLOR` together in its `pay` arm — a confounded arm that could not have told the
  two halves apart, which mattered because the plan was to revert one if the other adopted.
* **Pin every flag in every arm, including the ones you mean to be off.** The working tree had
  `MTG_SAC_POOL_TURN_COLOR` defaulted ON at the time, so an arm with `flags: {}` would have been
  silently tc-ON and not a baseline at all.
* A **CONTROL cell whose answer is known in advance**: Skirk Prospector pins `{R}`, so Goblins
  `base` and `tc` must be byte-identical. They were (0/4 digests, identical unit totals), which is
  what licenses the claim that the narrowing is correctly scoped to any-colour outlets.
* **Both lists**, because they disagree: the shipped list is mono-green and candidate-b splashes
  black, and the lever's sign differs between them at d5.

A `heurarm` slot rather than a bare env flag so both arms ride ONE pooled batch (an
`EnvOn` static can only ever BE one arm, which forces the per-arm wave CLAUDE.md forbids). The OFF
arm is byte-identical: smoke `configs changed: 0`, `play-changed=0`, scenarios 100/100.

Read §"AS BUILT" below before changing any of it — the implementation makes three choices the
proposal did not anticipate, and it uncovered a class of latent defect that is worth more than the
feature.

**Origin — user, 2026-09-17**, on Utopia Mycon and then generalising it:

> *"it's not clear to me whether we really need to search the sac ability for mana generation.
> Theoretically we could just treat it as a mana source with the highest level of deferral. i.e. It
> is only used if absolutely necessary and prioritizes saprolings with summoning sickness."*
>
> *"Honestly, it might make sense to do it this way for Skirk Prospector in Goblins as well. That
> is a very similar case."*
>
> *"So the idea would be to only spend the creatures when absolutely needed for our line."*

## The two cards are structurally identical

| card | deck | params |
|---|---|---|
| **Utopia Mycon** `{G}` | Fungus | `sac_creature_outlet`, `sac_creature_requires_subtype: Saproling`, `sac_outlet_add_mana_any_color`, `amount 1` |
| **Skirk Prospector** `{R}` | Goblins | `sac_creature_outlet`, `sac_creature_requires_subtype: Goblin`, `sac_outlet_add_mana_color: R`, `amount 1` |

Same shape; only the colour (any vs `{R}`) and the required subtype differ. Whatever is built here
serves both, and any future "sacrifice a creature: add mana" card for free.

## What the engine does today

They are emitted as **searched actions** in `CollectActions` (`src/ai/TurnSolver.cpp` ~15570-15730):
one action per activation count K, carrying `a.ritual_float = sac_outlet_add_mana_amount` and a
float colour, which `Solve` then credits. The *victim* is already heuristic, not searched — the
shared expendability ranking picks one canonical body, precisely because one-action-per-victim
"makes the O(2^candidates) subset search explode on a wide Goblin board / Krenko tokens and hangs".

So the search branches over **how many creatures to eat**, on a deck whose whole plan is making
dozens of fungible bodies. That is a branching axis paid on every enumeration.

There is already a note in the same function that these belong on the payment side —
*"Skirk's MANA outlet ... floats mana and needs the mana-solver float path (separate follow-up)"* —
so this direction is not new, just unfinished.

## The proposal

Move them to the **mana-payment** side, ranked last:

* `DecisionProvider::ManaSourceRank` already exists for exactly this class of choice — *"mana-source
  TAP ORDER: flexibility rank of a mana source (LOWER = tap earlier) ... a QUALITY heuristic for a
  sub-decision the search does NOT branch over (searching tap orderings is the very blowup we
  avoid)"*. Defaults run basic/bounce 10, dual 20, filter 25, tri 30, rainbow 50, `{C}`-manland 60.
* A sac outlet ranks **far above all of them** (say 500). The greedy payer takes the lowest-ranked
  qualifying source first, so the outlet is reached **only when nothing else can pay** — which is
  the user's "only spend the creatures when absolutely needed for our line", expressed structurally
  rather than as a heuristic anyone has to tune.
* **Victim preference: summoning-sick bodies first.** A creature that entered this turn cannot
  attack, so eating it costs nothing this turn. Then tokens, then the existing expendability order.

### What this buys

1. **A whole branching axis disappears** — directly relevant to the Fungus search-cost work
   (`fungus-token-search-cost.md`), and to the Goblin boards the current code already had to
   hand-bound.
2. **The semantics get simpler and more correct.** "Only if necessary" is what a last-ranked
   payment source *is*; as a searched action it is something the evaluation has to rediscover.

   **The sharp form of that (user):** *"what that means is that we don't take lines that sacrifice
   more creatures than we want to — since the search punishes us with a slower win turn."* A
   payment source eats **exactly as many as the cost requires, and only when no other source can
   pay**, so over-sacrifice becomes unrepresentable rather than merely discouraged.

   **BUT NOTE THE PARENTHETICAL, because it sets the bar for this whole change.** The search
   *already* rejects over-sacrificing lines: eating bodies slows the clock, and the objective IS
   avg win turn, so those lines score worse and lose. **So this is primarily a COST change, not a
   correctness fix.** The searched model arrives at the right answer; it just pays a branching
   axis on every enumeration of a deck that makes dozens of fungible bodies to get there.

   That flips the risk direction, and the adoption bar with it. If the search is already right,
   a heuristic replacing it can only match or lose on quality — so the bar is **quality-neutral,
   cost-better**, and any win-turn regression is disqualifying rather than something to trade
   against the speedup. Do not sell this as a correctness improvement.

   (The related guards `SubsetWastesCreatureSacMana` / `SubsetOversubscribesSacFodder`, and the
   sac-outlet enumeration divergence four sweep agents found independently, are about the
   enumeration being *wrong* — offering fodder that does not exist. That is a different failure
   from over-sacrifice, though this change would retire the axis both live on.)
3. **It may retire a heuristic.** `GoblinsProvider::DeferSacOutletPreCombat` haste-gates Skirk's
   mana outlet and defers value outlets to main 2 — a prune that exists partly because the
   action-side model offers the outlet in states where it is pointless. A payment-side outlet is
   never offered pointlessly.

## The hard part — and it is a real one

A payment source can only sacrifice a body that **already exists**. It cannot run the two-step line:

> activate a spore ability to CREATE a Saproling, then sacrifice that Saproling for the mana that
> casts the spell

The user flagged exactly this as the ordering complication:

> *"Ordering-wise, though, this does make things trickier as we need to consider dropping saprolings
> using abilities prior to doubling season if Utopia Mycon is in play."*

This collides with the deck's other ordering rule (`fungus-second-main-and-devour.md` §3c step 2):
**Doubling Season should be cast before any activation**, so the tokens it doubles are doubled. But
if the Season is castable *only* by eating a Saproling you must first make, the order has to invert
— and the Saproling made pre-Season is **not** doubled. That undoubled token is the price of
casting the Season a turn earlier, and whether it is worth paying is a genuine trade.

**The resolution that keeps both:** the *creation* step stays a searched action (spore activations
already are), and the *sacrifice* step becomes a payment source. A plan that activates and then
casts the Season simply has one more Saproling on the battlefield when the payer runs, and the
payer picks it up. Nothing needs to look ahead. The ordering rule in §3c then needs relaxing from
"Season strictly first" to "Season first **unless** an activation is required to pay for it".

### THE REQUIREMENT THAT MAKES OR BREAKS IT

**User:** *"We do need to count saprolings (or goblins) that are added to the battlefield during
the plan, though."*

This is the load-bearing constraint, not a detail. The payer's fodder count must be taken over the
board **as the plan leaves it**, including bodies created *earlier in the same plan* — spore
activations, a Tukatongue death refund, a Mycoloth upkeep, a Krenko/Siege-Gang token on the Goblin
side. A payer that reads only the plan-start battlefield will under-count available mana, and the
whole activate-then-sac line — the one the Doubling Season case is built on — silently disappears.
That failure is invisible in aggregate: the line does not error, it just never gets chosen, and the
deck merely looks a little slower.

The engine has already been bitten from the opposite direction here, which is a good sign the
awareness exists but the accounting is fiddly: `SubsetOversubscribesSacFodder` (added 2026-09-17)
has to **bail out entirely** when a co-selected action can add a matching creature, precisely
because plan-added fodder makes a static count wrong. A first cut of that guard broke Melira Pod,
because **persist returns a sacrificed body** — one creature legitimately feeding many activations.
Whatever counts fodder for the payer has to handle the same replenishment cases, and Melira Pod is
the regression test that catches it.

Implementation note: this argues for the fodder count being derived from the same post-apply state
the plan's other effects are, rather than a separate pre-pass — a pre-pass is what both of the
above defects were.

## Risks and how to measure

* **It is a behaviour change, not a refactor.** Both Goblins and Fungus play digests will move; GT
  must be re-accepted after inspection, not regenerated. Goblins is in the regression suite, so it
  is the real gate.
* **Risk: under-use.** A last-ranked source is never tapped for *value* — e.g. eating a soon-to-be-
  devoured Saproling for mana you did not strictly need. Against a passive opponent that is
  probably right, but it is the thing to watch in the A/B.
* **Risk: the float is coarser.** Today's action floats mana that a plan can spread across several
  casts; a payment source is pulled per cost. Likely equivalent, but worth a digest diff rather
  than an argument.
* Measure per `heuristic-optimization.md`: train seeds, held-out validation, report before adopting.
  The headline numbers are avg win turn (must not regress) **and** enumeration/branching cost, which
  is the reason to do it at all.

---

# AS BUILT (2026-09-18) — "§2b"

Named §2b because it is the second instalment of §2a (`TreasurePaySourceEnabled`, "one-shot lump
sac sources as PAYMENT sources"), and shares its machinery and its contract.

## The structural difference from §2a, and what falls out of it

A Treasure **is** the source: it taps, it dies, one permanent. A repeatable outlet is a permanent
that eats OTHER permanents. So:

> **The payment source is each piece of legal FODDER, priced at the outlet's yield. The outlet is
> only the permission.**

N Saprolings under a Utopia Mycon are N one-mana rainbow sources; N Goblins under a Skirk
Prospector are N `{R}` sources. That single reframing is what let this reuse the whole §2a path
instead of inventing a repeatable-activation concept inside the mana solver.

## Where it plugs in

| piece | what |
|---|---|
| `SacOutletPayEnabled()` | the lever (`SpellEffects.h`) |
| `LiveSacPayOutlet(state, controller)` | the board's outlet, resolved **once per payment** |
| `IsSacPayFodder(p, def, outlet)` | subtype filter + "Sacrifice ANOTHER" |
| `SacPayOutletColors(params)` | pinned `{R}` vs "any color" — same shape as `EffectiveProduces` |
| `SacPayFodderRank(...)` | **base 500** — behind every real source (defaults top out at 62) |
| `Permanent::pay_sac_eaten` | the in-flight mark; `CommitPaySacSacrifices` does the real sacrifice |
| `GameState::deck_has_sac_mana_outlet` | per-game presence gate, stamped in `StampDeckTraits` |
| kind 5 in `TapForCostSharedOnce` | the payer branch that eats |
| `CollectActions` | **suppresses** the mana outlet's searched actions — this is the cost win |

The four supply scans that must agree with the payer or they prune payable lines — `AvailableManaPool`,
`AvailableManaPoolNoAttackers`, `BuildNonCreaturePool`, `BuildColorFeasibility`, `ComputeAvailableColors`
— all credit fodder, and so does `UntappedManaUpperBound`. That last one is **load-bearing, not
bookkeeping**: `PaymentManaCovers` turns a short bound into a *proof* of unpayability and refuses
the payment before the greedy runs, so a bound blind to fodder would reject exactly the casts this
lever exists to enable.

## Three choices the proposal did not anticipate

**1. TAPPED bodies are fodder.** Sacrificing is not tapping, and *attack, then eat the attackers
post-combat for mana* is the Skirk line the Goblin deck is built on. The payer's source loop
short-circuits on `p.tapped` — that had to become `p.tapped && !sac_outlet.valid()`, which keeps
the historical short-circuit (including the `LookupCached`) when the lever is off. Within fodder,
`SacPayFodderCostsAttack` then defers a body that still *has* an attack to give, which is the
engine-side expression of the user's "prioritizes saprolings with summoning sickness".

**2. GREEDY PATH ONLY.** The backtracker memoises payments in `g_mana_cache` as a set of tapped
battlefield ordinals, and an eaten body is not a tap — a cached entry could not replay it. Fodder
ranks last, so the greedy reaches it whenever it is the answer; a cost only the backtracker could
assemble simply does not see fodder. Pessimistic, i.e. the safe direction: a payable cast fails
rather than an unpayable one resolving. (The legacy `MTG_TAP_LEGACY` 4-step path is likewise not
covered; it is an A/B baseline, not a ship path.)

**3. The commit is a REAL sacrifice.** Eaten fodder goes through `SacrificePermanentAt` —
graveyard, sacrifice watchers, death triggers — not the bare erase a Treasure gets, because
Pashalik Mons pings on a Goblin death and Rundvelt Hordemaster impulse-exiles, and the searched
`SacForMana` action this replaces fired both. That is correct, and it is also what made the whole
thing explode; see below.

## THE REAL FIND: a payment that sacrifices a creature MUTATES ZONES

The engine's payment layer rests on an unwritten invariant — **a mana payment does not change the
zones** — and essentially every caller relies on it. Making a payment sacrifice a creature breaks
that invariant comprehensively: the body moves to a graveyard, watchers fire, tokens are created,
cards are pushed into hand.

Turning the lever on segfaulted the batch. `perf`/gdb only showed the *symptom* (a corrupt free
inside `PoolAllocator`), so this was settled with an **ASan build** (a deliberate separate cmake
route, `build/Asan`, run with `MTG_POOL_ALLOC=0` so the custom allocator does not hide the poison).
ASan named the site in one run. Three defects, **two of them pre-existing and latent under §2a**:

| site | defect |
|---|---|
| `TurnSolver.cpp` `apply_one` | held a **hand iterator** across the cast's own payment. Eating a Goblin fired Rundvelt Hordemaster's dies-impulse, which `push_back`s into that same hand and reallocates it — so `it->m_is_staged` and `zone.erase(it)` ran on freed memory. Fixed by re-finding the copy by its stable per-copy `m_number`. |
| `AnimateLandsShared` | range-`for` over `state.battlefield` writing `p.is_animated` **after** a payment that can erase. Pre-existing: a cracked §2a Treasure does it too. Fixed to index + re-find by number. |
| `ActivateTapTokensShared` | cached `bf_size` across a payment that can shrink the battlefield — an out-of-bounds write on the `tapped = false` rollback. Pre-existing, same cause. Fixed to a live bound. |

And one in the new code: `CommitPaySacSacrifices` used a `static thread_local` scratch vector, which
a death trigger reaching another payment re-enters and clears underneath the loop walking it. Now a
plain local.

**The standing lesson, which outlives this lever:** a mana payment is not side-effect-free, and the
set of callers holding a zone iterator, a battlefield reference or a cached index across one is not
knowable by reading. ASan is the tool; guessing is not. The §2a note *"No index survives a
successful payment"* states the contract correctly — it was simply not being honoured.

## the plan-added fodder credit (stage 2) — NOW BUILT (2026-10-01), and it measured near-INERT

**Implemented** as `SacPayPlanFodderCount` / `MTG_SAC_PAY_PLAN_FODDER` (default ON): for each
permanent with a spore→token ability (`spore_saproling_cost` / `spore_creates_tokens`) whose token
subtype is legal fodder for the outlet, credit `floor(counters / cost) * per` bodies into §2b's term
of `UntappedManaUpperBound`, and into the `have[]` colour gate in `TurnSolver.cpp`. Sound for the
documented reason these bounds state about themselves — they may only ever OVER-count, so crediting a
body the line has not made yet can only KEEP a line, never invent one; a plan that cannot really
assemble the mana fails at the real payment, which no-ops it.

**But it was NOT the fix.** Measured against an otherwise identical arm (`npf`), it moves ≤0.0015t and
≤1% units at every cell, and on the reproducing game it changed nothing on its own. The actual defect
was the token-fodder bug above. Keep this section's distinction in mind when reading the history: the
hole described below was real, and it was also **not what was costing the quality**. The honest
ordering is *fix the capability first, then the bound* — a bound crediting fodder the payer then
refuses to eat buys nothing.

The user's load-bearing requirement — *"We do need to count saprolings (or goblins) that are added
to the battlefield during the plan"* — was, until this change, **not implemented**:

At APPLY time it happens to work: the plan applies sequentially, so a spore activation earlier in
the plan has already put its Saproling on the board when the payer runs. The hole is at
**ENUMERATION** time: every supply scan above reads the plan-start battlefield, so a subset whose
mana comes from a body the same subset creates scores as unpayable and is never offered. That is
exactly the Doubling-Season-off-an-activated-Saproling line §3c of
`fungus-second-main-and-devour.md` is built on.

The failure is invisible in aggregate — the line does not error, it simply is never chosen, and the
deck merely looks a little slower. If the A/B comes back quality-negative on Fungus, **this is the
first thing to suspect**, not the ranking. `SubsetOversubscribesSacFodder` already bails out
entirely when a co-selected action can add a matching creature, and Melira Pod (where persist
*returns* a sacrificed body) is the regression case that catches a naive static count.

---

# WHY THIS WENT 13 DAYS UNMEASURED (recorded 2026-10-01, at the user's question)

Worth writing down because the failure is procedural, not technical, and it is repeatable.

**There was never a technical blocker.** The lever has a `heurarm` slot, so it pools cleanly — which
is why its first real A/B could be launched as 72 jobs in a single batch. (Contrast
`MTG_FORCE_USES_M2`, which genuinely *could not* be measured in a pooled batch because
`GoldFishRunner.cpp` read it as a process-wide `static const bool`. That one had an excuse.)

**The A/B was planned and then skipped, inside three minutes.** Timestamps:

| when | what |
|---|---|
| 09-18 00:30 | `logs/sacpay_ab/manifest.json` written |
| 09-18 01:48 | the lever commits (`d81195f7`) — scenarios 100/100, smoke byte-identical OFF, ASan clean ON over 250 Goblins + 100 Fungus games |
| 09-18 01:51 | `logs/sacpay_ab/holdout.json` written |
| 09-18 02:05 | next commit is condemnation perf — the session had moved on |

Both of those files are **manifests only**: a `jobs` array of 16, and no `avg`, no `digest`, no
`played=` anywhere in them. Verified again 2026-10-01, they are still results-free. So the careful
work went into *correctness* (and found three real zone-mutation bugs, two of them live under §2a)
while the cost/quality question the lever exists to answer was never asked.

**Then it was carried as "believed measured."** The existence of `holdout.json` was mistaken for its
results. Caught 2026-09-18 11:08 while sequencing the Fungus value-leaf freeze, and recorded as the
memory *"open the artifact before believing a measurement exists."*

**And then the parking was legitimate, which is why nobody came back.** The freeze rule is that every
open play lever must be *settled* first, and *"this ships OFF for now"* is a complete way to settle
one. So it was settled OFF, the freeze proceeded correctly, and the measurement simply stayed
undone. It surfaced once more on 2026-09-24 — but only to verify it does **not** fix the same-line
fodder hole (`sac-fodder-created-in-the-same-line.md` §"root cause"), a narrow correctness check
rather than the A/B.

## The gap this exposes, which is the transferable part

**A lever parked as "ships OFF pending measurement" is indistinguishable in the tree from one that
was measured and REJECTED.** Both read as a default-OFF flag with a long justifying comment at the
read site. Nothing in the comment, the flag registry or the doc header separated the two, so every
later reader — including three of my own sessions — saw a settled decision where there was an unpaid
debt.

**So: a read-site comment for a default-OFF lever must say WHICH of the two it is.** "Default OFF,
UNMEASURED — A/B pending" and "default OFF, measured and rejected (evidence: ...)" are different
facts and must not share a phrasing. `MTG_FUNGUS_SAC_DRAW_CLOCK` was on the same 2026-09-18 list
("built and measured, adoption not yet decided") and is worth auditing for the same ambiguity.

# DEFAULT ON (2026-10-31) — the Goblins stack lands

USER 2026-10-31: *"2 we should turn on."* `MTG_SAC_OUTLET_PAY` is default ON together with `MTG_SUBSET_SAC_OUTLETS`,
`MTG_BP_DEV_SEQ` and `MTG_SAC_MANA_ACT_DEMAND`. With the payer on, fodder is the payment's LAST source, eaten
minimally, per order; the searched SacForMana action stays only where the death has value (Pashalik Mons, Mogg War
Marshal, Rundvelt boards), with the payer off there.

Measured, the whole stack on vs off (shipped settings, `MTG_PGO=0`):

| sample | games | slower | faster | net turns | units |
|---|---:|---:|---:|---:|---:|
| 41 suite cases, seed 1580001 | 18117 | 6 | 185 | -179 | 1.074 |
| Alchemist, seed 1600001 | 1500 | 3 | 25 | -21 | 0.984 |
| Alchemist 2HG, seed 1600001 | 500 | 2 | 9 | -6 (unwon = max_turns+1) | 0.984 |
| Midrange Burn, seed 1600001 | 1000 | 0 | 2 | -2 | 1.049 |

Every slower game recovers at `--depth 8 --budget-ms 0 --ignore-play-profile` (the digest is equal on both arms),
except Goblins 2HG gi794, which is T7 on both arms under `MTG_NC_SEARCH=1`: clear clairvoyance. Alchemist gi267 goes
from a T7 win to unwon at the shipped budget with the stack on; at d8 b0 it is T7 on both arms (budget churn).

`MTG_BP_DEV_SEQ`'s share, devseq on vs off with the payer on: 8 faster, 1 slower (-7 net, 6 of them Goblins out of
4000) at x1.077 units. Landfall at 3000 games: 2 faster, 1 slower (gi378, T5 on both arms at d8 b0). The cost is
uneven: Melira x1.27 with no game moved, Goblins x1.15, Goblins 2HG x1.13, Landfall x1.06, Minotaur x1.05. USER
2026-10-31: devseq "could be okay if it improves quality notably and only costs a little more", so it stays ON and the
wasted share (Melira first) is being folded out losslessly on `wip/devseq-prune`.

The same accept also takes TH and Niv 2HG regression keys that `MTG_RAMP_FILTER_C_FEED` (df33bd631) moved and its
scoped `--deck=niv` accept missed: held-out seed 1610001, TH 1500 games 0 slower / 0 faster (7 moved), Niv 2HG 300
games 0 slower / 1 faster.
