# BreachingDragonstorm: cast-order draft (agent proposal, 2026-10-14, for USER review)

## 1. Status: **(D) generic tiers only, and even those do not play**

- `BreachingDragonstormProvider` (src/ai/DecisionProviders.h:626-645) overrides only the cleanup discard
  (`MTG_BREACH_BUCKET_DISCARD`). It has no `CastOrderRank`, no `MainPhaseOverride` and no `OrderOpaqueCastsByRank`.
  There is no order flag (`grep MTG_BREACH` in src finds only the discard flag). No cast-order ruling exists in
  `analysis-BreachingDragonstorm.md`, and `cast-order-rankings.md` has no section for this deck.
- **The report's 10/20 ranks are never read for this deck.** Every nonland card is `OrderingOpaque`
  (ManaPayment.cpp:2722: `cascade_max_mv>0` covers 5 cards, plus `shuffle_reveal_freecast` = CT and
  `etb_exile_until_nonland` = BD). The provider does not opt in (DecisionProvider.h:927, default false), so the
  executor (AIEngine.cpp:~6290-6392), the breakpoint continuation (AIEngine.cpp:~4984) and the rollout
  (TurnSolver.cpp:~42722) all cast in **plan order**. A probe confirms it: `[ord] ... opaque=1` and
  `[uo-order] route=opaque` on every cast.

## 2. What plays today

| pos | card(s) | main | rule that put it there |
|---|---|---|---|
| land | the land drop | - | first at depth 0; folded into the plan at depth > 0 |
| 1..n | every hand cast (all 7 names) | m1 (Altisaur m2) | **plan order** = enumeration order, i.e. hand position. **ALL TIED**: nobody chose this order |
| (report only) | creatures 10 / BD, CT, CFT 20 | | printed by `--cast-order-report` but not applied (opaque set) |

Every cast here is a re-solve breakpoint (each one walks the library). With `MTG_BP_NEW_ONLY` (default ON,
TurnSolver.cpp:4664), a continuation keeps only the plan's own pending casts and newly arrived cards. So the
plan's order is final: of the two orders of {A, B}, only the plan's one can be found, at any budget.
Altisaur's m2 label has no effect: `uses_second_main=no`, so the phase filter never runs (TurnSolver.cpp:20388).

**Stakes, structurally.** The cheapest hand cast costs 5, and nothing in the 60 makes mana, cuts costs, adds a
land drop or puts a card into the hand mid-turn. Every 2-mana land (depletion, sac) enters tapped, so turn N gives
at most 2(N-1)+1 = 2N-1 mana. A main with two hand casts needs 10 or more, so **it cannot happen before turn 6.**
Probes (single games, current tree): **0 two-cast mains in 150 games** (30 at shipped d5 b20, all won by T3-T4;
120 at d0, all won by T7). The order only matters in the slow tail. Expect an A/B near 0.

## 3. Proposed order (rank: lower = earlier)

| pos | card(s) | main | range | why (card text / plan) |
|---|---|---|---|---|
| land | land drop | - | - | **FIRST, and it costs nothing:** no card puts a land into the hand (cascade bottoms its misses, CT's misses go to the bottom and its hit stays exiled, BD's lands stay in exile), and the deck has no landfall. Playing the land first can only add mana |
| 1 | Breaching Dragonstorm x4 | m1 | - | A permanent that enters BEFORE its own walk (ETB trigger). So a Protege found by this walk, or by any later one, can enter as a copy of BD and fire the walk again |
| 2 | Boarding Party x4 | m1 | - | Its body is an entrant (6/3 haste). Its cascade (MV < 6) can only flip BD or CT, never a Protege, so going early costs it nothing |
| 3 | Annoyed Altisaur x4 | m1 | - | Entrant. Its cascade (< 7) can flip a Protege, so it goes after the entrants above |
| 4 | Maelstrom Wanderer x4 | m1 | - | Entrant. Two cascades (< 8) can flip a Protege. Its haste grant is static, so its slot does not affect combat |
| 5 | Creative Technique x4 | m1 | - | A sorcery, not an entrant. Its two walks (the demonstrate copy plus the original, any nonland) see every entrant above as a copy target for a Protege they find |
| 6 | Call Forth the Tempest x1 | m1 | - | Its damage = "total mana value of OTHER spells you've cast this turn", so it goes after the casts it counts. Cosmetic in the goldfish: it only hits opponent creatures, which never block |
| 7 | Sakashima's Protege x2 | m1 | - | **LAST:** "enter as a copy of any permanent that entered this turn". Every earlier cast only adds copy targets. Its own cascade hit (BD or CT) still resolves before it enters |

- **Main 1 for everything.** Every card either has haste, grants it, or free-casts from the library cards that
  can attack this turn (Boarding Party, or a chain into Wanderer). Nothing gains from combat having happened (no
  combat-damage triggers, no spectacle, no mana creatures), so main 1 loses nothing.
- **No ranges.** No card changes another card's cost or makes mana, so any order of a payable set is payable.
  The colour-exact subset check prices the set as a whole, and `BatchPrepayMainCasts` pre-pays it jointly
  where it engages. Stranded colours are the payer's problem, not the order's.
- **Ties:** only copies of the same card. Each pair of distinct cards has a reason above. Slots 2-6 matter only
  through Protege copy targets and the CFT count, so they are low-stakes, and the questions say so.

**Dragonstorm USER rulings (dragonstorm-user-cast-order.md), and which carry over**

| ruling | here |
|---|---|
| "after the breakpoint the cast order should follow my order" (USER 2026-10-10) | **CARRIES, and it is the main one:** every cast here is a breakpoint, so the order has to govern each continuation. Opting in to `OrderOpaqueCastsByRank` does that: the executor main, the continuation and the rollout all read `OpaqueCastOrderActive` |
| raw hand order is a defect (Dragonstorm gi533: an opening-hand Apex sat third, T5 became T8; `MTG_DRAGONSTORM_ORDER_OPAQUE`) | **CARRIES:** this deck is the same class today |
| "once they become available" (rule 2) | carries trivially: no card enters the hand mid-turn in autonomous play. In the viewer, a declined BD hit goes to hand and joins at its own slot |
| rule 5: an enabler (Apex) before the payoffs | **for BD only.** BD's body is what a later Protege copies. CT enables no hand card (its free casts come off the library), so the copy rule places it, not "enabler first" |
| Ruby Medallion "as early as possible while the line pays"; the rituals by cost; splice legality; Irencrag second-to-last; MTG_PAYABLE_ORDER fallback | do NOT carry: no reducer, ritual, splice card or cast restrictor in the 60, and no order-dependent payment |
| searched order (`WantsCastOrderingSearch`, `MTG_DRAGONSTORM_DRAGON_ORDER`) | does NOT carry: DecisionProviders.h:346 forbids deriving this provider from Dragonstorm's, and a fixed order is the design |

## 4. What changes and why it matters

1. **The order starts to apply at all.** Today hand position decides every multi-cast main, the same defect class
   as Dragonstorm's raw-hand-order Apex (DecisionProviders.cpp:11425 comment).
2. **Protege last: today a line is lost.** Example: turn 6+, 11 mana (five 2-mana lands plus a Mountain drop).
   The hand holds a Protege kept from the opener and a BD drawn later. Plan order casts Protege first, so it can
   copy only its own cascade chain. The line "BD, then Protege as a copy of BD" (a second BD walk, one more free
   cast) cannot be found at any budget. `MTG_BP_NEW_ONLY` drops a Protege-only continuation after {BD}, and the
   {Protege, BD} plan casts Protege first. The same holds for a hand Boarding Party (a copy is another 6/3
   haste, +6 damage). Note: applying the generic ranks would make this worse, putting Protege (a creature, 10)
   ahead of BD and CT (20).
3. **Entrants before sorceries (BD first).** A Protege found by a later walk can copy any earlier entrant. BD's
   walk can find a Protege that copies BD itself; a creature's cascade resolves before its body enters
   (CR 702.85a), so its own hit cannot copy it.
4. **CFT after what it counts.** Today CFT ties with BD and CT at 20, which falls to hand order. This only
   changes damage to the opponent's creatures, which never block, so it is cosmetic.
5. **Altisaur m2 -> m1.** Its cascade (< 7) can put a hasty Boarding Party, or a chain into Wanderer, onto the
   battlefield before combat; main 2 would waste them. This has no effect today but is the correct label.

## 5. Questions for the USER (default in brackets; one word settles each)

1. **Sakashima's Protege after every other hand cast?** [yes]
2. **The rest: "entrant" (BD > Boarding Party > Altisaur > Wanderer > CT > CFT, permanents before sorceries) or
   "enabler" (BD > CT > Boarding Party > Altisaur > Wanderer > CFT, Dragonstorm's rule 5)?** [entrant]. The
   difference is tiny either way: only which entrants a Protege found mid-chain can copy.
3. **Call Forth the Tempest second-to-last (its damage clause counts the earlier casts)?** [yes]
4. **Every cast main 1 (Altisaur m2 -> m1)?** [yes]
5. **Ship default ON once you agree (`=0` restores today's plan order), A/B'd on `breaching` only?** [yes]

## 6. Generic-order findings

- **G1 (report accuracy, likely fleet-wide):** `--cast-order-report` prints ranks as "what plays" for decks
  where every nonland card is `OrderingOpaque` and the provider has not opted in. For those decks the rank is
  never read and the real order is hand order. The report should print "NOT APPLIED: order-opaque set -> plan
  order" per card. Every deck built on cascade, draw, staging, retrace, impulse or solo-trick cards is affected.
  Each other draft should check this.
- **G2:** creatures 10 before noncreatures 20 is a prowess rule. In a deck with no prowess or cast-trigger
  reader it is an arbitrary tie-break, and for `enter_as_copy_of_entrant` it is backwards. Proposed generic
  fixes: (a) rank a copy-an-entrant card after every permanent cast; (b) gate creature-first on the deck holding
  a prowess-style reader.
- **G3:** there is no generic slot for "goes after the casts it counts" (`damage_opp_creatures_mv_cast`, storm).
  Dragonstorm's USER order ranks Dragonstorm last for the same reason.
- **G4:** `ClassifyMainPhase` puts a cascade creature without its own haste in m2 (Altisaur), although its
  cascade hit can attack this turn. The ETB mass-put Main1 rule (Muxus, TurnSolver.cpp ~20775) applies just as
  well to `cascade_max_mv > 0`.
- **G5:** `IsIdealOrderDraw` (DecisionProviders.cpp:2997) counts cascade, `etb_exile_until_nonland` and
  `shuffle_reveal_freecast` as draws, which gives the `[2..N]` ranges in the report. None of them puts a land or
  spell into the hand, so principle 1 ("draw before the land drop") cannot use them. No effect today (the draw
  tier is off).
- **G6 (soundness note):** `MTG_BP_NEW_ONLY` drops an old-card continuation as "the sibling's line". That only
  holds if the sibling casts the breakpoint card first. In an opaque set cast in hand order the sibling may run
  the other order, so the dropped order is chosen per game by hand position. A fixed rank makes that prune
  deliberate.
- **G7 (tooling, for every drafting agent):** `mtg` resolves cards.json relative to the CWD. Run outside the
  repo root, it loads no cards, prints no error and plays nothing ("unwon" every game). Probes need
  `env -C /workspaces/MagicDeckTester ...`.

## 7. Implementation notes

- `BreachingDragonstormProvider`: add `CastOrderRank`, `CastOrderTierName`, `OrderOpaqueCastsByRank()` (the
  essential one: without it no rank is read) behind `EnvOn("MTG_BREACH_ORDER", <USER's call>)`, with a heurarm
  slot appended. `=0` falls through to `DeckProvider`, which is byte-identical (plan order). Pattern:
  `DragonstormProvider::OrderOpaqueCastsByRank` (DecisionProviders.cpp:11431).
- Ranks by PARAMS: `etb_exile_until_nonland` 10. Creature with `cascade_max_mv > 0` and no
  `enter_as_copy_of_entrant`: 20 + `cascade_max_mv` (gives Boarding Party 26 < Altisaur 27 < Wanderer 28,
  narrowest walk first). `shuffle_reveal_freecast` 40. `damage_opp_creatures_mv_cast` 50.
  `enter_as_copy_of_entrant` 60. Anything else goes to `GenericProvider::CastOrderRank`.
- Optional: `MainPhaseOverride` returns Main1 for any `cascade_max_mv > 0` creature (no effect while
  `uses_second_main=no`). The land drop is already first; `LandDropCastOrderRank` (the Mirrorwing pattern) is
  only needed if a later change moves it.
- No engine limitation blocks this. Every position is unconditional, so a scalar rank is enough, and the opaque
  rank sort plus `ApplyDeckCastArrangement` / ladder already run at all three sites. Expected play change: only
  mains with 2+ hand casts (turn 6+), so smoke/regression should move by digest in a handful of slow games. Run
  the adoption A/B at d0 (longer games) and shipped settings, held out, to the deck-aggregate <= 0 bar.
- Not verified by probe: plan order = hand position (from the code reading). No probed game reached a two-cast
  main to show it live.
