// ELVES PROVIDER (Stage 4a, 2026-11-26, docs/design/analysis-Elves.md "Provider heuristics"):
//
//  1. ROUTING: decks/Elves routes to ElvesProvider (it rode SelesnyaLifegainProvider through Genesis Wave's
//     reveal_x_put_permanents); SelesnyaLifegain and AccomplishedAlchemist are unchanged. The every-deck table is
//     test_pirates_provider.cpp's.
//  2. THE USER'S SCALING-SINK RULE (ElvesScalingAdmit through EnumerateMainPlans): one sink scaled to the max the
//     plan's other casts leave, hold-back counts for the attack-capable mana creatures (amended 2026-11-26), no
//     partial splits across two sinks; the idle-adjacent / unscaled plans untouched.
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

TEST_CASE("Elves scaling rule: hold-back sources -- untapped attack-capable mana creatures at their live yield, pre-combat")
{
    GameState s = PBase();
    std::vector<ElvesHoldBackSource> src = ElvesHoldBackSources(s, true);
    REQUIRE(src.size() == 1);                                 // the Llanowar
    CHECK(ElvesHoldBackMana(src, 0) == 1);
    CHECK(ElvesHoldBackSources(s, false).empty());            // post-combat: the attack is over
    PPut(s, "Elvish Mystic", 201, /*sick=*/true);
    CHECK(ElvesHoldBackSources(s, true).size() == 1);         // a summoning-sick Mystic cannot attack
    PPut(s, "Priest of Titania", 202);                        // a SCALED dork counts, at its live tap
    PPut(s, "Elvish Archdruid", 203, /*sick=*/true);
    PPut(s, "Marwyn, the Nurturer", 204, /*sick=*/true);
    src = ElvesHoldBackSources(s, true);
    REQUIRE(src.size() == 2);                                 // Llanowar + Priest
    // The Priest counts every Elf on the battlefield (Llanowar, Mystic, Priest, Archdruid, Marwyn = 5).
    CHECK(ElvesHoldBackMana(src, 0) == 1 + 5);
    CHECK(ElvesHoldBackMana(src, 2) == 1 + 7);                // two Elf casts in the plan grow the Priest's tap
    PPut(s, "Concordant Crossroads", 205);                    // haste: Mystic, Archdruid and Marwyn can attack now
    src = ElvesHoldBackSources(s, true);
    CHECK(src.size() == 5);
    // Llanowar 1 + Mystic 1 + Priest 5 + Archdruid 5 (Elves you control) + Marwyn 2 (1 + the Archdruid's +1).
    CHECK(ElvesHoldBackMana(src, 0) == 1 + 1 + 5 + 5 + 2);
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

TEST_CASE("Elves scaling rule: a held SCALED dork widens the band by its live yield; post-combat keeps the top")
{
    if (!RuleLive()) { return; }
    PruneArm on(1);
    GameState s = PBase();
    PPut(s, "Priest of Titania", 202);                        // 7 Forests + Llanowar 1 + Priest 2 = 10 mana
    s.players[0].hand.clear();
    PHand(s, "Joraga Warcaller", 1);
    const std::set<CastSet> m = Menu(TurnSolver::EnumerateMainPlansExhaustive(s, true, nullptr, nullptr));
    // Top K 4 ((10 - 1) / 2). Leaving the Priest AND the Llanowar untapped frees 3 mana: ceil(3 / 2) = 2 kicks
    // down. (The first cut counted only the Llanowar: one kick down, K 2 dropped.)
    CHECK(m.count(CastSet{ { "Joraga Warcaller", 4 } }) == 1);
    CHECK(m.count(CastSet{ { "Joraga Warcaller", 3 } }) == 1);
    CHECK(m.count(CastSet{ { "Joraga Warcaller", 2 } }) == 1);
    CHECK(m.count(CastSet{ { "Joraga Warcaller", 1 } }) == 0);   // only the Forests' mana would be unspent
    CHECK(m.count(CastSet{ { "Joraga Warcaller", 0 } }) == 1);   // the unscaled cast is never touched
    // Post-combat the band is the top alone.
    std::vector<TurnSolver::Plan> plans;
    { PruneArm off(0); plans = TurnSolver::EnumerateMainPlansExhaustive(s, true, nullptr, nullptr); }
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

// THE SUBSET-SUM FOLD (MTG_ELVES_SUBSET_FOLD, default ON): inside the band only the counts that are the largest for
// some reachable leftover survive -- subset sums of the held attackers' yields, plus Ezuri's converter multiples.
namespace {
struct FoldArm
{
    std::int8_t saved;
    explicit FoldArm(int v) : saved(heurarm::t_arm[heurarm::ELVES_SUBSET_FOLD])
    { heurarm::t_arm[heurarm::ELVES_SUBSET_FOLD] = static_cast<std::int8_t>(v); }
    ~FoldArm() { heurarm::t_arm[heurarm::ELVES_SUBSET_FOLD] = saved; }
};
// The Genesis Wave X values (Wave-alone plans) the rule keeps on this board, pre-combat, fold on or off.
std::set<int> WaveAloneXs(const GameState& s, int fold)
{
    std::vector<TurnSolver::Plan> plans;
    { PruneArm off(0); plans = TurnSolver::EnumerateMainPlansExhaustive(s, true, nullptr, nullptr); }
    std::vector<DecisionProvider::PlanView> views;
    for (const TurnSolver::Plan& p : plans) { views.push_back({ &p.actions, &p.land_to_play, &p }); }
    std::vector<char> admit(plans.size(), 1);
    { PruneArm on(1); FoldArm f(fold); ElvesScalingAdmit(s, /*is_pre_combat=*/true, views, admit); }
    std::set<int> xs;
    for (std::size_t i = 0; i < plans.size(); ++i)
    {
        const CastSet c = Casts(plans[i]);
        if (admit[i] && c.size() == 1 && c[0].first == "Genesis Wave") { xs.insert(c[0].second); }
    }
    return xs;
}
// 10 Forests, two unsick Priests of Titania (and optionally a Llanowar and an Ezuri); hand Genesis Wave alone.
GameState FoldBase(bool llanowar, bool ezuri)
{
    GameState s; s.active_player_index = 0; s.turn_number = 6; s.game_seed = 4243;
    s.players[0].life = 20; s.players[1].life = 20;
    s.players[0].lands_played_this_turn = 1;
    for (int i = 0; i < 30; ++i) { s.players[0].library.push_back(PCard(i % 2 ? "Forest" : "Llanowar Elves", 900 + i)); }
    for (int i = 0; i < 10; ++i) { PPut(s, "Forest", 100 + i); }
    PPut(s, "Priest of Titania", 201);
    PPut(s, "Priest of Titania", 202);
    if (llanowar) { PPut(s, "Llanowar Elves", 203); }
    if (ezuri) { PPut(s, "Ezuri, Renegade Leader", 204); }
    PHand(s, "Genesis Wave", 3);
    s.m_provider = &Prov();
    return s;
}
}   // namespace

TEST_CASE("Elves scaling rule: the subset-sum fold keeps only the hold-back points inside the band")
{
    if (!RuleLive()) { return; }
    // Two Priests at 2 each: 14 mana, Wave top X 11. Held sums {0, 2, 4}: X 11, 9, 7. The band keeps 7..11.
    const GameState s = FoldBase(false, false);
    CHECK(WaveAloneXs(s, 0) == std::set<int>{ 7, 8, 9, 10, 11 });
    CHECK(WaveAloneXs(s, 1) == std::set<int>{ 7, 9, 11 });
    CHECK(WaveAloneXs(s, -1) == WaveAloneXs(s, 1));            // default ON
    // + a Llanowar (Priests at 3): 17 mana, top 14; sums {0,1,3,4,6,7}: drops X 12 and 9.
    const GameState t = FoldBase(true, false);
    CHECK(WaveAloneXs(t, 0) == std::set<int>{ 7, 8, 9, 10, 11, 12, 13, 14 });
    CHECK(WaveAloneXs(t, 1) == std::set<int>{ 7, 8, 10, 11, 13, 14 });
}

TEST_CASE("Elves scaling rule: the fold keeps Ezuri's combat-pump multiples (leftover mana the converter spends)")
{
    if (!RuleLive()) { return; }
    // Ezuri is an Elf: Priests at 4, Llanowar 1 -> 19 mana, top X 16, h 9. Held sums {0,1,4,5,8,9}; with the pump's
    // 5-multiples also {5,6,9}: kept d {0,1,4,5,6,8,9}, so X 16,15,12,11,10,8,7 (drops 14, 13, 9).
    const GameState s = FoldBase(true, true);
    CHECK(WaveAloneXs(s, 0) == std::set<int>{ 7, 8, 9, 10, 11, 12, 13, 14, 15, 16 });
    CHECK(WaveAloneXs(s, 1) == std::set<int>{ 7, 8, 10, 11, 12, 15, 16 });
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

// ---- 4. THE USER'S CLEANUP-DISCARD BUCKETS (2026-11-27: "a discard policy that just buckets mana and threats",
// amended: "feel free to add more mana as long as we have 1-2 scaling threats ... If not, I suppose we need more
// threats") -- ElvesProvider::CleanupDiscardCandidates. Every expected order below was derived BY HAND from the rule as
// built (the header note above the implementation):
//   DEAD first. MANA shed order Lodge < Cavern < Forest < Llanowar = Mystic < Priest / Archdruid / Marwyn, the best
//   land last while < 4 lands are in play. THREAT order (worst first): a no-ability body, then farthest from castable
//   (MV - (board mana + mana cards in hand)), then value Crossroads 1 < Magistrate / Shepherd 2 < lords 3 < Ezuri /
//   Wave 4, ties the later copy first. WITH a sink (Wave / Warcaller / Brigade / Ezuri / Shepherd): DEAD > spares
//   (bodies, 3rd+ sinks, other threats) > MANA > the 2 kept sinks. WITHOUT: DEAD > threats past the best 3 > mana past
//   floor + 2 > the best 3 threats > those 2 mana > the floor land.
namespace {
Card EdCard(const std::string& name, int number)   // a NAME-ONLY placeholder, as real hand cards are
{
    REQUIRE_MESSAGE(CardDatabase::Instance().Lookup(name) != nullptr, "card not in cards.json: ", name);
    Card c; c.m_name = name; c.RehashName(); c.m_number = number;
    return c;
}
GameState EdState(const std::vector<std::string>& hand, const std::vector<std::string>& board)
{
    (void)PD("Forest");   // loads cards.json
    GameState s; s.active_player_index = 0; s.turn_number = 5; s.game_seed = 777;
    s.players[0].life = 20; s.players[1].life = 20; s.players[0].lands_played_this_turn = 1;
    int id = 1;
    for (const std::string& n : hand) { s.players[0].hand.push_back(EdCard(n, id++)); }
    for (const std::string& n : board) { PPut(s, n, 500 + id++); }
    s.m_provider = &Prov();
    return s;
}
std::vector<int> EdArm(const GameState& s, int arm)
{
    heurarm::t_arm[heurarm::ELVES_BUCKET_DISCARD] = static_cast<std::int8_t>(arm);
    std::vector<int> v = Prov().CleanupDiscardCandidates(s, nullptr);
    heurarm::t_arm[heurarm::ELVES_BUCKET_DISCARD] = -1;
    return v;
}
std::vector<int> Ed(const GameState& s) { return EdArm(s, 1); }
// A full permutation of the hand, and PREFIX-STABLE (CleanupDiscardShedStable() == true): the per-shed loop a cleanup
// would otherwise run (ask, shed the first, ask again, down to 7 cards) sheds exactly the single consultation's prefix.
void EdCheck(const GameState& s, const std::vector<int>& v)
{
    std::vector<int> sorted = v;
    std::sort(sorted.begin(), sorted.end());
    std::vector<int> want;
    for (int i = 0; i < static_cast<int>(s.players[0].hand.size()); ++i) { want.push_back(i); }
    REQUIRE(sorted == want);
    heurarm::t_arm[heurarm::ELVES_BUCKET_DISCARD] = 1;
    CHECK(Prov().CleanupDiscardShedStable());
    heurarm::t_arm[heurarm::ELVES_BUCKET_DISCARD] = -1;
    GameState cur = s;
    for (std::size_t k = 0; cur.players[0].hand.size() > 7; ++k)
    {
        auto& h = cur.players[0].hand;
        const std::vector<int> o = Ed(cur);
        REQUIRE(!o.empty());
        CHECK(h[static_cast<std::size_t>(o.front())].m_number == s.players[0].hand[static_cast<std::size_t>(v[k])].m_number);
        h.erase(h.begin() + o.front());
    }
}
const char* kEFo = "Forest";
const char* kELo = "Wirewood Lodge";
const char* kECa = "Cavern of Souls";
const char* kELl = "Llanowar Elves";
const char* kEPr = "Priest of Titania";
const char* kEMa = "Marwyn, the Nurturer";
const char* kEEz = "Ezuri, Renegade Leader";
const char* kEJo = "Joraga Warcaller";
const char* kECl = "Elvish Clancaller";
const char* kEVi = "Leaf-Crowned Visionary";
const char* kEBr = "Galadhrim Brigade";
const char* kETa = "Taunting Elf";
const char* kEIm = "Immaculate Magistrate";
const char* kEWa = "Genesis Wave";
const char* kECr = "Concordant Crossroads";
}   // namespace

// WITH sinks (Ezuri 3, Brigade 5), board 2 Forests. Spares: Taunting 6, then Clancaller 4 -- both before any mana. Mana:
// Lodge 7, Llanowar 1, Priest 2, then the floor Forest 0. Kept sinks last, Brigade (value 3) before Ezuri (4).
TEST_CASE("Elves discard: with a scaling threat, Taunting Elf and the other threats go before mana; =0 = inherited")
{
    const GameState s = EdState({ kEFo, kELl, kEPr, kEEz, kECl, kEBr, kETa, kELo }, { kEFo, kEFo });
    const std::vector<int> v = Ed(s);
    EdCheck(s, v);
    CHECK(v == std::vector<int>{ 6, 4, 7, 1, 2, 0, 5, 3 });
    CHECK(EdArm(s, 0) == Prov().SelesnyaLifegainProvider::CleanupDiscardCandidates(s, nullptr));
    CHECK(EdArm(s, -1) == v);   // default ON
}

// Same mana and lords, sink vs no sink. Board 2 Forests; hand Forest 0, Forest 1, Llanowar 2, Priest 3, Clancaller 4,
// Visionary 5, X 6, Taunting 7. Mana order Forest 1, Llanowar 2, Priest 3, Forest 0 (floor).
//   X = Ezuri (a sink): spares Taunting 7, Visionary 5, Clancaller 4 (later lord first), then all mana, then Ezuri.
//   X = Magistrate (no sink): the best 3 threats are Clancaller, Visionary, Magistrate (castable: reach 2 + 4); past them
//   Taunting 7; then the mana past floor + 2 = Forest 1; then Magistrate 6, Visionary 5, Clancaller 4; then Llanowar 2,
//   Priest 3, the floor Forest 0.
TEST_CASE("Elves discard: no scaling threat in hand keeps up to 3 threats ahead of the surplus mana")
{
    const GameState a = EdState({ kEFo, kEFo, kELl, kEPr, kECl, kEVi, kEEz, kETa }, { kEFo, kEFo });
    const std::vector<int> va = Ed(a);
    EdCheck(a, va);
    CHECK(va == std::vector<int>{ 7, 5, 4, 1, 2, 3, 0, 6 });
    const GameState b = EdState({ kEFo, kEFo, kELl, kEPr, kECl, kEVi, kEIm, kETa }, { kEFo, kEFo });
    const std::vector<int> vb = Ed(b);
    EdCheck(b, vb);
    CHECK(vb == std::vector<int>{ 7, 1, 6, 5, 4, 2, 3, 0 });
}

// Four sinks: Ezuri 2, Warcaller 3, Brigade 4, Wave 5 (board 4 Forests, reach 6: all castable). Kept: Wave 5 and Ezuri 2
// (value 4); the spares Brigade 4, Warcaller 3 go after Taunting 7 and before Clancaller 6, all before mana (Forest 0,
// Llanowar 1; no floor at 4 lands); then Wave, Ezuri.
TEST_CASE("Elves discard: at most 2 scaling threats are kept; a 3rd+ goes before mana")
{
    const GameState s = EdState({ kEFo, kELl, kEEz, kEJo, kEBr, kEWa, kECl, kETa }, { kEFo, kEFo, kEFo, kEFo });
    const std::vector<int> v = Ed(s);
    EdCheck(s, v);
    CHECK(v == std::vector<int>{ 7, 4, 3, 6, 0, 1, 5, 2 });
}

// DEAD first: Ezuri on our battlefield (hand Ezuri 0), the 2nd Marwyn in hand (2), Concordant Crossroads while one is
// ours in play (3).
TEST_CASE("Elves discard: a 2nd legend and a 2nd Concordant Crossroads are shed first")
{
    const GameState s = EdState({ kEEz, kEMa, kEMa, kECr, kEFo, kECl, kELl, kEBr }, { kEFo, kEFo, kEFo, kEEz, kECr });
    const std::vector<int> v = Ed(s);
    EdCheck(s, v);
    REQUIRE(v.size() == 8);
    CHECK(std::vector<int>(v.begin(), v.begin() + 3) == std::vector<int>{ 0, 2, 3 });
    // A 2nd Crossroads held with none in play: the later copy is dead, the first is a threat.
    const GameState t = EdState({ kECr, kECr, kEFo, kEFo, kELl, kEEz, kECl, kEBr }, { kEFo });
    const std::vector<int> w = Ed(t);
    EdCheck(t, w);
    CHECK(w.front() == 1);
}

// MANA ORDER and the LAND FLOOR. Hand Forest 0, Forest 1, Lodge 2, Llanowar 3, Priest 4, Ezuri 5, Brigade 6, Cavern 7
// (6 mana, 2 sinks): Lodge, Cavern, the surplus Forest, Llanowar (a mana Elf over a surplus land), Priest, and -- with
// 2 lands in play -- the floor Forest 0 last; with 4 lands in play no floor, so both Forests go before Llanowar.
TEST_CASE("Elves discard: Lodge < Cavern < Forest < mana Elves, one land kept while fewer than 4 are in play")
{
    const std::vector<std::string> hand{ kEFo, kEFo, kELo, kELl, kEPr, kEEz, kEBr, kECa };
    const GameState s = EdState(hand, { kEFo, kEFo });
    const std::vector<int> v = Ed(s);
    EdCheck(s, v);
    CHECK(v == std::vector<int>{ 2, 7, 1, 3, 4, 0, 6, 5 });
    const GameState t = EdState(hand, { kEFo, kEFo, kEFo, kEFo });
    const std::vector<int> w = Ed(t);
    EdCheck(t, w);
    CHECK(w == std::vector<int>{ 2, 7, 1, 0, 3, 4, 6, 5 });
}

// No sink, board 1 Forest: hand Magistrate 0, Taunting 1, Clancaller 2, Visionary 3, Crossroads 4, Forest 5,
// Llanowar 6, Clancaller 7. Reach 1 + 2 = 3, so Magistrate (MV 4) is 1 short. Threat order: Taunting 1, Magistrate 0,
// Crossroads 4, Clancaller 7, Visionary 3, Clancaller 2 -- the first three are past the best 3. Mana: Llanowar 6 and the
// floor Forest 5 are the protected floor + 2 (no surplus).
TEST_CASE("Elves discard: Taunting Elf first, then the threat farthest from castable, then the value rank")
{
    const GameState s = EdState({ kEIm, kETa, kECl, kEVi, kECr, kEFo, kELl, kECl }, { kEFo });
    const std::vector<int> v = Ed(s);
    EdCheck(s, v);
    CHECK(v == std::vector<int>{ 1, 0, 4, 7, 3, 2, 6, 5 });
}

// 10-card hands (a cleanup that sheds three): the single consultation's prefix is the per-shed loop's answer.
//   With sinks (Ezuri 5, Brigade 7), board 2 Forests: Taunting 8, Magistrate 9 (castable at reach 7), Clancaller 6.
//   No sink, board 2 Forests: Crossroads 9 (past the best 3), then the surplus Forests 3, 2 -- the mana shed moves the
//   reach (8 -> 7) and the threat order stays put.
TEST_CASE("Elves discard: a three-card cleanup is prefix-stable")
{
    const GameState s = EdState({ kEFo, kEFo, kEFo, kELl, kEPr, kEEz, kECl, kEBr, kETa, kEIm }, { kEFo, kEFo });
    const std::vector<int> v = Ed(s);
    EdCheck(s, v);
    REQUIRE(v.size() == 10);
    CHECK(std::vector<int>(v.begin(), v.begin() + 3) == std::vector<int>{ 8, 9, 6 });
    const GameState t = EdState({ kEFo, kEFo, kEFo, kEFo, kELl, kEPr, kECl, kEVi, kEIm, kECr }, { kEFo, kEFo });
    const std::vector<int> w = Ed(t);
    EdCheck(t, w);
    REQUIRE(w.size() == 10);
    CHECK(std::vector<int>(w.begin(), w.begin() + 3) == std::vector<int>{ 9, 3, 2 });
}
