// Greedy-scoring defects the canonical tie-break exposed (Maze's End P1b follow-up, 2026-11-18). Once the
// `1 << j` alias was gone, depth-0 play moved on Extra Turns / Jumbo Cactuar / Melira Pod, and every move traced
// to a candidate `eval` that scored two different outcomes the same. USER: "Those sound reasonable to fix. For
// Reveillark evoking can be worse or better depending on the context." Each case below pins the eval to the
// state the variant really produces:
//   1. a tutor variant that finds nothing (Tooth and Nail's empty pick, Finale's whiff) scores below a find and
//      strictly below the pass;
//   2. Time Warp aimed at the opponent gives US nothing -- strictly below the pass, below the self cast;
//   3. Reveillark's evoke is scored by the creatures its leave trigger returns NOW, the hard cast by the body,
//      so which mode scores higher depends on the graveyard -- and the search still sees both modes.
#include <doctest/doctest.h>

#include "ai/DecisionProviders.h"
#include "ai/TurnSolver.h"
#include "cards/CardDatabase.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"

#include <algorithm>
#include <utility>
#include <string>
#include <vector>

namespace
{
void EnsureCardsTs()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

Card CardTs(const std::string& name, int number)
{
    REQUIRE_MESSAGE(CardDatabase::Instance().Lookup(name) != nullptr, "card not in cards.json: ", name);
    Card c;
    c.m_name = name;
    c.RehashName();
    c.m_number = number;
    return c;
}

void PermTs(GameState& s, const std::string& name, int number)
{
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE(d != nullptr);
    Permanent p;
    p.card = d->card;
    p.card.m_number = number;
    p.controller_index = 0;
    p.owner_index = 0;
    s.battlefield.push_back(p);
}

GameState BaseTs(const DecisionProvider& prov, int turn)
{
    GameState s;
    s.active_player_index = 0;
    s.turn_number = turn;
    s.m_provider = &prov;
    s.players[0].lands_played_this_turn = 1;
    return s;
}

std::vector<Action> CastsOf(const GameState& s, const std::string& name)
{
    std::vector<Action> out;
    for (const Action& a : TurnSolver::CollectActionsForTest(s, /*is_pre_combat=*/true))
    {
        if (a.kind == Action::Kind::CastFromHand && a.card_name.str() == name) { out.push_back(a); }
    }
    return out;
}

const JumboCactuarProvider& JumboTs() { static const JumboCactuarProvider p; return p; }
const ExtraTurnsProvider& TurnsTs() { static const ExtraTurnsProvider p; return p; }
const MeliraPodProvider& PodTs() { static const MeliraPodProvider p; return p; }

// EncodeToothChoices: lib0 \x1f lib1 \x1f put0 \x1f put1 -> {library picks, hand puts}.
std::pair<int, int> ToothCounts(const std::string& pin)
{
    std::vector<std::string> f(1);
    for (char c : pin) { if (c == '\x1f') { f.emplace_back(); } else { f.back().push_back(c); } }
    f.resize(4);
    return { static_cast<int>(!f[0].empty()) + static_cast<int>(!f[1].empty()),
             static_cast<int>(!f[2].empty()) + static_cast<int>(!f[3].empty()) };
}

constexpr int kPassValue = -1;   // TurnSolver::Plan::value of "nothing castable"
}

TEST_CASE("Tooth and Nail: finding nothing scores below every find and strictly below the pass")
{
    EnsureCardsTs();
    GameState s = BaseTs(JumboTs(), 3);
    int n = 1;
    for (int k = 0; k < 7; ++k) { PermTs(s, "Forest", n++); }
    s.players[0].hand.push_back(CardTs("Tooth and Nail", n++));
    s.players[0].library.push_back(CardTs("Jumbo Cactuar", n++));
    s.players[0].library.push_back(CardTs("Voyaging Satyr", n++));

    int empty_eval = 1 << 30, one_find = -(1 << 30), two_find = -(1 << 30);
    bool saw_empty = false, saw_one = false, saw_two = false;
    for (const Action& a : CastsOf(s, "Tooth and Nail"))
    {
        if (a.chosen_x != 0) { continue; }   // mode 0: search for up to two creatures
        const int picks = ToothCounts(a.tutor_target.str()).first;
        if (picks == 0)      { saw_empty = true; empty_eval = a.eval; }
        else if (picks == 2) { saw_two = true; two_find = std::max(two_find, a.eval); }
        else                 { saw_one = true; one_find = std::max(one_find, a.eval); }
    }
    REQUIRE(saw_empty);
    REQUIRE(saw_one);
    REQUIRE(saw_two);
    CHECK(empty_eval < kPassValue);
    CHECK(empty_eval < one_find);
    CHECK(one_find < two_find);
}

TEST_CASE("Tooth and Nail: putting creatures onto the battlefield scores above leaving the hand unchanged")
{
    EnsureCardsTs();
    GameState s = BaseTs(JumboTs(), 3);
    int n = 1;
    for (int k = 0; k < 7; ++k) { PermTs(s, "Forest", n++); }
    s.players[0].hand.push_back(CardTs("Tooth and Nail", n++));
    s.players[0].hand.push_back(CardTs("Surrak and Goreclaw", n++));
    int empty_put = 1 << 30, real_put = -(1 << 30);
    for (const Action& a : CastsOf(s, "Tooth and Nail"))
    {
        if (a.chosen_x != 1) { continue; }   // mode 1: put up to two creature cards from hand
        if (ToothCounts(a.tutor_target.str()).second == 0) { empty_put = a.eval; }
        else { real_put = std::max(real_put, a.eval); }
    }
    CHECK(empty_put < kPassValue);
    CHECK(real_put > 0);
}

TEST_CASE("Finale of Devastation: the whiff scores below the find; at X >= 10 the whiff keeps the pump's value")
{
    EnsureCardsTs();
    GameState s = BaseTs(JumboTs(), 3);
    int n = 1;
    for (int k = 0; k < 4; ++k) { PermTs(s, "Forest", n++); }
    s.players[0].hand.push_back(CardTs("Finale of Devastation", n++));
    s.players[0].library.push_back(CardTs("Arbor Elf", n++));
    int whiff_x1 = 1 << 30, find_x1 = -(1 << 30);
    for (const Action& a : CastsOf(s, "Finale of Devastation"))
    {
        if (a.chosen_x != 1) { continue; }
        const std::string pin = a.tutor_target.str();
        if (pin == "N" || pin == "Z") { whiff_x1 = std::min(whiff_x1, a.eval); }
        else if (pin == "L0:Arbor Elf") { find_x1 = a.eval; }
    }
    CHECK(whiff_x1 < kPassValue);
    CHECK(find_x1 > whiff_x1);
    CHECK(find_x1 > 0);

    // X = 10: "creatures you control get +X/+X and gain haste" happens with or without a find, so the
    // whiff is a real spell there (the pump), not a card spent for nothing.
    GameState big = BaseTs(JumboTs(), 3);
    for (int k = 0; k < 12; ++k) { PermTs(big, "Forest", n++); }
    big.players[0].hand.push_back(CardTs("Finale of Devastation", n++));
    big.players[0].library.push_back(CardTs("Jumbo Cactuar", n++));
    bool saw_x10_whiff = false;
    for (const Action& a : CastsOf(big, "Finale of Devastation"))
    {
        if (a.chosen_x != 10 || a.tutor_target.str() != "N") { continue; }
        saw_x10_whiff = true;
        CHECK(a.eval > 0);
    }
    CHECK(saw_x10_whiff);
}

TEST_CASE("Time Warp aimed at the opponent gives us nothing: below the self cast and strictly below the pass")
{
    EnsureCardsTs();
    GameState s = BaseTs(TurnsTs(), 3);
    s.deck_has_extra_turn = true;
    int n = 1;
    for (int k = 0; k < 5; ++k) { PermTs(s, "Snow-Covered Island", n++); }
    s.players[0].hand.push_back(CardTs("Time Warp", n++));
    bool saw_self = false, saw_opp = false;
    int self_eval = 0, opp_eval = 0;
    for (const Action& a : CastsOf(s, "Time Warp"))
    {
        if (a.player_target == 0) { saw_self = true; self_eval = a.eval; }
        else { saw_opp = true; opp_eval = a.eval; }
    }
    // Both recipients stay ENUMERATED (a passive opponent's extra turn can deck them; the search owns it).
    REQUIRE(saw_self);
    REQUIRE(saw_opp);
    CHECK(self_eval > 0);
    CHECK(opp_eval < kPassValue);

    TurnSolver::Plan p = TurnSolver::Solve(s, true, TurnSolver::GreedyPermit(TurnSolver::GreedySite::HorizonLeaf, 0));
    REQUIRE(p.actions.size() == 1);
    CHECK(p.actions[0].card_name.str() == "Time Warp");
    CHECK(p.actions[0].player_target == 0);
}

TEST_CASE("Reveillark: evoke is scored by what the leave trigger returns now, so either mode can score higher")
{
    EnsureCardsTs();
    auto lark_evals = [](const std::vector<std::string>& graveyard, int& hard, int& evoke)
    {
        GameState s = BaseTs(PodTs(), 3);
        int n = 1;
        for (int k = 0; k < 6; ++k) { PermTs(s, "Plains", n++); }
        s.players[0].hand.push_back(CardTs("Reveillark", n++));
        for (const std::string& g : graveyard) { s.players[0].graveyard.push_back(CardTs(g, n++)); }
        bool saw_hard = false, saw_evoke = false;
        for (const Action& a : CastsOf(s, "Reveillark"))
        {
            if (a.evoke) { saw_evoke = true; evoke = a.eval; }
            else         { saw_hard = true; hard = a.eval; }
        }
        REQUIRE(saw_hard);
        REQUIRE(saw_evoke);
    };
    int hard_weak = 0, evoke_weak = 0, hard_strong = 0, evoke_strong = 0, hard_two = 0, evoke_two = 0;
    // One non-hasty 2/2 back: the 4/3 flier is worth more.
    lark_evals({ "Melira, Sylvok Outcast" }, hard_weak, evoke_weak);
    // Two returns are worth more than one.
    lark_evals({ "Melira, Sylvok Outcast", "Murderous Redcap" }, hard_two, evoke_two);
    // Two hasty 2-power bodies attack this turn too: the evoke is worth more than the body.
    lark_evals({ "Goblin Guide", "Goblin Chieftain" }, hard_strong, evoke_strong);
    CHECK(hard_weak == hard_two);
    CHECK(hard_weak == hard_strong);    // the hard cast's body does not depend on the graveyard
    CHECK(evoke_weak < hard_weak);
    CHECK(evoke_two > evoke_weak);
    CHECK(evoke_strong > hard_strong);
}

TEST_CASE("Reveillark: the searched enumeration keeps both the evoke and the hard cast as distinct plans")
{
    EnsureCardsTs();
    GameState s = BaseTs(PodTs(), 3);
    int n = 1;
    for (int k = 0; k < 6; ++k) { PermTs(s, "Plains", n++); }
    s.players[0].hand.push_back(CardTs("Reveillark", n++));
    s.players[0].graveyard.push_back(CardTs("Melira, Sylvok Outcast", n++));
    TurnSolver::ClearPerGameCaches();
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, /*is_pre_combat=*/true);
    bool hard = false, evoke = false;
    for (const TurnSolver::Plan& p : plans)
    {
        for (const Action& a : p.actions)
        {
            if (a.kind != Action::Kind::CastFromHand || a.card_name.str() != "Reveillark") { continue; }
            (a.evoke ? evoke : hard) = true;
        }
    }
    CHECK(hard);
    CHECK(evoke);
}
