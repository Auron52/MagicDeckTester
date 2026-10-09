// Unit tests for the Melira Pod cleanup-discard buckets -- the USER-settled design of 2026-10-09
// (docs/design/melira-pod-discard-policy-proposal.md, "#### Melira Pod: SETTLED"), implemented as
// MeliraPodProvider::CleanupDiscardCandidates behind MTG_POD_BUCKET_DISCARD (default ON).
//
// Every expected order below was derived BY HAND from the SETTLED block:
//   KEEP  mana1 > enabler (or Chord) > mana2 > outlet (or Chord) > mana3 > persist1 (or Chord) > Pod
//         > mana4 > persist2 > mana5 (with a Chord)
//   SHED  dead Karoo > Celes with no R > 2nd Melira > surplus lands > toolbox (Chupacabra > Severance
//         Priest > Rec Sage > [Celes with R] > [surplus dork] > Felidar > Reveillark > Ooze > Voice >
//         Ranger > Recruiter) > extra Pods / Pod with the combo assembled > surplus combo copies
//         (Bloodthrone before a spare Feeder) > extra Chord > [Ranger / Recruiter while a role is
//         missing] > the ladder reversed
// (bracketed positions are the agent readings recorded in the doc). Each board asserts the full shed
// vector, that it names every non-staged index exactly once, and the GENERIC fallback's index 0 --
// the control arm that must differ. The MTG_POD_BUCKET_DISCARD hatch is checked to restore exactly
// the generic ranking.
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

void EnsureCardsPod()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

// A zone card is a NAME-ONLY placeholder in real play (DeckLoader::MakePlaceholder); build hands and
// libraries that way so a policy reading the card's own masks would fail here too.
Card PhPod(const std::string& name, int number)
{
    REQUIRE_MESSAGE(CardDatabase::Instance().Lookup(name) != nullptr, "card not in cards.json: ", name);
    Card c;
    c.m_name = name;
    c.RehashName();
    c.m_number = number;
    return c;
}

const char* kMelira  = "Melira, Sylvok Outcast";
const char* kCeles   = "Celes, Rune Knight";
const char* kFinks   = "Kitchen Finks";
const char* kRedcap  = "Murderous Redcap";
const char* kFeeder  = "Carrion Feeder";
const char* kBlood   = "Bloodthrone Vampire";
const char* kPod     = "Birthing Pod";
const char* kChord   = "Chord of Calling";

// One copy of each combo role plus lands, so a Chord can reach every role unless a test says not.
std::vector<std::string> PodLibrary()
{
    std::vector<std::string> lib = { kMelira, "Vizier of Remedies", kFeeder, kFinks, kRedcap,
                                     "Voice of Resurgence" };
    const char* lands[] = { "Blooming Marsh", "Razorverge Thicket", "Llanowar Wastes", "Branchloft Pathway" };
    for (int k = 0; k < 12; ++k) { lib.push_back(lands[k % 4]); }
    return lib;
}

GameState MakePod(const std::vector<std::string>& hand, const std::vector<std::string>& board,
                  const std::vector<std::string>& library = PodLibrary())
{
    EnsureCardsPod();
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 5;
    int num = 1;
    for (const std::string& n : hand)    { s.players[0].hand.push_back(PhPod(n, num++)); }
    for (const std::string& n : library) { s.players[0].library.push_back(PhPod(n, num++)); }
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

std::vector<int> PodArm(const GameState& s, int arm)
{
    heurarm::t_arm[heurarm::POD_BUCKET_DISCARD] = arm;
    std::vector<int> v = MeliraPodProvider().CleanupDiscardCandidates(s, nullptr);
    heurarm::t_arm[heurarm::POD_BUCKET_DISCARD] = -1;
    return v;
}
std::vector<int> Pod(const GameState& s)    { return PodArm(s, 1); }
std::vector<int> GenPod(const GameState& s) { return GenericProvider().CleanupDiscardCandidates(s, nullptr); }

// Every hand index exactly once (staged ones land last, in the shared ranking's tier C).
void CheckPermutationPod(const GameState& s, const std::vector<int>& order)
{
    std::vector<int> sorted = order;
    std::sort(sorted.begin(), sorted.end());
    std::vector<int> want;
    for (int i = 0; i < static_cast<int>(s.players[0].hand.size()); ++i) { want.push_back(i); }
    CHECK(sorted == want);
}

// The full-order check every board shares: the policy's order, a permutation, the generic control.
void CheckPod(const GameState& s, const std::vector<int>& want, int generic_front)
{
    const std::vector<int> got = Pod(s);
    CHECK(got == want);
    CheckPermutationPod(s, got);
    const std::vector<int> gen = GenPod(s);
    CHECK(gen.front() == generic_front);
    CHECK(gen.front() != got.front());   // the control arm MUST differ
}

}   // namespace

TEST_CASE("Pod discard P1: combo pieces and Pod kept; inert toolbox first; extra Chord above the ladder")
{
    // board: Wastes + Razorverge (2 sources, G/W/B covered). No persist castable next turn (2 mana, no
    // land in hand) -> combo NOT easily castable -> Pod is the 1-of keep after persist1.
    // ladder: [enabler Melira 4, outlet Feeder 5, persist1 Redcap 1 (neither castable: payload first),
    //          Pod 0, persist2 Finks 2]; toolbox Chupacabra 6 > Reveillark 7; Chord 3 extra (all present).
    const GameState s = MakePod(
        { kPod, kRedcap, kFinks, kChord, kMelira, kFeeder, "Ravenous Chupacabra", "Reveillark" },
        { "Llanowar Wastes", "Razorverge Thicket" });
    CheckPod(s, { 6, 7, 3, 2, 0, 1, 5, 4 }, 7);   // generic max-MV pitches Reveillark (MV 5)
    // Default (arm unset, env unset) is the policy.
    CHECK(MeliraPodProvider().CleanupDiscardCandidates(s, nullptr) == Pod(s));
}

TEST_CASE("Pod discard P2: the MTG_POD_BUCKET_DISCARD=0 hatch returns exactly the generic ranking")
{
    const GameState a = MakePod(
        { kPod, kRedcap, kFinks, kChord, kMelira, kFeeder, "Ravenous Chupacabra", "Reveillark" },
        { "Llanowar Wastes", "Razorverge Thicket" });
    const GameState b = MakePod(
        { "Orzhov Basilica", kMelira, kFeeder, kFinks, kPod, kChord, "Ignoble Hierarch", "Voice of Resurgence" },
        {});
    for (const GameState* s : { &a, &b })
    {
        const std::vector<int> off = PodArm(*s, 0);
        CHECK(off == GenPod(*s));
        CHECK(off != Pod(*s));
    }
}

TEST_CASE("Pod discard P3: a Karoo with no other land to bounce is index 0; with a land in play it is a live source")
{
    // No land anywhere but the Basilica: dead. Hierarch (mana1, the only source), then the pieces.
    const GameState dead = MakePod(
        { "Orzhov Basilica", kMelira, kFeeder, kFinks, kPod, kChord, "Ignoble Hierarch", "Voice of Resurgence" },
        {});
    CheckPod(dead, { 0, 7, 5, 4, 3, 2, 1, 6 }, 4);   // generic pitches Birthing Pod
    // A Forest in play: the Basilica covers W+B first (coverage), the Hierarch follows.
    // ladder: [Melira 1, Basilica 0 (mana2), Feeder 2, Hierarch 6 (mana3), Finks 3, Pod 4]
    const GameState live = MakePod(
        { "Orzhov Basilica", kMelira, kFeeder, kFinks, kPod, kChord, "Ignoble Hierarch", "Voice of Resurgence" },
        { "Forest" });
    CheckPod(live, { 7, 5, 4, 3, 6, 2, 0, 1 }, 4);
}

TEST_CASE("Pod discard P4: Celes with no red source sheds first; with Hierarch in play she sits above the inert bodies")
{
    // Forest + Wastes + Caves: G/W/B but no R anywhere. Finks castable from the 3 lands, Melira and
    // Feeder too -> the combo is assembled and easily castable -> Pod 0 is surplus (after the toolbox).
    const std::vector<std::string> hand = { kPod, "Severance Priest", "Reclamation Sage", kMelira, kFeeder,
                                            kFinks, kCeles, "Felidar Guardian" };
    const GameState no_r = MakePod(hand, { "Forest", "Llanowar Wastes", "Caves of Koilos" });
    CheckPod(no_r, { 6, 1, 2, 7, 0, 5, 4, 3 }, 0);   // generic pitches Birthing Pod
    const GameState with_r = MakePod(hand, { "Forest", "Llanowar Wastes", "Caves of Koilos", "Ignoble Hierarch" });
    CheckPod(with_r, { 1, 2, 6, 7, 0, 5, 4, 3 }, 0);   // Severance > Sage > Celes > Felidar
}

TEST_CASE("Pod discard P5: a 2nd Melira -- one in play, or an earlier copy in hand -- sheds early")
{
    // Melira in play: the hand Melira 0 is dead; Vizier 1 is a spare enabler; Bloodthrone 3 the spare
    // outlet (Feeder kept). Finks 4 castable via the Branchloft drop, Redcap 5 not -> Finks is persist1.
    // Ranger 7: no role missing -> toolbox.
    const GameState in_play = MakePod(
        { kMelira, "Vizier of Remedies", kFeeder, kBlood, kFinks, kRedcap, "Branchloft Pathway", "Ranger of Eos" },
        { "Forest", kMelira, "Llanowar Wastes" });
    CheckPod(in_play, { 0, 7, 1, 3, 5, 4, 6, 2 }, 5);   // generic pitches Redcap
    // Two in hand: the second (2) is dead. Razorverge (2 lands in play -> untapped) is the drop that
    // makes Finks castable, so the combo is assembled and Pod 4 is surplus.
    const GameState in_hand = MakePod(
        { kMelira, kFeeder, kMelira, kFinks, kPod, "Razorverge Thicket", "Scavenging Ooze", "Voice of Resurgence" },
        { "Forest", "Llanowar Wastes" });
    CheckPod(in_hand, { 2, 6, 7, 4, 3, 5, 1, 0 }, 4);
}

TEST_CASE("Pod discard P6: Chord fills the MISSING enabler's slot; ANY Chord in hand raises mana to 5")
{
    // 4 lands in play. No enabler anywhere -> Chord 0 takes the enabler slot; mana5 = Branchloft 4
    // (the fastland Blooming Marsh 3 enters tapped at 4 lands -> the surplus land).
    const GameState missing = MakePod(
        { kChord, kFinks, kFeeder, "Blooming Marsh", "Branchloft Pathway", "Reveillark", "Scavenging Ooze",
          "Voice of Resurgence" },
        { "Forest", "Razorverge Thicket", "Llanowar Wastes", "Caves of Koilos" });
    CheckPod(missing, { 3, 5, 6, 7, 4, 1, 2, 0 }, 5);
    // A real Melira 6: the Chord is EXTRA (kept above the toolbox) and STILL raises the quota to 5
    // (SETTLED "5 while a Chord is kept", design-literal -- review FIX 3; the first cut shed the
    // Branchloft here too): Branchloft 4 is mana5, only the tapped fastland 3 is surplus.
    const GameState extra = MakePod(
        { kChord, kFinks, kFeeder, "Blooming Marsh", "Branchloft Pathway", "Reveillark", kMelira,
          "Voice of Resurgence" },
        { "Forest", "Razorverge Thicket", "Llanowar Wastes", "Caves of Koilos" });
    CheckPod(extra, { 3, 5, 7, 0, 4, 1, 2, 6 }, 5);
}

TEST_CASE("Pod discard FIX3: the reviewer's board -- an extra Chord keeps the 5th land (Reveillark sheds, not Branchloft)")
{
    // All pieces present; 4 lands in play; quota 5 with the Chord -> Branchloft 3 is mana5.
    // toolbox Reveillark 4 > Ooze 7 > Voice 6; extra Chord 0; ladder [Melira 5, Feeder 2, Finks 1, Branchloft 3].
    const GameState s = MakePod(
        { kChord, kFinks, kFeeder, "Branchloft Pathway", "Reveillark", kMelira, "Voice of Resurgence",
          "Scavenging Ooze" },
        { "Forest", "Razorverge Thicket", "Llanowar Wastes", "Caves of Koilos" });
    const std::vector<int> got = Pod(s);
    CHECK(got == std::vector<int>{ 4, 7, 6, 0, 3, 1, 2, 5 });   // the first cut: index 0 = Branchloft 3
    CheckPermutationPod(s, got);
}

TEST_CASE("Pod discard P7: Chord fills the missing 1st persist; a 2nd Chord is extra; no fetchable persist -> no fill")
{
    const std::vector<std::string> hand = { kMelira, kFeeder, kChord, kChord, "Razorverge Thicket",
                                            "Voice of Resurgence", "Felidar Guardian", "Reveillark" };
    const std::vector<std::string> board = { "Forest", "Llanowar Wastes", "Caves of Koilos", "Branchloft Pathway" };
    // ladder: [Melira 0, Feeder 1, Chord 2 (persist1), Razorverge 4 (mana5)]; Chord 3 extra.
    CheckPod(MakePod(hand, board), { 6, 7, 5, 3, 4, 2, 1, 0 }, 7);
    // Agent reading: a Chord with no persist creature left in the LIBRARY fills nothing -> both Chords
    // are extra. They still raise the quota to 5 (FIX 3), so the Razorverge stays as mana5.
    const std::vector<std::string> lib_no_persist = { kMelira, "Vizier of Remedies", kFeeder,
                                                      "Blooming Marsh", "Llanowar Wastes", "Forest" };
    CheckPod(MakePod(hand, board, lib_no_persist), { 6, 7, 5, 2, 3, 4, 1, 0 }, 7);
}

TEST_CASE("Pod discard P8: Pod is kept while the combo is not easily castable; extra Pods and a Pod with one in play are surplus")
{
    // Forest in play, Wastes in hand: Redcap (MV 4) is not castable next turn -> Pod 0 kept after
    // persist1; Pod 1 is extra.  ladder: [Melira 2, Wastes 6 (mana2), Feeder 3, Redcap 4, Pod 0]
    const std::vector<std::string> hand = { kPod, kPod, kMelira, kFeeder, kRedcap, "Severance Priest",
                                            "Llanowar Wastes", "Ravenous Chupacabra" };
    CheckPod(MakePod(hand, { "Forest" }), { 7, 5, 1, 0, 4, 3, 6, 2 }, 0);
    // A Pod in play fills the slot: both hand Pods are extra.
    CheckPod(MakePod(hand, { "Forest", kPod }), { 7, 5, 0, 1, 4, 3, 6, 2 }, 0);
}

TEST_CASE("Pod discard P9: Recruiter / Ranger are protected while a role THEY can fetch is missing, plain toolbox otherwise")
{
    const std::vector<std::string> hand = { "Recruiter of the Guard", "Ranger of Eos", kFinks, kFeeder, kBlood,
                                            "Felidar Guardian", kFinks, kFinks };
    // No enabler anywhere -> the ENABLER role is missing. Recruiter (toughness <= 2) reaches Melira /
    // Vizier in the library -> protected, shed only after the surplus copies (Finks 7, Bloodthrone 4).
    // Ranger (MV <= 1) cannot fetch an enabler -> plain toolbox, after Felidar (FIX 2; the first cut
    // protected it too). ladder: [Feeder 3, Finks 2, Finks 6 (persist2, fungible)].
    CheckPod(MakePod(hand, { "Forest", "Llanowar Wastes", "Caves of Koilos" }),
             { 5, 1, 7, 4, 0, 6, 2, 3 }, 1);   // generic pitches Ranger (MV 4)
    // Melira + Redcap in play: nothing missing -> they rejoin the toolbox after Felidar (Ranger first).
    // Board persist 1 fills persist1; the hand Finks 2 is persist2 (the missing lifegain sub-role).
    CheckPod(MakePod(hand, { "Forest", "Llanowar Wastes", "Caves of Koilos", kMelira, kRedcap }),
             { 5, 1, 0, 6, 7, 4, 2, 3 }, 1);
}

TEST_CASE("Pod discard P10: surplus lands -- the tapped fastland first, painlands not demoted -- and dorks are never surplus mana")
{
    const std::vector<std::string> hand = { "Razorverge Thicket", "Branchloft Pathway", "Darkbore Pathway",
                                            "Birds of Paradise", kMelira, kFeeder, kFinks, "Felidar Guardian" };
    // 3 lands in play: one hand mana slot (mana4), and the dork outranks the surplus lands for it.
    // Surplus lands, worst first: Razorverge 0 (enters tapped), Darkbore 2, Branchloft 1.
    CheckPod(MakePod(hand, { "Forest", "Llanowar Wastes", "Caves of Koilos" }),
             { 0, 2, 1, 7, 3, 6, 5, 4 }, 7);   // generic pitches Felidar (MV 4)
    // 4 lands in play: no hand mana slot. The Birds is still not "surplus mana": it sheds in the
    // toolbox (above the inert bodies, below nothing here but Felidar's slot -- Birds 3 before Felidar 7).
    CheckPod(MakePod(hand, { "Forest", "Llanowar Wastes", "Caves of Koilos", "Razorverge Thicket" }),
             { 0, 2, 1, 3, 7, 6, 5, 4 }, 7);
}

TEST_CASE("Pod discard P11: both persist castable -> Redcap is persist1; the USER's toolbox order Chupacabra > Severance > Sage > Felidar")
{
    const GameState s = MakePod(
        { kFinks, kRedcap, kMelira, kFeeder, "Ravenous Chupacabra", "Severance Priest", "Reclamation Sage",
          "Felidar Guardian" },
        { "Forest", "Llanowar Wastes", "Caves of Koilos", "Llanowar Wastes" });
    CheckPod(s, { 4, 5, 6, 7, 0, 1, 3, 2 }, 1);   // generic pitches Redcap
}

TEST_CASE("Pod discard P12: a staged card is never in the policy's order (it lands last, tier C)")
{
    GameState s = MakePod(
        { kPod, kRedcap, kFinks, kChord, kMelira, kFeeder, "Ravenous Chupacabra", "Reveillark" },
        { "Llanowar Wastes", "Razorverge Thicket" });
    s.players[0].hand[3].m_is_staged = true;
    const std::vector<int> got = Pod(s);
    CHECK(got == std::vector<int>{ 6, 7, 2, 0, 1, 5, 4, 3 });
    CheckPermutationPod(s, got);
}

// ---- 2026-10-09 review fixes ------------------------------------------------------------------------

TEST_CASE("Pod discard FIX1a: the reviewer's board -- the lone {W} land is not shed while the Pod is ruled surplus off it")
{
    // Board Forest / Wastes / Marsh / Hierarch: no {W}. The kept Vizier {1}{W} needs Caves 0. With the
    // Chord the quota is 5, so Caves is mana5; Vizier is castable off it, the combo is assembled, Pod 4
    // is surplus. toolbox Ooze 7 > Voice 6; extra Chord 5; ladder [Vizier 1, Feeder 2, Finks 3, Caves 0].
    const GameState s = MakePod(
        { "Caves of Koilos", "Vizier of Remedies", kFeeder, kFinks, kPod, kChord, "Voice of Resurgence",
          "Scavenging Ooze" },
        { "Forest", "Llanowar Wastes", "Blooming Marsh", "Ignoble Hierarch" });
    const std::vector<int> got = Pod(s);
    CHECK(got == std::vector<int>{ 7, 6, 4, 5, 0, 3, 2, 1 });   // the first cut: index 0 = Caves 0
    CheckPermutationPod(s, got);
}

TEST_CASE("Pod discard FIX1b: COLOURS FIRST BEATS THE COUNT -- a land supplying a kept piece's missing colour is kept past the quota")
{
    // Same board, no Chord: quota 4 is filled by the board, yet Caves 0 is the only {W} for the kept
    // Vizier 1 -> kept just above it (integrator reading). Vizier castable off the KEPT Caves -> combo
    // assembled -> Pod 4 surplus. toolbox Felidar 5 > Ooze 7 > Voice 6; ladder [Caves 0, Vizier 1,
    // Feeder 2, Finks 3].
    const GameState s = MakePod(
        { "Caves of Koilos", "Vizier of Remedies", kFeeder, kFinks, kPod, "Felidar Guardian",
          "Voice of Resurgence", "Scavenging Ooze" },
        { "Forest", "Llanowar Wastes", "Blooming Marsh", "Ignoble Hierarch" });
    CheckPod(s, { 5, 7, 6, 4, 3, 2, 1, 0 }, 4);   // the first cut: [0, 5, 7, 6, 4, 3, 2, 1]
}

TEST_CASE("Pod discard FIX1c: 'easily castable' counts only KEPT lands -- a surplus land does not make the combo castable")
{
    // Forest / Wastes / Caves in play (3 units). The quota's 4th source is the Birds 0 (dorks outrank
    // surplus lands), so Branchloft 1 is surplus. Redcap 4 (MV 4) is castable only off that surplus land
    // -> NOT easily castable -> the Pod 5 is kept after persist1.
    // ladder [Melira 2, Feeder 3, Redcap 4, Pod 5, Birds 0 (mana4)]; toolbox Felidar 6 > Voice 7.
    const GameState s = MakePod(
        { "Birds of Paradise", "Branchloft Pathway", kMelira, kFeeder, kRedcap, kPod, "Felidar Guardian",
          "Voice of Resurgence" },
        { "Forest", "Llanowar Wastes", "Caves of Koilos" });
    CheckPod(s, { 1, 6, 7, 0, 5, 4, 3, 2 }, 4);   // the first cut shed the Pod 5 ahead of the ladder
}

TEST_CASE("Pod discard FIX2a: the reviewer's board -- nothing Ranger can fetch is missing, so Ranger is toolbox")
{
    // Board Melira + Forest / Wastes / Caves; two Finks = persist 2 of 2 (fungible: a missing Redcap is
    // not a missing role). Feeder kept, Finks castable -> combo assembled -> both Pods surplus.
    // (Hand reordered from the reviewer's so the generic control differs: a Pod first.)
    // toolbox Ranger 4; Pods 0, 5; spare Bloodthrone 7; extra Chord 6; ladder [Feeder 1, Finks 2, Finks 3].
    const GameState s = MakePod(
        { kPod, kFeeder, kFinks, kFinks, "Ranger of Eos", kPod, kChord, kBlood },
        { kMelira, "Forest", "Llanowar Wastes", "Caves of Koilos" });
    CheckPod(s, { 4, 0, 5, 7, 6, 3, 2, 1 }, 0);   // the first cut: index 0 = Birthing Pod 0
}

TEST_CASE("Pod discard FIX2b: persist is ONE fungible role of 2 -- one copy short protects Recruiter, never Ranger")
{
    // Board Melira + Feeder + 3 lands; one Finks in hand -> persist 1 of 2 is missing. Recruiter 1
    // reaches Finks / Redcap -> protected (after the spare Bloodthrone 7). Ranger 0 (MV <= 1) cannot ->
    // toolbox, after Voice. toolbox Chupacabra 6 > Felidar 3 > Ooze 5 > Voice 4 > Ranger 0.
    const GameState s = MakePod(
        { "Ranger of Eos", "Recruiter of the Guard", kFinks, "Felidar Guardian", "Voice of Resurgence",
          "Scavenging Ooze", "Ravenous Chupacabra", kBlood },
        { "Forest", "Llanowar Wastes", "Caves of Koilos", kMelira, kFeeder });
    CheckPod(s, { 6, 3, 5, 4, 0, 7, 1, 2 }, 0);   // the first cut: Ranger after Bloodthrone, protected
}

TEST_CASE("Pod discard FIX2c: a missing OUTLET protects both tutors (each can fetch Carrion Feeder)")
{
    // Board Melira + 3 lands; no outlet anywhere. Ranger 0 (Feeder is MV 1) and Recruiter 1 (Feeder is
    // 1/1) are both protected, Ranger first. Finks 2 castable -> persist1; Redcap 3 persist2.
    const GameState s = MakePod(
        { "Ranger of Eos", "Recruiter of the Guard", kFinks, kRedcap, "Felidar Guardian", "Voice of Resurgence",
          "Scavenging Ooze", "Ravenous Chupacabra" },
        { "Forest", "Llanowar Wastes", "Caves of Koilos", kMelira });
    CheckPod(s, { 7, 4, 6, 5, 0, 1, 3, 2 }, 0);
}

TEST_CASE("Pod discard FIX4: the order is NOT prefix-stable while the policy is on -- re-rank per shed")
{
    // The hook: false with the policy on, true with the =0 hatch (the generic ranking is stable).
    heurarm::t_arm[heurarm::POD_BUCKET_DISCARD] = 1;
    CHECK_FALSE(MeliraPodProvider().CleanupDiscardShedStable());
    heurarm::t_arm[heurarm::POD_BUCKET_DISCARD] = 0;
    CHECK(MeliraPodProvider().CleanupDiscardShedStable());
    heurarm::t_arm[heurarm::POD_BUCKET_DISCARD] = -1;

    // Why: the combo is on the board (one persist), the hand is an extra Chord 0, a Branchloft 1 and a
    // Recruiter 2 (persist 1 of 2 missing -> protected). The Chord keeps the quota at 5, so the
    // Branchloft is mana5 and sheds last: [Chord, Recruiter, Branchloft]. Once the Chord is gone the
    // quota is 4 and the Branchloft is SURPLUS -- it now sheds before the Recruiter, which a prefix of
    // the first answer would have got backwards.
    const std::vector<std::string> board = { "Forest", "Razorverge Thicket", "Llanowar Wastes",
                                             "Caves of Koilos", kMelira, kFeeder, kFinks };
    GameState s = MakePod({ kChord, "Branchloft Pathway", "Recruiter of the Guard" }, board);
    CHECK(Pod(s) == std::vector<int>{ 0, 2, 1 });
    s.players[0].hand.erase(s.players[0].hand.begin());   // shed the Chord
    const std::vector<int> again = Pod(s);                 // hand: Branchloft 0, Recruiter 1
    CHECK(again == std::vector<int>{ 0, 1 });              // a prefix would say Recruiter, then Branchloft
}
