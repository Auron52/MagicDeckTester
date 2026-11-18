// "A provider heuristic is REQUIRED" -- see ProviderHeuristicRequired.h and docs/design/provider-heuristic-required.md.
#include "ProviderHeuristicRequired.h"

#include "DecisionProvider.h"
#include "DecisionProviders.h"   // DecisionUnpruned, UnprunedGate
#include "../cards/CardDatabase.h"
#include "../core/EnvFlags.h"
#include "../deck/DeckLoader.h"

#include <atomic>
#include <iostream>
#include <set>
#include <stdexcept>

namespace
{
bool CarriesExileLibraryLands(const CardParams& p) { return p.etb_exile_library_lands_set_base_pt; }
}  // namespace

const std::vector<RequiredHeuristicSpec>& RequiredHeuristicRegistry()
{
    // Every entry: a decision whose ENGINE menu is a disclosed generic shape that is not acceptable for normal play
    // (neither the full combinatorial menu nor the capped / ordered generic one), so a deck holding the card must
    // supply its own policy. New entries are the USER's call (see the doc's candidate list).
    static const std::vector<RequiredHeuristicSpec> k = {
        { RequiredHeuristic::ExileLibraryLands, "etb_exile_library_lands_set_base_pt",
          "\"search your library for any number of land cards, exile them\" -- WHICH lands to exile (the engine menu "
          "keeps 3+ lands only as one provider-ordered round-robin prefix per count, capped at 62 entries; the full "
          "keep-subset product is combinatorially huge)",
          "DecisionProvider::RestrictExileLibraryLandsMenu (and, for the beyond-horizon front, the same rule), then "
          "declare it with ImplementsRequiredHeuristic(RequiredHeuristic::ExileLibraryLands)",
          &CarriesExileLibraryLands },
    };
    return k;
}

std::vector<std::string> RequiredHeuristicGaps(const Decklist& deck, const DecisionProvider& prov)
{
    std::vector<std::string> out;
    std::set<std::pair<int, std::string>> seen;
    auto scan = [&](const std::vector<Card>& cards)
    {
        for (const Card& c : cards)
        {
            const CardDefinition* d = CardDatabase::Instance().LookupCached(c);
            if (d == nullptr) { continue; }
            for (const RequiredHeuristicSpec& r : RequiredHeuristicRegistry())
            {
                if (!r.carried_by(d->params) || prov.ImplementsRequiredHeuristic(r.id)) { continue; }
                const std::string name = d->card.m_name.str();
                if (!seen.insert({ static_cast<int>(r.id), name }).second) { continue; }
                out.push_back(name + " [" + r.param + "]: " + r.what + ". Implement: " + r.hook + ".");
            }
        }
    };
    scan(deck.mainboard);
    scan(deck.sideboard);
    return out;
}

std::string RequiredHeuristicMessage(const std::vector<std::string>& gaps, const DecisionProvider& prov,
                                     const std::string& deck_label)
{
    std::string m = "[provider-required] deck " + (deck_label.empty() ? std::string("(unnamed)") : deck_label)
                  + " holds a card whose decision REQUIRES a deck-specific provider heuristic, and its provider '"
                  + prov.Name() + "' does not implement it:\n";
    for (const std::string& g : gaps) { m += "  * " + g + "\n"; }
    m += "  Neither the full searched menu nor the engine's generic shape is acceptable for normal play (USER "
         "2026-11-18). Give the deck its own provider policy -- see docs/design/provider-heuristic-required.md.\n"
         "  Bypasses (never for normal play): human play (MTG_HUMAN_PLAY, the viewer); the unpruned measurement "
         "route (MTG_UNPRUNED, or MTG_UNPRUNE=heuristicrequired).";
    return m;
}

const char* RequiredHeuristicBypass()
{
    if (EnvSet("MTG_HUMAN_PLAY")) { return "MTG_HUMAN_PLAY"; }
    if (DecisionUnpruned()) { return "MTG_UNPRUNED"; }
    if (DecisionUnpruned(UnprunedGate::HeuristicRequired)) { return "MTG_UNPRUNE=heuristicrequired"; }
    return nullptr;
}

namespace
{
void EnforceGaps(const std::vector<std::string>& gaps, const DecisionProvider& prov, const std::string& deck_label,
                 const char* bypass)
{
    const std::string msg = RequiredHeuristicMessage(gaps, prov, deck_label);
    if (bypass == nullptr) { throw std::runtime_error(msg); }
    static std::atomic<bool> s_warned_unlabelled{ false };
    if (deck_label.empty() && s_warned_unlabelled.exchange(true)) { return; }
    std::cerr << "[provider-required] " << (deck_label.empty() ? std::string("(unnamed)") : deck_label)
              << " BYPASSED (" << bypass << "): the engine's menu, for measurement / human play only\n"
              << msg << "\n";
}

}  // namespace

void EnforceRequiredHeuristics(const Decklist& deck, const DecisionProvider& prov, const std::string& deck_label,
                               const char* bypass)
{
    const std::vector<std::string> gaps = RequiredHeuristicGaps(deck, prov);
    if (!gaps.empty()) { EnforceGaps(gaps, prov, deck_label, bypass); }
}

// The bypass is read only for a deck that HAS a gap, so the unprune gate probe never records the gate for a
// deck the gate cannot affect.
void EnforceRequiredHeuristics(const Decklist& deck, const DecisionProvider& prov, const std::string& deck_label)
{
    const std::vector<std::string> gaps = RequiredHeuristicGaps(deck, prov);
    if (!gaps.empty()) { EnforceGaps(gaps, prov, deck_label, RequiredHeuristicBypass()); }
}
