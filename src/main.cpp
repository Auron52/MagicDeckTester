#include "core/EnvFlags.h"
#include <bit>            // std::popcount -- portable replacement for __builtin_popcount (MSVC has no such builtin)
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
#include <map>
#include <set>            // --tap-pref (reference-replay payment-tap preference)
#include "deck/DeckLoader.h"
#include "cards/CardDatabase.h"
#include "core/HeuristicDefaults.h"
#include "core/FlagRegistry.h"
#include "runner/GoldFishRunner.h"
#include "runner/BatchRunner.h"
#include "ai/AIEngine.h"
#include "ai/TurnSolver.h"
#include "core/GameEngine.h"
#include "core/OpponentDeck.h"
#include "core/GameLogger.h"
#include "core/SpellEffects.h"   // LookKind / TopDisposition / EnumerateTopDispositions for look-top decisions
#include "core/HardwareConcurrency.h"
#include "ai/MulliganProfileIO.h"
#include "ai/Profiler.h"
#include "ai/DecisionProviders.h"   // SelectDecisionProvider for --scenario
#include "ai/ManaPayment.h"      // CastOrderKey for the --cast-order-report sort
#include <nlohmann/json.hpp>        // --scenario board spec

static void PrintUsage(const char* prog)
{
    std::cerr << "Usage: " << prog
              << " <deckfile> [--games N] [--seed S] [--max-turns T]"
                 " [--depth D] [--budget-ms M] [--profile path] [--log-dir path] [--cards-json path]\n"
              << "  <deckfile>      Plain text (.txt) or Cockatrice (.cod) decklist\n"
              << "  --games N       Number of games to simulate (default: 10000)\n"
              << "  --seed S        Base RNG seed (omit to generate randomly)\n"
              << "  --max-turns T   Maximum turns before declaring no-win (default: 8)\n"
              << "  --depth D       Lookahead depth (higher = stronger but slower). Omit --depth AND\n"
              << "                  --budget-ms to use the deck's value_play play policy (or the built-in\n"
              << "                  d5/budget-20 default). Passing either reproduces the old CLI behavior.\n"
              << "  --budget-ms M   Per-decision search budget in deterministic 'virtual ms';\n"
              << "                  0 = unlimited (default: 0). Alias: --timeout-ms\n"
              << "  --ignore-play-profile  Bypass a deck's derived value_play lock so an explicit\n"
              << "                  --depth/--budget-ms can be used for A/B (else that combination errors)\n"
              << "  --threads N     Worker threads (default: 0 = auto, affinity-based CPU count)\n"
              << "  --profile P     Path to a .profile.json file (default: auto-detect deckname.profile.json)\n"
              << "  --log-dir P     Write one JSON game log per game into this directory\n"
              << "  --exhaustive-keep  Load the exhaustive keep sidecar under --claude-play too\n"
              << "                  (claude-play skips it by default; it costs ~68s/launch to parse\n"
              << "                  and mulligan optimality is irrelevant to play verification)\n"
              << "  --cards-json P  Path to card definitions JSON (default: src/cards/data/cards.json)\n"
              << "  --list-flags    Print every MTG_* env flag this build reads (the registry the\n"
                 "                  startup unknown-flag warning checks against) and exit\n";
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
    struct Row { std::string name; bool is_land; bool is_le; std::vector<Cnt> counters; int idx; bool tapped;
                 int num; bool is_aura; bool is_equip; int attached_to; };
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
        if (p.storage_counters) { cs.push_back({ "storage",   "storage",   p.storage_counters }); }
        // Cumulative upkeep AGE counters (Varchild's War-Riders): the cost SCALES with this count
        // (one opponent Survivor token per age counter, every upkeep), so it is the single number
        // that explains why the drain accelerates -- it belongs on the board like every other
        // counter kind (viewer issue #12). Additive display field; absent when zero.
        if (p.age_counters)     { cs.push_back({ "age",       "age",       p.age_counters }); }
        // num = stable per-copy id; is_aura/is_equip + attached_to let the viewer draw an Aura or
        // an attached Equipment overlapping the creature (m_number) it enchants/equips
        // (0 = unattached). Additive display fields (equipment added 2026-08-14 -- attached
        // equipment previously rendered free-floating).
        bool is_aura  = d && d->params.is_aura;
        bool is_equip = d && d->params.is_equipment;
        const int att = p.aura_attached_to > 0 ? p.aura_attached_to : p.equipped_to;
        rows.push_back({ p.card.m_name, p.card.IsLand(), is_le, std::move(cs), pi, p.tapped,
                         p.card.m_number, is_aura, is_equip, att });
    }
    std::sort(rows.begin(), rows.end(),
              [](const Row& a, const Row& b){ return a.name < b.name; });
    os << '[';
    for (size_t i = 0; i < rows.size(); ++i)
    {
        if (i) { os << ", "; }
        os << "{ \"name\": "; JsonStr(os, rows[i].name);
        os << ", \"idx\": " << rows[i].idx;
        os << ", \"num\": " << rows[i].num;
        if (rows[i].tapped) { os << ", \"tapped\": true"; }
        if (rows[i].is_aura)  { os << ", \"is_aura\": true"; }
        if (rows[i].is_equip) { os << ", \"is_equip\": true"; }
        if (rows[i].attached_to > 0) { os << ", \"attached_to\": " << rows[i].attached_to; }
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
// Resolve a creature's m_number (an aura's enchant_target) to its card name for display, so two
// otherwise-identical aura casts read as distinct plans ("Rancor -> Kor Spiritdancer" vs "-> Bogle").
static std::string EnchantTargetName(const GameState& s, int m_number)
{
    // Any permanent you control, not only a creature: an "Enchant land" aura's host is a LAND, and
    // restricting this to creatures made every land-aura plan read "-> #31" instead of "-> Kitchen"
    // -- which is precisely the choice the player is being asked to make (WHICH land carries it).
    for (const Permanent& p : s.battlefield)
        if (p.controller_index == s.active_player_index && p.card.m_number == m_number)
        { return p.card.m_name.str(); }
    // A same-turn creature target (cast this turn to carry the Aura) is still in hand at enumeration, so
    // resolve its name there too -- else the plan reads "-> #38" instead of "-> Light-Paws".
    for (const Card& c : s.ActivePlayer().hand)
        if (c.m_number == m_number)
        { return c.m_name.str(); }
    return "#" + std::to_string(m_number);
}

static std::string SummarizePlan(const TurnSolver::Plan& plan, const GameState& s)
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
            case Action::Kind::CastFromHand:
                tag = a.card_name;
                if (a.enchant_target > 0) { tag += " \xE2\x86\x92 " + EnchantTargetName(s, a.enchant_target); }
                // The no-own-creature "cash the cantrip off their body" variant (kTrickOpponentTarget).
                else if (a.enchant_target == kTrickOpponentTarget)
                { tag += " \xE2\x86\x92 opponent's creature"; }
                break;
            case Action::Kind::CastFromGraveyard: tag = a.card_name + " (retrace)"; break;
            case Action::Kind::ActivateVial:      tag = a.card_name + " (vial)"; break;
            case Action::Kind::PlayLand:          tag = a.card_name + " (land)"; break;
            case Action::Kind::DigDraw:
                tag = (a.dig_sacrifice ? "sacrifice " : "cycle ") + a.card_name + " to draw"; break;
            // Battlefield activations (FiveColour): label as abilities, not bare card names --
            // a bare name reads as a CAST of a card that is not in hand (it confused the
            // play_invariants hand check and any human reading the plan list).
            case Action::Kind::GarthActivate:
                tag = a.card_name + ": conjure " + a.tutor_target; break;
            // "loyalty#1" is a raw index into cards.json -- meaningless to a player, and identical
            // in shape for every walker. Print the ability the way the CARD does, and name the
            // preselected target when the ability has one (the human re-picks it on the board at
            // resolution, so this is a default, not a commitment).
            case Action::Kind::ActivateLoyalty:
                tag = LoyaltyActionLabel(a.card_name.str(), a.loyalty_ability);
                if (a.enchant_target > 0) { tag += " \xE2\x86\x92 " + EnchantTargetName(s, a.enchant_target); }
                break;
            case Action::Kind::Equip:
                tag = "equip " + a.card_name + " \xE2\x86\x92 " + EnchantTargetName(s, a.sac_victim_id);
                break;
            case Action::Kind::GraveyardExileAbility:
                tag = a.card_name + (a.gy_exile_mode == 1 ? ": exile instant/sorcery (drain 2)"
                                                          : ": exile creature (gain 2)");
                break;
            case Action::Kind::GraveyardReturnAbility:
                // One plan variant per legal target, so the label must name WHICH card comes back.
                tag = a.card_name + ": sacrifice \xE2\x86\x92 return " + a.tutor_target + " to hand";
                break;
            case Action::Kind::AttachAllEquipment:
                tag = a.card_name + ": attach all Equipment"; break;
            case Action::Kind::PutFromHandAbility:
                tag = "put " + a.card_name + " onto battlefield (Stoneforge)"; break;
            case Action::Kind::JitteModeAbility:
            {
                // Mode 3 (+2/+2) names no victim -- it pumps the EQUIPPED creature -- and carries a
                // repeat COUNT on chosen_x, which is the only thing telling a 1-counter spend apart
                // from a 3-counter one in the menu (the sac-outlet count lesson).
                std::string jm = ": gain 2 life";
                if (a.gy_exile_mode == 1)
                { jm = ": -1/-1 \xE2\x86\x92 " + EnchantTargetName(s, a.sac_victim_id); }
                else if (a.gy_exile_mode == 3)
                {
                    // No "-> <host>": the pump has no target -- it is ALWAYS the equipped creature
                    // (USER 2026-08-27: naming one read as an allocation choice that doesn't exist).
                    const int reps = std::max(1, a.chosen_x);
                    jm = ": +2/+2" + (reps > 1 ? " x" + std::to_string(reps) : std::string{});
                }
                tag = a.card_name + jm;
                break;
            }
            // Sac outlets (Skirk Prospector "Sacrifice a Goblin: Add {R}", Siege-Gang, Pashalik):
            // a battlefield ACTIVATION, and the sac COUNT is the whole difference between two
            // otherwise identical menu entries -- a 1-sac and a 3-sac Skirk read the same without
            // it, which is how the human ended up unable to tell them apart (viewer issue #4).
            case Action::Kind::SacForMana:
            case Action::Kind::SacCreatureOutlet:
            {
                const int k = std::max(1, a.sac_count);
                // SacForMana sacrifices the SOURCE ITSELF (a Lotus Bloom / Treasure Token -- an
                // artifact, not a creature); only SacCreatureOutlet feeds creatures to an outlet.
                // The shared "creature(s)" suffix mislabeled a Treasure crack as "sac 1 creature"
                // (three independent 2026-08-11 Mirrorwing sweep agents flagged it). Text-only.
                tag = (a.kind == Action::Kind::SacForMana)
                    ? a.card_name + ": sacrifice"
                    : a.card_name + ": sac " + std::to_string(k)
                    + (k == 1 ? " creature" : " creatures");
                if (a.kind == Action::Kind::SacForMana && !a.chosen_float_color.empty())
                { tag += " for {" + a.chosen_float_color + "}\xC3\x97" + std::to_string(std::max(1, a.ritual_float)); }
                else if (a.direct_damage > 0) { tag += " \xE2\x86\x92 " + std::to_string(a.direct_damage) + " damage"; }
                break;
            }
            // Main-phase activated pumps (Minotaur). Without a case here these fell to the
            // "(other)" default and read to the human as a CAST of the card -- Burning-Fist's
            // two-activation pump rendered as "Burning-Fist Minotaur (other)", indistinguishable
            // from casting it, and with no hint that it DISCARDS two cards. The activation count
            // is the whole difference between two otherwise identical menu entries, exactly as
            // for the sac outlets above (viewer issue #4's lesson).
            // Blink outlet (Eldrazi Displacer / Emiel the Blessed). The TARGET and the COUNT are
            // the whole decision, and without them every blink variant renders as an identical
            // "<name> (other)" -- a Stage 5d agent worked around it by declining every blink option
            // rather than gamble, which for a deck whose engine IS a targeted repeatable activation
            // makes the sweep unable to verify the combo at all. sac_victim_id holds the target's
            // m_number; EnchantTargetName resolves battlefield and hand alike.
            case Action::Kind::ActivateBlink:
            {
                tag = a.card_name + ": blink " + EnchantTargetName(s, a.sac_victim_id);
                const int bk = std::max(1, a.chosen_x);
                if (bk > 1) { tag += " x" + std::to_string(bk); }
                break;
            }
            case Action::Kind::ActivatePermAbility:
                tag = a.card_name + ": " + PermAbilityLabel(a.ability_mode);
                break;
            case Action::Kind::ActivatePump:
            {
                const int k = std::max(1, a.chosen_x);
                if (a.gy_exile_mode == 1)
                {
                    const CardDefinition* pd = CardDatabase::Instance().Lookup(a.card_name);
                    const int per = pd ? pd->params.firebreathing_power : 0;
                    tag = a.card_name + ": pump +" + std::to_string(per * k) + "/+0"
                        + " (discard " + std::to_string(k)
                        + (k == 1 ? " card)" : " cards)");
                }
                else
                {
                    tag = a.card_name + ": team +" + std::to_string(k)
                        + "/+0 and haste"
                        + (k > 1 ? " \xC3\x97" + std::to_string(k) : std::string());
                }
                break;
            }
            // The two greedy sinks, now human activations. Without a case here they fall to the
            // "(other)" default, which reads as a CAST of the land -- meaningless for a land, and
            // silent about what the ability actually does. Same lesson as ActivatePump above.
            case Action::Kind::AnimateLand:
                tag = a.card_name + ": animate (becomes a creature)"; break;
            case Action::Kind::TapForTokenPay:
                tag = a.card_name + ": tap for a token"; break;
            // Channel is a from-HAND ability, so "(other)" was indistinguishable from casting the
            // creature -- the exact ambiguity the `channel=` verb exists to remove.
            case Action::Kind::Channel:
                tag = a.card_name + ": channel (discard)"; break;
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
    // MDFC back-face names, so the viewer can request the correct Scryfall FACE image (a back face's
    // default image is its front -> the GUI appends face=back for these). DB-global, computed once.
    {
        static const std::vector<std::string> mdfc_backs = CardDatabase::Instance().MdfcBackFaceNames();
        os << "  \"mdfc_backs\": [";
        for (size_t i = 0; i < mdfc_backs.size(); ++i) { if (i) { os << ", "; } JsonStr(os, mdfc_backs[i]); }
        os << "],\n";
    }
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
    bool hand_first = true;
    for (size_t i = 0; i < hand.size(); ++i)
    {
        const Card* hc = hand[i];
        const CardDefinition* d = CardDatabase::Instance().Lookup(hc->m_name);
        // Apex-of-Power-exiled LANDS are unplayable ("cast SPELLS from among them"; a land is played,
        // not cast). m_impulse_no_land is set on ALL of Apex's exiled cards but only MATTERS for lands,
        // so filter ONLY a staged LAND out of the hand (staged SPELLS stay, with their is_staged badge);
        // the removed land is re-emitted under "exile" below so the player still SEES it stuck in exile.
        if (hc->m_impulse_no_land && d && d->card.IsLand()) { continue; }
        if (!hand_first) { os << ", "; }
        hand_first = false;
        os << "{ \"num\": " << hc->m_number << ", \"name\": "; JsonStr(os, hc->m_name);
        os << ", \"cost\": "; JsonStr(os, d ? d->card.m_mana_cost.ToString() : std::string());
        os << ", \"mv\": " << (d ? d->card.m_mana_cost.ManaValue() : 0);
        os << ", \"kind\": \"" << HandKind(d) << "\"";
        if (HandIsDraw(d)) { os << ", \"is_draw\": true"; }
        // is_creature lets the GUI offer an Aether Vial deploy (a creature whose MV equals a
        // Vial's charge counters can be put onto the battlefield for free).
        if (d && d->card.IsCreature()) { os << ", \"is_creature\": true"; }
        // MDFC with a LAND back on a NONLAND front (Turntimber Symbiosis // Turntimber, Serpentine
        // Wood). `kind` above is the FRONT's ("nonpermanent" for the sorcery), so the palette could
        // only ever offer the cast -- while the engine enumerates the land drop as `land=<FRONT
        // name>` and accepts it. The land side was therefore unreachable by hand: user-reported
        // 2026-08-24 with a saved rejection artifact (StompySurprise s5 gi4 t2, "cast=Turntimber
        // Symbiosis" rejected for {4}{G}{G}{G} while `land=Turntimber Symbiosis;cast=Priest of
        // Titania` was accepted). Naming the back face lets the palette offer it explicitly. A
        // land//land MDFC (Branchloft Pathway) is excluded -- its front IS a land, so the ordinary
        // land route already reaches it and the `face` sub picks the side.
        if (d && !d->card.IsLand() && !d->params.mdfc_back_name.empty()
            && !d->params.mdfc_back_produces.empty())
        { os << ", \"mdfc_land_back\": "; JsonStr(os, d->params.mdfc_back_name); }
        // Staged (exiled-but-playable) cards live in hand with m_is_staged set; surface that so
        // the GUI sets them apart (Light Up the Stage / Soulfire Eruption / Expressive Iteration).
        if (hc->m_is_staged) { os << ", \"is_staged\": true, \"staged_until\": " << hc->m_staged_expiry; }
        os << " }";
    }
    os << "]";
    // Exile display: non-playable exiled cards the player should SEE but can't play now --
    // (a) Apex-exiled LANDS (m_impulse_no_land staged lands: stuck in exile, never playable), and
    // (b) SUSPENDED cards (Lotus Bloom) with remaining time counters (arrive_turn - current turn;
    // 0 = arrives this upkeep). Playable staged SPELLS stay in the hand (purple badge) and are NOT
    // repeated here. Emitted only when the exile zone has something to show -> byte-identical for
    // every deck/state without an Apex-exiled land or a suspended card.
    {
        bool have_exile = false;
        auto open_or_sep = [&]() {
            if (!have_exile) { os << ", \"exile\": ["; have_exile = true; } else { os << ", "; }
        };
        for (const Card& c : me.hand)
        {
            const CardDefinition* dc = CardDatabase::Instance().Lookup(c.m_name);
            if (!(c.m_impulse_no_land && dc && dc->card.IsLand())) { continue; }   // Apex-exiled land only
            open_or_sep();
            os << "{ \"name\": "; JsonStr(os, c.m_name);
            os << ", \"kind\": \"land\", \"reason\": \"apex_land\", \"staged_until\": " << c.m_staged_expiry << " }";
        }
        for (const SuspendedCard& sc : me.suspended_cards)
        {
            int counters = sc.arrive_turn - s.turn_number;
            if (counters < 0) { counters = 0; }
            open_or_sep();
            os << "{ \"name\": "; JsonStr(os, sc.card.m_name);
            os << ", \"kind\": \"suspend\", \"reason\": \"suspend\", \"time_counters\": " << counters
               << ", \"arrive_turn\": " << sc.arrive_turn << " }";
        }
        if (have_exile) { os << "]"; }
    }
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
    // Land drops still available this turn (1 normally; more after a Scale the Heights / Explore
    // grants one). A committed segment carries at most ONE land -- Plan::land_to_play is a single
    // land by construction -- so the viewer needs this to tell "you're changing your mind about
    // which land" (replace the queued one) from "you have a second drop" (queue both and commit
    // them as consecutive segments; viewer issue #7).
    os << ", \"land_drops_left\": "
       << std::max(0, me.LandDropsAvailable() - me.lands_played_this_turn);
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

// ---- DecisionJson: the shared emitter for every claude-play decision frame ------------------
//
// There are ~20 decision types and every one of them opened with the same 4-7 lines
// (`{`, decision_index, type, source, turn, board context, heuristic_default) hand-written from
// scratch. Adding a new type meant copy-pasting that prologue and hoping.
//
// The frames are a WIRE PROTOCOL -- tools/play/ parses them and test/viewer_protocol_check.py
// replays 138 saved references against them, anchored on (kind, index, source). So this helper is
// deliberately NOT a JSON library (the project links nlohmann::json, but hand-emitting is what
// keeps the bytes exactly stable): it is a thin ordered writer whose pieces emit the same bytes
// the hand-written code did. Each emitter still chooses its own key ORDER by the order it calls
// these -- several types legitimately interleave their own keys into the prologue -- so this
// removes the repetition without freezing a schema onto types that differ.
//
// Contract: every piece writes a trailing ",\n". `Note()` closes the object (no trailing comma).
// Verify any change with test/lib/capture_decisions.py before/after -- a byte diff over ~11.5k
// captured frames covering all 23 types is the check that matters here, not the regression suite.
class DecisionJson
{
public:
    // Opens the object and writes decision_index. `type` is the wire type name.
    // Deferring the type lets main_phase slot `main_ordinal` between the two, as it always has.
    DecisionJson(std::ostream& os, int decision_index) : m_os(os)
    {
        m_os << "{\n";
        m_os << "  \"decision_index\": " << decision_index << ",\n";
    }

    DecisionJson& Type(const char* type)
    {
        m_os << "  \"type\": \"" << type << "\",\n";
        return *this;
    }

    DecisionJson& Source(const std::string& source)
    {
        m_os << "  \"source\": "; JsonStr(m_os, source); m_os << ",\n";
        return *this;
    }

    // Literal source (a type whose source is a fixed card name, not a runtime string).
    DecisionJson& SourceLiteral(const char* source)
    {
        m_os << "  \"source\": \"" << source << "\",\n";
        return *this;
    }

    DecisionJson& Turn(int turn)
    {
        m_os << "  \"turn\": " << turn << ",\n";
        return *this;
    }

    DecisionJson& Board(const GameState& s, int reveal_count = 0)
    {
        WriteBoardContext(m_os, s, reveal_count);
        return *this;
    }

    // The pre-game board: mulligan and bottoming happen before any permanent exists, so they emit
    // a fixed empty board rather than reading one off a GameState they do not have.
    DecisionJson& PregameBoard()
    {
        m_os << "  \"me\": { \"life\": 20, \"battlefield\": [] },\n";
        m_os << "  \"opponent\": { \"life\": 20, \"battlefield\": [] },\n";
        return *this;
    }

    DecisionJson& Int(const char* key, int value)
    {
        m_os << "  \"" << key << "\": " << value << ",\n";
        return *this;
    }

    DecisionJson& Bool(const char* key, bool value)
    {
        m_os << "  \"" << key << "\": " << (value ? "true" : "false") << ",\n";
        return *this;
    }

    // JSON-escaped string value.
    DecisionJson& Str(const char* key, const std::string& value)
    {
        m_os << "  \"" << key << "\": "; JsonStr(m_os, value); m_os << ",\n";
        return *this;
    }

    // Bare (already-safe) string value, for fixed enum-like words such as away_zone.
    DecisionJson& Word(const char* key, const char* value)
    {
        m_os << "  \"" << key << "\": \"" << value << "\",\n";
        return *this;
    }

    DecisionJson& HeuristicDefault(int value) { return Int("heuristic_default", value); }

    // `emit(i)` writes item i's object; the separator and brackets are handled here.
    template <class F>
    DecisionJson& Array(const char* key, std::size_t count, F emit)
    {
        m_os << "  \"" << key << "\": [";
        for (std::size_t i = 0; i < count; ++i) { if (i) { m_os << ", "; } emit(i); }
        m_os << "],\n";
        return *this;
    }

    // Closes the object. Every frame ends with a `note` telling the client what to reply.
    void Note(const std::string& text)
    {
        m_os << "  \"note\": "; JsonStr(m_os, text); m_os << "\n";
        m_os << "}\n";
    }

    std::ostream& os() { return m_os; }

private:
    std::ostream& m_os;
};

static void WriteDecisionJson(std::ostream& os, const GameState& s,
                              const std::vector<TurnSolver::Plan>& plans,
                              bool is_pre_combat, int decision_index, int reveal_count,
                              const std::vector<std::pair<int, std::string>>& drew = {},
                              const std::vector<PlayEvent>& events = {},
                              const std::vector<std::string>& dropped_casts = {},
                              int main_ordinal = -1,
                              const std::vector<PlayReveal>& reveals = {},
                              int chosen_index = -1)
{
    const Player& me  = s.ActivePlayer();
    DecisionJson d(os, decision_index);
    // #10 cast-order key: the ordinal of THIS main-phase decision among all main-phase decisions (0-based),
    // matching AIEngine::m_ext_main_ordinal at reorder time. The viewer keys S.castOrder by it directly
    // (no fragile client-side counting). -1 => not supplied (validation/replay contexts that don't reorder).
    // Emitted BEFORE `type`, which is why DecisionJson defers the type rather than taking it up front.
    if (main_ordinal >= 0) { d.Int("main_ordinal", main_ordinal); }
    d.Type("main_phase").Turn(s.turn_number)
     .Word("phase", is_pre_combat ? "pre_main" : "post_main")
     .Bool("on_the_play", s.on_the_play);
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
    // Reveals since the previous main-phase decision (a Muxus reveal, a searched-up tutor target, a
    // scry/dig look), each { turn, source, cards:[{name, disposition}] }. The player must never have to
    // guess WHAT was revealed or WHAT WE DID with it -- including the mandatory, choice-free splits like
    // Muxus's "put all Goblins, rest to the bottom", which surface no decision to infer it from.
    if (!reveals.empty())
    {
        os << "  \"reveals\": [";
        for (size_t ri = 0; ri < reveals.size(); ++ri)
        {
            if (ri) { os << ", "; }
            const PlayReveal& r = reveals[ri];
            os << "{ \"turn\": " << r.turn << ", \"source\": "; JsonStr(os, r.source);
            os << ", \"cards\": [";
            for (size_t ci = 0; ci < r.cards.size(); ++ci)
            {
                if (ci) { os << ", "; }
                os << "{ \"name\": "; JsonStr(os, r.cards[ci]);
                os << ", \"disposition\": ";
                JsonStr(os, ci < r.disposition.size() ? r.disposition[ci] : std::string());
                os << " }";
            }
            os << "] }";
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
    // Server-truth resolution: the DECLARED casts of the just-committed plan the executor could not pay
    // (dropped, left in hand). The viewer reads this to know AUTHORITATIVELY that a line partially
    // failed -- and rolls it back -- instead of inferring it from a board diff (detectDropped, which
    // false-positived on working lines). Empty (the common case) => the line fully resolved.
    if (!dropped_casts.empty())
    {
        os << "  \"dropped_casts\": [";
        for (size_t di = 0; di < dropped_casts.size(); ++di)
        {
            if (di) { os << ", "; }
            JsonStr(os, dropped_casts[di]);
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
    // Emit at most the top-ranked slice of the plan list. A Zada/Mirrorwing fan-out segment
    // enumerated 334,139 plans = a 331 MB decision JSON, which crashed the play server
    // (spawnSync ENOBUFS at a 32 MB buffer) and would hang any browser rendering it. The
    // list is display-only: a hand-assembled line is validated ENGINE-side (--validate-line
    // / CheckLine against the FULL in-engine list), and a plan reply (--choices) indexes the
    // engine's list, so entries here keep their REAL index and a capped tail costs only
    // reject-panel suggestions beyond the cap. "plans_total" tells the GUI when truncation
    // happened.
    // MTG_PLAY_PLANS_CAP overrides (0 = unlimited): the reference protocol checker
    // (test/viewer_protocol_check.py) content-matches RECORDED plan indices against this list
    // -- a recorded pick beyond the cap would look unrepairable and read as play-drift
    // (FiveColour s8 gi7 records index 304), so the checker runs uncapped.
    static const size_t kMaxEmittedPlans = []() -> size_t {
        const char* v = std::getenv("MTG_PLAY_PLANS_CAP");
        if (v == nullptr || *v == '\0') { return 200; }
        const long n = std::atol(v);
        return n <= 0 ? std::numeric_limits<size_t>::max() : static_cast<size_t>(n);
    }();
    const size_t n_emit = std::min(plans.size(), kMaxEmittedPlans);
    // DIVERSITY-AWARE display cap. With variant-heavy enumerations (tutor-target cross products)
    // the FIRST n_emit plans can all be variants of one cast set, hiding whole SPELLS from the
    // player (StompySurprise T3, 5d sweep: 480 Arbor+2xTutor combos capped at 200 starved Natural
    // Order out of the visible list entirely). When the cap truncates, emit one representative per
    // distinct (land, face, cast-name multiset) FIRST (in plan order), then fill the remaining
    // slots in plan order. Each emitted plan keeps its TRUE index, so replies are unchanged;
    // an uncapped list emits identically to before (byte-identical for every small menu).
    std::vector<size_t> emit_order;
    emit_order.reserve(n_emit);
    if (plans.size() > kMaxEmittedPlans)
    {
        std::unordered_set<std::string> seen_sets;
        std::vector<char> taken(plans.size(), 0);
        for (size_t i = 0; i < plans.size() && emit_order.size() < n_emit; ++i)
        {
            std::string key = plans[i].land_to_play + "|" + plans[i].land_face + "#";
            std::vector<std::string> nm;
            nm.reserve(plans[i].actions.size());
            for (const Action& a : plans[i].actions) { nm.push_back(a.card_name); }
            std::sort(nm.begin(), nm.end());
            for (const std::string& n : nm) { key += n; key += ';'; }
            if (seen_sets.insert(key).second) { emit_order.push_back(i); taken[i] = 1; }
        }
        for (size_t i = 0; i < plans.size() && emit_order.size() < n_emit; ++i)
        { if (!taken[i]) { emit_order.push_back(i); } }
        std::sort(emit_order.begin(), emit_order.end());
    }
    else
    {
        for (size_t i = 0; i < n_emit; ++i) { emit_order.push_back(i); }
    }
    // When the caller already KNOWS the resolved choice (the --log-dir trace path), the chosen
    // plan is emitted even when it sits beyond the display cap: a saved reference must contain
    // the record of its own pick, or the protocol checker can never content-anchor that pick
    // again (Mirrorwing s7_gi6 picked a validated hand-assembled line at engine index 223 of
    // 412 -- the capped save kept 0..199 and the reference read as internally inconsistent).
    const bool emit_chosen_extra = chosen_index >= 0
        && static_cast<size_t>(chosen_index) < plans.size()
        && std::find(emit_order.begin(), emit_order.end(),
                     static_cast<size_t>(chosen_index)) == emit_order.end();
    os << "  \"plans\": [\n";
    auto emit_plan = [&](size_t i, bool last)
    {
        const TurnSolver::Plan& p = plans[i];
        os << "    { \"index\": " << i << ", \"summary\": ";
        JsonStr(os, SummarizePlan(p, s));
        // Structured land + cast list so the GUI can match a hand-assembled line against
        // the model's plans (and show, after a reject, exactly which lines it WOULD play).
        os << ", \"land\": ";
        if (p.land_decided && !p.land_to_play.empty()) { JsonStr(os, p.land_to_play); }
        else                                            { os << "null"; }
        // Plain name list (used for the land+cast multiset match). Land's Edge activations
        // are NOT casts -- they are surfaced via the action's "landsedge" count below and the
        // top-level "lands_edge" object, so the GUI's cast match doesn't treat them as spells.
        os << ", \"casts\": [";
        // NOTE: SacForMana / SacCreatureOutlet stay INSIDE this list -- saved references match
        // recorded lines against the casts multiset with sacs included (moving them to
        // "activations" broke 14 goblins refs with enum-gap, 2026-08-11). play_invariants
        // instead skips token-named entries in its casts-must-be-in-hand advisory.
        auto is_activation = [](Action::Kind k)
        {
            // AnimateLand / TapForTokenPay name a LAND, which is never castable: leaving them in the
            // casts multiset made the GUI's land+cast match read "cast Mutavault", and a hand-built
            // line then carried both `cast=Mutavault` and `animate=Mutavault` and matched no plan.
            return k == Action::Kind::GarthActivate || k == Action::Kind::ActivateLoyalty
                || k == Action::Kind::Equip         || k == Action::Kind::GraveyardExileAbility
                || k == Action::Kind::GraveyardReturnAbility
                || k == Action::Kind::AnimateLand   || k == Action::Kind::TapForTokenPay;
        };
        {
            bool first = true;
            for (size_t a = 0; a < p.actions.size(); ++a)
            {
                if (p.actions[a].kind == Action::Kind::DiscardToLandsEdge) { continue; }
                if (p.actions[a].kind == Action::Kind::DigDraw)            { continue; }  // a dig, not a cast
                // Channel: a from-HAND ABILITY that discards the card. It is not a cast, and leaving
                // it in this list made the GUI's cast multiset ask for `cast=Twinshot Sniper` AND
                // `channel=Twinshot Sniper` for one action -- a line CheckLine rightly calls illegal.
                if (p.actions[a].kind == Action::Kind::Channel)            { continue; }
                if (is_activation(p.actions[a].kind))                      { continue; }  // ability, not a cast
                if (!first) { os << ", "; }
                first = false;
                JsonStr(os, p.actions[a].card_name);
            }
        }
        os << "]";
        // Battlefield activations, set apart from casts so the GUI's land+cast multiset match and
        // the play_invariants hand check stay honest (a loyalty/equip/Garth/Deathrite action names
        // a PERMANENT, not a castable card). Key emitted only when the plan has one -> the decision
        // JSON of every deck without these kinds is unchanged.
        {
            bool any = false;
            for (size_t a = 0; a < p.actions.size(); ++a)
            {
                if (!is_activation(p.actions[a].kind)) { continue; }
                os << (any ? ", " : ", \"activations\": [");
                any = true;
                JsonStr(os, p.actions[a].card_name);
            }
            if (any) { os << "]"; }
        }
        // #10 cast-order: the CANONICAL execution order of this plan's non-sac hand casts, so the viewer
        // can tell whether the human's queued order is a REORDER (=> emit --cast-order) or already
        // canonical (=> omit, keeping references byte-identical). Emitted only when >=2 non-sac casts.
        {
            std::vector<std::string> canon = TurnSolver::CanonicalNonSacCastOrder(s, p);
            if (canon.size() >= 2)
            {
                os << ", \"cast_order_canonical\": [";
                for (size_t ci = 0; ci < canon.size(); ++ci) { if (ci) os << ", "; JsonStr(os, canon[ci]); }
                os << "]";
            }
        }
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
            // An activated ability of a permanent ALREADY ON THE BATTLEFIELD (Krenko's "{T}: create X
            // Goblins"; a Skirk Prospector / Siege-Gang sac outlet). CheckLine matches these by card_name
            // inside the ordinary cast multiset (see CheckLine's `orderNames`), so the human commits one
            // with the same `cast=<name>` verb -- but the GUI can only QUEUE cards from HAND, so without
            // this flag there was no way to express one and the ability was simply unusable by a human.
            // The flag says "this name is a board activation, not a hand cast", which is what the viewer
            // needs to make the permanent clickable. Emitted only in the human-play decision JSON.
            // SacForMana joins these: Skirk Prospector's "Sacrifice a Goblin: Add {R}" used to be an
            // IMPLICIT mana source the enumerator slipped in when a cast needed it, so it carried no
            // flag and the viewer had no way to render or queue it (viewer issue #4). It is an
            // activation of a permanent already in play, exactly like Krenko's tap.
            // ActivatePump joins these for the same reason: it is an activation of a permanent
            // already in play (Burning-Fist's discard-pump, Sethron's team pump + haste), and
            // without the flag it serialized as a bare {"card": ..., "x": K} -- byte-identical in
            // shape to CASTING an {X} spell, so the viewer would render it as a hand cast and the
            // human would have no way to click the source or see that it discards.
            // The flag is a property of the KIND, so this list is exactly "every action kind whose
            // source is a permanent already on the battlefield". Eight were missing it (2026-08-23)
            // and were therefore unusable by a human even though the engine enumerated them every
            // turn: Call of the Wild (ActivateRevealTop), every planeswalker loyalty ability
            // (ActivateLoyalty -- Bolas / Oko / Jared), Garth's tap-to-conjure, Wirewood Lodge's
            // untap (UntapCreature), and the whole KittyEquipment set (Equip, Balan's attach-all,
            // Stoneforge's put, the Jitte modes), which left the deck built around equipping
            // unplayable by hand.
            // GraveyardExileAbility was the THIRTEENTH miss (2026-08-25, user report #1): Deathrite
            // Shaman's "{B}, {T}: Exile target instant or sorcery card from a graveyard, each
            // opponent loses 2 life" is enumerated on every fueled FiveColour turn and `is_activation`
            // above already excludes it from `casts` -- but with no flag here the viewer had no board
            // thumb for it, so the one ability that turns a graveyard into a clock was unusable by hand.
            if (ac.kind == Action::Kind::TapForTokens
             || ac.kind == Action::Kind::SacForMana
             || ac.kind == Action::Kind::ActivatePump
             || ac.kind == Action::Kind::SacCreatureOutlet
             || ac.kind == Action::Kind::ActivateRevealTop
             || ac.kind == Action::Kind::ActivateLoyalty
             || ac.kind == Action::Kind::GarthActivate
             || ac.kind == Action::Kind::UntapCreature
             || ac.kind == Action::Kind::Equip
             || ac.kind == Action::Kind::AttachAllEquipment
             || ac.kind == Action::Kind::PutFromHandAbility
             || ac.kind == Action::Kind::GraveyardExileAbility
             || ac.kind == Action::Kind::GraveyardReturnAbility
             || ac.kind == Action::Kind::AnimateLand
             || ac.kind == Action::Kind::TapForTokenPay
             || ac.kind == Action::Kind::JitteModeAbility)
            {
                os << ", \"activate\": true";
                // `sacout` tells the GUI to encode this as the sacout= line verb rather than cast=,
                // and sac_count is how many creatures ONE activation eats (the burst). ONLY the two
                // kinds that really eat a creature get it: ActivatePump used to be swept in by a
                // `!= TapForTokens` test, so the viewer encoded a pump as `sacout=<source>` -- a verb
                // CheckLine only ever matches against SacForMana / SacCreatureOutlet actions, so the
                // line could never match a plan and every Minotaur pump read as a reject.
                if (ac.kind == Action::Kind::SacForMana
                 || ac.kind == Action::Kind::SacCreatureOutlet)
                { os << ", \"sacout\": true, \"sac_count\": " << std::max(1, ac.sac_count); }
                // `verb` = the LineSpec token the GUI must write for this activation. Absent means the
                // ordinary `cast=<card>` (matched inside CheckLine's orderNames multiset), which is
                // what the kinds above use. A kind needs its own verb exactly when `cast=<name>` would
                // be AMBIGUOUS with a hand cast of the same-named card: Stoneforge's put and a hard
                // cast of the Equipment it puts both read `cast=Colossus Hammer`, and equipping the
                // Bonesplitter in play reads the same as casting the copy in hand.
                if (ac.kind == Action::Kind::AttachAllEquipment)      { os << ", \"verb\": \"attachall\""; }
                else if (ac.kind == Action::Kind::PutFromHandAbility) { os << ", \"verb\": \"sfput\""; }
                else if (ac.kind == Action::Kind::Equip)              { os << ", \"verb\": \"equip\""; }
                else if (ac.kind == Action::Kind::JitteModeAbility)
                { os << ", \"verb\": \"jittemode\", \"mode\": " << ac.gy_exile_mode; }
                // Deathrite's graveyard-exile ability names the SOURCE permanent, so `cast=Deathrite
                // Shaman` would be ambiguous with hard-casting one of the other copies from hand --
                // exactly the test above. `mode` distinguishes the drain (1) from the lifegain (2).
                else if (ac.kind == Action::Kind::GraveyardExileAbility)
                { os << ", \"verb\": \"gyexile\", \"mode\": " << ac.gy_exile_mode; }
                // Mutavault's animate and Sliver Hive's token ability both name the LAND, so
                // `cast=<land>` would be meaningless (a land is played, not cast) -- and a deck can
                // hold several copies, so the verb also has to be distinguishable per source.
                // Haven's rebuy names the LAND (never castable) AND carries a target, so it needs
                // both its own verb and the target token -- otherwise two rebuy variants of one Haven
                // would write the identical line and the human could not say WHICH Dragon to return.
                else if (ac.kind == Action::Kind::GraveyardReturnAbility)
                {
                    os << ", \"verb\": \"gyreturn\", \"gyreturn_target\": ";
                    JsonStr(os, ac.tutor_target);
                }
                else if (ac.kind == Action::Kind::AnimateLand)    { os << ", \"verb\": \"animate\""; }
                else if (ac.kind == Action::Kind::TapForTokenPay) { os << ", \"verb\": \"taptoken\""; }
                // `activate_source` = the permanent the human CLICKS, when that is not `card`.
                // PutFromHandAbility's card_name is the Equipment being PUT (it is in hand), so
                // without this the viewer would look for a board thumb that isn't there.
                if (ac.kind == Action::Kind::PutFromHandAbility)
                {
                    os << ", \"activate_source\": ";
                    JsonStr(os, EnchantTargetName(s, ac.sac_source_id));
                }
                // Equip: the host this variant attaches to, so the plan list reads "Bonesplitter ->
                // Kor Duelist" rather than a bare card name -- and, since 2026-09-01, `equip_src`,
                // the m_number of the EQUIPMENT itself. The viewer stamps both onto its `equip=`
                // token, which is what lets a drag name one of two same-named Kor Duelists (or move
                // one of two Bonesplitters) instead of leaving it to a by-NAME sub-decision that
                // cannot tell them apart.
                if (ac.kind == Action::Kind::Equip && ac.sac_victim_id != 0)
                {
                    os << ", \"equip_host\": " << ac.sac_victim_id << ", \"equip_host_name\": ";
                    JsonStr(os, EnchantTargetName(s, ac.sac_victim_id));
                    if (ac.sac_source_id != 0) { os << ", \"equip_src\": " << ac.sac_source_id; }
                }
            }
            // A hand card with more than ONE way to be played needs a route in the palette for each
            // (the MDFC-face rule generalised -- `kind` describes only one of them). Two shapes here:
            //
            //  * CHANNEL (Twinshot Sniper "{1}{R}, Discard this card: 2 damage to any target") is a
            //    from-hand ABILITY, not a cast: same card, same hand slot, and it serialised as a
            //    bare {"card": "Twinshot Sniper"} -- byte-identical in shape to CASTING the creature.
            //    Its own `channel=` verb removes that ambiguity; the GUI badges the hand card.
            //  * BESTOW (Gnarled Scarhide) casts the SAME card as an Aura instead of a creature. It
            //    already splits from the creature cast by its `enchant` sub, so it needs no verb --
            //    but without this key nothing in the plan JSON says WHICH mode the variant is, so the
            //    choose dialog offered "Gnarled Scarhide" twice with no way to tell them apart.
            if (ac.kind == Action::Kind::Channel) { os << ", \"channel\": true, \"verb\": \"channel\""; }
            //  * SUSPEND (Lotus Bloom) is the third shape: a from-hand ALTERNATIVE to casting, which
            //    serialises as a bare {"card": "Lotus Bloom"} exactly like the cast would. Its own
            //    `suspend=` verb removes that ambiguity before a suspend card with a real mana cost
            //    makes it bite.
            if (ac.kind == Action::Kind::Suspend) { os << ", \"verb\": \"suspend\""; }
            if (ac.bestow)                        { os << ", \"bestow\": true"; }
            if (!ac.tutor_target.empty()) { os << ", \"tutor_target\": "; JsonStr(os, ac.tutor_target); }
            if (ac.chosen_x > 0)          { os << ", \"x\": " << ac.chosen_x; }
            if (ac.ponder_keep >= 0)      { os << ", \"ponder_keep\": " << ac.ponder_keep; }
            if (ac.soulfire_own_targets > 0) { os << ", \"soulfire_targets\": " << ac.soulfire_own_targets; }
            if (ac.splice_count > 0)      { os << ", \"splice_count\": " << ac.splice_count; }
            // Replicate: how many EXTRA token copies this cast pays for (CR 702.56). Emitted from
            // >= 0, not > 0 -- "replicate zero times" is a real declared line whose whole point is
            // the mana it does NOT spend, so the GUI must be able to tell it from a cast that never
            // had replicate at all (which carries no key).
            if (ac.replicate_count >= 0)  { os << ", \"replicate_count\": " << ac.replicate_count; }
            // Aura enchant target: the creature (m_number + resolved name) this Aura attaches to, so
            // the GUI shows WHICH creature when several plans cast the same aura on different targets.
            if (ac.enchant_target > 0)
            {
                os << ", \"enchant_target\": " << ac.enchant_target
                   << ", \"enchant_target_name\": "; JsonStr(os, EnchantTargetName(s, ac.enchant_target));
            }
            // Blink: the target's m_number + resolved name, and the activation count, so the GUI
            // and the claude-play protocol can tell "blink Peregrine Drake" (the mana-positive loop)
            // from "blink a Cloud of Faeries" (loses an attacker to renewed summoning sickness) or
            // from blinking the opponent's creature. Without these the variants are identical.
            if (ac.kind == Action::Kind::ActivateBlink)
            {
                os << ", \"blink_target\": " << ac.sac_victim_id
                   << ", \"blink_target_name\": "; JsonStr(os, EnchantTargetName(s, ac.sac_victim_id));
                os << ", \"blink_count\": " << std::max(1, ac.chosen_x);
            }
            if (!ac.chosen_float_color.empty()) { os << ", \"float_color\": "; JsonStr(os, ac.chosen_float_color); }
            // Solo-target trick (Zada/Mirrorwing): the target is chosen at RESOLUTION via the
            // board-click prompt (viewer feedback 2026-08-12 #2), so the GUI must NOT run its
            // queue-time enchant-variant dialog for these casts (enchantTargetsFor skips them).
            {
                const CardDefinition* ad = ac.def ? ac.def : CardDatabase::Instance().Lookup(ac.card_name);
                if (ad && ad->params.solo_target_trick) { os << ", \"trick\": true"; }
            }
            os << " }";
        }
        os << "]";
        os << (last ? " }\n" : " },\n");
    };
    for (size_t k = 0; k < emit_order.size(); ++k)
    { emit_plan(emit_order[k], !emit_chosen_extra && k + 1 == emit_order.size()); }
    if (emit_chosen_extra) { emit_plan(static_cast<size_t>(chosen_index), true); }
    os << "  ],\n";
    if (plans.size() > n_emit) { os << "  \"plans_total\": " << plans.size() << ",\n"; }
    os << "  \"note\": \"reply with one plan index (0-based), or -1 to pass / cast nothing\"\n";
    os << "}\n";
}

// Vial-as-a-choice decision: whether to add a charge counter to an Aether Vial this
// upkeep. The reply is 1 (add a counter) or 0 (hold). `heuristic` is the default the
// encoded AI would take.
static void WriteVialDecisionJson(std::ostream& os, const GameState& s,
                                  const Permanent& vial, int decision_index, bool heuristic)
{
    DecisionJson d(os, decision_index);
    d.Type("vial_charge").Turn(s.turn_number);
    // vial + current_counters deliberately share one output line (historic layout) -- emitted raw.
    os << "  \"vial\": "; JsonStr(os, vial.card.m_name);
    os << ", \"current_counters\": " << vial.charge_counters << ",\n";
    // perm_index of THIS vial on the battlefield, so the GUI can highlight it in place on the board.
    {
        int vi = -1;
        for (int i = 0; i < static_cast<int>(s.battlefield.size()); ++i) { if (&s.battlefield[i] == &vial) { vi = i; break; } }
        d.Int("perm_index", vi);
    }
    d.HeuristicDefault(heuristic ? 1 : 0).Board(s);
    d.Note("reply 1 to add a charge counter this upkeep, 0 to hold. Aether "
           "Vial deploys a creature whose mana value EQUALS its counter count.");
}

// Echo pay-or-sacrifice decision (claude-play): at upkeep, an echo creature is sacrificed unless its
// echo cost is paid. Emitted only when paying is affordable (else it is a forced sacrifice, no choice).
// Reply 1 = pay the echo cost (keep the creature), 0 = let it be sacrificed. `heuristic` is the AI's
// default (a self-replacing body like Mogg War Marshal declines; others pay).
static void WriteEchoDecisionJson(std::ostream& os, const GameState& s,
                                  const Permanent& creature, const std::string& echo_cost,
                                  int decision_index, bool heuristic)
{
    DecisionJson d(os, decision_index);
    d.Type("echo").Turn(s.turn_number);
    // creature + echo_cost deliberately share one output line (historic layout) -- emitted raw.
    os << "  \"creature\": "; JsonStr(os, creature.card.m_name);
    os << ", \"echo_cost\": "; JsonStr(os, echo_cost); os << ",\n";
    {
        int ci = -1;
        for (int i = 0; i < static_cast<int>(s.battlefield.size()); ++i) { if (&s.battlefield[i] == &creature) { ci = i; break; } }
        d.Int("perm_index", ci);
    }
    d.HeuristicDefault(heuristic ? 1 : 0).Board(s);
    d.Note("reply 1 to pay the echo cost and keep this creature, 0 to let it be sacrificed.");
}

// Mulligan keep/mulligan decision (claude-play). One per London-mulligan attempt, emitted BEFORE the
// first main-phase decision. The reply is 1 (keep this hand) or 0 (mulligan again). `ai_keep` is what
// the engine's KeepHand would do -- surfaced as the "AI would X" hint. `mulligan_count` is how many
// mulligans have been taken so far; keeping now means bottoming that many cards next.
static void WriteMulliganDecisionJson(std::ostream& os, const std::vector<Card>& hand,
                                      int mulligan_count, bool on_the_play, bool ai_keep,
                                      int decision_index)
{
    DecisionJson d(os, decision_index);
    // PregameBoard: mulligan/bottom happen before any permanent exists, so the viewer gets a clean
    // empty board behind the modal rather than an undefined one.
    d.Type("mulligan").Turn(0).PregameBoard()
     .Int("mulligan_count", mulligan_count).Bool("on_the_play", on_the_play)
     .Int("to_bottom", mulligan_count);   // London: keep at count K => bottom K cards
    d.Array("hand", hand.size(), [&](std::size_t i)
    {
        os << "{ \"num\": " << hand[i].m_number << ", \"name\": ";
        JsonStr(os, hand[i].m_name);
        os << " }";
    });
    d.Int("ai_choice", ai_keep ? 1 : 0);
    d.Note("reply 1 to KEEP this hand (then bottom " + std::to_string(mulligan_count)
           + " card(s)), or 0 to mulligan again");
}

// The exhaustive table's JOINT bottom recommendation: the hand indices to put on the bottom so the kept
// subhand matches DecideBottom's optimal (7-count)-keep composition. Bottoming with a table is a single
// joint decision (evaluate the kept subsets, pick the best), NOT a greedy card-at-a-time search -- so the
// GUI can show the whole set at once, pre-selected. Members of a bucket are interchangeable (any
// over-target member is an equally-optimal removal), mirroring AIEngine::BottomCards. Empty when the deck
// has no bottoming table or the hand isn't tabled (GUI then falls back to the per-step deep hint).
static std::vector<int> ExhaustiveBottomSet(const std::vector<Card>& hand,
                                            const ExhaustiveKeepPolicy& ek, int count, bool on_play)
{
    std::vector<int> out;
    if (ek.empty() || !ek.bottoming_enabled || count <= 0) { return out; }
    std::vector<std::string> names; names.reserve(hand.size());
    for (const Card& c : hand) { names.push_back(c.m_name.str()); }
    std::vector<int> target;
    if (!ek.DecideBottom(names, count, on_play, target)) { return out; }
    const auto& n2b = ek.name_to_bucket;
    std::vector<int> comp(ek.buckets.size(), 0);
    for (const std::string& n : names)
    { auto it = n2b.find(n); if (it != n2b.end()) { comp[it->second]++; } }
    std::vector<char> used(hand.size(), 0);
    for (int k = 0; k < count; ++k)
    {
        int pick = -1;
        for (int j = 0; j < static_cast<int>(hand.size()); ++j)
        {
            if (used[j]) { continue; }
            auto it = n2b.find(names[j]);
            if (it != n2b.end() && comp[it->second] > target[it->second]) { pick = j; break; }
        }
        if (pick < 0) { break; }
        used[pick] = 1; comp[n2b.at(names[pick])]--; out.push_back(pick);
    }
    // Only offer a set that covers ALL `count` bottoms. It under-covers when `count` exceeds the table's
    // mulligan depth (max_mull): DecideBottom caps its lookup at min(count, max_mull), so the keep-target
    // is larger than 7-count and fewer than `count` cards are over-target. Return nothing in that case so
    // the GUI falls back to the per-step deep hint / manual selection rather than pre-selecting too few.
    if (static_cast<int>(out.size()) != count) { out.clear(); }
    return out;
}

// London bottoming decision (claude-play). After keeping at mulligan_count K, the player bottoms K
// cards; the engine still reads one hand-index per bottom decision, but the GUI batches them into ONE
// multi-select dialog. `ai_pick` is the per-step engine pick (the "AI would X" hint); each hand card
// carries `win_optimal` (depth>0: does bottoming it preserve the earliest clairvoyant win?). `ai_set` is
// the exhaustive table's JOINT recommended set (all K indices at once) when the deck is tabled, else [].
static void WriteBottomDecisionJson(std::ostream& os, const std::vector<Card>& hand,
                                    int ai_pick, const std::vector<char>& win_optimal,
                                    int step, int total, int decision_index,
                                    const std::vector<int>& ai_set)
{
    DecisionJson d(os, decision_index);
    d.Type("bottom").Turn(0).PregameBoard().Int("bottom_step", step).Int("bottom_total", total);
    d.Array("hand", hand.size(), [&](std::size_t i)
    {
        os << "{ \"num\": " << hand[i].m_number << ", \"name\": ";
        JsonStr(os, hand[i].m_name);
        if (i < win_optimal.size())
        { os << ", \"win_optimal\": " << (win_optimal[i] ? "true" : "false"); }
        os << " }";
    });
    // ai_choice is an OBJECT here (index + card identity), not a bare int -- emitted raw.
    os << "  \"ai_choice\": { \"index\": " << ai_pick;
    if (ai_pick >= 0 && ai_pick < static_cast<int>(hand.size()))
    {
        os << ", \"num\": " << hand[ai_pick].m_number << ", \"name\": ";
        JsonStr(os, hand[ai_pick].m_name);
    }
    os << " },\n";
    d.Array("ai_set", ai_set.size(), [&](std::size_t i) { os << ai_set[i]; });
    d.Note("reply the hand INDEX (0-based) of the card to put on the bottom (step "
           + std::to_string(step + 1) + " of " + std::to_string(total) + ")");
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

    DecisionJson dj(os, decision_index);
    dj.Type(kindstr).Source(source).Turn(s.turn_number).Word("away_zone", away_zone).Board(s);
    dj.Array("looked", static_cast<std::size_t>(m), [&](std::size_t i)
    {
        const CardDefinition* cd = CardDatabase::Instance().LookupCached(looked[i]);
        os << "{ \"name\": "; JsonStr(os, looked[i].m_name.str());
        os << ", \"is_land\": " << ((cd && cd->card.IsLand()) ? "true" : "false") << " }";
    });
    dj.HeuristicDefault(heuristic_default);
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
                if (kind == LookKind::Reorder || kind == LookKind::ReorderNoShuffle)
                { top.push_back(looked[i].m_name.str()); }  // Ponder/Mirri keep all on top
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
         : kind == LookKind::ReorderNoShuffle ? "Order all cards on top (no shuffle option)."
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
                // Ask the PERMANENT, not the card database. Tokens (CreateToken) and the goldfish
                // opponent's scheduled spawns (GameEngine::UpkeepStep) are synthetic Cards that
                // carry CardType::Creature but have no cards.json entry, so the old
                // `LookupCached(...) -> IsCreature()` filter silently dropped EVERY one of them
                // from the human's target list -- while the engine's own targeting (e.g.
                // FindLifegainRemovalTarget) uses `p.card.IsCreature()` directly. That made
                // claude-play/viewer targeting strictly NARROWER than the AI's: Lathliss's 5/5 and
                // Utvara's 6/6 Dragon tokens could never be Bolt targets, and neither could the
                // opponent spawns that exist precisely "to provide creature targets for spells like
                // Searing Blood" (GameState.h). Found by the Dragons Stage-5d claude-play sweep.
                if (!p.card.IsCreature()) { continue; }
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
    // Shroud (CR 702.18b, rules fix 2026-08-14): a creature shrouded by its own equipment
    // (Lightning Greaves) can't be the target of its controller's spells either -- legality,
    // not narrowing, so it is filtered from the human-facing target list too.
    static const bool legacy_shroud = EnvOn("MTG_LEGACY_SHROUD");
    for (int i = 0; i < static_cast<int>(s.battlefield.size()); ++i)
    {
        const Permanent& p = s.battlefield[i];
        if (p.controller_index != controller) { continue; }
        // Card::IsCreature() directly, NOT a CardDatabase lookup -- the same reason the two
        // collectors below already say so: OUR creatures are often TOKENS too (Mirrorwing's whole
        // plan is token copies; Goblin Instigator's ETB makes one), tokens have no cards.json entry,
        // and a LookupCached here returned null and silently dropped every one of them. The search
        // enumerates tokens as trick targets on purpose ("tokens carry unique ids now, so they ride
        // the target axis too", CollectActions), so this dropped a target the AI can pick and the
        // rules allow -- copying a 1/1 Goblin Token with Twinflame is an ordinary legal line.
        if (!p.card.IsCreature()) { continue; }
        if (!legacy_shroud && CreatureHasShroud(p, s)) { continue; }
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
    // Same shroud legality filter as CollectOwnCreatureTargets (only own creatures can be
    // shrouded -- Greaves is ours; the passive opponent has no equipment).
    static const bool legacy_shroud = EnvOn("MTG_LEGACY_SHROUD");
    for (int i = 0; i < static_cast<int>(s.battlefield.size()); ++i)
    {
        const Permanent& p = s.battlefield[i];
        if (!p.card.IsCreature()) { continue; }
        const bool mine = (p.controller_index == controller);
        if (mine && !legacy_shroud && CreatureHasShroud(p, s)) { continue; }
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
    // min_targets == 0 (an "up to one target" trick -- Gold Rush / Scale the Heights) additionally
    // offers the EMPTY set: casting with no target is a real, rules-legal line. Every existing
    // caller passes min >= 1, so mask still starts at 1 for them -- byte-identical.
    std::vector<TargetOption> opts;
    const int n = static_cast<int>(legal.size());
    const int cap = std::max(1, std::min(max_targets, n));
    const int lo  = std::max(min_targets <= 0 ? 0 : 1, std::min(min_targets, cap));
    if (lo == 0)
    {
        TargetOption none;
        none.label = "(no target)";
        opts.push_back(std::move(none));
    }
    for (int mask = 1; mask < (1 << n) && opts.size() < 256; ++mask)
    {
        int bits = std::popcount(static_cast<unsigned>(mask));
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
    DecisionJson d(os, decision_index);
    d.Type("divide").Source(source).Turn(s.turn_number).Int("total_damage", total).Board(s);
    d.Array("legal_targets", legal.size(), [&](std::size_t i)
    {
        os << "{ \"kind\": \"" << (legal[i].kind == 0 ? "player" : "permanent")
           << "\", \"index\": " << legal[i].index << ", \"label\": "; JsonStr(os, legal_labels[i]);
        os << ", \"default\": " << (i < default_amounts.size() ? default_amounts[i] : 0) << " }";
    });
    d.Note("reply one integer per legal target (in this order), each >= 0 and summing to "
           + std::to_string(total) + " -- the damage assigned to each. Default = all to the opponent face.");
}

// Emit a board-click target decision for a uniform-damage spell. `options` (built by the caller)
// each map a target set + label; the player replies one index. `per_target` = damage each target
// takes (Crackle 5*X, else the fixed damage). `heuristic_default` = the option matching the AI pick.
static void WriteTargetDecisionJson(std::ostream& os, const GameState& s, const std::string& source,
                                    const std::vector<ChosenTarget>& legal, const std::vector<std::string>& legal_labels,
                                    const std::vector<TargetOption>& options, int per_target,
                                    int max_targets, int heuristic_default, int decision_index,
                                    const std::string& pump_desc = "", const std::string& remove_desc = "",
                                    int min_targets = 1, bool random_damage = false,
                                    const std::string& loyalty_desc = "",
                                    const std::string& copy_desc = "")
{
    DecisionJson d(os, decision_index);
    d.Type("target").Source(source).Turn(s.turn_number);
    // A non-empty loyalty_desc (e.g. "turned into a green 3/3 Elk with no abilities") flags this as
    // a PLANESWALKER LOYALTY ability's target: no damage number, and the legal set is whatever that
    // ability's printed restriction allows (artifacts and lands included), so the viewer must not
    // word it as "N damage" or assume the opponent's face is in the set.
    if (!loyalty_desc.empty()) { d.Str("loyalty", loyalty_desc); }
    // A non-empty copy_desc flags this as Sakashima's Protege's enter-as-a-copy choice: the
    // legal set is this-turn ENTRANTS (any permanent, either side), the "(no target)" option is
    // DECLINE (enter as the printed 3/1), and no damage is dealt.
    if (!copy_desc.empty())   { d.Str("copy", copy_desc); }
    // A non-empty pump_desc (e.g. "+4/+4") flags this as an own-creature pump rather than damage, so
    // the viewer shows "gets +4/+4" and highlights your creatures instead of "N damage".
    if (!pump_desc.empty())   { d.Str("pump", pump_desc); }
    // A non-empty remove_desc (e.g. "exiled ...") flags this as a creature-removal target (Swords),
    // so the viewer highlights the OPPONENT's creatures and shows the removal wording, not "N damage".
    if (!remove_desc.empty()) { d.Str("remove", remove_desc); }
    // Soulfire Eruption: each target takes a RANDOM exiled card's mana value (not a fixed number), so
    // per_target_damage is meaningless -- the viewer shows "a random card's mana value each" instead.
    if (random_damage)        { d.Bool("random_damage", true); }
    // These three deliberately share one output line (historic layout) -- emitted raw.
    os << "  \"per_target_damage\": " << per_target << ", \"max_targets\": " << max_targets
       << ", \"min_targets\": " << min_targets << ",\n";
    d.Board(s);
    d.Array("legal_targets", legal.size(), [&](std::size_t i)
    {
        os << "{ \"kind\": \"" << (legal[i].kind == 0 ? "player" : "permanent")
           << "\", \"index\": " << legal[i].index << ", \"label\": "; JsonStr(os, legal_labels[i]); os << " }";
    });
    d.HeuristicDefault(heuristic_default);
    d.Array("options", options.size(), [&](std::size_t oi)
    {
        os << "{ \"index\": " << oi << ", \"label\": "; JsonStr(os, options[oi].label);
        os << ", \"targets\": [";
        for (size_t ti = 0; ti < options[oi].targets.size(); ++ti)
        { if (ti) os << ", "; os << "{ \"kind\": \"" << (options[oi].targets[ti].kind == 0 ? "player" : "permanent")
                                  << "\", \"index\": " << options[oi].targets[ti].index << " }"; }
        os << "] }";
    });
    if (!loyalty_desc.empty())
    { d.Note("reply an option index -- the permanent this loyalty ability targets. It is "
             + loyalty_desc + ". Default = the AI's pick."); }
    else if (!pump_desc.empty())
    { d.Note("reply an option index. The chosen creature gets " + pump_desc
             + ". Default = the AI's pick (best attacker)."); }
    else if (!remove_desc.empty())
    { d.Note("reply an option index. The chosen opponent creature is " + remove_desc
             + ". Default = the AI's pick (largest)."); }
    else if (random_damage)
    { d.Note("reply an option index. Each chosen target is dealt a RANDOM exiled card's "
             "mana value (assigned positionally, not steerable); pick " + std::to_string(min_targets)
             + "-" + std::to_string(max_targets) + " targets. Default = the AI's pick."); }
    else if (!copy_desc.empty())
    { d.Note("reply an option index. Sakashima's Protege enters as a COPY of the chosen "
             "this-turn entrant (any permanent that entered this turn; a Breaching Dragonstorm "
             "copy re-fires its exile trigger); pick the \"(no target)\" option to decline and "
             "enter as the printed 3/1. Default = the AI's pick."); }
    else
    { d.Note("reply an option index. Each chosen target takes " + std::to_string(per_target)
             + " damage (up to " + std::to_string(max_targets) + " target(s)). Default = the AI's pick (face)."); }
}

// Emit a Karoo bounce-land return decision: which of the controller's lands goes back to hand.
// `legal` are battlefield indices; each option carries that index (so the GUI can highlight the
// permanent on the board) plus the land's name/tap state. `heuristic_default` indexes into `legal`.
static void WriteBounceDecisionJson(std::ostream& os, const GameState& s, const std::string& source,
                                    const std::vector<int>& legal, int heuristic_default, int decision_index,
                                    bool sacrifice = false)
{
    DecisionJson d(os, decision_index);
    // A `sacrifice` variant reuses the identical board-land picker as the Karoo bounce, but the land
    // goes to the graveyard (Shard Volley's additional cost) and the viewer says "sacrifice".
    d.Type(sacrifice ? "sacrifice" : "bounce").Source(source).Turn(s.turn_number)
     .Board(s).HeuristicDefault(heuristic_default);
    d.Array("options", legal.size(), [&](std::size_t i)
    {
        const Permanent& p = s.battlefield[legal[i]];
        os << "{ \"index\": " << i << ", \"perm_index\": " << legal[i] << ", \"tapped\": "
           << (p.tapped ? "true" : "false") << ", \"name\": "; JsonStr(os, p.card.m_name.str());
        os << ", \"label\": "; JsonStr(os, p.card.m_name.str()); os << " }";
    });
    d.Note(std::string("reply an option index -- the land to ")
           + (sacrifice ? "sacrifice" : "return to your hand") + ". Default = the AI's pick.");
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
    DecisionJson d(os, decision_index);
    d.Type("dig").Source(source).Turn(s.turn_number).Board(s).HeuristicDefault(heuristic_default);
    d.Array("examined", examined.size(), [&](std::size_t i)
    {
        os << "{ \"index\": " << i << ", \"legal\": " << (is_legal[i] ? "true" : "false")
           << ", \"name\": "; JsonStr(os, examined[i].m_name.str()); os << " }";
    });
    // The shared dig chooser serves two destinations: ETB digs put the pick INTO HAND
    // (Acclaimed Contender), the Skyhunter attack-dig and the Turntimber "(put)" look put it
    // ONTO THE BATTLEFIELD (Skyhunter's attach follows as its own attach_host decision). Say
    // the right one -- the 5d sweep flagged the fixed into-hand wording as contradicting
    // Skyhunter's oracle text.
    if (source.find("(attack dig)") != std::string::npos)
    {
        d.Note("reply an examined index to put that card onto the battlefield (attaching is the "
               "next decision), or -1 to take nothing. Default = the AI's pick.");
    }
    else if (source.find("(put)") != std::string::npos)
    {
        d.Note("reply an examined index to put that card onto the battlefield, or -1 to put "
               "nothing; the rest go to the bottom. Default = the AI's pick.");
    }
    else
    {
        d.Note("reply an examined index to put that card into your hand, or -1 to take nothing. "
               "Default = the AI's pick.");
    }
}

// Light-Paws tutor-attach decision (Light-Paws, Emperor's Voice): the player picks WHICH library Aura
// Light-Paws fetches and attaches to itself (or declines -- it is a "may search"). Emits the library
// Aura pool as image options with a `legal` flag (only fetchable Auras are pickable -- MV <= the cast
// Aura, a name you don't already control, whose restriction Light-Paws satisfies); reply = a pool
// index to fetch, or -1 to fetch nothing.
static void WriteLightPawsDecisionJson(std::ostream& os, const GameState& s, const std::string& source,
                                       const std::vector<Card>& pool, const std::vector<int>& legal,
                                       int heuristic_default, int decision_index)
{
    std::vector<bool> is_legal(pool.size(), false);
    for (int li : legal) { if (li >= 0 && li < static_cast<int>(pool.size())) { is_legal[li] = true; } }
    DecisionJson d(os, decision_index);
    d.Type("lightpaws").Source(source).Turn(s.turn_number).Board(s).HeuristicDefault(heuristic_default);
    d.Array("pool", pool.size(), [&](std::size_t i)
    {
        os << "{ \"index\": " << i << ", \"legal\": " << (is_legal[i] ? "true" : "false")
           << ", \"name\": "; JsonStr(os, pool[i].m_name.str()); os << " }";
    });
    d.Note("reply a pool index to fetch that Aura and attach it to Light-Paws, or -1 to fetch nothing. Default = the AI's pick.");
}

// Goblin Lackey combat-cheat decision: on combat damage to a player, the player MAY put a Goblin
// permanent card from hand onto the battlefield. Emits the matching hand cards as image options; the
// reply is a candidate index to put that card, or -1 to decline (it is a "may"). `heuristic_default`
// = the engine's highest-MV pick (pre-selected in the viewer).
static void WriteLackeyDecisionJson(std::ostream& os, const GameState& s, const std::string& source,
                                    const std::vector<Card>& candidates, int heuristic_default,
                                    int decision_index)
{
    DecisionJson d(os, decision_index);
    d.Type("lackey_put").Source(source).Turn(s.turn_number).Board(s).HeuristicDefault(heuristic_default);
    d.Array("candidates", candidates.size(), [&](std::size_t i)
    {
        os << "{ \"index\": " << i << ", \"name\": "; JsonStr(os, candidates[i].m_name.str()); os << " }";
    });
    d.Note("reply a candidate index to put that Goblin permanent from hand onto the battlefield, or -1 to decline. Default = the AI's pick.");
}

// Maelstrom Archangel free-cast decision: the combat-damage trigger's "you MAY cast a spell from your
// hand without paying its mana cost". A ONE-TIME choice (see AIEngine's fire point and the
// FreeCastChooser note) -- not a standing plan-menu option, which is what let the charge be spent at
// any moment in the phase or silently lost. Emits the castable hand cards as image options; the reply
// is a candidate index to cast that spell for free, or -1 to decline. `heuristic_default` = the
// engine's highest-MV pick (the charge is worth most on what you could least afford).
static void WriteFreeCastDecisionJson(std::ostream& os, const GameState& s,
                                      const std::string& source,
                                      const std::vector<Card>& candidates, int heuristic_default,
                                      int decision_index)
{
    DecisionJson d(os, decision_index);
    // heuristic_default is DELIBERATELY -1 (decline), not the AI's pick. It is the answer used for
    // any reference that PREDATES this decision type, and a newly-added "may" trigger must default to
    // the NO-OP or it silently rewrites recorded games: with the highest-MV pick as the default, the
    // seed-6 FiveColour reference had Faeburrow Elder cast for free at T4, so its recorded T5 line
    // ("land=Wooded Foothills; cast: Faeburrow Elder") became unplayable and the protocol checker --
    // which infers a reshuffle from a hand difference -- misreported it as `shuffle-dead`. Nothing had
    // reshuffled; the default had spent a card. The AI's suggestion still ships as `ai_pick` for the
    // viewer's badge, so the player keeps the hint without it being the silent answer.
    //
    // `source` names the offering trigger: "Maelstrom Archangel" (from-hand charge), or the
    // cascade / Breaching Dragonstorm / Creative Technique card whose resolution offers its
    // found card ("you may cast it without paying its mana cost"). The alternative disposition
    // on decline differs per mechanic (bottom / hand / stays exiled) but the reply shape is
    // identical: candidate index to cast free, -1 to decline.
    d.Type("free_cast").Source(source).Turn(s.turn_number).Board(s)
     .HeuristicDefault(-1);
    d.Int("ai_pick", heuristic_default);
    d.Array("candidates", candidates.size(), [&](std::size_t i)
    {
        os << "{ \"index\": " << i << ", \"name\": "; JsonStr(os, candidates[i].m_name.str()); os << " }";
    });
    d.Note("reply a candidate index to cast that spell WITHOUT paying its mana cost, or -1 to decline. Default = the AI's pick.");
}

// Creative Technique demonstrate decision (CR 702.145): "you may copy this spell" -- a yes/no
// asked as the demonstrate trigger resolves, BEFORE the spell's payload runs. Reply 1 = copy
// (the near-dominant line and the engine default), 0/-1 = don't. The opponent's copy is inert
// (the goldfish opponent is never dealt a library), so this only gates YOUR extra payload.
static void WriteDemonstrateDecisionJson(std::ostream& os, const GameState& s,
                                         const std::string& source, bool heuristic_default,
                                         int decision_index)
{
    DecisionJson d(os, decision_index);
    d.Type("demonstrate").Source(source).Turn(s.turn_number).Board(s)
     .HeuristicDefault(heuristic_default ? 1 : 0);
    d.Note("reply 1 to copy the spell (the copy resolves first, with its own free cast), or 0 to decline. Default = copy.");
}

// ETB tutor fired off a PUT rather than a cast (a Lackey combat cheat / Vial deploy / Muxus reveal
// dropping a Goblin Matron): the player picks WHICH card to search up, or declines. A tutor resolved
// from a CAST never reaches here -- the search enumerates one plan variant per candidate and the
// viewer's variant dialog already asks -- so this covers exactly the path that used to pick silently.
// `heuristic_default` = the engine's pick (candidate 0), pre-selected in the viewer.
static void WriteTutorDecisionJson(std::ostream& os, const GameState& s, const std::string& source,
                                   const std::vector<std::string>& candidates, int heuristic_default,
                                   int decision_index)
{
    DecisionJson d(os, decision_index);
    d.Type("tutor_etb").Source(source).Turn(s.turn_number).Board(s).HeuristicDefault(heuristic_default);
    d.Array("candidates", candidates.size(), [&](std::size_t i)
    {
        os << "{ \"index\": " << i << ", \"name\": "; JsonStr(os, candidates[i]); os << " }";
    });
    d.Note("reply a candidate index to search that card up, or -1 to decline the optional search. "
           "Default = the AI's pick.");
}

// Dragonstorm put-order decision (the Dragon override dialog): the player picks WHICH library Dragons
// enter the battlefield (up to `max_puts`); the engine keeps the rule's fixed play order (Lathliss ->
// Scourges -> Utvara -> haste). Emits the candidate Dragon copies as image options IN THAT ORDER, each
// with a `def` flag (in the rule's default selection). The reply is ONE int per candidate (1 = put this
// copy), read positionally like the divide / Soulfire decisions -- so any subset up to max_puts is
// expressible. `ai_set` lists the default-selected candidate indices (the viewer pre-checks them).
static void WriteDragonDecisionJson(std::ostream& os, const GameState& s, const std::string& source,
                                    const std::vector<Card>& candidates, int max_puts,
                                    const std::vector<int>& heuristic_subset, int decision_index)
{
    std::vector<bool> is_def(candidates.size(), false);
    for (int di : heuristic_subset)
    { if (di >= 0 && di < static_cast<int>(candidates.size())) { is_def[di] = true; } }
    DecisionJson d(os, decision_index);
    d.Type("dragon").Source(source).Turn(s.turn_number).Board(s).Int("max_puts", max_puts);
    d.Array("ai_set", heuristic_subset.size(), [&](std::size_t i) { os << heuristic_subset[i]; });
    d.Array("candidates", candidates.size(), [&](std::size_t i)
    {
        os << "{ \"index\": " << i << ", \"def\": " << (is_def[i] ? "true" : "false")
           << ", \"name\": "; JsonStr(os, candidates[i].m_name.str()); os << " }";
    });
    d.Note("reply one int per candidate (1 = put this Dragon), up to max_puts total. "
           "The engine keeps the rule's play order. Default = the AI's pick.");
}

// Defense of the Heart upkeep sac-tutor decision: the player picks WHICH library creature cards
// (up to `max_puts`, possibly none -- "up to two") are put onto the battlefield when the
// enchantment's upkeep trigger resolves. Same payload + reply shape as the Dragon put override:
// candidates are library creature copies (image options), the reply is ONE int per candidate
// (1 = put this copy), `ai_set` = the provider heuristic's default subset (pre-checked).
static void WriteSacTutorDecisionJson(std::ostream& os, const GameState& s, const std::string& source,
                                      const std::vector<Card>& candidates, int max_puts,
                                      const std::vector<int>& heuristic_subset, int decision_index)
{
    std::vector<bool> is_def(candidates.size(), false);
    for (int di : heuristic_subset)
    { if (di >= 0 && di < static_cast<int>(candidates.size())) { is_def[di] = true; } }
    DecisionJson d(os, decision_index);
    d.Type("sac_tutor").Source(source).Turn(s.turn_number).Board(s).Int("max_puts", max_puts);
    d.Array("ai_set", heuristic_subset.size(), [&](std::size_t i) { os << heuristic_subset[i]; });
    d.Array("candidates", candidates.size(), [&](std::size_t i)
    {
        os << "{ \"index\": " << i << ", \"def\": " << (is_def[i] ? "true" : "false")
           << ", \"name\": "; JsonStr(os, candidates[i].m_name.str()); os << " }";
    });
    d.Note("reply one int per candidate (1 = put this creature onto the battlefield), up to "
           "max_puts total. They enter in ascending candidate order. Default = the AI's pick.");
}

// Cleanup-discard decision (#2): the player picks WHICH hand card to discard down to maximum hand
// size. Emits every hand card as an image option; the reply is the hand index to discard. One such
// decision fires per over-limit card.
static void WriteDiscardDecisionJson(std::ostream& os, const GameState& s,
                                     const std::vector<int>& hand_indices, int heuristic_default,
                                     int decision_index)
{
    const Player& ap = s.players[s.active_player_index];
    DecisionJson d(os, decision_index);
    // A COST/TRIGGER discard (Burning-Fist's activation, Neheb's combat trigger) reuses this
    // decision type but is NOT a hand-size shed: it is always exactly ONE card, and the cleanup
    // framing's over_by (hand - 7) goes negative on a small hand ("Select -5 cards", Minotaur
    // seed 1). The context carries the ability's source so the viewer can title it honestly.
    const bool cost_discard = !NonCleanupDiscardContext().empty();
    d.Type("discard").Turn(s.turn_number)
     .Int("over_by", cost_discard ? 1 : static_cast<int>(ap.hand.size()) - 7);
    if (cost_discard)
    {
        d.Word("discard_context", "cost");
        d.Str("source", NonCleanupDiscardContext());
    }
    d.Board(s);
    // AI's FULL cleanup-discard set (original hand indices), by simulating the shared selector forward
    // on a state copy: record the pick, erase it from the copy's hand, repeat until at max hand size.
    // Lets the viewer pre-select and commit the whole set in ONE step instead of one card per engine
    // round-trip. ai_set[0] equals heuristic_default (same selector, same starting state).
    std::vector<int> ai_set;
    {
        GameState copy = s;
        Player& cp = copy.players[copy.active_player_index];
        std::vector<int> orig(cp.hand.size());
        for (int i = 0; i < static_cast<int>(cp.hand.size()); ++i) { orig[i] = i; }
        while (static_cast<int>(cp.hand.size()) > 7)
        {
            int idx = SelectCleanupDiscardIndex(copy, s.m_required_pieces);
            if (idx < 0 || idx >= static_cast<int>(cp.hand.size())) { break; }
            ai_set.push_back(orig[idx]);
            cp.hand.erase(cp.hand.begin() + idx);
            orig.erase(orig.begin() + idx);
        }
    }
    d.Array("ai_set", ai_set.size(), [&](std::size_t i) { os << ai_set[i]; });
    d.HeuristicDefault(heuristic_default);
    d.Array("options", hand_indices.size(), [&](std::size_t i)
    {
        int hi = hand_indices[i];
        os << "{ \"index\": " << hi << ", \"name\": ";
        JsonStr(os, (hi >= 0 && hi < static_cast<int>(ap.hand.size())) ? ap.hand[hi].m_name.str() : std::string());
        os << " }";
    });
    d.Note("reply a hand index -- the card to discard. Default = the AI's pick.");
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
    DecisionJson d(os, decision_index);
    d.Type("expressive_iteration").SourceLiteral("Expressive Iteration").Turn(s.turn_number).Board(s);
    d.Array("looked", static_cast<std::size_t>(look), [&](std::size_t i)
    { os << "{ \"index\": " << i << ", \"name\": "; JsonStr(os, looked[i].m_name.str()); os << " }"; });
    d.HeuristicDefault(heur_option);
    d.Array("options", asg.size(), [&](std::size_t oi)
    {
        int h = asg[oi].first, e = asg[oi].second, b = -1;
        for (int i = 0; i < look; ++i) { if (i != h && i != e) { b = i; break; } }
        os << "{ \"index\": " << oi << ", \"hand\": ";   JsonStr(os, looked[h].m_name.str());
        os << ", \"exile\": ";  JsonStr(os, looked[e].m_name.str());
        os << ", \"bottom\": "; if (b >= 0) { JsonStr(os, looked[b].m_name.str()); } else { os << "null"; }
        os << " }";
    });
    d.Note("reply an option index: 'hand' is banked, 'exile' is playable this turn, 'bottom' goes to the library bottom. Default = the AI's pick.");
}

// Retrace discard decision (Throes of Chaos): the player picks WHICH land in hand to discard as
// Retrace's additional cost. Emits the discardable lands as image options; the reply is the hand
// index to discard. One decision fires per land the retrace cast must discard (retrace = 1).
static void WriteRetraceDiscardDecisionJson(std::ostream& os, const GameState& s, const std::string& source,
                                            const std::vector<int>& hand_land_indices,
                                            int heuristic_default, int decision_index)
{
    const Player& ap = s.players[s.active_player_index];
    DecisionJson d(os, decision_index);
    d.Type("retrace_discard").Source(source).Turn(s.turn_number).Board(s)
     .HeuristicDefault(heuristic_default);
    d.Array("options", hand_land_indices.size(), [&](std::size_t i)
    {
        int hi = hand_land_indices[i];
        os << "{ \"index\": " << hi << ", \"name\": ";
        JsonStr(os, (hi >= 0 && hi < static_cast<int>(ap.hand.size())) ? ap.hand[hi].m_name.str() : std::string());
        os << " }";
    });
    d.Note("reply a hand index -- the land to discard as this spell's Retrace additional cost. Default = the AI's pick.");
}

// Replicate decision (Hatchery Sliver, and any Sliver spell it grants replicate to): the player picks
// HOW MANY times to pay the replicate cost when casting, each making a token copy. The AI default is
// greedy (max affordable). The reply is the chosen count in [0, max_count]; one decision fires per
// replicate-eligible cast that can afford at least one extra copy.
static void WriteReplicateDecisionJson(std::ostream& os, const GameState& s, const std::string& source,
                                       int max_count, int decision_index)
{
    DecisionJson d(os, decision_index);
    d.Type("replicate").Source(source).Turn(s.turn_number).Board(s)
     .Int("max_count", max_count).HeuristicDefault(max_count);
    d.Note("reply how many times to replicate this Sliver spell (0.." + std::to_string(max_count)
           + "); each pays the replicate cost again to make a token copy. Default = replicate the maximum.");
}

// Attach-host decision (Armored Skyhunter): the attack-dig put an Equipment onto the
// battlefield; the player picks WHICH controlled creature it attaches to (board-click, like the
// bounce picker), or -1 to leave it unattached. Rides the positional --choices stream (the
// trigger fires in chronological decision order).
static void WriteAttachHostDecisionJson(std::ostream& os, const GameState& s,
                                        const std::string& source,
                                        const std::vector<int>& legal, int heuristic_default,
                                        int decision_index)
{
    DecisionJson d(os, decision_index);
    d.Type("attach_host").Source(source).Turn(s.turn_number)
     .Board(s).HeuristicDefault(heuristic_default);
    d.Array("options", legal.size(), [&](std::size_t i)
    {
        const Permanent& p = s.battlefield[legal[i]];
        os << "{ \"index\": " << i << ", \"perm_index\": " << legal[i] << ", \"name\": ";
        JsonStr(os, p.card.m_name.str());
        os << ", \"label\": "; JsonStr(os, p.card.m_name.str()); os << " }";
    });
    d.Note("reply an option index -- the creature to attach the put Equipment to, or -1 to "
           "leave it unattached. Default = the AI's pick (best attacker).");
}

// Jitte counter-spend decision (Umezawa's Jitte): at combat, how many charge counters to spend
// on +2/+2 for this attacker. Reply = count in [0, max_count]; -1/default = greedy spend-all
// (incl. double-strike mid-step earnings). Rides the turn-keyed --jitte side-channel, NOT
// --choices (fires mid-combat; existing references replay byte-identically as greedy).
static void WriteJitteDecisionJson(std::ostream& os, const GameState& s,
                                   const std::vector<int>& attacker_indices,
                                   int max_count, int decision_index)
{
    DecisionJson d(os, decision_index);
    d.Type("jitte").Turn(s.turn_number).Board(s)
     .Int("max_count", max_count).HeuristicDefault(max_count);
    d.Array("attackers", attacker_indices.size(), [&](std::size_t j)
    {
        const Permanent& p = s.battlefield[attacker_indices[j]];
        os << "{ \"name\": "; JsonStr(os, p.card.m_name.str()); os << " }";
    });
    d.Note("reply how many Jitte charge counters to spend pre-strike on +2/+2 (0..max_count), "
           "or -1 for the greedy default (spend all, incl. double-strike mid-step earnings).");
}

// Firebreathe-amount decision (#4): at combat, how many pump ACTIVATIONS to buy with leftover mana
// (Scourge {R}:+1/+0 self, Lathliss {1}{R}: team +1/+0). Reply = count in [0, max_count]; default =
// max (the current greedy spend). Rides the turn-keyed --firebreathe side-channel, NOT --choices.
static void WriteFirebreatheDecisionJson(std::ostream& os, const GameState& s,
                                         const std::vector<int>& attacker_indices,
                                         int max_count, int decision_index)
{
    DecisionJson d(os, decision_index);
    d.Type("firebreathe").Turn(s.turn_number).Board(s)
     .Int("max_count", max_count).HeuristicDefault(max_count);
    d.Array("attackers", attacker_indices.size(), [&](std::size_t j)
    {
        const Permanent& p = s.battlefield[attacker_indices[j]];
        os << "{ \"name\": "; JsonStr(os, p.card.m_name.str()); os << " }";
    });
    d.Note("reply how many firebreathing activations to spend leftover combat mana on (0.."
           + std::to_string(max_count) + "); each pumps an attacker. Default = spend the maximum.");
}

// Storage-land tap-vs-charge decision (#6, Dwarven Hold / Mercadian Bazaar): a charged storage land can
// either BURST this turn (tap, spend counters as {R}) or CHARGE (stay untapped -> +1 counter at end of
// turn). Reply = 1 to HOLD (charge / build the battery), 0 to allow the normal tap/burst. Default = 0
// (the current heuristic: burst when a committed line needs it, reserve otherwise). Rides the
// (turn, land number)-keyed --storage-hold side-channel, NOT --choices.
static void WriteStorageHoldDecisionJson(std::ostream& os, const GameState& s,
                                         const Permanent& land, int counters, int decision_index)
{
    DecisionJson d(os, decision_index);
    d.Type("storage_hold").Turn(s.turn_number).Str("land", land.card.m_name.str());
    // Side-channel key: the land's BATTLEFIELD INDEX (its position in state.battlefield). Unique per
    // permanent and deterministic across the stateless replay (same seed/choices/holds => same board =>
    // same index at this consult), so it distinguishes two storage lands charged the same turn -- unlike
    // the card m_number, which is 0 for a played land and whose preservation would perturb the autonomous
    // behaviour digest. Computed from the reference's position (land is always an element of s.battlefield).
    d.Int("land_idx", static_cast<int>(&land - s.battlefield.data()));
    d.Int("counters", counters).HeuristicDefault(0);
    // Charge-mode drives the decision TIMING: "upkeep_if_tapped" (Dwarven Hold) is decided PRE-DRAW
    // (at upkeep -- the human commits without seeing this turn's draw); "tap" (Mercadian Bazaar) is a
    // post-draw main-phase tap. The viewer uses pre_draw to label the harder blind commitment.
    const CardDefinition* ld = CardDatabase::Instance().LookupCached(land.card);
    const bool pre_draw = ld && ld->params.storage_charge_mode == "upkeep_if_tapped";
    d.Str("charge_mode", ld ? ld->params.storage_charge_mode : std::string());
    d.Bool("pre_draw", pre_draw).Board(s);
    d.Note(std::string("reply 1 to HOLD this storage land untapped this turn (charge it: +1 counter at"
                       " end of turn), or 0 to let it tap/burst as needed. ")
           + (pre_draw
              ? "NOTE: decided at UPKEEP, BEFORE your draw -- you commit without seeing this turn's card."
              : "Holding forgoes bursting now to build toward a bigger future burst."));
}

// Land-entry decision (shock lands, and reveal lands like Frostboil Snarl): as the land enters you may
// pay a cost to have it enter UNTAPPED, or let it enter tapped. Shock lands pay `pay_life` life; reveal
// lands reveal a matching land (`reveal_types`) already in hand -- free, but shown as a choice. The AI
// default (heuristic_default = 1 untapped / 0 tapped) is: shock -> pay iff mana is needed this turn;
// reveal -> reveal iff able. The reply is 1 (enter untapped, pay the cost) or 0 (enter tapped).
static void WriteLandEntryDecisionJson(std::ostream& os, const GameState& s, const std::string& source,
                                       int pay_life, const std::vector<std::string>& reveal_types,
                                       bool heuristic_untapped, int decision_index)
{
    DecisionJson d(os, decision_index);
    d.Type("land_entry").Source(source).Turn(s.turn_number).Board(s).Int("pay_life", pay_life);
    d.Array("reveal_types", reveal_types.size(), [&](std::size_t i) { JsonStr(os, reveal_types[i]); });
    d.HeuristicDefault(heuristic_untapped ? 1 : 0);
    d.Note("reply 1 to enter UNTAPPED ("
           + (pay_life > 0 ? "pay " + std::to_string(pay_life) + " life"
                           : std::string("reveal a matching land"))
           + "), or 0 to enter tapped. Default = the AI's pick.");
}


// Parse a --validate-line spec into a LineSpec. Tokens are ';'-separated; each is
// "land=<name>", "cast=<name>", "vial=<name>", "retrace=<name>", "landsedge=<n>",
// "sacout=<outlet name>" (repeat for repeat activations),
// "equip=<equipment name>[#<source m_number>][@<host m_number>]",
// "attachall=<name>", "sfput=<equipment name>", "jittemode=<1|2>", "gyexile=<1|2>",
// "channel=<card name>", or the bare word "pass". Card
// names may contain spaces and commas (no MTG name contains ';' or '='), so they pass through
// verbatim.
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
        else if (key == "sacout")    { ls.sac_outlets.push_back(val); }   // one token per activation
        else if (key == "attachall") { ls.attach_all.push_back(val); }    // Balan attach-all
        else if (key == "sfput")     { ls.sf_puts.push_back(val); }       // Stoneforge put (card name)
        else if (key == "jittemode") { ls.jitte_modes.push_back(std::atoi(val.c_str())); }
        // "equip=<name>[#<source num>][@<host num>]": equip an Equipment already in play. The two
        // optional m_numbers pin WHICH copy attaches to WHICH creature -- see LineSpec::EquipSpec.
        // No MTG card name contains '#' or '@', so splitting on them cannot tear a name in half;
        // a bare "equip=<name>" still parses to the any-source/any-host wildcard it always was.
        else if (key == "equip")
        {
            TurnSolver::LineSpec::EquipSpec es;
            const size_t at = val.rfind('@');
            if (at != std::string::npos)
            { es.host = std::atoi(val.c_str() + at + 1); val = val.substr(0, at); }
            const size_t hash = val.rfind('#');
            if (hash != std::string::npos)
            { es.source = std::atoi(val.c_str() + hash + 1); val = val.substr(0, hash); }
            es.name = val;
            ls.equips.push_back(std::move(es));
        }
        else if (key == "gyexile")   { ls.gy_exiles.push_back(std::atoi(val.c_str())); }  // Deathrite mode
        else if (key == "gyreturn")  { ls.gy_returns.push_back(val); }    // Haven rebuy (returned card)
        else if (key == "channel")   { ls.channels.push_back(val); }      // from-hand channel ability
        else if (key == "suspend")   { ls.suspends.push_back(val); }      // from-hand Suspend (Lotus Bloom)
        else if (key == "animate")   { ls.animates.push_back(val); }      // Mutavault "{1}: 2/2"
        else if (key == "taptoken")  { ls.tap_tokens.push_back(val); }    // Sliver Hive "{5},{T}: token"
    }
    return ls;
}

// ---- --claude-play side-channel spec parsers ----------------------------------------------------
// Each turns one CLI string into the lookup its chooser consults. Pure string -> map: they touch no
// game state, and they were inline blocks inside RunClaudePlay. Every one of them SKIPS a malformed
// token rather than failing -- deliberate (a replay should not die on a stale spec), and preserved.

// --force-mulligan's parser now lives in runner/GoldFishRunner.h (ParseForcedMulliganSpec), so the
// CLI and the pooled batch runner share one implementation of the spec format.

// --firebreathe "<turn>:<count>,..." -> turn -> pump count.
std::map<int, int> ParseFirebreatheSpec(const std::string& firebreathe_spec)
{
    std::map<int, int> firebreathe_by_turn;
    std::stringstream fs(firebreathe_spec);
    std::string ftok;
    while (std::getline(fs, ftok, ','))
    {
        auto fc = ftok.find(':');
        if (fc == std::string::npos) { continue; }
        try { firebreathe_by_turn[std::stoi(ftok.substr(0, fc))] = std::stoi(ftok.substr(fc + 1)); }
        catch (...) { /* skip malformed token */ }
    }
    return firebreathe_by_turn;
}

// --cast-order "<ord>:A|B|C;<ord>:X|Y" -> main-phase ordinal -> card names, in cast order. Names are
// pipe-separated because ',' appears inside real card names.
std::map<int, std::vector<std::string>> ParseCastOrderSpec(const std::string& cast_order_spec)
{
    std::map<int, std::vector<std::string>> cast_order_by_main;
    std::stringstream cs(cast_order_spec);
    std::string entry;
    while (std::getline(cs, entry, ';'))
    {
        auto colon = entry.find(':');
        if (colon == std::string::npos) { continue; }
        int ord = 0;
        try { ord = std::stoi(entry.substr(0, colon)); }
        catch (...) { continue; }
        std::vector<std::string> names;
        std::stringstream ns(entry.substr(colon + 1));
        std::string nm;
        while (std::getline(ns, nm, '|')) { if (!nm.empty()) { names.push_back(nm); } }
        if (!names.empty()) { cast_order_by_main[ord] = std::move(names); }
    }
    return cast_order_by_main;
}

// --force-attackers "<turn>:A|B;<turn>:" -> turn -> the recorded attacker names for that combat
// (empty after ':' = attack with nobody). Same pipe/semicolon discipline as --cast-order (card
// names carry commas, never '|' or ';'). Turns NOT listed keep the natural declaration -- absence
// and "no attack" are different statements, which is why an empty name list is preserved as an
// entry rather than skipped.
std::map<int, std::vector<std::string>> ParseForceAttackersSpec(const std::string& spec)
{
    std::map<int, std::vector<std::string>> attackers_by_turn;
    std::stringstream cs(spec);
    std::string entry;
    while (std::getline(cs, entry, ';'))
    {
        auto colon = entry.find(':');
        if (colon == std::string::npos) { continue; }
        int turn = 0;
        try { turn = std::stoi(entry.substr(0, colon)); }
        catch (...) { continue; }
        std::vector<std::string> names;
        std::stringstream ns(entry.substr(colon + 1));
        std::string nm;
        while (std::getline(ns, nm, '|')) { if (!nm.empty()) { names.push_back(nm); } }
        attackers_by_turn[turn] = std::move(names);
    }
    return attackers_by_turn;
}

// --tap-pref "<turn>:<pre|post>:<idx>,<idx>;..." -> (turn, is_post_main) -> battlefield indices the
// RECORDING tapped in that main phase (the tapped-delta between two same-phase recorded frames).
// The payment greedy prefers these sources; see TapPrefChooser (GameLogger.h).
std::map<std::pair<int, int>, std::set<int>> ParseTapPrefSpec(const std::string& spec)
{
    std::map<std::pair<int, int>, std::set<int>> pref;
    std::stringstream cs(spec);
    std::string entry;
    while (std::getline(cs, entry, ';'))
    {
        auto c1 = entry.find(':');
        if (c1 == std::string::npos) { continue; }
        auto c2 = entry.find(':', c1 + 1);
        if (c2 == std::string::npos) { continue; }
        int turn = 0;
        try { turn = std::stoi(entry.substr(0, c1)); }
        catch (...) { continue; }
        const int post = (entry.substr(c1 + 1, c2 - c1 - 1) == "post") ? 1 : 0;
        std::set<int> idxs;
        std::stringstream ns(entry.substr(c2 + 1));
        std::string tok;
        while (std::getline(ns, tok, ','))
        { try { idxs.insert(std::stoi(tok)); } catch (...) { /* skip malformed token */ } }
        if (!idxs.empty()) { pref[{ turn, post }] = std::move(idxs); }
    }
    return pref;
}

// --storage-hold "<turn>:<land#>:<0|1>,..." -> (turn, land battlefield index) -> hold the counter?
std::map<std::pair<int, int>, bool> ParseStorageHoldSpec(const std::string& storage_hold_spec)
{
    std::map<std::pair<int, int>, bool> storage_hold_by_land;
    std::stringstream ss(storage_hold_spec);
    std::string tok;
    while (std::getline(ss, tok, ','))
    {
        auto c1 = tok.find(':');
        if (c1 == std::string::npos) { continue; }
        auto c2 = tok.find(':', c1 + 1);
        if (c2 == std::string::npos) { continue; }
        try
        {
            int t = std::stoi(tok.substr(0, c1));
            int n = std::stoi(tok.substr(c1 + 1, c2 - c1 - 1));
            int v = std::stoi(tok.substr(c2 + 1));
            storage_hold_by_land[{ t, n }] = (v != 0);
        }
        catch (...) { /* skip malformed token */ }
    }
    return storage_hold_by_land;
}

// ---- --claude-play teardown + output ------------------------------------------------------------
// The engine holds RAW POINTERS to the chooser objects RunClaudePlay owns on its stack, so every one
// has to be cleared before those objects go out of scope. Keeping the full list in one function makes
// a missed entry visible instead of buried at the end of a 1100-line body.
void ClearClaudePlayChoosers()
{
g_play_top_chooser = nullptr;
g_play_target_chooser = nullptr;
g_play_bounce_chooser = nullptr;
g_play_sacrifice_chooser = nullptr;
g_play_dig_chooser = nullptr;
g_play_discard_chooser = nullptr;
g_play_ei_chooser = nullptr;
g_play_retrace_chooser = nullptr;
g_play_replicate_chooser = nullptr;
g_play_land_entry_chooser = nullptr;
g_play_dragon_chooser = nullptr;
g_play_sac_tutor_chooser = nullptr;
g_play_lackey_chooser = nullptr;
g_play_free_cast_chooser = nullptr;
g_play_demonstrate_chooser = nullptr;
g_play_tutor_chooser = nullptr;
g_play_lightpaws_chooser = nullptr;
g_play_firebreathe_chooser = nullptr;
g_play_jitte_chooser = nullptr;
g_play_attach_host_chooser = nullptr;
g_play_loyalty_chooser = nullptr;
g_play_cast_order_chooser = nullptr;
g_play_attackers_chooser = nullptr;
g_play_tap_pref_chooser = nullptr;
g_play_storage_hold_chooser = nullptr;
g_play_draw_sink = nullptr;
g_play_reveal_sink = nullptr;
g_play_event_sink = nullptr;
g_play_dropped_cast_sink = nullptr;
// Was MISSING: installed at RunClaudePlay (the only g_play_* hook set outside Install), so the
// global outlived the harness it pointed into. Harmless only because the process returns from main
// straight after -- but this function's whole point is that one list makes a gap visible.
g_play_soulfire_chooser = nullptr;
// NOTE: g_play_hooks_installed is deliberately NOT reset here -- it is sticky by design (see
// GameLogger.h). Clearing it would be the one unsafe direction if a hook were ever re-installed.
}

// The per-game trace file a --log-dir run writes: the reference format the play viewer and
// test/viewer_protocol_check.py read back.
void WriteClaudePlayTrace(const std::filesystem::path& log_dir, uint64_t seed, int game_index,
                          bool won, int win_turn, const std::string& mulligan_json,
                          const std::vector<std::string>& trace)
{
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
}

// The <<<CLAUDE_RESULT>>> frame. Takes `state` by non-const reference because it temporarily forces
// the goldfish perspective for the board dump and restores it -- the original did the same inline.
void WriteClaudePlayResult(GameState& state, const std::vector<PlayEvent>& event_log,
                           const std::vector<std::string>& dropped_log, bool won, int win_turn,
                           int decisions_made, const std::string& mulligan_json)
{
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
// Server-truth: casts of the final committed line the executor couldn't pay (dropped). Carried on
// the result too so a line that drops a cast AS the game ends is still caught by the viewer (parity
// with the per-decision dropped_casts). Empty (the common case) => the final line fully resolved.
if (!dropped_log.empty())
{
    std::cout << "  \"dropped_casts\": [";
    for (size_t di = 0; di < dropped_log.size(); ++di)
    {
        if (di) { std::cout << ", "; }
        JsonStr(std::cout, dropped_log[di]);
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
}


// Soulfire Eruption's target encoding: SoulfireDig speaks sentinels for the two faces plus raw
// battlefield indices; the generic target decision speaks ChosenTarget. These two map between them.
// Free functions rather than capture-less lambdas inside the harness -- a chooser that captured them
// by reference would dangle the moment the installer returned.
ChosenTarget SentinelToChosen(int t, int ci)
{
    if (t == TARGET_OPP_FACE)  { return ChosenTarget{ 0, 1 - ci, 0 }; }
    if (t == TARGET_SELF_FACE) { return ChosenTarget{ 0, ci,     0 }; }
    return ChosenTarget{ 1, t, 0 };
}

int ChosenToSentinel(const ChosenTarget& c, int ci)
{
    if (c.kind == 0) { return (c.index == ci) ? TARGET_SELF_FACE : TARGET_OPP_FACE; }
    return c.index;
}


// ---- --claude-play harness ----------------------------------------------------------------------
// The eighteen decision choosers plus the state they thread. This exists as ONE object because of a
// lifetime constraint that is easy to miss: the engine stores RAW POINTERS to the chooser objects
// (the g_play_* globals, and AIEngine's external-chooser slots), so every chooser must outlive the
// game. As eighteen separate locals inside RunClaudePlay whose addresses were taken, that was implied
// by nothing; here the harness owns them and the caller owns the harness.
//
// The choosers capture `this` only. That is deliberate and load-bearing: a `[&]` lambda built inside
// a helper function would capture that helper's REFERENCE PARAMETERS, which die when it returns --
// so the naive "pass the context in by reference" split would leave every chooser dangling.
struct ClaudePlayHarness
{
    // ---- context the choosers thread ----
    std::vector<int>       choices;         // the positional --choices stream
    std::filesystem::path  log_dir;
    std::string            validate_line;
    const GameState*       state   = nullptr;
    const MulliganProfile* profile = nullptr;
    int         reveal_count   = 0;
    std::size_t cursor         = 0;         // next unconsumed --choices index
    int         decisions_made = 0;
    // #10: ordinal of the current main-phase decision among all main-phase decisions (the external
    // chooser is called once per main-phase decision, exactly mirroring AIEngine::m_ext_main_ordinal).
    // Emitted in the decision JSON so the viewer keys --cast-order by it. Post-incremented per call.
    int         main_ordinal   = 0;
    std::vector<std::string> trace;   // one entry per RESOLVED decision (for --log-dir)
    // Accurate per-draw reporting: the real draw sites append (turn, card_name) here as cards are
    // drawn (see g_play_draw_sink). It accumulates the draws since the last RESOLVED main-phase
    // decision, so the NEXT emitted main-phase decision reports exactly the new draws for the
    // viewer history. Nulled by RevealLogPause during the search, so only real draws land here.
    std::vector<std::pair<int, std::string>> draw_log;
    // Reveals since the last decision -> the viewer history (see GameLogger.h PlayReveal).
    std::vector<PlayReveal> reveal_log;
    // Life-affecting events (combat/burn/lifegain-loss) since the last resolved main decision, for the
    // viewer history. Same lifecycle as draw_log: real sites append, cleared when a decision is consumed.
    std::vector<PlayEvent> event_log;
    // Server-truth resolution: the DECLARED casts of the last-applied committed plan that the executor
    // could not pay (dropped, left in hand). Same lifecycle as draw_log -- the apply site appends, and
    // it is cleared when a decision is consumed, so the next emitted decision carries exactly the
    // just-committed plan's dropped casts. The browser reads this instead of guessing via detectDropped.
    std::vector<std::string> dropped_log;
    // ---- parsed side channels (keyed args, not the positional --choices stream) ----
    std::map<int, int>                      firebreathe_by_turn;
    std::map<int, std::vector<std::string>> cast_order_by_main;
    std::map<std::pair<int, int>, bool>     storage_hold_by_land;
    std::map<int, int>                      jitte_by_turn;   // Umezawa's Jitte counter-spend
    std::map<int, std::vector<std::string>> attackers_by_turn;   // --force-attackers (ref replay)
    std::map<std::pair<int, int>, std::set<int>> tap_pref_by_phase;   // --tap-pref (ref replay)
    bool firebreathe_prompt  = false;
    bool storage_hold_prompt = false;
    bool jitte_prompt        = false;

    // ---- the chooser objects themselves; the engine holds their addresses ----
    TopChooser            top_chooser;
    TargetChooser         target_chooser;
    BounceChooser         bounce_chooser;
    BounceChooser         sacrifice_chooser;
    DigChooser            dig_chooser;
    LightPawsChooser      lightpaws_chooser;
    LackeyChooser         lackey_chooser;
    FreeCastChooser       free_cast_chooser;
    DemonstrateChooser    demonstrate_chooser;
    TutorChooser          tutor_chooser;
    DragonChooser         dragon_chooser;
    SacTutorChooser       sac_tutor_chooser;
    DiscardChooser        discard_chooser;
    EIChooser             ei_chooser;
    RetraceDiscardChooser retrace_chooser;
    ReplicateChooser      replicate_chooser;
    FirebreatheChooser    firebreathe_chooser;
    FirebreatheChooser    jitte_chooser;        // Umezawa's Jitte counter-spend (same shape)
    BounceChooser         attach_host_chooser;  // Skyhunter attach-host (same shape as bounce)
    LoyaltyTargetChooser  loyalty_chooser;      // planeswalker loyalty-ability target (board click)
    CastOrderChooser      cast_order_chooser;
    AttackersChooser      attackers_chooser;    // --force-attackers (reference replay)
    TapPrefChooser        tap_pref_chooser;     // --tap-pref (reference replay)
    StorageHoldChooser    storage_hold_chooser;
    LandEntryChooser      land_entry_chooser;
    SoulfireTargetChooser soulfire_chooser;

    void Install(AIEngine& ai);

  private:
    // Install() in five parts, grouped by how a chooser gets its answer. Member functions, so
    // each chooser still captures only `this`.
    void InstallEngineChoosers(AIEngine& ai);
    void InstallResolutionChoosers(AIEngine& ai);
    void InstallCardChoosers(AIEngine& ai);
    void InstallSideChannelChoosers(AIEngine& ai);
    void InstallLandAndSoulfireChoosers(AIEngine& ai);
};

void ClaudePlayHarness::Install(AIEngine& ai)
{
    // MUST precede every hook install (see g_play_hooks_installed in GameLogger.h): this is what
    // tells RevealLogPause it can no longer take the "nothing is installed" fast path. Set once,
    // never cleared -- ClearClaudePlayChoosers deliberately leaves it true (sticky by design).
    g_play_hooks_installed   = true;
    g_play_draw_sink         = &draw_log;
    g_play_reveal_sink       = &reveal_log;
    g_play_event_sink        = &event_log;
    g_play_dropped_cast_sink = &dropped_log;

    InstallEngineChoosers(ai);
    InstallResolutionChoosers(ai);
    InstallCardChoosers(ai);
    InstallSideChannelChoosers(ai);
    InstallLandAndSoulfireChoosers(ai);
}

// The five choosers AIEngine owns directly (it stores them, so these are not g_play_* globals):
// the main-phase plan pick plus vial, echo, mulligan and bottom.
void ClaudePlayHarness::InstallEngineChoosers(AIEngine& ai)
{
    ai.SetExternalChooser(
        [this](const GameState& s, const std::vector<TurnSolver::Plan>& plans, bool is_pre) -> int
        {
            int di = static_cast<int>(cursor);
            const int this_main_ordinal = main_ordinal++;   // #10: this decision's main-phase ordinal
            if (cursor < choices.size())
            {
                int chosen = choices[cursor++];
                ++decisions_made;
                if (!log_dir.empty())
                {
                    // Record this resolved decision (state + plans + the chosen index).
                    // Only the completing full-CSV run writes the trace file (below).
                    std::ostringstream ss;
                    ss << "{ \"chosen\": " << chosen;
                    // #10 reference round-trip: record the APPLIED cast order (the human's pin) on the
                    // main-phase trace entry, so a saved reference that reordered can be replayed by the
                    // reference checks (which reconstruct --cast-order "<main_ordinal>:names" from this).
                    // Absent (no reorder) => omitted => the reference replays in canonical order unchanged.
                    if (g_play_cast_order_chooser)
                    {
                        std::vector<std::string> ord = (*g_play_cast_order_chooser)(this_main_ordinal);
                        if (!ord.empty())
                        {
                            ss << ", \"cast_order\": [";
                            for (size_t j = 0; j < ord.size(); ++j) { if (j) ss << ", "; JsonStr(ss, ord[j]); }
                            ss << "]";
                        }
                    }
                    ss << ", \"decision\": ";
                    WriteDecisionJson(ss, s, plans, is_pre, di, reveal_count, draw_log, event_log, dropped_log, this_main_ordinal, reveal_log, chosen);
                    ss << "}";
                    trace.push_back(ss.str());
                }
                // This main-phase decision has been consumed (it was shown in a prior viewer step);
                // its draws were already reported. Clear so the NEXT emitted decision reports only
                // the draws that happen AFTER it (turn draw / cantrip draws of the next segment).
                draw_log.clear();
                reveal_log.clear();
                event_log.clear();
                dropped_log.clear();
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
                        // `num` = the m_number the choice names (an enchant/equip host, a sacrifice
                        // victim), so the GUI can auto-resolve a DRAGGED attach target by identity
                        // rather than by a display name two creatures can share. Omitted when the
                        // choice names no board object (an X value, a mode, a count).
                        if (sub.num != 0) { std::cout << ", \"num\": " << sub.num; }
                        std::cout << " }";
                    }
                    std::cout << "] }";
                }
                std::cout << "],\n";
                std::cout << "  \"decision\": ";
                WriteDecisionJson(std::cout, s, plans, is_pre, di, reveal_count, draw_log, event_log, dropped_log, this_main_ordinal, reveal_log);
                std::cout << "}\n<<<END_VALIDATION>>>\n";
                std::cout.flush();
                std::exit(71);   // distinct code: "validation verdict emitted"
            }
            std::cout << "<<<CLAUDE_DECISION>>>\n";
            WriteDecisionJson(std::cout, s, plans, is_pre, di, reveal_count, draw_log, event_log, dropped_log, this_main_ordinal, reveal_log);
            std::cout << "<<<END_DECISION>>>\n";
            std::cout.flush();
            std::exit(70);   // distinct code: "more input needed"
        });

    // Vial-as-a-choice: claude decides each Aether Vial upkeep charge. Shares the single
    // --choices stream + cursor with the main chooser (consulted at upkeep, before the
    // main phase). Reply 1 = add a counter, 0 = hold. (Future default: only surface this
    // when the decision is genuinely ambiguous, not every upkeep.)
    ai.SetExternalVialChooser(
        [this](const GameState& s, const Permanent& vial, bool heuristic) -> bool
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

    // Echo pay-or-sacrifice (claude-play): the human decides whether to pay each echo creature's upkeep
    // cost. Shares the single --choices stream + cursor with the main chooser (consulted at upkeep,
    // before the main phase). Reply 1 = pay (keep), 0 = sacrifice. AIEngine only calls this when paying
    // is affordable, so every emission is a genuine choice.
    ai.SetExternalEchoChooser(
        [this](const GameState& s, const Permanent& creature, bool heuristic) -> bool
        {
            std::string echo_cost;
            const CardDefinition* d = CardDatabase::Instance().LookupCached(creature.card);
            if (d && d->params.echo_cost) { echo_cost = d->params.echo_cost->ToString(); }
            int di = static_cast<int>(cursor);
            if (cursor < choices.size())
            {
                int chosen = choices[cursor++];
                ++decisions_made;
                if (!log_dir.empty())
                {
                    std::ostringstream ss;
                    ss << "{ \"chosen\": " << chosen << ", \"decision\": ";
                    WriteEchoDecisionJson(ss, s, creature, echo_cost, di, heuristic);
                    ss << "}";
                    trace.push_back(ss.str());
                }
                return chosen != 0;
            }
            std::cout << "<<<CLAUDE_DECISION>>>\n";
            WriteEchoDecisionJson(std::cout, s, creature, echo_cost, di, heuristic);
            std::cout << "<<<END_DECISION>>>\n";
            std::cout.flush();
            std::exit(70);
        });

    // Mulligan keep/mulligan (claude-play): the human drives each London-mulligan attempt, sharing
    // the one --choices stream (these fire FIRST, before any turn decision). Reply 1 keep / 0 mulligan.
    // Skipped entirely under --force-mulligan (that reconstructs an exact recorded hand on the engine).
    ai.SetExternalMulliganChooser(
        [this](const std::vector<Card>& hand, int mull_count, bool on_play, bool ai_keep) -> bool
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
        [this](const std::vector<Card>& hand, int ai_pick, const std::vector<char>& win_opt,
            int step, int total) -> int
        {
            int di = static_cast<int>(cursor);
            // The exhaustive table's joint recommendation (all K at once) when this deck is tabled -- the
            // GUI pre-selects it. Empty for a table-less deck (GUI falls back to the per-step deep hint).
            static const ExhaustiveKeepPolicy kNoExhaustive;   // fallback when the deck has no sidecar
            std::vector<int> ai_set = ExhaustiveBottomSet(
                hand, profile->exhaustive_keep ? *profile->exhaustive_keep : kNoExhaustive,
                total, state->on_the_play);
            if (cursor < choices.size())
            {
                int chosen = choices[cursor++];
                ++decisions_made;
                if (!log_dir.empty())
                {
                    std::ostringstream ss;
                    ss << "{ \"chosen\": " << chosen << ", \"decision\": ";
                    WriteBottomDecisionJson(ss, hand, ai_pick, win_opt, step, total, di, ai_set);
                    ss << "}";
                    trace.push_back(ss.str());
                }
                return chosen;
            }
            std::cout << "<<<CLAUDE_DECISION>>>\n";
            WriteBottomDecisionJson(std::cout, hand, ai_pick, win_opt, step, total, di, ai_set);
            std::cout << "<<<END_DECISION>>>\n";
            std::cout.flush();
            std::exit(70);
        });
}

// Resolution-time choosers that share the positional --choices stream: look-at-top dispositions
// (scry / surveil / reorder) and board-click targeting.
void ClaudePlayHarness::InstallResolutionChoosers(AIEngine& ai)
{
    (void)ai;
    // Look-at-top resolution decisions (Scry / Surveil / Ponder-reorder). Fired from inside
    // ScryTop/SurveilTop/ReorderTopOrShuffle during REAL resolution (gated by g_reveal_logger, so
    // never the search). The player replies one option index into the enumerated dispositions;
    // shares the single --choices stream, consumed in resolution order like the vial decision.
    top_chooser =
        [this](const GameState& s, const std::string& source, const std::vector<Card>& looked,
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
    target_chooser =
        [this](const GameState& s, const CardDefinition& def, int controller, int max_targets,
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
            // Zada/Mirrorwing solo-target trick (viewer feedback 2026-08-12 #2/#3): the legal set is
            // EVERY own creature (the rules-legal board, like Invigorate's own-pump) -- the searched
            // variant target arrives only as the preselected heuristic default. Strive (Twinflame)
            // passes max_targets = 1 + paid extras with the floor equal to the ceiling; an up-to-one
            // trick (Gold Rush / Scale the Heights) allows an EMPTY pick (untargeted cast).
            const bool trick = def.params.solo_target_trick;
            // Sakashima's Protege "enter as a copy of any permanent that entered this turn"
            // (ChooseCopyEntrantIndex routes here with the Protege's own def, e.g. off a cascade
            // flip): the legal set is EVERY battlefield permanent with entered_this_turn -- either
            // side, creature or not (a Breaching Dragonstorm copy re-fires its exile trigger; a
            // land entrant is legal too) -- plus DECLINE (min 0: enter as the printed 3/1). The
            // 5d sweep found the old fall-through to CollectDamageTargets offered faces + stale
            // creatures (silent no-ops) while hiding the real noncreature entrants.
            const bool copy_entrant = def.params.enter_as_copy_of_entrant;
            const std::string copy_desc = copy_entrant
                ? std::string("enters as a copy of the chosen this-turn entrant") : std::string();
            std::vector<ChosenTarget> legal; std::vector<std::string> legal_labels;
            if (copy_entrant)
            {
                for (int bi = 0; bi < static_cast<int>(s.battlefield.size()); ++bi)
                {
                    const Permanent& p = s.battlefield[bi];
                    if (!p.entered_this_turn) { continue; }
                    legal.push_back({ 1, bi, 0 });
                    legal_labels.push_back(p.card.m_name.str()
                        + (p.controller_index == controller ? " (yours)" : " (opponent)"));
                }
            }
            else if (crackle)
            {
                for (int t : CrackleTargetOrder(s, controller, per_target_damage))
                {
                    if      (t == CRACKLE_OPP_FACE)  { legal.push_back({ 0, 1 - controller, 0 }); legal_labels.push_back("Opponent"); }
                    else if (t == CRACKLE_SELF_FACE) { legal.push_back({ 0, controller, 0 });     legal_labels.push_back("You (self)"); }
                    else                             { legal.push_back({ 1, t, 0 });              legal_labels.push_back(s.battlefield[t].card.m_name.str()); }
                }
            }
            else if (trick)          { CollectOwnCreatureTargets(s, controller, legal, legal_labels); }
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
            // Tricks: strive's floor == ceiling == the paid count (heuristic.size() = 1 + extras);
            // an up-to-one single-target trick floors at ZERO (the untargeted cast is a real option).
            const int min_targets =
                  crackle ? std::max(1, static_cast<int>(heuristic.size()))
                : copy_entrant ? 0   // "you MAY have it enter as a copy": decline is a real option
                : (trick && def.params.trick_up_to_one && max_targets == 1) ? 0
                : trick   ? std::min(max_targets, std::max(1, static_cast<int>(heuristic.size())))
                : 1;
            std::vector<TargetOption> opts = EnumerateTargetSets(legal, legal_labels, max_targets, min_targets);
            // No legal target at all: nothing to ask (min 0 would otherwise offer a lone,
            // pointless "(no target)" confirm for an empty-board up-to-one trick).
            if (opts.empty() || legal.empty()) { return heuristic; }
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
                    WriteTargetDecisionJson(ss, s, def.card.m_name.str(), legal, legal_labels, opts, per_target, max_targets, def_idx, di, pump_desc, remove_desc, min_targets, false, "", copy_desc);
                    ss << "}";
                    trace.push_back(ss.str());
                }
                return opts[chosen].targets;
            }
            std::cout << "<<<CLAUDE_DECISION>>>\n";
            WriteTargetDecisionJson(std::cout, s, def.card.m_name.str(), legal, legal_labels, opts, per_target, max_targets, def_idx, di, pump_desc, remove_desc, min_targets, false, "", copy_desc);
            std::cout << "<<<END_DECISION>>>\n";
            std::cout.flush();
            std::exit(70);
        };
    g_play_target_chooser = &target_chooser;
}

// Per-card choosers, all the same shape: enumerate the options, take the recorded pick if the
// --choices stream still has one, else emit the decision and exit(70).
void ClaudePlayHarness::InstallCardChoosers(AIEngine& ai)
{
    (void)ai;
    // Karoo bounce-land return: the player picks which land goes back to hand. Shares the --choices
    // stream; replies one option index into the legal lands. Default = the engine's heuristic pick.
    bounce_chooser =
        [this](const GameState& s, int controller, const std::string& source,
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
    sacrifice_chooser =
        [this](const GameState& s, int controller, const std::string& source,
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
    dig_chooser =
        [this](const GameState& s, int controller, const std::string& source,
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

    // Light-Paws tutor-attach (Light-Paws, Emperor's Voice): the player picks which library Aura it
    // fetches + attaches to itself (or -1 to decline). Shares the --choices stream; the reply is a pool
    // index, or -1. Default = the engine's heuristic pick (the highest static-power eligible Aura).
    lightpaws_chooser =
        [this](const GameState& s, int controller, const std::string& source,
            const std::vector<Card>& pool, const std::vector<int>& legal, int heuristic_pick) -> int
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
                    WriteLightPawsDecisionJson(ss, s, source, pool, legal, heuristic_pick, di);
                    ss << "}";
                    trace.push_back(ss.str());
                }
                return chosen;
            }
            std::cout << "<<<CLAUDE_DECISION>>>\n";
            WriteLightPawsDecisionJson(std::cout, s, source, pool, legal, heuristic_pick, di);
            std::cout << "<<<END_DECISION>>>\n";
            std::cout.flush();
            std::exit(70);
        };
    g_play_lightpaws_chooser = &lightpaws_chooser;

    // Goblin Lackey combat-cheat: on combat damage, the player picks WHICH Goblin permanent to put from
    // hand (or -1 to decline). Shares the --choices stream; the reply is a candidate index, or -1.
    // Default = the engine's heuristic pick (highest-MV matching hand card). Human-play only (nulled for
    // search/rollout), so batch ground truth is unaffected.
    lackey_chooser =
        [this](const GameState& s, int controller, const std::string& source,
            const std::vector<Card>& candidates, int heuristic_index) -> int
        {
            (void)controller;
            int di = static_cast<int>(cursor);
            if (cursor < choices.size())
            {
                int chosen = choices[cursor++];
                ++decisions_made;
                bool ok = (chosen == -1)
                       || (chosen >= 0 && chosen < static_cast<int>(candidates.size()));
                if (!ok) { chosen = heuristic_index; }
                if (!log_dir.empty())
                {
                    std::ostringstream ss;
                    ss << "{ \"chosen\": " << chosen << ", \"decision\": ";
                    WriteLackeyDecisionJson(ss, s, source, candidates, heuristic_index, di);
                    ss << "}";
                    trace.push_back(ss.str());
                }
                return chosen;
            }
            std::cout << "<<<CLAUDE_DECISION>>>\n";
            WriteLackeyDecisionJson(std::cout, s, source, candidates, heuristic_index, di);
            std::cout << "<<<END_DECISION>>>\n";
            std::cout.flush();
            std::exit(70);
        };
    g_play_lackey_chooser = &lackey_chooser;

    // Maelstrom Archangel free cast (one-time triggered choice; see WriteFreeCastDecisionJson).
    // Default = the engine's highest-MV pick. Human-play only, so ground truth is unaffected.
    free_cast_chooser =
        [this](const GameState& s, int controller, const std::string& source,
            const std::vector<Card>& candidates, int heuristic_index) -> int
        {
            (void)controller;
            int di = static_cast<int>(cursor);
            if (cursor < choices.size())
            {
                int chosen = choices[cursor++];
                ++decisions_made;
                bool ok = (chosen == -1)
                       || (chosen >= 0 && chosen < static_cast<int>(candidates.size()));
                if (!ok) { chosen = -1; }        // unusable index -> decline (the no-op), never a cast
                if (!log_dir.empty())
                {
                    std::ostringstream ss;
                    ss << "{ \"chosen\": " << chosen << ", \"decision\": ";
                    WriteFreeCastDecisionJson(ss, s, source, candidates, heuristic_index, di);
                    ss << "}";
                    trace.push_back(ss.str());
                }
                return chosen;
            }
            std::cout << "<<<CLAUDE_DECISION>>>\n";
            WriteFreeCastDecisionJson(std::cout, s, source, candidates, heuristic_index, di);
            std::cout << "<<<END_DECISION>>>\n";
            std::cout.flush();
            std::exit(70);
        };
    g_play_free_cast_chooser = &free_cast_chooser;

    // Creative Technique demonstrate (yes/no; see WriteDemonstrateDecisionJson). Default = copy
    // (the provider's call). Human-play only, so ground truth is unaffected.
    demonstrate_chooser =
        [this](const GameState& s, int controller, const Card& spell,
               bool heuristic_default) -> bool
        {
            (void)controller;
            int di = static_cast<int>(cursor);
            if (cursor < choices.size())
            {
                int chosen = choices[cursor++];
                ++decisions_made;
                const bool copy = (chosen != 0 && chosen != -1);   // 1 = copy; 0/-1 = decline
                if (!log_dir.empty())
                {
                    std::ostringstream ss;
                    ss << "{ \"chosen\": " << (copy ? 1 : 0) << ", \"decision\": ";
                    WriteDemonstrateDecisionJson(ss, s, spell.m_name.str(), heuristic_default, di);
                    ss << "}";
                    trace.push_back(ss.str());
                }
                return copy;
            }
            std::cout << "<<<CLAUDE_DECISION>>>\n";
            WriteDemonstrateDecisionJson(std::cout, s, spell.m_name.str(), heuristic_default, di);
            std::cout << "<<<END_DECISION>>>\n";
            std::cout.flush();
            std::exit(70);
        };
    g_play_demonstrate_chooser = &demonstrate_chooser;

    // ETB tutor off a PUT (Lackey cheat / Vial deploy / Muxus reveal drops a Goblin Matron): the human
    // picks WHICH card to search up, or -1 to decline ("you MAY search"). A tutor from a CAST carries a
    // searched target and never reaches the chooser. Shares the --choices stream; default = candidate 0
    // (the heuristic's pick). Human-play only (nulled for search/rollout), so ground truth is unaffected.
    tutor_chooser =
        [this](const GameState& s, int controller, const std::string& source,
            const std::vector<std::string>& candidates, int heuristic_index) -> int
        {
            (void)controller;
            int di = static_cast<int>(cursor);
            if (cursor < choices.size())
            {
                int chosen = choices[cursor++];
                ++decisions_made;
                bool ok = (chosen == -1)
                       || (chosen >= 0 && chosen < static_cast<int>(candidates.size()));
                if (!ok) { chosen = heuristic_index; }
                if (!log_dir.empty())
                {
                    std::ostringstream ss;
                    ss << "{ \"chosen\": " << chosen << ", \"decision\": ";
                    WriteTutorDecisionJson(ss, s, source, candidates, heuristic_index, di);
                    ss << "}";
                    trace.push_back(ss.str());
                }
                return chosen;
            }
            std::cout << "<<<CLAUDE_DECISION>>>\n";
            WriteTutorDecisionJson(std::cout, s, source, candidates, heuristic_index, di);
            std::cout << "<<<END_DECISION>>>\n";
            std::cout.flush();
            std::exit(70);
        };
    g_play_tutor_chooser = &tutor_chooser;

    // Dragonstorm put override (the Dragon dialog): the player picks WHICH library Dragons enter (up to
    // max_puts); the engine keeps the rule's play order. Reply = one int per candidate (1 = put this
    // copy), read positionally like the divide / Soulfire decisions (any subset up to max_puts is
    // expressible). Default = the rule's selection (heuristic_subset). Human-play only (the chooser is
    // nulled for the search/rollout), so batch ground truth is unaffected.
    dragon_chooser =
        [this](const GameState& s, int controller, const std::string& source,
            const std::vector<Card>& candidates, int max_puts,
            const std::vector<int>& heuristic_subset) -> std::vector<int>
        {
            (void)controller;
            int di = static_cast<int>(cursor);
            const int need = static_cast<int>(candidates.size());
            if (cursor + need <= static_cast<int>(choices.size()))
            {
                std::vector<int> flags(need);
                for (int i = 0; i < need; ++i) { flags[i] = choices[cursor++]; }
                ++decisions_made;
                std::vector<int> picked;
                for (int i = 0; i < need; ++i)
                { if (flags[i] > 0 && static_cast<int>(picked.size()) < max_puts) { picked.push_back(i); } }
                if (!log_dir.empty())
                {
                    std::ostringstream ss;
                    ss << "{ \"chosen\": [";
                    for (int i = 0; i < need; ++i) { if (i) ss << ", "; ss << flags[i]; }
                    ss << "], \"decision\": ";
                    WriteDragonDecisionJson(ss, s, source, candidates, max_puts, heuristic_subset, di);
                    ss << "}";
                    trace.push_back(ss.str());
                }
                return picked;
            }
            std::cout << "<<<CLAUDE_DECISION>>>\n";
            WriteDragonDecisionJson(std::cout, s, source, candidates, max_puts, heuristic_subset, di);
            std::cout << "<<<END_DECISION>>>\n";
            std::cout.flush();
            std::exit(70);
        };
    g_play_dragon_chooser = &dragon_chooser;

    // Defense of the Heart upkeep sac-tutor: the player picks WHICH library creature cards enter
    // (up to max_puts). Same reply shape as the Dragon put override: one 0/1 flag per candidate,
    // read positionally from the --choices stream. Default = the provider's SacTutorPutList pick.
    sac_tutor_chooser =
        [this](const GameState& s, int controller, const std::string& source,
            const std::vector<Card>& candidates, int max_puts,
            const std::vector<int>& heuristic_subset) -> std::vector<int>
        {
            (void)controller;
            int di = static_cast<int>(cursor);
            const int need = static_cast<int>(candidates.size());
            if (cursor + need <= static_cast<int>(choices.size()))
            {
                std::vector<int> flags(need);
                for (int i = 0; i < need; ++i) { flags[i] = choices[cursor++]; }
                ++decisions_made;
                std::vector<int> picked;
                for (int i = 0; i < need; ++i)
                { if (flags[i] > 0 && static_cast<int>(picked.size()) < max_puts) { picked.push_back(i); } }
                if (!log_dir.empty())
                {
                    std::ostringstream ss;
                    ss << "{ \"chosen\": [";
                    for (int i = 0; i < need; ++i) { if (i) ss << ", "; ss << flags[i]; }
                    ss << "], \"decision\": ";
                    WriteSacTutorDecisionJson(ss, s, source, candidates, max_puts, heuristic_subset, di);
                    ss << "}";
                    trace.push_back(ss.str());
                }
                return picked;
            }
            std::cout << "<<<CLAUDE_DECISION>>>\n";
            WriteSacTutorDecisionJson(std::cout, s, source, candidates, max_puts, heuristic_subset, di);
            std::cout << "<<<END_DECISION>>>\n";
            std::cout.flush();
            std::exit(70);
        };
    g_play_sac_tutor_chooser = &sac_tutor_chooser;

    // Cleanup discard (#2): the player picks which hand card to discard to max hand size. Shares the
    // --choices stream; the reply is a hand index. Default = the engine's heuristic pick.
    discard_chooser =
        [this](const GameState& s, int controller, const std::vector<int>& hand_indices, int heuristic_pick) -> int
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
    ei_chooser =
        [this](const GameState& s, const std::vector<Card>& looked, int heur_hand, int heur_exile)
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
    retrace_chooser =
        [this](const GameState& s, int controller, const std::string& source,
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

    // Replicate: the player picks how many times to replicate a Sliver spell on cast (each pays the
    // replicate cost again to make a token copy). Shares the --choices stream; the reply is the count in
    // [0, max_count]. Default (out-of-range or absent) = the engine's greedy heuristic (max affordable).
    replicate_chooser =
        [this](const GameState& s, int controller, const std::string& source, int max_count) -> int
        {
            (void)controller;
            int di = static_cast<int>(cursor);
            if (cursor < choices.size())
            {
                int chosen = choices[cursor++];
                ++decisions_made;
                if (chosen < 0 || chosen > max_count) { chosen = max_count; }
                if (!log_dir.empty())
                {
                    std::ostringstream ss;
                    ss << "{ \"chosen\": " << chosen << ", \"decision\": ";
                    WriteReplicateDecisionJson(ss, s, source, max_count, di);
                    ss << "}";
                    trace.push_back(ss.str());
                }
                return chosen;
            }
            std::cout << "<<<CLAUDE_DECISION>>>\n";
            WriteReplicateDecisionJson(std::cout, s, source, max_count, di);
            std::cout << "<<<END_DECISION>>>\n";
            std::cout.flush();
            std::exit(70);
        };
    g_play_replicate_chooser = &replicate_chooser;
}

// The three KEYED side-channel choosers. They do NOT consume the positional --choices cursor, so
// installing them never shifts an existing reference; each is installed only when it has
// something to do (a recorded answer, or live prompting).
void ClaudePlayHarness::InstallSideChannelChoosers(AIEngine& ai)
{
    (void)ai;
    // #4 Firebreathe-amount: at combat, how many pump activations to buy with leftover mana. Rides a
    // TURN-keyed side-channel (--firebreathe "turn:count,..."), NOT the positional --choices cursor, so
    // it never shifts the stream and existing references (no --firebreathe) replay byte-identically as
    // greedy. Installed ONLY when there is something to do -- a recorded answer to apply (non-empty map)
    // or live prompting (--firebreathe-prompt). Otherwise left null -> AIEngine::Firebreathe stays greedy
    // (no probe, byte-identical) -- which is exactly the reference-check / autonomous case.
    firebreathe_chooser =
        [this](const GameState& s, int controller, const std::vector<int>& attackers, int max_count) -> int
        {
            (void)controller;
            int di = static_cast<int>(cursor);   // informational decision_index for the JSON/trace
            auto it = firebreathe_by_turn.find(s.turn_number);
            if (it != firebreathe_by_turn.end())
            {
                int chosen = it->second;
                if (chosen < 0 || chosen > max_count) { chosen = max_count; }
                if (!log_dir.empty())
                {
                    std::ostringstream ss;
                    ss << "{ \"chosen\": " << chosen << ", \"decision\": ";
                    WriteFirebreatheDecisionJson(ss, s, attackers, max_count, di);
                    ss << "}";
                    trace.push_back(ss.str());
                }
                return chosen;
            }
            if (!firebreathe_prompt) { return -1; }   // reference replay / not-yet-live -> greedy default
            std::cout << "<<<CLAUDE_DECISION>>>\n";
            WriteFirebreatheDecisionJson(std::cout, s, attackers, max_count, di);
            std::cout << "<<<END_DECISION>>>\n";
            std::cout.flush();
            std::exit(70);
        };
    if (firebreathe_prompt || !firebreathe_by_turn.empty()) { g_play_firebreathe_chooser = &firebreathe_chooser; }

    // Umezawa's Jitte counter-spend: same shape and side-channel discipline as firebreathe
    // (turn-keyed --jitte "turn:count,...", or live prompting via --jitte-prompt). Left null
    // otherwise -> the combat core's greedy spend-all stands (reference replay / autonomous).
    jitte_chooser =
        [this](const GameState& s, int controller, const std::vector<int>& attackers, int max_count) -> int
        {
            (void)controller;
            int di = static_cast<int>(cursor);   // informational decision_index for the JSON/trace
            auto it = jitte_by_turn.find(s.turn_number);
            if (it != jitte_by_turn.end())
            {
                int chosen = it->second;
                if (chosen < 0 || chosen > max_count) { chosen = max_count; }
                if (!log_dir.empty())
                {
                    std::ostringstream ss;
                    ss << "{ \"chosen\": " << chosen << ", \"decision\": ";
                    WriteJitteDecisionJson(ss, s, attackers, max_count, di);
                    ss << "}";
                    trace.push_back(ss.str());
                }
                return chosen;
            }
            if (!jitte_prompt) { return -1; }   // reference replay / autonomous -> greedy default
            std::cout << "<<<CLAUDE_DECISION>>>\n";
            WriteJitteDecisionJson(std::cout, s, attackers, max_count, di);
            std::cout << "<<<END_DECISION>>>\n";
            std::cout.flush();
            std::exit(70);
        };
    if (jitte_prompt || !jitte_by_turn.empty()) { g_play_jitte_chooser = &jitte_chooser; }

    // Skyhunter attach-host: which controlled creature the attack-dig's put Equipment attaches
    // to. Rides the positional --choices stream (chronological), exactly like the bounce picker.
    attach_host_chooser =
        [this](const GameState& s, int controller, const std::string& source,
            const std::vector<int>& legal, int heuristic_pick) -> int
        {
            (void)controller;
            int di = static_cast<int>(cursor);
            if (cursor < choices.size())
            {
                int chosen = choices[cursor++];
                ++decisions_made;
                if (chosen != -1 && (chosen < 0 || chosen >= static_cast<int>(legal.size())))
                { chosen = heuristic_pick; }
                if (!log_dir.empty())
                {
                    std::ostringstream ss;
                    ss << "{ \"chosen\": " << chosen << ", \"decision\": ";
                    WriteAttachHostDecisionJson(ss, s, source, legal, heuristic_pick, di);
                    ss << "}";
                    trace.push_back(ss.str());
                }
                return chosen;
            }
            std::cout << "<<<CLAUDE_DECISION>>>\n";
            WriteAttachHostDecisionJson(std::cout, s, source, legal, heuristic_pick, di);
            std::cout << "<<<END_DECISION>>>\n";
            std::cout.flush();
            std::exit(70);
        };
    g_play_attach_host_chooser = &attach_host_chooser;

    // PLANESWALKER LOYALTY TARGET (Oko's +1, Bolas's +3 and -2). The engine hands us the ability's
    // full RULES-legal target set as battlefield indices; we surface it as the ordinary `target`
    // decision so it is picked by CLICKING THE BOARD -- the play-viewer decision principle, and what
    // the player asked for ("use targeting on the board if a targeted ability is chosen",
    // 2026-09-02). Deliberately NOT a new decision type: `target` already owns board-click
    // selection, and a `loyalty` wording field is all the viewer needs (the Soulfire precedent,
    // where a full-board target set also reuses `target`). Rides the positional --choices stream.
    loyalty_chooser =
        [this](const GameState& s, int controller, const std::string& source,
            const std::string& prompt, const std::vector<int>& legal, int heuristic_pick) -> int
        {
            std::vector<ChosenTarget> legal_ct; std::vector<std::string> legal_labels;
            for (int bi : legal)
            {
                if (bi < 0 || bi >= static_cast<int>(s.battlefield.size())) { continue; }
                const Permanent& p = s.battlefield[bi];
                legal_ct.push_back({ 1, bi, 0 });
                // Power/toughness only for creatures -- a land or a Food token has none, and
                // "Island (0/0)" reads as a bug. "(yours)"/"(opponent)" is the same marker the
                // damage-target collector uses, so the viewer's label normaliser already knows it.
                std::string lbl = p.card.m_name.str();
                if (p.card.IsCreature())
                { lbl += " (" + std::to_string(p.EffectivePower()) + "/"
                              + std::to_string(p.EffectiveToughness()) + ")"; }
                lbl += (p.controller_index == controller ? " (yours)" : " (opponent)");
                legal_labels.push_back(lbl);
            }
            if (legal_ct.empty()) { return heuristic_pick; }
            // One option per legal target, built directly rather than via EnumerateTargetSets: that
            // helper walks all 2^n subset masks even when the cap is 1, and this set is every
            // noncreature PERMANENT for Bolas's +3 (both players' lands) -- 20+ by turn 8, i.e.
            // a million wasted masks for n singletons. Option k IS legal[k], which is also the
            // contract the caller's return value relies on.
            std::vector<TargetOption> opts;
            opts.reserve(legal_ct.size());
            for (std::size_t k = 0; k < legal_ct.size(); ++k)
            { opts.push_back({ { legal_ct[k] }, legal_labels[k] }); }
            const int def_idx = (heuristic_pick >= 0 && heuristic_pick < static_cast<int>(opts.size()))
                              ? heuristic_pick : 0;
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
                    WriteTargetDecisionJson(ss, s, source, legal_ct, legal_labels, opts, 0, 1,
                                            def_idx, di, "", "", 1, false, prompt);
                    ss << "}";
                    trace.push_back(ss.str());
                }
                return chosen;
            }
            std::cout << "<<<CLAUDE_DECISION>>>\n";
            WriteTargetDecisionJson(std::cout, s, source, legal_ct, legal_labels, opts, 0, 1,
                                    def_idx, di, "", "", 1, false, prompt);
            std::cout << "<<<END_DECISION>>>\n";
            std::cout.flush();
            std::exit(70);
        };
    g_play_loyalty_chooser = &loyalty_chooser;

    // #10 Cast-order: the human-pinned execution order of a committed main plan's non-sacrifice hand
    // casts. Rides a MAIN-PHASE-ORDINAL-keyed side-channel (--cast-order "<ord>:A|B|C;<ord>:X|Y"),
    // NOT the positional --choices cursor -- the ordinal is the Nth main-phase decision (0-based),
    // names pipe-separated (pipe never appears in a card name, unlike ',' which some names carry).
    // Absent => the map is empty => the chooser returns {} for every ordinal => canonical order =>
    // existing references (no --cast-order) replay byte-identically. Installed only when non-empty.
    cast_order_chooser =
        [this](int main_ordinal) -> std::vector<std::string>
        {
            auto it = cast_order_by_main.find(main_ordinal);
            return it != cast_order_by_main.end() ? it->second : std::vector<std::string>{};
        };
    if (!cast_order_by_main.empty()) { g_play_cast_order_chooser = &cast_order_chooser; }

    // Forced attackers (reference replay): the recorded game's per-turn attack sets. Keyed by TURN
    // (--force-attackers "turn:A|B;turn:"), NOT the positional --choices stream -- absent turns
    // return nullptr so the natural declaration stands, and an empty recorded set (":"), meaning
    // "the recording attacked with nobody", pins exactly that. Installed only when non-empty, so
    // references replayed without the flag are byte-identical to before. See DeclareAttackerIndices
    // (Combat.cpp) for why: the attack heuristic reads the spare-mana pools, so mana-model work can
    // re-decide WHO attacks and tap a creature the recorded post-combat line needed as a source.
    attackers_chooser =
        [this](int turn) -> const std::vector<std::string>*
        {
            auto it = attackers_by_turn.find(turn);
            return it != attackers_by_turn.end() ? &it->second : nullptr;
        };
    if (!attackers_by_turn.empty()) { g_play_attackers_chooser = &attackers_chooser; }

    // Payment-tap preference (reference replay): where the recording brackets a main phase's
    // payment between two same-phase frames, the tapped-delta names the sources it spent
    // (--tap-pref "turn:pre|post:idx,idx;..."). The scarcity greedy prefers those battlefield
    // indices in that (turn, phase); everything else -- other phases, enumeration, the search
    // (chooser nulled by RevealLogPause) -- is untouched. Order bias only, never legality.
    tap_pref_chooser =
        [this](const GameState& s, const Permanent& p) -> bool
        {
            const int post = (s.phase == Phase::PostCombatMain) ? 1 : 0;
            auto it = tap_pref_by_phase.find({ s.turn_number, post });
            if (it == tap_pref_by_phase.end()) { return false; }
            const int idx = static_cast<int>(&p - s.battlefield.data());
            return it->second.count(idx) > 0;
        };
    if (!tap_pref_by_phase.empty()) { g_play_tap_pref_chooser = &tap_pref_chooser; }

    // #6 Storage-land tap-vs-charge: the human's per-(turn, land) tap-vs-charge answers. Keyed by
    // (turn, land BATTLEFIELD INDEX) on a side-channel (--storage-hold "turn:idx:val,...", val 1=HOLD/charge,
    // 0=allow tap), NOT the positional --choices cursor -> existing references (no --storage-hold) replay
    // byte-identically as the burst heuristic. BOTH answers are recorded (a 0 = an explicit "no hold")
    // so live prompting never re-asks an answered land. Installed only when there is a recorded answer or
    // live prompting (--storage-hold-prompt); otherwise null -> the engine never consults it (no hold).
    storage_hold_chooser =
        [this](const GameState& s, const Permanent& land, int counters) -> bool
        {
            int di = static_cast<int>(cursor);   // informational decision_index for the JSON/trace
            const int land_idx = static_cast<int>(&land - s.battlefield.data());   // #6 side-channel key
            auto it = storage_hold_by_land.find({ s.turn_number, land_idx });
            if (it != storage_hold_by_land.end())
            {
                if (!log_dir.empty())
                {
                    std::ostringstream tss;
                    tss << "{ \"chosen\": " << (it->second ? 1 : 0) << ", \"decision\": ";
                    WriteStorageHoldDecisionJson(tss, s, land, counters, di);
                    tss << "}";
                    trace.push_back(tss.str());
                }
                return it->second;   // recorded answer (hold / allow)
            }
            if (!storage_hold_prompt) { return false; }   // reference replay / not-yet-live -> heuristic (no hold)
            std::cout << "<<<CLAUDE_DECISION>>>\n";
            WriteStorageHoldDecisionJson(std::cout, s, land, counters, di);
            std::cout << "<<<END_DECISION>>>\n";
            std::cout.flush();
            std::exit(70);
        };
    if (storage_hold_prompt || !storage_hold_by_land.empty()) { g_play_storage_hold_chooser = &storage_hold_chooser; }
}

// Land-entry (shock lands / Snarls) and Soulfire Eruption targeting.
void ClaudePlayHarness::InstallLandAndSoulfireChoosers(AIEngine& ai)
{
    (void)ai;
    // Land entry (shock lands / Frostboil Snarl): the player chooses whether the land enters untapped
    // (paying its life / revealing a matching land) or tapped. Shares the --choices stream; the reply
    // is 1 (untapped, pay) or 0 (tapped). Default (out-of-range or absent) = the engine's heuristic.
    land_entry_chooser =
        [this](const GameState& s, int controller, const std::string& source, int pay_life,
            const std::vector<std::string>& reveal_types, bool heuristic_untapped) -> bool
        {
            (void)controller;
            int di = static_cast<int>(cursor);
            if (cursor < choices.size())
            {
                int chosen = choices[cursor++];
                ++decisions_made;
                if (chosen != 0 && chosen != 1) { chosen = heuristic_untapped ? 1 : 0; }
                if (!log_dir.empty())
                {
                    std::ostringstream ss;
                    ss << "{ \"chosen\": " << chosen << ", \"decision\": ";
                    WriteLandEntryDecisionJson(ss, s, source, pay_life, reveal_types, heuristic_untapped, di);
                    ss << "}";
                    trace.push_back(ss.str());
                }
                return chosen == 1;
            }
            std::cout << "<<<CLAUDE_DECISION>>>\n";
            WriteLandEntryDecisionJson(std::cout, s, source, pay_life, reveal_types, heuristic_untapped, di);
            std::cout << "<<<END_DECISION>>>\n";
            std::cout.flush();
            std::exit(70);
        };
    g_play_land_entry_chooser = &land_entry_chooser;

    // Soulfire Eruption targeting: the player picks the FULL target set on the board, exactly like
    // Crackle. `legal` is the canonical target order (sentinels for the faces + battlefield indices);
    // `min_targets` is the affordability floor (the count the cast already paid the discount for). We
    // reuse the generic `target` decision + EnumerateTargetSets, tagged random_damage so the viewer
    // shows "a random card's mana value each" instead of a fixed number. The reply is an option index;
    // we map the chosen target set back to the sentinel encoding SoulfireDig expects.
    soulfire_chooser =
        [this](const GameState& s, int controller, const std::string& source,
            const std::vector<int>& legal, int min_targets,
            const std::vector<int>& heuristic_subset) -> std::vector<int>
        {
            // Build the ChosenTarget legal set + labels in canonical order.
            std::vector<ChosenTarget> legal_ct; std::vector<std::string> legal_labels;
            for (int t : legal)
            {
                legal_ct.push_back(SentinelToChosen(t, controller));
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
            for (int t : heuristic_subset) { heur.push_back(SentinelToChosen(t, controller)); }
            auto same = [](const std::vector<ChosenTarget>& a, const std::vector<ChosenTarget>& b) {
                if (a.size() != b.size()) { return false; }
                for (size_t i = 0; i < a.size(); ++i) { if (a[i].kind != b[i].kind || a[i].index != b[i].index) { return false; } }
                return true; };
            int heur_option = 0;
            for (size_t oi = 0; oi < opts.size(); ++oi) { if (same(opts[oi].targets, heur)) { heur_option = static_cast<int>(oi); break; } }
            auto toSentinels = [&](const std::vector<ChosenTarget>& ts) {
                std::vector<int> out; for (const ChosenTarget& c : ts) { out.push_back(ChosenToSentinel(c, controller)); } return out; };
            int di = static_cast<int>(cursor);
            // PER-TARGET reply (issue #8): Soulfire targets ANY number of the legal targets, but the
            // 256-option enumeration can't represent a wide set (with ~13 targets the largest enumerated
            // option was only 8), so a human wanting to hit all opponent creatures had their pick silently
            // dropped to the heuristic floor. Read ONE int per legal target (1 = targeted), like the
            // `divide` decision -- uncapped, so any subset (incl. all creatures) is expressible. The
            // caller (SoulfireDig) validates count >= min_targets and re-charges owed mana. Reference-
            // safe: no saved reference casts Soulfire (all replay Crackle single-target). This chooser is
            // human-play only (nulled for the search), so batch ground truth is unaffected.
            const int need = static_cast<int>(legal_ct.size());
            if (cursor + need <= static_cast<int>(choices.size()))
            {
                std::vector<int> flags(need);
                for (int i = 0; i < need; ++i) { flags[i] = choices[cursor++]; }
                ++decisions_made;
                std::vector<ChosenTarget> picked;
                for (int i = 0; i < need; ++i) { if (flags[i] > 0) { picked.push_back(legal_ct[i]); } }
                const int cnt = static_cast<int>(picked.size());
                const bool ok = cnt >= min_targets && cnt <= max_targets;
                if (!log_dir.empty())
                {
                    std::ostringstream ss;
                    ss << "{ \"chosen\": [";
                    for (int i = 0; i < need; ++i) { if (i) { ss << ", "; } ss << flags[i]; }
                    ss << "], \"decision\": ";
                    WriteTargetDecisionJson(ss, s, source, legal_ct, legal_labels, opts, 0, max_targets, heur_option, di, "", "", min_targets, /*random_damage=*/true);
                    ss << "}";
                    trace.push_back(ss.str());
                }
                return ok ? toSentinels(picked) : heuristic_subset;   // invalid count -> heuristic floor
            }
            std::cout << "<<<CLAUDE_DECISION>>>\n";
            WriteTargetDecisionJson(std::cout, s, source, legal_ct, legal_labels, opts, 0, max_targets, heur_option, di, "", "", min_targets, /*random_damage=*/true);
            std::cout << "<<<END_DECISION>>>\n";
            std::cout.flush();
            std::exit(70);
        };
    g_play_soulfire_chooser = &soulfire_chooser;
}

static int RunClaudePlay(const Decklist& deck, const MulliganProfile& profile,
                         uint64_t seed, int game_index, int max_turns,
                         int lookahead_depth, int timeout_ms, std::vector<int> choices,
                         int reveal_count, const std::filesystem::path& log_dir,
                         const std::string& validate_line = "",
                         const std::string& force_mulligan = "",
                         const std::string& firebreathe_spec = "",
                         bool firebreathe_prompt = false,
                         const std::string& cast_order_spec = "",
                         const std::string& storage_hold_spec = "",
                         bool storage_hold_prompt = false,
                         const std::string& jitte_spec = "",
                         bool jitte_prompt = false,
                         const std::string& force_attackers_spec = "",
                         const std::string& tap_pref_spec = "")
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
        int fcount = 0;
        std::vector<int> fbottom;
        ParseForcedMulliganSpec(force_mulligan, fcount, fbottom);
        ai.SetForcedMulligan(fcount, std::move(fbottom));
    }
    // The choosers and the state they thread. Owned here: the engine keeps raw pointers into it,
    // so it must outlive the game (see ClaudePlayHarness).
    ClaudePlayHarness h;
    h.choices              = std::move(choices);
    h.log_dir              = log_dir;
    h.validate_line        = validate_line;
    h.state                = &state;
    h.profile              = &profile;
    h.reveal_count         = reveal_count;
    h.firebreathe_by_turn  = ParseFirebreatheSpec(firebreathe_spec);
    h.cast_order_by_main   = ParseCastOrderSpec(cast_order_spec);
    h.storage_hold_by_land = ParseStorageHoldSpec(storage_hold_spec);
    h.firebreathe_prompt   = firebreathe_prompt;
    h.storage_hold_prompt  = storage_hold_prompt;
    h.jitte_by_turn        = ParseFirebreatheSpec(jitte_spec);   // same "turn:count" format
    h.jitte_prompt         = jitte_prompt;
    h.attackers_by_turn    = ParseForceAttackersSpec(force_attackers_spec);
    h.tap_pref_by_phase    = ParseTapPrefSpec(tap_pref_spec);
    // Belt-and-braces (see g_play_hooks_installed): this process drives human choosers, so it must
    // never take the pause fast path even if a future chooser is installed outside Install().
    g_play_hooks_installed = true;
    h.Install(ai);

    GameEngine engine(ai);
    int win_turn = engine.RunGame(state, max_turns);
    ClearClaudePlayChoosers();
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
    WriteClaudePlayTrace(log_dir, seed, game_index, won, win_turn, mulligan_json, h.trace);

    WriteClaudePlayResult(state, h.event_log, h.dropped_log, won, win_turn, h.decisions_made,
                          mulligan_json);
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
                         const std::vector<uint64_t>& digests,
                         const std::vector<int>& game_indices = {})
{
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    std::ofstream out(dir / (name + ".wins"));
    char buf[17];
    for (int gi = 0; gi < static_cast<int>(win_turns.size()); ++gi)
    {
        // The GLOBAL game index when the caller supplied one, else the position. They differ only
        // for a job that is a CHUNK of a longer run or that finished short (skipped/voided games are
        // dropped), so every existing caller writes exactly the same bytes as before -- the
        // regression suite's jobs are whole runs at game_index 0 with nothing skipped.
        out << (gi < static_cast<int>(game_indices.size()) ? game_indices[gi] : gi)
            << ' ' << win_turns[gi];
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
//                                    // also: "charge_counters" / "storage_counters", and
//                                    // "equips": "<host card name>" to stage an ATTACHED Equipment/Aura
//     "hand": [ "Aria of Flame", "Invigorate" ],
//     "graveyard": [ "Scourge of Valkas" ],       // stage cards in the graveyard (gy-reading abilities)
//     "library_filler": "Forest", "library_size": 40,   // so draws / rollouts don't run dry
//     "energy_counters": 1,          // stage {E} (Aether Hub's any-colour tap is gated on it)
//     "opponent_library_size": 2,    // stage the OPPONENT'S library (forces opponent_library_dealt
//                                    // on); they draw one at the end of each of OUR turns, and
//                                    // drawing from empty is a deck-out win on THAT turn
//     "depth": 5, "budget_ms": 100, "max_turns": 4,
//     "expect_win_turn": 4,          // optional: nonzero exit if the actual win turn is later (a FAIL)
//     "expect_no_win": true,         // optional: nonzero exit if the engine DID win (negative guard)
//     "expect_opponent_life": 13,    // optional: pin the exact damage a non-lethal payoff dealt
//     "expect_active_life": 20,      // optional: pin OUR life (incidental lifegain / pain taps) --
//                                    // the only assertion that sees a rider-only illegal cast
//     "validate_line": "land=X;cast=A;cast=B",   // optional: run TurnSolver::CheckLine on this board
//     "expect_verdict": "accept",    // ... and fail unless the verdict matches (default "accept").
//                                    // Guards a line the SEARCH would not pick on its own (a
//                                    // goldfish values a do-nothing permanent at 0), which a
//                                    // win-turn assertion structurally cannot see.
//     "expect_variants": 4,          // optional: pin how many sub-decision variants a `choose`
//                                    // offers -- "choose" alone cannot see variants silently
//                                    // DEDUPED away (two same-named hosts sharing a choice string)
//     "log_out": "logs/play/scenario.json" }            // optional: write the per-turn trace
static int RunScenario(const std::filesystem::path& scenario_path)
{
    using json = nlohmann::json;
    std::ifstream in(scenario_path);
    if (!in) { std::cerr << "scenario: cannot open " << scenario_path << "\n"; return 2; }
    json j;
    try { in >> j; }
    catch (const std::exception& e) { std::cerr << "scenario: bad JSON: " << e.what() << "\n"; return 2; }

    // Optional per-fixture env ("env": {"MTG_X": "1"}), applied FIRST -- before any EnvOn read
    // caches its static -- so a fixture can pin the lever it guards (each scenarios.sh fixture
    // runs in its own process, so this cannot leak across fixtures). A fixture whose lever ships
    // default-off stays runnable in the sanity gate without a wrapper script.
    if (j.contains("env"))
    {
        for (const auto& [k, v] : j.at("env").items())
        { EnvPut(k.c_str(), v.get<std::string>().c_str(), /*overwrite=*/true); }
    }

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
    // NOTE: a scenario is a FIXED mid-game board -- it never makes a mulligan/keep decision, so
    // the exhaustive KEEP sidecar (mulligan policy only) is never consulted. Parsing it is pure
    // waste: the 11 MB Anti-Lifegain sidecar took ~67 s (60% of it nlohmann json::parse) and
    // dominated every regression run's scenario-sanity gate; skipping it drops the scenario to
    // ~0.08 s with a byte-identical result. (Mirrors the claude-play sidecar skip.) The eval +
    // value sidecars ARE consulted at search leaves, are small, and stay.
    AttachEvalSidecar(profile, profile_path);        // learned mid-game eval sidecar (search leaves)
    AttachValueSidecar(profile, profile_path);       // learned leaf value sidecar (search leaves)

    const int depth     = j.value("depth", 5);
    const int budget_ms = j.value("budget_ms", 100);
    const int turn      = j.value("turn", 4);
    const int max_turns = j.value("max_turns", turn);

    // Resolve a full Card (P/T, cost, types, keywords) from its name -- the definition's own card, the
    // same object play/cast copies onto a Permanent. Placeholder-by-name would be 0/0 with no cost.
    // Card::m_number is the engine's per-INSTANCE identity: an Equip names its Equipment and host by
    // number, an Aura its target, the free-cast bank its slot. The definition's card carries 0, so
    // building every scenario card from it gave the whole board the SAME number -- and the Equip
    // enumeration's "never equip an Equipment to itself" guard (equipment number == host number)
    // then rejected every pair, so NO equip line existed in any fixture. Hand out distinct numbers
    // in construction order (battlefield, then hand, then library) so a fixture can express one.
    int next_num = 1;
    auto make_card = [&next_num](const std::string& name) -> Card {
        const CardDefinition* d = CardDatabase::Instance().Lookup(name);
        if (!d) { throw std::runtime_error("scenario: unknown card \"" + name + "\""); }
        Card c = d->card; c.m_number = next_num++; c.RehashName(); return c;
    };

    GameState state;
    state.m_provider            = &SelectDecisionProvider(deck);
    // Deck traits + shuffle salts (uses_second_main, dependency pulls, ...). Without this every
    // deck-gated mechanism reads its default inside a fixture: the main-phase filter keys on
    // state.uses_second_main and so NEVER fired in a scenario, silently testing the unfiltered
    // engine no matter what MTG_AL_PHASE/MTG_PHASE_CLASSIFY said.
    GoldFishRunner::StampDeckTraits(state, deck);
    state.players[0].life       = j.value("active_life", 20);
    // Stage RAD COUNTERS directly (Mariposa Military Base). Same reason storage_counters /
    // charge_counters are settable: the counters arrive only from an optional land-ETB mode the
    // search normally DECLINES, so the rad mill is otherwise unreachable from a fixture -- and
    // unreachable code is untested code.
    state.players[0].rad_counters = j.value("rad_counters", 0);
    // Stage ENERGY directly (Aether Hub). Same reason rad_counters is settable: energy arrives only
    // from a land's ETB, and a fixture STAGES its battlefield rather than playing lands out, so the
    // energy-gated colour mode would otherwise be unreachable from a fixture -- and unreachable
    // code is untested code.
    state.players[0].energy_counters = j.value("energy_counters", 0);
    state.players[1].life       = j.value("opponent_life", 20);
    // Stage the OPPONENT'S library at an arbitrary size (core/OpponentDeck.h). Same reason
    // rad_counters is settable: reaching a deck-out through real play means milling 53 cards, which
    // no fixture can do in a handful of turns -- so without this the entire deck-out path, the win
    // check and its win-turn convention would be untestable, i.e. untested. Setting it also forces
    // opponent_library_dealt on, so a fixture can exercise the path on ANY deck -- including one
    // with no way to mill at all, which is what test/scenarios/opponent_deckout.json uses.
    if (j.contains("opponent_library_size"))
    {
        const int n = j.at("opponent_library_size").get<int>();
        state.opponent_library_dealt = true;
        std::vector<Card> empty;
        state.players[1].library.assign(empty.begin(), empty.end());
        int number = opponentdeck::kNumberBase;
        for (int i = 0; i < n; ++i)
        {
            Card c;
            c.m_name = "Forest";   // contents are irrelevant to a deck-out; only the COUNT is
            c.RehashName();
            c.m_number = number++;
            state.players[1].library.push_back(c);
        }
    }
    state.active_player_index   = 0;
    state.priority_player_index = 0;
    state.turn_number           = turn - 1;   // PlayOut steps INTO `turn`
    state.on_the_play           = j.value("on_the_play", true);
    state.game_seed             = j.value("seed", 1);

    // The opponent's real library + opening hand, exactly as SetupGame deals them, so a fixture on
    // a milling deck models the same opponent a real game does. Must come AFTER game_seed is set --
    // the deal is seeded off it. StampDeckTraits deliberately does NOT raise
    // opponent_library_dealt (only Deal may), so without this an EDF fixture would simply get the
    // old library-less opponent. An explicit `opponent_library_size` above has already dealt one,
    // and overriding it here would throw that away, so this stands down when it fired.
    if (!state.opponent_library_dealt)
    {
        opponentdeck::Deal(state, GoldFishRunner::DeckTouchesOpponentZones(deck), state.game_seed);
    }

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
            // Optional counters so a scenario can stage a CHARGED storage land (Mercadian Bazaar /
            // Dwarven Hold: storage_counters) or a primed Aether Vial (charge_counters).
            p.storage_counters  = e.value("storage_counters", 0);
            p.charge_counters   = e.value("charge_counters", 0);
            state.battlefield.push_back(p);
        }
        // Optional ATTACHMENT ("equips": "<host card name>" on an Equipment / Aura entry), resolved
        // after the whole battlefield exists so it can name a host listed later. Same reason
        // charge_counters is settable: an ATTACHED equipment is a state no self-driving sweep
        // reliably reaches, and abilities that read the host (Umezawa's Jitte's "equipped creature
        // gets +2/+2") are unreachable without it. First permanent of that name wins; an unmatched
        // name is a hard scenario error rather than a silently unattached fixture.
        {
            const auto& bf_spec = j.value("battlefield", json::array());
            for (std::size_t i = 0; i < bf_spec.size() && i < state.battlefield.size(); ++i)
            {
                const std::string host = bf_spec[i].value("equips", std::string());
                if (host.empty()) { continue; }
                int host_num = 0;
                for (const Permanent& h : state.battlefield)
                { if (h.card.m_name.str() == host) { host_num = h.card.m_number; break; } }
                if (host_num == 0)
                { std::cerr << "scenario: \"equips\" names no permanent: " << host << "\n"; return 2; }
                const CardDefinition* ed =
                    CardDatabase::Instance().Lookup(state.battlefield[i].card.m_name.str());
                if (ed && ed->params.is_aura) { state.battlefield[i].aura_attached_to = host_num; }
                else                          { state.battlefield[i].equipped_to      = host_num; }
            }
        }
        for (const auto& hc : j.value("hand", json::array()))
        { state.players[0].hand.push_back(make_card(hc.get<std::string>())); }

        // Optional exact top-of-library ("library_top": ["A","B",...] -- index 0 drawn first), so a
        // fixture can pin the draw sequence the interaction under test depends on (a kill turn that
        // needs a land on exactly turn N). Filler follows below it.
        if (j.contains("library_top"))
        {
            for (const auto& tc : j["library_top"])
            { state.players[0].library.push_back(make_card(tc.get<std::string>())); }
        }
        // Filler library so the draw step and lookahead rollouts have cards to draw. Lands by default
        // => the drawn cards are inert and don't perturb the interaction under test.
        const std::string filler = j.value("library_filler", std::string("Forest"));
        const int lib = j.value("library_size", 40);
        for (int i = 0; i < lib; ++i) { state.players[0].library.push_back(make_card(filler)); }
        // Optional GRAVEYARD ("graveyard": [names...]). Needed to fixture any ability that READS the
        // graveyard (Haven of the Spirit Dragon's sac-to-rebuy, Deathrite's exile modes): in a
        // goldfish those zones fill only by cleanup discard, so a seed-driven run reaches the state
        // far too rarely to regression-guard it.
        for (const auto& gc : j.value("graveyard", json::array()))
        { state.players[0].graveyard.push_back(make_card(gc.get<std::string>())); }
        // Optional named library cards ("library": [names...]), appended AFTER the filler -- i.e.
        // at the BOTTOM, below any top-N look window -- so a fixture can stage a card that is
        // reachable by a full-library search (a tutor) but NOT by a top-of-library peek. (The
        // tutor-top reset fixture needs exactly that: Craterhoof findable by Worldly Tutor, absent
        // from Turntimber's blind top-7.)
        for (const auto& lc : j.value("library", json::array()))
        { state.players[0].library.push_back(make_card(lc.get<std::string>())); }
    }
    catch (const std::exception& e) { std::cerr << e.what() << "\n"; return 2; }

    // Optional LINE assertion: reconcile a hand-assembled main-phase line against the model on this
    // exact board (TurnSolver::CheckLine -- the same path the play viewer and --validate-line use).
    // This is the only way to regression-guard a line the SEARCH will not choose on its own: against
    // a passive goldfish an extra do-nothing permanent is worth nothing, so a fixture asserting only
    // a win turn cannot tell "the line is legal and offered" from "the line does not exist". Issue 6
    // (the hasted-dork mana unlock) is exactly that shape. Runs on the board as authored -- no untap
    // or draw step -- which is what makes it a deterministic statement about THIS position.
    if (j.contains("validate_line"))
    {
        GameState s = state;
        s.turn_number = turn;                 // the main phase OF `turn`, not the step into it
        TurnSolver::LineSpec  spec = ParseLineSpec(j.at("validate_line").get<std::string>());
        TurnSolver::LineCheck chk  = TurnSolver::CheckLine(s, /*is_pre_combat=*/true, spec);
        using Vd = TurnSolver::LineCheck::Verdict;
        const char* got =
            chk.verdict == Vd::Accept             ? "accept" :
            chk.verdict == Vd::Choose             ? "choose" :
            chk.verdict == Vd::LegalNotEnumerated ? "legal_not_enumerated" :
            chk.verdict == Vd::Unsupported        ? "unsupported" :
                                                    "illegal";
        const std::string want = j.value("expect_verdict", std::string("accept"));
        std::cout << "scenario: validate_line verdict=" << got
                  << " variants=" << chk.variants.size()
                  << (chk.reason.empty() ? "" : (" reason=\"" + chk.reason + "\"")) << "\n";
        for (const TurnSolver::LineVariant& lv : chk.variants)
        { std::cout << "scenario:   variant " << lv.plan_index << " " << lv.label << "\n"; }
        if (want != got)
        {
            std::cout << "scenario: FAIL expected line verdict " << want << ", got " << got << "\n";
            return 1;
        }
        // Optional COUNT assertion. `choose` alone cannot tell "every sub-decision variant survived"
        // from "some were silently deduped away" -- and a same-name collapse (two creatures with one
        // name sharing a sub-decision choice string) is exactly the second shape, so a fixture that
        // exists to pin it has to say how many it expects.
        if (j.contains("expect_variants"))
        {
            const int want_n = j.at("expect_variants").get<int>();
            if (want_n != static_cast<int>(chk.variants.size()))
            {
                std::cout << "scenario: FAIL expected " << want_n << " line variants, got "
                          << chk.variants.size() << "\n";
                return 1;
            }
        }
        std::cout << "scenario: PASS (line " << got << ")\n";
    }

    const std::map<std::string, std::vector<int>> numbering = GoldFishRunner::BuildCardNumbering(deck);
    GameLogger logger;
    logger.StartGame("scenario", 0, deck_path.stem().string(), state.game_seed, numbering);
    AIEngine   ai(profile, depth, budget_ms);
    // A scenario must model the SAME turn structure the real goldfish runner does, or a fixture
    // silently misrepresents any deck whose combat generates resources: without this the post-combat
    // main is never searched, so a Maelstrom Archangel free cast / Two-Headed Hellkite draw / a
    // vigilant Faeburrow Elder's "attack first, then tap for mana" line simply do not exist inside
    // the fixture and it reports a worse turn than the engine really plays. Mirrors
    // GoldFishRunner::RunGames and the analyzer, which have always set it.
    ai.SetSearchPostCombat(GoldFishRunner::DeckUsesSecondMain(deck));
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

    // Optional DAMAGE assertion: fail (exit 1) unless the opponent is at exactly this life. A win-turn
    // assertion cannot see a payoff that does not finish the game -- an ETB ping that deals the wrong
    // X, or a haste grant that silently fails to let a fresh creature attack, both still produce
    // "no win" on a short fixture. This pins the actual number the board produced.
    if (j.contains("expect_opponent_life"))
    {
        const int exp = j.at("expect_opponent_life").get<int>();
        const int got = state.players[1].life;
        if (got != exp)
        {
            std::cout << "scenario: FAIL expected opponent_life " << exp << ", got " << got << "\n";
            return 1;
        }
        std::cout << "scenario: PASS (opponent_life " << exp << ")\n";
    }

    // Optional OWN-life assertion, the mirror of the above. Pins what the line did to US -- an
    // incidental lifegain (cast_lifegain), a Fastland/painland tap, a phyrexian pay. Some defects
    // are visible ONLY here: an ILLEGALLY-castable rider spell gains life without ever winning or
    // touching the opponent, so both expect_no_win and expect_opponent_life pass unchanged whether
    // the bug is present or not (Oracle's Restoration cast off an empty board -- see
    // scenarios/oracle_own_target_illegal.json). Deliberately placed BEFORE expect_no_win, which
    // returns early on success.
    if (j.contains("expect_active_life"))
    {
        const int exp = j.at("expect_active_life").get<int>();
        const int got = state.players[0].life;
        if (got != exp)
        {
            std::cout << "scenario: FAIL expected active_life " << exp << ", got " << got << "\n";
            return 1;
        }
        std::cout << "scenario: PASS (active_life " << exp << ")\n";
    }

    // Optional ATTACHMENT assertion: {"<aura or equipment name>": "<host name>"}. Pins WHICH
    // permanent an Aura/Equipment ended up on, which no other assertion in this harness can see --
    // attaching to the wrong host changes neither life total nor win turn on a short fixture, so a
    // rules defect in target legality is invisible to every check above. That is not hypothetical:
    // the shroud fix (CR 303.4a targeting + CR 702.18a) was verified by exactly this observable, and
    // pre-fix the aura went to the ILLEGAL host while life and win turn were identical either way.
    //
    // Matches by NAME on both sides and requires exactly one host match, so a fixture cannot pass by
    // accident on a board with two same-named lands. An unattached aura reports "<none>".
    if (j.contains("expect_attachment"))
    {
        for (auto it = j.at("expect_attachment").begin(); it != j.at("expect_attachment").end(); ++it)
        {
            const std::string aura = it.key();
            const std::string want = it.value().get<std::string>();
            std::string got = "<none>";
            int host_num = 0;
            for (const Permanent& p : state.battlefield)
            { if (p.card.m_name.str() == aura) { host_num = p.aura_attached_to; break; } }
            if (host_num != 0)
            {
                for (const Permanent& h : state.battlefield)
                { if (h.card.m_number == host_num) { got = h.card.m_name.str(); break; } }
            }
            if (got != want)
            {
                std::cout << "scenario: FAIL expected " << aura << " attached to \"" << want
                          << "\", got \"" << got << "\"\n";
                return 1;
            }
            std::cout << "scenario: PASS (" << aura << " -> " << want << ")\n";
        }
    }

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

// ---- --cast-order-report ---------------------------------------------------------------------
// Print this deck's CAST ORDER as the engine ranks it, so the ranking can be reviewed per deck
// (USER 2026-08-17: "ideally we should create a ranking for each deck that I can check over").
// The ranks come from the deck's REAL provider via CastOrderRank, not a re-implementation, so the
// report cannot drift from play; MTG_IDEAL_ORDER=1 shows the ideal-order draw tier applied.
// See docs/design/cast-order-ideal-with-ranges.md.
static int RunCastOrderReport(const Decklist& deck, const std::string& deck_path)
{
    GameState state;
    state.m_provider = &SelectDecisionProvider(deck);
    const DecisionProvider& prov = *state.m_provider;
    // Stamp the DECK-LEVEL inputs the main-phase classifier reads, exactly as SetupGame does.
    // Without them the report answers a different question than play does: `deck_feeds_combat`
    // defaults TRUE (so a non-combat deck's draws would read "both" instead of "m2"), and the
    // dependency pulls default FALSE (so Anti-Lifegain's Tainted Remedy / Plague Drone enablers
    // and its Aria of Flame cast-payoff would NOT show the Main1 pull the real game gives them).
    // The per-STATE inputs -- haste from lords in play, hand haste access, a scaling attacker --
    // are board facts with no static answer; the column is the BASELINE, and the header says so.
    GoldFishRunner::StampDeckTraits(state, deck);

    struct Row { std::string name; int rank; int key; int ideal; int mv; int count;
                 std::string note; const char* mp; };
    std::vector<Row> rows;
    for (const Card& c : deck.mainboard)
    {
        const CardDefinition* d = CardDatabase::Instance().LookupCached(c);
        if (!d || d->card.IsLand()) { continue; }
        bool seen = false;
        for (Row& r : rows) { if (r.name == c.m_name.str()) { ++r.count; seen = true; break; } }
        if (seen) { continue; }
        const int rank = prov.CastOrderRank(state, *d);
        // The RANGE's ideal end. A provider that adopted the reviewed full order
        // (OrderOpaqueCastsByRank) has NO draw promotion -- its ranks ARE the order, and the only
        // movement left is a FUNDING ladder (CastOrderFallbackRanks), whose earliest rung is shown
        // as the range's far end. Otherwise the legacy display: a draw's ideal position is the
        // information tier whether or not the discernment awarded it that RANK -- the ladder may
        // still walk a big draw early when the mana is genuinely there.
        const std::vector<int> fb = prov.CastOrderFallbackRanks(state, *d);
        const bool adopted_order = prov.OrderOpaqueCastsByRank();
        const int ideal = !fb.empty() ? *std::min_element(fb.begin(), fb.end())
                        : (adopted_order ? rank : (IsIdealOrderDraw(*d) ? 2 : rank));
        const int mp = TurnSolver::ClassifyCastMainPhase(state, *d);
        const char* mps = (mp == 0 ? "m1" : (mp == 1 ? "m2" : "both"));
        std::string note;
        if (!fb.empty())
        {
            note = "FUNDING ladder: prefers the late slot, walks earlier only while the line cannot pay (";
            for (std::size_t fi = 0; fi < fb.size(); ++fi)
            { note += (fi ? "->" : "") + std::to_string(fb[fi]); }
            note += ")";
        }
        else if (!adopted_order && IsIdealOrderDraw(*d))
        {
            note = IsIdealOrderCantrip(*d)
                 ? "cantrip"
                 : "draw, but its cost IS the turn -> cost-efficient end";
        }
        if (d->params.spectacle_cost)
        {
            note += note.empty() ? "" : "; ";
            note += "SPECTACLE: cheap only after damage -- conditional position, see doc";
        }
        // Sort by the REAL cast key, not the bare rank: with MTG_ORDER_M1_FIRST the natural
        // phase breaks a rank tie (USER: "m1 cards should be first in the order if there is no
        // reason to do it otherwise"), and a report that sorted by rank alone would show Birds of
        // Paradise ahead of Ignoble Hierarch while play did the opposite.
        rows.push_back(Row{ c.m_name.str(), rank, CastOrderKey(state, d, rank), ideal,
                            d->card.m_mana_cost.ManaValue(), 1, note, mps });
    }
    std::stable_sort(rows.begin(), rows.end(),
        [](const Row& a, const Row& b)
        { if (a.key != b.key) { return a.key < b.key; }
          if (a.mv  != b.mv)  { return a.mv  < b.mv;  }
          return a.name < b.name; });

    // Tier names mirror GenericProvider::CastOrderRank's documented tiers exactly; a provider
    // that defines its own tiers overrides per rank (CastOrderTierName).
    auto tier_name = [&prov](int r) -> const char*
    {
        if (const char* n = prov.CastOrderTierName(r)) { return n; }
        switch (r)
        {
            case 0:  return "ENABLER (lifegain->loss): first, so same-turn payloads see the flip";
            case 2:  return "DRAW: information before land drops and rituals (MTG_IDEAL_ORDER)";
            case 5:  return "MANA ROCK: online for the rest of the line";
            case 10: return "CREATURE: before noncreature spells (prowess catches later casts)";
            case 15: return "RITUAL: float online before the payoff";
            case 16: return "COST REDUCER: after the rituals that fund it, before a restrictor";
            case 18: return "CAST RESTRICTOR (Irencrag): last ritual, only the payoff may follow";
            case 19: return "CAST PAYOFF (verse): before the spells that feed it";
            case 20: return "other noncreature spell";
            case 21: return "POWER-PAYOFF (Swords): after the pumps that raise the power it pays";
            case 24: return "GIFT BODY: an alt-cost gift wearing a creature's clothes -- with the m2 group";
            case 22: return "SCALING AURA: later, so earlier fetches can still take one";
            case 23: return "AURA WITH ENCHANT REQUIREMENT: last (legality, not preference)";
            case 30: return "LAST: on-cast self-damage / destroy-all-enchantments";
            default: return "";
        }
    };

    // THE LAND DROP is part of the deck's order too (USER 2026-08-18: "land drops should be
    // ordered for decks. In this case, it should be put at the start, since there is no draw"),
    // but it is not a CAST and so has no CastOrderRank -- which is exactly why it was missing from
    // a report built out of ranks. Print where it actually happens so the position is reviewable:
    // at depth 0 it precedes every cast (one hand-written exception, the Treasure Hunt defer), and
    // at depth > 0 it is folded into the search rather than ranked at all.
    const bool deck_has_promoted_cantrip = [&]
    {
        for (const Card& c : deck.mainboard)
        {
            const CardDefinition* d = CardDatabase::Instance().LookupCached(c);
            if (d && IsIdealOrderCantrip(*d)) { return true; }
        }
        return false;
    }();

    std::cout << "# Cast order -- " << deck_path << "\n";
    std::cout << "# provider: " << prov.Name()
              << "   ideal-order draw tier: " << (EnvOn("MTG_IDEAL_ORDER") ? "ON" : "off")
              << "   cantrip max mv: " << IdealOrderCantripMaxMv()
              << "\n# deck flags: feeds_combat=" << (state.deck_feeds_combat ? "yes" : "no")
              << " uses_second_main=" << (state.uses_second_main ? "yes" : "no")
              << " enabler_pull=" << (state.dep_enabler_main1 ? "yes" : "no")
              << " castpayoff_pull=" << (state.dep_castpayoff_main1 ? "yes" : "no")
              << "\n# main = BASELINE: board-dependent pulls (haste from a lord in play, hand haste"
                 " access, a scaling attacker) can move a creature m2 -> m1 in an actual game.\n"
              << "#\n# rank  range      main  n  mv  card\n";
    std::cout << "#\n# [LAND] LAND DROP: "
              << (deck_has_promoted_cantrip
                    ? "this deck HAS a cantrip, so \"draw before the land\" is a live question"
                    : "no cantrip in the deck, so nothing wants to precede it -> FIRST")
              << "\n  land    -   m1    -  -   (the turn's land drop)"
                 "   -- before every cast at depth 0; SEARCHED (folded into the plan) at depth > 0\n";
    int last = -999;
    for (const Row& r : rows)
    {
        if (r.rank != last)
        {
            const char* t = tier_name(r.rank);
            std::cout << "#\n# [" << r.rank << "] " << (*t ? t : "(unclassified)") << "\n";
            last = r.rank;
        }
        std::string range = (r.ideal == r.rank)
                          ? std::string("-")
                          : ("[" + std::to_string(r.ideal) + ".." + std::to_string(r.rank) + "]");
        std::cout << "  " << r.rank << "     " << range << "   " << r.mp << "    " << r.count
                  << "  " << r.mv << "  " << r.name
                  << (r.note.empty() ? "" : "   -- " + r.note) << "\n";
    }
    return 0;
}

int main(int argc, char* argv[])
{
    // Apply committed heuristic defaults BEFORE anything reads a toggle (env vars still override).
    ApplyHeuristicDefaults();
    // Warn on MTG_* env vars this binary does not read (typo / deleted flag = silent no-op).
    WarnUnknownMtgFlags();
    ValidateHeuristicArmNames();
    if (argc >= 2 && std::string(argv[1]) == "--list-flags") { PrintFlagRegistry(std::cout); return 0; }
    // Arm the colored_creature_only legality audit's exit dump (MTG_CCO_AUDIT); no-op when unset.
    CcoAuditDumper();
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
                // Goldfish metric = avg (mean turn-to-win; an unwon game is scored max_turns+1).
                // Win/loss is NOT reported: a goldfishing loss is an arbitrary "no lethal by
                // max_turns" threshold, and reporting it makes readers treat it as the priority
                // metric. avg (to 4 dp) + the play digest are the case fingerprint. (games_won lives
                // in the result for a future 1v1 mode.) See ComputeAvgTurns / docs metric-avg-loss-as-9.
                // FILES FIRST, THEN THE LINE. The result line is what a driver waits on, so it has
                // to be a PROMISE that this job's per-game files are already on disk. Announcing
                // first is a race the driver loses often enough to matter: it wakes from readline,
                // opens <name>.wins before this thread has written it, finds nothing, and falls
                // back to storing the chunk MEAN-ONLY. Measured 2026-08-15 on a 12-chunk burn
                // matrix: 9 of 12 chunks lost their per-game rows that way, and the loss is worse
                // than it looks -- a chunk SHORTENED by abandonment then records n survivors with
                // no offsets, so it is read as covering off..off+n (a contiguous range it does not
                // hold: one chunk stored "0..7" while actually holding {1,2,4,7,12,17,19,22}), and
                // the abandoned games never reach the skip list, so every resume re-runs and
                // re-abandons them at full cost. Ordering costs one small write before a print.
                if (!game_log_dir.empty())
                {
                    WriteGameLog(game_log_dir, r.name, r.win_turns, r.digests, r.game_indices);
                    // Per-game search work, under MTG_DUMP_UNITS. A SEPARATE file on purpose: the
                    // .wins format is consumed by the regression suite's committed ground truth, so
                    // adding a column there would churn every gt_log for a diagnostic.
                    if (!r.units.empty())
                    {
                        std::ofstream uo(game_log_dir / (r.name + ".units"));
                        for (std::size_t i = 0; i < r.units.size(); ++i)
                        {
                            uo << (i < r.game_indices.size() ? r.game_indices[i]
                                                             : static_cast<int>(i))
                               << ' ' << r.units[i] << '\n';
                        }
                    }
                    // Games ABANDONED at the work ceiling, one global index per line. A driver
                    // cannot infer these from the hole in <name>.wins: a job also finishes short
                    // when its cell was condemned and its remaining games were skipped at dequeue,
                    // and those two holes call for opposite handling (BatchJobResult::abandoned).
                    // Written only when there are any, so the usual case adds no file.
                    if (!r.abandoned.empty())
                    {
                        std::ofstream ao(game_log_dir / (r.name + ".abandoned"));
                        for (int gi : r.abandoned) { ao << gi << '\n'; }
                    }
                }
                // Goldfish metric = avg (mean turn-to-win; an unwon game is scored max_turns+1).
                // Win/loss is NOT reported: a goldfishing loss is an arbitrary "no lethal by
                // max_turns" threshold, and reporting it makes readers treat it as the priority
                // metric. avg (to 4 dp) + the play digest are the case fingerprint. (games_won lives
                // in the result for a future 1v1 mode.) See ComputeAvgTurns / docs metric-avg-loss-as-9.
                char dbuf[17];
                std::snprintf(dbuf, sizeof(dbuf), "%016llx",
                              static_cast<unsigned long long>(r.case_digest));
                char avgbuf[32];
                std::snprintf(avgbuf, sizeof(avgbuf), "%.4f", r.avg_turns);
                std::cout << r.name << ": played=" << r.games_played
                          << " avg=" << avgbuf
                          << " digest=" << dbuf
                          << " ms=" << r.elapsed_ms << "\n" << std::flush;
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
        // Counters, same as the single-run path below. WITHOUT this an instrumented (-DMTG_PROFILE=ON)
        // batch run emits NO counters and still exits 0 -- a harness parsing them gets a clean, EMPTY
        // cost table, which reads exactly like "the two arms cost the same". That is the conclusion
        // such a measurement is usually run to test, and it cost a full cycle of the Goblins
        // value-leaf cost A/B before the empty table was traced to the mode rather than the build.
        // The trap was sharp because this repo MANDATES pooling long runs into ONE --batch, so the
        // sanctioned instrument was missing from exactly the mode required for the runs big enough to
        // need it -- pushing perf claims back onto wall clock, which is documented to lie here.
        // No-op (expands to nothing) unless MTG_PROFILE is defined, so Release codegen is unchanged.
        PROF_REPORT(std::cerr);
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
    bool     depth_provided  = false;      // --depth explicitly given (vs the default 0); feeds ResolvePlaySettings
    bool     budget_provided = false;      // --budget-ms/--timeout-ms explicitly given
    bool     ignore_play_profile = false;  // --ignore-play-profile: bypass a derived value_play lock
    int      num_threads    = 0;
    uint64_t seed           = 0;
    bool     seed_provided  = false;
    bool     diag_depth     = false;
    bool     cast_order_report = false;   // --cast-order-report: print the deck's cast order, then exit
    bool     trace_t1       = false;
    bool        claude_play = false;
    bool        force_exhaustive_keep = false;  // --exhaustive-keep: load the exhaustive keep sidecar
                                                // even under claude-play (which skips it by default)
    std::string choices_str;          // comma-separated plan indices for --claude-play
    std::string firebreathe_str;      // #4: "turn:count,..." firebreathe-amount side-channel (turn-keyed)
    bool firebreathe_prompt = false;  // #4: --firebreathe-prompt -> exit-70 to ask when a turn is unanswered
    std::string jitte_str;            // Umezawa's Jitte: "turn:count,..." counter-spend side-channel
    bool jitte_prompt = false;        // --jitte-prompt -> exit-70 to ask when a turn is unanswered
    std::string cast_order_str;       // #10: "<ord>:A|B|C;..." cast-order side-channel (main-ordinal-keyed)
    std::string force_attackers_str;  // ref replay: "<turn>:A|B;..." forced-attackers side-channel (turn-keyed)
    std::string tap_pref_str;         // ref replay: "<turn>:<pre|post>:<idx>,..." payment-tap preference
    std::string storage_hold_str;     // #6: "turn:num:val,..." storage tap-vs-charge side-channel
    bool storage_hold_prompt = false; // #6: --storage-hold-prompt -> exit-70 to ask per charged storage land
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
        if (flag == "--cast-order-report")   { cast_order_report = true; continue; }
        if (flag == "--diag-depth")          { diag_depth = true; continue; }
        if (flag == "--trace")               { trace_t1 = true; continue; }
        if (flag == "--claude-play")         { claude_play = true; continue; }
        if (flag == "--exhaustive-keep")     { force_exhaustive_keep = true; continue; }
        if (flag == "--ignore-play-profile") { ignore_play_profile = true; continue; }
        if (flag == "--eval-draw")           { eval_on_play = false; continue; }
        if (flag == "--storage-hold-prompt") { storage_hold_prompt = true; continue; }   // #6: value-less; parse regardless of position (the else-if chain below is gated on i+1<argc, so a trailing value-less flag would be dropped)
        if (flag == "--firebreathe-prompt")  { firebreathe_prompt = true; continue; }    // #4: value-less, same trap as above
        if (flag == "--jitte-prompt")        { jitte_prompt = true; continue; }          // Jitte spend: value-less, same trap as above
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
                else if (flag == "--firebreathe")
                {
                    // #4 firebreathe-amount side-channel: "turn:count,turn:count" (turn -> pump
                    // activations). Keyed by turn (firebreathing fires once per combat), so it never
                    // touches the positional --choices stream -> existing references replay unchanged.
                    firebreathe_str = argv[++i];
                }
                else if (flag == "--jitte")
                {
                    // Umezawa's Jitte counter-spend side-channel: "turn:count,..." (same format and
                    // turn-keyed discipline as --firebreathe; references without it replay greedy).
                    jitte_str = argv[++i];
                }
                else if (flag == "--tap-pref")
                {
                    // Reference-replay payment-tap preference: "<turn>:<pre|post>:<idx>,...;..."
                    // (the recording's tapped-delta for that main phase). Keyed, never positional.
                    tap_pref_str = argv[++i];
                }
                else if (flag == "--force-attackers")
                {
                    // Reference-replay attacker pin: "<turn>:A|B;<turn>:" (turn -> recorded attacker
                    // names; empty list = attack with nobody). Turn-keyed like --firebreathe, so it
                    // never touches the positional --choices stream; unlisted turns declare naturally.
                    force_attackers_str = argv[++i];
                }
                else if (flag == "--cast-order")
                {
                    // #10 cast-order side-channel: "<ord>:A|B|C;<ord>:X|Y" (main-phase decision ordinal ->
                    // pinned non-sac hand-cast order, pipe-separated names). Keyed by main-phase ordinal, so
                    // it never touches the positional --choices stream -> existing references replay unchanged.
                    cast_order_str = argv[++i];
                }
                else if (flag == "--storage-hold")
                {
                    // #6 storage tap-vs-charge side-channel: "turn:idx:val,..." (val 1=hold/charge, 0=allow
                    // tap; idx = land battlefield index). Keyed by (turn, index), so it never touches the
                    // positional --choices stream -> existing references replay unchanged (burst heuristic).
                    storage_hold_str = argv[++i];
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
                    depth_provided  = true;
                }
                else if (flag == "--timeout-ms" || flag == "--budget-ms")
                {
                    // Deterministic search budget in "virtual ms" (see SearchBudget).
                    // --timeout-ms kept as a back-compat alias for the same knob.
                    timeout_ms      = std::stoi(argv[++i]);
                    budget_provided = true;
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
        if (cast_order_report) { return RunCastOrderReport(deck, deck_path.string()); }
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
        // The exhaustive keep sidecar is a large JSON that costs ~68s to parse per launch and
        // dominates claude-play's stateless-replay cost. claude-play is a PLAY-verification tool
        // (can a human out-play the engine / are there play bugs?) where mulligan optimality is
        // irrelevant -- the base profile's static keep still tosses 0-land/flood hands, which is all
        // this needs ("if play is reliable, so is the mulligan table"). So skip the sidecar under
        // claude-play by default; --exhaustive-keep opts back in for the optimal keep hint. The
        // autonomous / batch / scenario paths always load it (unchanged, byte-identical).
        if (!claude_play || force_exhaustive_keep)
        {
            AttachExhaustiveSidecar(profile, profile_path);  // play uses the deck's exhaustive sidecar if present
        }
        AttachEvalSidecar(profile, profile_path);        // ... and its learned mid-game eval sidecar if present
        AttachValueSidecar(profile, profile_path);       // ... and its learned leaf value sidecar if present

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
            //
            // DIAGNOSTIC HATCH (MTG_CLAUDE_PLAY_SHIPPED_PRUNING=1, default unset = unchanged):
            // keep the SHIPPED pruning instead. Needed to ask "what does the real engine actually
            // enumerate / would it pick here?" -- with the widening above, every emitted plan list
            // and `ai_choice` is the UNPRUNED engine's, so claude-play cannot answer a question
            // about a prune gate (e.g. a reference the shipped search loses to the human). Opt-in
            // only, so the play GUI and every reference replay stay byte-identical.
            if (!EnvOn("MTG_CLAUDE_PLAY_SHIPPED_PRUNING"))
            {
                EnvPut("MTG_UNPRUNED", "1", /*overwrite=*/true);
                EnvPut("MTG_PONDER_SEARCH", "1", /*overwrite=*/true);
            }
            // Human play executes EXACTLY the committed plan -- no auto re-solve after a draw, no
            // auto-dig, no auto Land's Edge. Instead the chooser re-fires after any draw so the
            // human re-decides with the revealed cards (a draw "breakpoint"). See ApplyPlanDirect
            // (gated on MTG_HUMAN_PLAY) and AIEngine's external-chooser segment loop.
            EnvPut("MTG_HUMAN_PLAY", "1", /*overwrite=*/true);

            std::vector<int> choices;
            std::stringstream ss(choices_str);
            std::string tok;
            while (std::getline(ss, tok, ','))
            {
                if (!tok.empty()) { choices.push_back(std::stoi(tok)); }
            }
            return RunClaudePlay(deck, profile, seed, base_game_index, max_turns,
                                 lookahead_depth, timeout_ms, choices, reveal_count, log_dir,
                                 validate_line, force_mulligan, firebreathe_str, firebreathe_prompt,
                                 cast_order_str, storage_hold_str, storage_hold_prompt,
                                 jitte_str, jitte_prompt, force_attackers_str, tap_pref_str);
        }

        // Forced-mulligan replay (isolates play from mulligan/bottoming): reconstruct a recorded
        // opening hand on the autonomous search. Parse "<count>:<n1,n2,...>"; inert when unset.
        int              fm_count = -1;
        std::vector<int> fm_bottom;
        if (!force_mulligan.empty())
        { ParseForcedMulliganSpec(force_mulligan, fm_count, fm_bottom); }

        // Resolve the effective play settings from the CLI request + the deck's value_play (auto-attached
        // above). Byte-identical whenever --depth or --budget-ms is given (explicit mode reproduces the old
        // CLI defaults); the built-in d5/20 default applies only to a fully-bare run. Prints the resolved
        // (depth,budget,source) to stderr so the play policy is never hidden. See ResolvePlaySettings.
        try
        {
            PlaySettings ps = ResolvePlaySettings(profile,
                depth_provided  ? lookahead_depth : -1,
                budget_provided ? timeout_ms      : -1,
                ignore_play_profile);
            lookahead_depth = ps.depth;
            timeout_ms      = ps.budget_ms;
            std::cerr << "[play] depth=" << ps.depth << " budget=" << ps.budget_ms
                      << "ms source=" << ps.source << "\n";
        }
        catch (const std::exception& e) { std::cerr << "Error: " << e.what() << "\n"; return 1; }

        GoldFishRunner runner;
        // --trace on the NORMAL goldfish path: print the search's own per-candidate T1 win-turn
        // estimates. SetTraceSolve is a process-global and the trace interleaves, so it is only
        // meaningful single-threaded on one game -- which is exactly the repro shape for a
        // reference shortfall ("why did the search pick THAT plan?"). Previously the flag was
        // reachable only via --diag-depth, so the shipped runner could not be asked that question.
        const bool trace_goldfish = trace_t1 && num_games == 1 && num_threads == 1;
        if (trace_goldfish) { TurnSolver::SetTraceSolve(true); }
        RunResult result = runner.Run(deck, num_games, seed, max_turns, profile, log_dir,
                                       base_game_index, lookahead_depth, timeout_ms, num_threads,
                                       fm_count, std::move(fm_bottom));
        if (trace_goldfish) { TurnSolver::SetTraceSolve(false); }

        // Goldfish metric = avg (mean turn-to-win; an unwon game is scored max_turns+1). Win/loss is
        // not reported here: a goldfishing loss is an arbitrary "no lethal by max_turns" threshold,
        // and reporting it makes readers treat it as the priority metric. Lower avg is better.
        // Win/loss reporting belongs to a future 1v1 mode, where it is a real outcome. See ComputeAvgTurns.
        char avgline[32];
        std::snprintf(avgline, sizeof(avgline), "%.4f", result.avg_turns);
        std::cout << "Seed          : " << result.seed << "\n";
        std::cout << "Games played  : " << result.games_played << "\n";
        std::cout << "avg (turns)   : " << avgline
                  << "    [mean turn-to-win, unwon = max_turns+1; lower is better]\n";

        // Unwon games (no lethal by max_turns) listed as a REPRO aid -- game index + seed to replay
        // and inspect the slow line -- not as a win/loss metric (which goldfishing does not report).
        int unwon = result.games_played - result.games_won;
        if (unwon > 0)
        {
            std::cout << "Unwon games (repro: --seed <s> --game-index <i> --games 1):\n";
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
