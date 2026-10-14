# Goblins: cast-order draft (agent proposal, 2026-10-14; for USER review)

## 1. Status: **(D) generic tiers only**

- `GoblinsProvider` (src/ai/DecisionProviders.h:1338-1440) overrides no `CastOrderRank`,
  `CastOrderRankLatest`, `MainPhaseOverride` or `ClassifiesMainPhases`. Play therefore runs
  `GenericProvider::CastOrderRank` (DecisionProviders.cpp:3013). None of the ~40 `MTG_GOBLIN_*` flags is
  an order flag. **No USER order exists, so none is OFF.**
- The USER's only order-relevant words are from the discard review (goblins-discard-policy-proposal.md,
  USER 2026-10-09): *"a Piledriver should be played earlier than a lord."*
- **Rank 8 "(unclassified)" is the generic SUBTYPE COST-REDUCER tier.** The code is
  `if (!def.params.reduces_spell_subtype.empty()) { return 8; }` (DecisionProviders.cpp:3154-3157), with
  the comment "cast it just BEFORE other creatures (8 < 10) so a same-turn Goblin it discounts is
  enumerated after the reducer is online". It was added with Warchief (analysis-goblins.md:256) and is
  shared with Ragemonger's coloured-pip tier (:3084-3093). The rank is right and it carries weight: the
  in-order payability walk and the executor's live repricing both assume Warchief resolves first. The label
  is missing only because the report's `tier_name` switch (main.cpp:7885-7906) has no `case 8` (or 9).
- **Leverage is low.** I ran 64 single games at the shipped d6/b40 (seeds 2001-2064, `MTG_USER_ORDER_TRACE`):
  - 421 main phases in total; only **8** cast two or more spells from hand.
  - Warchief never shared a main with a spell it discounts.
  - Matron shared a main once ("Chieftain|Matron").
  - No set was order-opaque (417 `clean`, 4 `replay`). The deck has no draw/staging/impulse card, so the
    rank is what plays.

## 2. What plays today

| pos | card(s) | main | rule |
|---|---|---|---|
| land | land drop | m1 | no cantrip in the list, so FIRST at depth 0; folded into the plan at depth > 0 |
| 8 | Goblin Warchief | m1 | subtype cost-reducer tier (label missing, see above) |
| 10 | Lackey, Skirk, Piledriver, Mogg War Marshal, Hordemaster, Stingscourger, Chainwhirler, Chieftain, King, Matron, Pashalik, Krenko, Twinshot, Siege-Gang, Muxus **(15-way TIE)** | m1; Piledriver/Chainwhirler/Twinshot m2 | "creature". A non-ritual tie keeps plan order (ManaPayment.cpp:2645-2662), i.e. **hand order, which nobody chose**. The report's mv-then-name listing is display order, not play order. |
| 20 | Aether Vial, Lightning Bolt **(TIE)** | m1 / both | "other noncreature" |

**The main column is advisory for Goblins.** `ClassifiesMainPhases` is not overridden, so
`MainPhaseFilterActive` is false (TurnSolver.cpp:20396). `MTG_ORDER_M1_FIRST` is off too. Every cast is
offered and searched in both mains.

## 3. Proposed order

| pos | card(s) | main (advisory) | range | why |
|---|---|---|---|---|
| land | land drop | m1 | - | FIRST. Nothing in the list draws: Matron fetches "a Goblin card", Muxus and Hordemaster never put a land in hand. |
| 1 | Goblin Warchief | m1 | - | "Goblin spells you cast cost {1} less": before every Goblin with generic in its cost (principle 2). Unchanged. A Vial put of it ranks as the card (8), so it also goes first. |
| 2 | Goblin Lackey = Skirk Prospector **(tie)** | m1 | - | Provably order-free: both cost {R} (nothing for Warchief to discount) and neither has an ETB. Skirk's sacs are pre-cast floats or a post-entry continuation, so they never land between two casts. |
| 3 | Mogg War Marshal | m1 | - | Two bodies (ETB token). Bodies go before bigger casts (the Three Tree City count, §4c). |
| 4 | Goblin Piledriver = Stingscourger **(tie)** | m2 (m1 under a haste lord) | - | Piledriver goes ahead of every lord (USER, above). Within one main that is neutral: its +2/+0 counts attackers at declare-attackers. Stingscourger's bounce is inert vs the goldfish, so the tie is order-free. |
| 5 | Rundvelt Hordemaster | m1 | - | The 2-mana lord, after Piledriver. It is a Goblin-death watcher, so it goes before the one death a cast can cause: a 2nd Muxus's legend rule (§4d). |
| 6 | Goblin Chainwhirler = Pashalik Mons **(tie)** | m2 / m1 | - | Order-free with each other. Chainwhirler's ETB is face damage; Pashalik is the other death watcher (before Muxus, §4d). |
| 7 | Goblin Chieftain = Goblin King **(tie)** | m1 | - | Static lords with identical cost and no ETB, so provably order-free. |
| 8 | Krenko, Mob Boss = Twinshot Sniper **(tie)** | m1 / m2 | - | Order-free. Krenko's tap is a trailing activation after every cast (AIEngine.cpp:6628), so X already counts this main's bodies. Twinshot's ETB is 2 to the face. |
| 9 | Siege-Gang Commander | m1 | - | Four bodies. Its sac ping is a trailing / main-2 activation. |
| 10 | Muxus, Goblin Grandee | m1 | - | Reveal-6 happens BEFORE Matron searches and shuffles, so the reveal pool still holds Matron's target and Matron ranks its fetch against the post-reveal board. Late, so the bodies before it are already counted. |
| 11 | Aether Vial = Lightning Bolt **(tie)** | m1 / both | - | Order-free. Vial enters with no counters. Bolt needs a non-Cavern red source, and the payer already spends Cavern's creature-only colour on creatures (`MTG_RESTRICTED_MANA_RANK`). |
| 12 | Goblin Matron | m1 | - | **LAST** (§4a). m1 matters: a fetch made pre-combat is a Goblin Lackey put this combat. |

Shape: reducer → bodies by cost (two bodies first within a cost, lords last) → noncreatures → the tutor.

Ranges: none. Warchief is the only card that re-prices a later cast, and its ideal and cost-efficient ends
are the same slot (first).

## 4. What changes and why it matters

**a. Matron goes from a hand-order slot to LAST.** At depth > 0 her ETB is a deferred acquisition
(AIEngine.cpp:4409-4440). The plan's casts all run first, then a searched re-solve casts the fetch with
what is left. The fetch is chosen by index from a ranking computed at the true mid-plan state
(`Plan::tutor_choice`, TurnSolver.h:1028-1040). That ranking sets three things:
- the W=9 window;
- d0's pick;
- the playout default (index -1 = top pick).

Concrete case. T4, four Mountains, plan {Goblin Lackey, Goblin Matron}, with Matron earlier in hand:
1. Matron resolves first, while the Lackey is still in hand, so `lackey_persist` is false
   (DecisionProviders.cpp:12868).
2. An arriving Muxus therefore reads t = 1 + (6 - 5) = 2 turns (decay 0.38) instead of t = 1
   (0.85; :13785-13795).
3. The top pick, and with it d0 and every playout, slides off the Lackey-cheat line.

With Matron last, the ranking sees exactly the board and mana the re-solve gets.

*Trade-off for the USER (Q1):* at **d0** the re-solve is inline at the cast (`acq_deferred` is false at
depth 0). There, Matron-first lets the d0 runner re-plan the rest of the turn around the fetch, and moving
her last gives that up. The precedent is that deferring at d0 cost Hinata about 20 d0 games
(AIEngine.cpp:4422-4426).

Under `MTG_BP_NEW_ONLY` (default ON) an old-card-only continuation is dropped as the sibling plan's
duplicate. So the rank also decides which of "X, then Matron" and "Matron, then X" exists at all. Today it
is whichever hand order gives; proposed it is "X, then Matron".

**b. Muxus before Matron.** Muxus's reveal should run while Matron's target is still in the library, and
Matron should rank against the Muxus board (for example, a revealed lord makes Krenko the fetch). The
effect is tiny: both cost 9 together (7 with Warchief).

**c. Bodies by cost.** Three Tree City's "{2},{T}: add R equal to creatures you control" counts at
activation, so every body cast first adds one R. Today this buys spare executor mana more often than a
line, because the flat gate credits TTC at the pre-plan count. The sequential walk that would credit it
runs in CastOrderRank order, but only for subsets holding Warchief or a Skirk float (TurnSolver.cpp:47972).
Generic gap in §6.

**d. Death watchers before a duplicate legend.** The USER keeps a 2nd Muxus castable for its reveal ("a
Muxus in play does not make the hand copy dead"). With Hordemaster or Pashalik cast first, the legend-rule
death impulses a card and pings 1. With the 2nd Muxus first, neither trigger fires. This is rare (8+ mana).

**e. Report hygiene (no play change).** Label rank 8 and mark tie groups as "hand order".

## 5. Questions for the USER

1. **Matron: last or first?** Default **last**. It makes the searched tutor ranking honest; first is
   better only for the d0 runner's inline re-solve. A scalar rank cannot be depth-conditional.
2. **Rest of the creatures by cost (bodies before big casts), or leave hand order?** Default **by cost**.
   Apart from §4c/§4d the block is order-free, so hand order costs little, but nobody chose it.
3. **Is "a Piledriver should be played earlier than a lord" about turns?** Default **yes**. The turn-level
   choice is the search's subset pick plus the discard tie-break already shipped. In-phase, I rank
   Piledriver ahead of every lord, which is neutral within a main.
4. **Phase filter for Goblins: keep OFF (both mains search every cast)?** Default **off**. Turning it on
   first needs the Muxus fix in §6.5: with an attack-ready Muxus, a summoning-sick Goblin cast pre-combat
   still pumps it +1/+1, so the m2 labels for Piledriver, Chainwhirler and Twinshot would delete damage.

## 6. Generic-order findings

1. **Report: missing tier labels.** `tier_name` (main.cpp:7885-7906) lacks `case 8` (subtype cost reducer:
   Warchief, Ragemonger) and `case 9` (dork growth / watcher / Vial want-subtype).
2. **Report: misleading tie order.** It sorts ties by mv then name (main.cpp:7877-7881), but play keeps
   hand order. It should print tie groups as "(tie: hand order)". It also prints an m1/m2 column for
   providers whose filter is off, and should say "phase filter OFF: column advisory" (from
   `ClassifiesMainPhases`).
3. **Tutors have no slot.** A deferred-acquisition tutor (`tutor_to_hand`) has no generic slot and ties
   with its type tier. At depth > 0, LAST within the plan is the slot where its mid-plan ranking equals the
   re-solve's state. The precedent is `MTG_HINATA_GAMBLE_LATE` (play-inert; "moving a tutor in the order is
   free", TurnSolver.cpp:4273-4275). Caveat: on a deck with condemnation ON, a late tutor condemns more.
4. **Three Tree City is not in the growth tier.** The growth tier (rank 9, `FeedsLiveScaledDork`) covers
   scaled DORKS only (`IsScaledManaDork` requires a creature), not a scaled LAND. TTC's in-plan growth is
   also neither credited by the flat gate nor an exec-feas admission, so "bodies, then the TTC-paid cast" is
   unfindable unless Warchief or a Skirk float rides along. Related: ManaPayment.cpp:4264-4268 says of
   scaled lands "No suite deck plays one, so this is inert today". That is stale, because Goblins runs one.
5. **Muxus is not seen as a scaling attacker.** `BoardHasScalingAttacker` (TurnSolver.cpp:20336-20351)
   misses `attack_self_pump_per_other_subtype` (Muxus). It is inert while Goblins' filter is off, but it
   blocks Q4.

## 7. Implementation notes

- **Where the code goes.** `GoblinsProvider::CastOrderRank` + `CastOrderTierName`, behind a new
  `MTG_GOBLIN_CAST_ORDER` heurarm flag (default per the USER's answer). Rank by params, not names:

| slot | test |
|---|---|
| reducer | `!reduces_spell_subtype.empty()` → 8 (unchanged) |
| tutor | `tutor_to_hand` → 40 (last) |
| noncreature | → 30 |
| creature | `10 + 2*ManaValue` (Lackey/Skirk 12 ... Muxus 22) |
| within a cost | −1 if `etb_self_creates_tokens > 0` (Marshal); +1 for a lord (`tmpl == LordEffect` with `power_bonus > 0`: Hordemaster, Chieftain, King). Piledriver needs no key of its own: it sits at the base slot, ahead of the +1 lord. |

- **Inert hooks.** Vial puts already rank as their card (AIEngine.cpp:6162-6165). Condemnation is off for
  Goblins (`CondemnsConsideredAtBreakpoint` not overridden), so moving Matron last condemns nothing.
- **Engine limits a rank cannot fix:**
  - **(i) Depth.** A rank cannot be depth-conditional (Q1).
  - **(ii) Skirk's sacs come before every cast.** They are pre-cast floats (AIEngine.cpp:6169-6176), so a
    Hordemaster or Pashalik cast in the same plan never sees them. Probe seed 2059 T2 main 2: six Goblins
    were sacrificed to Skirk to pay for "Chainwhirler|Hordemaster|Lackey", and the cast Hordemaster
    triggered on none of them.
  - **(iii) A same-plan Skirk funds nothing.** Sac actions are collected from the battlefield only, so a
    Skirk cast this plan pays for nothing until the site-9 post-entry continuation. Its rank is inert,
    which is why it gets no mana-source tier here.
  - **(iv) TTC growth and (v) the phase filter's Muxus gap:** see §6.4 and §6.5.
- **Probe files.** Probe artifacts are under build/scratch/castorder/probe_Goblins/. They were run with
  `--cards-json`; one stray dump, build/scratch/castorder/probe_Goblins_cards.txt, sits next to it.
