// Unit tests for the SUBTYPE cost reducers and the "choose a creature type" permanent.
//
// Two things here are easy to get wrong in a way no aggregate fingerprint would catch:
//
//  1. The per-reducer AMOUNT. `reduces_spell_subtype` shipped as a flat "-1 generic per copy"
//     (Goblin Warchief). Dragonspeaker Shaman and Urza's Incubator read "{2} less", so the step is
//     now a parameter. A reduction that silently stayed at 1 still produces perfectly legal games --
//     just a deck that ramps a turn slower than the cards say.
//
//  2. Urza's Incubator must be GENERIC. Its type is chosen as it enters, so hard-coding "Dragon"
//     into cards.json would be wrong for every other tribal deck that runs it (USER, 2026-08-26:
//     "it should not be hardcoded because there will be other decks that use it, but it should
//     always choose dragon for this deck"). These tests pin exactly that: the SAME card entry
//     chooses Dragon next to Dragons and Sliver next to Slivers, decided off the deck around it.
#include <doctest/doctest.h>

#include "ai/ManaPayment.h"
#include "cards/CardDatabase.h"
#include "core/GameState.h"
#include "core/SpellEffects.h"
#include "core/HeuristicDefaults.h"

#include <string>
#include <vector>

namespace
{

void EnsureCardsLoaded()
{
    static const bool loaded = []
    {
        const auto path = ResolveHeuristicDefaultsPath("src/cards/data/cards.json");
        CardDatabase::Instance().LoadFromJson(path);
        return true;
    }();
    REQUIRE(loaded);
}

const CardDefinition& Def(const std::string& name)
{
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE_MESSAGE(d != nullptr, "card not in cards.json: ", name);
    return *d;
}

// Put `name` onto the battlefield under player 0, routed through the SAME universal enter cascade
// the executor and rollout use -- so a "choose a creature type" permanent makes its real choice.
void Deploy(GameState& s, const std::string& name)
{
    Permanent p;
    p.card             = Def(name).card;
    p.controller_index = 0;
    p.owner_index      = 0;
    s.battlefield.push_back(p);
    FireEtbWatchers(s, 0, static_cast<int>(s.battlefield.size()) - 1);
}

// A board whose owner's LIBRARY is the given tribe -- that is what a type-choosing permanent reads.
GameState BoardWithLibrary(const std::vector<std::string>& library)
{
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 4;
    for (const std::string& n : library) { s.players[0].library.push_back(Def(n).card); }
    return s;
}

int GenericOf(const std::string& spell, const GameState& s)
{
    return EffectiveSpellCost(Def(spell), s, 1).generic;
}

}   // namespace

TEST_CASE("subtype cost reducers subtract their own per-copy amount")
{
    EnsureCardsLoaded();

    // Inferno of the Star Mounts is {4}{R}{R}: generic 4, two red pips.
    REQUIRE(Def("Inferno of the Star Mounts").card.m_mana_cost.generic == 4);

    GameState s = BoardWithLibrary({});
    CHECK(GenericOf("Inferno of the Star Mounts", s) == 4);      // no reducer

    // Dragonspeaker Shaman: "Dragon spells you cast cost {2} less" -- the step is 2, not 1.
    Deploy(s, "Dragonspeaker Shaman");
    CHECK(GenericOf("Inferno of the Star Mounts", s) == 2);

    // ...and it STACKS per copy, still flooring at 0 rather than going negative.
    Deploy(s, "Dragonspeaker Shaman");
    CHECK(GenericOf("Inferno of the Star Mounts", s) == 0);
    Deploy(s, "Dragonspeaker Shaman");
    CHECK(GenericOf("Inferno of the Star Mounts", s) == 0);

    // Colour pips are never reduced -- "costs {2} less" only ever touches generic.
    CHECK(EffectiveSpellCost(Def("Inferno of the Star Mounts"), s, 1).red == 2);
}

TEST_CASE("a subtype reducer only discounts spells carrying its subtype")
{
    EnsureCardsLoaded();
    GameState s = BoardWithLibrary({});
    Deploy(s, "Dragonspeaker Shaman");

    // Lightning Bolt is not a Dragon spell (and has no generic to give anyway).
    CHECK(GenericOf("Lightning Bolt", s) == Def("Lightning Bolt").card.m_mana_cost.generic);
    // Dragonspeaker Shaman is a Human Barbarian Shaman, NOT a Dragon, so it never discounts itself.
    CHECK(GenericOf("Dragonspeaker Shaman", s)
          == Def("Dragonspeaker Shaman").card.m_mana_cost.generic);
}

TEST_CASE("Urza's Incubator chooses its creature type from the DECK, not from cards.json")
{
    EnsureCardsLoaded();

    // The card entry itself must carry NO hard-coded tribe -- that is the whole point.
    CHECK(Def("Urza's Incubator").params.chooses_creature_type);
    CHECK(Def("Urza's Incubator").params.reduces_spell_subtype.empty());

    const uint16_t dragon = SubtypeRegistry::Instance().Id("Dragon");
    const uint16_t sliver = SubtypeRegistry::Instance().Id("Sliver");
    REQUIRE(dragon != SubtypeRegistry::kNone);
    REQUIRE(sliver != SubtypeRegistry::kNone);

    SUBCASE("next to Dragons it chooses Dragon")
    {
        GameState s = BoardWithLibrary({ "Scourge of Valkas", "Utvara Hellkite", "Glorybringer",
                                         "Atsushi, the Blazing Sky", "Lightning Bolt" });
        Deploy(s, "Urza's Incubator");
        CHECK(s.battlefield.back().chosen_subtype_id == dragon);

        // Glorybringer {3}{R}{R} -> {1}{R}{R}: the SAME entry, discounting Dragons.
        CHECK(GenericOf("Glorybringer", s) == 1);
    }

    SUBCASE("next to Slivers the SAME card entry chooses Sliver")
    {
        GameState s = BoardWithLibrary({ "Sinew Sliver", "Sinew Sliver", "Predatory Sliver",
                                         "Lightning Bolt" });
        Deploy(s, "Urza's Incubator");
        CHECK(s.battlefield.back().chosen_subtype_id == sliver);
        CHECK(s.battlefield.back().chosen_subtype_id != dragon);
    }
}

TEST_CASE("Urza's Incubator discounts CREATURE spells only")
{
    EnsureCardsLoaded();
    // Dragon Tempest is an ENCHANTMENT, so a "creature spells of the chosen type" reducer must not
    // touch it even in a deck whose chosen type is Dragon (it is not a Dragon spell either way).
    CHECK(Def("Urza's Incubator").params.reduces_spell_subtype_creature_only);
    CHECK_FALSE(Def("Dragon Tempest").card.IsCreature());

    GameState s = BoardWithLibrary({ "Scourge of Valkas", "Utvara Hellkite", "Glorybringer" });
    Deploy(s, "Urza's Incubator");
    CHECK(GenericOf("Dragon Tempest", s) == Def("Dragon Tempest").card.m_mana_cost.generic);
}

TEST_CASE("Fire Diamond enters tapped when CAST, so it makes no mana that turn")
{
    EnsureCardsLoaded();
    CHECK(Def("Fire Diamond").params.enters_tapped);
    CHECK(Def("Fire Diamond").params.mana_rock);

    GameState s = BoardWithLibrary({});
    Deploy(s, "Fire Diamond");
    // Deploy() mirrors the enter cascade, not the cast path's tapped-entry, so assert the FLAG is
    // what both worlds read; the cast-path wiring itself is covered by the smoke digest.
    s.battlefield.back().tapped = Def("Fire Diamond").params.enters_tapped;
    CHECK(AvailableManaPool(s).Total() == 0);

    // Once it untaps it is a real red source.
    s.battlefield.back().tapped = false;
    CHECK(AvailableManaPool(s).red == 1);
}
