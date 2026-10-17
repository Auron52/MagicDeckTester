// Unit tests for the Colossus Hammer claude-play RECHECK fixes (2026-10-17, seeds 2100017 / 2100018):
//   * ORDER-AWARE (d) -- a target-less Path to Exile (the human's one menu entry; the board click picks
//     its creature at resolution) frees a Magnetic Theft's piece only when it is cast AFTER the Theft.
//     The human cast-ORDER menu offered "Colossus Hammer, Path to Exile, Speaker of the Heavens, Magnetic
//     Theft (Lizard Blades) -> Speaker, equip Colossus Hammer -> Lizard Blades" (seed 2100017 T6): the
//     Equip has no legal target. TurnSolver::AttachOrderStrandsPlan is driven with human=true here,
//     because HumanPlayActive() is a process-static MTG_HUMAN_PLAY read a unit test cannot switch on;
//     the end-to-end menu is the claude-play repro in the commit message.
//   * (A) the loose-Lizard fold (c) of SubsetHasStrandedAttachSpell now covers a HAND-CAST Lizard Blades
//     (seed 2100018 T7: "Lizard Blades, Magnetic Theft (Colossus Hammer) -> Lizard Blades, equip Lizard
//     Blades -> Kor Duelist" -- the reconfigure attaches the Lizard, the Hammer falls off).
//   * (B) CR 601.2c -- a Magnetic Theft with no legal (Equipment, creature) pair is not cast, in BOTH
//     worlds (AttachSpellCastLegal): seed 2100018 T4's Path to Exile on the only creature, then the Theft.
//     (Its mana is not spent ON it, but a multi-cast line's whole-turn prepay -- BatchPrepayMainCasts,
//     both worlds -- has already tapped it into the pool before the first cast, so the test asserts the
//     card, not the land.)
#include <doctest/doctest.h>

#include "ai/AIEngine.h"
#include "ai/DecisionProviders.h"
#include "ai/TurnSolver.h"
#include "cards/CardDatabase.h"
#include "core/EffectHandler.h"
#include "core/GameSetup.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"
#include "mtg_test_seam.h"

#include <algorithm>
#include <string>
#include <vector>

namespace
{

void EnsureCardsAo()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(
            ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

const CardDefinition& DefAo(const std::string& name)
{
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE_MESSAGE(d != nullptr, "card not in cards.json: ", name);
    return *d;
}

GameState FreshAo(int turn)
{
    EnsureCardsAo();
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = turn;
    s.players[0].life     = gamesetup::StartingLife();
    s.players[1].life     = gamesetup::StartingLife();
    s.players[0].lands_played_this_turn = 1;   // land drop spent: no land-fold plan variants
    for (int k = 0; k < 20; ++k)
    {
        Card c = DefAo("Plains").card;
        c.m_number = 900 + k;
        s.players[0].library.push_back(c);
    }
    return s;
}

Permanent& PutAo(GameState& s, const std::string& name, int number)
{
    Permanent p;
    p.card             = DefAo(name).card;
    p.card.m_number    = number;
    p.controller_index = 0;
    p.owner_index      = 0;
    s.battlefield.push_back(p);
    return s.battlefield.back();
}

void HandAo(GameState& s, const std::string& name, int number)
{
    Card c = DefAo(name).card;
    c.m_number = number;
    s.players[0].hand.push_back(c);
}

int HandIndexAo(const GameState& s, int number)
{
    for (int i = 0; i < static_cast<int>(s.players[0].hand.size()); ++i)
    { if (s.players[0].hand[static_cast<std::size_t>(i)].m_number == number) { return i; } }
    return -1;
}

Action CastAo(const GameState& s, const std::string& name, int number)
{
    const CardDefinition& d = DefAo(name);
    Action a;
    a.kind       = Action::Kind::CastFromHand;
    a.card_name  = d.card.m_name;
    a.def        = &d;
    a.hand_index = HandIndexAo(s, number);
    a.cost       = d.card.m_mana_cost;
    a.card_mv    = d.card.m_mana_cost.ManaValue();
    return a;
}

void ResolveAllAo(GameState& st)
{
    while (!st.stack.empty())
    {
        const StackEntry e = st.stack.back();
        st.stack.pop_back();
        const CardDefinition* d = CardDatabase::Instance().LookupCached(e.source);
        if (d != nullptr) { EffectHandler::Resolve(st, e, *d); }
    }
}
GameState RollAo(const GameState& s, const TurnSolver::Plan& p)
{
    GameState t = s;
    TurnSolver::ApplyPlan(t, p, /*is_pre_combat=*/true);
    return t;
}
GameState ExecAo(const GameState& s, const TurnSolver::Plan& p)
{
    AIEngine eng(MulliganProfile::DefaultProfile(), 3, 20);
    MtgTestSeam::CommitPhasePlan(eng, p, true);
    GameState t = s;
    eng.TakeTurn(t, true, ResolveAllAo);
    return t;
}
// Where every non-land permanent is attached -- the board up to mana and zones.
std::string AttachKeyAo(const GameState& g)
{
    std::vector<std::string> v;
    for (const Permanent& p : g.battlefield)
    {
        if (p.card.IsLand()) { continue; }
        v.push_back(std::to_string(p.card.m_number) + "@" + std::to_string(p.equipped_to));
    }
    std::sort(v.begin(), v.end());
    std::string k;
    for (const std::string& x : v) { k += x + ";"; }
    return k;
}
int UntappedLandsAo(const GameState& g)
{
    int n = 0;
    for (const Permanent& p : g.battlefield) { if (p.card.IsLand() && !p.tapped) { ++n; } }
    return n;
}
bool InHandAo(const GameState& g, int number)
{
    for (const Card& c : g.players[0].hand) { if (c.m_number == number) { return true; } }
    return false;
}
bool InGraveyardAo(const GameState& g, int number)
{
    for (const Card& c : g.players[0].graveyard) { if (c.m_number == number) { return true; } }
    return false;
}

}   // namespace

// ---- ORDER-AWARE (d): the seed-2100017 T6 human menu ------------------------------------------------
TEST_CASE("Attach order: a target-less Path cast BEFORE the Theft cannot free its piece; one AFTER it can")
{
    GameState s = FreshAo(6);
    PutAo(s, "Dragonfire Blade", 9).equipped_to = 19;   // Lizard Blades wears it, as on the seed's board
    PutAo(s, "Forge Anew", 10);
    PutAo(s, "Lizard Blades", 19);
    PutAo(s, "Mountain", 26);
    PutAo(s, "Plains", 36);
    PutAo(s, "Plains", 37);
    PutAo(s, "Plains", 38);
    PutAo(s, "Sigarda's Aid", 46);
    HandAo(s, "Colossus Hammer", 5);
    HandAo(s, "Magnetic Theft", 22);
    HandAo(s, "Magnetic Theft", 23);
    HandAo(s, "Path to Exile", 34);
    HandAo(s, "Speaker of the Heavens", 52);

    const Action hammer  = CastAo(s, "Colossus Hammer", 5);
    const Action speaker = CastAo(s, "Speaker of the Heavens", 52);
    const Action path    = CastAo(s, "Path to Exile", 34);      // target-less: the human's menu entry
    Action theft = CastAo(s, "Magnetic Theft", 22);              // Lizard Blades -> the cast Speaker
    theft.attach_equip_id   = 19;
    theft.enchant_target    = 52;
    theft.trick_hand_target = "Speaker of the Heavens";
    theft.is_noncreature    = true;
    Action equip;                                                // equip Colossus Hammer -> Lizard Blades
    equip.kind          = Action::Kind::Equip;
    equip.card_name     = DefAo("Colossus Hammer").card.m_name;
    equip.hand_index    = -1;
    equip.sac_source_id = 5;
    equip.sac_victim_id = 19;

    // The menu's orderings (plans 121, 124, 125, 127 and 123 -- Path first) and its two legal ones (122, 126).
    const std::vector<std::vector<Action>> path_first = {
        { hammer, path, speaker, theft, equip },   // 121
        { hammer, speaker, path, theft, equip },   // 123
        { path, hammer, speaker, theft, equip },   // 124
        { path, speaker, hammer, theft, equip },   // 125
        { speaker, path, hammer, theft, equip },   // 127
    };
    const std::vector<std::vector<Action>> path_after = {
        { hammer, speaker, theft, path, equip },   // 122
        { speaker, theft, path, hammer, equip },   // 126
    };
    for (const std::vector<Action>& acts : path_first)
    { CHECK(TurnSolver::AttachOrderStrandsPlan(s, acts, /*explicit_order=*/true, /*human=*/true)); }
    for (const std::vector<Action>& acts : path_after)
    { CHECK_FALSE(TurnSolver::AttachOrderStrandsPlan(s, acts, /*explicit_order=*/true, /*human=*/true)); }

    // Autonomous play has no target-less Path: never.
    CHECK_FALSE(TurnSolver::AttachOrderStrandsPlan(s, path_first[0], true, /*human=*/false));
    // Nothing to strand without the Equip onto the piece the Theft attaches.
    CHECK_FALSE(TurnSolver::AttachOrderStrandsPlan(s, { hammer, path, speaker, theft }, true, true));
    // ...nor when something ELSE in the plan can still free the piece (a removal TARGETING the Theft's
    // host frees it in either order: first, the Theft has no creature and is not cast).
    {
        Action path_at = path;
        path_at.enchant_target = 52;
        CHECK_FALSE(TurnSolver::AttachOrderStrandsPlan(s, { hammer, path_at, speaker, theft, equip }, true, true));
    }
}

// ---- (A): a Theft onto a HAND-CAST Lizard Blades the plan reconfigures ------------------------------
namespace
{
GameState Seed18T7()
{
    GameState s = FreshAo(7);
    PutAo(s, "Colossus Hammer", 8);          // loose
    PutAo(s, "Furycalm Snarl", 13);
    PutAo(s, "Kor Duelist", 15);
    PutAo(s, "Mountain", 30);
    PutAo(s, "Plains", 40);
    PutAo(s, "Plains", 41);
    PutAo(s, "Plains", 43);
    PutAo(s, "Speaker of the Heavens", 52);
    HandAo(s, "Lizard Blades", 19);
    HandAo(s, "Magnetic Theft", 25);
    HandAo(s, "Magnetic Theft", 22);
    HandAo(s, "Path to Exile", 35);
    return s;
}
bool TheftOntoHandLizard(const Action& a)
{
    return a.kind == Action::Kind::CastFromHand && a.attach_equip_id == 8 && a.trick_hand_target == "Lizard Blades";
}
bool ReconfiguresLizard(const TurnSolver::Plan& p)
{
    for (const Action& a : p.actions) { if (a.kind == Action::Kind::Equip && a.sac_source_id == 19) { return true; } }
    return false;
}
bool HasTheftOntoHandLizard(const TurnSolver::Plan& p)
{
    for (const Action& a : p.actions) { if (TheftOntoHandLizard(a)) { return true; } }
    return false;
}
}   // namespace

TEST_CASE("Attach (A): a Theft onto a hand-cast Lizard Blades the plan reconfigures is dropped; its twin realises the same board cheaper")
{
    const GameState s = Seed18T7();
    const std::vector<TurnSolver::Plan> menu = TurnSolver::EnumerateMainPlans(s, /*is_pre_combat=*/true);
    int dominated = 0, theft_kept = 0;
    const TurnSolver::Plan* twin = nullptr;
    const Action* theft = nullptr;
    for (const TurnSolver::Plan& p : menu)
    {
        const bool t = HasTheftOntoHandLizard(p), r = ReconfiguresLizard(p);
        if (t && r) { ++dominated; }
        if (t && !r)
        {
            ++theft_kept;   // the Theft onto a Lizard that STAYS a creature is a real line
            for (const Action& a : p.actions) { if (TheftOntoHandLizard(a)) { theft = &a; } }
        }
        if (r && !t && twin == nullptr)
        {
            bool only = true;   // the plain twin: cast Lizard Blades, reconfigure it, nothing else
            for (const Action& a : p.actions)
            {
                if (a.kind == Action::Kind::Equip && a.sac_source_id == 19) { continue; }
                if (a.kind == Action::Kind::CastFromHand && a.card_name.str() == "Lizard Blades") { continue; }
                only = false;
            }
            if (only) { twin = &p; }
        }
    }
    MESSAGE("seed-2100018 T7 board (autonomous menu): " << menu.size() << " plans; Theft onto the hand Lizard + its reconfigure: "
            << dominated << "; Theft onto it kept without one: " << theft_kept);
    CHECK(dominated == 0);
    CHECK(theft_kept > 0);
    REQUIRE(twin != nullptr);
    REQUIRE(theft != nullptr);
    // The dropped plan = the twin plus that Theft. Same attachments (the Hammer is attached to the Lizard,
    // then falls off when the reconfigure attaches it), for the Theft card and its {R}, in BOTH worlds.
    TurnSolver::Plan dropped = *twin;
    dropped.actions.push_back(*theft);
    for (int world = 0; world < 2; ++world)
    {
        INFO(std::string(world == 0 ? "rollout" : "executor"));
        const GameState a = world == 0 ? RollAo(s, dropped) : ExecAo(s, dropped);
        const GameState b = world == 0 ? RollAo(s, *twin) : ExecAo(s, *twin);
        CHECK(AttachKeyAo(a) == AttachKeyAo(b));
        CHECK(UntappedLandsAo(b) > UntappedLandsAo(a));
        const int theft_num = s.players[0].hand[static_cast<std::size_t>(theft->hand_index)].m_number;
        CHECK_FALSE(InHandAo(a, theft_num));   // cast (and resolved)...
        CHECK(InHandAo(b, theft_num));         // ...against kept
    }
}

TEST_CASE("Attach (A): refused while Sigarda's Aid could attach the cast Lizard Blades on entry")
{
    GameState s = Seed18T7();
    PutAo(s, "Sigarda's Aid", 46);
    int dominated = 0;
    for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(s, true))
    { if (HasTheftOntoHandLizard(p) && ReconfiguresLizard(p)) { ++dominated; } }
    CHECK(dominated > 0);
}

// ---- (B): CR 601.2c -- a Theft with no legal pair is not cast, in both worlds --------------------------
namespace
{
GameState Seed18T4()
{
    GameState s = FreshAo(4);
    PutAo(s, "Kor Duelist", 15);             // the only creature on either side
    PutAo(s, "Mountain", 30);
    PutAo(s, "Mountain", 31);
    PutAo(s, "Plains", 40);
    PutAo(s, "Plains", 41);
    HandAo(s, "Colossus Hammer", 8);
    HandAo(s, "Magnetic Theft", 25);
    HandAo(s, "Path to Exile", 35);
    return s;
}
}   // namespace

TEST_CASE("Attach (B): a Magnetic Theft whose only creature a Path exiled first is not cast -- card and mana kept, both worlds")
{
    const GameState s = Seed18T4();
    Action path = CastAo(s, "Path to Exile", 35);
    path.enchant_target = 15;                 // the self-target variant: exile Kor Duelist
    const Action hammer = CastAo(s, "Colossus Hammer", 8);
    Action theft = CastAo(s, "Magnetic Theft", 25);
    theft.attach_equip_id       = 8;
    theft.attach_equip_hand     = DefAo("Colossus Hammer").card.m_name;
    theft.attach_equip_hand_ord = 0;
    theft.enchant_target        = 15;
    theft.is_noncreature        = true;

    TurnSolver::Plan first;                   // Path, Hammer, Theft: no creature left for the Theft
    first.actions        = { path, hammer, theft };
    first.searched_order = true;
    TurnSolver::Plan after;                   // Hammer, Theft, Path: the Theft resolves, then the Path
    after.actions        = { hammer, theft, path };
    after.searched_order = true;
    for (int world = 0; world < 2; ++world)
    {
        INFO(std::string(world == 0 ? "rollout" : "executor"));
        const GameState a = world == 0 ? RollAo(s, first) : ExecAo(s, first);
        CHECK(InHandAo(a, 25));               // not cast (CR 601.2c): the card stays in hand
        CHECK_FALSE(InGraveyardAo(a, 25));
        const GameState b = world == 0 ? RollAo(s, after) : ExecAo(s, after);
        CHECK_FALSE(InHandAo(b, 25));         // control: a legal pair at cast time -> cast
    }
    // The shared predicate itself.
    GameState t = s;
    PutAo(t, "Colossus Hammer", 8);
    CHECK(AttachSpellCastLegal(t, 0));
    t.battlefield.erase(std::remove_if(t.battlefield.begin(), t.battlefield.end(),
                        [](const Permanent& p) { return p.card.m_number == 15; }), t.battlefield.end());
    CHECK_FALSE(AttachSpellCastLegal(t, 0));
}
