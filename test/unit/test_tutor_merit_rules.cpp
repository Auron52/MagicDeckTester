// Unit cover for the two tutor MERIT rules that replaced the deleted tutor width (2026-11-22; USER: "It
// should just search all options unless the heuristic says otherwise and the heuristic should have full
// control of how many it returns"):
//   * HinataProvider::TutorCandidates (Gamble) -- Hinata online: every NEEDED name (SituationalCardRank
//     above the cantrips), rank-descending; only when none is left, every name at the ScryKeepOnTop line
//     (kHinataKeepThreshold); only when none clears that, the full ranked list. Hinata absent: Hinata.
//   * MeliraPodProvider::TutorCandidates (Recruiter of the Guard) -- the missing combo pieces / Pod fuel
//     plus the USER's Pod/Chord whitelist (PutTargetOk); dorks join only when a held persist payoff
//     is short on known mana. Goldfish-dead toolbox bodies are not offered.
#include <doctest/doctest.h>

#include "ai/DecisionProviders.h"
#include "cards/CardDatabase.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"

#include <algorithm>
#include <string>
#include <vector>

namespace
{

void EnsureCardsTm()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

const CardDefinition& DefTm(const std::string& name)
{
    EnsureCardsTm();
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE_MESSAGE(d != nullptr, "card not in cards.json: ", name);
    return *d;
}

// Zone cards are NAME-ONLY placeholders in real play (DeckLoader::MakePlaceholder).
Card PhTm(const std::string& name, int number)
{
    DefTm(name);
    Card c;
    c.m_name = name;
    c.RehashName();
    c.m_number = number;
    return c;
}

GameState MakeTm(const std::vector<std::string>& hand, const std::vector<std::string>& board,
                 const std::vector<std::string>& library, int lands_played)
{
    EnsureCardsTm();
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 5;
    int num = 1;
    for (const std::string& n : hand)    { s.players[0].hand.push_back(PhTm(n, num++)); }
    for (const std::string& n : library) { s.players[0].library.push_back(PhTm(n, num++)); }
    for (const std::string& n : board)
    {
        Permanent p;
        p.card              = DefTm(n).card;
        p.card.m_number     = num++;
        p.controller_index  = 0;
        p.owner_index       = 0;
        p.entered_this_turn = false;
        s.battlefield.push_back(p);
    }
    s.players[0].lands_played_this_turn = lands_played;
    return s;
}

bool Has(const std::vector<std::string>& v, const std::string& n)
{ return std::find(v.begin(), v.end(), n) != v.end(); }

const char* kHinata = "Hinata, Dawn-Crowned";

}   // namespace

TEST_CASE("Hinata Gamble merit rule: online -> the needed pieces only, rank-descending")
{
    // Hinata in play on four lands carrying all her colours, the drop already spent, a Crackle in hand.
    const GameState s = MakeTm(
        { "Crackle with Power" },
        { kHinata, "Mystic Monastery", "Island", "Mountain", "Forbidden Orchard" },
        { "Crackle with Power", "Reality Spasm", "Soulfire Eruption", "Irencrag Feat", "Ponder",
          "Island", "Distorting Wake", kHinata, "Izzet Boilerworks" },
        /*lands_played=*/1);
    const CardParams& pp = DefTm("Gamble").params;
    const std::vector<std::string> got = HinataProvider().TutorCandidates(s, 0, pp);
    // Missing Spasm (750) > Irencrag (710) > Soulfire (700); a cantrip (500) is a dig for what the tutor
    // can name directly, so it waits until nothing needed is left.
    const std::vector<std::string> want = { "Reality Spasm", "Irencrag Feat", "Soulfire Eruption" };
    CHECK(got == want);
    CHECK_FALSE(Has(got, "Ponder"));
    // Below the keep line: a 2nd Crackle and a 2nd Hinata (duplicates), the surplus lands (no open drop,
    // mana not screwed), the inert interaction.
    CHECK_FALSE(Has(got, "Crackle with Power"));
    CHECK_FALSE(Has(got, kHinata));
    CHECK_FALSE(Has(got, "Island"));
    CHECK_FALSE(Has(got, "Distorting Wake"));
    // The generic list offers them all: the rule, not the legality, removed them.
    const std::vector<std::string> gen = GenericProvider().TutorCandidates(s, 0, pp);
    CHECK(Has(gen, "Crackle with Power"));
    CHECK(Has(gen, "Distorting Wake"));
    CHECK(Has(gen, "Island"));
}

TEST_CASE("Hinata Gamble merit rule: nothing needed left -> the keep line (cantrip, open-drop land)")
{
    // No piece left in the library. The land drop is open and mana is under the 7-source target, so an
    // untapped land is live mana this turn (kRankExtraLand 380 clears the keep line); a tapped one
    // (Boilerworks, 250) does not, nor does the inert interaction.
    const GameState s = MakeTm(
        { "Crackle with Power" },
        { kHinata, "Mystic Monastery", "Island", "Mountain", "Forbidden Orchard" },
        { "Ponder", "Island", "Izzet Boilerworks", "Distorting Wake", "Crackle with Power" },
        /*lands_played=*/0);
    const std::vector<std::string> got = HinataProvider().TutorCandidates(s, 0, DefTm("Gamble").params);
    const std::vector<std::string> want = { "Ponder", "Island" };
    CHECK(got == want);
}

TEST_CASE("Hinata Gamble merit rule: a mana-screwed hand adds the land that casts her")
{
    // Hinata in HAND on two lands with no white: a land supplying the missing colour is a NEED (screwed,
    // drop open: kRankScrewedLandNow) beside the missing combo piece.
    const GameState s = MakeTm(
        { kHinata, "Crackle with Power" },
        { "Island", "Mountain" },
        { "Reality Spasm", "Forbidden Orchard", "Ponder" },
        /*lands_played=*/0);
    const std::vector<std::string> got = HinataProvider().TutorCandidates(s, 0, DefTm("Gamble").params);
    CHECK(Has(got, "Forbidden Orchard"));
    CHECK(Has(got, "Reality Spasm"));
    CHECK_FALSE(Has(got, "Ponder"));
    CHECK(got.front() == "Forbidden Orchard");
}

TEST_CASE("Hinata Gamble merit rule: no Hinata in play or hand -> fetch Hinata alone")
{
    const GameState s = MakeTm(
        { "Crackle with Power" },
        { "Island", "Mountain" },
        { "Reality Spasm", kHinata, "Ponder" },
        /*lands_played=*/1);
    const std::vector<std::string> got = HinataProvider().TutorCandidates(s, 0, DefTm("Gamble").params);
    CHECK(got == std::vector<std::string>{ kHinata });
}

TEST_CASE("Hinata Gamble merit rule: nothing clears the line -> the full ranked list")
{
    const GameState s = MakeTm(
        { "Crackle with Power" },
        { kHinata, "Mystic Monastery", "Island", "Mountain", "Forbidden Orchard" },
        { "Distorting Wake", "Icy Blast" },
        /*lands_played=*/1);
    const std::vector<std::string> got = HinataProvider().TutorCandidates(s, 0, DefTm("Gamble").params);
    CHECK(got.size() == 2);
    CHECK(Has(got, "Distorting Wake"));
    CHECK(Has(got, "Icy Blast"));
}

TEST_CASE("Melira Recruiter merit rule: missing pieces + the USER's whitelist, no dorks or toolbox")
{
    // Enabler (Melira) and free outlet (Carrion Feeder) on the battlefield; the persist body is missing.
    const GameState s = MakeTm(
        {},
        { "Melira, Sylvok Outcast", "Carrion Feeder", "Blooming Marsh", "Razorverge Thicket" },
        { "Kitchen Finks", "Murderous Redcap", "Ignoble Hierarch", "Reclamation Sage", "Scavenging Ooze",
          "Vizier of Remedies", "Ranger of Eos", "Voice of Resurgence", "Llanowar Wastes" },
        /*lands_played=*/1);
    const CardParams& pp = DefTm("Recruiter of the Guard").params;
    const std::vector<std::string> got = MeliraPodProvider().TutorCandidates(s, 0, pp);
    REQUIRE_FALSE(got.empty());
    // The missing persist bodies lead (score 60, cheaper first: Finks MV3 before Redcap MV4).
    CHECK(got[0] == "Kitchen Finks");
    CHECK(got[1] == "Murderous Redcap");
    // A piece-tutor while a piece is missing stays (Ranger of Eos finds the 1-drop outlets).
    CHECK(Has(got, "Ranger of Eos"));
    // Not fetched: a dork, the goldfish-dead toolbox, a 2nd enabler, Voice with no Pod to climb.
    CHECK_FALSE(Has(got, "Ignoble Hierarch"));
    CHECK_FALSE(Has(got, "Reclamation Sage"));
    CHECK_FALSE(Has(got, "Scavenging Ooze"));
    CHECK_FALSE(Has(got, "Vizier of Remedies"));
    CHECK_FALSE(Has(got, "Voice of Resurgence"));
    // ...all of which the generic list offers.
    const std::vector<std::string> gen = GenericProvider().TutorCandidates(s, 0, pp);
    CHECK(Has(gen, "Ignoble Hierarch"));
    CHECK(Has(gen, "Scavenging Ooze"));
    CHECK(Has(gen, "Vizier of Remedies"));
}

TEST_CASE("Melira Recruiter merit rule: only dorks and toolbox left -> the full ranked list")
{
    const GameState s = MakeTm(
        {},
        { "Melira, Sylvok Outcast", "Carrion Feeder", "Kitchen Finks", "Murderous Redcap" },
        { "Ignoble Hierarch", "Reclamation Sage" },
        /*lands_played=*/1);
    const std::vector<std::string> got =
        MeliraPodProvider().TutorCandidates(s, 0, DefTm("Recruiter of the Guard").params);
    CHECK(got.size() == 2);
    CHECK(Has(got, "Ignoble Hierarch"));
    CHECK(Has(got, "Reclamation Sage"));
}

TEST_CASE("Melira Recruiter merit rule: a held Redcap short on mana keeps the dork line")
{
    const GameState short_mana = MakeTm(
        { "Carrion Feeder", "Melira, Sylvok Outcast", "Murderous Redcap", "Razorverge Thicket" },
        { "Forest", "Blooming Marsh" },
        { "Birds of Paradise", "Reclamation Sage", "Kitchen Finks" },
        /*lands_played=*/1);
    const CardParams& pp = DefTm("Recruiter of the Guard").params;
    const std::vector<std::string> got = MeliraPodProvider().TutorCandidates(short_mana, 0, pp);
    CHECK(Has(got, "Birds of Paradise"));
    CHECK_FALSE(Has(got, "Reclamation Sage"));

    GameState enough_mana = short_mana;
    Permanent fourth;
    fourth.card = DefTm("Forest").card;
    fourth.controller_index = 0;
    fourth.owner_index = 0;
    enough_mana.battlefield.push_back(fourth);
    const std::vector<std::string> after = MeliraPodProvider().TutorCandidates(enough_mana, 0, pp);
    CHECK_FALSE(Has(after, "Birds of Paradise"));
}
