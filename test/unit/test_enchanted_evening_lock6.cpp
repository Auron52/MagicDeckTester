// Enchanted Evening lock cluster, STEP 6 (2026-11-26; docs/design/enchanted-evening-lock-design.md §3.2-§3.4):
// free-turn integration -- the COMBINATION trait in GoldFishRunner::DeckMayLockOpponent (DeckHasEeLock), no reach
// certificate for this deck (DeckLockNeedsLandStrip false), and the USER's FAIRNESS CHECK (MTG_FREE_TURN_EE_FAIR,
// eefair:: in SpellEffects.h): the virtual board of what the opponent could have cast with its lands since the game
// began or the last Calming Verse wipe (rulings 5, 6, 11), cleared only by removal we actually cast (ruling 8: a
// Verse, the Wave + Opalescence sweep, a Force of Vigor), the hexproof clause (rulings 9 / 10: only trample and power
// >= 5, counting the Auras it could carry, breaks the full lock), Force of Vigor against the virtual Auras as a
// SEARCHED cast (ruling 13), and ruling 17 (no loop -> the sweep clears nothing and contains nothing).
#include <doctest/doctest.h>

#include "ai/AIEngine.h"
#include "ai/DecisionProviders.h"
#include "ai/TurnSolver.h"
#include "cards/CardDatabase.h"
#include "core/FreeTurns.h"
#include "core/GameSetup.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/OpponentDeck.h"
#include "core/SpellEffects.h"
#include "deck/DeckLoader.h"
#include "runner/GoldFishRunner.h"

#include <filesystem>
#include <set>
#include <string>
#include <vector>

namespace
{
const char* const kEE    = "Enchanted Evening";
const char* const kOpal  = "Opalescence";
const char* const kVerse = "Calming Verse";
const char* const kFoV   = "Force of Vigor";
const char* const kWave  = "Parallax Wave";
const char* const kGrove = "Sterling Grove";

struct L6Rule
{
    int saved = freeturn::t_force;
    L6Rule() { freeturn::t_force = 1; }
    ~L6Rule() { freeturn::t_force = saved; }
};

const CardDefinition& L6Def(const std::string& name)
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

Card L6Card(const std::string& name, int number)
{
    Card c = L6Def(name).card;
    c.m_number = number;
    return c;
}

int L6Put(GameState& s, const std::string& name, int number, int controller = 0)
{
    Permanent p;
    p.card = L6Card(name, number);
    p.controller_index = p.owner_index = controller;
    if (L6Def(name).params.fading_counters > 0) { p.fade_counters = L6Def(name).params.fading_counters; }
    s.battlefield.push_back(p);
    return static_cast<int>(s.battlefield.size()) - 1;
}

int L6SlotOf(const char* list)
{
    for (std::size_t i = 0; i < opponentdeck::Pool().size(); ++i)
    { if (std::string(opponentdeck::Pool()[i].name) == list) { return static_cast<int>(i); } }
    FAIL("no pool list ", list);
    return -1;
}

// The list index (bit) of the `nth` copy of `name` in pool list `slot` (Deal's numbering).
int L6Bit(int slot, const std::string& name, int nth = 0)
{
    int i = 0;
    for (const std::pair<const char*, int>& e : opponentdeck::Pool()[static_cast<std::size_t>(slot)].cards)
    {
        if (name == e.first)
        {
            REQUIRE(nth < e.second);
            return i + nth;
        }
        i += e.second;
    }
    FAIL("no ", name, " in the list");
    return -1;
}
int L6Num(int slot, const std::string& name, int nth = 0) { return opponentdeck::kNumberBase + L6Bit(slot, name, nth); }

Decklist L6Forests()
{
    Decklist deck;
    for (int i = 0; i < 40; ++i) { deck.mainboard.push_back(L6Def("Forest").card); }
    return deck;
}
Decklist L6DeckWith(const std::vector<std::string>& main)
{
    Decklist deck = L6Forests();
    for (const std::string& n : main) { deck.mainboard.push_back(L6Def(n).card); }
    return deck;
}

// A real-opponent game against the pool's "Auras" list, zones cleared, stamped as an EE lock deck with the rule armed.
GameState L6Fresh()
{
    L6Def("Forest");
    GameState s = GoldFishRunner::SetupGame(L6Forests(), 8, 0);
    REQUIRE(s.opponent_plays_lands);
    s.opponent_slot = static_cast<std::uint8_t>(L6SlotOf("Auras"));
    s.players[1].hand.clear();
    s.opponent2_hand.clear();
    s.battlefield.clear();
    s.opponent_spawns = nullptr;
    s.turn_number = 3;
    s.active_player_index = 0;
    s.deck_has_free_turn = true;
    s.deck_has_ee_lock = true;
    s.deck_has_type_statics = true;
    s.opp_virtual_board = {};
    s.opp_virtual_spent = {};
    // The EE list holds Sterling Grove, so StampDeckTraits sets this; the all-Forest setup deck stamped it false.
    s.deck_has_enchantment_shroud = true;
    return s;
}
std::uint64_t B(int slot, const std::string& name, int nth = 0) { return 1ull << L6Bit(slot, name, nth); }
void L6Hand(GameState& s, int slot, const std::string& name, int nth = 0)
{ s.players[1].hand.push_back(L6Card(name, L6Num(slot, name, nth))); }
void L6OppLand(GameState& s, int slot, const std::string& name, int nth)
{ L6Put(s, name, L6Num(slot, name, nth), 1); }

// Every distinct pin a single-cast plan of `name` carries.
std::set<std::string> L6Pins(const std::vector<TurnSolver::Plan>& plans, const std::string& name)
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
} // namespace

TEST_CASE("EE lock step 6: the combination trait, no reach certificate, the deck stamps")
{
    L6Rule rule;
    CHECK(GoldFishRunner::DeckHasEeLock(L6DeckWith({ kEE, kOpal })));
    CHECK(GoldFishRunner::DeckHasEeLock(L6DeckWith({ kEE, kVerse })));
    CHECK(GoldFishRunner::DeckHasEeLock(L6DeckWith({ kEE, kFoV })));
    CHECK_FALSE(GoldFishRunner::DeckHasEeLock(L6DeckWith({ kEE })));
    CHECK_FALSE(GoldFishRunner::DeckHasEeLock(L6DeckWith({ kOpal, kVerse, kFoV, kWave })));
    CHECK(GoldFishRunner::DeckMayLockOpponent(L6DeckWith({ kEE, kOpal })));
    CHECK_FALSE(GoldFishRunner::DeckMayLockOpponent(L6DeckWith({ kEE })));
    CHECK_FALSE(GoldFishRunner::DeckMayLockOpponent(L6DeckWith({ kOpal })));
    CHECK_FALSE(GoldFishRunner::DeckMayLockOpponent(L6DeckWith({ kVerse })));
    CHECK_FALSE(GoldFishRunner::DeckMayLockOpponent(L6DeckWith({ kFoV })));
    CHECK_FALSE(GoldFishRunner::DeckLockNeedsLandStrip(L6DeckWith({ kEE, kOpal })));
    CHECK(GoldFishRunner::DeckLockNeedsLandStrip(L6DeckWith({ kOpal })));   // nothing else changed

    // The shipped list: the trait, the fairness stamp, and NO certificate (every node stands down).
    const Decklist ee = DeckLoader::LoadFromFile(ResolveHeuristicDefaultsPath(
        "decks/Enchanted Evening/Enchanted Evening.cod"));
    CHECK(GoldFishRunner::DeckHasEeLock(ee));
    const GameState g = GoldFishRunner::SetupGame(ee, 7000, 0);
    CHECK(g.deck_has_free_turn);
    CHECK(g.deck_has_ee_lock);
    CHECK_FALSE(g.deck_free_turn_hand_intact);
    CHECK(freeturn::FreeTurnMayFollow(g));
    CHECK(freeturn::ReachAuditBefore(g) == -1);   // MTG_FREE_TURN_REACH_AUDIT has nothing to certify here
}

TEST_CASE("EE lock step 6: no other deck takes the trait; the pool fits the 64-bit masks")
{
    L6Def("Forest");
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
            CHECK(GoldFishRunner::DeckHasEeLock(d) == (name == "Enchanted Evening"));
            ++decks;
            break;
        }
    }
    CHECK(decks > 20);
    for (const opponentdeck::DeckSpec& spec : opponentdeck::Pool())
    {
        int n = 0;
        for (const auto& e : spec.cards) { n += e.second; }
        INFO("list: " << spec.name);
        CHECK(n <= 64);
    }
    // DefOfBit is Deal's numbering.
    GameState s = L6Fresh();
    const int slot = s.opponent_slot;
    CHECK(eefair::DefOfBit(s, 1, L6Bit(slot, "Slippery Bogle", 2))->card.m_name.str() == "Slippery Bogle");
    CHECK(eefair::DefOfBit(s, 1, L6Bit(slot, "Rancor"))->card.m_name.str() == "Rancor");
}

TEST_CASE("EE fairness: recording -- one land budget per turn, Auras need a host, nothing without lands or unarmed")
{
    L6Rule rule;
    GameState s = L6Fresh();
    const int slot = s.opponent_slot;
    L6Hand(s, slot, "Rancor");
    L6Hand(s, slot, "Slippery Bogle");
    L6Hand(s, slot, "Kor Spiritdancer");
    // No land: nothing.
    eefair::NoteVirtualDeployments(s);
    CHECK(s.opp_virtual_board[0] == 0);
    // One Forest: the Bogle (left to right among the non-Auras: Spiritdancer {1}{W} is not castable); Rancor's budget
    // is gone.
    L6OppLand(s, slot, "Forest", 0);
    eefair::NoteVirtualDeployments(s);
    CHECK(s.opp_virtual_board[0] == B(slot, "Slippery Bogle"));
    // Next turn, Brushland too (G/W): two mana -> the Spiritdancer ({1}{W}); then nothing left for Rancor.
    L6OppLand(s, slot, "Brushland", 0);
    eefair::NoteVirtualDeployments(s);
    CHECK(s.opp_virtual_board[0] == (B(slot, "Slippery Bogle") | B(slot, "Kor Spiritdancer")));
    // Next turn: Rancor onto a host it now has.
    eefair::NoteVirtualDeployments(s);
    CHECK((s.opp_virtual_board[0] & B(slot, "Rancor")) != 0);

    // An Aura with no possible host is never recorded.
    GameState a = L6Fresh();
    L6Hand(a, slot, "Rancor");
    L6OppLand(a, slot, "Forest", 0);
    eefair::NoteVirtualDeployments(a);
    CHECK(a.opp_virtual_board[0] == 0);

    // A card already removed (spent) is not deployed again; an unarmed deck records nothing.
    GameState p = L6Fresh();
    L6Hand(p, slot, "Slippery Bogle");
    L6OppLand(p, slot, "Forest", 0);
    p.opp_virtual_spent[0] = B(slot, "Slippery Bogle");
    eefair::NoteVirtualDeployments(p);
    CHECK(p.opp_virtual_board[0] == 0);
    GameState u = L6Fresh();
    u.deck_has_ee_lock = false;
    L6Hand(u, slot, "Slippery Bogle");
    L6OppLand(u, slot, "Forest", 0);
    eefair::NoteVirtualDeployments(u);
    CHECK(u.opp_virtual_board[0] == 0);
    {
        int saved = freeturn::t_force;
        freeturn::t_force = 0;   // MTG_FREE_TURNS off
        GameState o = L6Fresh();
        L6Hand(o, slot, "Slippery Bogle");
        L6OppLand(o, slot, "Forest", 0);
        eefair::NoteVirtualDeployments(o);
        CHECK(o.opp_virtual_board[0] == 0);
        freeturn::t_force = saved;
    }
}

TEST_CASE("EE fairness: Calming Verse is the wipe point -- under EE it clears everything, otherwise enchantments only")
{
    L6Rule rule;
    GameState s = L6Fresh();
    const int slot = s.opponent_slot;
    s.opp_virtual_board[0] = B(slot, "Slippery Bogle") | B(slot, "Rancor");
    GameState e = s;
    L6Put(e, kEE, 51);
    RefreshTypeStatics(e);
    ResolveCalmingVerse(e, 0, L6Def(kVerse), "VS");
    CHECK(e.opp_virtual_board[0] == 0);
    CHECK(e.opp_virtual_spent[0] == (B(slot, "Slippery Bogle") | B(slot, "Rancor")));
    // The gap restarts: a Bogle cast after the Verse is a new card; the removed one is never redeployed.
    L6Hand(e, slot, "Slippery Bogle", 0);
    L6Hand(e, slot, "Slippery Bogle", 1);
    L6OppLand(e, slot, "Forest", 0);
    eefair::NoteVirtualDeployments(e);
    CHECK(e.opp_virtual_board[0] == B(slot, "Slippery Bogle", 1));
    // Without Enchanted Evening only the enchantment cards (the Aura) are destroyed.
    GameState n = s;
    ResolveCalmingVerse(n, 0, L6Def(kVerse), "VS");
    CHECK(n.opp_virtual_board[0] == B(slot, "Slippery Bogle"));
}

TEST_CASE("EE fairness: the veto -- a castable creature breaks EE + Opalescence; the board lock alone is not enough")
{
    L6Rule rule;
    GameState s = L6Fresh();
    const int slot = s.opponent_slot;
    L6Hand(s, slot, "Ethereal Armor");   // a nonland in hand; no land: HeadLocked
    L6Put(s, kEE, 51);
    L6Put(s, kOpal, 52);
    RefreshTypeStatics(s);
    CHECK(freeturn::LockHolds(s));
    s.opp_virtual_board[0] = B(slot, "Kor Spiritdancer");
    CHECK(freeturn::LockHoldsOnBoard(s));
    CHECK(eefair::Veto(s) == eefair::kVirtualCreature);
    CHECK_FALSE(freeturn::LockHolds(s));
    freeturn::NoteOpponentTurnEnd(s);
    CHECK_FALSE(s.pending_free_turn);
    // A noncreature Aura is never a creature (Opalescence spares Auras): no veto.
    s.opp_virtual_board[0] = B(slot, "Ethereal Armor");
    CHECK(eefair::Veto(s) == eefair::kNone);
    freeturn::NoteOpponentTurnEnd(s);
    CHECK(s.pending_free_turn);
    // Verse + EE without Opalescence: a creature card vetoes, an Aura does not.
    GameState v = L6Fresh();
    L6Hand(v, slot, "Ethereal Armor");
    L6Put(v, kEE, 51);
    RefreshTypeStatics(v);
    v.opp_virtual_board[0] = B(slot, "Slippery Bogle");
    CHECK(eefair::Veto(v) == eefair::kHexproofNoLoop);
    v.opp_virtual_board[0] = B(slot, "Rancor");
    CHECK(eefair::Veto(v) == eefair::kNone);
    // MTG_FREE_TURN_EE_FAIR's masks are zero for an unstamped deck: nothing to veto.
    GameState z = L6Fresh();
    CHECK(eefair::Veto(z) == eefair::kNone);
}

TEST_CASE("EE fairness: hexproof (rulings 9-11) -- contained under an able Wave unless trample and power >= 5 with its Auras")
{
    L6Rule rule;
    static const EnchantedEveningProvider prov;
    GameState s = L6Fresh();
    const int slot = s.opponent_slot;
    s.m_provider = &prov;
    s.deck_has_wave_opal_sweep = true;
    L6Put(s, kEE, 51);
    L6Put(s, kOpal, 52);
    L6Put(s, kWave, 53);
    RefreshTypeStatics(s);
    const std::uint64_t bogle = B(slot, "Slippery Bogle");
    s.opp_virtual_board[0] = bogle;
    CHECK(eefair::WaveLoopAble(s));
    CHECK(eefair::Veto(s) == eefair::kNone);                        // a 1/1: contained
    s.opp_virtual_board[0] = bogle | B(slot, "Rancor");             // 1 (MV) + 2 = 3, trample: contained
    CHECK(eefair::Veto(s) == eefair::kNone);
    s.opp_virtual_board[0] = bogle | B(slot, "Daybreak Coronet") | B(slot, "Hyena Umbra");   // 5, NO trample
    CHECK(eefair::Veto(s) == eefair::kNone);
    s.opp_virtual_board[0] = bogle | B(slot, "Rancor") | B(slot, "Daybreak Coronet");       // 1 + 2 + 3 = 6, trample
    CHECK(eefair::Veto(s) == eefair::kHexproofThreat);
    CHECK_FALSE(freeturn::LockHolds(s));
    // Ancestral Mask counts every other enchantment on the battlefield -- under EE, ours too.
    s.opp_virtual_board[0] = bogle | B(slot, "Rancor") | B(slot, "Ancestral Mask");
    CHECK(eefair::Veto(s) == eefair::kHexproofThreat);
    // Ruling 17: two Groves make the loop impossible -- nothing contains even the 1/1.
    GameState g = s;
    L6Put(g, kGrove, 54);
    L6Put(g, kGrove, 55);
    RefreshTypeStatics(g);
    g.opp_virtual_board[0] = bogle;
    CHECK_FALSE(eefair::WaveLoopAble(g));
    CHECK(eefair::Veto(g) == eefair::kHexproofNoLoop);
    // Ruling 11: a Verse with EE out destroys the creature AND its Auras; the lock is back.
    s.opp_virtual_board[0] = bogle | B(slot, "Rancor") | B(slot, "Daybreak Coronet");
    ResolveCalmingVerse(s, 0, L6Def(kVerse), "VS");
    CHECK(s.opp_virtual_board[0] == 0);
    // ...except under umbra armor (CR 702.89a): the umbra is destroyed instead, so the hexproof creature survives the
    // Verse bare (its Auras all die); a bare 1/1 Bogle is then contained again under the able loop.
    const std::uint64_t gs = B(slot, "Gladecover Scout");
    s.opp_virtual_board[0] = gs | bogle | B(slot, "Spider Umbra") | B(slot, "Rancor");
    s.opp_virtual_spent[0] = 0;
    ResolveCalmingVerse(s, 0, L6Def(kVerse), "VS");
    const std::uint64_t left = s.opp_virtual_board[0];
    CHECK((left == gs || left == bogle));   // exactly ONE creature survives (all Auras are assumed on it)
    CHECK(s.opp_virtual_spent[0] == ((gs | bogle | B(slot, "Spider Umbra") | B(slot, "Rancor")) & ~left));
    CHECK(eefair::Veto(s) == eefair::kNone);
}

TEST_CASE("EE fairness: the Wave + Opalescence sweep exiles targetable virtual creatures, not hexproof ones; ruling 17 none")
{
    L6Rule rule;
    static const EnchantedEveningProvider prov;
    GameState s = L6Fresh();
    const int slot = s.opponent_slot;
    s.m_provider = &prov;
    s.deck_has_wave_opal_sweep = true;
    L6Put(s, kEE, 51);
    L6Put(s, kOpal, 52);
    L6Put(s, kWave, 53);
    RefreshTypeStatics(s);
    const std::uint64_t kor = B(slot, "Kor Spiritdancer"), bogle = B(slot, "Slippery Bogle");
    s.opp_virtual_board[0] = kor | bogle | B(slot, "Rancor");
    GameState n = s;
    PerformWaveOpalSweep(s);
    CHECK(s.opp_virtual_board[0] == (bogle | B(slot, "Rancor")));
    CHECK(s.opp_virtual_spent[0] == kor);
    int counters = -1;
    for (const Permanent& p : s.battlefield) { if (p.card.m_name.str() == kWave) { counters = p.fade_counters; } }
    CHECK(counters == 5);   // the loop ran: the Wave is back as a new object with five
    // Ruling 17: two Groves -> no loop -> the sweep exiles nothing, real or virtual, and keeps every counter.
    L6Put(n, kGrove, 54);
    L6Put(n, kGrove, 55);
    RefreshTypeStatics(n);
    for (Permanent& p : n.battlefield) { if (p.card.m_name.str() == kWave) { p.fade_counters = 4; } }
    PerformWaveOpalSweep(n);
    CHECK(n.opp_virtual_board[0] == (kor | bogle | B(slot, "Rancor")));
    CHECK(n.opp_virtual_spent[0] == 0);
    for (const Permanent& p : n.battlefield) { if (p.card.m_name.str() == kWave) { CHECK(p.fade_counters == 4); } }
}

TEST_CASE("EE fairness: Force of Vigor may target the virtual Auras of a hexproof threat (ruling 13), a searched cast")
{
    L6Rule rule;
    static const EnchantedEveningProvider prov;
    GameState s = L6Fresh();
    const int slot = s.opponent_slot;
    s.m_provider = &prov;
    s.phase = Phase::PreCombatMain;
    s.step = Step::MainPhase;
    s.players[0].lands_played_this_turn = 1;
    for (int k = 0; k < 8; ++k) { s.players[0].library.push_back(L6Card("Plains", 900 + k)); }
    L6Put(s, kEE, 51);
    for (int k = 0; k < 4; ++k) { L6Put(s, "Forest", 101 + k); }
    RefreshTypeStatics(s);
    s.players[0].hand.push_back(L6Card(kFoV, 30));
    const int rancor = L6Num(slot, "Rancor"), coronet = L6Num(slot, "Daybreak Coronet");
    const int bogle_n = L6Num(slot, "Slippery Bogle");
    // No threat (a bare Bogle): no virtual target is offered.
    s.opp_virtual_board[0] = B(slot, "Slippery Bogle");
    for (const std::string& pin : L6Pins(TurnSolver::EnumerateMainPlans(s, true), kFoV))
    {
        std::vector<int> nums;
        REQUIRE(DestroyUpToArtEnchPinTargets(pin, nums));
        for (int n : nums) { CHECK(n < opponentdeck::kNumberBase); }
    }
    // The threat (1 + 2 + 3 = 6 with Rancor's trample): each Aura class, the pair, never the hexproof creature.
    s.opp_virtual_board[0] = B(slot, "Slippery Bogle") | B(slot, "Rancor") | B(slot, "Daybreak Coronet");
    const std::set<std::string> pins = L6Pins(TurnSolver::EnumerateMainPlans(s, true), kFoV);
    CHECK(pins.count(DestroyUpToArtEnchPin({ rancor })) == 1);
    CHECK(pins.count(DestroyUpToArtEnchPin({ coronet })) == 1);
    CHECK((pins.count(DestroyUpToArtEnchPin({ rancor, coronet })) + pins.count(DestroyUpToArtEnchPin({ coronet, rancor })))
          == 1);
    for (const std::string& pin : pins) { CHECK(pin.find(std::to_string(bogle_n)) == std::string::npos); }
    // Resolving the Rancor pin clears it for good; the Bogle is now a 4/4 without trample (contained).
    GameState r = s;
    ResolveDestroyUpToArtEnch(r, 0, L6Def(kFoV), DestroyUpToArtEnchPin({ rancor }));
    CHECK(r.opp_virtual_board[0] == (B(slot, "Slippery Bogle") | B(slot, "Daybreak Coronet")));
    CHECK(r.opp_virtual_spent[0] == B(slot, "Rancor"));
    CHECK(eefair::FovVirtualAuraClasses(r).empty());   // no threat left: the window closes
    // The rollout applies the same pinned cast.
    std::optional<TurnSolver::Plan> plan;
    for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(s, true))
    {
        if (p.actions.size() == 1 && p.actions.front().card_name.str() == kFoV
            && p.actions.front().tutor_target.str() == DestroyUpToArtEnchPin({ rancor }))
        { plan = p; }
    }
    REQUIRE(plan.has_value());
    GameState roll = s;
    TurnSolver::ApplyPlan(roll, *plan, true);
    CHECK(roll.opp_virtual_board[0] == (B(slot, "Slippery Bogle") | B(slot, "Daybreak Coronet")));
}
