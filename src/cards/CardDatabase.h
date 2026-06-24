#pragma once
#include "../core/Card.h"
#include "CardTemplate.h"
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
    std::vector<std::string> subtypes_affected;  // for lord effects

    // On-cast trigger: when the controller casts a spell with MV <= on_cast_trigger_max_mv,
    // deal on_cast_trigger_damage to that player. Used for Eidolon of the Great Revel.
    int on_cast_trigger_max_mv  = 0;
    int on_cast_trigger_damage  = 0;

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

    // No maximum hand size (e.g. Reliquary Tower). If true, the cleanup-step discard
    // to 7 is skipped while this permanent is on the battlefield.
    bool no_max_hand_size = false;

    // Land's Edge pattern: "Discard a land card: deal this much damage to target player."
    // When > 0 this permanent provides the ability; AI will discard all hand lands for damage.
    int  discard_land_damage = 0;

    // Cascade: when cast, exile from library top until a nonland card with mana value
    // strictly less than cascade_max_mv is found; cast it for free; put the rest on the bottom.
    // 0 = no cascade.
    int  cascade_max_mv = 0;

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

    // Attack-trigger token creation (e.g. Adeline, Resplendent Cathar: "Whenever you
    // attack, for each opponent, create a 1/1 white Human token that's tapped and
    // attacking"). When attack_creates_tokens > 0, declaring at least one attacker makes
    // that many tokens (per opponent = 1 in goldfish) which are tapped and deal combat
    // damage this turn, then persist. Fired at combat (real game + rollout).
    int                      attack_creates_tokens = 0;
    int                      attack_token_power     = 0;
    int                      attack_token_toughness = 0;
    std::vector<std::string> attack_token_subtypes;

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

    // Scry-on-resolution for a draw spell (Preordain "Scry 2, then draw a card"; Ponder
    // "look at the top three cards, then put them back in any order. Draw a card." -- modelled
    // as Scry N then draw, the standard clairvoyant deck-ordering approximation: ScryTop keeps
    // the wanted cards on top via DecisionProvider::ScryKeepOnTop, which is what an optimal
    // reorder of a known library does for the immediately-following draw). Applied BEFORE the
    // draw in BOTH the executor (ResolveDrawSpell) and the rollout (ApplyPlanDirect DrawSpell
    // branch), so the realised draw matches the searched line. Gated > 0.
    int cast_scry = 0;

    // X-damage scaling for an {X} DirectDamage spell (Crackle with Power "deals five times X
    // damage" -> 5). The damage dealt per target is chosen_x * x_damage_multiplier; combined with
    // x_pips on the cost, Crackle = {X}{X}{X}{R}{R}, 5X to each target. Default 1 (plain {X} burn
    // like a hypothetical X-bolt). Applied in BOTH the search's X-enumeration (direct_damage/eval)
    // and ResolveDirectDamage. Gated (defaults to 1) so a plain X burn is unchanged.
    int x_damage_multiplier = 1;

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

    // Look up a card's definition via its memoized pointer (Card::m_def), falling
    // back to a by-name Lookup (and caching the result) the first time. Prefer this
    // over Lookup(c.m_name) on the hot path: a card resolved once carries its def
    // through every GameState deep-copy, so the repeated name-hash + hashtable find
    // is paid at most once per distinct card object rather than per search node.
    // (callgrind 2026-06-19: name hashing was ~39% of a search game after the
    // by-value-Lookup fix.) Behaviour is identical to Lookup(c.m_name) -- same result,
    // and m_def never escapes into any key/output (see Card::m_def).
    const CardDefinition* LookupCached(const Card& c) const
    {
        if (c.m_def) { return c.m_def; }
        c.m_def = Lookup(c.m_name);
        return c.m_def;
    }

    bool IsImplemented(const std::string& name) const;

    // Returns all registered card names — used by the analyzer to check coverage.
    std::vector<std::string> AllNames() const;

private:
    CardDatabase() = default;

    Card BuildCardFromJson(const nlohmann::json& entry) const;
    CardParams BuildParamsFromJson(const nlohmann::json& params) const;

    std::unordered_map<std::string, CardDefinition> m_cards;

    static CardDatabase s_instance;   // eager singleton storage (see Instance())
};
