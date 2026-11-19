#pragma once
// COMPLETE BOARD IDENTITY (the mana oracle's shared outcome boards, 2026-11-19;
// docs/design/mana-oracle-board-memory-2026-11-19.md section 8).
//
// BoardsEqual is the COMPLETE equality of two GameStates: C++20 defaulted memberwise operator== on GameState and
// on every type it holds (Player, Permanent, Card, StackEntry, ...), so EVERY member is compared, and a member
// added later is compared too. A member whose type has no operator== deletes GameState's, and BoardsEqual then
// fails to compile, so completeness cannot lapse silently. Three types compare their LOGICAL value instead of
// their raw storage, each because a part of the storage is unreachable: Library (the drawn prefix below m_top --
// a copy drops it, no accessor reads it), CounterList and SubtypeSet (fixed slots past the live count, stale after
// an erase). Everything else, cache pointers included (Card::m_def), is compared as stored: such a member can only
// make two boards UNEQUAL, never equal, so it costs sharing, never exactness.
//
// BoardHash is a partial hash consistent with BoardsEqual: it reads only members BoardsEqual compares, through the
// same logical views, so equal boards hash equal. It is a bucket key, never a verdict (a collision falls through to
// BoardsEqual).
#include "core/GameState.h"

#include <cstdint>

namespace boardid
{
inline bool BoardsEqual(const GameState& a, const GameState& b) { return a == b; }

inline std::uint64_t Mix(std::uint64_t h, std::uint64_t v)
{
    h ^= v + 0x9E3779B97F4A7C15ULL + (h << 6) + (h >> 2);
    return h;
}

inline std::uint64_t BoardHash(const GameState& s)
{
    std::uint64_t h = 0xCBF29CE484222325ULL;
    h = Mix(h, static_cast<std::uint64_t>(s.turn_number));
    h = Mix(h, static_cast<std::uint64_t>(s.active_player_index));
    h = Mix(h, static_cast<std::uint64_t>(s.spells_cast_this_turn));
    const ManaPool& f = s.floating_mana;
    h = Mix(h, (static_cast<std::uint64_t>(f.white) << 0) ^ (static_cast<std::uint64_t>(f.blue) << 8)
                   ^ (static_cast<std::uint64_t>(f.black) << 16) ^ (static_cast<std::uint64_t>(f.red) << 24)
                   ^ (static_cast<std::uint64_t>(f.green) << 32) ^ (static_cast<std::uint64_t>(f.colorless) << 40)
                   ^ (static_cast<std::uint64_t>(f.wild) << 48));
    h = Mix(h, s.battlefield.size());
    for (const Permanent& p : s.battlefield)
    {
        h = Mix(h, (static_cast<std::uint64_t>(static_cast<std::uint32_t>(p.card.m_number)) << 8)
                       ^ (p.tapped ? 1u : 0u) ^ (static_cast<std::uint64_t>(p.controller_index) << 2)
                       ^ (static_cast<std::uint64_t>(p.counters.size()) << 4));
        for (const Counter& c : p.counters)
        { h = Mix(h, (static_cast<std::uint64_t>(c.type) << 32) ^ static_cast<std::uint32_t>(c.count)); }
    }
    h = Mix(h, s.stack.size());
    h = Mix(h, s.exile.size());
    for (const Card& c : s.exile) { h = Mix(h, static_cast<std::uint32_t>(c.m_number)); }
    for (const Player& pl : s.players)
    {
        h = Mix(h, static_cast<std::uint32_t>(pl.life));
        h = Mix(h, pl.hand.size());
        for (const Card& c : pl.hand) { h = Mix(h, static_cast<std::uint32_t>(c.m_number)); }
        h = Mix(h, pl.graveyard.size());
        for (const Card& c : pl.graveyard) { h = Mix(h, static_cast<std::uint32_t>(c.m_number)); }
        h = Mix(h, pl.library.size());
        for (std::size_t i = 0; i < pl.library.size(); ++i)
        { h = Mix(h, static_cast<std::uint32_t>(pl.library.RawAt(i).m_number)); }
    }
    return h;
}
}  // namespace boardid
