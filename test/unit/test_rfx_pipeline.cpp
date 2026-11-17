// DAMAGE REFLECTION PIPELINE (core/DamageReflection.h, dmgrx::) -- design step 1, the core with no cards.
// docs/design/damage-reflection-pipeline.md section 2 / section 10 step 1.
//
// A symmetric sweep on an ARMED board (GameState::reflect_armed) is ONE damage event of the shared
// pipeline: marked damage, then ONE state-based-action pass that mirrors the executor's
// GameEngine::CheckStateBasedActions (lethal damage kills, indestructible saves, Equipment detaches;
// the card goes to its OWNER's graveyard, CR 400.3) plus what the pipeline needs inline (an orphaned Aura goes to the
// graveyard in the same pass; death watchers fire once per death). The unarmed board keeps the legacy
// PerformDamageAllCreatures body, pinned at the end.
#include <doctest/doctest.h>

#include "cards/CardDatabase.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"

#include <string>
#include <vector>

namespace
{

void EnsureCardsRfx()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

const CardDefinition& DefRfx(const std::string& name)
{
    EnsureCardsRfx();
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE_MESSAGE(d != nullptr, "card not in cards.json: ", name);
    return *d;
}

Permanent& PutRfx(GameState& s, const std::string& name, int number, int controller = 0)
{
    Permanent p;
    p.card              = DefRfx(name).card;
    p.card.m_number     = number;
    p.controller_index  = controller;
    p.owner_index       = controller;
    p.entered_this_turn = false;
    s.battlefield.push_back(p);
    return s.battlefield.back();
}

// A creature token exactly as the scenario loader / CreateToken shape it (tokens have no definition).
Permanent& TokenRfx(GameState& s, int number, int controller, int pw, int tg, const std::string& sub)
{
    Permanent p;
    p.card.m_name = std::to_string(pw) + "/" + std::to_string(tg) + " " + sub + " Token";
    p.card.RehashName();
    p.card.AddType(CardType::Creature);
    p.card.m_subtypes  = { sub };
    p.card.m_power     = pw;
    p.card.m_toughness = tg;
    p.card.m_number    = number;
    p.is_token          = true;
    p.controller_index  = controller;
    p.owner_index       = controller;
    p.entered_this_turn = false;
    s.battlefield.push_back(p);
    return s.battlefield.back();
}

GameState ArmedState()
{
    EnsureCardsRfx();
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 4;
    s.reflect_armed       = true;
    return s;
}

const Permanent* Find(const GameState& s, int number)
{
    for (const Permanent& p : s.battlefield) { if (p.card.m_number == number) { return &p; } }
    return nullptr;
}

int GyCount(const GameState& s, int player, const std::string& name)
{
    int n = 0;
    for (const Card& c : s.players[player].graveyard) { if (c.m_name.str() == name) { ++n; } }
    return n;
}

}   // namespace

TEST_CASE("rfx pipeline: simultaneous kill, indestructible survives, survivors keep marked damage")
{
    GameState s = ArmedState();
    TokenRfx(s, 901, 0, 1, 1, "Goblin");
    TokenRfx(s, 902, 1, 2, 2, "Spirit");
    Permanent& ind = TokenRfx(s, 903, 0, 1, 1, "Construct");
    ind.temp_indestructible = true;
    TokenRfx(s, 904, 0, 3, 3, "Minotaur");
    PerformDamageAllCreatures(s, 0, DefRfx("Pyroclasm"), 2);

    CHECK(Find(s, 901) == nullptr);   // 1/1 dies
    CHECK(Find(s, 902) == nullptr);   // 2/2 dies in the SAME pass (simultaneous, CR 608.2)
    REQUIRE(Find(s, 903) != nullptr); // indestructible: lethal damage does not destroy it
    CHECK(Find(s, 903)->damage == 2);
    REQUIRE(Find(s, 904) != nullptr); // 3/3 survives 2 with the damage marked
    CHECK(Find(s, 904)->damage == 2);
    CHECK(dmgrx::t_frame.depth == 0);
    CHECK(dmgrx::t_frame.stack.empty());
}

TEST_CASE("rfx pipeline: a dead creature goes to its OWNER's graveyard (CR 400.3), whoever controls it")
{
    GameState s = ArmedState();
    Permanent& stolen = PutRfx(s, "Boros Reckoner", 911, /*controller=*/0);
    stolen.owner_index = 1;   // the opponent's card, under our control (a steal)
    dmgrx::Sweep(s, 0, DefRfx("Pyroclasm"), 13);
    CHECK(Find(s, 911) == nullptr);
    CHECK(GyCount(s, 1, "Boros Reckoner") == 1);
    CHECK(GyCount(s, 0, "Boros Reckoner") == 0);
}

TEST_CASE("rfx pipeline: an Aura on a dead host goes to the graveyard in the same pass; Equipment detaches")
{
    GameState s = ArmedState();
    PutRfx(s, "Boros Reckoner", 921);
    Permanent& armor = PutRfx(s, "Ethereal Armor", 922);
    armor.aura_attached_to = 921;
    Permanent& eq = PutRfx(s, "Bonesplitter", 923);
    eq.equipped_to = 921;
    dmgrx::Sweep(s, 0, DefRfx("Pyroclasm"), 13);

    CHECK(Find(s, 921) == nullptr);
    CHECK(Find(s, 922) == nullptr);                    // CR 704.5m, inline
    CHECK(GyCount(s, 0, "Ethereal Armor") == 1);
    REQUIRE(Find(s, 923) != nullptr);                  // the Equipment stays ...
    CHECK(Find(s, 923)->equipped_to == 0);             // ... unattached (CR 301.5c)
}

TEST_CASE("rfx pipeline: death watchers fire exactly once per death (OnCreatureDies, FireOppCreatureDies)")
{
    GameState s = ArmedState();
    // Slimefoot (2/3): "Whenever a Saproling you control dies, Slimefoot deals 1 damage to each opponent
    // and you gain 1 life." Massacre Wurm (6/5): "Whenever a creature an opponent controls dies, that
    // player loses 2 life." Pyroclasm's 2 kills the four 1/1s and neither watcher.
    PutRfx(s, "Slimefoot, the Stowaway", 931);
    PutRfx(s, "Massacre Wurm", 932);
    TokenRfx(s, 933, 0, 1, 1, "Saproling");
    TokenRfx(s, 934, 0, 1, 1, "Saproling");
    TokenRfx(s, 935, 1, 1, 1, "Spirit");
    TokenRfx(s, 936, 1, 1, 1, "Spirit");
    const int me0 = s.players[0].life, opp0 = s.players[1].life;
    PerformDamageAllCreatures(s, 0, DefRfx("Pyroclasm"), 2);

    REQUIRE(Find(s, 931) != nullptr);
    REQUIRE(Find(s, 932) != nullptr);
    CHECK(s.battlefield.size() == 2);
    CHECK(s.players[0].life == me0 + 2);              // Slimefoot: +1 per Saproling death
    CHECK(s.players[1].life == opp0 - 2 - 2 * 2);     // Slimefoot 1+1, Massacre Wurm 2+2
    CHECK(s.creature_died_this_turn);
}

TEST_CASE("rfx pipeline: an UNARMED board keeps the legacy sweep body byte-for-byte")
{
    GameState s = ArmedState();
    s.reflect_armed = false;
    PutRfx(s, "Boros Reckoner", 941);
    Permanent& armor = PutRfx(s, "Ethereal Armor", 942);
    armor.aura_attached_to = 941;
    PerformDamageAllCreatures(s, 0, DefRfx("Pyroclasm"), 13);
    CHECK(Find(s, 941) == nullptr);
    // The legacy body clears the link and leaves the Aura for the next checkpoint (SweepOrphanedAuras'
    // disclosed approximation) -- unchanged, which is the point of the gate.
    REQUIRE(Find(s, 942) != nullptr);
    CHECK(Find(s, 942)->aura_attached_to == 0);
}
