// Unit cover for the per-game WORK METER and its two-stage WALL-CLOCK BACKSTOP
// (src/ai/GameWorkMeter.h; design + measurements in docs/design/per-game-wall-clock-backstop.md).
//
// WHY THIS IS WORTH LOCKING DOWN. The backstop is the only bound that reaches a game whose unit
// ceiling has stopped working -- including a cell whose ceiling is DISARMED outright (the matrix
// driver's skip_capped) -- and both of its failure directions are silent:
//
//   * ARMED WHEN IT SHOULD NOT BE. Every game on every path calls Add(); if a deadline leaks into a
//     game that never asked for one, games are abandoned in a run whose manifest says nothing about
//     wall clock, and the result reads as data. The default-off cases below are the guard.
//   * NOT FIRING WHEN IT SHOULD. That is the status quo ante -- a 4.26 h game producing nothing --
//     and it is invisible until a run fails to terminate.
//
// The elapsed-time cases use deliberately tiny bounds (tens of ms) so the whole file runs in well
// under a second. They assert only what is robust to a loaded box: a bound that HAS been passed
// fires, and one that has NOT been reached does not, with a wide margin between the two.
#include <doctest/doctest.h>
#include <chrono>
#include <thread>
#include "../../src/ai/GameWorkMeter.h"

namespace
{
// The clock is only read on a unit stride, so a test must actually deliver units to reach it.
constexpr long long kStride = gamework::kClockStride;

void SleepMs(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }
}  // namespace

TEST_CASE("work meter: disarmed by default -- no ceiling, no deadline, no clock")
{
    gamework::Begin(0);
    for (int i = 0; i < 10; ++i) { gamework::Add(kStride); }
    SleepMs(20);
    gamework::Add(kStride);
    CHECK_FALSE(gamework::Abandoned());
    CHECK(gamework::AbandonCause() == gamework::Cause::kNone);
    CHECK(gamework::Used() == kStride * 11);
    gamework::End();
}

TEST_CASE("work meter: the UNIT ceiling still governs, and reports itself as UNITS")
{
    gamework::Begin(100);
    gamework::Add(99);
    CHECK_FALSE(gamework::Abandoned());
    gamework::Add(1);
    CHECK(gamework::Abandoned());
    CHECK(gamework::AbandonCause() == gamework::Cause::kUnits);
    gamework::End();
}

TEST_CASE("work meter: a unit ceiling alone never reads the clock, however long the game runs")
{
    // The status quo ante, and the reason the backstop exists: this game is 1000x over any sane
    // wall bound and the meter is completely indifferent to it.
    gamework::Begin(1000000);
    SleepMs(30);
    gamework::Add(kStride);
    CHECK_FALSE(gamework::Abandoned());
    gamework::End();
}

TEST_CASE("work meter: STAGE 2 -- the hard cap abandons a game that outruns it")
{
    gamework::Begin(0);                      // no unit ceiling at all: stage 2 is the only bound
    gamework::ArmDeadline(0.0, 0.04);        // 40 ms hard cap
    gamework::Add(kStride);
    CHECK_FALSE(gamework::Abandoned());      // not yet past it
    SleepMs(80);
    gamework::Add(kStride);
    CHECK(gamework::Abandoned());
    CHECK(gamework::AbandonCause() == gamework::Cause::kWall);
    CHECK(gamework::Elapsed() > 0.0);
    gamework::End();
}

TEST_CASE("work meter: STAGE 2 bounds a game whose ceiling is DISARMED (the skip_capped case)")
{
    // Past --max-skip-frac the matrix driver zeroes abandon_units, abandon_k AND
    // abandon_floor_units together, so those cells have no bound in units at all. This is the
    // state Snow's matrix reaches, and the backstop is the only thing that reaches those games.
    gamework::Begin(0);
    gamework::ArmDeadline(0.02, 0.04);
    SleepMs(80);
    gamework::Add(kStride);
    CHECK(gamework::Abandoned());
    CHECK(gamework::AbandonCause() == gamework::Cause::kWall);
    gamework::End();
}

TEST_CASE("work meter: STAGE 1 -- a game far off the pace is cut at the predictive check")
{
    // 4096 units in >=20 ms against a 10^9 ceiling projects to ~4,000,000 s, which is astronomically
    // past 1.5 x the 10 s hard cap. This is the shape of the real pathology (a 40M ceiling at
    // 830 units/s projects to 13.4 h against a 3.5 h cap), just scaled down.
    gamework::Begin(1000000000LL);
    gamework::ArmDeadline(0.02, 10.0);
    SleepMs(40);
    gamework::Add(kStride);
    CHECK(gamework::Abandoned());
    CHECK(gamework::AbandonCause() == gamework::Cause::kPredict);
    gamework::End();
}

TEST_CASE("work meter: STAGE 1 spares a game that IS on pace -- the false-cut guard")
{
    // The one way stage 1 can be wrong is cutting a game that would have finished. Here the ceiling
    // is close enough that the projection lands well inside the hard cap, so the check must decline.
    gamework::Begin(kStride * 2);            // half done already when the check fires
    gamework::ArmDeadline(0.02, 10.0);
    SleepMs(40);
    gamework::Add(kStride);                  // projects to ~2 x elapsed, i.e. ~0.08 s << 10 s
    CHECK_FALSE(gamework::Abandoned());
    CHECK(gamework::AbandonCause() == gamework::Cause::kNone);
    gamework::End();
}

TEST_CASE("work meter: STAGE 1 is INERT without a unit ceiling -- there is nothing to extrapolate")
{
    // t_limit is the numerator of the projection. With no ceiling, stage 1 must decline rather than
    // invent a prediction -- otherwise it would silently become a second, shorter hard cap.
    gamework::Begin(0);
    gamework::ArmDeadline(0.02, 100.0);      // hard cap far away, predictive check long past
    SleepMs(40);
    gamework::Add(kStride);
    CHECK_FALSE(gamework::Abandoned());
    gamework::End();
}

TEST_CASE("work meter: STAGE 1 is evaluated ONCE, not on every stride")
{
    // A game spared by the check must not be re-judged every 4096 units: the rate it was spared on
    // is the rate it gets credit for until stage 2 has its say.
    gamework::Begin(kStride * 2);
    gamework::ArmDeadline(0.02, 10.0);
    SleepMs(40);
    gamework::Add(kStride);                  // the one evaluation -- spared
    REQUIRE_FALSE(gamework::Abandoned());
    // Now stall hard. A re-evaluation would see a collapsed rate and cut; it must not happen.
    for (int i = 0; i < 4; ++i) { SleepMs(20); gamework::Add(1); }
    CHECK_FALSE(gamework::Abandoned());
    gamework::End();
}

TEST_CASE("work meter: End() disarms the deadline so it cannot leak into the next game")
{
    // The meter is thread_local and a pooled worker thread runs game after game, so a deadline that
    // outlived its game would abandon an unrelated one. Begin() and End() must both clear it.
    gamework::Begin(0);
    gamework::ArmDeadline(0.0, 0.01);
    SleepMs(30);
    gamework::Add(kStride);
    REQUIRE(gamework::Abandoned());
    gamework::End();

    gamework::Begin(0);                      // the next game on this thread asks for no deadline
    SleepMs(30);
    for (int i = 0; i < 5; ++i) { gamework::Add(kStride); }
    CHECK_FALSE(gamework::Abandoned());
    CHECK(gamework::AbandonCause() == gamework::Cause::kNone);
    gamework::End();
}

TEST_CASE("work meter: Begin() alone clears a deadline left armed by a previous game")
{
    gamework::Begin(0);
    gamework::ArmDeadline(0.0, 0.01);
    SleepMs(30);
    gamework::Begin(0);                      // re-armed without an intervening End()
    gamework::Add(kStride);
    CHECK_FALSE(gamework::Abandoned());
    gamework::End();
}

TEST_CASE("work meter: cause names are distinguishable in the log")
{
    // The runner's ABANDONED / ABANDONED-WALL / ABANDONED-PREDICT tags are the only record that a
    // run was not reproducible from its data, so the names must never collapse together.
    CHECK(std::string(gamework::CauseName(gamework::Cause::kUnits))   == "UNITS");
    CHECK(std::string(gamework::CauseName(gamework::Cause::kWall))    == "WALL");
    CHECK(std::string(gamework::CauseName(gamework::Cause::kPredict)) == "PREDICT");
    CHECK(std::string(gamework::CauseName(gamework::Cause::kNone))    == "NONE");
}
