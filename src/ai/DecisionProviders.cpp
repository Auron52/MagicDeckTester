#include <array>
#include <map>
#include "HeuristicArm.h"
#include "ValueArm.h"
#include "../core/EnvFlags.h"
#include <iostream>
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
    {"tricktarget",UnprunedGate::TrickTarget},
    {"treasuretrickcast",UnprunedGate::TreasureTrickCast},
    {"equiphost",  UnprunedGate::EquipHost},
    {"jittemode",  UnprunedGate::JitteMode},
    {"uacast",     UnprunedGate::UACast},
    {"tapreserve", UnprunedGate::TapReserve},
    {"mainphase",  UnprunedGate::MainPhase},
    {"terak",      UnprunedGate::TeraK},
    {"replicate",  UnprunedGate::Replicate},
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

// Gates HUMAN PLAY must not open, even under MTG_UNPRUNED. The general rule for un-pruning in the
// play viewer is "the human, not a heuristic, owns the decision" -- but that only applies to a gate
// whose decision the human actually makes THROUGH THE PLAN. TrickTarget is not one: a solo-target
// trick's target is re-asked at RESOLUTION off the board (ResolveSoloTargetTrick's chooser offers
// EVERY own creature, deliberately ignoring TrickTargetCandidates), so opening the gate adds nothing
// the human can pick -- it only fans one enumerated plan per legal target, which
//   (a) re-asks the same target as a redundant "choose the sub-decision" dialog BEFORE the board
//       click the human actually uses (viewer issue #2), and
//   (b) multiplies the plan odometer on exactly the boards this deck builds: measured on
//       Mirrorwing s22 gi21 T4, one committed segment went 30ms -> 2.1s -> 21s -> 31s -> past the
//       viewer's 120s step timeout ("the game timed out", viewer issues #4/#8/#11). With the gate
//       pruned the same eleven segments run 33-74ms each.
// MTG_HUMAN_TRICK_UNPRUNE=1 restores the old behaviour (the definitive with/without A/B still runs
// autonomously, where MTG_HUMAN_PLAY is unset and this exemption is inert).
static bool UnpruneHumanExempt(UnprunedGate g)
{
    static const bool hp      = EnvSet("MTG_HUMAN_PLAY");
    static const bool restore = EnvOn("MTG_HUMAN_TRICK_UNPRUNE");
    if (!hp || restore) { return false; }
    return g == UnprunedGate::TrickTarget;
}

bool DecisionUnpruned(UnprunedGate g)
{
    // Gate probe: record that this gate has a REACHABLE callsite for the current deck (see the probe
    // comment above). Cheap relaxed OR, only when probing; normal runs pay one predictable branch.
    if (g_gate_probe.load(std::memory_order_relaxed))
    { g_gates_queried.fetch_or(1u << static_cast<int>(g), std::memory_order_relaxed); }
    if (UnpruneHumanExempt(g)) { return false; }     // human play never opens this gate (see above)
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
    // Per-job override first (see ValueArm.h): a pooled batch carries H and V jobs in ONE process,
    // so the arm cannot come from a process-wide static. Unset (-1) => the env default below, which
    // keeps every non-batch path byte-identical.
    if (valuearm::t_arm.value_model >= 0) { return valuearm::t_arm.value_model != 0; }
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
        if (!CardHasColorNamed(card, pp.tutor_color)) { type_ok = false; }   // Natural Order: green only
        if (type_ok && seen.insert(lc.m_name).second) { all.push_back(lc.m_name); }
    }
    // RANKED DEFAULT for put-onto-battlefield tutors (MTG_TUTOR_RANKED_DEFAULT, default off --
    // A/B lever, st993): the tutor-axis width prune and the tc=-1 base pick both assume this
    // list is BEST-FIRST ("the provider orders candidates best-first" -- TutorAxisWidth), but
    // library order is SHUFFLE order. st993's T4 Natural Order drew a library where Craterhoof
    // Behemoth sat 6th of 11 distinct names under width 5: neither the greedy default (which
    // fetched Llanowar Elves) nor any searched variant could reach the deck's win condition, so
    // no projection anywhere priced the T4 kill. For a tutor that CHEATS a card onto the
    // battlefield, mana value is the value being cheated -- rank MV descending (name ascending
    // for a total, shuffle-independent order). The searched variants then simulate the real
    // outcomes; the ranking only has to put the contenders inside the width, not pick the winner.
    static const bool s_ranked = EnvOn("MTG_TUTOR_RANKED_DEFAULT");
    if (s_ranked && pp.tutor_to_battlefield_single)
    {
        std::stable_sort(all.begin(), all.end(),
            [](const std::string& a, const std::string& b)
            {
                const CardDefinition* da = CardDatabase::Instance().Lookup(a);
                const CardDefinition* db = CardDatabase::Instance().Lookup(b);
                const int ma = da ? da->card.m_mana_cost.ManaValue() : 0;
                const int mb = db ? db->card.m_mana_cost.ManaValue() : 0;
                if (ma != mb) { return ma > mb; }
                return a < b;
            });
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

bool GenericProvider::WantVialCharge(const GameState& s, const Permanent& vial) const
{
    // The hand-aware charge policy is the ROOT default, not an archetype opt-in (adopted 2026-08-18,
    // user-directed). The previous `return false` was NOT a guard: every call site already gates on
    // params.upkeep_adds_charge (AIEngine::ChargeRemainingVialsHeuristic, AIEngine::DecideVialCharge,
    // TurnSolver's SimulateBeginningPhase), so this hook is only ever consulted for a real charge
    // permanent and is inert for every deck without one. Its only live effect was to FREEZE the Vial
    // in any deck routed to a non-VialProvider archetype -- Goblins (4x Aether Vial) never gained a
    // counter in its life, and Minotaur is the same latent case.
    //
    // It also poisoned the search: the ROLLOUT models future upkeeps through this same hook, so both
    // arms of the old out-of-band probe rolled out under "never charge again" and tied at every
    // upkeep (`win(heur)=9 win(alt)=9`), leaving the Vial at 0 counters forever. Held-out measurement
    // of the fix: goblins -0.2095 summed over 12 cases (342 games faster, 9 slower over 16,000);
    // knights/slivers byte-identical (they already routed to VialProvider, which had this policy).
    // See docs/design/searched-vial-charge.md.
    return ::WantVialCharge(s, vial);
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

bool GenericProvider::CastEnablerFirst(const GameState&, const std::string& card_name) const
{
    // Param-derived ENABLES edge (docs/design/card-dependency-map.md): a lifegain->loss
    // enabler casts + resolves before its payloads so a same-turn payload sees the flip
    // active. Was an AntiLifegainProvider override; the test is a pure card-parameter check
    // with no archetype knowledge, so it belongs here (identical set, ordering audit item 3:
    // the generic default now agrees with the generic CastOrderRank 0 tier below).
    return ::IsLifegainToLossCard(card_name);
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
// Does shedding a card from HAND pay for itself right now?
//
// The duplicate-legend prune below rests on the cast being a TIE -- the copy dies to the legend
// rule, the board is unchanged -- and that rests in turn on an UNSTATED premise: that the card
// leaving hand is worth nothing. For a deck whose payoff is a hand-size condition it is worth a
// great deal. Neheb, the Worthy gives "Minotaurs you control get +2/+0" only while its controller
// holds one or fewer cards (hand_size_anthem_max), so while that anthem is OFF the spare Neheb is
// not a tie at all: it converts a stranded card into a board-wide pump, and the duplicate dying to
// the legend rule is the POINT rather than the cost.
//
// (User-reported from logs/play/rejections/Minotaur_cod_s3_gi2_t5.json: hand of three, a Neheb and
// a Ragemonger already in play, and the two Minotaur casts leave exactly one card -- switching the
// anthem on across the whole board. The prune dropped that line as pointless.)
//
// PARAM-DRIVEN, never keyed on a card name, so any future hand-size payoff gets this for free and a
// deckbuilding swap that drops Neheb loses it automatically. Inert everywhere else: no other card
// in cards.json carries hand_size_anthem_max, so this scan finds nothing and the prune is exactly
// as before for every other deck.
//
// Deliberately NOT "does THIS cast cross the threshold". The hook is asked about one card at a
// time, while crossing the threshold usually takes the turn's whole cast sequence (two casts, in
// the reported game) -- so a single-cast crossing test would prune precisely the line that
// motivated the fix. Being generous costs one extra plan variant in the states where an anthem is
// live but unmet; being strict costs a real line, and this hook's own comment records that the
// failure direction that matters is pruning a GOOD cast.
static bool HandShedIsPayoff(const GameState& s, int controller)
{
    const int hand_size = static_cast<int>(s.players[controller].hand.size());
    for (const Permanent& p : s.battlefield)
    {
        if (p.controller_index != controller) { continue; }
        const CardDefinition* d = CardDatabase::Instance().LookupCached(p.card);
        if (!d || d->params.hand_size_anthem_max < 0) { continue; }
        // A gate with no bonus behind it is not a payoff.
        if (d->params.hand_size_anthem_power == 0 && d->params.hand_size_anthem_tough == 0)
        { continue; }
        // Anthem currently OFF and the hand is what is holding it off -> shedding pays.
        if (hand_size > d->params.hand_size_anthem_max) { return true; }
    }
    return false;
}

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
        if (p.controller_index == controller && p.card.m_name == def.card.m_name)
        {
            // ...unless emptying the hand is itself the payoff (see HandShedIsPayoff above): then
            // the cast is not the tie this prune assumes, and dropping it loses a real line.
            //
            // AUTONOMOUS GREEDY PLAY KEEPS THE ORIGINAL PRUNE (user, 2026-08-29: "turn it on all of
            // the time, but have d0 ignore that option"). The widened option set is only worth
            // offering where something can WEIGH it, which is SetSearchedPlay's own stated rationale
            // -- "at depth 0 an extra plan variant is not a search, it is just a different fixed rule
            // chosen by enumeration order". Measured exactly that way: at d0 the greedy scorer takes
            // the duplicate on static value and loses 3 games across smoke+regression (each of which
            // wins its original turn again at d3/d5, and none of which is budget -- d0 has no search
            // for a budget to buy); at d3/d5 the score never moved in either direction.
            //
            // HumanPlayActive() is checked FIRST and separately, because the viewer plays at
            // depth 0 BY CONSTRUCTION -- at depth > 0 the engine's bottoming/mulligan rollouts would
            // replay hypothetical games through the same external chooser and start asking the human
            // to play imaginary games (tools/play/server.js). So gating on depth alone would restore
            // exactly the rejection this fix exists to remove; the human path is d0 without being
            // greedy, since a person, not the static scorer, is choosing.
            if (!HumanPlayActive() && !g_searched_play) { return false; }
            return HandShedIsPayoff(s, controller);
        }
    }
    return true;
}

std::vector<int> DecisionProvider::CleanupDiscardCandidates(
    const GameState& s, const std::vector<std::string>* required_pieces) const
{
    return CleanupDiscardRanking(s, required_pieces);
}

// DIAGNOSTIC ONLY (MTG_DISCARD_SHED_VERIFY, see CleanupDiscardShed): the per-shed loop the rollout
// cleanup used to run -- ask, shed, rebuild the hand, ask again -- re-derived on a scratch copy of
// the state so the one-consultation answer can be checked against it. Reported as card NAMES: the
// loop's own indices shift under it as cards are erased, which is exactly the bookkeeping this
// retires. Off by default and never on a shipped path; it copies a GameState and re-ranks per shed,
// which is strictly more work than either form.
std::vector<std::string> CleanupDiscardShedLoopReference(
    const GameState& state, const std::vector<std::string>* required_pieces,
    int count, int pinned_first, bool invert, bool staged_exempt)
{
    std::vector<std::string> out;
    GameState copy = state;
    Player& ap = copy.players[copy.active_player_index];
    int pin = pinned_first;
    for (int k = 0; k < count; ++k)
    {
        const std::vector<int> cd =
            ResolveProvider(copy).CleanupDiscardCandidates(copy, required_pieces);
        if (cd.empty()) { break; }
        std::size_t pick = 0;
        if (pin > 0)     { pick = std::min(static_cast<std::size_t>(pin), cd.size() - 1); }
        else if (invert) { pick = cd.size() - 1; }
        pin = -1;
        int idx = cd[pick];
        const int hand_n = static_cast<int>(ap.hand.size());
        if (staged_exempt && idx >= 0 && idx < hand_n && ap.hand[idx].m_is_staged)
        {
            idx = -1;
            for (int j = 0; j < hand_n; ++j)
            { if (!ap.hand[j].m_is_staged) { idx = j; break; } }
            if (idx < 0) { break; }
        }
        if (idx < 0 || idx >= hand_n) { break; }
        out.push_back(ap.hand[static_cast<std::size_t>(idx)].m_name.str());
        if (!MaybeReplaceGraveyardWithLibraryShuffle(copy, copy.active_player_index, ap.hand[idx]))
        { ap.graveyard.push_back(ap.hand[idx]); }
        ap.hand.erase(ap.hand.begin() + idx);
    }
    return out;
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

// Armored Skyhunter attack-dig put pick (see the DecisionProvider.h note). Rank the legal
// Aura/Equipment cards by the flat power they would grant -- equip_power_bonus for Equipment,
// aura_power_bonus for an Aura -- descending, ties to lower index (library order). Colossus
// Hammer (+10) therefore tops any pool it appears in, which is the deck's real line.
std::vector<int> DecisionProvider::AttackDigPutCandidates(
    const GameState& /*s*/, int /*controller*/,
    const std::vector<Card>& examined, const std::vector<int>& legal) const
{
    std::vector<int> ranked = legal;
    auto grant = [&](int i) -> int {
        const CardDefinition* d = CardDatabase::Instance().LookupCached(examined[i]);
        if (d == nullptr) { return 0; }
        if (d->params.is_equipment) { return d->params.equip_power_bonus; }
        if (d->params.is_aura)      { return d->params.aura_power_bonus; }
        return 0;
    };
    std::stable_sort(ranked.begin(), ranked.end(),
                     [&](int a, int b) { return grant(a) > grant(b); });
    return ranked;
}

// Armored Skyhunter attach-host pick (see the DecisionProvider.h note): the ATTACKER whose
// realized damage this combat rises the most once the equipment lands on it. ds_after counts
// the incoming equipment (a bare Kor Duelist flips to double strike; Balan may cross his 2-
// equipment threshold), and equip_min_power (O-Naginata) filters illegal hosts. A non-attacker
// realizes nothing this combat, so attackers-only is the value-greedy default; the human
// chooser in the viewer may attach anywhere.
int DecisionProvider::AttackDigAttachHost(
    const GameState& s, int /*controller*/, const Card& equip_card,
    const std::vector<int>& attacker_bf_indices) const
{
    const CardDefinition* ed = CardDatabase::Instance().LookupCached(equip_card);
    if (ed == nullptr || !ed->params.is_equipment) { return 0; }
    int best_num = 0, best_delta = -1;
    for (int idx : attacker_bf_indices)
    {
        if (idx < 0 || idx >= static_cast<int>(s.battlefield.size())) { continue; }
        const Permanent& h = s.battlefield[idx];
        if (!h.card.IsCreature() && !h.is_animated) { continue; }
        const int pw_now = EquipGatePowerOf(h, s);
        if (ed->params.equip_min_power > 0 && pw_now < ed->params.equip_min_power) { continue; }
        const CardDefinition* hd = CardDatabase::Instance().LookupCached(h.card);
        const int  n_now    = CountEquipmentAttachedTo(s, h.controller_index, h.card.m_number);
        const bool ds_now   = h.card.HasKeyword(Keyword::DoubleStrike)
                           || HasDoubleStrikeFromEquipment(h, s);
        bool ds_after = ds_now;
        if (hd != nullptr)
        {
            if (hd->params.double_strike_while_equipped && n_now + 1 >= 1) { ds_after = true; }
            if (hd->params.double_strike_min_equipment > 0
                && n_now + 1 >= hd->params.double_strike_min_equipment) { ds_after = true; }
        }
        const int delta = (pw_now + ed->params.equip_power_bonus) * (ds_after ? 2 : 1)
                        - pw_now * (ds_now ? 2 : 1);
        if (delta > best_delta
            || (delta == best_delta && best_num != 0 && h.card.m_number < best_num))
        {
            best_delta = delta;
            best_num   = h.card.m_number;
        }
    }
    return best_num;
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
    //   4 = last-chance   scale-first ONLY on the last fetch, or for a scaler already in hand
    //                     (the USER's rule, 2026-08-25 -- see mode4_scale_first below)
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
    // DEFAULT = 4, the user's last-chance rule (2026-08-25). Measured against mode 1 on the Auras
    // deck, 500-per-seed held-out plus 400-per-seed train, 2700 games:
    //     train    2002 0.0000   3003 -0.0025   4004 -0.0025
    //     held-out 10010 +0.0020 11011 0.0000   12012 0.0000
    // i.e. a WASH on win turn (net -0.0030 summed), which is what the user predicted -- "this might
    // not provide that much benefit in terms of win-turn, but is a good idea in our heuristic". What
    // it buys is the BEHAVIOUR (200-game fetch census, MTG_TRACE=aura): Rancor 55 -> 63 fetches and
    // 4th -> 2nd most-fetched, while Ethereal Armor stays the most-fetched card at 97 (was 102) --
    // "we still want to get Ethereal Armor most of the time" holds. MTG_AURA_RANK_MODE=1 restores
    // the unconditional scale-first default for the A/B.
    static const int s_rank_mode = []{
        const char* e = std::getenv("MTG_AURA_RANK_MODE");
        return (e && *e) ? std::atoi(e) : 4;   // last-chance scale-first (see the note above)
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
    // Mode 4 -- the USER's rule (2026-08-25), reported as "Auras aren't fetched in the right order:
    // it prioritizes Ethereal Armor even though Rancor is better as you are about to play Rancor".
    // Mode 1 puts EVERY scaling Aura ahead of EVERY flat one unconditionally, so Ethereal Armor beats
    // Rancor no matter what is in hand or in the plan. The user's rule keeps the scaler for the cases
    // where it is the LAST chance at it, and takes the flat Aura otherwise:
    //
    //     take the SCALER  iff  this is the last 1-drop Aura fetch in the plan
    //                       OR  the scaler is about to be cast from hand before another fetch fires
    //     else             take the flat Aura
    //
    // "Last fetch in the plan" is the negation of another_aura_soon(): each Aura you CAST fires
    // Light-Paws again, so another castable Aura in hand IS another fetch coming. "About to be cast
    // before we can get it" is a same-NAME copy already in hand -- fetching it then duplicates
    // something the line delivers anyway, and the flat Aura is the one you cannot otherwise get.
    // Applied per-CANDIDATE (a scaler in hand does not speak for a scaler that is not), so the
    // comparator asks it of each side rather than flipping one global switch.
    auto in_hand_by_name = [&](int i) -> bool {
        const InternedName& nm = ap.library[i].m_name;
        for (const Card& c : ap.hand) { if (c.m_name == nm) { return true; } }
        return false;
    };
    const bool more_fetches_coming = another_aura_soon();
    // Under mode 4 a scaling candidate keeps its head start only when this is the last fetch, or when
    // that very Aura would otherwise arrive from hand first.
    auto mode4_scale_first = [&](int i) -> bool {
        return scales(i) && (!more_fetches_coming || in_hand_by_name(i));
    };
    const bool scale_first_now = (s_rank_mode == 1)
                              || (s_rank_mode == 3 && another_aura_soon());
    std::vector<int> out = legal;
    // Every comparison is strict, so the FIRST library index wins any remaining tie -- which
    // stable_sort preserves (that is what keeps mode 0 byte-identical to the historical scan).
    std::stable_sort(out.begin(), out.end(), [&](int a, int b) {
        if (s_rank_mode == 4)
        {
            const bool sa = mode4_scale_first(a), sb = mode4_scale_first(b);
            if (sa != sb) { return sa; }        // only a scaler this is the last chance at leads
        }
        else if (scale_first_now)
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

    // Distinct library creature names + copy counts. Keyed by the card's canonical
    // CardDefinition* (LookupCached returns the SAME pointer for every copy of a name), which is
    // byte-identical to name-keying -- one definition per name -- but skips the per-card std::string
    // construction (`.str()`) and string hashing that showed up hot on this DotH-heavy deck. The map
    // is only ever count-queried, never iterated, so the pointer key's arbitrary order is irrelevant;
    // `names` (a vector) still carries the library first-occurrence order that the enumeration below
    // depends on.
    std::vector<const CardDefinition*>            names;
    std::map<const CardDefinition*, int>          copies;
    for (const Card& lc : ap.library)
    {
        const CardDefinition* d = CardDatabase::Instance().LookupCached(lc);
        if (!d || !d->card.IsCreature()) { continue; }
        if (copies[d]++ == 0) { names.push_back(d); }
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
            if (a == b && copies[a] < 2) { continue; }
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
    //
    // MTG_SAC_SPAWN_LAND_LAST (DEFAULT ON, adopted 2026-08-26 (=0 disables) -- overhaul ledger cg30):
    // a token-spawn land (Forbidden Orchard, taps_spawn_opp_token) is a per-turn damage engine
    // (one opponent Spirit per turn = Suture Priest drip + Massacre Wurm death payoff), and a
    // sacrifice destroys it PERMANENTLY -- the same irrecoverable-source shape as the fuel-dork
    // tap rank (Deathrite). Tapped-first alone eats it whenever the turn's payment happened to
    // tap it before the sac fires (under MTG_DORK_TAP_LAST's lands-first order, reliably), and
    // the sac target is NOT a search branch, so no budget/depth recovers the loss: cg30's T4 win
    // exists for the executor (forced-walk proof in the ledger) but every searched line sacked
    // the Orchard and scored 5. Since MTG_SAC_AXIS the target IS a branch (Plan::sac_pins), and
    // this ranking is its PRIOR: the branch order for cutoffs, and the only decision on the
    // plan-less paths (d0 greedy, rollouts) and for the base plan's default.
    //
    // Band order under the lever (USER doctrine, 2026-08-25: "sacrifice Orchard last. Anything
    // else is a better choice except maybe the bounceland"): fungible lands first (tapped-first
    // within), then BOUNCELANDS (etb_bounce_land -- a Karoo taps for two, so it is 2 mana/turn
    // of ongoing production vs a fungible land's 1), then spawn lands strictly last. A board
    // with only spawn lands is unchanged.
    static const bool s_spawn_last = EnvOn("MTG_SAC_SPAWN_LAND_LAST", true);
    std::vector<int> out = land_indices;
    std::stable_sort(out.begin(), out.end(), [&](int a, int b) {
        if (s_spawn_last)
        {
            auto band = [&](int i) {
                const CardDefinition* d = CardDatabase::Instance().LookupCached(s.battlefield[i].card);
                if (d == nullptr) { return 0; }
                if (d->params.taps_spawn_opp_token) { return 2; }
                if (d->params.etb_bounce_land)      { return 1; }
                return 0;
            };
            const int ba = band(a), bb = band(b);
            if (ba != bb) { return ba < bb; }
        }
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

bool GenericProvider::ShouldEmitRiskyAltPayload(const GameState&, int, const CardDefinition&, bool) const
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

// Collapsed-main mana hold (see DecisionProvider::AttackWith in the header). With the
// main-phase filter active the attack runs BEFORE the turn's casts, so an attacking mana
// creature taps a source the deferred main still needs -- the base architecture never had
// this problem (main 1 spent the mana, the attack got the leftovers). Hold an untapped
// 0-power mana dork when some hand spell is affordable only with creature mana: its attack
// is worth at most an Exalted chip, the held mana is routinely a full turn of tempo
// (antilife gi=9: the one-dork Exalted swing left 3 non-creature sources, so {3}{B} Plague
// Drone was unreachable EVERY turn and the whole line slipped a turn). Colour-blind count
// (mana value only): over-holding costs a 1-point chip, under-holding costs a turn.
static int  AttackPowerOf(const GameState& s, const Permanent& p);          // defined below
static bool AttackHasNonPowerValue(const GameState& s, const Permanent& p); // defined below
static int  VerseDamageFromCast(const GameState& s, int controller,
                                const CardDefinition& def);                 // defined below

// Free-payload kill ceiling: the damage the hand's FREE alt-cost payloads (Reverent Silence
// class: "opponent gains N instead of paying" -> N damage under a live lifegain->loss enabler)
// could add THIS turn, verse triggers included. 0 without a live Remedy -- the payloads are
// then gifts, not damage. Consulted by the hold rule below to price a forgone exalted swing.
static int FreePayloadKillCeiling(const GameState& s, int controller)
{
    if (!RemedyActive(s, controller)) { return 0; }
    int total = 0;
    for (const Card& c : s.players[controller].hand)
    {
        const CardDefinition* d = CardDatabase::Instance().LookupCached(c);
        if (!d || d->params.alt_lifegain_cost <= 0) { continue; }
        if (!ControlsSubtype(s, controller, d->params.alt_cost_requires_subtype)) { continue; }
        total += d->params.alt_lifegain_cost + VerseDamageFromCast(s, controller, *d);
    }
    return total;
}

// Is this hand card a real claimant on the deferred main's mana? Shared by the hold tower's
// needs_creature_mana scan and Main2SpendsCreatureMana below -- ONE reader so the two cannot drift.
// The three exclusions are the measured ones from the antilife stack-vs-base digs:
//  * FREE-ALT (gi=531): a card whose alt cost is payable RIGHT NOW ("rather than pay this spell's
//    mana cost, an opponent gains N", with the required subtype on board) is affordable with ZERO
//    mana, so its printed mana value says nothing about what the deferred main needs. Pricing
//    Reverent Silence at {3}{G} held BOTH dorks on T2 and T3 for a spell ultimately cast free,
//    forfeiting two exalted swings; the turn ended exactly 2 damage short (4 -> 5). A statement
//    about AFFORDABILITY only -- the payload's own gates still decide whether firing it is wise.
//  * UNBACKED GIFT-DAMAGE (gi=839): a damage spell whose rider gifts life (Fiery Justice: 5 damage,
//    opponent gains 5 -> net ZERO unbacked) will be refused by the plan-validity gate, so holding a
//    dork to keep it affordable holds for a cast that provably will not happen.
//  * UNBACKED GIFT-ETB (gi=215/839/8/550/798): a card that HANDS the opponent life and deals no
//    damage is a pure gift with no enabler live; the provider's own EtbGiftValue scores it negative.
static bool M2ManaCandidate(const GameState& s, int active, const CardDefinition* hd)
{
    if (!hd || hd->card.IsLand()) { return false; }
    if (hd->params.alt_lifegain_cost > 0
        && ControlsSubtype(s, active, hd->params.alt_cost_requires_subtype))
    { return false; }
    if (hd->params.opponent_lifegain > 0
        && hd->params.damage - hd->params.opponent_lifegain <= 0
        && !RemedyActive(s, active))
    { return false; }
    if (hd->params.etb_opponent_lifegain > 0 && hd->params.damage <= 0
        && !RemedyActive(s, active))
    { return false; }
    return true;
}

static bool HoldManaSourceForCollapsedMain(const GameState& s, const Permanent& p)
{
    // Searched-branch overrides (MTG_DORK_ATK_SEARCH, EngineFlags.h). The FSLineWin branch and the
    // executor's committed-line pin force this heuristic tower's verdict so the SEARCH decides a
    // contested dork: override 1 = force RELEASE (the release direction), override 0 = force HOLD
    // (the hold direction, MTG_DORK_ATK_HOLD_DIR). Never set outside those two scopes.
    if (DorkAtkSearchEnabled() && g_dork_atk_override == 0)
    {
        if (p.tapped || !p.card.IsCreature()) { return false; }
        const CardDefinition* fd = CardDatabase::Instance().LookupCached(p.card);
        return fd && fd->tmpl == CardTemplate::ManaDork && CanTapNow(p, s.battlefield);
    }
    // DIAGNOSTIC (MTG_DORK_FORCE_HOLD_TURN=<turn>, default 0 = INERT): force every mana dork to
    // hold on that turn, in rollouts AND the executor. Kept as the instrument that PROVED the hold
    // direction was worth a turn on gi852. NB an executor-only force is not a valid test -- the m2
    // plan is replayed from a committed line built assuming combat happened.
    {
        static const int s_fh = EnvInt("MTG_DORK_FORCE_HOLD_TURN", 0);
        if (s_fh > 0 && s.turn_number == s_fh && p.card.IsCreature() && !p.tapped)
        {
            const CardDefinition* fd = CardDatabase::Instance().LookupCached(p.card);
            if (fd && fd->tmpl == CardTemplate::ManaDork) { return true; }
        }
    }
    if (DorkAtkSearchEnabled() && g_dork_atk_override == 1)  { return false; }
    if (p.tapped || !p.card.IsCreature())                   { return false; }
    // A vigilant dork does not tap to attack, so its mana survives combat and the hold buys
    // nothing (USER 2026-08-21: vigilance -> freely attack). Gated with the feature so the
    // default path stays byte-identical.
    if (DorkAtkSearchEnabled() && p.card.HasKeyword(Keyword::Vigilance)) { return false; }
    if (p.EffectivePower() > 0)                             { return false; }
    const CardDefinition* d = CardDatabase::Instance().LookupCached(p.card);
    if (!d || d->tmpl != CardTemplate::ManaDork)            { return false; }
    if (!CanTapNow(p, s.battlefield))                       { return false; }
    if (!TurnSolver::CollapsedMainActive(s))                { return false; }

    const int active = s.active_player_index;
    int total = 0, creature_src = 0;
    for (const Permanent& q : s.battlefield)
    {
        if (q.controller_index != active || q.tapped) { continue; }
        const CardDefinition* qd = CardDatabase::Instance().LookupCached(q.card);
        if (!qd) { continue; }
        const bool dork = qd->tmpl == CardTemplate::ManaDork && CanTapNow(q, s.battlefield);
        if (dork) { ++creature_src; ++total; continue; }
        if (q.card.IsLand() || qd->params.mana_rock) { ++total; }
    }
    if (creature_src <= 0) { return false; }
    // WHAT THE HELD MANA BUYS -- computed FIRST, because the exalted releases below are only
    // sound once we know it. A hand card is creature-mana-dependent when it is affordable with
    // the dorks and NOT without them; whether any such card is a DAMAGE payload decides whether
    // a 2-point exalted swing is the better use of the same permanent.
    const int noncreature = total - creature_src;
    bool needs_creature_mana = false;
    bool needed_deals_damage = false;
    int  need_creature_src   = 0;   // creature sources the most demanding such card requires
    for (const Card& c : s.players[active].hand)
    {
        const CardDefinition* hd = CardDatabase::Instance().LookupCached(c);
        if (!M2ManaCandidate(s, active, hd)) { continue; }
        // FREE-ALT EXCLUSION (antilife stack-vs-base dig 2026-08-16, gi=531): a card whose
        // alternative cost is payable RIGHT NOW ("rather than pay this spell's mana cost, an
        // opponent gains N" with the required Forest on board) is affordable with ZERO mana, so
        // it is never "affordable only with creature mana" -- its printed mana value says
        // nothing about what the deferred main needs. Pricing Reverent Silence at its printed
        // {3}{G} held BOTH dorks on T2 and T3 for a spell that was ultimately cast for free,
        // forfeiting two exalted swings; the turn then ended exactly 2 damage short (4 -> 5).
        // This is a statement about AFFORDABILITY, not about whether firing the alt is wise:
        // the payload's own gates (CanAutoFireAltPayload / ShouldEmitRiskyAltPayload) still
        // decide that. Inert for every deck without an alt-cost card.
        // ... and the same for a damage spell whose RIDER gifts life (Fiery Justice: 5 damage,
        // opponent gains 5 -> net ZERO swing unbacked). SubsetHasUnbackedGiftDamage now refuses to
        // cast it in that state, so holding a dork to keep it affordable is holding for a cast that
        // provably will not happen (gi=839: the lone Hierarch pinned on T2 for a Fiery Justice the
        // plan-validity gate rejects; base swung for 1 and its T3 was exactly lethal, 3 -> 4).
        // UNBACKED-GIFT EXCLUSION (antilife cs-vs-base dig 2026-08-16, gi=215/839/8/550/798 --
        // the 10-vs-0 one-sided 3 -> 4 bucket): a card whose resolution HANDS the opponent life
        // and deals no damage of its own is, with no lifegain->loss enabler live, a pure gift the
        // policy will not cast (the provider's own EtbGiftValue scores it negative). Holding a
        // dork back to keep it "affordable" therefore buys nothing THIS turn -- and the hold only
        // ever matters this turn, since every source unhtaps on the next one. gi=215: Aria of
        // Flame ({2}{R}, gifts 10 with no Remedy out) pinned the lone Hierarch on T2; base swung
        // it for 1 exalted and its T3 was EXACTLY lethal, while the held line finished the same
        // T3 on 1 life and needed a fourth turn. Keyed on params, so it generalises to any
        // enabler deck; inert wherever nothing gifts life.
        const int mv = hd->card.m_mana_cost.ManaValue();
        if (mv <= noncreature || mv > total) { continue; }
        needs_creature_mana = true;
        need_creature_src   = std::max(need_creature_src, mv - noncreature);
        const CardParams& hp = hd->params;
        if (hp.damage > 0 || hp.opponent_lifegain > 0 || hp.etb_opponent_lifegain > 0
            || hp.alt_lifegain_cost > 0 || hp.verse_damage || hp.lifegain_to_loss)
        { needed_deals_damage = true; }
    }
    if (!needs_creature_mana) { return false; }   // nothing to hold for
    // SURPLUS RELEASE (antilife stack-vs-base dig 2026-08-16, gi=174): the hold was
    // all-or-nothing -- ANY creature-mana-dependent hand card pinned EVERY dork -- but the
    // deferred main only needs `need_creature_src` of them. Exactly one dork ever swings
    // (ShouldAttackWith gives the lone-attacker slot to the lowest-index eligible body, so
    // exalted is never broken by releasing several), so the swing is free whenever holding one
    // fewer source still covers the most demanding card. gi=174 T4: opp at 11, two lands
    // covering {G}{W} and two dorks, Fiery Justice ({R}{G}{W}, 10 damage under the Remedy)
    // needing just ONE dork for its {R} -- the blanket hold forfeited the exalted swing and the
    // turn ended at 1 life instead of 0 (4 -> 5). Colour-blind like the count above: it can
    // under-hold when the surplus dork is the only source of a needed COLOUR, which costs the
    // same turn the blanket hold was protecting -- measured net-better, and the colour-aware
    // form is the open land/source tie-break item.
    if (creature_src - 1 >= need_creature_src) { return false; }
    // EXALTED correction (antilife stack-vs-base dig 2026-08-15, gi=76): the "attack is worth
    // at most a chip" premise fails when the 0-power dork IS the deck's attack -- swinging
    // alone it deals CountExalted damage (2+ with a second Hierarch out). When the board's
    // exalted count is >= 2 and no other real attacker exists, the forgone swing is worth more
    // than the held tempo (gi=76: holding cost 4 damage across T2/T3 and a full turn to enable
    // a BIRDS, 4->5). The motivating gi=9 case (SINGLE exalted -- a genuine 1-point chip --
    // enabling {3}{B} Plague Drone) still holds.
    const int exalted = CountExalted(s.battlefield, active);
    if (exalted >= 1)
    {
        bool other_attacker = false;
        for (const Permanent& q : s.battlefield)
        {
            if (&q == &p || q.controller_index != active)                 { continue; }
            if (!CanAttackFull(q, s.battlefield, active))                 { continue; }
            if (AttackPowerOf(s, q) > 0 || AttackHasNonPowerValue(s, q))  { other_attacker = true; break; }
        }
        if (!other_attacker)
        {
            // ... but NOT when the held mana buys DAMAGE (held-out census 2026-08-16, gi=530):
            // under the collapsed main the attack runs BEFORE the casts, so releasing the dorks
            // taps the deck's whole mana base and the post-combat payload dump cannot be paid.
            // gi=530: two Hierarchs attacking is worth 2 (and attacking with BOTH forfeits
            // exalted entirely), while the same mana casts Fiery Justice for the T4 kill --
            // measured base tail=4 vs classify tail=5 at the identical node. gi=76's release
            // stands: there the creature-mana-dependent card was a BIRDS (ramp, no damage).
            if (exalted >= 2 && !needed_deals_damage) { return false; }
            // LETHAL-RELEVANT single chip (gi=230): a 1-point exalted swing is no longer "at
            // most a chip" when it completes a this-turn kill -- opp at 8 with a free Reverent
            // Silence in hand (6 alt + 1 verse) needs exactly the swing to reach 0. Release
            // the hold when swing + the free-payload ceiling covers the opponent's life; the
            // motivating gi=9 hold (chip vs a full turn of Plague Drone tempo, opp far from
            // dead) is untouched because the ceiling cannot reach a healthy life total.
            if (s.players[1 - active].life <= exalted + FreePayloadKillCeiling(s, active))
            { return false; }
        }
    }
    return true;
}

bool DecisionProvider::AttackWith(const GameState& s, const Permanent& attacker) const
{
    // "This creature attacks each combat if able" (Deathbellow Raider, CR 508.1a). A RESTRICTION,
    // so it outranks every hold and every archetype's ShouldAttackWith -- the player has no choice.
    // Placed ahead of the mana hold deliberately: a must-attack creature that also taps for mana
    // still has to attack. Vs the passive opponent the goldfish default already attacks with
    // everything, so today this only binds if a provider would have declined; it is here because
    // the rule is a restriction, not because it currently changes a game.
    {
        const CardDefinition* ad = CardDatabase::Instance().LookupCached(attacker.card);
        if (ad && ad->params.must_attack) { return true; }
    }
    if (HoldManaSourceForCollapsedMain(s, attacker)) { return false; }
    return ShouldAttackWith(s, attacker);
}

// Searched dork attack/hold -- the CONTESTED test (MTG_DORK_ATK_SEARCH, EngineFlags.h; USER
// design 2026-08-21). True when this pre-combat state has >=1 dork the collapsed-main mana
// hold pins whose RELEASED swing would actually deal damage -- i.e. the hold's verdict is a
// judgment call the search should price, not an obvious case a heuristic may close:
//   * 0-effective-power swings are NOT contested (the release adds nothing; greedy hold
//     stands -- "a lot of dorks are 0 power, these rules will help a lot").
//   * effective power counts the lone-exalted bonus (the recorded Hierarch trap: printed
//     power is not the test) -- released ALONE, a 0/1 Hierarch swings for CountExalted.
//   * vigilant dorks never reach here (exempted inside the hold: attacking costs no mana).
//   * no-m2-need states never reach here (the hold's own trigger already released them --
//     the USER's vacuity rule, "freely attack if there is nothing we could need them for
//     in main 2", is the hold's needs_creature_mana test).
// Called by the FSLineWin branch site on the post-cast pre-combat state; one battlefield scan.
// Would the DEFERRED MAIN actually SPEND creature mana? The hold tower's needs_creature_mana asks
// only whether ONE hand card is affordable WITH the dorks and not without -- which misses the case
// where every copy is individually cheap but the SET is not. AL gi852: two Fiery Justice at 3 each
// against 4 lands, so each copy clears the single-card test while the PAIR needs the dork; the
// forgone 6th source cost 10 damage and the game a turn. This accumulates the cheapest eligible
// casts up to what the board can pay and asks whether that spend exceeds the NON-creature sources.
// Deliberately optimistic -- it does not re-check colours or whether each cast is wise, because it
// is a TRIGGER for the search, not a decision: the branch that follows measures both variants and
// the tie rule keeps the attack. Cheap (one hand scan + a sort of a handful of ints).
static bool Main2SpendsCreatureMana(const GameState& s, int active)
{
    int total = 0, creature_src = 0;
    for (const Permanent& q : s.battlefield)
    {
        if (q.controller_index != active || q.tapped) { continue; }
        const CardDefinition* qd = CardDatabase::Instance().LookupCached(q.card);
        if (!qd) { continue; }
        if (qd->tmpl == CardTemplate::ManaDork && CanTapNow(q, s.battlefield))
        { ++creature_src; ++total; continue; }
        if (q.card.IsLand() || qd->params.mana_rock) { ++total; }
    }
    if (creature_src <= 0) { return false; }
    const int noncreature = total - creature_src;
    std::vector<int> mvs;
    for (const Card& c : s.players[active].hand)
    {
        const CardDefinition* hd = CardDatabase::Instance().LookupCached(c);
        if (!M2ManaCandidate(s, active, hd)) { continue; }
        const int mv = hd->card.m_mana_cost.ManaValue();
        if (mv > 0) { mvs.push_back(mv); }
    }
    std::sort(mvs.begin(), mvs.end());
    int spend = 0;
    for (int mv : mvs) { if (spend + mv > total) { break; } spend += mv; }
    return spend > noncreature;
}

int DorkAtkContestedKind(const GameState& s)
{
    if (!DorkAtkSearchEnabled())             { return 0; }
    if (!TurnSolver::CollapsedMainActive(s)) { return 0; }
    const DecisionProvider& prov = ResolveProvider(s);
    const int active = s.active_player_index;
    int  natural_attackers = 0;
    int  held_power        = 0;   // released swing power of the held dorks (printed/effective)
    int  held_n            = 0;
    int  atk_dorks         = 0;   // mana dorks the greedy WANTS to swing (the hold direction)
    for (const Permanent& p : s.battlefield)
    {
        if (p.controller_index != active)              { continue; }
        if (!CanAttackFull(p, s.battlefield, active))  { continue; }
        if (prov.AttackWith(s, p))
        {
            ++natural_attackers;
            // HOLD DIRECTION (USER 2026-08-21: "we should contest the dork when main 2 has a use
            // for the mana"). An attacking mana dork TAPS for its swing, so the deferred main
            // loses that source -- the mirror of the release case below.
            // VIGILANCE IS EXEMPT (USER: "vigilance dorks like Faeburrow Elder ... can and should
            // freely attack"): a vigilant dork does not tap to attack, so its mana survives combat
            // and there is nothing to contest. Same exemption the tower applies on the release side.
            if (DorkAtkHoldDirEnabled() && !p.card.HasKeyword(Keyword::Vigilance)
                && CanTapNow(p, s.battlefield))
            {
                const CardDefinition* pd = CardDatabase::Instance().LookupCached(p.card);
                if (pd && pd->tmpl == CardTemplate::ManaDork) { ++atk_dorks; }
            }
            continue;
        }
        // Not attacking naturally: held by the mana hold, or declined by ShouldAttackWith.
        // Only the mana hold's victims are contested -- a ShouldAttackWith decline (0-power
        // dork NOT wanted even with mana free) stays greedy.
        if (!HoldManaSourceForCollapsedMain(s, p))     { continue; }
        if (!prov.ShouldAttackWith(s, p))              { continue; }
        ++held_n;
        held_power += p.EffectivePower();
    }
    // RELEASE DIRECTION (checked first, so the pre-existing verdict is unchanged wherever it
    // fires): the held dorks join (or become) the attack. Lone-exalted bonus applies when the
    // release yields exactly ONE total attacker.
    if (held_n > 0)
    {
        int released_dmg = held_power;
        if (natural_attackers == 0 && held_n == 1)
        { released_dmg += CountExalted(s.battlefield, active); }
        if (released_dmg >= 1) { return 1; }
    }
    // HOLD DIRECTION: only worth a branch when the deferred main can actually spend the mana the
    // swing would consume -- that gate is what keeps this from contesting every attacking dork
    // (USER's standing perf bar: "to avoid this becoming much slower").
    if (atk_dorks > 0 && Main2SpendsCreatureMana(s, active)) { return 2; }
    return 0;
}

// Does this card resolve into INFORMATION -- i.e. is it a "draw" for ordering purposes? Shared by
// the ideal-order rank and the --cast-order-report so the report cannot drift from play. Deliberately
// the SAME card classes OrderingOpaque lists (draw / staging / cascade / retrace / impulse /
// solo-target trick / EI), because those are precisely the ones whose order it currently refuses to
// decide. See docs/design/cast-order-ideal-with-ranges.md.
// Which draws actually WANT to go first (USER 2026-08-17: "there needs to be some discernment on
// the order and range"). Drawing is not the criterion -- Magma Opus (8), Apex of Power (10) and
// Throes of Chaos (4) all draw, and "cast it first" is not a real instruction for any of them:
// they ARE the turn, so leading with one is not information-gathering, it is the payoff.
//
// The discernment is DERIVED rather than chosen: a draw belongs in the information tier when
// casting it first leaves the turn's mana essentially intact. `IdealOrderCantripMaxMv` (2) is the
// point at which that stops being true for every deck in the suite -- Ponder/Preordain (1),
// Expressive Iteration / Light Up the Stage / Treasure Hunt (2) qualify; the three above do not.
// MTG_IDEAL_CANTRIP_MV overrides it for an A/B.
//
// A non-cantrip draw is NOT thereby banned from going early: it keeps its natural (cost-efficient)
// rank, which is the far end of its RANGE, and the ladder can still walk it toward the ideal end
// when the mana is genuinely there. Discernment picks the rank; the range keeps the option.
// MEASURED at 1, not reasoned at 2. The bar was first set to 2 on the argument that Expressive
// Iteration / Light Up the Stage / Treasure Hunt (all mv 2) leave the turn's mana "essentially
// intact". Smoke says otherwise: dropping the bar to 1 removes Treasure Hunt's +0.0140 d0
// regression entirely and flips mirrorwing d0 from +0.0020 to -0.0160, at no cost anywhere. On a
// two-or-three-land turn a 2-mana cantrip IS the turn -- the same argument that already excluded
// Magma Opus, applied at the threshold the measurement picked rather than the one that sounded
// right. MTG_IDEAL_CANTRIP_MV overrides it for an A/B.
int IdealOrderCantripMaxMv()
{
    static const int mv = EnvInt("MTG_IDEAL_CANTRIP_MV", 1);
    return mv;
}

bool IsIdealOrderCantrip(const CardDefinition& def)
{
    if (!IsIdealOrderDraw(def)) { return false; }
    // SPECTACLE IS NOT A CHEAPER CANTRIP -- it is a CONDITIONAL one, and the condition is an
    // ordering fact (USER 2026-08-17: "spectacle is messier, because it gets cost reductions by
    // going after the first damage spell"). Light Up the Stage is {2}{R} printed and {R} only once
    // an opponent has lost life, so ranking it by its cheapest cost would put it FIRST -- exactly
    // where spectacle is NOT live and it costs the full three. Its ideal slot is "after the first
    // damage source", which is principle 3 (enabler before payoff) pointing at a cost rather than
    // an effect: the burn spell ENABLES the cheap draw. A scalar rank cannot say that, so the
    // printed cost stands here and spectacle draws stay at their cost-efficient end until the
    // range carries a dependency. Recorded in cast-order-ideal-with-ranges.md as open work.
    return def.card.m_mana_cost.ManaValue() <= IdealOrderCantripMaxMv();
}

// See IdealOrderSuppressScope: the range's cost-efficient end is this same rank with the tier
// below stood down, so the two ends share one definition.
thread_local bool g_suppress_ideal_order_tier = false;
// Manland reserve release (see DecisionProviders.h). False everywhere except inside one
// ManlandReserveReleaseScope -> every autonomous rank query is unchanged.
thread_local bool g_release_manland_reserve = false;

bool IsIdealOrderDraw(const CardDefinition& def)
{
    return def.tmpl == CardTemplate::DrawUntilNonland
        || def.params.draw > 0
        || def.params.cast_draw > 0
        || def.params.cast_reorder > 0
        || def.params.stages_cards
        || def.params.expressive_iteration
        || def.params.impulse_exile > 0
        || def.params.cascade_max_mv > 0
        || def.params.retrace
        || def.params.solo_target_trick;
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
    // DEPENDENCY-MAP tiers (docs/design/card-dependency-map.md):
    //    0 a lifegain->loss ENABLER (Tainted Remedy / Plague Drone): FIRST, so every same-turn
    //      opponent-lifegain payload (alt costs, Aria's ETB, Fiery Justice's rider) resolves
    //      with the flip already active. Was an AntiLifegainProvider override; pure card-
    //      parameter test, so it lives here (any deck with the edge gets the order for free).
    //   19 a CAST-PAYOFF (verse_damage, Aria of Flame): benefits from every instant/sorcery
    //      cast AFTER it resolves, so it goes right before the generic tier-20 spells that
    //      feed its verse counters (and before any alt-cost payload burst it escalates).
    // ---- IDEAL ORDER, tier 2: DRAW FIRST (MTG_IDEAL_ORDER, USER 2026-08-17) --------------------
    // "Draw before playing land or rituals" -- principle 1 of docs/design/cast-order-ideal-with-
    // ranges.md. A cantrip resolves into INFORMATION; the land drop and the ritual are commitments
    // that are strictly better made once you have it. Ranked ahead of the mana rock (5) and the
    // ritual (15) accordingly, and behind the lifegain enabler (0), which is a correctness edge.
    //
    // THIS RANK IS NOT SAFE ON ITS OWN, and the note above says why: a bare "draw first" rank was
    // tried before and "fixes some games and breaks others", because a draw cast first can spend
    // the mana the rest of the line needed. That is exactly the case the USER's design answers with
    // a RANGE (ideal -> cost-efficient) rather than a fixed position: start here, and fall back
    // toward the cost-efficient slot only when the ideal order cannot actually be paid for. That
    // ladder is MTG_ORDER_RANGE (ApplyCastOrderRangeLadder in ManaPayment.cpp); this tier is the
    // range's IDEAL end and the un-promoted rank below is its cost-efficient end. Both levers are
    // DEFAULT OFF, and this one alone is expected to be a mixed result -- it is wired so the
    // per-deck ranking can be reviewed (mtg <deck> --cast-order-report) against the numbers play
    // would use.
    // MTG_IDEAL_ORDER promotes for EVERY deck (the measurement arm); PromoteCantripsInCastOrder is
    // the per-deck route the measurement actually endorses -- see the hook's declaration.
    static const bool s_ideal_order = EnvOn("MTG_IDEAL_ORDER");
    if ((s_ideal_order || PromoteCantripsInCastOrder())
        && !g_suppress_ideal_order_tier && IsIdealOrderCantrip(def)) { return 2; }
    if (def.params.lifegain_to_loss)             { return 0; }
    if (def.params.max_casts_after >= 0)         { return 18; }
    if (!def.params.reduces_spell_color.empty()) { return 16; }
    //    8 a COLOURED-pip subtype cost reducer that is itself a creature (Ragemonger): before the
    //      other creatures (tier 10), because the tier-10 tiebreak is cheapest-first and would cast
    //      the 1-drops it discounts AHEAD of it -- the executor reprices every cast on the live
    //      battlefield, so the reducer resolving first is what realises the discounted line the
    //      same-turn pip credit enumerated (Minotaur s4/gi3 t3: Ragemonger then Gnarled Scarhide
    //      for {0}; cheapest-first cast Scarhide first at {B} and stranded the line). Warchief's
    //      generic twin (reduces_spell_subtype) deliberately keeps its historical tier -- moving it
    //      would reorder measured Goblins lines, a separate adoption if ever wanted.
    if (!def.params.reduces_subtype_colored_subtype.empty()
        && def.params.reduces_subtype_colored_cost.has_value()) { return 8; }
    if (IsManaRitual(def))                       { return 15; }
    if (def.params.verse_damage)                 { return 19; }
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
    // GROWTH tier: a creature that feeds a live scaled mana dork's count (an Elf while an untapped
    // Priest of Titania / Elvish Archdruid can tap) casts just before the other creatures, so by
    // the time a payment must tap the dork every such body is already down -- the executor half of
    // the EnumeratePlans dork-growth credit (USER 2026-08-20: "play every elf we can [without]
    // tapping scaling dorks ... then play every elf remaining with scaled mana"). Within the tier
    // the stable sort keeps plan order, which is exactly the order the credit's sequential model
    // walks. Gated on a live scaled dork existing -> byte-identical for every other deck. (Rank 9
    // is also VialProvider's want-subtype tier; the two can never be live in the same deck, and no
    // comparator keys on the number alone.)
    if (DorkGrowthEnabled() && def.card.IsCreature() && FeedsLiveScaledDork(s, def)) { return 9; }
    if (def.card.IsCreature())                 { return 10; }
    return 20;
}

std::vector<int> GenericProvider::XCandidates(const GameState& s, const CardDefinition& def,
                                              int max_affordable) const
{
    // Tuck removal (Unexpectedly Absent): the INVERSE of the max-X rule below. X buries the
    // target deeper in its owner's library -- vs this sim's passive opponent the tucked spawn
    // never returns regardless (its owner draws nothing), and a self-tuck wants the EARLIEST
    // redraw -- so a higher X is never better for either use and only wastes mana. X = 0 is
    // legal ({X}{W}{W} with X=0) and always optimal; unpruned/human play opens the full range.
    if (def.params.tuck_to_library)
    {
        if (DecisionUnpruned(UnprunedGate::XSpell) || HumanPlayActive())
        {
            std::vector<int> all;
            all.reserve(static_cast<std::size_t>(std::max(0, max_affordable)) + 1);
            for (int x = 0; x <= max_affordable; ++x) { all.push_back(x); }
            return all;
        }
        return { 0 };
    }
    // See DecisionProvider::XCandidates. In a goldfish, an {X} spell (X burn, X draw, X pump)
    // wants all available mana: a larger X is never worse for closing the game. So the prune
    // proposes the single max-affordable value -- no branching. MTG_UNPRUNED opens the full
    // 1..max range so the unpruned-vs-pruned A/B can confirm the prune leaves nothing behind
    // (e.g. a turn where holding mana for a second spell beats max-X). Empty when X must be 0.
    (void)s;
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

static int ManaSourceRankBase(const GameState& s, const CardDefinition& def);

// A mana CREATURE taps AFTER every land, and a still-GROWING one taps after that.
//
// `BatchPrepayMainCasts` already states the doctrine for the whole-turn reserve -- "a land has no
// use but its mana, a creature does, so pay off the lands whenever the whole turn can be" -- and
// `TapSpareCreaturesEnabled` applies it to the BACKTRACKER's candidate list. Neither reaches the
// scarcity GREEDY, which is the path that pays most costs: there a mana creature was ranked by
// colour exactly like a land, so a mono dork (10) was spent ahead of a dual land (20), trading a
// body that can attack, block, be a copy target or take a pump for a permanent whose only use is
// its mana. 64 is past every land tier and past the 60-63 reserve tiers.
//
// GROWABLE (65): a source whose yield can still RISE is worth holding one slot longer than a fixed
// one. `domain_mana` (Faeburrow Elder, Bloom Tender) adds one mana per COLOUR you control, and the
// ladder had it exactly backwards -- ManaSourceRankBase reads EffectiveProduces, i.e. the colours
// in play RIGHT NOW, so at two colours out Bloom Tender ranked 20 (dual) and was spent ahead of a
// fixed Birds of Paradise at 50, i.e. cheapest precisely when it had the most room to grow.
//
// The SUBTYPE scalers (Priest of Titania, Elvish Archdruid) deliberately do NOT get this: holding
// them back measured -0.10 WORSE, all on stompy. One Priest tap yields N off ONE body, so forcing
// it last burns N Llanowar Elves for the same mana and loses N bodies -- and on an Elf deck the
// bodies are the win condition. Tier 61's DorkGrowth rationale ("tap it last so its burst is
// bigger") loses to body-count there. See docs/design/mana-creature-tap-order.md §5b.
//
// Measured (NET summed over every moved case, negative = better; nothing worse than +0.0010 in any
// mode): smoke -0.3616, regression -0.5783, HELD-OUT overnight -1.3840. Deterministic counters on
// Mirrorwing: -2.8% rollout calls, -3.8% turn_steps -- better play ends games sooner, so it is
// cheaper too. Off-switch MTG_NO_DORK_TAP_LAST restores the plain colour rank for A/B.
//
// FUEL-CONSUMING (67): a dork whose mana tap burns a NONRENEWABLE resource (gy_land_exile_mana --
// Deathrite Shaman exiles a graveyard land per tap) taps after EVERY other creature. USER doctrine
// (2026-08-25): "We should be prioritizing tapping most other dorks over Deathrite because Fetches
// are a limited resource" -- with the explicit caveat that this is NOT generic against a domain
// grower (Faeburrow Elder / Bloom Tender): "You might tap those after to get the full value of
// their tap" (spending Deathrite's one mana on a small pip can be what saves the grower's
// multi-yield for a later cast, exactly the FiveColour s13_gi12 human line), "or for the higher
// attack damage of Faeburrow" (+1/+1 per colour + vigilance: a pre-combat mana tap forfeits a
// 4-5 power attack that an attacking-but-vigilant Faeburrow would have kept, while still tapping
// for mana post-combat). So 67 is the
// DEFAULT, not a law: it is the measured-better greedy order on the one deck that has both
// (held-out fivecolour: every mover returned-to-GT or beat it), and the contextual exceptions
// belong to the layers that can see the whole turn -- the reserve ladder in engine play, the
// --tap-pref recording pin in reference replay -- never to this per-source rank. Any other
// creature tap is recovered at untap; this one permanently destroys a future activation. The plain
// colour ladder got this right by accident (DRS's any-colour yield read as rainbow 50, after
// domain ~30), and the band collapsed the pair: DRS landed at F (plain dork) / F+1 (plan-bias
// flat), BEFORE the domain grower, so a T3 payment that clean covered with lands+Faeburrow instead
// tapped Deathrite and exiled the fetch land -- and the T4 exact-cover the hand-played reference
// FiveColour s9_gi8 recorded (Cannons+Faeburrow+Oko off 9 = 3 duals + DRS + Faeburrow-5) became
// unpayable, dropping the recorded plan from enumeration. Same defect family as the Lodge band
// inversion below: a within-band order that spends the irrecoverable source first.
static constexpr int kManaCreatureTapRank         = 64;
static constexpr int kGrowableManaCreatureTapRank = 65;
static constexpr int kFuelManaCreatureTapRank     = 67;

// DEFAULT ON, adopted 2026-08-26 (MTG_DORK_TAP_LAST=0 disables); requires the one-shot fix below.
//
// NOT adopted despite the numbers, and the reason is a REFERENCE, not an aggregate: with this on,
// the hand-played `references/Mirrorwing_Dragon/claude_s26_gi25.json` replays to T8 against a
// recorded T5 -- a `play-drift`, which the reference reproducibility gate fails outright. With it
// off, 0 play-drift across all 208 refs.
//
// The mechanism is the one traced at docs/design/mana-creature-tap-order.md §6b: making the dork
// expensive to tap does not make the payment cheaper, it makes it reach for the ONE-SHOT instead.
// On Mirrorwing that is a Gold Rush Treasure -- the dork untaps next turn, the Treasure is gone for
// good, and the turn that needed it loses the kill. USER, 2026-08-23: "We need to keep the treasures
// for sure." So the prerequisite is a rule that prefers the REPEATABLE source whenever the turn can
// be paid either way (§2b's "waste is the trigger"), which lives at the plan level where the sac is
// actually chosen -- not in this rank. Revisit together; adopting this half alone trades a Treasure
// for a tap and the reference says that is a losing trade.
inline bool DorkTapLastEnabled()
{
    static const bool v = EnvOn("MTG_DORK_TAP_LAST", true);
    return v;
}

int GenericProvider::ManaSourceRank(const GameState& s, const CardDefinition& def) const
{
    const int r = ManaSourceRankBase(s, def);
    if (DorkTapLastEnabled() && def.tmpl == CardTemplate::ManaDork)
    {
        const int F = kManaCreatureTapRank;
        // Fuel-consuming dork (Deathrite): last of ALL creatures, both band paths -- its tap
        // permanently exiles graveyard fuel, which no other band member's tap costs. See the
        // kFuelManaCreatureTapRank comment for the FiveColour s9_gi8 trace.
        if (def.params.gy_land_exile_mana) { return kFuelManaCreatureTapRank; }
        // SCALER PLAN BIAS (MTG_SCALER_PLAN_BIAS + live PlanTraits): a scaler's slot in the band
        // depends on WHAT THE PLAN DOES (USER 2026-08-24; mana-order-and-reserve-overhaul.md).
        // Casting its food this turn -> tap it LAST (F+2): the burst counts the creatures the plan
        // is about to land. Attack turn, no food -> tap it FIRST among creatures (F): one big body
        // pays what N flat bodies would, keeping the flat dorks untapped to swing (§5b's stompy
        // body-count lesson, applied only on the turns it holds). The flat creatures sit at F+1 and
        // the domain scaler (Faeburrow/Bloom Tender -- grows with COLOURS, held back per §5b's
        // winning arm) at F+2. Null traits (lever off / outside a plan apply) -> the static band
        // below, byte-identical to the shipped shape.
        const PlanTraits* pt = ScalerPlanBiasEnabled() ? CurrentPlanTraits() : nullptr;
        if (pt)
        {
            if (IsScaledManaDork(def))
            {
                if (pt->casts_scaler_food) { return F + 2; }
                // NOTE (2026-08-25): a per-CAST waste-aware refinement here (demote the scaler
                // when the current payment's remaining pips < its yield) was BUILT AND REFUTED by
                // measurement -- held-out stompy overnight went +0.021/cell (net 111 slower / 23
                // faster with its companion). The per-cast view is myopic: the scaler's "wasted"
                // surplus floats into the turn's LATER casts (commit_leftover), so the demotion
                // taps a flat body on every cast's tail pip and bleeds attackers -- §5b's
                // one-big-tap rationale is correct at TURN scope. Any retry must reason at turn
                // scope (e.g. tap-the-sac-fodder-first, or batch-combined remaining need). See
                // the overhaul ledger's Cluster B section.
                if (pt->attack_matters)    { return F; }
                return F + 1;
            }
            if (def.params.domain_mana) { return F + 2; }
            return r < F + 1 ? F + 1 : r;
        }
        if (def.params.domain_mana) { return kGrowableManaCreatureTapRank; }
        return r < F ? F : r;
    }
    return r;
}

static int ManaSourceRankBase(const GameState& s, const CardDefinition& def)
{
    // See DecisionProvider::ManaSourceRank. Flexibility rank for the scarcity-first tap order (LOWER =
    // tap earlier). SPEND the least flexible first so the flexible sources stay available.
    const int active = s.active_player_index;
    // PAY-SAC ONE-SHOT (§2a Treasure): rank 26 -- after the filters (25), before tri (30). Without
    // this tier it accidentally ranked "rainbow" (50): its produces list is EMPTY, EffectiveProduces
    // synthesises WUBRG, and the ladder read that as flexibility. But its governing axis is
    // EXACTNESS, not colour (lump-mana-sources doc §9): one wild mana, gone forever when spent --
    // so it pays ahead of the flexible lands and all creatures, behind the fixed lands. Swept via
    // the (deleted) MTG_PAYSAC_RANK scaffolding: 25/26 measured -0.0050 train / -0.0044 held-out
    // (t=-6.77) on Mirrorwing, compounding with the creature band (mana-creature-tap-order.md §8).
    // The "keep the Treasure on a durdle turn" half is NOT this rank -- it is the reserve ladder's
    // one-shot hold (mana-order-and-reserve-overhaul.md layer 2), which overrides this order
    // whenever the turn pays without the one-shot. Inert unless MTG_TREASURE_PAY_SOURCE is on
    // (nothing else satisfies IsPaySacSource).
    if (IsPaySacSource(def)) { return 26; }
    // A COLOURLESS-only manland (Mutavault) has marginal mana (pays only generic) but real attack
    // value, so SAVE it: rank above even rainbow, so it's tapped only when nothing else can pay. (It
    // is still used when required; ranking it last just stops the greedy spending it on a pip a real
    // land could cover, which in the rollout was costing slivers a turn of Mutavault damage.) A
    // COLOURED manland (dual creature-land) has valuable fixing you tap for many turns before you'd
    // rather attack, so it falls through to the normal colour rank; holding it to attack is a
    // situational call left to the search, not this ordering.
    //
    // THE COST OF THE RESERVE (user report, 2026-08-25). Ranking it last is not free: on a board of
    // Cavern of Souls + Secluded Courtyard + 2x Mutavault, casting Sinew Sliver {1}{W} with replicate
    // {1}{W} available is EXACTLY payable (each Mutavault pays a generic, each colour land pays a
    // {W}) -- but the greedy takes Courtyard for the generic pip because 50 < 60, both white-capable
    // lands are gone, and the replicate reports max_count 0. Repro: slivers seed 30 / game-index 29,
    // turn 5. The manland's {C} can ONLY ever pay a generic pip, so spending it there costs nothing
    // in colour terms; what the reserve buys is the attack, and what it costs is a stranded colour.
    //
    // That trade was SWEPT (slivers, 1800 games, seeds 2002/3003/4004, MTG_MANLAND_RANK selector):
    // rank 5 and rank 30 are each +0.05 turns WORSE than the reserve at 60, and identical to each
    // other -- so the reserve is the whole effect and the rung it would move to is irrelevant. The
    // selector is deleted per the convention that a sweep scaffold goes once its result is recorded
    // (docs/design/viewer-feedback-2026-08-25.md). What SHIPPED instead is human-play-only: hold the
    // attack for the autonomous game, release it for the one payment a human's replicate follows.
    if (def.params.can_animate)
    {
        const std::vector<Color>& mprod = EffectiveProduces(s, active, def);
        bool has_colored = false;
        for (Color c : mprod) { if (c != Color::Colorless) { has_colored = true; break; } }
        // Released for one human-play payment that has a replicate to follow -- see the scope's
        // note in DecisionProviders.h. Falls through to the ordinary {C}-only rung (5) below.
        if (!has_colored && !g_release_manland_reserve) { return 60; }
    }
    // Storage-counter land (Dwarven Hold, Mercadian Bazaar): tap it LAST of all sources. Its burst is
    // a partial one -- it removes only the payment's remaining shortfall (see tap_source), so tapping
    // it after every basic/depletion source makes that shortfall MINIMAL, conserving the battery and
    // banking the rest. (The reserve already holds it entirely when the cost is payable without it.)
    if (def.params.storage_land) { return 62; }
    // A SCALED mana dork (Priest of Titania / Elvish Archdruid) grows with every creature of its
    // subtype cast this turn, so among sources it taps LAST: every payment that lands another Elf
    // first makes its eventual burst bigger. (Reservation already holds attack-capable dorks back;
    // this rank decides the order once dorks must pay, so a fixed Llanowar Elves taps before the
    // Priest.) Param-gated -> byte-identical for every deck without one. MTG_DORK_GROWTH=0 restores
    // the plain mono-colour rank.
    if (DorkGrowthEnabled() && IsScaledManaDork(def)) { return 61; }
    // A board-scaled LAND (Three Tree City: "{T}: Add {C}" plus "{2},{T}: Add {R} per Goblin") is a
    // mana MULTIPLIER, not a plain colourless source, and it belongs in the same reserve band as the
    // scaled dork above for the same reason: its yield grows with every creature the turn deploys.
    // IsScaledManaDork cannot see it (it demands IsCreature() and a ZERO feeder), so before this it
    // fell through to the plain ladder -- and since eaccc120 ranked a {C}-only source 5 ("spend
    // FIRST"), the best source on the board became the first one tapped, paying a one-generic pip
    // with a five-mana ability. See EngineFlags.h ScaledLandRankEnabled for the measured game.
    // Gated on the engine's own liveness predicate, mirroring the untap-burst tier just below: a
    // scaled mode that is dead, unaffordable, or no better than the basic {C} tap yields 0 and the
    // land keeps its ordinary rank, so no deck without a LIVE scaled land can move.
    if (ScaledLandRankEnabled() && ScaledManaNetYield(s, def) > 0) { return 61; }
    // An untap-land with a LIVE burst (Wirewood Lodge + a 2+ scaled Elf, see UntapBurstBestYield)
    // is reserved past even the scaled dorks: its burst yield reads the best Elf's count AT FIRE
    // TIME, so firing it absolutely last maximises the net. With no live burst it falls through to
    // the normal {C}-land rank below and its colourless is spent early -- USER 2026-08-20: "allow
    // using it for colourless early if there are no scaling sources at 2+ elves. Otherwise the
    // colourless could be stranded." (MTG_UNTAP_BURST=0 zeroes the net -> plain rank everywhere.)
    // BAND INTERACTION (strict-bar fix 2026-08-25, stompy block st139/st4/st351/...): 63 is "past
    // the scaled dorks" only against their STATIC rank (61). With the creature band on
    // (MTG_DORK_TAP_LAST) every dork moves to 64..66 (F..F+2), which INVERTED this invariant -- the
    // Lodge tapped before the Priest, its at-fire-time yield read an UNTAPPED board, and the burst
    // died (10 uniform 4->5 losses on held-out stompy; the {Priest, Symbiosis} pair became
    // unpayable). The burst land must out-rank the whole band; 63 is unchanged with the band off.
    if (def.params.untap_creature_cost.has_value()
        && UntapLandBurstNet(s, active, def) > 0)
    // +3 not +2 since the fuel tier (kFuelManaCreatureTapRank, 67) joined the band: the burst land
    // must stay past EVERY band member. Behaviour-identical for every existing deck (no deck holds
    // both a Lodge and a fuel dork; nothing else occupies 67/68).
    { return DorkTapLastEnabled() ? kGrowableManaCreatureTapRank + 3 : 63; }
    // Depletion lands (Saprazzan Skerry, Sandstone Needle) are deliberately NOT reserved (they are
    // RAMP you normally want to spend, so a blanket hold would misfire far more often than the rare
    // "wasted a counter" case helps) -- but they DO get a +1 nudge past their plain-land tier at the
    // bottom of this function (DepletionTapOrderEnabled), so a plain land of the same colour count
    // pays first and the counter is only spent when the cost needs it.
    if (def.params.is_filter || def.params.ramp_filter) { return 25; }
    const std::vector<Color>& prod = EffectiveProduces(s, active, def);
    const int amt = ManaProducedPerTap(def);
    // SOLE-COLOUR PROVIDER (MTG_SCARCE_COLOR_HOLD's rank half -- see ScarceColorHoldEnabled in
    // SpellEffects.h for the mw326 trace): a source that is the ONLY untapped provider of one of
    // its colours taps LAST OF THE LANDS (63: past every plain-land tier and the reserve tiers
    // 60-62, BEFORE the DTL creature band at 64+ -- a first build used 66, past the flat/grower
    // dorks, and measured +0.065 summed on held-out mirrorwing with the cost surviving every
    // firing-condition narrowing: ranked past the band, the tier tapped BODIES in the land's
    // place, contradicting the band's body-preservation doctrine on every mint turn; the fix
    // only ever needed the sole provider to route colour-flexible pips to OTHER LANDS first).
    // This is the same stranding class the colourless-before-mono tier
    // below fixes one level down: there a GENERIC pip ate a coloured source while colourless sat
    // untapped; here a colour-FLEXIBLE cost ate the board's lone {R} (mw326: Gruul Turf at the
    // fixed-multi 10 paid Gold Rush {1}{G} whole, so the breakpoint's Mirrorwing {R}{R} -- a cast
    // introduced MID-TURN by the mint, invisible to every plan-scope reserve -- stranded). Rank is
    // ordering, not exclusion: a cost that genuinely needs the colour still taps it. Covers mono
    // sources too (a lone Mountain should not pay a generic pip while any other source can).
    //
    // SCOPE (load-bearing, twice narrowed by measurement): fires ONLY (a) under a live plan
    // apply whose plan can introduce a mid-turn cast (PlanTraits::mid_turn_casts -- mint / flood
    // draw; the follow-on cast's pips are unknowable at payment time, the one shape no plan-scope
    // mechanism can cover), and (b) inside a LIVE payment whose own coloured need EXCLUDES the
    // scarce colour (g_pay_colored_need, published by TapForCostSharedOnce) -- the stranding is a
    // colour-FLEXIBLE cost eating the lone provider; a payment that needs the colour keeps
    // today's rank. Narrowing (a): the unscoped first build wholesale-churned the slivers/
    // antilife/dragonstorm trains (uniform 4->5 blocks) by reordering rollout-interior payments
    // (the MW gi75 distortion class). Narrowing (b): even mid-turn-cast-scoped, the
    // unconditioned tier measured +0.067 summed on held-out mirrorwing (12/12 cells worse) --
    // reordering every mint-plan payment costs more than the rare stranding it prevents.
    if (ScarceColorHoldEnabled() && g_pay_need_live && CurrentPlanTraits() != nullptr
        && CurrentPlanTraits()->mid_turn_casts)
    {
        int mine = 0;
        for (Color c : prod) { if (c != Color::Colorless) { mine |= 1 << static_cast<int>(c); } }
        if (mine != 0)
        {
            int counts[5] = {};
            for (const Permanent& p : s.battlefield)
            {
                if (p.controller_index != active || p.tapped) { continue; }
                const CardDefinition* d = CardDatabase::Instance().LookupCached(p.card);
                if (!d) { continue; }
                const bool dork = d->tmpl == CardTemplate::ManaDork
                                  && CanTapNow(p, s.battlefield)
                                  && GraveyardFuelLive(s, active, *d);
                if (!dork && !p.card.IsLand() && !d->params.mana_rock) { continue; }
                int seen = 0;
                for (Color c : EffectiveProduces(s, active, *d))
                {
                    const int ci = static_cast<int>(c);
                    if (ci >= 5 || (seen & (1 << ci))) { continue; }
                    seen |= (1 << ci);
                    ++counts[ci];
                }
            }
            for (int ci = 0; ci < 5; ++ci)
            {
                // Demote only for a colour this payment does NOT need (narrowing (b) above).
                if ((mine & (1 << ci)) && counts[ci] <= 1
                    && g_pay_colored_need[ci] == 0) { return 63; }
            }
        }
    }
    if (amt > 1 && static_cast<int>(prod.size()) > 1) { return 10; }  // bounce/fixed-multi: no choice
    const int ncol = static_cast<int>(prod.size());
    // A COLOURLESS-ONLY source is strictly LESS flexible than a mono-COLOURED one, so scarcity-first
    // must spend it EARLIER -- its mana pays generic pips only (no card in any deck has a {C} pip),
    // while a Forest's {G} pays generic AND green. Both read as "mono" (ncol == 1) and so both
    // ranked 10, which is not a tie: it let a GENERIC pip consume a coloured source while a
    // colourless one sat untapped, stranding the coloured pips of a later cast in the same turn.
    // Found by the user 2026-08-24 on StompySurprise s9 T2: `land=Wirewood Lodge; cast Sol Ring,
    // Natural Order` is EXACTLY payable (Lodge {C} pays Sol Ring's {1}; Forest + Llanowar make
    // {G}{G}; Sol Ring's {C}{C} pays the {2}) and the engine enumerated it, but the rank tie put
    // Forest ahead of the Lodge for Sol Ring's generic pip, leaving Natural Order one green short --
    // silently dropped in autonomous play, a hard "not enough mana" reject in the viewer. Nothing
    // downstream rescues it: the backtracker is per-PAYMENT (Sol Ring's own payment succeeds
    // whichever source it takes) and BatchPrepayMainCasts, the cross-cast solver, declines on a
    // line containing a producer (PP_PRODUCER). The colourless tiers ABOVE keep their reserves --
    // an animated manland (Mutavault, 60), a storage land (62) and a live untap-burst Lodge (63)
    // are all deliberately held back and are returned before this point.
    bool any_colored = false;
    for (Color c : prod) { if (c != Color::Colorless) { any_colored = true; break; } }
    int rank = (!prod.empty() && !any_colored) ? 5                    // {C}-only: least flexible
             : (ncol <= 1 ? 10 : ncol * 10);                          // mono=10 dual=20 tri=30 rainbow=50
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
    // Depletion land (Saprazzan Skerry, Sandstone Needle): its tap SPENDS a counter -- finite, and
    // the last one sacrifices the land -- so it taps one slot past its plain-tier peers (mono 10->11,
    // dual 20->21), same shape as the drip-land nudge above. Without this, a fresh basic and a
    // last-counter Skerry tie at 10 and battlefield order decides: treasure_hunt seed 8 T5 paid Fiery
    // Islet's sac-to-draw {1} by killing the Skerry (2 produced for 1 needed, {U} wasted) with an
    // untapped Island on the board, which then stranded the [Island, sac Islet, retrace Throes] line.
    // The within-tier "more counters first" half lives in TapForCostSharedOnce (per-permanent; this
    // ranking is per-DEFINITION and cannot see counters). USER 2026-08-27: "We should prefer those
    // with more counters before those with less." MTG_NO_DEPLETION_TAP_ORDER disables both halves.
    if (DepletionTapOrderEnabled() && def.params.enters_tapped_with_depletion > 0) { rank += 1; }
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
    // ATTACK-CRITICAL DORK EXCLUSION (MTG_AL_FETCH_ATK, measurement lever 2026-08-21): a mana
    // dork that is our SOLE attack-capable creature is not a free source -- tapping it for a
    // colour forfeits the attack, the Exalted bonus, and the Invigorate-pump line that is this
    // deck's kill. Counting it as coverage is how the fetch rank passed over Stomping Ground
    // (the deck's only R among fetchables) while holding Aria of Flame {2}{R}: "R covered" was
    // Hierarch's tap, whose R can never be spent on a turn that also attacks (the gi244 class:
    // the T4 Aria+Invigorate+attack kill dies at the T2 fetch pick, +1 turn, both filter arms).
    // With other attackers on board the dork's mana IS free and everything counts as before.
    // ^-- MEASURED REJECTION (decouple ensemble, salts 1-4, 8000 games/salt, 2026-08-21): the
    // exclusion fixes the narrative game's pick (Stomping Ground for the held Aria) but measures
    // +0.0008..+0.0018/game WORSE than stock on EVERY salt -- the dork's colours are genuine
    // coverage more often than they are attack forfeits. Kept as an instrument only.
    static const bool s_fetch_atk = EnvOn("MTG_AL_FETCH_ATK");   // DEFAULT OFF (measured rejection)
    int n_attack_capable = 0;
    if (s_fetch_atk)
    {
        for (const Permanent& p : state.battlefield)
        {
            if (p.controller_index != controller_index) { continue; }
            if (p.card.IsCreature()) { ++n_attack_capable; }
        }
    }
    for (const Permanent& p : state.battlefield)
    {
        if (p.controller_index != controller_index) { continue; }
        const CardDefinition* d = CardDatabase::Instance().LookupCached(p.card);
        if (!d) { continue; }
        if (!(d->card.IsLand() || d->tmpl == CardTemplate::ManaDork || d->params.mana_rock)) { continue; }
        if (s_fetch_atk && d->tmpl == CardTemplate::ManaDork && p.card.IsCreature()
            && n_attack_capable == 1)
        { continue; }   // sole attacker: its tap is an attack forfeit, not coverage
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

// Verse damage the CAST of this payload itself would deal: each on-battlefield CAST-PAYOFF
// permanent (verse_damage, Aria of Flame) triggers on an instant/sorcery cast for
// (counters + 1) real damage -- Remedy-independent, so it always lands. The lethal checks
// below must count it: at gi=184 the opponent sat at 7 with a fresh Aria out, and
// "6 alt + 0 ready attack" read non-lethal while the actual cast deals 6 + 1 verse = 7.
// Returns 0 for a non-instant/sorcery payload (no trigger fires).
static int VerseDamageFromCast(const GameState& s, int controller, const CardDefinition& def)
{
    if (!def.card.IsInstant() && !def.card.IsSorcery()) { return 0; }
    int total = 0;
    for (const Permanent& p : s.battlefield)
    {
        if (p.controller_index != controller) { continue; }
        const CardDefinition* d = CardDatabase::Instance().LookupCached(p.card);
        if (d && d->params.verse_damage) { total += p.verse_counters + 1; }
    }
    return total;
}

// Total power of the controller's creatures that can still attack this turn (untapped, not
// summoning-sick). Used by the Reverent-Silence lethal checks below so the "free payload + this
// turn's swing finishes the opponent" formula is identical at emission and at auto-fire time.
// Counts BOTH attack shapes (the lethal-projection precedent above): SWARM (summed power) and
// ALONE (best single attacker + the full Exalted count) -- a lone 0-power Ignoble Hierarch
// swings for 1 under its own Exalted, and reading it as 0 scored the gi=230 kill
// (opp 8 = 6 alt + 1 verse + 1 exalted swing) as non-lethal.
static int ReadyAttackPower(const GameState& s, int controller)
{
    // PHASE-HONEST: "this turn's swing" only exists while combat is still ahead. Post-combat
    // (the m2 harvest, the collapsed-main cast pass -- its attack runs BEFORE the casts) an
    // untapped creature that did not attack contributes NOTHING this turn; counting it fired
    // a NON-lethal Reverent Silence at opp 7 = "6 alt + 2 phantom exalted" (regression d0
    // gi=61/161: the wipe stripped the Remedy, the next Aria GIFTED 10, win-5 became a loss).
    // Both worlds maintain GameState::phase (executor: GameEngine; rollout: SimulateCombat /
    // SimulateEndAndStartNextTurn), so the gate is faithful in the apply path too.
    if (s.phase == Phase::PostCombatMain || s.phase == Phase::Combat
        || s.phase == Phase::Ending)
    { return 0; }
    int atk = 0;
    int best_single = 0;
    bool any_ready = false;
    for (const Permanent& p : s.battlefield)
    {
        if (p.controller_index == controller && p.card.IsCreature() && p.CanAttack())
        {
            any_ready = true;
            int pw = p.EffectivePower();
            if (pw > 0) { atk += pw; }
            best_single = std::max(best_single, std::max(pw, 0));
        }
    }
    if (any_ready)
    {
        const int alone = best_single + CountExalted(s.battlefield, controller);
        if (alone > atk) { atk = alone; }
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
        && s.players[1 - controller].life <= def.params.alt_lifegain_cost + ReadyAttackPower(s, controller)
                                             + VerseDamageFromCast(s, controller, def))
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
    return s.players[1 - controller].life <= def.params.alt_lifegain_cost + ReadyAttackPower(s, controller)
                                             + VerseDamageFromCast(s, controller, def);
}

// CastEnablerFirst: no override -- the enabler-first sequencing this provider used to hand-code is
// now the generic param-derived ENABLES tier (lifegain_to_loss -> rank 0,
// docs/design/card-dependency-map.md); the generic rank also orders Aria (verse_damage, 19) before
// the instants that feed it and Reverent Silence (destroy_all_enchantments, 30) last.
//
// ---- USER REVIEW of this deck's cast order, 2026-08-18 -------------------------------------
// Two corrections to the generic ranking, both DERIVED from card params rather than name-keyed so
// they generalise to any deck holding the same shapes. Gated on MTG_AL_ORDER, default off --
// order is a reviewed judgement, and this is the proposal, not an adoption.
//
//   * "Swords should go after Invigorate (because of the invigorate swords play)." Swords to
//     Plowshares pays its controller life EQUAL TO THE CREATURE'S POWER, which a Remedy flips
//     into damage -- so every spell that RAISES power is an enabler of a bigger Swords, and the
//     generic principle 3 (enabler before payoff) puts the pump first. Both sit at rank 20 today,
//     so the tie was being broken by plan order.
//   * "Skyshroud cutter can go lower in the cast order, to fit in with m2." A vanilla creature
//     whose whole function is its alt-cost GIFT is a damage spell wearing a creature's clothes;
//     ranking it at the rank-10 CREATURE tier casts it ahead of the deck's actual business.
static bool AlOrderReviewEnabled()
{
    // ADOPTED default-on (USER, 2026-08-18): the reviewed Anti-Lifegain order (Swords 21 after
    // the pumps, Skyshroud Cutter 24 with the m2 group, Reverent Silence -> Main2). Measured with
    // MTG_ORDER_RECHECK in the scoped arm: train green-or-flat, held-out 7 green / 5 flat /
    // 0 red, per-game 12 faster / 0 slower. =0 reverts.
    static const bool on = EnvOn("MTG_AL_ORDER", true);   // DEFAULT ON; =0 disables
    return on;
}

int AntiLifegainProvider::CastOrderRank(const GameState& s, const CardDefinition& def) const
{
    if (AlOrderReviewEnabled())
    {
        const CardParams& p = def.params;
        // WHICH SIDE of the pumps Swords belongs on is not obvious in THIS engine, so it is a
        // measured constant rather than an assumed one. The USER's play is "Invigorate, then
        // Swords" -- but the engine does not realise it that way: TryPumpThenSwordsRedirect
        // (SpellEffects.h) pulls a free alt-cost Invigorate OUT OF HAND during Swords' RESOLUTION
        // and puts the +4/+4 on the Swords target, which is the only way Invigorate ever pumps an
        // OPPONENT creature (it is target_own_creature otherwise). That redirect needs Invigorate
        // still in hand -- so a cast order that fires Invigorate FIRST defeats the very play the
        // ordering was asked for. Mostly moot at d0 (safe alt payloads are auto-fired after the
        // casts, not enumerated), live at search nodes where the speculative emission makes
        // Invigorate a real cast. MTG_AL_SWORDS_RANK: 21 = after the pumps, < 20 = before them.
        static const int s_swords_rank = EnvInt("MTG_AL_SWORDS_RANK", 21);
        if (p.controller_lifegain_equals_power) { return s_swords_rank; }
        if (p.alt_lifegain_cost > 0 && def.card.IsCreature() && !p.lifegain_to_loss) { return 24; }
        // REDUNDANT ENABLER demotion (MTG_AL_RED_ENABLER, measurement lever 2026-08-21): with a
        // lifegain->loss effect ALREADY LIVE, a second copy is a near-dead card (the USER's
        // 2026-08-07 interchangeable-required ruling: "you only need one at a time" -- encoded
        // for discard, but the greedy's enabler-tier rank 0 never learned it). On a 4-mana turn
        // the greedy tail cast the redundant second Remedy AHEAD of Aria of Flame, so every
        // projected kill turn fizzled and hold-lines read one turn worse than reality (the
        // gi244 class). A STATE-CONDITIONAL rank, not a static one (USER 2026-08-21: fully
        // static labels are likely flawed): first copy keeps the enabler tier, a copy cast
        // under a live effect drops behind the payoffs.
        // ^-- MEASURED INERT (decouple ensemble, salts 1-4, 2026-08-21): aggregate digit-identical
        // to stock on every salt -- the firing state (2nd copy in hand, 1st live, both castable
        // same turn) is too rare to move the metric. Kept default-off.
        static const bool s_red_enabler = EnvOn("MTG_AL_RED_ENABLER");   // DEFAULT OFF (measured inert)
        if (s_red_enabler && p.lifegain_to_loss
            && ::RemedyActive(s, s.active_player_index)) { return 25; }
    }
    return GenericProvider::CastOrderRank(s, def);
}

// MTG_AL_PHASE / MTG_AL_SSM -- the REAL m1/m2 split for Anti-Lifegain (USER ruling 2026-08-21:
// "There should be a real split between the two, not a re-evaluation"). Until now this deck's
// m2 labels were classification-only: the pre-combat Main2 filter never ran (no
// ClassifiesMainPhases opt-in), so every cast was enumerated m1 and the second main merely
// re-offered leftovers. PHASE enforces the labels (Birds/Fiery Justice/Skyshroud Cutter by the
// base rules, Reverent Silence by the 2026-08-18 override below); SSM makes the search's
// interior second main SEARCHED, scoped to the split being live -- without the split the
// interior m2 is leftovers-only and searching it measured as pure budget churn (train
// d3 +0.0133/+0.0133, all 7 diverging games recover at 4-16x budget; 2026-08-21). Same
// two-lever attribution shape as the FiveColour adoption (Fc5PhaseEnabled / MTG_5C_SSM).
static bool AlPhaseEnabled()
{
    // ADOPTED DEFAULT ON 2026-08-22 (USER). The split shipped only once its two rule defects were
    // root-caused out of it: condemnation's pass-is-not-a-decline + no-enabler-live gaps
    // (MTG_CONDEMN_*_EXEMPT) and the classifier's combat-cost blindness (MTG_PHASE_DAMAGE_BOTH).
    // Held-out (8000 games, searched): phase alone +15.00 turns, the full stack -5.00 (6 keys
    // better / 2 worse). MTG_AL_PHASE=0 reverts. See docs/design/antilife-main-phase-split.md 21t.
    static const bool on = EnvOn("MTG_AL_PHASE", true);
    return on;
}

bool AntiLifegainProvider::ClassifiesMainPhases() const
{
    return AlPhaseEnabled();
}

bool AntiLifegainProvider::SearchedSecondMainInSearch() const
{
    // Scoped to the phase split being live, exactly as FiveColour's hook is: a real split hands
    // the interior m2 real deferred decisions; without it the searched interior m2 is dilution.
    // Overridable PER JOB (heurarm) so both arms of the "drop the last greedy solve" A/B run in ONE
    // pooled batch instead of one batch per arm -- unset everywhere => the env default, byte-identical.
    //
    // 2026-08-22: the recorded "+13 turns vs keeping greedy" rejection was measuring TWO things at
    // once. Split by call site (MTG_SSM_SITE), the BRANCH site -- the actual decision -- is
    // BYTE-IDENTICAL to the greedy Solve on this deck over 26,000 games (6000 train d3+d5, 8000
    // held-out on all four overnight seeds, 12,000 across four shuffle salts), with the searched
    // path demonstrably firing (163 solves / 500 games, not a dead path). All +13 turns came from
    // the ROLLOUT site, which SearchesRolloutSecondMain() above declines. So with that override in
    // place this hook is free: it removes the last greedy DECISION from AL's main-phase search
    // without changing a single game. See docs/design/antilife-main-phase-split.md 2026-08-22y.
    // ADOPTED DEFAULT ON 2026-08-22 (USER). MTG_AL_SSM=0 reverts.
    static const bool env_on = EnvOn("MTG_AL_SSM", true);
    return heurarm::Flag(heurarm::AL_SSM, env_on) && AlPhaseEnabled();
}

bool AntiLifegainProvider::SearchesRolloutSecondMain() const
{
    // The ROLLOUT site is the leaf estimator's PLAYOUT POLICY, not a decision -- a plan that is
    // scored, never played. Declined here since 2026-08-22 on measurement: searching it cost +12
    // turns at d3 / +1 at d5 per 3000 train games, robust across 5 shuffle realisations and 4
    // DECOUPLED search salts, and NON-MONOTONE (removing the rollout's m2 entirely costs +7, so
    // greedy is an interior optimum -- more playout fidelity is not more ranking accuracy).
    // ~2/3 of the cost was budget dilution: the rollout charges the shared budget per simulated
    // turn-step, starving the outer candidate loop.
    //
    // USER 2026-08-23: "We shouldn't have any greedy within the searched window." MTG_AL_SSM_ROLLOUT
    // is the lever to re-open that, default OFF until the measurement says it can ship -- and the
    // strict-win route to try first is MTG_M2_CAP1, which caps the interior solve to depth 1 so it
    // is still SEARCHED (no greedy pick) without compounding against the iterative-deepening pass.
    static const bool env_on = EnvOn("MTG_AL_SSM_ROLLOUT");
    return heurarm::Flag(heurarm::AL_SSM_ROLLOUT, env_on) && SearchedSecondMainInSearch();
}

bool AntiLifegainProvider::CondemnsPassedMainPhase() const
{
    // ORDER CONDEMNATION for the split (USER 2026-08-21: "within a turn all breakpoints and
    // phases should use the same condemnation" -- extend the FiveColour doctrine, AL first).
    // Main 2 continues with main 1's condemnation list instead of re-litigating the hand;
    // membership decided once, at m1 (the base-hook contract). Every consumption site gates on
    // MainPhaseFilterActive, so this binds only with the split live -- the && here just makes
    // the scoping explicit, mirroring MTG_AL_SSM.
    // MEASURED INERT-AT-COST (2026-08-21, decouple ensemble salts 1-2, 8000 games/salt over
    // PHASE): quality +0 (0w/0b) / +3 (2w/0b) -- the lever BINDS (126k searched drops per 300
    // games, MTG_ROLLOUT_STATS) but everything it deletes is a line AL's search never preferred
    // -- at +14-16% compute (the per-rollout StampM1Hand pool walks outweigh the m2-shrink
    // savings on AL's small m2 sets; 5C's was perf-neutral). The OLD "m2 re-offer recovers
    // prune losses" rejection no longer reproduces post-fixes; the re-offer just no longer
    // matters either way.
    // ^-- THAT INERT-AT-COST VERDICT IS RETRACTED, and it was an artifact of measuring a BROKEN
    // condemnation. It was taken before the two rule gaps were found (pass-is-not-a-decline,
    // no-enabler-live): a rule that was deleting real lines and gaining real ones netted to
    // "inert". With both exemptions in and on top of PHASE+DAMAGE_BOTH+DORK, dropping
    // condemnation costs 5 turns per 8000 held-out AL games and 4 extra worse games -- one of
    // them a WIN TURNING UNWON (al_d5_s7007 gi10, T7 -> loss). ADOPTED DEFAULT ON 2026-08-22
    // (USER). MTG_AL_CONDEMN=0 reverts. See antilife-main-phase-split.md 21u.
    static const bool on = EnvOn("MTG_AL_CONDEMN", true);
    return on && AlPhaseEnabled();
}

bool AntiLifegainProvider::CondemnsConsideredAtBreakpoint() const
{
    // BREAKPOINT CONDEMNATION (the doctrine's other half). AL's only mid-phase breakpoint class
    // is the Idyllic Tutor acquisition (USER 2026-08-21) -- which is exactly the
    // value-changing-acquisition shape the base hook warns about (a declined Silence becomes
    // live once the tutor fetches a Remedy), i.e. the Dragonstorm-measured hazard, so the
    // PREDICTION was unsafe here.
    // MEASURED INERT (2026-08-21, decouple ensemble salts 1-2 over PHASE+CONDEMN): +2 (2w/0b) /
    // -1 (0w/1b) per 8000 -- the breakpoint is too rare on AL for either the hazard or any
    // benefit to register. Kept as an instrument; NOT adopted.
    static const bool on = EnvOn("MTG_AL_BP_CONDEMN");
    return on && AlPhaseEnabled();
}

bool AntiLifegainProvider::PhaseFilterRootTurnOnly() const
{
    // ROOT-TURN AUTHORITY for the split -- MEASURED AND REJECTED (2026-08-21): it recovers 39 of
    // the filter-everywhere arm's 53 held-out worse games (the projection-distortion class) but
    // CREATES ~60 new ones elsewhere (held-out 82 worse : 2 faster vs filter-everywhere's 53:29)
    // -- filtered root candidates scored by unfiltered future projections is mixed semantics the
    // rankings can't survive. NOT cache poisoning (memos-off battery identical). Kept as a
    // measurement instrument only; MTG_AL_PHASE_ROOT=1 arms it.
    static const bool on = EnvOn("MTG_AL_PHASE_ROOT");   // DEFAULT OFF (measured rejection)
    return on && AlPhaseEnabled();
}

// "Reverent Silence should be cast last, so it should be m2." (USER 2026-08-18.) The generic
// classifier reaches it through the DOUBT default, which keeps a card pre-combat -- but this one
// is not in doubt: it destroys the deck's own enchantments, so nothing it enables can be spent
// afterwards in the same main, and the deck's whole pre-combat build-up wants to happen first.
// Rank 30 already puts it last WITHIN a phase; this puts it in the later phase as well.
std::optional<DecisionProvider::MainPhase>
AntiLifegainProvider::MainPhaseOverride(const GameState&, const CardDefinition& def) const
{
    if (AlOrderReviewEnabled() && def.params.destroy_all_enchantments)
    { return MainPhase::Main2; }
    // MANA DORKS stay Main1 under the enforced split (MTG_AL_DORK_M1, measurement lever for the
    // 2026-08-21 held-out dig): the base classifier's sick-body rule sends Birds to Main2, but a
    // dork is this deck's DEVELOPMENT cast -- deleting it from the pre-combat candidate set
    // consistently flipped hold-lines into early dumps (38 budget-immune +1-turn games on the
    // PHASE held-out, e.g. s6006 gi8: T2 "Remedy+Invigorate now" over "Birds, hold for the T3
    // Remedy+Invigorate+Cutter kill"). Casting a sick dork pre-combat is combat-neutral, so
    // Main1 costs the filter nothing it exists to buy.
    if (AlPhaseEnabled() && def.tmpl == CardTemplate::ManaDork
        && EnvOn("MTG_AL_DORK_M1", true))   // DEFAULT ON within the split; =0 for the A/B
    { return MainPhase::Main1; }
    return std::nullopt;
}

bool AntiLifegainProvider::ShouldEmitRiskyAltPayload(const GameState& s, int controller,
                                                     const CardDefinition& def, bool at_cast_time) const
{
    if (DecisionUnpruned(UnprunedGate::AltPayload)) { return true; }   // unpruned A/B: let the search judge the wipe.
    // GREEDY-ONLY gate (docs/design/card-dependency-map.md). Reverent Silence's destroy-all-
    // enchantments wipes our OWN Aria/Remedy; the greedy second-main/rollout policy overvalues
    // the immediate 6 and bricks the combo (regressions gi=36/84 -- and DROPPING this gate
    // outright, USER's first-choice experiment 2026-08-15, re-bricked exactly that class: smoke
    // d0 5.5650 -> 5.9270 with outright losses). So the GREEDY consumer keeps the tight
    // conditions below, while SEARCH nodes bypass this gate entirely (CollectActions'
    // search_risky_live emission: Remedy live -> emit, the search + the SUBSET-level lethality
    // in SubsetHasUnbackedAltPayload judge the wipe -- per-card lethality at emission cannot see
    // a same-subset Aria's converted ETB landing first). The cast-time re-check in the rollout
    // apply still consults THIS gate and stays accurate for search-committed lines: the
    // canonical order casts the wipe LAST (rank 30), so the subset's converted damage has
    // already landed and the per-card test evaluates against the reduced life total.
    // Emit only when:
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

    // (c) REPLACEMENT accounting -- the RUN-OUT guard (USER 2026-08-18, second review round:
    // "we just need to allow it to not be cast when this will run us out without finishing the
    // opponent"). Two prior arms teach what "run out" means here, and hand-replacement alone is
    // NOT it: the bare "replacement in hand -> emit" arm measured d0 +0.0520 TWICE (the original
    // (c) experiment and its re-run with the recheck active -- byte-for-byte the same aggregate),
    // and the explain shows why: the greedy cashes the free 6 while BACKED PAYLOADS the enabler
    // still owes are pending -- gi46 held Invigorate (its flipped +3 needs the Remedy the wipe
    // kills), gi30 held Aria (its flipped ETB-10 next turn needs it). The wipe "runs us out" of
    // the enabler those payloads cash through, replacement or no. So (c) fires only when the wipe
    // STRANDS NOTHING:
    //   * a replacement lifegain_to_loss card is in HAND (the recheck orders it after the wipe --
    //     hence the OrderRecheckEnabled() condition; without it the replacement is cast
    //     enabler-first and dies to its own wipe, the original brick), and
    //   * no OTHER backed payload waits in hand (a gift alt-cost, a gift rider, a gift ETB --
    //     param-derived: Invigorate / Skyshroud Cutter / Fiery Justice / Aria of Flame), and
    //   * no own non-enabler enchantment is on the board (the wipe destroys a live Aria's verse
    //     engine; only the enabler being re-armed may die).
    // The chain still terminates itself: at the SECOND wipe's emission the hand holds no further
    // enabler, so only lethality (b) can justify it -- the USER's rule exactly.
    //
    // SCOPED TO THE CAST-TIME GUARD ONLY (at_cast_time): even strand-guarded, opening this term
    // at EMISSION hands the line to the greedy, and the held-out overnight read it red at d0
    // (3/4 keys, 39:16 slower -- the residual class DRAWS INTO its Aria/Cutter a turn after a
    // "clean-hand" wipe, which no cast-time hand test can see). At cast time only, the greedy
    // never initiates the chain; a SEARCH-committed chain (search_risky_live emission, the
    // search judged the whole line) is allowed to execute instead of being vetoed back to (a)/(b).
    if (at_cast_time && OrderRecheckEnabled())
    {
        bool replacement = false, strandable = false;
        for (const Card& c : s.players[controller].hand)
        {
            const CardDefinition* d = CardDatabase::Instance().LookupCached(c);
            if (!d) { continue; }
            const CardParams& p = d->params;
            if (p.lifegain_to_loss) { replacement = true; continue; }
            if ((p.alt_lifegain_cost > 0 && !p.destroy_all_enchantments)
                || p.opponent_lifegain > 0 || p.etb_opponent_lifegain > 0)
            { strandable = true; }
        }
        for (const Permanent& p : s.battlefield)
        {
            if (p.controller_index != controller
                || !p.card.HasType(CardType::Enchantment)) { continue; }
            const CardDefinition* d = CardDatabase::Instance().LookupCached(p.card);
            if (d && !d->params.lifegain_to_loss) { strandable = true; break; }
        }
        if (replacement && !strandable) { return true; }
    }
    // (b) lethal in combination with this turn's attackers
    return s.players[1 - controller].life <= def.params.alt_lifegain_cost + ReadyAttackPower(s, controller)
                                             + VerseDamageFromCast(s, controller, def);
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
    const std::pair<int,int> lb = ComputeLordBonus(p.card, s, active, animated, &p);
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
// Aria of Flame's ETB gift, priced by conversion state (stack-vs-base dig 2026-08-15, gi=136).
// The ETB gives the opponent `gift` life. CONVERTED (a lifegain->loss enabler live) that is
// `gift` damage on resolution -- credit it. UNBACKED it actively heals the opponent AND spends
// a future finisher, but the generic eval gave it the flat +1 fallback, so a tie-break
// (MoveOrderPlans static value) preferred a plan carrying a spare Aria over the same plan
// without it -- the [tie] audit's "free action" exactly, and gi=136 turned a base win-7 into a
// LOSS gifting +10 at T3. Half-weight penalty pre-conversion: the verse engine is real value
// (the first pre-Remedy Aria is routinely a strictly-better cast and stays one -- selection is
// by win turn, this value only orders/breaks ties), so deter the SPARE copy without zeroing
// the engine.
bool AntiLifegainProvider::ArchetypeCardValue(const GameState& s, const CardDefinition& def,
                                              int DMG, int& out) const
{
    const int gift = def.params.etb_opponent_lifegain;
    if (gift <= 0) { return false; }
    bool enabler_live = false;
    for (const Permanent& p : s.battlefield)
    {
        if (p.controller_index != s.active_player_index) { continue; }
        const CardDefinition* d = CardDatabase::Instance().LookupCached(p.card);
        if (d && d->params.lifegain_to_loss) { enabler_live = true; break; }
    }
    out = enabler_live ? (1 + gift) * DMG : DMG - (gift * DMG) / 2;
    return true;
}

// The exalted-aware hold, shared by every provider whose deck runs Exalted on a 0-power body, and
// honoured in lockstep by all three combat sites that gate on ShouldAttackWith: the PendingAttackDamage
// projection, the rollout's ApplyCombat, and the real DeclareAttackers.
// Extracted from AntiLifegainProvider so MirrorwingProvider (4x Ignoble Hierarch) can apply the same
// rule: it became load-bearing there the moment the whole-turn dork reservation started LEAVING
// those dorks untapped, since an untapped 0-power dork that then swings alongside the real attacker
// deals nothing AND cancels the lone-attacker bonus -- a strict 1-damage loss, exactly the
// "reservation only pays off with the exalted-aware attack declaration" caveat in
// docs/design/mana-source-reservation.md.
static bool ExaltedAwareShouldAttack(const GameState& s, const Permanent& p)
{
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

bool AntiLifegainProvider::ShouldAttackWith(const GameState& s, const Permanent& p) const
{
    // Default ON; off-switch MTG_NO_EXALTED_ATTACK reverts to generic attack-with-everything
    // (byte-identical to the pre-fix baseline) for A/B. Net win: +2-3% d0 wins and faster searched
    // avgs on Anti-Lifegain (the only exalted deck at the time), 0 win<->loss. A handful of searched
    // games win a turn LATER, but that was shown to be fetch-shuffle DRAW VARIANCE, not a bug: the
    // more accurate exalted valuation flips an early land tie-break, a fetchland reshuffles, and the
    // game draws differently. Among 462 games with IDENTICAL draw sequences, ON never wins later (0
    // regressions); every turn-later game has a divergent post-fetch draw. See the reservation design
    // doc's exalted section.
    static const bool enabled = !EnvOn("MTG_NO_EXALTED_ATTACK");
    if (!enabled) { return true; }
    return ExaltedAwareShouldAttack(s, p);
}

bool MirrorwingProvider::ShouldAttackWith(const GameState& s, const Permanent& p) const
{
    // Same rule, same switch (see ExaltedAwareShouldAttack): this deck plays 4x Ignoble Hierarch, so
    // a 0-power Elvish Mystic / second Hierarch swinging next to Zada trades the Exalted +1/+1 for
    // nothing. Only bites a dork the whole-turn reservation left untapped -- before that they were
    // nearly always tapped for mana, which is why the deck never needed the rule.
    static const bool enabled = !EnvOn("MTG_NO_EXALTED_ATTACK");
    if (!enabled) { return true; }
    return ExaltedAwareShouldAttack(s, p);
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

// ---- AurasProvider ----------------------------------------------------------

bool AurasProvider::HasAnyDigSource (const GameState& s) const { return ::HasAnyDigSource(s); }
bool AurasProvider::ShouldConsiderDig(const GameState& s) const
{
    // The DEFAULT/HORIZON heuristic only -- at the committed turn the dig is a searched axis
    // (DigDecisionSearched: the enumerator fans dig/no-dig variants and the rollout decides;
    // USER 2026-08-28: "we can certainly have a heuristic to help make the decision... but it
    // should be searched otherwise"). Two shapes where the sac is worth a card by default:
    //   * the land is genuinely SURPLUS: with >= 4 lands controlled the post-sac board (>= 3)
    //     still casts everything in the deck (the curve tops at Ancestral Mask, MV 3) -- the
    //     human reference line s21/gi20 exactly (dig on T4/T5 after the fourth drop);
    //   * the hand is EMPTY OF GAS (no castable nonland): with nothing to cast, drawing toward
    //     action is the only line, so dig from 2 lands up (the generic >= 2 don't-strand floor;
    //     USER: the surplus-only gate is "a little too restrictive for cases where your hand is
    //     completely empty of gas").
    // The dig loops already require the cost to be affordable AFTER the turn's casts (surplus
    // mana); this gate only encodes the land-count / gas judgement.
    if (DecisionUnpruned(UnprunedGate::Dig)) { return ::HasAnyDigSource(s); }
    int lands = 0;
    for (const Permanent& p : s.battlefield)
    {
        if (p.controller_index == s.active_player_index && p.card.IsLand()) { ++lands; }
    }
    if (lands >= 4) { return true; }
    if (lands < 2)  { return false; }
    for (const Card& c : s.ActivePlayer().hand)
    {
        const CardDefinition* d = CardDatabase::Instance().LookupCached(c);
        if (!d || d->card.IsLand()) { continue; }
        if (d->card.m_mana_cost.ManaValue() <= lands) { return false; }   // castable gas in hand
    }
    return true;
}
std::string AurasProvider::SelectDigSource(const GameState& s, const ManaPool& pool, bool& out_is_sac) const
{
    return ::SelectDigSource(s, pool, out_is_sac);
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
// The SHED ORDER is the untruncated ranking (see DecisionProvider::CleanupDiscardShedOrder): the
// top-1 narrowing below is about the searched FAN, and a 15-25-card Treasure Hunt cleanup routinely
// sheds eight cards -- every one of which this deck has an opinion about.
std::vector<int> TreasureHuntProvider::CleanupDiscardShedOrder(
    const GameState& s, const std::vector<std::string>* required_pieces) const
{
    return CleanupDiscardFullRanking(s, required_pieces);
}

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

// MTG_KNIGHTS_ORDER -- the USER-reviewed Knights cast order (2026-08-19, ruling recorded verbatim
// in docs/design/cast-order-rankings.md). Default OFF pending measurement; on adoption this
// becomes a default-on read with an off switch, as the other adopted per-deck rules are. Knights
// has NO order-opaque cards, so the rank sort governs every cast set -- ranks are the whole
// delivery. "Everything should be main 1" (USER) is today's behaviour (uses_second_main=no); the
// future rule-derived main-phase split (does THIS cast affect combat THIS turn, given the board --
// Adeline out, lords with bodies to pump) is a Phase 2 information-hiding item, recorded in the
// rankings doc, deliberately NOT encoded here.
static bool KnightsOrderEnabled()
{
    // ADOPTED default-on (USER, 2026-08-19). Held-out alone: 8/12 overnight keys green, 4 flat,
    // 0 red; combined with the d0-scoped MTG_ACQ_DIG: d0 -0.0035..-0.0075 on all four held-out
    // seeds, searched never worse. slivers_vial byte-identical at every tier (no sliver carries
    // either param). =0 reverts.
    static const bool on = EnvOn("MTG_KNIGHTS_ORDER", true);   // DEFAULT ON; =0 disables
    return on;
}

int VialProvider::CastOrderRank(const GameState& s, const CardDefinition& def) const
{
    if (KnightsOrderEnabled())
    {
        const CardParams& p = def.params;
        // Worthy Knight: "whenever you cast a Knight spell, create a Human token" -- before the
        // tribe, so every same-turn Knight cast is +1 body. Before the Contender too: WK-first
        // still mints a token off the Contender's own cast, the reverse forfeits it.
        if (p.cast_trigger_creates_tokens > 0 && !p.cast_trigger_subtype.empty()) { return 8; }
        // Acclaimed Contender: ETB dig gated on controlling ANOTHER creature of the required
        // subtype. USER: "before others if it is played, except when you don't yet control a
        // knight." Board already satisfies the gate -> 9 (right after the watcher: the dig
        // resolves early and MTG_ACQ_DIG can spend the turn's leftover mana on the dug card);
        // gate not yet met -> 12, after the other creatures, so a same-turn Knight cast
        // satisfies it before the Contender enters.
        if (p.etb_dig_count > 0 && !p.etb_dig_requires_subtypes.empty())
        {
            for (const Permanent& perm : s.battlefield)
            {
                if (perm.controller_index != s.active_player_index) { continue; }
                if (!perm.card.IsCreature())                        { continue; }
                for (const std::string& want : p.etb_dig_requires_subtypes)
                    for (const std::string& cs : perm.card.m_subtypes)
                        if (cs == want) { return 9; }
            }
            return 12;
        }
        return GenericProvider::CastOrderRank(s, def);
    }
    return GenericProvider::CastOrderRank(s, def);
}

const char* VialProvider::CastOrderTierName(int rank) const
{
    if (!KnightsOrderEnabled()) { return nullptr; }
    switch (rank)
    {
        case 8:  return "CAST-TRIGGER WATCHER (Worthy Knight): before the tribe -- every later Knight cast is +1 body";
        case 9:  return "GATED ETB DIGGER (Contender), gate already met on board: early, so the dug card can be cast this turn";
        case 12: return "GATED ETB DIGGER (Contender), gate NOT met: after the other creatures, so they satisfy it";
        default: return nullptr;
    }
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
    // FireEtbWatchers EXACTLY: for each entering NONTOKEN Dragon, every OTHER Lathliss first makes a 5/5
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
        if (!AttackWith(s, q))                             { continue; }
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
        const int lp = ComputeLordBonus(pc, s, controller, p.is_animated, &p).first;
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

// MTG_CG_ORDER -- the USER-reviewed Creature Giving cast order (2026-08-19, ruling recorded
// verbatim in docs/design/cast-order-rankings.md). Default OFF pending measurement; on adoption
// this becomes a default-on read with an off switch, as the other adopted per-deck rules are.
// This deck has NO order-opaque cards (no draws/tricks), so like Anti-Lifegain its rank sort
// governs every cast set -- these ranks are the whole delivery, no opaque hook needed.
static bool CgOrderReviewEnabled()
{
    // ADOPTED default-on (USER, 2026-08-19): the reviewed Creature Giving order incl. the
    // Scrying-before-land defer. Combined-arm evidence (with MTG_ACQ_RESOLVE): held-out 12/12
    // keys green (d3 -0.019..-0.028, d5 -0.014..-0.022), per-game 487 faster / 292 slower.
    // =0 reverts.
    static const bool on = EnvOn("MTG_CG_ORDER", true);   // DEFAULT ON; =0 disables
    return on;
}

int CreatureGivingProvider::CastOrderRank(const GameState& s, const CardDefinition& def) const
{
    if (CgOrderReviewEnabled())
    {
        const CardParams& p = def.params;
        // The USER's order (2026-08-19). Land drop is already before every cast at d0, which is
        // the ruled "land first -- more options for sacrifice"; Crop Rotation follows at 5.
        // Sylvan Scrying's BEFORE-LAND ideal ("it can fetch another Forbidden Orchard") cannot be
        // expressed by a cast rank -- the land drop is not a cast -- and is recorded as the
        // shared land-two-position open item (same gap as Mirrorwing's ruling).
        if (p.tutor_land_to_battlefield)            { return 5;  }  // Crop Rotation: mana-neutral, first cast
        if (p.tutor_to_hand)                        { return 6;  }  // Sylvan Scrying: early (land ideal, see above)
        if (p.etb_opp_creates_tokens > 0)           { return 12; }  // Hunted Phantasm: after the watchers
        if (p.etb_opp_creatures_debuff > 0)         { return 28; }  // Massacre Wurm: LAST -- gifts enter first, then die for 2 each
        if (p.opp_creature_enters_life_loss > 0
            || p.any_creature_enters_lifegain > 0)  { return 8;  }  // watchers: Suture Priest / the Wardens, before every giver
        if (p.tutor_to_top)                         { return 22; }  // Enlightened Tutor: after every same-turn shuffle
        return GenericProvider::CastOrderRank(s, def);
    }
    return GenericProvider::CastOrderRank(s, def);
}

bool CreatureGivingProvider::LandDropAfterHandLandTutor(const GameState& s, int controller) const
{
    // USER (2026-08-19, final round): "If you can play it before the land drop, you should. The
    // reason being that you want the fixing and creature creation of Forbidden Orchard, so doing
    // this is never a negative." Exactly two exceptions, both the USER's: "The only time when
    // you can just play the land first is if you had orchard already" (order then irrelevant --
    // and clarified: Scrying-first is ALSO fine there; land-first is chosen as the simpler
    // measured path, not a rule), "Or if you can't afford sylvan scrying without the land" (the
    // payability fallback).
    // The earlier no-hand-land narrowing is SUPERSEDED: its measured +0.0530 was this hook's own
    // defect (the turn was PLANNED one land short), fixed by the MTG_ACQ_RESOLVE second pass
    // re-solving after the tutor with the drop played -- not by narrowing the rule.
    if (!CgOrderReviewEnabled()) { return false; }
    bool tutor_payable = false;
    const ManaPool avail = AvailableManaPool(s);
    for (const Card& c : s.players[controller].hand)
    {
        const CardDefinition* d = CardDatabase::Instance().LookupCached(c);
        if (!d) { continue; }
        // Orchard already in hand: the drop can simply BE the Orchard -- land-first, no defer.
        if (d->card.IsLand() && d->params.taps_spawn_opp_token) { return false; }
        if (!d->params.tutor_to_hand) { continue; }
        bool land_tutor = false;
        for (const std::string& t : d->params.tutor_types)
        { if (t == "Land") { land_tutor = true; break; } }
        if (land_tutor && avail.CanPay(d->card.m_mana_cost)) { tutor_payable = true; }
    }
    return tutor_payable;
}

const char* CreatureGivingProvider::CastOrderTierName(int rank) const
{
    if (!CgOrderReviewEnabled()) { return nullptr; }
    switch (rank)
    {
        case 5:  return "CROP ROTATION: mana-neutral, right after the land (land first = more sacrifice options)";
        case 6:  return "SYLVAN SCRYING: early -- its before-land ideal (fetch another Orchard) is the land-two-position open item";
        case 8:  return "WATCHER: Suture Priest / Wardens before every giver (each gifted body billed on entry)";
        case 12: return "GIVER (Phantasm): after the watchers";
        case 22: return "ENLIGHTENED TUTOR: after every same-turn shuffle (to-top placement dies to one)";
        case 28: return "MASSACRE WURM: LAST -- enemy creatures are created first, then swept for 2 each";
        default: return nullptr;
    }
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

// MTG_5C_ORDER / MTG_5C_PHASE -- the USER-reviewed Five Colour cast order and phase rules
// (2026-08-19 prototype, verbatim in docs/design/cast-order-rankings.md). Two levers for
// attribution: ORDER is the rank sort (always live once the deck's canonical order runs), PHASE
// opts this deck into the pre-combat Main2 filter (ClassifiesMainPhases -- default-off machinery,
// per-deck adoption route; the global MTG_PHASE_CLASSIFY force remains rejected). Both DEFAULT
// OFF pending measurement. USER constraint honoured: irrelevant order adds NO search cost -- a
// rank sort is deterministic over the chosen set, plan order within a tier, no order branching.
static bool Fc5OrderEnabled()
{
    // ADOPTED default-on (USER, 2026-08-19). Held-out alone (with the Progenitus protection
    // fix): 12/12 overnight keys green, d0 ~-0.036 all four seeds, searched -0.01..-0.025.
    // =0 reverts.
    static const bool on = EnvOn("MTG_5C_ORDER", true);   // DEFAULT ON; =0 disables
    return on;
}
static bool Fc5PhaseEnabled()
{
    // ADOPTED default-on (USER, 2026-08-19) -- the FIRST per-deck ClassifiesMainPhases opt-in.
    // Held-out combined with the order lever: 12/12 overnight keys green, d3 to -0.0925, d5 to
    // -0.1133; d0 byte-identical to the order lever alone (the pre-combat filter is
    // searched-depth machinery). =0 reverts.
    static const bool on = EnvOn("MTG_5C_PHASE", true);   // DEFAULT ON; =0 disables
    return on;
}

// Lightning Greaves "active or in plan" (USER wording), read as: on our battlefield or in hand.
// The rank hook cannot see the chosen cast set; with ONE copy in the deck (USER: "decisions
// without it are much simpler") hand-presence is the plan to within a decline, and the decline
// case costs only Cornucopia's once-per-turn trigger, where late is mildly better anyway.
static bool Fc5GreavesLive(const GameState& s)
{
    const int active = s.active_player_index;
    for (const Permanent& p : s.battlefield)
    {
        if (p.controller_index != active) { continue; }
        const CardDefinition* d = CardDatabase::Instance().LookupCached(p.card);
        if (d && d->params.is_equipment && d->params.equip_grants_haste) { return true; }
    }
    for (const Card& c : s.players[active].hand)
    {
        const CardDefinition* d = CardDatabase::Instance().LookupCached(c);
        if (d && d->params.is_equipment && d->params.equip_grants_haste) { return true; }
    }
    return false;
}

// Rank scale note: under the lever this provider ranks EVERY card of the deck explicitly (no
// generic fallthrough), on a WIDENED scale -- enablers 4..9, bodies 100 + 10*mv (Hellkite
// pinned 195), the scaling-dork ideal slot 3 ABOVE its completer's rank, walkers/other
// noncreatures 210, late dorks 250. Rank comparisons only ever happen within one deck's cast
// set, so the scale is free to differ from the generic 0..30 tiers; the widening is what makes
// "immediately after the completing cast" expressible between body cost-tiers.
static int Fc5SpellRank(const CardDefinition& d)
{
    const CardParams& q = d.params;
    if (d.card.IsCreature())
    {
        // Progenitus (protection_from_everything): VERY LAST (USER: "can safely be cast the
        // very last in main 2 ... since it cannot wear mana greaves. (protection from
        // everything includes equipment)"). Staying in hand longest also keeps it available as
        // the common Maelstrom Archangel free-cast target (USER note); the HOLD decision
        // itself stays with the search -- this is only the within-set position.
        if (q.protection_from_everything) { return 220; }
        // Bodies: CHEAPEST FIRST (USER: "I prefer the cheaper ones first, because they might
        // enable a Faeburrow or Bloom Tender" -- each colorful body widens the domain). A full
        // deterministic ordering, no order branching, no search cost (the USER's constraint);
        // mv ties keep plan order (the middle order is otherwise not load-bearing, USER).
        // Hellkite needs no pin: mv 6 already slots it after the mv-5 bodies and before
        // nothing that matters (USER correction: "after 5 MV creatures, but not Progenitus").
        return 100 + 10 * std::min(d.card.m_mana_cost.ManaValue(), 9);
    }
    return 210;   // walkers + any other noncreature
}

// The scaling dork's IDEAL slot below 5 colors (USER, 2026-08-19: "a previous spell getting us
// up to 5 colours means that Bloom Tender and Faeburrow Elder will immediately be next"):
// 3 above the rank of the cheapest-slotted PERMANENT spell in hand that would complete the
// domain BY ITSELF -- an instant (Unite is WUBRG) adds no permanent colors and never counts.
// No single completer in hand -> 200, after all bodies (covers "whether spells that add new
// colours are available or not", and the USER's acknowledged approximation for the rare
// multiple-1-2-color-adders case: cumulative completion is only caught positionally, after the
// bodies. Searching the in-between placements for that rare case is a RECORDED FOLLOW-UP
// (USER: "rare, so it might be worth considering" vs "might be ... disadvantageous
// budget-wise") -- not built today.)
static int Fc5ScalingIdealRank(const GameState& s)
{
    bool have[5] = {false, false, false, false, false};
    for (Color c : DomainColors(s, s.active_player_index))
    { if (c != Color::Colorless) { have[static_cast<int>(c)] = true; } }
    int best = -1;
    for (const Card& h : s.players[s.active_player_index].hand)
    {
        const CardDefinition* d = CardDatabase::Instance().LookupCached(h);
        if (!d || d->card.IsLand()) { continue; }
        const bool permanent = d->card.IsCreature()
            || d->card.HasType(CardType::Artifact)
            || d->card.HasType(CardType::Enchantment)
            || d->card.HasType(CardType::Planeswalker);
        if (!permanent) { continue; }
        bool completes = true;
        for (int ci = 0; ci < 5; ++ci)
        { if (!have[ci] && !d->card.HasColor(static_cast<Color>(ci))) { completes = false; break; } }
        if (!completes) { continue; }
        const int r = Fc5SpellRank(*d);
        if (best < 0 || r < best) { best = r; }
    }
    return best < 0 ? 200 : best + 3;
}

// ---- FiveColourProvider cleanup discard (bucket policy) ---------------------
//
// USER-REVIEWED bucket policy (2026-08-21; the general shape is encoded in analyze-deck.md 5i).
// Evidence (discard-analysis stage, 400 games d3, logs/discard_analysis/FiveColour): only 12
// cleanup sheds reached; 11 were full ties across the hand, and the single non-tie was the
// generic max-MV rule pitching Progenitus for a 1-turn cost. Stakes are doctrine quality plus
// rollout/gen fidelity (the rollout always sheds heuristically), so the adoption bar is
// non-inferiority, not improvement.
//
// The keep set is built by BUCKET QUOTAS, all net of board; only overflow beyond quotas is
// sheddable, and the shed is the single overall-lowest-priority card:
//   1. colour coverage -- minimal mana set that, with board sources, covers all five colours
//   2. land drops      -- one land beyond coverage (two before turn 4); sub-roles are fungible
//                         upward (no dorks in hand => more lands), the parent quota binds
//   3. acceleration    -- up to two dorks beyond coverage while board sources < 7 (curve top:
//                         Bolas 8 / Unite 7); scaling dorks (domain) preferred
//   4. threat floor    -- at least TWO threats before any enabler (USER: Cannons yields)
//   5. Mana Cannons    -- one net of board, after the floor ("if there is space after")
//   6. Lightning Greaves while an Archangel-class body is kept or fielded
//   7. the rest        -- remaining spells are THREATS (USER: "extra spells should always be
//                         threats"), remaining mana keeps dorks over lands, colour-gain first
//
// Threat order (value rank): Archangel > Hellkite > Spider-Man > Unite > Jared > Garth >
// Progenitus > Oko > (unnamed spells) > Bolas LAST (goldfish-dead: +3 only ramps off opponent
// props, -2 dead, -9 slow). State promotions (USER 2026-08-21):
//   * Archangel online (on board or banked free casts): Progenitus and Unite promote to just
//     behind the engines -- free casts erase their costs and they hit hardest.
//   * Unite castable for LETHAL next turn (5 modes x 2 face + 5 per Cannons on board): front.
//   * Otherwise distance-to-playable demotes stranded payoffs in CLASSES (card colours missing
//     from board+hand coverage, plus mana shortfall vs board sources) -- "dropping a Progenitus
//     may still make sense... if it is nowhere near playable"; value rank breaks ties inside a
//     class.
//
// Returns ONE index: the rule IS the decision (TreasureHunt precedent -- a single-entry return
// skips the executor's trial fan; the one-option default is the USER's standing rule). The
// order is routed through CleanupDiscardRankingWithOrder so the staged-card and required-piece
// protections stay engine-enforced. MTG_5C_BUCKET_DISCARD=0 restores the generic ranking.
// Split in two (see DecisionProvider::CleanupDiscardShedOrder): the bucket rule produces a whole
// shed ORDER, and the one-index narrowing that follows it is about the executor's searched fan.
// A cleanup shedding three cards reads three entries off the order; it used to get one, shed it,
// and re-consult on a rebuilt hand.
std::vector<int> FiveColourProvider::CleanupDiscardCandidates(
    const GameState& s, const std::vector<std::string>* required_pieces) const
{
    static const bool s_bucket = EnvOn("MTG_5C_BUCKET_DISCARD", true);
    if (!s_bucket) { return GenericProvider::CleanupDiscardCandidates(s, required_pieces); }
    std::vector<int> ranked = CleanupDiscardShedOrder(s, required_pieces);
    // MTG_5C_DISCARD_FAN=1: TESTING-ONLY (discard-analysis evidence runs) -- return the full
    // shed order so the executor's searched pass trials every candidate and the trace grades
    // the bucket pick against the labels (a single-entry return skips the trace entirely,
    // AIEngine::ChooseDiscard). Never set in play; the shipped return is ONE index.
    static const bool s_fan = EnvOn("MTG_5C_DISCARD_FAN");
    if (!s_fan && ranked.size() > 1) { ranked.resize(1); }
    return ranked;
}

std::vector<int> FiveColourProvider::CleanupDiscardShedOrder(
    const GameState& s, const std::vector<std::string>* required_pieces) const
{
    static const bool s_bucket = EnvOn("MTG_5C_BUCKET_DISCARD", true);
    if (!s_bucket) { return GenericProvider::CleanupDiscardCandidates(s, required_pieces); }

    const Player& ap = s.players[s.active_player_index];
    const int n = static_cast<int>(ap.hand.size());
    if (n == 0) { return {}; }
    auto def_of = [](const Card& c) { return CardDatabase::Instance().LookupCached(c); };
    auto color_bit = [](Color c) { return 1u << static_cast<unsigned>(c); };
    auto popcount5 = [](unsigned m) { int k = 0; while (m) { m &= m - 1; ++k; } return k; };
    constexpr unsigned kAll5 = 31u;

    // Colours a mana source can produce. Fetches count as WILD: every fetch in this list
    // reaches a shock/triome mix spanning the deck's colours, and coverage precision is not
    // load-bearing here (11 of the 12 evidence sheds were full ties).
    auto source_mask = [&](const Card& c) -> unsigned
    {
        const CardDefinition* d = def_of(c);
        if (d == nullptr) { return 0u; }
        if (!d->card.IsLand() && d->tmpl != CardTemplate::ManaDork && !d->params.mana_rock)
        { return 0u; }
        if (!d->params.fetch_land_types.empty()) { return kAll5; }
        if (d->params.domain_mana)
        {
            unsigned m = 0;
            for (Color dc : DomainColors(s, s.active_player_index))
            { if (dc != Color::Colorless) { m |= color_bit(dc); } }
            return m;
        }
        unsigned m = 0;
        for (Color pc : d->params.produces)
        { if (pc != Color::Colorless) { m |= color_bit(pc); } }
        return m;
    };

    // Board census (every quota nets its board coverage out first).
    bool archangel_online = s.free_casts_available > 0;
    bool greaves_target_board = false;
    int  board_sources = 0, cannons_board = 0;
    unsigned covered = 0;
    for (const Permanent& p : s.battlefield)
    {
        if (p.controller_index != s.active_player_index) { continue; }
        const CardDefinition* d = def_of(p.card);
        const InternedName& nm = p.card.m_name;
        if (nm == "Maelstrom Archangel") { archangel_online = true; greaves_target_board = true; }
        if (nm == "Two-Headed Hellkite" || nm == "Cosmic Spider-Man")
        { greaves_target_board = true; }
        if (nm == "Mana Cannons") { ++cannons_board; }
        const bool dork = d != nullptr && d->tmpl == CardTemplate::ManaDork;
        const unsigned m = source_mask(p.card);
        if (m != 0 || dork) { ++board_sources; covered |= m; }
    }

    // Hand census (staged cards are in exile -- never bucketed; Greaves/Cannons split out).
    struct HandMana { int idx; unsigned mask; bool land; bool dork; bool domain; };
    std::vector<HandMana> mana;
    std::vector<int> threat_idx, cannons_idx;
    int greaves_idx = -1;
    unsigned hand_cover = 0;
    for (int i = 0; i < n; ++i)
    {
        const Card& c = ap.hand[i];
        if (c.m_is_staged) { continue; }
        const CardDefinition* d = def_of(c);
        const InternedName& nm = c.m_name;
        if (nm == "Lightning Greaves") { greaves_idx = i; continue; }
        if (nm == "Mana Cannons") { cannons_idx.push_back(i); continue; }
        const bool land = d != nullptr && d->card.IsLand();
        const bool dork = d != nullptr && d->tmpl == CardTemplate::ManaDork;
        const bool rock = d != nullptr && d->params.mana_rock;
        if (land || dork || rock)
        {
            const unsigned m = source_mask(c);
            mana.push_back({i, m, land, dork, d->params.domain_mana});
            hand_cover |= m;
            continue;
        }
        threat_idx.push_back(i);   // every non-mana spell is a THREAT (USER)
    }

    // Threat priority (lower = keep harder).
    const unsigned reach = covered | hand_cover;
    const int opp_life = s.players[1 - s.active_player_index].life;
    auto threat_rank = [&](const Card& c) -> int
    {
        const InternedName& nm = c.m_name;
        int r;
        if      (nm == "Maelstrom Archangel")       { r = 0; }
        else if (nm == "Two-Headed Hellkite")       { r = 10; }
        else if (nm == "Cosmic Spider-Man")         { r = 20; }
        else if (nm == "Unite the Coalition")       { r = 30; }
        else if (nm == "Jared Carthalion")          { r = 40; }
        else if (nm == "Garth One-Eye")             { r = 50; }
        else if (nm == "Progenitus")                { r = 60; }
        else if (nm == "Oko, Thief of Crowns")      { r = 70; }
        else if (nm == "Nicol Bolas, Planeswalker") { r = 90; }
        else                                        { r = 80; }
        if (archangel_online)
        {
            if (nm == "Progenitus")           { r = 12; }
            else if (nm == "Unite the Coalition") { r = 14; }
        }
        if (nm == "Unite the Coalition")
        {
            const bool affordable = archangel_online
                || (board_sources + 1 >= 7 && reach == kAll5);
            if (affordable && opp_life <= 10 + 5 * cannons_board) { r = -10; }
        }
        if (!archangel_online)
        {
            const CardDefinition* d = def_of(c);
            if (d != nullptr)
            {
                int missing = 0;
                for (int ci = 0; ci < 5; ++ci)
                {
                    const Color col = static_cast<Color>(ci);
                    if (d->card.HasColor(col) && (reach & color_bit(col)) == 0) { ++missing; }
                }
                const int shortfall = d->card.m_mana_cost.ManaValue() - (board_sources + 1);
                int dist = missing + (shortfall > 0 ? shortfall : 0);
                if (dist > 4) { dist = 4; }
                r += dist * 100;
            }
        }
        return r;
    };
    std::stable_sort(threat_idx.begin(), threat_idx.end(), [&](int a, int b)
                     { return threat_rank(ap.hand[a]) < threat_rank(ap.hand[b]); });

    // Build the KEEP-priority list (front = keep hardest).
    std::vector<int> keep;
    std::vector<char> used(static_cast<std::size_t>(n), 0);
    auto take = [&](int i) { if (i >= 0 && !used[static_cast<std::size_t>(i)])
                             { used[static_cast<std::size_t>(i)] = 1; keep.push_back(i); } };

    // 1. Colour coverage: greedy set cover of the colours the board is missing (most new
    //    colours first; a land beats a dork at equal gain -- the drop needs no cast).
    unsigned cov = covered;
    while (cov != kAll5)
    {
        int best = -1, best_gain = 0; bool best_land = false; unsigned best_mask = 0;
        for (const HandMana& h : mana)
        {
            if (used[static_cast<std::size_t>(h.idx)]) { continue; }
            const int gain = popcount5(h.mask & ~cov);
            if (gain > best_gain || (gain == best_gain && gain > 0 && h.land && !best_land))
            { best = h.idx; best_gain = gain; best_land = h.land; best_mask = h.mask; }
        }
        if (best < 0 || best_gain == 0) { break; }
        take(best);
        cov |= best_mask;
    }

    // 2. Land drops beyond coverage: one, two before turn 4 (fungible upward within mana).
    {
        int kept_lands = 0;
        for (int i : keep) { for (const HandMana& h : mana) { if (h.idx == i && h.land) { ++kept_lands; } } }
        const int drops = s.turn_number <= 3 ? 2 : 1;
        for (const HandMana& h : mana)
        {
            if (kept_lands >= drops) { break; }
            if (!h.land || used[static_cast<std::size_t>(h.idx)]) { continue; }
            take(h.idx); ++kept_lands;
        }
    }

    // 3. Acceleration: up to two dorks beyond coverage while board sources < 7; scaling
    //    (domain) dorks first.
    if (board_sources < 7)
    {
        int kept_dorks = 0;
        for (int i : keep) { for (const HandMana& h : mana) { if (h.idx == i && h.dork) { ++kept_dorks; } } }
        for (int pass = 0; pass < 2 && kept_dorks < 2; ++pass)
        {
            for (const HandMana& h : mana)
            {
                if (kept_dorks >= 2) { break; }
                if (!h.dork || used[static_cast<std::size_t>(h.idx)]) { continue; }
                if (pass == 0 && !h.domain) { continue; }
                take(h.idx); ++kept_dorks;
            }
        }
    }

    // 4+5. Threat floor of two, then one Cannons net of board, then Greaves, then the rest of
    //      the threats in rank order.
    std::size_t ti = 0;
    for (int taken = 0; taken < 2 && ti < threat_idx.size(); ++ti, ++taken) { take(threat_idx[ti]); }
    if (cannons_board == 0 && !cannons_idx.empty()) { take(cannons_idx.front()); }
    {
        bool greaves_body = greaves_target_board;
        for (int i : keep)
        {
            const InternedName& nm = ap.hand[i].m_name;
            if (nm == "Maelstrom Archangel" || nm == "Two-Headed Hellkite"
                || nm == "Cosmic Spider-Man") { greaves_body = true; }
        }
        if (greaves_body) { take(greaves_idx); }
    }
    for (; ti < threat_idx.size(); ++ti) { take(threat_idx[ti]); }
    for (int i : cannons_idx) { take(i); }

    // 7. Remaining mana: dorks over lands, colour-gain (vs the kept coverage) first.
    {
        std::vector<int> rest;
        for (const HandMana& h : mana)
        { if (!used[static_cast<std::size_t>(h.idx)]) { rest.push_back(h.idx); } }
        auto rest_key = [&](int i) -> int
        {
            for (const HandMana& h : mana)
            {
                if (h.idx != i) { continue; }
                return (h.dork ? 0 : 100) - popcount5(h.mask & ~cov) * 10;
            }
            return 1000;
        };
        std::stable_sort(rest.begin(), rest.end(),
                         [&](int a, int b) { return rest_key(a) < rest_key(b); });
        for (int i : rest) { take(i); }
    }
    take(greaves_idx);   // Greaves with no body: last of the keeps, first sensible shed

    // Shed order = keep priority reversed; unbucketed leftovers (staged cards were skipped and
    // are re-filtered by the shared ranking anyway) fall to the tiers via omission.
    std::vector<int> shed(keep.rbegin(), keep.rend());
    return CleanupDiscardRankingWithOrder(s, required_pieces, shed);
}

int FiveColourProvider::CastOrderRank(const GameState& s, const CardDefinition& def) const
{
    if (Fc5OrderEnabled())
    {
        const CardParams& p = def.params;
        // The USER's prototype order (2026-08-19): land -> Cannons -> Cornucopia -> Greaves ->
        // Unite -> [dorks early iff Greaves live; scaling dorks per the completer rule] ->
        // bodies cheapest-first -> walkers -> [dorks last otherwise].
        if (p.multicolor_cast_damage_per_color)      { return 4; }   // Cannons: before every multicolored cast
        if (p.mana_rock)                             { return 5; }   // Cornucopia
        // A FREE sac-for-mana artifact (the Black Lotus copy Garth conjures -- no such card is
        // in the decklist itself) is the arch-accelerant, so it joins the rock tier: cast first,
        // it can fund everything after it. Only reachable through MTG_GARTH_ORDERED's copy-def
        // ranking (OrderDefOf); inert otherwise. Disclosed as an encoding choice, not a ruling.
        if (p.sac_for_mana_amount > 0)               { return 5; }   // Black Lotus (Garth copy)
        if (p.is_equipment && p.equip_grants_haste)  { return 6; }   // Greaves: unlock before the dorks/bodies
        if (p.modal_choose_n > 0)                    { return 7; }   // Unite
        if (def.card.IsCreature() && !p.produces.empty())
        {
            if (p.domain_mana)
            {
                // Bloom Tender / Faeburrow. Greaves live:
                //   domain already 5 -> 9 (front, taps for 5 immediately);
                //   domain < 5      -> the IDEAL slot immediately after the domain-completing
                //     cast (Fc5ScalingIdealRank), with FUNDING ladder rungs {ideal, 9}
                //     (CastOrderFallbackRanks below): "try 2 options in this order: after all
                //     spells that add a new colour ... and in front. Whether it works is a
                //     matter of feasibility" -- the range ladder walks it to the front exactly
                //     when FirstUnpayablePos says the line as ordered cannot pay. (No dork-tap
                //     credit in FirstUnpayablePos: with two rungs the end positions are right
                //     either way -- payable-at-ideal stays late, everything else fronts, where
                //     the real payment machinery cashes the Greaves unlock.)
                // No Greaves: last (250) -- "otherwise they can be played last."
                if (Fc5GreavesLive(s))
                {
                    if (DomainColors(s, s.active_player_index).size() >= 5) { return 9; }
                    return Fc5ScalingIdealRank(s);
                }
                return 250;
            }
            // Birds / Deathrite: early iff the Greaves unlock makes their tap live this turn.
            return Fc5GreavesLive(s) ? 8 : 250;
        }
        return Fc5SpellRank(def);
    }
    return GenericProvider::CastOrderRank(s, def);
}

std::vector<int> FiveColourProvider::CastOrderFallbackRanks(const GameState& s,
                                                            const CardDefinition& def) const
{
    if (!Fc5OrderEnabled()) { return {}; }
    const CardParams& p = def.params;
    // The scaling dork's two USER-ruled options, preference order: the ideal completer-adjacent
    // slot, then the front (9). Active exactly in the Greaves-live below-5-colors situation the
    // rule is scoped to; everywhere else the rank above is a single fixed position.
    if (def.card.IsCreature() && p.domain_mana && Fc5GreavesLive(s)
        && DomainColors(s, s.active_player_index).size() < 5)
    { return { Fc5ScalingIdealRank(s), 9 }; }
    return {};
}

const char* FiveColourProvider::CastOrderTierName(int rank) const
{
    if (!Fc5OrderEnabled()) { return nullptr; }
    switch (rank)
    {
        case 4:   return "MANA CANNONS: before every multicolored cast (each pings its color count)";
        case 5:   return "CORNUCOPIA / MANA ROCK: online for the rest of the line";
        case 6:   return "GREAVES: the haste/unlock enabler, before the dorks and bodies";
        case 7:   return "UNITE: early within its main (phase decided by the MTG_5C_PHASE rule)";
        case 8:   return "PLAIN DORK, Greaves live: early -- equip {0} unlocks the tap this turn";
        case 9:   return "SCALING DORK, Greaves live + all 5 colors on field: early, taps for 5";
        case 200: return "SCALING DORK ideal, no single completer in hand: after all bodies (ladder may front it)";
        case 210: return "WALKERS / other noncreatures";
        case 220: return "PROGENITUS: very last -- cannot wear Greaves (protection), stays a free-cast target";
        case 250: return "DORKS, no Greaves unlock: last (USER ruling)";
        default:
            if (rank >= 100 && rank <= 190 && rank % 10 == 0)
            { return "BODY, cheapest first (rank = 100 + 10*mv): each colorful body widens the domain"; }
            if (rank >= 103 && rank % 10 == 3)
            { return "SCALING DORK ideal: immediately after the domain-completing cast (ladder may front it)"; }
            return nullptr;
    }
}

bool FiveColourProvider::ClassifiesMainPhases() const
{
    return Fc5PhaseEnabled();
}

bool FiveColourProvider::SearchedSecondMainInSearch() const
{
    // Scoped to the phase spec being live: without it the interior m2 is near-empty and
    // searching it is pure budget dilution (the global lever's recorded rejection).
    // ADOPTED default-on 2026-08-21 (USER: "change it to searched if we can do so without much
    // additional cost"; =0 hatch). The enum memo (adopted the same day) absorbs the extra m2
    // enumerations: the lever's pre-memo +9% wall on the heavy fivecolour game measured +0.7%
    // (noise) post-memo on interleaved quiet-box pairs. Suite A/B vs GT: every changed key
    // digest-only at an IDENTICAL average (30 games re-lined at the same per-game score), d0
    // untouched. This removes the last greedy step from the interior second main -- the user's
    // core bar (search primary; no greedy steps except attacks + mana allocation).
    static const bool on = EnvOn("MTG_5C_SSM", true);
    return on && Fc5PhaseEnabled();
}

bool FiveColourProvider::CondemnsPassedMainPhase() const
{
    // TRAIN NEUTRAL-TO-GREEN after the condemn-dig rounds (2026-08-19); ADOPTION PENDING
    // (held-out keys + USER call). The original rejection (+0.040..+0.050) decomposed into:
    //   Round 1 -- three FILTER BUGS fixed (see StampM1Hand + the filter in TurnSolver.cpp):
    //   free casts condemned (Archangel banks on combat damage; the m1 world never had the
    //   action), unaffordable-at-m1 cards condemned, and re-classification at the m2 state
    //   (the USER said "the same condemnation list" -- membership is now decided once, at m1).
    //   Round 2 -- the FAEBURROW DOCTRINE (USER 2026-08-19): "casting spells in main 2 becomes
    //   correct to allow Faeburrow Elder to attack" -- with a live vigilant mana scaler the
    //   scaling pull answers Both, not Main1 (but own-haste bodies -- Spider-Man, Hellkite --
    //   stay Main1, and capacity-one Greaves access is Both, protection-aware); Mana Cannons
    //   gets the Unite-style payable-without-attackers conditional above. The winning lines
    //   were exactly "vigilant Faeburrow attacks, then its mana funds the post-combat casts"
    //   (gi53/gi82 m2 Archangel, gi56 m2 Garth, gi158 m2 Cannons+Cannons+Garth), and they were
    //   being condemned -- escalation to d7/32x budget could not recover them (23/45 cells
    //   red, 0 green), proving FILTERED, not underexplored; after the doctrine, 3/45 red cells
    //   remain, all one game (gi187: Jared's m1-vs-m2 tie made load-bearing).
    // Train after both rounds: d3 +0.005/-0.005, d5 -0.030/-0.010 (6 worse / 11 better).
    // Off arm byte-identical throughout.
    // ADOPTED DEFAULT-ON (USER, 2026-08-20; =0 hatch) as the SOUND lever: root-turn authority
    // on the commit-the-line path + condemned tranche (MTG_CONDEMN_TRANCHE) closed all 3
    // residuals byte-identically. Final state: train 5/5 BYTE-IDENTICAL (x3 runs), held-out
    // 2800 searched games zero slower/zero faster (4 equal-score line swaps, 3 digest-only
    // keys), perf neutral within noise (sums +0.7%, makespan <= +3%). Semantics lever, not a
    // perf lever: the historical -22% was entirely the unsound projection-turn stamping.
    // 2026-08-21 UPDATE: the tranche is now DEFAULT OFF (USER; rescue-trace classification,
    // antilife-main-phase-split.md 2026-08-21g) -- its rescues were sibling-redundant tie-flips,
    // and condemnation's losslessness rests on the stamp's joint-affordability exemption + the
    // unfiltered m1 enumeration, not on the backstop. The old "closed the residuals" role is
    // covered by the same sibling argument; MTG_CONDEMN_TRANCHE=1 re-arms it as an audit.
    static const bool on = EnvOn("MTG_5C_CONDEMN", true);
    return on && Fc5PhaseEnabled();
}

std::optional<DecisionProvider::MainPhase>
FiveColourProvider::MainPhaseOverride(const GameState& s, const CardDefinition& def) const
{
    const CardParams& p = def.params;
    if (Fc5PhaseEnabled())
    {
        // USER phase rules (2026-08-19): "generally prefer second main for non-haste,
        // non-Greaves, non-Oko situations -- it ensures the maximum mana can be generated and
        // allows Faeburrow Elder to attack."
        // Mana Cannons: MAIN 1 -- the old "fires identically from either main" missed same-turn
        // sequencing: cast in main 2 it misses every main-1 multicolored body of that turn.
        // CONDEMN-DIG REFINEMENT (2026-08-19): Main1 only when payable WITHOUT tapping an
        // attack-capable mana creature (the same test as Unite below). When the mana is a
        // vigilant Faeburrow's, the winning line attacks first and casts Cannons at the top of
        // the post-combat window (gi158's T4: Cannons, Cannons, Garth -- the bodies follow it
        // in the SAME main, so no trigger is missed) -> Both, the search decides. Both is
        // shipped-inert (the pre-combat filter only drops Main2); the distinction matters to
        // the condemnation stamp, which takes Main1 only.
        //
        // HASTY-PAYOFF EXCEPTION (USER 2026-08-19): "Mana Cannons can also be cast main1
        // tapping Faeburrow if it has a hasty payoff. That would cause it to deal more damage
        // than Faeburrow." The USER's meta-rule for this whole family: "most decisions here
        // come down to 'how much damage does this deal' (compared to how much it leaves on the
        // table)" -- so a doctrine rule may only ASSERT a phase where that ledger is one-sided
        // BY CONSTRUCTION, and must answer Both (the search sums both sides exactly) where it
        // is arithmetic. Here it IS one-sided when a hasty multicolored body is castable
        // ALONGSIDE Cannons this turn AND its colour count >= the colours among our permanents
        // (= Faeburrow's ceiling power): the m1 line's trigger damage alone (one per colour of
        // the payoff -- 5 for Spider-Man/Hellkite) covers the forfeited Faeburrow attack, and
        // the hasty body's own attack is pure surplus. Pair-affordability uses the PLAIN pool
        // (Faeburrow's yield included, NO optimism), so the Main1 claim -- which the
        // condemnation stamp trusts -- only fires when the line is really there.
        if (p.multicolor_cast_damage_per_color)
        {
            if (AvailableManaPoolNoAttackers(s).CanPay(def.card.m_mana_cost))
            { return MainPhase::Main1; }
            const int pool_total    = AvailableManaPool(s).Total();
            const int board_colours = static_cast<int>(
                DomainColors(s, s.active_player_index).size());
            for (const Card& hc : s.ActivePlayer().hand)
            {
                const CardDefinition* hd = CardDatabase::Instance().LookupCached(hc);
                if (!hd || !hd->card.IsCreature()
                    || !hd->card.HasKeyword(Keyword::Haste)) { continue; }
                const ManaCost& mc = hd->card.m_mana_cost;
                const int colours = (mc.white > 0) + (mc.blue > 0) + (mc.black > 0)
                                  + (mc.red > 0) + (mc.green > 0);
                if (colours < 2 || colours < board_colours) { continue; }
                if (def.card.m_mana_cost.ManaValue() + mc.ManaValue() <= pool_total)
                { return MainPhase::Main1; }
            }
            return MainPhase::Both;
        }
        // Unite the Coalition: "second main is ideal when it would cause a Faeburrow Elder to
        // tap to play it; first main is ideal when you need to draw a hasty threat and won't tap
        // a Faeburrow that could attack." Encoded: main 1 iff payable WITHOUT tapping an
        // attack-capable creature source (the draw upside then comes free), else main 2.
        if (p.modal_choose_n > 0 && p.modal_damage_per_choice > 0)
        {
            return AvailableManaPoolNoAttackers(s).CanPay(def.card.m_mana_cost)
                       ? MainPhase::Main1 : MainPhase::Main2;
        }
        if (def.card.m_name == "Nicol Bolas, Planeswalker") { return MainPhase::Main2; }
        // Scaling dorks' own cast: main 2 (they cannot attack or tap the turn they land) unless
        // the Greaves unlock is live -- then main 1 so the tap can fund the rest of the turn.
        // Plain dorks already classify main 2 via the base rules.
        if (def.card.IsCreature() && p.domain_mana)
        { return Fc5GreavesLive(s) ? MainPhase::Main1 : MainPhase::Main2; }
        // Oko stays on the generic loyalty pull (USER 2026-08-16 ruling unchanged).
        return std::nullopt;
    }
    // Legacy (pre-review) classification -- inert in play without ClassifiesMainPhases, kept for
    // the MTG_PHASE_CLASSIFY archaeology A/B.
    // Unite the Coalition: S x (2 face damage) + (N-S) x draw -- no split feeds the attack, and
    // combat first means a vigilant Faeburrow's mana is still available to pay for it.
    if (p.modal_choose_n > 0 && p.modal_damage_per_choice > 0) { return MainPhase::Main2; }
    // Mana Cannons: on-cast face damage, fires identically from either main.
    if (p.multicolor_cast_damage_per_color)                    { return MainPhase::Main2; }
    if (def.card.m_name == "Nicol Bolas, Planeswalker")        { return MainPhase::Main2; }
    // Oko is NO LONGER forced to Main2 (USER 2026-08-16). The premise was that his abilities do
    // nothing pre-combat, which was only true because elk_transform was hardcoded to Food tokens:
    // his +1 turns an artifact OR CREATURE into a 3/3, and a permanent controlled since the turn
    // began attacks immediately (CR 302.6), so Elking a 0/1 dork is a pre-combat attack. With the
    // target searched, the generic loyalty_abilities -> Main1 pull is the correct class for him --
    // the same reasoning that already keeps Jared off this list.
    return std::nullopt;
}

namespace
{
    // Deduct `cost` from `p`, following ManaPool::CanPayFlat's model exactly (specific colour pays
    // its own pips first, `wild` covers any deficit, generic comes from whatever is left). Leaves
    // `p` untouched and returns false when unpayable. Spends specific leftovers on generic BEFORE
    // wild, so the most flexible mana survives for the next spell in the sequence.
    bool PayFlatFrom(ManaPool& p, const ManaCost& cost)
    {
        ManaPool t = p;
        int* col[6] = { &t.white, &t.blue, &t.black, &t.red, &t.green, &t.colorless };
        const int need[6] = { cost.white, cost.blue, cost.black, cost.red, cost.green, cost.colorless };
        for (int i = 0; i < 6; ++i)
        {
            int n = need[i] - std::min(need[i], *col[i]);
            *col[i] -= std::min(need[i], *col[i]);
            if (n > 0)
            {
                if (t.wild < n) { return false; }
                t.wild -= n;
            }
        }
        int gen = cost.generic;
        for (int i = 0; i < 6 && gen > 0; ++i)
        {
            const int use = std::min(gen, *col[i]);
            *col[i] -= use;
            gen -= use;
        }
        if (gen > 0)
        {
            if (t.wild < gen) { return false; }
            t.wild -= gen;
        }
        p = t;
        return true;
    }

    // Same, honouring two-colour hybrid pips: take the first payable concrete assignment
    // (2^hybrid_count <= 16), matching CanPay's expansion.
    bool PayFrom(ManaPool& p, const ManaCost& cost)
    {
        if (cost.hybrid_count == 0) { return PayFlatFrom(p, cost); }
        for (unsigned bits = 0; bits < (1u << cost.hybrid_count); ++bits)
        {
            ManaPool t = p;
            if (PayFlatFrom(t, cost.ExpandHybrids(bits))) { p = t; return true; }
        }
        return false;
    }

    // How many of `costs` (pre-sorted cheapest-first) this pool can cast in sequence. Greedy
    // cheapest-first, which is optimal for "most spells within a budget" in the colourless case
    // and a good approximation with colours.
    int CastableCount(ManaPool p, const std::vector<const ManaCost*>& costs)
    {
        int n = 0;
        for (const ManaCost* mc : costs) { if (PayFrom(p, *mc)) { ++n; } }
        return n;
    }
}

std::vector<std::string>
FiveColourProvider::FetchCandidates(const GameState& s, int controller,
                                    const CardParams& fetch_pp) const
{
    std::vector<std::string> all = GenericProvider::FetchCandidates(s, controller, fetch_pp);
    if (all.size() < 2 || DecisionUnpruned(UnprunedGate::Fetch)) { return all; }

    constexpr int NC = 6;                    // W,U,B,R,G,C
    std::array<int, NC> src_cnt{};           // distinct sources that already make each colour
    std::array<int, NC> land_cnt{};          // ... of which are LANDS (a dork dies; a land does not)
    int bf_sources = 0;                      // mana permanents we control, for the castability horizon
    auto count = [&](const std::vector<Color>& prod, bool is_land)
    {
        for (Color c : prod)
        {
            ++src_cnt[static_cast<int>(c)];
            if (is_land) { ++land_cnt[static_cast<int>(c)]; }
        }
    };

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
        ++bf_sources;
        count(EffectiveProduces(s, controller, *d), d->card.IsLand());
    }
    // Plus non-fetch lands already in hand (a land we are about to play is a near-future source,
    // so fetching to re-cover a colour it already brings is wasted fixing).
    const Player& ap = s.players[controller];
    for (const Card& c : ap.hand)
    {
        const CardDefinition* d = CardDatabase::Instance().LookupCached(c);
        if (d && d->card.IsLand() && d->params.fetch_land_types.empty()) { count(d->params.produces, true); }
    }

    // What the hand wants to cast, split into accelerants (mana dorks/rocks -- the compounding
    // early plays) and everything else. `want` counts PIPS, so a {W}{W} cost asks for two white
    // sources and feeds the depth term below.
    std::array<int, NC> want{}, accel_want{};
    // `want_deep` is the SAME quantity for the redundancy (depth) term alone, so the SUM/HORIZON
    // levers can move "how many sources of this colour do we want" without touching the coverage
    // terms above it. With both levers off it is identical to `want`.
    std::array<int, NC> want_deep{};
    // How many DISTINCT accelerants in hand each colour would turn on. Every accelerant in this
    // deck is green (Birds {G}, Bloom Tender {1}{G}, Faeburrow {1}{G}{W}, Deathrite {B/G}), so
    // GREEN turns on the most one-drops and BLACK turns on exactly one -- which is the user's T1
    // doctrine ("T1 green land is a priority. If we can't play T1 green, we should play T1 black
    // for Deathrite") falling out of the card costs instead of a hardcoded colour preference.
    std::array<int, NC> accel_hits{};
    // "Likely to play from hand": a card only asks for a SECOND source of its colours
    // if we could plausibly cast it soon -- within this turn's land drop plus one more. Without
    // this an uncastable Progenitus ({W}{W}{U}{U}{B}{B}{R}{R}{G}{G}) asks for two of every colour
    // on turn 1, which is a uniform ask and therefore no signal at all.
    const int horizon_mv = bf_sources + 2;
    for (const Card& c : ap.hand)
    {
        const CardDefinition* d = CardDatabase::Instance().LookupCached(c);
        if (!d || d->card.IsLand()) { continue; }
        const bool accel = (d->tmpl == CardTemplate::ManaDork) || d->params.mana_rock;
        const ManaCost& mc = d->card.m_mana_cost;
        int pips[NC] = { mc.white, mc.blue, mc.black, mc.red, mc.green, 0 };
        // A hybrid pip is baked into its FIRST colour's flat int (see ManaCost), so {B/G} reads as
        // pure black. For "which colour would let me cast this" both halves are true, so credit
        // the second colour too -- without double-counting the cost. Every mana-PAYMENT path
        // already decodes hybrid_pair (or enumerates ExpandHybrids); this ranking site was the one
        // place that read the raw flat pips, which made Deathrite Shaman invisible to green.
        for (int h = 0; h < mc.hybrid_count; ++h)
        {
            const int c2 = mc.hybrid_pair[h] & 0xF;
            if (c2 >= 0 && c2 < NC) { pips[c2] = std::max(pips[c2], 1); }
        }
        const bool deep_ok = mc.ManaValue() <= horizon_mv;
        for (int i = 0; i < NC; ++i)
        {
            if (pips[i] <= 0) { continue; }
            want[i] = std::max(want[i], pips[i]);
            // SUMMED (capped at 2), not max: the redundancy question is "can I cast two things off
            // this colour", which no single card's cost answers. Mana Cannons {2}{R} plus a
            // five-colour spell needs RR at eight mana; Oko {1}{G}{U} plus a five-colour spell
            // needs GG and UU. Only the sum sees it.
            if (deep_ok) { want_deep[i] = std::min(2, want_deep[i] + pips[i]); }
            if (accel) { accel_want[i] = std::max(accel_want[i], pips[i]); ++accel_hits[i]; }
        }
    }
    // RULE 6 (user, 2026-08-18): "we probably should have a way to get triomes when we can
    // determine there is no need for the land to enter untapped ... triomes make coverage very
    // very easy as long as you can be confident that you don't need the colours [this turn]."
    //
    // The predicate is "can this land's mana actually be SPENT this turn": does ONE more mana of
    // colour c let us cast something we cannot already cast? When nothing unlocks, entering
    // untapped buys nothing, so the two untapped-preferring terms fall silent and `breadth`
    // decides -- and a triome covers three colours to a dual's two, so the triome wins on its own
    // merits rather than needing a rule of its own.
    //
    // Concretely this is what stops a T1 hand holding only Faeburrow Elder ({1}{G}{W}, MV 3) from
    // taking an untapped green dual over a triome: enables_now fires on "green turns on an
    // accelerant" without ever asking whether ONE mana can cast it. Birds of Paradise ({G}) still
    // scores, because one green really does cast it -- which is the Part 1 behaviour we must keep.
    // USER 2026-08-19: "if there is a spell that requires the land we should always go for
    // untapped ... triomes are STRICTLY for times when you cannot use the mana." So the test is a
    // UNION of two questions, and either one answering yes means untapped:
    //
    //   (a) SOLO  -- some single card is not castable now but IS with one more mana of colour c.
    //               This is what catches a BIGGER spell coming online (a 5-drop at four lands).
    //   (b) SEQUENCE -- one more mana of colour c lets us cast one MORE spell in sequence. This is
    //               what the per-card test structurally could not see: with two 2-drops in hand and
    //               three mana, each is individually castable, so (a) says nothing -- yet the extra
    //               mana casts both. Measured before this fix: nothing unlocked on 51.5% of fetches,
    //               and 11.7% of those had a second spell affordable with one more mana.
    //
    // The union only ever ADDS untapped preference, so triomes are taken strictly less often --
    // which is the asymmetry the user asked for.
    std::array<bool, NC> accel_unlocks{};   // one more mana of colour c deploys another ACCELERANT
    std::array<bool, NC> any_unlocks{};     // one more mana of colour c casts anything more
    {
        const ManaPool pool_now = AvailableManaPool(s);
        std::vector<const ManaCost*> all_costs, accel_costs;
        for (const Card& c : ap.hand)
        {
            const CardDefinition* d = CardDatabase::Instance().LookupCached(c);
            if (!d || d->card.IsLand()) { continue; }
            const ManaCost& mc = d->card.m_mana_cost;
            all_costs.push_back(&mc);
            const bool accel = (d->tmpl == CardTemplate::ManaDork) || d->params.mana_rock;
            if (accel) { accel_costs.push_back(&mc); }
            // (a) SOLO.
            if (pool_now.CanPay(mc)) { continue; }
            for (int i = 0; i < NC; ++i)
            {
                if (any_unlocks[i] && (!accel || accel_unlocks[i])) { continue; }
                ManaPool p = pool_now;
                p.Add(static_cast<Color>(i));
                if (p.CanPay(mc))
                {
                    any_unlocks[i] = true;
                    if (accel) { accel_unlocks[i] = true; }
                }
            }
        }
        // (b) SEQUENCE. Cheapest-first so the greedy casts as many as the pool allows; ties broken
        // by mana value alone keeps this independent of hand order (stable_sort over a
        // deterministic hand, so the result is deterministic either way).
        auto by_mv = [](const ManaCost* a, const ManaCost* b) { return a->ManaValue() < b->ManaValue(); };
        std::stable_sort(all_costs.begin(), all_costs.end(), by_mv);
        std::stable_sort(accel_costs.begin(), accel_costs.end(), by_mv);
        const int base_all   = CastableCount(pool_now, all_costs);
        const int base_accel = CastableCount(pool_now, accel_costs);
        for (int i = 0; i < NC; ++i)
        {
            const bool need_any   = !any_unlocks[i];
            const bool need_accel = !accel_unlocks[i];
            if (!need_any && !need_accel) { continue; }
            ManaPool p = pool_now;
            p.Add(static_cast<Color>(i));
            if (need_any   && CastableCount(p, all_costs)   > base_all)   { any_unlocks[i]   = true; }
            if (need_accel && CastableCount(p, accel_costs) > base_accel) { accel_unlocks[i] = true; }
        }
    }

    struct Key
    {
        int enables_now = 0, accel_new = 0, spell_new = 0, breadth = 0;
        int soft_new = 0, untapped = 0, depth = 0, colours = 0;
        std::string name;
    };
    std::vector<Key> keys;
    keys.reserve(all.size());
    for (const std::string& nm : all)
    {
        const CardDefinition* d = CardDatabase::Instance().Lookup(nm);
        Key k;
        k.name = nm;
        if (!d) { keys.push_back(std::move(k)); continue; }
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
            else if (land_cnt[i] == 0)
            {
                // Covered ONLY by a dork or rock. It counts as covered today -- the user's
                // "counting dorks on board" -- so it is not breadth; but a dork dies to any
                // removal and cannot be replayed, so a LAND for that colour is worth more than a
                // second land for a colour the lands already make. That is the "and eventually
                // not counting them" half: as the game goes on, coverage has to migrate to lands.
                ++k.soft_new;
            }
            else if (land_cnt[i] == 1)
            {
                k.depth += (want_deep[i] >= 2) ? 2 : 1;   // a second LAND, wanted twice over
            }
        }
        // Only a land that can enter UNTAPPED can pay for something this turn. A shock pays 2 life
        // for the privilege, which the engine's own entry choice already weighs; here it is just a
        // tiebreak, and only while a wanted colour is still missing.
        // RULE 6: and only while that mana can actually be SPENT this turn -- otherwise
        // untapped is worth nothing and a tapped triome's extra colour should not be given away.
        bool spendable = false;
        for (Color col : d->params.produces)
        {
            const int i = static_cast<int>(col);
            if (i >= 0 && i < NC && any_unlocks[i]) { spendable = true; break; }
        }
        // USER 2026-08-19: "if there is a spell that requires the land we should ALWAYS go for
        // untapped ... triomes are STRICTLY for times when you cannot use the mana." This used to
        // be gated on `any_uncovered_want` ("is some wanted colour MISSING"), which by turn 2 is
        // false for every colour because one Birds of Paradise covers all five -- so the untapped
        // preference switched itself off at exactly the point soft_new started handing triomes a
        // 3-vs-2 win. Measured before the fix: turn-2 fetches took a triome 81% of the time, in a
        // deck built with 6 shocklands and 2 basics against 2 triomes. The right question is not
        // "is a colour missing" but "can we SPEND this mana", which `spendable` answers.
        k.untapped = (!d->params.enters_tapped && spendable) ? 1 : 0;
        // ENABLES A PLAY THIS TURN (user doctrine 2026-08-18: "the priority is getting to 5 colours,
        // starting with Green T1 and prioritizing other colours we might need in our hand after
        // that ... White is a good bet for T2 if we have Faeburrow Elder in hand").
        //
        // A land that ENTERS TAPPED cannot cast anything this turn, so when the fetch is what would
        // turn on an accelerant in hand, entering untapped is not a tiebreak -- it is the whole
        // point. Ranking it 5th (below breadth) let two TAPPED triomes take both cap slots on turn 1
        // of reference claude_s7_gi6, so the turn-1 Birds of Paradise line was never branched on and
        // the deck won a turn late. accel_new is the "a mana source in hand wants this colour and we
        // do not have it" term, so `accel_new && untapped` is exactly "this fetch casts the dork
        // NOW" -- Green for Birds on T1, White for Faeburrow on T2.
        if (!d->params.enters_tapped)
        {
            for (Color col : d->params.produces)
            {
                const int i = static_cast<int>(col);
                if (i < 0 || i >= NC || src_cnt[i] != 0) { continue; }
                // RULE 6: "turns on an accelerant" must mean one we can actually cast
                // THIS TURN. Without this, a T1 hand holding only Faeburrow ({1}{G}{W}) scores a
                // green dual as if it deployed a dork, and beats a triome for nothing.
                if (!accel_unlocks[i]) { continue; }
                k.enables_now = std::max(k.enables_now, accel_hits[i]);
            }
        }
        keys.push_back(std::move(k));
    }

    // Key order as a vector so `untapped` can be re-positioned by the lever. With the lever off the
    // sequence is exactly enables_now, accel_new, spell_new, breadth, soft_new, untapped, depth,
    // colours -- byte-identical to the shipped ranking.
    //
    // breadth is a TIEBREAK, deliberately BELOW the hand-want rules (user, 2026-08-18: "it is a
    // tie-break for cases that tie in the other rules"). Its job is picking a dual's SECOND colour
    // once a higher rule has fixed the first: wanting red for Mana Cannons does not say whether to
    // take Steam Vents or Blood Crypt, and breadth answers with whichever other half we are
    // missing. Promoting it above spell_new was built and measured -- byte-identical on all 8 train
    // cells, because breadth >= accel_new + spell_new means the two agree whenever both differ.
    // Measured: breadth differs between the top two 12.0% of picks but decides 0.9%; the other 92%
    // are the ones where the hand already named that colour.
    auto rank_of = [](const Key& k)
    {
        return std::array<int, 8>{ k.enables_now, k.untapped, k.accel_new, k.spell_new,
                                   k.breadth, k.soft_new, k.depth, k.colours };
    };
    std::stable_sort(keys.begin(), keys.end(), [&](const Key& a, const Key& b)
    {
        const std::array<int, 8> va = rank_of(a), vb = rank_of(b);
        if (va != vb) { return va > vb; }
        return a.name < b.name;
    });

    std::vector<std::string> out;
    out.reserve(keys.size());
    for (const Key& k : keys) { out.push_back(k.name); }
    // TEMPORARY instrument (MTG_FETCHKEY): which key actually SETTLES the pick, i.e. the first
    // one on which the top two candidates differ. "name" means every ranking term tied and the
    // choice fell through to the alphabetical backstop -- i.e. the doctrine said nothing.
    if (EnvOn("MTG_FETCHKEY") && keys.size() >= 2)
    {
        const Key& a = keys[0];
        const Key& b = keys[1];
        const char* who = "name";
        if      (a.enables_now != b.enables_now) { who = "enables_now"; }
        else if (a.accel_new   != b.accel_new)   { who = "accel_new";   }
        else if (a.spell_new   != b.spell_new)   { who = "spell_new";   }
        else if (a.breadth     != b.breadth)     { who = "breadth";     }
        else if (a.soft_new    != b.soft_new)    { who = "soft_new";    }
        else if (a.untapped    != b.untapped)    { who = "untapped";    }
        else if (a.depth       != b.depth)       { who = "depth";       }
        else if (a.colours     != b.colours)     { who = "colours";     }
        // Also: which keys DIFFER between the top two at all. A key that often differs but rarely
        // decides is MASKED by a correlated key above it -- a different fact from a key that never
        // differs, which is structurally unable to say anything.
        std::string diff;
        diff += (a.enables_now != b.enables_now) ? 'e' : '.';
        diff += (a.accel_new   != b.accel_new)   ? 'a' : '.';
        diff += (a.spell_new   != b.spell_new)   ? 's' : '.';
        diff += (a.breadth     != b.breadth)     ? 'b' : '.';
        diff += (a.soft_new    != b.soft_new)    ? 'f' : '.';
        diff += (a.untapped    != b.untapped)    ? 'u' : '.';
        diff += (a.depth       != b.depth)       ? 'd' : '.';
        diff += (a.colours     != b.colours)     ? 'c' : '.';
        // Diagnostic for the KNOWN GAPS in the rule-6 unlock predicate (see the design doc):
        //  nounlock : nothing at all unlocks, so both untapped terms are silent this fetch
        //  spare    : ... yet we CAN already cast something -- so the extra mana might have paid
        //             for a SECOND spell, which the per-card "already castable -> skip" test
        //             cannot see. Colour-blind greedy count, an upper bound on the gap.
        int nounlock = 1;
        for (int i = 0; i < NC; ++i) { if (any_unlocks[i]) { nounlock = 0; } }
        int spare = 0;
        if (nounlock)
        {
            const ManaPool pool_now = AvailableManaPool(s);
            std::vector<int> mvs;
            for (const Card& c : ap.hand)
            {
                const CardDefinition* dd = CardDatabase::Instance().LookupCached(c);
                if (dd && !dd->card.IsLand()) { mvs.push_back(dd->card.m_mana_cost.ManaValue()); }
            }
            std::sort(mvs.begin(), mvs.end());
            auto fit = [&](int budget) { int n = 0; for (int v : mvs) { if (v > budget) { break; } budget -= v; ++n; } return n; };
            const int m = pool_now.Total();
            if (fit(m + 1) > fit(m)) { spare = 1; }
        }
        std::cerr << "[fetchkey] nounlock=" << nounlock << " spare=" << spare
                  << " decide=" << who << " diff=" << diff
                  << " top1=" << a.name << "(b" << a.breadth << " s" << a.spell_new << ")"
                  << " top2=" << b.name << "(b" << b.breadth << " s" << b.spell_new << ")\n";
    }
    // TEMPORARY diagnostic (MTG_FETCHRANK): print the ordered candidate list with its keys.
    if (EnvOn("MTG_FETCHRANK"))
    {
        static const char* kCol = "WUBRGC";
        std::cerr << "[fetchrank T" << s.turn_number << " types=";
        for (const std::string& t : fetch_pp.fetch_land_types) { std::cerr << t << ","; }
        std::cerr << " src=";
        for (int i = 0; i < NC; ++i) { if (src_cnt[i]) { std::cerr << kCol[i] << src_cnt[i]; } }
        std::cerr << " land=";
        for (int i = 0; i < NC; ++i) { if (land_cnt[i]) { std::cerr << kCol[i] << land_cnt[i]; } }
        std::cerr << " wantdeep=";
        for (int i = 0; i < NC; ++i) { if (want_deep[i]) { std::cerr << kCol[i] << want_deep[i]; } }
        std::cerr << " unlock=";
        for (int i = 0; i < NC; ++i) { if (any_unlocks[i]) { std::cerr << kCol[i]; } }
        std::cerr << " aunlock=";
        for (int i = 0; i < NC; ++i) { if (accel_unlocks[i]) { std::cerr << kCol[i]; } }
        std::cerr << "] ";
        for (const Key& k : keys)
        {
            std::cerr << k.name << "(e" << k.enables_now << " a" << k.accel_new << " s" << k.spell_new
                      << " b" << k.breadth << " f" << k.soft_new << " u" << k.untapped
                      << " d" << k.depth << " c" << k.colours << ") ";
        }
        std::cerr << "\n";
    }
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
    const MirrorwingProvider     g_mirrorwing;
    const EquipmentProvider      g_equipment;
    const StompyProvider         g_stompy;
    const MinotaurProvider       g_minotaur;
    const DragonsProvider        g_dragons;
    const AurasProvider          g_auras;
}

const DecisionProvider& DefaultProvider()
{
    return g_generic;
}

const DecisionProvider& DetectDecisionProvider(const Decklist& deck)
{
    // Archetype detection by card params (same shape as GoldFishRunner::DeckUsesSecondMain).
    // Order matters only if a deck mixed signatures; today each is exclusive (verified).
    bool anti = false, th = false, vial = false, hinata = false, burn = false, dragonstorm = false;
    bool goblin = false, gift = false, fivec = false, mirrorwing = false, equipment = false;
    bool stompy = false;   // StompySurprise (elf ramp) -- routes to Generic BEFORE the anti check
    bool minotaur = false; // Minotaur tribal -- routes to Generic BEFORE the goblin check
    bool dragons = false;  // Mono-red Dragons ramp -- routes to Generic BEFORE the goblin check
    bool aura = false;     // Bogle Auras -- Light-Paws' aura_cast_tutor_attach is unique to it
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
        // Minotaur tribal (Rakdos aggro). MUST be detected and MUST win over the goblin check
        // below, because Slaughter-Priest of Mogis carries sac_creature_outlet and that ALONE sets
        // the Goblin signature -- the exact misroute class that already caught Mirrorwing (Goblin
        // Instigator) and StompySurprise (Hornet Queen's etb_self_creates_tokens). Verified for
        // real before this fix: `mtg --batch` reported `provider=Goblins` for Minotaur, and
        // GoblinsProvider::DeferSacOutletPreCombat then deferred Slaughter-Priest's outlet to the
        // SECOND MAIN -- which this deck does not have (DeckUsesSecondMain is false: no spectacle,
        // no lifegain_to_loss, no Goblin Lackey). So the outlet was not merely delayed, it was
        // silently DELETED from the autonomous search: a Goblins-tuned narrowing removing a real
        // decision branch from another deck, which is precisely what the core invariant forbids.
        // (It is doubly wrong here: the outlet's live effect is Slaughter-Priest's own +2/+0
        // sacrifice trigger, a COMBAT pump, so deferring it past combat destroys all of its value.)
        //
        // Signature = any of the Minotaur-only gated params, OR'd so a deckbuilding swap that drops
        // one card cannot silently lose the signature (the deck-screening lesson). Every one of
        // these is new and gated, and no other deck in cards.json carries any of them.
        if (p.hand_size_anthem_max >= 0
            || !p.etb_damage_devotion_color.empty()
            || !p.reduces_subtype_colored_subtype.empty()
            || p.attack_pump_matching_power > 0
            || p.sacrifice_watch_pump_power > 0
            || p.team_pump_grants_haste
            || p.bestow_cost.has_value()
            || p.must_attack)
        {
            minotaur = true;
        }

        // Mono-red Dragons ramp. MUST be detected and MUST win over the goblin check below, for the
        // FOURTH occurrence of the misroute class already recorded here for Mirrorwing (Goblin
        // Instigator), StompySurprise (Hornet Queen) and Minotaur (Slaughter-Priest): Dragonspeaker
        // Shaman carries reduces_spell_subtype ("Dragon spells you cast cost {2} less"), which is on
        // the Goblin signature only because Goblin Warchief reads the same way for Goblins. The
        // param is ARCHETYPE-NEUTRAL -- it says "reduces spells of subtype X", not "is a Goblin
        // deck" -- so keying a provider on it routes every tribal cost-reducer to GoblinsProvider.
        //
        // Measured: `mtg --batch` reported provider=Goblins for Dragons. Four of the five Goblins
        // hooks were inert (no sac outlet, no echo, no tutors), but ForcedEarlyLandName was NOT: it
        // prunes the turn-1 land drop to Mountain whenever one is in hand. Benign-looking for a
        // mono-red deck holding 4 Lightning Bolt, and quite possibly right -- but it was never
        // measured FOR THIS DECK, and the core invariant is that a deck gets another archetype's
        // narrowing only on evidence, never by accident.
        //
        // Routes to GenericProvider: Dragons has no measured deck heuristic to hold, so per the same
        // rule applied to Minotaur it gets no narrowing at all. If a Dragons hook is ever proposed
        // and measured, THAT is when this becomes a DragonsProvider.
        //
        // Signature = Dragons-only gated params, OR'd across FIVE cards (Scourge of Valkas, Dragon
        // Tempest, Utvara Hellkite, Haven of the Spirit Dragon, Inferno of the Star Mounts) so a
        // deckbuilding swap that cuts one card cannot silently lose it -- the deck-screening lesson.
        // Deliberately EXCLUDES Lightning Greaves' equip_grants_haste/shroud: those are colourless
        // staples that any deck may add, and keying on them would recreate this very bug pointing
        // the other way.
        if (p.dragon_ping_on_enter
            || p.attack_per_matching_creates_tokens > 0
            || p.haste_on_flying_enter
            || !p.gy_return_requires_subtype.empty()
            || p.firebreathing_threshold_power > 0)
        {
            dragons = true;
        }

        if (p.sac_creature_outlet
            || !p.tap_creates_tokens_per_controlled_subtype.empty()
            || !p.reduces_spell_subtype.empty()
            || !p.dies_watch_subtype.empty()
            || !p.combat_damage_puts_subtype_from_hand.empty()
            || p.etb_self_creates_tokens > 0)
        {
            goblin = true;
        }

        // Mirrorwing/Zada spell-copy swarm: the copy magnet IS the archetype signature (no other
        // deck carries copies_solo_targeted_spells). Detected BEFORE the goblin check for the same
        // reason Goblins is detected before anti: this deck's Goblin Instigator carries
        // etb_self_creates_tokens, which would otherwise set `goblin` and misroute the whole deck
        // to GoblinsProvider. Routes to GenericProvider (no archetype hooks yet) -- the trick
        // TARGET is a searched plan variant, not a provider decision, so Generic is exactly right.
        if (p.copies_solo_targeted_spells) { mirrorwing = true; }

        // Bogle Auras: Light-Paws' cast-an-Aura tutor-attach is unique to this deck. The provider
        // exists for the Horizon Canopy dig hooks (see AurasProvider); everything else is Generic.
        if (p.aura_cast_tutor_attach) { aura = true; }

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

        // Equipment aggro (KittyEquipment): any equipment-deck gated param marks the deck.
        // Stoneforge Mystic carries tutor_to_hand, which would otherwise trip the anti-lifegain
        // signature below and misroute the whole deck to AntiLifegainProvider (the Goblin-Matron
        // misroute class). Every field here is new + gated (0/false/empty inert), so only a deck
        // carrying the new equipment params sets this -- all existing decks are byte-identical.
        if (p.attack_dig_attach_count > 0 || p.equip_combat_damage_charges > 0
            || p.tap_put_from_hand_cost.has_value() || p.attach_all_equipment_cost.has_value()
            || p.metalcraft_equip_zero_artifacts || p.draw_on_equipment_etb
            || p.upkeep_tokens_per_equipment > 0 || p.double_strike_while_equipped)
        {
            equipment = true;
        }

        // StompySurprise (mono-green elf ramp): any of its gated params marks the deck. Its
        // Worldly Tutor carries tutor_to_top, which would otherwise trip the anti-lifegain
        // signature below and misroute the whole deck to AntiLifegainProvider (the Goblin-Matron
        // misroute class -- its tutor heuristic hunts lifegain_to_loss enablers this deck does
        // not run). Routes to StompyProvider (Generic + the cleanup-discard bucket policy).
        if (p.etb_team_pump_per_creature || p.activated_reveal_top_cost.has_value()
            || !p.sac_additional_creature_color.empty()
            || !p.mana_requires_land_subtype.empty()
            || (def->card.IsCreature() && !p.mana_per_creature_subtype.empty()))
        {
            stompy = true;
        }

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
    // Mirrorwing/Zada swarm: MirrorwingProvider (Generic + the trick-target 5f prune); must WIN
    // OVER goblin (its Goblin Instigator sets that signature -- see the detection note above).
    if (mirrorwing) { return g_mirrorwing; }
    // Goblins ride GoblinsProvider. This return WINS OVER anti (Goblin Matron's tutor_to_hand would
    // otherwise set anti and misroute the deck to AntiLifegainProvider) and over th/vial/burn/generic.
    // It sits below dragonstorm/hinata only for tidiness -- a Goblins deck carries none of those
    // signatures (no tutor_to_battlefield / hinata_cost_reducer).
    //
    // "Exclusivity" here is MAINTAINED, not intrinsic. This branch fires on any Goblin gated param,
    // and four decks have now reached it without being Goblins -- Mirrorwing, StompySurprise,
    // Minotaur and Dragons -- each caught only after the fact, because several of the params below
    // are archetype-NEUTRAL (etb_self_creates_tokens, sac_creature_outlet, reduces_spell_subtype
    // describe a card's behaviour, not a deck's identity). Every such deck is therefore routed
    // ABOVE this line, and the standing check that a new deck has not silently landed here is the
    // provider step in .claude/skills/analyze-deck.md (`scripts/provider_audit.py`).
    //
    // Was g_generic until GoblinsProvider had MEASURED hooks to hold: the sac-outlet deferral and the
    // Matron tutor width (12, -0.0620 held-out). It derives from GenericProvider and overrides only
    // those, so every other decision still resolves through exactly the code this deck used before.
    // StompySurprise: StompyProvider (Generic + the user's cleanup-discard bucket policy; every
    // other hook inherits Generic). Must WIN OVER goblin -- Hornet Queen's etb_self_creates_tokens
    // sets that signature on its own (the exact Mirrorwing/Instigator misroute class: until
    // 2026-08-21 this deck silently ran under GoblinsProvider) -- and over anti (Worldly Tutor's
    // tutor_to_top).
    if (stompy) { return g_stompy; }
    // Minotaur: MinotaurProvider (Generic + the user-amended cleanup-discard bucket policy; every
    // other hook inherits Generic, and its Aether Vials stay on the ROOT hand-aware charge policy
    // that has been the default since 2026-08-18). Placed above goblin for the reason spelled out in
    // the detection block. It rode g_generic from the 2026-08-21 misroute fix until the bucket policy
    // was authored, user-amended and measured -- a deck earns its own provider only once it has a
    // hook to hold, which is the same rule Dragons went through in both directions.
    if (minotaur) { return g_minotaur; }
    // Dragons: DragonsProvider (Generic + the user-approved cleanup-discard bucket policy; every
    // other hook inherits Generic). Must still sit ABOVE goblin for the reason spelled out in the
    // detection block -- Dragonspeaker Shaman's reduces_spell_subtype sets that signature on its
    // own. It was routed to g_generic when that misroute was fixed, because a deck earns its own
    // provider only once it has a MEASURED hook to hold; the bucket policy is that hook.
    if (dragons) { return g_dragons; }
    if (goblin) { return g_goblins; }
    // Equipment aggro; must WIN OVER anti (Stoneforge Mystic's tutor_to_hand sets that signature
    // on its own -- see the equipment detection note above). No other deck carries the equipment
    // gated params, so exclusivity is preserved.
    if (equipment) { return g_equipment; }
    // Creature Giving; must WIN OVER anti (see the gift detection note above).
    if (gift) { return g_creature_giving; }
    // FiveColour; must WIN OVER anti (its fetchlands set that signature on their own -- see the
    // domain_mana detection above). No other deck has domain_mana, so exclusivity is preserved.
    if (fivec) { return g_fivecolour; }
    // Bogle Auras: only the dig hooks differ from Generic (Horizon Canopy sac-draw). No other
    // deck carries aura_cast_tutor_attach, so exclusivity is preserved; the deck trips no other
    // signature (verified: it routed to Generic before this provider existed).
    if (aura) { return g_auras; }
    if (anti) { return g_antilife; }
    if (th)   { return g_treasure; }
    if (vial) { return g_vial; }
    if (burn) { return g_burn; }
    return g_generic;
}

const DecisionProvider& SelectDecisionProvider(const Decklist& deck)
{
    // MTG_PROVIDER_DECK=<decklist path>: pin every game's provider to the one DETECTED for THAT
    // decklist (value-carrying flag; unset/empty = detect per deck, byte-identical). The screening
    // driver sets it to the spec's BASE deck: an arm there is a DECLARED modification of that deck,
    // so its identity is given by the spec, not re-derived from an edited list that may have lost
    // (or gained) a signature card -- detection by card params has silently misrouted decks three
    // times, and an edit crossing a signature hands one arm of a comparison another deck's
    // heuristics (user directive 2026-08-13: in the modification context there must be NO room for
    // that error). Detection still runs on the edited list for REPORTING (BatchRunner's [play]
    // line), never for routing. An unreadable pin path throws out of the static initializer --
    // aborting loudly is the point; a silent fall-back to detection would reintroduce exactly the
    // error class the pin removes. Hooks keyed on a card the pinned deck's edit removed are inert
    // (they fire on card params present in play), which is the "present-but-inert without its
    // card" behaviour the provider-reuse design asked for, obtained structurally.
    static const DecisionProvider* const pinned = []() -> const DecisionProvider* {
        const char* p = std::getenv("MTG_PROVIDER_DECK");
        if (!p || !*p) { return nullptr; }
        return &DetectDecisionProvider(DeckLoader::LoadFromFile(p));
    }();
    if (pinned != nullptr) { return *pinned; }
    return DetectDecisionProvider(deck);
}

// ---- MirrorwingProvider::TrickTargetCandidates ------------------------------
//
// PERFORMANCE prune (5f), grounded in what the payloads can actually do. A solo-target trick's
// enumerated target group multiplies the plan odometer; measured on seed 45 gi3 (d3, board 7-10)
// the per-target groups drove sum_odo ~3M/game (Scale the Heights avg 979, max 18000) -- ~13 s a
// game where the suite budgets ~1. The lines a target choice can distinguish:
//   * target a COPY MAGNET (Zada / Mirrorwing) -> the whole-board fan-out. Dominates any single
//     non-magnet target for every symmetric payload (the fan-out delivers that same payload to
//     the same creature AND every other, for the same mana).
//   * target the best READY attacker -> the "no magnet / pump the attacker" line.
//   * haste/copy payloads (Expedite, Twinflame) additionally care about a SICK body (haste it /
//     copy it into a hasted token) -- keep the best sick creature, and the best HAND creature
//     (the cast-it-then-trick-it line).
// Everything else (pumping a lesser dork, hasting an already-ready creature, copying a token) is
// weakly dominated by one of the kept candidates. MTG_UNPRUNED / MTG_UNPRUNE=tricktarget opens
// the full set for the definitive with/without A/B (5f).
void MirrorwingProvider::TrickTargetCandidates(const GameState& s, const CardDefinition& def,
                                               std::vector<int>& out) const
{
    if (DecisionUnpruned(UnprunedGate::TrickTarget)) { return; }   // empty = enumerate all
    const int me = s.active_player_index;
    const bool wants_sick = def.params.grants_temp_haste || def.params.token_copy_of_target;

    int best_ready = 0, best_ready_pw = -1;   // best attack-eligible non-magnet (m_number, power)
    int best_sick  = 0, best_sick_pw  = -1;   // best non-eligible creature
    int own_creatures = 0;
    std::unordered_set<std::string> seen_magnet;   // (name, eligibility) equivalence -- a 2nd
                                                   // identical Mirrorwing is the same choice
    for (const Permanent& p : s.battlefield)
    {
        if (p.controller_index != me || !p.card.IsCreature()) { continue; }   // tokens included
        ++own_creatures;
        const CardDefinition* d = CardDatabase::Instance().LookupCached(p.card);
        if (d && d->params.copies_solo_targeted_spells)
        {
            std::string eq = p.card.m_name.str()
                           + (CanAttackFull(p, s.battlefield, me) ? "/A" : "/s");
            if (seen_magnet.insert(eq).second) { out.push_back(p.card.m_number); }
            continue;
        }
        const int pw = p.EffectivePower();
        if (CanAttackFull(p, s.battlefield, me))
        { if (pw > best_ready_pw) { best_ready_pw = pw; best_ready = p.card.m_number; } }
        else
        { if (pw > best_sick_pw)  { best_sick_pw  = pw; best_sick  = p.card.m_number; } }
    }
    // HUMAN PLAY: a narrowing may hide a TARGET, but it must never delete the CAST.
    //
    // Every pass below ranks targets by what the AI would gain, and when nothing scores the result
    // is the `{0}` sentinel -- which does not narrow a choice, it removes the spell from the plan
    // list entirely, so the human cannot cast it AT ALL. Measured on the recorded Mirrorwing
    // s3_gi2 T5 board (no magnet out or in hand, pending attack short of the gap-closing bound):
    // four own creatures, Twinflame in hand, {1}{R} trivially affordable -- and ZERO Twinflame
    // plans offered, on the exact turn that reference records casting it.
    //
    // ONE representative is the whole fix, because the target is re-asked at RESOLUTION off the
    // board and that chooser deliberately ignores this narrowing (see UnpruneHumanExempt): the
    // human still picks any creature they like. So this costs one option-group entry, NOT the
    // per-target fan-out that opening UnprunedGate::TrickTarget would cost -- which was measured
    // at 30ms -> 2.1s -> 21s -> 31s per segment, past the viewer's 120s step timeout.
    // Autonomous play is untouched: the doctrine that Twinflame waits for a magnet still holds
    // wherever no human is driving.
    auto human_keep_one_target = [&]()
    {
        if (!HumanPlayActive() || !out.empty()) { return; }
        int pick = 0, pick_pw = -1;
        for (const Permanent& p : s.battlefield)
        {
            if (p.controller_index != me || !p.card.IsCreature() || p.card.m_number == 0)
            { continue; }
            const int pw = p.EffectivePower();
            if (pw > pick_pw) { pick_pw = pw; pick = p.card.m_number; }
        }
        if (pick != 0) { out.push_back(pick); }
    };
    // Twinflame (token_copy_of_target) target policy (user, Stage-6 round 3): the go-off is the
    // MAGNET fan-out -- ungated (the draw-breakpoint re-solves own "might draw into lethal", so
    // the search tries it whenever a magnet target exists; no heuristic lethal gate). Without a
    // magnet anywhere the copies are small, so a non-magnet target is offered ONLY in the rare
    // gap-closing corner ("you would only cast it when you are 1 damage short"): the pending
    // attack falls short of lethal by no more than every copyable printed power (optimistic sum
    // -- the safe direction; the search still decides). Otherwise Twinflame waits in hand.
    if (def.params.token_copy_of_target)
    {
        const bool magnet_bf = !out.empty();   // battlefield magnets pushed above
        bool magnet_hand = false;
        for (const Card& hc : s.players[me].hand)
        {
            const CardDefinition* hd = CardDatabase::Instance().LookupCached(hc);
            if (hd && hd->params.copies_solo_targeted_spells) { magnet_hand = true; break; }
        }
        if (!magnet_bf && !magnet_hand)
        {
            int pending = 0, copy_sum = 0, best_pw = -1, best_num = 0;
            for (const Permanent& p : s.battlefield)
            {
                if (p.controller_index != me || !p.card.IsCreature()) { continue; }
                if (CanAttackFull(p, s.battlefield, me)) { pending += p.EffectivePower(); }
                const int ppw = p.card.m_power.value_or(0);
                copy_sum += ppw;
                if (ppw > best_pw && p.card.m_number != 0) { best_pw = ppw; best_num = p.card.m_number; }
            }
            const int opp_life = s.players[1 - me].life;
            if (pending < opp_life && pending + copy_sum >= opp_life && best_num != 0)
            { out.push_back(best_num); }
        }
        // Same-plan hand-magnet targets ("cast Zada, then Twinflame at it" -- the go-off's
        // other entry point), deduped by name; the generic hand loop below is skipped.
        std::unordered_set<std::string> seen_hand_magnet;
        for (const Card& hc : s.players[me].hand)
        {
            if (hc.m_number == 0 || hc.m_is_staged) { continue; }
            const CardDefinition* hd = CardDatabase::Instance().LookupCached(hc);
            if (!hd || !hd->params.copies_solo_targeted_spells) { continue; }
            if (seen_hand_magnet.insert(hc.m_name.str()).second) { out.push_back(hc.m_number); }
        }
        human_keep_one_target();                 // narrowing may hide a target, not the cast
        if (out.empty()) { out.push_back(0); }   // narrowing active, no candidates
        return;
    }
    // USER rule (2026-08-12, "We should always do so"): with a magnet on the battlefield, the
    // fan-out delivers every symmetric payload (pump / haste / draw / Treasure) to the target
    // AND every other creature for the same mana, so every single-target line is dominated --
    // enumerate the magnet target(s) ONLY. Lone exception: a mass-draw payload against a nearly
    // empty library could deck us; keep the best single target as the escape line. (Twinflame
    // returned above: its copy SET differs by target, so its bf/hand magnets both stay.)
    if (!out.empty())
    {
        const bool deck_risk = def.params.cast_draw > 0
            && s.players[me].library.size() <= static_cast<std::size_t>(own_creatures + 2);
        if (deck_risk && best_ready != 0) { out.push_back(best_ready); }
        return;
    }

    if (best_ready != 0)               { out.push_back(best_ready); }
    if (wants_sick && best_sick != 0)  { out.push_back(best_sick); }
    const bool have_bf_target = !out.empty();   // a target that exists RIGHT NOW (see rider fallback)

    // Hand candidates (the same-plan "cast it, then point the trick at it" line): every hand
    // MAGNET (deduped by name), plus -- for haste/copy payloads -- the biggest hand creature.
    const Player& ap = s.players[me];
    std::unordered_set<std::string> seen;
    int best_hand = 0, best_hand_pw = -1;
    for (const Card& hc : ap.hand)
    {
        if (hc.m_number == 0 || hc.m_is_staged) { continue; }
        const CardDefinition* hd = CardDatabase::Instance().LookupCached(hc);
        if (!hd || !hd->card.IsCreature()) { continue; }
        if (hd->params.copies_solo_targeted_spells)
        {
            if (seen.insert(hc.m_name.str()).second) { out.push_back(hc.m_number); }
            continue;
        }
        const int pw = hd->card.m_power.value_or(0);
        if (wants_sick && pw > best_hand_pw) { best_hand_pw = pw; best_hand = hc.m_number; }
    }
    // RIDER FALLBACK. Every pass above ranks a battlefield body by ATTACK value, so a board whose
    // only creature is TAPPED (the post-combat main, after it swung) keeps NO present-tense target
    // -- and if a hand magnet was kept, the only enumerated variant needs that creature cast in the
    // same subset, which a post-combat mana pool usually can't afford. The trick then vanishes
    // entirely, even though "cast Fists of Flame to DRAW A CARD" is a real line whose pump target
    // is irrelevant (it dropped the recorded Mirrorwing s6 gi5 T3 post-main line). Same gap as the
    // opponent-target one in CollectActions: a rider trick is bought for the rider, not the pump --
    // so keep one body that exists NOW whenever none of the passes above did.
    // MTG_NO_TRICK_RIDER_FALLBACK=1 restores the attack-only narrowing (the A/B lever: this widens
    // the target group by one on post-combat boards, so it must be separable from the session's
    // other Mirrorwing change when reading a suite delta).
    static const bool s_rider_fallback = !EnvOn("MTG_NO_TRICK_RIDER_FALLBACK");
    if (s_rider_fallback && !have_bf_target && best_sick != 0
        && (def.params.cast_draw > 0 || def.params.creates_treasures > 0
            || def.params.cast_lifegain > 0 || def.params.grants_extra_land_drop > 0))
    { out.push_back(best_sick); }
    // out non-empty => CollectActions emits exactly these; if the board/hand had no candidates at
    // all (empty out would mean "no narrowing"), push a sentinel-free fallback: with no creatures
    // there are no legal targets and the caller's loops emit nothing anyway -- but out.empty()
    // must not silently mean "all", so mark narrowing active with a 0 (skipped by the caller).
    human_keep_one_target();                 // narrowing may hide a target, not the cast
    if (out.empty()) { out.push_back(0); }
}

// ---- MirrorwingProvider::TrickCastSensible ----------------------------------
// Gold Rush cast gate (USER doctrine, 2026-08-12). Magnetless Gold Rush is {1}{G} in for ONE
// Treasure out -- a net mana LOSS this turn, so it is never a this-turn mana play. It is real as:
//   (a) the magnet fan-out (Zada/Mirrorwing out: k copies -> k Treasures + mass pump),
//   (b) a pump that could push THIS turn's lethal (+2/+2 per Treasure incl. banked ones;
//       optimistic keep -- the search still decides whether the line actually wins),
//   (c) ramp / mana-screw mitigation: banking toward mana-constrained gas in hand ("essentially
//       a way to drop a Zada or Mirrorwing a turn or more earlier", incl. a needed colour no
//       current source produces), or
//   (d) redundancy: >=2 copies in hand with a real board -- "no point holding back".
// Anything else is "cast Gold Rush just to get a treasure", which a pilot does not do -- and
// which multiplied the searched-breakpoint fan-out for nothing (the 2026-08-12 label-path
// profile: 63.5M wave candidates, improved=0). Game-understanding filter, not a width cap;
// MTG_UNPRUNE=treasuretrickcast restores the ungated enumeration.
bool MirrorwingProvider::TrickCastSensible(const GameState& s, int me,
                                           const CardDefinition& def) const
{
    if (DecisionUnpruned(UnprunedGate::TreasureTrickCast)) { return true; }

    int treasures = 0, atk_power = 0, total_power = 0, pot = 0, creatures_bf = 0;
    bool have_color[6] = { false, false, false, false, false, false };
    for (const Permanent& p : s.battlefield)
    {
        if (p.controller_index != me) { continue; }
        const CardDefinition* d = CardDatabase::Instance().LookupCached(p.card);
        if (d && d->params.copies_solo_targeted_spells) { return true; }   // (a) magnet out
        if (p.card.m_name == "Treasure Token") { ++treasures; ++pot; continue; }
        if (p.card.IsCreature())
        {
            ++creatures_bf;
            total_power += p.EffectivePower();   // ALL bodies: haste/copy tricks unlock them
            if (d && CanAttackFull(p, s.battlefield, me)) { atk_power += p.EffectivePower(); }
        }
        const bool src = (p.card.IsLand() && !p.tapped)
                      || (d && d->tmpl == CardTemplate::ManaDork && CanTapNow(p, s.battlefield))
                      || (d && d->params.mana_rock && !p.tapped);
        if (src && d)
        {
            ++pot;
            for (Color c : EffectiveProduces(s, me, *d)) { have_color[static_cast<int>(c)] = true; }
        }
    }

    // One hand scan feeding every clause: gas totals + colour-fix (c), candidate redundancy (d),
    // and the SAME-TURN combat potential clause (b) must see (Twinflame token-copies the swarm,
    // Expedite unlocks sick bodies, a held Fists pumps per draw -- all can precede this cast
    // inside one plan).
    int gr_in_hand = 0, gas_mv_sum = 0, fists_in_hand = 0;
    bool copy_trick_in_hand = false, haste_in_hand = false;
    for (const Card& hc : s.players[me].hand)
    {
        const CardDefinition* hd = CardDatabase::Instance().LookupCached(hc);
        if (hd == nullptr || hd->card.IsLand()) { continue; }
        if (hd->params.token_copy_of_target) { copy_trick_in_hand = true; }
        if (hd->params.grants_temp_haste) { haste_in_hand = true; }
        if (hc.m_name == def.card.m_name) { ++gr_in_hand; continue; }   // GR is not its own gas
        if (hd->params.pump_per_cards_drawn_power > 0) { ++fists_in_hand; }
        const ManaCost& mc = hd->card.m_mana_cost;
        // (c) "lacking in mana but have a lot of gas" is a SUM condition (gi145: two 3-MV Fists
        // vs pot 5 -- no single card is constrained, but the Treasure lets BOTH resolve in one
        // turn). Accumulate total gas demand; compared against pot after the loop. The
        // single-big-card ramp case (magnet above the curve) is subsumed by the sum.
        gas_mv_sum += mc.ManaValue();
        // Colour-fix corner: affordable on count but a required colour has no producer.
        if ((mc.white > 0 && !have_color[static_cast<int>(Color::White)])
            || (mc.blue  > 0 && !have_color[static_cast<int>(Color::Blue)])
            || (mc.black > 0 && !have_color[static_cast<int>(Color::Black)])
            || (mc.red   > 0 && !have_color[static_cast<int>(Color::Red)])
            || (mc.green > 0 && !have_color[static_cast<int>(Color::Green)]))
        { return true; }
    }

    // (b) optimistic this-turn lethal. The pre-cast board is NOT the ceiling of "this turn":
    // gi17 (smoke s1001) cast Twinflame THEN Gold Rush, the Treasure pump finishing the doubled
    // swarm -- a projection off eligible attackers alone pruned that winning finisher, and no
    // budget recovers a hard prune. Project the optimistic ceiling from board + hand together;
    // overshoot only KEEPS a candidate the search then judges on its merits.
    // Two attack shapes bound the ceiling, and every pump (Treasure / Fists / the candidate's
    // own payload) lands on whichever shape is taken:
    //   SWARM -- every eligible body (all bodies when a haste/copy trick in hand frees the sick
    //   ones), doubled by a held Twinflame's hasty tokens;
    //   ALONE -- the best single attacker plus the FULL Exalted count (gi87's real win: three
    //   0/1 Hierarchs turn Mystic's lone swing into 4; projecting only summed printed power
    //   scored that lethal at 6 of 8 and blocked the winning Gold Rush AT the lethal state).
    const bool tricks_free_sick = copy_trick_in_hand || haste_in_hand;
    const bool has_attacker = atk_power > 0 || (tricks_free_sick && total_power > 0)
                           || (CountExalted(s.battlefield, me) > 0 && creatures_bf > 0);
    if (has_attacker)
    {
        const int drawn = s.players[me].cards_drawn_this_turn;
        int swarm = tricks_free_sick ? total_power : atk_power;
        if (copy_trick_in_hand) { swarm *= 2; }                      // hasty copies of the swarm
        int best_single = 0;
        for (const Permanent& p : s.battlefield)
        {
            if (p.controller_index != me || !p.card.IsCreature()) { continue; }
            if (tricks_free_sick || CanAttackFull(p, s.battlefield, me))
            { best_single = std::max(best_single, p.EffectivePower()); }
        }
        const int alone = best_single + CountExalted(s.battlefield, me);
        int proj = std::max(swarm, alone);
        proj += 2 * (treasures + def.params.creates_treasures);      // Treasure pump on a target
        if (def.params.pump_per_cards_drawn_power > 0)               // the candidate's own pump
        { proj += def.params.pump_per_cards_drawn_power * (drawn + def.params.cast_draw + 1); }
        if (fists_in_hand > 0) { proj += drawn + 2; }                // a held Fists can also fire
        if (proj >= s.players[1 - me].life) { return true; }
    }

    // (A draw-trick HOLD rule lived here 2026-08-12 and was retired the same day by
    // measurement: zero branching reduction on the label path, and a persistent line-loss
    // (smoke gi99: the winning line spends Fists as tempo and never deploys the in-hand
    // Zada -- two lands through T4 meant "everything in place" was never true). USER call:
    // "if it doesn't reduce branching we can skip it.")

    // (c) mana-poor relative to TOTAL gas in hand. DELIBERATELY LOOSE -- admission is not
    // choice. A next-turn "acceleration" tightening was built and REVERTED the same day
    // (2026-08-25): it was compensating for the real §2a defect (BuildNonCreaturePool missing
    // the pay-sac term starved the deferred continuations that make an admitted Gold Rush GOOD
    // or BAD visible to the search), and once that was fixed the tightening only *cost* lines --
    // mw74's winning T2 bank is SPECULATIVE (Mirrorwing drawn next turn, made castable by the
    // banked Treasure), which no hand-aware acceleration test can credit. With the pools honest,
    // the search itself separates mw68's bad early GR (a body forgone) from mw74's good one.
    if (gas_mv_sum > pot) { return true; }
    // (d) redundancy: the candidate itself is in hand, so >=2 means multiple copies held.
    if (gr_in_hand >= 2 && creatures_bf >= 2) { return true; }
    // (e) combat-pump chip (gi87: old T6 win -> gated T9). The gi87 denial profile was flooded
    // endgame states -- pot 8-11, hand gas 1-2, a dork board chipping for 1-3 -- where the
    // ungated search casts GR as a 2-mana COMBAT TRICK: the Treasure pump lands on the attacker
    // the same turn (tripling a 1-power chip), and that faster chip is what builds the lethal
    // window the (b) finisher needs. "Never a this-turn play" is true of the Treasure-as-mana,
    // FALSE of the pump. A pilot pumps the attack whenever the mana would otherwise idle: an
    // attack under way, and pot spare beyond the hand's whole gas plus GR itself.
    if ((atk_power > 0 || (creatures_bf > 0 && CountExalted(s.battlefield, me) > 0))
        && pot >= gas_mv_sum + 2)
    { return true; }

    return false;   // magnetless bank with no use -- not a line
}

// ---- MirrorwingProvider::LegendKeepIndex ------------------------------------
//
// User directive (Stage 6 review, analysis-Mirrorwing Dragon.md): the base keep-the-oldest rule is
// wrong in exactly one corner -- Zada is summoning-sick, Twinflame makes a hasty token copy of it,
// and attacking WITH the token wins this turn. The token exiles at end of turn either way, so
// giving up the original only pays when it converts THIS turn into the win; in every other state
// keep-the-oldest is strictly better (the original stays, the token was temporary).
//
// Decided by SIMULATING combat on scratch copies (both arms), not by the pending-damage
// projection: the projection can over-count vs the simulated combat (commit-the-line note in
// TurnSolver.cpp), and an over-count here would discard the original for a phantom lethal. The
// same simulation runs in both worlds (executor's EnforceLegendRule and the rollout's resolve the
// same provider), so the choice is lockstep by construction. Phase gate: only while combat is
// still ahead (GameState::phase, maintained by the executor's MainPhase and the rollout's
// SimulateCombat/turn-boundary writes) -- a post-combat keep-the-copy is a pure loss (the token
// exiles before it can ever attack). Later plan casts (a pump after the trick) are not seen; the
// projection is a same-instant lower bound, so the miss direction is the safe one (keep original).
int MirrorwingProvider::LegendKeepIndex(const GameState& s, int controller,
                                        const std::vector<int>& duplicates) const
{
    const int oldest = duplicates.empty() ? -1 : duplicates.front();
    if (duplicates.size() != 2) { return oldest; }   // the Twinflame pattern is exactly one pair
    if (s.phase != Phase::PreCombatMain && s.phase != Phase::Combat) { return oldest; }
    const int newest = duplicates.back();
    const Permanent& po = s.battlefield[oldest];
    const Permanent& pn = s.battlefield[newest];
    if (!pn.exile_at_end)                              { return oldest; }  // not the temp-copy pattern
    if (CanAttackFull(po, s.battlefield, controller))  { return oldest; }  // original attacks fine
    if (!CanAttackFull(pn, s.battlefield, controller)) { return oldest; }  // copy cannot attack either
    const int opp = 1 - controller;

    auto lethal_keeping = [&](int keep_idx) {
        GameState arm = s;
        // Apply this arm's legend-rule outcome exactly as EnforceLegendRule will (loser to the
        // graveyard), then simulate the attack. No duplicates remain, so no recursion back here.
        const int lose_idx = (keep_idx == oldest) ? newest : oldest;
        arm.players[arm.battlefield[lose_idx].owner_index].graveyard.push_back(
            arm.battlefield[lose_idx].card);
        arm.battlefield.erase(arm.battlefield.begin() + lose_idx);
        RolloutSimulateCombat(arm);
        return arm.players[opp].life <= 0;
    };
    if (!lethal_keeping(newest)) { return oldest; }   // not lethal even with the copy
    if (lethal_keeping(oldest))  { return oldest; }   // lethal anyway -> keep the original
    return newest;
}

// ---- MirrorwingProvider go-off policy (user, Stage-6 round 3) ---------------
//
// "Usually you just use it [Twinflame] to go off by casting it first so there are more critters
// (order matters a bit here) and then cast all of your other spells to pump for lethal. It's
// honestly kind of bad without Mirrorwing or Zada on board as all other creatures are small --
// without them you would only cast it when you are 1 damage short. Pretty rare."

// MTG_MW_ORDERED -- the USER-reviewed FULL cast order for the Mirrorwing decks (review held
// 2026-08-18; the ruling is recorded verbatim in docs/design/cast-order-rankings.md). One lever
// carries the whole ruling -- ranks, hoist membership, opaque rank-sort, the Gold Rush funding
// ladder, and the cantrip-promotion supersession -- so an A/B arm cannot get half of it. Default
// OFF -> byte-identical pre-review behaviour; =1 to measure. On adoption this becomes a
// default-on read with an off switch, as the other adopted per-deck rules are.
static bool MirrorwingOrderedEnabled()
{
    // ADOPTED default-on (USER, 2026-08-18): the reviewed full Mirrorwing order. Evidence: train
    // smoke d0 -0.0120 / d3 -0.0067, regression d0 -0.0110 / d3 -0.0100; held-out 11 green /
    // 1 flat / 0 red, per-game 96 faster / 8 slower, both searched slower games budget-churn.
    // =0 reverts to the pre-review order.
    static const bool on = EnvOn("MTG_MW_ORDERED", true);   // DEFAULT ON; =0 disables
    return on;
}

// Bodies a card supplies to the Mirrorwing/Zada fan-out. A "body" is anything that puts a creature
// on the board for a magnet to copy onto -- which is NOT the same as being a creature.
//
// USER 2026-08-27, on the four new Goblin-Instigator-slot candidates: "let's try to update all of
// the heuristics to take the new cards into account. They should probably all go into the creature
// bucket, counting as 2 critters." Property-keyed rather than name-keyed so it stays true of any
// future card with the same shape:
//
//   Goblin Instigator    creature(1) + ETB token(1)                     = 2
//   Nest Invader         creature(1) + ETB Spawn(1)                     = 2
//   Undercellar Myconid  creature(1) + ETB Saproling(1)                 = 2
//   Young Pyromancer     creature(1) + a token per instant/sorcery(1)   = 2
//   Frontline Heroism    NOT a creature(0) + ETB Soldier(1) + per-cast Soldier(1) = 2
//
// The ongoing term is deliberately +1, not a count: an engine that makes one body per qualifying
// cast is worth strictly more than a one-shot two-for-one, so 2 is a FLOOR that merely ties
// Instigator rather than an estimate of its real output.
//
// Byte-identical for every shipped decklist: Goblin Instigator is the only card in any of them that
// scores above 1, and it scored 2 under the previous hardcoded constant too.
static int MwBodyCount(const CardDefinition& d)
{
    const int self    = d.card.IsCreature() ? 1 : 0;
    const int etb     = d.params.etb_self_creates_tokens;
    const int ongoing = (d.params.cast_trigger_instant_sorcery_tokens > 0
                      || d.params.frontline_copy_tokens > 0) ? 1 : 0;
    return self + etb + ongoing;
}

bool MirrorwingProvider::CastEnablerFirst(const GameState&, const std::string& name) const
{
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    if (!d) { return false; }
    if (MirrorwingOrderedEnabled())
    {
        // Reviewed body camp (USER 2026-08-18): "Magnets, creatures, Libation and Twinflame are
        // still before draw" -- each body cast before a magnet fan-out is one more COPY of the
        // mass-draw itself. Gold Rush LEAVES the hoist: unconditionally-early was the pre-review
        // shape; its position is now the funding ladder (CastOrderFallbackRanks).
        return d->card.IsCreature()                 // bodies first: more copies for the fan-outs
            || d->params.token_copy_of_target       // Twinflame: double the board before the pumps
            || d->params.trick_token_power > 0      // Luxurious Libation: its token is a body too
            // ...and a NONCREATURE body-maker (Frontline Heroism) belongs in the same camp: its
            // Soldier is a body, and every trick cast after it makes another one, so casting it
            // late wastes exactly the tricks it exists to copy.
            || MwBodyCount(*d) > 0;
    }
    // ALL creatures precede the doubler (user round 3: "you might cast creatures before
    // Twinflame to get more [critters]"), the doubler precedes the pump tricks, and Gold Rush
    // precedes the DRAW tricks (user: "essentially a ritual... sometimes you need to cast it
    // before other spells to keep the chain going" -- its Treasures are spendable at the next
    // draw-breakpoint re-solve, so casting it early funds the continuation; its own pump counts
    // its own Treasures, so early costs nothing).
    return d->card.IsCreature()                     // bodies first: more copies for the fan-outs
        || d->params.token_copy_of_target           // Twinflame: double the board before the pumps
        || d->params.creates_treasures > 0;         // Gold Rush: the ritual funds the chain
}

int MirrorwingProvider::CastOrderRank(const GameState& s, const CardDefinition& def) const
{
    if (MirrorwingOrderedEnabled())
    {
        // The FULL reviewed order (USER 2026-08-18). No card in this deck carries more than one
        // of these params except Fists of Flame (cast_draw AND pump_per_cards_drawn), so the
        // payoff check precedes the draw check. Creatures fall through to Generic's 10.
        if (def.params.copies_solo_targeted_spells)       { return 5;  }  // magnets: first body
        // Frontline Heroism: AFTER the magnets, BEFORE every trick (USER 2026-08-27). It makes a
        // Soldier -- and a copy -- for each qualifying spell cast AFTER it resolves, so any trick
        // cast ahead of it is a Soldier and a copy thrown away. That puts it below Libation (11)
        // and Twinflame (12), which are themselves tricks, and above the magnets (5), which must
        // still land first because they are what the copies fan onto.
        if (!def.card.IsCreature() && MwBodyCount(def) > 0) { return 6; }
        if (def.params.trick_token_power > 0)             { return 11; }  // Libation: body-maker, before the doubler
        if (def.params.token_copy_of_target)              { return 12; }  // Twinflame: doubler, after every other body
        if (def.params.pump_per_cards_drawn_power > 0)    { return 16; }  // Fists: the payoff, after every draw
        if (def.params.cast_draw > 0)                     { return 14; }  // draws: Anger / Expedite / Impolite / Scale
        if (def.params.creates_treasures > 0)             { return 15; }  // Gold Rush: preferred slot = after the draws
        if (def.params.pump_per_life_gained_power > 0)    { return 18; }  // Draught: X counts the turn's PRIOR gains
        return GenericProvider::CastOrderRank(s, def);
    }
    if (def.params.copies_solo_targeted_spells) { return 5; }    // magnet first of the bodies
    if (def.params.token_copy_of_target)        { return 12; }   // after every creature (10), before tricks (20)
    if (def.params.creates_treasures > 0)       { return 15; }   // Gold Rush: after the doubler, before draw tricks
    return GenericProvider::CastOrderRank(s, def);
}

bool MirrorwingProvider::OrderOpaqueCastsByRank() const
{
    return MirrorwingOrderedEnabled();
}

const char* MirrorwingProvider::CastOrderTierName(int rank) const
{
    if (!MirrorwingOrderedEnabled()) { return nullptr; }
    switch (rank)
    {
        case 5:  return "MAGNET: the copy target must exist before any trick";
        case 11: return "BODY-MAKER (Libation): its token is one more copy of everything after";
        case 12: return "DOUBLER (Twinflame): after every other body, before the pumps";
        case 14: return "DRAW: after the bodies (each body is one more copy of the mass-draw)";
        case 15: return "GOLD RUSH: after the draws (post-draw bodies widen the fan-out); funding ladder walks it earlier";
        case 16: return "PAYOFF (Fists): after every draw it counts";
        case 18: return "DRAUGHT: last -- X counts the turn's prior lifegain";
        default: return nullptr;
    }
}

std::vector<int> MirrorwingProvider::CastOrderFallbackRanks(const GameState&,
                                                            const CardDefinition& def) const
{
    // Gold Rush's funding ladder (USER 2026-08-18): "Definitely it should go after the Magnets at
    // the earliest, but preferably you would be able to wait until after Twinflame or after
    // draw" -- and "might need more searching unless we have a lot of mana up", i.e. when the
    // late slot pays there is nothing to search. Preferred 15 (after the draws: post-draw bodies
    // widen its fan-out), then 13 (after Twinflame, before the draws), then 6 (after the
    // magnets) -- each earlier rung only while FirstUnpayablePos says the line cannot be paid.
    if (MirrorwingOrderedEnabled() && def.params.creates_treasures > 0) { return { 15, 13, 6 }; }
    return {};
}

// Mirrorwing is the deck the cantrip promotion measurably SUITS -- every trick in it cantrips, so
// casting the 1-mana ones first is information before the fan-out is committed. Measured on both
// seed sets (with MTG_ORDER_OPAQUE + MTG_ORDER_RANGE, which the promotion needs to have a domain
// and to stay payable): regression d0 -0.0150, d3 -0.0100/-0.0100, d5 -0.0100, = -20.0 game-turns;
// smoke d0 -0.0160. The mv-1 bar matters here specifically: at mv 2 the promotion also catches
// Fists of Flame, the deck's PAYOFF, and mirrorwing d0 goes from -0.0160 to +0.0020.
// DEFAULT OFF -- adoption is the USER's call (order/range/main-phase is a reviewed per-deck
// judgement). MTG_MW_CANTRIP_ORDER=1 to measure; on adoption this becomes a default-on read with
// an off switch, as the other adopted per-deck rules are.
bool MirrorwingProvider::PromoteCantripsInCastOrder() const
{
    // SUPERSEDED by MTG_MW_ORDERED (USER review 2026-08-18): the reviewed order puts the bodies
    // BEFORE the draws (each body is one more copy of the mass-draw), so the draws-first
    // promotion this hook awarded is the wrong shape for this deck and must not combine with it.
    if (MirrorwingOrderedEnabled()) { return false; }
    static const bool on = EnvOn("MTG_MW_CANTRIP_ORDER");
    return on;
}

bool MirrorwingProvider::StriveCountMaxOnly(const GameState&, const CardDefinition& def) const
{
    return def.params.token_copy_of_target;   // Twinflame: K = 0 or max -- a lethal burst, not a dial
}

// ---- MirrorwingProvider::XCandidates ----------------------------------------
//
// Luxurious Libation ({X}{G}: +X/+X per resolved copy, then a 1/1 Citizen per copy) is TWO cards
// at once (user, 2026-08-17): "X=0 is a 'generate more creatures' play and is cast early,
// whereas X=maximum board power is cast late as a final pump to close the game".
//
// The generic prune proposes the single value {max_affordable}, and `max_affordable` reaches
// this hook already computed off AvailableManaPool -- which counts untapped MANA DORKS and rocks
// as well as lands (ManaPayment.cpp). So the ONLY X>0 the search ever saw was "tap every land
// AND every Elvish Mystic and Ignoble Hierarch", which is close to always wrong for this deck: a
// dork tapped for mana cannot attack, and the +X/+X the fan-out puts on it is wasted. Measured
// over 300 games, the search took X=0 in 172 of 172 casts -- the closing role never fired once,
// not because the search dislikes it but because the value that expresses it was never offered.
//
// So propose exactly the two values the user actually plays: X=0 (added unconditionally by the
// trick enumerator itself) and the X that MAXIMISES ATTACKING BOARD POWER. Dorks are spent only
// where spending them raises that total -- "generally not tap your dorks for mana unless that
// actually increases the total power. Most of the time, this would mean tapping no dorks, but
// there are boards where tapping one is correct." A SICK dork is free mana (it was never going
// to attack); an attack-capable one costs its power, so those are spent cheapest-first and only
// while the trade pays. Intermediate X values stay unsearched by explicit sign-off: "I'm okay
// with missing weird clairvoyance cases where X is set somewhere in between."
//
// This is a NARROWING heuristic and therefore lives in the provider, never in the enumerator
// (the core invariant). MTG_UNPRUNED restores the full 1..max range for the audit A/B.
std::vector<int> MirrorwingProvider::XCandidates(const GameState& s, const CardDefinition& def,
                                                 int max_affordable) const
{
    // Only the X-scaled pump trick takes this rule; every other {X} card keeps the generic prune
    // (and the unpruned / human-play routes must see the whole range).
    if (def.params.pump_per_x_power <= 0 || !def.params.solo_target_trick
        || DecisionUnpruned(UnprunedGate::XSpell) || HumanPlayActive())
    { return GenericProvider::XCandidates(s, def, max_affordable); }

    if (max_affordable <= 0) { return {}; }   // X=0 only; the enumerator supplies it itself

    const int me = s.active_player_index;
    const CardDatabase& db = CardDatabase::Instance();

    bool magnet = false;
    int  atk_count = 0, atk_power = 0;
    std::vector<int> atk_dork_power;   // attack-capable dorks: tapping one COSTS its attack
    for (const Permanent& p : s.battlefield)
    {
        if (p.controller_index != me) { continue; }
        const CardDefinition* d = db.LookupCached(p.card);
        if (d == nullptr) { continue; }
        if (d->params.copies_solo_targeted_spells) { magnet = true; }
        if (!p.card.IsCreature()) { continue; }
        const bool can_attack = CanAttackFull(p, s.battlefield, me);
        if (can_attack) { ++atk_count; atk_power += p.EffectivePower(); }
        if (d->tmpl == CardTemplate::ManaDork && CanTapNow(p, s.battlefield) && can_attack)
        { atk_dork_power.push_back(p.EffectivePower()); }
    }
    // Spend the cheapest attackers first, so j dorks tapped always costs the least power it can.
    std::sort(atk_dork_power.begin(), atk_dork_power.end());

    // Every creature the fan-out reaches gets +X; without a magnet the trick pumps its ONE
    // target, so only a single attacker scales with X.
    const int n_atk_dorks = static_cast<int>(atk_dork_power.size());
    int best_x = 0, best_total = -1, lost_power = 0;
    for (int j = 0; j <= n_atk_dorks; ++j)
    {
        // j = attack-capable dorks tapped. max_affordable already counts ALL of them, so
        // declining to tap (n_atk_dorks - j) of them removes exactly that much X.
        const int x = max_affordable - (n_atk_dorks - j);
        if (j > 0) { lost_power += atk_dork_power[static_cast<std::size_t>(j - 1)]; }
        if (x <= 0) { continue; }
        const int attackers = atk_count - j;
        if (attackers <= 0) { continue; }
        const int pumped = magnet ? attackers : 1;
        const int total  = (atk_power - lost_power) + pumped * x;
        // Ties go to the SMALLER X: same damage for less mana leaves the rest of the turn open.
        if (total > best_total) { best_total = total; best_x = x; }
    }
    if (best_x <= 0) { return {}; }
    return { best_x };
}

// ---- MirrorwingProvider cleanup discard -------------------------------------
//
// USER-AUTHORED keep policy (Stage 6 review 2026-08-11, refined 2026-08-13): bucket the hand
// into Enabler (only 1 -- none with a magnet on board), Other Creatures (enough for 4 weighted
// bodies counting the board; Instigator weighs 2; dorks count here AND as mana), Mana (enough
// to cast the kept enabler with at least 2 red and 2 green across board + hand; when next turn
// is the cast turn the kept drop must enter untapped), and Pump/Draw spells (keep priority:
// Gold Rush, ONE Twinflame and Fists of Flame, then Ancestral Anger > Expedite > Scale the
// Heights, preferring DIFFERENT pump spells over copies with spare space). Every bucket nets
// its board coverage first ("you don't need to fill up the hand if there is already enough on
// board"). This is the FULL decision -- no search: the rule names the shed order, omission =
// keep, the shared ranking keeps required-piece protection as the safety net, and both the
// rollout cleanup and the executor consume rank 0 (width stays 1, no discard search node).
// The deck rarely discards (0.13 mean label regret in the discard-analysis stage), so this rule
// is mostly about the post-Fists mass-draw turns where the hand blows past seven.

const std::vector<std::string>*
MirrorwingProvider::InterchangeableRequiredGroup(const std::string& name) const
{
    static const std::vector<std::string> kMagnets = { "Zada, Hedron Grinder", "Mirrorwing Dragon" };
    if (name == "Zada, Hedron Grinder" || name == "Mirrorwing Dragon") { return &kMagnets; }
    return nullptr;
}

std::vector<int> MirrorwingProvider::CleanupDiscardCandidates(
    const GameState& s, const std::vector<std::string>* required_pieces) const
{
    static const bool s_bucket = EnvOn("MTG_MW_BUCKET_DISCARD", true);
    if (!s_bucket) { return GenericProvider::CleanupDiscardCandidates(s, required_pieces); }

    const Player& ap = s.players[s.active_player_index];
    const int n = static_cast<int>(ap.hand.size());
    auto def_of = [](const Card& c) { return CardDatabase::Instance().LookupCached(c); };
    auto is_magnet = [&](const Card& c)
    { const CardDefinition* d = def_of(c); return d && d->params.copies_solo_targeted_spells; };
    auto is_dork = [&](const Card& c)
    { const CardDefinition* d = def_of(c); return d && d->tmpl == CardTemplate::ManaDork; };
    auto is_land = [&](const Card& c)
    { const CardDefinition* d = def_of(c); return d ? d->card.IsLand() : c.IsLand(); };
    auto produces = [&](const Card& c, Color col)
    {
        const CardDefinition* d = def_of(c);
        if (!d) { return false; }
        for (Color p : d->params.produces) { if (p == col) { return true; } }
        return false;
    };

    // Board census the buckets key on. Every bucket nets its board coverage out first (user
    // 2026-08-13: "you don't need to fill up the hand if there is already enough on board").
    bool magnet_board = false, board_sick_dork = false;
    int  board_sources = 0, board_red = 0, board_green = 0, board_bodies = 0;
    for (const Permanent& p : s.battlefield)
    {
        if (p.controller_index != s.active_player_index) { continue; }
        const CardDefinition* d = def_of(p.card);
        const bool creature = d ? d->card.IsCreature() : p.card.IsCreature();
        if (creature) { ++board_bodies; }
        if (d && d->params.copies_solo_targeted_spells) { magnet_board = true; }
        if ((d && d->card.IsLand()) || is_dork(p.card))
        {
            ++board_sources;
            if (is_dork(p.card) && p.entered_this_turn) { board_sick_dork = true; }
            if (produces(p.card, Color::Red))   { ++board_red; }
            if (produces(p.card, Color::Green)) { ++board_green; }
        }
    }

    // Hand census.
    std::vector<int> magnets, mana, bodies;
    for (int i = 0; i < n; ++i)
    {
        const Card& c = ap.hand[i];
        if (c.m_is_staged) { continue; }
        if (is_magnet(c)) { magnets.push_back(i); continue; }
        if (is_land(c) || is_dork(c)) { mana.push_back(i); }
        const CardDefinition* d = def_of(c);
        // A "body" is anything that puts a creature on the board for the fan-out to copy onto.
        // That is normally a creature, but a NONCREATURE permanent whose ETB makes a token
        // (Frontline Heroism's 1/1 Soldier) supplies one just the same. Counting it here is what
        // keeps it NAMED in the shed list below -- an unlisted card falls through to the shared
        // highest-MV fallback, which is the gi295 defect that sheds the magnet. Byte-identical for
        // the shipped decklist: Goblin Instigator is the only ETB-token maker in it and it is
        // already a creature.
        if (d && MwBodyCount(*d) > 0) { bodies.push_back(i); }
    }
    // The kept enabler: none needed with a magnet already on board. Among hand magnets the pick
    // is NOT a fixed cheapest-first (user, Stage-6 round 2): estimate each magnet's EARLIEST CAST
    // TURN from board sources + the hand's mana (one land drop per turn), and keep Mirrorwing
    // when it is castable the SAME turn as Zada (more power, better Twinflame interactions) --
    // Zada only when it is castable strictly earlier or Mirrorwing's cost (incl. the RR pips) is
    // not coverable at all. The kill usually comes the turn AFTER the enabler lands, so
    // equal-turn castability is the decision point.
    int hand_mana_n = 0, hand_red = 0;
    for (int i : mana)
    {
        ++hand_mana_n;
        if (produces(ap.hand[i], Color::Red)) { ++hand_red; }
    }
    auto cast_turns = [&](const Card& c) -> int
    {
        const int mv      = c.m_mana_cost.ManaValue();
        const int red_req = c.m_mana_cost.red;
        const int deficit = std::max(0, mv - board_sources);
        if (deficit > hand_mana_n)              { return 1000; }   // mana not coverable
        if (board_red + hand_red < red_req)     { return 1000; }   // red pips not coverable
        return deficit;
    };
    std::stable_sort(magnets.begin(), magnets.end(), [&](int a, int b)
    {
        const int ta = cast_turns(ap.hand[a]), tb = cast_turns(ap.hand[b]);
        if (ta != tb) { return ta < tb; }                                   // castable sooner wins
        return ap.hand[a].m_mana_cost.ManaValue() > ap.hand[b].m_mana_cost.ManaValue();   // tie: bigger body
    });
    const int kept_magnet = (!magnet_board && !magnets.empty()) ? magnets.front() : -1;
    const int enabler_mv  = kept_magnet >= 0
        ? ap.hand[kept_magnet].m_mana_cost.ManaValue()
        : (magnet_board ? 2 : 4);   // magnet down -> trick mana; none anywhere -> assume Zada

    std::vector<int> shed;
    // S1 -- excess mana beyond what the kept enabler needs (user 2026-08-13). The KEPT sources
    // must (a) cover the enabler's remaining MV against board sources, (b) close the colour
    // floor -- at least TWO red and TWO green across board + kept hand (both magnets are {..}{R}
    // and the tricks are red; Gold Rush / Scale the Heights carry the green pips), and (c) when
    // the enabler is castable NEXT turn (one more drop), that drop must be a land that ENTERS
    // UNTAPPED -- a Karoo dropped on the go-off turn contributes nothing that turn. Dorks are
    // kept preferentially (they are fan-out bodies too, the next bucket). Greedy keep passes;
    // everything unkept is the shed, deadest first (tapped-entering lands, untapped lands, dorks).
    const int  need          = std::max(0, enabler_mv - board_sources);
    // "Next turn is the turn" (user): at need <= 1 the enabler lands next turn (need 1: after
    // the drop; need 0: castable already, next turn is the go-off), so the kept drop must enter
    // untapped. This hoard is safe ONLY because S5 below names the full hand -- pre-S5, on an
    // all-keeps hand, it emptied the named list and the MV fallback shed the only magnet
    // (gi295 s7007: win -> loss, persistent at UNLIMITED budget).
    const bool the_turn_next = need <= 1;
    auto untapped_land = [&](const Card& c)
    {
        const CardDefinition* d = def_of(c);
        return d && d->card.IsLand() && !d->params.enters_tapped;
    };
    int red_deficit   = std::max(0, 2 - board_red);
    int green_deficit = std::max(0, 2 - board_green);
    std::vector<char> kept(mana.size(), 0);
    std::vector<int>  mana_pick_order;   // kept mana, most-important-first (colour closers lead)
    int kept_n = 0;
    // Best unkept source under a preference score; -1 when none qualifies.
    auto pick_best = [&](auto&& qualifies, auto&& score) -> int
    {
        int best = -1, best_score = -1;
        for (std::size_t k = 0; k < mana.size(); ++k)
        {
            if (kept[k]) { continue; }
            const Card& c = ap.hand[static_cast<std::size_t>(mana[k])];
            if (!qualifies(c)) { continue; }
            const int sc = score(c);
            if (sc > best_score) { best_score = sc; best = static_cast<int>(k); }
        }
        return best;
    };
    auto keep_one = [&](auto&& qualifies) -> bool
    {
        const int k = pick_best(qualifies, [&](const Card& c)
        {
            int sc = 0;                                       // prefer: dual coverage, dork,
            if (produces(c, Color::Red) && red_deficit > 0)     { sc += 2; }   // untapped drop
            if (produces(c, Color::Green) && green_deficit > 0) { sc += 2; }
            if (is_dork(c))                        { sc += 1; }
            if (the_turn_next && untapped_land(c)) { sc += 1; }
            return sc;
        });
        if (k < 0) { return false; }
        const Card& c = ap.hand[static_cast<std::size_t>(mana[static_cast<std::size_t>(k)])];
        kept[static_cast<std::size_t>(k)] = 1; ++kept_n;
        mana_pick_order.push_back(mana[static_cast<std::size_t>(k)]);   // most important first
        if (produces(c, Color::Red))   { red_deficit   = std::max(0, red_deficit - 1); }
        if (produces(c, Color::Green)) { green_deficit = std::max(0, green_deficit - 1); }
        return true;
    };
    while (red_deficit > 0   && keep_one([&](const Card& c) { return produces(c, Color::Red); }))   {}
    while (green_deficit > 0 && keep_one([&](const Card& c) { return produces(c, Color::Green); })) {}
    while (kept_n < need     && keep_one([&](const Card&)   { return true; }))                      {}
    // Hedge land (user 2026-08-13): "you may keep an extra land even if you have enough mana in
    // theory, due to summoning sickness or lands that come into play tapped." Theory counts a
    // kept hand DORK, a tapped-entering kept land, and a summoning-sick board dork as sources,
    // but each lags a turn in practice -- when the keep leans on any of them, keep one more
    // LAND (untapped-entering preferred, via keep_one's score).
    {
        bool laggy = board_sick_dork;
        for (std::size_t k = 0; k < mana.size() && !laggy; ++k)
        {
            if (!kept[k]) { continue; }
            const Card& c = ap.hand[static_cast<std::size_t>(mana[k])];
            if (is_dork(c) || (!untapped_land(c) && !is_dork(c))) { laggy = true; }
        }
        if (laggy)
        { keep_one([&](const Card& c) { return !is_dork(c); }); }
    }
    // Untapped-drop guard: the colour/count passes may have kept only tapped-entering Karoos;
    // if next turn is the cast turn and no kept LAND enters untapped, keep one that does.
    if (the_turn_next)
    {
        bool have_untapped = false;
        for (std::size_t k = 0; k < mana.size(); ++k)
        { if (kept[k] && untapped_land(ap.hand[static_cast<std::size_t>(mana[k])])) { have_untapped = true; break; } }
        if (!have_untapped)
        {
            const int k = pick_best([&](const Card& c) { return untapped_land(c); },
                                    [&](const Card& c) { return produces(c, Color::Red) ? 1 : 0; });
            if (k >= 0)
            {
                kept[static_cast<std::size_t>(k)] = 1; ++kept_n;
                mana_pick_order.push_back(mana[static_cast<std::size_t>(k)]);
            }
        }
    }
    // ---- The ORDERED shed list: the user's full doctrine (2026-08-13), least wanted first. ----
    // "Duplicate enablers, unnecessary creatures or unnecessary lands should be cut first. Then,
    // if needed you can cut spells down to 2. In the worst case where you need everything, you
    // could go down to 1 enabler 2 pump 2 creatures and 2 land" (a kept dork counts as BOTH mana
    // and creature "to make this fit"), "if there is extra space, we generally want more variety
    // of spells", and Gold Rush outranks any other pump with ALL its copies ("it gives mana and
    // pumps a lot. It's very difficult to lose when you get an enabler on board with 2 Gold
    // Rush"). The list names EVERY card (the gi295 lesson: an under-covering list hands the
    // decision to the shared ranking's highest-MV fallback, which sheds the magnet), so the
    // shared ranking only ever contributes the required-piece protection net.
    auto mana_shed_tier = [&](const Card& c)
    { return is_dork(c) ? 2 : (untapped_land(c) ? 1 : 0); };
    {
        std::vector<char> listed(static_cast<std::size_t>(n), 0);
        auto put = [&](int i)
        { if (i >= 0 && i < n && !listed[static_cast<std::size_t>(i)]
              && !ap.hand[i].m_is_staged) { listed[static_cast<std::size_t>(i)] = 1; shed.push_back(i); } };

        // 1. Dead cards: magnets beyond the kept one, then copies beyond the first of the
        //    "one is enough" spells. Twinflame and Luxurious Libation are both bought for the
        //    EXTRA BODIES a magnet fan-out makes (hasted token copies / a 1/1 Citizen per
        //    instance), and a second copy adds more of what the first already supplied rather
        //    than a new line -- user, 2026-08-18: Libation is "mainly good for extra creatures,
        //    but you don't really need multiple", i.e. rank it exactly as Twinflame is ranked.
        for (int i : magnets) { if (i != kept_magnet) { put(i); } }
        {
            static const char* kOneIsEnough[] = { "Twinflame", "Luxurious Libation" };
            for (const char* name : kOneIsEnough)
            {
                bool seen = false;
                for (int i = 0; i < n; ++i)
                {
                    if (ap.hand[i].m_is_staged || ap.hand[i].m_name != name) { continue; }
                    if (seen) { put(i); } else { seen = true; }
                }
            }
        }

        // 2. Unnecessary lands/dorks -- mana beyond the desired keep (need + colour floor +
        //    untapped drop), deadest first: tapped-entering lands, untapped lands, dorks.
        for (int tier = 0; tier <= 2; ++tier)
        {
            for (std::size_t k = 0; k < mana.size(); ++k)
            {
                if (kept[k]) { continue; }
                if (mana_shed_tier(ap.hand[static_cast<std::size_t>(mana[k])]) == tier) { put(mana[k]); }
            }
        }

        // 3. Unnecessary creatures -- beyond the 4-weighted-bodies target (Instigator weighs 2;
        //    board bodies count; a mana-kept dork counts toward the 4 and is never a spare).
        std::vector<char> s1_kept(static_cast<std::size_t>(n), 0);
        for (std::size_t k = 0; k < mana.size(); ++k)
        { if (kept[k]) { s1_kept[static_cast<std::size_t>(mana[k])] = 1; } }
        // "Instigator weighs 2" is really "this card brings a SECOND body". Keyed on the property
        // rather than the name so any ETB-token maker earns the same weight -- otherwise a screen
        // that swaps Instigator for another two-for-one (Nest Invader's Spawn, Undercellar
        // Myconid's Saproling, Frontline Heroism's Soldier) silently marks the newcomer down to 1
        // and measures the name, not the card. Byte-identical for the shipped decklist: Instigator
        // is a creature (1) whose ETB makes one token (+1) = 2, exactly as before.
        // An ONGOING body producer (Frontline Heroism: a token on every qualifying cast) is worth
        // more than its ETB token alone. Without the rider it weighs 1 against Goblin Instigator's
        // 2, which undercounts the card this slot is being asked to fill -- user, 2026-08-26:
        // "Frontline heroism is a 'creature' because it produces them." +1 is a deliberate FLOOR
        // (it ties Instigator); over a game with ~19 tricks it produces far more. Measurement
        // lever MTG_FRONTLINE_BODY, default OFF until the A/B is accepted.
        auto body_weight = [&](const Card& c)
        { const CardDefinition* d = def_of(c); return d ? MwBodyCount(*d) : 1; };
        int secured = board_bodies + (kept_magnet >= 0 ? 1 : 0);
        std::vector<int> kept_bodies, spare_bodies;
        for (int i : bodies)
        {
            if (s1_kept[static_cast<std::size_t>(i)])
            { secured += body_weight(ap.hand[i]); kept_bodies.push_back(i); continue; }
            if (secured < 4)
            { secured += body_weight(ap.hand[i]); kept_bodies.push_back(i); continue; }
            spare_bodies.push_back(i);
        }
        std::stable_sort(spare_bodies.begin(), spare_bodies.end(), [&](int a, int b)
        { return is_dork(ap.hand[a]) > is_dork(ap.hand[b]); });
        for (int i : spare_bodies) { put(i); }

        // 4. Spells down to the 2-pump floor, worst first. Keep priority (best-first): every
        //    Gold Rush, the Twinflame, Fists' first copy, then VARIETY across the tail -- first
        //    copies of Anger > Expedite > Scale before second copies of anything but GR.
        std::vector<int> pumps;   // best-first
        {
            auto copies_of = [&](const char* name)
            {
                std::vector<int> v;
                for (int i = 0; i < n; ++i)
                { if (!ap.hand[i].m_is_staged && ap.hand[i].m_name == name) { v.push_back(i); } }
                return v;
            };
            const std::vector<int> grs   = copies_of("Gold Rush");
            const std::vector<int> tfs   = copies_of("Twinflame");
            const std::vector<int> fists = copies_of("Fists of Flame");
            const std::vector<int> anger = copies_of("Ancestral Anger");
            const std::vector<int> exped = copies_of("Expedite");
            const std::vector<int> scale = copies_of("Scale the Heights");
            // The 2026-08-17 trick suite. These MUST be named here: the list's contract is that
            // it covers EVERY card (the gi295 lesson recorded above), and an unnamed pump falls
            // through to the shared ranking's highest-MV fallback -- which sheds the MAGNET.
            // The ranking below is the USER's (2026-08-18), not a measurement -- it is authored
            // judgment about this deck, and it is the user's to make:
            //   * Fortifying Draught ranks SECOND, behind only Gold Rush -- "our second-best
            //     spell". Its extra copies stay in the variety round-robin and lead each round:
            //     life_gained_this_turn accumulates, so a second Draught pumps strictly harder
            //     than the first.
            //   * Luxurious Libation is ranked as Twinflame is: first copy high, copies beyond
            //     the first DEAD in step 1 above. Both are bought for extra bodies.
            //   * Impolite Entrance sits beside Expedite, its "pal" -- under this engine the two
            //     are parameter-identical (trample and sorcery-vs-instant both unmodelled), so
            //     no ordering between them is even observable.
            // Their order relative to Anger/Expedite/Scale never fires in a shipped decklist (no
            // deck holds both suites -- the swap trades one set for the other); it matters only
            // for a pool/union arm. Placement WITHIN the new set is very observable, though, and
            // that is what the user set here.
            //   * Oracle's Restoration sits "in the same area as Ancestral Anger" (user,
            //     2026-08-19) -- it is the Anger slot's candidate replacement: same {1}-cantrip
            //     role, and its per-copy life rider additionally ENABLES Fortifying Draught.
            const std::vector<int> libat = copies_of("Luxurious Libation");
            const std::vector<int> draug = copies_of("Fortifying Draught");
            const std::vector<int> entra = copies_of("Impolite Entrance");
            const std::vector<int> orest = copies_of("Oracle's Restoration");
            for (int i : grs) { pumps.push_back(i); }
            if (!draug.empty()) { pumps.push_back(draug[0]); }    // 2nd-best spell (user)
            if (!tfs.empty())   { pumps.push_back(tfs[0]); }      // extras are dead (step 1)
            if (!libat.empty()) { pumps.push_back(libat[0]); }    // ditto -- bodies, one is enough
            if (!fists.empty()) { pumps.push_back(fists[0]); }
            if (!anger.empty()) { pumps.push_back(anger[0]); }
            if (!orest.empty()) { pumps.push_back(orest[0]); }  // same area as Anger (user)
            if (!exped.empty()) { pumps.push_back(exped[0]); }
            if (!entra.empty()) { pumps.push_back(entra[0]); }    // beside its pal Expedite
            if (!scale.empty()) { pumps.push_back(scale[0]); }
            std::size_t rounds = std::max({ draug.size(), fists.size(), anger.size(), orest.size(),
                                            exped.size(), entra.size(), scale.size() });
            for (std::size_t r = 1; r < rounds; ++r)
            {
                if (r < draug.size()) { pumps.push_back(draug[r]); }
                if (r < fists.size()) { pumps.push_back(fists[r]); }
                if (r < anger.size()) { pumps.push_back(anger[r]); }
                if (r < orest.size()) { pumps.push_back(orest[r]); }
                if (r < exped.size()) { pumps.push_back(exped[r]); }
                if (r < entra.size()) { pumps.push_back(entra[r]); }
                if (r < scale.size()) { pumps.push_back(scale[r]); }
            }
        }
        for (std::size_t k = pumps.size(); k-- > 2; ) { put(pumps[k]); }

        // 5. Worst case ("you need everything"): trim the kept mana LANDS down to the floor
        //    (least important first -- reverse of the keep passes' pick order; dorks are
        //    creatures here) and the kept bodies down to 2. The floors trade (user 2026-08-13):
        //    with a kept dork the mana slot floors at 2 lands (dork = mana AND creature);
        //    dork-less it "may keep 3 land in the mana spot", with an Instigator preferred on
        //    the creature side ("a goblin... would give you 3 creatures" -- it weighs 2, plus
        //    the enabler).
        {
            std::vector<int> kept_lands;
            bool kept_dork = false;
            for (int i : mana_pick_order)
            {
                if (is_dork(ap.hand[i])) { kept_dork = true; }
                else                     { kept_lands.push_back(i); }
            }
            const std::size_t land_floor = kept_dork ? 2 : 3;
            for (std::size_t k = kept_lands.size(); k-- > land_floor; ) { put(kept_lands[k]); }
            // Same de-naming as body_weight above: the doctrine is "prefer the body that brings a
            // second body", not "prefer this one card". Byte-identical for the shipped decklist.
            auto keep_rank = [&](const Card& c)   // higher = kept longer
            {
                if (is_dork(c)) { return 2; }
                const CardDefinition* d = def_of(c);
                return (d && MwBodyCount(*d) >= 2) ? 1 : 0;
            };
            std::vector<int> kb = kept_bodies;    // keep-priority order, so the tail sheds first
            std::stable_sort(kb.begin(), kb.end(), [&](int a, int b)
            { return keep_rank(ap.hand[a]) > keep_rank(ap.hand[b]); });
            for (std::size_t k = kb.size(); k-- > 2; ) { put(kb[k]); }
        }

        // 6. The floor itself, least precious first, magnet ABSOLUTE last. Everything still
        //    unlisted (the 1/2/2/2 keep) is named so the list always covers the whole hand.
        for (int i : bodies)          { put(i); }
        for (std::size_t k = mana.size(); k-- > 0; ) { put(mana[k]); }
        for (std::size_t k = pumps.size(); k-- > 0; ) { put(pumps[k]); }
        if (kept_magnet >= 0) { put(kept_magnet); }
        for (int i : magnets) { put(i); }   // any magnet the group protection released
    }
    return CleanupDiscardRankingWithOrder(s, required_pieces, shed);
}

// Does this card reduce the cost of a whole SUBTYPE (Goblin Warchief, Dragonspeaker Shaman,
// Urza's Incubator, Ragemonger)? Shared by the two bucket-discard policies below, which both have a
// cost-reducer bucket -- the "sharing parts" the per-deck provider direction asks for
// (docs/design/provider-per-deck-direction.md), rather than one provider inheriting the other.
//
// The gate is the ENGINE's own (ManaPayment.cpp): a named subtype, a type chosen on resolution, or
// the coloured-pip twin. It is deliberately NOT `reduces_spell_subtype_amount > 0` -- that field
// DEFAULTS TO 1 on every card in the database, so reading it alone classifies the entire hand as
// cost reducers. DragonsProvider did exactly that from the day it shipped until 2026-08-30: seven
// of eight cards in a measured hand landed in the reducer bucket, which emptied the payoff and
// enabler buckets and left the "policy" shedding nonland cards in reverse hand order. Any new
// param-keyed classifier should check its field's DEFAULT before trusting a `> 0` test.
inline bool IsSubtypeCostReducer(const CardDefinition& d)
{
    return !d.params.reduces_spell_subtype.empty()
        || d.params.chooses_creature_type
        || !d.params.reduces_subtype_colored_subtype.empty();
}

// ---- MinotaurProvider::CleanupDiscardCandidates -----------------------------
//
// USER-AMENDED role-bucket policy (approved 2026-08-30; see
// docs/design/minotaur-discard-policy-proposal.md for the rationale, the deck's role table and the
// two amendments quoted below).
//
// WHY THIS DECK NEEDS ONE. Same shape as the Dragons case below: the shared fallback's tier B is
// descending mana value, and MTG_SHED_STATS over 200 games at the shipped d5/b40 says this rule
// runs 99 times in real play against 250,265 times inside the SEARCH (2,528x) -- with **100%** of
// the rollout sheds taken at fewer than four lands. That is exactly the state where max-MV is least
// defensible here: at two or three lands it pitches Kragma Warcaller and Sethron, the top of the
// curve the deck is climbing toward, and keeps a Gnarled Scarhide. Index 0 of this ranking decides
// every one of those sheds with no search above it.
//
// THE BUCKETS and their caps (a card a slot protects is not sheddable; only overflow is):
//   LANDS, enough to reach FIVE mana sources counting the board first -- "we do not ever need more
//     than 5" (user 2026-08-30). Sethron and Kragma at 5 are the top of the curve and Ragemonger
//     takes {B}{R} off every Minotaur below them, so a sixth source is pure overflow; a RESOLVED
//     reducer lowers the target to 4 for the same reason. KAROO CAVEAT: a Rakdos Carnarium counts as
//     a land only if there is another land to bounce -- with none it returns ITSELF and is a blank.
//     That was found for real (regression seed 1001 gi=27 kept two Karoos and no other land and
//     played zero lands in eight turns), and it is why a dead Karoo is the FIRST thing shed.
//   THREATS, 3 hard and a 4th soft -- "we should always keep at least 3-4 threats" (user 2026-08-30,
//     raising the 2 this policy originally proposed). Deck value order, best kept first: the
//     attack-trigger payoff (Kragma) > static lord (Rageblood) > token/haste engine (Sethron) > the
//     hand-size anthem (Neheb) > the devotion burn (Fanatic -- the deck's ONLY non-combat damage) >
//     bodies (biggest first, the cheapest body shed first among threats).
//   AETHER VIAL, 1. A second copy is close to dead once the first is online.
//   RAGEMONGER, 1 while none is resolved -- "usually a good idea to keep 1 of them" (user). It is
//     classified with the MANA, not as a late enabler: taking {B}{R} off every Minotaur is this
//     deck's answer to a mana problem, and a mana problem is the state every one of these sheds is
//     taken in. Surplus copies fall back into the body pool -- they are still 3-mana 2/2 Minotaurs
//     and the engine stacks the reductions.
//
// DISTANCE-TO-PLAYABLE. A threat of effective mana value 5+ ranks BELOW a cheap body while total
// reach (board sources + live lands in hand) is 3 or less -- unless a Ragemonger is already
// resolved, which takes {B}{R} off it and erases the distance. This is bounded and reach-conditional
// on purpose: it is not the blanket max-MV rule the evidence above rejects, it fires only where the
// 5-drop genuinely cannot be cast for two more turns.
//
// THE NEHEB INVERSION needs no special case, which is worth stating because it looks like it should.
// With Neheb resolved, emptying the hand to <=1 card gives every Minotaur +2/+0, so a shed is a
// BENEFIT and the right move is simply to shed the least valuable card. This list names EVERY card
// in the hand (the Mirrorwing gi295 lesson -- an under-covering list hands the rest of the decision
// back to the max-MV fallback, which on this deck is inverted), so index 0 IS the least valuable
// card at all times and that is already the answer. The other two state promotions in the proposal
// are NOT implemented and are not lost: Burning-Fist's "a dead card in hand is firebreathing
// ammunition" and Fanatic's "Boros Reckoner is worth 3 red devotion when the opponent is in reach"
// are both CAST decisions with a damage projection behind them, which belongs to the search, not to
// a cleanup ranking. (The devotion tie-break among bodies is kept, and it reproduces the proposal's
// worked example on this deck without needing the reach term.)
//
// Classification is by PARAMS, never card names, so a screening arm that swaps a card keeps the
// right bucket, and the return routes through CleanupDiscardRankingWithOrder so the staged-card and
// required-piece protections stay engine-enforced.
std::vector<int> MinotaurProvider::CleanupDiscardCandidates(
    const GameState& s, const std::vector<std::string>* required_pieces) const
{
    static const bool s_bucket = EnvOn("MTG_MINOTAUR_BUCKET_DISCARD", true);
    if (!s_bucket) { return GenericProvider::CleanupDiscardCandidates(s, required_pieces); }

    const Player& ap = s.players[s.active_player_index];
    const int n = static_cast<int>(ap.hand.size());
    if (n <= 0) { return GenericProvider::CleanupDiscardCandidates(s, required_pieces); }

    // Every characteristic read goes through the DEFINITION: a hand card is a name-only placeholder
    // (DeckLoader::MakePlaceholder), so its own type/cost/subtype masks are all empty.
    auto def_of   = [](const Card& c) { return CardDatabase::Instance().LookupCached(c); };
    auto mv_of    = [&](int i) { return CleanupDiscardManaValue(ap.hand[i]); };
    auto is_land  = [&](int i) { return CleanupDiscardIsLand(ap.hand[i]); };
    auto is_karoo = [&](int i)
    { const CardDefinition* d = def_of(ap.hand[i]); return d && d->params.etb_bounce_land; };
    auto is_vial  = [&](int i)
    { const CardDefinition* d = def_of(ap.hand[i]); return d && d->params.upkeep_adds_charge; };
    auto is_reducer = [&](int i)
    { const CardDefinition* d = def_of(ap.hand[i]); return d && IsSubtypeCostReducer(*d); };
    auto is_creature = [&](int i)
    { const CardDefinition* d = def_of(ap.hand[i]); return d && d->card.IsCreature(); };
    auto power_of = [&](int i)
    {
        const CardDefinition* d = def_of(ap.hand[i]);
        return (d && d->card.m_power.has_value()) ? d->card.m_power.value() : 0;
    };

    // ---- board census, netted before any quota --------------------------------------------------
    // board_lands is tracked SEPARATELY from board_sources on purpose. The land quota is about mana,
    // so a rock counts toward it; the Karoo test is about having a LAND to bounce, and a Sol Ring
    // cannot be returned to hand by a Rakdos Carnarium. Conflating the two would call a Karoo live
    // off a board of nothing but rocks.
    int board_lands = 0, board_sources = 0, board_reducers = 0, board_vials = 0, reducer_amount = 0;
    std::string reduced_tribe;
    for (const Permanent& p : s.battlefield)
    {
        if (p.controller_index != s.active_player_index) { continue; }
        const CardDefinition* d = CardDatabase::Instance().LookupCached(p.card);
        if (p.card.IsLand())                   { ++board_lands; ++board_sources; }
        else if (d && d->params.mana_rock)     { ++board_sources; }
        if (d == nullptr) { continue; }
        if (d->params.upkeep_adds_charge)      { ++board_vials; }
        // The reducer's own params supply the discount AND the subtype it applies to, so nothing
        // here hardcodes "Minotaur" -- a deck whose reducer names another tribe gets that tribe.
        // A reducer that picks its type on resolution (Urza's Incubator, chooses_creature_type)
        // names nothing here, so it deliberately counts for NEITHER the discount nor the reducer
        // census: we cannot price a discount whose subtype we do not know, and under-counting only
        // keeps the land target where it was. Inert on this decklist, which runs no such card.
        if (!IsSubtypeCostReducer(*d)) { continue; }
        int amt = 0;
        if (!d->params.reduces_subtype_colored_subtype.empty()
            && d->params.reduces_subtype_colored_cost.has_value())
        {
            amt = d->params.reduces_subtype_colored_cost.value().ManaValue();
            reduced_tribe = d->params.reduces_subtype_colored_subtype;
        }
        else if (!d->params.reduces_spell_subtype.empty())
        {
            amt = d->params.reduces_spell_subtype_amount;
            reduced_tribe = d->params.reduces_spell_subtype;
        }
        if (amt > 0) { ++board_reducers; reducer_amount = std::max(reducer_amount, amt); }
    }

    // The devotion payoff's colour, derived from whichever card carries it (Fanatic of Mogis) rather
    // than assumed red; used only as a tie-break between otherwise equal bodies.
    std::string devotion_color;
    for (int i = 0; i < n && devotion_color.empty(); ++i)
    {
        const CardDefinition* d = def_of(ap.hand[i]);
        if (d && !d->params.etb_damage_devotion_color.empty())
        { devotion_color = d->params.etb_damage_devotion_color; }
    }
    for (const Permanent& p : s.battlefield)
    {
        if (!devotion_color.empty()) { break; }
        if (p.controller_index != s.active_player_index) { continue; }
        const CardDefinition* d = CardDatabase::Instance().LookupCached(p.card);
        if (d && !d->params.etb_damage_devotion_color.empty())
        { devotion_color = d->params.etb_damage_devotion_color; }
    }
    // Devotion pips of one hand card, counting a hybrid pip for BOTH its colours (CR 202.2b) exactly
    // as DevotionTo does for the battlefield -- that is what makes Boros Reckoner's {R/W}{R/W}{R/W}
    // read as three red devotion.
    auto devotion_of = [&](int i)
    {
        if (devotion_color.empty()) { return 0; }
        const CardDefinition* d = def_of(ap.hand[i]);
        if (d == nullptr) { return 0; }
        const ManaCost& mc = d->card.m_mana_cost;
        const char c = devotion_color[0];
        int dev = 0;
        switch (c)
        {
            case 'W': dev += mc.white; break;
            case 'U': dev += mc.blue;  break;
            case 'B': dev += mc.black; break;
            case 'R': dev += mc.red;   break;
            case 'G': dev += mc.green; break;
            default: break;
        }
        for (int k = 0; k < mc.hybrid_count; ++k)
        {
            const Color second = static_cast<Color>(mc.hybrid_pair[k] & 0xF);
            if ((c == 'W' && second == Color::White) || (c == 'U' && second == Color::Blue)
                || (c == 'B' && second == Color::Black) || (c == 'R' && second == Color::Red)
                || (c == 'G' && second == Color::Green))
            { ++dev; }
        }
        return dev;
    };

    // ---- partition the hand ---------------------------------------------------------------------
    std::vector<int> lands, vials, reducers, threats, rest;
    for (int i = 0; i < n; ++i)
    {
        if (ap.hand[i].m_is_staged) { continue; }
        if (is_land(i))          { lands.push_back(i); }
        else if (is_vial(i))     { vials.push_back(i); }
        else if (is_reducer(i))  { reducers.push_back(i); }
        else if (is_creature(i)) { threats.push_back(i); }
        else                     { rest.push_back(i); }
    }

    // Lands: non-Karoo first, and a Karoo with nothing to bounce is not a land at all.
    const bool karoo_live = board_sources > 0
        || std::any_of(lands.begin(), lands.end(), [&](int i) { return !is_karoo(i); });
    std::stable_sort(lands.begin(), lands.end(), [&](int a, int b)
    { return static_cast<int>(is_karoo(a)) < static_cast<int>(is_karoo(b)); });

    // Reach for the distance rule: sources on board plus every land in hand that is really a land.
    int live_hand_lands = 0;
    for (int i : lands) { if (!is_karoo(i) || karoo_live) { ++live_hand_lands; } }
    const int reach = board_sources + live_hand_lands;

    // Effective cost: a resolved reducer takes its discount off every spell of ITS named subtype.
    auto eff_mv = [&](int i)
    {
        const CardDefinition* d = def_of(ap.hand[i]);
        const bool matches = board_reducers > 0 && d != nullptr && !reduced_tribe.empty()
                          && CardHasSubtype(d->card, reduced_tribe);
        return mv_of(i) - (matches ? reducer_amount : 0);
    };
    // Deck value order, best kept FIRST. Every test is a gated param, so nothing here is name-bound.
    auto threat_rank = [&](int i)
    {
        const CardDefinition* d = def_of(ap.hand[i]);
        if (d == nullptr) { return 5; }
        const CardParams& p = d->params;
        if (p.attack_pump_matching_power > 0)                              { return 0; }  // Kragma
        if (p.power_bonus > 0 && !p.subtypes_affected.empty())             { return 1; }  // Rageblood
        if (p.etb_other_subtype_creates_tokens || p.team_pump_grants_haste){ return 2; }  // Sethron
        if (p.hand_size_anthem_max >= 0)                                   { return 3; }  // Neheb
        if (!p.etb_damage_devotion_color.empty())                          { return 4; }  // Fanatic
        return 5;                                                                         // bodies
    };
    // A surplus reducer is a body: still a Minotaur with a stacking discount, so it competes for the
    // threat slots rather than being shed as a spare enabler.
    const int reducer_keep = board_reducers > 0 ? 0 : 1;
    for (std::size_t k = static_cast<std::size_t>(reducer_keep); k < reducers.size(); ++k)
    { threats.push_back(reducers[k]); }
    if (static_cast<int>(reducers.size()) > reducer_keep)
    { reducers.resize(static_cast<std::size_t>(reducer_keep)); }

    std::stable_sort(threats.begin(), threats.end(), [&](int a, int b)
    {
        // Out of reach sinks below everything castable (see DISTANCE-TO-PLAYABLE above).
        const int fa = (reach <= 3 && eff_mv(a) >= 5) ? 1 : 0;
        const int fb = (reach <= 3 && eff_mv(b) >= 5) ? 1 : 0;
        if (fa != fb) { return fa < fb; }
        const int ra = threat_rank(a), rb = threat_rank(b);
        if (ra != rb) { return ra < rb; }
        // Within the body tier: biggest first, then the one worth more devotion to the deck's burn,
        // then the more expensive (so the cheapest body sheds first among threats).
        if (power_of(a) != power_of(b))       { return power_of(a) > power_of(b); }
        if (devotion_of(a) != devotion_of(b)) { return devotion_of(a) > devotion_of(b); }
        return mv_of(a) > mv_of(b);
    });

    // ---- the KEEP PRIORITY, one card per slot ---------------------------------------------------
    // The quotas are not filled bucket-by-bucket, they are INTERLEAVED, because the fill order is
    // also the marginal-value order and those are different questions. "Five lands" and "three
    // threats" are both ceilings-and-floors on the same seven-card hand, so what the ranking has to
    // say is which land beats which threat. The FIRST land beats the first threat (a missed land
    // drop is the one thing this deck cannot recover); a Vial beats the second land (it deploys
    // without mana at all); and RAGEMONGER RANKS AS MANA, above the second threat -- "Ragemonger is
    // also quite helpful when dealing with mana problems, it's usually a good idea to keep 1 of
    // them" (user 2026-08-30). That last one supersedes this policy's original placement, which had
    // the reducer behind the whole 3-threat floor on the reasoning that a discount needs Minotaurs
    // to discount. The correction matters precisely because 100% of this deck's sheds happen with
    // fewer than four lands: taking {B}{R} off every Minotaur IS the answer to that state, so the
    // card that does it cannot be the last thing kept.
    //
    // Reversed, this same list is the order the quota-protected cards shed in when the whole hand is
    // covered and something must still go -- which is most of them. A bucket-at-a-time fill gets
    // that tail badly wrong: it protects a fifth land ahead of a castable body (seen for real in the
    // MTG_TRACE=discard probe, T5 lip=4 -> Boros Reckoner).
    const int kLandTarget  = 5;   // "we do not ever need more than 5 mana" (user)
    const int kThreatCap   = 4;   // "always keep at least 3-4 threats" (user): 3 hard, the 4th soft

    enum Slot { S_LAND, S_THREAT, S_VIAL, S_REDUCER };
    static const Slot kFill[] = {
        S_LAND, S_THREAT, S_VIAL, S_LAND, S_REDUCER, S_THREAT, S_LAND, S_THREAT, S_LAND, S_LAND,
        S_THREAT
    };

    // A resolved reducer takes {B}{R} off the whole curve, so four sources cast everything.
    int land_need    = std::max(0, kLandTarget - (board_reducers > 0 ? 1 : 0) - board_sources);
    int threat_need  = kThreatCap;
    int vial_need    = board_vials > 0 ? 0 : 1;
    int reducer_need = reducer_keep;

    std::vector<char> keep(static_cast<std::size_t>(n), 0);
    std::vector<int>  taken_order;                 // acquisition order; reversed, it is the keep tail
    std::size_t next_land = 0, next_threat = 0, next_vial = 0, next_reducer = 0;
    auto take = [&](int i)
    { keep[static_cast<std::size_t>(i)] = 1; taken_order.push_back(i); };

    for (Slot slot : kFill)
    {
        switch (slot)
        {
            case S_LAND:
                // A self-bouncing Karoo is not a land and never fills a land slot.
                while (land_need > 0 && next_land < lands.size())
                {
                    const int i = lands[next_land++];
                    if (is_karoo(i) && !karoo_live) { continue; }
                    take(i); --land_need; break;
                }
                break;
            case S_THREAT:
                if (threat_need > 0 && next_threat < threats.size())
                { take(threats[next_threat++]); --threat_need; }
                break;
            case S_VIAL:
                if (vial_need > 0 && next_vial < vials.size())
                { take(vials[next_vial++]); --vial_need; }
                break;
            case S_REDUCER:
                if (reducer_need > 0 && next_reducer < reducers.size())
                { take(reducers[next_reducer++]); --reducer_need; }
                break;
        }
    }

    // ---- shed order: overflow first, weakest first ----------------------------------------------
    std::vector<int> shed;
    std::vector<char> listed(static_cast<std::size_t>(n), 0);
    auto put = [&](int i)
    {
        if (i < 0 || i >= n || listed[static_cast<std::size_t>(i)]) { return; }
        if (ap.hand[i].m_is_staged) { return; }
        listed[static_cast<std::size_t>(i)] = 1; shed.push_back(i);
    };
    auto put_unkept = [&](int i) { if (!keep[static_cast<std::size_t>(i)]) { put(i); } };

    // S0 -- a Karoo with nothing to bounce: the single most worthless card in the hand.
    for (int i : lands) { if (is_karoo(i) && !karoo_live) { put_unkept(i); } }
    // S1 -- lands past the quota. The deck has no mana sink worth a sixth source.
    for (int i : lands) { put_unkept(i); }
    // S2 -- a second Vial, near-dead once the first is online.
    for (int i : vials) { put_unkept(i); }
    // S3 -- surplus threats, REVERSE of the keep order: the cheapest body first, and any 5-drop the
    //       distance rule pushed out of reach ahead of all of them.
    for (auto it = threats.rbegin(); it != threats.rend(); ++it) { put_unkept(*it); }
    // S4 -- a surplus reducer with one already resolved.
    for (auto it = reducers.rbegin(); it != reducers.rend(); ++it) { put_unkept(*it); }
    // S5 -- anything this policy does not recognise sheds LAST among the overflow: no opinion is a
    //       reason to protect a card, not to pitch it.
    for (int i : rest) { put_unkept(i); }
    // The KEEP TAIL, so the list covers the whole hand: the keep priority read backwards, i.e. the
    // 4th threat, then the 5th and 4th land, then the Ragemonger, then the 3rd threat... down to the
    // first land kept, which is the last card in the deck's hand this policy will ever give up.
    for (auto it = taken_order.rbegin(); it != taken_order.rend(); ++it) { put(*it); }

    return CleanupDiscardRankingWithOrder(s, required_pieces, shed);
}

// ---- DragonsProvider::CleanupDiscardCandidates ------------------------------
//
// USER-AUTHORED role-bucket policy (approved 2026-08-30; see
// docs/design/dragons-discard-policy-proposal.md for the rationale and the numbers).
//
// WHY THIS DECK NEEDS ONE AT ALL. The shared fallback's tier B is descending mana value, which
// here sheds Utvara Hellkite (8) -> Lathliss/Inferno (6) -> Glorybringer/Scourge (5): the payoffs
// in almost exactly descending order of importance, while keeping Lightning Bolt and Sol Ring.
//
// And it is NOT a rare decision, which is the thing a real-play census gets wrong (user,
// 2026-08-30). MTG_SHED_STATS over 200 games at the shipped d5/b40: 66 sheds in real play against
// 661,269 inside the SEARCH -- a 10,020x ratio -- and 99.6% of those are taken with fewer than four
// lands on the battlefield. Index 0 of this ranking decides every one of them with no search above
// it, so the rule shapes which plans the search believes are good even in games where nothing is
// ever discarded.
//
// THE BUCKETS (quota-first, net of board; only overflow beyond a quota is sheddable):
//   1 MANA. Lands to reach 4-5 on the battlefield, board counted first; then rocks, Sol Ring first
//     (it alone nets +2 off a {1}). A Karoo (etb_bounce_land) counts as a land ONLY if another land
//     exists to bounce -- with none it returns ITSELF and is a blank. That was found on Minotaur's
//     Rakdos Carnarium (seed 1001 gi=27: two Karoos, no other land, zero lands played in eight
//     turns) and this deck runs three Gruul Turf carrying the same param.
//   2 COST REDUCERS (reduces_spell_subtype_amount): 2 while none is on board, 1 once one is. They
//     take 2 off EVERY Dragon -- Utvara 8->6, Lathliss 6->4 -- so they are what makes the top of
//     the curve reachable.
//   3 PAYOFFS: keep at least 2, and deliberately FEWER than a threats-bucket instinct suggests
//     (user: "you might keep fewer dragons, since they can be expensive... 2 at minimum"). A hand
//     of five Dragons and two lands does nothing.
//   4 A 2-MANA ENABLER, only IF THERE IS SPACE (user) -- a SOFT quota filled from overflow, never
//     displacing the three above.
//
// Classification is by PARAMS and subtype, never card names, so a screening arm that swaps a card
// keeps the right bucket. The list names every hand card it has an opinion about; anything it does
// not name falls behind everything it did (omission == keep), and the return routes through
// CleanupDiscardRankingWithOrder so the staged-card and required-piece protections stay
// engine-enforced.
std::vector<int> DragonsProvider::CleanupDiscardCandidates(
    const GameState& s, const std::vector<std::string>* required_pieces) const
{
    static const bool s_bucket = EnvOn("MTG_DRAGONS_BUCKET_DISCARD", true);
    if (!s_bucket) { return GenericProvider::CleanupDiscardCandidates(s, required_pieces); }

    const Player& ap = s.players[s.active_player_index];
    const int n = static_cast<int>(ap.hand.size());
    if (n <= 0) { return GenericProvider::CleanupDiscardCandidates(s, required_pieces); }

    auto def_of  = [](const Card& c) { return CardDatabase::Instance().LookupCached(c); };
    auto mv_of   = [&](int i) { return CleanupDiscardManaValue(ap.hand[i]); };
    auto is_land = [&](int i) { return CleanupDiscardIsLand(ap.hand[i]); };
    auto is_karoo = [&](int i)
    { const CardDefinition* d = def_of(ap.hand[i]); return d && d->params.etb_bounce_land; };
    auto is_rock = [&](int i)
    { const CardDefinition* d = def_of(ap.hand[i]); return d && d->params.mana_rock; };
    auto is_reducer = [&](int i)
    { const CardDefinition* d = def_of(ap.hand[i]); return d && IsSubtypeCostReducer(*d); };
    // The payoff subtype is DERIVED, not hardcoded: it is whatever this deck's own cost reducers
    // discount. A reducer that picks its type at resolution (chooses_creature_type) names nothing,
    // so the literal-subtype reducer is the one that supplies it.
    std::string tribe;
    for (int i = 0; i < n && tribe.empty(); ++i)
    {
        const CardDefinition* d = def_of(ap.hand[i]);
        if (d && !d->params.reduces_spell_subtype.empty()) { tribe = d->params.reduces_spell_subtype; }
    }
    for (const Permanent& p : s.battlefield)
    {
        if (!tribe.empty()) { break; }
        if (p.controller_index != s.active_player_index) { continue; }
        const CardDefinition* d = CardDatabase::Instance().LookupCached(p.card);
        if (d && !d->params.reduces_spell_subtype.empty()) { tribe = d->params.reduces_spell_subtype; }
    }
    auto is_payoff = [&](int i)
    {
        const CardDefinition* d = def_of(ap.hand[i]);
        if (!d || !d->card.IsCreature()) { return false; }
        if (is_reducer(i)) { return false; }             // a reducer is mana, not a payoff
        // The subtype test MUST read the DEFINITION's card: a hand card is a name-only placeholder
        // (DeckLoader::MakePlaceholder) whose own m_subtypes is empty, so testing ap.hand[i]
        // directly returns false for every card -- which silently emptied this bucket whenever a
        // Dragonspeaker Shaman was visible to supply `tribe`, and left it correct when none was.
        return tribe.empty() ? (mv_of(i) >= 4) : CardHasSubtype(d->card, tribe);
    };

    // ---- board state, netted first -------------------------------------------------------------
    int board_lands = 0, board_rocks = 0, board_reducers = 0;
    for (const Permanent& p : s.battlefield)
    {
        if (p.controller_index != s.active_player_index) { continue; }
        const CardDefinition* d = CardDatabase::Instance().LookupCached(p.card);
        if (p.card.IsLand())                                   { ++board_lands; }
        else if (d && d->params.mana_rock)                     { ++board_rocks; }
        if (d && IsSubtypeCostReducer(*d))                     { ++board_reducers; }
    }

    // ---- partition the hand --------------------------------------------------------------------
    // Rocks get their OWN bucket: they are the second half of the mana quota (a Sol Ring is a land
    // that costs a card), not a two-drop trick. Folding them into `enablers` left the `rocks` vector
    // permanently empty, so the yield sort and the rock contribution to land_need below were both
    // dead code.
    std::vector<int> lands, rocks, reducers, payoffs, enablers, rest;
    for (int i = 0; i < n; ++i)
    {
        if (ap.hand[i].m_is_staged)  { continue; }
        if (is_land(i))              { lands.push_back(i); }
        else if (is_reducer(i))      { reducers.push_back(i); }
        else if (is_payoff(i))       { payoffs.push_back(i); }
        else if (is_rock(i))         { rocks.push_back(i); }
        else if (mv_of(i) <= 2)      { enablers.push_back(i); }
        else                         { rest.push_back(i); }
    }

    // Lands: a Karoo with nothing to bounce is a blank, so it is kept LAST among lands and does not
    // count toward the quota. Non-Karoo lands first, then Karoos.
    const bool karoo_live = board_lands > 0
        || std::any_of(lands.begin(), lands.end(), [&](int i) { return !is_karoo(i); });
    std::stable_sort(lands.begin(), lands.end(), [&](int a, int b)
    { return static_cast<int>(is_karoo(a)) < static_cast<int>(is_karoo(b)); });

    // Rocks by yield: Sol Ring (produces_amount 2 off {1}) first, then cheapest.
    std::stable_sort(rocks.begin(), rocks.end(), [&](int a, int b)
    {
        const CardDefinition* da = def_of(ap.hand[a]);
        const CardDefinition* db = def_of(ap.hand[b]);
        const int ya = da ? da->params.produces_amount : 1;
        const int yb = db ? db->params.produces_amount : 1;
        if (ya != yb) { return ya > yb; }
        return mv_of(a) < mv_of(b);
    });
    // Payoffs: cheapest-to-deploy kept first, because distance-to-playable is what decides which of
    // them is real. A resolved reducer erases 2 of that distance for EVERY payoff, which is the one
    // place the fallback's "shed the biggest" must stop agreeing with us.
    const int reducer_discount = 2 * (board_reducers > 0 ? 1 : 0);
    std::stable_sort(payoffs.begin(), payoffs.end(), [&](int a, int b)
    { return (mv_of(a) - reducer_discount) < (mv_of(b) - reducer_discount); });
    // Enablers: cheapest first, and a rock outranks a pure trick at equal cost.
    std::stable_sort(enablers.begin(), enablers.end(), [&](int a, int b)
    {
        if (mv_of(a) != mv_of(b)) { return mv_of(a) < mv_of(b); }
        return static_cast<int>(is_rock(a)) > static_cast<int>(is_rock(b));
    });

    // ---- quotas ---------------------------------------------------------------------------------
    const int kLandTarget    = 5;    // "enough land to have 4 or 5 on the field" (user)
    const int kPayoffFloor   = 2;    // "2 at minimum" (user)
    const int kEnablerSlots  = 1;    // "if there is space" -- soft, filled from overflow only

    int land_need = std::max(0, kLandTarget - board_lands - board_rocks);
    int reducer_need = board_reducers > 0 ? 1 : 2;

    std::vector<char> keep(static_cast<std::size_t>(n), 0);
    std::vector<int>  taken_order;                 // acquisition order; reversed, it is the keep tail
    auto take = [&](int i)
    { keep[static_cast<std::size_t>(i)] = 1; taken_order.push_back(i); };

    for (int i : lands)
    {
        if (land_need <= 0) { break; }
        if (is_karoo(i) && !karoo_live) { continue; }   // a self-bouncing Karoo is not a land
        take(i); --land_need;
    }
    for (int i : rocks) { if (land_need <= 0) { break; } take(i); --land_need; }
    for (int i : reducers) { if (reducer_need <= 0) { break; } take(i); --reducer_need; }
    int payoff_need = kPayoffFloor;
    for (int i : payoffs) { if (payoff_need <= 0) { break; } take(i); --payoff_need; }
    // The soft enabler slot: only once every hard quota above is satisfied.
    if (land_need <= 0 && reducer_need <= 0 && payoff_need <= 0)
    {
        int slots = kEnablerSlots;
        for (int i : enablers) { if (slots <= 0) { break; } if (!keep[i]) { take(i); --slots; } }
    }

    // ---- shed order: everything unkept, weakest first -------------------------------------------
    std::vector<int> shed;
    std::vector<char> listed(static_cast<std::size_t>(n), 0);
    auto put = [&](int i)
    {
        if (i < 0 || i >= n || listed[static_cast<std::size_t>(i)]) { return; }
        if (ap.hand[i].m_is_staged) { return; }
        listed[static_cast<std::size_t>(i)] = 1; shed.push_back(i);
    };
    auto put_unkept = [&](int i) { if (!keep[static_cast<std::size_t>(i)]) { put(i); } };

    // S0 -- a dead Karoo (nothing to bounce) is the single most worthless card in the hand.
    for (int i : lands) { if (is_karoo(i) && !karoo_live) { put_unkept(i); } }
    // S1 -- surplus lands beyond the quota. The deck never wants a sixth source.
    for (int i : lands) { put_unkept(i); }
    // S2 -- surplus payoffs, MOST EXPENSIVE FIRST. This is where the fallback and this policy agree
    // in the common land-light state and diverge once a reducer lands (see reducer_discount).
    for (auto it = payoffs.rbegin(); it != payoffs.rend(); ++it) { put_unkept(*it); }
    // S3 -- surplus reducers: dead once the discount is already online.
    for (auto it = reducers.rbegin(); it != reducers.rend(); ++it) { put_unkept(*it); }
    // S4 -- surplus enablers and everything unclassified, cheapest-value last.
    for (auto it = enablers.rbegin(); it != enablers.rend(); ++it) { put_unkept(*it); }
    for (int i : rocks) { put_unkept(i); }
    for (int i : rest)  { put_unkept(i); }
    // The KEEP TAIL: the quota order read backwards, so the list covers the WHOLE hand. Without it
    // the quota-protected cards fall through to the shared tier B -- descending mana value -- which
    // is the exact ranking this provider exists to overturn, and on a hand where every card is
    // quota-covered (common on a deck whose curve tops at eight) that fallback decided everything.
    for (auto it = taken_order.rbegin(); it != taken_order.rend(); ++it) { put(*it); }

    return CleanupDiscardRankingWithOrder(s, required_pieces, shed);
}

// ---- StompyProvider::CleanupDiscardCandidates -------------------------------
//
// Cleanup discard: the USER-AUTHORED role-bucket policy (2026-08-21, worst-case allocation
// per user review the same day). The hand divides into role buckets -- <mana> <threats>
// <enablers> -- and the WORST CASE (a deep flood discarding to 7) keeps the tight breakdown
// **4 mana / 2 threats / 1 enabler**; buckets a board already covers shrink, and any slot a
// bucket does not need fills back with threats or additional scaling mana (user). Dead
// copies (a hand Call/Guile with one already on board) belong to no bucket and shed first.
//   MANA bucket (<=4 slots): sources count by EFFECTIVE yield, board always netted out
//     first -- every own permanent counts (lands/rocks by produces_amount, a scaling dork
//     (Priest of Titania / Elvish Archdruid, `mana_per_creature_subtype`) by its live
//     subtype count). COMPOSITION (user): lean 1 LAND + 3 ACCELERATORS (dorks + rocks;
//     "you might draw another land in the next two turns"), but "you always want at least a
//     land for next turn if you can" -- best land first, then accelerators, backfilling
//     from whichever side remains when the other runs short. Within accelerators: Sol Ring
//     first when available ("a generically insane card" -- it also never sheds as early
//     excess, only in the slack zone), then one 1-mana dork if neither hand nor battlefield
//     has one, then the rest by yield. Stops when board + kept yield reaches the TARGET:
//     floor 6, raised to 7 when the kept plan needs 7+ (Turntimber-only route, or a 7+-drop
//     hardcast with no cheaper route) -- but NEVER more than 4 slots: "we should not aim
//     for 7 mana if that is going to take more than 4 card slots".
//     Turntimber always keeps (its back face is a land drop) and counts against the cap.
//   THREAT bucket (2 slots): threats are NOT hand cards in this deck (user) -- with a live
//     DEPLOYMENT ROUTE (Call on board {2}{G}=3 / castable 4, Natural Order + green fodder 4,
//     Turntimber front face 7, judged against reach = board + all hand yield) the library
//     serves every fatty, sometimes more cheaply than hardcast. Spares (same-name in
//     library, or a hand duplicate) never take a slot. Slot preference among last copies:
//     7-8-drops fill the role; an 11-drop "only very very rarely" (user) -- it takes a slot
//     only when nothing cheaper can. The hoof-role (Craterhoof) last copy always takes a
//     slot: pitching it removes it from every fetch route and the h=0 kill projection.
//     A route-covered last copy that is unplayable from hand (mv > reach) sheds early.
//   ENABLER bucket (1 slot; 0 with Call already on board -- the board covers the role):
//     pick priority Call > Natural Order > Worldly Tutor > Mirri's Guile. The unkept rest
//     sheds weakest-first (Guile, extra Calls/Orders, extra Tutors last -- a 2nd Tutor is
//     the best surplus enabler and goes just before the slack zone).
//   SLACK: unneeded slots refill in shed-resistance order -- unpicked scaling dorks survive
//     longer than flat ones, and unslotted playable threats survive longest of all the
//     surplus ("fill them in with threats or additional scaling mana").
// The list names EVERY hand card (the Mirrorwing gi295 lesson: an under-covering list hands
// the decision to the shared ranking's highest-MV fallback -- which here sheds exactly
// backwards, pitching Craterhoof and the engine while keeping flood). Classification is by
// card params, not names, so a screening arm that swaps a card keeps the right bucket.
// This is a CHOICE, not a search: CleanupDiscardSearchWidth() stays at the base 1, so the
// front of this ranking IS the shed. MTG_STOMPY_BUCKET_DISCARD=0 -> generic base (A/B).
std::vector<int> StompyProvider::CleanupDiscardCandidates(
    const GameState& s, const std::vector<std::string>* required_pieces) const
{
    static const bool s_bucket = EnvOn("MTG_STOMPY_BUCKET_DISCARD", true);
    if (!s_bucket) { return GenericProvider::CleanupDiscardCandidates(s, required_pieces); }

    const Player& ap = s.players[s.active_player_index];
    const int n = static_cast<int>(ap.hand.size());
    auto def_of = [](const Card& c) { return CardDatabase::Instance().LookupCached(c); };
    auto is_dork = [&](const Card& c)
    { const CardDefinition* d = def_of(c); return d && d->tmpl == CardTemplate::ManaDork; };
    auto is_land = [&](const Card& c)
    { const CardDefinition* d = def_of(c); return d ? d->card.IsLand() : c.IsLand(); };
    auto is_rock = [&](const Card& c)
    { const CardDefinition* d = def_of(c); return d && d->params.mana_rock; };
    auto is_hoof_role = [&](const Card& c)
    { const CardDefinition* d = def_of(c); return d && d->params.etb_team_pump_per_creature; };

    auto scaling_sub = [&](const Card& c) -> const std::string*
    {
        const CardDefinition* d = def_of(c);
        return (d && !d->params.mana_per_creature_subtype.empty())
             ? &d->params.mana_per_creature_subtype : nullptr;
    };
    auto subtype_on_board = [&](const std::string& sub)
    {
        int k = 0;
        for (const Permanent& p : s.battlefield)
        { if (p.card.IsCreature() && CardHasSubtype(p.card, sub)) { ++k; } }
        return k;
    };

    // Board census: EFFECTIVE yield -- every own permanent counts (user: "sources on board
    // should always be counted, for any permanents"): lands/rocks by produces_amount, a
    // scaling dork by its live subtype count, flat dorks 1. Also which persisting enablers
    // are down, and a body for Natural Order fodder (mono-green deck: any own creature is
    // treated as green).
    int  board_yield = 0;
    bool call_board = false, guile_board = false, own_body = false, board_cheap_dork = false;
    for (const Permanent& p : s.battlefield)
    {
        if (p.controller_index != s.active_player_index) { continue; }
        const CardDefinition* d = def_of(p.card);
        if (is_land(p.card) || is_rock(p.card))
        { board_yield += d ? std::max(1, d->params.produces_amount) : 1; }
        else if (is_dork(p.card))
        {
            const std::string* sub = scaling_sub(p.card);
            board_yield += sub ? std::max(1, subtype_on_board(*sub)) : 1;
            if (p.card.m_mana_cost.ManaValue() <= 1) { board_cheap_dork = true; }
        }
        if (d && d->params.activated_reveal_top_cost.has_value()) { call_board  = true; }
        if (d && d->params.upkeep_reorder > 0)                    { guile_board = true; }
        if (d && d->card.IsCreature())                            { own_body    = true; }
    }

    // Hand census (staged cards are in exile -- never bucketed). Enablers split by param so
    // each rule can address its own kind.
    std::vector<int> loose_mana, dorks, threats, calls, tutors, orders, guiles, turntimbers;
    for (int i = 0; i < n; ++i)
    {
        const Card& c = ap.hand[i];
        if (c.m_is_staged) { continue; }
        if (is_dork(c)) { dorks.push_back(i); continue; }
        const CardDefinition* d = def_of(c);
        const CardParams* p = d ? &d->params : nullptr;
        if (p && p->look_top_put_creature_count > 0)      { turntimbers.push_back(i); continue; }
        if (p && p->activated_reveal_top_cost.has_value()){ calls.push_back(i);       continue; }
        if (p && p->tutor_to_top)                         { tutors.push_back(i);      continue; }
        if (p && p->tutor_to_battlefield_single)          { orders.push_back(i);      continue; }
        if (p && p->upkeep_reorder > 0)                   { guiles.push_back(i);      continue; }
        if (is_land(c) || is_rock(c))                     { loose_mana.push_back(i);  continue; }
        if (d && d->card.IsCreature())                    { threats.push_back(i); }
    }
    // Prospective yield of a hand mana card once deployed (a scaling dork joins the board and
    // counts itself; Turntimber's back face is one land drop).
    auto hand_yield = [&](int i)
    {
        const Card& c = ap.hand[i];
        const CardDefinition* d = def_of(c);
        if (is_land(c) || is_rock(c)) { return d ? std::max(1, d->params.produces_amount) : 1; }
        if (is_dork(c))
        {
            const std::string* sub = scaling_sub(c);
            return sub ? subtype_on_board(*sub) + 1 : 1;
        }
        return 1;
    };
    // Sheddable loose mana, shed-soonest-first: plain colored lands (Forest -- fungible),
    // then the colourless utility land (Lodge -- the untap burst), then the rock (Sol Ring).
    auto mana_shed_rank = [&](int i)
    {
        const Card& c = ap.hand[i];
        if (is_rock(c)) { return 2; }
        const CardDefinition* d = def_of(c);
        bool colored = false;
        if (d) { for (Color col : d->params.produces) { if (col != Color::Colorless) { colored = true; } } }
        return colored ? 0 : 1;
    };
    std::stable_sort(loose_mana.begin(), loose_mana.end(),
                     [&](int a, int b) { return mana_shed_rank(a) < mana_shed_rank(b); });

    // REACH: what this hand could develop keeping all its mana -- board effective yield plus
    // every hand card's prospective yield. Route liveness and hand-playability are judged on
    // this potential; the mana bucket then keeps only what the chosen plan needs.
    int reach = board_yield;
    for (int i : loose_mana)  { reach += hand_yield(i); }
    for (int i : dorks)       { reach += hand_yield(i); }
    reach += static_cast<int>(turntimbers.size());

    // Deployment routes that drop fatties from the library, cheapest live one wins:
    // Call activation {2}{G} = 3 (board) / cast it first = 4 (hand), Natural Order = 4 with
    // green fodder (a dork or any own body), Turntimber front face = 7.
    const bool fodder = own_body || !dorks.empty();
    int route_cost = 1000;
    if (call_board)                { route_cost = std::min(route_cost, 3); }
    if (!calls.empty())            { route_cost = std::min(route_cost, 4); }
    if (!orders.empty() && fodder) { route_cost = std::min(route_cost, 4); }
    if (!turntimbers.empty())      { route_cost = std::min(route_cost, 7); }
    const bool route_live = route_cost <= reach;

    // THREAT bucket (2 slots): spares (same-name in library, or a hand duplicate) never take
    // one; a route-covered last copy that is unplayable from hand (mv > reach) sheds early;
    // the hoof-role last copy always takes a slot. Slot preference among the rest: 7-8-drops
    // fill the role, an 11-drop only when nothing cheaper can (user), cheapest first.
    auto lib_copies = [&](const Card& c)
    {
        int k = 0;
        for (const Card& l : ap.library) { if (l.m_name == c.m_name) { ++k; } }
        return k;
    };
    auto mv_of = [&](int i) { return CleanupDiscardManaValue(ap.hand[i]); };
    auto mv_desc = [&](std::vector<int>& v)
    {
        std::stable_sort(v.begin(), v.end(), [&](int a, int b) { return mv_of(a) > mv_of(b); });
    };
    std::vector<int> spares, dead_lasts, slot_pool;
    int hoof_keep = -1;
    std::vector<std::string> seen;
    for (int i : threats)
    {
        const bool dup = std::find(seen.begin(), seen.end(), ap.hand[i].m_name) != seen.end();
        seen.push_back(ap.hand[i].m_name);
        if (dup || lib_copies(ap.hand[i]) > 0)      { spares.push_back(i); continue; }
        if (is_hoof_role(ap.hand[i]) && hoof_keep < 0) { hoof_keep = i; continue; }
        if (route_live && mv_of(i) > reach)         { dead_lasts.push_back(i); continue; }
        slot_pool.push_back(i);
    }
    mv_desc(spares); mv_desc(dead_lasts);
    std::stable_sort(slot_pool.begin(), slot_pool.end(), [&](int a, int b)
    {
        const bool fa = mv_of(a) <= 8, fb = mv_of(b) <= 8;
        if (fa != fb) { return fa; }   // role-sized (7-8-drop) before the 11-drop
        return mv_of(a) < mv_of(b);    // then cheapest = most deployable
    });
    std::vector<int> kept_threats, slack_threats;
    if (hoof_keep >= 0) { kept_threats.push_back(hoof_keep); }
    for (int i : slot_pool)
    {
        if (static_cast<int>(kept_threats.size()) < 2) { kept_threats.push_back(i); }
        else                                           { slack_threats.push_back(i); }
    }

    // MANA target: floor 6, raised to 7 when the kept plan needs 7+ (the cheapest live route
    // is Turntimber's front face, or the kept win path is a 7+-drop hardcast with no cheaper
    // route). User: "we might want 7 given the deck's mana costs, but it depends on what
    // else we are keeping."
    int plan_cost = 6;
    if (route_live) { plan_cost = route_cost; }
    else if (!kept_threats.empty())
    {
        int cheapest = 1000;
        for (int i : kept_threats) { cheapest = std::min(cheapest, mv_of(i)); }
        plan_cost = cheapest;
    }
    const int target = plan_cost >= 7 ? 7 : 6;

    // MANA bucket (<=4 slots, Turntimbers count against the cap and always keep): greedy by
    // prospective yield -- a scaled Priest/Archdruid covers the target in fewer slots ("the
    // mana bucket should count the scaling of scaling dorks") -- under the COMPOSITION
    // preference (user): lean 1 LAND + 3 ACCELERATORS ("you might draw another land in the
    // next two turns"), but "you always want at least a land for next turn if you can" -- so
    // the walk is best land first, then accelerators, backfilling from whichever side
    // remains when the other runs short. Within accelerators (user): Sol Ring first when
    // available ("a generically insane card"), then one 1-mana dork if neither hand nor
    // battlefield has one yet, then the rest by yield. Stops at the target or the cap,
    // whichever first: "we should not aim for 7 mana if that is going to take more than 4
    // card slots."
    std::vector<int> land_pool, accel_pool;
    for (int i : loose_mana)
    { (is_land(ap.hand[i]) ? land_pool : accel_pool).push_back(i); }
    for (int i : dorks) { accel_pool.push_back(i); }
    std::stable_sort(land_pool.begin(), land_pool.end(), [&](int a, int b)
    {
        const int ya = hand_yield(a), yb = hand_yield(b);
        if (ya != yb) { return ya > yb; }
        return mana_shed_rank(a) > mana_shed_rank(b);   // the utility 1-of over a Forest
    });
    std::stable_sort(accel_pool.begin(), accel_pool.end(), [&](int a, int b)
    {
        const int ya = hand_yield(a), yb = hand_yield(b);
        if (ya != yb) { return ya > yb; }
        const bool da = is_dork(ap.hand[a]), db = is_dork(ap.hand[b]);
        return da > db;                                  // tie: the dork is a body too
    });
    std::vector<int> accel_seq;
    {
        std::vector<char> used(accel_pool.size(), 0);
        for (std::size_t k = 0; k < accel_pool.size(); ++k)          // the rock leads
        { if (is_rock(ap.hand[accel_pool[k]])) { used[k] = 1; accel_seq.push_back(accel_pool[k]); break; } }
        if (!board_cheap_dork)                                       // one 1-mana dork
        {
            for (std::size_t k = 0; k < accel_pool.size(); ++k)
            {
                const int i = accel_pool[k];
                if (!used[k] && is_dork(ap.hand[i]) && ap.hand[i].m_mana_cost.ManaValue() <= 1)
                { used[k] = 1; accel_seq.push_back(i); break; }
            }
        }
        for (std::size_t k = 0; k < accel_pool.size(); ++k)
        { if (!used[k]) { accel_seq.push_back(accel_pool[k]); } }
    }
    std::vector<int> mana_seq;
    {
        std::size_t li = 0, ai = 0;
        if (li < land_pool.size()) { mana_seq.push_back(land_pool[li++]); }
        while (ai < accel_seq.size()) { mana_seq.push_back(accel_seq[ai++]); }
        while (li < land_pool.size())  { mana_seq.push_back(land_pool[li++]); }
    }
    int mana_slots = static_cast<int>(turntimbers.size());
    int covered    = board_yield + static_cast<int>(turntimbers.size());
    std::vector<int>  kept_mana;   // keep-preference order
    std::vector<char> kept_hand(static_cast<std::size_t>(n), 0);
    for (int i : mana_seq)
    {
        if (mana_slots >= 4 || covered >= target) { break; }
        kept_hand[static_cast<std::size_t>(i)] = 1; kept_mana.push_back(i);
        ++mana_slots; covered += hand_yield(i);
    }

    // ENABLER bucket (1 slot; 0 with Call already on board -- the board covers the role):
    // Call > Natural Order (with fodder) > Worldly Tutor > Guile.
    int kept_enabler = -1;
    if (!call_board)
    {
        if      (!calls.empty())            { kept_enabler = calls.front();  }
        else if (!orders.empty() && fodder) { kept_enabler = orders.front(); }
        else if (!tutors.empty())           { kept_enabler = tutors.front(); }
        else if (!guiles.empty())           { kept_enabler = guiles.front(); }
    }

    std::vector<int> shed;
    std::vector<char> listed(static_cast<std::size_t>(n), 0);
    auto put = [&](int i)
    { if (i >= 0 && i < n && !listed[static_cast<std::size_t>(i)]
          && !ap.hand[i].m_is_staged) { listed[static_cast<std::size_t>(i)] = 1; shed.push_back(i); } };

    // S0 -- dead copies (no bucket): hand copies of a persisting enabler already on board.
    if (call_board)  { for (int i : calls)  { put(i); } }
    if (guile_board) { for (int i : guiles) { put(i); } }

    // S1 -- loose LANDS that are EXCESS TO TARGET (the greedy met the target without them),
    // in shed rank order (Forests, then Lodge). Loose mana the greedy wanted but the 4-slot
    // CAP truncated is NOT excess -- it sheds late, in the slack zone (S4), so a single
    // forced discard never takes a needed land while a spare fatty sits in hand (game-seed
    // 8095: the cap-truncated 3rd Forest shed ahead of a spare Worldspine on an empty
    // board -- one turn lost). Unpicked DORKS are never excess (slack bodies, S4), and
    // neither is an unpicked Sol Ring (user: "keep sol ring if available -- a generically
    // insane card"): it too sheds only in the slack zone.
    if (covered >= target)
    {
        for (int i : loose_mana)
        { if (!kept_hand[static_cast<std::size_t>(i)] && !is_rock(ap.hand[i])) { put(i); } }
    }

    // S2 -- threat spares biggest-MV-first, then the route-covered unplayable last copies.
    for (int i : spares)     { put(i); }
    for (int i : dead_lasts) { put(i); }

    // S3 -- enablers beyond the one kept slot, weakest kind first; a 2nd Worldly Tutor is
    // the best surplus enabler and sheds last of these.
    for (int i : guiles) { if (i != kept_enabler) { put(i); } }
    for (int i : calls)  { if (i != kept_enabler) { put(i); } }
    for (int i : orders) { if (i != kept_enabler) { put(i); } }
    for (int i : tutors) { if (i != kept_enabler) { put(i); } }

    // S4 -- SLACK zone ("fill them in with threats or additional scaling mana"): first the
    // cap-truncated loose mana (wanted for the target but over the 4-slot cap -- it goes in
    // the worst case, but only after the true junk), then unpicked dorks (flat first,
    // scaling last -- the best surplus mana), then unslotted playable threats (worst
    // keep-preference first), which survive longest of all the surplus.
    if (covered < target)
    {
        for (int i : loose_mana)
        { if (!kept_hand[static_cast<std::size_t>(i)] && !is_rock(ap.hand[i])) { put(i); } }
    }
    {
        std::vector<int> slack_dorks;
        for (int i : dorks)
        { if (!kept_hand[static_cast<std::size_t>(i)]) { slack_dorks.push_back(i); } }
        std::stable_sort(slack_dorks.begin(), slack_dorks.end(),
                         [&](int a, int b) { return hand_yield(a) < hand_yield(b); });
        for (int i : slack_dorks) { put(i); }
        // The unpicked rock sheds last of the slack mana (kept over any surplus dork).
        for (int i : loose_mana)
        { if (!kept_hand[static_cast<std::size_t>(i)] && is_rock(ap.hand[i])) { put(i); } }
    }
    for (std::size_t k = slack_threats.size(); k-- > 0; ) { put(slack_threats[k]); }

    // S5 -- the KEEPS (the tight 4 mana / 2 threats / 1 enabler), least critical first, so
    // the MV fallback never decides (the Mirrorwing gi295 lesson); a safety sweep names any
    // unclassified card; the hoof-role keep is dead last.
    for (std::size_t k = kept_mana.size(); k-- > 0; )     { put(kept_mana[k]); }
    if (kept_enabler >= 0)                                { put(kept_enabler); }
    for (std::size_t k = kept_threats.size(); k-- > 0; )
    { if (kept_threats[k] != hoof_keep) { put(kept_threats[k]); } }
    for (int i : turntimbers)                             { put(i); }
    for (int i = 0; i < n; ++i) { if (i != hoof_keep) { put(i); } }
    if (hoof_keep >= 0)                                   { put(hoof_keep); }

    return CleanupDiscardRankingWithOrder(s, required_pieces, shed);
}

// Tutor target ordering (Worldly Tutor creature-to-top / Natural Order green-creature-to-play).
// ADOPTED (USER-AUTHORED lethality calculation, 2026-08-21; swept as MTG_STOMPY_TUTOR_HEUR V3 and
// validated held-out -- see docs/design/analysis-StompySurprise.md for the table; the static V0
// and early-ramp V1 variants it beat are recorded there, scaffolding deleted per the
// heuristic-optimization skill). "It depends on when we can deploy it and how low the opponent
// is":
//   * THREAT POOL = the team pump + power-6+ bodies (Worldspine / Terastodon / Elderscale /
//     Vaultborn), ranked by EARLIEST LETHAL: per candidate, deploy turn (a live Call of the Wild
//     or a hand Turntimber flips the tutored top for ITS cost regardless of the threat's MV --
//     the whole point of the cheat-out; hard-cast waits for the draw plus mana growth at a
//     conservative +1 source/turn), plus a wake turn unless it is the hasted team pump, then
//     "does that swing close the opponent's CURRENT life". No lethal in reach -> the arrival
//     term rules ("maybe a 7 drop like Vaultborn Tyrant if we need it -- i.e. we can't play an
//     8+ drop reasonably"), and an open cheat-out equalises arrivals so the swing tie-break
//     hands the slot back to the fatties.
//   * "Titania when we are really low on mana": Worldly Tutor with <=2 board sources promotes
//     the scaled dorks, cheapest first (Priest over Archdruid).
//   * Natural Order runs the SAME ranking with deployment free ("happens more naturally, and
//     doesn\'t require any calculation"): t_dep 0 for every size, the sacrifice docked from the
//     census, and no starving tier (its slot is a threat decision).
// Census idiom matches ProjectEtbDestroyK (EffectivePower net of until-EOT bonuses, no lord
// credit -- under-counting is the safe direction). Ordering-only over the full candidate list;
// the width the axis searches is TutorSearchWidth() as usual. Honors MTG_UNPRUNE=tutor.
std::vector<std::string>
StompyProvider::TutorCandidates(const GameState& s, int controller, const CardParams& pp) const
{
    std::vector<std::string> cands = GenericProvider::TutorCandidates(s, controller, pp);
    if (DecisionUnpruned(UnprunedGate::Tutor) || cands.size() <= 1) { return cands; }

    if (pp.tutor_to_top || pp.tutor_to_battlefield_single)
    {
        const bool is_no = pp.tutor_to_battlefield_single;
        const Player& ap = s.players[controller];
        const int opp_life = s.players[1 - controller].life;
        // Board survey for the lethality model (rewritten 2026-08-26; the original conflated
        // three quantities the gi130 family showed must stay separate):
        //   n_cre     -- creatures that will be pumped (Craterhoof's X counts ALL of them);
        //   atk_sum/atk_n -- the ATTACK-CAPABLE subset and its lorded power (at RESOLUTION the
        //                 payment is real: a tapped or this-turn creature deals nothing; pre-cast
        //                 everything is still untapped so the two coincide);
        //   max_atk   -- the largest single attacker's lorded power (the margin term below).
        // Lord bonuses are ADDED here (EffectivePower excludes them; Priest under Archdruid
        // attacks for 2, not 1 -- the lordless read was off by one per lord which is exactly the
        // margin these 18-vs-18 kills live on).
        int n_cre = 0, atk_n = 0, atk_sum = 0, max_atk = 0, board_sources = 0;
        bool call_live = false;
        for (const Permanent& q : s.battlefield)
        {
            if (q.controller_index != controller) { continue; }
            const CardDefinition* qd = CardDatabase::Instance().LookupCached(q.card);
            if (qd != nullptr)
            {
                if (qd->params.activated_reveal_top_cost.has_value() && !q.tapped)
                { call_live = true; }
                if (qd->card.IsLand() || qd->tmpl == CardTemplate::ManaDork
                    || qd->params.mana_rock) { ++board_sources; }
            }
            if (!(q.card.IsCreature() || q.is_animated)) { continue; }
            ++n_cre;
            const int lp = std::max(0, q.EffectivePower() - q.temp_power_bonus
                                       + ComputeLordBonus(q.card, s, controller,
                                                          false, &q).first);
            const bool can_attack = !g_tutor_at_resolution
                                  || (!q.tapped && !q.entered_this_turn);
            if (can_attack) { ++atk_n; atk_sum += lp; max_atk = std::max(max_atk, lp); }
        }
        // Pre-cast, Natural Order's board still holds the sacrifice-to-be: discount one body. At
        // RESOLUTION (g_tutor_at_resolution) the cost is paid and the victim is already gone --
        // discounting again double-counts the sac (st993's resolution front went Worldspine).
        if (is_no && !g_tutor_at_resolution)
        { n_cre = std::max(0, n_cre - 1); atk_n = std::max(0, atk_n - 1);
          atk_sum = std::max(0, atk_sum - 1); }
        // DIG INSTRUMENT (MTG_STOMPY_TUTOR_TRACE, default off): one line per ranking call.
        static const bool s_ttrace = EnvOn("MTG_STOMPY_TUTOR_TRACE");
        if (s_ttrace)
        {
            std::fprintf(stderr, "[sttut] res=%d is_no=%d oppL=%d n=%d atkn=%d atk=%d T%d\n",
                         g_tutor_at_resolution ? 1 : 0, is_no ? 1 : 0, opp_life, n_cre,
                         atk_n, atk_sum, s.turn_number);
        }
        int outlet_t = 1000;   // turns until a cheat-out can flip the tutored top into play
        if (is_no) { outlet_t = 0; }
        else
        {
            if (call_live) { outlet_t = std::max(0, 5 - board_sources); }   // tutor {G} + {2}{G}
            for (const Card& hc : ap.hand)
            {
                const CardDefinition* hd = CardDatabase::Instance().LookupCached(hc);
                if (hd && hd->params.look_top_put_creature_count > 0)
                { outlet_t = std::min(outlet_t, std::max(0, 1 + hd->card.m_mana_cost.ManaValue()
                                                                - board_sources)); }
            }
        }
        struct Rank { int kill; int tie; };
        auto rank_threat = [&](const CardDefinition& d) -> Rank
        {
            const bool  pump  = d.params.etb_team_pump_per_creature;
            const int   mv    = d.card.m_mana_cost.ManaValue();
            const int   t_dep = is_no ? 0
                              : std::min(outlet_t, std::max(1, mv - board_sources));
            const int   t_sw  = t_dep + (pump ? 0 : 1);   // only the team pump has haste
            // Team-pump swing: Craterhoof's X counts ITSELF ("+X/+X ... where X is the number of
            // creatures you control" -- the Hoof is on the battlefield when its ETB resolves), so
            // X = n_cre + 1, ATTACKERS each swing for (lorded power + X), and the hasty Hoof adds
            // 5 + X. The original n^2 + n + 5 form modelled X = n (Hoof excluded) on a lordless
            // all-creatures sum -- st993's T4 read 15 vs a true alpha of 18 against 16 life, and
            // gi130's post-payment boards read pumped power for bodies that were TAPPED for the
            // very cost being paid.
            const int   swing = pump ? atk_sum + atk_n * (n_cre + 1) + 5 + (n_cre + 1)
                                     : atk_sum + d.card.m_power.value_or(0);
            // (A permanent-board-stats tie-break was tried for the non-lethal case and REVERTED:
            // d0 -- the purest front-quality read, no axis exists there -- measured it 0.032
            // WORSE than the swing tie-break, and it recovered none of the searched-depth gap.)
            return { swing >= opp_life ? t_sw : 1000 + t_sw, swing };
        };
        // NO-DOCTRINE (MTG_STOMPY_NO_DOCTRINE, default ON, adopted 2026-08-26 on user approval
        // "Okay, fair enough. Let's keep Hornet Queen then"; =0 disables. Held-out: all 8
        // searched cells faster, 31 games incl. three new turn-3 kills, the one slower game
        // budget-recovers; greedy byte-identical everywhere. Known accepted trade: train gi65
        // (seed 2067) -- the elf exclusion lowers the bottomer's valuation of a Natural-Order
        // keep, it keeps Worldly Tutor instead and that hand's best is T5 vs the old keep's T4;
        // one game across ~11,600 measured, none recurred on held-out): USER doctrine
        // 2026-08-26 for the Natural Order fetch -- "probably zero cases where you want a small
        // elf. It is pretty much Craterhoof, Worldspine Wurm, Terastodon, Hornet Queen, Vaultborn
        // Tyrant in that order." Encoded by card shape, not name: team pump > the biggest bodies
        // by power > a multi-body token ETB (Hornet Queen, power 2 but five bodies -- invisible
        // to the power>=6 threat filter today) > the remaining big body (Vaultborn). Scaled dorks
        // are excluded from the NO fan entirely. GOLDFISH-SCOPED (user, same day): Vaultborn
        // "probably incorrect in all situations (in this goldfish scenario)" -- vs a real
        // opponent (Phase 2) his lifegain body and opposing-permanent Terastodon modes are real
        // options and this ordering must be re-judged, not inherited.
        static const bool s_no_doctrine = EnvOn("MTG_STOMPY_NO_DOCTRINE", true);
        const bool no_doctrine = s_no_doctrine && is_no;
        // USER refinement 2026-08-26: "The only case for Hornet Queen would be something like
        // Craterhoof in hand or second Natural Order. Want a bigger pump for Craterhoof in
        // order to get lethal." -- five extra bodies only matter when a team pump is still
        // COMING (from hand, or fetchable by a second copy of this tutor); then she outranks
        // the flat fatties. (A hand Hoof is also no longer in the library, so she competes for
        // his vacated slot exactly in this state.)
        // DEPTH-GATED (v4, measured through three shapes): the promotion is only sound when a
        // search exists to validate the follow-up. v2 promoted everywhere -- searched depths
        // gained 4 games (two new turn-3 kills: fetch the Queen, land the hand-Hoof on her five
        // bodies) but greedy d0 fetched her blind and could not sequence the 8-mana Hoof after
        // (+56 turn-steps, one game lost). v3 gated on enumeration-context and lost the GAINS
        // too: the wins live at RESOLUTION, where an unbound Natural Order binds its fetch --
        // the searched executor's rollouts price that same resolution, greedy's don't. So the
        // gate is the RUN-level searched-play bit, the same signal cantrip-first uses.
        bool pump_follow_up = false;
        if (no_doctrine && g_searched_play)
        {
            int no_in_hand = 0;
            for (const Card& hc : ap.hand)
            {
                const CardDefinition* hd = CardDatabase::Instance().LookupCached(hc);
                if (hd == nullptr) { continue; }
                if (hd->params.etb_team_pump_per_creature) { pump_follow_up = true; break; }
                if (hd->params.tutor_to_battlefield_single) { ++no_in_hand; }
            }
            // Pre-cast, the NO being cast is itself still in hand -- a "second" copy means two.
            if (no_in_hand >= (g_tutor_at_resolution ? 1 : 2)) { pump_follow_up = true; }
        }
        auto doctrine_class = [&](const CardDefinition& d) -> int
        {
            if (d.params.etb_team_pump_per_creature)     { return 6; }
            if (d.params.etb_self_creates_tokens >= 2)   { return pump_follow_up ? 5 : 2; }
            if (d.card.m_power.value_or(0) >= 10)        { return 4; }
            if (d.card.m_power.value_or(0) >= 7)         { return 3; }
            return 1;
        };
        // USER 2026-08-26: "we should have the 4 potential targets and leave the rest out. In
        // this situation." (goldfish) -- the Natural Order pool is EXACTLY {team pump, power>=10,
        // power>=7, multi-body token ETB}; Vaultborn (class 1) is out of the contender fan even
        // as a last resort. The static fall-through below still fires when NONE of the four
        // remain in the library (a castable NO must offer something legal); the search decides
        // whether that cast is worth making at all.
        auto is_threat = [&](const CardDefinition* d)
        {
            if (d == nullptr || !d->card.IsCreature()) { return false; }
            if (no_doctrine) { return doctrine_class(*d) >= 2; }
            return d->params.etb_team_pump_per_creature || d->card.m_power.value_or(0) >= 6;
        };
        auto is_scaled = [&](const CardDefinition* d)
        { return d != nullptr && d->card.IsCreature()
              && !d->params.mana_per_creature_subtype.empty(); };
        const bool starving = !is_no && board_sources <= 2;

        // UNCERTAINTY-GATED CONTENDER LIST (user design 2026-08-21: "return a list of serious
        // contenders for the search to look over, not always take 3... in the absolute worst
        // case branch only when we are absolutely uncertain"). The calculation itself supplies
        // the certainty signal:
        //   * a LETHAL pick exists (kill class < 1000): the answer is computed -- emit the
        //     earliest-lethal pick alone (plus exact ties, which are genuinely undecided);
        //   * NO lethal in reach: the projection cannot rank board-building lines (the searched
        //     axis out-decided every static tie-break we measured here -- swing AND permanent
        //     board stats -- because the right fetch couples with the whole plan, down to the
        //     turn-1 land face in the traced games): genuinely uncertain -- emit the top three
        //     threats as the contenders.
        // Starving prepends the cheapest scaled dork. This TRUNCATES (user-authorized): the
        // un-emitted names are unreachable, the emitted LIST is the branching -- there is no
        // fixed width anywhere.
        std::vector<std::string> threats;
        for (const std::string& c : cands)
        { if (is_threat(CardDatabase::Instance().Lookup(c))) { threats.push_back(c); } }
        if (!threats.empty())
        {
            std::stable_sort(threats.begin(), threats.end(),
                             [&](const std::string& a, const std::string& b)
            {
                const CardDefinition& da = *CardDatabase::Instance().Lookup(a);
                const CardDefinition& db = *CardDatabase::Instance().Lookup(b);
                const Rank ra = rank_threat(da);
                const Rank rb = rank_threat(db);
                if (ra.kill != rb.kill) { return ra.kill < rb.kill; }
                if (no_doctrine)
                {
                    // Doctrine order among non-lethal contenders; power desc within a class.
                    const int ca = doctrine_class(da), cb = doctrine_class(db);
                    if (ca != cb) { return ca > cb; }
                }
                return ra.tie > rb.tie;
            });
            const Rank best = rank_threat(*CardDatabase::Instance().Lookup(threats.front()));
            std::vector<std::string> out;
            if (starving)
            {
                const std::string* dork = nullptr;
                int dork_mv = 0;
                for (const std::string& c : cands)
                {
                    const CardDefinition* d = CardDatabase::Instance().Lookup(c);
                    if (!is_scaled(d)) { continue; }
                    const int mv = d->card.m_mana_cost.ManaValue();
                    if (dork == nullptr || mv < dork_mv) { dork = &c; dork_mv = mv; }
                }
                if (dork != nullptr) { out.push_back(*dork); }
            }
            // COLLAPSE ONLY ON A ROBUST KILL (2026-08-26, the gi130 family). Pre-cast, the
            // "lethal" read is computed on a board the CAST ITSELF will damage: paying the
            // tutor's mana taps an attacker (or the sac eats one), and which body it costs
            // depends on the victim/tap combo -- exactly the choice the searched variants
            // exist to explore. Collapsing to one name on a 1-point margin is therefore false
            // confidence: the fixed (correct) swing formula made st993-class boards read
            // "certain" at 19-vs-18, k fell to 1, the variant fan died, and SEVEN held-out
            // games regressed 4->5 unrecoverably (the old buggy formula's underconfidence had
            // been keeping the variants alive by accident). Collapse only when the kill
            // survives losing the biggest single attacker to the cost; at RESOLUTION the board
            // is post-payment and the read is real, so collapse freely.
            const bool robust_kill = best.kill < 1000
                && (g_tutor_at_resolution
                    || best.tie - (max_atk + n_cre + 1) >= opp_life);
            if (robust_kill)
            {
                for (const std::string& c : threats)
                {
                    const Rank r = rank_threat(*CardDatabase::Instance().Lookup(c));
                    if (r.kill == best.kill && r.tie == best.tie) { out.push_back(c); }
                }
            }
            else
            {
                // No lethal computable: ramp-vs-threat is exactly the uncertain choice (traced
                // 2026-08-21: a threats-only list here truncated the tutor->Archdruid engine
                // line out of a game entirely -- T7 win became UNWON). Contenders = the top
                // three threats plus the scaled dorks; starving already put a dork in front.
                for (std::size_t k = 0; k < threats.size() && k < 3; ++k)
                { out.push_back(threats[k]); }
                // Doctrine: a Natural Order never fetches a scaled dork ("zero cases where you
                // want a small elf" -- the sacrifice trades a body for it, unlike Worldly Tutor).
                if (!no_doctrine)
                {
                    for (const std::string& c : cands)
                    {
                        if (!is_scaled(CardDatabase::Instance().Lookup(c))) { continue; }
                        if (std::find(out.begin(), out.end(), c) == out.end()) { out.push_back(c); }
                    }
                }
            }
            if (!out.empty()) { return out; }
        }
        // No threat left in the library (or nothing emitted): fall through to the static order
        // over whatever remains.
    }

    // Any other tutor kind this deck might gain: the static closer order (team pump first, then
    // power + ETB token count).
    auto score = [](const std::string& name) -> int
    {
        const CardDefinition* d = CardDatabase::Instance().Lookup(name);
        if (d == nullptr) { return 0; }
        int sc = d->card.m_power.value_or(0) + d->params.etb_self_creates_tokens;
        if (d->params.etb_team_pump_per_creature) { sc += 1000; }
        return sc;
    };
    std::stable_sort(cands.begin(), cands.end(),
                     [&](const std::string& a, const std::string& b) { return score(a) > score(b); });
    return cands;
}

// Tutor-top combo lethality model (HasExtraLethalModel / ExtraLethalDamage), the d0 half of the
// USER's reset (2026-08-21: "If in d0 I would always do it in that order if you can"). The greedy
// scores plans WITHOUT applying them, so the deferred post-tutor continuation (MTG_TOP_RESOLVE)
// is invisible to it: the plan "cast Worldly Tutor, HOLD Turntimber" projects no win and loses to
// "play Turntimber as the land" -- burning the combo piece (the isolated fixture measured exactly
// this; depth 3 composes the win, depth 0 never picks the line). This model adds the combo's
// projected swing into the subset lethality check both scorers share: when the subset casts a
// tutor_to_top, a team-pump threat (Craterhoof) is still in the library, and the LEFTOVER mana
// affords a top-consumer route afterwards (a Call of the Wild activation -- the USER's preferred
// route, "cheaper and doesn't waste a card" -- or a spare Turntimber cast), the tutored Hoof
// arrives hasted through the consumer and swings this turn. Same census idiom and pump formula as
// TutorCandidates' rank_threat (n^2 + n + 5 on top of the counted base power; under-counting is
// the safe direction). Gated on TopResolveEnabled(): without the reset the continuation never
// happens and the projection would be fiction.
bool StompyProvider::HasExtraLethalModel() const
{
    return TopResolveEnabled();
}

int StompyProvider::ExtraLethalDamage(const GameState& s,
                                      const std::vector<const CardDefinition*>& casting) const
{
    if (!TopResolveEnabled()) { return 0; }
    const CardDefinition* wt = nullptr;
    const CardDefinition* call_in_plan = nullptr;   // Call def: its CAST or its activation action
    int cast_mv = 0, cast_creatures = 0, tt_in_plan = 0;
    for (const CardDefinition* d : casting)
    {
        if (d == nullptr) { continue; }
        cast_mv += d->card.m_mana_cost.ManaValue();
        if (d->params.tutor_to_top)                     { wt = d; }
        if (d->card.IsCreature())                       { ++cast_creatures; }
        if (d->params.look_top_put_creature_count > 0)  { ++tt_in_plan; }
        if (d->params.activated_reveal_top_cost.has_value()) { call_in_plan = d; }
    }

    const int active = s.active_player_index;
    const Player& ap = s.players[active];
    // The combo's same-turn kill is specifically the HASTED team pump (Craterhoof): a plain
    // fatty deployed by the consumer enters summoning-sick and adds nothing this turn. Same
    // pump shape as TutorCandidates' rank_threat (n^2 + n + 5 on top of the counted base).
    auto pump_swing = [&]() -> int
    {
        int n = cast_creatures;
        for (const Permanent& q : s.battlefield)
        {
            if (q.controller_index == active && (q.card.IsCreature() || q.is_animated)) { ++n; }
        }
        return n * n + n + 5;
    };

    // CASE B -- the top is ALREADY intentionally stacked (this turn's tutor resolved earlier,
    // e.g. the post-tutor second pass / continuation) with the team pump on top, and this subset
    // fires a consumer (a Turntimber cast, or a Call of the Wild cast/activation). Without this
    // the continuation's Solve has no idea the consumer is a win and picks by raw evals (measured:
    // the second pass cast two Call enchantments and never took the stacked Hoof). Reading the
    // top here honours the USER's intentionality gate: top_stacked_turn is written only by a
    // deliberate tutor-to-top, never by a coincidentally-known top.
    if (s.top_stacked_turn == s.turn_number && !ap.library.empty()
        && (tt_in_plan > 0 || call_in_plan != nullptr))
    {
        const CardDefinition* td = CardDatabase::Instance().LookupCached(ap.library.front());
        if (td && td->params.etb_team_pump_per_creature) { return pump_swing(); }
    }

    // CASE A -- this subset casts the tutor itself; the consumer fires from the LEFTOVER mana
    // (the deferred continuation realises it). Mana values only -- the deck is mono-green,
    // colour payability is not the binding constraint here.
    if (wt == nullptr) { return 0; }
    bool hoof_in_lib = false;
    for (const Card& c : ap.library)
    {
        const CardDefinition* d = CardDatabase::Instance().LookupCached(c);
        if (d && d->params.etb_team_pump_per_creature) { hoof_in_lib = true; break; }
    }
    if (!hoof_in_lib) { return 0; }

    int route = std::numeric_limits<int>::max();
    // A Call already CAST by this very subset: only its activation remains to pay.
    if (call_in_plan != nullptr)
    { route = std::min(route, call_in_plan->params.activated_reveal_top_cost->ManaValue()); }
    for (const Permanent& q : s.battlefield)
    {
        if (q.controller_index != active) { continue; }
        const CardDefinition* qd = CardDatabase::Instance().LookupCached(q.card);
        if (qd && qd->params.activated_reveal_top_cost.has_value())
        { route = std::min(route, qd->params.activated_reveal_top_cost->ManaValue()); }
    }
    int hand_tt = 0;
    for (const Card& hc : ap.hand)
    {
        const CardDefinition* hd = CardDatabase::Instance().LookupCached(hc);
        if (hd == nullptr) { continue; }
        if (hd->params.look_top_put_creature_count > 0) { ++hand_tt; }
        if (hd->params.activated_reveal_top_cost.has_value())
        {
            route = std::min(route, hd->card.m_mana_cost.ManaValue()
                                  + hd->params.activated_reveal_top_cost->ManaValue());
        }
    }
    if (hand_tt > tt_in_plan)   // a SPARE Turntimber (not consumed by this subset's own casts)
    {
        for (const Card& hc : ap.hand)
        {
            const CardDefinition* hd = CardDatabase::Instance().LookupCached(hc);
            if (hd && hd->params.look_top_put_creature_count > 0)
            { route = std::min(route, hd->card.m_mana_cost.ManaValue()); break; }
        }
    }
    if (route == std::numeric_limits<int>::max()) { return 0; }
    if (AvailableManaPool(s).Total() - cast_mv < route) { return 0; }
    return pump_swing();
}

// MTG_STOMPY_ORDER -- the USER-PROPOSED StompySurprise cast order (2026-08-21, proposal recorded
// verbatim in docs/design/cast-order-rankings.md). DEFAULT OFF pending measurement (the
// heuristic-optimization loop: sweep the suite, report, adopt on approval -- on adoption this
// becomes a default-on read with an off switch, like the adopted per-deck rules).
static bool StompyOrderEnabled()
{
    // Per-job overridable (heurarm) so both arms of the adoption A/B run in ONE pooled batch
    // rather than one batch per arm -- unset everywhere => the env default, byte-identical.
    static const bool on = EnvOn("MTG_STOMPY_ORDER");   // default OFF; =1 enables (A/B lever)
    return heurarm::Flag(heurarm::STOMPY_ORDER, on);
}

int StompyProvider::CastOrderRank(const GameState& s, const CardDefinition& def) const
{
    if (!StompyOrderEnabled()) { return GenericProvider::CastOrderRank(s, def); }
    const CardParams& p = def.params;
    // The USER's tiers (their numbering in [brackets]). All param-derived, no names. Three of the
    // fourteen cannot be expressed by a cast rank and are OPEN ITEMS in the rankings doc: [6]
    // Call-of-the-Wild activations (the executor dispatches every activation AFTER every cast --
    // right for the tutor->activation compose, inexpressible for activate-on-the-unknown-top-
    // BEFORE-the-tutor lines), the [9] "Worldly Tutor resets the top-consumers" re-enable (needs
    // a breakpoint-style re-solve, not an order), and the UPKEEP Call activation (pre-draw window
    // so a late tutor's target is dumped into play for 4 instead of drawn -- not modelled yet).
    if (p.tutor_to_top)
    {
        // [9 vs 14] Worldly Tutor casts MID-turn only when something can still consume the
        // tutored top AFTER it resolves this turn: a Call activation (trails every cast) or an
        // enters-draw engine (Vaultborn -- on the battlefield watching, or in hand to cast later
        // at 16). Otherwise it is the turn's LAST cast, setting up next turn's upkeep/draw
        // ("can choose to cast either location").
        bool consumer = false;
        for (const Permanent& q : s.battlefield)
        {
            if (q.controller_index != s.active_player_index) { continue; }
            const CardDefinition* qd = CardDatabase::Instance().LookupCached(q.card);
            if (qd && (qd->params.activated_reveal_top_cost.has_value()
                       || qd->params.own_creature_enters_draw > 0)) { consumer = true; break; }
        }
        if (!consumer)
        {
            // Hand consumers include a Turntimber (look_top): it now ranks 15 -- AFTER this 14 --
            // exactly so the {tutor, Turntimber} pair composes; without counting it here a
            // Call-less/Vaultborn-less hand would send the tutor to 24 (after 15) and break it.
            for (const Card& hc : s.players[s.active_player_index].hand)
            {
                const CardDefinition* hd = CardDatabase::Instance().LookupCached(hc);
                if (hd && (hd->params.activated_reveal_top_cost.has_value()
                           || hd->params.own_creature_enters_draw > 0
                           || hd->params.look_top_put_creature_count > 0)) { consumer = true; break; }
            }
        }
        return consumer ? 14 : 24;
    }
    if (def.tmpl == CardTemplate::ManaDork)
    {
        // [1] 1-mana elves first ("Sol Ring can't fund these, but they can be funded by scaling
        // elves if they are played early"); [3-4] scaling elves cheapest-first ("more elves can
        // still be discounted and the cheapest is easiest to do this with").
        if (!p.mana_per_creature_subtype.empty())
        { return def.card.m_mana_cost.ManaValue() <= 2 ? 6 : 7; }
        return 3;
    }
    if (p.mana_rock && !def.card.IsCreature())   { return 5;  }  // [2] Sol Ring: funds almost everything else
    if (p.activated_reveal_top_cost.has_value()) { return 8;  }  // [5] Call of the Wild (cast)
    if (p.look_top_put_creature_count > 0)
    {
        // [7 vs post-tutor] Turntimber Symbiosis: rank 12 consumes the CURRENT top -- right when
        // that top was intentionally stacked (a tutor already resolved this turn: upkeep Guile, an
        // earlier pass's Worldly Tutor). But when an UNCAST tutor_to_top is still in HAND and the
        // top is unstacked, a plan pairing {Worldly Tutor, Turntimber} at 12-before-14 executes
        // Turntimber into seven blanks and leaves the tutor's stack with no consumer -- one
        // Turntimber copy = the compose is dead (measured: d5 gi47's hold line realizes T4 under
        // reset-only and T5 under the order flag for exactly this reason). USER: "they will almost
        // certainly be better if Worldly Tutor was just cast" -- so it slots AFTER the mid tutor
        // (15), and the re-solve loop still re-enables a second copy either way. Known edge, in
        // the doc: with TWO Turntimbers + a tutor, both share rank 15, losing "spend #1 on the
        // unknown top first"; the loop's pass structure, not the rank, is the route for that.
        const bool stacked = (s.top_stacked_turn == s.turn_number);
        if (!stacked)
        {
            for (const Card& hc : s.players[s.active_player_index].hand)
            {
                const CardDefinition* hd = CardDatabase::Instance().LookupCached(hc);
                if (hd && hd->params.tutor_to_top) { return 15; }
            }
        }
        return 12;
    }
    if (p.tutor_to_battlefield_single)
    {
        // [8 vs 12] Natural Order (USER update, 2026-08-21): DEFAULT is LATE -- after the normal
        // creatures, just before Craterhoof ("That would avoid us sacrificing creatures early and
        // would allow us to drop a powerful Craterhoof with it"): the resolution-time sac-victim /
        // fetch census sees the finished board (measured: NO-before-fatty cost gi106 27->17 and
        // gi445 30->10 by starving the fetch census). The EARLY copy exists solely because "the
        // only reason I stuck Natural order in its current spot is to avoid random shuffles before
        // effects that deal with the top of the library" -- so it fires only when a mid-turn
        // Worldly Tutor stack is live (a tutor_to_top in HAND and a top-consumer to follow -- the
        // same census that gives the tutor rank 14): NO's shuffle must land BEFORE that stack, and
        // any cast after the tutor precedes the trailing consumers. No live mid-tutor -> no stack
        // to bury before rank 19 (a rank-24 tutor casts last, after us), so LATE is safe.
        for (const Card& hc : s.players[s.active_player_index].hand)
        {
            const CardDefinition* hd = CardDatabase::Instance().LookupCached(hc);
            if (hd == nullptr || !hd->params.tutor_to_top) { continue; }
            bool consumer = false;
            for (const Permanent& q : s.battlefield)
            {
                if (q.controller_index != s.active_player_index) { continue; }
                const CardDefinition* qd = CardDatabase::Instance().LookupCached(q.card);
                if (qd && (qd->params.activated_reveal_top_cost.has_value()
                           || qd->params.own_creature_enters_draw > 0)) { consumer = true; break; }
            }
            if (!consumer)
            {
                // Same census as the tutor's rank-14 test (incl. a hand Turntimber at 15) -- the
                // two MUST agree, else a {tutor, Turntimber, Natural Order} hand ranks the tutor
                // mid (14) while this sees "no consumer", goes late (19), and shuffles the stack.
                for (const Card& hc2 : s.players[s.active_player_index].hand)
                {
                    const CardDefinition* hd2 = CardDatabase::Instance().LookupCached(hc2);
                    if (hd2 && (hd2->params.activated_reveal_top_cost.has_value()
                                || hd2->params.own_creature_enters_draw > 0
                                || hd2->params.look_top_put_creature_count > 0)) { consumer = true; break; }
                }
            }
            if (consumer) { return 13; }   // early: shuffle before the live tutor stack
            break;
        }
        return 19;                         // late: full-census fetch, right before Craterhoof
    }
    if (p.etb_team_pump_per_creature)            { return 20; }  // [12] Craterhoof: last for maximum pump
    if (p.own_creature_enters_draw > 0
        && def.card.IsCreature())                { return 16; }  // [10] drawing creature (Vaultborn)
    if (p.upkeep_reorder > 0)                    { return 22; }  // [13] Mirri's Guile: irrelevant this turn
    if (def.card.IsCreature())                   { return 18; }  // [11] normal fatties: order doesn't matter
    return GenericProvider::CastOrderRank(s, def);
}

const char* StompyProvider::CastOrderTierName(int rank) const
{
    if (!StompyOrderEnabled()) { return nullptr; }
    switch (rank)
    {
        case 3:  return "1-MANA ELF: first (Sol Ring can't fund {G}; early elves feed the scalers)";
        case 5:  return "SOL RING: successfully funds almost everything else";
        case 6:  return "SCALING ELF, cheapest first (Priest): later elves still get the discount";
        case 7:  return "SCALING ELF (Archdruid): after the cheaper scaler";
        case 8:  return "CALL OF THE WILD: down before the cheat-outs (activations trail every cast)";
        case 12: return "TURNTIMBER SYMBIOSIS: cheat-out before the hand fatties";
        case 13: return "NATURAL ORDER (early): shuffle must land before the live tutor's stack";
        case 14: return "WORLDLY TUTOR (mid): a live top-consumer follows (Call activation / enters-draw)";
        case 15: return "TURNTIMBER SYMBIOSIS (post-tutor): a held tutor stacks first, then this consumes it";
        case 16: return "DRAWING CREATURE (Vaultborn): right after the tutor -- the draw IS the tutored card";
        case 18: return "FATTY: order doesn't matter";
        case 19: return "NATURAL ORDER (late): full board census -- don't sac early, fetch can be the hoof";
        case 20: return "CRATERHOOF: last for maximum pump";
        case 22: return "MIRRI'S GUILE: irrelevant for this turn";
        case 24: return "WORLDLY TUTOR (late): set next turn's upkeep/draw (upkeep Call = dump it for 4)";
        default: return nullptr;
    }
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
        const int lp = ComputeLordBonus(p.card, s, me, p.is_animated, &p).first;
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

// ---- KittyEquipment: the USER-reviewed cast order (review held 2026-08-19) -----------------------
//
// THE RULING, verbatim: "Puresteel Paladin, Stoneforge Mystic and equipment should be in front I
// think for the card draw/tutor effect. Swords and Unexpectedly are essentially unused in
// goldfish."
//
// What that buys, in card terms (params read from cards.json, not recalled):
//   * Puresteel Paladin FIRST of the three. draw_on_equipment_etb makes every Equipment that
//     enters AFTER it draw a card, and metalcraft_equip_zero_artifacts=3 makes every equip cost
//     {0} once three artifacts are out. Cast it after the equipment and BOTH halves are wasted --
//     the draws never happen and the equips are paid at printed cost.
//   * Stoneforge Mystic second. Its ETB tutor_to_hand(Equipment) puts a card in HAND, so it cannot
//     draw off itself -- it has to precede the equipment casts it feeds, and MTG_ACQ_RESOLVE
//     re-solves the phase at that acquisition so the tutored card is castable in the same line.
//   * Equipment third -- AHEAD OF THE REMAINING CREATURES, which is the part that differs from the
//     generic order (creatures 10, "other noncreature" 20). With a Paladin out a {1} Equipment IS
//     this deck's cantrip, so it belongs in front on the same information-first argument that puts
//     draws early everywhere else. With no Paladin out the move is a no-op: the equips are applied
//     after every cast either way, so cast order among triggerless spells cannot change the board.
//     That is why this is a FLAT rule and not a board-conditional promotion -- there is nothing to
//     gate, and a conditional rank would only make the report harder to review.
//   * Swords to Plowshares / Unexpectedly Absent LAST, and Main2. HONEST BRACKET: they are inert
//     here only because this goldfish has no blocking -- in a real game Swords on a blocker is
//     precisely an attack-enabler, so this ranking is right for the current apparatus and wrong for
//     the eventual real-opponent phase. UA's hand-cast is additionally pruned from the autonomous
//     search altogether (UnprunedGate::UACast), so its rank is inert today whatever it says.
//
// Sol Ring is deliberately NOT listed: it is not an Equipment, so it falls through to the generic
// MANA ROCK tier (5) and stays ahead of all of this -- which is what the ruling wants anyway, since
// its {C}{C} funds the very casts being pulled forward, and it is itself artifact #N for metalcraft.
// Colossus Hammer is deliberately NOT special-cased either: its equip {8} is unpayable without
// metalcraft / Balan's attach-all / a Skyhunter put, but it is still a fine ARTIFACT to cast (it
// advances metalcraft and Balan's two-equipment double-strike threshold), so it rides the
// equipment tier with the rest.
//
// MTG_KE_ORDER, DEFAULT OFF -> byte-identical. On adoption this becomes a default-on read with an
// off switch, like the other adopted per-deck orders.
static bool KittyOrderEnabled()
{
    static const bool on = EnvOn("MTG_KE_ORDER");
    return heurarm::Flag(heurarm::KE_ORDER, on);
}

int EquipmentProvider::CastOrderRank(const GameState& s, const CardDefinition& def) const
{
    if (!KittyOrderEnabled()) { return GenericProvider::CastOrderRank(s, def); }
    // Order within the ruling's own list. The three tests are disjoint on this deck: no card
    // carries two of them (Paladin is the only metalcraft/equipment-ETB watcher, Stoneforge the
    // only Equipment tutor, and neither is an Equipment).
    if (def.params.draw_on_equipment_etb
        || def.params.metalcraft_equip_zero_artifacts > 0)      { return 6; }
    if (def.params.tap_put_from_hand_cost.has_value()
        || (def.params.tutor_to_hand
            && std::find(def.params.tutor_types.begin(), def.params.tutor_types.end(),
                         std::string("Equipment")) != def.params.tutor_types.end())) { return 7; }
    if (def.params.is_equipment)                                { return 8; }
    if (def.tmpl == CardTemplate::Removal)                      { return 30; }
    return GenericProvider::CastOrderRank(s, def);   // hosts (Duelist / Kemba / Balan / Skyhunter) = 10
}

bool EquipmentProvider::OrderOpaqueCastsByRank() const
{
    return KittyOrderEnabled();
}

// Enumeration breadth for this deck -- the Mirrorwing lever (its EnumGroupCap 8) applied to the
// shape UseLethalShortCircuit's comment already names: Kemba cats + creatures + 5-8 equipment, whose
// {0} equips the mana bound cannot prune. MEASURED on the d5 tail game (seed 70001 gi=0), win turn 6
// in every arm: cap 12 (generic default) 2.854B odometer positions / 268 s; cap 8 2.305B / 267 s;
// cap 6 1.580B / 200 s; cap 4 1.146B / 146 s.
//
// The cap partly fights itself here: dropped groups come back as group-wave TRANCHES, so enumeration
// CALLS rise 47% as positions fall 2.5x. That machinery is load-bearing, not overhead -- cap 6 with
// MTG_GROUP_WAVES=0 cuts positions 7.3x but loses a turn on this same game (wt 6 -> 7).
//
// >>> MEASURED AND REJECTED as a tail fix (2026-08-20). DEFAULT OFF, and the reason is worth reading
// before anyone re-proposes it. The wall-clock reading above ("1.8x at cap 4") was taken on a box a
// second container had ~90% of, and it does not survive a deterministic meter:
//
//   d3 QUALITY (150 games/block, paired): train +0.0000, held-out +0.0000, 0 faster / 0 slower --
//     free, though it changes play in 44/150 and 53/150 games. So quality is not the objection.
//   d5 COST in work UNITS (seed-70001 block): TOTAL 21.04M -> 20.44M, a mere -2.9%, and game gi=4
//     goes 40,745 -> 404,865 units -- 9.9x WORSE at a BYTE-IDENTICAL play digest.
//
// The two meters disagree because each is blind to half of it: GameWorkMeter does not meter greedy
// Solve enumeration (the documented trap on this deck), so units cannot see the positions the cap
// removes; and the cap converts positions into more enumeration CALLS, which units DO count. A lever
// that trades one unmetered cost for a metered one, with a 9.9x regression on a game it does not
// otherwise change, is not a tail fix -- taming a tail is exactly the job that cannot tolerate a 10x
// outlier. The honest deterministic meter for enumeration work is the ODOMETER POSITION count
// (MTG_ENUM_STATS), not units and certainly not wall clock.
//
// The tail is rollout-bound anyway: 4.02M rollout calls and 6.91M simulated turn-steps against
// 231k interior nodes (96.8% rollout), with SolveUncached 30.4% of runtime. The lever for that
// shape is the VALUE LEAF, not enumeration breadth. See docs/design/analysis-KittyEquipment.md.
// MTG_KE_TUTOR_ALL -- MEASURED AND REJECTED 2026-08-21, kept as the record.
//
// The gap is real: 8 distinct Equipment names against a default width of 6, so two legal targets go
// unscored and which two is decided by library order. But it does not MATTER. Paired, 150 games per
// cell (logs/kitty_tutor): the wider axis changes the decision stream in exactly ONE game of 150 on
// each of train and held-out, moves the win turn in ZERO, and costs +12.3% / +13.5% search work
// (each extra target is a full rollout, and 26 tutor triggers per 96 games is enough to show up).
//
// The reason it cannot matter much is worth keeping: with Puresteel out every equip is {0}, so the
// fetched card's identity barely changes the turn -- and when no Puresteel is out, the cheap
// equipment the width-6 window already contains is what gets cast anyway. Do not re-widen this
// without a NEW argument; the coverage argument alone is measured empty.
static bool KittyTutorAllEnabled()
{
    static const bool on = EnvOn("MTG_KE_TUTOR_ALL");
    return heurarm::Flag(heurarm::KE_TUTOR_ALL, on);
}

static bool KittyTutorRankEnabled()
{
    static const bool on = EnvOn("MTG_KE_TUTOR_RANK");
    return heurarm::Flag(heurarm::KE_TUTOR_RANK, on);
}
static bool KittyTutorOneEnabled()
{
    static const bool on = EnvOn("MTG_KE_TUTOR_ONE");
    return heurarm::Flag(heurarm::KE_TUTOR_ONE, on);
}

// ---- Stoneforge Mystic's fetch: the REASONED ranking -------------------------------------------
//
// USER 2026-08-21: "We should be able to narrow the list to a small number and potentially only
// choose one most to all of the time in a heuristic."
//
// What motivates narrowing rather than widening: scoring all 8 targets instead of 6 changed the
// decision stream in ONE game of 150 per cell and moved ZERO win turns, at +12-13% search work. The
// targets past the top are not carrying anything, and each one costs a full rollout.
//
// THE ARGUMENT, which is what makes this doctrine-class rather than a blind cap. The fetched card is
// going to be equipped, so its value is decided by the EQUIP cost, not by the card:
//   * With a Puresteel Paladin out, metalcraft makes EVERY equip {0} -- this deck reaches three
//     artifacts almost immediately -- so the fetch is simply the biggest bonus there is. Colossus
//     Hammer's +10/+10 beats every other Equipment here by a factor of three, and its {8} equip, the
//     one thing that ever made it a bad fetch, is exactly what the Paladin cancels.
//   * With no Paladin out the {8} is real and the Hammer is a brick, so the fetch is the best body
//     whose equip we can actually pay. Grafted Wargear equips for {0} UNCONDITIONALLY at +3/+2 (its
//     sacrifice-on-unattach drawback is inert in a goldfish, where nothing unattaches it), strictly
//     beating Bonesplitter's +2 for {1}; Bonesplitter is the fallback once the 1-of Wargear is gone.
// The rest are dominated ON THIS DECK: Loxodon Warhammer and Shadowspear pay more for trample and
// lifelink a blockerless goldfish cannot use, O-Naginata's +3 adds a power-2 gate over the Wargear
// for no more bonus, Lightning Greaves' haste and shroud buy nothing on a board already attacking,
// and Umezawa's Jitte is +0 now for counters later in a race we are trying to end.
//
// Losslessness is MEASURED per game, never argued -- see docs/design/kitty-tutor-and-discard-
// heuristics.md. Both levers off -> the generic library-order list, byte-identical.
std::vector<std::string>
EquipmentProvider::TutorCandidates(const GameState& s, int controller, const CardParams& pp) const
{
    std::vector<std::string> all = GenericProvider::TutorCandidates(s, controller, pp);
    if (!KittyTutorRankEnabled() && !KittyTutorOneEnabled()) { return all; }

    bool paladin = false;
    for (const Permanent& p : s.battlefield)
    {
        if (p.controller_index != controller) { continue; }
        const CardDefinition* d = CardDatabase::Instance().LookupCached(p.card);
        if (d && d->params.draw_on_equipment_etb) { paladin = true; break; }
    }
    auto tier = [&](const std::string& n) -> int
    {
        if (paladin && n == "Colossus Hammer") { return 0; }
        if (n == "Grafted Wargear")            { return 1; }
        if (n == "Bonesplitter")               { return 2; }
        if (n == "Colossus Hammer")            { return 3; }   // no Paladin: the {8} is real
        return 4;
    };
    std::stable_sort(all.begin(), all.end(),
                     [&](const std::string& a, const std::string& b) { return tier(a) < tier(b); });
    return all;
}

int EquipmentProvider::TutorSearchWidth() const
{
    if (KittyTutorOneEnabled())  { return 1; }   // the heuristic alone
    if (KittyTutorRankEnabled()) { return 2; }   // heuristic + one searched alternative
    return KittyTutorAllEnabled() ? 8 : GenericProvider::TutorSearchWidth();
}

int EquipmentProvider::EnumGroupCap() const
{
    static const bool on = EnvOn("MTG_KE_GROUP_CAP");
    return heurarm::Flag(heurarm::KE_GROUP_CAP, on) ? 4 : GenericProvider::EnumGroupCap();
}

std::optional<DecisionProvider::MainPhase>
EquipmentProvider::MainPhaseOverride(const GameState&, const CardDefinition& def) const
{
    // "Swords and Unexpectedly are essentially unused in goldfish" (USER 2026-08-19). The base
    // classifier reaches removal through the DOUBT default, which keeps it pre-combat; rank 30
    // already puts it last within a phase, this puts it in the later phase too. See the honest
    // bracket above -- this is an apparatus-scoped ruling, not an MTG-general one.
    if (KittyOrderEnabled() && def.tmpl == CardTemplate::Removal) { return MainPhase::Main2; }
    return std::nullopt;
}

// ---- BUCKETED cleanup discard (USER design, 2026-08-22) ---------------------------------------
// The user's rule, verbatim: "bucket creatures (max 2, preferably Puresteel Paladin and Kor Duelist,
// or alternatively another enabler (Armored Skyhunter, Kemba, Stoneforge) and a doublestriker or
// Kemba if none are available), mana sources (up to 3-4 mana, no more than 3 sources and always keep
// sol ring) and equipment (at least 2 if not 3, preferring high-impact like Colossus Hammer if we
// have a way to cheat equip it and otherwise look for cheap equipment like bonesplitter and
// o-naginata)". Structurally the AntiLifegain bucketed rule: census the hand, fill each bucket to
// its cap counting what the BOARD already supplies, and shed the surplus worst-first. Omission from
// the returned order means KEEP (see CleanupDiscardRankingWithOrder), so a bucket is expressed by
// simply not naming the cards that fill it.
//
// WHAT "cheat equip" MEANS HERE, read off cards.json rather than memory. Colossus Hammer is {1} to
// cast and Equip {8} -- unpayable in a deck whose curve tops out around four mana -- so it is live
// only through a cost-BYPASS. The question this bucket asks is specifically about a Hammer IN HAND,
// and by that test the deck has exactly TWO bypasses:
//   Puresteel Paladin  metalcraft_equip_zero_artifacts=3  -- every equip becomes {0}
//   Balan              attach_all_equipment_cost={1}{W}   -- attaches EVERY Equipment, equip cost bypassed
// USER, 2026-08-22: "Armored Skyhunter doesn't help for equipping equipment in hand" -- correct, and
// the card confirms it. Its attach applies to an Equipment put from "the top six cards of your
// LIBRARY"; a card sitting in hand is not among them, so it cheats nothing this bucket is deciding
// about. Stoneforge is out for the mirror-image reason: its put "dodges only the CAST cost --
// equipping is still the Equip action / metalcraft" (cards.json), and the Hammer's cast is the {1}
// that was never the problem. Both remain valid ENABLERS in the creature bucket; neither is a
// cheat-equipper. Without one of the two real bypasses the Hammer is the deck's deadest card in hand
// and sheds first.
//
// O-Naginata carries equip_min_power=3, and the only power-3 bodies here are Balan and Skyhunter --
// so on a Kor Duelist / Puresteel board it is live only AFTER a Bonesplitter (+2/+0) pumps the host
// over the line. It is therefore ranked as cheap-but-conditional rather than alongside Bonesplitter.
//
// REMOVAL IS IN NO BUCKET, so it sheds first -- which is the user's own standing ruling on these two
// cards ("Swords and Unexpectedly are essentially unused in goldfish", 2026-08-19) and what the
// deck's own profile says: Swords to Plowshares scores -0.5016, the worst card in the deck, and
// Unexpectedly Absent -0.1796.
//
// EXPECTATIONS, stated up front because they bound what this can be worth: the cleanup discard
// almost never fires in PLAY (measured: 1 real shed per 120 games with mulligans on, 9 with them
// forced off). Its denominator is the ROLLOUT -- 425/game normal, 591/game forced-keep -- and above
// all the mulligan generator, where a land-light hand cannot deploy and so crosses seven cards every
// turn. This is a rollout-model and keep-generation rule, not a play rule, and it should be measured
// on that workload. See docs/design/kitty-tutor-and-discard-heuristics.md.
static bool KittyBucketDiscardEnabled()
{
    static const bool env = EnvOn("MTG_KE_BUCKET_DISCARD", true);
    return heurarm::Flag(heurarm::KE_BUCKET_DISCARD, env);
}

// Target hand size after a cleanup discard (CR 514.1). The USER's allocation is stated against it
// directly -- 2 creatures + 3 mana sources + 2 equipment = 7 -- so the residual below is literally
// "what is left of the hand".
static constexpr int kHandTarget = 7;

// The mana the hand is trying to REACH, and the cap on how many source CARDS it spends doing so.
// USER: "4 mana is pretty much the cap, so we don't need to go higher than that" -- and the curve
// agrees, topping out at Balan {2}{W}{W} and Armored Skyhunter {3}{W}.
static constexpr int kManaTarget     = 4;
static constexpr int kMaxHandSources = 3;

// ...and the COLOUR floor, which is the other half of the same user note: "the bounceland also counts
// as 2 sources, though not in combination with just sol ring" (2026-08-22). Sol Ring produces {C}{C}
// and Boros Garrison produces {R}{W} -- one white between them -- so that pair reaches four mana and
// still cannot cast Puresteel {W}{W}, Kemba {1}{W}{W} or Balan {2}{W}{W}, which are the deck's only
// two-pip costs and three of its five creatures. Reaching the mana target is therefore not on its own
// a reason to stop keeping lands.
static constexpr int kMinWhiteSources = 2;

// A mana source that can actually pay a {W} pip. Read off `produces` rather than assumed from the
// deck being mono-white: Sol Ring is colourless and the Garrison's other pip is red.
static bool ProducesWhite(const CardDefinition& d)
{
    for (Color c : d.params.produces) { if (c == Color::White) { return true; } }
    return false;
}

// EQUIPMENT AS THE RESIDUAL, separable so a sweep can attribute it rather than measuring it folded
// into the buckets and guessing which half moved. Off = the flat cap of 3 this rule carried before
// the allocation was settled. See the ALLOCATION block for the argument.
static bool KittyDiscardResidualEnabled()
{
    static const bool env = EnvOn("MTG_KE_DISCARD_RESIDUAL", true);
    return heurarm::Flag(heurarm::KE_DISCARD_RESIDUAL, env);
}

std::vector<int> EquipmentProvider::CleanupDiscardCandidates(
    const GameState& s, const std::vector<std::string>* required_pieces) const
{
    if (!KittyBucketDiscardEnabled())
    { return GenericProvider::CleanupDiscardCandidates(s, required_pieces); }

    const Player& ap = s.players[s.active_player_index];
    const int n = static_cast<int>(ap.hand.size());
    if (n == 0) { return {}; }
    auto def_of = [](const Card& c) { return CardDatabase::Instance().LookupCached(c); };

    // ---- Role tables. Lower rank = kept sooner. ------------------------------------------------
    // Enablers: the user named Puresteel first and then "another enabler (Armored Skyhunter, Kemba,
    // Stoneforge)" without ordering the alternatives. Ordered here by CASTABILITY, because the hands
    // that reach a discard at all are the land-light ones: Stoneforge {1}{W} < Kemba {1}{W}{W} <
    // Skyhunter {3}{W}. Flagged as a judgement call the spec did not settle.
    auto enabler_rank = [](const std::string& nm) -> int {
        if (nm == "Puresteel Paladin")  { return 0; }
        if (nm == "Stoneforge Mystic")  { return 1; }
        if (nm == "Kemba, Kha Regent")  { return 2; }
        if (nm == "Armored Skyhunter")  { return 3; }
        return -1;
    };
    // Double strikers, cheapest first: Kor Duelist {W} (double_strike_while_equipped) then Balan
    // {2}{W}{W} (double_strike_min_equipment=2).
    auto ds_rank = [](const std::string& nm) -> int {
        if (nm == "Kor Duelist")             { return 0; }
        if (nm == "Balan, Wandering Knight") { return 1; }
        return -1;
    };
    // Bypasses the EQUIP cost for a card in HAND -- see the note above for why Skyhunter (library
    // dig) and Stoneforge (cast cost only) are deliberately not here.
    auto is_cheat_equipper = [](const std::string& nm) {
        return nm == "Puresteel Paladin" || nm == "Balan, Wandering Knight";
    };

    // ---- What the BOARD already supplies. A role filled on board needs no hand copy. ------------
    int  board_mana = 0, board_white = 0, board_lands = 0;
    bool board_enabler = false, board_ds = false, board_kemba = false, board_cheat = false;
    for (const Permanent& p : s.battlefield)
    {
        if (p.controller_index != s.active_player_index) { continue; }
        const CardDefinition* d = def_of(p.card);
        if (d == nullptr) { continue; }
        const std::string& nm = p.card.m_name.str();
        // MANA, not source count -- a Sol Ring or a Boros Garrison already in play taps for two, and
        // the target below is denominated in mana. (Garrison's second pip is red in a mono-white
        // deck, but every generic cost in the deck can spend it, starting with Colossus Hammer {1}.)
        if (d->card.IsLand() || d->params.mana_rock)
        {
            board_mana += std::max(1, d->params.produces_amount);
            if (ProducesWhite(*d)) { ++board_white; }
            if (d->card.IsLand())  { ++board_lands; }   // what a Karoo's ETB can bounce
        }
        if (enabler_rank(nm) >= 0)   { board_enabler = true; }
        if (ds_rank(nm) >= 0)        { board_ds = true; }
        if (nm == "Kemba, Kha Regent") { board_kemba = true; }
        if (is_cheat_equipper(nm))   { board_cheat = true; }
    }

    // ---- Hand census ---------------------------------------------------------------------------
    struct Ent { int idx; int rank; };
    std::vector<Ent> enablers, strikers, kembas, sources, equipment;
    std::vector<int> unbucketed, sol_rings;
    for (int i = 0; i < n; ++i)
    {
        const Card& c = ap.hand[i];
        if (c.m_is_staged) { continue; }          // in EXILE, never sheddable
        const CardDefinition* d = def_of(c);
        if (d == nullptr) { unbucketed.push_back(i); continue; }
        const std::string& nm = c.m_name.str();
        if (nm == "Sol Ring")            { sol_rings.push_back(i); continue; }   // never shed
        if (d->card.IsLand())            { sources.push_back({ i, nm == "Plains" ? 0 : 1 }); continue; }
        if (d->params.mana_rock)         { sources.push_back({ i, 1 }); continue; }
        const int er = enabler_rank(nm), dr = ds_rank(nm);
        if (er >= 0)                     { enablers.push_back({ i, er }); }
        if (dr >= 0)                     { strikers.push_back({ i, dr }); }
        if (nm == "Kemba, Kha Regent")   { kembas.push_back({ i, 0 }); }
        if (er >= 0 || dr >= 0)          { continue; }
        if (d->params.is_equipment)      { equipment.push_back({ i, 0 }); continue; }
        unbucketed.push_back(i);         // removal (Swords / Unexpectedly Absent): no bucket
    }
    auto by_rank = [](const Ent& a, const Ent& b)
    { return a.rank != b.rank ? a.rank < b.rank : a.idx < b.idx; };
    std::stable_sort(enablers.begin(), enablers.end(), by_rank);
    std::stable_sort(strikers.begin(), strikers.end(), by_rank);
    std::stable_sort(sources.begin(), sources.end(), by_rank);

    // ---- The ALLOCATION (USER, 2026-08-22) -----------------------------------------------------
    // "3 mana sources at most and 2 equipment. If some of these are not necessary or can't be filled
    // we keep more equipment." With the creature bucket that is 2 + 3 + 2 = exactly a seven-card
    // hand, and EQUIPMENT IS THE RESIDUAL: every slot the other buckets do not need (the role is
    // already on the battlefield) or cannot fill (the card is not in hand) flows to it.
    //
    // That single rule subsumes what I had written as a separate "land skew". Fewer lands in hand
    // means the source bucket under-fills, so equipment absorbs the slack -- which is right for the
    // curve, since every creature here costs two to four (Kor Duelist {W}, Puresteel {W}{W},
    // Stoneforge {1}{W}, Kemba {1}{W}{W}, Balan {2}{W}{W}, Skyhunter {3}{W}) while almost every
    // Equipment costs {1}. And "count what is on board already" is the same mechanism seen from the
    // other side: three Plains in play zeroes the source bucket, and an enabler in play zeroes half
    // the creature bucket, and in both cases the freed slots become equipment.
    //
    // The creature bucket is NOT squeezed by any of this. USER: "You keep 1 of each" -- one enabler
    // and one double striker, as in the original rule. An earlier cut of mine reduced it to a single
    // creature on a land-light hand, which split exactly the Puresteel + Kor Duelist pair the spec
    // names first; that pair is three mana across two turns and is the deck's engine.
    std::vector<char> kept(static_cast<std::size_t>(n), 0);

    // ---- Bucket 1: mana sources -- fill to kManaTarget, never more than kMaxHandSources cards ----
    // The spec's "up to 3-4 mana, no more than 3 sources" is TWO constraints, and collapsing them
    // into one count was wrong. USER 2026-08-22: "in the rarer case where we have 2-3 [mana] out
    // already you might want to keep 1-2 mana sources, so you can hit 4 mana. 4 mana is pretty much
    // the cap, so we don't need to go higher than that." Counting the board against a flat 3-CARD cap
    // kept ZERO lands behind three in play, which strands Balan {2}{W}{W} and Skyhunter {3}{W} -- the
    // top of the curve and the reason the target is four.
    //
    // Sol Ring is excluded from `sources` and never shed; it counts as one CARD against the cap and
    // two MANA against the target, which is exactly what makes it the best source in the deck.
    // Sol Ring is one CARD but two MANA and -- the point of the colour clause below -- ZERO white.
    int mana_outlook = board_mana + 2 * static_cast<int>(sol_rings.size());
    int white_kept   = board_white;
    int sources_kept = static_cast<int>(sol_rings.size());
    int lands_avail  = board_lands;
    for (const Ent& e : sources)
    {
        if (sources_kept >= kMaxHandSources) { break; }
        if (mana_outlook >= kManaTarget && white_kept >= kMinWhiteSources) { break; }
        const CardDefinition* d = def_of(ap.hand[e.idx]);
        // A Karoo CANNOT BE PLAYED as your only land. USER 2026-08-22: "the Garrison cannot be played
        // without a plains." Its ETB returns a land you control, and with no other land that is
        // itself, so the drop is simply wasted -- which is why LandPlay's drop chooser refuses to
        // offer it in this state (`etb_bounce_land && !has_other_land`). Crediting it here would keep
        // a card the engine will never play, so it is passed over and falls through to the shed list.
        // `sources` ranks plain lands first, so any Plains this hand holds has already been counted.
        const bool bounce = d != nullptr && d->params.etb_bounce_land;
        if (bounce && lands_avail == 0) { continue; }
        kept[e.idx] = 1;
        ++sources_kept;
        // USER: "technically the bounceland also counts as 2 sources". Once it IS playable it taps
        // for two, so it is two mana toward the target off one card -- the credit the board gives it.
        mana_outlook += d != nullptr ? std::max(1, d->params.produces_amount) : 1;
        if (d != nullptr && ProducesWhite(*d)) { ++white_kept; }
        if (!bounce) { ++lands_avail; }   // a Karoo nets no land: it returns one as it enters
    }

    // ---- Bucket 2: creatures = ONE enabler + ONE double striker (Kemba if none) -----------------
    // A role already covered on the battlefield is not kept again in hand; that is a freed slot, and
    // it goes to equipment below.
    int creatures_kept = 0;
    int kept_enabler   = -1;
    if (!board_enabler && !enablers.empty())
    { kept_enabler = enablers.front().idx; kept[kept_enabler] = 1; ++creatures_kept; }
    if (!board_ds)
    {
        int pick = -1;
        for (const Ent& e : strikers) { if (!kept[e.idx]) { pick = e.idx; break; } }
        // "...or Kemba if none are available": the fallback is a Kemba, not a second enabler.
        if (pick < 0 && !board_kemba)
        { for (const Ent& e : kembas) { if (!kept[e.idx]) { pick = e.idx; break; } } }
        if (pick >= 0) { kept[pick] = 1; ++creatures_kept; }
    }

    // ---- Bucket 3: equipment = the RESIDUAL, a floor of 2 ---------------------------------------
    // The Hammer's rank is CONDITIONAL on a cost-bypass being secured -- on board, or kept just
    // above by the creature bucket. Without one it is dead weight and ranks last of all equipment.
    const bool cheat_secured = board_cheat
        || (kept_enabler >= 0 && is_cheat_equipper(ap.hand[kept_enabler].m_name.str()))
        || [&] { for (int i = 0; i < n; ++i)
                 { if (kept[i] && is_cheat_equipper(ap.hand[i].m_name.str())) { return true; } }
                 return false; }();
    auto equip_rank = [&](const std::string& nm) -> int {
        if (nm == "Colossus Hammer") { return cheat_secured ? 0 : 99; }   // +10/+10 or a dead card
        if (nm == "Bonesplitter")    { return 1; }   // {1} cast, {1} equip -- the cheap default
        if (nm == "O-Naginata")      { return 2; }   // {1} cast, {2} equip, needs a power-3 host
        if (nm == "Shadowspear")     { return 3; }
        if (nm == "Umezawa's Jitte") { return 4; }
        if (nm == "Lightning Greaves") { return 5; }
        if (nm == "Grafted Wargear") { return 6; }
        if (nm == "Loxodon Warhammer") { return 7; }
        return 8;
    };
    for (Ent& e : equipment) { e.rank = equip_rank(ap.hand[e.idx].m_name.str()); }
    std::stable_sort(equipment.begin(), equipment.end(), by_rank);
    // The residual: whatever the seven-card hand has left once the other two buckets have taken what
    // they need, never fewer than the two the spec asks for outright. With the residual disabled the
    // bucket is a flat 3, which is the shape this rule had before the allocation was settled -- kept
    // as the A/B arm so a sweep can attribute the residual rather than measure it folded in.
    int equip_budget = KittyDiscardResidualEnabled()
                     ? std::max(2, kHandTarget - creatures_kept - sources_kept)
                     : 3;
    for (const Ent& e : equipment)
    {
        if (equip_budget <= 0) { break; }
        kept[e.idx] = 1; --equip_budget;
    }

    // ---- Shed order: everything not kept, worst first -------------------------------------------
    // Removal leads (no bucket at all), then surplus equipment worst-first, then surplus creatures,
    // then surplus sources. Sources go LAST of the surplus because a land-light hand is the one that
    // reaches a discard, and the deck's own card_scores put a 23rd mana source above a fourth copy
    // of an Equipment it cannot equip.
    std::vector<int> shed;
    for (int i : unbucketed) { if (!kept[i]) { shed.push_back(i); } }
    for (auto it = equipment.rbegin(); it != equipment.rend(); ++it)
    { if (!kept[it->idx]) { shed.push_back(it->idx); } }
    for (auto it = enablers.rbegin(); it != enablers.rend(); ++it)
    { if (!kept[it->idx]) { shed.push_back(it->idx); } }
    for (auto it = strikers.rbegin(); it != strikers.rend(); ++it)
    { if (!kept[it->idx]) { shed.push_back(it->idx); } }
    for (auto it = sources.rbegin(); it != sources.rend(); ++it)
    { if (!kept[it->idx]) { shed.push_back(it->idx); } }
    // Dedup (a Kemba can sit in both the enabler and Kemba lists) while preserving order.
    std::vector<char> seen(static_cast<std::size_t>(n), 0);
    std::vector<int> order;
    for (int i : shed)
    { if (!seen[i]) { seen[i] = 1; order.push_back(i); } }
    // The base ranking applies required-piece protection and the staged exemption to this order, and
    // appends everything unnamed behind it -- so a hand that is ENTIRELY buckets still sheds
    // something rather than returning empty and stalling the cleanup loop.
    return CleanupDiscardRankingWithOrder(s, required_pieces, order);
}

const char* EquipmentProvider::CastOrderTierName(int rank) const
{
    if (!KittyOrderEnabled()) { return nullptr; }
    switch (rank)
    {
        case 6:  return "ENGINE (Puresteel): every Equipment after it DRAWS, and 3 artifacts make equip {0}";
        case 7:  return "TUTOR (Stoneforge): its ETB puts an Equipment in HAND, so it must precede the casts it feeds";
        case 8:  return "EQUIPMENT: in front of the hosts -- with a Paladin out each one is a cantrip";
        case 30: return "GOLDFISH-INERT REMOVAL: last, and m2 (no blocking in this apparatus)";
        default: return nullptr;
    }
}
