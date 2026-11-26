// ELVES PROVIDER (Stage 4a, 2026-11-26, docs/design/analysis-Elves.md "Provider heuristics"):
//
//  1. ROUTING: decks/Elves routes to ElvesProvider (it rode SelesnyaLifegainProvider through Genesis Wave's
//     reveal_x_put_permanents); SelesnyaLifegain and AccomplishedAlchemist are unchanged. The every-deck table is
//     test_pirates_provider.cpp's.
//  2. THE USER'S SCALING-SINK RULE (ElvesScalingAdmit through EnumerateMainPlans): one sink scaled to the max the
//     plan's other casts leave, hold-back counts for the 1-mana attackers, no partial splits across two sinks; the
//     idle-adjacent / unscaled plans untouched.
//  3. THE LIFT SWITCHES: MTG_ELVES_SCALING_PRUNE=0 (heurarm, per job) restores every count and every split;
//     MTG_UNPRUNED / MTG_UNPRUNE=replicate|xspell and human play are process-static, so their case asserts the
//     contract in whichever process runs it (run once under MTG_UNPRUNED=1 and once under MTG_HUMAN_PLAY=1 to
//     exercise the open branch).
#include <doctest/doctest.h>
#include "ai/DecisionProviders.h"
#include "ai/HeuristicArm.h"
#include "ai/TurnSolver.h"
#include "cards/CardDatabase.h"
#include "core/GameLogger.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"
#include "deck/DeckLoader.h"

#include <algorithm>
#include <filesystem>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace {
const CardDefinition& PD(const std::string& name)
{
    static const bool loaded = [] { CardDatabase::Instance().LoadFromJson(
        ResolveHeuristicDefaultsPath("src/cards/data/cards.json")); return true; }();
    REQUIRE(loaded);
    const auto* d = CardDatabase::Instance().Lookup(name);
    REQUIRE_MESSAGE(d != nullptr, "card not in cards.json: ", name);
    return *d;
}
Card PCard(const std::string& name, int id) { Card c = PD(name).card; c.m_number = id; return c; }
void PPut(GameState& s, const std::string& name, int id, bool sick = false)
{
    Permanent p; p.card = PCard(name, id); p.controller_index = 0; p.owner_index = 0;
    p.entered_this_turn = sick; s.battlefield.push_back(p);
}
void PHand(GameState& s, const std::string& name, int id) { s.players[0].hand.push_back(PCard(name, id)); }

const ElvesProvider& Prov() { static const ElvesProvider p; return p; }

// 7 Forests + an untapped, unsick Llanowar Elves (8 mana, one 1-mana attacker); hand Warcaller, Brigade, Wave.
// Affordable maxima: Warcaller alone K 3; Brigade alone squad 2; Wave alone X 5; Warcaller(K0) + Wave X 4;
// Warcaller(K0) + Brigade squad 2; Brigade(0) + Wave X 2.
GameState PBase()
{
    GameState s; s.active_player_index = 0; s.turn_number = 5; s.game_seed = 4242;
    s.players[0].life = 20; s.players[1].life = 20;
    s.players[0].lands_played_this_turn = 1;   // no land drop
    for (int i = 0; i < 12; ++i) { s.players[0].library.push_back(PCard(i % 2 ? "Forest" : "Llanowar Elves", 900 + i)); }
    for (int i = 0; i < 7; ++i) { PPut(s, "Forest", 100 + i); }
    PPut(s, "Llanowar Elves", 200);
    PHand(s, "Joraga Warcaller", 1);
    PHand(s, "Galadhrim Brigade", 2);
    PHand(s, "Genesis Wave", 3);
    s.m_provider = &Prov();
    return s;
}

// One plan's casts: name -> its count (kick_count for Warcaller / Brigade, X for Wave). Activations ignored.
std::vector<std::pair<std::string, int>> Casts(const TurnSolver::Plan& p)
{
    std::vector<std::pair<std::string, int>> out;
    for (const Action& a : p.actions)
    {
        if (a.kind != Action::Kind::CastFromHand) { continue; }
        const std::string n = a.card_name.str();
        out.emplace_back(n, n == "Genesis Wave" ? a.chosen_x : static_cast<int>(a.kick_count));
    }
    std::sort(out.begin(), out.end());
    return out;
}
// Every distinct cast multiset in a plan list.
std::set<std::vector<std::pair<std::string, int>>> Menu(const std::vector<TurnSolver::Plan>& plans)
{
    std::set<std::vector<std::pair<std::string, int>>> m;
    for (const TurnSolver::Plan& p : plans) { m.insert(Casts(p)); }
    return m;
}
using CastSet = std::vector<std::pair<std::string, int>>;
int ScaledSinks(const CastSet& c)
{
    int n = 0;
    for (const auto& [name, k] : c)
    {
        if (name == "Genesis Wave" || ((name == "Joraga Warcaller" || name == "Galadhrim Brigade") && k > 0)) { ++n; }
    }
    return n;
}

struct PruneArm
{
    std::int8_t saved;
    explicit PruneArm(int v) : saved(heurarm::t_arm[heurarm::ELVES_SCALING_PRUNE])
    { heurarm::t_arm[heurarm::ELVES_SCALING_PRUNE] = static_cast<std::int8_t>(v); }
    ~PruneArm() { heurarm::t_arm[heurarm::ELVES_SCALING_PRUNE] = saved; }
};

struct AmendArm
{
    std::int8_t saved;
    explicit AmendArm(int v) : saved(heurarm::t_arm[heurarm::ELVES_SCALING_AMEND])
    { heurarm::t_arm[heurarm::ELVES_SCALING_AMEND] = static_cast<std::int8_t>(v); }
    ~AmendArm() { heurarm::t_arm[heurarm::ELVES_SCALING_AMEND] = saved; }
};

bool RuleLive() { return !HumanPlayActive() && !DecisionUnpruned(UnprunedGate::Replicate) && !DecisionUnpruned(UnprunedGate::XSpell); }

// The full (rule-lifted) menu's facts this board must show, whatever lifted it.
void CheckFullMenu(const std::set<CastSet>& m)
{
    CHECK(m.count(CastSet{ { "Joraga Warcaller", 1 } }) == 1);              // a low kick count
    CHECK(m.count(CastSet{ { "Genesis Wave", 3 } }) == 1);                  // a low X
    CHECK(m.count(CastSet{ { "Genesis Wave", 2 }, { "Joraga Warcaller", 1 } }) == 1);   // a split
}
}   // namespace

// ---- 1. ROUTING ------------------------------------------------------------------------------------------------

TEST_CASE("Elves routing: decks/Elves -> ElvesProvider; SelesnyaLifegain and AccomplishedAlchemist unchanged")
{
    (void)PD("Forest");
    namespace fs = std::filesystem;
    const fs::path root = fs::path(ResolveHeuristicDefaultsPath("decks"));
    const std::vector<std::pair<std::string, std::string>> want = {
        { "Elves/Elves.cod", "Elves" },
    };
    for (const auto& [rel, name] : want)
    {
        const fs::path deck = root / rel;
        REQUIRE_MESSAGE(fs::exists(deck), deck.string());
        CHECK(std::string(SelectDecisionProvider(DeckLoader::LoadFromFile(deck)).Name()) == name);
    }
    // The two decks the Elves params could plausibly reach (same Elf / Genesis Wave shell).
    for (const auto& e : fs::directory_iterator(root))
    {
        const std::string folder = e.path().filename().string();
        if (folder != "Selesnya Lifegain" && folder != "Accomplished Alchemist") { continue; }
        for (const auto& f : fs::directory_iterator(e.path()))
        {
            const std::string ext = f.path().extension().string();
            if (ext != ".cod" && ext != ".txt") { continue; }
            if (f.path().stem().string() != e.path().filename().string()) { continue; }
            const std::string got = SelectDecisionProvider(DeckLoader::LoadFromFile(f.path())).Name();
            CHECK_MESSAGE(got != "Elves", folder, " captured by the Elves signature");
        }
    }
    CHECK(std::string(Prov().Name()) == "Elves");
    CHECK(Prov().Certificate().state == CertState::NotAssessed);
}

// ---- 2. THE RULE -----------------------------------------------------------------------------------------------

TEST_CASE("Elves scaling rule: hold-back attackers -- untapped unsick 1-mana dorks, Crossroads haste, pre-combat, cap 2")
{
    GameState s = PBase();
    CHECK(ElvesHoldBackAttackers(s, true) == 1);              // the Llanowar
    CHECK(ElvesHoldBackAttackers(s, false) == 0);             // post-combat: the attack is over
    PPut(s, "Elvish Mystic", 201, /*sick=*/true);
    CHECK(ElvesHoldBackAttackers(s, true) == 1);              // a summoning-sick Mystic cannot attack
    PPut(s, "Priest of Titania", 202);
    CHECK(ElvesHoldBackAttackers(s, true) == 1);              // a scaled dork is not a 1-mana producer
    PPut(s, "Concordant Crossroads", 203);
    CHECK(ElvesHoldBackAttackers(s, true) == 2);              // Crossroads hastes the Mystic
    PPut(s, "Llanowar Elves", 204);
    CHECK(ElvesHoldBackAttackers(s, true) == 2);              // capped at 2
}

TEST_CASE("Elves scaling rule: one sink at the max after the other casts, hold-back counts, no splits")
{
    if (!RuleLive()) { return; }   // the unpruned / human process: the lift case below covers it
    PruneArm on(1);
    GameState s = PBase();
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlansExhaustive(s, true, nullptr, nullptr);
    const std::set<CastSet> m = Menu(plans);
    // (1) no plan scales two sinks
    for (const CastSet& c : m) { CHECK_MESSAGE(ScaledSinks(c) <= 1, "split survived"); }
    // (2)+(3) one Llanowar can attack: h = 1 step for Wave (1 mana a step) and for a kick (2 mana a step).
    CHECK(m.count(CastSet{ { "Joraga Warcaller", 3 } }) == 1);
    CHECK(m.count(CastSet{ { "Joraga Warcaller", 2 } }) == 1);
    CHECK(m.count(CastSet{ { "Joraga Warcaller", 1 } }) == 0);
    CHECK(m.count(CastSet{ { "Galadhrim Brigade", 2 } }) == 1);
    CHECK(m.count(CastSet{ { "Galadhrim Brigade", 1 } }) == 1);
    CHECK(m.count(CastSet{ { "Genesis Wave", 5 } }) == 1);
    CHECK(m.count(CastSet{ { "Genesis Wave", 4 } }) == 1);
    CHECK(m.count(CastSet{ { "Genesis Wave", 3 } }) == 0);
    CHECK(m.count(CastSet{ { "Genesis Wave", 2 } }) == 0);
    // "Max after the plan's other casts": Wave beside an UNSCALED Warcaller is kept at 8 - 1 - 3 = 4 (and the
    // hold-back 3), not at the Wave-alone max.
    CHECK(m.count(CastSet{ { "Genesis Wave", 4 }, { "Joraga Warcaller", 0 } }) == 1);
    CHECK(m.count(CastSet{ { "Genesis Wave", 3 }, { "Joraga Warcaller", 0 } }) == 1);
    CHECK(m.count(CastSet{ { "Genesis Wave", 2 }, { "Joraga Warcaller", 0 } }) == 0);
    // Unscaled plans untouched: an unkicked Warcaller alone, Warcaller K0 + Brigade at its max beside it.
    CHECK(m.count(CastSet{ { "Joraga Warcaller", 0 } }) == 1);
    CHECK(m.count(CastSet{ { "Galadhrim Brigade", 2 }, { "Joraga Warcaller", 0 } }) == 1);
    CHECK(m.count(CastSet{ { "Galadhrim Brigade", 0 }, { "Joraga Warcaller", 0 } }) == 1);
}

TEST_CASE("Elves scaling rule: post-combat (and no attacker) keeps the top count only")
{
    if (!RuleLive()) { return; }
    // The rule-lifted list run through the rule with an explicit phase: post-combat, h = 0.
    std::vector<TurnSolver::Plan> plans;
    GameState s = PBase();
    { PruneArm off(0); plans = TurnSolver::EnumerateMainPlansExhaustive(s, true, nullptr, nullptr); }
    std::vector<DecisionProvider::PlanView> views;
    for (const TurnSolver::Plan& p : plans) { views.push_back({ &p.actions, &p.land_to_play, &p }); }
    std::vector<char> admit(plans.size(), 1);
    ElvesScalingAdmit(s, /*is_pre_combat=*/false, views, admit);
    std::vector<TurnSolver::Plan> kept;
    for (std::size_t i = 0; i < plans.size(); ++i) { if (admit[i]) { kept.push_back(plans[i]); } }
    const std::set<CastSet> m = Menu(kept);
    CHECK(m.count(CastSet{ { "Joraga Warcaller", 3 } }) == 1);
    CHECK(m.count(CastSet{ { "Joraga Warcaller", 2 } }) == 0);
    CHECK(m.count(CastSet{ { "Genesis Wave", 5 } }) == 1);
    CHECK(m.count(CastSet{ { "Genesis Wave", 4 } }) == 0);
    CHECK(m.count(CastSet{ { "Genesis Wave", 4 }, { "Joraga Warcaller", 0 } }) == 1);
    CHECK(m.count(CastSet{ { "Joraga Warcaller", 0 } }) == 1);
    for (const CastSet& c : m) { CHECK(ScaledSinks(c) <= 1); }
    // Every plan with no scaled sink survives, untouched.
    std::size_t unscaled_before = 0, unscaled_after = 0;
    for (const TurnSolver::Plan& p : plans) { unscaled_before += ScaledSinks(Casts(p)) == 0; }
    for (const TurnSolver::Plan& p : kept) { unscaled_after += ScaledSinks(Casts(p)) == 0; }
    CHECK(unscaled_before == unscaled_after);
    CHECK(unscaled_before > 0);
}

TEST_CASE("Elves scaling rule (amended): the hold-back sources are every attack-capable mana creature, by live yield")
{
    GameState s = PBase();
    PPut(s, "Priest of Titania", 202);                       // 2 Elves on the battlefield: taps for {G}{G}
    PPut(s, "Elvish Archdruid", 203, /*sick=*/true);          // summoning-sick: cannot attack, not a source
    PPut(s, "Marwyn, the Nurturer", 204, /*sick=*/true);
    std::vector<ElvesHoldBackSource> src = ElvesHoldBackSources(s, true);
    REQUIRE(src.size() == 2);                                 // the Llanowar and the Priest
    // The Priest counts every Elf on the battlefield (4 now, sick ones included); the Llanowar makes 1.
    CHECK(ElvesHoldBackMana(src, 0) == 4 + 1);
    CHECK(ElvesHoldBackMana(src, 2) == 6 + 1);                // two Elf casts in the plan grow the Priest's tap
    CHECK(ElvesHoldBackSources(s, false).empty());            // post-combat: the attack is over
    PPut(s, "Concordant Crossroads", 205);                    // haste: Archdruid and Marwyn can attack now
    src = ElvesHoldBackSources(s, true);
    CHECK(src.size() == 4);
    // Priest 4, Archdruid 4 (Elves you control: Llanowar, Priest, Archdruid, Marwyn), Marwyn 2 (1 + the Archdruid's
    // +1), Llanowar 1: the two largest are 4 + 4.
    CHECK(ElvesHoldBackMana(src, 0) == 4 + 4);
}

TEST_CASE("Elves scaling rule (amended): a held scaled dork widens the band by its yield; =0 keeps the 1-mana band")
{
    if (!RuleLive()) { return; }
    PruneArm on(1);
    GameState s = PBase();
    PPut(s, "Priest of Titania", 202);                        // 7 Forests + Llanowar 1 + Priest 2 = 10 mana
    s.players[0].hand.clear();
    PHand(s, "Joraga Warcaller", 1);
    std::set<CastSet> m;
    {
        AmendArm amend(1);
        m = Menu(TurnSolver::EnumerateMainPlansExhaustive(s, true, nullptr, nullptr));
    }
    // Top K 4 ((10 - 1) / 2). Holding back the Priest and the Llanowar frees 3 mana: ceil(3 / 2) = 2 kicks down.
    CHECK(m.count(CastSet{ { "Joraga Warcaller", 4 } }) == 1);
    CHECK(m.count(CastSet{ { "Joraga Warcaller", 3 } }) == 1);
    CHECK(m.count(CastSet{ { "Joraga Warcaller", 2 } }) == 1);
    CHECK(m.count(CastSet{ { "Joraga Warcaller", 1 } }) == 0);
    CHECK(m.count(CastSet{ { "Joraga Warcaller", 0 } }) == 1);   // the unscaled cast is never touched
    {
        AmendArm amend(0);
        m = Menu(TurnSolver::EnumerateMainPlansExhaustive(s, true, nullptr, nullptr));
    }
    // The first cut: only the Llanowar counts, one kick down.
    CHECK(m.count(CastSet{ { "Joraga Warcaller", 4 } }) == 1);
    CHECK(m.count(CastSet{ { "Joraga Warcaller", 3 } }) == 1);
    CHECK(m.count(CastSet{ { "Joraga Warcaller", 2 } }) == 0);
    // Post-combat the amended band is the top alone, as before.
    {
        AmendArm amend(1);
        PruneArm off(0);
        const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlansExhaustive(s, true, nullptr, nullptr);
        std::vector<DecisionProvider::PlanView> views;
        for (const TurnSolver::Plan& p : plans) { views.push_back({ &p.actions, &p.land_to_play, &p }); }
        std::vector<char> admit(plans.size(), 1);
        ElvesScalingAdmit(s, /*is_pre_combat=*/false, views, admit);
        std::vector<TurnSolver::Plan> kept;
        for (std::size_t i = 0; i < plans.size(); ++i) { if (admit[i]) { kept.push_back(plans[i]); } }
        const std::set<CastSet> mk = Menu(kept);
        CHECK(mk.count(CastSet{ { "Joraga Warcaller", 4 } }) == 1);
        CHECK(mk.count(CastSet{ { "Joraga Warcaller", 3 } }) == 0);
    }
}

// ---- 3. THE LIFT SWITCHES ----------------------------------------------------------------------------------------

TEST_CASE("Elves scaling rule: MTG_ELVES_SCALING_PRUNE=0 restores every count and every split")
{
    PruneArm off(0);
    GameState s = PBase();
    CHECK_FALSE(Prov().PlanListFilterLive(s));
    CheckFullMenu(Menu(TurnSolver::EnumerateMainPlansExhaustive(s, true, nullptr, nullptr)));
}

TEST_CASE("Elves scaling rule: MTG_UNPRUNED / MTG_UNPRUNE=replicate|xspell and human play see the full menu")
{
    // Process-static: asserts whichever branch this process runs.
    PruneArm on(1);
    GameState s = PBase();
    const std::set<CastSet> m = Menu(TurnSolver::EnumerateMainPlansExhaustive(s, true, nullptr, nullptr));
    if (RuleLive()) { CHECK(m.count(CastSet{ { "Joraga Warcaller", 1 } }) == 0); }
    else            { CheckFullMenu(m); }
}

TEST_CASE("Elves scaling rule: not live with no sink in hand (the lazy layout is left alone)")
{
    PruneArm on(1);
    GameState s = PBase();
    s.players[0].hand.clear();
    PHand(s, "Elvish Clancaller", 5);
    CHECK_FALSE(Prov().PlanListFilterLive(s));
    PHand(s, "Joraga Warcaller", 6);
    // Human play is gated at TurnSolver's call sites (ApplyProviderPlanListFilter), not in the provider, so the
    // provider-level liveness ignores HumanPlayActive(); only the unpruned gates lift it here.
    CHECK(Prov().PlanListFilterLive(s) == (!DecisionUnpruned(UnprunedGate::Replicate) && !DecisionUnpruned(UnprunedGate::XSpell)));
}
