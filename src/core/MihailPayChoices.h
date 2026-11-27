#pragma once
#include "Card.h"   // ManaCost (the playout pay-draw reserve)
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// Trigger order is significant: payment can change mana before the next copy resolves.
// An unpinned choice is distinct from an explicitly pinned decline-all sequence.
struct MihailPayChoices
{
    bool pinned = false;
    // THE BEYOND-HORIZON PAY-DRAW POLICY (Leaf-Crowned Visionary's cast_subtype_pay_draw_cost; USER 2026-11-27:
    // "Leaf crowned-visionary should probably not decline the draw automatically even beyond the search horizon",
    // "If you don't have anything else to use the mana for it should draw."). Set ONLY on a plan the greedy
    // Solve() returned (a GreedyPermit site: the horizon playout / the depth-0 runner), never inside the search
    // window, where the trigger stays a searched pin (unpinned = decline, the pinned variants = pay). With it set,
    // an UNPINNED pay-draw trigger pays when the mana is spare (PayDrawPlayoutSpare, SpellEffects.h): the rest of
    // the applying line (g_line_unpaid_cost) plus the reserve (the plan's non-cast spends: activations) and,
    // before combat, the combat pump count stay payable. Mihail's token payment never reads it.
    // PACKED into the padding before `bits` (sizeof(Plan) / sizeof(GameState) are static-asserted and copied on
    // every node): one flag byte and a saturating per-field reserve (generic, {G}, {C} a byte each; W/U and B/R a
    // nibble each -- saturation can only under-reserve a colour past 15 pips of plan activations).
    std::uint8_t spare_flags = 0;   // bit 0: pay_spare; bit 1: pre-combat
    std::uint8_t res_generic = 0, res_green = 0, res_colorless = 0, res_wu = 0, res_br = 0;

    bool PaySpare() const { return (spare_flags & 1u) != 0; }
    bool SparePreCombat() const { return (spare_flags & 2u) != 0; }
    void SetPaySpare(bool pre_combat, const ManaCost& r)
    {
        auto b8 = [](int v) { return static_cast<std::uint8_t>(v < 0 ? 0 : (v > 255 ? 255 : v)); };
        auto n4 = [](int v) { return static_cast<std::uint8_t>(v < 0 ? 0 : (v > 15 ? 15 : v)); };
        spare_flags   = static_cast<std::uint8_t>(1u | (pre_combat ? 2u : 0u));
        res_generic   = b8(r.generic);
        res_green     = b8(r.green);
        res_colorless = b8(r.colorless);
        res_wu        = static_cast<std::uint8_t>(n4(r.white) | (n4(r.blue) << 4));
        res_br        = static_cast<std::uint8_t>(n4(r.black) | (n4(r.red) << 4));
    }
    ManaCost SpareReserve() const
    {
        ManaCost r;
        r.generic = res_generic; r.green = res_green; r.colorless = res_colorless;
        r.white = res_wu & 15; r.blue = res_wu >> 4; r.black = res_br & 15; r.red = res_br >> 4;
        return r;
    }
    std::uint64_t SpareWord() const
    {
        return static_cast<std::uint64_t>(spare_flags) | (static_cast<std::uint64_t>(res_generic) << 8)
             | (static_cast<std::uint64_t>(res_green) << 16) | (static_cast<std::uint64_t>(res_colorless) << 24)
             | (static_cast<std::uint64_t>(res_wu) << 32) | (static_cast<std::uint64_t>(res_br) << 40);
    }
    std::vector<std::uint8_t> bits;

    bool IsPinned() const { return pinned; }
    bool Paid(std::size_t ordinal) const
    { return pinned && ordinal < bits.size() && bits[ordinal] != 0; }
    void Clear()
    {
        pinned = false; bits.clear();
        spare_flags = 0; res_generic = res_green = res_colorless = res_wu = res_br = 0;
    }
    void Pin(std::size_t count) { pinned = true; bits.assign(count, 0); }

    // Binary successor, least significant choice first. Returns false after the last choice.
    bool Next()
    {
        for (std::uint8_t& bit : bits)
        {
            bit ^= 1;
            if (bit) { return true; }
        }
        return false;
    }

    template <typename Fold> void FoldValues(Fold&& fold) const
    {
        fold(pinned ? 1ull : 0ull);
        if (PaySpare()) { fold(0x5A4ED0A000000000ull ^ SpareWord()); }   // only a playout plan sets it
        if (!pinned) { return; }
        fold(static_cast<std::uint64_t>(bits.size()));
        for (std::uint8_t bit : bits) { fold(static_cast<std::uint64_t>(bit)); }
    }
    std::string Label() const
    {
        std::string result;
        result.reserve(bits.size());
        for (std::uint8_t bit : bits) { result.push_back(bit ? '1' : '0'); }
        return result;
    }
    bool operator==(const MihailPayChoices& other) const
    {
        if (SpareWord() != other.SpareWord()) { return false; }
        return pinned == other.pinned && (!pinned || bits == other.bits);
    }
    bool operator!=(const MihailPayChoices& other) const { return !(*this == other); }
};
