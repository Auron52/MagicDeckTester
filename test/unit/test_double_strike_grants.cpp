// Unit tests for the two DOUBLE-STRIKE GRANTS added with the WhiteKnights onboarding (2026-09-25),
// and for the shared oracle they were the reason for:
//
//   * Valiant Knight   "{3}{W}{W}: Knights you control gain double strike until end of turn."
//                      -> team_pump_grants_double_strike -> Permanent::temp_double_strike
//   * Silverblade Paladin  soulbond: "As long as this creature is paired with another creature,
//                      both creatures have double strike."   (Stage D -- see the second section)
//
// WHY THESE ARE UNIT TESTS. Before this work the same three-way double-strike expression was
// open-coded at THREE damage sites (ResolveCombatDamage and TurnSolver's two attack projections).
// A grant added to combat but missed in a projection does not crash, does not diverge loudly, and
// does not change any digest a neutrality A/B would look at -- it just makes the search price an
// attack differently from the attack it then delivers. The observable here is combat damage, which
// is the only thing that can actually catch it.
//
//  1. The grant FIRES: a Knight's damage doubles after the activation.
//  2. It is SUBTYPE-FILTERED: a non-Knight on the same board gains nothing.
//  3. It INCLUDES the source. The grant says "Knights you control", where the lord clause on the
//     same card says "Other Knights" -- so Valiant Knight double-strikes itself while NOT giving
//     itself +1/+1. Getting these two the same way round is a one-word mistake in cards.json.
//  4. It is UNTIL-EOT state (CR 514.2) that the dominance clean-boundary guard recognises -- an
//     until-EOT flag missing from that guard lets two boards differing only in "the team is
//     double-striking" be declared equivalent, and an uncleared one is a permanent buff that only
//     shows up as "this deck is oddly good". The two real reset sites are not callable from here
//     (CleanupStep is private); see the note in that test for how they are covered instead.
//  5. It is IDEMPOTENT, which is what licenses capping K at 1 in the enumeration.
//  6. The shared oracle still answers for the pre-existing sources (Kor Duelist's Equipment grant
//     and a subtype lord), because Stage B rewrote all three sites to call it.
#include <doctest/doctest.h>

#include "cards/CardDatabase.h"
#include "core/GameState.h"
#include "core/GameSetup.h"
#include "ai/Dominance.h"
#include "core/SpellEffects.h"
#include "core/HeuristicDefaults.h"
#include "ai/Combat.h"

#include <string>
#include <vector>

namespace
{

void EnsureCardsLoaded()
{
    static const bool loaded = []
    {
        const auto path = ResolveHeuristicDefaultsPath("src/cards/data/cards.json");
        CardDatabase::Instance().LoadFromJson(path);
        return true;
    }();
    REQUIRE(loaded);
}

const CardDefinition& Def(const std::string& name)
{
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE_MESSAGE(d != nullptr, "card not in cards.json: ", name);
    return *d;
}

int Put(GameState& s, const std::string& name, int controller, int number)
{
    Permanent p;
    p.card             = Def(name).card;
    p.card.m_number    = number;
    p.controller_index = controller;
    p.owner_index      = controller;
    s.battlefield.push_back(p);
    RefreshDevotionCreatures(s);
    return static_cast<int>(s.battlefield.size()) - 1;
}

const Permanent& ByNumber(const GameState& s, int number)
{
    for (const Permanent& p : s.battlefield) { if (p.card.m_number == number) { return p; } }
    static Permanent none;
    REQUIRE_MESSAGE(false, "permanent not found: ", number);
    return none;
}

GameState Fresh()
{
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 6;
    s.players[0].life     = gamesetup::StartingLife();
    s.players[1].life     = gamesetup::StartingLife();
    return s;
}

// Activate the source's team grant K times, through the SAME ApplyActivatePump the executor and the
// rollout both call (mode 2 = the team pump + keyword riders).
int ActivateTeamGrant(GameState& s, int controller, int source_number, int k = 1)
{
    return ApplyActivatePump(s, controller, source_number, /*mode=*/2, k);
}

// Total combat damage for a declared attack, through the real combat core -- the one site whose
// answer actually matters, and the only place a missed double-strike grant is observable.
int SwingFor(GameState& s, const std::vector<int>& atk_idx)
{
    return ResolveCombatDamage(s, atk_idx, /*exalted_bonus=*/0, /*collect_descs=*/false)
               .total_damage;
}

}   // namespace

TEST_CASE("Valiant Knight's grant FIRES: a Knight's combat damage doubles")
{
    EnsureCardsLoaded();

    // Baseline: one Venerable Knight (2/1) swinging under Valiant Knight's +1/+1 lord = 3 damage.
    // Valiant Knight stays home so only one attacker's arithmetic is in play.
    int base = 0;
    {
        GameState s = Fresh();
        Put(s, "Valiant Knight", 0, 1);
        const int vk = Put(s, "Venerable Knight", 0, 2);
        base = SwingFor(s, {vk});
        CHECK(base == 3);                      // 2 printed + 1 lord, single strike
    }

    // Same board, grant activated first: the same attacker must deal exactly twice as much.
    {
        GameState s = Fresh();
        Put(s, "Valiant Knight", 0, 1);
        const int vk = Put(s, "Venerable Knight", 0, 2);
        REQUIRE(ActivateTeamGrant(s, 0, /*source_number=*/1) == 1);
        CHECK(ByNumber(s, 2).temp_double_strike);
        CHECK(CreatureHasDoubleStrike(ByNumber(s, 2), s));
        CHECK(SwingFor(s, {vk}) == base * 2);  // 6, not 3
    }
}

TEST_CASE("Valiant Knight's grant is SUBTYPE-FILTERED: a non-Knight gains nothing")
{
    EnsureCardsLoaded();
    GameState s = Fresh();
    Put(s, "Valiant Knight", 0, 1);
    Put(s, "Venerable Knight", 0, 2);       // Human Knight -> granted
    Put(s, "Monastery Swiftspear", 0, 3);   // Human MONK, not a Knight -> not granted

    REQUIRE(ActivateTeamGrant(s, 0, 1) == 1);

    CHECK(ByNumber(s, 2).temp_double_strike);
    CHECK_FALSE(ByNumber(s, 3).temp_double_strike);
    CHECK_FALSE(CreatureHasDoubleStrike(ByNumber(s, 3), s));
}

TEST_CASE("Valiant Knight: the grant INCLUDES itself, the +1/+1 lord EXCLUDES itself")
{
    EnsureCardsLoaded();
    GameState s = Fresh();
    Put(s, "Valiant Knight", 0, 1);

    REQUIRE(ActivateTeamGrant(s, 0, 1) == 1);

    // "Knights you control gain double strike" -- no 'other', so it covers itself.
    const Permanent& self = ByNumber(s, 1);
    CHECK(self.temp_double_strike);
    CHECK(CreatureHasDoubleStrike(self, s));
    // "OTHER Knights you control get +1/+1" -- so it does NOT pump itself. If cards.json had
    // lord_excludes_self wrong, this reads 1 and Valiant Knight is a 4/5.
    CHECK(ComputeLordBonus(self.card, s, 0, false, &self).first == 0);
    CHECK(self.EffectivePower() == 3);
}

TEST_CASE("Valiant Knight's grant is recognised as UNTIL-EOT state by the clean-boundary guard")
{
    EnsureCardsLoaded();
    GameState s = Fresh();
    Put(s, "Valiant Knight", 0, 1);
    Put(s, "Venerable Knight", 0, 2);

    // A board with no until-EOT state is a legal dominance comparison point.
    CHECK(dominance::AtCleanBoundary(s));

    REQUIRE(ActivateTeamGrant(s, 0, 1) == 1);
    REQUIRE(ByNumber(s, 2).temp_double_strike);

    // Carrying the grant, it must NOT be. This is the guard that makes the field safe to leave out
    // of a DomAxis: a state still holding it is refused rather than compared. Omitting the field
    // here would let two boards that differ only in "the team is double-striking" be declared
    // equivalent, and the dominance prune would discard the better one.
    CHECK_FALSE(dominance::AtCleanBoundary(s));

    // NOTE ON THE ACTUAL EXPIRY: the two reset sites (GameEngine::CleanupStep and
    // TurnSolver::SimulateEndAndStartNextTurn) cannot be called from here -- CleanupStep is
    // private and the rollout's twin needs a full AIEngine and deck. Both were verified by
    // reading, and the whole-game firing evidence (logs/wk_firing) shows the grant present during
    // one combat and absent the following turn, which is the end-to-end form of this check.
    for (Permanent& p : s.battlefield) { p.temp_double_strike = false; }   // simulate the cleanup
    CHECK(dominance::AtCleanBoundary(s));
    CHECK_FALSE(CreatureHasDoubleStrike(ByNumber(s, 2), s));
}

TEST_CASE("Valiant Knight's grant is IDEMPOTENT -- which is why K is capped at 1")
{
    EnsureCardsLoaded();
    GameState s = Fresh();
    Put(s, "Valiant Knight", 0, 1);
    const int vk = Put(s, "Venerable Knight", 0, 2);

    const int once = [&] {
        GameState a = Fresh();
        Put(a, "Valiant Knight", 0, 1);
        const int i = Put(a, "Venerable Knight", 0, 2);
        ActivateTeamGrant(a, 0, 1, 1);
        return SwingFor(a, {i});
    }();

    // Three activations must produce EXACTLY the same board and the same damage as one. If the
    // payload ever gains a stacking component, this test fails and the K cap has to be revisited.
    REQUIRE(ActivateTeamGrant(s, 0, 1, 3) == 3);
    CHECK(ByNumber(s, 2).temp_power_bonus == 0);   // team_pump_power is 0: nothing accrues
    CHECK(SwingFor(s, {vk}) == once);
}

// ---------------------------------------------------------------------------------------------
// SOULBOND (CR 702.46b) -- Silverblade Paladin
//
// Every case here is a rule that is wrong in a way that still plays a plausible game:
//   7.  "when EITHER enters" is TWO triggers. Implementing only the Paladin's own entry is the easy
//       half and silently loses every game where the Paladin lands first -- which, on a 2-drop in a
//       deck of 1- and 2-drops, is most of them.
//   8.  The payload is SYMMETRIC ("both creatures have double strike"), so it must be answerable
//       from the PARTNER's side too -- and the pair link is stored only on the Paladin, so that
//       direction needs the bs.ds prefilter to include soulbond sources (the Stage E widening).
//   9.  "ANOTHER unpaired creature": never itself, and never a creature already in a pair.
//   10. The pair breaks when the partner leaves, with no detach bookkeeping, because every read
//       re-verifies. A stale m_number must not resolve onto anything.
//   11. It survives CLEANUP. This is the opposite of temp_double_strike on the same card pair, and
//       classifying it with the until-EOT family would have ended the pair every turn.
// ---------------------------------------------------------------------------------------------

// Route an enter through the SAME universal cascade both worlds use -- the soulbond triggers live
// there, so a test that hand-set paired_with would prove nothing about them firing.
namespace
{
int Enter(GameState& s, const std::string& name, int controller, int number)
{
    const int idx = Put(s, name, controller, number);
    s.battlefield[idx].entered_this_turn = true;
    FireEtbWatchers(s, controller, idx);
    return idx;
}
}   // namespace

TEST_CASE("soulbond trigger A: the Paladin enters SECOND and pairs with a creature already out")
{
    EnsureCardsLoaded();
    GameState s = Fresh();
    Put(s, "Venerable Knight", 0, 2);                 // already on the battlefield
    Enter(s, "Silverblade Paladin", 0, 1);

    CHECK(ByNumber(s, 1).paired_with == 2);
    CHECK(SoulbondPartnerIndex(ByNumber(s, 1), s) >= 0);
    CHECK(CreatureHasDoubleStrike(ByNumber(s, 1), s));   // the Paladin
    CHECK(CreatureHasDoubleStrike(ByNumber(s, 2), s));   // AND the partner -- "both creatures"
}

TEST_CASE("soulbond trigger B: the Paladin enters FIRST and pairs with the next creature")
{
    EnsureCardsLoaded();
    GameState s = Fresh();
    Enter(s, "Silverblade Paladin", 0, 1);            // nothing to pair with yet
    REQUIRE(ByNumber(s, 1).paired_with == 0);
    CHECK_FALSE(CreatureHasDoubleStrike(ByNumber(s, 1), s));

    Enter(s, "Venerable Knight", 0, 2);               // "or another creature enters"

    CHECK(ByNumber(s, 1).paired_with == 2);
    CHECK(CreatureHasDoubleStrike(ByNumber(s, 1), s));
    CHECK(CreatureHasDoubleStrike(ByNumber(s, 2), s));
}

TEST_CASE("soulbond: the partner's double strike is reachable through the bs.ds prefilter")
{
    EnsureCardsLoaded();
    GameState s = Fresh();
    Put(s, "Venerable Knight", 0, 2);
    Enter(s, "Silverblade Paladin", 0, 1);
    REQUIRE(ByNumber(s, 1).paired_with == 2);

    // The combat sites pass this prefilter, so if a soulbond source were not admitted to it the
    // PARTNER's double strike would be invisible in real combat while still passing the
    // nullptr-prefilter unit checks above -- a feature that builds, digests clean and never fires.
    const BoardSources bs = GatherBoardSources(s.battlefield, 0);
    CHECK(bs.ds.size() == 1);
    CHECK(CreatureHasDoubleStrike(ByNumber(s, 2), s, &bs.ds));

    // And it is observable as real damage: a 2/2 partner swinging deals 4.
    const int partner_idx = [&] {
        for (int i = 0; i < static_cast<int>(s.battlefield.size()); ++i)
        { if (s.battlefield[i].card.m_number == 2) { return i; } }
        return -1;
    }();
    REQUIRE(partner_idx >= 0);
    CHECK(SwingFor(s, {partner_idx}) == 4);          // 2 power, doubled
}

TEST_CASE("soulbond pairs with ANOTHER creature: never itself, never an already-paired one")
{
    EnsureCardsLoaded();
    GameState s = Fresh();
    Enter(s, "Silverblade Paladin", 0, 1);
    CHECK(ByNumber(s, 1).paired_with != 1);           // never itself
    CHECK(ByNumber(s, 1).paired_with == 0);           // and nothing else is out yet

    Enter(s, "Venerable Knight", 0, 2);
    REQUIRE(ByNumber(s, 1).paired_with == 2);

    // A SECOND Paladin may not claim the same body -- a creature is in at most one pair. It must
    // instead pair with the other free creature.
    Put(s, "Worthy Knight", 0, 4);
    Enter(s, "Silverblade Paladin", 0, 3);
    CHECK(ByNumber(s, 3).paired_with != 2);
    CHECK(ByNumber(s, 3).paired_with == 4);
    CHECK(ByNumber(s, 1).paired_with == 2);           // the first pair is undisturbed
}

TEST_CASE("soulbond trigger B pairs ONLY with the creature that entered")
{
    // REGRESSION (found 2026-09-26 by a claude-play sweep agent, seed 7877).
    //
    // soulbond's two abilities have DIFFERENT partner sets (CR 702.46b):
    //   A: "...pair this creature with another unpaired creature you control as it enters" -> any.
    //   B: "Whenever another creature you control enters, if this creature is unpaired, you may
    //       pair it with THAT creature"                                          -> the entrant ONLY.
    // Trigger B was passing the unrestricted legal-partner list, so an unpaired Paladin could pair
    // with a creature that entered turns earlier -- a pairing no ability in the game grants. The
    // agent swung a turn by 4 extra damage that way.
    //
    // The test is built so it FAILS on the old code rather than merely describing the new: the
    // stale candidate (#2 Hero of Bladehold, 3 power) OUTRANKS the entrant (#3 Venerable Knight,
    // 2 power), and SoulbondPartner ranks on effective power -- so the unrestricted list would
    // pick the illegal one.
    EnsureCardsLoaded();
    GameState s = Fresh();
    Put(s, "Hero of Bladehold", 0, 2);            // out since an earlier turn, unpaired, 3 power
    Put(s, "Silverblade Paladin", 0, 1);          // Put, not Enter: no trigger A, so it is unpaired
    REQUIRE(ByNumber(s, 1).paired_with == 0);

    Enter(s, "Venerable Knight", 0, 3);           // 2 power -- the ONLY legal partner for trigger B

    CHECK(ByNumber(s, 1).paired_with == 3);       // the entrant
    CHECK(ByNumber(s, 1).paired_with != 2);       // NOT the higher-power creature already out
    CHECK(CreatureHasDoubleStrike(ByNumber(s, 1), s));
    CHECK(CreatureHasDoubleStrike(ByNumber(s, 3), s));
    CHECK_FALSE(CreatureHasDoubleStrike(ByNumber(s, 2), s));   // Hero gained nothing
}

TEST_CASE("soulbond trigger A stays UNRESTRICTED -- the fix must not narrow it too")
{
    // The paired control for the regression above. Trigger A's partner set really is "any unpaired
    // creature you control", so restricting BOTH triggers to the entrant would be just as wrong in
    // the other direction -- and would silently disable soulbond for every Paladin that enters
    // after its partners, which is the common case in this deck.
    EnsureCardsLoaded();
    GameState s = Fresh();
    Put(s, "Hero of Bladehold", 0, 2);
    Put(s, "Venerable Knight", 0, 3);
    Enter(s, "Silverblade Paladin", 0, 1);        // trigger A: picks freely among #2 and #3

    CHECK(ByNumber(s, 1).paired_with != 0);       // it DID pair with a pre-existing creature
    CHECK(ByNumber(s, 1).paired_with == 2);       // and took the highest effective power (Hero, 3)
    CHECK(CreatureHasDoubleStrike(ByNumber(s, 2), s));
}

TEST_CASE("soulbond trigger B: one entrant can only complete ONE pair")
{
    // Two unpaired Paladins, one creature enters. The entrant can be half of at most one pair, so
    // exactly one Paladin gets it -- the other stays unpaired rather than sharing the body. Worth
    // asserting alongside the only_idx change, because that change made both sources ask for the
    // SAME single candidate instead of each scanning a wide list.
    EnsureCardsLoaded();
    GameState s = Fresh();
    Put(s, "Silverblade Paladin", 0, 1);
    Put(s, "Silverblade Paladin", 0, 2);
    REQUIRE(ByNumber(s, 1).paired_with == 0);
    REQUIRE(ByNumber(s, 2).paired_with == 0);

    Enter(s, "Venerable Knight", 0, 3);

    const int paired = (ByNumber(s, 1).paired_with == 3 ? 1 : 0)
                     + (ByNumber(s, 2).paired_with == 3 ? 1 : 0);
    CHECK(paired == 1);
    CHECK(CreatureHasDoubleStrike(ByNumber(s, 3), s));   // the entrant is in a pair either way
}

TEST_CASE("soulbond: the pair breaks when the partner leaves, with no detach bookkeeping")
{
    EnsureCardsLoaded();
    GameState s = Fresh();
    Put(s, "Venerable Knight", 0, 2);
    Enter(s, "Silverblade Paladin", 0, 1);
    REQUIRE(ByNumber(s, 1).paired_with == 2);
    REQUIRE(CreatureHasDoubleStrike(ByNumber(s, 1), s));

    // The partner leaves the battlefield. NOTHING clears paired_with -- that is the design.
    for (std::size_t i = 0; i < s.battlefield.size(); ++i)
    {
        if (s.battlefield[i].card.m_number == 2)
        { s.battlefield.erase(s.battlefield.begin() + static_cast<long>(i)); break; }
    }

    CHECK(ByNumber(s, 1).paired_with == 2);                      // the stale number is still there
    CHECK(SoulbondPartnerIndex(ByNumber(s, 1), s) == -1);        // but it resolves to nothing
    CHECK_FALSE(CreatureHasDoubleStrike(ByNumber(s, 1), s));     // so the grant is gone

    // And per CR the Paladin is now unpaired, so the NEXT creature to enter pairs with it.
    Enter(s, "Worthy Knight", 0, 5);
    CHECK(ByNumber(s, 1).paired_with == 5);
    CHECK(CreatureHasDoubleStrike(ByNumber(s, 1), s));
}

TEST_CASE("soulbond is NOT until-EOT state: it survives the clean boundary")
{
    EnsureCardsLoaded();
    GameState s = Fresh();
    Put(s, "Venerable Knight", 0, 2);
    Enter(s, "Silverblade Paladin", 0, 1);
    REQUIRE(ByNumber(s, 1).paired_with == 2);

    // The pair persists across turns, so unlike temp_double_strike it must NOT make a board an
    // illegal dominance comparison point. Classifying the two together -- they look alike, both
    // being "this creature has double strike" state on the same card pair -- would have refused
    // every comparison from the turn a Paladin pairs onward.
    CHECK(dominance::AtCleanBoundary(s));
    CHECK(CreatureHasDoubleStrike(ByNumber(s, 2), s));
}

TEST_CASE("the shared oracle still answers for the PRE-EXISTING double-strike sources")
{
    EnsureCardsLoaded();

    // Equipment grant (Kor Duelist: "as long as this creature is equipped, it has double strike").
    // This is the path Stage B's refactor had to preserve, and KittyEquipment is a shipped deck.
    {
        GameState s = Fresh();
        const int kd = Put(s, "Kor Duelist", 0, 1);
        (void)kd;
        CHECK_FALSE(CreatureHasDoubleStrike(ByNumber(s, 1), s));   // unequipped: no
        const int eq = Put(s, "Bonesplitter", 0, 2);
        s.battlefield[eq].equipped_to = 1;
        CHECK(CreatureHasDoubleStrike(ByNumber(s, 1), s));         // equipped: yes
    }

    // Subtype LORD grant (Thrumming Hivepool: "Slivers you control have double strike"), including
    // through the bs.ds prefilter the combat sites pass -- the list must still find the granter.
    {
        GameState s = Fresh();
        Put(s, "Thrumming Hivepool", 0, 1);
        Put(s, "Leeching Sliver", 0, 2);
        const BoardSources bs = GatherBoardSources(s.battlefield, 0);
        CHECK(bs.ds.size() == 1);                                  // the Hivepool is in the list
        CHECK(CreatureHasDoubleStrike(ByNumber(s, 2), s, &bs.ds));
        CHECK(CreatureHasDoubleStrike(ByNumber(s, 2), s));         // and without the prefilter
    }
}
