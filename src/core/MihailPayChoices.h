#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// Trigger order is significant: payment can change mana before the next copy resolves.
// An unpinned choice is distinct from an explicitly pinned decline-all sequence.
struct MihailPayChoices
{
    bool pinned = false;
    std::vector<std::uint8_t> bits;

    bool IsPinned() const { return pinned; }
    bool Paid(std::size_t ordinal) const
    { return pinned && ordinal < bits.size() && bits[ordinal] != 0; }
    void Clear() { pinned = false; bits.clear(); }
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
    { return pinned == other.pinned && (!pinned || bits == other.bits); }
    bool operator!=(const MihailPayChoices& other) const { return !(*this == other); }
};
