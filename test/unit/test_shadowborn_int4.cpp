// Shadowborn Apostle onboarding, integrator INT-4 (2026-11-03; docs/design/analysis-Shadowborn
// Apostle.md): the USER's Demon and tutor rules in ShadowbornProvider -- RESTRICTIONS, not orders (USER:
// "I would remove the others outright"): a name left out is not offered at any budget, --budget-ms 0
// included. Demon: Abhorrent Overlord then Jetmir, nothing else, no activation with neither. Tutor (as
// AMENDED by the USER at INT-5, 2026-11-03; test_shadowborn_int5.cpp pins the full table): with no granter on
// the battlefield, a Thrumming Stone when none is held, the mana tier while short of 6 (Lake / Ritual /
// Culling with the multiples rules; Lotus Bloom never), Shadowborn Apostle, then the mana tier as "extra
// mana"; the generic list when none of those is in the library.
#include <doctest/doctest.h>

#include "ai/DecisionProviders.h"
#include "ai/TurnSolver.h"
#include "cards/CardDatabase.h"
#include "core/GameSetup.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"

#include <algorithm>
#include <string>
#include <vector>

namespace
{
const char* const kAp4      = "Shadowborn Apostle";
const char* const kOverlord = "Abhorrent Overlord";
const char* const kJetmir   = "Jetmir, Nexus of Revels";
const char* const kReaper   = "Reaper from the Abyss";
const char* const kSire     = "Sire of Insanity";
const char* const kStone    = "Thrumming Stone";
const char* const kLake     = "Lake of the Dead";
const char* const kCulling  = "Culling the Weak";
const char* const kRitual   = "Dark Ritual";
const char* const kBloom    = "Lotus Bloom";

void Ensure4()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}
const CardDefinition& Def4(const std::string& n)
{
    const CardDefinition* d = CardDatabase::Instance().Lookup(n);
    REQUIRE_MESSAGE(d != nullptr, "card not in cards.json: ", n);
    return *d;
}
Card Card4(const std::string& name, int number)
{
    Card c     = Def4(name).card;
    c.m_number = number;
    return c;
}
Permanent Perm4(const std::string& name, int number, bool fresh = false)
{
    Permanent p;
    p.card              = Card4(name, number);
    p.controller_index  = 0;
    p.owner_index       = 0;
    p.entered_this_turn = fresh;
    return p;
}
// INT-9 added the Demon-search TIMING (Jetmir main 1 only when it raises this turn's attack, else the Overlord main 2;
// test_shadowborn_int9.cpp pins it). These INT-4 tests pin the USER's Demon and tutor LISTS, so they run with the
// timing filter off (the lists as INT-4 left them, in either main).
struct ShadowbornNoTimingSb4 : ShadowbornProvider
{
    void SacNamedTimingFilter(const GameState&, int, const CardParams&, bool, std::vector<std::string>&) const override {}
};
const ShadowbornProvider& Prov4() { static const ShadowbornNoTimingSb4 p; return p; }
GameState Base4()
{
    Ensure4();
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 4;
    s.phase               = Phase::PreCombatMain;
    s.players[0].life     = gamesetup::StartingLife();
    s.players[1].life     = gamesetup::StartingLife();
    s.m_provider          = &Prov4();
    return s;
}
void Lib4(GameState& s, const std::vector<std::string>& names, int first = 300)
{
    for (const std::string& n : names) { s.players[0].library.push_back(Card4(n, first++)); }
}
std::vector<Action> Acts4(const GameState& s)
{
    std::vector<Action> out;
    for (const Action& a : TurnSolver::CollectActionsForTest(s, true))
    {
        if (a.kind == Action::Kind::ActivatePermAbility && a.ability_mode == Action::AbilityMode::SacNamedTutorPut)
        { out.push_back(a); }
    }
    return out;
}
std::vector<std::string> Targets4(const std::vector<Action>& acts)
{
    std::vector<std::string> t;
    for (const Action& a : acts)
    { if (std::find(t.begin(), t.end(), a.tutor_target.str()) == t.end()) { t.push_back(a.tutor_target.str()); } }
    return t;
}
std::vector<std::string> Tut4(const GameState& s, const std::string& tutor = "Demonic Tutor")
{
    return RankedTutorCandidates(Prov4(), s, 0, Def4(tutor).params);
}
using VS = std::vector<std::string>;
const VS kFullLib = { "Swamp", kReaper, kStone, kLake, kCulling, kRitual, kBloom, kAp4, "Blood Crypt", kSire,
                      "Diabolic Intent", "Profane Tutor", kOverlord, kJetmir, "Swamp" };
}   // namespace

TEST_CASE("Shadowborn INT-4 Demon: Abhorrent Overlord then Jetmir only; the eval fronts Overlord")
{
    GameState s = Base4();
    s.battlefield.push_back(Perm4("Swamp", 1));
    for (int k = 0; k < 6; ++k) { s.battlefield.push_back(Perm4(kAp4, 10 + k, /*fresh=*/true)); }
    Lib4(s, { kReaper, kSire, kJetmir, "Swamp", kOverlord, "Swamp" });
    const std::vector<Action> acts = Acts4(s);
    CHECK(Targets4(acts) == VS{ kOverlord, kJetmir });   // no Reaper, no Sire, no fail-to-find
    REQUIRE(acts.size() == 2);
    CHECK(acts[0].eval > acts[1].eval);                  // the playout default is the front
    // The unpinned resolution reads the same list.
    CHECK(Tut4(s, kAp4) == VS{ kOverlord, kJetmir });
    // Resolving the front puts the Overlord.
    GameState t = s;
    TurnSolver::Plan p;
    p.actions.push_back(acts[0]);
    TurnSolver::ApplyPlan(t, p, true);
    int ov = 0;
    for (const Permanent& q : t.battlefield) { if (q.card.m_name.str() == kOverlord) { ++ov; } }
    CHECK(ov == 1);
}

TEST_CASE("Shadowborn INT-4 Demon: Jetmir fronts once Overlord is gone; NO activation with neither left")
{
    GameState s = Base4();
    s.battlefield.push_back(Perm4("Swamp", 1));
    for (int k = 0; k < 6; ++k) { s.battlefield.push_back(Perm4(kAp4, 10 + k)); }
    Lib4(s, { kReaper, kSire, kJetmir, "Swamp" });
    CHECK(Targets4(Acts4(s)) == VS{ kJetmir });
    GameState n = Base4();
    n.battlefield = s.battlefield;
    Lib4(n, { kReaper, kSire, "Swamp" });
    CHECK(Acts4(n).empty());   // USER: "sacrificing 6 to get a 6/6 doesn't actually add power"
    // Not offered at an UNLIMITED budget either: no main plan anywhere activates the Apostle.
    for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlansExhaustive(n, true, nullptr, nullptr))
    {
        for (const Action& a : p.actions)
        { CHECK_FALSE(a.ability_mode == Action::AbilityMode::SacNamedTutorPut); }
    }
    for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlansExhaustive(s, true, nullptr, nullptr))
    {
        for (const Action& a : p.actions)
        {
            if (a.ability_mode == Action::AbilityMode::SacNamedTutorPut) { CHECK(a.tutor_target.str() == kJetmir); }
        }
    }
}

TEST_CASE("Shadowborn INT-4 tutor: the tiers -- Stone, the mana tier (Lake > Ritual > Culling; no Bloom), Apostle")
{
    GameState s = Base4();
    s.battlefield.push_back(Perm4("Swamp", 1));
    s.battlefield.push_back(Perm4("Swamp", 2));
    Lib4(s, kFullLib);
    // Two Swamps, no Lake held -> Lake; no spare Apostles -> no Culling; short of 6 -> the mana tier.
    CHECK(Prov4().ShadowbornReachableMana(s, 0) == 2);
    CHECK(Tut4(s) == VS{ kStone, kLake, kRitual, kAp4 });
    // Seven Apostles (one beyond the six) -> Culling joins; neither ritual reaches 6 a turn earlier than the
    // other here, so the plain Ritual leads (INT-5, USER: "otherwise Dark Ritual which has no drawbacks").
    GameState c = s;
    for (int k = 0; k < 7; ++k) { c.battlefield.push_back(Perm4(kAp4, 20 + k)); }
    CHECK(Tut4(c) == VS{ kStone, kLake, kRitual, kCulling, kAp4 });
    // Exactly six Apostles (none spare) -> no Culling.
    GameState six = s;
    for (int k = 0; k < 5; ++k) { six.battlefield.push_back(Perm4(kAp4, 20 + k)); }
    six.players[0].hand.push_back(Card4(kAp4, 40));
    CHECK(Tut4(six) == VS{ kStone, kLake, kRitual, kAp4 });
    // Lake / Culling are bad in multiples: a held copy removes them (the Culling's net 3 leaves us at 5, still short).
    GameState held = c;
    held.players[0].hand.push_back(Card4(kCulling, 41));
    held.players[0].hand.push_back(Card4(kLake, 45));
    CHECK(Prov4().ShadowbornReachableMana(held, 0) == 5);
    // INT-8 (earliest turn to the bar): the held Lake drops THIS turn onto the two Swamps and cracks (2 + 1 + 3) and
    // Culling adds 3 -- six this turn, so NOT short: the mana tier follows the Apostle as extra mana.
    CHECK(Tut4(held) == VS{ kStone, kAp4, kRitual });
    // One Swamp -> no Lake.
    GameState one = s;
    one.battlefield.pop_back();
    CHECK(Tut4(one) == VS{ kStone, kRitual, kAp4 });
    // A Stone held (hand or battlefield) -> no Stone; Ritual stays even with a Ritual in hand (good in multiples).
    GameState st = s;
    st.players[0].hand.push_back(Card4(kStone, 42));
    st.players[0].hand.push_back(Card4(kRitual, 43));
    CHECK(Tut4(st) == VS{ kLake, kRitual, kAp4 });
    // A Stone on the BATTLEFIELD switches the table (INT-5): an Apostle first while none is in hand.
    GameState sb = s;
    sb.battlefield.push_back(Perm4(kStone, 44));
    CHECK(Tut4(sb) == VS{ kAp4, kLake, kRitual });
}

TEST_CASE("Shadowborn INT-4 tutor: reachable mana and the 6 threshold; the generic fallback")
{
    GameState s = Base4();
    for (int k = 0; k < 4; ++k) { s.battlefield.push_back(Perm4("Swamp", 1 + k)); }
    Lib4(s, kFullLib);
    CHECK(Prov4().ShadowbornReachableMana(s, 0) == 4);
    CHECK(Tut4(s) == VS{ kStone, kLake, kRitual, kAp4 });
    // A land in hand (two drops left: this turn and next) + a Dark Ritual's net 2 -> 4 + 1 + 2 = 7: not short.
    GameState r = s;
    r.players[0].hand.push_back(Card4("Swamp", 50));
    r.players[0].hand.push_back(Card4(kRitual, 51));
    CHECK(Prov4().ShadowbornReachableMana(r, 0) == 7);
    CHECK(Tut4(r) == VS{ kStone, kAp4, kLake, kRitual });   // not short: the mana tier follows as extra mana
    // Land drop already used: one drop left (next turn) -> still 7 with two lands in hand.
    GameState d = r;
    d.players[0].lands_played_this_turn = 1;
    d.players[0].hand.push_back(Card4("Swamp", 52));
    CHECK(Prov4().ShadowbornReachableMana(d, 0) == 7);
    // A Lake out with another Swamp: +3 crack surplus.
    GameState lk = s;
    lk.battlefield.push_back(Perm4(kLake, 9));
    CHECK(Prov4().ShadowbornReachableMana(lk, 0) == 5 + 3);
    // Six lands -> not short; Stone ON THE BATTLEFIELD -> the Apostle, then extra mana (INT-5).
    GameState full = s;
    full.battlefield.push_back(Perm4("Swamp", 7));
    full.battlefield.push_back(Perm4("Swamp", 8));
    full.battlefield.push_back(Perm4(kStone, 9));
    CHECK(Tut4(full) == VS{ kAp4, kLake, kRitual });
    // Nothing of the early set left in the library -> the FALLBACK (INT-5, USER 2026-11-03): exactly one
    // candidate, a basic land first (test_shadowborn_int5.cpp pins the order and the not-cast case).
    GameState g = Base4();
    g.battlefield = full.battlefield;
    Lib4(g, { kReaper, "Swamp", kBloom, "Blood Crypt" });
    CHECK(Tut4(g) == VS{ "Swamp" });
}

TEST_CASE("Shadowborn INT-4 tutor: Profane Tutor's upkeep pin and the cast's searched variants use the early set only")
{
    // Profane off suspend: every pin rank lands inside the early set (a rank past it clamps to its last).
    for (int pin : { -1, 0, 1, 2, 9 })
    {
        GameState s = Base4();
        s.battlefield.push_back(Perm4("Swamp", 1));
        Lib4(s, kFullLib);
        const VS early = Tut4(s, "Profane Tutor");
        REQUIRE(early == VS{ kStone, kRitual, kAp4 });
        s.scripted_suspend_tutor = static_cast<std::int16_t>(pin < 0 ? -1 : SuspendTutorPinPack(1, &pin));   // the crossing's pin (MTG_PROFANE_RESOLVE_FAN)
        CastOffSuspend(s, 0, Card4("Profane Tutor", 93));
        REQUIRE(s.players[0].hand.size() == 1);
        const std::size_t want = pin < 0 ? 0 : std::min<std::size_t>(static_cast<std::size_t>(pin), early.size() - 1);
        CHECK(s.players[0].hand[0].m_name.str() == early[want]);
    }
    // Demonic Tutor in hand, unlimited enumeration: every searched pick indexes the early set.
    GameState s = Base4();
    s.battlefield.push_back(Perm4("Swamp", 1));
    s.battlefield.push_back(Perm4("Swamp", 2));
    s.players[0].hand.push_back(Card4("Demonic Tutor", 60));
    Lib4(s, kFullLib);
    const VS early = Tut4(s);
    int max_choice = -1;
    bool cast_seen = false;
    for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(s, true))
    {
        for (const Action& a : p.actions)
        {
            if (a.kind != Action::Kind::CastFromHand || a.card_name.str() != "Demonic Tutor") { continue; }
            cast_seen = true;
            if (!a.tutor_target.empty())
            { CHECK(std::find(early.begin(), early.end(), a.tutor_target.str()) != early.end()); }
            max_choice = std::max(max_choice, p.tutor_choice);
        }
    }
    CHECK(cast_seen);
    CHECK(max_choice < static_cast<int>(early.size()));
}

TEST_CASE("Shadowborn INT-4: other decks keep the generic lists (base hooks untouched)")
{
    Ensure4();
    GameState s = Base4();
    static const GenericProvider gen;
    s.m_provider = &gen;
    s.battlefield.push_back(Perm4("Swamp", 1));
    for (int k = 0; k < 6; ++k) { s.battlefield.push_back(Perm4(kAp4, 10 + k)); }
    Lib4(s, { kReaper, kSire, kJetmir, "Swamp", kOverlord });
    std::vector<std::string> out;
    CHECK_FALSE(gen.SacNamedPutTargets(s, 0, Def4(kAp4).params, out));
    // The engine's own list: every Demon + the fail-to-find.
    CHECK(Targets4(Acts4(s)).size() == 5);
    CHECK(RankedTutorCandidates(gen, s, 0, Def4("Demonic Tutor").params).size() == 5);
}
