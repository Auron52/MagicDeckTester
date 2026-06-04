#pragma once
#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

// Records a single game's events in the structured format defined by the AI skill.
// One GameLogger instance per game; GoldFishRunner owns the lifecycle.
//
// Call sequence per game:
//   StartGame(...)
//   for each main phase / combat phase:
//     StartPhase(turn, "MAIN_1" | "COMBAT" | "MAIN_2")
//     Log*(...) for each action
//     CommitPhase(board state)
//   EndGame(win_turn)
//   WriteToFile(path)
class GameLogger
{
public:
    void StartGame(const std::string& run_id, int game_number,
                   const std::string& deck_id, uint64_t seed,
                   const std::map<std::string, std::vector<int>>& card_numbering);

    void StartPhase(int turn, const std::string& phase);

    void LogPlayLand(int card_num, const std::string& card_name);
    void LogCastSpell(int card_num, const std::string& card_name,
                      const std::string& mana_paid);
    void LogAttack(int damage, int opp_life_after);

    // Captures board state snapshot at the end of the current phase.
    void CommitPhase(int player_life, int opp_life,
                     const std::vector<int>& battlefield,
                     const std::vector<int>& hand);

    // win_turn: turn the opponent reached 0 life; -1 if the game was not won.
    void EndGame(int win_turn);

    void WriteToFile(const std::filesystem::path& path) const;

private:
    struct Action
    {
        std::string type;
        int         card_num  = 0;
        std::string card_name;
        std::string mana_paid;
        int         damage    = 0;
        int         opp_life  = 0;
    };

    struct PhaseEntry
    {
        int                 turn = 0;
        std::string         phase;
        std::vector<Action> actions;
        int                 player_life = 0;
        int                 opp_life    = 0;
        std::vector<int>    battlefield;
        std::vector<int>    hand;
    };

    std::string                             m_run_id;
    int                                     m_game_number = 0;
    std::string                             m_deck_id;
    uint64_t                                m_seed        = 0;
    std::map<std::string, std::vector<int>> m_numbering;
    std::vector<PhaseEntry>                 m_phases;
    PhaseEntry                              m_current;
    bool                                    m_in_phase    = false;
    int                                     m_win_turn    = -1;
};
