#pragma once
// DIABOLIC REVELATION (CardParams::tutor_up_to_x): "Search your library for up to X cards, put those cards into your
// hand, then shuffle." -- the cast-time PACKAGE axis (docs/design/blood-sun-pipeline.md §4, Blood Sun step 6).
//
// THE SHAPE. Each autonomous Revelation cast Action carries its whole choice: chosen_x = |S| and the package S as
// concrete names on Action::tutor_target (EncodePackage; the Tooth and Nail encoded-names precedent). Resolution
// (PerformTutorPackageToHand, SpellEffects.h) moves those cards to hand and shuffles ONCE, in both worlds.
//
// THE CLASSES (§4.3). Library cards are grouped by identity: a land by its behaviour identity -- the mana-only
// projection's while Blood Sun suppresses land abilities AND no card that could remove that Blood Sun is reachable
// (fold F-R2: Lotus Vale == Lotus Field) -- and a nonland by name. Which copy of a class is fetched is visible only
// through the post-shuffle order (fold F-R1, the LandSearch.h copy identity), so a package is a multiset of classes.
// F-R3 (X = |S|: a larger X only spends mana) and F-R4 (X >= 1: X = 0 is a bare shuffle, useful only through
// clairvoyance) are applied by the emitter.
//
// THE ORDER AND THE WAVES (§4.4). For each X, the multisets of size X are enumerated in LEXICOGRAPHIC order over the
// provider's class ranking (DecisionProvider::RevelationClassOrder; ORDER only) -- the rank-lex enumeration is
// COMPLETE: it yields every multiset of size X within the per-class counts exactly once (unit-tested against the
// closed form, CountMultisets). Ordinal o of X's enumeration belongs to wave WaveOf(o): wave 0 = ordinals [0, K0),
// wave k = [K0 (2^k - 1), K0 (2^(k+1) - 1)). A normal enumeration admits wave 0 only; a host's Revelation wave phase
// (TurnSolver FSLineWin) re-enumerates wave k with waves 0..k admitted and keeps only the plans whose
// highest-wave Revelation cast is wave k, so wave 0 plus the wave phases partition the full package space exactly
// (the group-wave tranche shape). Each admitted wave is priced through TurnSolver::ChargeDecisionUnits (inert at
// an unlimited budget), and the wave phase never runs at a node that already proved a win THIS ROUND (the
// generator-stop: no line from a state at round R wins before R, so no later package can improve the objective).
#include "GameState.h"
#include "LandAbilityLoss.h"
#include "../cards/CardDatabase.h"
#include <algorithm>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace revpkg
{
// K0: wave 0's per-X package count (design §4.4).
constexpr int kWave0PerX = 4;

// Ordinal o of an X-enumeration -> its wave (0 for [0, K0), k for [K0 (2^k - 1), K0 (2^(k+1) - 1))).
inline int WaveOf(long long o)
{
    int k = 0;
    long long hi = kWave0PerX;
    while (o >= hi && k < 60) { ++k; hi = kWave0PerX * ((1LL << (k + 1)) - 1); }
    return k;
}
// The first ordinal past wave k.
inline long long WaveEnd(int k) { return kWave0PerX * ((1LL << (k + 1)) - 1); }

struct Class
{
    std::string key;                                    // identity key ("L<ident>" / "N<name>")
    const CardDefinition* def = nullptr;                // a representative definition (the first member's)
    std::vector<std::pair<std::string, int>> members;   // (name, copies in library), sorted by name
    int cap = 0;                                        // total copies in library
};

// Could this card remove an enchantment WE control (and so end a Blood Sun) other than by an effect that also
// exiles every hand (Worldfire: the fetched card is exiled with it)? CONSERVATIVE by construction: every param the
// pool knows to destroy / exile / bounce / sacrifice a non-land permanent of ours. Used only to switch fold F-R2 OFF.
inline bool CardCanEndOwnEnchantment(const CardParams& p)
{
    return p.destroy_all_enchantments || p.cast_trigger_exile_target_permanents > 0
        || p.cast_trigger_destroy_target_permanents > 0 || p.modal_exile_target_artifact_enchantment
        || p.etb_destroy_artifact_enchantment_land || p.etb_destroy_artifact_enchantment_optional
        || p.sac_outlet_destroy_artifact_enchantment_optional || p.sac_outlet_allows_enchantment
        || p.etb_destroy_own_noncreature_max > 0 || p.cast_trigger_bounce_nonland || p.etb_bounce_nonland
        || (p.exile_all_permanents && !p.exile_all_hands_and_graveyards);
}

// Fold F-R2's condition: a Blood Sun suppresses land abilities NOW, and no card of ours that could end it is
// reachable (hand, library, graveyard -- an Emrakul shuffle returns the graveyard --, battlefield, stack). Then a
// land fetched to hand keeps its mana-only projection until it is played (design §4.3). The goldfish opponent never
// casts a spell (standing decision), so its decklist cannot end the Blood Sun.
inline bool TwinFoldLive(const GameState& s, int controller)
{
    if (!LandNonManaAbilitiesSuppressed(s)) { return false; }
    const Player& pl = s.players[static_cast<std::size_t>(controller)];
    auto bad = [](const Card& c) -> bool
    {
        const CardDefinition* d = CardDatabase::Instance().LookupCached(c);
        return d != nullptr && CardCanEndOwnEnchantment(d->params);
    };
    for (const Card& c : pl.hand)      { if (bad(c)) { return false; } }
    for (const Card& c : pl.library)   { if (bad(c)) { return false; } }
    for (const Card& c : pl.graveyard) { if (bad(c)) { return false; } }
    for (const Permanent& p : s.battlefield)
    { if (p.controller_index == controller && !p.def_absent && bad(p.card)) { return false; } }
    for (const auto& e : s.stack) { if (bad(e.source)) { return false; } }
    return true;
}

// The library's classes, in CANONICAL order (by first member name). Deterministic in the library's CONTENT (never
// its order), so every enumeration of one node -- every land variant -- sees the same classes. `twin_forced` (a
// provider's RevelationTwinFoldForced, Blood Sun step 6c: the USER's "Lotus Field and Vale are the same" under Blood
// Sun) holds F-R2 whenever land abilities are suppressed, whatever TwinFoldLive's conservative scan finds.
inline std::vector<Class> BuildClasses(const GameState& s, int controller, bool twin_forced = false)
{
    const bool twin = twin_forced ? LandNonManaAbilitiesSuppressed(s) : TwinFoldLive(s, controller);
    std::vector<Class> out;
    for (const Card& c : s.players[static_cast<std::size_t>(controller)].library)
    {
        const CardDefinition* d = CardDatabase::Instance().LookupCached(c);
        const std::string& nm = c.m_name.str();
        std::string key;
        if (d != nullptr && d->card.IsLand())
        {
            const std::uint64_t id = twin ? EffectiveLandIdentity(s, *d) : d->behaviour_identity;
            key = id != 0 ? "L" + std::to_string(id) : "N" + nm;
        }
        else { key = "N" + nm; }
        auto it = std::find_if(out.begin(), out.end(), [&](const Class& k) { return k.key == key; });
        if (it == out.end()) { out.push_back(Class{ key, d, {}, 0 }); it = out.end() - 1; }
        auto mt = std::find_if(it->members.begin(), it->members.end(),
                               [&](const std::pair<std::string, int>& m) { return m.first == nm; });
        if (mt == it->members.end()) { it->members.emplace_back(nm, 1); } else { ++mt->second; }
        ++it->cap;
    }
    for (Class& k : out) { std::sort(k.members.begin(), k.members.end()); k.def = CardDatabase::Instance().Lookup(k.members.front().first); }
    std::sort(out.begin(), out.end(),
              [](const Class& a, const Class& b) { return a.members.front().first < b.members.front().first; });
    return out;
}

// RANK-LEX multiset enumeration over classes in rank order: count vectors c (sum X, 0 <= c[i] <= cap[i]) in
// lexicographically DESCENDING order of c, i.e. packages made of the best-ranked classes first. First() fills
// greedily from the front; Next() moves one pick from the rightmost position that can shed one to its suffix and
// refills the suffix greedily. Complete and duplicate-free (each composition is visited once, in a total order).
inline bool FirstFill(std::vector<int>& c, const std::vector<int>& cap, int X)
{
    c.assign(cap.size(), 0);
    int rem = X;
    for (std::size_t i = 0; i < cap.size() && rem > 0; ++i) { c[i] = std::min(cap[i], rem); rem -= c[i]; }
    return rem == 0;
}
inline bool NextLex(std::vector<int>& c, const std::vector<int>& cap)
{
    const int n = static_cast<int>(c.size());
    int suffix_sum = 0, suffix_cap = 0;
    for (int i = n - 2; i >= 0; --i)
    {
        suffix_sum += c[static_cast<std::size_t>(i + 1)];
        suffix_cap += cap[static_cast<std::size_t>(i + 1)];
        if (c[static_cast<std::size_t>(i)] > 0 && suffix_sum + 1 <= suffix_cap)
        {
            --c[static_cast<std::size_t>(i)];
            int rem = suffix_sum + 1;
            for (int j = i + 1; j < n; ++j)
            {
                const std::size_t uj = static_cast<std::size_t>(j);
                c[uj] = std::min(cap[uj], rem);
                rem -= c[uj];
            }
            return true;
        }
    }
    return false;
}
// The closed form the enumeration is tested against: the number of multisets of size X within the caps (the
// coefficient of t^X in prod_i (1 + t + ... + t^cap_i)).
inline long long CountMultisets(const std::vector<int>& cap, int X)
{
    std::vector<long long> w(static_cast<std::size_t>(X) + 1, 0);
    w[0] = 1;
    for (int ci : cap)
    {
        std::vector<long long> nw(w.size(), 0);
        for (int s = 0; s <= X; ++s)
        {
            if (w[static_cast<std::size_t>(s)] == 0) { continue; }
            for (int k = 0; k <= ci && s + k <= X; ++k)
            { nw[static_cast<std::size_t>(s + k)] += w[static_cast<std::size_t>(s)]; }
        }
        w.swap(nw);
    }
    return w[static_cast<std::size_t>(X)];
}

// Concrete names for a count vector over `ranked` (class indices in rank order): a class's picks take its members
// in name order (any choice is the same outcome by F-R1 / F-R2). Sorted, so the encoding is canonical.
inline std::vector<std::string> PackageNames(const std::vector<Class>& classes, const std::vector<int>& ranked,
                                             const std::vector<int>& c)
{
    std::vector<std::string> names;
    for (std::size_t r = 0; r < ranked.size(); ++r)
    {
        int want = c[r];
        for (const auto& m : classes[static_cast<std::size_t>(ranked[r])].members)
        {
            for (int k = 0; k < m.second && want > 0; ++k, --want) { names.push_back(m.first); }
            if (want == 0) { break; }
        }
    }
    std::sort(names.begin(), names.end());
    return names;
}

// The encoding on Action::tutor_target: "<wave>\x1d" + names joined by '\x1f'. The wave tag lets a host's wave phase
// keep exactly the plans whose highest-wave cast is the current wave; a package lies in one wave, so the tag is a
// function of the package and every signature that keys the string keys the package.
inline std::string EncodePackage(int wave, const std::vector<std::string>& names)
{
    std::string out = std::to_string(wave);
    out.push_back('\x1d');
    for (std::size_t i = 0; i < names.size(); ++i)
    {
        if (i) { out.push_back('\x1f'); }
        out += names[i];
    }
    return out;
}
inline bool IsPackage(const std::string& enc) { return enc.find('\x1d') != std::string::npos; }
inline int PackageWave(const std::string& enc)
{
    const std::size_t at = enc.find('\x1d');
    if (at == std::string::npos || at == 0) { return -1; }
    return std::atoi(enc.substr(0, at).c_str());
}
inline std::vector<std::string> DecodePackage(const std::string& enc)
{
    std::vector<std::string> names;
    const std::size_t at = enc.find('\x1d');
    if (at == std::string::npos) { return names; }
    std::size_t p = at + 1;
    while (p < enc.size())
    {
        const std::size_t e = enc.find('\x1f', p);
        names.push_back(enc.substr(p, e == std::string::npos ? std::string::npos : e - p));
        if (e == std::string::npos) { break; }
        p = e + 1;
    }
    return names;
}
}   // namespace revpkg
