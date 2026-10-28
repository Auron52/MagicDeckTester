# Cantrip search cost: Brainstorm, Brainsurge, Ponder, Preordain

Status: DESIGN (2026-10-27), not implemented. This is a read-only study written against the Cyclone
phase-5 tree (`f3d4974fa` + `9341d9823`). Every file:line below refers to that tree.

USER 2026-10-27: *"Let's leave the mulligan stuff until the profile generation. I want to focus on
other performance wins. We should also look at optimizing brainstorm, ponder and preordain (and
brainsurge)."* The USER also set the purpose: *"the point of all of these rules is to come up with
heuristics with which we can effectively prune a number of search options."*

The decks that play these cards:

| deck | cantrips | suite | notes |
|---|---|---|---|
| Unpredictable Cyclone | 4 Brainstorm, 4 Brainsurge, 4 Ponder (no Preordain) | no (cost gate FAIL 17.36x) | the put-back fan is the search's main cost |
| NivMizzetCombo | 2 Brainstorm, 4 Ponder, 4 Preordain | yes | the USER-adopted package already prunes Brainstorm to one pick and Ponder to keep/shuffle |
| Hinata2 | 4 Ponder, 4 Preordain | yes (`hinata` rows, keep table, value leaf) | Ponder is the full 7-way order axis |

## 0. The rules this design is bound by

These rules come from CLAUDE.md:
- **No greedy pick inside the search window.** A provider may ORDER or RESTRICT options. The engine
  never collapses a decision with depth remaining to an option it did not branch on. *"The budget
  ran out" is not a licence to pick.* So any budget-staged admission must still admit **at least two
  options** at every node. Only a provider restriction may cut to one (the existing
  `MTG_*_SINGLE` levers), and that is a lossy lever, the USER's call.
- **Collapse wasted search unconditionally.** A sound identity or dominance fold ships default ON,
  even when wall time does not move. A lossy narrowing stays default OFF until a held-out A/B and the
  USER's approval.
- **A budget may re-price work. It may not make a line unfindable.** The test is at unlimited or
  large budget. The preferred shape is a price or a budget-staged admission (waves) that is inert at
  `--budget-ms 0` **by construction**: return early when `budget == nullptr || budget->Unlimited()`.
  Turn-complete preference: under a binding budget, finish the current turn's options before
  spending on later turns' depth.
- **No unbudgeted-only changes.** Everything below applies to budgeted play and is tested as a play
  change.

## 1. How each cantrip is represented today

### 1.1 Brainstorm and Brainsurge: the put-back axis (`Plan::putback_choice`)

**The choice.** Both cards are a `draw_spell` with `put_back_from_hand: 2`. Brainstorm draws 3 and
Brainsurge draws 4 (cards.json:13777 for Brainsurge). After the draw, two hand cards go on top in
any order. The choice is an ordinal into a provider-ordered list (SpellEffects.h:36398-36432).

**The candidate list** is `PutBackCandidates` (SpellEffects.h:36482). The fast path that every
autonomous resolution uses is `PutBackPickAt` (SpellEffects.h:36664), which never builds the list.
- Hand cards are folded into identity CLASSES (`PutBackClassKey`, SpellEffects.h:36436): name,
  printed P/T, and the side of the m1-hand snapshot.
- Candidates are the ORDERED pairs of classes. A pair (a, a) is legal only when the class has two or
  more copies.
- **Width = C(C-1) + #{classes with 2+ copies}**, where C is the number of classes in the post-draw
  hand minus the cast card (SpellEffects.h:36636-36645). A 9-10 card post-draw hand has C = 7-9,
  which gives 42-73 candidates.
- Measured on Cyclone (126 kept games, ledger phase 5): mean width 55, median 57, p90 73. Brainsurge
  draws one more card, so its hand is one class wider (C = 8-10, 56-90).

**The order.** Classes are ranked by `DecisionProvider::PutBackRanking` (DecisionProvider.h:557-566;
it returns hand indices, most expendable first). The default ranking is the cleanup shed order
(DecisionProviders.cpp:1051-1067). Pairs are sorted by the summed class rank, ascending. Within one
sum, the LESS expendable card goes on top (SpellEffects.h:36537-36546). `PutBackRankingPerCard`
moves one per-card selection to rank 0 (SpellEffects.h:36547-36566).
- **This structure cannot express "card X above card Y" independently of expendability.** That
  matters for Cyclone (§3.2). For example, a dead Toxic Deluge has to go ABOVE Emrakul, yet Deluge
  is the more expendable of the two.

**Where the variants are generated.** `AppendSubdecisionAxes` (TurnSolver.cpp:74372-74477) clones
every BASE plan that casts a put-back card once per rank `k = 1 .. w-1` (`v.putback_choice = k`,
74431-74436). Rank 0 is the base plan itself.
- Only the first put-back cast in a plan varies ("vary ONE put-back per variant", 74454). A second
  one in the same plan resolves at the default, because the pin is consumed (SpellEffects.h:36829).
  A breakpoint continuation re-solve gives a later cast its own fan.
- **`w` is an UPPER bound**: `PutBackAxisWidth` (TurnSolver.cpp:23898-23961). It counts the classes
  of hand + the library cards the plan can draw before the put-back. When the plan also holds a
  shuffle, tutor or Ponder (`PutBackPredictionUnsafe`, 23893), it counts the classes over the hand
  plus the WHOLE library.
- Ranks past the real list CLAMP to its last candidate (SpellEffects.h:36830).
- **Single-game probe** (frozen p5 binary, seed 81000 gi 0, d3 b10, 1 thread, `MTG_PUTBACK_TRACE=1
  MTG_CANTRIP_ATTRIB=1`):
  - 25,383 fanned base plans produced 2.79M variants (110 per plan), against a mean resolution
    width of 55.5. The widest fans were 240, from plans holding a fetch or a Ponder.
  - 14,145 of 205,024 pinned resolutions clamped.
  - The 126-game attribution reads 98.8 variants per fanned plan.

**How variants are keyed and deduped.**
- The plan-signature dedup inside `EnumeratePlans` keys on cast names only, so the axis runs AFTER
  it (the post-dedup axis pattern, TurnSolver.cpp:23675-23686).
- Each fan has an id, `Plan::putback_group` (stamped on the base at 76854). `PutBackFoldMemo`
  (26028-26060) learns the real width from the first applied member (`g_apd_last_putback_w/_i`,
  23454-23457). It then declines every rank that clamps onto a rank already applied, BEFORE the
  apply and the unit charge (93218-93236 in `SolveWithLookahead`; FSLineWin 86870; m2 84723).
- `NodePutBackFold` (26090-26150) does the same for breakpoint-node children, but still charges
  each one a unit.
- `MTG_PB_LAZY_CANDS` (71666-71880) holds a fan as one prototype plus a count in
  `SolveWithLookahead` and `FSLineWin` lists, so clones are synthesised per index and a declined
  rank is never built.
- After apply, the state-key dedup (`PlanIsAxisVariant`, 25022-25046) skips a variant whose state a
  sibling already reached. It costs one apply and no rollout.

**What the rollout does with them.**
- Inside the window, every rollout turn is a nested `SolveWithLookahead` (`RolloutPreCombatSolve`,
  TurnSolver.cpp:81099, 95584-95588), and it fans exactly like the root.
- A nested solve is non-enforcing: *"the rollout sub-search runs every pass to completion and merely
  consumes"* (92340-92351). `gate = enforce_budget && budget != nullptr` (92369). It stops only at
  the overrun ceiling (`MTG_LA_NESTED_BAIL`, 92371, 93188-93189). So **every in-window rollout turn
  with a Brainstorm in hand evaluates the whole fan after the decision budget is already spent.**
  - Probe: `SolveWithLookahead` applied 316,219 put-back variants, and 304,007 of them (96%) ran
    with the decision budget EXHAUSTED.
  - `FSLineWin` applied 1,281 variants, 890 of them past the overrun ceiling.
- Beyond the horizon (`Solve(..., GreedySite::HorizonLeaf)`, 92211-92226), plans carry no pin, so
  the put-back resolves at rank 0. That is the permitted greedy.

**Lockstep.**
- The executor installs the same pin (`ScriptedPutBack _sputback_exec(plan.putback_choice)`,
  AIEngine.cpp:4539) as the search's apply (TurnSolver.cpp:54789). Both resolve through the shared
  `ResolvePutBackFromHand` (SpellEffects.h:36797), which is called at TurnSolver.cpp:57972 and in
  the executor's `ResolveDrawSpell`.
- The candidate list is rebuilt at resolution from the true state in both worlds. **The pin is an
  ordinal, so any new ORDER hook must be a pure function of the resolution state.** Otherwise the
  same rank names different cards in the two worlds.
- "Which copy" is lockstep too: the last copy of a class goes back (SpellEffects.h:36880-36910).

### 1.2 Ponder: the order axis (`Plan::ponder_choice`)

- `cast_reorder: 3, draw: 1`. The reorder happens before the draw (TurnSolver.cpp:57920 then 57957;
  executor EffectHandler.cpp:910-912). So the first card of the chosen order is drawn immediately,
  and the other two are the next draws.
- **Width = m! + 1 = 7** (`PonderOrderAxis()` is hard-wired true, 23851-23861). The axis is at
  74301-74370. `k_max = look!` (74342-74346) is computed WITHOUT looking at the cards.
- Candidate 0 is the provider's `TopDispositionPick` (default `DefaultTopDisposition`,
  SpellEffects.h:35720-35748: shuffle unless `KeepReorderTop`; otherwise `ScryKeepOnTop` cards first
  by `SituationalCardRank`). The rest follow `next_permutation` order plus "Shuffle", with the
  heuristic's duplicate dropped (`TopDispositionCandidates`, SpellEffects.h:36022-36066;
  `EnumerateTopDispositions`, 35978-35994).
- **Identical cards are not folded at enumeration.** [Island, Island, X] still yields 7 variants.
  The post-apply dedup catches the duplicates at one apply each: the state key folds the library by
  name hash (TurnSolver.cpp:79956-79959).
- The pin is consumed by the first Ponder (SpellEffects.h:36293-36302). Only one Ponder per plan
  varies (74364).
- Provider narrowings:
  - `ReorderKeepShuffleOnly`: 2 options. Niv has it ON (`MTG_NIV_PONDER_KEEP_SHUFFLE`,
    DecisionProviders.cpp:53734-53743). Cyclone has it as a proposal, default OFF
    (`MTG_CYCLONE_PONDER_KEEPSHUFFLE`, 30973-30977).
  - `ReorderSingleCandidate`: 1 option. Niv's `MTG_NIV_PONDER_SINGLE` is OFF.
- Lockstep: `ScriptedReorder` is installed by both worlds (AIEngine.cpp:4538, TurnSolver.cpp:54788),
  and both call the shared `ReorderTopOrShuffle`.

### 1.3 Preordain: NOT searched

- `cast_scry: 2, draw: 1`. It resolves `ScryTop` -> `ChooseTopDisposition(LookKind::Scry)` -> the
  provider's `TopDispositionPick` / `ScryKeepOnTop` (SpellEffects.h:36221, 35745-35754).
- The searched scry axis (`Plan::scry_choice`, `MTG_SCRY_SEARCH`) fans only a land drop's ETB scry
  and the triggered snow scry (TurnSolver.cpp:73273-73279).
- The no-greedy audit classifies the cast scry as a provider prune (Niv ledger Q14; it is the USER's
  call whether to search it).
- Rules today:
  - Generic: keep nonland, keep a land only while fewer than two are in play
    (DecisionProviders.cpp:548-561).
  - Niv: `NivWantOf >= 300` (53581-53585).
  - Hinata: `HinataRank >= 300` (14184-14191).
- Full width if it were searched: 5 options (keep both in either order, keep one of two, keep none).

### 1.4 The shuffle inside the search, and clairvoyance

**The search is clairvoyant about the library.**
- It plays on the real library. The search shuffle uses `shuffle_salt_search`, which by default
  equals the executor's `shuffle_salt` (SpellEffects.h:1856-1879; GameSetup.h:117-119). So it sees
  the exact post-shuffle order the executor will deal.
- Brainstorm's fan is sized by reading the cards it will draw (TurnSolver.cpp:23945-23950).
- Ponder's candidates are the real top three.

**The shuffle is order-independent.** `Library::ShuffleByKey` (Library.h:209-253) sorts the live
cards by `splitmix64(seed, m_number)` with `seed = SearchShuffleSeed(game_seed, search_count)`. The
post-shuffle library is therefore a function of the SET of card numbers in the library and the
shuffle ordinal, not of their order before the shuffle. This is the stable-shuffle default; the
`MTG_LEGACY_SHUFFLE` Fisher-Yates is the exception. Two consequences:
- Put-backs that differ only in ORDER and are followed by a shuffle reach the **identical** engine
  state. That makes the fold exact in the engine's own (clairvoyant) model, not only in the real
  game (§2, fold F3).
- Under a following shuffle, *which* cards go back still matters to the clairvoyant search beyond
  the hand. Each card lands at a position fixed by its own key, so the search can "prefer" to bury a
  card it can see will come back late. That is a clairvoyance artifact. The USER flagged that this
  deck is very susceptible to them. Any A/B touching the shuffle branch must be read with the
  re-salt method of the Cyclone ledger: replay slower games under `shuffle_salt_search` 1-4.

### 1.5 Where the turn's land drop goes relative to a cantrip

This matters for the USER's shuffle line (Brainstorm, then crack a Scalding Tarn).
- **The no-land base plan is always emitted** (`add_for_land("", "")`, TurnSolver.cpp:78282-78291).
- Brainstorm, Brainsurge, Ponder and Preordain are all plain-cantrip breakpoints (site 3, `IsPlainCantrip`
  28672; `MTG_BP_SITE3` default ON, 24196-24201). The continuation is land-folded:
  `bp_play_searched_land` plays the continuation's land, with its `fetch_target`, before the
  continuation's casts (56206-56232).
- So "cast Brainstorm, put back, THEN play Scalding Tarn and crack it" is expressible today as the
  no-land base plan plus a continuation that plays the Tarn. A fetchland never reaches the
  battlefield uncracked here: `PerformFetch` cracks on play and then shuffles (cards.json Scalding
  Tarn oracle note).
- The same holds for Myriad Landscape (an activation in the continuation) and Long-Term Plans (a
  cast in the continuation).
- **What is NOT offered:** a base plan with the land AFTER its casts (`kLandSlotAfterCasts`) exists
  only under `LandOrderMatters`, meaning landfall watchers or graveyard feeders (78339-78375).
  Cyclone has neither. So the line is reachable only where the breakpoint continuation is searched:
  - node children in `FSLineWin`;
  - wave-0 `bp_choice` variants and the deferred `BpWaveWalker` waves in `SolveWithLookahead`.
- It is not a capability gap at budget, but it is budget-limited. The base plan is one of many,
  and its continuation rank must be reached. Making it cheap is part of §3: the shuffle branch is
  admitted with its own ordering.
- **Next-turn variant (the USER's shape ii):** put the card we want to draw on top and the dead card
  second; next turn's fetch shuffles the dead card away. This needs no new capability. It is purely
  an ORDER question for the put-back.

## 2. Sound folds

These are exact identities, or dominance with a stated argument. The USER's examples are taken one
by one. "Unit-neutral" means the fold removes only work that is already uncharged. Anything else
re-prices a finite budget and is tested as a play change (held-out A/B plus GT accept), like the
phase-5 cycle-seed pin fold.

**F1. Identical cards.** DONE: classes (`PutBackClassKey`) and the last-copy remap.
- Remaining: Ponder looks at enumeration time do not fold identical names. **Fix:** size the Ponder
  fan by the number of DISTINCT orders of the cards actually looked at (a multinomial: [L, L, X] has
  3 orders), and make `TopDispositionCandidates` drop name-duplicate orders in both worlds through
  the shared function.
- This needs the look to be known at enumeration. That is exact when the Ponder is the plan's first
  draw, look or shuffle, which cantrip-first makes the common case (the "safe" test of
  `PutBackPredictionUnsafe` already exists). Otherwise keep `look!`.
- Expected: Ponder fan 6 -> 2-3 variants whenever the look holds a duplicate name. Lands are the
  common duplicate (Cyclone runs 5 Island and 3 Mountain; Hinata runs mountains).
- Soundness: identity, because the post-apply dedup already proves these states equal. It removes
  applies, so it is not unit-neutral and moves Hinata.

**F2. Exact fan width (no clamped ranks).** When the put-back cast is the plan's first
library-reading or library-changing action (no earlier shuffle, fetch, tutor, look or draw), the
resolution hand is known exactly at enumeration: the hand, minus the casts before it and the card
itself, plus the top `draw` library cards. Emit exactly `count` ranks.
- Today the bound is mean 98.8 against 55 real, and up to 240 when any shuffle-capable action
  appears ANYWHERE in the plan, even after the put-back.
- **Sharpen `PutBackPredictionUnsafe` to actions ordered BEFORE the put-back cast in apply order.**
  With cantrip-first casting order, a Ponder or fetch AFTER the Brainstorm does not make the drawn
  cards unknown. Note that a land-first fetch (`land_slot == 0` with `fetch_target`) does come
  before it.
- Sound: an identity. The clamped ranks are duplicates of the last rank.
- Gain:
  - Memory: about 45% fewer clones in every non-lazy list (§4).
  - List length: clamped declines count in `candidates_done` but cost 0, so they distort the
    overrun guard's `avg_per_cand` projection (93196-93214). That is a play change, so A/B it.

**F3. Put-back followed by a shuffle: from ordered to unordered.** If the plan's actions after the
put-back cast reach a shuffle of our library before any draw, look, library read or walk, then
(a, b) and (b, a) give the IDENTICAL state, because `ShuffleByKey` is order-independent (§1.4). The
copies are the same cards because the last-copy remap is per class.
- Qualifying shuffles: a Long-Term Plans cast, a Myriad Landscape activation, or a positioned land
  that is a fetch.
- Implementation: extend `PutBackFoldMemo` with a mirror decline. From the first applied member it
  learns C and the class multiplicities, so it can compute each rank's mirror rank in closed form,
  the same arithmetic as `PutBackPickAt`. When the plan qualifies, decline rank r whose mirror r' < r
  was applied.
- **When the shuffle is in a breakpoint continuation** (the USER's Tarn-after-Brainstorm line), the
  qualifying test moves to the node: a child of fan member (b, a) whose continuation's first library
  event is a shuffle equals the child of (a, b) with the same continuation, matched by continuation
  plan IDENTITY, not by index, because the two parents' lists can differ.
- Gain: halves the fan on plans in the shuffle branch. Exact in both the clairvoyant and the
  non-clairvoyant model.
- Can the engine know at put-back time that a shuffle will follow? Yes for the plan's own later
  actions (they are fixed in the full-tail apply). For a continuation, only at the node, after the
  parent is applied.

**F4. Put-back pairs that strand the plan's own later cast.** In full-tail applies (every nested
`SolveWithLookahead`, where 96% of variant applies happen), the base "Brainstorm, then X" fans pairs
that put back X's last copy. X's cast then fails and the apply realises "Brainstorm" plus the same
pick without X.
- That line is the same-rank clone of the base without X, which is enumerated in its own right. It
  shares the same resolution state, since the Brainstorm is cast first.
- Any residual difference is the payment allocation (the standing mana-payment exemption, the same
  argument as the land-position prefix probe at TurnSolver.cpp:78376-78394).
- Implementation: in the fold memo, decline ranks whose pair includes the last copy of a class the
  plan casts after the put-back.
- Gain: with t distinct classes cast after it, (C-t)(C-t-1) pairs survive instead of C(C-1).
  - t = 1, C = 8: 42 of 56 (-25%).
  - t = 2: 30 of 56 (-46%).
- Sound by the degenerate-line argument. It is not an identity, so verify it with a mode that
  applies both and compares realised action records.

**F5. Cross-base duplicates at a truncated cantrip.** Where the breakpoint node hosts the cantrip
(`partition_here`, TurnSolver.cpp:57282-57305; `node_owns_site`, 55017-55021), every base sharing
one truncated prefix reaches the same partition state. `node_prefix_seen` already lets only the
first host the node (84549-84553). But each duplicate base's whole put-back fan is still applied:
rank k of base j truncates to rank k of base i.
- Fix: map duplicate-prefix bases to their first, and decline all their clones unbuilt.
- Exact: the same state at resolution and the same ordinal give the same pick.
- Gain: in the FSLineWin host only, roughly (number of tails - 1) x w applies per node.

**F6. Dead cards whose position is irrelevant.** NOT exact in general. A dead card on top still
delays every later draw by one, and the library differs afterwards.
- Exact merges are narrower. Two different `goldfish_inert` classes with the same card-type mask
  (the Cyclone walk reads types, SpellEffects.h:39929-39937), no cycling and no other param are
  interchangeable, so they can share one class.
- Small: Cyclone has one such name (Toxic Deluge).
- **"Dead noncreature above the Eldrazi when going off this turn"** is a DOMINANCE (the walk bottoms
  it for free, §3.2). It is not an identity, so it belongs in the provider ORDER or PRUNE set, not
  in the folds.

**F7. Ponder and Preordain orders where the remainder is irrelevant.** If, in the same full-tail
apply, the plan draws two more cards before any decision reads hand or library order (a following
Brainstorm draws 3), the three Ponder orders that share the drawn card are identical states. The
deferred cantrip continuation runs after the tail (57290-57294), so no decision intervenes.
- Fold the 6 orders to the 3 distinct now-draws, plus the shuffle.
- Exact if the dedup key treats the hand as a multiset in that context ("ordered; multiset when
  canon", 79967). Otherwise the folded states are equal only up to hand order, so the predicate must
  require the canonical key. Small gain.

**F8. Dominated scry and Ponder dispositions under a known library.** NOT foldable exactly. "Bottom
a dead card" vs "keep it on top" changes every later draw, and the clairvoyant search knows those
draws. Only F1 (identical names) is exact here.
- Under a NON-clairvoyant model, the dispositions that keep the same multiset of known cards above
  an unknown remainder become exchangeable in more cases. The engine does not have that model, so
  nothing is claimed from it.

**Dedup across plans that differ only in put-back** is already complete: the post-apply state dedup
plus the two fold memos. What is left is doing it BEFORE the apply (F2-F5).

**Which folds are exact under which model.** F1-F5 and F7 are exact under the engine's
known-library model and stay exact under a non-clairvoyant model. F2 only READS the library to size
the fan, and a wrong size costs a clamped duplicate, never a lost line. F3 is exact under both
because the shuffle is keyed on card numbers. F6 (the merge) is exact under both. Its "dead above
Eldrazi" form and F8 are not exact under either.

## 3. Budget re-pricing, provider ORDER and the go-off estimate

### 3.1 The order must come first

`MTG_PUTBACK_PRICE` (TurnSolver.cpp:25988-26026, one extra unit per non-default variant) measured
WORSE on Niv (d3 +0.027). The reason recorded then was that the provider's candidate 0 did not
deserve the head of the queue (`docs/design/niv-cantrip-branching.md`). On Cyclone the provider
single pick (`MTG_CYCLONE_PUTBACK_SINGLE` + `_RULE`) measured BETTER than the searched fan:
- d3: -0.105 (se 0.058) at x0.270 units;
- d5: -0.110 at x0.246.

The full fan cannot fit the first pass (median 43K units against a 9K budget), so the search ends
on a worse line than the rule's pick. So every budget shape below spends in provider order, and its
value depends on that order being good. The order is item 1 of the plan.

### 3.2 The Cyclone go-off estimate and the scored order (CycloneProvider)

This is the USER's guidance (2026-10-27) turned into a cheap estimate plus a score. Quotes:
- *"You need to make sure Cyclone can be played as early as possible. It is also a key card to
  always keep 1-of between hand and board. Beyond that you need enough mana to play it and 1-3
  cycling cards (preferring Street Wraith for the free cycle). Even better if you can rig the top
  card or cards of the library to drop an early Emrakul with Brainstorm, Brainsurge or Ponder."*
- *"the put back order actually depends on when you can go off. I recommend calculating that
  heuristically."*
- *"if it is this turn you are cycling with cyclone you would want the Emrakul (or ulamog is no
  Emrakul) on top. Otherwise you may want it second or third from the top."*
- *"there is actually reason to put it under a useless non-creature card you have in hand even if
  you are cycling with cyclone this turn."*
- *"if you can cycle twice this turn and have multiple of Emrakul or Ulamog you put both back and
  cycle both."*
- *"Emrakul goes first ... the extra copies still give extra turns that equate to 15 damage each."*
  The USER made Emrakul-over-Ulamog one rule for every format: *"you can make that rule general."*

**`CycloneGoOff(state) -> {k, c}`.** O(hand + battlefield), computed once per cantrip resolution
(cached per fan, §5).
- **k** = the number of our draw steps before the first turn on which a Cyclone can be on the
  battlefield AND a nonland creature cycler can be cycled.
  - A Cyclone out and a payable creature cycle now gives k = 0.
  - A Cyclone in hand: count mana this turn = untapped sources we control (the shared red count
    `CycloneShortOnRed`, DecisionProviders.cpp:30647, decides RR), +1 if the land drop is unused and
    an untapped-entering land is in hand, and Seething Song as the second red (net +2 after its
    {2}{R}). Then project one land drop per future turn from lands in hand. k is the first turn with
    {3}{R}{R} payable and a creature cycle payable after it.
  - No Cyclone in hand or on the battlefield, or a cycler or mana that the known cards cannot
    supply: k = 2+ (a missing piece; capped at 3).
- **c** = creature cycles payable on the go-off turn.
  - Street Wraith costs 2 life each and needs life > 2 per cycle (cards.json:13792).
  - Curator costs {U} each, out of the mana left after Cyclone (13829).
- Lands do not stop the walk. `CycleWalkTypeMask` excludes Land and `WalkExileUntilSharedType`
  bottoms every card it passes (SpellEffects.h:39929-39968). So lands and noncreature spells above
  an Eldrazi are dumped for free by a creature-cycler walk.
- A Curator on the battlefield scries 1 before the cycling draw (39903-39906), which can also clear
  a blocker.

**The score of a put-back candidate** (lower = earlier in the fan). It is computed over the
RESULTING top sequence and the hand that remains:
1. **Keep-need** (cost of removing a card from hand):
   - The only Cyclone across hand + battlefield: very high. A second copy is surplus: low.
   - The lands and Seething Song needed for {3}{R}{R} by the go-off turn: high. Lands beyond that:
     low.
   - Creature cyclers up to 3: high, and Street Wraith above Curator. Beyond 3: low.
   - Toxic Deluge (`goldfish_inert`): zero.
   - Remand: low-mid (the existing rule's tier 5).
   - Eldrazi in hand: low. They are castable only at 10-11 mana; their value is as walk targets.
2. **Position value at go-off.** Positions 1..k are drawn before go-off, and position k+1 onward is
   what the first walk sees.
   - **k = 0:** every Eldrazi put back should be the first CREATURE at its walk. With c cycles and e
     Eldrazi in hand, put back min(c, e, 2), Emrakul copies first. Dead noncreatures (Deluge, a
     surplus Cyclone) and surplus lands go ABOVE or BETWEEN them, because the walk bottoms them. A
     non-Eldrazi creature (Wraith, Curator) above an Eldrazi is a heavy penalty: the walk hits it
     first.
   - **k = 1:** position 1 is the card drawn next turn (a keepable card we do not need now), and
     the Eldrazi (Emrakul first) is position 2. With two slots, only ONE Eldrazi can be placed for
     a k = 1 go-off: the top card is drawn in the draw step. Two Eldrazi back is a k = 0 shape
     only.
   - **k >= 2:** an Eldrazi put back is drawn before go-off, so the walk-placement value is zero.
     Put back the least needed cards, the one wanted sooner on top. This is the only case where the
     shuffle branch below is admitted.
3. **The shuffle branch** (USER: *"use Brainstorm or Brainsurge to throw away bad cards with a
   shuffle"*; *"Naturally, you don't do this when you are in position to go off"*; *"If it says we
   can go off next I wouldn't consider it either"*). Admitted only when ALL of these hold:
   - k >= 2;
   - the hand holds 1-2 dead or near-dead cards;
   - a piece is missing (Cyclone, its mana, a cycler);
   - a shuffle is available AFTER the cantrip this turn: an unused land drop with Scalding Tarn in
     hand, an activatable Myriad Landscape, or a castable Long-Term Plans.

   In that branch the put-back is the dead cards in either order (F3 makes the order free), and
   Eldrazi may stay in hand. Shape (ii), next turn's fetch: the keepable card on top, the dead card
   second.

**Long-Term Plans** puts its card 3rd from top (`tutor_library_position: 3`, cards.json:13924).
After an LTP-for-Emrakul, a later put-back of two noncreatures keeps Emrakul the first creature
(positions 1-2 dead, Emrakul at 5). Two creatures put on top would bury it.

**The provider may read only what the player knows.** That means the hand, the battlefield, and
library cards it has seen and placed since the last shuffle (put-backs, LTP's target, Ponder's
look). Reading deeper library cards would make the order itself clairvoyant. If the engine does not
track known top positions, the score reads nothing below the cards the current resolution reveals.

**Ponder under the same estimate.** USER 2026-10-27: *"Are we treating which cards to keep on top
with ponder the same as Brainstorm? That is what I recommend there. We can search the shuffle as
needed and realistically it probably is safest to do so most of the time."*
- **The keep orders use the SAME scorer as Brainstorm's put-back** (`ScoreTopDisposition` over the
  resulting top sequence). Ponder removes nothing from hand, so only the position terms apply (plus
  the value of the card drawn now).
  - k = 0: draw the needed piece now, with the Eldrazi as the next creature (positions 2-3 may hold
    noncreatures).
  - k = 1: [draw now, draw next turn, Eldrazi].
- **The shuffle is ALWAYS searched.** It is never pruned (not by PONDER-GOOFF, not by a wave) and it
  sits in wave 0 of every Ponder fan, beside the scored best keep order. The score decides only where
  it sits within wave 0: ahead of the keep orders when the look holds no Eldrazi and nothing
  needed and k >= 2, behind the best keep order otherwise.

### 3.3 One generic hook shape for Cyclone, Niv and Hinata

The coordinator asked whether one rule shape covers both decks. **Yes, with one extension.**

- **Niv's put-back** (`MTG_NIV_BRAINSTORM_RULE`, DecisionProviders.cpp:53746-53850) is already a
  "keep-need" score (`NivWantOf`, the bucket want, 53384-53453) plus a "needed-soon" order
  (`NivOrderTier` / `NivTurnsUntilUsable`, 53455-53532). The least wanted go back first; among
  wanted cards, the later-usable ones go deeper.
- **Cyclone needs one thing Niv does not:** the value of a card at a position depends on what sits
  ABOVE it (the first-creature rule). So per-card independent position values are not enough.

**Proposed hook** (DecisionProvider; the default returns false and today's order stands, so Niv and
Hinata are byte-identical until they opt in):

```cpp
struct CantripReadiness { int k = -1; int c = 0; };   // draws before the payoff turn; payoff actions then
virtual bool CantripReadinessOf(const GameState&, int controller, CantripReadiness&) const { return false; }
// Score the RESULT of one disposition: the hand cards removed and the known top-of-library sequence
// after it (and whether a shuffle followed). Lower = earlier in the fan. Called for every candidate
// of one resolution; must be a pure function of (state, candidate) for ordinal lockstep.
virtual bool ScoreTopDisposition(const GameState&, int controller, const CantripReadiness&,
                                 const TopDispositionView& cand, int& score_out) const { return false; }
```

- The engine sorts the put-back candidates (C(C-1)+d <= 90), the Ponder orders (7) and, if it is
  ever searched, Preordain's 5 by score, with the existing order as the tie-break. Same ranks in
  both worlds: the sort runs inside the shared `PutBackPickAt` / `TopDispositionCandidates`.
- A generic helper implements the per-card decomposition (keep[a] + keep[b] - top[a][1] -
  top[b][2]) for providers that do not need sequence effects. Niv's `NivWantOf` / `NivOrderTier`
  drop straight into it, with its k = the combo-ready turn. Hinata's `HinataRank` gives keep and
  top.
- Cyclone overrides the sequence scorer for the walk.

### 3.4 Budget shapes that cover the high-yield variants first

All three below are inert at unlimited budget by construction: they return before reading anything
when `budget == nullptr || budget->Unlimited()`, the `ChargeDecisionUnits` pattern. Each fan admits
at least two ranks at every node (the no-greedy rule).

**B1. Rank waves over the fan (root and interior enforcing solves).**
- Wave 0 holds ranks 0..W-1 of each fan, with W provider-owned like `TutorSearchWidth`, and W >= 2.
  Ranks >= W form a deferred tranche.
- The tranche is walked after a NON-aborted pass at the same sub-depth, budget-checked per plan,
  before the next pass starts. This is the turn-complete preference: this turn's put-backs before
  the next ply.
- Machinery: the lazy layout already synthesises fan members by index (`CandSeq`,
  `PbLazyLayout::PutBackClone`, 71859). The tranche is an index range per fan, walked like
  `BpWaveWalker` (72018) or the group waves (94366-94490).
- **Comparator:** the bp and group waves accept strictly-better win turns only. For byte-identity
  at unlimited, B1 does not defer at all when the budget is unlimited. At a large finite budget,
  every rank is still reached.

**B2. Exhaustion-gated admission in NESTED solves.**
- A nested in-window rollout turn (non-enforcing) admits ranks >= W only while the decision budget
  is not exhausted. Today these solves evaluate every rank after exhaustion (96% of variant applies).
- This is a re-price of the next turns' depth in favour of the current turn's frames, which is the
  USER's turn-complete preference. At unlimited budget it is never exhausted.

**B3. Set-then-mirror waves.** Wave 0 holds each unordered pair once, in the order the score prefers
(28 of 56 at C = 8). The mirrors follow in a later wave. Under F3 the mirrors are folded away
anyway, so B3 matters only off the shuffle branch.

**B4. Per-variant price** (`MTG_PUTBACK_PRICE`, already built). Re-measure only after the order
lands. It composes with B1.

**Recommended order per deck** (wave 0 = the head of the scored order):
- **Cyclone.** k = 0: Eldrazi placements per §3.2 (min(c, e, 2) Eldrazi, Emrakul first, dead
  noncreatures above), then pairs of surplus only, then the rest. k = 1: [next-turn card, Eldrazi],
  then surplus pairs. k >= 2: surplus and dead pairs, then the shuffle-branch dumps (when admitted),
  then the rest. Ponder: wave 0 = the best scored keep order + the shuffle, always (USER), per §3.2.
- **Niv.** Unchanged unless the USER drops `MTG_NIV_BRAINSTORM_SINGLE`. If a staged fan replaces the
  single pick, the head is Niv's existing rank 0.
- **Hinata.** Ponder only. Rank 0 is `HinataRank`'s order. Wave 0 should be the heuristic order plus
  the shuffle plus one alternative top card (W = 3).

### 3.5 Prunes (provider restrictions; lossy; each its own lever and A/B; the USER decides)

Each prune can ship in two forms. The hard form removes the options at every budget: lossy, the
USER's call. The staged form puts the pruned set in a later wave (B1/B2) and stays complete. Measure
both.
- **PB-KEEP.** Never put back the sole Cyclone, a land needed for {3}{R}{R}, or the last of up to 3
  cyclers while surplus or dead cards fill the slot.
- **PB-ELDRAZI** (k <= 1, c >= 1, Eldrazi in hand). Only pairs that place min(c, e, 2) Eldrazi,
  Emrakul first, each the first creature at its walk.
- **PB-NOCREATURE-ABOVE** (k = 0). Exclude a non-Eldrazi creature above an Eldrazi.
- **PB-SHUFFLE.** The dump branch exists only when §3.2's conditions hold. Otherwise the put-backs
  that keep Eldrazi in hand while discarding to a shuffle are excluded.
- **PONDER-GOOFF.** Keep only the orders that put the Eldrazi at the go-off position, plus the
  shuffle.
- **SINGLE** (exists). Rank 0 of the new score. Re-measure: the old rule's rank 0 puts Emrakul on
  top of a Deluge, which leaves Deluge on top after the walk. When a dead card is present it also
  prefers an Ulamog over Emrakul. Both contradict the USER's rule, so SINGLE should improve with the
  new order.

## 4. Memory: why the plan cache fills, and the fix

**What fills it.** Every fanned base plan is cloned w - 1 times as a whole `Plan` (496 bytes,
TurnSolver.cpp:77093) plus its `actions` vector (about 480 bytes per `Action`), so roughly 2 KB per
clone for a 3-action plan. The pool (`plancache`, 78825-78918) prices entries with
`ApproxPlansBytes` (78798-78815).
- **The enum memo stores lazy entries compactly but charges them at FULL-list size**
  (`PbLazyListBytes`, 79132-79149, 79353). That was chosen deliberately so admission matches a
  non-lazy run. So the pool reports "full" for bytes that are never resident.
- **The breakpoint continuation cache stores materialised lists.** `BpDeriveContinuationList`
  (96616) passes no lazy layout, and the put-back and Ponder axes fan inside derivations (74372-74374).
  `BpEnumEntryFor` stores and prices the full list (97439-97497). The m2 memo (79474-79481), the
  second-main hosts and the other enumerations listed at 87952, 91417, 91837, 94388, 94647 and
  95118 are non-lazy too.
- Probe: of 2.79M put-back clones emitted in one game, only 415K were lazy, so **about 2.37M were
  built as full Plans**. Across the 126-game attribution, 55.3M of 89.2M.
- The ledger's numbers agree: unbounded pool demand 3.27 GB and max RSS 3.85 GB per worker. With
  the single pick it is 123 MB and 190 MB.

**STATUS (2026-10-28, branch `cyclone-mem`; full record in the ledger's "Memory: M1/M3 lazy fans + M2 stored-size
pricing").** M1 and M3 BUILT and byte-identical (0fce7726a): the bp cache holds its lists lazily (a selection over
blocks of fans), the m2 memo's store too, and the layout holds several blocks and fan kinds (put-back, look-top, Ponder
ORDER). Measured first: on the 8-game probe the bp cache was 100% of the pool's peak, 86% of its plans put-back clones and
5.7% Ponder clones; the m1 / m2 memos held almost nothing. Probe max RSS 2.08 GB -> 0.25 GB; a 2-thread 120-game batch
20.1 GB -> 0.92 GB. M2 BUILT and ADOPTED DEFAULT ON (`MTG_PLAN_CACHE_STORED_PRICE`; =0 restores the
materialised price): on held-out Cyclone seeds (d3 x200 + d5 x100, the shipped 2.4 GB pool) 300 / 300 games identical,
refusals 4,458 -> 0 and the `[determinism] WARNING` gone; the same manifest at 6 and 12 threads is byte-identical. The
probe's pool peak 1.77 GB -> 0.16 GB.
NOT lazy (and why): the Ponder keep-vs-shuffle pair and the tutor fan -- later axes clone those clones.

**Fixes:**
- **M1 (sound, byte-identical in content).** Carry `PbLazyLayout` through `BpDeriveContinuationList`
  into `BpEnumEntry`, and through the m2 memo.
  - Obstacles (all in TurnSolver.cpp): `MoveOrderPlans`, the condemnation stable partition and the
    new-only filter all reorder the whole list (96948, 97000). That needs a permutation, as
    `PbLazyOrderFs` does for FSLineWin (71948-71979). The node's `g_bp_cands_pb` reads every entry's
    group and rank (55893-55897). The executor's `EnumerateBreakpointPlans` copies by value
    (95946-95950).
  - Verify the same way as `MTG_PB_LAZY_CANDS_VERIFY`.
- **M2 (re-price).** Charge lazy entries at their STORED size. This changes pool admission and so
  cache residency and units. Test it as a play change (expected neutral). It also removes the
  accounting-only refusals behind the ledger's `[determinism] WARNING` (Q34).
- **M3.** Generalise the layout to several fan kinds per list (`fan_kind` exists, 71721; today one
  block per layout, 76653) so Ponder, tutor and look-top fans are lazy too.
- **F2's exact width** removes about 45% of the clones outright, and B1's tranche is synthesised only
  when walked.
- **Do not cache dominated variants.** With F2-F5 declining ranks before they are built, nothing
  needs to be stored for them. A stored list needs only base plans plus per-fan (base, width, group)
  records.

## 5. Per-node waste

- **The provider ranking is recomputed per variant.** `PutBackPickAt` rebuilds the classes and calls
  `PutBackRanking` on every apply (SpellEffects.h:36664-36692). Cyclone's rule scans the battlefield
  and hand and stable-sorts. Niv's values the whole hand greedily. All members of one fan resolve on
  the same state. **Fix:** a thread-local per-fan cache keyed on (`putback_group`, a fingerprint of
  the resolution state: hand numbers, battlefield, life, mana, land-played flag), holding the class
  order and the go-off estimate. Add a verify mode. Byte-identical.
- **The prefix is re-applied per variant.** Each of about 55 variants copies the GameState and
  re-applies the land, the accelerants, and the Brainstorm's cast, payment and draw before reaching
  the put-back. **Fix:** snapshot once per fan at the put-back resolution and resume per rank, the
  `BpPrefixSnap` pattern (23407-23409) used by the wave loops. Units unchanged (one per apply), wall
  only.
- **`PutBackAxisWidth` walks hand + library per base plan per enumeration**, the WHOLE library when
  "unsafe" (23959). F2 replaces it with the exact count over hand + `draw` cards.
- **Clamped ranks** occupy list slots and `candidates_done` (F2).
- **Ponder** builds `TopDispositionCandidates` vectors per resolution (SpellEffects.h:36022-36066).
  It is small; fold it into the same per-fan cache.

## 6. Prioritised implementation plan

"Decks" are the decks whose play moves. Mulligan bottoming is about 80% of Cyclone's total units
until a keep table exists (ledger phase 5; the USER deferred the mulligan stage). So measure
kept-game units separately (the existing `MTG_CANTRIP_ATTRIB` split), or the gain is diluted
five-fold.

| # | item | class | expected gain | decks | measurement |
|---|---|---|---|---|---|
| 1 | **Go-off estimate {k, c} + scored ORDER** (`CantripReadinessOf` / `ScoreTopDisposition`; CycloneProvider; generic per-card helper) | ORDER (re-price, inert at unlimited) | ~0 alone; the precondition for 2, 6 and the SINGLE re-measure | Cyclone (Niv and Hinata opt in later) | Paired held-out A/B order-on vs order-off at the shipped budget (d3 b10 x200, d5 b20 x100, one pooled batch, per-job flags), plus units. Second arm: SINGLE under the new order vs the old rule (measures rank-0 quality). Third: the go-off estimate's cost (census) |
| 2 | **B1 + B2: rank waves at the root, exhaustion-gated admission in nested solves, W >= 2** | re-price, inert at unlimited | the bulk of the 57% put-back share. Expected between today and SINGLE's x0.25 kept-game units; first pass fits | Cyclone; Hinata (Ponder W = 3) | Arms W = 2/4/8 vs today vs SINGLE, held-out paired. `--budget-ms 0` byte-identity on ~20 games (structural inertness). Every slower game re-run at d8 b0 (adoption bar). Then smoke/regression GT accept for Hinata |
| 3 | **F2 exact width** (sharpen `PutBackPredictionUnsafe` to actions before the cast) | sound identity; re-prices the overrun projection | about 45% fewer clones; memory; truer overrun guard | Cyclone (Niv only under human or unpruned play, while SINGLE is ON) | `MTG_PB_LAZY_CANDS_VERIFY`-style check (the emitted width equals the resolution count when "safe"); held-out A/B; units |
| 4 | **F4 stranded-cast pairs + F5 cross-base truncated-prefix decline** | sound (F5 identity; F4 degenerate-line) | F4 -25..-46% on fans with tails in nested solves; F5 (tails - 1) x w applies per node | Cyclone | Verify modes (apply both, compare the realised record or state key); held-out A/B; units |
| 5 | **F3 shuffle mirror fold** (plan-level now; node-level for continuations) | sound identity (exact under `ShuffleByKey`) | halves the shuffle-branch fans | Cyclone | Verify (state equality); A/B read with the re-salt clairvoyance method |
| 6 | **Prunes PB-KEEP, PB-ELDRAZI, PB-NOCREATURE-ABOVE, PB-SHUFFLE, PONDER-GOOFF, SINGLE**, each hard and staged | LOSSY when hard (USER's call); staged = re-price | fan ~56 -> ~4-8 hard; SINGLE x0.25 | Cyclone | One pooled batch, one per-job flag per lever plus the all-on arm, held-out paired d3/d5; slower games at d8 b0 and the re-salt reading; results to the USER |
| 7 | **M1/M3 lazy fans in the bp cache and m2 memo; M2 stored-size pricing** -- DONE 2026-10-28 (§4 STATUS) | M1/M3 sound (byte-identical); M2 re-price | measured: probe RSS 2.08 -> 0.25 GB, pool peak 1.77 -> 0.16 GB (M2); determinism warning gone at the shipped pool | Cyclone (Niv / Hinata / LandfallElementals lists lazy too, byte-identical) | Verify mode 0 mismatches; 120-game byte-identity vs the pre-change binary; smoke play-changed 0; M2 held-out A/B 300 / 300 identical (ledger) |
| 8 | **Per-fan ranking cache + prefix snapshot** | sound, byte-identical | wall only (prefix apply x (w-1) per fan; the ranking per apply) | all three | Byte-identity on smoke + 100 Cyclone games; wall A/B with `MTG_PGO=0` on both arms |
| 9 | **F1 Ponder identical-name fold; F7 tail fold; F6 inert-class merge** | sound | Ponder 6 -> 2-3 variants on duplicate looks; small elsewhere | Hinata, Cyclone | Verify mode; Hinata smoke/regression GT accept plus a held-out A/B |
| 10 | Preordain as a searched scry (Niv Q14), using item 1's order + item 2's waves | completeness (adds width 5, F1-folded) | quality, not cost | Niv, Hinata | The USER's decision first; then held-out A/B |

## 7. Open points for the USER (collected, not blocking)

1. Should items 2 and 6 ship as staged waves (complete) or as hard prunes (lossy)? Both will be
   measured. The design recommends staged for everything except where the USER prefers a
   rule-as-prune, as with Niv's SINGLE.
2. The go-off estimate counts at most one land drop per turn and does not count unknown draws. With
   a missing piece it returns k = 2+, which turns off Eldrazi placement and admits the shuffle
   branch. Is "2+" the right reading of "a piece is missing"?
3. For k = 1 with two Eldrazi and c >= 2 on the go-off turn: the top card is drawn in the draw step,
   so only one Eldrazi can be placed below it. Confirm that the second stays in hand (or goes deeper
   when Brainsurge/Brainstorm gives a third slot later).
