// Enchanted Evening lock cluster, STEPS 3 and 5 (2026-11-26; docs/design/enchanted-evening-lock-design.md §4.3 / §4.5):
//   * Calming Verse (destroy_opp_enchantments_then_own_if_untapped_land): clause 1 destroys every enchantment we
//     don't control (under Enchanted Evening: every opposing permanent); clause 2, checked during resolution, destroys
//     ours if we control an untapped land. KEEP ("VK": tap every land into the float first) / SWEEP-SELF ("VS") is a
//     searched two-variant cast axis.
//   * Force of Vigor clause 2 (destroy_up_to_artifacts_enchantments 2): the hard cast; the target SET (0..2) is a
//     searched cast axis pinned "FV<n>[,<m>]"; own targets honour Sterling Grove's shroud (PermanentHasShroud).
// Every destroy goes through the leave chokepoints (an Oblivion Ring returns its card). Both worlds: the rollout's
// ApplyPlan and the executor's AIEngine::TakeTurn through the real GameEngine::ResolveStack.
#include <doctest/doctest.h>

#include "ai/AIEngine.h"
#include "ai/DecisionProviders.h"
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
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace
{
const char* const kEE    = "Enchanted Evening";
const char* const kVerse = "Calming Verse";
const char* const kFoV   = "Force of Vigor";
const char* const kExpl  = "Exploration";
const char* const kPres  = "Enchantress's Presence";
const char* const kGrove = "Sterling Grove";
const char* const kORing = "Oblivion Ring";

const CardDefinition& L5Def(const std::string& name)
{
    static const bool loaded = [] {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE_MESSAGE(d != nullptr, "card not in cards.json: ", name);
    return *d;
}

Card L5Card(const std::string& name, int number)
{
    Card c = L5Def(name).card;
    c.m_number = number;
    return c;
}

int L5Put(GameState& s, const std::string& name, int number, int controller = 0, bool tapped = false)
{
    Permanent p;
    p.card = L5Card(name, number);
    p.controller_index = p.owner_index = controller;
    p.tapped = tapped;
    s.battlefield.push_back(p);
    return static_cast<int>(s.battlefield.size()) - 1;
}

int L5Spawn(GameState& s, int pw, int number)
{
    Card c;
    c.m_name = std::to_string(pw) + "/" + std::to_string(pw) + " Creature";
    c.RehashName();
    c.AddType(CardType::Creature);
    c.m_power = pw;
    c.m_toughness = pw;
    c.m_number = number;
    Permanent p;
    p.card = c;
    p.controller_index = p.owner_index = 1;
    s.battlefield.push_back(p);
    return static_cast<int>(s.battlefield.size()) - 1;
}

GameState L5Board()
{
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 4;
    s.phase               = Phase::PreCombatMain;
    s.step                = Step::MainPhase;
    s.players[0].life     = gamesetup::StartingLife();
    s.players[1].life     = gamesetup::StartingLife();
    s.players[0].lands_played_this_turn = 1;
    s.deck_float_wild_exact = true;
    for (int k = 0; k < 8; ++k) { s.players[0].library.push_back(L5Card("Plains", 900 + k)); }
    for (int k = 0; k < 8; ++k) { s.players[1].library.push_back(L5Card("Plains", 100900 + k)); }
    return s;
}

const Permanent* L5Find(const GameState& s, int number)
{
    for (const Permanent& p : s.battlefield) { if (p.card.m_number == number) { return &p; } }
    return nullptr;
}

bool L5InGy(const GameState& s, int player, int number)
{
    for (const Card& c : s.players[static_cast<std::size_t>(player)].graveyard)
    { if (c.m_number == number) { return true; } }
    return false;
}

int L5Count(const GameState& s, int controller)
{
    int n = 0;
    for (const Permanent& p : s.battlefield) { if (p.controller_index == controller) { ++n; } }
    return n;
}

// The plan whose ONLY action is a cast of `name` pinned `pin`.
std::optional<TurnSolver::Plan> L5Plan(const std::vector<TurnSolver::Plan>& plans, const std::string& name,
                                       const std::string& pin)
{
    for (const TurnSolver::Plan& p : plans)
    {
        if (p.actions.size() != 1) { continue; }
        const Action& a = p.actions.front();
        if (a.kind == Action::Kind::CastFromHand && a.card_name.str() == name && a.tutor_target.str() == pin)
        { return p; }
    }
    return std::nullopt;
}

// Every distinct pin a single-cast plan of `name` carries.
std::set<std::string> L5Pins(const std::vector<TurnSolver::Plan>& plans, const std::string& name)
{
    std::set<std::string> out;
    for (const TurnSolver::Plan& p : plans)
    {
        if (p.actions.size() != 1) { continue; }
        const Action& a = p.actions.front();
        if (a.kind == Action::Kind::CastFromHand && a.card_name.str() == name) { out.insert(a.tutor_target.str()); }
    }
    return out;
}

struct L5Worlds
{
    AIEngine   eng{ MulliganProfile::DefaultProfile(), 3, 20 };
    GameEngine ge{ eng };
    GameState  roll;
    GameState  exec;
    void Run(const GameState& s, const TurnSolver::Plan& plan)
    {
        roll = s;
        TurnSolver::ApplyPlan(roll, plan, true);
        MtgTestSeam::CommitPhasePlan(eng, plan, true);
        exec = s;
        eng.TakeTurn(exec, true, [this](GameState& st) { MtgTestSeam::GeResolveStack(ge, st); });
    }
    void Check(const std::function<void(const GameState&)>& f) const
    {
        for (const GameState* g : { &roll, &exec })
        {
            INFO("world: " << std::string(g == &roll ? "rollout (ApplyPlan)" : "executor (AIEngine::TakeTurn)"));
            f(*g);
        }
    }
};
}   // namespace

TEST_CASE("Calming Verse / Force of Vigor: costs, types, params; routed to the deck's provider")
{
    const CardDefinition& v = L5Def(kVerse);
    CHECK(v.card.IsSorcery());
    CHECK(v.card.m_mana_cost.ManaValue() == 4);
    CHECK(v.card.m_mana_cost.green == 1);
    CHECK(v.params.destroy_opp_enchantments_then_own_if_untapped_land);
    CHECK_FALSE(v.params.destroy_all_enchantments);   // its OWN param (ManaPayment's wipe recheck keys on that one)
    const CardDefinition& f = L5Def(kFoV);
    CHECK(f.card.IsInstant());
    CHECK(f.card.m_mana_cost.ManaValue() == 4);
    CHECK(f.card.m_mana_cost.green == 2);
    CHECK(f.params.destroy_up_to_artifacts_enchantments == 2);
    for (const char* only : { kVerse, kFoV })
    {
        Decklist d;
        d.mainboard.push_back(L5Card(only, 1));
        d.mainboard.push_back(L5Card("Forest", 2));
        CAPTURE(only);
        CHECK(std::string(DetectDecisionProvider(d).Name()) == "EnchantedEvening");
    }
    CHECK(revpkg::CardCanEndOwnEnchantment(v.params));
    CHECK(revpkg::CardCanEndOwnEnchantment(f.params));
}

TEST_CASE("Calming Verse alone: KEEP and SWEEP-SELF are both searched variants; KEEP floats the lands and keeps the board")
{
    // Five Forests, our Exploration + Presence; an opponent Plains and spawn (neither an enchantment without EE).
    GameState s = L5Board();
    for (int k = 0; k < 5; ++k) { L5Put(s, "Forest", 101 + k); }
    L5Put(s, kExpl, 40);
    L5Put(s, kPres, 41);
    L5Put(s, "Plains", 100001, 1);
    L5Spawn(s, 2, 700001);
    s.players[0].hand.push_back(L5Card(kVerse, 30));
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, true);
    const std::set<std::string> pins = L5Pins(plans, kVerse);
    CHECK(pins == std::set<std::string>{ "VK", "VS" });
    const std::optional<TurnSolver::Plan> keep  = L5Plan(plans, kVerse, "VK");
    const std::optional<TurnSolver::Plan> sweep = L5Plan(plans, kVerse, "VS");
    REQUIRE(keep.has_value());
    REQUIRE(sweep.has_value());
    L5Worlds k;
    k.Run(s, *keep);
    k.Check([](const GameState& g) {
        CHECK(L5Find(g, 40) != nullptr);                     // ours survive: no untapped land at clause 2
        CHECK(L5Find(g, 41) != nullptr);
        CHECK(L5Find(g, 100001) != nullptr);                 // the opponent has no enchantment without EE
        CHECK(L5Find(g, 700001) != nullptr);
        CHECK(L5InGy(g, 0, 30));                             // the Verse resolved
        CHECK(g.floating_mana.green == 1);                   // the fifth Forest floated {G}
        for (const Permanent& p : g.battlefield) { if (p.card.IsLand() && p.controller_index == 0) { CHECK(p.tapped); } }
    });
    L5Worlds w;
    w.Run(s, *sweep);
    w.Check([](const GameState& g) {
        CHECK(L5Find(g, 40) == nullptr);                     // an untapped Forest remained: ours are destroyed
        CHECK(L5Find(g, 41) == nullptr);
        CHECK(L5InGy(g, 0, 40));
        CHECK(L5InGy(g, 0, 41));
        int forests = 0;
        for (const Permanent& p : g.battlefield) { if (p.card.IsLand() && p.controller_index == 0) { ++forests; } }
        CHECK(forests == 5);                                 // lands are not enchantments without EE
        CHECK(g.floating_mana.Total() == 0);
        CHECK(L5Find(g, 700001) != nullptr);
    });
}

TEST_CASE("Calming Verse under Enchanted Evening destroys every opposing permanent; KEEP keeps ours, SWEEP-SELF takes all")
{
    GameState s = L5Board();
    L5Put(s, kEE, 51);
    for (int k = 0; k < 5; ++k) { L5Put(s, "Forest", 101 + k); }
    L5Put(s, kExpl, 40);
    L5Put(s, "Plains", 100001, 1);
    L5Put(s, "Plains", 100002, 1, /*tapped=*/true);
    L5Spawn(s, 3, 700001);
    RefreshTypeStatics(s);
    for (const Permanent& p : s.battlefield) { CHECK(p.card.IsEnchantment()); }
    s.players[0].hand.push_back(L5Card(kVerse, 30));
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, true);
    const std::optional<TurnSolver::Plan> keep  = L5Plan(plans, kVerse, "VK");
    const std::optional<TurnSolver::Plan> sweep = L5Plan(plans, kVerse, "VS");
    REQUIRE(keep.has_value());
    REQUIRE(sweep.has_value());
    L5Worlds k;
    k.Run(s, *keep);
    k.Check([](const GameState& g) {
        CHECK(L5Count(g, 1) == 0);                           // every opposing permanent is gone
        CHECK(L5InGy(g, 1, 100001));                         // its lands to ITS graveyard
        CHECK(L5InGy(g, 1, 100002));
        CHECK_FALSE(L5InGy(g, 1, 700001));                   // the spawn is a token: it ceases to exist
        CHECK(L5Find(g, 51) != nullptr);                     // ours survive
        CHECK(L5Find(g, 40) != nullptr);
        CHECK(L5Count(g, 0) == 7);
    });
    L5Worlds w;
    w.Run(s, *sweep);
    w.Check([](const GameState& g) {
        CHECK(L5Count(g, 1) == 0);
        CHECK(L5Count(g, 0) == 0);                           // under EE everything of ours, lands included
        CHECK(L5InGy(g, 0, 51));
        CHECK(L5InGy(g, 0, 101));
        CHECK(L5InGy(g, 0, 40));
    });
}

TEST_CASE("Calming Verse with an untapped fetchland: KEEP cannot float it (clause 2 fires); cracking it first keeps")
{
    GameState s = L5Board();
    for (int k = 0; k < 5; ++k) { L5Put(s, "Forest", 101 + k); }
    L5Put(s, "Windswept Heath", 120);
    L5Put(s, kExpl, 40);
    s.players[0].hand.push_back(L5Card(kVerse, 30));
    // Both variants are offered (the line may crack the fetch first; the emission state cannot know).
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, true);
    CHECK(L5Pins(plans, kVerse) == std::set<std::string>{ "VK", "VS" });
    // The resolver, after paying with four Forests: KEEP floats the fifth Forest, but the Heath has no mana ability,
    // stays untapped, and clause 2 destroys Exploration (the literal result).
    auto paid = [&]() {
        GameState g = s;
        for (Permanent& p : g.battlefield) { if (p.card.m_number >= 101 && p.card.m_number <= 104) { p.tapped = true; } }
        return g;
    };
    for (const char* pin : { "VK", "VS" })
    {
        CAPTURE(pin);
        GameState g = paid();
        ResolveCalmingVerse(g, 0, L5Def(kVerse), pin);
        CHECK(L5Find(g, 40) == nullptr);
        CHECK(L5Find(g, 120) != nullptr);                    // a land, not an enchantment without EE
        CHECK(g.floating_mana.green == (std::string(pin) == "VK" ? 1 : 0));
    }
    // The fetch cracked earlier in the line (its land entered untapped): KEEP floats it too and Exploration lives.
    GameState c = paid();
    c.battlefield.erase(std::remove_if(c.battlefield.begin(), c.battlefield.end(),
                                       [](const Permanent& p) { return p.card.m_number == 120; }),
                        c.battlefield.end());
    L5Put(c, "Plains", 121);
    ResolveCalmingVerse(c, 0, L5Def(kVerse), "VK");
    CHECK(L5Find(c, 40) != nullptr);
    CHECK(c.floating_mana.Total() == 2);
}

TEST_CASE("Calming Verse's own-land clause: no untapped land at resolution -> ours survive even unkept")
{
    GameState s = L5Board();
    for (int k = 0; k < 4; ++k) { L5Put(s, "Forest", 101 + k, 0, /*tapped=*/true); }
    L5Put(s, kExpl, 40);
    ResolveCalmingVerse(s, 0, L5Def(kVerse), "VS");
    CHECK(L5Find(s, 40) != nullptr);
    // ...and one untapped land is enough to take them.
    GameState t = L5Board();
    for (int k = 0; k < 4; ++k) { L5Put(t, "Forest", 101 + k, 0, /*tapped=*/true); }
    L5Put(t, "Plains", 105);
    L5Put(t, kExpl, 40);
    ResolveCalmingVerse(t, 0, L5Def(kVerse), "VS");
    CHECK(L5Find(t, 40) == nullptr);
    // The unpinned cast (a rollout leaf) takes the provider default: KEEP.
    GameState u = L5Board();
    L5Put(u, "Plains", 105);
    L5Put(u, kExpl, 40);
    ResolveCalmingVerse(u, 0, L5Def(kVerse), "");
    CHECK(L5Find(u, 40) != nullptr);
    CHECK(u.floating_mana.white == 1);
}

TEST_CASE("Calming Verse SWEEP-SELF destroys our Oblivion Ring through the leave chokepoint: its card returns")
{
    GameState s = L5Board();
    L5Spawn(s, 2, 700001);
    L5Put(s, "Plains", 105);
    L5Put(s, kORing, 30);
    ResolveEtbExileNonlandUntilLeaves(s, 0, 30, kORing, ExileUntilLeavesPin(700001));
    REQUIRE(L5Find(s, 700001) == nullptr);
    ResolveCalmingVerse(s, 0, L5Def(kVerse), "VS");
    CHECK(L5Find(s, 30) == nullptr);
    CHECK(L5InGy(s, 0, 30));
    const Permanent* back = L5Find(s, 700001);
    REQUIRE(back != nullptr);                                 // returned under its owner
    CHECK(back->controller_index == 1);
}

TEST_CASE("Force of Vigor hard cast: 0, 1 and 2 target sets are each a searched variant (both worlds)")
{
    GameState s = L5Board();
    L5Put(s, kEE, 51);
    for (int k = 0; k < 4; ++k) { L5Put(s, "Forest", 101 + k); }
    L5Put(s, "Plains", 100001, 1);
    L5Put(s, "Mountain", 100002, 1);
    L5Spawn(s, 2, 700001);
    RefreshTypeStatics(s);
    s.players[0].hand.push_back(L5Card(kFoV, 30));
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, true);
    const std::set<std::string> pins = L5Pins(plans, kFoV);
    CHECK(pins.count("FV") == 1);                             // "up to": no target is a legal option
    CHECK(pins.count(DestroyUpToArtEnchPin({ 100001 })) == 1);
    CHECK(pins.count(DestroyUpToArtEnchPin({ 700001 })) == 1);
    CHECK(pins.count(DestroyUpToArtEnchPin({ 51 })) == 1);   // our own EE is a legal target too
    bool pair = false;
    for (const std::string& p : pins)
    {
        std::vector<int> n;
        REQUIRE(DestroyUpToArtEnchPinTargets(p, n));
        CHECK(n.size() <= 2);
        if (n.size() == 2 && std::find(n.begin(), n.end(), 100001) != n.end()
            && std::find(n.begin(), n.end(), 100002) != n.end()) { pair = true; }
    }
    CHECK(pair);
    CHECK_FALSE(pins.count(""));                              // the decision space carries no unpinned cast
    // Two pinned targets: both destroyed together, in both worlds; ours untouched.
    std::optional<TurnSolver::Plan> two;
    for (const std::string& p : pins)
    {
        std::vector<int> n;
        DestroyUpToArtEnchPinTargets(p, n);
        if (n.size() == 2 && std::find(n.begin(), n.end(), 100001) != n.end()
            && std::find(n.begin(), n.end(), 100002) != n.end()) { two = L5Plan(plans, kFoV, p); }
    }
    REQUIRE(two.has_value());
    L5Worlds w2;
    w2.Run(s, *two);
    w2.Check([](const GameState& g) {
        CHECK(L5Find(g, 100001) == nullptr);
        CHECK(L5Find(g, 100002) == nullptr);
        CHECK(L5InGy(g, 1, 100001));
        CHECK(L5Find(g, 700001) != nullptr);
        CHECK(L5Find(g, 51) != nullptr);
        CHECK(L5InGy(g, 0, 30));
    });
    const std::optional<TurnSolver::Plan> one = L5Plan(plans, kFoV, DestroyUpToArtEnchPin({ 700001 }));
    REQUIRE(one.has_value());
    L5Worlds w1;
    w1.Run(s, *one);
    w1.Check([](const GameState& g) {
        CHECK(L5Find(g, 700001) == nullptr);
        CHECK_FALSE(L5InGy(g, 1, 700001));                    // a token ceases to exist
        CHECK(L5Find(g, 100001) != nullptr);
        CHECK(L5Find(g, 100002) != nullptr);
    });
    const std::optional<TurnSolver::Plan> none = L5Plan(plans, kFoV, "FV");
    REQUIRE(none.has_value());
    L5Worlds w0;
    w0.Run(s, *none);
    w0.Check([](const GameState& g) {
        CHECK(L5Count(g, 1) == 3);
        CHECK(L5Find(g, 51) != nullptr);
        CHECK(L5InGy(g, 0, 30));
    });
    // Without Enchanted Evening the opponent has no artifact or enchantment: only our own enchantments are legal.
    GameState t = L5Board();
    for (int k = 0; k < 4; ++k) { L5Put(t, "Forest", 101 + k); }
    L5Put(t, "Plains", 100001, 1);
    L5Put(t, kExpl, 40);
    const std::vector<std::vector<int>> opts = DestroyUpToArtEnchOptions(t, 0, 2);
    REQUIRE(opts.size() == 2);                                // {} and {Exploration}
    CHECK(opts[0].empty());
    // The unpinned (rollout-leaf) pick never destroys our own: the empty set here.
    CHECK(ResolveProvider(t).DestroyUpToArtEnchPick(t, 0, opts) == 0);
    // A pin is parsed exactly.
    std::vector<int> n;
    CHECK(DestroyUpToArtEnchPinTargets("FV12,345", n));
    CHECK(n == std::vector<int>{ 12, 345 });
    CHECK_FALSE(DestroyUpToArtEnchPinTargets("VK", n));
    CHECK_FALSE(DestroyUpToArtEnchPinTargets("FV1,", n));
}

TEST_CASE("Force of Vigor: Sterling Grove's shroud removes our own permanents from its legal set (not the Grove itself)")
{
    GameState s = L5Board();
    L5Put(s, kEE, 51);
    for (int k = 0; k < 4; ++k) { L5Put(s, "Forest", 101 + k); }
    L5Put(s, kGrove, 60);
    L5Put(s, "Plains", 100001, 1);
    RefreshTypeStatics(s);
    std::set<int> legal;
    for (const auto& o : DestroyUpToArtEnchOptions(s, 0, 2))
    { for (int bi : o) { legal.insert(s.battlefield[static_cast<std::size_t>(bi)].card.m_number); } }
    CHECK(legal == std::set<int>{ 60, 100001 });              // the Grove and the opponent's land only
    // A target that gains shroud before resolution is unaffected (CR 608.2b).
    GameState t = L5Board();
    L5Put(t, kExpl, 40);
    L5Put(t, kGrove, 60);
    ResolveDestroyUpToArtEnch(t, 0, L5Def(kFoV), DestroyUpToArtEnchPin({ 40 }));
    CHECK(L5Find(t, 40) != nullptr);
    ResolveDestroyUpToArtEnch(t, 0, L5Def(kFoV), DestroyUpToArtEnchPin({ 60, 40 }));
    CHECK(L5Find(t, 60) == nullptr);                          // the Grove itself was legal
    CHECK(L5Find(t, 40) != nullptr);                          // (Exploration was shrouded when it was chosen)
}

TEST_CASE("Force of Vigor destroying an Oblivion Ring returns the exiled card (both worlds)")
{
    GameState s = L5Board();
    for (int k = 0; k < 4; ++k) { L5Put(s, "Forest", 101 + k); }
    L5Spawn(s, 2, 700001);
    L5Put(s, kORing, 35);
    ResolveEtbExileNonlandUntilLeaves(s, 0, 35, kORing, ExileUntilLeavesPin(700001));
    REQUIRE(L5Find(s, 700001) == nullptr);
    s.players[0].hand.push_back(L5Card(kFoV, 30));
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, true);
    const std::optional<TurnSolver::Plan> p = L5Plan(plans, kFoV, DestroyUpToArtEnchPin({ 35 }));
    REQUIRE(p.has_value());
    L5Worlds w;
    w.Run(s, *p);
    w.Check([](const GameState& g) {
        CHECK(L5Find(g, 35) == nullptr);
        CHECK(L5InGy(g, 0, 35));
        const Permanent* back = L5Find(g, 700001);
        REQUIRE(back != nullptr);
        CHECK(back->controller_index == 1);
    });
}

TEST_CASE("EnchantedEveningProvider folds: a no-op Verse KEEP and a target-less Force of Vigor are not offered")
{
    static const EnchantedEveningProvider ee_prov;
    // No enchantment we don't control (no EE): KEEP would destroy nothing -> only SWEEP-SELF is offered.
    GameState s = L5Board();
    s.m_provider = &ee_prov;
    for (int k = 0; k < 5; ++k) { L5Put(s, "Forest", 101 + k); }
    L5Put(s, kExpl, 40);
    L5Put(s, "Plains", 100001, 1);
    s.players[0].hand.push_back(L5Card(kVerse, 30));
    CHECK(L5Pins(TurnSolver::EnumerateMainPlans(s, true), kVerse) == std::set<std::string>{ "VS" });
    // The generic provider still offers both (the fold is this deck's).
    GameState g = s;
    g.m_provider = nullptr;
    CHECK(L5Pins(TurnSolver::EnumerateMainPlans(g, true), kVerse) == std::set<std::string>{ "VK", "VS" });
    // Under EE every opposing permanent is an enchantment: KEEP is back.
    GameState e = s;
    L5Put(e, kEE, 51);
    RefreshTypeStatics(e);
    CHECK(L5Pins(TurnSolver::EnumerateMainPlans(e, true), kVerse) == std::set<std::string>{ "VK", "VS" });

    // Force of Vigor: never the empty set under this provider...
    GameState f = L5Board();
    f.m_provider = &ee_prov;
    L5Put(f, kEE, 51);
    for (int k = 0; k < 4; ++k) { L5Put(f, "Forest", 101 + k); }
    L5Put(f, "Plains", 100001, 1);
    RefreshTypeStatics(f);
    f.players[0].hand.push_back(L5Card(kFoV, 30));
    const std::set<std::string> fp = L5Pins(TurnSolver::EnumerateMainPlans(f, true), kFoV);
    CHECK_FALSE(fp.empty());
    CHECK(fp.count("FV") == 0);
    CHECK(fp.count(DestroyUpToArtEnchPin({ 100001 })) == 1);
    // ...and with no legal target at all (no artifact or enchantment anywhere) it is not offered.
    GameState n = L5Board();
    n.m_provider = &ee_prov;
    for (int k = 0; k < 4; ++k) { L5Put(n, "Forest", 101 + k); }
    L5Put(n, "Plains", 100001, 1);
    n.players[0].hand.push_back(L5Card(kFoV, 30));
    CHECK(L5Pins(TurnSolver::EnumerateMainPlans(n, true), kFoV).empty());
    GameState ng = n;
    ng.m_provider = nullptr;
    CHECK(L5Pins(TurnSolver::EnumerateMainPlans(ng, true), kFoV) == std::set<std::string>{ "FV" });
}
