// Enchanted Evening lock cluster, STEP 7 (2026-11-25; docs/design/enchanted-evening-lock-design.md §4.5): Force of
// Vigor clause 1 -- "If it's not your turn, you may exile a green card from your hand rather than pay this spell's
// mana cost." -- cast in the OPPONENT'S END-STEP window (the morph window, EotFaceUpMenu's FoV half, SpellEffects.h
// EotFovOptions / EotFovApply), after the USER ruling-7 Wave + Opalescence sweep, before the lock check. Searched:
// "no cast" first, then {target pin} x {pitched green card class}; paid with untapped mana instead when that is
// payable (the sound fold).
#include <doctest/doctest.h>

#include "ai/AIEngine.h"
#include "ai/DecisionProviders.h"
#include "ai/TurnSolver.h"
#include "cards/CardDatabase.h"
#include "core/GameEngine.h"
#include "core/GameSetup.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"
#include "deck/DeckLoader.h"
#include "runner/GoldFishRunner.h"
#include "mtg_test_seam.h"

#include <algorithm>
#include <filesystem>
#include <set>
#include <string>
#include <vector>

namespace
{
const char* const kEE    = "Enchanted Evening";
const char* const kOpal  = "Opalescence";
const char* const kFoV   = "Force of Vigor";
const char* const kWave  = "Parallax Wave";
const char* const kExpl  = "Exploration";
const char* const kWild  = "Wild Growth";

const CardDefinition& L7Def(const std::string& name)
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
Card L7Card(const std::string& name, int number)
{
    Card c = L7Def(name).card;
    c.m_number = number;
    return c;
}
int L7Put(GameState& s, const std::string& name, int number, int controller = 0)
{
    Permanent p;
    p.card = L7Card(name, number);
    p.controller_index = p.owner_index = controller;
    if (L7Def(name).params.fading_counters > 0) { p.fade_counters = L7Def(name).params.fading_counters; }
    s.battlefield.push_back(p);
    return static_cast<int>(s.battlefield.size()) - 1;
}
// An opponent spawn body (a definition-less creature token, as opponentdeck materialises them).
void L7Spawn(GameState& s, int pw, int number)
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
    p.is_token = true;
    p.controller_index = p.owner_index = 1;
    s.battlefield.push_back(p);
}
bool L7Has(const GameState& s, int number)
{
    for (const Permanent& p : s.battlefield) { if (p.card.m_number == number) { return true; } }
    return false;
}
bool L7InHand(const GameState& s, const std::string& name)
{
    for (const Card& c : s.players[0].hand) { if (c.m_name.str() == name) { return true; } }
    return false;
}
bool L7In(const std::vector<Card>& zone, const std::string& name)
{
    for (const Card& c : zone) { if (c.m_name.str() == name) { return true; } }
    return false;
}

// Our end of turn 4 under Enchanted Evening (every permanent an enchantment), an opponent 3/3 spawn (a legal FoV
// target under EE), the EE provider (it drops the empty target set), no opponent land play.
GameState L7Board()
{
    static const EnchantedEveningProvider prov;
    L7Def("Forest");
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 4;
    s.phase               = Phase::Ending;
    s.step                = Step::MainPhase;
    s.players[0].life     = gamesetup::StartingLife();
    s.players[1].life     = gamesetup::StartingLife();
    s.players[0].lands_played_this_turn = 1;
    s.deck_float_wild_exact = true;
    s.deck_has_type_statics = true;
    s.deck_has_eot_fov_pitch = true;
    s.m_provider = &prov;
    for (int k = 0; k < 8; ++k) { s.players[0].library.push_back(L7Card("Plains", 900 + k)); }
    for (int k = 0; k < 8; ++k) { s.players[1].library.push_back(L7Card("Plains", 100900 + k)); }
    L7Put(s, kEE, 51);
    L7Spawn(s, 3, 700001);
    RefreshTypeStatics(s);
    return s;
}
std::vector<EotFovOption> L7Opts(const GameState& s)
{
    const OppEndStepWindowScope w;
    return EotFovOptions(s);
}
std::set<std::string> L7Pitches(const std::vector<EotFovOption>& v)
{
    std::set<std::string> out;
    for (const EotFovOption& o : v) { out.insert(o.pitch); }
    return out;
}
std::string L7SpawnPin(const GameState& s)
{
    (void)s;
    return DestroyUpToArtEnchPin({ 700001 });
}
} // namespace

TEST_CASE("EE lock step 7: the param and the deck stamp -- only the Enchanted Evening list holds the pitch")
{
    CHECK(L7Def(kFoV).params.alt_cost_exile_green_off_turn);
    CHECK_FALSE(L7Def(kExpl).params.alt_cost_exile_green_off_turn);
    const Decklist ee = DeckLoader::LoadFromFile(ResolveHeuristicDefaultsPath(
        "decks/Enchanted Evening/Enchanted Evening.cod"));
    CHECK(GoldFishRunner::SetupGame(ee, 7000, 0).deck_has_eot_fov_pitch);
    int decks = 0;
    for (const auto& dir : std::filesystem::directory_iterator(ResolveHeuristicDefaultsPath("decks")))
    {
        if (!dir.is_directory()) { continue; }
        const std::string name = dir.path().filename().string();
        for (const char* ext : { ".cod", ".txt" })
        {
            const std::filesystem::path f = dir.path() / (name + ext);
            if (!std::filesystem::exists(f)) { continue; }
            INFO("deck: " << name);
            const Decklist d = DeckLoader::LoadFromFile(f);
            bool any = false;
            for (const Card& c : d.mainboard)
            {
                const CardDefinition* cd = CardDatabase::Instance().LookupCached(c);
                any = any || (cd != nullptr && cd->params.alt_cost_exile_green_off_turn);
            }
            CHECK(any == (name == "Enchanted Evening"));   // StampDeckTraits' rule: the window is free elsewhere
            ++decks;
            break;
        }
    }
    CHECK(decks > 10);
}

TEST_CASE("EE lock step 7: pitch legality -- not on our turn, another green card, never the FoV itself")
{
    GameState s = L7Board();
    s.players[0].hand.push_back(L7Card(kFoV, 30));
    s.players[0].hand.push_back(L7Card(kExpl, 31));
    // Our turn (outside the opponent's end-step window): nothing, and the main phase offers no FoV without mana.
    CHECK(EotFovOptions(s).empty());
    {
        GameState m = s;
        m.phase = Phase::PreCombatMain;
        bool fov_plan = false;
        for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(m, true))
        { for (const Action& a : p.actions) { fov_plan = fov_plan || a.card_name.str() == kFoV; } }
        CHECK_FALSE(fov_plan);
    }
    // In the window: Exploration (green) is pitchable.
    CHECK(L7Pitches(L7Opts(s)) == std::set<std::string>{ kExpl });
    // FoV alone: it cannot exile itself -> no option.
    {
        GameState t = s;
        t.players[0].hand.pop_back();
        CHECK(L7Opts(t).empty());
    }
    // A white card is not green -> no option.
    {
        GameState t = s;
        t.players[0].hand.back() = L7Card(kOpal, 31);
        CHECK(L7Opts(t).empty());
    }
    // A second Force of Vigor is a green card: legal.
    {
        GameState t = s;
        t.players[0].hand.back() = L7Card(kFoV, 31);
        CHECK(L7Pitches(L7Opts(t)) == std::set<std::string>{ kFoV });
    }
    // Identity fold: two Explorations are one class; Wild Growth is a second class.
    {
        GameState t = s;
        const std::size_t one = L7Opts(t).size();
        t.players[0].hand.push_back(L7Card(kExpl, 32));
        CHECK(L7Opts(t).size() == one);
        t.players[0].hand.push_back(L7Card(kWild, 33));
        CHECK(L7Opts(t).size() == 2 * one);
        CHECK(L7Pitches(L7Opts(t)) == std::set<std::string>{ kExpl, kWild });
    }
    // The deck stamp gates it.
    {
        GameState t = s;
        t.deck_has_eot_fov_pitch = false;
        CHECK_FALSE(AnyEotFovCandidate(t));
        CHECK(L7Opts(t).empty());
    }
}

TEST_CASE("EE lock step 7: the target menu is the main-phase cast's; hard cast with untapped mana folds the pitches")
{
    GameState s = L7Board();
    for (int k = 0; k < 4; ++k) { L7Put(s, "Forest", 101 + k); }
    RefreshTypeStatics(s);
    s.players[0].hand.push_back(L7Card(kFoV, 30));
    s.players[0].hand.push_back(L7Card(kExpl, 31));
    const std::vector<std::string> pins = DestroyUpToArtEnchSearchPins(s, 0, 2);
    REQUIRE_FALSE(pins.empty());
    CHECK(std::find(pins.begin(), pins.end(), std::string("FV")) == pins.end());   // the EE provider's empty-set fold
    CHECK(pins.front() == L7SpawnPin(s));   // the provider's pick (the opponent's spawn) first
    // Four untapped Forests pay {2}{G}{G}: every option is the mana-paid cast, one per pin, in pin order.
    const std::vector<EotFovOption> hard = L7Opts(s);
    REQUIRE(hard.size() == pins.size());
    for (std::size_t i = 0; i < hard.size(); ++i)
    {
        CHECK(hard[i].pitch.empty());
        CHECK(hard[i].pin == pins[i]);
    }
    // The same state in OUR main phase: the hard cast's pin menu is the same set.
    {
        GameState m = s;
        m.phase = Phase::PreCombatMain;
        std::set<std::string> main_pins;
        for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(m, true))
        {
            if (p.actions.size() == 1 && p.actions.front().card_name.str() == kFoV)
            { main_pins.insert(p.actions.front().tutor_target.str()); }
        }
        CHECK(main_pins == std::set<std::string>(pins.begin(), pins.end()));
    }
    // Tap one Forest: the mana is short, so the pitch is offered instead, every pin x Exploration.
    for (Permanent& p : s.battlefield) { if (p.card.m_number == 101) { p.tapped = true; break; } }
    const std::vector<EotFovOption> pitch = L7Opts(s);
    CHECK(pitch.size() == DestroyUpToArtEnchSearchPins(s, 0, 2).size());
    CHECK(L7Pitches(pitch) == std::set<std::string>{ kExpl });
    // The mana-paid cast taps the lands and keeps the green card.
    {
        GameState t = s;
        for (Permanent& p : t.battlefield) { p.tapped = false; }
        EotFaceUpOption o;
        o.fov = true;
        o.fov_pin = L7SpawnPin(t);
        const OppEndStepWindowScope w;
        EotFovApply(t, o);
        CHECK_FALSE(L7Has(t, 700001));
        CHECK(L7InHand(t, kExpl));
        CHECK(L7In(t.players[0].graveyard, kFoV));
        int tapped = 0;
        for (const Permanent& p : t.battlefield) { tapped += p.tapped ? 1 : 0; }
        CHECK(tapped == 4);
    }
}

TEST_CASE("EE lock step 7: the window menu -- no cast first, the pitched-card choice, the cast resolves")
{
    GameState s = L7Board();
    s.players[0].hand.push_back(L7Card(kExpl, 31));
    s.players[0].hand.push_back(L7Card(kFoV, 30));
    s.players[0].hand.push_back(L7Card(kWild, 33));
    const OppEndStepWindowScope w;
    const std::vector<EotFaceUpOption> menu = EotFaceUpMenu(s);
    REQUIRE(menu.size() >= 3);
    CHECK_FALSE(menu.front().fov);   // rank 0 = do nothing (the front; the playout's pick)
    std::set<std::string> pitched;
    int wild_spawn = -1;
    for (int r = 1; r < static_cast<int>(menu.size()); ++r)
    {
        const EotFaceUpOption& o = menu[static_cast<std::size_t>(r)];
        CHECK(o.fov);
        pitched.insert(o.fov_pitch);
        if (o.fov_pitch == kWild && o.fov_pin == L7SpawnPin(s) && wild_spawn < 0) { wild_spawn = r; }
    }
    CHECK(pitched == std::set<std::string>{ kExpl, kWild });
    REQUIRE(wild_spawn > 0);
    GameState t = s;
    EotFaceUpApply(t, menu[static_cast<std::size_t>(wild_spawn)]);
    CHECK_FALSE(L7Has(t, 700001));               // the spawn (an enchantment under EE) is destroyed
    CHECK(L7Has(t, 51));                         // ...and nothing else
    CHECK(L7In(t.exile, kWild));                 // the pitched card is exiled
    CHECK(L7In(t.players[0].graveyard, kFoV));   // the FoV resolved to our graveyard
    CHECK(L7InHand(t, kExpl));                   // the other green card stays
    CHECK(t.players[0].hand.size() == 1);
    // A stale pin (the pitched card is gone) casts nothing.
    GameState u = s;
    u.players[0].hand.pop_back();   // Wild Growth leaves the hand
    EotFaceUpApply(u, menu[static_cast<std::size_t>(wild_spawn)]);
    CHECK(L7Has(u, 700001));
    CHECK(L7InHand(u, kFoV));
}

TEST_CASE("EE lock step 7: no window without a Force of Vigor in hand")
{
    GameState s = L7Board();
    s.players[0].hand.push_back(L7Card(kExpl, 31));
    CHECK_FALSE(AnyEotFovCandidate(s));
    {
        const OppEndStepWindowScope w;
        CHECK(EotFaceUpMenu(s).empty());
    }
    CHECK(TurnSolver::ChooseRealCrossingFaceUp(s, 2, 12, nullptr, false) == -2);
    // With the FoV (and a green card to pitch) the crossing is searched.
    s.players[0].hand.push_back(L7Card(kFoV, 30));
    CHECK(AnyEotFovCandidate(s));
    CHECK(TurnSolver::ChooseRealCrossingFaceUp(s, 1, 12, nullptr, false) != -2);
}

TEST_CASE("EE lock step 7: the window runs AFTER the Wave + Opalescence sweep, in both worlds")
{
    for (const bool sweep : { true, false })
    {
        INFO("sweep: " << sweep);
        GameState s = L7Board();
        s.deck_has_wave_opal_sweep = sweep;
        L7Put(s, kOpal, 52);
        L7Put(s, kWave, 53);
        // A CARD the opponent controls (the spawn token above leaves no trace): its Exploration, a 1/1 under Opalescence.
        L7Put(s, kExpl, 800001, 1);
        RefreshTypeStatics(s);
        s.players[0].hand.push_back(L7Card(kFoV, 30));
        s.players[0].hand.push_back(L7Card(kExpl, 31));
        s.scripted_eot_face_up = 1;   // the first cast entry: the front pin (the provider's pick), pitching Exploration
        AIEngine eng{ MulliganProfile::DefaultProfile(), 3, 20 };
        GameEngine ge{ eng };
        GameState roll = s;
        REQUIRE(TurnSolver::SimulateEndAndStartNextTurnForTest(roll));
        GameState exec = s;
        MtgTestSeam::GeRunTurnFromEnd(ge, exec);
        for (const GameState* g : { &roll, &exec })
        {
            INFO("world: " << std::string(g == &roll ? "rollout" : "executor"));
            CHECK(L7In(g->exile, kExpl));              // the FoV was cast, pitching Exploration
            CHECK(L7In(g->players[0].graveyard, kFoV));
            const int ours = (L7Has(*g, 51) ? 1 : 0) + (L7Has(*g, 52) ? 1 : 0) + (L7Has(*g, 53) ? 1 : 0);
            CHECK_FALSE(L7Has(*g, 800001));   // the opponent's Exploration is gone either way
            // Sweep first: both opponent bodies were gone (exiled for good) when the window ran, so the FoV's front pin
            // was one of OURS. Had the window run first, its front pin would have been the opponent's pair.
            // No sweep: the front pin was the opponent's, and every permanent of ours survives.
            if (sweep) { CHECK(ours < 3); } else { CHECK(ours == 3); }
        }
    }
}
