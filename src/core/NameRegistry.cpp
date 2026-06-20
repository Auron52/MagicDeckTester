#include "NameRegistry.h"
#include <mutex>
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
    std::lock_guard<std::mutex> lock(g_mutex);
    return &*g_names.insert(s).first;
}
