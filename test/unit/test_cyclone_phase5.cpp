// Unpredictable Cyclone, PHASE 5 (2026-10-27): the cost work -- the sound cycle-seed pin fold and the USER's
// search heuristics (ledger "Optimization plan (USER 2026-10-27)").
//
//   * THE CYCLE-SEED PIN FOLD (MTG_CYCLE_SEED_FOLD, sound, default ON): a seed whose Curator j exceeds the
//     watchers that can be out when it is cycled, or whose free-cast pin cannot be read (no Cyclone out or cast),
//     is not emitted -- its sibling is. Not applied to a plan that can draw before the seed.
//   * SCALDING TARN (MTG_CYCLONE_FETCH_RULE): Steam Vents; then red NEED -> Blood Crypt / Mountain, else Watery
//     Grave / Island; Xander's Lounge searched beside the pick.
//   * THE SHARED RED COUNT: Seething Song is the second red only; an enters-tapped land in hand does not count;
//     at most one red land from hand; Cascade Bluffs counts two beside a U/R land.
//   * MYRIAD LANDSCAPE (MTG_CYCLONE_MYRIAD_RULE): two Islands, one option; two Mountains beside them only while
//     short on red.
//   * LONG-TERM PLANS (MTG_CYCLONE_LTP_RULE): a missing Cyclone, Emrakul with no other way to top one; neither
//     -> the cast is held. MTG_CYCLONE_LTP_ULAMOG (OFF): an Ulamog once no Emrakul is left.
#include <doctest/doctest.h>

#include "ai/DecisionProviders.h"
#include "ai/HeuristicArm.h"
#include "ai/TurnSolver.h"
#include "cards/CardDatabase.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"

#include <algorithm>
#include <string>
#include <vector>

namespace
{

constexpr const char* kCyclone = "Unpredictable Cyclone";
constexpr const char* kCurator = "Curator of Mysteries";
constexpr const char* kWraith  = "Street Wraith";
constexpr const char* kEmrakul = "Emrakul, the Aeons Torn";
constexpr const char* kHunger  = "Ulamog, the Ceaseless Hunger";
constexpr const char* kGyre    = "Ulamog, the Infinite Gyre";
constexpr const char* kLtp     = "Long-Term Plans";
constexpr const char* kTarn    = "Scalding Tarn";
constexpr const char* kVents   = "Steam Vents";
constexpr const char* kGrave   = "Watery Grave";
constexpr const char* kCrypt   = "Blood Crypt";
constexpr const char* kLounge  = "Xander's Lounge";

void EnsureCards5()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

const CardDefinition& Def5(const std::string& name)
{
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE_MESSAGE(d != nullptr, "card not in cards.json: ", name);
    return *d;
}

Card Card5(const std::string& name, int number)
{
    Card c     = Def5(name).card;
    c.m_number = number;
    return c;
}

void Put5(GameState& s, const std::string& name, int number)
{
    Permanent p;
    p.card             = Card5(name, number);
    p.controller_index = 0;
    p.owner_index      = 0;
    s.battlefield.push_back(p);
}

const CycloneProvider& Prov5() { static const CycloneProvider cp; return cp; }

// Round 3, the land drop made, `lands` untapped Islands, `lib` on top of the library over Islands.
GameState Base5(int lands, const std::vector<std::string>& lib, int filler = 10)
{
    GameState s;
    s.m_provider          = &Prov5();
    s.active_player_index = 0;
    s.turn_number         = 3;
    s.on_the_play         = true;
    s.players[0].life     = 20;
    s.players[1].life     = 20;
    s.players[0].lands_played_this_turn = 1;
    s.deck_has_extra_turn = true;
    s.deck_has_gy_shuffle = true;
    s.deck_has_annihilator = true;
    s.deck_has_cast_target_trigger = true;
    s.deck_has_cycle_draw_replacement = true;
    s.deck_has_discard_scry_watcher = true;
    for (int k = 0; k < lands; ++k) { Put5(s, "Island", 40 + k); }
    int num = 500;
    for (const std::string& n : lib) { s.players[0].library.push_back(Card5(n, num++)); }
    for (int k = 0; k < filler; ++k) { s.players[0].library.push_back(Card5("Island", 900 + k)); }
    return s;
}

struct ArmScope5
{
    heurarm::Arm saved;
    ArmScope5(heurarm::Slot slot, int v) : saved(heurarm::t_arm) { heurarm::t_arm[static_cast<int>(slot)] = static_cast<std::int8_t>(v); }
    ~ArmScope5() { heurarm::t_arm = saved; }
};

bool CastsName(const TurnSolver::Plan& p, const char* name)
{
    for (const Action& a : p.actions)
    { if (a.kind == Action::Kind::CastFromHand && a.card_name.str() == name) { return true; } }
    return false;
}

struct SeedTally { int seeded = 0, j_over = 0, fc_unread = 0; };

// Over a plan list: seeded plans; seeds whose j exceeds the watchers out + cast by the plan; seeds with a
// non-take free-cast pin where no Cyclone is out and the plan casts none.
SeedTally Tally(const std::vector<TurnSolver::Plan>& plans, int watchers_bf, bool cyclone_out)
{
    SeedTally t;
    for (const TurnSolver::Plan& p : plans)
    {
        const Action* sd = CycleSeedOf(p.actions);
        if (sd == nullptr) { continue; }
        ++t.seeded;
        bool draws = false;
        int  cur_cast = 0;
        for (const Action& a : p.actions)
        {
            if (a.kind != Action::Kind::CastFromHand) { continue; }
            if (a.card_name.str() == kCurator) { ++cur_cast; }
            if (a.card_name.str() == "Brainstorm" || a.card_name.str() == "Ponder") { draws = true; }
        }
        if (draws) { continue; }
        if (sd->devour_count > watchers_bf + cur_cast) { ++t.j_over; }
        if (sd->ponder_keep != kCycFcTake && sd->ponder_keep != kCycFcDefault && !cyclone_out && !CastsName(p, kCyclone))
        { ++t.fc_unread; }
    }
    return t;
}

}   // namespace

TEST_CASE("Cyclone phase 5: the cycle-seed pin fold drops only unreadable pins, and only where exact")
{
    EnsureCards5();
    heurarm::Clear();
    // Cyclone in HAND (not castable: no red), a Curator in hand (castable: four Islands), Street Wraith; no watcher
    // out. CollectActions emits Wraith seeds j in {0,1} x {take, decline (an Ulamog), take-and-Remand}.
    GameState s = Base5(4, { kHunger, kEmrakul });
    s.players[0].hand.push_back(Card5(kWraith, 101));
    s.players[0].hand.push_back(Card5(kCurator, 102));
    s.players[0].hand.push_back(Card5(kCyclone, 103));
    s.players[0].hand.push_back(Card5("Remand", 104));
    s.players[0].hand.push_back(Card5("Brainstorm", 105));

    std::vector<TurnSolver::Plan> on, off;
    {
        ArmScope5 _a(heurarm::CYCLE_SEED_FOLD, 1);
        on = TurnSolver::EnumerateMainPlans(s, /*is_pre_combat=*/true);
    }
    {
        ArmScope5 _a(heurarm::CYCLE_SEED_FOLD, 0);
        off = TurnSolver::EnumerateMainPlans(s, /*is_pre_combat=*/true);
    }
    const SeedTally t_on  = Tally(on, 0, false);
    const SeedTally t_off = Tally(off, 0, false);
    // Fold OFF: the unreadable pins are there (a j = 1 seed in a plan that casts no Curator; a decline / Remand pin
    // with no Cyclone castable).
    CHECK(t_off.j_over > 0);
    CHECK(t_off.fc_unread > 0);
    // Fold ON: none of them, and every surviving seeded plan is still there.
    CHECK(t_on.j_over == 0);
    CHECK(t_on.fc_unread == 0);
    CHECK(t_on.seeded > 0);
    CHECK(t_on.seeded < t_off.seeded);
    CHECK(on.size() == off.size() - static_cast<std::size_t>(t_off.seeded - t_on.seeded));
    // A plan that casts the Curator keeps j = 1 (the Curator it casts triggers), with the fold on.
    bool cur_j1 = false;
    for (const TurnSolver::Plan& p : on)
    {
        const Action* sd = CycleSeedOf(p.actions);
        if (sd != nullptr && sd->card_name.str() == kWraith && sd->devour_count == 1 && CastsName(p, kCurator))
        { cur_j1 = true; }
    }
    CHECK(cur_j1);
    // A plan that casts Brainstorm first is NOT folded (its continuation may cast a drawn Curator / Cyclone).
    bool bs_j1 = false;
    for (const TurnSolver::Plan& p : on)
    {
        const Action* sd = CycleSeedOf(p.actions);
        if (sd != nullptr && sd->devour_count == 1 && CastsName(p, "Brainstorm") && !CastsName(p, kCurator))
        { bs_j1 = true; }
    }
    CHECK(bs_j1);
}

TEST_CASE("Cyclone phase 5: the shared red count")
{
    EnsureCards5();
    heurarm::Clear();
    // Fetch rank on a library holding every Tarn target but Steam Vents: the front pick names the red verdict.
    auto front = [](const GameState& s)
    {
        const std::vector<std::string> c = Prov5().FetchCandidates(s, 0, Def5(kTarn).params);
        REQUIRE(!c.empty());
        return c.front();
    };
    const std::vector<std::string> lib{ "Island", "Mountain", kGrave, kCrypt, kLounge };
    {   // Nothing red: short -> Blood Crypt.
        GameState s = Base5(1, lib, 0);
        CHECK(front(s) == kCrypt);
    }
    {   // One Mountain out + Seething Song in hand: the Song is the second red -> not short -> Watery Grave.
        GameState s = Base5(1, lib, 0);
        Put5(s, "Mountain", 60);
        s.players[0].hand.push_back(Card5("Seething Song", 120));
        CHECK(front(s) == kGrave);
    }
    {   // Seething Song alone is not a red source.
        GameState s = Base5(1, lib, 0);
        s.players[0].hand.push_back(Card5("Seething Song", 120));
        CHECK(front(s) == kCrypt);
    }
    {   // One Mountain out + Xander's Lounge in hand (enters tapped): still short.
        GameState s = Base5(1, lib, 0);
        Put5(s, "Mountain", 60);
        s.players[0].hand.push_back(Card5(kLounge, 121));
        CHECK(front(s) == kCrypt);
    }
    {   // Two Mountains in hand, none out: only one hand land counts -> short.
        GameState s = Base5(1, lib, 0);
        s.players[0].hand.push_back(Card5("Mountain", 122));
        s.players[0].hand.push_back(Card5("Mountain", 123));
        CHECK(front(s) == kCrypt);
    }
    {   // One Mountain out + one in hand: two -> not short.
        GameState s = Base5(1, lib, 0);
        Put5(s, "Mountain", 60);
        s.players[0].hand.push_back(Card5("Mountain", 122));
        CHECK(front(s) == kGrave);
    }
    {   // Cascade Bluffs beside an Island makes RR: not short.
        GameState s = Base5(1, lib, 0);
        Put5(s, "Cascade Bluffs", 61);
        CHECK(front(s) == kGrave);
    }
}

TEST_CASE("Cyclone phase 5: the Scalding Tarn rule -- Steam Vents, then need; the triome beside the pick")
{
    EnsureCards5();
    heurarm::Clear();
    GameState s = Base5(1, { "Island", kLounge, "Mountain", kVents, kGrave, kCrypt }, 0);
    Put5(s, "Mountain", 60);
    Put5(s, kVents, 61);   // two red out: not short
    const CardParams& tp = Def5(kTarn).params;
    std::vector<std::string> ranked = Prov5().FetchCandidates(s, 0, tp);
    REQUIRE(ranked.size() == 6);
    CHECK(ranked[0] == kVents);
    CHECK(Prov5().FetchSearchCandidates(s, 0, tp, ranked) == std::vector<std::string>{ kVents, kLounge });
    // Vents gone: blue (Watery Grave, then Island) ahead of the red, the triome last but searched beside the pick.
    s.players[0].library.erase(std::find_if(s.players[0].library.begin(), s.players[0].library.end(),
                                            [](const Card& c) { return c.m_name.str() == kVents; }));
    ranked = Prov5().FetchCandidates(s, 0, tp);
    CHECK(ranked == std::vector<std::string>{ kGrave, "Island", kCrypt, "Mountain", kLounge });
    CHECK(Prov5().FetchSearchCandidates(s, 0, tp, ranked) == std::vector<std::string>{ kGrave, kLounge });
    // Lever off: the generic library-order list and its single top pick.
    {
        ArmScope5 _a(heurarm::CYCLONE_FETCH_RULE, 0);
        ranked = Prov5().FetchCandidates(s, 0, tp);
        CHECK(ranked.front() == "Island");
        CHECK(Prov5().FetchSearchCandidates(s, 0, tp, ranked).size() == 1);
    }
}

TEST_CASE("Cyclone phase 5: the Myriad Landscape rule -- two Islands; two Mountains only while short on red")
{
    EnsureCards5();
    heurarm::Clear();
    const CardParams& mp = Def5("Myriad Landscape").params;
    auto picks0 = []()
    {
        return std::vector<std::vector<std::string>>{ { "Mountain", "Mountain" }, { "Island", "Island" } };
    };
    {   // Not short (Steam Vents + Mountain out): Islands, one option.
        GameState s = Base5(1, {}, 0);
        Put5(s, kVents, 60);
        Put5(s, "Mountain", 61);
        auto picks = picks0();
        Prov5().LandSearchPickOrder(s, 0, mp.land_search_count, 0, true, true, 0, picks);
        CHECK(picks[0] == std::vector<std::string>{ "Island", "Island" });
        CHECK(Prov5().LandSearchPickWidth(s, 0, mp.land_search_count, 0, true, true, 0, picks) == 1);
    }
    {   // Short (Islands only): the Mountains are searched beside the Islands.
        GameState s = Base5(2, {}, 0);
        auto picks = picks0();
        Prov5().LandSearchPickOrder(s, 0, mp.land_search_count, 0, true, true, 0, picks);
        CHECK(picks[0] == std::vector<std::string>{ "Island", "Island" });
        CHECK(picks[1] == std::vector<std::string>{ "Mountain", "Mountain" });
        CHECK(Prov5().LandSearchPickWidth(s, 0, mp.land_search_count, 0, true, true, 0, picks) == 2);
        std::uint64_t k_short = 0, k_ok = 0;
        Prov5().LandSearchPickOrderKey(s, 0, k_short);
        Put5(s, "Mountain", 62);
        Put5(s, "Mountain", 63);
        Prov5().LandSearchPickOrderKey(s, 0, k_ok);
        CHECK(k_short != k_ok);   // the memo key carries the red verdict
    }
    {   // No two-Island pick left: the rule cannot express it -> the full fan.
        GameState s = Base5(1, {}, 0);
        std::vector<std::vector<std::string>> picks{ { "Mountain", "Mountain" }, { "Island" } };
        Prov5().LandSearchPickOrder(s, 0, mp.land_search_count, 0, true, true, 0, picks);
        CHECK(Prov5().LandSearchPickWidth(s, 0, mp.land_search_count, 0, true, true, 0, picks) == 2);
    }
}

TEST_CASE("Cyclone phase 5: the Long-Term Plans rule -- missing Cyclone, Emrakul with no other way, else held")
{
    EnsureCards5();
    heurarm::Clear();
    const CardParams& lp = Def5(kLtp).params;
    auto has = [](const std::vector<std::string>& v, const char* n) { return std::find(v.begin(), v.end(), n) != v.end(); };
    auto ltp_cast_emitted = [](const GameState& s)
    {
        for (const Action& a : TurnSolver::CollectActionsForTest(s, /*is_pre_combat=*/true))
        { if (a.kind == Action::Kind::CastFromHand && a.card_name.str() == kLtp) { return true; } }
        return false;
    };
    {   // No Cyclone held, no Emrakul in hand: Cyclone, then Emrakul.
        GameState s = Base5(3, { kCyclone, kEmrakul, kCurator, kWraith, "Ponder" });
        s.players[0].hand.push_back(Card5(kLtp, 101));
        const std::vector<std::string> c = Prov5().TutorCandidates(s, 0, lp);
        CHECK(c == std::vector<std::string>{ kCyclone, kEmrakul });
        CHECK(ltp_cast_emitted(s));
    }
    {   // A Cyclone out: Emrakul only.
        GameState s = Base5(3, { kCyclone, kEmrakul, kCurator });
        Put5(s, kCyclone, 70);
        s.players[0].hand.push_back(Card5(kLtp, 101));
        CHECK(Prov5().TutorCandidates(s, 0, lp) == std::vector<std::string>{ kEmrakul });
    }
    {   // A Cyclone out AND another way to top an Emrakul (one in hand + Brainstorm), no cycler left to find: HELD.
        GameState s = Base5(3, { kCyclone, kEmrakul, "Ponder" });
        Put5(s, kCyclone, 70);
        s.players[0].hand.push_back(Card5(kLtp, 101));
        s.players[0].hand.push_back(Card5(kEmrakul, 102));
        s.players[0].hand.push_back(Card5("Brainstorm", 103));
        CHECK(!ltp_cast_emitted(s));
        // A resolution reached anyway never whiffs: the full list.
        CHECK(Prov5().TutorCandidates(s, 0, lp).size() > 2);
        // Lever off: cast and every name.
        ArmScope5 _a(heurarm::CYCLONE_LTP_RULE, 0);
        CHECK(ltp_cast_emitted(s));
    }
    {   // No Emrakul left, a Cyclone out: held -- unless the Ulamog amendment is on (Ceaseless Hunger first).
        GameState s = Base5(3, { kGyre, kHunger, "Ponder" });
        Put5(s, kCyclone, 70);
        s.players[0].hand.push_back(Card5(kLtp, 101));
        CHECK(!ltp_cast_emitted(s));
        ArmScope5 _a(heurarm::CYCLONE_LTP_ULAMOG, 1);
        const std::vector<std::string> c = Prov5().TutorCandidates(s, 0, lp);
        CHECK(c == std::vector<std::string>{ kHunger, kGyre });
        CHECK(ltp_cast_emitted(s));
        CHECK(!has(c, kCurator));
    }
    {   // Street Wraith (USER 2026-10-27): no creature cycler in hand and nothing else missing (a Cyclone out, the
        // Emrakul target not admitted) -> the cheapest creature cycler, Wraith before Curator.
        GameState s = Base5(3, { kEmrakul, kCurator, kWraith, "Ponder" });
        Put5(s, kCyclone, 70);
        s.players[0].hand.push_back(Card5(kLtp, 101));
        s.players[0].hand.push_back(Card5(kEmrakul, 102));
        s.players[0].hand.push_back(Card5("Brainstorm", 103));
        CHECK(Prov5().TutorCandidates(s, 0, lp) == std::vector<std::string>{ kWraith });
        CHECK(ltp_cast_emitted(s));
        // A cycler already in hand: nothing to find -> held.
        s.players[0].hand.push_back(Card5(kCurator, 104));
        CHECK(!ltp_cast_emitted(s));
    }
    {   // Something still missing (no Cyclone anywhere): the Wraith is not offered.
        GameState s = Base5(3, { kCyclone, kWraith, "Ponder" });
        s.players[0].hand.push_back(Card5(kLtp, 101));
        CHECK(Prov5().TutorCandidates(s, 0, lp) == std::vector<std::string>{ kCyclone });
    }
    {   // Seething Song (MTG_CYCLONE_LTP_SONG, OFF): admitted with a Brainstorm / Brainsurge / Ponder to draw it.
        GameState s = Base5(3, { kCyclone, "Seething Song", "Ponder" });
        s.players[0].hand.push_back(Card5(kLtp, 101));
        s.players[0].hand.push_back(Card5("Ponder", 102));
        CHECK(Prov5().TutorCandidates(s, 0, lp) == std::vector<std::string>{ kCyclone });
        ArmScope5 _a(heurarm::CYCLONE_LTP_SONG, 1);
        CHECK(Prov5().TutorCandidates(s, 0, lp) == std::vector<std::string>{ kCyclone, "Seething Song" });
        s.players[0].hand.push_back(Card5("Seething Song", 103));   // one held: not missing
        CHECK(Prov5().TutorCandidates(s, 0, lp) == std::vector<std::string>{ kCyclone });
    }
}
