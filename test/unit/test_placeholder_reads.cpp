// NAME-ONLY PLACEHOLDER reads (the 2026-10-29 audit; HeuristicArm.h PLACEHOLDER_*).
//
// Every hand / library / graveyard Card in real play is built by DeckLoader::MakePlaceholder from its
// NAME alone, so its own m_mana_cost, type / supertype / colour masks, m_subtypes and P/T are EMPTY; the
// real characteristics live on CardDatabase::LookupCached(c)->card (ZoneCard). These pin sites the audit
// found reading the empty fields, each with its lever ON (the default, the definition read) and OFF (the
// historical placeholder read), and every hand here is built from placeholders exactly as play builds it
// -- a test hand built from `def.card` would pass with the bug in place.
//
//   * GoblinsProvider::PayEchoToKeep's "no gas" clause (MTG_PLACEHOLDER_ECHO, DEFAULT OFF until echo is a
//     searched decision): an uncastable Muxus read as {0} and so "castable", and the echo was declined
//     whenever the hand held any spell at all; and its sub-lever MTG_GOBLIN_ECHO_LAND_DROP (the turn's land
//     drop counts toward "castable").
//   * LandfallProvider::CleanupDiscardCandidates' legend rule (MTG_PLACEHOLDER_PROVIDER, DEFAULT ON): a
//     second copy of a legend already on our battlefield is a blank and sheds first -- never, with the
//     placeholder read.
//   * ExtractMidGameFeatures' lands_in_hand / hand_castable_now (MTG_PLACEHOLDER_FEATURES, DEFAULT OFF --
//     the USER's call): a placeholder land in hand was not a land, and counted as a castable MV-0 spell.
#include <doctest/doctest.h>

#include "ai/DecisionProviders.h"
#include "ai/HeuristicArm.h"
#include "ai/KeepModel.h"
#include "cards/CardDatabase.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"

#include <string>
#include <vector>

namespace
{

void EnsureCardsPh()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

// A zone card exactly as play builds it (DeckLoader::MakePlaceholder, which is private: the name, its
// hash and nothing else).
Card PhCard(const std::string& name, int number)
{
    EnsureCardsPh();
    REQUIRE_MESSAGE(CardDatabase::Instance().Lookup(name) != nullptr, "card not in cards.json: ", name);
    Card c;
    c.m_name = CardDatabase::Instance().CanonicalName(name);
    c.RehashName();
    c.m_number = number;
    return c;
}

void PutPh(GameState& s, const std::string& name, int number, bool tapped = false)
{
    EnsureCardsPh();
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE_MESSAGE(d != nullptr, "card not in cards.json: ", name);
    Permanent p;
    p.card              = d->card;
    p.card.m_number     = number;
    p.controller_index  = 0;
    p.owner_index       = 0;
    p.entered_this_turn = false;
    p.tapped            = tapped;
    s.battlefield.push_back(p);
}

GameState BasePh(int turn)
{
    EnsureCardsPh();
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = turn;
    s.players[1].life     = 20;
    return s;
}

// Run `f` with one lever forced on (1) or off (0); restores "unset" afterwards.
template <class F>
auto WithArm(heurarm::Slot slot, int arm, F&& f)
{
    heurarm::t_arm[slot] = static_cast<std::int8_t>(arm);
    auto r = f();
    heurarm::t_arm[slot] = -1;
    return r;
}

}   // namespace

TEST_CASE("Placeholder: the placeholder really is empty (the premise of every case below)")
{
    const Card c = PhCard("Muxus, Goblin Grandee", 1);
    CHECK(c.m_mana_cost.ManaValue() == 0);
    CHECK_FALSE(c.IsCreature());
    CHECK_FALSE(c.HasSupertype(Supertype::Legendary));
    CHECK(ZoneCard(c).m_mana_cost.ManaValue() == 6);
    CHECK(ZoneCard(c).IsCreature());
    CHECK(ZoneCard(c).HasSupertype(Supertype::Legendary));
    CHECK(&ZoneCardIf(false, c) == &c);
}

TEST_CASE("Placeholder: Goblins echo -- an uncastable hand is NO gas, so the Marshal is kept")
{
    // Two untapped Mountains; Mogg War Marshal's echo is due. Hand: Muxus ({4}{R}{R}) and a Mountain --
    // nothing castable off two mana, so clause (b) keeps the attacker (PAY). Opponent at 20: clause (a)
    // (lethal now) cannot fire.
    GameState s = BasePh(3);
    PutPh(s, "Mountain", 10);
    PutPh(s, "Mountain", 11);
    PutPh(s, "Mogg War Marshal", 12);
    s.players[0].hand.push_back(PhCard("Muxus, Goblin Grandee", 1));
    s.players[0].hand.push_back(PhCard("Mountain", 2));
    const Permanent& marshal = s.battlefield.back();
    const GoblinsProvider gob;

    CHECK(WithArm(heurarm::PLACEHOLDER_ECHO, 1, [&] { return gob.PayEchoToKeep(s, marshal); }));
    // The historical read: Muxus's placeholder cost {0} is "castable" -> decline the echo.
    CHECK_FALSE(WithArm(heurarm::PLACEHOLDER_ECHO, 0, [&] { return gob.PayEchoToKeep(s, marshal); }));
    // Unset == the default (ON, unblocked by the searched echo): the real read, which pays.
    CHECK(gob.PayEchoToKeep(s, marshal));
}

TEST_CASE("Placeholder: Goblins echo -- a castable spell in hand is gas, so the echo is declined")
{
    // Goblin Lackey ({R}) IS castable off two Mountains: both reads decline.
    GameState s = BasePh(3);
    PutPh(s, "Mountain", 10);
    PutPh(s, "Mountain", 11);
    PutPh(s, "Mogg War Marshal", 12);
    s.players[0].hand.push_back(PhCard("Muxus, Goblin Grandee", 1));
    s.players[0].hand.push_back(PhCard("Goblin Lackey", 2));
    const Permanent& marshal = s.battlefield.back();
    const GoblinsProvider gob;
    CHECK_FALSE(WithArm(heurarm::PLACEHOLDER_ECHO, 1, [&] { return gob.PayEchoToKeep(s, marshal); }));
    CHECK_FALSE(WithArm(heurarm::PLACEHOLDER_ECHO, 0, [&] { return gob.PayEchoToKeep(s, marshal); }));
}

TEST_CASE("Placeholder: Goblins echo -- the turn's land drop counts toward 'castable this turn'")
{
    // Held-out seeds 1350641 / 1360791, T3 upkeep: two untapped Mountains, Mogg War Marshal's echo due,
    // Goblin Chieftain ({1}{R}{R}) and a Mountain in hand, the land drop still open. The Chieftain is
    // castable THIS turn off the drop, so the echo is declined (MTG_GOBLIN_ECHO_LAND_DROP, default ON);
    // the upkeep pool alone (two mana) reads it uncastable and pays -- stranding the T3 Chieftain.
    GameState s = BasePh(3);
    PutPh(s, "Mountain", 10);
    PutPh(s, "Mountain", 11);
    PutPh(s, "Mogg War Marshal", 12);
    s.players[0].hand.push_back(PhCard("Goblin Chieftain", 1));
    s.players[0].hand.push_back(PhCard("Mountain", 2));
    s.players[0].lands_played_this_turn = 0;
    const Permanent& marshal = s.battlefield.back();
    const GoblinsProvider gob;
    auto pay = [&](int land_drop)
    {
        heurarm::t_arm[heurarm::GOBLIN_ECHO_LAND_DROP] = static_cast<std::int8_t>(land_drop);
        const bool r = WithArm(heurarm::PLACEHOLDER_ECHO, 1, [&] { return gob.PayEchoToKeep(s, marshal); });
        heurarm::t_arm[heurarm::GOBLIN_ECHO_LAND_DROP] = -1;
        return r;
    };
    CHECK_FALSE(pay(1));   // castable after the drop -> decline the echo, cast the Chieftain
    CHECK(pay(0));         // upkeep pool alone: "no gas" -> pay
    CHECK_FALSE(gob.PayEchoToKeep(s, marshal));   // the defaults (both ON): castable after the drop -> decline
    // The drop already made: only the upkeep pool counts, and two mana do not cast the Chieftain.
    s.players[0].lands_played_this_turn = 1;
    CHECK(pay(1));
}

TEST_CASE("Placeholder: Landfall cleanup discard sheds a second copy of a legend on our battlefield")
{
    // Phylath on our battlefield (with four lands); a second Phylath in an eight-card hand is a blank
    // under the legend rule and is the first shed. Read as a placeholder it is no legend, so it ranks as
    // the deck's top threat instead and something else goes.
    GameState s = BasePh(6);
    PutPh(s, "Forest", 20);
    PutPh(s, "Forest", 21);
    PutPh(s, "Mountain", 22);
    PutPh(s, "Mountain", 23);
    PutPh(s, "Phylath, World Sculptor", 24);
    const std::vector<std::string> hand = {
        "Rampaging Baloths", "Phylath, World Sculptor", "Avenger of Zendikar", "Courser of Kruphix",
        "Forest", "Mountain", "Forest", "Primeval Titan",
    };
    int num = 1;
    for (const std::string& n : hand) { s.players[0].hand.push_back(PhCard(n, num++)); }
    for (int k = 0; k < 20; ++k) { s.players[0].library.push_back(PhCard(k % 2 ? "Forest" : "Mountain", 100 + k)); }
    s.players[0].lands_played_this_turn = 1;
    const LandfallProvider lf;

    const std::vector<int> on  = WithArm(heurarm::PLACEHOLDER_PROVIDER, 1,
                                         [&] { return lf.CleanupDiscardCandidates(s, nullptr); });
    const std::vector<int> off = WithArm(heurarm::PLACEHOLDER_PROVIDER, 0,
                                         [&] { return lf.CleanupDiscardCandidates(s, nullptr); });
    REQUIRE_FALSE(on.empty());
    REQUIRE_FALSE(off.empty());
    CHECK(on.front() == 1);    // the dead Phylath
    CHECK(off.front() != 1);   // the placeholder read keeps it (and sheds a live card)
}

TEST_CASE("Placeholder: mid-game features count a placeholder land in hand as a land")
{
    // One untapped Forest; hand = Forest + Courser of Kruphix ({1}{G}{G}, not castable off one source).
    GameState s = BasePh(2);
    PutPh(s, "Forest", 20);
    s.players[0].hand.push_back(PhCard("Forest", 1));
    s.players[0].hand.push_back(PhCard("Courser of Kruphix", 2));
    const int lih = static_cast<int>(MidGameFeature::LandsInHand);
    const int hcn = static_cast<int>(MidGameFeature::HandCastableNow);

    const std::vector<int> on  = WithArm(heurarm::PLACEHOLDER_FEATURES, 1,
                                         [&] { return ExtractMidGameFeatures(s, MidGamePlanSummary{}); });
    const std::vector<int> off = WithArm(heurarm::PLACEHOLDER_FEATURES, 0,
                                         [&] { return ExtractMidGameFeatures(s, MidGamePlanSummary{}); });
    CHECK(on[lih] == 1);
    CHECK(on[hcn] == 0);    // Courser costs 3; the Forest is not a spell
    CHECK(off[lih] == 0);   // the historical read: the placeholder Forest is no land...
    CHECK(off[hcn] == 1);   // ...and is "castable" at MV 0
}

TEST_CASE("Placeholder: shipped lever defaults (ON where the bar was met, OFF where it was not)")
{
    // Unset arms read the env defaults (the test environment sets no MTG_PLACEHOLDER_* variable).
    CHECK(PlaceholderProviderOn());
    CHECK(PlaceholderSearchOn());
    CHECK(PlaceholderSnowOn());
    CHECK(PlaceholderTectonicOn());         // unblocked by the keep axis for a Giant the plan casts
    CHECK(PlaceholderEchoOn());             // unblocked by the searched echo (MTG_ECHO_AXIS)
    CHECK_FALSE(PlaceholderManaOn());       // blocked on a sequence-aware float commit
    CHECK_FALSE(PlaceholderFeaturesOn());   // the USER's call: the shipped models were fitted on the placeholder read
}
