// value_play.rollout_from (P16 skip-leaves ladder): the sidecar key parses end to end, alone or beside other keys,
// and an absent key reads as 0 (off).
#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <system_error>

#include "ai/MulliganProfileIO.h"

TEST_CASE("rollout_from: the sidecar loader reads value_play.rollout_from (end to end)")
{
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "mdt_unit_rollout_from";
    fs::create_directories(dir);
    auto from_of = [&](const char* json) -> int
    {
        { std::ofstream(dir / "rf.value.json") << json; }
        MulliganProfile prof;
        AttachValueSidecar(prof, dir / "rf.profile.json");
        return prof.value_play.rollout_from;
    };
    CHECK(from_of(R"({"value_play":{"ladder":"single","leaf":"none"}})") == 0);
    CHECK(from_of(R"({"value_play":{"rollout_from":3}})") == 3);   // a sidecar holding ONLY this key still parses
    CHECK(from_of(R"({"value_play":{"mull_gen_depth":1,"rollout_from":2}})") == 2);
    std::error_code ec;
    fs::remove_all(dir, ec);
}
