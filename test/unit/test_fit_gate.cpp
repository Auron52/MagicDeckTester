// value_play.fit_gate (P12 "ladder" / P14 "ladder_rem" prototypes): the sidecar key parses, maps to its code,
// resolves against the env defaults, and the climb's allowance reads the right budget quantity.
#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <system_error>

#include "ai/MulliganProfileIO.h"
#include "ai/ValueArm.h"

TEST_CASE("fit_gate: sidecar string -> code")
{
    CHECK(valuearm::FitGateCode("") == -1);
    CHECK(valuearm::FitGateCode("bogus") == -1);
    CHECK(valuearm::FitGateCode("fit") == 0);
    CHECK(valuearm::FitGateCode("ladder") == 1);
    CHECK(valuearm::FitGateCode("ladder_rem") == 2);
}

TEST_CASE("fit_gate: the deck code wins, else the env (REM before LADDER), else off")
{
    CHECK(valuearm::FitGateResolve(-1, false, false) == 0);
    CHECK(valuearm::FitGateResolve(-1, true, false) == 1);
    CHECK(valuearm::FitGateResolve(-1, false, true) == 2);
    CHECK(valuearm::FitGateResolve(-1, true, true) == 2);
    CHECK(valuearm::FitGateResolve(0, true, true) == 0);
    CHECK(valuearm::FitGateResolve(1, false, true) == 1);
    CHECK(valuearm::FitGateResolve(2, false, false) == 2);
}

TEST_CASE("fit_gate: the climb allowance -- ladder ignores the probe, ladder_rem is the true remaining")
{
    // 18,000-unit decision; the probe + calibration spent 11,000 and FIT's rollout passes 2,000: remaining 5,000.
    CHECK(valuearm::FitClimbAllowance(1, 18000.0, 2000.0, 5000.0) == doctest::Approx(16000.0));
    CHECK(valuearm::FitClimbAllowance(2, 18000.0, 2000.0, 5000.0) == doctest::Approx(5000.0));
    // Never negative.
    CHECK(valuearm::FitClimbAllowance(1, 18000.0, 25000.0, 0.0) == doctest::Approx(0.0));
    CHECK(valuearm::FitClimbAllowance(2, 18000.0, 2000.0, -10.0) == doctest::Approx(0.0));
}

TEST_CASE("fit_gate: the sidecar loader reads value_play.fit_gate (end to end)")
{
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "mdt_unit_fit_gate";
    fs::create_directories(dir);
    auto gate_of = [&](const char* json) -> std::string
    {
        { std::ofstream(dir / "fg.value.json") << json; }
        MulliganProfile prof;
        AttachValueSidecar(prof, dir / "fg.profile.json");
        return prof.value_play.fit_gate;
    };
    CHECK(gate_of(R"({"value_play":{"ladder":"single","leaf":"none","alpha":"relaxed"}})").empty());
    CHECK(gate_of(R"({"value_play":{"ladder":"single","leaf":"none","alpha":"relaxed","fit_gate":"ladder"}})") == "ladder");
    CHECK(gate_of(R"({"value_play":{"ladder":"single","leaf":"none","alpha":"relaxed","fit_gate":"ladder_rem"}})") == "ladder_rem");
    CHECK(valuearm::FitGateCode(gate_of(R"({"value_play":{"ladder":"single","fit_gate":"ladder_rem"}})")) == 2);
    std::error_code ec;
    fs::remove_all(dir, ec);
}
