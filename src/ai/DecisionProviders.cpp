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
//
// Now opens ALL the BRANCH-NARROWING gates (the audit tool for evaluating heuristic state):
//   - ShouldCastDrawEngine / ShouldEmitRiskyAltPayload : un-gate the cast (here).
//   - ShouldConsiderDig                                : always consider a dig (here).
//   - Tutor / Fetch candidate sets (shared SpellEffects.h ::TutorCandidates/::FetchCandidates):
//     return EVERY legal target instead of the heuristic-narrowed pick, and TurnSolver lifts
//     its fetch-target search cap. So the search branches over every tutor/fetch target.
// Expect a large branching blow-up -- run with a high budget. Pure DECISION/POLICY hooks that
// pick ONE option the search never alternatives over (cast-ORDER, vial-charge, scry-keep,
// discard-order, combat) are NOT yet opened here: making the search branch on them needs new
// enumeration (the ordering/combat work items), not just a wider gate.
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
GenericProvider::TutorCandidates(const GameState& s, int controller, const CardParams& pp) const
{
    // Search-primary default: return EVERY legal tutor target (distinct library card names
    // matching the tutor's type filter) and let the search pick the best. There is no
    // deck-agnostic tutor heuristic worth encoding (the only narrowing logic -- enabler vs.
    // wincon -- is antilife-specific, so it lives in AntiLifegainProvider). A deck that needs
    // its tutor narrowed for perf adds a provider override via the analyze-deck workflow;
    // until then the general search decides, never whiffs. (Previously returned {} -> a
    // generic tutor silently fetched nothing.)
    const Player& ap = s.players[controller];
    std::vector<std::string>        all;
    std::unordered_set<std::string> seen;
    for (const Card& lc : ap.library)
    {
        const CardDefinition* def = CardDatabase::Instance().LookupCached(lc);
        const Card&           card = def ? def->card : lc;
        // Empty tutor_types == no restriction ("search for a card", e.g. Gamble): every card is
        // a legal target. A non-empty filter keeps only the matching types (Idyllic/Enlightened).
        bool type_ok = pp.tutor_types.empty();
        for (const std::string& t : pp.tutor_types)
        { if (CardMatchesTypeName(card, t)) { type_ok = true; break; } }
        if (type_ok && seen.insert(lc.m_name).second) { all.push_back(lc.m_name); }
    }
    return all;
}

std::vector<std::string>
GenericProvider::FetchCandidates(const GameState& s, int controller, const CardParams& fetch_pp) const
{
    // Search-primary default: return EVERY legal fetch target (distinct library land names
    // whose subtypes match the fetchland) and let the search pick. The color-fixing heuristic
    // in ::FetchCandidates is tuned to a specific 4-colour shell (its tiebreaks favour that
    // deck's doubled colours), so it is NOT a safe deck-agnostic default; it stays an archetype
    // override. A generic fetchland deck thus searches its fetch targets rather than whiffing.
    // (Previously returned {} -> a generic fetch paid 1 life and fetched nothing.)
    const Player& ap = s.players[controller];
    std::vector<std::string>        all;
    std::unordered_set<std::string> seen;
    for (const Card& lc : ap.library)
    {
        const CardDefinition* d    = CardDatabase::Instance().LookupCached(lc);
        const Card&           card = d ? d->card : lc;
        if (!card.IsLand()) { continue; }
        bool match = false;
        for (const std::string& want : fetch_pp.fetch_land_types)
        {
            for (const std::string& cs : card.m_subtypes) { if (cs == want) { match = true; break; } }
            if (match) { break; }
        }
        if (match && seen.insert(lc.m_name).second) { all.push_back(lc.m_name); }
    }
    return all;
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

bool GenericProvider::ShouldAttackWith(const GameState&, const Permanent&) const
{
    return true;    // goldfish default: attack with everything that can attack (no blockers).
}

int GenericProvider::CastOrderRank(const GameState&, const CardDefinition& def) const
{
    // See DecisionProvider.h Hook 17. Reliable deck-agnostic order so the canonical line
    // realises what EnumeratePlans projects (prowess), at no search cost. Tiers (lower =
    // earlier):
    //   10 creatures: before noncreature spells, so a haste prowess creature catches the
    //      later noncreature casts' prowess triggers and attacks bigger.
    //   20 other noncreature spells.
    //   30 on-cast SELF-damage sources (Eidolon of the Great Revel): LAST, so this turn's
    //      other MV<=3 casts (already resolved) don't trigger its self-ping.
    // NOTE: this rank is only applied to cast sets with NO re-solve breakpoint (draw/staging/
    // cascade) card -- see OrderingOpaque / the canonical branches. Draw-engine turns keep
    // their plan/breakpoint order, whose post-draw re-solve is order-sensitive in ways a
    // static rank can't capture (verified: a "draw first" rank fixes some games and breaks
    // others); that ambiguous ordering is left to the search.
    //    5 non-creature mana rocks (Sol Ring): EARLIEST, so the rock's mana is online for the
    //      rest of the line (the same-turn ramp the enumerator now credits). Gated on the rock-
    //      ramp flag so MTG_NO_ROCK_RAMP keeps the legacy order (rocks ranked with noncreatures).
    if (def.params.on_cast_trigger_damage > 0) { return 30; }
    if (RockRampEnumEnabled() && def.params.mana_rock && !def.card.IsCreature()) { return 5; }
    if (def.card.IsCreature())                 { return 10; }
    return 20;
}

std::vector<int> GenericProvider::XCandidates(const GameState&, const CardDefinition&,
                                              int max_affordable) const
{
    // See DecisionProvider.h Hook 18. In a goldfish, an {X} spell (X burn, X draw, X pump)
    // wants all available mana: a larger X is never worse for closing the game. So the prune
    // proposes the single max-affordable value -- no branching. MTG_UNPRUNED opens the full
    // 1..max range so the unpruned-vs-pruned A/B can confirm the prune leaves nothing behind
    // (e.g. a turn where holding mana for a second spell beats max-X). Empty when X must be 0.
    if (max_affordable <= 0) { return {}; }
    if (DecisionUnpruned())
    {
        std::vector<int> all;
        all.reserve(max_affordable);
        for (int x = 1; x <= max_affordable; ++x) { all.push_back(x); }
        return all;
    }
    return { max_affordable };
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

// Total power of the controller's creatures that can still attack this turn (untapped, not
// summoning-sick). Used by the Reverent-Silence lethal checks below so the "free payload + this
// turn's swing finishes the opponent" formula is identical at emission and at auto-fire time.
static int ReadyAttackPower(const GameState& s, int controller)
{
    int atk = 0;
    for (const Permanent& p : s.battlefield)
    {
        if (p.controller_index == controller && p.card.IsCreature() && p.CanAttack())
        {
            int pw = p.EffectivePower();
            if (pw > 0) { atk += pw; }
        }
    }
    return atk;
}

bool AntiLifegainProvider::CanAutoFireAltPayload(const GameState& s, int controller,
                                                 const CardDefinition& def) const
{
    if (::CanAutoFireAltPayload(s, controller, def)) { return true; }  // safe payloads (Invigorate/Skyshroud)

    // Same-turn enabler -> Reverent Silence LETHAL combo. ::CanAutoFireAltPayload refuses ANY
    // destroy_all_enchantments payload (it wipes our own Aria/Remedy, so it is normally a SEARCH
    // choice via ShouldEmitRiskyAltPayload). But when it is LETHAL this turn the wipe is moot (the
    // game ends), so it becomes a safe auto-fire here. This loop runs AFTER the plan's casts
    // resolve, so a Tainted Remedy / Plague Drone cast THIS turn (enabler-first) is already live --
    // closing the "cast the enabler + free-cast Reverent Silence the same turn for the kill" gap
    // that collection-time emission (gated on a Remedy already active) cannot express.
    if (def.params.alt_lifegain_cost <= 0 || !def.params.destroy_all_enchantments) { return false; }
    if (!::RemedyActive(s, controller)) { return false; }
    if (!::ControlsSubtype(s, controller, def.params.alt_cost_requires_subtype)) { return false; }
    return s.players[1 - controller].life <= def.params.alt_lifegain_cost + ReadyAttackPower(s, controller);
}

bool AntiLifegainProvider::CastEnablerFirst(const GameState&, const std::string& card_name) const
{
    // Enabler-first: lifegain_to_loss cards (Tainted Remedy / Plague Drone) cast + resolve
    // before payloads so a same-turn payload sees the enabler active.
    return ::IsLifegainToLossCard(card_name);
}

int AntiLifegainProvider::CastOrderRank(const GameState& s, const CardDefinition& def) const
{
    // Enabler-first (Tainted Remedy / Plague Drone) so a same-turn payload resolves with the
    // lifegain->loss flip already active; otherwise the generic ranks.
    if (CastEnablerFirst(s, def.card.m_name)) { return 0; }
    return GenericProvider::CastOrderRank(s, def);
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
    return s.players[1 - controller].life <= def.params.alt_lifegain_cost + ReadyAttackPower(s, controller);
}

// ---- TreasureHuntProvider ---------------------------------------------------

bool TreasureHuntProvider::HasAnyDigSource (const GameState& s) const { return ::HasAnyDigSource(s); }
bool TreasureHuntProvider::ShouldConsiderDig(const GameState& s) const
{
    // Unpruned audit: consider a dig whenever a dig source exists, instead of the
    // affordability/flood heuristic gating it. See DecisionUnpruned.
    if (DecisionUnpruned()) { return ::HasAnyDigSource(s); }
    return ::ShouldConsiderDig(s);
}
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
            bool is_dork = (d->tmpl == CardTemplate::ManaDork && p.CanTap()) || d->params.mana_rock;
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

// ---- HinataProvider ---------------------------------------------------------

std::vector<std::string>
HinataProvider::TutorCandidates(const GameState& s, int controller, const CardParams& pp) const
{
    // Unpruned A/B: do not narrow -- let the search branch over every legal tutor target.
    if (DecisionUnpruned()) { return GenericProvider::TutorCandidates(s, controller, pp); }

    // Already have Hinata in play or hand? The payoffs are live -> search the full set for the
    // missing piece. Otherwise the deck is dead without her, so fetch Hinata if she's findable.
    bool have_hinata = HinataInPlay(s);
    if (!have_hinata)
    {
        for (const Card& c : s.players[controller].hand)
        {
            const CardDefinition* d = CardDatabase::Instance().LookupCached(c);
            if (d && d->params.hinata_cost_reducer) { have_hinata = true; break; }
        }
    }
    if (!have_hinata)
    {
        for (const Card& lc : s.players[controller].library)
        {
            const CardDefinition* d = CardDatabase::Instance().LookupCached(lc);
            if (d && d->params.hinata_cost_reducer) { return { lc.m_name }; }   // decided: fetch her
        }
        // Hinata not in library (all copies drawn/played but none counted above is rare) -> fall through.
    }

    // Hinata is online: return the full legal set (search-primary -- still branches over everything),
    // but ORDER it by situational need (Hook 19). The plan tie-break is win-turn then plan.value, and
    // every tutor candidate shares the tutor spell's eval, so win-turn-equal fetches tie on value and
    // the FIRST listed wins. Ordering by SituationalCardRank therefore makes an indifferent
    // (clairvoyant-tie) search fetch the most-wanted MISSING piece -- e.g. Reality Spasm (rank 750)
    // over a third Crackle when two are already in hand (rank 150, duplicate) -- instead of an
    // arbitrary library-order card. Pure tie-break: a fetch that wins strictly sooner still wins.
    std::vector<std::string> cands = GenericProvider::TutorCandidates(s, controller, pp);
    auto rank_of = [&](const std::string& name) -> int
    {
        for (const Card& lc : s.players[controller].library)
        { if (lc.m_name == name) { return SituationalCardRank(s, lc); } }
        return 0;
    };
    std::stable_sort(cands.begin(), cands.end(),
                     [&](const std::string& a, const std::string& b) { return rank_of(a) > rank_of(b); });
    return cands;
}

int HinataProvider::CastOrderRank(const GameState& s, const CardDefinition& def) const
{
    // Irencrag Feat restricts further casts ("you can cast only one more spell this turn"), so it
    // must be the LAST ritual: cast AFTER other rituals (Reality Spasm, 15) but BEFORE the payoff
    // (Crackle, 20), so the only spell that follows it is the payoff. Rank 18 -> the cast order and
    // the max_casts_after check in Solve::consider agree on ...Reality Spasm -> Irencrag -> Crackle.
    if (def.params.max_casts_after >= 0) { return 18; }
    // A mana ritual must resolve BEFORE the payoff X-spell so its floating mana is available to
    // pay the bigger Crackle. Rank it between creatures (Hinata, 10) and other noncreatures
    // (Crackle, 20). Everything else uses the generic order.
    if (IsManaRitual(def)) { return 15; }
    return GenericProvider::CastOrderRank(s, def);
}

// Situational "what do I need THIS turn" ranking (Hook 19). HIGHER = more wanted. The decisive
// idea is that situational NEED overrides static card power: a land tops the list on a turn we need
// the land drop (even though a land is a generically weak card), and once mana is covered the
// MISSING combo pieces outrank the digging cantrips, which outrank the dead/duplicate payoffs.
// Used to ORDER the cards a dig spell (Expressive Iteration / Ponder / Preordain) looks at, so the
// selection is deterministic (no search branch) and combo-aware. ScryKeepOnTop below is a threshold
// on this rank, so the keep/bottom gate and the ordering share one source of truth.
//
// Tiers (named so the relative order is the contract, not the magnitudes):
namespace
{
    enum HinataRank
    {
        kRankHinataLynchpin = 1000,  // Hinata when not yet online -- the deck is dead without her
        kRankNeededLand     =  900,  // a land when we need this turn's drop and are land-light
        kRankMissingCrackle =  800,  // the lethal finisher, not yet in hand (Hinata online)
        kRankMissingSpasm   =  750,  // Reality Spasm (the ritual that powers the lethal X)
        kRankIrencragShort  =  720,  // Irencrag Feat when MANA-SHORT: a mana ritual beats Soulfire
        kRankSoulfire       =  700,  // Soulfire Eruption: digs AND finishes
        kRankIrencrag       =  650,  // Irencrag Feat (mana not the bottleneck): more mana, but late
        kRankMagma          =  600,  // Magma Opus: secondary payoff (tokens + draw)
        kRankCantrip        =  500,  // Ponder / Preordain / EI -- keep digging toward pieces
        kRankRamp           =  450,  // a mana rock while still short of the mana target
        kRankExtraLand      =  380,  // a land beyond the urgent drop: still mana for the combo
        kRankFloodedLand    =  340,  // a land once well past the mana target (mild)
        kRankDeadPayoff     =  200,  // a payoff/ritual while Hinata is NOT online -- dead now
        kRankDigPastLand    =  250,  // a surplus land/rock while hunting Hinata -- dig past it
        kRankDuplicate      =  150,  // a second copy of a piece we already hold -- redundant
        kRankInert          =  100,  // goldfish-inert interaction / unknown
    };
    const int kHinataKeepThreshold = 300;   // ScryKeepOnTop = keep-on-top iff rank >= this
}

int HinataProvider::SituationalCardRank(const GameState& s, const Card& card) const
{
    const int active = s.active_player_index;
    const Player& ap = s.players[active];
    const CardDefinition* def = CardDatabase::Instance().LookupCached(card);
    const Card& c = def ? def->card : card;

    const bool is_land     = c.IsLand();
    const bool is_hinata   = def && def->params.hinata_cost_reducer;
    const bool is_rock     = def && def->params.mana_rock;
    const bool is_ritual   = def && IsManaRitual(*def);                 // Reality Spasm / Irencrag
    const bool is_spasm    = def && def->params.untap_x_mana_sources;   // Reality Spasm
    const bool is_crackle  = def && def->params.x_damage_multiplier > 1;// Crackle with Power (5X)
    const bool is_soulfire = def && def->params.damage_equals_top_mv;   // Soulfire Eruption
    const bool is_magma    = def && def->params.cast_draw > 0;          // Magma Opus (draw payoff)
    const bool is_cantrip  = def && (def->params.cast_scry > 0 || def->params.cast_reorder > 0
                                     || def->params.expressive_iteration);
    const bool is_payoff   = is_crackle || is_soulfire || is_magma;

    // Hinata online (battlefield or hand)? Determines whether the payoffs are live.
    bool have_hinata = HinataInPlay(s);
    if (!have_hinata)
    {
        for (const Card& h : ap.hand)
        {
            const CardDefinition* d = CardDatabase::Instance().LookupCached(h);
            if (d && d->params.hinata_cost_reducer) { have_hinata = true; break; }
        }
    }

    // Do we already hold a card matching `pred` in hand (duplicate demotion)?
    auto have_in_hand = [&](bool (*pred)(const CardParams&)) -> bool
    {
        for (const Card& h : ap.hand)
        {
            const CardDefinition* d = CardDatabase::Instance().LookupCached(h);
            if (d && pred(d->params)) { return true; }
        }
        return false;
    };

    // Mana sources in play and this turn's land-drop need.
    int sources = 0;
    for (const Permanent& p : s.battlefield)
    {
        if (p.controller_index != active) { continue; }
        const CardDefinition* d = CardDatabase::Instance().LookupCached(p.card);
        if (d && (p.card.IsLand() || d->params.mana_rock)) { ++sources; }
    }
    // The combo wants a lot of mana once Hinata is online (more sources = more Reality Spasm
    // refloat); before her we just need enough to cast her ({1}{U}{R}{W} = four sources).
    const int  source_target  = have_hinata ? 7 : 4;
    const bool land_drop_open = ap.lands_played_this_turn < ap.LandDropsAvailable();
    bool land_in_hand = false;
    for (const Card& h : ap.hand)
    {
        const CardDefinition* d = CardDatabase::Instance().LookupCached(h);
        if (d ? d->card.IsLand() : h.IsLand()) { land_in_hand = true; break; }
    }
    const bool need_land = land_drop_open && sources < source_target;

    // --- the lynchpin / a needed land outrank everything else ---
    if (is_hinata) { return have_hinata ? kRankDuplicate : kRankHinataLynchpin; }
    if (is_land)
    {
        if (need_land && !land_in_hand) { return kRankNeededLand; }          // land-light, no drop in hand
        if (sources < source_target)   { return kRankExtraLand; }           // more mana still helps
        return have_hinata ? kRankFloodedLand : kRankDigPastLand;           // flooded: keep (combo) / dig (hunt)
    }

    // --- before Hinata: payoffs/rituals are DEAD; dig past them, keep ramp + cantrips ---
    if (!have_hinata)
    {
        if (is_rock)                { return sources < source_target ? kRankRamp : kRankDigPastLand; }
        if (is_cantrip)             { return kRankCantrip; }    // keep digging for her
        if (is_payoff || is_ritual) { return kRankDeadPayoff; } // uncastable until she lands
        return kRankInert;
    }

    // --- Hinata online: the payoffs are live. Missing pieces > digging > duplicates. ---
    if (is_crackle)
    {
        const bool dup = have_in_hand([](const CardParams& p) { return p.x_damage_multiplier > 1; });
        return dup ? kRankDuplicate : kRankMissingCrackle;
    }
    if (is_spasm)
    {
        const bool dup = have_in_hand([](const CardParams& p) { return p.untap_x_mana_sources; });
        return dup ? kRankDuplicate : kRankMissingSpasm;
    }
    if (is_soulfire) { return kRankSoulfire; }
    // Irencrag Feat (fixed ritual burst). When mana is the bottleneck it outranks Soulfire -- the
    // shortage is exactly what the +7 mana fixes, and a {6}{R}{R}{R} Soulfire is uncastable while
    // short anyway; otherwise it ranks below the dig (Soulfire finds pieces, Irencrag is just mana).
    if (is_ritual)   { return (sources < source_target) ? kRankIrencragShort : kRankIrencrag; }
    if (is_magma)    { return kRankMagma; }
    if (is_cantrip)  { return kRankCantrip; }
    if (is_rock)     { return sources < source_target ? kRankRamp : kRankDigPastLand; }
    return kRankInert;
}

bool HinataProvider::ScryKeepOnTop(const GameState& s, const Card& top_card) const
{
    // One source of truth: keep on top exactly the cards the situational ranking wants this turn.
    // (Reproduces the previous keep/bottom decisions in both phases -- dig hard for Hinata before
    // she lands, keep the live pieces after -- while the rank ALSO orders the kept cards for the
    // dig spells, which the old binary keep could not.)
    return SituationalCardRank(s, top_card) >= kHinataKeepThreshold;
}

bool HinataProvider::KeepReorderTop(const GameState& s, const std::vector<Card>& top) const
{
    if (top.empty()) { return false; }
    const int active = s.active_player_index;
    const Player& ap = s.players[active];

    // Hinata online (in play or hand)? Once she is, the pieces are live and worth holding, so fall
    // back to the generic "keep iff any card is individually wanted" rule.
    bool have_hinata = HinataInPlay(s);
    if (!have_hinata)
    {
        for (const Card& h : ap.hand)
        {
            const CardDefinition* d = CardDatabase::Instance().LookupCached(h);
            if (d && d->params.hinata_cost_reducer) { have_hinata = true; break; }
        }
    }
    if (have_hinata)
    {
        for (const Card& c : top) { if (ScryKeepOnTop(s, c)) { return true; } }
        return false;
    }

    // --- Missing Hinata: she is in a class of her own -- without her the combo and even an
    // affordable Soulfire are unreachable, so a top set is only worth keeping if it advances toward
    // her: it contains Hinata herself, OR a dig/tutor toward her PLUS at least one other useful card
    // (a land/rock we still need for mana, or a holdable combo piece). A lone dig amid dead cards is
    // NOT enough -- shuffle and dig fresh for her. ---

    // Mana sources in play + this turn's land-drop need (mirrors SituationalCardRank).
    int sources = 0;
    for (const Permanent& p : s.battlefield)
    {
        if (p.controller_index != active) { continue; }
        const CardDefinition* d = CardDatabase::Instance().LookupCached(p.card);
        if (d && (p.card.IsLand() || d->params.mana_rock)) { ++sources; }
    }
    const int  source_target  = 4;   // enough to cast Hinata ({1}{U}{R}{W})
    const bool land_drop_open = ap.lands_played_this_turn < ap.LandDropsAvailable();
    const bool need_land      = land_drop_open && sources < source_target;

    bool has_hinata = false;
    int  dig = 0, other_useful = 0;
    for (const Card& c : top)
    {
        const CardDefinition* d = CardDatabase::Instance().LookupCached(c);
        if (!d) { continue; }
        const CardParams& p = d->params;
        if (p.hinata_cost_reducer) { has_hinata = true; continue; }
        const bool is_dig = p.cast_scry > 0 || p.cast_reorder > 0 || p.expressive_iteration
                          || p.tutor_to_hand || p.tutor_to_top;
        if (is_dig) { ++dig; continue; }
        // A non-dig card is "useful other" if it helps reach or execute the combo: a land/rock we
        // still need for mana, or a combo piece (Crackle / Soulfire / Magma / a mana ritual) worth
        // holding for after she lands. Surplus lands and goldfish-inert cards are dead weight here.
        const bool is_land  = d->card.IsLand();
        const bool is_rock  = p.mana_rock;
        const bool is_piece = (p.x_damage_multiplier > 1) || p.damage_equals_top_mv
                            || p.cast_draw > 0 || IsManaRitual(*d);
        if (is_land || is_rock) { if (need_land || sources < source_target) { ++other_useful; } }
        else if (is_piece)      { ++other_useful; }
    }
    if (has_hinata) { return true; }
    // dig + (a second dig OR a useful other) -> keep; a lone dig or no dig at all -> shuffle.
    return dig >= 1 && (dig + other_useful) >= 2;
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
    const HinataProvider       g_hinata;
}

const DecisionProvider& DefaultProvider()
{
    return g_generic;
}

const DecisionProvider& SelectDecisionProvider(const Decklist& deck)
{
    // Archetype detection by card params (same shape as GoldFishRunner::DeckUsesSecondMain).
    // Order matters only if a deck mixed signatures; today each is exclusive (verified).
    bool anti = false, th = false, vial = false, hinata = false;
    for (const Card& c : deck.mainboard)
    {
        const CardDefinition* def = CardDatabase::Instance().LookupCached(c);
        if (!def) { continue; }
        const CardParams& p = def->params;

        // Hinata, Dawn-Crowned's cost-reduction static is the deck's defining signature.
        if (p.hinata_cost_reducer) { hinata = true; }

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

    if (hinata) { return g_hinata; }
    if (anti) { return g_antilife; }
    if (th)   { return g_treasure; }
    if (vial) { return g_vial; }
    return g_generic;
}
