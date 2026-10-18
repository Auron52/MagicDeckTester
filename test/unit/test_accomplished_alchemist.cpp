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

    // One mana short of lethal: the opponent variant is not offered at all; self still is.
    GameState u = FreshAa();
    for (int k = 0; k < 6; ++k) { PutAa(u, "Island"); }
    HandAa(u, "Stroke of Genius");
    DealOppAa(u, 4);
    bool any_opp = false, any_self = false;
    for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(u, true))
    {
        const Action* a = StrokeCastAa(p);
        if (a == nullptr) { continue; }
        if (a->draw_target != 0) { any_opp = true; } else { any_self = true; }
    }
    CHECK_FALSE(any_opp);
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
