#pragma once
#include <atomic>
#include "../core/Card.h"
#include "CardTemplate.h"
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <nlohmann/json.hpp>
#include <vector>

// What a spell or ability can legally target.
// Used by the AI to check for legal targets before casting,
// and by CastSpellFromHand to build the correct Target list.
enum class Targeting
{
    None,     // no target required (creatures, draw spells, etc.)
    Any,      // any target: player, planeswalker, or creature (e.g. Lightning Bolt)
    Player,   // players/planeswalkers only
    Creature, // creatures only (e.g. Searing Blood)
    Multi,    // one player target AND one creature that player controls (e.g. Searing Blaze)
    NonlandPermanent, // any nonland permanent (Unexpectedly Absent) -- a one-line !IsLand()
                      // widening of Creature; opponent side loses nothing (spawns are always
                      // creatures), and with allow_self_target the controller's own equipment
                      // becomes a legal (self-tuck) target.
};

// Parameters extracted from a card's JSON definition, forwarded to the template handler.
struct CardParams
{
    int damage       = 0;
    int draw         = 0;
    int power_bonus  = 0;
    int tough_bonus  = 0;
    Targeting targeting = Targeting::None;
    bool sacrifice_land = false;              // additional cost: sacrifice a land (e.g. Shard Volley)
    std::optional<ManaCost> spectacle_cost;  // alternate cost when opponent lost life this turn
    std::vector<Color> produces;   // mana colors this card produces
    // Modal double-faced LAND (MDFC Pathway lands, e.g. Branchloft // Boulderloft): the FRONT face
    // is this card (name + `produces` above); the BACK face is a distinct single-color land the
    // player may CHOOSE to play instead. When `mdfc_back_name` is set the DB synthesizes a back-face
    // CardDefinition of that name that taps for `mdfc_back_produces`, and land enumeration offers BOTH
    // faces as distinct land-play options (the played permanent's IDENTITY locks its colour, read live
    // by name). Empty name = not an MDFC. In hand the card counts as its FRONT `produces` for colour
    // eval (a minor, disclosed simplification for Pathway lands; the played battlefield face is exact).
    std::string        mdfc_back_name;
    std::vector<Color> mdfc_back_produces;
    std::vector<std::string> subtypes_affected;  // for lord effects

    // On-cast trigger: when the controller casts a spell with MV <= on_cast_trigger_max_mv,
    // deal on_cast_trigger_damage to that player. Used for Eidolon of the Great Revel.
    int on_cast_trigger_max_mv  = 0;
    int on_cast_trigger_damage  = 0;

    // Mana Cannons: whenever the controller casts a multicolored spell, this permanent deals
    // (that spell's color count) damage "to any target" -- collapsed to the opponent's face,
    // provably optimal vs the passive opponent (etb_damage_any precedent). Fired from
    // FireOnCastTriggers alongside Eidolon/Aria. Needs card colors populated (BuildCardFromJson
    // colors pass).
    bool multicolor_cast_damage_per_color = false;

    // Ancient Cornucopia: whenever the controller casts a spell that's one or more colors,
    // gain life equal to its color count ("may" always taken -- free resource, no anti-lifegain
    // tech in-deck). ONCE each turn per permanent (Permanent::colored_cast_lifegain_used_this_turn,
    // reset at both untap sites). Fired from FireOnCastTriggers.
    bool colored_cast_lifegain = false;

    // Two-Headed Hellkite: "Whenever this creature attacks, draw two cards." Self-only attack
    // trigger, once per attacking copy, applied at declare-attackers alongside
    // ApplyAttackSelfPumps (ApplyAttackDrawTriggers) in BOTH executor and rollout. Also flips
    // DeckUsesSecondMain (cards drawn in combat are a combat-generated resource, 2c-bis).
    int attack_draw_cards = 0;

    // Progenitus: "If ~ would be put into a graveyard from anywhere, reveal it and shuffle it
    // into its owner's library instead." Wired at the cleanup-discard sites (executor
    // CleanupStep + rollout scripted-discard mirror) -- the only graveyard path reachable for
    // this card in the current engine (it cannot die: no opponent damage/removal, no self-sac).
    bool graveyard_replace_shuffle_library = false;

    // Progenitus: "Protection from everything." Vs the passive goldfish opponent this is inert
    // for THEIR actions, but it also blocks our OWN equipment -- equip targets (CR 702.6b), so
    // a protected creature can never be a host (Lightning Greaves haste was an illegal line,
    // found 2026-08-19). Read by the equip host enumeration (TurnSolver); no general targeting
    // sites need it today (nothing else of ours targets our creatures in this deck class).
    bool protection_from_everything = false;

    // Maelstrom Archangel: "Whenever this creature deals combat damage to a player, you may cast
    // a spell from your hand without paying its mana cost." Modelled as BANKING (user-approved
    // 2026-08-06): connecting increments GameState::free_casts_available (Combat.cpp, shared
    // rollout+executor), spent as a free-cast plan variant in the post-combat main. CAUTION
    // (user): banking is only safe because the free cast itself carries nothing across a phase
    // boundary -- a future card producing MANA mid-combat (or otherwise benefiting from resolving
    // in a different phase) must NOT reuse this pattern blindly. Also flips DeckUsesSecondMain.
    bool combat_damage_free_cast = false;

    // Landfall: if > 0 and a land entered the battlefield under the caster's control
    // this turn, use this value instead of `damage` (e.g. Searing Blaze).
    int  landfall_damage = 0;

    // If true, drawn cards are placed in the staged exile zone rather than the hand
    // and expire at the end of the player's next turn (e.g. Light Up the Stage).
    bool stages_cards = false;

    // Death trigger: if > 0 and the targeted creature dies from this spell's damage,
    // deal this much damage to its controller (e.g. Searing Blood).
    int death_trigger_damage = 0;

    // Lord scaling: if true, the P/T bonus scales per other matching creature on the
    // battlefield rather than being a flat bonus (e.g. Predatory Sliver).
    bool scales_per_matching = false;

    // Attack trigger: deal this much damage to the opponent per attacker that matches
    // subtypes_affected (e.g. Leeching Sliver: 1 per attacking Sliver).
    int attack_trigger_life_loss = 0;

    // Keyword lords: grant the named keyword to all creatures matching subtypes_affected.
    bool grants_haste        = false;  // Cloudshredder Sliver, Thrumming Hivepool
    bool grants_double_strike = false; // Thrumming Hivepool

    // Affinity for subtype: reduce this card's generic mana cost by 1 per matching
    // permanent you control (e.g. Thrumming Hivepool — Affinity for Slivers).
    bool affinity_for_subtype = false;
    // Medallion static cost reducer: a permanent (e.g. Ruby Medallion = "R") that reduces the
    // GENERIC cost of every spell of this colour YOU cast by 1 (floored at 0, stacks per copy).
    // Empty = not a reducer. Applied in EffectiveCost off permanents already in play; a Medallion
    // cast the SAME turn as the spell it discounts is handled by the ManaPruneBound bail.
    std::string reduces_spell_color;

    // Aether Vial: if true, this permanent gains a charge counter each upkeep (with
    // AI heuristic to stop at the optimal count), and can tap to put a creature from
    // hand with MV equal to the counter count onto the battlefield.
    bool upkeep_adds_charge = false;

    // Animated land (e.g. Mutavault): if true, the AI may pay animate_cost during the
    // main phase to make this permanent a creature with animate_power/animate_toughness
    // and all creature types until end of turn.
    bool                   can_animate      = false;
    int                    animate_power    = 0;
    int                    animate_toughness = 0;
    std::optional<ManaCost> animate_cost;

    // Replicate: if true, this permanent itself has replicate (cost = its own mana cost),
    // creating a token copy for each additional time the cost is paid at cast time.
    bool has_replicate = false;

    // Replicate lord: if true, Sliver spells (or matching subtypes_affected) you cast
    // have replicate (e.g. Hatchery Sliver). Checked on permanents already in play when
    // a new matching creature is cast.
    bool grants_replicate_to_subtypes = false;

    // Creature-only mana: if true, mana from this land may only be spent to cast creature
    // spells (e.g. Ancient Ziggurat). Enforced at payment time and in solver pool checks.
    bool creature_mana_only = false;

    // Colored-creature-only mana (Unclaimed Territory / Cavern of Souls / Secluded Courtyard):
    // "{T}: Add {C}. {T}: Add one mana of any color -- spend only on a creature spell of the chosen
    // type." Unlike creature_mana_only, the {C} is UNRESTRICTED; only the COLORED mana is creature-
    // Haven of the Spirit Dragon: "{2}, {T}, Sacrifice this land: Return target Dragon creature card
    // from your graveyard to your hand." Set gy_return_cost to enable (empty = no such ability).
    // gy_return_requires_subtype narrows the legal targets ("Dragon"; empty = any creature card);
    // gy_return_requires_creature gates on the graveyard card being a creature. The source is TAPPED
    // and SACRIFICED as part of the cost, so it can never pay its own {2} and never targets itself.
    // WHICH card to return is a real choice: the enumeration emits one Action per distinct legal
    // graveyard card NAME (carried on Action::tutor_target and folded into the plan signature), so
    // the search picks among them and the viewer surfaces every option -- never a "first match".
    std::optional<ManaCost> gy_return_cost;
    std::string             gy_return_requires_subtype;
    bool                    gy_return_requires_creature = false;

    // restricted. Modelled as produces = [C, <colors>] + this flag: at payment time a non-creature
    // spell may take only {C} from the source (ProducesForPayment strips the colours), a creature
    // spell may take any colour. The chosen creature TYPE is simplified to "any creature" (these
    // lands are played in single-tribe decks, so type == any-creature for every deck tested). See
    // docs/design/unclaimed-territory-restricted-mana.md.
    bool colored_creature_only = false;
    // D12 (Secluded Courtyard): the restricted coloured mana is ALSO legal for an activated
    // ability whose source is a creature of the chosen type (Cavern of Souls / Unclaimed
    // Territory / Sliver Hive lack this clause -- casts only). Consulted by ProducesForPayment
    // under the CreatureAbilityPayScope payment context.
    bool colored_creature_ability_ok = false;

    // Upkeep token creation: at the beginning of upkeep, create N creature tokens
    // with the given power/toughness/subtypes (e.g. Thrumming Hivepool: 2 × 1/1 Sliver).
    int upkeep_creates_tokens     = 0;
    int upkeep_token_power        = 0;
    int upkeep_token_toughness    = 0;
    std::vector<std::string> upkeep_token_subtypes;

    // Tap-and-pay activated token creation: {tap_token_cost}, {T} creates 1 token
    // with the given power/toughness/subtypes. Only activatable when at least one
    // permanent matching tap_token_requires_subtypes is controlled (e.g. Sliver Hive).
    std::optional<ManaCost>  tap_token_cost;
    int                      tap_token_power     = 0;
    int                      tap_token_toughness = 0;
    std::vector<std::string> tap_token_subtypes;
    std::vector<std::string> tap_token_requires_subtypes;

    // Enters tapped (e.g. Saprazzan Skerry, Lonely Sandbar, Temple of Epiphany).
    // If true, the permanent is placed on the battlefield tapped and cannot
    // produce mana until the next turn's untap step.
    bool enters_tapped = false;

    // Fastland (Razorverge Thicket): "enters tapped UNLESS you control N or fewer OTHER lands."
    // >= 0 gates it: enters untapped iff (other lands you control) <= this value (Razorverge: 2).
    // Evaluated in LandWouldEnterTapped (the ETB authority). -1 = not a fastland (default) ->
    // byte-identical for every other deck.
    int fastland_max_other_lands = -1;

    // No maximum hand size (e.g. Reliquary Tower). If true, the cleanup-step discard
    // to 7 is skipped while this permanent is on the battlefield.
    bool no_max_hand_size = false;

    // Land's Edge pattern: "Discard a land card: deal this much damage to target player."
    // When > 0 this permanent provides the ability; AI will discard all hand lands for damage.
    int  discard_land_damage = 0;

    // Cascade: when cast, exile from library top until a nonland card with mana value
    // strictly less than cascade_max_mv is found; cast it for free; put the rest on the bottom.
    // 0 = no cascade.
    // Since the BreachingDragonstorm onboarding (2026-09-03) cascade is a real CAST TRIGGER:
    // CastSpellFromHand / PushFreeCast push one Triggered{Cascade} stack entry per instance
    // ABOVE the spell, so the cascade exile-walk and its free cast resolve BEFORE the casting
    // spell does (CR 601.2i / 702.85a) -- which is what lets cascade fire on CREATURE and
    // other permanent spells (previously it was reachable only on non-permanent resolution).
    int  cascade_max_mv = 0;

    // Number of cascade INSTANCES on the card ("Cascade, cascade" = 2 -- Maelstrom Wanderer,
    // Call Forth the Tempest). Read only when cascade_max_mv > 0. Each instance is its own
    // Triggered{Cascade} entry, resolving fully (exile walk + free cast) before the next
    // begins, so the second cascade sees the library the first one left. Default 1 keeps
    // every existing cascade card byte-identical.
    int  cascade_count = 1;

    // Breaching Dragonstorm: "When this enchantment enters, exile cards from the top of your
    // library until you exile a nonland card. You may cast it without paying its mana cost if
    // that spell's mana value is 8 or less. If you don't, put that card into your hand."
    // THREE deliberate divergences from cascade, all oracle-faithful: the exile walk has NO
    // mana-value bound (the first nonland always stops it); the exiled non-hit LANDS stay in
    // state.exile PERMANENTLY (the oracle gives them no disposition -- cascade bottoms its
    // non-hits), which in a 37-land list is real cumulative library thinning; and a declined /
    // over-MV hit goes to HAND (cascade bottoms it). etb_exile_free_cast_max_mv is the
    // ORACLE-PRINTED constant (8), NOT this card's cmc (5) -- do not "correct" it, and do not
    // extend audit_card_costs.py's cascade_max_mv==cmc cross-check to it. Fired through the
    // universal enter path (FireOwnEtbTriggers -> pending queue), so a Sakashima's Protege
    // entering as a copy re-fires it.
    bool etb_exile_until_nonland    = false;
    int  etb_exile_free_cast_max_mv = 0;

    // Creative Technique: "Shuffle your library, then reveal cards from the top of it until
    // you reveal a nonland card. Exile that card and put the rest on the bottom of your
    // library in a random order. You may cast the exiled card without paying its mana cost."
    // The shuffle MUST go through ShuffleAfterSearch (CRN) -- an ad-hoc Shuffle() would desync
    // executor and rollout. The revealed non-hits bottom in reveal order, which off a
    // just-shuffled library IS a uniform random order (faithful; no second shuffle -- that
    // would burn a search_count ordinal). A declined hit stays in exile.
    bool shuffle_reveal_freecast = false;

    // Demonstrate (Creative Technique, CR 702.145): when you cast this spell, you may copy it.
    // The copy resolves BEFORE the original (2021-04-16 ruling), is NOT a cast (no cast
    // triggers, no spells_cast_this_turn increment) and ceases to exist on resolution (never
    // touches the graveyard -- StackEntry::is_copy). The opponent's copy ("choose an opponent
    // to also copy it") IS modelled (2026-09-03): when the opponent has a library
    // (opponent_library_dealt), their copy shuffles THEIR library and walks reveal-until-
    // nonland, resolving first (APNAP); their free cast is declined by the model (no opponent
    // casting machinery -- the engine boundary), so the hit stays exiled. No-op while no
    // demonstrate deck deals the opponent a library (see the card's bracket note).
    bool demonstrate = false;

    // Sakashima's Protege: "You may have this creature enter as a copy of any permanent that
    // entered this turn." As it enters, the entering Permanent's card is replaced by the chosen
    // source permanent's PRINTED card (CR 706.2 copiable values -- no counters, no attachments,
    // no temp pumps), keeping this cast's own m_number for per-copy identity; enter triggers
    // then fire for the COPIED card through the normal enter path (a copy of Breaching
    // Dragonstorm re-fires its exile trigger). Choice rides StackEntry/Action::copy_target
    // (searched plan variant / human chooser); 0 = heuristic pick (best-power entrant) /
    // decline when none. Scoped to permanents STILL on the battlefield with entered_this_turn
    // (the entered-and-left set is provably empty in this deck -- see the bracket note).
    bool enter_as_copy_of_entrant = false;

    // Call Forth the Tempest clause 2: "deals damage to each creature your opponents control
    // equal to the total mana value of OTHER spells you've cast this turn." Damage source =
    // GameState::mv_cast_this_turn (summed at every cast site, lockstep with
    // spells_cast_this_turn) minus THIS card's own mana value ("other" excludes exactly one
    // instance of itself; a second copy cast earlier this turn counts). Cascade free-casts off
    // this very spell resolve BEFORE it (cast triggers), so their MV counts -- CR-correct, the
    // clause reads state at resolution. Real DAMAGE (marked, SBA death when damage >=
    // toughness, indestructible respected), not a debuff -- contrast etb_opp_creatures_debuff.
    // Untargeted sweep, no decision surface. Kills fire FireOppCreatureDies (Massacre Wurm
    // watchers). Opponent creatures exist in 8 of 10 goldfish games (the spawn schedule), so
    // this is live board contact; it cannot change the CLOCK in a deck without opponent-death
    // watchers (spawns never block/attack), which is why it was once deferred as inert.
    bool damage_opp_creatures_mv_cast = false;

    // Breaching Dragonstorm clause 2: "When a Dragon you control enters, return this
    // enchantment to its owner's hand." Non-empty = the watched subtype ("Dragon"). Fires from
    // the universal enter cascade (FireEtbWatchers) for ANY enter route -- cast, put, token,
    // copy -- via a pending-bounce queue (g_pending_etb_self_bounces) drained after the
    // resolution completes, because erasing a battlefield permanent mid-cascade would shift
    // callers' saved slot indices. Composition note: no current deck pairs this card with a
    // Dragon, so the trigger is live machinery that never fires today; it goes live the moment
    // any Dragon (including a Lathliss/Utvara token or a Protege copying a Dragon) enters
    // under the controller.
    std::string self_bounce_on_etb_subtype;

    // Retrace (e.g. Throes of Chaos): this card may be cast from the graveyard by
    // discarding a land card as an additional cost. It is not exiled, so it returns
    // to the graveyard on resolution and can be retraced again on a later turn.
    bool retrace = false;

    // Shock land (e.g. Steam Vents): "As this land enters, you may pay N life. If you
    // don't, it enters tapped." The AI pays the life (entering untapped) whenever it
    // can afford to, since early-game speed dominates in a goldfish.
    int etb_pay_life_to_untap = 0;

    // Conditional-untap reveal land (e.g. Frostboil Snarl): the land enters tapped
    // unless the player can reveal a card of one of these subtypes from hand. Empty =
    // no condition. Checked against m_subtypes of other cards in hand at ETB.
    std::vector<std::string> etb_untap_reveal_subtypes;

    // ETB scry (e.g. Temple of Epiphany: scry 1). When > 0, look at the top N cards on
    // ETB and bottom the unwanted ones via a deck-aware heuristic (see ScryTop).
    int etb_scry = 0;

    // ETB surveil (e.g. Thundering Falls: surveil 1). Like etb_scry but unwanted cards go
    // to the GRAVEYARD instead of the library bottom (true deck thinning). Same keep/bin
    // heuristic as ScryTop; see SurveilTop.
    int etb_surveil = 0;

    // Pain land (e.g. Fiery Islet): tapping this land for mana costs the controller
    // this much life. Applied at tap time in TapForCost / TapForCostDirect.
    int tap_self_damage = 0;

    // Cycling (e.g. Lonely Sandbar, Forgotten Cave, Remote Isle): pay this cost and
    // discard the card from hand to draw a card. Modelled as an activated ability the
    // AI uses only when the card is surplus (see AIEngine cycling heuristic).
    std::optional<ManaCost> cycling_cost;

    // Sacrifice-to-draw activated ability (e.g. Fiery Islet: {1}, {T}, Sacrifice: draw
    // a card). Cost is this mana plus tapping and sacrificing the source.
    std::optional<ManaCost> sacrifice_draw_cost;

    // Depletion land (e.g. Saprazzan Skerry, Sandstone Needle): enters tapped with this
    // many depletion counters; each tap removes one and adds two mana of `produces[0]`;
    // when the last counter is removed the land is sacrificed. 0 = not a depletion land.
    int enters_tapped_with_depletion = 0;

    // Mana produced per tap (default 1). Depletion lands set this to 2 so one tap yields
    // two mana of `produces[0]` (the {U}{U} / {R}{R} ability). The extra mana is held in
    // a local floating pool during a single cost payment.
    int produces_amount = 1;

    // Storage-counter land (Dwarven Hold, Mercadian Bazaar): a battery that accumulates
    // storage counters over turns, then a single tap removes ANY NUMBER of counters to add
    // that many {R} (a VARIABLE-size burst; produces[0] is the burst colour). Distinct from a
    // depletion land: it is NOT sacrificed and its per-tap yield = the PERMANENT's current
    // storage_counters (not the static produces_amount). Both cards enter tapped. The per-tap
    // burst amount is read per-permanent via PermanentManaYield; storage_land gates every
    // storage path so a false value is byte-identical. false = not a storage land.
    bool storage_land = false;
    // How the battery charges: "upkeep_if_tapped" (Dwarven Hold: +1 at upkeep while held tapped)
    // or "tap" (Mercadian Bazaar: {T} main-phase action banking +1, no mana). Both are modelled
    // by the same weakly-dominant "charge an idle storage land at end of turn" rule (a storage
    // land left untapped this turn banks +1 counter), which reproduces the optimal +1/idle-turn
    // accumulation of BOTH modes for goldfish; the mode string is recorded for future fidelity.
    std::string storage_charge_mode;

    // Filter land (e.g. Cascade Bluffs): "{T}: Add {C}. {U/R}, {T}: Add {U}{U}/{U}{R}/
    // {R}{R}." Modelled as color-fixing, not ramp: a filter tap is fed one mana of a
    // `produces` colour from another source, then yields two of `produces`. It can only
    // make `produces` colours when such a feeder exists; otherwise it taps for {C}.
    bool is_filter = false;

    // Ramp filter (e.g. Ferrous Lake: "{1}, {T}: Add {U}{R}"). Distinct from is_filter:
    // the activation cost is {1} GENERIC (any mana, including a filter's {C}), not a
    // coloured pip, and there is NO free mode — it produces nothing without a feeder.
    // When fed it yields ONE of EACH `produces` colour (net +1 mana). Modelled in the
    // mana pool as +1 wild iff another untapped source can pay the {1}, else 0.
    bool ramp_filter = false;

    // --- Knights tribal (white aggro) extensions ---

    // Anthem for ALL creatures you control (e.g. Benalish Marshal: "Other creatures you
    // control get +1/+1"). On a lord_effect this applies power_bonus/tough_bonus to every
    // creature the controller controls, regardless of subtype. Distinct from
    // subtypes_affected (which matches only listed subtypes). Empty subtypes_affected has
    // always meant "match nothing"; this flag is the explicit "match all creatures" case.
    bool affects_all_creatures = false;

    // Lord that excludes itself from its own buff (e.g. "Other Knights you control get
    // +1/+1" — Inspiring Veteran, Knight Exemplar, Benalish Marshal, Marshal of Zhalfir,
    // Haytham Kenway). When true, the lord does NOT apply its bonus to the very permanent
    // granting it. Default false keeps the existing "All/your Slivers get +1/+1" lords
    // (which DO buff themselves) byte-identical.
    bool lord_excludes_self = false;

    // Characteristic-defining ability: this creature's power equals the number of
    // creatures its controller controls (e.g. Adeline, Resplendent Cathar, printed */4).
    // Set card power to 0 in JSON; the count is added on top of counters/temp/lords at
    // damage time. Counts every creature the controller controls (including itself and
    // tokens), plus animated lands.
    bool power_equals_creature_count = false;

    // Cast-trigger token creation (e.g. Worthy Knight: "Whenever you cast a Knight spell,
    // create a 1/1 white Human token"). When cast_trigger_creates_tokens > 0, casting a
    // spell whose subtypes include cast_trigger_subtype makes that many tokens with the
    // given P/T and subtypes. Fired from FireOnCastTriggers (both real game and rollout).
    std::string              cast_trigger_subtype;       // subtype the CAST spell must have
    int                      cast_trigger_creates_tokens = 0;
    int                      cast_token_power     = 0;
    int                      cast_token_toughness = 0;
    std::vector<std::string> cast_token_subtypes;

    // Cast-trigger token creation keyed on the CARD TYPE of the cast spell rather than a creature
    // subtype (Young Pyromancer: "Whenever you cast an instant or sorcery spell, create a 1/1 red
    // Elemental creature token"). Shares the cast_token_* spec above plus created_token_color.
    // Fired from the same FireOnCastTriggers pass as cast_trigger_creates_tokens, so executor and
    // rollout stay lockstep. A COPY is not cast (CR 707.10), so a Zada/Mirrorwing fan-out fires
    // this exactly once -- on the original cast -- which is the rules-correct count.
    // 0 = not an instant/sorcery cast-trigger -> byte-identical for every other deck.
    int                      cast_trigger_instant_sorcery_tokens = 0;

    // Attack-trigger token creation (e.g. Adeline, Resplendent Cathar: "Whenever you
    // attack, for each opponent, create a 1/1 white Human token that's tapped and
    // attacking"). When attack_creates_tokens > 0, declaring at least one attacker makes
    // that many tokens (per opponent = 1 in goldfish) which are tapped and deal combat
    // damage this turn, then persist. Fired at combat (real game + rollout).
    int                      attack_creates_tokens = 0;
    int                      attack_token_power     = 0;
    int                      attack_token_toughness = 0;
    std::vector<std::string> attack_token_subtypes;

    // --- Dragonstorm kill-engine (Scourge of Valkas / Lathliss / Utvara Hellkite) ---
    // All new paths are gated on these params, so decks that never set them are byte-identical.

    // Scourge of Valkas: "Whenever this creature or another Dragon you control enters, it deals
    // X damage to any target, where X is the number of Dragons you control." Modelled as
    // opponent LIFE LOSS of (Dragons you control, INCLUDING the just-entered one). Fired from
    // FireEtbWatchers (SpellEffects.h) at EVERY dragon-enter site -- executor EnterBattlefield,
    // rollout creature-enter, and every CreateToken (so a Lathliss/Utvara token also pings).
    // Multiple Scourges each ping. "Any target" collapses to the opponent's face (optimal /
    // byte-identical in a passive goldfish). Firebreathing "{R}: +1/+0" via firebreathing_* below.
    bool                     dragon_ping_on_enter = false;

    // Lathliss, Dragon Queen: "Whenever another nontoken Dragon you control enters, create a
    // 5/5 red Dragon creature token with flying." When set, FireEtbWatchers creates the token for
    // each NONTOKEN Dragon (Permanent::is_token == false) that enters, is not this Lathliss
    // itself, and whose subtypes include etb_token_requires_subtype. Token-first ordering: the
    // 5/5 is created (and re-pings Scourge via CreateToken) BEFORE the newcomer's own Scourge
    // ping resolves, so both see the higher Dragon count. The token is is_token=true so it never
    // re-triggers Lathliss (loop-safe). Team firebreathing "{1}{R}:" via team_pump_* below.
    bool                     etb_other_subtype_creates_tokens = false;
    std::string              etb_token_requires_subtype;      // "Dragon"
    int                      etb_created_token_power     = 0;
    int                      etb_created_token_toughness = 0;
    std::vector<std::string> etb_created_token_subtypes;
    // Keywords the created token is printed with (Lathliss/Utvara: "with flying"). Historically
    // dropped because Flying is inert in a goldfish; Dragon Tempest reads it ("a creature you
    // control WITH FLYING enters"), so it is now carried. Empty = the prior keywordless token.
    std::vector<std::string> etb_created_token_keywords;

    // Utvara Hellkite: "Whenever a Dragon you control attacks, create a 6/6 red Dragon creature
    // token with flying." PER attacking matching creature (not the flat tapped-and-attacking
    // Adeline shape above). Fired at declare-attackers (SimulateCombat / GameEngine::CombatPhase):
    // for each source, count declared attackers matching attack_token_requires_subtypes (empty =
    // any; animated land = all types) and create N x count tokens that enter UNTAPPED and
    // summoning-sick -- they do NOT join this combat (they attack next turn, or this turn only if
    // a haste-lord grants their "Dragon" subtype haste). Each token entering fires FireEtbWatchers
    // (Scourge ping / Lathliss token) via CreateToken.
    int                      attack_per_matching_creates_tokens = 0;
    int                      attack_per_token_power       = 0;
    int                      attack_per_token_toughness   = 0;
    std::vector<std::string> attack_per_token_subtypes;
    std::vector<std::string> attack_per_token_keywords;       // see etb_created_token_keywords
    std::vector<std::string> attack_token_requires_subtypes;  // attacker subtypes counted (empty = any)

    // Firebreathing (activated pump; converts LEFTOVER combat mana -> attacker power = face
    // damage). Applied in the combat step of BOTH worlds (ApplyFirebreathing, SpellEffects.h) so
    // the win projection sees the extra damage and the search pumps for lethal. Self: Scourge
    // "{R}: this creature gets +1/+0 until end of turn" (firebreathing_cost {R}, power 1). Team:
    // Lathliss "{1}{R}: Dragons you control get +1/+0 until end of turn" (team_pump_cost {1}{R},
    // power 1, subtypes ["Dragon"]). Empty cost -> inert (other decks byte-identical).
    std::optional<ManaCost>  firebreathing_cost;
    int                      firebreathing_power = 0;
    std::optional<ManaCost>  team_pump_cost;
    int                      team_pump_power = 0;
    std::vector<std::string> team_pump_subtypes;

    // Inferno of the Star Mounts: "{R}: gets +1/+0 until end of turn. When its power becomes 20
    // THIS WAY, it deals 20 damage to any target." A threshold rider on the SELF firebreathing
    // above: after an activation lands on this permanent, if its power is now exactly
    // firebreathing_threshold_power, deal firebreathing_threshold_damage to the opponent's face
    // ("any target" collapses to the face -- optimal vs a passive opponent, the Scourge precedent).
    // "This way" = the increment that crosses the line must come from THIS ability, which is what
    // checking at the activation site gives; other pumps (a Lathliss team pump) legitimately count
    // toward the total. Fires at most once per permanent per turn (the equality test cannot
    // re-trigger, since further activations push power past the threshold), and the damage is
    // opponent LIFE LOSS so the rollout's win projection counts it toward lethal. 0 = inert.
    int                      firebreathing_threshold_power  = 0;
    int                      firebreathing_threshold_damage = 0;

    // Dragon Tempest: "Whenever a creature you control with flying enters, it gains haste until
    // end of turn." A permanent you control with this flag grants Permanent::temp_haste to every
    // entering controlled creature that has flying (read by CanAttackFull / CanTapNow, cleared each
    // cleanup -- the Expedite grants_temp_haste machinery). Fired from the universal enter cascade
    // (FireEtbWatchers' creature-enter watcher block), so it covers hard-cast Dragons AND every
    // token (Lathliss 5/5, Utvara 6/6 -- both enter with flying). Tempest's OTHER clause, the
    // Dragon-count ping, is exactly Scourge's and rides dragon_ping_on_enter above. false = inert.
    bool                     haste_on_flying_enter = false;

    // ETB library dig (e.g. Acclaimed Contender: "When this enters, if you control another
    // Knight, look at the top five cards of your library. You may reveal a Knight ... and
    // put it into your hand. Put the rest on the bottom of your library."). When
    // etb_dig_count > 0 and the controller controls another creature whose subtype is in
    // etb_dig_requires_subtypes (empty = no condition), look at the top etb_dig_count
    // cards, move the first whose subtype is in etb_dig_subtypes into hand, and put the
    // rest on the bottom (deterministic order; the printed "random order" is unobservable
    // in goldfishing). Done at resolution (EffectHandler) and in the rollout
    // (ApplyPlanDirect); the dug card is cast on a later turn (no same-turn re-solve).
    int                      etb_dig_count = 0;
    std::vector<std::string> etb_dig_subtypes;
    std::vector<std::string> etb_dig_requires_subtypes;

    // --- Anti-Lifegain (Tainted Remedy / Aria of Flame) extensions ---

    // Static replacement (Tainted Remedy; Plague Drone's "Rot Fly"): while the controller
    // has a permanent with this flag, "an opponent would gain N life" becomes "that opponent
    // loses N life instead" (CR 614.12). This is the deck's entire damage engine -- every
    // "opponent gains life" rider below is routed through OpponentGainsLife (SpellEffects.h),
    // which checks RemedyActive. Default false -> no effect for other decks.
    bool lifegain_to_loss = false;

    // Resolution rider "target opponent / each opponent gains N life" (Fiery Justice: 5).
    // Applied at resolution via OpponentGainsLife, so Tainted Remedy reverses it into damage.
    int opponent_lifegain = 0;

    // ETB "each opponent gains N life" (Aria of Flame: 10). Applied via OpponentGainsLife
    // when the permanent enters -- with Tainted Remedy out it is 10 immediate damage.
    int etb_opponent_lifegain = 0;

    // Verse engine (Aria of Flame): when the controller CASTS an instant or sorcery, put a
    // verse counter on this permanent, then it deals (verse counters) damage to the opponent.
    // Fired from FireOnCastTriggers (real game + rollout). Counter stored on Permanent.
    bool verse_damage = false;

    // Alternate cost (Invigorate: 3 / Skyshroud Cutter: 5 / Reverent Silence: 6): "If you
    // control a <alt_cost_requires_subtype>, rather than pay this spell's mana cost, you may
    // have an opponent gain alt_lifegain_cost life." The spell still resolves its normal
    // effect; only the cost changes. The search offers it as a separate ZERO-mana Action
    // when the controller controls a permanent with alt_cost_requires_subtype, applying the
    // opponent lifegain (-> damage under Tainted Remedy) at cast time. 0 = no alt cost.
    int         alt_lifegain_cost = 0;
    std::string alt_cost_requires_subtype;

    // Destroy all enchantments on resolution (Reverent Silence) -- includes the caster's own.
    bool destroy_all_enchantments = false;

    // Tutor: search the library for a card whose card type is in tutor_types and move it to
    // the hand (tutor_to_hand) or the top of the library (tutor_to_top). Deterministic target
    // choice via TutorPick (SpellEffects.h). Idyllic Tutor (enchantment -> hand), Enlightened
    // Tutor (artifact/enchantment -> top).
    bool                     tutor_to_hand = false;
    bool                     tutor_to_top  = false;
    std::vector<std::string> tutor_types;
    // Dragonstorm (Storm) tutor-TO-BATTLEFIELD: on resolution, put min(spells_cast_this_turn,
    // #library cards matching tutor_types) cards (Dragons) from the library ONTO THE BATTLEFIELD
    // (not hand/top). Each put routes through the shared FireEtbWatchers cascade (Scourge ping /
    // Lathliss token) via PerformTutorToBattlefield (SpellEffects.h), lockstep executor
    // (EffectHandler) + rollout (apply_one). Which/order of Dragons = the provider's TutorCandidates
    // (search enumeration; a future DragonstormProvider owns the Lathliss-first/Scourge-second
    // ranking). Empty tutor_types / false -> ordinary spell (every other deck byte-identical).
    bool                     tutor_to_battlefield = false;
    // "then shuffle your library" after a tutor/search (Dragonstorm KEEPS the shuffle, per user):
    // deterministic CRN reshuffle (Library::ShuffleByKey via ShuffleAfterSearch) exactly like a
    // fetch, so post-Dragonstorm draws come from a shuffled deck. Off -> no reshuffle.
    bool                     tutor_shuffle_after  = false;
    // WISH (Living Wish, "reveal a creature or land card you own from OUTSIDE THE GAME and put it
    // into your hand"). A wish is mechanically a tutor whose search ZONE is the sideboard rather
    // than the library (CR 400.11b: in constructed, a sideboard card is outside the game), so it
    // rides the whole searched-tutor apparatus -- tutor_to_hand + tutor_types, the Plan::tutor_choice
    // index axis, the plan-signature folds, the pin, the viewer chooser -- with only the pool
    // swapped. Building a parallel wish axis instead would have re-created the dedup bug that once
    // made the tutor target unsearched, and would need all five signature folds re-added by hand.
    //
    // Two consequences a reader must not miss. (1) Each sideboard card is a SINGLETON consumed on
    // fetch, so the pool is per-game state (Player::sideboard), not a static read of the decklist --
    // four Living Wishes must see a shrinking pool. (2) A wish does NOT search a library, so
    // CR 701.19c's shuffle never triggers: PerformTutor's ShuffleAfterSearch is explicitly SKIPPED
    // on this path. That is not covered by leaving tutor_shuffle_after false -- the call there is
    // unconditional -- and letting it run would advance search_count, which seeds every later
    // fetch's deterministic reshuffle.
    //
    // scripts/analyze_deck.py reads this param name as its most authoritative sideboard-reachability
    // detector, so renaming it silently un-scans a wish deck's toolbox.
    bool                     wish_from_sideboard  = false;
    // "Exile <this card>" on resolution, instead of the graveyard (Living Wish). NOT inert, though
    // nothing in this deck can interact with an exiled card: MidGameFeature::GraveyardSize and
    // ExileSize are real learned-model features, and EOT dominance folds each zone when the attached
    // model reads it -- so four Living Wishes in the graveyard is a different state from four in
    // exile to any value leaf that splits on either size. Recording it as inert would be recording
    // an engine gap as a card fact, and would be wrong the day this deck's value leaf lands.
    bool                     exiles_self_on_resolve = false;
    // Tutor target selection. The intended DEFAULT (empty) is a SEARCHED choice -- the
    // search branches over fetch targets and keeps the best (a Tier-2 search-choice like
    // dig-as-search-choice; pending, see follow-up). A non-empty value names a HEURISTIC
    // override used instead of searching. "enabler_then_wincon" = TutorPick: fetch a combo
    // enabler (lifegain_to_loss) while none is active, else the wincon engine (verse_damage),
    // else the first match. (Until searched-tutor lands, the default also uses TutorPick.)
    std::string              tutor_heuristic;
    // Gamble's downside: after the fetched card lands in hand, discard one uniformly-random card
    // (can be the tutored card). Deterministic seed (game_seed/turn/search_count), applied inside
    // PerformTutor so the rollout and executor stay lockstep. Off everywhere else.
    bool                     discard_random_after_tutor = false;

    // Removal rider (Swords to Plowshares): the exiled creature's controller gains life equal
    // to its power. Routed through OpponentGainsLife when that controller is the opponent (so
    // with Tainted Remedy it becomes damage equal to the exiled creature's power).
    bool controller_lifegain_equals_power = false;

    // Per-tap opponent lifegain (Grove of the Burnwillows: "{T}: Add {R} or {G}. Each opponent
    // gains 1 life."). When > 0, tapping this source for mana makes the opponent gain this much
    // (-> that much DAMAGE with a Tainted Remedy / Plague Drone). Applied via OpponentGainsLife
    // at tap time, mirroring tap_self_damage. Gated > 0 so other decks are unaffected.
    int tap_opponent_lifegain = 0;

    // Fetchland (Windswept Heath etc.): "{T}, pay 1 life, sacrifice this land: search your
    // library for a land whose subtype is in this list, put it onto the battlefield, then
    // shuffle." When non-empty the land is resolved by PerformFetch (SpellEffects.h) instead
    // of entering itself: it pulls the heuristically/searched-chosen library land, makes IT
    // enter (resolving that land's own enters-tapped/shock choice), pays 1 life, and sends the
    // fetchland to the graveyard. The fetchland keeps a `produces` list purely so a copy in
    // HAND counts as a flexible colour source for color-fixing heuristics; it never taps for
    // mana (it never reaches the battlefield). Empty -> ordinary land (other decks unaffected).
    std::vector<std::string> fetch_land_types;

    // Pump spell that targets YOUR OWN creature (Invigorate: "Target creature gets +4/+4").
    // Default false keeps the existing pump/creature targeting (opponent creature). When true,
    // the spell targets the controller's best attacker and the bonus is applied until end of
    // turn. Gated so no existing deck is affected.
    bool target_own_creature = false;

    // Artifact mana source / mana rock (Sol Ring "{T}: Add {C}{C}", Izzet Signet "{1},{T}: Add
    // {U}{R}"). A noncreature permanent that taps for mana like a land but is CAST as a spell
    // (not a land drop) and, being a noncreature, has no summoning sickness -- it taps the turn
    // it enters (Permanent::CanTap() is always true for non-creatures). Recognised as a tappable
    // mana source everywhere a BasicLand/ManaDork is (BuildPool / TapForCost / ComputeAvailable-
    // Colors / ...), using the same `produces` / `produces_amount` / `ramp_filter` fields. Kept a
    // noncreature ON PURPOSE: unlike a creature mana dork it is NOT a legal target for Crackle /
    // Soulfire (which target creatures/players, not artifacts), so it never inflates Hinata's
    // per-target cost reduction. Gated > 0/false so other decks are unaffected.
    bool mana_rock = false;

    // Reflecting Pool: "{T}: Add one mana of any type that a land you control could produce."
    // The `produces` list is IGNORED for a reflecting source; its real colours are computed at
    // runtime as the union of the controller's OTHER non-reflecting lands (EffectiveProduces),
    // nothing if it controls no other land. Gated false -> every other card uses static produces.
    bool reflecting = false;

    // Faeburrow Elder / Bloom Tender: "{T}: For each color among permanents you control, add one
    // mana of that color." A DYNAMIC source like `reflecting`, but (a) the union is over card
    // COLORS of ALL controlled permanents (DomainColors, needs the colors pass), not land
    // produces, and (b) the YIELD is dynamic too -- one mana of EACH such colour per tap (2..5),
    // like a variable Karoo. EffectiveProduces returns DomainColors; AddSourceToPool / the greedy
    // tap_source / the backtracker override the static per-tap amount with the colour count.
    // The static `produces` list is kept (WUBRG) only as the in-hand fixing-heuristic hint.
    bool domain_mana = false;

    // Planeswalkers (Jared Carthalion / Nicol Bolas, Planeswalker / Oko, Thief of Crowns).
    // loyalty_start > 0 marks a planeswalker card (enters with that many loyalty; the dedicated
    // Permanent::loyalty int is the source of truth, mirrored into Counter{Loyalty} for the
    // existing viewer badge). loyalty_abilities lists the once-per-turn choices as {delta,
    // effect, amount}; each is an Action::Kind::ActivateLoyalty plan variant (sorcery-speed,
    // usable the turn the walker enters -- loyalty abilities have no summoning sickness), applied
    // in ApplyLoyaltyAbility (both worlds). A walker at loyalty <= 0 after paying a cost goes to
    // its owner's graveyard there (the only loyalty-loss path vs a passive opponent). Effects are
    // small scripted primitives; per-ability modeling/deferral notes live on the cards.json entry.
    int loyalty_start = 0;
    struct LoyaltyAbilityParam
    {
        int         delta  = 0;   // signed loyalty change, paid as the activation cost (CR 606.5)
        std::string effect;       // kavu_token | counters_up_to_two | regrow_multicolored |
                                  // destroy_own_noncreature | face_damage | food_token | elk_transform
        int         amount = 0;   // effect-specific magnitude (face_damage 7, counters cap 2, ...)
    };
    std::vector<LoyaltyAbilityParam> loyalty_abilities;

    // Unite the Coalition (user-approved collapse 2026-08-06): "Choose five. You may choose the
    // same mode more than once." -> a SEARCHED split S in [0..modal_choose_n]: S picks of "deal
    // modal_damage_per_choice to any target" (collapsed to the opponent face) + (N-S) picks of
    // "target player draws modal_draw_per_choice" (self). The three dead modes (phase out /
    // exile a graveyard / destroy artifact-or-enchantment) are dropped -- provably dead vs this
    // opponent model; disclosed. S rides Action::chosen_x / StackEntry::chosen_x; one CastFromHand
    // variant per S (shared hand_index -> mutually exclusive).
    int modal_choose_n          = 0;
    int modal_damage_per_choice = 0;
    int modal_draw_per_choice   = 0;

    // Garth One-Eye: "{T}: Choose a card name that hasn't been chosen -- Disenchant, Braingeyser,
    // Terror, Shivan Dragon, Regrowth, or Black Lotus. Create a copy of the card with the chosen
    // name. You may cast the copy." Modeled as GarthActivate plan variants (one per un-chosen,
    // affordable, goldfish-live name; the copy is cast AS THE ABILITY RESOLVES per the WotC
    // ruling -- no holding it). Once-per-game-per-name tracking is PER PERMANENT OBJECT
    // (Permanent::garth_chosen_mask). Name order for the mask bits: Disenchant=0, Braingeyser=1,
    // Terror=2, Shivan Dragon=3, Regrowth=4, Black Lotus=5. Disenchant is never enumerated
    // (structurally target-less vs this opponent model -- user-approved stub); Braingeyser's X is
    // auto-maxed from leftover mana at apply (disclosed).
    bool garth_copy_ability = false;

    // Regrowth (Garth conjure): "Return target card from your graveyard to your hand."
    // AUTO-RESOLVED pick = highest mana value (disclosed).
    bool return_target_from_graveyard = false;

    // Terror (Garth conjure): "Destroy target nonartifact, nonblack creature." First hard
    // single-target destroy primitive; vs this opponent model its only targets are opponent
    // spawn creatures (payoff ~0, but faithful and reusable). Pick = largest opponent creature.
    bool destroy_target_creature = false;

    // Equipment (Lightning Greaves). is_equipment marks an attach-to-creature artifact; the attach
    // state is Permanent::equipped_to and re-equipping is Action::Kind::Equip (sorcery-speed, cost
    // equip_cost_generic -- {0} for Greaves; promote to a full ManaCost if a costed Equipment ever
    // arrives). equip_grants_haste is read by CanAttackFull (attacking) and CanTapNow ({T}
    // abilities) -- CR 302.6 lifts one restriction covering both, so both consult the same three
    // haste sources (own keyword / lord / equipment). The former attack-only LIMITATION is FIXED;
    // a Greaves'd fresh mana dork taps this turn, and a Greaves'd fresh Deathrite may use its
    // graveyard-exile modes. equip_grants_shroud is ENFORCED against OUR OWN targeting (the
    // passive opponent never targets us, but shroud also blocks the controller -- CR 702.18b):
    // CreatureHasShroud (SpellEffects.h) gates equip hosts (equip targets, CR 702.6b), Jitte
    // -1/-1 targets, and removal retargets; Balan attach-all / Skyhunter attach-dig do not
    // target and stay legal. MTG_LEGACY_SHROUD=1 restores the old unenforced behavior.
    bool is_equipment       = false;
    int  equip_cost_generic = 0;
    bool equip_grants_haste = false;
    bool equip_grants_shroud = false;
    // KittyEquipment additions -- the first P/T-rider / keyword-grant / restricted equipment.
    // equip_power_bonus/equip_tough_bonus mirror aura_power_bonus/aura_tough_bonus and are summed
    // by EquipBonusFor (SpellEffects.h) at the SAME three attack-power sites as AuraBonusFor
    // (Combat.cpp ResolveCombatDamage; TurnSolver attacking-mana-source scorer +
    // PendingAttackDamage) plus the SBA toughness re-check. equip_grants_lifelink is read by
    // CreatureHasLifelink (equipment scan beside its aura scan). equip_min_power (O-Naginata):
    // legal-attach gate "power 3 or greater" -- enforced at Equip-candidate enumeration AND in
    // ApplyEquip's host check (host's effective power counts already-attached equipment, not the
    // one being placed); the continuous CR 704.5p re-check is deliberately NOT built (approved
    // deferral: this deck cannot reduce its own creatures' power, and the +3 self-sustains).
    // equip_sacrifices_prior_host (Grafted Wargear): per the 2020-11-10 ruling the unattach-
    // sacrifice only DOES something on a genuine re-host to a different creature, so it lives
    // solely in ApplyEquip's re-host branch (host death / equipment removal resolve as no-ops).
    int  equip_power_bonus  = 0;
    int  equip_tough_bonus  = 0;
    bool equip_grants_lifelink = false;
    int  equip_min_power    = 0;
    bool equip_sacrifices_prior_host = false;

    // Umezawa's Jitte: equip_combat_damage_charges counters land on the Jitte each time the
    // equipped creature deals combat damage (collapsed to "to a player" -- approved: nothing
    // blocks; the counter GAIN itself is fully modeled, incl. the double-strike two-event
    // timing). Counters live in Permanent::charge_counters (Aether Vial storage: deep-copied,
    // sim-key-folded, viewer badge). charge_pump_power/_tough = the "+2/+2 until end of turn"
    // mode per counter removed, spent in ResolveCombatDamage via the provider's JitteSpendCount
    // (default greedy spend-all). The -1/-1 and gain-2-life modes are implemented as main-phase
    // actions (user-directed 2026-08-13): charge_minus_power/_tough on a target creature and
    // charge_lifegain for the controller, each costing one counter.
    int  equip_combat_damage_charges = 0;
    int  charge_pump_power  = 0;
    int  charge_pump_tough  = 0;
    int  charge_minus_power = 0;
    int  charge_minus_tough = 0;
    int  charge_lifegain    = 0;

    // Kor Duelist: the CREATURE gains double strike while ANY Equipment is attached to it
    // (checked via HasDoubleStrikeWhileEquipped beside the lord-granted ds sources). Balan:
    // double strike while >= N Equipment attached (double_strike_min_equipment = 2), plus the
    // "{1}{W}: Attach all Equipment you control to Balan" activated ability
    // (attach_all_equipment_cost, a full ManaCost string -- colored; rides Action::cost). The
    // attach-all routes through ApplyEquip per equipment so Grafted Wargear's re-host sacrifice
    // fires identically to a normal Equip.
    bool double_strike_while_equipped = false;
    int  double_strike_min_equipment  = 0;
    std::optional<ManaCost> attach_all_equipment_cost;   // nullopt = no such ability

    // Puresteel Paladin. draw_on_equipment_etb: "Whenever an Equipment you control enters, you
    // may draw a card" -- hooked at the UNIVERSAL enter cascade (FireEtbWatchers) so it fires on
    // cast AND on put-onto-battlefield (Stoneforge / Skyhunter); "may" always taken (draw is
    // strictly good in goldfish; empty-library guard skips -- disclosed 6a).
    // metalcraft_equip_zero_artifacts: "Equipment you control have equip {0} as long as you
    // control three or more artifacts" -- the threshold (3). The dynamic cost is computed by
    // EquipCostGenericNow at BOTH enumeration and every payment site (executor + rollout), so a
    // mid-plan metalcraft flip (cast artifact #3, then equip) stays lockstep.
    bool draw_on_equipment_etb = false;
    int  metalcraft_equip_zero_artifacts = 0;

    // Kemba, Kha Regent: at the controller's upkeep, create upkeep_token_power/toughness tokens
    // -- one per Equipment attached to THIS permanent (counted via Permanent::equipped_to).
    // Rides the existing upkeep_token_* machinery; lockstep GameEngine::UpkeepStep +
    // TurnSolver::SimulateEndAndStartNextTurn. Token color (white) unmodeled -- approved
    // (CreateToken carries no color; nothing reads it).
    bool upkeep_tokens_per_equipment = false;

    // Armored Skyhunter: "Whenever this creature attacks, look at the top six cards of your
    // library. You may put an Aura or Equipment card from among them onto the battlefield. If an
    // Equipment is put onto the battlefield this way, you may attach it to a creature you
    // control. Put the rest ... on the bottom ... in a random order." attack_dig_attach_count =
    // 6. Fired by FireAttackDigAttach at declare-attackers BEFORE damage is read (lockstep
    // GameEngine::CombatPhase + TurnSolver rollout combat), so a put-and-attached Colossus
    // Hammer swings THIS combat. The put bypasses equip cost (attach, not the Equip action) and
    // routes through the shared equipment-enters path (Puresteel draw fires). Aura half of the
    // filter implemented but structurally dead here (deck has zero Auras -- approved deferral);
    // deterministic bottoming + battlefield trigger order (approved conventions).
    int  attack_dig_attach_count = 0;

    // Stoneforge Mystic ability 2: "{1}{W}, {T}: You may put an Equipment card from your hand
    // onto the battlefield." tap_put_from_hand_cost is a full ManaCost string (colored -- the
    // int-generic shortcut deliberately not reused); tap_put_from_hand_types filters the hand
    // card (type or subtype match). Summoning-sick gate via CanTapNow; the put card enters
    // UNATTACHED (the put dodges only the CAST cost -- equipping is still the Equip action).
    std::optional<ManaCost> tap_put_from_hand_cost;      // nullopt = no such ability
    std::vector<std::string> tap_put_from_hand_types;

    // Unexpectedly Absent (removal-template extensions): tuck_to_library resolves the removal
    // as "put into its owner's library just beneath the top X cards" -- a real
    // Library::insert at min(X, size) for OUR OWN permanents; for opponent targets the spawn is
    // a token (CR 111.7) and simply ceases -- same erase Swords uses, X irrelevant (faithful,
    // not a simplification). allow_self_target widens candidates to the controller's own
    // permanents (the Stoneforge-reset line); with targeting "nonland_permanent" own equipment
    // is legal too. Pruned-path X candidates = {0} (higher X provably never better for either
    // use); unpruned/human-play exposes the full range (user-approved lean search 2026-08-13).
    bool tuck_to_library   = false;
    bool allow_self_target = false;

    // Deathrite Shaman ability 1: "{T}: Exile target land card from a graveyard. Add one mana of
    // any color." The {T} mana ability is FUELED by exiling a land card from the controller's OWN
    // graveyard ("a graveyard" collapses to ours -- the passive opponent's is always empty;
    // disclosed). NOT a live mana source while the graveyard holds no land card; each tap exiles
    // one (fungible -- the ability adds any color regardless of which land goes). Gated at the
    // payment usable()/backtracker sites (exact, sequential) and fuel-counted in the pool
    // builders (N Deathrites credit at most #graveyard-lands sources).
    bool gy_land_exile_mana = false;

    // Deathrite Shaman abilities 2/3 (GraveyardExileAbility actions, mutually exclusive with the
    // mana tap via the shared {T}):
    //   "{B}, {T}: Exile target instant or sorcery card from a graveyard. Each opponent loses
    //    2 life."  -> gy_exile_instant_sorcery_drain = 2 (cost fixed {B})
    //   "{G}, {T}: Exile target creature card from a graveyard. You gain 2 life."
    //    -> gy_exile_creature_lifegain = 2 (cost fixed {G})
    // The exiled card is fungible within its type filter (deterministic first-match pick, both
    // worlds).
    int gy_exile_instant_sorcery_drain = 0;
    int gy_exile_creature_lifegain     = 0;

    // Faeburrow Elder: "This creature gets +1/+1 for each color among permanents you control."
    // A characteristic P/T buff on the creature ITSELF (not a lord effect); applied in
    // ComputeLordBonus (back-fills every combat/eval call site) and in the executor's SBA
    // toughness check (base 0/0 must not die on ETB -- it always counts its own G/W).
    bool domain_self_pump = false;

    // Scry-on-resolution for a draw spell (Preordain "Scry 2, then draw a card"). Scry lets you
    // put any of the top N on the BOTTOM and the rest on top: ScryTop keeps the wanted cards on
    // top via DecisionProvider::ScryKeepOnTop and bottoms the rest -- deck filtering. Applied
    // BEFORE the draw in BOTH the executor (ResolveDrawSpell) and the rollout (ApplyPlanDirect
    // DrawSpell branch), so the realised draw matches the searched line. Gated > 0.
    // NB: do NOT use this for Ponder -- Ponder cannot bottom cards (see cast_reorder).
    int cast_scry = 0;

    // Reorder-or-shuffle on resolution for a draw spell (Ponder: "look at the top three cards,
    // then put them back in ANY ORDER. You may shuffle. Draw a card."). Unlike Scry, Ponder
    // CANNOT send cards to the bottom -- it is all-or-nothing: either keep all N on top in your
    // best order (you then draw the best, but the rest stay on top and you are stuck drawing
    // them), or shuffle them all away and draw fresh. Modelled by ReorderTopOrShuffle: the
    // provider's ScryKeepOnTop decides keep-vs-shuffle (keep when at least one of the top N is
    // wanted, ordering wanted-first; shuffle the whole library when none are). The shuffle is
    // the same DETERMINISTIC, lockstep ShuffleAfterSearch used for fetch/tutor (CR "you may
    // shuffle"). Applied BEFORE the draw in both executor and rollout. Gated > 0.
    int cast_reorder = 0;

    // X-damage scaling for an {X} DirectDamage spell (Crackle with Power "deals five times X
    // damage" -> 5). The damage dealt per target is chosen_x * x_damage_multiplier; combined with
    // x_pips on the cost, Crackle = {X}{X}{X}{R}{R}, 5X to each target. Default 1 (plain {X} burn
    // like a hypothetical X-bolt). Applied in BOTH the search's X-enumeration (direct_damage/eval)
    // and ResolveDirectDamage. Gated (defaults to 1) so a plain X burn is unchanged.
    int x_damage_multiplier = 1;

    // "deals N damage divided as you choose among any number of targets" (Fiery Justice, Magma
    // Opus): the total `damage` is SPLIT across the chosen targets, not dealt in full to each.
    // Autonomously the model points all of it at the opponent face (optimal vs a passive opponent),
    // so this is byte-identical there; under --claude-play the player allocates per-target amounts
    // (the human-play target chooser enumerates allocations, heuristic-defaulted to all-to-face).
    bool damage_divided = false;

    // Goldfish-inert: the card has no productive use against a single passive opponent that
    // never casts spells, attacks, or blocks (counterspells with no spell to counter; "tap X
    // target creatures" / "return X target permanents" with no useful target). CollectActions
    // never offers it, so it is a faithful dead draw in hand. This is the allowed "provably
    // changes nothing for goldfishing" simplification (a counter/tap/bounce vs a do-nothing
    // opponent collapses to a do-nothing card). Default false; gated so other decks unaffected.
    bool goldfish_inert = false;

    // Hinata, Dawn-Crowned's static: "Spells you cast cost {1} less to cast for each target."
    // hinata_cost_reducer marks the permanent that grants the reduction (on Hinata herself).
    // discount_targets is the per-spell target count used for the reduction (Hinata's spells
    // each subtract this many from their GENERIC cost, floored at 0); discount_targets_scale_x
    // makes the target count grow with the chosen X (Reality Spasm "untap X target permanents" ->
    // X targets, so a Hinata in play cancels its entire {X}, the ritual). The reduction is
    // applied at every cast-cost finalization site (both EffectiveCost copies for fixed costs +
    // the three X-cost payers), via HinataGenericDiscount, so planner/rollout/executor agree.
    bool hinata_cost_reducer       = false;
    // Board-aware multi-target discount inputs (Layer 2b). The discount = min(cap, available
    // beneficial targets on the board), where cap = discount_max_targets (or the chosen X when
    // discount_targets_scale_x). available = the opponent + your creatures + yourself (if
    // discount_self_safe -- the spell's per-target effect isn't self-lethal: Magma 1dmg / Soulfire
    // small MV = safe; Crackle 5X = NOT) + (every permanent, incl. the opponent's lands, if
    // discount_targets_permanents -- the spell taps/untaps/targets permanents: Magma's "tap two",
    // Reality Spasm's "untap X"). Computed in HinataAvailableTargets / HinataGenericDiscount.
    int  discount_max_targets      = 0;    // fixed cap (Magma 6 = 4 damage + 2 tap; Soulfire large)
    bool discount_targets_scale_x  = false; // cap = chosen X (Crackle up to X, Reality Spasm X)
    bool discount_self_safe        = false; // count yourself as a target (per-target effect non-lethal)
    bool discount_targets_permanents = false; // spell can target permanents (count all, incl. opp lands)

    // Karoo "bounce land" (Izzet Boilerworks: "This land enters tapped. When this land enters,
    // return a land you control to its owner's hand. {T}: Add {U}{R}"). On ETB, return one of
    // the controller's OTHER lands to hand (preferring a tapped one -> no mana lost this turn),
    // modelled by BounceKarooLand at every land-play ETB site. Combined with enters_tapped +
    // produces[U,R] + produces_amount 2. The bounce is the real tempo cost (a future land drop
    // re-plays the returned land) and is modelled so the deck's mana is not over-rated. Gated.
    bool etb_bounce_land = false;

    // Soulfire Eruption (partial): "for each target, exile the top card of your library, then
    // deal damage equal to that card's mana value." Modelled (single target = the opponent's
    // face) as DirectDamage whose damage = the top library card's mana value, exiling that card.
    // The multi-target + "you may play the exiled cards" DIG (Soulfire's real value -- targeting
    // yourself/creatures to draw into the combo) is the deferred Layer-2 item; this captures the
    // clairvoyant face-damage faithfully and under-rates the dig. Gated false.
    bool damage_equals_top_mv = false;

    // Reality Spasm (partial): "Untap X target permanents." On resolution, untap X of the
    // caster's tapped mana sources. With Hinata in play discount_targets_scale_x cancels the
    // {X}, the intended ritual. NOTE the static-pool planner does not yet CHAIN the freed mana
    // into a same-turn cast (the Reality Spasm -> Crackle combo), so in the current model this
    // is rarely productive -- the deferred Layer-2 HinataProvider search item. Gated false.
    bool untap_x_mana_sources = false;
    // Irencrag Feat: a fixed mana-burst RITUAL -- on resolution, add this much mana to the
    // turn-scoped reserve (state.floating_mana) for a same-turn payoff. 0 for non-ritual cards.
    int  ritual_floating_mana = 0;
    // Colour the ritual_floating_mana burst is added in ("R"/"U"/"B"/"G"/"W"/"C"). Empty = WILD
    // (the legacy default -- Irencrag Feat / Reality Spasm, which stay wild to preserve Hinata's
    // byte-identity). The Dragonstorm rituals set "R" so the burst is real RED mana that cannot
    // pay the off-colour {B}/{G} pips of dragons that only arrive via Dragonstorm.
    std::string ritual_float_color;
    // Rite of Flame: "then add {R} for each card named Rite of Flame in each graveyard." When true,
    // the resolution float = ritual_floating_mana + (count of cards with THIS card's name across all
    // graveyards at resolution) -- so a same-turn chain escalates (2,3,4,... as each prior copy hits
    // the graveyard). The planner credits the same escalation via a triangular term in Solve::consider.
    bool ritual_float_gy_self_bonus = false;
    // Desperate Ritual: "Splice onto Arcane {1}{R}." When you cast ONE copy you may reveal + splice k
    // OTHER copies from hand onto it, paying this card's splice cost ({1}{R}, == its own printed cost)
    // per spliced copy and adding their ritual float too -- the spliced copies STAY IN HAND (not cast,
    // not exiled), so they can be spliced again onto another base or hard-cast later the same/a future
    // turn. A search-chosen splice_count k (0..#other splice copies in hand) rides on the Action /
    // StackEntry; every cast path prices the COST as base + k*splice_cost and the FLOAT as (k+1) times
    // the per-copy effect, in lockstep (EffectiveCost / RitualFloatAmount). False = not a splice card
    // -> every non-splice deck is byte-identical.
    bool splice_onto_arcane = false;
    // Splice cost paid PER spliced copy (CR 702.47: an additional cost added to the base spell as it is
    // cast). UNSET defaults to the card's own printed mana cost -- exact for Desperate Ritual, whose
    // splice cost {1}{R} equals its {1}{R} mana cost, so EffectiveCost stays byte-identical. Set this
    // explicitly for any future splice card whose splice cost DIFFERS from its mana cost (the general
    // case, e.g. Glacial Ray, Kodama's Reach) so the (k+1)-copy price is correct rather than assuming
    // splice cost == cast cost.
    std::optional<ManaCost> splice_cost;

    // --- Lotus Bloom (Suspend 3-{0}; {T},Sacrifice: add 3 mana of ONE chosen color) ---
    // Suspend: the number of time counters the card is exiled with by the {0} suspend action (3 for
    // Lotus Bloom). > 0 gates the WHOLE suspend mechanic (a {0} main-phase action moves the card from
    // hand to Player::suspended_cards with arrive_turn = turn + this; at that upkeep the last time
    // counter is removed and the card is cast off suspend into play). 0 = not suspendable -> every
    // other deck is byte-identical.
    int suspend_time_counters = 0;
    // Sac-for-mana: "{T}, Sacrifice this artifact: Add N mana of any one color." N (3 for Lotus). > 0
    // gates a battlefield-activated ability enumerated as an Action::Kind::SacForMana (one variant per
    // candidate colour): it taps + SACRIFICES the permanent and floats N mana of a SEARCH-CHOSEN single
    // colour into state.floating_mana.<colour> (NOT wild -- wild could illegally pay a multicolour mix).
    // The chosen colour rides on Action::chosen_float_color and resolves via AddChosenColorFloat (the
    // reusable colour-float dimension Apex of Power's "add ten mana of one colour" shares). 0 = not a
    // sac-for-mana source -> byte-identical for every other deck.
    int sac_for_mana_amount = 0;

    // Colour PIN for the sac-for-mana ability above. Empty (the historical default) = "one mana of
    // any colour" (Black Lotus / Lotus Bloom / Treasure), which enumerates a colour fan and is
    // credited as WILD. A non-empty letter means the ability adds THAT mana only -- "C" for an
    // Eldrazi Spawn's "Sacrifice this creature: Add {C}", which pays generic costs but no coloured
    // pip. It collapses the colour fan to one action, pins Action::chosen_float_color at
    // enumeration, and narrows EffectiveProduces on the pay-sac path so the payment solver never
    // spends the Spawn on a {R} or {G} pip. Modelling it as any-colour would materially FLATTER a
    // Spawn-making card in a two-colour deck, which is exactly the comparison this pin protects.
    std::string sac_for_mana_color;

    // --- Apex of Power ({7}{R}{R}{R} Sorcery: impulse-exile-7 + conditional 10-of-one-colour float) ---
    // "Exile the top seven cards of your library. Until end of turn, you may cast spells from among
    // them." impulse_exile = the number of top cards exiled as STAGED cards (7 for Apex). > 0 gates the
    // whole Apex mechanic (resolution in EffectHandler custom-else + TurnSolver::apply_one custom branch,
    // lockstep). The staged cards are marked m_impulse_no_land so their LANDS are non-playable ("cast
    // SPELLS" only). 0 = not an impulse-exile card -> byte-identical for every other deck.
    int  impulse_exile = 0;
    // Staged exile expires at turn_number (THIS turn only), like Expressive Iteration -- vs
    // turn_number+1 for Light Up the Stage. true for Apex ("Until end of turn").
    bool impulse_expiry_this_turn = false;
    // "If this spell was cast from your hand, add ten mana of any one color." N mana of ONE search-chosen
    // colour (Apex = 10) floated on resolution into state.floating_mana.<colour> (the reusable
    // AddChosenColorFloat dimension Lotus Bloom shares) -- ONLY when cast FROM HAND (StackEntry.cast_from_hand
    // == !m_is_staged; withheld for an Apex cast off another Apex's staged exile). NOT wild (a multicolour
    // float could illegally pay off-colour pips). The colour rides on Action::chosen_float_color (one cast
    // variant per candidate colour, opened to all five under MTG_UNPRUNED(SacColor)). 0 = no float.
    int  impulse_float_amount = 0;

    // Irencrag Feat: "You can cast only one more spell this turn." After this spell resolves, the
    // controller may cast at most this many MORE spells this turn. -1 = no restriction (default).
    // Enforced in Solve::consider (reject a subset with > this many spells ordered AFTER it by
    // CastOrderRank); the provider ranks the restricting ritual as the LAST ritual (just before the
    // payoff) so the only legal shape is ...setup -> Irencrag -> Crackle.
    int  max_casts_after = -1;
    // Forbidden Orchard: "whenever you tap this land for mana, target opponent creates a 1/1 Spirit."
    // Modelled as one opponent 1/1 colourless Spirit per turn the land is in play (assume tapped each
    // turn) -- created at turn start for existing copies + on-play for a freshly-played copy. The
    // Spirits are real opponent creatures -> first-class Soulfire/Crackle/removal targets.
    bool taps_spawn_opp_token = false;

    // ================= Creature Giving (gift-the-opponent drain) =================
    // The deck gives the opponent creatures (Forbidden Orchard above, Hunted Phantasm,
    // Varchild's War-Riders) and drains for each enter (Suture Priest) / death (Massacre
    // Wurm). Every param is 0/false by default -> byte-identical for every other deck.
    //
    // ETB opponent-token gift (Hunted Phantasm: "When this enters, target opponent creates
    // five 1/1 red Goblin creature tokens"). Uses the shared etb_created_token_* spec (a card
    // sets exactly one ETB-token gate, same reuse rule as Lathliss/Mogg). The tokens are real
    // opponent creatures -> they fire the enter-watchers below and feed DotH / Massacre Wurm.
    int  etb_opp_creates_tokens = 0;
    // Enter-watchers ("whenever a creature enters ..."), fired for every creature entering on
    // EITHER side via FireCreatureEnterWatchers (called from the universal enter cascade +
    // the opponent-spawn sites, lockstep executor + rollout):
    //   any_creature_enters_lifegain   -- "another creature enters, you gain N" (Soul Warden,
    //                                     Essence Warden: any controller, excludes itself).
    //   own_creature_enters_lifegain   -- "another creature YOU control enters, gain N"
    //                                     (Suture Priest clause 1; "you may" always taken).
    //   opp_creature_enters_life_loss  -- "a creature an OPPONENT controls enters, that player
    //                                     loses N" (Suture Priest clause 2 -- the drain engine).
    int  any_creature_enters_lifegain  = 0;
    int  own_creature_enters_lifegain  = 0;
    int  opp_creature_enters_life_loss = 0;
    // Massacre Wurm ETB: "creatures your opponents control get -2/-2 until end of turn",
    // collapsed to "destroy each opponent creature with toughness - damage <= N at ETB"
    // (equivalent in goldfish: opp creatures never block/attack/get buffs, and the power
    // reduction on survivors is inert). Each kill fires FireOppCreatureDies.
    int  etb_opp_creatures_debuff = 0;
    // Massacre Wurm clause 2: "whenever a creature an opponent controls dies, that player
    // loses N". Summed over all watchers at every opponent-creature death site.
    int  opp_dies_life_loss = 0;
    // Varchild's War-Riders cumulative upkeep ("Have an opponent create a 1/1 red Survivor
    // token" per age counter). Modelled as ALWAYS PAID (weakly dominant vs the passive
    // opponent: the tokens only feed our drains/DotH and the body is kept): at each of the
    // controller's upkeeps, +1 Permanent::age_counters, then the OPPONENT creates
    // age_counters tokens using the shared upkeep_token_* spec. Pay-vs-sacrifice is a
    // disclosed auto-decision, not a surfaced choice.
    bool cumulative_upkeep_opp_token = false;
    // Defense of the Heart: "At the beginning of your upkeep, if an opponent controls three
    // or more creatures, sacrifice this enchantment, search your library for up to two
    // creature cards, put those cards onto the battlefield, then shuffle."
    //   upkeep_sac_tutor_creatures -- max creature cards put (2); 0 = not this mechanic.
    //   upkeep_sac_tutor_opp_min   -- the intervening-if threshold (3).
    // WHICH creatures (and their enter ORDER) = DecisionProvider::SacTutorPutList (default:
    // closed-form immediate-drain maximisation, token-makers enter before sweepers), with a
    // human-play chooser override.
    int  upkeep_sac_tutor_creatures = 0;
    int  upkeep_sac_tutor_opp_min   = 0;
    // Crop Rotation: "search your library for a land card, put it onto the battlefield"
    // (with the existing sacrifice_land additional cost). Resolved by
    // PerformLandTutorToBattlefield through EnterLand (the fetched land resolves its own
    // shock/enters-tapped choice; a fetched Forbidden Orchard spawns its Spirit on entry,
    // mirroring the on-play hook). Target = the searched tutor axis (tutor_types [Land]).
    bool tutor_land_to_battlefield = false;

    // Expressive Iteration: "look at the top 3; 1 to hand, 1 to bottom, 1 exiled & playable THIS
    // turn." Resolved by ResolveExpressiveIteration (NOT the normal draw/scry path). A DrawSpell so
    // the existing draw-breakpoint re-solve lets the staged (this-turn-only) card be played.
    bool expressive_iteration = false;
    // Magma Opus: a non-draw spell that ALSO draws on resolution ("draw two cards"). Drawn to
    // hand in both cast paths (executor + rollout) -- deterministic, lockstep. 0 = no draw rider.
    int  cast_draw = 0;

    // --- Auras (attach-to-creature enchantments; Bogles / hexproof-auras deck) ---
    // An Aura is an Enchantment that, when cast, targets a creature and enters attached to it,
    // granting the enchanted creature a P/T bonus and/or keyword. is_aura routes resolution to the
    // attach path (EffectHandler / TurnSolver apply_one), and its grant is applied to the enchanted
    // creature at every combat site via AuraBonusFor (SpellEffects.h). The enchant TARGET is a SEARCH
    // decision (Action/StackEntry::enchant_target, one plan variant per legal creature), because which
    // creature carries the auras changes the clock (summoning sickness + a Kor Spiritdancer's per-aura
    // self-buff). false => not an aura (every other deck byte-identical).
    bool is_aura = false;
    int  aura_power_bonus = 0;   // flat +P granted to the enchanted creature (Rancor +2, Daybreak +3)
    int  aura_tough_bonus = 0;   // flat +T (stored but currently INERT vs the passive goldfish opponent
                                 // -- nothing damages our creatures; kept for future life-total fidelity)
    // Lifelink grant (MODELED, per user 2026-07-22: life-total decks are coming). When true the enchanted
    // creature has lifelink -- combat damage it deals also gains its controller that much life (applied at
    // the combat sites). Daybreak Coronet, Armadillo Cloak ("deals damage -> gain that much life"), Spirit
    // Link. Every OTHER aura keyword (trample/flying/first strike/vigilance/reach/hexproof/protection/umbra
    // armor) is provably INERT vs the passive opponent and is NOT modeled (disclosed Stage 6a).
    bool aura_grants_lifelink = false;
    // Dynamic P/T scaling: the aura's grant grows with a board count. aura_scale_kind selects WHICH count
    // multiplies aura_scale_power/tough:
    //   "enchantments"            +N per enchantment you control, INCLUDING this aura (Ethereal Armor: 1/1)
    //   "other_enchantments"      +N per OTHER enchantment on the battlefield, any controller (Ancestral
    //                             Mask: 2/2; goldfish opp controls none -> = other enchantments you control)
    //   "artifacts_enchantments"  +N per artifact and/or enchantment you control, incl. this aura (All That
    //                             Glitters: 1/1)
    // Empty => flat aura (no scaling). Computed in AuraBonusFor / CountAuraScaleUnits.
    std::string aura_scale_kind;
    int  aura_scale_power = 0;   // per-unit +P for the scaling term
    int  aura_scale_tough = 0;   // per-unit +T for the scaling term
    // Casting restriction on WHICH creature may be enchanted (CR 601.2c legality, enforced in
    // LegalEnchantTargets so an aura with no legal target is uncastable and stays in hand):
    //   "another_aura"  Daybreak Coronet: the creature must ALREADY have another Aura attached
    //   "modified"      Lion Umbra: the creature must be MODIFIED (has an Aura you control, Equipment,
    //                   or a +1/+1 counter). No equipment in this deck; = has an aura or a +1/+1 counter.
    // Empty => any creature you control is a legal target.
    std::string aura_enchant_requires;

    // --- Kor Spiritdancer ---
    // "This creature gets +N/+N for each Aura attached to it." Applied to THIS permanent in AuraBonusFor
    // (per aura whose aura_attached_to == this creature's m_number). Kor: 2/2. 0 => no self-buff.
    int  aura_self_buff_power = 0;
    int  aura_self_buff_tough = 0;
    // "Whenever you cast an Aura spell, you may draw a card." Fired in FireOnCastTriggers when the cast
    // card is_aura, once per such permanent controlled. Drawn to hand (deterministic top-of-library,
    // lockstep in executor + rollout); NO same-turn re-solve (the drawn card is a resource for later
    // turns -- conservative, avoids an fd-diverge re-solve; disclosed 6a). false => no trigger.
    bool draw_on_aura_cast = false;

    // --- Light-Paws, Emperor's Voice ---
    // "Whenever an Aura you control enters, if you cast it, search your library for an Aura card with mana
    // value <= that Aura and with a different name than each Aura you control, put it onto the battlefield
    // attached to Light-Paws, then shuffle." Fired when an Aura YOU CAST resolves (not for the auras
    // Light-Paws itself puts into play -- the "if you cast it" intervening-if bounds the chain to one fetch
    // per cast aura). The fetched aura enters attached to THIS permanent. WHICH aura is a heuristic pick
    // (PerformLightPawsAttach: max power contribution; disclosed 6a as a heuristic-narrowed target, like a
    // cascade target) -- a future search-decision candidate. false => no trigger.
    bool aura_cast_tutor_attach = false;

    // ================= Goblins tribal (mono-red aggro/sacrifice) =================
    // Every field below is gated (0/false/empty = inert) so decks that don't run Goblins
    // stay byte-identical. See docs/design/analysis-goblins.md for the per-card mapping.

    // Subtype cost reducer (Goblin Warchief: "Goblin spells you cast cost {1} less"). A
    // permanent you control whose reduces_spell_subtype matches a SUBTYPE of the spell being
    // cast reduces that spell's GENERIC cost by 1, floored at 0, stacking per copy. The subtype
    // twin of reduces_spell_color (which keys on colour pips); matching scans def.card.m_subtypes
    // like affinity_for_subtype. Empty = not a reducer.
    std::string reduces_spell_subtype;

    // How much GENERIC one matching reducer takes off (Goblin Warchief 1; Dragonspeaker Shaman
    // and Urza's Incubator both read "cost {2} less to cast" -> 2). Still floored at 0 and still
    // stacks per copy, so this only scales the per-reducer step. Defaults to 1 = the shipped
    // Warchief behaviour, so every existing deck is byte-identical.
    int reduces_spell_subtype_amount = 1;

    // Urza's Incubator: "CREATURE spells of the chosen type cost {2} less" -- the reduction
    // applies ONLY to creature spells. Dragonspeaker Shaman ("Dragon SPELLS you cast") leaves
    // this false and discounts any spell carrying the subtype. Inert for Warchief (false).
    //
    bool reduces_spell_subtype_creature_only = false;

    // "As this permanent enters, choose a creature type" (Urza's Incubator). When true the reducer's
    // subtype is NOT the printed reduces_spell_subtype (which stays empty) but the type chosen at
    // ETB and carried on Permanent::chosen_subtype_id -- so the card is generic and works in ANY
    // tribal deck (Dragons picks Dragon, a Slivers deck picks Sliver) instead of being hard-coded.
    // The choice is made by the shared DominantCreatureSubtypeId: the creature subtype with the most
    // cards across the controller's zones, i.e. what the deck is actually built around. false =
    // a printed-subtype reducer (Goblin Warchief / Dragonspeaker Shaman) -> byte-identical.
    bool chooses_creature_type = false;

    // ETB fixed-token creation ("When THIS creature enters, create N 1/1 red Goblin tokens" —
    // Mogg War Marshal 1, Siege-Gang Commander 3). > 0 fires at THIS permanent's own ETB, using
    // the shared etb_created_token_power/toughness/subtypes spec (Lathliss). A card sets exactly
    // one ETB-token gate, so reusing that spec never conflicts with etb_other_subtype_creates_tokens.
    int etb_self_creates_tokens = 0;

    // ETB single-target burn ("When THIS enters, it deals N damage to any target" — Twinshot
    // Sniper 2). Collapses to the opponent's face vs a passive goldfish (optimal). > 0 gates it.
    int etb_damage_any = 0;
    // ETB "each opponent" ping ("deals N damage to each opponent and each creature/planeswalker
    // they control" — Goblin Chainwhirler 1). N to the opponent face (race-relevant, via the
    // OpponentGainsLife life-loss path so the win projection sees it) AND N to each permanent the
    // opponent controls (only nonzero when the opponent has spawn tokens). > 0 gates it.
    int etb_damage_each_opponent = 0;

    // ETB reveal-and-cheat (Muxus, Goblin Grandee: "reveal the top six cards; put all Goblin
    // creature cards with mana value 5 or less onto the battlefield and the rest on the bottom").
    // etb_reveal_count > 0 gates it: reveal the top N, put every revealed card matching
    // etb_reveal_put_subtypes (and, if etb_reveal_put_creatures_only, a Creature) with mana value
    // <= etb_reveal_put_max_mv onto the battlefield through the shared enter cascade (each put
    // creature's own ETB fires); the rest go to the library bottom (deterministic order — the
    // printed "random order" is unobservable in goldfishing, same rationale as etb_dig).
    int                      etb_reveal_count = 0;
    std::vector<std::string> etb_reveal_put_subtypes;
    bool                     etb_reveal_put_creatures_only = false;
    int                      etb_reveal_put_max_mv = 0;

    // Attack self-pump, base = other ATTACKING matching creatures (Goblin Piledriver: "Whenever
    // this attacks, it gets +2/+0 until end of turn for each other attacking Goblin"). > 0 gates
    // it; +power per OTHER declared attacker whose subtype is in subtypes_affected (self-excluded),
    // applied at declare-attackers in both worlds (mirrors attack_trigger_life_loss's scan).
    int attack_pump_power_per_other_matching = 0;
    // Attack self-pump, base = other CONTROLLED matching permanents (Muxus: "Whenever Muxus
    // attacks, it gets +1/+1 until end of turn for each other Goblin you control"). Non-empty
    // subtype gates it; +power/+tough per OTHER permanent you control whose subtype matches.
    std::string attack_self_pump_per_other_subtype;
    int         attack_self_pump_power = 0;
    int         attack_self_pump_tough = 0;

    // Permanent death-watcher ("Whenever [this or] another <subtype> you control dies, ...").
    // Fired from the SBA creature-death site (executor) and the rollout death path (lockstep) for
    // every creature the controller controls that dies while this permanent is in play. Gates:
    //   dies_watch_subtype  -- subtype the dead creature must have (empty = watch ONLY this
    //                          permanent's own death, e.g. Mogg War Marshal's self-death token).
    //   dies_watch_includes_self -- also fire on this permanent's own death (Pashalik/Rundvelt/
    //                          Mogg = true). With an empty subtype this must be true to fire.
    // Effects per matching death (any combination):
    //   dies_trigger_damage           -- deal N to any target -> opponent face (Pashalik 1).
    //   dies_trigger_creates_tokens   -- create N tokens (Rundvelt 1, Mogg 1) with dies_token_*.
    //   dies_trigger_impulse_exile    -- exile the top library card; if it matches
    //                          dies_impulse_requires_type/subtype, it is playable until end of
    //                          this turn (dies_impulse_expiry_next_turn=false) or end of the
    //                          controller's NEXT turn (true — Rundvelt) via the staged-exile zone.
    // These use the dies_* prefix to avoid colliding with death_trigger_damage (Searing Blood,
    // a spell-target-death rider, unrelated).
    std::string              dies_watch_subtype;
    bool                     dies_watch_includes_self = false;
    int                      dies_trigger_damage = 0;
    int                      dies_trigger_creates_tokens = 0;
    int                      dies_token_power = 0;
    int                      dies_token_toughness = 0;
    std::vector<std::string> dies_token_subtypes;
    bool                     dies_trigger_impulse_exile = false;
    std::string              dies_impulse_requires_type;     // "Creature"
    std::string              dies_impulse_requires_subtype;  // "Goblin"
    bool                     dies_impulse_expiry_next_turn = false;

    // Sacrifice-a-creature activated outlets. A permanent you control offers an activated ability
    // whose cost is {sac_creature_cost} mana + Sacrifice one creature you control whose subtype is
    // sac_creature_requires_subtype (self-inclusive). Distinct from sac_for_mana_amount (Lotus:
    // tap + sac SELF for mana). Enumerated as new Action kinds; each activation sacrifices one
    // chosen creature (firing its own dies-watchers) and applies exactly one payload:
    //   sac_outlet_add_mana_color  -- MANA ability (no stack): float this colour (Skirk Prospector
    //                                 "Sacrifice a Goblin: Add {R}"; sac_creature_cost empty).
    //   sac_outlet_damage          -- deal N to any target -> face (Siege-Gang "{1}{R}, Sacrifice a
    //                                 Goblin: 2 damage to any target").
    //   sac_outlet_creates_tokens  -- create N tokens with sac_outlet_token_* (Pashalik "{3}{R},
    //                                 Sacrifice a Goblin: create two 1/1 Goblins").
    bool                     sac_creature_outlet = false;
    std::optional<ManaCost>  sac_creature_cost;
    std::string              sac_creature_requires_subtype;
    std::string              sac_outlet_add_mana_color;   // "R" for Skirk (empty = not a mana outlet)
    int                      sac_outlet_add_mana_amount = 0;
    int                      sac_outlet_damage = 0;
    int                      sac_outlet_creates_tokens = 0;
    int                      sac_outlet_token_power = 0;
    int                      sac_outlet_token_toughness = 0;
    std::vector<std::string> sac_outlet_token_subtypes;

    // Krenko, Mob Boss: "{T}: Create X 1/1 red Goblin tokens, where X = the number of Goblins you
    // control." Non-empty subtype gates a {T}-activated (no mana) ability creating N tokens where
    // N = permanents you control whose subtype matches (counted at resolution, incl. self+tokens).
    // Summoning sickness applies (Krenko is a creature — the tap respects CanTap()).
    std::string              tap_creates_tokens_per_controlled_subtype;
    int                      tap_created_token_power = 0;
    int                      tap_created_token_toughness = 0;
    std::vector<std::string> tap_created_token_subtypes;

    // Channel (Twinshot Sniper: "Channel — {1}{R}, Discard this card: it deals 2 damage to any
    // target"). A from-HAND activated ability (not a battlefield permanent): pay channel_cost +
    // discard this card from hand -> deal channel_damage to the opponent face. Enumerated as a
    // hand action when the card is in hand. Empty cost / 0 damage = inert.
    std::optional<ManaCost>  channel_cost;
    int                      channel_damage = 0;

    // Echo (Mogg War Marshal {1}{R}, Stingscourger {3}{R}): "At the beginning of your upkeep, if
    // this came under your control since your last upkeep, sacrifice it unless you pay its echo
    // cost." Modelled as an upkeep pay-echo_cost-or-sacrifice decision. The pay-or-not is a real
    // choice (Mogg's death token means NOT paying keeps a body while saving mana). Empty = no echo.
    std::optional<ManaCost>  echo_cost;

    // Goblin Lackey: "Whenever this creature deals damage to a player, you may put a Goblin
    // permanent card from your hand onto the battlefield." Non-empty gates a combat-damage-to-
    // player trigger: after this creature deals combat damage, optionally put a hand card whose
    // subtype is in this list onto the battlefield (shared enter cascade). A "resource generated
    // during combat" -> DeckUsesSecondMain returns true for a deck running it (2c-bis), enabling
    // the post-combat second main so the cheated creature's follow-up plays are evaluated. WHICH
    // card is a search/viewer decision (bucket B).
    std::vector<std::string> combat_damage_puts_subtype_from_hand;

    // Three Tree City: "{2}, {T}: Choose a color. Add an amount of mana of that color equal to the
    // number of creatures you control of the chosen type." Non-empty subtype gates a {T}-activated
    // mana ability with a mana_per_creature_feeder generic cost, yielding N mana of a search-chosen
    // colour where N = creatures you control whose subtype matches. The chosen creature TYPE is
    // simplified to "any creature" (Cavern of Souls precedent — single-tribe decks). The basic
    // "{T}: Add {C}" lives in `produces`. Empty = not a scaled-mana land.
    std::string              mana_per_creature_subtype;
    int                      mana_per_creature_feeder_generic = 0;   // {2}

    // --- Zada / Mirrorwing spell-copy swarm (Mirrorwing Dragon deck) -----------------------------
    // Copy magnet (Zada, Hedron Grinder / Mirrorwing Dragon): "Whenever you cast an instant or
    // sorcery spell that targets only [this], copy that spell for each other creature you control
    // that the spell could target. Each copy targets a different one of those creatures." When a
    // solo_target_trick's ONLY target is a creature with this flag, ResolveSoloTargetTrick fans the
    // payload out: one copy per OTHER own creature, the original on the magnet. Copies resolve
    // BEFORE the original (they go on the stack above it, CR 601.2 order chosen by the controller);
    // the deterministic order used -- non-attack-eligible creatures first, then eligible, original
    // (the magnet) last -- is the goldfish-optimal choice for escalating payloads (Fists of Flame's
    // drawn-count, Gold Rush's treasure count grow with resolution position) and is disclosed in
    // the deck's Stage-6a report. Mirrorwing's "a player casts" collapses to our own casts (the
    // passive opponent never casts -- inert, bracket-noted on the entry).
    bool copies_solo_targeted_spells = false;

    // Solo-target trick (Expedite, Fists of Flame, Ancestral Anger, Gold Rush, Scale the Heights,
    // Twinflame): an instant/sorcery that targets ONE own creature. WHICH creature is a SEARCH
    // decision -- CollectActions emits one CastFromHand variant per own creature, carried on the
    // (widened) Action/StackEntry::enchant_target int (the aura-target precedent), so the search --
    // never a heuristic -- decides whether to point it at a copy magnet. Resolution runs the
    // payload params below once per recipient via ResolveSoloTargetTrick (shared executor/rollout).
    bool solo_target_trick = false;
    // "up to one target" (Gold Rush, Scale the Heights): also emit a NO-target variant
    // (enchant_target 0) -- the untargeted payloads (treasure/life/land-drop/draw) still resolve,
    // and an untargeted cast never triggers a copy magnet ("targets only" requires a target).
    bool trick_up_to_one = false;
    // "target creature YOU CONTROL" (Oracle's Restoration, Twinflame) vs the bare "target creature"
    // every other solo_target_trick prints. The distinction only bites in ONE place -- the
    // no-own-target opponent-target fallback in CollectActions -- which exists so a rider-carrying
    // trick can still cash its cantrip/Treasure/life on an empty board by pointing the inert pump
    // at the opponent's creature. That is legal for "target creature" and ILLEGAL here, and the
    // difference is not cosmetic: it invents a castable mode the real card does not have (a {G}
    // "draw a card, gain 1 life" with no board requirement), which OVERVALUES the card wherever it
    // is measured -- in a deck screen, every arm holding more copies gets more of the phantom mode.
    // Resolution was never wrong (ResolveSoloTargetTrick has always filtered to own creatures), so
    // the mana was simply paid for a pump that did nothing; only the ENUMERATION offered the line.
    //
    // Set from card data, NOT derived from oracle_text: the engine never reads oracle_text (it is
    // stripped from the canonical form in CardDatabase.cpp). Drift is caught instead by
    // audit_card_fields.py, which cross-checks this flag against the authoritative Scryfall text.
    bool trick_own_target_only = false;
    // Fists of Flame: "+N/+0 for each card you've drawn this turn", computed AT RESOLUTION per
    // copy, after that copy's own cast_draw (draw first, then count -- oracle order), off
    // Player::cards_drawn_this_turn. 0 = no drawn-count pump.
    int  pump_per_cards_drawn_power = 0;
    // Ancestral Anger: "+X/+0 where X is 1 plus the number of cards named [this] in your
    // graveyard" -> power bonus += this * (1 + graveyard copies of the card itself).
    int  gy_self_power_bonus = 0;
    // Gold Rush: "+N/+N for each Treasure you control", counted AT RESOLUTION per copy (after that
    // copy's own creates_treasures), so stacked copies escalate faithfully.
    int  pump_per_treasure_power = 0;
    int  pump_per_treasure_tough = 0;
    // Gold Rush: "Create a Treasure token." -> N "Treasure Token" permanents (existing cards.json
    // token def; its sac-for-mana machinery prices/spends them).
    int  creates_treasures = 0;
    // Expedite: "target creature gains haste until end of turn" -> Permanent::temp_haste (read by
    // CanAttackFull / CanTapNow -- a hasted fresh dork may tap for mana; reset each cleanup).
    bool grants_temp_haste = false;
    // Scale the Heights: "Put a +1/+1 counter on up to one target creature."
    int  counters_on_target = 0;
    // Scale the Heights: "You gain 2 life." (per resolved copy -- faithful escalation of life.)
    int  cast_lifegain = 0;
    // Fortifying Draught: "+X/+X where X is the amount of life you gained this turn", computed AT
    // RESOLUTION per copy AFTER that copy's own cast_lifegain (gain first, THEN count -- oracle
    // order), off Player::life_gained_this_turn. So a magnet fan-out escalates: each copy gains its
    // own 2 life before sizing its pump, and the count also picks up the turn's OTHER gains
    // (Kazandu Refuge's ETB, Scale the Heights). 0 = no lifegain-count pump.
    int  pump_per_life_gained_power = 0;
    int  pump_per_life_gained_tough = 0;
    // Luxurious Libation: "Target creature gets +X/+X until end of turn" where X is the {X} PAID.
    // A copy of a spell copies the value of X (CR 707.10), so every instance of a magnet fan-out
    // pumps by the SAME X -- unlike the escalating counters above. Multiplier per point of X.
    int  pump_per_x_power = 0;
    int  pump_per_x_tough = 0;
    // Luxurious Libation: "Create a 1/1 green and white Citizen creature token." Created once per
    // RESOLVED instance (a magnet fan-out makes one per copy, each of which is itself a further
    // copy target next cast). Power 0 AND toughness 0 => no token.
    int  trick_token_power = 0;
    int  trick_token_toughness = 0;
    std::vector<std::string> trick_token_subtypes;
    // Scale the Heights: "You may play an additional land this turn." -> +1
    // Player::bonus_land_drops_this_turn per resolved copy.
    int  grants_extra_land_drop = 0;
    // Twinflame: "create a token that's a copy of that creature, except it has haste. Exile those
    // tokens at the beginning of the next end step." Token = the target's BASE card (copies copy
    // printed characteristics, CR 707.2 -- not counters/until-EOT effects) + Haste keyword +
    // Permanent::exile_at_end (swept at both end-step sites). A token copy of a LEGENDARY target
    // legend-rules immediately; the engine keeps the earlier (original) copy.
    bool token_copy_of_target = false;
    // Twinflame "Strive -- this spell costs {2}{R} more for each target beyond the first." Set =>
    // CollectActions also emits multi-target variants: K extra targets (1..#creatures-1) on the
    // soulfire_own_targets count field (reused; both are "count of extra own-creature targets"),
    // cost += K * strive_cost. WHICH extras is resolved deterministically at resolution (highest
    // printed power first, battlefield order tiebreak) -- a provider-ownable narrowing, disclosed.
    // A strived (multi-target) cast never triggers a copy magnet ("targets only" fails).
    std::optional<ManaCost> strive_cost;

    // Kazandu Refuge: "When this land enters, you gain 1 life." (land ETB, controller gains.)
    int  etb_lifegain = 0;
    // Check land (Rootbound Crag): "enters tapped UNLESS you control a [subtype] or a [subtype]".
    // Non-empty => LandWouldEnterTapped returns tapped iff NO controlled land carries any of these
    // subtypes (the fastland precedent: the shared predicate keeps enumeration pricing and the real
    // drop in lockstep). enters_tapped stays false on the entry (the static flag would double-tap).
    std::vector<std::string> checkland_subtypes;

    // ================= StompySurprise (mono-green elf ramp) =================
    // Every field below is gated (0/false/empty = inert) so other decks stay byte-identical.
    // See docs/design/analysis-stompysurprise.md for the per-card mapping.

    // Elderscale Wurm ETB: "if your life total is less than N, your life total becomes N."
    // (The ongoing damage-floor REPLACEMENT is not modelled -- provably inert: no damage-to-us
    // source exists in this sim; disclosed on the card entry.) 0 = no floor.
    int etb_life_floor = 0;

    // Priest of Titania: "{T}: Add {G} for each Elf ON THE BATTLEFIELD" -- with
    // mana_per_creature_subtype set and feeder 0 on a CREATURE, the dork's per-tap yield is the
    // live subtype count (see IsScaledManaDork / ScaledDorkCount). This flag widens the count to
    // BOTH players' creatures (Priest); false = own side only (Elvish Archdruid).
    bool mana_per_creature_count_all = false;

    // Arbor Elf: "{T}: Untap target Forest." Modelled as a G dork that is LIVE only while the
    // controller controls a land with this subtype -- equivalent in a single-main goldfish
    // (each Arbor untap lets one Forest tap once more: N Arbors + >=1 Forest = +N G).
    std::string mana_requires_land_subtype;

    // Craterhoof Behemoth ETB: "creatures you control gain trample and get +X/+X until end of
    // turn, where X is the number of creatures you control" (X counted AFTER it enters, so it
    // includes itself). Applied as temp_power/tough bonuses to every own creature on the
    // battlefield at resolution (later arrivals correctly excluded, CR 611.2c). The trample
    // grant is inert (passive opponent never blocks) and not modelled -- disclosed.
    bool etb_team_pump_per_creature = false;

    // Mirri's Guile: "At the beginning of your upkeep, you may look at the top three cards of
    // your library, then put them back in any order." N = 3. NO bottoming, NO shuffle (the
    // Ponder family, not scry). Provider-ordered wanted-first; human reorder chooser fires in
    // the viewer. 0 = no upkeep reorder.
    int upkeep_reorder = 0;

    // Vaultborn Tyrant: "Whenever this creature or another creature you control with power 4 or
    // greater enters, you gain 3 life and draw a card." Extends the own_creature_enters_lifegain
    // watcher with a minimum-power filter (effective power incl. counters/lords), a draw rider,
    // and self-inclusion (the watcher fires for its OWN enter too).
    int  creature_enters_min_power     = 0;
    int  own_creature_enters_draw      = 0;
    bool creature_enters_includes_self = false;

    // Vaultborn Tyrant: "When this creature dies, if it's not a token, create a token that's a
    // copy of it ..." -> a token copy of the card (same name -> the copy's own params stay live:
    // its enter fires the watcher above; being is_token it never re-copies itself).
    // "except it's an artifact in addition" is cosmetic here (nothing reads artifact-ness of a
    // creature token in this deck) -- disclosed.
    bool dies_trigger_copy_self_token = false;

    // Natural Order: tutor target must ALSO have this color ("G" = green creature card).
    // Applied on top of tutor_types in every tutor path. Empty = no color filter.
    std::string tutor_color;
    // Natural Order: "As an additional cost to cast this spell, sacrifice a creature" of this
    // color ("G"). The searched victim rides Action/StackEntry::sac_victim_id (one cast variant
    // per own matching creature); the sacrifice is paid at cast time (before resolution) and
    // fires the death cascade (dies-watchers, shuffle-back). Empty = no such cost.
    std::string sac_additional_creature_color;
    // Natural Order / Turntimber front: tutor_to_battlefield puts exactly ONE card (the searched
    // tutor_target). Distinguishes the single-target shape from Dragonstorm's storm-count put.
    bool tutor_to_battlefield_single = false;

    // Call of the Wild: "{cost}: Reveal the top card of your library. If it's a creature card,
    // put it onto the battlefield. Otherwise, put it into your graveyard." Repeatable; the
    // search chooses how many activations (clairvoyant top). nullopt = no such ability.
    std::optional<ManaCost> activated_reveal_top_cost;

    // Wirewood Lodge: "{cost}, {T}: Untap target <subtype>." An activated ability action; the
    // untap target is auto-resolved to the highest-yield tapped matching creature (weakly
    // dominant for mana purposes -- disclosed). nullopt/empty = no such ability.
    std::optional<ManaCost> untap_creature_cost;
    std::string             untap_creature_subtype;

    // Terastodon ETB: "destroy up to three target noncreature permanents; for each, its
    // controller creates a 3/3 green Elephant token." The passive opponent controls no
    // noncreature permanents, so the live mode is destroying OUR OWN -- candidates narrowed to
    // own Forests (tapped first; provider-style narrowing, disclosed). K = 0..this rides
    // Action/StackEntry::chosen_x; the 3/3 Elephant spec reuses etb_created_token_*. 0 = off.
    int etb_destroy_own_noncreature_max = 0;

    // Turntimber Symbiosis back face ("Turntimber, Serpentine Wood"): "As this land enters, you
    // may pay 3 life. If you don't, it enters tapped." Placed on the SYNTHESIZED back face as
    // etb_pay_life_to_untap (shock-land semantics). Also marks the FRONT (a nonland) as having
    // a playable MDFC land back -> land enumeration offers it. 0 = no life-or-tapped choice.
    int mdfc_back_pay_life = 0;

    // Turntimber Symbiosis front: "Look at the top N cards of your library. You may put a
    // creature card from among them onto the battlefield. If that card has mana value
    // <= look_put_counter_bonus_max_mv, it enters with look_put_counter_bonus additional +1/+1
    // counters. Put the rest on the bottom in a random order" (deterministic order --
    // unobservable in goldfish, Muxus precedent). WHICH creature (or none) is the searched
    // tutor_target axis. 0 = not this mechanic.
    int look_top_put_creature_count = 0;
    int look_put_counter_bonus        = 0;
    int look_put_counter_bonus_max_mv = 0;

    // Colour of the creature tokens this card creates ("G" for Hornet Queen's Insects /
    // Worldspine's Wurms / Terastodon's Elephants). Read by the etb_self / dies_trigger /
    // Terastodon token sites so a green token is legal "sacrifice a green creature" fodder
    // (Natural Order). Empty = colourless token (the historical default; byte-identical).
    std::string created_token_color;

    // Creature tokens this card creates enter with HASTE (Frontline Heroism's "1/1 red Soldier
    // creature token with haste"). Read by the same etb_self / cast-trigger / frontline sites that
    // read created_token_color, so the token is attack-eligible the turn it is made (CanAttackFull
    // reads Keyword::Haste). false = the historical summoning-sick token -> byte-identical.
    bool created_token_haste = false;

    // --- Frontline Heroism ({2}{R} enchantment) --------------------------------------------------
    // "Whenever you cast a spell that targets only a single creature you control, create a 1/1 red
    // Soldier creature token with haste, then copy that spell. The copy targets that token."
    // > 0 = this permanent makes that many token+copy pairs per qualifying cast. The qualifying
    // cast is exactly a solo_target_trick resolving against ONE own creature, so the hook lives in
    // ResolveSoloTargetTrick (shared executor/rollout): the token is created, then appended to the
    // recipient list so the shared payload applier gives it the copy. Uses the cast_token_* spec
    // plus created_token_color / created_token_haste. 0 = off -> byte-identical for every other deck.
    int frontline_copy_tokens = 0;

    // ================= Minotaur tribal (Rakdos aggro) =================
    // Every field below is gated (0/false/empty/-1 = inert) so decks that don't run Minotaurs
    // stay byte-identical. See docs/design/analysis-Minotaur.md for the per-card mapping.

    // Fanatic of Mogis: "When this creature enters, it deals damage to each opponent equal to your
    // devotion to red." Non-empty colour ("R") gates it: on ETB, count that colour's mana symbols
    // across the mana costs of every permanent the controller controls (CR 700.5 devotion --
    // INCLUDING this creature, which is already on the battlefield when its own ETB resolves) and
    // deal that much to the opponent's face through the life-loss sink the win projection reads.
    // A HYBRID pip counts for BOTH its colours (CR 202.2b); the engine's flat representation bakes
    // a hybrid into its FIRST colour, so Boros Reckoner's {R/W}{R/W}{R/W} already reads as red 3 --
    // exact here, and DevotionTo() adds the SECOND colour of each hybrid pip explicitly so a
    // future devotion-to-white card is right too. Empty = no ETB devotion damage.
    std::string etb_damage_devotion_color;

    // Ragemonger: "Minotaur spells you cast cost {B}{R} less to cast. This effect reduces only the
    // amount of COLORED mana you pay." The coloured twin of reduces_spell_subtype (which reduces
    // GENERIC). Each permanent you control whose reduces_subtype_colored_subtype matches a subtype
    // of the spell being cast subtracts reduces_subtype_colored_cost's coloured pips from that
    // spell's cost (each colour floored at 0, stacking per copy); the generic is untouched. A
    // reduction that lands on a colour with HYBRID pips consumes a PLAIN pip first and only then a
    // hybrid one (keeping the hybrid the more flexible symbol -- {R}{R/W} minus {R} leaves {R/W},
    // not {R}). Empty subtype / unset cost = not a reducer.
    std::string             reduces_subtype_colored_subtype;
    std::optional<ManaCost> reduces_subtype_colored_cost;

    // Kragma Warcaller: "Whenever a Minotaur you control attacks, it gets +2/+0 until end of turn."
    // A TEAM attack trigger from a lord-like source, unlike attack_pump_power_per_other_matching
    // (Piledriver, which pumps only ITSELF). > 0 gates it: at declare-attackers every declared
    // attacker whose subtype is in this permanent's subtypes_affected gets +this/+0, once per
    // source (the source pumps itself too when it attacks -- "a Minotaur you control" is not
    // "another"). Applied in ApplyAttackSelfPumps, i.e. in BOTH worlds.
    int attack_pump_matching_power = 0;

    // Deathbellow Raider: "This creature attacks each combat if able." A RESTRICTION, not a
    // benefit: DecisionProvider::AttackWith returns true unconditionally for such a permanent, so
    // no provider hold (e.g. the mana-dork hold) can keep it home. Vs the passive opponent the
    // goldfish default already attacks with everything, so this is faithful and today inert --
    // but it is the correct model and it binds the moment any provider declines an attack.
    bool must_attack = false;

    // Neheb, the Worthy: "As long as you have one or fewer cards in hand, Minotaurs you control
    // get +2/+0." A CONDITIONAL anthem: hand_size_anthem_max >= 0 gates it, and the bonus applies
    // to creatures matching subtypes_affected (SELF-INCLUSIVE -- "Minotaurs you control", not
    // "other Minotaurs") only while the controller's hand size is <= hand_size_anthem_max.
    // Evaluated inside ComputeLordBonus, which is why that function takes the whole GameState.
    // -1 = not a conditional anthem.
    int hand_size_anthem_max   = -1;
    int hand_size_anthem_power = 0;
    int hand_size_anthem_tough = 0;

    // Neheb, the Worthy: "Whenever Neheb deals combat damage to a player, each player discards a
    // card." > 0 gates it: after this creature's combat damage connects, its controller discards
    // that many cards (chosen by the provider's cleanup-discard ranking, so the deck's own doctrine
    // picks the card). The OPPONENT's half is inert and not modelled -- the passive opponent never
    // casts and no decision reads their hand (disclosed deferral D10). Note the SYNERGY this
    // creates with hand_size_anthem_max above: the discard is what turns the anthem on.
    int combat_damage_each_discards = 0;

    // Burning-Fist Minotaur: "{1}{R}, Discard a card: This creature gets +2/+0 until end of turn."
    // Rides the existing firebreathing_cost/_power machinery with this extra non-mana cost: each
    // activation also discards one card from the controller's hand, so ApplyFirebreathing stops
    // when the hand is empty. The discarded card is chosen by the provider's cleanup-discard
    // ranking (same doctrine as a cleanup shed). false = a plain mana-only firebreather.
    bool firebreathing_discard = false;

    // Sethron, Hurloon General: "Whenever Sethron OR another nontoken Minotaur you control enters,
    // create a 2/3 red Minotaur creature token." The Lathliss shape
    // (etb_other_subtype_creates_tokens) fires only for ANOTHER matching permanent; this flag
    // widens it to include the source's OWN enter. false = "another ..." (Lathliss, unchanged).
    bool etb_token_includes_self = false;

    // Sethron, Hurloon General: "{2}{B/R}: Minotaurs you control get +1/+0 and gain menace and
    // haste until end of turn." The +1/+0 rides team_pump_cost/_power/_subtypes (spent as combat
    // firebreathing). This flag adds the HASTE half, which cannot be a combat-time conversion --
    // haste has to be granted BEFORE attackers are declared -- so it is enumerated as a main-phase
    // Action::Kind::TeamPumpActivate that sets Permanent::temp_haste (and the same +1/+0) on every
    // matching creature. Menace is inert (no blockers; disclosed deferral D7). false = no haste.
    bool team_pump_grants_haste = false;

    // Slaughter-Priest of Mogis: "Whenever you sacrifice a permanent, this creature gets +2/+0
    // until end of turn." > 0 gates a SACRIFICE WATCHER on this permanent, fired from the shared
    // sacrifice sites (fetchland self-sacrifice, sac outlets, sac-as-an-additional-cost) for every
    // permanent its controller sacrifices. Note a fetchland cracking is a sacrifice, so this fires
    // on a Bloodstained Mire the same turn it fixes mana.
    int sacrifice_watch_pump_power = 0;

    // Slaughter-Priest of Mogis: "{2}, Sacrifice another creature or an enchantment: ...". Widens
    // the sac_creature_outlet victim filter: an ENCHANTMENT you control is also legal fodder
    // (sac_outlet_allows_enchantment), and the source itself is excluded ("another" --
    // sac_outlet_excludes_self). The outlet's own payload here is first strike, which is inert
    // (disclosed deferral D9); the LIVE effect of activating it is the sacrifice_watch_pump_power
    // trigger above, so an outlet with no payload is legal and is NOT a no-op.
    bool sac_outlet_allows_enchantment = false;
    bool sac_outlet_excludes_self      = false;

    // Gnarled Scarhide: "Bestow {3}{B}". An ALTERNATE cast mode (CR 702.103): the same card may be
    // cast for its printed cost as a creature, or for this cost as an AURA that enters attached to
    // a creature and grants aura_power_bonus/aura_tough_bonus. Set alongside the aura_* fields; the
    // card itself stays a creature card (is_aura stays FALSE -- it is an aura only when cast this
    // way, carried on Action/StackEntry::bestow). Both modes are emitted as distinct plan variants
    // so the SEARCH chooses; neither dominates (the body is cheaper and is itself a Minotaur that
    // lords buff, the aura mode dodges summoning sickness by pumping a creature that can attack
    // now). "It becomes a creature again if it's not attached" is inert -- nothing removes our
    // creatures. nullopt = no bestow.
    std::optional<ManaCost> bestow_cost;

    // ---- Flicker combo (EldraziDisplacerFlicker; see docs/design/flicker-combo.md) ----

    // Peregrine Drake: "When this creature enters, untap up to five lands." Cloud of Faeries: "up
    // to two lands." A REAL untap of tapped lands you control (the Reality Spasm literal-untap
    // precedent, RitualUntapSources), highest per-tap yield first -- NOT a floating-mana fake, and
    // that distinction is the whole deck: the untapped lands must be re-tappable so a blink loop
    // nets mana. 0 = no such trigger.
    int etb_untap_lands = 0;

    // Land Auras (Wild Growth / Fertile Ground / Overgrowth / Trace of Abundance): "Enchant land.
    // Whenever enchanted land is tapped for mana, its controller adds an additional <X>."
    // is_land_aura routes the aura to a LAND host (is_aura's LegalEnchantTargets is creature-only);
    // the bonus is credited at the SAME two places a land's own yield is -- PermanentManaYield /
    // AddSourceToPool for the projection and ManaPayment's tap_source for the real tap.
    // land_aura_produces is the bonus's colour, in the same "produces" string form ({"G"});
    // EMPTY means "one mana of any colour" (Fertile Ground / Trace of Abundance), credited as
    // wild -- and NOT as wild_c, because "any colour" cannot pay a {C} pip (ManaPool::wild_c).
    bool               is_land_aura         = false;
    int                land_aura_extra_mana = 0;
    std::vector<Color> land_aura_produces;
    // "Enchanted land has shroud" (Trace of Abundance). NOT cosmetic, and not inert just because the
    // opponent is passive: an Aura SPELL targets its host as it is cast (CR 303.4a, 702.18a), so a
    // land already carrying a shroud-granting aura cannot legally receive ANOTHER aura -- including a
    // second copy of the same card. That makes this a legality filter on the deck's OWN plays, which
    // is the case a "does anything target a land?" reading misses. It matters here because
    // concentrating auras on one host is a real line (it raises the top-N yield the blink loop
    // untaps), so the host ranking actively steers INTO the illegal play unless this is enforced.
    bool               land_aura_grants_shroud = false;

    // Eldrazi Displacer "{2}{C}: Exile another target creature, then return it to the battlefield
    // tapped under its owner's control" / Emiel the Blessed "{3}: Exile another target creature you
    // control, then return it". A repeatable, NON-tap activated ability: no {T} in the cost, so a
    // summoning-sick outlet can blink the turn it lands. The returned permanent is a NEW OBJECT --
    // it re-enters (both ETB cascades fire, which is what makes the loop) and is summoning sick
    // again. blink_returns_tapped is Displacer's drawback; blink_own_only is Emiel's "you control"
    // restriction (Displacer may target ANY creature). nullopt = no such ability.
    std::optional<ManaCost> blink_cost;
    bool                    blink_returns_tapped = false;
    bool                    blink_own_only       = false;

    // Training Grounds: "Activated abilities of creatures you control cost {2} less to activate.
    // This effect can't reduce the mana in that cost to less than one mana." A STATIC cost reducer
    // for ACTIVATION costs -- a different axis from every reducer in EffectiveSpellCost, which are
    // all keyed on the card being CAST. Applied by EffectiveActivationCost (ManaPayment.h) at all
    // four sites an activation cost is read (enumeration + both pay paths + the human trial-pay).
    // The one-mana FLOOR is the card's own wording and is unlike every spell reducer, which floors
    // at zero: {3} -> {1}, and {2}{C} -> {C} (the generic half goes, the colourless pip stays).
    int reduces_creature_activation = 0;

    // Emiel the Blessed's second ability: "Whenever ANOTHER creature you control enters, you may
    // pay {G/W}. If you do, put a +1/+1 counter on it. If it's a Unicorn, put two instead." An
    // optional-cost ETB WATCHER (fires from FireEtbWatchers, so a blinked creature re-triggers it
    // every iteration -- the deck's second kill). The payment is optional and taken whenever it is
    // affordable from mana that is otherwise spare. Empty cost = no such watcher.
    std::optional<ManaCost> other_creature_etb_counter_cost;
    int         other_creature_etb_counters         = 0;
    std::string other_creature_etb_counter_subtype;   // "Unicorn"
    int         other_creature_etb_counters_subtype = 0;

    // Shivan Gorge: "{2}{R}, {T}: Shivan Gorge deals 1 damage to each opponent." A {cost}+{T}
    // activated ability on a LAND. This is the deck's same-turn kill: the blink loop untaps the
    // Gorge every iteration, so N iterations are N activations. nullopt = no such ability.
    std::optional<ManaCost> tap_damage_cost;
    int                     tap_damage_each_opponent = 0;

    // Essence Depleter: "{1}{C}: Target opponent loses 1 life and you gain 1 life." nullopt = no
    // such ability.
    //
    // STRUCTURALLY DIFFERENT FROM tap_damage_cost ABOVE, and the difference is the whole reason this
    // deck wants it: there is **no {T} in the cost**, so it is not once-per-untap. It is a pure mana
    // sink -- N activations cost N x the price and nothing else -- which makes it the ONLY win
    // condition in the deck that converts unbounded mana into a kill without the blink loop having
    // to untap anything. Shivan Gorge needs a fresh untap per activation and therefore rides the
    // loop's untap priority; this just needs mana. Under Training Grounds the generic half is
    // reduced and the {C} pip survives ({1}{C} -> {C}), the same arithmetic as Displacer's cost.
    //
    // Consequences that any consumer must respect: the activation COUNT is unbounded (so it needs a
    // count axis, like ActivateBlink, not a single emission), and the lifegain half is real but
    // inert for the clock against a passive opponent.
    std::optional<ManaCost> drain_cost;
    int                     drain_amount    = 0;   // life the opponent loses per activation
    int                     drain_self_gain = 0;   // life we gain per activation

    // "{cost}: Target opponent exiles the top card of their library." (Dimensional Infiltrator's
    // "{1}{C}: ... If it's a land card, you may return this creature to its owner's hand.")
    // nullopt = no such ability. Like drain_cost there is NO {T}, so it is repeatable within a turn
    // and bounded only by mana -- which is what makes it a real deck-out clock off an infinite loop.
    //
    // This param is ALSO the deck trait that gives the opponent a library at all
    // (GoldFishRunner::DeckTouchesOpponentZones -> GameState::opponent_library_dealt). Any future
    // mill / forced-draw / discard param must be added to that detector too, or the effect will
    // resolve against an EMPTY zone and silently do nothing.
    std::optional<ManaCost> exile_opponent_top_cost;
    // "If it's a land card, you may return this creature to its owner's hand." A MAY, and declining
    // is provably dominant here (bouncing costs us the permanent and re-casting it, buying nothing
    // a goldfish can use), so it is modelled as always-declined rather than as a searched choice --
    // recorded as a bracket note on the card, not silently dropped.
    bool                    exile_opponent_top_may_bounce_on_land = false;

    // AETHER HUB. "When this land enters, you get {E} (an energy counter). / {T}: Add {C}. /
    // {T}, Pay {E}: Add one mana of any color."
    //
    // ENERGY IS A PLAYER RESOURCE (Player::energy_counters), not a permanent's -- three Hubs pool
    // three {E} and any ONE of them may spend all three. That distinction is reachable here rather
    // than academic: Peregrine Drake and Cloud of Faeries untap lands on every blink iteration, so
    // correct play is to tap ONE Hub three times spending the whole pool through it, which a
    // per-permanent counter cannot express.
    //
    // AND THE METERING IS THE WHOLE POINT. Under that same untap loop a Hub is re-tapped an
    // unbounded number of times per turn, so modelled as a plain 6-colour land it becomes an
    // INFINITE any-colour source -- which switches on the {2}{R} Shivan Gorge kill for free and
    // makes the deck look far faster than it is. That is the over-acceptance class the rad-mode
    // comment warns about, at a much larger magnitude.
    //
    // Modelled as produces = [C,W,U,B,R,G] + this cost: the coloured half is STRIPPED while the
    // controller holds less than energy_per_colored_tap, so a spent-out Hub degrades to a plain
    // {C} source rather than vanishing. "One mana of any COLOR" is the five colours only ({C} is
    // not a colour, CR 105.1), and the free "{T}: Add {C}" mode covers colourless -- so a {C} pip
    // (Eldrazi Displacer's {2}{C}) is NEVER energy-gated. 0 = every other source -> byte-identical.
    int  etb_energy             = 0;   // energy gained when this permanent enters
    int  energy_per_colored_tap = 0;   // energy ONE coloured tap of this source costs

    // Mariposa Military Base: "{5}, {T}: Draw a card. This ability costs {1} less to activate for
    // each rad counter you have", plus "You may have this land enter tapped. If you do, you get two
    // rad counters." Rad counters are a PLAYER resource (Player::rad_counters), not a permanent's.
    // nullopt = no such ability; 0 = no rad option.
    std::optional<ManaCost> tap_draw_cost;
    bool                    tap_draw_cost_less_per_rad = false;
    int                     etb_optional_tapped_rad    = 0;

    // Conservatory / Kitchen: "{4}, {T}: Investigate." Creates one Clue artifact token, whose own
    // "{2}, Sacrifice this token: Draw a card" lives on the "Clue Token" NAMED TOKEN DEF in
    // cards.json (the Treasure Token precedent) via sac_draw_cost below. nullopt = no such ability.
    std::optional<ManaCost> tap_investigate_cost;

    // "{cost}, Sacrifice this: Draw a card" on a PERMANENT (the Clue Token). Distinct from
    // sacrifice_draw_cost, which is a LAND ability that also requires {T} (Fiery Islet) -- a Clue
    // has no tap symbol, so a Clue made this turn can be cracked this turn. nullopt = no such
    // ability.
    std::optional<ManaCost> sac_draw_cost;
};

// A fully resolved card definition: base Card data plus template + parameters.
struct CardDefinition
{
    Card card;
    CardTier tier        = CardTier::Data;
    CardTemplate tmpl    = CardTemplate::None;
    CardParams params;
};

// Singleton registry of all known card definitions.
// Populated from:
//   - JSON files in src/cards/data/   (Tiers 1 & 2)
//   - Registration calls in custom card files (Tier 3)
class CardDatabase
{
public:
    // Eagerly-constructed static singleton (s_instance, defined in the .cpp). Inline +
    // non-function-local so each call is a plain address load with NO per-call
    // thread-safe-init guard -- the Meyers function-local-static form cost ~6% of a
    // search game in guard-acquire loads on the hot Lookup/LookupCached path (callgrind).
    // Safe to init eagerly: nothing constructs or uses the DB during static init (Register
    // is unused; LoadFromJson runs from main), so there is no static-init-order hazard.
    static CardDatabase& Instance() { return s_instance; }

    // Load all card definitions from a JSON file.
    // Can be called multiple times to load multiple files.
    void LoadFromJson(const std::filesystem::path& path);

    // Register a Tier 3 custom card. Called from generated registration functions.
    using CardFactory = std::function<CardDefinition()>;
    void Register(const std::string& name, CardFactory factory);

    // Look up a card by name (case-sensitive, matches Scryfall name).
    const CardDefinition* Lookup(const std::string& name) const;

    // Names of every modal double-faced (MDFC) BACK face known to the DB (e.g. "Boulderloft
    // Pathway"). The play viewer uses this to request the correct Scryfall face image -- a back
    // face's default image is its FRONT, so it must ask for face=back. Empty without MDFC lands.
    std::vector<std::string> MdfcBackFaceNames() const;

    // Look up a card's definition via its memoized pointer (Card::m_def), falling
    // back to a by-name Lookup (and caching the result) the first time. Prefer this
    // over Lookup(c.m_name) on the hot path: a card resolved once carries its def
    // through every GameState deep-copy, so the repeated name-hash + hashtable find
    // is paid at most once per distinct card object rather than per search node.
    // (callgrind 2026-06-19: name hashing was ~39% of a search game after the
    // by-value-Lookup fix.) Behaviour is identical to Lookup(c.m_name) -- same result,
    // and m_def never escapes into any key/output (see Card::m_def).
    //
    // MISS caching (2026-07-31): a TOKEN (or any card not in the DB) has no def, so
    // Lookup(name) returns nullptr -- and the old code stored that nullptr in m_def, which
    // is indistinguishable from "not yet resolved", so EVERY LookupCached on a token re-hashed
    // its name AND paid a failed full-table probe (the most expensive find). On a token-heavy
    // board (Krenko / Siege-Gang / Mogg spawn many "N/N Goblin Token"s) this was the dominant
    // hot-path cost -- ~13% of a deep-search game (perf on the seed-8021 60s Goblin outlier).
    // Cache the MISS with a non-null sentinel (NotInDb, a stable address never dereferenced):
    // a token now hashes ONCE, then returns nullptr from the cached sentinel forever. The
    // return value is IDENTICAL (nullptr for a token every time) -- purely a speed fix. The
    // m_def=nullptr invalidation sites (name change: CardDatabase.cpp / SpellEffects.cpp) still
    // force a re-lookup, because only the sentinel (not nullptr) means "resolved-and-absent".
    static const CardDefinition* NotInDb() noexcept
    {
        static const char sentinel = 0;   // stable address, distinct from every real def; never dereferenced
        return reinterpret_cast<const CardDefinition*>(&sentinel);
    }
    // RACE-FREE lazy fill (2026-08-30). `c` is sometimes a process-wide SHARED Card -- EvalCard
    // hands ComputeLordBonus the CardDatabase's own `def.card` prototype -- so every worker thread
    // fills the same m_def concurrently. The value stored is always identical (one definition per
    // name), so no result was ever at stake, but the plain read/write pair is still a data race and
    // ThreadSanitizer reports it. std::atomic_ref makes it well-defined while leaving Card
    // trivially copyable (std::atomic would not); relaxed ordering suffices because the pointee is
    // immutable and published before any thread starts. See Card::m_def.
    const CardDefinition* LookupCached(const Card& c) const
    {
        std::atomic_ref<const CardDefinition*> slot(c.m_def);
        const CardDefinition* cached = slot.load(std::memory_order_relaxed);
        if (cached == NotInDb()) { return nullptr; }    // cached miss (token / unknown card)
        if (cached)              { return cached; }     // cached hit
        const CardDefinition* d = LookupInterned(c.m_name);
        slot.store(d ? d : NotInDb(), std::memory_order_relaxed);
        return d;
    }

    // By-INTERNED-NAME lookup: hash + compare the InternedName's canonical POINTER instead of the
    // name string. Byte-identical to Lookup(n.str()) -- m_by_name_ptr indexes exactly m_cards, keyed
    // by each name's canonical interned address (unique per name, global registry) -- so a miss here
    // IS "not in the DB". Motivation (gdb sampling, Mirrorwing phase-A heavy game 2026-08-12): the
    // per-object m_def memo above pays one STRING-map probe per fresh Card object, and a fan-out
    // apply (Twinflame / Gold Rush copies) creates its tokens anew on EVERY speculative
    // ApplyPlanDirect (114M applies in a 60-game batch) -- string hash+equals on the CardDefinition
    // map was ~25% of the heavy game's samples. A pointer probe removes the string work entirely.
    const CardDefinition* LookupInterned(const InternedName& n) const
    {
        auto it = m_by_name_ptr.find(&n.str());
        return it == m_by_name_ptr.end() ? nullptr : it->second;
    }

    bool IsImplemented(const std::string& name) const;

    // Returns all registered card names — used by the analyzer to check coverage.
    std::vector<std::string> AllNames() const;

    // Behaviorally-relevant definition hash for a card, used by the mulligan re-run
    // carry (execution-trace Phase B) to auto-detect which cards' DATA changed between
    // two commits. It is an FNV-1a over the card's canonical cards.json entry with the
    // COSMETIC fields ("name", "oracle_text") removed -- i.e. every field the engine
    // actually reads (mana_cost, types, keywords, P/T, parameters, ...). We hash the raw
    // JSON (canonically re-serialized so key order / whitespace don't matter) rather than
    // walking CardParams by hand, so a newly-added parameter can never be silently missed.
    // Deterministic across machines (unlike std::hash). Returns 0 for an unknown card or
    // one registered in C++ (Tier 3, no JSON) -- such a card's behaviour lives in the
    // engine, so it is covered by the engine fingerprint instead.
    uint64_t DefHash(const std::string& name) const
    {
        auto it = m_def_hash.find(name);
        return it == m_def_hash.end() ? 0ULL : it->second;
    }

private:
    CardDatabase() = default;

    Card BuildCardFromJson(const nlohmann::json& entry) const;
    CardParams BuildParamsFromJson(const nlohmann::json& params) const;

    std::unordered_map<std::string, CardDefinition> m_cards;
    std::unordered_map<std::string, uint64_t>       m_def_hash;   // see DefHash()
    // Canonical-interned-name-pointer -> definition index over m_cards (see LookupInterned).
    // Rebuilt at the end of every LoadFromJson/Register, so it is complete and IMMUTABLE during
    // play (values point into m_cards nodes, which never move). Lock-free reads are safe.
    std::unordered_map<const std::string*, const CardDefinition*> m_by_name_ptr;
    void RebuildInternedIndex();

    static CardDatabase s_instance;   // eager singleton storage (see Instance())
};
