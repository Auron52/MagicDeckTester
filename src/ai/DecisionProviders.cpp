#include <cstdlib>
#include "DecisionProviders.h"

#include "../core/SpellEffects.h"   // shared rules helpers + the archetype heuristic free fns
#include "../deck/DeckLoader.h"     // Decklist

// Standing unpruned-vs-pruned A/B (search-primary requirement): when MTG_UNPRUNED is set,
// the search-narrowing heuristics return their MAXIMALLY-PERMISSIVE value so the general
// search explores the full branch space instead of the heuristic-narrowed one. Run the
// suite with and without it and diff per-game: if the unpruned arm wins MORE or FASTER, a
// pruning heuristic is costing the search a line (a bad heuristic); if it is the same (or
// only slower), the heuristic is a sound perf-only pruner. Default off => byte-identical.
// Currently widens the GATE hooks (ShouldCastDrawEngine / ShouldEmitRiskyAltPayload) --
// the two heuristics that actually delete a cast from the search's branch space. The
// candidate-narrowers (Tutor/Fetch, in shared SpellEffects.h) are a follow-up; for the
// current decks they rarely narrow to a costly choice. Decision/policy hooks that don't
// prune the search space (scry-keep, vial-charge, discard-order, ...) are unaffected.
bool DecisionUnpruned()
{
    static const bool v = std::getenv("MTG_UNPRUNED") != nullptr;
    return v;
}

// Stage 6: the search tree calls the provider for every deck decision; here the GENERIC
// defaults are minimal (a deck-agnostic baseline) and each archetype subclass holds its
// own heuristics. Archetype detection (SelectDecisionProvider) routes each deck to its
// provider. Byte-identical to the pre-refactor engine: every archetype hook is exclusive
// to one deck family (verified), so a Generic default is only ever exercised by decks
// that don't use that hook.

// ---- GenericProvider: deck-agnostic baseline --------------------------------

std::vector<std::string>
GenericProvider::TutorCandidates(const GameState&, int, const CardParams&) const
{
    return {};   // no generic tutor heuristic; archetypes with tutors override.
}

std::vector<std::string>
GenericProvider::FetchCandidates(const GameState&, int, const CardParams&) const
{
    return {};   // no generic fetch heuristic; archetypes with fetchlands override.
}

bool GenericProvider::CanAutoFireAltPayload(const GameState&, int, const CardDefinition&) const
{
    return false;   // no free alt-cost payloads in a generic deck.
}

bool GenericProvider::HasAnyDigSource (const GameState&) const { return false; }
bool GenericProvider::ShouldConsiderDig(const GameState&) const { return false; }
std::string GenericProvider::SelectDigSource(const GameState&, const ManaPool&, bool&) const { return {}; }

int GenericProvider::LandsEdgeFireCount(const GameState&, int) const
{
    return 0;   // only Land's Edge decks activate this; archetype overrides.
}

bool GenericProvider::WantVialCharge(const GameState&, const Permanent&) const
{
    return false;   // only Aether Vial decks charge; archetype overrides.
}

bool GenericProvider::ScryKeepOnTop(const GameState& s, const Card& top_card) const
{
    // Generic scry/surveil keep: keep nonland spells; keep a land only while fewer than
    // two lands are in play. (The Treasure Hunt provider adds the DrawUntilNonland clause.)
    const CardDefinition* tdef = CardDatabase::Instance().LookupCached(top_card);
    bool is_land = tdef ? tdef->card.IsLand() : top_card.IsLand();
    if (!is_land) { return true; }
    int lands_in_play = 0;
    for (const Permanent& p : s.battlefield)
    {
        if (p.controller_index == s.active_player_index && p.card.IsLand()) { ++lands_in_play; }
    }
    return lands_in_play < 2;
}

bool GenericProvider::CastEnablerFirst(const GameState&, const std::string&) const
{
    return false;   // no enabler-first sequencing in a generic deck.
}

bool GenericProvider::DiscardLandsFirst(const GameState&) const
{
    return false;   // generic: discard the highest-MV card, not lands.
}

bool GenericProvider::ShouldEmitRiskyAltPayload(const GameState&, int, const CardDefinition&) const
{
    return false;   // no risky alt-cost payloads in a generic deck.
}

bool GenericProvider::ShouldCastDrawEngine(const GameState&, int,
                                          const CardDefinition&) const
{
    return true;   // no generic flood-engine gate; the Treasure-Hunt archetype overrides.
}

std::string GenericProvider::PostDrawKeepLandName(const GameState&, int) const
{
    return {};   // no deferred draw-engine keep-land in a generic deck (only the engine's
                 // best-normal-land fallback applies). The Treasure-Hunt archetype overrides.
}

bool GenericProvider::HasExtraLethalModel() const
{
    return false;   // no deck-specific lethal addend; the Treasure-Hunt archetype overrides.
}

int GenericProvider::ExtraLethalDamage(const GameState&,
                                       const std::vector<const CardDefinition*>&) const
{
    return 0;
}

bool GenericProvider::ArchetypeCardValue(const GameState&, const CardDefinition&, int, int&) const
{
    return false;   // no archetype card-value override; EvalCard's generic estimate applies.
}

bool GenericProvider::ShouldStageSpectacleDraw(const GameState&, int,
                                               const CardDefinition& draw_def) const
{
    // Spectacle is a card-mechanic alternate cost: stage a draw spell with a Spectacle
    // cost behind a cheap damage spell to unlock it. Kept generic (param-gated) so a
    // Spectacle deck routed to Generic still enumerates the variant.
    return draw_def.params.spectacle_cost.has_value();
}

// ---- AntiLifegainProvider ---------------------------------------------------

std::vector<std::string>
AntiLifegainProvider::TutorCandidates(const GameState& s, int controller, const CardParams& pp) const
{
    return ::TutorCandidates(s, controller, pp);
}

std::vector<std::string>
AntiLifegainProvider::FetchCandidates(const GameState& s, int controller, const CardParams& fetch_pp) const
{
    return ::FetchCandidates(s, controller, fetch_pp);
}

bool AntiLifegainProvider::CanAutoFireAltPayload(const GameState& s, int controller,
                                                 const CardDefinition& def) const
{
    return ::CanAutoFireAltPayload(s, controller, def);
}

bool AntiLifegainProvider::CastEnablerFirst(const GameState&, const std::string& card_name) const
{
    // Enabler-first: lifegain_to_loss cards (Tainted Remedy / Plague Drone) cast + resolve
    // before payloads so a same-turn payload sees the enabler active.
    return ::IsLifegainToLossCard(card_name);
}

bool AntiLifegainProvider::ShouldEmitRiskyAltPayload(const GameState& s, int controller,
                                                     const CardDefinition& def) const
{
    if (DecisionUnpruned()) { return true; }   // unpruned A/B: let the search judge the wipe.
    // Reverent Silence's destroy-all-enchantments wipes our OWN Aria/Remedy. Casting it
    // non-lethally with no surviving enabler bricks the combo (the greedy second-main rollout
    // overvalues the immediate 6 -- regression gi=36: opp 23, single Tainted Remedy, no Drone
    // -> Reverent destroys the only enabler and the deck stalls). Emit it only when:
    //   (a) a Plague Drone (lifegain_to_loss CREATURE) is IN PLAY -- it survives the wipe, so
    //       the enabler stays online. An enchantment Remedy does NOT survive, even a 2nd one
    //       cast the same turn (enabler-first casts it before Reverent, so it is wiped too --
    //       the "Reverent + 2nd Remedy + Reverent" rebuild needs cross-turn sequencing the
    //       engine does not model; allowing it just re-bricks, regression gi=84); or
    //   (b) it is lethal in combination -- the free 6 plus an unblocked attack finishes the
    //       opponent this turn (wiping our own combo is fine once the game is won).
    if (!def.params.destroy_all_enchantments || !::RemedyActive(s, controller)) { return false; }

    // (a) a Plague Drone in play survives the enchantment wipe
    for (const Permanent& p : s.battlefield)
    {
        if (p.controller_index != controller) { continue; }
        const CardDefinition* d = CardDatabase::Instance().LookupCached(p.card);
        if (d && d->params.lifegain_to_loss && p.card.IsCreature()) { return true; }
    }

    // (b) lethal in combination with this turn's attackers
    int atk = 0;
    for (const Permanent& p : s.battlefield)
    {
        if (p.controller_index == controller && p.card.IsCreature() && p.CanAttack())
        {
            int pw = p.EffectivePower();
            if (pw > 0) { atk += pw; }
        }
    }
    return s.players[1 - controller].life <= def.params.alt_lifegain_cost + atk;
}

// ---- TreasureHuntProvider ---------------------------------------------------

bool TreasureHuntProvider::HasAnyDigSource (const GameState& s) const { return ::HasAnyDigSource(s); }
bool TreasureHuntProvider::ShouldConsiderDig(const GameState& s) const { return ::ShouldConsiderDig(s); }
std::string TreasureHuntProvider::SelectDigSource(const GameState& s, const ManaPool& pool, bool& out_is_sac) const
{
    return ::SelectDigSource(s, pool, out_is_sac);
}

int TreasureHuntProvider::LandsEdgeFireCount(const GameState& s, int rate) const
{
    return ::LandsEdgeHeuristicFireCount(s, rate);
}

bool TreasureHuntProvider::DiscardLandsFirst(const GameState& s) const
{
    // Land's Edge land outlet (discard_land_damage) in hand or in play -> lands are
    // ammunition; shed a land before the highest-MV card.
    const Player& ap = s.players[s.active_player_index];
    for (const Card& c : ap.hand)
    {
        const CardDefinition* def = CardDatabase::Instance().LookupCached(c);
        if (def && def->params.discard_land_damage > 0) { return true; }
    }
    for (const Permanent& p : s.battlefield)
    {
        if (p.controller_index != s.active_player_index) { continue; }
        const CardDefinition* def = CardDatabase::Instance().LookupCached(p.card);
        if (def && def->params.discard_land_damage > 0) { return true; }
    }
    return false;
}

bool TreasureHuntProvider::ScryKeepOnTop(const GameState& s, const Card& top_card) const
{
    // Deck-aware keep: keep nonlands always; keep a land while a DrawUntilNonland (Treasure
    // Hunt) in hand wants land fuel, or fewer than two lands are in play.
    const CardDefinition* tdef = CardDatabase::Instance().LookupCached(top_card);
    bool is_land = tdef ? tdef->card.IsLand() : top_card.IsLand();
    if (!is_land) { return true; }

    const Player& ap = s.players[s.active_player_index];
    for (const Card& c : ap.hand)
    {
        const CardDefinition* cdef = CardDatabase::Instance().LookupCached(c);
        if (cdef && cdef->tmpl == CardTemplate::DrawUntilNonland) { return true; }
    }
    int lands_in_play = 0;
    for (const Permanent& p : s.battlefield)
    {
        if (p.controller_index == s.active_player_index && p.card.IsLand()) { ++lands_in_play; }
    }
    return lands_in_play < 2;
}

bool TreasureHuntProvider::ShouldCastDrawEngine(const GameState& s, int controller,
                                                const CardDefinition& def) const
{
    if (DecisionUnpruned()) { return true; }   // unpruned A/B: never gate the flood engine.
    // Cast a flood engine -- Treasure Hunt (DrawUntilNonland) or a cascade/retrace card that
    // can cascade INTO it (Throes of Chaos) -- only when the cards it draws will not be wasted.
    // Without a payoff the drawn lands just hit cleanup discard (gi=67: Treasure Hunt drew 31
    // lands with no Land's Edge online -> all discarded). Three real payoffs:
    //   (1) Land's Edge already in play         -> the drawn lands become damage now;
    //   (2) enough untapped mana THIS turn to cast the engine AND Land's Edge afterward
    //       -> the same-turn combo (the engine draws Land's Edge, cast it, throw the lands).
    //       Checked with COLORED affordability, so the {R}{R} requirement separates a real
    //       combo hand (a Sandstone Needle for {R}{R}) from a flood hand that cannot make it;
    //   (3) a no-max-hand-size land (Reliquary Tower) in play or in hand -> the draw is KEPT.
    // Gambling on DRAWING Reliquary Tower (or Land's Edge) and bricking is an acceptable real
    // game -- not credited here.
    const Player& ap = s.players[controller];

    const CardDefinition* le_def = nullptr;   // a Land's Edge def (for its cost in (2))
    for (const Permanent& p : s.battlefield)
    {
        if (p.controller_index != controller) { continue; }
        const CardDefinition* d = CardDatabase::Instance().LookupCached(p.card);
        if (!d) { continue; }
        if (d->params.discard_land_damage > 0) { return true; }                 // (1) LE in play
        if (d->params.no_max_hand_size && d->card.IsLand()) { return true; }    // never floods
    }
    // (3) a land drop THIS TURN -> either still open (defer it to play a drawn Reliquary Tower
    //     / Land's-Edge enabler), OR already spent developing a real land this turn (in which
    //     case digging alongside is still fine -- the drop was used productively).
    //     IMPORTANT: the land-fold enumeration (add_for_land) plays the candidate land into the
    //     trial state BEFORE this gate runs, so a "play a land AND cast Treasure Hunt" plan
    //     shows lands_played_this_turn==1 here. Crediting a just-played land keeps that line
    //     legal -- otherwise the gate deletes Treasure Hunt from every play-a-land branch and
    //     forces deferring the land, which then gets discarded in the flood (gi=881). Whiffing
    //     the drawn payoff and bricking is an acceptable real game.
    if (ap.lands_played_this_turn > 0
        || ap.lands_played_this_turn < ap.LandDropsAvailable()) { return true; }   // (3)

    // (2) -- find a Land's Edge cost from any zone (it is usually still in the library, since
    // the engine is what draws it), then check the same-turn combo affordability.
    auto find_le = [](auto begin, auto end) -> const CardDefinition*
    {
        for (auto it = begin; it != end; ++it)
        {
            const CardDefinition* d = CardDatabase::Instance().LookupCached(*it);
            if (d && d->params.discard_land_damage > 0) { return d; }
        }
        return nullptr;
    };
    le_def = find_le(ap.hand.begin(), ap.hand.end());
    if (!le_def) { le_def = find_le(ap.library.begin(), ap.library.end()); }
    if (!le_def) { le_def = find_le(ap.graveyard.begin(), ap.graveyard.end()); }
    if (le_def)
    {
        ManaPool pool;   // untapped lands/dorks (mirrors TurnSolver::BuildPool)
        for (const Permanent& p : s.battlefield)
        {
            if (p.controller_index != controller || p.tapped) { continue; }
            const CardDefinition* d = CardDatabase::Instance().LookupCached(p.card);
            if (!d) { continue; }
            bool is_land = (d->tmpl == CardTemplate::BasicLand);
            bool is_dork = (d->tmpl == CardTemplate::ManaDork && p.CanTap());
            if (!is_land && !is_dork) { continue; }
            AddSourceToPool(pool, s, *d);
        }
        ManaCost combined  = def.card.m_mana_cost;
        const ManaCost& lc = le_def->card.m_mana_cost;
        combined.white += lc.white; combined.blue += lc.blue; combined.black += lc.black;
        combined.red   += lc.red;   combined.green += lc.green;
        combined.colorless += lc.colorless; combined.generic += lc.generic;
        if (pool.CanPay(combined)) { return true; }                            // (2)
    }
    return false;
}

std::string TreasureHuntProvider::PostDrawKeepLandName(const GameState& s, int controller) const
{
    // After a deferred Treasure Hunt resolves: if the hand is flooding past max size and no
    // no-max-hand-size land (Reliquary Tower) is already in play, play a DRAWN Reliquary so the
    // whole flood is KEPT as Land's Edge ammo instead of being discarded at cleanup (gi=65).
    // Otherwise return "" -> the engine plays the best normal land (the deferred drop). The
    // engine owns the open-land-drop precondition + the land-play mechanism; this is the choice.
    const Player& lp = s.players[controller];
    if (static_cast<int>(lp.hand.size()) <= 7) { return {}; }                  // not flooding
    for (const Permanent& p : s.battlefield)                                   // already safe?
    {
        if (p.controller_index != controller) { continue; }
        const CardDefinition* d = CardDatabase::Instance().LookupCached(p.card);
        if (d && d->params.no_max_hand_size && d->card.IsLand()) { return {}; }
    }
    for (const Card& c : lp.hand)                                              // keep with a drawn Reliquary
    {
        const CardDefinition* d = CardDatabase::Instance().LookupCached(c);
        if (d && d->params.no_max_hand_size && d->card.IsLand()) { return c.m_name; }
    }
    return {};
}

bool TreasureHuntProvider::HasExtraLethalModel() const
{
    return true;   // the Land's Edge / Treasure Hunt lethal model below.
}

int TreasureHuntProvider::ExtraLethalDamage(const GameState& s,
        const std::vector<const CardDefinition*>& casting) const
{
    // The deck's reach toward THIS turn's lethal beyond combat + direct damage: lands in hand
    // are Land's Edge ammunition, and a Treasure Hunt cast this turn adds the run of lands on
    // top of the library (clairvoyant). Relocated verbatim from TurnSolver::Solve so the search
    // stays byte-identical; only the model is now archetype-owned (the engine keeps the win-check).
    const int active = s.active_player_index;

    // Land's Edge rate already on the battlefield (damage per land discarded).
    int lands_edge_rate = 0;
    for (const Permanent& p : s.battlefield)
    {
        if (p.controller_index != active) { continue; }
        const CardDefinition* def = CardDatabase::Instance().LookupCached(p.card);
        if (def && def->params.discard_land_damage > 0)
        {
            lands_edge_rate = std::max(lands_edge_rate, def->params.discard_land_damage);
        }
    }
    int lands_in_hand = 0;
    for (const Card& c : s.players[active].hand)
    {
        const CardDefinition* def = CardDatabase::Instance().LookupCached(c);
        if (def ? def->card.IsLand() : c.IsLand()) { ++lands_in_hand; }
    }
    // Clairvoyant count of consecutive lands on top of the library (what a Treasure Hunt cast
    // this turn would draw into hand, minus the triggering nonland).
    int th_lands_estimate = 0;
    for (const Card& c : s.players[active].library)
    {
        const CardDefinition* def = CardDatabase::Instance().LookupCached(c);
        bool is_land = def ? def->card.IsLand() : c.IsLand();
        if (!is_land) { break; }
        ++th_lands_estimate;
    }

    int base_lands_edge_dmg = lands_in_hand * lands_edge_rate;
    int plan_le_dmg         = 0;
    for (const CardDefinition* c : casting)
    {
        if (!c) { continue; }
        // Land's Edge being cast with none on board yet: this plan enables it.
        if (lands_edge_rate == 0 && c->params.discard_land_damage > 0)
        {
            plan_le_dmg += lands_in_hand * c->params.discard_land_damage;
        }
        // Treasure Hunt with Land's Edge already on board: th_lands_estimate new ammo lands.
        if (c->tmpl == CardTemplate::DrawUntilNonland)
        {
            int active_rate = (lands_edge_rate > 0) ? lands_edge_rate : 0;
            if (active_rate > 0) { plan_le_dmg += th_lands_estimate * active_rate; }
        }
    }
    // Second pass: TH + Land's Edge both cast this plan (none on board) -> add the TH bonus
    // lands at Land's Edge's rate (2). Mirrors the original Solve second pass exactly.
    if (lands_edge_rate == 0)
    {
        bool has_le = false, has_th = false;
        for (const CardDefinition* c : casting)
        {
            if (!c) { continue; }
            if (c->params.discard_land_damage > 0)        { has_le = true; }
            if (c->tmpl == CardTemplate::DrawUntilNonland) { has_th = true; }
        }
        if (has_le && has_th) { plan_le_dmg += th_lands_estimate * 2; }
    }
    return base_lands_edge_dmg + plan_le_dmg;
}

bool TreasureHuntProvider::ArchetypeCardValue(const GameState& state, const CardDefinition& def,
                                              int DMG, int& out) const
{
    // Per-card value for the Treasure Hunt / Land's Edge combo, relocated verbatim from
    // TurnSolver::EvalCard so candidate ordering stays byte-identical; only the archetype
    // value is now provider-owned. The engine keeps the generic value for every other card.
    if (def.tmpl == CardTemplate::DrawUntilNonland)
    {
        // Estimate how many lands TH will draw (clairvoyant scan of the library top).
        int estimated_lands = 0;
        for (const Card& c : state.ActivePlayer().library)
        {
            const CardDefinition* cdef = CardDatabase::Instance().LookupCached(c);
            bool is_land = cdef ? cdef->card.IsLand() : c.IsLand();
            if (!is_land) { break; }
            ++estimated_lands;
        }
        // Check for enabling permanents on the battlefield.
        bool has_no_max_hand = false;
        bool has_lands_edge  = false;
        int  lands_edge_rate = 0;
        for (const Permanent& p : state.battlefield)
        {
            if (p.controller_index != state.active_player_index) { continue; }
            const CardDefinition* pdef = CardDatabase::Instance().LookupCached(p.card);
            if (!pdef) { continue; }
            if (pdef->params.no_max_hand_size) { has_no_max_hand = true; }
            if (pdef->params.discard_land_damage > 0)
            {
                has_lands_edge  = true;
                lands_edge_rate = pdef->params.discard_land_damage;
            }
        }
        // With Land's Edge active, each drawn land converts to direct damage.
        if (has_lands_edge) { out = (estimated_lands + 1) * lands_edge_rate * DMG; return true; }
        // With Reliquary Tower (no max hand size) but no Land's Edge, the drawn lands
        // accumulate for a future LE activation. Card-draw value only.
        if (has_no_max_hand) { out = (estimated_lands + 1) * DMG; return true; }
        // No enabler in play: value the draw normally (the lands accumulate in hand).
        out = (estimated_lands + 1) * DMG; return true;
    }

    // Land's Edge: each land already in hand is worth discard_land_damage damage.
    if (def.params.discard_land_damage > 0)
    {
        int lands_in_hand = 0;
        for (const Card& c : state.ActivePlayer().hand)
        {
            const CardDefinition* cdef = CardDatabase::Instance().LookupCached(c);
            if (cdef && cdef->card.IsLand()) { ++lands_in_hand; }
        }
        out = lands_in_hand * def.params.discard_land_damage * DMG;
        return true;
    }
    return false;   // not an archetype card -> EvalCard's generic estimate applies.
}

// ---- VialProvider -----------------------------------------------------------

bool VialProvider::WantVialCharge(const GameState& s, const Permanent& vial) const
{
    return ::WantVialCharge(s, vial);
}

// ---- instances + selection --------------------------------------------------

namespace
{
    // Stateless, read-only -> single shared const instances are thread-safe (same model as
    // CardDatabase). Process lifetime, so GameState's raw pointer stays valid.
    const GenericProvider      g_generic;
    const AntiLifegainProvider g_antilife;
    const TreasureHuntProvider g_treasure;
    const VialProvider         g_vial;
}

const DecisionProvider& DefaultProvider()
{
    return g_generic;
}

const DecisionProvider& SelectDecisionProvider(const Decklist& deck)
{
    // Archetype detection by card params (same shape as GoldFishRunner::DeckUsesSecondMain).
    // Order matters only if a deck mixed signatures; today each is exclusive (verified).
    bool anti = false, th = false, vial = false;
    for (const Card& c : deck.mainboard)
    {
        const CardDefinition* def = CardDatabase::Instance().LookupCached(c);
        if (!def) { continue; }
        const CardParams& p = def->params;

        if (p.lifegain_to_loss || p.verse_damage || p.alt_lifegain_cost > 0
            || p.tutor_to_hand || p.tutor_to_top || !p.fetch_land_types.empty())
        {
            anti = true;
        }
        if (p.discard_land_damage > 0 || p.etb_scry > 0 || p.etb_surveil > 0
            || p.cycling_cost.has_value() || p.sacrifice_draw_cost.has_value()
            || def->tmpl == CardTemplate::DrawUntilNonland)
        {
            th = true;
        }
        if (p.upkeep_adds_charge) { vial = true; }
    }

    if (anti) { return g_antilife; }
    if (th)   { return g_treasure; }
    if (vial) { return g_vial; }
    return g_generic;
}
