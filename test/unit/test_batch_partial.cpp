// Unit tests for the per-game partial results of `mtg --batch` (runner/BatchPartial.h).
//
//  1. A record round-trips through FormatRecord / ParseRecord, abandoned and played alike.
//  2. A TORN line is never parsed as a result: every proper prefix of a record line is rejected, an
//     unterminated tail is dropped by ParseFile even when its bytes happen to be complete, and a
//     flipped byte fails the checksum.
//  3. A zero-filled tail (what a crash can leave behind a delayed-allocation write) is dropped.
//  4. The MERGE: identical duplicate records fold into one; disagreeing ones are dropped (replayed); the
//     same game under another per-game ceiling is a separate entry, never a conflict.
//  5. The case digest of a resumed job (persisted games + replayed games, folded in gi order) equals
//     the uninterrupted one, whatever order the games landed in.
#include <doctest/doctest.h>

#include "runner/BatchPartial.h"

#include <algorithm>
#include <map>
#include <string>
#include <vector>

using batchpartial::Record;

namespace
{
Record Make(int gi, int wt, uint64_t dg, bool abandoned = false)
{
    Record r;
    r.gi = gi;
    r.seed = 7000 + static_cast<uint64_t>(gi);
    r.abandoned = abandoned;
    r.win_turn = abandoned ? 0 : wt;
    r.digest = dg;
    r.units = 1000 + gi;
    r.ms = 50 + gi;
    r.limit = 250000;
    r.rel = gi % 2 ? std::string("R25000000,10,4000") : std::string("-");
    return r;
}

bool Same(const Record& a, const Record& b)
{
    return a.gi == b.gi && a.seed == b.seed && a.abandoned == b.abandoned && a.win_turn == b.win_turn
        && a.digest == b.digest && a.units == b.units && a.ms == b.ms && a.limit == b.limit && a.rel == b.rel;
}
}  // namespace

TEST_CASE("batch partial: a record round-trips")
{
    for (const Record& r : {Make(0, 5, 0x0123456789abcdefULL), Make(3, -1, 0xffffffffffffffffULL),
                            Make(17, 0, 0, true)})
    {
        const std::string line = batchpartial::FormatRecord(r);
        REQUIRE(line.back() == '\n');
        const auto p = batchpartial::ParseRecord(std::string_view(line).substr(0, line.size() - 1));
        REQUIRE(p.has_value());
        CHECK(Same(*p, r));
    }
}

TEST_CASE("batch partial: a torn or corrupted line is never a result")
{
    const std::string line = batchpartial::FormatRecord(Make(4, 6, 0xdeadbeefcafef00dULL));
    const std::string body = line.substr(0, line.size() - 1);
    // Every proper prefix of the line is rejected by the record parser.
    for (std::size_t n = 0; n < body.size(); ++n)
    { CHECK_FALSE(batchpartial::ParseRecord(std::string_view(body).substr(0, n)).has_value()); }
    // A single flipped character anywhere fails the checksum (or the syntax).
    for (std::size_t i = 0; i < body.size(); ++i)
    {
        std::string bad = body;
        bad[i] = (bad[i] == '1') ? '2' : '1';
        CHECK_FALSE(batchpartial::ParseRecord(bad).has_value());
    }
}

TEST_CASE("batch partial: ParseFile drops the torn tail and keeps the rest")
{
    const std::string head = batchpartial::FormatHeader(0x1122334455667788ULL, "Deck_H5_s8008_off25");
    const std::string r0 = batchpartial::FormatRecord(Make(25, 5, 11));
    const std::string r1 = batchpartial::FormatRecord(Make(26, -1, 12, false));
    const std::string r2 = batchpartial::FormatRecord(Make(27, 0, 13, true));

    SUBCASE("complete file")
    {
        const auto fc = batchpartial::ParseFile(head + r0 + r1 + r2);
        REQUIRE(fc.has_value());
        CHECK(fc->id == 0x1122334455667788ULL);
        CHECK(fc->job_name == "Deck_H5_s8008_off25");
        REQUIRE(fc->records.size() == 3);
        CHECK(fc->dropped == 0);
        CHECK(fc->records[2].abandoned);
    }
    SUBCASE("every truncation point of the last record")
    {
        for (std::size_t n = 0; n < r2.size(); ++n)
        {
            const auto fc = batchpartial::ParseFile(head + r0 + r1 + r2.substr(0, n));
            REQUIRE(fc.has_value());
            CHECK(fc->records.size() == 2);
            CHECK(fc->dropped == (n == 0 ? 0 : 1));
        }
    }
    SUBCASE("a zero-filled tail")
    {
        const auto fc = batchpartial::ParseFile(head + r0 + std::string(4096, '\0'));
        REQUIRE(fc.has_value());
        CHECK(fc->records.size() == 1);
        CHECK(fc->dropped == 1);
    }
    SUBCASE("a torn header holds nothing")
    {
        for (std::size_t n = 0; n < head.size(); ++n)
        { CHECK_FALSE(batchpartial::ParseFile(head.substr(0, n)).has_value()); }
    }
}

TEST_CASE("batch partial: the index folds identical duplicates and drops conflicts")
{
    batchpartial::Index idx;
    const uint64_t id = 42;
    idx.Add(id, Make(1, 5, 100), 0);
    idx.Add(id, Make(1, 5, 100), 1);   // re-persisted copy in a second file: fine
    idx.Add(id, Make(2, 5, 200), 0);
    Record other = Make(2, 6, 201);    // the same game, a different result: not deterministic
    idx.Add(id, other, 1);
    REQUIRE(idx.Find(id, 1, 7001) != nullptr);
    // The same game under ANOTHER ceiling is a separate entry, not a conflict.
    Record tight = Make(1, 5, 100);
    tight.limit = 4000;
    idx.Add(id, tight, 2);
    CHECK(idx.Conflicts() == 1);   // still only gi 2's
    const auto want_limit = [](long long l) { return [l](const Record& c) { return c.limit == l; }; };
    REQUIRE(idx.Find(id, 1, 7001, want_limit(4000)) != nullptr);
    CHECK(idx.Find(id, 1, 7001, want_limit(250000)) != nullptr);
    CHECK(idx.Find(id, 1, 7001, want_limit(1)) == nullptr);
    CHECK(idx.Find(id, 2, 7002) == nullptr);
    CHECK(idx.Conflicts() == 1);
    CHECK(idx.Find(id + 1, 1, 7001) == nullptr);   // another job identity
    CHECK(idx.Find(id, 1, 9999) == nullptr);       // same index, another seed
}

TEST_CASE("batch partial: a resumed job folds to the uninterrupted case digest")
{
    // An uninterrupted 24-game job...
    std::vector<uint64_t> all;
    for (int g = 0; g < 24; ++g) { all.push_back(0x9e3779b97f4a7c15ULL * static_cast<uint64_t>(g + 1)); }
    const uint64_t want = batchpartial::FoldCaseDigest(all);
    // ...killed with games {0,3,4,9,10,11,20} persisted (in completion order, through the file format)
    // and the rest replayed. The resumed job's slots are filled from both and folded in gi order.
    const std::vector<int> persisted = {11, 3, 0, 20, 9, 4, 10};
    std::string file = batchpartial::FormatHeader(1, "job");
    for (int g : persisted) { file += batchpartial::FormatRecord(Make(g, 5, all[static_cast<std::size_t>(g)])); }
    const auto fc = batchpartial::ParseFile(file);
    REQUIRE(fc.has_value());
    std::vector<uint64_t> slots(24, 0);
    std::vector<char> have(24, 0);
    for (const Record& r : fc->records) { slots[static_cast<std::size_t>(r.gi)] = r.digest; have[static_cast<std::size_t>(r.gi)] = 1; }
    int replayed = 0;
    for (int g = 0; g < 24; ++g)
    { if (!have[static_cast<std::size_t>(g)]) { slots[static_cast<std::size_t>(g)] = all[static_cast<std::size_t>(g)]; ++replayed; } }
    CHECK(replayed == 24 - static_cast<int>(persisted.size()));
    CHECK(batchpartial::FoldCaseDigest(slots) == want);
    // Order matters to the fold, so the gi-order reduction is what makes this hold.
    std::vector<uint64_t> shuffled = slots;
    std::swap(shuffled[0], shuffled[1]);
    CHECK(batchpartial::FoldCaseDigest(shuffled) != want);
}
