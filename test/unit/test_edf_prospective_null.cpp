// EDF: the prospective flicker recogniser tolerates a non-cast pick in `casting`.
//
// Solve's consider() pushes cands[j].def for EVERY selected action, so a land drop or an activation
// arrives as nullptr. The outlet / payload scans null-checked; the MTG_EDF_PROSPECTIVE_DEBUG print and
// the cast-sink scan did not -- the print segfaulted every traced EDF game (rc3 kit). This pins the
// sink scan: a plan that assembles a loop (Cloud of Faeries cast onto a board holding Eldrazi
// Displacer) next to a non-cast pick must project without crashing.
#include <doctest/doctest.h>

#include "ai/DecisionProviders.h"
#include "cards/CardDatabase.h"
#include "core/GameSetup.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"

#include <string>
#include <vector>

namespace
{

void EnsureCardsPn()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

Card PhPn(const std::string& name, int number)
{
    REQUIRE_MESSAGE(CardDatabase::Instance().Lookup(name) != nullptr, "card not in cards.json: ", name);
    Card c;
    c.m_name = name;
    c.RehashName();
    c.m_number = number;
    return c;
}

}   // namespace

TEST_CASE("EDF: the prospective go-off projection skips a nullptr (non-cast) pick")
{
    EnsureCardsPn();
    static const EldraziFlickerProvider prov;
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 5;
    s.players[0].life     = gamesetup::StartingLife();
    s.players[1].life     = 20;
    s.deck_has_land_aura  = true;
    s.m_provider          = &prov;
    int num = 1;
    for (int i = 0; i < 20; ++i) { s.players[0].library.push_back(PhPn("Brushland", num++)); }
    // Overgrowth on the Coast and Wild Growth on a Brushland: the two best untaps refund 3 + 2 = 5
    // against Displacer's {2}{C}, so the assembled loop is net positive and the projection reaches
    // the cast-sink scan (the one that dereferenced every entry of `casting`).
    struct Bp { const char* name; int host; };
    const std::vector<Bp> board = { { "Yavimaya Coast", -1 }, { "Overgrowth", 0 },
                                    { "Brushland", -1 },      { "Wild Growth", 2 },
                                    { "Mariposa Military Base", -1 }, { "Brushland", -1 },
                                    { "Shivan Gorge", -1 },   { "Eldrazi Displacer", -1 } };
    std::vector<int> ids;
    for (const Bp& bp : board)
    {
        Permanent p;
        p.card              = CardDatabase::Instance().Lookup(bp.name)->card;
        p.card.m_number     = num++;
        p.controller_index  = 0;
        p.owner_index       = 0;
        p.entered_this_turn = false;
        if (bp.host >= 0) { p.aura_attached_to = ids[static_cast<std::size_t>(bp.host)]; }
        ids.push_back(p.card.m_number);
        s.battlefield.push_back(p);
    }
    const CardDefinition* cloud = CardDatabase::Instance().Lookup("Cloud of Faeries");
    REQUIRE(cloud != nullptr);
    // The payload being cast this turn, and a non-cast pick beside it (nullptr), in both orders.
    const std::vector<const CardDefinition*> a{ cloud, nullptr };
    const std::vector<const CardDefinition*> b{ nullptr, cloud };
    CHECK(prov.ExtraLethalDamage(s, a) >= 0);
    CHECK(prov.ExtraLethalDamage(s, b) >= 0);
    CHECK(prov.ExtraLethalDamage(s, a) == prov.ExtraLethalDamage(s, b));
}
