# The land-search cluster (Landfall deck, 2026-10-15)

Self-contained record of how "search your library for land(s)" is modelled, what is searched, and
what is knowingly NOT yet expressible. Code: `src/core/LandSearch.h` (pick generator + resolution +
the activation / attack-trigger / in-combat resolvers), the pins beside `ShuffleAfterSearch` in
`src/core/SpellEffects.h`, the axis fan at the end of `AppendSubdecisionAxes` (TurnSolver.cpp).

## Cards and fire sites

| card | spec | fire site |
|---|---|---|
| Rampant Growth | 1 basic, tapped | spell resolution (EffectHandler / apply_one) |
| Search for Tomorrow | 1 basic, untapped; Suspend 2--{G} | resolution; off suspend: `CastOffSuspend` non-permanent branch |
| Cultivate | 2 basic, 1 to hand, tapped | resolution |
| Evolving Wilds / Terramorphic Expanse | 1 basic, tapped | `PermAbilityMode::SacLandSearch` ({T}, sacrifice) |
| Myriad Landscape | 2 basic sharing a land type, tapped | `SacLandSearch` ({2}, {T}, sacrifice) |
| Khalni Heart Expedition | 2 basic, tapped | `PermAbilityMode::QuestSacLandSearch` (3 quest counters + sacrifice) |
| Primeval Titan | 2 ANY lands, tapped, optional | `FireOwnEtbTriggers` + `FireAttackLandSearch` (declare-attackers) |

Radiant Fountain and Haunted Fengraf are the two non-basic utility lands Titan can fetch; Fengraf's
random graveyard return is `gy_return_at_random` (ApplyGraveyardReturnAbility).

## What is searched (no greedy in the window)

* `LandSearchPicks` enumerates every legal COMPLETE pick at resolution, folded only by two sound
  identities: copies / behavioural twins (`behaviour_identity`: Evolving Wilds == Terramorphic
  Expanse), and "up to N" -> the MAXIMAL picks (one card removed from an otherwise identical CRN
  future; the human may stop early). The provider only ORDERS it (`LandSearchPickOrder`). No width.
* `Plan::land_search_pins` -- one entry per in-plan land-search ordinal (spells, Titan ETB incl. a
  Titan a Genesis Wave will put, activations). Full product across ordinals up to 64 variants,
  additive (one-hot per ordinal) past it.
* `Plan::attack_land_search_choice` -- Titan's attack trigger, packed per attacking Titan; the
  in-combat Khalni cracks continue the same packed stream.
* `Plan::upkeep_land_search_choice` -- Search for Tomorrow off suspend, packed per arrival.
* `Plan::combat_crack_choice` -- the in-combat Khalni crack (bitmask over Khalni-type permanents).
* `Plan::land_search_act_slot` -- WHERE in the cast sequence the land-search activations run, in
  `Plan::land_slot`'s encoding: -1 the trailing pass (default), 0 before every cast, k after the k-th
  landfall-watcher cast (see gap 1).
* Human play: sequential `tutor_etb` prompts per land (DECISIONS.md), the `landsearch=` verb for the
  activations, the `dig` prompt for the in-combat crack.

## Collapses that are PROVISIONAL (need user sign-off)

1. "Up to N" / "you may search" (Titan) collapsed to the maximal picks / to searching, for the
   SEARCH; the human keeps both. Argument: the extra land is one card removed from an otherwise
   identical future (Library::ShuffleByKey keys each card independently). Corner: Cultivate's hand
   basic can force a cleanup discard -- our own choice, equivalent to not fetching it.
2. Search for Tomorrow's "you may cast it" off suspend collapsed to cast, for the search AND the
   human (declining gains nothing; only the shuffle differs).
3. Instant-speed activation timing (Evolving Wilds, Terramorphic, Myriad, Fengraf) collapsed to the
   trailing pass of our main phase -- inert vs the passive opponent. Khalni's combat window is
   modelled (below); its opponent-turn window is not (equivalent to a main-1 crack next turn except
   that the lands would untap -- they enter tapped either way, so a main-2 / end-step crack gives the
   same next-turn board).
4. Haunted Fengraf's random card is a seeded, deterministic draw the clairvoyant search reads (the
   Gamble model), keyed on (game seed, turn, eligible count) in canonical order.

## Known gaps (DEFERRED -- each is a missing capability, not a heuristic)

1. *(Closed 2026-10-15, follow-up (b); re-expressed as a POSITION when the land drop became one.)*
   **Activation-vs-cast ORDER.** Board activations run in the TRAILING pass, after the plan's casts,
   so "crack Evolving Wilds / Myriad / Khalni BEFORE casting Avenger of Zendikar or Phylath" (more
   lands -> more Plants from the ETB count) was not one plan. `Plan::land_search_act_slot` carries the
   activations' position in exactly `Plan::land_slot`'s encoding (-1 trailing, the default; 0 before
   every cast, at the Lotus/Suspend point before the batch prepay and after a land-first drop; k
   right after the k-th `TurnSolver::PutsLandfallWatcher` cast, AFTER the land when both sit at k, so
   a Khalni the land just filled is crackable; an unreached k falls back to the trailing pass). Both
   worlds apply them through ONE body (`TurnSolver::ApplyLandSearchActs`) at the same points --
   ApplyPlanDirect's pre-cast pass / `slot_hook` / trailing pass and AIEngine::TakeTurn's twins --
   and a position plan's tied-rank watchers go first (`LandSlotWatchersFirst`) as for the land.
   Emitted for every plan that both activates a land search and casts a card whose ETB counts lands
   (`etb_tokens_per_land`): position 0, and 1..W-1 for its W watcher casts ("Baloths, crack, Avenger
   counting the crack's land"); W only when a land-counting cast is not itself a watcher (with every
   land-counter a watcher -- the whole current pool -- position W is the trailing pass up to where
   tapped lands and a shuffle fall among casts that read neither, an identity fold).
   LOCKSTEP NOTE (Courser of Kruphix): a crack's shuffle can leave a new playable land on top. The
   rollout's per-cast window (end of apply_one) runs BEFORE its slot hook, the executor's
   (`put_in_hand_armed`) AFTER `exec_slot_hook`, so the executor freezes the cast window's library-top
   verdict across a positioned crack (`rdb_top_verdict`); the shuffle then arms site 10 through the
   SECTION window in both worlds. Remaining boundary: no position twin inside a breakpoint
   continuation, and none for a plan without a land-counting ETB (an ordinary landfall watcher already
   prefers cast-then-crack, the default). Human play orders freely (`human_action_order`).
2. **Search for Tomorrow hard-cast mana.** The untapped basic a HARD-CAST Search for Tomorrow puts is
   not credited to the same plan's later casts by the subset mana math (the Crop Rotation precedent,
   `Deliberately NOT modelled` in TurnSolver), so "Search for Tomorrow, then a spell paid with the
   fetched land" in one main phase is not enumerated. Fix shape: a producer credit
   (`Action::rock_mana`-style, gated on a basic remaining in the library) plus the pending-producer
   hook in `SubsetPayableSequential` and the funding-order hoist in both apply worlds.
3. **A fetched Gruul Turf's bounce target** takes `BounceSearchCandidates`' front: the searched
   bounce axis (`Plan::bounce_choice`) fans only for a Karoo LAND DROP. Fix: fan it for a plan whose
   land search can put a Karoo (Titan's ETB/attack, Genesis Wave).
4. **Additive across axes.** The land-search axes follow the engine's one-axis-at-a-time convention:
   a land-search pick variant never also carries another axis's deviation (a scry, a bounce, a
   Phylath target rank), and an in-combat crack variant keeps the Titan's own attack pick at the
   front. Past 64 product combinations the in-plan pins are one-hot per ordinal.
5. **Cast ordinals are sized at the widest cast source** (the canonical cast bucketing is not
   reproduced at fan time); a narrower source clamps to its last pick at resolution (a duplicate the
   post-apply dedup skips).
6. **Packed-pin width cap.** A packed state pin holds 5 ordinals of 6 bits (64 picks each); a deck
   whose Titan sees more than 64 identity classes of land pairs would clamp. Landfall's is ~25.
7. **Lands with no mana ability.** Evolving Wilds / Terramorphic are `template: custom` so no mana
   site counts them; a few IsLand()-based heuristics (keep-for-main-2 count, end-of-main sweep, the
   anti-lifegain tutor source count) still read them as lands -- optimistic, not wrong.

## Not touched here

* Crop Rotation's land-to-battlefield tutor pin is inert in resolve mode (Creature Giving): a real
  in-window front pick, its own item with its own rebaseline.
