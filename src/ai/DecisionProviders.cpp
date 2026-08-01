#include "../core/EnvFlags.h"
#include <cstdlib>
#include <cctype>
#include <utility>
#include <atomic>
#include <cstdint>
#include <algorithm>   // std::stable_sort (OrderEntriesByEtbValue payoff-ordering primitive)
#include <tuple>       // std::make_tuple (CombatCheatCandidates ranking key)
#include "DecisionProviders.h"

#include "../core/SpellEffects.h"   // shared rules helpers + the archetype heuristic free fns
#include "../deck/DeckLoader.h"     // Decklist
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
// Returns the historical SINGLE pick, so this port is byte-identical: every caller takes index 0,
// which is exactly the index SelectCleanupDiscardIndex returned before the hook existed. Widening
// it to several candidates is what turns the discard into a search branch.
std::vector<int> DecisionProvider::CleanupDiscardCandidates(
    const GameState& s, const std::vector<std::string>* required_pieces) const
{
    const int idx = SelectCleanupDiscardIndex(s, required_pieces);
    if (idx < 0) { return {}; }
    return std::vector<int>{ idx };
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
    std::vector<int> out = legal;
    // Original scan: higher contrib wins; on equal contrib the higher MV wins; both comparisons are
    // strict, so the FIRST library index wins any remaining tie -- which stable_sort preserves.
    std::stable_sort(out.begin(), out.end(), [&](int a, int b) {
        const int ca = contrib(a), cb = contrib(b);
        if (ca != cb) { return ca > cb; }
        return mv(a) > mv(b);
    });
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

int GenericProvider::CastOrderRank(const GameState&, const CardDefinition& def) const
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
        const bool is_dork = (d->tmpl == CardTemplate::ManaDork && p.CanTap()) || d->params.mana_rock;
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
        bool is_dork = (d->tmpl == CardTemplate::ManaDork && p.CanTap()) || d->params.mana_rock;
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

// Defer the creature-sac VALUE outlets (Siege-Gang damage / Pashalik tokens / the multi-sac burst) out
// of the pre-combat enumeration, and haste-gate Skirk's sac-for-mana. Off-switch MTG_NO_GOBLIN_SAC_2ND
// (ADOPTED default-ON 2026-07-31). See DeferSacOutletPreCombat in DecisionProvider.h and analysis-goblins.md.
bool GoblinsProvider::DeferSacOutletPreCombat(const GameState& s, const Permanent& src,
                                              bool is_mana_outlet) const
{
    static const bool on = !EnvOn("MTG_NO_GOBLIN_SAC_2ND");
    if (!on) { return false; }
    if (!is_mana_outlet) { return true; }   // value outlets: always defer to the second main
    // Skirk MANA outlet: keep it pre-combat only if a Goblin haste lord is on the battlefield (src is a
    // Goblin, so HasHasteFromLords answers this) OR one is castable from hand this turn ("our plan").
    // Without haste its float is second-main-recoverable, and Skirk-for-mana is the dominant pre-combat
    // branch amplifier on a wide board -- so defer it too when no haste enabler is available.
    const int active = s.active_player_index;
    if (HasHasteFromLords(src.card, s.battlefield, active)) { return false; }
    for (const Card& h : s.players[active].hand)
    {
        const CardDefinition* hd = CardDatabase::Instance().LookupCached(h);
        if (hd && hd->params.grants_haste) { return false; }
    }
    return true;   // no haste enabler in play or hand -> defer Skirk to the second main
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
    const DragonstormProvider  g_dragonstorm;
    const GoblinsProvider      g_goblins;
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
    bool goblin = false;
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
    }

    if (dragonstorm) { return g_dragonstorm; }
    if (hinata) { return g_hinata; }
    // Goblins ride GoblinsProvider. This return WINS OVER anti (Goblin Matron's tutor_to_hand would
    // otherwise set anti and misroute the deck to AntiLifegainProvider) and over th/vial/burn/generic.
    // It sits below dragonstorm/hinata only for tidiness -- a Goblins deck carries none of those
    // signatures (no tutor_to_battlefield / hinata_cost_reducer), so exclusivity is preserved: this
    // branch fires iff the deck has a Goblin gated param, which no other suite deck does. GoblinsProvider
    // only overrides DeferSacOutletPreCombat (all other hooks inherit Generic verbatim), so this is
    // byte-identical to the old g_generic routing EXCEPT for the adopted sac-outlet deferral heuristic.
    if (goblin) { return g_goblins; }
    if (anti) { return g_antilife; }
    if (th)   { return g_treasure; }
    if (vial) { return g_vial; }
    if (burn) { return g_burn; }
    return g_generic;
}
