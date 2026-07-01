#include "GameLogger.h"
#include <fstream>
#include <nlohmann/json.hpp>
#include <stdexcept>

using json = nlohmann::json;

thread_local GameLogger*   g_reveal_logger = nullptr;
thread_local TopChooser*    g_play_top_chooser = nullptr;
thread_local TargetChooser* g_play_target_chooser = nullptr;
thread_local BounceChooser*  g_play_bounce_chooser = nullptr;
thread_local DigChooser*     g_play_dig_chooser    = nullptr;
thread_local DiscardChooser* g_play_discard_chooser = nullptr;
thread_local EIChooser*      g_play_ei_chooser      = nullptr;
thread_local RetraceDiscardChooser* g_play_retrace_chooser = nullptr;
thread_local SoulfireTargetChooser* g_play_soulfire_chooser = nullptr;
thread_local std::vector<std::pair<int, std::string>>* g_play_draw_sink = nullptr;

void GameLogger::StartGame(const std::string& run_id, int game_number,
                            const std::string& deck_id, uint64_t seed,
                            const std::map<std::string, std::vector<int>>& card_numbering)
{
    m_run_id       = run_id;
    m_game_number  = game_number;
    m_deck_id      = deck_id;
    m_seed         = seed;
    m_numbering    = card_numbering;
    m_mulligan_sequence.clear();
    m_phases.clear();
    m_in_phase     = false;
    m_win_turn     = -1;
}

void GameLogger::StartPhase(int turn, const std::string& phase)
{
    m_current         = PhaseEntry{};
    m_current.turn    = turn;
    m_current.phase   = phase;
    m_in_phase        = true;
}

void GameLogger::LogMulliganAttempt(int attempt,
                                     const std::vector<int>& hand_nums,
                                     const std::vector<std::string>& hand_names,
                                     bool kept)
{
    MulliganAttempt ma;
    ma.attempt    = attempt;
    ma.hand_nums  = hand_nums;
    ma.hand_names = hand_names;
    ma.kept       = kept;
    m_mulligan_sequence.push_back(std::move(ma));
}

void GameLogger::LogBottomed(int card_num, const std::string& card_name)
{
    if (m_mulligan_sequence.empty()) { return; }
    m_mulligan_sequence.back().bottomed_nums.push_back(card_num);
    m_mulligan_sequence.back().bottomed_names.push_back(card_name);
}

void GameLogger::LogOpeningHand(const std::vector<int>& card_nums,
                                 const std::vector<std::string>& card_names)
{
    m_opening_hand_nums  = card_nums;
    m_opening_hand_names = card_names;
}

void GameLogger::LogPlayLand(int card_num, const std::string& card_name)
{
    if (!m_in_phase) { return; }
    Action a;
    a.type      = "PLAY_LAND";
    a.card_num  = card_num;
    a.card_name = card_name;
    m_current.actions.push_back(std::move(a));
}

void GameLogger::LogCastSpell(int card_num, const std::string& card_name,
                               const std::string& mana_paid, int chosen_x,
                               const std::vector<TargetDesc>& targets)
{
    if (!m_in_phase) { return; }
    Action a;
    a.type      = "CAST_SPELL";
    a.card_num  = card_num;
    a.card_name = card_name;
    a.mana_paid = mana_paid;
    a.chosen_x  = chosen_x;
    a.targets   = targets;
    m_current.actions.push_back(std::move(a));
}

void GameLogger::LogReveal(const std::string& source_name,
                            const std::vector<int>& looked_at_nums,
                            const std::vector<std::string>& looked_at_names,
                            const std::vector<int>& kept_nums,
                            const std::vector<int>& bottomed_nums,
                            const std::vector<std::string>& dispositions)
{
    if (!m_in_phase) { return; }
    Action a;
    a.type            = "REVEAL";
    a.card_name       = source_name;
    a.looked_at       = looked_at_nums;
    a.looked_at_names = looked_at_names;
    a.kept            = kept_nums;
    a.bottomed        = bottomed_nums;
    a.dispositions    = dispositions;
    m_current.actions.push_back(std::move(a));
}

void GameLogger::LogAbility(int source_card_num, const std::string& source_card_name,
                             const std::string& ability)
{
    if (!m_in_phase) { return; }
    Action a;
    a.type      = "ABILITY";
    a.card_num  = source_card_num;
    a.card_name = source_card_name;
    a.ability   = ability;
    m_current.actions.push_back(std::move(a));
}

void GameLogger::LogDraw(int card_num, const std::string& card_name)
{
    if (!m_in_phase) { return; }
    Action a;
    a.type      = "DRAW";
    a.card_num  = card_num;
    a.card_name = card_name;
    m_current.actions.push_back(std::move(a));
}

void GameLogger::LogDiscard(int card_num, const std::string& card_name)
{
    if (!m_in_phase) { return; }
    Action a;
    a.type      = "DISCARD";
    a.card_num  = card_num;
    a.card_name = card_name;
    m_current.actions.push_back(std::move(a));
}

void GameLogger::LogAttack(int damage, int opp_life_after)
{
    if (!m_in_phase) { return; }
    Action a;
    a.type     = "ATTACK";
    a.damage   = damage;
    a.opp_life = opp_life_after;
    m_current.actions.push_back(std::move(a));
}

void GameLogger::CommitPhase(int player_life, int opp_life,
                              const std::vector<PermSnapshot>& battlefield,
                              const std::vector<int>& hand,
                              const std::vector<PermSnapshot>& opp_battlefield,
                              const std::vector<int>& graveyard,
                              const std::vector<int>& staged)
{
    if (!m_in_phase) { return; }
    m_current.player_life     = player_life;
    m_current.opp_life        = opp_life;
    m_current.battlefield     = battlefield;
    m_current.opp_battlefield = opp_battlefield;
    m_current.hand            = hand;
    m_current.graveyard       = graveyard;
    m_current.staged          = staged;
    m_phases.push_back(std::move(m_current));
    m_in_phase = false;
}

void GameLogger::EndGame(int win_turn)
{
    m_win_turn = win_turn;
}

void GameLogger::WriteToFile(const std::filesystem::path& path) const
{
    json root;
    root["runId"]      = m_run_id;
    root["gameNumber"] = m_game_number;
    root["deckId"]     = m_deck_id;
    root["seed"]       = m_seed;

    if (m_win_turn > 0)
    {
        root["result"] = { {"winner", "player"}, {"turn", m_win_turn} };
    }
    else
    {
        root["result"] = { {"winner", "none"}, {"turn", nullptr} };
    }

    // Card numbering map — stored for reference (spec requirement)
    json numbering_obj = json::object();
    for (const std::pair<const std::string, std::vector<int>>& kv : m_numbering)
    {
        json nums = json::array();
        for (int n : kv.second) { nums.push_back(n); }
        numbering_obj[kv.first] = nums;
    }
    root["cardNumbering"] = numbering_obj;

    // Mulligan sequence: one entry per hand drawn; last entry is the kept hand.
    json mull_arr = json::array();
    for (const MulliganAttempt& ma : m_mulligan_sequence)
    {
        json entry;
        entry["attempt"] = ma.attempt;
        entry["kept"]    = ma.kept;
        json hand_arr = json::array();
        for (std::size_t i = 0; i < ma.hand_nums.size(); ++i)
        {
            json card_entry;
            card_entry["card"]     = ma.hand_nums[i];
            card_entry["cardName"] = ma.hand_names[i];
            hand_arr.push_back(std::move(card_entry));
        }
        entry["hand"] = hand_arr;
        if (!ma.bottomed_nums.empty())
        {
            json bot_arr = json::array();
            for (std::size_t i = 0; i < ma.bottomed_nums.size(); ++i)
            {
                json card_entry;
                card_entry["card"]     = ma.bottomed_nums[i];
                card_entry["cardName"] = ma.bottomed_names[i];
                bot_arr.push_back(std::move(card_entry));
            }
            entry["bottomed"] = bot_arr;
        }
        mull_arr.push_back(std::move(entry));
    }
    root["mulliganSequence"] = mull_arr;

    // Opening hand (kept after mulligan, before T1 draw step)
    json oh_arr = json::array();
    for (std::size_t i = 0; i < m_opening_hand_nums.size(); ++i)
    {
        json entry;
        entry["card"]     = m_opening_hand_nums[i];
        entry["cardName"] = m_opening_hand_names[i];
        oh_arr.push_back(std::move(entry));
    }
    root["openingHand"] = oh_arr;

    json turns_arr = json::array();
    for (const PhaseEntry& pe : m_phases)
    {
        json entry;
        entry["turn"]  = pe.turn;
        entry["phase"] = pe.phase;

        json actions_arr = json::array();
        for (const Action& a : pe.actions)
        {
            json act;
            act["type"] = a.type;
            if (a.type == "PLAY_LAND" || a.type == "CAST_SPELL"
                || a.type == "DRAW"    || a.type == "DISCARD")
            {
                act["card"]     = a.card_num;
                act["cardName"] = a.card_name;
                if (a.type == "CAST_SPELL")
                {
                    if (!a.mana_paid.empty()) { act["manaPaid"] = a.mana_paid; }
                    if (a.chosen_x >= 0)      { act["chosenX"]  = a.chosen_x; }
                    if (!a.targets.empty())
                    {
                        json tgts = json::array();
                        for (const TargetDesc& t : a.targets)
                        {
                            json tj;
                            tj["kind"] = t.kind;
                            tj["who"]  = t.who;
                            if (t.kind == "permanent")
                            {
                                tj["card"]     = t.card_num;
                                tj["cardName"] = t.card_name;
                            }
                            tgts.push_back(std::move(tj));
                        }
                        act["targets"] = tgts;
                    }
                }
            }
            else if (a.type == "ATTACK")
            {
                act["damage"]  = a.damage;
                act["oppLife"] = a.opp_life;
            }
            else if (a.type == "REVEAL")
            {
                act["source"] = a.card_name;
                json looked = json::array();
                for (std::size_t i = 0; i < a.looked_at.size(); ++i)
                {
                    json c;
                    c["card"] = a.looked_at[i];
                    if (i < a.looked_at_names.size()) { c["cardName"] = a.looked_at_names[i]; }
                    if (i < a.dispositions.size() && !a.dispositions[i].empty())
                    { c["to"] = a.dispositions[i]; }
                    looked.push_back(std::move(c));
                }
                act["lookedAt"] = looked;
                json kept = json::array();
                for (int n : a.kept)     { kept.push_back(n); }
                act["kept"] = kept;
                json bot = json::array();
                for (int n : a.bottomed) { bot.push_back(n); }
                act["bottomed"] = bot;
            }
            else if (a.type == "ABILITY")
            {
                act["card"]     = a.card_num;
                act["cardName"] = a.card_name;
                act["ability"]  = a.ability;
            }
            actions_arr.push_back(std::move(act));
        }
        entry["actions"] = actions_arr;

        json board;
        board["playerLife"]  = pe.player_life;
        board["opponentLife"] = pe.opp_life;
        // Battlefield as [{card, cardName, tapped}] so the viewer can name tokens and rotate
        // tapped permanents. opponentBattlefield is the opponent's side (tokens/spawns).
        auto serialize_bf = [](const std::vector<PermSnapshot>& perms)
        {
            json arr = json::array();
            for (const PermSnapshot& p : perms)
            {
                json j;
                j["card"]     = p.card_num;
                j["cardName"] = p.card_name;
                j["tapped"]   = p.tapped;
                j["isLand"]   = p.is_land;
                if (!p.counters.empty())
                {
                    json cs = json::array();
                    for (const CounterInfo& c : p.counters)
                    {
                        cs.push_back({ {"kind", c.kind}, {"count", c.count} });
                    }
                    j["counters"] = cs;
                }
                arr.push_back(std::move(j));
            }
            return arr;
        };
        board["battlefield"]         = serialize_bf(pe.battlefield);
        board["opponentBattlefield"] = serialize_bf(pe.opp_battlefield);
        json hand = json::array();
        for (int n : pe.hand) { hand.push_back(n); }
        board["hand"] = hand;
        json gy = json::array();
        for (int n : pe.graveyard) { gy.push_back(n); }
        board["graveyard"] = gy;
        json staged = json::array();
        for (int n : pe.staged) { staged.push_back(n); }
        board["staged"] = staged;
        entry["boardAfter"] = board;

        turns_arr.push_back(std::move(entry));
    }
    root["turns"] = turns_arr;

    std::ofstream file(path);
    if (!file) { throw std::runtime_error("Cannot write game log: " + path.string()); }
    file << root.dump(2);
}
