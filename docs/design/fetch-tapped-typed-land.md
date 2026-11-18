# Fetching a tapped typed land (Hedge Maze, Triomes): only when the mana is not needed this turn

Status (2026-11-18): Sea Creatures flag built (`MTG_SEA_FETCH_MAZE`, default OFF), A/B measured below. Cyclone's
narrowing built and **ON** (`MTG_CYCLONE_LOUNGE_IDLE`, USER-adopted). Five Colour's width-2 Triome built and **OFF**
(`MTG_FIVECOLOUR_TRIOME_BESIDE`, USER "give it a try"; adoption is the USER's call). Both measured below.

## The USER's rule (2026-11-17, verbatim)

- "Hedge maze is a maybe if we absolutely do not need it for mana. We can discuss more tomorrow."
- "(if we do not need it for mana this turn it is possible, otherwise not)"
- "Honestly, Triomes have a similar disposition where they appear."

Earlier statements of the same rule for Five Colour: "we probably should have a way to get triomes when we can
determine there is no need for the land to enter untapped" (2026-08-18), and "if there is a spell that requires
the land we should always go for untapped ... triomes are STRICTLY for times when you cannot use the mana"
(2026-08-19). For Unpredictable Cyclone: "Arguably the triome should be fetched if we don't need the mana"
(2026-10-27).

USER rulings on Cyclone and Five Colour (2026-11-17, verbatim):
- On narrowing Cyclone's Xander's Lounge to "only when the mana is not needed": "The Xander's Lounge change sounds
  good."
- On Five Colour offering the Triome BESIDE the untapped pick instead of as the only target: "On Five Colour we could
  give it a try. I'm not sure what the implications are right now. Almost always the Triome is the call if you don't
  use the mana, but maybe sometimes we will need it for a subsequent step?"

## Where each deck stands

| Deck | Tapped typed land | Today |
|---|---|---|
| Five Colour | Zagoth Triome, Jetmir's Garden | Already applies the rule. `FiveColourProvider::FetchCandidates` RULE 6: a land's "untapped" preference counts only while its mana is *spendable* this turn. A fetch whose mana is spendable ranks an untapped land first; otherwise breadth decides and a Triome wins on its third colour. The search sees only the top entry. |
| Unpredictable Cyclone | Xander's Lounge | Applies the test since 2026-11-18 (`MTG_CYCLONE_LOUNGE_IDLE`, default ON): the Lounge is searched beside the Tarn rule's untapped pick only when the pick's mana is not needed. `=0` restores the old shape (the Lounge beside every pick). |
| Sea Creatures | Hedge Maze | Was last in the one-option ladder (Pool > Island > Forest > Maze), so it was fetched only when nothing else was left. `MTG_SEA_FETCH_MAZE=1` applies the rule (below). |

## "Needed this turn": the test

This is Five Colour's RULE 6 test. Sea and Cyclone share one implementation, `FetchManaNeededThisTurn`
(`src/ai/DecisionProviders.cpp`); Five Colour computes the same union per colour inside its ranking. It is a YES
when either:

- **(a) Solo:** a hand card can't be cast from the current untapped pool, but could be with one more mana of a
  colour the untapped pick makes.
- **(b) Sequence:** that one more mana casts one more hand card in sequence (cheapest first).

Either YES means the mana is needed, and only the untapped land is offered. Otherwise the tapped land is
**offered beside** the untapped pick as a second searched option. It never replaces it, so the search still picks
between the two (searched-with-heuristics; never a forced pick).

What the test does not see:
- A land Aura's bonus mana on the fetched land itself.
- Activations that cost mana.

Both would only make "needed" true more often, so the error is on the side of fetching the untapped land.

## What a tapped typed land buys when the mana is idle

- **Surveil 1** (Hedge Maze) or a third colour (Triomes).
- **A library with one fewer tapped land.** A Maze or Triome drawn later costs a tempo; fetching it on a turn when
  the mana was idle removes that cost for free.
- It is still a Forest/Island (Sea), so it hosts a land Aura later.

## Sea Creatures implementation (`MTG_SEA_FETCH_MAZE`, HeuristicArm slot `SEA_FETCH_MAZE`)

`SeaCreaturesProvider` changes:
- `LandSearchFetchPickOrder` moves an admitted Maze to second place.
- `LandSearchFetchPickWidth` returns 2 when Maze is admitted.
- The legacy route `FetchSearchCandidates` returns {top, Maze}.
- `OnPlayFetchWidth` caps at 2.
- `LandSearchPickOrderKey` bits 8–11 carry the flag and the "needed" verdict for each possible top pick.

The flag only matters under `MTG_SEA_FETCH_PRUNE` (default ON). It is tested in
`test/unit/test_sea_creatures_mana.cpp` ("Sea fetch Maze").

## Measurement (2026-11-17)

Held-out paired A/B, Sea Creatures at d5 b20, `MTG_SEA_FETCH_MAZE=1` vs off, one pooled batch:

| Set | Games | On | Off | Delta (turns) | se | Faster | Slower |
|---|---|---|---|---|---|---|---|
| Standard (seed base 9400001) | 1000 | 5.8220 | 5.8270 | -0.0050 | 0.0022 | 5 | 0 |
| 2HG, 30 life, 2 heads (base 9500001) | 300 | 6.3567 | 6.3600 | -0.0033 | 0.0033 | 1 | 0 |

Six games change, each by exactly one turn faster (std gi 223, 383, 814, 935, 989; 2HG gi 183). No game is
slower. Search units per game are within 1% (std 1.1076 on vs 1.1186 off). The flag is rarely live, because it
needs a fetch, an untapped pick whose mana is idle that turn, and a Hedge Maze still in the library. See also
the Sea Creatures ledger (`docs/design/analysis-Sea Creatures.md`, "Hedge Maze fetch").

## Cyclone implementation (`MTG_CYCLONE_LOUNGE_IDLE`, HeuristicArm slot `CYCLONE_LOUNGE_IDLE`, default ON)

`CycloneProvider::FetchSearchCandidates` (the Scalding Tarn route; Cyclone's Tarn always takes the legacy immediate
route) keeps the rule's untapped pick and adds Xander's Lounge only when `FetchManaNeededThisTurn` says the pick's
colours are not needed. Two Cyclone-specific inputs to the test:
- **Mana cycling costs count as a use** (Curator {U}, Cyclone {2}; `EffectiveCyclingCost`). In the solo half either
  cost unlocking counts. In the sequence half each card counts once, at the cheaper of its cast and cycling cost.
- **`goldfish_inert` cards are never cast** (Toxic Deluge).

Sea calls the same helper with cast costs only, so Sea's play is byte-identical. Unit test: `test/unit/
test_cyclone_phase5.cpp`, "Cyclone MTG_CYCLONE_LOUNGE_IDLE".

## Five Colour implementation (`MTG_FIVECOLOUR_TRIOME_BESIDE`, HeuristicArm slot `FIVECOLOUR_TRIOME_BESIDE`, default OFF)

When RULE 6 ranks an enters-tapped land first (Zagoth Triome, Jetmir's Garden):
- `FetchCandidates` moves the best untapped pick (the first untapped land in its ranking) to second place. The top
  pick does not change, so a rollout's one-option fetch does not change.
- `FetchSearchCandidates` returns {tapped top, untapped second}. This is the legacy Scalding Tarn route, and the
  width of the battlefield fetches' activation (`FetchPicksWidthByProvider`).
- `OnPlayFetchWidth` caps the land drop at 2 (1 when the flag is off, as before).

Off, every route is unchanged (top-1). Unit test: `test/unit/test_boars_fetch_rank.cpp`, "Five Colour
MTG_FIVECOLOUR_TRIOME_BESIDE".

## Measurement: Cyclone and Five Colour (2026-11-18)

Held-out paired A/B, each flag ON vs OFF, one pooled `mtg --batch` (208 jobs, 24/24 workers). Play settings come
from each deck's `value_play`: Cyclone d6 b20, Five Colour d7 b40. 2HG means 30 life and 2 heads. Units are the
search units of all games in the cell, ON / OFF.

| Deck, set (seed base) | Games | On | Off | Delta | se | Faster | Slower | Units on/off |
|---|---|---|---|---|---|---|---|---|
| Cyclone std (10100001) | 1000 | 4.7340 | 4.7440 | -0.0100 | 0.0063 | 23 | 14 | 0.961 |
| Cyclone 2HG (10200001) | 300 | 4.8000 | 4.8100 | -0.0100 | 0.0111 | 7 | 4 | 0.944 |
| Five Colour std (10300001) | 1000 | 4.7480 | 4.7780 | **-0.0300** | 0.0056 | 31 | 1 | 1.244 |
| Five Colour 2HG (10400001) | 300 | 5.2967 | 5.3133 | -0.0167 | 0.0110 | 8 | 3 | 1.300 |

**Non-clairvoyant bracket.** The same manifest under `MTG_NO_SEARCH_SHUFFLE=1`, which turns off the post-fetch
shuffle. That gives a different game, but no fetch target can buy a re-rolled library.

| Deck, set | Games | On | Off | Delta | se | Faster | Slower | Units on/off |
|---|---|---|---|---|---|---|---|---|
| Cyclone std | 1000 | 5.5510 | 5.5490 | +0.0020 | 0.0058 | 15 | 16 | 0.965 |
| Cyclone 2HG | 300 | 5.6267 | 5.6233 | +0.0033 | 0.0111 | 3 | 5 | 0.979 |
| Five Colour std | 1000 | 4.9740 | 4.9880 | **-0.0140** | 0.0037 | 14 | 0 | 1.332 |
| Five Colour 2HG | 300 | 5.4933 | 5.5167 | -0.0233 | 0.0099 | 8 | 1 | 1.172 |

Reading:
- **Cyclone:** slightly faster in the default game (-0.010 in both sets, about 1.6 se in std) and neutral in the
  bracket. About 4% fewer search units. Nothing points to a loss.
- **Five Colour:** a real gain. It survives the bracket at about half its size (std -0.030 -> -0.014, t 3.8, 14
  faster and 0 slower). It costs 1.17-1.33x search units, on the suite's costliest deck per game. The other half of
  the default-game gain is draw luck from the post-fetch shuffle (see the per-game table below).

### Cyclone: the 18 slower games (14 std, 4 2HG), root-caused

- **9 are search noise / look-ahead, not the Lounge.** The OFF arm never fetched the Lounge where ON could not, or
  both arms had the same options. The early split is usually a T1/T2 cantrip or land choice, then a different
  shuffle. std 112, 158, 337, 632, 877, 972; 2HG 58, 124, 139. In 2HG 124 the ON arm made no land drop on T1 while
  holding Steam Vents, an Island and two Tarns, then discarded Emrakul in cleanup. That looks like a search defect
  unrelated to this flag, and is worth its own look.
- **4 are shuffle luck after a correct "needed".** ON spent the extra mana on a cantrip; OFF took the Lounge and drew
  better. std 76, 260, 553; 2HG 89.
- **5 show the Lounge was really worth more, and the test over-fired.** The extra mana could wait, or went somewhere
  it should not have. std 395, 397 (mixed with look-ahead), 649, 663, 894. The triggers:
  - a mana cycling cost before Cyclone is out (649 cycled Curator early and had nothing to cycle once Cyclone landed);
  - Seething Song alone (663);
  - a 1-mana cantrip that could wait a turn (395, 894).

**Proposed amendment (not built; the USER's call):**
1. Count a mana cycling cost only while Unpredictable Cyclone is on the battlefield. Before that, cycling Curator or
   a Cyclone is a cantrip the search usually defers. This flips 112, 649, 2HG 89 and part of 663.
2. Count Seething Song only when its net mana pays for another hand card (663).
3. Lower confidence: do not count a 1-mana cantrip while an untapped land is still in hand for next turn. This flips
   395, 649, 894 and 397 toward the Lounge, but also 76, 260 and 553, where the mana was used and lost only on luck.

The faster games checked (std 113, 424, 640) are mostly search or shuffle variance. In 113 and 640 the narrowing
made the correct untapped pick on a turn where Seething Song into Cyclone needed every mana.

### Five Colour: the faster games and the "subsequent step"

There are 39 faster games (31 std, 8 2HG). **17 real subsequent steps** used the untapped land's mana: 12 std, 5 2HG.
**20 are draw luck** from fetching at a different time or for a different target (17 std, 3 2HG). In 2 the search
rippled with the same draws and no untapped mana was used. In every real step RULE 6 had ranked the tapped land
first, with no colour unlocked (checked with `MTG_FETCHRANK=1`). Every one of them is a gap in RULE 6's own
"needed" test, not a step that was invisible at fetch time:

| Mechanism | Games |
|---|---|
| **Deathrite fuel from the cracked fetch itself.** The pool is counted before the fetchland reaches the graveyard, so a Deathrite with no land in the graveyard counts 0 mana. | std 69, 447, 662; 2HG 94 |
| **Colour-blind pool.** `AvailableManaPool` counts every multicolour land and domain dork (Bloom Tender, Faeburrow Elder, Birds) as wild, so a missing colour looks available. | std 318, 818; 2HG 232 |
| **Domain growth within the turn.** A coloured permanent cast first raises Bloom Tender's or Faeburrow's yield, so the board makes more mana than RULE 6 counts. | std 22, 933 (318 too); 2HG 299, 115 |
| **Lightning Greaves haste.** A Faeburrow cast this turn taps for mana the same turn. | std 72, 396 |
| **Cheapest-first count.** It cannot see a bigger spell replacing a cheaper one: 2 cards either way. | std 105, 453 |
| **An untapped land frees a mana creature to attack** (the extra damage was exact lethal). | std 440; 2HG 115 |
| **A card drawn mid-turn** by Two-Headed Hellkite's attack trigger (Unite the Coalition, paid with the untapped Steam Vents). | 2HG 258 |

Worked example, std gi 22 (5 -> 4): T3 Wooded Foothills. OFF fetched Zagoth Triome; ON fetched Blood Crypt and cast
Faeburrow Elder. That added W to Bloom Tender's colours, so Tender paid for a second Bloom Tender, and T4 cast Cosmic
Spider-Man and Unite the Coalition. Both arms drew the same cards.

Slower games: std 218 is draw luck from a different T1 line. 2HG 66, 127 and 271 are budget dilution: all three come
back at 4x budget (`--budget-ms 160`). With the flag on, games cost about 1.7-2x the search work. The T1 flips come
from the widened fetch nodes deeper in the tree diluting the root's budget. ON's T1 Steam Vents without a Deathrite,
in 127 and 271, is a choice open to both arms.

**Alternative the USER may prefer (not built):** keep width 1 and sharpen RULE 6's test. This would catch 11 of the
12 std real steps, without the 1.2-1.3x unit cost:
1. Treat the cracked fetchland as Deathrite fuel.
2. Make the solo test colour-exact: each source's real colour set, one pip per source, instead of wild.
3. In the sequence test, raise a domain dork's yield when a hand permanent adds a colour, and count a mana creature
   cast this turn as a source while Greaves is out.
4. Compare the best total mana value castable, not the card count.

Std 440 (attack instead of tap) and 2HG 258 (mid-turn draw) are caught only by width 2. RULE 6 is the USER's
doctrine, so this is a proposal only.

### Scoped regression (`--both --deck=cyclone,cyclone2hg,fivecolour,fivecolour2hg`, 22 rows)

Three runs on one binary:
- **OFF/OFF:** `MTG_CYCLONE_LOUNGE_IDLE=0`.
- **Default:** Cyclone ON, Five Colour OFF.
- **ON/ON:** `MTG_FIVECOLOUR_TRIOME_BESIDE=1` as well.

Results:
- Many rows already fail against GT in OFF/OFF. This is upstream drift on `origin/phase-1-2-deck-analyzer`
  (af685ee3e), not from this change.
- **Default vs OFF/OFF:** every Five Colour row is byte-identical. All 9 searched Cyclone rows move (the d0 row does
  not):
  - smoke: d3 4.96 = 4.96, d5 4.79 -> 4.76, 2HG d3 4.92 = 4.92;
  - regression: d3 s2002 4.7867 -> 4.8000, d3 s3003 4.9067 -> 4.9200, d5 s2002 4.7400 -> 4.7000, d5 s3003 4.7467 ->
    4.7600;
  - the unbudgeted backstop row `cyclone_regression_d5_s30031` played 2 -> 3 games: one abandoned game now completes.
- **ON/ON vs default:** all 10 searched Five Colour rows move. Every changed average is faster: smoke d3 4.80 ->
  4.76, d5 4.784 -> 4.768; regression d5 s2002 4.82 -> 4.78; 2HG regression 5.333 -> 5.267.

## Open questions for the USER

4. **Five Colour:** adopt `MTG_FIVECOLOUR_TRIOME_BESIDE` (a real -0.014 to -0.030 turns at 1.2-1.3x search units),
   or sharpen RULE 6's own test instead (the four amendments above, width 1)?
5. **Cyclone:** build the amendment to the "needed" test (cycling counts only with Cyclone out; Seething Song only
   when its mana casts something more)?

## Open questions for the USER (Sea, earlier)

1. **Sea:** adopt `MTG_SEA_FETCH_MAZE` as default ON?
2. ~~Cyclone: narrow the Lounge?~~ Adopted by the USER 2026-11-17, built ON (above).
3. ~~Five Colour: offer the Triome beside the untapped pick?~~ USER "give it a try"; built OFF and measured (above,
   question 4).
