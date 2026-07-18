#pragma once
#include <string>
#include <vector>
#include <optional>
#include <cstdint>
#include <functional>
#include "SubtypeRegistry.h"
#include "NameRegistry.h"

struct CardDefinition; // fwd decl: Card caches a pointer into the CardDatabase (see m_def)

enum class CardType { Land, Creature, Instant, Sorcery, Enchantment, Artifact, Planeswalker, Battle };
enum class Color { White, Blue, Black, Red, Green, Colorless };
enum class Keyword
{
    Haste, Flying, Trample, Deathtouch, Lifelink, FirstStrike, DoubleStrike,
    Vigilance, Reach, Defender, Indestructible, Flash, Menace, Prowess, Exalted,
    // Suspend (Lotus Bloom): an INERT keyword-ability tag -- the mechanic is modelled structurally via
    // CardParams::suspend_time_counters (Action::Kind::Suspend + CastOffSuspend), not by this enum. Kept
    // only so the Scryfall keywords field ["Suspend"] stays faithful; no engine code reads it.
    Suspend,
    // Splice (Desperate Ritual): an INERT keyword-ability tag -- the "Splice onto Arcane {1}{R}" mechanic
    // is modelled structurally via CardParams::splice_onto_arcane (Action::splice_count scaling cost+float),
    // not by this enum. Kept only so the Scryfall keywords field ["Splice"] stays faithful; no code reads it.
    Splice
};
enum class Supertype { Legendary, Basic, Snow, World };

struct ManaCost
{
    int generic   = 0;
    int white     = 0;
    int blue      = 0;
    int black     = 0;
    int red       = 0;
    int green     = 0;
    int colorless = 0;  // {C} symbols
    bool has_x    = false;
    int  x_pips   = 0;  // number of {X} symbols (Crackle with Power = {X}{X}{X} -> 3). The chosen
                        // X is paid x_pips times as generic; max affordable X divides by x_pips.

    // X counts as 0 outside the stack (CR 202.3)
    int ManaValue() const { return generic + white + blue + black + red + green + colorless; }

    // Canonical MTG notation: {X}{N}{W}{U}{B}{R}{G}{C}
    std::string ToString() const
    {
        std::string s;
        if (has_x)    { s += "{X}"; }
        if (generic > 0) { s += "{" + std::to_string(generic) + "}"; }
        for (int i = 0; i < white;     ++i) { s += "{W}"; }
        for (int i = 0; i < blue;      ++i) { s += "{U}"; }
        for (int i = 0; i < black;     ++i) { s += "{B}"; }
        for (int i = 0; i < red;       ++i) { s += "{R}"; }
        for (int i = 0; i < green;     ++i) { s += "{G}"; }
        for (int i = 0; i < colorless; ++i) { s += "{C}"; }
        return s;
    }
};

struct Card
{
    // NB: this struct is deep-copied once per search node (~0.55 copies/node), so
    // every member here is paid for on the hot path. Fields that are never read
    // during simulation are deliberately kept OUT of it -- e.g. a card's oracle
    // text and Scryfall id live only on the CardDefinition in CardDatabase, since
    // dispatch is by template/params, not by parsing rules text at runtime.
    //
    // The small-enum sets (types / supertypes / colors / keywords) are stored as
    // bitmasks rather than std::vector. Each enum has < 32 distinct values, so a
    // membership set fits in one uint32_t: copying is a register move (no per-copy
    // heap allocation) and Has*() is an O(1) bit test instead of a linear scan.
    // This is the dominant clone cost on the hot path -- every card formerly paid
    // a heap alloc/free for its (always non-empty) type set on each GameState copy.
    // Subtypes stay a vector<string>: they are string-matched (lord subtype checks)
    // and would need an intern table to pack, which the lord paths don't justify.
    InternedName m_name;   // interned: an 8-byte pointer into a process-wide name registry (see
                           // NameRegistry.h). Reads convert implicitly to const std::string&; this
                           // keeps Card trivially copyable so vector<Card> copy/erase are memcpy/
                           // memmove. m_name_hash (below) is still the std::hash of the name string.
    // Cached std::hash of m_name. The search folds card NAMES into the transposition-table
    // key (BuildSimKey) once per card per node via std::hash<std::string>{}(m_name); caching
    // it here turns that hot per-node hash into a load. MUST be refreshed (RehashName) at the
    // few sites that assign m_name -- a stale value would change the TT key (a bug, caught by
    // the byte-identical smoke/regression gate). Copied with the card (stays consistent).
    uint64_t    m_name_hash = 0;
    int         m_number    = 0;    // per-copy stable ID (1–60); assigned at deck setup
    bool        m_is_staged = false; // true while the card is a staged (exiled) card in hand
    int         m_staged_expiry = 0; // last turn this staged card may be played (CR 406); valid when m_is_staged
    // Apex of Power impulse-exile marker: this staged card was exiled by Apex ("you may cast SPELLS
    // from among them"), so if it is a LAND it may NOT be PLAYED (a land is played, not cast; CR 601.2).
    // Set ONLY in Apex's exile loop (DrawTopAsImpulseStaged) -- never on Light Up / Expressive Iteration
    // / Soulfire staged cards, whose lands MUST stay playable. The land-play sites skip a staged card
    // with this bit; every other card leaves it false -> byte-identical. Travels with the card (copied).
    bool        m_impulse_no_land = false;
    ManaCost m_mana_cost;
    SubtypeSet  m_subtypes;              // creature/land subtypes (e.g. "Sliver", "Goblin", "Mountain"):
                                         // interned-id storage, iterates as std::string (see SubtypeSet)
    uint32_t    m_type_mask      = 0;    // set of CardType  (see Bit())
    uint32_t    m_supertype_mask = 0;    // set of Supertype
    uint32_t    m_color_mask     = 0;    // set of Color
    uint32_t    m_keyword_mask   = 0;    // set of Keyword
    std::optional<int>     m_power;      // null for non-creatures
    std::optional<int>     m_toughness;

    // Memoized pointer to this card's CardDatabase entry, resolved lazily by
    // CardDatabase::LookupCached(const Card&). The DB is a lifetime singleton whose
    // entries are never erased/relocated, so the pointer stays valid; it is copied
    // along with the card (a cheap register move) so a card resolved once carries its
    // definition through every GameState deep-copy in the search -- eliminating the
    // repeated name-hash + hashtable find that dominated the profile after the
    // by-value-Lookup fix. nullptr means "not yet resolved" (re-resolves on next use).
    //
    // DERIVED FROM m_name: it must be reset to nullptr anywhere m_name is reassigned
    // (token P/T naming), and it must NEVER feed BuildSimKey / game-log output / any
    // hashing -- a heap address is non-deterministic across runs and would break the
    // deterministic-budget contract. (BuildSimKey folds only chosen fields, not the
    // struct, so it is unaffected.)
    mutable const CardDefinition* m_def = nullptr;

    // The enum value's ordinal is its bit index; every enum above has < 32 values.
    static constexpr uint32_t Bit(CardType t)  { return 1u << static_cast<int>(t); }
    static constexpr uint32_t Bit(Supertype s) { return 1u << static_cast<int>(s); }
    static constexpr uint32_t Bit(Color c)     { return 1u << static_cast<int>(c); }
    static constexpr uint32_t Bit(Keyword k)   { return 1u << static_cast<int>(k); }

    // Recompute m_name_hash; call after any assignment to m_name. The hash must match
    // exactly what BuildSimKey/graveyard folding used before (std::hash<std::string>).
    void RehashName() { m_name_hash = std::hash<std::string>{}(m_name); }

    void AddType(CardType t)       { m_type_mask      |= Bit(t); }
    void AddSupertype(Supertype s) { m_supertype_mask |= Bit(s); }
    void AddColor(Color c)         { m_color_mask     |= Bit(c); }
    void AddKeyword(Keyword k)     { m_keyword_mask   |= Bit(k); }

    bool IsLand()     const { return HasType(CardType::Land); }
    bool IsCreature() const { return HasType(CardType::Creature); }
    bool IsInstant()  const { return HasType(CardType::Instant); }
    bool IsSorcery()  const { return HasType(CardType::Sorcery); }

    bool HasType(CardType t)       const { return (m_type_mask      & Bit(t)) != 0; }
    bool HasSupertype(Supertype s) const { return (m_supertype_mask & Bit(s)) != 0; }
    bool HasColor(Color c)         const { return (m_color_mask     & Bit(c)) != 0; }
    bool HasKeyword(Keyword k)     const { return (m_keyword_mask   & Bit(k)) != 0; }
};
