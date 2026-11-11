// Hydras: the authored cleanup-discard BUCKET policy (MTG_HYDRAS_BUCKET_DISCARD, analyze-deck 5i, 2026-11-11,
// DEFAULT OFF pending the USER). Pins: OFF (unset / =0) is the generic ranking byte-for-byte; ON names every hand
// index exactly once; the dead legend sheds first; extra lands shed before the threat floor; the threat order;
// the board netting (a doubler quota already met by the board); and prefix stability (the default
// CleanupDiscardShedStable claim).
#include <doctest/doctest.h>

#include "ai/DecisionProviders.h"
#include "ai/HeuristicArm.h"
#include "cards/CardDatabase.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"

#include <algorithm>
#include <cstdlib>
#include <string>
#include <vector>

namespace
{
void EnsureCardsHyd()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

Card PhHyd(const std::string& name, int number)
{
    REQUIRE_MESSAGE(CardDatabase::Instance().Lookup(name) != nullptr, "card not in cards.json: ", name);
    Card c;
    c.m_name = name;
    c.RehashName();
    c.m_number = number;
    return c;
}

GameState MakeHyd(const std::vector<std::string>& hand, const std::vector<std::string>& board)
{
    EnsureCardsHyd();
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 4;
    s.players[0].life     = 20;
    int num = 1;
    for (const std::string& n : hand) { s.players[0].hand.push_back(PhHyd(n, num++)); }
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

std::vector<int> Hyd(const GameState& s, int arm)
{
    heurarm::t_arm[heurarm::HYDRAS_BUCKET_DISCARD] = static_cast<std::int8_t>(arm);
    std::vector<int> v = HydrasProvider().CleanupDiscardCandidates(s, nullptr);
    heurarm::t_arm[heurarm::HYDRAS_BUCKET_DISCARD] = -1;
    return v;
}

std::vector<std::string> NamesHyd(const GameState& s, const std::vector<int>& order)
{
    std::vector<std::string> out;
    for (int i : order) { out.push_back(s.players[0].hand[static_cast<std::size_t>(i)].m_name.str()); }
    return out;
}

void CheckPermutationHyd(const GameState& s, const std::vector<int>& order)
{
    std::vector<int> sorted = order;
    std::sort(sorted.begin(), sorted.end());
    std::vector<int> want;
    for (int i = 0; i < static_cast<int>(s.players[0].hand.size()); ++i) { want.push_back(i); }
    CHECK(sorted == want);
}

const char* kF = "Forest";
const char* kKal = "Kalonian Hydra";
const char* kVor = "Voracious Hydra";
const char* kPri = "Primordial Hydra";
const char* kGar = "Gargos, Vicious Watcher";
const char* kBloom = "Vernal Bloom";
const char* kGaunt = "Gauntlet of Power";
const char* kLore = "Nature's Lore";
const char* kSft = "Search for Tomorrow";
const char* kGreaves = "Lightning Greaves";
}   // namespace

TEST_CASE("Hydras discard buckets: =0 is the generic ranking byte-for-byte; unset is ON (default since 2026-11-11)")
{
    const GameState s = MakeHyd({kF, kF, kF, kKal, kGaunt, kLore, kGar, kVor}, {kF, kF});
    const std::vector<int> gen = GenericProvider().CleanupDiscardCandidates(s, nullptr);
    CHECK(Hyd(s, 0) == gen);
    // The unset arm falls through to the env default, which is ON unless MTG_HYDRAS_BUCKET_DISCARD=0.
    const char* env = std::getenv("MTG_HYDRAS_BUCKET_DISCARD");
    if (env == nullptr || std::string(env) != "0") { CHECK(Hyd(s, -1) == Hyd(s, 1)); }
}

TEST_CASE("Hydras discard buckets: ON names every card; a dead legend first, then the extra lands")
{
    // Board: Gargos + two Forests. Hand: a second Gargos (dead), four Forests, Kalonian, Voracious, Bloom.
    const GameState s = MakeHyd({kGar, kF, kF, kF, kF, kKal, kVor, kBloom}, {kF, kF, kGar});
    const std::vector<int> on = Hyd(s, 1);
    CheckPermutationHyd(s, on);
    const std::vector<std::string> names = NamesHyd(s, on);
    CHECK(names[0] == kGar);
    // Two Forests are kept (fewer than five lands out); the other two shed right after the dead legend.
    CHECK(names[1] == kF);
    CHECK(names[2] == kF);
    CHECK(names.back() == kF);   // land 1 is the most protected card
    // Distance to playable (rule 4): next turn's mana is 3 Forests, so Voracious (X = 1 costs 3) is castable
    // and is kept as threat 1 ahead of Kalonian (5), Bloom is doubler 1, and Kalonian is threat 2.
    const auto pos = [&](const char* n) { return std::find(names.begin(), names.end(), n) - names.begin(); };
    CHECK(pos(kKal) < pos(kBloom));
    CHECK(pos(kBloom) < pos(kVor));
}

TEST_CASE("Hydras discard buckets: the doubler quota is net of board; spare ramp, then spare doublers, then spare lands")
{
    // Board already holds two doublers: no doubler is protected by the quota, so a hand doubler is overflow
    // (kept after every threat, before the spare ramp / lands).
    const GameState s = MakeHyd({kF, kKal, kPri, kGaunt, kLore, kSft, kGreaves, kF},
                                {kF, kF, kF, kF, kF, kF, kBloom, kBloom});
    const std::vector<int> on = Hyd(s, 1);
    CheckPermutationHyd(s, on);
    const std::vector<std::string> names = NamesHyd(s, on);
    // Six lands out: no land-2 rung and no ramp rung, so the spare ramp sheds first (Search for Tomorrow
    // before Nature's Lore), then the overflow doubler, then the spare Forest.
    CHECK(names[0] == kSft);
    CHECK(names[1] == kLore);
    CHECK(names[2] == kGaunt);
    CHECK(names[3] == kF);
}

TEST_CASE("Hydras discard buckets: the USER's own cleanup (references/Hydras/claude_s5_gi4.json, T5)")
{
    // Board: five Forests, Gargos, Gauntlet of Power, Kalonian, Vernal Bloom. The USER shed the second Gargos,
    // then a Vernal Bloom.
    const GameState s = MakeHyd({kBloom, kVor, kSft, kGar, kF, kF, kF, kBloom, kKal},
                                {kF, kF, kF, kF, kF, kGar, kGaunt, kKal, kBloom});
    const std::vector<std::string> names = NamesHyd(s, Hyd(s, 1));
    CHECK(names[0] == kGar);
    CHECK(names[1] == kBloom);
}

TEST_CASE("Hydras discard buckets: prefix-stable (shedding the top pick leaves the rest in order)")
{
    GameState s = MakeHyd({kF, kF, kF, kKal, kGaunt, kLore, kGar, kVor, kSft, kBloom, kGreaves}, {kF, kF, kF});
    std::vector<int> order = Hyd(s, 1);
    while (s.players[0].hand.size() > 1)
    {
        std::vector<std::string> rest = NamesHyd(s, order);
        rest.erase(rest.begin());
        s.players[0].hand.erase(s.players[0].hand.begin() + order.front());
        order = Hyd(s, 1);
        CHECK(NamesHyd(s, order) == rest);
    }
}
