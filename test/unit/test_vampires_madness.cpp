// Unit tests for the Vampires cluster A1 (2026-10-18; docs/design/vampires/design_A1_madness.md):
// MADNESS (the shared discard hook, Falkenrath Gorger's grant, the searched outlet pin, the cleanup rule),
// CONVOKE generalised to a creature spell (Markov Baron) and ASYLUM VISITOR's upkeep trigger.
//
// WHY THESE ARE UNIT TESTS. Every defect this cluster can have is quiet: a madness pin missing from the
// plan signature collapses cast and decline into one plan (an in-window greedy); a sacrificed Gorger that
// revokes a grant already made turns a cast into a graveyard trip; a madness payment run under the
// outlet's ABILITY scope cannot use Unclaimed Territory's colours; a madness-cast Baron that skips the
// shared enter path never meets Stromkirk Condemned's pump. None crashes; only a direct test sees them.
#include <doctest/doctest.h>

#include "ai/AIEngine.h"
#include "ai/DecisionProviders.h"
#include "ai/ManaPayment.h"
#include "ai/TurnSolver.h"
#include "cards/CardDatabase.h"
#include "core/EffectHandler.h"
#include "core/GameEngine.h"
#include "core/GameSetup.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"
#include "deck/DeckLoader.h"
#include "mtg_test_seam.h"

#include <algorithm>
#include <functional>
#include <set>
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

GameState Fresh()
{
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 6;
    s.phase               = Phase::PreCombatMain;
    s.players[0].life     = gamesetup::StartingLife();
    s.players[1].life     = gamesetup::StartingLife();
    s.players[0].lands_played_this_turn = 1;   // land drop spent unless a test opens it
    s.m_provider = &SelectDecisionProvider(DeckLoader::LoadFromFile(
        ResolveHeuristicDefaultsPath("decks/Vampires/Vampire.cod")));
    for (int k = 0; k < 20; ++k)
    {
        Card c = Def("Swamp").card;
        c.m_number = 900 + k;
        s.players[0].library.push_back(c);
    }
    return s;
}

int Put(GameState& s, const std::string& name, int number, bool sick = false, int ctrl = 0)
{
    Permanent p;
    p.card              = Def(name).card;
    p.card.m_number     = number;
    p.controller_index  = ctrl;
    p.owner_index       = ctrl;
    p.entered_this_turn = sick;
    s.battlefield.push_back(p);
    return number;
}

int Hand(GameState& s, const std::string& name, int number)
{
    Card c = Def(name).card;
    c.m_number = number;
    s.players[0].hand.push_back(c);
    return number;
}

int HandIdx(const GameState& s, int number)
{
    const auto& h = s.players[0].hand;
    for (int i = 0; i < static_cast<int>(h.size()); ++i) { if (h[static_cast<std::size_t>(i)].m_number == number) { return i; } }
    return -1;
}

const Permanent* FindNum(const GameState& s, int number)
{
    for (const Permanent& p : s.battlefield) { if (p.card.m_number == number) { return &p; } }
    return nullptr;
}

int CountIn(const std::vector<Card>& zone, const std::string& name)
{
    int n = 0;
    for (const Card& c : zone) { if (c.m_name.str() == name) { ++n; } }
    return n;
}

int TappedLands(const GameState& s)
{
    int n = 0;
    for (const Permanent& p : s.battlefield) { if (p.card.IsLand() && p.tapped) { ++n; } }
    return n;
}

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

std::vector<Action> ActionsOf(const std::vector<TurnSolver::Plan>& plans, Action::Kind k)
{
    std::vector<Action> out;
    for (const TurnSolver::Plan& p : plans)
    { for (const Action& a : p.actions) { if (a.kind == k) { out.push_back(a); } } }
    return out;
}

// Discard `number` from hand through the hook and resolve its madness with `pin`, under the rollout payer.
int DiscardAndResolve(GameState& s, int number, MadnessPin pin, MadnessWindow w = MadnessWindow::PreCombatMain)
{
    std::vector<MadnessPending> mq;
    const int hi = HandIdx(s, number);
    REQUIRE(hi >= 0);
    DiscardCardFromHand(s, 0, hi, &mq, "test");
    const MadnessHostScope host(&RolloutMadnessPayer(), MadnessPin{});
    return ResolvePendingMadness(s, mq, w, pin, "test");
}

}   // namespace

// ---- 1. card data -----------------------------------------------------------------------------------

TEST_CASE("Vampires A1: Gorger, Baron and Visitor load with their params and the Madness keyword")
{
    EnsureCardsLoaded();
    const CardDefinition& g = Def("Falkenrath Gorger");
    CHECK(g.params.grants_madness_creature_subtype == "Vampire");
    CHECK_FALSE(g.params.madness_cost.has_value());
    CHECK(g.card.m_mana_cost.ManaValue() == 1);
    const CardDefinition& b = Def("Markov Baron");
    REQUIRE(b.params.madness_cost.has_value());
    CHECK(b.params.madness_cost->ManaValue() == 3);
    CHECK(b.params.madness_cost->black == 1);
    CHECK(b.params.convoke);
    CHECK(b.card.HasKeyword(Keyword::Madness));
    CHECK(b.card.HasKeyword(Keyword::Lifelink));
    CHECK(b.params.lord_excludes_self);
    const CardDefinition& v = Def("Asylum Visitor");
    REQUIRE(v.params.madness_cost.has_value());
    CHECK(v.params.madness_cost->ManaValue() == 2);
    CHECK(v.card.HasKeyword(Keyword::Madness));
    CHECK(v.params.hellbent_upkeep_draw == 1);
    CHECK(v.params.hellbent_upkeep_life_loss == 1);
    CHECK(v.params.hellbent_upkeep_each_player);
    CHECK(ConvokePipColor(b) == Color::Black);
    CHECK(ConvokePipColor(Def("Chord of Calling")) == Color::Green);
}

// ---- 2. MadnessCostsFor -----------------------------------------------------------------------------

TEST_CASE("MadnessCostsFor: own cost, Gorger's grant (as a set), controller scope, the presence gate")
{
    EnsureCardsLoaded();
    GameState s = Fresh();
    const Card baron   = Def("Markov Baron").card;
    const Card visitor = Def("Asylum Visitor").card;
    const Card pit     = Def("Falkenrath Pit Fighter").card;
    const Card gorger  = Def("Falkenrath Gorger").card;
    CHECK(MadnessCostsFor(s, 0, baron).size() == 1);
    CHECK(MadnessCostsFor(s, 0, visitor).size() == 1);
    CHECK(MadnessCostsFor(s, 0, pit).empty());                 // no Gorger: no madness
    CHECK(MadnessCostsFor(s, 0, gorger).empty());               // a lone discarded Gorger gets nothing
    CHECK(MadnessCostsFor(s, 0, Def("Swamp").card).empty());
    CHECK(MadnessCostsFor(s, 0, Def("Aether Vial").card).empty());
    Put(s, "Falkenrath Gorger", 1, /*sick=*/false, /*ctrl=*/1);   // an OPPONENT's Gorger grants nothing
    CHECK(MadnessCostsFor(s, 0, pit).empty());
    Put(s, "Falkenrath Gorger", 2);
    REQUIRE(MadnessCostsFor(s, 0, pit).size() == 1);
    CHECK(MadnessCostsFor(s, 0, pit).front() == pit.m_mana_cost);  // its own mana cost, {1}{R}
    CHECK(MadnessCostsFor(s, 0, gorger).size() == 1);            // another Gorger grants a discarded one
    CHECK(MadnessCostsFor(s, 0, baron).size() == 1);             // own {2}{B} == granted {2}{B}: ONE option
    Put(s, "Falkenrath Gorger", 3);
    CHECK(MadnessCostsFor(s, 0, pit).size() == 1);               // two Gorgers: still one
    CHECK(MadnessCostsFor(s, 0, Def("Aether Vial").card).empty());   // not a creature
    s.deck_has_madness = false;                                   // the presence gate proves "none"
    CHECK(MadnessCostsFor(s, 0, baron).empty());
}

// ---- 3. the hook --------------------------------------------------------------------------------------

TEST_CASE("DiscardCardFromHand: a madness card goes to exile and is queued; others to the graveyard")
{
    EnsureCardsLoaded();
    GameState s = Fresh();
    Hand(s, "Markov Baron", 10);
    Hand(s, "Swamp", 11);
    std::vector<MadnessPending> mq;
    CHECK(DiscardCardFromHand(s, 0, HandIdx(s, 10), &mq, "test") == DiscardDest::MadnessExile);
    REQUIRE(mq.size() == 1);
    CHECK(mq.front().card.m_number == 10);
    CHECK(CountIn(s.exile, "Markov Baron") == 1);
    CHECK(CountIn(s.players[0].graveyard, "Markov Baron") == 0);
    CHECK(DiscardCardFromHand(s, 0, HandIdx(s, 11), &mq, "test") == DiscardDest::Graveyard);
    CHECK(mq.size() == 1);
    CHECK(CountIn(s.players[0].graveyard, "Swamp") == 1);
    // No host (a scratch caller): the card is declined into the graveyard and the event counted.
    Hand(s, "Asylum Visitor", 12);
    const long long unhosted = g_madness_unhosted.load();
    CHECK(DiscardCardFromHand(s, 0, HandIdx(s, 12), nullptr, "test") == DiscardDest::Graveyard);
    CHECK(g_madness_unhosted.load() == unhosted + 1);
    CHECK(CountIn(s.players[0].graveyard, "Asylum Visitor") == 1);
}

// ---- 4. the resolver ----------------------------------------------------------------------------------

TEST_CASE("ResolvePendingMadness: decline -> graveyard; cast -> a real cast that enters; unpayable -> graveyard")
{
    EnsureCardsLoaded();
    {
        GameState s = Fresh();
        Put(s, "Swamp", 50); Put(s, "Swamp", 51); Put(s, "Swamp", 52);
        Hand(s, "Asylum Visitor", 10);
        CHECK(DiscardAndResolve(s, 10, MadnessPin{ 0, 0 }) == 0);        // pinned DECLINE
        CHECK(CountIn(s.players[0].graveyard, "Asylum Visitor") == 1);
        CHECK(s.exile.empty());
        CHECK(TappedLands(s) == 0);
    }
    {
        GameState s = Fresh();
        Put(s, "Swamp", 50); Put(s, "Swamp", 51); Put(s, "Swamp", 52);
        Hand(s, "Asylum Visitor", 10);
        const int casts_before = s.spells_cast_this_turn;
        CHECK(DiscardAndResolve(s, 10, MadnessPin{ 1, 0 }) == 1);        // pinned CAST, {1}{B}
        const Permanent* v = FindNum(s, 10);
        REQUIRE(v != nullptr);
        CHECK(v->entered_this_turn);
        CHECK(s.spells_cast_this_turn == casts_before + 1);
        CHECK(TappedLands(s) == 2);
        CHECK(s.exile.empty());
        CHECK(CountIn(s.players[0].graveyard, "Asylum Visitor") == 0);
    }
    {
        GameState s = Fresh();
        Put(s, "Swamp", 50);                                              // one land: {1}{B} unpayable
        Hand(s, "Asylum Visitor", 10);
        CHECK(DiscardAndResolve(s, 10, MadnessPin{ 1, 0 }) == 0);        // stranded pin -> decline
        CHECK(CountIn(s.players[0].graveyard, "Asylum Visitor") == 1);
        CHECK(TappedLands(s) == 0);                                       // nothing paid
    }
    {
        // Unpinned (beyond the horizon / cleanup): the provider's default casts when payable.
        GameState s = Fresh();
        Put(s, "Swamp", 50); Put(s, "Swamp", 51);
        Hand(s, "Asylum Visitor", 10);
        CHECK(DiscardAndResolve(s, 10, MadnessPin{}) == 1);
        CHECK(FindNum(s, 10) != nullptr);
    }
}

TEST_CASE("Madness payment is a creature SPELL: Unclaimed Territory's colour pays it, not a Blood crack")
{
    EnsureCardsLoaded();
    GameState s = Fresh();
    Put(s, "Unclaimed Territory", 50);
    Put(s, "Unclaimed Territory", 51);
    Hand(s, "Asylum Visitor", 10);
    CHECK(DiscardAndResolve(s, 10, MadnessPin{ 1, 0 }) == 1);   // {1}{B} off two Territories
    CHECK(FindNum(s, 10) != nullptr);
    // The same lands cannot pay a coloured ABILITY cost (the restriction the A2 outlets live under).
    GameState t = Fresh();
    Put(t, "Unclaimed Territory", 50);
    ManaCost b; b.black = 1;
    CHECK_FALSE(TapForCostDirect(t, b, /*for_creature=*/false));
}

TEST_CASE("Gorger's grant is SNAPSHOTTED at the discard: Pit Fighter sacrificing the lone Gorger still casts")
{
    EnsureCardsLoaded();
    GameState s = Fresh();
    Put(s, "Falkenrath Pit Fighter", 1);
    Put(s, "Falkenrath Gorger", 2);
    Put(s, "Swamp", 50); Put(s, "Swamp", 51); Put(s, "Blackcleave Cliffs", 52); Put(s, "Blackcleave Cliffs", 53);
    Hand(s, "Vampire of the Dire Moon", 10);   // no madness of its own: only the Gorger grants it
    s.opponent_lost_life_this_turn = true;
    s.phase = Phase::PostCombatMain;
    REQUIRE(SacOutletLive(s, 0, 1, 2, std::string(), "Vampire of the Dire Moon"));
    {
        CreatureAbilityPayScope scope;
        REQUIRE(TapForCostDirect(s, Def("Falkenrath Pit Fighter").params.sac_creature_cost.value(), false));
    }
    {
        const MadnessHostScope host(&RolloutMadnessPayer(), MadnessPin{ 1, 0 });
        REQUIRE(ApplyDiscardSacOutlet(s, 0, 1, /*victim_id=*/2, std::string(), "Vampire of the Dire Moon"));
    }
    CHECK(FindNum(s, 2) == nullptr);                       // the Gorger was sacrificed
    CHECK(CountIn(s.players[0].graveyard, "Falkenrath Gorger") == 1);
    REQUIRE(FindNum(s, 10) != nullptr);                    // ...and the Dire Moon was still cast via madness
    CHECK(FindNum(s, 10)->entered_this_turn);
    CHECK(s.players[0].hand.size() == 2);                  // draw two after
}

TEST_CASE("Stromkirk Condemned: the madness-cast Baron is on the battlefield BEFORE the pump resolves")
{
    EnsureCardsLoaded();
    GameState s = Fresh();
    Put(s, "Stromkirk Condemned", 1);
    Put(s, "Vampire of the Dire Moon", 2);
    Put(s, "Swamp", 50); Put(s, "Swamp", 51); Put(s, "Swamp", 52);
    Hand(s, "Markov Baron", 10);
    {
        const MadnessHostScope host(&RolloutMadnessPayer(), MadnessPin{ 1, 0 });
        CHECK(ApplyActivatePump(s, 0, 1, /*mode=*/4, 1, "Markov Baron") == 1);
    }
    const Permanent* baron = FindNum(s, 10);
    REQUIRE(baron != nullptr);
    CHECK(baron->temp_power_bonus == 1);                   // pumped: it resolved first
    CHECK(FindNum(s, 2)->temp_power_bonus == 1);
    // The Dire Moon also gets Baron's lord bonus.
    CHECK(ComputeLordBonus(FindNum(s, 2)->card, s, 0, false, FindNum(s, 2)).first >= 1);
}

// ---- 5. convoke ---------------------------------------------------------------------------------------

TEST_CASE("Convoke generalised: Baron's pip is {B}; arm 0 = every free body; post-combat every body is free")
{
    EnsureCardsLoaded();
    GameState s = Fresh();
    Put(s, "Vampire of the Dire Moon", 1, /*sick=*/true);     // black, sick: free, pays {B}
    Put(s, "Legion Lieutenant", 2, /*sick=*/true);            // W/B multicolour, sick: free (a black body)
    Put(s, "Stromkirk Captain", 3, /*sick=*/false);           // can attack: COSTLY pre-combat
    const CardDefinition& baron = Def("Markov Baron");
    const ConvokeRules pre = ConvokeRulesFor(baron, /*post_combat_or_cleanup=*/false);
    CHECK(pre.pip == Color::Black);
    const std::vector<ConvokeArmSpec> arms = CreatureConvokeArms(s, 0, baron.card.m_mana_cost, pre);
    REQUIRE(arms.size() >= 2);
    CHECK(arms[0].pip + arms[0].other == 2);                  // both sick bodies
    CHECK(arms[0].reduced.ManaValue() == 1);                  // {2}{B} -> {1}
    CHECK(arms[0].reduced.black == 0);                        // the pip is covered
    CHECK(arms[1].reduced.ManaValue() == 0);                  // + the Captain
    const ConvokeRules post = ConvokeRulesFor(baron, /*post_combat_or_cleanup=*/true);
    const std::vector<ConvokeArmSpec> parms = CreatureConvokeArms(s, 0, baron.card.m_mana_cost, post);
    REQUIRE(parms.size() == 1);                               // every body free -> one arm
    CHECK(parms[0].reduced.ManaValue() == 0);
    // Chord of Calling keeps the legacy classification byte-for-byte (green pip, no post-combat collapse).
    const ConvokeRules chord = ConvokeRulesFor(Def("Chord of Calling"), true);
    CHECK(chord.pip == Color::Green);
    CHECK_FALSE(chord.all_free);
    CHECK_FALSE(chord.tap_ability_costly);
    const ConvokeClasses a = ClassifyConvokeBodies(s, 0);
    const ConvokeClasses b = ClassifyConvokeBodiesEx(s, 0, ConvokeRules{});
    CHECK(a.free_green == b.free_green); CHECK(a.free_other == b.free_other);
    CHECK(a.atk_green == b.atk_green);   CHECK(a.atk_other == b.atk_other);
    ManaCost g; g.generic = 1; g.green = 3;
    ApplyConvokeReduction(g, 2, 1);                           // default pip = green: unchanged semantics
    CHECK(g.green == 1); CHECK(g.generic == 0);
}

TEST_CASE("Convoke (enumeration): Baron is offered with arm 0 and the costly arms, each a distinct plan")
{
    EnsureCardsLoaded();
    GameState s = Fresh();
    Put(s, "Vampire of the Dire Moon", 1, /*sick=*/true);
    Put(s, "Stromkirk Captain", 2, /*sick=*/false);
    Put(s, "Swamp", 50);
    Hand(s, "Markov Baron", 10);
    // One land + one free body + one costly body: {2}{B} is castable only with BOTH bodies.
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, /*is_pre_combat=*/true);
    std::set<std::pair<int, int>> arms;
    for (const Action& a : ActionsOf(plans, Action::Kind::CastFromHand))
    { if (a.card_name.str() == "Markov Baron") { arms.insert({ a.convoke_green, a.convoke_other }); } }
    CHECK(arms.count({ 2, 0 }) == 1);    // the Dire Moon (free) + the Captain (costly), both black
    // Realised: the bodies tap, one Swamp pays the {1}... the {B} is covered by a black body.
    for (const TurnSolver::Plan& p : plans)
    {
        bool has = false;
        for (const Action& a : p.actions) { if (a.card_name.str() == "Markov Baron") { has = true; } }
        if (!has) { continue; }
        GameState r = s;
        TurnSolver::ApplyPlan(r, p, true);
        CHECK(FindNum(r, 10) != nullptr);
        break;
    }
}

// ---- 6. the searched outlet axis ----------------------------------------------------------------------

TEST_CASE("Madness fan: an outlet discard of a madness card is offered as decline AND cast, distinct plans")
{
    EnsureCardsLoaded();
    GameState s = Fresh();
    Put(s, "Stromkirk Condemned", 1);
    Put(s, "Swamp", 50); Put(s, "Swamp", 51); Put(s, "Swamp", 52);
    Hand(s, "Markov Baron", 10);
    Hand(s, "Swamp", 11);
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, /*is_pre_combat=*/true);
    std::set<int> baron_choices;
    int swamp_unpinned = 0;
    for (const Action& a : ActionsOf(plans, Action::Kind::ActivatePump))
    {
        if (a.discard_name.str() == "Markov Baron") { baron_choices.insert(a.madness_choice); }
        if (a.discard_name.str() == "Swamp" && a.madness_choice == -1) { ++swamp_unpinned; }
    }
    CHECK(baron_choices.count(0) == 1);    // decline survives the dedup
    CHECK(baron_choices.count(1) == 1);    // cast survives the dedup
    CHECK(baron_choices.count(-1) == 0);   // never unpinned when madness is possible
    CHECK(swamp_unpinned > 0);             // a land: no madness, no fan (keys byte-identical)
    // Both realise: the cast plan puts Baron onto the battlefield, the decline plan puts it in the yard.
    bool saw_cast = false, saw_decline = false;
    for (const TurnSolver::Plan& p : plans)
    {
        const Action* pump = nullptr;
        for (const Action& a : p.actions)
        { if (a.kind == Action::Kind::ActivatePump && a.discard_name.str() == "Markov Baron") { pump = &a; } }
        if (pump == nullptr || p.actions.size() != 1) { continue; }
        GameState r = s;
        TurnSolver::ApplyPlan(r, p, true);
        if (pump->madness_choice == 1) { saw_cast = true; CHECK(FindNum(r, 10) != nullptr); }
        if (pump->madness_choice == 0) { saw_decline = true; CHECK(CountIn(r.players[0].graveyard, "Markov Baron") == 1); }
    }
    CHECK(saw_cast);
    CHECK(saw_decline);
}

// (Human play -- ONE unpinned outlet entry, the card and its madness asked at resolution -- is not
// unit-testable here: HumanPlayActive() is a process-static MTG_HUMAN_PLAY read. It is verified on the
// real protocol by the --claude-play repro recorded in the ledger.)

TEST_CASE("Madness fan: a Gorger IN HAND opens the grant for this plan; the cast variant requires casting it")
{
    EnsureCardsLoaded();
    GameState s = Fresh();
    Put(s, "Stromkirk Condemned", 1);
    Put(s, "Blackcleave Cliffs", 50); Put(s, "Blackcleave Cliffs", 51); Put(s, "Swamp", 52);
    Hand(s, "Falkenrath Gorger", 10);
    Hand(s, "Vampire of the Dire Moon", 11);   // {B}: madness only through the Gorger
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, /*is_pre_combat=*/true);
    bool cast_with_gorger = false;
    for (const TurnSolver::Plan& p : plans)
    {
        bool casts_gorger = false, madness_cast = false;
        for (const Action& a : p.actions)
        {
            if (a.kind == Action::Kind::CastFromHand && a.card_name.str() == "Falkenrath Gorger") { casts_gorger = true; }
            if (a.kind == Action::Kind::ActivatePump && a.discard_name.str() == "Vampire of the Dire Moon"
                && a.madness_choice > 0) { madness_cast = true; }
        }
        if (madness_cast) { CHECK(casts_gorger); }          // rule 5
        if (madness_cast && casts_gorger)
        {
            cast_with_gorger = true;
            GameState r = s;
            TurnSolver::ApplyPlan(r, p, true);
            CHECK(FindNum(r, 11) != nullptr);              // Gorger first, then the madness cast
        }
    }
    CHECK(cast_with_gorger);
}

TEST_CASE("Two Gorgers grant one option: the fan holds one cast variant per discard, not two")
{
    EnsureCardsLoaded();
    GameState s = Fresh();
    Put(s, "Stromkirk Condemned", 1);
    Put(s, "Falkenrath Gorger", 2);
    Put(s, "Falkenrath Gorger", 3);
    Put(s, "Swamp", 50);
    Hand(s, "Vampire of the Dire Moon", 10);
    std::multiset<int> choices;
    for (const Action& a : ActionsOf(TurnSolver::EnumerateMainPlans(s, true), Action::Kind::ActivatePump))
    { if (a.discard_name.str() == "Vampire of the Dire Moon") { choices.insert(a.madness_choice); } }
    CHECK(choices.count(1) >= 1);
    CHECK(choices.count(2) == 0);   // no second (duplicate) cost
}

// ---- 7. cleanup ---------------------------------------------------------------------------------------

TEST_CASE("Cleanup madness: a shed Visitor is cast when payable (the provider rule), declined when not")
{
    EnsureCardsLoaded();
    // Eight Asylum Visitors: whichever the cleanup sheds is a madness card.
    auto build = [](int lands)
    {
        GameState s = Fresh();
        s.phase = Phase::PostCombatMain;
        for (int i = 0; i < lands; ++i) { Put(s, "Swamp", 50 + i); }
        for (int k = 0; k < 8; ++k) { Hand(s, "Asylum Visitor", 10 + k); }
        s.floating_mana.black = 2;   // main 2's float: gone before the cleanup payment (CR 500.4)
        return s;
    };
    auto visitors_on_bf = [](const GameState& s)
    {
        int n = 0;
        for (const Permanent& p : s.battlefield) { if (p.card.m_name.str() == "Asylum Visitor") { ++n; } }
        return n;
    };
    // Rollout shape (SimulateEndAndStartNextTurn): the shed with a queue, the reset, the resolver.
    for (int lands : { 2, 0 })
    {
        GameState r = build(lands);
        std::vector<MadnessPending> mq;
        bool consulted = false;
        CHECK(CleanupDiscardShed(r, nullptr, 1, -1, false, true, &consulted, &mq) == 1);
        REQUIRE(mq.size() == 1);
        r.floating_mana = ManaPool{};
        {
            const MadnessHostScope host(&RolloutMadnessPayer(), MadnessPin{});
            ResolvePendingMadness(r, mq, MadnessWindow::Cleanup, MadnessPin{}, "cleanup");
        }
        CHECK(r.players[0].hand.size() == 7);
        if (lands == 2) { CHECK(visitors_on_bf(r) == 1); CHECK(TappedLands(r) == 2); }
        else            { CHECK(CountIn(r.players[0].graveyard, "Asylum Visitor") == 1); }
    }
    // Executor: GameEngine::CleanupStep itself (PlayOutFrom at ResumeAt::Cleanup, stopping at this turn).
    for (int lands : { 2, 0 })
    {
        GameState e = build(lands);
        AIEngine eng(MulliganProfile::DefaultProfile(), 0, 0);
        GameEngine ge(eng);
        ge.PlayOutFrom(e, e.turn_number, GameEngine::ResumeAt::Cleanup);
        CHECK(e.players[0].hand.size() == 7);
        CHECK(e.floating_mana.Total() == 0);
        if (lands == 2) { CHECK(visitors_on_bf(e) == 1); CHECK(TappedLands(e) == 2); }
        else            { CHECK(CountIn(e.players[0].graveyard, "Asylum Visitor") == 1); }
    }
}

// ---- 8. Asylum Visitor ---------------------------------------------------------------------------------

TEST_CASE("Asylum Visitor: our upkeep, empty hand -> draw one, lose one; two Visitors -> still one")
{
    EnsureCardsLoaded();
    {
        GameState s = Fresh();
        Put(s, "Asylum Visitor", 1);
        const int life = s.players[0].life;
        CHECK(PerformUpkeepHellbentDraw(s));
        CHECK(s.players[0].hand.size() == 1);
        CHECK(s.players[0].life == life - 1);
        CHECK(s.players[0].cards_drawn_this_turn == 1);
    }
    {
        GameState s = Fresh();
        Put(s, "Asylum Visitor", 1);
        Put(s, "Asylum Visitor", 2);
        const int life = s.players[0].life;
        CHECK(PerformUpkeepHellbentDraw(s));
        CHECK(s.players[0].hand.size() == 1);                 // the intervening-if fails for the second
        CHECK(s.players[0].life == life - 1);
    }
    {
        GameState s = Fresh();
        Put(s, "Asylum Visitor", 1);
        Hand(s, "Swamp", 10);
        const int life = s.players[0].life;
        CHECK(PerformUpkeepHellbentDraw(s));
        CHECK(s.players[0].hand.size() == 1);                 // not hellbent: nothing
        CHECK(s.players[0].life == life);
    }
    {
        GameState s = Fresh();
        Put(s, "Asylum Visitor", 1);
        Hand(s, "Swamp", 10);
        s.players[0].hand.back().m_is_staged = true;          // a staged card is in exile: hand is empty
        CHECK(PerformUpkeepHellbentDraw(s));
        CHECK(s.players[0].hand.size() == 2);
    }
    {
        GameState s = Fresh();
        Put(s, "Asylum Visitor", 1);
        s.players[0].library = Library{};
        CHECK_FALSE(PerformUpkeepHellbentDraw(s));            // drawing from an empty library loses
    }
}

TEST_CASE("Asylum Visitor at the OPPONENT's upkeep: never without a dealt hand; one draw per Visitor with one")
{
    EnsureCardsLoaded();
    GameState s = Fresh();
    Put(s, "Asylum Visitor", 1);
    Put(s, "Asylum Visitor", 2);
    const int life = s.players[0].life;
    CHECK(PerformOpponentUpkeepHellbentDraw(s));
    CHECK(s.players[0].hand.empty());                         // not dealt: their opening seven
    s.opponent_library_dealt = true;                          // dealt, and their hand is empty
    CHECK(PerformOpponentUpkeepHellbentDraw(s));
    CHECK(s.players[0].hand.size() == 2);                     // each Visitor resolves
    CHECK(s.players[0].life == life - 2);
    CHECK(s.players[0].cards_drawn_this_turn == 0);           // their turn: our draw count untouched
}

// ---- 9. viewer ----------------------------------------------------------------------------------------

TEST_CASE("Madness chooser: asked at a real unpinned trigger, never inside a search scope")
{
    EnsureCardsLoaded();
    int asked = 0;
    MadnessChooser ch = [&asked](const GameState&, int, const std::string&, const Card&,
                                 const std::vector<MadnessChoice>& opts, int heur) -> int
    { ++asked; CHECK(!opts.empty()); CHECK(heur == 0); return -1; };
    MadnessChooser* prev = g_play_madness_chooser;
    const bool prev_hooks = g_play_hooks_installed;
    g_play_madness_chooser = &ch;
    g_play_hooks_installed = true;
    {
        GameState s = Fresh();
        Put(s, "Swamp", 50); Put(s, "Swamp", 51);
        Hand(s, "Asylum Visitor", 10);
        DiscardAndResolve(s, 10, MadnessPin{});
        CHECK(asked == 1);
        CHECK(CountIn(s.players[0].graveyard, "Asylum Visitor") == 1);   // the human declined
    }
    {
        GameState s = Fresh();
        Put(s, "Swamp", 50); Put(s, "Swamp", 51);
        Hand(s, "Asylum Visitor", 10);
        RevealLogPause pause;
        DiscardAndResolve(s, 10, MadnessPin{});
        CHECK(asked == 1);                                     // nulled under RevealLogPause
        CHECK(FindNum(s, 10) != nullptr);                      // the provider's default cast
    }
    {
        GameState s = Fresh();
        Put(s, "Swamp", 50); Put(s, "Swamp", 51);
        Hand(s, "Asylum Visitor", 10);
        DiscardAndResolve(s, 10, MadnessPin{ 1, 0 });           // PINNED: the choice was made already
        CHECK(asked == 1);
    }
    g_play_madness_chooser = prev;
    g_play_hooks_installed = prev_hooks;
}

// ---- 10. lockstep -------------------------------------------------------------------------------------

TEST_CASE("Lockstep: every madness outlet plan realises the same state in both worlds; no in-window default")
{
    EnsureCardsLoaded();
    for (bool main2 : { false, true })
    {
        GameState s = Fresh();
        s.phase = main2 ? Phase::PostCombatMain : Phase::PreCombatMain;
        Put(s, "Falkenrath Pit Fighter", 1);
        Put(s, "Stromkirk Condemned", 2);
        Put(s, "Falkenrath Gorger", 3);
        Put(s, "Vampire of the Dire Moon", 4, /*sick=*/true);
        Put(s, "Unclaimed Territory", 50);
        Put(s, "Blackcleave Cliffs", 51);
        Put(s, "Swamp", 52);
        Put(s, "Swamp", 53);
        Hand(s, "Markov Baron", 10);
        Hand(s, "Asylum Visitor", 11);
        Hand(s, "Swamp", 12);
        s.opponent_lost_life_this_turn = main2;
        const long long unpinned_before = g_madness_unpinned_in_window.load();
        const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, /*is_pre_combat=*/!main2);
        int checked = 0, madness_plans = 0;
        for (const TurnSolver::Plan& line0 : plans)
        {
            TurnSolver::Plan line = line0;
            if (line.bp_choice < 0) { line.bp_choice = TurnSolver::kBpEmptyChoice; line.bp_all = true; }
            bool interesting = false;
            for (const Action& a : line.actions)
            {
                if (a.madness_choice >= 0) { interesting = true; }
                if (a.kind == Action::Kind::CastFromHand && (a.convoke_green > 0 || a.convoke_other > 0)) { interesting = true; }
            }
            if (!interesting) { continue; }
            ++madness_plans;
            GameState roll = s;
            {
                greedywindow::Frame frame(3);   // as inside a search: an unpinned default would be counted
                TurnSolver::ApplyPlan(roll, line, /*is_pre_combat=*/!main2);
            }
            AIEngine eng(MulliganProfile::DefaultProfile(), 3, 20);
            eng.SetSearchPostCombat(true);
            MtgTestSeam::CommitPhasePlan(eng, line, !main2);
            GameState exec = s;
            eng.TakeTurn(exec, !main2, ResolveAll);
            std::string desc;
            for (const Action& a : line.actions)
            { desc += a.card_name.str() + "/k" + std::to_string(static_cast<int>(a.kind)) + "/d" + a.discard_name.str()
                    + "/M" + std::to_string(static_cast<int>(a.madness_choice)) + "." + std::to_string(static_cast<int>(a.madness_arm))
                    + "/C" + std::to_string(a.convoke_green) + "," + std::to_string(a.convoke_other) + " "; }
            INFO("main2=" << main2 << " plan " << checked << " [" << desc << "]");
            CHECK(roll.battlefield.size() == exec.battlefield.size());
            for (const Permanent& p : roll.battlefield)
            {
                if (p.card.m_number >= 1000) { continue; }
                const Permanent* q = FindNum(exec, p.card.m_number);
                REQUIRE(q != nullptr);
                CHECK(q->tapped == p.tapped);
                CHECK(q->temp_power_bonus == p.temp_power_bonus);
            }
            CHECK(roll.players[0].hand.size() == exec.players[0].hand.size());
            CHECK(roll.players[0].graveyard.size() == exec.players[0].graveyard.size());
            CHECK(roll.exile.size() == exec.exile.size());
            CHECK(roll.players[0].life == exec.players[0].life);
            CHECK(roll.spells_cast_this_turn == exec.spells_cast_this_turn);
            ++checked;
            if (checked >= 40) { break; }
        }
        CHECK(madness_plans > 0);
        CHECK(g_madness_unpinned_in_window.load() == unpinned_before);
    }
}

// ---- 11. routing --------------------------------------------------------------------------------------

TEST_CASE("Routing: Vampires (now with convoke) stays VampiresProvider; Melira Pod stays MeliraPod")
{
    EnsureCardsLoaded();
    CHECK(std::string(DetectDecisionProvider(DeckLoader::LoadFromFile(
        ResolveHeuristicDefaultsPath("decks/Vampires/Vampire.cod"))).Name()) == "Vampires");
    CHECK(std::string(DetectDecisionProvider(DeckLoader::LoadFromFile(
        ResolveHeuristicDefaultsPath("decks/Melira Pod/Melira Pod.cod"))).Name()) == "MeliraPod");
}
