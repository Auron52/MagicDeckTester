#include <array>
#include <map>
#include "../core/EnvFlags.h"
#include <cstdlib>
#include <cctype>
#include <utility>
#include <atomic>
#include <cstdint>
#include <algorithm>   // std::stable_sort (OrderEntriesByEtbValue payoff-ordering primitive)
#include <tuple>       // std::make_tuple (CombatCheatCandidates ranking key)
#include <set>         // MTG_TUTOR_RANK_DUMP situation dedupe (diagnostic only)
#include "DecisionProviders.h"

#include "../core/SpellEffects.h"   // shared rules helpers + the archetype heuristic free fns
#include "../deck/DeckLoader.h"     // Decklist
#include "TurnSolver.h"             // Action (PlanContext walks the plan's action list)
#include "PlanContext.h"            // CurrentPlanContext / PlanContextRest
#include "EngineFlags.h"            // TutorAxisResolveEnabled (capacity-anchored deploy read)
#include "ManaPayment.h"            // AvailableManaPool (echo "no gas" check; MTG_LACKEY_RANK=uncast)

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
// pick ONE option the search never alternatives over (cast-ORDER, combat) are NOT yet opened
// here: making the search branch on them needs new enumeration (the ordering/combat work items),
// not just a wider gate. Three have since LEFT this list, each searched by default with this
// hook's ranked pick as its prune and tie-break:
//   * cleanup DISCARD  -- AIEngine::ChooseDiscard      (MTG_SEARCHED_DISCARD)
//   * scry-keep        -- Plan::scry_choice            (MTG_SCRY_SEARCH), land ETB dispositions
//   * vial-charge      -- AIEngine::DecideVialCharge   (MTG_SEARCHED_VIAL)
// Human-play suppression, shared by both the global and per-gate forms: in a --claude-play
// session (MTG_HUMAN_PLAY set) the engine's clairvoyant bottoming/keep rollout is an ENGINE
// decision the human never makes, so un-pruning is suppressed there (a HumanPlaySuppress guard
// is live) -> the kept hand reproduces the real gated d5 game. A pure autonomous audit (no
// human-play) is unaffected.
static bool UnpruneHumanSuppressed()
{
    static const bool hp = EnvSet("MTG_HUMAN_PLAY");
    return hp && g_human_play_suppressed;
}

// Canonical gate name <-> enum table, shared by the MTG_UNPRUNE parser and the gate-probe report.
static const std::pair<const char*, UnprunedGate> kGateNames[] = {
    {"altpayload", UnprunedGate::AltPayload}, {"tutor",     UnprunedGate::Tutor},
    {"fetch",      UnprunedGate::Fetch},      {"dig",       UnprunedGate::Dig},
    {"xspell",     UnprunedGate::XSpell},     {"ponder",    UnprunedGate::Ponder},
    {"groupcap",   UnprunedGate::GroupCap},   {"comboline", UnprunedGate::ComboLine},
    {"searchorder",UnprunedGate::SearchOrder},{"redirect",  UnprunedGate::Redirect},
    {"drawengine", UnprunedGate::DrawEngine}, {"saccolor", UnprunedGate::SacColor},
    {"accelprefix",UnprunedGate::AccelPrefix},
    {"payoffprune",UnprunedGate::PayoffPrune},
    {"splicecollapse",UnprunedGate::SpliceCollapse},
    {"saclandhold",UnprunedGate::SacLandHold},
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
    static const bool v = EnvOn("MTG_UNPRUNED");
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
    static const bool v = EnvOn("MTG_EVAL_MODEL");
    return v;
}

bool UseValueModel()
{
    // ADOPTED default-ON (2026-07-11): the learned value leaf is LP-neutral-within-noise (6-seed d5, TH even
    // net-better) + 1.1-2.7x faster once paired with the hybrid redo + start-gate relaxation (see
    // learned-d0-policy.md). It only engages when a deck ships <deck>.value.json (else m_value_model is
    // empty -> plain search), so decks without a sidecar (e.g. Hinata) are unaffected. Override OFF with
    // MTG_VALUE_MODEL=0/off/no/none/false for A/B against the pure heuristic leaf.
    static const bool v = []{
        const char* e = std::getenv("MTG_VALUE_MODEL");
        if (e && *e)
        {
            const std::string s = e;
            if (s == "0" || s == "off" || s == "no" || s == "none" || s == "false") { return false; }
        }
        return true;
    }();
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

// The BASE cleanup-discard rule for every provider (see DecisionProvider::CleanupDiscardCandidates).
// Lives on the base class, not GenericProvider, because it is the shared default an archetype
// narrows or widens rather than a generic-deck-only answer.
//
// Returns the FULL ranking now, not just the winner -- index 0 is still the historical single pick,
// so this stays byte-identical for every caller that takes the front. The tail is what lets
// AIEngine::ChooseDiscard tie-break among win-optimal discards and what the searched rollout axis
// fans out over. Must NOT call SelectCleanupDiscardIndex: that routes through this hook (so a
// provider override reaches every caller), which would recurse.
// Prune casting a legendary permanent we already control a copy of, when that card does nothing on
// entry. See the hook comment in DecisionProvider.h for why the whitelist is positive rather than
// an etb_* enumeration (the failure direction matters: a missed field would prune a GOOD cast).
bool DecisionProvider::OfferDuplicateLegendCast(const GameState& s, int controller,
                                                const CardDefinition& def) const
{
    // ADOPTED (user's idea, user-approved). MTG_PRUNE_DUP_LEGEND=0 restores the old behaviour.
    // MEASURED (test/dup_legend_prune_ab.sh, fresh seeds 41041..46046):
    //     hinata_d0  36000 games/arm  7.1199 -> 7.0782  -0.0417  t=-158  6/6 seeds
    //     hinata_d3/d5 -0.0058 each;  goblins_d0 +0.0000 (Muxus control);  knights_d0 +0.0000
    // ~200x the two rankings adopted the same day. It was never a modelling bug: the legend rule is
    // enforced immediately in both paths, so the duplicate dies and board value is unchanged --
    // which makes the cast a TIE, and a tie-break took it 109 times per 600 d0 games.
    static const bool s_prune = EnvOn("MTG_PRUNE_DUP_LEGEND", true);
    if (!s_prune) { return true; }

    if (!def.card.HasSupertype(Supertype::Legendary)) { return true; }
    // Enter-inert templates ONLY: a body (plus, for a lord, a continuous effect). Anything else --
    // every `custom` card, notably Muxus with its enter-reveal -- keeps being offered.
    if (def.tmpl != CardTemplate::VanillaCreature && def.tmpl != CardTemplate::LordEffect)
    { return true; }
    // A copy already on OUR battlefield is what makes the new one die on resolution. (An opponent's
    // copy is irrelevant: the legend rule is per-controller.)
    for (const Permanent& p : s.battlefield)
    {
        if (p.controller_index == controller && p.card.m_name == def.card.m_name) { return false; }
    }
    return true;
}

std::vector<int> DecisionProvider::CleanupDiscardCandidates(
    const GameState& s, const std::vector<std::string>* required_pieces) const
{
    return CleanupDiscardRanking(s, required_pieces);
}

// The BASE Goblin Lackey put rule (see DecisionProvider::CombatCheatCandidates). Highest MV, ties
// by power, then by lowest card number -- exactly the ordering FireCombatDamageCheatIntoPlay
// computed inline before the hook existed, so index 0 reproduces the historical single pick and
// this port is byte-identical.
//
// The FULL ranked list is returned (not just the winner) because this decision wants a search: the
// put is free and lands after attackers are declared, so it is an Aether Vial-shaped "what do I want
// on board" choice, and MV is only a proxy for that. Ranked-best-first means a search over the axis
// inherits the historical pick as its tie-break winner.
// TEMPORARY A/B selector (MTG_LACKEY_RANK), per the heuristic-optimization loop: expose the rival
// orderings behind a runtime lever, sweep, report, then delete the losers. "mv" is the shipped rule.
//   mv     -- highest MV, ties by power, then lowest card number  (the historical engine rule)
//   low    -- LOWEST MV first. Deliberate anti-heuristic: it exists to BOUND the headroom. If the
//             best and worst orderings play nearly the same, the choice does not matter much and a
//             search axis over it cannot repay its cost, whatever ranking wins.
//   pow    -- highest power first (is the body what matters, or the mana you skipped paying?)
//   uncast -- highest MV among the cards you could NOT cast right now, then the rest by MV. The
//             put is free and lands after attackers are declared, so its whole value is the mana you
//             never paid; a card you were going to cast anyway converts far less of that.
enum class LackeyRank { Mv, Low, Pow, Uncast };
static LackeyRank LackeyRankMode()
{
    static const LackeyRank m = []() -> LackeyRank {
        const char* v = std::getenv("MTG_LACKEY_RANK");
        if (v == nullptr || *v == '\0') { return LackeyRank::Mv; }
        const std::string s = v;
        if (s == "low")    { return LackeyRank::Low; }
        if (s == "pow")    { return LackeyRank::Pow; }
        if (s == "uncast") { return LackeyRank::Uncast; }
        return LackeyRank::Mv;
    }();
    return m;
}

std::vector<int> DecisionProvider::CombatCheatCandidates(
    const GameState& s, int /*controller*/, const CardDefinition& /*source*/,
    const std::vector<int>& hand_indices) const
{
    const Player& ap = s.players[s.active_player_index];
    const LackeyRank mode = LackeyRankMode();
    // LETHAL FIRST (MTG_LACKEY_LETHAL, default ON). The put resolves in the combat-DAMAGE step, so a
    // creature whose ETB deals damage closes the game THIS turn, while any other body cannot attack
    // until the next one -- a full turn of the primary metric. Param-driven (etb_damage_any), not
    // card-named, so any deck with an ETB-damage creature in the cheat pool gets it.
    //
    // This is an ORDERING key, not a forced choice: it makes the lethal candidate index 0 so the
    // searched axis always scores it, and the search may still prefer another line.
    const int opp_life = s.players[1 - s.active_player_index].life;
    static const bool s_lethal_first = EnvOn("MTG_LACKEY_LETHAL", true);
    auto lethal_now = [&](int h) -> bool {
        if (!s_lethal_first) { return false; }
        const CardDefinition* d = CardDatabase::Instance().LookupCached(ap.hand[h]);
        return d != nullptr && d->params.etb_damage_any > 0
            && d->params.etb_damage_any >= opp_life;
    };
    // Affordability is only consulted by the `uncast` variant, and it is the expensive part, so it
    // is computed once per call and only when that variant is live.
    ManaPool pool;
    if (mode == LackeyRank::Uncast) { pool = AvailableManaPool(s); }
    auto key = [&](int h) {
        const CardDefinition* d = CardDatabase::Instance().LookupCached(ap.hand[h]);
        const Card& c = d ? d->card : ap.hand[h];
        const int mv = c.m_mana_cost.ManaValue();
        const int pw = c.m_power.value_or(0);
        const int num = ap.hand[h].m_number;
        switch (mode)
        {
            case LackeyRank::Low:    return std::make_tuple(mv, -pw, num);
            case LackeyRank::Pow:    return std::make_tuple(-pw, -mv, num);
            case LackeyRank::Uncast: return std::make_tuple(pool.CanPay(c.m_mana_cost) ? 1 : 0,
                                                            -mv, num);
            case LackeyRank::Mv:
            default:                 return std::make_tuple(-mv, -pw, num);
        }
    };
    std::vector<int> out = hand_indices;
    std::stable_sort(out.begin(), out.end(), [&](int a, int b) {
        // Lethal-now outranks every ranking variant, including the anti-heuristic arms, so the
        // MTG_LACKEY_RANK sweep stays a test of the NON-lethal ordering rather than of this.
        const bool la = lethal_now(a), lb = lethal_now(b);
        if (la != lb) { return la; }
        return key(a) < key(b);
    });

    // DOMINANCE PRUNE (MTG_LACKEY_DOMINANCE, default ON). Drop a candidate that another candidate in
    // the same set beats on every axis the engine models, so it cannot consume a searched slot. This
    // is NOT a ranking preference -- it is "B can never be right while A is here", which is the only
    // safe reason to remove an option from a search entirely.
    //
    // Live case: Goblin King vs Goblin Chieftain. Same mana cost, same body, identical lord params
    // (+1/+1 to other Goblins); Chieftain additionally grants haste, while King's only differentiator
    // is mountainwalk, which the card data itself marks INERT in goldfishing (the opponent never
    // blocks). So King is dominated -- in THIS format, not in real Magic. Measured: the search chose
    // King 0 times in 62 triggers where Chieftain was also in hand.
    //
    // It matters because those two tie on mana value AND power, so the sort falls through to card
    // NUMBER (shuffle order) -- with both plus a Goblin Warchief in hand, a width-2 axis could spend
    // both slots on the lords and never score the Warchief, which is the best of the three.
    //
    // Param-derived, not name-keyed: same cost, same P/T, same lord effect, and a strict superset of
    // grant flags. A new card that happens to be dominated is handled without a code change.
    static const bool s_dominance = EnvOn("MTG_LACKEY_DOMINANCE", true);
    if (s_dominance && out.size() > 1)
    {
        auto def_of = [&](int h) { return CardDatabase::Instance().LookupCached(ap.hand[h]); };
        auto dominates = [&](int a, int b) {   // does a strictly dominate b?
            const CardDefinition* da = def_of(a);
            const CardDefinition* db = def_of(b);
            if (da == nullptr || db == nullptr || da == db) { return false; }
            const ManaCost& ca = da->card.m_mana_cost;
            const ManaCost& cb = db->card.m_mana_cost;
            if (ca.generic != cb.generic || ca.white != cb.white || ca.blue != cb.blue
                || ca.black != cb.black || ca.red != cb.red || ca.green != cb.green
                || ca.colorless != cb.colorless || ca.has_x != cb.has_x) { return false; }
            if (da->card.m_power != db->card.m_power || da->card.m_toughness != db->card.m_toughness)
            { return false; }
            const CardParams& pa = da->params;
            const CardParams& pb = db->params;
            if (pa.subtypes_affected != pb.subtypes_affected) { return false; }
            if (pa.power_bonus != pb.power_bonus || pa.tough_bonus != pb.tough_bonus) { return false; }
            if (pa.lord_excludes_self != pb.lord_excludes_self) { return false; }
            if (pa.etb_damage_any != pb.etb_damage_any) { return false; }
            // Same cost, body and lord effect: a wins iff it grants something b does not, and b
            // grants nothing a does not. Only MODELLED grants count -- an unmodelled ability is
            // inert by construction, which is exactly why this is a goldfish-only relation.
            const bool a_extra = (pa.grants_haste && !pb.grants_haste)
                              || (!pa.reduces_spell_subtype.empty() && pb.reduces_spell_subtype.empty());
            const bool b_extra = (pb.grants_haste && !pa.grants_haste)
                              || (!pb.reduces_spell_subtype.empty() && pa.reduces_spell_subtype.empty());
            return a_extra && !b_extra;
        };
        std::vector<int> kept;
        for (int cand : out)
        {
            bool dominated = false;
            for (int other : out) { if (dominates(other, cand)) { dominated = true; break; } }
            if (!dominated) { kept.push_back(cand); }
        }
        if (!kept.empty()) { out.swap(kept); }
    }
    return out;
}

// The BASE ETB-dig rule (see DecisionProvider::EtbDigCandidates): HEURISTIC FIRST, then the
// alternatives. Index 0 is the FIRST legal match in look order -- exactly what PerformEtbDig took
// inline before the hook existed -- so a non-branching caller (and MTG_ETBDIG_WIDTH=1) is
// byte-identical. The REST of the legal matches follow in look order so the searched axis has
// something to choose among; the base rule cannot rank them (look order is library order, i.e.
// shuffle order), which is precisely why this decision is handed to the search rather than to a
// better default. Same shape as TopDispositionCandidates.
std::vector<int> DecisionProvider::EtbDigCandidates(
    const GameState& /*s*/, int /*controller*/, const std::vector<Card>& /*examined*/,
    const std::vector<int>& legal) const
{
    return legal;
}

// ---- Base rules for the remaining ported built-ins -------------------------------------------
// Each reproduces the historical inline pick at index 0, so every port is byte-identical. See the
// hook declarations in DecisionProvider.h for what each rule says and where it looks weak.

std::vector<int> DecisionProvider::LightPawsAuraCandidates(
    const GameState& s, int controller, const Permanent& lightpaws,
    const std::vector<int>& legal) const
{
    const Player& ap = s.players[controller];
    // Rank by the power the Aura would REALIZE if attached now, not by a static coefficient: a
    // scaling Aura grants per matching permanent, so on a wide board its true contribution dwarfs a
    // flat +N. The fetched Aura is itself an enchantment you control, hence the +1 correction (see
    // the original note at the call site). MTG_LEGACY_LIGHTPAWS_STATIC restores the static rank.
    static const bool lp_static = EnvOn("MTG_LEGACY_LIGHTPAWS_STATIC");
    auto contrib = [&](int i) -> int {
        const CardDefinition* d = CardDatabase::Instance().LookupCached(ap.library[i]);
        if (d == nullptr) { return 0; }
        if (lp_static) { return d->params.aura_power_bonus + d->params.aura_scale_power; }
        int c = d->params.aura_power_bonus;
        if (!d->params.aura_scale_kind.empty())
        {
            const int units = CountAuraScaleUnits(d->params.aura_scale_kind, lightpaws, s, controller) + 1;
            c += d->params.aura_scale_power * units;
        }
        return c;
    };
    auto mv = [&](int i) -> int {
        const CardDefinition* d = CardDatabase::Instance().LookupCached(ap.library[i]);
        return d ? d->card.m_mana_cost.ManaValue() : -1;
    };
    // Where a SCALING Aura sits in the ranking (MTG_AURA_RANK_MODE):
    //   0 = legacy      power -> MV -> library order  (the historical rule)
    //   1 = scale-first SCALING -> power -> MV        (all scalers ahead of all flat Auras)
    //   2 = scale-last  power -> MV -> SCALING        (scaling breaks EXACT ties only)
    //   3 = conditional  scale-first WHEN another Aura is castable this/next turn, else mode 2
    //
    // Both beat legacy decisively and by almost the same margin (12600 fresh games/arm, seeds
    // 9001-9006, d0+d3+d5): mode 1 -0.0067, mode 2 -0.0071, each 18 jobs better / 0 worse. The
    // shared win is real and comes from the same place -- realized power prices the board as it
    // stands, which systematically undervalues an Aura whose whole point is that it grows. Ethereal
    // Armor (+1/+1 per enchantment) and Audacity (+2/+2 flat) tie at two enchantments and the Armor
    // is strictly ahead from the third on; in an Auras deck a third is coming, and under legacy ~18
    // fetches per 250 games landed on the flat card by LIBRARY ORDER alone.
    //
    // Mode 1 is the default: scaling Auras COMPOUND with each other. Each one reads the count of
    // enchantments you control, so a third scaler does not add its own power -- it raises the power
    // of every scaler already attached. Realized power at fetch time cannot see that, and it is why
    // trading a scaler for a flat Aura of EQUAL current power is a much worse deal than it looks.
    //
    // Worked case (Auras gi=309, d3, full logs in the commit message): mode 1 fetches All That
    // Glitters then Ethereal Armor and wins T4 with THREE scaling Auras on board; mode 2 fetches
    // Armadillo Cloak (flat +2, MV 3 -- it wins the MV rung at equal power) and needs T5, despite
    // casting its Ancestral Mask a turn EARLIER. That is the whole argument in one game.
    //
    // Mode 2 measures 0.0003 ahead over 72000 disjoint games, and that gap is NOT a preference for
    // flat Auras: inspecting every divergent game, mode 2's wins come from the re-valuation changing
    // which Aura the search casts FROM HAND (a tempo side-effect), not from a better fetch. See
    // gi=1472, where mode 2 casts All That Glitters on T3 instead of Spirit Link and simply attacks
    // for more. Weighed against a mechanism that is real in every game, 0.0003 of cast-order churn
    // is not a reason to prefer the flat Aura.
    static const int s_rank_mode = []{
        const char* e = std::getenv("MTG_AURA_RANK_MODE");
        return (e && *e) ? std::atoi(e) : 1;   // scale-first (see the note above)
    }();
    auto scales = [&](int i) -> bool {
        const CardDefinition* d = CardDatabase::Instance().LookupCached(ap.library[i]);
        return d && !d->params.aura_scale_kind.empty() && d->params.aura_scale_power > 0;
    };
    // Mode 3 -- ask the question the other modes only guess at. A scaling Aura is worth taking while
    // it is a point behind IFF more enchantments are actually coming; otherwise the +3/+3 is simply
    // better and should win. So look at the HAND: is another Aura castable this turn or next?
    // If yes, prefer the scaler (it will be ahead shortly, and by more than one -- casting that Aura
    // fires Light-Paws again, so it adds TWO enchantments, itself and its fetch). If no, fall
    // through to the straight power comparison.
    // "This turn or next" is modelled as MV <= lands in play + 1 (next turn's land drop). Deliberately
    // generous: the failure it guards against is taking a scaler that never grows, and a hand with a
    // castable Aura in it is exactly the case where it does.
    auto another_aura_soon = [&]() -> bool {
        int lands = 0;
        for (const Permanent& p : s.battlefield)
        { if (p.controller_index == controller && p.card.IsLand()) { ++lands; } }
        const int reach = lands + 1;
        for (const Card& c : ap.hand)
        {
            const CardDefinition* d = CardDatabase::Instance().LookupCached(c);
            if (d && d->params.is_aura && d->card.m_mana_cost.ManaValue() <= reach) { return true; }
        }
        return false;
    };
    const bool scale_first_now = (s_rank_mode == 1)
                              || (s_rank_mode == 3 && another_aura_soon());
    std::vector<int> out = legal;
    // Every comparison is strict, so the FIRST library index wins any remaining tie -- which
    // stable_sort preserves (that is what keeps mode 0 byte-identical to the historical scan).
    std::stable_sort(out.begin(), out.end(), [&](int a, int b) {
        if (scale_first_now)
        {
            const bool sa = scales(a), sb = scales(b);
            if (sa != sb) { return sa; }        // all scaling Auras ahead of all flat ones
        }
        const int ca = contrib(a), cb = contrib(b);
        if (ca != cb) { return ca > cb; }       // then greatest realized power
        const int ma = mv(a), mb = mv(b);
        if (ma != mb) { return ma > mb; }       // then highest mana value
        if (s_rank_mode == 2 || s_rank_mode == 3)
        {
            const bool sa = scales(a), sb = scales(b);
            if (sa != sb) { return sa; }        // exact-tie preference (also mode 3's fallback)
        }
        return false;
    });
    // Contention probe (MTG_TRACE=aura) -- is this fetch a real decision, or does the top pick win
    // outright? The triage question for whether to BRANCH this tutor rather than take front(): a
    // fetched Aura PERSISTS (unlike the legend-rule keep, whose differences the next untap erased),
    // so a wrong pick here is not self-correcting. `gap` is the realized-power margin over the
    // runner-up: gap=0 means the heuristic broke a genuine tie and the search never saw the choice.
    // Real resolution only, so rollout re-evaluations do not flood it.
    if (TRACE_ON("aura") && g_real_resolution && !out.empty())
    {
        const int top = contrib(out.front());
        const int second = out.size() > 1 ? contrib(out[1]) : top;
        TRACE("aura", "legal=%zu gap=%d pick=%s(%d) runnerup=%s", out.size(), top - second,
              ap.library[out.front()].m_name.str().c_str(), top,
              out.size() > 1 ? ap.library[out[1]].m_name.str().c_str() : "-");
    }
    return out;
}

std::vector<std::string> DecisionProvider::SacTutorPutList(
    const GameState& s, int controller, const CardParams& /*pp*/, int max_puts) const
{
    // Defense of the Heart default: deterministic closed-form immediate-drain maximisation.
    // Enumerate singles + ordered pairs of library creature NAMES (repeats allowed up to copy
    // count) and score the burst each sequence realises the moment it enters, using the same
    // arithmetic the engine's resolution applies (see PerformUpkeepSacTutor):
    //   - a gift-maker's tokens each drain the CURRENT enter-watch total (Suture Priests);
    //   - a sweeper kills every opp creature with toughness - damage <= debuff, each death
    //     draining the CURRENT death-watch total (Massacre Wurms, incl. the newcomer itself);
    //   - a watcher entering EARLIER in the sequence raises the multiplier for later cards.
    // Total printed power breaks ties (attack value). Fires at most once per game per copy
    // (the permanent is sacrificed), so the ~O(names^2) scan is cheap.
    const Player& ap  = s.players[controller];

    int enter_drain = 0, death_drain = 0;
    std::vector<int> opp_margin;   // per opp creature: effective toughness - marked damage
    for (const Permanent& p : s.battlefield)
    {
        if (p.controller_index == controller)
        {
            const CardDefinition* d = CardDatabase::Instance().LookupCached(p.card);
            if (d)
            {
                enter_drain += d->params.opp_creature_enters_life_loss;
                death_drain += d->params.opp_dies_life_loss;
            }
        }
        else if (p.card.IsCreature() || p.is_animated)
        {
            opp_margin.push_back(p.EffectiveToughness() - p.damage);
        }
    }

    // Distinct library creature names + copy counts.
    std::vector<const CardDefinition*> names;
    std::map<std::string, int>         copies;
    for (const Card& lc : ap.library)
    {
        const CardDefinition* d = CardDatabase::Instance().LookupCached(lc);
        if (!d || !d->card.IsCreature()) { continue; }
        if (copies[lc.m_name.str()]++ == 0) { names.push_back(d); }
    }
    if (names.empty() || max_puts <= 0) { return {}; }

    auto seq_value = [&](const std::vector<const CardDefinition*>& seq) -> long long {
        int P = enter_drain, W = death_drain;
        std::vector<int> margin = opp_margin;
        long long burst = 0, power = 0;
        // All puts enter SIMULTANEOUSLY (see PerformUpkeepSacTutor), so EVERY newcomer's
        // watchers are live before any enter-trigger resolves: a double Massacre Wurm drains
        // 4 per swept creature, not 2+0.
        for (const CardDefinition* d : seq)
        {
            P += d->params.opp_creature_enters_life_loss;
            W += d->params.opp_dies_life_loss;
        }
        // Then the enter-triggers resolve in list order (gifts before sweeps when we order
        // them that way -- the ordered-pair enumeration below tries both). Sweeps STACK the
        // until-EOT debuff on survivors, mirroring the engine's temp_tough_bonus: the second
        // sweep of a double-Wurm put kills at cumulative -4/-4. Gifted tokens appended after
        // a sweep only see later sweeps (CR 611.2c set-locking), also mirrored here.
        for (const CardDefinition* d : seq)
        {
            const CardParams& cp = d->params;
            if (cp.etb_opp_creates_tokens > 0)
            {
                burst += static_cast<long long>(cp.etb_opp_creates_tokens) * P;
                for (int k = 0; k < cp.etb_opp_creates_tokens; ++k)   // gifts -> sweep fodder
                { margin.push_back(std::max(1, cp.etb_created_token_toughness)); }
            }
            if (cp.etb_opp_creatures_debuff > 0)
            {
                std::size_t live = 0;
                for (int& m : margin)
                {
                    m -= cp.etb_opp_creatures_debuff;
                    if (m > 0) { margin[live++] = m; }
                    else       { burst += W; }
                }
                margin.resize(live);
            }
            power += std::max(0, d->card.m_power.value_or(0));
        }
        return burst * 64 + power;   // burst dominates; power is a pure tiebreak
    };

    std::vector<const CardDefinition*> best_seq;
    long long best_val = -1;
    // Singles.
    for (const CardDefinition* a : names)
    {
        std::vector<const CardDefinition*> seq{a};
        long long v = seq_value(seq);
        if (v > best_val) { best_val = v; best_seq = seq; }
    }
    // Ordered pairs (same-name pairs allowed when >= 2 copies remain in the library).
    if (max_puts >= 2)
    {
        for (const CardDefinition* a : names)
        for (const CardDefinition* b : names)
        {
            if (a == b && copies[a->card.m_name.str()] < 2) { continue; }
            std::vector<const CardDefinition*> seq{a, b};
            long long v = seq_value(seq);
            if (v > best_val) { best_val = v; best_seq = seq; }
        }
    }

    std::vector<std::string> out;
    out.reserve(best_seq.size());
    for (const CardDefinition* d : best_seq) { out.push_back(d->card.m_name.str()); }
    return out;
}

std::vector<int> DecisionProvider::SacrificeLandCandidates(
    const GameState& s, int /*controller*/, const std::vector<int>& land_indices) const
{
    // Historical rule: the first TAPPED land if any, else the first land. Reproduced as "tapped
    // before untapped, stable within each band" -- index 0 is the same land as before.
    std::vector<int> out = land_indices;
    std::stable_sort(out.begin(), out.end(), [&](int a, int b) {
        return s.battlefield[a].tapped && !s.battlefield[b].tapped;
    });
    return out;
}

std::vector<int> DecisionProvider::BounceLandCandidates(
    const GameState& s, int /*controller*/, int /*self_index*/,
    const std::vector<int>& legal) const
{
    auto score = [&](int i) -> long {
        const Permanent& p = s.battlefield[i];
        const CardDefinition* d = CardDatabase::Instance().LookupCached(p.card);
        const bool is_karoo = d && d->params.etb_bounce_land;
        const bool enters_untapped =
            !(d && (d->params.enters_tapped || d->params.enters_tapped_with_depletion > 0));
        long v = 0;
        if (is_karoo)        { v -= 1000; }   // never re-trigger the bounce loop
        if (p.tapped)        { v += 100;  }   // already spent -> no mana lost this turn
        if (enters_untapped) { v += 10;   }   // clean replay
        return v;
    };
    std::vector<int> out = legal;
    // The original scan kept a strict `>` winner, so the LOWEST index wins ties -- stable_sort on a
    // descending score over an ascending input reproduces that.
    std::stable_sort(out.begin(), out.end(), [&](int a, int b) { return score(a) > score(b); });
    return out;
}

std::vector<int> DecisionProvider::BurnCreatureTargetCandidates(
    const GameState& s, int /*active*/, int damage,
    const std::vector<int>& opp_creatures) const
{
    // Killable first (so the death rider fires), stable otherwise -> index 0 is the first killable
    // creature, or the first creature when none is killable. Exactly FindBurnKillTarget.
    std::vector<int> out = opp_creatures;
    std::stable_sort(out.begin(), out.end(), [&](int a, int b) {
        return s.battlefield[a].EffectiveToughness() <= damage
            && s.battlefield[b].EffectiveToughness() >  damage;
    });
    return out;
}

std::vector<int> DecisionProvider::LifegainRemovalCandidates(
    const GameState& s, int /*active*/, const std::vector<int>& opp_creatures) const
{
    std::vector<int> out = opp_creatures;
    std::stable_sort(out.begin(), out.end(), [&](int a, int b) {
        return s.battlefield[a].EffectivePower() > s.battlefield[b].EffectivePower();
    });
    return out;
}

std::vector<int> DecisionProvider::OwnPumpTargetCandidates(
    const GameState& s, int /*controller*/, const std::vector<int>& own_attackers) const
{
    std::vector<int> out = own_attackers;
    std::stable_sort(out.begin(), out.end(), [&](int a, int b) {
        return s.battlefield[a].EffectivePower() > s.battlefield[b].EffectivePower();
    });
    return out;
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

int GenericProvider::CastOrderRank(const GameState& s, const CardDefinition& def) const
{
    // See DecisionProvider::CastOrderRank. Reliable deck-agnostic order so the canonical line
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
    //   15/16/18 the SAME-TURN MANA ACCELERANT tiers -- see below.
    //
    // ACCELERANT TIERS (15/16/18), checked FIRST so they take precedence exactly as they did when
    // they lived in the per-archetype overrides. These were written out IDENTICALLY (same ranks,
    // same reasoning, near-identical comments) in HinataProvider and DragonstormProvider, which
    // meant every new ritual deck had to rediscover them -- and until it did, its rituals ranked 20,
    // i.e. TIED WITH THE PAYOFF, so the canonical order could cast the payoff first and strand them
    // (decks/Unpredictable Cyclone holds Seething Song on the generic provider today: exactly that
    // hole). They are pure card-parameter tests with no archetype knowledge, so they belong here:
    // a deck gets correct accelerant ordering from its CARD DATA, with no provider to write.
    //
    //   18 a cast-RESTRICTING ritual (Irencrag Feat, "you can cast only one more spell this turn")
    //      must be the LAST ritual -- after the plain rituals (15) but BEFORE the payoff (20), so
    //      the only spell that follows it is the payoff. Checked before IsManaRitual because
    //      Irencrag is both. Lockstep with the max_casts_after budget in Solve::consider.
    //   16 a cost REDUCER (Ruby Medallion) after the rituals that fund it but before a restrictor:
    //      cast as the single spell allowed AFTER Irencrag it discounts nothing and wastes the "one
    //      more spell" slot the payoff should take. Also governs the post-Apex STAGED re-solve,
    //      where the ordering search cannot reach the exiled casts.
    //   15 a mana ritual: must resolve BEFORE the payoff so its float is available to pay the
    //      bigger spell. Between creatures (10) and other noncreatures (20).
    if (def.params.max_casts_after >= 0)         { return 18; }
    if (!def.params.reduces_spell_color.empty()) { return 16; }
    if (IsManaRitual(def))                       { return 15; }
    if (def.params.on_cast_trigger_damage > 0) { return 30; }
    // Destroy-all-enchantments (Reverent Silence) wipes our OWN Aria/Remedy, so cast it LAST --
    // after this turn's wincon casts (Aria's lethal ETB reversal) have already resolved. Casting
    // it earlier can pre-empt a lethal line and, worse, let a later un-reversed lifegain rider
    // (Aria with the Remedy now gone) HEAL the opponent. Ranked alongside the self-damage tier.
    if (def.params.destroy_all_enchantments)   { return 30; }
    // ---- Light-Paws aura cast ORDER -------------------------------------------------------------
    // Only when we actually control an aura_cast_tutor_attach permanent (Light-Paws): every Aura we
    // cast then fetches ANOTHER Aura, and the fetch may not name an Aura we already control. So the
    // cast order decides what stays fetchable.
    //
    // Cast the Auras we do NOT want to duplicate FIRST, and the ones we do want LAST. Casting
    // Ethereal Armor from hand makes Ethereal Armor unfetchable for the rest of the turn; casting a
    // Rancor first lets its fetch GRAB an Ethereal Armor, after which our own copy is still in hand
    // to cast -- two of them instead of one. Since scaling Auras compound (each reads the
    // enchantment count, see LightPawsAuraCandidates), they are exactly the ones worth duplicating.
    //
    //   20 plain flat Aura        -- first; its fetch can still reach a scaler
    //   22 SCALING Aura           -- later, so the fetches above can take one
    //   23 Aura with an ENCHANT REQUIREMENT -- LAST
    //
    // Rank 23 is a correctness constraint, not a preference: Daybreak Coronet ("enchant creature
    // with another Aura attached to it") and Lion Umbra ("enchant modified creature") CANNOT be cast
    // onto a bare creature. Ordering them last guarantees an Aura is already attached whenever any
    // other Aura is in the plan; if one of them is the only Aura in the plan, order cannot help and
    // enumeration legality decides as before. This is why the scalers are demoted to 22 rather than
    // to the last slot -- pushing them past the constrained pair would strand it.
    // Gated MTG_AURA_CAST_ORDER (default on) and on controlling the tutor, so every deck without a
    // Light-Paws is byte-identical.
    static const bool s_aura_cast_order = EnvOn("MTG_AURA_CAST_ORDER", true);
    if (s_aura_cast_order && def.params.is_aura)
    {
        bool tutor = false;
        for (const Permanent& p : s.battlefield)
        {
            if (p.controller_index != s.active_player_index) { continue; }
            const CardDefinition* d = CardDatabase::Instance().LookupCached(p.card);
            if (d && d->params.aura_cast_tutor_attach) { tutor = true; break; }
        }
        if (tutor)
        {
            if (!def.params.aura_enchant_requires.empty()) { return 23; }
            if (!def.params.aura_scale_kind.empty() && def.params.aura_scale_power > 0) { return 22; }
            return 20;
        }
    }
    if (RockRampEnumEnabled() && def.params.mana_rock && !def.card.IsCreature()) { return 5; }
    // Goblin Warchief (a subtype cost reducer): cast it just BEFORE other creatures (8 < 10) so a
    // same-turn Goblin it discounts is enumerated after the reducer is online. Gated on the param,
    // so every non-reducer deck keeps creatures at 10 (byte-identical).
    if (!def.params.reduces_spell_subtype.empty()) { return 8; }
    if (def.card.IsCreature())                 { return 10; }
    return 20;
}

std::vector<int> GenericProvider::XCandidates(const GameState&, const CardDefinition&,
                                              int max_affordable) const
{
    // See DecisionProvider::XCandidates. In a goldfish, an {X} spell (X burn, X draw, X pump)
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
    // See DecisionProvider::ManaSourceRank. Flexibility rank for the scarcity-first tap order (LOWER =
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
    // Storage-counter land (Dwarven Hold, Mercadian Bazaar): tap it LAST of all sources. Its burst is
    // a partial one -- it removes only the payment's remaining shortfall (see tap_source), so tapping
    // it after every basic/depletion source makes that shortfall MINIMAL, conserving the battery and
    // banking the rest. (The reserve already holds it entirely when the cost is payable without it.)
    if (def.params.storage_land) { return 62; }
    // Depletion lands (Saprazzan Skerry, Sandstone Needle) are deliberately NOT reserved: they are
    // RAMP you normally want to spend, so blanket-conserving them via the ordering would misfire far
    // more often than the rare "wasted a counter" case helps. They rank by colour like any land.
    if (def.params.is_filter || def.params.ramp_filter) { return 25; }
    const std::vector<Color>& prod = EffectiveProduces(s, active, def);
    const int amt = ManaProducedPerTap(def);
    if (amt > 1 && static_cast<int>(prod.size()) > 1) { return 10; }  // bounce/fixed-multi: no choice
    const int ncol = static_cast<int>(prod.size());
    int rank = ncol <= 1 ? 10 : ncol * 10;                            // mono=10 dual=20 tri=30 rainbow=50
    // A COLOUR-producing land must not sit in the colourless-manland RESERVE tier (60): its {C} mode
    // pushes ncol to 6 and collides with Mutavault's save-to-attack rank, stranding the manland's
    // attack. Keep it just below (docs/design/slivers-restricted-mana-tap-order-bug.md).
    if (rank >= 60) { rank = 59; }
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
    // Per-colour SOURCE COUNT (how many distinct sources already make each colour), used by the
    // s_short coverage key below to detect "I want this colour more times this turn than I have
    // sources for it" (e.g. Fiery Justice {W} + Swords {W} both want white -- one white land is not
    // enough). Distinct from the bool `have` set, which only records whether a colour is coverable
    // at all. Counts battlefield lands/dorks/rocks, non-fetch hand lands, AND hand dorks/rocks about
    // to be cast (a dork is a near-future source -- see below).
    std::array<int, NC> src_cnt{};
    auto count_src = [&](const std::vector<Color>& prod)
    {
        for (Color c : prod)
        {
            ++src_cnt[static_cast<int>(c)];
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
        count_src(d->params.produces);
    }
    // Plus colours from OTHER (non-fetch) lands in hand -- part of the deck-fixing equation.
    ColorSet have_or_hand = have;
    for (const Card& c : ap.hand)
    {
        const CardDefinition* d = CardDatabase::Instance().LookupCached(c);
        if (!d || !d->card.IsLand() || !d->params.fetch_land_types.empty()) { continue; }
        add_colors(have_or_hand, d->params.produces);
        count_src(d->params.produces);
    }
    // A mana dork/rock IN HAND about to be cast is a near-future source of its colours -- the payoff
    // it fixes for (e.g. Tainted Remedy {2}{B}) is cast the turn the dork can tap, not the turn the
    // dork is played. So it counts toward securing a colour: don't fetch to secure black when the
    // Ignoble Hierarch already in hand will make it. (Only the source COUNTS -- `have`/`have_or_hand`
    // are left untouched so the distinct-colour coverage keys s_turn/s_deck/s_breadth are unchanged;
    // this is a strictly additive multiplicity signal.)
    for (const Card& c : ap.hand)
    {
        const CardDefinition* d = CardDatabase::Instance().LookupCached(c);
        if (!d || d->card.IsLand()) { continue; }
        if (!(d->tmpl == CardTemplate::ManaDork || d->params.mana_rock)) { continue; }
        count_src(d->params.produces);
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
    // How MANY nonland hand cards want each colour this turn (one count per card per colour, not per
    // pip). demand_turn[W]==2 for a hand of Fiery Justice {W}{R}{G} + Swords {W} -- the multiplicity
    // the bool want_turn cannot express -- so the s_short key below can tell "I have a white source
    // but want white TWICE, so a 2nd white source is still genuine coverage" from "already covered".
    std::array<int, NC> demand_turn{};
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
        const ManaCost& mc = card.m_mana_cost;
        if (mc.white > 0) { ++demand_turn[static_cast<int>(Color::White)]; }
        if (mc.blue  > 0) { ++demand_turn[static_cast<int>(Color::Blue)];  }
        if (mc.black > 0) { ++demand_turn[static_cast<int>(Color::Black)]; }
        if (mc.red   > 0) { ++demand_turn[static_cast<int>(Color::Red)];   }
        if (mc.green > 0) { ++demand_turn[static_cast<int>(Color::Green)]; }
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
    //   multi (fixes >1 colour) / s_short (a colour wanted more times this turn than we have
    //   sources for) / dup_pref (colour-priority tiebreak) / shock (dual over basic)
    bool        have_best = false;
    std::string best_name;
    int b_gc = 0, b_st = 0, b_sd = 0, b_sb = 0, b_multi = 0, b_short = 0, b_dup = 0, b_shock = 0;
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
        // s_short: this candidate produces a colour we want MORE times this turn than we currently
        // have sources for (demand_turn[c] > src_cnt[c]). This is the multiplicity coverage the
        // distinct-colour keys (s_turn/s_breadth) miss: a hand of Fiery Justice {W}{R}{G} + Swords
        // {W} wants white twice, so a 2nd white source is real coverage even though white is already
        // in `have`. Ranked ABOVE the cosmetic dup_pref (which rewards green-first regardless of
        // whether green is already over-covered) but below the distinct-colour keys, so it only
        // decides among candidates those keys tie -- exactly where the green-first tiebreak used to
        // strand a needed 2nd white behind a redundant green/red dual. src_cnt counts near-future
        // in-hand dorks, so a colour a to-be-cast dork already makes is not counted "short".
        int s_short = 0;
        for (Color c : prod)
        {
            int ci = static_cast<int>(c);
            if (ci == static_cast<int>(Color::Colorless)) { continue; }
            if (demand_turn[ci] > src_cnt[ci]) { ++s_short; }
        }
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
        else if (s_short    != b_short) { better = s_short    > b_short; }
        else if (dup_pref   != b_dup)   { better = dup_pref   > b_dup; }
        else if (shock      != b_shock) { better = shock      > b_shock; }
        else                            { better = false; }  // full tie -> keep the earlier incumbent

        if (!have_best || better)
        {
            have_best = true;
            best_name = lc.m_name;
            b_gc = gives_crit; b_st = s_turn; b_sd = s_deck; b_sb = s_breadth;
            b_multi = multi;   b_short = s_short; b_dup = dup_pref; b_shock = shock;
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
    static const bool enabled = !EnvOn("MTG_NO_EXALTED_ATTACK");
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

// Cleanup discard: the USER-AUTHORED bucket policy (2026-08-07). Three buckets -- ENABLER,
// MANA, PAYOFF -- keep 1 enabler and enough mana, maximize payoffs. The discard-analysis
// stage's static name order (Idyllic, StP, ...) measured d0 -0.00525 t=-4.21 but the user's
// review rejected it as a flattened shadow of this state-dependent policy:
//   1. Keep ONE enabler: Tainted Remedy preferred; with 2+ Reverent Silence in hand and a
//      Plague Drone available, prefer the Drone (RS destroys our own enchantments, so under a
//      Remedy enabler the extra Silences are dead). No enabler anywhere -> keep one tutor
//      (Idyllic over Enlightened: to hand beats to top).
//   2. Keep mana to GUARANTEE 3 ON BOARD: hand mana kept = max(0, 3 - board lands/dorks),
//      fetches preferred, then dorks, then plain lands. Excess mana is shed early.
//   3. Everything else is PAYOFF, kept biggest-first: free spells (Invigorate / Skyshroud
//      Cutter / the one allowed Silence) over Aria/Fiery Justice (strong but mana-competing)
//      over payoff-search tutors (slow) over StP (dead unless the opponent has a creature).
// Shed order = reverse keep priority; omission = keep (falls to tier B/C, where required-piece
// protection still guards last copies). MTG_AL_BUCKET_DISCARD=0 -> generic base ranking (A/B).
const std::vector<std::string>*
AntiLifegainProvider::InterchangeableRequiredGroup(const std::string& name) const
{
    // One role, two cards: both replace opponent lifegain with life LOSS. Keeping either makes
    // the other spare, so redundancy must be counted across the pair (see the header note and
    // CleanupDiscardProtected). Idyllic Tutor is NOT in the group: it only FETCHES an enabler,
    // so it is not itself a live enabler.
    static const std::vector<std::string> kEnablers = { "Tainted Remedy", "Plague Drone" };
    if (name == "Tainted Remedy" || name == "Plague Drone") { return &kEnablers; }
    return nullptr;
}

std::vector<int> AntiLifegainProvider::CleanupDiscardCandidates(
    const GameState& s, const std::vector<std::string>* required_pieces) const
{
    static const bool s_bucket = EnvOn("MTG_AL_BUCKET_DISCARD", true);
    if (!s_bucket) { return GenericProvider::CleanupDiscardCandidates(s, required_pieces); }

    const Player& ap = s.players[s.active_player_index];
    const int n = static_cast<int>(ap.hand.size());
    auto def_of  = [](const Card& c) { return CardDatabase::Instance().LookupCached(c); };
    auto is_dork = [&](const Card& c)
    { const CardDefinition* d = def_of(c); return d != nullptr && d->tmpl == CardTemplate::ManaDork; };
    auto is_fetch = [&](const Card& c)
    { const CardDefinition* d = def_of(c); return d != nullptr && !d->params.fetch_land_types.empty(); };
    auto is_land = [&](const Card& c)
    { const CardDefinition* d = def_of(c); return d != nullptr ? d->card.IsLand() : c.IsLand(); };
    auto is_mana = [&](const Card& c) { return is_land(c) || is_dork(c); };
    auto is_tutor = [](const Card& c)
    { return c.m_name == "Idyllic Tutor" || c.m_name == "Enlightened Tutor"; };

    // Board state the buckets key on.
    int  board_sources = 0;
    bool remedy_board = false, drone_board = false, opp_creature = false;
    for (const Permanent& p : s.battlefield)
    {
        if (p.controller_index == s.active_player_index)
        {
            if (is_mana(p.card)) { ++board_sources; }
            if (p.card.m_name == "Tainted Remedy") { remedy_board = true; }
            if (p.card.m_name == "Plague Drone")   { drone_board  = true; }
        }
        else
        {
            const CardDefinition* d = def_of(p.card);
            if (d != nullptr && d->card.IsCreature()) { opp_creature = true; }
        }
    }

    // Hand census (staged cards are in exile -- never bucketed).
    std::vector<int> remedies, drones, tutors, silences;
    int rs_count = 0;
    for (int i = 0; i < n; ++i)
    {
        const Card& c = ap.hand[i];
        if (c.m_is_staged) { continue; }
        if (c.m_name == "Tainted Remedy")    { remedies.push_back(i); }
        if (c.m_name == "Plague Drone")      { drones.push_back(i); }
        if (c.m_name == "Reverent Silence")  { silences.push_back(i); ++rs_count; }
        if (is_tutor(c)) { tutors.push_back(i); }
    }
    // Prefer Idyllic (to hand) over Enlightened (to top) as the kept tutor.
    std::stable_sort(tutors.begin(), tutors.end(), [&](int a, int b)
    { return (ap.hand[a].m_name == "Idyllic Tutor") > (ap.hand[b].m_name == "Idyllic Tutor"); });

    // Bucket 1 -- the kept enabler. Board enabler counts; from hand, Remedy unless the
    // 2+-Silence-and-Drone-available exception applies.
    const bool enabler_on_board = remedy_board || drone_board;
    int kept_enabler = -1, kept_tutor = -1;
    if (!enabler_on_board)
    {
        const bool prefer_drone = rs_count >= 2 && !drones.empty();
        if (prefer_drone)             { kept_enabler = drones.front(); }
        else if (!remedies.empty())   { kept_enabler = remedies.front(); }
        else if (!drones.empty())     { kept_enabler = drones.front(); }
        else if (!tutors.empty())     { kept_tutor = tutors.front(); }
    }
    const bool drone_secured  = drone_board || (kept_enabler >= 0 && ap.hand[kept_enabler].m_name == "Plague Drone");
    const bool remedy_effective = !drone_secured
        && (remedy_board || (kept_enabler >= 0 && ap.hand[kept_enabler].m_name == "Tainted Remedy"));

    std::vector<int> shed;
    // S1 -- dead payoffs: StP with no opponent creature to hit; Silence copies past the first
    // while the effective enabler is a Remedy (casting one destroys it -- the rest never fire).
    for (int i = 0; i < n; ++i)
    {
        if (ap.hand[i].m_is_staged) { continue; }
        if (!opp_creature && ap.hand[i].m_name == "Swords to Plowshares") { shed.push_back(i); }
    }
    if (remedy_effective)
    { for (std::size_t k = 1; k < silences.size(); ++k) { shed.push_back(silences[k]); } }

    // S2 -- excess mana beyond the 3-on-board guarantee: shed plain lands, then dorks, then
    // fetches, keeping the best `need` (the reverse of that order).
    const int need = std::max(0, 3 - board_sources);
    std::vector<int> mana;
    for (int i = 0; i < n; ++i)
    { if (!ap.hand[i].m_is_staged && is_mana(ap.hand[i])) { mana.push_back(i); } }
    auto mana_shed_rank = [&](int i)   // lower = shed sooner
    { return is_fetch(ap.hand[i]) ? 2 : (is_dork(ap.hand[i]) ? 1 : 0); };
    std::stable_sort(mana.begin(), mana.end(), [&](int a, int b)
    { return mana_shed_rank(a) < mana_shed_rank(b); });
    const int excess = static_cast<int>(mana.size()) - need;
    for (int k = 0; k < excess; ++k) { shed.push_back(mana[static_cast<std::size_t>(k)]); }

    // S3 -- redundant enablers and tutors (protection scope re-checks last copies below).
    for (int i : remedies) { if (i != kept_enabler) { shed.push_back(i); } }
    for (int i : drones)   { if (i != kept_enabler) { shed.push_back(i); } }
    for (int i : tutors)   { if (i != kept_tutor)   { shed.push_back(i); } }

    // S4 -- strong but mana-competing payoffs go before the free ones.
    for (int i = 0; i < n; ++i)
    {
        if (ap.hand[i].m_is_staged) { continue; }
        if (ap.hand[i].m_name == "Aria of Flame" || ap.hand[i].m_name == "Fiery Justice")
        { shed.push_back(i); }
    }

    // S5 -- the free payoffs, LEAST wanted first. USER keep priority (2026-08-07) is
    // Skyshroud Cutter > Invigorate > Reverent Silence, so they are shed in reverse. Naming them
    // matters: left unnamed they fell through to the generic highest-mana-value tier, which is the
    // wrong yardstick for cards cast by having an opponent GAIN LIFE -- their printed cost is
    // irrelevant and MV ordering pitched the biggest payoff (Cutter, 5 life -> 5 damage under the
    // enabler) while keeping the smallest (Invigorate, 3). Reverent Silence ranks last because it
    // destroys ALL enchantments including your own Tainted Remedy.
    static const char* const kPayoffShedOrder[] = {
        "Reverent Silence", "Invigorate", "Skyshroud Cutter" };
    for (const char* name : kPayoffShedOrder)
    {
        for (int i = 0; i < n; ++i)
        { if (!ap.hand[i].m_is_staged && ap.hand[i].m_name == name) { shed.push_back(i); } }
    }

    // Omitted (kept): the kept enabler/tutor and the needed mana.
    return CleanupDiscardRankingWithOrder(s, required_pieces, shed);
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

// Strict flood-engine sequencing for the Treasure Hunt archetype (ADOPTED 2026-07-16, default ON;
// MTG_TH_STRICT_FLOOD=0 restores the legacy spent-drop clause as a byte-identical A/B escape hatch).
// The ShouldCastDrawEngine gate drops the spent-drop "dig anyway" clause (condition (3) becomes a
// still-OPEN land drop only). That single change realises the user's force-defer design rule: "don't
// play a land BEFORE Treasure Hunt/Throes" -- because the flood-engine gate is consulted inside the
// per-land enumeration (EnumeratePlansWithLand -> EnumeratePlans), so once a land is played the drop
// is spent and the gate refuses the flood engine; the ONLY way to cast it with no outlet is via the
// defer plan (drop still open). What happens AFTER (play the drawn Reliquary, play a hand land, cast
// Land's Edge for the win, or nothing) is left to the search's post-draw breakpoint re-solve -- the
// gate says nothing about the after-play. Only the TreasureHuntProvider gates the flood engine
// (GenericProvider returns true), so this is TH-archetype-only; other decks are byte-identical.
//
// Adoption evidence (avg = mean turn-to-win, unwon = max_turns+1): non-clairvoyant play is BETTER --
// d0 greedy -0.123, reshuffle-avg NC search -0.034. Clairvoyant search is +0.11..+0.125 WORSE, but
// that is FAKE known-draw speed: all 1012 clairvoyantly-slower games across 4 seeds carry the
// clairvoyance signature (legacy spends the drop, casts the flood, discards the overflow it only
// tolerates because it foresees the kill; the gate defers and keeps the enabler) -- 0 real regressions.
// RE-CONFIRMED 2026-07-29 from the other direction. Overnight seed 4661 is the suite's single SEVERE
// fd-diverge (realized=9 predicted=5): the search proves a T5 win by casting Treasure Hunt with the
// drop spent and no outlet, into a library whose next ~9 cards are lands followed by Land's Edge,
// then casting it and throwing 17 lands for 34. Both MTG_TH_STRICT_FLOOD=0 and
// MTG_UNPRUNE=drawengine recover that win -- i.e. this gate is exactly what declines it, and the
// "proof" is clairvoyant. Re-A/B on held-out seeds 4004-7007 reproduces the note above: removing the
// gate is WORSE at d0 (4/4 cases, +0.062..+0.115) and "better" at searched depth (8/8, -0.066..-0.077),
// the latter being the same fake known-draw speed audited across 1012 games with 0 real regressions.
// CONSEQUENCE: TH has an irreducible fd-diverge FLOOR at searched depth -- a clairvoyant oracle can
// always prove wins a non-clairvoyant gate correctly refuses. Do NOT "fix" seed 4661; judge TH by
// avg turn-to-win. See docs/design/rollout-executor-lockstep.md.
bool THStrictFlood()
{
    static const bool on = []{ const char* e = std::getenv("MTG_TH_STRICT_FLOOD");
                               return !(e && std::string(e) == "0"); }();   // default ON; =0 -> legacy
    return on;
}

// NOTE (2026-07-16): a "favorable-Throes" refinement was proposed and A/B-tested here -- allow a
// spent-drop Throes when the library makes the cascade likely to hit Land's Edge rather than a
// Treasure Hunt (variants: 0 TH left / #LE>=#TH / any LE). It was REJECTED: all variants improved
// CLAIRVOYANT avg but were NC-NEUTRAL (8-seed NC ge vs off = +0.006, ge better on only 2/8; the
// 4-seed -0.017 was noise) -- i.e. the clairvoyant gain was a clairvoyance artifact (the search casts
// the Throes only when it foresees the Land's-Edge hit). The current conservative gate is correct;
// see docs/design/th-keep-model-overmulligans-th-hands.md for the measurements.

// Treasure Hunt keep-floor (heuristic-optimization skill; A/B behind MTG_TH_KEEPFLOOR, default OFF ->
// byte-identical). The exhaustive keep table trains each bucket-comp at only R=41 rollouts; on hands
// that sit right at the keep/mull threshold that is enough noise to land on MULLIGAN. The floor
// force-keeps exactly the ONE over-mull that survives the honest test: a CASTABLE Treasure Hunt hand
// ({1}{U} -> >=2 lands incl. a blue source) that ALSO holds a Reliquary Tower. Measured against the
// realistic baseline -- NC blind play, keep vs the table's own RECURSIVE mulligan, on exactly the
// hands the table mulls -- TH+Reliquary Tower is -0.737 avg (t=-4.17, n=137): a strong, significant
// keep the table wrongly mulls. RT's "no maximum hand size" is a PASSIVE payoff (stops the
// flood-discards) that pays off under blind play. Saprazzan Skerry was DROPPED: same test gives it
// +0.232 (t=+1.40, mildly keep-worse), and the table already keeps every Skerry+2TH hand, so a
// Skerry clause would only ever touch Skerry+1TH -- where the table's re-mull is ~correct. (The user,
// an expert blind player, still suspects Skerry+TH is a keep; the data doesn't clear the bar vs the
// table's smart mull, so we take the safe RT-only bet -- see the design doc's "revisit Skerry"
// tooling note.) Only the initial 7 (mulligan_count 0); everything else falls through to the table.
// See docs/design/th-keep-model-overmulligans-th-hands.md.
KeepGuard TreasureHuntProvider::KeepFloor(const std::vector<Card>& hand, int mulligan_count,
                                          bool /*on_the_play*/) const
{
    static const bool on = []{ const char* e = std::getenv("MTG_TH_KEEPFLOOR");
                               return !(e && std::string(e) == "0"); }();   // default ON; =0 -> legacy (off)
    if (!on || mulligan_count != 0) { return KeepGuard::Undecided; }

    int th = 0, lands = 0, blue_sources = 0;
    bool has_rt = false;
    for (const Card& c : hand)
    {
        if (c.m_name == "Treasure Hunt")   { ++th; continue; }
        if (c.m_name == "Reliquary Tower") { has_rt = true; }
        const CardDefinition* def = CardDatabase::Instance().LookupCached(c);
        const bool is_land = def ? def->card.IsLand() : c.IsLand();
        if (!is_land) { continue; }
        ++lands;
        if (def) { for (Color col : def->params.produces) { if (col == Color::Blue) { ++blue_sources; break; } } }
    }
    // Require RELIQUARY TOWER specifically -- not Skerry, not bare >=2 TH. Measured against the REALISTIC
    // baseline (NC blind play, keep vs the table's own RECURSIVE mulligan -- the thing this floor actually
    // replaces), on exactly the hands the table mulls:
    //   * TH+Reliquary Tower  n=137  delta -0.737 (t=-4.17)  -- strong, significant keep. The genuine
    //     over-mull: RT's "no maximum hand size" stops the flood-discards, a PASSIVE payoff that pays off
    //     blind. Adopt.
    //   * TH+Saprazzan Skerry n=168  delta +0.232 (t=+1.40)  -- mildly keep-WORSE. And the table already
    //     KEEPS every Skerry+2TH hand (0 mulled in a 12k-game scan), so a Skerry clause would only ever
    //     change Skerry+1TH hands, where the table's re-mull is ~correct. So Skerry is dropped: the
    //     "Skerry+TH is a good keep" intuition is true, but the table agrees and keeps them already.
    // (Earlier composition tests showed Skerry/2TH strongly "keep" only vs a WEAK mull-once-to-a-random-6
    // baseline; that answered the wrong question -- keep vs random-6, not keep vs the table's smart mull.)
    // blue_sources>=1 keeps out uncastable (colour-screwed) hands. Only the initial 7 (mulligan_count 0).
    const bool castable_th = (th >= 1 && lands >= 2 && blue_sources >= 1);
    if (castable_th && has_rt) { return KeepGuard::ForceKeep; }
    return KeepGuard::Undecided;
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

// Treasure Hunt scry/surveil keep (user's play model, 2026-07-29; legacy rule behind
// MTG_TH_LEGACY_SCRY=1 for a byte-identical A/B).
//
// The deck is 53 lands / 7 spells, so the DEFAULT for a land on top is BOTTOM -- digging toward the
// 4 Treasure Hunt / 2 Land's Edge / 1 Throes is almost always what the scry is for. The legacy rule
// did the opposite: it kept a land whenever a Treasure Hunt sat in hand ("land fuel") or fewer than
// two lands were in play, which made the scry a guaranteed no-op in the early turns (with an empty
// hand on turn 1 both clauses fire) -- so a Temple of Epiphany was, to the engine, a strictly worse
// Forgotten Cave, and the keep model faithfully learned to prefer the cycler.
//
// Lands are kept only for a NAMED reason:
//   (1) Treasure Hunt is castable THIS turn -> keep. It reveals until a nonland and draws everything
//       revealed, so lands on top are literally drawn into hand (Land's Edge ammo + land drops)
//       rather than skipped. This is the one case where stacking lands on top is pure profit.
//   (2) Land's Edge in hand with >= 10 lands in hand is LETHAL (2 damage a land) -- the only thing
//       that matters is CASTING it, so keep a land only while {1}{R}{R} is not yet payable.
//   (3) Something castable to build toward: keep a land that fills a REAL gap -- a first Reliquary
//       Tower (stops the flood being discarded), a missing colour for Treasure Hunt ({U}) or Land's
//       Edge ({R}{R}), or an untapped source of a colour we can otherwise only make off a tapped
//       land next turn. Cards already in HAND count toward all of these, which is what usually makes
//       the top card unnecessary.
//   (4) Otherwise bottom.
static bool THLegacyScry()
{
    static const bool on = EnvOn("MTG_TH_LEGACY_SCRY");
    return on;
}

// Same four rules, three corrections to what they MEASURE (2026-07-29 sweep). Two further
// corrections were authored, measured and REJECTED; they are recorded at the bottom because the
// reason they lose is the most useful thing the sweep found. Full numbers:
// docs/design/treasure-hunt-open-findings.md section 3b.
//
//  (a) FILTER LANDS ARE NOT UNCONDITIONAL COLOUR SOURCES. Cascade Bluffs (is_filter) and Ferrous
//      Lake (ramp_filter) are 8 of the 53 lands and list produces [U,R], but neither makes a
//      coloured mana alone. Their inputs differ and the old rule counted both at face value:
//        * Ferrous Lake needs {1} GENERIC -- literally any other land switches it on, including
//          Reliquary Tower's {C}. Cheap to satisfy, so it is live from the second land onward.
//        * Cascade Bluffs needs a {U/R} PIP, so it needs another land that makes U or R on its
//          own. It cannot feed itself, and Reliquary Tower cannot feed it either.
//      This is what made a Steam Vents look redundant next to a Ferrous Lake and get bottomed.
//  (b) COUNT MANA, NOT CARDS. A depletion land taps for two of its colour, so ONE Sandstone Needle
//      covers BOTH of Land's Edge's red pips; the old rule counted it as a single source and kept a
//      second red land it did not need.
//  (c) CONDITIONAL-UNTAP LANDS ARE NOT UNCONDITIONALLY UNTAPPED. Frostboil Snarl only enters
//      untapped if an Island or Mountain can be revealed (in this deck: Island, Steam Vents,
//      Thundering Falls). The old rule tested the raw enters_tapped flag, which Snarl does not set,
//      so it counted as a guaranteed untapped source. LandWouldEnterTapped answers this properly
//      (and covers Steam Vents' life payment too).
//  (d) LAND'S EDGE IN PLAY DOES NOT END THE COLOUR PROBLEM. The old rule gated the whole colour path
//      on !le_in_play, so once Land's Edge resolved every land fell through to "bottom" -- but we
//      still need {1}{U} for the next Treasure Hunt. Red, conversely, becomes worthless the moment
//      Land's Edge is on the battlefield: its damage ability costs no mana at all.
//
// MEASURED AND REJECTED -- do not re-add either without a new measurement:
//
//  * KEEPING DEPLETION LANDS as double-spell enablers. Skerry / Needle are the only lands that turn
//    one drop into two mana, which is how the deck casts Treasure Hunt + Treasure Hunt or Treasure
//    Hunt + Land's Edge in a turn, and the untapped test below ranks them last. Crediting that burst
//    measured WORSE on the held-out seeds: +0.0060 alone, +0.0200 with the rest, 14 games slower and
//    0 faster, never once a gain. The mechanism is TIMING -- a depletion land ENTERS TAPPED, so
//    keeping one converts a spell-casting turn into a do-nothing turn, and this deck's clock (T3-T4)
//    leaves no room to ramp. In all three isolated slowdowns the designed double-spell turn really
//    does happen, one turn too late (s6006 gi188: bottoming the Skerry draws Reliquary Tower and
//    casts Treasure Hunt on T2 for a T3 win; keeping it makes T2 a blank tapped land, then casts
//    Treasure Hunt TWICE on T3 and wins T4). A depletion land is worth having when the tapped turn
//    it costs is one we were going to spend anyway -- never something to dig toward.
//  * TARGETING A TWO-TREASURE-HUNT TURN (2 blue / 4 mana). d0 +0.0050 with no searched gain, and it
//    is what armed the depletion clause most often (dropping it cut that clause's damage from
//    +0.0200 to +0.0160). Same lesson one level up: at this clock, a turn spent assembling the
//    bigger turn costs more than the turn buys.
//
// The horizon is fixed and needs no simulation: EVERY scry/surveil in this deck comes from Temple
// of Epiphany (etb_scry) or Thundering Falls (etb_surveil), and BOTH enter tapped. So the land drop
// is always already spent and the new land is already on the battlefield when we choose -- the top
// card cannot produce mana before NEXT turn, which makes the outlook a static count (every land in
// play untaps; one land in hand is a drop away). That is why the drop-simulating variant this
// replaces bought nothing for its +77% wall time.
static bool ScryKeepOnTopLands(const GameState& s, const Card& top_card)
{
    const CardDefinition* tdef = CardDatabase::Instance().LookupCached(top_card);
    const bool is_land = tdef ? tdef->card.IsLand() : top_card.IsLand();

    const int      me = s.active_player_index;
    const Player&  ap = s.players[me];

    auto land_of = [](const Card& c) -> const CardDefinition*
    {
        const CardDefinition* d = CardDatabase::Instance().LookupCached(c);
        return (d && d->card.IsLand()) ? d : nullptr;
    };

    // --- what we hold -------------------------------------------------------------------------
    const CardDefinition* th_def = nullptr;   // Treasure Hunt in hand (the draw engine)
    const CardDefinition* le_def = nullptr;   // Land's Edge in hand (the wincon)
    int th_in_hand = 0, lands_in_hand = 0;
    for (const Card& c : ap.hand)
    {
        const CardDefinition* d = CardDatabase::Instance().LookupCached(c);
        if (!d) { continue; }
        if (d->tmpl == CardTemplate::DrawUntilNonland)      { th_def = d; ++th_in_hand; }
        if (d->params.discard_land_damage > 0)              { le_def = d; }
        if (d->card.IsLand())                               { ++lands_in_hand; }
    }

    // Untapped mana available right now (mirrors TurnSolver::BuildPool / ShouldCastDrawEngine).
    ManaPool pool;
    for (const Permanent& p : s.battlefield)
    {
        if (p.controller_index != me || p.tapped) { continue; }
        const CardDefinition* d = CardDatabase::Instance().LookupCached(p.card);
        if (!d) { continue; }
        const bool is_l = (d->tmpl == CardTemplate::BasicLand);
        const bool is_dork = (d->tmpl == CardTemplate::ManaDork && CanTapNow(p, s.battlefield)) || d->params.mana_rock;
        if (!is_l && !is_dork) { continue; }
        AddSourceToPool(pool, s, *d);
    }

    // Outlets already online: Land's Edge (lands become damage) or Reliquary Tower (the flood is
    // never discarded). Their presence is exactly what lets ShouldCastDrawEngine cast the engine
    // with the land drop already SPENT -- i.e. the land was played BEFORE Treasure Hunt.
    bool le_in_play = false, rt_in_play = false;
    int  lands_in_play = 0;
    for (const Permanent& p : s.battlefield)
    {
        if (p.controller_index != me) { continue; }
        const CardDefinition* d = CardDatabase::Instance().LookupCached(p.card);
        if (!d) { continue; }
        if (d->params.discard_land_damage > 0)                 { le_in_play = true; }
        if (d->params.no_max_hand_size && d->card.IsLand())     { rt_in_play = true; }
        if (d->card.IsLand())                                  { ++lands_in_play; }
    }

    // (1) Treasure Hunt castable THIS turn, with this land played BEFORE it -> the revealed lands
    // get DRAWN into hand, so stacking them on top is pure profit. Gated on an outlet being in
    // play, because that is the only state where the land-then-engine ordering is legal: with no
    // outlet, strict flood (THStrictFlood) forces the DEFER, so a land entering play has already
    // followed the Treasure Hunt and its scry cannot feed it.
    const bool casting_th_now =
        th_def && (le_in_play || rt_in_play) && pool.CanPay(th_def->card.m_mana_cost);

    // --- NONLAND on top ------------------------------------------------------------------------
    // Unchanged from V1: a spell is what we are digging for, except a DUPLICATE Land's Edge (one
    // outlet already discards every land we own) and a Treasure Hunt while we are about to cast one
    // (Treasure Hunt reveals until a NONLAND, so a Treasure Hunt on top truncates the reveal to a
    // single card; moving it away puts a land there instead -- 53 of 60 cards). Scry-bottom and
    // surveil-bin are deliberately treated the SAME: in a 53-land deck the bottom is as unreachable
    // as the graveyard, and either way removing a nonland RAISES the density of lands the next
    // Treasure Hunt reveals. The FIRST Land's Edge stays a search decision.
    if (!is_land)
    {
        if (tdef && tdef->params.discard_land_damage > 0 && (le_in_play || le_def != nullptr))
        { return false; }
        if (tdef && tdef->tmpl == CardTemplate::DrawUntilNonland && casting_th_now)
        { return false; }
        return true;
    }

    if (casting_th_now) { return true; }

    // --- land-base facts, resolved once --------------------------------------------------------
    // (a) A filter land only counts once its feeder exists. Ferrous Lake takes any second land;
    // Cascade Bluffs takes a land that makes U or R WITHOUT being a filter itself.
    bool plain_ur = false;
    auto note_plain = [&](const CardDefinition& d)
    {
        if (d.params.is_filter || d.params.ramp_filter) { return; }
        for (Color c : d.params.produces)
        { if (c == Color::Blue || c == Color::Red) { plain_ur = true; return; } }
        for (Color c : d.params.mdfc_back_produces)
        { if (c == Color::Blue || c == Color::Red) { plain_ur = true; return; } }
    };
    for (const Permanent& p : s.battlefield)
    { if (p.controller_index == me) { if (const CardDefinition* d = land_of(p.card)) { note_plain(*d); } } }
    for (const Card& c : ap.hand)
    { if (const CardDefinition* d = land_of(c)) { note_plain(*d); } }

    const bool ramp_filter_live = (lands_in_play + lands_in_hand) >= 2;   // any other land pays {1}
    const bool filter_live      = plain_ur;                              // needs a real {U}/{R} pip

    // (b) Colour output in MANA per tap, with filters resolved. A depletion land yields two.
    auto amount_of = [&](const CardDefinition& d) { return std::max(1, d.params.produces_amount); };
    auto colour_mana = [&](const CardDefinition& d, Color want) -> int
    {
        auto has = [&](const std::vector<Color>& prod)
        { return std::find(prod.begin(), prod.end(), want) != prod.end(); };
        if (!has(d.params.produces) && !has(d.params.mdfc_back_produces)) { return 0; }
        if (d.params.ramp_filter) { return ramp_filter_live ? 1 : 0; }   // {1} -> {U}{R}: net +1 each
        if (d.params.is_filter)   { return filter_live      ? 1 : 0; }   // {U/R} -> two: net +1
        return amount_of(d);
    };
    auto count_sources = [&](Color want)
    {
        int n = 0;
        for (const Permanent& p : s.battlefield)
        { if (p.controller_index == me) { if (const CardDefinition* d = land_of(p.card)) { n += colour_mana(*d, want); } } }
        for (const Card& c : ap.hand)
        { if (const CardDefinition* d = land_of(c)) { n += colour_mana(*d, want); } }
        return n;
    };
    auto top_makes = [&](Color want) { return tdef && colour_mana(*tdef, want) > 0; };

    // (2) Land's Edge + 10 lands in hand = lethal. Nothing matters but casting {1}{R}{R}.
    if (le_def && lands_in_hand >= 10)
    {
        if (pool.CanPay(le_def->card.m_mana_cost)) { return false; }   // already lethal -> dig
        return top_makes(Color::Red) && count_sources(Color::Red) < le_def->card.m_mana_cost.red;
    }

    // --- what we are trying to cast -------------------------------------------------------------
    // Blue is for Treasure Hunt ({1}{U}); red is ONLY ever for casting Land's Edge, whose damage
    // ability costs no mana at all -- so once it is on the battlefield red is worthless and only
    // {1}{U} matters. While it is not online we keep the speculative {R}{R} target: it is the deck's
    // one wincon. (Deliberately NOT scaled up for a two-Treasure-Hunt turn -- measured worse.)
    const int want_blue = th_def ? 1 : 0;
    const int want_red  = le_in_play ? 0 : 2;

    // (3) Something castable to build toward -> keep a land that fills a REAL gap. (d) applies
    // whether or not Land's Edge is already in play: the engine still costs {1}{U}.
    if (th_def || (le_def && !le_in_play))
    {
        // A first Reliquary Tower: without one the flood Treasure Hunt draws is discarded at
        // cleanup. Pointless once Land's Edge is online -- there the flood is ammunition, not
        // overdraw -- and only relevant while the engine is actually in hand.
        if (th_def && !le_in_play && tdef->params.no_max_hand_size)
        {
            bool have_rt = rt_in_play;
            for (const Card& c : ap.hand)
            {
                const CardDefinition* d = CardDatabase::Instance().LookupCached(c);
                if (d && d->params.no_max_hand_size && d->card.IsLand()) { have_rt = true; break; }
            }
            if (!have_rt) { return true; }
        }
        // A colour we are actually short of, for Treasure Hunt ({U}) or Land's Edge ({R}{R}).
        if (top_makes(Color::Blue) && count_sources(Color::Blue) < want_blue) { return true; }
        if (top_makes(Color::Red)  && count_sources(Color::Red)  < want_red)  { return true; }

        // (c) An UNTAPPED source of a needed colour, when everything we hold enters tapped -- that
        // is the difference between casting the engine next turn and durdling another turn. Every
        // land already in play untaps on its own; a land in hand has to enter untapped, which for a
        // reveal/shock land is conditional (LandWouldEnterTapped resolves it).
        if (!LandWouldEnterTapped(s, *tdef))
        {
            auto untapped_source_of = [&](Color want)
            {
                for (const Permanent& p : s.battlefield)
                {
                    if (p.controller_index != me) { continue; }
                    const CardDefinition* d = land_of(p.card);
                    if (d && colour_mana(*d, want) > 0) { return true; }
                }
                for (const Card& c : ap.hand)
                {
                    const CardDefinition* d = land_of(c);
                    if (!d || colour_mana(*d, want) == 0) { continue; }
                    if (!LandWouldEnterTapped(s, *d)) { return true; }
                }
                return false;
            };
            if (want_blue > 0 && top_makes(Color::Blue) && !untapped_source_of(Color::Blue)) { return true; }
            if (want_red  > 0 && top_makes(Color::Red)  && !untapped_source_of(Color::Red))  { return true; }
        }
        return false;
    }

    // (4) No engine, no lethal -> a land on top is just another land in a 53-land deck.
    return false;
}

bool TreasureHuntProvider::ScryKeepOnTop(const GameState& s, const Card& top_card) const
{
    if (THLegacyScry())
    {
        const CardDefinition* tdef = CardDatabase::Instance().LookupCached(top_card);
        const bool is_land = tdef ? tdef->card.IsLand() : top_card.IsLand();
        if (!is_land) { return true; }
        const int     me = s.active_player_index;
        const Player& ap = s.players[me];
        for (const Card& c : ap.hand)
        {
            const CardDefinition* cdef = CardDatabase::Instance().LookupCached(c);
            if (cdef && cdef->tmpl == CardTemplate::DrawUntilNonland) { return true; }
        }
        int lip = 0;
        for (const Permanent& p : s.battlefield)
        { if (p.controller_index == me && p.card.IsLand()) { ++lip; } }
        return lip < 2;
    }
    return ScryKeepOnTopLands(s, top_card);
}

// Treasure Hunt's cleanup discard. The answer is essentially always "shed a land" (the base rule's
// tier A already says that, because a Land's Edge outlet makes lands ammunition) -- the real
// question is WHICH, and the base rule answers it with hand order. This orders the lands.
//
// Authored from the deck's actual structure, as a LADDER so each rule is separately attributable
// (MTG_TH_DISCARD selects a rung for the sweep; the adopted default is documented at the enum):
//
//   1 dup      -- the SPARE cards first, in the order they are spare: a dead duplicate Land's Edge,
//                 then a retrace card (recastable from the graveyard), then a duplicate land. The
//                 safest rule available -- each is a card whose copy in hand is doing nothing
//                 another copy (or another zone) is not already doing.
//   2 tapped   -- then lands that ENTER TAPPED, excluding the depletion/storage lands. Entering
//                 tapped is a real tempo cost, and Temple of Epiphany / Thundering Falls pay for
//                 it with an ETB that only ever fires if the land is PLAYED -- so in hand they are
//                 the plainest lands the deck has. The depletion lands (Saprazzan Skerry, Sandstone
//                 Needle) are excluded deliberately: they also enter tapped, but produce TWO mana,
//                 which is a burst this deck actually wants.
//   3 nodig    -- and the diggers (cycling: Lonely Sandbar / Forgotten Cave / Remote Isle;
//                 sacrifice-to-draw: Fiery Islet) are protected to the BACK, most strongly when no
//                 Treasure Hunt is in hand -- with no engine to find, a land that replaces itself
//                 is the deck's remaining source of cards.
//
// Reliquary Tower is last at every rung: it is the card that ENDS this decision (no maximum hand
// size), so shedding it to satisfy a hand limit is self-defeating.
//
// MEASURED (test/th_discard_sweep.sh, TRAIN seeds; searched d3/d5 + d0 sums vs rung 0):
//     rung 1  -0.0073 / -0.0130   <- ADOPTED, the only rung that pays
//     rung 2  -0.0006 / -0.0130      the tapped rule gives back the searched half
//     rung 3  +0.0027 / -0.0110      protecting the diggers is worse still
// Rungs 2 and 3 are NOT adopted, and the reason they fail is instructive: these diggers are
// LANDS, not spells. Cycling one costs mana AND spends a card that was Land's Edge ammunition, so
// "protect the diggers" is not the free upside it is in a deck where the digger is a cantrip --
// and for the outlet's purposes the deck's lands are largely interchangeable, which is why
// discriminating further among them stops paying once the genuinely SPARE cards are handled.
//   4 tapdig   -- the FAITHFUL version of rung 2: tapped-and-not-a-digger-and-not-depletion goes
//                 early (Temple of Epiphany, Thundering Falls, and a Frostboil Snarl we cannot
//                 currently turn on), while the diggers sit NEUTRAL with the plain untapped lands
//                 rather than being protected to the back. See the note on rungs 2/3 above for why
//                 neither of them actually tested this.
//   5 mono     -- + among the untapped keepers, a land producing FEWER distinct colours goes first
//                 (Island before a U/R dual): the dual is the one that can still cast both halves
//                 of the deck, so the mono source is the more expendable of two untapped lands.
//   6 mix      -- ROLE DIVERSITY instead of a static category order, and the one rung whose shape
//                 differs from the rest. The measurement that sank rungs 2-5 says ordering the
//                 ~10-land pool by category is invisible; what a flooded hand can still get wrong
//                 is keeping ten lands that all do the SAME thing. So: classify each land by ROLE
//                 (untapped dual / untapped mono / depletion / digger / tapped / no-max-hand), and
//                 shed from the role we hold the MOST of, never taking the last copy of a role
//                 while a role with spares exists. Note this SUBSUMES the adopted rung's duplicate
//                 rule -- a name-duplicate is just the special case of functional redundancy where
//                 the two cards are the same card -- so it is built on top of rung 1 rather than
//                 replacing it.
//   7 keep     -- an explicit KEEP SET in priority order (2 untapped incl. one filter, then
//                 depletion 1-per-name, then diggers) rather than a shed order, with Frostboil
//                 Snarl only counting as untapped when an enabler SURVIVES the cleanup. See the
//                 keep-set construction below for why that ordering has to exist.
//   8 shop     -- the keep set as an explicit SHOPPING LIST sized to the hand limit: 1 untapped
//                 non-filter, 1 untapped filter, 2 depletion (one per name), 3 diggers (Fiery
//                 Islet, then a 1-mana cycler, then a typed scry/surveil land). That is exactly 7
//                 -- if the hand can fill it there is no room left, which is fine, and any slot it
//                 cannot fill is simply given back to whatever lands remain. Differs from rung 7
//                 in taking a filter as its OWN slot rather than as a preference inside two shared
//                 untapped slots, and in buying three diggers rather than one or two.
//   9 islet    -- rung 8 with the Fiery Islet double-count resolved. Islet is BOTH untapped U/R
//                 mana and a digger, and rung 8 spent a DIG slot on it -- so the hand paid for its
//                 dig role twice and bought one fewer real cycler. Here it fills the untapped
//                 non-filter slot instead (the mana it provides is the same either way) and is
//                 barred from the dig slots, which frees a cycler into the list; and the leftover
//                 capacity prefers CYCLERS rather than alternating, so the extras keep digging.
//                 The measured symptom this targets: rung 8 shed MORE cyclers than the simple
//                 rule, which is the opposite of its intent.
//
// ADOPTED: rung 9 is the default. Read the size of that decision honestly -- the whole ladder is
// worth very little, and almost all of what it IS worth comes from rung 1:
//
//   test/th_rung0_baseline_ab.sh, 320k d0 games/arm, fresh seeds 16016..23023, vs rung 0
//   (the arbitrary rule: base ranking, WHICH land chosen by hand order)
//                       d0        d3        d5
//     rung 1        -0.0041   -0.0015   -0.0005     the two spare-card rules
//     rung 9        -0.0042   -0.0022   -0.0011     + this entire keep-order spec
//
// So rung 1 does ~98% of the work at d0, and the keep order adds -0.0001. It earns more at
// searched depth (-0.0007 at d3, -0.0006 at d5, on 20-50x fewer games), and it is separated
// (t=-7.6, 8/8 seeds, every depth agreeing in sign) -- but separated at 320k games is not the
// same as material, and it is not claimed to be.
//
// WHY SO SMALL, measured rather than guessed (test/th_mechanism_probe.sh): Land's Edge deals
// damage PER LAND, so WHICH land it burns is unobservable unless the outlet takes a strict subset
// of the hand -- and it burns everything ~97.6% of the time. The ranked pitch picks a different
// set in 1.7% of activations. That one number explains this whole family of nulls.
//
// It was adopted anyway, and the reason is mechanism rather than metric: the ranking demonstrably
// does what it says. Cleanup discards per 1000 games, rung 1 -> rung 9 --
//     digger-cycle 174.5 -> 145.2   depletion 166.5 -> 120.2   digger-scry 117.8 -> 158.0
// i.e. it keeps 17% more cycling lands and 28% more depletion lands, paying in scry/surveil lands,
// which is exactly the trade this spec asks for. A rule that is right for a stated reason and
// costs nothing beats an arbitrary hand-order tie-break of equal measured value.
enum class ThDiscard { Base = 0, Dup = 1, Tapped = 2, NoDig = 3, TapDig = 4, Mono = 5, Mix = 6,
                       Keep = 7, Shop = 8, Islet = 9 };
static ThDiscard ThDiscardVariant()
{
    static const ThDiscard v = []() -> ThDiscard
    {
        const char* e = std::getenv("MTG_TH_DISCARD");
        if (e == nullptr || *e == '\0') { return ThDiscard::Islet; }   // DEFAULT (see the sweep)
        switch (std::atoi(e))
        {
            case 0:  return ThDiscard::Base;
            case 1:  return ThDiscard::Dup;
            case 2:  return ThDiscard::Tapped;
            case 3:  return ThDiscard::NoDig;
            case 4:  return ThDiscard::TapDig;
            case 5:  return ThDiscard::Mono;
            case 6:  return ThDiscard::Mix;
            case 7:  return ThDiscard::Keep;
            case 8:  return ThDiscard::Shop;
            default: return ThDiscard::Islet;
        }
    }();
    return v;
}

// The hook: the ranking IS the decision (user design 2026-08-06 -- "the heuristic decides
// everything and returns a list of some size; all of those options are searched"). This provider
// returns exactly ONE index, so the executor's searched pass has nothing to fan over and the
// keep-set rule below decides the shed outright -- which is what it was commissioned for: without
// this prune the searched pass fanned a probe rollout per HAND CARD over this deck's 15-25-card
// cleanups, and (x each trial turn's full-depth ladder) one bounded game cost hours
// (docs/design/th-d5-five-hour-game.md). The legacy Base variant keeps the full fan: it exists to
// A/B the ranking rules, and its historical meaning includes the searched pass choosing.
std::vector<int> TreasureHuntProvider::CleanupDiscardCandidates(
    const GameState& s, const std::vector<std::string>* required_pieces) const
{
    std::vector<int> full = CleanupDiscardFullRanking(s, required_pieces);
    if (ThDiscardVariant() == ThDiscard::Base) { return full; }
    // ONE candidate -- measured, not assumed (2026-08-06 sweep, regression tier, vs the whole-hand
    // fan the ground truth embodied): top-1 flipped 4 of ~1800 games +1 turn; top-2/3 recovered
    // only one of them; and after the retrace-protection fix (CleanupDiscardProtected) top-1 is
    // NET BETTER than the fan (smoke d0 -0.0050, regression -0.0020/-0.0033, rest equal) at 0.3s
    // flat vs hours. The residual fan wins were clairvoyant (shedding the only Land's Edge because
    // a replacement is known to be coming) or a one-game scry-valuation edge (gi=229).
    if (full.size() > 1) { full.resize(1); }
    return full;
}

std::vector<int> TreasureHuntProvider::CleanupDiscardFullRanking(
    const GameState& s, const std::vector<std::string>* required_pieces) const
{
    const ThDiscard variant = ThDiscardVariant();
    if (variant == ThDiscard::Base) { return CleanupDiscardRanking(s, required_pieces); }

    const Player& ap = s.players[s.active_player_index];
    const int     n  = static_cast<int>(ap.hand.size());

    // "with no engine to find" -- a Treasure Hunt still in hand means the diggers have a better
    // card to find than themselves, so their protection only tightens once it is gone.
    bool have_engine = false;
    for (const Card& c : ap.hand)
    {
        const CardDefinition* d = CardDatabase::Instance().LookupCached(c);
        if (d != nullptr && d->tmpl == CardTemplate::DrawUntilNonland) { have_engine = true; break; }
    }

    // Copies of a name across hand AND battlefield: a Land's Edge in hand is dead if one is already
    // in play, which "duplicates in hand" alone would miss.
    auto copies_seen = [&](const InternedName& name) -> int
    {
        int k = 0;
        for (const Card& h : ap.hand) { if (h.m_name == name) { ++k; } }
        for (const Permanent& perm : s.battlefield)
        { if (perm.controller_index == s.active_player_index && perm.card.m_name == name) { ++k; } }
        return k;
    };

    // Board state the Tower rule is conditional on (see band()): another no-max-hand-size land
    // already out makes the one in hand dead, and a Land's Edge outlet makes the hand limit moot.
    bool tower_in_play = false, outlet_in_play = false;
    for (const Permanent& perm : s.battlefield)
    {
        if (perm.controller_index != s.active_player_index) { continue; }
        const CardDefinition* pd = CardDatabase::Instance().LookupCached(perm.card);
        if (pd == nullptr) { continue; }
        if (pd->params.no_max_hand_size && pd->card.IsLand()) { tower_in_play  = true; }
        if (pd->params.discard_land_damage > 0)               { outlet_in_play = true; }
    }

    // What JOB a land does, for the rung-6 diversity rule. Deliberately coarse: the point is not to
    // rank these against each other (rungs 4/5 measured that as invisible) but to notice when the
    // hand holds five of one and none of another. Order of the tests matters -- a cycler that also
    // enters tapped is a DIGGER, because that is the job we would miss.
    auto th_role = [&](const Card& c) -> int
    {
        const CardDefinition* d = CardDatabase::Instance().LookupCached(c);
        const CardParams*     p = d != nullptr ? &d->params : nullptr;
        if (p == nullptr)                            { return 0; }
        if (p->no_max_hand_size)                     { return 1; }   // ends the decision entirely
        if (p->cycling_cost.has_value()
            || p->sacrifice_draw_cost.has_value())   { return 2; }   // digger
        if (p->enters_tapped_with_depletion > 0)     { return 3; }   // burst mana
        if (d != nullptr && LandWouldEnterTapped(s, *d)) { return 4; }   // tapped mana
        return p->produces.size() > 1 ? 5 : 6;                       // untapped dual / untapped mono
    };

    // Rung 7 -- an explicit KEEP SET, built once before ranking. The hand ends at seven cards, so
    // the real question is not "which land is worst" but "which few lands do we want left", in
    // priority order: two untapped sources (one of them ideally a FILTER -- Cascade Bluffs, else
    // Ferrous Lake), then the depletion lands one per name, then diggers if room remains.
    //
    // Two things need the keep set to exist BEFORE the ranking rather than being decided per card:
    //   * Frostboil Snarl enters untapped only while an Island/Mountain card can be revealed, so it
    //     counts toward the untapped quota ONLY if an enabler is itself being kept. Asking
    //     LandWouldEnterTapped instead (as rungs 4-6 do) reads the hand as it is NOW -- and a
    //     cleanup that sheds three cards can shed the enabler and quietly turn the Snarl into a
    //     tapped land. That is the easy mistake here, and it is invisible without this ordering.
    //   * Fiery Islet is the best FIRST digger precisely because it doubles as untapped U/R mana --
    //     a fact about the rest of the keep set, not about the card alone.
    // keep_slot: 0 == not bought by the list; otherwise the SLOT it filled, 1 = most wanted.
    // Banding kept lands by this rather than by role is load-bearing: a hand can hold more lands
    // than the 7-card limit leaves room for even after the list is filled (Treasure Hunt and
    // Land's Edge are never shed, so the real land budget is 7 minus those), and when the kept set
    // itself has to be trimmed, the trim MUST follow the list's priority. Banding it by th_role
    // instead sheds Reliquary Tower first and the diggers second -- almost exactly inverted -- and
    // that bug, not the ranking, is what the first rung-8 measurement measured.
    std::vector<int>  keep_slot(static_cast<std::size_t>(n), 0);
    int next_slot = 0;
    std::vector<char> kept(static_cast<std::size_t>(n), 0);
    auto take = [&](int i) { if (!kept[i]) { kept[i] = 1; keep_slot[i] = ++next_slot; } };
    // Rung 8 fallback ordering for lands the shopping list did not buy: 0 == not ranked, otherwise
    // smaller == more wanted. Cards ranked here are shed AFTER the unranked surplus, in reverse.
    std::vector<int>  fallback(static_cast<std::size_t>(n), 0);
    if (variant >= ThDiscard::Keep)
    {
        auto par = [&](int i) -> const CardParams*
        {
            const CardDefinition* d = CardDatabase::Instance().LookupCached(ap.hand[i]);
            return d != nullptr ? &d->params : nullptr;
        };
        auto is_snarl = [&](int i) { const CardParams* p = par(i);
                                     return p != nullptr && !p->etb_untap_reveal_subtypes.empty(); };
        // "Dig" is the whole selection category, not just cycling: a scry/surveil land is the
        // weakest member of it rather than a separate role, because when PLAYED it also filters.
        auto digs_i   = [&](int i) { const CardParams* p = par(i);
                                     return p != nullptr && (p->cycling_cost.has_value()
                                                          || p->sacrifice_draw_cost.has_value()
                                                          || p->etb_scry > 0 || p->etb_surveil > 0); };
        auto deplet_i = [&](int i) { const CardParams* p = par(i);
                                     return p != nullptr && p->enters_tapped_with_depletion > 0; };
        // "Untapped" for quota purposes: not the tapped flag, and not a Snarl (handled separately).
        auto untapped_i = [&](int i)
        {
            const CardParams* p = par(i);
            if (p == nullptr || deplet_i(i) || is_snarl(i)) { return false; }
            return !p->enters_tapped;
        };
        // Rung 9: a digger that is ALSO untapped mana (Fiery Islet) is bought with the untapped
        // slot and barred from the dig slots, so the list stops paying for the same card twice.
        auto islet_i = [&](int i)
        {
            const CardParams* p = par(i);
            return variant >= ThDiscard::Islet && p != nullptr && !p->enters_tapped
                && (p->cycling_cost.has_value() || p->sacrifice_draw_cost.has_value());
        };
        // Preference inside the untapped slots: a filter first (Cascade Bluffs over Ferrous Lake),
        // then a dual, then a mono source.
        auto untapped_rank = [&](int i)
        {
            const CardParams* p = par(i);
            if (p == nullptr) { return 9; }
            if (p->is_filter)              { return 0; }
            if (p->ramp_filter)            { return 1; }
            return p->produces.size() > 1 ? 2 : 3;
        };

        std::vector<int> lands_i;
        for (int i = 0; i < n; ++i)
        { if (!ap.hand[i].m_is_staged && CleanupDiscardIsLand(ap.hand[i])) { lands_i.push_back(i); } }

        // Reliquary Tower is kept outside the quotas -- but only while it still HAS the job that
        // earns the exemption, so the keep set agrees with band() instead of quietly contradicting
        // it. Buying it unconditionally made the shopping list say "most-wanted land in the deck"
        // about a card band() had already decided was spare. band()'s early returns mean this take()
        // cannot actually change a ranking (all three Tower states return before the keep-set
        // banding), but a keep set that disagrees with the ranking it feeds is how the last two
        // bugs here hid -- and an unconditional take() also silently shifts every other land's slot
        // number by one.
        if (!outlet_in_play && !tower_in_play)
        {
            for (int i : lands_i) { const CardParams* p = par(i);
                                    if (p != nullptr && p->no_max_hand_size) { take(i); } }
        }

        // 1. two untapped sources, best-ranked first.
        std::vector<int> ups;
        for (int i : lands_i) { if (untapped_i(i)) { ups.push_back(i); } }
        std::stable_sort(ups.begin(), ups.end(),
                         [&](int a, int b) { return untapped_rank(a) < untapped_rank(b); });
        int untapped_kept = 0;
        if (variant >= ThDiscard::Shop)
        {
            // One NON-filter untapped source and one FILTER, as separate slots -- the filter is not
            // a better version of a plain untapped land, it is a different tool, so competing them
            // for the same two slots (rung 7) can end up buying two of one kind.
            bool took_plain = false, took_filter = false;
            // Rung 9 takes the Islet FIRST for the plain untapped slot -- it is the one untapped
            // source that keeps digging after it is played.
            std::vector<int> ups2 = ups;
            std::stable_sort(ups2.begin(), ups2.end(),
                             [&](int a, int b) { return islet_i(a) > islet_i(b); });
            for (int i : (variant >= ThDiscard::Islet ? ups2 : ups))
            {
                const CardParams* p2 = par(i);
                const bool filt = p2 != nullptr && (p2->is_filter || p2->ramp_filter);
                if (filt && !took_filter)       { take(i); took_filter = true; ++untapped_kept; }
                else if (!filt && !took_plain)  { take(i); took_plain  = true; ++untapped_kept; }
            }
        }
        else
        {
            for (int i : ups) { if (untapped_kept < 2) { take(i); ++untapped_kept; } }
        }

        // A Snarl only fills a remaining untapped slot if something we are KEEPING turns it on.
        if (untapped_kept < 2)
        {
            for (int i : lands_i)
            {
                if (untapped_kept >= 2 || !is_snarl(i) || kept[i]) { continue; }
                const CardDefinition* d = CardDatabase::Instance().LookupCached(ap.hand[i]);
                bool enabled = false;
                for (int j : lands_i)
                {
                    if (!kept[j] || j == i) { continue; }
                    const CardDefinition* e = CardDatabase::Instance().LookupCached(ap.hand[j]);
                    const SubtypeSet& subs = e != nullptr ? e->card.m_subtypes : ap.hand[j].m_subtypes;
                    for (const std::string& cs : subs)
                        for (const std::string& want : d->params.etb_untap_reveal_subtypes)
                            if (cs == want) { enabled = true; }
                }
                if (enabled) { take(i); ++untapped_kept; }
            }
        }

        // 2. depletion lands, one per NAME, the OUTLET's colour first. Sandstone Needle before
        //    Saprazzan Skerry -- and that ordering is derivable rather than a card-name table:
        //    Land's Edge, the deck's payoff, costs {1}{R}{R}, and a depletion land is how a
        //    three-land board pays a DOUBLE pip. Treasure Hunt is {1}{U}, so the blue one is the
        //    better OPENING-hand card -- but by the time a cleanup discard is happening the engine
        //    has usually already run, and it is the payoff that still needs casting.
        int outlet_w = 0, outlet_u = 0, outlet_b = 0, outlet_r = 0, outlet_g = 0;
        {
            auto note_outlet = [&](const Card& c)
            {
                const CardDefinition* d2 = CardDatabase::Instance().LookupCached(c);
                if (d2 == nullptr || d2->params.discard_land_damage <= 0) { return; }
                const ManaCost& mc = d2->card.m_mana_cost;
                outlet_w += mc.white; outlet_u += mc.blue;  outlet_b += mc.black;
                outlet_r += mc.red;   outlet_g += mc.green;
            };
            for (const Card& c : ap.hand)    { note_outlet(c); }
            for (const Card& c : ap.library) { note_outlet(c); }
        }
        auto outlet_colored = [&](int i)
        {
            const CardParams* p2 = par(i);
            if (p2 == nullptr) { return false; }
            for (Color col : p2->produces)
            {
                if (col == Color::White && outlet_w > 0) { return true; }
                if (col == Color::Blue  && outlet_u > 0) { return true; }
                if (col == Color::Black && outlet_b > 0) { return true; }
                if (col == Color::Red   && outlet_r > 0) { return true; }
                if (col == Color::Green && outlet_g > 0) { return true; }
            }
            return false;
        };
        std::vector<int> deps;
        for (int i : lands_i) { if (!kept[i] && deplet_i(i)) { deps.push_back(i); } }
        std::stable_sort(deps.begin(), deps.end(), [&](int a, int b)
        { return outlet_colored(a) > outlet_colored(b); });
        for (int i : deps)
        {
            bool have_name = false;
            for (int j : lands_i)
            { if (kept[j] && ap.hand[j].m_name == ap.hand[i].m_name) { have_name = true; } }
            if (!have_name) { take(i); }
        }

        // 3. diggers if room remains, in the order they are worth a slot:
        //      0  Fiery Islet   -- also untapped U/R mana, so it pays for its slot twice
        //      1  a 1-mana cycler (Lonely Sandbar, Forgotten Cave) -- selection at a real price
        //      2  a costlier cycler (Remote Isle at {2})
        //      3  a scry/surveil land -- it only filters if we get to PLAY it
        //    Fewer when a Treasure Hunt is already in hand: the dig role matters less when the
        //    engine we would be digging for is here.
        auto dig_rank = [&](int i) -> int
        {
            const CardParams* p = par(i);
            if (p == nullptr) { return 4; }
            if (p->sacrifice_draw_cost.has_value() && !p->enters_tapped) { return 0; }
            if (p->cycling_cost.has_value())
            { return p->cycling_cost->ManaValue() <= 1 ? 1 : 2; }
            if (p->sacrifice_draw_cost.has_value()) { return 1; }
            // Scry/surveil lands last -- and among them, one carrying BASIC LAND TYPES first
            // (Thundering Falls is Island Mountain, Temple of Epiphany is a bare Land). The types
            // are not cosmetic here: they are exactly what a Frostboil Snarl reveals to enter
            // untapped, so the typed one is worth strictly more to this hand than the other.
            const CardDefinition* dd = CardDatabase::Instance().LookupCached(ap.hand[i]);
            const SubtypeSet& subs = dd != nullptr ? dd->card.m_subtypes : ap.hand[i].m_subtypes;
            return subs.begin() != subs.end() ? 3 : 4;
        };
        std::vector<int> digs_v;
        for (int i : lands_i)
        { if (!kept[i] && digs_i(i) && !islet_i(i)) { digs_v.push_back(i); } }
        std::stable_sort(digs_v.begin(), digs_v.end(),
                         [&](int a, int b) { return dig_rank(a) < dig_rank(b); });
        // Three dig slots at rung 8 -- the list is sized to the hand limit, not rationed.
        const int dig_quota = variant >= ThDiscard::Shop ? 3 : (have_engine ? 1 : 2);
        int dig_kept = 0;
        for (int i : digs_v) { if (dig_kept < dig_quota) { take(i); ++dig_kept; } }

        // 4. FALLBACK for slots the hand could not fill (or capacity past the list): alternate
        //    untapped and dig while both remain, then take whatever is left. Alternating matters
        //    because the list is a shopping list, not a ration -- a hand that is all diggers should
        //    not end up keeping seven diggers just because the untapped slots went unfilled. The
        //    only way to finish with a lopsided hand is to have held nothing else.
        if (variant >= ThDiscard::Shop)
        {
            std::vector<int> rest_up, rest_dig, rest_other;
            for (int i : lands_i)
            {
                if (kept[i]) { continue; }
                if (untapped_i(i))   { rest_up.push_back(i); }
                else if (digs_i(i))  { rest_dig.push_back(i); }
                else                 { rest_other.push_back(i); }
            }
            std::stable_sort(rest_dig.begin(), rest_dig.end(),
                             [&](int a, int b) { return dig_rank(a) < dig_rank(b); });
            std::stable_sort(rest_up.begin(), rest_up.end(),
                             [&](int a, int b) { return untapped_rank(a) < untapped_rank(b); });
            // fallback_rank: earlier = more wanted. Interleave, then the remainder, then the rest.
            std::size_t a_i = 0, d_i = 0;
            int order = 0;
            if (variant >= ThDiscard::Islet)
            {
                // Extras go to DIGGERS first, not alternating: the list has already bought the mana
                // it needs, so spare capacity is worth more as selection than as a fourth land that
                // taps for the same colours.
                for (int i : rest_dig) { fallback[i] = ++order; }
                for (int i : rest_up)  { fallback[i] = ++order; }
            }
            else
            while (a_i < rest_up.size() || d_i < rest_dig.size())
            {
                if (a_i < rest_up.size())  { fallback[rest_up[a_i++]]  = ++order; }
                if (d_i < rest_dig.size()) { fallback[rest_dig[d_i++]] = ++order; }
            }
            for (int i : rest_other) { fallback[i] = ++order + 100; }
        }
    }

    // Lower band = shed earlier. Bands, not a comparator chain, so the ladder rungs compose and a
    // stable_sort keeps hand order inside a band (the historical tie-break). Band 99 means "do not
    // name this card at all" -- omitting it from the provider order is how a card gets pushed
    // behind everything the provider DID name (see CleanupDiscardRankingWithOrder).
    auto band = [&](int i) -> int
    {
        const Card&           c = ap.hand[i];
        const CardDefinition* d = CardDatabase::Instance().LookupCached(c);
        const CardParams*     p = d != nullptr ? &d->params : nullptr;
        const bool is_land   = CleanupDiscardIsLand(c);
        const bool depletion = p != nullptr && p->enters_tapped_with_depletion > 0;
        const bool digs      = p != nullptr && (p->cycling_cost.has_value()
                                             || p->sacrifice_draw_cost.has_value());
        const bool tapped    = p != nullptr && p->enters_tapped && !depletion;
        // Rungs 4+ ask the STATE-AWARE predicate instead of the raw flag, because "enters tapped"
        // is not a property of the card for two of this deck's lands: Frostboil Snarl
        // (etb_untap_reveal_subtypes) enters untapped only while a revealable Island/Mountain card
        // is in hand, and Steam Vents (etb_pay_life_to_untap) only if the life is there to pay.
        // A Snarl we cannot currently turn on IS a tapped land and should be shed like one.
        const bool tapped_now = d != nullptr && !depletion && LandWouldEnterTapped(s, *d);
        const int  colors     = p != nullptr ? static_cast<int>(p->produces.size()) : 0;

        // Reliquary Tower protection is CONDITIONAL on the Tower still having a job. Its whole
        // value is that PLAYING it ends this decision, so protecting it is only right while that
        // is still true:
        //   * another no-max-hand-size land already in play -> the one in hand is DEAD, exactly
        //     like a duplicate Land's Edge. Shed it first. (Reached only via a non-cleanup discard:
        //     CleanupStep skips the shed entirely once one is out.)
        //   * a Land's Edge outlet in play -> SPARE, band 2, same as a duplicate land. The outlet
        //     costs no mana to activate ("Discard a land card:"), so excess cards are never
        //     discarded to a hand limit while it is out -- they are converted to damage instead.
        //     That kills the Tower's one distinguishing ability outright, and what is left is a
        //     land producing only {C}: no dig, no depletion burst, and the only mana in the deck
        //     that cannot help cast {1}{R}{R}. Redundant ability plus the weakest mana is the same
        //     shape as a duplicate, which is why it shares that band rather than a new one.
        //   * otherwise -> protect it. 90 is above every band any rung can produce (kept tops out
        //     at 59, the rung-8 fallback at 18, surplus at 8).
        //
        // The absolute version also silently outranked the duplicate rule: copies_seen() counts the
        // battlefield, so a second Tower WAS already classified spare -- the early return just never
        // let that fire. Same failure shape as the band-9 bug, one layer up.
        if (p != nullptr && p->no_max_hand_size)
        {
            if (tower_in_play) { return 0; }
            if (!outlet_in_play) { return 90; }
            // ADOPTED (user-approved), but BELOW MEASUREMENT and labelled as such: on its own it
            // never changed a game -- 0 diverged of 320k d0 games -- because a cleanup discard
            // essentially never happens while a free outlet is out (that is the same fact that
            // makes the Tower dead here). It only reaches a decision through the Land's Edge pitch,
            // and there it is worth +0.0000. Kept because the reasoning is structural rather than
            // fitted, and it costs nothing; NOT kept on the strength of a number.
            static const bool s_tower_spare = EnvOn("MTG_TH_TOWER_SPARE", true);
            if (s_tower_spare && variant >= ThDiscard::Dup) { return 2; }
        }

        if (!is_land)
        {
            // NONLANDS. Only two are ever spare, and both for a structural reason rather than a
            // value judgement, so this needs no card names. Both are safe to shed AHEAD of a real
            // land -- a land is at least Land's Edge ammunition, and these two are not even that --
            // but they are not equally spare:
            //
            //   * a duplicate Land's Edge (discard_land_damage) is DEAD. One outlet is all the deck
            //     can use, so the second copy does nothing the first does not, and shedding it
            //     costs literally nothing. Band 0 -- the best discard in the deck.
            //   * retrace (Throes of Chaos) WEAKLY DOMINATES shedding a land, which is a structural
            //     argument rather than a measured preference: whatever land we would have discarded
            //     instead is still in hand afterwards, and it is exactly what pays the retrace cost
            //     when we cast Throes from the graveyard. So shedding Throes cannot come out behind
            //     shedding that land -- at worst it DEFERS the same land discard to a turn where we
            //     choose it with more information, and in the games where Throes is never cast at
            //     all it was pure gain. Band 1: behind the free discard, ahead of every real card.
            //     (Promoting it here from band 6 improved the train sum on both metrics, which is
            //     what a genuinely dominant rule should do -- searched -0.0053 -> -0.0073, d0
            //     -0.0070 -> -0.0130. Any residual downside is churn, not a real cost.)
            //
            // Everything else -- Treasure Hunt above all -- is deliberately NOT named, which puts
            // it behind every land in the ranking: kept unless the hand is nothing but engine.
            if (variant >= ThDiscard::Dup)
            {
                if (p != nullptr && p->discard_land_damage > 0
                    && copies_seen(c.m_name) > 1)                                  { return 0; }
                if (p != nullptr && p->retrace)                                    { return 1; }
            }
            return 99;
        }

        // A duplicate LAND is the safest land discard -- the second copy is the one card in hand
        // guaranteed to be doing nothing the first is not -- but it still ranks behind the two
        // spare nonlands above, because a land is at least ammunition and they are not.
        //
        // "The second copy", NOT every copy: copies_seen() is symmetric across copies, so banding
        // on it alone marked BOTH Temples of Epiphany spare and shed the pair while the keep set
        // had bought one (keeping a surplus basic Island instead) -- the dup rule silently
        // outranking the shopping list, the same failure shape as the Tower and band-9 bugs (th
        // s3003 gi=229: T6 vs the keep-one-Temple line's T5, found by the searched fan). A copy
        // the keep set bought IS the first copy: it falls through to its slot band; only UNKEPT
        // duplicates are spare. Variants below Keep have no keep set and are unchanged.
        if (variant >= ThDiscard::Dup && copies_seen(c.m_name) > 1
            && !(variant >= ThDiscard::Keep && kept[static_cast<std::size_t>(i)])) { return 2; }

        // Rung 6 -- QUOTAS per role, not a static category order. A flooded hand's real mistake is
        // not "kept the wrong land", it is "kept ten lands that all do the same job", so each role
        // gets a number it is worth holding and everything past it is surplus:
        //
        //   no-max-hand   never shed        it ends the decision
        //   untapped dual 2                 "a couple of untapped, ideally tapping both colours"
        //   untapped mono 1                 a colour-restricted source is worth less of the quota
        //   depletion     1 PER NAME        worth having, but a second Skerry adds nothing a first
        //                                   one does not -- so the quota is per card, not per role
        //   digger        1 with a Treasure Hunt in hand, else 2 -- the dig role matters less when
        //                                   the engine we would be digging for is already here
        //   tapped mana   0                 nothing to reserve; this is the surplus role
        //
        // Surplus lands are shed in role order (tapped first); anything inside quota sits behind
        // every surplus card. Quota is filled in HAND ORDER, which keeps the historical tie-break.
        // Rung 7 -- the keep set decides: anything holding a slot sits behind everything that does
        // not, and the surplus is shed in role order.
        if (variant >= ThDiscard::Keep)
        {
            // Kept: slot 1 (most wanted) sheds LAST. 60 keeps every kept land above every
            // fallback and surplus band below.
            if (kept[i]) { return 60 - std::min(keep_slot[i], 30); }
            if (variant >= ThDiscard::Shop && fallback[i] > 0)
            {
                // Ranked fallback: wanted-est last to be shed. 19 down to 3, so every fallback land
                // still sheds after the unranked surplus below and before anything holding a slot.
                return std::max(3, 19 - fallback[i]);
            }
            static const int kSurplus[7] = { 8, 8, 5, 6, 3, 4, 4 };
            return kSurplus[th_role(c)];
        }
        if (variant >= ThDiscard::Mix)
        {
            if (p != nullptr && p->no_max_hand_size) { return 9; }
            const int role  = th_role(c);
            const int quota = role == 5 ? 2
                            : role == 6 ? 1
                            : role == 3 ? 1
                            : role == 2 ? (have_engine ? 1 : 2)
                            : 0;
            // Position of this card among the ones competing for the same quota. Depletion lands
            // count PER NAME (each is its own burst source); every other role counts per role.
            int ahead = 0;
            for (int j = 0; j < i; ++j)
            {
                const Card& h = ap.hand[j];
                if (h.m_is_staged || !CleanupDiscardIsLand(h)) { continue; }
                if (role == 3 ? (h.m_name == c.m_name) : (th_role(h) == role)) { ++ahead; }
            }
            const bool surplus = ahead >= quota;
            // 3..8 for surplus (tapped soonest), 10+ for anything holding a quota slot.
            static const int kShedOrder[7] = { 8, 6, 5, 7, 3, 4, 4 };
            return surplus ? kShedOrder[role] : 10 + role;
        }

        // Rungs 4/5 -- the faithful "tapped unless it digs or is a depletion land" rule.
        if (variant >= ThDiscard::TapDig)
        {
            if (tapped_now && !digs) { return 3; }   // its ETB only ever pays if the land is PLAYED
            if (depletion)           { return 6; }   // two mana off one land: keep it
            // Rung 4 stops here: diggers and untapped lands are all NEUTRAL at 4, so this rung
            // isolates the tapped rule and nothing else. Rung 5 then splits the untapped mana by
            // colour count -- the mono source goes first, because the dual is the one that can
            // still cast both halves of the deck -- and parks the diggers alongside the duals:
            // worth more than a spare mono source, not worth protecting behind the depletion lands
            // (rung 3 showed that protection costs more than it returns, since cycling one spends
            // BOTH mana and a card that was Land's Edge ammunition).
            if (variant >= ThDiscard::Mono) { return digs ? 5 : (colors <= 1 ? 4 : 5); }
            return 4;
        }

        if (variant >= ThDiscard::NoDig && digs) { return have_engine ? 7 : 8; }
        if (variant >= ThDiscard::Tapped)
        {
            if (tapped)    { return 3; }   // plain tapped land: the ETB only pays if it is PLAYED
            if (depletion) { return 5; }   // two mana off one land -- keep over a plain untapped one
            return 4;                      // plain untapped land
        }
        return 4;
    };

    std::vector<int> pref;
    for (int i = 0; i < n; ++i)
    {
        if (ap.hand[i].m_is_staged) { continue; }
        if (band(i) < 99) { pref.push_back(i); }
    }
    std::stable_sort(pref.begin(), pref.end(),
                     [&](int a, int b) { return band(a) < band(b); });
    return CleanupDiscardRankingWithOrder(s, required_pieces, pref);
}

// How many ranked cards the ROLLOUT's cleanup shed branches over. TH is the only deck that opts in
// Throes of Chaos retraces by discarding a land. That is the same question as the cleanup shed and
// the Land's Edge pitch -- "which land in this hand is worth least to me" -- so it reuses the same
// ranking instead of holding a third, differently-arbitrary opinion. The base rule (first land in
// hand order) is the defect this deck has now measured twice.
//
// Note the direction of the two rules agrees rather than conflicting: the cleanup ranking puts
// Throes ITSELF at band 1 (shedding it weakly dominates shedding a land, because the land stays in
// hand to pay a later retrace), and this makes the retrace it enables spend the cheapest land. The
// unifying idea is one notion of "least valuable card", asked by three different callers.
std::vector<int> TreasureHuntProvider::RetraceDiscardCandidates(
    const GameState& s, int /*controller*/, const std::vector<int>& hand_land_indices) const
{
    // ADOPTED (user-approved). MTG_TH_RETRACE_RANKED=0 restores the historical hand-order pick.
    // MEASURED (test/th_retrace_ab.sh, 96k d0 games/arm, fresh seeds 32032..39039):
    //     d0  5.4391 -> 5.4389   -0.0002   t=-4.32   8/8 seeds better   SEPARATED
    //     divergence 32 of 96000 games -- 24 FASTER, 8 slower
    // Read the 3:1 ratio, not the -0.0002: this decision is rare (~1 per 8.6 games) but genuinely
    // contested when it fires -- 98% of retraces have 2+ lands to choose from and the ranking moves
    // the pick in 60% of them. Contrast the Land's Edge pitch, which burns the whole hand 97.6% of
    // the time so its ORDER is usually unobservable, and whose divergences were a near coin flip.
    static const bool s_ranked = EnvOn("MTG_TH_RETRACE_RANKED", true);
    if (!s_ranked) { return hand_land_indices; }

    // Rank the WHOLE hand, then keep only the lands this caller offered, in ranked order. Filtering
    // after ranking (rather than ranking a filtered list) matters: the ranking's bands are relative
    // to the whole hand -- the Tower rule, for instance, asks what else is in play and in hand.
    // Full ranking, NOT the hook: the hook now returns only the top pick (the searched pass's
    // candidate set), and this multi-land cost needs the complete ordering.
    const std::vector<int> ranked = CleanupDiscardFullRanking(s, s.m_required_pieces);
    std::vector<int> out;
    out.reserve(hand_land_indices.size());
    for (int i : ranked)
    {
        if (std::find(hand_land_indices.begin(), hand_land_indices.end(), i) != hand_land_indices.end()
            && std::find(out.begin(), out.end(), i) == out.end())
        { out.push_back(i); }
    }
    // Anything the ranking did not name keeps its historical hand order at the back, so a short
    // ranking can never drop a legal choice and make the cost unpayable.
    for (int i : hand_land_indices)
    { if (std::find(out.begin(), out.end(), i) == out.end()) { out.push_back(i); } }
    return out;
}

// -- it is the only one that reaches this decision often enough for a plan variant to be anything
// but wasted enumeration (336 discards per 400 d0 games; five suite decks never reach it at all).
int TreasureHuntProvider::CleanupDiscardSearchWidth() const
{
    static const int w = []() -> int
    {
        const char* e = std::getenv("MTG_TH_DISCARD_WIDTH");
        if (e == nullptr || *e == '\0') { return 1; }   // DEFAULT: see the sweep before raising
        const int n = std::atoi(e);
        return n < 1 ? 1 : n;
    }();
    return w;
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
    // (3) a land drop THIS TURN -> the drop is still OPEN, so a deferred Treasure Hunt can play a
    //     drawn Reliquary Tower / Land's-Edge enabler as the drop and keep the flood. This is the
    //     ONLY no-payoff justification: an open drop is the mechanism that prevents the cleanup
    //     discard (design rule 2026-07-15). If Treasure Hunt draws no enabler the deferred drop
    //     still develops the best normal land (ApplyPlanDirect, gi=881) -- a whiff-and-develop, not
    //     a waste.
    //
    //     The land-fold enumeration (add_for_land) plays the candidate land into the trial state
    //     BEFORE this gate runs, so a "play a land AND cast Treasure Hunt" plan shows
    //     lands_played_this_turn==1 here. b4f2a3a credited that (`lands_played_this_turn > 0`) to
    //     keep the play-a-land+dig branch legal -- but with the drop already SPENT and no enabler
    //     out, a Reliquary/Land's-Edge the dig reveals CANNOT be played this turn, so the flood is
    //     discarded to cleanup. That "dig anyway once the drop is spent" line only ever looks safe
    //     under CLAIRVOYANT lookahead (it knows the flood is harmless); a real pilot would hold
    //     Treasure Hunt. Strict flood (THStrictFlood, ADOPTED default ON) drops this spent-drop
    //     clause: with the drop spent, this gate refuses the flood engine, so the ONLY way to cast it
    //     with no outlet is the defer plan (drop still open). That IS the user's force-defer ("don't
    //     play a land before TH/Throes") -- nothing else forces it, and the after-play stays a search
    //     decision. MTG_TH_STRICT_FLOOD=0 restores the b4f2a3a spent-drop clause (byte-identical A/B).
    const bool drop_open = ap.lands_played_this_turn < ap.LandDropsAvailable();
    if (drop_open || (!THStrictFlood() && ap.lands_played_this_turn > 0)) { return true; }   // (3)

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
            bool is_dork = (d->tmpl == CardTemplate::ManaDork && CanTapNow(p, s.battlefield)) || d->params.mana_rock;
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

bool TreasureHuntProvider::HoldDeferredDropForLethal(const GameState& s, int controller) const
{
    // After a deferred Treasure Hunt resolves with the land drop still open: HOLD the drop when the
    // lands now in hand are the MARGINAL Land's Edge ammunition for a lethal this turn. Developing
    // the drop (play_drawn_flood_keep_land / the engine's generic land play) would spend a land that
    // is worth `rate` face damage as ammo, dropping the count below lethal -- and the fire-count
    // heuristic (LandsEdgeHeuristicFireCount) then holds the remaining lands, slipping the kill a
    // full turn (s1 gi0: 10 lands -> play one -> 9 -> not lethal -> hold -> T4 instead of T3). Only
    // the marginal case (developing would cost the kill) holds; a hand with strictly MORE than
    // lethal ammo still develops (playing one leaves it lethal, so the win turn is unchanged and we
    // keep the extra land in play). MTG_NO_LE_HOLD_LETHAL disables -> legacy develop-always (A/B).
    static const bool s_off = EnvOn("MTG_NO_LE_HOLD_LETHAL");
    if (s_off) { return false; }

    // Land's Edge damage-per-land currently on the battlefield.
    int rate = 0;
    for (const Permanent& p : s.battlefield)
    {
        if (p.controller_index != controller) { continue; }
        const CardDefinition* d = CardDatabase::Instance().LookupCached(p.card);
        if (d && d->params.discard_land_damage > 0) { rate = std::max(rate, d->params.discard_land_damage); }
    }
    if (rate <= 0) { return false; }

    int lands_in_hand = 0;
    for (const Card& c : s.players[controller].hand)
    {
        const CardDefinition* d = CardDatabase::Instance().LookupCached(c);
        if (d ? d->card.IsLand() : c.IsLand()) { ++lands_in_hand; }
    }
    if (lands_in_hand == 0) { return false; }

    const Player& opp = s.players[1 - controller];
    const int lethal_lands = (opp.life + rate - 1) / rate;
    // Marginal: already lethal, but developing the single open drop would drop below lethal. (Only
    // one land is ever played by the deferred-drop step, so the -1 test is exact.)
    return lands_in_hand >= lethal_lands && (lands_in_hand - 1) < lethal_lands;
}

bool TreasureHuntProvider::HoldDeferredDropForFurtherDig(const GameState& s, int controller) const
{
    // HOLD the still-open deferred drop when the hand is flooding, nothing keeps it yet, and another Treasure
    // Hunt is castable THIS TURN. Developing now (the generic fallback) spends the only way to play a
    // Reliquary Tower one dig too early: dig 2 then reveals the Tower with no drop left and the flood is
    // discarded at cleanup instead of becoming Land's Edge ammo (s2 gi1: engine T5, human T4 -- the human
    // held the drop through BOTH digs and played the revealed Tower). ADOPTED default ON;
    // MTG_NO_TH_HOLD_FOR_DIG restores eager-develop for A/Bs (presence-tested, so =0 also disables).
    // Preconditions guaranteed by the caller: pre-combat, drop open, HoldDeferredDropForLethal declined,
    // PostDrawKeepLandName found no keep land.
    static const bool s_off = EnvOn("MTG_NO_TH_HOLD_FOR_DIG");
    if (s_off) { return false; }

    const Player& ap = s.players[controller];
    if (static_cast<int>(ap.hand.size()) <= 7) { return false; }   // not flooding -> nothing to protect

    for (const Permanent& p : s.battlefield)                    // already safe -> develop as usual
    {
        if (p.controller_index != controller) { continue; }
        const CardDefinition* d = CardDatabase::Instance().LookupCached(p.card);
        if (d && d->params.no_max_hand_size && d->card.IsLand()) { return false; }
    }

    // Another dig payable from mana available WITHOUT the held land (untapped sources + float, so
    // holding can never starve the dig it is waiting for). Mirrors TurnSolver::BuildPool.
    ManaPool pool;
    for (const Permanent& p : s.battlefield)
    {
        if (p.controller_index != controller || p.tapped) { continue; }
        const CardDefinition* d = CardDatabase::Instance().LookupCached(p.card);
        if (!d) { continue; }
        bool is_land = (d->tmpl == CardTemplate::BasicLand);
        bool is_dork = (d->tmpl == CardTemplate::ManaDork && CanTapNow(p, s.battlefield)) || d->params.mana_rock;
        if (!is_land && !is_dork) { continue; }
        AddSourceToPool(pool, s, *d);
    }
    if (FloatLeftoverManaEnabled()) { pool.AddPool(s.floating_mana); }
    for (const Card& c : ap.hand)
    {
        const CardDefinition* d = CardDatabase::Instance().LookupCached(c);
        if (!d || d->tmpl != CardTemplate::DrawUntilNonland) { continue; }
        if (pool.CanPay(d->card.m_mana_cost)) { return true; }
    }
    return false;
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
        static const bool s_cascade_lethal = !EnvOn("MTG_NO_CASCADE_LETHAL");
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

std::string GoblinsProvider::ForcedEarlyLandName(const GameState& s, int controller) const
{
    // How many opening turns to force (MTG_FORCED_EARLY_LAND_TURNS, default 1).
    //
    // The window is a genuine trade, and it is NOT "more is better". A forced land does not delete a
    // branch, it DEFERS one: the singleton utility lands stay in hand and fan out on a later turn
    // instead. The land fan-out multiplies the spell-subset enumeration (plans ~ land options x
    // spell subsets), and that subset space grows fast with turn number -- so a 2-way land branch on
    // turn 1 is cheap and the same branch on turn 3 is not. Forcing turns 1-2 pushed the singleton
    // out to turn 3+ and measured +1.87% MORE rollout calls on disjoint seeds, the opposite of the
    // intended saving.
    //
    // Turn 1 alone keeps the part that motivated the rule -- red available for Lightning Bolt on the
    // opening turn, which a Cavern (colored_creature_only) and a Three Tree City ({C} until turn 3)
    // cannot provide -- while letting the singleton leave hand on turn 2, before the multiplier gets
    // expensive.
    static const int s_early_turns = []{
        const char* e = std::getenv("MTG_FORCED_EARLY_LAND_TURNS");
        return (e && *e) ? std::atoi(e) : 1;
    }();
    if (s.turn_number > s_early_turns) { return {}; }
    for (const Card& c : s.players[controller].hand)
    {
        if (c.m_name == "Mountain") { return "Mountain"; }
    }
    return {};   // no Mountain in hand -> no prune, the search fans out as usual
}

// ---- NC tempo bonus (NcLandDropTempoBonus) -----------------------------------------------

static int CountLandsInPlay(const GameState& s, int controller)
{
    int lands = 0;
    for (const Permanent& p : s.battlefield)
    {
        if (p.controller_index == controller && p.card.IsLand()) { ++lands; }
    }
    return lands;
}

double GenericProvider::NcLandDropTempoBonus(const GameState& s, int controller) const
{
    // SAFE conservative default for ANY deck (incl. unknown/new ones): a small tempo bonus that only
    // breaks decisions the mana-optimistic NC objective already considers close, and ONLY while still
    // establishing the mana base (<2 lands = turns 1-2 on curve, where playing a land is pure tempo for
    // every archetype -- even a land-pitch deck needs its first lands). Off once the base exists (a land
    // in hand may then be a resource) or when the archetype wants to bank/hold (PreferHoldLandDrop). This
    // is why an UNGATED bonus wrecks Treasure Hunt (-18/400 games) but this gated default does not.
    if (PreferHoldLandDrop(s, controller)) { return 0.0; }
    return CountLandsInPlay(s, controller) < 2 ? 0.5 : 0.0;
}

double AntiLifegainProvider::NcLandDropTempoBonus(const GameState& s, int controller) const
{
    // Anti-Lifegain wins through dorks + an on-curve enabler (Tainted Remedy) deploy and has NO
    // land-as-resource mechanic, so developing mana is essentially always correct -- reward the land
    // drop every turn (ungated), not just while establishing the base. Measured best on this deck
    // (dLP -0.040 vs -0.017 for the gated generic rule over 400 autonomous games).
    (void)controller;
    (void)s;
    return 1.0;
}

// ---- HinataProvider ---------------------------------------------------------

// Cleanup discard: the AI-authored shed order (discard-analysis stage, 2026-08-07). The five
// named cards are dead or near-dead against a passive goldfish opponent -- Memory Lapse / Remand
// counter spells that are never cast, Distorting Wake / Icy Blast are X-tempo with nothing to
// bounce or tap that matters -- so they shed before anything else; omission keeps the rest on
// the shared MV ranking, where the required-piece scope protects the singleton Hinata (the
// pre-rule base shed her into a loss holding four Reality Spasms, gi21). Searched trial tables:
// 98.3% label-optimal vs base 95.7%; outcome d0 -0.0070 t=-2.99 with every worsened game
// churn-classified at unbounded (gi472 is BETTER unbounded under the rule).
std::vector<int> HinataProvider::CleanupDiscardCandidates(
    const GameState& s, const std::vector<std::string>* required_pieces) const
{
    static const bool s_order = EnvOn("MTG_HINATA_DISCARD_ORDER", true);
    if (!s_order) { return GenericProvider::CleanupDiscardCandidates(s, required_pieces); }
    // USER-AUTHORED KEEP PRIORITY (2026-08-07), expressed as ranks: 1 = keep hardest, 13 =
    // shed first. The shed list below is this list read backwards. Cantrips and Soulfire move
    // between ranks with the board, which is why this is a rank assignment and not a static
    // name order.
    //
    //    1 Hinata #1                    8  Soulfire Eruption #1 without Hinata
    //    2 Reality Spasm #1             9  Magma Opus
    //    3 Crackle with Power #1        10 extra mana / extra cantrips
    //    4 Sol Ring (never shed)        11 extra Crackle
    //    5 Reality Spasm #2 or Irencrag 12 anything else
    //    6 mana up to 5 + colours       13 inert spells (always shed if available)
    //    7 1-2 cantrips if pieces missing
    //
    // MANA-SCREWED override (USER 2026-08-07, from vs-searched residuals gi541/gi1848): when the
    // mana requirement is NOT met we cannot cast Hinata, let alone Soulfire Eruption, so the fat
    // spells stop being assets and the cheap digging does the work. Soulfire drops below the
    // cantrips (but stays above Magma Opus, which the list already values less), and TWO cantrips
    // are held instead of one, Ponder first. Consequence worth naming: holding the second cantrip
    // is what lets Magma Opus reach the top of the shed list at all -- with only one held, the
    // spare cantrip outranks it and Magma Opus is never shed (gi541).
    // Ranks are spaced so a variant can slot between two of them without renumbering.
    enum Keep {
        kHinata1 = 10, kSpasm1 = 20, kCrackle1 = 30, kSolRing = 35, kSpasm2OrIrencrag = 40,
        kManaNeeded = 50, kSoulfireWithHinata = 60, kCantripNeeded = 70,
        kSoulfireNoHinata = 80, kSoulfireScrewed = 85, kMagmaOpus = 90,
        kExtraManaOrCantrip = 100, kExtraCrackle = 110, kAnythingElse = 120, kInert = 130 };
    static const char* const kInertNames[] = {
        "Distorting Wake", "Icy Blast", "Memory Lapse", "Remand" };
    auto is_cantrip = [](const std::string_view nm)
    { return nm == "Expressive Iteration" || nm == "Ponder" || nm == "Preordain"; };
    // Which cantrip earns a kept slot: Ponder first (USER), then Preordain, then the 2-mana
    // Expressive Iteration -- the cheapest, deepest dig goes first when we are digging for land.
    auto cantrip_pref = [](const std::string_view nm)
    { return nm == "Ponder" ? 0 : (nm == "Preordain" ? 1 : 2); };

    const Player& ap = s.players[s.active_player_index];
    const int n = static_cast<int>(ap.hand.size());

    // ---- Mana census (USER: "up to 5 mana ... 2 blue, 1 red and 1 white at least, including
    // what is on board"). EVERY producer counts -- land, dork, or rock -- by the TOTAL mana it
    // provides (Sol Ring 2, bounce lands 2), colours per-pip (a wild source counts toward every
    // colour; slightly optimistic, which is acceptable for a shed preference).
    // `quality` (lower = keep sooner) breaks ties between sources the amount/colour census scores
    // as equal but that are NOT interchangeable in play. Measured, not assumed: a naive
    // "untapped first" tie-break tested WORSE (0 better / 2 worse over 12000 games) because
    // enters_tapped=false is a bad proxy for usable-now -- it is also false for a summoning-sick
    // 2-mana dork (gi1595) and for Reflecting Pool, which only makes what your OTHER lands make,
    // i.e. no white off Island + Boilerworks (gi2869). The tiers below say what those games say.
    // Tiers in shed-later-to-shed-sooner order. The CONDITIONAL lands rank below the dork: a filter
    // land makes nothing by itself and a Reflecting Pool only makes what your OTHER lands make -- a
    // spare copy next to one already on the battlefield adds no colour at all (gi2869) -- whereas
    // the dork, once it resolves, taps for any colour.
    enum SrcQuality { kSrcUntappedLand = 0, kSrcTappedLand = 1, kSrcDork = 2, kSrcConditional = 3 };
    struct Contrib { int amt = 0, w = 0, r = 0, u = 0; int quality = kSrcDork; };
    auto contrib_of = [](const CardDefinition* d) -> Contrib
    {
        Contrib c;
        if (d == nullptr) { return c; }
        const bool src = d->card.IsLand() || d->tmpl == CardTemplate::ManaDork
                         || d->params.mana_rock;
        if (!src) { return c; }
        const auto& prod = d->params.produces;
        if (prod.empty() && !d->card.IsLand()) { return c; }
        c.amt = std::max(1, d->params.produces_amount);
        c.quality = !d->card.IsLand()                              ? kSrcDork
                  : (d->params.reflecting || d->params.is_filter)  ? kSrcConditional
                  : d->params.enters_tapped                        ? kSrcTappedLand
                                                                   : kSrcUntappedLand;
        auto makes = [&](Color col)
        { return prod.empty() || std::find(prod.begin(), prod.end(), col) != prod.end(); };
        c.w = makes(Color::White) ? 1 : 0;
        c.r = makes(Color::Red)   ? 1 : 0;
        c.u = makes(Color::Blue)  ? 1 : 0;
        return c;
    };
    int need_amt = 5, need_w = 1, need_r = 1, need_u = 2;    // deficits, decremented below
    auto apply = [&](const Contrib& c)
    {
        need_amt -= c.amt;
        need_w -= c.w; need_r -= c.r; need_u -= c.u;
    };
    for (const Permanent& p : s.battlefield)
    {
        if (p.controller_index != s.active_player_index) { continue; }
        apply(contrib_of(CardDatabase::Instance().LookupCached(p.card)));
    }
    const bool hinata_board = HinataInPlay(s);

    // Hand census: which cards exist, and (greedily) which mana sources are the ones we need.
    // mana_pick[i] is the 1-based order the greedy kept source i in; 0 = not needed.
    std::vector<int> mana_pick(static_cast<std::size_t>(std::max(0, n)), 0);
    std::vector<int> mana_idx;
    int hinata_hand = 0, crackle_hand = 0;
    for (int i = 0; i < n; ++i)
    {
        if (ap.hand[i].m_is_staged) { continue; }
        const CardDefinition* d = CardDatabase::Instance().LookupCached(ap.hand[i]);
        if (d == nullptr) { continue; }
        if (d->params.hinata_cost_reducer) { ++hinata_hand; }
        if (ap.hand[i].m_name == "Crackle with Power") { ++crackle_hand; }
        // Sol Ring is never shed (USER: "the card is amazing"), so its mana is guaranteed --
        // charge it to the requirement up front and leave it out of the greedy.
        if (ap.hand[i].m_name == "Sol Ring") { apply(contrib_of(d)); continue; }
        if (contrib_of(d).amt > 0) { mana_idx.push_back(i); }
    }
    // Greedy: repeatedly keep the hand source that closes the most of the remaining deficit
    // (colours weighted, so a needed colour beats raw quantity), until the requirement is met.
    // Quantity and colour are what the requirement asks for, so source quality stays a pure
    // TIE-BREAK: a worse-quality source that closes a colour the better one cannot still wins.
    // The case that motivated it (regression d0 gi523): board Island + Izzet Boilerworks = 3 mana,
    // no white; hand held Forbidden Orchard (untapped, any colour) and Mystic Monastery (tapped,
    // U/R/W). The census scored them equal, hand order shed the ORCHARD, and turn 4 could not cast
    // Hinata ({1}{U}{R}{W}) off a freshly-tapped Monastery -- keeping the Orchard wins on 5.
    // MTG_HINATA_SRC_TIEBREAK=0 restores the pre-change pure hand order for an exact A/B.
    // (Two other orderings were swept and lost: a naive "untapped first" -- which measured 0 better
    // / 2 WORSE over 12000 games, because enters_tapped=false is a bad proxy for usable-now, being
    // equally false for a summoning-sick dork and for Reflecting Pool -- and one ranking the
    // conditional lands ABOVE the dork. See docs/design/per-deck-discard-analysis-phase.md.)
    static const bool s_src_tiebreak = EnvOn("MTG_HINATA_SRC_TIEBREAK", true);
    int picks = 0;
    while (need_amt > 0 || need_w > 0 || need_r > 0 || need_u > 0)
    {
        int best = -1, best_gain = 0, best_amt = 0, best_key = kSrcConditional + 1;
        for (int i : mana_idx)
        {
            if (mana_pick[static_cast<std::size_t>(i)]) { continue; }
            const Contrib c = contrib_of(CardDatabase::Instance().LookupCached(ap.hand[i]));
            const int gain = std::min(c.amt, std::max(0, need_amt))
                           + 2 * (std::min(c.w, std::max(0, need_w))
                                + std::min(c.r, std::max(0, need_r))
                                + std::min(c.u, std::max(0, need_u)));
            const bool better = gain > best_gain
                || (gain == best_gain && gain > 0
                    && (c.amt > best_amt
                        || (c.amt == best_amt && s_src_tiebreak && c.quality < best_key)));
            if (better)
            { best = i; best_gain = gain; best_amt = c.amt; best_key = c.quality; }
        }
        if (best < 0) { break; }                       // nothing left that helps
        mana_pick[static_cast<std::size_t>(best)] = ++picks;
        apply(contrib_of(CardDatabase::Instance().LookupCached(ap.hand[best])));
    }
    const bool mana_ok = need_amt <= 0 && need_w <= 0 && need_r <= 0 && need_u <= 0;
    // Cantrips are for FINDING what is missing: pieces or lands (USER). With Hinata, a Crackle
    // and the mana already assembled, they dig for nothing and drop to the "extra" rank.
    // How many to hold (USER): "1 cantrip if missing a combo piece and 2 if missing Hinata" --
    // and 2 whenever the MANA is what is missing, since digging for land is the whole plan then.
    const bool have_hinata = hinata_board || hinata_hand > 0;
    const bool combo_ready = have_hinata && crackle_hand > 0 && mana_ok;
    const int  cantrip_limit = combo_ready ? 0 : ((have_hinata && mana_ok) ? 1 : 2);
    // Hand out the kept cantrip slots by preference (Ponder > Preordain > Expressive Iteration)
    // rather than by hand order.
    std::vector<char> cantrip_keep(static_cast<std::size_t>(std::max(0, n)), 0);
    {
        std::vector<int> cantrips;
        for (int i = 0; i < n; ++i)
        {
            if (ap.hand[i].m_is_staged) { continue; }
            if (is_cantrip(ap.hand[i].m_name.str())) { cantrips.push_back(i); }
        }
        std::stable_sort(cantrips.begin(), cantrips.end(), [&](int a, int b)
        { return cantrip_pref(ap.hand[a].m_name.str()) < cantrip_pref(ap.hand[b].m_name.str()); });
        const int keep = std::min<int>(cantrip_limit, static_cast<int>(cantrips.size()));
        for (int k = 0; k < keep; ++k) { cantrip_keep[static_cast<std::size_t>(cantrips[k])] = 1; }
    }

    // ---- Rank every hand card. First-copy slots are consumed in hand order.
    bool hinata1_taken = hinata_board;   // a Hinata already in play satisfies the #1 slot
    bool spasm1_taken = false, spasm2_taken = false, crackle1_taken = false;
    bool soulfire1_taken = false, irencrag_taken = false;
    std::vector<std::pair<int, int>> ranked;   // (rank, hand index)
    for (int i = 0; i < n; ++i)
    {
        if (ap.hand[i].m_is_staged) { continue; }
        const std::string_view nm = ap.hand[i].m_name.str();
        const CardDefinition* d = CardDatabase::Instance().LookupCached(ap.hand[i]);
        int rank = kAnythingElse;
        bool inert = false;
        for (const char* in : kInertNames) { if (nm == in) { inert = true; break; } }
        if (inert)                                        { rank = kInert; }
        else if (d != nullptr && d->params.hinata_cost_reducer)
        { rank = hinata1_taken ? kAnythingElse : kHinata1; hinata1_taken = true; }
        else if (nm == "Reality Spasm")
        {
            if (!spasm1_taken)      { rank = kSpasm1;            spasm1_taken = true; }
            else if (!spasm2_taken) { rank = kSpasm2OrIrencrag;  spasm2_taken = true; }
            else                    { rank = kAnythingElse; }
        }
        else if (nm == "Crackle with Power")
        { rank = crackle1_taken ? kExtraCrackle : kCrackle1; crackle1_taken = true; }
        else if (nm == "Irencrag Feat")
        {
            // Shares the #4 slot with the second Spasm -- whichever the hand actually has.
            if (!spasm2_taken && !irencrag_taken) { rank = kSpasm2OrIrencrag; irencrag_taken = true; }
            else                                  { rank = kAnythingElse; }
        }
        else if (nm == "Sol Ring")                    { rank = kSolRing; }
        else if (nm == "Soulfire Eruption")
        {
            if (soulfire1_taken) { rank = kAnythingElse; }
            else { rank = !mana_ok       ? kSoulfireScrewed
                        : have_hinata    ? kSoulfireWithHinata
                                         : kSoulfireNoHinata;
                   soulfire1_taken = true; }
        }
        else if (nm == "Magma Opus")                  { rank = kMagmaOpus; }
        else if (is_cantrip(nm))
        {
            // Note the flood case (USER): mana BEYOND the 5+colours requirement is already
            // kExtraManaOrCantrip, which sheds ahead of a needed cantrip -- so a flooding hand
            // pitches the surplus land, not the cantrip.
            rank = cantrip_keep[static_cast<std::size_t>(i)] ? kCantripNeeded
                                                             : kExtraManaOrCantrip;
        }
        else if (contrib_of(d).amt > 0)
        {
            rank = mana_pick[static_cast<std::size_t>(i)] ? kManaNeeded : kExtraManaOrCantrip;
        }
        ranked.emplace_back(rank, i);
    }
    // Shed order = keep order reversed: highest rank first, mana value descending within a rank.
    std::stable_sort(ranked.begin(), ranked.end(), [&](const auto& a, const auto& b)
    {
        if (a.first != b.first) { return a.first > b.first; }
        return CleanupDiscardManaValue(ap.hand[a.second]) > CleanupDiscardManaValue(ap.hand[b.second]);
    });
    std::vector<int> pref;
    pref.reserve(ranked.size());
    for (const auto& r : ranked) { pref.push_back(r.second); }
    return CleanupDiscardRankingWithOrder(s, required_pieces, pref);
}

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

    // Hinata is online: return the full legal set (search-primary -- still branches over everything), but
    // ORDER it by situational need (SituationalCardRank). The plan tie-break is win-turn then plan.value, and
    // every tutor candidate shares the tutor spell's eval, so win-turn-equal fetches tie on value and the
    // FIRST listed wins. Ordering by SituationalCardRank therefore makes an indifferent (clairvoyant-tie)
    // search fetch the most-wanted MISSING piece -- e.g. Reality Spasm (rank 750) over a third Crackle when
    // two are already in hand (rank 150, duplicate) -- instead of an arbitrary library-order card. Pure
    // tie-break: a fetch that wins strictly sooner still wins.
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

// CastOrderRank: no override. Hinata's ritual (Reality Spasm, 15) and restrictor (Irencrag Feat, 18)
// tiers are now the GENERIC card-parameter tiers -- byte-identical ranks, one implementation.

// Archetype gates relocated out of TurnSolver (audit B1/B2). Both branches only pay off with
// Hinata's per-target discount online: the untap ritual (Reality Spasm) floats mana for a same-turn
// Crackle the discount makes free, and Soulfire's own-creature targets each shave {1} (and dig
// deeper). Off Hinata they are dead weight, so the solver must not branch on them.
//
// The spasm gate (MTG_HINATA_SPASM_GATE, a Reality-Spasm-needs-a-Crackle-sink emission/plan
// prune) was REMOVED 2026-07-30 with user approval: its recorded outcome was "perf/quality
// tradeoff, NOT adopted", and its redesign lives on the recovered/hinata-spasm-gate-redesign
// branch. Emission is back to the pre-gate rule: whenever Hinata is online.
bool HinataProvider::ShouldEmitUntapRitual(const GameState& s) const
{
    return HinataInPlay(s);
}
bool HinataProvider::BranchSoulfireOwnTargets(const GameState& s) const  { return HinataInPlay(s); }

// Situational "what do I need THIS turn" ranking (SituationalCardRank). HIGHER = more wanted. The decisive
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
        kRankSolRingScrewed = 1100,  // Sol Ring while mana-short: THE card to find -- 2 mana off a {1} rock
                                     // and every Reality Spasm untaps it for 2 more. Above even a screwed
                                     // land; it fixes the screw harder than any single land.
        kRankScrewedLandNow = 1050,  // mana-screwed AND can play this land NOW for mana: drawn FIRST (above
                                     // Hinata in the dig ORDER) -- you cannot cast her without the mana --
                                     // though Hinata is ALWAYS kept too (never bottomed unless a duplicate).
        kRankHinataLynchpin = 1000,  // Hinata when not yet online -- the deck is dead without her
        kRankMissingCrackle =  800,  // the lethal finisher, not yet in hand (Hinata online)
        kRankMissingSpasm   =  750,  // Reality Spasm (the ritual that powers the lethal X)
        kRankExtraSpasm     =  730,  // a 2nd+ Reality Spasm -- STACKABLE ramp (the combo untaps twice) and
                                     // straight-up better than Irencrag even in multiples
        kRankIrencrag       =  710,  // Irencrag Feat, single tier: a slightly-worse, harder-to-cast Reality
                                     // Spasm stand-in -- ABOVE Soulfire regardless of mana level
        kRankSoulfire       =  700,  // Soulfire Eruption: digs AND finishes
        kRankSolRing        =  695,  // Sol Ring, mana OK: over everything EXCEPT the actual combo pieces
        kRankEarlyRamp      =  660,  // a mana rock/dork on T1-T2 while short: accelerate into the combo --
                                     // just under the combo pieces, above lands-late / cantrips
        kRankScrewedLandLate=  560,  // mana-screwed but land drop already spent (can't play it this turn):
                                     // still a keep for next turn, but below the live combo pieces
        kRankCantrip        =  500,  // Ponder / Preordain / EI -- keep digging toward pieces
        kRankMagma          =  470,  // Magma Opus UNDER the cantrips -- a one-of secondary payoff, not a
                                     // piece to dig for (there is a reason the deck runs a single copy)
        kRankRamp           =  450,  // a mana rock/dork (past the early turns) while still short of target
        kRankExtraLand      =  380,  // a land beyond the urgent drop: still mana for the combo
        kRankDigPastLand    =  250,  // a surplus land/rock/dork while hunting Hinata -- dig past it
        kRankDeadPayoff     =  200,  // a payoff/ritual while Hinata is NOT online -- dead now
        kRankExtraSoulfire  =  170,  // a 2nd+ Soulfire Eruption -- rarely want multiples (only a rare
                                     // Reality-Spasm chain affords two); pretty low, above a strict duplicate
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

    // Situational dig/keep ranking, user-designed (2026-07-20; see docs/design/viewer-magma-opus-modeling.md,
    // SESSION 3e). Adopted after held-out validation (paired -0.035 seed4004 / -0.036 seed7000). Key ideas:
    // a 2nd+ Reality Spasm is STACKABLE ramp not a duplicate; a mana DORK (Ornithopter) is ramp not inert;
    // Sol Ring is in its own class; lands are ranked colour-aware toward casting Hinata; Magma sits under
    // the cantrips; a 2nd Soulfire goes low. The relative tier ORDER is the contract.

    const bool is_land     = c.IsLand();
    const bool is_hinata   = def && def->params.hinata_cost_reducer;
    const bool is_rock     = def && def->params.mana_rock;
    const bool is_solring  = def && def->params.mana_rock && def->params.produces_amount >= 2; // Sol Ring (2 off one rock)
    const bool is_dork     = def && !def->params.produces.empty() && !is_land && !is_rock; // mana dork (Ornithopter)
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
    int  hand_sources = 0;   // mana-makers already in hand (lands, rocks, dorks)
    for (const Card& h : ap.hand)
    {
        const CardDefinition* d = CardDatabase::Instance().LookupCached(h);
        const bool h_land = d ? d->card.IsLand() : h.IsLand();
        if (h_land) { land_in_hand = true; }
        if (h_land || (d && (d->params.mana_rock || !d->params.produces.empty()))) { ++hand_sources; }
    }
    // Total mana-makers across hand + board: the real "are we mana-screwed" signal (user: a land is
    // crucial when we have only ~2-3 total sources, extra so when we can't make a land drop from hand).
    const int  total_sources = sources + hand_sources;
    const bool need_land = land_drop_open && sources < source_target;

    // Ramp (a non-Sol-Ring rock or a dork): prioritise on the early turns (T1-T2), where accelerating into
    // the combo matters most -- just under the key combo pieces; normal once past them; low once we
    // already have enough mana.
    auto ramp_rank = [&]() -> int
    {
        if (sources >= source_target) { return kRankDigPastLand; }   // enough mana already
        return (s.turn_number <= 2) ? kRankEarlyRamp : kRankRamp;    // short: high early, normal later
    };

    // --- the lynchpin / a needed land outrank everything else ---
    if (is_hinata) { return have_hinata ? kRankDuplicate : kRankHinataLynchpin; }
    if (is_land)
    {
        const bool enters_tapped = def && def->params.enters_tapped;
        {
            // Four land categories on (mana-screwed x can-play-now), user-specified (2026-07-20):
            //  1) mana-SCREWED + playable NOW  -> top keep (nothing matters until we have mana)
            //  2) mana-SCREWED + drop already spent / tapped -> keep for next turn, but below live pieces
            //  3) OKAY mana + playable now -> extra mana, helps but loses to the missing pieces
            //  4) OKAY mana + can't use now -> pretty useless, dig past it
            // "playable now" = a land drop is open AND the land enters untapped (a tapped Boilerworks
            // yields no mana the turn it is played).
            //
            // "mana-screwed" is COLOUR-AWARE (user 2026-07-20): the goal is casting Hinata ({1}{U}{R}{W}),
            // so we count a land toward the screw rating only if we cannot yet cast her AND this land
            // ADVANCES that goal -- i.e. it supplies a still-MISSING Hinata colour, OR raw count is the
            // binding constraint (we need more bodies than we are missing colours, so any land helps).
            // A land supplying only colours we already have, when we are merely one source short, does
            // NOT count (3 sources + no white -> a non-white land is useless for Hinata; at 2 sources it
            // does count, since even after the white we still need another body). A land of a missing
            // colour ALWAYS counts (only blue in play -> a red land is urgent).
            static const Color kHinataColors[3] = { Color::White, Color::Blue, Color::Red };
            bool have_col[3] = { false, false, false };
            auto note_colors = [&](const CardDefinition* d) {
                if (!d) { return; }
                for (Color col : d->params.produces)
                    for (int i = 0; i < 3; ++i) { if (col == kHinataColors[i]) { have_col[i] = true; } }
            };
            for (const Permanent& p : s.battlefield)
            {
                if (p.controller_index != active) { continue; }
                const CardDefinition* d = CardDatabase::Instance().LookupCached(p.card);
                if (d && (p.card.IsLand() || d->params.mana_rock)) { note_colors(d); }
            }
            for (const Card& h : ap.hand)
            {
                const CardDefinition* d = CardDatabase::Instance().LookupCached(h);
                if (d && (d->card.IsLand() || d->params.mana_rock || !d->params.produces.empty())) { note_colors(d); }
            }
            int  missing = 0;
            for (int i = 0; i < 3; ++i) { if (!have_col[i]) { ++missing; } }
            bool provides_missing = false;
            if (def) { for (Color col : def->params.produces)
                for (int i = 0; i < 3; ++i) { if (col == kHinataColors[i] && !have_col[i]) { provides_missing = true; } } }
            const int  kHinataMV       = 4;                                   // {1}{U}{R}{W}
            const bool can_cast_hinata = (total_sources >= kHinataMV) && (missing == 0);
            const bool count_binding   = (kHinataMV - total_sources) > missing; // need more bodies than colours
            const bool screwed         = !can_cast_hinata && (provides_missing || count_binding);
            const bool usable_now      = land_drop_open && !enters_tapped;
            if (screwed)  { return usable_now ? kRankScrewedLandNow : kRankScrewedLandLate; }      // (1) / (2)
            return (usable_now && sources < source_target) ? kRankExtraLand : kRankDigPastLand;    // (3) / (4)
        }
    }

    // Sol Ring is in a class of its own: two mana off a {1} rock, and every Reality Spasm untaps it for
    // two more (it compounds with the combo). Mana-short -> the single best card to see, above even a
    // screwed land; mana OK -> still beats everything except the actual combo pieces.
    if (is_solring)
    {
        return (sources < source_target) ? kRankSolRingScrewed : kRankSolRing;
    }

    // --- before Hinata: payoffs/rituals are DEAD; dig past them, keep ramp + cantrips ---
    if (!have_hinata)
    {
        if (is_rock || is_dork)     { return ramp_rank(); }
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
        // Reality Spasm is STACKABLE ramp (the lethal line untaps twice), not a redundant duplicate --
        // a 2nd+ copy stays a top-tier piece (above Irencrag / any idle land), not kRankDuplicate.
        const bool dup = have_in_hand([](const CardParams& p) { return p.untap_x_mana_sources; });
        return dup ? kRankExtraSpasm : kRankMissingSpasm;
    }
    if (is_soulfire)
    {
        // A 2nd+ Soulfire Eruption is nearly redundant -- you normally never cast multiples; only a
        // rare Reality-Spasm ramp chain affords two. So a duplicate drops PRETTY LOW (a hair above a
        // strict duplicate for that uncommon multi-cast), not the full finisher rank.
        const bool dup = have_in_hand([](const CardParams& p) { return p.damage_equals_top_mv; });
        return dup ? kRankExtraSoulfire : kRankSoulfire;
    }
    // Irencrag Feat: a single tier just above Soulfire -- a decent (if slightly worse, harder-to-cast)
    // Reality-Spasm stand-in regardless of mana level.
    if (is_ritual)   { return kRankIrencrag; }
    if (is_magma)    { return kRankMagma; }        // Magma Opus UNDER the cantrips (one-of secondary payoff)
    if (is_cantrip)  { return kRankCantrip; }
    if (is_rock || is_dork) { return ramp_rank(); }
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
    static const bool enabled = !EnvOn("MTG_NO_HINATA_HOLD_DORK");
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
    static const bool enabled = !EnvOn("MTG_NO_HINATA_HOLD_CRACKLE");
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

// Magma Opus scaled-cast (face-damage) variants (ScaledCastVariants) -- the DECK-SPECIFIC cost model.
//
// Magma Opus: "deal 4 damage divided among any number of targets. Tap two target permanents." Hinata
// reduces the cost {1} per DISTINCT target, so the cheapest cast SPREADS the 4 one-per-target across
// yourself + creatures and taps two permanents (up to 6 distinct targets -> {U}{R}); CONCENTRATING
// more of the 4 onto the opponent's FACE uses fewer distinct targets, so each extra point of face
// costs {1} more (face F costs generic 6 - discount(F), colours {U}{R} kept):
//   distinct(F) = 1 (face) + min(4-F, self+creatures) (spread) + min(2, permanents) (tap);
//   discount(F) = min(discount_max_targets, distinct(F)).
// We enumerate every affordable face level 1..damage and let the SEARCH pick -- the plan enumerator's
// CanPay drops the concentrate levels a mana-tight plan can't pay, and with a Crackle also in hand the
// search allocates spare mana across both (this is the archetype's opinionated set; the search decides
// which to scale). Dominated levels (a HIGHER face at the SAME cost) are dropped. The spread/tap damage
// itself is goldfish-inert (a passive opponent, and our own creatures survive 1), so only the face
// amount + the cost are load-bearing; resolution deals exactly `face` to the opponent (threaded on the
// stack). ADOPTED default-ON (2026-07-21); MTG_LEGACY_MAGMA restores the over-count. Also gated on
// Magma-by-DATA (divided damage that taps permanents; Reality Spasm is has_x and never reaches this
// fixed-cost path) + Hinata online (no Hinata -> no per-target discount -> nothing to scale). Returns
// {} otherwise -> the engine's normal single-line Magma cast (the "2 + every permanent" over-count),
// byte-identical to the pre-adoption ground truth.
std::vector<ScaledCastVariant>
HinataProvider::ScaledCastVariants(const GameState& s, const CardDefinition& def) const
{
    // ADOPTED default-ON (2026-07-21). MTG_LEGACY_MAGMA => {} => the over-count single line (4-to-face
    // at {U}{R} regardless -- an impossible cheap line), byte-identical to the pre-adoption GT. Root-cause
    // of the +0.02 adoption cost: a handful of games (~2%) where the honest price makes a Magma-dependent
    // kill land one turn later -- either genuine (the cheap-4-face fiction bought a turn that isn't there,
    // e.g. small overkill) or search variance (the honest cost reshapes EARLY lookahead decisions so the
    // game walks a different, slightly slower board). Never faster, never a bug. See
    // docs/design/viewer-magma-opus-modeling.md (2026-07-21 verdict).
    //
    // This hook owns only the MODEL (which face levels + what each costs). Extra-mana ALLOCATION across
    // competing scaling sinks (a Crackle {X} and this face) is handled by FillScaledCastFace (TurnSolver):
    // the search searches Crackle X (XCandidates), and the leftover mana fills Magma's face up from its
    // emitted minimum -- Crackle first, Magma the sub-chunk remainder. (There is no separate allocation
    // prune; an earlier comment referencing "SubsetMisallocatesScalingMana in TurnSolver" was describing a
    // function that was never written -- FillScaledCastFace already does the job.)
    static const bool legacy = EnvOn("MTG_LEGACY_MAGMA");
    if (legacy) { return {}; }
    if (!def.params.damage_divided || !def.params.discount_targets_permanents) { return {}; }
    if (!HinataInPlay(s)) { return {}; }

    const int dmg = def.params.damage;
    if (dmg <= 0) { return {}; }
    const int cap = def.params.discount_max_targets;   // Hinata reduction ceiling (Magma = 6)

    // Distinct 1-damage recipients OTHER than the opponent face, for the spread: yourself + every
    // creature (own first, opponent's as a last resort -- only the COUNT feeds the discount). Plus the
    // mandatory "tap two target permanents", up to two of any permanents on the board.
    int creatures = 0;
    for (const Permanent& p : s.battlefield) { if (p.card.IsCreature()) { ++creatures; } }
    const int spread_capacity = 1 + creatures;                                 // self + creatures
    const int tap = std::min(2, static_cast<int>(s.battlefield.size()));

    // Emit the FULL face ladder (high -> low, cost non-increasing): each face only when it is the HIGHEST
    // face at its cost (a lower face at the same cost is strictly dominated -- same mana, less reach). The
    // plan enumerator + search pick the face; the scaling-mana packing prune handles Crackle competition.
    std::vector<ScaledCastVariant> out;
    int prev_generic = -1;
    for (int face = dmg; face >= 1; --face)
    {
        const int spread   = std::min(dmg - face, spread_capacity);
        const int distinct = 1 + spread + tap;
        const int discount = std::min(cap, distinct);
        ManaCost cost      = def.card.m_mana_cost;                              // printed {6}{U}{R}
        cost.generic       = std::max(0, cost.generic - discount);
        if (cost.generic == prev_generic) { continue; }                        // dominated -> skip
        prev_generic = cost.generic;
        out.push_back({ face, cost });
    }
    return out;
}

// ---- DragonstormProvider ----------------------------------------------------

// ===================================================================================================
// Payoff / ETB-value ordering -- the shared "trigger ordering" primitive (kind 3 of the three
// within-turn ordering kinds; see docs/design/sequential-plan-evaluation.md). DECK-AGNOSTIC and
// param-driven, so a NEW deck gets correct payoff-ordering by handing its entering set here.
//
// A permanent with a "whenever another X enters" trigger wants to be on the battlefield BEFORE the
// entries it cares about, so it sees the most of them. The rule is value-SIGN-aware -- it only holds
// when the trigger HELPS you; a harmful on-other-ETB permanent flips it (enter LAST, minimise
// exposure). Bands (lower = earlier), a STABLE partition so the caller's intra-band tiebreak survives:
//
//   0  beneficial on-other-ETB PRODUCER      -- trigger creates permanents that themselves re-trigger
//                                               on-other-ETB sources, so placing it first MULTIPLIES
//                                               later triggers (Lathliss: each later Dragon makes a 5/5
//                                               token that re-pings Scourge). "Put the maker of
//                                               triggerers ahead of the things it triggers."
//   1  beneficial on-other-ETB non-producer  -- beneficial trigger, creates nothing that re-triggers
//                                               (Scourge: pure face ping).
//   2  neutral                               -- no on-other-ETB trigger; order-independent here.
//   3  harmful on-other-ETB                  -- trigger hurts you; trail so it sees the FEWEST entries.
//                                               (No such param in the pool yet -- EXTENSION POINT: add
//                                               the param + a sign check to ClassifyEtbOrder, NOT a new
//                                               mechanism.)
//
// The producer-vs-non-producer split (bands 0 vs 1) is a HEURISTIC default (Rule 0: no single correct
// order, only measurably-better) validated on Dragonstorm and the piece a future per-deck ordering-
// analysis step would MEASURE/override; "beneficial leads neutral" (0/1 vs 2) is the well-founded core.
namespace {
enum class EtbOrderBand { BeneficialProducer = 0, BeneficialOther = 1, Neutral = 2, Harmful = 3 };

EtbOrderBand ClassifyEtbOrder(const CardParams& p)
{
    if (p.etb_other_subtype_creates_tokens) { return EtbOrderBand::BeneficialProducer; } // Lathliss
    if (p.dragon_ping_on_enter)             { return EtbOrderBand::BeneficialOther; }     // Scourge
    // (Harmful on-other-ETB: no param yet -- add it here when a card needs it.)
    return EtbOrderBand::Neutral;
}
} // namespace

std::vector<std::string> OrderEntriesByEtbValue(std::vector<std::string> names)
{
    // Stable partition by band: an already-banded input (Dragonstorm's put list) is unchanged
    // (byte-identical); a set handed in any order comes back beneficial-on-other-ETB-first. The
    // comparator only orders by band, so std::stable_sort keeps every within-band (order-independent)
    // pick in the caller's order.
    std::stable_sort(names.begin(), names.end(),
        [](const std::string& a, const std::string& b)
        {
            const CardDefinition* da = CardDatabase::Instance().Lookup(a);
            const CardDefinition* db = CardDatabase::Instance().Lookup(b);
            const EtbOrderBand ba = da ? ClassifyEtbOrder(da->params) : EtbOrderBand::Neutral;
            const EtbOrderBand bb = db ? ClassifyEtbOrder(db->params) : EtbOrderBand::Neutral;
            return static_cast<int>(ba) < static_cast<int>(bb);
        });
    return names;
}

// Tutor-to-battlefield SELECTION + put-ORDER for Dragonstorm (user-shaped 2026-07-18; see
// docs/design/analysis-Dragonstorm.md "DRAGONSTORM tutor-to-battlefield SELECTION HEURISTIC" +
// "ORDER RULE" + the KILL PATTERN section). Given N = max_puts (storm total, already capped at the
// number of library Dragons by PerformTutorToBattlefield), pick WHICH Dragons to put and emit the
// ONE deterministic put-order.
//
// The Dragons and their engine roles (classified by params, so this is robust to renames except the
// Karrthus-vs-Kolaghan preference, which is a NAME rule the user stated explicitly):
//   * Lathliss, Dragon Queen  -- etb_other_subtype_creates_tokens : token engine (5/5 per later Dragon)
//   * Scourge of Valkas   (x3) -- dragon_ping_on_enter            : the pinger (X = Dragons you control)
//   * Utvara Hellkite     (x2) -- attack_per_matching_creates_tokens : combat-token amplifier
//   * Karrthus / Kolaghan (x1) -- grants_haste                     : the haste-Dragon (same-turn alpha
//     strike). Karrthus PREFERRED over Kolaghan (user).
//
// SELECTION (max_puts-aware SUBSET, done FIRST -- NOT a truncation of the order list) by three cases:
//   A. Ideal   (Lathliss present AND a haste-Dragon present): reserve the preferred haste-Dragon,
//      then Lathliss + a Scourge, then dump the extras (more Scourges, Utvara, other haste-Dragon).
//   B. Missing Lathliss (haste-Dragon present, no Lathliss): reserve the preferred haste-Dragon,
//      then Scourges, then Utvara(s).
//   C. No haste-Dragon (toughest -- ping is the wincon): Lathliss if present, then as many Scourges
//      as possible, then Utvara(s). No alpha strike this turn.
// The haste-Dragon is RESERVED FIRST in A/B so it is guaranteed in the set even when N is small (the
// user's hard "haste-Dragon MUST be in the chosen N-set" rule) -- a same-turn alpha strike is the
// dominant line at small N (at N=1 the hasted Dragon attacks now; at N=2 haste + Lathliss makes a
// hasted token). At the normal storm-3+ kill size every case's picks all fit, so the reservation
// only matters at the small-N edge; this is DISCLOSED as the interpretation of the "when N is small"
// clause. More bodies is never worse in a goldfish, so any leftover slots are filled greedily.
//
// ORDER (of the chosen subset, one deterministic put-order): Lathliss first (so its token fires off
// every LATER Dragon), then ALL Scourges (each pings every later entry), then every other Dragon in a
// fixed order (Utvara, then Karrthus before Kolaghan). Only Lathliss+Scourge are order-relevant; the
// rest are order-INDEPENDENT (identical outcome), so collapsing their permutations to one
// representative is LOSSLESS -- the search need not branch over orderings.
//
// MTG_UNPRUNED / MTG_UNPRUNE=tutor returns {} -> the heuristic is OFF and PerformTutorToBattlefield
// falls back to the full library-order enumeration (the committed engine behaviour), so the Stage-5
// audit can open the space.
std::vector<std::string>
DragonstormProvider::TutorToBattlefieldPutOrder(const GameState& s, int controller,
                                                const CardParams& pp, int max_puts) const
{
    if (max_puts <= 0) { return {}; }
    // Heuristic OFF only for the autonomous SEARCH audit (MTG_UNPRUNED opens the full library-order
    // enumeration so the Stage-5 tutor audit can branch the whole space). Human play ALSO sets
    // MTG_UNPRUNED, but there the fallback = raw library order, which drops whatever Dragons sit on top
    // of the library (e.g. 3 Scourge + 1 Utvara) instead of the rule pick (Lathliss + reserved haste-
    // Dragon). So the carve-out: in HUMAN play keep the rule ON as the default the viewer resolves to.
    if (DecisionUnpruned(UnprunedGate::Tutor) && !HumanPlayActive()) { return {}; }

    // --- Inventory: classify library Dragons (matching pp.tutor_types) by role + count copies. ---
    std::string L, S, U, K, G;                 // the actual card names per role (as found)
    int nL = 0, nS = 0, nU = 0, nK = 0, nG = 0; // library copy counts per role
    const Player& ap = s.players[controller];
    for (const Card& c : ap.library)
    {
        const CardDefinition* d = CardDatabase::Instance().LookupCached(c);
        if (!d) { continue; }
        const Card& card = d->card;
        bool type_ok = pp.tutor_types.empty();
        for (const std::string& t : pp.tutor_types)
        { if (CardMatchesTypeName(card, t)) { type_ok = true; break; } }
        if (!type_ok) { continue; }

        const CardParams& cp = d->params;
        const std::string nm = c.m_name;
        if      (cp.etb_other_subtype_creates_tokens)       { L = nm; ++nL; }   // Lathliss (token engine)
        else if (cp.dragon_ping_on_enter)                   { S = nm; ++nS; }   // Scourge (pinger)
        else if (cp.attack_per_matching_creates_tokens > 0) { U = nm; ++nU; }   // Utvara (attack tokens)
        else if (cp.grants_haste)                                                // haste-Dragon
        {
            if (nm.find("Karrthus") != std::string::npos) { K = nm; ++nK; }     // preferred
            else                                          { G = nm; ++nG; }     // Kolaghan / other
        }
        // Any other Dragon type is caught by the defensive fill below (more bodies never worse).
    }

    const bool has_haste    = (nK > 0 || nG > 0);
    const bool has_lathliss = (nL > 0);

    // --- SELECTION: choose per-role counts (sX <= inventory), total <= N, in priority order. ---
    int sL = 0, sS = 0, sU = 0, sK = 0, sG = 0;
    auto pick = [&](int& sel, int inv, int want)
    {
        int room = max_puts - (sL + sS + sU + sK + sG);
        int add  = want;
        if (add > inv - sel) { add = inv - sel; }
        if (add > room)      { add = room; }
        if (add > 0)         { sel += add; }
    };

    if (max_puts <= 2)                      // SMALL-N (1-2 Dragons) -- SELECTION priority (what you GET)
    {
        // Distinct from the play ORDER below (Lathliss/Scourge stay front there because they are
        // order-dependent). With only 1-2 Dragons lethal is usually out of reach and the Scourge ping
        // scales with Dragon count, so it is DEPRIORITIZED here; a haste-Dragon (same-turn attack) and
        // the token engines are worth more. Priority: haste -> Lathliss -> Utvara -> Scourge -> 2nd
        // haste (user rule). The defensive fill below still tops up from any leftover inventory.
        if (nK > 0) { pick(sK, nK, 1); } else { pick(sG, nG, 1); }  // haste 1 (Karrthus preferred)
        pick(sL, nL, 1);                    // Lathliss
        pick(sU, nU, 1);                    // Utvara
        pick(sS, nS, 1);                    // Scourge (low at small N -- weak ping)
        if (nK > 0) { pick(sG, nG, 1); } else { pick(sK, nK, 1); }  // haste 2 (the other haste-Dragon)
    }
    else if (has_haste && has_lathliss)     // Case A -- Ideal (>=3 Dragons: ping scales -> max Scourges)
    {
        if (nK > 0) { pick(sK, nK, 1); } else { pick(sG, nG, 1); }  // reserve the preferred haste-Dragon
        pick(sL, nL, 1);                    // Lathliss (token engine)
        pick(sS, nS, 1);                    // a Scourge
        pick(sS, nS, nS);                   // dump extras: more Scourges
        pick(sU, nU, nU);                   //             Utvara(s)
        pick(sK, nK, nK);                   //             remaining haste-Dragon(s)
        pick(sG, nG, nG);
    }
    else if (has_haste)                     // Case B -- Missing Lathliss
    {
        if (nK > 0) { pick(sK, nK, 1); } else { pick(sG, nG, 1); }  // reserve the preferred haste-Dragon
        pick(sS, nS, nS);                   // Scourge first
        pick(sU, nU, nU);                   // Utvara second
        pick(sK, nK, nK);                   // remaining haste-Dragon(s)
        pick(sG, nG, nG);
    }
    else                                    // Case C -- No haste-Dragon (ping is the wincon)
    {
        pick(sL, nL, 1);                    // Lathliss if available
        pick(sS, nS, nS);                   // as many Scourges as possible
        pick(sU, nU, nU);                   // then Utvara
    }
    // Defensive fill: any leftover inventory up to N (more bodies is never worse in a goldfish).
    pick(sL, nL, nL); pick(sS, nS, nS); pick(sU, nU, nU); pick(sK, nK, nK); pick(sG, nG, nG);

    // --- ORDER: emit the selected multiset, then route it through the shared payoff-ordering
    // primitive (OrderEntriesByEtbValue): beneficial on-other-ETB sources lead so they see the most
    // later entries -- Lathliss (token PRODUCER, band 0) first, then the Scourge ping (band 1), then
    // the order-independent bodies (Utvara, Karrthus, Kolaghan; band 2). The selection order below is
    // already banded, so the stable reorder is a NO-OP here (BYTE-IDENTICAL); the point of routing
    // through the helper is that it now OWNS the cross-band payoff rule, so a future mass-ETB deck
    // gets it by handing its set here in any order. The intra-band tiebreak (Utvara before the
    // haste-Dragons; Karrthus before Kolaghan) is a caller decision the stable sort preserves. ---
    std::vector<std::string> put;
    put.reserve(sL + sS + sU + sK + sG);
    for (int i = 0; i < sL; ++i) { put.push_back(L); }
    for (int i = 0; i < sS; ++i) { put.push_back(S); }
    for (int i = 0; i < sU; ++i) { put.push_back(U); }
    for (int i = 0; i < sK; ++i) { put.push_back(K); }
    for (int i = 0; i < sG; ++i) { put.push_back(G); }
    return OrderEntriesByEtbValue(std::move(put));
}

namespace
{
    // Cheap integer model of the Dragonstorm go-off's this-turn ETB burst, mirroring SpellEffects.h
    // OnDragonEnters EXACTLY: for each entering NONTOKEN Dragon, every OTHER Lathliss first makes a 5/5
    // Dragon token (which itself enters -> pings), then every Scourge in play deals `dragons` (the current
    // count) to the opponent. Pure integer recursion (no GameState copy, tiny N) -> safe on the rollout
    // hot path. Dragons already in play seed the counts (they don't re-enter but scale the ping X and
    // their Scourges/Lathliss fire for the new entrants).
    struct GoOffSim
    {
        long long dmg      = 0;
        int       dragons  = 0;   // Dragons currently controlled
        int       scourges = 0;   // Scourge of Valkas in play (pingers)
        int       lathliss = 0;   // Lathliss in play (token engines)
        void enter(bool is_scourge, bool is_lathliss, bool is_token)
        {
            ++dragons;
            if (!is_token) { if (is_scourge) { ++scourges; } if (is_lathliss) { ++lathliss; } }
            if (!is_token)                                  // STEP 1: other Lathliss each make a 5/5 token
            {
                const int makers = lathliss - (is_lathliss ? 1 : 0);
                for (int k = 0; k < makers; ++k) { enter(false, false, true); }
            }
            dmg += static_cast<long long>(scourges) * dragons;   // STEP 2: this entrant's Scourge pings
        }
    };
    // ADOPTED default ON: the go-off win-now model. MTG_NO_DRAGONSTORM_GOFF restores the pre-fix behavior
    // (byte-identical -- the engine skips building `casting` when HasExtraLethalModel is false) for A/B.
    // Measured (train seeds 4004/5005): d0 -0.33, d3/d5 -0.05..-0.10, no regressions, no other deck moves.
    const bool s_goff_lethal = !EnvOn("MTG_NO_DRAGONSTORM_GOFF");
}  // namespace

bool DragonstormProvider::HasExtraLethalModel() const
{
    return s_goff_lethal;
}

int DragonstormProvider::ExtraLethalDamage(const GameState& s,
        const std::vector<const CardDefinition*>& casting) const
{
    if (!s_goff_lethal) { return 0; }
    // Fire only for the storm go-off: a plan that casts Dragonstorm (tutor_to_battlefield). Apex's impulse
    // lethal is a separate cast decision and is left to execution (no storm card -> 0, byte-identical intent).
    const CardDefinition* storm = nullptr;
    for (const CardDefinition* c : casting)
    {
        if (c && c->params.tutor_to_battlefield) { storm = c; break; }
    }
    if (!storm) { return 0; }

    const int me = s.active_player_index;

    // Seed with the Dragons already in play (count + Scourge/Lathliss roles).
    GoOffSim sim;
    for (const Permanent& p : s.battlefield)
    {
        if (p.controller_index != me || !CardHasSubtype(p.card, "Dragon")) { continue; }
        ++sim.dragons;
        const CardDefinition* d = CardDatabase::Instance().LookupCached(p.card);
        if (d && d->params.dragon_ping_on_enter)             { ++sim.scourges; }
        if (d && d->params.etb_other_subtype_creates_tokens) { ++sim.lathliss; }
    }

    // Hard-cast Dragons in THIS plan resolve BEFORE Dragonstorm (creatures cast before the rank-20
    // payoff), so they are in play for the storm puts' pings -- enter them first.
    for (const CardDefinition* c : casting)
    {
        if (!c || c == storm || !CardHasSubtype(c->card, "Dragon")) { continue; }
        sim.enter(c->params.dragon_ping_on_enter, c->params.etb_other_subtype_creates_tokens, false);
    }

    // Storm count -> Dragons put. spells_cast_this_turn is ++'d per cast; this plan adds casting.size()
    // spells and Dragonstorm (cast last by CastOrderRank) puts that many, capped at the library inventory.
    // Reuse the picker for the EXACT ordered put-list execution will use (lockstep, ping-maximising order).
    const int projected_storm = s.spells_cast_this_turn + static_cast<int>(casting.size());
    const std::vector<std::string> put = TutorToBattlefieldPutOrder(s, me, storm->params, projected_storm);
    for (const std::string& nm : put)
    {
        const CardDefinition* d = CardDatabase::Instance().Lookup(nm);
        sim.enter(d && d->params.dragon_ping_on_enter, d && d->params.etb_other_subtype_creates_tokens, false);
    }

    return sim.dmg > 1000000 ? 1000000 : static_cast<int>(sim.dmg);   // clamp: never overflow the win-check
}

// CastOrderRank: no override. Dragonstorm's ritual (15), Ruby Medallion reducer (16) and Irencrag
// restrictor (18) tiers are now the GENERIC card-parameter tiers -- byte-identical ranks, one
// implementation, and any future ritual/reducer deck inherits them from its card data.

// ---- Goblins ---------------------------------------------------------------

// Defer the creature-sac outlets (Siege-Gang damage / Pashalik tokens / Skirk's sac-for-mana and its
// multi-sac burst) out of the pre-combat enumeration -- UNLESS a haste enabler makes the outlet's output
// able to attack THIS turn. Off-switch MTG_NO_GOBLIN_SAC_2ND (ADOPTED default-ON 2026-07-31).
// See DeferSacOutletPreCombat in DecisionProvider.h and analysis-goblins.md.
//
// The haste gate reaches TOKEN-CREATING value outlets too, not just the mana one (2026-08-02). It used
// to sit below an unconditional `if (!is_mana_outlet) return true;`, so a VALUE outlet never reached it
// -- and Pashalik Mons ({3}{R}, Sacrifice a Goblin: create two 1/1 Goblins) is exactly the case that
// breaks: under Goblin Chieftain/Warchief those tokens enter as 2/2s WITH HASTE, so the outlet is a
// pre-combat play worth +2 power and a free Pashalik death-trigger ping. Deferring it to the second main
// throws the whole turn away. Caught by goblins gi5 (seed 1006), which lost a T4 kill at d0/d3/d5 alike:
//   pre-combat sac Lackey -> ping 1 (9->8) + two hasty 2/2s -> attack for 9 -> exactly lethal
//   deferred              -> cast Aether Vial instead      -> attack for 7 -> opp 2, wins T5
// The original rationale ("without haste the float is second-main-recoverable") was always a statement
// about haste, not about mana; it just wasn't reachable for the other outlets.
//
// HONEST MEASUREMENT (goblins held-out overnight seeds, 16,000 games/arm, 2026-08-02). This is adopted on
// the DESIGN principle -- do not let a heuristic prune a line that can be lethal -- NOT on a measured win,
// because there isn't one:
//                          d3+d5 (8000g)      d0 (8000g)      overall
//   mode 1 (all outlets)      +0.0000          +0.0030         +0.0015   (4/4 d0 cases WORSE)
//   mode 2 (adopted)          +0.0000          +0.0004         +0.0002   (1 better / 2 worse / 9 equal)
// Two things to read off that table. First, at searched depth the deferral is win-turn-NEUTRAL over 8000
// held-out games even though the play digests all differ -- the search reaches the same outcome either
// way, which vindicates the deferral as a pure perf pruner and means gi5's recovery is a TRAIN-seed
// result (it was selected because it regressed; it is not evidence of generalisation). Second, mode 2
// exists because a DAMAGE outlet is timing-indifferent: narrowing to token-creating outlets removed ~87%
// of mode 1's d0 cost, confirming Siege-Gang's un-deferral was pure branch noise.
// If a future Goblins change needs to buy back d0, this is a known-cheap thing to re-examine.
bool GoblinsProvider::DeferSacOutletPreCombat(const GameState& s, const Permanent& src,
                                              bool is_mana_outlet) const
{
    static const bool on = !EnvOn("MTG_NO_GOBLIN_SAC_2ND");
    if (!on) { return false; }
    // Which outlet kinds reach the haste gate below (2026-08-02 A/B lever):
    //   0 = historical -- only the mana outlet; every value outlet always defers
    //   1 = every outlet kind
    //   2 = mana outlet + value outlets that CREATE CREATURE TOKENS (see below)
    static const int haste_mode = []{
        const char* e = std::getenv("MTG_GOBLIN_SAC_HASTE_MODE");
        return (e && *e) ? std::atoi(e) : 2;
    }();
    if (!is_mana_outlet && haste_mode != 1)
    {
        if (haste_mode == 0) { return true; }
        // Mode 2: only a TOKEN-CREATING value outlet is pre-combat-relevant. Pashalik's two 1/1 Goblins
        // enter as hasty 2/2s under Chieftain/Warchief and can attack this turn, so the timing is the
        // whole point. A damage outlet (Siege-Gang) is timing-INDIFFERENT -- 2 to the face is 2 to the
        // face in either main -- so un-deferring it only widens the pre-combat branch for nothing.
        const CardDefinition* sd = CardDatabase::Instance().LookupCached(src.card);
        if (!sd || sd->params.sac_outlet_creates_tokens <= 0) { return true; }
    }
    // Keep the outlet pre-combat if a Goblin haste lord is on the battlefield (src is a Goblin, and so
    // are Pashalik's tokens, so HasHasteFromLords answers this for both the source and its output) OR one
    // is castable from hand this turn ("our plan"). Otherwise the outlet buys nothing combat can use and
    // is the dominant pre-combat branch amplifier on a wide board -- defer it to the second main.
    const int active = s.active_player_index;
    if (HasHasteFromLords(src.card, s.battlefield, active)) { return false; }
    for (const Card& h : s.players[active].hand)
    {
        const CardDefinition* hd = CardDatabase::Instance().LookupCached(h);
        if (hd && hd->params.grants_haste) { return false; }
    }
    return true;   // no haste enabler in play or hand -> defer to the second main
}

// Mogg War Marshal echo keep-exception (off-switch MTG_NO_GOBLIN_ECHO -> historical always-decline). The
// base heuristic ALWAYS declines a self-replacing body (the death token nets the same 1/1 while saving the
// mana) -- but that token is summoning-SICK, so declining forfeits an attacker THIS turn. Keep (pay) when
// that attacker matters: (a) the current board is already attack-lethal counting this creature (declining
// could drop below lethal), or (b) there is no other castable spell this turn ("no gas"), so the banked
// mana buys nothing and a live attacker strictly beats a sick token. Otherwise decline (cast the gas).
bool GoblinsProvider::PayEchoToKeep(const GameState& s, const Permanent& p) const
{
    const CardDefinition* d = CardDatabase::Instance().LookupCached(p.card);
    if (!d) { return true; }
    const CardParams& ep = d->params;
    const bool self_token = ep.dies_watch_includes_self && ep.dies_trigger_creates_tokens > 0;
    if (!self_token) { return true; }                       // Stingscourger etc: default PAY
    static const bool on = !EnvOn("MTG_NO_GOBLIN_ECHO");
    if (!on) { return false; }                              // isolation: historical always-decline

    const int active   = s.active_player_index;
    const int opp_life = s.players[1 - active].life;
    // (a) Lethal now? Sum attack power over legal attackers (this creature is NOT summoning-sick -- echo
    // resolves the upkeep AFTER it entered). AttackPowerOf mirrors the combat sites (conservative: it omits
    // double-strike/exalted, absent from this deck), so atk <= true lethal -> we only keep on a real kill.
    int atk = 0;
    for (const Permanent& q : s.battlefield)
    {
        if (q.controller_index != active)                  { continue; }
        if (!CanAttackFull(q, s.battlefield, active))      { continue; }
        if (!ShouldAttackWith(s, q))                       { continue; }
        atk += AttackPowerOf(s, q);
    }
    if (atk >= opp_life) { return true; }                   // keeping the body wins this turn -> PAY

    // (b) No gas: any non-land hand card castable from the currently-available mana?
    const ManaPool pool = AvailableManaPool(s);
    for (const Card& h : s.players[active].hand)
    {
        const CardDefinition* hd = CardDatabase::Instance().LookupCached(h);
        if (!hd || hd->card.IsLand()) { continue; }
        if (pool.CanPay(h.m_mana_cost)) { return false; }   // a real alternative play exists -> DECLINE
    }
    return true;                                             // nothing else to cast -> keep the attacker (PAY)
}


// Goblin Matron fetch RANKING (value x deploy-discount, STATE-AWARE), FOLDED IN (default-on; disable for
// A/B via the unpruned Tutor gate = raw unranked list). Orders the full candidate set best-first so the
// narrow searched axis (TutorSearchWidth=4) searches the genuine THREATS, not shuffle-order chaff. value =
// board impact EvalCard misses (lords over board+hand Goblins, haste, cost-cut, Krenko/Muxus/Siege-Gang);
// discount = 0.55^turns-to-deploy over the best enabler path (mana + Skirk ramp, Vial charge-gated put,
// Lackey free-drop) + opportunity-cost downweight. cands[0] (the dedup's default pick) is thus the best.
// 6 shipped; 9 under the resolve axis -- see the width history note in the header. Byte-identical
// with the flag off.
int
GoblinsProvider::TutorSearchWidth() const
{
    // DIAGNOSTIC (MTG_GOBLIN_TUTOR_WIDTH=n, unset/0 = off): override THIS provider's width for the
    // width A/B. Distinct from the axis-level MTG_TUTOR_WIDTH, which trims only the search fan-out:
    // the value-reserve curation below reads this method too, so overriding here moves the reserve's
    // rescue slots WITH the window (a provider-W=6 arm rescues into ranks 4-5, not 7-8).
    static const int ovr = EnvInt("MTG_GOBLIN_TUTOR_WIDTH", 0);
    if (ovr > 0) { return ovr; }
    // Back to 6 (2026-08-05): the 9-under-resolve patch existed because the winning fetches of six
    // games ranked 7-9 -- all of them CLOSER reads the ranking was blind to. With the honest-swing
    // reads (lord buffs + attack pump) and the burst-closer in, every one of those games takes the
    // W=12 line at W=6, and the full W=6-vs-W=12 residual is noise classes only: budget churn
    // (gi553/gi352, recover at 4x), clairvoyance (gi124, edge dies under MTG_SHUFFLE_SALT_SEARCH
    // decoupling), and indirect width-leakage into NON-tutor decisions (gi714's T2 self-sac,
    // gi200's mulligan bottoming) -- with same-magnitude games in W=6's favor (gi483/gi624/gi299).
    // A width of 9 out of Matron's ~14 distinct names made the ranking nearly a no-op (user).
    return 6;
}

std::vector<std::string>
GoblinsProvider::TutorCandidates(const GameState& s, int controller, const CardParams& pp) const
{
    const bool unpruned = DecisionUnpruned(UnprunedGate::Tutor);
    // MTG_TUTOR_RANK_DUMP=1 prints this ranking and every input it derives from, ONCE PER TURN --
    // the first call of a turn is the search's root enumeration, i.e. the real pre-cast state (the
    // top-level resolution never gets here: it replays the search's chosen name, so PerformTutor
    // short-circuits and g_real_resolution would never fire). DIAGNOSTIC ONLY -- it never branches
    // play, and under the unpruned gate it still returns the unranked list. This is the instrument
    // that showed the lethal-reach clause missing gi865's Twinshot Sniper kill.
    // Deduped by the ranking's own INPUTS (turn/board/mana), each distinct situation printed once,
    // capped. A per-turn latch does not work: the search simulates future turns, so "the first T3
    // call" is a hypothetical inside T1's lookahead, not the real T3 root -- match the real state by
    // its printed inputs instead. MTG_TUTOR_RANK_DUMP_MAX caps the distinct situations (default 400).
    static const bool s_dump_on = EnvOn("MTG_TUTOR_RANK_DUMP");   // cached: this is a hot path
    bool dump = s_dump_on;
    if (unpruned && !dump) { return GenericProvider::TutorCandidates(s, controller, pp); }
    std::vector<std::string> cands = GenericProvider::TutorCandidates(s, controller, pp);
    if (cands.size() <= 1) { return cands; }
    // --- board scan (done once) ---------------------------------------------------------------
    int  goblins_controlled = 0;   // my Goblin creatures (lord/Krenko/Piledriver scale with this)
    int  goblin_fodder      = 0;   // ... that are actually EXPENDABLE to a sac outlet (no lords)
    int  goblins_sick       = 0;   // ... that cannot attack yet (the ones a haste grant unlocks NOW)
    int  ready_atk          = 0;   // total power that can already swing at the opponent THIS turn (lethal reach)
    bool lackey_now = false;       // a Goblin Lackey that can attack THIS turn -> free drop this turn
    bool lackey_persist = false;   // any Goblin Lackey on board -> free drop next turn
    bool skirk_on   = false;       // a Skirk Prospector (sac Goblin -> {R}) -> a mana RAMP for bombs
    bool haste_source = false;     // a Goblin lord granting haste -> a fetched body can attack NOW
    bool deathwatch_on = false;    // Rundvelt Hordemaster: every Goblin death impulse-digs a card
    int  vial_charge = -1;         // best untapped Aether Vial's charge (-1 = no Vial); puts a creature of MV==charge
    int  sick_goblin_power  = 0;   // power still locked up by summoning sickness (a haste grant frees it)
    // HONEST BOARD POWER (MTG_GOBLIN_BOARD_LORD_POWER=0 restores the printed-power scan): count each
    // body at its combat-site power -- EffectivePower (counters/temp) + on-board lord buffs -- exactly
    // what the attack will deal. The printed-power read halves a lord-heavy board: gi496 (overnight
    // d3/d5 s6006) had Hordemaster+Chieftain+fresh Matron read ready_atk=4 where the real swing was 8,
    // so face_burst's this-turn-lethal test (8 swing + 2 burst >= 10 life) never fired and the
    // T5-closing Twinshot Sniper sat at rank 7 -- one outside W=6, invisible to the window.
    static const bool s_board_lord_power = EnvOn("MTG_GOBLIN_BOARD_LORD_POWER", true);
    auto board_power = [&](const Permanent& p, const Card& pc) -> int
    {
        if (!s_board_lord_power) { return std::max(0, pc.m_power.value_or(0)); }
        const int lp = ComputeLordBonus(pc, s.battlefield, controller, p.is_animated, &p).first;
        return std::max(0, p.EffectivePower() + lp);
    };
    // ... and the ATTACK-PUMP term of the same read (MTG_GOBLIN_BOARD_PUMP_POWER=0 restores):
    // Goblin Piledriver's +2 per other attacking Goblin is combat-time temp power, invisible to
    // both printed power and EffectivePower at the main phase. gi828 (overnight d3/d5 s4004): the
    // real T5 swing was 10 (Piledriver + 2 others) but the scan read 5, so face_burst's
    // this-turn-lethal test (10 swing + 2 burst >= 12 life) never fired and the T5-closing
    // Twinshot Sniper stayed outside the W=6 window. In a goldfish every ready body attacks, so
    // each ready pump body sees (ready Goblin attackers - 1) others. Summed after the scan below.
    int ready_goblin_attackers = 0;   // ready GOBLIN bodies (the pump's crowd)
    int ready_pump_per         = 0;   // summed per-other-attacker pump across ready pump bodies
    for (const Permanent& p : s.battlefield)
    {
        if (p.controller_index != controller) { continue; }
        const CardDefinition* d = CardDatabase::Instance().LookupCached(p.card);
        const Card& pc = d ? d->card : p.card;
        if (pc.IsCreature() && CardHasSubtype(pc, "Goblin"))
        {
            ++goblins_controlled;
            if (!CanAttackFull(p, s.battlefield, controller))
            { ++goblins_sick; sick_goblin_power += board_power(p, pc); }
            // Sacrificeable BODIES, which is not the same set as "Goblins I control". A lord or a
            // scaling payoff is fodder only in extremis -- feeding a Chieftain to Skirk de-buffs
            // everything else on the board -- so it does not count toward the ramp. Same
            // expendability ordering CanonicalSacVictim uses when it actually picks a victim.
            const bool scaling = d && ((!d->params.subtypes_affected.empty()
                                        && (d->params.power_bonus > 0 || d->params.tough_bonus > 0
                                            || !d->params.reduces_spell_subtype.empty()))
                                       || d->params.attack_pump_power_per_other_matching > 0);
            if (!scaling) { ++goblin_fodder; }
        }
        if (pc.IsCreature() && CanAttackFull(p, s.battlefield, controller))
        {
            ready_atk += board_power(p, pc);   // any ready attacker (goldfish: connects)
            if (CardHasSubtype(pc, "Goblin"))
            {
                ++ready_goblin_attackers;
                if (d && d->params.attack_pump_power_per_other_matching > 0)
                { ready_pump_per += d->params.attack_pump_power_per_other_matching; }
            }
        }
        if (!d) { continue; }
        if (!d->params.combat_damage_puts_subtype_from_hand.empty())      // Goblin Lackey
        {
            lackey_persist = true;
            if (CanAttackFull(p, s.battlefield, controller)) { lackey_now = true; }
        }
        if (d->params.sac_creature_outlet && !d->params.sac_outlet_add_mana_color.empty()) { skirk_on = true; } // Skirk
        // A Goblin lord granting haste (Warchief / Chieftain): whatever we fetch can attack the
        // turn it lands, which is the whole difference for an attack-triggered payoff.
        if (d->params.grants_haste && !d->params.subtypes_affected.empty()) { haste_source = true; }
        if (d->params.dies_trigger_impulse_exile) { deathwatch_on = true; }
        if (d->params.upkeep_adds_charge && !p.tapped) { vial_charge = std::max(vial_charge, p.charge_counters); } // Vial
    }
    static const bool s_board_pump = EnvOn("MTG_GOBLIN_BOARD_PUMP_POWER", true);
    if (s_board_pump && ready_pump_per > 0 && ready_goblin_attackers >= 2)
    { ready_atk += ready_pump_per * (ready_goblin_attackers - 1); }   // each pump body: per x others
    const int G         = goblins_controlled;
    // Goblins waiting in hand are near-future lord-buff targets: a +1/+1 lord makes EACH of them hit
    // harder the moment it lands, so a lord's team-buff must be credited over board + hand, not board
    // alone (this is why a lord out-values a same-power vanilla body -- e.g. Goblin King vs Chainwhirler
    // with a Matron down: the King's +1 on Matron already matches the body, and every Goblin added widens
    // the gap). Capped so a flooded hand can't unboundedly balloon the term.
    int goblins_in_hand = 0;
    for (const Card& h : s.players[controller].hand)
    {
        const CardDefinition* hd = CardDatabase::Instance().LookupCached(h);
        const Card& hc = hd ? hd->card : h;
        if (hc.IsCreature() && CardHasSubtype(hc, "Goblin")) { ++goblins_in_hand; }
    }
    // ---- PLAN-AWARE INPUTS (MTG_GOBLIN_PLAN_AWARE=1, default off) -------------------------------
    // Five of the inputs below are TUNED GUESSES at what the rest of this turn will do, and the plan
    // being enumerated already knows all five exactly (see PlanContext.h):
    //
    //   buff_targets     G + min(goblins_in_hand, 3)      -> G + Goblins the plan actually casts
    //   entering_fodder  1 if a Goblin tutor is in hand   -> the Goblins the plan actually casts
    //   haste_avail      "a haste lord we could afford"   -> is one actually cast this turn?
    //   hand_has_play    "hand holds a deployable body"   -> does the plan actually deploy one?
    //   mana_next        lands + 1                        -> +1 more if the plan still plays its land
    //
    // Deliberately moved TOGETHER. The three earlier attempts to correct this model each made ONE
    // input honest while the others stayed calibrated to the old wrong value, and all three lost
    // (+20, +9, +18 held-out). If that diagnosis is right, the cluster is only coherent when it moves
    // as a unit; if this loses too, the diagnosis was wrong and the whole line is dead.
    // MODE MATTERS, and mode 1 was WRONG (user, 2026-08-05: "is the problem not our handling of this
    // new setup within the heuristic itself?"). The plan is exact about THIS TURN; several of these
    // terms are deliberately about the FUTURE, so substituting one for the other silently narrows
    // them rather than making them honest:
    //
    //   buff_targets is documented "board + NEAR-FUTURE (hand) buff recipients" -- a lord's +1/+1
    //   persists, so a Goblin still in hand two turns out is a real recipient. Mode 1 replaced that
    //   with "Goblins the plan casts this turn", dropping every hand Goblin the plan does not cast.
    //   That UNDERCOUNTS lords, which pushes the ranking toward bombs -- precisely the failure mode
    //   rounds 12-15 kept measuring. haste_avail had the same flaw: mode 1 gated off the in-hand
    //   fallback, so a haste lord cast NEXT turn stopped counting even though it is on the
    //   battlefield when the fetched card lands.
    //
    //   1 = replace (measured +2.0 held-out; kept for the A/B, but the narrowing above is a bug)
    //   2 = UNION: plan-exact for this turn, the old approximation for everything after it
    static const int plan_aware = EnvInt("MTG_GOBLIN_PLAN_AWARE", 0);
    bool plan_known = false, plan_haste_cast = false, plan_deploys = false, plan_land_pending = false;
    int  plan_goblins_entering = 0;
    if (plan_aware)
    {
        const PlanContext* pc = CurrentPlanContext();
        if (pc != nullptr && pc->actions != nullptr)
        {
            plan_known = true;
            plan_land_pending = pc->land != nullptr && !pc->land->empty() && !pc->land_done;
            auto count_cast = [&](const std::string& nm, bool is_source)
            {
                const CardDefinition* ad = CardDatabase::Instance().Lookup(nm);
                if (ad == nullptr || !ad->card.IsCreature()) { return; }
                if (!is_source) { plan_deploys = true; }
                if (CardHasSubtype(ad->card, "Goblin")) { ++plan_goblins_entering; }
                if (ad->params.grants_haste && !ad->params.subtypes_affected.empty())
                { plan_haste_cast = true; }
            };
            // The action being decided is the tutor SOURCE, and it is entering too -- that is the
            // body the old entering_fodder was standing in for.
            if (pc->index < pc->actions->size())
            { count_cast((*pc->actions)[pc->index].card_name, /*is_source=*/true); }
            const auto rest = PlanContextRest(pc);
            for (const Action* a = rest.first; a != rest.second; ++a)
            {
                if (a->kind != Action::Kind::CastFromHand && a->kind != Action::Kind::ActivateVial)
                { continue; }
                count_cast(a->card_name, /*is_source=*/false);
            }
        }
    }
    // Mode 2: the plan's bodies are certain, and the hand Goblins it does NOT cast are still the
    // near-future recipients the term was always about -- so ADD them, do not discard them.
    const int hand_left = std::max(0, goblins_in_hand - plan_goblins_entering);
    const int buff_targets =
        !plan_known                 ? G + std::min(goblins_in_hand, 3)
      : plan_aware >= 2             ? G + plan_goblins_entering + std::min(hand_left, 3)
                                    : G + plan_goblins_entering;
    // Skirk ramp: each OTHER Goblin sacs for {R}, so a bomb is reachable ~this turn if Skirk + fodder pay for it.
    //
    // TWO corrections (user, 2026-08-04), both about counting the bodies that will ACTUALLY be
    // available to sacrifice, rather than the board exactly as it stands right now:
    //
    // 1. COUNT THE ENTERING TUTOR SOURCE. Goblin Matron is itself a Goblin creature and it is
    //    entering right now -- that is the whole reason this function is running -- so it is fodder
    //    for the very turn this ramp predicts. The board scan runs while the source is still in
    //    hand, so it was missed. 606a381 made exactly this correction on the ATTACK side (the swing
    //    projection counts the source as entering); the MANA side never got it. Detected the same
    //    way, by finding the tutor source still in hand, so at real ETB resolution -- when the source
    //    is already on the battlefield and so already in the scan -- it is not double-counted.
    //
    // 2. LORDS ARE NOT FODDER (goblin_fodder, see the board scan). "In most cases you probably would
    //    not use them, but maybe in a rare case it could happen" -- so they are excluded from the
    //    ramp while staying in G for every other purpose. This is only about what Skirk can eat.
    //
    // gi206 is the case: at the T3 Matron, mana_next reads 4 against a Muxus at MV 6, so the deploy
    // discount prices the deck's bomb three turns out and buries it -- while the line that wins a
    // turn earlier hard-casts Muxus next turn off precisely these sacs.
    // The two corrections are SEPARABLE and were measured separately -- 0 = off, 1 = both (ADOPTED),
    // 2 = entering source only, 3 = lord-exclusion only. Bundling them unmeasured would have repeated
    // this session's sharpest mistake, where a bundle's aggregate hid a component that was actively
    // harmful on the tier that matters. Held-out overnight, 8,000 searched / 12,000 d0 games:
    //
    //   entering source only    searched -2.0   d0 +10.0
    //   lords-not-fodder only   searched  0.0   d0  -2.0
    //   both (adopted)          searched -2.0   d0  +1.0
    //
    // The entering-source correction carries the searched gain; the lord exclusion is searched-neutral
    // and cancels its d0 cost, so they complement. The aggregate is inside the noise band -- these are
    // adopted for MODEL CORRECTNESS (both counts were simply wrong about which bodies exist and which
    // can be eaten) on a measured non-regression, not on the turn-units. The sharper evidence is the
    // ranking diagnostic: gi206's Muxus climbs rank 13 -> 7, and the worst miss across 100 sampled
    // decisions improves from 13 to 11 (test/goblins_tutor_truth.py).
    static const int  skirk_fodder = EnvInt("MTG_GOBLIN_SKIRK_FODDER", 1);
    const bool use_entering = (skirk_fodder == 1 || skirk_fodder == 2);
    const bool use_nolord   = (skirk_fodder == 1 || skirk_fodder == 3);
    int entering_fodder = 0;
    if (plan_known)
    {
        // Exact: every Goblin body the plan puts onto the battlefield this turn, source included.
        entering_fodder = plan_goblins_entering;
    }
    else if (use_entering)
    {
        for (const Card& h : s.players[controller].hand)
        {
            const CardDefinition* hd = CardDatabase::Instance().LookupCached(h);
            if (!hd || !(hd->params.tutor_to_hand || hd->params.tutor_to_top)) { continue; }
            if (hd->card.IsCreature() && CardHasSubtype(hd->card, "Goblin")) { entering_fodder = 1; }
            break;
        }
    }
    // The "-1" contradicts the engine. "Sacrifice a Goblin: Add {R}" is repeatable and needs no tap,
    // and Skirk Prospector is itself a Goblin, so the LAST activation can eat Skirk: N bodies convert
    // to N mana, not N-1. CollectActions' own multi-sac burst already models it that way -- its victim
    // count V counts every Goblin creature including the source, and k is capped at V -- so the ranking
    // was predicting one less mana than the solver will actually find. gi206 is exactly that mana: at
    // the T3 Matron, N-1 puts mana_next at 5 against a Muxus at MV 6 (so the discount prices the bomb
    // two turns out), while N puts it at 6 -- castable next turn, which is the line that wins a turn
    // earlier. MTG_GOBLIN_SKIRK_SELFSAC=0 restores the old count for the A/B.
    static const bool selfsac = EnvOn("MTG_GOBLIN_SKIRK_SELFSAC", true);
    const int skirk_ramp = !skirk_on ? 0
                         : std::max(0, (use_nolord ? goblin_fodder : G) - (selfsac ? 0 : 1)
                                       + entering_fodder);
    const int untapped_mana = AvailableManaPool(s).Total();               // real mana this turn (no Skirk fudge)
    const int mana_now  = untapped_mana + skirk_ramp;
    // mana_next assumed THIS turn's land drop was already spent. It routinely is NOT: the plan
    // enumerator ranks a tutor at the pre-land-drop state, so a turn where the land is still to come
    // reads one mana short next turn, and a 5-drop prices as t=2 ("genuinely stuck", disc 0.287)
    // instead of t=1 (disc 0.637). Same class of bug as the Skirk self-sac miscount above, and the
    // same symptom -- gi101 is the case: at the T4 Matron the ranking is computed twice, once with
    // 3 lands (mana_next=4, Siege-Gang rank 8, OUTSIDE a 6-wide window) and once with 4 (mana_next=5,
    // rank 4, inside). The 3-land copy is the one the enumerator binds on, so a bomb that IS castable
    // next turn gets vetoed off the axis. Gated diagnostics MTG_GOBLIN_RESERVE_TURN /
    // MTG_GOBLIN_RESERVE_NEXT pinned it to exactly that state (turn 4, mana_next 4).
    // REJECTED, kept default-off with its number. Correcting the projection is measurably WORSE:
    //
    //                                        gi101   HELD-OUT (8000 searched)
    //   reserve=2, PENDING_LAND=0 (shipped)   T5        0.0  (baseline)
    //   reserve=0, PENDING_LAND=0             T6        0.0
    //   reserve=0, PENDING_LAND=1             T5       +7.0  (0 better / 7 worse)
    //   reserve=2, PENDING_LAND=1             T5       +9.0  (0 better / 9 worse)
    //
    // It fixes gi101 on its own -- the diagnosis is right -- but never helps anywhere else and costs
    // 7-9 games. The likely reason: the discount CURVE (0.85 at t=1, then 0.45/step) was calibrated
    // against this pessimistic mana_next, so the bias is already priced in. Crediting the pending
    // drop moves EVERY expensive bomb from t=2 to t=1 at every pre-land-drop state at once, which
    // re-tunes all of the thresholds the curve was fitted to. Making this pay would mean refitting
    // the curve with it, not dropping it in. Same lesson as the reserve eviction below.
    // MTG_GOBLIN_PENDING_LAND=1 enables.
    static const bool pending_land = EnvOn("MTG_GOBLIN_PENDING_LAND", false);
    bool land_drop_pending = plan_known && plan_land_pending;   // exact: does THIS plan still play one
    if (!plan_known && pending_land
        && s.players[controller].lands_played_this_turn < s.players[controller].LandDropsAvailable())
    {
        for (const Card& h : s.players[controller].hand)
        {
            const CardDefinition* hd = CardDatabase::Instance().LookupCached(h);
            if (hd != nullptr && hd->card.IsLand()) { land_drop_pending = true; break; }
        }
    }
    const int mana_next = CountLandsInPlay(s, controller) + 1 + (land_drop_pending ? 1 : 0) + skirk_ramp;
    const int opp_life  = s.players[1 - controller].life;
    // Can a fetched body attack the turn it lands? A Goblin lord granting haste already on the
    // battlefield, or one in hand we can afford now, is the difference between an attack-triggered
    // payoff paying immediately and losing a full swing.
    // Haste only matters for the turn the fetched body LANDS -- after that it is unsick anyway --
    // so a hand haste-lord counts only if it is castable ALONGSIDE the tutor source out of real
    // untapped mana, which is the same conservative rule the swing_atk projection below uses. (The
    // loose "MV <= mana_now" test was wrong: it credited a Chieftain at MV 3 against mana_now 3
    // while the Matron being cast is already spending exactly that mana.)
    int tutor_src_mv = 0;
    for (const Card& h : s.players[controller].hand)
    {
        const CardDefinition* hd = CardDatabase::Instance().LookupCached(h);
        if (!hd || !(hd->params.tutor_to_hand || hd->params.tutor_to_top)) { continue; }
        tutor_src_mv = hd->card.m_mana_cost.ManaValue();
        break;
    }
    bool haste_avail = haste_source || (plan_known && plan_haste_cast);
    // Mode 2 keeps the in-hand fallback: a haste lord we can still afford will be on the battlefield
    // when a fetched card lands, whether or not THIS turn's plan is the one that casts it.
    if (!haste_avail && (!plan_known || plan_aware >= 2))
    {
        for (const Card& h : s.players[controller].hand)
        {
            const CardDefinition* hd = CardDatabase::Instance().LookupCached(h);
            if (!hd || !hd->params.grants_haste || hd->params.subtypes_affected.empty()) { continue; }
            if (hd->card.m_mana_cost.ManaValue() + tutor_src_mv <= untapped_mana)
            { haste_avail = true; break; }
        }
    }
    // How many more attack steps we expect to need. This is the SAME quantity as "how close to
    // lethal are we", read the other way round, and it is what decides whether a delayed
    // attack-trigger payoff is worth anything at all (see the Piledriver note in value_of).
    // Sick bodies are included: they wake up before a fetched creature could swing anyway.
    const int per_swing   = std::max(1, ready_atk + sick_goblin_power);
    const int swings_left = std::max(1, std::min(6, (opp_life + per_swing - 1) / per_swing));

    // --- STRICT DOMINANCE: drop candidates that are "similar but worse" -----------------------
    // (user, 2026-08-05) "keep cards that are significantly different in utility and drop ones that
    // are similar, but worse in a situation. With W=6, this should be pretty accurate." The tutor
    // fetches ONE card and the window holds six, so two cards that do the same job only differently
    // -badly are burning a slot that measurably matters: held-out, W=5 costs +11.0 turn-units against
    // W=6 and W=4 costs +26.0, so the marginal slot is worth ~11 turn-units.
    //
    // This is the PROVABLE half of that idea -- no heuristics, no card names. B is dropped when some
    // other fetchable A costs no more, has a body no smaller, and is at least as good on EVERY
    // goldfish-relevant capability while being strictly better on one. Goblin King is the case in
    // this deck: identical {1}{R}{R} and 2/2 and +1/+1-to-Goblins as Goblin Chieftain, whose only
    // difference is that it ALSO grants haste and has haste itself. King's one differentiator is
    // mountainwalk, which cards.json itself flags "INERT in goldfishing (evasion vs a passive
    // non-blocking opponent)". And King is a 2-of that ranks 1st or 2nd in nearly every dump, so the
    // pair burns two of six slots to do one card's job.
    //
    // Capabilities are compared as a vector so the rule survives a decklist change; a card with any
    // capability the other lacks is "significantly different" and both survive.
    // MTG_GOBLIN_DOMINANCE=0 disables it for the A/B.
    static const bool dominance_on = EnvOn("MTG_GOBLIN_DOMINANCE", true);
    auto caps_of = [](const CardParams& q)
    {
        return std::array<int, 14>{
            q.power_bonus, q.tough_bonus, q.grants_haste ? 1 : 0,
            q.reduces_spell_subtype.empty() ? 0 : 1,
            q.attack_pump_power_per_other_matching, q.etb_reveal_count, q.etb_self_creates_tokens,
            std::max({ q.etb_damage_any, q.etb_damage_each_opponent, q.channel_damage }),
            q.sac_outlet_add_mana_amount, q.sac_outlet_damage,
            q.combat_damage_puts_subtype_from_hand.empty() ? 0 : 1,
            q.tap_creates_tokens_per_controlled_subtype.empty() ? 0 : 1,
            (q.dies_trigger_impulse_exile || q.dies_trigger_damage
             || q.dies_trigger_creates_tokens) ? 1 : 0,
            (q.tutor_to_hand || q.tutor_to_top) ? 1 : 0 };
    };
    std::set<std::string> dominated_out;
    if (dominance_on)
    {
        auto lookup = [&](const std::string& n) -> const CardDefinition*
        {
            for (const Card& lc : s.players[controller].library)
            { if (lc.m_name == n) { return CardDatabase::Instance().LookupCached(lc); } }
            return nullptr;
        };
        for (const std::string& bn : cands)
        {
            const CardDefinition* bd = lookup(bn);
            if (!bd) { continue; }
            for (const std::string& an : cands)
            {
                if (an == bn) { continue; }
                const CardDefinition* ad = lookup(an);
                if (!ad) { continue; }
                if (ad->card.m_mana_cost.ManaValue() > bd->card.m_mana_cost.ManaValue()) { continue; }
                if (ad->card.m_power.value_or(0)     < bd->card.m_power.value_or(0))     { continue; }
                if (ad->card.m_toughness.value_or(0) < bd->card.m_toughness.value_or(0)) { continue; }
                const auto ca = caps_of(ad->params), cb = caps_of(bd->params);
                bool ge = true, gt = false;
                for (std::size_t i = 0; i < ca.size(); ++i)
                {
                    if (ca[i] < cb[i]) { ge = false; break; }
                    if (ca[i] > cb[i]) { gt = true; }
                }
                // A tie on every axis would drop one of two identical names arbitrarily; require a
                // strict edge somewhere, so genuine duplicates both stay.
                if (ge && gt) { dominated_out.insert(bn); break; }
            }
        }
        if (!dominated_out.empty() && dominated_out.size() < cands.size())
        {
            cands.erase(std::remove_if(cands.begin(), cands.end(),
                        [&](const std::string& n) { return dominated_out.count(n) > 0; }),
                        cands.end());
        }
    }

    // --- per-card value (board impact, incl. payoffs EvalCard misses) --------------------------
    constexpr double BODY = 100.0;   // per power (a damage-equivalent, matching EvalCard's DMG)
    // Best face damage still FETCHABLE, for the dominated-burn rule in value_of below. A tutor takes
    // exactly one card, so a burn payoff only earns its credit if nothing strictly better is sitting
    // in the same library. MTG_GOBLIN_DOMINATED_BURN: 0 = off, 1 = drop the redundant face credit,
    // 2 = drop the card out of contention entirely (rank it last).
    static const int dom_burn_mode = EnvInt("MTG_GOBLIN_DOMINATED_BURN", 0);
    auto face_of = [](const CardParams& q) {
        return std::max({ q.etb_damage_any, q.etb_damage_each_opponent, q.channel_damage });
    };
    int pool_best_face = 0;
    if (dom_burn_mode > 0)
    {
        for (const std::string& n : cands)
        {
            for (const Card& lc : s.players[controller].library)
            {
                if (lc.m_name != n) { continue; }
                const CardDefinition* ld = CardDatabase::Instance().LookupCached(lc);
                if (ld) { pool_best_face = std::max(pool_best_face, face_of(ld->params)); }
                break;
            }
        }
    }
    // True when this card's burn is strictly beaten by another fetchable candidate's.
    auto is_dominated_burn = [&](const CardDefinition* d) -> bool
    {
        if (dom_burn_mode == 0 || !d) { return false; }
        const int f = face_of(d->params);
        return f > 0 && f < pool_best_face;
    };
    auto value_of = [&](const CardDefinition* d, const Card& c) -> double
    {
        if (!d) { return 0.0; }
        const CardParams& p = d->params;
        double v = c.m_power.value_or(0) * BODY;
        // Lord / team-buff: the buff THIS card grants my existing Goblins (EvalCard scores only the card).
        if (!p.subtypes_affected.empty())
        {
            if (p.power_bonus > 0)              { v += p.power_bonus * buff_targets * BODY; } // King/Chieftain/Rundvelt +1/+1: a buffed Goblin's +1 power is worth a body power point AND recurs+scales
            if (p.grants_haste)                 { v += goblins_sick * 90.0; }        // Warchief/Chieftain: sick attack NOW
            if (!p.reduces_spell_subtype.empty()) { v += 70.0; }                     // Warchief: cost cut -> deploy more
            // GOBLIN PILEDRIVER IS A LORD-EQUIVALENT, and CONDITIONALLY so (user, 2026-08-04):
            // "similar to a lord except it does 2 per other goblin and only realized when it
            // attacks ... 2 per other attacking goblin plus the 1 base power, whereas the lord does
            // 1 per other attacking goblin + 1 base power ... pretty close to Rundvelt Hordemaster
            // unless the lord effect can give lethal this turn. Piledriver usually wins if there is
            // haste or there are multiple turns it can attack." And, decisively: "it's important
            // that the Piledriver is not strictly better ... If close to lethal and no haste the
            // lord is better."
            //
            // As swing damage added, with N = other ATTACKING Goblins:
            //       +1/+1 lord   N*1 + 1        Piledriver   N*2 + 1
            // The lord side is already priced exactly that way (power_bonus * buff_targets * BODY,
            // plus its own body). Piledriver was priced as G * 2 * 45 -- a DIFFERENT crowd
            // (board-only, read at the instant of the fetch, when the board is smallest) and UNDER
            // HALF the per-point rate. At buff_targets 4 that is 190 against the lord's 500, where
            // the true ratio is 9/5. The two halves of the comparison were never on one scale.
            //
            // That also explains the earlier negative result recorded here: the crowd was swept at
            // the old 45/point (buff_targets -1.0, G+entering+1 +4.0, buff_targets@25 +2.0 held-out)
            // and d0 was EXACTLY 0.0 in every arm -- across 12,000 greedy games Piledriver never
            // once changed the top pick, because 190 -> 460 still lost to every lord. The COUNT was
            // never the axis; the SCALE was, and nothing tested moved it far enough to matter.
            //
            // The conditionality is not a tuned constant -- it falls out of how many swings are
            // left. Over T remaining attack steps a lord realizes T*(N+1) while Piledriver, which
            // cannot attack the turn it lands, realizes only (T-1)*(2N+1). So the pump is scaled by
            // (T-1)/T, and every one of the user's conditions drops out of that single factor:
            //     T = 1 (lethal this turn, no haste) -> 0.00  Piledriver contributes NOTHING,
            //                                                 the lord is strictly better
            //     T = 2                              -> 0.50  2N * 0.5 = N: parity with the lord
            //     T = 3+                             -> 0.67+ Piledriver ahead ("multiple turns")
            //     haste                              -> 1.00  no lost swing, ~2x the lord
            // T is estimated from the damage already on the board against the opponent's life, so
            // "close to lethal" and "plenty of turns left" are the same quantity read two ways.
            // MTG_GOBLIN_PILEDRIVER_CROWD: 0 = board only, 1 = buff_targets. _PER = per pump point
            // (100 = BODY = lord parity). _DELAY: 0 = derived (T-1)/T, >0 = that fixed percent.
            if (p.attack_pump_power_per_other_matching > 0)
            {
                static const int pile_crowd = EnvInt("MTG_GOBLIN_PILEDRIVER_CROWD", 2);
                static const int pile_per   = EnvInt("MTG_GOBLIN_PILEDRIVER_PER", static_cast<int>(BODY));
                static const int pile_delay = EnvInt("MTG_GOBLIN_PILEDRIVER_DELAY", 50);
                // The crowd is "other ATTACKING Goblins", which is not the lord's crowd: a lord's
                // buff waits around for hand Goblins to land, but a pump that only fires on the
                // swing is bounded by what is actually deployed by then -- about one more body per
                // turn. Mode 1 (buff_targets, board + up to 3 in HAND) ranks Piledriver FIRST on a
                // T1 empty board off three undeployed hand cards, which is the same over-reach as
                // the rejected variant 3. Mode 2 is board + the entering source + one deploy.
                const int crowd = pile_crowd == 1 ? buff_targets
                                : pile_crowd == 2 ? G + entering_fodder + 1
                                                  : G;
                // (T-1)/T from the swings left, CAPPED at lord parity. Both halves are load-bearing
                // and each was measured: the derived factor alone reaches 0.83 by T=6, which makes
                // Piledriver ~1.67x a lord and measures WORSE held-out (+2.0 searched) -- the user's
                // "it's important that the Piledriver is not strictly better" is empirically right,
                // and the cost is monotone in how far past parity it goes (0.50 -> -3.0, 0.65 -> 0.0,
                // 0.83 -> +2.0). The cap alone would lose the other half, since a flat 0.50 still
                // pays half credit when the game ends THIS turn. min() keeps both: T=1 gives exactly
                // 0.0 ("if close to lethal and no haste the lord is better") and everything from
                // T=2 up sits at parity.
                double realized = 1.0;
                if (!haste_avail)
                {
                    const double derived = (swings_left - 1) / static_cast<double>(swings_left);
                    realized = std::min(pile_delay / 100.0, derived);
                }
                v += p.attack_pump_power_per_other_matching * crowd
                     * static_cast<double>(pile_per) * realized;
            }
        }
        // Payoffs.
        if (p.etb_reveal_count > 0)            { v += p.etb_reveal_count * 75.0; }   // Muxus: cheat ~half of N free
        if (!p.tap_creates_tokens_per_controlled_subtype.empty())
        { v += G * 80.0; }                                                           // Krenko: G tokens/tap, snowballs
        if (p.etb_self_creates_tokens > 0)     { v += p.etb_self_creates_tokens * 90.0; } // Siege-Gang(3)/Mogg(1)
        if (p.sac_outlet_damage > 0)           { v += 40.0; }                        // Siege-Gang reach
        // DEATH-WATCH IMPULSE next to a sac outlet (user, 2026-08-04): "Rundvelt Hordemaster is
        // better if we are sacrificing creatures to Skirk Prospector -- the extra effect comes into
        // play ... and the skirk effect can be pretty huge."
        //
        // Hordemaster is "Whenever a Goblin you control dies, exile the top card; you may play it if
        // it's a Goblin", dies_watch_includes_self. On its own that is a slow trickle -- against a
        // goldfish nothing blocks, so Goblins essentially do not die. Next to a Skirk Prospector it
        // is a different card: every body converted to mana ALSO digs, and roughly half this deck is
        // Goblin creatures, so the ramp and the card advantage are the same activation. Worth
        // exactly ZERO until now -- value_of had no dies_trigger_impulse_exile term at all.
        //
        // Priced off the sacs we actually expect: skirk_ramp is the bodies a Skirk on the
        // battlefield can eat, and it is already 0 when there is no outlet, so the term is
        // self-gating and cannot inflate Hordemaster in a boardless state. Half of BODY per expected
        // death is the hit rate -- a dig that whiffs on a non-Goblin is inert deck-thinning.
        // The pairing is SYMMETRIC and both halves are priced here, because either card can be the
        // one already on the battlefield when we fetch the other:
        //   * fetching the Hordemaster INTO a Skirk board  -> skirk_ramp bodies are already eatable
        //   * fetching the Skirk INTO a Hordemaster board  -> the fetched outlet turns the board
        //     into mana AND cards, and this is the commoner state of the two, since Hordemaster
        //     ranks 2-4 and so is usually the one already down
        // Both are self-gating (each is zero without its partner), so neither can inflate a card in
        // a state where the combination does not exist.
        static const double impulse_per = EnvInt("MTG_GOBLIN_IMPULSE_PER", 0);
        if (p.dies_trigger_impulse_exile && skirk_ramp > 0)
        {
            v += skirk_ramp * impulse_per;
        }
        if (p.sac_outlet_add_mana_amount > 0 && deathwatch_on)
        {
            // Same body count the enabler credit uses for a FETCHED Prospector: the board's
            // expendable Goblins, the tutor source entering now, and the Prospector itself.
            const int sacs = (use_nolord ? goblin_fodder : G) + entering_fodder + 1;
            v += sacs * impulse_per;
        }
        // DIRECT DAMAGE TO THE FACE (MTG_GOBLIN_FACE_VALUE). Until now this was worth exactly ZERO
        // here: value_of had no etb_damage_any / channel_damage term at all, and face_burst -- which
        // does compute the damage correctly -- is consumed only by the exact-lethal override, so
        // Twinshot Sniper's "deals 2 damage to any target" paid nothing unless it happened to be the
        // last 2 points. It was scored as a 2-power body, 200, and the measured consequence is that
        // it is the deck's single largest ranking miss: reading the rank the SEARCH commits to at
        // W=12 (MTG_TUTOR_CHOSEN_RANK), Twinshot Sniper is 4 of the 5 past-window commits over an
        // unbiased 300-game sample, and 3 more in the games the width decides -- always at rank
        // 11-13 of 14-16.
        //
        // The weight is BODY -- one point of face damage is worth one point of power on a creature.
        // Burn is one-shot where a body's power recurs every combat, but against a goldfish it is
        // unconditional: nothing blocks, no lifegain, no summoning sickness, no needing to survive,
        // and it can be aimed at exactly the last points. Parity is the claim; it deliberately does
        // NOT assert that burn beats bodies.
        //
        // TRAINED, not guessed (test/goblins_face_value_train.sh). Selecting on the overnight
        // searched cases of seeds s4004+s5005 and reading s6006+s7007 only afterwards -- because
        // sweeping the whole overnight tier and taking its minimum is selection on the holdout, and
        // the regression tier's ~1,325 searched games cannot resolve a delta this size:
        //
        //     per     TRAIN(4000g)   VALIDATE(4000g)   d0(12000g)
        //      80        0.0             0.0             +3
        //     100       -4.0            -2.0             +4     <- adopted
        //     120       -4.0            -4.0            +15
        //     160       -6.0            -3.0            +25     <- train minimum
        //     200       -4.0            -3.0           +133
        //
        // Train's minimum is 160, but 160 vs 100 is 2 turn-units over 4,000 games -- noise. Searched
        // is flat from 100 up; what actually separates the weights is d0 cost, which climbs steeply.
        // So take the smallest weight that captures the effect, which is also the least extreme claim
        // and the cheapest. Below ~100 the term is inert (80 is byte-identical to off): Twinshot
        // Sniper's score has to clear a lord's before any ordering changes.
        //
        // max(), not sum: the ETB ping and the Channel mode are ALTERNATIVES (cast the creature, or
        // discard it from hand for the same damage), so adding them would double-count one card.
        static const bool  face_value = EnvOn("MTG_GOBLIN_FACE_VALUE", true);
        // 160 was trained under the LEGACY turn-start states, where the deploy discount separated
        // Twinshot Sniper (mv2) from Goblin Chainwhirler (mv3). Under the resolve axis both read
        // t=1 and the pair reduces to raw values, where 160 hands Twinshot the duel (520 vs 460)
        // -- measured 7 worse / 1 better across the held-out d0 flips. Chainwhirler's third power
        // point recurs every combat; Twinshot's extra face point is one-shot. Swept under resolve
        // (goblins overnight, closer on): 160 base; 120 d0 -27 (23/0); 100 -26 (23/1); 90 -129
        // (31/0); 80 -131 (33/0) -- a plateau below the crossing at 100, so the effect is the
        // CW>TS ordering itself. 90 = the smallest deviation from the trained value that captures
        // it (same selection rule the 160 was picked by). Searched untouched at every value.
        static const double face_per  = EnvInt("MTG_GOBLIN_FACE_VALUE_PER",
                                               TutorAxisResolveEnabled() ? 90 : 160);
        if (face_value)
        {
            const int face = std::max({ p.etb_damage_any, p.etb_damage_each_opponent, p.channel_damage });
            // DOMINATED BURN (user, 2026-08-05). A tutor fetches ONE card, so a burn payoff is only
            // worth its face value if it is the BEST burn still fetchable -- if a strictly better one
            // is sitting in the same library, this card's damage is not a reason to take it.
            //
            // Goblin Chainwhirler is the case, and in this environment it is dominated twice over:
            // "if you need a good 3-drop threat you want a lord. If you want the immediate damage
            // Twinshot is better ... what makes it playable in a real game is the ability to ping
            // 1 toughness creatures (i.e. like dorks or aggressive 1-drops)". Goldfishing has no
            // opponent creatures, so the half of its ETB that justifies the card is INERT -- the
            // card data says as much ("only matters vs opponent spawn tokens"). What is left is
            // 1 damage to the face against Twinshot Sniper's 2, on a card that also costs {R}{R}{R}
            // where Twinshot can be CHANNELLED from hand for {1}{R}.
            //
            // Deliberately by RULE, not by card name: any candidate whose face damage is strictly
            // beaten by another fetchable candidate loses the credit, so the deck can change without
            // this going stale. It self-restores -- once Twinshot Sniper has left the library,
            // Chainwhirler is the best burn again and gets its full credit back, which is exactly
            // "only taken if twinshot is gone and we need the 1 damage".
            //
            // MEASURED AND NOT ADOPTED (default 0) -- the card evaluation is right and the engine
            // already agrees. Demoting Chainwhirler 460 -> 300 moves it from rank 5 to rank 7, out
            // of the window, and changes EXACTLY ZERO searched games across 8,000 held-out (and 1,100
            // regression). Modes 1 and 2 are indistinguishable from each other: once it leaves the
            // window, ranking it dead last buys nothing further. So the search never wanted it, and
            // freeing its window slot never helps anyone else either.
            //
            // The only measurable effect is a cost: d0 +88.0, which is one game -- s8008 gi1882, T8
            // -> unwon -- against two d0 games improved. And that game is a GREEDY artifact: at
            // depth 3 and depth 5 both arms win on T5. d0 takes cands[0] with no search, so it is
            // the only policy that can be hurt by removing a card the search would have rejected.
            //
            // Kept as a lever because the reasoning generalises (a tutor takes ONE card, so a
            // dominated payoff is not a reason to take it) and because it answers, with numbers, a
            // question worth not re-asking.
            if (face > 0 && !is_dominated_burn(d)) { v += face * face_per; }
        }
        return v;
    };

    // --- enabler credit: what the fetch UNLOCKS, not just what it is ---------------------------
    // value_of above scores a card for its own board impact, so every ENABLER in this deck reads as
    // a vanilla body: Goblin Warchief 270, Skirk Prospector 100, Goblin Lackey 100 -- against a
    // lord's 900-1100. At W=12 the unranked axis stumbled onto them by shuffle order; at W=4 they
    // are unreachable, and that is the whole remaining W12->4 held-out cost. Measured (Goblins
    // overnight, both arms at unlimited budget) the pre-W arm fetched an enabler and won a turn
    // earlier in every one of the four: gi602 + gi206 Warchief (T5 vs T6), gi842 Skirk (T5 vs T6),
    // gi924 Lackey (T5 vs T6) -- each time to land the bomb ALREADY IN HAND one turn sooner.
    //
    // So the credit is measured against the hand: how much does this fetch pull forward the best
    // thing I am already holding? One idea, four expressions of it (cost cut / ramp / free cheat /
    // blanket haste), each a fraction of the accelerated card's own value so the scale needs no new
    // constant. Gated on actually holding something stuck -- an enabler with nothing to enable is
    // just its body. MTG_GOBLIN_ENABLER_RANK=0 drops the term for the A/B.
    static const bool enabler_rank = EnvOn("MTG_GOBLIN_ENABLER_RANK", true);   // cached: hot path
    // ROUND-2 ranking, ADOPTED 2026-08-04 (see docs/design/goblins-enabler-worse-games.md): three
    // user-directed corrections -- the "stuck" test measured against mana_next rather than untapped
    // mana, Skirk/Lackey scaled by the board and hand states that actually make each good, and a
    // duplicate discount applied to copies in HAND only. Shipped together with the lord-amplification
    // term and the W=4 -> 6 widening it needs; the three recover all three tracked regressions
    // (gi44, gi573, gi849) and measure -5.0 held-out searched with train agreeing on every tier.
    static const bool rank_v2 = EnvOn("MTG_GOBLIN_RANK_V2", true);
    // LORD AMPLIFICATION (MTG_GOBLIN_LORD_AMP, ADOPTED default-on). The enabler channels
    // below all ask "what makes the stuck bomb ARRIVE sooner", which is only half the question -- a LORD
    // asks "what is the bomb worth WHEN it arrives", and a Goblin bomb arrives as a CROWD: Muxus puts
    // roughly half of its six revealed cards onto the battlefield, Siege-Gang shows up as four bodies.
    // A +1/+1 lord buffs every one of them, and nothing in the ranking sees that: value_of credits a
    // lord over buff_targets = board + hand, which counts the bomb as the single card it is in hand.
    //
    // This is variant 3's insight made LOCAL, which is the one thing that write-up got wrong. Variant 3
    // widened buff_targets itself, so every lord in every state got the wider count -- it recovered its
    // target games and cost +20.0 turn-units on held-out searched. Here the extra recipients are read
    // off the SINGLE best stuck hand card and credited only to a lord, so the term is inert wherever
    // the enabler term is (nothing stuck -> no amplification) and cannot move a state with no bomb in
    // hand. See docs/design/goblins-enabler-worse-games.md.
    //
    // gi44 is the case: at the T2 Matron the hand holds Muxus, and the ranking scores Chieftain 945 vs
    // Warchief 1040 -- Warchief takes it on 0.80 x 850 of "arrives sooner" credit while Chieftain's
    // +1/+1 is priced over four recipients. The Muxus that lands on T4 brings three more Goblins, so
    // Chieftain's real contribution that swing is +7, not +4, and Chieftain is the fetch that wins a
    // turn earlier.
    //
    // NOT adoptable on its own, and the reason is worth keeping: at the old W=4 this term measured
    // -114.0 turn-units on held-out d0 and +11.0 on held-out SEARCHED. d0 takes cands[0], so it reads
    // the ordering directly and says the ranking genuinely improved; the searched loss was the ranking
    // pushing a still-wanted card out of a four-slot window. Re-measured at W=6 the searched sign flips
    // to -4.0 with the d0 gain unchanged, which is what identifies it as window membership rather than
    // a bad order. Hence the paired TutorSearchWidth 4 -> 6 -- see the note there.
    static const bool lord_amp = EnvOn("MTG_GOBLIN_LORD_AMP", true);
    double stuck_hand_value = 0.0;   // best hand Goblin I cannot cast right now = what to accelerate
    int    stuck_mv         = 0;
    int    stuck_bodies     = 0;     // extra bodies that bomb brings with it (lord buff recipients)
    for (const Card& h : s.players[controller].hand)
    {
        const CardDefinition* hd = CardDatabase::Instance().LookupCached(h);
        if (!hd) { continue; }
        const Card& hc = hd->card;
        if (!hc.IsCreature() || !CardHasSubtype(hc, "Goblin")) { continue; }
        // "Stuck" means genuinely out of reach -- not castable this turn NOR next, ramp included
        // (user, 2026-08-04: "when we are low on gas or can cast what we have anyway neither is
        // worth considering"). Testing against untapped mana alone was the defect: in gi573 a Goblin
        // Chieftain with untapped=1 / now=4 / next=6 counted as stranded, so the enablers collected
        // ~1000 points for "accelerating" a 3-drop that was castable immediately -- and Chieftain
        // even collected 378 for accelerating ITSELF. mana_next already includes the Skirk ramp.
        if (hc.m_mana_cost.ManaValue() <= (rank_v2 ? mana_next : untapped_mana)) { continue; }
        const double v = value_of(hd, hc);
        if (v > stuck_hand_value)
        {
            stuck_hand_value = v;
            stuck_mv         = hc.m_mana_cost.ManaValue();
            // Bodies this bomb brings ALONGSIDE itself, i.e. the extra recipients a lord would buff.
            // Muxus reveals N and puts the Goblins among them -- half of a Goblin-dense library is the
            // same estimate value_of already uses for the reveal ("cheat ~half of N free").
            stuck_bodies = hd->params.etb_self_creates_tokens + hd->params.etb_reveal_count / 2;
        }
    }
    // LACKEY AS A REPEATING ENGINE (MTG_GOBLIN_LACKEY_REPEAT). Every other enabler channel is a
    // fraction of the ONE best stuck card, because each of them accelerates that card by a turn or
    // two. Goblin Lackey is not that shape: it puts a Goblin from hand onto the battlefield EVERY
    // combat, for the rest of the game, and 33 of this deck's 61 cards are Goblin creatures -- so its
    // worth is not bounded by what happens to be stuck right now.
    //
    // gi124 is the case the hand-bound model cannot reach. At the T3 Matron the hand holds Krenko,
    // Matron and Chainwhirler, of which only Krenko counts as stuck and only by one turn, so the
    // credit is 0.20 x 300 = 60 and Lackey ranks 11th. The fetched Lackey then connects on T4 AND T5,
    // putting Siege-Gang (MV 5) and Chainwhirler (MV 3) onto the battlefield free -- 8 mana of
    // creatures, and Siege-Gang was not even in hand when the fetch was made.
    //
    // So the second and later drops are priced off what the DECK will supply rather than what the
    // hand holds: the mean value of the Goblin creatures still in the library. Reading library
    // COMPOSITION (not order) is deck knowledge a player has, and is what TutorCandidates already
    // does to build its candidate list -- no clairvoyance about the draw.
    //
    // But a LATER drop is worth much less than the first, and not merely by a decay (user,
    // 2026-08-04): "that Chainwhirler can only attack on turn 6 ... unless the 1 damage from it does
    // the trick, this isn't so amazing. But yes, it is very much not nothing. And really does ensure
    // we don't slip much beyond T6." Lackey puts the creature in on COMBAT DAMAGE, so it arrives
    // summoning-sick and its body does nothing until the following turn. What a late drop DOES buy
    // immediately is its enter-the-battlefield effect -- Chainwhirler's ping, Siege-Gang's three
    // tokens and reach. So the later drop is credited on the ETB/payoff half of value_of with the
    // BODY half struck out, which is exactly the user's "not amazing, very much not nothing": it is
    // a floor against slipping a turn rather than a tempo gain.
    static const bool lackey_repeat = EnvOn("MTG_GOBLIN_LACKEY_REPEAT");
    static const int  lackey_pct    = EnvInt("MTG_GOBLIN_LACKEY_REPEAT_PCT", 35);   // swept, see the doc
    double lib_goblin_mean = 0.0;
    if (lackey_repeat)
    {
        double sum = 0.0; int n = 0;
        for (const Card& lc : s.players[controller].library)
        {
            const CardDefinition* ld = CardDatabase::Instance().LookupCached(lc);
            if (!ld || !ld->card.IsCreature() || !CardHasSubtype(ld->card, "Goblin")) { continue; }
            // ETB/payoff value only -- the body cannot attack the turn a Lackey hit drops it in.
            sum += std::max(0.0, value_of(ld, ld->card) - ld->card.m_power.value_or(0) * BODY);
            ++n;
        }
        if (n > 0) { lib_goblin_mean = sum / n; }
    }
    // Turns until the stuck card could be HARD-CAST unaided (+1 land/turn). This is what an enabler
    // is competing against: if the answer is 1, we can just cast it next turn and no enabler is
    // worth a fetch slot (user: "when we are low on gas or can cast what we have anyway neither is
    // worth considering").
    const int stuck_turns = (stuck_mv <= untapped_mana)
                          ? 0 : std::max(1, stuck_mv - CountLandsInPlay(s, controller));
    auto enabler_of = [&](const CardDefinition* d) -> double
    {
        if (!enabler_rank || !d || stuck_hand_value <= 0.0) { return 0.0; }
        const CardParams& p = d->params;
        double frac = 0.0;
        // Warchief: "Goblin spells cost {1} less" -- the stuck bomb arrives a turn early.
        //
        // MEASURED AND REJECTED (2026-08-04), kept as the lever + the reasoning because the DIAGNOSIS
        // it came from is still the best account of where this term is wrong. The flat 0.50 prices the
        // cut as if it accelerated exactly ONE card, because the whole enabler credit is anchored on the
        // single best stuck card. That is about right in gi44 (a hand of Krenko / Siege-Gang / Muxus,
        // only one anywhere near castable) and badly short in gi131 (King + Stingscourger + Piledriver,
        // where -{1} each is the difference between two spells and THREE in one turn -- the line that
        // kills on T4 instead of T5). So: scale the cut by the gas it can actually be spent on, which is
        // the user's "only capable of helping us use gas in hand" made quantitative.
        //
        // It does exactly what it was designed to do on gi131 (T5 -> T4) and simply MOVES the error --
        // gi44 goes T4 -> T5 and gi573 T3 -> T4 straight back. One scalar cannot separate these states,
        // which is the useful negative result: "how much gas the cut unlocks" is not the axis that
        // distinguishes them. Held-out overnight, against the same bundle without it: searched -5.0 ->
        // 0.0, d0 -95.0 -> +37.0. Worse on both tiers. MTG_GOBLIN_CUT_WIDTH=1 re-enables it.
        if (!p.reduces_spell_subtype.empty())
        {
            static const bool cut_width = EnvOn("MTG_GOBLIN_CUT_WIDTH");
            if (!cut_width) { frac += 0.50; }
            else
            {
                int deployable = 0;   // hand Goblins the cut could actually be spent on soon
                for (const Card& h : s.players[controller].hand)
                {
                    const CardDefinition* hd = CardDatabase::Instance().LookupCached(h);
                    if (!hd) { continue; }
                    const Card& hc = hd->card;
                    if (!hc.IsCreature() || !CardHasSubtype(hc, "Goblin")) { continue; }
                    if (hc.m_mana_cost.ManaValue() <= mana_next + 1) { ++deployable; }
                }
                frac += 0.25 + 0.25 * std::min(3, deployable);
            }
        }
        // Warchief again: blanket haste means whatever lands next swings the turn it lands.
        if (p.grants_haste && !p.subtypes_affected.empty())    { frac += 0.30; }
        // Skirk and Lackey are MIRROR-IMAGE enablers, and a flat fraction models neither (user,
        // 2026-08-04): "Skirk should probably not be considered when we have almost no goblins on
        // board ... on the flip side it is a great enabler when we have a lot on board. Lackey is
        // the opposite, better when we have heavy stuff in hand and almost nothing on board."
        //
        // Skirk converts BODIES into mana, so its worth scales with the bodies available to sac --
        // the old flat 0.40 behind a G>=2 gate paid full price for a single sacrificeable Goblin.
        //
        // But "G - 1" is the WRONG BODY COUNT, in exactly the three ways skirk_ramp was already
        // corrected (236bb13, c13cbac) -- and this line never got any of them, because skirk_ramp is
        // gated on skirk_on (a Prospector ALREADY on the battlefield) and so is identically zero in
        // precisely the states where we are deciding whether to FETCH one. The fodder a fetched
        // Prospector will actually eat is:
        //   + the board's EXPENDABLE Goblins (goblin_fodder, not G -- feeding a lord to Skirk
        //     de-buffs everything else, so lords are fodder only in extremis)
        //   + the tutor SOURCE, a Goblin creature entering right now, which is the whole reason this
        //     function is running (the board scan ran while it was still in hand)
        //   + the PROSPECTOR ITSELF: "Sacrifice a Goblin: Add {R}" is repeatable, needs no tap, and
        //     Skirk is a Goblin, so the last activation eats it. N bodies convert to N mana, not N-1
        //     -- which is how CollectActions' own multi-sac burst already counts victims.
        // The old "-1" was thus doubly wrong here: it subtracted a self-sac that is legal AND omitted
        // the two bodies that are arriving.
        //
        // The measured consequence is stark. Reading the rank the SEARCH commits to (W=12,
        // MTG_TUTOR_CHOSEN_RANK), Skirk is 3 of the 10 past-window commits -- and in all three
        // (s3003 gi194, s4004 gi727, s5005 gi920) G is 0 or 1, so 0.13 * max(0, G-1) is EXACTLY
        // ZERO and Skirk scores 100: a vanilla 1/1, rank 12 of 14-16. That it is the gate and not the
        // hand is provable from the same dumps -- Goblin Lackey, the other enabler, collects +340
        // from stuck_hand_value in those very states. With the real count (typically 1 + 1 + 1 = 3)
        // the fraction is 0.39, just under the cap, so Skirk scores 321 instead of 100 and moves from
        // rank 12 to rank 5-8, inside or on the edge of the shipped W=6.
        //
        // ADOPTED on a replicating measurement, unlike the Piledriver crowd correction above:
        // regression (train) searched -2.0, overnight (held-out) searched -3.0 over 8,000 games,
        // 4 games better / 1 worse, d0 exactly 0.0 over 12,000 games. Same sign on both tiers.
        // MTG_GOBLIN_SKIRK_FETCH_FODDER=0 restores the board-only count for the A/B.
        if (p.sac_outlet_add_mana_amount > 0)
        {
            static const bool fetch_fodder = EnvOn("MTG_GOBLIN_SKIRK_FETCH_FODDER", true);
            // Per-body rate and cap, both in hundredths, TRAINED on s4004+s5005 and read off
            // s6006+s7007 afterwards (test/goblins_skirk_rate_train.sh -- same split protocol and
            // same reason as the face-damage weight).
            //
            // The CAP is not what binds at the miss states: gi194 credits 0.26 = 0.13 x 2, because
            // its one board Goblin is a LORD (so goblin_fodder is 0) and only the entering Matron
            // and the Prospector itself are fodder. Raising the old 0.40 cap would change nothing
            // there -- the per-body RATE is the lever. The cap is raised to 0.60 alongside purely so
            // a higher rate cannot silently clip on high-fodder boards; it is inert at the old rate
            // (13/60 reproduces the 13/40 measurement exactly, train -4.0 / validate +1.0).
            //
            //     rate   TRAIN(4000g)   VALIDATE(4000g)   d0(12000g)
            //      13       -4.0            +1.0             0.0     (the fodder fix alone)
            //      18       -6.0            +1.0             0.0     <- ADOPTED
            //      22       -6.0            +1.0             0.0
            //      26       -6.0            +1.0             0.0
            //      32       -6.0            +1.0             0.0
            //
            // Flat from 18 up: the ordering SATURATES, because past 0.18/body Skirk passes nothing
            // further that changes an outcome. At gi194, 0.18 x 2 = 0.36 puts it at 406, just over
            // Goblin Chainwhirler's 400 -- that single crossing is the whole gain. So take the
            // smallest rate that captures it, the same rule the face-damage weight was picked by.
            // The +1.0 on validate is one game, s7007 gi588 (T4->T5), and it is CHURN: it recovers
            // at 16x budget and at unlimited. d0 is 0.0 at every rate -- the greedy top pick never
            // changes, so this is purely window membership, which is what it was designed to be.
            static const double rate = EnvInt("MTG_GOBLIN_SKIRK_RATE", 18) / 100.0;
            static const double cap  = EnvInt("MTG_GOBLIN_SKIRK_CAP",  60) / 100.0;
            const int fodder = fetch_fodder
                             ? (use_nolord ? goblin_fodder : G) + entering_fodder + 1
                             : std::max(0, G - 1);
            frac += rank_v2 ? std::min(cap, rate * fodder) : (G >= 2 ? 0.40 : 0.0);
        }
        // Lackey buys the TURNS we would otherwise spend reaching the stuck card's cost, so it
        // scales with how far out of reach that card is -- and is worth nothing when the card is
        // castable next turn anyway.
        double repeat = 0.0;
        if (!p.combat_damage_puts_subtype_from_hand.empty())
        {
            frac += rank_v2 ? std::min(0.60, 0.20 * std::max(0, stuck_turns - 1)) : 0.60;
            // ... and the drops AFTER the first, priced off the deck rather than the hand (see the
            // MTG_GOBLIN_LACKEY_REPEAT note above). ONE decayed extra drop, not an unbounded stream:
            // the goldfish opponent never blocks, but the game is usually over within a turn or two
            // of the engine coming online, so a second drop is the realistic horizon.
            repeat = (lackey_pct / 100.0) * lib_goblin_mean;
        }
        // A lord does not make the bomb arrive sooner -- it makes the arrival hit harder, across every
        // body the bomb brings with it (see the MTG_GOBLIN_LORD_AMP note above). Additive in BODY units
        // rather than a fraction of stuck_hand_value: this is literal extra power on the swing, not a
        // share of the bomb's worth.
        double amp = 0.0;
        if (lord_amp && p.power_bonus > 0 && !p.subtypes_affected.empty())
        { amp = p.power_bonus * stuck_bodies * BODY; }
        return frac * stuck_hand_value + amp + repeat;
    };

    // --- deploy discount: how many TURNS until it can hit the board, over the best path ---------
    // (mana incl. Skirk ramp; Goblin Lackey free-drop; Aether Vial put once its charge reaches MV).
    // Value-PRIMARY but deployability shades HARD: a bomb that is genuinely stuck for turns must fall
    // BELOW a deployable developer -- the flat 0.45 was too gentle and over-fetched an undeployable
    // Muxus (analysis-goblins.md, the searched-slower audit). Curve (user 2026-08-02): t=1 (NEXT turn)
    // is ACCEPTABLE -- "you don't always have mana for any of them this turn" -- so t1 is only a mild
    // 0.85; the steep 0.45/step decay begins at t>=2 (genuinely stuck): 1.0 / 0.85 / 0.38 / 0.17.
    auto turns_to_deploy = [&](const Card& c, bool arriving = false) -> int
    {
        const int mv = c.m_mana_cost.ManaValue();
        int t;
        // CAPACITY ANCHOR (MTG_TUTOR_AXIS_RESOLVE only; legacy mode byte-identical). Under resolve
        // mode this ranking runs at the tutor's RESOLUTION state, where the plan's mana is already
        // SPENT -- so "mv - mana_now" stops meaning "turns until deployable" and becomes "turns to
        // re-accumulate from the leftover", a miscount that buried Muxus at t=5 / disc 0.035 (off
        // leftover 1 where capacity was 5) and cost every one of the resolve-mode held-out goblins
        // regressions (gi714/727/768/200: the baseline's winning line is Muxus T4 in all four; the
        // resolve arm never put it on the axis). The honest read for a card ARRIVING IN HAND is
        // capacity-based: next turn at the earliest -- a to-hand fetch can never be cast this turn,
        // the plan is frozen before it arrives -- plus one turn per land it needs beyond next
        // turn's capacity. mana_next (lands + next drop + Skirk ramp) is the same quantity the
        // t<=1 clamp below always trusted at the boundary; this extends it past the boundary
        // instead of falling back to the leftover fiction. HAND cards (hand_has_play,
        // arriving=false) keep the leftover read: for them "castable this turn out of unspent
        // mana" is the honest question at any state.
        // MTG_GOBLIN_RESOLVE_CAP=0 restores the leftover-anchored read under resolve mode for the
        // component A/B (the anchor is model-correct but must carry its own number -- bundles hide
        // harmful components).
        static const bool resolve_cap = EnvOn("MTG_GOBLIN_RESOLVE_CAP", true);
        if (arriving && resolve_cap && TutorAxisResolveEnabled())
        {
            t = 1 + std::max(0, mv - mana_next);
        }
        else
        {
            t = std::max(0, mv - mana_now);        // via mana (+~1/turn beyond what's available now)
            if (mv <= mana_next)          { t = std::min(t, 1); }
        }
        if (lackey_now)               { t = 0; }   // free-drop any Goblin this turn (ignores mana)
        else if (lackey_persist)      { t = std::min(t, 1); }
        if (vial_charge >= 0)         { t = std::min(t, std::max(0, mv - vial_charge)); } // Vial: +1 charge/turn
        return std::max(0, t);
    };
    // Opportunity cost: if the hand ALREADY holds a deployable-this-turn threat, another card we can't
    // play now is redundant gas -- we need to DEPLOY, not draw more. Mild downweight for such candidates.
    bool hand_has_play = plan_known && plan_deploys;   // exact: does the plan actually deploy one
    if (!plan_known)
    {
        for (const Card& h : s.players[controller].hand)
        {
            const CardDefinition* hd = CardDatabase::Instance().LookupCached(h);
            if (hd && hd->card.IsCreature() && turns_to_deploy(hd->card) == 0
                && h.m_name != "Goblin Matron") { hand_has_play = true; break; }
        }
    }
    // The two curve constants are exposed for the MTG_TUTOR_AXIS_POSTLAND recalibration sweep: that
    // fix moves cards from t=2 to t=1 wholesale, so if the curve had merely absorbed the old
    // projection bias, re-fitting these should recover the loss. (Hundredths.)
    static const double disc_t1   = EnvInt("MTG_GOBLIN_DISC_T1", 85) / 100.0;
    static const double disc_step = EnvInt("MTG_GOBLIN_DISC_STEP", 45) / 100.0;
    auto discount_of = [&](const Card& c) -> double
    {
        const int t = turns_to_deploy(c, /*arriving=*/true);   // candidates arrive in HAND
        double disc = 1.0;
        if (t >= 1) { disc = disc_t1; for (int k = 1; k < t; ++k) { disc *= disc_step; } } // mild t1, steep t>=2
        if (t > 0 && hand_has_play) { disc *= 0.75; }   // opportunity cost: already have a play
        return disc;
    };

    // --- projected swing: the attack we will ACTUALLY make this turn, not the board as it stands -----
    // The lethal-reach test below has to measure the swing we are about to make, and by the time it runs
    // two deploys are already all but certain: the tutor SOURCE is entering (that is why we are here),
    // and this deck's standard turn is "and also cast the lord". A bare board scan sees neither. Measured
    // on gi865 (Goblins overnight d3_s4004) it read ready_atk=7 where the real swing was 17 -- Goblin
    // Chieftain's +1/+1 over 7 Goblins plus its own hasty body plus the haste it grants the fresh Matron
    // -- so the fetch that closed the game (Twinshot Sniper, 2 to the face for EXACTLY lethal at 19) never
    // reached the top 4 and the T3 kill was invisible to the search at W=4. Conservative by construction:
    // the lord must be hard-castable ALONGSIDE the source out of real untapped mana (no Skirk ramp, which
    // eats the very attackers being counted; no Lackey drop, which resolves after combat damage and so
    // cannot attack). MTG_GOBLIN_SWING_LETHAL=0 restores the bare board scan for the A/B.
    int swing_atk = ready_atk;
    static const bool s_swing_lethal = EnvOn("MTG_GOBLIN_SWING_LETHAL", true);   // cached: hot path
    if (s_swing_lethal)
    {
        int src_mv = 0, src_pow = 0;   // the tutor source in hand -- it is entering this turn
        for (const Card& h : s.players[controller].hand)
        {
            const CardDefinition* hd = CardDatabase::Instance().LookupCached(h);
            if (!hd || !(hd->params.tutor_to_hand || hd->params.tutor_to_top)) { continue; }
            src_mv  = hd->card.m_mana_cost.ManaValue();
            src_pow = std::max(0, hd->card.m_power.value_or(0));
            break;
        }
        const int ready_goblins = std::max(0, goblins_controlled - goblins_sick);
        int best_lord = 0;             // the best team-buff/haste lord affordable next to the source
        for (const Card& h : s.players[controller].hand)
        {
            const CardDefinition* hd = CardDatabase::Instance().LookupCached(h);
            if (!hd) { continue; }
            const Card&       hc = hd->card;
            const CardParams& hp = hd->params;
            if (!hc.IsCreature() || hp.subtypes_affected.empty()) { continue; }
            if (hc.m_mana_cost.ManaValue() + src_mv > untapped_mana) { continue; }
            const int  bonus = std::max(0, hp.power_bonus);
            int extra = bonus * ready_goblins;                        // buff on what already swings
            if (hp.grants_haste)
            {   // sick bodies (and the fresh source) turn on, each also carrying the buff
                extra += sick_goblin_power + bonus * goblins_sick;
                extra += src_pow + bonus;
            }
            // Its own body swings too if it has haste intrinsically (Chieftain) or grants it to
            // itself (Warchief: "Goblins you control have haste", no lord_excludes_self).
            if (hc.HasKeyword(Keyword::Haste) || (hp.grants_haste && !hp.lord_excludes_self))
            { extra += std::max(0, hc.m_power.value_or(0)); }
            best_lord = std::max(best_lord, extra);
        }
        swing_atk += best_lord;
    }

    // --- lethal reach: a fetch whose CHEAP direct damage closes the game THIS turn must outrank a bigger
    // body. Credit only burst reachable now over the card's cheapest path (channel from hand / Lackey or
    // hardcast ETB ping / Siege-Gang sac), gated on paying that path this turn. Generalizes "opp at 1,
    // drop it now": e.g. Twinshot Sniper channels {1}{R} for 2 from hand (no body) or pings 2 off a Lackey.
    auto face_burst = [&](const CardDefinition* d, const Card& c) -> int
    {
        if (!d) { return 0; }
        const CardParams& p = d->params;
        const int mv = c.m_mana_cost.ManaValue();
        int burst = 0;
        // Channel: pay channel_cost + discard from HAND -> channel_damage to face (needs no board slot).
        if (p.channel_damage > 0 && p.channel_cost && p.channel_cost->ManaValue() <= untapped_mana)
        { burst = std::max(burst, p.channel_damage); }
        // ETB ping ("any target" -> face, or "each opponent" -> face): the body must ENTER this turn.
        const int etb_face = std::max(p.etb_damage_any, p.etb_damage_each_opponent);
        if (etb_face > 0)
        {
            const bool enters = lackey_now || mv <= untapped_mana || (vial_charge >= 0 && vial_charge >= mv);
            if (enters) { burst = std::max(burst, etb_face); }
        }
        // Siege-Gang: cast it, then sac the tokens it makes for sac_outlet_damage each ({1}{R}/activation).
        if (p.sac_outlet_damage > 0 && p.etb_self_creates_tokens > 0 && mv <= untapped_mana)
        {
            const int sacs = std::min(p.etb_self_creates_tokens, (untapped_mana - mv) / 2);
            if (sacs > 0) { burst = std::max(burst, p.sac_outlet_damage * sacs); }
        }
        return burst;
    };

    // "Play this turn" premium + redundancy discount (user, 2026-08-04): "Hordemaster is a better
    // option when we can play it this turn, especially if we already have a Chieftain. There should
    // be a 'play this turn' benefit in those cases."
    //
    // The existing deploy discount only separates castable-now from next-turn by 1.00 vs 0.85 -- 15%,
    // nowhere near enough to rank a castable Rundvelt Hordemaster over an UNCASTABLE second Goblin
    // Chieftain that is already sitting in hand. Two independent facts were missing: a fetch we can
    // deploy immediately turns into board presence a full turn sooner, and a copy we already hold or
    // control adds far less than the first (only one can be deployed per turn anyway).
    // Deliberately HARD-CAST mana only -- a Lackey/Vial put is not "cast it this turn".
    // HAND copies only, deliberately (user, 2026-08-04): "duplicates in hand will be slow ... but for
    // example, you might want to get a Muxus in hand for a Lackey drop even if you have one on
    // board." A second copy in HAND is close to dead -- only one can be cast per turn. A copy on the
    // BATTLEFIELD says nothing about wanting one in hand: it is exactly the Lackey/Vial cheat target,
    // and a second lord stacks anyway. Counting board copies was measured and is the wrong half.
    auto copies_in_hand = [&](const std::string& name) -> int
    {
        int c = 0;
        for (const Card& h : s.players[controller].hand) { if (h.m_name == name) { ++c; } }
        return c;
    };
    static const bool dup_hand_penalty = EnvOn("MTG_GOBLIN_DUP_HAND", true);
    auto score_of = [&](const std::string& name) -> double
    {
        for (const Card& lc : s.players[controller].library)
        {
            if (lc.m_name != name) { continue; }
            const CardDefinition* d = CardDatabase::Instance().LookupCached(lc);
            const Card& c = d ? d->card : lc;
            double sc = (value_of(d, c) + enabler_of(d)) * discount_of(c);
            const int burst = face_burst(d, c);
            // Only when the burst is what CROSSES lethal (attackers alone fall short, but reach them): a
            // fetch that guarantees the kill this turn sorts ahead of every non-lethal one, best burst first.
            if (burst > 0 && swing_atk < opp_life && opp_life <= swing_atk + burst)
            { sc += 1.0e6 + burst; }
            // NEXT-TURN LETHAL VIA A FETCHED LORD (MTG_GOBLIN_LORD_CLOSER, default off pending its
            // A/B). The symmetric override to face_burst's this-turn-lethal above, one turn later,
            // for the crowd instead of the burn -- and the missing term behind the resolve-mode d0
            // regressions: dissecting all 30 worse held-out d0 games, the flip legacy->resolve is
            // 'Chieftain -> Muxus' in 18 of them against ONE better -- the honest state prices
            // Muxus t=1 correctly (it IS castable next turn off the Skirk sacs) but nothing scores
            // that the LORD closes the game a turn before Muxus's swing does. s10010 gi4 is the
            // worked case: T3 fetch, board Skirk+Lackey+Hordemaster+Matron, opp 16 -- Chieftain's
            // +1/+1 over the (by-then unsick) board plus its own hasty body plus a hand Piledriver
            // is exactly lethal on T4; Muxus casts T4 and swings T5. The user's own rule, already
            // encoded in the Piledriver (T-1)/T factor: "if close to lethal and no haste the lord
            // is better" -- this computes it for real instead of via the swings_left estimate.
            //
            // Projection (conservative, same spirit as swing_atk's rules): next turn every body on
            // the board today attacks (sickness has worn off), each Goblin carries the fetched
            // lord's +1/+1; the lord's own body counts only if it is hasty or grants itself haste
            // (cast next turn, it is otherwise sick); no hand deploys are counted. Gated on the
            // lord actually being deployable next turn (t <= 1) and on the board ALONE not already
            // being next-turn lethal (then the fetch is not what crosses). Tier 5e5: below the
            // this-turn kill, above every non-lethal score.
            // DEFAULT ON under the resolve axis (adopted 2026-08-05 with the face_per recalibration
            // below it in the file; =0 disables). Held-out: d0 -9 (9 better / 0 worse), searched
            // untouched (the search already finds these closes -- this is a pure greedy-tier fix),
            // train-neutral, truth-table regret +4 = exact oracle parity.
            static const bool lord_closer = EnvOn("MTG_GOBLIN_LORD_CLOSER", TutorAxisResolveEnabled());
            // copies_in_hand gate: a copy of this lord ALREADY IN HAND provides the close without
            // spending the fetch on it -- the bonus otherwise bulldozes the duplicate discount
            // (applied later at a mere x0.55) and takes a redundant copy over a fresh card.
            // s10010 gi1669 is the case: a Lackey-put Matron fetched a second Chieftain past a
            // hand Chieftain and the T3 kill became T5 (the baseline fetched Hordemaster and cast
            // BOTH lords on T3).
            if (lord_closer && sc < 1.0e6 && d != nullptr
                && !d->params.subtypes_affected.empty()
                && (d->params.power_bonus > 0 || d->params.grants_haste)
                && copies_in_hand(name) == 0
                && turns_to_deploy(c, /*arriving=*/true) <= 1)
            {
                const int bonus = std::max(0, d->params.power_bonus);
                const int board_next = ready_atk + sick_goblin_power;   // everyone attacks next turn
                int with_lord  = board_next + bonus * goblins_controlled;
                int attackers  = goblins_controlled;                    // crowd for a pump deploy
                const bool lord_haste = c.HasKeyword(Keyword::Haste)
                    || (d->params.grants_haste && !d->params.lord_excludes_self);
                if (lord_haste)
                { with_lord += std::max(0, c.m_power.value_or(0) + bonus); ++attackers; }
                // ... plus the best ONE hand creature castable ALONGSIDE the lord out of next
                // turn's mana (the same affordability rule swing_atk uses for this turn). It
                // attacks only with haste from somewhere -- its own keyword, this lord's grant, or
                // a haste source already on the battlefield. A pump body (Piledriver) uses its
                // documented swing formula: base + bonus + 2 per OTHER attacker. gi4's actual T4
                // close was exactly lord + hand Piledriver; the board-only projection missed it.
                const int lord_mv = c.m_mana_cost.ManaValue();
                int best_extra = 0;
                for (const Card& h : s.players[controller].hand)
                {
                    const CardDefinition* hd = CardDatabase::Instance().LookupCached(h);
                    if (!hd || !hd->card.IsCreature()) { continue; }
                    if (hd->card.m_mana_cost.ManaValue() + lord_mv > mana_next) { continue; }
                    const bool is_gob = CardHasSubtype(hd->card, "Goblin");
                    const bool hasty  = hd->card.HasKeyword(Keyword::Haste)
                        || (d->params.grants_haste && is_gob) || haste_source;
                    if (!hasty) { continue; }
                    const int b = is_gob ? bonus : 0;
                    int contrib = std::max(0, hd->card.m_power.value_or(0)) + b;
                    if (hd->params.attack_pump_power_per_other_matching > 0 && is_gob)
                    { contrib += hd->params.attack_pump_power_per_other_matching * attackers; }
                    best_extra = std::max(best_extra, contrib);
                }
                with_lord += best_extra;
                if (board_next < opp_life && opp_life <= with_lord)
                { sc += 5.0e5 + with_lord; }
            }
            // NEXT-TURN LETHAL VIA THE FETCHED BURST (MTG_GOBLIN_BURST_CLOSER, default on under
            // the resolve axis; =0 disables). The remaining cell of the closer matrix: face_burst
            // covers the THIS-turn kill, lord_closer the next-turn kill via a lord's crowd -- this
            // is the next-turn kill where the fetched card's own direct damage supplies what the
            // board lacks. gi828/gi32 (overnight d3/d5): the deciding lookahead states have the
            // whole board attacking next turn a couple short of lethal and Twinshot Sniper's ETB 2
            // crosses -- but on raw value it ranks 7+, so the W=6 lookahead never sees the line and
            // the root undervalues the Matron plan (gi828's W=6 arm never casts Matron at all).
            // Projection mirrors lord_closer's: every body on board attacks next turn (sickness
            // worn off); the burst is payable off NEXT turn's mana (ETB fires on cast, no haste
            // needed; channel likewise); its own body swings only under an existing haste source.
            // Siege-Gang's cast+sac path is deliberately omitted (mana_next >= 7 territory -- the
            // this-turn face_burst already owns that). Same copies_in_hand gate as lord_closer
            // (a copy in hand closes without spending the fetch), same 5e5 shelf (bigger projected
            // total wins ties), same board-not-already-lethal gate (else the fetch isn't what
            // crosses).
            static const bool burst_closer = EnvOn("MTG_GOBLIN_BURST_CLOSER", TutorAxisResolveEnabled());
            if (burst_closer && sc < 1.0e6 && d != nullptr && copies_in_hand(name) == 0)
            {
                const CardParams& bp = d->params;
                int nburst = 0;
                if (bp.channel_damage > 0 && bp.channel_cost
                    && bp.channel_cost->ManaValue() <= mana_next)
                { nburst = std::max(nburst, bp.channel_damage); }
                const int etb_face2 = std::max(bp.etb_damage_any, bp.etb_damage_each_opponent);
                if (etb_face2 > 0 && c.m_mana_cost.ManaValue() <= mana_next)
                { nburst = std::max(nburst, etb_face2); }
                if (nburst > 0)
                {
                    int total_next = ready_atk + sick_goblin_power + nburst;
                    if (haste_source) { total_next += std::max(0, c.m_power.value_or(0)); }
                    const int board_next2 = ready_atk + sick_goblin_power;
                    if (board_next2 < opp_life && opp_life <= total_next)
                    { sc += 5.0e5 + total_next; }
                }
            }
            // Mode 2 -- "drop it from consideration entirely": sort it below every real
            // candidate rather than removing it, so it stays legal if it is all that is left.
            if (dom_burn_mode == 2 && is_dominated_burn(d) && sc < 1.0e6) { return 1.0; }
            // After the lethal override, so a fetch that wins outright is never discounted.
            if (rank_v2 && dup_hand_penalty && sc < 1.0e6)
            {
                // A flat "castable now" multiplier was measured here and REJECTED (+10.0 turn-units
                // on held-out searched): it rewards every cheap card that happens to be castable,
                // chaff included, which is not the intent. The idea -- prefer what we can deploy THIS
                // turn -- is sound but needs to be proportional to what deploying now actually adds.
                for (int k = copies_in_hand(name); k > 0; --k) { sc *= 0.55; }
            }
            return sc;
        }
        return 0.0;   // whiff placeholder ("") or vanished name -> lowest
    };

    // Scores are PURE per name, so compute each once and sort on the cached key -- byte-identical
    // to calling score_of inside the comparator (stable_sort + identical keys = identical order),
    // and ~8x fewer library scans. This matters because under MTG_TUTOR_AXIS_RESOLVE the ranking
    // runs inside every plan apply that casts a tutor (the price of ranking at the true state),
    // where the comparator's O(n log n) score_of calls -- each a library scan + full value/enabler
    // evaluation -- were eating wall-clock search budget (the goblins-only all-churn asymmetry in
    // the resolve-mode A/B).
    std::map<std::string, double> score_memo;
    for (const std::string& n : cands)
    { if (score_memo.find(n) == score_memo.end()) { score_memo.emplace(n, score_of(n)); } }
    std::stable_sort(cands.begin(), cands.end(),
                     [&](const std::string& a, const std::string& b)
                     { return score_memo.find(a)->second > score_memo.find(b)->second; });
    // --- ROLE CUT: one card per role, best-on-this-board wins ---------------------------------
    // (user, 2026-08-05) "Rundvelt Hordemaster and Piledriver. We should be able to work out which
    // is better on the current board and drop the other." Strict dominance above only removes cards
    // that are worse in EVERY state; this removes the one that is worse in THIS state, which is the
    // other half of "keep cards that are significantly different in utility and drop ones that are
    // similar, but worse in a situation".
    //
    // The decision rule is already in the scores, which is why this is a cut and not new judgement:
    // "an early or hasty piledriver will inevitably be better. The lord wins when the damage add
    // from the turn it is played will be significant. Otherwise the piledriver's +2 effect will
    // easily win out." Piledriver is 2*crowd*BODY*realized where realized is 1.0 with haste
    // (hasty -> inevitably better), falls to 0.0 when the game ends this turn (the lord's immediate
    // damage is all that counts), and sits at 0.5 otherwise -- which is algebraically identical to
    // a +1/+1 lord's 1*crowd*BODY. So at parity they TIE, and every condition the user named breaks
    // the tie the way they described. Taking the higher score is exactly "which is better on the
    // current board".
    //
    // Roles are read off capabilities, not names. Haste and cost-reduction are treated as
    // genuinely different utility and never cut against each other -- Goblin Chieftain must survive
    // ("a haste lord is very strong in a lot of different situations") and so must Goblin Warchief,
    // even though all three carry a lord effect. Toughness is deliberately ignored in this grouping:
    // against a goldfish nothing blocks, so it is not a differentiator.
    // MTG_GOBLIN_ROLE_CUT: 0 = off, 1 = crowd-scaling payoffs, 2 = also the deploy enablers.
    static const int role_cut = EnvInt("MTG_GOBLIN_ROLE_CUT", 1);
    if (role_cut > 0 && cands.size() > 1)
    {
        auto role_of = [&](const std::string& n) -> int
        {
            for (const Card& lc : s.players[controller].library)
            {
                if (lc.m_name != n) { continue; }
                const CardDefinition* d = CardDatabase::Instance().LookupCached(lc);
                if (!d) { return 0; }
                const CardParams& q = d->params;
                if (q.grants_haste || !q.reduces_spell_subtype.empty()) { return 0; }  // unique utility
                if (q.power_bonus > 0 || q.attack_pump_power_per_other_matching > 0) { return 1; }
                if (role_cut >= 2
                    && (q.sac_outlet_add_mana_amount > 0
                        || !q.combat_damage_puts_subtype_from_hand.empty())) { return 2; }
                return 0;
            }
            return 0;
        };
        // Pick the survivor of each role on BOARD CONTRIBUTION (value_of), not on the full score.
        // Comparing totals was wrong and s3003 gi290 is the proof: at that T4 a haste lord is out
        // (haste=1), so by the user's rule "an early or hasty piledriver will inevitably be better"
        // -- and on board value it is, 700 to Rundvelt Hordemaster's 500. But Hordemaster's TOTAL is
        // 800, because it collects +300 of enabler/lord-amplification credit, which is about how
        // much sooner and harder a stuck bomb in HAND arrives. That is a different axis entirely, so
        // letting it settle a role duel cut the right card: the game went T5 -> T6.
        // ... and compare each role on the axis that DEFINES that role. Board value is right for the
        // crowd payoffs, whose job is damage, and useless for the enablers: Goblin Lackey and Skirk
        // Prospector are both 1/1 bodies worth exactly 100, so board value cannot tell them apart and
        // the duel became a coin flip (role_cut=2 measured -3.0 held-out against role_cut=1's -9.0).
        // What separates them is precisely the enabler credit -- Lackey scales with how far out of
        // reach the stuck card is, Skirk with the fodder available to eat -- which is the user's
        // "Lackey better on empty board, Skirk Prospector when there is significant fodder".
        auto role_metric = [&](const std::string& n, int role) -> double
        {
            for (const Card& lc : s.players[controller].library)
            {
                if (lc.m_name != n) { continue; }
                const CardDefinition* d = CardDatabase::Instance().LookupCached(lc);
                if (role == 2) { return enabler_of(d); }
                return value_of(d, d ? d->card : lc);
            }
            return 0.0;
        };
        std::map<int, std::string> role_keeper;      // role -> the name that survives it
        for (const std::string& n : cands)
        {
            const int r = role_of(n);
            if (r == 0) { continue; }
            auto it = role_keeper.find(r);
            if (it == role_keeper.end() || role_metric(n, r) > role_metric(it->second, r))
            { role_keeper[r] = n; }
        }
        // "Sometimes we can drop both" (user). An enabler with nothing to enable is just a 1/1: in
        // 61% of sampled states BOTH Goblin Lackey and Skirk Prospector score enable=0, because
        // nothing in hand is stuck, and there the duel has no signal at all -- which is why cutting
        // to one of them measured WORSE (-3.0 held-out vs -9.0 without the enabler cut). Mode 3
        // drops the whole role when its best member is contributing nothing, instead of keeping one
        // of two identical 1/1 bodies by coin flip.
        std::set<int> role_dropped;
        if (role_cut >= 3)
        {
            for (const auto& kv : role_keeper)
            {
                if (kv.first == 2 && role_metric(kv.second, 2) <= 0.0) { role_dropped.insert(kv.first); }
            }
        }
        std::vector<std::string> kept;
        kept.reserve(cands.size());
        for (const std::string& n : cands)
        {
            const int r = role_of(n);
            if (r != 0 && role_dropped.count(r) > 0) { continue; }   // nothing to enable -> drop all
            if (r != 0 && role_keeper[r] != n) { continue; }         // a better one of this role won
            kept.push_back(n);
        }
        if (!kept.empty()) { cands.swap(kept); }
    }
    // --- RESERVE A SLOT FOR THE BEST RAW CARD -------------------------------------------------
    // The deploy discount is a PRE-SCORER ESTIMATE, and it can hide a bomb from the search entirely
    // rather than merely ranking it low. s3003 gi101 is the worked case. When the search asks at T3
    // "should I hold the Matron and cast it on T4 instead", it evaluates projected T4 states that
    // look like this:
    //
    //    8.  Siege-Gang Commander  score=146.3  value=510.0  x disc=0.287 (t=2)
    //    9.  Muxus, Goblin Grandee score=109.7  value=850.0  x disc=0.129 (t=3)
    //
    // Siege-Gang's RAW value (510) is higher than Goblin Chieftain's (500), the card that tops that
    // list, and Muxus at 850 is the best card in the deck. Both are outside a 6-wide window purely
    // because a {3}{R}{R} bomb reads as two turns away at untapped=3 / next=4. So the whole "hold
    // the Matron" branch is unreachable, the search casts on T3 for the lord, and the game ends T6
    // instead of T5. The width threshold is exactly W=9 -- the rank Siege-Gang occupies there.
    //
    // The discount is not wrong as a RANKING signal; it is wrong as an EXCLUSION. Once the bomb is
    // on the axis the search re-simulates the line and either verifies it or throws it away, which
    // is the same "optimism proposes, the re-sim disposes" pattern that measured load-bearing for
    // Dragonstorm's ritual-afford credit. So reserve the last window slot for the highest RAW-value
    // candidate when the discount has pushed it out, instead of letting an estimate veto it.
    // ON CLAIRVOYANCE, and a claim retracted. The recovered line turns on a card drawn AFTER the
    // decision: T3 hold the Matron, T4 DRAW Goblin Lackey + cast it + Matron fetches Siege-Gang,
    // T5 the Lackey connects and puts Siege-Gang in free. A MTG_SHUFFLE_SALT_SEARCH decouple was run
    // and the edge survived at salts 1-5, which was reported here as "not a clairvoyance artifact".
    // That was WRONG: that instrument re-salts MID-GAME shuffles only (the opening library order
    // comes from MTG_SHUFFLE_SALT_OPENING), so it strips reshuffle clairvoyance -- worth testing,
    // since Goblin Matron has tutor_shuffle_after -- but a normal draw off the pre-shuffled library
    // is identical in search and reality at every search salt. It could not have detected this.
    //
    // The justification is different, and does hold. First, the searched metric is clairvoyant BY
    // CONSTRUCTION -- the search simulates the real library, so every searched decision in this
    // project sees future draws, including the already-shipped W=4 -> 6 widening. Foreknowledge is a
    // uniform baseline, not something this line uniquely exploits. Second, and decisively: the line
    // is not merely unfound, it is UNREACHABLE. At W=6 the game stays T6 at depth 3, 5 AND 6 with
    // unlimited budget, while W=6 + reserve wins T5 at all three. More search cannot evaluate a line
    // whose key card was never put on the axis, so the window is hard-vetoing a valid play and this
    // removes an artificial restriction rather than encoding foreknowledge.
    //
    // THE BINDING DECISION IS THE T2 LAND DROP, not the fetch (traced 2026-08-05, replacing an
    // earlier "the deciding state is unpinned" note here -- and an in-hand-Lackey hypothesis that
    // measured completely inert, for the reason below). Three Tree City taps for {C} in base mode,
    // so playing it T2 leaves {R}{R}{C} on T3 and Goblin Chainwhirler ({R}{R}{R}) uncastable:
    //
    //   W<=8   T2 Three Tree City -> T3 can only cast Matron -> fetch Chieftain            -> T6
    //   W>=9   T2 Mountain        -> T3 Chainwhirler -> T4 Three Tree City + Matron->Siege-Gang
    //                              + Lackey -> T5 Lackey connects, Siege-Gang in FREE, and Three
    //                                Tree City's {2},{T} "add {R} per Goblin" pays 4 sac activations
    //                                for exactly 8                                          -> T5
    //
    // THE BINDING STATE IS THE PRE-LAND-DROP T4 NODE. The lookahead does re-rank at every projected
    // turn, so an earlier draft of this note blaming a T3 state was wrong -- the rank-8 match there
    // was a coincidence. Gating the reserve by turn (MTG_GOBLIN_RESERVE_TURN) recovers gi101 ONLY at
    // turn 4, and gating additionally by mana_next (MTG_GOBLIN_RESERVE_NEXT) ONLY at mana_next=4.
    // T4 is ranked TWICE, because the enumerator evaluates the cast before and after the land drop:
    //
    //   T4 G=1 opp=16 untapped=3 next=4 (3 lands, land STILL IN HAND)  Siege-Gang rank 8  OUTSIDE
    //   T4 G=1 opp=16 untapped=4 next=5 (4 lands, land played)         Siege-Gang rank 4  inside
    //
    // and it binds on the pre-land copy, where mana_next is one short so a {3}{R}{R} bomb prices t=2
    // instead of t=1. Rank 8 is also why the width threshold is exactly W=9 (W=7 and W=8 add only
    // Krenko and Mogg War Marshal and change nothing). Crediting that pending drop is the obvious
    // sharper fix and it DOES recover gi101 with the reserve off -- but it measures +7 to +9 held-out,
    // so it is rejected; see the MTG_GOBLIN_PENDING_LAND table at the mana_next definition.
    //
    // The Lackey that carries the line is DRAWN ON T4, so at the deciding state it is in neither hand
    // nor play but still in the library -- which is why teaching turns_to_deploy to count an in-hand
    // Lackey measured inert. The discount is doing its job on the information it has; the defect is
    // that a fixed window promotes that estimate to a VETO over a search that simulates the draw.
    //
    // THE EVICTION BELOW IS DELIBERATE AND MEASURED -- do not "fix" it. Inserting the k-th rescue at
    // W-1-k shifts the (k-1)-th rescue out of the window, so reserve=N really rescues raw-value ranks
    // 2..N and DROPS rank 1. That is not what the knob's name suggests, but it is the better rule:
    // rank 1 is Muxus (raw 850, MV 6), the one bomb whose "genuinely stuck" discount is usually RIGHT.
    //
    //   reserve=1  rescue Muxus only          gi101 T6   held-out  0.0    (inert)
    //   reserve=2  rescue rank 2 only         gi101 T5   held-out  0.0    <- shipped
    //   reserve=2 + MTG_GOBLIN_VALUE_RESERVE_FIX=1, i.e. keep BOTH ranks 1 and 2:
    //                                         gi101 T5   held-out +20.0   0 better / 20 worse
    //
    // MTG_GOBLIN_VALUE_RESERVE_FIX=1 restores the naive "keep every rescue" semantics (evict the
    // weakest SURVIVOR rather than the previous rescue). Kept default-off with its number.
    //
    // Held-out is EXACTLY 0.0 over 8,000 searched and 12,000 d0 games -- not one file changed -- so
    // the -2.0 on regression (gi101 at d3 and d5) is the only movement in 20,000 games. Adopted as a
    // zero-cost fix to a diagnosed mechanism, NOT as a measured win.
    // MTG_GOBLIN_VALUE_RESERVE=0 disables.
    static const int value_reserve = EnvInt("MTG_GOBLIN_VALUE_RESERVE", 2);
    // DIAGNOSTIC: apply the reserve only at states whose turn number is N (0 = every turn, default).
    // Pins WHICH projected turn's window a width/reserve effect actually binds at -- the ranking is a
    // pure function of state, so a lookahead re-ranks at every projected turn and the deciding state
    // is not necessarily the one being played.
    static const int reserve_turn = EnvInt("MTG_GOBLIN_RESERVE_TURN", 0);
    static const int reserve_next = EnvInt("MTG_GOBLIN_RESERVE_NEXT", 0);
    if (value_reserve > 0 && (reserve_turn == 0 || s.turn_number == reserve_turn)
        && (reserve_next == 0 || mana_next == reserve_next)
        && static_cast<int>(cands.size()) > TutorSearchWidth())
    {
        const int W = TutorSearchWidth();
        std::map<std::string, double> raw_memo;   // pure per name -- same byte-identical memo as the sort
        auto raw_value = [&](const std::string& n) -> double
        {
            auto it = raw_memo.find(n);
            if (it != raw_memo.end()) { return it->second; }
            double v = 0.0;
            for (const Card& lc : s.players[controller].library)
            {
                if (lc.m_name != n) { continue; }
                const CardDefinition* d = CardDatabase::Instance().LookupCached(lc);
                v = value_of(d, d ? d->card : lc);
                break;
            }
            return raw_memo.emplace(n, v).first->second;
        };
        // See the eviction note above: the default path's insert at W-1-k pushes the previous rescue
        // out of the window ON PURPOSE (measured better). MTG_GOBLIN_VALUE_RESERVE_FIX=1 is the
        // rejected naive form -- drop the weakest SURVIVOR and append at W-1 so rescues accumulate,
        // with already-rescued slots excluded from the weakest scan. Held-out +20.0, 0/20.
        static const bool reserve_fix = EnvOn("MTG_GOBLIN_VALUE_RESERVE_FIX", false);
        std::vector<int> kept;                       // window indices already rescued (fix mode)
        for (int k = 0; k < value_reserve; ++k)
        {
            // Best raw-value candidate currently OUTSIDE the window.
            int best = -1;
            for (std::size_t i = W; i < cands.size(); ++i)
            {
                if (best < 0 || raw_value(cands[i]) > raw_value(cands[best])) { best = static_cast<int>(i); }
            }
            if (best < 0) { break; }
            // Only if it actually beats the weakest card already inside on raw value -- otherwise the
            // window is already holding the best cards and there is nothing to rescue.
            int weakest = -1;
            for (int i = 0; i < W; ++i)
            {
                if (reserve_fix
                    && std::find(kept.begin(), kept.end(), i) != kept.end()) { continue; }
                if (weakest < 0 || raw_value(cands[i]) < raw_value(cands[weakest])) { weakest = i; }
            }
            if (weakest < 0 || raw_value(cands[best]) <= raw_value(cands[weakest])) { break; }
            const std::string rescued = cands[best];
            cands.erase(cands.begin() + best);
            if (!reserve_fix)
            {
                cands.insert(cands.begin() + (W - 1 - k), rescued);
                continue;
            }
            cands.erase(cands.begin() + weakest);        // evict the weakest SURVIVOR, not a rescue
            cands.insert(cands.begin() + (W - 1), rescued);
            for (int& idx : kept) { if (idx > weakest) { --idx; } }
            kept.push_back(W - 1);
        }
    }
    if (dump)
    {
        static thread_local std::set<uint64_t> s_seen;
        uint64_t sig = 1469598103934665603ull;
        for (int v : { s.turn_number, G, goblins_sick, ready_atk, opp_life, (int)lackey_now,
                       (int)lackey_persist, (int)skirk_on, vial_charge, untapped_mana, mana_now,
                       mana_next, buff_targets, (int)hand_has_play })
        { sig = (sig ^ (uint64_t)(uint32_t)v) * 1099511628211ull; }
        if (!s_seen.insert(sig).second
            || (int)s_seen.size() > EnvInt("MTG_TUTOR_RANK_DUMP_MAX", 400)) { dump = false; }
    }
    if (dump)
    {
        std::fprintf(stderr,
                     "[tutor-rank] T%d src=%s | G=%d sick=%d ready_atk=%d swing_atk=%d opp_life=%d | lackey_now=%d "
                     "lackey_persist=%d skirk=%d vial=%d | mana: untapped=%d now=%d next=%d | "
                     "buff_targets=%d hand_has_play=%d | haste=%d swings_left=%d skirk_ramp=%d | W=%d\n",
                     s.turn_number, pp.tutor_types.empty() ? "?" : pp.tutor_types[0].c_str(),
                     G, goblins_sick, ready_atk, swing_atk, opp_life, (int)lackey_now, (int)lackey_persist,
                     (int)skirk_on, vial_charge, untapped_mana, mana_now, mana_next,
                     buff_targets, (int)hand_has_play, (int)haste_avail, swings_left, skirk_ramp,
                     TutorSearchWidth());
        for (std::size_t i = 0; i < cands.size(); ++i)
        {
            const Card* lc = nullptr;
            for (const Card& c : s.players[controller].library)
            { if (c.m_name == cands[i]) { lc = &c; break; } }
            if (!lc) { std::fprintf(stderr, "  %2zu. %-28s (gone)\n", i, cands[i].c_str()); continue; }
            const CardDefinition* d = CardDatabase::Instance().LookupCached(*lc);
            const Card& c = d ? d->card : *lc;
            std::fprintf(stderr, "  %2zu.%s %-28s score=%10.1f  value=%7.1f +enable=%7.1f x disc=%.3f (t=%d)  burst=%d\n",
                         i, (int)i < TutorSearchWidth() ? " *" : "  ", cands[i].c_str(),
                         score_of(cands[i]), value_of(d, c), enabler_of(d), discount_of(c),
                         turns_to_deploy(c, /*arriving=*/true), face_burst(d, c));
        }
    }
    if (unpruned) { return GenericProvider::TutorCandidates(s, controller, pp); }
    // DIAGNOSTIC (MTG_TUTOR_FORCE_RANK=k, 1-based; unset = off): return ONLY the k-th ranked
    // candidate, collapsing the tutor axis to that one card. Sweeping k over a game therefore
    // measures the REAL win turn of fetching each ranked candidate -- ground truth for the ranking
    // itself, rather than the usual proxy of "what width does the search need".
    //
    // The question it answers (user, 2026-08-04): "determine how real game win-turns match against
    // our ranking ... W=4 being insufficient means we are quite far off." A ranking is only as good
    // as the position it assigns the card that actually wins soonest, and no aggregate turn-unit
    // delta reports that. This does: rank of the best-outcome candidate, per decision.
    //
    // Every callsite reaches the provider through ResolveProvider(state).TutorCandidates, so the
    // truncation covers the heuristic pick, the search axis and resolution alike. Diagnostic only --
    // it makes play strictly worse whenever k is not the best rank, so never set it in a measured run.
    static const int force_rank = EnvInt("MTG_TUTOR_FORCE_RANK", 0);
    if (force_rank > 0 && static_cast<std::size_t>(force_rank) <= cands.size())
    { return { cands[force_rank - 1] }; }
    // DIAGNOSTIC (MTG_TUTOR_FORCE_CARD="<exact card name>"; unset = off): same collapse, keyed by
    // NAME rather than rank. FORCE_RANK's ground truth is unusable the moment the ranking changes --
    // rank 4 is a different card before and after -- so a table built with it cannot be reused to
    // score a NEW model. Keyed by name it is model-independent: run once per candidate card, record
    // the real win turn, and the resulting table scores any ranking function offline (top-1 accuracy,
    // and whether the true best is inside the window) without re-running a single game.
    // Returns empty (a whiff) when the named card is not among the candidates, so the caller still
    // sees a legal, deterministic decision instead of silently falling back to the heuristic pick.
    static const std::string force_card = []() -> std::string
    {
        const char* v = std::getenv("MTG_TUTOR_FORCE_CARD");
        return v == nullptr ? std::string{} : std::string(v);
    }();
    if (!force_card.empty())
    {
        for (const std::string& c : cands) { if (c == force_card) { return { c }; } }
        return {};
    }
    return cands;
}

// ---- CreatureGivingProvider -------------------------------------------------

// The cleanup shed must never pitch the engine. Defense of the Heart is the deck's whole plan
// (fetch Hunted Phantasm + Wurm; the drain chain follows), and the generic highest-MV rule ranks
// it FIRST when the hand floods -- the probe-retirement classification (2026-08-06, gi564/gi798:
// shedding DotH rolled out a turn worse than shedding ANY other card) is the measurement. Rank
// every DotH copy last among the otherwise-ranked candidates; the spare-copy band and MV rule
// order the rest. Visible information only. docs/design/searched-discard-as-search-node.md.
std::vector<int> CreatureGivingProvider::CleanupDiscardCandidates(
    const GameState& s, const std::vector<std::string>* required_pieces) const
{
    std::vector<int> base = GenericProvider::CleanupDiscardCandidates(s, required_pieces);
    const Player& ap = s.players[s.active_player_index];
    std::vector<int> out, doth;
    for (int i : base)
    {
        if (i >= 0 && i < static_cast<int>(ap.hand.size())
            && ap.hand[i].m_name == "Defense of the Heart") { doth.push_back(i); }
        else { out.push_back(i); }
    }
    out.insert(out.end(), doth.begin(), doth.end());
    return out;
}

std::vector<std::string>
CreatureGivingProvider::TutorCandidates(const GameState& s, int controller, const CardParams& pp) const
{
    // Orchard-first land tutoring (USER-DIRECTED, 2026-08-06; see the class comment). Applies
    // to any tutor whose type filter is Land (Sylvan Scrying, Crop Rotation); every other
    // tutor (Enlightened Tutor's artifact/enchantment search) keeps the Generic full list.
    if (DecisionUnpruned(UnprunedGate::Tutor))
    { return GenericProvider::TutorCandidates(s, controller, pp); }
    bool land_tutor = false;
    for (const std::string& t : pp.tutor_types)
    { if (t == "Land") { land_tutor = true; break; } }
    if (!land_tutor) { return GenericProvider::TutorCandidates(s, controller, pp); }
    for (const Card& lc : s.players[controller].library)
    { if (lc.m_name.str() == "Forbidden Orchard") { return { "Forbidden Orchard" }; } }
    // No Orchard left: the full list returns and the search picks (mana fixing is board-
    // dependent -- exactly the depth a static fallback ranking lacks).
    return GenericProvider::TutorCandidates(s, controller, pp);
}

// ---- FiveColourProvider -----------------------------------------------------
//
// Fetch policy for the 5-colour domain shell (USER-DIRECTED 2026-08-07; see the class comment):
// get the colours that let us cast our EARLY ACCELERATION (and then anything else) first, spread
// the five colours over DIFFERENT sources, and once every colour is covered start building toward
// TWO sources of each.
//
// Implemented as a strict lexicographic key so the intent stays readable and the ordering is
// total + deterministic (no float weights to re-tune). Per candidate land, in priority order:
//
//   1. accel_new  -- colours it adds that we have NO source for and an ACCELERANT in hand wants.
//                    A turn-2 Bloom Tender / Faeburrow Elder compounds; missing its pip is the
//                    single most expensive fixing failure in the deck.
//   2. spell_new  -- same, for any other castable-cost card in hand.
//   3. breadth    -- colours it adds that we have no source for at all (the "all 5 spread out"
//                    half of the directive), regardless of what is in hand right now.
//   4. untapped   -- tiebreak that only bites while some wanted colour is still uncovered: a
//                    triome enters TAPPED and cannot pay for anything this turn, so between two
//                    otherwise-equal picks take the one that can actually be spent now.
//   5. depth      -- colours it takes from exactly one source toward two (the second half of the
//                    directive), double-counted for a colour the hand wants more than once.
//   6. colours    -- raw colour count (triome > shock > basic) as a generic flexibility tiebreak.
//   7. name       -- determinism.
//
// Returns the FULL ordered list rather than a single pick: the engine's FetchSearchCap (2) decides
// how many the search actually branches on, so the policy stays a ranking and the search keeps its
// say. MTG_UNPRUNED opens the whole list, as everywhere else.
// Hold a live UTILITY mana source out of combat when its TAP is worth more than its chip damage
// (USER-REPORTED 2026-08-09: "Deathrite only very rarely wants to attack, because it has abilities
// which are more useful"). The reported misplay: Deathrite Shaman (a 1/2) swung for 1, which tapped
// it, and the post-combat Unite the Coalition {2}{W}{U}{B}{R}{G} then went uncastable -- Deathrite's
// "add one mana of any color" was the board's ONLY white source, so 1 damage cost a 7-mana spell.
//
// The rule is deliberately narrow, and the two guards are what keep it from ever losing damage:
//   * VIGILANCE is exempt. Faeburrow Elder attacks AND still taps for mana afterwards, so holding
//     it back would be pure loss -- which is exactly why the user singled it out as always wanting
//     to attack and then cast in the second main.
//   * LETHAL is exempt. If the eligible attackers together already kill, everything swings; a
//     held-back dork can never cost a kill this turn.
// Everything else about combat stays the generic goldfish behaviour. Gated to this archetype's
// provider (the root stays generic per the search-primary bar) and behind MTG_NO_5C_HOLD_DORK for
// the standing A/B.
bool FiveColourProvider::ShouldAttackWith(const GameState& s, const Permanent& p) const
{
    static const bool enabled = !EnvOn("MTG_NO_5C_HOLD_DORK");
    if (!enabled) { return GenericProvider::ShouldAttackWith(s, p); }

    const CardDefinition* d = CardDatabase::Instance().LookupCached(p.card);
    if (!d) { return true; }

    // Is this a live utility source -- something whose {T} we would rather spend on mana or on one
    // of its activated abilities than on combat damage?
    const bool taps_for_mana = (d->tmpl == CardTemplate::ManaDork) && !d->params.produces.empty();
    const bool gy_abilities  = d->params.gy_land_exile_mana
                            || d->params.gy_exile_instant_sorcery_drain > 0
                            || d->params.gy_exile_creature_lifegain > 0;
    if (!taps_for_mana && !gy_abilities) { return true; }        // a real attacker -> swing

    // Vigilance: attacking costs it nothing, so never hold it back.
    if (p.card.HasKeyword(Keyword::Vigilance)) { return true; }
    if (AttackHasNonPowerValue(s, p))          { return true; }  // attack-trigger value

    // Never forfeit a kill: if everything that CAN attack already adds up to lethal, swing with all
    // of it. Eligibility uses CanAttackFull (not ShouldAttackWith) to avoid recursion.
    const int active   = s.active_player_index;
    const int opp_life = s.players[1 - active].life;
    int total = 0;
    for (const Permanent& q : s.battlefield)
    {
        if (q.controller_index != active) { continue; }
        if (!CanAttackFull(q, s.battlefield, active)) { continue; }
        total += AttackPowerOf(s, q);
    }
    if (total >= opp_life) { return true; }

    // Hold whenever the mana can still BUY something this turn. The bar is deliberately low, per the
    // user: "Deathrite only very rarely wants to attack, because it has abilities which are more
    // useful" -- one chip damage is worth far less than a mana of any colour in a 5-colour deck.
    //
    // The previous, tighter test asked "is some ONE hand card payable WITH this source and unpayable
    // WITHOUT it?" and was reported as inconsistent, correctly: it judges each card ALONE against the
    // full pool, so it holds for a single expensive card but swings whenever the second main's plan
    // needs the extra mana spread across SEVERAL casts, or when the card is payable without this
    // source yet its mana is still wanted for the rest of the line. Both are exactly the case the
    // player hits by passing main 1 and casting after combat.
    //
    // So: hold if any nonland card in hand is castable off the full pool. The measured loss case that
    // motivated the tighter test is still covered -- with nothing castable the tap buys nothing, so it
    // swings (verified: empty hand -> Deathrite attacks, opponent 19 not 20).
    // A LIVE graveyard ability beats one chip damage outright (Deathrite's drain-2 / gain-2 need a
    // matching card in the graveyard to be worth anything; with none, the ability is dead).
    if (gy_abilities)
    {
        for (const Card& g : s.players[active].graveyard)
        {
            const CardDefinition* gd = CardDatabase::Instance().LookupCached(g);
            if (!gd) { continue; }
            if (d->params.gy_exile_instant_sorcery_drain > 0
                && (gd->card.IsInstant() || gd->card.IsSorcery()))            { return false; }
            if (d->params.gy_exile_creature_lifegain > 0 && gd->card.IsCreature()) { return false; }
        }
    }

    // Does tapping it actually PRODUCE mana right now? Deathrite's mana ability is fuel-gated on a
    // land in the graveyard, so an unfuelled one makes nothing and holding it buys nothing -- it
    // should take the chip damage. AvailableManaPool already models the fuel, so the difference the
    // source makes to the pool IS the test.
    const ManaPool with    = AvailableManaPool(s);
    const ManaPool without = AvailableManaPool(s, &p);
    if (with.Total() <= without.Total()) { return true; }   // contributes no mana -> swing

    for (const Card& h : s.players[active].hand)
    {
        const CardDefinition* hd = CardDatabase::Instance().LookupCached(h);
        if (!hd || hd->card.IsLand()) { continue; }
        const ManaCost& mc = hd->card.m_mana_cost;
        // {X} spells are always "castable" for X=0, so they count: the tap raises X, which is the
        // whole point of holding for them.
        if (mc.has_x || with.CanPay(mc)) { return false; }   // the mana has somewhere to go -> hold
    }
    return true;   // nothing castable at all -> the tap buys nothing, take the chip damage
}

std::vector<std::string>
FiveColourProvider::FetchCandidates(const GameState& s, int controller,
                                    const CardParams& fetch_pp) const
{
    std::vector<std::string> all = GenericProvider::FetchCandidates(s, controller, fetch_pp);
    if (all.size() < 2 || DecisionUnpruned(UnprunedGate::Fetch)) { return all; }

    constexpr int NC = 6;                    // W,U,B,R,G,C
    std::array<int, NC> src_cnt{};           // distinct sources that already make each colour
    auto count = [&](const std::vector<Color>& prod)
    { for (Color c : prod) { ++src_cnt[static_cast<int>(c)]; } };

    // Sources we control. EffectiveProduces (not the static `produces` hint) so a domain source
    // contributes the colours it ACTUALLY makes right now, and Deathrite only counts while its
    // graveyard-land fuel is live.
    for (const Permanent& p : s.battlefield)
    {
        if (p.controller_index != controller) { continue; }
        const CardDefinition* d = CardDatabase::Instance().LookupCached(p.card);
        if (!d) { continue; }
        if (!(d->card.IsLand() || d->tmpl == CardTemplate::ManaDork || d->params.mana_rock)) { continue; }
        if (!GraveyardFuelLive(s, controller, *d)) { continue; }
        count(EffectiveProduces(s, controller, *d));
    }
    // Plus non-fetch lands already in hand (a land we are about to play is a near-future source,
    // so fetching to re-cover a colour it already brings is wasted fixing).
    const Player& ap = s.players[controller];
    for (const Card& c : ap.hand)
    {
        const CardDefinition* d = CardDatabase::Instance().LookupCached(c);
        if (d && d->card.IsLand() && d->params.fetch_land_types.empty()) { count(d->params.produces); }
    }

    // What the hand wants to cast, split into accelerants (mana dorks/rocks -- the compounding
    // early plays) and everything else. `want` counts PIPS, so a {W}{W} cost asks for two white
    // sources and feeds the depth term below.
    std::array<int, NC> want{}, accel_want{};
    for (const Card& c : ap.hand)
    {
        const CardDefinition* d = CardDatabase::Instance().LookupCached(c);
        if (!d || d->card.IsLand()) { continue; }
        const bool accel = (d->tmpl == CardTemplate::ManaDork) || d->params.mana_rock;
        const ManaCost& mc = d->card.m_mana_cost;
        const int pips[NC] = { mc.white, mc.blue, mc.black, mc.red, mc.green, 0 };
        for (int i = 0; i < NC; ++i)
        {
            if (pips[i] <= 0) { continue; }
            want[i] = std::max(want[i], pips[i]);
            if (accel) { accel_want[i] = std::max(accel_want[i], pips[i]); }
        }
    }
    bool any_uncovered_want = false;
    for (int i = 0; i < NC; ++i) { if (want[i] > 0 && src_cnt[i] == 0) { any_uncovered_want = true; } }

    struct Key { int accel_new, spell_new, breadth, untapped, depth, colours; std::string name; };
    std::vector<Key> keys;
    keys.reserve(all.size());
    for (const std::string& nm : all)
    {
        const CardDefinition* d = CardDatabase::Instance().Lookup(nm);
        if (!d) { keys.push_back({ 0, 0, 0, 0, 0, 0, nm }); continue; }
        Key k{ 0, 0, 0, 0, 0, 0, nm };
        for (Color col : d->params.produces)
        {
            const int i = static_cast<int>(col);
            if (i < 0 || i >= NC) { continue; }
            ++k.colours;
            if (src_cnt[i] == 0)
            {
                ++k.breadth;
                if (accel_want[i] > 0) { ++k.accel_new; }
                else if (want[i] > 0)  { ++k.spell_new; }
            }
            else if (src_cnt[i] == 1)
            {
                k.depth += (want[i] >= 2) ? 2 : 1;   // a second source, wanted twice over
            }
        }
        // Only a land that can enter UNTAPPED can pay for something this turn. A shock pays 2 life
        // for the privilege, which the engine's own entry choice already weighs; here it is just a
        // tiebreak, and only while a wanted colour is still missing.
        k.untapped = (any_uncovered_want && !d->params.enters_tapped) ? 1 : 0;
        keys.push_back(std::move(k));
    }

    std::stable_sort(keys.begin(), keys.end(), [](const Key& a, const Key& b)
    {
        if (a.accel_new != b.accel_new) { return a.accel_new > b.accel_new; }
        if (a.spell_new != b.spell_new) { return a.spell_new > b.spell_new; }
        if (a.breadth   != b.breadth)   { return a.breadth   > b.breadth;   }
        if (a.untapped  != b.untapped)  { return a.untapped  > b.untapped;  }
        if (a.depth     != b.depth)     { return a.depth     > b.depth;     }
        if (a.colours   != b.colours)   { return a.colours   > b.colours;   }
        return a.name < b.name;
    });

    std::vector<std::string> out;
    out.reserve(keys.size());
    for (const Key& k : keys) { out.push_back(k.name); }
    return out;
}

// ---- instances + selection --------------------------------------------------

namespace
{
    // Stateless, read-only -> single shared const instances are thread-safe (same model as
    // CardDatabase). Process lifetime, so GameState's raw pointer stays valid.
    const GenericProvider        g_generic;
    const AntiLifegainProvider   g_antilife;
    const TreasureHuntProvider   g_treasure;
    const VialProvider           g_vial;
    const HinataProvider         g_hinata;
    const BurnProvider           g_burn;
    const DragonstormProvider    g_dragonstorm;
    const GoblinsProvider        g_goblins;
    const CreatureGivingProvider g_creature_giving;
    const FiveColourProvider     g_fivecolour;
}

const DecisionProvider& DefaultProvider()
{
    return g_generic;
}

const DecisionProvider& SelectDecisionProvider(const Decklist& deck)
{
    // Archetype detection by card params (same shape as GoldFishRunner::DeckUsesSecondMain).
    // Order matters only if a deck mixed signatures; today each is exclusive (verified).
    bool anti = false, th = false, vial = false, hinata = false, burn = false, dragonstorm = false;
    bool goblin = false, gift = false, fivec = false;
    for (const Card& c : deck.mainboard)
    {
        const CardDefinition* def = CardDatabase::Instance().LookupCached(c);
        if (!def) { continue; }
        const CardParams& p = def->params;

        // Goblins archetype: any Goblin-specific gated param marks the deck. Goblin Matron carries
        // tutor_to_hand, which would otherwise trip the anti-lifegain signature below and MISROUTE
        // the whole deck to AntiLifegainProvider. Detecting a Goblin signature here lets us route
        // the deck to GenericProvider before the anti check (see the goblin return below). Every
        // one of these fields is new + gated (0/false/empty inert), so ONLY a deck carrying the new
        // Goblin params sets this -- all existing decks are byte-identical.
        if (p.sac_creature_outlet
            || !p.tap_creates_tokens_per_controlled_subtype.empty()
            || !p.reduces_spell_subtype.empty()
            || !p.dies_watch_subtype.empty()
            || !p.combat_damage_puts_subtype_from_hand.empty()
            || p.etb_self_creates_tokens > 0)
        {
            goblin = true;
        }

        // Dragonstorm (Storm ritual-storm): the tutor-to-battlefield put IS the archetype signature
        // (a {8}{R} that puts a wave of Dragons in). Owns the put-order / selection heuristic.
        if (p.tutor_to_battlefield) { dragonstorm = true; }

        // Hinata, Dawn-Crowned's cost-reduction static is the deck's defining signature.
        if (p.hinata_cost_reducer) { hinata = true; }

        // Mono-red Burn: Searing Blaze's landfall damage is unique to this deck; it drives the
        // land-banking heuristic (bank spare lands for a future Blaze's landfall).
        if (p.landfall_damage > 0) { burn = true; }

        // FiveColour (5-colour domain goodstuff): the dynamic domain source (Faeburrow Elder /
        // Bloom Tender, "one mana of each colour among permanents you control") is the archetype
        // signature -- no other suite deck carries domain_mana. Detected BEFORE the anti check for
        // the same reason Goblins and Creature Giving are: this deck's eleven fetchlands set
        // `anti` on their own, which silently routed the whole deck to AntiLifegainProvider and
        // ranked its fetches with a 4-colour anti-lifegain shell's tiebreaks. See the fivec return
        // below and docs/design/fivecolour-search-cost.md section 6.
        if (p.domain_mana) { fivec = true; }

        if (p.lifegain_to_loss || p.verse_damage || p.alt_lifegain_cost > 0
            || p.tutor_to_hand || p.tutor_to_top || !p.fetch_land_types.empty())
        {
            anti = true;
        }
        // Treasure Hunt archetype = the actual flood-combo ENGINE only: Treasure Hunt itself
        // (DrawUntilNonland) or Land's Edge (discard_land_damage). A generic sac-to-draw / cycling /
        // scry-surveil land (Horizon Canopy, Lonely Sandbar, a Temple) is NOT the archetype -- those
        // appear in aggro/other decks (e.g. the Auras/Bogles deck's Horizon Canopy) that must ride
        // GenericProvider, so they no longer trip TH detection. Verified by deck scan (2026-07-22):
        // among all suite decks, only treasure_hunt carries the engine (it has BOTH signals); every
        // other deck that matched the old broad signature (only Auras, via sacrifice_draw_cost) should
        // be generic. treasure_hunt still routes to g_treasure -> byte-identical.
        if (p.discard_land_damage > 0 || def->tmpl == CardTemplate::DrawUntilNonland)
        {
            th = true;
        }
        if (p.upkeep_adds_charge) { vial = true; }

        // Creature Giving (gift-the-opponent drain): any of its gated params marks the deck. Its
        // Sylvan Scrying (tutor_to_hand) + fetchlands would otherwise trip the anti-lifegain
        // signature below and misroute the whole deck to AntiLifegainProvider (whose tutor
        // heuristic hunts lifegain_to_loss enablers this deck does not run). Routed to
        // CreatureGivingProvider (Orchard-first land tutoring, user-directed); the Defense of
        // the Heart SacTutorPutList default lives in the DecisionProvider root and is inherited.
        if (p.opp_creature_enters_life_loss > 0 || p.etb_opp_creates_tokens > 0
            || p.upkeep_sac_tutor_creatures > 0 || p.cumulative_upkeep_opp_token
            || p.etb_opp_creatures_debuff > 0  || p.opp_dies_life_loss > 0)
        {
            gift = true;
        }
    }

    if (dragonstorm) { return g_dragonstorm; }
    if (hinata) { return g_hinata; }
    // Goblins ride GoblinsProvider. This return WINS OVER anti (Goblin Matron's tutor_to_hand would
    // otherwise set anti and misroute the deck to AntiLifegainProvider) and over th/vial/burn/generic.
    // It sits below dragonstorm/hinata only for tidiness -- a Goblins deck carries none of those
    // signatures (no tutor_to_battlefield / hinata_cost_reducer), so exclusivity is preserved: this
    // branch fires iff the deck has a Goblin gated param, which no other suite deck does.
    //
    // Was g_generic until GoblinsProvider had MEASURED hooks to hold: the sac-outlet deferral and the
    // Matron tutor width (12, -0.0620 held-out). It derives from GenericProvider and overrides only
    // those, so every other decision still resolves through exactly the code this deck used before.
    if (goblin) { return g_goblins; }
    // Creature Giving; must WIN OVER anti (see the gift detection note above).
    if (gift) { return g_creature_giving; }
    // FiveColour; must WIN OVER anti (its fetchlands set that signature on their own -- see the
    // domain_mana detection above). No other deck has domain_mana, so exclusivity is preserved.
    if (fivec) { return g_fivecolour; }
    if (anti) { return g_antilife; }
    if (th)   { return g_treasure; }
    if (vial) { return g_vial; }
    if (burn) { return g_burn; }
    return g_generic;
}

// ---- FiveColourProvider::ModalSplitCandidates -------------------------------
//
// Unite the Coalition, {2}{W}{U}{B}{R}{G}: "choose five, repeats allowed", modelled as
// S x (2 damage to the face) + (5-S) x (draw a card). The solver branched on all six splits, which
// measured as a x7 group factor -- the single largest branching source on this deck -- for six
// same-cost lines that differ only in a damage-vs-draw trade. Same shape as Ponder's variants, and
// the same fix: decide the split instead of searching it.
//
// The rule is the user's (2026-08-09): "you want to hit them for 10 when we will have lethal on
// board this turn or next. If we have at least 2 other castable creature threats in hand I would
// also hit them for full damage. Otherwise, we should draw 3-5 cards and deal damage with any left
// over" -- with the explicit caveat "sometimes the damage can hit breakpoints like multiples of 5,
// so I don't want to restrict this decision too much", which is why the draw case keeps all three
// of S=0,1,2 rather than committing to one. Six variants become one or three.
void FiveColourProvider::ModalSplitCandidates(const GameState& s, const CardDefinition& def,
                                              std::vector<int>& out) const
{
    const int N = def.params.modal_choose_n;
    if (N <= 0) { return; }
    if (DecisionUnpruned(UnprunedGate::Fetch) || !EnvOn("MTG_FIVEC_UNITE_SPLIT", true))
    {
        GenericProvider::ModalSplitCandidates(s, def, out);
        return;
    }

    const int me  = s.active_player_index;
    const int opp = 1 - me;
    const int face_damage = N * def.params.modal_damage_per_choice;   // all-in: 10

    // Board damage we could add on top, counting creatures that can attack this turn or next --
    // summoning sickness wears off, so a creature that entered this turn still counts for "next".
    // HONEST LETHAL REACH -- what this turn's ATTACK can actually deal, not what the board prints.
    // Same lesson as GoblinsProvider::TutorCandidates' honest-power read, which exists because a
    // printed-power scan halved a lord-heavy board and its this-turn-lethal test never fired. Here
    // the gap was bigger still: on s6006 gi209 the real turn-4 swing was 11 and this read said 4,
    // so the all-in was declined on a turn it was exactly lethal (11 + 10 vs 20 life). Everything
    // missing lived in the PLAN rather than on the battlefield:
    //   Kavu 3/3 -> 8/8   (Jared's -3, +1/+1 counters equal to each target's colour count)
    //   Deathrite -> 3    (+2/+2 from the same -3, and attacking at all only because Lightning
    //                      Greaves was cast that same turn and granted haste)
    // So the three terms below are: honest current power, the pump a planeswalker can activate
    // this turn, and the bodies haste lets us deploy and swing with.
    int board_power = 0;
    std::vector<int> colors_desc;      // colour counts of my creatures -- the counter-pump magnitude
    for (const Permanent& p : s.battlefield)
    {
        if (p.controller_index != me || !p.card.IsCreature()) { continue; }
        const int lp = ComputeLordBonus(p.card, s.battlefield, me, p.is_animated, &p).first;
        board_power += std::max(0, p.EffectivePower() + lp);
        colors_desc.push_back(p.card.ColorCount());
    }
    // Planeswalker counter-pump available THIS turn (loyalty abilities have no summoning sickness).
    // "+1/+1 counters equal to the number of colours it is, on up to N creatures" -> take the N
    // most colourful bodies we control. Jared's -3 on a 5-colour Kavu is +5 by itself.
    std::sort(colors_desc.begin(), colors_desc.end(), std::greater<int>());
    for (const Permanent& p : s.battlefield)
    {
        if (p.controller_index != me || !p.card.HasType(CardType::Planeswalker)) { continue; }
        const CardDefinition* pd = CardDatabase::Instance().LookupCached(p.card);
        if (!pd) { continue; }
        for (const auto& la : pd->params.loyalty_abilities)
        {
            if (la.effect != "counters_up_to_two" || p.loyalty + la.delta < 0) { continue; }
            for (int k = 0; k < la.amount && k < static_cast<int>(colors_desc.size()); ++k)
            { board_power += colors_desc[k]; }
        }
    }

    // Other castable creature threats in hand: a body we could actually deploy, not just hold.
    // A haste enabler counts whether it is already down OR castable from hand -- the question is
    // about OUR PLAN this turn, the same reading GoblinsProvider::DeferSacOutletPreCombat takes of
    // grants_haste ("or one is castable from hand this turn"). Without it this test read only the
    // creatures already on the battlefield and missed lethal assembled in the same main phase:
    // measured on seed 6006 gi209, the search cast Deathrite + Unite + Lightning Greaves on turn 4
    // and won there, while this heuristic saw 2 power on board, declined the all-in split, and won
    // on turn 5 instead.
    int threats = 0, hand_power = 0;
    bool haste_enabler = false;
    const ManaPool pool = AvailableManaPool(s);
    for (const Permanent& p : s.battlefield)
    {
        if (p.controller_index != me) { continue; }
        const CardDefinition* d = CardDatabase::Instance().LookupCached(p.card);
        if (d && d->params.grants_haste) { haste_enabler = true; break; }
    }
    for (const Card& c : s.players[me].hand)
    {
        const CardDefinition* d = CardDatabase::Instance().LookupCached(c);
        if (!d) { continue; }
        if (d->params.grants_haste && pool.CanPay(EffectiveSpellCost(*d, s))) { haste_enabler = true; }
        if (!d->card.IsCreature()) { continue; }
        if (pool.CanPay(EffectiveSpellCost(*d, s))) { ++threats; hand_power += std::max(0, d->card.m_power.value_or(0)); }
    }
    // With haste available, a body deployed this turn can attack, so it counts toward lethal NOW.
    if (haste_enabler) { board_power += hand_power; }

    // Lethal on board this turn or next, once Unite's 10 is added; or two more threats to deploy.
    const bool go_face = (s.players[opp].life <= face_damage + board_power) || (threats >= 2);
    if (go_face) { out.push_back(N); return; }

    // Otherwise draw 3-5 and spend the remainder on damage: S = 0, 1, 2 -- PLUS the all-in split,
    // always. This hook is consulted at EVERY search node, including hypothetical future turns, so
    // dropping S=N does not merely decline it now: it deletes the all-in finish from the search's
    // model of the future, and lines whose value is SETTING UP that finish stop evaluating as
    // lethal. Measured on s6006 gi209: without S=N here the arms diverged on TURN 3 -- the search
    // played Jared Carthalion (-> Kavu token -> swing 11 with the all-in on turn 4, win t4), the
    // narrowed arm could not see that finish, played Deathrite + Greaves instead, and won on t5.
    // The dominated MIDDLE (S=3,4) is what is safe to cut; the extremes carry the deck's reach.
    for (int k = 0; k <= 2 && k <= N; ++k) { out.push_back(k); }
    // A/B lever: MTG_FIVEC_UNITE_ALLIN=0 drops the all-in from the non-lethal case, which is the
    // arm that deletes the finish from the lookahead (see above). Default ON.
    if (EnvOn("MTG_FIVEC_UNITE_ALLIN", true)) { out.push_back(N); }
}
