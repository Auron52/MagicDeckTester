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
    Splice,
    // Storm (Dragonstorm): an INERT keyword-ability tag -- the storm mechanic is modelled structurally via
    // CardParams::tutor_to_battlefield + GameState::spells_cast_this_turn (put storm+1 Dragons), not by this
    // enum. Kept only so the Scryfall keywords field ["Storm"] stays faithful; no engine code reads it.
    Storm,
    // Enchant (every Aura): an INERT keyword-ability tag -- the aura's attach mechanic is modelled
    // structurally via CardParams::is_aura + Permanent::aura_attached_to, not by this enum. Kept only so the
    // Scryfall keywords field ["Enchant"] stays faithful; no engine code reads it.
    // Hexproof (Slippery Bogle, Gladecover Scout, Alpha Authority's grant): an INERT keyword-ability
    // tag -- "can't be the target of spells/abilities your opponents control." Provably inert vs the
    // passive goldfish opponent (no opponent spells/abilities target our creatures). No engine code
    // reads it; kept so the Scryfall keywords field ["Hexproof"] stays faithful.
    Hexproof,
    Enchant,
    // Equip (Lightning Greaves): an INERT keyword-ability tag -- the equip mechanic is modelled
    // structurally via CardParams::is_equipment + Permanent::equipped_to + Action::Kind::Equip,
    // not by this enum. Kept only so the Scryfall keywords field ["Equip"] stays faithful.
    Equip,
    // Umbra armor (Hyena/Spider/Lion Umbra): an INERT keyword-ability tag -- "if enchanted creature would be
    // destroyed, instead destroy this Aura." Provably inert vs the passive goldfish opponent (no removal /
    // combat damage ever destroys our creatures). Kept only so the Scryfall keywords field stays faithful.
    UmbraArmor,
    // Rampage (Varchild's War-Riders): an INERT keyword-ability tag -- "+1/+1 per blocker beyond the
    // first." Provably inert vs the passive goldfish opponent (it never blocks). Kept only so the
    // Scryfall keywords field stays faithful; no engine code reads it.
    Rampage,
    // Cumulative upkeep (Varchild's War-Riders): an INERT keyword-ability tag -- the mechanic is
    // modelled structurally via CardParams::cumulative_upkeep_opp_token + Permanent::age_counters
    // (always-paid opponent-token gifts), not by this enum. Kept only so the Scryfall keywords field
    // ["Cumulative upkeep"] stays faithful; no engine code reads it.
    CumulativeUpkeep,
    // Strive (Twinflame): an INERT keyword-ability tag -- the "costs {2}{R} more per extra target"
    // mechanic is modelled structurally via CardParams::strive_cost (searched extra-target-count
    // plan variants), not by this enum. Kept only so the Scryfall keywords field stays faithful.
    Strive,
    // Treasure (Gold Rush): an INERT keyword-ability tag (Scryfall marks Treasure-token makers with
    // it) -- the mechanic is modelled structurally via CardParams::creates_treasures + the
    // "Treasure Token" card def's sac-for-mana. Kept only so the Scryfall keywords field stays
    // faithful; no engine code reads it.
    Treasure,
    // Metalcraft (Puresteel Paladin): an INERT ability-word tag -- the mechanic is modelled
    // structurally via CardParams::metalcraft_equip_zero_artifacts (EquipCostGenericNow). Kept
    // only so the Scryfall keywords field stays faithful; no engine code reads it.
    Metalcraft,
    // Regenerate (Deathbellow Raider's "{2}{B}: Regenerate this creature"): an INERT keyword-ability
    // tag. Regeneration is a replacement effect for DESTRUCTION, and nothing in this sim destroys our
    // creatures (the opponent never blocks, casts, or removes -- see Combat.cpp, which has no blocker
    // path at all), so the ability can never apply. Kept only so the Scryfall keywords field
    // ["Regenerate"] stays faithful; no engine code reads it. Disclosed deferral D8.
    Regenerate,
    // Bestow (Gnarled Scarhide): an INERT keyword-ability TAG -- the mechanic itself is fully modelled
    // structurally via CardParams::bestow_cost + Action::bestow + the DB's synthesized
    // "<name> (Bestowed)" aura face. Kept only so the Scryfall keywords field ["Bestow"] stays
    // faithful; no engine code reads this enumerator.
    Bestow
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

    // Two-colour hybrid pips ({B/G}: pay with EITHER colour; Deathrite Shaman, user-directed
    // 2026-08-06). REPRESENTATION: the pip's FIRST listed colour is baked into the flat colour
    // ints above exactly as the historical collapse did (so every flat reader -- ManaValue,
    // colour-demand heuristics, equality, signatures -- is BYTE-IDENTICAL to the pre-hybrid
    // engine; the smoke suite proved churn otherwise on Slippery Bogle). The metadata below only
    // ADDS payable assignments: ExpandHybrids(bits) moves a first-colour pip over to the second
    // colour per set bit, and the affordability/payment sites (ManaPool::CanPay, TapForCostShared,
    // PayFromPool) try assignments in bits order -- bits==0 IS the old flat cost, so behaviour
    // changes only where the old collapse could not pay at all. Each entry packs the two colours
    // as (first << 4) | second in printed order; > 4 hybrid pips unsupported (no such card).
    // {2/W} / Phyrexian remain unsupported.
    uint8_t hybrid_count   = 0;
    uint8_t hybrid_pair[4] = {0, 0, 0, 0};

    // X counts as 0 outside the stack (CR 202.3). Hybrid pips are already counted in their
    // first colour's flat int.
    int ManaValue() const { return generic + white + blue + black + red + green + colorless; }

    // Flat copy with hybrid pip i moved from its FIRST colour to its SECOND when bit i of `bits`
    // is set (bits == 0 returns the plain flat cost). Result carries no hybrid metadata.
    ManaCost ExpandHybrids(unsigned bits) const
    {
        ManaCost c = *this;
        c.hybrid_count = 0;
        for (int i = 0; i < 4; ++i) { c.hybrid_pair[i] = 0; }
        auto bump = [&c](int col, int delta)
        {
            switch (static_cast<Color>(col))
            {
                case Color::White:     c.white     += delta; break;
                case Color::Blue:      c.blue      += delta; break;
                case Color::Black:     c.black     += delta; break;
                case Color::Red:       c.red       += delta; break;
                case Color::Green:     c.green     += delta; break;
                case Color::Colorless: c.colorless += delta; break;
            }
        };
        for (int i = 0; i < hybrid_count; ++i)
        {
            if ((bits >> i) & 1u)
            {
                bump(hybrid_pair[i] >> 4, -1);
                bump(hybrid_pair[i] & 0xF, +1);
            }
        }
        return c;
    }

    // Canonical MTG notation: {X}{N}{W}{U}{B}{R}{G}{C}
    std::string ToString() const
    {
        // NOTE: hybrid pips render as their FIRST colour's plain pip (they are baked into the
        // flat ints below). Deliberate: the regression play digest folds this string
        // (GameLogger::LogCastSpell manaPaid), so rendering {G/U} would phantom-churn every
        // existing digest (proved on the auras deck). Payment semantics are unaffected; the
        // authoritative printed cost lives in cards.json's mana_cost/oracle_text.
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
    bool IsEnchantment() const { return HasType(CardType::Enchantment); }

    bool HasType(CardType t)       const { return (m_type_mask      & Bit(t)) != 0; }
    bool HasSupertype(Supertype s) const { return (m_supertype_mask & Bit(s)) != 0; }
    bool HasColor(Color c)         const { return (m_color_mask     & Bit(c)) != 0; }
    bool HasKeyword(Keyword k)     const { return (m_keyword_mask   & Bit(k)) != 0; }

    // Number of colors this card is (popcount of the color mask; 0 = colorless, CR 105.2).
    // Used by Mana Cannons (damage = colors of cast spell), Ancient Cornucopia (lifegain),
    // and Jared Carthalion's -3 (+1/+1 counters = target's color count).
    int ColorCount() const
    {
        uint32_t m = m_color_mask;
        int n = 0;
        while (m) { m &= m - 1; ++n; }
        return n;
    }
    bool IsMulticolored() const { return ColorCount() >= 2; }
};
