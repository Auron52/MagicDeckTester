// Unit tests for the CritterLifegain cleanup-discard policy -- the USER's design (2026-10-09),
// CritterLifegainProvider::CleanupDiscardCandidates, docs/design/critterlifegain-discard-policy-
// proposal.md "CritterLifegain: SETTLED" (Angels-style):
//   MANA 3 lands counting the board, 4 while holding a 4+ drop (Ajani, Archangel), Plains > Remote
//   Farm; ENABLERS Soul Warden / Soul's Attendant > Auriok Champion > Daxos, 2 counting the board;
//   CREATURES fill the rest -- payoffs (Pridemate / Voice > Archangel > Heliod > Serra; Serra top at
//   30+ life), Ocelot Prides, Ranger-Captain, Ajani; variety first, 4-5 drops shed first, then value.
//   Ocelot NOT discounted by Prides in play; the 2nd payoff kept ahead of the 2nd watcher.
//   KEEP: land -> watcher -> payoff -> Ocelot -> land -> payoff -> 2nd watcher -> land -> creatures
//         to fill -> 4th land (only with a 4+ drop)
//   SHED: Unexpectedly Absent -> duplicate legend -> surplus lands (Remote Farm first) -> creature
//         overflow (repeats, then 4-5 drops, then value) -> spare watchers -> protected
// Every expected order below was derived BY HAND from that text plus the agent readings the
// implementation lists (each test names the one it pins), not read off the code's output. Every
// board checks the GENERIC fallback's index 0 (the control arm, which must differ) and that every
// non-staged index is named exactly once; the hatch case proves MTG_CRITTER_BUCKET_DISCARD=0 restores
// exactly the generic ranking, and the stability case proves the order is prefix-stable.
#include <doctest/doctest.h>

#include "ai/DecisionProviders.h"
#include "ai/HeuristicArm.h"
#include "cards/CardDatabase.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"

#include <algorithm>
#include <string>
#include <vector>

namespace
{

void EnsureCardsCritter()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

// A hand card is a NAME-ONLY placeholder in real play; board permanents carry real card data.
Card PhCritter(const std::string& name, int number)
{
    REQUIRE_MESSAGE(CardDatabase::Instance().Lookup(name) != nullptr, "card not in cards.json: ", name);
    Card c;
    c.m_name = name;
    c.RehashName();
    c.m_number = number;
    return c;
}

GameState MakeCritter(const std::vector<std::string>& hand, const std::vector<std::string>& board,
                      int life = 20)
{
    EnsureCardsCritter();
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 5;
    s.players[0].life     = life;
    int num = 1;
    for (const std::string& n : hand) { s.players[0].hand.push_back(PhCritter(n, num++)); }
    for (const std::string& n : board)
    {
        Permanent p;
        p.card              = CardDatabase::Instance().Lookup(n)->card;
        p.card.m_number     = num++;
        p.controller_index  = 0;
        p.owner_index       = 0;
        p.entered_this_turn = false;
        s.battlefield.push_back(p);
    }
    return s;
}

std::vector<int> Crit(const GameState& s, int arm = 1)
{
    heurarm::t_arm[heurarm::CRITTER_BUCKET_DISCARD] = static_cast<std::int8_t>(arm);
    std::vector<int> v = CritterLifegainProvider().CleanupDiscardCandidates(s, nullptr);
    heurarm::t_arm[heurarm::CRITTER_BUCKET_DISCARD] = -1;
    return v;
}
std::vector<int> GenCritter(const GameState& s) { return GenericProvider().CleanupDiscardCandidates(s, nullptr); }

void CheckPermutationCritter(const GameState& s, const std::vector<int>& order)
{
    std::vector<int> sorted = order;
    std::sort(sorted.begin(), sorted.end());
    std::vector<int> want;
    for (int i = 0; i < static_cast<int>(s.players[0].hand.size()); ++i) { want.push_back(i); }
    CHECK(sorted == want);
}

std::vector<std::string> NamesCritter(const GameState& s, const std::vector<int>& order, std::size_t from = 0)
{
    std::vector<std::string> out;
    for (std::size_t k = from; k < order.size(); ++k)
    { out.push_back(s.players[0].hand[static_cast<std::size_t>(order[k])].m_name.str()); }
    return out;
}

// Shed index 0, re-rank the smaller hand: the rest of the order must come back unchanged, down to
// one card (DecisionProvider::CleanupDiscardShedStable defaults to true).
void CheckPrefixStableCritter(GameState s)
{
    std::vector<int> order = Crit(s);
    while (s.players[0].hand.size() > 1)
    {
        const std::vector<std::string> rest = NamesCritter(s, order, 1);
        s.players[0].hand.erase(s.players[0].hand.begin() + order.front());
        order = Crit(s);
        CHECK(NamesCritter(s, order) == rest);
    }
}

const char* kWarden    = "Soul Warden";
const char* kAttendant = "Soul's Attendant";
const char* kChampion  = "Auriok Champion";
const char* kDaxos     = "Daxos, Blessed by the Sun";
const char* kPridemate = "Ajani's Pridemate";
const char* kVoice     = "Voice of the Blessed";
const char* kArch      = "Archangel of Thune";
const char* kHeliod    = "Heliod, Sun-Crowned";
const char* kSerraA    = "Serra Ascendant";
const char* kOcelot    = "Ocelot Pride";
const char* kRanger    = "Ranger-Captain of Eos";
const char* kAjani     = "Ajani, Strength of the Pride";
const char* kUAbs      = "Unexpectedly Absent";
const char* kRFarm     = "Remote Farm";

GameState CritterT1()
{
    return MakeCritter({ kArch, kUAbs, "Plains", kVoice, kOcelot, kAttendant, kSerraA, kHeliod },
                       { "Plains", "Plains", "Plains", kWarden, kPridemate });
}

GameState CritterT4(const char* slot4)
{
    return MakeCritter({ "Plains", kChampion, kVoice, kOcelot, slot4, kSerraA, kHeliod, kWarden },
                       { "Plains", "Plains", "Plains", kWarden, kAttendant, kPridemate });
}

}   // namespace

TEST_CASE("Critter discard T1: Unexpectedly Absent sheds first, not Archangel of Thune")
{
    // Board 3 Plains + Soul Warden + Pridemate: lands 1-3, watcher 1 and payoff 1 are full. Core:
    // Ocelot, payoff #2 = Voice, 2nd watcher = Soul's Attendant. Holding Archangel (MV 5) -> the hand
    // Plains is the 4th land and the fill room is 7 - 3 - 1 = 3: Heliod, Serra, then Archangel (4-5
    // drops last). Shed: UA, then the ladder backwards: Plains, Archangel, Serra, Heliod, Attendant,
    // Voice, Ocelot.
    const GameState s = CritterT1();
    const std::vector<int> o = Crit(s);
    CHECK(o == std::vector<int>{ 1, 2, 0, 6, 7, 5, 3, 4 });
    CheckPermutationCritter(s, o);
    CHECK(GenCritter(s).front() == 0);   // generic max-MV sheds Archangel of Thune
}

TEST_CASE("Critter discard T2: duplicate legends -- Heliod and Ajani in play, a 2nd Daxos in hand -- shed right after UA")
{
    // The hand Heliod and Ajani are dead (their names are in play), the 2nd Daxos is dead (a 2nd copy
    // in hand). Core: watcher #1 = the first Daxos (no watcher in play), payoff #1 = Heliod in play,
    // Ocelot, payoff #2 = Pridemate (over Archangel). Archangel is held -> 4th land kept; it is the
    // only fill creature.
    const GameState s = MakeCritter(
        { kHeliod, kAjani, kDaxos, kDaxos, kPridemate, kOcelot, "Plains", kArch },
        { "Plains", "Plains", "Plains", kHeliod, kAjani });
    const std::vector<int> o = Crit(s);
    CHECK(o == std::vector<int>{ 0, 1, 3, 6, 7, 4, 5, 2 });
    CheckPermutationCritter(s, o);
    CHECK(GenCritter(s).front() == 7);   // generic sheds Archangel
}

TEST_CASE("Critter discard T3: the land quota counts the board; surplus lands shed Remote Farm first")
{
    // Board 2 Plains + Soul Warden: land 3 is the first hand Plains; no 4+ drop, so the other three
    // hand lands are surplus -- both Remote Farms (higher index first), then the Plains.
    const GameState s = MakeCritter(
        { "Plains", kRFarm, "Plains", kRFarm, kPridemate, kOcelot, kAttendant, kVoice },
        { "Plains", "Plains", kWarden });
    const std::vector<int> o = Crit(s);
    CHECK(o == std::vector<int>{ 3, 1, 2, 0, 6, 7, 5, 4 });
    CheckPermutationCritter(s, o);
    CHECK(GenCritter(s).front() == 4);   // generic sheds Ajani's Pridemate (MV 2)
}

TEST_CASE("Critter discard T4: the 4th land is kept only with a 4+ drop -- then it outlives a spare watcher; without one it is surplus")
{
    // Board 3 Plains + 2 watchers + Pridemate. Core: Ocelot, Voice. Spare watchers (beyond 2):
    // Auriok Champion, the hand Soul Warden -- shed Champion first (the watcher order reversed).
    // WITH Archangel: the Plains is the 4th land (protected), fill = Heliod, Serra, Archangel.
    const GameState with_big = CritterT4(kArch);
    const std::vector<int> a = Crit(with_big);
    CHECK(a == std::vector<int>{ 1, 7, 0, 4, 5, 6, 2, 3 });
    CheckPermutationCritter(with_big, a);
    CHECK(GenCritter(with_big).front() == 4);
    // WITHOUT a 4+ drop (Ranger-Captain, MV 3, in that slot): the Plains is a surplus land and sheds
    // first, ahead of the spare watchers.
    const GameState without = CritterT4(kRanger);
    const std::vector<int> b = Crit(without);
    CHECK(b == std::vector<int>{ 0, 1, 7, 4, 5, 6, 2, 3 });
    CheckPermutationCritter(without, b);
    CHECK(GenCritter(without).front() == 4);   // generic sheds Ranger-Captain (MV 3, first)
}

TEST_CASE("Critter discard T5: creature overflow -- the repeat name first, a 2nd Ocelot is NOT a repeat; overflow before spare watchers")
{
    // Nine cards (two to shed). Board 3 Plains + 2 watchers + Pridemate. Core: Ocelot #1, Voice #1.
    // No land in hand, so no 4th land: fill room 7 - 2 = 5. Fill order: first copies (the 2nd Ocelot
    // counts as one -- agent default) Ocelot, Heliod, Serra, Ranger-Captain, then Archangel (4-5 drops
    // last); then the repeat Voice -> the overflow. Shed: Voice #2, the spare Champion, then the
    // protected tail.
    const GameState s = MakeCritter(
        { kVoice, kVoice, kOcelot, kOcelot, kArch, kHeliod, kSerraA, kRanger, kChampion },
        { "Plains", "Plains", "Plains", kWarden, kAttendant, kPridemate });
    const std::vector<int> o = Crit(s);
    CHECK(o == std::vector<int>{ 1, 8, 4, 7, 6, 5, 3, 0, 2 });
    CheckPermutationCritter(s, o);
    CHECK(GenCritter(s).front() == 4);   // generic sheds Archangel
}

TEST_CASE("Critter discard T6: Serra Ascendant is the top payoff at 30+ life")
{
    const std::vector<std::string> hand =
        { kPridemate, kSerraA, kAttendant, kOcelot, kHeliod, "Plains", kRanger, kUAbs };
    const std::vector<std::string> board = { "Plains", "Plains", "Plains", kWarden };
    // At 30 life: payoff #1 = Serra, Ocelot, payoff #2 = Pridemate, 2nd watcher = Attendant; fill
    // Heliod, Ranger-Captain. Shed: UA, the surplus Plains, then the ladder backwards.
    const GameState hi = MakeCritter(hand, board, 30);
    const std::vector<int> a = Crit(hi);
    CHECK(a == std::vector<int>{ 7, 5, 6, 4, 2, 0, 3, 1 });
    CheckPermutationCritter(hi, a);
    CHECK(GenCritter(hi).front() == 4);   // generic sheds Heliod (MV 3, first)
    // At 20 life Serra is the LAST payoff: payoff #1 = Pridemate, #2 = Heliod; Serra falls to the fill.
    const GameState lo = MakeCritter(hand, board, 20);
    CHECK(Crit(lo) == std::vector<int>{ 7, 5, 6, 1, 2, 4, 3, 0 });
}

TEST_CASE("Critter discard T7: watchers Warden / Attendant > Champion > Daxos, and the 2nd payoff is kept ahead of the 2nd watcher")
{
    // Board 3 Plains only. Core: watcher #1 Soul Warden, payoff #1 Pridemate, payoff #2 Voice, 2nd
    // watcher Soul's Attendant. Surplus: both hand Plains; spare watchers Daxos then Champion. The
    // protected tail sheds the 2nd watcher BEFORE the 2nd payoff.
    const GameState s = MakeCritter(
        { kDaxos, kChampion, kWarden, kPridemate, kVoice, kAttendant, "Plains", "Plains" },
        { "Plains", "Plains", "Plains" });
    const std::vector<int> o = Crit(s);
    CHECK(o == std::vector<int>{ 7, 6, 0, 1, 5, 4, 3, 2 });
    CheckPermutationCritter(s, o);
    CHECK(GenCritter(s).front() == 0);   // generic sheds Daxos (MV 2, first)
    CHECK(o.front() != GenCritter(s).front());
}

TEST_CASE("Critter discard T8: Ocelot Pride is NOT discounted by Prides in play")
{
    // Board: 3 Plains, TWO Ocelot Prides, 2 watchers, 2 payoffs -- every netted slot is full, yet the
    // Ocelot slot still takes the hand Pride (it is the last card to go). Ajani is held -> the 4th
    // land is kept; fill Heliod, Serra, Ranger-Captain, then Ajani (4-5 drops last).
    const GameState s = MakeCritter(
        { kOcelot, "Plains", "Plains", kHeliod, kSerraA, kRanger, kChampion, kAjani },
        { "Plains", "Plains", "Plains", kOcelot, kOcelot, kWarden, kAttendant, kPridemate, kVoice });
    const std::vector<int> o = Crit(s);
    CHECK(o == std::vector<int>{ 2, 6, 1, 7, 5, 4, 3, 0 });
    CheckPermutationCritter(s, o);
    CHECK(GenCritter(s).front() == 7);   // generic sheds Ajani (MV 4)
}

TEST_CASE("Critter discard T9: the 4+ drop and the 4th land compete for the last slot -- KEEP's 'creatures to fill -> 4th land' keeps the creature")
{
    // Board 3 Plains + 2 watchers + Pridemate. Core: Ocelot, Voice (2 hand cards). Fill: the 2nd
    // Ocelot, Heliod, Serra, Ranger-Captain, then Archangel -- five creatures fill the seven-card hand
    // exactly, so the Plains (the would-be 4th land, ranked below the fill) is the one shed.
    const GameState s = MakeCritter(
        { "Plains", kVoice, kOcelot, kOcelot, kHeliod, kSerraA, kRanger, kArch },
        { "Plains", "Plains", "Plains", kWarden, kAttendant, kPridemate });
    const std::vector<int> o = Crit(s);
    CHECK(o == std::vector<int>{ 0, 7, 6, 5, 4, 3, 1, 2 });
    CheckPermutationCritter(s, o);
    CHECK(GenCritter(s).front() == 7);   // generic sheds Archangel
}

TEST_CASE("Critter discard T10: a 4+ drop that is itself creature overflow reserves no 4th land")
{
    // As T9 plus a third Ocelot (nine cards, two to shed). The fill now runs Ocelot, Ocelot, Heliod,
    // Serra, Ranger-Captain and Archangel is the overflow -- the kept hand holds no 4+ drop, so the
    // Plains is a surplus land: shed the Plains, then Archangel. Kept: 3 lands on board, no 4-drop.
    const GameState s = MakeCritter(
        { "Plains", kVoice, kOcelot, kOcelot, kHeliod, kSerraA, kRanger, kArch, kOcelot },
        { "Plains", "Plains", "Plains", kWarden, kAttendant, kPridemate });
    const std::vector<int> o = Crit(s);
    CHECK(o == std::vector<int>{ 0, 7, 6, 5, 4, 8, 3, 1, 2 });
    CheckPermutationCritter(s, o);
    CHECK(GenCritter(s).front() == 7);
    CheckPrefixStableCritter(s);
}

TEST_CASE("Critter discard: MTG_CRITTER_BUCKET_DISCARD=0 restores exactly the generic ranking; the default is the policy")
{
    const GameState s = CritterT1();
    CHECK(Crit(s, 0) == GenCritter(s));
    CHECK(Crit(s, 0) != Crit(s, 1));
    CHECK(CritterLifegainProvider().CleanupDiscardCandidates(s, nullptr) == Crit(s, 1));   // the default
}

TEST_CASE("Critter discard: a staged card is never in the policy's order and lands last")
{
    GameState s = MakeCritter({ kArch, kUAbs, "Plains", kVoice, kOcelot, kAttendant, kSerraA, kHeliod, kAjani },
                              { "Plains", "Plains", "Plains", kWarden, kPridemate });
    s.players[0].hand[8].m_is_staged = true;
    const std::vector<int> o = Crit(s);
    CHECK(o == std::vector<int>{ 1, 2, 0, 6, 7, 5, 3, 4, 8 });   // T1's order, then the staged Ajani
    CheckPermutationCritter(s, o);
}

TEST_CASE("Critter discard: the shed order is prefix-stable shed by shed")
{
    CheckPrefixStableCritter(CritterT1());
    CheckPrefixStableCritter(CritterT4(kArch));
    CheckPrefixStableCritter(CritterT4(kRanger));
    CheckPrefixStableCritter(MakeCritter(
        { kVoice, kVoice, kOcelot, kOcelot, kArch, kHeliod, kSerraA, kRanger, kChampion },
        { "Plains", "Plains", "Plains", kWarden, kAttendant, kPridemate }));
    CheckPrefixStableCritter(MakeCritter(
        { kOcelot, "Plains", "Plains", kHeliod, kSerraA, kRanger, kChampion, kAjani },
        { "Plains", "Plains", "Plains", kOcelot, kOcelot, kWarden, kAttendant, kPridemate, kVoice }));
    // An empty board and a crowded hand: every slot of the core ladder is filled from the hand.
    CheckPrefixStableCritter(MakeCritter(
        { "Plains", kRFarm, "Plains", "Plains", kWarden, kDaxos, kPridemate, kVoice, kOcelot, kArch, kOcelot,
          kHeliod, kUAbs },
        {}, 30));
}
