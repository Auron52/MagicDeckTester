// Unit tests for the NIV-MIZZET DRAW/DAMAGE LOOP (NivMizzetCombo, 2026-10-17; core/SpellEffects.h,
// "NIV-MIZZET DRAW/DAMAGE LOOP"):
//
//   * Niv-Mizzet, the Firemind / Parun  "Whenever you draw a card, Niv-Mizzet deals 1 damage to any
//                                       target."            -> draw_trigger_damage (face, folded)
//   * Niv-Mizzet, the Firemind          "{T}: Draw a card."  -> tap_draw_cost {0}
//   * Niv-Mizzet, Parun                 "Whenever a player casts an instant or sorcery spell, you
//                                       draw a card."        -> cast_trigger_instant_sorcery_draw
//   * Curiosity                         "Whenever enchanted creature deals damage to an opponent, you
//                                       may draw a card."    -> aura_damage_trigger_draw ("may" folded)
//   * Tandem Lookout                    soulbond + both halves "Whenever this creature deals damage to
//                                       an opponent, draw a card." -> soulbond_damage_trigger_draw
//
// WHY UNIT TESTS. The loop is a counting machine -- pings per draw, draws per ping, where it STOPS --
// and every miscount is silent: a loop that runs one draw past the kill decks nobody in a game that is
// already won, and one that stops a ping short just looks like a slightly slower deck. These pin the
// counts exactly, the two stop conditions (the kill; a mandatory draw from an empty library), the
// player filter ("whenever YOU draw"), the deck gate that keeps every other deck byte-identical, and
// that the executor and the rollout realise the same line.
#include <doctest/doctest.h>

#include "ai/AIEngine.h"
#include "ai/Combat.h"
#include "ai/DecisionProviders.h"
#include "ai/HeuristicArm.h"
#include "ai/TurnSolver.h"
#include "cards/CardDatabase.h"
#include "core/EffectHandler.h"
#include "core/GameSetup.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"
#include "deck/DeckLoader.h"
#include "mtg_test_seam.h"
#include "runner/GoldFishRunner.h"

#include <string>
#include <vector>

namespace
{

void EnsureCardsLoadedNiv()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

const CardDefinition& DefNiv(const std::string& name)
{
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE_MESSAGE(d != nullptr, "card not in cards.json: ", name);
    return *d;
}

Card CardNiv(const std::string& name, int number)
{
    Card c = DefNiv(name).card;
    c.m_number = number;
    return c;
}

// A fresh mid-game board: turn 6, both at 20, land drop spent, `library` Islands on top of OUR library
// and 10 on the opponent's (so an opponent draw is possible at all). deck_has_draw_ping stays at its
// unstamped default (TRUE), the same state the scenario harness builds.
GameState FreshNiv(int library = 40)
{
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 6;
    s.players[0].life     = gamesetup::StartingLife();
    s.players[1].life     = gamesetup::StartingLife();
    s.players[0].lands_played_this_turn = 1;
    for (int k = 0; k < library; ++k) { s.players[0].library.push_back(CardNiv("Island", 900 + k)); }
    for (int k = 0; k < 10; ++k)      { s.players[1].library.push_back(CardNiv("Island", 700 + k)); }
    return s;
}

int PutNiv(GameState& s, const std::string& name, int number, int controller = 0, bool sick = false)
{
    Permanent p;
    p.card              = CardNiv(name, number);
    p.controller_index  = controller;
    p.owner_index       = controller;
    p.entered_this_turn = sick;
    s.battlefield.push_back(p);
    return static_cast<int>(s.battlefield.size()) - 1;
}

// Attach an Aura already on the battlefield to a host, as the cast's ResolveEnchantTarget would.
void AttachNiv(GameState& s, int aura_idx, int host_number)
{
    s.battlefield[static_cast<std::size_t>(aura_idx)].aura_attached_to = host_number;
}

// Pair a soulbond permanent with a partner, as try_pair would (the link lives on the soulbond side).
void PairNiv(GameState& s, int soulbond_idx, int partner_number)
{
    s.battlefield[static_cast<std::size_t>(soulbond_idx)].paired_with = partner_number;
}

int Lib(const GameState& s, int who = 0) { return static_cast<int>(s.players[who].library.size()); }
int Hand(const GameState& s, int who = 0) { return static_cast<int>(s.players[who].hand.size()); }

// "Draw N, then the triggers": the shape of every resolution site (the flush runs after the effect).
void DrawThenFlush(GameState& s, int who, int n)
{
    TrickDraw(s, who, n);
    ResolveDrawTriggers(s);
}

// GameEngine::MainPhase's shape: every cast resolves before the next.
void ResolveAllNiv(GameState& st)
{
    while (!st.stack.empty())
    {
        const StackEntry e = st.stack.back();
        st.stack.pop_back();
        const CardDefinition* d = CardDatabase::Instance().LookupCached(e.source);
        if (d != nullptr) { EffectHandler::Resolve(st, e, *d); }
    }
}

}   // namespace

// ---- 1. the draw trigger: one ping per card drawn ---------------------------------------------------

TEST_CASE("Niv draw trigger: one draw is one ping, to the opponent's face")
{
    EnsureCardsLoadedNiv();
    GameState s = FreshNiv();
    PutNiv(s, "Niv-Mizzet, the Firemind", 1);
    DrawThenFlush(s, 0, 1);
    CHECK(s.players[1].life == 19);
    CHECK(s.players[0].life == 20);            // never ourselves
    CHECK(Hand(s) == 1);
    CHECK(Lib(s) == 39);
    CHECK(s.opponent_lost_life_this_turn);
    CHECK(s.draw_trigger_mark == 1);           // the watermark caught up
    // Idempotent: a second flush with nothing new drawn resolves nothing.
    ResolveDrawTriggers(s);
    CHECK(s.players[1].life == 19);
}

TEST_CASE("Niv draw trigger: a multi-card draw (Brainstorm-like) is one ping PER CARD, after the draw")
{
    EnsureCardsLoadedNiv();
    GameState s = FreshNiv();
    PutNiv(s, "Niv-Mizzet, the Firemind", 1);
    TrickDraw(s, 0, 3);
    // The effect is still resolving: no ping has happened yet (CR 603.3 -- a Brainstorm would put
    // back here). Only the flush resolves the triggers.
    CHECK(s.players[1].life == 20);
    ResolveDrawTriggers(s);
    CHECK(s.players[1].life == 17);
    CHECK(Hand(s) == 3);
}

TEST_CASE("Niv draw trigger: two Nivs ping twice per draw")
{
    EnsureCardsLoadedNiv();
    GameState s = FreshNiv();
    PutNiv(s, "Niv-Mizzet, the Firemind", 1);
    PutNiv(s, "Niv-Mizzet, Parun", 2);
    DrawThenFlush(s, 0, 1);
    CHECK(s.players[1].life == 18);
    DrawThenFlush(s, 0, 2);
    CHECK(s.players[1].life == 14);
}

TEST_CASE("Niv draw trigger: the OPPONENT's draws never ping (\"whenever YOU draw\")")
{
    EnsureCardsLoadedNiv();
    GameState s = FreshNiv();
    PutNiv(s, "Niv-Mizzet, the Firemind", 1);
    DrawThenFlush(s, 1, 2);                    // the opponent draws two
    CHECK(Hand(s, 1) == 2);
    CHECK(s.players[1].life == 20);
    CHECK(s.players[0].life == 20);
    CHECK(s.draw_trigger_mark == 0);
    // ...and an opponent-controlled Niv does not ping on OUR draws either.
    GameState t = FreshNiv();
    PutNiv(t, "Niv-Mizzet, the Firemind", 1, /*controller=*/1);
    DrawThenFlush(t, 0, 1);
    CHECK(t.players[1].life == 20);
    CHECK(t.players[0].life == 20);
}

TEST_CASE("Niv draw trigger: a deck stamped WITHOUT the loop resolves nothing (the byte-identity gate)")
{
    EnsureCardsLoadedNiv();
    GameState s = FreshNiv();
    s.deck_has_draw_ping = false;
    PutNiv(s, "Niv-Mizzet, the Firemind", 1);
    DrawThenFlush(s, 0, 2);
    CHECK(s.players[1].life == 20);
    CHECK(s.draw_trigger_mark == 0);           // never written on a gated-off deck
    // And the stamp itself: a decklist with no loop card is false, one with a Niv is true.
    Decklist without;
    without.mainboard.push_back(DefNiv("Island").card);
    without.mainboard.push_back(DefNiv("Ponder").card);
    GameState a;
    GoldFishRunner::StampDeckTraits(a, without);
    CHECK_FALSE(a.deck_has_draw_ping);
    Decklist with_niv = without;
    with_niv.mainboard.push_back(DefNiv("Curiosity").card);
    GameState b;
    GoldFishRunner::StampDeckTraits(b, with_niv);
    CHECK(b.deck_has_draw_ping);
}

// ---- 2. Curiosity: the optional loop ------------------------------------------------------------------

TEST_CASE("Curiosity on a Niv: one draw starts the loop, it KILLS from 20 and stops exactly at the kill")
{
    EnsureCardsLoadedNiv();
    GameState s = FreshNiv(40);
    PutNiv(s, "Niv-Mizzet, the Firemind", 1);
    const int cur = PutNiv(s, "Curiosity", 2);
    AttachNiv(s, cur, 1);
    DrawThenFlush(s, 0, 1);
    CHECK(s.players[1].life == 0);
    CHECK(OpponentHasLost(s));
    // 20 pings need 20 draws: the first plus one per ping except the killing one. Not one more.
    CHECK(s.players[0].cards_drawn_this_turn == 20);
    CHECK(Lib(s) == 20);
    CHECK_FALSE(s.player_lost_on_draw);
    // After the kill, a further draw resolves no trigger (the game is over).
    DrawThenFlush(s, 0, 1);
    CHECK(s.players[1].life == 0);
    CHECK(Lib(s) == 19);
}

TEST_CASE("Curiosity's \"may\": with a short library the loop stops when it runs dry and NEVER decks us")
{
    EnsureCardsLoadedNiv();
    GameState s = FreshNiv(5);
    PutNiv(s, "Niv-Mizzet, the Firemind", 1);
    const int cur = PutNiv(s, "Curiosity", 2);
    AttachNiv(s, cur, 1);
    DrawThenFlush(s, 0, 1);
    CHECK(Lib(s) == 0);
    CHECK(s.players[1].life == 15);            // 5 draws -> 5 pings; the 5th's may-draw is declined
    CHECK_FALSE(s.player_lost_on_draw);
    CHECK_FALSE(OpponentHasLost(s));
}

TEST_CASE("Curiosity on a NON-Niv creature: combat damage draws one card per damage event")
{
    EnsureCardsLoadedNiv();
    GameState s = FreshNiv();
    const int guide = PutNiv(s, "Goblin Guide", 3);
    const int cur   = PutNiv(s, "Curiosity", 4);
    AttachNiv(s, cur, 3);
    ResolveCombatDamage(s, { guide }, /*exalted_bonus=*/0, /*collect_descs=*/false);
    CHECK(s.players[1].life == 18);            // 2 combat damage, no Niv to ping
    CHECK(Hand(s) == 1);                       // ONE draw for the event, not one per point
    // With a Niv on the battlefield (not attacking) that combat draw pings once -- and only once,
    // because the ping is NIV's damage and Niv carries no Curiosity.
    GameState t = FreshNiv();
    PutNiv(t, "Niv-Mizzet, the Firemind", 1);
    const int g2 = PutNiv(t, "Goblin Guide", 3);
    const int c2 = PutNiv(t, "Curiosity", 4);
    AttachNiv(t, c2, 3);
    ResolveCombatDamage(t, { g2 }, 0, false);
    CHECK(t.players[1].life == 17);
    CHECK(Hand(t) == 1);
}

TEST_CASE("Curiosity on a first / double striker: one draw per combat-damage STEP it connects in (CR 510.4)")
{
    // A first or double striker sends the combat to the two-step resolution; the damage->draw grant
    // fires per step there, so a double striker draws twice and a first striker once.
    EnsureCardsLoadedNiv();
    GameState s = FreshNiv();
    const int vind = PutNiv(s, "Swiftblade Vindicator", 3);
    const int cur  = PutNiv(s, "Curiosity", 4);
    AttachNiv(s, cur, 3);
    ResolveCombatDamage(s, { vind }, 0, false);
    CHECK(s.players[1].life == 18);            // 1 in each step
    CHECK(Hand(s) == 2);                       // two damage events, two draws
    // With a Niv up (not attacking), each step's draw pings before the next step.
    GameState t = FreshNiv();
    PutNiv(t, "Niv-Mizzet, the Firemind", 1);
    const int v2 = PutNiv(t, "Swiftblade Vindicator", 3);
    const int c2 = PutNiv(t, "Curiosity", 4);
    AttachNiv(t, c2, 3);
    ResolveCombatDamage(t, { v2 }, 0, false);
    CHECK(t.players[1].life == 16);            // 1 + ping, 1 + ping
    CHECK(Hand(t) == 2);
    // A FIRST striker connects in the first step only: one draw, one ping.
    GameState u = FreshNiv();
    PutNiv(u, "Niv-Mizzet, the Firemind", 1);
    const int th = PutNiv(u, "Thalia, Guardian of Thraben", 3);
    const int c3 = PutNiv(u, "Curiosity", 4);
    AttachNiv(u, c3, 3);
    ResolveCombatDamage(u, { th }, 0, false);
    CHECK(u.players[1].life == 17);            // 2 combat + 1 ping
    CHECK(Hand(u) == 1);
}

TEST_CASE("Curiosity on an attacking Niv: the combat-damage draw starts the loop and it kills")
{
    EnsureCardsLoadedNiv();
    GameState s = FreshNiv(40);
    const int niv = PutNiv(s, "Niv-Mizzet, the Firemind", 1);
    const int cur = PutNiv(s, "Curiosity", 2);
    AttachNiv(s, cur, 1);
    ResolveCombatDamage(s, { niv }, 0, false);
    CHECK(OpponentHasLost(s));
    CHECK(s.players[1].life == 0);
    // 4 combat damage leaves 16: the combat draw plus 15 Curiosity draws, the 16th ping kills.
    CHECK(s.players[0].cards_drawn_this_turn == 16);
}

TEST_CASE("Combat damage that already killed fires no damage->draw trigger")
{
    EnsureCardsLoadedNiv();
    GameState s = FreshNiv(40);
    s.players[1].life = 2;
    const int guide = PutNiv(s, "Goblin Guide", 3);
    const int cur   = PutNiv(s, "Curiosity", 4);
    AttachNiv(s, cur, 3);
    ResolveCombatDamage(s, { guide }, 0, false);
    CHECK(OpponentHasLost(s));
    CHECK(Hand(s) == 0);
}

// ---- 3. Tandem Lookout: the mandatory loop -----------------------------------------------------------

TEST_CASE("Tandem Lookout paired with a Niv: the MANDATORY loop stops at the kill and does not deck us")
{
    EnsureCardsLoadedNiv();
    GameState s = FreshNiv(20);                // exactly the 20 draws a 20-ping kill needs
    PutNiv(s, "Niv-Mizzet, the Firemind", 1);
    const int lk = PutNiv(s, "Tandem Lookout", 2);
    PairNiv(s, lk, 1);
    DrawThenFlush(s, 0, 1);
    CHECK(OpponentHasLost(s));
    CHECK(s.players[1].life == 0);
    CHECK(Lib(s) == 0);
    CHECK_FALSE(s.player_lost_on_draw);        // the 20th ping's draw never happened
}

TEST_CASE("Tandem Lookout: one card short, the mandatory draw from an EMPTY library loses (CR 704.5b)")
{
    EnsureCardsLoadedNiv();
    GameState s = FreshNiv(19);
    PutNiv(s, "Niv-Mizzet, the Firemind", 1);
    const int lk = PutNiv(s, "Tandem Lookout", 2);
    PairNiv(s, lk, 1);
    DrawThenFlush(s, 0, 1);
    CHECK(s.player_lost_on_draw);
    CHECK(s.players[1].life == 1);             // 19 pings; the loss stops the loop before a 20th
    CHECK_FALSE(OpponentHasLost(s));
}

TEST_CASE("Tandem Lookout's grant is on BOTH halves, and only while the pair is live")
{
    EnsureCardsLoadedNiv();
    // The Lookout itself connecting draws too (it is half of the pair).
    GameState s = FreshNiv();
    const int guide = PutNiv(s, "Goblin Guide", 3);
    const int lk    = PutNiv(s, "Tandem Lookout", 5);
    PairNiv(s, lk, 3);
    ResolveCombatDamage(s, { guide, lk }, 0, false);
    CHECK(Hand(s) == 2);                       // one per connecting half
    // Unpaired, it grants nothing.
    GameState t = FreshNiv();
    const int g2  = PutNiv(t, "Goblin Guide", 3);
    const int lk2 = PutNiv(t, "Tandem Lookout", 5);
    ResolveCombatDamage(t, { g2, lk2 }, 0, false);
    CHECK(Hand(t) == 0);
    // A pair whose partner left is broken (read-time verified): the Guide is gone, nothing draws.
    GameState u = FreshNiv();
    const int lk3 = PutNiv(u, "Tandem Lookout", 5);
    PairNiv(u, lk3, 3);                        // number 3 is not on the battlefield
    ResolveCombatDamage(u, { lk3 }, 0, false);
    CHECK(Hand(u) == 0);
}

TEST_CASE("Curiosity AND a paired Lookout on one Niv: two draw TRIGGERS per ping, but the stack draws ONE per ping")
{
    EnsureCardsLoadedNiv();
    // CR 405.5 / 603.3: a resolved ping's two draw triggers go ABOVE every older pending trigger, and the
    // draw's ping above them again -- draw, ping, draw, ping. The kill takes one draw per ping; the extra
    // triggers wait underneath and never resolve. Until 2026-10-18 the loop resolved breadth-first and drew
    // 1 + 2 x 19 = 39 cards here (claude-play sweep, seed 71009: 35 for an 18-ping kill).
    auto board = [](int library)
    {
        GameState s = FreshNiv(library);
        PutNiv(s, "Niv-Mizzet, the Firemind", 1);
        const int cur = PutNiv(s, "Curiosity", 2);
        AttachNiv(s, cur, 1);
        const int lk = PutNiv(s, "Tandem Lookout", 3);
        PairNiv(s, lk, 1);
        return s;
    };
    {
        GameState s = board(40);
        DrawThenFlush(s, 0, 1);
        CHECK(OpponentHasLost(s));
        CHECK(s.players[0].cards_drawn_this_turn == 20);   // the opening draw + one per ping 1..19
        CHECK(Lib(s) == 20);
        CHECK_FALSE(s.player_lost_on_draw);
    }
    {
        // Exactly enough: 20 cards, 20 draws -> the kill, never the empty-library loss. The breadth-first
        // loop LOST this (Lookout's mandatory draw from an empty library) at any library below 39.
        GameState s = board(20);
        DrawThenFlush(s, 0, 1);
        CHECK(OpponentHasLost(s));
        CHECK_FALSE(s.player_lost_on_draw);
        CHECK(Lib(s) == 0);
    }
    {
        // One card short: the 20th draw (Lookout's, mandatory) finds an empty library -> we lose first.
        GameState s = board(19);
        DrawThenFlush(s, 0, 1);
        CHECK(s.player_lost_on_draw);
        CHECK(s.players[1].life == 1);
    }
}

TEST_CASE("Two Nivs, Curiosity on ONE: each draw pings twice, the free ping first (the controller's order)")
{
    EnsureCardsLoadedNiv();
    GameState s = FreshNiv(40);
    PutNiv(s, "Niv-Mizzet, the Firemind", 1);
    PutNiv(s, "Niv-Mizzet, Parun", 4);
    const int cur = PutNiv(s, "Curiosity", 2);
    AttachNiv(s, cur, 1);
    DrawThenFlush(s, 0, 1);
    CHECK(OpponentHasLost(s));
    CHECK(s.players[0].cards_drawn_this_turn == 10);   // 2 damage per draw: 20 life in 10 draws
    CHECK(Lib(s) == 30);
}

TEST_CASE("Tandem Lookout pairs with a Niv over a BIGGER non-Niv creature (provider order)")
{
    EnsureCardsLoadedNiv();
    GameState s = FreshNiv();
    PutNiv(s, "Utvara Hellkite", 7);           // 6/6, the highest power on board
    PutNiv(s, "Niv-Mizzet, the Firemind", 1);  // 4/4, but it pings on every draw
    const int lk = PutNiv(s, "Tandem Lookout", 5, 0, /*sick=*/true);
    FireEtbWatchers(s, 0, lk);
    CHECK(s.battlefield[static_cast<std::size_t>(lk)].paired_with == 1);
    // Silverblade Paladin (no damage->draw payload) keeps the pure power order.
    GameState t = FreshNiv();
    PutNiv(t, "Utvara Hellkite", 7);
    PutNiv(t, "Niv-Mizzet, the Firemind", 1);
    const int sp = PutNiv(t, "Silverblade Paladin", 6, 0, true);
    FireEtbWatchers(t, 0, sp);
    CHECK(t.battlefield[static_cast<std::size_t>(sp)].paired_with == 7);
}

TEST_CASE("Tandem Lookout with no Niv to pair with: pair-now vs WAIT is a searched twin; with a Niv up it is not")
{
    EnsureCardsLoadedNiv();
    auto board = [](bool niv_up)
    {
        GameState s = FreshNiv();
        for (int k = 0; k < 3; ++k) { PutNiv(s, "Island", 50 + k); }
        PutNiv(s, "Goblin Guide", 3);          // a 2-power partner: no WEAK-partner twin for it
        if (niv_up) { PutNiv(s, "Niv-Mizzet, the Firemind", 1); }
        s.players[0].hand.push_back(CardNiv("Tandem Lookout", 5));
        return s;
    };
    auto twins = [](const GameState& s)
    {
        int n = 0;
        for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(s, true))
        {
            if (!p.soulbond_decline) { continue; }
            for (const Action& a : p.actions)
            { if (a.card_name.str() == "Tandem Lookout") { ++n; break; } }
        }
        return n;
    };
    CHECK(twins(board(false)) >= 1);
    CHECK(twins(board(true)) == 0);
    // (b) An UNPAIRED Lookout already on the battlefield: casting a non-Niv creature would pair them
    // (Trigger B), so the plan gets a "wait" twin -- unless a Niv is on our battlefield, which the
    // Lookout can then never pair with anyway (Trigger B offers only the entrant).
    auto board_b = [](bool niv_on_bf)
    {
        GameState s = FreshNiv();
        for (int k = 0; k < 2; ++k) { PutNiv(s, "Island", 50 + k); }
        PutNiv(s, "Mountain", 52);
        PutNiv(s, "Tandem Lookout", 5);
        if (niv_on_bf) { PutNiv(s, "Niv-Mizzet, the Firemind", 1); }
        s.players[0].hand.push_back(CardNiv("Goblin Guide", 3));
        return s;
    };
    auto guide_twins = [](const GameState& s)
    {
        int n = 0;
        for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(s, true))
        {
            if (!p.soulbond_decline) { continue; }
            for (const Action& a : p.actions)
            { if (a.card_name.str() == "Goblin Guide") { ++n; break; } }
        }
        return n;
    };
    CHECK(guide_twins(board_b(false)) >= 1);
    CHECK(guide_twins(board_b(true)) == 0);
    // The twin really declines: applied, the Lookout stays unpaired; the base plan pairs it.
    {
        const GameState s = board_b(false);
        int paired_base = -1, paired_twin = -1;
        for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(s, true))
        {
            bool guide = false;
            for (const Action& a : p.actions) { if (a.card_name.str() == "Goblin Guide") { guide = true; } }
            if (!guide) { continue; }
            GameState r = s;
            TurnSolver::ApplyPlan(r, p, true);
            int pw = 0;
            for (const Permanent& q : r.battlefield) { if (q.card.m_number == 5) { pw = q.paired_with; } }
            (p.soulbond_decline ? paired_twin : paired_base) = pw;
        }
        CHECK(paired_base == 3);
        CHECK(paired_twin == 0);
    }
}

TEST_CASE("Curiosity's HOST: the Aura host ranking puts it on the NIV, and the autonomous plan list holds the kill")
{
    EnsureCardsLoadedNiv();
    // Curiosity adds no power, so every host scores the same team damage: the old tie-breaks kept the
    // Guide (first enumerated; and the lower card number at resolution). The Niv-loop term must rank
    // the Niv (#9) first anyway.
    GameState s = FreshNiv(40);
    PutNiv(s, "Island", 50);
    PutNiv(s, "Island", 51);
    PutNiv(s, "Goblin Guide", 3);
    PutNiv(s, "Niv-Mizzet, the Firemind", 9, 0, /*sick=*/true);
    s.players[0].hand.push_back(CardNiv("Curiosity", 10));
    s.players[0].hand.push_back(CardNiv("Preordain", 11));
    // BOTH plan-list modes: searched play (any AIEngine at depth > 0 sets it, thread-locally) hoists
    // plain cantrips first (cantrip-first), and the payoffs must still precede Preordain there -- the
    // kill was missing from every SEARCHED list until MayPrecedeCantrip's VALUE clause.
    const bool saved_searched = g_searched_play;
    for (const bool searched : { false, true })
    {
        TurnSolver::SetSearchedPlay(searched);
        INFO("searched=" << searched);
        bool kill = false, host_niv = false, host_guide = false;
        for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(s, true))
        {
            for (const Action& a : p.actions)
            {
                if (a.card_name.str() != "Curiosity") { continue; }
                if (a.enchant_target == 9) { host_niv = true; }
                if (a.enchant_target == 3) { host_guide = true; }
            }
            GameState r = s;
            TurnSolver::ApplyPlan(r, p, true);
            if (OpponentHasLost(r)) { kill = true; }
        }
        CHECK(host_niv);
        CHECK_FALSE(host_guide);               // the dedup keeps the ranking's pick, not battlefield order
        CHECK(kill);                           // ...and Curiosity -> Preordain on it is the kill
    }
    TurnSolver::SetSearchedPlay(saved_searched);
    // The resolution-time fallback agrees (one ranking, both worlds).
    GameState t = s;
    const int cur = PutNiv(t, "Curiosity", 12);
    CHECK(AuraCastHostByRank(t, 0, cur) == 9);
    // A deck stamped without the loop keeps the old order exactly (the tie goes to the lower number).
    GameState u = s;
    u.deck_has_draw_ping = false;
    const int cu = PutNiv(u, "Curiosity", 12);
    CHECK(AuraCastHostByRank(u, 0, cu) == 3);
}

// ---- 4. Parun's cast trigger and Firemind's activation -----------------------------------------------

TEST_CASE("Niv-Mizzet, Parun: an instant/sorcery CAST draws a card and that draw pings; a creature cast does not")
{
    EnsureCardsLoadedNiv();
    GameState s = FreshNiv();
    PutNiv(s, "Niv-Mizzet, Parun", 1);
    FireOnCastTriggers(s, DefNiv("Preordain"));
    CHECK(Hand(s) == 1);
    CHECK(s.players[1].life == 19);            // the cast trigger's draw pinged before the spell resolves
    FireOnCastTriggers(s, DefNiv("Tandem Lookout"));
    CHECK(Hand(s) == 1);
    CHECK(s.players[1].life == 19);
    // Mandatory: from an empty library it loses.
    GameState t = FreshNiv(0);
    PutNiv(t, "Niv-Mizzet, Parun", 1);
    FireOnCastTriggers(t, DefNiv("Ponder"));
    CHECK(t.player_lost_on_draw);
}

TEST_CASE("Niv-Mizzet, the Firemind: {T}: Draw a card pings through the shared activation, sickness applies")
{
    EnsureCardsLoadedNiv();
    GameState s = FreshNiv();
    const int niv = PutNiv(s, "Niv-Mizzet, the Firemind", 1);
    REQUIRE(DefNiv("Niv-Mizzet, the Firemind").params.tap_draw_cost.has_value());
    CHECK(DefNiv("Niv-Mizzet, the Firemind").params.tap_draw_cost->ManaValue() == 0);
    ApplyPermAbility(s, 0, 1, PermAbilityMode::TapDraw);
    CHECK(Hand(s) == 1);
    CHECK(s.players[1].life == 19);
    CHECK(CanTapNow(s.battlefield[static_cast<std::size_t>(niv)], s.battlefield));
    // A summoning-sick Firemind cannot pay the {T}.
    GameState t = FreshNiv();
    const int sick = PutNiv(t, "Niv-Mizzet, the Firemind", 1, 0, /*sick=*/true);
    CHECK_FALSE(CanTapNow(t.battlefield[static_cast<std::size_t>(sick)], t.battlefield));
    // The activation is a main-phase plan action of the enumerated menu when it can tap.
    bool offered = false;
    for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(s, true))
    {
        for (const Action& a : p.actions)
        {
            if (a.kind == Action::Kind::ActivatePermAbility && a.ability_mode == Action::AbilityMode::TapDraw)
            { offered = true; }
        }
    }
    CHECK(offered);
}

// ---- 4b. search completeness: what the activation draws is castable the same phase -----------------

TEST_CASE("Firemind {T} draw into an EMPTY hand: the drawn card is castable the same phase (main 1 and main 2)")
{
    EnsureCardsLoadedNiv();
    // Opponent at 4, one Mountain, empty hand, Lightning Bolt on top: tap (ping, 3) -> Bolt (0). The
    // draw is outside any cast window, so only the section hand-entry breakpoint can re-decide on it,
    // and with an EMPTY section snapshot HandGainedACard reads "no snapshot" -- the reason
    // SectionHandGainedACard exists. Each main: the searched pick, applied, kills.
    auto board = [](bool loop_deck)
    {
        GameState s = FreshNiv(0);
        s.players[0].library.push_back(CardNiv("Lightning Bolt", 30));
        for (int k = 0; k < 20; ++k) { s.players[0].library.push_back(CardNiv("Island", 900 + k)); }
        s.players[1].life = 4;
        s.deck_has_draw_ping = loop_deck;
        PutNiv(s, "Mountain", 50);
        PutNiv(s, "Niv-Mizzet, the Firemind", 1);
        return s;
    };
    for (int pre = 0; pre < 2; ++pre)
    {
        CAPTURE(pre);
        GameState s = board(true);
        s.phase = pre ? Phase::PreCombatMain : Phase::PostCombatMain;
        const TurnSolver::Plan pick = TurnSolver::SolveWithLookahead(s, pre == 1, 3, s.turn_number);
        GameState r = s;
        TurnSolver::ApplyPlan(r, pick, pre == 1);
        CHECK(OpponentHasLost(r));
        CHECK(r.players[0].hand.empty());          // the Bolt was cast, not held
    }
    // The helper's contract: an empty SECTION snapshot is a real empty hand -- for EVERY deck (the hole
    // is general; it was deck-gated at first and un-gated as a measured play change).
    GameState g = board(true);
    g.players[0].hand.push_back(CardNiv("Lightning Bolt", 31));
    CHECK(TurnSolver::SectionHandGainedACard({}, g));
    CHECK_FALSE(TurnSolver::HandGainedACard({}, g));
    g.deck_has_draw_ping = false;                   // a deck without the loop: the same answer
    CHECK(TurnSolver::SectionHandGainedACard({}, g));
    g.players[0].hand.clear();
    CHECK_FALSE(TurnSolver::SectionHandGainedACard({}, g));   // still empty: nothing entered
}

// ---- 5. lockstep: the executor and the rollout realise the same line ----------------------------------

TEST_CASE("Niv loop LOCKSTEP: rollout ApplyPlan and the executor's TakeTurn realise every Curiosity line identically")
{
    EnsureCardsLoadedNiv();
    // Firemind (not sick) + two Islands; hand Curiosity + Preordain; library 12 -- too short for the
    // loop to kill from 20, so the lines END in different, comparable states (the kill would hide a
    // divergence behind "both won").
    auto board = []()
    {
        GameState s = FreshNiv(12);
        PutNiv(s, "Niv-Mizzet, the Firemind", 1);
        PutNiv(s, "Island", 50);
        PutNiv(s, "Island", 51);
        s.players[0].hand.push_back(CardNiv("Curiosity", 10));
        s.players[0].hand.push_back(CardNiv("Preordain", 11));
        return s;
    };
    const GameState s = board();
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, true);
    int compared = 0, curiosity_on_niv = 0;
    for (const TurnSolver::Plan& p : plans)
    {
        bool casts_curiosity = false;
        for (const Action& a : p.actions)
        {
            if (a.card_name.str() == "Curiosity") { casts_curiosity = true; if (a.enchant_target == 1) { ++curiosity_on_niv; } }
        }
        if (!casts_curiosity) { continue; }
        GameState roll = s;
        TurnSolver::ApplyPlan(roll, p, /*is_pre_combat=*/true);
        AIEngine eng(MulliganProfile::DefaultProfile(), 3, 20);
        MtgTestSeam::CommitPhasePlan(eng, p, true);
        GameState exec = s;
        eng.TakeTurn(exec, true, ResolveAllNiv);
        CAPTURE(p.actions.size());
        CHECK(roll.players[1].life == exec.players[1].life);
        CHECK(roll.players[0].cards_drawn_this_turn == exec.players[0].cards_drawn_this_turn);
        CHECK(Lib(roll) == Lib(exec));
        CHECK(Hand(roll) == Hand(exec));
        CHECK(roll.player_lost_on_draw == exec.player_lost_on_draw);
        ++compared;
    }
    CHECK(curiosity_on_niv >= 1);              // the host is a searched variant and the Niv is one
    CHECK(compared >= 1);
}



TEST_CASE("Cantrip-first carries a hoisted Aura's SAME-PLAN host with it (searched play)")
{
    EnsureCardsLoadedNiv();
    // Searched play hoists plain cantrips first, except payoffs (MayPrecedeCantrip's VALUE clause), so
    // Curiosity goes ahead of Preordain. Its host here is cast in the SAME plan (a Generator Servant
    // from hand): the Servant must move up with it, or Curiosity would come first, have no legal target
    // (AuraSpellCastLegal) and never be cast -- the line would be in no plan at all.
    // USER 2026-10-31 (the Niv cast order, MTG_NIV_CAST_ORDER, Q2): the hoist holds only when Curiosity's
    // host is a Niv -- on a Servant, with no Niv anywhere, it waits behind the cantrip. The host still
    // comes first either way, and the Aura still lands on it.
    for (const bool niv_order : { false, true })
    {
        INFO("MTG_NIV_CAST_ORDER=" << niv_order);
        heurarm::t_arm[heurarm::NIV_CAST_ORDER] = niv_order ? 1 : 0;
        GameState s = FreshNiv(niv_order ? 41 : 40);
        PutNiv(s, "Island", 50);
        PutNiv(s, "Island", 51);
        PutNiv(s, "Mountain", 52);
        PutNiv(s, "Mountain", 53);
        s.players[0].hand.push_back(CardNiv("Generator Servant", 12));
        s.players[0].hand.push_back(CardNiv("Curiosity", 10));
        s.players[0].hand.push_back(CardNiv("Preordain", 11));
        const bool saved_searched = g_searched_play;
        TurnSolver::SetSearchedPlay(true);
        const long long before = g_enchant_retargets.load();
        bool found = false;
        for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(s, true))
        {
            int servant = -1, curiosity = -1, preordain = -1;
            for (std::size_t i = 0; i < p.actions.size(); ++i)
            {
                const Action& a = p.actions[i];
                if (a.kind != Action::Kind::CastFromHand) { continue; }
                if (a.card_name.str() == "Generator Servant") { servant = static_cast<int>(i); }
                if (a.card_name.str() == "Curiosity" && a.enchant_target == 12) { curiosity = static_cast<int>(i); }
                if (a.card_name.str() == "Preordain") { preordain = static_cast<int>(i); }
            }
            if (servant < 0 || curiosity < 0 || preordain < 0) { continue; }
            found = true;
            CHECK(servant < curiosity);     // the host first...
            if (niv_order) { CHECK(preordain < curiosity); }   // ...no Niv: the payoff waits for the cantrip
            else           { CHECK(curiosity < preordain); }   // ...and the payoff still ahead of the cantrip
            GameState r = s;
            TurnSolver::ApplyPlan(r, p, true);
            bool attached = false;
            for (const Permanent& q : r.battlefield) { if (q.card.m_number == 10 && q.aura_attached_to == 12) { attached = true; } }
            CHECK(attached);
        }
        CHECK(found);
        CHECK(g_enchant_retargets.load() == before);
        TurnSolver::SetSearchedPlay(saved_searched);
        heurarm::t_arm[heurarm::NIV_CAST_ORDER] = -1;
    }
}
