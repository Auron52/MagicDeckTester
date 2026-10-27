// Unpredictable Cyclone onboarding, PHASE 4 (2026-10-27): Long-Term Plans and Toxic Deluge.
//
//   * Card data (Scryfall-verified): Long-Term Plans {2}{U} instant, Toxic Deluge {2}{B} sorcery.
//   * tutor_library_position (Long-Term Plans 3): the shared PerformTutor removes the card, shuffles, then
//     inserts it at index min(N-1, library size) -- with fewer than three cards left, the bottom (ruling).
//     NOT tutor_to_top (no top-stack marker; the anti-lifegain signature untouched), but IsLibraryTutor.
//   * LOCKSTEP: the executor's stack resolution (EffectHandler) and the rollout's ApplyPlan reach the same
//     library ORDER.
//   * The tutor cast is emitted and its target axis is as wide as the distinct names (CycloneProvider).
//   * The deck's line: Long-Term Plans puts Emrakul third from the top, Brainstorm draws it.
//   * Human play: the instant re-asks at resolution; a -1 takes the default ("a card" names no quality).
//   * Toxic Deluge: goldfish_inert, never emitted as a cast (USER ruling).
#include <doctest/doctest.h>

#include "ai/DecisionProviders.h"
#include "ai/TurnSolver.h"
#include "cards/CardDatabase.h"
#include "core/EffectHandler.h"
#include "core/GameLogger.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"
#include "deck/DeckLoader.h"
#include "ai/ManaPayment.h"

#include <algorithm>
#include <string>
#include <vector>

namespace
{

constexpr const char* kLtp     = "Long-Term Plans";
constexpr const char* kDeluge  = "Toxic Deluge";
constexpr const char* kEmrakul = "Emrakul, the Aeons Torn";

void EnsureCards4()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

const CardDefinition& Def4(const std::string& name)
{
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE_MESSAGE(d != nullptr, "card not in cards.json: ", name);
    return *d;
}

Card Card4(const std::string& name, int number)
{
    Card c     = Def4(name).card;
    c.m_number = number;
    return c;
}

void Put4(GameState& s, const std::string& name, int number)
{
    Permanent p;
    p.card             = Card4(name, number);
    p.controller_index = 0;
    p.owner_index      = 0;
    s.battlefield.push_back(p);
}

const CycloneProvider& Prov4() { static const CycloneProvider cp; return cp; }

// Round 3, main 1, land drop made, `lands` untapped Islands; library = `lib` top-first.
GameState Base4(int lands, const std::vector<std::string>& lib)
{
    GameState s;
    s.m_provider          = &Prov4();
    s.active_player_index = 0;
    s.turn_number         = 3;
    s.on_the_play         = true;
    s.game_seed           = 777;
    s.players[0].life     = 20;
    s.players[1].life     = 20;
    s.players[0].lands_played_this_turn = 1;
    for (int k = 0; k < lands; ++k) { Put4(s, "Island", 40 + k); }
    int num = 500;
    for (const std::string& n : lib) { s.players[0].library.push_back(Card4(n, num++)); }
    return s;
}

int IndexOf(const GameState& s, const std::string& name)
{
    const auto& lib = s.players[0].library;
    for (int i = 0; i < static_cast<int>(lib.size()); ++i)
    { if (lib[static_cast<std::size_t>(i)].m_name.str() == name) { return i; } }
    return -1;
}

std::vector<int> LibNums4(const GameState& s)
{
    std::vector<int> v;
    for (const Card& c : s.players[0].library) { v.push_back(c.m_number); }
    return v;
}

TurnSolver::Plan LtpPlan(const GameState& s, const std::string& target, bool then_brainstorm)
{
    TurnSolver::Plan plan;
    plan.land_decided = true;
    auto cast = [&](const std::string& name, const std::string& tgt)
    {
        Action a;
        a.kind         = Action::Kind::CastFromHand;
        a.card_name    = name;
        a.def          = &Def4(name);
        a.cost         = EffectiveSpellCost(Def4(name), s, 1);
        a.tutor_target = tgt;
        for (int i = 0; i < static_cast<int>(s.players[0].hand.size()); ++i)
        { if (s.players[0].hand[i].m_name == name) { a.hand_index = i; break; } }
        plan.actions.push_back(a);
    };
    cast(kLtp, target);
    if (then_brainstorm) { cast("Brainstorm", std::string()); }
    return plan;
}

struct TutorChooserScope4
{
    TutorChooser  fn;
    TutorChooser* saved;
    int           calls = 0;
    explicit TutorChooserScope4(int answer) : saved(g_play_tutor_chooser)
    {
        fn = [this, answer](const GameState&, int, const std::string&, const std::vector<std::string>&, int,
                            const std::vector<int>&) -> int { ++calls; return answer; };
        g_play_tutor_chooser = &fn;
    }
    ~TutorChooserScope4() { g_play_tutor_chooser = saved; }
};

}   // namespace

TEST_CASE("Cyclone phase 4: card data for Long-Term Plans and Toxic Deluge")
{
    EnsureCards4();
    const CardDefinition& l = Def4(kLtp);
    CHECK(l.card.m_mana_cost.ManaValue() == 3);
    CHECK(l.card.HasType(CardType::Instant));
    CHECK(l.params.tutor_library_position == 3);
    CHECK_FALSE(l.params.tutor_to_top);    // NOT a next-draw tutor (provider sites, the anti signature)
    CHECK_FALSE(l.params.tutor_to_hand);
    CHECK(l.params.tutor_types.empty());   // "a card"
    CHECK(IsLibraryTutor(l.params));
    const CardDefinition& d = Def4(kDeluge);
    CHECK(d.card.m_mana_cost.ManaValue() == 3);
    CHECK(d.card.HasType(CardType::Sorcery));
    CHECK(d.params.goldfish_inert);
    // The deck still routes to CycloneProvider with both cards in it.
    Decklist deck;
    int num = 1;
    for (const char* n : { "Unpredictable Cyclone", "Curator of Mysteries", "Emrakul, the Aeons Torn",
                           kLtp, kDeluge, "Scalding Tarn" })
    { for (int k = 0; k < 4; ++k) { deck.mainboard.push_back(Card4(n, num++)); } }
    CHECK(std::string(DetectDecisionProvider(deck).Name()) == "Cyclone");
    // ...and Long-Term Plans alone does NOT set the anti-lifegain signature that tutor_to_top would.
    Decklist ltp_only;
    for (int k = 0; k < 4; ++k)  { ltp_only.mainboard.push_back(Card4(kLtp, num++)); }
    for (int k = 0; k < 20; ++k) { ltp_only.mainboard.push_back(Card4("Island", num++)); }
    CHECK(std::string(DetectDecisionProvider(ltp_only).Name()) != "AntiLifegain");
}

TEST_CASE("Cyclone phase 4: Long-Term Plans puts the card third from the top, or the bottom")
{
    EnsureCards4();
    const CardDefinition& l = Def4(kLtp);
    // Library sizes after the removal: 0, 1, 2, 3, 10 -> index 0, 1, 2, 2, 2.
    for (int others : { 0, 1, 2, 3, 10 })
    {
        std::vector<std::string> lib(static_cast<std::size_t>(others), "Island");
        lib.push_back(kEmrakul);   // at the bottom before the search
        GameState s = Base4(0, lib);
        const int sc = s.search_count;
        PerformTutor(s, 0, l.params, kEmrakul, kLtp);
        REQUIRE(static_cast<int>(s.players[0].library.size()) == others + 1);
        CHECK(IndexOf(s, kEmrakul) == std::min(2, others));
        CHECK(s.search_count == sc + 1);              // the search shuffled
        CHECK(s.top_stacked_card_number < 0);         // not a deliberate top stack
        CHECK(s.players[0].hand.empty());
    }
}

TEST_CASE("Cyclone phase 4: LOCKSTEP -- executor stack resolution and the rollout's ApplyPlan")
{
    EnsureCards4();
    std::vector<std::string> lib;
    for (int k = 0; k < 6; ++k) { lib.push_back("Island"); lib.push_back("Street Wraith"); }
    lib.push_back(kEmrakul);
    GameState s = Base4(3, lib);
    s.players[0].hand.push_back(Card4(kLtp, 101));
    // Executor: the stack entry the cast site builds, resolved by EffectHandler.
    GameState ex = s;
    ex.players[0].hand.clear();
    StackEntry e;
    e.type             = StackEntry::EntryType::Spell;
    e.source           = Card4(kLtp, 101);
    e.controller_index = 0;
    e.tutor_target     = kEmrakul;
    EffectHandler::Resolve(ex, e, Def4(kLtp));
    // Rollout: the plan's cast.
    GameState ro = s;
    TurnSolver::ApplyPlan(ro, LtpPlan(ro, kEmrakul, false), /*is_pre_combat=*/true);
    CHECK(IndexOf(ex, kEmrakul) == 2);
    CHECK(LibNums4(ex) == LibNums4(ro));
    CHECK(ex.search_count == ro.search_count);
    CHECK(ro.players[0].hand.empty());
}

TEST_CASE("Cyclone phase 4: the deck's line -- Long-Term Plans for Emrakul, then Brainstorm draws it")
{
    EnsureCards4();
    std::vector<std::string> lib(20, "Island");
    lib.push_back(kEmrakul);
    GameState s = Base4(4, lib);
    s.players[0].hand.push_back(Card4(kLtp, 101));
    s.players[0].hand.push_back(Card4("Brainstorm", 102));
    TurnSolver::ApplyPlan(s, LtpPlan(s, kEmrakul, true), /*is_pre_combat=*/true);
    // Brainstorm drew the top three (Emrakul is the third) and put two back; whichever two, Emrakul was
    // drawn -- it is in hand, or one of the two put back on top.
    const bool in_hand = std::any_of(s.players[0].hand.begin(), s.players[0].hand.end(),
                                     [](const Card& c) { return c.m_name.str() == kEmrakul; });
    const int at = IndexOf(s, kEmrakul);
    CHECK((in_hand || at == 0 || at == 1));
}

TEST_CASE("Cyclone phase 4: the tutor cast is emitted, its target axis spans every distinct name")
{
    EnsureCards4();
    GameState s = Base4(3, { "Island", "Brainstorm", "Ponder", "Street Wraith", kEmrakul, "Remand",
                             "Curator of Mysteries", "Mountain" });
    s.players[0].hand.push_back(Card4(kLtp, 101));
    s.players[0].hand.push_back(Card4(kDeluge, 102));
    const std::vector<Action> acts = TurnSolver::CollectActionsForTest(s, /*is_pre_combat=*/true);
    int ltp = 0, deluge = 0;
    for (const Action& a : acts)
    {
        if (a.kind != Action::Kind::CastFromHand) { continue; }
        if (a.card_name.str() == kLtp)    { ++ltp; }
        if (a.card_name.str() == kDeluge) { ++deluge; }
    }
    CHECK(ltp >= 1);
    CHECK(deluge == 0);   // goldfish_inert: never cast (USER ruling)
    // The candidate list the axis indexes covers all eight distinct names (width 32 >= 8).
    const std::vector<std::string> cands =
        RankedTutorCandidates(Prov4(), s, 0, Def4(kLtp).params);
    CHECK(cands.size() == 8);
    CHECK(std::find(cands.begin(), cands.end(), kEmrakul) != cands.end());
    CHECK(Prov4().TutorSearchWidth() >= 8);
}

TEST_CASE("Cyclone phase 4: human play -- re-asked at resolution; a -1 takes the default")
{
    EnsureCards4();
    CHECK_FALSE(HumanPlayDefersTutorTarget(Def4(kLtp)));   // autonomous: the plan fan stays
    GameState s = Base4(0, { "Island", "Island", kEmrakul, "Island" });
    TutorChooserScope4 ask(-1);
    PerformTutor(s, 0, Def4(kLtp).params, std::string(), kLtp);
    CHECK(ask.calls == 1);
    CHECK(s.players[0].library.size() == 4);   // a card was found and put back into the library
    CHECK(s.search_count == 1);
}
