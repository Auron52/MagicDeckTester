#include "GameLogger.h"
#include <fstream>
#include <nlohmann/json.hpp>
#include <stdexcept>

using json = nlohmann::json;

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
                               const std::string& mana_paid)
{
    if (!m_in_phase) { return; }
    Action a;
    a.type      = "CAST_SPELL";
    a.card_num  = card_num;
    a.card_name = card_name;
    a.mana_paid = mana_paid;
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
                              const std::vector<int>& battlefield,
                              const std::vector<int>& hand)
{
    if (!m_in_phase) { return; }
    m_current.player_life = player_life;
    m_current.opp_life    = opp_life;
    m_current.battlefield = battlefield;
    m_current.hand        = hand;
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
                if (a.type == "CAST_SPELL" && !a.mana_paid.empty())
                {
                    act["manaPaid"] = a.mana_paid;
                }
            }
            else if (a.type == "ATTACK")
            {
                act["damage"]  = a.damage;
                act["oppLife"] = a.opp_life;
            }
            actions_arr.push_back(std::move(act));
        }
        entry["actions"] = actions_arr;

        json board;
        board["playerLife"]  = pe.player_life;
        board["opponentLife"] = pe.opp_life;
        json bf = json::array();
        for (int n : pe.battlefield) { bf.push_back(n); }
        board["battlefield"] = bf;
        json hand = json::array();
        for (int n : pe.hand) { hand.push_back(n); }
        board["hand"] = hand;
        entry["boardAfter"] = board;

        turns_arr.push_back(std::move(entry));
    }
    root["turns"] = turns_arr;

    std::ofstream file(path);
    if (!file) { throw std::runtime_error("Cannot write game log: " + path.string()); }
    file << root.dump(2);
}
