// "A provider heuristic is EXPECTED" -- see ProviderHeuristicRequired.h and docs/design/provider-heuristic-required.md.
#include "ProviderHeuristicRequired.h"

#include "DecisionProvider.h"
#include "DecisionProviders.h"   // DecisionUnpruned, UnprunedGate
#include "HeuristicArm.h"
#include "../cards/CardDatabase.h"
#include "../core/EnvFlags.h"
#include "../deck/DeckLoader.h"

#include <atomic>
#include <iostream>
#include <set>

namespace
{
bool CarriesExileLibraryLands(const CardParams& p) { return p.etb_exile_library_lands_set_base_pt; }
bool CarriesEtbDig(const CardParams& p)            { return p.etb_dig_count > 0; }
bool CarriesLifegainCountersOnce(const CardParams& p) { return p.lifegain_counters_once_each_turn; }
bool CarriesGenesisWave(const CardParams& p)       { return p.reveal_x_put_permanents; }
bool CarriesTutorHand(const CardParams& p)         { return p.etb_tutor_hand_count > 0; }
bool CarriesTutorToBattlefield(const CardParams& p) { return p.tutor_to_battlefield; }
bool CarriesHedronTarget(const CardParams& p)      { return p.landfall_mill_target_player; }
bool CarriesRevive(const CardParams& p)            { return p.ltb_return_creatures > 0; }
}  // namespace

const std::vector<RequiredHeuristicSpec>& RequiredHeuristicRegistry()
{
    // Every entry: a decision whose full menu is large or whose cheap generic shape is near-arbitrary, so a deck holding
    // the card is EXPECTED to supply its own policy (USER 2026-11-20: "The expectation is that we will override it and
    // so this should be a red flag"). Registered 2026-11-18 (Gorger) and 2026-11-20 (the rest, the USER's survey list).
    // New entries are the USER's call. Order = the enum's.
    static const std::vector<RequiredHeuristicSpec> k = {
        { RequiredHeuristic::ExileLibraryLands, "etb_exile_library_lands_set_base_pt",
          "\"search your library for any number of land cards, exile them\" -- WHICH lands to exile",
          "the engine's searched menu: every count, but for 3+ kept lands WHICH lands is one round-robin prefix per "
          "count, capped at 62 entries (the full keep-subset product is combinatorially huge)",
          "DecisionProvider::RestrictExileLibraryLandsMenu, then ImplementsRequiredHeuristic(ExileLibraryLands)",
          &CarriesExileLibraryLands },
        { RequiredHeuristic::EtbDigPick, "etb_dig_count",
          "\"look at the top N, you may put a <subtype> card into your hand\" -- WHICH match to take",
          "the searched ETB-dig axis over EVERY legal match (it was the 3 front matches in LIBRARY order); beyond the "
          "horizon the playout still takes the first match in library order",
          "DecisionProvider::EtbDigCandidates (rank / restrict the matches; EtbDigSearchWidth to cap the axis), then "
          "ImplementsRequiredHeuristic(EtbDigPick)",
          &CarriesEtbDig },
        { RequiredHeuristic::LifegainCountersSpend, "lifegain_counters_once_each_turn",
          "\"you may put that many +1/+1 counters on each creature you control. Do this only once each turn\" -- WHICH "
          "life-gain event of the turn spends the unused uses",
          "NOT YET SEARCHED (owed, 2026-11-20 handoff): the old greedy still runs -- every unused use is spent on the "
          "first gain of the turn",
          "DecisionProvider::LifegainCountersSpendCount (a per-event count), then "
          "ImplementsRequiredHeuristic(LifegainCountersSpend)",
          &CarriesLifegainCountersOnce },
        { RequiredHeuristic::GenesisWavePut, "reveal_x_put_permanents",
          "\"put any number of permanent cards with mana value X or less from among them onto the battlefield\" -- "
          "WHICH revealed permanents to put",
          "NOT YET SEARCHED (owed, 2026-11-20 handoff; the full menu is 2^L subsets of the L legal reveals): the "
          "disclosed rule still runs -- every legal permanent except a creature whose mandatory ETB destroys one of ours",
          "DecisionProvider::GenesisWavePutPicks (the subset), then "
          "ImplementsRequiredHeuristic(GenesisWavePut)",
          &CarriesGenesisWave },
        { RequiredHeuristic::TutorHandPut, "etb_tutor_hand_count",
          "\"search your library for up to N <type> cards\" to HAND -- WHICH cards to fetch",
          "library order over the legal cards, truncated (no searched axis yet -- OWED, every holder has a policy)",
          "DecisionProvider::TutorHandPutList, then ImplementsRequiredHeuristic(TutorHandPut)",
          &CarriesTutorHand },
        { RequiredHeuristic::TutorToBattlefieldPut, "tutor_to_battlefield",
          "a tutor to the BATTLEFIELD for up to N cards (Dragonstorm) -- WHICH cards enter, in what order",
          "the engine's library-order enumeration of TutorCandidates (no searched axis yet -- OWED, every holder has a "
          "policy)",
          "DecisionProvider::TutorToBattlefieldPutOrder, then ImplementsRequiredHeuristic(TutorToBattlefieldPut)",
          &CarriesTutorToBattlefield },
        { RequiredHeuristic::HedronMillTarget, "landfall_mill_target_player",
          "\"target player mills\" on each landfall trigger -- self or opponent, per trigger",
          "no periodic target policy (HedronMillTargetPolicyPeriod 0): the engine's own target handling (every holder "
          "has a policy)",
          "DecisionProvider::HedronMillTargetPolicyPeriod, then ImplementsRequiredHeuristic(HedronMillTarget)",
          &CarriesHedronTarget },
        { RequiredHeuristic::RevivePick, "ltb_return_creatures",
          "\"return up to N target creature cards from your graveyard to the battlefield\" -- WHICH cards",
          "the deck-agnostic resolution ranking (printed mana value descending; no searched axis yet -- OWED, every "
          "holder has a policy)",
          "DecisionProvider::ReviveCandidates, then ImplementsRequiredHeuristic(RevivePick)",
          &CarriesRevive },
    };
    return k;
}

const RequiredHeuristicSpec& RequiredHeuristicSpecOf(RequiredHeuristic h)
{
    return RequiredHeuristicRegistry()[static_cast<std::size_t>(h)];
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
                out.push_back(name + " [" + r.param + "]: " + r.what + ". Running: " + r.fallback + ". Implement: "
                              + r.hook + ".");
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
    std::string m = "[provider-required] RED FLAG: deck " + (deck_label.empty() ? std::string("(unnamed)") : deck_label)
                  + " holds a card whose decision EXPECTS a deck-specific provider policy, and its provider '"
                  + prov.Name() + "' has none, so the ENGINE DEFAULT runs:\n";
    for (const std::string& g : gaps) { m += "  * " + g + "\n"; }
    m += "  USER 2026-11-20: \"The default should be searched ... The expectation is that we will override it and so "
         "this should be a red flag.\" Draft a provider policy for the USER -- see "
         "docs/design/provider-heuristic-required.md.";
    return m;
}

bool RequiredHeuristicPolicyLive(const DecisionProvider& prov, RequiredHeuristic h)
{
    if (!prov.ImplementsRequiredHeuristic(h)) { return false; }
    static const bool lifted = DecisionUnpruned() || DecisionUnpruned(UnprunedGate::HeuristicRequired);
    return !lifted;
}

bool RequiredHeuristicOldDefault()
{
    // MEASUREMENT ARM, DEFAULT OFF; =1 -> the pre-2026-11-20 engine defaults (see the header). Per-job via "flags".
    static const bool env = EnvOn("MTG_REQHEUR_OLD_DEFAULT");
    return heurarm::Flag(heurarm::REQHEUR_OLD_DEFAULT, env);
}

void WarnRequiredHeuristics(const Decklist& deck, const DecisionProvider& prov, const std::string& deck_label)
{
    const std::vector<std::string> gaps = RequiredHeuristicGaps(deck, prov);
    if (gaps.empty()) { return; }
    static std::atomic<bool> s_warned_unlabelled{ false };
    if (deck_label.empty() && s_warned_unlabelled.exchange(true)) { return; }
    // ONE machine-readable line first (provider_audit.py / verify_deck.py parse it), then the full message.
    std::string cards;
    for (const std::string& g : gaps)
    {
        const std::size_t cut = g.find("]:");
        cards += (cards.empty() ? "" : ", ") + g.substr(0, cut == std::string::npos ? g.size() : cut + 1);
    }
    std::cerr << "[provider-required] RED-FLAG " << (deck_label.empty() ? std::string("(unnamed)") : deck_label)
              << " provider=" << prov.Name() << ": " << cards
              << " -- no deck policy, the engine default runs\n"
              << RequiredHeuristicMessage(gaps, prov, deck_label) << "\n";
}
