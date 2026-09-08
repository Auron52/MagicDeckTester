// Unit tests for the CritterLifegain onboarding (2026-09-08): the shared GainLife hook and its
// "whenever you gain life" watchers, the per-EVENT semantics every one of the deck's payoffs rides
// on, and the two orderings that are easy to get silently wrong:
//
//  1. ONE trigger per life-gain EVENT (CR 119.10), never per point of life and never summed across
//     sources: two Soul Wardens on one creature entering = two Ajani's Pridemate counters; a single
//     gain of 5 = one counter; a gain of 0 = no counter at all.
//  2. A Pridemate that JUST entered collects the counters from its own entry's Warden gains (it is
//     on the battlefield when those triggers resolve).
//  3. Archangel of Thune's team pump lands on every own creature, Thune included, and never on the
//     opponent's; Heliod's counter goes on exactly ONE own permanent.
//  4. Combat lifelink is deferred to after the damage loop (CR 510.2): Thune + a 1/1 attacking with
//     a Soul Warden out deals 4 this combat (not 5), and the 1/1 is 2/2 afterwards.
//  5. Daxos: toughness = devotion to white (own pips count); "another creature you control dies"
//     gains 1 (one event) -- via the shared sacrifice helper, which every death site funnels through.
//  6. Heliod is not a creature below devotion 5 and becomes one, without "entering", when a later
//     permanent lifts devotion to 5; Serra Ascendant is 1/1 below 30 life and 6/6 at 30.
#include <doctest/doctest.h>

#include "cards/CardDatabase.h"
#include "core/GameState.h"
#include "core/SpellEffects.h"
#include "core/HeuristicDefaults.h"
#include "ai/Combat.h"

#include <string>

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

// A permanent that has been under our control since before this turn (attack-eligible).
int Put(GameState& s, const std::string& name, int controller, int number, bool sick = false)
{
    Permanent p;
    p.card              = Def(name).card;
    p.card.m_number     = number;
    p.controller_index  = controller;
    p.owner_index       = controller;
    p.entered_this_turn = sick;
    s.battlefield.push_back(p);
    RefreshDevotionCreatures(s);
    return static_cast<int>(s.battlefield.size()) - 1;
}

// Route an enter through the SAME universal cascade both worlds use.
int Enter(GameState& s, const std::string& name, int controller, int number)
{
    const int idx = Put(s, name, controller, number, /*sick=*/true);
    FireEtbWatchers(s, controller, idx);
    return idx;
}

int PlusCounters(const Permanent& p)
{
    int n = 0;
    for (const Counter& c : p.counters) { if (c.type == Counter::Type::PlusOnePlusOne) { n += c.count; } }
    return n;
}

const Permanent& ByNumber(const GameState& s, int number)
{
    for (const Permanent& p : s.battlefield) { if (p.card.m_number == number) { return p; } }
    static Permanent none;
    REQUIRE_MESSAGE(false, "permanent not found: ", number);
    return none;
}

GameState Fresh()
{
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 4;
    s.players[0].life     = 20;
    s.players[1].life     = 20;
    return s;
}

}   // namespace

TEST_CASE("GainLife: one watcher trigger per EVENT, never per point; zero is no event")
{
    EnsureCardsLoaded();
    GameState s = Fresh();
    const int pm = Put(s, "Ajani's Pridemate", 0, 10);
    GainLife(s, 0, 5);
    CHECK(s.players[0].life == 25);
    CHECK(s.players[0].life_gained_this_turn == 5);
    CHECK(PlusCounters(s.battlefield[pm]) == 1);   // one event of 5 = one counter
    GainLife(s, 0, 0);
    CHECK(PlusCounters(s.battlefield[pm]) == 1);   // zero gain: no event
    GainLife(s, 1, 3);                              // the OPPONENT gaining is not "you gain"
    CHECK(PlusCounters(s.battlefield[pm]) == 1);
    CHECK(s.players[1].life == 23);
}

TEST_CASE("Enter-watchers: 2 Wardens + 2 Attendants on one creature entering = 4 events = 4 counters")
{
    EnsureCardsLoaded();
    GameState s = Fresh();
    Put(s, "Soul Warden", 0, 1);
    Put(s, "Soul Warden", 0, 2);
    Put(s, "Soul's Attendant", 0, 3);
    Put(s, "Soul's Attendant", 0, 4);
    const int pm = Put(s, "Ajani's Pridemate", 0, 10);
    Enter(s, "Auriok Champion", 0, 20);
    CHECK(s.players[0].life == 24);
    CHECK(PlusCounters(s.battlefield[pm]) == 4);
    // The Champion itself is a watcher too: a fifth body now makes FIVE events.
    Enter(s, "Serra Ascendant", 0, 21);
    CHECK(s.players[0].life == 29);
    CHECK(PlusCounters(s.battlefield[pm]) == 9);
}

TEST_CASE("A Pridemate entering into two Wardens collects its own entry's counters (3 -> 4/4)")
{
    EnsureCardsLoaded();
    GameState s = Fresh();
    Put(s, "Soul Warden", 0, 1);
    Put(s, "Soul Warden", 0, 2);
    const int pm = Enter(s, "Ajani's Pridemate", 0, 10);
    CHECK(PlusCounters(s.battlefield[pm]) == 2);
    CHECK(s.battlefield[pm].EffectivePower() == 4);
    CHECK(s.battlefield[pm].EffectiveToughness() == 4);
    // The opponent's spawn entering fires the Wardens ("another creature", either side) too.
    Enter(s, "Goblin Guide", 1, 100001);
    CHECK(PlusCounters(s.battlefield[pm]) == 4);
}

TEST_CASE("Archangel of Thune: every own creature incl. itself, never the opponent's; Heliod: one target")
{
    EnsureCardsLoaded();
    GameState s = Fresh();
    const int th = Put(s, "Archangel of Thune", 0, 5);
    const int sw = Put(s, "Soul Warden", 0, 1);
    const int op = Put(s, "Goblin Guide", 1, 100001);
    // Heliod as a non-creature enchantment (devotion: Thune 2 + Warden 1 + Heliod 1 = 4 < 5).
    const int he = Put(s, "Heliod, Sun-Crowned", 0, 7);
    CHECK_FALSE(s.battlefield[he].card.IsCreature());
    GainLife(s, 0, 1);
    CHECK(PlusCounters(s.battlefield[th]) == 1 + 1);   // team pump + Heliod's single counter lands on the biggest attacker
    CHECK(PlusCounters(s.battlefield[sw]) == 1);
    CHECK(PlusCounters(s.battlefield[op]) == 0);
    CHECK(PlusCounters(s.battlefield[he]) == 0);         // never a non-creature while a creature exists
}

TEST_CASE("Combat lifelink is one event per attacker, applied AFTER the damage loop (CR 510.2)")
{
    EnsureCardsLoaded();
    GameState s = Fresh();
    Put(s, "Soul Warden", 0, 1);            // 1/1, attacks
    Put(s, "Archangel of Thune", 0, 5);     // 3/4 lifelink, attacks
    Put(s, "Ajani's Pridemate", 0, 10);     // 2/2, attacks
    std::vector<int> atk;
    for (int i = 0; i < static_cast<int>(s.battlefield.size()); ++i) { atk.push_back(i); }
    const CombatDamageResult r = ResolveCombatDamage(s, atk, 0, false);
    // Thune's gain must not pump the Warden or the Pridemate inside this same damage step.
    CHECK(r.total_damage == 1 + 3 + 2);
    CHECK(s.players[1].life == 14);
    CHECK(s.players[0].life == 23);
    // After the step: one lifelink event -> +1/+1 on everyone (Thune) and +1 on the Pridemate.
    CHECK(PlusCounters(ByNumber(s, 1))  == 1);
    CHECK(PlusCounters(ByNumber(s, 5))  == 1);
    CHECK(PlusCounters(ByNumber(s, 10)) == 2);
}

TEST_CASE("Two lifelink attackers = two events (Serra Ascendant + Thune)")
{
    EnsureCardsLoaded();
    GameState s = Fresh();
    Put(s, "Serra Ascendant", 0, 2);        // 1/1 lifelink
    Put(s, "Archangel of Thune", 0, 5);     // 3/4 lifelink
    Put(s, "Ajani's Pridemate", 0, 10);
    std::vector<int> atk{0, 1, 2};
    ResolveCombatDamage(s, atk, 0, false);
    CHECK(s.players[0].life == 24);
    CHECK(PlusCounters(ByNumber(s, 10)) == 2 + 2);   // 2 team pumps + 2 self counters
}

TEST_CASE("Daxos: toughness = devotion to white; another own creature dying gains 1 (one event)")
{
    EnsureCardsLoaded();
    GameState s = Fresh();
    const int dx = Put(s, "Daxos, Blessed by the Sun", 0, 3);
    const CardDefinition& dd = Def("Daxos, Blessed by the Sun");
    CHECK(DynamicBaseToughness(dd, s, 0) == 2);      // its own {W}{W}
    Put(s, "Auriok Champion", 0, 4);                 // {W}{W}
    CHECK(DynamicBaseToughness(dd, s, 0) == 4);
    const int pm = Put(s, "Ajani's Pridemate", 0, 10);
    const int rc = Put(s, "Ranger-Captain of Eos", 0, 11);
    (void)rc;
    const int life0 = s.players[0].life;
    ApplySacCreatureOutlet(s, 0, 11, 11);            // "Sacrifice this creature": self is the only victim
    CHECK(s.players[0].life == life0 + 1);
    CHECK(PlusCounters(ByNumber(s, 10)) == 1);
    bool captain_gone = true;
    for (const Permanent& p : s.battlefield) { if (p.card.m_number == 11) { captain_gone = false; } }
    CHECK(captain_gone);
    CHECK(s.battlefield[dx].card.IsCreature());
    (void)pm;
}

TEST_CASE("Heliod: not a creature below devotion 5; turns on when a later permanent lifts devotion")
{
    EnsureCardsLoaded();
    GameState s = Fresh();
    Put(s, "Soul Warden", 0, 1);                                  // W = 1
    const int he = Enter(s, "Heliod, Sun-Crowned", 0, 7);         // +1 -> 2: not a creature
    CHECK_FALSE(s.battlefield[he].card.IsCreature());
    CHECK(s.players[0].life == 20);                               // entered as a non-creature: no Warden trigger
    Enter(s, "Auriok Champion", 0, 4);                            // +2 -> 4; the Warden fires (21)
    CHECK_FALSE(s.battlefield[he].card.IsCreature());
    CHECK(s.players[0].life == 21);
    Enter(s, "Voice of the Blessed", 0, 5);                       // +2 -> 6: Heliod is now a creature
    CHECK(s.battlefield[he].card.IsCreature());
    // Heliod did not "enter": the Warden + Champion fired for the Voice only (2 events -> 23).
    CHECK(s.players[0].life == 23);
    // A Heliod entering INTO devotion >= 5 is a creature as it enters and fires the Wardens.
    const int he2 = Enter(s, "Heliod, Sun-Crowned", 0, 8);
    CHECK(s.battlefield[he2].card.IsCreature());
    CHECK(s.players[0].life == 25);
}

TEST_CASE("Serra Ascendant: +5/+5 only at 30 or more life, via ComputeLordBonus")
{
    EnsureCardsLoaded();
    GameState s = Fresh();
    const int sa = Put(s, "Serra Ascendant", 0, 2);
    auto bonus = [&]{ return ComputeLordBonus(s.battlefield[sa].card, s, 0); };
    CHECK(bonus().first == 0);
    s.players[0].life = 29;
    CHECK(bonus().first == 0);
    s.players[0].life = 30;
    CHECK(bonus().first == 5);
    CHECK(bonus().second == 5);
}

TEST_CASE("Heliod's {1}{W}: grants until-EOT lifelink to another creature; counters merge into one entry")
{
    EnsureCardsLoaded();
    GameState s = Fresh();
    Put(s, "Heliod, Sun-Crowned", 0, 7);
    const int pm = Put(s, "Ajani's Pridemate", 0, 10);
    ApplyPermAbility(s, 0, 7, PermAbilityMode::GrantLifelink);
    CHECK(s.battlefield[pm].temp_lifelink);
    CHECK(CreatureHasLifelink(s.battlefield[pm], s));
    GainLife(s, 0, 1);
    GainLife(s, 0, 1);
    GainLife(s, 0, 1);
    CHECK(PlusCounters(s.battlefield[pm]) == 3 + 3);   // 3 self + 3 Heliod counters (only creature)
    CHECK(s.battlefield[pm].counters.size() == 1);     // merged, not one entry per event
}

TEST_CASE("Voice of the Blessed: flying+vigilance from the 4th +1/+1 counter, indestructible from the 10th; both drop with the counters")
{
    EnsureCardsLoaded();
    GameState s = Fresh();
    const int v = Put(s, "Voice of the Blessed", 0, 10);
    CHECK_FALSE(s.battlefield[v].card.HasKeyword(Keyword::Vigilance));
    AddPlusCounters(s.battlefield[v], 3);
    CHECK_FALSE(s.battlefield[v].card.HasKeyword(Keyword::Flying));
    CHECK_FALSE(s.battlefield[v].card.HasKeyword(Keyword::Vigilance));
    AddPlusCounters(s.battlefield[v], 1);                    // 4
    CHECK(s.battlefield[v].card.HasKeyword(Keyword::Flying));
    CHECK(s.battlefield[v].card.HasKeyword(Keyword::Vigilance));
    CHECK_FALSE(s.battlefield[v].card.HasKeyword(Keyword::Indestructible));
    AddPlusCounters(s.battlefield[v], 6);                    // 10
    CHECK(s.battlefield[v].card.HasKeyword(Keyword::Indestructible));
    // Continuously checked (CR 611.3): seven -1/-1 counters annihilate down to 3 -> all gone.
    s.battlefield[v].counters.push_back(Counter{Counter::Type::MinusOneMinusOne, 7});
    AnnihilateCounters(s.battlefield[v]);
    CHECK(PlusCounters(s.battlefield[v]) == 3);
    CHECK_FALSE(s.battlefield[v].card.HasKeyword(Keyword::Flying));
    CHECK_FALSE(s.battlefield[v].card.HasKeyword(Keyword::Vigilance));
    CHECK_FALSE(s.battlefield[v].card.HasKeyword(Keyword::Indestructible));
    // The grant lives on the permanent's copy only: the definition is untouched.
    CHECK_FALSE(Def("Voice of the Blessed").card.HasKeyword(Keyword::Vigilance));
    // The lifegain path is the real counter source: 4 events -> the 4th turns the keywords on.
    GameState t = Fresh();
    const int v2 = Put(t, "Voice of the Blessed", 0, 11);
    for (int i = 0; i < 4; ++i) { GainLife(t, 0, 1); }
    CHECK(t.battlefield[v2].card.HasKeyword(Keyword::Vigilance));
}

TEST_CASE("Legend rule: a second Heliod dying as a CREATURE is a death -- Daxos gains 1, the survivor drops off")
{
    EnsureCardsLoaded();
    GameState s = Fresh();
    Put(s, "Daxos, Blessed by the Sun", 0, 1);       // {W}{W}: devotion 2
    Put(s, "Ajani's Pridemate", 0, 2);               // {1}{W}: 3
    const int h1 = Put(s, "Heliod, Sun-Crowned", 0, 3);   // {2}{W}: 4
    CHECK_FALSE(s.battlefield[h1].card.IsCreature());
    Put(s, "Heliod, Sun-Crowned", 0, 4);             // 5: BOTH Heliods are creatures now
    CHECK(s.battlefield[h1].card.IsCreature());
    CHECK(s.battlefield[3].card.IsCreature());
    const int pridemate_before = PlusCounters(s.battlefield[1]);
    EnforceLegendRule(s, 0);
    int heliods = 0;
    for (const Permanent& p : s.battlefield) { if (p.card.m_name == "Heliod, Sun-Crowned") { ++heliods; } }
    CHECK(heliods == 1);
    // Daxos: "whenever another creature you control dies, you gain 1 life" -- ONE event, seen by
    // TWO watchers: the Pridemate's own counter and the surviving Heliod's target counter (a God's
    // abilities work whether or not it is a creature), which the default target lands on the same
    // Pridemate (the only attack-eligible creature). So +2 counters from +1 life.
    CHECK(s.players[0].life == 21);
    CHECK(PlusCounters(s.battlefield[1]) == pridemate_before + 2);
    // Devotion fell back to 4: the surviving Heliod is no longer a creature.
    CHECK_FALSE(ByNumber(s, 3).card.IsCreature());
    // A doomed NON-creature Heliod (devotion < 5 for both) is not a creature death: no gain.
    GameState u = Fresh();
    Put(u, "Daxos, Blessed by the Sun", 0, 1);
    Put(u, "Heliod, Sun-Crowned", 0, 3);
    Put(u, "Heliod, Sun-Crowned", 0, 4);             // devotion 4
    EnforceLegendRule(u, 0);
    CHECK(u.players[0].life == 20);
}
