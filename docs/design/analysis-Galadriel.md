# Analysis ledger — Galadriel

Deck: `decks/Galadriel/Galadriel.cod` — 4 Galadriel of Lothlórien, 2 Crackle with Power, 1 Sol Ring,
and 53 lands: 35 Temples (scry lands, enter tapped), 4 Crystal Grotto, 4 Rumble Arena, 4 Zhalfirin
Void, 2 The Grey Havens (untapped scry lands) and 4 Saprazzan Skerry (no scry). The engine of the deck
is Galadriel's "whenever you scry, reveal the top card; a land goes onto the battlefield tapped": with
her out, one land drop chains through the library. The finisher is Crackle with Power (X = 4 for 20 is
14 mana), plus Galadriel's 3/3 attacks.

Analysis started 2026-10-17 in the primary checkout, branch `phase-1-2-deck-analyzer`.

## Standing decisions (pass to every subagent)
- Goldfish: the opponent never casts spells or blocks. Model the full card. Goldfish-inert deferrals
  stay PROVISIONAL until the user signs off.
- No greedy pick inside the search window (user hard rule). The Galadriel chain is SEARCHED as one
  skip-count policy per chain (see Stage 2). Its two dominance collapses are PROVISIONAL.
- Cast order / range / main-split / mulligan / discard buckets are USER-OWNED: author and present,
  never adopt unilaterally.
- One heavy job (build or batch) at a time.

## Stage 1 — coverage (2026-10-17)
Missing (13): Galadriel of Lothlórien, Crystal Grotto, Temple of Abandon / Deceit / Enlightenment /
Malady / Malice / Mystery / Plenty / Triumph, The Grey Havens, Zhalfirin Void, Rumble Arena.
Present (4): Crackle with Power (Hinata), Temple of Epiphany (Treasure Hunt), Sol Ring, Saprazzan
Skerry. No sideboard. Routed to `GenericProvider` before this analysis.

## Stage 2 — cards

| Card | Tier | Model | Deferrals (status) |
|---|---|---|---|
| 8 new Temples | 1 | `basic_land`, two colours, `enters_tapped`, `etb_scry 1` (the Temple of Epiphany shape) | none |
| Zhalfirin Void | 1 | untapped {C}, `etb_scry 1` | none |
| Crystal Grotto, Rumble Arena | 1 | `any_color_filter` (the Capital City shape) + `etb_scry 1`; Rumble Arena's Vigilance is on a land that is never a creature (inert, carried) | none |
| The Grey Havens | 2 | {C}, `etb_scry 1`, Legendary; NEW `mana_colors_of_graveyard_legends`: `EffectiveProduces` adds every colour of a legendary creature card in our graveyard (`GraveyardLegendColors`), and the four payment paths that inline their own colour logic were taught it (the DFS payer, its pruning bound, the payment memo hash, the source colour cap) | none |
| Galadriel of Lothlórien | 3 | NEW `scry_reveal_land_to_battlefield`: `FireScryWatchers` at the tail of `ScryTop` (the one scry chokepoint, both worlds); a revealed land enters tapped through `EnterLand` and its own ETB scry re-enters `ScryTop` (the chain) | Ring-tempt scry 3: deferral APPROVED by the user 2026-10-17 (no card in the pool tempts) |

### The Galadriel chain: how it is searched
- Every look in a chain is a choice, so a chain is ONE searched decision. Two PROVISIONAL dominance
  collapses for the autonomous search: (a) a scry-land on top is always kept (bottoming it reveals the
  same next card with one fewer land and no look at it); (b) "you may reveal" is always taken.
- What remains is a skip count k: bottom the first k STOP cards (nonlands, and Saprazzan Skerry, which
  ends the chain when taken) and stop at the next. `ScryChainCandidates` lists k = 0..S (S = stop
  cards in the library), the provider default first (`ScryKeepOnTop` walked as a k).
- The pin is the land drop's existing `Plan::scry_choice` (consumed by the first look, which is the
  dropped land's own scry), so the chain rides every site that already installs it. The axis is not
  width-capped for a chain.
- Galadriel counts as a land-position watcher (`PutsLandfallWatcher`, `LandOrderMatters`), so "cast
  Galadriel, then play the land" is enumerated: a land played after her starts a chain.
- Viewer: every look is the normal `scry` decision; a revealed land asks `dig` with source
  `(galadriel reveal)` (default reveal, -1 declines).

## Open questions for the user (work continues on the stated default)
1. ~~Ring-tempt deferral~~ — APPROVED by the user 2026-10-17 ("That's fine on the Ring-tempt clause").
2. PROVISIONAL dominance collapses in the chain (keep scry-lands; always reveal). [default: collapsed
   for the search; the human keeps both choices]

## User direction (2026-10-17)
- "Realistically we will want some smart scry heuristics." → owed: a `GaladrielProvider` scry policy
  (the per-look `ScryKeepOnTop` outside a chain AND the chain's default skip count, candidate 0 of
  `ScryChainCandidates`). It ORDERS only (the search still branches every k), so it is a provider
  default, but it is authored, measured (with/without A/B), and presented to the user before adoption.

## Scry policy — `GaladrielProvider::ScryKeepOnTop` (USER rules, 2026-10-17; `MTG_GALADRIEL_SCRY`, default ON)
A DEFAULT, not a narrowing on its own: the land drop's scry stays searched. Inside a reveal chain the
rule is candidate 0 (walked live, per look); `MTG_GALADRIEL_CHAIN_PRUNE` (default ON pending A/B) prunes
the chain to it alone outside human play.
- **Galadriel out** (USER): "You keep any scry land on top and pitch other things except for Crackle when
  you have enough mana out to finish the opponent next turn." (`GaladrielCrackleLethalNextTurn`.)
- **Galadriel not out** (USER): "Without Galadriel, you want to look for colors, Crackle and acceleration.
  Anything else is thrown unless you are short on lands and when you are really in bad shape for colours
  you might even throw Crackle or acceleration if they don't help solve the problem." Encoded as: colours =
  G, U (Galadriel) and R (Crackle), covered by any land/rock on the battlefield or in hand (a filter covers
  all); a top card covering a missing colour is kept; "bad shape" = G or U missing, and then Crackle and
  acceleration (Sol Ring, Skerry) are thrown unless they cover a missing colour; "short on lands" = under
  three lands on the battlefield + in hand, and then any land is kept -- but only once G and U are covered
  (USER clarification: "you still need to prioritize getting the colours even if short on lands. It is
  only beyond that that you would accept anything"); everything else is thrown.
  USER refinements, same day: colours are Galadriel's ONLY ("You don't need red until you have a chance to
  dump a whole bunch of lands and this almost always provides it"); a filter land does not count as G/U
  ("you want actually green and blue rather than filter lands, since the latter slows you down") UNLESS
  far enough along ("if you are already far enough along in turns the filter land is sufficient" -- the
  agent's threshold is turn >= 4) or the battlefield already makes 3 mana ("since the filter land will be
  drawn next turn").
  AGENT READING (surfaced): a Galadriel on top while none is in hand or on the battlefield is kept.
- BUG FIXED with this change: `x_damage_multiplier` DEFAULTS TO 1, so the first version's "Crackle"
  test (`> 0`) matched every card (every hand card counted as a held Crackle; with Galadriel out a Sol
  Ring / Skerry on top went down the Crackle-lethal path). Now `IsCrackle` = multiplier > 1.

## Turn order — `GaladrielProvider::LandEntrySpotsRule` (USER 2026-10-17; `MTG_GALADRIEL_SPOTS`, default ON)
USER: "the typical order should have Galadriel before lands and Crackle after. It might sound weird, but
this works the best." Galadriel is the deck's only land-entry watcher (`PutsLandfallWatcher` includes
`scry_reveal_land_to_battlefield`), so the Landfall deck's SPOTS rule expresses it: the land right after
her cast (land_slot k=1, payable prefix) takes the preferred list position, the land-first plan stays
searched, and Crackle (dispatched after the land) is sized with the chain's lands in play. Human play keeps
every position. USER on land-first: "there is some argument for trying the land before Galadriel if
mana requires it. But really, all you get is an extra attack or two from her (the only way to start the
combo is to actually play it after she has landed)" -- land-first stays searched (and is the only spot
emitted when her cast needs the land's mana), ranked behind the after-Galadriel spot. NOT YET COVERED: the beyond-horizon playout / depth-0 runner still play the land first.

## A/B — the USER's rules (2026-10-17, 314fa393, seeds 61000+62000 pooled, one 24-job batch, 32/32 busy)
Arms are cumulative: generic (all three levers off) / userscry (MTG_GALADRIEL_SCRY) / pruned
(+ MTG_GALADRIEL_CHAIN_PRUNE) / all (+ MTG_GALADRIEL_SPOTS).

| shape        | generic avg / ms | userscry       | pruned         | all            |
|--------------|------------------|----------------|----------------|----------------|
| d0 (8000)    | 7.9733           | 7.3924         | 7.3924         | 7.3924         |
| d3 b20 (1200)| 5.5275 / 9,709   | 5.5233 / 9,569 | 5.5325 / 7,834 | 5.5316 / 7,690 |
| d5 b40 (600) | 5.5267 / 12,530  | 5.5167 / 12,085| 5.5283 / 10,024| 5.5283 / 9,828 |

Reading: the scry rule is worth **0.58 turns at d0** (the playout policy -- prune and spots do not act at
d0). Searched, every arm is within noise (spread 0.01 t; se of a difference ~0.03-0.05 t); the prune cuts
**19-20% of cost**, and the spots rule another ~2%. Defaults stay ON (all three are the USER's rules).

## Unit tests (2026-10-17)
`test/unit/test_galadriel.cpp` (6 cases: printed data, chain to the first nonland, pinned skip, Skerry
ends / is skipped, no-watcher and gate-clear, Grey Havens colours). Two existing tests updated for the
new gate: `test_landfall.cpp` "no landfall card" also clears `deck_has_scry_watcher`;
`test_pirates_provider.cpp` pins Galadriel -> Galadriel.

## Performance — mana payment (2026-10-17; USER: "We should optimize the mana logic. This deck is not complex in that way.")
Symptom: the first profile run (card scores, 1000 games d5) stuck in a tail at ~2 of 32 cores; perf put
~96% of the time in `TapForCostBacktrackWorker`. Repro: Galadriel d5 b40 seed 2008 gi 6 (the
`--games 48 --seed 2002` gi=6 game).

Diagnosis (new diagnostic `MTG_TAP_STATS=1 MTG_TAP_SLOW_NODES=N`: dumps any top-level payment over N nodes):
every slow payment was Crackle with Power at X=4/5 on a 15-land + Sol Ring board.
1. UNPAYABLE {15}{R}{R}: 8-14 M nodes each. Total mana was exactly 17, but the only native red source
   was one Temple of Triumph; the second {R} needs a filter (Crystal Grotto / Rumble Arena) whose {1}
   feed leaves 16. The flow oracle priced any_color_filter as a FREE rainbow land, so it never proved it.
2. PAYABLE {12}{R}{R} / {15}{R}{R}: ~1.8 M nodes each to FIND the payment -- the oracle runs at the top
   only, so the DFS proved every dead partial tap set by exhausting it.

Fixes (both lossless + byte-identical -- they only cut subtrees that fail; smoke 120/120, play-changed 0):
- `MTG_ACF_EXACT` (default ON): when the gross oracle says feasible and an any_color_filter is present,
  an exact check tries k = 0..F filters in coloured mode -- their colour capped at k through a CONV node,
  generic demand + k for the feeds -- and proves infeasible if no k saturates. Sound: a real payment with
  j conversions is a flow of the k=j graph. The payable path's verdict and hint are untouched.
- `MTG_ACF_INTERIOR` (default ON): the same oracle at interior DFS nodes (reads current tapped flags +
  floating), armed only on a filter board, after 256 nodes of the payment, and never while the
  `MTG_FLOW_ORDER` hint is live.

Measured, gi 6: did not finish in minutes -> 21.1 s (ACF_EXACT) -> 9.6 s (+INTERIOR), same result (T6);
slow-payment nodes 157 M -> 0.87 M. Smoke makespan 79 s -> 70 s (another filter deck got faster).
Not yet done: identical-capability source folding (Grotto == Arena; the dup collapse keys on the
CardDefinition) -- only if a residual payment tail shows up again.

## Stage 4 — baseline profile (2026-10-17, on 48bf0607 + the GALADRIEL_SCRY heurarm slot)
`decks/Galadriel/Galadriel.profile.json`. Card scores: Galadriel 1st copy +2.07 (2nd -0.07, the legend
rule), Skerry +0.72, Sol Ring +0.63, Crackle 1st +0.23 / 2nd -0.82. Cost diagnostic COST_NEUTRAL.
Discard: DISCARD_UNLABELLED -- 0 real sheds, 17,823 rollout sheds / 400 games (13,183 under 4 lands);
owed: the MTG_SHED_WORST bound.
**USER decision (2026-10-17): Galadriel is a REQUIRED PIECE** ("We should probably make Galadriel a
required pieces"; after the user noted "it's not difficult to lose the game if you keep a no-Galadriel
hand"). Applied with min_playable 0 -> 1 (the skill's multi-colour floor); stop_at stays 4.
Measured, seed 63000: d0 4000 games 7.8625 -> 7.8450; d3 b20 300 games 5.5733 -> 5.5567.

## Stage 5j — cost gate: OVER (2026-10-17, first reading)
Budget = 3x pd = 8,085.6 ms/game. Galadriel measured ~9,900 ms/game at d3 b20 (300 games, 30 threads) --
OVER before the d5 case is even measured. Per CLAUDE.md the deck is NOT added to the suite; getting it
into range is now the FIRST goal (ahead of value leaf / mulligan). Post-payment-fix profile is flat
(BuildSimKey ~7%, CollectActions ~4%, the chain's ScryTop/FireScryWatchers/ScryKeepOnTop ~6%).
Next: measure the chain axis' share (MTG_SCRY_SEARCH=0 diagnostic) and the k-variant census.

## Scry rules, round 2 (USER 2026-10-18, from the hand-played references)
- **No Galadriel anywhere** (7e55a090): keep ONLY Galadriel ("you must scry away everything that is not
  Galadriel until you find her. You will get plenty of land just as a matter of course").
- **Galadriel in hand, colours covered, no scry land in hand** (seed 14 reference, T3): keep a SCRY LAND,
  throw everything else -- Crackle, Sol Ring, Skerry included ("if we need a scry land in hand ... and have
  no other needs we should not bottom any scry land. Drawing something else like a Galadriel, Crackle,
  Skerry or Sol Ring in that situation would be really bad"). Every land but Saprazzan Skerry scries.
- **Galadriel out: the last reachable Crackle behind the pile** (seed 10 reference, both Crackles bottomed;
  `MTG_GALADRIEL_CRACKLE_PILE`, default ON). USER: "it is probably okay as long as Crackle is the first card
  bottomed ... if there are other cards you have to wait a turn for each... That is really expensive and
  would generally mean you want to keep the second crackle"; "if both Crackles were condemned into a
  notable pile at the bottom it would be very difficult to extract either". New engine state:
  `Library::KnownBottom()` (cards we bottomed ourselves since the last shuffle -- public information;
  `m_top` went 32-bit so sizeof(Library) stays 32). Rule: no Crackle in hand, the first stop card in the
  known pile is not a Crackle, and no other Crackle in the unknown library above the pile -> keep.
  Measured (paired, seeds 50001/60001, d0 4000 / d3 b10 600 / d5 b20 300):
  | form | d0 | d3 | d5 |
  |---|---|---|---|
  | any stop card in the pile (first reading) | +0.040 +/- 0.007 | -0.010 +/- 0.011 | -0.013 +/- 0.016 |
  | first pile stop not a Crackle | +0.055 +/- 0.005 | +0.017 +/- 0.007 | +0.020 +/- 0.011 |
  | ...and the LAST reachable Crackle (SHIPPED) | +0.001 +/- 0.002 | -0.002 +/- 0.002 | 0 |
  Root cause of the losses (d0 seed 50085, d3 seed 60052): keeping a Crackle ENDS the chain, so the turn's
  remaining ~30-40 lands never enter; with the other Crackle still above the pile, the chain would have met
  it with lethal mana that turn or the next. The shipped form fires only in the USER's own case.
- **Searched keep-a-Crackle chain variants: DELETED.** USER: "I don't think we should generically give that
  option unless it pays off in the numbers." Measured: d3 2/300 games changed (1 better, 1 worse), d0/d5
  identical, +2-12% cost.
- **References**: 15 USER games (seeds 1-15). `ref_bench --deck galadriel`: 0/15 short; search 5.267 vs
  human 5.667 (faster on seeds 5, 7, 10, 15). Seeds 4 and 5 hold two USER scry choices the rules do not
  follow (a second Crackle / a Skerry kept on T5 with 28-40 lands out, game already won): read as
  indifferent, surfaced.
- **Bottoming cost** (pre-keep-table): lookahead bottoming is ~96% of CPU at d3 b10 (60 games: 245 s user
  vs 10 s with MTG_BOTTOM_EVAL_DEPTH=0, avg 5.267 vs 5.283). USER 2026-10-18: "mulligan profile first and
  then value-leaf is pretty much always the right order now" -- the value leaf (stopped at phase C, 533/534
  chunks, pool exited early) is to be REGENERATED after the keep table.

## Mulligan profile (2026-10-18; ORDER: mulligan before the value leaf, USER)
`bash scripts/mullgen.sh run decks/Galadriel` (recipe complete, PGO, src c26cf134, MTG_RSS_CAP_GB=26.2),
03:12-04:30 UTC: settings = the deck's PLAY settings d5 b20 (no screen existed for a no-leaf deck yet --
USER: "I'm not so fond of using play settings ... We should have run the setting test first"; since
1a0c495b `mullgen.sh run` screens once before the first table; this run was let finish, USER-approved).
- Buckets: K=9 (USER cap 13). Discovery merged beyond the USER's four pairs: {Malice, Triumph, Grey Havens,
  Zhalfirin Void} (no G/U), {Deceit, Enlightenment, Epiphany} (blue only), {Abandon, Malady, Plenty} (green
  only), {Grotto, Arena}; Temple of Mystery, Galadriel, Crackle, Sol Ring, Skerry alone. USER: "not sure how
  I feel about those extra merges, but maybe it is okay." 8,615 hands (4,566 size-7), R=40, 128k size-7
  rollouts, ~49 min.
- Validation (`mullgen.sh`): keep (exhaustive vs static) **-0.187 t**, bottoming (exhaustive vs lookahead,
  confounded) **-0.076 t** (4/4 seeds, mean/se -10). ACCEPTED, live.
- GT moved (accepted smoke + regression): smoke d0 7.170 -> 6.958, d3 4.833 -> 4.900 (30 g), d5 4.467 ->
  4.800 (15 g), 2hg = ; regression d0 7.171 -> 6.964, d3 4.675 -> 4.550 / 4.875 -> 4.850, d5 4.800 -> 4.650 /
  5.150 -> 5.200, 2hg 4.725 -> 4.625. `keep_adoption_attribution.py`: every changed game explained (0
  unexplained) once a new class was added -- bottomed-card ORDER reached by play: the reveal chain digs to the
  library's bottom, so the table's bottom order surfaces (smoke 11, regression 6 games).
- Overnight rows NOT resized (the skill's FULL-set step) -- surfaced to the USER as a proposal.

## Value leaf (2026-10-18, after the keep table; `valueleaf.sh run`, frozen e3ba99ef, ~13 min end to end)
- C0: greedy leaf from H3 (H3-H5: 0 of 50 paired games moved; 0.24x / 0.69x / 0.94x units).
- E.5 ADOPTED the staged model: -0.0026 t (t -3.7) at 0.70x units vs live (no leaf), 8 seeds x 1000.
  Leafless shapes, same A/B: nlesc (escalation, leafless probe) +0.0004 t at 0.67x; nlfit (final-depth
  single pass) +0.0005 t at 0.58x. Model vs nlfit: -0.0031 t (t -4.7) at 1.20x -- flagged a TRADE for the
  USER; the run picked the model as notably better. Switch = copy logs/vlq_galadriel/Galadriel.value.NLFIT.json.
- E.6: no escalation-skip allowance (headroom 0.3% < 4%).
- Compare: the pre-keep-table run (logs/vlq_galadriel.prekeep-20261018) spent 9+ h in phase C alone.

## Suite rows resized (USER 2026-10-18: "we should resize all of the regression test rows for the deck")
Smoke d3 150 b10 / d5 75 b20 / 2hg d3 75; regression d3 2x150 / d5 2x75 / 2hg d3 100 + d5 50; overnight the
FULL standard set (d0 4x2000, d3 4x1000 b20, d5 4x500 b40, 2hg d5 4x170 b40). Worst searched case now
~169 ms/game (regression d5 s3003) vs 2,969 before the keep table -- 21st of 36 decks (test/suite_cost.json
still holds the stale 2,969 until the next pooled --measure-all). Galadriel's whole overnight: 42 s makespan.
All three tiers accepted with the leaf live.

## Reference shortfalls explained (2026-10-18)
`ref_bench` (clairvoyant shipped search): search faster on seeds 5, 7, 10, 15. The USER followed every scry
recommendation (replayed: today's defaults == the viewer's, identical states). Cause: the land-drop scry was
only candidate 0 of a SEARCHED choice (MTG_SCRY_WIDTH 2; Galadriel had no ScryVerdict), and the clairvoyant
search kept lands the rule bottoms because it saw where the Crackle sat -- more lands ahead of the Crackle in
the turn-4 chain flip GaladrielCrackleLethalNextTurn (seed 7: 13 lands vs the USER's 12; X=4 = 20 needs 14
mana). With MTG_SCRY_WIDTH=1 the search reproduces the USER on 7 / 10 / 15 exactly (avg 5.600 vs 5.667).
Seed 5 is the one non-clairvoyant gain: an untapped land on T3 (Grotto/Arena) puts Galadriel down a turn early;
her T4 attack is the margin for a T5 X=3 lethal.

## Scry rule as a firm prune (2026-10-18; `MTG_GALADRIEL_SCRY_PRUNE`, default ON)
USER: "No greedy in the search doesn't meant the heuristics in the provider cannot prune the tree. They can and
should." -- `GaladrielProvider::ScryVerdict` returns the USER's keep/bottom rule as a firm verdict, so a
land-drop scry offers one candidate. A/B, 24 seeds x 1000 paired (800000+), d5 b20, three arms:
- CLAIRVOYANT (shipped search): pruned +0.0476 t (t +33; 1,124 slower / 16 faster) at 0.49x core vs the
  unpruned search; NONEED +0.0662 (t +40). Expected: the unpruned search reads the library order.
- NON-CLAIRVOYANT (`MTG_NC_SEARCH=1`, the honest bar): pruned **-0.0108 t (t -6.9; 590 faster / 378 slower)**
  at 0.39x core; NONEED +0.0000 vs searched, **+0.0108 (t +5.6) vs pruned**.
- USER bar ("the only losses we can accept are those that are clearly clairvoyant"): all 378 NC games the
  unpruned search wins and all 590 the prune wins were replayed (both reproduce exactly) and netted by the
  FIRST divergence. Strong confirmations of the rule: Galadriel in hand, a 2nd Galadriel bottomed 5/38 (games
  searched wins / prune wins); Crackle kept 10/35; no Galadriel, Galadriel kept 0/12; temples kept 45/66 and
  bottomed 82/150. The only class leaning to the search: keeping an off-colour scry land (Crystal Grotto, The
  Grey Havens, Zhalfirin Void) under the short-on-lands clause, 23/9, net -16 turns in 24,000 games (-0.0007
  t; p~0.02 single-class, one of ~30 classes, so not significant after correction). Rumble Arena in the same
  role is 6/7, and bottoming the same lands is neutral (44/37). Divergences that are not scries (the search
  spending its budget differently) net +25 for the prune.
- NONEED (USER: "a case I'm genuinely uncertain of"): worse than the plain prune in both modes; stays OFF
  pending the USER.
- Suite (clairvoyant): smoke 6 slower / 0 faster, regression 31 slower / 0 faster, only galadriel moved;
  accepted. Replayed under NC: 28 of 32 (+ the 2hg smoke loss) are clearly clairvoyant (the unpruned honest
  search misses the line too). Honest losses come from 3 deals, all the rule's early keep: seed 1041 (T2,
  colours covered, Skerry out, Epiphany bottomed -- the NONEED case), seed 2091 (T1, short on lands, Temple of
  Mystery kept) and seed 3020 (T2, Temple of Plenty kept for a missing colour). In the 24k NC data the T1
  "short on lands" keep is the only near-even spot: temples kept 38/35, colourless scry lands kept 23/9.
- ref_bench: 0/15 short; search 5.600 vs USER 5.667. Seeds 7, 10, 15 now MATCH the USER (the clairvoyant
  scry wins are gone); seed 5 is still a turn faster.

## nlfit at a raised budget (USER 2026-10-18: "nlfit's time could probably be used better with a higher budget")
Same 40 seeds (700000+) x 1000, d5, with the scry prune (5c85e812) live; each arm vs the model leaf at b20:
- nlfit b20: **-0.0006 t (t -4.1; 24 faster / 2 slower)** at 0.72x core (0.76x units).
- nlfit b27: -0.0006 (t -4.2) at 0.78x core; nlfit b40: -0.0006 (t -4.5) at 0.81x core / 0.95x units.
- nlfit b27 / b40 vs nlfit b20: 1 / 1 and 2 / 1 games faster / slower. Extra budget is spent (units +12% /
  +26%) but buys nothing: at d5 the budget almost never binds for this deck.
- The pre-prune verdict reversed: before the prune the model was -0.0014 t vs nlfit; with the prune, nlfit is
  better AND cheaper. The model was fitted to pre-prune play. Switching = copy
  logs/vlq_galadriel/Galadriel.value.NLFIT.json over decks/Galadriel/Galadriel.value.json -- the USER's call.
- **ADOPTED (USER 2026-10-18: "Okay, let's switch to it.")** decks/Galadriel/Galadriel.value.json = nlfit. Smoke
  unchanged (byte-identical digests); regression 3 faster / 1 slower (d3 s2002 gi9 4->5), accepted.

## Land timing (USER 2026-10-18; `MTG_GALADRIEL_LAND_TIMING`, default ON)
The USER's refinement of the pre-Galadriel land clauses, keyed on MANA IN PLAY: "It's basically: I am missing
a piece which is a scry land"; "So you either have 3+ mana already and can go off next turn or you have 2 mana
which is insufficient to cast galadriel without using your land drop"; acceleration is useful only below two
lands ("none of the acceleration cards can allow us to play Galadriel and a scry land with only 2 scry lands
out"); the short-on-lands keep "is only for when you already have everything else including Galadriel and the
needed colours etc., acceleration is either on board or no longer useful"; and "Sol Ring + Filter land is a
workaround for bad colours" -- "it only fills one colour", two filters fill both ("slowish, but ... the combo
[goes] off T4"). So under the rule each fed filter (Sol Ring held, or turn 4+ / 3+ mana) fills ONE missing
colour; the shipped clause had let one filter cover both from turn 4.
A/B, 24 HELD-OUT seeds (850000+) x 1000, d5 b20, nlfit leaf: non-clairvoyant **-0.0055 t (t -4.4; 381 faster /
265 slower)**, clairvoyant -0.0033 t (t -3.7; 263 / 181), same cost. ADOPTED as the default (the USER's rule,
better in both modes).
- Suite: only galadriel moved. Smoke 7 slower / 1 faster (small rows), regression 9 faster / 1 slower; both
  accepted. RECOVERY (USER: "We should ensure that slower games are recoverable"): the 5 d0 deals recover at
  d8 b0. The 5 searched deals do not -- the rule prunes the scry, so depth cannot take the other branch -- so
  each was checked against the UNPRUNED non-clairvoyant search: on 1045, 1059, 1141 and 3151 it is slow too
  (the old rule's faster line was library-order luck). Seed 1026 is the exception: after five mulligans
  (Galadriel, Skerry, Temple of Plenty kept) T1 Plenty scries Rumble Arena; short on lands, G/U in; the rule
  read "acceleration on board" literally, so the Skerry in HAND did not count and the Arena was bottomed; the
  unpruned honest search keeps it and wins T4 (rule: T5).
- A/B ARM `MTG_GALADRIEL_ACCEL_HAND` (default OFF): acceleration held in hand (Skerry = next land drop, Sol
  Ring off one land) counts as settled. Clairvoyant, 24 seeds x 1000 (850000+): -0.0002 t (t -0.6; 36 / 32).
  Non-clairvoyant, 12 seeds x 1000 (850000+): -0.0007 t (t -0.8; 30 / 24). Neutral in both modes; stays OFF -- the
  USER's call (it reads "acceleration on board" as "held").
- Rebased onto origin 8b3776df (Colossus): GT conflict taken from origin and galadriel re-accepted on the rebased
  binary -- every galadriel row reproduced its pre-rebase value and digest; every other smoke row byte-identical.

## Scry rule: PROPOSED amendment (2026-10-20; `MTG_GALADRIEL_SCRY_AMEND`, default OFF -- the USER decides)
Trigger: the 2026-10-19 overnight rebaseline (b7d599a2) took 267 Galadriel games slower, all recovered by
`MTG_GALADRIEL_SCRY_PRUNE=0`. In 223 the unpruned NON-clairvoyant search misses the old line too (clearly
clairvoyant). In the other **44** (32 deals) the unpruned honest search wins a turn earlier and the pruned one does not:
honest losses to the rule. Both honest arms were replayed with traces (all 44 reproduce) and netted by FIRST
divergence (pruned vs unpruned, `MTG_NC_SEARCH=1`, case settings).

| first divergence (shipped clause) | games / deals | rule / search | mechanism |
|---|---|---|---|
| "anything else is thrown": a LAND bottomed, Galadriel in hand, colours "covered", not short | 20 / 15 | BOT / KEEP | 5 = a lone Temple of Mystery read as both G and U (g20, g345); 3 = an untapped 3rd land puts her down a turn early (g996, 2HG g36, g112); the rest a one-card library shift (which card the next draw / the chain meets) |
| not a scry: the land played | 6 / 4 | -- | g54 x3: the pruned arm's next scry bottoms Mystery's G partner (the same lone-Mystery count); g43 x2: Grotto played T3 to cast her T3; g722: Skerry T1 |
| short on lands, acceleration only in hand (LAND TIMING blocks the keep) | 4 / 3 | BOT / KEEP | the `MTG_GALADRIEL_ACCEL_HAND` case (seed 1026's shape); g774 is also lone-Mystery |
| no Galadriel: keep Galadriel (hand empty after a mull to 1) | 3 / 1 | KEEP / BOT | luck: the rule wins this class 0/12 (24k, 800000+) and 0/3 (4k, 940000+) |
| Crackle kept | 3 / 2 | KEEP / BOT | g187 x2: a SECOND Crackle kept, it displaced the scry land the next chain needed; g621: short on lands, the Crackle displaced her third land |
| a 2nd Galadriel bottomed | 3 / 2 | BOT / KEEP | luck (2/8 in the 940000+ netting; 5/38 in the 24k one) |
| short on lands: any land kept | 3 / 2 | KEEP / BOT | g824: Saprazzan Skerry kept while Sol Ring in hand casts her off two lands -- the land after her must scry; g92 x2: luck |
| colour fixer kept | 2 / 2 | KEEP / BOT | luck (a Skerry / Sol Ring one card deeper) |

Fresh NC netting under the CURRENT rule (pruned vs unpruned, seeds 940000-943000, 4,000 paired games, d5 b20): the
rule is +0.019 t better (t +5.2; 52 / 112), and EVERY clause class nets for the rule except the sub-cases below. Where a
lone-Mystery count flips the verdict the search wins 10 / 1 (net -9 turns); an untapped land with two lands in play 2 / 0;
Sol Ring held 2 / 1; "short-on-lands Crackle" 2 / 2 (dropped); ACCEL_HAND 1 / 2. Compared with the 24k netting
(pre-LAND-TIMING): the "temples bottomed" class is closer to even now (15 / 19), but without its lone-Mystery part it is
8 / 19, i.e. the rule's bottoming is right; the old near-even T1 short-on-lands spot is 6 / 11 for the rule since LAND TIMING.

**The amendment, in the USER's terms** (pre-Galadriel, Galadriel in hand; everything else unchanged):
1. **PAIR** -- "colours covered" means a green source AND a DIFFERENT blue source: Galadriel's {G}{U} is two pips, and
   Temple of Mystery makes one or the other. Mystery alone leaves a colour missing, so a second G or U land is a fix
   (kept), and "bad shape for colours" applies (Crackle / acceleration thrown unless they fix).
2. **CRACKLE HELD** -- a second Crackle is thrown, as it already is once Galadriel is out ("a Crackle we already hold").
3. **SOL MANA** -- Sol Ring held counts as a land for "short on lands", and a Sol Ring in hand adds its two to next turn's
   mana: with two lands + Sol Ring she comes down next turn without the land drop, so the land that follows her must be
   a SCRY land (Saprazzan Skerry starts no chain).
4. **UNTAPPED** -- with exactly two lands in play that make G and U, and no untapped land in hand, keep an untapped land
   (Crystal Grotto, Rumble Arena, Zhalfirin Void, The Grey Havens): it is next turn's drop and she comes down with it --
   the extra attack the seed-5 reference showed. (A first draft also did this at ONE land in play; that branch measured
   worse -- d0 +0.038 t, searched net +24 turns -- because the land is played at the next drop, ahead of the coloured
   Temples, instead of being held. Narrowed in 624e8326.)
Each clause has its own flag (`MTG_GALADRIEL_SCRY_PAIR` / `_CRACKLE_HELD` / `_SOL_MANA` / `_UNTAPPED`) for attribution;
`MTG_GALADRIEL_SCRY_AMEND=1` turns all four on. Flag off is byte-identical to HEAD (44/44 case digests; smoke 4/4 PASS).
"Short-on-lands Crackle thrown" was measured too (2 / 2 in the netting, 0 / 2 the wrong way at first) and dropped.

**Held-out A/B** (24 seeds x 1000, 900000+, d5 b20, shipped profile, AMEND vs shipped):
- NON-clairvoyant (`MTG_NC_SEARCH=1`, the bar): **-0.0062 t (t -7.5; 195 faster / 70 slower)**, no extra cost (core-ms
  per game 0.89x, wall-based on a contended box). Attributed by first divergence: PAIR ~-106 turns, SOL MANA ~-15,
  CRACKLE HELD ~-10, UNTAPPED -8 (24 / 14), follow-on non-scry -9 -- every clause nets for the amendment.
- clairvoyant (shipped search): **-0.0054 t (t -8.5; 170 faster / 46 slower)**, cost 0.997x units. Attributed by first
  divergence: PAIR ~-67 turns, UNTAPPED -23 (25 / 4), SOL MANA ~-14, CRACKLE HELD ~-10, follow-on non-scry -16.
- d0 (the rule played greedily, 24k): **-0.0184 t (t -9.9; 685 / 268)**; per clause PAIR -0.0047, UNTAPPED -0.0202
  (450 / 21), SOL MANA -0.0003, CRACKLE HELD **+0.0065** (t +6.2). The HELD loss is the greedy runner's, not the rule's:
  the d0 runner fires the first Crackle as soon as it is castable, non-lethally, and then needs the second (900000 g490);
  under search it nets for HELD.
- Recovery of the 44 (honest, case settings): **14 / 44** (PAIR 9: g20 x3, g54 x3, g345 x2, g774; UNTAPPED 3: g996,
  2HG g36 / g112; CRACKLE HELD 2: g187 x2; SOL MANA 1: g824); none of the 44 slower. The other 30 are the luck classes
  above, the ACCEL_HAND class (4; that flag alone recovers them, still the USER's open question), the search's own land
  choices (g43, g722) and g621 / g92 / g707.
- Suite, AMEND on (not accepted): smoke searched 0 slower / 1 faster (d3 4.9933 -> 4.9867; d5 and 2HG equal, 15 games
  change play), d0 6.940 -> 6.937 (14 / 22); regression searched 0 / 0 (12 play changes at the same turn, d5 s3003
  byte-identical), d0 6.950 = (16 / 26); 630 references replay clean (0 drift / enum-gap / board-diverged).

## PARKED (USER 2026-10-18: "once this is done I would like to park this deck")
Shipped: exhaustive keep table (K=9, R=40); leaf = nlfit (USER); the USER's scry rule as a firm prune; the
Crackle-pile rule; land timing; every suite row resized; smoke + regression GT accepted (overnight GT is left
for the periodic overnight run).
OPEN USER QUESTIONS (none blocks anything):
- `MTG_GALADRIEL_SCRY_AMEND` (2026-10-20, the four-clause amendment above: G/U pairing, a held Crackle, Sol Ring
  mana, an untapped third land): better in both modes on 24k held-out games; OFF until the USER approves. Adopt all,
  some clauses, or none?
- `MTG_GALADRIEL_ACCEL_HAND` (acceleration held in hand counts as settled; fixes seed 1026): neutral, OFF.
  Turn it on?
- `MTG_GALADRIEL_SCRY_NONEED` (keep on top when nothing is needed): measured worse than the plain rule in both
  modes (+0.011 t non-clairvoyant); OFF. Drop it or keep it as an option?
- The seeds 4/5 turn-5 scry choices in the USER's references.
- Showing a recommended main-phase plan in the viewer (offered).
OWED: the old value-leaf matrix RSS growth (13 -> 19 GiB); test/suite_cost.json refresh on the next pooled
tier run (still holds the pre-keep-table 2,969 ms/game).
Gotchas: 9p clock lag (sleep 200 and delete the objects, including every HeuristicArm.h dependent, before
trusting a build); never pkill -f; gh CLI logged out (curl + `git credential fill` for CI).

## Claude-play sweep
- commit: `314fa393` (the sweep's play; fixes below land after it -- re-sweep owed if they move more than the flagged plans)
- seeds: 90000-90015 (one game per seed; `--game-index` does not vary a claude-play game) games: 16, Opus players
- flags: 0 unresolved
- Outcome vs the search: 13 ties, Claude faster 2 (90003: AI unwon vs T5; 90008: AI T7 vs T5), AI faster 1 (90010).
  Both Claude-faster games are the KEEP policy, not play: the baseline profile's `curve_check: two_drop` (no
  spell of MV <= 2 -> mulligan; Galadriel is MV 3) and `max_lands: 5` (the deck is 53 lands of 60) mulligan
  Galadriel + six lands to four cards. Same mechanism visible in 90007/90014 (AI mulled to 4, still won T5).
- CONFIRMED, FIXED: the flat mana pool credited a fed FREE-mode any-colour filter (Crystal Grotto, Rumble
  Arena) a colour AND a full unit, ignoring the {1} feed -- the enumerator offered Crackle X=1 off
  Grotto+Arena+Skerry+Void (90000 T4) and Galadriel off Skerry+Arena (90013 T2), both dropped by the payer.
  `ManaPool::wild_conv` + CanPayFlat's conversion charge; unit test "Mana pool: a fed filter's colour costs a
  feed". Smoke: only Fluctuator moves (searched scores identical, digests moved; d0 5.067 -> 4.987).
- DISMISSED (by design): "Crackle (X=2), Sol Ring" with 8 mana (90013 T4) -- the plan carries the
  disclosed `drops: ['Sol Ring']`; it duplicates the plain Crackle plan (a collapse candidate, not a defect).
- DISMISSED (cosmetic / goldfish-irrelevant): the decision JSON shows Crackle's cost as `{X}{R}{R}` mv 2
  (pricing is 3X+RR); Crackle's target list omits the controller; combat runs after the opponent hits 0
  (win turn unchanged); a 0-counter Skerry stays until the sacrifice sweep.
- UNCERTAIN, not pursued: 90001 T5 a possible one-land overpay on Crackle X=4 (the protocol does not expose
  the taps; the mana-payment exemption).

## Keep policy — `curve_check: none`, `max_lands: 6` (2026-10-17; AGENT DEFAULT, surfaced to the USER for sign-off)
Found by the claude-play sweep (above): the baseline profile's `curve_check: two_drop` (no MV <= 2 spell ->
mulligan; Galadriel is MV 3) and `max_lands: 5` (53 lands of 60) mulligan Galadriel + six lands to four
cards, against the USER's required-piece intent. A/B, seeds 64000+65000 pooled, suite budgets, on the
wild_conv-fixed binary:

| shape          | current avg / ms | keepfix avg / ms |
|----------------|------------------|------------------|
| d0 (8000)      | 7.3897           | 7.2432           |
| d3 b10 (1200)  | 5.4583 / 5,334   | 5.0075 / 2,294   |
| d5 b20 (600)   | 5.5050 / 7,259   | 5.0066 / 3,340   |

-0.45 to -0.50 turns searched at under half the cost (fewer mulligans -> fewer bottoming trial games).
Adopted into Galadriel.profile.json as the default; the USER owns the keep policy and may overrule.

<!-- verify_deck:begin (generated -- do not edit inside) -->
## Last verification (2026-10-17)

`verify_deck.py decks/Galadriel/Galadriel.cod --write-ledger` -> **FAIL**

| Gate | Status | Blocking | Summary |
|---|---|---|---|
| coverage | PASS | yes | all 17 cards full (missing=0, partial=0) |
| card_costs | FAIL | yes | cost audit INCOMPLETE -- 105 cost(s) UNVERIFIED (Scryfall rate-limited/unreachable) |
| card_fields | FAIL | yes | 2 Scryfall field mismatch(es) |
| clause_ledger | SKIP | no | covered by coverage+bracket-notes+oracle-diff |
| regression_tiers | PASS | yes | in all 3 suite tiers with GT (galadriel) |
| viewer | PASS | yes | self-guard + surface sweep clean |
| viewer_wiring | PASS | yes | 3 type(s) wired (emitter + GUI): dig, scry, target |
| mismatch | PASS | yes | no nonconv/fd-diverge across seeds [7001, 8001] x 60 games (both arms completed) |
| play_invariants | PASS | yes | 8 game(s)/536 decisions: determinism+integrity+progress hold |
| claude_sweep | PASS | yes | Claude-play sweep recorded, 0 unresolved flags |
| suite | PASS | yes | suite gate: Galadriel is regression key `galadriel` |

### Pending user sign-off (block the gate until fixed OR approved below)
Add a key to `## Approved deferrals` to sign one off (only if it is a genuine, understood deferral -- not a bug):
- `card_costs:*` -- 105 card(s) could not be fetched, so their costs were never compared -- a did-not-run, not a mismatch. Re-run when the rate limit clears.
- `card_costs:Leeching Sliver` -- Leeching Sliver: HTTP 429
- `card_costs:Hibernation Sliver` -- Hibernation Sliver: HTTP 429
- `card_costs:Hatchery Sliver` -- Hatchery Sliver: HTTP 429
- `card_costs:Aether Vial` -- Aether Vial: HTTP 429
- `card_costs:Thrumming Hivepool` -- Thrumming Hivepool: HTTP 429
- `card_costs:Treasure Hunt` -- Treasure Hunt: HTTP 429
- `card_costs:Land's Edge` -- Land's Edge: HTTP 429
- `card_costs:Tainted Remedy` -- Tainted Remedy: HTTP 429
- `card_costs:Aria of Flame` -- Aria of Flame: HTTP 429
- `card_costs:Fiery Justice` -- Fiery Justice: HTTP 429
- `card_fields:Saproling Migration` -- Saproling Migration: keywords local=[] scryfall=['kicker']
- `card_fields:Kíli the Resourceful` -- Kíli the Resourceful: keywords local=[] scryfall=['storied']

### Stage 6a disclosure (deferrals + not-yet-built checks)
- coverage deferral -- Galadriel of Lothlórien: CLAUSE 2 MODELLED -- scry_reveal_land_to_battlefield: FireScryWatchers runs at the tail of ScryTop (the ONE scry chokepoint, shared by the land drop, EnterLand and every scry spell in executor and rollout alike), once per Galadriel we control: reveal the top card; a LAND is popped and enters TAPPED through EnterLand (ForcedTapped; not a land drop; landfall fires), and its own etb_scry is a new scry, so the trigger chains down the library. THE CHAIN IS SEARCHED as ONE policy per chain, a skip count k (bottom the first k STOP cards -- nonlands and scry-less lands -- and stop at the next): GaladrielChainCandidates, provider default first, bound by the land's existing searched scry pin (Plan::scry_choice) and clamped at resolution. Two PROVISIONAL DOMINANCE collapses for the AUTONOMOUS search (the human keeps both choices, per look): a scry-land on top is always KEPT (bottoming it reveals the same next card with one fewer land and no look at it), and 'you may reveal' is always TAKEN (a revealed land onto the battlefield beats the same card drawn later; a revealed nonland changes nothing). Human play: every look is the normal `scry` decision and a revealed land asks `dig` '(galadriel reveal)' -- -1 declines the reveal. CLAUSE 1 PROVISIONALLY DEFERRED: 'Whenever the Ring tempts you, if you chose a creature other than Galadriel as your Ring-bearer, scry 3' -- this pool has no card that makes the Ring tempt you, so the trigger can never fire in this deck (the engine does not model the Ring). Galadriel's 3/3 body attacks the passive opponent.
- coverage deferral -- Crackle with Power: Modelled as an {X} DirectDamage with x_damage_multiplier 5 and three {X} pips (cost = 3X + {R}{R}). The win-relevant case is targeting the opponent's face for 5X; 'up to X targets' matters for Hinata's per-target cost reduction AND, in 2HG (opponent_heads 2, core/GameSetup.h), for the SECOND opposing head: CrackleExtraTargetOrder offers it first, so a declared count >= 1 deals 5X to both heads of the shared team pool (lethal at X=3 vs 30). In a 1-head game extra targets never add face damage, so the single-target face model is exact there.
- coverage deferral -- Crystal Grotto: All clauses modelled. etb_scry 1; '{T}: Add {C}' + '{1}, {T}: Add one mana of any color' is the Capital City shape, any_color_filter (the free {C} mode is implicit; `produces` lists the FED mode's colours). Nothing deferred.
- coverage deferral -- Temple of Abandon: All three clauses modelled, the Temple of Epiphany shape: enters_tapped, etb_scry 1 (the searched land-ETB scry axis, Plan::scry_choice; under Galadriel of Lothlórien the same pin selects the reveal chain's policy), and a two-colour tap. Nothing deferred.
- coverage deferral -- Temple of Deceit: All three clauses modelled, the Temple of Epiphany shape: enters_tapped, etb_scry 1 (the searched land-ETB scry axis, Plan::scry_choice; under Galadriel of Lothlórien the same pin selects the reveal chain's policy), and a two-colour tap. Nothing deferred.
- coverage deferral -- Temple of Enlightenment: All three clauses modelled, the Temple of Epiphany shape: enters_tapped, etb_scry 1 (the searched land-ETB scry axis, Plan::scry_choice; under Galadriel of Lothlórien the same pin selects the reveal chain's policy), and a two-colour tap. Nothing deferred.
- coverage deferral -- Temple of Malady: All three clauses modelled, the Temple of Epiphany shape: enters_tapped, etb_scry 1 (the searched land-ETB scry axis, Plan::scry_choice; under Galadriel of Lothlórien the same pin selects the reveal chain's policy), and a two-colour tap. Nothing deferred.
- coverage deferral -- Temple of Malice: All three clauses modelled, the Temple of Epiphany shape: enters_tapped, etb_scry 1 (the searched land-ETB scry axis, Plan::scry_choice; under Galadriel of Lothlórien the same pin selects the reveal chain's policy), and a two-colour tap. Nothing deferred.
- coverage deferral -- Temple of Mystery: All three clauses modelled, the Temple of Epiphany shape: enters_tapped, etb_scry 1 (the searched land-ETB scry axis, Plan::scry_choice; under Galadriel of Lothlórien the same pin selects the reveal chain's policy), and a two-colour tap. Nothing deferred.
- coverage deferral -- Temple of Plenty: All three clauses modelled, the Temple of Epiphany shape: enters_tapped, etb_scry 1 (the searched land-ETB scry axis, Plan::scry_choice; under Galadriel of Lothlórien the same pin selects the reveal chain's policy), and a two-colour tap. Nothing deferred.
- coverage deferral -- Temple of Triumph: All three clauses modelled, the Temple of Epiphany shape: enters_tapped, etb_scry 1 (the searched land-ETB scry axis, Plan::scry_choice; under Galadriel of Lothlórien the same pin selects the reveal chain's policy), and a two-colour tap. Nothing deferred.
- coverage deferral -- The Grey Havens: All clauses modelled. Untapped, etb_scry 1, '{T}: Add {C}'. The second mana ability is mana_colors_of_graveyard_legends: EffectiveProduces (core/SpellEffects.h, the dynamic-colour accessor Reflecting Pool / domain mana use) returns {C} plus every colour of a LEGENDARY CREATURE card in its controller's graveyard -- read off the definition (LookupCached), never the zone placeholder. In this deck the only such card is Galadriel of Lothlórien (G/U), which reaches the graveyard by the legend rule or a cleanup discard. LEGENDARY: a second copy is legend-ruled by EnforceLegendRule at the engine's beginning-of-combat SBA checkpoint (mana-equivalent for main 1: a real player may tap the first copy before playing the second). Nothing deferred.
- coverage deferral -- Zhalfirin Void: Both clauses modelled: an UNTAPPED {C} land with etb_scry 1. Nothing deferred.
- coverage deferral -- Rumble Arena: All clauses modelled. etb_scry 1; '{T}: Add {C}' + '{1}, {T}: Add one mana of any color' is the Capital City shape, any_color_filter (the free {C} mode is implicit; `produces` lists the FED mode's colours). VIGILANCE is printed on a land that is never a creature (nothing in this pool animates it), so it has no game effect; carried as a keyword for faithfulness. Nothing deferred.
- allowlisted divergence -- Galerider Sliver [keywords]: Keyword-lord: 'Sliver creatures you control have flying' grants flying to your Slivers INCLUDING itself, so the card functionally has flying (modeled 
- allowlisted divergence -- Striking Sliver [keywords]: Keyword-lord: grants first strike to your Slivers incl. itself (modeled self-innate). First strike is inert in goldfishing (no blockers). See oracle b
- allowlisted divergence -- Cloudshredder Sliver [keywords]: Keyword-lord: grants flying+haste to your Slivers incl. itself. Flying self-innate + inert in goldfishing; haste additionally granted to other Slivers
- allowlisted divergence -- Haytham Kenway [keywords]: 'Protection from Assassins' is a real keyword but inert in goldfishing (no Assassins in play); the protection-to-other-Knights is an anthem grant, not
- allowlisted divergence -- Goblin Piledriver [keywords]: 'Protection from blue' is a real keyword but inert in goldfishing (the passive opponent has no blue sources or blockers to target); the attack-trigger
- allowlisted divergence -- Progenitus [keywords]: 'Protection from everything' is a real keyword but inert in goldfishing (the passive opponent never targets, blocks, or damages); the graveyard shuffl
- allowlisted divergence -- Bloom Tender [keywords]: Scryfall lists 'vivid' in keywords -- a data quirk (no rules-meaningful innate keyword on this card); the each-color-among-permanents mana ability is 
- allowlisted divergence -- Auriok Champion [keywords]: 'Protection from black and from red' is a real keyword but inert in goldfishing on all four DEBT axes (the passive opponent never damages, targets, bl
- allowlisted divergence -- Brightcap Badger [mana_cost]: ADVENTURE CARD (CR 715), authored as TWO entries -- 'Brightcap Badger' (the {3}{G} 3/4 creature face) and 'Fungus Frolic' (the {2}{G} instant half). S
- allowlisted divergence -- Fungus Frolic [mana_cost]: The ADVENTURE HALF of Brightcap Badger // Fungus Frolic -- see the Brightcap Badger entry. Scryfall's combined mana_cost is '{3}{G} // {2}{G}'; the in
- allowlisted divergence -- Fungus Frolic [P/T]: An Instant has no power or toughness. Scryfall reports 3/4 because it describes the COMBINED card, whose creature face is the 3/4 Brightcap Badger. Gi
- allowlisted divergence -- Mycoloth [keywords]: 'Devour 2' is a real keyword and is FULLY MODELLED -- but via the `devour` CardParam, not via Keyword::Devour. The engine's Keyword enum deliberately 
- allowlisted divergence -- Brutal Cathar [keywords]: Transforming DFC (Brutal Cathar // Moonrage Brute): Scryfall reports CARD-LEVEL keywords for both faces. The front face's only keyword is Daybound, mo
- allowlisted divergence -- Moonrage Brute [keywords]: Transforming DFC back face: Scryfall's card-level keywords include the front's Daybound and the layout's Transform; Nightbound is modelled structurall
- STALE allowlist entry -- Glorybringer [keywords] no longer mismatches; remove from scryfall_divergences.json
- oracle_text advisory -- Gideon, Ally of Zendikar: oracle_text diverges (similarity 0.10); scryfall='+1: Until end of turn, Gideon becomes a 5/5 Human Soldier Ally creature with indestructible that\'s still a pl
- oracle_text advisory -- Basri, Tomorrow's Champion: oracle_text diverges (similarity 0.15); scryfall="{W}, {T}, Exert Basri: Create a 1/1 white Cat creature token with lifelink. (An exerted creature won't untap d
- oracle_text advisory -- Light Up the Stage: oracle_text diverges (similarity 0.69); scryfall='Spectacle {R} (You may cast this spell for its spectacle cost rather than its mana cost if an opponent lost li
- oracle_text advisory -- Crystalline Sliver: oracle_text diverges (similarity 0.61); scryfall="All Slivers have shroud. (They can't be the targets of spells or abilities.)"
- oracle_text advisory -- Galerider Sliver: oracle_text diverges (similarity 0.41); scryfall='Sliver creatures you control have flying.'
- oracle_text advisory -- Striking Sliver: oracle_text diverges (similarity 0.56); scryfall='Sliver creatures you control have first strike. (They deal combat damage before creatures without first strike
- oracle_text advisory -- Cloudshredder Sliver: oracle_text diverges (similarity 0.48); scryfall='Sliver creatures you control have flying and haste.'
- oracle_text advisory -- Hibernation Sliver: oracle_text diverges (similarity 0.49); scryfall='All Slivers have "Pay 2 life: Return this permanent to its owner\'s hand."'
- oracle_text advisory -- Cavern of Souls: oracle_text diverges (similarity 0.40); scryfall="As this land enters, choose a creature type.\n{T}: Add {C}.\n{T}: Add one mana of any color. Spend this mana o
- oracle_text advisory -- Unclaimed Territory: oracle_text diverges (similarity 0.20); scryfall='As this land enters, choose a creature type.\n{T}: Add {C}.\n{T}: Add one mana of any color. Spend this mana o
- oracle_text advisory -- Secluded Courtyard: oracle_text diverges (similarity 0.27); scryfall='As this land enters, choose a creature type.\n{T}: Add {C}.\n{T}: Add one mana of any color. Spend this mana o
- oracle_text advisory -- Mutavault: oracle_text diverges (similarity 0.56); scryfall="{T}: Add {C}.\n{1}: This land becomes a 2/2 creature with all creature types until end of turn. It's still a l
- oracle_text advisory -- Aether Vial: oracle_text diverges (similarity 0.71); scryfall='At the beginning of your upkeep, you may put a charge counter on this artifact.\n{T}: You may put a creature c
- oracle_text advisory -- Reliquary Tower: oracle_text diverges (similarity 0.44); scryfall='You have no maximum hand size.\n{T}: Add {C}.'
- oracle_text advisory -- Dwarven Hold: oracle_text diverges (similarity 0.23); scryfall='This land enters tapped.\nYou may choose not to untap this land during your untap step.\nAt the beginning of y
- oracle_text advisory -- Mercadian Bazaar: oracle_text diverges (similarity 0.26); scryfall='This land enters tapped.\n{T}: Put a storage counter on this land.\n{T}, Remove any number of storage counters
- oracle_text advisory -- Temple of Epiphany: oracle_text diverges (similarity 0.60); scryfall='This land enters tapped.\nWhen this land enters, scry 1. (Look at the top card of your library. You may put th
- oracle_text advisory -- Thundering Falls: oracle_text diverges (similarity 0.63); scryfall='({T}: Add {U} or {R}.)\nThis land enters tapped.\nWhen this land enters, surveil 1. (Look at the top card of y
- oracle_text advisory -- Land's Edge: oracle_text diverges (similarity 0.51); scryfall='Discard a card: If the discarded card was a land card, this enchantment deals 2 damage to target player or pla
- oracle_text advisory -- Throes of Chaos: oracle_text diverges (similarity 0.06); scryfall='Cascade (When you cast this spell, exile cards from the top of your library until you exile a nonland card tha
- oracle_text advisory -- Tournament Grounds: oracle_text diverges (similarity 0.37); scryfall='{T}: Add {C}.\n{T}: Add {R}, {W}, or {B}. Spend this mana only to cast a Knight or Equipment spell.'
- oracle_text advisory -- Dauntless Bodyguard: oracle_text diverges (similarity 0.55); scryfall='As this creature enters, choose another creature you control.\nSacrifice this creature: The chosen creature ga
- oracle_text advisory -- Venerable Knight: oracle_text diverges (similarity 0.52); scryfall='When this creature dies, put a +1/+1 counter on target Knight you control.'
- oracle_text advisory -- Acclaimed Contender: oracle_text diverges (similarity 0.45); scryfall='When this creature enters, if you control another Knight, look at the top five cards of your library. You may 
- oracle_text advisory -- Knight Exemplar: oracle_text diverges (similarity 0.41); scryfall='First strike (This creature deals combat damage before creatures without first strike.)\nOther Knight creature
- oracle_text advisory -- Valiant Knight: oracle_text diverges (similarity 0.18); scryfall='Other Knights you control get +1/+1.\n{3}{W}{W}: Knights you control gain double strike until end of turn.'
- oracle_text advisory -- Kinsbaile Cavalier: oracle_text diverges (similarity 0.06); scryfall='Knight creatures you control have double strike.'
- oracle_text advisory -- Marshal of Zhalfir: oracle_text diverges (similarity 0.49); scryfall='Other Knights you control get +1/+1.\n{W}{U}, {T}: Tap another target creature.'
- oracle_text advisory -- Haytham Kenway: oracle_text diverges (similarity 0.53); scryfall='Protection from Assassins\nOther Knights you control get +2/+2 and have protection from Assassins.\nWhen Hayth
- oracle_text advisory -- Adeline, Resplendent Cathar: oracle_text diverges (similarity 0.76); scryfall="Vigilance\nAdeline's power is equal to the number of creatures you control.\nWhenever you attack, for each opp
- oracle_text advisory -- Accorder Paladin: oracle_text diverges (similarity 0.33); scryfall='Battle cry (Whenever this creature attacks, each other attacking creature gets +1/+0 until end of turn.)'
- oracle_text advisory -- Silverblade Paladin: oracle_text diverges (similarity 0.25); scryfall='Soulbond (You may pair this creature with another unpaired creature when either enters. They remain paired for
- oracle_text advisory -- Hero of Bladehold: oracle_text diverges (similarity 0.50); scryfall='Battle cry (Whenever this creature attacks, each other attacking creature gets +1/+0 until end of turn.)\nWhen
- oracle_text advisory -- Windswept Heath: oracle_text diverges (similarity 0.36); scryfall='{T}, Pay 1 life, Sacrifice this land: Search your library for a Forest or Plains card, put it onto the battlef
- oracle_text advisory -- Marsh Flats: oracle_text diverges (similarity 0.36); scryfall='{T}, Pay 1 life, Sacrifice this land: Search your library for a Plains or Swamp card, put it onto the battlefi
- oracle_text advisory -- Bloodstained Mire: oracle_text diverges (similarity 0.36); scryfall='{T}, Pay 1 life, Sacrifice this land: Search your library for a Swamp or Mountain card, put it onto the battle
- oracle_text advisory -- Wooded Foothills: oracle_text diverges (similarity 0.36); scryfall='{T}, Pay 1 life, Sacrifice this land: Search your library for a Mountain or Forest card, put it onto the battl
- oracle_text advisory -- Grove of the Burnwillows: oracle_text diverges (similarity 0.20); scryfall='{T}: Add {C}.\n{T}: Add {R} or {G}. Each opponent gains 1 life.'
- oracle_text advisory -- Ignoble Hierarch: oracle_text diverges (similarity 0.56); scryfall='Exalted (Whenever a creature you control attacks alone, that creature gets +1/+1 until end of turn.)\n{T}: Add
- oracle_text advisory -- Skyshroud Cutter: oracle_text diverges (similarity 0.38); scryfall="If you control a Forest, rather than pay this spell's mana cost, you may have each other player gain 5 life."
- oracle_text advisory -- Plague Drone: oracle_text diverges (similarity 0.70); scryfall='Flying\nRot Fly — If an opponent would gain life, that player loses that much life instead.'
- oracle_text advisory -- Aria of Flame: oracle_text diverges (similarity 0.78); scryfall='When this enchantment enters, each opponent gains 10 life.\nWhenever you cast an instant or sorcery spell, put
- oracle_text advisory -- Fiery Justice: oracle_text diverges (similarity 0.54); scryfall='Fiery Justice deals 5 damage divided as you choose among any number of targets. Target opponent gains 5 life.'
- oracle_text advisory -- Swords to Plowshares: oracle_text diverges (similarity 0.17); scryfall='Exile target creature. Its controller gains life equal to its power.'
- oracle_text advisory -- Invigorate: oracle_text diverges (similarity 0.52); scryfall="If you control a Forest, rather than pay this spell's mana cost, you may have an opponent gain 3 life.\nTarget
- oracle_text advisory -- Reverent Silence: oracle_text diverges (similarity 0.38); scryfall="If you control a Forest, rather than pay this spell's mana cost, you may have each other player gain 6 life.\n
- oracle_text advisory -- Idyllic Tutor: oracle_text diverges (similarity 0.30); scryfall='Search your library for an enchantment card, reveal it, put it into your hand, then shuffle.'
- oracle_text advisory -- Enlightened Tutor: oracle_text diverges (similarity 0.32); scryfall='Search your library for an artifact or enchantment card, reveal it, then shuffle and put that card on top.'
- oracle_text advisory -- Forbidden Orchard: oracle_text diverges (similarity 0.22); scryfall='{T}: Add one mana of any color.\nWhenever you tap this land for mana, target opponent creates a 1/1 colorless 
- oracle_text advisory -- Reflecting Pool: oracle_text diverges (similarity 0.26); scryfall='{T}: Add one mana of any type that a land you control could produce.'
- oracle_text advisory -- Izzet Signet: oracle_text diverges (similarity 0.11); scryfall='{1}, {T}: Add {U}{R}.'
- oracle_text advisory -- Ponder: oracle_text diverges (similarity 0.32); scryfall='Look at the top three cards of your library, then put them back in any order. You may shuffle.\nDraw a card.'
- oracle_text advisory -- Preordain: oracle_text diverges (similarity 0.27); scryfall='Scry 2, then draw a card. (To scry 2, look at the top two cards of your library, then put any number of them o
- oracle_text advisory -- Expressive Iteration: oracle_text diverges (similarity 0.45); scryfall='Look at the top three cards of your library. Put one of them into your hand, put one of them on the bottom of 
- oracle_text advisory -- Crackle with Power: oracle_text diverges (similarity 0.17); scryfall='Crackle with Power deals five times X damage to each of up to X targets.'
- oracle_text advisory -- Remand: oracle_text diverges (similarity 0.58); scryfall="Counter target spell. If that spell is countered this way, put it into its owner's hand instead of into that p
- oracle_text advisory -- Memory Lapse: oracle_text diverges (similarity 0.69); scryfall="Counter target spell. If that spell is countered this way, put it on top of its owner's library instead of int
- oracle_text advisory -- Distorting Wake: oracle_text diverges (similarity 0.34); scryfall="Return X target nonland permanents to their owners' hands."
- oracle_text advisory -- Icy Blast: oracle_text diverges (similarity 0.64); scryfall="Tap X target creatures.\nFerocious — If you control a creature with power 4 or greater, those creatures don't 
- oracle_text advisory -- Hinata, Dawn-Crowned: oracle_text diverges (similarity 0.31); scryfall='Flying, trample\nSpells you cast cost {1} less to cast for each target.\nSpells your opponents cast cost {1} m
- oracle_text advisory -- Izzet Boilerworks: oracle_text diverges (similarity 0.38); scryfall="This land enters tapped.\nWhen this land enters, return a land you control to its owner's hand.\n{T}: Add {U}{
- oracle_text advisory -- Orzhov Basilica: oracle_text diverges (similarity 0.38); scryfall="This land enters tapped.\nWhen this land enters, return a land you control to its owner's hand.\n{T}: Add {W}{
- oracle_text advisory -- Soulfire Eruption: oracle_text diverges (similarity 0.28); scryfall="Choose any number of target creatures, planeswalkers, and/or players. For each of them, exile the top card of 
- oracle_text advisory -- Magma Opus: oracle_text diverges (similarity 0.46); scryfall='Magma Opus deals 4 damage divided as you choose among any number of targets. Tap two target permanents. Create
- oracle_text advisory -- Reality Spasm: oracle_text diverges (similarity 0.20); scryfall='Choose one —\n• Tap X target permanents.\n• Untap X target permanents.'
- oracle_text advisory -- Ornithopter of Paradise: oracle_text diverges (similarity 0.13); scryfall='Flying\n{T}: Add one mana of any color.'
- oracle_text advisory -- Gamble: oracle_text diverges (similarity 0.22); scryfall='Search your library for a card, put that card into your hand, discard a card at random, then shuffle.'
- oracle_text advisory -- Irencrag Feat: oracle_text diverges (similarity 0.10); scryfall='Add seven {R}. You can cast only one more spell this turn.'
- oracle_text advisory -- Pyretic Ritual: oracle_text diverges (similarity 0.05); scryfall='Add {R}{R}{R}.'
- oracle_text advisory -- Seething Song: oracle_text diverges (similarity 0.08); scryfall='Add {R}{R}{R}{R}{R}.'
- oracle_text advisory -- Desperate Ritual: oracle_text diverges (similarity 0.18); scryfall="Add {R}{R}{R}.\nSplice onto Arcane {1}{R} (As you cast an Arcane spell, you may reveal this card from your han
- oracle_text advisory -- Dragonlord Kolaghan: oracle_text diverges (similarity 0.53); scryfall='Flying, haste\nOther creatures you control have haste.\nWhenever an opponent casts a creature or planeswalker 
- oracle_text advisory -- Karrthus, Tyrant of Jund: oracle_text diverges (similarity 0.37); scryfall='Flying, haste\nWhen Karrthus enters, gain control of all Dragons, then untap all Dragons.\nOther Dragon creatu
- oracle_text advisory -- Ruby Medallion: oracle_text diverges (similarity 0.17); scryfall='Red spells you cast cost {1} less to cast.'
- oracle_text advisory -- Lotus Bloom: oracle_text diverges (similarity 0.25); scryfall='Suspend 3—{0} (Rather than cast this card from your hand, pay {0} and exile it with three time counters on it.
- oracle_text advisory -- Rite of Flame: oracle_text diverges (similarity 0.21); scryfall='Add {R}{R}, then add {R} for each card named Rite of Flame in each graveyard.'
- oracle_text advisory -- Scourge of Valkas: oracle_text diverges (similarity 0.32); scryfall='Flying\nWhenever this creature or another Dragon you control enters, it deals X damage to any target, where X 
- oracle_text advisory -- Lathliss, Dragon Queen: oracle_text diverges (similarity 0.31); scryfall='Flying\nWhenever another nontoken Dragon you control enters, create a 5/5 red Dragon creature token with flyin
- oracle_text advisory -- Utvara Hellkite: oracle_text diverges (similarity 0.24); scryfall='Flying\nWhenever a Dragon you control attacks, create a 6/6 red Dragon creature token with flying.'
- oracle_text advisory -- Dragonstorm: oracle_text diverges (similarity 0.16); scryfall='Search your library for a Dragon permanent card, put it onto the battlefield, then shuffle.\nStorm (When you c
- oracle_text advisory -- Apex of Power: oracle_text diverges (similarity 0.15); scryfall='Exile the top seven cards of your library. Until end of turn, you may cast spells from among them.\nIf this sp
- oracle_text advisory -- Slippery Bogle: oracle_text diverges (similarity 0.41); scryfall="Hexproof (This creature can't be the target of spells or abilities your opponents control.)"
- oracle_text advisory -- Gladecover Scout: oracle_text diverges (similarity 0.76); scryfall="Hexproof (This creature can't be the target of spells or abilities your opponents control.)"
- oracle_text advisory -- Kor Spiritdancer: oracle_text diverges (similarity 0.53); scryfall='This creature gets +2/+2 for each Aura attached to it.\nWhenever you cast an Aura spell, you may draw a card.'
- oracle_text advisory -- Light-Paws, Emperor's Voice: oracle_text diverges (similarity 0.74); scryfall='Whenever an Aura you control enters, if you cast it, you may search your library for an Aura card with mana va
- oracle_text advisory -- Ethereal Armor: oracle_text diverges (similarity 0.60); scryfall='Enchant creature\nEnchanted creature gets +1/+1 for each enchantment you control and has first strike.'
- oracle_text advisory -- Rancor: oracle_text diverges (similarity 0.68); scryfall="Enchant creature\nEnchanted creature gets +2/+0 and has trample.\nWhen this Aura is put into a graveyard from 
- oracle_text advisory -- Daybreak Coronet: oracle_text diverges (similarity 0.58); scryfall='Enchant creature with another Aura attached to it\nEnchanted creature gets +3/+3 and has first strike, vigilan
- oracle_text advisory -- Armadillo Cloak: oracle_text diverges (similarity 0.77); scryfall='Enchant creature\nEnchanted creature gets +2/+2 and has trample.\nWhenever enchanted creature deals damage, yo
- oracle_text advisory -- Spirit Mantle: oracle_text diverges (similarity 0.66); scryfall='Enchant creature\nEnchanted creature gets +1/+1 and has protection from creatures.'
- oracle_text advisory -- Spider Umbra: oracle_text diverges (similarity 0.40); scryfall='Enchant creature\nEnchanted creature gets +1/+1 and has reach. (It can block creatures with flying.)\nUmbra ar
- oracle_text advisory -- Ancestral Mask: oracle_text diverges (similarity 0.59); scryfall='Enchant creature\nEnchanted creature gets +2/+2 for each other enchantment on the battlefield.'
- oracle_text advisory -- Alpha Authority: oracle_text diverges (similarity 0.54); scryfall="Enchant creature\nEnchanted creature has hexproof and can't be blocked by more than one creature."
- oracle_text advisory -- Gryff's Boon: oracle_text diverges (similarity 0.75); scryfall='Enchant creature\nEnchanted creature gets +1/+0 and has flying.\n{3}{W}: Return this card from your graveyard 
- oracle_text advisory -- Audacity: oracle_text diverges (similarity 0.59); scryfall="Enchant creature\nEnchanted creature gets +2/+0 and has trample. (It can deal excess combat damage to the play
- oracle_text advisory -- All That Glitters: oracle_text diverges (similarity 0.57); scryfall='Enchant creature\nEnchanted creature gets +1/+1 for each artifact and/or enchantment you control.'
- oracle_text advisory -- Spirit Link: oracle_text diverges (similarity 0.47); scryfall='Enchant creature (Target a creature as you cast this. This card enters attached to that creature.)\nWhenever e
- oracle_text advisory -- Lion Umbra: oracle_text diverges (similarity 0.77); scryfall='Enchant modified creature (Equipment, Auras its controller controls, and counters are modifications.)\nEnchant
- oracle_text advisory -- Brushland: oracle_text diverges (similarity 0.28); scryfall='{T}: Add {C}.\n{T}: Add {G} or {W}. This land deals 1 damage to you.'
- oracle_text advisory -- Branchloft Pathway: oracle_text diverges (similarity 0.07); scryfall='{T}: Add {G}.'
- oracle_text advisory -- Darkbore Pathway: oracle_text diverges (similarity 0.07); scryfall='{T}: Add {B}.'
- oracle_text advisory -- Goblin King: oracle_text diverges (similarity 0.31); scryfall='Other Goblins get +1/+1 and have mountainwalk.'
- oracle_text advisory -- Goblin Chieftain: oracle_text diverges (similarity 0.41); scryfall='Haste (This creature can attack and {T} as soon as it comes under your control.)\nOther Goblin creatures you c
- oracle_text advisory -- Goblin Warchief: oracle_text diverges (similarity 0.52); scryfall='Goblin spells you cast cost {1} less to cast.\nGoblins you control have haste.'
- oracle_text advisory -- Goblin Piledriver: oracle_text diverges (similarity 0.43); scryfall="Protection from blue (This creature can't be blocked, targeted, dealt damage, or enchanted by anything blue.)\
- oracle_text advisory -- Goblin Matron: oracle_text diverges (similarity 0.66); scryfall='When this creature enters, you may search your library for a Goblin card, reveal that card, put it into your h
- oracle_text advisory -- Mogg War Marshal: oracle_text diverges (similarity 0.56); scryfall='Echo {1}{R} (At the beginning of your upkeep, if this came under your control since the beginning of your last
- oracle_text advisory -- Siege-Gang Commander: oracle_text diverges (similarity 0.58); scryfall='When this creature enters, create three 1/1 red Goblin creature tokens.\n{1}{R}, Sacrifice a Goblin: This crea
- oracle_text advisory -- Skirk Prospector: oracle_text diverges (similarity 0.31); scryfall='Sacrifice a Goblin: Add {R}.'
- oracle_text advisory -- Krenko, Mob Boss: oracle_text diverges (similarity 0.43); scryfall='{T}: Create X 1/1 red Goblin creature tokens, where X is the number of Goblins you control.'
- oracle_text advisory -- Pashalik Mons: oracle_text diverges (similarity 0.52); scryfall='Whenever Pashalik Mons or another Goblin you control dies, Pashalik Mons deals 1 damage to any target.\n{3}{R}
- oracle_text advisory -- Rundvelt Hordemaster: oracle_text diverges (similarity 0.36); scryfall="Other Goblins you control get +1/+1.\nWhenever this creature or another Goblin you control dies, exile the top
- oracle_text advisory -- Goblin Lackey: oracle_text diverges (similarity 0.56); scryfall='Whenever this creature deals damage to a player, you may put a Goblin permanent card from your hand onto the b
- oracle_text advisory -- Muxus, Goblin Grandee: oracle_text diverges (similarity 0.46); scryfall='When Muxus enters, reveal the top six cards of your library. Put all Goblin creature cards with mana value 5 o
- oracle_text advisory -- Goblin Chainwhirler: oracle_text diverges (similarity 0.40); scryfall='First strike\nWhen this creature enters, it deals 1 damage to each opponent and each creature and planeswalker
- oracle_text advisory -- Twinshot Sniper: oracle_text diverges (similarity 0.50); scryfall='Reach\nWhen this creature enters, it deals 2 damage to any target.\nChannel — {1}{R}, Discard this card: It de
- oracle_text advisory -- Stingscourger: oracle_text diverges (similarity 0.71); scryfall="Echo {3}{R} (At the beginning of your upkeep, if this came under your control since the beginning of your last
- oracle_text advisory -- Three Tree City: oracle_text diverges (similarity 0.47); scryfall='As Three Tree City enters, choose a creature type.\n{T}: Add {C}.\n{2}, {T}: Choose a color. Add an amount of 
- oracle_text advisory -- Hunted Phantasm: oracle_text diverges (similarity 0.34); scryfall="This creature can't be blocked.\nWhen this creature enters, target opponent creates five 1/1 red Goblin creatu
- oracle_text advisory -- Suture Priest: oracle_text diverges (similarity 0.49); scryfall='Whenever another creature you control enters, you may gain 1 life.\nWhenever a creature an opponent controls e
- oracle_text advisory -- Massacre Wurm: oracle_text diverges (similarity 0.38); scryfall='When this creature enters, creatures your opponents control get -2/-2 until end of turn.\nWhenever a creature 
- oracle_text advisory -- Soul Warden: oracle_text diverges (similarity 0.15); scryfall='Whenever another creature enters, you gain 1 life.'
- oracle_text advisory -- Essence Warden: oracle_text diverges (similarity 0.24); scryfall='Whenever another creature enters, you gain 1 life.'
- oracle_text advisory -- City of Brass: oracle_text diverges (similarity 0.19); scryfall='Whenever this land becomes tapped, it deals 1 damage to you.\n{T}: Add one mana of any color.'
- oracle_text advisory -- Defense of the Heart: oracle_text diverges (similarity 0.43); scryfall='At the beginning of your upkeep, if an opponent controls three or more creatures, sacrifice this enchantment, 
- oracle_text advisory -- Sylvan Scrying: oracle_text diverges (similarity 0.47); scryfall='Search your library for a land card, reveal it, put it into your hand, then shuffle.'
- oracle_text advisory -- Crop Rotation: oracle_text diverges (similarity 0.42); scryfall='As an additional cost to cast this spell, sacrifice a land.\nSearch your library for a land card, put that car
- oracle_text advisory -- Varchild's War-Riders: oracle_text diverges (similarity 0.58); scryfall='Cumulative upkeep—Have an opponent create a 1/1 red Survivor creature token. (At the beginning of your upkeep,
- oracle_text advisory -- Azorius Chancery: oracle_text diverges (similarity 0.38); scryfall="This land enters tapped.\nWhen this land enters, return a land you control to its owner's hand.\n{T}: Add {W}{
- oracle_text advisory -- Tree of Tales: oracle_text diverges (similarity 0.15); scryfall='{T}: Add {G}.'
- oracle_text advisory -- Misty Rainforest: oracle_text diverges (similarity 0.36); scryfall='{T}, Pay 1 life, Sacrifice this land: Search your library for a Forest or Island card, put it onto the battlef
- oracle_text advisory -- Verdant Catacombs: oracle_text diverges (similarity 0.28); scryfall='{T}, Pay 1 life, Sacrifice this land: Search your library for a Swamp or Forest card, put it onto the battlefi
- oracle_text advisory -- Scalding Tarn: oracle_text diverges (similarity 0.29); scryfall='{T}, Pay 1 life, Sacrifice this land: Search your library for an Island or Mountain card, put it onto the batt
- oracle_text advisory -- Cosmic Spider-Man: oracle_text diverges (similarity 0.47); scryfall='Flying, first strike, trample, lifelink, haste\nAt the beginning of combat on your turn, other Spiders you con
- oracle_text advisory -- Mana Cannons: oracle_text diverges (similarity 0.44); scryfall='Whenever you cast a multicolored spell, this enchantment deals X damage to any target, where X is the number o
- oracle_text advisory -- Ancient Cornucopia: oracle_text diverges (similarity 0.43); scryfall="Whenever you cast a spell that's one or more colors, you may gain 1 life for each of that spell's colors. Do t
- oracle_text advisory -- Two-Headed Hellkite: oracle_text diverges (similarity 0.26); scryfall='Flying, menace, haste\nWhenever this creature attacks, draw two cards.'
- oracle_text advisory -- Progenitus: oracle_text diverges (similarity 0.28); scryfall="Protection from everything\nIf Progenitus would be put into a graveyard from anywhere, reveal Progenitus and s
- oracle_text advisory -- Faeburrow Elder: oracle_text diverges (similarity 0.36); scryfall='Vigilance\nThis creature gets +1/+1 for each color among permanents you control.\n{T}: For each color among pe
- oracle_text advisory -- Bloom Tender: oracle_text diverges (similarity 0.52); scryfall='Vivid — {T}: For each color among permanents you control, add one mana of that color.'
- oracle_text advisory -- Deathrite Shaman: oracle_text diverges (similarity 0.46); scryfall='{T}: Exile target land card from a graveyard. Add one mana of any color. (Activate only as an instant.)\n{B}, 
- oracle_text advisory -- Lightning Greaves: oracle_text diverges (similarity 0.18); scryfall="Equipped creature has haste and shroud. (It can't be the target of spells or abilities.)\nEquip {0}"
- oracle_text advisory -- Maelstrom Archangel: oracle_text diverges (similarity 0.31); scryfall='Flying\nWhenever this creature deals combat damage to a player, you may cast a spell from your hand without pa
- oracle_text advisory -- Jared Carthalion: oracle_text diverges (similarity 0.60); scryfall="+1: Create a 3/3 Kavu creature token with trample that's all colors.\n−3: Choose up to two target creatures. F
- oracle_text advisory -- Nicol Bolas, Planeswalker: oracle_text diverges (similarity 0.21); scryfall="+3: Destroy target noncreature permanent.\n−2: Gain control of target creature.\n−9: Nicol Bolas deals 7 damag
- oracle_text advisory -- Oko, Thief of Crowns: oracle_text diverges (similarity 0.42); scryfall='+2: Create a Food token. (It\'s an artifact with "{2}, {T}, Sacrifice this token: You gain 3 life.")\n+1: Targ
- oracle_text advisory -- Garth One-Eye: oracle_text diverges (similarity 0.39); scryfall="{T}: Choose a card name that hasn't been chosen from among Disenchant, Braingeyser, Terror, Shivan Dragon, Reg
- oracle_text advisory -- Black Lotus: oracle_text diverges (similarity 0.36); scryfall='{T}, Sacrifice this artifact: Add three mana of any one color.'
- oracle_text advisory -- Braingeyser: oracle_text diverges (similarity 0.23); scryfall='Target player draws X cards.'
- oracle_text advisory -- Terror: oracle_text diverges (similarity 0.32); scryfall="Destroy target nonartifact, nonblack creature. It can't be regenerated."
- oracle_text advisory -- Shivan Dragon: oracle_text diverges (similarity 0.32); scryfall='Flying\n{R}: This creature gets +1/+0 until end of turn.'
- oracle_text advisory -- Regrowth: oracle_text diverges (similarity 0.46); scryfall='Return target card from your graveyard to your hand.'
- oracle_text advisory -- Unite the Coalition: oracle_text diverges (similarity 0.46); scryfall="Choose five. You may choose the same mode more than once.\n• Target permanent phases out.\n• Target player dra
- oracle_text advisory -- Disenchant: oracle_text diverges (similarity 0.21); scryfall='Destroy target artifact or enchantment.'
- oracle_text advisory -- Mirrorwing Dragon: oracle_text diverges (similarity 0.42); scryfall='Flying\nWhenever a player casts an instant or sorcery spell that targets only this creature, that player copie
- oracle_text advisory -- Zada, Hedron Grinder: oracle_text diverges (similarity 0.57); scryfall='Whenever you cast an instant or sorcery spell that targets only Zada, copy that spell for each other creature 
- oracle_text advisory -- Goblin Instigator: oracle_text diverges (similarity 0.32); scryfall='When this creature enters, create a 1/1 red Goblin creature token.'
- oracle_text advisory -- Fists of Flame: oracle_text diverges (similarity 0.36); scryfall="Draw a card. Until end of turn, target creature gains trample and gets +1/+0 for each card you've drawn this t
- oracle_text advisory -- Luxurious Libation: oracle_text diverges (similarity 0.24); scryfall='Target creature gets +X/+X until end of turn. Create a 1/1 green and white Citizen creature token.'
- oracle_text advisory -- Fortifying Draught: oracle_text diverges (similarity 0.33); scryfall='You gain 2 life. Target creature gets +X/+X until end of turn, where X is the amount of life you gained this t
- oracle_text advisory -- Gold Rush: oracle_text diverges (similarity 0.36); scryfall='Create a Treasure token. Until end of turn, up to one target creature gets +2/+2 for each Treasure you control
- oracle_text advisory -- Ancestral Anger: oracle_text diverges (similarity 0.52); scryfall='Target creature gains trample and gets +X/+0 until end of turn, where X is 1 plus the number of cards named An
- oracle_text advisory -- Oracle's Restoration: oracle_text diverges (similarity 0.10); scryfall='Target creature you control gets +1/+1 until end of turn. You draw a card and gain 1 life.'
- oracle_text advisory -- Expedite: oracle_text diverges (similarity 0.29); scryfall='Target creature gains haste until end of turn.\nDraw a card.'
- oracle_text advisory -- Impolite Entrance: oracle_text diverges (similarity 0.18); scryfall='Target creature gains trample and haste until end of turn.\nDraw a card.'
- oracle_text advisory -- Scale the Heights: oracle_text diverges (similarity 0.49); scryfall='Put a +1/+1 counter on up to one target creature. You gain 2 life. You may play an additional land this turn.\
- oracle_text advisory -- Twinflame: oracle_text diverges (similarity 0.42); scryfall="Strive — This spell costs {2}{R} more to cast for each target beyond the first.\nChoose any number of target c
- oracle_text advisory -- Gruul Turf: oracle_text diverges (similarity 0.39); scryfall="This land enters tapped.\nWhen this land enters, return a land you control to its owner's hand.\n{T}: Add {R}{
- oracle_text advisory -- Kazandu Refuge: oracle_text diverges (similarity 0.50); scryfall='This land enters tapped.\nWhen this land enters, you gain 1 life.\n{T}: Add {R} or {G}.'
- oracle_text advisory -- Rootbound Crag: oracle_text diverges (similarity 0.43); scryfall='This land enters tapped unless you control a Mountain or a Forest.\n{T}: Add {R} or {G}.'
- oracle_text advisory -- Colossus Hammer: oracle_text diverges (similarity 0.25); scryfall='Equipped creature gets +10/+10 and loses flying.\nEquip {8} ({8}: Attach to target creature you control. Equip
- oracle_text advisory -- Loxodon Warhammer: oracle_text diverges (similarity 0.36); scryfall='Equipped creature gets +3/+0 and has trample and lifelink.\nEquip {3}'
- oracle_text advisory -- Shadowspear: oracle_text diverges (similarity 0.54); scryfall='Equipped creature gets +1/+1 and has trample and lifelink.\n{1}: Permanents your opponents control lose hexpro
- oracle_text advisory -- Grafted Wargear: oracle_text diverges (similarity 0.52); scryfall='Equipped creature gets +3/+2.\nWhenever this Equipment becomes unattached from a permanent, sacrifice that per
- oracle_text advisory -- O-Naginata: oracle_text diverges (similarity 0.49); scryfall='This Equipment can be attached only to a creature with power 3 or greater.\nEquipped creature gets +3/+0 and h
- oracle_text advisory -- Ancient Den: oracle_text diverges (similarity 0.02); scryfall='{T}: Add {W}.'
- oracle_text advisory -- Bone Saw: oracle_text diverges (similarity 0.29); scryfall='Equipped creature gets +1/+0.\nEquip {1} ({1}: Attach to target creature you control. Equip only as a sorcery.
- oracle_text advisory -- Cathar's Shield: oracle_text diverges (similarity 0.17); scryfall='Equipped creature gets +0/+3 and has vigilance.\nEquip {3} ({3}: Attach to target creature you control. Equip 
- oracle_text advisory -- Accorder's Shield: oracle_text diverges (similarity 0.23); scryfall="Equipped creature gets +0/+3 and has vigilance. (Attacking doesn't cause it to tap.)\nEquip {3} ({3}: Attach t
- oracle_text advisory -- Kite Shield: oracle_text diverges (similarity 0.52); scryfall='Equipped creature gets +0/+3.\nEquip {3} ({3}: Attach to target creature you control. Equip only as a sorcery.
- oracle_text advisory -- Spidersilk Net: oracle_text diverges (similarity 0.22); scryfall='Equipped creature gets +0/+2 and has reach. (It can block creatures with flying.)\nEquip {2} ({2}: Attach to t
- oracle_text advisory -- Golem-Skin Gauntlets: oracle_text diverges (similarity 0.17); scryfall='Equipped creature gets +1/+0 for each Equipment attached to it.\nEquip {2} ({2}: Attach to target creature you
- oracle_text advisory -- Skateboard: oracle_text diverges (similarity 0.19); scryfall='When this Equipment enters, tap target permanent.\nEquipped creature gets +1/+0 and has haste.\nEquip {1} ({1}
- oracle_text advisory -- Dragonfire Blade: oracle_text diverges (similarity 0.17); scryfall='Equipped creature gets +2/+2 and has hexproof from monocolored.\nEquip {4}. This ability costs {1} less to act
- oracle_text advisory -- Deconstruction Hammer: oracle_text diverges (similarity 0.17); scryfall='Equipped creature gets +1/+1 and has "{3}, {T}, Sacrifice Deconstruction Hammer: Destroy target artifact or en
- oracle_text advisory -- Sram, Senior Edificer: oracle_text diverges (similarity 0.08); scryfall='Whenever you cast an Aura, Equipment, or Vehicle spell, draw a card.'
- oracle_text advisory -- Cid, Freeflier Pilot: oracle_text diverges (similarity 0.13); scryfall='Equipment and Vehicle spells you cast cost {1} less to cast.\nJump — During your turn, Cid has flying.\n{2}, {
- oracle_text advisory -- Sigarda's Aid: oracle_text diverges (similarity 0.14); scryfall='You may cast Aura and Equipment spells as though they had flash.\nWhenever an Equipment you control enters, yo
- oracle_text advisory -- Dwalin, Weaponmaster: oracle_text diverges (similarity 0.12); scryfall='First strike\nWhenever Dwalin enters or attacks, put a hone counter on each Equipment you control. (Each hone 
- oracle_text advisory -- Ardenn, Intrepid Archaeologist: oracle_text diverges (similarity 0.17); scryfall='At the beginning of combat on your turn, you may attach any number of Auras and Equipment you control to targe
- oracle_text advisory -- Umezawa's Jitte: oracle_text diverges (similarity 0.47); scryfall="Whenever equipped creature deals combat damage, put two charge counters on Umezawa's Jitte.\nRemove a charge c
- oracle_text advisory -- Kor Duelist: oracle_text diverges (similarity 0.49); scryfall='As long as this creature is equipped, it has double strike. (It deals both first-strike and regular combat dam
- oracle_text advisory -- Puresteel Paladin: oracle_text diverges (similarity 0.34); scryfall='Whenever an Equipment you control enters, you may draw a card.\nMetalcraft — Equipment you control have equip 
- oracle_text advisory -- Balan, Wandering Knight: oracle_text diverges (similarity 0.37); scryfall='First strike\nBalan has double strike as long as two or more Equipment are attached to it.\n{1}{W}: Attach all
- oracle_text advisory -- Armored Skyhunter: oracle_text diverges (similarity 0.49); scryfall='Flying\nWhenever this creature attacks, look at the top six cards of your library. You may put an Aura or Equi
- oracle_text advisory -- Kemba, Kha Regent: oracle_text diverges (similarity 0.34); scryfall='At the beginning of your upkeep, create a 2/2 white Cat creature token for each Equipment attached to Kemba.'
- oracle_text advisory -- Stoneforge Mystic: oracle_text diverges (similarity 0.44); scryfall='When this creature enters, you may search your library for an Equipment card, reveal it, put it into your hand
- oracle_text advisory -- Unexpectedly Absent: oracle_text diverges (similarity 0.28); scryfall="Put target nonland permanent into its owner's library just beneath the top X cards of that library."
- oracle_text advisory -- Boros Garrison: oracle_text diverges (similarity 0.34); scryfall="This land enters tapped.\nWhen this land enters, return a land you control to its owner's hand.\n{T}: Add {R}{
- oracle_text advisory -- Elvish Archdruid: oracle_text diverges (similarity 0.38); scryfall='Other Elf creatures you control get +1/+1.\n{T}: Add {G} for each Elf you control.'
- oracle_text advisory -- Priest of Titania: oracle_text diverges (similarity 0.28); scryfall='{T}: Add {G} for each Elf on the battlefield.'
- oracle_text advisory -- Arbor Elf: oracle_text diverges (similarity 0.12); scryfall='{T}: Untap target Forest.'
- oracle_text advisory -- Wirewood Lodge: oracle_text diverges (similarity 0.11); scryfall='{T}: Add {C}.\n{G}, {T}: Untap target Elf.'
- oracle_text advisory -- Worldly Tutor: oracle_text diverges (similarity 0.39); scryfall='Search your library for a creature card, reveal it, then shuffle and put the card on top.'
- oracle_text advisory -- Mirri's Guile: oracle_text diverges (similarity 0.44); scryfall='At the beginning of your upkeep, you may look at the top three cards of your library, then put them back in an
- oracle_text advisory -- Call of the Wild: oracle_text diverges (similarity 0.52); scryfall="{2}{G}{G}: Reveal the top card of your library. If it's a creature card, put it onto the battlefield. Otherwis
- oracle_text advisory -- Hornet Queen: oracle_text diverges (similarity 0.41); scryfall='Flying, deathtouch\nWhen this creature enters, create four 1/1 green Insect creature tokens with flying and de
- oracle_text advisory -- Terastodon: oracle_text diverges (similarity 0.21); scryfall='When this creature enters, you may destroy up to three target noncreature permanents. For each permanent put i
- oracle_text advisory -- Elderscale Wurm: oracle_text diverges (similarity 0.53); scryfall='Trample\nWhen this creature enters, if your life total is less than 7, your life total becomes 7.\nAs long as 
- oracle_text advisory -- Craterhoof Behemoth: oracle_text diverges (similarity 0.44); scryfall='Haste\nWhen this creature enters, creatures you control gain trample and get +X/+X until end of turn, where X 
- oracle_text advisory -- Worldspine Wurm: oracle_text diverges (similarity 0.39); scryfall="Trample\nWhen this creature dies, create three 5/5 green Wurm creature tokens with trample.\nWhen Worldspine W
- oracle_text advisory -- Vaultborn Tyrant: oracle_text diverges (similarity 0.47); scryfall="Trample\nWhenever this creature or another creature you control with power 4 or greater enters, you gain 3 lif
- oracle_text advisory -- Natural Order: oracle_text diverges (similarity 0.38); scryfall='As an additional cost to cast this spell, sacrifice a green creature.\nSearch your library for a green creatur
- oracle_text advisory -- Turntimber Symbiosis: oracle_text diverges (similarity 0.45); scryfall='Look at the top seven cards of your library. You may put a creature card from among them onto the battlefield.
- oracle_text advisory -- Boros Reckoner: oracle_text diverges (similarity 0.24); scryfall='Whenever this creature is dealt damage, it deals that much damage to any target.\n{R/W}: This creature gains f
- oracle_text advisory -- Burning-Fist Minotaur: oracle_text diverges (similarity 0.17); scryfall='First strike\n{1}{R}, Discard a card: This creature gets +2/+0 until end of turn.'
- oracle_text advisory -- Deathbellow Raider: oracle_text diverges (similarity 0.16); scryfall='This creature attacks each combat if able.\n{2}{B}: Regenerate this creature.'
- oracle_text advisory -- Fanatic of Mogis: oracle_text diverges (similarity 0.39); scryfall='When this creature enters, it deals damage to each opponent equal to your devotion to red. (Each {R} in the ma
- oracle_text advisory -- Gnarled Scarhide: oracle_text diverges (similarity 0.34); scryfall="Bestow {3}{B} (If you cast this card for its bestow cost, it's an Aura spell with enchant creature. It becomes
- oracle_text advisory -- Kragma Warcaller: oracle_text diverges (similarity 0.33); scryfall='Minotaur creatures you control have haste.\nWhenever a Minotaur you control attacks, it gets +2/+0 until end o
- oracle_text advisory -- Neheb, the Worthy: oracle_text diverges (similarity 0.35); scryfall='First strike\nOther Minotaurs you control have first strike.\nAs long as you have one or fewer cards in hand, 
- oracle_text advisory -- Rageblood Shaman: oracle_text diverges (similarity 0.35); scryfall='Trample\nOther Minotaur creatures you control get +1/+1 and have trample.'
- oracle_text advisory -- Ragemonger: oracle_text diverges (similarity 0.45); scryfall='Minotaur spells you cast cost {B}{R} less to cast. This effect reduces only the amount of colored mana you pay
- oracle_text advisory -- Rakdos Carnarium: oracle_text diverges (similarity 0.36); scryfall="This land enters tapped.\nWhen this land enters, return a land you control to its owner's hand.\n{T}: Add {B}{
- oracle_text advisory -- Sethron, Hurloon General: oracle_text diverges (similarity 0.34); scryfall='Whenever Sethron or another nontoken Minotaur you control enters, create a 2/3 red Minotaur creature token.\n{
- oracle_text advisory -- Slaughter-Priest of Mogis: oracle_text diverges (similarity 0.28); scryfall='Whenever you sacrifice a permanent, this creature gets +2/+0 until end of turn.\n{2}, Sacrifice another creatu
- oracle_text advisory -- Atsushi, the Blazing Sky: oracle_text diverges (similarity 0.43); scryfall='Flying, trample\nWhen Atsushi dies, choose one —\n• Exile the top two cards of your library. Until the end of 
- oracle_text advisory -- Inferno of the Star Mounts: oracle_text diverges (similarity 0.35); scryfall="This spell can't be countered.\nFlying, haste\n{R}: Inferno of the Star Mounts gets +1/+0 until end of turn. W
- oracle_text advisory -- Dragon Tempest: oracle_text diverges (similarity 0.34); scryfall='Whenever a creature you control with flying enters, it gains haste until end of turn.\nWhenever a Dragon you c
- oracle_text advisory -- Urza's Incubator: oracle_text diverges (similarity 0.15); scryfall='As this artifact enters, choose a creature type.\nCreature spells of the chosen type cost {2} less to cast.'
- oracle_text advisory -- Mind Stone: oracle_text diverges (similarity 0.25); scryfall='{T}: Add {C}.\n{1}, {T}, Sacrifice this artifact: Draw a card.'
- oracle_text advisory -- Fire Diamond: oracle_text diverges (similarity 0.11); scryfall='This artifact enters tapped.\n{T}: Add {R}.'
- oracle_text advisory -- Dragonspeaker Shaman: oracle_text diverges (similarity 0.15); scryfall='Dragon spells you cast cost {2} less to cast.'
- oracle_text advisory -- Glorybringer: oracle_text diverges (similarity 0.46); scryfall="Flying, haste\nYou may exert this creature as it attacks. When you do, it deals 4 damage to target non-Dragon 
- oracle_text advisory -- Haven of the Spirit Dragon: oracle_text diverges (similarity 0.32); scryfall='{T}: Add {C}.\n{T}: Add one mana of any color. Spend this mana only to cast a Dragon creature spell.\n{2}, {T}
- oracle_text advisory -- Nest Invader: oracle_text diverges (similarity 0.27); scryfall='When this creature enters, create a 0/1 colorless Eldrazi Spawn creature token. It has "Sacrifice this token: 
- oracle_text advisory -- Young Pyromancer: oracle_text diverges (similarity 0.28); scryfall='Whenever you cast an instant or sorcery spell, create a 1/1 red Elemental creature token.'
- oracle_text advisory -- Undercellar Myconid: oracle_text diverges (similarity 0.39); scryfall='Whenever this creature enters or dies, create a 1/1 green Saproling creature token.\n{T}: Add one mana of any 
- oracle_text advisory -- Frontline Heroism: oracle_text diverges (similarity 0.39); scryfall='When this enchantment enters, create a 1/1 red Soldier creature token with haste.\nWhenever you cast a spell t
- oracle_text advisory -- Adarkar Wastes: oracle_text diverges (similarity 0.29); scryfall='{T}: Add {C}.\n{T}: Add {W} or {U}. This land deals 1 damage to you.'
- oracle_text advisory -- Caves of Koilos: oracle_text diverges (similarity 0.68); scryfall='{T}: Add {C}.\n{T}: Add {W} or {B}. This land deals 1 damage to you.'
- oracle_text advisory -- Yavimaya Coast: oracle_text diverges (similarity 0.68); scryfall='{T}: Add {C}.\n{T}: Add {G} or {U}. This land deals 1 damage to you.'
- oracle_text advisory -- Llanowar Wastes: oracle_text diverges (similarity 0.68); scryfall='{T}: Add {C}.\n{T}: Add {B} or {G}. This land deals 1 damage to you.'
- oracle_text advisory -- Conservatory: oracle_text diverges (similarity 0.64); scryfall='This land enters tapped.\n{T}: Add {G} or {W}.\n{4}, {T}: Investigate. (Create a Clue token. It\'s an artifact
- oracle_text advisory -- Shivan Gorge: oracle_text diverges (similarity 0.28); scryfall='{T}: Add {C}.\n{2}{R}, {T}: Shivan Gorge deals 1 damage to each opponent.'
- oracle_text advisory -- Mariposa Military Base: oracle_text diverges (similarity 0.26); scryfall='You may have this land enter tapped. If you do, you get two rad counters.\n{T}: Add {C}.\n{5}, {T}: Draw a car
- oracle_text advisory -- Eldrazi Displacer: oracle_text diverges (similarity 0.47); scryfall="Devoid (This card has no color.)\n{2}{C}: Exile another target creature, then return it to the battlefield tap
- oracle_text advisory -- Emiel the Blessed: oracle_text diverges (similarity 0.48); scryfall="{3}: Exile another target creature you control, then return it to the battlefield under its owner's control.\n
- oracle_text advisory -- Cloud of Faeries: oracle_text diverges (similarity 0.25); scryfall='Flying\nWhen this creature enters, untap up to two lands.\nCycling {2} ({2}, Discard this card: Draw a card.)'
- oracle_text advisory -- Peregrine Drake: oracle_text diverges (similarity 0.22); scryfall='Flying\nWhen this creature enters, untap up to five lands.'
- oracle_text advisory -- Wild Growth: oracle_text diverges (similarity 0.50); scryfall='Enchant land\nWhenever enchanted land is tapped for mana, its controller adds an additional {G}.'
- oracle_text advisory -- Cryptolith Rite: oracle_text diverges (similarity 0.05); scryfall='Creatures you control have "{T}: Add one mana of any color."'
- oracle_text advisory -- Overgrowth: oracle_text diverges (similarity 0.61); scryfall='Enchant land\nWhenever enchanted land is tapped for mana, its controller adds an additional {G}{G}.'
- oracle_text advisory -- Fertile Ground: oracle_text diverges (similarity 0.49); scryfall='Enchant land\nWhenever enchanted land is tapped for mana, its controller adds an additional one mana of any co
- oracle_text advisory -- Trace of Abundance: oracle_text diverges (similarity 0.34); scryfall="Enchant land\nEnchanted land has shroud. (It can't be the target of spells or abilities.)\nWhenever enchanted 
- oracle_text advisory -- Training Grounds: oracle_text diverges (similarity 0.49); scryfall="Activated abilities of creatures you control cost {2} less to activate. This effect can't reduce the mana in t
- oracle_text advisory -- Eladamri's Call: oracle_text diverges (similarity 0.61); scryfall='Search your library for a creature card, reveal that card, put it into your hand, then shuffle.'
- oracle_text advisory -- Stroke of Genius: oracle_text diverges (similarity 0.22); scryfall='Target player draws X cards.'
- oracle_text advisory -- Vexing Shusher: oracle_text diverges (similarity 0.05); scryfall="This spell can't be countered.\n{R/G}: Target spell can't be countered."
- oracle_text advisory -- Essence Depleter: oracle_text diverges (similarity 0.13); scryfall='Devoid (This card has no color.)\n{1}{C}: Target opponent loses 1 life and you gain 1 life. ({C} represents co
- oracle_text advisory -- Dimensional Infiltrator: oracle_text diverges (similarity 0.14); scryfall="Devoid (This card has no color.)\nFlash\nFlying\n{1}{C}: Target opponent exiles the top card of their library.
- oracle_text advisory -- Living Wish: oracle_text diverges (similarity 0.08); scryfall='You may reveal a creature or land card you own from outside the game and put it into your hand. Exile Living W
- oracle_text advisory -- Aether Hub: oracle_text diverges (similarity 0.08); scryfall='When this land enters, you get {E} (an energy counter).\n{T}: Add {C}.\n{T}, Pay {E}: Add one mana of any colo
- oracle_text advisory -- Maelstrom Wanderer: oracle_text diverges (similarity 0.34); scryfall='Creatures you control have haste.\nCascade, cascade (When you cast this spell, exile cards from the top of you
- oracle_text advisory -- Annoyed Altisaur: oracle_text diverges (similarity 0.50); scryfall='Reach, trample\nCascade (When you cast this spell, exile cards from the top of your library until you exile a 
- oracle_text advisory -- Sakashima's Protege: oracle_text diverges (similarity 0.28); scryfall='Flash\nCascade (When you cast this spell, exile cards from the top of your library until you exile a nonland c
- oracle_text advisory -- Boarding Party: oracle_text diverges (similarity 0.62); scryfall='Haste\nCascade (When you cast this spell, exile cards from the top of your library until you exile a nonland c
- oracle_text advisory -- Breaching Dragonstorm: oracle_text diverges (similarity 0.26); scryfall="When this enchantment enters, exile cards from the top of your library until you exile a nonland card. You may
- oracle_text advisory -- Call Forth the Tempest: oracle_text diverges (similarity 0.40); scryfall="Cascade, cascade (When you cast this spell, exile cards from the top of your library until you exile a nonland
- oracle_text advisory -- Creative Technique: oracle_text diverges (similarity 0.33); scryfall='Demonstrate (When you cast this spell, you may copy it. If you do, choose an opponent to also copy it.)\nShuff
- oracle_text advisory -- Dwarven Ruins: oracle_text diverges (similarity 0.09); scryfall='This land enters tapped.\n{T}: Add {R}.\n{T}, Sacrifice this land: Add {R}{R}.'
- oracle_text advisory -- Svyelunite Temple: oracle_text diverges (similarity 0.22); scryfall='This land enters tapped.\n{T}: Add {U}.\n{T}, Sacrifice this land: Add {U}{U}.'
- oracle_text advisory -- Hickory Woodlot: oracle_text diverges (similarity 0.24); scryfall='This land enters tapped with two depletion counters on it.\n{T}, Remove a depletion counter from this land: Ad
- oracle_text advisory -- Melira, Sylvok Outcast: oracle_text diverges (similarity 0.34); scryfall="You can't get poison counters.\nCreatures you control can't have -1/-1 counters put on them.\nCreatures your o
- oracle_text advisory -- Vizier of Remedies: oracle_text diverges (similarity 0.55); scryfall='If one or more -1/-1 counters would be put on a creature you control, that many -1/-1 counters minus one are p
- oracle_text advisory -- Kitchen Finks: oracle_text diverges (similarity 0.44); scryfall="When this creature enters, you gain 2 life.\nPersist (When this creature dies, if it had no -1/-1 counters on 
- oracle_text advisory -- Murderous Redcap: oracle_text diverges (similarity 0.51); scryfall="When this creature enters, it deals damage equal to its power to any target.\nPersist (When this creature dies
- oracle_text advisory -- Carrion Feeder: oracle_text diverges (similarity 0.28); scryfall="This creature can't block.\nSacrifice a creature: Put a +1/+1 counter on this creature."
- oracle_text advisory -- Bloodthrone Vampire: oracle_text diverges (similarity 0.26); scryfall='Sacrifice a creature: This creature gets +2/+2 until end of turn.'
- oracle_text advisory -- Recruiter of the Guard: oracle_text diverges (similarity 0.35); scryfall='When this creature enters, you may search your library for a creature card with toughness 2 or less, reveal it
- oracle_text advisory -- Ranger of Eos: oracle_text diverges (similarity 0.33); scryfall='When this creature enters, you may search your library for up to two creature cards with mana value 1 or less,
- oracle_text advisory -- Severance Priest: oracle_text diverges (similarity 0.40); scryfall="Deathtouch\nWhen this creature enters, target opponent reveals their hand. You may choose a nonland card from 
- oracle_text advisory -- Birthing Pod: oracle_text diverges (similarity 0.25); scryfall="({G/P} can be paid with either {G} or 2 life.)\n{1}{G/P}, {T}, Sacrifice a creature: Search your library for a
- oracle_text advisory -- Chord of Calling: oracle_text diverges (similarity 0.25); scryfall="Convoke (Your creatures can help cast this spell. Each creature you tap while casting this spell pays for {1} 
- oracle_text advisory -- Reveillark: oracle_text diverges (similarity 0.22); scryfall="Flying\nWhen this creature leaves the battlefield, return up to two target creature cards with power 2 or less
- oracle_text advisory -- Felidar Guardian: oracle_text diverges (similarity 0.18); scryfall="When this creature enters, you may exile another target permanent you control, then return that card to the ba
- oracle_text advisory -- Voice of Resurgence: oracle_text diverges (similarity 0.32); scryfall='Whenever an opponent casts a spell during your turn and when this creature dies, create a green and white Elem
- oracle_text advisory -- Scavenging Ooze: oracle_text diverges (similarity 0.23); scryfall='{G}: Exile target card from a graveyard. If it was a creature card, put a +1/+1 counter on this creature and y
- oracle_text advisory -- Ravenous Chupacabra: oracle_text diverges (similarity 0.18); scryfall='When this creature enters, destroy target creature an opponent controls.'
- oracle_text advisory -- Reclamation Sage: oracle_text diverges (similarity 0.16); scryfall='When this creature enters, you may destroy target artifact or enchantment.'
- oracle_text advisory -- Celes, Rune Knight: oracle_text diverges (similarity 0.25); scryfall='When Celes enters, discard any number of cards, then draw that many cards plus one.\nWhenever one or more othe
- oracle_text advisory -- Drifting Meadow: oracle_text diverges (similarity 0.33); scryfall='This land enters tapped.\n{T}: Add {W}.\nCycling {2} ({2}, Discard this card: Draw a card.)'
- oracle_text advisory -- Polluted Mire: oracle_text diverges (similarity 0.33); scryfall='This land enters tapped.\n{T}: Add {B}.\nCycling {2} ({2}, Discard this card: Draw a card.)'
- oracle_text advisory -- Smoldering Crater: oracle_text diverges (similarity 0.33); scryfall='This land enters tapped.\n{T}: Add {R}.\nCycling {2} ({2}, Discard this card: Draw a card.)'
- oracle_text advisory -- Blasted Landscape: oracle_text diverges (similarity 0.30); scryfall='{T}: Add {C}.\nCycling {2} ({2}, Discard this card: Draw a card.)'
- oracle_text advisory -- Fetid Pools: oracle_text diverges (similarity 0.39); scryfall='({T}: Add {U} or {B}.)\nThis land enters tapped.\nCycling {2} ({2}, Discard this card: Draw a card.)'
- oracle_text advisory -- Irrigated Farmland: oracle_text diverges (similarity 0.39); scryfall='({T}: Add {W} or {U}.)\nThis land enters tapped.\nCycling {2} ({2}, Discard this card: Draw a card.)'
- oracle_text advisory -- Canyon Slough: oracle_text diverges (similarity 0.39); scryfall='({T}: Add {B} or {R}.)\nThis land enters tapped.\nCycling {2} ({2}, Discard this card: Draw a card.)'
- oracle_text advisory -- Sheltered Thicket: oracle_text diverges (similarity 0.39); scryfall='({T}: Add {R} or {G}.)\nThis land enters tapped.\nCycling {2} ({2}, Discard this card: Draw a card.)'
- oracle_text advisory -- Scattered Groves: oracle_text diverges (similarity 0.39); scryfall='({T}: Add {G} or {W}.)\nThis land enters tapped.\nCycling {2} ({2}, Discard this card: Draw a card.)'
- oracle_text advisory -- Glittering Massif: oracle_text diverges (similarity 0.39); scryfall='({T}: Add {R} or {W}.)\nThis land enters tapped.\nCycling {2} ({2}, Discard this card: Draw a card.)'
- oracle_text advisory -- Festering Thicket: oracle_text diverges (similarity 0.39); scryfall='({T}: Add {B} or {G}.)\nThis land enters tapped.\nCycling {2} ({2}, Discard this card: Draw a card.)'
- oracle_text advisory -- Capital City: oracle_text diverges (similarity 0.11); scryfall='{T}: Add {C}.\n{1}, {T}: Add one mana of any color.\nCycling {2} ({2}, Discard this card: Draw a card.)'
- oracle_text advisory -- Forsake the Worldly: oracle_text diverges (similarity 0.22); scryfall='Exile target artifact or enchantment.\nCycling {2} ({2}, Discard this card: Draw a card.)'
- oracle_text advisory -- Fluctuator: oracle_text diverges (similarity 0.17); scryfall='Cycling abilities you activate cost {2} less to activate.'
- oracle_text advisory -- Drannith Stinger: oracle_text diverges (similarity 0.26); scryfall='Whenever you cycle another card, this creature deals 1 damage to each opponent.\nCycling {1} ({1}, Discard thi
- oracle_text advisory -- Hollow One: oracle_text diverges (similarity 0.20); scryfall="This spell costs {2} less to cast for each card you've cycled or discarded this turn.\nCycling {2} ({2}, Disca
- oracle_text advisory -- Unearth: oracle_text diverges (similarity 0.24); scryfall='Return target creature card with mana value 3 or less from your graveyard to the battlefield.\nCycling {2} ({2
- oracle_text advisory -- Scrying Sheets: oracle_text diverges (similarity 0.28); scryfall='{T}: Add {C}.\n{1}{S}, {T}: Look at the top card of your library. If that card is snow, you may reveal it and 
- oracle_text advisory -- Skred: oracle_text diverges (similarity 0.23); scryfall='Skred deals damage to target creature equal to the number of snow permanents you control.'
- oracle_text advisory -- Coldsteel Heart: oracle_text diverges (similarity 0.11); scryfall='This artifact enters tapped.\nAs this artifact enters, choose a color.\n{T}: Add one mana of the chosen color.
- oracle_text advisory -- Abominable Treefolk: oracle_text diverges (similarity 0.35); scryfall="Trample\nAbominable Treefolk's power and toughness are each equal to the number of snow permanents you control
- oracle_text advisory -- Arcum's Astrolabe: oracle_text diverges (similarity 0.25); scryfall='({S} can be paid with one mana from a snow source.)\nWhen this artifact enters, draw a card.\n{1}, {T}: Add on
- oracle_text advisory -- Frost Augur: oracle_text diverges (similarity 0.39); scryfall="{S}, {T}: Look at the top card of your library. If it's a snow card, you may reveal it and put it into your ha
- oracle_text advisory -- Ice-Fang Coatl: oracle_text diverges (similarity 0.21); scryfall='Flash\nFlying\nWhen this creature enters, draw a card.\nThis creature has deathtouch as long as you control at
- oracle_text advisory -- Jorn, God of Winter: oracle_text diverges (similarity 0.10); scryfall='Whenever Jorn attacks, untap each snow permanent you control.'
- oracle_text advisory -- Kaldring, the Rimestaff: oracle_text diverges (similarity 0.17); scryfall='{T}: You may play target snow permanent card from your graveyard this turn. If you do, it enters tapped.'
- oracle_text advisory -- Marit Lage's Slumber: oracle_text diverges (similarity 0.35); scryfall="Whenever Marit Lage's Slumber or another snow permanent you control enters, scry 1.\nAt the beginning of your 
- oracle_text advisory -- Rimefeather Owl: oracle_text diverges (similarity 0.26); scryfall="Flying\nRimefeather Owl's power and toughness are each equal to the number of snow permanents on the battlefie
- oracle_text advisory -- Rimescale Dragon: oracle_text diverges (similarity 0.29); scryfall="Flying\n{2}{S}: Tap target creature and put an ice counter on it. ({S} can be paid with one mana from a snow s
- oracle_text advisory -- Soul's Attendant: oracle_text diverges (similarity 0.12); scryfall='Whenever another creature enters, you may gain 1 life.'
- oracle_text advisory -- Auriok Champion: oracle_text diverges (similarity 0.11); scryfall='Protection from black and from red\nWhenever another creature enters, you may gain 1 life.'
- oracle_text advisory -- Ocelot Pride: oracle_text diverges (similarity 0.13); scryfall="First strike, lifelink\nAscend (If you control ten or more permanents, you get the city's blessing for the res
- oracle_text advisory -- Serra Ascendant: oracle_text diverges (similarity 0.21); scryfall='Lifelink (Damage dealt by this creature also causes you to gain that much life.)\nAs long as you have 30 or mo
- oracle_text advisory -- Ajani's Pridemate: oracle_text diverges (similarity 0.09); scryfall='Whenever you gain life, put a +1/+1 counter on this creature.'
- oracle_text advisory -- Voice of the Blessed: oracle_text diverges (similarity 0.26); scryfall='Whenever you gain life, put a +1/+1 counter on this creature.\nAs long as this creature has four or more +1/+1
- oracle_text advisory -- Daxos, Blessed by the Sun: oracle_text diverges (similarity 0.18); scryfall="Daxos's toughness is equal to your devotion to white. (Each {W} in the mana costs of permanents you control co
- oracle_text advisory -- Archangel of Thune: oracle_text diverges (similarity 0.24); scryfall='Flying\nLifelink (Damage dealt by this creature also causes you to gain that much life.)\nWhenever you gain li
- oracle_text advisory -- Heliod, Sun-Crowned: oracle_text diverges (similarity 0.13); scryfall="Indestructible\nAs long as your devotion to white is less than five, Heliod isn't a creature.\nWhenever you ga
- oracle_text advisory -- Ranger-Captain of Eos: oracle_text diverges (similarity 0.13); scryfall="When this creature enters, you may search your library for a creature card with mana value 1 or less, reveal i
- oracle_text advisory -- Ajani, Strength of the Pride: oracle_text diverges (similarity 0.27); scryfall='+1: You gain life equal to the number of creatures you control plus the number of planeswalkers you control.\n
- oracle_text advisory -- Apex Altisaur: oracle_text diverges (similarity 0.21); scryfall="When this creature enters, it fights up to one target creature you don't control.\nEnrage — Whenever this crea
- oracle_text advisory -- World War Hulk: oracle_text diverges (similarity 0.17); scryfall='(As this Saga enters and after your draw step, add a lore counter. Sacrifice after III.)\nI — The next red or 
- oracle_text advisory -- Ghalta, Stampede Tyrant: oracle_text diverges (similarity 0.17); scryfall='Trample\nWhen Ghalta enters, put any number of creature cards from your hand onto the battlefield.'
- oracle_text advisory -- Lyra Dawnbringer: oracle_text diverges (similarity 0.13); scryfall='Flying\nFirst strike (This creature deals combat damage before creatures without first strike.)\nLifelink (Dam
- oracle_text advisory -- Youthful Valkyrie: oracle_text diverges (similarity 0.08); scryfall='Flying\nWhenever another Angel you control enters, put a +1/+1 counter on this creature.'
- oracle_text advisory -- Bishop of Wings: oracle_text diverges (similarity 0.10); scryfall='Whenever an Angel you control enters, you gain 4 life.\nWhenever an Angel you control dies, create a 1/1 white
- oracle_text advisory -- Righteous Valkyrie: oracle_text diverges (similarity 0.15); scryfall="Flying\nWhenever another Angel or Cleric you control enters, you gain life equal to that creature's toughness.
- oracle_text advisory -- Seraph Sanctuary: oracle_text diverges (similarity 0.12); scryfall='When this land enters, you gain 1 life.\nWhenever an Angel you control enters, you gain 1 life.\n{T}: Add {C}.
- oracle_text advisory -- Resplendent Angel: oracle_text diverges (similarity 0.11); scryfall='Flying\nAt the beginning of each end step, if you gained 5 or more life this turn, create a 4/4 white Angel cr
- oracle_text advisory -- Legion Angel: oracle_text diverges (similarity 0.07); scryfall='Flying\nWhen this creature enters, you may reveal a card you own named Legion Angel from outside the game and 
- oracle_text advisory -- Serra the Benevolent: oracle_text diverges (similarity 0.16); scryfall='+2: Creatures you control with flying get +1/+1 until end of turn.\n−3: Create a 4/4 white Angel creature toke
- oracle_text advisory -- Giada, Font of Hope: oracle_text diverges (similarity 0.11); scryfall='Flying, vigilance\nEach other Angel you control enters with an additional +1/+1 counter on it for each Angel y
- oracle_text advisory -- Saproling Burst: oracle_text diverges (similarity 0.13); scryfall='Fading 7 (This enchantment enters with seven fade counters on it. At the beginning of your upkeep, remove a fa
- oracle_text advisory -- Shroofus Sproutsire: oracle_text diverges (similarity 0.06); scryfall='Trample\nWhenever a Saproling you control deals combat damage to a player, create that many 1/1 green Saprolin
- oracle_text advisory -- Slimefoot, the Stowaway: oracle_text diverges (similarity 0.05); scryfall='Whenever a Saproling you control dies, Slimefoot deals 1 damage to each opponent and you gain 1 life.\n{4}: Cr
- oracle_text advisory -- Vitaspore Thallid: oracle_text diverges (similarity 0.11); scryfall='At the beginning of your upkeep, put a spore counter on this creature.\nRemove three spore counters from this 
- oracle_text advisory -- Deathspore Thallid: oracle_text diverges (similarity 0.10); scryfall='At the beginning of your upkeep, put a spore counter on this creature.\nRemove three spore counters from this 
- oracle_text advisory -- Sporecrown Thallid: oracle_text diverges (similarity 0.22); scryfall="Each other creature you control that's a Fungus or Saproling gets +1/+1."
- oracle_text advisory -- Tukatongue Thallid: oracle_text diverges (similarity 0.13); scryfall='When this creature dies, create a 1/1 green Saproling creature token.'
- oracle_text advisory -- Yavimaya Sapherd: oracle_text diverges (similarity 0.06); scryfall='When this creature enters, create a 1/1 green Saproling creature token.'
- oracle_text advisory -- Deathbloom Thallid: oracle_text diverges (similarity 0.05); scryfall='When this creature dies, create a 1/1 green Saproling creature token.'
- oracle_text advisory -- The Mycotyrant: oracle_text diverges (similarity 0.18); scryfall='Trample\nThe Mycotyrant\'s power and toughness are each equal to the number of creatures you control that are 
- oracle_text advisory -- Simic Growth Chamber: oracle_text diverges (similarity 0.21); scryfall="This land enters tapped.\nWhen this land enters, return a land you control to its owner's hand.\n{T}: Add {G}{
- oracle_text advisory -- Psychotrope Thallid: oracle_text diverges (similarity 0.22); scryfall='At the beginning of your upkeep, put a spore counter on this creature.\nRemove three spore counters from this 
- oracle_text advisory -- Concordant Crossroads: oracle_text diverges (similarity 0.01); scryfall='All creatures have haste.'
- oracle_text advisory -- Brightcap Badger: oracle_text diverges (similarity 0.12); scryfall='Each Fungus and Saproling you control has "{T}: Add {G}."\nAt the beginning of your end step, create a 1/1 gre
- oracle_text advisory -- Fungus Frolic: oracle_text diverges (similarity 0.10); scryfall='Create two 1/1 green Saproling creature tokens. (Then exile this card. You may cast the creature later from ex
- oracle_text advisory -- Spore Swarm: oracle_text diverges (similarity 0.06); scryfall='Create three 1/1 green Saproling creature tokens.'
- oracle_text advisory -- Saproling Migration: oracle_text diverges (similarity 0.13); scryfall='Kicker {4} (You may pay an additional {4} as you cast this spell.)\nCreate two 1/1 green Saproling creature to
- oracle_text advisory -- Doubling Season: oracle_text diverges (similarity 0.28); scryfall='If an effect would create one or more tokens under your control, it creates twice that many of those tokens in
- oracle_text advisory -- Parallel Lives: oracle_text diverges (similarity 0.13); scryfall='If an effect would create one or more tokens under your control, it creates twice that many of those tokens in
- oracle_text advisory -- Primal Vigor: oracle_text diverges (similarity 0.15); scryfall='If one or more tokens would be created, twice that many of those tokens are created instead.\nIf one or more +
- oracle_text advisory -- Beastmaster Ascension: oracle_text diverges (similarity 0.17); scryfall='Whenever a creature you control attacks, you may put a quest counter on this enchantment.\nAs long as this enc
- oracle_text advisory -- Thallid: oracle_text diverges (similarity 0.17); scryfall='At the beginning of your upkeep, put a spore counter on this creature.\nRemove three spore counters from this 
- oracle_text advisory -- Thallid Shell-Dweller: oracle_text diverges (similarity 0.13); scryfall='Defender\nAt the beginning of your upkeep, put a spore counter on this creature.\nRemove three spore counters 
- oracle_text advisory -- Sporesower Thallid: oracle_text diverges (similarity 0.12); scryfall='At the beginning of your upkeep, put a spore counter on each Fungus you control.\nRemove three spore counters 
- oracle_text advisory -- Utopia Mycon: oracle_text diverges (similarity 0.12); scryfall='At the beginning of your upkeep, put a spore counter on this creature.\nRemove three spore counters from this 
- oracle_text advisory -- Mycoloth: oracle_text diverges (similarity 0.14); scryfall='Devour 2 (As this creature enters, you may sacrifice any number of creatures. It enters with twice that many +
- oracle_text advisory -- Lightstall Inquisitor: oracle_text diverges (similarity 0.17); scryfall='Vigilance\nWhen this creature enters, each opponent exiles a card from their hand and may play that card for a
- oracle_text advisory -- Lyra, Archangel of Dawn: oracle_text diverges (similarity 0.06); scryfall='Flying\nWhenever you gain life, put a +1/+1 counter on each Angel you control.'
- oracle_text advisory -- Sunrise Sovereign: oracle_text diverges (similarity 0.10); scryfall='Other Giant creatures you control get +2/+2 and have trample.'
- oracle_text advisory -- Stinkdrinker Daredevil: oracle_text diverges (similarity 0.11); scryfall='Giant spells you cast cost {2} less to cast.'
- oracle_text advisory -- Giant Harbinger: oracle_text diverges (similarity 0.14); scryfall='When this creature enters, you may search your library for a Giant card, reveal it, then shuffle and put that 
- oracle_text advisory -- Pyroclasm: oracle_text diverges (similarity 0.05); scryfall='Pyroclasm deals 2 damage to each creature.'
- oracle_text advisory -- Hamletback Goliath: oracle_text diverges (similarity 0.09); scryfall="Whenever another creature enters, you may put X +1/+1 counters on this creature, where X is that creature's po
- oracle_text advisory -- Borderland Behemoth: oracle_text diverges (similarity 0.06); scryfall='Trample\nThis creature gets +4/+4 for each other Giant you control.'
- oracle_text advisory -- Inferno Titan: oracle_text diverges (similarity 0.13); scryfall='{R}: This creature gets +1/+0 until end of turn.\nWhenever this creature enters or attacks, it deals 3 damage 
- oracle_text advisory -- Surtland Flinger: oracle_text diverges (similarity 0.18); scryfall="Whenever this creature attacks, you may sacrifice another creature. When you do, this creature deals damage eq
- oracle_text advisory -- Tectonic Giant: oracle_text diverges (similarity 0.16); scryfall='Whenever this creature attacks or becomes the target of a spell an opponent controls, choose one —\n• This cre
- oracle_text advisory -- Metallic Mimic: oracle_text diverges (similarity 0.18); scryfall='As this creature enters, choose a creature type.\nThis creature is the chosen type in addition to its other ty
- oracle_text advisory -- Adaptive Automaton: oracle_text diverges (similarity 0.23); scryfall='As this creature enters, choose a creature type.\nThis creature is the chosen type in addition to its other ty
- oracle_text advisory -- Corsair Captain: oracle_text diverges (similarity 0.18); scryfall='When this creature enters, create a Treasure token. (It\'s an artifact with "{T}, Sacrifice this token: Add on
- oracle_text advisory -- Dire Fleet Captain: oracle_text diverges (similarity 0.17); scryfall='Whenever this creature attacks, it gets +1/+1 until end of turn for each other attacking Pirate.'
- oracle_text advisory -- Goblin Tomb Raider: oracle_text diverges (similarity 0.09); scryfall='As long as you control an artifact, this creature gets +1/+0 and has haste.'
- oracle_text advisory -- Forerunner of the Coalition: oracle_text diverges (similarity 0.22); scryfall='When this creature enters, you may search your library for a Pirate card, reveal it, then shuffle and put that
- oracle_text advisory -- Staunch Crewmate: oracle_text diverges (similarity 0.33); scryfall='When this creature enters, look at the top four cards of your library. You may reveal an artifact or Pirate ca
- oracle_text advisory -- Malcolm, the Eyes: oracle_text diverges (similarity 0.26); scryfall='Flying, haste\nWhenever you cast your second spell each turn, investigate. (Create a Clue token. It\'s an arti
- oracle_text advisory -- Siren Stormtamer: oracle_text diverges (similarity 0.16); scryfall='Flying\n{U}, Sacrifice this creature: Counter target spell or ability that targets you or a creature you contr
- oracle_text advisory -- Daring Buccaneer: oracle_text diverges (similarity 0.09); scryfall='As an additional cost to cast this spell, reveal a Pirate card from your hand or pay {2}.'
- oracle_text advisory -- Kitesail Larcenist: oracle_text diverges (similarity 0.17); scryfall='Flying, ward {1}\nWhen this creature enters, for each player, choose up to one other target artifact or creatu
- oracle_text advisory -- Spirebluff Canal: oracle_text diverges (similarity 0.33); scryfall='This land enters tapped unless you control two or fewer other lands.\n{T}: Add {U} or {R}.'
- oracle_text advisory -- Blackcleave Cliffs: oracle_text diverges (similarity 0.33); scryfall='This land enters tapped unless you control two or fewer other lands.\n{T}: Add {B} or {R}.'
- oracle_text advisory -- Battlefield Forge: oracle_text diverges (similarity 0.18); scryfall='{T}: Add {C}.\n{T}: Add {R} or {W}. This land deals 1 damage to you.'
- oracle_text advisory -- Karplusan Forest: oracle_text diverges (similarity 0.43); scryfall='{T}: Add {C}.\n{T}: Add {R} or {G}. This land deals 1 damage to you.'
- oracle_text advisory -- Tarnished Citadel: oracle_text diverges (similarity 0.28); scryfall='{T}: Add {C}.\n{T}: Add one mana of any color. This land deals 3 damage to you.'
- oracle_text advisory -- Grand Coliseum: oracle_text diverges (similarity 0.53); scryfall='This land enters tapped.\n{T}: Add {C}.\n{T}: Add one mana of any color. This land deals 1 damage to you.'
- oracle_text advisory -- Ancient Tomb: oracle_text diverges (similarity 0.17); scryfall='{T}: Add {C}{C}. This land deals 2 damage to you.'
- oracle_text advisory -- Manabarbs: oracle_text diverges (similarity 0.12); scryfall='Whenever a player taps a land for mana, this enchantment deals 1 damage to that player.'
- oracle_text advisory -- Tamanoa: oracle_text diverges (similarity 0.13); scryfall='Whenever a noncreature source you control deals damage, you gain that much life.'
- oracle_text advisory -- Rhox Faithmender: oracle_text diverges (similarity 0.37); scryfall='Lifelink (Damage dealt by this creature also causes you to gain that much life.)\nIf you would gain life, you 
- oracle_text advisory -- Vito, Thorn of the Dusk Rose: oracle_text diverges (similarity 0.16); scryfall='Whenever you gain life, target opponent loses that much life.\n{3}{B}{B}: Creatures you control gain lifelink 
- oracle_text advisory -- Dina, Soul Steeper: oracle_text diverges (similarity 0.21); scryfall="Whenever you gain life, each opponent loses 1 life.\n{1}, Sacrifice another creature: Dina gets +X/+0 until en
- oracle_text advisory -- Bilbo, Birthday Celebrant: oracle_text diverges (similarity 0.24); scryfall='If you would gain life, you gain that much life plus 1 instead.\n{2}{W}{B}{G}, {T}, Exile Bilbo: Search your l
- oracle_text advisory -- Purity: oracle_text diverges (similarity 0.24); scryfall="Flying\nIf noncombat damage would be dealt to you, prevent that damage. You gain life equal to the damage prev
- oracle_text advisory -- Spellshock: oracle_text diverges (similarity 0.09); scryfall='Whenever a player casts a spell, this enchantment deals 2 damage to that player.'
- oracle_text advisory -- Pyrohemia: oracle_text diverges (similarity 0.11); scryfall='At the beginning of the end step, if no creatures are on the battlefield, sacrifice this enchantment.\n{R}: Th
- oracle_text advisory -- Rolling Earthquake: oracle_text diverges (similarity 0.08); scryfall='Rolling Earthquake deals X damage to each creature without horsemanship and each player.'
- oracle_text advisory -- Beseech the Queen: oracle_text diverges (similarity 0.20); scryfall="({2/B} can be paid with any two mana or with {B}. This card's mana value is 6.)\nSearch your library for a car
- oracle_text advisory -- Green Sun's Zenith: oracle_text diverges (similarity 0.17); scryfall="Search your library for a green creature card with mana value X or less, put it onto the battlefield, then shu
- oracle_text advisory -- Dimir House Guard: oracle_text diverges (similarity 0.25); scryfall="Fear (This creature can't be blocked except by artifact creatures and/or black creatures.)\nSacrifice a creatu
- oracle_text advisory -- Shriekmaw: oracle_text diverges (similarity 0.26); scryfall="Fear (This creature can't be blocked except by artifact creatures and/or black creatures.)\nWhen this creature
- oracle_text advisory -- Timeless Witness: oracle_text diverges (similarity 0.29); scryfall="When this creature enters, return target card from your graveyard to your hand.\nEternalize {5}{G}{G} ({5}{G}{
- oracle_text advisory -- Acidic Slime: oracle_text diverges (similarity 0.25); scryfall='Deathtouch (Any amount of damage this deals to a creature is enough to destroy it.)\nWhen this creature enters
- oracle_text advisory -- Ageless Entity: oracle_text diverges (similarity 0.03); scryfall='Whenever you gain life, put that many +1/+1 counters on this creature.'
- oracle_text advisory -- Selesnya Sanctuary: oracle_text diverges (similarity 0.06); scryfall="This land enters tapped.\nWhen this land enters, return a land you control to its owner's hand.\n{T}: Add {G}{
- oracle_text advisory -- Blossoming Sands: oracle_text diverges (similarity 0.06); scryfall='This land enters tapped.\nWhen this land enters, you gain 1 life.\n{T}: Add {G} or {W}.'
- oracle_text advisory -- Verdant Sun's Avatar: oracle_text diverges (similarity 0.05); scryfall="Whenever this creature or another creature you control enters, you gain life equal to that creature's toughnes
- oracle_text advisory -- Feed the Clan: oracle_text diverges (similarity 0.04); scryfall='You gain 5 life.\nFerocious — You gain 10 life instead if you control a creature with power 4 or greater.'
- oracle_text advisory -- Accomplished Alchemist: oracle_text diverges (similarity 0.04); scryfall='{T}: Add one mana of any color.\n{T}: Add X mana of any one color, where X is the amount of life you gained th
- oracle_text advisory -- Blighted Steppe: oracle_text diverges (similarity 0.04); scryfall='{T}: Add {C}.\n{3}{W}, {T}, Sacrifice this land: You gain 2 life for each creature you control.'
- oracle_text advisory -- Wellwisher: oracle_text diverges (similarity 0.02); scryfall='{T}: You gain 1 life for each Elf on the battlefield.'
- oracle_text advisory -- Nykthos Paragon: oracle_text diverges (similarity 0.03); scryfall='Whenever you gain life, you may put that many +1/+1 counters on each creature you control. Do this only once e
- oracle_text advisory -- Blossoming Bogbeast: oracle_text diverges (similarity 0.05); scryfall='Whenever this creature attacks, you gain 2 life. Then creatures you control gain trample and get +X/+X until e
- oracle_text advisory -- Genesis Wave: oracle_text diverges (similarity 0.07); scryfall="Reveal the top X cards of your library. You may put any number of permanent cards with mana value X or less fr
- oracle_text advisory -- Champion of the Parish: oracle_text diverges (similarity 0.11); scryfall='Whenever another Human you control enters, put a +1/+1 counter on this creature.'
- oracle_text advisory -- Field Marshal: oracle_text diverges (similarity 0.13); scryfall='Other Soldier creatures get +1/+1 and have first strike. (They deal combat damage before creatures without fir
- oracle_text advisory -- Thalia, Guardian of Thraben: oracle_text diverges (similarity 0.06); scryfall='First strike\nNoncreature spells cost {1} more to cast.'
- oracle_text advisory -- Thalia's Lieutenant: oracle_text diverges (similarity 0.12); scryfall='When this creature enters, put a +1/+1 counter on each other Human you control.\nWhenever another Human you co
- oracle_text advisory -- Rick, Steadfast Leader: oracle_text diverges (similarity 0.16); scryfall="As Greymond, Avacyn's Stalwart enters, choose two abilities from among first strike, vigilance, and lifelink.\
- oracle_text advisory -- Esper Sentinel: oracle_text diverges (similarity 0.14); scryfall="Whenever an opponent casts their first noncreature spell each turn, draw a card unless that player pays {X}, w
- oracle_text advisory -- Coppercoat Vanguard: oracle_text diverges (similarity 0.17); scryfall='Each other Human you control gets +1/+0 and has ward {1}. (Whenever it becomes the target of a spell or abilit
- oracle_text advisory -- Recruitment Officer: oracle_text diverges (similarity 0.17); scryfall='{3}{W}: Look at the top four cards of your library. You may reveal a creature card with mana value 3 or less f
- oracle_text advisory -- Jirina, Dauntless General: oracle_text diverges (similarity 0.09); scryfall="When Jirina enters, exile target player's graveyard.\nSacrifice Jirina: Humans you control gain hexproof and i
- oracle_text advisory -- Harbin, Vanguard Aviator: oracle_text diverges (similarity 0.08); scryfall='Flying\nWhenever you attack with five or more Soldiers, creatures you control get +1/+1 and gain flying until 
- oracle_text advisory -- General Kudro of Drannith: oracle_text diverges (similarity 0.19); scryfall="Other Humans you control get +1/+1.\nWhenever General Kudro or another Human you control enters, exile target 
- oracle_text advisory -- Cathar Commando: oracle_text diverges (similarity 0.06); scryfall='Flash\n{1}, Sacrifice this creature: Destroy target artifact or enchantment.'
- oracle_text advisory -- Brutal Cathar: oracle_text diverges (similarity 0.10); scryfall='Whenever this creature enters or transforms into Brutal Cathar, exile target creature an opponent controls unt
- oracle_text advisory -- Moonrage Brute: oracle_text diverges (similarity 0.25); scryfall='First strike\nWard—Pay 3 life.\nNightbound (If a player casts at least two spells during their own turn, it be
- oracle_text advisory -- King Darien XLVIII: oracle_text diverges (similarity 0.17); scryfall='Other creatures you control get +1/+1.\n{3}{G}{W}: Put a +1/+1 counter on King Darien and create a 1/1 white S
- oracle_text advisory -- Fortified Beachhead: oracle_text diverges (similarity 0.26); scryfall='As this land enters, you may reveal a Soldier card from your hand. This land enters tapped unless you revealed
- oracle_text advisory -- Silent Clearing: oracle_text diverges (similarity 0.10); scryfall='{T}, Pay 1 life: Add {W} or {B}.\n{1}, {T}, Sacrifice this land: Draw a card.'
- oracle_text advisory -- Adelbert Steiner: oracle_text diverges (similarity 0.02); scryfall='Lifelink\nAdelbert Steiner gets +1/+1 for each Equipment you control.'
- oracle_text advisory -- Adaptive Omnitool: oracle_text diverges (similarity 0.07); scryfall='Equipped creature gets +1/+1 for each artifact you control.\nWhenever equipped creature attacks, look at the t
- oracle_text advisory -- Kíli the Resourceful: oracle_text diverges (similarity 0.07); scryfall='Storied (If you control three or more artifacts, legendaries, and/or Sagas, you have an enduring story for the
- oracle_text advisory -- Almost Perfect: oracle_text diverges (similarity 0.20); scryfall='Enchant creature\nEnchanted creature has base power and toughness 9/10 and has indestructible.'
- oracle_text advisory -- Arcanum Wings: oracle_text diverges (similarity 0.15); scryfall='Enchant creature\nEnchanted creature has flying.\nAura swap {2}{U} ({2}{U}: Exchange this Aura with an Aura ca
- oracle_text advisory -- Boseiju, Who Endures: oracle_text diverges (similarity 0.38); scryfall='{T}: Add {G}.\nChannel — {1}{G}, Discard this card: Destroy target artifact, enchantment, or nonbasic land an 
- oracle_text advisory -- Botanical Sanctum: oracle_text diverges (similarity 0.29); scryfall='This land enters tapped unless you control two or fewer other lands.\n{T}: Add {G} or {U}.'
- oracle_text advisory -- Bruna, Light of Alabaster: oracle_text diverges (similarity 0.24); scryfall='Flying, vigilance\nWhenever Bruna attacks or blocks, you may attach to it any number of Auras on the battlefie
- oracle_text advisory -- Colossification: oracle_text diverges (similarity 0.14); scryfall='Enchant creature\nWhen this Aura enters, tap enchanted creature.\nEnchanted creature gets +20/+20.'
- oracle_text advisory -- Eldrazi Conscription: oracle_text diverges (similarity 0.39); scryfall='Enchant creature\nEnchanted creature gets +10/+10 and has trample and annihilator 2. (Whenever it attacks, def
- oracle_text advisory -- Glittering Wish: oracle_text diverges (similarity 0.06); scryfall='You may reveal a multicolored card you own from outside the game and put it into your hand. Exile Glittering W
- oracle_text advisory -- Indrik Umbra: oracle_text diverges (similarity 0.46); scryfall='Enchant creature\nEnchanted creature gets +4/+4 and has first strike, and all creatures able to block it do so
- oracle_text advisory -- Mother of Runes: oracle_text diverges (similarity 0.13); scryfall='{T}: Target creature you control gains protection from the color of your choice until end of turn.'
- oracle_text advisory -- Mythic Proportions: oracle_text diverges (similarity 0.40); scryfall='Enchant creature\nEnchanted creature gets +8/+8 and has trample.'
- oracle_text advisory -- Open the Armory: oracle_text diverges (similarity 0.21); scryfall='Search your library for an Aura or Equipment card, reveal it, put it into your hand, then shuffle.'
- oracle_text advisory -- Prodigious Growth: oracle_text diverges (similarity 0.26); scryfall='Enchant creature\nEnchanted creature gets +7/+7 and has trample.'
- oracle_text advisory -- Seaside Citadel: oracle_text diverges (similarity 0.15); scryfall='This land enters tapped.\n{T}: Add {G}, {W}, or {U}.'
- oracle_text advisory -- Skycloud Expanse: oracle_text diverges (similarity 0.06); scryfall='{1}, {T}: Add {W}{U}.'
- oracle_text advisory -- Somberwald Sage: oracle_text diverges (similarity 0.12); scryfall='{T}: Add three mana of any one color. Spend this mana only to cast creature spells.'
- oracle_text advisory -- Unflinching Courage: oracle_text diverges (similarity 0.43); scryfall='Enchant creature\nEnchanted creature gets +2/+2 and has trample and lifelink. (Damage dealt by the creature al
- oracle_text advisory -- Linvala, Shield of Sea Gate: oracle_text diverges (similarity 0.31); scryfall="Flying\nAt the beginning of combat on your turn, if you have a full party, choose target nonland permanent an 
- oracle_text advisory -- Troyan, Gutsy Explorer: oracle_text diverges (similarity 0.15); scryfall='{T}: Add {G}{U}. Spend this mana only to cast spells with mana value 5 or greater or spells with {X} in their 
- oracle_text advisory -- Detention Sphere: oracle_text diverges (similarity 0.25); scryfall="When this enchantment enters, you may exile target nonland permanent not named Detention Sphere and all other 
- oracle_text advisory -- Steel of the Godhead: oracle_text diverges (similarity 0.31); scryfall="Enchant creature\nAs long as enchanted creature is white, it gets +1/+1 and has lifelink. (Damage dealt by the
- oracle_text advisory -- Auroral Procession: oracle_text diverges (similarity 0.07); scryfall='Return target card from your graveyard to your hand.'
- oracle_text advisory -- Reborn Hope: oracle_text diverges (similarity 0.10); scryfall='Return target multicolored card from your graveyard to your hand.'
- oracle_text advisory -- Rampaging Baloths: oracle_text diverges (similarity 0.14); scryfall='Trample\nLandfall — Whenever a land you control enters, create a 4/4 green Beast creature token.'
- oracle_text advisory -- Avenger of Zendikar: oracle_text diverges (similarity 0.21); scryfall='When this creature enters, create a 0/1 green Plant creature token for each land you control.\nLandfall — When
- oracle_text advisory -- Omnath, Locus of Rage: oracle_text diverges (similarity 0.20); scryfall='Landfall — Whenever a land you control enters, create a 5/5 red and green Elemental creature token.\nWhenever 
- oracle_text advisory -- Phylath, World Sculptor: oracle_text diverges (similarity 0.19); scryfall='When Phylath enters, create a 0/1 green Plant creature token for each basic land you control.\nLandfall — When
- oracle_text advisory -- Courser of Kruphix: oracle_text diverges (similarity 0.17); scryfall='Play with the top card of your library revealed.\nYou may play lands from the top of your library.\nLandfall —
- oracle_text advisory -- Khalni Heart Expedition: oracle_text diverges (similarity 0.19); scryfall='Landfall — Whenever a land you control enters, you may put a quest counter on this enchantment.\nRemove three 
- oracle_text advisory -- Rampant Growth: oracle_text diverges (similarity 0.16); scryfall='Search your library for a basic land card, put that card onto the battlefield tapped, then shuffle.'
- oracle_text advisory -- Search for Tomorrow: oracle_text diverges (similarity 0.26); scryfall='Search your library for a basic land card, put it onto the battlefield, then shuffle.\nSuspend 2—{G} (Rather t
- oracle_text advisory -- Cultivate: oracle_text diverges (similarity 0.24); scryfall='Search your library for up to two basic land cards, reveal those cards, put one onto the battlefield tapped an
- oracle_text advisory -- Evolving Wilds: oracle_text diverges (similarity 0.11); scryfall='{T}, Sacrifice this land: Search your library for a basic land card, put it onto the battlefield tapped, then 
- oracle_text advisory -- Terramorphic Expanse: oracle_text diverges (similarity 0.11); scryfall='{T}, Sacrifice this land: Search your library for a basic land card, put it onto the battlefield tapped, then 
- oracle_text advisory -- Myriad Landscape: oracle_text diverges (similarity 0.25); scryfall='This land enters tapped.\n{T}: Add {C}.\n{2}, {T}, Sacrifice this land: Search your library for up to two basi
- oracle_text advisory -- Primeval Titan: oracle_text diverges (similarity 0.16); scryfall='Trample\nWhenever this creature enters or attacks, you may search your library for up to two land cards, put t
- oracle_text advisory -- Radiant Fountain: oracle_text diverges (similarity 0.12); scryfall='When this land enters, you gain 2 life.\n{T}: Add {C}.'
- oracle_text advisory -- Haunted Fengraf: oracle_text diverges (similarity 0.12); scryfall='{T}: Add {C}.\n{3}, {T}, Sacrifice this land: Return a creature card at random from your graveyard to your han
- oracle_text advisory -- Temple of Malady: oracle_text diverges (similarity 0.52); scryfall='This land enters tapped.\nWhen this land enters, scry 1. (Look at the top card of your library. You may put th
- oracle_text advisory -- Temple of Malice: oracle_text diverges (similarity 0.52); scryfall='This land enters tapped.\nWhen this land enters, scry 1. (Look at the top card of your library. You may put th
- oracle_text advisory -- Temple of Mystery: oracle_text diverges (similarity 0.52); scryfall='This land enters tapped.\nWhen this land enters, scry 1. (Look at the top card of your library. You may put th
- oracle_text advisory -- Temple of Plenty: oracle_text diverges (similarity 0.52); scryfall='This land enters tapped.\nWhen this land enters, scry 1. (Look at the top card of your library. You may put th
- oracle_text advisory -- Temple of Triumph: oracle_text diverges (similarity 0.52); scryfall='This land enters tapped.\nWhen this land enters, scry 1. (Look at the top card of your library. You may put th
- oracle_text advisory -- Zhalfirin Void: oracle_text diverges (similarity 0.74); scryfall='When this land enters, scry 1. (Look at the top card of your library. You may put that card on the bottom.)\n{
- oracle_text advisory -- Crystal Grotto: oracle_text diverges (similarity 0.39); scryfall='When this land enters, scry 1.\n{T}: Add {C}.\n{1}, {T}: Add one mana of any color.'
- oracle_text advisory -- Rumble Arena: oracle_text diverges (similarity 0.44); scryfall='Vigilance\nWhen this land enters, scry 1. (Look at the top card of your library. You may put it on the bottom.
- oracle_text advisory -- The Grey Havens: oracle_text diverges (similarity 0.24); scryfall='When The Grey Havens enters, scry 1.\n{T}: Add {C}.\n{T}: Add one mana of any color among legendary creature c
- oracle_text advisory -- Galadriel of Lothlórien: oracle_text diverges (similarity 0.24); scryfall='Whenever the Ring tempts you, if you chose a creature other than Galadriel as your Ring-bearer, scry 3.\nWhene
- clause_ledger: no dedicated per-clause artifact. Its function -- every oracle clause modeled/inert/deferred -- is covered by coverage(partial hard-stop) + bracket-note deferrals + viewer oracle cross-check + audit_card_fields oracle-diff. A dedicated ledger is deferred (high per-card cost, marginal added rigor).

<!-- verify_deck:end -->

## Stage 5c2 — horizon-honest tie-break (2026-10-17, 4b57bf72)
`leaf_tiebreak_check.py` at PLAY settings, 12,000 paired games: net -9 turns (half A -6, half B -3; 19
better / 9 worse; 28 changed = 0.233%). VERDICT: KEEP THE DEFAULT (GradesNoWinLeaf ON).
