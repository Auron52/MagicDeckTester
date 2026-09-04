#pragma once
#include "../core/GameState.h"
#include "../cards/CardDatabase.h"
#include "PlanContext.h"   // PlanTraits (ComputePlanTraits below); header is Action-free on purpose
#include "SearchBudget.h"
#include <chrono>
#include <string>
#include <vector>

class TranspositionTable;  // per-decision SimulateToEnd memo (see TranspositionTable.h)

// A single atomic play the active player can make in a main phase. Unifies the
// formerly-separate action sources (hand cast, Aether Vial activation, Land's Edge
// discard, graveyard retrace) so enumeration, evaluation and execution each handle
// one collection rather than several parallel special cases.
//
// The valuation scalars (eval, direct_damage, ...) are populated by CollectActions
// at enumeration time and read by the subset evaluators; the apply/execute paths
// re-derive costs and effects from the card definition, so they ignore those fields.
struct Action
{
    enum class Kind
    {
        CastFromHand,        // cast a spell from hand
        CastFromGraveyard,   // Retrace: cast from graveyard, discarding a land as an additional cost
        ActivateVial,        // Aether Vial: put a creature from hand onto the battlefield
        DiscardToLandsEdge,  // discard `discard_lands` lands from hand to a Land's Edge for damage
        PlayLand,            // play `card_name` as the turn's land drop (recorded in a draw
                             // breakpoint so commit-the-line replay reproduces a land revealed
                             // and played post-draw, e.g. a Light Up the Stage land)
        DigDraw,             // activate a surplus-land card-draw ability to dig toward action
                             // (Treasure Hunt): cycle a land from hand (dig_sacrifice=false) or
                             // {cost},{T},Sacrifice a land in play (dig_sacrifice=true). Draws one
                             // card, then re-solves the phase exactly like a DrawUntilNonland draw
                             // engine, so a dug Treasure Hunt is cast the same turn.
        Suspend,             // Lotus Bloom "Suspend 3-{0}": pay {0} and move card_name from hand to
                             // Player::suspended_cards with three time counters (arrives turn+3). NOT
                             // a cast; adds no storm, no mana this turn. hand_index = the card's hand
                             // slot. cost = {0}. Emitted only when suspend_time_counters > 0.
        SacForMana,          // Lotus Bloom "{T}, Sacrifice: add 3 of one color": tap + SACRIFICE the
                             // battlefield source (sac_source_id = its card.m_number) and float
                             // ritual_float mana of chosen_float_color into state.floating_mana. cost
                             // = {0} (the cost is tap+sac). Emitted only when sac_for_mana_amount > 0;
                             // one variant per candidate colour (mutually exclusive per source).
        TapForTokens,        // Krenko, Mob Boss "{T}: Create X 1/1 Goblins, X = Goblins you control":
                             // tap the source (sac_source_id = its card.m_number); at apply, create
                             // (controlled matching-subtype permanents) tokens. cost = {0} (tap only).
                             // Emitted only when tap_creates_tokens_per_controlled_subtype is set.
        SacCreatureOutlet,   // Skirk Prospector / Siege-Gang / Pashalik: pay sac_creature_cost + Sacrifice
                             // one controlled creature with sac_creature_requires_subtype (sac_source_id),
                             // then apply the outlet payload (float mana / face damage / create tokens).
                             // The sacrificed creature fires OnCreatureDies. cost = sac_creature_cost.
        Channel,             // Twinshot Sniper "Channel -- {1}{R}, Discard this: 2 damage to any target":
                             // a from-HAND ability. Pay channel_cost + discard card_name (hand_index) ->
                             // channel_damage to the opponent face. cost = channel_cost.
        GarthActivate,       // Garth One-Eye: tap Garth (sac_source_id), choose the un-chosen name
                             // carried on tutor_target, conjure + CAST the copy as the ability
                             // resolves. cost = the copy's mana cost (Braingeyser: {U}{U} + the
                             // auto-maxed X as generic, X on chosen_x). One per Garth per plan.
        ActivateLoyalty,     // Planeswalker: activate loyalty ability #loyalty_ability of the walker
                             // (sac_source_id = its card.m_number). cost = {0} (the cost is the
                             // loyalty delta, paid inside ApplyLoyaltyAbility). One per walker per
                             // plan (exclusivity clause) and per turn (loyalty_activated_this_turn).
        Equip,               // Lightning Greaves: pay equip_cost_generic (as a.cost; {0} for Greaves)
                             // and attach the Equipment (sac_source_id = its card.m_number) to a
                             // controlled creature (sac_victim_id = the host's card.m_number).
                             // Sorcery-speed; variants of one Equipment are mutually exclusive per
                             // plan via the shared sac_source_id.
        GraveyardReturnAbility, // Haven of the Spirit Dragon: "{2}, {T}, Sacrifice this land: Return
                             // target Dragon creature card from your graveyard to your hand." Pay
                             // gy_return_cost (a.cost) + tap and SACRIFICE the source (sac_source_id
                             // = its card.m_number); the chosen target rides tutor_target, with one
                             // Action per distinct legal graveyard card NAME so the search picks
                             // among them (and the viewer shows them all) rather than taking the
                             // first match. One activation per source per plan (shared sac_source_id).
        GraveyardExileAbility, // Deathrite Shaman abilities 2/3: pay the colored cost ({B}/{G}) + tap
                             // the source (sac_source_id = its card.m_number), exile the first
                             // matching graveyard card, then drain 2 (gy_exile_mode 1) or gain 2
                             // (gy_exile_mode 2). Mutually exclusive with the source's mana tap via
                             // the shared {T} (the apply is a no-op if the source is already tapped).
        AttachAllEquipment,  // Balan, Wandering Knight "{1}{W}: Attach all Equipment you control to
                             // Balan": pay attach_all_equipment_cost (a.cost), then route EVERY
                             // controlled Equipment not already on Balan through the shared
                             // ApplyEquip (so Grafted Wargear's re-host sacrifice fires
                             // identically to a normal Equip; equip costs are BYPASSED -- this is
                             // an attach, Colossus Hammer's {8} is irrelevant). sac_source_id =
                             // Balan's card.m_number. No tap, usable while summoning-sick,
                             // instant-speed collapsed to the main phase (approved). One per Balan
                             // per plan; gated on >= 1 equipment not already attached to him.
        PutFromHandAbility,  // Stoneforge Mystic "{1}{W}, {T}: put an Equipment card from your hand
                             // onto the battlefield": pay tap_put_from_hand_cost (a.cost) + tap
                             // the source (sac_source_id, CanTapNow -- summoning-sick gated). The
                             // named hand card (card_name = the put card, tutor_target unused)
                             // enters UNATTACHED through the shared enter cascade (Puresteel's
                             // draw fires). One variant per distinct matching hand card name,
                             // mutually exclusive per source via sac_source_id.
        JitteModeAbility,    // Umezawa's Jitte's non-combat modes (user-directed 2026-08-13):
                             // remove one charge counter from the Jitte (sac_source_id) for
                             // gy_exile_mode 1 = "target creature gets -1/-1 until end of turn"
                             // (sac_victim_id = the target creature's card.m_number; kills a
                             // toughness-1 spawn via the SBA) or mode 2 = "you gain 2 life".
                             // cost = {0} (the cost is the counter). The +2/+2 pump mode is NOT
                             // this action -- it is spent inside combat (JitteDamageMath).
        ActivateRevealTop,   // Call of the Wild "{2}{G}{G}: Reveal the top card of your library. If
                             // it's a creature card, put it onto the battlefield. Otherwise, put it
                             // into your graveyard." sac_source_id = the enchantment; chosen_x = the
                             // searched activation COUNT K (cost pre-scaled to K x the printed
                             // activation cost). Applied in the trailing pass (after main casts,
                             // pre-combat), K sequential reveals per action (clairvoyant top).
        ActivatePump,        // A MAIN-PHASE activated pump whose cost the combat-time firebreathing
                             // converter cannot price. sac_source_id = the source; chosen_x = the
                             // activation COUNT K (cost pre-scaled to K x the unit cost, the Call
                             // of the Wild pattern); gy_exile_mode selects the shape:
                             //   1 = SELF pump with a DISCARD rider (Burning-Fist Minotaur
                             //       "{1}{R}, Discard a card: this gets +2/+0"). Not left to
                             //       ApplyFirebreathing because its greedy damage-per-mana ratio
                             //       cannot price a CARD -- and in this deck an emptied hand is
                             //       itself a payoff (Neheb's hand-size anthem), so the tradeoff
                             //       is exactly the kind of judgment the search must own.
                             //   2 = TEAM pump that also grants HASTE (Sethron, Hurloon General
                             //       "{2}{B/R}: Minotaurs you control get +1/+0 and gain menace
                             //       and haste"). Haste has to land BEFORE attackers are declared,
                             //       which a combat-time converter structurally cannot do.
                             // The mana-only half of a team pump stays in ApplyFirebreathing too
                             // (leftover combat mana); activating both is legal -- the ability is
                             // repeatable -- and no mana is double-spent.
        AnimateLand,         // Mutavault "{1}: This land becomes a 2/2 creature with all creature
                             // types until end of turn." sac_source_id = the land; cost =
                             // animate_cost. HUMAN PLAY ONLY: autonomously this is a greedy
                             // post-cast mana sink (AnimateLandsShared) that fires whenever there is
                             // spare mana, which is fine for the AI but leaves a human unable to ask
                             // for it OR to decline it -- the ability had no plan action at all, so
                             // no verb, no board affordance, nothing (user report #1). Applied in the
                             // trailing pass, pre-combat, so the animated land can attack.
        TapForTokenPay,      // Sliver Hive "{5}, {T}: Create a 1/1 colorless Sliver creature token.
                             // Activate only if you control a Sliver." sac_source_id = the land;
                             // cost = tap_token_cost. Same story and same fix as AnimateLand above:
                             // ActivateTapTokensShared spends spare mana on it greedily and a human
                             // could neither request nor refuse it.
        UntapCreature,       // Wirewood Lodge "{G}, {T}: Untap target Elf." sac_source_id = the
                             // land; cost = untap_creature_cost. Applied in the trailing pass:
                             // taps the source and untaps the highest-yield TAPPED matching
                             // creature (disclosed weakly-dominant auto-target); a no-op (cost
                             // unpaid) when the source is tapped or no matching creature is tapped.
        ActivateBlink,       // Eldrazi Displacer "{2}{C}:" / Emiel the Blessed "{3}:" -- exile
                             // another target creature and return it. sac_source_id = the outlet,
                             // sac_victim_id = the blinked creature (a real search axis: one
                             // variant per legal target), chosen_x = how many times.
                             //
                             // UNLIKE every other K-count activation (ActivateRevealTop,
                             // ActivatePump), `cost` is ONE activation, NOT K pre-scaled -- because
                             // this loop is SELF-FUNDING: blinking a Peregrine Drake untaps five
                             // lands, so iteration k+1 is paid for by iteration k. Pre-scaling
                             // would price a 20-iteration go-off at 20x its entry cost and prune
                             // the only line the deck wins with. The apply loop pays per iteration
                             // and BREAKS when one cannot be paid, so a K the board cannot sustain
                             // costs plan-ranking quality, never phantom mana or a phantom win.
        ActivatePermAbility, // "{cost}, {T}: <effect>" on a permanent you control (and the Clue
                             // token's tap-free "{2}, Sacrifice this: Draw a card").
                             // sac_source_id = the source; ability_mode selects which effect:
                             // Shivan Gorge's damage, Conservatory/Kitchen's investigate,
                             // Mariposa's draw, a Clue's sacrifice-to-draw.
        ActivatePod,         // Birthing Pod "{1}{G/P}, {T}, Sacrifice a creature: search for a
                             // creature with MV exactly 1 more, put onto the battlefield, shuffle.
                             // Only as a sorcery." sac_source_id = the Pod, sac_victim_id = the
                             // sacrificed creature (a real searched axis: one variant per
                             // equivalence-classed victim), tutor_target = the fetched name (one
                             // variant per legal MV+1 library name, plus the no-fetch sentinel
                             // "(no fetch)" -- saccing with nothing to get is legal and can be the
                             // play when the death itself is the payoff). cost = pod_activation_cost;
                             // the {T} bounds it to one activation per Pod per untap.
        GraveyardExileGrow,  // Scavenging Ooze "{G}: Exile target card from a graveyard. If it was
                             // a creature card, +1/+1 counter + gain 1 life." REPEATABLE (no {T});
                             // sac_source_id = the Ooze, tutor_target = the exiled graveyard card
                             // NAME (a searched choice -- exiling own creatures strips Reveillark
                             // targets, so it is not fungible; one action per distinct name).
                             // cost = gy_exile_grow_cost per activation.
    };

    // ActivatePermAbility sub-mode. Defined in the CORE layer (core/Permanent.h) because the shared
    // resolver ApplyPermAbility takes it and core must not depend on ai; aliased here so call sites
    // read as Action::AbilityMode. A named enum rather than a reused gy_exile_mode int so the plan
    // signature and the family key read as what they are.
    using AbilityMode = PermAbilityMode;

    // The three name fields are InternedName, not std::string (2026-08-12): an Action is copied,
    // moved, sorted and destroyed ~10 per plan x 113M plans in a 60-game Mirrorwing label batch,
    // and any name over the 15-char SSO limit ("Mirrorwing Dragon", "Goblin Instigator") made
    // EVERY such copy heap-allocate -- Action/Plan churn was ~1/3 of a heavy phase-A game's gdb
    // samples. InternedName is one pointer: trivially copyable, no alloc, no dtor work; every
    // read site sees the same text via the implicit const std::string& conversion (comparisons /
    // streaming / .empty()/.c_str() covered in NameRegistry.h). Byte-identical: values unchanged,
    // and no site orders by the POINTER (the one lexicographic sort compares .str()).
    Kind        kind           = Kind::CastFromHand;
    InternedName card_name;            // source card (creature name for ActivateVial)
    int         hand_index     = -1;   // hand index of the source/creature at enumeration time
    ManaCost    cost;                  // effective mana cost (enumeration feasibility only)
    bool        sacrifice_land = false;// additional cost: sacrifice a land (e.g. Shard Volley)
    int         discard_lands  = 0;    // Retrace = 1; for DiscardToLandsEdge = lands to discard
    int         vial_bf_index  = -1;   // ActivateVial: battlefield index of the tapped Vial
    bool        dig_sacrifice  = false;// DigDraw: true = sacrifice a land in play (sac-draw, e.g.
                                       // Fiery Islet); false = cycle a land from hand (e.g. Lonely
                                       // Sandbar). card_name = the source land; cost = its
                                       // cycling_cost / sacrifice_draw_cost.
    bool        alt_cost       = false;// CastFromHand via an alternative cost (Invigorate /
                                       // Skyshroud Cutter / Reverent Silence): pay no mana and
                                       // instead make the opponent gain alt_lifegain life (-> that
                                       // much damage with Tainted Remedy). cost is empty.
    int         alt_lifegain   = 0;    // opponent lifegain paid as the alt cost (see alt_cost)
    InternedName tutor_target;          // CastFromHand of a tutor: the specific library card to
                                       // fetch. When the heuristic is UNSURE, CollectActions emits
                                       // one variant per candidate (same hand_index -> mutually
                                       // exclusive) so the search picks; one variant when it is
                                       // sure. Empty for non-tutors (PerformTutor falls back to
                                       // the heuristic's top pick).
    AbilityMode ability_mode   = AbilityMode::None;  // ActivatePermAbility sub-mode; None elsewhere
    int         ritual_float   = 0;    // Hinata combo: gross floating mana this cast adds when it
                                       // resolves (Reality Spasm refloat / Irencrag burst), stamped
                                       // at enumeration (CollectActions, where the def is in hand)
                                       // so Solve/EnumeratePlans credit it WITHOUT a per-node card
                                       // lookup. 0 for every non-ritual action (all other decks).
    ManaPool    rock_mana;             // Same-turn mana-rock ramp: the mana a non-creature mana rock
                                       // (Sol Ring -> {C}{C}) taps for once cast, stamped at
                                       // enumeration so Solve/EnumeratePlans credit it (by real
                                       // colour) toward the rest of the subset. Total()==0 for every
                                       // non-rock action. See RockRampEnumEnabled.
    int         chosen_x       = 0;    // CastFromHand of an {X} spell: the X value chosen at
                                       // enumeration. The provider (XCandidates) narrows the X
                                       // range; CollectActions emits one variant per candidate
                                       // (sharing hand_index -> mutually exclusive) so the search
                                       // picks. cost already includes the X generic; chosen_x is
                                       // carried so the cast (rollout AND executor) scales the
                                       // effect (X damage) identically. 0 = not an X spell.
    int         splice_count   = 0;    // Desperate Ritual "Splice onto Arcane {1}{R}": the SEARCHED
                                       // number of OTHER splice_onto_arcane copies revealed + spliced
                                       // onto THIS base cast (0..#other copies in hand). CollectActions
                                       // emits one variant per k (sharing hand_index -> mutually
                                       // exclusive) so the search picks. cost AND float are scaled by
                                       // (k+1) at enumeration (a.cost/a.ritual_float) and re-scaled
                                       // IDENTICALLY at apply (rollout apply_one + executor
                                       // CastSpellFromHand/EffectHandler) off this same k -> lockstep.
                                       // The spliced copies STAY IN HAND (never removed) -> reusable.
                                       // 0 for every non-splice action (all other decks). A per-plan
                                       // legality guard (SubsetHasIllegalSplice) rejects the physically
                                       // impossible over-splice combinations.
    InternedName chosen_float_color;    // The REUSABLE "N mana of one CHOSEN colour" dimension
                                       // (Lotus Bloom's SacForMana; Apex of Power's "add ten of one
                                       // colour" will reuse it). CollectActions emits one plan VARIANT
                                       // per candidate colour ("W"/"U"/"B"/"R"/"G"); the chosen colour
                                       // is stamped here and read at resolution by AddChosenColorFloat
                                       // (which routes to state.floating_mana.<colour>). Empty = no
                                       // chosen-colour float (every non-Lotus/Apex action).
    int         sac_source_id  = 0;    // SacForMana: the sacrificed battlefield source's card.m_number
                                       // (a stable per-instance id). Used to keep the colour variants
                                       // of ONE source mutually exclusive in the subset enumeration
                                       // (you can sac a given Lotus only once). 0 for every other kind.
                                       // TapForTokens / SacCreatureOutlet: the OUTLET permanent's id
                                       // (Krenko / Skirk / Siege-Gang / Pashalik).
    int         sac_victim_id  = 0;    // SacCreatureOutlet: the sacrificed creature's card.m_number
                                       // (the chosen Goblin fed to the outlet). 0 for every other kind.
    int         sac_count      = 1;    // SacCreatureOutlet: how many creatures this activation sacs.
                                       // 1 (default) = the single-victim action (uses sac_victim_id).
                                       // >1 = the multi-sac BURST (Siege-Gang saccing the swarm for
                                       // sac_count*damage lethal): the apply loops the canonical victim
                                       // pick sac_count times; cost/direct_damage are pre-scaled by k.
    int         gy_exile_mode  = 0;    // GraveyardExileAbility: 1 = exile instant/sorcery -> opponent
                                       // loses N (Deathrite {B}); 2 = exile creature -> gain N ({G}).
    int         loyalty_ability = -1;  // ActivateLoyalty: index into the walker's loyalty_abilities.
    bool        free_cast      = false; // Maelstrom Archangel: this CastFromHand variant spends one
                                       // banked free cast (GameState::free_casts_available) instead
                                       // of paying mana (a.cost cleared; sac_source_id = the bank
                                       // SLOT id -1000-slot so two free casts never share a slot).
                                       // Emitted by CollectActions' free-variant post-pass only when
                                       // the counter is > 0 (the post-combat main).
    int         convoke_green = 0;     // Chord of Calling (CastFromHand + params.convoke): how many
    int         convoke_other = 0;     // GREEN / non-green creatures this cast taps for convoke.
                                       // The action's `cost` is emitted ALREADY REDUCED by their
                                       // contribution (green body -> a {G} pip or {1}; other -> {1}
                                       // only), so the payment machinery is untouched; the apply
                                       // pre-pass (ApplyConvokeTaps, both worlds) taps the actual
                                       // bodies -- deterministic shared order, free bodies first.
    int         soulfire_own_targets = 0;
                                       // Soulfire Eruption: searched COUNT of own creatures to add
                                       // as extra targets (0..#own creatures). CollectActions emits
                                       // one variant per value (sharing hand_index -> mutually
                                       // exclusive) so the search weighs deeper dig + cheaper
                                       // Hinata cost against the mana-value damage to those
                                       // creatures. SoulfireDig picks WHICH (expendable first,
                                       // Hinata last). 0 for every non-Soulfire action.
    int         crackle_targets  = -1;
                                       // Crackle with Power (scale_x Hinata discount): searched COUNT
                                       // of extra beneficial targets BEYOND the opponent face
                                       // (0..cap of opp creatures + own non-Hinata creatures + self if
                                       // 5X<life + Hinata last). Total targets = 1 + count; the Hinata
                                       // discount DERIVES from it (= min(X, 1+count)) instead of the old
                                       // auto-max. The cast (rollout AND executor) deals 5X to each
                                       // chosen creature/self and kills the lethal ones (SBA), so they
                                       // leave the target pool for later spells.
                                       // SENTINEL -1 = legacy (auto-max discount, NO faithful kill) --
                                       // the DEFAULT, so any action that isn't an explicit Crackle
                                       // count-variant behaves exactly as before. 0 would mean "declared
                                       // zero extras" -> discount min(X,1)=1, drastically overpricing
                                       // Crackle and hiding the combo from the search (the gi26 bug).
    int         max_casts_after  = -1;
                                       // Irencrag Feat: after this cast resolves the controller may
                                       // cast at most this many MORE spells this turn. -1 = no limit.
                                       // Solve::consider rejects a subset with more than this many
                                       // spells ordered after it (by CastOrderRank). Set only for the
                                       // restricting ritual; -1 for every other action.
    int         enchant_target   = 0;  // Aura cast: the card.m_number of the creature this Aura enters
                                       // attached to. CollectActions emits one CastFromHand variant per
                                       // legal creature target (sharing hand_index -> mutually exclusive),
                                       // so the search picks WHICH creature carries the aura (the clock
                                       // depends on it: summoning sickness + Kor's per-aura self-buff).
                                       // Threaded to resolution via apply_one (rollout) and cast_by_name ->
                                       // CastSpellFromHand -> StackEntry (executor). 0 = not an aura.
    std::string trick_hand_target; // Solo-target trick whose target is a SAME-PLAN HAND creature:
                                       // that creature's card NAME (enchant_target still carries its
                                       // m_number). Set at emission so the per-subset legality filter
                                       // (SubsetHasMissingTrickTarget) is a name compare instead of a
                                       // zone scan (it profiled at 3.7%). Empty = battlefield target
                                       // (always legal) or not a trick.
    bool        bestow           = false;
                                       // BESTOW (Gnarled Scarhide, CR 702.103): this CastFromHand is
                                       // the AURA mode -- pay CardParams::bestow_cost instead of the
                                       // printed cost, and resolve the "<name> (Bestowed)" aura face
                                       // the DB synthesizes rather than the creature. enchant_target
                                       // carries the host. CollectActions emits BOTH modes as
                                       // variants sharing hand_index (mutually exclusive), so the
                                       // SEARCH decides -- neither dominates (the creature mode is
                                       // cheaper and is itself a lord-buffed Minotaur; the aura mode
                                       // dodges summoning sickness and pumps a creature that can
                                       // attack NOW). false = the ordinary creature cast.
    int         ponder_keep      = -1;
                                       // Ponder-style cast_reorder: the SEARCHED keep-vs-shuffle
                                       // call. CollectActions emits TWO variants (1 = keep top N in
                                       // the provider's order, 0 = shuffle them away), sharing
                                       // hand_index -> mutually exclusive, so the search plays both
                                       // out and picks. -1 = not a reorder spell (legacy heuristic
                                       // path in ReorderTopOrShuffle).
    int         replicate_count  = -1;
                                       // REPLICATE (CR 702.56, Hatchery Sliver + every Sliver spell it
                                       // grants it to): how many EXTRA token copies this cast pays for.
                                       // The dimension exists because replicate TAPS REAL SOURCES, so
                                       // the count competes with the rest of the turn -- it is priced
                                       // into a.cost here (effective cast cost + k x the PRINTED cost,
                                       // CR 702.56a) so the whole-turn solve pays cast + replicate in
                                       // ONE BatchPrepayMainCasts bill instead of letting a greedy sink
                                       // spend the mana a later cast was holding (user report #2/#3:
                                       // the Sliver's coloured pip stranded, then Thrumming Hivepool
                                       // dropped). CollectActions emits one variant per k sharing
                                       // hand_index -> mutually exclusive, exactly like splice_count.
                                       // SENTINEL -1 = NOT DECLARED: resolution keeps the greedy-max
                                       // default (autonomous play, every rollout, and every deck
                                       // without replicate) -- so ground truth is untouched by
                                       // construction. >= 0 = pinned by the plan; resolution makes
                                       // exactly k copies and does NOT consult the human chooser
                                       // (the choice was already made where the person could see it).
                                       // Fanned only under HumanPlayActive() / MTG_UNPRUNE=replicate.

    // Valuation / win-check scalars (mirror the former per-function Candidate fields).
    int  eval                  = 0;
    int  direct_damage         = 0;
    bool is_noncreature        = true;
    int  card_mv               = 0;
    int  vial_attack_power     = 0;    // power this turn if a hasted Vial drop (wins_this_turn)
    int  haste_attack_power    = 0;    // power this turn if this is a HARD-CAST haste creature. Same
                                       // role as vial_attack_power for the other way a creature can
                                       // attack the turn it arrives; without it the this-turn attack
                                       // projection silently misses a cast Goblin Guide / Monastery
                                       // Swiftspear. Stamped once by CollectActions (both enumerators
                                       // share it), 0 for every non-creature / summoning-sick cast.
    bool haste_prowess         = false;// ... and it has prowess, so the plan's noncreature casts pump
                                       // it too (canonical cast order puts prowess creatures first).
    bool is_draw               = false;// DrawSpell / DrawX (Plan-B draw-early variants)
    bool has_spectacle         = false;// has a spectacle alternate cost (Plan-B)
    bool is_draw_until_nonland = false;// Treasure Hunt (Solve's LE/TH combo valuation)
    int  discard_land_damage   = 0;    // if this card IS a Land's Edge being cast (Solve)

    // Cached card definition for `card_name`, resolved ONCE by CollectActions (where the
    // name is assigned) so the hot subset evaluators (consider's max_casts_after loop, the
    // combo-line scan, CapGroupsBySituationalRank) read the pointer instead of re-hashing the
    // name string per node. Behaviour-identical to Lookup(card_name) -- same result, just no
    // repeated hashtable find (callgrind 2026-06-26: string-keyed Lookup ~3.5% of a Hinata d2
    // game). Transient enumeration scratch like the eval scalars; never enters a TT key/output.
    const CardDefinition* def  = nullptr;

    // Commit-the-line (MTG_FULL_DEPTH) faithful replay of DYNAMIC draw turns: the
    // exact casts the search's draw-breakpoint re-solve made right AFTER this card's
    // resolution revealed new cards (DrawSpell staging / DrawUntilNonland / the
    // cascade target it free-cast). Recorded by ApplyPlanDirect when building the
    // committed line, in execution order, nested (a recorded cast that is itself a
    // draw engine carries its own breakpoint_casts). AIEngine replays this script
    // verbatim instead of re-solving, so the realised turn matches the searched one
    // (the re-solve diverged on land-drop/mana state -> phantom wins). Empty for
    // non-draw cards and for decks/turns with no breakpoint. See
    // project-full-depth-search (TH oracle class).
    std::vector<Action> breakpoint_casts;
};

// Finds the optimal set of spells to cast in one main phase by exhaustive
// subset enumeration (O(2^|hand|), tractable for hand sizes up to ~15).
//
// Evaluation accounts for tempo: creatures are valued at power * expected
// remaining attacks, not just immediate damage. This prevents the solver
// from always preferring burn over board development.
//
// Sacrifice-land spells are always placed last in the execution order so
// that other spells have already tapped their lands before the sacrifice
// fires, minimising the real cost of the additional cost.
class TurnSolver
{
public:
    struct Plan
    {
        // The set of plays to execute this main phase. Execution order is canonical
        // (ActivateVial -> hand casts -> sacrifice-land casts -> graveyard casts ->
        // Land's Edge discards), applied by ApplyPlanDirect / AIEngine::TakeTurn.
        std::vector<Action> actions;
        int  value          = -1;   // -1 = nothing castable
        bool wins_this_turn = false;

        // Pump-waste tie-break flag (SubsetPumpWasted): this plan casts a target_own_creature
        // alt payload (Invigorate) whose pump cannot be used -- no ready attacker, or the
        // plan's own payment must tap the pump's target. Consequence is ORDERING ONLY:
        // MoveOrderPlans sorts flagged plans after their siblings (below wins_this_turn,
        // above value), so among horizon-TIED wins the first-verified-win commit prefers the
        // line the tuned auto-fire hold would pick; a strictly better flagged line still
        // wins its pass. Derived deterministically from (state, actions) at enumeration.
        bool pump_waste     = false;

        // Attack-forfeit tie-break flag (SubsetAttackForfeit): this plan's paid casts cannot
        // be covered without tapping the best own attacker, so committing it forfeits this
        // turn's attack. Same architecture and same ORDERING-ONLY consequence as pump_waste
        // (sorted after it in MoveOrderPlans): among horizon-tied plans, prefer the line that
        // keeps the attack live; a strictly better flagged line still wins its pass. Built for
        // the AL split's non-pump dump residual (antilife-main-phase-split.md, gi113 class:
        // "Remedy now, tapping Hierarch, combat passes" tied with "attack now, Remedy next
        // turn" and move-order committed the forfeit). MTG_ATK_FORFEIT_GATE, default OFF
        // pending measurement + user review.
        bool atk_forfeit    = false;

        // Searched dork attack/hold choice (MTG_DORK_ATK_SEARCH): -1 = not contested at this
        // node (natural heuristic combat, the default); 0 = contested, search chose the
        // natural HOLD; 1 = contested, search chose RELEASE (held dorks attack). Carried on
        // the committed line's m1 plan and pinned into the executor's DeclareAttackers
        // (AIEngine m_atk_release_pin -- the discard-pin pattern), so the realised combat is
        // the one the scored line simulated.
        int  atk_dork_release = -1;

        // Cast-ordering search (C): when true, ApplyPlanDirect executes the non-sacrifice
        // hand casts in `actions` VECTOR ORDER instead of the canonical enabler-first
        // bucketing -- so the search can explore orderings the canonical heuristic batches
        // wrong (e.g. enabler/destroy-all-payload interleaving: Tainted Remedy -> Reverent
        // Silence -> Tainted Remedy -> Reverent Silence, where casting both Remedies first
        // lets the first Reverent wipe both). Default false => canonical order (byte-
        // identical). Set only by the gated ordering enumeration (MTG_SEARCH_ORDER /
        // MTG_UNPRUNED), which dedups orderings by end-of-phase state.
        bool searched_order = false;

        // Land drop folded into the plan (searched alongside spells). When
        // land_decided is true the executor plays exactly land_to_play this turn
        // ("" == a deliberate defer / no land available); when false the land was
        // not searched (depth-0 static Solve plans) and the executor falls back to
        // the greedy land heuristic. Folding the land into the plan keeps the land
        // choice consistent between the real game and the lookahead rollout — the
        // rollout re-searches lands every turn exactly as the real game does.
        bool        land_decided = false;
        std::string land_to_play;

        // Fetchland search target (Pass 2 of the real-fetch model): when land_to_play is a
        // fetchland and FetchCandidates returned MORE THAN ONE legal target, the land
        // enumeration emits one Plan variant per candidate, each carrying the chosen target
        // here so the rollout (PlayLandByName -> PerformFetch) and the realised game
        // (TryPlaySpecificLand) fetch the SAME land. Empty == use the heuristic's top pick
        // (the single-candidate / Pass-1 case). Parallels Action::tutor_target for the
        // [[heuristic-then-search]] "heuristic narrows, search decides" land choice.
        std::string fetch_target;

        // Modal double-faced LAND (Pathway) face choice: when land_to_play is an MDFC land
        // (params.mdfc_back_name set), the land enumeration emits one Plan variant per face,
        // each carrying "" / "front" (play the front, e.g. Branchloft {G}) or "back" (play the
        // back, e.g. Boulderloft {W}) here. PlayLandByName / TryPlaySpecificLand swap the entering
        // permanent's identity to the chosen face so its colour locks. Empty == front (default /
        // non-MDFC). Parallels fetch_target: a plan-level land sub-decision, searched not narrowed.
        std::string land_face;

        // Land ETB scry/surveil disposition (Temple of Epiphany etb_scry, Thundering Falls
        // etb_surveil): which candidate of TopDispositionCandidates that look takes. -1 (default)
        // == the provider heuristic decides at resolution, byte-identical to no branch. k >= 0 ==
        // the land enumeration emitted one Plan variant per candidate and the search -- not
        // ScryKeepOnTop -- picks. The disposition resolves INLINE inside the land's ETB, so it
        // cannot be an Action; it is pinned for the apply instead (ScriptedTopChoice), exactly as
        // bp_choice pins a breakpoint continuation. Parallels fetch_target / land_face: a
        // plan-level land sub-decision, searched rather than narrowed.
        // See docs/design/searched-scry-disposition.md.
        int scry_choice = -1;

        // Mariposa Military Base's "you may have this land enter tapped; if you do, you get two rad
        // counters". -1 (default) == DECLINE, which is what the engine did unconditionally before
        // 2026-09-02 and keeps every other deck byte-identical. 0 / 1 == the land enumeration
        // emitted one Plan variant per mode and the SEARCH picks, on the user's instruction: "We
        // probably shouldn't always decline the rad counters, since it draws more cheaply with them
        // out." A hardcoded answer either way is a greedy heuristic standing where a searched
        // decision belongs.
        //
        // Both sides of the trade are modelled, which is the precondition for searching it at all:
        // the counters discount the land's own {5} draw by {1} each, and they carry the rad MILL
        // (ApplyRadMill) that costs life and eats the counters. Carried into the drop through
        // LandPlayOptions::rad_mode by BOTH worlds. Parallels fetch_target / land_face /
        // scry_choice: a plan-level land sub-decision, searched rather than narrowed.
        int rad_mode = -1;

        // ETB-dig pick (Acclaimed Contender's "look at the top 5, put a Knight into your hand"):
        // which candidate of the provider's ranked EtbDigCandidates the dig takes. -1 (default) ==
        // the provider's top pick, byte-identical to no branch. k >= 0 == the enumeration emitted one
        // Plan variant per candidate and the SEARCH picks. Like scry_choice, the dig resolves INLINE
        // inside the creature's ETB, so it cannot be an Action; it is pinned for the apply instead
        // (ScriptedEtbDig). The dug card goes to HAND and is not castable this turn, so the pick
        // cannot interact with the rest of this turn's subset -- the same property that makes the
        // tutor axis additive rather than an approximation of a cross product.
        int etbdig_choice = -1;

        // Searched TUTOR pick by INDEX (MTG_TUTOR_AXIS_RESOLVE=1): which candidate of the
        // provider's TutorCandidates ranking -- computed AT RESOLUTION, on the true mid-plan state
        // (land played, prefix casts applied, the source on the battlefield, its mana actually
        // spent) -- the fetch takes. -1 (default) == the provider's top pick at that same state,
        // byte-identical to no branch. This is the index-bound form of Action::tutor_target: the
        // name-bound axis ranks every plan's alternatives against one shared PRE-land turn-start
        // state (the located defect -- see the fan-out in EnumeratePlansWithLand), while an index
        // defers the ranking to the state the line actually produces, exactly as scry_choice /
        // etbdig_choice / ponder_choice already do. Pinned for the apply (ScriptedTutor); the
        // first tutor resolution consumes it. The fetched card goes to HAND (or top) and is not
        // castable this turn, so the pick cannot interact with the rest of this turn's subset --
        // the same property that makes the name axis additive.
        int tutor_choice = -1;

        // Searched SAC-LAND TARGET by index (MTG_SAC_AXIS): which candidate of the provider's
        // SacrificeLandCandidates ranking -- computed AT RESOLUTION, on the true mid-plan state
        // (a mid-plan fetch can add a land the turn-start board never held) -- each
        // sacrifice-a-land additional cost (Crop Rotation, Shard Volley) takes. One entry PER
        // SAC ORDINAL in this plan's canonical execution order, because the winning deviation
        // can be the SECOND sacrifice of a turn (cg30: CR#1's default is right, CR#2 must spare
        // the Orchard). Empty (default) == every sacrifice takes the provider's front,
        // byte-identical to no branch; entry k >= 0 pins that ordinal to
        // ranked[min(k, size-1)] (clamp = the tutor axis's duplicate-not-whiff rule); entry -1
        // is per-ordinal inert. Pinned for the apply (ScriptedSacLand list+cursor);
        // PerformSacrificeLandCost consumes one entry per call. Without this axis the sac
        // target was heuristic-only, so no depth or budget could represent the line where a
        // spawn land survives -- the cg30 unrecoverable-regression class.
        // See docs/design/searched-choice-audit.md.
        std::vector<int> sac_pins;

        // HOLD-vs-TAP of this turn's mana creatures. 0 (default) == the shipped heuristic (reserve
        // every mana creature for the whole turn, and sort them last in the tap backtracker);
        // 1 == spend them like any other source. Pinned for the apply (ScriptedTapMode) and NOT
        // consumed by its first reader -- every payment in the turn must see the same mode.
        //
        // This axis exists because the decision is irreducibly situational and every static
        // encoding of it lost: whether a body is worth more untapped than the mana it makes depends
        // on whether anything pumps it, whether it swings for damage, and how finite the
        // alternative source is. Gated on UnprunedGate::TapReserve -- default off, so the shipped
        // heuristic is unchanged and this costs nothing until the audit asks for it.
        int tapmode_choice = 0;

        // FRESH-MINT release (MTG_FRESH_SPEND_AXIS). 0 (default) == the §2a fresh-hold doctrine
        // (a magnetless same-turn Treasure is a bank, invisible to payment and pools); 1 == this
        // variant prices and pays with the fresh mint spendable. Whole-plan static pin
        // (ScriptedFreshMode), tapmode's twin. A freshmode variant is ADMISSIBLE only when its
        // simulated combat wins the turn it applies (FSLineWin discards the rest): spending
        // vs banking is a speculative next-turn comparison the search misprices (train: global
        // release 73 slower / 31 faster), but when the line kills NOW the bank is worth zero and
        // the comparison is exact (mw136's T5 Gold-Rush -> Libation X+1 exact lethal).
        int freshmode_choice = 0;

        // Goblin Lackey put (combat_damage_puts_subtype_from_hand): which candidate of the
        // provider's ranked CombatCheatCandidates the trigger puts onto the battlefield. -1
        // (default) == the provider's top pick, byte-identical to no branch. Unlike scry_choice and
        // etbdig_choice this is copied onto GameState::scripted_cheat_choice rather than pinned by a
        // scoped guard, because it is decided in the main phase and consumed in the COMBAT-DAMAGE
        // step -- a guard around the plan apply would be gone before the trigger fires.
        int lackey_choice = -1;

        // Ponder-style REORDER disposition: which candidate of ReorderCandidatesNarrow the reorder
        // takes. -1 == the provider heuristic, byte-identical to no branch. Narrow by construction
        // (shuffle + one variant per distinct TOP card) because Ponder draws immediately, so only
        // the top card is received now; the full m! permutation set is mostly waste. Pinned for the
        // apply via ScriptedReorder -- a SEPARATE pin from scry_choice, which the first look of any
        // kind consumes.
        int ponder_choice = -1;

        // CLEANUP DISCARD (hand over its size limit at end of turn): which candidate of the
        // provider's ranked CleanupDiscardCandidates this turn's first shed takes. -1 (default) ==
        // the provider's top pick, byte-identical to no branch.
        //
        // The EXECUTOR already searches this decision (AIEngine::ChooseDiscard rolls out every hand
        // card and keeps the win-optimal ones). The ROLLOUT did not -- SimulateEndAndStartNextTurn
        // sheds the heuristic's card, so every line the search scored assumed that shed. This axis
        // is what lets a plan be scored against a DIFFERENT one. Copied onto
        // GameState::scripted_discard_choice rather than pinned by a scoped guard, for the same
        // reason as lackey_choice: cleanup runs after ApplyPlanDirect returns.
        int discard_choice = -1;

        // AETHER VIAL upkeep charge (0 = hold, 1 = charge). -1 (default) == the provider heuristic
        // (WantVialCharge), byte-identical to no branch.
        //
        // The decision is two-sided and MULTI-TURN: the Vial deploys a creature whose mana value
        // EQUALS its counter count, so holding at k keeps a free MV-k deploy while charging trades it
        // for MV-(k+1) from next turn on. Reaching a 3- or 5-drop takes several consecutive charges.
        // That is exactly what the retired out-of-band probe (MTG_SEARCHED_VIAL) could not see: it
        // rolled BOTH answers out under a continuation that never charges again, so the arms differed
        // by at most one deploy and tied. As a plan axis the branch re-fans at every level of the
        // recursion, so a multi-charge climb is a reachable line. Copied onto
        // GameState::scripted_vial_charge rather than pinned by a scoped guard, for the same reason
        // as discard_choice -- and one turn further out, since the upkeep is next turn's.
        int vial_charge_choice = -1;

        // SEARCHED CYCLE/SAC-DRAW DIG (Horizon Canopy class; USER 2026-08-28: "we want that
        // decision to be searched to some degree... searched with heuristics is the way to go").
        // -1 (default) == the provider's dig heuristic decides, exactly as before -- and the
        // rollout HORIZON (future turns, depth-0, non-opted decks) always stays heuristic.
        // 0 == this plan's end-of-casts dig loop is SUPPRESSED; 1 == it runs gated only on
        // affordability (the provider's land-count judgement overridden). Emitted as post-dedup
        // plan variants for providers opting in via DigDecisionSearched() (Auras), so the rollout
        // scores the dig and no-dig lines and the SEARCH decides; the heuristic is the branch's
        // default, not a substitute for search. The executor needs no handling -- a committed dig
        // line rides breakpoint_actions verbatim, and a committed no-dig line records none.
        int dig_choice = -1;

        // Commit-the-line (MTG_FULL_DEPTH): the casts the search's draw-breakpoint
        // re-solve(s) made this phase, after a main `actions` draw engine revealed new
        // cards. Top-level (triggered by the main plan); each entry nests its own
        // breakpoint_casts. Populated by ApplyPlanDirect's out_breakpoint only when
        // building the committed line; AIEngine replays it verbatim (no re-solve) so
        // the realised turn matches the search. Empty for static turns. See
        // Action::breakpoint_casts and project-full-depth-search.
        std::vector<Action> breakpoint_actions;

        // SEARCHED MID-TURN BREAKPOINT (see docs/design/post-breakpoint-search.md). When a spell
        // that reveals NEW castables resolves mid-main (Treasure Hunt, Light Up the Stage /
        // Expressive Iteration, Apex of Power, Ponder/Preordain, a cycle/sac dig), ApplyPlanDirect
        // re-decides the rest of the turn. That continuation used to be picked GREEDILY
        // (TurnSolver::Solve + a static land ranker) with no search node at all, so no depth and no
        // budget could reach an alternative post-breakpoint line.
        //
        // -1 (default) == the greedy continuation, byte-identical to the old behaviour. k >= 0 ==
        // "at the FIRST breakpoint of this apply, play candidate k of EnumeratePlansWithLand
        // (land drop included) instead of the greedy pick". The land enumeration emits one Plan
        // variant per k, so the OUTER rollout scores each continuation and the search -- not a
        // heuristic -- decides. Parallels fetch_target / land_face: a plan-level sub-decision
        // resolved at apply time, searched rather than narrowed.
        int bp_choice = -1;

        // WHICH breakpoint of the apply bp_choice applies to (0 = the first, the original
        // behaviour). Nested breakpoints -- a second Treasure Hunt, an Apex cast off another
        // Apex's exile, a cantrip chain -- used to be unreachable by search entirely: only the
        // first was searched and every later one fell back to greedy. Measured on Dragonstorm,
        // nested breakpoints OUTNUMBER searched ones (183 vs 145 per 40 games), which is why the
        // Apex chain reference (claude_s1_gi0) stayed a turn behind the human.
        //
        // This is a second AXIS, not a cross product: the enumeration emits one variant per
        // (breakpoint index, candidate) pair, so cost is L*W, not W^L. Every individual
        // breakpoint's alternative continuation is therefore reachable by search; a line needing
        // TWO simultaneous non-greedy choices is not, which is the deliberate cost/coverage trade
        // (see MTG_BP_DEPTH). Ignored when bp_choice < 0.
        int bp_at = 0;

        // Did WAVE 0 (AppendBreakpointVariants) fan this BASE plan out? Set on the base plan, never
        // on a variant. MTG_BP_MAXBASE caps how many breakpoint-opening plans get variants at all,
        // and that cap -- like the width W -- is a RANK gate, so on its own it makes a whole plan's
        // continuations unreachable at any budget. The deferred wave phase reads this to start an
        // uncovered plan at rank 0 instead of rank W, which demotes MAXBASE to a cost prune: it
        // decides who waits, not who is reachable. Meaningless once the plan is applied.
        bool bp_wave0 = false;

        bool empty() const { return actions.empty(); }
    };

    // Returns the highest-value feasible plan for one main phase.
    // Uses a static evaluation function (no lookahead).
    static Plan Solve(const GameState& state, bool is_pre_combat);

    // The uncached greedy solve. Solve() is a thin wrapper that (under MTG_SOLVE_MEMO, search
    // interiors only) memoizes this per decision -- see namespace solvememo in TurnSolver.cpp.
    static Plan SolveUncached(const GameState& state, bool is_pre_combat);

    // MTG_CANTRIP_ORDER (canonical cantrip ordering, default off) plumbing: binds the cantrip
    // whose breakpoint continuation is being enumerated on this thread, so CollectActions can
    // suppress canonically-earlier cantrips (permutation dedup). Used by BOTH worlds -- the
    // rollout's deferred re-solve (ApplyPlanDirect) and the executor's live fallback
    // (AIEngine::resolve_draw_breakpoint) -- which is the lockstep. No-op when the lever is off
    // or `site` is not in the ordered class (clears instead). See TurnSolver.cpp for the design.
    //
    // `hand_before` is the PRE-DRAW hand (card numbers), captured where the site is armed. It is
    // what makes a NEW card first-class (USER 2026-08-17, docs/design/breakpoint-phase-
    // classification.md): a card the breakpoint DREW was never considered before it, so it is a
    // duplicate of nothing. Two consumers, one snapshot:
    //   * the ordering ban only fires for a cantrip that was already in hand -- "if the order was
    //     Ponder -> Preordain and you had no Ponder in hand you wouldn't be able to ignore the
    //     Ponder that Preordain drew" (the twin chain was never enumerable, so banning it deletes
    //     a real line rather than a permutation duplicate);
    //   * MTG_BP_CLASSIFY drops an already-in-hand cast that the pre-land pool already paid for.
    // Passing nullptr (or an unarmed site) leaves both inert -- byte-identical.
    class CantripOrderScope
    {
    public:
    // `land_drop_reserved` carries the apply's karoo_deferred: a Karoo is played AFTER the main
    // casts (so its mandatory bounce returns an already-tapped land), which leaves the land in hand
    // and lands_played_this_turn at 0 for the whole cast section. A breakpoint therefore sees a
    // state indistinguishable from "the plan passed on its drop" when the plan in fact CHOSE that
    // land -- and MTG_BP_CONDEMN_LAND would condemn every other held land on that false premise.
    // Bound on the SCOPE rather than around the whole apply so the executor's live fallback binds
    // the same fact at the same place: the lockstep-pair discipline this class already exists for.
        explicit CantripOrderScope(const CardDefinition* site,
                                   const std::vector<int>* hand_before = nullptr,
                                   const std::vector<std::uint64_t>* plan_casts = nullptr,
                                   bool classify_active = false,
                                   bool land_drop_reserved = false,
                                   int mana_sources_before = -1);
        ~CantripOrderScope();
        CantripOrderScope(const CantripOrderScope&) = delete;
        CantripOrderScope& operator=(const CantripOrderScope&) = delete;
    private:
        const CardDefinition*   m_saved;
        const std::vector<int>* m_saved_hand;
        const std::vector<std::uint64_t>* m_saved_casts;
        const CardDefinition*   m_saved_site;   // order-aware condemnation: the breakpoint's site
        bool                    m_saved_reserved;   // ...and whether the drop was RESERVED, not passed
        int                     m_saved_mana_before;   // ...and the mana-source count at the cast
    };

    // Card numbers in the active player's hand, for the breakpoint snapshot above. Cheap (one
    // small vector per armed breakpoint) and taken BEFORE the draw resolves.
    static std::vector<int> HandCardNumbers(const GameState& state);

    // Lands + mana rocks + mana dorks the active player controls. Snapshotted at a breakpoint so a
    // decline can be re-admitted once the mana base grows (BpTurnManaSettled).
    static int ManaSourceCount(const GameState& state);

    // Stamp the ORDER-CONDEMNATION snapshot (GameState::m1_hand) from the active player's
    // current hand. Called at the pre-combat main decision in BOTH worlds -- AIEngine::TakeTurn
    // (after the plan is chosen, before its land/casts execute) and ApplyPlanDirect's pre-combat
    // entry -- the lockstep pair; see the field's note. `m1_casts` is the chosen plan's action
    // list: a passed card only stamps when the m1 pool could have paid it IN ADDITION to those
    // casts (the split-turn test; held-out dig 2026-08-20). Both callers must pass the same
    // plan the m1 decision committed, at the same pre-land point, or the worlds stamp
    // different sets.
    // trace_stamp: MTG_CONDEMN_TRACE diagnostic (executor-side only -- the rollout worlds
    // would flood stderr); prints each stamped card with the pool/planned totals.
    static void StampM1Hand(GameState& state, const std::vector<Action>* m1_casts,
                            bool trace_stamp = false);

    // Do either of the snapshot's consumers (MTG_CANTRIP_ORDER / MTG_BP_CLASSIFY) need it? False
    // in every ship config, so the capture is skipped entirely on the cast hot path.
    static bool BreakpointHandSnapshotWanted();
    // State-aware twin: the per-deck provider route (CondemnsConsideredAtBreakpoint) needs
    // the board to answer. Callers on the cast hot path all have it.
    static bool BreakpointHandSnapshotWanted(const GameState& state);

    // BREAKPOINT SITE 6 -- the equipment-ETB draw (Puresteel Paladin). True when resolving `def`
    // puts an Equipment onto the battlefield while the active player controls a
    // draw_on_equipment_etb watcher, i.e. the enter cascade is about to DRAW and the rest of the
    // phase must therefore re-solve with the drawn card in hand.
    //
    // Unlike every other breakpoint class this one is STATE-keyed, not name-keyed: the draw belongs
    // to the WATCHER, not to the equipment, so no card param on the cast can classify it and
    // is_draw_engine/OrderingOpaque (both name-only) structurally cannot express it. That is why
    // the class was missing -- see docs/design/analysis-KittyEquipment.md. Both worlds call this
    // one predicate (rollout: ApplyPlanDirect's enter cascade; executor: AIEngine's
    // note_draw_engine) so the two cannot disagree about whether a breakpoint exists.
    //
    // No library-emptiness test on purpose: the executor evaluates it after the draw already
    // happened and the rollout before, so a size-1 library would make the two sides disagree. An
    // empty library just yields a continuation that re-solves with no new card -- identically on
    // both sides, and unreachable in a goldfish that ends far short of decking.
    static bool EquipmentDrawBreakpoint(const GameState& state, const CardDefinition& def);
    // The lever, one shared reader for both worlds (MTG_EQUIP_DRAW_BP, default OFF -> the whole
    // class is absent and every deck is byte-identical). Both sides MUST read this, not the env,
    // or a batch job's per-arm override would arm one world and not the other.
    static bool EquipmentDrawBreakpointEnabled();
    // MTG_EQUIP_DRAW_BP_INLINE -- the PARTITION shape (USER 2026-08-20/21): resolve the continuation
    // INLINE at the draw and TRUNCATE the base plan there, instead of deferring it to after every
    // main cast. "We should only be considering spells that have not been considered already at
    // every point, making the breakpoints fully distinct from each other." A sub-mode of the class
    // rather than a replacement so one pooled batch can measure it against the deferred shape.
    static bool EquipmentDrawBreakpointInline();
    // THE PARTITION SHAPE for the plain-cantrip class (MTG_BP_PARTITION_CANTRIP). Exposed because
    // the executor MUST take the same shape as the rollout -- one reader, no second flag, or the
    // committed line diverges from the searched one.
    static bool PartitionCantrip();
    // THE PLAIN-CANTRIP BREAKPOINT AS A REAL SEARCH NODE (MTG_BP_NODE; see BpNodeEnabled in the
    // .cpp). Exposed for the executor's truncation twin: a COMMITTED plan's casts past its first
    // plain cantrip were truncated by the search's partition, so the executor must drop them too
    // (the recorded continuation owns that section). Same one-reader rule as PartitionCantrip.
    static bool BpNodeSearch();
    // WHICH breakpoint classes the node hosts, as a site bitmask (MTG_BP_NODE_D56; see
    // BpNodeSites in the .cpp). Exposed for the same reason as BpNodeSearch: the executor's
    // truncation twin has to ask about the SAME sites the search partitioned at, or a committed
    // site-5/6 line executes a tail its scored continuation already owned.
    static int BpNodeHostedSites();
    // The same question WITHOUT the lever: does resolving `def` draw off an equipment-ETB watcher?
    // The draw happens whether or not the search is allowed to plan around it, so anything
    // REPORTING the draw (the game log's draw reporter) must ask this one, not the gated one --
    // otherwise an A/B arm would silently also change what the log says happened.
    static bool EquipmentEtbDrawFires(const GameState& state, const CardDefinition& def);

    // Enable per-pass per-candidate trace output for top-level T1 decisions.
    static void SetTraceSolve(bool enable);
    static bool GetTraceSolve();

    // Is the main-phase classification filter active for this state (the collapsed main)?
    // Exported for the shared attack predicate (DecisionProvider::AttackWith): with the filter
    // on, the turn's casts run AFTER combat, so the attack policy must know it is competing
    // with the deferred main for creature mana. False whenever the lever/provider is off,
    // in human play, and under MTG_UNPRUNE=mainphase -- same carve-outs as the filter itself.
    static bool CollapsedMainActive(const GameState& state);

    // Baseline main-phase classification for a hand cast, for the per-deck review report
    // (--cast-order-report). Uses the SAME ClassifyMainPhase the filter uses, so the reviewed
    // table cannot drift from play. Evaluated against the given (report) state, i.e. an empty
    // board: no haste access and no scaling attacker, which is the deck's baseline answer.
    // Returns 0 = Main1, 1 = Main2, 2 = Both (DecisionProvider::MainPhase, as an int so this
    // header needs only the forward declaration).
    static int ClassifyCastMainPhase(const GameState& state, const CardDefinition& def);

    // Is this run SEARCHED play (lookahead depth > 0)? Set once from the AIEngine constructor.
    // Gates cantrip-first ordering (docs/design/cantrip-first-collapse.md), whose justification --
    // an earlier draw improves the decisions that FOLLOW it -- presupposes a searcher able to act
    // on that information. At depth 0 there is none: the greedy post-draw re-solve just strands a
    // different set of spells, and it measures WORSE (+0.0830 across the three seed sets) while
    // searched play measures -0.2040. Applied via a run-level flag rather than per-call site so the
    // rollout leaf policy (Solve) and the searched plans (EnumeratePlans) order IDENTICALLY -- a
    // per-site gate would make the leaf estimate mis-order relative to the line it is scoring.
    static void SetSearchedPlay(bool enable);

    // Drop every per-THREAD plan memo at the start of a game. Called from AIEngine::HandleMulligan
    // beside the m_leaf_cache clear, and for the same stated reason: "so a reused batch worker's
    // AIEngine does not accumulate/cross-hit across games".
    //
    // These memos are scoped to ONE decision by g_decision_epoch, which is thread_local and only
    // ever increments -- so a previous game's entries can never be HIT. They are pure dead weight,
    // and that is exactly the problem: they occupy the shared Cap(), so how much residue a worker
    // carries decides WHEN cache.clear() fires, which decides which of the CURRENT game's entries
    // survive. That made a game's work-unit count depend on which games happened to share its
    // worker thread -- measured on KittyEquipment game 54: 2,127,508 run alone, 2,127,270 with 53
    // games ahead of it on the same thread, and varying run-to-run at --threads 24.
    //
    // PLAY was never affected (a memo hit returns what recomputation returns, and every digest was
    // identical across thread counts); the casualty was ai/GameWorkMeter.h's invariant that units
    // are "a deterministic function of (deck, seed, depth, arm, limit) and identical everywhere",
    // on which the abandon ceiling and cross-machine skip lists rest.
    static void ClearPerGameCaches();

    // Committed-depth telemetry (MTG_ROLLOUT_STATS; counters only, no behaviour). Called once per
    // top-level main-phase decision with the depth iterative deepening actually COMMITTED. See the
    // g_cdepth_hist comment in TurnSolver.cpp for why a units total cannot show this.
    static void RecordCommittedDepth(int committed_sub_depth);

    // --- External-controller hooks (Claude-play / human-play prototype) ---------
    // Expose the same candidate enumeration and plan application the solver uses, so
    // an external decision provider can be offered the legal main-phase plans and have
    // its chosen plan executed identically to a searched one. EnumerateMainPlans folds
    // the land choice for a pre-combat main (each Plan carries its land_to_play), just
    // like the search; ApplyPlan runs the canonical execution order (land, casts, Land's
    // Edge) via the same path the rollouts use. Not used by the normal AI path.
    static std::vector<Plan> EnumerateMainPlans(const GameState& state, bool is_pre_combat);
    static void              ApplyPlan(GameState& state, const Plan& plan, bool is_pre_combat);

    // The candidate list a SEARCHED breakpoint continuation indexes with Plan::bp_choice (see the
    // field). Identical to EnumerateMainPlans except it suppresses the bp_choice fan-out, so the
    // list ApplyPlanDirect scored and the list the executor replays are the same list -- the
    // executor's fallback breakpoint re-solve (AIEngine::resolve_draw_breakpoint) MUST use this,
    // never EnumerateMainPlans, or the realised play would drift from the searched one.
    static std::vector<Plan> EnumerateBreakpointPlans(const GameState& state, bool is_pre_combat);
    // #10 cast-order: canonical (executor clean-set) order of a plan's non-sac hand casts, for the
    // viewer to diff the human's queued order against (equal => don't emit --cast-order).
    static std::vector<std::string> CanonicalNonSacCastOrder(const GameState& state, const Plan& plan);

    // --- Whole-turn (batch) mana pre-payment ------------------------------------
    // Pays the COMBINED mana cost of this turn's main hand casts in a SINGLE complete-solver call,
    // then pre-loads state.floating_mana with that combined cost (coloured pips pinned to their
    // colours, the generic portion as `wild`). Each main cast then drains the pool instead of
    // tapping just-in-time, so scarce colours are allocated jointly and ramp-filters get fed --
    // fixing the per-cast greedy that strands a later same-turn cast. Sources not needed stay
    // untapped (mana-source reservation falls out for free). Returns true iff it prepaid; the
    // caller then simply runs its cast loop (the casts pay from floating). Returns false with
    // state UNTOUCHED when prepay does not apply (declined or the full batch is unaffordable), so
    // the caller falls back to per-cast greedy -- byte-identical to the pre-batch behaviour.
    // Called identically by the rollout (ApplyPlanDirect) and the executor (AIEngine::TakeTurn)
    // so the two stay in lockstep. Off-switch: MTG_NO_BATCH_PAY.
    static bool BatchPrepayMainCasts(GameState& state, const std::vector<Action>& acts);

    // PlanTraits builder (docs/design/mana-order-and-reserve-overhaul.md layer 3): distil what the
    // plan DOES (own-creature pump + its projected target, copy magnet, scaler food, phase, attack
    // relevance) for the payment layer's reserve/rank overrides. ONE function called by BOTH apply
    // paths (rollout ApplyPlanDirect, executor AIEngine::TakeTurn) and installed over the whole
    // payment via PlanTraitsScope -- the same lockstep-by-construction as BatchPrepayMainCasts.
    // Only run when PlanTraitsWanted() (some consumer lever on); otherwise the scope holds nullptr
    // and every consumer behaves exactly as before.
    static PlanTraits ComputePlanTraits(const GameState& state, const std::vector<Action>& acts);

    // SAC-COLOUR FOLD (MTG_SAC_COLOR_FOLD, default OFF -- see docs/design/
    // lump-mana-sources-as-payment-sources.md). With the fold ON, a SacForMana source emits ONE
    // colour-AGNOSTIC action (empty `chosen_float_color`) instead of one action per candidate colour,
    // and the colour is resolved HERE, at apply, from the plan's own remaining coloured demand.
    //
    // Why this is sound: the subset math credits a sac source's output as `ritual_float` WILD
    // regardless of which colour variant was selected, so the per-colour variants are indistinguishable
    // to the very test that decides them (for a Treasure, amount 1, `wild` is exactly correct). The
    // colour only ever became real at apply -- which is precisely where this resolves it, now informed
    // by what the plan actually needs rather than guessed by the enumerator across N branches.
    //
    // `self.chosen_float_color` non-empty -> returned unchanged, so every legacy (unfolded) action and
    // every recorded/replayed plan is BYTE-IDENTICAL. Called by BOTH the executor (AIEngine) and the
    // rollout (ApplyPlanDirect / continuation pre-casts) so the two stay in lockstep by construction --
    // the same discipline ApplySacForMana's own shared-helper note describes.
    static std::string SacFloatColorFor(const GameState& state, const std::vector<Action>& acts,
                                        const Action& self);

    // "We are resolving a BREAKPOINT CONTINUATION" marker, for MTG_CONDEMN_M1_BP.
    // g_bp_enum_depth is NOT the right signal: it only covers EnumerateBreakpointPlans, i.e. the
    // SEARCHED continuation (Plan::bp_choice >= 0). A deck whose breakpoints all resolve greedily
    // -- FiveColour is 1457/1457 greedy at the deferred_cantrip site -- falls back to a plain
    // TurnSolver::Solve and never increments it, so a gate on it never opens (measured: entries=0).
    // This scope wraps the WHOLE deferred re-solve (searched list, greedy fallback, and the
    // continuation's own application) in the rollout, and the twin region in the executor's
    // resolve_draw_breakpoint -- the same lockstep-pair discipline as CantripOrderScope.
    struct BpContinuationScope
    {
        BpContinuationScope();
        ~BpContinuationScope();
        BpContinuationScope(const BpContinuationScope&) = delete;
        BpContinuationScope& operator=(const BpContinuationScope&) = delete;
    };
    static bool InBpContinuation();

    // Fire every planned Equip that UNLOCKS MANA -- a haste-granting Equipment onto a mana dork
    // that cannot tap yet (CR 302.6: haste lifts the {T} restriction too) -- as soon as both of its
    // pieces are on the battlefield, rather than in the trailing equip pass that runs after the
    // main casts. That ordering is what makes EnumeratePlans' same-turn hasted-dork mana credit
    // realisable: the plan is offered BECAUSE the hasted dork pays for a later cast in it. Called
    // before the casts and again after each one, by the rollout (ApplyPlanDirect) and the executor
    // (AIEngine::TakeTurn), through this one function so the two stay in lockstep. Self-gating on
    // "the host is a still-locked mana source", so an equip onto a beater is untouched and still
    // fires in the trailing pass. Returns how many fired. Off-switch: MTG_NO_HASTE_DORK_CREDIT.
    static int ApplyManaUnlockEquips(GameState& state, const std::vector<Action>& acts);

    // Card numbers of battlefield sources a plan with a mana-unlock equip must hold untapped until
    // that equip fires -- the "reserve red" half of the same line. Empty for every other plan.
    // Fed to PlanSourceReserveScope by the rollout and the executor. See the definition for the
    // scarcity rule and why the batch pre-pay cannot cover this case.
    static std::vector<int> ManaUnlockColorReserve(const GameState& state,
                                                   const std::vector<Action>& acts);

    // COLOUR-CRITICAL reserve (MTG_COLOR_RESERVE, default off). The batch pre-pay solves the turn's
    // mana JOINTLY, but it declines on most interesting turns -- producers, {X} spells, per-target
    // discounts, flood engines -- and every decline routes the turn to a per-cast greedy that can
    // spend a flexible source an ordered-later cast needed, stranding it (the cast is then silently
    // dropped). Measured: hinata declines ~81% of its multi-cast turns.
    //
    // This holds back the sources the plan cannot afford to lose: a source is CRITICAL when removing
    // it makes the plan's combined coloured demand infeasible by the same Hall test the enumeration
    // gate uses. It rides the existing reserve-then-fallback retry, so an early cast pays around a
    // critical source while the cast that genuinely needs it still takes it on the fallback attempt
    // -- slack-only, and it can never make a payable cost unpayable. Empty when the lever is off,
    // when the plan has fewer than two mana casts, or when nothing is critical.
    static std::vector<int> ColorCriticalReserve(const GameState& state,
                                                 const std::vector<Action>& acts);

    // The union both apply paths install (unlock pieces + colour-critical). One function so the
    // executor and the rollout cannot drift on which sources are held.
    static std::vector<int> PlanReserveSources(const GameState& state,
                                               const std::vector<Action>& acts);

    // --- Human-play line reconciliation (tools/play GUI) ------------------------
    // A human assembles a free-form main-phase line by hand (play a land, cast some
    // spells) and commits it at the phase breakpoint. CheckLine reconciles that line
    // against what the model would actually do:
    //   - Accept             : the line matches one of the enumerated plans -> the
    //                          game can proceed with that plan index (recorded for the
    //                          stateless --choices replay).
    //   - LegalNotEnumerated : the line is rules-legal (an affordability simulation
    //                          that DOES model same-turn ramp from a freshly-cast mana
    //                          rock can execute it) but the search never enumerated it
    //                          -- an enumeration gap worth flagging, not a misplay.
    //   - Illegal            : the line cannot be executed (a cast is unaffordable, a
    //                          land can't be played); `failed_action`/`reason` say why.
    //   - Unsupported        : the line uses an action kind this v1 check can't yet
    //                          validate (X spells, tutors, alt-costs); reported, not
    //                          guessed at.
    // The affordability simulation is deliberately INDEPENDENT of the enumerator's
    // mana model: BuildPool (used by enumeration) does not credit mana produced by a
    // rock cast THIS turn toward a later same-turn cast, which is exactly why lines
    // like Mountain -> Sol Ring -> Ornithopter of Paradise are legal-but-not-enumerated.
    struct LineSpec
    {
        bool        pass = false;            // explicit pass / cast nothing, no land
        bool        has_land = false;        // play a land this phase
        std::string land;                    // the land card name (has_land)
        std::vector<std::string> casts;      // hand spells to cast, in clicked order
        int         lands_edge = 0;          // discard this many lands to Land's Edge (0 = none)
        std::vector<std::string> vial_deploys;  // creatures put onto the battlefield via Aether
                                                // Vial (MV == the Vial's charge counters), free
        std::vector<std::string> retrace_casts; // spells cast from the graveyard via Retrace
                                                // (pay cost + discard a land each)
        // Sac-outlet ACTIVATIONS of a permanent already on the battlefield: one entry per
        // activation, naming the outlet (Skirk Prospector / Siege-Gang Commander / Pashalik Mons).
        // Needed as its own verb because these are neither hand casts nor a pass: Skirk's
        // "Sacrifice a Goblin: Add {R}" was previously only ever an IMPLICIT mana source the
        // enumerator added when a cast needed it (LineCheck's `planSacs`), so a human could not ask
        // for one -- and a line consisting ONLY of sacs read as "cast nothing" at stage 0 (viewer
        // issue #4). WHICH creature dies is not encoded here: it is answered at resolution by the
        // `sacrifice` board-click decision, so the human sees the real board when choosing.
        // EMPTY => legacy matching (SacForMana stays implicit, SacCreatureOutlet stays matched via
        // `cast=<name>`), which is what keeps every saved reference validating unchanged.
        std::vector<std::string> sac_outlets;
        // KittyEquipment battlefield activations (each its own verb so the GUI/line can ask for
        // them explicitly; EMPTY => a plan containing that kind matches by its card name in the
        // ordinary cast multiset, the legacy Equip behaviour):
        std::vector<std::string> attach_all;    // "attachall=<Balan name>": AttachAllEquipment
        std::vector<std::string> sf_puts;       // "sfput=<equipment name>": PutFromHandAbility
        std::vector<int>         jitte_modes;   // "jittemode=<1|2>": JitteModeAbility activations
        // "equip=<equipment name>[#<source m_number>][@<host m_number>]": Equip, one entry per
        // activation. Needed as its own verb because an Equipment on the battlefield and a copy of it
        // in hand share a NAME: matched inside the ordinary `cast=` multiset, "equip the Bonesplitter
        // in play" and "cast the Bonesplitter in hand" encode identically, so the viewer could not
        // express the first without maybe getting the second.
        //
        // The HOST rides here too (2026-09-01). It used to be left to the `equip` sub-decision, whose
        // choice string is the host's NAME -- so with two Kor Duelists in play every host variant
        // carried the SAME sub, shared a signature, and CheckLine's dedup silently dropped all but the
        // first: the human could not equip the second Duelist at all (user-reported, KittyEquipment
        // seed 6). The viewer's drag already knows exactly which permanent was dropped on, so it says
        // so; `host` == 0 means "any host" (an unstamped legacy line), which still fans out as the
        // sub-decision. `source` is the same disambiguation for the EQUIPMENT itself -- two
        // Bonesplitters in play are not interchangeable when one is already attached elsewhere,
        // because equipping that one MOVES it. 0 == any.
        struct EquipSpec { std::string name; int source = 0; int host = 0; };
        std::vector<EquipSpec> equips;
        // "gyexile=<mode>": Deathrite Shaman's graveyard-exile activations, one entry per activation,
        // carrying the MODE (1 = exile an instant/sorcery, each opponent loses 2; 2 = exile a creature,
        // gain 2). Its own verb for the same reason `equip=` has one: the action names the SOURCE
        // permanent, so `cast=Deathrite Shaman` is ambiguous with hard-casting another copy from hand
        // -- and FiveColour runs four. WHICH graveyard card is exiled is fungible (disclosed 6a).
        std::vector<int>         gy_exiles;
        // "gyreturn=<returned card name>": Haven of the Spirit Dragon's "{2}, {T}, Sacrifice: return
        // target Dragon creature card from your graveyard to your hand", one entry per activation.
        // Its own verb for the same reason `equip=` has one -- the action names the LAND, which is
        // never cast -- and it carries the RETURNED CARD rather than the source, because that is the
        // real choice: one Haven with three distinct Dragons in the graveyard offers three plans, and
        // encoding only the source could not tell them apart.
        std::vector<std::string> gy_returns;
        // "channel=<card name>": Twinshot Sniper's from-HAND channel ability. Not a board activation
        // (its source is a card in hand) and not a cast either -- "{1}{R}, Discard this card" plays the
        // same card a different way, so `cast=<name>` cannot distinguish the two. EMPTY => legacy
        // matching (a Channel action matches by card name inside the ordinary cast multiset).
        std::vector<std::string> channels;
        // "suspend=<card name>": Lotus Bloom's own Suspend (CR 702.61) -- exile it from hand with
        // time counters instead of casting it. Its own verb for the SAME reason `channel=` has one:
        // suspend is a from-hand ALTERNATIVE to casting the same card, so `cast=<name>` cannot say
        // which of the two the human meant. Today that is unambiguous only by accident -- Lotus
        // Bloom has no mana cost and can never be hard-cast -- and the ambiguity becomes real the
        // moment a suspend card with a payable cost is added. EMPTY => legacy matching (a Suspend
        // action matches by card name inside the ordinary cast multiset), so no saved reference moves.
        std::vector<std::string> suspends;
        // "animate=<land name>" / "taptoken=<land name>": the two greedy mana sinks, now real
        // human-play activations (Mutavault's "{1}: becomes a 2/2", Sliver Hive's "{5},{T}: create a
        // Sliver"). One entry per activation; EMPTY keeps the legacy card-name-in-casts matching, so
        // a line that does not mention them is unchanged.
        std::vector<std::string> animates;
        std::vector<std::string> tap_tokens;
    };
    // One concrete plan variant the human's line matched -- when several enumerated plans
    // share the same land + cast names but differ in a per-spell sub-decision (tutor target,
    // X value, Ponder keep/shuffle, Soulfire own-target count), each distinct combination is a
    // variant the human picks among (Verdict::Choose). `label` describes what's distinct.
    // One sub-decision dimension within a variant (the fetch target, a tutor target, an X value,
    // a Soulfire own-target count). `key` is the dimension the GUI groups by (e.g. "Marsh Flats
    // fetches"); `choice` is this variant's value in that dimension (e.g. "Godless Shrine"); `card`
    // is the art to show. Structured so the GUI can ask one dimension at a time and FILTER the
    // remaining variants after each pick -- this respects couplings (a fetch target gates which
    // tutor targets are affordable this turn), so no illegal combination is ever offered.
    // `num` is the m_number of the permanent/card the choice NAMES, when it names one (an enchant or
    // equip host, a sacrifice victim). 0 = the choice is not a board object (an X value, a mode, a
    // count) . It exists because `choice` is a display string: with two Kor Duelists in play the two
    // hosts read alike, so the GUI could not tell "the creature the human dragged onto" from "the
    // other one with the same name" and auto-resolved the drag to whichever came first.
    struct SubChoice { std::string key, choice, card, kind; int num = 0; };
    struct LineVariant { int plan_index = -1; std::string label;
                         std::vector<std::string> cards;      // card names to show as art
                         std::vector<SubChoice> subs; };      // structured sub-decision dimensions
    struct LineCheck
    {
        enum class Verdict { Accept, Choose, LegalNotEnumerated, Illegal, Unsupported };
        Verdict     verdict       = Verdict::Illegal;
        int         plan_index    = -1;      // Accept: matched enumerated plan (-1 == pass)
        std::string matched_summary;         // Accept: the matched plan's summary
        std::string reason;                  // Illegal/Unsupported: human-readable detail
        std::string failed_action;           // Illegal: the action that could not be made
        std::vector<LineVariant> variants;   // Choose: the distinct sub-decision variants
    };
    static LineCheck CheckLine(const GameState& state, bool is_pre_combat,
                               const LineSpec& spec);

    // Returns the plan that leads to the earliest win, evaluated by simulating
    // the rest of the game for each candidate play at this turn.
    // depth=0 falls back to Solve.  depth=1 simulates one turn ahead using Solve
    // for all subsequent decisions; depth=2 uses depth=1 for subsequent decisions;
    // and so on.
    //
    // budget: deterministic work budget (see SearchBudget). All rollout work is
    // counted against it; nullptr means unlimited. enforce_budget governs whether
    // THIS invocation may stop iterative deepening when the budget runs out:
    //   - true  (top-level decision): applies the start-gate / overrun-guard and
    //            commits the deepest fully-completed pass.
    //   - false (rollout sub-search):  runs every pass to completion regardless,
    //            only consuming from the shared budget (preserves rollout
    //            fidelity, mirroring the old time_point::max() deadline).
    //
    // second_main: when true, the simulation plays a post-combat (second) main
    // phase each turn (greedy in the rollout), and a top-level is_pre_combat=false
    // call is treated as a real second-main decision (no combat is re-simulated).
    // Off for most decks; on only for ones whose combat enables second-main plays
    // (spectacle unlocked by combat damage, lands untapped in combat). See
    // AIEngine::SetSearchPostCombat.
    //
    // tt: per-decision transposition table memoizing SimulateToEnd. The enforcing
    // top-level call creates one when none is supplied and threads it through the
    // whole recursion; rollout sub-searches forward the table they were given.
    //
    // When is_pre_combat is true and the active player still has a land drop, the
    // land choice is folded into the candidate enumeration (each candidate carries
    // its land_to_play) and searched alongside the spells. The same fold runs in
    // the rollout, so the land decision is consistent between real game and rollout.
    // out_committed_win / out_committed_sub_depth (optional): report the committed
    // pass's exact win turn and the rollout sub_depth that proved it, so the caller
    // can detect non-convergence (a later turn's verified win exceeding an earlier
    // one). A win-this-turn reports (turn, depth-1); an empty / depth<=0 decision
    // reports (max_turns+1, 0) i.e. "no verified win".
    static Plan SolveWithLookahead(const GameState& state, bool is_pre_combat,
                                   int depth, int max_turns = 20,
                                   SearchBudget* budget = nullptr,
                                   bool enforce_budget = true,
                                   bool second_main = false,
                                   TranspositionTable* tt = nullptr,
                                   int* out_committed_win = nullptr,
                                   int* out_committed_sub_depth = nullptr);

    // One committed phase of a full-depth line: the plan to execute and whether it
    // is the pre-combat (true) or post-combat second main (false) of its turn.
    struct PhasePlan
    {
        bool is_pre_combat = true;
        Plan plan;
    };

    // The optimal line found by a full-depth search: the win turn it achieves and
    // the exact sequence of per-turn phase plans (pre-combat, then second main when
    // the deck uses one) over the fully-searched turns. Empty `phases` means no
    // play was searched (depth 0 or no candidates).
    struct SearchLine
    {
        int win_turn = 0;
        std::vector<PhasePlan> phases;
    };

    // fd-oracle diagnostic (MTG_FD_ORACLE only, inert otherwise): the smallest WIN-CLAIMING estimate
    // the VALUE leaf returned since the last ResetLeafEstimate(). Lets the oracle distinguish a win
    // the search SIMULATED from one the learned leaf merely PREDICTED -- indistinguishable in
    // SearchLine, which carries only a win_turn. LLONG_MAX => the leaf claimed no win this decision.
    static void      ResetLeafEstimate();
    static long long MinLeafEstimate();

    // Running count of search-truncation events on this thread (g_fs_trunc_events). A decision
    // whose before/after delta is ZERO ran to completion with nothing truncated anywhere beneath
    // it -- the precondition for treating its no-win as a full-coverage REFUTATION rather than
    // "I ran out" (see MTG_REFUTED_FOLLOW in AIEngine).
    static unsigned long long TruncEvents();

    // FULL-DEPTH search (experimental, env-gated via MTG_FULL_DEPTH). Unlike
    // SolveWithLookahead — which iterative-deepens the PRE-COMBAT decision and
    // approximates every future turn with a reduced-depth rollout plus a GREEDY
    // second main — this fully searches `depth` COMPLETE turns: at every turn it
    // branches over both the pre-combat plans (EnumeratePlansWithLand) and, when
    // second_main is set, the post-combat plans (EnumeratePlans), advancing the
    // turn and recursing. Beyond `depth` turns a greedy rollout (SimulateToEnd at
    // depth 0) estimates the tail. The objective is the EARLIEST win turn, with
    // branch-and-bound pruning: a plan that wins the current turn is the hard
    // floor, and any branch that cannot beat the best win found so far is abandoned.
    // Deterministic (no RNG), so thread-invariant.
    //
    // Returns the WHOLE optimal line (commit-the-line), not just the next plan, so
    // the caller can REPLAY the exact searched sequence instead of re-deciding each
    // turn. Re-deciding makes the realised win drift below the searched win (the
    // search idles on an optimistic continuation its turn-by-turn policy never
    // reproduces); replaying the committed line makes realised == searched within
    // the horizon. `state` must be positioned at the start of a pre-combat main.
    //
    // `tt` memoizes the greedy tail rollouts (leaf SimulateToEnd) across the whole
    // branch-and-bound tree, exactly as SolveWithLookahead does — the deep search
    // revisits the same leaf states many times. When null, a per-call local table
    // is created; the result is byte-identical either way (SimulateToEnd is a pure
    // deterministic function of its key), the table only avoids recompute.
    //
    // `budget` drives iterative deepening: the search runs passes of 1..depth complete
    // turns and a deterministic start gate skips a pass that won't fit the remaining
    // budget, committing the deepest pass that did fit. It also stops early at the
    // first pass that finds a win VERIFIED within its horizon (a deeper pass can only
    // push the win later), which is lossless. nullptr (or a generous budget) still
    // commits a verified win at the shallowest pass that finds it; with no verified
    // win it runs every pass and commits depth -- byte-identical to a single search.
    //
    // out_committed_depth (optional) receives the depth actually searched for the
    // committed line (= the last pass run). The caller needs it to tell a VERIFIED
    // win (win_turn <= turn + committed_depth - 1) from a greedy-tail ESTIMATE: the
    // start gate can commit a pass shallower than `depth`, so the nominal depth would
    // misjudge a shallow estimate as verified.
    static SearchLine FullSearchLine(const GameState& state, int depth,
                                     int max_turns, bool second_main,
                                     TranspositionTable* tt = nullptr,
                                     SearchBudget* budget = nullptr,
                                     int* out_committed_depth = nullptr);

    // Hybrid value-leaf search: run FullSearchLine with the cheap learned value-leaf; if it committed an
    // UNVERIFIED pass shallower than value_min_depth (the per-model trust depth, where the WEAK leaf is
    // unreliable), escalate to ONE heuristic search on the REMAINING budget and take it only if the depth it
    // can afford clears the crossover (heuristic-Hd beats value-leaf-committed iff Hd > committed-3). A
    // VERIFIED win, or a line at/above the trust depth, is kept as-is. budget_ms is unused (escalation spends
    // the remaining shared budget). value_min_depth <= 0, or no value model attached/enabled, => identical to
    // FullSearchLine (no escalation -- pure value leaf). See learned-d0-policy.md.
    // value_fallback_take_at: the table-driven per-committed-depth take-crossover (MulliganProfile::
    // value_fallback_take_at, index = committed depth, hc*[c]). When non-empty it REPLACES the uniform
    // "committed-3" crossover: take the escalation iff hcommitted >= hc*[clamp(committed)]. Empty => legacy
    // uniform offset + value_no_fallback.
    static SearchLine FullSearchLineHybrid(const GameState& state, int depth,
                                           int max_turns, bool second_main,
                                           TranspositionTable* tt, SearchBudget* budget,
                                           int* out_committed_depth,
                                           int value_min_depth, int budget_ms,
                                           bool value_no_fallback = false,
                                           const std::vector<int>& value_fallback_take_at = {},
                                           // Per-deck escalation budget renewal (value_play.escalation_fresh_frac).
                                           // Sentinel <= -1.5 (default) => use the MTG_ESCALATION_FRESH_FRAC env
                                           // static (byte-identical). -1 => legacy shared budget; >=0 => fresh frac.
                                           double escalation_fresh_frac = -2.0,
                                           // Per-deck escalation beam (value_play.beam_width / beam_leafdepth).
                                           // beam_width < 0 (default) => use the MTG_ESC_BEAM env static (byte-
                                           // identical). 0 => off; >0 => keep top-N value-ranked plans near the
                                           // leaf. beam_leafdepth: beam only nodes within that many plies of the
                                           // leaf (protects the top plies / committed play).
                                           int beam_width = -1, int beam_leafdepth = 2,
                                           // Per-deck single-depth escalation cap (value_play.escalation_cap).
                                           // 0 (default) => use the MTG_ESC_SINGLE* env statics (byte-identical).
                                           // >0 => the escalation runs ONE predicted-affordable pass capped at
                                           // this depth (predict-then-jump), instead of the 1..depth ladder.
                                           int escalation_cap = 0,
                                           // Per-deck FROZEN heuristic cost-per-probe-leaf (value_play.escalation_r),
                                           // used by the predicted-affordable walk. <=0 => 120 prior. Freezing it
                                           // keeps the adopted single-pass DETERMINISTIC (no adaptive thread_local).
                                           double escalation_r = -1.0);

    // ---- Rule-miner: enumerate-all-earliest-wins (offline diagnostic) -------------------
    // For the CURRENT pre-combat main, score EVERY candidate top-level play (the same
    // EnumeratePlansWithLand candidates the search ranks -- run with MTG_SEARCH_ORDER=1 to
    // also expand cast ORDERINGS) by the EARLIEST full-game win turn it leads to: apply the
    // play, run its combat, then full B&B-search the rest of the game (no cross-plan pruning,
    // so each candidate gets its TRUE earliest win, not the first one the search commits).
    // Emitting all candidates -- and especially the set tied at the minimum win turn -- lets
    // the analyzer mine the COMMON structure of optimal lines (cast order, which land, which
    // target) to ground ordering/targeting heuristics. EXPENSIVE (a full rollout per
    // candidate); single-game offline use only, never in the hot search. See analyze-deck 5g.
    struct EarliestWinCandidate
    {
        std::vector<std::string> cast_order;   // non-sacrifice hand casts in execution order
        std::vector<std::string> sac_casts;    // sacrifice-land casts (Shard Volley) -- after
        std::string land;                      // land played this turn ("" = none)
        std::string fetch;                     // fetch target if land is a fetchland ("" = n/a)
        bool        searched_order = false;    // true => cast_order is a searched permutation
        int         win_turn       = 0;        // earliest full-game win if this play is committed
    };
    struct EarliestWinReport
    {
        int turn     = 0;                      // the decision turn
        int earliest = 0;                      // min win_turn over all candidates
        std::vector<EarliestWinCandidate> candidates;
        // earliest_only was used => each candidate's win_turn is an UPPER BOUND, not its true
        // earliest win (a candidate that could not beat the running incumbent was cut). `earliest`
        // itself is exact. Never feed bounded candidates to a per-candidate consumer (eval rows).
        bool bounded_candidates = false;
        // The search was CUT SHORT (budget overrun / exhaustion) somewhere under this report, so
        // every number in it is unreliable in the one direction that matters: a truncated search
        // returns max_turns+1, which is indistinguishable from a genuine loss. A consumer must
        // DISCARD the position rather than emit the label -- writing it would teach the model that
        // a position it could not afford to solve is a position you cannot win from.
        bool truncated = false;
    };
    // rollout_label: label each candidate by a NON-CLAIRVOYANT greedy d0 rollout (apply plan ->
    // SimulateToEnd under the baseline policy) instead of the clairvoyant earliest-win SEARCH. Used
    // for eval-row LABEL generation to stop the oracle over-crediting durdle lines a real d0 can't
    // realise (see the antilife d0 work in learned-d0-policy.md). Default false = searched label.
    // rollout_depth: the per-turn lookahead the rollout policy uses (0 = greedy d0 = imitate the
    // baseline; >0 = a stronger searched policy, distilled via K-reshuffle averaging -> can BEAT a
    // weak baseline). Only used when rollout_label. See learned-d0-policy.md (hinata d0).
    // honest: when rollout_depth>0, DECOUPLE the continuation lookahead from the real draw order
    // (reshuffle each turn's unseen library before the lookahead, resolve against the true order) ->
    // a full-strength NON-clairvoyant teacher, not a clairvoyant deep search. See g_honest_teacher.
    // earliest_only: the caller wants ONLY report.earliest, so carry the running incumbent as the
    // cross-candidate cutoff (branch-and-bound). LOSSLESS for `earliest` -- pruning only cuts lines
    // that cannot beat the incumbent, and a line that could would not be cut -- but it makes each
    // candidate's win_turn an upper bound, so report.bounded_candidates is set. This is the same
    // bound FSLineTail already carries between siblings (std::min(cutoff, best.win_turn)); the
    // candidate loop opted out of it deliberately to give every candidate its TRUE win turn, which
    // only the eval-row dump needs. Use ONLY when value-dumping with eval-dumping off.
    static EarliestWinReport EnumerateEarliestWins(const GameState& state, int max_turns,
                                                   bool second_main, bool rollout_label = false,
                                                   int rollout_depth = 0, bool honest = false,
                                                   bool earliest_only = false);

    // Reshuffle-averaged NON-CLAIRVOYANT search as a PLAY policy (ceiling measurement +
    // learned-lookahead training target). Ranks each candidate plan by its win turn AVERAGED over K
    // reshuffled futures (common random numbers across candidates), with an honest depth-D
    // continuation (each continuation turn re-plans against a fresh reshuffle -> non-clairvoyant at
    // every ply; depth 0 = greedy non-clairvoyant rollout). Returns the best plan for the caller to
    // EXECUTE against the true library. Approaches the strongest tractable non-clairvoyant policy as
    // K/depth grow; expensive by construction (K x #plans x unmemoised honest rollout). See
    // g_honest_teacher and learned-d0-policy.md.
    static Plan ReshuffleAvgChoosePlan(const GameState& state, int K, int depth,
                                       int max_turns, bool second_main, bool is_pre_combat = true);

    // The hand-tuned baseline's plan value = Sum EvalCard(def, state) over the plan's cast cards.
    // Exposed so the learned-eval label dump (AIEngine) and the ranking seam compute the SAME
    // plan_baseline_eval feature (lockstep, non-clairvoyant). See docs/design/learned-d0-policy.md.
    static int PlanBaselineEval(const GameState& state, const std::vector<std::string>& cast_names);
};

// ---- Unit-test seam (backlog C2) ----
// The rollout's mana-payment entry point (defined in TurnSolver.cpp; previously file-static).
// External linkage exists ONLY so test/unit can drive it against AIEngine::TapForCost on
// identical fixed boards -- the executor/rollout twin-equivalence tests that guard the Tier C
// unification. Not part of the solver's interface: src/ code outside TurnSolver.cpp must keep
// paying costs through AIEngine::TapForCost (executor) or plan application (rollout).
bool TapForCostDirect(GameState& state, const ManaCost& cost_in, bool for_creature);

// The rollout's combat simulation (defined in TurnSolver.cpp; the same shared combat core the
// executor mirrors). External linkage exists ONLY for MirrorwingProvider::LegendKeepIndex, which
// decides a legend-rule keep by simulating the attack on scratch copies of the state -- the
// pending-damage projection can over-count vs the simulated combat (commit-the-line note), and
// over-counting there would discard the original Zada for a phantom lethal. Mutates `state`
// (damage, taps, pumps): call on a throwaway copy only.
void RolloutSimulateCombat(GameState& state);
