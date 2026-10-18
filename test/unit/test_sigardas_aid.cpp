// Unit tests for Sigarda's Aid's host on the Colossus Hammer list -- {W} Enchantment, "Whenever an
// Equipment you control enters, you may attach it to target creature you control."
// (CardParams::attach_equipment_on_etb).
//
// WHICH creature is ONE provider pick (DecisionProvider::EquipmentEtbAttachHost), and on this list it is
// ColossusHammerProvider's USER-ruled heuristic (2026-10-17: "Prefer doublestrike and creatures without
// summoning sickness." / "And just drop it on something." / "It can be done heuristically." / "Lizard
// Blades is also a creature replacement as needed."): double-strike potential, then can-attack-this-
// turn, then resulting power, then card number; no decline except Lizard Blades itself entering onto
// nothing that gains double strike from it. The base rule (every other deck) is untouched.
//
// WHY THESE ARE UNIT TESTS. The base scorer ties a summoning-sick double striker with a ready one and
// takes the lower card number, so the claude-play sweep's seed 2100011 T4 put the Hammer on the sick
// Lizard Blades and dealt 2 instead of 22. Nothing crashes or diverges when a host pick is wrong.
#include <doctest/doctest.h>

#include "ai/AIEngine.h"
#include "ai/DecisionProviders.h"
#include "ai/HeuristicArm.h"
#include "ai/TurnSolver.h"
#include "cards/CardDatabase.h"
#include "core/EffectHandler.h"
#include "core/GameSetup.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"

#include <string>
#include <vector>

namespace
{

void EnsureCardsLoaded()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(
            ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
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

const ColossusHammerProvider& Hammer()
{
    static const ColossusHammerProvider p;
    return p;
}

GameState Fresh()
{
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 4;
    s.players[0].life     = gamesetup::StartingLife();
    s.players[1].life     = gamesetup::StartingLife();
    s.players[0].lands_played_this_turn = 1;   // land drop spent: no land-fold plan variants
    for (int k = 0; k < 20; ++k)
    {
        Card c = Def("Plains").card;
        c.m_number = 900 + k;
        s.players[0].library.push_back(c);
    }
    return s;
}

int Put(GameState& s, const std::string& name, int number, bool sick = false)
{
    Permanent p;
    p.card              = Def(name).card;
    p.card.m_number     = number;
    p.controller_index  = 0;
    p.owner_index       = 0;
    p.entered_this_turn = sick;
    s.battlefield.push_back(p);
    return number;
}

int IndexOf(const GameState& s, int number)
{
    for (int i = 0; i < static_cast<int>(s.battlefield.size()); ++i)
    { if (s.battlefield[static_cast<std::size_t>(i)].card.m_number == number) { return i; } }
    return -1;
}

const Permanent* FindNum(const GameState& s, int number)
{
    for (const Permanent& p : s.battlefield) { if (p.card.m_number == number) { return &p; } }
    return nullptr;
}

// The provider's pick for `piece` entering, over these candidate creatures (the caller's legal set).
int Pick(const DecisionProvider& prov, const GameState& s, const std::string& piece, int piece_num,
         const std::vector<int>& host_nums)
{
    Card c = Def(piece).card;
    c.m_number = piece_num;
    std::vector<int> idx;
    for (int n : host_nums) { idx.push_back(IndexOf(s, n)); }
    return prov.EquipmentEtbAttachHost(s, 0, c, idx);
}

// GameEngine::MainPhase's shape: every cast resolves before the next.
void ResolveAll(GameState& st)
{
    while (!st.stack.empty())
    {
        const StackEntry e = st.stack.back();
        st.stack.pop_back();
        const CardDefinition* d = CardDatabase::Instance().LookupCached(e.source);
        if (d != nullptr) { EffectHandler::Resolve(st, e, *d); }
    }
}

}   // namespace

// ---- 1. the seed-2100011 shape ----------------------------------------------------------------

TEST_CASE("Sigarda's Aid (Colossus): seed 2100011 -- the Hammer goes onto the READY Vindicator, not the sick Lizard")
{
    EnsureCardsLoaded();
    GameState s = Fresh();
    const int lizard = Put(s, "Lizard Blades", 19, /*sick=*/true);
    const int swift  = Put(s, "Swiftblade Vindicator", 57);
    CHECK(Pick(Hammer(), s, "Colossus Hammer", 6, { lizard, swift }) == swift);
    // The base rule -- every other deck, KittyEquipment included -- is untouched: the tie still goes to
    // the lower card number.
    CHECK(Pick(DefaultProvider(), s, "Colossus Hammer", 6, { lizard, swift }) == lizard);
}

TEST_CASE("Sigarda's Aid (Colossus): seed 2100011 end to end -- the real turn swings for 22")
{
    EnsureCardsLoaded();
    GameState s = Fresh();
    s.m_provider = &Hammer();
    Put(s, "Sigarda's Aid", 48);
    const int lizard = Put(s, "Lizard Blades", 19, /*sick=*/true);
    const int swift  = Put(s, "Swiftblade Vindicator", 57);
    Put(s, "Mountain", 29);
    Card h = Def("Colossus Hammer").card;
    h.m_number = 6;
    s.players[0].hand.push_back(h);
    AIEngine eng(MulliganProfile::DefaultProfile(), 2, 20);
    eng.TakeTurn(s, /*is_pre_combat_main=*/true, ResolveAll);
    const Permanent* hp = FindNum(s, 6);
    REQUIRE(hp != nullptr);
    CHECK(hp->equipped_to == swift);
    CHECK(hp->equipped_to != lizard);
    CHECK(BeginCombatProjectedDamage(s, 0) == 22);
}

// ---- 2. the ranking -----------------------------------------------------------------------------

TEST_CASE("Sigarda's Aid (Colossus): double-strike potential outranks readiness, readiness outranks power")
{
    EnsureCardsLoaded();
    {   // a SICK double striker beats a READY non-double-striker (tier 1 before tier 2)
        GameState s = Fresh();
        const int swift   = Put(s, "Swiftblade Vindicator", 5, /*sick=*/true);
        const int raubahn = Put(s, "Raubahn, Bull of Ala Mhigo", 3);
        CHECK(Pick(Hammer(), s, "Colossus Hammer", 60, { raubahn, swift }) == swift);
    }
    {   // a bare Kor Duelist has double-strike potential (the attach itself equips it)
        GameState s = Fresh();
        const int ardenn  = Put(s, "Ardenn, Intrepid Archaeologist", 2);
        const int duelist = Put(s, "Kor Duelist", 7);
        CHECK(Pick(Hammer(), s, "Colossus Hammer", 60, { ardenn, duelist }) == duelist);
    }
    {   // a creature WEARING Lizard Blades has double strike: it outranks a ready plain creature
        GameState s = Fresh();
        const int ardenn  = Put(s, "Ardenn, Intrepid Archaeologist", 2);
        const int raubahn = Put(s, "Raubahn, Bull of Ala Mhigo", 9);
        const int lizard  = Put(s, "Lizard Blades", 11);
        ApplyEquip(s, 0, lizard, raubahn);
        REQUIRE(CreatureHasDoubleStrike(*FindNum(s, raubahn), s));
        CHECK(Pick(Hammer(), s, "Colossus Hammer", 60, { ardenn, raubahn }) == raubahn);
    }
    {   // both double strikers and both ready: the one already wearing the Hammer (consolidation)
        GameState s = Fresh();
        const int swift_a = Put(s, "Swiftblade Vindicator", 5);
        const int swift_b = Put(s, "Swiftblade Vindicator", 6);
        const int ham     = Put(s, "Colossus Hammer", 61);
        ApplyEquip(s, 0, ham, swift_b);
        CHECK(Pick(Hammer(), s, "Colossus Hammer", 62, { swift_a, swift_b }) == swift_b);
    }
    {   // a full tie: the lower card number (deterministic)
        GameState s = Fresh();
        const int swift_a = Put(s, "Swiftblade Vindicator", 8);
        const int swift_b = Put(s, "Swiftblade Vindicator", 4);
        CHECK(Pick(Hammer(), s, "Colossus Hammer", 62, { swift_a, swift_b }) == swift_b);
    }
}

TEST_CASE("Sigarda's Aid (Colossus): \"just drop it on something\" -- no decline for an ordinary Equipment")
{
    EnsureCardsLoaded();
    GameState s = Fresh();
    const int raubahn = Put(s, "Raubahn, Bull of Ala Mhigo", 3, /*sick=*/true);
    CHECK(Pick(Hammer(), s, "Dragonfire Blade", 60, { raubahn }) == raubahn);
    CHECK(Pick(Hammer(), s, "Loxodon Warhammer", 61, { raubahn }) == raubahn);
    // ...and it only ever picks from the caller's set: no candidate, no attach.
    CHECK(Pick(Hammer(), s, "Colossus Hammer", 62, {}) == 0);
}

// ---- 3. Lizard Blades: "a creature replacement as needed" --------------------------------------

TEST_CASE("Sigarda's Aid (Colossus): an entering Lizard Blades attaches only onto a host that GAINS double strike")
{
    EnsureCardsLoaded();
    {   // every candidate already double-strikes: stay a 1/1 double-strike creature
        GameState s = Fresh();
        const int swift = Put(s, "Swiftblade Vindicator", 5);
        CHECK(Pick(Hammer(), s, "Lizard Blades", 19, { swift }) == 0);
    }
    {   // two hosts that gain it: the READY one
        GameState s = Fresh();
        const int raubahn = Put(s, "Raubahn, Bull of Ala Mhigo", 3, /*sick=*/true);
        const int ardenn  = Put(s, "Ardenn, Intrepid Archaeologist", 9);
        CHECK(Pick(Hammer(), s, "Lizard Blades", 19, { raubahn, ardenn }) == ardenn);
    }
    {   // a non-double-striker beside a double striker: onto the one that gains it
        GameState s = Fresh();
        const int swift  = Put(s, "Swiftblade Vindicator", 2);
        const int ardenn = Put(s, "Ardenn, Intrepid Archaeologist", 9);
        CHECK(Pick(Hammer(), s, "Lizard Blades", 19, { swift, ardenn }) == ardenn);
    }
    {   // a LOOSE Lizard Blades is an ordinary host candidate: double strike, sick if new
        GameState s = Fresh();
        const int lizard  = Put(s, "Lizard Blades", 19);                 // not new: ready
        const int raubahn = Put(s, "Raubahn, Bull of Ala Mhigo", 3);
        CHECK(Pick(Hammer(), s, "Colossus Hammer", 60, { raubahn, lizard }) == lizard);
    }
}

TEST_CASE("Sigarda's Aid (Colossus): a READY host that makes this turn LETHAL beats a sick double striker (USER 2026-10-18)")
{
    // "Certainly the latter should be taken if it is lethal. Otherwise the former is okay."
    EnsureCardsLoaded();
    auto board = [](int opp_life)
    {
        GameState s = Fresh();
        s.players[1].life = opp_life;
        Put(s, "Swiftblade Vindicator", 5, /*sick=*/true);   // double strike, cannot attack this turn
        Put(s, "Speaker of the Heavens", 3);                 // 1/1, ready
        Put(s, "Colossus Hammer", 60);                       // the piece, entered (unattached)
        return s;
    };
    {   // Speaker + the Hammer = 11 this turn: lethal at 11 -> Speaker
        const GameState s = board(11);
        CHECK(TurnSolver::ProjectedAttackDamage(s) == 1);
        CHECK(Pick(Hammer(), s, "Colossus Hammer", 60, { 3, 5 }) == 3);
    }
    {   // not lethal at 12 -> the double striker, ready next turn (the ranking)
        const GameState s = board(12);
        CHECK(Pick(Hammer(), s, "Colossus Hammer", 60, { 3, 5 }) == 5);
    }
    {   // =0: the ranking alone, even when lethal
        heurarm::t_arm[heurarm::COLOSSUS_AID_LETHAL] = 0;
        const GameState s = board(11);
        CHECK(Pick(Hammer(), s, "Colossus Hammer", 60, { 3, 5 }) == 5);
        heurarm::t_arm[heurarm::COLOSSUS_AID_LETHAL] = -1;
    }
    {   // Raubahn's own attack trigger moves the Hammer onto himself, so the double striker's attach is
        // lethal already (12 at 12) -- the ranking's pick stands
        GameState s = Fresh();
        s.players[1].life = 12;
        Put(s, "Swiftblade Vindicator", 5, /*sick=*/true);
        Put(s, "Raubahn, Bull of Ala Mhigo", 3);
        Put(s, "Colossus Hammer", 60);
        CHECK(Pick(Hammer(), s, "Colossus Hammer", 60, { 3, 5 }) == 5);
    }
}
