// Unit tests for the Goblin Lackey put axis's cast route and ordinals (TurnSolver.cpp AppendSubdecisionAxes,
// "SEARCHED GOBLIN LACKEY PUT"; SpellEffects.h FireCombatDamageCheatIntoPlay; Combat.cpp ResolveCombatDamage) --
// the 2026-10-29 fix of the same battlefield-only gate the Tectonic / Flinger axes had:
//
//   * MTG_LACKEY_CAST_AXIS: a Lackey the PLAN casts while a haste source is reachable (Goblin Warchief) is fanned,
//     and the put width is the hand left at combat damage (minus the plan's casts, plus its tutors-to-hand);
//   * MTG_LACKEY_ORDINALS: the pin is packed per damaging-Lackey trigger, so a second Lackey's put is searched;
//   * MTG_LACKEY_PIN_FOLD: the pin is spent at the end of combat damage whether or not a put fired.
//
// Zone cards are NAME-ONLY placeholders, as in real play (DeckLoader::MakePlaceholder).
#include <doctest/doctest.h>

#include "ai/Combat.h"
#include "ai/HeuristicArm.h"
#include "ai/TurnSolver.h"
#include "cards/CardDatabase.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"

#include <set>
#include <string>
#include <utility>
#include <vector>

namespace
{

void EnsureCardsLk()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

int g_lk_num = 9500;

Card PhLk(const std::string& name)
{
    REQUIRE_MESSAGE(CardDatabase::Instance().Lookup(name) != nullptr, "card not in cards.json: ", name);
    Card c;
    c.m_name = name;
    c.RehashName();
    c.m_number = g_lk_num++;
    return c;
}

int PutLk(GameState& s, const std::string& name, bool sick = false)
{
    Permanent p;
    p.card              = CardDatabase::Instance().Lookup(name)->card;
    p.card.m_number     = g_lk_num++;
    p.controller_index  = 0;
    p.owner_index       = 0;
    p.entered_this_turn = sick;
    s.battlefield.push_back(p);
    return p.card.m_number;
}

// T4, `mountains` Mountains, no land drop left. Library top first.
GameState FreshLk(int mountains, const std::vector<std::string>& hand, const std::vector<std::string>& board,
                  const std::vector<std::string>& library = { "Mountain", "Mountain", "Mountain" })
{
    EnsureCardsLk();
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 4;
    s.players[0].life     = 20;
    s.players[1].life     = 20;
    s.players[0].lands_played_this_turn = 1;
    for (int k = 0; k < mountains; ++k) { PutLk(s, "Mountain"); }
    for (const std::string& n : board)   { PutLk(s, n); }
    for (const std::string& n : hand)    { s.players[0].hand.push_back(PhLk(n)); }
    for (const std::string& n : library) { s.players[0].library.push_back(PhLk(n)); }
    return s;
}

struct ArmLk
{
    std::vector<std::pair<int, int>> saved;
    void Set(int slot, int v) { saved.emplace_back(slot, heurarm::t_arm[slot]); heurarm::t_arm[slot] = static_cast<std::int8_t>(v); }
    ~ArmLk() { for (auto it = saved.rbegin(); it != saved.rend(); ++it) { heurarm::t_arm[it->first] = static_cast<std::int8_t>(it->second); } }
};

bool CastsLk(const TurnSolver::Plan& p, const std::string& name)
{
    for (const Action& a : p.actions)
    { if (a.kind == Action::Kind::CastFromHand && a.card_name.str() == name) { return true; } }
    return false;
}

// The Lackey pins carried by plans that cast `name` (every plan when empty).
std::set<int> LackeyPins(const std::vector<TurnSolver::Plan>& plans, const std::string& name)
{
    std::set<int> out;
    for (const TurnSolver::Plan& p : plans)
    {
        if (!name.empty() && !CastsLk(p, name)) { continue; }
        if (p.lackey_choice >= 0) { out.insert(p.lackey_choice); }
    }
    return out;
}

const char* kLk = "Goblin Lackey";

}   // namespace

TEST_CASE("Lackey pins: the per-ordinal packing keeps every historical single-trigger rank, and shifts")
{
    for (int r = 0; r <= kLackeyMaxRank; ++r)
    {
        const int one[1] = { r };
        CHECK(LackeyPackRanks(one, 1) == r);        // the historical value
        CHECK(LackeyRankAt(r, 0) == r);
        CHECK(LackeyRankAt(r, 1) == -1);
        CHECK(LackeyPinShift(r) == -1);             // the first trigger spends it
    }
    CHECK(LackeyRankAt(-1, 0) == -1);
    for (int a = -1; a <= 2; ++a)
    for (int b = -1; b <= 2; ++b)
    for (int c = -1; c <= 2; ++c)
    {
        const int r[3] = { a, b, c };
        const int v = LackeyPackRanks(r, 3);
        CHECK((v == -1) == (a < 0 && b < 0 && c < 0));
        CHECK(LackeyRankAt(v, 0) == a);
        CHECK(LackeyRankAt(v, 1) == b);
        CHECK(LackeyRankAt(v, 2) == c);
        const int s = LackeyPinShift(v);
        CHECK(LackeyRankAt(s, 0) == b);
        CHECK(LackeyRankAt(s, 1) == c);
        CHECK(LackeyRankAt(s, 2) == -1);
    }
}

TEST_CASE("Lackey cast axis: a Lackey the plan casts beside Goblin Warchief gets the put ranks")
{
    ArmLk arm;
    // Warchief in play (haste to Goblins); hand: Lackey + two Goblin permanents to put.
    GameState s = FreshLk(2, { kLk, "Goblin King", "Goblin Piledriver" }, { "Goblin Warchief" });
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, /*is_pre_combat=*/true);
    bool casts = false;
    for (const TurnSolver::Plan& p : plans) { if (CastsLk(p, kLk)) { casts = true; } }
    REQUIRE(casts);
    CHECK(LackeyPins(plans, kLk) == std::set<int>{ 1 });   // width 2: the front is the base plan, rank 1 the variant
    // A plan that does not cast the Lackey cannot read the pin.
    for (const TurnSolver::Plan& p : plans) { if (!CastsLk(p, kLk)) { CHECK(p.lackey_choice == -1); } }
    // =0 hatches: the battlefield-only gate.
    arm.Set(heurarm::LACKEY_CAST_AXIS, 0);
    CHECK(LackeyPins(TurnSolver::EnumerateMainPlans(s, /*is_pre_combat=*/true), "").empty());
}

TEST_CASE("Lackey cast axis: no haste source, no variant")
{
    GameState s = FreshLk(2, { kLk, "Goblin King", "Goblin Piledriver" }, {});
    CHECK(LackeyPins(TurnSolver::EnumerateMainPlans(s, /*is_pre_combat=*/true), "").empty());
}

TEST_CASE("Lackey cast axis: the put width is the hand LEFT at combat damage")
{
    // The plan that casts the Lackey leaves one Goblin permanent in hand: a forced put, no variant. (The hand as
    // it stands counts the Lackey itself.)
    GameState s = FreshLk(1, { kLk, "Goblin King", "Lightning Bolt" }, { "Goblin Warchief" });
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, /*is_pre_combat=*/true);
    bool casts = false;
    for (const TurnSolver::Plan& p : plans) { if (CastsLk(p, kLk)) { casts = true; } }
    REQUIRE(casts);
    CHECK(LackeyPins(plans, kLk).empty());
}

TEST_CASE("Lackey ordinals: two Lackeys able to attack get the PRODUCT of ranks, minus the base")
{
    ArmLk arm;
    GameState s = FreshLk(1, { "Goblin King", "Goblin Piledriver", "Lightning Bolt" }, { kLk, kLk });
    const std::set<int> pins = LackeyPins(TurnSolver::EnumerateMainPlans(s, /*is_pre_combat=*/true), "");
    std::set<std::pair<int, int>> tuples;
    for (int v : pins) { tuples.emplace(LackeyRankAt(v, 0), LackeyRankAt(v, 1)); }
    CHECK(tuples == std::set<std::pair<int, int>>{ { 1, 0 }, { 0, 1 }, { 1, 1 } });
    // =0 hatch: one rank, read by every trigger.
    arm.Set(heurarm::LACKEY_ORDINALS, 0);
    arm.Set(heurarm::LACKEY_CAST_AXIS, 0);
    CHECK(LackeyPins(TurnSolver::EnumerateMainPlans(s, /*is_pre_combat=*/true), "") == std::set<int>{ 1 });
}

TEST_CASE("Lackey triggers: each damaging Lackey reads its own ordinal; the pin is spent after combat damage")
{
    ArmLk arm;
    GameState s = FreshLk(0, { "Goblin King", "Goblin Piledriver", "Siege-Gang Commander" }, { kLk, kLk });
    std::vector<int> lk;
    for (int i = 0; i < static_cast<int>(s.battlefield.size()); ++i)
    { if (s.battlefield[i].card.m_name.str() == kLk) { lk.push_back(i); } }
    REQUIRE(lk.size() == 2);
    // The front of each trigger's ranked list, and the first trigger's rank 1, without any pin.
    GameState base = s;
    FireCombatDamageCheatIntoPlay(base, 0, lk);
    REQUIRE(base.players[0].hand.size() == 1);
    // Pin (1, 1): each Lackey puts the SECOND card of its own ranked list (the front leaves a different card).
    const int r[2] = { 1, 1 };
    s.scripted_cheat_choice = LackeyPackRanks(r, 2);
    GameState pinned = s;
    FireCombatDamageCheatIntoPlay(pinned, 0, lk);
    REQUIRE(pinned.players[0].hand.size() == 1);
    CHECK(pinned.scripted_cheat_choice == -1);   // both ordinals read
    CHECK(pinned.players[0].hand[0].m_name != base.players[0].hand[0].m_name);
    // ResolveCombatDamage spends an unread pin (nothing attacked).
    GameState t = FreshLk(0, {}, {});
    t.scripted_cheat_choice = 1;
    ResolveCombatDamage(t, std::vector<int>{}, 0, false);
    CHECK(t.scripted_cheat_choice == -1);
    arm.Set(heurarm::LACKEY_PIN_FOLD, 0);
    GameState u = FreshLk(0, {}, {});
    u.scripted_cheat_choice = 1;
    ResolveCombatDamage(u, std::vector<int>{}, 0, false);
    CHECK(u.scripted_cheat_choice == 1);
}
