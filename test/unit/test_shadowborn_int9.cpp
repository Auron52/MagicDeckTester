// Shadowborn Apostle onboarding, integrator INT-9 (2026-11-03; docs/design/analysis-Shadowborn Apostle.md, brief
// logs/shadowborn/int9-brief.md): a prune for EVERY search effect (USER: "we should have prunes for every search effect
// in the deck and for obvious decisions like whether to cast free spells off Thumming Stone") and the Demon-search
// TIMING (Jetmir before combat on an attack turn, Overlord after combat or on a sick turn).
//   * free casts off ripple: Dark Ritual / Thrumming Stone / Profane Tutor / Apostles TAKEN, a free Diabolic Intent
//     DECLINED (the MTG_SHADOWBORN_FREE_INTENT_TAKE arm takes it with an expendable victim); every trigger reveals;
//   * ONE victim for a "sacrifice a creature" additional cost (Harpy > Apostle > Demon), ONE body set for the
//     Apostle activation (the weakest six), ONE free tutor pick (the table's front);
//   * the activation's Demon by phase, the Jetmir kill (20 life: 13 attack-ready; Two-Headed Giant's 30: 14).
#include <doctest/doctest.h>

#include "ai/DecisionProviders.h"
#include "ai/HeuristicArm.h"
#include "ai/TurnSolver.h"
#include "cards/CardDatabase.h"
#include "core/EffectHandler.h"
#include "core/GameSetup.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"

#include <algorithm>
#include <string>
#include <vector>

namespace
{
const char* const kAp9     = "Shadowborn Apostle";
const char* const kStone9  = "Thrumming Stone";
const char* const kIntent9 = "Diabolic Intent";
const char* const kRit9    = "Dark Ritual";
const char* const kPT9     = "Profane Tutor";
const char* const kJet9    = "Jetmir, Nexus of Revels";
const char* const kOver9   = "Abhorrent Overlord";
const char* const kReaper9 = "Reaper from the Abyss";
const char* const kSw9     = "Swamp";

const CardDefinition& Def9(const std::string& n)
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
    const CardDefinition* d = CardDatabase::Instance().Lookup(n);
    REQUIRE_MESSAGE(d != nullptr, "card not in cards.json: ", n);
    return *d;
}
Card Card9(const std::string& name, int number)
{
    Card c     = Def9(name).card;
    c.m_number = number;
    return c;
}
Permanent Perm9(const std::string& name, int number, bool fresh = false, bool tapped = false)
{
    Permanent p;
    p.card              = Card9(name, number);
    p.controller_index  = 0;
    p.owner_index       = 0;
    p.entered_this_turn = fresh;
    p.tapped            = tapped;
    return p;
}
const ShadowbornProvider& Prov9() { static const ShadowbornProvider p; return p; }

struct Arm9
{
    heurarm::Arm saved;
    Arm9() : saved(heurarm::t_arm) {}
    Arm9& Set(heurarm::Slot slot, int v) { heurarm::t_arm[static_cast<int>(slot)] = static_cast<std::int8_t>(v); return *this; }
    ~Arm9() { heurarm::t_arm = saved; }
};

GameState Base9(int swamps = 4)
{
    (void)Def9(kSw9);
    GameState s;
    s.m_provider          = &Prov9();
    s.active_player_index = 0;
    s.turn_number         = 5;
    s.phase               = Phase::PreCombatMain;
    s.players[0].life     = gamesetup::StartingLife();
    s.players[1].life     = gamesetup::StartingLife();
    for (int k = 0; k < swamps; ++k) { s.battlefield.push_back(Perm9(kSw9, 1 + k)); }
    return s;
}
void Lib9(GameState& s, const std::vector<std::string>& names, int first = 500)
{
    // assign(), not push_back: a game's library starts UNKNOWN to its owner (push_back is a bottom placement, which
    // the ripple known stop reads as known information).
    std::vector<Card> cards;
    for (const Card& c : s.players[0].library) { cards.push_back(c); }
    for (const std::string& nm : names) { cards.push_back(Card9(nm, first++)); }
    s.players[0].library.assign(cards.begin(), cards.end());
}
int CountBf9(const GameState& s, const std::string& name)
{
    int n = 0;
    for (const Permanent& p : s.battlefield) { if (p.controller_index == 0 && p.card.m_name.str() == name) { ++n; } }
    return n;
}
void Harpies9(GameState& s, int n) { CreateTokens(s, 0, n, 1, 1, { "Harpy" }, "B", { "Flying" }); }

// One hard cast through the shared plan apply (the rollout's apply_one), with whatever pins the enumerator gave it.
void Rollout9(GameState& s, const Action& a)
{
    TurnSolver::Plan p;
    p.land_decided = true;
    p.actions.push_back(a);
    TurnSolver::ApplyPlan(s, p, /*is_pre_combat=*/true);
}
std::vector<Action> Casts9(const GameState& s, const std::string& name, bool pre_combat = true)
{
    std::vector<Action> out;
    for (const Action& a : TurnSolver::CollectActionsForTest(s, pre_combat))
    { if (a.kind == Action::Kind::CastFromHand && a.card_name.str() == name) { out.push_back(a); } }
    return out;
}
std::vector<Action> Activations9(const GameState& s, bool pre_combat = true)
{
    std::vector<Action> out;
    for (const Action& a : TurnSolver::CollectActionsForTest(s, pre_combat))
    {
        if (a.kind == Action::Kind::ActivatePermAbility && a.ability_mode == Action::AbilityMode::SacNamedTutorPut)
        { out.push_back(a); }
    }
    return out;
}
std::vector<std::string> Targets9(const std::vector<Action>& acts)
{
    std::vector<std::string> o;
    for (const Action& a : acts) { o.push_back(a.tutor_target.str()); }
    return o;
}
}   // namespace

// ---- B. the free casts --------------------------------------------------------------------------------------------
TEST_CASE("INT-9 free casts: the provider's answers -- Ritual / Stone / Profane / Apostle taken, Intent declined")
{
    const GameState s = Base9();
    CHECK(Prov9().RippleFixed(s));
    CHECK(Prov9().RippleFreeCastAnswer(s, 0, Def9(kRit9)) == 1);
    CHECK(Prov9().RippleFreeCastAnswer(s, 0, Def9(kStone9)) == 1);
    CHECK(Prov9().RippleFreeCastAnswer(s, 0, Def9(kPT9)) == 1);
    CHECK(Prov9().RippleFreeCastAnswer(s, 0, Def9(kAp9)) == 1);
    CHECK(Prov9().RippleFreeCastAnswer(s, 0, Def9(kIntent9)) == 0);   // DEFAULT: never a free Intent
    // The measured alternative arm: take iff an expendable victim (a Harpy) and a non-fallback tutor pick.
    Arm9 arm;
    arm.Set(heurarm::SHADOWBORN_FREE_INTENT_TAKE, 1);
    GameState h = Base9();
    Lib9(h, { kAp9, kRit9, kSw9, kSw9 });
    CHECK(Prov9().RippleFreeCastAnswer(h, 0, Def9(kIntent9)) == 0);   // no victim at all
    Harpies9(h, 1);
    CHECK(Prov9().RippleFreeCastAnswer(h, 0, Def9(kIntent9)) == 1);   // a Harpy to eat, an Apostle / Ritual to fetch
}

TEST_CASE("INT-9 free casts: a revealed Dark Ritual is always cast (its pins say decline)")
{
    GameState s = Base9();
    s.battlefield.push_back(Perm9(kStone9, 40));
    s.players[0].hand.push_back(Card9(kRit9, 60));
    Lib9(s, { kRit9, kSw9, kSw9, kSw9, kSw9, kSw9, kSw9, kSw9 });
    const std::vector<Action> casts = Casts9(s, kRit9);
    REQUIRE(casts.size() == 1);                       // ONE ripple variant (every answer is fixed)
    Action a = casts.front();
    a.ripple_decline = 0xFF;                          // even a hand-built "decline every match" is overridden
    Rollout9(s, a);
    CHECK(s.floating_mana.Total() >= 6);              // the hard Ritual's BBB + the free one's BBB
    CHECK(s.players[0].graveyard.size() == 2);
}

TEST_CASE("INT-9 free casts: a revealed Thrumming Stone is cast and the legend rule keeps the battlefield copy")
{
    GameState s = Base9(6);
    s.battlefield.push_back(Perm9(kStone9, 40));
    s.players[0].hand.push_back(Card9(kStone9, 60));
    Lib9(s, { kStone9, kSw9, kSw9, kSw9, kSw9, kSw9, kSw9, kSw9, kSw9 });
    const std::vector<Action> casts = Casts9(s, kStone9);
    REQUIRE(casts.size() == 1);
    Rollout9(s, casts.front());
    CHECK(CountBf9(s, kStone9) == 1);
    bool kept_old = false;
    for (const Permanent& p : s.battlefield) { if (p.card.m_name.str() == kStone9 && p.card.m_number == 40) { kept_old = true; } }
    CHECK(kept_old);                                  // the copy already on the battlefield stays
    int stones_gy = 0;
    for (const Card& c : s.players[0].graveyard) { if (c.m_name.str() == kStone9) { ++stones_gy; } }
    CHECK(stones_gy == 2);                            // the hard copy AND the free copy were cast
}

TEST_CASE("INT-9 free casts: a revealed Diabolic Intent is declined (bottomed), the hard Intent resolves")
{
    GameState s = Base9();
    s.battlefield.push_back(Perm9(kStone9, 40));
    s.battlefield.push_back(Perm9(kAp9, 41));
    Harpies9(s, 1);
    s.players[0].hand.push_back(Card9(kIntent9, 60));
    Lib9(s, { kIntent9, kSw9, kSw9, kSw9, kAp9, kRit9, kSw9, kSw9 });
    const std::vector<Action> casts = Casts9(s, kIntent9);
    REQUIRE(casts.size() == 1);                       // one victim, one ripple variant
    Rollout9(s, casts.front());
    int intents_gy = 0;
    for (const Card& c : s.players[0].graveyard) { if (c.m_name.str() == kIntent9) { ++intents_gy; } }
    CHECK(intents_gy == 1);                           // only the hard one was cast
    bool kept41 = false;
    int tokens = 0;
    for (const Permanent& p : s.battlefield) { if (p.card.m_number == 41) { kept41 = true; } if (p.is_token) { ++tokens; } }
    CHECK(kept41);                                    // the Harpy paid for the hard Intent, the Apostle stayed
    CHECK(tokens == 0);
}

TEST_CASE("INT-9 free tutor pick: the front of the table at its resolution, without consuming the plan's pin")
{
    GameState s = Base9();
    RippleScript sc;
    sc.pinned = true;
    sc.free_tutor = 0x0003;   // a searched rank 2 -- ignored under the fixed answers
    s.ripple_script = sc;
    CHECK(RippleNextFreeTutorTarget(s) == RippleTutorRankPrefix() + "0");
}

// ---- A.4 / A.5: one victim ----------------------------------------------------------------------------------------
TEST_CASE("INT-9 victims: Intent sacrifices a Harpy before an Apostle; an Apostle before a Demon; one variant")
{
    GameState s = Base9();
    s.battlefield.push_back(Perm9(kAp9, 41));
    s.battlefield.push_back(Perm9(kOver9, 42));
    Harpies9(s, 2);
    s.players[0].hand.push_back(Card9(kIntent9, 60));
    s.players[0].hand.push_back(Card9(kAp9, 61));
    Lib9(s, { kRit9, kAp9, kSw9 });
    const int bi = Prov9().SacCostVictimLive(s, 0);
    REQUIRE(bi >= 0);
    CHECK(s.battlefield[static_cast<std::size_t>(bi)].is_token);
    const std::vector<Action> casts = Casts9(s, kIntent9);
    REQUIRE(casts.size() == 1);
    CHECK(casts.front().soulfire_own_targets == s.battlefield[static_cast<std::size_t>(bi)].card.m_number);
    GameState t = s;
    Rollout9(t, casts.front());
    bool kept41 = false;
    int tokens = 0;
    for (const Permanent& p : t.battlefield) { if (p.card.m_number == 41) { kept41 = true; } if (p.is_token) { ++tokens; } }
    CHECK(kept41);
    CHECK(tokens == 1);                               // one of the two Harpies paid
    CHECK(CountBf9(t, kOver9) == 1);
    // No Harpy: the Apostle, never the Overlord.
    GameState n = Base9();
    n.battlefield.push_back(Perm9(kOver9, 42));
    n.battlefield.push_back(Perm9(kAp9, 41));
    const int ni = Prov9().SacCostVictimLive(n, 0);
    REQUIRE(ni >= 0);
    CHECK(n.battlefield[static_cast<std::size_t>(ni)].card.m_name.str() == kAp9);
    // Only a Demon on the battlefield, an Apostle in hand: the variant is the hand Apostle (cast it, then eat it).
    GameState d = Base9();
    d.battlefield.push_back(Perm9(kOver9, 42));
    d.players[0].hand.push_back(Card9(kIntent9, 60));
    d.players[0].hand.push_back(Card9(kAp9, 61));
    int bf = -1;
    std::string hn;
    Prov9().SacCostVictimVariant(d, 0, bf, hn);
    CHECK(bf == 0);
    CHECK(hn == kAp9);
}

TEST_CASE("INT-9 victims: the Demon tail -- Overlord, then Sire, then Reaper, Jetmir kept longest")
{
    GameState s = Base9();
    s.battlefield.push_back(Perm9(kJet9, 44));
    s.battlefield.push_back(Perm9(kReaper9, 43));
    s.battlefield.push_back(Perm9("Sire of Insanity", 42));
    s.battlefield.push_back(Perm9(kOver9, 41));
    for (const char* want : { kOver9, "Sire of Insanity", kReaper9, kJet9 })
    {
        const int bi = Prov9().SacCostVictimLive(s, 0);
        REQUIRE(bi >= 0);
        CHECK(s.battlefield[static_cast<std::size_t>(bi)].card.m_name.str() == want);
        s.battlefield.erase(s.battlefield.begin() + bi);
    }
}

TEST_CASE("INT-9 victims: the weakest Apostle -- a summoning-sick copy before an attack-ready one")
{
    GameState s = Base9();
    s.battlefield.push_back(Perm9(kAp9, 41));
    s.battlefield.push_back(Perm9(kAp9, 42, /*fresh=*/true));
    const int bi = Prov9().SacCostVictimLive(s, 0);
    REQUIRE(bi >= 0);
    CHECK(s.battlefield[static_cast<std::size_t>(bi)].card.m_number == 42);
}

TEST_CASE("INT-9 Lake of the Dead: a basic Swamp is sacrificed before Blood Crypt (one victim)")
{
    GameState s = Base9(0);
    s.battlefield.push_back(Perm9("Blood Crypt", 70));
    s.battlefield.push_back(Perm9(kSw9, 71));
    const int v = ChooseSubtypeLandSacrifice(s, 0, "Swamp", 0, "Lake of the Dead");
    REQUIRE(v >= 0);
    CHECK(s.battlefield[static_cast<std::size_t>(v)].card.m_name.str() == kSw9);
}

// The INT-9 activation/timing tests below document the arm WITHOUT the USER's main-1 rule / cast order / ritual
// prune (MTG_SB_MAIN1_RULE / MTG_SB_CAST_ORDER / MTG_SB_RITUAL_PAYOFF, default ON since 2026-11-05). Under the rule
// a sick turn's Demon search is offered after combat too, and an attack turn with both sixes offers the Overlord ahead of Jetmir
// in main 1; those shapes are tested in test_shadowborn_mainphase.cpp.
struct PreMainRule9
{
    Arm9 arm;
    PreMainRule9()
    {
        arm.Set(heurarm::SB_MAIN1_RULE, 0).Set(heurarm::SB_CAST_ORDER, 0).Set(heurarm::SB_RITUAL_PAYOFF, 0);
    }
};

// ---- A.2 + E: the Apostle activation ------------------------------------------------------------------------------
TEST_CASE("INT-9 activation: ONE body code (the weakest six) per Demon")
{
    PreMainRule9 pre_rule;
    GameState s = Base9(1);
    for (int k = 0; k < 4; ++k) { s.battlefield.push_back(Perm9(kAp9, 10 + k, /*fresh=*/true)); }
    for (int k = 0; k < 12; ++k) { s.battlefield.push_back(Perm9(kAp9, 20 + k)); }
    Lib9(s, { kOver9, kJet9, kReaper9, kSw9, kSw9 });
    // 4 sick + 12 ready: an ATTACK turn. The six weakest are the 4 sick + 2 ready, leaving 10 attackers + Jetmir = 11
    // creatures (double strike: 10 x 4 x 2 = 80) vs 12 without -> Jetmir, main 1, ONE body code.
    const std::vector<Action> m1 = Activations9(s, true);
    REQUIRE(m1.size() == 1);
    CHECK(m1.front().sac_victim_id == kSacNamedWeakestCode);
    CHECK(m1.front().tutor_target.str() == kJet9);
    GameState t = s;
    Rollout9(t, m1.front());
    // The four sick copies and the two lowest-numbered ready ones went; ten ready attackers stay.
    int ready = 0;
    bool low_ready_gone = true;
    for (const Permanent& p : t.battlefield)
    {
        if (p.card.m_name.str() != kAp9) { continue; }
        if (!p.entered_this_turn) { ++ready; }
        if (p.card.m_number == 20 || p.card.m_number == 21) { low_ready_gone = false; }
    }
    CHECK(CountBf9(t, kAp9) == 10);
    CHECK(ready == 10);
    CHECK(low_ready_gone);
    CHECK(CountBf9(t, kJet9) == 1);
}

TEST_CASE("INT-9 timing (USER CONFIRMED amendment): Jetmir main 1 only when it raises this turn's attack")
{
    auto board = [](int ready, int harpies)
    {
        GameState s = Base9(1);
        for (int k = 0; k < ready; ++k) { s.battlefield.push_back(Perm9(kAp9, 10 + k)); }
        if (harpies > 0) { Harpies9(s, harpies); }
        Lib9(s, { kOver9, kJet9, kSw9, kSw9 });
        return s;
    };
    // 7 attack-ready: 1 attacker + Jetmir = 2 creatures, no tier: 1 < 7 -> the Overlord, after combat only.
    GameState s7 = board(7, 0);
    CHECK(Activations9(s7, true).empty());
    CHECK(Targets9(Activations9(s7, false)) == std::vector<std::string>{ kOver9 });
    // 10: 4 x 2 = 8 < 10 -> Overlord. 11: 5 + Jetmir = 6 creatures, +2: 5 x 3 = 15 > 11 -> Jetmir, main 1 only.
    CHECK(Activations9(board(10, 0), true).empty());
    GameState s11 = board(11, 0);
    CHECK(Targets9(Activations9(s11, true)) == std::vector<std::string>{ kJet9 });
    CHECK(Activations9(s11, false).empty());
    // 10 + 2 (sick, freshly made) Harpies: 4 attackers + 2 Harpies + Jetmir = 7 creatures, +2: 4 x 3 = 12 > 10.
    CHECK(Targets9(Activations9(board(10, 2), true)) == std::vector<std::string>{ kJet9 });
}

TEST_CASE("INT-9 timing: attack turn -> Jetmir main 1 only; no Jetmir -> Overlord main 2 only; sick turn -> both")
{
    PreMainRule9 pre_rule;
    GameState s = Base9(1);
    for (int k = 0; k < 13; ++k) { s.battlefield.push_back(Perm9(kAp9, 10 + k)); }
    Lib9(s, { kOver9, kJet9, kSw9, kSw9 });
    CHECK(Targets9(Activations9(s, true)) == std::vector<std::string>{ kJet9 });
    CHECK(Activations9(s, false).empty());
    // Jetmir already gone: the Overlord is the backup, after combat only.
    GameState o = Base9(1);
    for (int k = 0; k < 13; ++k) { o.battlefield.push_back(Perm9(kAp9, 10 + k)); }
    Lib9(o, { kOver9, kSw9, kSw9 });
    CHECK(Activations9(o, true).empty());
    CHECK(Targets9(Activations9(o, false)) == std::vector<std::string>{ kOver9 });
    // A SICK turn (every Apostle entered this turn): the INT-4 order, Overlord first, in main 1.
    GameState k = Base9(1);
    for (int i = 0; i < 6; ++i) { k.battlefield.push_back(Perm9(kAp9, 10 + i, /*fresh=*/true)); }
    Lib9(k, { kOver9, kJet9, kSw9, kSw9 });
    CHECK(Targets9(Activations9(k, true)) == std::vector<std::string>{ kOver9, kJet9 });
    // ...in main 1 ONLY (USER CONFIRMED: "On a summoning sick turn you can do demon search main 1 if you want.").
    CHECK(Activations9(k, false).empty());
}

TEST_CASE("INT-9 timing: bodies still to come from a chain (pre-chain board short of six) -> Jetmir offered in main 1")
{
    // Held-out seed 7001 gi126: three attack-ready Apostles, then a Stone chain in main 1 makes the six. The pre-chain
    // board cannot price Jetmir, so it stays offered before combat; after combat the devotion Demon is the pick.
    GameState s = Base9(1);
    for (int k = 0; k < 3; ++k) { s.battlefield.push_back(Perm9(kAp9, 10 + k)); }
    const CardParams& pp = Def9(kAp9).params;
    std::vector<std::string> m1{ kOver9, kJet9 };
    Prov9().SacNamedTimingFilter(s, 0, pp, /*pre_combat=*/true, m1);
    CHECK(m1 == std::vector<std::string>{ kJet9 });
    std::vector<std::string> m2{ kOver9, kJet9 };
    Prov9().SacNamedTimingFilter(s, 0, pp, /*pre_combat=*/false, m2);
    CHECK(m2 == std::vector<std::string>{ kOver9 });
}

TEST_CASE("INT-9 timing: six attack-ready + an Apostle in hand (a chain can still add bodies) -> Jetmir offered in main 1")
{
    // Held-out seed 7001 gi97: the six are on the battlefield, but the plan's chain makes sick copies the six.
    GameState s = Base9(1);
    for (int k = 0; k < 6; ++k) { s.battlefield.push_back(Perm9(kAp9, 10 + k)); }
    const CardParams& pp = Def9(kAp9).params;
    std::vector<std::string> bare{ kOver9, kJet9 };
    Prov9().SacNamedTimingFilter(s, 0, pp, /*pre_combat=*/true, bare);
    CHECK(bare.empty());                               // nothing in hand: six ready, none left to attack -> main 2
    s.players[0].hand.push_back(Card9(kAp9, 60));
    std::vector<std::string> m1{ kOver9, kJet9 };
    Prov9().SacNamedTimingFilter(s, 0, pp, /*pre_combat=*/true, m1);
    CHECK(m1 == std::vector<std::string>{ kJet9 });
    std::vector<std::string> m2{ kOver9, kJet9 };
    Prov9().SacNamedTimingFilter(s, 0, pp, /*pre_combat=*/false, m2);
    CHECK(m2 == std::vector<std::string>{ kOver9 });
}

// The kill arithmetic, read off card data: the activation, then everyone able to attack attacks.
static bool JetmirKill9(int ready, int opp_life)
{
    PreMainRule9 pre_rule;
    GameState s = Base9(1);
    s.players[1].life = opp_life;
    for (int k = 0; k < ready; ++k) { s.battlefield.push_back(Perm9(kAp9, 10 + k)); }
    Lib9(s, { kOver9, kJet9, kSw9, kSw9 });
    const std::vector<Action> m1 = Activations9(s, true);
    REQUIRE(m1.size() == 1);
    REQUIRE(m1.front().tutor_target.str() == kJet9);
    TurnSolver::Plan p;
    p.land_decided = true;
    p.actions.push_back(m1.front());
    TurnSolver::ApplyPlan(s, p, /*is_pre_combat=*/true);
    RolloutSimulateCombat(s);
    return s.players[1].life <= 0;
}
TEST_CASE("INT-9 Jetmir kill: 13 attack-ready vs 20 life is lethal, 12 is not")
{
    CHECK(JetmirKill9(13, 20));   // 7 attackers + Jetmir = 8 creatures: +2 trample, 7 x 3 = 21
    CHECK(!JetmirKill9(12, 20));  // 6 attackers: 6 x 3 = 18
}
TEST_CASE("INT-9 Jetmir kill, Two-Headed Giant (30 team life): 14 attack-ready is lethal, 13 is not")
{
    CHECK(JetmirKill9(14, 30));   // 8 attackers + Jetmir = 9: double strike, 8 x 4 x 2 = 64
    CHECK(!JetmirKill9(13, 30));  // 21 < 30
}

TEST_CASE("INT-9 Jetmir kill: the enumeration collapses to the one verified killing plan")
{
    GameState s = Base9(1);
    for (int k = 0; k < 13; ++k) { s.battlefield.push_back(Perm9(kAp9, 10 + k)); }
    s.players[0].hand.push_back(Card9(kAp9, 60));
    s.players[0].hand.push_back(Card9(kRit9, 61));
    Lib9(s, { kOver9, kJet9, kSw9, kSw9, kSw9, kSw9 });
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, /*is_pre_combat=*/true);
    MESSAGE("plans on the kill turn: ", plans.size());
    // The land family's "cast nothing" baseline (EnumeratePlansWithLand's idle plan, pushed before EnumeratePlans
    // runs) rides along; EnumeratePlans itself returns the ONE verified killing plan (no Apostle / Ritual casts).
    REQUIRE(plans.size() == 2);
    int idle = 0, kill = 0;
    for (const TurnSolver::Plan& p : plans)
    {
        if (p.actions.empty()) { ++idle; continue; }
        REQUIRE(p.actions.size() == 1);
        const Action& a = p.actions.front();
        if (a.ability_mode == Action::AbilityMode::SacNamedTutorPut && a.tutor_target.str() == kJet9) { ++kill; }
    }
    CHECK(idle == 1);
    CHECK(kill == 1);
}

// ---- the enumerator: one ripple variant per cast ------------------------------------------------------------------
TEST_CASE("INT-9 enumerator: Stone out, a hard Intent with three Intents in the library -> one variant")
{
    GameState s = Base9();
    s.battlefield.push_back(Perm9(kStone9, 40));
    s.battlefield.push_back(Perm9(kAp9, 41));
    s.players[0].hand.push_back(Card9(kIntent9, 60));
    Lib9(s, { kIntent9, kIntent9, kIntent9, kAp9, kRit9, kSw9, kSw9, kSw9, kSw9, kSw9 });
    CHECK(Casts9(s, kIntent9).size() == 1);
    // The first Apostle copy: ALL only (the granter is on the battlefield).
    GameState a = Base9();
    a.battlefield.push_back(Perm9(kStone9, 40));
    a.players[0].hand.push_back(Card9(kAp9, 60));
    Lib9(a, { kAp9, kAp9, kSw9, kAp9, kSw9, kSw9, kSw9, kSw9 });
    const std::vector<Action> ap = Casts9(a, kAp9);
    REQUIRE(ap.size() == 1);
    CHECK(ap.front().ripple_reveals == kRippleRevealAll);
}

TEST_CASE("INT-9 known stop: once the Apostles are gone from a fully seen library, stopping the reveals is offered")
{
    // Two Apostles on top: the hard cast's trigger takes both; the first free trigger reveals the rest of the
    // original library (a Ritual among Swamps) -- the library is now fully known and holds no Apostle, so the last
    // trigger's reveal is a choice on known information (held-out gi97: INT-8 stopped with a Swamp on top).
    GameState s = Base9();
    s.battlefield.push_back(Perm9(kStone9, 40));
    s.players[0].hand.push_back(Card9(kAp9, 60));
    Lib9(s, { kAp9, kAp9, kSw9, kSw9, kRit9, kSw9, kSw9, kSw9 });
    const std::vector<Action> casts = Casts9(s, kAp9);
    REQUIRE(casts.size() == 2);   // ALL + the known stop (k = 0)
    CHECK(casts[0].ripple_reveals == kRippleRevealAll);
    CHECK(casts[1].ripple_reveals == kRippleKnownStop);
    auto ritual_pos = [](const GameState& g)
    {
        int i = 0;
        for (const Card& c : g.players[0].library) { if (c.m_name.str() == kRit9) { return i; } ++i; }
        return -1;
    };
    GameState all = s;
    Rollout9(all, casts[0]);
    GameState stop = s;
    Rollout9(stop, casts[1]);
    CHECK(CountBf9(all, kAp9) == CountBf9(stop, kAp9));   // the same Apostles either way
    CHECK(ritual_pos(stop) == 2);                         // stopped: Swamp, Swamp, Ritual, ...
    CHECK(ritual_pos(all) == 4);                          // the last reveal moved the Ritual down
    // Swamps only after the Apostles: every stop leaves the same order -> folded, ALL alone.
    GameState f = Base9();
    f.battlefield.push_back(Perm9(kStone9, 40));
    f.players[0].hand.push_back(Card9(kAp9, 60));
    Lib9(f, { kAp9, kAp9, kSw9, kAp9, kSw9, kSw9, kSw9, kSw9 });
    CHECK(Casts9(f, kAp9).size() == 1);
}

// ---- F: the INT-8 5,488-variant state, re-measured with the waves OPEN (an unlimited root = the wave-deleted shape) --
namespace
{
// The INT-8 searched shape (every INT-9 prune off): the engine's ripple menus and victim fan.
struct SearchedProv9 : ShadowbornProvider
{
    bool RippleFixed(const GameState&) const override { return false; }
    bool SacCostVictimFixed(const GameState&) const override { return false; }
    int  RippleFreeCastAnswer(const GameState& s, int c, const CardDefinition& h) const override
    { return DeckProvider::RippleFreeCastAnswer(s, c, h); }
};
// ...with only the USER's free-Intent ruling applied (a free Intent declined; everything else searched).
struct IntentOnlyProv9 : SearchedProv9
{
    int RippleFreeCastAnswer(const GameState& s, int c, const CardDefinition& h) const override
    { return h.params.sac_additional_creature ? 0 : SearchedProv9::RippleFreeCastAnswer(s, c, h); }
};
std::size_t IntentVariants9(const DecisionProvider& prov)
{
    GameState s = Base9(6);
    s.m_provider = &prov;
    s.battlefield.push_back(Perm9(kStone9, 40));
    s.battlefield.push_back(Perm9(kAp9, 41));
    s.battlefield.push_back(Perm9(kAp9, 42, /*fresh=*/true));
    s.players[0].hand.push_back(Card9(kIntent9, 60));
    Lib9(s, { kIntent9, kIntent9, kIntent9, kAp9, kRit9, kSw9, kSw9, kSw9, kSw9, kSw9, kSw9, kSw9, kAp9, kRit9 });
    std::size_t n = 0;
    for (const Action& a : TurnSolver::CollectActionsUnderRootForTest(s, true, nullptr))
    { if (a.kind == Action::Kind::CastFromHand && a.card_name.str() == kIntent9) { ++n; } }
    return n;
}
}   // namespace
TEST_CASE("INT-9 F: the Intent probe state -- searched (INT-8) vs the free-Intent ruling vs every INT-9 prune")
{
    static const SearchedProv9 searched;
    static const IntentOnlyProv9 intent_only;
    const std::size_t a = IntentVariants9(searched), b = IntentVariants9(intent_only), c = IntentVariants9(Prov9());
    MESSAGE("Intent variants at an unlimited root: searched ", a, ", free Intent declined ", b, ", INT-9 ", c);
    CHECK(a > b);
    CHECK(b > c);
    CHECK(c == 1);
}

// ---- P1: no "play no land" plan while a free land is in hand (USER CONFIRMED: "We don't have a good reason to hold
// lands.") ------------------------------------------------------------------------------------------------------------
TEST_CASE("INT-9 P1: a Swamp or Blood Crypt in hand drops the no-land plans; a Lake alone does not")
{
    auto no_land_plans = [](const GameState& s)
    {
        int n = 0;
        for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(s, /*is_pre_combat=*/true))
        { if (p.land_to_play.empty()) { ++n; } }
        return n;
    };
    GameState s = Base9(1);
    s.players[0].hand.push_back(Card9(kSw9, 60));
    s.players[0].hand.push_back(Card9(kAp9, 61));
    Lib9(s, { kSw9, kSw9, kSw9 });
    CHECK(Prov9().NoLandPlanDominated(s));
    CHECK(no_land_plans(s) == 0);
    GameState c = Base9(1);
    c.players[0].hand.push_back(Card9("Blood Crypt", 60));
    CHECK(Prov9().NoLandPlanDominated(c));
    GameState l = Base9(1);
    l.players[0].hand.push_back(Card9("Lake of the Dead", 60));
    l.players[0].hand.push_back(Card9(kAp9, 61));
    Lib9(l, { kSw9, kSw9, kSw9 });
    CHECK(!Prov9().NoLandPlanDominated(l));
    CHECK(no_land_plans(l) > 0);
    GameState d = s;
    d.players[0].lands_played_this_turn = 1;
    CHECK(!Prov9().NoLandPlanDominated(d));
}

// P1, the Lake case (USER CONFIRMED: "We don't have a good reason to hold lands." + "(beyond the Lake of the Dead case
// you mentioned. That is held for the burst turn)"): only the NO-LAND plans are dropped. With a Swamp AND a Lake of the
// Dead in hand both land plays stay on the menu -- holding the Lake for the burst turn is still reachable by playing
// the Swamp -- and no plan plays no land.
TEST_CASE("INT-9 P1: Swamp + Lake in hand -> a Swamp plan AND a Lake plan, no no-land plan")
{
    GameState s = Base9(2);
    s.players[0].hand.push_back(Card9(kSw9, 60));
    s.players[0].hand.push_back(Card9("Lake of the Dead", 61));
    s.players[0].hand.push_back(Card9(kAp9, 62));
    Lib9(s, { kSw9, kSw9, kSw9 });
    CHECK(Prov9().NoLandPlanDominated(s));
    int swamp = 0, lake = 0, none = 0;
    for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(s, /*is_pre_combat=*/true))
    {
        if (p.land_to_play.empty()) { ++none; }
        else if (p.land_to_play == kSw9) { ++swamp; }
        else if (p.land_to_play == "Lake of the Dead") { ++lake; }
    }
    CHECK(swamp > 0);
    CHECK(lake > 0);
    CHECK(none == 0);
}

// ---- a victim is claimed once (USER 2026-11-10) ------------------------------------------------------------------
// "We cannot choose something already marked as a sacrifice." One Apostle in hand, nothing on the battlefield:
// Culling the Weak and Diabolic Intent cannot BOTH be paid, so no plan casts both (it used to be a menu entry whose
// realisation dropped one of them). Each one alone, with the Apostle as its victim, stays on the menu.
TEST_CASE("Sacrifice claims: two sacrifice costs never share the one creature the plan has")
{
    GameState s = Base9(3);
    s.players[0].hand.push_back(Card9(kAp9, 60));
    s.players[0].hand.push_back(Card9("Culling the Weak", 61));
    s.players[0].hand.push_back(Card9(kIntent9, 62));
    s.players[0].hand.push_back(Card9(kRit9, 63));
    Lib9(s, { kSw9, kSw9, kSw9, kSw9, kSw9, kSw9 });
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, /*is_pre_combat=*/true);
    int both = 0, culling = 0, intent = 0;
    for (const TurnSolver::Plan& p : plans)
    {
        bool c = false, i = false;
        for (const Action& a : p.actions)
        {
            if (a.kind != Action::Kind::CastFromHand) { continue; }
            if (a.card_name.str() == "Culling the Weak") { c = true; }
            if (a.card_name.str() == kIntent9) { i = true; }
        }
        if (c && i) { ++both; }
        if (c && !i) { ++culling; }
        if (i && !c) { ++intent; }
    }
    MESSAGE("plans=", plans.size(), " culling-only=", culling, " intent-only=", intent, " both=", both);
    CHECK(both == 0);
    CHECK(culling > 0);
    CHECK(intent > 0);
    // A second creature on the battlefield funds both claims.
    GameState t = s;
    t.battlefield.push_back(Perm9(kAp9, 30));
    int both2 = 0;
    for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(t, /*is_pre_combat=*/true))
    {
        bool c = false, i = false;
        for (const Action& a : p.actions)
        {
            if (a.kind != Action::Kind::CastFromHand) { continue; }
            if (a.card_name.str() == "Culling the Weak") { c = true; }
            if (a.card_name.str() == kIntent9) { i = true; }
        }
        if (c && i) { ++both2; }
    }
    CHECK(both2 > 0);
}
