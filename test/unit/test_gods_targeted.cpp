#include <doctest/doctest.h>
#include "core/SpellEffects.h"
#include "core/HeuristicDefaults.h"
#include "ai/TurnSolver.h"
#include "ai/ManaPayment.h"
#include "ai/DecisionProviders.h"
#include <algorithm>

namespace {
const CardDefinition* Def(const char* name) {
    static bool loaded = [] { CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json")); return true; }();
    (void)loaded;
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE(d != nullptr);
    return d;
}
Card Physical(const char* name, int n) { Card c = Def(name)->card; c.m_number=n; c.m_def=Def(name); return c; }
Permanent Board(const char* name, int n, int ctrl=0) { Permanent p; p.card=Physical(name,n); p.controller_index=ctrl; p.owner_index=ctrl; return p; }
}

TEST_CASE("Gods target group: Phenax mills physical cards into the chosen graveyard") {
    GameState s;
    s.battlefield.push_back(Board("Phenax, God of Deception",10));
    s.battlefield.push_back(Board("Stinkweed Imp",11));
    s.players[0].library.push_back(Physical("Forest",12));
    s.players[0].library.push_back(Physical("Forest",13));
    s.players[1].library.push_back(Physical("Forest",101));
    s.players[1].library.push_back(Physical("Forest",102));
    s.opponent_library_dealt=true;
    REQUIRE(PhenaxTapMillLive(s,0,11));
    CHECK(PhenaxMillAmount(s.battlefield[1],s)==2);
    REQUIRE(ApplyPhenaxTapMill(s,0,11,0));
    CHECK(s.players[0].graveyard.size()==2);
    CHECK(s.players[1].graveyard.empty());
    CHECK(s.battlefield[1].tapped);
    s.battlefield[1].tapped=false;
    REQUIRE(ApplyPhenaxTapMill(s,0,11,1));
    CHECK(s.players[1].graveyard.size()==2);
    CHECK(s.players[1].library.empty());
}

TEST_CASE("Gods target group: mass Phenax taps eligible Gods and mills only the opponent") {
    GameState s;
    s.battlefield.push_back(Board("Phenax, God of Deception", 110));
    s.battlefield.push_back(Board("Xenagos, God of Revels", 111));
    s.battlefield.push_back(Board("Stinkweed Imp", 112));
    for (int i = 0; i < 30; ++i)
    { s.players[1].library.push_back(Physical("Forest", 200 + i)); }
    s.players[0].library.push_back(Physical("Forest", 300));
    s.opponent_library_dealt = true;
    REQUIRE(ApplyPhenaxGodsMassMill(s, 0) == 2);
    CHECK(s.battlefield[0].tapped);
    CHECK(s.battlefield[1].tapped);
    CHECK_FALSE(s.battlefield[2].tapped);
    CHECK(s.players[0].graveyard.empty());
    CHECK(s.players[0].library.size() == 1);
    CHECK(s.players[1].graveyard.size() > 0);
}

TEST_CASE("Gods target group: mass Phenax is offered only when it beats combat") {
    static const GodsProvider gods;
    GameState s;
    s.m_provider = &gods;
    s.battlefield.push_back(Board("Phenax, God of Deception", 310));
    s.battlefield.push_back(Board("Xenagos, God of Revels", 311));
    for (int i = 0; i < 30; ++i)
    { s.players[1].library.push_back(Physical("Forest", 400 + i)); }
    s.players[1].life = 100;
    auto actions = TurnSolver::CollectActionsForTest(s, true);
    const auto count_mass = [](const std::vector<Action>& choices) {
        return std::count_if(choices.begin(), choices.end(), [](const Action& a) {
            return a.kind == Action::Kind::TapMillToughness && a.sac_source_id == -1;
        });
    };
    CHECK(count_mass(actions) == 1);
    s.players[1].life = 1;
    actions = TurnSolver::CollectActionsForTest(s, true);
    CHECK(count_mass(actions) == 0);
}

TEST_CASE("Gods target group: Pharika exiles exact graveyard cards and gives owner enchantment Snakes") {
    GameState s;
    s.battlefield.push_back(Board("Pharika, God of Affliction",20));
    s.players[0].graveyard.push_back(Physical("Stinkweed Imp",21));
    s.players[1].graveyard.push_back(Physical("Stinkweed Imp",101));
    REQUIRE(ApplyPharikaExile(s,0,20,1,101));
    CHECK(s.players[1].graveyard.empty());
    CHECK(s.exile.back().m_number==101);
    REQUIRE(s.battlefield.size()==2);
    CHECK(s.battlefield.back().controller_index==1);
    CHECK(s.battlefield.back().card.IsEnchantment());
    CHECK(s.battlefield.back().card.HasKeyword(Keyword::Deathtouch));
    REQUIRE(ApplyPharikaExile(s,0,20,0,21));
    CHECK(s.battlefield.back().controller_index==0);
    CHECK(s.players[0].graveyard.empty());
}

TEST_CASE("Gods target group: Bronze put works while tapped and sacrifices at end step") {
    GameState s;
    Permanent bronze=Board("Purphoros, Bronze-Blooded",30);
    bronze.tapped=true;
    bronze.entered_this_turn=true;
    s.battlefield.push_back(bronze);
    s.players[0].hand.push_back(Physical("Mogis, God of Slaughter",31));
    s.players[0].hand.push_back(Physical("Purphoros, God of the Forge",32));
    CHECK(PutFromHandLegalIndex(s,0,30,"Mogis, God of Slaughter")==0);
    REQUIRE(ApplyPutFromHand(s,0,30,"Mogis, God of Slaughter"));
    CHECK(s.battlefield[0].tapped);
    REQUIRE(s.battlefield.size()==2);
    CHECK(bool(s.battlefield[1].sac_at_end_step));
    REQUIRE(ApplyPutFromHand(s,0,30,"Purphoros, God of the Forge"));
    CHECK(s.battlefield.size()==3);
    CHECK(bool(s.battlefield[2].sac_at_end_step));
    PerformEndStepDelayedSacrifices(s);
    CHECK(s.battlefield.size()==1);
    CHECK(s.players[0].graveyard.size()==2);
}

TEST_CASE("Gods target group: Kruphix converts separate mana pools at boundaries") {
    GameState s;
    s.battlefield.push_back(Board("Kruphix, God of Horizons",40));
    s.floating_mana.red=2;
    s.floating_mana.snow_units=1;
    s.floating_creature_mana.green=1;
    s.floating_bigspell_mana.blue=3;
    s.floating_bigspell_pp=&Def("Magus of the Bazaar")->params;
    CrossManaBoundary(s,0);
    CHECK(s.floating_mana.colorless==2);
    CHECK(s.floating_mana.snow_units==1);
    CHECK(s.floating_creature_mana.colorless==1);
    CHECK(s.floating_bigspell_mana.colorless==3);
    CHECK(s.floating_bigspell_pp!=nullptr);
    s.battlefield.clear();
    CrossManaBoundary(s,0);
    CHECK(s.floating_mana.Total()==0);
    CHECK(s.floating_creature_mana.Total()==0);
    CHECK(s.floating_bigspell_mana.Total()==0);
    CHECK(s.floating_bigspell_pp==nullptr);
}

TEST_CASE("Gods target group: new Phenax grant and Pharika graveyard target reopen site nine") {
    GameState s;
    s.battlefield.push_back(Board("Stinkweed Imp",50));
    const auto before=TurnSolver::SnapshotActivatableAbilities(s);
    s.battlefield.push_back(Board("Phenax, God of Deception",51));
    CHECK(TurnSolver::PostEntryActivationPending(s,before));
    GameState p;
    p.battlefield.push_back(Board("Pharika, God of Affliction",60));
    p.battlefield.push_back(Board("City of Brass",61));
    p.battlefield.push_back(Board("City of Brass",62));
    const auto pre=TurnSolver::SnapshotActivatableAbilities(p);
    p.players[0].graveyard.push_back(Physical("Stinkweed Imp",63));
    CHECK(TurnSolver::PostEntryActivationPending(p,pre));
}
