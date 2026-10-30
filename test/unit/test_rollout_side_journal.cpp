// Unit cover for the keep generator's ROLLOUT SIDE JOURNAL (src/analyzer/RolloutSideJournal.h): the
// record format, the torn-tail rule, compaction, failure-free reopen. The end-to-end property -- a run
// killed mid-speculation and resumed lands on the uninterrupted run's raw, playing fewer games -- is
// test/lib/keepgen_specjournal_check.sh; these pin the pieces that property rests on, in milliseconds.
//
// What each case guards (every one fails if the corresponding rule is removed):
//   * exact round trip  -- a reloaded value must be the SAME double, or a resume is not byte-identical;
//   * torn tail         -- a record the kill cut must be dropped, not parsed as a shorter number;
//   * strict parse      -- a malformed line is skipped whole, never half-applied;
//   * compaction        -- only what the keep-test still wants survives, the header is preserved, and
//                          appends after a compaction land in the new file.
#include <doctest/doctest.h>
#include <filesystem>
#include <fstream>
#include <limits>
#include <atomic>
#include <map>
#include <set>
#include <thread>
#include "../../src/analyzer/RolloutSideJournal.h"

namespace
{
std::string TmpPath(const std::string& name)
{
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "mtg_rollj_test";
    std::filesystem::create_directories(dir);
    const std::filesystem::path p = dir / name;
    std::error_code ec;
    std::filesystem::remove(p, ec);
    return p.string();
}

std::vector<rollj::Rec> ReadAll(const std::string& path, rollj::ReadStats* st = nullptr)
{
    std::vector<rollj::Rec> out;
    const rollj::ReadStats s = rollj::ReadFile(path, [&](const rollj::Rec& x) { out.push_back(x); });
    if (st) { *st = s; }
    return out;
}

rollj::Rec R(long long w, int pd, long long r, double v, long long us = 7)
{
    rollj::Rec x; x.w = w; x.pd = pd; x.r = r; x.v = v; x.us = us; return x;
}
}  // namespace

TEST_CASE("side journal: a record round-trips to the identical double, NaN and hits included")
{
    const std::vector<double> vals = { 5.0, 30.0, 4.25, 0.1, 1.0 / 3.0, 7.000000000000001,
                                       std::numeric_limits<double>::denorm_min(), 1e300 };
    for (const double v : vals)
    {
        std::string line;
        rollj::Format(line, R(123456, 1, 39, v, 98765));
        REQUIRE(line.back() == '\n');
        rollj::Rec y;
        REQUIRE(rollj::Parse(line.data(), line.data() + line.size() - 1, y));
        CHECK(y.w == 123456); CHECK(y.pd == 1); CHECK(y.r == 39); CHECK(y.us == 98765);
        CHECK(std::memcmp(&y.v, &v, sizeof v) == 0);   // bit-identical, not merely ==
        CHECK_FALSE(y.has_hits);
    }
    rollj::Rec n = R(1, 0, 2, std::numeric_limits<double>::quiet_NaN());
    n.has_hits = true; n.hits = { 0, 5, 17 };
    std::string line;
    rollj::Format(line, n);
    rollj::Rec y;
    REQUIRE(rollj::Parse(line.data(), line.data() + line.size() - 1, y));
    CHECK(std::isnan(y.v));
    CHECK(y.has_hits);
    CHECK(y.hits == std::vector<int>{ 0, 5, 17 });
    // A traced rollout that touched nothing is still "traced" (empty set), not "untraced".
    rollj::Rec e = R(1, 0, 3, 6.0); e.has_hits = true;
    line.clear(); rollj::Format(line, e);
    REQUIRE(rollj::Parse(line.data(), line.data() + line.size() - 1, y));
    CHECK(y.has_hits); CHECK(y.hits.empty());
}

TEST_CASE("side journal: malformed lines are rejected whole")
{
    rollj::Rec y;
    for (const std::string bad : { "", "1 0 2", "1 2 3 4 5", "-1 0 2 5 1", "1 0 -2 5 1", "1 0 2 x 1",
                                   "1 0 2 5", "1 0 2 5 1 junk", "1 0 2 inf 1", "1 0 8388608 5 1",
                                   "1 0 2 5 1 h1,,2", "1 0 2 5 1 h-3" })
    {
        CHECK_MESSAGE(!rollj::Parse(bad.data(), bad.data() + bad.size(), y), "accepted: '" << bad << "'");
    }
}

TEST_CASE("side journal: a torn last record is dropped, blank and garbage lines skipped")
{
    const std::string p = TmpPath("torn.spec");
    {
        std::ofstream f(p, std::ios::binary);
        f << "{\"specjournal\":{}}\n";
        std::string s;
        rollj::Format(s, R(1, 0, 0, 5.0));
        rollj::Format(s, R(2, 1, 3, 6.0));
        f << s << "\n" << "garbage line\n";   // a recovery newline and an unparsable line
        rollj::Format(s = std::string(), R(3, 0, 4, 7.0));
        f << s;
        f << "4 1 5 123";                       // CUT mid-value: "1234.5" would have been a different number
    }
    rollj::ReadStats st;
    const auto recs = ReadAll(p, &st);
    CHECK(st.header_ok);
    CHECK(st.header == "{\"specjournal\":{}}");
    CHECK(st.torn_tail);
    CHECK(st.bad == 1);
    REQUIRE(recs.size() == 3);
    CHECK(recs[2].w == 3);
}

TEST_CASE("side journal: a file with no complete header yields nothing")
{
    const std::string p = TmpPath("nohdr.spec");
    { std::ofstream f(p, std::ios::binary); f << "{\"specjournal\":"; }
    rollj::ReadStats st;
    CHECK(ReadAll(p, &st).empty());
    CHECK_FALSE(st.header_ok);
}

TEST_CASE("side journal: every Append is in the file when it returns (a process kill loses nothing)")
{
    const std::string p = TmpPath("immediate.spec");
    rollj::Writer::Opts o;
    o.path = p; o.header = "{\"specjournal\":{\"format\":1}}"; o.sync_ms = 60000; o.backup_s = 0;
    o.compact_bytes = 1LL << 40; o.quiet = true;
    rollj::Writer w(o, [](const rollj::Rec&) { return true; });
    std::string err;
    REQUIRE(w.Open({}, &err));
    for (int r = 0; r < 20; ++r)
    {
        w.Append(R(5, 0, r, 4.0 + r));
        // No flush, no sync, no wait: what a SIGKILL right now would leave is exactly the file.
        rollj::ReadStats st;
        const auto recs = ReadAll(p, &st);
        REQUIRE(recs.size() == static_cast<std::size_t>(r + 1));
        CHECK_FALSE(st.torn_tail);
        CHECK(recs.back().r == r);
    }
    w.Close(false);
}

TEST_CASE("side journal: writer carries the initial frontier, appends, and compacts to what keep wants")
{
    const std::string p = TmpPath("writer.spec");
    // keep-test stands in for rj_jdone: a cell-side "commits" up to covered[w].
    std::map<long long, long long> covered;
    std::mutex cm;
    auto keep = [&](const rollj::Rec& x)
    { std::lock_guard<std::mutex> lk(cm); auto it = covered.find(x.w); return it == covered.end() || x.r >= it->second; };
    std::atomic<int> syncs{ 0 };
    rollj::Writer::Opts o;
    o.path = p; o.header = "{\"specjournal\":{\"format\":1}}"; o.sync_ms = 5; o.backup_s = 0;
    o.compact_bytes = 1LL << 40; o.quiet = true;
    {
        rollj::Writer w(o, keep, [&] { ++syncs; });
        std::string err;
        REQUIRE(w.Open({ R(9, 0, 1, 4.0), R(9, 0, 2, 5.0) }, &err));
        for (int r = 0; r < 50; ++r) { w.Append(R(1, r & 1, r, 3.0 + r)); }
        {
            rollj::ReadStats st;
            const auto recs = ReadAll(p, &st);
            CHECK(st.header == o.header);
            CHECK(recs.size() == 52);
            CHECK_FALSE(st.torn_tail);
        }
        for (int k = 0; k < 400 && syncs.load() == 0; ++k) { std::this_thread::sleep_for(std::chrono::milliseconds(5)); }
        CHECK(syncs.load() > 0);   // the on_sync hook (the generator's main-journal fsync) runs
        // Commit w=1 below r=40 and w=9 entirely; compaction must keep exactly w=1, r>=40.
        { std::lock_guard<std::mutex> lk(cm); covered[1] = 40; covered[9] = std::numeric_limits<long long>::max(); }
        w.ForceCompactNext();
        for (int k = 0; k < 400 && w.Compactions() == 0; ++k) { std::this_thread::sleep_for(std::chrono::milliseconds(5)); }
        REQUIRE(w.Compactions() == 1);
        w.Append(R(2, 0, 0, 8.0));   // lands AFTER the rename, in the compacted file
        w.Close(false);
    }
    rollj::ReadStats st;
    const auto recs = ReadAll(p, &st);
    CHECK(st.header == o.header);
    REQUIRE(recs.size() == 11);
    for (std::size_t i = 0; i + 1 < recs.size(); ++i) { CHECK(recs[i].w == 1); CHECK(recs[i].r >= 40); }
    CHECK(recs.back().w == 2);
    CHECK_FALSE(std::filesystem::exists(p + ".tmp"));
}

TEST_CASE("side journal: appends racing a compaction are all kept")
{
    const std::string p = TmpPath("race.spec");
    rollj::Writer::Opts o;
    o.path = p; o.header = "{\"specjournal\":{}}"; o.sync_ms = 1; o.backup_s = 0;
    o.compact_bytes = 2048; o.quiet = true;   // compacts over and over while the writers run
    std::atomic<long long> floor{ 0 };
    rollj::Writer w(o, [&](const rollj::Rec& x) { return x.r >= floor.load(); });
    std::string err;
    REQUIRE(w.Open({}, &err));
    std::vector<std::thread> th;
    for (int t = 0; t < 4; ++t)
    { th.emplace_back([&w, t] { for (int r = 0; r < 3000; ++r) { w.Append(R(t, 0, r, 1.0 * r)); } }); }
    for (auto& x : th) { x.join(); }
    floor.store(1000);   // everything below r=1000 is now "committed"
    w.ForceCompactNext();
    for (int k = 0; k < 400 && w.Compactions() < 2; ++k) { std::this_thread::sleep_for(std::chrono::milliseconds(5)); }
    w.Close(false);
    std::map<long long, std::set<long long>> got;
    for (const auto& x : ReadAll(p)) { got[x.w].insert(x.r); }
    for (int t = 0; t < 4; ++t)
    {
        // nothing at or above the floor may be missing, whatever the compactions interleaved with
        for (long long r = 1000; r < 3000; ++r) { REQUIRE_MESSAGE(got[t].count(r) == 1, "lost w=" << t << " r=" << r); }
    }
}

TEST_CASE("side journal: rename-aside never destroys the file")
{
    const std::string p = TmpPath("aside.spec");
    { std::ofstream f(p, std::ios::binary); f << "hdr\n1 0 0 5 1\n"; }
    const std::string a = rollj::RenameAside(p);
    REQUIRE_FALSE(a.empty());
    CHECK_FALSE(std::filesystem::exists(p));
    CHECK(std::filesystem::file_size(a) == 14);   // "hdr\n" + "1 0 0 5 1\n"
    std::error_code ec;
    std::filesystem::remove(a, ec);
}
