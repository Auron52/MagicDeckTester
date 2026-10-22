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
