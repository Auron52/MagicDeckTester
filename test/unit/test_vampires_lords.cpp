// Unit tests for the Vampires cluster B (2026-10-18; docs/design/vampires/design_B_lords.md):
// Captivating Vampire (lord + "Tap five untapped Vampires you control: Gain control of target creature.
// It becomes a Vampire"), Bloodline Keeper // Lord of Lineage (tap-for-token + the {B} transform, its
// in-combat window and the back face leaving the battlefield).
//
// WHY THESE ARE UNIT TESTS. The defects this cluster can have are silent: a tap set missing from the
// plan signature collapses every variant to the first enumerated (a greedy pick inside the window); a
// transform gated on the pre-plan count makes "cast the fifth Vampire, then transform" unfindable; a
// steal that consults summoning sickness refuses the ruling's own example (tapping itself the turn it
// lands); a face swap that runs the enter cascade hands Vampire Socialite a free counter. None of them
// crashes or diverges between the worlds -- they share the resolvers -- so only a direct test sees them.
#include <doctest/doctest.h>

#include "ai/AIEngine.h"
#include "ai/DecisionProviders.h"
#include "ai/ManaPayment.h"
#include "ai/TurnSolver.h"
#include "cards/CardDatabase.h"
#include "core/EffectHandler.h"
#include "core/GameSetup.h"
#include "core/GameState.h"
#include "core/GreedyWindow.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"
#include "deck/DeckLoader.h"
#include "mtg_test_seam.h"

#include <algorithm>
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

// A passive-opponent scheduled spawn (GameEngine's shape: no definition, colourless, no subtypes).
int Spawn(GameState& s, int pw, int tf, int number)
{
    Permanent p;
    p.card.m_name = std::to_string(pw) + "/" + std::to_string(tf) + " Creature";
    p.card.RehashName();
    p.card.AddType(CardType::Creature);
    p.card.m_power     = pw;
    p.card.m_toughness = tf;
    p.card.m_number    = number;
    p.controller_index = 1;
    p.owner_index      = 1;
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

const Permanent* FindNum(const GameState& s, int number)
{
    for (const Permanent& p : s.battlefield) { if (p.card.m_number == number) { return &p; } }
    return nullptr;
}
Permanent* FindNumMut(GameState& s, int number)
{
    for (Permanent& p : s.battlefield) { if (p.card.m_number == number) { return &p; } }
    return nullptr;
}

int CountIn(const std::vector<Card>& zone, const std::string& name)
{
    int n = 0;
    for (const Card& c : zone) { if (c.m_name.str() == name) { ++n; } }
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

bool IsMode(const Action& a, Action::AbilityMode m)
{ return a.kind == Action::Kind::ActivatePermAbility && a.ability_mode == m; }

std::vector<Action> ModeActions(const std::vector<TurnSolver::Plan>& plans, Action::AbilityMode m)
{
    std::vector<Action> out;
    for (const TurnSolver::Plan& p : plans)
    { for (const Action& a : p.actions) { if (IsMode(a, m)) { out.push_back(a); } } }
    return out;
}

int TotalPower(const GameState& s, const Permanent& p)
{
    return p.EffectivePower() + ComputeLordBonus(p.card, s, p.controller_index, p.AnimatedAllTypes(), &p).first;
}

}   // namespace

// ---- 1. card data -----------------------------------------------------------------------------------

TEST_CASE("Vampires B: Captivating Vampire and both Keeper faces load with their params")
{
    EnsureCardsLoaded();
    const CardDefinition& cv = Def("Captivating Vampire");
    CHECK(cv.card.m_mana_cost.ManaValue() == 3);
    CHECK(cv.params.steal_tap_count == 5);
    CHECK(cv.params.steal_tap_subtype == "Vampire");
    CHECK(cv.params.steal_adds_subtype == "Vampire");
    CHECK(cv.params.power_bonus == 1);
    const CardDefinition& bk = Def("Bloodline Keeper");
    CHECK(bk.card.m_mana_cost.ManaValue() == 4);
    CHECK(bk.params.transform_back_name == "Lord of Lineage");
    REQUIRE(bk.params.transform_cost.has_value());
    CHECK(bk.params.transform_cost->black == 1);
    CHECK(bk.params.transform_min_controlled_count == 5);
    CHECK(bk.params.tap_token_cost.has_value());
    CHECK(bk.params.tap_token_color == "B");
    const CardDefinition& ll = Def("Lord of Lineage");
    CHECK(ll.params.transform_front_name == "Bloodline Keeper");
    CHECK(ll.params.transform_back_name.empty());          // one-shot: no transform on the back face
    CHECK(ll.params.power_bonus == 2);
    CHECK(ll.params.tap_token_cost.has_value());
    CHECK(ll.card.HasColor(Color::Black));                 // colour indicator (convoke-relevant)
    const std::vector<std::string> backs = CardDatabase::Instance().MdfcBackFaceNames();
    CHECK(std::find(backs.begin(), backs.end(), "Lord of Lineage") != backs.end());
    CHECK_FALSE(PermAbilityTaps(PermAbilityMode::TapSubtypeSteal));
    CHECK_FALSE(PermAbilityTaps(PermAbilityMode::TransformSelf));
}

// ---- 2. Bloodline Keeper's token ----------------------------------------------------------------------

TEST_CASE("Keeper token: a sick Keeper is not offered; a ready one makes a sick 2/2 black flying Vampire")
{
    EnsureCardsLoaded();
    GameState s = Fresh();
    Put(s, "Bloodline Keeper", 1, /*sick=*/true);
    {
        const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, true);
        int n = 0;
        for (const TurnSolver::Plan& p : plans)
        { for (const Action& a : p.actions) { if (a.kind == Action::Kind::TapForTokenPay) { ++n; } } }
        CHECK(n == 0);
    }
    FindNumMut(s, 1)->entered_this_turn = false;
    Put(s, "Legion Lieutenant", 2);
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, true);
    const TurnSolver::Plan* tok = nullptr;
    for (const TurnSolver::Plan& p : plans)
    {
        if (p.actions.size() == 1 && p.actions[0].kind == Action::Kind::TapForTokenPay) { tok = &p; break; }
    }
    REQUIRE(tok != nullptr);
    GameState r = s;
    TurnSolver::ApplyPlan(r, *tok, true);
    CHECK(FindNum(r, 1)->tapped);
    const Permanent* t = nullptr;
    for (const Permanent& p : r.battlefield) { if (p.is_token) { t = &p; } }
    REQUIRE(t != nullptr);
    CHECK(CardHasSubtype(t->card, "Vampire"));
    CHECK(t->card.HasColor(Color::Black));
    CHECK(t->card.HasKeyword(Keyword::Flying));
    CHECK(t->entered_this_turn);
    CHECK(TotalPower(r, *t) == 3);          // 2/2 + Legion Lieutenant
    // A live token tap makes the Keeper a COSTLY convoke body; a sick one is not.
    CHECK(ConvokeBodyTapAbilityLive(s, *FindNum(s, 1), &Def("Bloodline Keeper")));
    GameState sick = s;
    FindNumMut(sick, 1)->entered_this_turn = true;
    CHECK_FALSE(ConvokeBodyTapAbilityLive(sick, *FindNum(sick, 1), &Def("Bloodline Keeper")));
}

// ---- 3. the transform -----------------------------------------------------------------------------------

TEST_CASE("Transform restriction: four Vampires no, five yes -- sick or tapped Keeper alike")
{
    EnsureCardsLoaded();
    GameState s = Fresh();
    Put(s, "Bloodline Keeper", 1, /*sick=*/true);
    Put(s, "Vampire of the Dire Moon", 2);
    Put(s, "Legion Lieutenant", 3);
    Put(s, "Falkenrath Gorger", 4);
    CHECK_FALSE(PermAbilitySourceLive(s, 0, 1, PermAbilityMode::TransformSelf));
    CreateToken(s, 0, 2, 2, std::vector<std::string>{ "Vampire" }, "B", std::vector<std::string>{ "Flying" });
    CHECK(PermAbilitySourceLive(s, 0, 1, PermAbilityMode::TransformSelf));   // the token is the fifth
    FindNumMut(s, 1)->tapped = true;
    CHECK(PermAbilitySourceLive(s, 0, 1, PermAbilityMode::TransformSelf));   // no {T} in the cost
    // A stolen creature counts once it is a Vampire; an opponent's Vampire never does.
    GameState t = Fresh();
    Put(t, "Bloodline Keeper", 1);
    Put(t, "Vampire of the Dire Moon", 2);
    Put(t, "Legion Lieutenant", 3);
    Put(t, "Falkenrath Gorger", 4);
    Put(t, "Vampire of the Dire Moon", 5, false, /*ctrl=*/1);
    CHECK_FALSE(PermAbilitySourceLive(t, 0, 1, PermAbilityMode::TransformSelf));
}

TEST_CASE("Transform swap: same object -- number, counters, tapped, sickness kept; Lord pumps others +2/+2")
{
    EnsureCardsLoaded();
    GameState s = Fresh();
    s.opponent_lost_life_this_turn = true;   // Vampire Socialite's static would add a counter to an ENTERING Vampire
    Put(s, "Bloodline Keeper", 1, /*sick=*/true);
    Put(s, "Vampire Socialite", 2);
    Put(s, "Legion Lieutenant", 3);
    Put(s, "Falkenrath Gorger", 4);
    Put(s, "Vampire of the Dire Moon", 5);
    Permanent* k = FindNumMut(s, 1);
    PutPlusCounters(s, *k, 1);
    k->tapped = true;
    const int plus_before = [&] { int n = 0; for (const Counter& c : FindNum(s, 1)->counters) { if (c.type == Counter::Type::PlusOnePlusOne) { n += c.count; } } return n; }();
    const int gorger_before = TotalPower(s, *FindNum(s, 4));
    REQUIRE(PermAbilitySourceLive(s, 0, 1, PermAbilityMode::TransformSelf));
    const std::size_t bf_before = s.battlefield.size();
    ApplyPermAbility(s, 0, 1, PermAbilityMode::TransformSelf);
    const Permanent* l = FindNum(s, 1);
    REQUIRE(l != nullptr);
    CHECK(l->card.m_name.str() == "Lord of Lineage");
    CHECK(CardDatabase::Instance().LookupCached(l->card) == &Def("Lord of Lineage"));
    CHECK(l->tapped);
    CHECK(l->entered_this_turn);
    int plus_after = 0;
    for (const Counter& c : l->counters) { if (c.type == Counter::Type::PlusOnePlusOne) { plus_after += c.count; } }
    CHECK(plus_after == plus_before);            // no Socialite counter: transforming is not entering
    CHECK(s.battlefield.size() == bf_before);
    CHECK(TotalPower(s, *FindNum(s, 4)) == gorger_before + 2);   // other Vampires +2/+2
    // Lord does not pump itself: 5 base + its counter + Legion Lieutenant's +1.
    CHECK(TotalPower(s, *l) == 5 + plus_after + 1);
    // One-shot: the back face offers no transform.
    CHECK_FALSE(PermAbilitySourceLive(s, 0, 1, PermAbilityMode::TransformSelf));
}

TEST_CASE("Transform leaves: a sacrificed Lord of Lineage is a Bloodline Keeper card in the graveyard")
{
    EnsureCardsLoaded();
    GameState s = Fresh();
    Put(s, "Lord of Lineage", 1);
    int idx = -1;
    for (int i = 0; i < static_cast<int>(s.battlefield.size()); ++i)
    { if (s.battlefield[static_cast<std::size_t>(i)].card.m_number == 1) { idx = i; } }
    REQUIRE(idx >= 0);
    SacrificePermanentAt(s, 0, idx);
    CHECK(CountIn(s.players[0].graveyard, "Bloodline Keeper") == 1);
    CHECK(CountIn(s.players[0].graveyard, "Lord of Lineage") == 0);
}

TEST_CASE("Transform payment: Swamp and Secluded Courtyard pay {B}; Unclaimed Territory alone does not")
{
    EnsureCardsLoaded();
    for (const char* land : { "Swamp", "Secluded Courtyard", "Unclaimed Territory" })
    {
        GameState s = Fresh();
        Put(s, "Bloodline Keeper", 1);
        Put(s, "Vampire of the Dire Moon", 2);
        Put(s, "Legion Lieutenant", 3);
        Put(s, "Falkenrath Gorger", 4);
        Put(s, "Vampire of the Dire Moon", 5);
        Put(s, land, 50);
        const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, true);
        bool transformed = false;
        for (const TurnSolver::Plan& p : plans)
        {
            bool has = false;
            for (const Action& a : p.actions) { if (IsMode(a, Action::AbilityMode::TransformSelf)) { has = true; } }
            if (!has) { continue; }
            GameState r = s;
            TurnSolver::ApplyPlan(r, p, true);
            if (FindNum(r, 1)->card.m_name.str() == "Lord of Lineage") { transformed = true; }
        }
        INFO("land " << land);
        CHECK(transformed == (std::string(land) != "Unclaimed Territory"));
    }
}

TEST_CASE("Transform reachability: the fifth Vampire cast THIS plan, and 'token then transform', both realise")
{
    EnsureCardsLoaded();
    // (a) four Vampires on the board, the fifth in hand: [cast Dire Moon, transform] is one plan.
    {
        GameState s = Fresh();
        Put(s, "Bloodline Keeper", 1, /*sick=*/true);
        Put(s, "Legion Lieutenant", 2);
        Put(s, "Falkenrath Gorger", 3);
        Put(s, "Stromkirk Captain", 4);
        Put(s, "Swamp", 50); Put(s, "Swamp", 51);
        Hand(s, "Vampire of the Dire Moon", 10);
        const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, true);
        bool ok = false;
        for (const TurnSolver::Plan& p : plans)
        {
            bool tr = false, cast = false;
            for (const Action& a : p.actions)
            {
                if (IsMode(a, Action::AbilityMode::TransformSelf)) { tr = true; CHECK(a.vamp_casts_needed == 1); }
                if (a.kind == Action::Kind::CastFromHand && a.card_name.str() == "Vampire of the Dire Moon") { cast = true; }
            }
            if (!tr) { continue; }
            CHECK(cast);   // rule 6b: a transform relying on one arrival needs the cast in the selection
            GameState r = s;
            TurnSolver::ApplyPlan(r, p, true);
            if (FindNum(r, 1)->card.m_name.str() == "Lord of Lineage") { ok = true; }
        }
        CHECK(ok);
    }
    // (b) four Vampires incl. a ready Keeper: [token tap, transform] -- the count bands run the token first.
    {
        GameState s = Fresh();
        Put(s, "Bloodline Keeper", 1);
        Put(s, "Legion Lieutenant", 2);
        Put(s, "Falkenrath Gorger", 3);
        Put(s, "Stromkirk Captain", 4);
        Put(s, "Swamp", 50);
        const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, true);
        bool ok = false;
        for (const TurnSolver::Plan& p : plans)
        {
            bool tr = false, tok = false;
            for (const Action& a : p.actions)
            {
                if (IsMode(a, Action::AbilityMode::TransformSelf)) { tr = true; }
                if (a.kind == Action::Kind::TapForTokenPay) { tok = true; }
            }
            if (!tr || !tok) { continue; }
            // Both orders of the plan's actions realise the transform (OrderTrailingActivations).
            for (bool reversed : { false, true })
            {
                TurnSolver::Plan q = p;
                if (reversed) { std::reverse(q.actions.begin(), q.actions.end()); }
                GameState r = s;
                TurnSolver::ApplyPlan(r, q, true);
                CHECK(FindNum(r, 1)->card.m_name.str() == "Lord of Lineage");
                if (FindNum(r, 1)->card.m_name.str() == "Lord of Lineage") { ok = true; }
            }
        }
        CHECK(ok);
    }
    // (c) a Keeper cast THIS plan transforms the same turn (no {T}; hand-source variant).
    {
        GameState s = Fresh();
        Put(s, "Legion Lieutenant", 2);
        Put(s, "Falkenrath Gorger", 3);
        Put(s, "Stromkirk Captain", 4);
        Put(s, "Vampire of the Dire Moon", 5);
        for (int k = 0; k < 5; ++k) { Put(s, "Swamp", 50 + k); }
        Hand(s, "Bloodline Keeper", 10);
        const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, true);
        bool ok = false;
        for (const TurnSolver::Plan& p : plans)
        {
            bool tr = false;
            for (const Action& a : p.actions) { if (IsMode(a, Action::AbilityMode::TransformSelf)) { tr = true; CHECK((a.needs_cast_mask & 1) != 0); } }
            if (!tr) { continue; }
            GameState r = s;
            TurnSolver::ApplyPlan(r, p, true);
            const Permanent* l = FindNum(r, 10);
            if (l != nullptr && l->card.m_name.str() == "Lord of Lineage") { ok = true; CHECK(l->entered_this_turn); }
        }
        CHECK(ok);
    }
}

TEST_CASE("Combat window: Mavren Fein's token makes the fifth Vampire; the pinned transform pumps THIS combat")
{
    EnsureCardsLoaded();
    GameState s = Fresh();
    Put(s, "Bloodline Keeper", 1);
    Put(s, "Mavren Fein, Dusk Apostle", 2);
    Put(s, "Falkenrath Gorger", 3);
    Put(s, "Vampire of the Dire Moon", 4);
    Put(s, "Swamp", 50);
    CHECK_FALSE(PermAbilitySourceLive(s, 0, 1, PermAbilityMode::TransformSelf));   // four
    // The axis offers the combat window (the count cannot reach five before combat).
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, true);
    bool fanned = false;
    for (const TurnSolver::Plan& p : plans) { if (p.combat_transform_choice >= 1) { fanned = true; } }
    CHECK(fanned);
    // Unpinned: the window holds.
    {
        GameState c = s;
        std::vector<int> atk;
        for (int i = 0; i < static_cast<int>(c.battlefield.size()); ++i)
        { if (c.battlefield[static_cast<std::size_t>(i)].card.m_number == 3) { atk.push_back(i); } }
        FireAttackOneOrMoreTokens(c, 0, atk);
        ApplyCombatTransforms(c, 0, atk);
        CHECK(FindNum(c, 1)->card.m_name.str() == "Bloodline Keeper");
    }
    // Pinned (bit 0 = the only Keeper): transforms after the token, pays {B}, the attacker gets +2/+2.
    {
        GameState c = s;
        c.scripted_combat_transform = 1;
        std::vector<int> atk;
        for (int i = 0; i < static_cast<int>(c.battlefield.size()); ++i)
        { if (c.battlefield[static_cast<std::size_t>(i)].card.m_number == 3) { atk.push_back(i); } }
        const int before = TotalPower(c, *FindNum(c, 3));
        FireAttackOneOrMoreTokens(c, 0, atk);
        ApplyCombatTransforms(c, 0, atk);
        CHECK(FindNum(c, 1)->card.m_name.str() == "Lord of Lineage");
        CHECK(FindNum(c, 50)->tapped);                       // {B} paid
        CHECK(TotalPower(c, *FindNum(c, 3)) == before + 2);
        CHECK(c.scripted_combat_transform == -1);            // consumed
    }
    // Pinned but no fifth Vampire (no Mavren attack): a clean no-op, nothing paid.
    {
        GameState c = s;
        c.scripted_combat_transform = 1;
        std::vector<int> atk;
        ApplyCombatTransforms(c, 0, atk);
        CHECK(FindNum(c, 1)->card.m_name.str() == "Bloodline Keeper");
        CHECK_FALSE(FindNum(c, 50)->tapped);
    }
}

// ---- 4. Captivating Vampire's steal ------------------------------------------------------------------

TEST_CASE("Steal legality: four untapped Vampires no; five with the source sick yes; no target no")
{
    EnsureCardsLoaded();
    GameState s = Fresh();
    Put(s, "Captivating Vampire", 1, /*sick=*/true);
    Put(s, "Vampire of the Dire Moon", 2, true);
    Put(s, "Legion Lieutenant", 3);
    Put(s, "Falkenrath Gorger", 4);
    Spawn(s, 4, 4, 500000);
    CHECK_FALSE(PermAbilitySourceLive(s, 0, 1, PermAbilityMode::TapSubtypeSteal));   // four
    CHECK(ModeActions(TurnSolver::EnumerateMainPlans(s, true), Action::AbilityMode::TapSubtypeSteal).empty());
    Put(s, "Stromkirk Captain", 5, true);
    CHECK(PermAbilitySourceLive(s, 0, 1, PermAbilityMode::TapSubtypeSteal));         // five, two of them sick
    CHECK_FALSE(ModeActions(TurnSolver::EnumerateMainPlans(s, true), Action::AbilityMode::TapSubtypeSteal).empty());
    GameState tapped = s;
    FindNumMut(tapped, 1)->tapped = true;
    CHECK_FALSE(PermAbilitySourceLive(tapped, 0, 1, PermAbilityMode::TapSubtypeSteal));   // only four untapped
    GameState none = s;
    none.battlefield.erase(std::remove_if(none.battlefield.begin(), none.battlefield.end(),
                                          [](const Permanent& p) { return p.controller_index == 1; }),
                           none.battlefield.end());
    CHECK_FALSE(PermAbilitySourceLive(none, 0, 1, PermAbilityMode::TapSubtypeSteal));
    CHECK(ModeActions(TurnSolver::EnumerateMainPlans(none, true), Action::AbilityMode::TapSubtypeSteal).empty());
}

TEST_CASE("Steal effect: five tapped (sick ones and itself), control changes, owner kept, a Vampire, sick")
{
    EnsureCardsLoaded();
    GameState s = Fresh();
    Put(s, "Captivating Vampire", 1, /*sick=*/true);
    Put(s, "Vampire of the Dire Moon", 2, true);
    Put(s, "Legion Lieutenant", 3);
    Put(s, "Falkenrath Gorger", 4, true);
    Put(s, "Stromkirk Captain", 5, true);
    Spawn(s, 4, 4, 500000);
    const std::vector<Action> steals = ModeActions(TurnSolver::EnumerateMainPlans(s, true),
                                                   Action::AbilityMode::TapSubtypeSteal);
    REQUIRE_FALSE(steals.empty());
    const Action& a = steals.front();
    CHECK(a.enchant_target == 500000);
    CHECK(a.steal_spec != 0);
    {
        const StealPinScope pin(a.enchant_target, a.steal_spec);
        REQUIRE(PermAbilitySourceLive(s, 0, 1, PermAbilityMode::TapSubtypeSteal));
        ApplyPermAbility(s, 0, 1, PermAbilityMode::TapSubtypeSteal);
    }
    for (int n : { 1, 2, 3, 4, 5 }) { CHECK(FindNum(s, n)->tapped); }
    const Permanent* got = FindNum(s, 500000);
    REQUIRE(got != nullptr);
    CHECK(got->controller_index == 0);
    CHECK(got->owner_index == 1);
    CHECK(got->gained_control_this_turn);
    CHECK_FALSE(got->tapped);
    CHECK(CardHasSubtype(got->card, "Vampire"));
    CHECK_FALSE(CanAttackFull(*got, s.battlefield, 0));
    // Lords reach it now: Captivating +1, Lieutenant +1, Captain +1.
    CHECK(TotalPower(s, *got) == 4 + 3);
    // Next turn it attacks (control-change sickness clears at our untap).
    Permanent next = *got;
    next.gained_control_this_turn = false;
    CHECK(CanAttackFull(next, s.battlefield, 0));
    // Sacrificed, it goes to its OWNER's graveyard.
    int idx = -1;
    for (int i = 0; i < static_cast<int>(s.battlefield.size()); ++i)
    { if (s.battlefield[static_cast<std::size_t>(i)].card.m_number == 500000) { idx = i; } }
    SacrificePermanentAt(s, 0, idx);
    CHECK(CountIn(s.players[1].graveyard, "4/4 Creature") == 1);
    CHECK(CountIn(s.players[0].graveyard, "4/4 Creature") == 0);
}

TEST_CASE("Steal folds: identical spawns are ONE target class; distinct targets / tap sets survive the dedup")
{
    EnsureCardsLoaded();
    GameState s = Fresh();
    s.phase = Phase::PostCombatMain;   // every untapped body is FREE (colour + live {T} classes)
    Put(s, "Captivating Vampire", 1);
    Put(s, "Vampire of the Dire Moon", 2);            // black
    Put(s, "Vampire of the Dire Moon", 3);            // black (folds with #2)
    Put(s, "Falkenrath Gorger", 4);                   // red
    Put(s, "Legion Lieutenant", 5);                   // white-black
    Put(s, "Stromkirk Captain", 6);                   // black-red
    Spawn(s, 1, 1, 500000); Spawn(s, 1, 1, 500001); Spawn(s, 1, 1, 500002);
    Spawn(s, 6, 6, 500003);
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, /*is_pre_combat=*/false);
    std::set<int> targets;
    std::set<std::string> specs;
    for (const Action& a : ModeActions(plans, Action::AbilityMode::TapSubtypeSteal))
    {
        targets.insert(a.enchant_target);
        specs.insert(StealSpecText(a.steal_spec));
    }
    CHECK(targets.size() == 2);                 // the three 1/1s fold to one class (rep 500000) + the 6/6
    CHECK(targets.count(500000) == 1);
    CHECK(targets.count(500003) == 1);
    // Six bodies, five tapped: one set per CLASS left one short. Captivating Vampire and the two Dire
    // Moons are one class (black, no live {T} ability -- tapping the source itself is no different),
    // so the sets are "leave out a black one / the Gorger / the Lieutenant / the Captain": 4, not 6.
    CHECK(specs.size() == 4);
    // The search sees both a steal of the 6/6 and of a 1/1 as different plans.
    int plans_6 = 0, plans_1 = 0;
    for (const TurnSolver::Plan& p : plans)
    {
        for (const Action& a : p.actions)
        {
            if (!IsMode(a, Action::AbilityMode::TapSubtypeSteal)) { continue; }
            if (a.enchant_target == 500003) { ++plans_6; } else { ++plans_1; }
        }
    }
    CHECK(plans_6 > 0);
    CHECK(plans_1 > 0);
}

TEST_CASE("Steal stale: a pinned body tapped earlier is a clean no-op (nothing tapped, nothing taken)")
{
    EnsureCardsLoaded();
    GameState s = Fresh();
    Put(s, "Captivating Vampire", 1, true);
    Put(s, "Vampire of the Dire Moon", 2, true);
    Put(s, "Legion Lieutenant", 3, true);
    Put(s, "Falkenrath Gorger", 4, true);
    Put(s, "Stromkirk Captain", 5, true);
    Spawn(s, 2, 2, 500000);
    const std::vector<Action> steals = ModeActions(TurnSolver::EnumerateMainPlans(s, true),
                                                   Action::AbilityMode::TapSubtypeSteal);
    REQUIRE_FALSE(steals.empty());
    FindNumMut(s, 4)->tapped = true;   // the red Gorger -- its class has no other member
    const StealPinScope pin(steals.front().enchant_target, steals.front().steal_spec);
    CHECK_FALSE(PermAbilitySourceLive(s, 0, 1, PermAbilityMode::TapSubtypeSteal));
}

TEST_CASE("Steal reachability: Vampires cast THIS plan pay the cost; a Captivating cast this plan taps itself")
{
    EnsureCardsLoaded();
    GameState s = Fresh();
    Put(s, "Legion Lieutenant", 3);
    Put(s, "Falkenrath Gorger", 4);
    Put(s, "Stromkirk Captain", 5);
    for (int k = 0; k < 4; ++k) { Put(s, "Swamp", 50 + k); }
    Hand(s, "Captivating Vampire", 10);
    Hand(s, "Vampire of the Dire Moon", 11);
    Spawn(s, 3, 3, 500000);
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, true);
    bool ok = false;
    for (const TurnSolver::Plan& p : plans)
    {
        bool st = false;
        for (const Action& a : p.actions) { if (IsMode(a, Action::AbilityMode::TapSubtypeSteal)) { st = true; } }
        if (!st) { continue; }
        GameState r = s;
        TurnSolver::ApplyPlan(r, p, true);
        const Permanent* got = FindNum(r, 500000);
        if (got != nullptr && got->controller_index == 0) { ok = true; CHECK(FindNum(r, 10)->tapped); }
    }
    CHECK(ok);
}

// ---- 5. lockstep ---------------------------------------------------------------------------------------

TEST_CASE("Lockstep: every steal / transform / token plan realises the same board in both worlds")
{
    EnsureCardsLoaded();
    for (bool main2 : { false, true })
    {
        GameState s = Fresh();
        s.phase = main2 ? Phase::PostCombatMain : Phase::PreCombatMain;
        Put(s, "Captivating Vampire", 1);
        Put(s, "Bloodline Keeper", 2);
        Put(s, "Vampire of the Dire Moon", 3, true);
        Put(s, "Legion Lieutenant", 4);
        Put(s, "Falkenrath Gorger", 5, true);
        Put(s, "Swamp", 50); Put(s, "Swamp", 51); Put(s, "Secluded Courtyard", 52);
        Hand(s, "Stromkirk Captain", 10);
        Spawn(s, 2, 2, 500000);
        Spawn(s, 6, 6, 500001);
        const long long unpinned_before = g_steal_unpinned.load();
        const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, /*is_pre_combat=*/!main2);
        int checked = 0, interesting_n = 0;
        for (const TurnSolver::Plan& line0 : plans)
        {
            TurnSolver::Plan line = line0;
            if (line.bp_choice < 0) { line.bp_choice = TurnSolver::kBpEmptyChoice; line.bp_all = true; }
            bool interesting = false;
            for (const Action& a : line.actions)
            {
                if (IsMode(a, Action::AbilityMode::TapSubtypeSteal) || IsMode(a, Action::AbilityMode::TransformSelf)
                    || a.kind == Action::Kind::TapForTokenPay) { interesting = true; }
            }
            if (!interesting) { continue; }
            ++interesting_n;
            GameState roll = s;
            {
                greedywindow::Frame frame(3);
                TurnSolver::ApplyPlan(roll, line, /*is_pre_combat=*/!main2);
            }
            AIEngine eng(MulliganProfile::DefaultProfile(), 3, 20);
            eng.SetSearchPostCombat(true);
            MtgTestSeam::CommitPhasePlan(eng, line, !main2);
            GameState exec = s;
            eng.TakeTurn(exec, !main2, ResolveAll);
            INFO("main2=" << main2 << " plan " << checked);
            CHECK(roll.battlefield.size() == exec.battlefield.size());
            for (const Permanent& p : roll.battlefield)
            {
                if (p.card.m_number >= 1000 && p.card.m_number < 500000) { continue; }   // token ids
                const Permanent* q = FindNum(exec, p.card.m_number);
                REQUIRE(q != nullptr);
                CHECK(q->tapped == p.tapped);
                CHECK(q->controller_index == p.controller_index);
                CHECK(q->card.m_name.str() == p.card.m_name.str());
                CHECK(CardHasSubtype(q->card, "Vampire") == CardHasSubtype(p.card, "Vampire"));
            }
            CHECK(roll.players[0].hand.size() == exec.players[0].hand.size());
            ++checked;
            if (checked >= 60) { break; }
        }
        CHECK(interesting_n > 0);
        CHECK(g_steal_unpinned.load() == unpinned_before);
    }
}

// ---- 6. routing -----------------------------------------------------------------------------------------

TEST_CASE("Routing: Vampires stays VampiresProvider with the cluster B cards in -- and on them alone")
{
    EnsureCardsLoaded();
    const Decklist full = DeckLoader::LoadFromFile(ResolveHeuristicDefaultsPath("decks/Vampires/Vampire.cod"));
    CHECK(std::string(DetectDecisionProvider(full).Name()) == "Vampires");
    // Cut EVERY A1/A2 signature card: Captivating Vampire's steal and Bloodline Keeper's transform
    // (both keyed on the Vampire subtype) still carry the route.
    for (bool keep_keeper : { true, false })
    {
        Decklist d = full;
        d.mainboard.erase(std::remove_if(d.mainboard.begin(), d.mainboard.end(), [&](const Card& c)
        {
            const std::string n = c.m_name.str();
            return n == "Falkenrath Pit Fighter" || n == "Stromkirk Condemned" || n == "Voldaren Estate"
                || n == "Bloodghast" || n == "Falkenrath Gorger"
                || (!keep_keeper && n == "Bloodline Keeper");
        }), d.mainboard.end());
        CHECK_MESSAGE(std::string(DetectDecisionProvider(d).Name()) == "Vampires", "keep_keeper=", keep_keeper);
    }
}
