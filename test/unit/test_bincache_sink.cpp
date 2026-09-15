// Unit cover for the exhaustive-keep BINARY CACHE writer (src/ai/MulliganProfileIO.h).
//
// The cache is written two ways -- into a std::string (SerializeExhaustiveKeep) and straight to a
// file with a bounded buffer (WriteExhaustiveKeepBinCache) -- and the file path exists ONLY to keep
// the transient off the heap: FiveColour's cache is 3.59 GB, which on a 10.7 GB box pushed the
// one-time build to 8.7 GB and tripped the RSS watchdog at 8.02 GB.
//
// The risk that buys is a silent format drift between the two emitters. That failure is invisible in
// play: a cache whose bytes do not match what Deserialize expects is REJECTED, so the deck quietly
// falls back to re-parsing 1.8 GB of JSON every launch and the only symptom is slowness. So these
// cases assert the two sinks are byte-for-byte identical, that the bytes round-trip back to an equal
// policy, and that the FileSink's buffer boundary is not where it goes wrong -- the payload below is
// deliberately far larger than the 8 MB buffer, so the flush path is exercised many times.
#include <doctest/doctest.h>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include "../../src/ai/MulliganProfileIO.h"

namespace
{
ExhaustiveKeepPolicy MakePolicy(int cells)
{
    ExhaustiveKeepPolicy ek;
    ek.buckets = { {"Misty Rainforest", "Scalding Tarn"}, {"Birds of Paradise"}, {"Island"} };
    ek.max_mull = 6;
    ek.bottoming_enabled = true;
    ek.commit = "2f7822a2";
    ek.play_digest = "b79a141457869ca5";
    ek.effective_R = 30;
    for (int i = 0; i < cells; ++i)
    {
        // DISTINCT keys: these land in a std::map, so a generator that collides silently shrinks the
        // fixture to the number of unique compositions -- which is how the first version of this test
        // produced a 5.8 KB blob while claiming to exercise the 8 MB buffer boundary.
        const std::vector<int> comp{ i % 32, (i / 32) % 32, (i / 1024) % 32, (i / 32768) % 32 };
        ek.keep[comp] = std::vector<char>(static_cast<std::size_t>(1 + (i % 7)), static_cast<char>('a' + (i % 26)));
        ek.bottom_keep[comp] = { { i, i + 1, i + 2 }, { i + 3, i + 4, i + 5 } };
    }
    return ek;
}

std::string ReadFile(const std::filesystem::path& p)
{
    std::ifstream f(p, std::ios::binary | std::ios::ate);
    const std::streamoff n = f.tellg();
    std::string s(static_cast<std::size_t>(n), '\0');
    f.seekg(0);
    f.read(&s[0], n);
    return s;
}
}  // namespace

TEST_CASE("bincache: the file sink emits byte-identical output to the string sink")
{
    // ~300k cells puts the blob well past the 8 MB FileSink buffer, so this covers the flush path
    // and any boundary where a record straddles two buffers -- the one thing a small fixture would
    // miss, and exactly where a hand-rolled second emitter would have drifted.
    const ExhaustiveKeepPolicy ek = MakePolicy(300000);
    const uint64_t sz = 1784066360ull, mt = 1789440199073280910ull;

    const std::string in_memory = SerializeExhaustiveKeep(ek, sz, mt);
    CHECK(in_memory.size() > (8u << 20));   // the point of the test is the multi-flush case

    const std::filesystem::path tmp =
        std::filesystem::temp_directory_path() / "mtg_bincache_sink_test.bin";
    std::filesystem::remove(tmp);
    REQUIRE(WriteExhaustiveKeepBinCache(tmp, ek, sz, mt));

    const std::string streamed = ReadFile(tmp);
    CHECK(streamed.size() == in_memory.size());
    CHECK(streamed == in_memory);
    std::filesystem::remove(tmp);
}

TEST_CASE("bincache: streamed bytes round-trip through Deserialize")
{
    const ExhaustiveKeepPolicy ek = MakePolicy(1000);
    const uint64_t sz = 4242ull, mt = 99ull;

    const std::filesystem::path tmp =
        std::filesystem::temp_directory_path() / "mtg_bincache_roundtrip_test.bin";
    std::filesystem::remove(tmp);
    REQUIRE(WriteExhaustiveKeepBinCache(tmp, ek, sz, mt));

    ExhaustiveKeepPolicy back;
    REQUIRE(DeserializeExhaustiveKeep(ReadFile(tmp), sz, mt, back));
    CHECK(back.buckets == ek.buckets);
    CHECK(back.max_mull == ek.max_mull);
    CHECK(back.bottoming_enabled == ek.bottoming_enabled);
    CHECK(back.commit == ek.commit);
    CHECK(back.play_digest == ek.play_digest);
    CHECK(back.effective_R == ek.effective_R);
    CHECK(back.keep.size() == ek.keep.size());
    CHECK(back.bottom_keep.size() == ek.bottom_keep.size());
    for (const auto& kv : ek.bottom_keep)
    {
        auto it = back.bottom_keep.find(kv.first);
        REQUIRE(it != back.bottom_keep.end());
        CHECK(it->second == kv.second);
    }

    // A source fingerprint that does not match must be refused -- that check is what makes a stale
    // cache harmless, and it has to keep working through the streamed writer.
    ExhaustiveKeepPolicy stale;
    CHECK_FALSE(DeserializeExhaustiveKeep(ReadFile(tmp), sz + 1, mt, stale));
    std::filesystem::remove(tmp);
}
