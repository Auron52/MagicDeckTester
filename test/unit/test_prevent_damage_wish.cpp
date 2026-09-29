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

// The ORDER tests read the whole list, so they pin the useful-target restriction
// (MTG_PD_WISH_USEFUL) off; WishUseful() reads the shipped default.
std::vector<std::string> WishUseful(const GameState& s)
{ return PreventDamageProvider().TutorCandidates(s, 0, ParamsOf("Living Wish")); }

std::vector<std::string> Wish(const GameState& s)
{
    heurarm::t_arm[heurarm::PD_WISH_USEFUL] = 0;
    std::vector<std::string> v = WishUseful(s);
    heurarm::t_arm[heurarm::PD_WISH_USEFUL] = -1;
    return v;
}

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

TEST_CASE("PD wish W7: a held Green Sun's Zenith covers the gain role -> Vito first (USER 2026-09-29)")
{
    // Same board as W1 plus a Zenith in hand and a Tamanoa in the library: the Zenith gets Tamanoa,
    // it cannot get Vito (black), so the Wish goes for Vito.
    const GameState s = MakeW({ "Living Wish", "Green Sun's Zenith", "Manabarbs" },
                              { "City of Brass", "Battlefield Forge", "Karplusan Forest" });
    const std::vector<std::string> v = Wish(s);
    CHECK(v[0] == kVitoW);
    CHECK(v[1] == kDinaW);
    // Control: the Zenith sub-lever off -> the plain no-gain order (Tamanoa first).
    heurarm::t_arm[heurarm::PD_WISH_ZENITH] = 0;
    const std::vector<std::string> off = Wish(s);
    heurarm::t_arm[heurarm::PD_WISH_ZENITH] = -1;
    CHECK(off[0] == "Tamanoa");
}

TEST_CASE("PD wish W7b: a Zenith with NO Tamanoa left in the library covers nothing -> Tamanoa first")
{
    const GameState s = MakeW({ "Living Wish", "Green Sun's Zenith" },
                              { "City of Brass", "Battlefield Forge", "Karplusan Forest" },
                              { kDinaW, kVitoW, kRhoxW, "City of Brass" });
    CHECK(Wish(s)[0] == "Tamanoa");
}

TEST_CASE("PD wish W8: the search sees only the useful targets -- engine creatures + lands (USER 2026-09-29)")
{
    const GameState s = MakeW({ "Living Wish", "Manabarbs" }, { "City of Brass", "Battlefield Forge" });
    const std::vector<std::string> v = WishUseful(s);
    // (the one-land trim keeps Battlefield Forge only -- W15)
    const std::vector<std::string> want = { "Tamanoa", kVitoW, kDinaW, kRhoxW, "Battlefield Forge" };
    CHECK(v.size() == want.size());
    for (const std::string& nm : want) { CHECK(Pos(v, nm) >= 0); }
    for (const char* gone : { "Purity", "Bilbo, Birthday Celebrant", "Dimir House Guard", "Shriekmaw",
                              "Vexing Shusher", "Timeless Witness", "Acidic Slime" })
    { CHECK(Pos(v, gone) < 0); }
    CHECK(v[0] == "Tamanoa");
    // Control: the restriction off -> every legal name.
    CHECK(Wish(s).size() == kOffOrder.size());
}

TEST_CASE("PD wish W9: colours covered + a land for this turn and next -> no land targets (USER 2026-09-29)")
{
    // City of Brass makes every colour; two lands in hand cover this turn's drop and next turn's.
    const GameState s = MakeW({ "Living Wish", "Karplusan Forest", "Brushland" },
                              { "City of Brass", "Battlefield Forge" });
    const std::vector<std::string> v = WishUseful(s);
    CHECK(Pos(v, "Brushland") < 0);
    CHECK(Pos(v, "Battlefield Forge") < 0);
    // {R}{G}{W} from three distinct lands and no Ancient Tomb -> Rhox goes too (W11).
    CHECK(v.size() == 3);
    // Control: only ONE land in hand for two drops -> the lands stay.
    const GameState s1 = MakeW({ "Living Wish", "Karplusan Forest" }, { "City of Brass", "Battlefield Forge" });
    CHECK(Pos(WishUseful(s1), "Battlefield Forge") >= 0);
    // Control: the trim sub-lever off -> the six useful targets.
    heurarm::t_arm[heurarm::PD_WISH_TRIM] = 0;
    CHECK(WishUseful(s).size() == 6);
    heurarm::t_arm[heurarm::PD_WISH_TRIM] = -1;
}

TEST_CASE("PD wish W10: a Vito already held (hand or board) is not a Wish target; Dina, Tamanoa, Rhox stay")
{
    const GameState s = MakeW({ "Living Wish", kVitoW }, { "City of Brass" });
    const std::vector<std::string> v = WishUseful(s);
    CHECK(Pos(v, kVitoW) < 0);
    CHECK(Pos(v, kDinaW) >= 0);
    CHECK(Pos(v, "Tamanoa") >= 0);
    CHECK(Pos(v, kRhoxW) >= 0);
    const GameState b = MakeW({ "Living Wish" }, { "City of Brass", kDinaW });
    CHECK(Pos(WishUseful(b), kDinaW) < 0);
    CHECK(Pos(WishUseful(b), kVitoW) >= 0);
}

TEST_CASE("PD wish W11: Tamanoa easy (R/G/W from distinct lands, no Ancient Tomb) -> Rhox trimmed; with a Tomb -> Rhox kept")
{
    const GameState easy = MakeW({ "Living Wish" }, { "Karplusan Forest", "Brushland", "City of Brass" });
    CHECK(Pos(WishUseful(easy), kRhoxW) < 0);
    CHECK(Pos(WishUseful(easy), "Tamanoa") >= 0);
    // Same colours plus an Ancient Tomb in hand -> Rhox stays a target.
    const GameState tomb = MakeW({ "Living Wish", "Ancient Tomb" }, { "Karplusan Forest", "Brushland", "City of Brass" });
    CHECK(Pos(WishUseful(tomb), kRhoxW) >= 0);
    // Tight colours (no green source) -> Rhox stays.
    const GameState tight = MakeW({ "Living Wish" }, { "Battlefield Forge", "City of Brass" });
    CHECK(Pos(WishUseful(tight), kRhoxW) >= 0);
}

TEST_CASE("PD wish W12: gain + drain held, an Ancient Tomb -> Rhox ranks above the 2nd Tamanoa")
{
    const GameState s = MakeW({ "Living Wish", "Ancient Tomb" },
                              { "Tamanoa", kVitoW, "Karplusan Forest", "Brushland", "City of Brass" });
    const std::vector<std::string> v = Wish(s);
    CHECK(Pos(v, kRhoxW) < Pos(v, "Tamanoa"));
    heurarm::t_arm[heurarm::PD_WISH_TRIM] = 0;           // control: the tight-colours rule only
    const std::vector<std::string> off = Wish(s);
    heurarm::t_arm[heurarm::PD_WISH_TRIM] = -1;
    CHECK(Pos(off, "Tamanoa") < Pos(off, kRhoxW));
}

TEST_CASE("PD wish W13: MTG_PD_WISH_VITO_OVER_DINA (measurement lever) -- Dina is not a target while Vito is fetchable")
{
    const GameState s = MakeW({ "Living Wish" }, { "City of Brass", "Battlefield Forge" });
    heurarm::t_arm[heurarm::PD_WISH_VITO_OVER_DINA] = 1;
    const std::vector<std::string> on = WishUseful(s);
    heurarm::t_arm[heurarm::PD_WISH_VITO_OVER_DINA] = -1;
    CHECK(Pos(on, kDinaW) < 0);
    CHECK(Pos(on, kVitoW) >= 0);
    CHECK(Pos(WishUseful(s), kDinaW) >= 0);              // control: default OFF keeps Dina
    // Vito already held -> Vito is trimmed (legend) and Dina stays even with the lever on.
    const GameState held = MakeW({ "Living Wish", kVitoW }, { "City of Brass" });
    heurarm::t_arm[heurarm::PD_WISH_VITO_OVER_DINA] = 1;
    const std::vector<std::string> h = WishUseful(held);
    heurarm::t_arm[heurarm::PD_WISH_VITO_OVER_DINA] = -1;
    CHECK(Pos(h, kDinaW) >= 0);
    CHECK(Pos(h, kVitoW) < 0);
}

TEST_CASE("PD wish W14: a Vito ON THE BATTLEFIELD counts as held -- legend trim + Vito-over-Dina both see it")
{
    const GameState s = MakeW({ "Living Wish" }, { "City of Brass", kVitoW });
    heurarm::t_arm[heurarm::PD_WISH_VITO_OVER_DINA] = 1;
    const std::vector<std::string> v = WishUseful(s);
    heurarm::t_arm[heurarm::PD_WISH_VITO_OVER_DINA] = -1;
    CHECK(Pos(v, kVitoW) < 0);    // legend rule: a second Vito would die
    CHECK(Pos(v, kDinaW) >= 0);   // Vito is out, so Dina is the drain left to add
}

TEST_CASE("PD wish W15: at most ONE land target -- Battlefield Forge when available, else Brushland")
{
    const GameState s = MakeW({ "Living Wish", "Manabarbs" }, { "City of Brass", "Battlefield Forge" });
    const std::vector<std::string> v = WishUseful(s);
    CHECK(Pos(v, "Battlefield Forge") >= 0);
    CHECK(Pos(v, "Brushland") < 0);
    heurarm::t_arm[heurarm::PD_WISH_TRIM] = 0;             // control: both lands without the trim
    const std::vector<std::string> off = WishUseful(s);
    heurarm::t_arm[heurarm::PD_WISH_TRIM] = -1;
    CHECK(Pos(off, "Brushland") >= 0);
}

namespace
{
bool ZenithMayFetchDina(const GameState& s)
{
    const PreventDamageProvider prov;
    const DecisionProvider::PutPolicy pol = prov.PutTargetPolicy(s, 0);
    return !pol.narrow || prov.PutTargetOk(pol, *CardDatabase::Instance().Lookup(kDinaW));
}
}

TEST_CASE("PD zenith Z1: Dina on the battlefield -> Green Sun's Zenith does not fetch Dina; Tamanoa still")
{
    const GameState s = MakeW({ "Green Sun's Zenith" }, { "City of Brass", "Karplusan Forest", kDinaW });
    CHECK_FALSE(ZenithMayFetchDina(s));
    const PreventDamageProvider prov;
    CHECK(prov.PutTargetOk(prov.PutTargetPolicy(s, 0), *CardDatabase::Instance().Lookup("Tamanoa")));
    heurarm::t_arm[heurarm::PD_ZENITH_SKIP_DINA] = 0;     // control: lever off -> Dina allowed
    CHECK(ZenithMayFetchDina(s));
    heurarm::t_arm[heurarm::PD_ZENITH_SKIP_DINA] = -1;
}

TEST_CASE("PD zenith Z2: Dina in HAND -- skipped only when castable ({B} + {G} from distinct lands)")
{
    // City of Brass (B) + Karplusan Forest (G): castable -> skip.
    CHECK_FALSE(ZenithMayFetchDina(MakeW({ "Green Sun's Zenith", kDinaW }, { "City of Brass", "Karplusan Forest" })));
    // One land only -> not castable -> Zenith may still fetch Dina.
    CHECK(ZenithMayFetchDina(MakeW({ "Green Sun's Zenith", kDinaW }, { "Karplusan Forest" })));
    // One land on board + a black source in hand with the land drop open -> castable -> skip.
    CHECK_FALSE(ZenithMayFetchDina(MakeW({ "Green Sun's Zenith", kDinaW, "City of Brass" }, { "Karplusan Forest" })));
    // No Dina anywhere -> no narrowing.
    CHECK(ZenithMayFetchDina(MakeW({ "Green Sun's Zenith" }, { "City of Brass", "Karplusan Forest" })));
}
