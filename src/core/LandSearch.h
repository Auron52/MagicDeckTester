#pragma once
// =================================================================================================
// THE LAND-SEARCH CLUSTER (2026-10-15) -- INCLUDED FROM SpellEffects.h ONLY (after EnterLand,
// ResolveLandEnterEtbExtras and PerformLandTutorToBattlefield, which it builds on). The pin, the spec
// struct and the forward declarations live beside ShuffleAfterSearch in SpellEffects.h, because the
// fire sites are spread through that header.
//
// One resolution helper and one pick generator for every "search your library for up to N [basic]
// land card(s) [that share a land type], put them onto the battlefield [tapped] [and/or into your
// hand], then shuffle":
//
//   card                       spec                                    fire site
//   Rampant Growth             1 basic, tapped                         spell resolution
//   Search for Tomorrow        1 basic, untapped                       spell resolution / off suspend
//   Cultivate                  2 basic, 1 to hand, tapped              spell resolution
//   Evolving Wilds /           1 basic, tapped                         {T}, sac: SacLandSearch
//     Terramorphic Expanse
//   Myriad Landscape           2 basic sharing a land type, tapped     {2},{T}, sac: SacLandSearch
//   Khalni Heart Expedition    2 basic, tapped                         3 quest ctrs + sac: QuestSac...
//   Primeval Titan             2 ANY lands, tapped, optional           ETB + attack trigger
//
// WHY NOT the fetchland collapse (fetch_land_types / PerformFetch): the fetchland never enters the
// battlefield (so its own landfall and the hold line vanish), pays 1 life, filters by SUBTYPE and
// ignores "tapped". WHY NOT Crop Rotation's PerformLandTutorToBattlefield: it puts ONE card through
// EnterLand without the land's own ETB extras (a fetched Karoo bounces nothing) and its target pin is
// inert in resolve mode (the provider's front is taken inside the search window -- the Creature
// Giving item, deliberately not touched here).
//
// THE PICK IS SEARCHED, never greedy (USER hard rule). LandSearchPicks enumerates every legal
// complete pick at RESOLUTION, folded only by two sound identities:
//   * COPIES / BEHAVIOURAL TWINS are one class (CardDefinition::behaviour_identity, which digests the
//     whole cards.json entry minus name and oracle text -- Evolving Wilds == Terramorphic Expanse).
//     Which physical copy is taken is visible only through the post-shuffle library order, i.e. only
//     through clairvoyance; in real-information terms the copies are interchangeable.
//   * "UP TO N" COLLAPSES TO THE MAXIMAL picks (no valid strict superset). Dominance: the extra land
//     is one card removed from an otherwise IDENTICAL future (Library::ShuffleByKey is keyed per card,
//     so removing a card leaves every other card's relative order unchanged), and it arrives now --
//     on the battlefield (a land is never worse to have, against a passive opponent) or in hand
//     (Cultivate: the only cost is a possible cleanup discard, which is itself our choice, and binning
//     that basic is equivalent to not having fetched it, plus one card of library thinning). The
//     human may always stop early or (when optional) decline.
// The provider ORDERS the list (DecisionProvider::LandSearchPickOrder; entry 0 is the default for
// every path without a pin: the d0 runner, rollout leaves beyond the horizon, the human's
// preselection) and the SEARCH picks the index (Plan::land_search_pins for in-plan resolutions,
// Plan::attack_land_search_choice for Titan's attack trigger, Plan::upkeep_land_search_choice for
// Search for Tomorrow off suspend). Nothing here truncates.
//
// LANDFALL. Every battlefield entry goes through EnterLand -- the land-entry chokepoint the landfall
// dispatch hangs off -- once per land. A multi-put of lands only (Myriad, Khalni, Titan) may call it
// sequentially: every landfall payload in the pool commutes with every other, and none creates a
// permanent another landfall trigger could target (the dispatch's own contract). The lands' OWN
// enter triggers (a Gruul Turf's mandatory bounce, Radiant Fountain's 2 life) resolve in a SECOND
// pass, after every land of the put is on the battlefield (CR 603.6a: one simultaneous event), so a
// Titan-fetched Turf may return the Forest fetched beside it.
// =================================================================================================

#include <algorithm>
#include <cstdint>
#include <functional>
#include <string>
#include <typeinfo>
#include <vector>

// One identity class of legal library lands for a search. `rep` = the lexicographically smallest
// member name (stable across shuffles); `def` = that member's definition.
struct LandSearchClass
{
    std::string           rep;
    std::uint64_t         ident  = 0;   // behaviour_identity; 0 = fold by name only
    int                   copies = 0;
    const CardDefinition* def    = nullptr;
};

// Is library card `d` a legal find for `spec`? "Land card" reads the DEFINITION (a library Card is a
// name-only placeholder with empty masks) -- the FRONT face, which is the card's only face in the
// library (CR 712.8a), so a land-backed MDFC is not a land card here.
inline bool LandSearchLegal(const CardDefinition* d, const LandSearchSpec& spec)
{
    if (d == nullptr || !d->card.IsLand()) { return false; }
    if (spec.basic_only && !IsBasicLandCard(*d)) { return false; }
    return true;
}

// Do two land definitions share at least one land subtype (CR 205.3i -- the land types)?
inline bool LandDefsShareLandType(const CardDefinition* a, const CardDefinition* b)
{
    if (a == nullptr || b == nullptr) { return false; }
    const SubtypeSet& sa = a->card.m_subtypes;
    for (std::size_t i = 0; i < sa.size(); ++i)
    {
        if (b->card.m_subtypes.HasId(sa.IdAt(i))) { return true; }
    }
    return false;
}

inline std::vector<LandSearchClass> LandSearchClasses(const GameState& s, int ctrl,
                                                      const LandSearchSpec& spec)
{
    std::vector<LandSearchClass> cls;
    const Player& pl = s.players[static_cast<std::size_t>(ctrl)];
    for (std::size_t i = 0; i < pl.library.size(); ++i)
    {
        const Card& c = pl.library[i];
        const CardDefinition* d = CardDatabase::Instance().LookupCached(c);
        if (!LandSearchLegal(d, spec)) { continue; }
        const std::string nm = c.m_name.str();
        LandSearchClass* hit = nullptr;
        for (LandSearchClass& k : cls)
        {
            const bool same = d->behaviour_identity != 0
                ? k.ident == d->behaviour_identity
                : (k.ident == 0 && k.rep == nm);
            if (same) { hit = &k; break; }
        }
        if (hit == nullptr)
        {
            LandSearchClass k;
            k.rep = nm; k.ident = d->behaviour_identity; k.copies = 1; k.def = d;
            cls.push_back(std::move(k));
        }
        else
        {
            ++hit->copies;
            if (nm < hit->rep) { hit->rep = nm; hit->def = d; }
        }
    }
    std::sort(cls.begin(), cls.end(),
              [](const LandSearchClass& a, const LandSearchClass& b) { return a.rep < b.rep; });
    return cls;
}

// How many of a pick's lands go to the BATTLEFIELD: "put one onto the battlefield and the other into
// your hand" sends a LONE find to the battlefield (Cultivate ruling 2010-08-15), so the battlefield
// fills first.
inline int LandSearchBattlefieldCount(const LandSearchSpec& spec, int picked)
{
    return std::min(picked, std::max(0, spec.count - spec.to_hand));
}

// Whether the provider's pick NARROWING applies (DecisionProvider::LandSearchPickWidth): never under
// human play (the human answers every tutor_etb prompt over the full legal set), never with the
// standing A/B gate MTG_UNPRUNE=landpick open. Part of the memo key: one process can hold both lists.
inline bool LandSearchPickPruned()
{
    return !HumanPlayActive() && !DecisionUnpruned(UnprunedGate::LandPick);
}

// Every legal COMPLETE pick, provider-ordered. Each entry is a list of class-representative names:
// the battlefield entries first (sorted), then the hand entries (sorted). See the header for the two
// sound folds. Never empty unless nothing legal exists (then the search simply finds nothing).
// The UNMEMOIZED generator -- callers want LandSearchPicksMemo / LandSearchPicks below.
inline std::vector<std::vector<std::string>> LandSearchPicksUncached(const GameState& s, int ctrl,
                                                                     const LandSearchSpec& spec)
{
    std::vector<std::vector<std::string>> out;
    if (spec.count <= 0) { return out; }
    std::vector<LandSearchClass> cls = LandSearchClasses(s, ctrl, spec);
    const DecisionProvider& prov = ResolveProvider(s);
    // The provider's CLASS narrowing (LandSearchAdmitsLand): autonomous play only, like the width below.
    if (LandSearchPickPruned())
    {
        cls.erase(std::remove_if(cls.begin(), cls.end(), [&](const LandSearchClass& k)
                  { return k.def == nullptr
                        || !prov.LandSearchAdmitsLand(s, ctrl, spec.count, spec.to_hand, spec.basic_only,
                                                      spec.share_land_type, *k.def); }),
                  cls.end());
    }
    const int n = static_cast<int>(cls.size());
    if (n == 0) { return out; }
    // Multisets of class indices (non-decreasing), size 1..count, respecting copy counts and the
    // shared-type constraint; only MAXIMAL ones are kept.
    std::vector<int> cur;
    std::vector<int> used(static_cast<std::size_t>(n), 0);
    std::vector<std::vector<int>> multisets;
    auto can_add = [&](int c)
    {
        if (used[static_cast<std::size_t>(c)] >= cls[static_cast<std::size_t>(c)].copies) { return false; }
        if (spec.share_land_type)
        {
            for (int m : cur)
            {
                if (!LandDefsShareLandType(cls[static_cast<std::size_t>(m)].def,
                                           cls[static_cast<std::size_t>(c)].def)) { return false; }
            }
        }
        return true;
    };
    // Recursive walk. `start` keeps the multiset non-decreasing so each is generated once.
    auto rec = [&](auto&& self, int start) -> void
    {
        bool extendable = false;
        if (static_cast<int>(cur.size()) < spec.count)
        {
            // Maximality is tested against ANY class (not only >= start), since a superset may add a
            // smaller-index class.
            for (int c = 0; c < n; ++c) { if (can_add(c)) { extendable = true; break; } }
            for (int c = start; c < n; ++c)
            {
                if (!can_add(c)) { continue; }
                cur.push_back(c); ++used[static_cast<std::size_t>(c)];
                self(self, c);
                cur.pop_back(); --used[static_cast<std::size_t>(c)];
            }
        }
        if (!cur.empty() && !extendable) { multisets.push_back(cur); }
    };
    rec(rec, 0);

    // Destinations: every distinct (battlefield part, hand part) split of each multiset.
    for (const std::vector<int>& ms : multisets)
    {
        const int sz  = static_cast<int>(ms.size());
        const int bfn = LandSearchBattlefieldCount(spec, sz);
        std::vector<std::vector<std::string>> splits;
        if (bfn >= sz)
        {
            std::vector<std::string> names;
            for (int c : ms) { names.push_back(cls[static_cast<std::size_t>(c)].rep); }
            std::sort(names.begin(), names.end());
            splits.push_back(std::move(names));
        }
        else
        {
            // Choose which members go to hand (combinations of positions), dedup by the sorted parts.
            std::vector<int> sel(static_cast<std::size_t>(sz), 0);
            std::fill(sel.end() - (sz - bfn), sel.end(), 1);   // 1 = hand
            do
            {
                std::vector<std::string> bf, hand;
                for (int i = 0; i < sz; ++i)
                {
                    (sel[static_cast<std::size_t>(i)] ? hand : bf)
                        .push_back(cls[static_cast<std::size_t>(ms[static_cast<std::size_t>(i)])].rep);
                }
                std::sort(bf.begin(), bf.end());
                std::sort(hand.begin(), hand.end());
                std::vector<std::string> v = bf;
                v.insert(v.end(), hand.begin(), hand.end());
                if (std::find(splits.begin(), splits.end(), v) == splits.end()) { splits.push_back(std::move(v)); }
            } while (std::next_permutation(sel.begin(), sel.end()));
        }
        for (auto& v : splits) { out.push_back(std::move(v)); }
    }
    prov.LandSearchPickOrder(s, ctrl, spec.count, spec.to_hand, spec.basic_only, spec.share_land_type, out);
    // The provider's NARROWING (LandSearchPickWidth): autonomous play only; see LandSearchPickPruned.
    if (LandSearchPickPruned())
    {
        const int w = prov.LandSearchPickWidth(s, ctrl, spec.count, spec.to_hand, spec.basic_only,
                                               spec.share_land_type, out);
        if (w >= 1 && w < static_cast<int>(out.size())) { out.resize(static_cast<std::size_t>(w)); }
    }
    return out;
}

// ---- THE PICK-LIST MEMO (perf, 2026-10-15) -------------------------------------------------------
// LandSearchPicks ran IN FULL at every resolution -- the class scan, the maximal-multiset walk, the
// string-built pick list and the provider's sort -- and a Primeval Titan attack trigger resolves one
// per attacking Titan per combat in every rollout. perf on a heavy Landfall game (seed 788, d3 b20):
// LandSearchPicks 20.2% of the game's samples (inclusive), PerformLandSearch 37.3%.
//
// EXACT, so play is BYTE-IDENTICAL. The result is a pure function of
//   (a) spec.count / basic_only / share_land_type / to_hand (tapped / optional never enter it);
//   (b) the MULTISET of DEFINITIONS of the legal land cards in ctrl's library -- the classes, their
//       representatives, copy counts and the shared-type test are all functions of it; library ORDER
//       never enters (LandSearchClasses sorts by representative name);
//   (c) the provider's order inputs: its dynamic type and DecisionProvider::LandSearchPickOrderKey
//       (a provider that cannot key its order returns false and is never memoized).
// The key stores (a)-(c) EXACTLY (no hash-only match): a slot is reused only on full equality.
// The library scan below reads every card through Library::operator[], exactly as the generator
// does, so the index-carry recorder (IndexCarry.h) sees the same accesses on a hit as on a miss.
namespace landsearchmemo
{
constexpr int kMaxDefs = 24;   // distinct legal land definitions in the library; more -> no memo
struct Key
{
    std::size_t           prov_type = 0;
    std::uint64_t         prov_key  = 0;
    int                   count = 0, to_hand = 0;
    bool                  basic_only = false, share = false;
    bool                  pruned = false;   // LandSearchPickPruned() -- the provider's narrowing applied
    int                   n = 0;
    const CardDefinition* defs[kMaxDefs] = {};
    int                   copies[kMaxDefs] = {};
    bool operator==(const Key& o) const
    {
        if (prov_type != o.prov_type || prov_key != o.prov_key || count != o.count
            || to_hand != o.to_hand || basic_only != o.basic_only || share != o.share
            || pruned != o.pruned || n != o.n)
        { return false; }
        for (int i = 0; i < n; ++i)
        { if (defs[i] != o.defs[i] || copies[i] != o.copies[i]) { return false; } }
        return true;
    }
    std::uint64_t Hash() const
    {
        std::uint64_t h = 0x4C534D454D4Full;   // "LSMEMO"
        auto mix = [&h](std::uint64_t v)
        {
            h ^= v + 0x9E3779B97F4A7C15ull + (h << 6) + (h >> 2);
            h *= 0xBF58476D1CE4E5B9ull;
            h ^= h >> 31;
        };
        mix(prov_type); mix(prov_key);
        mix(static_cast<std::uint64_t>(count) | (static_cast<std::uint64_t>(to_hand) << 8)
            | (basic_only ? 1ull << 16 : 0ull) | (share ? 1ull << 17 : 0ull) | (pruned ? 1ull << 18 : 0ull));
        for (int i = 0; i < n; ++i)
        {
            mix(static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(defs[i])));
            mix(static_cast<std::uint64_t>(copies[i]));
        }
        return h;
    }
};
struct Slot
{
    bool                                            valid = false;
    Key                                             key;
    std::vector<std::vector<std::string>>           picks;
    // The DEFINITION of every entry of `picks`, same shape (CardDatabase::Lookup of the name, filled
    // once per miss): the resolution takes its lands by definition pointer instead of copying and
    // string-comparing the names (TakeLandFromLibraryDef).
    std::vector<std::vector<const CardDefinition*>> pick_defs;
    // The picks whose BATTLEFIELD part holds a Karoo (etb_bounce_land), in list order -- the picks a
    // put-Karoo bounce sub-choice extends (LandSearchPinDecode, the "extended pick list"). A function
    // of the same key as `picks` (the battlefield count is spec.count - spec.to_hand), filled with it.
    std::vector<int> karoo_picks;
    void Fill(std::vector<std::vector<std::string>>&& p, const LandSearchSpec& spec)
    {
        picks = std::move(p);
        pick_defs.resize(picks.size());
        karoo_picks.clear();
        for (std::size_t i = 0; i < picks.size(); ++i)
        {
            pick_defs[i].resize(picks[i].size());
            for (std::size_t j = 0; j < picks[i].size(); ++j)
            { pick_defs[i][j] = CardDatabase::Instance().Lookup(picks[i][j]); }
            const int bf = std::min(static_cast<int>(pick_defs[i].size()),
                                    std::max(0, spec.count - spec.to_hand));
            for (int j = 0; j < bf; ++j)
            {
                const CardDefinition* d = pick_defs[i][static_cast<std::size_t>(j)];
                if (d != nullptr && d->params.etb_bounce_land)
                { karoo_picks.push_back(static_cast<int>(i)); break; }
            }
        }
    }
};
constexpr std::size_t kSlots = 1024;   // direct-mapped, per thread
inline thread_local Slot t_slots[kSlots];
// Diagnostic counters (thread-local; read by nothing that plays).
inline thread_local unsigned long long t_hits = 0, t_misses = 0, t_bypass = 0;
}   // namespace landsearchmemo

// The provider-ordered pick list, MEMOIZED (see the block above), as the whole slot (names + defs).
// Returns a reference into a per-thread slot: it is valid only until the NEXT memo call on this
// thread, so read what you need from it at once (every caller copies one pick, or reads size()).
inline const landsearchmemo::Slot& LandSearchMemoSlot(const GameState& s, int ctrl,
                                                      const LandSearchSpec& spec)
{
    static thread_local landsearchmemo::Slot t_bypass_out;
    landsearchmemo::Key k;
    k.count      = spec.count;
    k.to_hand    = spec.to_hand;
    k.basic_only = spec.basic_only;
    k.share      = spec.share_land_type;
    k.pruned     = LandSearchPickPruned();
    bool ok = true;
    if (spec.count > 0)
    {
        const Player& pl = s.players[static_cast<std::size_t>(ctrl)];
        for (std::size_t i = 0; i < pl.library.size(); ++i)
        {
            const Card& c = pl.library[i];
            const CardDefinition* d = CardDatabase::Instance().LookupCached(c);
            if (!LandSearchLegal(d, spec)) { continue; }
            int j = 0;
            while (j < k.n && k.defs[j] != d) { ++j; }
            if (j == k.n)
            {
                if (k.n == landsearchmemo::kMaxDefs) { ok = false; break; }
                k.defs[j] = d; k.copies[j] = 0; ++k.n;
            }
            ++k.copies[j];
        }
    }
    const DecisionProvider& prov = ResolveProvider(s);
    if (ok) { ok = prov.LandSearchPickOrderKey(s, ctrl, k.prov_key); }
    if (!ok)
    {
        ++landsearchmemo::t_bypass;
        t_bypass_out.Fill(LandSearchPicksUncached(s, ctrl, spec), spec);
        return t_bypass_out;
    }
    k.prov_type = typeid(prov).hash_code();
    // Canonical order for the exact key (pointer order: used for EQUALITY only, never for output).
    for (int a = 1; a < k.n; ++a)
    {
        const CardDefinition* dv = k.defs[a];
        const int             cv = k.copies[a];
        int b = a - 1;
        while (b >= 0 && std::less<const CardDefinition*>()(dv, k.defs[b]))
        { k.defs[b + 1] = k.defs[b]; k.copies[b + 1] = k.copies[b]; --b; }
        k.defs[b + 1] = dv; k.copies[b + 1] = cv;
    }
    landsearchmemo::Slot& slot = landsearchmemo::t_slots[k.Hash() % landsearchmemo::kSlots];
    if (slot.valid && slot.key == k) { ++landsearchmemo::t_hits; return slot; }
    ++landsearchmemo::t_misses;
    slot.Fill(LandSearchPicksUncached(s, ctrl, spec), spec);
    slot.key   = k;
    slot.valid = true;
    return slot;
}
inline const std::vector<std::vector<std::string>>& LandSearchPicksMemo(const GameState& s, int ctrl,
                                                                        const LandSearchSpec& spec)
{
    return LandSearchMemoSlot(s, ctrl, spec).picks;
}

// By value (tests, the human prompt's default, anything that keeps the list): a copy of the memo.
inline std::vector<std::vector<std::string>> LandSearchPicks(const GameState& s, int ctrl,
                                                             const LandSearchSpec& spec)
{
    return LandSearchPicksMemo(s, ctrl, spec);
}

// ---- THE LAND-PUT MANA CREDIT (land-search gap 2, 2026-10-15) ----------------------------------------
// Search for Tomorrow HARD-CAST puts its basic UNTAPPED, so the land is mana for the SAME main phase:
// turn 4 "Search for Tomorrow, then Rampant Growth / Khalni Heart Expedition paid with the fetched
// Forest" is a real line. The subset mana math never credited it (the Crop Rotation precedent), so the
// line was unenumerable at any budget. The credit rides Action::rock_mana -- the shape every producer
// consumer already agrees on ("a rock never funds its own cost", the odometer gain terms, the colour
// presence widening, FirstUnpayablePos, the funding-order hoist) -- and both real-payment walks put the
// land itself on their scratch board.
//
// THE PICK AND THE CREDIT ARE ONE DECISION. Which basic arrives is a SEARCHED pick, so a colour credit
// is only honest if the pick is bound to it: a {G} credit that the pick then spends on a Mountain is
// phantom mana. So the cast is emitted once per distinct CREDIT among the legal picks (one Forest
// class -> {G}, one Mountain class -> {R}), each variant crediting exactly its own colour, and its
// resolution RESTRICTS the pick to the picks earning that credit (LandSearchCreditFilter; the in-plan
// pin then indexes the restricted list). The variants partition the pick space, so every pick stays
// reachable -- the colour split simply replaces the in-plan pin's colour fan for this cast.
//
// FAIL TOWARD NOT CREDITING. A pick is credited only when every battlefield land of it is a BASIC with
// no enters-tapped rule (AddSourceToPool's own credit for it); one uncreditable pick (a non-basic, a
// tapland, nothing to find) withdraws the credit from the whole cast, so a basic-only untapped search is
// the only shape credited today. No basic in the library -> no credit at all.
inline bool LandSearchPutsUntapped(const CardDefinition& d)
{
    const CardParams& p = d.params;
    return p.land_search_on_resolve && p.land_search_count > 0 && !p.land_search_tapped
        && p.land_search_count - p.land_search_to_hand >= 1
        && (d.card.IsInstant() || d.card.IsSorcery());
}
inline bool LandSearchPickCredit(const GameState& s, const LandSearchSpec& spec,
                                 const std::vector<const CardDefinition*>& pick, ManaPool& out)
{
    out.Clear();
    if (spec.tapped) { return false; }
    const int bf = LandSearchBattlefieldCount(spec, static_cast<int>(pick.size()));
    if (bf <= 0) { return false; }
    for (int i = 0; i < bf; ++i)
    {
        const CardDefinition* d = pick[static_cast<std::size_t>(i)];
        if (d == nullptr || !IsBasicLandCard(*d) || d->params.enters_tapped
            || d->params.enters_tapped_with_depletion > 0 || d->params.produces.empty())
        { return false; }
        AddSourceToPool(out, s, *d);
    }
    return out.Total() > 0;
}
// The distinct credits of every legal pick, in pick-list order (the provider's front pick's credit
// first). EMPTY when any pick is uncreditable or nothing is legal (see above).
inline std::vector<ManaPool> LandSearchCreditGroups(const GameState& s, int ctrl, const LandSearchSpec& spec)
{
    std::vector<ManaPool> out;
    const landsearchmemo::Slot& ms = LandSearchMemoSlot(s, ctrl, spec);
    ManaPool c;
    for (const std::vector<const CardDefinition*>& pd : ms.pick_defs)
    {
        if (!LandSearchPickCredit(s, spec, pd, c)) { out.clear(); return out; }
        if (std::find(out.begin(), out.end(), c) == out.end()) { out.push_back(c); }
    }
    return out;
}
// The indices of `ms`'s picks whose credit equals `credit` (the restricted list a credited cast's
// resolution and its pin index into).
inline void LandSearchCreditFilter(const GameState& s, const LandSearchSpec& spec,
                                   const landsearchmemo::Slot& ms, const ManaPool& credit,
                                   std::vector<int>& out)
{
    out.clear();
    ManaPool c;
    for (std::size_t i = 0; i < ms.pick_defs.size(); ++i)
    { if (LandSearchPickCredit(s, spec, ms.pick_defs[i], c) && c == credit) { out.push_back(static_cast<int>(i)); } }
}

// ---- THE EXTENDED PICK LIST: a PUT Karoo's bounce (land-search gap 3, 2026-10-15) ------------------
// Gruul Turf put by Primeval Titan's ETB or attack trigger returns a land the moment it lands, and
// WHICH land is a searched decision (Plan::bounce_choice searches it for a Karoo DROP); a put took
// BounceSearchCandidates' front -- an in-window greedy pick. The bounce exists only for a pick holding
// the Karoo, so it is a SUB-CHOICE of the pick, and it rides the pick's own pin: after the list's W
// picks come kLandSearchBounceStride extra entries per Karoo-bearing pick (Slot::karoo_picks, in list
// order) -- entry W + r*S + (b-1) is "the r-th Karoo pick, and its first Karoo returns candidate b of
// BounceSearchCandidates on the state AT THE BOUNCE" (clamped there, like the drop's pin). Every pin
// value below W means exactly what it always did, and with no Karoo in the list the extended list IS
// the list, so a deck without a fetchable Karoo is byte-identical. One axis, not two: a bounce variant
// is a pick variant (the in-plan pins, Titan's packed attack stream), so every plan identity, key and
// combat-pin share already carries it. Sized at the fan by PutKarooBounceBound (TurnSolver.cpp).
constexpr int kLandSearchBounceStride = static_cast<int>(DecisionProvider::kBounceSearchWidth) - 1;
struct LandSearchPinPick { int pick = 0; int bounce = -1; };
inline int LandSearchExtendedWidth(const landsearchmemo::Slot& ms)
{
    return static_cast<int>(ms.picks.size())
         + static_cast<int>(ms.karoo_picks.size()) * kLandSearchBounceStride;
}
// The pin value of "the r-th Karoo pick, bounce candidate b (>= 1)" in a list of `w` picks.
inline int LandSearchBouncePinValue(int w, int karoo_rank, int b)
{
    return w + karoo_rank * kLandSearchBounceStride + (b - 1);
}
inline LandSearchPinPick LandSearchPinDecode(const landsearchmemo::Slot& ms, int pin)
{
    LandSearchPinPick out;
    if (pin < 0 || ms.picks.empty()) { return out; }
    const int w = static_cast<int>(ms.picks.size());
    const int p = std::min(pin, LandSearchExtendedWidth(ms) - 1);   // duplicate-not-whiff clamp
    if (p < w) { out.pick = p; return out; }
    const int e = p - w;
    out.pick   = ms.karoo_picks[static_cast<std::size_t>(e / kLandSearchBounceStride)];
    out.bounce = e % kLandSearchBounceStride + 1;
    return out;
}

// The HUMAN's k-th prompt: every legal NAME still available given the earlier picks (copies left;
// the shared land type with every earlier pick). Real names, never the identity fold -- a human sees
// Evolving Wilds and Terramorphic Expanse as the two cards they are. Sorted by name so the offered
// list never leaks the hidden library order.
inline std::vector<std::string> LandSearchHumanCandidates(const GameState& s, int ctrl,
                                                          const LandSearchSpec& spec,
                                                          const std::vector<std::string>& chosen)
{
    const Player& pl = s.players[static_cast<std::size_t>(ctrl)];
    std::vector<std::string> out;
    for (std::size_t i = 0; i < pl.library.size(); ++i)
    {
        const Card& c = pl.library[i];
        const CardDefinition* d = CardDatabase::Instance().LookupCached(c);
        if (!LandSearchLegal(d, spec)) { continue; }
        const std::string nm = c.m_name.str();
        if (std::find(out.begin(), out.end(), nm) != out.end()) { continue; }
        int in_lib = 0;
        for (std::size_t j = 0; j < pl.library.size(); ++j) { if (pl.library[j].m_name == nm) { ++in_lib; } }
        const int taken = static_cast<int>(std::count(chosen.begin(), chosen.end(), nm));
        if (in_lib - taken <= 0) { continue; }
        if (spec.share_land_type)
        {
            bool ok = true;
            for (const std::string& o : chosen)
            {
                if (!LandDefsShareLandType(CardDatabase::Instance().Lookup(o), d)) { ok = false; break; }
            }
            if (!ok) { continue; }
        }
        out.push_back(nm);
    }
    std::sort(out.begin(), out.end());
    return out;
}

// Take one card named `name` out of `pl`'s library (the first copy); failing that, a behavioural twin
// of it (the identity class the autonomous pick was folded over). False if neither is left.
inline bool TakeLandFromLibrary(Player& pl, const std::string& name, Card& out)
{
    for (std::size_t i = 0; i < pl.library.size(); ++i)
    {
        if (pl.library[i].m_name == name)
        {
            out = pl.library[i];
            pl.library.erase(pl.library.begin() + static_cast<std::ptrdiff_t>(i));
            return true;
        }
    }
    const CardDefinition* want = CardDatabase::Instance().Lookup(name);
    if (want == nullptr || want->behaviour_identity == 0) { return false; }
    for (std::size_t i = 0; i < pl.library.size(); ++i)
    {
        const CardDefinition* d = CardDatabase::Instance().LookupCached(pl.library[i]);
        if (d != nullptr && d->behaviour_identity == want->behaviour_identity)
        {
            out = pl.library[i];
            pl.library.erase(pl.library.begin() + static_cast<std::ptrdiff_t>(i));
            return true;
        }
    }
    return false;
}

// TakeLandFromLibrary by DEFINITION: the same two passes, matched on the cached definition pointer
// instead of the name string (a library card's definition IS its name's entry, so `LookupCached(c) ==
// want` holds exactly when `c.m_name == want's name`). Reads the library through operator[] in the
// same order, so the index-carry recorder sees the same accesses.
inline bool TakeLandFromLibraryDef(Player& pl, const CardDefinition* want, Card& out)
{
    if (want == nullptr) { return false; }
    for (std::size_t i = 0; i < pl.library.size(); ++i)
    {
        if (CardDatabase::Instance().LookupCached(pl.library[i]) == want)
        {
            out = pl.library[i];
            pl.library.erase(pl.library.begin() + static_cast<std::ptrdiff_t>(i));
            return true;
        }
    }
    if (want->behaviour_identity == 0) { return false; }
    for (std::size_t i = 0; i < pl.library.size(); ++i)
    {
        const CardDefinition* d = CardDatabase::Instance().LookupCached(pl.library[i]);
        if (d != nullptr && d->behaviour_identity == want->behaviour_identity)
        {
            out = pl.library[i];
            pl.library.erase(pl.library.begin() + static_cast<std::ptrdiff_t>(i));
            return true;
        }
    }
    return false;
}

// THE land-search resolution. `pin` = the searched pick index (-1 = the provider's front). Shared by
// every world: the executor (EffectHandler / AIEngine via ApplyPermAbility / GameEngine combat and
// upkeep), the rollout (apply_one / SimulateCombat / SimulateEndAndStartNextTurn) and human play.
//
// HUMAN PLAY (g_play_tutor_chooser set -- null in every search / rollout / enumeration scope, so the
// autonomous engine never reaches it): one `tutor_etb` prompt PER LAND, in destination order, each
// over the live legal names given the earlier picks; -1 on the first prompt of an optional search
// DECLINES it (no shuffle -- nothing was searched), any other -1 stops early (a search for cards with
// a stated quality may fail to find, CR 701.19b). The default is the autonomous pick.
inline void PerformLandSearch(GameState& state, int controller, const LandSearchSpec& spec,
                              const std::string& source, int pin, const char* source_suffix,
                              const ManaPool* put_credit)
{
    if (spec.count <= 0) { return; }
    Player& pl = state.players[static_cast<std::size_t>(controller)];
    // THE CREDITED CAST'S RESTRICTION (land-search gap 2): the picks earning exactly the credit the
    // cast was enumerated with. Empty restriction (no credit, or nothing earns it any more -- a
    // same-plan search took the last copy) = the whole list, so a credited cast never whiffs.
    const bool credited = put_credit != nullptr && put_credit->Total() > 0;
    thread_local std::vector<int> t_credit_idx;
    t_credit_idx.clear();
    // The put Karoo's searched bounce (land-search gap 3, the extended pick list); -1 = the front.
    int bounce_pin = -1;
    // The put-bounce fold's probe (SpellEffects.h PutBounceProbe): armed, and naming an attack-stream
    // ordinal, only around a search candidate's combat. What the observation needs from the list.
    PutBounceProbe* probe = nullptr;
    PutBounceObs probe_obs;
    if (g_put_bounce_probe != nullptr && g_put_bounce_probe->target == &state && !credited)
    {
        PutBounceProbe* pr = g_put_bounce_probe;
        if (!pr->in_plan && pr->ordinal >= 0)
        {
            probe = pr;
            probe_obs.ordinal = pr->ordinal;
            pr->ordinal = -1;   // this search only (a nested one is never the probed ordinal)
        }
        else if (pr->in_plan && g_in_plan_ls_last >= pr->count0)
        {
            probe = pr;
            probe_obs.ordinal = g_in_plan_ls_last - pr->count0;
        }
    }
    g_in_plan_ls_last = -1;   // consumed by THIS search (an unpinned later one never inherits it)
    // The source's display label ("Primeval Titan (attack)"), built only where something shows it --
    // the human prompt and the play-event line -- never on the autonomous path.
    auto label = [&]() { return source_suffix != nullptr ? source + source_suffix : source; };
    // The chosen lands' DEFINITIONS, battlefield entries first, then hand entries. A small inline
    // buffer: the autonomous path resolves millions of these, and every pool pick holds <= 2 lands.
    constexpr int kInline = 8;
    const CardDefinition* chosen_buf[kInline] = {};
    std::vector<const CardDefinition*> chosen_big;
    const CardDefinition** chosen = chosen_buf;
    int n_chosen = 0;
    auto reserve_chosen = [&](std::size_t n)
    {
        if (n > static_cast<std::size_t>(kInline)) { chosen_big.assign(n, nullptr); chosen = chosen_big.data(); }
    };
    int bf_n = 0;
    if (g_play_tutor_chooser)
    {
        const std::vector<std::vector<std::string>> picks = LandSearchPicks(state, controller, spec);
        // The prompt's DEFAULT is the pick the credited cast was priced with (the human may answer
        // anything -- a human line is never restricted, it is the human's decision).
        std::size_t def_k = 0;
        if (credited)
        {
            LandSearchCreditFilter(state, spec, LandSearchMemoSlot(state, controller, spec), *put_credit,
                                   t_credit_idx);
            if (!t_credit_idx.empty()) { def_k = static_cast<std::size_t>(t_credit_idx.front()); }
        }
        // The picks hold CLASS REPRESENTATIVES (Evolving Wilds stands for Terramorphic Expanse too) and
        // the prompts offer REAL names, so the two are matched by identity class, never by name: with
        // the Wilds already taken, a {Wilds, Wilds} pick's second land IS the Terramorphic Expanse (USER
        // 2026-10-16, Landfall s5 gi4: the second Titan prompt defaulted to a Forest, entry 0, because
        // "Evolving Wilds" was no longer a candidate name). And the default follows the HUMAN's earlier
        // answers: it is the next land of the first pick -- the credited one, then list order -- that
        // holds every land chosen so far.
        auto same_class = [](const std::string& a, const std::string& b)
        {
            if (a == b) { return true; }
            const CardDefinition* da = CardDatabase::Instance().Lookup(a);
            const CardDefinition* db = CardDatabase::Instance().Lookup(b);
            return da != nullptr && db != nullptr && da->behaviour_identity != 0
                && da->behaviour_identity == db->behaviour_identity;
        };
        const int bf_slots = std::max(0, spec.count - spec.to_hand);
        std::vector<std::string> names;
        auto default_for = [&](const std::vector<std::string>& cands) -> int
        {
            for (std::size_t r = 0; r <= picks.size(); ++r)
            {
                const std::size_t pi = r == 0 ? def_k : r - 1;
                if (pi >= picks.size() || (r > 0 && pi == def_k)) { continue; }
                const std::vector<std::string>& pk = picks[pi];
                std::vector<char> used(pk.size(), 0);
                bool holds = true;
                for (const std::string& nm : names)
                {
                    bool hit = false;
                    for (std::size_t j = 0; j < pk.size() && !hit; ++j)
                    { if (!used[j] && same_class(pk[j], nm)) { used[j] = 1; hit = true; } }
                    if (!hit) { holds = false; break; }
                }
                if (!holds) { continue; }
                for (std::size_t j = 0; j < pk.size(); ++j)
                {
                    if (used[j]) { continue; }
                    for (std::size_t c = 0; c < cands.size(); ++c)
                    { if (same_class(cands[c], pk[j])) { return static_cast<int>(c); } }
                }
            }
            return 0;
        };
        for (int k = 0; k < spec.count; ++k)
        {
            const std::vector<std::string> cands =
                LandSearchHumanCandidates(state, controller, spec, names);
            if (cands.empty()) { break; }
            const int def_idx = default_for(cands);
            std::string lbl = label() + " -- " + (spec.basic_only ? "basic land" : "land");
            if (k < bf_slots) { lbl += spec.tapped ? " onto the battlefield tapped" : " onto the battlefield"; }
            else              { lbl += " into your hand"; }
            if (spec.count > 1)
            { lbl += " (" + std::to_string(k + 1) + " of up to " + std::to_string(spec.count) + ")"; }
            const int got = (*g_play_tutor_chooser)(state, controller, lbl, cands, def_idx, {});
            if (got < 0 || got >= static_cast<int>(cands.size()))
            {
                if (k == 0 && spec.optional) { return; }   // declined "you may search": no shuffle
                break;                                     // stop early ("up to" / fail to find)
            }
            names.push_back(cands[static_cast<std::size_t>(got)]);
        }
        reserve_chosen(names.size());
        for (const std::string& nm : names) { chosen[n_chosen++] = CardDatabase::Instance().Lookup(nm); }
        bf_n = LandSearchBattlefieldCount(spec, n_chosen);
    }
    else
    {
        const landsearchmemo::Slot& ms = LandSearchMemoSlot(state, controller, spec);
        if (credited) { LandSearchCreditFilter(state, spec, ms, *put_credit, t_credit_idx); }
        if (!ms.picks.empty())
        {
            std::size_t k = 0;
            if (!t_credit_idx.empty())
            {
                // A credited cast: the pin indexes the RESTRICTED list (clamped).
                const std::size_t r = pin < 0 ? 0
                    : std::min<std::size_t>(static_cast<std::size_t>(pin), t_credit_idx.size() - 1);
                k = static_cast<std::size_t>(t_credit_idx[r]);
            }
            else
            {
                const LandSearchPinPick pp = LandSearchPinDecode(ms, pin);
                k          = static_cast<std::size_t>(pp.pick);
                bounce_pin = pp.bounce;
            }
            if (probe != nullptr)
            {
                probe_obs.w       = static_cast<int>(ms.picks.size());
                probe_obs.karoo   = ms.karoo_picks;
                probe_obs.pick    = static_cast<int>(k);
            }
            // Copied at once: the memo reference is valid only until its next call.
            const std::vector<const CardDefinition*>& pd = ms.pick_defs[k];
            reserve_chosen(pd.size());
            for (const CardDefinition* d : pd) { chosen[n_chosen++] = d; }
        }
        bf_n = LandSearchBattlefieldCount(spec, n_chosen);
    }

    // The play-event text is assembled only when an event sink exists (human play / logged games);
    // the line itself is emitted under the same test as before, at the end.
    const bool texts = g_play_event_sink != nullptr;
    // PASS 1 -- every battlefield land enters (one simultaneous event, CR 603.6a). EnterLand without
    // the extras: the land's own enter triggers wait for pass 2.
    int                   bf_nums_buf[kInline];
    const CardDefinition* bf_defs_buf[kInline];
    std::vector<int>                   bf_nums_big;
    std::vector<const CardDefinition*> bf_defs_big;
    int*                   bf_nums = bf_nums_buf;
    const CardDefinition** bf_defs = bf_defs_buf;
    if (bf_n > kInline)
    {
        bf_nums_big.assign(static_cast<std::size_t>(bf_n), 0);
        bf_defs_big.assign(static_cast<std::size_t>(bf_n), nullptr);
        bf_nums = bf_nums_big.data();
        bf_defs = bf_defs_big.data();
    }
    int n_bf = 0;
    std::string bf_text, hand_text;
    for (int i = 0; i < bf_n; ++i)
    {
        Card lc;
        if (!TakeLandFromLibraryDef(pl, chosen[i], lc)) { continue; }
        const CardDefinition* d = CardDatabase::Instance().LookupCached(lc);
        if (d == nullptr) { continue; }   // unreachable: legality read the definition
        EnterLand(state, *d, lc.m_number, /*resolve_full_etb=*/false, /*fire_landfall=*/true,
                  spec.tapped ? LandEntryTap::ForcedTapped : LandEntryTap::Natural);
        // Forbidden Orchard's on-play spawn, mirrored as Crop Rotation does -- only for an UNTAPPED
        // entry (it models "tapped for mana this turn"); a forced-tapped Orchard taps next turn and
        // the turn-start spawn covers it. Unreachable for every basic-only search.
        if (!spec.tapped && IsForbiddenOrchard(d)) { SpawnOpponentSpirit(state); }
        bf_nums[n_bf] = lc.m_number;
        bf_defs[n_bf] = d;
        ++n_bf;
        if (texts) { bf_text += (bf_text.empty() ? "" : ", ") + lc.m_name.str(); }
    }
    // PASS 2 -- the put lands' own enter triggers (Karoo bounce, land lifegain), after all of them are
    // on the battlefield. A trigger still resolves for a land that has already left (an earlier
    // Karoo returned it): the lifegain does, the bounce (which needs its own source on the
    // battlefield to pick "a land you control" sensibly) is skipped -- no pool card can reach that.
    for (int i = 0; i < n_bf; ++i)
    {
        int slot = -1;
        for (int j = 0; j < static_cast<int>(state.battlefield.size()); ++j)
        {
            if (state.battlefield[static_cast<std::size_t>(j)].card.m_number == bf_nums[i]
                && state.battlefield[static_cast<std::size_t>(j)].controller_index == controller)
            { slot = j; break; }
        }
        // The probed bounce (put-bounce fold): the pick's FIRST Karoo, whatever its pin.
        const bool probed = probe != nullptr && probe_obs.pick >= 0 && slot >= 0
                          && bf_defs[i]->params.etb_bounce_land;
        if (probed) { g_put_bounce_live = true; g_put_bounce_width = -1; g_put_bounce_index = -1; }
        if (slot >= 0 && bounce_pin >= 1 && bf_defs[i]->params.etb_bounce_land)
        {
            // The searched bounce sub-choice binds the pick's FIRST Karoo (a second Karoo of the same
            // pick takes the front: one sub-choice per pick, disclosed in land-search-cluster.md).
            ScriptedBounceChoice _sbc(bounce_pin);
            bounce_pin = -1;
            ResolveLandEnterEtbExtras(state, *bf_defs[i], slot);
        }
        else if (slot >= 0 && bf_defs[i]->params.etb_bounce_land)
        {
            // An unpinned fetched Karoo takes the front -- and never spends the PLAN's put pin
            // (ScriptedPutBounce: that one belongs to a non-search put, Genesis Wave's).
            ScriptedPutBounce _spb(-1);
            ResolveLandEnterEtbExtras(state, *bf_defs[i], slot);
        }
        else if (slot >= 0) { ResolveLandEnterEtbExtras(state, *bf_defs[i], slot); }
        else if (bf_defs[i]->params.etb_lifegain > 0)
        { GainLife(state, state.active_player_index, bf_defs[i]->params.etb_lifegain); }
        if (probed)
        {
            g_put_bounce_live = false;
            if (g_put_bounce_width >= 0)
            {
                probe_obs.width = g_put_bounce_width;
                probe_obs.index = g_put_bounce_index;
                probe->obs.push_back(probe_obs);
            }
            probe_obs.pick = -1;   // the first Karoo only
        }
    }
    // Hand entries (Cultivate). EnterHand(Tutor) arms the put-in-hand breakpoint (site 10), so the
    // fetched basic can still be the turn's land drop.
    for (int i = bf_n; i < n_chosen; ++i)
    {
        Card lc;
        if (!TakeLandFromLibraryDef(pl, chosen[i], lc)) { continue; }
        if (texts) { hand_text += (hand_text.empty() ? "" : ", ") + lc.m_name.str(); }
        EnterHand(state, controller, std::move(lc), HandEntryReason::Tutor);
    }
    if (g_play_event_sink && !g_tap_speculating)
    {
        std::string t = "\xF0\x9F\x94\x8E " + label() + ": ";
        if (bf_text.empty() && hand_text.empty()) { t += "found nothing"; }
        if (!bf_text.empty())
        { t += bf_text + (spec.tapped ? " onto the battlefield tapped" : " onto the battlefield"); }
        if (!hand_text.empty()) { t += (bf_text.empty() ? "" : "; ") + hand_text + " into hand (revealed)"; }
        EmitPlayEvent(state.turn_number, "tutor", t);
    }
    // "...then shuffle" -- exactly ONCE, also on a fail-to-find (CR 701.19).
    ShuffleAfterSearch(state, controller);
}

// The two activated land searches (PermAbilityMode::SacLandSearch / QuestSacLandSearch), called from
// ApplyPermAbility AFTER the caller paid the mana and the {T}. The rest of the COST is paid here,
// before the effect (CR 602.2b / 601.2h): remove the quest counters, then SACRIFICE the source --
// graveyard (a token would cease to exist), erase, detach (CR 301.5c), the sacrifice watchers (a
// sacrificed land/enchantment is not a creature dying: no OnCreatureDies, the Blighted Steppe
// resolver's pattern). Then the search, with the in-plan pin.
// The explicit-pin core: the in-plan activation consumes the in-plan pin list (below); the in-combat
// Khalni crack (FireCombatLandSearchCracks) passes the next ordinal of the packed attack stream.
inline void ApplyLandSearchActivationPinned(GameState& state, int controller, int src_idx,
                                            const CardDefinition& def, int pin)
{
    const CardParams& p = def.params;
    if (src_idx < 0 || src_idx >= static_cast<int>(state.battlefield.size())) { return; }
    // A reference into the name registry (InternedName::str), stable after the source is sacrificed.
    const std::string& src_name = state.battlefield[static_cast<std::size_t>(src_idx)].card.m_name.str();
    if (p.land_search_activation_remove_quest > 0)
    {
        Permanent& q = state.battlefield[static_cast<std::size_t>(src_idx)];
        if (q.quest_counters < p.land_search_activation_remove_quest) { return; }   // stale plan: cannot pay
        q.quest_counters -= p.land_search_activation_remove_quest;   // a cost is never doubled
    }
    if (p.land_search_activation_sacrifice_self)
    {
        const Permanent& sq  = state.battlefield[static_cast<std::size_t>(src_idx)];
        const int        num = sq.card.m_number;
        const Card       dead = sq.card;
        const bool       tok  = sq.is_token;
        state.battlefield.erase(state.battlefield.begin() + src_idx);
        for (Permanent& o : state.battlefield)
        {
            if (o.equipped_to      == num) { o.equipped_to      = 0; }
            if (o.aura_attached_to == num) { o.aura_attached_to = 0; }
        }
        if (!tok) { state.players[static_cast<std::size_t>(controller)].graveyard.push_back(dead); }
        if (g_play_event_sink && !g_tap_speculating)
        { EmitPlayEvent(state.turn_number, "sacrifice", "\xF0\x9F\x94\xAA " + src_name + " -- sacrificed (cost)"); }
        FireSacrificeWatchers(state, controller);
    }
    PerformLandSearch(state, controller, LandSearchSpecOf(p), src_name, pin);
}
inline void ApplyLandSearchActivation(GameState& state, int controller, int src_idx,
                                      const CardDefinition& def)
{
    const int pin = ConsumeScriptedLandSearchPin();   // the activation holds its ordinal regardless
    ApplyLandSearchActivationPinned(state, controller, src_idx, def, pin);
}

// Re-find a list of permanents by card NUMBER after the battlefield was appended to / erased from
// (a land search appends lands; a fetched Karoo's bounce erases one). Both operations keep the
// relative order of the survivors, so the j-th occurrence of a number among `nums` maps to the j-th
// battlefield permanent carrying it -- exact even if two entries share a number. A permanent that
// is gone is dropped (an attacker cannot leave this way; the guard is for completeness).
inline std::vector<int> RefindByNumberInOrder(const GameState& state, const std::vector<int>& nums)
{
    std::vector<int> out;
    out.reserve(nums.size());
    for (std::size_t i = 0; i < nums.size(); ++i)
    {
        int seen_before = 0;
        for (std::size_t k = 0; k < i; ++k) { if (nums[k] == nums[i]) { ++seen_before; } }
        int hit = -1;
        for (int j = 0; j < static_cast<int>(state.battlefield.size()); ++j)
        {
            if (state.battlefield[static_cast<std::size_t>(j)].card.m_number != nums[i]) { continue; }
            if (seen_before-- == 0) { hit = j; break; }
        }
        if (hit >= 0) { out.push_back(hit); }
    }
    return out;
}

// THE COMBAT LANDFALL-TARGET RANK (GameState::scripted_combat_landfall_target, Plan::
// combat_landfall_target_choice; 2026-10-17). The lands a combat puts (the two consumers below) fire
// their TARGETED landfall triggers (Phylath) here, after the main-phase apply returned -- so the
// main-phase policy (Plan::landfall_target_choice, ScriptedLandfallTarget) is not live, and every such
// trigger used to take the provider's front for every plan. This installs the main-1 plan's COMBAT rank
// for exactly these entries, -1 (the front) included: installing -1 too makes a projection that runs
// while a main-phase policy is live (CountAttackLandSearchPump under ApplyPlanDirect) resolve exactly as
// the real combat does. Turn-scoped like the policy it shadows; restores the outer pin on exit.
struct CombatLandfallTargetScope
{
    int saved, saved_turn;
    explicit CombatLandfallTargetScope(const GameState& s)
        : saved(g_landfall_target_pin), saved_turn(g_landfall_target_pin_turn)
    {
        g_landfall_target_pin      = s.scripted_combat_landfall_target;
        g_landfall_target_pin_turn = s.turn_number;
    }
    ~CombatLandfallTargetScope() { g_landfall_target_pin = saved; g_landfall_target_pin_turn = saved_turn; }
    CombatLandfallTargetScope(const CombatLandfallTargetScope&) = delete;
    CombatLandfallTargetScope& operator=(const CombatLandfallTargetScope&) = delete;
};

// Primeval Titan's ATTACK half ("whenever this creature enters OR ATTACKS"), at the CR 508.4 point
// in BOTH worlds (GameEngine::CombatPhase and TurnSolver's SimulateCombat call it right after
// ApplyAttackModalTriggers, before the attack-token block): once per DECLARED attacker carrying
// land_search_on_enter_or_attack, before any damage is read, so the landfall payloads of the fetched
// lands (Avenger's counters, Phylath's +4) land on the attacking Plants THIS combat. The pick is the
// packed GameState::scripted_attack_land_search (one ordinal per attacking Titan, in attacker
// order). A fetched Gruul Turf's bounce ERASES a battlefield entry, so `atk_idx` is rebuilt by card
// number afterwards (the FireAttackSacFling contract). Param-gated: inert for every other deck.
inline void FireAttackLandSearch(GameState& state, int controller, std::vector<int>& atk_idx)
{
    if (!state.deck_has_land_search) { return; }   // deck stamp: no land-search source can exist
    ++combatpin::t_reads;   // reads scripted_attack_land_search (GameState.h, COMBAT-PIN READ WITNESS)
    std::vector<int> nums;
    nums.reserve(atk_idx.size());
    bool any = false;
    for (int i : atk_idx)
    {
        if (i < 0 || i >= static_cast<int>(state.battlefield.size())) { nums.push_back(0); continue; }
        const Permanent& a = state.battlefield[static_cast<std::size_t>(i)];
        nums.push_back(a.card.m_number);
        if (a.def_absent || a.controller_index != controller) { continue; }
        const CardDefinition* d = CardDatabase::Instance().LookupCached(a.card);
        if (d != nullptr && d->params.land_search_on_enter_or_attack && d->params.land_search_count > 0)
        { any = true; }
    }
    if (!any) { return; }
    const CombatLandfallTargetScope _clt(state);   // the combat landfalls' target rank (above)
    int attack_ordinal = 0;   // the packed stream's ordinal (the put-bounce probe's key)
    for (std::size_t ai = 0; ai < nums.size(); ++ai)
    {
        const std::vector<int> now = RefindByNumberInOrder(state, nums);
        if (ai >= now.size()) { break; }
        const int slot = now[ai];
        if (state.battlefield[static_cast<std::size_t>(slot)].controller_index != controller) { continue; }
        const CardDefinition* d =
            CardDatabase::Instance().LookupCached(state.battlefield[static_cast<std::size_t>(slot)].card);
        if (d == nullptr || !d->params.land_search_on_enter_or_attack || d->params.land_search_count <= 0)
        { continue; }
        const int pin = ConsumePackedLandSearchPin(state.scripted_attack_land_search);
        PutBounceProbe* const probe = (g_put_bounce_probe != nullptr && g_put_bounce_probe->target == &state)
                                    ? g_put_bounce_probe : nullptr;
        if (probe != nullptr) { probe->ordinal = attack_ordinal; }
        PerformLandSearch(state, controller, LandSearchSpecOf(d->params),
                          d->card.m_name.str(), pin, " (attack)");
        if (probe != nullptr) { probe->ordinal = -1; }
        ++attack_ordinal;
    }
    // Repair the attacker indices (a Karoo bounce erased a slot; appended lands shifted nothing
    // before them, but the erase may have).
    atk_idx = RefindByNumberInOrder(state, nums);
}

// Is `p` a counter-costed land-search source (Khalni Heart Expedition) -- and is it crackable NOW?
inline bool IsQuestLandSearchSource(const Permanent& p, const CardDefinition*& d_out)
{
    d_out = nullptr;
    if (p.def_absent) { return false; }
    const CardDefinition* d = CardDatabase::Instance().LookupCached(p.card);
    if (d == nullptr || d->params.land_search_activation_remove_quest <= 0 || d->params.land_search_count <= 0)
    { return false; }
    d_out = d;
    return true;
}

// THE IN-COMBAT KHALNI CRACK (2026-10-15; follow-up (a) of the land-search design). Khalni Heart
// Expedition's "Remove three quest counters ... and sacrifice it: search for two basics" has no timing
// restriction, so after Primeval Titan's attack trigger resolves (its two lands' landfalls may have
// filled an Expedition) the active player gets priority in the declare-attackers step and may crack it
// BEFORE damage -- two more landfalls, whose payloads (Avenger's +1/+1 on every Plant, Phylath's +4)
// land on the ATTACKING Plants. A main-2 crack can never reach this combat, so without this window the
// line was unreachable at any budget.
//
// SEARCHED, never greedy: GameState::scripted_combat_land_search_crack (Plan::combat_crack_choice) is a
// BITMASK over the Khalni-type permanents in battlefield order at this point; bit i cracks the i-th one
// if it is eligible (counters at the count) when reached -- ascending, so an Expedition filled by an
// earlier crack's landfalls in the same window is still crackable. Cracking is weakly better than a
// main-2 crack but NOT than holding it for a later turn's payoff, so not cracking (no pin) stays a line.
// The cracks' land picks continue the packed ATTACK stream (after the Titans' ordinals). Human play
// asks through the shared `dig` chooser (the Arcanum Wings in-combat window's shape): the eligible
// Expeditions, -1 = do not crack, re-asked after each crack. Called right after FireAttackLandSearch in
// BOTH combat worlds and inside its PendingAttackDamage projection twin; repairs atk_idx by number.
inline void FireCombatLandSearchCracks(GameState& state, int controller, std::vector<int>& atk_idx)
{
    if (!state.deck_has_land_search) { return; }
    ++combatpin::t_reads;   // reads both combat pins (GameState.h, COMBAT-PIN READ WITNESS)
    const int mask = state.scripted_combat_land_search_crack;
    state.scripted_combat_land_search_crack = -1;   // consumed whether or not it fires
    const bool human = g_play_dig_chooser != nullptr && !g_tap_speculating;
    if (mask <= 0 && !human) { return; }
    std::vector<int> atk_nums;
    atk_nums.reserve(atk_idx.size());
    for (int i : atk_idx)
    {
        atk_nums.push_back((i >= 0 && i < static_cast<int>(state.battlefield.size()))
                               ? state.battlefield[static_cast<std::size_t>(i)].card.m_number : 0);
    }
    // The Khalni-type permanents in battlefield order (the mask's index space).
    std::vector<int> order;
    for (const Permanent& p : state.battlefield)
    {
        const CardDefinition* d = nullptr;
        if (p.controller_index == controller && IsQuestLandSearchSource(p, d)) { order.push_back(p.card.m_number); }
    }
    if (order.empty()) { return; }
    const CombatLandfallTargetScope _clt(state);   // the combat landfalls' target rank (see the struct)
    auto crack = [&](int num) -> bool
    {
        for (int j = 0; j < static_cast<int>(state.battlefield.size()); ++j)
        {
            const Permanent& p = state.battlefield[static_cast<std::size_t>(j)];
            if (p.card.m_number != num || p.controller_index != controller) { continue; }
            const CardDefinition* d = nullptr;
            if (!IsQuestLandSearchSource(p, d)) { return false; }
            if (p.quest_counters < d->params.land_search_activation_remove_quest) { return false; }
            ApplyLandSearchActivationPinned(state, controller, j, *d,
                                            ConsumePackedLandSearchPin(state.scripted_attack_land_search));
            return true;
        }
        return false;
    };
    bool any = false;
    if (!human)
    {
        for (int i = 0; i < static_cast<int>(order.size()) && i < 30; ++i)
        { if ((mask >> i) & 1) { any = crack(order[static_cast<std::size_t>(i)]) || any; } }
    }
    else
    {
        for (;;)
        {
            std::vector<Card> examined;
            std::vector<int>  nums;
            for (const Permanent& p : state.battlefield)
            {
                const CardDefinition* d = nullptr;
                if (p.controller_index != controller || !IsQuestLandSearchSource(p, d)) { continue; }
                if (p.quest_counters < d->params.land_search_activation_remove_quest) { continue; }
                examined.push_back(p.card);
                nums.push_back(p.card.m_number);
            }
            if (examined.empty()) { break; }
            std::vector<int> legal(examined.size());
            for (std::size_t k = 0; k < legal.size(); ++k) { legal[k] = static_cast<int>(k); }
            // DEFAULT = KEEP (-1), which is what the engine does with no pin: it cracks in combat only
            // when its search pinned the crack (Plan::combat_crack_choice), so an unpinned window holds
            // -- the old default 0 ("crack now") was labelled the AI's pick while the AI held (Landfall
            // claude-play sweep gi10, 2026-10-16). The "(combat crack)" tag selects the emitter's
            // accurate wording (main.cpp WriteDigDecisionJson) and the viewer's (digPanelHtml).
            const int picked = (*g_play_dig_chooser)(
                state, controller,
                "Khalni Heart Expedition (combat crack) -- crack it NOW, before combat damage",
                examined, legal, -1);
            if (picked < 0 || picked >= static_cast<int>(nums.size())) { break; }
            if (!crack(nums[static_cast<std::size_t>(picked)])) { break; }
            any = true;
        }
    }
    if (!any) { return; }
    atk_idx = RefindByNumberInOrder(state, atk_nums);
}
