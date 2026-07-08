#include <iostream>
#include <fstream>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <random>
#include <algorithm>
#include <vector>
#include <thread>
#include <sstream>
#include <cstdlib>
#include "deck/DeckLoader.h"
#include "cards/CardDatabase.h"
#include "runner/GoldFishRunner.h"
#include "runner/BatchRunner.h"
#include "ai/AIEngine.h"
#include "ai/TurnSolver.h"
#include "core/GameEngine.h"
#include "core/GameLogger.h"
#include "core/SpellEffects.h"   // LookKind / TopDisposition / EnumerateTopDispositions for look-top decisions
#include "core/HardwareConcurrency.h"
#include "ai/MulliganProfileIO.h"
#include "ai/Profiler.h"
#include "ai/DecisionProviders.h"   // SelectDecisionProvider for --scenario
#include <nlohmann/json.hpp>        // --scenario board spec

static void PrintUsage(const char* prog)
{
    std::cerr << "Usage: " << prog
              << " <deckfile> [--games N] [--seed S] [--max-turns T]"
                 " [--depth D] [--budget-ms M] [--profile path] [--log-dir path] [--cards-json path]\n"
              << "  <deckfile>      Plain text (.txt) or Cockatrice (.cod) decklist\n"
              << "  --games N       Number of games to simulate (default: 10000)\n"
              << "  --seed S        Base RNG seed (omit to generate randomly)\n"
              << "  --max-turns T   Maximum turns before declaring no-win (default: 20)\n"
              << "  --depth D       Lookahead depth (default: 0; higher = stronger but slower)\n"
              << "  --budget-ms M   Per-decision search budget in deterministic 'virtual ms';\n"
              << "                  0 = unlimited (default: 0). Alias: --timeout-ms\n"
              << "  --threads N     Worker threads (default: 0 = auto, affinity-based CPU count)\n"
              << "  --profile P     Path to a .profile.json file (default: auto-detect deckname.profile.json)\n"
              << "  --log-dir P     Write one JSON game log per game into this directory\n"
              << "  --cards-json P  Path to card definitions JSON (default: src/cards/data/cards.json)\n";
}

static std::vector<std::string> SortedHandNames(GameState& state)
{
    std::vector<std::string> names;
    for (const Card& c : state.ActivePlayer().hand) { names.push_back(c.m_name); }
    std::sort(names.begin(), names.end());
    return names;
}

// ---- Claude-play / human-play prototype (opt-in; --claude-play) ---------------
// An external decision provider drives the goldfish's MAIN phases (combat + cleanup
// stay on the engine heuristics). Stateless-replay protocol: each process run replays
// the deterministic game (fixed seed + game-index) applying the pre-supplied --choices,
// and when it reaches the first un-chosen main-phase decision it prints that decision
// (current legal info + the enumerated legal plans) and exits with code 70. The driver
// (a Claude agent) reads it, appends a plan index, and re-invokes. When every decision
// is supplied the game finishes and the result (win turn) is printed. Purpose: a flag-
// generating verification sweep -- a game Claude wins earlier than the AI, or a plan
// set that looks wrong, is a flag for the analyzer's convergence loop to investigate.
static void JsonStr(std::ostream& os, const std::string& s)
{
    os << '"';
    for (char c : s)
    {
        if (c == '"' || c == '\\') { os << '\\' << c; }
        else                        { os << c; }
    }
    os << '"';
}

static void JsonNameArray(std::ostream& os, const std::vector<std::string>& names)
{
    os << '[';
    for (size_t i = 0; i < names.size(); ++i)
    {
        if (i) { os << ", "; }
        JsonStr(os, names[i]);
    }
    os << ']';
}

// Battlefield as {name, is_land} objects so the GUI classifies lands by the REAL card type
// (Permanent::card.IsLand) instead of a fragile name regex (e.g. Blood Crypt is a land but
// matches no obvious land suffix). Sorted by name for a stable render.
static void JsonBattlefield(std::ostream& os, const GameState& s, int controller)
{
    // One displayable counter group on a permanent: a kind (drives the GUI badge colour),
    // a human label (tooltip), and the count. A permanent may carry several kinds at once
    // (e.g. a depletion land that also caught a +1/+1), hence a vector.
    struct Cnt { const char* kind; const char* label; int count; };
    struct Row { std::string name; bool is_land; bool is_le; std::vector<Cnt> counters; int idx; bool tapped; };
    std::vector<Row> rows;
    for (int pi = 0; pi < static_cast<int>(s.battlefield.size()); ++pi)
    {
        const Permanent& p = s.battlefield[pi];
        if (p.controller_index != controller) { continue; }
        // is_le: this permanent is a "discard a land: deal N" outlet (Land's Edge) -> the GUI
        // makes it the clickable SOURCE for the discard-to-Land's-Edge activation.
        const CardDefinition* d = CardDatabase::Instance().LookupCached(p.card);
        bool is_le = d && d->params.discard_land_damage > 0;
        // Aggregate counters by kind so the GUI can draw one badge per kind (with a count).
        int p1 = 0, m1 = 0, loy = 0, poi = 0, dep = 0;
        for (const Counter& ct : p.counters)
        {
            switch (ct.type)
            {
                case Counter::Type::PlusOnePlusOne:   p1  += ct.count; break;
                case Counter::Type::MinusOneMinusOne: m1  += ct.count; break;
                case Counter::Type::Loyalty:          loy += ct.count; break;
                case Counter::Type::Poison:           poi += ct.count; break;
                case Counter::Type::Depletion:        dep += ct.count; break;
            }
        }
        std::vector<Cnt> cs;
        if (p1)                 { cs.push_back({ "p1p1",      "+1/+1",     p1 }); }
        if (m1)                 { cs.push_back({ "m1m1",      "-1/-1",     m1 }); }
        if (loy)                { cs.push_back({ "loyalty",   "loyalty",   loy }); }
        if (poi)                { cs.push_back({ "poison",    "poison",    poi }); }
        if (dep)                { cs.push_back({ "depletion", "depletion", dep }); }
        if (p.charge_counters)  { cs.push_back({ "charge",    "charge",    p.charge_counters }); }
        if (p.verse_counters)   { cs.push_back({ "verse",     "verse",     p.verse_counters }); }
        rows.push_back({ p.card.m_name, p.card.IsLand(), is_le, std::move(cs), pi, p.tapped });
    }
    std::sort(rows.begin(), rows.end(),
              [](const Row& a, const Row& b){ return a.name < b.name; });
    os << '[';
    for (size_t i = 0; i < rows.size(); ++i)
    {
        if (i) { os << ", "; }
        os << "{ \"name\": "; JsonStr(os, rows[i].name);
        os << ", \"idx\": " << rows[i].idx;
        if (rows[i].tapped) { os << ", \"tapped\": true"; }
        os << ", \"is_land\": " << (rows[i].is_land ? "true" : "false");
        if (rows[i].is_le) { os << ", \"is_le\": true"; }
        if (!rows[i].counters.empty())
        {
            os << ", \"counters\": [";
            for (size_t j = 0; j < rows[i].counters.size(); ++j)
            {
                if (j) { os << ", "; }
                const Cnt& c = rows[i].counters[j];
                os << "{ \"kind\": \"" << c.kind << "\", \"label\": \"" << c.label
                   << "\", \"count\": " << c.count << " }";
            }
            os << "]";
        }
        os << " }";
    }
    os << ']';
}

// Card category for the play palette / on-field plan rendering. NB "spell" is avoided on purpose:
// in MTG every non-land cast is a spell, so it's ambiguous. The useful split for the board is
// where the card ends up -- a land plays as the land drop; an instant/sorcery is NON-PERMANENT
// (won't stay on the battlefield, shown set apart); everything else casts into a permanent.
static const char* HandKind(const CardDefinition* d)
{
    if (!d) { return "permanent"; }
    if (d->card.IsLand()) { return "land"; }
    if (d->card.IsInstant() || d->card.IsSorcery()) { return "nonpermanent"; }
    return "permanent";
}

// A draw-engine card (Treasure Hunt, Ponder, ...) -- casting it draws, which triggers a
// human-play breakpoint (the chooser re-fires so you re-decide with the revealed cards). The
// GUI flags these so you know to commit them WITHOUT a land if you want to play a revealed one.
static bool HandIsDraw(const CardDefinition* d)
{
    if (!d) { return false; }
    return d->tmpl == CardTemplate::DrawSpell
        || d->tmpl == CardTemplate::DrawX
        || d->tmpl == CardTemplate::DrawUntilNonland;
}

// One-line human-readable summary of a candidate plan (land drop + casts).
static std::string SummarizePlan(const TurnSolver::Plan& plan)
{
    std::ostringstream os;
    // Pure dig line (human play): cycle a land / sacrifice Fiery Islet to draw -- show just the
    // ability, not "land=none; cast: ...", since a dig spends no land drop and casts nothing.
    if (plan.actions.size() == 1 && plan.actions[0].kind == Action::Kind::DigDraw
        && plan.land_to_play.empty())
    {
        const Action& a = plan.actions[0];
        return (a.dig_sacrifice ? "sacrifice " : "cycle ") + a.card_name + " to draw";
    }
    if (plan.land_decided && !plan.land_to_play.empty()) { os << "land=" << plan.land_to_play << "; "; }
    else if (plan.land_decided)                           { os << "land=none; "; }
    std::vector<std::string> casts;
    for (const Action& a : plan.actions)
    {
        std::string tag;
        switch (a.kind)
        {
            case Action::Kind::CastFromHand:      tag = a.card_name; break;
            case Action::Kind::CastFromGraveyard: tag = a.card_name + " (retrace)"; break;
            case Action::Kind::ActivateVial:      tag = a.card_name + " (vial)"; break;
            case Action::Kind::PlayLand:          tag = a.card_name + " (land)"; break;
            case Action::Kind::DigDraw:
                tag = (a.dig_sacrifice ? "sacrifice " : "cycle ") + a.card_name + " to draw"; break;
            default:                              tag = a.card_name + " (other)"; break;
        }
        if (a.sacrifice_land) { tag += " +sac-land"; }
        if (a.discard_lands)  { tag += " +discard" + std::to_string(a.discard_lands); }
        casts.push_back(tag);
    }
    if (casts.empty()) { os << "cast: (nothing)"; }
    else
    {
        os << "cast: ";
        for (size_t i = 0; i < casts.size(); ++i) { if (i) os << ", "; os << casts[i]; }
    }
    return os.str();
}

// Writes the decision as a JSON object (no markers) to `os`. Used for both the live
// stdout dump (wrapped in <<<CLAUDE_DECISION>>> markers by the caller) and the per-game
// trace log written on game completion (--log-dir).
// Emit the shared "me" + "opponent" board/hand context (battlefield, hand, graveyard, life,
// library size, vial counters, floating mana). Used by the main-phase decision AND every
// sub-decision (scry/surveil/reorder/target/bounce) so the GUI always has the full board to render
// and highlight instead of blanking it. reveal_count > 0 appends the next draws (--reveal). Emits
// two top-level keys ("me": {...}, "opponent": {...}) each followed by a trailing comma+newline.
static void WriteBoardContext(std::ostream& os, const GameState& s, int reveal_count)
{
    const Player& me  = s.ActivePlayer();
    int           opp = 1 - s.active_player_index;
    // Iterate hand Cards (not just names) so per-instance flags (m_is_staged / expiry) survive.
    std::vector<const Card*> hand;
    for (const Card& c : me.hand) { hand.push_back(&c); }
    std::sort(hand.begin(), hand.end(),
              [](const Card* a, const Card* b){ return a->m_name.str() < b->m_name.str(); });
    std::vector<std::string> gy;
    for (const Card& c : me.graveyard) { gy.push_back(c.m_name); }
    std::sort(gy.begin(), gy.end());

    os << "  \"me\": { \"life\": " << me.life << ", \"battlefield\": ";
    JsonBattlefield(os, s, s.active_player_index);
    // Aether Vial charge counters (a Vial deploys a creature whose MV EQUALS its
    // counters) — exposed so the player needn't guess the Vial's state.
    {
        std::vector<int> vials;
        for (const Permanent& p : s.battlefield)
        {
            if (p.controller_index != s.active_player_index) { continue; }
            const CardDefinition* d = CardDatabase::Instance().LookupCached(p.card);
            if (d && d->params.upkeep_adds_charge) { vials.push_back(p.charge_counters); }
        }
        os << ", \"vial_counters\": [";
        for (size_t i = 0; i < vials.size(); ++i) { if (i) os << ", "; os << vials[i]; }
        os << "]";
    }
    // Hand as {name, cost, mv} objects so the player judges affordability from the real
    // card data, not memory (the slivers false positives came from guessing costs).
    os << ", \"hand\": [";
    for (size_t i = 0; i < hand.size(); ++i)
    {
        if (i) { os << ", "; }
        const Card* hc = hand[i];
        const CardDefinition* d = CardDatabase::Instance().Lookup(hc->m_name);
        os << "{ \"num\": " << hc->m_number << ", \"name\": "; JsonStr(os, hc->m_name);
        os << ", \"cost\": "; JsonStr(os, d ? d->card.m_mana_cost.ToString() : std::string());
        os << ", \"mv\": " << (d ? d->card.m_mana_cost.ManaValue() : 0);
        os << ", \"kind\": \"" << HandKind(d) << "\"";
        if (HandIsDraw(d)) { os << ", \"is_draw\": true"; }
        // is_creature lets the GUI offer an Aether Vial deploy (a creature whose MV equals a
        // Vial's charge counters can be put onto the battlefield for free).
        if (d && d->card.IsCreature()) { os << ", \"is_creature\": true"; }
        // Staged (exiled-but-playable) cards live in hand with m_is_staged set; surface that so
        // the GUI sets them apart (Light Up the Stage / Soulfire Eruption / Expressive Iteration).
        if (hc->m_is_staged) { os << ", \"is_staged\": true, \"staged_until\": " << hc->m_staged_expiry; }
        os << " }";
    }
    os << "]";
    os << ", \"graveyard\": "; JsonNameArray(os, gy);
    // Retrace: graveyard spells castable from the yard (pay cost + discard a land). The GUI makes
    // these clickable in the graveyard zone; absent when the yard holds no retrace card.
    {
        std::vector<std::string> rt;
        for (const Card& c : me.graveyard)
        {
            const CardDefinition* d = CardDatabase::Instance().LookupCached(c);
            if (d && d->params.retrace) { rt.push_back(c.m_name); }
        }
        if (!rt.empty()) { std::sort(rt.begin(), rt.end()); os << ", \"retrace_gy\": "; JsonNameArray(os, rt); }
    }
    os << ", \"library_size\": " << me.library.size();
    // Floating (unspent) mana in the pool. Usually empty at a main-phase breakpoint (the pool is
    // cleared at turn start and a line is committed atomically), so emit only when non-empty.
    {
        const ManaPool& fm = s.floating_mana;
        if (fm.Total() > 0)
        {
            os << ", \"floating_mana\": {";
            bool first = true;
            auto emit = [&](const char* sym, int n) {
                if (n > 0) { if (!first) { os << ", "; } first = false; os << "\"" << sym << "\": " << n; }
            };
            emit("W", fm.white); emit("U", fm.blue); emit("B", fm.black); emit("R", fm.red);
            emit("G", fm.green); emit("C", fm.colorless); emit("wild", fm.wild);
            os << "}";
        }
    }
    if (reveal_count > 0)
    {
        // Optional partial clairvoyance (--reveal N): the next N draws, in draw order
        // (library top = index 0). A small "accessible part" of the library, not the
        // whole thing -- enough foresight to plan a line without full clairvoyance.
        int n = std::min(reveal_count, static_cast<int>(me.library.size()));
        std::vector<std::string> up;
        for (int k = 0; k < n; ++k) { up.push_back(me.library[k].m_name); }
        os << ", \"upcoming_draws\": ";
        JsonNameArray(os, up);
    }
    os << " },\n";
    os << "  \"opponent\": { \"life\": " << s.players[opp].life << ", \"battlefield\": ";
    JsonBattlefield(os, s, opp);
    os << " },\n";
}

static void WriteDecisionJson(std::ostream& os, const GameState& s,
                              const std::vector<TurnSolver::Plan>& plans,
                              bool is_pre_combat, int decision_index, int reveal_count,
                              const std::vector<std::pair<int, std::string>>& drew = {},
                              const std::vector<PlayEvent>& events = {})
{
    const Player& me  = s.ActivePlayer();
    os << "{\n";
    os << "  \"decision_index\": " << decision_index << ",\n";
    os << "  \"type\": \"main_phase\",\n";
    os << "  \"turn\": " << s.turn_number << ",\n";
    os << "  \"phase\": \"" << (is_pre_combat ? "pre_main" : "post_main") << "\",\n";
    os << "  \"on_the_play\": " << (s.on_the_play ? "true" : "false") << ",\n";
    // Cards drawn since the previous main-phase decision (turn draw + any cantrip draws this
    // segment), each with the turn it was drawn on, so the viewer history can show exactly what
    // was drawn rather than guessing from a hand diff. Empty for replayed/validation contexts.
    if (!drew.empty())
    {
        os << "  \"drew\": [";
        for (size_t di = 0; di < drew.size(); ++di)
        {
            if (di) { os << ", "; }
            os << "{ \"turn\": " << drew[di].first << ", \"card\": ";
            JsonStr(os, drew[di].second);
            os << " }";
        }
        os << "],\n";
    }
    // Life-affecting events since the previous main-phase decision (combat with attacker breakdown,
    // burn, lifegain/loss incl. Tainted-Remedy flips), each { turn, kind, text }, so the viewer can
    // enumerate them in the history and the user needn't recompute life by hand.
    if (!events.empty())
    {
        os << "  \"events\": [";
        for (size_t ei = 0; ei < events.size(); ++ei)
        {
            if (ei) { os << ", "; }
            os << "{ \"turn\": " << events[ei].turn << ", \"kind\": ";
            JsonStr(os, events[ei].kind);
            os << ", \"text\": ";
            JsonStr(os, events[ei].text);
            os << " }";
        }
        os << "],\n";
    }
    WriteBoardContext(os, s, reveal_count);
    // Land's Edge availability: when the active player controls a Land's Edge (or any
    // "discard a land: deal N" outlet) and holds lands, the GUI single-click-activates it.
    // rate = damage per land discarded; lands_in_hand = how many can be fired (the UI caps
    // its picker at lethal). Absent when no outlet is in play.
    {
        int le_rate = 0;
        for (const Permanent& p : s.battlefield)
        {
            if (p.controller_index != s.active_player_index) { continue; }
            const CardDefinition* d = CardDatabase::Instance().LookupCached(p.card);
            if (d && d->params.discard_land_damage > 0)
            { le_rate = std::max(le_rate, d->params.discard_land_damage); }
        }
        if (le_rate > 0)
        {
            int lih = 0;
            for (const Card& c : me.hand)
            {
                const CardDefinition* d = CardDatabase::Instance().LookupCached(c);
                if (d ? d->card.IsLand() : c.IsLand()) { ++lih; }
            }
            os << "  \"lands_edge\": { \"rate\": " << le_rate
               << ", \"lands_in_hand\": " << lih << " },\n";
        }
    }
    os << "  \"plans\": [\n";
    for (size_t i = 0; i < plans.size(); ++i)
    {
        const TurnSolver::Plan& p = plans[i];
        os << "    { \"index\": " << i << ", \"summary\": ";
        JsonStr(os, SummarizePlan(p));
        // Structured land + cast list so the GUI can match a hand-assembled line against
        // the model's plans (and show, after a reject, exactly which lines it WOULD play).
        os << ", \"land\": ";
        if (p.land_decided && !p.land_to_play.empty()) { JsonStr(os, p.land_to_play); }
        else                                            { os << "null"; }
        // Plain name list (used for the land+cast multiset match). Land's Edge activations
        // are NOT casts -- they are surfaced via the action's "landsedge" count below and the
        // top-level "lands_edge" object, so the GUI's cast match doesn't treat them as spells.
        os << ", \"casts\": [";
        {
            bool first = true;
            for (size_t a = 0; a < p.actions.size(); ++a)
            {
                if (p.actions[a].kind == Action::Kind::DiscardToLandsEdge) { continue; }
                if (p.actions[a].kind == Action::Kind::DigDraw)            { continue; }  // a dig, not a cast
                if (!first) { os << ", "; }
                first = false;
                JsonStr(os, p.actions[a].card_name);
            }
        }
        os << "]";
        // ... plus the per-action variant params (tutor target / X / Ponder keep / Soulfire
        // own-targets) so the GUI can show WHICH variant when several plans share the same casts.
        os << ", \"actions\": [";
        for (size_t a = 0; a < p.actions.size(); ++a)
        {
            if (a) { os << ", "; }
            const Action& ac = p.actions[a];
            os << "{ \"card\": "; JsonStr(os, ac.card_name);
            if (ac.kind == Action::Kind::DiscardToLandsEdge) { os << ", \"landsedge\": " << ac.discard_lands; }
            if (ac.kind == Action::Kind::DigDraw) { os << ", \"dig\": true, \"dig_sacrifice\": " << (ac.dig_sacrifice ? "true" : "false"); }
            if (!ac.tutor_target.empty()) { os << ", \"tutor_target\": "; JsonStr(os, ac.tutor_target); }
            if (ac.chosen_x > 0)          { os << ", \"x\": " << ac.chosen_x; }
            if (ac.ponder_keep >= 0)      { os << ", \"ponder_keep\": " << ac.ponder_keep; }
            if (ac.soulfire_own_targets > 0) { os << ", \"soulfire_targets\": " << ac.soulfire_own_targets; }
            os << " }";
        }
        os << "]";
        os << (i + 1 < plans.size() ? " },\n" : " }\n");
    }
    os << "  ],\n";
    os << "  \"note\": \"reply with one plan index (0-based), or -1 to pass / cast nothing\"\n";
    os << "}\n";
}

// Vial-as-a-choice decision: whether to add a charge counter to an Aether Vial this
// upkeep. The reply is 1 (add a counter) or 0 (hold). `heuristic` is the default the
// encoded AI would take.
static void WriteVialDecisionJson(std::ostream& os, const GameState& s,
                                  const Permanent& vial, int decision_index, bool heuristic)
{
    os << "{\n";
    os << "  \"decision_index\": " << decision_index << ",\n";
    os << "  \"type\": \"vial_charge\",\n";
    os << "  \"turn\": " << s.turn_number << ",\n";
    os << "  \"vial\": "; JsonStr(os, vial.card.m_name);
    os << ", \"current_counters\": " << vial.charge_counters << ",\n";
    // perm_index of THIS vial on the battlefield, so the GUI can highlight it in place on the board.
    {
        int vi = -1;
        for (int i = 0; i < static_cast<int>(s.battlefield.size()); ++i) { if (&s.battlefield[i] == &vial) { vi = i; break; } }
        os << "  \"perm_index\": " << vi << ",\n";
    }
    os << "  \"heuristic_default\": " << (heuristic ? 1 : 0) << ",\n";
    WriteBoardContext(os, s, 0);
    os << "  \"note\": \"reply 1 to add a charge counter this upkeep, 0 to hold. Aether "
          "Vial deploys a creature whose mana value EQUALS its counter count.\"\n";
    os << "}\n";
}

// Mulligan keep/mulligan decision (claude-play). One per London-mulligan attempt, emitted BEFORE the
// first main-phase decision. The reply is 1 (keep this hand) or 0 (mulligan again). `ai_keep` is what
// the engine's KeepHand would do -- surfaced as the "AI would X" hint. `mulligan_count` is how many
// mulligans have been taken so far; keeping now means bottoming that many cards next.
static void WriteMulliganDecisionJson(std::ostream& os, const std::vector<Card>& hand,
                                      int mulligan_count, bool on_the_play, bool ai_keep,
                                      int decision_index)
{
    os << "{\n";
    os << "  \"decision_index\": " << decision_index << ",\n";
    os << "  \"type\": \"mulligan\",\n";
    os << "  \"turn\": 0,\n";
    // Minimal pre-game board so the viewer renders a clean empty board behind the modal (no permanents,
    // both players at 20) rather than an undefined one -- the real state has no battlefield yet.
    os << "  \"me\": { \"life\": 20, \"battlefield\": [] },\n";
    os << "  \"opponent\": { \"life\": 20, \"battlefield\": [] },\n";
    os << "  \"mulligan_count\": " << mulligan_count << ",\n";
    os << "  \"on_the_play\": " << (on_the_play ? "true" : "false") << ",\n";
    os << "  \"to_bottom\": " << mulligan_count << ",\n";   // London: keep at count K => bottom K cards
    os << "  \"hand\": [";
    for (size_t i = 0; i < hand.size(); ++i)
    {
        if (i) { os << ", "; }
        os << "{ \"num\": " << hand[i].m_number << ", \"name\": ";
        JsonStr(os, hand[i].m_name);
        os << " }";
    }
    os << "],\n";
    os << "  \"ai_choice\": " << (ai_keep ? 1 : 0) << ",\n";
    os << "  \"note\": \"reply 1 to KEEP this hand (then bottom " << mulligan_count
       << " card(s)), or 0 to mulligan again\"\n";
    os << "}\n";
}

// London bottoming decision (claude-play). After keeping at mulligan_count K, the player bottoms K
// cards one at a time; this fires once per card. The reply is the hand INDEX (0-based) of the card to
// put on the bottom. `ai_pick` is the hand index the engine would bottom (the "AI would X" hint); each
// hand card carries `win_optimal` (depth>0: does bottoming it preserve the earliest clairvoyant win?).
static void WriteBottomDecisionJson(std::ostream& os, const std::vector<Card>& hand,
                                    int ai_pick, const std::vector<char>& win_optimal,
                                    int step, int total, int decision_index)
{
    os << "{\n";
    os << "  \"decision_index\": " << decision_index << ",\n";
    os << "  \"type\": \"bottom\",\n";
    os << "  \"turn\": 0,\n";
    os << "  \"me\": { \"life\": 20, \"battlefield\": [] },\n";
    os << "  \"opponent\": { \"life\": 20, \"battlefield\": [] },\n";
    os << "  \"bottom_step\": " << step << ",\n";
    os << "  \"bottom_total\": " << total << ",\n";
    os << "  \"hand\": [";
    for (size_t i = 0; i < hand.size(); ++i)
    {
        if (i) { os << ", "; }
        os << "{ \"num\": " << hand[i].m_number << ", \"name\": ";
        JsonStr(os, hand[i].m_name);
        if (i < win_optimal.size())
        { os << ", \"win_optimal\": " << (win_optimal[i] ? "true" : "false"); }
        os << " }";
    }
    os << "],\n";
    os << "  \"ai_choice\": { \"index\": " << ai_pick;
    if (ai_pick >= 0 && ai_pick < static_cast<int>(hand.size()))
    {
        os << ", \"num\": " << hand[ai_pick].m_number << ", \"name\": ";
        JsonStr(os, hand[ai_pick].m_name);
    }
    os << " },\n";
    os << "  \"note\": \"reply the hand INDEX (0-based) of the card to put on the bottom (step "
       << (step + 1) << " of " << total << ")\"\n";
    os << "}\n";
}

// Emit a resolution-time "look at the top N" decision (Scry / Surveil / Ponder-reorder). The
// player replies an option INDEX into `opts` (the legal dispositions). Mirrors the vial decision:
// shares the one --choices stream, consumed in resolution order. The GUI renders `looked`
// face-up and `options` (each with its resulting top order + away pile) for the pick.
static void WriteTopDecisionJson(std::ostream& os, const GameState& s, const std::string& source,
                                 const std::vector<Card>& looked, LookKind kind,
                                 const std::vector<TopOption>& opts, int heuristic_default,
                                 int decision_index)
{
    const char* kindstr   = kind == LookKind::Scry ? "scry" : kind == LookKind::Surveil ? "surveil" : "reorder";
    const char* away_zone = kind == LookKind::Surveil ? "graveyard" : kind == LookKind::Scry ? "bottom" : "none";
    const int   m         = static_cast<int>(looked.size());

    os << "{\n";
    os << "  \"decision_index\": " << decision_index << ",\n";
    os << "  \"type\": \"" << kindstr << "\",\n";
    os << "  \"source\": "; JsonStr(os, source); os << ",\n";
    os << "  \"turn\": " << s.turn_number << ",\n";
    os << "  \"away_zone\": \"" << away_zone << "\",\n";
    WriteBoardContext(os, s, 0);
    os << "  \"looked\": [";
    for (int i = 0; i < m; ++i)
    {
        if (i) { os << ", "; }
        const CardDefinition* d = CardDatabase::Instance().LookupCached(looked[i]);
        os << "{ \"name\": "; JsonStr(os, looked[i].m_name.str());
        os << ", \"is_land\": " << ((d && d->card.IsLand()) ? "true" : "false") << " }";
    }
    os << "],\n";
    os << "  \"heuristic_default\": " << heuristic_default << ",\n";
    os << "  \"options\": [";
    for (size_t oi = 0; oi < opts.size(); ++oi)
    {
        if (oi) { os << ", "; }
        const TopDisposition& d = opts[oi].disp;
        std::vector<char>        on_top(m, 0);
        std::vector<std::string> top, away;
        if (!d.shuffle)
        {
            for (int idx : d.top_order)
            { if (idx >= 0 && idx < m && !on_top[idx]) { on_top[idx] = 1; top.push_back(looked[idx].m_name.str()); } }
            for (int i = 0; i < m; ++i)
            {
                if (on_top[i]) { continue; }
                if (kind == LookKind::Reorder) { top.push_back(looked[i].m_name.str()); }  // Ponder keeps all on top
                else                           { away.push_back(looked[i].m_name.str()); }
            }
        }
        os << "{ \"index\": " << oi << ", \"label\": "; JsonStr(os, opts[oi].label);
        os << ", \"shuffle\": " << (d.shuffle ? "true" : "false");
        os << ", \"top\": ";  JsonNameArray(os, top);
        os << ", \"away\": "; JsonNameArray(os, away);
        os << " }";
    }
    os << "],\n";
    os << "  \"note\": \"reply an option index. "
       << (kind == LookKind::Scry    ? "Kept cards stay on top (listed order); the rest go to the bottom."
         : kind == LookKind::Surveil ? "Kept cards stay on top; the rest go to the graveyard."
         :                             "Order all cards on top, or shuffle them away.")
       << "\"\n";
    os << "}\n";
}

// A target-set option for a damage spell's board-click targeting decision: the chosen targets
// (int-encoded) plus a human label. Built from the live board.
struct TargetOption { std::vector<ChosenTarget> targets; std::string label; };

// Legal damage targets on the board, in a stable order: opponent face, then every creature
// (opp first, then yours, board order), then your own face. Each carries a display label.
// `players_only` (Targeting::Player, e.g. Skullcrack -> "target player or planeswalker") restricts
// the set to the two faces -- no creatures. Any-target burn (Lightning Bolt) offers creatures too.
static void CollectDamageTargets(const GameState& s, int controller, bool players_only,
                                 std::vector<ChosenTarget>& out, std::vector<std::string>& labels)
{
    int opp = 1 - controller;
    out.push_back({ 0, opp });        labels.push_back("Opponent (face)");
    if (!players_only)
    {
        auto add_creatures = [&](int side) {
            for (int i = 0; i < static_cast<int>(s.battlefield.size()); ++i)
            {
                const Permanent& p = s.battlefield[i];
                if (p.controller_index != side) { continue; }
                const CardDefinition* d = CardDatabase::Instance().LookupCached(p.card);
                if (!d || !d->card.IsCreature()) { continue; }
                out.push_back({ 1, i });
                labels.push_back(p.card.m_name.str() + (side == controller ? " (yours)" : " (opponent)"));
            }
        };
        add_creatures(opp);
        add_creatures(controller);
    }
    out.push_back({ 0, controller }); labels.push_back("You (face)");
}

// The controller's own creatures, in board order -- the legal targets for an own-creature pump
// spell (Invigorate). kind==1 (permanent), index == battlefield index. No faces/opponent creatures.
static void CollectOwnCreatureTargets(const GameState& s, int controller,
                                      std::vector<ChosenTarget>& out, std::vector<std::string>& labels)
{
    for (int i = 0; i < static_cast<int>(s.battlefield.size()); ++i)
    {
        const Permanent& p = s.battlefield[i];
        if (p.controller_index != controller) { continue; }
        const CardDefinition* d = CardDatabase::Instance().LookupCached(p.card);
        if (!d || !d->card.IsCreature()) { continue; }
        out.push_back({ 1, i });
        labels.push_back(p.card.m_name.str() + " (yours)");
    }
}

// The OPPONENT's creatures, in board order -- the legal targets for a creature-removal spell (Swords
// to Plowshares exiles a creature and its controller gains life = its power; a Tainted Remedy / Plague
// Drone flips that gain to a loss). kind==1 (permanent), index == battlefield index. No faces / own
// creatures. The power/toughness in the label lets the player see which is biggest (max life swing).
static void CollectOpponentCreatureTargets(const GameState& s, int controller,
                                           std::vector<ChosenTarget>& out, std::vector<std::string>& labels)
{
    for (int i = 0; i < static_cast<int>(s.battlefield.size()); ++i)
    {
        const Permanent& p = s.battlefield[i];
        if (p.controller_index == controller) { continue; }
        // Use Card::IsCreature() directly (not a CardDatabase lookup): the opponent's creatures are
        // often TOKENS (Orchard Spirits), which have no entry in the card DB -- a LookupCached here
        // would return null and silently drop every token from the target list. P/T come from the
        // Permanent, so no definition is needed for the label either.
        if (!p.card.IsCreature()) { continue; }
        out.push_back({ 1, i });
        labels.push_back(p.card.m_name.str() + " (" + std::to_string(p.EffectivePower()) +
                         "/" + std::to_string(p.EffectiveToughness()) + ")");
    }
}

// ALL creatures on the battlefield (own AND opponent) -- the legal target creatures for a creature-
// targeting burn (Searing Blood "target creature"; Searing Blaze "target creature that player
// controls"). Own creatures are offered too so the human can cast Blood/Blaze on their own creature
// to trigger prowess when the opponent has none (a rare but legal line). kind==1, index == bf index.
static void CollectCreatureTargets(const GameState& s, int controller,
                                   std::vector<ChosenTarget>& out, std::vector<std::string>& labels)
{
    for (int i = 0; i < static_cast<int>(s.battlefield.size()); ++i)
    {
        const Permanent& p = s.battlefield[i];
        if (!p.card.IsCreature()) { continue; }
        const bool mine = (p.controller_index == controller);
        out.push_back({ 1, i });
        labels.push_back(p.card.m_name.str() + " (" + std::to_string(p.EffectivePower()) + "/"
                         + std::to_string(p.EffectiveToughness()) + (mine ? ", yours)" : ")"));
    }
}

// Enumerate legal target-set options for a uniform-damage spell: every nonempty subset of the
// legal targets with 1..max_targets members. Bounded (capped) so a big board can't explode the menu.
static std::vector<TargetOption> EnumerateTargetSets(const std::vector<ChosenTarget>& legal,
                                                     const std::vector<std::string>& labels, int max_targets,
                                                     int min_targets = 1)
{
    // Offer every subset of size [min_targets, cap]. min_targets is the affordability FLOOR: for
    // Crackle it is the committed plan's target count (the discount it already paid for) -- picking
    // fewer would under-pay the discount, so those subsets are not offered; picking more only raises
    // the discount (a free over-pay). A plain "up to N targets" burn passes min_targets = 1.
    std::vector<TargetOption> opts;
    const int n = static_cast<int>(legal.size());
    const int cap = std::max(1, std::min(max_targets, n));
    const int lo  = std::max(1, std::min(min_targets, cap));
    for (int mask = 1; mask < (1 << n) && opts.size() < 256; ++mask)
    {
        int bits = __builtin_popcount(static_cast<unsigned>(mask));
        if (bits > cap || bits < lo) { continue; }
        TargetOption o; std::string lbl;
        for (int i = 0; i < n; ++i)
        {
            if (!(mask & (1 << i))) { continue; }
            o.targets.push_back(legal[i]);
            lbl += (lbl.empty() ? "" : " + ") + labels[i];
        }
        o.label = lbl;
        opts.push_back(std::move(o));
    }
    return opts;
}

// Emit a divided-damage allocation decision. Each option carries its per-target amounts; the player
// replies one option index. `total` = the total damage to divide. Default = all to the opponent face.
// Divided-damage decision (Fiery Justice / Magma Opus): the player allocates `total` damage among
// ANY number of the legal targets, each getting >= 1, on the board via editable per-target numbers.
// The answer is one integer PER legal target (in this `legal_targets` order), 0 = not targeted, so
// there is no target-count cap (a spell that says "any number of targets" is modelled faithfully).
// `default_amounts` (aligned to legal_targets) seeds the board with the heuristic pick (all to face).
static void WriteDivideDecisionJson(std::ostream& os, const GameState& s, const std::string& source,
                                    const std::vector<ChosenTarget>& legal, const std::vector<std::string>& legal_labels,
                                    int total, const std::vector<int>& default_amounts, int decision_index)
{
    os << "{\n";
    os << "  \"decision_index\": " << decision_index << ",\n";
    os << "  \"type\": \"divide\",\n";
    os << "  \"source\": "; JsonStr(os, source); os << ",\n";
    os << "  \"turn\": " << s.turn_number << ",\n";
    os << "  \"total_damage\": " << total << ",\n";
    WriteBoardContext(os, s, 0);
    os << "  \"legal_targets\": [";
    for (size_t i = 0; i < legal.size(); ++i)
    { if (i) os << ", "; os << "{ \"kind\": \"" << (legal[i].kind == 0 ? "player" : "permanent")
                            << "\", \"index\": " << legal[i].index << ", \"label\": "; JsonStr(os, legal_labels[i]);
      os << ", \"default\": " << (i < default_amounts.size() ? default_amounts[i] : 0) << " }"; }
    os << "],\n";
    os << "  \"note\": \"reply one integer per legal target (in this order), each >= 0 and summing to "
       << total << " -- the damage assigned to each. Default = all to the opponent face.\"\n";
    os << "}\n";
}

// Emit a board-click target decision for a uniform-damage spell. `options` (built by the caller)
// each map a target set + label; the player replies one index. `per_target` = damage each target
// takes (Crackle 5*X, else the fixed damage). `heuristic_default` = the option matching the AI pick.
static void WriteTargetDecisionJson(std::ostream& os, const GameState& s, const std::string& source,
                                    const std::vector<ChosenTarget>& legal, const std::vector<std::string>& legal_labels,
                                    const std::vector<TargetOption>& options, int per_target,
                                    int max_targets, int heuristic_default, int decision_index,
                                    const std::string& pump_desc = "", const std::string& remove_desc = "",
                                    int min_targets = 1, bool random_damage = false)
{
    os << "{\n";
    os << "  \"decision_index\": " << decision_index << ",\n";
    os << "  \"type\": \"target\",\n";
    os << "  \"source\": "; JsonStr(os, source); os << ",\n";
    os << "  \"turn\": " << s.turn_number << ",\n";
    // A non-empty pump_desc (e.g. "+4/+4") flags this as an own-creature pump rather than damage, so
    // the viewer shows "gets +4/+4" and highlights your creatures instead of "N damage".
    if (!pump_desc.empty()) { os << "  \"pump\": "; JsonStr(os, pump_desc); os << ",\n"; }
    // A non-empty remove_desc (e.g. "exiled ...") flags this as a creature-removal target (Swords),
    // so the viewer highlights the OPPONENT's creatures and shows the removal wording, not "N damage".
    if (!remove_desc.empty()) { os << "  \"remove\": "; JsonStr(os, remove_desc); os << ",\n"; }
    // Soulfire Eruption: each target takes a RANDOM exiled card's mana value (not a fixed number), so
    // per_target_damage is meaningless -- the viewer shows "a random card's mana value each" instead.
    if (random_damage) { os << "  \"random_damage\": true,\n"; }
    os << "  \"per_target_damage\": " << per_target << ", \"max_targets\": " << max_targets
       << ", \"min_targets\": " << min_targets << ",\n";
    WriteBoardContext(os, s, 0);
    os << "  \"legal_targets\": [";
    for (size_t i = 0; i < legal.size(); ++i)
    { if (i) os << ", "; os << "{ \"kind\": \"" << (legal[i].kind == 0 ? "player" : "permanent")
                            << "\", \"index\": " << legal[i].index << ", \"label\": "; JsonStr(os, legal_labels[i]); os << " }"; }
    os << "],\n";
    os << "  \"heuristic_default\": " << heuristic_default << ",\n";
    os << "  \"options\": [";
    for (size_t oi = 0; oi < options.size(); ++oi)
    {
        if (oi) os << ", ";
        os << "{ \"index\": " << oi << ", \"label\": "; JsonStr(os, options[oi].label);
        os << ", \"targets\": [";
        for (size_t ti = 0; ti < options[oi].targets.size(); ++ti)
        { if (ti) os << ", "; os << "{ \"kind\": \"" << (options[oi].targets[ti].kind == 0 ? "player" : "permanent")
                                  << "\", \"index\": " << options[oi].targets[ti].index << " }"; }
        os << "] }";
    }
    os << "],\n";
    if (!pump_desc.empty())
    { os << "  \"note\": \"reply an option index. The chosen creature gets " << pump_desc
         << ". Default = the AI's pick (best attacker).\"\n"; }
    else if (!remove_desc.empty())
    { os << "  \"note\": \"reply an option index. The chosen opponent creature is " << remove_desc
         << ". Default = the AI's pick (largest).\"\n"; }
    else if (random_damage)
    { os << "  \"note\": \"reply an option index. Each chosen target is dealt a RANDOM exiled card's "
         << "mana value (assigned positionally, not steerable); pick " << min_targets << "-" << max_targets
         << " targets. Default = the AI's pick.\"\n"; }
    else
    { os << "  \"note\": \"reply an option index. Each chosen target takes " << per_target
         << " damage (up to " << max_targets << " target(s)). Default = the AI's pick (face).\"\n"; }
    os << "}\n";
}

// Emit a Karoo bounce-land return decision: which of the controller's lands goes back to hand.
// `legal` are battlefield indices; each option carries that index (so the GUI can highlight the
// permanent on the board) plus the land's name/tap state. `heuristic_default` indexes into `legal`.
static void WriteBounceDecisionJson(std::ostream& os, const GameState& s, const std::string& source,
                                    const std::vector<int>& legal, int heuristic_default, int decision_index,
                                    bool sacrifice = false)
{
    os << "{\n";
    os << "  \"decision_index\": " << decision_index << ",\n";
    // A `sacrifice` variant reuses the identical board-land picker as the Karoo bounce, but the land
    // goes to the graveyard (Shard Volley's additional cost) and the viewer says "sacrifice".
    os << "  \"type\": \"" << (sacrifice ? "sacrifice" : "bounce") << "\",\n";
    os << "  \"source\": "; JsonStr(os, source); os << ",\n";
    os << "  \"turn\": " << s.turn_number << ",\n";
    WriteBoardContext(os, s, 0);
    os << "  \"heuristic_default\": " << heuristic_default << ",\n";
    os << "  \"options\": [";
    for (size_t i = 0; i < legal.size(); ++i)
    {
        const Permanent& p = s.battlefield[legal[i]];
        if (i) os << ", ";
        os << "{ \"index\": " << i << ", \"perm_index\": " << legal[i] << ", \"tapped\": "
           << (p.tapped ? "true" : "false") << ", \"name\": "; JsonStr(os, p.card.m_name.str());
        os << ", \"label\": "; JsonStr(os, p.card.m_name.str()); os << " }";
    }
    os << "],\n";
    os << "  \"note\": \"reply an option index -- the land to " << (sacrifice ? "sacrifice" : "return to your hand")
       << ". Default = the AI's pick.\"\n";
    os << "}\n";
}

// ETB-dig decision (Acclaimed Contender): the player picks WHICH examined card enters hand (or
// declines). Emits the examined cards as image options with a `legal` flag (only legal candidates
// are takeable); the reply is the examined index to take, or -1 to take nothing.
static void WriteDigDecisionJson(std::ostream& os, const GameState& s, const std::string& source,
                                 const std::vector<Card>& examined, const std::vector<int>& legal,
                                 int heuristic_default, int decision_index)
{
    std::vector<bool> is_legal(examined.size(), false);
    for (int li : legal) { if (li >= 0 && li < static_cast<int>(examined.size())) { is_legal[li] = true; } }
    os << "{\n";
    os << "  \"decision_index\": " << decision_index << ",\n";
    os << "  \"type\": \"dig\",\n";
    os << "  \"source\": "; JsonStr(os, source); os << ",\n";
    os << "  \"turn\": " << s.turn_number << ",\n";
    WriteBoardContext(os, s, 0);
    os << "  \"heuristic_default\": " << heuristic_default << ",\n";
    os << "  \"examined\": [";
    for (size_t i = 0; i < examined.size(); ++i)
    {
        if (i) os << ", ";
        os << "{ \"index\": " << i << ", \"legal\": " << (is_legal[i] ? "true" : "false")
           << ", \"name\": "; JsonStr(os, examined[i].m_name.str()); os << " }";
    }
    os << "],\n";
    os << "  \"note\": \"reply an examined index to put that card into your hand, or -1 to take nothing. Default = the AI's pick.\"\n";
    os << "}\n";
}

// Cleanup-discard decision (#2): the player picks WHICH hand card to discard down to maximum hand
// size. Emits every hand card as an image option; the reply is the hand index to discard. One such
// decision fires per over-limit card.
static void WriteDiscardDecisionJson(std::ostream& os, const GameState& s,
                                     const std::vector<int>& hand_indices, int heuristic_default,
                                     int decision_index)
{
    const Player& ap = s.players[s.active_player_index];
    os << "{\n";
    os << "  \"decision_index\": " << decision_index << ",\n";
    os << "  \"type\": \"discard\",\n";
    os << "  \"turn\": " << s.turn_number << ",\n";
    os << "  \"over_by\": " << (static_cast<int>(ap.hand.size()) - 7) << ",\n";
    WriteBoardContext(os, s, 0);
    os << "  \"heuristic_default\": " << heuristic_default << ",\n";
    os << "  \"options\": [";
    for (size_t i = 0; i < hand_indices.size(); ++i)
    {
        int hi = hand_indices[i];
        if (i) os << ", ";
        os << "{ \"index\": " << hi << ", \"name\": ";
        JsonStr(os, (hi >= 0 && hi < static_cast<int>(ap.hand.size())) ? ap.hand[hi].m_name.str() : std::string());
        os << " }";
    }
    os << "],\n";
    os << "  \"note\": \"reply a hand index -- the card to discard. Default = the AI's pick.\"\n";
    os << "}\n";
}

// Expressive Iteration: enumerate the legal (hand_idx, exile_idx) splits over `look` looked cards
// (the remaining index -> bottom). Deterministic order, shared by the decision JSON and the chooser,
// so a --choices option index maps back to the same split.
static std::vector<std::pair<int,int>> EIAssignments(int look)
{
    std::vector<std::pair<int,int>> out;
    for (int h = 0; h < look; ++h)
        for (int e = 0; e < look; ++e)
            if (e != h) { out.emplace_back(h, e); }
    return out;
}

// Expressive Iteration decision: the player looks at the top cards and assigns one to HAND
// (banked), one to EXILE (playable this turn), the rest to the BOTTOM. Each option is one legal
// split; the reply is an option index. heur_option = the index of the AI's default split.
static void WriteEIDecisionJson(std::ostream& os, const GameState& s,
                                const std::vector<Card>& looked, int heur_option, int decision_index)
{
    const int look = static_cast<int>(looked.size());
    std::vector<std::pair<int,int>> asg = EIAssignments(look);
    os << "{\n";
    os << "  \"decision_index\": " << decision_index << ",\n";
    os << "  \"type\": \"expressive_iteration\",\n";
    os << "  \"source\": \"Expressive Iteration\",\n";
    os << "  \"turn\": " << s.turn_number << ",\n";
    WriteBoardContext(os, s, 0);
    os << "  \"looked\": [";
    for (int i = 0; i < look; ++i)
    { if (i) os << ", "; os << "{ \"index\": " << i << ", \"name\": "; JsonStr(os, looked[i].m_name.str()); os << " }"; }
    os << "],\n";
    os << "  \"heuristic_default\": " << heur_option << ",\n";
    os << "  \"options\": [";
    for (size_t oi = 0; oi < asg.size(); ++oi)
    {
        int h = asg[oi].first, e = asg[oi].second, b = -1;
        for (int i = 0; i < look; ++i) { if (i != h && i != e) { b = i; break; } }
        if (oi) os << ", ";
        os << "{ \"index\": " << oi << ", \"hand\": ";   JsonStr(os, looked[h].m_name.str());
        os << ", \"exile\": ";  JsonStr(os, looked[e].m_name.str());
        os << ", \"bottom\": "; if (b >= 0) { JsonStr(os, looked[b].m_name.str()); } else { os << "null"; }
        os << " }";
    }
    os << "],\n";
    os << "  \"note\": \"reply an option index: 'hand' is banked, 'exile' is playable this turn, 'bottom' goes to the library bottom. Default = the AI's pick.\"\n";
    os << "}\n";
}

// Retrace discard decision (Throes of Chaos): the player picks WHICH land in hand to discard as
// Retrace's additional cost. Emits the discardable lands as image options; the reply is the hand
// index to discard. One decision fires per land the retrace cast must discard (retrace = 1).
static void WriteRetraceDiscardDecisionJson(std::ostream& os, const GameState& s, const std::string& source,
                                            const std::vector<int>& hand_land_indices,
                                            int heuristic_default, int decision_index)
{
    const Player& ap = s.players[s.active_player_index];
    os << "{\n";
    os << "  \"decision_index\": " << decision_index << ",\n";
    os << "  \"type\": \"retrace_discard\",\n";
    os << "  \"source\": "; JsonStr(os, source); os << ",\n";
    os << "  \"turn\": " << s.turn_number << ",\n";
    WriteBoardContext(os, s, 0);
    os << "  \"heuristic_default\": " << heuristic_default << ",\n";
    os << "  \"options\": [";
    for (size_t i = 0; i < hand_land_indices.size(); ++i)
    {
        int hi = hand_land_indices[i];
        if (i) os << ", ";
        os << "{ \"index\": " << hi << ", \"name\": ";
        JsonStr(os, (hi >= 0 && hi < static_cast<int>(ap.hand.size())) ? ap.hand[hi].m_name.str() : std::string());
        os << " }";
    }
    os << "],\n";
    os << "  \"note\": \"reply a hand index -- the land to discard as this spell's Retrace additional cost. Default = the AI's pick.\"\n";
    os << "}\n";
}


// Parse a --validate-line spec into a LineSpec. Tokens are ';'-separated; each is
// "land=<name>", "cast=<name>", or the bare word "pass". Card names may contain spaces
// and commas (no MTG name contains ';' or '='), so they pass through verbatim.
static TurnSolver::LineSpec ParseLineSpec(const std::string& spec)
{
    TurnSolver::LineSpec ls;
    std::stringstream ss(spec);
    std::string tok;
    while (std::getline(ss, tok, ';'))
    {
        // trim surrounding whitespace
        size_t b = tok.find_first_not_of(" \t");
        size_t e = tok.find_last_not_of(" \t");
        if (b == std::string::npos) { continue; }
        tok = tok.substr(b, e - b + 1);
        if (tok == "pass") { ls.pass = true; continue; }
        auto eq = tok.find('=');
        if (eq == std::string::npos) { continue; }
        std::string key = tok.substr(0, eq);
        std::string val = tok.substr(eq + 1);
        if      (key == "land")      { ls.has_land = true; ls.land = val; }
        else if (key == "cast")      { ls.casts.push_back(val); }
        else if (key == "landsedge") { ls.lands_edge = std::atoi(val.c_str()); }
        else if (key == "vial")      { ls.vial_deploys.push_back(val); }
        else if (key == "retrace")   { ls.retrace_casts.push_back(val); }
    }
    return ls;
}

static int RunClaudePlay(const Decklist& deck, const MulliganProfile& profile,
                         uint64_t seed, int game_index, int max_turns,
                         int lookahead_depth, int timeout_ms, std::vector<int> choices,
                         int reveal_count, const std::filesystem::path& log_dir,
                         const std::string& validate_line = "",
                         const std::string& force_mulligan = "")
{
    GameState state = GoldFishRunner::SetupGame(deck, seed);
    state.vial_target_mv = profile.vial_target_mv;
    GoldFishRunner::PopulateOpponentSpawns(state, game_index);
    // Stamp stable per-copy card numbers (goldfish only does this under --log-dir). Claude-play needs
    // them so the emitted hand/decision JSON carries real "num"s and --force-mulligan can bottom a
    // specific card by number (mulligan reproducibility). See docs/design/claude-play-mulligan-*.
    GoldFishRunner::AssignCardNumbers(state, GoldFishRunner::BuildCardNumbering(deck));

    AIEngine ai(profile, lookahead_depth, timeout_ms);

    // Mulligan reproducibility (--force-mulligan "<count>:<n1,n2,...>"): reconstruct a reference's
    // exact opening hand by keeping at <count> mulligans and bottoming the listed card numbers,
    // independent of the current keep/bottoming heuristics. See docs/design/claude-play-mulligan-*.
    if (!force_mulligan.empty())
    {
        std::string spec = force_mulligan;
        auto colon = spec.find(':');
        int fcount = std::stoi(spec.substr(0, colon));
        std::vector<int> fbottom;
        if (colon != std::string::npos)
        {
            std::stringstream bs(spec.substr(colon + 1));
            std::string tok;
            while (std::getline(bs, tok, ',')) { if (!tok.empty()) { fbottom.push_back(std::stoi(tok)); } }
        }
        ai.SetForcedMulligan(fcount, std::move(fbottom));
    }
    size_t cursor = 0;
    int decisions_made = 0;
    std::vector<std::string> trace;   // one entry per RESOLVED decision (for --log-dir)
    // Accurate per-draw reporting: the real draw sites append (turn, card_name) here as cards are
    // drawn (see g_play_draw_sink). It accumulates the draws since the last RESOLVED main-phase
    // decision, so the NEXT emitted main-phase decision reports exactly the new draws for the
    // viewer history. Nulled by RevealLogPause during the search, so only real draws land here.
    std::vector<std::pair<int, std::string>> draw_log;
    g_play_draw_sink = &draw_log;
    // Life-affecting events (combat/burn/lifegain-loss) since the last resolved main decision, for the
    // viewer history. Same lifecycle as draw_log: real sites append, cleared when a decision is consumed.
    std::vector<PlayEvent> event_log;
    g_play_event_sink = &event_log;
    ai.SetExternalChooser(
        [&](const GameState& s, const std::vector<TurnSolver::Plan>& plans, bool is_pre) -> int
        {
            int di = static_cast<int>(cursor);
            if (cursor < choices.size())
            {
                int chosen = choices[cursor++];
                ++decisions_made;
                if (!log_dir.empty())
                {
                    // Record this resolved decision (state + plans + the chosen index).
                    // Only the completing full-CSV run writes the trace file (below).
                    std::ostringstream ss;
                    ss << "{ \"chosen\": " << chosen << ", \"decision\": ";
                    WriteDecisionJson(ss, s, plans, is_pre, di, reveal_count, draw_log, event_log);
                    ss << "}";
                    trace.push_back(ss.str());
                }
                // This main-phase decision has been consumed (it was shown in a prior viewer step);
                // its draws were already reported. Clear so the NEXT emitted decision reports only
                // the draws that happen AFTER it (turn draw / cantrip draws of the next segment).
                draw_log.clear();
                event_log.clear();
                return chosen;
            }
            // Human-play line reconciliation: if a --validate-line was supplied, this is the
            // FIRST un-chosen main-phase decision -- reconcile the hand-assembled line against
            // the model instead of dumping the plan menu. Accept -> emit the matched plan index
            // (the bridge records it and the game proceeds); reject -> classify (illegal vs
            // legal-but-not-enumerated) so the GUI can offer to store it as an artifact.
            if (!validate_line.empty())
            {
                TurnSolver::LineSpec spec = ParseLineSpec(validate_line);
                TurnSolver::LineCheck chk = TurnSolver::CheckLine(s, is_pre, spec);
                using Vd = TurnSolver::LineCheck::Verdict;
                const char* vstr =
                    chk.verdict == Vd::Accept             ? "accept" :
                    chk.verdict == Vd::Choose             ? "choose" :
                    chk.verdict == Vd::LegalNotEnumerated ? "legal_not_enumerated" :
                    chk.verdict == Vd::Unsupported        ? "unsupported" :
                                                            "illegal";
                std::cout << "<<<CLAUDE_VALIDATION>>>\n{\n";
                std::cout << "  \"decision_index\": " << di << ",\n";
                std::cout << "  \"verdict\": \""  << vstr << "\",\n";
                std::cout << "  \"plan_index\": " << chk.plan_index << ",\n";
                std::cout << "  \"matched_summary\": "; JsonStr(std::cout, chk.matched_summary); std::cout << ",\n";
                std::cout << "  \"failed_action\": ";   JsonStr(std::cout, chk.failed_action);   std::cout << ",\n";
                std::cout << "  \"reason\": ";          JsonStr(std::cout, chk.reason);          std::cout << ",\n";
                std::cout << "  \"variants\": [";
                for (size_t vi = 0; vi < chk.variants.size(); ++vi)
                {
                    if (vi) { std::cout << ", "; }
                    std::cout << "{ \"plan_index\": " << chk.variants[vi].plan_index
                              << ", \"label\": "; JsonStr(std::cout, chk.variants[vi].label);
                    std::cout << ", \"cards\": [";
                    for (size_t ci = 0; ci < chk.variants[vi].cards.size(); ++ci)
                    { if (ci) std::cout << ", "; JsonStr(std::cout, chk.variants[vi].cards[ci]); }
                    std::cout << "]";
                    // Structured sub-decision breakdown so the GUI can ask one dimension at a time
                    // (fetch target, then tutor target, ...) and filter variants after each pick.
                    std::cout << ", \"subs\": [";
                    for (size_t si = 0; si < chk.variants[vi].subs.size(); ++si)
                    {
                        const auto& sub = chk.variants[vi].subs[si];
                        if (si) { std::cout << ", "; }
                        std::cout << "{ \"key\": ";    JsonStr(std::cout, sub.key);
                        std::cout << ", \"choice\": "; JsonStr(std::cout, sub.choice);
                        std::cout << ", \"card\": ";   JsonStr(std::cout, sub.card);
                        std::cout << ", \"kind\": ";   JsonStr(std::cout, sub.kind);
                        std::cout << " }";
                    }
                    std::cout << "] }";
                }
                std::cout << "],\n";
                std::cout << "  \"decision\": ";
                WriteDecisionJson(std::cout, s, plans, is_pre, di, reveal_count, draw_log, event_log);
                std::cout << "}\n<<<END_VALIDATION>>>\n";
                std::cout.flush();
                std::exit(71);   // distinct code: "validation verdict emitted"
            }
            std::cout << "<<<CLAUDE_DECISION>>>\n";
            WriteDecisionJson(std::cout, s, plans, is_pre, di, reveal_count, draw_log, event_log);
            std::cout << "<<<END_DECISION>>>\n";
            std::cout.flush();
            std::exit(70);   // distinct code: "more input needed"
        });

    // Vial-as-a-choice: claude decides each Aether Vial upkeep charge. Shares the single
    // --choices stream + cursor with the main chooser (consulted at upkeep, before the
    // main phase). Reply 1 = add a counter, 0 = hold. (Future default: only surface this
    // when the decision is genuinely ambiguous, not every upkeep.)
    ai.SetExternalVialChooser(
        [&](const GameState& s, const Permanent& vial, bool heuristic) -> bool
        {
            int di = static_cast<int>(cursor);
            if (cursor < choices.size())
            {
                int chosen = choices[cursor++];
                ++decisions_made;
                if (!log_dir.empty())
                {
                    std::ostringstream ss;
                    ss << "{ \"chosen\": " << chosen << ", \"decision\": ";
                    WriteVialDecisionJson(ss, s, vial, di, heuristic);
                    ss << "}";
                    trace.push_back(ss.str());
                }
                return chosen != 0;
            }
            std::cout << "<<<CLAUDE_DECISION>>>\n";
            WriteVialDecisionJson(std::cout, s, vial, di, heuristic);
            std::cout << "<<<END_DECISION>>>\n";
            std::cout.flush();
            std::exit(70);
        });

    // Mulligan keep/mulligan (claude-play): the human drives each London-mulligan attempt, sharing
    // the one --choices stream (these fire FIRST, before any turn decision). Reply 1 keep / 0 mulligan.
    // Skipped entirely under --force-mulligan (that reconstructs an exact recorded hand on the engine).
    ai.SetExternalMulliganChooser(
        [&](const std::vector<Card>& hand, int mull_count, bool on_play, bool ai_keep) -> bool
        {
            int di = static_cast<int>(cursor);
            if (cursor < choices.size())
            {
                int chosen = choices[cursor++];
                ++decisions_made;
                if (!log_dir.empty())
                {
                    std::ostringstream ss;
                    ss << "{ \"chosen\": " << chosen << ", \"decision\": ";
                    WriteMulliganDecisionJson(ss, hand, mull_count, on_play, ai_keep, di);
                    ss << "}";
                    trace.push_back(ss.str());
                }
                return chosen != 0;
            }
            std::cout << "<<<CLAUDE_DECISION>>>\n";
            WriteMulliganDecisionJson(std::cout, hand, mull_count, on_play, ai_keep, di);
            std::cout << "<<<END_DECISION>>>\n";
            std::cout.flush();
            std::exit(70);
        });

    // London bottoming (claude-play): after the kept hand, the human bottoms one card at a time.
    // Reply the hand INDEX to bottom. Shares the --choices stream, consumed right after the keep.
    ai.SetExternalBottomChooser(
        [&](const std::vector<Card>& hand, int ai_pick, const std::vector<char>& win_opt,
            int step, int total) -> int
        {
            int di = static_cast<int>(cursor);
            if (cursor < choices.size())
            {
                int chosen = choices[cursor++];
                ++decisions_made;
                if (!log_dir.empty())
                {
                    std::ostringstream ss;
                    ss << "{ \"chosen\": " << chosen << ", \"decision\": ";
                    WriteBottomDecisionJson(ss, hand, ai_pick, win_opt, step, total, di);
                    ss << "}";
                    trace.push_back(ss.str());
                }
                return chosen;
            }
            std::cout << "<<<CLAUDE_DECISION>>>\n";
            WriteBottomDecisionJson(std::cout, hand, ai_pick, win_opt, step, total, di);
            std::cout << "<<<END_DECISION>>>\n";
            std::cout.flush();
            std::exit(70);
        });

    // Look-at-top resolution decisions (Scry / Surveil / Ponder-reorder). Fired from inside
    // ScryTop/SurveilTop/ReorderTopOrShuffle during REAL resolution (gated by g_reveal_logger, so
    // never the search). The player replies one option index into the enumerated dispositions;
    // shares the single --choices stream, consumed in resolution order like the vial decision.
    TopChooser top_chooser =
        [&](const GameState& s, const std::string& source, const std::vector<Card>& looked,
            LookKind kind) -> TopDisposition
        {
            std::vector<TopOption> opts = EnumerateTopDispositions(kind, looked);
            TopDisposition hd = HeuristicTopDisposition(s, looked, kind);
            int def = 0;
            for (size_t i = 0; i < opts.size(); ++i)
            {
                if (opts[i].disp.shuffle == hd.shuffle && opts[i].disp.top_order == hd.top_order) { def = static_cast<int>(i); break; }
            }
            int di = static_cast<int>(cursor);
            if (cursor < choices.size())
            {
                int chosen = choices[cursor++];
                ++decisions_made;
                if (chosen < 0 || chosen >= static_cast<int>(opts.size())) { chosen = def; }
                if (!log_dir.empty())
                {
                    std::ostringstream ss;
                    ss << "{ \"chosen\": " << chosen << ", \"decision\": ";
                    WriteTopDecisionJson(ss, s, source, looked, kind, opts, def, di);
                    ss << "}";
                    trace.push_back(ss.str());
                }
                return opts[chosen].disp;
            }
            std::cout << "<<<CLAUDE_DECISION>>>\n";
            WriteTopDecisionJson(std::cout, s, source, looked, kind, opts, def, di);
            std::cout << "<<<END_DECISION>>>\n";
            std::cout.flush();
            std::exit(70);
        };
    g_play_top_chooser = &top_chooser;

    // Board-click targeting for uniform-damage spells (fixed burn, Crackle). Fired from
    // CastSpellFromHand during the real cast; shares the --choices stream. The player replies one
    // option index (a target set). Prepopulated default = the AI's heuristic pick (usually face).
    TargetChooser target_chooser =
        [&](const GameState& s, const CardDefinition& def, int controller, int max_targets,
            int per_target_damage, const std::vector<ChosenTarget>& heuristic) -> std::vector<ChosenTarget>
        {
            // Own-creature pump (Invigorate): legal targets are the controller's creatures, not
            // damage targets, and the dialog reads "+P/+T" rather than "N damage".
            const bool own_pump = (def.tmpl == CardTemplate::PumpSpell) && def.params.target_own_creature;
            const std::string pump_desc = own_pump
                ? ("+" + std::to_string(def.params.power_bonus) + "/+" + std::to_string(def.params.tough_bonus))
                : std::string();
            // Creature removal (Swords to Plowshares): legal targets are the OPPONENT's creatures; the
            // exile makes the opponent gain life = the creature's power (a Tainted Remedy flips that to
            // a loss -- the whole point of the deck), so surface the target as a human choice.
            const bool exile_removal = (def.tmpl == CardTemplate::Removal);
            const std::string remove_desc = exile_removal
                ? std::string("exiled; you gain life equal to its power (a Tainted Remedy / Plague Drone flips that to a life loss)")
                : std::string();
            // Creature-targeting burn (Searing Blood "target creature"; Searing Blaze "... that player
            // controls"): the human picks WHICH creature takes the damage. Own creatures are offered too
            // (cast for prowess with no opponent creature). Not the face-inclusive damage-target set.
            const bool creature_burn = (def.tmpl == CardTemplate::DirectDamage)
                && (def.params.targeting == Targeting::Creature || def.params.targeting == Targeting::Multi)
                && !def.params.damage_divided;
            const bool players_only = (def.params.targeting == Targeting::Player);
            // Crackle with Power: a generic "up to X targets, 5X each" spell. The opponent FACE is a
            // NORMAL, optional target (CrackleTargetOrder[0]), NOT a forced hit -- the human may target
            // it, deselect it, and add creatures/self. Legal set + order come from CrackleTargetOrder
            // (opponent face, opponent creatures, own non-Hinata, self-if-safe, Hinata last). The count
            // is bounded [min, max]: min = the committed plan's paid discount (heuristic.size()), max =
            // max_targets (= min(X, #legal)). No separate count dialog -- the count IS the selection.
            const bool crackle = IsCrackleCountSpell(def.params);
            std::vector<ChosenTarget> legal; std::vector<std::string> legal_labels;
            if (crackle)
            {
                for (int t : CrackleTargetOrder(s, controller, per_target_damage))
                {
                    if      (t == CRACKLE_OPP_FACE)  { legal.push_back({ 0, 1 - controller, 0 }); legal_labels.push_back("Opponent"); }
                    else if (t == CRACKLE_SELF_FACE) { legal.push_back({ 0, controller, 0 });     legal_labels.push_back("You (self)"); }
                    else                             { legal.push_back({ 1, t, 0 });              legal_labels.push_back(s.battlefield[t].card.m_name.str()); }
                }
            }
            else if (own_pump)       { CollectOwnCreatureTargets(s, controller, legal, legal_labels); }
            else if (exile_removal)  { CollectOpponentCreatureTargets(s, controller, legal, legal_labels); }
            else if (creature_burn)  { CollectCreatureTargets(s, controller, legal, legal_labels); }
            else                     { CollectDamageTargets(s, controller, players_only, legal, legal_labels); }

            // Divided damage (Fiery Justice / Magma Opus): the player allocates `per_target_damage`
            // (the TOTAL here) among ANY number of the legal targets, each >= 1. The answer is ONE
            // integer per legal target (in `legal` order), 0 = untargeted -- so there is no
            // target-count cap. The board GUI edits these numbers directly. Default = all to opp face.
            if (def.params.damage_divided)
            {
                if (legal.empty()) { return heuristic; }
                const int total = per_target_damage;
                const int need  = static_cast<int>(legal.size());
                std::vector<int> defaults(need, 0);
                defaults[0] = total;   // legal[0] is the opponent face (CollectDamageTargets order)
                std::vector<ChosenTarget> heur_alloc = { legal[0] }; heur_alloc[0].amount = total;
                int di = static_cast<int>(cursor);
                if (cursor + need <= static_cast<int>(choices.size()))
                {
                    std::vector<int> amts(need);
                    int sum = 0;
                    for (int i = 0; i < need; ++i) { int a = choices[cursor++]; if (a < 0) { a = 0; } amts[i] = a; sum += a; }
                    ++decisions_made;
                    std::vector<ChosenTarget> built;
                    for (int i = 0; i < need; ++i)
                    { if (amts[i] > 0) { ChosenTarget c = legal[i]; c.amount = amts[i]; built.push_back(c); } }
                    const bool ok = (sum == total) && !built.empty();   // else fall back to all-to-face
                    if (!log_dir.empty())
                    {
                        std::ostringstream ss;
                        ss << "{ \"chosen\": [";
                        for (int i = 0; i < need; ++i) { if (i) ss << ", "; ss << amts[i]; }
                        ss << "], \"decision\": ";
                        WriteDivideDecisionJson(ss, s, def.card.m_name.str(), legal, legal_labels, total, defaults, di);
                        ss << "}";
                        trace.push_back(ss.str());
                    }
                    return ok ? built : heur_alloc;
                }
                std::cout << "<<<CLAUDE_DECISION>>>\n";
                WriteDivideDecisionJson(std::cout, s, def.card.m_name.str(), legal, legal_labels, total, defaults, di);
                std::cout << "<<<END_DECISION>>>\n";
                std::cout.flush();
                std::exit(70);
            }

            // Crackle: floor the count at the committed plan's discount (heuristic.size()); every other
            // burn offers 1..max. max_targets is min(X, #legal) for Crackle, 1 for a single-target burn.
            const int min_targets = crackle ? std::max(1, static_cast<int>(heuristic.size())) : 1;
            std::vector<TargetOption> opts = EnumerateTargetSets(legal, legal_labels, max_targets, min_targets);
            if (opts.empty()) { return heuristic; }
            const int per_target = per_target_damage;   // actual engine damage per target (not recomputed)
            // Default index = the option whose target set matches the heuristic pick.
            auto same = [](const std::vector<ChosenTarget>& a, const std::vector<ChosenTarget>& b) {
                if (a.size() != b.size()) { return false; }
                for (size_t i = 0; i < a.size(); ++i) { if (a[i].kind != b[i].kind || a[i].index != b[i].index) { return false; } }
                return true; };
            int def_idx = 0;
            for (size_t i = 0; i < opts.size(); ++i) { if (same(opts[i].targets, heuristic)) { def_idx = static_cast<int>(i); break; } }
            int di = static_cast<int>(cursor);
            if (cursor < choices.size())
            {
                int chosen = choices[cursor++];
                ++decisions_made;
                if (chosen < 0 || chosen >= static_cast<int>(opts.size())) { chosen = def_idx; }
                if (!log_dir.empty())
                {
                    std::ostringstream ss;
                    ss << "{ \"chosen\": " << chosen << ", \"decision\": ";
                    WriteTargetDecisionJson(ss, s, def.card.m_name.str(), legal, legal_labels, opts, per_target, max_targets, def_idx, di, pump_desc, remove_desc, min_targets);
                    ss << "}";
                    trace.push_back(ss.str());
                }
                return opts[chosen].targets;
            }
            std::cout << "<<<CLAUDE_DECISION>>>\n";
            WriteTargetDecisionJson(std::cout, s, def.card.m_name.str(), legal, legal_labels, opts, per_target, max_targets, def_idx, di, pump_desc, remove_desc, min_targets);
            std::cout << "<<<END_DECISION>>>\n";
            std::cout.flush();
            std::exit(70);
        };
    g_play_target_chooser = &target_chooser;

    // Karoo bounce-land return: the player picks which land goes back to hand. Shares the --choices
    // stream; replies one option index into the legal lands. Default = the engine's heuristic pick.
    BounceChooser bounce_chooser =
        [&](const GameState& s, int controller, const std::string& source,
            const std::vector<int>& legal, int heuristic_pick) -> int
        {
            (void)controller;
            int di = static_cast<int>(cursor);
            if (cursor < choices.size())
            {
                int chosen = choices[cursor++];
                ++decisions_made;
                if (chosen < 0 || chosen >= static_cast<int>(legal.size())) { chosen = heuristic_pick; }
                if (!log_dir.empty())
                {
                    std::ostringstream ss;
                    ss << "{ \"chosen\": " << chosen << ", \"decision\": ";
                    WriteBounceDecisionJson(ss, s, source, legal, heuristic_pick, di);
                    ss << "}";
                    trace.push_back(ss.str());
                }
                return chosen;
            }
            std::cout << "<<<CLAUDE_DECISION>>>\n";
            WriteBounceDecisionJson(std::cout, s, source, legal, heuristic_pick, di);
            std::cout << "<<<END_DECISION>>>\n";
            std::cout.flush();
            std::exit(70);
        };
    g_play_bounce_chooser = &bounce_chooser;

    // Sacrifice-a-land (Shard Volley's additional cost): identical board-land picker as bounce, but the
    // land is sacrificed (to graveyard). Shares the --choices stream; reply an option index into the
    // legal lands. Default = the engine's pick (a tapped land if any). Nulled for search by RevealLogPause.
    BounceChooser sacrifice_chooser =
        [&](const GameState& s, int controller, const std::string& source,
            const std::vector<int>& legal, int heuristic_pick) -> int
        {
            (void)controller;
            int di = static_cast<int>(cursor);
            if (cursor < choices.size())
            {
                int chosen = choices[cursor++];
                ++decisions_made;
                if (chosen < 0 || chosen >= static_cast<int>(legal.size())) { chosen = heuristic_pick; }
                if (!log_dir.empty())
                {
                    std::ostringstream ss;
                    ss << "{ \"chosen\": " << chosen << ", \"decision\": ";
                    WriteBounceDecisionJson(ss, s, source, legal, heuristic_pick, di, /*sacrifice=*/true);
                    ss << "}";
                    trace.push_back(ss.str());
                }
                return chosen;
            }
            std::cout << "<<<CLAUDE_DECISION>>>\n";
            WriteBounceDecisionJson(std::cout, s, source, legal, heuristic_pick, di, /*sacrifice=*/true);
            std::cout << "<<<END_DECISION>>>\n";
            std::cout.flush();
            std::exit(70);
        };
    g_play_sacrifice_chooser = &sacrifice_chooser;

    // ETB dig (Acclaimed Contender): the player picks which examined card enters hand (or declines).
    // Shares the --choices stream; the reply is an examined index, or -1 to take nothing. Default =
    // the engine's heuristic pick (the first legal match).
    DigChooser dig_chooser =
        [&](const GameState& s, int controller, const std::string& source,
            const std::vector<Card>& examined, const std::vector<int>& legal, int heuristic_pick) -> int
        {
            (void)controller;
            int di = static_cast<int>(cursor);
            if (cursor < choices.size())
            {
                int chosen = choices[cursor++];
                ++decisions_made;
                bool ok = (chosen == -1);
                for (int li : legal) { if (li == chosen) { ok = true; break; } }
                if (!ok) { chosen = heuristic_pick; }
                if (!log_dir.empty())
                {
                    std::ostringstream ss;
                    ss << "{ \"chosen\": " << chosen << ", \"decision\": ";
                    WriteDigDecisionJson(ss, s, source, examined, legal, heuristic_pick, di);
                    ss << "}";
                    trace.push_back(ss.str());
                }
                return chosen;
            }
            std::cout << "<<<CLAUDE_DECISION>>>\n";
            WriteDigDecisionJson(std::cout, s, source, examined, legal, heuristic_pick, di);
            std::cout << "<<<END_DECISION>>>\n";
            std::cout.flush();
            std::exit(70);
        };
    g_play_dig_chooser = &dig_chooser;

    // Cleanup discard (#2): the player picks which hand card to discard to max hand size. Shares the
    // --choices stream; the reply is a hand index. Default = the engine's heuristic pick.
    DiscardChooser discard_chooser =
        [&](const GameState& s, int controller, const std::vector<int>& hand_indices, int heuristic_pick) -> int
        {
            (void)controller;
            int di = static_cast<int>(cursor);
            if (cursor < choices.size())
            {
                int chosen = choices[cursor++];
                ++decisions_made;
                bool ok = false;
                for (int hi : hand_indices) { if (hi == chosen) { ok = true; break; } }
                if (!ok) { chosen = heuristic_pick; }
                if (!log_dir.empty())
                {
                    std::ostringstream ss;
                    ss << "{ \"chosen\": " << chosen << ", \"decision\": ";
                    WriteDiscardDecisionJson(ss, s, hand_indices, heuristic_pick, di);
                    ss << "}";
                    trace.push_back(ss.str());
                }
                return chosen;
            }
            std::cout << "<<<CLAUDE_DECISION>>>\n";
            WriteDiscardDecisionJson(std::cout, s, hand_indices, heuristic_pick, di);
            std::cout << "<<<END_DECISION>>>\n";
            std::cout.flush();
            std::exit(70);
        };
    g_play_discard_chooser = &discard_chooser;

    // Expressive Iteration (#): the player assigns the looked-at top cards to hand / exile (play this
    // turn) / bottom. Shares the --choices stream; the reply is an option index into the enumerated
    // splits. Default = the engine's heuristic split (heur_hand_idx, heur_exile_idx).
    EIChooser ei_chooser =
        [&](const GameState& s, const std::vector<Card>& looked, int heur_hand, int heur_exile)
            -> std::pair<int,int>
        {
            std::vector<std::pair<int,int>> asg = EIAssignments(static_cast<int>(looked.size()));
            int heur_option = 0;
            for (size_t oi = 0; oi < asg.size(); ++oi)
            { if (asg[oi].first == heur_hand && asg[oi].second == heur_exile) { heur_option = static_cast<int>(oi); break; } }
            int di = static_cast<int>(cursor);
            if (cursor < choices.size())
            {
                int chosen = choices[cursor++];
                ++decisions_made;
                if (chosen < 0 || chosen >= static_cast<int>(asg.size())) { chosen = heur_option; }
                if (!log_dir.empty())
                {
                    std::ostringstream ss;
                    ss << "{ \"chosen\": " << chosen << ", \"decision\": ";
                    WriteEIDecisionJson(ss, s, looked, heur_option, di);
                    ss << "}";
                    trace.push_back(ss.str());
                }
                return asg[chosen];
            }
            std::cout << "<<<CLAUDE_DECISION>>>\n";
            WriteEIDecisionJson(std::cout, s, looked, heur_option, di);
            std::cout << "<<<END_DECISION>>>\n";
            std::cout.flush();
            std::exit(70);
        };
    g_play_ei_chooser = &ei_chooser;

    // Retrace discard: the player picks which land to discard as Throes of Chaos's additional cost.
    // Shares the --choices stream; the reply is a hand index. Default = the engine's heuristic (first
    // land in hand order).
    RetraceDiscardChooser retrace_chooser =
        [&](const GameState& s, int controller, const std::string& source,
            const std::vector<int>& lands, int heuristic_pick) -> int
        {
            (void)controller;
            int di = static_cast<int>(cursor);
            if (cursor < choices.size())
            {
                int chosen = choices[cursor++];
                ++decisions_made;
                bool ok = false;
                for (int li : lands) { if (li == chosen) { ok = true; break; } }
                if (!ok) { chosen = heuristic_pick; }
                if (!log_dir.empty())
                {
                    std::ostringstream ss;
                    ss << "{ \"chosen\": " << chosen << ", \"decision\": ";
                    WriteRetraceDiscardDecisionJson(ss, s, source, lands, heuristic_pick, di);
                    ss << "}";
                    trace.push_back(ss.str());
                }
                return chosen;
            }
            std::cout << "<<<CLAUDE_DECISION>>>\n";
            WriteRetraceDiscardDecisionJson(std::cout, s, source, lands, heuristic_pick, di);
            std::cout << "<<<END_DECISION>>>\n";
            std::cout.flush();
            std::exit(70);
        };
    g_play_retrace_chooser = &retrace_chooser;

    // Soulfire Eruption targeting: the player picks the FULL target set on the board, exactly like
    // Crackle. `legal` is the canonical target order (sentinels for the faces + battlefield indices);
    // `min_targets` is the affordability floor (the count the cast already paid the discount for). We
    // reuse the generic `target` decision + EnumerateTargetSets, tagged random_damage so the viewer
    // shows "a random card's mana value each" instead of a fixed number. The reply is an option index;
    // we map the chosen target set back to the sentinel encoding SoulfireDig expects.
    auto sentinelToChosen = [](int t, int ci) -> ChosenTarget {
        if (t == TARGET_OPP_FACE)  { return ChosenTarget{ 0, 1 - ci, 0 }; }
        if (t == TARGET_SELF_FACE) { return ChosenTarget{ 0, ci,     0 }; }
        return ChosenTarget{ 1, t, 0 };
    };
    auto chosenToSentinel = [](const ChosenTarget& c, int ci) -> int {
        if (c.kind == 0) { return (c.index == ci) ? TARGET_SELF_FACE : TARGET_OPP_FACE; }
        return c.index;
    };
    SoulfireTargetChooser soulfire_chooser =
        [&](const GameState& s, int controller, const std::string& source,
            const std::vector<int>& legal, int min_targets,
            const std::vector<int>& heuristic_subset) -> std::vector<int>
        {
            // Build the ChosenTarget legal set + labels in canonical order.
            std::vector<ChosenTarget> legal_ct; std::vector<std::string> legal_labels;
            for (int t : legal)
            {
                legal_ct.push_back(sentinelToChosen(t, controller));
                if      (t == TARGET_OPP_FACE)  { legal_labels.push_back("Opponent (face)"); }
                else if (t == TARGET_SELF_FACE) { legal_labels.push_back("You (face)"); }
                else if (t >= 0 && t < static_cast<int>(s.battlefield.size()))
                {
                    const Permanent& p = s.battlefield[t];
                    const bool mine = (p.controller_index == controller);
                    legal_labels.push_back(p.card.m_name.str() + " (" + std::to_string(p.EffectivePower()) + "/"
                                           + std::to_string(p.EffectiveToughness()) + (mine ? ", yours)" : ")"));
                }
                else { legal_labels.push_back("?"); }
            }
            const int max_targets = static_cast<int>(legal.size());
            std::vector<TargetOption> opts = EnumerateTargetSets(legal_ct, legal_labels, max_targets, min_targets);
            if (opts.empty()) { return heuristic_subset; }   // defensive
            // Default index = the option whose target set matches the heuristic (floor) subset.
            std::vector<ChosenTarget> heur;
            for (int t : heuristic_subset) { heur.push_back(sentinelToChosen(t, controller)); }
            auto same = [](const std::vector<ChosenTarget>& a, const std::vector<ChosenTarget>& b) {
                if (a.size() != b.size()) { return false; }
                for (size_t i = 0; i < a.size(); ++i) { if (a[i].kind != b[i].kind || a[i].index != b[i].index) { return false; } }
                return true; };
            int heur_option = 0;
            for (size_t oi = 0; oi < opts.size(); ++oi) { if (same(opts[oi].targets, heur)) { heur_option = static_cast<int>(oi); break; } }
            auto toSentinels = [&](const std::vector<ChosenTarget>& ts) {
                std::vector<int> out; for (const ChosenTarget& c : ts) { out.push_back(chosenToSentinel(c, controller)); } return out; };
            int di = static_cast<int>(cursor);
            if (cursor < choices.size())
            {
                int chosen = choices[cursor++];
                ++decisions_made;
                if (chosen < 0 || chosen >= static_cast<int>(opts.size())) { chosen = heur_option; }
                if (!log_dir.empty())
                {
                    std::ostringstream ss;
                    ss << "{ \"chosen\": " << chosen << ", \"decision\": ";
                    WriteTargetDecisionJson(ss, s, source, legal_ct, legal_labels, opts, 0, max_targets, heur_option, di, "", "", min_targets, /*random_damage=*/true);
                    ss << "}";
                    trace.push_back(ss.str());
                }
                return toSentinels(opts[chosen].targets);
            }
            std::cout << "<<<CLAUDE_DECISION>>>\n";
            WriteTargetDecisionJson(std::cout, s, source, legal_ct, legal_labels, opts, 0, max_targets, heur_option, di, "", "", min_targets, /*random_damage=*/true);
            std::cout << "<<<END_DECISION>>>\n";
            std::cout.flush();
            std::exit(70);
        };
    g_play_soulfire_chooser = &soulfire_chooser;

    GameEngine engine(ai);
    int win_turn = engine.RunGame(state, max_turns);
    g_play_top_chooser = nullptr;
    g_play_target_chooser = nullptr;
    g_play_bounce_chooser = nullptr;
    g_play_sacrifice_chooser = nullptr;
    g_play_dig_chooser = nullptr;
    g_play_discard_chooser = nullptr;
    g_play_ei_chooser = nullptr;
    g_play_draw_sink = nullptr;
    g_play_event_sink = nullptr;
    bool won = win_turn > 0 && win_turn <= max_turns;

    // Mulligan reproducibility: the actual (count, bottomed-card-numbers) this game used. Recorded
    // into the reference so a later replay (--force-mulligan) reconstructs the exact opening hand
    // regardless of how the keep/bottoming heuristics change. See docs/design/claude-play-mulligan-*.
    std::ostringstream mull_ss;
    mull_ss << "{ \"count\": " << ai.LastMulliganCount() << ", \"bottom\": [";
    for (size_t i = 0; i < ai.LastBottomedNumbers().size(); ++i)
    { mull_ss << (i ? ", " : "") << ai.LastBottomedNumbers()[i]; }
    mull_ss << "] }";
    const std::string mulligan_json = mull_ss.str();

    // Game completed (every decision was supplied). Write the per-game trace if asked.
    if (!log_dir.empty())
    {
        std::error_code ec;
        std::filesystem::create_directories(log_dir, ec);
        std::ostringstream fn;
        fn << "claude_s" << seed << "_gi" << game_index << ".json";
        std::ofstream out(log_dir / fn.str());
        out << "{\n  \"seed\": " << seed << ", \"game_index\": " << game_index
            << ", \"win_turn\": " << (won ? win_turn : -1)
            << ", \"won\": " << (won ? "true" : "false")
            << ",\n  \"mulligan\": " << mulligan_json << ",\n  \"decisions\": [\n";
        for (size_t i = 0; i < trace.size(); ++i)
        {
            out << "    " << trace[i] << (i + 1 < trace.size() ? ",\n" : "\n");
        }
        out << "  ]\n}\n";
        std::cerr << "Claude-play trace written to " << (log_dir / fn.str()).string() << "\n";
    }

    // Final life totals (player 0 = goldfish, player 1 = opponent) so the GUI can show the
    // opponent at 0/negative on a win, making the lethal blow visible rather than freezing the
    // board at the pre-win life.
    // Include the FINAL board (the post-final-turn state) so the win screen shows the permanents
    // actually in play at the end -- including those played on the winning turn -- rather than
    // freezing on the last decision's board. Force the player's (player 0) perspective so "me" is
    // always the goldfish regardless of whose turn the game ended on.
    int saved_ap = state.active_player_index;
    state.active_player_index = 0;
    std::cout << "<<<CLAUDE_RESULT>>>\n{\n";
    WriteBoardContext(std::cout, state, 0);   // emits "me": {...}, "opponent": {...},
    state.active_player_index = saved_ap;
    // Events since the last decision -- crucially the WINNING turn's combat/damage/life swings, which
    // happen after the final chooser call and would otherwise be dropped (no next decision to carry them).
    if (!event_log.empty())
    {
        std::cout << "  \"events\": [";
        for (size_t ei = 0; ei < event_log.size(); ++ei)
        {
            if (ei) { std::cout << ", "; }
            std::cout << "{ \"turn\": " << event_log[ei].turn << ", \"kind\": ";
            JsonStr(std::cout, event_log[ei].kind);
            std::cout << ", \"text\": ";
            JsonStr(std::cout, event_log[ei].text);
            std::cout << " }";
        }
        std::cout << "],\n";
    }
    std::cout << "  \"win_turn\": " << (won ? win_turn : -1)
              << ", \"won\": " << (won ? "true" : "false")
              << ", \"decisions_made\": " << decisions_made
              << ", \"opponent_life\": " << state.players[1].life
              << ", \"player_life\": " << state.players[0].life
              << ", \"mulligan\": " << mulligan_json
              << "\n}\n<<<END_RESULT>>>\n";
    return 0;
}

// Plays a (post-mulligan) state to a win turn at the given lookahead depth.
// Takes state by value so the caller's copy is preserved for reuse.
// trace: enable per-pass candidate trace output for the T1 decision.
static int PlayOutWinTurn(GameState state, const MulliganProfile& profile,
                          int depth, int timeout_ms, int max_turns,
                          bool trace = false)
{
    TurnSolver::SetTraceSolve(trace);
    AIEngine   ai(profile, depth, timeout_ms);
    GameEngine engine(ai);
    int win_turn = engine.PlayOut(state, max_turns);
    TurnSolver::SetTraceSolve(false);
    return win_turn > 0 ? win_turn : max_turns + 1;
}

// Replays a post-mulligan state with a GameLogger attached, writing the log to log_path.
// Used by the depth-divergence diagnostic to record the actual game when d3 and d4 diverge.
static void PlayOutLogged(GameState state, const MulliganProfile& profile,
                          int depth, int timeout_ms, int max_turns,
                          uint64_t game_seed,
                          const std::map<std::string, std::vector<int>>& numbering,
                          const std::filesystem::path& log_path)
{
    GoldFishRunner::AssignCardNumbers(state, numbering);

    GameLogger logger;
    logger.StartGame("diag_d" + std::to_string(depth), 0, "d1", game_seed, numbering);

    AIEngine   ai(profile, depth, timeout_ms);
    ai.SetLogger(&logger);
    GameEngine engine(ai);
    engine.SetLogger(&logger);

    int win_turn = engine.PlayOut(state, max_turns);
    logger.EndGame(win_turn);
    logger.WriteToFile(log_path);
}

// Diagnostic: attribute the depth-4-worse-than-depth-3 (bottoming auto-on at depth>0)
// regression to its locus. For each game, run bottoming at depth 3 and depth 4
// (the keep decision is depth-independent, so both reach the same pre-bottom hand
// and identical library order — they differ only in which card bottoming chose).
// Then play the resulting state out at each depth, forming a 2x2:
//   W33 = bottom@3, play@3   W34 = bottom@3, play@4 (isolates main-phase depth)
//   W43 = bottom@4, play@3   W44 = bottom@4, play@4 (W43 isolates bottoming choice)
//
// Games are distributed evenly across num_threads (0 = hardware_concurrency).
// The budget is virtual/deterministic, so results are thread-invariant.
// Logging and trace work is serialised in a post-pass (bounded by MAX_EXAMPLES).
static void RunDepthDivergenceDiagnostic(const Decklist& deck, const MulliganProfile& profile,
                                         int num_games, uint64_t base_seed,
                                         int max_turns, int timeout_ms,
                                         int num_threads = 0,
                                         const std::filesystem::path& log_dir = {},
                                         bool trace_divergence = false)
{
    const int MAX_EXAMPLES = 10;

    bool logging = !log_dir.empty();
    std::map<std::string, std::vector<int>> numbering;
    if (logging)
    {
        numbering = GoldFishRunner::BuildCardNumbering(deck);
        std::filesystem::create_directories(log_dir);
    }

    // Per-game result; pre-allocated so threads write to disjoint indices — no mutex needed.
    struct GameResult
    {
        int W33 = 0, W34 = 0, W43 = 0, W44 = 0;
        bool bottom_differs    = false;
        bool mainphase_differs = false;
        bool mulliganed        = false;
        std::vector<std::string> hand3;
        std::vector<std::string> hand4;
    };
    std::vector<GameResult> results(num_games);

    // Thread count setup (mirrors GoldFishRunner).
    num_threads = concurrency_util::ResolveWorkerThreads(num_threads);
    num_threads = std::min(num_threads, num_games);

    int base_count = num_games / num_threads;
    int extra      = num_games % num_threads;
    int start      = 0;

    std::vector<std::thread> threads;
    threads.reserve(num_threads);

    for (int t = 0; t < num_threads; ++t)
    {
        int count        = base_count + (t < extra ? 1 : 0);
        int thread_start = start;
        start           += count;

        threads.emplace_back([&, thread_start, count]()
        {
            for (int li = 0; li < count; ++li)
            {
                int i = thread_start + li;
                uint64_t seed = base_seed + static_cast<uint64_t>(i);
                GameResult& gr = results[i];

                GameState s3 = GoldFishRunner::SetupGame(deck, seed);
                s3.vial_target_mv = profile.vial_target_mv;
                GoldFishRunner::PopulateOpponentSpawns(s3, i);
                AIEngine bot3(profile, 3, timeout_ms);
                bot3.HandleMulligan(s3, max_turns);
                gr.hand3 = SortedHandNames(s3);

                GameState s4 = GoldFishRunner::SetupGame(deck, seed);
                s4.vial_target_mv = profile.vial_target_mv;
                GoldFishRunner::PopulateOpponentSpawns(s4, i);
                AIEngine bot4(profile, 4, timeout_ms);
                bot4.HandleMulligan(s4, max_turns);
                gr.hand4 = SortedHandNames(s4);

                gr.bottom_differs = (gr.hand3 != gr.hand4);
                gr.mulliganed     = (static_cast<int>(gr.hand3.size()) < 7);

                gr.W33 = PlayOutWinTurn(s3, profile, 3, timeout_ms, max_turns);
                gr.W34 = PlayOutWinTurn(s3, profile, 4, timeout_ms, max_turns);
                gr.W43 = PlayOutWinTurn(s4, profile, 3, timeout_ms, max_turns);
                gr.W44 = PlayOutWinTurn(s4, profile, 4, timeout_ms, max_turns);

                gr.mainphase_differs = (gr.W34 != gr.W33);
            }
        });
    }

    for (std::thread& th : threads) { th.join(); }

    // Serial post-pass: accumulate counters and print examples in seed order.
    // Logging and tracing re-run the deterministic mulligan for each diverging game
    // rather than carrying full GameState copies through the parallel phase.
    int    bottom_diff     = 0;
    int    mainphase_diff  = 0;
    int    mulliganed      = 0;
    double sum33 = 0.0, sum34 = 0.0, sum43 = 0.0, sum44 = 0.0;
    int    examples_shown  = 0;
    int    mainphase_shown = 0;

    for (int i = 0; i < num_games; ++i)
    {
        const GameResult& gr = results[i];
        uint64_t seed = base_seed + static_cast<uint64_t>(i);

        if (gr.bottom_differs)    { ++bottom_diff; }
        if (gr.mainphase_differs) { ++mainphase_diff; }
        if (gr.mulliganed)        { ++mulliganed; }
        sum33 += gr.W33; sum34 += gr.W34; sum43 += gr.W43; sum44 += gr.W44;

        if (gr.mainphase_differs && mainphase_shown < MAX_EXAMPLES)
        {
            ++mainphase_shown;
            std::cout << "[mainphase] seed " << seed << "  W33=" << gr.W33
                      << " W34=" << gr.W34 << " (spawn pattern " << (i % 10) << ")  hand: ";
            for (const std::string& n : gr.hand3) { std::cout << n << " | "; }
            std::cout << "\n";

            if (logging || trace_divergence)
            {
                // Reconstruct the post-mulligan state for this seed (cheap + deterministic).
                GameState s3 = GoldFishRunner::SetupGame(deck, seed);
                s3.vial_target_mv = profile.vial_target_mv;
                GoldFishRunner::PopulateOpponentSpawns(s3, i);
                AIEngine bot3(profile, 3, timeout_ms);
                bot3.HandleMulligan(s3, max_turns);

                if (logging)
                {
                    std::string prefix = std::to_string(seed);
                    PlayOutLogged(s3, profile, 3, timeout_ms, max_turns, seed, numbering,
                                  log_dir / (prefix + "_play3.json"));
                    PlayOutLogged(s3, profile, 4, timeout_ms, max_turns, seed, numbering,
                                  log_dir / (prefix + "_play4.json"));
                    std::cout << "  -> logs: " << prefix << "_play3.json / " << prefix << "_play4.json\n";
                }
                if (trace_divergence)
                {
                    std::cerr << "\n=== T1 TRACE depth=3 (seed " << seed << ") ===\n";
                    PlayOutWinTurn(s3, profile, 3, timeout_ms, max_turns, /*trace=*/true);
                    std::cerr << "\n=== T1 TRACE depth=4 (seed " << seed << ") ===\n";
                    PlayOutWinTurn(s3, profile, 4, timeout_ms, max_turns, /*trace=*/true);
                }
            }
        }

        if (gr.bottom_differs && examples_shown < MAX_EXAMPLES)
        {
            ++examples_shown;
            std::cout << "[bottoming] seed " << seed << " (W33=" << gr.W33
                      << " W34=" << gr.W34 << " W43=" << gr.W43 << " W44=" << gr.W44 << ")\n";
            std::cout << "  bottom@3 keeps: ";
            for (const std::string& n : gr.hand3) { std::cout << n << " | "; }
            std::cout << "\n  bottom@4 keeps: ";
            for (const std::string& n : gr.hand4) { std::cout << n << " | "; }
            std::cout << "\n";

            if (logging || trace_divergence)
            {
                // Reconstruct both post-mulligan states (cheap + deterministic).
                GameState s3b = GoldFishRunner::SetupGame(deck, seed);
                s3b.vial_target_mv = profile.vial_target_mv;
                GoldFishRunner::PopulateOpponentSpawns(s3b, i);
                AIEngine bot3b(profile, 3, timeout_ms);
                if (trace_divergence)
                {
                    std::cerr << "\n=== BOTTOMING TRACE depth=3 (seed " << seed << ") ===\n";
                    TurnSolver::SetTraceSolve(true);
                }
                bot3b.HandleMulligan(s3b, max_turns);
                TurnSolver::SetTraceSolve(false);

                GameState s4b = GoldFishRunner::SetupGame(deck, seed);
                s4b.vial_target_mv = profile.vial_target_mv;
                GoldFishRunner::PopulateOpponentSpawns(s4b, i);
                AIEngine bot4b(profile, 4, timeout_ms);
                if (trace_divergence)
                {
                    std::cerr << "\n=== BOTTOMING TRACE depth=4 (seed " << seed << ") ===\n";
                    TurnSolver::SetTraceSolve(true);
                }
                bot4b.HandleMulligan(s4b, max_turns);
                TurnSolver::SetTraceSolve(false);

                if (logging)
                {
                    std::string prefix = std::to_string(seed);
                    PlayOutLogged(s3b, profile, 3, timeout_ms, max_turns, seed, numbering,
                                  log_dir / (prefix + "_bottom3_play3.json"));
                    PlayOutLogged(s4b, profile, 3, timeout_ms, max_turns, seed, numbering,
                                  log_dir / (prefix + "_bottom4_play3.json"));
                    std::cout << "  -> logs: " << prefix << "_bottom3_play3.json / "
                              << prefix << "_bottom4_play3.json\n";
                }
            }
        }
    }

    double n = static_cast<double>(num_games);
    std::cout << "\n=== DEPTH DIVERGENCE (" << num_games << " games, " << num_threads
              << " threads, budget " << timeout_ms << "ms, bottoming on at depth>0) ===\n";
    std::cout << "bottoming differs (d3 vs d4 kept hand): " << bottom_diff
              << " (" << (100.0 * bottom_diff / n) << "%)\n";
    std::cout << "main-phase differs (W34 != W33):        " << mainphase_diff
              << " (" << (100.0 * mainphase_diff / n) << "%)\n";
    std::cout << "mulliganed (kept < 7):                  " << mulliganed
              << " (" << (100.0 * mulliganed / n) << "%)\n";
    std::cout << "mean W33 (bottom@3 play@3): " << (sum33 / n) << "\n";
    std::cout << "mean W34 (bottom@3 play@4): " << (sum34 / n)
              << "   [main-phase effect (W34-W33): " << ((sum34 - sum33) / n) << "]\n";
    std::cout << "mean W43 (bottom@4 play@3): " << (sum43 / n)
              << "   [bottoming effect  (W43-W33): " << ((sum43 - sum33) / n) << "]\n";
    std::cout << "mean W44 (bottom@4 play@4): " << (sum44 / n)
              << "   [total             (W44-W33): " << ((sum44 - sum33) / n) << "]\n";
}

// Per-game ground-truth log: one "<game_index> <win_turn> <play_digest_hex>" line per game
// (win_turn <= 0 means no win within max_turns; the digest is GameLogger::Digest, 16 hex chars,
// a fingerprint of that game's exact decision stream). Written to <dir>/<name>.wins. These are the
// committed regression ground truth at the per-game level, so a later run can diff new logs against
// them to see EXACTLY which games changed -- in win TURN and/or in PLAY at the same win turn --
// without rebuilding the old binary. Older 2-column logs (no digest) still parse: the digest column
// is optional to every reader (regression.sh awk on $1/$2, audit read_wins on $1/$2).
static void WriteGameLog(const std::filesystem::path& dir, const std::string& name,
                         const std::vector<int>& win_turns,
                         const std::vector<uint64_t>& digests)
{
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    std::ofstream out(dir / (name + ".wins"));
    char buf[17];
    for (int gi = 0; gi < static_cast<int>(win_turns.size()); ++gi)
    {
        out << gi << ' ' << win_turns[gi];
        if (gi < static_cast<int>(digests.size()))
        {
            std::snprintf(buf, sizeof(buf), "%016llx", static_cast<unsigned long long>(digests[gi]));
            out << ' ' << buf;
        }
        out << '\n';
    }
}

// Scenario mode: run ONE constructed board through the real AI turn engine and report the outcome.
// Unlike seed-driven goldfish, this hand-builds the battlefield/hand so a specific interaction (e.g.
// "tap a land, not the dork you want to pump and swing") can be reproduced deterministically and
// used as a regression fixture. See docs/design/scenario-harness.md / test/scenarios/*.json.
//
//   mtg --scenario <scenario.json>
//
// The JSON:
//   { "deck": "decks/X.cod",          // provider selection + profile auto-detect only (NOT shuffled in)
//     "profile": "decks/X.profile.json",         // optional; auto-detected from deck if omitted
//     "turn": 4, "on_the_play": true,            // turn to run; state runs THROUGH this turn
//     "active_life": 20, "opponent_life": 5,
//     "battlefield": [ { "name": "Birds of Paradise", "controller": 0, "tapped": false, "sick": false }, ... ],
//     "hand": [ "Aria of Flame", "Invigorate" ],
//     "library_filler": "Forest", "library_size": 40,   // so draws / rollouts don't run dry
//     "depth": 5, "budget_ms": 100, "max_turns": 4,
//     "expect_win_turn": 4,          // optional: nonzero exit if the actual win turn is later (a FAIL)
//     "expect_no_win": true,         // optional: nonzero exit if the engine DID win (negative guard)
//     "log_out": "logs/play/scenario.json" }            // optional: write the per-turn trace
static int RunScenario(const std::filesystem::path& scenario_path)
{
    using json = nlohmann::json;
    std::ifstream in(scenario_path);
    if (!in) { std::cerr << "scenario: cannot open " << scenario_path << "\n"; return 2; }
    json j;
    try { in >> j; }
    catch (const std::exception& e) { std::cerr << "scenario: bad JSON: " << e.what() << "\n"; return 2; }

    const std::string cards_json = j.value("cards_json", std::string("src/cards/data/cards.json"));
    if (std::filesystem::exists(cards_json)) { CardDatabase::Instance().LoadFromJson(cards_json); }
    else { std::cerr << "scenario: cards.json not found at " << cards_json << "\n"; return 2; }

    if (!j.contains("deck")) { std::cerr << "scenario: missing \"deck\"\n"; return 2; }
    const std::filesystem::path deck_path = j.at("deck").get<std::string>();
    Decklist deck = DeckLoader::LoadFromFile(deck_path);

    std::filesystem::path profile_path = j.value("profile", std::string(""));
    if (profile_path.empty())
    { profile_path = deck_path.parent_path() / (deck_path.stem().string() + ".profile.json"); }
    MulliganProfile profile;
    if (std::filesystem::exists(profile_path)) { profile = LoadDeckProfile(profile_path); }
    AttachExhaustiveSidecar(profile, profile_path);  // play uses the deck's exhaustive sidecar if present
    AttachEvalSidecar(profile, profile_path);        // ... and its learned mid-game eval sidecar if present

    const int depth     = j.value("depth", 5);
    const int budget_ms = j.value("budget_ms", 100);
    const int turn      = j.value("turn", 4);
    const int max_turns = j.value("max_turns", turn);

    // Resolve a full Card (P/T, cost, types, keywords) from its name -- the definition's own card, the
    // same object play/cast copies onto a Permanent. Placeholder-by-name would be 0/0 with no cost.
    auto make_card = [](const std::string& name) -> Card {
        const CardDefinition* d = CardDatabase::Instance().Lookup(name);
        if (!d) { throw std::runtime_error("scenario: unknown card \"" + name + "\""); }
        Card c = d->card; c.RehashName(); return c;
    };

    GameState state;
    state.m_provider            = &SelectDecisionProvider(deck);
    state.players[0].life       = j.value("active_life", 20);
    state.players[1].life       = j.value("opponent_life", 20);
    state.active_player_index   = 0;
    state.priority_player_index = 0;
    state.turn_number           = turn - 1;   // PlayOut steps INTO `turn`
    state.on_the_play           = j.value("on_the_play", true);
    state.game_seed             = j.value("seed", 1);

    try
    {
        for (const auto& e : j.value("battlefield", json::array()))
        {
            Permanent p;
            p.card              = make_card(e.at("name").get<std::string>());
            p.controller_index  = e.value("controller", 0);
            p.owner_index       = p.controller_index;
            p.tapped            = e.value("tapped", false);
            p.entered_this_turn = e.value("sick", false);   // false => can attack (not summoning sick)
            state.battlefield.push_back(p);
        }
        for (const auto& hc : j.value("hand", json::array()))
        { state.players[0].hand.push_back(make_card(hc.get<std::string>())); }

        // Filler library so the draw step and lookahead rollouts have cards to draw. Lands by default
        // => the drawn cards are inert and don't perturb the interaction under test.
        const std::string filler = j.value("library_filler", std::string("Forest"));
        const int lib = j.value("library_size", 40);
        for (int i = 0; i < lib; ++i) { state.players[0].library.push_back(make_card(filler)); }
    }
    catch (const std::exception& e) { std::cerr << e.what() << "\n"; return 2; }

    const std::map<std::string, std::vector<int>> numbering = GoldFishRunner::BuildCardNumbering(deck);
    GameLogger logger;
    logger.StartGame("scenario", 0, deck_path.stem().string(), state.game_seed, numbering);
    AIEngine   ai(profile, depth, budget_ms);
    ai.SetLogger(&logger);
    GameEngine engine(ai);
    engine.SetLogger(&logger);

    const int win_turn = engine.PlayOut(state, max_turns);
    logger.EndGame(win_turn);

    const std::string log_out = j.value("log_out", std::string(""));
    if (!log_out.empty())
    {
        std::filesystem::create_directories(std::filesystem::path(log_out).parent_path());
        logger.WriteToFile(log_out);
    }

    const bool won = (win_turn > 0 && win_turn <= max_turns);
    std::cout << "scenario: win_turn=" << (won ? std::to_string(win_turn) : std::string("none"))
              << " opponent_life=" << state.players[1].life
              << " active_life="   << state.players[0].life
              << (log_out.empty() ? "" : (" log=" + log_out)) << "\n";

    // Optional NEGATIVE assertion: fail (exit 1) if the engine DID win, when the fixture asserts it
    // must not (e.g. Invigorate must NOT auto-fire and gift life when it is not lethal, or when no
    // legal target exists). Guards a conditional fire against becoming too eager.
    if (j.value("expect_no_win", false))
    {
        if (won)
        {
            std::cout << "scenario: FAIL expected NO win, got win by turn " << win_turn << "\n";
            return 1;
        }
        std::cout << "scenario: PASS (no win, as expected)\n";
        return 0;
    }

    // Optional assertion: fail (exit 1) if the win came later than expected (or not at all).
    if (j.contains("expect_win_turn"))
    {
        const int exp = j.at("expect_win_turn").get<int>();
        if (!won || win_turn > exp)
        {
            std::cout << "scenario: FAIL expected win by turn " << exp
                      << ", got " << (won ? std::to_string(win_turn) : std::string("no win")) << "\n";
            return 1;
        }
        std::cout << "scenario: PASS (win by turn " << exp << ")\n";
    }
    return 0;
}

int main(int argc, char* argv[])
{
    if (argc < 2)
    {
        PrintUsage(argv[0]);
        return 1;
    }

    // Scenario mode: one hand-built board through the AI turn engine (see RunScenario).
    //   mtg --scenario <scenario.json>
    if (std::string(argv[1]) == "--scenario")
    {
        if (argc < 3) { std::cerr << "usage: " << argv[0] << " --scenario <scenario.json>\n"; return 2; }
        try { return RunScenario(argv[2]); }
        catch (const std::exception& e) { std::cerr << "scenario error: " << e.what() << "\n"; return 2; }
    }

    // Batch mode: pool every game from every job in a manifest into one work queue.
    //   mtg.exe --batch <manifest.json> [--threads N] [--cards-json P]
    if (std::string(argv[1]) == "--batch")
    {
        std::filesystem::path manifest;
        std::filesystem::path cards_json  = "src/cards/data/cards.json";
        std::filesystem::path game_log_dir;
        std::filesystem::path game_trace_dir;
        bool                  gate_probe = false;
        int                   num_threads = 0;
        for (int i = 2; i < argc; ++i)
        {
            std::string flag = argv[i];
            if (flag == "--threads"    && i + 1 < argc) { num_threads = std::stoi(argv[++i]); }
            else if (flag == "--cards-json" && i + 1 < argc) { cards_json = argv[++i]; }
            else if (flag == "--game-log-dir" && i + 1 < argc) { game_log_dir = argv[++i]; }
            else if (flag == "--game-trace-dir" && i + 1 < argc) { game_trace_dir = argv[++i]; }
            else if (flag == "--gate-probe")            { gate_probe = true; }
            else if (manifest.empty())                  { manifest = flag; }
        }
        if (gate_probe) { SetGateProbe(true); }
        if (manifest.empty())
        {
            std::cerr << "Usage: " << argv[0]
                      << " --batch <manifest.json> [--threads N] [--cards-json P]"
                         " [--game-log-dir D] [--game-trace-dir D]\n";
            return 1;
        }
        try
        {
            if (std::filesystem::exists(cards_json))
            {
                CardDatabase::Instance().LoadFromJson(cards_json);
            }
            std::cout << "=== BATCH (streaming per-job results as each job finishes) ===\n"
                      << std::flush;
            // Stream each job's line the moment it completes (jobs arrive in completion
            // order, not manifest order). Flush so progress is visible live rather than
            // buffered until the whole batch ends.
            auto on_job_done = [&](const BatchJobResult& r)
            {
                double pct = r.games_played > 0
                             ? 100.0 * r.games_won / r.games_played : 0.0;
                char dbuf[17];
                std::snprintf(dbuf, sizeof(dbuf), "%016llx",
                              static_cast<unsigned long long>(r.case_digest));
                std::cout << r.name << ": played=" << r.games_played
                          << " won=" << r.games_won << " (" << pct << "%)"
                          << " avg=" << r.average_win_turn
                          << " digest=" << dbuf << "\n" << std::flush;
                if (!game_log_dir.empty())
                {
                    WriteGameLog(game_log_dir, r.name, r.win_turns, r.digests);
                }
            };
            std::vector<BatchJobResult> results =
                BatchRunner::RunManifest(manifest, num_threads, on_job_done, game_trace_dir);
            int total_games = 0;
            for (const BatchJobResult& r : results) { total_games += r.games_played; }
            std::cout << "=== BATCH done (" << results.size() << " jobs, "
                      << total_games << " games) ===\n" << std::flush;
            if (gate_probe)
            {
                const uint32_t live = QueriedGatesMask();
                std::string live_s, dead_s;
                for (int i = 0; i < static_cast<int>(UnprunedGate::_Count); ++i)
                {
                    const char* nm = GateName(static_cast<UnprunedGate>(i));
                    ((live >> i) & 1u ? live_s : dead_s).append(nm).append(" ");
                }
                std::cout << "=== GATE PROBE ===\n  live (sweep these): " << live_s
                          << "\n  dead (skip these):  " << dead_s << "\n" << std::flush;
            }
        }
        catch (const std::exception& e)
        {
            std::cerr << "Error: " << e.what() << "\n";
            return 1;
        }
        return 0;
    }

    std::filesystem::path deck_path    = argv[1];
    std::filesystem::path cards_json   = "src/cards/data/cards.json";
    std::filesystem::path profile_path;
    std::filesystem::path log_dir;
    int      num_games      = 10000;
    int      max_turns      = 8;   // goldfish horizon: wins on turn >8 are not useful data (a
                                   // real game is lost by then; goldfishing can't model control).
                                   // Bounding it also stops the search exploring deep no-early-win
                                   // lines -- override with --max-turns for a genuinely slow deck.
    int      base_game_index = 0;
    int      lookahead_depth = 0;
    int      timeout_ms     = 0;
    int      num_threads    = 0;
    uint64_t seed           = 0;
    bool     seed_provided  = false;
    bool     diag_depth     = false;
    bool     trace_t1       = false;
    bool        claude_play = false;
    std::string choices_str;          // comma-separated plan indices for --claude-play
    int         reveal_count = 0;     // --reveal N: expose top N upcoming draws (claude-play)
    std::string validate_line;        // --validate-line "<spec>": human-play line to reconcile
                                       // at the first un-chosen main phase (tools/play GUI)
    std::string force_mulligan;       // --force-mulligan "<count>:<n1,n2,...>": reconstruct a
                                       // reference's opening hand (claude-play replay), see below
    // Keep-decision probe (--eval-hand "n1;n2;..."): run the EXACT runtime keep predicate
    // (KeepHand, which consults the loaded profile's keep_model) on one constructed opening
    // hand and print keep/mull. Names are ';'-separated (MTG names contain spaces/commas) and
    // must appear in the deck. Used to A/B a keep-model vs static on specific marginal hands
    // (e.g. the 2-lands + 4-Vials hand) without playing a game. --eval-mull/--eval-draw vary
    // the mulligan count and play/draw the decision is asked under.
    std::string eval_hand;
    int         eval_mull    = 0;      // --eval-mull N (London mulligan count so far)
    bool        eval_on_play = true;   // --eval-draw flips to on-the-draw

    for (int i = 2; i < argc; ++i)
    {
        std::string flag = argv[i];
        if (flag == "--diag-depth")          { diag_depth = true; continue; }
        if (flag == "--trace")               { trace_t1 = true; continue; }
        if (flag == "--claude-play")         { claude_play = true; continue; }
        if (flag == "--eval-draw")           { eval_on_play = false; continue; }
        try
        {
            if (i + 1 < argc)
            {
                if (flag == "--games")
                {
                    num_games = std::stoi(argv[++i]);
                }
                else if (flag == "--seed")
                {
                    seed          = std::stoull(argv[++i]);
                    seed_provided = true;
                }
                else if (flag == "--max-turns")
                {
                    max_turns = std::stoi(argv[++i]);
                }
                else if (flag == "--profile")
                {
                    profile_path = argv[++i];
                }
                else if (flag == "--log-dir")
                {
                    log_dir = argv[++i];
                }
                else if (flag == "--game-index")
                {
                    base_game_index = std::stoi(argv[++i]);
                }
                else if (flag == "--choices")
                {
                    choices_str = argv[++i];
                }
                else if (flag == "--reveal")
                {
                    reveal_count = std::stoi(argv[++i]);
                }
                else if (flag == "--validate-line")
                {
                    validate_line = argv[++i];
                }
                else if (flag == "--force-mulligan")
                {
                    // Mulligan reproducibility (claude-play replay): "<count>:<n1,n2,...>" forces
                    // the engine to keep at <count> mulligans and bottom the listed card numbers,
                    // reconstructing a reference's exact opening hand independent of the heuristics.
                    force_mulligan = argv[++i];
                }
                else if (flag == "--eval-hand")
                {
                    eval_hand = argv[++i];
                }
                else if (flag == "--eval-mull")
                {
                    eval_mull = std::stoi(argv[++i]);
                }
                else if (flag == "--depth")
                {
                    lookahead_depth = std::stoi(argv[++i]);
                }
                else if (flag == "--timeout-ms" || flag == "--budget-ms")
                {
                    // Deterministic search budget in "virtual ms" (see SearchBudget).
                    // --timeout-ms kept as a back-compat alias for the same knob.
                    timeout_ms = std::stoi(argv[++i]);
                }
                else if (flag == "--threads")
                {
                    num_threads = std::stoi(argv[++i]);
                }
                else if (flag == "--cards-json")
                {
                    cards_json = argv[++i];
                }
            }
        }
        catch (...)
        {
            std::cerr << "Invalid value for " << flag << ": " << argv[i] << "\n";
            return 1;
        }
    }

    if (!seed_provided)
    {
        std::random_device rd;
        seed = (static_cast<uint64_t>(rd()) << 32) | rd();
    }

    try
    {
        if (std::filesystem::exists(cards_json))
        {
            CardDatabase::Instance().LoadFromJson(cards_json);
        }

        Decklist deck = DeckLoader::LoadFromFile(deck_path);
        std::cout << "Loaded " << deck.mainboard.size() << " mainboard card(s)";
        if (!deck.sideboard.empty())
        {
            std::cout << " + " << deck.sideboard.size() << " sideboard card(s)";
        }
        std::cout << "\n";

        // Auto-detect deckname.profile.json if no explicit --profile was given.
        if (profile_path.empty())
        {
            profile_path = deck_path.parent_path()
                         / (deck_path.stem().string() + ".profile.json");
        }

        MulliganProfile profile;
        if (std::filesystem::exists(profile_path))
        {
            profile = LoadDeckProfile(profile_path);
            std::cerr << "Loaded profile from " << profile_path.string() << "\n";
        }
        AttachExhaustiveSidecar(profile, profile_path);  // play uses the deck's exhaustive sidecar if present
        AttachEvalSidecar(profile, profile_path);        // ... and its learned mid-game eval sidecar if present

        // --eval-hand: one keep/mull decision on a constructed hand via the runtime predicate.
        // Prints "KEEP"/"MULLIGAN" for the given (hand, mull, play/draw) so a keep-model can be
        // A/B'd against static on specific marginal hands. Names must be in the deck (we reuse the
        // real Card instances so all fields are populated exactly as in a real game).
        if (!eval_hand.empty())
        {
            std::vector<Card> hand;
            std::vector<std::string> missing;
            std::stringstream hs(eval_hand);
            std::string nm;
            while (std::getline(hs, nm, ';'))
            {
                size_t b = nm.find_first_not_of(" \t");
                size_t e = nm.find_last_not_of(" \t");
                if (b == std::string::npos) { continue; }
                nm = nm.substr(b, e - b + 1);
                const Card* found = nullptr;
                for (const Card& c : deck.mainboard) { if (c.m_name.str() == nm) { found = &c; break; } }
                if (found) { hand.push_back(*found); }
                else       { missing.push_back(nm); }
            }
            if (!missing.empty())
            {
                std::cerr << "Error: card(s) not in deck: ";
                for (size_t i = 0; i < missing.size(); ++i) { std::cerr << (i ? ", " : "") << missing[i]; }
                std::cerr << "\n";
                return 1;
            }
            AIEngine ai(profile, lookahead_depth, timeout_ms);
            bool keep = ai.ReferenceKeep(hand, eval_mull, eval_on_play);
            std::vector<std::string> names;
            for (const Card& c : hand) { names.push_back(c.m_name.str()); }
            std::sort(names.begin(), names.end());
            std::cout << (keep ? "KEEP" : "MULLIGAN")
                      << "  mull=" << eval_mull
                      << "  " << (eval_on_play ? "on-the-play" : "on-the-draw")
                      << "  hand=[";
            for (size_t i = 0; i < names.size(); ++i) { std::cout << (i ? ", " : "") << names[i]; }
            std::cout << "]\n";
            return 0;
        }

        if (diag_depth)
        {
            RunDepthDivergenceDiagnostic(deck, profile, num_games, seed, max_turns, timeout_ms,
                                         num_threads, log_dir, trace_t1);
            return 0;
        }

        if (claude_play)
        {
            // Human-play mode unrestricts the search's choice heuristics: the human should be
            // able to pick ANY legal tutor/fetch target, X value, Ponder keep/shuffle, dig, and
            // un-gated draw-engine/alt-payload cast -- not just the heuristic-narrowed one. These
            // standing A/B switches widen exactly those gates (see DecisionUnpruned / the order
            // and Ponder enumeration). setenv before any TurnSolver call so the cached getenv
            // reads pick them up. Anything still narrowed is fixed if it turns out to matter.
            setenv("MTG_UNPRUNED", "1", 1);
            setenv("MTG_PONDER_SEARCH", "1", 1);
            // Human play executes EXACTLY the committed plan -- no auto re-solve after a draw, no
            // auto-dig, no auto Land's Edge. Instead the chooser re-fires after any draw so the
            // human re-decides with the revealed cards (a draw "breakpoint"). See ApplyPlanDirect
            // (gated on MTG_HUMAN_PLAY) and AIEngine's external-chooser segment loop.
            setenv("MTG_HUMAN_PLAY", "1", 1);

            std::vector<int> choices;
            std::stringstream ss(choices_str);
            std::string tok;
            while (std::getline(ss, tok, ','))
            {
                if (!tok.empty()) { choices.push_back(std::stoi(tok)); }
            }
            return RunClaudePlay(deck, profile, seed, base_game_index, max_turns,
                                 lookahead_depth, timeout_ms, choices, reveal_count, log_dir,
                                 validate_line, force_mulligan);
        }

        GoldFishRunner runner;
        RunResult result = runner.Run(deck, num_games, seed, max_turns, profile, log_dir,
                                       base_game_index, lookahead_depth, timeout_ms, num_threads);

        std::cout << "Seed         : " << result.seed << "\n";
        std::cout << "Games played : " << result.games_played << "\n";
        std::cout << "Games won    : " << result.games_won
                  << " (" << (100.0 * result.games_won / result.games_played) << "%)\n";
        if (result.games_won > 0)
        {
            std::cout << "Avg win turn : " << result.average_win_turn << "\n";
        }
        else
        {
            std::cout << "No wins recorded.\n";
        }

        int losses = result.games_played - result.games_won;
        if (losses > 0)
        {
            std::cout << "Losses (" << losses << "):\n";
            for (int i = 0; i < static_cast<int>(result.win_turns.size()); ++i)
            {
                if (result.win_turns[i] <= 0)
                {
                    std::cout << "  game " << i
                              << "  seed " << (result.seed + static_cast<uint64_t>(i)) << "\n";
                }
            }
        }

        if (!log_dir.empty())
        {
            std::cerr << "Game logs written to " << log_dir.string() << "\n";
        }

        PROF_REPORT(std::cerr);
    }
    catch (const std::exception& e)
    {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }

    return 0;
}
