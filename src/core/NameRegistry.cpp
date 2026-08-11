#include "NameRegistry.h"
#include <mutex>
#include <unordered_map>
#include <unordered_set>

// The registry: a node-based unordered_set owns the canonical strings. Node-based storage keeps
// element addresses stable across rehash (the standard guarantees references/pointers to set
// elements survive rehash; only iterators are invalidated), so the pointers handed out by
// Intern() stay valid for the life of the process. A single mutex guards inserts -- interning
// happens at deck/DB load (single-threaded) and, during the parallel search, only when a worker
// creates a token whose "P/T [subtype] Token/Creature" name is not yet seen. Distinct token
// names are few and repeat heavily, so contention is negligible against the search's work.
namespace
{
    std::mutex                     g_mutex;
    std::unordered_set<std::string> g_names;
}

const std::string& InternedName::EmptyStr()
{
    static const std::string empty;
    return empty;
}

const std::string* InternedName::Intern(const std::string& s)
{
    if (s.empty()) { return &EmptyStr(); }
    // Thread-local memo. Interned strings repeat heavily -- during the parallel search a worker
    // re-interns the same token name ("P/T subtype Token") across many nodes -- and every call
    // otherwise takes the global insert mutex. Caching the canonical pointer per thread returns the
    // SAME process-stable pointer on a repeat (g_names is node-based, so element addresses survive
    // rehash) with no lock, so this is byte-identical -- pointer identity is preserved -- and only
    // the first sighting of a name on a given thread pays the lock. The memo holds pointers into
    // g_names, which outlive every thread, so it is safe to drop at thread exit.
    static thread_local std::unordered_map<std::string, const std::string*> memo;
    auto it = memo.find(s);
    if (it != memo.end()) { return it->second; }
    const std::string* p;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        p = &*g_names.insert(s).first;
    }
    memo.emplace(s, p);
    return p;
}
