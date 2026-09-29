// Aether Vial's charge ceiling must come from the DECKLIST, and the charge policy must not depend
// on it being known. Both halves are pinned here because the alternative arrangement -- the ceiling
// carried on MulliganProfile, and WantVialCharge returning false on line 1 when it was unset --
// silently turned every Aether Vial into a blank three separate times:
//
//   * slivers_vial, 2026-08: a screen measured on a DEFAULT profile; cutting two Vials read
//     -0.0953 instead of -0.0246 (docs/design/deck-combination-screening.md).
//   * Pirates, 2026-09: "the Vial never charges, because there is no .profile.json yet"
//     (docs/design/analysis-Pirates.md).
//   * WhiteKnights, 2026-09-29: deck_compare.py hands every screen arm the BASE deck's profile,
//     and the base held no Vial, so six arms across three rounds priced a card that could not act.
//
// The failure was invisible every time: a Vial on zero counters is a legal permanent that simply
// never deploys anything, so nothing errors, nothing warns, and the games look fine.
#include <doctest/doctest.h>

#include "cards/CardDatabase.h"
#include "core/GameState.h"
#include "core/SpellEffects.h"
#include "core/HeuristicDefaults.h"
#include "deck/DeckLoader.h"
#include "runner/GoldFishRunner.h"

#include "mtg_test_seam.h"

#include <string>
#include <vector>

namespace
{

void EnsureCardsLoaded()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

Card Make(const std::string& name)
{
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE_MESSAGE(d != nullptr, "card not in cards.json: ", name);
    return d->card;
}

void Add(Decklist& deck, const std::string& name, int n)
{
    for (int i = 0; i < n; ++i) { deck.mainboard.push_back(Make(name)); }
}

}   // namespace

TEST_CASE("vial target is derived from the decklist, not from a profile")
{
    EnsureCardsLoaded();

    SUBCASE("a deck with no Aether Vial has no target")
    {
        Decklist deck;
        Add(deck, "Venerable Knight", 4);
        Add(deck, "Knight Exemplar", 4);
        Add(deck, "Plains", 20);
        CHECK(GoldFishRunner::DeckVialTargetMv(deck) == 0);
    }

    SUBCASE("a Vial deck targets its most common creature mana value")
    {
        Decklist deck;
        Add(deck, "Aether Vial", 4);
        Add(deck, "Venerable Knight", 4);    // MV 1
        Add(deck, "Knight Exemplar", 6);     // MV 3  <- dominant
        Add(deck, "Benalish Marshal", 2);    // MV 3
        Add(deck, "Plains", 20);
        CHECK(GoldFishRunner::DeckVialTargetMv(deck) == 3);
    }

    SUBCASE("ties break toward the HIGHER mana value")
    {
        // Goblins ships exactly this shape (nine 2-drops, nine 3-drops) and its committed profile
        // records 3, so the tie-break is load-bearing on a real deck rather than a nicety.
        Decklist deck;
        Add(deck, "Aether Vial", 4);
        Add(deck, "Worthy Knight", 5);       // MV 2
        Add(deck, "Knight Exemplar", 5);     // MV 3
        Add(deck, "Plains", 20);
        CHECK(GoldFishRunner::DeckVialTargetMv(deck) == 3);
    }

    SUBCASE("SetupGame stamps it with NO profile anywhere in sight")
    {
        // This is the regression that matters. Every caller of SetupGame used to overwrite the
        // field from a MulliganProfile immediately afterwards; a profile belonging to a different
        // decklist, or one predating the key, wrote 0 straight over the truth. The profile can no
        // longer carry the field at all, so a game set up from the decklist alone must be correct.
        Decklist deck;
        Add(deck, "Aether Vial", 4);
        Add(deck, "Knight Exemplar", 12);
        Add(deck, "Venerable Knight", 4);
        Add(deck, "Plains", 20);
        GameState state = GoldFishRunner::SetupGame(deck, 12345);
        CHECK(state.vial_target_mv == 3);
    }
}

TEST_CASE("the Vial charge policy does not need a known target")
{
    EnsureCardsLoaded();

    // A hand holding a creature ABOVE the current counter is reason enough to tick: the counter
    // only climbs, so a 0-counter Vial that never ticks can deploy only mana-value-0 creatures --
    // a card type no deck in this repo runs. This used to return false whenever the target was
    // unset, which is precisely how the card became a blank.
    GameState state;
    state.active_player_index = 0;
    state.vial_target_mv      = 0;          // unset, the poisoned case
    state.players[0].hand.push_back(Make("Knight Exemplar"));   // MV 3

    Permanent vial;
    vial.card             = Make("Aether Vial");
    vial.controller_index = 0;
    vial.charge_counters  = 0;

    CHECK(WantVialCharge(state, vial) == true);

    SUBCASE("but it still will not climb blind with an empty hand")
    {
        // The speculative pre-charge is the one clause that genuinely needs a target. With none
        // known there is nothing to climb toward, so holding is correct -- and, unlike the old
        // early return, this affects ONLY the no-creature-in-hand case.
        state.players[0].hand.clear();
        CHECK(WantVialCharge(state, vial) == false);

        state.vial_target_mv = 2;
        CHECK(WantVialCharge(state, vial) == true);
    }
}
