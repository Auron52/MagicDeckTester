// Unit tests for the Prevent Damage ENGINE-ROLE tutor ranking (USER doctrine 2026-09-29):
// PreventDamageProvider::TutorCandidates for Living Wish (MTG_PD_WISH_RANK); Beseech the Queen and
// Green Sun's Zenith are NOT ranked (GSZ's enumerator bakes every target, so the list has no
// consumer there). One board per branch of PdRankEngineTutor; every board also pins the lever-OFF
// order (the control arm that MUST differ), and the hatch case proves =0 restores the old
// nonlands-first zone order exactly.
#include <doctest/doctest.h>

#include "ai/DecisionProviders.h"
#include "ai/HeuristicArm.h"
#include "cards/CardDatabase.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"

#include <algorithm>
#include <string>
#include <vector>

namespace
{

void EnsureCardsPdw()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

Card PhW(const std::string& name, int number)
{
    REQUIRE_MESSAGE(CardDatabase::Instance().Lookup(name) != nullptr, "card not in cards.json: ", name);
    Card c;
    c.m_name = name;
    c.RehashName();
    c.m_number = number;
    return c;
}

const char* kVitoW  = "Vito, Thorn of the Dusk Rose";
const char* kDinaW  = "Dina, Soul Steeper";
const char* kRhoxW  = "Rhox Faithmender";

// The shipped sideboard, in .cod order (the OFF arm's order is this, nonlands first).
const std::vector<std::string> kSideW = {
    kRhoxW, kRhoxW, "Tamanoa", "Purity", "Brushland", "Battlefield Forge", "Dimir House Guard",
    "Shriekmaw", "Vexing Shusher", kVitoW, "Timeless Witness", "Bilbo, Birthday Celebrant",
    kDinaW, kDinaW, "Acidic Slime",
};

GameState MakeW(const std::vector<std::string>& hand, const std::vector<std::string>& board,
                const std::vector<std::string>& library = { "Tamanoa", kDinaW, kVitoW, kRhoxW,
                                                            "City of Brass" })
{
    EnsureCardsPdw();
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 4;
    int num = 1;
    for (const std::string& n : hand)    { s.players[0].hand.push_back(PhW(n, num++)); }
    for (const std::string& n : library) { s.players[0].library.push_back(PhW(n, num++)); }
    for (const std::string& n : kSideW)  { s.players[0].sideboard.push_back(PhW(n, num++)); }
    for (const std::string& n : board)
    {
        Permanent p;
        p.card              = CardDatabase::Instance().Lookup(n)->card;
        p.card.m_number     = num++;
        p.controller_index  = 0;
        p.owner_index       = 0;
        p.entered_this_turn = false;
        s.battlefield.push_back(p);
    }
    return s;
}

const CardParams& ParamsOf(const char* name) { return CardDatabase::Instance().Lookup(name)->params; }

std::vector<std::string> Wish(const GameState& s)
{ return PreventDamageProvider().TutorCandidates(s, 0, ParamsOf("Living Wish")); }

std::vector<std::string> WishOff(const GameState& s)
{
    heurarm::t_arm[heurarm::PD_WISH_RANK] = 0;
    std::vector<std::string> v = Wish(s);
    heurarm::t_arm[heurarm::PD_WISH_RANK] = -1;
    return v;
}

int Pos(const std::vector<std::string>& v, const std::string& nm)
{
    auto it = std::find(v.begin(), v.end(), nm);
    return it == v.end() ? -1 : static_cast<int>(it - v.begin());
}

// The lever-OFF order: the sideboard's distinct names, nonlands first, zone order in each half.
const std::vector<std::string> kOffOrder = {
    kRhoxW, "Tamanoa", "Purity", "Dimir House Guard", "Shriekmaw", "Vexing Shusher", kVitoW,
    "Timeless Witness", "Bilbo, Birthday Celebrant", kDinaW, "Acidic Slime",
    "Brushland", "Battlefield Forge",
};

}   // namespace

TEST_CASE("PD wish W1: no gain engine -> Tamanoa, Vito, Dina, Rhox; lands; the rest")
{
    const GameState s = MakeW({ "Living Wish", "Manabarbs" }, { "City of Brass", "Battlefield Forge" });
    const std::vector<std::string> v = Wish(s);
    REQUIRE(v.size() == kOffOrder.size());
    CHECK(v[0] == "Tamanoa");
    CHECK(v[1] == kVitoW);
    CHECK(v[2] == kDinaW);
    CHECK(v[3] == kRhoxW);
    CHECK(Pos(v, "Brushland") < Pos(v, "Purity"));       // lands before the non-engine rest
    CHECK(Pos(v, "Purity") > Pos(v, "Battlefield Forge"));
    CHECK(WishOff(s) == kOffOrder);                      // control: OFF leads with Rhox
}

TEST_CASE("PD wish W2: gain engine, no drain -> Vito first, then Dina")
{
    const GameState s = MakeW({ "Living Wish" },
                              { "Tamanoa", "City of Brass", "Battlefield Forge", "Grand Coliseum" });
    const std::vector<std::string> v = Wish(s);
    CHECK(v[0] == kVitoW);
    CHECK(v[1] == kDinaW);
    CHECK(v[2] == "Tamanoa");
    CHECK(v[3] == kRhoxW);
    CHECK(WishOff(s).front() == kRhoxW);
}

TEST_CASE("PD wish W2b: gain in HAND, no drain, next turn's mana is 2 -> Dina before Vito")
{
    const GameState s = MakeW({ "Living Wish", "Tamanoa", "Brushland" }, { "City of Brass" });
    const std::vector<std::string> v = Wish(s);
    CHECK(v[0] == kDinaW);
    CHECK(v[1] == kVitoW);
}

TEST_CASE("PD wish W3: gain + drain, {R}{G}{W} coverable -> a SECOND Tamanoa, then Rhox")
{
    const GameState s = MakeW({ "Living Wish" },
                              { "Tamanoa", kVitoW, "City of Brass", "Battlefield Forge", "Brushland" });
    const std::vector<std::string> v = Wish(s);
    CHECK(v[0] == "Tamanoa");
    CHECK(v[1] == kRhoxW);
    CHECK(v[2] == kDinaW);
    CHECK(v[3] == kVitoW);   // the legend-rule duplicate sinks to the end of the engine group
}

TEST_CASE("PD wish W4: gain + drain, {R}{G}{W} TIGHT (no green) but {3}{W} coverable -> Rhox")
{
    const GameState s = MakeW({ "Living Wish" },
                              { "Tamanoa", kDinaW, "Ancient Tomb", "Battlefield Forge", "Battlefield Forge" });
    const std::vector<std::string> v = Wish(s);
    CHECK(v[0] == kRhoxW);
    CHECK(v[1] == "Tamanoa");
    CHECK(Pos(v, kDinaW) > Pos(v, kVitoW));   // Dina is the legend duplicate here
}

TEST_CASE("PD wish W5: no gain engine, Vito in HAND -> a second Vito sinks below Rhox")
{
    const GameState s = MakeW({ "Living Wish", kVitoW }, { "City of Brass", "Grand Coliseum" });
    const std::vector<std::string> v = Wish(s);
    CHECK(v[0] == "Tamanoa");
    CHECK(v[1] == kDinaW);
    CHECK(v[2] == kRhoxW);
    CHECK(v[3] == kVitoW);
}

TEST_CASE("PD wish W6: lands -- the colour-fixer for the missing colour first")
{
    // Board makes G and W (Brushland) but no R: Tamanoa (the top pick) needs R -> Battlefield Forge
    // ahead of a second Brushland, reversing the sideboard's zone order.
    const GameState s = MakeW({ "Living Wish" }, { "Brushland" });
    const std::vector<std::string> v = Wish(s);
    CHECK(v[0] == "Tamanoa");
    CHECK(Pos(v, "Battlefield Forge") < Pos(v, "Brushland"));
    // With R covered (Karplusan) nothing is missing -> zone order (Brushland first).
    const GameState s2 = MakeW({ "Living Wish" }, { "Brushland", "Karplusan Forest" });
    const std::vector<std::string> v2 = Wish(s2);
    CHECK(Pos(v2, "Brushland") < Pos(v2, "Battlefield Forge"));
}

TEST_CASE("PD wish hatch: MTG_PD_WISH_RANK=0 restores the nonlands-first zone order exactly")
{
    const GameState s = MakeW({ "Living Wish" }, { "Tamanoa", "City of Brass" });
    CHECK(WishOff(s) == kOffOrder);
    heurarm::t_arm[heurarm::PD_WISH_RANK] = 1;
    CHECK(Wish(s).front() == kVitoW);
    heurarm::t_arm[heurarm::PD_WISH_RANK] = -1;
}

TEST_CASE("PD wish: Beseech the Queen and Green Sun's Zenith are NOT ranked (lever-invariant)")
{
    PreventDamageProvider pd;
    const GameState s = MakeW({ "Green Sun's Zenith", "Beseech the Queen" },
                              { "Tamanoa", "City of Brass", "Grand Coliseum", "Ancient Tomb" });
    for (const char* tutor : { "Beseech the Queen", "Green Sun's Zenith" })
    {
        const CardParams& pp = ParamsOf(tutor);
        const std::vector<std::string> on = pd.TutorCandidates(s, 0, pp);
        heurarm::t_arm[heurarm::PD_WISH_RANK] = 0;
        CHECK(pd.TutorCandidates(s, 0, pp) == on);
        heurarm::t_arm[heurarm::PD_WISH_RANK] = -1;
        CHECK(on.front() == "Tamanoa");   // library order: the ranking would have led Vito / Dina
    }
}
