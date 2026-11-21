// Blood Sun step 7 (design docs/design/blood-sun-pipeline.md §8, §12 step 7; ledger B7):
//   * the duplicate-legend prune offers a second Emrakul, the Aeons Torn in SEARCHED play (its own cast trigger is an
//     extra turn; its legend-rule death shuffles a non-empty graveyard back) and still refuses it at autonomous d0;
//   * fold F-E1: the LEGEND-KEEP pass emits only keep-oldest when the copies can differ only in age;
//   * Overgrown Tomb: under Blood Sun untapped, nothing paid, no choice; without it the pay / tapped answer is two
//     searched land options for an opted-in provider with own death live; at life 2 only tapped.
#include <doctest/doctest.h>

#include "ai/DecisionProviders.h"
#include "ai/LandPlay.h"
#include "ai/TurnSolver.h"
#include "cards/CardDatabase.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"

#include <algorithm>
#include <set>
#include <string>
#include <vector>

namespace
{
void EnsureCards7()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}
const CardDefinition& Def7(const std::string& name)
{
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE_MESSAGE(d != nullptr, "card not in cards.json: ", name);
    return *d;
}
Card Card7(const std::string& name, int number)
{
    Card c = Def7(name).card;
    c.m_number = number;
    return c;
}
Permanent& Put7(GameState& s, const std::string& name, int number, bool tapped = false)
{
    Permanent p;
    p.card = Card7(name, number);
    p.controller_index = 0;
    p.owner_index = 0;
    p.tapped = tapped;
    s.battlefield.push_back(p);
    return s.battlefield.back();
}
const BloodSunProvider& Bs7()
{
    static const BloodSunProvider bp;
    return bp;
}
const GenericProvider& Gen7()
{
    static const GenericProvider gp;
    return gp;
}
GameState Base7(const DecisionProvider& prov)
{
    EnsureCards7();
    GameState s;
    s.m_provider          = &prov;
    s.active_player_index = 0;
    s.turn_number         = 6;
    s.phase               = Phase::PreCombatMain;
    s.players[0].life     = 20;
    s.players[1].life     = 20;
    s.deck_has_landfall   = false;
    s.deck_has_revolt     = false;
    s.deck_has_land_ability_loss = true;
    s.own_death_live      = true;
    for (int k = 0; k < 10; ++k) { s.players[0].library.push_back(Card7("Swamp", 900 + k)); }
    return s;
}
struct Searched7
{
    bool saved = g_searched_play;
    explicit Searched7(bool on) { TurnSolver::SetSearchedPlay(on); }
    ~Searched7() { TurnSolver::SetSearchedPlay(saved); }
};
constexpr const char* kEmrakul = "Emrakul, the Aeons Torn";
constexpr const char* kGyre    = "Ulamog, the Infinite Gyre";
constexpr const char* kTomb    = "Overgrown Tomb";

// Emrakul #1 (un-sick, untapped) on the battlefield, Emrakul #2 in hand, `lands` untapped Swamps.
GameState EmrakulBoard(const DecisionProvider& prov, int lands = 15)
{
    GameState s = Base7(prov);
    for (int k = 0; k < lands; ++k) { Put7(s, "Swamp", 100 + k); }
    Put7(s, kEmrakul, 10);
    s.players[0].hand.push_back(Card7(kEmrakul, 20));
    return s;
}
std::vector<int> EmrakulKeeps(const GameState& s)
{
    std::vector<int> out;
    for (const Action& a : TurnSolver::CollectActionsForTest(s, /*is_pre_combat=*/true))
    { if (a.kind == Action::Kind::CastFromHand && a.card_name.str() == kEmrakul) { out.push_back(a.legend_keep); } }
    std::sort(out.begin(), out.end());
    return out;
}
// The rad_mode (land-entry pin) of every enumerated plan that plays `land`.
std::set<int> LandPins(const GameState& s, const char* land)
{
    std::set<int> out;
    for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(s, /*is_pre_combat=*/true))
    { if (p.land_to_play == land) { out.insert(p.rad_mode); } }
    return out;
}
}   // namespace

TEST_CASE("Blood Sun step 7: a second Emrakul is offered in searched play (its cast trigger), refused at d0")
{
    for (const DecisionProvider* prov : { static_cast<const DecisionProvider*>(&Bs7()),
                                          static_cast<const DecisionProvider*>(&Gen7()) })
    {
        const GameState s = EmrakulBoard(*prov);
        REQUIRE(s.players[0].graveyard.empty());   // the cast trigger alone carries it
        {
            Searched7 on(true);
            CHECK(prov->OfferDuplicateLegendCast(s, 0, Def7(kEmrakul)));
            CHECK_FALSE(EmrakulKeeps(s).empty());
        }
        {
            Searched7 off(false);   // autonomous d0 keeps the prune (USER 2026-08-29 / 2026-11-18)
            CHECK_FALSE(prov->OfferDuplicateLegendCast(s, 0, Def7(kEmrakul)));
            CHECK(EmrakulKeeps(s).empty());
        }
    }
}

TEST_CASE("Blood Sun step 7: the graveyard-shuffle death is upside only with a card to shuffle back")
{
    // Ulamog, the Infinite Gyre: a vanilla_creature legend with the shuffle clause and NO extra-turn cast trigger.
    REQUIRE(Def7(kGyre).params.put_into_graveyard_shuffles_graveyard);
    REQUIRE(Def7(kGyre).params.cast_trigger_extra_turns == 0);
    GameState s = Base7(Gen7());
    for (int k = 0; k < 11; ++k) { Put7(s, "Swamp", 100 + k); }
    Put7(s, kGyre, 10);
    s.players[0].hand.push_back(Card7(kGyre, 20));
    Searched7 on(true);
    CHECK_FALSE(Gen7().OfferDuplicateLegendCast(s, 0, Def7(kGyre)));   // empty graveyard: the tie
    s.players[0].graveyard.push_back(Card7("Swamp", 300));
    CHECK(Gen7().OfferDuplicateLegendCast(s, 0, Def7(kGyre)));
    Searched7 off(false);
    CHECK_FALSE(Gen7().OfferDuplicateLegendCast(s, 0, Def7(kGyre)));   // d0 unchanged
}

TEST_CASE("Blood Sun step 7: fold F-E1 keeps only the older copy when the copies differ only in age")
{
    Searched7 on(true);
    {   // plain un-sick old copy, lands only beside it: keep-oldest alone
        const GameState s = EmrakulBoard(Bs7());
        CHECK(EmrakulKeeps(s) == std::vector<int>{ 1 });
    }
    {   // a TAPPED old copy differs from a fresh entrant: both keeps searched
        GameState s = EmrakulBoard(Bs7());
        for (Permanent& p : s.battlefield) { if (p.card.m_name.str() == kEmrakul) { p.tapped = true; } }
        CHECK(EmrakulKeeps(s) == std::vector<int>{ 1, 2 });
    }
    {   // a +1/+1 counter on the old copy: both keeps
        GameState s = EmrakulBoard(Bs7());
        for (Permanent& p : s.battlefield)
        { if (p.card.m_name.str() == kEmrakul) { p.counters.Add(Counter::Type::PlusOnePlusOne, 1); } }
        CHECK(EmrakulKeeps(s) == std::vector<int>{ 1, 2 });
    }
    {   // another creature of ours (a possible entrant-modifying watcher): fail closed, both keeps
        GameState s = EmrakulBoard(Bs7());
        Put7(s, "Monastery Swiftspear", 60);
        CHECK(EmrakulKeeps(s) == std::vector<int>{ 1, 2 });
    }
    {   // enchantments beside it (Blood Sun, Exploration, Fastbond): still the fold
        GameState s = EmrakulBoard(Bs7());
        Put7(s, "Blood Sun", 61);
        Put7(s, "Exploration", 62);
        Put7(s, "Fastbond", 63);
        CHECK(EmrakulKeeps(s) == std::vector<int>{ 1 });
    }
    {   // a creature in HAND the same plan could cast first (step 7 smoke, minotaur2hg gi61): fail closed, both keeps
        GameState s = EmrakulBoard(Bs7());
        s.players[0].hand.push_back(Card7("Monastery Swiftspear", 64));
        CHECK(EmrakulKeeps(s) == std::vector<int>{ 1, 2 });
    }
    {   // a noncreature, non-watcher card in hand (Time Warp) does not: still the fold
        GameState s = EmrakulBoard(Bs7());
        s.players[0].hand.push_back(Card7("Time Warp", 65));
        CHECK(EmrakulKeeps(s) == std::vector<int>{ 1 });
    }
}

TEST_CASE("Blood Sun step 7: Overgrown Tomb under Blood Sun enters untapped, pays nothing, asks nothing")
{
    GameState s = Base7(Bs7());
    Put7(s, "Blood Sun", 50);
    Put7(s, "Swamp", 51);
    s.players[0].hand.push_back(Card7(kTomb, 70));
    CHECK_FALSE(LandEntryHasChoice(s, Def7(kTomb)));     // no viewer land_entry prompt
    CHECK_FALSE(LandShockPayPinned(s, Def7(kTomb)));
    CHECK(LandPins(s, kTomb) == std::set<int>{ -1 });
    LandPlayOptions o;
    REQUIRE(PlayLandFromHand(s, 0, Def7(kTomb), o));
    const Permanent& t = s.battlefield.back();
    CHECK(t.card.m_name.str() == kTomb);
    CHECK_FALSE(t.tapped);
    CHECK(s.players[0].life == 20);
}

TEST_CASE("Blood Sun step 7: without Blood Sun the Tomb's pay / tapped answer is searched; at life 2 only tapped")
{
    {   // life 20: both answers are land options, and each realises its answer
        GameState s = Base7(Bs7());
        Put7(s, "Swamp", 51);
        s.players[0].hand.push_back(Card7(kTomb, 70));
        CHECK(LandShockPayPinned(s, Def7(kTomb)));
        CHECK(LandPins(s, kTomb) == std::set<int>{ 0, 1 });
        for (int pin : { 0, 1 })
        {
            GameState g = s;
            LandPlayOptions o;
            o.rad_mode = pin;
            REQUIRE(PlayLandFromHand(g, 0, Def7(kTomb), o));
            CHECK(g.battlefield.back().tapped == (pin == 0));
            CHECK(g.players[0].life == (pin == 0 ? 20 : 18));
        }
        GameState g = s;   // unpinned (beyond the horizon, d0): the engine rule pays
        REQUIRE(PlayLandFromHand(g, 0, Def7(kTomb), LandPlayOptions{}));
        CHECK_FALSE(g.battlefield.back().tapped);
        CHECK(g.players[0].life == 18);
    }
    {   // life 2: paying would kill us (B1) -- forced tapped, no pin, no prompt; a stray pin 1 cannot pay
        GameState s = Base7(Bs7());
        s.players[0].life = 2;
        Put7(s, "Swamp", 51);
        s.players[0].hand.push_back(Card7(kTomb, 70));
        CHECK_FALSE(LandShockPayPinned(s, Def7(kTomb)));
        CHECK_FALSE(LandEntryHasChoice(s, Def7(kTomb)));
        CHECK(LandPins(s, kTomb) == std::set<int>{ -1 });
        LandPlayOptions o;
        o.rad_mode = 1;
        REQUIRE(PlayLandFromHand(s, 0, Def7(kTomb), o));
        CHECK(s.battlefield.back().tapped);
        CHECK(s.players[0].life == 2);
    }
    {   // another provider (Anti-Lifegain / Creature Giving / Five Colour route elsewhere): never pinned
        GameState s = Base7(Gen7());
        Put7(s, "Swamp", 51);
        s.players[0].hand.push_back(Card7(kTomb, 70));
        CHECK_FALSE(LandShockPayPinned(s, Def7(kTomb)));
        CHECK(LandPins(s, kTomb) == std::set<int>{ -1 });
    }
    {   // own death not live: life is bookkeeping, paying is never worse -- not pinned even for the opted-in provider
        GameState s = Base7(Bs7());
        s.own_death_live = false;
        Put7(s, "Swamp", 51);
        s.players[0].hand.push_back(Card7(kTomb, 70));
        CHECK_FALSE(LandShockPayPinned(s, Def7(kTomb)));
    }
}

// Parent resolution 2 (step 7): a Tomb put onto our battlefield on the OPPONENT's turn (Burgeoning) untaps in our untap
// step before any sorcery-speed use, so with no instant / flash / mana-costed ability / untapped-lands consumer the pay
// is dominated: tapped, no life paid (ShockPayDominatedOffTurn). Fail-closed: any such use keeps the engine rule (pay).
TEST_CASE("Blood Sun step 7: a Tomb put on the opponent's turn enters tapped unless something could use it")
{
    auto put = [](GameState s) -> std::pair<bool, int>
    {
        s.active_player_index = 1;   // the opponent's turn; the land is ours (controller 0)
        EnterLand(s, Def7(kTomb), 70, /*resolve_full_etb=*/true, /*fire_landfall=*/true, LandEntryTap::Natural, 0);
        const Permanent& t = s.battlefield.back();
        REQUIRE(t.card.m_name.str() == kTomb);
        return { t.tapped, s.players[0].life };
    };
    auto with_hand = [](const DecisionProvider& prov, std::vector<const char*> hand) -> GameState
    {
        GameState s = Base7(prov);
        Put7(s, "Swamp", 51, /*tapped=*/true);
        Put7(s, "Burgeoning", 52);
        int n = 80;
        for (const char* h : hand) { s.players[0].hand.push_back(Card7(h, n++)); }
        return s;
    };
    // Nothing could use it this turn (a sorcery, a creature, a transmute -- sorcery speed only): tapped, 20 life.
    CHECK(put(with_hand(Bs7(), { "Time Warp", kEmrakul, "Dimir Machinations" })) == std::make_pair(true, 20));
    // An instant in hand could: the engine rule pays.
    CHECK(put(with_hand(Bs7(), { "Time Warp", "Lightning Bolt" })) == std::make_pair(false, 18));
    // A Lotus Vale in hand: a later put of the same turn sacrifices two UNTAPPED lands -- a use, pays.
    CHECK(put(with_hand(Bs7(), { "Lotus Vale" })) == std::make_pair(false, 18));
    // Our own turn (a land drop or our own put): not this fold (the drop's pay is the searched pin).
    {
        GameState s = with_hand(Bs7(), { "Time Warp" });
        CHECK_FALSE(ShockPayDominatedOffTurn(s, 0));
        s.active_player_index = 1;
        CHECK(ShockPayDominatedOffTurn(s, 0));
    }
    // Scope: another provider, or own death not live, keeps always-pay.
    CHECK(put(with_hand(Gen7(), { "Time Warp" })) == std::make_pair(false, 18));
    {
        GameState s = with_hand(Bs7(), { "Time Warp" });
        s.own_death_live = false;
        CHECK(put(s) == std::make_pair(false, 18));
    }
}
