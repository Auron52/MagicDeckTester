// Unit tests for DOUBLER AFTER EXALTED (MTG_DOUBLER_AFTER_EXALTED, USER 2026-11-13: "we need to be able to
// berserk and Legion Leadership in the attack phase, if there is exalted ... Anything doubler cast at the end
// will be assumed to be in the attack phase"). The model: GameState::dbl_defer_* (marked by the shared trick
// resolver), SpellEffects.h DoublerDeferExaltedExtra (every lone-attacker exalted site), and the CR 500.4
// payability probe TurnSolver::NoteDoublerDeferPayable (both plan appliers).
#include <doctest/doctest.h>

#include "ai/Combat.h"
#include "ai/HeuristicArm.h"
#include "ai/TurnSolver.h"
#include "cards/CardDatabase.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"

#include <string>
#include <vector>

namespace
{

void EnsureCardsDx()
{
    static const bool loaded = []
    {
        const auto path = ResolveHeuristicDefaultsPath("src/cards/data/cards.json");
        CardDatabase::Instance().LoadFromJson(path);
        return true;
    }();
    REQUIRE(loaded);
}

const CardDefinition& DefDx(const std::string& name)
{
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE_MESSAGE(d != nullptr, "card not in cards.json: ", name);
    return *d;
}

int PutDx(GameState& s, const std::string& name, int number, bool sick = false)
{
    Permanent p;
    p.card             = DefDx(name).card;
    p.card.m_number    = number;
    p.controller_index = 0;
    p.owner_index      = 0;
    p.entered_this_turn = sick;
    s.battlefield.push_back(p);
    return static_cast<int>(s.battlefield.size()) - 1;
}

int PutBearDx(GameState& s, int number)
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

GameState BaseDx()
{
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 3;
    s.phase               = Phase::PreCombatMain;
    s.players[0].life     = 20;
    s.players[1].life     = 20;
    return s;
}

// A cast as both appliers make it: the storm counter first, then the shared resolver.
void CastDx(GameState& s, const std::string& spell, int target_number)
{
    ++s.spells_cast_this_turn;
    REQUIRE(ResolveSoloTargetTrick(s, 0, DefDx(spell), target_number, 0, 0, 0));
}

// The probe's verdict, as if NoteDoublerDeferPayable had found the run payable in combat.
void ProbeOk(GameState& s, int casts)
{
    s.dbl_pay_turn       = static_cast<std::int16_t>(s.TurnSeq());
    s.dbl_pay_max_spells = static_cast<std::int16_t>(s.spells_cast_this_turn + casts);
    s.dbl_pay_ok         = 1;
}

int ExtraDx(const GameState& s, int bf)
{
    const int ex = CountExalted(s.battlefield, 0);
    return DoublerDeferExaltedExtra(s, s.battlefield[static_cast<std::size_t>(bf)], ex);
}

// Lone-attacker combat damage, exactly as the two combat sites compute it.
int LoneDamageDx(GameState s, int bf)
{
    int ex = CountExalted(s.battlefield, 0);
    if (ex > 0) { ex += DoublerDeferExaltedExtra(s, s.battlefield[static_cast<std::size_t>(bf)], ex); }
    s.phase = Phase::Combat;
    const int before = s.players[1].life;
    ResolveCombatDamage(s, std::vector<int>{ bf }, ex, false);
    return before - s.players[1].life;
}

Action CastActDx(const std::string& name)
{
    Action a;
    a.kind      = Action::Kind::CastFromHand;
    a.card_name = name;
    a.def       = &DefDx(name);
    a.cost      = DefDx(name).card.m_mana_cost;
    return a;
}

struct ArmDx
{
    heurarm::Slot slot;
    ArmDx(heurarm::Slot s, int v) : slot(s) { heurarm::t_arm[s] = static_cast<std::int8_t>(v); }
    ~ArmDx() { heurarm::t_arm[slot] = -1; }
};

}  // namespace

TEST_CASE("doubler after exalted: a trailing Berserk doubles exalted's +1 too")
{
    EnsureCardsDx();
    GameState s = BaseDx();
    PutDx(s, "Ignoble Hierarch", 30);
    const int b = PutBearDx(s, 10);
    ProbeOk(s, 1);
    CastDx(s, "Berserk", 10);
    CHECK(CombatPowerOf(s.battlefield[static_cast<std::size_t>(b)], s) == 4);   // resolved in main: 2 -> 4
    CHECK(ExtraDx(s, b) == 1);                 // (2 + 1) x 2 = 6, not 4 + 1 = 5
    CHECK(LoneDamageDx(s, b) == 6);
    CHECK(s.DoublerDeferKey() != -1);
}

TEST_CASE("doubler after exalted: two trailing doublers -> E x (2^2 - 1); two Hierarchs -> E = 2")
{
    EnsureCardsDx();
    GameState s = BaseDx();
    PutDx(s, "Ignoble Hierarch", 30);
    PutDx(s, "Ignoble Hierarch", 31);
    const int b = PutBearDx(s, 10);
    ProbeOk(s, 2);
    CastDx(s, "Berserk", 10);
    CastDx(s, "Legion Leadership", 10);
    CHECK(ExtraDx(s, b) == 2 * 3);
    CHECK(LoneDamageDx(s, b) == (2 + 2) * 4);
}

TEST_CASE("doubler after exalted: Wild Defiance still fires; the doubled total includes it and exalted")
{
    EnsureCardsDx();
    GameState s = BaseDx();
    s.deck_has_spell_target_watch = true;
    PutDx(s, "Ignoble Hierarch", 30);
    PutDx(s, "Wild Defiance", 20);
    const int b = PutBearDx(s, 10);
    ProbeOk(s, 1);
    CastDx(s, "Berserk", 10);
    CHECK(CombatPowerOf(s.battlefield[static_cast<std::size_t>(b)], s) == 10);   // (2 + 3) x 2
    CHECK(LoneDamageDx(s, b) == (2 + 3 + 1) * 2);
}

TEST_CASE("doubler after exalted: a spell cast AFTER the doubler voids the run (it no longer trails)")
{
    EnsureCardsDx();
    GameState s = BaseDx();
    PutDx(s, "Ignoble Hierarch", 30);
    const int b = PutBearDx(s, 10);
    ProbeOk(s, 2);
    CastDx(s, "Berserk", 10);
    CastDx(s, "Might of Old Krosa", 10);       // 4 + 4 = 8; Berserk did not trail
    CHECK(ExtraDx(s, b) == 0);
    CHECK(LoneDamageDx(s, b) == 9);
    CHECK(s.DoublerDeferKey() == -1);
}

TEST_CASE("doubler after exalted: pump first, doubler last -> the pump AND exalted are doubled")
{
    EnsureCardsDx();
    GameState s = BaseDx();
    PutDx(s, "Ignoble Hierarch", 30);
    const int b = PutBearDx(s, 10);
    ProbeOk(s, 2);
    CastDx(s, "Might of Old Krosa", 10);
    CastDx(s, "Berserk", 10);
    CHECK(LoneDamageDx(s, b) == (2 + 4 + 1) * 2);
}

TEST_CASE("doubler after exalted: a power change after the run (an activation) voids it for that creature")
{
    EnsureCardsDx();
    GameState s = BaseDx();
    PutDx(s, "Ignoble Hierarch", 30);
    const int b = PutBearDx(s, 10);
    ProbeOk(s, 1);
    CastDx(s, "Berserk", 10);
    s.battlefield[static_cast<std::size_t>(b)].temp_power_bonus += 1;   // e.g. an activated pump
    CHECK(ExtraDx(s, b) == 0);
}

TEST_CASE("doubler after exalted: inert without exalted, without the probe's verdict, off the flag, next turn")
{
    EnsureCardsDx();
    {
        GameState s = BaseDx();                 // no Hierarch
        const int b = PutBearDx(s, 10);
        ProbeOk(s, 1);
        CastDx(s, "Berserk", 10);
        CHECK(ExtraDx(s, b) == 0);
        CHECK(s.DoublerDeferKey() == -1);
    }
    {
        GameState s = BaseDx();                 // the probe found it unpayable in combat
        PutDx(s, "Ignoble Hierarch", 30);
        const int b = PutBearDx(s, 10);
        CastDx(s, "Berserk", 10);
        CHECK(ExtraDx(s, b) == 0);
    }
    {
        ArmDx off(heurarm::DOUBLER_AFTER_EXALTED, 0);
        GameState s = BaseDx();
        PutDx(s, "Ignoble Hierarch", 30);
        const int b = PutBearDx(s, 10);
        ProbeOk(s, 1);
        CastDx(s, "Berserk", 10);
        CHECK(ExtraDx(s, b) == 0);
        CHECK(LoneDamageDx(s, b) == 5);
    }
    {
        GameState s = BaseDx();                 // marked last turn: stale
        PutDx(s, "Ignoble Hierarch", 30);
        const int b = PutBearDx(s, 10);
        ProbeOk(s, 1);
        CastDx(s, "Berserk", 10);
        ++s.turn_number;
        s.spells_cast_this_turn = 1;
        CHECK(ExtraDx(s, b) == 0);
    }
    {
        GameState s = BaseDx();                 // a post-combat main's doubler is not deferred
        s.phase = Phase::PostCombatMain;
        PutDx(s, "Ignoble Hierarch", 30);
        const int b = PutBearDx(s, 10);
        ProbeOk(s, 1);
        CastDx(s, "Berserk", 10);
        CHECK(ExtraDx(s, b) == 0);
    }
}

TEST_CASE("doubler after exalted: the CR 500.4 probe -- payable in combat from untapped sources only")
{
    EnsureCardsDx();
    {
        // Forest + Forest: Might off one in main, Berserk off the other in combat.
        GameState s = BaseDx();
        PutDx(s, "Ignoble Hierarch", 30, /*sick=*/true);
        PutDx(s, "Forest", 40);
        PutDx(s, "Forest", 41);
        TurnSolver::NoteDoublerDeferPayable(s, { CastActDx("Might of Old Krosa"), CastActDx("Berserk") });
        CHECK(s.dbl_pay_turn == s.TurnSeq());
        CHECK(s.dbl_pay_ok == 1);
    }
    {
        // Gruul Turf + Mountain, Might {G} + Legion Leadership {1}{R/W}: main can pay both (Turf's {R}{G}:
        // G -> Might, the floating R + Mountain -> Leadership), but in combat the floating R is gone and no
        // split works (Mountain cannot pay Might's {G}; the Turf alone is Leadership's only two units).
        GameState s = BaseDx();
        PutDx(s, "Ignoble Hierarch", 30, /*sick=*/true);
        PutDx(s, "Gruul Turf", 40);
        PutDx(s, "Mountain", 41);
        TurnSolver::NoteDoublerDeferPayable(s, { CastActDx("Might of Old Krosa"), CastActDx("Legion Leadership") });
        CHECK(s.dbl_pay_turn == s.TurnSeq());
        CHECK(s.dbl_pay_ok == 0);
    }
    {
        // Gruul Turf + a READY Hierarch: Might off the Hierarch in main, Leadership off the Turf in combat --
        // found although the greedy order (Might first) would tap the Turf for it.
        GameState s = BaseDx();
        PutDx(s, "Ignoble Hierarch", 30);
        PutDx(s, "Gruul Turf", 40);
        TurnSolver::NoteDoublerDeferPayable(s, { CastActDx("Might of Old Krosa"), CastActDx("Legion Leadership") });
        CHECK(s.dbl_pay_ok == 1);
    }
    {
        // No doubler in the plan, or no exalted: nothing stamped.
        GameState s = BaseDx();
        PutDx(s, "Forest", 40);
        PutDx(s, "Ignoble Hierarch", 30);
        TurnSolver::NoteDoublerDeferPayable(s, { CastActDx("Might of Old Krosa") });
        CHECK(s.dbl_pay_turn == -1);
        GameState t = BaseDx();
        PutDx(t, "Forest", 40);
        TurnSolver::NoteDoublerDeferPayable(t, { CastActDx("Berserk") });
        CHECK(t.dbl_pay_turn == -1);
    }
}
