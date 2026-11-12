// Unit tests for the Morph deck's pump family (INT-1, 2026-11-11; ledger docs/design/analysis-Morph.md):
// Wild Defiance's target watcher, Berserk / Legion Leadership power doubling, Temur Battle Rage's double
// strike, Might of Old Krosa's main-phase "instead", kicked Vines fanned per target, Berserk's delayed
// destroy-if-attacked, Legion Stronghold entering tapped, the Spellskite redirect, and MorphProvider's
// routing + cast-order rank. Every payload runs through the ONE shared resolver (ResolveSoloTargetTrick ->
// ApplyTrickPayload) that the executor and the rollout both call, so a resolver test covers both worlds.
#include <doctest/doctest.h>

#include "ai/DecisionProviders.h"
#include "ai/HeuristicArm.h"
#include "ai/TurnSolver.h"
#include "cards/CardDatabase.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"
#include "deck/DeckLoader.h"

#include <algorithm>
#include <set>
#include <string>
#include <vector>

namespace
{

void EnsureCardsMorph()
{
    static const bool loaded = []
    {
        const auto path = ResolveHeuristicDefaultsPath("src/cards/data/cards.json");
        CardDatabase::Instance().LoadFromJson(path);
        return true;
    }();
    REQUIRE(loaded);
}

const CardDefinition& DefM(const std::string& name)
{
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE_MESSAGE(d != nullptr, "card not in cards.json: ", name);
    return *d;
}

Card CardM(const std::string& name, int number)
{
    Card c     = DefM(name).card;
    c.m_number = number;
    return c;
}

// A ready (not summoning-sick) permanent of ours.
int PutM(GameState& s, const std::string& name, int number)
{
    Permanent p;
    p.card             = CardM(name, number);
    p.controller_index = 0;
    p.owner_index      = 0;
    s.battlefield.push_back(p);
    return static_cast<int>(s.battlefield.size()) - 1;
}

// A vanilla 2/2 body (deck-agnostic, so the test does not depend on any creature entry).
int PutBear(GameState& s, int number)
{
    Card c;
    c.m_name = "Test Bear";
    c.RehashName();
    c.AddType(CardType::Creature);
    c.m_power     = 2;
    c.m_toughness = 2;
    c.m_number    = number;
    Permanent p;
    p.card             = c;
    p.controller_index = 0;
    p.owner_index      = 0;
    s.battlefield.push_back(p);
    return static_cast<int>(s.battlefield.size()) - 1;
}

GameState BaseM()
{
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 3;
    s.phase               = Phase::PreCombatMain;
    s.players[0].life     = 20;
    s.players[1].life     = 20;
    return s;
}

int Power(const GameState& s, int i) { return CombatPowerOf(s.battlefield[static_cast<std::size_t>(i)], s); }

void Cast(GameState& s, const std::string& spell, int target_number, int redirect = 0)
{
    REQUIRE(ResolveSoloTargetTrick(s, 0, DefM(spell), target_number, 0, 0, redirect));
}

}  // namespace

TEST_CASE("morph pumps: Wild Defiance triggers once per copy, before the spell's own pump")
{
    EnsureCardsMorph();
    GameState s = BaseM();
    const int b = PutBear(s, 10);
    PutM(s, "Wild Defiance", 20);
    PutM(s, "Wild Defiance", 21);
    Cast(s, "Might of Old Krosa", 10);
    // 2 + 3 + 3 (two Defiances) + 4 (main phase) = 12.
    CHECK(Power(s, b) == 12);
    CHECK(s.battlefield[static_cast<std::size_t>(b)].temp_tough_bonus == 10);
}

TEST_CASE("morph pumps: Might of Old Krosa is +4/+4 in a main phase, +2/+2 otherwise")
{
    EnsureCardsMorph();
    GameState s = BaseM();
    const int b = PutBear(s, 10);
    Cast(s, "Might of Old Krosa", 10);
    CHECK(Power(s, b) == 6);
    GameState c = BaseM();
    c.phase = Phase::Combat;
    const int cb = PutBear(c, 10);
    Cast(c, "Might of Old Krosa", 10);
    CHECK(Power(c, cb) == 4);
}

TEST_CASE("morph pumps: doublers double the Defiance trigger and earlier pumps, not later ones")
{
    EnsureCardsMorph();
    {
        // Defiance + Berserk on a 2/2: the trigger resolves first, so X = 5 -> 10.
        GameState s = BaseM();
        const int b = PutBear(s, 10);
        PutM(s, "Wild Defiance", 20);
        Cast(s, "Berserk", 10);
        CHECK(Power(s, b) == 10);
    }
    {
        // Pump then double: (2 + 4) x 2 = 12.
        GameState s = BaseM();
        const int b = PutBear(s, 10);
        Cast(s, "Might of Old Krosa", 10);
        Cast(s, "Legion Leadership", 10);
        CHECK(Power(s, b) == 12);
    }
    {
        // Double then pump: 2 x 2 + 4 = 8 -- why the cast-order rank puts additive pumps first.
        GameState s = BaseM();
        const int b = PutBear(s, 10);
        Cast(s, "Berserk", 10);
        Cast(s, "Might of Old Krosa", 10);
        CHECK(Power(s, b) == 8);
    }
    {
        // Two doublers commute: 2 -> 4 -> 8 either way.
        GameState s = BaseM();
        const int b = PutBear(s, 10);
        Cast(s, "Berserk", 10);
        Cast(s, "Legion Leadership", 10);
        CHECK(Power(s, b) == 8);
    }
}

TEST_CASE("morph pumps: Temur Battle Rage grants double strike until end of turn")
{
    EnsureCardsMorph();
    GameState s = BaseM();
    const int b = PutBear(s, 10);
    CHECK_FALSE(CreatureHasDoubleStrike(s.battlefield[static_cast<std::size_t>(b)], s));
    Cast(s, "Temur Battle Rage", 10);
    CHECK(s.battlefield[static_cast<std::size_t>(b)].temp_double_strike);
    CHECK(CreatureHasDoubleStrike(s.battlefield[static_cast<std::size_t>(b)], s));
}

TEST_CASE("morph pumps: Berserk destroys its target at the end step ONLY if it attacked")
{
    EnsureCardsMorph();
    {
        GameState s = BaseM();
        s.deck_tracks_attacks = true;
        const int b = PutBear(s, 10);
        Cast(s, "Berserk", 10);
        CHECK(static_cast<bool>(s.battlefield[static_cast<std::size_t>(b)].destroy_at_end_if_attacked));
        NoteAttacksDeclared(s, 0, std::vector<int>{ b });
        CHECK(s.battlefield[static_cast<std::size_t>(b)].AttacksThisTurn() == 1);
        DestroyAttackedAtEndStep(s);
        CHECK(s.battlefield.empty());
        REQUIRE(s.players[0].graveyard.size() == 1);
        CHECK(s.players[0].graveyard[0].m_name.str() == "Test Bear");
    }
    {
        // Did not attack: survives, and the delayed trigger is spent.
        GameState s = BaseM();
        s.deck_tracks_attacks = true;
        const int b = PutBear(s, 10);
        Cast(s, "Berserk", 10);
        DestroyAttackedAtEndStep(s);
        REQUIRE(s.battlefield.size() == 1);
        CHECK_FALSE(static_cast<bool>(s.battlefield[static_cast<std::size_t>(b)].destroy_at_end_if_attacked));
    }
    {
        // Indestructible: attacked, but "destroy" does nothing.
        GameState s = BaseM();
        s.deck_tracks_attacks = true;
        const int b = PutBear(s, 10);
        s.battlefield[static_cast<std::size_t>(b)].temp_indestructible = true;
        Cast(s, "Berserk", 10);
        NoteAttacksDeclared(s, 0, std::vector<int>{ b });
        DestroyAttackedAtEndStep(s);
        CHECK(s.battlefield.size() == 1);
    }
    {
        // Without the deck stamp the attack is not counted (every other deck byte-identical).
        GameState s = BaseM();
        const int b = PutBear(s, 10);
        NoteAttacksDeclared(s, 0, std::vector<int>{ b });
        CHECK(s.battlefield[static_cast<std::size_t>(b)].AttacksThisTurn() == 0);
    }
}

TEST_CASE("morph pumps: Vines of Vastwood -- unkicked only targets, kicked is +4/+4")
{
    EnsureCardsMorph();
    const CardDefinition& k = DefM("Vines of Vastwood (Kicked)");
    CHECK(k.params.solo_target_trick);
    CHECK(k.params.power_bonus == 4);
    CHECK(k.params.tough_bonus == 4);
    CHECK_FALSE(k.params.kicker_cost.has_value());
    CHECK(k.card.m_mana_cost.green == 2);
    {
        GameState s = BaseM();
        const int b = PutBear(s, 10);
        Cast(s, "Vines of Vastwood", 10);
        CHECK(Power(s, b) == 2);                     // no Defiance: unkicked does nothing here
        PutM(s, "Wild Defiance", 20);
        Cast(s, "Vines of Vastwood", 10);
        CHECK(Power(s, b) == 5);                     // ...but it TARGETS, so Defiance fires
        Cast(s, "Vines of Vastwood (Kicked)", 10);
        CHECK(Power(s, b) == 12);                    // +3 Defiance +4 kicked
    }
}

TEST_CASE("morph pumps: kicked Vines is enumerated once PER TARGET (the generic kicker gap)")
{
    EnsureCardsMorph();
    GameState s = BaseM();
    for (int k = 0; k < 3; ++k) { PutM(s, "Forest", 40 + k); }
    PutBear(s, 10);
    PutM(s, "Spellskite", 11);
    s.players[0].hand.push_back(CardM("Vines of Vastwood", 101));
    const std::vector<Action> acts = TurnSolver::CollectActionsForTest(s, /*is_pre_combat=*/true);
    int kicked = 0, unkicked = 0, untargeted_kicked = 0;
    for (const Action& a : acts)
    {
        if (a.kind != Action::Kind::CastFromHand || a.card_name.str() != "Vines of Vastwood") { continue; }
        if (a.kicked) { ++kicked; if (a.enchant_target == 0) { ++untargeted_kicked; } }
        else          { ++unkicked; }
    }
    CHECK(kicked == 2);
    CHECK(unkicked == 2);
    CHECK(untargeted_kicked == 0);
}

TEST_CASE("morph pumps: Legion Stronghold always enters tapped and taps for R or W")
{
    EnsureCardsMorph();
    const CardDefinition& back = DefM("Legion Stronghold");
    CHECK(back.card.IsLand());
    CHECK(back.params.enters_tapped);
    REQUIRE(back.params.produces.size() == 2);
    CHECK(back.params.produces[0] == Color::Red);
    CHECK(back.params.produces[1] == Color::White);
    CHECK(back.params.etb_pay_life_to_untap == 0);
}

TEST_CASE("morph pumps: Spellskite redirect -- Defiance fires for BOTH targets, the payload lands on Spellskite")
{
    EnsureCardsMorph();
    GameState s = BaseM();
    const int b  = PutBear(s, 10);
    const int sk = PutM(s, "Spellskite", 11);
    PutM(s, "Wild Defiance", 20);
    Cast(s, "Might of Old Krosa", 10, /*redirect=*/11);
    CHECK(Power(s, b) == 5);                         // the bear became the target: +3 only
    CHECK(Power(s, sk) == 7);                        // 0 + 3 (redirect) + 4 (Might)
}

TEST_CASE("morph pumps: the redirect twin is offered only with a watcher, and carries a {U/P} pip")
{
    EnsureCardsMorph();
    auto count_redirects = [](const GameState& s, int& life_twins)
    {
        int n = 0;
        life_twins = 0;
        for (const Action& a : TurnSolver::CollectActionsForTest(s, /*is_pre_combat=*/true))
        {
            if (a.kind != Action::Kind::CastFromHand || a.trick_redirect == 0) { continue; }
            ++n;
            if (a.phyrexian_life == 2) { ++life_twins; CHECK(a.cost.blue == 0); }
            else                       { CHECK(a.cost.blue == 1); CHECK(a.cost.phyrexian_count == 1); }
        }
        return n;
    };
    GameState s = BaseM();
    for (int k = 0; k < 3; ++k) { PutM(s, "Forest", 40 + k); }
    PutBear(s, 10);
    PutM(s, "Spellskite", 11);
    s.players[0].hand.push_back(CardM("Might of Old Krosa", 101));
    int life = 0;
    CHECK(count_redirects(s, life) == 0);            // no watcher: the redirect is dominated
    PutM(s, "Wild Defiance", 20);
    const int n = count_redirects(s, life);
    CHECK(n >= 1);                                   // bear -> Spellskite (mana form)
    CHECK(life >= 1);                                // ...and its pay-2-life twin
}

TEST_CASE("morph pumps: the Morph list routes to MorphProvider, whose rank orders permanents < pumps < doublers")
{
    EnsureCardsMorph();
    const Decklist deck = DeckLoader::LoadFromFile(ResolveHeuristicDefaultsPath("decks/Morph/Morph.cod"));
    const DecisionProvider& p = DetectDecisionProvider(deck);
    CHECK(std::string(p.Name()) == "Morph");
    CHECK(p.OrderOpaqueCastsByRank());
    GameState s = BaseM();
    const int defiance = p.CastOrderRank(s, DefM("Wild Defiance"));
    const int might    = p.CastOrderRank(s, DefM("Might of Old Krosa"));
    const int vines    = p.CastOrderRank(s, DefM("Vines of Vastwood (Kicked)"));
    const int berserk  = p.CastOrderRank(s, DefM("Berserk"));
    const int legion   = p.CastOrderRank(s, DefM("Legion Leadership"));
    const int tbr      = p.CastOrderRank(s, DefM("Temur Battle Rage"));
    const int skite    = p.CastOrderRank(s, DefM("Spellskite"));
    CHECK(skite < might);
    CHECK(defiance < might);
    CHECK(defiance < skite);     // INT-8 (B, USER R8): Wild Defiance before every other permanent
    CHECK(might == vines);
    CHECK(might < tbr);          // ...fixed pumps before the doubling tier,
    CHECK(tbr < berserk);        // ...Temur Battle Rage first in it (its Defiance trigger is then doubled),
    CHECK(berserk == legion);    // ...the doublers last (they commute)
}

// INT-5 (2026-11-12; logs/morph/diag1-report.md): the plan-signature dedup keyed a solo-target trick by NAME
// only, so plans differing only in the trick's TARGET merged into the first enumerated -- the first creature
// in battlefield order (a 0-power dork), since EvalCard does not read the target. The search could never
// Berserk the real attacker: a greedy pick inside the search window. Both signature builders now key the
// trick variant (#TT: target / same-plan hand target / strive count / X), so each target is its own plan.
TEST_CASE("morph pumps: plans differing only in a trick's TARGET are distinct plans (no first-target fold)")
{
    EnsureCardsMorph();
    GameState s = BaseM();
    for (int k = 0; k < 3; ++k) { PutM(s, "Forest", 40 + k); }
    PutM(s, "Birds of Paradise", 10);   // FIRST on the battlefield: the old dedup's survivor
    PutBear(s, 11);                     // a distinct, non-interchangeable body
    s.players[0].hand.push_back(CardM("Berserk", 101));
    std::set<int> bf_targets;
    int hand_targeted = 0;
    for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(s, /*is_pre_combat=*/true))
    {
        for (const Action& a : p.actions)
        {
            if (a.kind != Action::Kind::CastFromHand || a.card_name.str() != "Berserk") { continue; }
            bf_targets.insert(a.enchant_target);
        }
    }
    CHECK(bf_targets.count(10) == 1);
    CHECK(bf_targets.count(11) == 1);   // pre-fix: folded into the Birds plan, unfindable at any budget

    // A same-plan HAND creature target ("cast it, then Berserk it") is a distinct plan too.
    s.players[0].hand.push_back(CardM("Birds of Paradise", 102));
    bool on_hand_birds = false, on_bf_birds = false;
    for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(s, /*is_pre_combat=*/true))
    {
        bool casts_birds = false;
        int  tgt = 0;
        for (const Action& a : p.actions)
        {
            if (a.kind != Action::Kind::CastFromHand) { continue; }
            if (a.card_name.str() == "Birds of Paradise") { casts_birds = true; }
            if (a.card_name.str() == "Berserk") { tgt = a.enchant_target; if (!a.trick_hand_target.empty()) { ++hand_targeted; } }
        }
        if (casts_birds && tgt == 102) { on_hand_birds = true; }
        if (casts_birds && tgt == 10)  { on_bf_birds = true; }
    }
    CHECK(hand_targeted >= 1);
    CHECK(on_hand_birds);
    CHECK(on_bf_birds);                 // same cast-name multiset {Birds, Berserk}: both survive the dedup
}

// INT-6 USER RULE (2026-11-12): "The largest attacking creature should be the pump target always." Under
// MorphProvider every solo-target trick has ONE target variant, the cast-time sentinel kTrickBestOwnTarget; the
// creature is picked when the trick resolves -- the most combat damage (power, x2 double strike; ties -> first in
// battlefield order), counting a TRAIL flip the plan makes after the casts (rulings 2-4).
TEST_CASE("morph pumps: largest-attacker rule -- one cast-time variant per trick, none without a recipient")
{
    EnsureCardsMorph();
    static const MorphProvider mp;
    auto variants = [](const GameState& s, const char* name)
    {
        std::vector<int> t;
        for (const Action& a : TurnSolver::CollectActionsForTest(s, /*is_pre_combat=*/true))
        {
            if (a.kind == Action::Kind::CastFromHand && a.card_name.str() == name) { t.push_back(a.enchant_target); }
        }
        return t;
    };
    GameState s = BaseM();
    s.m_provider = &mp;
    for (int k = 0; k < 3; ++k) { PutM(s, "Forest", 40 + k); }
    PutM(s, "Birds of Paradise", 10);
    PutBear(s, 11);
    PutM(s, "Blistering Firecat (Face-down)", 12);
    s.players[0].hand.push_back(CardM("Berserk", 101));
    s.players[0].hand.push_back(CardM("Vines of Vastwood", 102));
    CHECK(variants(s, "Berserk") == std::vector<int>{ kTrickBestOwnTarget });
    CHECK(variants(s, "Vines of Vastwood") == std::vector<int>{ kTrickBestOwnTarget });   // INT-8 R7: kicked only (no Defiance)

    // No creature can attack this turn and no hasty creature in hand: no variant (the old non-attacker fold's answer).
    GameState sick = BaseM();
    sick.m_provider = &mp;
    for (int k = 0; k < 3; ++k) { PutM(sick, "Forest", 40 + k); }
    sick.battlefield[static_cast<std::size_t>(PutBear(sick, 11))].entered_this_turn = true;
    sick.players[0].hand.push_back(CardM("Berserk", 101));
    CHECK(variants(sick, "Berserk").empty());
    // ...a hasty hand creature the plan can cast first is a recipient.
    sick.players[0].hand.push_back(CardM("Blistering Firecat", 103));
    CHECK(variants(sick, "Berserk") == std::vector<int>{ kTrickBestOwnTarget });

    // Without the provider (human play / MTG_UNPRUNE=tricktarget / =0 take this road): every target again.
    s.m_provider = nullptr;
    const std::vector<int> all = variants(s, "Berserk");
    CHECK(std::find(all.begin(), all.end(), 11) != all.end());
    CHECK(std::find(all.begin(), all.end(), kTrickBestOwnTarget) == all.end());
}

TEST_CASE("morph pumps: largest-attacker rule -- the cast-time pick (power, double strike, ties, pumps stack)")
{
    EnsureCardsMorph();
    static const MorphProvider mp;
    GameState s = BaseM();
    s.m_provider = &mp;
    const int birds = PutM(s, "Birds of Paradise", 10);
    const int bear  = PutBear(s, 11);
    const int bear2 = PutBear(s, 13);
    (void)birds;
    // Tie (2 vs 2): the first in battlefield order.
    CHECK(FindBestOwnAttacker(s, 0) == bear);
    Cast(s, "Might of Old Krosa", kTrickBestOwnTarget);   // main phase: +4/+4 -> the first bear is 6/6
    CHECK(Power(s, bear) == 6);
    Cast(s, "Berserk", kTrickBestOwnTarget);              // the pumps STACK on the largest: 12
    CHECK(Power(s, bear) == 12);
    CHECK(Power(s, bear2) == 2);
    // Double strike doubles the score: a 3-power double striker beats a 5-power creature.
    GameState d = BaseM();
    d.m_provider = &mp;
    const int five = PutBear(d, 21);
    d.battlefield[static_cast<std::size_t>(five)].temp_power_bonus = 3;
    const int ds = PutBear(d, 22);
    d.battlefield[static_cast<std::size_t>(ds)].temp_power_bonus = 1;
    d.battlefield[static_cast<std::size_t>(ds)].temp_double_strike = true;
    CHECK(FindBestOwnAttacker(d, 0) == ds);
}

TEST_CASE("morph pumps: largest-attacker rule -- a TRAIL flip later in the plan counts (look-ahead)")
{
    EnsureCardsMorph();
    static const MorphProvider mp;
    GameState s = BaseM();
    s.m_provider = &mp;
    s.deck_has_morph = true;
    const int hyst = PutM(s, "Hystrodon (Face-down)", 30);     // 2/2 face down; first in battlefield order
    const int den  = PutM(s, "Den Protector (Face-down)", 31); // 2/2 face down; 2/1 + a megamorph counter face up
    CHECK(FindBestOwnAttacker(s, 0) == hyst);                  // a tie on the board as it stands
    {
        PlanTrailFlips t;
        t.state = &s;
        t.nums[0] = 31;
        t.n = 1;
        const PlanTrailFlipsScope scope(t);
        CHECK(LargestAttackerScore(s.battlefield[static_cast<std::size_t>(den)], s) == 3);
        CHECK(FindBestOwnAttacker(s, 0) == den);               // 3 at combat beats 2
        const GameState copy = s;                              // a copy (a nested rollout) never inherits it
        CHECK(FindBestOwnAttacker(copy, 0) == hyst);
    }
    CHECK(FindBestOwnAttacker(s, 0) == hyst);                  // scope gone

    // The builder: only TRAIL battlefield flips; a LEAD flip has already happened when the casts run.
    std::vector<Action> acts(2);
    acts[0].kind = Action::Kind::ActivatePermAbility;
    acts[0].ability_mode = Action::AbilityMode::TurnFaceUp;
    acts[0].sac_source_id = 30;
    acts[1] = acts[0];
    acts[1].ability_mode = Action::AbilityMode::TurnFaceUpLate;
    acts[1].sac_source_id = 31;
    const PlanTrailFlips built = TurnSolver::PlanTrailFlipsFor(s, acts);
    CHECK(built.n == 1);
    CHECK(built.nums[0] == 31);
    CHECK(built.state == &s);
}

// USER rulings 5 + 6 (2026-11-12, verbatim): "Yes, let's drop spellskites ability heuristically unless Wild
// Defiance is active. If it is active we should keep it for the potential double pump." / "The important thing if
// we are pumping this way is that Spellskite should not be the target even if it is the largest creature. The
// reason being that we cannot get the benefit of redirecting."
TEST_CASE("morph pumps: largest-attacker rule -- the Spellskite twin exists only with Wild Defiance")
{
    EnsureCardsMorph();
    static const MorphProvider mp;
    auto twins = [](const GameState& s)
    {
        int n = 0;
        for (const Action& a : TurnSolver::CollectActionsForTest(s, /*is_pre_combat=*/true))
        {
            if (a.kind != Action::Kind::CastFromHand || a.card_name.str() != "Might of Old Krosa") { continue; }
            CHECK(a.enchant_target == kTrickBestOwnTarget);            // the rule: one cast-time target
            if (a.trick_redirect != 0) { CHECK(a.trick_redirect == 11); ++n; }
        }
        return n;
    };
    GameState s = BaseM();
    s.m_provider = &mp;
    for (int k = 0; k < 3; ++k) { PutM(s, "Forest", 40 + k); }
    PutBear(s, 10);
    PutM(s, "Spellskite", 11);
    s.players[0].hand.push_back(CardM("Might of Old Krosa", 101));
    CHECK(twins(s) == 0);                                              // Defiance absent: no twin
    GameState in_hand = s;
    in_hand.players[0].hand.push_back(CardM("Wild Defiance", 102));
    CHECK(twins(in_hand) >= 1);                                        // in hand: kept (the plan may cast it first)
    PutM(s, "Wild Defiance", 20);
    CHECK(twins(s) >= 1);                                              // Defiance active: the twin is offered
    // Spellskite the ONLY creature: the twin has no non-redirector original target, so there is none.
    GameState lone = BaseM();
    lone.m_provider = &mp;
    for (int k = 0; k < 3; ++k) { PutM(lone, "Forest", 40 + k); }
    PutM(lone, "Spellskite", 11);
    PutM(lone, "Wild Defiance", 20);
    lone.players[0].hand.push_back(CardM("Might of Old Krosa", 101));
    CHECK(twins(lone) == 0);
}

TEST_CASE("morph pumps: largest-attacker rule -- the twin resolves: both Defiance triggers, payload on Spellskite")
{
    EnsureCardsMorph();
    static const MorphProvider mp;
    GameState s = BaseM();
    s.m_provider = &mp;
    const int b  = PutBear(s, 10);
    const int sk = PutM(s, "Spellskite", 11);
    PutM(s, "Wild Defiance", 20);
    Cast(s, "Might of Old Krosa", kTrickBestOwnTarget, /*redirect=*/11);
    CHECK(Power(s, b) == 5);                                           // original target: its Defiance +3 only
    CHECK(Power(s, sk) == 7);                                          // 0 + 3 (redirect trigger) + 4 (Might)
}

TEST_CASE("morph pumps: largest-attacker rule -- Spellskite as the LARGEST is never the twin's original target")
{
    EnsureCardsMorph();
    static const MorphProvider mp;
    GameState s = BaseM();
    s.m_provider = &mp;
    const int small = PutBear(s, 10);                                  // 2 power
    const int sk    = PutM(s, "Spellskite", 11);
    s.battlefield[static_cast<std::size_t>(sk)].temp_power_bonus = 5;  // Spellskite 5: the largest
    const int mid   = PutBear(s, 12);
    s.battlefield[static_cast<std::size_t>(mid)].temp_power_bonus = 1; // 3: the next-largest
    PutM(s, "Wild Defiance", 20);
    CHECK(FindBestOwnAttacker(s, 0) == sk);                            // the plain rule counts Spellskite
    CHECK(FindBestOwnAttacker(s, 0, /*exclude_redirector=*/true) == mid);
    GameState plain = s;
    Cast(plain, "Might of Old Krosa", kTrickBestOwnTarget);            // plain variant: Spellskite, one trigger
    CHECK(Power(plain, sk) == 12);
    CHECK(Power(plain, mid) == 3);
    Cast(s, "Might of Old Krosa", kTrickBestOwnTarget, /*redirect=*/11);
    CHECK(Power(s, mid) == 6);                                         // the next-largest was the original target
    CHECK(Power(s, small) == 2);
    CHECK(Power(s, sk) == 12);                                         // 5 + 3 (redirect trigger) + 4 (Might)
}

// ---- INT-8 (USER rulings R7-R10, 2026-11-12) ----------------------------------------------------------------------
// R7: "Vines should only be cast with Kicker heuristically." / R7b: "Sorry, except for with Wild Defiance."
TEST_CASE("morph INT-8 (A): Vines is offered KICKED only, unless Wild Defiance is around")
{
    EnsureCardsMorph();
    static const MorphProvider mp;
    auto count = [](const GameState& s, int& kicked, int& unkicked)
    {
        kicked = unkicked = 0;
        for (const Action& a : TurnSolver::CollectActionsForTest(s, /*is_pre_combat=*/true))
        {
            if (a.kind != Action::Kind::CastFromHand || a.card_name.str() != "Vines of Vastwood") { continue; }
            (a.kicked ? kicked : unkicked) += 1;
        }
    };
    GameState s = BaseM();
    s.m_provider = &mp;
    for (int k = 0; k < 3; ++k) { PutM(s, "Forest", 40 + k); }
    PutBear(s, 10);
    s.players[0].hand.push_back(CardM("Vines of Vastwood", 101));
    int kicked = 0, unkicked = 0;
    count(s, kicked, unkicked);
    CHECK(kicked >= 1);
    CHECK(unkicked == 0);                                  // no Defiance: kicked only
    GameState in_hand = s;
    in_hand.players[0].hand.push_back(CardM("Wild Defiance", 102));
    count(in_hand, kicked, unkicked);
    CHECK(kicked >= 1);
    CHECK(unkicked >= 1);                                  // Defiance in hand: both searched
    PutM(s, "Wild Defiance", 20);
    count(s, kicked, unkicked);
    CHECK(kicked >= 1);
    CHECK(unkicked >= 1);                                  // Defiance active: both searched
}

// R8 / R9 (C): Den Protector's named TRAIL flip runs right after the cast of the card it returns, so a later doubler
// doubles its megamorph counter: Might (+4) on the face-down 2/2 = 6, flip -> 2/1 +4 +1 counter = 7, Berserk -> 14.
// The trailing position (the generic provider) doubles 6 -> 12, then flips -> 13.
TEST_CASE("morph INT-8 (C): the named trail flip runs right after its target's cast, before the doubler")
{
    EnsureCardsMorph();
    static const MorphProvider mp;
    auto run = [](const DecisionProvider* prov)
    {
        GameState s = BaseM();
        s.m_provider = prov;
        s.deck_has_morph = true;
        PutM(s, "Den Protector (Face-down)", 31);
        for (int k = 0; k < 4; ++k) { PutM(s, "Forest", 40 + k); }
        s.players[0].hand.push_back(CardM("Might of Old Krosa", 101));
        s.players[0].hand.push_back(CardM("Berserk", 102));
        auto trick = [](const std::string& n)
        {
            Action a;
            a.kind = Action::Kind::CastFromHand;
            a.card_name = n;
            a.def = &DefM(n);
            a.hand_index = -1;
            a.cost = DefM(n).card.m_mana_cost;
            a.enchant_target = 31;
            a.is_noncreature = true;
            return a;
        };
        Action flip;
        flip.kind = Action::Kind::ActivatePermAbility;
        flip.card_name = "Den Protector (Face-down)";
        flip.def = &DefM("Den Protector (Face-down)");
        flip.hand_index = -1;
        flip.sac_source_id = 31;
        flip.ability_mode = Action::AbilityMode::TurnFaceUpLate;
        flip.chosen_x = 1;
        flip.cost = *DefM("Den Protector").params.megamorph_cost;
        flip.tutor_target = std::string("Might of Old Krosa");
        flip.is_noncreature = true;
        TurnSolver::Plan p;
        p.actions = { trick("Might of Old Krosa"), trick("Berserk"), flip };
        // Due-ness: nothing before the cast (the Might is in hand, not the graveyard).
        std::vector<Action> due;
        TurnSolver::FaceUpInlineTrailDue(s, p.actions, due);
        CHECK(due.empty());
        TurnSolver::ApplyPlan(s, p, /*is_pre_combat=*/true);
        int power = -1;
        for (const Permanent& q : s.battlefield) { if (q.card.m_number == 31) { power = CombatPowerOf(q, s); } }
        bool might_in_hand = false;
        for (const Card& c : s.players[0].hand) { might_in_hand |= c.m_name.str() == "Might of Old Krosa"; }
        CHECK(might_in_hand);                              // regrown either way
        return power;
    };
    CHECK(run(&mp) == 14);
    CHECK(run(nullptr) == 13);
}

TEST_CASE("morph INT-8 (C): a battlefield Den's TRAIL flip naming a GRAVEYARD card is not emitted (LEAD covers it)")
{
    EnsureCardsMorph();
    static const MorphProvider mp;
    GameState s = BaseM();
    s.m_provider = &mp;
    s.deck_has_morph = true;
    PutM(s, "Den Protector (Face-down)", 31);
    for (int k = 0; k < 4; ++k) { PutM(s, "Forest", 40 + k); }
    s.players[0].graveyard.push_back(CardM("Berserk", 90));
    s.players[0].hand.push_back(CardM("Might of Old Krosa", 101));
    int lead_berserk = 0, trail_berserk = 0, trail_might = 0;
    for (const Action& a : TurnSolver::CollectActionsForTest(s, /*is_pre_combat=*/true))
    {
        if (a.kind != Action::Kind::ActivatePermAbility || a.sac_source_id != 31) { continue; }
        const bool trail = a.ability_mode == Action::AbilityMode::TurnFaceUpLate;
        if (a.tutor_target.str() == "Berserk") { (trail ? trail_berserk : lead_berserk) += 1; }
        if (trail && a.tutor_target.str() == "Might of Old Krosa") { ++trail_might; }
    }
    CHECK(lead_berserk == 1);
    CHECK(trail_berserk == 0);
    CHECK(trail_might == 1);
}

// R9 (D): "Redirect all Pump Spells" -- all-or-none per plan, doublers included; the 2-life pips must leave us above 0.
TEST_CASE("morph INT-8 (D): Spellskite redirects are all-or-none per plan, and must be payable in life")
{
    EnsureCardsMorph();
    static const MorphProvider mp;
    auto scan = [](const GameState& s, int& mixed, int& both, int& single)
    {
        mixed = both = single = 0;
        for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(s, /*is_pre_combat=*/true))
        {
            int rd = 0, plain = 0;
            for (const Action& a : p.actions)
            {
                if (a.kind != Action::Kind::CastFromHand) { continue; }
                if (a.trick_redirect != 0) { ++rd; }
                else if (a.enchant_target == kTrickBestOwnTarget) { ++plain; }
            }
            if (rd > 0 && plain > 0) { ++mixed; }
            if (rd == 2) { ++both; }
            if (rd == 1 && plain == 0) { ++single; }
        }
    };
    GameState s = BaseM();
    s.m_provider = &mp;
    for (int k = 0; k < 4; ++k) { PutM(s, "Forest", 40 + k); }
    PutBear(s, 10);
    PutM(s, "Spellskite", 11);
    PutM(s, "Wild Defiance", 20);
    s.players[0].hand.push_back(CardM("Might of Old Krosa", 101));
    s.players[0].hand.push_back(CardM("Berserk", 102));
    int mixed = 0, both = 0, single = 0;
    scan(s, mixed, both, single);
    CHECK(mixed == 0);
    CHECK(both >= 1);                                      // the group form (4 life of 20)
    CHECK(single >= 1);                                    // one trick alone, redirected
    s.players[0].life = 4;                                 // two redirects would take us to 0
    scan(s, mixed, both, single);
    CHECK(mixed == 0);
    CHECK(both == 0);
    CHECK(single >= 1);
}

// R9 / R10 (E): Berserk first -- an ORDER (the move-order tier and the regrowth variants' order), never a restriction.
TEST_CASE("morph INT-8 (E): Berserk plans are fronted; Den Protector's regrowth names are ordered Berserk first")
{
    EnsureCardsMorph();
    static const MorphProvider mp;
    GameState s = BaseM();
    s.m_provider = &mp;
    std::vector<Action> cands(3);
    cands[0].kind = Action::Kind::CastFromHand;
    cands[0].card_name = "Berserk";
    cands[0].def = &DefM("Berserk");
    cands[1] = cands[0];
    cands[1].card_name = "Might of Old Krosa";
    cands[1].def = &DefM("Might of Old Krosa");
    cands[2].kind = Action::Kind::ActivatePermAbility;
    cands[2].ability_mode = Action::AbilityMode::TurnFaceUpLate;
    cands[2].tutor_target = std::string("Berserk");
    CHECK(mp.PlanMoveOrderFront(s, cands, { 0 }, false));
    CHECK_FALSE(mp.PlanMoveOrderFront(s, cands, { 1 }, false));
    CHECK(mp.PlanMoveOrderFront(s, cands, { 1, 2 }, false));     // Den Protector regrowing Berserk
    // INT-9 arm MTG_MORPH_BERSERK_FIRST_LETHAL_ONLY (DEFAULT OFF): the tier only for a plan projected lethal.
    heurarm::t_arm[heurarm::MORPH_BERSERK_FIRST_LETHAL] = 1;
    CHECK_FALSE(mp.PlanMoveOrderFront(s, cands, { 0 }, false));
    CHECK(mp.PlanMoveOrderFront(s, cands, { 0 }, true));
    CHECK_FALSE(mp.PlanMoveOrderFront(s, cands, { 1 }, true));
    heurarm::t_arm[heurarm::MORPH_BERSERK_FIRST_LETHAL] = -1;
    std::vector<std::string> names = { "Forest", "Blistering Firecat", "Temur Battle Rage", "Vines of Vastwood",
                                       "Legion Leadership", "Might of Old Krosa", "Berserk" };
    mp.FaceUpRegrowOrder(s, names);                       // no attacker, no mana: Might before Leadership
    const std::vector<std::string> want = { "Berserk", "Might of Old Krosa", "Legion Leadership", "Vines of Vastwood",
                                            "Temur Battle Rage", "Blistering Firecat", "Forest" };
    CHECK(names == want);
    // A 5-power attacker and 4+ mana left after the {1}{G} flip: Leadership (doubling +5) before Might (+4).
    PutM(s, "Blistering Firecat", 7);
    for (int k = 0; k < 4; ++k) { PutM(s, "Mountain", 50 + k); }
    std::vector<std::string> two = { "Might of Old Krosa", "Legion Leadership" };
    mp.FaceUpRegrowOrder(s, two);
    CHECK(two.front() == "Legion Leadership");
}

// R11 (verbatim 2026-11-12: "It should be disallowed heuristically if no Wild Defiance ends up in the plan."): a redirect
// twin or an unkicked Vines needs the watcher on the battlefield or CAST IN THE SAME PLAN; one left in hand voids it.
TEST_CASE("morph INT-8 (R11): redirect twins and unkicked Vines need Wild Defiance on board or cast in the plan")
{
    EnsureCardsMorph();
    static const MorphProvider mp;
    GameState s = BaseM();
    s.m_provider = &mp;
    for (int k = 0; k < 6; ++k) { PutM(s, "Forest", 40 + k); }
    PutBear(s, 10);
    PutM(s, "Spellskite", 11);
    s.players[0].hand.push_back(CardM("Wild Defiance", 100));
    s.players[0].hand.push_back(CardM("Might of Old Krosa", 101));
    s.players[0].hand.push_back(CardM("Vines of Vastwood", 102));
    int with_defiance = 0, voided = 0;
    for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(s, /*is_pre_combat=*/true))
    {
        bool defiance = false, needs = false;
        for (const Action& a : p.actions)
        {
            if (a.kind != Action::Kind::CastFromHand) { continue; }
            if (a.card_name.str() == "Wild Defiance") { defiance = true; }
            if (a.trick_redirect != 0 || (a.card_name.str() == "Vines of Vastwood" && !a.kicked)) { needs = true; }
        }
        if (needs && defiance) { ++with_defiance; }
        if (needs && !defiance) { ++voided; }
    }
    CHECK(voided == 0);
    CHECK(with_defiance >= 1);
}

// R12 (verbatim: "Yes, Temur Battle Rage 2 should target another non-zero power creature. (after pumps)").
TEST_CASE("morph INT-8 (R12): a second Temur Battle Rage goes on the largest creature WITHOUT double strike")
{
    EnsureCardsMorph();
    static const MorphProvider mp;
    GameState s = BaseM();
    s.m_provider = &mp;
    const int cat  = PutM(s, "Blistering Firecat", 7);    // 7/1 haste
    const int bear = PutBear(s, 10);                       // 2/2
    Cast(s, "Temur Battle Rage", kTrickBestOwnTarget);
    CHECK(CreatureHasDoubleStrike(s.battlefield[static_cast<std::size_t>(cat)], s));
    CHECK_FALSE(CreatureHasDoubleStrike(s.battlefield[static_cast<std::size_t>(bear)], s));
    Cast(s, "Temur Battle Rage", kTrickBestOwnTarget);     // the cat already strikes twice: the bear
    CHECK(CreatureHasDoubleStrike(s.battlefield[static_cast<std::size_t>(bear)], s));
    // Only a 0-power other creature: no eligible recipient, the plain pick (a no-op the not-cast sibling dominates).
    GameState z = BaseM();
    z.m_provider = &mp;
    const int cat2  = PutM(z, "Blistering Firecat", 7);
    const int birds = PutM(z, "Birds of Paradise", 12);    // 0/1
    Cast(z, "Temur Battle Rage", kTrickBestOwnTarget);
    Cast(z, "Temur Battle Rage", kTrickBestOwnTarget);
    CHECK(CreatureHasDoubleStrike(z.battlefield[static_cast<std::size_t>(cat2)], z));
    CHECK_FALSE(CreatureHasDoubleStrike(z.battlefield[static_cast<std::size_t>(birds)], z));
}

// R13 (verbatim: "Ties can go to the oldest if the creatures do not differ in ability to tap for mana. We should prefer
// non-mana-producing targets.") + R14 ("Keeping in mind that the sizing is done relative to the morphed body if morph is
// used."): a tie prefers the non-mana body; a face-down body has no abilities, so it never produces mana.
TEST_CASE("morph INT-8 (R13/R14): ties prefer a non-mana body; sizing and mana read the combat-time (morphed) body")
{
    EnsureCardsMorph();
    static const MorphProvider mp;
    GameState s = BaseM();
    s.m_provider = &mp;
    const int birds = PutM(s, "Birds of Paradise", 12);   // 0/1, a mana dork, OLDEST
    const int skite = PutM(s, "Spellskite", 11);          // 0/4, no mana
    CHECK(LargestAttackerProducesMana(s.battlefield[static_cast<std::size_t>(birds)], s));
    CHECK_FALSE(LargestAttackerProducesMana(s.battlefield[static_cast<std::size_t>(skite)], s));
    CHECK(FindBestOwnAttacker(s, 0) == skite);             // tie at 0: the non-mana body, not the oldest
    // R14: one face-down creature the plan flips later (Den Protector: 2/1 + a megamorph counter = 3 at combat) and
    // one that stays face down (Hystrodon: a 2/2 with no abilities).
    GameState m = BaseM();
    m.m_provider = &mp;
    m.deck_has_morph = true;
    const int hyst = PutM(m, "Hystrodon (Face-down)", 30);
    const int den  = PutM(m, "Den Protector (Face-down)", 31);
    CHECK_FALSE(LargestAttackerProducesMana(m.battlefield[static_cast<std::size_t>(hyst)], m));
    CHECK(FindBestOwnAttacker(m, 0) == hyst);              // as the board stands: a 2/2 tie, the oldest
    PlanTrailFlips t;
    t.state = &m;
    t.nums[0] = 31;
    t.n = 1;
    const PlanTrailFlipsScope scope(t);
    CHECK(FindBestOwnAttacker(m, 0) == den);               // the plan flips Den Protector: 3 at combat beats 2
    Cast(m, "Might of Old Krosa", kTrickBestOwnTarget);    // ...so the cast-time pick pumps it
    CHECK(Power(m, den) == 6);
    CHECK(Power(m, hyst) == 2);
}

// INT-9 fetch rule (USER 2026-11-12, verbatim: "Let's look for other prunes as needed, including for the fetchland Wooded
// Foothills. That should be relatively straightforward as goldfish can just always get stomping ground."): Wooded
// Foothills resolves on play and fetches Stomping Ground first; without one, the generic library order.
// MTG_MORPH_FETCH_RULE=0 restores the generic hold choice and order. (The human-play / MTG_UNPRUNED bypass is the
// engine's -- FetchResolvesOnPlay and the fetch fan -- and is process-static, so not toggled here.)
TEST_CASE("morph INT-9: Wooded Foothills cracks on play and fetches Stomping Ground first")
{
    EnsureCardsMorph();
    static const MorphProvider mp;
    GameState s = BaseM();
    s.m_provider = &mp;
    s.players[0].library.push_back(CardM("Forest", 200));
    s.players[0].library.push_back(CardM("Mountain", 201));
    s.players[0].library.push_back(CardM("Stomping Ground", 202));
    s.players[0].library.push_back(CardM("Gruul Turf", 203));          // not a Mountain / Forest: never a target
    const CardDefinition& foothills = DefM("Wooded Foothills");
    std::vector<std::string> c = mp.FetchCandidates(s, 0, foothills.params);
    REQUIRE(c.size() == 3);
    CHECK(c[0] == "Stomping Ground");
    CHECK(c[1] == "Forest");
    CHECK(c[2] == "Mountain");
    CHECK(mp.FetchSearchCandidates(s, 0, foothills.params, c) == std::vector<std::string>{ "Stomping Ground" });
    CHECK_FALSE(mp.ConsiderFetchHold(s, foothills));
    CHECK(FetchResolvesOnPlay(s, foothills));
    s.players[0].life = 3;                                             // the engine keeps the hold at 3 life or less
    CHECK_FALSE(FetchResolvesOnPlay(s, foothills));
    s.players[0].life = 20;
    // No Stomping Ground left: the generic (library) order.
    s.players[0].library.erase(s.players[0].library.begin() + 2);
    c = mp.FetchCandidates(s, 0, foothills.params);
    REQUIRE(c.size() == 2);
    CHECK(c[0] == "Forest");
    // The lever off: library order, and the hold stays a searched choice.
    s.players[0].library.push_back(CardM("Stomping Ground", 202));
    heurarm::t_arm[heurarm::MORPH_FETCH_RULE] = 0;
    c = mp.FetchCandidates(s, 0, foothills.params);
    CHECK(c[0] == "Forest");
    CHECK(mp.ConsiderFetchHold(s, foothills));
    CHECK_FALSE(FetchResolvesOnPlay(s, foothills));
    heurarm::t_arm[heurarm::MORPH_FETCH_RULE] = -1;
}

// INT-10 (USER, approved 2026-11-12: "Never pump a creature that can't attack this turn."): after combat no creature can
// attack this turn, so a solo-target trick is dominated by not casting it -- the post-combat main offers no trick variant.
// MTG_MORPH_NO_POSTCOMBAT_TRICK=0 restores it; the pre-combat main is unchanged. (Human play / MTG_UNPRUNED take the
// provider's early return, tested above with m_provider = nullptr.)
TEST_CASE("morph INT-10: no solo-target trick is offered after combat")
{
    EnsureCardsMorph();
    static const MorphProvider mp;
    auto variants = [](const GameState& s, const char* name)
    {
        std::vector<int> t;
        for (const Action& a : TurnSolver::CollectActionsForTest(s, /*is_pre_combat=*/s.phase != Phase::PostCombatMain))
        {
            if (a.kind == Action::Kind::CastFromHand && a.card_name.str() == name) { t.push_back(a.enchant_target); }
        }
        return t;
    };
    GameState s = BaseM();
    s.m_provider = &mp;
    for (int k = 0; k < 3; ++k) { PutM(s, "Forest", 40 + k); }
    PutM(s, "Mountain", 43);
    PutBear(s, 11);                                    // untapped, not sick: it CAN attack before combat
    s.players[0].hand.push_back(CardM("Might of Old Krosa", 101));
    s.players[0].hand.push_back(CardM("Temur Battle Rage", 102));
    CHECK(variants(s, "Might of Old Krosa") == std::vector<int>{ kTrickBestOwnTarget });
    CHECK(variants(s, "Temur Battle Rage") == std::vector<int>{ kTrickBestOwnTarget });
    s.phase = Phase::PostCombatMain;                   // combat is over: the same untapped body can no longer attack
    CHECK(variants(s, "Might of Old Krosa").empty());
    CHECK(variants(s, "Temur Battle Rage").empty());
    heurarm::t_arm[heurarm::MORPH_NO_POSTCOMBAT_TRICK] = 0;
    CHECK(variants(s, "Might of Old Krosa") == std::vector<int>{ kTrickBestOwnTarget });
    heurarm::t_arm[heurarm::MORPH_NO_POSTCOMBAT_TRICK] = -1;
    // A deck that queues an additional combat keeps its post-combat tricks.
    s.deck_has_extra_combat = true;
    CHECK(variants(s, "Might of Old Krosa") == std::vector<int>{ kTrickBestOwnTarget });
}

// INT-10 (b): after combat a FACE-UP entry of Blistering Firecat (hard cast, face-down-and-up cast, or turning a face-down
// copy face up) is dominated -- it cannot attack, and is sacrificed at this turn's end step. The face-down cast stays.
// MTG_MORPH_NO_POSTCOMBAT_SACUP=0 restores the entries; the pre-combat main is unchanged.
TEST_CASE("morph INT-10: no face-up Blistering Firecat entry after combat (the face-down cast stays)")
{
    EnsureCardsMorph();
    static const MorphProvider mp;
    struct Seen { int fd0 = 0, fd1 = 0, fd2 = 0, flip = 0; };
    auto seen = [](const GameState& s)
    {
        Seen r;
        for (const Action& a : TurnSolver::CollectActionsForTest(s, /*is_pre_combat=*/s.phase != Phase::PostCombatMain))
        {
            if (a.kind == Action::Kind::CastFromHand && a.card_name.str() == "Blistering Firecat")
            { (a.face_down == 0 ? r.fd0 : a.face_down == 1 ? r.fd1 : r.fd2) += 1; }
            if (a.kind == Action::Kind::ActivatePermAbility && IsTurnFaceUpMode(a.ability_mode) && a.sac_source_id == 12) { ++r.flip; }
        }
        return r;
    };
    GameState s = BaseM();
    s.m_provider = &mp;
    s.deck_has_morph = true;
    for (int k = 0; k < 6; ++k) { PutM(s, "Mountain", 40 + k); }
    PutM(s, "Blistering Firecat (Face-down)", 12);
    s.players[0].hand.push_back(CardM("Blistering Firecat", 101));
    Seen pre = seen(s);
    CHECK(pre.fd0 == 1);
    CHECK(pre.fd1 == 1);
    CHECK(pre.fd2 == 1);
    CHECK(pre.flip >= 1);
    s.phase = Phase::PostCombatMain;
    Seen post = seen(s);
    CHECK(post.fd0 == 0);
    CHECK(post.fd1 == 1);                               // the face-down cast is a real setup line: kept
    CHECK(post.fd2 == 0);
    CHECK(post.flip == 0);
    heurarm::t_arm[heurarm::MORPH_NO_POSTCOMBAT_SACUP] = 0;
    Seen off = seen(s);
    CHECK(off.fd0 == 1);
    CHECK(off.fd2 == 1);
    CHECK(off.flip >= 1);
    heurarm::t_arm[heurarm::MORPH_NO_POSTCOMBAT_SACUP] = -1;
}

// INT-10 identity fold (MTG_FACEUP_LEAD_PIN_FOLD): a LEAD flip's unpinned base resolves to the provider's pick among the
// pre-plan graveyard, every name of which has its own named LEAD variant -- so with names emitted the base is not. With an
// empty graveyard (no names) the base is the only LEAD and stays; the TRAIL base always stays (a different position).
TEST_CASE("morph INT-10: Den Protector's unpinned LEAD flip is folded into its named variants")
{
    EnsureCardsMorph();
    static const MorphProvider mp;
    GameState s = BaseM();
    s.m_provider = &mp;
    s.deck_has_morph = true;
    PutM(s, "Den Protector (Face-down)", 31);
    for (int k = 0; k < 4; ++k) { PutM(s, "Forest", 40 + k); }
    auto count = [&](bool trail, bool pinned)
    {
        int n = 0;
        for (const Action& a : TurnSolver::CollectActionsForTest(s, /*is_pre_combat=*/true))
        {
            if (a.kind != Action::Kind::ActivatePermAbility || a.sac_source_id != 31) { continue; }
            if ((a.ability_mode == Action::AbilityMode::TurnFaceUpLate) != trail) { continue; }
            if (a.tutor_target.empty() != !pinned) { continue; }
            ++n;
        }
        return n;
    };
    CHECK(count(false, false) == 1);                    // empty graveyard: the base LEAD is the only LEAD
    s.players[0].graveyard.push_back(CardM("Berserk", 90));
    s.players[0].graveyard.push_back(CardM("Forest", 91));
    CHECK(count(false, true) == 2);                     // one named LEAD per distinct graveyard name
    CHECK(count(false, false) == 0);                    // ...and no unpinned base beside them
    CHECK(count(true, false) == 1);                     // the TRAIL base stays
    heurarm::t_arm[heurarm::FACEUP_LEAD_PIN_FOLD] = 0;
    CHECK(count(false, false) == 1);
    heurarm::t_arm[heurarm::FACEUP_LEAD_PIN_FOLD] = -1;
}
