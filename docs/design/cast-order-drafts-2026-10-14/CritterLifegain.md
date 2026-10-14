# CritterLifegain -- cast-order draft (2026-10-14)

Deck: `decks/CritterLifegain/CritterLifegain.cod` (v2: 4 Soul Warden, 4 Soul's Attendant, 3 Auriok Champion,
1 Daxos, 4 Ajani's Pridemate, 4 Voice of the Blessed, 2 Archangel of Thune, 2 Heliod, 4 Ocelot Pride,
4 Serra Ascendant, 1 Ranger-Captain of Eos, 1 Ajani Strength of the Pride, 3 Unexpectedly Absent;
19 Plains + 4 Remote Farm). Provider `CritterLifegainProvider`.

## 1. Status -- (C) agent-proposed order, never reviewed by the USER

`CritterLifegainProvider::CastOrderRank` (DecisionProviders.cpp:19115-19125) behind `MTG_CRITTER_WATCHER_ORDER`
(default ON; `=0` falls back to Generic). It was mined by an agent in the 2026-09-08 Stage 5g pass and adopted under
the no-drawback rule (A/B +7/-0 at d0 on 400 games, +11/-0 at d3 on 300; analysis-CritterLifegain.md:89-98).
No USER review of the cast order is on record. The "watcher order is the USER's explicit order" line in
critterlifegain-discard-policy-proposal.md:126 is about the DISCARD keep ladder, not this order. The mining ran on the
**v1 list** (4 Thune, no Ocelot Pride, Orzhov Basilica). So where it puts Ocelot, Thune and Heliod is a gap in the
evidence, not something it measured.

**The report's "(unclassified)" ranks 8 and 9 are this provider's two tiers.** It has no `CastOrderTierName`, so the
report has no label for them:
* **8 = ENTER-WATCHER**: a creature with `any_creature_enters_lifegain > 0 || own_creature_enters_lifegain > 0`
  (Soul Warden, Soul's Attendant, Auriok Champion, Daxos).
* **9 = LIFEGAIN PAYOFF**: a creature with `lifegain_self_counters > 0` (Ajani's Pridemate, Voice of the Blessed).
* Everything else falls through to the Generic tiers.

## 2. What plays today

| pos | card(s) | main | rule |
|---|---|---|---|
| 0 | land drop | m1 | no cantrip -> first (searched at depth > 0) |
| 1 | Soul Warden, Soul's Attendant, Auriok Champion, Daxos -- **TIE** | m1 | rank 8, provider watcher tier |
| 2 | Ajani's Pridemate, Voice of the Blessed -- **TIE** | m1 | rank 9, provider payoff tier |
| 3 | Ocelot Pride, Serra Ascendant (m2*), **Heliod (m2*)**, Ranger-Captain, **Archangel of Thune** -- **TIE** | m1 | rank 10, Generic creature. The tie falls to plan order (`CastOrderLessRanked`, ManaPayment.cpp:2645: cheapest-first applies only among rituals). **Nobody chose this order, and for Thune and Heliod it matters.** |
| 4 | Ajani, Strength of the Pride; Unexpectedly Absent -- **TIE** | m1 | rank 20, Generic noncreature |

\*The m2 labels do nothing: the deck has `uses_second_main=no`, so `MainPhaseFilterActive` returns false
(TurnSolver.cpp:20388) and every cast happens in main 1.

## 3. Proposed order

Rows 2-4 are the payoff tier. **Conditional flip:** when the board already holds at least 2 more watchers than payoffs,
and no Archangel of Thune is on the board, the payoff tier (rows 2-4) moves ahead of row 1 (§4.3).

| pos | card(s) | main | range | why |
|---|---|---|---|---|
| 0 | land drop | m1 | - | The deck has no draw. Ranger-Captain fetches a creature (MV <= 1), never a land, so nothing this turn reveals changes the land choice. |
| 1 | Soul Warden, Soul's Attendant, Auriok Champion, Daxos (tie) | m1 | - | "Whenever another creature enters, you gain 1 life": a watcher already on the battlefield sees every later entry, and one cast afterwards sees none of them. The tie is provably harmless: each watcher sees the others symmetrically, and Daxos's own-side-only clause doesn't matter because every cast is ours. |
| 2 | Archangel of Thune | m1 | - | "+1/+1 counter on EACH creature you control" per gain event. Every creature that enters after it (its own entry included) is worth one counter per watcher on every body. Casting it before a Pridemate gains a x c counters (a watchers, c creatures). |
| 3 | Ajani's Pridemate, Voice of the Blessed (tie) | m1 | - | Each sees the gains from its own entry (cards.json), so they go after the watchers. The tie is harmless: both have the same self-counter trigger. Both also add white devotion for Heliod. |
| 4 | Heliod, Sun-Crowned | m1 | conditional | "isn't a creature while devotion < 5". It goes right after the casts that make it a creature on entry, so the watchers see it enter (CR 603.6d, per its bracket note). If it cannot become a creature this turn, it goes **first**, so it is on the battlefield for every gain. Static fallback: this slot. |
| 5 | Ocelot Pride, Serra Ascendant, Ranger-Captain (tie) | m1 | - | Bodies with no watcher or payoff text, so they go last among creatures, where every watcher and payoff above sees them enter. The tie is harmless: none triggers on or pays off another. Ocelot's token comes from an END-STEP trigger, and Ranger's fetched card is cast in the deferred re-solve after the whole plan. |
| 6 | Ajani, Strength of the Pride | m1 | - | +1 is a single gain event of creatures + walkers (the amount feeds Serra's 30-life clause), so it goes after every creature. The -2 token is a Pridemate that the watchers above see enter. |
| 7 | Unexpectedly Absent | m1 | - | The search only targets opponent spawn tokens, at X=0, which touches nothing above, so it goes last (a tie with Ajani would be harmless). It was never cast in 120 probe games. |

No ranges: the deck is mono-W, every source makes W and every cost is W/generic, so no order changes what is affordable.
No card in the list is ORDER-OPAQUE. None has draw, staging, cascade, retrace, impulse or solo-target params; I checked
every card's params against `OrderingOpaque`. So every set here is rank-sorted and the report's ranks are what plays.

## 4. What changes and why it matters

The turns below are one-turn scenarios run on the current binary (`build/scratch/castorder/probe_CritterLifegain/scen*/`).
"Unlimited" means `depth 5 budget 0`. Every probe passed an absolute `cards_json` / `--cards-json`. Three early
cwd-relative runs that went all-unwon were thrown away and are not used anywhere here.

1. **Thune moves out of the rank-10 tie.** Board: Soul Warden + Soul's Attendant, 6 Plains; hand listed
   [Ocelot Pride, Archangel of Thune]. Today the engine casts Ocelot then Thune at d3/b50 **and at unlimited**. That puts
   +2 on each of 4 bodies (8 counters), and the T4 attack deals 6. With the hand listed the other way, the same position
   casts Thune first: +4 on Warden, Attendant and Thune and +2 on Ocelot (14 counters), and the attack deals 10. Hand
   order alone makes the better line unfindable at every budget.
2. **Heliod moves out of the rank-10 tie and is placed by devotion.**
   * Board Warden + Attendant + Pridemate (devotion 3), 4 Plains, hand Heliod + Ocelot. Today hand order decides:
     Heliod first gives Pridemate +4. Ocelot first gives +6, because Heliod then enters at devotion 5 as a creature and
     both watchers fire.
   * Board Warden + Pridemate + Champion (devotion 4). Here Heliod first is right (+8 vs +6). It came first with
     breakpoint site 9 on, and second with `MTG_POST_ENTRY_BP=0`, so engine plumbing picks the slot, not a rule.
   * The opposite trap also exists. Probe game seed 2116 / gi 114 casts Voice then Heliod (+5); a static "Heliod ahead of
     the payoffs" rule would lose 2 there. That is why the static slot is after Pridemate/Voice.
3. **Payoff before watcher when watchers already dominate (the flip).** Board Warden + Attendant, 3 Plains, hand Pridemate + Soul Warden. Today the engine casts Warden then Pridemate at every budget: Pridemate ends 5/5 and we gain 5 life. Pridemate first: 6/6, 4 life.
   * The general rule for one watcher and one payoff: watcher-first wins when board watchers <= board payoffs. It is a
     tie at payoffs + 1, and payoff-first wins beyond that (already at +1 when 2 or more watchers are cast).
   * With Thune on the board, every event is worth a whole team, so watcher-first always wins. That is why Thune
     excludes the flip.
   * **It is rare:** 0 of 126 multi-cast mains in 120 probe games.
4. **m1 for everything.** Serra and Heliod are labelled m2 today. The label is harmless now, but wrong for this deck:
   every pre-combat entry is a gain event that puts counters on this turn's attackers before damage. Heliod's counter
   also targets "the highest-power creature that can still attack this turn", and its lifelink grant is pre-combat.
5. **Kept from the mined order:** watchers before Pridemate/Voice, the only part the 5g mining and its A/B measured.

**Expect a small measured effect.** On 120 d3/b10 probe games (seeds 2002+gi, gi 0-119), today's order already gets
the most counters available in all 121 tutor-free multi-cast mains. I checked this by counting each turn's entry
triggers from the card text; the final proposal also matches all 121. The deck wins T4-T5 off 1- and 2-drops, and
Thune was cast 7 times, never alongside another spell. The changes fix lines the order makes unfindable; this sample
rarely reached them.

## 5. Questions for the USER

1. Archangel of Thune right after the watchers, ahead of Pridemate/Voice and the 1-drops? **Default: yes.**
2. Heliod placed by devotion (right after the casts that make it a creature; first if it can't become one this turn)?
   The alternative is the static slot only (after Pridemate/Voice). **Default: devotion.**
3. The flip: payoffs ahead of new watchers when the board holds at least 2 more watchers than payoffs and no Thune.
   It trades 1 life for 1 or more counters per pair. **Default: yes.**
4. Pin every cast to main 1, so Serra and Heliod stop reading m2? **Default: yes.**
5. Ajani after every creature, even on a -2 turn (the token would catch one more set of watcher events per 1-drop cast
   after it)? **Default: yes.**

## 6. Generic-order findings

* Generic ranks every creature 10, so a team payoff (Thune) or a devotion god ties with plain bodies and plan order
  decides. The roles are identifiable from params:
  * Watchers: `any_creature_enters_lifegain`, `own_creature_enters_lifegain`.
  * Payoffs: `lifegain_self_counters`, `lifegain_each_own_creature_counters`, `lifegain_target_own_counter`.

  A generic tier order of watchers -> team payoff -> unit payoffs -> other creatures follows the same principle as the
  default-OFF `MTG_WATCHER_ORDER` (DecisionProviders.cpp:3180), which today covers only the dragon enter-pingers.
  Angels runs Thune and Creature Giving runs Soul Warden. SelesnyaLifegainProvider's comment already lists this as an
  unmeasured candidate.
* `creature_requires_devotion` (Heliod and the other Theros gods) has a conditional position. Such a card wants to enter
  after the permanents that make it a creature. A per-card rank cannot see that; only a set-level arrangement can.
* The baseline phase classifier marks a summoning-sick creature m2 even when its entry is a pre-combat gain event that
  pumps this turn's attackers. That is harmless only because this deck has a single main phase.
* The report prints "(unclassified)" for any provider rank without a `CastOrderTierName`.

## 7. Implementation notes

* **Card params for each slot (no card names):**
  * watcher = creature && (`any_creature_enters_lifegain` || `own_creature_enters_lifegain`), rank 6 (flip: 8)
  * team payoff = `lifegain_each_own_creature_counters`, rank 7 (flip: 4)
  * unit payoff = `lifegain_self_counters`, rank 8 (flip: 5)
  * devotion payoff = `lifegain_target_own_counter && creature_requires_devotion > 0`, rank 9 (flip: 6)
  * everything else -> `GenericProvider::CastOrderRank` (bodies 10, Ajani and UA 20)
* **Flip census:** count over `s.battlefield` where the controller is the active player. The Ajani -2 token resolves by
  name through `LookupCached`, so it counts. A non-creature Heliod counts as a payoff. The census reads the state the
  sort sees: the board at the start of the phase, or the post-breakpoint board for a continuation, which is the
  consistent choice.
* **Heliod placement:** override `ArrangeCastOrder`, following the precedent of Dragonstorm's Medallion slot
  (DecisionProviders.h:1226-1231). It runs in both worlds through `ApplyDeckCastArrangement` (ManaPayment.cpp:2710), so
  the two stay in lockstep.
  * Start from Heliod's static slot and walk later, adding up white pips, until board + preceding casts + its own pip
    >= `creature_requires_devotion`.
  * If it never gets there, move it to the front. Never move it earlier than the static slot.
* **Main phase and report:** `MainPhaseOverride` -> Main1 for every cast. `CastOrderTierName` for 6/7/8/9.
* **A/B:** a temporary `EnvOn` selector for the A/B, held out on critter + critter2hg. The bar is that every slower game
  recovers at `--depth 8 --budget-ms 0`.
* **Engine limits:**
  * The exact optimum is an interleave decided by running counts. A per-card rank cannot express that for same-name
    copies; the flip covers the practical case, and `ArrangeCastOrder` could do the exact merge.
  * Ranger-Captain's fetched card is cast in the deferred re-solve after every planned cast, so a fetched Soul Warden
    never sees the plan's own entries (probe seed 2051 / gi 49). The search can still leave the 1-drops for that
    continuation, so no order fix is needed.
