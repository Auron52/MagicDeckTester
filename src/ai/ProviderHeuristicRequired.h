#pragma once

// "A PROVIDER HEURISTIC IS EXPECTED" -- decisions whose ENGINE default is the full SEARCHED menu, which a deck is
// expected to restrict / order with its own provider policy (docs/design/provider-heuristic-required.md).
//
// USER 2026-11-18 (Trench Gorger, which created the registry): "Maybe the best way to handle this is throwing an error
// if we don't implement a heuristic in the provider? Essentially we want the agent working on this to be forced to
// implement something because fully searched and what you mentioned are both unacceptable for normal usage."
//
// USER 2026-11-20 (SUPERSEDES the throw): "The default should be searched and probably the analysis should be given a
// warning if we don't override the policy. The expectation is that we will override it and so this should be a red
// flag."
//
// So: a REGISTRY of such decisions, each keyed on the card parameter that carries it; a provider CAPABILITY query
// (DecisionProvider::ImplementsRequiredHeuristic, false in the root / generic provider); the ENGINE's default for a
// registered decision under a provider that answers false is the searched menu (RequiredHeuristicPolicyLive is the
// query the engine sites read); and a RED-FLAG WARNING (WarnRequiredHeuristics, called at BatchRunner's job parse,
// GoldFishRunner::SetupGame and the --scenario route) that names every such gap. Nothing refuses to run.
// scripts/provider_audit.py --check and scripts/verify_deck.py's `provider_policy` gate turn the warning line into a
// visible FAIL.

#include <string>
#include <vector>

struct CardParams;
struct Decklist;
class DecisionProvider;

enum class RequiredHeuristic : int
{
    ExileLibraryLands = 0,   // Trench Gorger: etb_exile_library_lands_set_base_pt -- WHICH library lands to exile
    EtbDigPick,              // Acclaimed Contender: etb_dig_count -- WHICH dug card to take (EtbDigCandidates)
    LifegainCountersSpend,   // Nykthos Paragon: lifegain_counters_once_each_turn -- WHICH gain event spends the uses
    GenesisWavePut,          // Genesis Wave: reveal_x_put_permanents -- WHICH revealed permanents to put
    TutorHandPut,            // Ranger of Eos: etb_tutor_hand_count -- WHICH cards the multi-tutor fetches
    TutorToBattlefieldPut,   // Dragonstorm: tutor_to_battlefield -- WHICH Dragons enter, in what order
    HedronMillTarget,        // Hedron Crab: landfall_mill_target_player -- self / opponent per trigger
    RevivePick,              // Reveillark: ltb_return_creatures -- WHICH creature cards return
    _Count
};

struct RequiredHeuristicSpec
{
    RequiredHeuristic id;
    const char*       param;     // the cards.json parameter that carries the decision
    const char*       what;      // the decision, in words
    const char*       fallback;  // what the ENGINE does for a deck with no policy (the searched default, or what is owed)
    const char*       hook;      // the provider hook(s) a deck implements
    bool (*carried_by)(const CardParams&);
};

// Every registered decision. Adding one is a USER decision (docs/design/provider-heuristic-required.md).
const std::vector<RequiredHeuristicSpec>& RequiredHeuristicRegistry();
const RequiredHeuristicSpec& RequiredHeuristicSpecOf(RequiredHeuristic h);

// One line per (card name, decision) the deck holds (mainboard and sideboard) that `prov` does not implement.
// Empty = no red flag.
std::vector<std::string> RequiredHeuristicGaps(const Decklist& deck, const DecisionProvider& prov);

// The full red-flag message for `gaps` (names the deck, the provider, the hooks and the doc).
std::string RequiredHeuristicMessage(const std::vector<std::string>& gaps, const DecisionProvider& prov,
                                     const std::string& deck_label);

// THE ENGINE-SITE QUERY: is `prov`'s own policy for `h` in force? False = run the engine's searched default. False
// when the provider does not implement it, and for MEASUREMENT under MTG_UNPRUNED or MTG_UNPRUNE=heuristicrequired
// (which lift a deck's policy to the searched default, the Sea `seagorger` pattern applied to every registered
// decision whose engine site reads this query).
bool RequiredHeuristicPolicyLive(const DecisionProvider& prov, RequiredHeuristic h);

// MEASUREMENT ARM ONLY (MTG_REQHEUR_OLD_DEFAULT, per-job overridable): the pre-2026-11-20 engine defaults for a
// registered decision that has no deck policy (Acclaimed Contender's dig axis capped at the 3 front candidates in
// library order). Never a play setting; it exists so the searched default can be A/B'd against what it replaced.
bool RequiredHeuristicOldDefault();

// The red-flag warning. Gaps -> one `[provider-required] RED-FLAG <label> provider=<P>: <card> [<param>], ...` line to
// stderr, followed by the full message (with a label: every call; without one: once per process). Never throws.
void WarnRequiredHeuristics(const Decklist& deck, const DecisionProvider& prov, const std::string& deck_label);
