// The viewer's MANUAL TAP for ARTIFACT and CREATURE sources (docs/design/viewer-manual-tap-pay.md).
//
// USER, 2026-10-22, hand-playing NivMizzetCombo: "Can you also change the Tap Mana feature to allow me
// to use artifact and creature sources?" The first version of the feature kept three classes for the
// engine's payer -- one-shot sacrifice sources, mana-CONVERSION sources (filters) and feed-cost lands --
// which in that deck left Izzet Signet, Ferrous Lake, Cascade Bluffs and Generator Servant un-tappable
// by hand. They are offered now, each through the mechanic the payer / executor already uses for it:
//
//   1. GENERATOR SERVANT ({T}, sacrifice: {C}{C}, haste rider) -- ApplySacForMana, the executor's own
//      crack: the float is {C}{C}, BOTH haste-tagged (GameState::floating_haste_c), the Servant is in
//      the graveyard, and the next creature spell spends a tagged {C} and enters with haste -- in the
//      rollout AND the executor. A summoning-sick Servant is refused (CR 302.6), a hasted one is not.
//   2. IZZET SIGNET ({1},{T}: {U}{R}) -- the {1} is paid FROM THE FLOATING POOL the human has already
//      filled. With nothing floating the tap is REJECTED with what to do ("tap a source for the
//      Izzet Signet's {1} first") and nothing moves; after an Island's {U} it floats {U}{R}.
//   3. CASCADE BLUFFS ({U/R},{T}: {U}{U}, {U}{R} or {R}{R}) -- a MULTI-LETTER face names the output
//      (`:UU` / `:UR` / `:RR`, matched as a multiset), the {U/R} feed takes only a {U} or {R}, and its
//      free "{T}: Add {C}" mode is still a one-letter face.
//   4. BACKWARD COMPATIBILITY -- a single-letter token parses and behaves exactly as before.
//   5. The OFFER and the EXECUTOR agree face by face over a board of every new class (the relation
//      test_fungus_revision.cpp pins for the original classes, extended to fed faces).
//   6. What stays excluded: restricted mana (creature-only), which the pool would launder.
#include <doctest/doctest.h>

#include "cards/CardDatabase.h"
#include "core/EffectHandler.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"
#include "ai/AIEngine.h"
#include "ai/ManaPayment.h"
#include "ai/TurnSolver.h"
#include "mtg_test_seam.h"

#include <algorithm>
#include <string>
#include <vector>

namespace
{

void EnsureCardsMt()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

const CardDefinition& DefMt(const std::string& name)
{
    EnsureCardsMt();
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE_MESSAGE(d != nullptr, "card not in cards.json: ", name);
    return *d;
}

int PutMt(GameState& s, const std::string& name, int number, bool sick = false)
{
    Permanent p;
    p.card              = DefMt(name).card;
    p.card.m_number     = number;
    p.controller_index  = 0;
    p.owner_index       = 0;
    p.entered_this_turn = sick;
    s.battlefield.push_back(p);
    return static_cast<int>(s.battlefield.size()) - 1;
}

void HandMt(GameState& s, const std::string& name, int number)
{
    Card c = DefMt(name).card;
    c.m_number = number;
    s.players[0].hand.push_back(c);
}

GameState FreshMt()
{
    EnsureCardsMt();
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 4;
    s.game_seed           = 4242;
    s.players[0].life     = 20;
    s.players[1].life     = 20;
    s.players[0].lands_played_this_turn = 1;   // no land drop muddies the plan search
    for (int k = 0; k < 8; ++k)
    {
        Card c = DefMt("Lightning Bolt").card;
        c.m_number = 200 + k;
        s.players[0].library.push_back(c);
    }
    return s;
}

const Permanent* FindMt(const GameState& s, int number)
{
    for (const Permanent& p : s.battlefield) { if (p.card.m_number == number) { return &p; } }
    return nullptr;
}

bool InGyMt(const GameState& s, int number)
{
    for (const Card& c : s.players[0].graveyard) { if (c.m_number == number) { return true; } }
    return false;
}

// Parse a token exactly as the viewer's click arrives (the shared reader), then apply it.
std::string Tap(GameState& s, const std::string& tok)
{
    TurnSolver::PreTap t;
    REQUIRE_MESSAGE(ParseHumanPreTapToken(tok, t), "token did not parse: ", tok);
    return ApplyHumanPreTap(s, t);
}

void ResolveStackMt(GameState& st)
{
    while (!st.stack.empty())
    {
        const StackEntry e = st.stack.back();
        st.stack.pop_back();
        const CardDefinition* d = CardDatabase::Instance().LookupCached(e.source);
        if (d != nullptr) { EffectHandler::Resolve(st, e, *d); }
    }
}

GameState RunExecutorMt(const GameState& s, const TurnSolver::Plan& plan)
{
    AIEngine eng(MulliganProfile::DefaultProfile(), 3, 20);
    MtgTestSeam::CommitPhasePlan(eng, plan, true);
    GameState exec = s;
    eng.TakeTurn(exec, true, ResolveStackMt);
    return exec;
}

// The plan that casts exactly `name` from hand and nothing else (no land, no sacrifice).
const TurnSolver::Plan* OnlyCastMt(const std::vector<TurnSolver::Plan>& plans, const std::string& name)
{
    for (const TurnSolver::Plan& p : plans)
    {
        if (!p.land_to_play.empty() || p.actions.size() != 1) { continue; }
        const Action& a = p.actions[0];
        if (a.kind == Action::Kind::CastFromHand && a.card_name == name) { return &p; }
    }
    return nullptr;
}
const TurnSolver::Plan* OnlyCastMt(std::vector<TurnSolver::Plan>&&, const std::string&) = delete;

bool Contains(const std::string& hay, const std::string& needle) { return hay.find(needle) != std::string::npos; }

}   // namespace

// ---------------------------------------------------------------------------------------------------
TEST_CASE("Manual tap / Generator Servant: the pre-tap IS the executor's crack -- {C}{C} tagged, Servant gone")
{
    GameState s = FreshMt();
    PutMt(s, "Generator Servant", 10);
    PutMt(s, "Mountain", 1);

    const Permanent& gs = *FindMt(s, 10);
    const HumanPreTapOffer o = HumanPreTapOfferFor(s, gs);
    CHECK(o.route == HumanPreTapOffer::Route::SacAction);
    CHECK(o.free_faces == "C");          // pinned {C}: one face, the viewer asks nothing
    CHECK(o.fed_faces.empty());
    CHECK(o.sacrifice);
    CHECK(o.sac_amount == 2);
    CHECK(Contains(o.note, "haste"));    // the dialog / chip can say what the sacrifice buys
    CHECK(HumanPreTapFaces(s, gs) == "C");

    CHECK(Tap(s, "tap=Generator Servant#10:C") == "");
    CHECK(s.floating_mana.colorless == 2);
    CHECK(s.floating_mana.Total() == 2);
    CHECK(s.floating_haste_c == 2);      // exactly as ApplySacForMana tags a normal crack
    CHECK(HasteFloatUnits(s) == 2);
    CHECK(FindMt(s, 10) == nullptr);     // sacrificed...
    CHECK(InGyMt(s, 10));                // ...through the shared chokepoint (a creature death)
    CHECK_FALSE(FindMt(s, 1)->tapped);   // the Mountain is the human's to keep

    // A second tap of the same Servant: it is gone, and the reason says so.
    CHECK(Contains(Tap(s, "tap=Generator Servant#10:C"), "you control no untapped 'Generator Servant'"));
    // Any other colour is refused with the face list.
    GameState t = FreshMt();
    PutMt(t, "Generator Servant", 10);
    CHECK(Contains(Tap(t, "tap=Generator Servant#10:R"), "cannot produce {R} (it makes {C})"));
    CHECK(FindMt(t, 10) != nullptr);
    CHECK(t.floating_mana.Total() == 0);
}

TEST_CASE("Manual tap / Generator Servant: the next creature spell spends a tagged {C} and gains haste, both worlds")
{
    // Servant A (10) is hand-tapped; Servant B (30, {1}{R}) is then cast off the float + the Mountain.
    GameState s = FreshMt();
    PutMt(s, "Generator Servant", 10);
    PutMt(s, "Mountain", 1);
    HandMt(s, "Generator Servant", 30);
    REQUIRE(Tap(s, "tap=Generator Servant#10:C") == "");
    REQUIRE(s.floating_haste_c == 2);

    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, true);
    const TurnSolver::Plan* p = OnlyCastMt(plans, "Generator Servant");
    REQUIRE(p != nullptr);
    GameState roll = s;
    TurnSolver::ApplyPlan(roll, *p, true);
    GameState exec = RunExecutorMt(s, *p);
    for (const GameState* g : { &roll, &exec })
    {
        INFO("world: " << (g == &roll ? "rollout (ApplyPlan)" : "executor (AIEngine::TakeTurn)"));
        const Permanent* b = FindMt(*g, 30);
        REQUIRE(b != nullptr);
        CHECK(b->entered_this_turn);
        CHECK(b->temp_haste);                         // "it gains haste until end of turn"
        CHECK(CanTapNow(*b, g->battlefield));         // ...so B is itself crackable at once
        CHECK(HumanPreTapFaces(*g, *b) == "C");       // and the viewer offers it
    }
}

TEST_CASE("Manual tap / Generator Servant: summoning sick is refused (CR 302.6); haste lifts it")
{
    GameState s = FreshMt();
    PutMt(s, "Generator Servant", 10, /*sick=*/true);
    const HumanPreTapOffer o = HumanPreTapOfferFor(s, *FindMt(s, 10));
    CHECK(o.free_faces.empty());
    CHECK(o.fed_faces.empty());
    CHECK(Contains(o.why_not, "summoning sick"));
    const std::string why = Tap(s, "tap=Generator Servant#10:C");
    CHECK(Contains(why, "cannot be tapped by hand"));
    CHECK(Contains(why, "summoning sick"));
    CHECK(FindMt(s, 10) != nullptr);                  // nothing happened
    CHECK(s.floating_mana.Total() == 0);
    CHECK(s.floating_haste_c == 0);

    // Hasted (another Servant's mana was spent on it): offered, and the crack works.
    GameState h = FreshMt();
    const int bi = PutMt(h, "Generator Servant", 10, /*sick=*/true);
    h.battlefield[bi].temp_haste = true;
    CHECK(HumanPreTapFaces(h, h.battlefield[bi]) == "C");
    CHECK(Tap(h, "tap=Generator Servant#10:C") == "");
    CHECK(h.floating_haste_c == 2);
}

// ---------------------------------------------------------------------------------------------------
TEST_CASE("Manual tap / Izzet Signet: the {1} is paid from the floating pool, or the tap is refused with what to do")
{
    // Nothing floating: REJECTED, and nothing moves (the engine does not tap a feeder on the
    // human's behalf -- choosing that source is the allocation a hand tap exists to override).
    GameState s = FreshMt();
    PutMt(s, "Island", 1);
    PutMt(s, "Izzet Signet", 20);
    const HumanPreTapOffer o = HumanPreTapOfferFor(s, *FindMt(s, 20));
    CHECK(o.route == HumanPreTapOffer::Route::Conversion);
    CHECK(o.free_faces.empty());                                   // no free mode
    CHECK(o.fed_faces == std::vector<std::string>{ "UR" });        // one fixed output: no choice
    CHECK(o.feed == "{1}");
    CHECK(HumanPreTapFaces(s, *FindMt(s, 20)) == "");
    CHECK(HumanPreTapFedFaces(s, *FindMt(s, 20)) == std::vector<std::string>{ "UR" });

    const std::string why = Tap(s, "tap=Izzet Signet#20:UR");
    CHECK(Contains(why, "tap a source for the Izzet Signet's {1} first"));
    CHECK_FALSE(FindMt(s, 20)->tapped);
    CHECK_FALSE(FindMt(s, 1)->tapped);
    CHECK(s.floating_mana.Total() == 0);

    // Island first, then the Signet: the {U} feeds the {1}, the Signet adds {U}{R}.
    CHECK(Tap(s, "tap=Island#1:U") == "");
    CHECK(s.floating_mana.blue == 1);
    CHECK(Tap(s, "tap=Izzet Signet#20:UR") == "");
    CHECK(FindMt(s, 20)->tapped);
    CHECK(s.floating_mana.blue == 1);
    CHECK(s.floating_mana.red == 1);
    CHECK(s.floating_mana.Total() == 2);

    // The face is a MULTISET: `:RU` is the same output.
    GameState r = FreshMt();
    PutMt(r, "Island", 1);
    PutMt(r, "Izzet Signet", 20);
    REQUIRE(Tap(r, "tap=Island#1:U") == "");
    CHECK(Tap(r, "tap=Izzet Signet#20:RU") == "");
    CHECK(r.floating_mana.blue == 1);
    CHECK(r.floating_mana.red == 1);

    // A face the Signet cannot make is refused with the list (one letter or several).
    GameState u = FreshMt();
    PutMt(u, "Island", 1);
    PutMt(u, "Izzet Signet", 20);
    REQUIRE(Tap(u, "tap=Island#1:U") == "");
    CHECK(Contains(Tap(u, "tap=Izzet Signet#20:U"),  "cannot produce {U} (it makes {U}{R} for {1})"));
    CHECK(Contains(Tap(u, "tap=Izzet Signet#20:UU"), "cannot produce {U}{U}"));
    CHECK(u.floating_mana.blue == 1);                 // both refusals left the pool alone
    CHECK_FALSE(FindMt(u, 20)->tapped);
}

TEST_CASE("Manual tap / Izzet Signet fed by Generator Servant: the {1} eats a {C}, and its haste tag goes with it")
{
    // The disclosed residue of the haste rider: a {C} spent on the Signet's ability is not spent on a
    // creature spell. The human controls it by ORDER -- tap the Island for the Signet first, or not.
    GameState s = FreshMt();
    PutMt(s, "Generator Servant", 10);
    PutMt(s, "Izzet Signet", 20);
    REQUIRE(Tap(s, "tap=Generator Servant#10:C") == "");
    REQUIRE(s.floating_haste_c == 2);
    CHECK(Tap(s, "tap=Izzet Signet#20:UR") == "");
    CHECK(s.floating_mana.colorless == 1);
    CHECK(s.floating_mana.blue == 1);
    CHECK(s.floating_mana.red == 1);
    CHECK(HasteFloatUnits(s) == 1);                   // the surviving {C} keeps its tag
}

// ---------------------------------------------------------------------------------------------------
TEST_CASE("Manual tap / Cascade Bluffs: multi-letter combos, the {U/R} feed, and the free {C} mode")
{
    GameState s = FreshMt();
    PutMt(s, "Island", 1);
    PutMt(s, "Cascade Bluffs", 5);
    const HumanPreTapOffer o = HumanPreTapOfferFor(s, *FindMt(s, 5));
    CHECK(o.route == HumanPreTapOffer::Route::Conversion);
    CHECK(o.free_faces == "C");                                                  // {T}: Add {C}
    CHECK(o.fed_faces == std::vector<std::string>{ "UU", "UR", "RR" });         // the player's choice
    CHECK(o.feed == "{U/R}");

    // {U} in, {R}{R} out.
    {
        GameState g = s;
        REQUIRE(Tap(g, "tap=Island#1:U") == "");
        CHECK(Tap(g, "tap=Cascade Bluffs#5:RR") == "");
        CHECK(g.floating_mana.blue == 0);
        CHECK(g.floating_mana.red == 2);
        CHECK(g.floating_mana.Total() == 2);
        CHECK(FindMt(g, 5)->tapped);
    }
    // {U} in, {U}{R} out -- written either way round.
    for (const char* face : { "UR", "RU" })
    {
        GameState g = s;
        REQUIRE(Tap(g, "tap=Island#1:U") == "");
        CHECK(Tap(g, std::string("tap=Cascade Bluffs#5:") + face) == "");
        CHECK(g.floating_mana.blue == 1);
        CHECK(g.floating_mana.red == 1);
    }
    // {U} in, {U}{U} out.
    {
        GameState g = s;
        REQUIRE(Tap(g, "tap=Island#1:U") == "");
        CHECK(Tap(g, "tap=Cascade Bluffs#5:UU") == "");
        CHECK(g.floating_mana.blue == 2);
    }
    // The free mode needs no feed at all.
    {
        GameState g = s;
        CHECK(Tap(g, "tap=Cascade Bluffs#5:C") == "");
        CHECK(g.floating_mana.colorless == 1);
        CHECK(g.floating_mana.Total() == 1);
        CHECK(FindMt(g, 5)->tapped);
        CHECK_FALSE(FindMt(g, 1)->tapped);
    }
    // No feed in the pool -> refused, naming the hybrid cost.
    {
        GameState g = s;
        const std::string why = Tap(g, "tap=Cascade Bluffs#5:UR");
        CHECK(Contains(why, "tap a source for the Cascade Bluffs's {U/R} first"));
        CHECK_FALSE(FindMt(g, 5)->tapped);
    }
    // A {C} cannot pay a hybrid {U/R} pip: Sol Ring's {C}{C} floating is not a feed.
    {
        GameState g = s;
        PutMt(g, "Sol Ring", 7);
        REQUIRE(Tap(g, "tap=Sol Ring#7:C") == "");
        REQUIRE(g.floating_mana.colorless == 2);
        CHECK(Contains(Tap(g, "tap=Cascade Bluffs#5:UU"), "{U/R}"));
        CHECK(g.floating_mana.colorless == 2);        // untouched by the refusal
        CHECK_FALSE(FindMt(g, 5)->tapped);
    }
    // An output the land cannot make.
    {
        GameState g = s;
        REQUIRE(Tap(g, "tap=Island#1:U") == "");
        CHECK(Contains(Tap(g, "tap=Cascade Bluffs#5:UB"), "cannot produce {U}{B}"));
        CHECK(Contains(Tap(g, "tap=Cascade Bluffs#5:URR"), "cannot produce {U}{R}{R}"));
    }
    // A malformed combo is "no colour", never a guess at the first letter.
    {
        GameState g = s;
        TurnSolver::PreTap t;
        REQUIRE(ParseHumanPreTapToken("tap=Cascade Bluffs#5:UX", t));
        CHECK(t.combo.empty());
        CHECK(t.color == -1);
        CHECK(Contains(ApplyHumanPreTap(g, t), "names no colour"));
    }
}

TEST_CASE("Manual tap / Ferrous Lake (a ramp-filter LAND) takes the same conversion path")
{
    GameState s = FreshMt();
    PutMt(s, "Mountain", 1);
    PutMt(s, "Ferrous Lake", 6);
    CHECK(HumanPreTapFaces(s, *FindMt(s, 6)) == "");
    CHECK(HumanPreTapFedFaces(s, *FindMt(s, 6)) == std::vector<std::string>{ "UR" });
    CHECK(Contains(Tap(s, "tap=Ferrous Lake#6:UR"), "first"));
    REQUIRE(Tap(s, "tap=Mountain#1:R") == "");
    CHECK(Tap(s, "tap=Ferrous Lake#6:UR") == "");
    CHECK(s.floating_mana.blue == 1);
    CHECK(s.floating_mana.red == 1);
    CHECK(FindMt(s, 6)->tapped);
}

// ---------------------------------------------------------------------------------------------------
TEST_CASE("Manual tap: an old single-letter token parses and behaves exactly as before")
{
    TurnSolver::PreTap t;
    REQUIRE(ParseHumanPreTapToken("tap=Island#1:U", t));
    CHECK(t.name == "Island");
    CHECK(t.num == 1);
    CHECK(t.color == static_cast<int>(Color::Blue));
    CHECK(t.combo.empty());
    CHECK(t.aura_colors.empty());
    CHECK(HumanPreTapFaceText(t) == "{U}");

    TurnSolver::PreTap a;
    REQUIRE(ParseHumanPreTapToken("tap=Brushland#7:W+U", a));
    CHECK(a.color == static_cast<int>(Color::White));
    CHECK(a.combo.empty());
    CHECK(a.aura_colors == std::vector<int>{ static_cast<int>(Color::Blue) });

    TurnSolver::PreTap c;
    REQUIRE(ParseHumanPreTapToken("tap=Izzet Signet#20:UR", c));
    CHECK(c.color == -1);
    CHECK(c.combo == std::vector<int>{ static_cast<int>(Color::Blue), static_cast<int>(Color::Red) });
    CHECK(HumanPreTapFaceText(c) == "{U}{R}");

    // A plain land and a plain rock: same faces, same float, same refusal wording as the first version.
    GameState s = FreshMt();
    PutMt(s, "Island", 1);
    PutMt(s, "Sol Ring", 7);
    PutMt(s, "Spirebluff Canal", 3);
    CHECK(HumanPreTapFaces(s, *FindMt(s, 1)) == "U");
    CHECK(HumanPreTapFaces(s, *FindMt(s, 7)) == "C");
    CHECK(HumanPreTapFaces(s, *FindMt(s, 3)) == "UR");
    for (int n : { 1, 7, 3 })
    {
        CHECK(HumanPreTapFedFaces(s, *FindMt(s, n)).empty());
        const HumanPreTapOffer o = HumanPreTapOfferFor(s, *FindMt(s, n));
        CHECK(o.route == HumanPreTapOffer::Route::Plain);
        CHECK_FALSE(o.sacrifice);
        CHECK(o.note.empty());
    }
    CHECK(Tap(s, "tap=Island#1:U") == "");
    CHECK(Tap(s, "tap=Sol Ring#7:C") == "");
    CHECK(Tap(s, "tap=Spirebluff Canal#3:R") == "");
    CHECK(s.floating_mana.blue == 1);
    CHECK(s.floating_mana.colorless == 2);
    CHECK(s.floating_mana.red == 1);
    CHECK(s.floating_haste_c == 0);
    GameState t2 = FreshMt();
    PutMt(t2, "Island", 1);
    CHECK(Tap(t2, "tap=Island#1:G") == "'Island' cannot produce {G} (it makes {U})");
    CHECK(Contains(Tap(t2, "tap=Island#1"), "names no colour"));
}

// ---------------------------------------------------------------------------------------------------
TEST_CASE("Manual tap: Treasure, Eldrazi Spawn and Lotus Bloom crack through the payer's / executor's own route")
{
    GameState s = FreshMt();
    PutMt(s, "Treasure Token", 1001);
    PutMt(s, "0/1 Eldrazi Spawn Token", 1002, /*sick=*/true);   // "Sacrifice this creature" -- no {T}
    PutMt(s, "Lotus Bloom", 40);

    const HumanPreTapOffer tr = HumanPreTapOfferFor(s, *FindMt(s, 1001));
    CHECK(tr.sacrifice);
    CHECK(tr.free_faces == "WUBRG");
    CHECK(tr.route == (IsPaySacSource(DefMt("Treasure Token")) ? HumanPreTapOffer::Route::PaySac
                                                              : HumanPreTapOffer::Route::SacAction));
    const HumanPreTapOffer sp = HumanPreTapOfferFor(s, *FindMt(s, 1002));
    CHECK(sp.free_faces == "C");                      // a sick Spawn is fine: its cost has no {T}
    CHECK(sp.sacrifice);
    const HumanPreTapOffer lb = HumanPreTapOfferFor(s, *FindMt(s, 40));
    CHECK(lb.route == HumanPreTapOffer::Route::SacAction);
    CHECK(lb.free_faces == "WUBRG");                  // "three mana of any ONE color"
    CHECK(lb.sac_amount == 3);

    CHECK(Tap(s, "tap=Treasure Token#1001:U") == "");
    CHECK(s.floating_mana.blue == 1);
    CHECK(FindMt(s, 1001) == nullptr);
    CHECK(Tap(s, "tap=0/1 Eldrazi Spawn Token#1002:C") == "");
    CHECK(s.floating_mana.colorless == 1);
    CHECK(FindMt(s, 1002) == nullptr);
    CHECK(Tap(s, "tap=Lotus Bloom#40:R") == "");
    CHECK(s.floating_mana.red == 3);
    CHECK(FindMt(s, 40) == nullptr);
    CHECK(InGyMt(s, 40));
    CHECK(s.floating_mana.Total() == 5);
    CHECK(s.floating_haste_c == 0);                   // no haste rider on any of them
}

TEST_CASE("Manual tap: restricted mana stays excluded -- the floating pool would launder it")
{
    GameState s = FreshMt();
    PutMt(s, "Somberwald Sage", 50);                  // creature_mana_only
    const HumanPreTapOffer o = HumanPreTapOfferFor(s, *FindMt(s, 50));
    CHECK(o.free_faces.empty());
    CHECK(o.fed_faces.empty());
    CHECK(Contains(o.why_not, "creature spells"));
    CHECK(Contains(Tap(s, "tap=Somberwald Sage#50:G"), "cannot be tapped by hand"));
    CHECK(s.floating_mana.Total() == 0);
}

// ---------------------------------------------------------------------------------------------------
// THE OFFER AND THE EXECUTOR ARE ONE QUESTION, over every new class at once (see the fungus twin).
// Each face -- every single letter, plus every fed face the offer publishes -- runs on a fresh copy
// whose pool can pay any feed ({U}{R}{C} floating), and must be accepted iff offered.
TEST_CASE("Manual tap: offer / executor parity over filters and sacrifice sources")
{
    GameState s = FreshMt();
    PutMt(s, "Generator Servant", 10);
    PutMt(s, "Generator Servant", 11, /*sick=*/true);
    PutMt(s, "Izzet Signet", 20);
    PutMt(s, "Cascade Bluffs", 5);
    PutMt(s, "Ferrous Lake", 6);
    PutMt(s, "Sol Ring", 7);
    PutMt(s, "Island", 1);
    PutMt(s, "Treasure Token", 1001);
    PutMt(s, "Lotus Bloom", 40);
    PutMt(s, "Somberwald Sage", 50);
    s.floating_mana.blue = 1;
    s.floating_mana.red = 1;
    s.floating_mana.colorless = 1;

    std::vector<std::string> bad;
    for (const Permanent& p : s.battlefield)
    {
        const std::string where = p.card.m_name.str() + "#" + std::to_string(p.card.m_number);
        const HumanPreTapOffer o = HumanPreTapOfferFor(s, p);
        std::vector<std::string> faces = { "W", "U", "B", "R", "G", "C", "UU", "UR", "RR", "UB" };
        for (const std::string& f : o.fed_faces)
        { if (std::find(faces.begin(), faces.end(), f) == faces.end()) { faces.push_back(f); } }
        for (const std::string& f : faces)
        {
            const bool offered = (f.size() == 1 && o.free_faces.find(f[0]) != std::string::npos)
                              || std::find(o.fed_faces.begin(), o.fed_faces.end(), f) != o.fed_faces.end();
            GameState g = s;                          // a tap mutates; never reuse a board
            const int bf_before = static_cast<int>(g.battlefield.size());
            const std::string why = Tap(g, "tap=" + where + ":" + f);
            const Permanent* after = FindMt(g, p.card.m_number);
            const bool spent = after == nullptr || after->tapped
                            || static_cast<int>(g.battlefield.size()) != bf_before;
            if (offered && !why.empty()) { bad.push_back(where + ":" + f + " OFFERED but refused: " + why); }
            else if (!offered && why.empty()) { bad.push_back(where + ":" + f + " NOT offered but accepted"); }
            else if (offered && !spent) { bad.push_back(where + ":" + f + " accepted but the source was not used"); }
            else if (!offered && !(g.floating_mana == s.floating_mana))
            { bad.push_back(where + ":" + f + " refused but the pool moved"); }
        }
    }
    std::string joined;
    for (const std::string& b : bad) { joined += "\n    " + b; }
    CHECK_MESSAGE(bad.empty(), "offer/executor mismatches:", joined);
}

// ---------------------------------------------------------------------------------------------------
// A HAND-TAPPED SACRIFICE SOURCE REPLACES THE PLAN'S IMPLICIT CRACK (TurnSolver::AbsorbPreTappedSacrifices).
// Found on the end-to-end walk (NivMizzetCombo seed 15, turn 6, two Generator Servants): the line
// "tap=Generator Servant#13:C; cast=Niv-Mizzet, the Firemind" matched the menu's only Firemind plan,
// whose implicit crack is Servant #12, and the commit sacrificed BOTH -- #12 in the pre-cast pass, then
// the human's #13. The human's tap IS the crack that plan action stood for.
namespace
{
Action SacMt(int src)
{
    Action a;
    a.kind               = Action::Kind::SacForMana;
    a.card_name          = std::string("Generator Servant");
    a.hand_index         = -1;
    a.sac_source_id      = src;
    a.ritual_float       = 2;
    a.chosen_float_color = std::string("C");
    return a;
}
Action CastMt(const std::string& name)
{
    Action a;
    a.kind      = Action::Kind::CastFromHand;
    a.card_name = name;
    return a;
}
int SacCount(const TurnSolver::Plan& p, int src)
{
    int n = 0;
    for (const Action& a : p.actions)
    { if (a.kind == Action::Kind::SacForMana && (src == 0 || a.sac_source_id == src)) { ++n; } }
    return n;
}
}   // namespace

TEST_CASE("Manual tap: a hand-tapped Generator Servant replaces the plan's implicit crack -- never a second body")
{
    EnsureCardsMt();
    // The reported shape: the plan cracks #12, the human taps #13 -> the plan's crack is absorbed.
    {
        TurnSolver::Plan p;
        p.actions = { CastMt("Niv-Mizzet, the Firemind"), SacMt(12) };
        TurnSolver::ApplyHumanSequencePin(p, { "tap=Generator Servant#13:C", "cast=Niv-Mizzet, the Firemind" });
        CHECK(SacCount(p, 0) == 0);
        REQUIRE(p.actions.size() == 1);
        CHECK(p.actions[0].card_name.str() == "Niv-Mizzet, the Firemind");
        CHECK(p.human_seq_len == 1);                      // the human's step is untouched
        REQUIRE(p.human_pre_taps.size() == 1);
        CHECK(p.human_pre_taps[0].num == 13);
        CHECK(p.human_pre_taps[0].position == 0);
    }
    // Two implicit cracks, one hand tap: the SAME copy's action is the one absorbed; the other stays.
    {
        TurnSolver::Plan p;
        p.actions = { CastMt("Niv-Mizzet, the Firemind"), SacMt(12), SacMt(13) };
        TurnSolver::ApplyHumanSequencePin(p, { "tap=Generator Servant#13:C", "cast=Niv-Mizzet, the Firemind" });
        CHECK(SacCount(p, 13) == 0);
        CHECK(SacCount(p, 12) == 1);
    }
    // The legacy (non-"**") entry point absorbs the same way.
    {
        TurnSolver::Plan p;
        p.actions = { CastMt("Niv-Mizzet, the Firemind"), SacMt(12) };
        TurnSolver::PreTap t;
        REQUIRE(ParseHumanPreTapToken("tap=Generator Servant#12:C", t));
        p.human_pre_taps.push_back(t);
        TurnSolver::AbsorbPreTappedSacrifices(p);
        CHECK(SacCount(p, 0) == 0);
    }
    // No pre-tap of a sacrifice source -> untouched: a plain land tap, or no tap at all.
    {
        TurnSolver::Plan p;
        p.actions = { CastMt("Niv-Mizzet, the Firemind"), SacMt(12) };
        TurnSolver::ApplyHumanSequencePin(p, { "tap=Island#1:U", "cast=Niv-Mizzet, the Firemind" });
        CHECK(SacCount(p, 12) == 1);
        TurnSolver::Plan q;
        q.actions = { CastMt("Niv-Mizzet, the Firemind"), SacMt(12) };
        TurnSolver::ApplyHumanSequencePin(q, { "cast=Niv-Mizzet, the Firemind" });
        CHECK(SacCount(q, 12) == 1);
    }
}
