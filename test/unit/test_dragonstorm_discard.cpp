// Unit tests for the USER-settled Dragonstorm cleanup-discard buckets (2026-10-09):
// DragonstormProvider::CleanupDiscardCandidates + CleanupDiscardProtectionWaived, implementing
// docs/design/dragonstorm-discard-policy-proposal.md "#### Dragonstorm: SETTLED".
//   KEEP: land -> Lotus Bloom (end of turn 1 only) -> Dragonstorm (else Apex) -> fast mana (rituals,
//         Medallion, lands to 4 / 3 with a Medallion in play, interleaved) -> no Dragonstorm: every Apex
//         -> one cheap Dragon (Scourge > Lathliss) if mana is very limited or no Dragonstorm/Apex held
//         -> Apex alongside a Dragonstorm -> soft 2nd Medallion.
//   SHED: Karrthus / Kolaghan -> other Dragons (Utvara > Lathliss > Scourge) -> 2nd Dragonstorm ->
//         Lotus Bloom after turn 1 -> Apex alongside a Dragonstorm -> 3rd+ Medallion -> surplus lands
//         -> rituals -> protected.
// Every expected order below was derived BY HAND from that text (the derivation is in each case's
// comment), not read off the implementation. Every board also asserts the GENERIC fallback's answer --
// the control arm, which must differ somewhere -- and the =0 hatch restores the generic ranking AND
// the profile's last-copy-of-EACH protection exactly.
#include <doctest/doctest.h>

#include "ai/DecisionProviders.h"
#include "ai/HeuristicArm.h"
#include "cards/CardDatabase.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"
#include "deck/DeckLoader.h"

#include <algorithm>
#include <string>
#include <vector>

namespace
{

void EnsureCardsDsd()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

// A hand card is a NAME-ONLY placeholder in real play (DeckLoader::MakePlaceholder).
Card PhDs(const std::string& name, int number)
{
    REQUIRE_MESSAGE(CardDatabase::Instance().Lookup(name) != nullptr, "card not in cards.json: ", name);
    Card c;
    c.m_name = name;
    c.RehashName();
    c.m_number = number;
    return c;
}

const DragonstormProvider& DsProv()
{
    static const DragonstormProvider p;
    return p;
}

// The deck's profile: required_pieces [Apex of Power, Dragonstorm], discard_protect "hand".
const std::vector<std::string> kDsReq = { "Apex of Power", "Dragonstorm" };

const char* kDS    = "Dragonstorm";
const char* kApex  = "Apex of Power";
const char* kRite  = "Rite of Flame";
const char* kPyr   = "Pyretic Ritual";
const char* kDesp  = "Desperate Ritual";
const char* kSeeth = "Seething Song";
const char* kIren  = "Irencrag Feat";
const char* kMed   = "Ruby Medallion";
const char* kBloom = "Lotus Bloom";
const char* kMtn   = "Mountain";
const char* kNeedle = "Sandstone Needle";
const char* kKarr  = "Karrthus, Tyrant of Jund";
const char* kKola  = "Dragonlord Kolaghan";
const char* kUtv   = "Utvara Hellkite";
const char* kScour = "Scourge of Valkas";
const char* kLath  = "Lathliss, Dragon Queen";

GameState MakeDs(const std::vector<std::string>& hand, const std::vector<std::string>& board, int turn = 3)
{
    EnsureCardsDsd();
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = turn;
    s.m_provider          = &DsProv();                         // the waiver resolves through it
    s.m_discard_protect   = DiscardProtectScope::LastInHand;   // the profile's "hand"
    int num = 1;
    for (const std::string& n : hand) { s.players[0].hand.push_back(PhDs(n, num++)); }
    for (const char* n : { kDS, kApex, kRite, kMtn, kScour, kSeeth })
    { s.players[0].library.push_back(PhDs(n, num++)); }
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
    s.players[0].lands_played_this_turn = 1;
    return s;
}

std::vector<int> DsArm(const GameState& s, int arm)
{
    heurarm::t_arm[heurarm::DRAGONSTORM_BUCKET_DISCARD] = static_cast<std::int8_t>(arm);
    std::vector<int> v = DsProv().CleanupDiscardCandidates(s, &kDsReq);
    heurarm::t_arm[heurarm::DRAGONSTORM_BUCKET_DISCARD] = -1;
    return v;
}
std::vector<int> Ds(const GameState& s) { return DsArm(s, 1); }
// The control: the generic max-MV ranking with the =0 arm's protection (no waiver).
std::vector<int> GenDs(const GameState& s)
{
    heurarm::t_arm[heurarm::DRAGONSTORM_BUCKET_DISCARD] = 0;
    std::vector<int> v = GenericProvider().CleanupDiscardCandidates(s, &kDsReq);
    heurarm::t_arm[heurarm::DRAGONSTORM_BUCKET_DISCARD] = -1;
    return v;
}

void CheckPermDs(const GameState& s, const std::vector<int>& order)
{
    std::vector<int> sorted = order;
    std::sort(sorted.begin(), sorted.end());
    std::vector<int> want;
    for (int i = 0; i < static_cast<int>(s.players[0].hand.size()); ++i) { want.push_back(i); }
    CHECK(sorted == want);
}

bool Prot(const GameState& s, int i, int arm)
{
    heurarm::t_arm[heurarm::DRAGONSTORM_BUCKET_DISCARD] = static_cast<std::int8_t>(arm);
    const bool p = CleanupDiscardProtected(s, s.players[0].hand[static_cast<std::size_t>(i)], &kDsReq);
    heurarm::t_arm[heurarm::DRAGONSTORM_BUCKET_DISCARD] = -1;
    return p;
}

}   // namespace

TEST_CASE("Dragonstorm discard: the deck routes to DragonstormProvider")
{
    EnsureCardsDsd();
    const Decklist d = DeckLoader::LoadFromFile(ResolveHeuristicDefaultsPath("decks/Dragonstorm/Dragonstorm.cod"));
    CHECK(std::string(DetectDecisionProvider(d).Name()) == "Dragonstorm");
}

// USER: "We only must keep one or the other, prioritizing dragonstorm ... There is no card more
// crucial." Engine protection outcomes, policy ON vs the =0 arm (the profile's last-copy-of-EACH).
TEST_CASE("Dragonstorm discard: exactly one of {Dragonstorm, Apex} is protected, Dragonstorm first")
{
    {   // {DS}: Dragonstorm protected (sole copy).
        const GameState s = MakeDs({ kDS, kRite, kMtn }, {});
        CHECK(Prot(s, 0, 1));
        CHECK(Prot(s, 0, 0));
    }
    {   // {DS, Apex}: Dragonstorm only -- Apex is waived while a Dragonstorm is held. =0: both.
        const GameState s = MakeDs({ kDS, kApex, kRite }, {});
        CHECK(Prot(s, 0, 1));
        CHECK_FALSE(Prot(s, 1, 1));
        CHECK(Prot(s, 0, 0));
        CHECK(Prot(s, 1, 0));
        heurarm::t_arm[heurarm::DRAGONSTORM_BUCKET_DISCARD] = 1;
        CHECK(DsProv().CleanupDiscardProtectionWaived(s, s.players[0].hand[1]));
        CHECK_FALSE(DsProv().CleanupDiscardProtectionWaived(s, s.players[0].hand[0]));
        heurarm::t_arm[heurarm::DRAGONSTORM_BUCKET_DISCARD] = 0;
        CHECK_FALSE(DsProv().CleanupDiscardProtectionWaived(s, s.players[0].hand[1]));
        heurarm::t_arm[heurarm::DRAGONSTORM_BUCKET_DISCARD] = -1;
    }
    {   // {DS, DS}: neither by the engine (a duplicate is spare under "hand"); the ORDER keeps one.
        const GameState s = MakeDs({ kDS, kDS, kRite }, {});
        CHECK_FALSE(Prot(s, 0, 1));
        CHECK_FALSE(Prot(s, 1, 1));
    }
    {   // {Apex}: Apex protected (no Dragonstorm held, nothing waived).
        const GameState s = MakeDs({ kApex, kRite, kMtn }, {});
        CHECK(Prot(s, 0, 1));
        CHECK(Prot(s, 0, 0));
    }
    {   // {Apex, Apex}: neither by the engine; the ORDER keeps both, one last.
        const GameState s = MakeDs({ kApex, kApex, kRite }, {});
        CHECK_FALSE(Prot(s, 0, 1));
        CHECK_FALSE(Prot(s, 1, 1));
    }
    {   // A STAGED Dragonstorm (Apex exile, expiring) is not held: Apex keeps its protection.
        GameState s = MakeDs({ kApex, kDS, kRite }, {});
        s.players[0].hand[1].m_is_staged = true;
        CHECK(Prot(s, 0, 1));
    }
}

// The proposal's worked hand (lip=1, no land in hand). Derivation: no land to keep (K1 empty); the
// protected payoff is Dragonstorm #0. Rituals by EV at reach 1 / 1 red source: Rite (castable, 1x81)
// > Pyretic (1 short, 1x27) > Seething (2 short, 2x9); interleave F,L,F,M,F -> Rite, Pyretic,
// Medallion, Seething. Karrthus is a haste Dragon (never the cheap exception). SHED: Karrthus -> 2nd
// Dragonstorm -> Apex alongside a Dragonstorm -> the fast mana backwards -> the protected Dragonstorm.
TEST_CASE("Dragonstorm discard D1: Karrthus first, then the 2nd Dragonstorm, then the Apex beside it")
{
    const GameState s = MakeDs({ kDS, kDS, kApex, kKarr, kSeeth, kRite, kPyr, kMed }, { kMtn });
    const std::vector<int> o = Ds(s);
    CHECK(o == std::vector<int>{ 3, 1, 2, 4, 7, 6, 5, 0 });
    // The control: generic max-MV (Apex protected as a sole copy, both Dragonstorms spare) sheds a
    // Dragonstorm first -- and would shed BOTH in a two-card cleanup.
    CHECK(GenDs(s) == std::vector<int>{ 0, 1, 3, 4, 6, 7, 5, 2 });
}

// "Apex only adds storm count when casting off lands; it ranks below fast mana." Board 2 Mountains,
// no land in hand, reach 2, Dragonstorm (9) 7 short. EV: Desperate (net 1 + splice 1, castable) 162 >
// Pyretic 81 (gross 3) > Rite 81 (gross 2) x2 > Seething (1 short) 54 > Irencrag (net 3 + shortfall 7,
// 2 mana + 1 red pip short) 30. Not very limited (2 + 2 + 9 >= 9), Dragonstorm held -> no Dragon.
TEST_CASE("Dragonstorm discard D2: with a Dragonstorm, Apex sheds before every ritual")
{
    const GameState s = MakeDs({ kDS, kApex, kRite, kRite, kPyr, kDesp, kSeeth, kIren }, { kMtn, kMtn });
    CHECK(Ds(s) == std::vector<int>{ 1, 7, 6, 3, 2, 4, 5, 0 });
    CHECK(GenDs(s).front() == 7);   // generic: both payoffs protected -> the max-MV ritual (Irencrag)
}

TEST_CASE("Dragonstorm discard D2b: MTG_DRAGONSTORM_BUCKET_DISCARD=0 restores the generic ranking exactly")
{
    const GameState s = MakeDs({ kDS, kApex, kRite, kRite, kPyr, kDesp, kSeeth, kIren }, { kMtn, kMtn });
    const std::vector<int> off = DsArm(s, 0);
    const std::vector<int> on  = DsArm(s, 1);
    CHECK(off == GenDs(s));
    CHECK(off == std::vector<int>{ 7, 6, 4, 5, 2, 3, 1, 0 });   // both payoffs protected (tier C last)
    CHECK(on.front() == 1);                                     // the policy: the Apex beside the DS
    CHECK(on != off);
}

// "Rite of Flame first at 1 source." Board 1 Mountain, reach 1; no payoff held (9 short). Rite
// castable 81 > Desperate (1 short, value 2) 54 > Pyretic 27 > Seething (2 short) 18 > Irencrag (3 mana
// + 2 red short -> decay floor) 11. No Dragonstorm/Apex held -> the cheap-Dragon exception is armed,
// but no Dragon here is cheap and non-haste. SHED: Karrthus, Kolaghan (haste, MV desc) -> Utvara.
TEST_CASE("Dragonstorm discard D3: haste Dragons first; Rite of Flame is the last ritual at 1 source")
{
    const GameState s = MakeDs({ kRite, kPyr, kDesp, kSeeth, kIren, kKarr, kKola, kUtv }, { kMtn });
    CHECK(Ds(s) == std::vector<int>{ 5, 6, 7, 4, 3, 1, 2, 0 });
    CHECK(GenDs(s).front() == 7);   // generic: Utvara (MV 8)
}

// "Irencrag best when far short ... Rite first at 1 source" turned round: 4 Mountains (reach 4), 5
// short of Dragonstorm. Irencrag (net 3 + 5, castable) 648 > Seething (gross 5) 162 > Desperate (gross
// 3) 162 > Pyretic (gross 3) 81 > Rite (gross 2) 81: Rite sinks to the bottom. Not very limited, so the
// Dragons are useless (Dragonstorm castable soon): Utvara > Scourge.
TEST_CASE("Dragonstorm discard D4: at 4 sources and far short, Irencrag is the best ritual and Rite the worst")
{
    const GameState s = MakeDs({ kDS, kRite, kPyr, kSeeth, kIren, kDesp, kUtv, kScour }, { kMtn, kMtn, kMtn, kMtn });
    CHECK(Ds(s) == std::vector<int>{ 6, 7, 1, 2, 5, 3, 4, 0 });
    CHECK(GenDs(s) == std::vector<int>{ 6, 7, 4, 3, 2, 5, 1, 0 });   // generic sheds Irencrag 3rd
}

// "Irencrag ... worst when mana is already enough": 5 Mountains + 2 Sandstone Needles = 9 mana, the
// Dragonstorm is not short at all -> Irencrag's value is 0. 7 lands in play, so both hand Mountains
// are surplus and shed first.
TEST_CASE("Dragonstorm discard D5: not short at all -> Irencrag is the first ritual shed; surplus lands lead")
{
    const GameState s = MakeDs({ kDS, kIren, kRite, kSeeth, kPyr, kDesp, kMtn, kMtn },
                               { kMtn, kMtn, kMtn, kMtn, kMtn, kNeedle, kNeedle });
    CHECK(Ds(s) == std::vector<int>{ 7, 6, 1, 2, 4, 5, 3, 0 });
    CHECK(GenDs(s).front() == 1);   // generic: Irencrag (MV 4) -- the policy keeps it over the lands
}

// "WITHOUT Dragonstorm in hand: keep EVERY Apex." No Dragonstorm: the first Apex is protected (named
// last), the second sits in the ladder above the cheap Dragon. Board 1 Mountain + the Mountain kept
// (K1): reach 2. Very limited (1 + 2 + 4 < 9) -> one cheap Dragon: Scourge (5) over Lathliss (6).
// SHED: Lathliss -> Scourge -> Apex #2 -> Seething, Rite, Pyretic -> the Mountain -> Apex #1.
TEST_CASE("Dragonstorm discard D6: no Dragonstorm -> both Apexes kept; Scourge is the cheap Dragon")
{
    const GameState s = MakeDs({ kApex, kApex, kScour, kLath, kRite, kPyr, kSeeth, kMtn }, { kMtn });
    CHECK(Ds(s) == std::vector<int>{ 3, 2, 1, 6, 4, 5, 7, 0 });
    CHECK(GenDs(s).front() == 0);   // generic: an Apex (MV 10, a spare copy under "hand")
}

// Exception (1): no Dragonstorm and no Apex held -> keep ONE cheap Dragon even with plenty of mana
// (4 + 2 + 5 >= 9). Lathliss (6) is the cheapest non-haste Dragon here (Utvara 8 is never cheap).
// Fast mana: Seething, Desperate, the Medallion (no Medallion in play), Pyretic, Rite.
TEST_CASE("Dragonstorm discard D7: no payoff held -> one cheap Dragon kept below the fast mana")
{
    const GameState s = MakeDs({ kLath, kUtv, kKarr, kRite, kPyr, kSeeth, kDesp, kMed }, { kMtn, kMtn, kMtn, kMtn });
    CHECK(Ds(s) == std::vector<int>{ 2, 1, 0, 3, 4, 7, 6, 5 });
    CHECK(GenDs(s).front() == 1);   // generic: Utvara
}

// LOTUS BLOOM: keep one only at the end of turn 1. Mana very limited (1 + 2 + 5 + 0 < 9) -> Lathliss is
// the cheap Dragon, ABOVE the Apex beside the Dragonstorm ("below a cheap Dragon when mana is limited").
TEST_CASE("Dragonstorm discard D8: the Bloom is kept at the end of turn 1 and dropped from turn 2")
{
    const std::vector<std::string> hand = { kBloom, kRite, kPyr, kSeeth, kDesp, kLath, kDS, kApex };
    const GameState t1 = MakeDs(hand, { kMtn }, 1);
    CHECK(Ds(t1) == std::vector<int>{ 7, 5, 3, 2, 4, 1, 0, 6 });
    const GameState t2 = MakeDs(hand, { kMtn }, 2);
    CHECK(Ds(t2) == std::vector<int>{ 0, 7, 5, 3, 2, 4, 1, 6 });
    CHECK(GenDs(t1).front() == 5);   // generic: Lathliss (both payoffs protected)
    // A Bloom suspended on turn 1 arrives on turn 4 -- "ready" within 2 turns of turn 2 -> 8 + 3 >= 9,
    // mana is no longer very limited and the Dragon is useless: Lathliss goes first.
    GameState susp = t2;
    SuspendedCard sc;
    sc.card        = PhDs(kBloom, 900);
    sc.arrive_turn = 4;
    susp.players[0].suspended_cards.push_back(sc);
    CHECK(Ds(susp) == std::vector<int>{ 5, 0, 7, 3, 2, 4, 1, 6 });
}

// The tail of the SHED line: "Apex alongside a Dragonstorm -> 3rd+ Medallion -> surplus lands ->
// rituals -> protected", with the soft 2nd Medallion after the surplus lands (agent reading (a)).
// Board 3 Mountains: one land slot left -> Mountain #5 kept, #6 surplus. Medallions: #2 is the
// Medallion slot, #3 the soft 2nd, #4 the 3rd.
TEST_CASE("Dragonstorm discard D9: Apex beside a Dragonstorm -> 3rd Medallion -> surplus land -> soft 2nd Medallion")
{
    const GameState s = MakeDs({ kDS, kApex, kMed, kMed, kMed, kMtn, kMtn, kRite }, { kMtn, kMtn, kMtn });
    CHECK(Ds(s) == std::vector<int>{ 1, 4, 6, 3, 2, 7, 5, 0 });
    CHECK(GenDs(s).front() == 2);   // generic: a Medallion (both payoffs protected)
}

// "lands to 4 (3 with a Medallion in play)": a Medallion on the battlefield fills the Medallion slot
// AND lowers the land target to 3, so both hand Mountains are surplus and the hand Medallion is the
// soft 2nd. The Medallion discount: Pyretic and Seething cost 1 less (net 2 / 3).
TEST_CASE("Dragonstorm discard D10: a Medallion in play -> land target 3; the hand Medallion is the soft 2nd")
{
    const GameState s = MakeDs({ kDS, kMtn, kMtn, kRite, kPyr, kSeeth, kMed, kKarr }, { kMtn, kMtn, kMtn, kMed });
    CHECK(Ds(s) == std::vector<int>{ 7, 2, 1, 6, 3, 4, 5, 0 });
    CHECK(GenDs(s) == std::vector<int>{ 7, 5, 4, 6, 3, 1, 2, 0 });
}

// {DS, DS}: neither copy is engine-protected, so the generic rule ranks BOTH first; the policy sheds
// the 2nd Dragonstorm early and names the kept one last. Turn 3: the Bloom is dropped.
TEST_CASE("Dragonstorm discard D11: two Dragonstorms -> one is shed early, the other is the last card")
{
    const GameState s = MakeDs({ kDS, kDS, kRite, kPyr, kSeeth, kDesp, kMtn, kBloom }, { kMtn });
    const std::vector<int> o = Ds(s);
    CHECK(o == std::vector<int>{ 1, 7, 4, 2, 3, 5, 6, 0 });
    const std::vector<int> g = GenDs(s);
    CHECK(g.front() == 0);
    CHECK(g[1] == 1);   // the control: a two-card generic cleanup sheds BOTH Dragonstorms
}

// A staged card (an Apex-exiled Dragonstorm) is never named and is not a HELD payoff: the Apex is the
// protected payoff (engine-protected too) and the staged card sorts last.
TEST_CASE("Dragonstorm discard D12: a staged Dragonstorm is never named; Apex stays protected")
{
    GameState s = MakeDs({ kApex, kDS, kRite, kPyr, kSeeth, kKarr, kMtn, kUtv }, { kMtn });
    s.players[0].hand[1].m_is_staged = true;
    const std::vector<int> o = Ds(s);
    CHECK(o == std::vector<int>{ 5, 7, 4, 2, 3, 6, 0, 1 });
    CheckPermDs(s, o);
}

// A legend we already control is dead (a Lathliss in play + one in hand): it goes first, ahead of
// Utvara (which generic max-MV picks).
TEST_CASE("Dragonstorm discard D13: a duplicate legend is shed first")
{
    const GameState s = MakeDs({ kLath, kDS, kRite, kPyr, kSeeth, kDesp, kIren, kUtv }, { kMtn, kMtn, kLath }, 4);
    const std::vector<int> o = Ds(s);
    CHECK(o == std::vector<int>{ 0, 7, 6, 4, 2, 3, 5, 1 });
    CHECK(GenDs(s).front() == 7);
}

TEST_CASE("Dragonstorm discard: every non-staged hand index is named exactly once on every board")
{
    const std::vector<std::vector<std::string>> hands = {
        { kDS, kDS, kApex, kKarr, kSeeth, kRite, kPyr, kMed },
        { kApex, kApex, kScour, kLath, kRite, kPyr, kSeeth, kMtn },
        { kBloom, kBloom, kMed, kMed, kMed, kMed, kNeedle, kMtn, kDS },
        { kUtv, kScour, kScour, kLath, kKola, kKarr, kMtn, kMtn, kMtn, kApex },
    };
    for (const auto& h : hands)
    {
        const GameState s = MakeDs(h, { kMtn });
        const std::vector<int> o = Ds(s);
        CheckPermDs(s, o);
        CHECK(o.size() == h.size());
    }
}
