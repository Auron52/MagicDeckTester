// Unit tests for the AccomplishedAlchemist chunk-A cards (2026-10-18). Every new param is checked in
// the shared helper that BOTH worlds call, and -- where the param reaches the search -- through the
// rollout's own enumeration + ApplyPlan, so the executor and the rollout cannot disagree silently.
//
//  * Nourish            cast_lifegain 6 -- one event, bumps life_gained_this_turn.
//  * Weather the Storm  storm: max(1, spells_cast_this_turn) instances, ONE GainLife EVENT EACH; the
//                       rollout's apply counts its own cast; a non-storm spell ignores the count.
//  * Scholar of Athreos drain_each_opponent: x OpponentHeads on the loss AND the gain (one event);
//                       the rollout's drain action credits the per-head loss.
//  * Brindle Boar       sac_outlet_lifegain: ONE gain event in ApplySacCreatureOutlet; the self-only
//                       outlet is offered only while the gain is READ, and bakes ITSELF as the victim.
//  * Elvish Harbinger   tutor_to_top an Elf + five-colour mana_dork.
//  * Eternal Witness    etb_return_gy_to_hand (searched graveyard pick).
//  * Ezuri              team pump +3/+3 to Elves only, at combat (ApplyFirebreathing).
//  * deck_reads_lifegain_in_play also scans a WISH-REACHABLE sideboard.
//  * Stroke of Genius   (chunk B) draw_target_player: ForcedDraw deck-out boundaries, the searched
//                       (target, X) variants, executor == rollout, and the opponent-library trait.
#include <doctest/doctest.h>

#include "ai/DecisionProviders.h"
#include "ai/ManaPayment.h"
#include "ai/TurnSolver.h"
#include "cards/CardDatabase.h"
#include "core/GameSetup.h"
#include "core/EffectHandler.h"
#include "core/GameState.h"
#include "core/OpponentDeck.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"
#include "deck/DeckLoader.h"
#include "runner/GoldFishRunner.h"

#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace
{

void EnsureCardsAa()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

const CardDefinition& DefAa(const std::string& name)
{
    EnsureCardsAa();
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE_MESSAGE(d != nullptr, "card not in cards.json: ", name);
    return *d;
}

int g_aa_num = 7000;

int PutAa(GameState& s, const std::string& name, bool sick = false)
{
    Permanent p;
    p.card              = DefAa(name).card;
    p.card.m_number     = g_aa_num++;
    p.controller_index  = 0;
    p.owner_index       = 0;
    p.entered_this_turn = sick;
    s.battlefield.push_back(p);
    return p.card.m_number;
}

void HandAa(GameState& s, const std::string& name)
{
    Card c = DefAa(name).card; c.m_number = g_aa_num++; s.players[0].hand.push_back(c);
}

void LibAa(GameState& s, const std::string& name)
{
    Card c = DefAa(name).card; c.m_number = g_aa_num++; s.players[0].library.push_back(c);
}

const Permanent* ByNumAa(const GameState& s, int number)
{
    for (const Permanent& p : s.battlefield) { if (p.card.m_number == number) { return &p; } }
    return nullptr;
}

int PlusCountersAa(const Permanent& p)
{
    int n = 0;
    for (const Counter& c : p.counters)
    { if (c.type == Counter::Type::PlusOnePlusOne) { n += c.count; } }
    return n;
}

GameState FreshAa()
{
    EnsureCardsAa();
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 6;
    s.players[0].life     = 20;
    s.players[1].life     = 20;
    s.players[0].lands_played_this_turn = 1;   // no land drop: keeps the plan space small
    for (int k = 0; k < 10; ++k) { LibAa(s, "Forest"); }
    return s;
}

struct ScopedHeadsAa
{
    int saved;
    explicit ScopedHeadsAa(int n) : saved(gamesetup::t_setup.opponent_heads)
    { gamesetup::t_setup.opponent_heads = n; }
    ~ScopedHeadsAa() { gamesetup::t_setup.opponent_heads = saved; }
};

bool CastsOnlyAa(const TurnSolver::Plan& p, const std::string& name)
{
    int casts = 0; bool hit = false;
    for (const Action& a : p.actions)
    {
        if (a.kind == Action::Kind::PlayLand) { continue; }
        if (a.kind != Action::Kind::CastFromHand) { return false; }
        ++casts;
        if (a.card_name.str() == name) { hit = true; }
    }
    return casts == 1 && hit;
}

}   // namespace

TEST_CASE("Nourish: one 6-life event through the shared resolver")
{
    GameState s = FreshAa();
    const int pm = PutAa(s, "Ajani's Pridemate");          // +1/+1 counter per life-gain EVENT
    ApplyCastLifegain(s, 0, DefAa("Nourish"));
    CHECK(s.players[0].life == 26);
    CHECK(s.players[0].life_gained_this_turn == 6);
    CHECK(PlusCountersAa(*ByNumAa(s, pm)) == 1);
}

TEST_CASE("Weather the Storm: storm = one instance per spell cast this turn, each its own event")
{
    GameState s = FreshAa();
    const int pm = PutAa(s, "Ajani's Pridemate");
    const CardDefinition& wts = DefAa("Weather the Storm");
    CHECK(wts.params.storm);
    CHECK(wts.params.cast_lifegain == 3);
    // Cast as the turn's THIRD spell: the counter (++'d at its own cast) reads 3 -> original + 2 copies.
    s.spells_cast_this_turn = 3;
    ApplyCastLifegain(s, 0, wts);
    CHECK(s.players[0].life_gained_this_turn == 9);
    CHECK(s.players[0].life == 29);
    CHECK(PlusCountersAa(*ByNumAa(s, pm)) == 3);           // three separate events, not one of 9
    // The first spell of the turn: no copies.
    GameState t = FreshAa();
    t.spells_cast_this_turn = 1;
    ApplyCastLifegain(t, 0, wts);
    CHECK(t.players[0].life_gained_this_turn == 3);
    // A NON-storm cast_lifegain spell ignores the count entirely.
    GameState u = FreshAa();
    u.spells_cast_this_turn = 4;
    ApplyCastLifegain(u, 0, DefAa("Nourish"));
    CHECK(u.players[0].life_gained_this_turn == 6);
}

TEST_CASE("Weather the Storm: the rollout's apply counts its own cast (lockstep with the executor)")
{
    GameState s = FreshAa();
    PutAa(s, "Forest");
    PutAa(s, "Forest");
    HandAa(s, "Weather the Storm");
    s.spells_cast_this_turn = 2;                           // two spells already cast this turn
    bool seen = false;
    for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(s, /*is_pre_combat=*/true))
    {
        if (!CastsOnlyAa(p, "Weather the Storm")) { continue; }
        seen = true;
        GameState t = s;
        TurnSolver::ApplyPlan(t, p, /*is_pre_combat=*/true);
        CHECK(t.spells_cast_this_turn == 3);
        CHECK(t.players[0].life_gained_this_turn == 9);   // 3 instances x 3
    }
    CHECK(seen);
}

TEST_CASE("Scholar of Athreos: 'each opponent' scales loss AND gain by heads; one gain event")
{
    const CardDefinition& sch = DefAa("Scholar of Athreos");
    CHECK(sch.params.drain_each_opponent);
    CHECK_FALSE(DefAa("Essence Depleter").params.drain_each_opponent);
    {
        GameState s = FreshAa();
        const int sc = PutAa(s, "Scholar of Athreos");
        const int pm = PutAa(s, "Ajani's Pridemate");
        ApplyPermAbility(s, 0, sc, PermAbilityMode::Drain);
        CHECK(s.players[1].life == 19);
        CHECK(s.players[0].life == 21);
        CHECK(s.players[0].life_gained_this_turn == 1);
        CHECK(PlusCountersAa(*ByNumAa(s, pm)) == 1);
        CHECK(DrainLossPerActivation(sch.params) == 1);
    }
    {
        ScopedHeadsAa heads(2);
        GameState s = FreshAa();
        s.players[1].life = 30;                            // the shared 2HG pool
        const int sc = PutAa(s, "Scholar of Athreos");
        const int pm = PutAa(s, "Ajani's Pridemate");
        ApplyPermAbility(s, 0, sc, PermAbilityMode::Drain);
        CHECK(s.players[1].life == 28);                    // each of two heads loses 1
        CHECK(s.players[0].life_gained_this_turn == 2);    // "equal to the life lost this way"
        CHECK(PlusCountersAa(*ByNumAa(s, pm)) == 1);       // ...as ONE event
        CHECK(DrainLossPerActivation(sch.params) == 2);
        CHECK(DrainGainPerActivation(sch.params) == 2);
        // Essence Depleter's "TARGET opponent" stays x1 under two heads.
        CHECK(DrainLossPerActivation(DefAa("Essence Depleter").params) == 1);
    }
}

TEST_CASE("Scholar of Athreos: the rollout's drain action credits the per-head loss")
{
    ScopedHeadsAa heads(2);
    GameState s = FreshAa();
    s.players[1].life = 30;
    PutAa(s, "Scholar of Athreos");
    PutAa(s, "Elvish Harbinger");                          // any colour: the {B}
    PutAa(s, "Forest");
    PutAa(s, "Forest");
    bool seen = false;
    for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(s, /*is_pre_combat=*/true))
    {
        for (const Action& a : p.actions)
        {
            if (a.kind != Action::Kind::ActivatePermAbility || a.ability_mode != Action::AbilityMode::Drain)
            { continue; }
            seen = true;
            CHECK(a.direct_damage == 2 * a.chosen_x);
            GameState t = s;
            TurnSolver::ApplyPlan(t, p, /*is_pre_combat=*/true);
            CHECK(t.players[1].life <= 28);
            CHECK(t.players[0].life_gained_this_turn == 30 - t.players[1].life);
        }
    }
    CHECK(seen);
}

TEST_CASE("Brindle Boar: sacrifice itself for ONE 4-life event")
{
    GameState s = FreshAa();
    const int bb = PutAa(s, "Brindle Boar");
    const int pm = PutAa(s, "Ajani's Pridemate");
    ApplySacCreatureOutlet(s, 0, bb, bb);
    CHECK(ByNumAa(s, bb) == nullptr);
    CHECK(s.players[0].life == 24);
    CHECK(s.players[0].life_gained_this_turn == 4);
    CHECK(PlusCountersAa(*ByNumAa(s, pm)) == 1);
    CHECK(s.players[0].graveyard.size() == 1);
}

TEST_CASE("Brindle Boar: offered only while the gain is read; bakes ITSELF as the victim")
{
    // Nothing reads life: the sacrifice only costs a 2/2 -> dominated, not offered.
    {
        GameState s = FreshAa();
        PutAa(s, "Brindle Boar");
        PutAa(s, "Arbor Elf");
        for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(s, /*is_pre_combat=*/true))
        {
            for (const Action& a : p.actions) { CHECK(a.kind != Action::Kind::SacCreatureOutlet); }
        }
    }
    // An Alchemist deck reads life_gained_this_turn: offered, with the Boar as its own victim even
    // though a cheaper body (Arbor Elf) is on the board, and the rollout's apply gains the 4.
    GameState s = FreshAa();
    s.deck_reads_lifegain_in_play = true;
    const int bb = PutAa(s, "Brindle Boar");
    PutAa(s, "Arbor Elf");
    PutAa(s, "Accomplished Alchemist");
    bool seen = false;
    for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(s, /*is_pre_combat=*/true))
    {
        for (const Action& a : p.actions)
        {
            if (a.kind != Action::Kind::SacCreatureOutlet) { continue; }
            seen = true;
            CHECK(a.sac_source_id == bb);
            CHECK(a.sac_victim_id == bb);
            GameState t = s;
            TurnSolver::ApplyPlan(t, p, /*is_pre_combat=*/true);
            CHECK(ByNumAa(t, bb) == nullptr);
            CHECK(t.players[0].life_gained_this_turn == 4);
        }
    }
    CHECK(seen);
}

TEST_CASE("deck_reads_lifegain_in_play: a wish-reachable sideboard Alchemist counts")
{
    EnsureCardsAa();
    Decklist wish;
    wish.mainboard.push_back(DefAa("Living Wish").card);
    wish.mainboard.push_back(DefAa("Forest").card);
    wish.sideboard.push_back(DefAa("Accomplished Alchemist").card);
    GameState a;
    GoldFishRunner::StampDeckTraits(a, wish);
    CHECK(a.deck_reads_lifegain_in_play);
    // No wish in the main: the sideboard is unreachable and the stamp stays off.
    Decklist nowish;
    nowish.mainboard.push_back(DefAa("Forest").card);
    nowish.sideboard.push_back(DefAa("Accomplished Alchemist").card);
    GameState b;
    GoldFishRunner::StampDeckTraits(b, nowish);
    CHECK_FALSE(b.deck_reads_lifegain_in_play);
}

TEST_CASE("Elvish Harbinger: five-colour dork; the ETB stacks a searched Elf on top")
{
    const CardDefinition& eh = DefAa("Elvish Harbinger");
    CHECK(eh.tmpl == CardTemplate::ManaDork);
    CHECK(eh.params.produces.size() == 5);
    GameState s = FreshAa();
    for (int k = 0; k < 3; ++k) { PutAa(s, "Forest"); }
    HandAa(s, "Elvish Harbinger");
    LibAa(s, "Accomplished Alchemist");
    LibAa(s, "Ezuri, Renegade Leader");
    // The tutor axis is SEARCHED (resolve mode: one plan per fetch choice); every Elf is reachable.
    // (Library order: ten Forests first, then the two Elves -- so a fetched Elf on top is the tutor.)
    std::set<std::string> tops;
    for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(s, /*is_pre_combat=*/true))
    {
        if (!CastsOnlyAa(p, "Elvish Harbinger")) { continue; }
        GameState t = s;
        TurnSolver::ApplyPlan(t, p, /*is_pre_combat=*/true);
        REQUIRE_FALSE(t.players[0].library.empty());
        tops.insert(t.players[0].library.front().m_name.str());
    }
    CHECK(tops.count("Accomplished Alchemist") == 1);
    CHECK(tops.count("Ezuri, Renegade Leader") == 1);
    // "You MAY search" (tutor_optional): the DECLINE is its own searched variant, leaving the
    // library's own top card (a Forest) where it was.
    CHECK(tops.count("Forest") == 1);
}

TEST_CASE("Eternal Witness: the ETB returns a searched graveyard card to hand")
{
    GameState s = FreshAa();
    for (int k = 0; k < 3; ++k) { PutAa(s, "Forest"); }
    HandAa(s, "Eternal Witness");
    Card gone = DefAa("Nourish").card; gone.m_number = g_aa_num++;
    s.players[0].graveyard.push_back(gone);
    bool seen = false;
    for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(s, /*is_pre_combat=*/true))
    {
        if (!CastsOnlyAa(p, "Eternal Witness")) { continue; }
        seen = true;
        GameState t = s;
        TurnSolver::ApplyPlan(t, p, /*is_pre_combat=*/true);
        bool in_hand = false;
        for (const Card& c : t.players[0].hand) { if (c.m_name.str() == "Nourish") { in_hand = true; } }
        CHECK(in_hand);
        CHECK(t.players[0].graveyard.empty());
    }
    CHECK(seen);
}

TEST_CASE("Ezuri: {2}{G}{G}{G} gives attacking ELVES +3/+3, not other creatures")
{
    GameState s = FreshAa();
    const int ez = PutAa(s, "Ezuri, Renegade Leader");
    const int ae = PutAa(s, "Arbor Elf");
    const int bb = PutAa(s, "Brindle Boar");
    std::vector<int> atk;
    for (int i = 0; i < static_cast<int>(s.battlefield.size()); ++i) { atk.push_back(i); }
    ManaPool pool;
    pool.green = 5;
    const int acts = ApplyFirebreathing(s, 0, atk, pool);
    CHECK(acts == 1);
    CHECK(ByNumAa(s, ez)->EffectivePower() == 5);
    CHECK(ByNumAa(s, ez)->EffectiveToughness() == 5);
    CHECK(ByNumAa(s, ae)->EffectivePower() == 4);
    CHECK(ByNumAa(s, bb)->EffectivePower() == 2);
}


// ===================================================================================== chunk B
namespace
{
// An opponent library of `n` placeholder cards, dealt (opponent_library_dealt) -- the fixture shape.
void DealOppAa(GameState& s, int n)
{
    s.opponent_library_dealt = true;
    for (int k = 0; k < n; ++k)
    {
        Card c = DefAa("Forest").card; c.m_number = opponentdeck::kNumberBase + k;
        s.players[1].library.push_back(c);
    }
}

const Action* StrokeCastAa(const TurnSolver::Plan& p)
{
    if (!CastsOnlyAa(p, "Stroke of Genius")) { return nullptr; }
    for (const Action& a : p.actions)
    { if (a.kind == Action::Kind::CastFromHand) { return &a; } }
    return nullptr;
}
}   // namespace

TEST_CASE("ForcedDraw: deck-out boundaries (n = L-1, L, L+1; empty before; X = 0; no library)")
{
    EnsureCardsAa();
    {   // n < L: draws, no deck-out
        GameState s; DealOppAa(s, 5);
        CHECK(opponentdeck::ForcedDraw(s, 4) == 4);
        CHECK(s.players[1].library.size() == 1);
        CHECK(s.players[1].hand.size() == 4);
        CHECK_FALSE(s.opponent_decked);
        CHECK_FALSE(OpponentHasLost(s));
    }
    {   // n == L: the library is emptied -> decked NOW (their end-of-turn draw is certain this turn)
        GameState s; DealOppAa(s, 5);
        CHECK(opponentdeck::ForcedDraw(s, 5) == 5);
        CHECK(s.players[1].library.empty());
        CHECK(s.opponent_decked);
        CHECK(OpponentHasLost(s));
    }
    {   // n > L: asked to draw from an empty library -- the CR 704.5b loss itself
        GameState s; DealOppAa(s, 5);
        CHECK(opponentdeck::ForcedDraw(s, 6) == 5);
        CHECK(s.opponent_decked);
    }
    {   // library ALREADY empty: any draw decks them; X = 0 asks for no draw and decks nobody
        GameState s; DealOppAa(s, 0);
        CHECK(opponentdeck::ForcedDraw(s, 0) == 0);
        CHECK_FALSE(s.opponent_decked);
        CHECK(opponentdeck::ForcedDraw(s, 1) == 0);
        CHECK(s.opponent_decked);
    }
    {   // no library dealt: empty is not absent -- a forced draw is inert, never a spurious win
        GameState s;
        CHECK(opponentdeck::ForcedDraw(s, 3) == 0);
        CHECK_FALSE(s.opponent_decked);
        CHECK(opponentdeck::ForcedDrawLethalX(s) == 0);
    }
    {   // the lethal X the enumerator sizes from
        GameState s; DealOppAa(s, 7);
        CHECK(opponentdeck::ForcedDrawLethalX(s) == 7);
        GameState e; DealOppAa(e, 0);
        CHECK(opponentdeck::ForcedDrawLethalX(e) == 1);
    }
}

TEST_CASE("PerformDrawX: a self over-draw is a loss; within the library it is a plain draw")
{
    GameState s = FreshAa();                       // 10 Forests in our library
    CHECK(PerformDrawX(s, 0, /*target_opponent=*/false, 4) == 4);
    CHECK(s.players[0].hand.size() == 4);
    CHECK(s.players[0].cards_drawn_this_turn == 4);
    CHECK_FALSE(s.player_lost_on_draw);
    CHECK(PerformDrawX(s, 0, false, 7) == 6);      // only 6 left
    CHECK(s.player_lost_on_draw);
}

TEST_CASE("Stroke of Genius: the search is offered BOTH targets; the opponent variant is the lethal X")
{
    GameState s = FreshAa();
    for (int k = 0; k < 7; ++k) { PutAa(s, "Island"); }   // 7 mana: base {2}{U} + X up to 4
    HandAa(s, "Stroke of Genius");
    DealOppAa(s, 4);                                      // lethal X = 4, affordable exactly
    std::set<int> self_x; std::set<int> opp_x;
    bool opp_kills = false, self_draws = false;
    for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(s, /*is_pre_combat=*/true))
    {
        const Action* a = StrokeCastAa(p);
        if (a == nullptr) { continue; }
        (a->draw_target != 0 ? opp_x : self_x).insert(a->chosen_x);
        GameState t = s;
        TurnSolver::ApplyPlan(t, p, /*is_pre_combat=*/true);
        if (a->draw_target != 0 && a->chosen_x == 4)
        {
            opp_kills = OpponentHasLost(t) && t.players[1].hand.size() == 4;
            CHECK(t.players[0].hand.empty());             // nothing reached OUR hand
        }
        if (a->draw_target == 0 && a->chosen_x == 4)
        {
            self_draws = (t.players[0].hand.size() == 4) && !OpponentHasLost(t)
                      && t.players[1].library.size() == 4;
        }
    }
    CHECK(opp_x == std::set<int>{ 4 });                   // the minimal lethal X only (provider prune)
    CHECK(self_x == std::set<int>{ 1, 2, 3, 4 });         // cap (4), half (2), 1..3 -- searched, not picked
    CHECK(opp_kills);
    CHECK(self_draws);

    // One mana short of lethal: the opponent is offered only the MAX-affordable X (chunk C2b: the
    // two-turn split deck-out the minimal-lethal-only prune made unfindable); self still is offered.
    GameState u = FreshAa();
    for (int k = 0; k < 6; ++k) { PutAa(u, "Island"); }
    HandAa(u, "Stroke of Genius");
    DealOppAa(u, 4);
    std::set<int> u_opp_x;
    bool any_self = false;
    for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(u, true))
    {
        const Action* a = StrokeCastAa(p);
        if (a == nullptr) { continue; }
        if (a->draw_target != 0) { u_opp_x.insert(a->chosen_x); } else { any_self = true; }
    }
    CHECK(u_opp_x == std::set<int>{ 3 });                 // 6 mana - {2}{U} = X 3 < lethal 4
    CHECK(any_self);
}

TEST_CASE("Stroke of Genius: the executor's ResolveDrawX and the rollout reach the same state")
{
    for (int target : { 0, 1 })
    {
        GameState s = FreshAa();
        DealOppAa(s, 6);
        const CardDefinition& def = DefAa("Stroke of Genius");
        // Executor: the stack entry the cast site builds (Player target = the rider's draw_target).
        GameState ex = s;
        StackEntry e;
        e.type = StackEntry::EntryType::Spell;
        e.source = def.card; e.source.m_number = 4242;
        e.controller_index = 0;
        e.chosen_x = 6;
        Target t; t.type = Target::Type::Player; t.player_index = target;
        e.targets.push_back(t);
        EffectHandler::Resolve(ex, e, def);   // public dispatch -> ResolveDrawX
        // Rollout twin: the one shared resolver.
        GameState ro = s;
        PerformDrawX(ro, 0, target != 0, 6);
        CHECK(ex.players[0].hand.size() == ro.players[0].hand.size());
        CHECK(ex.players[1].hand.size() == ro.players[1].hand.size());
        CHECK(ex.players[0].library.size() == ro.players[0].library.size());
        CHECK(ex.players[1].library.size() == ro.players[1].library.size());
        CHECK(ex.opponent_decked == ro.opponent_decked);
        CHECK(ex.opponent_decked == (target == 1));
        CHECK(ex.player_lost_on_draw == ro.player_lost_on_draw);
    }
}

TEST_CASE("Stroke of Genius: draw_target_player deals the opponent a library; Braingeyser does not")
{
    EnsureCardsAa();
    CHECK(DefAa("Stroke of Genius").params.draw_target_player);
    CHECK_FALSE(DefAa("Braingeyser").params.draw_target_player);
    Decklist with;
    with.mainboard.push_back(DefAa("Forest").card);
    with.sideboard.push_back(DefAa("Stroke of Genius").card);   // the sideboard counts too (Living Wish)
    CHECK(GoldFishRunner::DeckTouchesOpponentZones(with));
    Decklist without;
    without.mainboard.push_back(DefAa("Forest").card);
    without.mainboard.push_back(DefAa("Braingeyser").card);
    CHECK_FALSE(GoldFishRunner::DeckTouchesOpponentZones(without));
    // The real deck file: dealt.
    Decklist aa = DeckLoader::LoadFromFile(ResolveHeuristicDefaultsPath(
        "decks/AccomplishedAlchemist/AccomplishedAlchemist.cod"));
    CHECK(GoldFishRunner::DeckTouchesOpponentZones(aa));
}

// ---- chunk C1 (2026-10-18): Utopia Sprawl, Trufflesnout, the ETB CHOICE AXIS ----------------------

namespace
{
const Permanent* SprawlAa(const GameState& s)
{
    for (const Permanent& p : s.battlefield) { if (p.card.m_name.str() == "Utopia Sprawl") { return &p; } }
    return nullptr;
}
const Permanent* SnoutAa(const GameState& s)
{
    for (const Permanent& p : s.battlefield) { if (p.card.m_name.str() == "Trufflesnout") { return &p; } }
    return nullptr;
}
const Action* CastOfAa(const TurnSolver::Plan& p, const std::string& name)
{
    for (const Action& a : p.actions)
    { if (a.kind == Action::Kind::CastFromHand && a.card_name.str() == name) { return &a; } }
    return nullptr;
}
bool IsForestAa(const GameState& s, int num)
{
    const Permanent* p = ByNumAa(s, num);
    return p != nullptr && p->card.m_name.str() == "Forest";
}
}   // namespace

TEST_CASE("Utopia Sprawl: Enchant Forest -- the (sideboard) Island is never a host")
{
    GameState s = FreshAa();
    s.deck_has_land_aura = true;
    const int island = PutAa(s, "Island");
    const int forest = PutAa(s, "Forest");
    const CardParams& sp = DefAa("Utopia Sprawl").params;
    CHECK(sp.is_land_aura);
    CHECK(sp.etb_choose_color);
    CHECK(sp.land_aura_host_subtype == "Forest");
    // The enumerator's legal set.
    CHECK(LegalEnchantTargets(s, 0, sp) == std::vector<int>{ forest });
    // Resolution: a stale/illegal Island target falls back to the Forest; the fallback never picks it.
    CHECK(ResolveEnchantTarget(s, 0, island, /*land_aura=*/true, -1, &sp) == forest);
    CHECK(ResolveEnchantTarget(s, 0, 0, /*land_aura=*/true, -1, &sp) == forest);
    // The non-targeting attach (Bruna-style) honours it too.
    CHECK_FALSE(AuraCouldEnchant(s, sp, *ByNumAa(s, island)));
    CHECK(AuraCouldEnchant(s, sp, *ByNumAa(s, forest)));
    // Wild Growth ("Enchant land") is unaffected: both lands.
    CHECK(LegalEnchantTargets(s, 0, DefAa("Wild Growth").params).size() == 2);
    // With ONLY an Island out the card is uncastable (no plan casts it).
    GameState u = FreshAa();
    PutAa(u, "Island"); PutAa(u, "Island");
    HandAa(u, "Utopia Sprawl");
    for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(u, true))
    { CHECK(CastOfAa(p, "Utopia Sprawl") == nullptr); }
}

TEST_CASE("Utopia Sprawl: every colour variant is searched, only Forest hosts, executor == rollout")
{
    GameState s = FreshAa();
    s.deck_has_land_aura = true;
    PutAa(s, "Island");
    PutAa(s, "Forest"); PutAa(s, "Forest");
    HandAa(s, "Utopia Sprawl");
    std::set<int> colours;
    for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(s, true))
    {
        const Action* a = CastOfAa(p, "Utopia Sprawl");
        if (a == nullptr) { continue; }
        CHECK(IsForestAa(s, a->enchant_target));
        colours.insert(a->chosen_x);
        // The rollout resolves the pinned colour onto the Forest.
        GameState t = s;
        TurnSolver::ApplyPlan(t, p, true);
        const Permanent* sp = SprawlAa(t);
        REQUIRE(sp != nullptr);
        CHECK(sp->chosen_color == a->chosen_x - 1);
        CHECK(IsForestAa(t, sp->aura_attached_to));
    }
    // Generic provider: the whole five-colour menu (W U B R G -> 1..5), each its own plan.
    CHECK(colours == std::set<int>{ 1, 2, 3, 4, 5 });

    // The executor's stack entry (positive chosen_x recorded by CastSpellFromHand) locks the same colour.
    const CardDefinition& def = DefAa("Utopia Sprawl");
    for (int col : { static_cast<int>(Color::Blue), static_cast<int>(Color::Green) })
    {
        GameState ex = s;
        StackEntry e;
        e.type = StackEntry::EntryType::Spell;
        e.source = def.card; e.source.m_number = 4343;
        e.controller_index = 0;
        e.chosen_x = col + 1;
        int forest = 0;
        for (const Permanent& p : ex.battlefield) { if (p.card.m_name.str() == "Forest") { forest = p.card.m_number; break; } }
        e.enchant_target = forest;
        EffectHandler::Resolve(ex, e, def);
        const Permanent* sp = SprawlAa(ex);
        REQUIRE(sp != nullptr);
        CHECK(sp->chosen_color == col);
        CHECK(sp->aura_attached_to == forest);
    }
}

TEST_CASE("Utopia Sprawl: the locked colour is the ONLY colour credited (U pays {U}, G does not)")
{
    for (int col : { static_cast<int>(Color::Blue), static_cast<int>(Color::Green) })
    {
        GameState s = FreshAa();
        s.deck_has_land_aura = true;
        const int forest = PutAa(s, "Forest");
        const int sprawl = PutAa(s, "Utopia Sprawl");
        for (Permanent& p : s.battlefield)
        {
            if (p.card.m_number != sprawl) { continue; }
            p.aura_attached_to = forest;
            p.chosen_color = static_cast<int8_t>(col);
        }
        const Permanent& host = *ByNumAa(s, forest);
        ManaPool pool;
        LandAuraAddToPool(pool, s, host);
        CHECK(pool.Total() == 1);
        CHECK(pool.wild == 0);                               // typed, never wild
        CHECK(LandAuraColorMask(s, host) == (1 << col));
        ManaCost u; u.blue = 1;
        GameState t = s;
        CHECK(TapForCostShared(t, u, /*for_creature=*/false, nullptr, true) == (col == static_cast<int>(Color::Blue)));
        // {G}{U} off ONE Forest: only the U-locked Sprawl does it.
        ManaCost gu; gu.green = 1; gu.blue = 1;
        GameState w = s;
        CHECK(TapForCostShared(w, gu, false, nullptr, true) == (col == static_cast<int>(Color::Blue)));
    }
}

TEST_CASE("AccomplishedAlchemistProvider: Sprawl's colours restricted to G/U/W/B (searched, not picked)")
{
    EnsureCardsAa();
    GameState s = FreshAa();
    const std::vector<int> c = AccomplishedAlchemistProvider().EtbChoiceCandidates(s, 0, DefAa("Utopia Sprawl"));
    CHECK(std::set<int>(c.begin(), c.end())
          == std::set<int>{ static_cast<int>(Color::Green) + 1, static_cast<int>(Color::Blue) + 1,
                            static_cast<int>(Color::White) + 1, static_cast<int>(Color::Black) + 1 });
    // Trufflesnout's two modes are untouched by the restriction.
    CHECK(AccomplishedAlchemistProvider().EtbChoiceCandidates(s, 0, DefAa("Trufflesnout")) == std::vector<int>{ 1, 2 });
    // Elvish Harbinger's five Elf names fit the inherited tutor width.
    CHECK(AccomplishedAlchemistProvider().TutorSearchWidth() >= 5);
}

TEST_CASE("Trufflesnout: both modes are searched and resolve identically in executor and rollout")
{
    GameState s = FreshAa();
    for (int k = 0; k < 3; ++k) { PutAa(s, "Forest"); }
    HandAa(s, "Trufflesnout");
    std::set<int> modes;
    for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(s, true))
    {
        const Action* a = CastOfAa(p, "Trufflesnout");
        if (a == nullptr) { continue; }
        modes.insert(a->chosen_x);
        GameState t = s;
        TurnSolver::ApplyPlan(t, p, true);
        const Permanent* sn = SnoutAa(t);
        REQUIRE(sn != nullptr);
        if (a->chosen_x == 1) { CHECK(PlusCountersAa(*sn) == 1); CHECK(t.players[0].life == 20); }
        if (a->chosen_x == 2)
        {
            CHECK(PlusCountersAa(*sn) == 0);
            CHECK(t.players[0].life == 24);
            CHECK(t.players[0].life_gained_this_turn == 4);
        }
    }
    CHECK(modes == std::set<int>{ 1, 2 });

    const CardDefinition& def = DefAa("Trufflesnout");
    for (int mode : { 1, 2 })
    {
        GameState ex = s;
        StackEntry e;
        e.type = StackEntry::EntryType::Spell;
        e.source = def.card; e.source.m_number = 4444;
        e.controller_index = 0;
        e.chosen_x = mode;
        EffectHandler::Resolve(ex, e, def);
        const Permanent* sn = SnoutAa(ex);
        REQUIRE(sn != nullptr);
        CHECK(PlusCountersAa(*sn) == (mode == 1 ? 1 : 0));
        CHECK(ex.players[0].life == (mode == 2 ? 24 : 20));
        CHECK(ex.players[0].life_gained_this_turn == (mode == 2 ? 4 : 0));
    }
}

// ==== chunk C2a: the activated untappers + the lifegain breakpoint ================================
//  * Crab Umbra / Galvanic grant / Staff untap-target / Seedcradle Witch: Action::Kind::ActivateUntap,
//    ONE resolver (ApplyActivatedUntap) both worlds call; searched TARGET + searched COUNT.
//  * Staff of Domination: "{1}: Untap this artifact" interleaved by the resolver (untap-creature,
//    untap-self, untap-creature as ONE action) and ahead of each repeat of a {T} PermAbility mode.
//  * Galvanic Alchemist: the soulbond PARTNER is a searched ETB-choice axis (+ decline).
//  * Site 9's lifegain route: a tappable Alchemist's grown yield is a new key.
#include "mtg_test_seam.h"

namespace
{

void ResolveStackAa(GameState& st)
{
    while (!st.stack.empty())
    {
        const StackEntry e = st.stack.back();
        st.stack.pop_back();
        const CardDefinition* d = CardDatabase::Instance().LookupCached(e.source);
        if (d != nullptr) { EffectHandler::Resolve(st, e, *d); }
    }
}

Permanent* MutAa(GameState& s, int number)
{
    for (Permanent& p : s.battlefield) { if (p.card.m_number == number) { return &p; } }
    return nullptr;
}

int AttachAa(GameState& s, const std::string& aura, int host)
{
    const int n = PutAa(s, aura);
    MutAa(s, n)->aura_attached_to = host;
    return n;
}

int UntappedNamedAa(const GameState& s, const std::string& name)
{
    int n = 0;
    for (const Permanent& p : s.battlefield)
    { if (p.controller_index == 0 && !p.tapped && p.card.m_name.str() == name) { ++n; } }
    return n;
}

const Action* OnlyActionAa(const TurnSolver::Plan& p, Action::Kind k)
{
    const Action* hit = nullptr;
    for (const Action& a : p.actions)
    {
        if (a.kind == Action::Kind::PlayLand) { continue; }
        if (a.kind != k || hit != nullptr) { return nullptr; }
        hit = &a;
    }
    return hit;
}

}   // namespace

TEST_CASE("Crab Umbra on an Alchemist with 5 life gained: K activations net mana through the shared resolver")
{
    GameState s = FreshAa();
    const int alch = PutAa(s, "Accomplished Alchemist");
    for (int k = 0; k < 3; ++k) { PutAa(s, "Island"); }
    const int umbra = AttachAa(s, "Crab Umbra", alch);
    MutAa(s, alch)->tapped = true;
    s.players[0].life_gained_this_turn = 5;

    // Iteration 1 pays {2}{U} off the Islands and untaps it; iterations 2 and 3 tap it for 5 blue
    // first (the only reason an untap of an untapped creature is not a no-op), pay 3, untap it again.
    GameState t = s;
    CHECK(ApplyActivatedUntap(t, 0, kUntapEnchanted, umbra, alch, 3) == 3);
    CHECK_FALSE(ByNumAa(t, alch)->tapped);
    CHECK(UntappedNamedAa(t, "Island") == 0);
    CHECK(t.floating_mana.Total() == 4);          // 3 + 5 + 5 - 9: the loop NETS mana at X >= 4
    CHECK(t.floating_mana.blue == 4);             // one colour per Alchemist tap

    // At X = 2 an activation costs more than the untap returns: the second cannot be paid, so the
    // loop stops there -- nothing is paid for an activation that does not happen.
    GameState u = s;
    u.players[0].life_gained_this_turn = 2;
    CHECK(ApplyActivatedUntap(u, 0, kUntapEnchanted, umbra, alch, 3) == 1);

    // An untapped NON-mana target is a no-op: refused without paying.
    GameState v = s;
    const int galv = PutAa(v, "Galvanic Alchemist");
    MutAa(v, umbra)->aura_attached_to = galv;
    CHECK(ApplyActivatedUntap(v, 0, kUntapEnchanted, umbra, galv, 1) == 0);
    CHECK(UntappedNamedAa(v, "Island") == 3);
}

TEST_CASE("Crab Umbra: the search offers the untap with a searched count; executor == rollout")
{
    GameState s = FreshAa();
    const int alch = PutAa(s, "Accomplished Alchemist");
    for (int k = 0; k < 3; ++k) { PutAa(s, "Island"); }
    const int umbra = AttachAa(s, "Crab Umbra", alch);
    s.players[0].life_gained_this_turn = 5;
    std::set<int> ks;
    TurnSolver::Plan pick;
    bool found = false;
    for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(s, true))
    {
        const Action* a = OnlyActionAa(p, Action::Kind::ActivateUntap);
        if (a == nullptr) { continue; }
        CHECK(a->sac_source_id == umbra);
        CHECK(a->sac_victim_id == alch);
        CHECK(a->gy_exile_mode == kUntapEnchanted);
        ks.insert(a->chosen_x);
        if (a->chosen_x == 2) { pick = p; found = true; }
    }
    CHECK(ks == std::set<int>{ 1, 2 });     // affordable = (3 Islands + the Alchemist's 5) / 3
    REQUIRE(found);

    GameState ro = s;
    TurnSolver::ApplyPlan(ro, pick, true);
    AIEngine eng(MulliganProfile::DefaultProfile(), 3, 20);
    MtgTestSeam::CommitPhasePlan(eng, pick, true);
    GameState ex = s;
    eng.TakeTurn(ex, true, ResolveStackAa);
    for (const GameState* g : { &ro, &ex })
    {
        // The Alchemist started UNTAPPED, so each activation taps it for 5 blue first and pays out of
        // that: the Islands are never needed, and it ends untapped.
        CHECK_FALSE(ByNumAa(*g, alch)->tapped);
        CHECK(UntappedNamedAa(*g, "Island") == 3);
    }
    CHECK(ro.floating_mana.Total() == 4);   // 5 + 5 - 2 x 3
}

TEST_CASE("Staff of Domination: the untap TARGET is searched; dominated and opponent targets are not offered")
{
    GameState s = FreshAa();
    const int staff = PutAa(s, "Staff of Domination");
    for (int k = 0; k < 6; ++k) { PutAa(s, "Forest"); }
    const int alch  = PutAa(s, "Accomplished Alchemist");
    MutAa(s, alch)->tapped = true;
    const int elf   = PutAa(s, "Arbor Elf");                 // untapped, a live mana creature
    const int galv  = PutAa(s, "Galvanic Alchemist");         // untapped, no mana / {T} ability
    Permanent opp;
    opp.card = DefAa("Galvanic Alchemist").card; opp.card.m_number = g_aa_num++;
    opp.controller_index = 1; opp.owner_index = 1; opp.tapped = true;
    s.battlefield.push_back(opp);
    std::set<int> staff_targets;
    for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(s, true))
    {
        for (const Action& a : p.actions)
        {
            if (a.kind != Action::Kind::ActivateUntap || a.sac_source_id != staff) { continue; }
            CHECK(a.gy_exile_mode == kUntapStaffTarget);
            staff_targets.insert(a.sac_victim_id);
        }
    }
    CHECK(staff_targets == std::set<int>{ alch, elf });       // one variant per target, no auto-pick
    CHECK(staff_targets.count(galv) == 0);
    CHECK(staff_targets.count(opp.card.m_number) == 0);

    // Seedcradle Witch's pump is never a no-op, so every OWN creature is a target (itself included).
    GameState w = s;
    const int witch = PutAa(w, "Seedcradle Witch");
    for (int k = 0; k < 4; ++k) { PutAa(w, "Plains"); }
    std::set<int> witch_targets;
    for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(w, true))
    {
        for (const Action& a : p.actions)
        { if (a.kind == Action::Kind::ActivateUntap && a.sac_source_id == witch) { witch_targets.insert(a.sac_victim_id); } }
    }
    CHECK(witch_targets == std::set<int>{ alch, elf, galv, witch });
}

TEST_CASE("Staff of Domination: untap-creature, untap-self, untap-creature again is ONE action")
{
    GameState s = FreshAa();
    const int staff = PutAa(s, "Staff of Domination");
    for (int k = 0; k < 3; ++k) { PutAa(s, "Forest"); }
    const int alch = PutAa(s, "Accomplished Alchemist");
    MutAa(s, alch)->tapped = true;
    s.players[0].life_gained_this_turn = 6;

    // {3},{T} off the Forests; then the Alchemist taps for 6 green, {1} untaps the Staff and {3},{T}
    // untaps the Alchemist again: 2 left floating, Staff tapped, Alchemist untapped.
    GameState t = s;
    CHECK(ApplyActivatedUntap(t, 0, kUntapStaffTarget, staff, alch, 2) == 2);
    CHECK(ByNumAa(t, staff)->tapped);
    CHECK_FALSE(ByNumAa(t, alch)->tapped);
    CHECK(t.floating_mana.Total() == 2);

    // A TAPPED Staff pays its {1} first: {1}+{3} = 4 > 3 Forests -> unpayable, nothing spent, the
    // Staff stays tapped.
    GameState u = s;
    MutAa(u, staff)->tapped = true;
    CHECK(ApplyActivatedUntap(u, 0, kUntapStaffTarget, staff, alch, 1) == 0);
    CHECK(ByNumAa(u, staff)->tapped);
    CHECK(UntappedNamedAa(u, "Forest") == 3);
    PutAa(u, "Forest");
    CHECK(ApplyActivatedUntap(u, 0, kUntapStaffTarget, staff, alch, 1) == 1);
    CHECK(UntappedNamedAa(u, "Forest") == 0);
}

TEST_CASE("Staff of Domination: {2},{T}: gain 1 life repeats with the {1} self-untap; executor == rollout")
{
    GameState s = FreshAa();
    PutAa(s, "Staff of Domination");
    for (int k = 0; k < 5; ++k) { PutAa(s, "Forest"); }
    std::set<int> ks;
    TurnSolver::Plan pick;
    bool found = false;
    for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(s, true))
    {
        const Action* a = OnlyActionAa(p, Action::Kind::ActivatePermAbility);
        if (a == nullptr || a->ability_mode != Action::AbilityMode::TapLifegain) { continue; }
        ks.insert(a->chosen_x);
        if (a->chosen_x == 2) { pick = p; found = true; }
    }
    CHECK(ks == std::set<int>{ 1, 2 });     // 5 mana: {2} + ({1}+{2}) -- a third would need 8
    REQUIRE(found);
    GameState ro = s;
    TurnSolver::ApplyPlan(ro, pick, true);
    AIEngine eng(MulliganProfile::DefaultProfile(), 3, 20);
    MtgTestSeam::CommitPhasePlan(eng, pick, true);
    GameState ex = s;
    eng.TakeTurn(ex, true, ResolveStackAa);
    for (const GameState* g : { &ro, &ex })
    {
        CHECK(g->players[0].life == 22);
        CHECK(g->players[0].life_gained_this_turn == 2);   // two separate 1-life events
        CHECK(UntappedNamedAa(*g, "Forest") == 0);
        CHECK(UntappedNamedAa(*g, "Staff of Domination") == 0);
    }
}

TEST_CASE("Galvanic Alchemist: the soulbond partner is searched (+ decline); executor == rollout")
{
    GameState s = FreshAa();
    for (int k = 0; k < 3; ++k) { PutAa(s, "Island"); }
    const int alch = PutAa(s, "Accomplished Alchemist");
    const int elf  = PutAa(s, "Arbor Elf");
    HandAa(s, "Galvanic Alchemist");
    std::set<int> pins;
    TurnSolver::Plan to_alch;
    bool found = false;
    for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(s, true))
    {
        const Action* a = CastOfAa(p, "Galvanic Alchemist");
        if (a == nullptr) { continue; }
        pins.insert(a->chosen_x);
        GameState t = s;
        TurnSolver::ApplyPlan(t, p, true);
        const Permanent* g = nullptr;
        for (const Permanent& q : t.battlefield) { if (q.card.m_name.str() == "Galvanic Alchemist") { g = &q; } }
        REQUIRE(g != nullptr);
        if (a->chosen_x == kSoulbondDeclinePin) { CHECK(g->paired_with == 0); }
        else                                    { CHECK(g->paired_with == a->chosen_x); }
        if (a->chosen_x == alch)
        {
            to_alch = p; found = true;
            // Both halves of the pair now carry "{2}{U}: Untap this creature".
            std::vector<UntapAbilityRef> refs;
            CollectCreatureUntapAbilities(t, 0, *ByNumAa(t, alch), refs);
            bool self = false;
            for (const UntapAbilityRef& r : refs) { if (r.shape == kUntapSoulbondSelf && r.source_id == alch) { self = true; } }
            CHECK(self);
            CHECK(SoulbondGrantedUntapCost(t, *g).has_value());
        }
    }
    CHECK(pins == std::set<int>{ alch, elf, kSoulbondDeclinePin });
    REQUIRE(found);
    AIEngine eng(MulliganProfile::DefaultProfile(), 3, 20);
    MtgTestSeam::CommitPhasePlan(eng, to_alch, true);
    GameState ex = s;
    eng.TakeTurn(ex, true, ResolveStackAa);
    const Permanent* g = nullptr;
    for (const Permanent& q : ex.battlefield) { if (q.card.m_name.str() == "Galvanic Alchemist") { g = &q; } }
    REQUIRE(g != nullptr);
    CHECK(g->paired_with == alch);

    // Silverblade Paladin (soulbond WITHOUT an ability grant) is not fanned: its cast stays unpinned.
    GameState w = FreshAa();
    for (int k = 0; k < 3; ++k) { PutAa(w, "Plains"); }
    PutAa(w, "Arbor Elf");
    HandAa(w, "Silverblade Paladin");
    for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(w, true))
    {
        const Action* a = CastOfAa(p, "Silverblade Paladin");
        if (a != nullptr) { CHECK(a->chosen_x == 0); }
    }
}

TEST_CASE("Lifegain breakpoint: a tappable Alchemist's grown yield is a NEW site-9 key")
{
    GameState s = FreshAa();
    const int alch = PutAa(s, "Accomplished Alchemist");
    const std::vector<uint64_t> pre = TurnSolver::SnapshotActivatableAbilities(s);
    CHECK_FALSE(TurnSolver::PostEntryActivationPending(s, pre));
    GameState t = s;
    GainLife(t, 0, 6);
    CHECK(TurnSolver::PostEntryActivationPending(t, pre));          // yield 1 -> 6: re-plan
    GameState u = t;
    MutAa(u, alch)->tapped = true;
    CHECK_FALSE(TurnSolver::PostEntryActivationPending(u, pre));    // tapped: nothing new to spend
    // Gained BEFORE the plan started and nothing more since: the same key, no re-plan (a declined
    // tap is never re-opened).
    const std::vector<uint64_t> pre6 = TurnSolver::SnapshotActivatableAbilities(t);
    CHECK_FALSE(TurnSolver::PostEntryActivationPending(t, pre6));
    // A summoning-sick Alchemist cannot tap: no key.
    GameState v = FreshAa();
    PutAa(v, "Accomplished Alchemist", /*sick=*/true);
    const std::vector<uint64_t> prev = TurnSolver::SnapshotActivatableAbilities(v);
    GainLife(v, 0, 6);
    CHECK_FALSE(TurnSolver::PostEntryActivationPending(v, prev));
}

TEST_CASE("Accomplished Alchemist pays a TWO-colour cost only with a second source (single-colour burst)")
{
    // Seedcradle Witch's {2}{G}{W}: X mana of ONE colour cannot supply both {G} and {W}. The pool
    // credits the burst as wild (enumerate-optimistic); the exact payer refuses (apply-validates).
    GameState s = FreshAa();
    PutAa(s, "Accomplished Alchemist");
    s.players[0].life_gained_this_turn = 6;
    ManaCost c; c.generic = 2; c.green = 1; c.white = 1;
    GameState t = s;
    CHECK_FALSE(TapForCostDirect(t, c, false));
    PutAa(s, "Forest");
    GameState u = s;
    CHECK(TapForCostDirect(u, c, false));
}

// ==== Chunk C2b: the closed-form untap loop and its payoffs ==========================================
namespace
{
const Action* LoopActionAa(const TurnSolver::Plan& p)
{
    for (const Action& a : p.actions)
    { if (a.kind == Action::Kind::ActivateUntap && a.chosen_x >= kUntapLoopToNeed) { return &a; } }
    return nullptr;
}

ManaCost StrokeNeedAa(int x)
{
    ManaCost c; c.generic = x + 2; c.blue = 1; return c;
}

const AccomplishedAlchemistProvider& AaProv()
{
    static const AccomplishedAlchemistProvider p;
    return p;
}
}   // namespace

TEST_CASE("Untap loop: Crab Umbra on a 6-life Alchemist loops until the float pays Stroke's lethal X")
{
    GameState s = FreshAa();
    const int alch = PutAa(s, "Accomplished Alchemist");
    PutAa(s, "Island");
    for (int k = 0; k < 3; ++k) { PutAa(s, "Forest"); }
    const int umbra = AttachAa(s, "Crab Umbra", alch);
    s.players[0].life_gained_this_turn = 6;
    HandAa(s, "Stroke of Genius");
    DealOppAa(s, 40);
    s.m_provider = &AaProv();

    const UntapLoopNeed need = ComputeUntapLoopNeed(s, 0);
    CHECK(need.payoff == kLoopPayStroke);
    CHECK(need.count == 40);
    CHECK(need.cost.ManaValue() == 43);

    // The resolver: 6 - 3 = 3 net blue per iteration; it stops the moment the float pays {42}{U}.
    GameState t = s;
    const int n = ApplyUntapLoop(t, 0, kUntapEnchanted, umbra, alch);
    CHECK(n > 0);
    CHECK(t.floating_mana.CanPay(StrokeNeedAa(40)));
    CHECK(t.floating_mana.Total() < 43 + 6);          // EXACT need: stopped within one iteration of it
    CHECK_FALSE(ByNumAa(t, alch)->tapped);             // the loop leaves its creature untapped

    // The search is offered it as ONE more searched variant (the generic 1..3 are still there).
    std::set<int> ks;
    TurnSolver::Plan pick;
    bool offered = false;
    for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(s, true))
    {
        const Action* a = OnlyActionAa(p, Action::Kind::ActivateUntap);
        if (a == nullptr) { continue; }
        ks.insert(a->chosen_x);
        if (a->chosen_x == kUntapLoopToNeed) { offered = true; pick = p; }
    }
    CHECK(ks.count(1) == 1);
    REQUIRE(offered);

    // executor == rollout: the ONE dispatch (ApplyUntapAction) in both worlds, and the loop's continuation
    // (kActUntapLoopFunded, site 9 -- folded to the proven kill) casts Stroke X = 40 off the float in BOTH.
    bool both_win = false;
    for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(s, true))
    {
        const Action* a = OnlyActionAa(p, Action::Kind::ActivateUntap);
        if (a == nullptr || a->chosen_x != kUntapLoopToNeed) { continue; }
        GameState ro = s;
        TurnSolver::ApplyPlan(ro, p, true);
        AIEngine eng(MulliganProfile::DefaultProfile(), 3, 20);
        MtgTestSeam::CommitPhasePlan(eng, p, true);
        GameState ex = s;
        eng.TakeTurn(ex, true, ResolveStackAa);
        CHECK(OpponentHasLost(ro) == OpponentHasLost(ex));
        if (OpponentHasLost(ro) && OpponentHasLost(ex)) { both_win = true; }
    }
    CHECK(both_win);
    (void)pick;
}

namespace
{
// Apply `p` in BOTH worlds from `s`: the rollout (TurnSolver::ApplyPlan, continuations included) and the
// executor (AIEngine::TakeTurn on the committed plan).
std::pair<GameState, GameState> BothWorldsAa(const GameState& s, const TurnSolver::Plan& p)
{
    GameState ro = s;
    TurnSolver::ApplyPlan(ro, p, true);
    AIEngine eng(MulliganProfile::DefaultProfile(), 3, 20);
    MtgTestSeam::CommitPhasePlan(eng, p, true);
    GameState ex = s;
    eng.TakeTurn(ex, true, ResolveStackAa);
    return { ro, ex };
}

// Every plan whose only action is the closed-form loop: the base plan and its site-9 continuation
// variants (bp_choice >= 0 -- the wave's fan-out of what the loop's re-plan does with the float).
std::vector<TurnSolver::Plan> LoopPlansAa(const GameState& s)
{
    std::vector<TurnSolver::Plan> out;
    for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(s, true))
    {
        bool only = true; const Action* hit = nullptr;
        for (const Action& a : p.actions)
        {
            if (a.kind == Action::Kind::PlayLand) { continue; }
            if (a.kind == Action::Kind::ActivateUntap && a.chosen_x >= kUntapLoopToNeed && hit == nullptr) { hit = &a; }
            else { only = false; }
        }
        if (hit != nullptr && only) { out.push_back(p); }
    }
    return out;
}

bool FindLoopPlanAa(const GameState& s, TurnSolver::Plan& out)
{
    const std::vector<TurnSolver::Plan> v = LoopPlansAa(s);
    if (v.empty()) { return false; }
    out = v.front();
    return true;
}

// Does SOME loop plan (a continuation variant) satisfy `ok` in BOTH worlds? Returns it via `out`.
template <class F>
bool LoopPlanBothAa(const GameState& s, F ok, TurnSolver::Plan* out = nullptr)
{
    for (const TurnSolver::Plan& p : LoopPlansAa(s))
    {
        const auto both = BothWorldsAa(s, p);
        if (ok(both.first) && ok(both.second)) { if (out) { *out = p; } return true; }
    }
    return false;
}

int PowerAa(const GameState& s, int num) { return CombatPowerOf(*ByNumAa(s, num), s); }
}   // namespace

TEST_CASE("Untap loop thresholds: Umbra X = 3 is not a loop, X = 4 is; the Staff bootstrap's minimal P")
{
    // The pure search: Umbra/Galvanic ({2}{U}, no Staff to gain with) -- net X - 3.
    CHECK(UntapLoopBootOp(10, 3, false, false, false, 3, 0, false, 0) == -1);
    CHECK(UntapLoopBootOp(10, 4, false, false, false, 3, 0, false, 0) == 0);
    CHECK(UntapLoopBootOp(2, 4, false, true, false, 3, 0, false, 0) == -1);   // tapped, cannot pay the first untap
    CHECK(UntapLoopBootOp(3, 4, false, true, false, 3, 0, false, 0) == 3);    // ...three can: untap first
    // The Staff ({3},{T} + {1}: untap Staff) needs X >= 5; "{2},{T}: gain 1 life" buys X. Minimal other
    // mana P per starting X (docs/design/alchemist-untap-loop.md: 2, 5, 8 for X = 4, 3, 2).
    CHECK(UntapLoopBootOp(1, 4, false, false, true, 3, 1, true, 2) == -1);
    CHECK(UntapLoopBootOp(2, 4, false, false, true, 3, 1, true, 2) >= 1);
    CHECK(UntapLoopBootOp(4, 3, false, false, true, 3, 1, true, 2) == -1);
    CHECK(UntapLoopBootOp(5, 3, false, false, true, 3, 1, true, 2) >= 1);
    CHECK(UntapLoopBootOp(7, 2, false, false, true, 3, 1, true, 2) == -1);
    CHECK(UntapLoopBootOp(8, 2, false, false, true, 3, 1, true, 2) >= 1);
    CHECK(UntapLoopBootOp(0, 5, false, false, true, 3, 1, true, 2) == 0);     // X = 5 is already unbounded

    // ...and the provider does NOT offer a net-zero loop: Umbra at X = 3, no lifegain in hand.
    GameState s = FreshAa();
    const int alch = PutAa(s, "Accomplished Alchemist");
    PutAa(s, "Island");
    for (int k = 0; k < 3; ++k) { PutAa(s, "Forest"); }
    AttachAa(s, "Crab Umbra", alch);
    s.players[0].life_gained_this_turn = 3;
    HandAa(s, "Stroke of Genius");
    DealOppAa(s, 40);
    s.m_provider = &AaProv();
    TurnSolver::Plan none;
    CHECK_FALSE(FindLoopPlanAa(s, none));
    s.players[0].life_gained_this_turn = 4;
    CHECK(FindLoopPlanAa(s, none));
}

TEST_CASE("Untap loop: Staff of Domination bootstraps X with its own lifegain, then loops; executor == rollout")
{
    GameState s = FreshAa();
    const int staff = PutAa(s, "Staff of Domination");
    const int alch  = PutAa(s, "Accomplished Alchemist");
    for (int k = 0; k < 4; ++k) { PutAa(s, "Forest"); }
    PutAa(s, "Island");                                  // P = 5 other mana: exactly the X = 3 minimum
    s.players[0].life_gained_this_turn = 3;
    HandAa(s, "Stroke of Genius");
    DealOppAa(s, 12);
    s.m_provider = &AaProv();

    GameState t = s;
    CHECK(ApplyUntapLoop(t, 0, kUntapStaffTarget, staff, alch) > 0);
    INFO("float=", t.floating_mana.Total(), " U=", t.floating_mana.blue, " gained=", t.players[0].life_gained_this_turn,
         " staffT=", ByNumAa(t, staff)->tapped, " alchT=", ByNumAa(t, alch)->tapped);
    CHECK(t.players[0].life_gained_this_turn >= 5);     // two {2},{T}: gain 1 life bought X to 5
    CHECK(t.floating_mana.CanPay(StrokeNeedAa(12)));
    CHECK_FALSE(ByNumAa(t, alch)->tapped);
    CHECK_FALSE(ByNumAa(t, staff)->tapped);              // {1} out of the float leaves the Staff usable

    // A continuation variant of the loop plan casts Stroke X = 12 off the float, in both worlds.
    CHECK(LoopPlanBothAa(s, [](const GameState& g) { return OpponentHasLost(g); }));

    // One mana less and the bootstrap is unreachable: no loop variant.
    GameState u = FreshAa();
    PutAa(u, "Staff of Domination");
    PutAa(u, "Accomplished Alchemist");
    for (int k = 0; k < 3; ++k) { PutAa(u, "Forest"); }
    PutAa(u, "Island");
    u.players[0].life_gained_this_turn = 3;
    HandAa(u, "Stroke of Genius");
    DealOppAa(u, 12);
    u.m_provider = &AaProv();
    TurnSolver::Plan q;
    CHECK_FALSE(FindLoopPlanAa(u, q));
}

TEST_CASE("Untap loop: a Galvanic pair drains with Scholar of Athreos to lethal; black floated; executor == rollout")
{
    GameState s = FreshAa();
    const int alch = PutAa(s, "Accomplished Alchemist");
    const int galv = PutAa(s, "Galvanic Alchemist");
    MutAa(s, alch)->paired_with = galv;
    MutAa(s, galv)->paired_with = alch;
    PutAa(s, "Scholar of Athreos");
    PutAa(s, "Island");
    for (int k = 0; k < 2; ++k) { PutAa(s, "Forest"); }
    s.players[0].life_gained_this_turn = 6;
    s.players[1].life = 8;
    s.m_provider = &AaProv();

    const UntapLoopNeed need = ComputeUntapLoopNeed(s, 0);
    CHECK(need.payoff == kLoopPayDrain);
    CHECK(need.count == 8);
    GameState t = s;
    CHECK(ApplyUntapLoop(t, 0, kUntapSoulbondSelf, alch, alch) > 0);
    CHECK(t.floating_mana.black >= 8);                   // the colour plan floated the drains' {B}s

    // A continuation variant drains 8 times off the float, in both worlds.
    CHECK(LoopPlanBothAa(s, [](const GameState& g) { return OpponentHasLost(g); }));
}

TEST_CASE("Untap loop: Ezuri's main-phase pump sized to lethal off the float (mode 2 adds toughness too)")
{
    GameState s = FreshAa();
    const int alch  = PutAa(s, "Accomplished Alchemist");
    const int ezuri = PutAa(s, "Ezuri, Renegade Leader");
    AttachAa(s, "Crab Umbra", alch);
    PutAa(s, "Island");
    for (int k = 0; k < 2; ++k) { PutAa(s, "Forest"); }
    s.players[0].life_gained_this_turn = 7;
    s.m_provider = &AaProv();

    // Attackers: Alchemist 2 + Ezuri 2 = 4 of 20 -> 16 short, 2 Elves x 3 = 6 per pump -> 3 pumps.
    const UntapLoopNeed need = ComputeUntapLoopNeed(s, 0);
    CHECK(need.payoff == kLoopPayPump);
    CHECK(need.count == 3);

    // The float is what the main-phase hook reads (empty -> nothing; combat converter's job).
    CHECK(AaProv().MainPhaseTeamPumpCounts(s, *ByNumAa(s, ezuri), 10).empty());
    GameState t = s;
    t.floating_mana.green = 15;
    CHECK(AaProv().MainPhaseTeamPumpCounts(t, *ByNumAa(t, ezuri), 3) == std::vector<int>{ 3 });

    // A continuation variant pumps 3 times off the float, in both worlds; the loop leaves the Alchemist
    // untapped to attack.
    CHECK(LoopPlanBothAa(s, [&](const GameState& g)
    {
        return PowerAa(g, alch) == 2 + 9 && PowerAa(g, ezuri) == 2 + 9
            && ByNumAa(g, ezuri)->temp_tough_bonus == 9 && !ByNumAa(g, alch)->tapped;
    }));
}

TEST_CASE("Untap loop: Seedcradle Witch alternates G/W taps, and its own +3/+3 is the kill")
{
    GameState s = FreshAa();
    const int alch  = PutAa(s, "Accomplished Alchemist");
    const int witch = PutAa(s, "Seedcradle Witch");
    for (int k = 0; k < 4; ++k) { PutAa(s, "Forest"); }
    PutAa(s, "Plains");
    s.players[0].life_gained_this_turn = 6;
    s.m_provider = &AaProv();

    // No mana payoff anywhere: the Witch loop buys power. Attackers: Alchemist 2 (Witch is sick? no --
    // staged unsick) + Witch 1 = 3; 17 short at +3 per iteration -> 6 iterations.
    CHECK(UntapLoopPumpIterations(s, 0, kUntapPumpTarget, witch, alch) == 6);
    GameState t = s;
    CHECK(ApplyUntapLoop(t, 0, kUntapPumpTarget, witch, alch) == 6);
    CHECK(PowerAa(t, alch) == 2 + 18);
    CHECK_FALSE(ByNumAa(t, alch)->tapped);

    TurnSolver::Plan p;
    REQUIRE(FindLoopPlanAa(s, p));
    const auto [ro, ex] = BothWorldsAa(s, p);
    CHECK(PowerAa(ro, alch) == PowerAa(ex, alch));
    CHECK(PowerAa(ro, alch) >= 2 + 18);
}

TEST_CASE("Untap loop: the Staff draws TO FIND Stroke inside the loop, then the loop funds it")
{
    GameState s = FreshAa();
    s.players[0].library = Library{};
    for (int k = 0; k < 2; ++k) { LibAa(s, "Forest"); }
    LibAa(s, "Stroke of Genius");                        // depth 3
    for (int k = 0; k < 6; ++k) { LibAa(s, "Forest"); }
    const int alch  = PutAa(s, "Accomplished Alchemist");
    PutAa(s, "Staff of Domination");
    AttachAa(s, "Crab Umbra", alch);
    PutAa(s, "Island");
    for (int k = 0; k < 2; ++k) { PutAa(s, "Forest"); }
    s.players[0].life_gained_this_turn = 7;
    DealOppAa(s, 10);
    s.m_provider = &AaProv();

    const UntapLoopNeed need = ComputeUntapLoopNeed(s, 0);
    CHECK(need.payoff == kLoopPayDraw);
    CHECK(need.count == 3);
    // Drew 3 inside the loop; a continuation variant cast the drawn Stroke off the float -- both worlds.
    CHECK(LoopPlanBothAa(s, [](const GameState& g) { return OpponentHasLost(g) && g.players[0].library.size() == 6; }));
}

// ---- chunk P (2026-10-18): the two SOUND identity folds the branch census priced on this deck ----------------
//  * MTG_HAND_COPY_CLASS_FOLD -- identical hand copies of ANY card (not just creature Auras) are one odometer
//    class: the copies' option lists must match member for member, so "copy #1 X, copy #2 Y" and its mirror are
//    one position;
//  * MTG_LAND_AURA_HOST_CLASS_FOLD -- a land carrying only land Auras gets a (name, tapped, attachments) class
//    instead of its own slot, and the odometer keeps only positions whose used hosts are a PREFIX of their class.
// Each case compares the folded enumeration with the unfolded one as a set of plans CANONICALISED over the very
// symmetry the fold claims (which physical copy / which identical land), and shows the fold actually armed.
#include "ai/HeuristicArm.h"
namespace
{
// Both arms also keep every land-Aura host a DISTINCT plan in the dedup (MTG_EDF_AURA_HOST_SIG_KAROO=0, the
// unconditional host signature): the shipped autonomous dedup folds hosts by name, which would hide exactly the
// host axis these cases compare.
struct CopyFoldArmsAa
{
    std::int8_t ph, pl, pk;
    explicit CopyFoldArmsAa(bool on)
        : ph(heurarm::t_arm[heurarm::HAND_COPY_CLASS_FOLD]), pl(heurarm::t_arm[heurarm::LAND_AURA_HOST_CLASS_FOLD]),
          pk(heurarm::t_arm[heurarm::EDF_AURA_HOST_SIG_KAROO])
    {
        heurarm::t_arm[heurarm::HAND_COPY_CLASS_FOLD]      = on ? 1 : 0;
        heurarm::t_arm[heurarm::LAND_AURA_HOST_CLASS_FOLD] = on ? 1 : 0;
        heurarm::t_arm[heurarm::EDF_AURA_HOST_SIG_KAROO]   = 0;
    }
    ~CopyFoldArmsAa()
    {
        heurarm::t_arm[heurarm::HAND_COPY_CLASS_FOLD]      = ph;
        heurarm::t_arm[heurarm::LAND_AURA_HOST_CLASS_FOLD] = pl;
        heurarm::t_arm[heurarm::EDF_AURA_HOST_SIG_KAROO]   = pk;
    }
};

// A plan as the multiset of its casts, with every host relabelled by its CLASS label (`cls` maps a host number
// to a class string; a host not in the map keeps its number) and the class members renamed to the
// lexicographically least assignment -- i.e. the plan modulo "which identical land".
std::string CanonPlanAa(const TurnSolver::Plan& p, const std::map<int, std::string>& cls)
{
    std::vector<int> hosts;
    for (const Action& a : p.actions)
    {
        for (int n : { a.enchant_target, a.sac_source_id })
        {
            if (n > 0 && cls.count(n) && std::find(hosts.begin(), hosts.end(), n) == hosts.end()) { hosts.push_back(n); }
        }
    }
    std::sort(hosts.begin(), hosts.end());
    std::string best;
    bool first = true;
    do
    {
        std::map<int, std::string> lab;
        std::map<std::string, int> next;
        for (int h : hosts) { const std::string& c = cls.at(h); lab[h] = c + "#" + std::to_string(next[c]++); }
        std::vector<std::string> parts;
        for (const Action& a : p.actions)
        {
            std::string k = std::to_string(static_cast<int>(a.kind)) + ":" + a.card_name.str()
                          + ":x" + std::to_string(a.chosen_x) + ":d" + std::to_string(a.draw_target);
            if (a.enchant_target > 0)
            { k += ":h" + (lab.count(a.enchant_target) ? lab[a.enchant_target] : std::to_string(a.enchant_target)); }
            if (a.sac_source_id > 0)
            { k += ":s" + (lab.count(a.sac_source_id) ? lab[a.sac_source_id] : std::to_string(a.sac_source_id)); }
            k += ":v" + std::to_string(a.sac_victim_id) + ":m" + std::to_string(static_cast<int>(a.ability_mode));
            parts.push_back(k);
        }
        std::sort(parts.begin(), parts.end());
        std::string s;
        for (const std::string& x : parts) { s += x + ";"; }
        if (first || s < best) { best = s; first = false; }
    } while (std::next_permutation(hosts.begin(), hosts.end()));
    return best;
}

std::set<std::string> CanonPlansAa(const std::vector<TurnSolver::Plan>& plans, const std::map<int, std::string>& cls)
{
    std::set<std::string> out;
    for (const TurnSolver::Plan& p : plans) { out.insert(CanonPlanAa(p, cls)); }
    return out;
}
}   // namespace

TEST_CASE("Hand-copy class fold: two Stroke of Genius (a searched (target, X) axis) -- the same plans, folded or not")
{
    GameState s = FreshAa();
    for (int k = 0; k < 6; ++k) { PutAa(s, "Island"); }
    for (int k = 0; k < 4; ++k) { PutAa(s, "Forest"); }
    HandAa(s, "Stroke of Genius");
    HandAa(s, "Stroke of Genius");
    HandAa(s, "Nourish");
    HandAa(s, "Nourish");
    DealOppAa(s, 40);
    s.m_provider = &AaProv();
    const std::map<int, std::string> no_cls;
    std::vector<TurnSolver::Plan> on, off;
    long long built = 0;
    {
        CopyFoldArmsAa a(true);
        const long long b0 = CopyHostFoldTestCount(0);
        on = TurnSolver::EnumerateMainPlans(s, true);
        built = CopyHostFoldTestCount(0) - b0;
    }
    { CopyFoldArmsAa a(false); off = TurnSolver::EnumerateMainPlans(s, true); }
    CHECK(built > 0);                                   // the Stroke class (and the Nourish class) armed
    CHECK(on.size() > 1);
    CHECK(CanonPlansAa(on, no_cls) == CanonPlansAa(off, no_cls));
    // A copy another candidate NAMES by number is refused: Galvanic Alchemist's soulbond partner pin names a
    // creature card in HAND, so two Scholars in hand with a Galvanic stay two groups -- and still the same plans.
    GameState g = FreshAa();
    for (int k = 0; k < 8; ++k) { PutAa(g, "Island"); }
    for (int k = 0; k < 4; ++k) { PutAa(g, "Plains"); }
    HandAa(g, "Galvanic Alchemist");
    HandAa(g, "Scholar of Athreos");
    HandAa(g, "Scholar of Athreos");
    g.m_provider = &AaProv();
    std::vector<TurnSolver::Plan> gon, goff;
    { CopyFoldArmsAa a(true);  gon  = TurnSolver::EnumerateMainPlans(g, true); }
    { CopyFoldArmsAa a(false); goff = TurnSolver::EnumerateMainPlans(g, true); }
    CHECK(CanonPlansAa(gon, no_cls) == CanonPlansAa(goff, no_cls));
}

TEST_CASE("Land-Aura host prefix: Sprawl + Wild Growth over four bare Forests -- stack AND spread survive, mirrors do not")
{
    GameState s = FreshAa();
    s.deck_has_land_aura = true;
    std::map<int, std::string> cls;
    for (int k = 0; k < 4; ++k) { cls[PutAa(s, "Forest")] = "F"; }
    HandAa(s, "Utopia Sprawl");
    HandAa(s, "Wild Growth");
    s.m_provider = &AaProv();
    std::vector<TurnSolver::Plan> on, off;
    long long armed = 0, rejected = 0;
    {
        CopyFoldArmsAa a(true);
        const long long f0 = CopyHostFoldTestCount(1), r0 = CopyHostFoldTestCount(2);
        on = TurnSolver::EnumerateMainPlans(s, true);
        armed = CopyHostFoldTestCount(1) - f0;
        rejected = CopyHostFoldTestCount(2) - r0;
    }
    { CopyFoldArmsAa a(false); off = TurnSolver::EnumerateMainPlans(s, true); }
    CHECK(armed > 0);
    CHECK(rejected > 0);
    CHECK(on.size() < off.size());                      // "Sprawl on Forest #2 alone" etc. are gone...
    CHECK(CanonPlansAa(on, cls) == CanonPlansAa(off, cls));   // ...and every distinct line is still there
    // Both the STACK and the SPREAD of the two Auras are offered (the fold never collapses one onto the other).
    bool stacked = false, spread = false;
    for (const TurnSolver::Plan& p : on)
    {
        const Action* sp = CastOfAa(p, "Utopia Sprawl");
        const Action* wg = CastOfAa(p, "Wild Growth");
        if (sp == nullptr || wg == nullptr) { continue; }
        if (sp->enchant_target == wg->enchant_target) { stacked = true; } else { spread = true; }
    }
    CHECK(stacked);
    CHECK(spread);
}

TEST_CASE("Land-Aura host class: identically-decorated Forests are one class; a differently-locked colour is not")
{
    auto board = [](int colour_a, int colour_b, std::map<int, std::string>& cls, std::vector<int>& deco)
    {
        GameState s = FreshAa();
        s.deck_has_land_aura = true;
        const int fa = PutAa(s, "Forest");
        const int fb = PutAa(s, "Forest");
        cls[PutAa(s, "Forest")] = "bare";
        MutAa(s, AttachAa(s, "Utopia Sprawl", fa))->chosen_color = static_cast<int8_t>(colour_a);
        MutAa(s, AttachAa(s, "Utopia Sprawl", fb))->chosen_color = static_cast<int8_t>(colour_b);
        cls[fa] = "deco" + std::to_string(colour_a);
        cls[fb] = "deco" + std::to_string(colour_b);
        deco = { fa, fb };
        HandAa(s, "Wild Growth");
        s.m_provider = &AaProv();
        return s;
    };
    auto hosts_of = [](const std::vector<TurnSolver::Plan>& plans)
    {
        std::set<int> h;
        for (const TurnSolver::Plan& p : plans)
        { if (const Action* a = CastOfAa(p, "Wild Growth")) { h.insert(a->enchant_target); } }
        return h;
    };
    const int blue = static_cast<int>(Color::Blue), green = static_cast<int>(Color::Green);
    {   // two blue-locked Sprawls: one decorated class + the bare Forest = two hosts (three unfolded)
        std::map<int, std::string> cls; std::vector<int> deco;
        const GameState s = board(blue, blue, cls, deco);
        std::vector<TurnSolver::Plan> on, off;
        { CopyFoldArmsAa a(true);  on  = TurnSolver::EnumerateMainPlans(s, true); }
        { CopyFoldArmsAa a(false); off = TurnSolver::EnumerateMainPlans(s, true); }
        CHECK(hosts_of(on).size() == 2);
        CHECK(hosts_of(off).size() == 3);
        CHECK(CanonPlansAa(on, cls) == CanonPlansAa(off, cls));
    }
    {   // a blue and a green Sprawl are different hosts: nothing folds between them
        std::map<int, std::string> cls; std::vector<int> deco;
        const GameState s = board(blue, green, cls, deco);
        std::vector<TurnSolver::Plan> on;
        { CopyFoldArmsAa a(true); on = TurnSolver::EnumerateMainPlans(s, true); }
        const std::set<int> h = hosts_of(on);
        CHECK(h.size() == 3);
        CHECK(h.count(deco[0]) == 1);
        CHECK(h.count(deco[1]) == 1);
    }
}

TEST_CASE("Activation-source copy classes: two identical Staffs of Domination -- the same plans, folded or not")
{
    GameState s = FreshAa();
    std::map<int, std::string> cls;
    cls[PutAa(s, "Staff of Domination")] = "staff";
    cls[PutAa(s, "Staff of Domination")] = "staff";
    PutAa(s, "Accomplished Alchemist");
    for (int k = 0; k < 7; ++k) { PutAa(s, "Forest"); }
    s.m_provider = &AaProv();
    std::vector<TurnSolver::Plan> on, off;
    long long built = 0;
    {
        CopyFoldArmsAa a(true);
        const long long b0 = CopyHostFoldTestCount(3);
        on = TurnSolver::EnumerateMainPlans(s, true);
        built = CopyHostFoldTestCount(3) - b0;
    }
    { CopyFoldArmsAa a(false); off = TurnSolver::EnumerateMainPlans(s, true); }
    CHECK(built > 0);
    CHECK(on.size() > 1);
    CHECK(CanonPlansAa(on, cls) == CanonPlansAa(off, cls));
    // The greedy pick (the rollout leaf's walk, where the class matters most) is the same line up to which Staff.
    TurnSolver::Plan gon, goff;
    { CopyFoldArmsAa a(true);  gon  = TurnSolver::Solve(s, true, TurnSolver::GreedyPermit(TurnSolver::GreedySite::HorizonLeaf, 0)); }
    { CopyFoldArmsAa a(false); goff = TurnSolver::Solve(s, true, TurnSolver::GreedyPermit(TurnSolver::GreedySite::HorizonLeaf, 0)); }
    CHECK(CanonPlanAa(gon, cls) == CanonPlanAa(goff, cls));
}

// ---- Arbor Elf: "{T}: Untap target Forest" re-taps the BEST Forest, land-Aura bonuses included ---------
namespace
{
// Attach land Aura `aura` to `host`; `col` >= 0 locks a Utopia Sprawl's colour.
void AttachAa(GameState& s, int aura, int host, int col = -1)
{
    for (Permanent& p : s.battlefield)
    {
        if (p.card.m_number != aura) { continue; }
        p.aura_attached_to = host;
        if (col >= 0) { p.chosen_color = static_cast<int8_t>(col); }
    }
}
void TapAa(GameState& s, int num) { for (Permanent& p : s.battlefield) { if (p.card.m_number == num) { p.tapped = true; } } }
}   // namespace

TEST_CASE("Arbor Elf: a plain Forest -- the re-tap is {G}, exactly as before")
{
    GameState s = FreshAa();
    s.deck_has_land_aura = true;
    const int f = PutAa(s, "Forest");
    TapAa(s, f);
    const int arbor = PutAa(s, "Arbor Elf");
    const Permanent& a = *ByNumAa(s, arbor);
    CHECK(LandAuraBonus(s, a) == 0);
    CHECK(PermanentManaYield(s, a, DefAa("Arbor Elf")) == 1);
    CHECK(AvailableManaPool(s).Total() == 1);
    ManaCost gg; gg.green = 2;
    GameState t = s;
    CHECK_FALSE(TapForCostShared(t, gg, false, nullptr, true));
    ManaCost g; g.green = 1;
    GameState w = s;
    CHECK(TapForCostShared(w, g, false, nullptr, true));
    // No Forest at all: still a dead source.
    GameState n = FreshAa();
    n.deck_has_land_aura = true;
    const int arb2 = PutAa(n, "Arbor Elf");
    CHECK(PermanentManaYield(n, *ByNumAa(n, arb2), DefAa("Arbor Elf")) == 0);
}

TEST_CASE("Arbor Elf + Wild Growth Forest: 4 mana on the T2 board; the Alchemist is enumerated and paid in both worlds")
{
    // T2 of seed 80020: Forest A enchanted with Wild Growth (cast off Forest B, now tapped), Arbor Elf
    // from T1. Forest A {G}{G} + Arbor re-tapping A {G}{G} = 4 -> Accomplished Alchemist {3}{G}.
    GameState s = FreshAa();
    s.deck_has_land_aura = true;
    const int fa = PutAa(s, "Forest");
    const int fb = PutAa(s, "Forest");
    TapAa(s, fb);
    const int wg = PutAa(s, "Wild Growth");
    AttachAa(s, wg, fa);
    const int arbor = PutAa(s, "Arbor Elf");
    HandAa(s, "Accomplished Alchemist");
    s.m_provider = &AaProv();
    const Permanent& a = *ByNumAa(s, arbor);
    CHECK(LandAuraBonus(s, a) == 1);
    CHECK(PermanentManaYield(s, a, DefAa("Arbor Elf")) == 2);
    const ManaPool pool = AvailableManaPool(s);
    CHECK(pool.Total() == 4);
    CHECK(pool.green == 4);
    ManaCost c; c.generic = 3; c.green = 1;   // Accomplished Alchemist {3}{G}
    { GameState t = s; CHECK(TapForCostShared(t, c, true, nullptr, true)); }
    { GameState t = s; CHECK(TapForCostDirect(t, c, true)); }
    { GameState t = s; CHECK(TapForCostBacktrack(t, c, true, ManaPool{})); }
    // Five is one too many.
    ManaCost c5; c5.generic = 4; c5.green = 1;
    { GameState t = s; CHECK_FALSE(TapForCostShared(t, c5, true, nullptr, true)); }
    // The search offers the cast, and both worlds resolve it.
    bool offered = false, both = false;
    for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(s, true))
    {
        if (CastOfAa(p, "Accomplished Alchemist") == nullptr) { continue; }
        offered = true;
        const auto w = BothWorldsAa(s, p);
        auto has = [](const GameState& g)
        { for (const Permanent& q : g.battlefield) { if (q.card.m_name.str() == "Accomplished Alchemist") { return true; } } return false; };
        if (has(w.first) && has(w.second)) { both = true; break; }
    }
    CHECK(offered);
    CHECK(both);
    // TWO Arbor Elves may both re-tap the same Wild Growth Forest: +{G}{G} each.
    GameState d = s;
    const int arbor2 = PutAa(d, "Arbor Elf");
    CHECK(PermanentManaYield(d, *ByNumAa(d, arbor2), DefAa("Arbor Elf")) == 2);
    CHECK(AvailableManaPool(d).Total() == 6);
    ManaCost c6; c6.generic = 5; c6.green = 1;
    { GameState t = d; CHECK(TapForCostShared(t, c6, true, nullptr, true)); }
}

TEST_CASE("Arbor Elf + Utopia Sprawl (blue) Forest: the re-tap is {G}{U} and pays a {U} pip")
{
    GameState s = FreshAa();
    s.deck_has_land_aura = true;
    const int f = PutAa(s, "Forest");
    TapAa(s, f);                                   // the Forest already tapped once
    const int sp = PutAa(s, "Utopia Sprawl");
    AttachAa(s, sp, f, static_cast<int>(Color::Blue));
    const int arbor = PutAa(s, "Arbor Elf");
    const Permanent& a = *ByNumAa(s, arbor);
    CHECK(LandAuraColorMask(s, a) == (1 << static_cast<int>(Color::Blue)));
    ManaPool bonus;
    LandAuraAddToPool(bonus, s, a);
    CHECK(bonus.blue == 1);
    CHECK(bonus.Total() == 1);
    const ManaPool pool = AvailableManaPool(s);
    CHECK(pool.green == 1);
    CHECK(pool.blue == 1);
    ManaCost gu; gu.green = 1; gu.blue = 1;
    { GameState t = s; CHECK(TapForCostShared(t, gu, false, nullptr, true)); }
    { GameState t = s; CHECK(TapForCostBacktrack(t, gu, false, ManaPool{})); }
    ManaCost uu; uu.blue = 2;
    { GameState t = s; CHECK_FALSE(TapForCostShared(t, uu, false, nullptr, true)); }
}

TEST_CASE("Arbor Elf facing incomparable Forests ({U} Sprawl vs {G} Wild Growth): the payer picks the Forest")
{
    GameState s = FreshAa();
    s.deck_has_land_aura = true;
    const int fu = PutAa(s, "Forest");
    const int fg = PutAa(s, "Forest");
    const int fp = PutAa(s, "Forest");            // plain: dominated by both, never a class
    TapAa(s, fu); TapAa(s, fg); TapAa(s, fp);
    const int sp = PutAa(s, "Utopia Sprawl");
    AttachAa(s, sp, fu, static_cast<int>(Color::Blue));
    const int wg = PutAa(s, "Wild Growth");
    AttachAa(s, wg, fg);
    const int arbor = PutAa(s, "Arbor Elf");
    const Permanent& a = *ByNumAa(s, arbor);
    CHECK(UntapLandHostCount(s, a) == 2);
    CHECK(LandAuraBonus(s, a) == 1);
    CHECK(LandAuraColorMask(s, a)
          == ((1 << static_cast<int>(Color::Blue)) | (1 << static_cast<int>(Color::Green))));
    // Projection: the over-approximation (one wild beside the {G}).
    const ManaPool pool = AvailableManaPool(s);
    CHECK(pool.Total() == 2);
    // Either class pays its own cost (the greedy's class 0 is the Sprawl Forest; {G}{G} needs class 1,
    // reached by the backtracker's per-class branch).
    ManaCost gu; gu.green = 1; gu.blue = 1;
    ManaCost gg; gg.green = 2;
    ManaCost uu; uu.blue = 2;
    { GameState t = s; CHECK(TapForCostShared(t, gu, false, nullptr, true)); }
    { GameState t = s; CHECK(TapForCostShared(t, gg, false, nullptr, true)); }
    { GameState t = s; CHECK(TapForCostBacktrack(t, gg, false, ManaPool{})); }
    { GameState t = s; CHECK_FALSE(TapForCostShared(t, uu, false, nullptr, true)); }
    // A Forest whose bonus is a superset ({U} + {G}) dominates both: one class again.
    GameState d = s;
    const int wg2 = PutAa(d, "Wild Growth");
    AttachAa(d, wg2, fu);
    CHECK(UntapLandHostCount(d, *ByNumAa(d, arbor)) == 1);
    CHECK(LandAuraBonus(d, *ByNumAa(d, arbor)) == 2);
}

// ---- chunk E1 (2026-10-19): the claude-play sweep's fix list ----------------------------------------------------
//  * CHAINED loop payoffs: a drain / team-pump creature cast from HAND (or a drain creature WISHED from the
//    sideboard) is a this-turn kill once site 9 RE-OPENS for the ability that cast made activatable;
//  * the shortest chain is preferred (a one-step kill wins the need);
//  * two Living Wishes never name the one sideboard copy twice;
//  * the Staff's {5},{T}: draw is offered standalone;
//  * "non-lethal opponent Stroke + the loop" in one plan is dominated and dropped;
//  * an unreachable loop does nothing at all (scored identical to the plan without it).
namespace
{
void SideAa(GameState& s, const std::string& name)
{
    Card c = DefAa(name).card; c.m_number = g_aa_num++; s.players[0].sideboard.push_back(c);
}
int CountKindAa(const TurnSolver::Plan& p, Action::Kind k)
{
    int n = 0;
    for (const Action& a : p.actions) { if (a.kind == k) { ++n; } }
    return n;
}
}   // namespace

TEST_CASE("Chained loop payoff: Scholar of Athreos from HAND -- loop, cast it, the re-opened site 9 drains")
{
    GameState s = FreshAa();
    const int alch = PutAa(s, "Accomplished Alchemist");
    const int umbra = AttachAa(s, "Crab Umbra", alch);
    PutAa(s, "Island");
    for (int k = 0; k < 2; ++k) { PutAa(s, "Forest"); }
    s.players[0].life_gained_this_turn = 6;
    s.players[1].life = 6;
    HandAa(s, "Scholar of Athreos");
    s.m_provider = &AaProv();

    const UntapLoopNeed need = ComputeUntapLoopNeed(s, 0);
    CHECK(need.payoff == kLoopPayCastDrain);
    CHECK(need.count == 6);
    CHECK(need.n_pre == 1);
    CHECK(need.cost.ManaValue() == 3 + 6 * 3);
    GameState t = s;
    CHECK(ApplyUntapLoop(t, 0, kUntapEnchanted, umbra, alch) > 0);
    CHECK(UntapLoopFloatPays(t.floating_mana, need));
    CHECK(t.floating_mana.white >= 1);
    CHECK(t.floating_mana.black >= 6);

    // Some loop plan (its uniform continuation variant: cast Scholar at occurrence 0, drain at the
    // re-opened occurrence 1) wins THIS turn in both worlds.
    CHECK(LoopPlanBothAa(s, [](const GameState& g) { return OpponentHasLost(g); }));
}

TEST_CASE("Chained loop payoff: Living Wish for the sideboard Scholar, cast it, drain -- both worlds")
{
    GameState s = FreshAa();
    const int alch = PutAa(s, "Accomplished Alchemist");
    AttachAa(s, "Crab Umbra", alch);
    PutAa(s, "Island");
    for (int k = 0; k < 2; ++k) { PutAa(s, "Forest"); }
    s.players[0].life_gained_this_turn = 6;
    s.players[1].life = 5;
    HandAa(s, "Living Wish");
    SideAa(s, "Scholar of Athreos");
    SideAa(s, "Brindle Boar");
    s.m_provider = &AaProv();

    const UntapLoopNeed need = ComputeUntapLoopNeed(s, 0);
    CHECK(need.payoff == kLoopPayWishDrain);
    CHECK(need.card == InternedName("Scholar of Athreos"));
    CHECK(need.n_pre == 2);
    CHECK(LoopPlanBothAa(s, [](const GameState& g) { return OpponentHasLost(g); }));
}

TEST_CASE("Chained loop payoff: Ezuri from HAND is cast off the float and pumps the team in the same main")
{
    GameState s = FreshAa();
    const int alch = PutAa(s, "Accomplished Alchemist");
    AttachAa(s, "Crab Umbra", alch);
    PutAa(s, "Elvish Harbinger");
    PutAa(s, "Island");
    for (int k = 0; k < 2; ++k) { PutAa(s, "Forest"); }
    s.players[0].life_gained_this_turn = 7;
    HandAa(s, "Ezuri, Renegade Leader");
    s.m_provider = &AaProv();

    // Attackers: Alchemist 2 + Harbinger 1 = 3 of 20; 17 short at 2 Elves x 3 = 6 per pump -> 3 pumps.
    const UntapLoopNeed need = ComputeUntapLoopNeed(s, 0);
    CHECK(need.payoff == kLoopPayCastPump);
    CHECK(need.count == 3);
    // Both worlds end the main with Ezuri out and the attackers pumped to lethal (+9/+9 each).
    CHECK(LoopPlanBothAa(s, [&](const GameState& g)
    {
        bool ezuri = false;
        for (const Permanent& p : g.battlefield) { if (p.card.m_name.str() == "Ezuri, Renegade Leader") { ezuri = true; } }
        return ezuri && PowerAa(g, alch) == 2 + 9 && !ByNumAa(g, alch)->tapped;
    }));
}

TEST_CASE("Chained loop payoffs yield to a one-step kill: Stroke in hand wins the need over Scholar")
{
    GameState s = FreshAa();
    const int alch = PutAa(s, "Accomplished Alchemist");
    AttachAa(s, "Crab Umbra", alch);
    PutAa(s, "Island");
    for (int k = 0; k < 2; ++k) { PutAa(s, "Forest"); }
    s.players[0].life_gained_this_turn = 6;
    s.players[1].life = 2;                       // the chained drain would be CHEAPER (3 + 2x3) ...
    HandAa(s, "Scholar of Athreos");
    HandAa(s, "Stroke of Genius");
    DealOppAa(s, 40);                            // ... than Stroke X = 40, and Stroke still wins the need
    s.m_provider = &AaProv();
    CHECK(ComputeUntapLoopNeed(s, 0).payoff == kLoopPayStroke);
}

TEST_CASE("Living Wish singletons: two wishes in one plan fetch two DIFFERENT sideboard cards (both worlds)")
{
    // Autonomous play binds no name at collection (MTG_TUTOR_AXIS_RESOLVE): each wish ranks the LIVE
    // sideboard at its own resolution, so the second wish cannot re-name the singleton the first took.
    // (Human play names the targets on the menu; the AppendSubdecisionAxes singleton pass drops a plan
    // naming one sideboard copy twice -- MTG_HUMAN_PLAY is process-static, so that half is covered by
    // the code path, not this case.)
    GameState s = FreshAa();
    for (int k = 0; k < 6; ++k) { PutAa(s, "Forest"); }
    HandAa(s, "Living Wish");
    HandAa(s, "Living Wish");
    SideAa(s, "Accomplished Alchemist");
    SideAa(s, "Scholar of Athreos");
    SideAa(s, "Forest");
    s.m_provider = &AaProv();
    int checked = 0;
    for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(s, true))
    {
        int wishes = 0;
        for (const Action& a : p.actions)
        {
            if (a.kind != Action::Kind::CastFromHand) { continue; }
            if (a.card_name.str() != "Living Wish") { wishes = -99; break; }
            ++wishes;
        }
        if (wishes != 2 || p.bp_choice >= 0) { continue; }
        const auto [ro, ex] = BothWorldsAa(s, p);
        for (const GameState* g : { &ro, &ex })
        {
            std::set<std::string> got;
            for (const Card& c : g->players[0].hand) { got.insert(c.m_name.str()); }
            CHECK(g->players[0].sideboard.size() == 1);
            CHECK(got.size() == 2);   // two distinct fetched names
        }
        if (++checked >= 3) { break; }
    }
    CHECK(checked > 0);
}

TEST_CASE("Staff of Domination: {5},{T}: draw is offered standalone (no loop needed)")
{
    GameState s = FreshAa();
    PutAa(s, "Staff of Domination");
    for (int k = 0; k < 5; ++k) { PutAa(s, "Forest"); }
    s.m_provider = &AaProv();
    bool standalone = false;
    for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(s, true))
    {
        const Action* a = OnlyActionAa(p, Action::Kind::ActivatePermAbility);
        if (a && a->ability_mode == Action::AbilityMode::TapDraw) { standalone = true; }
    }
    CHECK(standalone);
}

TEST_CASE("Non-lethal opponent Stroke + the loop in one plan is dominated and dropped (the loop-only and Stroke-only plans stay)")
{
    GameState s = FreshAa();
    const int alch = PutAa(s, "Accomplished Alchemist");
    AttachAa(s, "Crab Umbra", alch);
    PutAa(s, "Island");
    for (int k = 0; k < 3; ++k) { PutAa(s, "Forest"); }
    s.players[0].life_gained_this_turn = 6;
    HandAa(s, "Stroke of Genius");
    DealOppAa(s, 40);
    s.m_provider = &AaProv();
    bool loop_only = false, small_only = false, combined = false;
    const int lx = opponentdeck::ForcedDrawLethalX(s);
    for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(s, true))
    {
        bool loop = LoopActionAa(p) != nullptr, small = false;
        for (const Action& a : p.actions)
        { if (a.kind == Action::Kind::CastFromHand && a.draw_target == 1 && a.chosen_x < lx) { small = true; } }
        if (loop && small) { combined = true; }
        if (loop && !small && CountKindAa(p, Action::Kind::CastFromHand) == 0) { loop_only = true; }
        if (small && !loop) { small_only = true; }
    }
    CHECK_FALSE(combined);
    CHECK(loop_only);
    CHECK(small_only);
}

TEST_CASE("An unreachable loop does nothing: no payment, no tap, no float (scored identical to the plan without it)")
{
    GameState s = FreshAa();
    const int alch = PutAa(s, "Accomplished Alchemist");
    MutAa(s, alch)->tapped = true;               // tapped, and nothing to pay the first {2}{U} untap with
    AttachAa(s, "Crab Umbra", alch);
    s.players[0].life_gained_this_turn = 6;
    HandAa(s, "Stroke of Genius");
    DealOppAa(s, 40);
    s.m_provider = &AaProv();
    GameState t = s;
    CHECK(ApplyUntapLoop(t, 0, kUntapEnchanted, alch, alch) == 0);
    CHECK(t.floating_mana.Total() == 0);
    CHECK(ByNumAa(t, alch)->tapped);
    CHECK(t.players[0].life == s.players[0].life);
}

TEST_CASE("Loop colour: cast Staff off the Alchemist, then the Umbra untap still finds its {U} (both worlds)")
{
    GameState s = FreshAa();
    const int alch = PutAa(s, "Accomplished Alchemist");
    AttachAa(s, "Crab Umbra", alch);
    PutAa(s, "Forest");
    s.players[0].life_gained_this_turn = 6;
    HandAa(s, "Staff of Domination");
    s.m_provider = &AaProv();
    // Any plan that casts the Staff AND activates the Umbra untap once must realise the untap in BOTH worlds:
    // the Alchemist's 6 have to come out blue (or the Forest pays the Staff), never all white.
    int seen = 0;
    for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(s, true))
    {
        bool staff = false; const Action* un = nullptr;
        for (const Action& a : p.actions)
        {
            if (a.kind == Action::Kind::CastFromHand && a.card_name.str() == "Staff of Domination") { staff = true; }
            if (a.kind == Action::Kind::ActivateUntap && a.chosen_x == 1) { un = &a; }
        }
        if (!staff || un == nullptr || p.bp_choice >= 0) { continue; }
        ++seen;
        const auto [ro, ex] = BothWorldsAa(s, p);
        CHECK_MESSAGE(!ByNumAa(ro, alch)->tapped, "rollout left the Alchemist tapped");
        CHECK_MESSAGE(!ByNumAa(ex, alch)->tapped, "executor left the Alchemist tapped");
    }
    CHECK(seen > 0);
}

// ---- Greedy-walk branching folds (2026-10-20): the same greedy plan with each fold on and off ----------------
namespace
{
struct GreedyFoldArmsAa
{
    std::int8_t pk, ph;
    GreedyFoldArmsAa(bool kdom, bool host)
        : pk(heurarm::t_arm[heurarm::GREEDY_UNTAP_K_DOM]), ph(heurarm::t_arm[heurarm::GREEDY_AURA_HOST_LATE])
    {
        heurarm::t_arm[heurarm::GREEDY_UNTAP_K_DOM]    = kdom ? 1 : 0;
        heurarm::t_arm[heurarm::GREEDY_AURA_HOST_LATE] = host ? 1 : 0;
    }
    ~GreedyFoldArmsAa()
    {
        heurarm::t_arm[heurarm::GREEDY_UNTAP_K_DOM]    = pk;
        heurarm::t_arm[heurarm::GREEDY_AURA_HOST_LATE] = ph;
    }
};
// Everything the apply worlds read off the greedy's chosen plan.
std::string GreedyPlanTextAa(const TurnSolver::Plan& p)
{
    std::string o = "v" + std::to_string(p.value) + (p.wins_this_turn ? "W" : "") + " land=" + p.land_to_play + ":";
    for (const Action& a : p.actions)
    {
        o += " " + a.card_name.str() + "/k" + std::to_string(static_cast<int>(a.kind)) + "/x" + std::to_string(a.chosen_x)
           + "/s" + std::to_string(a.sac_source_id) + "/v" + std::to_string(a.sac_victim_id)
           + "/t" + std::to_string(a.enchant_target) + "/m" + std::to_string(a.gy_exile_mode)
           + "/a" + std::to_string(static_cast<int>(a.ability_mode));
    }
    return o;
}
TurnSolver::Plan GreedyAa(const GameState& s, bool kdom, bool host)
{
    GreedyFoldArmsAa arms(kdom, host);
    return TurnSolver::Solve(s, true, TurnSolver::GreedyPermit(TurnSolver::GreedySite::HorizonLeaf, 0));
}
}   // namespace

TEST_CASE("Greedy untap-count dominance: the greedy walk picks the same plan with every K or only the top K")
{
    // Boards that put an untapper's (target, K) family in the greedy walk: a Staff on a tapped Alchemist
    // with life gained, a Crab Umbra on it, both, and two Staffs. The dominated K are dropped (the hook
    // counts them) and the chosen plan is byte-for-byte the plan of the unfolded walk.
    long long dropped_total = 0;
    for (int board = 0; board < 6; ++board)
    {
        GameState s = FreshAa();
        s.m_provider = &AaProv();
        const int alch = PutAa(s, "Accomplished Alchemist");
        MutAa(s, alch)->tapped = true;
        s.players[0].life_gained_this_turn = 3 + board;
        for (int k = 0; k < 4 + (board % 3); ++k) { PutAa(s, "Forest"); }
        PutAa(s, "Island");
        if (board != 1) { PutAa(s, "Staff of Domination"); }
        if (board >= 1) { AttachAa(s, "Crab Umbra", alch); }
        if (board >= 4) { PutAa(s, "Staff of Domination"); }
        if (board % 2 == 0) { PutAa(s, "Arbor Elf"); }
        if (board == 5) { HandAa(s, "Nourish"); HandAa(s, "Elvish Harbinger"); }
        const long long d0 = GreedyFoldTestCount(0);
        const TurnSolver::Plan on  = GreedyAa(s, true, false);
        dropped_total += GreedyFoldTestCount(0) - d0;
        const TurnSolver::Plan off = GreedyAa(s, false, false);
        CHECK_MESSAGE(GreedyPlanTextAa(on) == GreedyPlanTextAa(off), "board ", board);
    }
    CHECK(dropped_total > 0);
}

TEST_CASE("Greedy Crab Umbra host collapse: one host in the walk, the re-pointing still picks over every host")
{
    long long dropped_total = 0;
    for (int board = 0; board < 4; ++board)
    {
        GameState s = FreshAa();
        s.m_provider = &AaProv();
        for (int k = 0; k < 5; ++k) { PutAa(s, "Forest"); }
        for (int k = 0; k < 2; ++k) { PutAa(s, "Island"); }
        PutAa(s, "Arbor Elf");
        const int h1 = PutAa(s, "Elvish Harbinger");
        PutAa(s, "Elvish Harbinger", board % 2 == 1);
        const int alch = PutAa(s, "Accomplished Alchemist", board >= 2);
        if (board == 3) { MutAa(s, h1)->tapped = true; MutAa(s, alch)->tapped = true; }
        s.players[0].life_gained_this_turn = board;
        HandAa(s, "Crab Umbra");
        if (board >= 1) { HandAa(s, "Nourish"); }
        const long long d0 = GreedyFoldTestCount(1);
        const TurnSolver::Plan on  = GreedyAa(s, false, true);
        dropped_total += GreedyFoldTestCount(1) - d0;
        const TurnSolver::Plan off = GreedyAa(s, false, false);
        CHECK_MESSAGE(GreedyPlanTextAa(on) == GreedyPlanTextAa(off), "board ", board);
        bool umbra = false;
        for (const Action& a : on.actions) { if (a.card_name.str() == "Crab Umbra") { umbra = true; } }
        CHECK_MESSAGE(umbra, "board ", board, ": the greedy should cast the Umbra here");
    }
    CHECK(dropped_total > 0);
}

TEST_CASE("Late-bound Crab Umbra host (searched walk): the same plan list as walking every host")
{
    // The dedup keeps ONE member per name-keyed class -- the best host key -- so binding that host at plan
    // build must reproduce the unfolded enumeration's list: same plans, same hosts (no key ties on these
    // boards: the hosts differ in tapped / sick / mana-dork state), same order.
    struct Arm
    {
        std::int8_t p;
        explicit Arm(bool on) : p(heurarm::t_arm[heurarm::AURA_HOST_LATE_BIND]) { heurarm::t_arm[heurarm::AURA_HOST_LATE_BIND] = on ? 1 : 0; }
        ~Arm() { heurarm::t_arm[heurarm::AURA_HOST_LATE_BIND] = p; }
    };
    auto text = [](const std::vector<TurnSolver::Plan>& v)
    {
        std::vector<std::string> o;
        for (const TurnSolver::Plan& p : v) { o.push_back(GreedyPlanTextAa(p)); }
        return o;
    };
    long long dropped = 0;
    for (int board = 0; board < 4; ++board)
    {
        GameState s = FreshAa();
        s.m_provider = &AaProv();
        for (int k = 0; k < 5; ++k) { PutAa(s, "Forest"); }
        for (int k = 0; k < 2; ++k) { PutAa(s, "Island"); }
        PutAa(s, "Arbor Elf");
        const int alch = PutAa(s, "Accomplished Alchemist", board == 1);
        if (board >= 2) { MutAa(s, alch)->tapped = true; s.players[0].life_gained_this_turn = 4; }
        PutAa(s, "Galvanic Alchemist");
        HandAa(s, "Crab Umbra");
        HandAa(s, "Elvish Harbinger");                    // a same-turn hand host
        if (board == 3) { HandAa(s, "Crab Umbra"); HandAa(s, "Accomplished Alchemist"); }
        std::vector<TurnSolver::Plan> on, off;
        const long long d0 = AuraLateBindTestCount();
        { Arm a(true);  on  = TurnSolver::EnumerateMainPlans(s, true); }
        dropped += AuraLateBindTestCount() - d0;
        { Arm a(false); off = TurnSolver::EnumerateMainPlans(s, true); }
        CHECK_MESSAGE(text(on) == text(off), "board ", board);
    }
    CHECK(dropped > 0);
// ---- THE USER'S LIVING WISH BUCKETS (2026-10-20, MTG_AA_WISH_BUCKETS) ------------------------------------------
// AccomplishedAlchemistProvider::TutorCandidates offers a wish only the sideboard cards of the buckets (Mana,
// Accomplished Alchemist, Lifegain, Untap, Wincon/Draw) that our hand + battlefield lack, in that order;
// Trufflesnout and Eternal Witness never. Under the Tutor unprune gate (human play / MTG_UNPRUNED) the
// full list stands -- these cases expect it whenever the gate is open, so they hold in either process.
namespace
{
const std::vector<std::string> kAaSide = { "Accomplished Alchemist", "Galvanic Alchemist", "Trufflesnout",
                                           "Scholar of Athreos", "Forest", "Island", "Eternal Witness",
                                           "Arbor Elf", "Brindle Boar" };
GameState WishAa()
{
    GameState s = FreshAa();
    HandAa(s, "Living Wish");
    for (const std::string& n : kAaSide) { SideAa(s, n); }
    s.m_provider = &AaProv();
    return s;
}
std::vector<std::string> WishCandsAa(const GameState& s)
{
    return AaProv().TutorCandidates(s, 0, DefAa("Living Wish").params);
}
// The narrowed expectation, or the whole sideboard when the Tutor gate is open.
void ExpectWishAa(const GameState& s, const std::vector<std::string>& want)
{
    const std::vector<std::string> full = AaProv().GenericProvider::TutorCandidates(s, 0, DefAa("Living Wish").params);
    CHECK(WishCandsAa(s) == (DecisionUnpruned(UnprunedGate::Tutor) ? full : want));
}
struct WishArmAa
{
    std::int8_t saved;
    explicit WishArmAa(int v) : saved(heurarm::t_arm[heurarm::AA_WISH_BUCKETS])
    { heurarm::t_arm[heurarm::AA_WISH_BUCKETS] = static_cast<std::int8_t>(v); }
    ~WishArmAa() { heurarm::t_arm[heurarm::AA_WISH_BUCKETS] = saved; }
};
}   // namespace

TEST_CASE("Wish buckets: nothing held -> every bucket, in the USER's order; never Trufflesnout / Eternal Witness")
{
    const GameState s = WishAa();
    ExpectWishAa(s, { "Arbor Elf", "Forest", "Island", "Accomplished Alchemist", "Brindle Boar",
                      "Galvanic Alchemist", "Scholar of Athreos" });
    CHECK(AaProv().TutorSearchWidth() >= 7);   // every admitted name is searched (coverage)
}

TEST_CASE("Wish buckets: ALCHEMIST -- offered only while none is in hand or in play")
{
    GameState h = WishAa();
    for (int k = 0; k < 4; ++k) { PutAa(h, "Forest"); }
    HandAa(h, "Nourish"); HandAa(h, "Crab Umbra"); HandAa(h, "Stroke of Genius");
    GameState p = h;
    ExpectWishAa(h, { "Accomplished Alchemist" });           // only the Alchemist is lacking
    HandAa(h, "Accomplished Alchemist");
    ExpectWishAa(h, { "Scholar of Athreos" });               // nothing lacking -> Scholar
    PutAa(p, "Accomplished Alchemist");
    ExpectWishAa(p, { "Scholar of Athreos" });               // in play fills the bucket too
}

TEST_CASE("Wish buckets: MANA -- < 4 projected mana; hand lands count; Island only without a blue source")
{
    GameState s = WishAa();
    HandAa(s, "Accomplished Alchemist"); HandAa(s, "Nourish"); HandAa(s, "Crab Umbra"); HandAa(s, "Stroke of Genius");
    for (int k = 0; k < 3; ++k) { PutAa(s, "Forest"); }
    // 3 mana (Utopia Sprawl in hand is not counted as mana; it IS a blue source) -> Elf + Forest, no Island.
    GameState sprawl = s;
    HandAa(sprawl, "Utopia Sprawl");
    ExpectWishAa(sprawl, { "Arbor Elf", "Forest" });
    // 3 mana and no blue source -> Elf + Forest + Island.
    ExpectWishAa(s, { "Arbor Elf", "Forest", "Island" });
    // An Island in play is a blue source (and the 4th mana) -> not lacking.
    GameState isl = s;
    PutAa(isl, "Island");
    ExpectWishAa(isl, { "Scholar of Athreos" });
    // A land in HAND is the 4th mana.
    GameState hl = s;
    HandAa(hl, "Forest");
    ExpectWishAa(hl, { "Scholar of Athreos" });
    // A dork in play is the 4th mana.
    GameState dk = s;
    PutAa(dk, "Elvish Harbinger");
    ExpectWishAa(dk, { "Scholar of Athreos" });
}

TEST_CASE("Wish buckets: MANA -- Arbor Elf ALONE when it re-taps a land-Aura Forest for more than one mana")
{
    GameState s = WishAa();
    HandAa(s, "Accomplished Alchemist"); HandAa(s, "Nourish"); HandAa(s, "Crab Umbra"); HandAa(s, "Stroke of Genius");
    const int f = PutAa(s, "Forest");
    PutAa(s, "Forest");
    AttachAa(s, "Wild Growth", f);                       // 3 mana, and the Elf would make 2
    ExpectWishAa(s, { "Arbor Elf" });
    // The Elf already fetched: the lands come back (Wild Growth is green, so Island too).
    GameState gone = s;
    auto& sb = gone.players[0].sideboard;
    sb.erase(std::remove_if(sb.begin(), sb.end(), [](const Card& c) { return c.m_name.str() == "Arbor Elf"; }), sb.end());
    ExpectWishAa(gone, { "Forest", "Island" });
}

TEST_CASE("Wish buckets: LIFEGAIN -- Brindle Boar only with no lifegain held; the Staff's gain-1 is not lifegain")
{
    GameState s = WishAa();
    HandAa(s, "Accomplished Alchemist"); HandAa(s, "Crab Umbra"); HandAa(s, "Stroke of Genius");
    for (int k = 0; k < 4; ++k) { PutAa(s, "Forest"); }
    ExpectWishAa(s, { "Brindle Boar" });
    GameState st = s;
    PutAa(st, "Staff of Domination");                    // untap + draw, but not lifegain for this test
    ExpectWishAa(st, { "Brindle Boar" });
    for (const char* g : { "Nourish", "Feed the Clan", "Weather the Storm", "Trufflesnout" })
    {
        GameState h = s;
        HandAa(h, g);
        ExpectWishAa(h, { "Scholar of Athreos" });
    }
    GameState bp = s;
    PutAa(bp, "Brindle Boar");                           // an unspent Boar in play is held lifegain
    ExpectWishAa(bp, { "Scholar of Athreos" });
}

TEST_CASE("Wish buckets: UNTAP -- Galvanic only with no Crab Umbra / Staff / Galvanic held")
{
    GameState s = WishAa();
    HandAa(s, "Accomplished Alchemist"); HandAa(s, "Nourish"); HandAa(s, "Stroke of Genius");
    for (int k = 0; k < 4; ++k) { PutAa(s, "Forest"); }
    ExpectWishAa(s, { "Galvanic Alchemist" });
    for (const char* u : { "Crab Umbra", "Staff of Domination", "Galvanic Alchemist" })
    {
        GameState h = s;
        HandAa(h, u);
        ExpectWishAa(h, { "Scholar of Athreos" });
    }
}

TEST_CASE("Wish buckets: Staff of Domination fills BOTH Untap and Wincon/Draw")
{
    GameState s = WishAa();
    HandAa(s, "Accomplished Alchemist"); HandAa(s, "Nourish");
    for (int k = 0; k < 4; ++k) { PutAa(s, "Forest"); }
    ExpectWishAa(s, { "Galvanic Alchemist", "Scholar of Athreos" });
    GameState st = s;
    HandAa(st, "Staff of Domination");
    ExpectWishAa(st, { "Scholar of Athreos" });          // nothing lacking -> Scholar ("need nothing else")
    GameState sp = s;
    PutAa(sp, "Staff of Domination");
    ExpectWishAa(sp, { "Scholar of Athreos" });
}

TEST_CASE("Wish buckets: Seedcradle Witch is an untapper unless the held lifegain totals < 5 (the Boar's 4)")
{
    GameState s = WishAa();
    HandAa(s, "Accomplished Alchemist"); HandAa(s, "Stroke of Genius"); HandAa(s, "Seedcradle Witch");
    for (int k = 0; k < 4; ++k) { PutAa(s, "Forest"); }
    GameState big = s;
    HandAa(big, "Nourish");                              // 6 >= 5: the Witch loops -> no Galvanic
    ExpectWishAa(big, { "Scholar of Athreos" });
    GameState boar = s;
    HandAa(boar, "Brindle Boar");                        // 4 < 5: only a Boar -> Galvanic allowed
    ExpectWishAa(boar, { "Galvanic Alchemist" });
    GameState two = boar;
    HandAa(two, "Weather the Storm");                    // 4 + 3 = 7 >= 5 -> the Witch counts again
    ExpectWishAa(two, { "Scholar of Athreos" });
}

TEST_CASE("Wish buckets: WINCON -- Scholar only with no Stroke / Staff / Scholar held, or with nothing else lacking")
{
    GameState s = WishAa();
    HandAa(s, "Nourish"); HandAa(s, "Crab Umbra");
    for (int k = 0; k < 4; ++k) { PutAa(s, "Forest"); }
    ExpectWishAa(s, { "Accomplished Alchemist", "Scholar of Athreos" });
    GameState st = s;
    HandAa(st, "Stroke of Genius");
    ExpectWishAa(st, { "Accomplished Alchemist" });      // a lacking bucket elsewhere: no Scholar
    GameState sc = s;
    HandAa(sc, "Scholar of Athreos");
    ExpectWishAa(sc, { "Accomplished Alchemist" });
}

TEST_CASE("Wish buckets: nothing admitted -> the whole legal list (a wish is never made unfetchable)")
{
    GameState s = WishAa();
    HandAa(s, "Accomplished Alchemist"); HandAa(s, "Nourish"); HandAa(s, "Crab Umbra"); HandAa(s, "Stroke of Genius");
    for (int k = 0; k < 4; ++k) { PutAa(s, "Forest"); }
    auto& sb = s.players[0].sideboard;
    sb.erase(std::remove_if(sb.begin(), sb.end(), [](const Card& c) { return c.m_name.str() == "Scholar of Athreos"; }), sb.end());
    std::vector<std::string> rest;
    for (const std::string& n : kAaSide) { if (n != "Scholar of Athreos") { rest.push_back(n); } }
    CHECK(WishCandsAa(s) == rest);
}

TEST_CASE("Wish buckets: =0 restores every name at the base width; a non-wish tutor is untouched")
{
    GameState s = WishAa();
    HandAa(s, "Accomplished Alchemist");
    {
        WishArmAa off(0);
        CHECK(WishCandsAa(s) == kAaSide);
        CHECK(AaProv().TutorSearchWidth() == 6);
    }
    {
        WishArmAa on(1);
        CHECK(AaProv().TutorSearchWidth() == 9);
        // Elvish Harbinger's library Elf tutor is not a wish: the generic list.
        GameState h = FreshAa();
        LibAa(h, "Accomplished Alchemist"); LibAa(h, "Arbor Elf");
        h.m_provider = &AaProv();
        const CardParams& hp = DefAa("Elvish Harbinger").params;
        CHECK(AaProv().TutorCandidates(h, 0, hp) == AaProv().GenericProvider::TutorCandidates(h, 0, hp));
    }
}

TEST_CASE("Wish buckets: human play / the Tutor unprune gate keeps the whole list, the rule's pick ranked first")
{
    // MTG_HUMAN_PLAY / MTG_UNPRUNED are process-static, so this case asserts the gate's contract in
    // whichever process runs it (run it once under MTG_UNPRUNE=tutor to exercise the open branch).
    GameState s = WishAa();
    for (int k = 0; k < 4; ++k) { PutAa(s, "Forest"); }
    HandAa(s, "Accomplished Alchemist"); HandAa(s, "Nourish"); HandAa(s, "Crab Umbra"); HandAa(s, "Stroke of Genius");
    if (DecisionUnpruned(UnprunedGate::Tutor)) { CHECK(WishCandsAa(s) == kAaSide); }
    else { CHECK(WishCandsAa(s) == std::vector<std::string>{ "Scholar of Athreos" }); }
}

TEST_CASE("Wish buckets, PROPOSED amendments (default OFF): blue as its own need; Trufflesnout beside the Boar")
{
    struct Arm2
    {
        std::int8_t b, t;
        Arm2(int blue, int truf) : b(heurarm::t_arm[heurarm::AA_WISH_BLUE]), t(heurarm::t_arm[heurarm::AA_WISH_TRUFFLE])
        { heurarm::t_arm[heurarm::AA_WISH_BLUE] = static_cast<std::int8_t>(blue);
          heurarm::t_arm[heurarm::AA_WISH_TRUFFLE] = static_cast<std::int8_t>(truf); }
        ~Arm2() { heurarm::t_arm[heurarm::AA_WISH_BLUE] = b; heurarm::t_arm[heurarm::AA_WISH_TRUFFLE] = t; }
    };
    // 4 mana (not short), Stroke in hand needs {U}, no blue source: the USER's rule offers no land.
    GameState s = WishAa();
    HandAa(s, "Accomplished Alchemist"); HandAa(s, "Nourish"); HandAa(s, "Crab Umbra"); HandAa(s, "Stroke of Genius");
    for (int k = 0; k < 4; ++k) { PutAa(s, "Forest"); }
    { Arm2 a(0, 0); ExpectWishAa(s, { "Scholar of Athreos" }); }
    { Arm2 a(1, 0); ExpectWishAa(s, { "Island", "Scholar of Athreos" }); }
    // A dork in HAND is not a firm blue source under the amendment; a blue land in hand is.
    GameState h = s;
    HandAa(h, "Elvish Harbinger");
    { Arm2 a(1, 0); ExpectWishAa(h, { "Island", "Scholar of Athreos" }); }
    GameState sp = s;
    HandAa(sp, "Utopia Sprawl");
    { Arm2 a(1, 0); ExpectWishAa(sp, { "Scholar of Athreos" }); }
    // Trufflesnout joins the Boar only while lifegain is lacking.
    GameState g = WishAa();
    HandAa(g, "Accomplished Alchemist"); HandAa(g, "Crab Umbra"); HandAa(g, "Stroke of Genius");
    for (int k = 0; k < 4; ++k) { PutAa(g, "Forest"); }
    PutAa(g, "Island");
    { Arm2 a(0, 0); ExpectWishAa(g, { "Brindle Boar" }); }
    { Arm2 a(0, 1); ExpectWishAa(g, { "Trufflesnout", "Brindle Boar" }); }
}
