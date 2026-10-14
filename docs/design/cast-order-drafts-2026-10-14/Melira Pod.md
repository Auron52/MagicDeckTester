# Melira Pod -- cast-order draft (2026-10-14)

Probes cited below live in `build/scratch/castorder/probe_MeliraPod/scen/` (`mtg --scenario`, run from the repo
root, Release binary at HEAD 77e094a2).

## 1. Status: **(D) generic tiers only**

- `MeliraPodProvider` (DecisionProviders.h:2714) overrides no `CastOrderRank`, `CastOrderRankLatest`,
  `MainPhaseOverride` or `ClassifiesMainPhases`. There is no `MTG_POD_*ORDER` / `MTG_MELIRA*` flag, so every rank
  comes from `GenericProvider::CastOrderRank` (DecisionProviders.cpp:3013; creatures 10 at :3183, everything else 20).
- No USER order exists. The ledger (`analysis-Melira Pod.md`) and the discard proposal hold no USER ruling on cast
  order or main phase. The USER's binding view of the pieces is the Pod ladder ("Sacrifice creature for 1, Melira or
  Voice or sac creature on 2. Finks + Recruiter on 3. Redcap + Ranger + Celes on 4") and "Celes is primarily a Melira
  replacement that draws."
- `cast-order-rankings.md` §Melira Pod is stale: it shows `provider: Generic` and `uses_second_main=no`. Today it is
  `MeliraPod` and `yes` (Pod and convoke both open a second main).

## 2. What plays today

| pos | card(s) | main (label) | rule |
|---|---|---|---|
| 0 | land drop | m1 | generic: no cantrip, so the drop goes FIRST. **Orzhov Basilica** is the exception: the engine holds a Karoo until after the casts (`karoo_deferred`), so it bounces a land the casts already tapped. |
| 1 | **TIE x18, hand order**: Birds, Ignoble, Carrion Feeder, Bloodthrone, Melira, Vizier, Scavenging Ooze, Voice, Kitchen Finks, Recruiter, Rec Sage, Severance Priest, Murderous Redcap, Ranger, Chupacabra, Celes, Felidar, Reveillark | m2 (Ignoble m1, exalted) | creature tier 10 |
| 2 | **TIE x2, hand order**: Chord of Calling, Birthing Pod | m2 | other noncreature 20 |

- No card in the list is `OrderingOpaque`. Celes (`etb_discard_any_*`), Recruiter and Ranger (`tutor_to_hand`) and
  Felidar (`etb_blink_permanent`) are not in that list, so every cast set takes the rank sort. The report's ranks are
  what plays.
- **The main column never reaches play for this deck.** `ClassifiesMainPhases()` is false, so `MainPhaseFilterActive`
  returns false (TurnSolver.cpp:20396). `MTG_ORDER_M1_FIRST` is off (ManaPayment.cpp:2666), so the labels do not even
  break ties. The search offers every cast in both mains. In 8 probe games (d3/b10) essentially everything was cast
  in main 1.

## 3. Proposed order

| pos | card(s) | main | range | why |
|---|---|---|---|---|
| 0 | **land drop** | m1 | - | No information card comes before it. The deck has no cantrip, the to-hand tutors' picks are searched or known, and Celes is mv 4. Orzhov Basilica stays after the casts (engine `karoo_deferred`, unchanged). |
| 1 | Celes, Rune Knight | m1 | - | Her rummage is the deck's only random draw, so it resolves first (principle 1). Her "other creatures enter from a graveyard -> +1/+1 counter on each creature" watcher must already be in play when a same-main Reveillark returns bodies from the graveyard. Recruiter's pick counts Celes as the enabler only once she is **on the battlefield** (`NotePodRoles`), so she must land before Recruiter resolves, or Recruiter fetches a redundant Melira. |
| 2 | **TIE**: Birds, Ignoble, Carrion Feeder, Bloodthrone Vampire, Melira, Vizier, Scavenging Ooze, Voice, Kitchen Finks, Murderous Redcap, Recruiter, Ranger of Eos, Reclamation Sage, Severance Priest, Ravenous Chupacabra | m1 | - | Order provably cannot matter; see the proof below the table. |
| 3 | Felidar Guardian | m1 | - | Its flicker target is fixed at enumeration (`chosen_x` = a permanent in play). Casting it after the other creatures means a targeted land has already been tapped when it resolves, so it comes back untapped as +1 mana (for the trailing Pod `{1}{G/P}` / Ooze `{G}`). This only matters when the prepay declines (below). |
| 4 | Reveillark | m1 | - | The evoke LTB picks "missing loop piece **on the battlefield**" at resolution (`ReviveCandidates`). If this main's pieces land first, it returns what is still missing rather than a copy of a piece being cast anyway. It goes after Celes (pos 1). |
| 5 | Chord of Calling | m1 | - | Goes after every creature, for convoke. `ApplyConvokeTaps` re-classifies at apply time, so a body cast earlier THIS main (summoning-sick = a free body) pays convoke and spares the attackers. **PROBED** (`chord_fresh_convoke2.json`): Feeder first, then Chord X=4, pays `{3}{G}{G}` and wins T6. Chord-first cannot convoke the Feeder. It goes before Pod: X > 0 makes the whole-turn prepay decline (`PP_XSPELL`, TurnSolver.cpp:38016), so a Chord turn is paid card by card, and the `{G}{G}{G}` bill should pay before Pod's generic one. |
| 6 | Birthing Pod | m1 | - | Nothing reads Pod at cast time: its activation runs in the trailing pass after every cast, and the hand-Pod pairing still covers casting and activating it in the same main. `{3}{G/P}` (or `{3}` + 2 life) is the most flexible bill, so it pays last. |

**Ranges:** none. No card here impacts affordability within a turn: no ritual, rock, reducer or cantrip, and dorks
cast this turn are summoning-sick.

**Proof for the pos-2 tie.**
- None of these cards has a cast trigger, a cost reducer, or a static that changes another card's cost.
- Every activation runs in the trailing pass after all casts (TurnSolver.cpp:42587): persist loops, the outlets,
  Pod, Ooze. So enablers vs persist bodies vs outlets cannot interact at cast time.
- The ETBs ignore same-main casts:
  - Finks gains life; Redcap hits face; Chupacabra kills the largest spawn.
  - Ranger's pick is board-agnostic (`TutorHandPutList`).
  - Recruiter's pick reads roles "present in hand OR battlefield", and a same-main cast only moves a card from one to
    the other. Celes is the one card counted on the battlefield only, which is why she sits at pos 1, outside the tie.
  - Rec Sage and Severance are not modelled (declined).
- A multi-cast plan without an X spell is paid jointly by `BatchPrepayMainCasts` before any cast resolves, so colour
  sequencing is not an order question.
- Dorks cast this turn make no mana.
- Melira's legend rule does not depend on cast order.

## 4. What changes and why it matters

The order itself changes little. Today's generic tiers are already right in the one place order is load-bearing.
The real findings are the main-phase column and one line that cannot be expressed.

1. **Chord after the creatures is load-bearing. Keep it and pin it.** Probe above: the T6 kill exists only because the
   Feeder resolves before Chord and convokes. Today this holds only by accident of the generic 10 < 20 tiers. A
   Melira rank would pin it.
2. **The report's m2 labels are wrong for this deck. Every cast here is m1.** Activations are enumerated against the
   board as each main starts. A body cast in m2 cannot be Podded or persist-looped in that m2, because no new site-9
   key appears: the Pod or outlet key already exists. So deferring a body deletes the same-turn line, which is the
   same class as the documented Skirk Prospector / walker / Muxus pulls.
   - **PROBED** (`redcap_phase_classify.json`): Melira and Feeder in play, Redcap in hand. Default: cast m1, loop m2,
     win **T6**. With the labels enforced (`MTG_PHASE_CLASSIFY=1`): Redcap is pushed to m2, can't loop, win **T7**.
   - Nothing is lost today, because the filter is off. Main 1 weakly dominates for every cast here. The goldfish
     never blocks, combat creates no mana, and a Chord cast in m1 can still convoke only non-attackers.
3. **A persist body that ENTERS this main can't be looped before combat, at any budget.** This is not an order
   defect, but it is exactly the line an m1/m2 review assumes exists. The s10 fix pairs a *closer* cast from hand
   with the loop. The loop still needs the *body* in play when the main starts (`closer_active || closer_castable`,
   TurnSolver.cpp:29325, then a walk over the battlefield).
   - **PROBED at d6 / budget 0**, Melira and a non-sick Feeder in play: Kitchen Finks cast from hand
     (`finks_from_hand_growloop_d6b0.json`), Pod-put (`pod_put_finks_growloop.json`) and Chord-put
     (`chord_put_finks_growloop.json`) all win **T7**.
   - The real line, grow the Feeder to 20 and attack, is **T6**. The engine loops in m2 (life 56) and kills a turn
     late.
   - A Redcap entering this main is fine, because its kill needs no combat.
   - Under the USER's completeness invariant this is a defect. It needs its own engine fix.
4. **Tie-breaks (Celes first; Felidar, then Reveillark, last among creatures; Chord before Pod).** All come from card
   text. Each needs a 7-10-mana main to be live: Celes + Recruiter is 7 and needs a dork for her {R}; Celes +
   evoked Reveillark is 10. I found no line lost today. They replace "hand order" with an order
   someone chose.

## 5. Questions for the USER

1. Adopt this order for Melira? It is the generic order plus the four pinned tie-breaks above. Default: **yes**.
2. Label every Melira cast **m1** and keep the m1/m2 filter **off**, so the search keeps choosing the phase?
   Default: **yes**. Enforcing the current m2 labels costs a turn (probe 2).
3. Queue an engine fix for #3 (loop a persist body that entered this main, before combat)? Default: **yes**. It is
   an unfindable line, so it is a defect rather than a heuristic.
4. Chord before Pod (colour-hungry bill first), or Pod before Chord? Pod-first lets a Chord-put Recruiter's pick see
   an active Pod for its "Pod fuel" tier. Default: **Chord first**.

## 6. Generic-order findings

- **`ClassifyMainPhase` sends a summoning-sick body to Main2 even when a source in play will consume it this turn**
  (Pod, or a free sac outlet with a loop closer). The licence "a Main2 class moves a line, it cannot delete one" fails
  there. Proposed generic pull: a creature is Main1 while a `pod_mv_delta` source or a free `sac_creature_outlet` is
  on the battlefield or castable. The comment that `MTG_PHASE_CLASSIFY` is "safe to set SUITE-WIDE" is false for
  Melira (probe 2).
- **The creature-before-noncreature tier (10 < 20) is load-bearing for convoke.** Any generic rework must keep convoke
  spells after the turn's creatures.
- **Every X spell with X > 0 declines the whole-turn prepay (`PP_XSPELL`).** On such turns the casts pay one by one in
  rank order. A tie between noncreatures is then a payability question, not a cosmetic one.
- **`--cast-order-report` should say when the main column is inert**, i.e. the provider does not classify main
  phases and `MTG_ORDER_M1_FIRST` is off. Read today, the Melira report looks like "almost everything is cast in
  main 2", which is the opposite of what plays.

## 7. Implementation notes

- `MeliraPodProvider::CastOrderRank`, keyed by params (house style), behind a default-ON-on-approval lever
  (suggested `MTG_POD_CAST_ORDER`). Everything else falls through to `GenericProvider::CastOrderRank`, which gives
  creatures 10 and Chord 20.

  | rank | slot | param |
  |---|---|---|
  | 9 | Celes | `other_creature_gy_enter_team_counters > 0 \|\| etb_discard_any_draw_bonus > 0` |
  | 11 | Felidar | `etb_blink_permanent` |
  | 12 | Reveillark | `ltb_return_creatures > 0` |
  | 21 | Birthing Pod | `pod_mv_delta != 0` |

  Ranks 9 and 11 do not collide with the generic 9 (dork growth / watcher tiers): neither can be live in this deck.
- **Main phase:** `MainPhaseOverride` returns `Main1` for every cast. It is inert in play while
  `ClassifiesMainPhases()` stays false, but it makes the report truthful and makes `MTG_PHASE_CLASSIFY` safe for this
  deck.
  - A sound collapse may also be on offer: m1 weakly dominates every cast here, so creature casts need not be
    re-enumerated in m2. The engine has no "Main1-only" filter class today; that would be new work and a separate
    measurement.
- **No engine limitation blocks the proposed order.** It is a plain scalar rank; there is no conditional position
  and no range.
- **Separate engine gap #3.** The persist-loop emission walks the battlefield for the body. The fix is the persist-body
  twin of `MTG_POD_HAND_PAIR`'s closer-castable pairing (body cast from hand or put this main, loop in the same
  plan), plus a matching `SubsetHasUnclosedPersistLoop`-style stranded-loop guard. Guard scenarios: the three probes.
