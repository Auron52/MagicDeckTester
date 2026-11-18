# "A provider heuristic is REQUIRED" -- the setup-time gate

Status: BUILT 2026-11-18 (branch `sea-sin-gorger`). First and only registered decision: Trench Gorger.

## Why

Some decisions have no acceptable engine-side shape. The full searched menu is combinatorially huge, and the cheap
generic shape the engine can build without deck knowledge is not close to correct. A deck holding such a card must
therefore carry its own provider policy, and the engine must refuse to play it otherwise.

USER, 2026-11-18, verbatim:

> For Trench Gorger this is a bit of a mess. It is one of those cases where we might want to consider it searched,
> but then would always want to prune it anyway. Maybe the best way to handle this is throwing an error if we don't
> implement a heuristic in the provider? Essentially we want the agent working on this to be forced to implement
> something because fully searched and what you mentioned are both unacceptable for normal usage.

> (i.e. searching a massive combinatoric set is not practical, but neither is removing them in library order anywhere
> near correct)

("What you mentioned" is the engine's current Gorger menu: for 3 or more KEPT lands only the count is searched, and
WHICH lands are kept is one provider-ordered round-robin prefix per count, capped at 62 entries -- ledger
`analysis-Sea Creatures.md` Q19(a).)

## The mechanism

All in `src/ai/ProviderHeuristicRequired.{h,cpp}`.

* **Registry** (`RequiredHeuristicRegistry`). One entry per decision: an id (`RequiredHeuristic`), the cards.json
  parameter that carries it, the decision in words, and the provider hook(s) to implement. Today:
  `ExileLibraryLands` <- `etb_exile_library_lands_set_base_pt` (Trench Gorger), hook
  `DecisionProvider::RestrictExileLibraryLandsMenu`.
* **Capability query** (`DecisionProvider::ImplementsRequiredHeuristic(RequiredHeuristic)`). The root / generic
  answer is `false`. A deck provider answers `true` only for a policy it really implements.
  `SeaCreaturesProvider` answers `true` for `ExileLibraryLands` (its USER rule: >= 6 library lands -> exile all,
  < 6 -> decline).
* **Setup-time check** (`EnforceRequiredHeuristics`). Scans the mainboard AND the sideboard (a wish can fetch the
  card). For every registered card the provider does not implement it throws `std::runtime_error` with a message
  naming the card, the parameter, the decision, the deck, the provider, the hook, this doc and the bypasses. Called:
  * `BatchRunner` `ParseJob`, right after the `[play] ... provider=` line: the whole batch is refused before any
    game runs (`mtg --batch` exits non-zero with `Error: [provider-required] ...`);
  * `GoldFishRunner::SetupGame`, the choke point every other game route goes through (single game, analyzer,
    mulligan generation, viewer);
  * the `--scenario` route.
  It reads nothing into the game state, so every deck plays byte-identically.
* **Bypasses -- the only two, both explicit:**
  * human play (`MTG_HUMAN_PLAY`, set by the viewer / `--claude-play`): the human chooses from the full list;
  * the unpruned measurement route: `MTG_UNPRUNED`, or `MTG_UNPRUNE=heuristicrequired` (the new gate
    `UnprunedGate::HeuristicRequired`). The engine's menu runs, for measurement only.
  A bypassed run prints `[provider-required] <job> BYPASSED (<bypass>)` plus the full message to stderr (per batch
  job; once per process from `SetupGame`).
* **CI / verify**: `scripts/provider_audit.py` runs its routing batch under `MTG_UNPRUNE=heuristicrequired` (so one
  bad deck cannot abort the audit of the others), reads the `BYPASSED` lines, and `--check` FAILS on any deck listed.
* **Tests**: `test/unit/test_provider_heuristic_required.cpp` (generic provider refused with the full message, Sea
  provider and a Gorger-free deck run, sideboard counted, both bypasses, `SetupGame` throws).

### Sea Creatures' own measurement levers

`MTG_SEA_GORGER_ALL=0` and `MTG_UNPRUNE=seagorger` still lift the Sea policy and restore the engine's searched menu.
They are kept, and are now defined as Sea's form of the unpruned measurement route (the same thing the global route
does for a deck with no policy); the Sea provider keeps declaring the capability under them. They are A/B levers,
never a normal-play setting.

## Adding an entry

Registering a decision is the USER's call: it makes every deck holding the card unplayable until its provider has a
policy. To add one: a `RequiredHeuristic` value, a registry row (param, words, hook, predicate), a deck provider
override of the hook plus `ImplementsRequiredHeuristic`, and a test.

## Candidates (survey 2026-11-18, NOT registered -- for the USER)

Decisions whose engine/root default is a disclosed generic shape (library order, a capped or round-robin prefix, a
single greedy pick) or whose full menu is combinatorially huge. None is registered; each would make the decks that
hold the card refuse to run until their providers add a policy.

| Card / hook | Engine or root default today | Why it fits the pattern |
|---|---|---|
| Genesis Wave -- `DecisionProvider::GenesisWavePutPicks` ("any number" of revealed permanents) | A disclosed rule, not a searched subset: put every legal revealed permanent except a creature whose mandatory ETB destroys one of ours; X = max (GenericProvider) | A searched subset is 2^X plan variants -- the precedent the Gorger menu cites |
| Ranger of Eos -- `TutorHandPutList` (multi-tutor to hand) | Library order over the legal cards, truncated to `max_puts` ("deliberately unopinionated") | Literally "in library order" |
| Dragonstorm -- `TutorToBattlefieldPutOrder` | Empty -> the engine's library-order enumeration of TutorCandidates | Library order decides which Dragons enter |
| Acclaimed Contender -- `EtbDigCandidates` | The FIRST legal match in look order (= library order); the header calls it "poor and retained only for byte-identity" | Library order |
| Nykthos Paragon -- `LifegainCountersSpendCount` | One greedy pick (spend every copy), deliberately not searched | A per-event spend axis is a Cartesian explosion |
| Activation counts -- `BlinkActivationCounts`, ActivatePump / ActivateRevealTop K | Generic 1..min(3, affordable) | A self-funding loop needs a deck provider to propose the go-off count |
| Hedron Crab -- `HedronMillTargetPolicyPeriod` | 0 = no narrowing policy (the comment: "the solver must not silently use a bounded sequence") | Per-trigger self/opponent targets are 2^N; Mill supplies period 4 |
| Reveillark -- `ReviveCandidates` | A deck-agnostic ranking (printed MV desc) | Weaker fit: ranked, not library order |
