# Fetching a tapped typed land (Hedge Maze, Triomes): only when the mana is not needed this turn

Status (2026-11-18, round 2): Sea Creatures' Maze fetch is ADOPTED (`MTG_SEA_FETCH_MAZE`, default ON).
- Cyclone: the Lounge narrowing is ON (`MTG_CYCLONE_LOUNGE_IDLE`, USER-adopted). Its round-2 amendment is ON as well
  (`MTG_CYCLONE_LOUNGE_IDLE_AMEND`, a refinement of that rule).
- Five Colour has two OFF options, and adoption is the USER's call:
  - the width-2 Triome (`MTG_FIVECOLOUR_TRIOME_BESIDE`);
  - the sharpened width-1 RULE 6 test (`MTG_FIVECOLOUR_RULE6_SEES_STEPS`).

Round 2 is at the end of this file.

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

4. **Five Colour:** which, if either, to adopt? The round-2 numbers are below.
   - `MTG_FIVECOLOUR_RULE6_SEES_STEPS`: width 1, about 1.0x search units. Std -0.018, 19 faster / 1 slower.
     2HG -0.017, 5 faster / 0 slower.
   - `MTG_FIVECOLOUR_TRIOME_BESIDE`: width 2, 1.24-1.30x units. Std -0.030, 31/1. 2HG -0.017, 8/3.
   - Both together: plays exactly as BESIDE.
   - No-shuffle bracket: BESIDE std -0.014 vs RULE6 -0.009; 2HG -0.023 vs -0.010. BESIDE keeps a real edge at
     1.17-1.33x units. The agent leans to RULE6_SEES_STEPS; see "Recommendation".
5. ~~Cyclone: build the amendment?~~ Built ON in round 2 (`MTG_CYCLONE_LOUNGE_IDLE_AMEND`). It measures neutral:
   std +0.000, 2HG -0.003. `=0` restores the round-1 test.

## Open questions for the USER (Sea, earlier)

1. ~~Sea: adopt `MTG_SEA_FETCH_MAZE` as default ON?~~ Adopted by the USER ("3 sounds good to adopt."), ON.
2. ~~Cyclone: narrow the Lounge?~~ Adopted by the USER 2026-11-17, built ON (above).
3. ~~Five Colour: offer the Triome beside the untapped pick?~~ USER "give it a try"; built OFF and measured (above,
   question 4).

## Round 2 (2026-11-18): the Cyclone amendment, Five Colour RULE 6 sharpened, one pooled A/B

### Cyclone amendment (`MTG_CYCLONE_LOUNGE_IDLE_AMEND`, HeuristicArm slot `CYCLONE_LOUNGE_IDLE_AMEND`, default ON)

It is part of `MTG_CYCLONE_LOUNGE_IDLE` and is read only under it. It refines the "needed" test that the USER
adopted. `=0` restores the round-1 test. Two changes in `FetchManaNeededThisTurn`, as two new `FetchNeedScan` fields
that only Cyclone sets (Sea sets neither, so Sea is byte-identical by construction):
- `cycling_live`: a mana cycling cost counts as a use only while an Unpredictable Cyclone
  (`cycle_draw_exile_shared_type_freecast`) is on our battlefield.
- `ritual_payoff`: a ritual (`ritual_floating_mana`, Seething Song) is never counted by itself. Each pool is tested
  with and without the hand's rituals cast first, so the Song counts only when its RRRRR pays for another hand card.

Unit tests: `test/unit/test_cyclone_phase5.cpp`, "Cyclone MTG_CYCLONE_LOUNGE_IDLE" (cycling with and without a
Cyclone out, the Song alone, the Song + Cyclone, and the amendment off).

**The one-mana cantrip that "could wait" (not built; judgement).** Leave it alone. Whether Brainstorm or Ponder
can wait a turn depends on next turn's mana and draws, which is a projection the test cannot make. The evidence
points both ways. In round 1, 395 and 894 wanted the Lounge; 76, 260 and 553 used the mana and lost only on luck.
Round 2 adds 649, where the remaining loss is a T1 Brainstorm. The OFF arm took the Lounge on T1 instead, so this
is the cantrip class too, not the cycling class (the amendment did fix 649's T2 cycling read). And 832 (below) lost
because the extra mana went UNUSED by a Brainstorm draw. A cantrip rule would also have to model what the cantrip
draws. There is no clean rule here. The search already branches on the Lounge whenever the test says "idle".

### Five Colour RULE 6 sharpened (`MTG_FIVECOLOUR_RULE6_SEES_STEPS`, slot `FIVECOLOUR_RULE6_SEES_STEPS`, default OFF)

This is the width-1 alternative from round 1. `FiveColourRule6Steps` (in `src/ai/DecisionProviders.cpp`) asks, per
colour, whether one more mana of that colour lets this turn spend more. "Spend more" means either of:
- a single hand card castable only with it (colour-exact);
- a larger best TOTAL MANA VALUE over every castable subset of the hand (at most 10 cards, so at most 1024 subsets).

The model:
- **(1) Deathrite fuel.** An untapped Deathrite with no fuel gets the cracked fetchland as fuel: one more wild mana.
- **(2) Colour-exact.** Payment is re-checked with Hall's condition over the board's real colour sets
  (`BuildColorFeasibility` / `PayablePips`, with `WithDomainOpen` at the board's domain). Multicolour lands and
  domain dorks are no longer wild.
- **(3) Domain growth.** A coloured permanent in the subset adds its new colours to every untapped domain dork:
  generic supply for all, and a pip for another card (the `DomainOpenFor` rule).
- **(4) Greaves.** With Lightning Greaves out, a mana creature in the subset taps the same turn (Birds 1, a domain
  dork its domain).
- (3) and (4) count only when the permanents bringing new colours and the hasted dorks fit the supply that is not
  a domain dork's, since they must be paid before the dorks tap.
- **(5) Best total mana value.** The subset search replaces the cheapest-first count.

The result is OR-ed into RULE 6's `any_unlocks`, so it only ever adds untapped preference. The ranking still returns
one target. Unit tests: `test/unit/test_boars_fetch_rank.cpp`, "Five Colour MTG_FIVECOLOUR_RULE6_SEES_STEPS". There is
one SUBCASE per blind spot. Each hand reads idle to the old test (a triome is the top pick) and needed to the new one
(an untapped land is the top pick), plus an idle-stays-idle case.

### Measurement (one pooled `mtg --batch`, 364 jobs, 24/24 workers; same seeds and play settings as round 1)

Cyclone arms: `off` (`LOUNGE_IDLE=0`), `cur` (`LOUNGE_IDLE=1`, `AMEND=0`, the round-1 ON), `amd` (both on, the new
default). Five Colour arms: `off`, `bes` (`TRIOME_BESIDE`), `stp` (`RULE6_SEES_STEPS`), `both`. The `cur`-vs-`off` and
`bes`-vs-`off` rows reproduce round 1 game for game, so the rebase moved nothing in either deck. Delta = A - B
(negative = A faster). Faster/slower count games where A beat B / lost to B.

| Deck, set | A vs B | Games | A | B | Delta | se | Faster | Slower | Units A/B | Wall A/B |
|---|---|---|---|---|---|---|---|---|---|---|
| Cyclone std | amd vs off | 1000 | 4.7340 | 4.7440 | -0.0100 | 0.0057 | 19 | 10 | 0.966 | 0.957 |
| Cyclone std | cur vs off | 1000 | 4.7340 | 4.7440 | -0.0100 | 0.0063 | 23 | 14 | 0.961 | 0.951 |
| Cyclone std | **amd vs cur** | 1000 | 4.7340 | 4.7340 | +0.0000 | 0.0035 | 6 | 6 | 1.005 | 1.006 |
| Cyclone 2HG | amd vs off | 300 | 4.7967 | 4.8100 | -0.0133 | 0.0094 | 6 | 2 | 0.955 | 0.913 |
| Cyclone 2HG | cur vs off | 300 | 4.8000 | 4.8100 | -0.0100 | 0.0111 | 7 | 4 | 0.944 | 0.910 |
| Cyclone 2HG | **amd vs cur** | 300 | 4.7967 | 4.8000 | -0.0033 | 0.0058 | 2 | 1 | 1.011 | 1.003 |
| Five Colour std | bes vs off | 1000 | 4.7480 | 4.7780 | -0.0300 | 0.0056 | 31 | 1 | 1.244 | 1.237 |
| Five Colour std | **stp vs off** | 1000 | 4.7600 | 4.7780 | **-0.0180** | 0.0044 | 19 | 1 | **0.991** | 1.024* |
| Five Colour std | both vs off | 1000 | 4.7480 | 4.7780 | -0.0300 | 0.0056 | 31 | 1 | 1.184 | 1.179 |
| Five Colour std | stp vs bes | 1000 | 4.7600 | 4.7480 | +0.0120 | 0.0037 | 1 | 13 | 0.797 | - |
| Five Colour 2HG | bes vs off | 300 | 5.2967 | 5.3133 | -0.0167 | 0.0110 | 8 | 3 | 1.300 | 1.306 |
| Five Colour 2HG | **stp vs off** | 300 | 5.2967 | 5.3133 | **-0.0167** | 0.0074 | 5 | 0 | **1.020** | 1.020* |
| Five Colour 2HG | stp vs bes | 300 | 5.2967 | 5.2967 | +0.0000 | 0.0082 | 3 | 3 | 0.785 | - |
| Five Colour 2HG | both vs off | 300 | 5.2967 | 5.3133 | -0.0167 | 0.0110 | 8 | 3 | 1.272 | 1.245 |

The `stp` rows are the FINAL version of the RULE 6 test (commit `634d3023f`), measured in a second pooled batch on
the same seeds (`manifest2.json`: off / stp / both). Its `off` arm is byte-identical, game for game, to the first
batch's `off`. The first version (`31d3aeb4f`) measured std -0.0170 (se 0.0043, 18/1, units 0.993) and 2HG -0.0167
(5/0). The final version adds std 72 (Faeburrow hasted by Greaves into Mana Cannons). The first version forced the
red Cannons to be paid before the hasted dork, though no domain source was waiting on its colour. Wall ratios marked
* come from the second batch, which ran on a contended box; units are the reliable cost measure.

RULE 6 now catches 10 of the 12 standard real steps from round 1: 22, 69, 72, 105, 318, 396, 447, 453, 662, 818.
It misses 933 (domain growth whose order the model rejects) and 440 (an untapped land freeing a creature to attack;
only width 2 can see that). It also gains 50, 186, 484, 559, 624, 637, 689, 712 and 979. In 2HG it gains 69, 77,
115, 232 and 258.

The one slower RULE 6 game, std 849, is search noise. The fetch targets are identical in both arms through T2. The
split is the T2 cast (Bloom Tender versus Mana Cannons), and both arms win T5 at 4x budget (`--budget-ms 160`).

`both` plays every game exactly as `bes` does (0 faster, 0 slower against it). With the untapped land already
searched beside the triome, re-ranking which of the two is on top changes the search order (units 0.95-0.98x of
`bes`) but not one outcome.

### Cyclone: what the amendment moved (amd vs cur)

- **Faster (std 6, 2HG 2):** std 107, 112, 158, 489, 663, 877; 2HG 89, 124. 663 is the Seething Song game predicted
  in round 1. 112, 158 and 2HG 89 were round-1 slower games.
- **Slower (std 6, 2HG 1):** std 210, 212, 352, 621, 825, 832; 2HG 23. Each was replayed in both arms
  (`triage.sh`: first divergence, boards, both arms at 4x budget, `--budget-ms 80`).
  - **Six are search and shuffle noise.** At 4x budget the two arms tie. The first divergence is a root choice with
    the same options in both arms:
    - Tarn versus a plain land on T1 (210);
    - Ponder shuffle-or-keep (212);
    - the turn-2 or turn-3 land choice (352, 621, 825);
    - a Brainsurge-or-discard line (2HG 23).
    Fetch targets are identical up to that point. The draws then diverge.
  - **One is real: std 832, and it is not fixable by this test.** T2: Island out; hand Brainstorm, Tarn, Cascade
    Bluffs, Cyclone, Brainsurge. Amended: the Cyclone cycling cost no longer counts (no Cyclone out), so the extra
    mana reads idle. The search, offered both, took the Lounge and cast Brainstorm. The round-1 test called the mana
    "needed" for the wrong reason (cycling with no Cyclone out) and played Cascade Bluffs. Its Brainstorm then drew
    Ponder, which the second mana cast. This is the mid-turn-draw class (Five Colour's (7)): the extra mana is spent
    on a card drawn this turn. It stays slower at 4x budget.
- **649 is still slower than OFF.** Its loss is a T1 Brainstorm: the rule's pick, where OFF took the Lounge on T1.
  That is the cantrip-could-wait class, not cycling.
- **Net:** standard exactly neutral (+0.0000, se 0.0035), 2HG -0.0033, units 1.005-1.011x. The amendment removes
  two counting errors without changing the deck's result.

### 2HG game 124: the missing T1 land drop

Replayed with the round-1 binary at `LOUNGE_IDLE=1`, with `MTG_FD_TRACE=1 MTG_TRACE=search`. T1 hand: Steam Vents,
two Scalding Tarns, two Emrakuls, Cyclone, Ponder, Island (8 cards, on the draw).
- At the shipped b20, all three T1 ladder passes are "win=9" (no win inside the horizon). The committed main-1 plan
  is `land=(defer)` with no casts. Cyclone plays no main 2, so the drop is simply not made. Cleanup discards Emrakul,
  whose trigger shuffles the graveyard into the library.
- At `--budget-ms 40` and `80`, the identical ladder (same pass costs) commits Steam Vents + Ponder and wins T4. So
  do OFF and `MTG_NO_SEARCH_SHUFFLE=1`.

Verdict: **not a defect of the flag, and not a small engine bug.** It is a beyond-horizon tie between no-win plans
(both estimate T5). It is broken by budget-dependent work after the ladder. The no-land plan's estimate rides a
search-world re-shuffle: holding 8 cards forces the Emrakul discard, and the discard shuffles the library. That is
the in-search shuffle clairvoyance class.

The no-land plan is not soundly dominated. Hand size feeds discard, and Emrakul's discard is a real shuffle. So it
cannot be folded away, and nothing was changed. Worth recording for whoever next touches no-win tie-breaking: a
no-land main 1 in a deck with no main 2 can win a tie only through a discard effect.

### Non-clairvoyant bracket (round 2, `MTG_NO_SEARCH_SHUFFLE=1`, all four Five Colour arms, same seeds)

| Set | A vs B | Delta | se | Faster | Slower | Units A/B |
|---|---|---|---|---|---|---|
| std | bes vs off | -0.0140 | 0.0037 | 14 | 0 | 1.332 |
| std | stp vs off | -0.0090 | 0.0033 | 10 | 1 | 0.994 |
| std | both vs off | -0.0130 | 0.0039 | 14 | 1 | 1.269 |
| std | stp vs bes | +0.0050 | 0.0022 | 0 | 5 | 0.746 |
| 2HG | bes vs off | -0.0233 | 0.0099 | 8 | 1 | 1.172 |
| 2HG | stp vs off | -0.0100 | 0.0058 | 3 | 0 | 1.012 |
| 2HG | both vs off | -0.0267 | 0.0093 | 8 | 0 | 1.158 |
| 2HG | stp vs bes | +0.0133 | 0.0081 | 1 | 5 | 0.864 |

BESIDE's standard gain shrinks from -0.030 to -0.014, the same as in round 1. RULE 6's shrinks from -0.018 to
-0.009. So in the game that no fetch target can win on luck, RULE 6 delivers about 65% of BESIDE's standard gain and
43% of its 2HG gain. BESIDE keeps a REAL residual edge over it: std -0.005 (t 2.3, 5 games, 0 the other way) and
2HG -0.013 (t 1.6). These are steps the one-target test cannot see. RULE 6's one slower no-shuffle game, std 896,
is library composition. RULE 6 plays the textbook turn 1 (Tarn into Breeding Pool, Birds; Cannons on T2). Fetching
the Pool rather than playing Blood Crypt changes which land is left in the library, and the T4 draw differs
(Two-Headed Hellkite instead of Breeding Pool). It does not recover at 4x budget because the draws are
deterministic, not because the search misses anything.

### Recommendation (agent's; the decision is the USER's)

There is a genuine trade, and the numbers above are the input:
- **`MTG_FIVECOLOUR_RULE6_SEES_STEPS` (width 1):** about 1.0x search units. It keeps "triomes are STRICTLY for
  times when you cannot use the mana" as a one-target rule. Default game: std -0.018, 2HG -0.017. No-shuffle
  bracket: std -0.009, 2HG -0.010.
- **`MTG_FIVECOLOUR_TRIOME_BESIDE` (width 2, or both flags):** 1.17-1.33x search units on the suite's costliest deck
  per game, for a further real std -0.005 and 2HG -0.013 in the bracket. The search, not the rule, decides.

The agent's lean: RULE6_SEES_STEPS. It is free and stays inside the USER's doctrine. BESIDE's extra ~0.005-0.013
turns costs about 25-30% more search on every Five Colour game. If the USER prefers the larger gain, "both" is the
form to adopt: it plays like BESIDE, at slightly fewer units (0.95-0.98x of BESIDE). It also keeps the more accurate
untapped-first order, which rollouts use.
