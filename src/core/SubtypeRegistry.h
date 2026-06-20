#pragma once
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>
#include <cstdint>
#include <cstddef>

// Process-wide interning of subtype NAME strings ("Sliver", "Goblin", "Mountain", ...)
// to small integer ids, so a Card can store its subtypes as a few uint16_t (POD, no heap)
// instead of a std::vector<std::string>. That shrinks Card -- which is deep-copied once
// per search node -- cutting both the per-node copy (memcpy of a smaller struct) and the
// per-copy subtype heap allocation.
//
// Determinism / byte-identical: subtype ids are NEVER folded into BuildSimKey, the TT key,
// or any logged output -- subtypes only drive string-equality "is this a Sliver?" checks,
// which become id-equality checks here. Ids are assigned in first-seen order at LOAD time
// (single-threaded), so the same cards.json yields the same ids every run; results are
// independent of the id values regardless.
//
// Threading: Intern() (which may insert) runs only at load (single-threaded). Id() is a
// read-only lookup used on the search hot path across worker threads; it never inserts, so
// concurrent reads of the (load-frozen) map are safe. An unknown name -> id 0 (kNone),
// which no card stores, so it can never spuriously match.
class SubtypeRegistry
{
public:
    static constexpr uint16_t kNone = 0;

    static SubtypeRegistry& Instance()
    {
        static SubtypeRegistry inst;
        return inst;
    }

    // Load-time: intern a name, assigning a fresh id if unseen. Single-threaded.
    uint16_t Intern(const std::string& name)
    {
        if (name.empty()) { return kNone; }
        auto it = m_ids.find(name);
        if (it != m_ids.end()) { return it->second; }
        const uint16_t id = static_cast<uint16_t>(m_names.size()); // next id == current size
        m_ids.emplace(name, id);
        m_names.push_back(name);
        return id;
    }

    // Hot-path read-only lookup: returns kNone for an unknown/empty name (no insert).
    uint16_t Id(std::string_view name) const
    {
        if (name.empty()) { return kNone; }
        auto it = m_ids.find(std::string(name));
        return it == m_ids.end() ? kNone : it->second;
    }

    const std::string& Name(uint16_t id) const
    {
        static const std::string kEmpty;
        return id < m_names.size() ? m_names[id] : kEmpty;
    }

private:
    SubtypeRegistry() { m_names.emplace_back(); } // index 0 == kNone == "" (the empty/none name)

    std::vector<std::string>                  m_names; // id -> name (index 0 = none)
    std::unordered_map<std::string, uint16_t> m_ids;   // name -> id
};

// A card's subtypes, stored as a small inline array of interned ids (POD, no heap) but
// presented as a sequence of std::string -- so existing
//   for (const std::string& cs : card.m_subtypes) { if (cs == want) ... }
// loops and `subtypes[i]` reads compile unchanged. Replaces the old
// std::vector<std::string> m_subtypes, shrinking Card (deep-copied per search node) and
// removing the per-copy subtype heap allocation. Cards have <= 2 subtypes today; kCap=4
// leaves headroom (e.g. triome lands). Byte-identical: the yielded strings and their
// comparisons are exactly as before; ids never enter any key/output.
class SubtypeSet
{
public:
    static constexpr int kCap = 4;

    // Random-access const iterator that dereferences an id to its registry name string.
    class const_iterator
    {
    public:
        explicit const_iterator(const uint16_t* p) : m_p(p) {}
        const std::string& operator*() const { return SubtypeRegistry::Instance().Name(*m_p); }
        const_iterator& operator++() { ++m_p; return *this; }
        bool operator==(const const_iterator& o) const { return m_p == o.m_p; }
        bool operator!=(const const_iterator& o) const { return m_p != o.m_p; }
    private:
        const uint16_t* m_p;
    };

    const_iterator begin() const { return const_iterator(m_ids); }
    const_iterator end()   const { return const_iterator(m_ids + m_count); }

    std::size_t size()  const { return m_count; }
    bool        empty() const { return m_count == 0; }

    const std::string& operator[](std::size_t i) const
    {
        return SubtypeRegistry::Instance().Name(m_ids[i]);
    }

    void clear() { m_count = 0; }

    // Append a subtype by name. Interns it (read-only for an already-interned name, so
    // safe on the worker threads once everything is pre-interned at load). Silently caps
    // at kCap -- which holds for all current cards (max 2).
    void push_back(const std::string& name)
    {
        if (m_count < kCap)
        {
            m_ids[m_count++] = SubtypeRegistry::Instance().Intern(name);
        }
    }

    // Assign from a name list (e.g. a token's subtypes from CardParams). The names must be
    // pre-interned at load so this only does read-only Intern lookups at runtime.
    SubtypeSet& operator=(const std::vector<std::string>& names)
    {
        m_count = 0;
        for (const std::string& s : names) { push_back(s); }
        return *this;
    }

private:
    uint16_t m_ids[kCap] = {0, 0, 0, 0};
    uint8_t  m_count     = 0;
};
