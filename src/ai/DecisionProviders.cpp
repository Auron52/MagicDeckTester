#include <cstdlib>
#include <cctype>
#include <utility>
#include <atomic>
#include <cstdint>
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
// Human-play suppression, shared by both the global and per-gate forms: in a --claude-play
// session (MTG_HUMAN_PLAY set) the engine's clairvoyant bottoming/keep rollout is an ENGINE
// decision the human never makes, so un-pruning is suppressed there (a HumanPlaySuppress guard
// is live) -> the kept hand reproduces the real gated d5 game. A pure autonomous audit (no
// human-play) is unaffected.
static bool UnpruneHumanSuppressed()
{
    static const bool hp = std::getenv("MTG_HUMAN_PLAY") != nullptr;
    return hp && g_human_play_suppressed;
}

// Canonical gate name <-> enum table, shared by the MTG_UNPRUNE parser and the gate-probe report.
static const std::pair<const char*, UnprunedGate> kGateNames[] = {
    {"altpayload", UnprunedGate::AltPayload}, {"tutor",     UnprunedGate::Tutor},
    {"fetch",      UnprunedGate::Fetch},      {"dig",       UnprunedGate::Dig},
    {"xspell",     UnprunedGate::XSpell},     {"ponder",    UnprunedGate::Ponder},
    {"groupcap",   UnprunedGate::GroupCap},   {"comboline", UnprunedGate::ComboLine},
    {"searchorder",UnprunedGate::SearchOrder},{"redirect",  UnprunedGate::Redirect},
    {"drawengine", UnprunedGate::DrawEngine},
};

const char* GateName(UnprunedGate g)
{
    for (const auto& n : kGateNames) { if (n.second == g) { return n.first; } }
    return "?";
}

// Gate PROBE: skip sweeping gates that have no live decision point for a deck. When enabled, every
// DecisionUnpruned(gate) callsite that actually EXECUTES ORs its gate into a global mask (across all
// threads/games of a run). A gate never queried has no reachable callsite for this deck (no matching
// cards / no rituals / no dig source / ...), so opening it provably changes nothing -- skip it. A gate
// that IS queried may still be neutral, but can only be cleared by actually sweeping it. The `&&`/`||`
// short-circuits at the callsites mean the query only fires when the gate's guard condition holds
// (e.g. Ponder only when a cast_reorder card exists), so "queried" is a faithful "live for this deck".
static std::atomic<bool>     g_gate_probe{false};
static std::atomic<uint32_t> g_gates_queried{0};

void     SetGateProbe(bool on) { g_gate_probe.store(on); if (on) { g_gates_queried.store(0); } }
uint32_t QueriedGatesMask()    { return g_gates_queried.load(); }

// Parse MTG_UNPRUNE=<comma/space/;/| separated gate names> once into a bitmask over UnprunedGate.
// "all" (or the legacy MTG_UNPRUNED) sets every bit. Unknown tokens are ignored. Case-insensitive.
static uint32_t ParseUnpruneMask()
{
    const char* e = std::getenv("MTG_UNPRUNE");
    if (!e) { return 0; }
    std::string s = e;
    for (char& c : s) { c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                        if (c == ',' || c == ';' || c == '|') { c = ' '; } }
    auto bit = [](UnprunedGate g) { return 1u << static_cast<int>(g); };
    uint32_t mask = 0;
    std::size_t pos = 0;
    while (pos < s.size())
    {
        while (pos < s.size() && s[pos] == ' ') { ++pos; }
        std::size_t end = s.find(' ', pos);
        if (end == std::string::npos) { end = s.size(); }
        std::string tok = s.substr(pos, end - pos);
        pos = end;
        if (tok.empty()) { continue; }
        if (tok == "all") { mask = (1u << static_cast<int>(UnprunedGate::_Count)) - 1u; continue; }
        for (const auto& n : kGateNames) { if (tok == n.first) { mask |= bit(n.second); break; } }
    }
    return mask;
}

static uint32_t UnpruneMask()
{
    static const uint32_t m = ParseUnpruneMask();
    return m;
}

bool DecisionUnpruned()
{
    static const bool v = std::getenv("MTG_UNPRUNED") != nullptr;
    if (!v) { return false; }
    if (UnpruneHumanSuppressed()) { return false; }
    return true;
}

bool DecisionUnpruned(UnprunedGate g)
{
    // Gate probe: record that this gate has a REACHABLE callsite for the current deck (see the probe
    // comment above). Cheap relaxed OR, only when probing; normal runs pay one predictable branch.
    if (g_gate_probe.load(std::memory_order_relaxed))
    { g_gates_queried.fetch_or(1u << static_cast<int>(g), std::memory_order_relaxed); }
    if (DecisionUnpruned()) { return true; }         // global MTG_UNPRUNED opens every gate
    if (UnpruneHumanSuppressed()) { return false; }  // selective mode honours the same suppression
    return (UnpruneMask() >> static_cast<int>(g)) & 1u;
}

bool UseLearnedEval()
{
    static const bool v = std::getenv("MTG_EVAL_MODEL") != nullptr;
    return v;
}

bool UseValueModel()
{
    static const bool v = std::getenv("MTG_VALUE_MODEL") != nullptr;
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
    // Destroy-all-enchantments (Reverent Silence) wipes our OWN Aria/Remedy, so cast it LAST --
    // after this turn's wincon casts (Aria's lethal ETB reversal) have already resolved. Casting
    // it earlier can pre-empt a lethal line and, worse, let a later un-reversed lifegain rider
    // (Aria with the Remedy now gone) HEAL the opponent. Ranked alongside the self-damage tier.
    if (def.params.destroy_all_enchantments)   { return 30; }
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
    if (DecisionUnpruned(UnprunedGate::XSpell))
    {
        std::vector<int> all;
        all.reserve(max_affordable);
        for (int x = 1; x <= max_affordable; ++x) { all.push_back(x); }
        return all;
    }
    return { max_affordable };
}

int GenericProvider::ManaSourceRank(const GameState& s, const CardDefinition& def) const
{
    // See DecisionProvider.h Hook 24. Flexibility rank for the scarcity-first tap order (LOWER =
    // tap earlier). SPEND the least flexible first so the flexible sources stay available.
    const int active = s.active_player_index;
    // A COLOURLESS-only manland (Mutavault) has marginal mana (pays only generic) but real attack
    // value, so SAVE it: rank above even rainbow, so it's tapped only when nothing else can pay. (It
    // is still used when required; ranking it last just stops the greedy spending it on a pip a real
    // land could cover, which in the rollout was costing slivers a turn of Mutavault damage.) A
    // COLOURED manland (dual creature-land) has valuable fixing you tap for many turns before you'd
    // rather attack, so it falls through to the normal colour rank; holding it to attack is a
    // situational call left to the search, not this ordering.
    if (def.params.can_animate)
    {
        const std::vector<Color>& mprod = EffectiveProduces(s, active, def);
        bool has_colored = false;
        for (Color c : mprod) { if (c != Color::Colorless) { has_colored = true; break; } }
        if (!has_colored) { return 60; }
    }
    // Depletion lands (Saprazzan Skerry, Sandstone Needle) are deliberately NOT reserved: they are
    // RAMP you normally want to spend, so blanket-conserving them via the ordering would misfire far
    // more often than the rare "wasted a counter" case helps. They rank by colour like any land.
    if (def.params.is_filter || def.params.ramp_filter) { return 25; }
    const std::vector<Color>& prod = EffectiveProduces(s, active, def);
    const int amt = ManaProducedPerTap(def);
    if (amt > 1 && static_cast<int>(prod.size()) > 1) { return 10; }  // bounce/fixed-multi: no choice
    const int ncol = static_cast<int>(prod.size());
    int rank = ncol <= 1 ? 10 : ncol * 10;                            // mono=10 dual=20 tri=30 rainbow=50
    // Drip land (Grove of the Burnwillows, tap_opponent_lifegain > 0): its coloured tap gifts the
    // opponent 1 life, so among LANDS spend a painless source first and spare it (+1 -> one slot past its
    // own flexibility tier, i.e. last of a mono/dual land base). It stays AHEAD of the CREATURES (dorks,
    // 30+): a mana creature is usually worth more kept up (Invigorate pump target, lone-Exalted attacker,
    // repeatable fixing) than one avoided pre-enabler life gift, so on average we tap Grove before a dork
    // rather than burn the dork. Static / enabler-agnostic on purpose: with a lifegain->loss enabler the
    // drip becomes 1 damage that MUST fire, but that is guaranteed separately by DripLandAnyPipColor's
    // Remedy gate (taps COLOURED, never {C}) + the TapDripLandsIfUseful sweep -- NOT by tap order.
    // (Measured outcome-identical at searched depth to the old enabler-conditional nudge; ranking the
    // drip land AFTER the dorks instead was net-negative -- see the heuristic-optimization skill.) This
    // is the net-positive AVERAGE; the drip-land-vs-dork call is genuinely situational (an idle dork is
    // sometimes better tapped first), which a static rank can't capture -- left to future search.
    // Inert for every deck without a drip land.
    if (def.params.tap_opponent_lifegain > 0) { rank += 1; }
    return rank;
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
    // Human play (unpruned): offer EVERY legal target so the player picks the tutor's card
    // freely, not the enabler-then-wincon heuristic's single choice. Mirrors HinataProvider;
    // autonomous search is unchanged (DecisionUnpruned() is false there).
    if (DecisionUnpruned(UnprunedGate::Tutor)) { return GenericProvider::TutorCandidates(s, controller, pp); }
    return ::TutorCandidates(s, controller, pp);
}

std::vector<std::string>
AntiLifegainProvider::FetchCandidates(const GameState& state, int controller_index,
                                      const CardParams& fetch_pp) const
{
    // Deck-specific colour-fixing fetch heuristic (relocated here from a shared free function --
    // it is tuned to THIS 4-colour shell and must not live in standard code). It ranks fetch
    // targets by colour COVERAGE, not just the tiebreak order, and returns the single best pick
    // so the search never branches over fetch targets. GenericProvider returns every legal
    // target instead, so no other deck sees this logic.

    // Unpruned audit (MTG_UNPRUNED): return EVERY legal fetch target so the search branches over
    // all of them -- identical to the generic "return all matching library lands" path.
    if (DecisionUnpruned(UnprunedGate::Fetch))
    {
        return GenericProvider::FetchCandidates(state, controller_index, fetch_pp);
    }

    const Player& ap = state.players[controller_index];

    constexpr int NC = 6;   // Color enum cardinality (W,U,B,R,G,C)
    using ColorSet = std::array<bool, NC>;   // stack-resident; avoids per-call vector<bool> allocs + bit-proxy cost
    auto add_colors = [](ColorSet& set, const std::vector<Color>& cs)
    {
        for (Color c : cs) { set[static_cast<int>(c)] = true; }
    };

    // Colours we already have on the battlefield (lands + mana dorks/rocks we control). A mana dork
    // (incl. a produces-any Birds of Paradise) COUNTS as a source of the colours it makes -- that is
    // exactly why we don't over-fetch black once a dork can make it (see the conditional black
    // tiebreak below): "dorks can be that black". Also count how many DISTINCT sources make black vs
    // white, because the deck needs black only ONCE (one black source suffices to cast the payoffs)
    // but can want white TWICE in a turn (Fiery Justice {W} + Swords {W}) -- so a 2nd white source
    // has value a single 1-mana dork cannot supply, while a 2nd black source does not.
    ColorSet have{};
    int n_black_src = 0, n_white_src = 0;
    auto count_bw = [&](const std::vector<Color>& prod)
    {
        for (Color c : prod)
        {
            if (c == Color::Black) { ++n_black_src; }
            if (c == Color::White) { ++n_white_src; }
        }
    };
    for (const Permanent& p : state.battlefield)
    {
        if (p.controller_index != controller_index) { continue; }
        const CardDefinition* d = CardDatabase::Instance().LookupCached(p.card);
        if (!d) { continue; }
        if (!(d->card.IsLand() || d->tmpl == CardTemplate::ManaDork || d->params.mana_rock)) { continue; }
        add_colors(have, d->params.produces);
        count_bw(d->params.produces);
    }
    // Plus colours from OTHER (non-fetch) lands in hand -- part of the deck-fixing equation.
    ColorSet have_or_hand = have;
    for (const Card& c : ap.hand)
    {
        const CardDefinition* d = CardDatabase::Instance().LookupCached(c);
        if (!d || !d->card.IsLand() || !d->params.fetch_land_types.empty()) { continue; }
        add_colors(have_or_hand, d->params.produces);
        count_bw(d->params.produces);
    }
    const bool black_secured = n_black_src > 0;   // one black source is enough for the wincon
    const bool want_more_white = n_white_src < 2; // a 2nd white source is still useful

    // Critical subtype (e.g. "Forest"): the subtype an alt-cost card in HAND requires
    // ("If you control a Forest, rather than pay ...") -- a Forest also makes {G} for the
    // dorks. We only weight it when we DON'T already control/hold that subtype.
    std::string crit_subtype;
    for (const Card& c : ap.hand)
    {
        const CardDefinition* d = CardDatabase::Instance().LookupCached(c);
        if (d && !d->params.alt_cost_requires_subtype.empty())
        { crit_subtype = d->params.alt_cost_requires_subtype; break; }
    }
    bool have_crit = crit_subtype.empty()
                  || ControlsSubtype(state, controller_index, crit_subtype);
    if (!have_crit)   // also satisfied by a non-fetch land of that subtype already in hand
    {
        for (const Card& c : ap.hand)
        {
            const CardDefinition* d = CardDatabase::Instance().LookupCached(c);
            if (!d || !d->card.IsLand() || !d->params.fetch_land_types.empty()) { continue; }
            for (const std::string& s : d->card.m_subtypes) { if (s == crit_subtype) { have_crit = true; break; } }
            if (have_crit) { break; }
        }
    }
    bool want_crit = !crit_subtype.empty() && !have_crit;

    // Proactive subtype-bank ("bank a Forest when there's no other immediate need"): the reactive
    // want_crit above fires only when the alt-cost payoff (Skyshroud Cutter / Invigorate) is in
    // HAND. But a Forest is worth banking BEFORE we draw the payoff -- and green from Grove /
    // dorks does NOT satisfy it (that is a colour, not the Forest SUBTYPE). So also detect the
    // subtype anywhere in the DECK (hand + library), and if we don't yet control/hold it, prefer a
    // candidate carrying it -- but as the LOWEST-priority key (below the coverage keys), so it
    // never pre-empts a colour we actually need this turn/deck-wide.
    std::string deck_crit = crit_subtype;
    if (deck_crit.empty())
    {
        for (const Card& c : ap.library)
        {
            const CardDefinition* d = CardDatabase::Instance().LookupCached(c);
            if (d && !d->params.alt_cost_requires_subtype.empty())
            { deck_crit = d->params.alt_cost_requires_subtype; break; }
        }
    }
    bool have_deck_crit = deck_crit.empty()
                       || ControlsSubtype(state, controller_index, deck_crit);
    if (!have_deck_crit)
    {
        for (const Card& c : ap.hand)
        {
            const CardDefinition* d = CardDatabase::Instance().LookupCached(c);
            if (!d || !d->card.IsLand() || !d->params.fetch_land_types.empty()) { continue; }
            for (const std::string& s : d->card.m_subtypes) { if (s == deck_crit) { have_deck_crit = true; break; } }
            if (have_deck_crit) { break; }
        }
    }
    bool bank_crit = !deck_crit.empty() && !have_deck_crit;

    // Colours wanted this turn (coloured pips of nonland cards in hand) and deck-wide. A {W} tutor
    // (Enlightened/Idyllic) in hand thus registers white in want_turn, so coverage fetches a white
    // source when white is genuinely wanted; no separate "white when a tutor is in hand" special case.
    ColorSet want_turn{}, want_deck{};
    auto note_cost = [&](const Card& card, ColorSet& set)
    {
        const ManaCost& mc = card.m_mana_cost;
        if (mc.white > 0)  { set[static_cast<int>(Color::White)] = true; }
        if (mc.blue  > 0)  { set[static_cast<int>(Color::Blue)]  = true; }
        if (mc.black > 0)  { set[static_cast<int>(Color::Black)] = true; }
        if (mc.red   > 0)  { set[static_cast<int>(Color::Red)]   = true; }
        if (mc.green > 0)  { set[static_cast<int>(Color::Green)] = true; }
    };
    for (const Card& c : ap.hand)
    {
        const CardDefinition* d = CardDatabase::Instance().LookupCached(c);
        const Card& card = d ? d->card : c;
        if (card.IsLand()) { continue; }
        note_cost(card, want_turn);
        note_cost(card, want_deck);
    }
    for (const Card& c : ap.library)
    {
        const CardDefinition* d = CardDatabase::Instance().LookupCached(c);
        const Card& card = d ? d->card : c;
        if (card.IsLand()) { continue; }
        note_cost(card, want_deck);
    }

    // Score each distinct candidate land (subtype in fetch_land_types) and keep the single best in
    // ONE PASS over the library (no candidate vector / dedup set / sort -- fetch-decision hot path).
    // A candidate beats the incumbent on the first differing key (all "higher is better"); a full
    // tie keeps the incumbent, which -- scanning the library in order -- is the earliest, exactly
    // reproducing the old sort-by-(keys desc, insertion order asc) + front(). Keys, highest first:
    //   gives_crit (carries the critical subtype we still need, e.g. Forest unlock)
    //   s_turn (new colours wanted THIS turn) / s_deck (deck-wide) / s_breadth (new colours)
    //   multi (fixes >1 colour) / dup_pref (colour-priority tiebreak) / shock (dual over basic)
    bool        have_best = false;
    std::string best_name;
    int b_gc = 0, b_st = 0, b_sd = 0, b_sb = 0, b_multi = 0, b_dup = 0, b_shock = 0;
    for (const Card& lc : ap.library)
    {
        const CardDefinition* d = CardDatabase::Instance().LookupCached(lc);
        const Card& card = d ? d->card : lc;
        if (!card.IsLand()) { continue; }
        bool type_ok = false;
        for (const std::string& want : fetch_pp.fetch_land_types)
        {
            for (const std::string& cs : card.m_subtypes) { if (cs == want) { type_ok = true; break; } }
            if (type_ok) { break; }
        }
        if (!type_ok) { continue; }

        int gives_crit = 0;
        if (want_crit)
        {
            for (const std::string& cs : card.m_subtypes) { if (cs == crit_subtype) { gives_crit = 1; break; } }
        }
        const std::vector<Color>& prod = d ? d->params.produces : std::vector<Color>{};
        int s_turn = 0, s_deck = 0, s_breadth = 0;
        for (Color c : prod)
        {
            int ci = static_cast<int>(c);
            if (want_turn[ci] && !have[ci])         { ++s_turn; }
            if (want_deck[ci] && !have_or_hand[ci]) { ++s_deck; }
            if (!have[ci] && ci != static_cast<int>(Color::Colorless)) { ++s_breadth; }
        }
        int multi = static_cast<int>(prod.size()) > 1 ? 1 : 0;
        bool pw = false, pg = false, pb = false, pr = false;
        for (Color c : prod)
        {
            if      (c == Color::White) { pw = true; }
            else if (c == Color::Green) { pg = true; }
            else if (c == Color::Black) { pb = true; }
            else if (c == Color::Red)   { pr = true; }
        }
        // Colour-priority tiebreak among coverage-equal targets -- the user's rule for THIS deck,
        // all else being equal, SUMMED so a land carrying several ranks higher ("when we can we get
        // multiple"), each rank weighted to dominate every lower one combined so the order is strict.
        // This is a LOW-priority key (below the coverage keys), so it only decides genuine ties (which
        // dual to grab first) and never strands a needed colour -- a colour not yet covered scores on
        // s_turn/s_deck/s_breadth first. Green is always top (it enables the {G} dorks + the free
        // alt-cost spells + Fiery Justice). Black is CONDITIONAL: the deck needs black only ONCE (a
        // single black source lets it cast the payoffs = the wincon), so black outranks white ONLY
        // while black is unsecured; once ANY source (incl. a dork -- "dorks can be that black") makes
        // black, a further black source is not useless (it frees the dork to attack) but ranks BELOW
        // white, because a 2nd WHITE source can still be used the same turn (Fiery Justice {W} + Swords
        // {W}) and a single 1-mana dork cannot supply two. White is thus itself gated on wanting a 2nd
        // source (want_more_white). Forest (the alt-cost subtype) is the finest term and counts only
        // while we still want one (bank_crit); green from Grove/dorks is a colour, not the subtype.
        //   black still needed:  Green(16) > Black(8) > White(4) > Red(2) > Forest(1)
        //   black not needed  :  Green(16) > White(4) > Red(2) > Black(1) > Forest(via bank_crit)
        // "Still needed" = black is unsecured AND we still run a black spell we want to cast
        // (want_deck[Black]) -- i.e. we have not yet secured our one black and there is a payoff/
        // enabler that requires it. Once black is secured (a land OR dork makes it) OR there is no
        // black spell left to cast (the enabler is already down and nothing black remains), a further
        // black source is not useless -- it frees the dork to attack -- but ranks BELOW red, per the
        // user: "if you've already cast your enabler, black is of very low priority."
        const int BLACK_I = static_cast<int>(Color::Black);
        const bool black_still_needed = !black_secured && want_deck[BLACK_I];
        int black_w = black_still_needed ? 8 : 1;
        int white_w = want_more_white ? 4 : 0;
        bool pf = false;
        if (bank_crit)
        {
            for (const std::string& s : card.m_subtypes) { if (s == deck_crit) { pf = true; break; } }
        }
        int dup_pref = (pg ? 16 : 0) + (pw ? white_w : 0) + (pr ? 2 : 0) + (pb ? black_w : 0) + (pf ? 1 : 0);
        int shock    = (d && d->params.etb_pay_life_to_untap > 0) ? 1 : 0;

        bool better;
        if      (gives_crit != b_gc)    { better = gives_crit > b_gc; }
        else if (s_turn     != b_st)    { better = s_turn     > b_st; }
        else if (s_deck     != b_sd)    { better = s_deck     > b_sd; }
        else if (s_breadth  != b_sb)    { better = s_breadth  > b_sb; }
        else if (multi      != b_multi) { better = multi      > b_multi; }
        else if (dup_pref   != b_dup)   { better = dup_pref   > b_dup; }
        else if (shock      != b_shock) { better = shock      > b_shock; }
        else                            { better = false; }  // full tie -> keep the earlier incumbent

        if (!have_best || better)
        {
            have_best = true;
            best_name = lc.m_name;
            b_gc = gives_crit; b_st = s_turn; b_sd = s_deck; b_sb = s_breadth;
            b_multi = multi;   b_dup = dup_pref; b_shock = shock;
        }
    }

    // Return exactly the single best (never a tied group) so a fetch is always decided by the
    // heuristic and the search never branches over fetch targets. Empty only on a true whiff.
    std::vector<std::string> out;
    if (have_best) { out.push_back(best_name); }
    return out;
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

    // Invigorate-type SAFE pump-alt with no READY own attacker. ::CanAutoFireAltPayload refused it
    // (its own-attacker requirement -- the pump is what a normal fire is FOR). But the free alt-cost
    // damage (opp gains N -> N loss under a Remedy) can itself be LETHAL this turn with the pump
    // moot, as long as a legal creature target exists: CR "target creature" is ANY creature -- an
    // opponent's, or our own tapped/summoning-sick one -- so the spell is castable (item-1 guard).
    // Fire it when it closes the game: opp life <= alt damage + ready attack power (0 when nothing
    // can attack). Unlike casting it EARLY for tempo (a clairvoyant, enabler-dependent gamble we do
    // NOT auto-fire), "lethal THIS turn" is deterministic from the current board -- no clairvoyance.
    // The rollout (FireSafeAltPayloads) and executor both apply the alt-cost and skip the moot pump,
    // staying in lockstep.
    // opp still ALIVE guard: "close out the game this turn" presupposes it is not already closed.
    // The auto-fire pass fires payloads greedily and re-scans the mutated board, so without this a
    // prior payload/attack that already dropped the opponent to <= 0 would still let Invigorate fire
    // a redundant overkill (changing the realised line for no gain). Fire only when Invigorate is the
    // actual closer: opp alive and its alt-cost damage (+ any ready attackers, though this branch is
    // only reached with none -- a ready attacker makes the first line fire the pump) is lethal.
    if (def.params.target_own_creature && !def.params.destroy_all_enchantments
        && ::RemedyActive(s, controller)
        && ::ControlsSubtype(s, controller, def.params.alt_cost_requires_subtype)
        && ::AltPayloadTargetLegal(s, def)
        && s.players[1 - controller].life > 0
        && s.players[1 - controller].life <= def.params.alt_lifegain_cost + ReadyAttackPower(s, controller))
    {
        return true;
    }

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
    if (DecisionUnpruned(UnprunedGate::AltPayload)) { return true; }   // unpruned A/B: let the search judge the wipe.
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

bool AntiLifegainProvider::OpponentLifegainUseful(const GameState& s, int controller) const
{
    // A lifegain->loss enabler (Tainted Remedy / Plague Drone) reverses the opponent's "gain 1" into
    // 1 DAMAGE, so seeking the Grove drip is useful exactly when one is active. (Future refinement: also
    // true when an enabler WILL be active by the time the drip resolves -- e.g. one is being cast this
    // turn -- so an early coloured Grove tap that turn is worth the gift.)
    return ::RemedyActive(s, controller);
}

// Effective ATTACKING power of a permanent, computed like the combat sites (PendingAttackDamage /
// SimulateCombat / GameEngine): base + temp pump (Invigorate) + counters + lord anthem + animate +
// dynamic. Used only to decide whether swinging a creature adds damage.
static int AttackPowerOf(const GameState& s, const Permanent& p)
{
    const int active = s.active_player_index;
    const bool animated = p.is_animated;
    const std::pair<int,int> lb = ComputeLordBonus(p.card, s.battlefield, active, animated, &p);
    int base = p.EffectivePower() + lb.first;
    const CardDefinition* d = CardDatabase::Instance().LookupCached(p.card);
    if (d)
    {
        if (animated) { base += d->params.animate_power; }
        base += DynamicBasePower(*d, s, active);
    }
    return base;
}

// Does declaring `p` as an attacker produce value BEYOND its raw power? (Attack-token creation, or
// being a beneficiary of a controlled attack_trigger_life_loss source matching its subtypes.) These
// creatures are worth attacking even at 0 power, so they are never held back.
static bool AttackHasNonPowerValue(const GameState& s, const Permanent& p)
{
    const int active = s.active_player_index;
    const CardDefinition* pd = CardDatabase::Instance().LookupCached(p.card);
    if (pd && pd->params.attack_creates_tokens > 0) { return true; }
    for (const Permanent& src : s.battlefield)
    {
        if (src.controller_index != active) { continue; }
        const CardDefinition* sd = CardDatabase::Instance().LookupCached(src.card);
        if (!sd || sd->params.attack_trigger_life_loss <= 0) { continue; }
        for (const std::string& sub : sd->params.subtypes_affected)
        {
            if (p.is_animated) { return true; }
            for (const std::string& cs : p.card.m_subtypes) { if (cs == sub) { return true; } }
        }
    }
    return false;
}

// Exalted-aware attack declaration (see the header note). Honoured in lockstep by every combat site
// (all gate on ShouldAttackWith): the projection, the rollout, and the real DeclareAttackers.
bool AntiLifegainProvider::ShouldAttackWith(const GameState& s, const Permanent& p) const
{
    // Default ON; off-switch MTG_NO_EXALTED_ATTACK reverts to generic attack-with-everything
    // (byte-identical to the pre-fix baseline) for A/B. Net win: +2-3% d0 wins and faster searched
    // avgs on Anti-Lifegain (the only exalted deck), 0 win<->loss. A handful of searched-depth games
    // win a turn LATER, but that was shown to be fetch-shuffle DRAW VARIANCE, not a bug: the more
    // accurate exalted valuation flips an early land tie-break, a fetchland reshuffles, and the game
    // draws differently. Among 462 games with IDENTICAL draw sequences, ON never wins later (0
    // regressions); every turn-later game has a divergent post-fetch draw. See the reservation design
    // doc's exalted section.
    static const bool enabled = std::getenv("MTG_NO_EXALTED_ATTACK") == nullptr;
    if (!enabled) { return true; }

    if (AttackPowerOf(s, p) > 0)        { return true; }   // deals damage (incl. an Invigorate-pumped dork)
    if (AttackHasNonPowerValue(s, p))   { return true; }   // attack-trigger value

    // p is a 0-power, no-trigger creature. Swinging it deals nothing and, worse, breaks the
    // lone-attacker Exalted bonus. Hold it unless it is the ONLY eligible attacker -- then a single
    // such creature swings to switch Exalted on. (Eligibility uses CanAttackFull, NOT ShouldAttackWith,
    // to avoid recursion; the pick is deterministic -- lowest battlefield index -- so all three combat
    // sites agree on which lone dork attacks.)
    const int active = s.active_player_index;
    const int n      = static_cast<int>(s.battlefield.size());
    int lone_idx = -1, p_idx = -1;
    for (int i = 0; i < n; ++i)
    {
        const Permanent& q = s.battlefield[i];
        if (q.controller_index != active) { continue; }
        if (&q == &p) { p_idx = i; }
        if (!CanAttackFull(q, s.battlefield, active)) { continue; }
        if (AttackPowerOf(s, q) > 0 || AttackHasNonPowerValue(s, q)) { return false; }  // real attacker exists -> hold p
        if (lone_idx < 0) { lone_idx = i; }
    }
    if (CountExalted(s.battlefield, active) <= 0) { return false; }   // pointless swing, no Exalted to earn
    return (p_idx == lone_idx);
}

// ---- TreasureHuntProvider ---------------------------------------------------

bool TreasureHuntProvider::HasAnyDigSource (const GameState& s) const { return ::HasAnyDigSource(s); }
bool TreasureHuntProvider::ShouldConsiderDig(const GameState& s) const
{
    // Unpruned audit: consider a dig whenever a dig source exists, instead of the
    // affordability/flood heuristic gating it. See DecisionUnpruned.
    if (DecisionUnpruned(UnprunedGate::Dig)) { return ::HasAnyDigSource(s); }
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
    if (DecisionUnpruned(UnprunedGate::DrawEngine)) { return true; }   // unpruned A/B: never gate the flood engine.
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
        // Cascade payoff (Throes of Chaos): a cascade card can cascade INTO Land's Edge, putting it
        // onto the battlefield for free, so the lands already in hand become lethal ammunition THIS
        // turn. Credit it when the cascade's target -- the first nonland in the library with mana
        // value < cascade_max_mv (cascade skips lands and higher-MV nonlands) -- is a Land's Edge.
        // Clairvoyant, like th_lands_estimate above; only when no Land's Edge is already on board
        // (else it is already counted in base_lands_edge_dmg). Simulation remains the win arbiter,
        // so this optimistic projection only steers the search toward the line (it does not commit
        // a phantom win). See docs/design/th-reliquary-defer-gi627.md.
        static const bool s_cascade_lethal = std::getenv("MTG_NO_CASCADE_LETHAL") == nullptr;
        if (s_cascade_lethal && lands_edge_rate == 0 && c->params.cascade_max_mv > 0)
        {
            for (const Card& lc : s.players[active].library)
            {
                const CardDefinition* ld = CardDatabase::Instance().LookupCached(lc);
                const Card&           card = ld ? ld->card : lc;
                if (card.IsLand()) { continue; }                                   // cascade skips lands
                if (card.m_mana_cost.ManaValue() >= c->params.cascade_max_mv) { continue; } // too costly: skipped
                if (ld && ld->params.discard_land_damage > 0)                       // target IS Land's Edge
                { plan_le_dmg += lands_in_hand * ld->params.discard_land_damage; }
                break;                                                             // first hittable nonland = target
            }
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

// ---- BurnProvider -----------------------------------------------------------

bool BurnProvider::PreferHoldLandDrop(const GameState& s, int controller) const
{
    // Burn's curve tops out at mana value 2, so ~2-3 lands cast the whole deck; a further land in
    // play adds no castable value. Once we control this many lands, BANK the next land in hand
    // instead of developing it, so a future topdecked Searing Blaze can play it for landfall (3 to
    // the face instead of 1). This only flips the EQUAL-VALUE land tiebreak (a flooded turn where
    // playing vs holding a land is indifferent), never a value decision -- the turn we actually
    // cast Blaze, playing the land raises the plan's value (landfall), so it still develops.
    constexpr int kBankThreshold = 3;
    int lands = 0;
    for (const Permanent& p : s.battlefield)
    {
        if (p.controller_index == controller && p.card.IsLand()) { ++lands; }
    }
    return lands >= kBankThreshold;
}

// ---- HinataProvider ---------------------------------------------------------

std::vector<std::string>
HinataProvider::TutorCandidates(const GameState& s, int controller, const CardParams& pp) const
{
    // Unpruned A/B: do not narrow -- let the search branch over every legal tutor target.
    if (DecisionUnpruned(UnprunedGate::Tutor)) { return GenericProvider::TutorCandidates(s, controller, pp); }

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

// Archetype gates relocated out of TurnSolver (audit B1/B2). Both branches only pay off with
// Hinata's per-target discount online: the untap ritual (Reality Spasm) floats mana for a same-turn
// Crackle the discount makes free, and Soulfire's own-creature targets each shave {1} (and dig
// deeper). Off Hinata they are dead weight, so the solver must not branch on them.
bool HinataProvider::ShouldEmitUntapRitual(const GameState& s) const     { return HinataInPlay(s); }
bool HinataProvider::BranchSoulfireOwnTargets(const GameState& s) const  { return HinataInPlay(s); }

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

    // --- Missing Hinata: she is in a class of her own -- without her the combo (and even an
    // affordable Soulfire) is unreachable, so KEEP the top only if Hinata herself is in it; otherwise
    // SHUFFLE and dig fresh for her. Previously the top was also kept when it held a dig/tutor + a
    // useful card, but the shuffle-variance instrument (docs/design/shuffle-variance-instrument.md)
    // showed that keeping-for-dig-cards is marginally WORSE than re-digging: Ponder keeps all 3 on top
    // (it cannot bottom the junk 3rd card), so locking in a merely-useful top costs the fresh look.
    // A/B (heuristic vs this) over 3 seeds incl. 2 held-out: +0.39pp win% and -0.030 avg win turn,
    // never regressing. Keeping Hinata when she is in the top-3 recovers the win% a blind always-shuffle
    // would give up. This also matches how the deck is played by hand (you don't keep cantrips on top).
    //
    // CONFIRMED best on the CLAIRVOYANCE-STRIPPED metric (2026-07-05, MTG_SHUFFLE_SALT_SEARCH decouple
    // instrument, docs/design/shuffle-variance-instrument.md). always-shuffle only beat this rule under
    // clairvoyant timing (edge REVERSES to +0.062 when the search can't pre-see the reshuffle) -> it is
    // an artifact, not a ceiling. Four keep-rules swept over 3 seeds x 6 decouple salts (150g d5): this
    // "keep only Hinata" (6.043) < dig-in-hand (6.060) < original dig+useful>=2 (6.075) << ignore-dig
    // (6.252). Keeping the EXTRA dig/useful tops isn't worth locking in the junk beside them even blind;
    // ignoring dig keeps too little. "Keep Hinata, shuffle the rest" is the measured sweet spot. ---
    for (const Card& c : top)
    {
        const CardDefinition* d = CardDatabase::Instance().LookupCached(c);
        if (d && d->params.hinata_cost_reducer) { return true; }   // Hinata in the top-3 -> keep her
    }
    return false;   // no Hinata on top -> shuffle and dig fresh for her
}

// Hold 0-power mana dorks (Ornithopter of Paradise) back from combat -- see the header note. In a
// goldfish (no blockers) with no Exalted, a 0-power no-trigger creature swinging deals nothing and
// only taps itself, forfeiting the mana the second-main Crackle wants. Off-switch
// MTG_NO_HINATA_HOLD_DORK reverts to generic attack-with-everything for A/B. Gates the projection,
// rollout, AND real declaration in lockstep (all call ShouldAttackWith), so no search/executor desync.
bool HinataProvider::ShouldAttackWith(const GameState& s, const Permanent& p) const
{
    static const bool enabled = std::getenv("MTG_NO_HINATA_HOLD_DORK") == nullptr;
    if (!enabled) { return true; }
    if (AttackPowerOf(s, p) > 0)      { return true; }   // deals damage (a real attacker, incl. Hinata)
    if (AttackHasNonPowerValue(s, p)) { return true; }   // attack-trigger value (none in this deck today)
    return false;                                         // 0-power no-trigger dork -> hold for mana
}

// Hold a LONE Crackle with Power as a combo piece. See the header note: casting a single non-lethal
// Crackle for chip damage throws away the Reality-Spasm -> big-Crackle lethal the shallow search
// cannot see past its horizon. This is the "default to hold Crackle unless you have multiples" prior
// -- the search's own lethal enumeration still fires it the turn it wins (the combo turn's ritual
// mana inflates max_affordable so 5X reaches lethal here and the gate lets it through).
std::vector<int> HinataProvider::XCandidates(const GameState& s, const CardDefinition& def,
                                             int max_affordable) const
{
    std::vector<int> generic = GenericProvider::XCandidates(s, def, max_affordable);
    static const bool enabled = std::getenv("MTG_NO_HINATA_HOLD_CRACKLE") == nullptr;
    if (!enabled || generic.empty() || !IsCrackleCountSpell(def.params)) { return generic; }
    // HUMAN play (the viewer): never hide a castable Crackle -- the hold-as-a-combo-piece prior is
    // an AUTONOMOUS search heuristic, not a restriction on the player. Offer the full affordable X
    // range so the human can choose to fire a chip Crackle if they want.
    if (HumanPlayActive()) { return generic; }

    const int active   = s.active_player_index;
    const int opp_life = s.players[1 - active].life;
    int mult = def.params.x_damage_multiplier; if (mult < 1) { mult = 1; }

    // A SECOND copy in hand -> spending one chip Crackle is free (the other stays for the combo).
    int copies = 0;
    for (const Card& h : s.players[active].hand)
    {
        const CardDefinition* d = CardDatabase::Instance().LookupCached(h);
        if (d && IsCrackleCountSpell(d->params)) { ++copies; }
    }
    if (copies >= 2) { return generic; }

    // Would the biggest affordable Crackle win THIS turn? Over-estimate the attack (every own
    // creature's power, ignoring summoning sickness) so we only ever HOLD when even the best case
    // whiffs -- never forfeiting a same-turn Crackle+attack kill.
    int max_x = 0;
    for (int x : generic) { if (x > max_x) { max_x = x; } }
    int attack_ub = 0;
    for (const Permanent& p : s.battlefield)
    {
        if (p.controller_index == active) { attack_ub += std::max(0, AttackPowerOf(s, p)); }
    }
    if (max_x * mult + attack_ub >= opp_life) { return generic; }   // wins now -> cast it

    return {};   // lone, non-lethal -> HOLD the combo piece
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
    const BurnProvider         g_burn;
}

const DecisionProvider& DefaultProvider()
{
    return g_generic;
}

const DecisionProvider& SelectDecisionProvider(const Decklist& deck)
{
    // Archetype detection by card params (same shape as GoldFishRunner::DeckUsesSecondMain).
    // Order matters only if a deck mixed signatures; today each is exclusive (verified).
    bool anti = false, th = false, vial = false, hinata = false, burn = false;
    for (const Card& c : deck.mainboard)
    {
        const CardDefinition* def = CardDatabase::Instance().LookupCached(c);
        if (!def) { continue; }
        const CardParams& p = def->params;

        // Hinata, Dawn-Crowned's cost-reduction static is the deck's defining signature.
        if (p.hinata_cost_reducer) { hinata = true; }

        // Mono-red Burn: Searing Blaze's landfall damage is unique to this deck; it drives the
        // land-banking heuristic (bank spare lands for a future Blaze's landfall).
        if (p.landfall_damage > 0) { burn = true; }

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
    if (burn) { return g_burn; }
    return g_generic;
}
