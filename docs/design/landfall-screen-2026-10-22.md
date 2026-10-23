# Landfall card screen (2026-10-22): Mossborn Hydra, Kessig Wolf Run, threats and lands

**Status:** in progress. Rounds 0 (cards the engine already had) and 1 (Mossborn Hydra, Kessig Wolf Run,
mana creatures) are the first two; a non-clairvoyant bracket follows. Nothing is adopted: a list change is
the USER's call, and an adopted list still owes its own keep table and value leaf
(`.claude/skills/deck-screening.md`, "When a combination graduates").

## The ask (USER, 2026-10-22)

*"Let's ... work on screening card changes for the landfall agent deck. I recommend trying Mossborn Hydra
and other cards that might help. Note that Terastodon or a similar effect should remain in the list. If we
need more threats I suppose we could add more Baloths, Omnath or Genesis Wave, but I feel like this will
probably be unnecessary after adding the Hydras. Feel free to try lands as well, but we have to be careful
of shuffle effects. When adding those we need to keep in mind that clairvoyance loves shuffles so they will
be overvalued. We might want to use the non-clairvoyant engine for sanity check."* Then: *"we should also try
Kessig Wolf Run. My suspicion is that it is a clear 1-of (since it can be fetched by Primeval Titan. We
should add that to the heuristic."* and *"It's a very good option if you don't have other threats."*

Earlier screening input (ledger, 2026-10-16): the deck stalled in some hand-played games and its owner
reported running out of threats, so the T7+ / unwon tail is reported beside the average.

## Apparatus

- `scripts/deck_compare.py`, one pooled `mtg --batch` per round, PGO+LTO binary, the deck's shipped play
  profile + value leaf (d5 / b20, the emulated ladder), 20,000 paired games per arm (inherited numbering),
  `max_turns` 8 (unwon scored 9).
- **Keep table: the shipped one, never regenerated.** Every card in it has its own bucket (K = 18), so an
  introduced card rides the bucket of the card it replaces (the approved alias route). A screen that tries
  one new card in several slots needs that host PER ARM, so the driver gained `"arm_alias"` (commit
  `8474e7bca`): each aliased arm gets its own copy of the table with the new name in its host's bucket.
  The base arm keeps the shipped table. Consequence, stated plainly: the mulligan treats the newcomer as
  the card it replaced, so these numbers measure the in-play difference, not a different keep policy.
- Raising a 1-of to a 2-of (Omnath) or a 2-of past 2 makes some hands untabled; worst arm 1.19% of hands
  (~0.0008 t of one-sided bias), under the spec's 2% limit.
- The bias floor is UNMEASURED (`--with-floor` generates tables, which this route forbids). Expect
  single-digit margins to be needed; treat sub-0.01 t effects as unresolved.
- **Clairvoyance.** The search sees post-shuffle library order. This deck shuffles constantly (Evolving
  Wilds, Terramorphic, Myriad, Rampant Growth, Search for Tomorrow, Cultivate, Khalni, Primeval Titan), and
  Courser of Kruphix turns each shuffle into a re-rolled top card. So EVERY arm that changes the count of a
  shuffler or of Courser carries the bias, in either direction. Each round is re-run with
  `MTG_NO_SEARCH_SHUFFLE=1` (the screening skill's minimum non-clairvoyant bracket); the user's
  non-clairvoyant engine (`MTG_NC_SEARCH=1`) costs ~4.5 min and ~1.8 GB per game here, so it is reserved for
  a small sanity sample on the finalists.

## New cards implemented for the screen

- **Mossborn Hydra** (`87965f62d`..`e12d274c6`): generic params `enters_with_plus_counters` (an as-enters
  replacement, CR 614.1c, doubler-aware, applied in pass 1 of every simultaneous multi-put) and
  `landfall_double_self_counters` (CR 701.10e via a shared `DoublePlusCounters`). It is a landfall watcher,
  so it sits in the USER's cast-order tier 1 automatically (rank 130, tied with Courser, after it by name).
  5i discard: PROVISIONAL threat slot right after Rampaging Baloths.
- **Kessig Wolf Run** (`f1d4c8642`, `86cc74a47`): `PermAbilityMode::TapTargetPump` (`tap_target_pump_*`);
  (target, X) searched as plan variants; Landfall folds (single largest attacker; X = all remaining mana
  with the pump last in main 1) are sound dominance folds, opened by `MTG_UNPRUNE=xspell`. Titan rule:
  while a Wolf Run is in the library and none is on our battlefield, Titan's searched picks also include
  "Wolf Run + a land the USER's rule would pick", FIRST when we have no other threat, after the rule's own
  picks otherwise (interpretation, pending the USER). Karoo bounce: last among the colourless lands.
- Both are byte-identical on every deck that does not hold them (smoke 140/140, 0 play-changed, each branch
  separately; the combined tree re-checked before round 1).

## Known modelling gap that keeps a candidate out

**Fetchlands (Wooded Foothills and friends) are not screenable here yet.** `PlayLandByName` resolves a
fetchland atomically, hand -> graveyard -> fetched land (`docs/design/uncracked-fetchland.md`), so it never
enters the battlefield and gives ONE landfall where the real card gives two. In this deck that would make
Foothills read worse than Evolving Wilds, when it is really better (untapped fetch, can find Stomping
Ground). Excluded until fetchlands are real permanents.

## Round 0 — cards the engine already had (`logs/landfall_screen/r0.json`, seed 7300000)

20,000 paired games per arm. Base 5.7311 (T7+ 6.99%, T8+ 0.70%, unwon 0.14%). Negative = faster.

| arm | change | delta | se | T7+ d | T8+ d |
|---|---|---|---|---|---|
| turntimber2 | 2 Forest -> 2 Turntimber Symbiosis | **-0.0120** | 0.0014 | -0.68% | -0.07% |
| hoof1_c1 | 1 Courser -> 1 Craterhoof Behemoth | **-0.0103** | 0.0012 | -0.27% | +0.02% |
| wave3_c1 | 1 Courser -> 3rd Genesis Wave | **-0.0086** | 0.0014 | -0.17% | -0.03% |
| turntimber1 | 1 Forest -> 1 Turntimber Symbiosis | -0.0064 | 0.0010 | -0.43% | -0.05% |
| c0_bal3_wave3 | 2 Courser -> 3rd Baloths + 3rd Wave | -0.0043 | 0.0019 | -0.05% | +0.06% |
| stomp1 | 1 Mountain -> Stomping Ground | +0.0006 | 0.0013 | +0.14% | -0.04% |
| stomp_f1 | 1 Forest -> Stomping Ground | +0.0008 | 0.0007 | -0.06% | -0.01% |
| bal3_c1 | 1 Courser -> 3rd Baloths | +0.0027 | 0.0013 | +0.08% | +0.01% |
| omn2_c1 | 1 Courser -> 2nd Omnath | +0.0038 | 0.0013 | +0.29% | +0.01% |
| pvigor1_c1 | 1 Courser -> Primal Vigor | +0.0053 | 0.0009 | +0.45% | +0.10% |
| bal4_c0 | 2 Courser -> 2 more Baloths | +0.0081 | 0.0019 | +0.29% | +0.07% |
| khalni2_bal4 | 2 Khalni -> 2 more Baloths | **+0.0816** | 0.0029 | +2.35% | +0.28% |
| cult0_bal4 | 2 Cultivate -> 2 more Baloths | **+0.0946** | 0.0030 | +3.33% | +0.51% |

Reading (clairvoyant engine; the bracket is still owed):
- **The ramp is load-bearing.** Trading two ramp spells for two more Baloths costs ~0.08-0.09 turns and
  raises the T7+ tail by 2-3 points. More of the same threats is not what the deck lacks.
- **Courser is worth about a threat.** Swapping one Courser for a third Baloths / second Omnath is neutral to
  slightly worse; for Craterhoof or a third Genesis Wave slightly better (~0.01 t, the scale where the
  unmeasured floor matters).
- **Turntimber Symbiosis's gain is suspect.** Its front face LOOKS at the top seven cards, a hidden-library
  read the clairvoyant search already knows the answer to, and as a land it replaces a basic Forest that the
  fetch rule can find. Needs the bracket before it is believed.
- Stomping Ground is neutral (one more red source the deck rarely needs). Primal Vigor is worse.

## Round 1 — Mossborn Hydra, Kessig Wolf Run, mana creatures (`logs/landfall_screen/r1.json`, seed 7400000)

20,000 paired games per arm, pre-rebase engine (`ae1bb771c`'s code without the 11 commits pulled at push
time; `cards.json` identical). Base 5.7321 (T7+ 6.84%, T8+ 0.69%, unwon 0.13%). Negative = faster.

| arm | change | delta | se | identical | T7+ d | T8+ d | unwon d |
|---|---|---|---|---|---|---|---|
| h4_phy0 | 4 Phylath -> 4 Hydra | **-0.6989** | 0.0047 | 40.4% | -3.59% | -0.30% | -0.07% |
| h4_k0_elf4_rg0 | 4 Khalni -> 4 Hydra, 4 Rampant Growth -> 4 Llanowar Elves | -0.6791 | 0.0052 | 41.8% | -4.18% | -0.35% | -0.05% |
| h4_k0 | 4 Khalni -> 4 Hydra | -0.6009 | 0.0049 | 49.8% | -4.12% | -0.37% | -0.04% |
| h4_rg0 | 4 Rampant Growth -> 4 Hydra | -0.5841 | 0.0051 | 48.4% | -2.80% | **+0.07%** | **+0.14%** |
| h4_c0_cult0 | 2 Courser + 2 Cultivate -> 4 Hydra | -0.5327 | 0.0055 | 53.1% | -2.54% | -0.20% | -0.04% |
| h4_sft0 | 4 Search for Tomorrow -> 4 Hydra | -0.5206 | 0.0053 | 50.8% | -2.10% | **+0.47%** | **+0.30%** |
| h2_cult0 | 2 Cultivate -> 2 Hydra | -0.4051 | 0.0046 | 65.7% | -2.10% | -0.15% | -0.03% |
| h2_k2 | 2 Khalni -> 2 Hydra | -0.3952 | 0.0045 | 67.8% | -2.80% | -0.29% | -0.04% |
| h2_wave0 | 2 Genesis Wave -> 2 Hydra | -0.3609 | 0.0046 | 71.6% | -2.40% | -0.14% | -0.06% |
| h2_bal0 | 2 Baloths -> 2 Hydra | -0.3582 | 0.0045 | 72.7% | -2.59% | -0.19% | -0.04% |
| h2_c0_wr | 2 Courser -> 2 Hydra, Fengraf -> Wolf Run | -0.3510 | 0.0045 | 72.3% | -3.06% | -0.34% | -0.07% |
| h2_c0 | 2 Courser -> 2 Hydra | -0.3439 | 0.0044 | 73.3% | -2.65% | -0.27% | -0.06% |
| elf4_k0 | 4 Khalni -> 4 Llanowar Elves | -0.1137 | 0.0032 | 79.1% | -1.60% | -0.25% | -0.07% |
| elf4_rg0 | 4 Rampant Growth -> 4 Llanowar Elves | -0.1048 | 0.0032 | 79.9% | -0.60% | +0.07% | +0.06% |
| bop4_rg0 | 4 Rampant Growth -> 4 Birds of Paradise | -0.0877 | 0.0032 | 80.3% | -0.06% | +0.16% | +0.04% |
| wr_font | Radiant Fountain -> Kessig Wolf Run | -0.0145 | 0.0010 | 98.0% | -0.97% | -0.15% | -0.01% |
| wr_feng | Haunted Fengraf -> Kessig Wolf Run | -0.0138 | 0.0010 | 98.0% | -0.87% | -0.14% | -0.01% |
| wr_mtn | 1 Mountain -> Kessig Wolf Run | -0.0043 | 0.0015 | 95.6% | -0.63% | -0.11% | -0.01% |
| wr_forest | 1 Forest -> Kessig Wolf Run | +0.0078 | 0.0017 | 96.1% | -0.22% | +0.14% | +0.13% |

Reading:
- **Mossborn Hydra is the screen's headline: ~0.35 t per pair, ~0.5-0.7 t as a 4-of**, an order of magnitude
  past any floor this apparatus could carry, and it roughly HALVES the T7+ tail (6.8% -> 2.7-3.3%) -- the
  threat problem the deck's owner reported. A spot-checked game (`h4_phy0`, seed 7400003) shows the mechanism
  as printed: T3 Hydra (1), T4 suspended Search + land drop (4) and a Genesis Wave, T5 Cultivate's battlefield
  land + the drop -> 16 counters, an exactly-lethal swing.
- **The cut matters.** Phylath (a 6-drop) is the best slot measured; cutting ramp that puts LANDS (Rampant
  Growth, Search for Tomorrow) gains less and RAISES the T8+ / unwon tail -- those spells are Hydra fuel.
- **Kessig Wolf Run is a real 1-of over a colourless utility land** (-0.014 t, T7+ -0.9 points), as the USER
  expected; over a Mountain it is ~neutral, over a Forest slightly worse (the green source matters more).
- **Mana creatures help (-0.09 to -0.11 t)** but far less than the Hydra.
- **Clairvoyance direction.** Every Hydra arm cuts a shuffler or a Phylath, so the arm holds FEWER shuffles
  than base; if the clairvoyant search over-values shuffles, these deltas are, if anything, understated.
  The bracket below tests that.

## Round 2 — around four Hydra (`logs/landfall_screen/r2.json`, seed 7600000)

**From here every round runs on ONE pinned engine** (`logs/landfall_screen/run_pinned.sh`: a copy of the PGO
build of src tree `ec2de514`, i.e. this branch rebased onto origin at 21:20 UTC, smoke 144/144 byte-identical)
via `MTG_BIN` / `MTG_ANALYZE_BIN` + `MTG_PGO=0` -- another session kept pushing engine changes, and each pull
would otherwise have moved the engine between rounds.

20,000 paired games per arm. Base 5.7295 (T7+ 6.69%, T8+ 0.70%, unwon 0.21%). All arms play 4 Hydra.

| arm | Hydra replaces | also | delta | vs h4_phy0 | T7+ | T8+ |
|---|---|---|---|---|---|---|
| h4p_wr_elf4_rg0 | 4 Phylath | Elves for Rampant Growth, Wolf Run for Fengraf | **-0.7907** | -0.082 | 2.50% | 0.40% |
| h4p_elf4_rg0 | 4 Phylath | 4 Llanowar Elves for 4 Rampant Growth | -0.7798 | -0.071 | 3.04% | 0.48% |
| h4p_elf4_k0 | 4 Phylath | 4 Llanowar Elves for 4 Khalni | -0.7773 | -0.069 | 2.46% | 0.26% |
| h4_phy2_c0 | 2 Phylath + 2 Courser | | -0.7271 | -0.018 | 2.33% | 0.31% |
| h4_phy2_bal0 | 2 Phylath + 2 Baloths | | -0.7252 | -0.016 | 2.49% | 0.32% |
| h4_phy2_tit2 | 2 Phylath + 2 Titan | | -0.7232 | -0.014 | 2.30% | 0.30% |
| h4p_tt2 | 4 Phylath | 2 Turntimber for 2 Forest | -0.7197 | -0.011 | 2.74% | 0.29% |
| h4_phy2_ava2 | 2 Phylath + 2 Avenger | | -0.7188 | -0.010 | 2.71% | 0.36% |
| h4p_wave3_c1 | 4 Phylath | 3rd Wave for 1 Courser | -0.7186 | -0.010 | 2.79% | 0.32% |
| h4p_wr_feng | 4 Phylath | Wolf Run for Fengraf | -0.7173 | -0.0085 | 2.58% | 0.27% |
| h4p_wr_font | 4 Phylath | Wolf Run for Fountain | -0.7166 | -0.0077 | 2.60% | 0.29% |
| h4p_hoof_c1 | 4 Phylath | Craterhoof for 1 Courser | -0.7137 | -0.005 | 2.93% | 0.34% |
| h4_phy0 | 4 Phylath | | -0.7088 | -- | 3.17% | 0.36% |
| h4_phy2_wave0 | 2 Phylath + 2 Wave | | -0.7004 | +0.008 | 2.91% | 0.30% |
| h4_phy2_k2 | 2 Phylath + 2 Khalni | | -0.6546 | +0.054 | 3.06% | 0.40% |
| h4_wave0_bal0 | 2 Wave + 2 Baloths | | -0.6236 | +0.085 | 3.02% | 0.40% |
| h4_c0_bal0 | 2 Courser + 2 Baloths | | -0.5880 | +0.121 | 2.79% | 0.36% |
| h4_c0_wave0 | 2 Courser + 2 Wave | | -0.5646 | +0.144 | 3.40% | 0.43% |

Reading:
- **Phylath is the slot**: every arm that keeps all four Phylath is 0.09-0.14 t worse. Cutting two Phylath
  plus two Courser / Baloths / Titan / Avenger is ~0.01-0.02 t better than all four Phylath (a near tie among
  those four); Khalni is Hydra fuel and should stay (cutting two costs 0.054 t).
- **Llanowar Elves are a real second upgrade with Hydra (-0.07 t)**: T1 Elves -> T2 Hydra. Over Khalni they
  trim the T8+ tail more; over Rampant Growth the average a hair more.
- **Kessig Wolf Run: -0.0109 +- 0.0008 (t = -13.6) on the Hydra + Elves list** (h4p_wr_elf4_rg0 vs
  h4p_elf4_rg0, a direct paired comparison) -- the USER's "clear 1-of", confirmed on the list it would join.
- The ~0.01 t add-ons (Turntimber, a 3rd Wave, Craterhoof) are near the noise of the apparatus, and
  Turntimber's is still the clairvoyance-suspect one.

## Round 3 — combining the round-2 winners (`logs/landfall_screen/r3.json`, seed 7900000)

Anchor `F0` = 4 Phylath -> 4 Hydra, 4 Rampant Growth -> 4 Llanowar Elves, Haunted Fengraf -> Kessig Wolf Run.
20,000 paired games; base 5.7257; F0 4.9387 (**-0.7870**; T7+ 6.62% -> 2.38%, T8+ 0.65% -> 0.27%).
Deltas below are PAIRED AGAINST F0 (negative = better than F0).

| arm | change vs F0 | vs F0 | se | T7+ | T8+ |
|---|---|---|---|---|---|
| F_b | Hydra for 2 Phylath + 2 Baloths (keeps 2 Phylath) | **-0.0244** | 0.0056 | 1.92% | 0.26% |
| F_rg2k2 | Elves for 2 Rampant Growth + 2 Khalni | **-0.0194** | 0.0036 | 1.82% | 0.24% |
| F_c | Hydra for 2 Phylath + 2 Courser | -0.0118 | 0.0056 | 2.04% | 0.34% |
| F_c_k | ... + Elves for Khalni | -0.0112 | 0.0062 | 1.70% | 0.19% |
| F_hoof | Craterhoof for 1 Courser | **-0.0104** | 0.0011 | 2.36% | 0.30% |
| F_wave3 | 3rd Genesis Wave for 1 Courser | -0.0095 | 0.0016 | 2.30% | 0.27% |
| F_p1c | Hydra for 3 Phylath + 1 Courser | -0.0062 | 0.0042 | 2.14% | 0.28% |
| F_t | Hydra for 2 Phylath + 2 Titan | -0.0049 | 0.0057 | 1.99% | 0.26% |
| F_c_bop2elf2 | F_c with 2 Birds + 2 Elves | -0.0035 | 0.0056 | 2.15% | 0.33% |
| F_wr2 | a 2nd Wolf Run for Radiant Fountain | -0.0024 | 0.0004 | 2.37% | 0.27% |
| F_8dork | + 4 Birds for 4 Khalni | -0.0008 | 0.0029 | 2.22% | 0.24% |
| F_k | Elves for Khalni instead of Rampant Growth | +0.0043 | 0.0035 | 1.88% | 0.18% |
| F_bop2elf2 | 2 Birds + 2 Elves | +0.0088 | 0.0009 | 2.52% | 0.30% |
| F_bop | 4 Birds instead of 4 Elves | +0.0172 | 0.0012 | 2.71% | 0.33% |

Reading: Llanowar Elves beat Birds of Paradise (the deck's red is easy; Birds' colour is not worth its
fragility -- here simply the same {G} at the same cost, and a 0/1 vs a 1/1 attacker). Splitting the Elves
cut across Rampant Growth and Khalni is better than either alone. With Hydra and Elves, the two Baloths are
now the most cuttable threat slot after Phylath. Craterhoof and a 3rd Wave each take ~0.01 t from a Courser.
Round 4 combines these on HELD-OUT seeds.

## Round 4 — the combinations, on HELD-OUT seeds (`logs/landfall_screen/r4.json`, seed 8000000)

20,000 paired games; base 5.7266 (T7+ 6.73%, T8+ 0.65%). Every arm: 4 Mossborn Hydra, 4 Llanowar Elves, Kessig
Wolf Run for Haunted Fengraf.

| arm | Hydra replaces | Elves replace | Courser slots | vs base | vs F0 | vs G1 | T7+ | T8+ |
|---|---|---|---|---|---|---|---|---|
| **G6** | 2 Phylath + 2 Baloths | 2 RG + 2 Khalni | Craterhoof + 3rd Genesis Wave | **-0.8411** | -0.0422 +-0.0062 | **-0.0080 +-0.0018** | 1.55% | 0.24% |
| G3 | 2 Phylath + 2 Baloths | 2 RG + 2 Khalni | 1 Courser + 3rd Wave | -0.8407 | -0.0418 | -0.0076 +-0.0015 | 1.50% | 0.18% |
| G2 | 2 Phylath + 2 Baloths | 2 RG + 2 Khalni | 1 Courser + Craterhoof | -0.8390 | -0.0401 | -0.0060 +-0.0009 | 1.54% | 0.21% |
| G7 | 1 Phylath + 2 Baloths + 1 Courser | 2 RG + 2 Khalni | 1 Courser | -0.8364 | -0.0374 | -0.0032 +-0.0038 | 1.42% | 0.18% |
| G1 | 2 Phylath + 2 Baloths | 2 RG + 2 Khalni | 2 Courser | -0.8331 | -0.0342 +-0.0061 | -- | 1.57% | 0.18% |
| G9 | 2 Phylath + 2 Baloths | 1 RG + 3 Khalni | 2 Courser | -0.8315 | -0.0326 | +0.0016 | 1.48% | 0.15% |
| G8 | 2 Phylath + 2 Baloths | 3 RG + 1 Khalni | 2 Courser | -0.8251 | -0.0262 | +0.0080 | 1.75% | 0.19% |
| G5 | 2 Phylath + 1 Baloths + 1 Courser | 2 RG + 2 Khalni | 1 Courser | -0.8217 | -0.0228 | +0.0114 | 1.62% | 0.19% |
| F_b | 2 Phylath + 2 Baloths | 4 RG | 2 Courser | -0.8125 | -0.0136 +-0.0056 | +0.0206 | 1.96% | 0.25% |
| F_rg2k2 | 4 Phylath | 2 RG + 2 Khalni | 2 Courser | -0.8121 | -0.0132 +-0.0036 | +0.0210 | 2.05% | 0.16% |
| G4 | 2 Phylath + 2 Courser | 2 RG + 2 Khalni | -- | -0.8104 | -0.0115 | +0.0227 | 1.82% | 0.22% |
| F0 | 4 Phylath | 4 RG | 2 Courser | -0.7989 | -- | +0.0342 | 2.40% | 0.29% |

Held out, the round-3 single winners SHRANK as selected winners do (F_b -0.0244 -> -0.0136, F_rg2k2
-0.0194 -> -0.0132) but held their sign, and they ADD: G1 = both = -0.0342 vs F0. A third Genesis Wave and/or
Craterhoof in the Courser slots adds another ~0.006-0.008 t. **Leading list: G6** (or G3, statistically tied),
-0.84 t vs the shipped list, T7+ tail 6.7% -> 1.5%.

## Non-clairvoyant bracket 1 — no post-search shuffle (`logs/landfall_screen/nss1.json`, `MTG_NO_SEARCH_SHUFFLE=1`)

Same pinned engine, seed 8000000 (round 4's), 20,000 paired games, every arm the same as in its default round.
With the shuffle off the search can no longer pick a re-roll it can see, so a gain that lived on clairvoyant
shuffle-steering would SHRINK here (SelesnyaLifegain's Windswept Heath went -0.028 -> -0.003).

| arm | NSS delta | default delta (round) |
|---|---|---|
| G6 | **-0.8555** | -0.8411 (r4, same seeds) |
| G3 | -0.8494 | -0.8407 (r4) |
| G1 | -0.8408 | -0.8331 (r4) |
| F_rg2k2 | -0.8094 | -0.8121 (r4) |
| F0 | -0.8044 | -0.7989 (r4) |
| h4p_elf4_rg0 | -0.7801 | -0.7798 (r2) |
| h4_phy0 | -0.6630 | -0.7088 (r2) / -0.6989 (r1) |
| wr_feng | **-0.0328** | -0.0138 (r1) |
| turntimber2 | -0.0263 | -0.0120 (r0) |
| wave3_c1 | -0.0101 | -0.0086 (r0) |
| hoof1_c1 | -0.0047 | -0.0103 (r0) |

**Base is 0.15 t SLOWER without the shuffle (5.8761 vs 5.7266 on the same seeds).** The shipped list leans on
its shuffles -- eight shufflers plus Courser's top-of-library -- and under the clairvoyant default some of that
is the search picking re-rolls it can see. The changes under test CUT shufflers (Rampant Growth, Khalni) and
Courser, so if anything the default screen understated them. Under NSS: the finalists gain slightly MORE;
Hydra-for-Phylath alone gains a little less (-0.66 vs -0.70); Elves are unchanged; Kessig Wolf Run gains more
than twice as much (-0.033). Turntimber Symbiosis also gains more, but NSS does not touch its real suspicion --
the front face LOOKS at the top seven, which no shuffle setting hides -- so it stays unverified.
**Nothing in the finalist depends on clairvoyant shuffle re-rolls.** (Cross-round rows compare different seeds
and, for r0/r1, the pre-pin engine: read them to ~0.01.)

## Non-clairvoyant sample 1 — STOPPED (`logs/landfall_screen/nc1.json`, `MTG_NC_SEARCH=1`)

base / G6 / h4_phy0, 300 games, seed 7800000, 20 workers. Stopped after 80 min (it was our own experiment, not a
user-requested run) because the arm that does not matter had become the whole run:

* **G6 finished: 300 games, avg 5.2267, 5,693 core-s in total (19 s a game).**
* **h4_phy0 (4 Hydra for 4 Phylath, Courser still in) was the cost.** ~283 of its 300 games were done, but 17 of
  the 20 workers were each on one of its games at over 1.1 h (finished ones ran to 4,418 s). The non-clairvoyant
  engine's reshuffle-averaged search is expensive on boards holding both Courser of Kruphix (a revealed top
  card) and a Hydra's counter growth; G6, with no Courser, averaged 19 s.
* **base had barely started** (~15 games). The batch dispatched it last, so it would have waited behind the
  stragglers on 3 workers, and G6 cannot be read without it.

h4_phy0 changes no shuffle effect, and the non-clairvoyant check matters mainly when a change alters the
number of shuffle effects (USER 2026-10-22). Dropping it cost nothing the screen needs. **Replaced by nc2**
(below): base, G6 and round 5's six shuffle-count arms on round 5's seeds, on every core.

## Round 5 — single changes around G6 (`logs/landfall_screen/r5.json`, seed 8100000)

20,000 games, `"centre": "G6"` so every arm is numbered against G6 and differs from it by exactly the copies
named (k = n on all 13). base 5.7313 (T7+ 6.93%); **G6 4.8952, -0.8361 vs base on fresh seeds** (T7+ 1.59%).
Deltas below are paired per game against G6 (`logs/landfall_screen/vsref.py`).

| arm | change from G6 | vs G6 | se | T7+ | shuffle count |
|---|---|---|---|---|---|
| G6_w5f11 | 2 Forest -> 2 Evolving Wilds (5) | **-0.0238** | 0.0028 | 1.38% | **+2** |
| G6_w4f12 | Forest -> Evolving Wilds (4) | -0.0128 | 0.0020 | 1.49% | **+1** |
| G6_f12em1 | Forest -> Elvish Mystic | -0.0112 | 0.0025 | 1.65% | = |
| G6_f14cu1 | Cultivate -> Forest | -0.0081 | 0.0023 | 1.77% | **-1** |
| G6_a3w4 | Avenger -> 4th Genesis Wave | -0.0068 | 0.0016 | 1.58% | = |
| G6_o0p3 | Omnath -> 3rd Phylath | -0.0056 | 0.0018 | 1.35% | = |
| G6_t3p3 | Primeval Titan -> 3rd Phylath | +0.0017 | 0.0019 | 1.49% | **-1** |
| G6_hf2w2 | Genesis Wave -> 2nd Craterhoof | +0.0057 | 0.0015 | 1.75% | = |
| G6_turf2f12 | Forest -> 2nd Gruul Turf | +0.0116 | 0.0019 | 1.57% | = |
| G6_f12p3 | Forest -> 3rd Phylath | +0.0307 | 0.0025 | 1.91% | = |
| G6_s3p3 | Search for Tomorrow -> 3rd Phylath | +0.0449 | 0.0025 | 1.91% | **-1** |
| G6_cu0p4 | 2 Cultivate -> 2 Phylath | +0.0563 | 0.0033 | 2.55% | **-2** |

Reading: the list wants MORE mana, not more threats -- every land or ramp spell cut for a Phylath is worse (by
0.03-0.06), while a Forest for a fifth one-drop dork is better. The ramp spells are not interchangeable with lands:
Cultivate -> Forest is mildly better, but cutting Search for Tomorrow or both Cultivates is clearly worse.
Among threats, a 4th Genesis Wave for an Avenger and a 3rd Phylath for Omnath are small gains; a 2nd Craterhoof
is worse than the 3rd Wave. Titan -> Phylath is neutral. The two Evolving Wilds arms ADD shuffles, so their gain
is exactly what clairvoyance would inflate; they go to the brackets (nss2 / nc3), as do the Cultivate, Search and
Titan arms, which change the shuffle count the other way. Selection bias applies: round 6 re-measures the
winners, combined, on held-out seeds.

## Non-clairvoyant bracket 2 — round 5's shuffle-count arms (`logs/landfall_screen/nss2.json`, `MTG_NO_SEARCH_SHUFFLE=1`)

Round 5's seeds (8100000), 20,000 games, centre G6; only the arms that change the number of shuffle effects
(USER 2026-10-22: the non-clairvoyant check matters mainly when a change alters the shuffle count).
base 5.8820 under NSS (0.15 t slower than default again); **G6 -0.8512 vs base** (default -0.8361).

| arm | change from G6 | default vs G6 | NSS vs G6 | NSS se |
|---|---|---|---|---|
| G6_w5f11 | 2 Forest -> 2 Evolving Wilds | -0.0238 | **+0.0001** | 0.0030 |
| G6_w4f12 | Forest -> Evolving Wilds | -0.0128 | -0.0036 | 0.0021 |
| G6_f14cu1 | Cultivate -> Forest | -0.0081 | -0.0053 | 0.0026 |
| G6_t3p3 | Primeval Titan -> Phylath | +0.0017 | +0.0004 | 0.0022 |
| G6_s3p3 | Search for Tomorrow -> Phylath | +0.0449 | +0.0382 | 0.0031 |
| G6_cu0p4 | 2 Cultivate -> 2 Phylath | +0.0563 | +0.0732 | 0.0040 |

**The Evolving Wilds gain is clairvoyance.** With the post-search shuffle off, the two extra Wilds go from the
round's best arm (-0.024) to exactly nothing (+0.000); one extra Wilds keeps a third of its gain, inside 2 se.
That is the user's warning ("clairvoyance loves shuffles so they will be overvalued") measured: the shuffle
added nothing a real player could use, and the search was picking re-rolls it could see. Do not add Wilds on
this screen's evidence. Arms that CUT a shuffler hold their sign (Cultivate -> Forest stays a small gain; the
Search and Cultivate cuts stay clearly worse), as they should if the default overvalues shuffles.

## Round 6 — round 5's winners combined, HELD OUT (`logs/landfall_screen/r6.json`, seed 8200000)

20,000 games, centre G6 (k = n on every arm). base 5.7260 (T7+ 6.89%); G6 4.8918, -0.8341 vs base. The arms were
fixed from round 5 before these seeds were played, so each designed combination's delta is not selection-biased
(picking the best of the 11 still is, a little).

| arm | change from G6 | vs G6 | se | vs base | T7+ | shuffle count |
|---|---|---|---|---|---|---|
| J1_w2 | J1 + 2 Forest -> 2 Evolving Wilds | -0.0565 | 0.0038 | -0.8906 | 1.23% | -1 +2 |
| J1_w1 | J1 + Forest -> Evolving Wilds | -0.0485 | 0.0034 | -0.8826 | 1.34% | -1 +1 |
| **J1_em2** | J1 + Forest -> 2nd Elvish Mystic | **-0.0433** | 0.0037 | -0.8775 | 1.51% | -1 |
| **J1** | Cultivate -> Elvish Mystic, Avenger -> 4th Genesis Wave, Omnath -> 3rd Phylath | **-0.0364** | 0.0030 | -0.8706 | 1.45% | -1 |
| J_ao_w2 | J_ao + 2 Forest -> 2 Evolving Wilds | -0.0347 | 0.0034 | -0.8689 | 1.28% | +2 |
| J_em_w2 | J_em + 2 Forest -> 2 Evolving Wilds | -0.0286 | 0.0036 | -0.8628 | 1.44% | +2 |
| J2 | J_ao + Forest -> Elvish Mystic | -0.0280 | 0.0033 | -0.8622 | 1.47% | = |
| J3 | J_ao + Cultivate -> Forest | -0.0249 | 0.0030 | -0.8591 | 1.53% | -1 |
| J_cuem | Cultivate -> Elvish Mystic | -0.0209 | 0.0021 | -0.8550 | 1.65% | -1 |
| J_ao | Avenger -> 4th Genesis Wave, Omnath -> 3rd Phylath | -0.0167 | 0.0023 | -0.8508 | 1.44% | = |
| J_em | Forest -> Elvish Mystic | -0.0132 | 0.0026 | -0.8474 | 1.63% | = |

Reading: the round-5 singles held out and ADD (J_ao -0.017 vs their r5 sum -0.012; J1 -0.036 vs J_cuem + J_ao
-0.038). Cultivate is the right slot for the Mystic (J_cuem -0.021 vs Forest -> Mystic -0.013), and a second
Mystic for a Forest adds another -0.007 on top of J1. The Evolving Wilds rows lead again, but bracket 2 showed
that gain is the clairvoyant search steering re-rolls; the NSS bracket of this round (nss3) re-checks them.

## Non-clairvoyant bracket 3 — all of round 6 (`logs/landfall_screen/nss3.json`, `MTG_NO_SEARCH_SHUFFLE=1`)

Round 6's seeds (8200000), 20,000 games, centre G6. base 5.8857; G6 -0.8456 vs base.

| arm | default vs G6 | NSS vs G6 | NSS se | the Wilds' own share (vs the arm without them), default -> NSS |
|---|---|---|---|---|
| J1_em2 | -0.0433 | **-0.0445** | 0.0042 | |
| J1 | -0.0364 | **-0.0428** | 0.0034 | |
| J1_w1 | -0.0485 | -0.0410 | 0.0039 | 1 Wilds on J1: -0.012 -> **+0.002** |
| J1_w2 | -0.0565 | -0.0363 | 0.0042 | 2 Wilds on J1: -0.020 -> **+0.007** |
| J3 | -0.0249 | -0.0304 | 0.0036 | |
| J2 | -0.0280 | -0.0294 | 0.0038 | |
| J_ao | -0.0167 | -0.0222 | 0.0027 | |
| J_cuem | -0.0209 | -0.0191 | 0.0023 | |
| J_ao_w2 | -0.0347 | -0.0176 | 0.0039 | 2 Wilds on J_ao: -0.018 -> **+0.005** |
| J_em | -0.0132 | -0.0074 | 0.0029 | |
| J_em_w2 | -0.0286 | -0.0043 | 0.0040 | 2 Wilds on J_em: -0.015 -> **+0.003** |

Held out, the Evolving Wilds artifact replicates on all three hosts: every Wilds share goes from a gain of
0.012-0.020 to nothing or a small loss. The non-Wilds candidates hold (J1 even grows, as a list that cuts a
Cultivate should if the default overvalues shuffles). J1_em2's second Mystic over J1 shrinks to -0.002 here
(default -0.007): J1 and J1_em2 are close; both are ~-0.04 t better than G6.

## Round 7 — single changes around K0 = J1_em2 (`logs/landfall_screen/r7.json`, seed 8300000)

20,000 games, centre K0 (k = n on every arm). base 5.7256; **K0 4.8498, -0.8758 vs base**; G6 +0.0440 behind K0
(round 6 had -0.043 the other way round: it replicates). K0 = G6 + Cultivate -> Elvish Mystic, Forest -> Elvish
Mystic, Avenger -> 4th Genesis Wave, Omnath -> 3rd Phylath.

| arm | change from K0 | vs K0 | se | T7+ | shuffle count |
|---|---|---|---|---|---|
| K_t3 | Primeval Titan -> 3rd Elvish Mystic | **-0.0294** | 0.0022 | 1.16% | -1 |
| K_fnt | Radiant Fountain -> Forest | -0.0169 | 0.0018 | 1.20% | = |
| K_mtn1 | Mountain -> Forest | -0.0118 | 0.0019 | 1.30% | = |
| K_cu0 | last Cultivate -> 3rd Mystic | -0.0105 | 0.0019 | 1.50% | -1 |
| K_k1 | Khalni -> 3rd Mystic | -0.0102 | 0.0019 | 1.34% | -1 |
| K_rg1 | Rampant Growth -> 3rd Mystic | -0.0039 | 0.0015 | 1.40% | -1 |
| K_s3 | Search for Tomorrow -> 3rd Mystic | -0.0019 | 0.0019 | 1.41% | -1 |
| K_av2 | Avenger -> 4th Phylath | +0.0012 | 0.0015 | 1.35% | = |
| K_em3 | Forest -> 3rd Mystic | +0.0038 | 0.0025 | 1.52% | = |
| K_myr3 | Myriad Landscape -> Forest | +0.0122 | 0.0022 | 1.48% | -1 |
| K_tx | Terramorphic Expanse -> Forest | +0.0152 | 0.0020 | 1.44% | -1 |
| K_mtn0 | both Mountains -> Forest | +0.0524 | 0.0032 | 2.43% | = |

Reading: a one-mana dork beats every two-mana-or-more ramp card and the fourth Titan, but NOT a land (25 lands
is the floor here). Of the lands, the colourless Radiant Fountain and one of the two Mountains are the weak
slots; the second Mountain is load-bearing (the red for Kessig Wolf Run and the fetches' only basic Mountain
targets -- cutting both is the worst row). The fetch lands are pulling their weight even with the shuffle
counted honestly (the K_myr3 / K_tx cuts are worse, and cutting a shuffler is UNDER-stated by the default).
The Titan cut is the round's surprise (-0.029); Titan fetches Kessig Wolf Run, and with 7 dorks it is the
slowest-to-matter card in the list. NSS bracket of this round: nss4 (below). Round 8 combines the winners.

## Non-clairvoyant bracket 4 — all of round 7 (`logs/landfall_screen/nss4.json`, `MTG_NO_SEARCH_SHUFFLE=1`)

Round 7's seeds, 20,000 games, centre K0. base 5.8755; K0 -0.8814 vs base.

| arm | change from K0 | default vs K0 | NSS vs K0 | NSS se |
|---|---|---|---|---|
| K_t3 | Titan -> Mystic | -0.0294 | **-0.0343** | 0.0025 |
| K_fnt | Radiant Fountain -> Forest | -0.0169 | -0.0202 | 0.0020 |
| K_k1 | Khalni -> Mystic | -0.0102 | -0.0155 | 0.0021 |
| K_rg1 | Rampant Growth -> Mystic | -0.0039 | -0.0154 | 0.0014 |
| K_s3 | Search for Tomorrow -> Mystic | -0.0019 | -0.0093 | 0.0020 |
| K_mtn1 | Mountain -> Forest | -0.0118 | -0.0081 | 0.0021 |
| K_cu0 | Cultivate -> Mystic | -0.0105 | -0.0065 | 0.0022 |
| K_av2 | Avenger -> 4th Phylath | +0.0012 | -0.0036 | 0.0017 |
| K_tx | Terramorphic -> Forest | +0.0152 | +0.0050 | 0.0021 |
| K_em3 | Forest -> Mystic | +0.0038 | +0.0076 | 0.0028 |
| K_myr3 | Myriad Landscape -> Forest | +0.0122 | +0.0124 | 0.0024 |
| K_mtn0 | both Mountains -> Forest | +0.0524 | +0.1105 | 0.0042 |

Every shuffler-for-dork swap holds its sign and most GROW with the shuffle off (Rampant Growth -> Mystic
-0.004 -> -0.015), which is the direction the clairvoyance argument predicts for a cut shuffler. Nothing
round 8 builds on depends on a re-roll the search could see.

## Round 8 — round 7's winners combined + more dorks, HELD OUT (`logs/landfall_screen/r8.json`, seed 8400000)

20,000 games, centre K0 (k = n on every arm; keep-table fall-through 0.48% worst arm). base 5.7268; K0 4.8365
(-0.8902 vs base); G6 +0.0508 behind K0. Fyndhorn Elves (= Elvish Mystic, a third name so a list can hold more
than 8 one-drop {G} dorks) rides Khalni Heart Expedition's keep bucket.

| arm | change from K0 | vs K0 | se | vs base | T7+ |
|---|---|---|---|---|---|
| **L7** | L2 + a 2nd Titan -> Fyndhorn Elves | **-0.0814** | 0.0039 | **-0.9716** | 0.98% |
| **L4** | L1 + a 2nd Titan -> 4th Mystic | **-0.0795** | 0.0035 | -0.9697 | 0.86% |
| L6 | L2 + 2 Khalni -> 2 Fyndhorn | -0.0691 | 0.0038 | -0.9593 | 1.04% |
| L5 | L2 + Khalni -> Fyndhorn | -0.0680 | 0.0036 | -0.9582 | 1.09% |
| L3 | L1 + Khalni -> 4th Mystic | -0.0624 | 0.0034 | -0.9526 | 0.98% |
| L2 | L1 + Cultivate -> 4th Mystic | -0.0623 | 0.0035 | -0.9525 | 1.13% |
| L1 | Titan -> 3rd Mystic, Radiant Fountain -> Forest, Mountain -> Forest | -0.0536 | 0.0031 | -0.9438 | 1.00% |
| L8 | L2 + Rampant Growth -> Fyndhorn | -0.0536 | 0.0038 | -0.9438 | 1.26% |
| L1a | L1 without the Mountain change | -0.0450 | 0.0027 | -0.9352 | 1.06% |

Reading: round 7's three singles ADD held out (L1 -0.054 vs their sum -0.058; the Mountain is worth -0.009 of
it). The second Titan cut is worth another -0.026 on top (L4 vs L1), the same size as the first: Titan is the
list's most expendable expensive card once it has 8+ one-mana dorks. With the Titans down to two, Cultivate,
Khalni and Rampant Growth are interchangeable with a dork to within ~0.01 (L4 ~ L7 ~ L6/L5 > L8). **Leader:
L7 / L4, about -0.97 t vs the shipped list** (T7+ 6.6% -> 0.9-1.0%). Round 9 follows the Titan direction.

## Round 9 — single changes around M0 = L7 (`logs/landfall_screen/r9.json`, seed 8500000)

20,000 games, centre M0 (k = n; fall-through 0.49%). base 5.7345; **M0 4.7694, -0.9652 vs base**; K0 +0.0845
behind M0 (round 8: -0.081, replicates). M0 = 4 Hydra, 3 Phylath, 4 Genesis Wave, 1 Craterhoof, 1 Terastodon,
3 Avenger, 2 Titan, 4 Llanowar + 4 Mystic + 1 Fyndhorn, 4 Search, 2 Rampant Growth, 2 Khalni; 25 lands
(14 Forest, 1 Mountain, 3 Wilds, 4 Myriad, Gruul Turf, Kessig Wolf Run, Terramorphic).

| arm | change from M0 | vs M0 | se | T7+ |
|---|---|---|---|---|
| M_av2 | Avenger -> Fyndhorn | **-0.0256** | 0.0020 | 1.04% |
| M_t0 | both Titans -> 2 Fyndhorn | -0.0204 | 0.0026 | 1.13% |
| M_t1 | Titan -> Fyndhorn | -0.0141 | 0.0019 | 1.12% |
| M_k0 | both Khalni -> 2 Fyndhorn | -0.0041 | 0.0018 | 1.12% |
| M_k1 | Khalni -> Fyndhorn | -0.0029 | 0.0013 | 1.12% |
| M_f13 | Forest -> Fyndhorn | +0.0058 | 0.0022 | 1.21% |
| M_rg1 | Rampant Growth -> Fyndhorn | +0.0073 | 0.0018 | 1.31% |
| M_s3 | Search for Tomorrow -> Fyndhorn | +0.0081 | 0.0019 | 1.34% |
| M_w3 | Genesis Wave -> 4th Phylath | +0.0104 | 0.0018 | 1.24% |
| M_t1p | Titan -> 4th Phylath | +0.0157 | 0.0019 | 1.29% |

Reading: it is the DORK that gains, not the Titan cut (Titan -> Phylath is worse; Titan -> Fyndhorn better).
Every six-drop (Avenger, Titan) is now worth less than a one-mana {G} creature; the one-mana ramp spells
(Search, Rampant Growth) and the 25th land are not. **A goldfish caveat that grows with every dork added:** the
passive opponent never removes or blocks a creature, so a mana creature here is at its most valuable; a
real opponent's removal would tax a 10-dork list that the goldfish never charges.

## Non-clairvoyant bracket 6 — all of round 9 (`logs/landfall_screen/nss6.json`, `MTG_NO_SEARCH_SHUFFLE=1`)

(Brackets 5 and NC samples 2-5 were written as specs and superseded before they ran; numbering kept so the spec
files match.) Round 9's seeds, 20,000 games, centre M0. base 5.8816; M0 -0.9711 vs base; K0 +0.0838 behind.

| arm | change from M0 | default vs M0 | NSS vs M0 | NSS se |
|---|---|---|---|---|
| M_av2 | Avenger -> Fyndhorn | -0.0256 | **-0.0243** | 0.0024 |
| M_t1 | Titan -> Fyndhorn | -0.0141 | -0.0101 | 0.0023 |
| M_k0 | both Khalni -> 2 Fyndhorn | -0.0041 | -0.0073 | 0.0019 |
| M_t0 | both Titans -> 2 Fyndhorn | -0.0204 | **-0.0073** | 0.0031 |
| M_k1 | Khalni -> Fyndhorn | -0.0029 | -0.0040 | 0.0014 |
| M_rg1 | Rampant Growth -> Fyndhorn | +0.0073 | +0.0005 | 0.0020 |
| M_s3 | Search -> Fyndhorn | +0.0081 | +0.0013 | 0.0020 |
| M_w3 | Genesis Wave -> Phylath | +0.0104 | +0.0081 | 0.0021 |
| M_f13 | Forest -> Fyndhorn | +0.0058 | +0.0099 | 0.0027 |
| M_t1p | Titan -> Phylath | +0.0157 | +0.0160 | 0.0022 |

**The Titan count is NOT settled by this screen.** The FIRST Titan -> dork holds (-0.014 -> -0.010), but the
second does not: cutting both Titans is -0.020 by default and only -0.007 with the shuffle off, i.e. the
second cut's share goes -0.006 -> +0.003. The default prefers 0 Titans, NSS prefers 1, and both are within
~1-2 se of a tie. One Titan is the conservative reading (and keeps Kessig Wolf Run's fetcher, which the user
called out). Avenger -> dork holds unchanged (-0.024), the strongest single in round 9.

## Round 10 — round 9's winners combined + Ignoble Hierarch, HELD OUT (`logs/landfall_screen/r10.json`, seed 8600000)

20,000 games, centre M0 (k = n; fall-through 0.64%). base 5.7290; M0 4.7552 (-0.9738 vs base). Ignoble Hierarch
({G}, taps for {B}/{R}/{G}; exalted modelled) is the 13th-14th one-drop dork and the list's second red source;
it rides Rampant Growth's keep bucket.

| arm | change from M0 | vs M0 | se | vs base | T7+ |
|---|---|---|---|---|---|
| **N8** | 0 Avenger, 0 Titan; +3 Fyndhorn, +2 Hierarch (14 dorks) | **-0.0707** | 0.0039 | **-1.0445** | 1.44% |
| N3 | 1 Avenger, 0 Titan; +3 Fyndhorn, +1 Hierarch | -0.0576 | 0.0036 | -1.0314 | 1.20% |
| N4 | N2 + Kessig Wolf Run -> Forest | -0.0481 | 0.0034 | -1.0219 | 1.01% |
| N5 | N2 + Khalni -> Hierarch | -0.0424 | 0.0034 | -1.0162 | 0.93% |
| N2 | 2 Avenger, 0 Titan; +3 Fyndhorn | -0.0380 | 0.0031 | -1.0118 | 1.03% |
| N1 | 2 Avenger, 1 Titan; +2 Fyndhorn | -0.0336 | 0.0027 | -1.0074 | 0.95% |
| N_a | Avenger -> Fyndhorn (= M_av2) | -0.0237 | 0.0020 | -0.9975 | 0.96% |
| N_t | both Titans -> Fyndhorn (= M_t0) | -0.0193 | 0.0026 | -0.9931 | 1.03% |

Reading: the round-9 singles replicate held out (Avenger -0.024 / -0.026; Titans -0.019 / -0.020) and add
(N2 -0.038 vs -0.043). Each further six-drop out for a dork still gains (N3, N8), now past ONE TURN faster than
the shipped list. Two signs the direction is near its end: the T7+ tail turns back UP (1.0% -> 1.4% at N8: with
only 13 threats some games run out of gas), and the goldfish caveat above scales with the dork count.
**With no Titan, Kessig Wolf Run is worse than a basic Forest (N4 vs N2: -0.010)** -- its value in this list
came through Titan fetching it, which is the user's own reading of the card.

## Non-clairvoyant bracket 7 — all of round 10 (`logs/landfall_screen/nss7.json`, `MTG_NO_SEARCH_SHUFFLE=1`)

Round 10's seeds, 20,000 games, centre M0. base 5.8896; M0 -0.9898 vs base; M0 T7+ 3.49%.

| arm | default vs M0 | NSS vs M0 | NSS se | NSS T7+ | NSS unwon |
|---|---|---|---|---|---|
| N5 | -0.0424 | -0.0318 | 0.0043 | 4.03% | 0.38% |
| N3 | -0.0576 | -0.0309 | 0.0047 | 4.84% | 0.61% |
| N4 | -0.0481 | -0.0308 | 0.0043 | 4.16% | 0.51% |
| **N1** | -0.0336 | **-0.0279** | 0.0033 | **3.71%** | 0.35% |
| N2 | -0.0380 | -0.0254 | 0.0039 | 4.26% | 0.39% |
| N8 | **-0.0707** | **-0.0235** | 0.0054 | **5.85%** | **0.91%** |
| N_a | -0.0237 | -0.0215 | 0.0025 | 3.55% | 0.29% |
| N_t | -0.0193 | -0.0158 | 0.0032 | 3.89% | 0.31% |

**The clairvoyant default overvalues LOW-THREAT lists, through the shuffles.** N8 (no Avenger, no Titan, 14
dorks, 13 threats) loses two thirds of its gain with the shuffle off, and its slow tail more than doubles (T7+
3.5% -> 5.9%, unwon 0.22% -> 0.91%). The mechanism: a list with few threats and many shufflers (Search, Rampant
Growth, Khalni, Evolving Wilds, Myriad, Terramorphic) lets the clairvoyant search re-roll the library until a
threat is on top -- the same steering that inflated Evolving Wilds, here propping up thin threat density. With
the shuffle off the deep-threat games stay deep, as they do for a real player. Every arm that cuts a six-drop
shrinks the same way, in proportion to the threats it removes. **N1 (2 Avenger, 1 Titan, +2 Fyndhorn) is the
robust step: -0.034 default, -0.028 NSS, tail level with M0.** The arms are within ~1-2 se of each other under
NSS; the TAIL is what separates them. Round 11 is re-centred on N1 (it had been queued around N8).
