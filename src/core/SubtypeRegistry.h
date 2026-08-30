#pragma once
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <mutex>

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
//
// THAT INVARIANT WAS FALSE UNTIL 2026-08-30, and it is now ENFORCED rather than assumed.
// A handful of subtype names exist only as C++ string literals in effect code -- Oko's "Elk" and
// "Food" tokens, the Kavu token -- so they are NOT in cards.json and were NOT interned at load.
// The first worker thread to create one called Intern() mid-search, which does
// `m_names.push_back(...)`: a std::vector REALLOCATION racing against every other worker's
// `Name(id)` read of that same vector, plus an unordered_map insert racing its readers. Confirmed
// by ThreadSanitizer (write at SubtypeRegistry.h:44 from ApplyLoyaltyAbility vs read at :59 from
// CardHasSubtype, on 12 threads). That is real UB -- a torn read or a freed buffer, not a benign
// same-value write.
//
// Two changes close it, and BOTH matter:
//   * kRuntimeSubtypeLiterals below is the single source of truth for every subtype name that
//     exists only in code. CardDatabase interns the whole list at load (RebuildInternedIndex), so
//     the runtime path always HITS and never mutates.
//   * Freeze() then makes that structural: after it, Intern() cannot insert at all. A name that
//     was somehow missed returns kNone loudly (one warning per name) instead of racing. There is
//     a unit test asserting every literal in the list resolves after a real DB load, so a newly
//     added token subtype fails the suite rather than corrupting a run.
// Post-freeze the containers are immutable, so every read path is lock-free and allocation-free
// exactly as before -- this costs nothing on the hot path.
class SubtypeRegistry
{
public:
    static constexpr uint16_t kNone = 0;

    // Subtype names that appear ONLY in C++ effect code, never in cards.json. Keep this list in
    // sync when adding a token whose subtype is a literal -- the unit test enforces it.
    static const std::vector<std::string>& RuntimeLiterals()
    {
        static const std::vector<std::string> kRuntimeSubtypeLiterals = {
            "Elk",       // Oko +1 elk_transform
            "Food",      // Oko +2 food_token
            "Kavu",      // Kavu token
            "Treasure",  // treasure tokens (also in cards.json today; listed so it stays covered)
            "Aura",      // synthesised aura back-faces
        };
        return kRuntimeSubtypeLiterals;
    }

    static SubtypeRegistry& Instance()
    {
        static SubtypeRegistry inst;
        return inst;
    }

    // Load-time: intern a name, assigning a fresh id if unseen. Single-threaded.
    //
    // AFTER Freeze() this NEVER mutates -- it degrades to the same read-only lookup Id() does, so
    // the worker threads that reach it through SubtypeSet::push_back (token creation) cannot race
    // the containers. See the class note.
    uint16_t Intern(const std::string& name)
    {
        if (name.empty()) { return kNone; }
        auto it = m_ids.find(name);
        if (it != m_ids.end()) { return it->second; }
        if (m_frozen)
        {
            // A name reachable at runtime that load never saw: a bug in RuntimeLiterals(), not a
            // condition to paper over. Warn once per name (this is off the hot path by
            // construction -- a hit returns above) and refuse to mutate.
            WarnUnfrozen(name);
            return kNone;
        }
        const uint16_t id = static_cast<uint16_t>(m_names.size()); // next id == current size
        m_ids.emplace(name, id);
        m_names.push_back(name);
        return id;
    }

    // Called once by CardDatabase after the DB (and RuntimeLiterals) are interned. From here on the
    // registry is immutable, which is what makes the lock-free Id()/Name() reads on the search hot
    // path actually safe rather than merely intended.
    void Freeze() { m_frozen = true; }
    bool Frozen() const { return m_frozen; }
    // The loader may run again (LoadFromJson is documented as callable per file, and Register()
    // adds Tier-3 cards), and parsing a card interns its subtypes. Loading is single-threaded and
    // happens before the worker pool exists, so re-opening the registry for it is safe; each load
    // re-freezes at its end via RebuildInternedIndex().
    void Thaw() { m_frozen = false; }

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

    // Out-of-line-ish so the hot path stays a plain find. Guarded by its own mutex because the
    // callers ARE worker threads -- the whole point is that this path must not touch m_ids/m_names.
    static void WarnUnfrozen(const std::string& name)
    {
        static std::mutex                    mu;
        static std::unordered_set<std::string> seen;
        std::lock_guard<std::mutex> lk(mu);
        if (!seen.insert(name).second) { return; }
        std::fprintf(stderr,
                     "[subtype-registry] '%s' was first seen AFTER freeze and resolves to kNone. "
                     "Add it to SubtypeRegistry::RuntimeLiterals().\n", name.c_str());
    }

    std::vector<std::string>                  m_names;  // id -> name (index 0 = none)
    std::unordered_map<std::string, uint16_t> m_ids;    // name -> id
    bool                                      m_frozen = false;
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

    // Raw interned id at slot i, for id-equality checks that must not touch the name strings
    // (Urza's Incubator's chosen creature type is carried as an id on the Permanent).
    uint16_t IdAt(std::size_t i) const { return m_ids[i]; }
    bool     HasId(uint16_t id) const
    {
        if (id == SubtypeRegistry::kNone) { return false; }
        for (uint8_t i = 0; i < m_count; ++i) { if (m_ids[i] == id) { return true; } }
        return false;
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
