#pragma once
#include <string>
#include <ostream>

// Global interning table for card NAMES. Card::m_name was a std::string, deep-copied once per
// card per search node -- every long (> SSO) name heap-allocated on each GameState clone and
// each order-preserving zone erase (move-shift). InternedName replaces the inline string with an
// 8-byte pointer into a process-wide registry of canonical name strings, so:
//   * copying a Card is a register move (no per-card heap alloc) -- and with m_name the LAST
//     non-trivial member of Card, the whole struct becomes trivially copyable, so vector<Card>
//     copy/erase lower to memcpy/memmove (kills the _M_erase string-move cost too); and
//   * the canonical strings live forever in the registry (stable addresses), so a bound
//     `const std::string&` outlives the Card it came from.
//
// BYTE-IDENTICAL: m_name_hash is still computed by Card::RehashName via std::hash<std::string>
// (independent of this registry), so the transposition-table key is unchanged. Comparisons stay
// string comparisons (via the implicit conversion to const std::string&); the string ctors are
// EXPLICIT so a `name == some_std_string` never implicitly interns the RHS (which would both
// change semantics and race the registry from a worker thread).
class InternedName
{
public:
    InternedName() : m_str(&EmptyStr()) {}
    explicit InternedName(const std::string& s) : m_str(Intern(s)) {}
    explicit InternedName(const char* s)        : m_str(Intern(std::string(s))) {}

    // Copy/move/dtor are implicit and trivial (pointer member) -> InternedName, and thus Card,
    // is trivially copyable. Assigning a string interns it (the only mutating path).
    InternedName& operator=(const std::string& s) { m_str = Intern(s); return *this; }
    InternedName& operator=(const char* s)        { m_str = Intern(std::string(s)); return *this; }

    operator const std::string&() const { return *m_str; }   // implicit: covers every read site
    const std::string& str() const      { return *m_str; }

    // Interned names are canonical, so pointer equality == string equality (used for
    // InternedName-vs-InternedName; string/char* comparisons go via the conversion above).
    bool operator==(const InternedName& o) const { return m_str == o.m_str; }
    bool operator!=(const InternedName& o) const { return m_str != o.m_str; }

    // Returns the canonical, stable address of `s` in the registry (thread-safe). Empty maps to
    // a fixed canonical empty so a default-constructed name and an assigned "" share a pointer.
    static const std::string* Intern(const std::string& s);
    static const std::string& EmptyStr();

private:
    const std::string* m_str;
};

// Comparisons against std::string / const char* (templated std::string::operator== ignores the
// implicit conversion during deduction, so these are spelled out). All are plain string compares
// -> byte-identical with the former std::string m_name.
inline bool operator==(const InternedName& a, const std::string& b) { return a.str() == b; }
inline bool operator==(const std::string& a, const InternedName& b) { return a == b.str(); }
inline bool operator!=(const InternedName& a, const std::string& b) { return a.str() != b; }
inline bool operator!=(const std::string& a, const InternedName& b) { return a != b.str(); }
inline bool operator==(const InternedName& a, const char* b)        { return a.str() == b; }
inline bool operator==(const char* a, const InternedName& b)        { return a == b.str(); }
inline bool operator!=(const InternedName& a, const char* b)        { return a.str() != b; }
inline bool operator!=(const char* a, const InternedName& b)        { return a != b.str(); }

inline std::ostream& operator<<(std::ostream& os, const InternedName& n) { return os << n.str(); }
