// Unit tests for the Tectonic Giant mode / keep axis (TurnSolver.cpp AppendSubdecisionAxes, "SEARCHED TECTONIC
// GIANT MODE"; SpellEffects.h ApplyAttackModalTriggers) -- the 2026-10-29 fix of the in-window greedy:
//
//   * MTG_TECTONIC_CAST_AXIS: the fan covers a Giant the PLAN casts and can attack with (Lightning Greaves'
//     haste), not only one already on the battlefield;
//   * MTG_TECTONIC_ORDINALS: the pins are packed per trigger ordinal, so a SECOND attacking copy's mode and
//     keep are searched (the product over ordinals) and the projection reads the same ordinals;
//   * MTG_TECTONIC_PIN_FOLD: the pins are combat pins -- spent at declare-attackers whether or not a trigger
//     fired, and never fanned on a post-combat list.
// ...and the same gate shape on Surtland Flinger (MTG_FLING_CAST_AXIS, MTG_FLING_PIN_FOLD): a Flinger the plan casts
// with haste reachable is fanned (DECLINE + every victim, the plan's own creature casts among them).
//
// Zone cards are NAME-ONLY placeholders, as in real play (DeckLoader::MakePlaceholder).
#include <doctest/doctest.h>

#include "ai/HeuristicArm.h"
#include "ai/TurnSolver.h"
#include "cards/CardDatabase.h"
#include "core/GameSetup.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"

#include <set>
#include <string>
#include <utility>
#include <vector>

namespace
{

void EnsureCardsTec()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

int g_tec_num = 9000;

Card PhTec(const std::string& name)
{
    REQUIRE_MESSAGE(CardDatabase::Instance().Lookup(name) != nullptr, "card not in cards.json: ", name);
    Card c;
    c.m_name = name;
    c.RehashName();
    c.m_number = g_tec_num++;
    return c;
}

int PutTec(GameState& s, const std::string& name, bool sick = false)
{
    Permanent p;
    p.card              = CardDatabase::Instance().Lookup(name)->card;
    p.card.m_number     = g_tec_num++;
    p.controller_index  = 0;
    p.owner_index       = 0;
    p.entered_this_turn = sick;
    s.battlefield.push_back(p);
    return p.card.m_number;
}

// T5, four Mountains, no land drop left (keeps the plan space small). Library top first.
GameState FreshTec(const std::vector<std::string>& hand, const std::vector<std::string>& board,
                   const std::vector<std::string>& library)
{
    EnsureCardsTec();
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 5;
    s.players[0].life     = 20;
    s.players[1].life     = 20;
    s.players[0].lands_played_this_turn = 1;
    for (int k = 0; k < 4; ++k) { PutTec(s, "Mountain"); }
    for (const std::string& n : board)   { PutTec(s, n); }
    for (const std::string& n : hand)    { s.players[0].hand.push_back(PhTec(n)); }
    for (const std::string& n : library) { s.players[0].library.push_back(PhTec(n)); }
    return s;
}

struct ArmTec
{
    std::vector<std::pair<int, int>> saved;
    void Set(int slot, int v) { saved.emplace_back(slot, heurarm::t_arm[slot]); heurarm::t_arm[slot] = static_cast<std::int8_t>(v); }
    ~ArmTec() { for (auto it = saved.rbegin(); it != saved.rend(); ++it) { heurarm::t_arm[it->first] = static_cast<std::int8_t>(it->second); } }
};

bool CastsName(const TurnSolver::Plan& p, const std::string& name)
{
    for (const Action& a : p.actions)
    { if (a.kind == Action::Kind::CastFromHand && a.card_name.str() == name) { return true; } }
    return false;
}
int CountCasts(const TurnSolver::Plan& p, const std::string& name)
{
    int n = 0;
    for (const Action& a : p.actions)
    { if (a.kind == Action::Kind::CastFromHand && a.card_name.str() == name) { ++n; } }
    return n;
}

// The (mode, keep) pin pairs carried by the plans that cast `name` `casts` times.
std::set<std::pair<int, int>> PinsOfCasting(const std::vector<TurnSolver::Plan>& plans, const std::string& name,
                                            int casts = 1)
{
    std::set<std::pair<int, int>> out;
    for (const TurnSolver::Plan& p : plans)
    {
        if (CountCasts(p, name) != casts) { continue; }
        if (p.tectonic_mode_choice < 0 && p.tectonic_keep_choice < 0) { continue; }
        out.emplace(p.tectonic_mode_choice, p.tectonic_keep_choice);
    }
    return out;
}
bool AnyPinned(const std::vector<TurnSolver::Plan>& plans)
{
    for (const TurnSolver::Plan& p : plans)
    { if (p.tectonic_mode_choice >= 0 || p.tectonic_keep_choice >= 0) { return true; } }
    return false;
}

const char* kTecG = "Tectonic Giant";

std::string Show(const std::set<std::pair<int, int>>& v)
{
    std::string out;
    for (const auto& [m, k] : v) { out += "(" + std::to_string(m) + "," + std::to_string(k) + ")"; }
    return out;
}

}   // namespace

TEST_CASE("Tectonic pins: the per-ordinal packing keeps every historical single-trigger value")
{
    // Ordinal 0 alone packs to exactly what the single-trigger axis always wrote.
    { const int m[1] = { 0 };  CHECK(TectonicPackModes(m, 1) == 0); }
    { const int m[1] = { 1 };  CHECK(TectonicPackModes(m, 1) == 1); }
    { const int m[1] = { -1 }; CHECK(TectonicPackModes(m, 1) == -1); }
    { const int k[1] = { 0 };  CHECK(TectonicPackKeeps(k, 1) == 0); }
    { const int k[1] = { 1 };  CHECK(TectonicPackKeeps(k, 1) == 1); }
    { const int k[1] = { -1 }; CHECK(TectonicPackKeeps(k, 1) == -1); }
    // ...and a historical value decodes as ordinal 0 only, every later copy unpinned.
    for (int v : { 0, 1 })
    {
        CHECK(TectonicModePinAt(v, 0) == v);
        CHECK(TectonicModePinAt(v, 1) == -1);
        CHECK(TectonicKeepPinAt(v, 0) == v);
        CHECK(TectonicKeepPinAt(v, 1) == -1);
    }
    CHECK(TectonicModePinAt(-1, 0) == -1);
    CHECK(TectonicKeepPinAt(-1, 0) == -1);
    // Round trip over two ordinals, every combination of {unpinned, A, B} x keep {unpinned, 0, 1}.
    for (int m0 = -1; m0 <= 1; ++m0)
    for (int m1 = -1; m1 <= 1; ++m1)
    for (int k0 = -1; k0 <= 1; ++k0)
    for (int k1 = -1; k1 <= 1; ++k1)
    {
        const int m[2] = { m0, m1 }, k[2] = { k0, k1 };
        const int pm = TectonicPackModes(m, 2), pk = TectonicPackKeeps(k, 2);
        CHECK(TectonicModePinAt(pm, 0) == m0);
        CHECK(TectonicModePinAt(pm, 1) == m1);
        CHECK(TectonicKeepPinAt(pk, 0) == k0);
        CHECK(TectonicKeepPinAt(pk, 1) == k1);
        CHECK((pm == -1) == (m0 < 0 && m1 < 0));
        CHECK((pk == -1) == (k0 < 0 && k1 < 0));
    }
}

TEST_CASE("Tectonic cast axis: a Giant the plan casts beside Lightning Greaves gets every mode and keep")
{
    ArmTec arm;
    // Greaves already in play (equip {0}), Tectonic Giant in hand: the plan that casts it can attack with it.
    GameState s = FreshTec({ kTecG }, { "Lightning Greaves" },
                           { "Stinkdrinker Daredevil", "Mountain", "Lightning Bolt", "Mountain" });
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, /*is_pre_combat=*/true);
    bool casts = false;
    for (const TurnSolver::Plan& p : plans) { if (CastsName(p, kTecG)) { casts = true; } }
    REQUIRE(casts);
    // {A}, {B, keep 0}, {B, keep 1} -- the battlefield shape, now for the cast Giant.
    const std::set<std::pair<int, int>> want = { { 0, -1 }, { 1, 0 }, { 1, 1 } };
    CHECK(PinsOfCasting(plans, kTecG) == want);
    // A plan that does NOT cast the Giant cannot read the pin: no variant.
    for (const TurnSolver::Plan& p : plans)
    {
        if (CastsName(p, kTecG)) { continue; }
        CHECK(p.tectonic_mode_choice == -1);
        CHECK(p.tectonic_keep_choice == -1);
    }

    // =0 hatch: the battlefield-only gate, so the cast Giant takes the default.
    arm.Set(heurarm::TECTONIC_CAST_AXIS, 0);
    CHECK_FALSE(AnyPinned(TurnSolver::EnumerateMainPlans(s, /*is_pre_combat=*/true)));
}

TEST_CASE("Tectonic cast axis: Greaves cast in the same plan counts; no haste source anywhere, no variant")
{
    // Greaves in HAND: the plan that casts both can attack -- the fan covers it.
    {
        GameState s = FreshTec({ kTecG, "Lightning Greaves" }, { "Sol Ring" },
                               { "Mountain", "Mountain", "Lightning Bolt" });
        const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, /*is_pre_combat=*/true);
        bool both = false;
        for (const TurnSolver::Plan& p : plans)
        {
            if (!CastsName(p, kTecG) || !CastsName(p, "Lightning Greaves")) { continue; }
            both = true;
        }
        REQUIRE(both);
        bool pinned_both = false;
        for (const TurnSolver::Plan& p : plans)
        {
            if (CastsName(p, kTecG) && CastsName(p, "Lightning Greaves") && p.tectonic_mode_choice >= 0)
            { pinned_both = true; }
        }
        CHECK(pinned_both);
    }
    // No haste source on the battlefield, in hand, or among the casts: the cast Giant is summoning sick, its
    // trigger cannot fire this turn, so no variant is emitted.
    {
        GameState s = FreshTec({ kTecG, "Lightning Bolt" }, {}, { "Mountain", "Mountain", "Lightning Bolt" });
        CHECK_FALSE(AnyPinned(TurnSolver::EnumerateMainPlans(s, /*is_pre_combat=*/true)));
    }
}

TEST_CASE("Tectonic ordinals: a Giant on the battlefield and a second one cast get the PRODUCT of choices")
{
    ArmTec arm;
    // A Lightning Bolt in hand too, so some plans cast no Giant (four Mountains pay for one or the other).
    GameState s = FreshTec({ kTecG, "Lightning Bolt" }, { "Lightning Greaves", kTecG },
                           { "Stinkdrinker Daredevil", "Mountain", "Lightning Bolt", "Mountain", "Mountain" });
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, /*is_pre_combat=*/true);
    // The plan casting the second copy: 3 x 3 = 9 combinations over two ordinals.
    const std::set<std::pair<int, int>> two = PinsOfCasting(plans, kTecG, 1);
    CHECK(two.size() == 9);
    bool both_damage = false;
    for (const auto& [m, k] : two)
    {
        CHECK(TectonicModePinAt(m, 0) >= 0);
        CHECK(TectonicModePinAt(m, 1) >= 0);
        if (TectonicModePinAt(m, 0) == 0 && TectonicModePinAt(m, 1) == 0) { both_damage = true; }
    }
    CHECK(both_damage);   // gi103 T5's line: both triggers to the face
    // A plan that casts nothing still fans the battlefield copy alone: the historical three.
    const std::set<std::pair<int, int>> one = PinsOfCasting(plans, kTecG, 0);
    INFO("non-casting plans' pins: ", Show(one));
    CHECK(one == std::set<std::pair<int, int>>{ { 0, -1 }, { 1, 0 }, { 1, 1 } });

    // =0 hatch: one ordinal, so the cast copy's plans carry only the first trigger's three.
    arm.Set(heurarm::TECTONIC_ORDINALS, 0);
    const std::vector<TurnSolver::Plan> old = TurnSolver::EnumerateMainPlans(s, /*is_pre_combat=*/true);
    CHECK(PinsOfCasting(old, kTecG, 1) == std::set<std::pair<int, int>>{ { 0, -1 }, { 1, 0 }, { 1, 1 } });
}

TEST_CASE("Tectonic pin fold: a post-combat list fans no Tectonic variant")
{
    ArmTec arm;
    GameState s = FreshTec({ "Lightning Bolt" }, { kTecG }, { "Mountain", "Mountain", "Mountain" });
    CHECK(AnyPinned(TurnSolver::EnumerateMainPlans(s, /*is_pre_combat=*/true)));
    CHECK_FALSE(AnyPinned(TurnSolver::EnumerateMainPlans(s, /*is_pre_combat=*/false)));
    // =0 hatch: the historical (inert) post-combat variants.
    arm.Set(heurarm::TECTONIC_PIN_FOLD, 0);
    CHECK(AnyPinned(TurnSolver::EnumerateMainPlans(s, /*is_pre_combat=*/false)));
}

TEST_CASE("Tectonic triggers: each attacking copy resolves its own ordinal, and the pins are spent")
{
    ArmTec arm;
    // Two Giants attacking; library top: Stinkdrinker Daredevil, Mountain, Lightning Bolt, Mountain.
    auto fresh = [&]()
    {
        GameState s = FreshTec({}, { kTecG, kTecG },
                               { "Stinkdrinker Daredevil", "Mountain", "Lightning Bolt", "Mountain", "Sol Ring" });
        return s;
    };
    std::vector<int> atk;
    {
        GameState s = fresh();
        for (int i = 0; i < static_cast<int>(s.battlefield.size()); ++i)
        { if (s.battlefield[i].card.m_name.str() == kTecG) { atk.push_back(i); } }
    }
    REQUIRE(atk.size() == 2);
    // Both to the face: 6, nothing exiled.
    {
        GameState s = fresh();
        const int m[2] = { 0, 0 };
        s.scripted_tectonic_mode = TectonicPackModes(m, 2);
        std::vector<const Permanent*> ap;
        for (int i : atk) { ap.push_back(&s.battlefield[i]); }
        CHECK(CountAttackTriggerModalDamage(s, 0, ap) == 6);   // the projection reads both ordinals
        ApplyAttackModalTriggers(s, 0, atk);
        CHECK(s.players[1].life == 14);
        CHECK(s.players[0].library.size() == 5);
        CHECK(s.scripted_tectonic_mode == -1);
        CHECK(s.scripted_tectonic_keep == -1);
    }
    // First copy B keeping index 1 (the Mountain), second copy B keeping index 0 (the Lightning Bolt).
    {
        GameState s = fresh();
        const int m[2] = { 1, 1 }, k[2] = { 1, 0 };
        s.scripted_tectonic_mode = TectonicPackModes(m, 2);
        s.scripted_tectonic_keep = TectonicPackKeeps(k, 2);
        ApplyAttackModalTriggers(s, 0, atk);
        CHECK(s.players[1].life == 20);
        REQUIRE(s.players[0].hand.size() == 2);
        CHECK(s.players[0].hand[0].m_name.str() == "Mountain");
        CHECK(s.players[0].hand[1].m_name.str() == "Lightning Bolt");
        CHECK(s.players[0].library.size() == 1);
    }
    // =0 hatch: the second copy takes the default (impulse, highest MV) after the first consumed the pin.
    {
        arm.Set(heurarm::TECTONIC_ORDINALS, 0);
        GameState s = fresh();
        s.scripted_tectonic_mode = 0;
        std::vector<const Permanent*> ap;
        for (int i : atk) { ap.push_back(&s.battlefield[i]); }
        CHECK(CountAttackTriggerModalDamage(s, 0, ap) == 6);   // the historical projection: every copy reads it
        ApplyAttackModalTriggers(s, 0, atk);
        CHECK(s.players[1].life == 17);
        CHECK(s.players[0].hand.size() == 1);
    }
}

TEST_CASE("Tectonic pin fold: a pin no trigger read is spent at declare-attackers")
{
    ArmTec arm;
    GameState s = FreshTec({}, {}, { "Mountain", "Mountain" });
    s.scripted_tectonic_mode = 1;
    s.scripted_tectonic_keep = 0;
    ApplyAttackModalTriggers(s, 0, std::vector<int>{});   // nothing attacked
    CHECK(s.scripted_tectonic_mode == -1);
    CHECK(s.scripted_tectonic_keep == -1);
    // =0 hatch: the pins ride on to the turn start.
    arm.Set(heurarm::TECTONIC_PIN_FOLD, 0);
    GameState t = FreshTec({}, {}, { "Mountain", "Mountain" });
    t.scripted_tectonic_mode = 1;
    t.scripted_tectonic_keep = 0;
    ApplyAttackModalTriggers(t, 0, std::vector<int>{});
    CHECK(t.scripted_tectonic_mode == 1);
    CHECK(t.scripted_tectonic_keep == 0);
}

// ---- Surtland Flinger: the same gate shape (MTG_FLING_CAST_AXIS / MTG_FLING_PIN_FOLD) -------------------------

namespace
{
const char* kFlg = "Surtland Flinger";

// The fling pins carried by the plans that cast `name` (or by every plan when `name` is empty).
std::set<int> FlingPins(const std::vector<TurnSolver::Plan>& plans, const std::string& name)
{
    std::set<int> out;
    for (const TurnSolver::Plan& p : plans)
    {
        if (!name.empty() && !CastsName(p, name)) { continue; }
        if (p.fling_victim_choice != -1) { out.insert(p.fling_victim_choice); }
    }
    return out;
}
int NumberOf(const GameState& s, const std::string& name)
{
    for (const Permanent& p : s.battlefield) { if (p.card.m_name.str() == name) { return p.card.m_number; } }
    for (const Card& c : s.players[0].hand) { if (c.m_name.str() == name) { return c.m_number; } }
    return -1;
}
}   // namespace

TEST_CASE("Fling cast axis: a Flinger the plan casts beside Lightning Greaves gets DECLINE and every victim")
{
    ArmTec arm;
    // Five Mountains: Surtland Flinger {3}{R}{R}. A Daredevil in play is the one victim.
    GameState s = FreshTec({ kFlg }, { "Lightning Greaves", "Mountain", "Stinkdrinker Daredevil" },
                           { "Mountain", "Mountain", "Lightning Bolt" });
    const int dd = NumberOf(s, "Stinkdrinker Daredevil");
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, /*is_pre_combat=*/true);
    CHECK(FlingPins(plans, kFlg) == std::set<int>{ -2, dd });
    // =0 hatch: the battlefield-only gate -- the cast Flinger takes the front (sacrifices the Daredevil).
    arm.Set(heurarm::FLING_CAST_AXIS, 0);
    CHECK(FlingPins(TurnSolver::EnumerateMainPlans(s, /*is_pre_combat=*/true), "").empty());
}

TEST_CASE("Fling cast axis: no haste source, no variant; a creature the plan casts is a victim of a Flinger in play")
{
    {
        GameState s = FreshTec({ kFlg }, { "Mountain", "Stinkdrinker Daredevil" }, { "Mountain", "Mountain" });
        CHECK(FlingPins(TurnSolver::EnumerateMainPlans(s, /*is_pre_combat=*/true), "").empty());
    }
    {
        // A Flinger in play; the plan casts a Tectonic Giant (four Mountains): the fresh Giant is a legal victim.
        GameState s = FreshTec({ kTecG }, { kFlg }, { "Mountain", "Mountain" });
        const int tg = NumberOf(s, kTecG);
        const std::set<int> pins = FlingPins(TurnSolver::EnumerateMainPlans(s, /*is_pre_combat=*/true), kTecG);
        CHECK(pins.count(-2) == 1);
        CHECK(pins.count(tg) == 1);
    }
}

TEST_CASE("Fling pin fold: a post-combat list fans no fling variant, and an unread pin is spent at combat")
{
    ArmTec arm;
    GameState s = FreshTec({ "Lightning Bolt" }, { kFlg, "Stinkdrinker Daredevil" }, { "Mountain", "Mountain" });
    CHECK_FALSE(FlingPins(TurnSolver::EnumerateMainPlans(s, /*is_pre_combat=*/true), "").empty());
    CHECK(FlingPins(TurnSolver::EnumerateMainPlans(s, /*is_pre_combat=*/false), "").empty());
    GameState t = FreshTec({}, {}, { "Mountain" });
    t.scripted_fling_victim = -2;
    std::vector<int> none;
    FireAttackSacFling(t, 0, none);
    CHECK(t.scripted_fling_victim == -1);
    // =0 hatch: the historical shapes.
    arm.Set(heurarm::FLING_PIN_FOLD, 0);
    CHECK_FALSE(FlingPins(TurnSolver::EnumerateMainPlans(s, /*is_pre_combat=*/false), "").empty());
    GameState u = FreshTec({}, {}, { "Mountain" });
    u.scripted_fling_victim = -2;
    FireAttackSacFling(u, 0, none);
    CHECK(u.scripted_fling_victim == -2);
}
