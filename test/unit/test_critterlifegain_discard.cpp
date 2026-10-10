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
// exactly the generic ranking, and the stability case proves the order is prefix-stable with the
// distance model off. USER 2026-10-10: the payoff slots no longer subtract payoffs in play (T1, T2,
// T4, T8), and inside the payoff slots' and the fill's tiers the order is EV = the shared discard_dist
// CastFactor x value (T1, T4, T5, T9, T12, T13 pin it, each against MTG_DISCARD_DIST_MODEL=0).
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

// `dist` pins MTG_DISCARD_DIST_MODEL (1 = the shared distance model, 0 = value alone, -1 = the env
// default, which is ON). Test GameStates have an EMPTY library, so the model's factor is exact by hand:
// castable on our t-th next turn from the board plus the core ladder's kept lands (one per turn, a
// tapped one a turn late) -> 0.75^(t-1); never within 4 turns -> the floor 0.02 x 0.75^4 = 0.0063.
// Values are 0.85^position. Payoff slots: Serra at 30+ life 1, Pridemate / Voice 0.85, Archangel
// 0.7225, Heliod 0.6141, Serra 0.5220. Fill: Serra at 30+ 1, Pridemate / Voice 0.85, Ocelot 0.7225,
// Archangel 0.6141, Heliod 0.5220, Serra 0.4437, Ajani 0.3771, Ranger-Captain 0.3206.
std::vector<int> Crit(const GameState& s, int arm = 1, int dist = -1)
{
    heurarm::t_arm[heurarm::CRITTER_BUCKET_DISCARD] = static_cast<std::int8_t>(arm);
    heurarm::t_arm[heurarm::DISCARD_DIST_MODEL]     = static_cast<std::int8_t>(dist);
    std::vector<int> v = CritterLifegainProvider().CleanupDiscardCandidates(s, nullptr);
    heurarm::t_arm[heurarm::CRITTER_BUCKET_DISCARD] = -1;
    heurarm::t_arm[heurarm::DISCARD_DIST_MODEL]     = -1;
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
// one card -- with the distance model OFF, the only arm in which CleanupDiscardShedStable() is true.
void CheckPrefixStableCritter(GameState s)
{
    std::vector<int> order = Crit(s, 1, 0);
    while (s.players[0].hand.size() > 1)
    {
        const std::vector<std::string> rest = NamesCritter(s, order, 1);
        s.players[0].hand.erase(s.players[0].hand.begin() + order.front());
        order = Crit(s, 1, 0);
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
    // Board 3 Plains + Soul Warden + Pridemate: lands 1-3 and watcher 1 are full; the Pridemate in
    // play does NOT fill a payoff slot (USER 2026-10-10: creature quotas do not net the board). Board
    // mana 3, no hand land kept by the core, empty library: Archangel (5) is never castable (EV
    // 0.7225 x 0.0063); Voice, Heliod, Serra are castable now.
    // Core: payoff #1 = Voice (0.85), Ocelot, payoff #2 = Heliod (0.614 > Serra 0.522 > Archangel),
    // 2nd watcher = Soul's Attendant. Fill: Serra, then Archangel (4-5 drops last). Archangel is kept,
    // so the hand Plains is the 4th land. Shed: UA, then the ladder backwards: Plains, Archangel,
    // Serra, Attendant, Heliod, Ocelot, Voice.
    const GameState s = CritterT1();
    const std::vector<int> o = Crit(s, 1, 1);
    CHECK(o == std::vector<int>{ 1, 2, 0, 6, 5, 7, 4, 3 });
    CheckPermutationCritter(s, o);
    CHECK(Crit(s) == o);                 // the model is the default
    // Value alone: payoff #2 = Archangel (0.7225 > Heliod), fill Heliod, Serra.
    CHECK(Crit(s, 1, 0) == std::vector<int>{ 1, 2, 6, 7, 5, 0, 4, 3 });
    CHECK(GenCritter(s).front() == 0);   // generic max-MV sheds Archangel of Thune
}

TEST_CASE("Critter discard T2: duplicate legends -- Heliod and Ajani in play, a 2nd Daxos in hand -- shed right after UA")
{
    // The hand Heliod and Ajani are dead (their names are in play), the 2nd Daxos is dead (a 2nd copy
    // in hand). Core: watcher #1 = the first Daxos (no watcher in play); the Heliod in play fills no
    // payoff slot (USER 2026-10-10), so payoff #1 = Pridemate, Ocelot, payoff #2 = Archangel (the only
    // payoff left). Archangel is kept -> the hand Plains is the 4th land; nothing is left to fill.
    const GameState s = MakeCritter(
        { kHeliod, kAjani, kDaxos, kDaxos, kPridemate, kOcelot, "Plains", kArch },
        { "Plains", "Plains", "Plains", kHeliod, kAjani });
    const std::vector<int> o = Crit(s);
    CHECK(o == std::vector<int>{ 0, 1, 3, 6, 7, 5, 4, 2 });
    CHECK(Crit(s, 1, 0) == o);
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
    // Board 3 Plains + 2 watchers + Pridemate (which fills no payoff slot). Core: payoff #1 = Voice,
    // Ocelot, payoff #2 = Heliod (castable now, 0.614; Archangel never castable on 3 mana with an empty
    // library). Spare watchers (beyond 2): Auriok Champion, the hand Soul Warden -- shed Champion first.
    // WITH Archangel: fill = Serra, Archangel; Archangel is kept, so the Plains is the 4th land.
    const GameState with_big = CritterT4(kArch);
    const std::vector<int> a = Crit(with_big);
    CHECK(a == std::vector<int>{ 1, 7, 0, 4, 5, 6, 3, 2 });
    CheckPermutationCritter(with_big, a);
    CHECK(GenCritter(with_big).front() == 4);
    // Value alone: payoff #2 = Archangel; fill Heliod, Serra.
    CHECK(Crit(with_big, 1, 0) == std::vector<int>{ 1, 7, 0, 5, 6, 4, 3, 2 });
    // WITHOUT a 4+ drop (Ranger-Captain, MV 3, in that slot): the Plains is a surplus land and sheds
    // first, ahead of the spare watchers. Payoff #2 = Heliod; fill Serra, Ranger-Captain.
    const GameState without = CritterT4(kRanger);
    const std::vector<int> b = Crit(without);
    CHECK(b == std::vector<int>{ 0, 1, 7, 4, 5, 6, 3, 2 });
    CHECK(Crit(without, 1, 0) == b);
    CheckPermutationCritter(without, b);
    CHECK(GenCritter(without).front() == 4);   // generic sheds Ranger-Captain (MV 3, first)
}

TEST_CASE("Critter discard T5: creature overflow -- the repeat name first, a 2nd Ocelot is NOT a repeat; overflow before spare watchers")
{
    // Nine cards (two to shed). Board 3 Plains + 2 watchers + Pridemate (no payoff slot filled by it).
    // Core: payoff #1 = Voice #1, Ocelot #1, payoff #2 = Heliod (the 2nd Voice is a repeat; Heliod
    // castable 0.614 beats Serra 0.522 and the never-castable Archangel). No land in hand, so no 4th
    // land: fill room 7 - 3 = 4. Fill: first copies (the 2nd Ocelot counts as one -- agent default;
    // it costs 2 cumulatively, castable) Ocelot 0.7225, Serra 0.4437, Ranger-Captain 0.3206, then
    // Archangel (4-5 drops last); then the repeat Voice -> the overflow. Shed: Voice #2, the spare
    // Champion, then the protected tail.
    const GameState s = MakeCritter(
        { kVoice, kVoice, kOcelot, kOcelot, kArch, kHeliod, kSerraA, kRanger, kChampion },
        { "Plains", "Plains", "Plains", kWarden, kAttendant, kPridemate });
    const std::vector<int> o = Crit(s);
    CHECK(o == std::vector<int>{ 1, 8, 4, 7, 6, 3, 5, 2, 0 });
    // Value alone: payoff #2 = Archangel; fill Ocelot, Heliod, Serra, Ranger-Captain.
    CHECK(Crit(s, 1, 0) == std::vector<int>{ 1, 8, 7, 6, 5, 3, 4, 2, 0 });
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
    // Board: 3 Plains, TWO Ocelot Prides, 2 watchers, 2 payoffs. The Ocelot slot still takes the hand
    // Pride, and -- USER 2026-10-10, "Creature quotas are less worried about the board than land
    // quotas" -- the two payoffs in play fill NEITHER payoff slot: payoff #1 = Heliod, Ocelot, payoff
    // #2 = Serra. Ajani is kept in the fill -> the 4th land is kept; fill Ranger-Captain, then Ajani.
    // (Before the ruling the payoff slots were full from the board and Heliod / Serra were fill.)
    const GameState s = MakeCritter(
        { kOcelot, "Plains", "Plains", kHeliod, kSerraA, kRanger, kChampion, kAjani },
        { "Plains", "Plains", "Plains", kOcelot, kOcelot, kWarden, kAttendant, kPridemate, kVoice });
    const std::vector<int> o = Crit(s);
    CHECK(o == std::vector<int>{ 2, 6, 1, 7, 5, 4, 0, 3 });
    CHECK(Crit(s, 1, 0) == o);   // the ruling, not the model: value alone gives the same order
    CheckPermutationCritter(s, o);
    CHECK(GenCritter(s).front() == 7);   // generic sheds Ajani (MV 4)
}

TEST_CASE("Critter discard T9: the 4+ drop and the 4th land compete for the last slot -- KEEP's 'creatures to fill -> 4th land' keeps the creature")
{
    // Board 3 Plains + 2 watchers + Pridemate. Core: payoff #1 = Voice, Ocelot, payoff #2 = Heliod
    // (castable; Archangel is not, on 3 mana with an empty library). Fill: the 2nd Ocelot, Serra,
    // Ranger-Captain, then Archangel -- four creatures fill the seven-card hand exactly, so the Plains
    // (the would-be 4th land, ranked below the fill) is the one shed.
    const GameState s = MakeCritter(
        { "Plains", kVoice, kOcelot, kOcelot, kHeliod, kSerraA, kRanger, kArch },
        { "Plains", "Plains", "Plains", kWarden, kAttendant, kPridemate });
    const std::vector<int> o = Crit(s);
    CHECK(o == std::vector<int>{ 0, 7, 6, 5, 3, 4, 2, 1 });
    // Value alone: payoff #2 = Archangel (core) -> the 4th land is reserved and Ranger-Captain is the
    // creature overflow.
    CHECK(Crit(s, 1, 0) == std::vector<int>{ 6, 0, 5, 4, 3, 7, 2, 1 });
    CheckPermutationCritter(s, o);
    CHECK(GenCritter(s).front() == 7);   // generic sheds Archangel
}

TEST_CASE("Critter discard T10: a 4+ drop that is itself creature overflow reserves no 4th land")
{
    // As T9 plus a third Ocelot (nine cards, two to shed). Core: Voice, Ocelot, Heliod. The fill runs
    // Ocelot, Ocelot (2 and 3 mana cumulatively, both castable), Serra, Ranger-Captain and Archangel is
    // the overflow -- the kept hand holds no 4+ drop, so the Plains is a surplus land: shed the
    // Plains, then Archangel. Kept: 3 lands on board, no 4-drop.
    const GameState s = MakeCritter(
        { "Plains", kVoice, kOcelot, kOcelot, kHeliod, kSerraA, kRanger, kArch, kOcelot },
        { "Plains", "Plains", "Plains", kWarden, kAttendant, kPridemate });
    const std::vector<int> o = Crit(s);
    CHECK(o == std::vector<int>{ 0, 7, 6, 5, 8, 3, 4, 2, 1 });
    CheckPermutationCritter(s, o);
    CHECK(GenCritter(s).front() == 7);
    CheckPrefixStableCritter(s);
}

TEST_CASE("Critter discard T11: the 4th land counts the board -- with 4 lands in play a hand land is surplus even beside a 4+ drop")
{
    // Review finding (2026-10-09): the 4th-land reservation ignored lands in play, so with 4 Plains
    // out and an Archangel kept, a 5th land was protected over a creature. "MANA: 3 lands counting
    // board, 4 while holding a 4+ mana card": the 4 is already met by the board, so the hand Plains
    // is a surplus land and sheds FIRST (surplus lands shed before creature overflow / spare watchers).
    const GameState s = MakeCritter(
        { "Plains", "Archangel of Thune", "Ajani's Pridemate", "Ocelot Pride", "Auriok Champion",
          "Voice of the Blessed", "Heliod, Sun-Crowned", "Serra Ascendant" },
        { "Plains", "Plains", "Plains", "Plains", "Soul Warden", "Soul's Attendant" });
    const std::vector<int> o = Crit(s);
    REQUIRE(!o.empty());
    CHECK(o.front() == 0);
    CheckPermutationCritter(s, o);
}

TEST_CASE("Critter discard T12: kept lands feed the model -- an untapped kept Plains casts Heliod next turn, a kept Remote Farm a turn later")
{
    // Board 2 Plains + Soul Warden: the core keeps the hand land as land 3. Empty library.
    //   Plains kept: Heliod {2}{W} castable turn 1 (2 + 1) -> 0.614 > Serra 0.522 -> payoff #1 Heliod,
    //     Ocelot, payoff #2 Serra, 2nd watcher Soul's Attendant (over Champion), land 3; fill
    //     Ranger-Captain. Shed: UA, the spare Champion, then the ladder backwards.
    //   Remote Farm kept (enters tapped): Heliod only on turn 2 (2 + 2) -> 0.75 x 0.614 = 0.461 <
    //     Serra 0.522 -> payoff #1 Serra, payoff #2 Heliod.
    std::vector<std::string> hand = { "Plains", kHeliod, kSerraA, kOcelot, kRanger, kChampion, kUAbs, kAttendant };
    const std::vector<std::string> board = { "Plains", "Plains", kWarden };
    const GameState plains = MakeCritter(hand, board);
    CHECK(Crit(plains, 1, 1) == std::vector<int>{ 6, 5, 4, 0, 7, 2, 3, 1 });
    hand[0] = kRFarm;
    const GameState farm = MakeCritter(hand, board);
    const std::vector<int> o = Crit(farm, 1, 1);
    CHECK(o == std::vector<int>{ 6, 5, 4, 0, 7, 1, 3, 2 });
    CheckPermutationCritter(farm, o);
    CHECK(Crit(farm, 1, 0) == std::vector<int>{ 6, 5, 4, 0, 7, 2, 3, 1 });   // value alone: Heliod first
    CHECK(GenCritter(farm).front() == 1);   // generic sheds Heliod (MV 3, first)
}

TEST_CASE("Critter discard T13: the distance model never lifts a repeat over a first copy in the payoff slots")
{
    // Board 3 Plains + 2 watchers, empty library. Payoff #1 = Pridemate #1. Payoff #2: the 2nd
    // Pridemate is a repeat (EV 0.85 x floor -- it costs 4 cumulatively) and Archangel a first copy
    // (EV 0.7225 x floor, lower): variety is a USER tier, so Archangel takes the slot anyway. Archangel
    // is kept -> the hand Plains is the 4th land. Fill: Ranger-Captain (first copy) then Pridemate #2.
    const GameState s = MakeCritter(
        { kPridemate, kPridemate, kArch, kOcelot, "Plains", kUAbs, kChampion, kRanger },
        { "Plains", "Plains", "Plains", kWarden, kAttendant });
    const std::vector<int> o = Crit(s, 1, 1);
    CHECK(o == std::vector<int>{ 5, 6, 4, 1, 7, 2, 3, 0 });
    CheckPermutationCritter(s, o);
    CHECK(Crit(s, 1, 0) == o);
    CHECK(GenCritter(s).front() == 2);   // generic sheds Archangel
}

TEST_CASE("Critter discard: CleanupDiscardShedStable() is false only while the buckets AND the distance model are on")
{
    auto stable = [](int bucket, int dist)
    {
        heurarm::t_arm[heurarm::CRITTER_BUCKET_DISCARD] = static_cast<std::int8_t>(bucket);
        heurarm::t_arm[heurarm::DISCARD_DIST_MODEL]     = static_cast<std::int8_t>(dist);
        const bool v = CritterLifegainProvider().CleanupDiscardShedStable();
        heurarm::t_arm[heurarm::CRITTER_BUCKET_DISCARD] = -1;
        heurarm::t_arm[heurarm::DISCARD_DIST_MODEL]     = -1;
        return v;
    };
    CHECK_FALSE(stable(1, 1));
    CHECK(stable(1, 0));
    CHECK(stable(0, 1));
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
    CHECK(o == std::vector<int>{ 1, 2, 0, 6, 5, 7, 4, 3, 8 });   // T1's order, then the staged Ajani
    CheckPermutationCritter(s, o);
}

TEST_CASE("Critter discard: with the distance model off the shed order is prefix-stable shed by shed")
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
