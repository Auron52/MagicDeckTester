// Unit cover for the batch runner's per-game RESUME log (src/runner/BatchResume.h). End to end (a batch
// killed mid-run and re-run lands on the uninterrupted run's per-job lines and .wins files) is
// test/batch_resume_check.sh; these pin the record format and the torn-tail / append rules it rests on.
#include <doctest/doctest.h>
#include <climits>
#include <filesystem>
#include <fstream>
#include "../../src/runner/BatchResume.h"

namespace
{
std::string TmpPath(const std::string& name)
{
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "mtg_batchresume_test";
    std::filesystem::create_directories(dir);
    const std::filesystem::path p = dir / name;
    std::error_code ec;
    std::filesystem::remove(p, ec);
    return p.string();
}
batchresume::GameRec G(int job, int gi, int wt, const char* name)
{
    batchresume::GameRec r;
    r.job = job; r.gi = gi; r.wt = wt; r.dg = 0xfedcba9876543210ULL; r.units = 123456789012LL; r.ms = 4242;
    r.ab = (wt == INT_MIN); r.name = name;
    return r;
}
}  // namespace

TEST_CASE("batch resume: a record round-trips exactly, including an abandoned (kSkipped) game")
{
    for (const int wt : { 5, -1, INT_MIN })
    {
        std::string s;
        batchresume::Format(s, G(3, 17, wt, "s4004@old"));
        REQUIRE(s.back() == '\n');
        batchresume::GameRec r;
        REQUIRE(batchresume::Parse(s.substr(0, s.size() - 1), r));
        CHECK(r.job == 3); CHECK(r.gi == 17); CHECK(r.wt == wt);
        CHECK(r.dg == 0xfedcba9876543210ULL); CHECK(r.units == 123456789012LL); CHECK(r.ms == 4242);
        CHECK(r.ab == (wt == INT_MIN)); CHECK(r.name == "s4004@old");
    }
    batchresume::GameRec r;
    for (const std::string bad : { "", "1 2 3", "1 2 3 zz 4 5 0 n", "1 2 3 ff 4 5 2 n", "-1 2 3 ff 4 5 0 n",
                                   "1 2 3 ff 4 5 0 ", "1 2 3 ff 4 5 0 a b" })
    { CHECK_MESSAGE(!batchresume::Parse(bad, r), "accepted '" << bad << "'"); }
}

TEST_CASE("batch resume: header + every appended game is on disk at once; a torn tail is dropped and fenced")
{
    const std::string p = TmpPath("log.resume");
    const std::string key = batchresume::Fnv64Hex("some batch");
    {
        batchresume::Log L;
        REQUIRE(L.Open(p, key, /*fresh=*/true, false));
        for (int g = 0; g < 10; ++g)
        {
            L.Append(G(0, g, 4 + g, "job0"));
            const batchresume::Loaded now = batchresume::Load(p);   // no close, no flush: what a SIGKILL leaves
            REQUIRE(now.recs.size() == static_cast<std::size_t>(g + 1));
            CHECK_FALSE(now.torn);
        }
    }
    { std::ofstream f(p, std::ios::binary | std::ios::app); f << "0 10 7 ff"; }   // a cut record
    batchresume::Loaded L1 = batchresume::Load(p);
    CHECK(L1.header == batchresume::Header(key));
    CHECK(L1.torn);
    CHECK(L1.recs.size() == 10);
    {
        batchresume::Log L;
        REQUIRE(L.Open(p, key, /*fresh=*/false, /*torn_tail=*/true));   // must start a NEW line
        L.Append(G(1, 0, 6, "job1"));
    }
    batchresume::Loaded L2 = batchresume::Load(p);
    CHECK_FALSE(L2.torn);
    CHECK(L2.bad == 1);                 // the fenced-off fragment, as a line of its own
    REQUIRE(L2.recs.size() == 11);
    CHECK(L2.recs.back().name == "job1");
}
