// Unit cover for the per-deck BUCKET POLICY (src/analyzer/BucketPolicy.h) -- the stored human ruling
// about which cards equivalence discovery may put in one bucket.
//
// What these lock down is the file's CONTRACT, because the failure mode this exists to prevent is a
// silent one: a ruling that stops applying (renamed card, reordered file, missing reason) and nobody
// notices until a table has been generated on buckets the user rejected. Every case below fails if
// the corresponding guard is removed.
#include <doctest/doctest.h>
#include <filesystem>
#include <fstream>
#include "../../src/analyzer/BucketPolicy.h"

namespace
{
// Write a policy file for a throwaway deck stem and return the deck path LoadBucketPolicy expects.
std::filesystem::path WritePolicy(const std::string& stem, const std::string& body)
{
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "mtg_bucketpolicy_test";
    std::filesystem::create_directories(dir);
    const std::filesystem::path deck = dir / (stem + ".cod");
    { std::ofstream f(deck); f << "decklist placeholder\n"; }
    { std::ofstream f(dir / (stem + ".buckets.json")); f << body; }
    return deck;
}
}  // namespace

TEST_CASE("bucket policy: a deck with no file behaves exactly as before")
{
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "mtg_bucketpolicy_test";
    std::filesystem::create_directories(dir);
    const std::filesystem::path deck = dir / "no_policy_here.cod";
    { std::ofstream f(deck); f << "x\n"; }
    std::filesystem::remove(dir / "no_policy_here.buckets.json");

    const BucketPolicy pol = LoadBucketPolicy(deck);
    CHECK(pol.Empty());
    CHECK(pol.path.empty());
    CHECK(pol.MergeSpec().empty());
    // The whole point of Empty(): the call sites pass nullptr, so discovery is untouched.
    CHECK_FALSE(pol.MustKeepApart("Colossus Hammer", "O-Naginata"));
}

TEST_CASE("bucket policy: keep_apart is symmetric and does not leak to other pairs")
{
    const auto deck = WritePolicy("kept_apart", R"({
      "keep_apart": [ { "cards": ["Colossus Hammer", "O-Naginata"], "why": "opposite enablers" } ]
    })");
    const BucketPolicy pol = LoadBucketPolicy(deck);

    REQUIRE_FALSE(pol.Empty());
    CHECK(pol.MustKeepApart("Colossus Hammer", "O-Naginata"));
    CHECK(pol.MustKeepApart("O-Naginata", "Colossus Hammer"));   // order must not matter
    // A card named in a group is not thereby kept apart from everything.
    CHECK_FALSE(pol.MustKeepApart("Colossus Hammer", "Bonesplitter"));
    CHECK_FALSE(pol.MustKeepApart("Bonesplitter", "Grafted Wargear"));
    // A card is never kept apart from itself (it would veto its own class).
    CHECK_FALSE(pol.MustKeepApart("Colossus Hammer", "Colossus Hammer"));
}

TEST_CASE("bucket policy: a 3+ card keep_apart group separates every pair in it")
{
    const auto deck = WritePolicy("triple", R"({
      "keep_apart": [ { "cards": ["A", "B", "C"], "why": "all distinct roles" } ]
    })");
    const BucketPolicy pol = LoadBucketPolicy(deck);
    CHECK(pol.MustKeepApart("A", "B"));
    CHECK(pol.MustKeepApart("B", "C"));
    CHECK(pol.MustKeepApart("A", "C"));
}

TEST_CASE("bucket policy: Canonical ignores formatting but not content")
{
    // Same ruling, written two ways: cards swapped inside the group, groups swapped, different
    // reason text. Fingerprint-equal, because reformatting a file or improving a comment must not
    // invalidate hours of generated cells.
    const auto a = WritePolicy("canon_a", R"({
      "keep_apart": [ { "cards": ["Colossus Hammer", "O-Naginata"], "why": "reason one" },
                      { "cards": ["X", "Y"], "why": "reason two" } ]
    })");
    const auto b = WritePolicy("canon_b", R"({
      "keep_apart": [ { "cards": ["Y", "X"], "why": "COMPLETELY different prose" },
                      { "cards": ["O-Naginata", "Colossus Hammer"], "why": "and here too" } ]
    })");
    CHECK(LoadBucketPolicy(a).Canonical() == LoadBucketPolicy(b).Canonical());

    // But a real change to the ruling MUST move the fingerprint, or a stale cache survives it.
    const auto c = WritePolicy("canon_c", R"({
      "keep_apart": [ { "cards": ["Colossus Hammer", "O-Naginata"], "why": "reason one" } ]
    })");
    CHECK(LoadBucketPolicy(a).Canonical() != LoadBucketPolicy(c).Canonical());

    // keep_apart and merge are different rulings even over the same cards.
    const auto d = WritePolicy("canon_d", R"({
      "merge": [ { "cards": ["Colossus Hammer", "O-Naginata"], "why": "reason one" } ]
    })");
    CHECK(LoadBucketPolicy(c).Canonical() != LoadBucketPolicy(d).Canonical());
}

TEST_CASE("bucket policy: merge groups reach the force-merge path in its own wire format")
{
    const auto deck = WritePolicy("merged", R"({
      "merge": [ { "cards": ["Flooded Strand", "Windswept Heath"], "why": "fetch cycle" },
                 { "cards": ["P", "Q"], "why": "same dead draw" } ]
    })");
    const BucketPolicy pol = LoadBucketPolicy(deck);
    CHECK(pol.MergeSpec() == "Flooded Strand,Windswept Heath;P,Q");
    // merge is NOT keep_apart -- the two lists must not bleed into each other.
    CHECK_FALSE(pol.MustKeepApart("Flooded Strand", "Windswept Heath"));
}

TEST_CASE("bucket policy: a ruling with no reason is rejected")
{
    const auto deck = WritePolicy("noreason", R"({
      "keep_apart": [ { "cards": ["A", "B"] } ]
    })");
    CHECK_THROWS_AS(LoadBucketPolicy(deck), std::runtime_error);
}

TEST_CASE("bucket policy: comma full names merge distinct Ulamogs after discovery")
{
    const auto deck = WritePolicy("ulamogs", R"({
      "merge": [{"cards": ["Ulamog, the Ceaseless Hunger", "Ulamog, the Infinite Gyre"],
                 "why": "user-approved Cyclone bucket ruling"}]
    })");
    const BucketPolicy pol = LoadBucketPolicy(deck);
    const std::vector<std::string> names = {
        "Ulamog, the Ceaseless Hunger", "Ulamog, the Infinite Gyre", "Emrakul, the Aeons Torn",
        "Island", "Watery Grave", "Mountain"
    };
    CHECK_NOTHROW(ValidateBucketPolicy(pol, names));
    const std::string spec = pol.MergeSpec();
    CHECK(nlohmann::json::parse(spec) == nlohmann::json::array({pol.merge[0].cards}));
    // Feed raw discovery classes through the actual production post-pass. It must preserve
    // an unrelated automatic group and Emrakul's separate class, not flatten every Eldrazi.
    EquivReport eq;
    eq.classes = {{{names[0]}, {1}}, {{names[1]}, {2}}, {{names[2]}, {3}},
                  {{names[3], names[4]}, {4}}, {{names[5]}, {5}}};
    std::ostringstream log;
    ApplyBucketMergeSpec(eq, spec, log);
    REQUIRE(eq.classes.size() == 4);
    CHECK(eq.classes[0].members == pol.merge[0].cards);
    CHECK(eq.classes[0].signature == std::vector<int>{1});
    CHECK(eq.classes[1].members == std::vector<std::string>{names[2]});
    CHECK(eq.classes[2].members == std::vector<std::string>{names[3], names[4]});
    CHECK(eq.classes[3].members == std::vector<std::string>{names[5]});
    CHECK(log.str().empty());
    // A cached raw discovery result takes the same post-pass; repeated merges are idempotent.
    ApplyBucketMergeSpec(eq, spec, log);
    CHECK(eq.classes.size() == 4);
}

TEST_CASE("bucket policy: structured merges compose with legacy environment groups")
{
    BucketPolicy pol;
    pol.merge = {{{"A, first", "A, second"}, "distinct full names"},
                 {{"P", "Q"}, "ordinary group"}};
    EquivReport eq;
    eq.classes = {{{"A, first"}, {}}, {{"A, second"}, {}}, {{"P"}, {}},
                  {{"Q"}, {}}, {{"R"}, {}}, {{"Untouched"}, {}}};
    std::ostringstream log;
    // main.cpp appends the env spec with a semicolon, including overlapping merge groups.
    ApplyBucketMergeSpec(eq, pol.MergeSpec() + "; Q, R ; missing ", log);
    REQUIRE(eq.classes.size() == 3);
    CHECK(eq.classes[0].members == std::vector<std::string>{"A, first", "A, second"});
    CHECK(eq.classes[1].members == std::vector<std::string>{"P", "Q", "R"});
    CHECK(eq.classes[2].members == std::vector<std::string>{"Untouched"});
    CHECK(log.str().find("card not found: 'missing'") != std::string::npos);
}

TEST_CASE("bucket policy: legacy unique prefixes and comma-free wire stay compatible")
{
    EquivReport eq;
    eq.classes = {{{"Karrthus, Tyrant of Jund"}, {}}, {{"Lathliss, Dragon Queen"}, {}},
                  {{"Other"}, {}}};
    std::ostringstream log;
    ApplyBucketMergeSpec(eq, " Karrthus, Lathliss ; missing ", log);
    REQUIRE(eq.classes.size() == 2);
    CHECK(eq.classes[0].members == std::vector<std::string>{
        "Karrthus, Tyrant of Jund", "Lathliss, Dragon Queen"});
    CHECK(eq.classes[1].members == std::vector<std::string>{"Other"});
    CHECK(log.str().find("card not found: 'missing'") != std::string::npos);
}

TEST_CASE("bucket policy: structured full names never fall back to ambiguous prefixes")
{
    EquivReport eq;
    eq.classes = {{{"Ulamog, the Ceaseless Hunger"}, {}},
                  {{"Ulamog, the Infinite Gyre"}, {}}};
    std::ostringstream log;
    CHECK_THROWS_AS(ApplyBucketMergeSpec(eq, R"([["Ulamog","Ulamog, the Infinite Gyre"]])", log),
                    std::runtime_error);
    CHECK(eq.classes.size() == 2);
    CHECK_THROWS_AS(ApplyBucketMergeSpec(eq, R"([["Ulamog, the Infinite Gyre"]])", log),
                    std::runtime_error);
    CHECK_THROWS_AS(ApplyBucketMergeSpec(eq, R"([["Ulamog, the Infinite Gyre",3]])", log),
                    std::runtime_error);
    CHECK_THROWS_AS(ApplyBucketMergeSpec(eq, R"([["A","B"]]trailing)", log), std::runtime_error);
}

TEST_CASE("bucket policy: the shipped Cyclone ruling names only the approved Ulamog pair")
{
    const std::filesystem::path deck = "decks/Unpredictable Cyclone/Unpredictable Cyclone.cod";
    if (!std::filesystem::exists(deck)) { return; }
    const BucketPolicy pol = LoadBucketPolicy(deck);
    REQUIRE(pol.merge.size() == 1);
    CHECK(pol.keep_apart.empty());
    CHECK(pol.merge[0].cards == std::vector<std::string>{
        "Ulamog, the Ceaseless Hunger", "Ulamog, the Infinite Gyre"});
}

TEST_CASE("bucket policy: a one-card group is rejected")
{
    // A group of one expresses nothing and is almost certainly a typo for a real pairing.
    const auto deck = WritePolicy("single", R"({
      "keep_apart": [ { "cards": ["A"], "why": "..." } ]
    })");
    CHECK_THROWS_AS(LoadBucketPolicy(deck), std::runtime_error);
}

TEST_CASE("bucket policy: naming a card the deck does not contain is an ERROR, not a no-op")
{
    // THE failure this file exists to prevent: a rename or a decklist edit quietly turns a
    // deliberate decision into no decision at all, and the next generation merges the pair again.
    const auto deck = WritePolicy("unknown_card", R"({
      "keep_apart": [ { "cards": ["Colossus Hammer", "O-Naginatta"], "why": "typo in the second name" } ]
    })");
    const BucketPolicy pol = LoadBucketPolicy(deck);
    const std::vector<std::string> deck_cards = { "Colossus Hammer", "O-Naginata", "Bonesplitter" };
    CHECK_THROWS_AS(ValidateBucketPolicy(pol, deck_cards), std::runtime_error);

    // The correctly-spelled ruling validates against the same decklist.
    const auto ok_deck = WritePolicy("known_card", R"({
      "keep_apart": [ { "cards": ["Colossus Hammer", "O-Naginata"], "why": "opposite enablers" } ]
    })");
    CHECK_NOTHROW(ValidateBucketPolicy(LoadBucketPolicy(ok_deck), deck_cards));

    // An empty policy validates against any decklist (nothing to check).
    CHECK_NOTHROW(ValidateBucketPolicy(BucketPolicy{}, {}));
}

TEST_CASE("bucket policy: the shipped KittyEquipment ruling loads and keeps the pair apart")
{
    // The real file, not a fixture -- so an edit that breaks the format fails the suite rather than
    // a generation hours later.
    const std::filesystem::path deck = "decks/KittyEquipment/v1-kemba-balan/KittyEquipment.cod";
    if (!std::filesystem::exists(deck)) { return; }   // running outside the repo root
    const BucketPolicy pol = LoadBucketPolicy(deck);
    REQUIRE_FALSE(pol.Empty());
    CHECK(pol.MustKeepApart("Colossus Hammer", "O-Naginata"));
    REQUIRE(pol.keep_apart.size() == 1);
    CHECK_FALSE(pol.keep_apart[0].why.empty());
}
