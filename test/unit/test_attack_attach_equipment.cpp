// Unit tests for Raubahn, Bull of Ala Mhigo's attack trigger -- "whenever Raubahn attacks, attach up
// to one target Equipment you control to target attacking creature" -- the shared
// FireAttackAttachEquipment body (both combat worlds + the attack projection), the provider's
// AttackAttachCandidates ordering behind it, the SEARCHED combat pin (Plan::attack_attach_choice ->
// GameState::scripted_attack_attach) that picks a rank of that list, and its projection addend
// (CountAttackAttachPump, through PendingAttackDamage).
//
// WHY UNIT TESTS. The trigger's failure modes are quiet: an attach the projection does not price
// (the search under-rates the attack and fd-diverges), a pair the list cannot express (a greedy pick
// inside the search window), a pin that leaks past its combat, an attached Lizard Blades that keeps
// dealing damage after it stopped being a creature. None of them crashes; the deck is not in the
// regression suite yet. These pin each one board by board.
#include <doctest/doctest.h>

#include "ai/DecisionProviders.h"
#include "ai/TurnSolver.h"
#include "cards/CardDatabase.h"
#include "core/GameSetup.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"

#include <algorithm>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace
{

void EnsureCardsLoaded()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
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

const Permanent* Find(const GameState& s, int number)
{
    for (const Permanent& p : s.battlefield) { if (p.card.m_number == number) { return &p; } }
    return nullptr;
}

int AttachedTo(const GameState& s, int equip_number)
{
    const Permanent* p = Find(s, equip_number);
    return p ? p->equipped_to : -1;
}

void Attach(GameState& s, int equip_number, int host_number)
{
    for (Permanent& p : s.battlefield) { if (p.card.m_number == equip_number) { p.equipped_to = host_number; } }
    RefreshReconfigureCreatures(s);
}

std::vector<int> Idx(const GameState& s, const std::vector<int>& nums)
{
    std::vector<int> out;
    for (int n : nums)
    {
        for (int i = 0; i < static_cast<int>(s.battlefield.size()); ++i)
        { if (s.battlefield[static_cast<std::size_t>(i)].card.m_number == n) { out.push_back(i); break; } }
    }
    return out;
}

std::vector<std::pair<int, int>> Attachments(const GameState& s)
{
    std::vector<std::pair<int, int>> v;
    for (const Permanent& p : s.battlefield) { v.emplace_back(p.card.m_number, p.equipped_to); }
    std::sort(v.begin(), v.end());
    return v;
}

// Fire the trigger on a copy with `pin`, returning the post-trigger board and the repaired atk_idx.
GameState FireWith(const GameState& s, const std::vector<int>& atk_nums, int pin, std::vector<int>* atk_out = nullptr)
{
    GameState t = s;
    t.scripted_attack_attach = static_cast<std::int16_t>(pin);
    std::vector<int> atk = Idx(t, atk_nums);
    FireAttackAttachEquipment(t, 0, atk, /*live=*/false);
    if (atk_out) { *atk_out = atk; }
    return t;
}

int OppLifeDrop(GameState s)
{
    const int before = s.players[1].life;
    RolloutSimulateCombat(s);
    return before - s.players[1].life;
}

}   // namespace

TEST_CASE("Raubahn: a loose Colossus Hammer goes to the attacking double striker (the front)")
{
    EnsureCardsLoaded();
    GameState s = Fresh();
    const int raubahn = Put(s, "Raubahn, Bull of Ala Mhigo", 10);
    const int duelist = Put(s, "Kor Duelist", 20);
    const int hammer  = Put(s, "Colossus Hammer", 1);
    // Hammer -> Duelist: (1+10) x 2 = 22 over a bare 1 (gain 21) beats Hammer -> Raubahn (gain 10).
    const GameState t = FireWith(s, { raubahn, duelist }, -1);
    CHECK(AttachedTo(t, hammer) == duelist);
    CHECK_FALSE(t.players[0].equip_activated_this_turn);   // not an equip ability: no waiver spent
    CHECK(t.scripted_attack_attach == -1);
    CHECK(TurnSolver::ProjectedAttackDamageForTest(s) == 24);   // 2 + 22
    CHECK(OppLifeDrop(s) == 24);
}

TEST_CASE("Raubahn: takes the Hammer off a summoning-sick carrier onto the attacker")
{
    EnsureCardsLoaded();
    GameState s = Fresh();
    const int raubahn = Put(s, "Raubahn, Bull of Ala Mhigo", 10);
    const int duelist = Put(s, "Kor Duelist", 20);
    const int sick    = Put(s, "Kor Duelist", 21, /*sick=*/true);
    const int hammer  = Put(s, "Colossus Hammer", 1);
    Attach(s, hammer, sick);
    const GameState t = FireWith(s, { raubahn, duelist }, -1);
    CHECK(AttachedTo(t, hammer) == duelist);
    CHECK(OppLifeDrop(s) == TurnSolver::ProjectedAttackDamageForTest(s));
}

TEST_CASE("Raubahn: declines when every move loses damage (attach nothing is the front on a tie or loss)")
{
    EnsureCardsLoaded();
    GameState s = Fresh();
    const int raubahn = Put(s, "Raubahn, Bull of Ala Mhigo", 10);
    const int duelist = Put(s, "Kor Duelist", 20);
    const int hammer  = Put(s, "Colossus Hammer", 1);
    Attach(s, hammer, duelist);
    const GameState t = FireWith(s, { raubahn, duelist }, -1);
    CHECK(Attachments(t) == Attachments(s));
    // ...but the losing move is still IN the list (rank 1): the search may take it.
    const GameState u = FireWith(s, { raubahn, duelist }, 1);
    CHECK(AttachedTo(u, hammer) == raubahn);
}

TEST_CASE("Raubahn: every rank reaches a distinct legal board; shroud, non-attackers and refused attaches never appear")
{
    EnsureCardsLoaded();
    GameState s = Fresh();
    const int raubahn  = Put(s, "Raubahn, Bull of Ala Mhigo", 10);   // power 2
    const int duelist  = Put(s, "Kor Duelist", 20);                  // power 1, attacking
    const int greaved  = Put(s, "Swiftblade Vindicator", 30);        // attacking, wears Greaves -> shroud
    const int idle     = Put(s, "Kor Duelist", 21, /*sick=*/true);   // not attacking
    const int hammer   = Put(s, "Colossus Hammer", 1);
    const int naginata = Put(s, "O-Naginata", 2);                    // min power 3: no host qualifies
    const int greaves  = Put(s, "Lightning Greaves", 3);
    Attach(s, greaves, greaved);
    const std::vector<int> atk{ raubahn, duelist, greaved };
    // Legal: nothing; Hammer -> Raubahn / Duelist; Greaves -> Raubahn / Duelist (it MOVES off the
    // Vindicator). Never: anything onto the shrouded Vindicator or the idle Duelist; O-Naginata anywhere.
    std::set<std::vector<std::pair<int, int>>> boards;
    for (int k = 0; k < 12; ++k)
    {
        const GameState t = FireWith(s, atk, k);
        boards.insert(Attachments(t));
        CHECK(AttachedTo(t, naginata) == 0);
        CHECK(AttachedTo(t, hammer) != greaved);
        CHECK(AttachedTo(t, hammer) != idle);
        CHECK((AttachedTo(t, greaves) == greaved || AttachedTo(t, greaves) == raubahn || AttachedTo(t, greaves) == duelist));
    }
    CHECK(boards.size() == 5);
    std::set<std::vector<std::pair<int, int>>> want;
    want.insert(Attachments(s));
    for (const auto& pr : std::vector<std::pair<int, int>>{ { hammer, raubahn }, { hammer, duelist },
                                                            { greaves, raubahn }, { greaves, duelist } })
    {
        GameState w = s;
        ApplyEquip(w, 0, pr.first, pr.second);
        want.insert(Attachments(w));
    }
    CHECK(boards == want);
    // Projection == realised on EVERY rank (the copy-and-diff addend runs the very same body).
    for (int k = -1; k < 6; ++k)
    {
        GameState p = s;
        p.scripted_attack_attach = static_cast<std::int16_t>(k);
        CHECK(TurnSolver::ProjectedAttackDamageForTest(p) == OppLifeDrop(p));
    }
}

TEST_CASE("Raubahn: identical copies fold -- two loose Hammers and two bare Vindicators are one move")
{
    EnsureCardsLoaded();
    GameState s = Fresh();
    const int raubahn = Put(s, "Raubahn, Bull of Ala Mhigo", 10);
    const int v1 = Put(s, "Swiftblade Vindicator", 30);
    const int v2 = Put(s, "Swiftblade Vindicator", 31);
    Put(s, "Colossus Hammer", 1);
    Put(s, "Colossus Hammer", 2);
    // Classes: nothing, Hammer -> Raubahn, Hammer -> a Vindicator = 3 entries; rank 3+ = the front.
    std::set<std::vector<std::pair<int, int>>> boards;
    for (int k = 0; k < 8; ++k) { boards.insert(Attachments(FireWith(s, { raubahn, v1, v2 }, k))); }
    CHECK(boards.size() == 3);
    CHECK(Attachments(FireWith(s, { raubahn, v1, v2 }, 3)) == Attachments(FireWith(s, { raubahn, v1, v2 }, 0)));
}

TEST_CASE("Raubahn: the pin is THIS combat's -- consumed when nothing attacked, out of range = the front")
{
    EnsureCardsLoaded();
    GameState s = Fresh();
    Put(s, "Raubahn, Bull of Ala Mhigo", 10, /*sick=*/true);   // cannot attack
    const int duelist = Put(s, "Kor Duelist", 20);
    const int hammer  = Put(s, "Colossus Hammer", 1);
    s.scripted_attack_attach = 2;
    GameState t = s;
    RolloutSimulateCombat(t);
    CHECK(t.scripted_attack_attach == -1);
    CHECK(AttachedTo(t, hammer) == 0);   // no Raubahn attacked: nothing moved
    GameState u = Fresh();
    const int r2 = Put(u, "Raubahn, Bull of Ala Mhigo", 10);
    const int d2 = Put(u, "Kor Duelist", 20);
    const int h2 = Put(u, "Colossus Hammer", 1);
    (void)duelist;
    CHECK(Attachments(FireWith(u, { r2, d2 }, 99)) == Attachments(FireWith(u, { r2, d2 }, -1)));
    CHECK(AttachedTo(FireWith(u, { r2, d2 }, 99), h2) == d2);
}

TEST_CASE("Raubahn: the searched axis is emitted pre-combat only, and only while Raubahn can attack")
{
    EnsureCardsLoaded();
    auto board = [](bool raubahn_sick)
    {
        GameState s = Fresh();
        Put(s, "Raubahn, Bull of Ala Mhigo", 10, raubahn_sick);
        Put(s, "Kor Duelist", 20);
        Put(s, "Colossus Hammer", 1);
        for (int i = 0; i < 3; ++i) { Put(s, "Plains", 50 + i); }
        Card gift = Def("Steelshaper's Gift").card;
        gift.m_number = 60;
        s.players[0].hand.push_back(gift);
        return s;
    };
    auto max_pin = [](const std::vector<TurnSolver::Plan>& plans)
    {
        int m = -1;
        for (const TurnSolver::Plan& p : plans) { m = std::max(m, p.attack_attach_choice); }
        return m;
    };
    const GameState s = board(false);
    // E = Hammer + the Gift (an Equipment tutor) = 2, H = Raubahn + Duelist = 2 -> W = 5, ranks 1..4.
    CHECK(max_pin(TurnSolver::EnumerateMainPlans(s, true)) == 4);
    CHECK(max_pin(TurnSolver::EnumerateMainPlans(s, false)) == -1);   // post-combat: the pin is dead
    CHECK(max_pin(TurnSolver::EnumerateMainPlans(board(true), true)) == -1);   // Raubahn sick, no haste
}

TEST_CASE("Raubahn: the pending pin is part of the dedup key and is cleared at the turn start")
{
    EnsureCardsLoaded();
    GameState s = Fresh();
    Put(s, "Raubahn, Bull of Ala Mhigo", 10);
    GameState t = s;
    t.scripted_attack_attach = 1;
    CHECK(TurnSolver::DedupKeyOf(s) != TurnSolver::DedupKeyOf(t));
}

TEST_CASE("Raubahn: projection == realised with Ardenn's begin-combat attach first, front and pinned ranks")
{
    EnsureCardsLoaded();
    GameState s = Fresh();
    Put(s, "Ardenn, Intrepid Archaeologist", 40);
    Put(s, "Raubahn, Bull of Ala Mhigo", 10);
    Put(s, "Kor Duelist", 20);
    Put(s, "Colossus Hammer", 1);
    Put(s, "Loxodon Warhammer", 2);
    REQUIRE(HasBeginCombatAttachSource(s, 0));
    std::set<int> drops;
    for (int k = -1; k < 6; ++k)
    {
        GameState p = s;
        p.scripted_attack_attach = static_cast<std::int16_t>(k);
        const int realised = OppLifeDrop(p);
        CHECK(TurnSolver::ProjectedAttackDamageForTest(p) == realised);
        drops.insert(realised);
    }
    CHECK(drops.size() > 1);   // the ranks really reach different combats on Ardenn's board
}

TEST_CASE("Raubahn: an attacking Lizard Blades attached to Raubahn leaves combat and grants him double strike")
{
    EnsureCardsLoaded();
    GameState s = Fresh();
    s.deck_has_reconfigure = true;
    const int raubahn = Put(s, "Raubahn, Bull of Ala Mhigo", 10);
    const int lizard  = Put(s, "Lizard Blades", 5);
    REQUIRE(Find(s, lizard)->card.IsCreature());
    // Before: Raubahn 2 + Lizard 1 x 2 = 4. After Lizard -> Raubahn: Raubahn 2 x 2 = 4, Lizard gone
    // from combat. A tie -> the front declines; rank 1 is the attach.
    std::vector<int> atk;
    const GameState t = FireWith(s, { raubahn, lizard }, 1, &atk);
    CHECK(AttachedTo(t, lizard) == raubahn);
    CHECK_FALSE(Find(t, lizard)->card.IsCreature());
    CHECK(Find(t, lizard)->tapped);                 // it was declared: the attack's tap stays
    REQUIRE(atk.size() == 1);
    CHECK(t.battlefield[static_cast<std::size_t>(atk[0])].card.m_number == raubahn);
    CHECK(CreatureHasDoubleStrike(*Find(t, raubahn), t));
    // The Lizard is never offered onto itself: every rank is one of the two boards.
    std::set<std::vector<std::pair<int, int>>> boards;
    for (int k = 0; k < 6; ++k) { boards.insert(Attachments(FireWith(s, { raubahn, lizard }, k))); }
    CHECK(boards.size() == 2);
    for (int k = -1; k < 3; ++k)
    {
        GameState p = s;
        p.scripted_attack_attach = static_cast<std::int16_t>(k);
        CHECK(TurnSolver::ProjectedAttackDamageForTest(p) == OppLifeDrop(p));
    }
    GameState p1 = s;
    p1.scripted_attack_attach = 1;
    CHECK(OppLifeDrop(p1) == 4);
}

TEST_CASE("Raubahn: ApplyPlan writes the plan's pin and the next combat takes that rank")
{
    EnsureCardsLoaded();
    GameState s = Fresh();
    const int raubahn = Put(s, "Raubahn, Bull of Ala Mhigo", 10);
    const int duelist = Put(s, "Kor Duelist", 20);
    const int hammer  = Put(s, "Colossus Hammer", 1);
    Attach(s, hammer, duelist);
    TurnSolver::Plan idle;
    idle.land_decided = true;
    idle.attack_attach_choice = 1;   // rank 1 = the losing move (Hammer -> Raubahn); the front declines
    GameState t = s;
    TurnSolver::ApplyPlan(t, idle, /*is_pre_combat=*/true);
    CHECK(t.scripted_attack_attach == 1);
    RolloutSimulateCombat(t);
    CHECK(AttachedTo(t, hammer) == raubahn);
    CHECK(t.scripted_attack_attach == -1);
    CHECK(t.players[1].life == s.players[1].life - (12 + 1));   // Raubahn 12, the now-bare Duelist 1
}

TEST_CASE("Raubahn: a main with nothing to cast still searches the attach (the idle pass carries the ranks)")
{
    EnsureCardsLoaded();
    GameState s = Fresh();
    const int raubahn = Put(s, "Raubahn, Bull of Ala Mhigo", 10);
    const int duelist = Put(s, "Kor Duelist", 20);
    const int hammer  = Put(s, "Colossus Hammer", 1);
    (void)raubahn; (void)duelist; (void)hammer;
    // No hand, no lands, the drop spent: EnumerateMainPlans is empty, so the host appends the pass --
    // and its rank variants (AppendIdleAttackAttachSiblings). The search returns a legal pass whose pin
    // (if any) is a rank the trigger can take, and the line it scores wins this turn (2 + 22 >= 20).
    REQUIRE(TurnSolver::EnumerateMainPlans(s, true).empty());
    const TurnSolver::Plan pick = TurnSolver::SolveWithLookahead(s, /*is_pre_combat=*/true, /*depth=*/2);
    CHECK(pick.actions.empty());
    CHECK(pick.attack_attach_choice < 3);
    GameState t = s;
    TurnSolver::ApplyPlan(t, pick, /*is_pre_combat=*/true);
    RolloutSimulateCombat(t);
    CHECK(t.players[1].life <= 0);
}
