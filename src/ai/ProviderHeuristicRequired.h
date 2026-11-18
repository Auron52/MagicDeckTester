#pragma once

// "A PROVIDER HEURISTIC IS REQUIRED" -- decisions whose ENGINE menu is not acceptable for normal play
// (docs/design/provider-heuristic-required.md).
//
// USER 2026-11-18 (Trench Gorger): "It is one of those cases where we might want to consider it searched, but then
// would always want to prune it anyway. Maybe the best way to handle this is throwing an error if we don't implement
// a heuristic in the provider? Essentially we want the agent working on this to be forced to implement something
// because fully searched and what you mentioned are both unacceptable for normal usage." -- "(i.e. searching a
// massive combinatoric set is not practical, but neither is removing them in library order anywhere near correct)"
//
// So: a REGISTRY of such decisions, each keyed on the card parameter that carries it; a provider CAPABILITY query
// (DecisionProvider::ImplementsRequiredHeuristic, false in the root / generic provider); and a SETUP-TIME check
// (EnforceRequiredHeuristics, called before any game of a deck is played: BatchRunner's job parse and
// GoldFishRunner::SetupGame) that REFUSES to run a deck holding a registered card under a provider that does not
// implement its heuristic. The ONLY bypasses, both explicit:
//   * human play (MTG_HUMAN_PLAY -- the viewer / --claude-play): the human chooses from the full list;
//   * the unpruned measurement route (MTG_UNPRUNED, or MTG_UNPRUNE=heuristicrequired): the engine's menu, for
//     measurement only. A bypassed run prints a `[provider-required] ... BYPASSED` warning, which
//     scripts/provider_audit.py --check turns into a failure.

#include <string>
#include <vector>

struct CardParams;
struct Decklist;
class DecisionProvider;

enum class RequiredHeuristic : int
{
    ExileLibraryLands = 0,   // Trench Gorger: etb_exile_library_lands_set_base_pt -- WHICH library lands to exile
    _Count
};

struct RequiredHeuristicSpec
{
    RequiredHeuristic id;
    const char*       param;    // the cards.json parameter that carries the decision
    const char*       what;     // the decision, in words
    const char*       hook;     // the provider hook(s) a deck must implement
    bool (*carried_by)(const CardParams&);
};

// Every registered decision. Adding one is a USER decision (docs/design/provider-heuristic-required.md).
const std::vector<RequiredHeuristicSpec>& RequiredHeuristicRegistry();

// One line per (card name, decision) the deck holds (mainboard and sideboard) that `prov` does not implement.
// Empty = the deck may run.
std::vector<std::string> RequiredHeuristicGaps(const Decklist& deck, const DecisionProvider& prov);

// The full refusal message for `gaps` (names the deck, the provider, the hooks, the doc and the bypasses).
std::string RequiredHeuristicMessage(const std::vector<std::string>& gaps, const DecisionProvider& prov,
                                     const std::string& deck_label);

// The bypass in force for this process, or nullptr: "MTG_HUMAN_PLAY", "MTG_UNPRUNED",
// "MTG_UNPRUNE=heuristicrequired".
const char* RequiredHeuristicBypass();

// The setup-time gate. Gaps and no bypass -> throws std::runtime_error(message) (the CLI prints it and exits
// non-zero). Gaps under a bypass -> prints a `[provider-required] <label> BYPASSED (<bypass>)` warning to stderr
// (with a label: every call; without one: once per process) and returns. `bypass` defaults to the process's.
void EnforceRequiredHeuristics(const Decklist& deck, const DecisionProvider& prov, const std::string& deck_label);
void EnforceRequiredHeuristics(const Decklist& deck, const DecisionProvider& prov, const std::string& deck_label,
                               const char* bypass);
