// value_play.sampled_gate (P18 sampled-gate ladder): the sidecar key parses end to end, alone or beside other keys,
// and an absent key reads as 0 (off).
#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <system_error>

#include "ai/MulliganProfileIO.h"

TEST_CASE("sampled_gate: the sidecar loader reads value_play.sampled_gate (end to end)")
{
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "mdt_unit_sampled_gate";
    fs::create_directories(dir);
    auto from_of = [&](const char* json) -> int
    {
        { std::ofstream(dir / "rf.value.json") << json; }
        MulliganProfile prof;
        AttachValueSidecar(prof, dir / "rf.profile.json");
        return prof.value_play.sampled_gate;
    };
    CHECK(from_of(R"({"value_play":{"ladder":"single","leaf":"none"}})") == 0);
    CHECK(from_of(R"({"value_play":{"sampled_gate":1}})") == 1);   // a sidecar holding ONLY this key still parses
    CHECK(from_of(R"({"value_play":{"mull_gen_depth":1,"sampled_gate":1}})") == 1);
    CHECK(from_of(R"({"value_play":{"sampled_gate":2}})") == 2);   // calibrated fast shape
    std::error_code ec;
    fs::remove_all(dir, ec);
}

TEST_CASE("sparse heuristic ladder: value_play.ladder \"sparse\" parses and sets any learned model aside")
{
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "mdt_unit_sparse_ladder";
    fs::create_directories(dir);
    auto load = [&](const char* json) -> MulliganProfile
    {
        { std::ofstream(dir / "sp.value.json") << json; }
        MulliganProfile prof;
        AttachValueSidecar(prof, dir / "sp.profile.json");
        return prof;
    };
    // A minimal learned model: one coefficient makes EvalModelFromJsonObj non-empty.
    const MulliganProfile learned = load(R"({"coefs":{"intercept_dummy":0,"turn":1},"value_play":{}})");
    const MulliganProfile sparse  = load(R"({"coefs":{"intercept_dummy":0,"turn":1},"value_play":{"ladder":"sparse"}})");
    CHECK(sparse.value_play.ladder == "sparse");
    CHECK(sparse.value_model.empty());               // no model attached: the sampled gate acts (p18_on needs !vl_active)
    CHECK(!sparse.value_model.constant);
    CHECK(sparse.value_trust_depth == 0);
    CHECK(sparse.value_play.sampled_gate == 0);      // the mode is resolved per decision (AIEngine DeckShapeScope)
    const MulliganProfile none = load(R"({"value_play":{"ladder":"sparse","leaf":"none"}})");
    CHECK(none.value_model.constant);                // leaf none still wins: the constant no-leaf stand-in
    CHECK(!learned.value_model.empty());            // the fixture IS a learned model without "sparse"
    std::error_code ec;
    fs::remove_all(dir, ec);
}
