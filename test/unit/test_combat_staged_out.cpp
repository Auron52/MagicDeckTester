// The rollout's combat runs on the SAME hand as the executor's.
//
// The executor holds unplayed staged cards (a Rundvelt Hordemaster impulse, Light Up the Stage, ...) in
// Player::staged_cards between TakeTurn passes, so its combat sees a hand WITHOUT them; the rollout keeps them
// in hand flagged m_is_staged. A Goblin Lackey put's Goblin Matron ranks its tutor off that hand
// (GoblinsProvider::TutorCandidates: Goblins in hand, an affordable lord in hand, a deployable body), so the two
// worlds fetched different cards -- Goblins seed 2514835 gi4834 at d8 b0: the search's T3 combat fetched Goblin
// Warchief, the real one Goblin King, and the root's verified T3 kill realised as T4. These pin: the combat-time
// tutor sees no staged card in the rollout; the rollout and the executor's representation fetch the same card;
// the staged cards come back AFTER what combat put in hand (the executor's main-2-head layout).
#include <doctest/doctest.h>

#include "ai/DecisionProviders.h"
#include "ai/TurnSolver.h"
#include "cards/CardDatabase.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"

#include <string>
#include <vector>

namespace
{

void LoadCso()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

Card CardCso(const std::string& name, int number, bool staged = false, int expiry = 0)
{
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE_MESSAGE(d != nullptr, "card not in cards.json: ", name);
    Card c = d->card;
    c.m_number = number;
    if (staged) { c.m_is_staged = true; c.m_staged_expiry = expiry; }
    return c;
}

void PutCso(GameState& s, const std::string& name, int number, bool entered_this_turn = false)
{
    Permanent p;
    p.card              = CardCso(name, number);
    p.controller_index  = 0;
    p.owner_index       = 0;
    p.entered_this_turn = entered_this_turn;
    s.battlefield.push_back(p);
}

// The Goblins provider, recording the hand every combat-time tutor ranking reads.
struct RecordingGoblins : GoblinsProvider
{
    mutable int calls = 0;
    mutable int staged_in_hand = 0;   // staged cards visible to the LAST ranking
    mutable int hand_size = -1;
    std::vector<std::string> TutorCandidates(const GameState& s, int controller, const CardParams& pp) const override
    {
        ++calls;
        staged_in_hand = 0;
        for (const Card& c : s.players[controller].hand) { if (c.m_is_staged) { ++staged_in_hand; } }
        hand_size = static_cast<int>(s.players[controller].hand.size());
        return GoblinsProvider::TutorCandidates(s, controller, pp);
    }
};

// gi4834's T3 combat (Goblins seed 2514835, d8 b0, as the executor reached it): Goblin Chieftain, Skirk Prospector
// and two hasted Goblin Lackeys attack into 16; Mountains 24/25 tapped, 26 open. The hand holds the two put
// candidates (Goblin Matron, a second Chieftain) and SIX Rundvelt Hordemaster impulse cards, staged: Twinshot
// Sniper, Goblin King, Siege-Gang Commander, a second Matron, Krenko and Muxus. The plan pins the FIRST Lackey's
// put to rank 1 (Matron; Chieftain is rank 0 by MV), the second takes its front (Chieftain).
GameState Board(const RecordingGoblins& prov)
{
    LoadCso();
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 3;
    s.players[0].life     = 20;
    s.players[1].life     = 16;
    s.phase               = Phase::PreCombatMain;
    s.m_provider          = &prov;
    PutCso(s, "Mountain", 24);
    PutCso(s, "Mountain", 25);
    s.battlefield[0].tapped = true;
    s.battlefield[1].tapped = true;
    PutCso(s, "Goblin Chieftain", 5);
    PutCso(s, "Skirk Prospector", 57);
    PutCso(s, "Mountain", 26);
    PutCso(s, "Goblin Lackey", 10, /*entered_this_turn=*/true);   // Chieftain's haste
    PutCso(s, "Goblin Lackey", 11, /*entered_this_turn=*/true);
    s.players[0].hand.push_back(CardCso("Goblin Matron", 13));
    s.players[0].hand.push_back(CardCso("Goblin Chieftain", 6));
    const int exp = s.TurnSeq() + 1;
    s.players[0].hand.push_back(CardCso("Twinshot Sniper", 60, /*staged=*/true, exp));
    s.players[0].hand.push_back(CardCso("Goblin King", 7, /*staged=*/true, exp));
    s.players[0].hand.push_back(CardCso("Siege-Gang Commander", 52, /*staged=*/true, exp));
    s.players[0].hand.push_back(CardCso("Goblin Matron", 14, /*staged=*/true, exp));
    s.players[0].hand.push_back(CardCso("Krenko, Mob Boss", 20, /*staged=*/true, exp));
    s.players[0].hand.push_back(CardCso("Muxus, Goblin Grandee", 47, /*staged=*/true, exp));
    s.scripted_cheat_choice = 1;
    for (const auto& [nm, num] : std::vector<std::pair<std::string, int>>{
             { "Mountain", 34 }, { "Goblin Warchief", 19 }, { "Goblin King", 8 }, { "Goblin Chainwhirler", 4 },
             { "Siege-Gang Commander", 53 }, { "Pashalik Mons", 48 }, { "Stingscourger", 58 },
             { "Mogg War Marshal", 23 }, { "Goblin Lackey", 12 }, { "Mountain", 35 } })
    { s.players[0].library.push_back(CardCso(nm, num)); }
    return s;
}

// The executor's combat-time representation of the same state: the staged cards in Player::staged_cards.
GameState ExecutorView(const GameState& rollout)
{
    GameState s = rollout;
    std::vector<Card> keep;
    for (const Card& c : s.players[0].hand)
    {
        if (!c.m_is_staged) { keep.push_back(c); continue; }
        Card sc = c;
        sc.m_is_staged = false;
        s.players[0].staged_cards.push_back(StagedCard{ sc, c.m_staged_expiry });
    }
    s.players[0].hand = keep;
    return s;
}

std::vector<int> HandNumbers(const GameState& s)
{
    std::vector<int> v;
    for (const Card& c : s.players[0].hand) { v.push_back(c.m_number); }
    return v;
}

}  // namespace

TEST_CASE("Combat staged-out: the Lackey-put Matron's tutor ranks on the executor's hand")
{
    RecordingGoblins prov;
    GameState s = Board(prov);
    RolloutSimulateCombat(s);
    REQUIRE(prov.calls >= 1);                 // the Lackey connected, Matron entered and tutored
    CHECK(prov.staged_in_hand == 0);          // ... seeing no staged card, as the executor's combat does
    CHECK(prov.hand_size == 1);               // Matron left the hand; the second Chieftain is still in it
}

TEST_CASE("Combat staged-out: the rollout and the executor's representation fetch the same card")
{
    RecordingGoblins prov_r;
    GameState r = Board(prov_r);
    RecordingGoblins prov_e;
    GameState e = ExecutorView(Board(prov_e));
    RolloutSimulateCombat(r);
    RolloutSimulateCombat(e);
    REQUIRE(prov_r.calls >= 1);
    REQUIRE(prov_e.calls >= 1);
    // Executor hand after combat: Matron's fetch (both puts left the hand); its staged cards are held out.
    const std::vector<int> eh = HandNumbers(e);
    REQUIRE(eh.size() == 1);
    // The real game fetched Goblin King (#8) here; the rollout fetched Goblin Warchief while it ranked on the
    // staged cards (buff_targets 8 vs 6, a staged Matron read as the entering tutor source: skirk_ramp 5 vs 4).
    CHECK(eh[0] == 8);
    // Rollout hand after combat: the same fetch, then the staged cards back at the END, in their order (the
    // executor's main-2 head appends them there via MergeStagedIntoHand).
    const std::vector<int> rh = HandNumbers(r);
    REQUIRE(rh.size() == 7);
    CHECK(rh[0] == eh[0]);
    CHECK(rh == std::vector<int>{ eh[0], 60, 7, 52, 14, 20, 47 });
    for (std::size_t i = 1; i < rh.size(); ++i) { CHECK(r.players[0].hand[i].m_is_staged); }
    CHECK(r.players[1].life == e.players[1].life);
}
