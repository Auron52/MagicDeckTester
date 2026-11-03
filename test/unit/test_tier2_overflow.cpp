// Tier-2 overflow store (core/Tier2Overflow.h): byte budget, replacement, take/clear accounting, and the
// shared-budget invariant under concurrent per-thread stores.
#include <doctest/doctest.h>

#include <cstdint>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>
#include "core/Tier2Overflow.h"

namespace
{
using Map = std::unordered_map<std::uint64_t, std::string>;
long long StrBytes(const std::string& s) { return static_cast<long long>(s.size()); }
using Store = tier2::Overflow<Map, &StrBytes>;
}

TEST_CASE("tier2 overflow: admits within the budget and refuses beyond it")
{
    tier2::Pool pool(100);
    Store st(tier2::kMana, &pool);
    Map t1;
    t1[1] = std::string(40, 'a');
    t1[2] = std::string(40, 'b');
    t1[3] = std::string(40, 'c');
    st.SpillAll(t1);
    CHECK(t1.empty());                                 // tier 1 is empty after a spill, whatever was admitted
    CHECK(st.Size() == 2);                             // 80 of 100 bytes; the third would make 120
    CHECK(pool.used.load() == 80);
    CHECK(pool.cat[tier2::kMana].stores.load() == 2);
    CHECK(pool.cat[tier2::kMana].refused.load() == 1);
    CHECK(pool.peak.load() == 80);
}

TEST_CASE("tier2 overflow: a store under an existing key replaces it and releases its bytes")
{
    tier2::Pool pool(1000);
    Store st(tier2::kMana, &pool);
    Map t1;
    t1[7] = std::string(30, 'x');
    st.SpillAll(t1);
    t1[7] = std::string(10, 'y');
    st.SpillAll(t1);
    CHECK(st.Size() == 1);
    CHECK(pool.used.load() == 10);
    auto it = st.Find(7);
    REQUIRE(it != st.End());
    CHECK(it->second == std::string(10, 'y'));
}

TEST_CASE("tier2 overflow: take hands the node back and returns its bytes; clear empties the rest")
{
    tier2::Pool pool(0);                               // 0 = unbounded
    Store st(tier2::kMana, &pool);
    Map t1;
    for (std::uint64_t k = 0; k < 10; ++k) { t1[k] = std::string(static_cast<std::size_t>(k + 1), 'z'); }
    st.SpillAll(t1);
    CHECK(pool.used.load() == 55);
    auto it = st.Find(4);
    REQUIRE(it != st.End());
    st.NoteHit();
    Map::node_type nh = st.Take(it);
    CHECK(nh.mapped().size() == 5);
    CHECK(pool.used.load() == 50);
    t1.insert(std::move(nh));                          // promoted back into tier 1
    CHECK(t1.count(4) == 1);
    CHECK(st.Find(4) == st.End());
    st.Clear();
    CHECK(st.Size() == 0);
    CHECK(pool.used.load() == 0);
    CHECK(pool.cat[tier2::kMana].bytes.load() == 0);
    CHECK(pool.cat[tier2::kMana].dropped.load() == 9);
    CHECK(pool.cat[tier2::kMana].hits.load() == 1);
    CHECK(pool.cat[tier2::kMana].lookups.load() == 2);
}

TEST_CASE("tier2 overflow: the destructor returns a thread's bytes to the shared pool")
{
    tier2::Pool pool(1000);
    {
        Store st(tier2::kMana, &pool);
        Map t1;
        t1[1] = std::string(64, 'q');
        st.SpillAll(t1);
        CHECK(pool.used.load() == 64);
    }
    CHECK(pool.used.load() == 0);
}

TEST_CASE("tier2 overflow: concurrent per-thread stores never exceed the shared budget")
{
    const long long budget = 64 * 1024;
    tier2::Pool pool(budget);
    std::vector<std::thread> th;
    std::atomic<bool> over{false};
    for (int t = 0; t < 8; ++t)
    {
        th.emplace_back([&pool, &over, t, budget]{
            Store st(tier2::kMana, &pool);
            for (int round = 0; round < 50; ++round)
            {
                Map t1;
                for (int i = 0; i < 200; ++i)
                { t1[static_cast<std::uint64_t>(t) << 32 | static_cast<std::uint64_t>(round * 1000 + i)] = std::string(37, 'w'); }
                st.SpillAll(t1);
                if (pool.used.load() > budget) { over = true; }
                if (round % 7 == 6) { st.Clear(); }
            }
        });
    }
    for (std::thread& x : th) { x.join(); }
    CHECK_FALSE(over.load());
    CHECK(pool.peak.load() <= budget);
    CHECK(pool.used.load() == 0);                      // every thread's store was destroyed
    CHECK(pool.cat[tier2::kMana].refused.load() > 0);  // the budget did bind
}
