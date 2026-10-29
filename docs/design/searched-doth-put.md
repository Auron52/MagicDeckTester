# Searched Defense of the Heart put list (MTG_DOTH_AXIS) -- 2026-10-29

Defense of the Heart: *"At the beginning of your upkeep, if an opponent controls three or more creatures,
sacrifice this enchantment, search your library for up to two creature cards, put those cards onto the
battlefield, then shuffle."* (`cards.json`: `upkeep_sac_tutor_creatures 2`, `upkeep_sac_tutor_opp_min 3`.)

WHICH creatures, and in what ENTER order, used to be the provider's closed-form pick
(`DecisionProvider::SacTutorPutList`, the immediate-drain maximiser) at two places, neither of which branched:

* the executor's real upkeep (`GameEngine::UpkeepTail` -> `PerformUpkeepSacTutor`), before that turn's search;
* the rollout's turn boundary (`SimulateEndAndStartNextTurn` -> `PerformUpkeepSacTutor`), which `FSLineTail` and
  the lookahead cross with depth remaining.

That is an in-window greedy pick (CLAUDE.md, NO GREEDY PICK INSIDE THE SEARCH WINDOW), and survey row #7 of
`searched-echo.md`. It is a genuine multi-option decision. The closed form maximises the burst the puts realise
the moment they enter, which is not the earliest kill. The unit test's board is the clean witness: two Defense of
the Heart, one Suture Priest, three opposing 1/1s, the opponent on 40.

* The closed form puts Phantasm then Wurm for the first trigger: 5 gifted Goblins drain 1 each, the sweep kills
  eight creatures for 2 each, 21 in all. The sweep empties the opponent's board, so the SECOND trigger's
  intervening if fails. The opponent survives on 18.
* Priest + Phantasm first drains only 10, but leaves eight opposing creatures. The second trigger then puts
  Phantasm then Wurm for 10 + 13 x 2 = 36. The opponent dies in turn 4's upkeep. On the closed form's line the
  second trigger never fires (the swept board stays empty), and the attackers -- one Priest now, the summoning-sick
  Phantasm and Wurm from turn 5 -- cannot close 18 before turn 6.

## The decision and its option space

The engine resolves a trigger in `PerformUpkeepSacTutor` (`core/SpellEffects.h`):

1. sacrifice the enchantment;
2. pick the put list -- an ordered list of up to `max_puts` (2) library creature NAMES, repeats allowed up to the
   copy count (the first library copy of each name is the one put);
3. **pass 1** moves every put onto the battlefield (the as-enters counters at the pre-event doubler shift);
4. **pass 2** resolves, put by put in LIST ORDER, its enter-watcher cascade (`FireEtbWatchers`) and then its own
   enter trigger (`FireOwnEtbTriggers`);
5. shuffle (`ShuffleAfterSearch`, the stable keyed shuffle).

A second Defense of the Heart re-checks the intervening if after the first resolved (the loop re-scans), so two
triggers in one upkeep are SEQUENTIAL decisions: the second one's menu, and whether it fires at all, depend on the
first one's puts.

The raw option space per trigger is the empty list, each single, and each ordered pair: for Creature Giving's
seven creature names (Hunted Phantasm, Suture Priest, Massacre Wurm, Soul Warden, Essence Warden, Varchild's
War-Riders, Birds of Paradise) with two or more copies left, 1 + 7 + 49 = 57 lists.

## The folds (each one proven, with what breaks it)

The menu is built by `DecisionProvider::SacTutorPutOptions`. Entry 0 is ALWAYS `SacTutorPutList`'s answer -- the
provider's pick is the first branch and keeps every tie -- and the rest follow in descending closed-form score.
The provider may only order and restrict, and each restriction below carries its proof. The two
provider-proven folds sit behind `MTG_DOTH_FOLDS` (default ON). They are sound, so the lever is a measurement
arm, not a play choice. The proofs are card by card from `cards.json`, against the passive goldfish
opponent (never attacks, never blocks, casts nothing). They are classified BY NAME
(`CreatureGivingProvider`, `CgPutClassOf`), so a creature added to the list later gets no fold until someone
extends the proof. The unit test "a creature outside the proven pool gets no fold" pins that.

### 1. Identical cards (engine identity, every provider)

Two copies of one card are one option -- the resolution puts by name. This is the pre-axis enumeration's own
rule, and `SacTutorPutsCommute(a, a)` is true by default.

### 2. Orders that reach the same state (identity, provider-proven)

For a pair `(x, y)`, pass 1 has put both on the battlefield before either resolves anything. So the two orders
differ only in pass 2's sequence: `W(x) E(x) W(y) E(y)` versus `W(y) E(y) W(x) E(x)`, where `W` is the enter-watcher
cascade and `E` the put's own enter trigger. The pool, card by card:

| card | own enter trigger | what its entry fires (`W`) |
|---|---|---|
| Suture Priest | none | +1 life per other Suture Priest (own creature entered), +1 per Warden |
| Soul Warden, Essence Warden | none | the same lifegain watchers |
| Birds of Paradise | none | the same |
| Varchild's War-Riders | none (cumulative upkeep is an upkeep trigger) | the same |
| Hunted Phantasm | the opponent creates five 1/1 Goblins; each drains 1 per Suture Priest and gains 1 per Warden | the same |
| Massacre Wurm | -2/-2 to the opponent's creatures (the engine kills toughness - damage <= 2); each death drains 2 per Wurm | the same |

* Every `W` changes only OUR life.
* Phantasm's and the Wurm's `E` change only the OPPONENT's board and life, and read only the watcher set, which
  pass 1 fixed.
* So a pair with at most one of {Phantasm, Wurm} reaches the same state in both orders, up to the battlefield
  order of the two puts, which no rule reads. The unit test crosses both orders of Priest+Phantasm and of
  Priest+Wurm and compares the boards, both life totals, the library and the hand.
* Phantasm+Wurm does NOT commute. Phantasm first gives the sweep five more victims (2 each per Wurm). Wurm first
  leaves the Goblins alive, as fuel for a later Defense of the Heart or a later sweep. Both orders are kept.

**Breaks if** a put's own enter trigger touches our board or reads our life, or a watcher's effect depends on
the order. Examples: a "whenever you gain life" payoff with a threshold, or a sweeper that hits our side.

### 3. A free slot loses to a free-gain card (dominance, provider-proven)

A list shorter than `max_puts` is dropped while a FREE-GAIN card (Suture Priest, Soul Warden, Essence Warden,
Birds of Paradise, Varchild's War-Riders) still has a spare copy. Adding that card weakly dominates leaving it in
the library, and the proof is pathwise, not just in expectation:

* **The stable shuffle couples the two worlds.** `ShuffleByKey` orders each library card by a key of the seed,
  the shuffle ordinal and the card. Removing one copy leaves every other card's relative order unchanged. So
  world Y (the list plus B) draws exactly what world X (the list alone) draws, minus B's copy: identical until X
  would draw B, then one card AHEAD.
* **Y mimics X.** Everything X holds, Y holds too. The one difference is B, which Y has on the battlefield since
  this upkeep while X has it in the library or hand. Whenever X casts B, Y keeps that mana. Y's extra card is
  never needed; at cleanup Y sheds it first.
* **B on the battlefield earlier only adds:**
  * Suture Priest drains (the win condition) and gains;
  * the Wardens gain;
  * Birds makes mana;
  * Varchild is a 3/4 trampler the goldfish never blocks. Its cumulative upkeep gifts the opponent 1/1s, and is
    always paid: the searched-echo doc's proof shows every reader of an opposing creature in the pool gains from
    one (Suture Priest, the Wurm, the Wardens, Defense of the Heart's count).
* **Nothing in the pool reads against us** our creature count, our library size or an opposing creature.

The one quantity X can have MORE of is our life. X's later cast of B fires our lifegain watchers at that point,
possibly more of them than were out at this upkeep. Our life matters to Creature Giving only through two things:

* a shock's "pay 2 life" (allowed only above 2 life);
* own death, which is not live for this deck (`own_death_live` is false: no damage-event card).

**Breaks if:**

* our life can come within the watchers' difference of 2 -- the deck pays life only to its own lands: fetches 1,
  shocks 2, City of Brass 1 per tap, against 20 life plus the Wardens;
* own death goes live, or a card reads our life (a Serra-style threshold);
* a free-gain card gains a drawback;
* the opponent stops being passive.

### Not folded, and why

* **Suture Priest vs a Warden.** The Priest is the better body, but the dominance holds only in EXPECTATION. When
  the Warden's copy sits before the Priest's in the keyed order, the Warden world is one card ahead for a window,
  and the clairvoyant search can see it. Kept.
* **Soul Warden vs Essence Warden.** They are identical on the battlefield, but the copy left in the library
  differs in colour ({W} vs {G}). Kept.
* **Phantasm or Wurm now vs later.** This is the real timing decision: more Suture Priests later make the gift
  drain more, a bigger board later makes the sweep drain more, and the sweep eats the next trigger's fuel.

**The menu that remains** for Creature Giving's seven names is 27-29 lists: the unordered pairs with repetition
(28 when every name has two copies left), plus the second Phantasm/Wurm order, less the pairs a single remaining
copy forbids. The census measured a mean menu of 28.8 (max 29). Unfolded it is 57.

## The design

**The pin.** The plan of the turn that crosses the boundary carries the answer, and the next upkeep consumes it in
both worlds:

* `Plan::doth_put_choice` -> `GameState::scripted_doth_put`, written by `WritePostApplyStatePins`
  (write-when-set);
* consumed and cleared by `PerformUpkeepSacTutor`, in the executor (`GameEngine::UpkeepTail`) and in the rollout
  (`SimulateEndAndStartNextTurn`);
* both resolve the rank through ONE function, `DothUpkeepPutList`.

The encoding is -1 for no pin. Otherwise it holds a 7-bit RANK per trigger ordinal (four ordinals, one per copy
of the card) into that trigger's resolution-time menu, plus the count of ordinals it COVERS. An ordinal past the
cover takes the provider's pick. Both fields sit in existing padding:

* `int32 scripted_doth_put` at GameState offset 772;
* `int32 doth_put_choice` at Plan offset 36;
* `sizeof(GameState)` (1152) and `sizeof(Plan)` (496) are unchanged, measured with gdb `ptype /o`.

**Why a rank, decided at the boundary.** The menu is a function of the UPKEEP state: the library, the opposing
board after the turn-start Orchard Spirits and Varchild's gifts, and an earlier trigger's puts. No plan can see
that state when it is enumerated, and the decision changes nothing before the upkeep. So the cheapest exact place
to branch is where the search CROSSES the boundary, not at enumeration (echo's main route). Fanning plans at
enumeration would apply the same plan once per option and would have to guess the menu's size.

**The boundary fan (`BoundaryFan`, `TurnSolver.cpp`).** One class now drives both the searched echo and this
fan; with no Defense of the Heart live it reproduces the echo boundary fan exactly. A site:

* crosses a COPY of its pre-boundary state once per fan entry;
* stamps the crossing's pin;
* holds a probe (`DothProbe`) open across the crossing, through which each resolution reports its menu size;
* records the winning entry's pin on the plan it adopts.

The ranks are an ODOMETER over the ordinals the state's pin does not cover, its radices learned crossing by
crossing: a second trigger's menu, and whether it fires at all, depends on the first one's puts. The first
crossing is every rank 0, i.e. the provider's picks, and later ones must be STRICTLY better, so ties keep the
provider.

The fan sites:

* `FSLineTail`'s **single-main boundary**. This is Creature Giving's: the deck is single-main. The winner's pin
  travels back on the line (`SearchLine::boundary_doth`) and the caller stamps it on the main-1 plan that crossed
  (`StampBoundaryPins`, at FSLineWin's four tail-adoption sites).
* `FSLineTail`'s three **second-main crossings** (plan loop, breakpoint children, deferred waves), shared with echo.
* `SolveWithLookahead`'s candidate loop, its waves and its condemnation escalation, through
  `BoundaryChooseByRollout`: one rollout per entry, each later one cut at the best so far.
* A second main is never "unproductive", and never takes its 0-cost empty path, while a trigger is uncovered
  (`DothUncoveredPending`).

**Where it does not fan.**

* In human play: the person picks at the `sac_tutor` prompt, whose default is the pinned or provider list.
* In a narrowed tail rollout and the d0 runner: beyond the horizon the provider's pick is the permitted playout
  policy.
* At a full-line node whose next turn is a CONSTANT leaf (`EchoEdgeFoldHere`). That leaf answers max_turns+1 for
  every state, and FSLineWin at depth 0 reads no win off the boundary, so two put lists cannot score differently
  there. A deeper pass searches that turn, so no line is unfindable at any budget. Creature Giving's leaf is a
  model, so this fold does not fire for it.

**Menu cache.** A fan crosses one pre-boundary state once per entry, so every crossing rebuilds the same menu. The
first build measured ~10% of a Creature Giving game's CPU in `perf`. `DothMenuCached` keeps a few menus per
thread, keyed EXACTLY on what `SacTutorPutOptions` may read. That key is the virtual's documented contract.

**Gating and keys.**

* `GameState::deck_has_sac_tutor` is a deck constant (`StampDeckTraits`), so every deck without the card is
  byte-identical by construction.
* `MTG_DOTH_AXIS=0` (heurarm slot `DOTH_AXIS`) is an A/B arm only, play-identical to the pre-change tree. With it
  off, nothing writes a pin, and an unpinned resolution calls `SacTutorPutList` exactly as before.
* The pin is future-determining. It is folded value-gated into `dominance::Build`, `BuildDedupKey`'s pending pins
  and `BuildBreakpointKey`.
* `Plan::doth_put_choice` is in the plan hash, equality, signature fingerprint and `IsApplyEmptyPlan`, the
  dedup census, the executor's re-solve pin checks (`CarriesPostApplyPin` / `RecordReSolveRealized`) and the
  provider route signature.

**The census (`MTG_DOTH_AXIS_STATS=1`; `=2` prints the first uncovered ones).** It counts every resolution:

* covered (and off the provider's pick), or uncovered;
* uncovered while the innermost search frame had depth, split into a SEARCHED frame (a host that crossed without
  fanning: the no-greedy instrument, target 0), the playout beyond the horizon, and the constant-leaf edge fold;
* the executor's real upkeeps in a searched game, covered or not;
* fans, crossings, and the menus built with their mean and max size.

## Measurement

The base is origin `3c729a5ae` (the searched-echo stack plus the Lackey axis). Each run is one pooled batch:

* `--threads 12`, `MTG_PGO=0`, `MTG_RSS_CAP_GB=43`;
* play settings, i.e. the deck's `value_play`: d5 b20 with the value leaf;
* both arms in one batch, paired per game (`seed = BASE+off`, `game_index = off`).

The seeds are new and held out. Creature Giving uses 3250001 and 3260001 (3,000 games each); creature_giving2hg
uses 3270001 (3,000 games, 30 life, two heads).

| arm vs B0 (`MTG_DOTH_AXIS=0` = the pre-axis tree) | deck | games | faster | slower | net turns | z | units | wall (loaded box) |
|---|---|---|---|---|---|---|---|---|
| **B1** axis on (default) | Creature Giving | 6,000 | 6 | **0** | **-6** | -2.45 | **1.016x** | 1.09x |
| **B1** axis on (default) | creature_giving2hg | 3,000 | 12 | 2 | **-10** | -2.68 | **1.079x** | 1.23x |

The same batch on the pre-rebase base `95abee8b6` gave the identical per-game result and identical units: the
upstream commits do not touch this deck's play.

* **Slower games, each re-run at `--depth 8 --budget-ms 0` (`ignore_play_profile`) in both arms:**

  | game | B0 play | B1 play | B1 d8 b0 | B0 d8 b0 |
  |---|---|---|---|---|
  | creature_giving2hg s3270001 gi1194 | T5 | T6 | **T5** | T5 |
  | creature_giving2hg s3270001 gi1486 | T4 | T5 | **T4** | T4 |

  Both recover; at d8 b0 the two arms play the same game. These are budget dilution at b20 -- the menu's extra
  crossings below the root spend the budget -- not a lost line.
* **Preliminary run** (`95abee8b6`, seeds 3230001 x 2,000 / 3240001 x 1,000): Creature Giving 2 faster / 1 slower
  (-1); 2HG 3 / 0 (-3); units 1.016x / 1.071x. Its one slower game (s3230001 gi510, T4 -> T5) recovers at d8 b0
  (T4).
* **A witness** (s3230001 gi1407, Creature Giving, T5 -> T4). At turn 4's upkeep the closed form puts two Massacre
  Wurms: the stacked -4/-4 sweeps everything, the opponent's 4/4 included, for 16. The turn-4 Wurm cast from hand
  then has nothing to kill: the opponent is left on 4 and dies on turn 5. The search puts Phantasm then Wurm
  instead: the same 16, but the 4/4 survives at -2/-2. The hand-cast Wurm's -2/-2 then kills it for the last 4,
  on turn 4.
* **`[fd-diverge]`** (`MTG_FD_ORACLE=1`, 600 + 300 games, both arms): 0.
* **The folds** (`MTG_DOTH_FOLDS=0` vs on, axis on; 1,000 + 500 games):
  * units with the folds OFF: 1.007x (Creature Giving), 1.031x (2HG);
  * win turns: 0 / 0 (Creature Giving), 0 better / 2 worse (2HG) -- the larger unfolded menu dilutes the budget.
  * So the folds remove work and lose nothing, as a sound fold must.

**Wall.** The extra crossings at the horizon edge are not charged a search unit -- the same as echo's boundary
fan, and as every leaf evaluation -- so the budget does not see them. That is why units move 1.6% / 7.9% while
wall moves more. Profile of a 2HG run: `SimulateEndAndStartNextTurn`, the stable shuffle, the enter-watcher
cascade and the value leaf's `Score`. The menu build itself was ~10% of a game before `DothMenuCached`, and is
off the profile now. A per-crossing price (the `MTG_EQUIP_ROUTE_NODE_UNITS` shape: inert at an unlimited budget)
would make the budget see them. That is a play change and a separate measurement -- left open for the USER.

**The cover census** (`MTG_DOTH_AXIS_STATS=1`, 200 Creature Giving + 200 creature_giving2hg games, final build):

| Defense of the Heart resolutions the upkeep resolved | 2,763,897 |
|---|---|
| covered by a searched pin | 2,731,181 (2,641,255 off the provider's pick -- the fan's other branches) |
| in the playout beyond the horizon (permitted) | 29,574 + 3,142 with no frame |
| at a constant-leaf edge (the identity fold) | 0 (this deck's leaf is a model) |
| **uncovered inside a searched frame** | **0** |
| the executor's real upkeeps in a searched game | 277, **0 uncovered** |
| fans / crossings | 134,430 / 2,622,284 |
| menu size | mean 28.87, max 29 |

**Byte-identity.** The new binary at defaults was compared, per job digest and units, with origin's binary
(`3c729a5ae`). 82 of 82 jobs were identical, 30 games each:

* every other suite deck (38), plus goblins2hg (the echo fan now runs through `BoundaryFan`);
* Creature Giving and creature_giving2hg with `MTG_DOTH_AXIS=0`, which is therefore play-identical to the pre-axis
  tree.

The same check passed on the pre-rebase base.

**The suite's Creature Giving rows**, run on the final build. The B0 arm reproduces every row's ground truth
exactly. B1 (the default) moves these rows:

| row | GT (= B0) | B1 |
|---|---|---|
| creature_giving_smoke_d5_s1001 | 4.6800 | 4.6800 (digest moves) |
| creature_giving2hg_smoke_d5_s1001 | 4.9600 | **4.9400** |
| creature_giving_regression_d3_s2002 | 4.5700 | 4.5700 (digest moves) |
| creature_giving_regression_d5_s2002 | 4.6400 | 4.6400 (digest moves) |
| creature_giving_regression_d5_s3003 | 4.6376 | **4.6353** |

Unchanged: smoke d0 / d3, regression d0, and the unbudgeted backstop rows d3 / d5 s30001. No row gets worse;
units range from 1.010x to 1.066x. The smoke and regression GT therefore need accepting for these rows when
this lands. The overnight rows are left to the periodic overnight run.

**Unit tests** (`test/unit/test_doth_axis.cpp`, 8 cases; full suite 2,149 / 2,149):

* the pin encoding;
* the menu: rank 0 = the closed form, both Phantasm/Wurm orders, the folds, the unfolded menu under
  `MTG_DOTH_FOLDS=0`, and no fold for an unproven creature;
* the fold's soundness on real crossings: both orders of Priest+Phantasm and Priest+Wurm reach the same state,
  Phantasm/Wurm do not;
* both worlds: the rollout's crossing and `GameEngine`'s real upkeep put the same rank and consume the pin;
* the boundary fan: one crossing per menu entry, the provider's pick first; nothing with the lever off or with a
  covering pin;
* the two-trigger odometer;
* the full-line search finds the turn-4 upkeep kill and records it (cover 2, rank > 0) on the main-1 plan, and
  does NOT with `MTG_DOTH_AXIS=0`.

## Open

* **A per-crossing unit price** for the fan's extra crossings (see Wall above). Recommendation: leave unpriced
  (echo's precedent) unless the regression tier's Creature Giving wall becomes a problem.
* **`MTG_NC_SEARCH`'s reshuffle-average instrument** (`ReshuffleAvgChoosePlan`) and the label tools cross the
  boundary unfanned, so under them the put list is the provider's pick (echo is the same). No slower game needed
  that instrument here.
* **Echo's single-main residual** (searched-echo.md) is unchanged. The single-main boundary fan built here fans
  Defense of the Heart only. Adding `with_echo` there closes it, but it changes no current deck: no echo deck is
  single-main.
* **Expectation-only folds** (a Suture Priest over a Warden) are deliberately NOT taken -- the user's bar is
  soundness, and the stable shuffle lets the clairvoyant search tell them apart.

## Dwarven Hold's storage hold (the survey's executor-only item)

`StorageLandHold` for Dwarven Hold ("upkeep_if_tapped") was asked only by `GameEngine::UpkeepTail`. The rollout
never asked, and its untap step never cleared `Permanent::storage_hold_this_turn` either. That was lockstep only
because the base answers false and nothing overrides it. Under an override the search would have priced a land
the real game holds (or the reverse), and a held land would have stayed held for every later simulated turn.

Now both worlds call ONE function, `PerformUpkeepStorageHolds` (`SpellEffects.h`), at the same point of the
upkeep: first after the Vial charges. Both untap steps clear the flag. It is byte-identical while no provider
holds, because nothing sets the flag.
