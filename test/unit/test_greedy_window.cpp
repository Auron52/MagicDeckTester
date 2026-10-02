// The in-window greedy tripwire (greedywindow, TurnSolver.h) is FATAL by design, so the only way to
// prove it can fire without killing the test process is through its pure predicate. Every case below
// pairs an arm that MUST trip with one that must not -- a gate whose positive arm has never been seen
// is indistinguishable from a gate that checks nothing.
#include <doctest/doctest.h>

#include "ai/TurnSolver.h"

TEST_CASE("greedy window: no search frame and no claimed depth is the d0 play path")
{
    REQUIRE(greedywindow::t_depth == greedywindow::kNoSearch);
    CHECK_FALSE(greedywindow::InWindow(0));
    CHECK(greedywindow::InWindow(1));            // a call site claiming depth left trips on its own
}

TEST_CASE("greedy window: the innermost frame decides, whatever the caller claims")
{
    {
        const greedywindow::Frame outer(5);
        CHECK(greedywindow::InWindow(0));        // mislabelled call site: claims 0, frame says 5
        {
            const greedywindow::Frame leaf(0);   // the horizon playout inside a depth-5 search
            CHECK_FALSE(greedywindow::InWindow(0));
            CHECK(greedywindow::InWindow(2));    // ...but a positive claim still trips
        }
        CHECK(greedywindow::InWindow(0));        // leaving the leaf restores the outer frame
    }
    CHECK(greedywindow::t_depth == greedywindow::kNoSearch);
    CHECK_FALSE(greedywindow::InWindow(0));
}
