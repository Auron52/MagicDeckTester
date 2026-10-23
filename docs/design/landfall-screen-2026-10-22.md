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

## COURSE CORRECTION (USER 2026-10-23) — no mana creatures; landfall is the staple

On seeing rounds 5-10's lists (G6 / K0 / M0 / N1 / N8: 4 to 14 mana creatures, Craterhoof, and progressively
fewer Titans, Avengers, Cultivates and Khalnis): *"it seems you went quite off the deep end. I don't want to add
elves to every deck."* and *"The deck should be centered around landfall which means that lands entering and
cards that benefit from that must be a staple."*

The drift began in round 1, which read "other cards that might help" as licence to try Llanowar Elves; round 3
built them into the anchor (F0) and every later round inherited them. So **rounds 3-11 measure lists the deck
will not play, and none of G6 / K0 / M0 / N1 / N8 is a candidate.** What still stands from them is the
engine-level reading (Evolving Wilds and low-threat lists are flattered by the clairvoyant shuffle; the NSS
bracket is the cheap check). The non-clairvoyant sample 6 (`nc6`, dork lists only) and round 11 / NSS bracket 8
(around N1) were stopped; nc6's partial per-game log is kept as `logs/landfall_screen/nc6.partial.screen.err`.

**Candidate pool from here:** the shipped list's own cards, the cards the USER named (Mossborn Hydra -- itself
a landfall payoff --, Kessig Wolf Run as a 1-of, more Rampaging Baloths / Omnath / Genesis Wave, Terastodon kept)
and lands. No mana creatures, no Craterhoof (not a landfall card). The no-dork evidence that already exists is
rounds 0-2: four Hydra for 2 Phylath + 2 Courser / Baloths / Titan measured -0.727 / -0.725 / -0.723 vs shipped
(a near tie), and Kessig Wolf Run for Haunted Fengraf a further -0.0085 on that list.

## Round 12 — landfall-package swaps around A0 (`logs/landfall_screen/r12.json`, seed 8800000)

Anchor **A0 = shipped + 4 Mossborn Hydra (for 2 Phylath + 2 Courser) + Kessig Wolf Run (for Haunted Fengraf)**,
`"centre": "A0"`. Twenty single swaps, every one inside the landfall package: Phylath / Courser / Baloths /
Omnath / Genesis Wave / Cultivate against Titan, Avenger and each other, the two alternative Hydra cuts
(2 Baloths or 2 Titan instead of 2 Courser), and the lands (Radiant Fountain, a Mountain, 25 lands with a 3rd
Cultivate, Evolving Wilds 2 and 4). NSS bracket 9 runs the same spec with the shuffle off.

20,000 paired games per arm. base 5.7310 (T7+ 6.94%, T8+ 0.64%); **A0 5.0052, -0.7258 vs shipped on fresh
seeds** (se 0.0053; T7+ 2.17%, T8+ 0.24%, unwon 0.07%) -- round 2's no-dork reading, reproduced. Deltas are
paired per game against A0 (`logs/landfall_screen/vsref.py`); "shuffles" counts library searches added (+) or
removed (-), the quantity the clairvoyant default can flatter.

| arm | change from A0 | vs A0 | se | T7+ | shuffles |
|---|---|---|---|---|---|
| A_cu3b | 1 Rampaging Baloths -> 3rd Cultivate | **-0.0190** | 0.0020 | 1.70% | +1 |
| A_o0 | Omnath -> 3rd Genesis Wave | **-0.0146** | 0.0015 | 1.81% | 0 |
| A_cu3a | 1 Avenger -> 3rd Cultivate | -0.0124 | 0.0021 | 1.85% | +1 |
| A_ew4 | 1 Forest -> 4th Evolving Wilds | -0.0123 | 0.0021 | 2.03% | +1 |
| A_w3b | 1 Rampaging Baloths -> 3rd Genesis Wave | -0.0118 | 0.0015 | 1.96% | 0 |
| A_fnt | Radiant Fountain -> Forest | -0.0101 | 0.0016 | 2.05% | 0 |
| A_cu3t | 1 Titan -> 3rd Cultivate | -0.0092 | 0.0019 | 1.92% | 0 |
| A_c2b0 | 2 Baloths -> 2 Courser (the Hydras cut Baloths, not Courser) | -0.0089 | 0.0019 | 2.00% | 0 |
| A_w3a | 1 Avenger -> 3rd Genesis Wave | -0.0076 | 0.0016 | 2.19% | 0 |
| A_ph3a | 1 Avenger -> 3rd Phylath | -0.0065 | 0.0016 | 2.10% | 0 |
| A_w3t | 1 Titan -> 3rd Genesis Wave | -0.0043 | 0.0014 | 2.10% | -1 |
| A_mtn | 1 Mountain -> Forest | -0.0038 | 0.0018 | 2.14% | 0 |
| A_ph3t | 1 Titan -> 3rd Phylath | -0.0031 | 0.0018 | 2.09% | -1 |
| A_o2 | 1 Baloths -> 2nd Omnath | -0.0008 | 0.0013 | 2.18% | 0 |
| A_c2t2 | 2 Titan -> 2 Courser | +0.0031 | 0.0020 | 2.19% | -2 |
| A_c1 | 1 Phylath -> Courser | +0.0049 | 0.0020 | 2.28% | 0 |
| A_cu1 | 1 Cultivate -> 3rd Genesis Wave | +0.0055 | 0.0020 | 2.44% | -1 |
| A_b3 | 1 Phylath -> 3rd Baloths | +0.0097 | 0.0019 | 2.35% | 0 |
| A_ew2 | 1 Evolving Wilds -> Forest | +0.0149 | 0.0021 | 2.30% | -1 |
| A_l25 | 1 Forest -> 3rd Cultivate (25 lands) | **+0.0198** | 0.0024 | 2.18% | +1 |

Reading (clairvoyant default; the NSS bracket is the check for every arm with a non-zero shuffle count):
- **Rampaging Baloths is the weakest card in A0.** Every swap out of it gains (Cultivate -0.019, Wave -0.012,
  Courser -0.009 per two), and a third copy in for Phylath costs +0.010. Omnath likewise: out for a Wave -0.015,
  in for a Baloths ~0. Both are landfall payoffs, so cutting them is a USER call on the deck's shape.
- **Land-putting cards gain**: a 3rd Cultivate for any six-drop and a 3rd Genesis Wave (no shuffle, so not a
  clairvoyance artefact) for Omnath / Baloths / Avenger. Cultivate's gains carry a +1 shuffle and need nss9.
- **26 lands is the floor**: Forest -> Cultivate costs +0.020 although it adds a shuffle, the strongest single
  loss here. Radiant Fountain -> Forest -0.010 (a green source beats 2 life).
- **Evolving Wilds looks like round 5's clairvoyance again** (4th copy -0.012, 2nd copy cut +0.015), pending nss9.
- Titan and Avenger are close to neutral against the others (-0.003 to -0.012 out); Phylath out for Courser is
  worse, so the Hydras' best second cut stays Courser or Baloths (A_c2b0 prefers Baloths by -0.009).

## Non-clairvoyant bracket 9 — all of round 12 (`logs/landfall_screen/nss9.json`, `MTG_NO_SEARCH_SHUFFLE=1`)

Round 12's seeds, 20,000 games, centre A0. base 5.8761; **A0 -0.7118 vs shipped** (T7+ 14.29% -> 5.45%).

| arm | change from A0 | default vs A0 | NSS vs A0 | NSS se | NSS T7+ | shuffles |
|---|---|---|---|---|---|---|
| A_cu3b | 1 Baloths -> 3rd Cultivate | -0.0190 | **-0.0267** | 0.0025 | 4.81% | +1 |
| A_cu3a | 1 Avenger -> 3rd Cultivate | -0.0124 | **-0.0194** | 0.0027 | 4.83% | +1 |
| A_cu3t | 1 Titan -> 3rd Cultivate | -0.0092 | **-0.0178** | 0.0025 | 4.84% | 0 |
| A_o0 | Omnath -> 3rd Wave | -0.0146 | -0.0133 | 0.0017 | 5.08% | 0 |
| A_w3b | 1 Baloths -> 3rd Wave | -0.0118 | -0.0123 | 0.0017 | 5.30% | 0 |
| A_fnt | Radiant Fountain -> Forest | -0.0101 | -0.0111 | 0.0019 | 5.22% | 0 |
| A_ph3a | 1 Avenger -> 3rd Phylath | -0.0065 | -0.0099 | 0.0018 | 5.30% | 0 |
| A_w3a | 1 Avenger -> 3rd Wave | -0.0076 | -0.0092 | 0.0019 | 5.34% | 0 |
| A_w3t | 1 Titan -> 3rd Wave | -0.0043 | -0.0064 | 0.0018 | 5.37% | -1 |
| A_ph3t | 1 Titan -> 3rd Phylath | -0.0031 | -0.0052 | 0.0022 | 5.24% | -1 |
| A_c2b0 | 2 Baloths -> 2 Courser | -0.0089 | -0.0045 | 0.0024 | 5.70% | 0 |
| A_mtn | 1 Mountain -> Forest | -0.0038 | -0.0034 | 0.0021 | 5.54% | 0 |
| A_ew4 | 1 Forest -> 4th Evolving Wilds | -0.0123 | **-0.0001** | 0.0022 | 5.44% | +1 |
| A_o2 | 1 Baloths -> 2nd Omnath | -0.0008 | +0.0035 | 0.0016 | 5.68% | 0 |
| A_ew2 | 1 Evolving Wilds -> Forest | +0.0149 | +0.0052 | 0.0023 | 5.56% | -1 |
| A_c2t2 | 2 Titan -> 2 Courser | +0.0031 | +0.0078 | 0.0025 | 5.80% | -2 |
| A_c1 | 1 Phylath -> Courser | +0.0049 | +0.0116 | 0.0024 | 5.76% | 0 |
| A_b3 | 1 Phylath -> 3rd Baloths | +0.0097 | +0.0160 | 0.0022 | 5.78% | 0 |
| A_l25 | 1 Forest -> 3rd Cultivate (25 lands) | +0.0198 | +0.0205 | 0.0027 | 5.39% | +1 |
| A_cu1 | 1 Cultivate -> 3rd Wave | +0.0055 | **+0.0207** | 0.0025 | 6.20% | -1 |

Reading:
- **Cultivate's gain is NOT clairvoyance -- it GROWS with the shuffle off** (-0.019 -> -0.027 over a Baloths;
  cutting one costs +0.021). Cultivate puts a land onto the battlefield AND one in hand, so it is landfall fuel
  on two turns; without the shuffle re-roll to find threats, that card advantage is worth more, not less.
- **The 4th Evolving Wilds was clairvoyance** (-0.012 -> 0.000), as in rounds 5-7; the 2nd-copy cut shrinks too.
- Every no-shuffle swap holds (Omnath / Baloths -> Wave, Fountain -> Forest): those were never in question.
- **Courser does not earn a slot here**: 2 Courser for 2 Baloths halves to -0.0045 (t -1.9), and Courser for a
  Phylath or a Titan is worse (+0.012 / +0.008). The default engine already reads the top card, so Courser's
  information value is invisible to it: its reading is, if anything, slightly low.
- 26 lands stays the floor (+0.020 under both engines).

## USER decisions on round 12 (2026-10-23)

* *"Omnath I would keep over Baloths for the extra upside vs removal and sweepers"* -- *"(as a 1-of)"*. The
  goldfish has no removal or sweepers, so Omnath's resilience (a 5/5 per land drop rebuilds a board) is
  exactly what this apparatus cannot see; A_o0's -0.015 does not override it. **Omnath stays at 1; Rampaging
  Baloths is the cut.**
* *"I also agree with the Hydra replacing Courser."*, then: *"to be clear, we can still test courser in, but if
  things show it isn't worth being in the deck I have no issue with dropping it."* **Courser stays a
  candidate**: A0 cuts it, and round 13 tests putting it back in the Baloths and Avenger slots.

Round 13 was rewritten before it started (the Omnath-cutting draft is kept as `r13.omnath_cut.json`): every
arm holds 1 Omnath; three arms bring Courser back.

## Round 13 — round 12's winners combined, HELD OUT (`logs/landfall_screen/r13.json`, seed 8900000)

Centre A0, 20,000 games per arm. Omnath 1 in every arm.

| arm | change from A0 |
|---|---|
| C0 | Radiant Fountain -> Forest |
| C11 | C0 + 1 Baloths -> 3rd Cultivate |
| C1 | C0 + both Baloths -> 3rd Cultivate + 3rd Genesis Wave |
| C2 | C0 + both Baloths -> 2 Genesis Wave (4) |
| C3 | C0 + both Baloths -> 2 Cultivate (4) |
| C4 / C5 / C9 | C1 + 1 Avenger -> 4th Cultivate / 4th Wave / 3rd Phylath |
| C6 / C13 | C1 + 1 Titan -> 4th Wave / 4th Cultivate |
| C7 | C1 + 1 Mountain -> Forest |
| C8 | C1 + 1 Forest -> 4th Evolving Wilds |
| C10 | C0 + both Baloths -> 3rd Cultivate + 1 Courser |
| C12 | C0 + both Baloths -> 2 Courser |
| C14 | C1 + 1 Avenger -> 1 Courser |

NSS bracket 10 runs the same spec with the shuffle off.

20,000 paired games per arm, fresh seeds. base 5.7257 (T7+ 7.00%); **A0 4.9941, -0.7317 vs shipped** (T7+ 2.07%).

| arm | change from A0 | vs A0 | se | T7+ | T8+ |
|---|---|---|---|---|---|
| C8 | C1 + 1 Forest -> 4th Evolving Wilds | -0.0476 | 0.0034 | 1.44% | 0.19% |
| C4 | C1 + 1 Avenger -> 4th Cultivate | **-0.0444** | 0.0033 | 1.40% | 0.19% |
| C13 | C1 + 1 Titan -> 4th Cultivate | **-0.0425** | 0.0033 | **1.27%** | 0.19% |
| C7 | C1 + 1 Mountain -> Forest | -0.0400 | 0.0032 | 1.43% | 0.19% |
| C3 | C0 + both Baloths -> 2 Cultivate (4) | -0.0392 | 0.0031 | 1.35% | 0.19% |
| C5 | C1 + 1 Avenger -> 4th Wave | -0.0391 | 0.0031 | 1.48% | 0.18% |
| C9 | C1 + 1 Avenger -> 3rd Phylath | -0.0367 | 0.0032 | 1.43% | 0.19% |
| C6 | C1 + 1 Titan -> 4th Wave | -0.0365 | 0.0031 | 1.36% | 0.19% |
| **C1** | C0 + both Baloths -> 3rd Cultivate + 3rd Wave | **-0.0346** | 0.0028 | 1.44% | 0.19% |
| C14 | C1 + 1 Avenger -> Courser | -0.0319 | 0.0032 | 1.56% | 0.19% |
| C10 | C0 + both Baloths -> 3rd Cultivate + Courser | -0.0295 | 0.0028 | 1.48% | 0.18% |
| C2 | C0 + both Baloths -> 2 Wave (4) | -0.0277 | 0.0026 | 1.68% | 0.22% |
| C11 | C0 + 1 Baloths -> 3rd Cultivate (keeps 1 Baloths) | -0.0267 | 0.0025 | 1.57% | 0.18% |
| C12 | C0 + both Baloths -> 2 Courser | -0.0194 | 0.0025 | 1.67% | 0.18% |
| C0 | Radiant Fountain -> Forest | -0.0117 | 0.0017 | 1.94% | 0.21% |

Reading (default engine; nss10 is the check for C8 / C4 / C13 / C3, which add shuffles):
- **The round-12 singles add up on fresh seeds.** C1 (-0.035) is close to the sum of its three parts
  (-0.010 - 0.019 - 0.012), so C1 = shipped -0.77 t, with Omnath kept as the USER asked.
- **A 4th Cultivate is the next step**, for an Avenger (C4) or a Titan (C13, the thinnest tail: T7+ 1.27%).
  Splitting the Baloths slots Cultivate + Wave (C1) beats 2 Wave (C2) and is level with 2 Cultivate (C3).
- **Courser loses every slot it was offered**: in the 2nd Baloths slot it is 0.005 behind a Wave (C10 vs C1),
  for an Avenger 0.003 behind (C14 vs C1), both Baloths slots 0.015 behind (C12 vs C1).
- C8's 4th Evolving Wilds is round 12's clairvoyant shuffle again until nss10 says otherwise.

## Non-clairvoyant bracket 10 — all of round 13 (`logs/landfall_screen/nss10.json`, `MTG_NO_SEARCH_SHUFFLE=1`)

Round 13's seeds, 20,000 games, centre A0. base 5.8784; A0 -0.7184 vs shipped (T7+ 14.29% -> 5.35%).

| arm | change from A0 | default vs A0 | NSS vs A0 | NSS se | NSS T7+ |
|---|---|---|---|---|---|
| C13 | C1 + 1 Titan -> 4th Cultivate | -0.0425 | **-0.0583** | 0.0041 | **4.07%** |
| C4 | C1 + 1 Avenger -> 4th Cultivate | -0.0444 | **-0.0569** | 0.0041 | 4.30% |
| C3 | C0 + both Baloths -> 2 Cultivate (4) | -0.0392 | -0.0546 | 0.0038 | 4.13% |
| C9 | C1 + 1 Avenger -> 3rd Phylath | -0.0367 | -0.0536 | 0.0038 | 4.37% |
| C8 | C1 + 1 Forest -> 4th Evolving Wilds | -0.0476 | -0.0530 | 0.0041 | 4.42% |
| C6 | C1 + 1 Titan -> 4th Wave | -0.0365 | -0.0497 | 0.0038 | 4.38% |
| C5 | C1 + 1 Avenger -> 4th Wave | -0.0391 | -0.0481 | 0.0038 | 4.59% |
| **C1** | C0 + both Baloths -> 3rd Cultivate + 3rd Wave | -0.0346 | **-0.0468** | 0.0035 | 4.42% |
| C7 | C1 + 1 Mountain -> Forest | -0.0400 | -0.0466 | 0.0039 | 4.65% |
| C10 | C0 + both Baloths -> 3rd Cultivate + Courser | -0.0295 | -0.0389 | 0.0035 | 4.57% |
| C14 | C1 + 1 Avenger -> Courser | -0.0319 | -0.0378 | 0.0039 | 4.80% |
| C11 | C0 + 1 Baloths -> 3rd Cultivate | -0.0267 | -0.0364 | 0.0031 | 4.54% |
| C2 | C0 + both Baloths -> 2 Wave (4) | -0.0277 | -0.0328 | 0.0030 | 4.86% |
| C12 | C0 + both Baloths -> 2 Courser | -0.0194 | -0.0169 | 0.0030 | 5.13% |
| C0 | Radiant Fountain -> Forest | -0.0117 | -0.0131 | 0.0020 | 5.14% |

Reading:
- **Every combination holds or grows with the shuffle off**; C1 = shipped -0.765 t under NSS (-0.767 default).
- **The 4th Cultivate holds** (C13 / C4 -0.011 / -0.010 past C1 under NSS) and is the best step on both engines.
  Titan or Avenger as its cut is a tie (0.0014 apart, well inside one se).
- **The 4th Evolving Wilds halves** (C8 vs C1: -0.013 default -> -0.006 NSS): mostly clairvoyance, as before.
- **Mountain -> Forest is nothing under NSS** (C7 vs C1 +0.0002): its default -0.005 was shuffle-assisted.
- **Avenger -> 3rd Phylath gains more without the shuffle** (C9 vs C1 -0.007 NSS vs -0.002 default); round 14
  tests it on top of the 4th Cultivate.
- **Courser loses again**, by 0.008-0.009 vs C1 in both slots (C10, C14).

## Round 14 — the 4th Cultivate and its companions, around C1, HELD OUT (`logs/landfall_screen/r14.json`, seed 9000000)

`"centre": "C1"`, 20,000 paired games per arm. base 5.7275 (T7+ 6.65%); **C1 4.9662, -0.7613 vs shipped**
(T7+ 1.38%, T8+ 0.17%). Deltas vs C1.

| arm | change from C1 | vs C1 | se | T7+ | T8+ |
|---|---|---|---|---|---|
| D_a_tw_m | 1 Avenger -> 4th Cultivate, 1 Titan -> 4th Wave, 1 Mountain -> Forest | **-0.0196** | 0.0028 | 1.32% | 0.15% |
| D_t_m | 1 Titan -> 4th Cultivate, 1 Mountain -> Forest | -0.0146 | 0.0024 | 1.26% | 0.16% |
| D_t_aw | 1 Titan -> 4th Cultivate, 1 Avenger -> 4th Wave | -0.0138 | 0.0023 | 1.26% | 0.17% |
| D_a_ap | 1 Avenger -> 4th Cultivate, 1 Avenger -> 3rd Phylath | -0.0126 | 0.0025 | 1.34% | 0.18% |
| D_a_m | 1 Avenger -> 4th Cultivate, 1 Mountain -> Forest | -0.0124 | 0.0025 | 1.43% | 0.20% |
| D_a_tw | 1 Avenger -> 4th Cultivate, 1 Titan -> 4th Wave | -0.0123 | 0.0023 | 1.27% | 0.14% |
| D_t_tw | 1 Titan -> 4th Cultivate, 1 Titan -> 4th Wave (2 Titan) | -0.0112 | 0.0022 | **1.16%** | 0.17% |
| D_a_aw | 1 Avenger -> 4th Cultivate, 1 Avenger -> 4th Wave (2 Avenger) | -0.0088 | 0.0024 | 1.45% | 0.18% |
| D_t | 1 Titan -> 4th Cultivate | -0.0084 | 0.0018 | 1.21% | 0.16% |
| D_a | 1 Avenger -> 4th Cultivate | -0.0062 | 0.0019 | 1.36% | 0.18% |
| D_m | 1 Mountain -> Forest | -0.0060 | 0.0017 | 1.44% | 0.18% |
| D_aw | 1 Avenger -> 4th Wave | -0.0057 | 0.0015 | 1.42% | 0.17% |

Reading (default engine; nss11 next):
- **Diminishing returns.** Single steps from C1 are now worth 0.006-0.008 t and the best pair 0.012-0.015; the
  arms are mostly within one or two se of each other. The deck is near the top of this landfall-only family.
- **The 4th Cultivate is confirmed on fresh seeds** (D_t -0.008, D_a -0.006); cutting a Titan for it is
  slightly ahead of an Avenger on the average and the tail, but within one se.
- **A second six-drop -> 4th Wave or 3rd Phylath adds another ~0.004-0.006** on top (D_t_aw, D_a_tw, D_a_ap).
- Mountain -> Forest is -0.006 here but was 0 under NSS in round 13: it waits for nss11.

## Phylath (USER question, 2026-10-23) and round 15

USER: *"It's a little surprising to me that Phylath goes that quickly. Isn't it pretty strong at killing the next
turn?"*, *"It is a lot better than the Baloths"*, *"Even just in the references I made the Phylath copies were a
big help."*

**The references agree with the USER.** The ten hand-played games in `references/Landfall/` (shipped list) win on
T5, T7, T5, T6, T6, T8, T8, T5, T5, T5. **Phylath is in six of the ten killing attacks, and four of the five T5
wins are the same line:** Phylath on T4, then on T5 a land drop plus fetch / Myriad cracks put four landfall
triggers on ONE Plant (0/1 -> 16/17), and Phylath 5 + Plant 16 = 21, exactly lethal from 20 (gi0, gi7, gi8, gi9).
The engine models that targeting (the provider orders an un-sick Plant first, then the highest power).

**Why Phylath beats Baloths (USER):** *"Because they can attack next turn and take all of the landfall as current
turn damage. The Baloths are much worse because they need to wait another turn to realize the damage."* Phylath's
Plants (and Avenger's) exist BEFORE the turn's landfalls, so each land that turn is damage that turn; Mossborn
Hydra is the same shape at 3 mana. Rampaging Baloths' Beasts -- and Omnath's Elementals -- are CREATED by the
landfall, summoning sick, so they hit a turn later. The references show it: the two Baloths-led wins are T6, the
Phylath line T5.

**Phylath + Avenger (USER):** *"it does need some landfall support, but we have quite a lot of that in the deck"*
and *"in a tougher game ... Phylath and Avenger can work together."* Both make Plants, and each one's landfall
pumps the other's (Avenger: a counter on EVERY Plant; Phylath: four on any one) -- modelled, both by Plant
subtype. The goldfish rarely produces the tougher game (no removal, no blocks), so this shows mainly in the T7+
tail; round 15 separates it by adding Phylath while KEEPING four Avengers (E_tp, E_atpp keeps 3) vs cutting them
(E_ap, E_aapp). USER: *"this is a way in which they are understated. Even with the Legend rule and multiple
Phylaths you could still create more plants for Avenger, but most of the time having one of each is plenty to
break through an opponent's setup."* (A 2nd Phylath's ETB does resolve before the legend rule -- modelled.)
**Reading rule adopted for round 15 on:** where a Phylath / Avenger arm is within noise of a Titan / Wave arm,
the tie breaks toward Phylath + Avenger, because the goldfish understates them; said in the report each time.

**What the screen says about Phylath, and why its count is 2 so far:**
- It was cut 4 -> 2 in round 2 only to make room for the Hydras (Hydra for 4 Phylath beat Hydra for 2 Courser
  + 2 Baloths while keeping 4 Phylath by 0.12 t).
- **Legendary costs little here (USER):** *"legendary doesn't hurt much if the card kills on its own. In that case
  having access to one is what matters."* A second copy only matters in a game the first did not already win.
  What copies 3-4 buy is ACCESS: P(a Phylath among the 11 cards seen by T4 on the draw) is 34% at 2 copies, 46%
  at 3, 57% at 4. Their small measured gain is about what they REPLACE -- an Avenger or Titan, which also wins on
  its own, or the ramp that puts Phylath down on T4 -- so it is finisher-for-finisher, not finisher-for-nothing.
- Mossborn Hydra plays the same role (one attacker grown by landfall; 1 -> 16 counters over four landfalls, with
  trample) at 3 mana, so with four Hydras the marginal Phylath is worth less than in the references' list.
- Every test since has favoured a 3rd Phylath slightly: for an Avenger -0.007 / -0.010 NSS (round 12), -0.002 /
  -0.007 (round 13), +-0.006 on top of the 4th Cultivate (round 14); for a Titan -0.003 / -0.005. Cutting the
  2nd for Courser or Baloths costs +0.005..+0.016. Phylath > Baloths in every reading.

**Round 15 (`logs/landfall_screen/r15.json`, seed 9100000, HELD OUT)** measures the count directly: Phylath 0..4
around D_t (C1 + 1 Titan -> 4th Cultivate), 20,000 games, centre D_t. Arms: 3rd Phylath for an Avenger / a Titan
/ a Wave / a Hydra; 4 Phylath for 2 Avenger or Avenger + Titan; 3rd Phylath + a Forest -> Mountain (red); 1
Phylath (for a Titan or a Wave), 0 Phylath (for Titan + Wave); plus C1 and D_t_aw for reference. NSS bracket 12
follows, then the non-clairvoyant nc7 (now also holding the 3- and 4-Phylath lists).

## Non-clairvoyant bracket 11 — all of round 14 (`logs/landfall_screen/nss11.json`, `MTG_NO_SEARCH_SHUFFLE=1`)

Round 14's seeds, centre C1. base 5.8734; C1 -0.7597 vs shipped. vs C1: D_a_ap (Avenger -> 4th Cultivate +
Avenger -> 3rd Phylath) **-0.0167**, D_t_aw -0.0138, D_t_tw -0.0120, D_t -0.0117, D_t_m -0.0112, D_a_tw -0.0100,
D_a_tw_m -0.0092, D_a_aw -0.0076, D_a -0.0066, D_a_m -0.0053, D_aw -0.0030, **D_m (Mountain -> Forest) +0.0006**
(se 0.002-0.0035). The 4th Cultivate holds without the shuffle; Mountain -> Forest is again nothing; the best arm
is the one that adds a 3rd Phylath. These rounds trade threats for ramp, which the USER's goals below rule out
as a direction, and they share the alias apparatus questioned below.

## USER goals and constraints (2026-10-23, after round 14)

* *"the deck owner mentioned to me that they felt like the deck was lacking in threats. I'm guessing part of the
  reason is removal and another part draw variance. One of our top goals is to change that so there is some
  redundancy."*, *"we cannot simulate removal, but we could determine how often we have backup"*, *"we want
  higher threat density in the final list"*, *"All of our creatures are threats except arguably Titan and even
  then it is a decent threat."* -> **the final list holds MORE creatures than the shipped list's 18**; trading a
  threat for a Cultivate (C1 -> D_t) is the wrong direction, whatever the goldfish says (it has no removal).
* *"Hydra replacing courser helps a bit, but may not be sufficient."*, *"Dropping Baloths for more Hydra is
  probably okay, but that is also why I am a bit suspicious of the Phylath replacement."*
* **Haunted Fengraf stays**: *"That is a way to get back a threat. Especially when you can search it up with
  Titan."* Radiant Fountain only with care: *"there are definitely games where titan fetching that Fountain could
  be important. Less common likely than the Fengraf case, but still a real concern."* and *"Titan -> Fountain + Gruul Turf. This
  means you can play it twice for 4 life."* (the Turf's bounce returns the Fountain); *"if you are up against burn or aggro this can be handy. The fengraf is the
  opposite. If control is locking you down you need to be able to get back a critter."* -- matchup insurance the
  goldfish (no damage, no removal) cannot value -> Wolf Run's slot is
  re-opened: *"We should take a look at the cost of dropping other lands or other things."* The goldfish values
  neither card (it never kills a creature and never deals damage), so their cost is unmeasured by construction.

## The keep-table alias question

Every Hydra arm since round 1 rode Phylath's keep-table bucket (`arm_alias` Hydra -> Phylath). The shipped
table buckets by card name (`decks/Landfall/Landfall.buckets.json`), so an arm with 4 Hydra and 0 Phylath fills
that bucket exactly as the shipped 4 Phylath did, while 4 Hydra + 2-4 Phylath shows it 6-8 "Phylaths" and
keeps / bottoms a hand of Hydras as if it were a hand of 6-drops. That would flatter "Hydra replaces Phylath"
-- round 2's 0.12-0.14 t penalty for keeping 4 Phylath is the reading in question.
**Diagnostic (`chain13.sh`, seed 9300000):** U0 = shipped + 4 Hydra for 2 Courser + 2 Baloths (Phylath 4; lands
unchanged), U0_p3 / U0_p2 / U0_p0 = Phylath 3 / 2 / 0, measured under x1 (Hydra -> Phylath bucket, as before),
x2 (Hydra -> Courser bucket, a 3-drop for a 3-drop) and x3 (no alias: the table is dropped on every arm,
heuristic mulligan, symmetric). If the Phylath-count deltas move between x1 and x2 / x3, the alias biased them.

### x1 — Hydra on Phylath's bucket for EVERY arm (`logs/landfall_screen/x1.json`)

20,000 paired games, centre U0, fall-through 0.02% worst-arm. base 5.7226 (T7+ 6.64%).

| arm | Phylath | Hydra replaces | vs U0 | se | vs shipped | T7+ |
|---|---|---|---|---|---|---|
| **U0** | 4 | 2 Courser + 2 Baloths | -- | -- | **-0.7495** | **1.97%** |
| U0_p3 | 3 | 2 Courser + 1 Baloths + 1 Phylath | +0.0072 | 0.0017 | -0.742 | 2.16% |
| U0_p2 | 2 | 2 Courser + 2 Phylath | +0.0143 | 0.0024 | -0.735 | 2.29% |
| U0_p0 | 0 | 4 Phylath | +0.0287 | 0.0037 | -0.721 | 3.10% |

**Round 2's Phylath reading was an apparatus artefact, and the USER was right to doubt it.** Round 2 gave the
Hydra a DIFFERENT keep-table host per arm: the arms that cut Phylath rode Phylath's bucket, the arms that cut
Courser / Baloths / Wave rode COURSER's (`r2.json`: h4_c0_bal0 `arm_alias` Hydra -> Courser). So "4 Phylath kept
is 0.12-0.14 t worse" (h4_c0_bal0 -0.588 vs h4_phy0 -0.709) compared two keep tables, not two decks. With ONE
host for every arm the order reverses: **each Phylath cut costs ~0.007 t, and U0 (the USER's shape: Hydra for
Courser + Baloths, all four Phylath) is the best of the four, with the thinnest tail.** Every round from 5 on
used one host (Phylath) for all its Hydra arms, so their within-round comparisons are consistent; but A0 itself
was chosen on round 2's mixed-host reading. x2 (Courser host) and x3 (no table) say how much the host choice
moves the level.

### x2 — Hydra on COURSER's bucket for every arm (`logs/landfall_screen/x2.json`)

Same seeds (9300000, 20,000 paired games); U0_p0 left out (its Courser is real, so it cannot also host the Hydra).

| arm | Phylath | vs U0 | se | t | vs shipped | T7+ |
|---|---|---|---|---|---|---|
| **U0** | 4 | -- | -- | -- | **-0.594** | **2.71%** |
| U0_p3 | 3 | +0.0249 | 0.0020 | +12.7 | -0.569 | 3.08% |
| U0_p2 | 2 | +0.0551 | 0.0028 | +19.7 | -0.539 | 3.45% |

**The order holds under the other host, and the Phylath cuts cost MORE (~0.025-0.028 t per copy, vs ~0.007 on
Phylath's bucket).** What moves with the host is the LEVEL: U0 is -0.7495 vs shipped on Phylath's bucket and
-0.594 on Courser's, a 0.155 t swing from the keep table alone. So no absolute "vs shipped" number from a
Hydra arm is worth more than its host; only within-host comparisons are. x3 (no table on any arm) is the
host-free arbiter of the level.

### x3 — NO keep table on any arm (`logs/landfall_screen/x3.json`)

Same seeds; the table is dropped from EVERY arm, base included (the Hydra is not bucketed and `pool_table` is
false), so every arm mulligans with the deck's default keep rules -- the host-free measurement.

| arm | Phylath | vs U0 | se | t | vs shipped (no table either) | T7+ |
|---|---|---|---|---|---|---|
| **U0** | 4 | -- | -- | -- | **-0.780** | **3.02%** |
| U0_p3 | 3 | +0.0180 | 0.0010 | +18.8 | -0.762 | 3.24% |
| U0_p2 | 2 | +0.0369 | 0.0014 | +27.1 | -0.743 | 3.39% |
| U0_p0 | 0 | +0.0795 | 0.0022 | +36.3 | -0.700 | 4.07% |

**Settled: U0 is the best of the four under all three apparatus** (Phylath's bucket, Courser's bucket, no
table), each Phylath cut costing 0.007-0.028 t per copy by host and 0.018-0.020 with no table. The level
moves with the apparatus (U0 vs shipped: -0.750 / -0.594 / -0.780) because a Hydra arm riding another card's
keep bucket mulligans like a different deck; with no table both sides keep alike. **Apparatus for the
new-card rounds: no keep table on any arm** (x3's shape) -- slower (~74 min per 100k games against ~12 with
a table) but free of the host choice that produced round 2's artefact.

## New cards (USER 2026-10-23: "Let's add Moraug, Fury of Akoum, Scute Swarm and maybe even Scythecat Cub")

Budget caveat (USER: "the owner is a bit under a budget"; "The Cub is especially expensive"; "Hydra is not cheap
either"). Cheapest printings on Scryfall, 2026-10-23: Scythecat Cub $27.48, Mossborn Hydra $13.72, Scute Swarm $8.22,
Primeval Titan $5.00, Moraug $1.93; Avenger / Wolf Run / Cultivate / Genesis Wave / Omnath / Phylath $0.30-0.50.

Implemented and verified (byte-identical for every other deck: smoke 148/148, regression 282/282, 656 references):
* **Scute Swarm, Scythecat Cub** -- d7381d0c9. Engine rules they needed: a multi-land put (Titan's two, Genesis Wave)
  is ONE land event for landfall (a Scute copy made during it does not trigger for its lands: 1 Swarm -> 3 on Titan,
  not 4; a token another trigger of the same land made is not a Cub target), and counter DOUBLINGS resolve after a
  land's other landfall triggers (the controller's order, a dominance). The Cub's target follows the Phylath pick
  rule; its doubling goes to the attack-ready body with the most counters (PROVISIONAL).
* **Moraug, Fury of Akoum** -- 5e25e1e23. Additional combat phases: a main-1 landfall's combat runs BEFORE the
  regular one (which then does not untap: no extra attack), a main-2 landfall's after it (untap, attack again).
  +1/+0 per attack this turn. While Moraug is ours the post-combat main is searched (full menu) and a land held
  since main 1 may be played there (PROVISIONAL exemption from the newly-arrived-lands rule). Harness on a Moraug
  list: 0 non-convergence (60 games, d3), 0 commit-the-line divergence (40 games, d5 full depth).

Backup odds (P(>=2 threats by T4/T5/T6 on the draw), P(>=3 by T6); every creature a threat):
18 creatures (U0, shipped) 91.0/93.7/95.7%, 83.0%; 20: 94.4/96.3/97.6%, 89.1%; 22: 96.6/97.9/98.7%, 93.4%.

Rounds (no keep table on any arm; pinned PGO engines in build/scratch/lfscreen/):
* **n1** (`logs/landfall_screen/n1.json`, seed 9400000, 20,000 games, pin d7381d0): U0 vs Cub / Scute in the slots
  of Rampant Growth / Search for Tomorrow / Khalni / Hydra / a Forest, plus the budget mixes (Hydra 2 + Scute 2,
  Cub 2 + Scute 2). **nss13**: its first 10,000 games with MTG_NO_SEARCH_SHUFFLE=1 (the cuts change the number of
  shuffle effects).
* **m1** (`m1.json`, seed 9500000, pin 5e25e1e): Moraug 1-2 for Rampant Growth / Genesis Wave / Hydra and a
  3-Mountain list, plus the KESSIG WOLF RUN SLOT arms (Wolf Run for one Forest / Mountain / Fountain / Wilds /
  Terramorphic / Myriad / Gruul Turf / Rampant Growth; Fengraf kept; Fountain's burn/aggro value is NOT measured
  here), with MTG_DUMP_CARDS=1 (seen-but-never-cast: Omnath / Moraug red on the 1-Mountain arm). **nss14**: NSS.

### n1 — Scythecat Cub / Scute Swarm around U0 (`logs/landfall_screen/n1.json`, seed 9400000, 20,000 paired games)

No keep table on any arm; pin d7381d0. Average win turn (unwon = 9), paired against U0 (18 creatures). "Buy" is what
the owner pays for the ADDED copies at the cheapest printing (Cub $27.48, Hydra $13.72, Scute $8.22).

| arm | change from U0 | creatures | vs U0 | se | t | vs shipped | T7+ | buy |
|---|---|---|---|---|---|---|---|---|
| **N_cKH** | 4 Cub for 4 Khalni Heart Expedition | 22 | **-0.2660** | 0.0041 | -64.8 | **-1.026** | **1.71%** | $110 |
| N_cRG | 4 Cub for 4 Rampant Growth | 22 | -0.1865 | 0.0042 | -43.9 | -0.946 | 3.08% | $110 |
| N_cST | 4 Cub for 4 Search for Tomorrow | 22 | -0.1578 | 0.0047 | -33.7 | -0.918 | 2.72% | $110 |
| N_c2RG | 2 Cub for 2 Rampant Growth | 20 | -0.0827 | 0.0028 | -29.2 | -0.842 | 2.96% | $55 |
| N_cF | 1 Cub for a Forest | 19 | -0.0350 | 0.0030 | -11.7 | -0.795 | 3.07% | $27 |
| U0 | -- | 18 | -- | -- | -- | -0.760 | 3.22% | -- |
| N_c2s2RG | 2 Cub + 2 Scute for 4 Rampant Growth | 22 | +0.0227 | 0.0047 | +4.9 | -0.737 | 4.54% | $71 |
| N_s2RG | 2 Scute for 2 Rampant Growth | 20 | +0.1061 | 0.0038 | +28.1 | -0.654 | 4.73% | $16 |
| N_cH | 4 Cub for the 4 Hydra | 18 | +0.1134 | 0.0049 | +23.2 | -0.646 | 2.60% | $110 (sell 4 Hydra) |
| N_sST | 4 Scute for 4 Search for Tomorrow | 22 | +0.2279 | 0.0035 | +64.6 | -0.532 | 5.55% | $33 |
| N_sRG | 4 Scute for 4 Rampant Growth | 22 | +0.2495 | 0.0056 | +44.5 | -0.510 | 7.47% | $33 |
| N_h2s2 | 2 Scute for 2 Hydra | 18 | +0.2956 | 0.0040 | +73.7 | -0.464 | 4.46% | $16 |
| N_sH | 4 Scute for the 4 Hydra | 18 | +0.7251 | 0.0051 | +141.6 | -0.035 | 6.87% | $33 |
| shipped | (2 Courser, 2 Baloths, no Hydra) | 16 | +0.7597 | 0.0055 | +139.2 | -- | 8.60% | -- |

* **Scythecat Cub is a real gain, and the slot matters more than the count.** Four Cubs for the four Khalni Heart
  Expeditions is the best list of the screen so far: -0.266 t against U0, -1.026 against the shipped list, and it
  nearly HALVES the slow tail (games still going on turn 7: 1.71% against 3.22%) -- the backup the USER asked for
  (22 creatures: >=2 threats by T4/T5/T6 96.6/97.9/98.7%, >=3 by T6 93.4%, against U0's 91.0/93.7/95.7%, 83.0%).
  Khalni is the weakest of the three ramp slots (Rampant Growth -0.187, Search -0.158 for the same four Cubs).
  Two Cubs buy about 45% of four (-0.083 for Rampant Growth); one Cub for a Forest buys -0.035.
* **Head to head the Hydra beats the Cub** (Cub for Hydra +0.113): the Cub is an addition, not a replacement, and
  per dollar the Hydra is the better buy.
* **Scute Swarm is worse than every card it replaced, ramp included** (+0.106 for 2 Rampant Growth, +0.23-0.25 for
  four ramp spells, +0.725 for the four Hydras). A 1/1 for three whose landfall tokens are summoning-sick and whose
  copying needs six lands does not race a goldfish. Drop it.
* **Clairvoyance caveat:** Khalni, Rampant Growth and Search for Tomorrow are all shuffle effects, so every Cub-for-
  ramp arm changes the deck's shuffle count -- nss13 (MTG_NO_SEARCH_SHUFFLE=1, first 10,000 games) brackets it.
* **Speed and a perf fix.** n1 played 280,000 games in 3 h 11 min (11:37-14:48 UTC). The Scute arms carried games of
  up to 2,951 s (one ran 1.83 h): with hundreds of attacking Scute tokens, Primeval Titan's attack fetch re-found
  every attacker by number once per attacker through a quadratic scan -- cubic per projected combat, 98.8% of a
  960 s game. Fixed byte-identically in a16b898b3 (same digest, 8 s on PGO; smoke 148/148 unchanged); nss13, m1
  and nss14 run on that pin (pin-a16b898). The remaining per-node cost on huge token boards (FireLandfallTriggers'
  per-watcher scans, the lord/aura power reads) is a follow-up only if Scute were adopted.

### nss13 — n1's first 10,000 games with `MTG_NO_SEARCH_SHUFFLE=1` (`logs/landfall_screen/nss13.json`, pin a16b898)

| arm | n1, same 10,000 games | nss13 | shift |
|---|---|---|---|
| N_cKH (4 Cub for Khalni) | -0.2695 | **-0.3214** | -0.052 |
| N_cRG (4 Cub for Rampant Growth) | -0.1842 | -0.2518 | -0.068 |
| N_cST (4 Cub for Search) | -0.1537 | -0.2178 | -0.064 |
| N_c2RG (2 Cub for Rampant Growth) | -0.0789 | -0.1226 | -0.044 |
| N_cF (1 Cub for a Forest) | -0.0343 | -0.0451 | -0.011 |
| N_c2s2RG | +0.0265 | -0.0341 | -0.061 |
| N_cH (4 Cub for the Hydra) | +0.1142 | +0.0575 | -0.057 |
| Scute arms | +0.11 to +0.72 | +0.09 to +0.68 | -0.006 to -0.042 |
| shipped | +0.7553 | +0.7263 | -0.029 |

**The order is unchanged and the Cub's gain is LARGER without the clairvoyant shuffle** (Khalni slot -0.321, se
0.0066). Cutting three-to-four shuffle effects was understated by the clairvoyant search, as expected ("clairvoyance
loves shuffles"). Part of the shift is not about the shuffle count -- Cub for Hydra changes no shuffle effect and
still moves -0.057 -- so the cheap two-drop threat itself reads better with less lookahead. Hydra still beats Cub
head to head; Scute still loses everywhere.

### m1 — Moraug and the Kessig Wolf Run slot around U0 (`logs/landfall_screen/m1.json`, seed 9500000, 20,000 paired games)

No keep table on any arm; pin a16b898 (byte-identical to 5e25e1e on these lists). 300,000 games 16:25-18:34 UTC
(~2 h 10 min including the card-score pooling for Moraug / Wolf Run). Moraug $1.93, Wolf Run $0.40.

| arm | change from U0 | creatures | vs U0 | se | t | T7+ |
|---|---|---|---|---|---|---|
| **M_2W** | 2 Moraug for 2 Genesis Wave | 20 | **-0.0242** | 0.0015 | -16.1 | 2.90% |
| W_Fo | Wolf Run for Radiant Fountain | 18 | -0.0022 | 0.0006 | -3.8 | 2.83% |
| U0 | -- | 18 | -- | -- | -- | 2.96% |
| W_M | Wolf Run for a Mountain (1 Mountain left) | 18 | +0.0003 | 0.0009 | +0.3 | 2.89% |
| W_GT | Wolf Run for Gruul Turf | 18 | +0.0051 | 0.0016 | +3.3 | 3.29% |
| W_F | Wolf Run for a Forest | 18 | +0.0180 | 0.0014 | +12.6 | 3.15% |
| W_RG | Wolf Run for a Rampant Growth | 18 | +0.0181 | 0.0029 | +6.2 | 3.06% |
| M_1RG | 1 Moraug for 1 Rampant Growth | 19 | +0.0207 | 0.0026 | +7.8 | 3.31% |
| W_My | Wolf Run for a Myriad Landscape | 18 | +0.0297 | 0.0017 | +17.3 | 3.24% |
| W_TE | Wolf Run for Terramorphic Expanse | 18 | +0.0331 | 0.0019 | +17.8 | 3.28% |
| W_EW | Wolf Run for an Evolving Wilds | 18 | +0.0396 | 0.0020 | +19.8 | 3.50% |
| M_2RG | 2 Moraug for 2 Rampant Growth | 20 | +0.0600 | 0.0038 | +15.8 | 4.04% |
| M_2RG_mt | the same with 3 Mountain / 12 Forest | 20 | +0.0772 | 0.0040 | +19.4 | 4.28% |
| M_2H | 2 Moraug for 2 Hydra | 18 | +0.2167 | 0.0035 | +62.7 | 3.85% |
| shipped | | 16 | +0.7604 | 0.0055 | +138.8 | 8.23% |

Seen-but-never-cast (`MTG_DUMP_CARDS=1`, `logs/landfall_screen/cardseen.py`; "never" = share of the games where a copy
reached hand in which no copy was cast / played):

| card | U0 | M_2W | W_M (1 Mountain) | shipped |
|---|---|---|---|---|
| Omnath, Locus of Rage | 90.3% | 91.9% | **96.7%** | 82.3% |
| Moraug, Fury of Akoum | -- | 50.6% | -- | -- |
| Genesis Wave | 48.6% | -- | 48.9% | 24.7% |
| Kessig Wolf Run | -- | -- | 7.7% | -- |
| Mountain | 10.8% | 9.2% | 6.9% | 6.0% |

* **Moraug earns a slot only in Genesis Wave's, and only a small one** (-0.024 for two; $3.86). In a ramp slot it
  is worse (+0.021 for one, +0.060 for two), with more Mountains worse still (+0.077), and the Hydra beats it by
  0.217. Moraug and Wave are cast about equally often (half the games each is drawn), so the gain is the body:
  M_2W has 20 creatures. n2 tests it beside the four Cubs and against 2 Baloths for Wave (is it Moraug, or any
  creature for Wave?).
* **Kessig Wolf Run is neutral at best in the goldfish.** Its only non-negative slots are Radiant Fountain (-0.002)
  and a Mountain (+0.000); it costs 0.03-0.04 in a fetch / Myriad slot (fewer landfall doublers). Like Fountain's
  life, its real value -- trample-pumping a Plant over a blocker -- is invisible against a goldfish with no
  blockers, so this says only that it costs nothing in the Fountain or Mountain slot.
* **Omnath is mostly a dead draw in this list**: cast in under 10% of the games it is drawn in (7 mana in a deck
  that wins on turn 5), against 17.7% in the slower shipped list. One Mountain makes it worse (96.7% uncast) --
  the USER's red-mana concern is real, but it is a 1-of rarely cast either way.
* nss14 (first 10,000 games, MTG_NO_SEARCH_SHUFFLE=1) brackets the Wolf Run arms that cut a shuffle effect
  (Wilds, Terramorphic, Myriad, Rampant Growth).

### nss14 — m1's first 10,000 games with `MTG_NO_SEARCH_SHUFFLE=1` (`logs/landfall_screen/nss14.json`)

| arm | m1, same 10,000 games | nss14 | se | shift |
|---|---|---|---|---|
| M_2W (2 Moraug for 2 Wave) | -0.0253 | **-0.0392** | 0.0025 | -0.014 |
| W_Fo (Wolf Run for Fountain) | -0.0029 | -0.0134 | 0.0013 | -0.011 |
| W_M (Wolf Run for a Mountain) | +0.0017 | -0.0073 | 0.0020 | -0.009 |
| W_RG (Wolf Run for a Rampant Growth) | +0.0227 | -0.0003 | 0.0046 | -0.023 |
| W_GT (for Gruul Turf) | +0.0039 | +0.0069 | 0.0027 | +0.003 |
| W_F (for a Forest) | +0.0178 | +0.0073 | 0.0024 | -0.011 |
| M_1RG | +0.0265 | +0.0094 | 0.0044 | -0.017 |
| W_TE / W_EW / W_My | +0.032 / +0.040 / +0.027 | +0.010 / +0.014 / +0.016 | ~0.0027 | -0.011 to -0.026 |
| M_2RG / M_2RG_mt | +0.061 / +0.079 | +0.034 / +0.051 | ~0.0063 | -0.028 |
| M_2H | +0.2155 | +0.1962 | 0.0049 | -0.019 |

**Same order.** Moraug for Genesis Wave stays the only Moraug gain (-0.025 to -0.039 across the bracket). Kessig
Wolf Run is bracketed at [-0.003, -0.013] in Fountain's slot and [+0.002, -0.007] in a Mountain's -- a small gain
at best, before its unmeasured blocker value and Fountain's unmeasured life. The arms that cut a shuffle effect
(Rampant Growth, Wilds, Terramorphic, Moraug for ramp) gain 0.02-0.03 without the clairvoyant shuffle, as in nss13.

### n2 — the Khalni slot, priced (`logs/landfall_screen/n2.json`, seed 9600000, 20,000 paired games, HELD OUT from n1)

No keep table on any arm; pin a16b898. 340,000 games 19:56-21:44 UTC including the card-score pooling (1 h 48 min).
Backup odds = P(>=2 threats by T4/T5/T6 on the draw), P(>=3 by T6); 21 creatures 95.6/97.2/98.3%, 91.4%; 24 creatures
98.0/98.9/99.4%, 96.2% (the others are in the New cards section).

| arm | change from U0 | creatures | vs U0 | se | t | vs shipped | T7+ | buy |
|---|---|---|---|---|---|---|---|---|
| **K0** | 4 Cub for 4 Khalni | 22 | **-0.2632** | 0.0042 | -63.3 | **-1.029** | **1.77%** | $110 |
| K0_mw | K0 + 2 Moraug for 2 Genesis Wave | 24 | -0.2627 | 0.0042 | -62.3 | -1.028 | 1.78% | $114 |
| K_mix | 4 Cub for 2 Khalni + 2 Rampant Growth | 22 | -0.2374 | 0.0040 | -58.8 | -1.003 | 2.14% | $110 |
| K_c3 | 3 Cub for 3 Khalni | 21 | -0.1885 | 0.0036 | -52.6 | -0.954 | 2.02% | $82 |
| K0_b2RG | K0 + 2 Baloths for 2 Rampant Growth | 24 | -0.1872 | 0.0054 | -35.0 | -0.953 | 2.84% | $111 |
| K0_b2ST | K0 + 2 Baloths for 2 Search | 24 | -0.1705 | 0.0047 | -36.2 | -0.936 | 2.46% | $111 |
| K_c2 | 2 Cub for 2 Khalni | 20 | -0.1132 | 0.0028 | -40.2 | -0.879 | 2.32% | $55 |
| K_c2F2 | 2 Cub + 2 Forest for 4 Khalni | 20 | -0.1104 | 0.0048 | -23.2 | -0.876 | 2.19% | $55 |
| K_c1 | 1 Cub for 1 Khalni | 19 | -0.0480 | 0.0018 | -26.2 | -0.813 | 2.67% | $27 |
| K_c2b2 | 2 Cub + 2 Baloths for 4 Khalni | 22 | -0.0357 | 0.0045 | -7.9 | -0.801 | 3.23% | $56 |
| M_2W | 2 Moraug for 2 Genesis Wave | 20 | -0.0237 | 0.0015 | -15.5 | -0.789 | 3.04% | $4 |
| U0 | -- | 18 | -- | -- | -- | -0.765 | 3.19% | -- |
| W_b2 | 2 Baloths for 2 Genesis Wave | 20 | +0.0331 | 0.0014 | +24.5 | -0.732 | 3.77% | $1 |
| K_F | 4 Forest for 4 Khalni | 18 | +0.0704 | 0.0056 | +12.6 | -0.695 | 3.14% | -- |
| K_b4_mw | 4 Baloths for Khalni + 2 Moraug for Wave | 22 | +0.1969 | 0.0056 | +35.3 | -0.568 | 6.14% | $5 |
| K_b4 | 4 Baloths for 4 Khalni | 22 | +0.2333 | 0.0055 | +42.7 | -0.532 | 6.72% | $1 |
| shipped | | 16 | +0.7653 | 0.0054 | +141.0 | -- | 8.35% | -- |

* **K0 replicates on held-out seeds** (-0.263 against n1's -0.266) and stays the best list: -1.03 against the shipped
  list, slow tail (T7+) 1.77% against 3.19%.
* **The gain is the Cub, not the cut.** Four Forests for the four Khalnis is WORSE (+0.070), so Khalni is worth ~0.018
  a copy over a Forest and the Cub ~0.066 a copy over Khalni. Two Cubs with the other two Khalnis cut to Forests (K_c2F2,
  -0.110) is the same as keeping them (K_c2, -0.113).
* **The Cub ladder is close to linear, i.e. a budget can stop anywhere**: 1 / 2 / 3 / 4 Cubs = -0.048 / -0.113 / -0.189
  / -0.263 ($27 / $55 / $82 / $110; ~0.024-0.028 t per $10 at every step). Cutting ramp beside Khalni costs a little
  (4 Cubs for 2 Khalni + 2 Rampant Growth -0.237 against -0.263).
* **Rampaging Baloths is the wrong cheap threat.** Every Baloths arm loses to the card it replaces: 4 for Khalni +0.233,
  2 for Wave +0.033, 2 for a ramp spell next to four Cubs +0.076 to +0.093. A 6-mana 6/6 does not race a goldfish that
  wins on turn 5 even when it adds threat density (24 creatures in K0_b2RG and still worse than K0).
* **Moraug is a real but small gain over Genesis Wave on its own (-0.024; Baloths there +0.033, so it is Moraug, not
  just a creature), and adds NOTHING beside four Cubs** (K0_mw -0.2627 against K0 -0.2632). With four Cubs the deck
  already wins before a six-drop's second combat matters.
