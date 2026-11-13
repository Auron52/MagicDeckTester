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

#include <algorithm>
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
    ArmDx legacy(heurarm::DOUBLER_COMBAT_PAY, 0);   // the approximate probe (MTG_DOUBLER_COMBAT_PAY=0)
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
    ArmDx legacy(heurarm::DOUBLER_COMBAT_PAY, 0);   // the approximate probe (MTG_DOUBLER_COMBAT_PAY=0)
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
    ArmDx legacy(heurarm::DOUBLER_COMBAT_PAY, 0);   // the approximate probe (MTG_DOUBLER_COMBAT_PAY=0)
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
    ArmDx legacy(heurarm::DOUBLER_COMBAT_PAY, 0);   // the approximate probe (MTG_DOUBLER_COMBAT_PAY=0)
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
    ArmDx legacy(heurarm::DOUBLER_COMBAT_PAY, 0);   // the approximate probe (MTG_DOUBLER_COMBAT_PAY=0)
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
    ArmDx legacy(heurarm::DOUBLER_COMBAT_PAY, 0);   // the approximate probe (MTG_DOUBLER_COMBAT_PAY=0)
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
    ArmDx legacy(heurarm::DOUBLER_COMBAT_PAY, 0);   // the approximate probe (MTG_DOUBLER_COMBAT_PAY=0)
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
    ArmDx legacy(heurarm::DOUBLER_COMBAT_PAY, 0);   // the approximate probe (MTG_DOUBLER_COMBAT_PAY=0)
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

// MTG_EXALTED_DS_PROJECTION: exalted is POWER until end of turn (CR 702.83a), so a double-striking lone
// attacker (Temur Battle Rage) deals the +E in BOTH damage steps. The combat core folds it into base_pw;
// the attack projection used to add it once and under-count by E.
TEST_CASE("exalted ds projection: a double-striking lone attacker's projection counts exalted twice")
{
    EnsureCardsDx();
    GameState s = BaseDx();
    PutDx(s, "Ignoble Hierarch", 1, /*sick=*/true);
    PutDx(s, "Ignoble Hierarch", 2, /*sick=*/true);
    const int bear = PutBearDx(s, 3);
    s.battlefield[static_cast<std::size_t>(bear)].temp_double_strike = true;
    REQUIRE(CountExalted(s.battlefield, 0) == 2);
    const int real = LoneDamageDx(s, bear);
    CHECK(real == 2 * (2 + 2));                                    // (2 power + exalted 2) x 2 strikes
    CHECK(TurnSolver::ProjectedAttackDamageForTest(s) == real);
    {
        ArmDx off(heurarm::EXALTED_DS_PROJECTION, 0);
        CHECK(TurnSolver::ProjectedAttackDamageForTest(s) == real - 2);   // the old once-only +E
    }
    // A single striker is unchanged by the fix: +E once either way.
    s.battlefield[static_cast<std::size_t>(bear)].temp_double_strike = false;
    CHECK(TurnSolver::ProjectedAttackDamageForTest(s) == LoneDamageDx(s, bear));
    CHECK(LoneDamageDx(s, bear) == 4);
}

TEST_CASE("exalted ds projection: a deferred Berserk on a double striker -- projection == combat")
{
    EnsureCardsDx();
    ArmDx legacy(heurarm::DOUBLER_COMBAT_PAY, 0);   // the approximate probe (MTG_DOUBLER_COMBAT_PAY=0)
    GameState s = BaseDx();
    PutDx(s, "Ignoble Hierarch", 1, /*sick=*/true);
    const int bear = PutBearDx(s, 3);
    s.battlefield[static_cast<std::size_t>(bear)].temp_double_strike = true;
    ProbeOk(s, 1);
    CastDx(s, "Berserk", 3);
    const int real = LoneDamageDx(s, bear);
    CHECK(real == 2 * (2 * (2 + 1)));                              // Berserk after exalted, both strikes
    CHECK(TurnSolver::ProjectedAttackDamageForTest(s) == real);
}

// ---- EXACT COMBAT PAYMENT (MTG_DOUBLER_COMBAT_PAY, default ON; Morph INT-16) ----------------------------------
// USER 2026-11-13: "If we are approximating and this might fail on us we might want to address it." A doubler is
// credited only when its cast was PAID as the combat step pays (SpellEffects.h DoublerCastPay, the hook both
// appliers' cast payment goes through), so the three approximations of the old probe cannot credit damage the
// turn cannot deal.
namespace
{
// A doubler's cast as the appliers make it: the shared payment hook (else the plain payer), the storm counter,
// the shared resolver, then the card to the graveyard.
bool PayCastDx(GameState& s, const std::string& spell, int target_number, int card_number)
{
    const CardDefinition& d = DefDx(spell);
    bool paid = false;
    if (!DoublerCastPay(s, d, card_number, [&] { return TapForCostDirect(s, d.card.m_mana_cost, false); }, paid))
    { paid = TapForCostDirect(s, d.card.m_mana_cost, false); }
    if (!paid) { return false; }
    ++s.spells_cast_this_turn;
    REQUIRE(ResolveSoloTargetTrick(s, 0, d, target_number, 0, 0, 0));
    Card c = d.card;
    c.m_number = card_number;
    s.players[0].graveyard.push_back(c);
    return true;
}
bool TappedDx(const GameState& s, int number)
{
    for (const Permanent& p : s.battlefield) { if (p.card.m_number == number) { return p.tapped; } }
    FAIL("no permanent #", number);
    return false;
}
Action FlipLateDx(int source_number, const ManaCost& cost)
{
    Action a;
    a.kind          = Action::Kind::ActivatePermAbility;
    a.ability_mode  = Action::AbilityMode::TurnFaceUpLate;
    a.sac_source_id = source_number;
    a.card_name     = "Blistering Firecat";
    a.cost          = cost;
    return a;
}
}  // namespace

TEST_CASE("doubler combat pay: an armed doubler pays from untapped sources, main's float set aside")
{
    EnsureCardsDx();
    GameState s = BaseDx();
    PutDx(s, "Ignoble Hierarch", 30, /*sick=*/true);
    const int b = PutBearDx(s, 10);
    PutDx(s, "Forest", 40);
    s.floating_mana.green = 1;   // main's float: a combat cast can never spend it (CR 500.4)
    TurnSolver::NoteDoublerDeferPayable(s, { CastActDx("Berserk") });
    REQUIRE(DblCpLive(s) != nullptr);
    REQUIRE(PayCastDx(s, "Berserk", 10, 77));
    CHECK(TappedDx(s, 40));                    // the Forest paid it...
    CHECK(s.floating_mana.green == 1);         // ...not the float, which empties with main
    CHECK(s.dbl_defer_ndef[0] == 1);
    CHECK(ExtraDx(s, b) == 1);
    CHECK(LoneDamageDx(s, b) == 6);
    // Only the float: not payable in combat -> paid in main, and nothing is credited.
    GameState t = BaseDx();
    PutDx(t, "Ignoble Hierarch", 30, /*sick=*/true);
    const int tb = PutBearDx(t, 10);
    t.floating_mana.green = 1;
    TurnSolver::NoteDoublerDeferPayable(t, { CastActDx("Berserk") });
    REQUIRE(PayCastDx(t, "Berserk", 10, 77));
    CHECK(t.floating_mana.green == 0);
    CHECK(ExtraDx(t, tb) == 0);
    CHECK(LoneDamageDx(t, tb) == 5);
}

TEST_CASE("doubler combat pay (ii): a trailing activation's cost is in the main side")
{
    EnsureCardsDx();
    // Gruul Turf + Mountain; Berserk {G}, then a trailing Firecat flip {R}{R}. Main pays both (Turf: G -> Berserk,
    // the floating R + Mountain -> the flip); with Berserk in combat the flip needs the Turf's R AND the Mountain,
    // and the Turf's G empties with main. The old probe ignored the flip and credited the combat cast.
    auto board = []
    {
        GameState s = BaseDx();
        PutDx(s, "Ignoble Hierarch", 30, /*sick=*/true);
        PutBearDx(s, 10);
        PutDx(s, "Gruul Turf", 40);
        PutDx(s, "Mountain", 41);
        return s;
    };
    ManaCost rr; rr.red = 2;
    const std::vector<Action> plan = { CastActDx("Berserk"), FlipLateDx(20, rr) };
    {
        ArmDx legacy(heurarm::DOUBLER_COMBAT_PAY, 0);
        GameState s = board();
        TurnSolver::NoteDoublerDeferPayable(s, plan);
        CHECK(s.dbl_pay_ok == 1);              // the approximation: the flip's {R}{R} was not counted
    }
    GameState s = board();
    TurnSolver::NoteDoublerDeferPayable(s, plan);
    const GameState::DblCombatPay* cp = DblCpLive(s);
    CHECK((cp == nullptr || cp->recs.empty()));   // nothing armed: Berserk stays in main
    REQUIRE(PayCastDx(s, "Berserk", 10, 77));
    CHECK(ExtraDx(s, 1) == 0);
    CHECK(TapForCostDirect(s, rr, false));     // and the flip is still paid
}

TEST_CASE("doubler combat pay (i): doublers of two decisions never share a source; the earlier one is re-paid in main")
{
    EnsureCardsDx();
    // Turf (tapped for an earlier Might: {R} floating), Mountain, Forest. Decision 1 casts Legion Leadership,
    // decision 2 Berserk. Combat cannot pay both ({1}{R/W} + {G} from two lands), so the best real schedule is
    // Leadership in main (floating R + Mountain) and Berserk in combat (Forest): ONE doubling after exalted.
    // The old per-decision probe found each payable alone (both off the Forest) and credited two.
    auto board = []
    {
        GameState s = BaseDx();
        PutDx(s, "Ignoble Hierarch", 30, /*sick=*/true);
        PutBearDx(s, 10);
        const int turf = PutDx(s, "Gruul Turf", 40);
        PutDx(s, "Mountain", 41);
        PutDx(s, "Forest", 42);
        s.battlefield[static_cast<std::size_t>(turf)].tapped = true;
        s.floating_mana.red = 1;
        ++s.spells_cast_this_turn;   // the Might (resolved below)
        REQUIRE(ResolveSoloTargetTrick(s, 0, DefDx("Might of Old Krosa"), 10, 0, 0, 0));
        return s;
    };
    {
        ArmDx legacy(heurarm::DOUBLER_COMBAT_PAY, 0);
        GameState s = board();
        TurnSolver::NoteDoublerDeferPayable(s, { CastActDx("Legion Leadership") });
        CastDx(s, "Legion Leadership", 10);
        s.floating_mana.red = 0;               // main paid it: floating R + Mountain
        s.battlefield[3].tapped = true;
        TurnSolver::NoteDoublerDeferPayable(s, { CastActDx("Berserk") });
        CastDx(s, "Berserk", 10);
        CHECK(ExtraDx(s, 1) == 3);             // the approximation: E x (2^2 - 1)
    }
    GameState s = board();
    TurnSolver::NoteDoublerDeferPayable(s, { CastActDx("Legion Leadership") });
    REQUIRE(PayCastDx(s, "Legion Leadership", 10, 71));
    CHECK(TappedDx(s, 41));
    CHECK(TappedDx(s, 42));                    // combat-paid off both lands; the R still floats in main
    CHECK(s.floating_mana.red == 1);
    CHECK(s.dbl_defer_ndef[0] == 1);
    TurnSolver::NoteDoublerDeferPayable(s, { CastActDx("Berserk") });
    REQUIRE(PayCastDx(s, "Berserk", 10, 72));
    CHECK(s.floating_mana.red == 0);           // Leadership went back to main: floating R + Mountain
    CHECK(TappedDx(s, 41));
    CHECK(TappedDx(s, 42));                    // the Forest paid Berserk in combat
    CHECK(s.dbl_defer_n[0] == 2);
    CHECK(s.dbl_defer_ndef[0] == 1);
    CHECK(ExtraDx(s, 1) == 1);
    CHECK(LoneDamageDx(s, 1) == (2 + 4) * 4 + 1 + 1);
}

TEST_CASE("doubler combat pay (INT-17): a later decision RE-PAYS the earlier doubler in combat off another source")
{
    EnsureCardsDx();
    // Mountain + Forest; decision 1 casts Legion Leadership (its combat payment can only be Mountain + Forest).
    // A second Mountain then enters (the land drop of the next human line) and decision 2 casts Berserk ({G}).
    // The undo alone would move Leadership back to MAIN (one doubling after exalted); re-paying it in combat off
    // the two Mountains, with Berserk on the Forest, puts BOTH after exalted.
    GameState s = BaseDx();
    PutDx(s, "Ignoble Hierarch", 30, /*sick=*/true);
    const int b = PutBearDx(s, 10);
    PutDx(s, "Mountain", 41);
    PutDx(s, "Forest", 42);
    TurnSolver::NoteDoublerDeferPayable(s, { CastActDx("Legion Leadership") });
    REQUIRE(PayCastDx(s, "Legion Leadership", 10, 71));
    CHECK(TappedDx(s, 41));
    CHECK(TappedDx(s, 42));
    CHECK(s.dbl_defer_ndef[0] == 1);
    PutDx(s, "Mountain", 43);
    TurnSolver::NoteDoublerDeferPayable(s, { CastActDx("Berserk") });
    REQUIRE(PayCastDx(s, "Berserk", 10, 72));
    CHECK(TappedDx(s, 41));
    CHECK(TappedDx(s, 42));
    CHECK(TappedDx(s, 43));
    CHECK(s.dbl_defer_n[0] == 2);
    CHECK(s.dbl_defer_ndef[0] == 2);           // both paid in combat
    CHECK(ExtraDx(s, b) == 3);                 // E x (2^2 - 1)
    CHECK(LoneDamageDx(s, b) == (2 + 1) * 4);
}

TEST_CASE("doubler combat pay (iii): the board after combat carries the combat schedule's taps")
{
    EnsureCardsDx();
    // Gruul Turf x2 + Forest; a {3} cast and Berserk. Main pays both off the two Turfs (R,G,R -> {3}, G -> Berserk)
    // and leaves the Forest up; with Berserk in combat the {3} cannot use the Turfs' fourth unit, so all three
    // lands are tapped. The old probe credited the combat cast but left the Forest untapped.
    GameState s = BaseDx();
    PutDx(s, "Ignoble Hierarch", 30, /*sick=*/true);
    const int b = PutBearDx(s, 10);
    PutDx(s, "Gruul Turf", 40);
    PutDx(s, "Gruul Turf", 41);
    PutDx(s, "Forest", 42);
    Action three = CastActDx("Den Protector");
    three.cost = ManaCost{};
    three.cost.generic = 3;
    const std::vector<Action> plan = { three, CastActDx("Berserk") };
    TurnSolver::NoteDoublerDeferPayable(s, plan);
    std::vector<Action> scratch;
    const std::vector<Action>& pre = TurnSolver::DoublerPrepayActs(s, plan, scratch);
    CHECK(pre.size() == 1);                    // the armed Berserk is out of the whole-turn prepay
    REQUIRE(TapForCostDirect(s, three.cost, true));
    ++s.spells_cast_this_turn;
    REQUIRE(PayCastDx(s, "Berserk", 10, 77));
    CHECK(TappedDx(s, 40));
    CHECK(TappedDx(s, 41));
    CHECK(TappedDx(s, 42));
    CHECK(ExtraDx(s, b) == 1);
}

TEST_CASE("doubler combat pay: a main-paid run doubler keeps the combat one's credit; a regrown one loses it")
{
    EnsureCardsDx();
    GameState s = BaseDx();
    PutDx(s, "Ignoble Hierarch", 30, /*sick=*/true);
    const int b = PutBearDx(s, 10);
    PutDx(s, "Forest", 40);
    TurnSolver::NoteDoublerDeferPayable(s, { CastActDx("Berserk") });
    REQUIRE(PayCastDx(s, "Berserk", 10, 77));
    // A second doubler paid in main (nothing armed for it): the doublings commute, so it is cast at the end of
    // main and the first still doubles after exalted -- 2 -> 4 (main) -> +1 -> 10.
    s.floating_mana.green = 1;
    REQUIRE(PayCastDx(s, "Berserk", 10, 78));
    CHECK(s.dbl_defer_n[0] == 2);
    CHECK(s.dbl_defer_ndef[0] == 1);
    CHECK(LoneDamageDx(s, b) == (2 * 2 + 1) * 2);
    // Den Protector's regrowth takes the combat-paid Berserk back in main: it was never cast in combat.
    auto& gy = s.players[0].graveyard;
    gy.erase(std::remove_if(gy.begin(), gy.end(), [](const Card& c) { return c.m_number == 77; }), gy.end());
    CHECK(ExtraDx(s, b) == 0);
}
