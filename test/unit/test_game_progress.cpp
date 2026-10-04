// Unit cover for IN-FLIGHT progress reporting (src/ai/GameProgress.h) -- the instrument that makes a
// still-running game predictable instead of opaque.
//
// The failure mode this guards is the one that motivated the file: on 2026-10-03 a keep rollout held
// a generation's phase barrier for over ten hours and NOTHING could say how far along it was, because
// every existing instrument (SlowTracker, the slow log, the keepgen monitor) reports a rollout only
// once it FINISHES. These cases fail if that observability regresses.
#include <doctest/doctest.h>
#include <sstream>
#include <thread>
#include "../../src/ai/GameProgress.h"

TEST_CASE("gameprogress: an in-flight unit is reported with its turn and search progress")
{
    std::ostringstream os;
    {
        gameprogress::Scope sc([]{ return "keep-rollout size7 draw r=5 seed=12345"; });
        gameprogress::NoteDecision(6);
        gameprogress::NoteEnumerate(49200000ULL);
        gameprogress::NoteDecision(7);          // turn advances; decisions accumulate

        // min_s = 0 => report regardless of age, which is what makes this deterministic (a test must
        // not wait out a real threshold).
        CHECK(gameprogress::Report(os, 0) == 1);
        const std::string out = os.str();
        CHECK(out.find("keep-rollout size7 draw r=5 seed=12345") != std::string::npos);
        CHECK(out.find("turn=7")        != std::string::npos);   // the LATEST turn, not the first
        CHECK(out.find("decisions=2")   != std::string::npos);
        CHECK(out.find("enum_calls=1")  != std::string::npos);
        CHECK(out.find("cur_odometer=49200000") != std::string::npos);  // the width denominator
    }
    // Leaving the scope must deregister: a finished unit is SlowTracker's business, not this one's.
    std::ostringstream after;
    CHECK(gameprogress::Report(after, 0) == 0);
}

TEST_CASE("gameprogress: publishers are no-ops outside a scope")
{
    // The hot path calls these unconditionally, so they must be safe (and silent) when the thread
    // owns no slot -- e.g. the live executor, the viewer, any non-generation play.
    gameprogress::NoteDecision(3);
    gameprogress::NoteEnumerate(1234);
    std::ostringstream os;
    CHECK(gameprogress::Report(os, 0) == 0);
    CHECK(os.str().empty());
}

TEST_CASE("gameprogress: slots are per-thread and do not cross-contaminate")
{
    // 24 workers each inside their own rollout is the real configuration; a shared slot would make
    // every report wrong in a way that reads as plausible.
    gameprogress::Scope outer([]{ return "unit-A"; });
    gameprogress::NoteDecision(2);

    std::thread other([]{
        gameprogress::Scope inner([]{ return "unit-B"; });
        gameprogress::NoteDecision(5);
        gameprogress::NoteEnumerate(77);
        std::ostringstream os;
        // Both units are live, so both are reported -- but with their OWN numbers.
        CHECK(gameprogress::Report(os, 0) == 2);
        const std::string out = os.str();
        CHECK(out.find("unit-A") != std::string::npos);
        CHECK(out.find("unit-B") != std::string::npos);
        CHECK(out.find("turn=5") != std::string::npos);
    });
    other.join();

    std::ostringstream os;
    CHECK(gameprogress::Report(os, 0) == 1);               // B deregistered with its thread
    CHECK(os.str().find("unit-A") != std::string::npos);
    CHECK(os.str().find("turn=2") != std::string::npos);    // A's turn is untouched by B
}

TEST_CASE("gameprogress: the report interval cannot be weakened, only tightened")
{
    // Same lower-only discipline as MTG_KEEP_SLOW_MS, per docs/design/keepgen-no-off-switches.md: a
    // run must not be able to silence its own stuck-work reporting.
    CHECK(gameprogress::ReportS() <= 1800);
    CHECK(gameprogress::ReportS() > 0);
}

TEST_CASE("gameprogress: a unit younger than the threshold is not reported")
{
    gameprogress::Scope sc([]{ return "fresh-unit"; });
    std::ostringstream os;
    CHECK(gameprogress::Report(os, 3600) == 0);   // just started; nothing to say
    CHECK(os.str().empty());
}
