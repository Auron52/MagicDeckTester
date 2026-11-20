# "A provider heuristic is EXPECTED" -- searched default + a RED FLAG

Status: BUILT 2026-11-18 as a setup-time THROW (branch `sea-sin-gorger`, Trench Gorger only). REWORKED 2026-11-20
(branch `provreg-1120`) per the USER ruling below: no throw; the engine default is the searched menu; a deck with no
policy gets a RED FLAG. Eight decisions registered.

## Why

Some decisions have a menu that is combinatorially large, or a cheap generic shape that is near-arbitrary (library
order, one greedy pick). A deck holding such a card is EXPECTED to carry its own provider policy (restrict / order the
options), and the analysis must say loudly when it does not.

USER, 2026-11-18 (Trench Gorger, created the registry), verbatim:

> For Trench Gorger this is a bit of a mess. It is one of those cases where we might want to consider it searched,
> but then would always want to prune it anyway. Maybe the best way to handle this is throwing an error if we don't
> implement a heuristic in the provider? Essentially we want the agent working on this to be forced to implement
> something because fully searched and what you mentioned are both unacceptable for normal usage.

USER, 2026-11-20 (SUPERSEDES the throw), verbatim:

> The default should be searched and probably the analysis should be given a warning if we don't override the policy.
> The expectation is that we will override it and so this should be a red flag. I don't think Breaching Dragonstorm
> has an issue because they don't play the same cards. Knights looks like the worst offender here, which might help
> explain why Acclaimed Contender did so poorly in screens. Nykthos Paragon could also be an issue potentially.

## The mechanism (2026-11-20)

All in `src/ai/ProviderHeuristicRequired.{h,cpp}`.

* **Registry** (`RequiredHeuristicRegistry`), one entry per decision: id, the cards.json parameter that carries it, the
  decision in words, what the ENGINE runs without a policy (`fallback`), and the hook(s) to implement.
* **Capability query** (`DecisionProvider::ImplementsRequiredHeuristic`), false in the root / generic provider.
* **Engine-site query** (`RequiredHeuristicPolicyLive(prov, h)`): the provider implements `h` AND the policy is not
  lifted for measurement (`MTG_UNPRUNED`, or `MTG_UNPRUNE=heuristicrequired`). An engine site reads it to choose
  between the deck's policy and the searched default.
* **RED FLAG** (`WarnRequiredHeuristics`), never a refusal. One machine-readable stderr line
  `[provider-required] RED-FLAG <job> provider=<P>: <card> [<param>], ... -- no deck policy, the engine default runs`
  plus the full message: per `--batch` job (BatchRunner job parse), once per process from `GoldFishRunner::SetupGame`
  (single game, analyzer, mulligan generation, viewer) and from `--scenario`.
* **Tools that make it visibly red:** `scripts/verify_deck.py` gate `provider_policy` (BLOCKING FAIL, one finding
  `provider_policy:<card>` per card -- signing one off is the USER's call); `scripts/analyze_deck.py` prints a
  `PROVIDER POLICY RED FLAG` banner and records `provider_policy` in its report; `scripts/provider_audit.py` marks
  the deck's row `<-- RED FLAG` and `--check` FAILS on it.
* **Measurement levers:** `MTG_REQHEUR_OLD_DEFAULT=1` (per-job `flags`) runs the pre-2026-11-20 engine default for a
  deck with no policy (Contender: the 3-cap). `MTG_UNPRUNE=heuristicrequired` lifts every deck's policy to the
  searched default where the engine site reads `RequiredHeuristicPolicyLive` (today: the ETB dig). Sea's own
  `MTG_SEA_GORGER_ALL=0` / `MTG_UNPRUNE=seagorger` levers are unchanged.
* **Human play** is unaffected: every registered decision's human chooser offers the full list.
* **Tests:** `test/unit/test_provider_heuristic_required.cpp`.

## The registered decisions (2026-11-20)

| Decision (id) | Card(s) carrying the param | Engine default with NO policy | Decks / policy |
|---|---|---|---|
| ExileLibraryLands | Trench Gorger | the engine's Gorger menu (every count; 3+ kept lands = one round-robin prefix per count, cap 62 -- the full keep-subset product is not built) | Sea Creatures: policy (USER rule) |
| EtbDigPick | Acclaimed Contender, Staunch Crewmate | **SEARCHED: the ETB-dig axis over EVERY legal match** (was: the 3 front matches in library order). Beyond the horizon the playout still takes the first match in library order | Pirates: policy (name fold). **Knights: RED FLAG** (draft `MTG_KN_ETBDIG_RANK`, below) |
| LifegainCountersSpend | Nykthos Paragon | **NOT YET SEARCHED** -- the old greedy (spend every use on the first gain). A searched axis is a new plan pin in both worlds; owed (see "Owed") | **Selesnya Lifegain: RED FLAG** |
| GenesisWavePut | Genesis Wave | **NOT YET SEARCHED** -- the disclosed rule (every legal permanent except a creature whose mandatory ETB destroys one of ours). Full menu = 2^L subsets of the L legal reveals; owed | **Landfall, Selesnya Lifegain: RED FLAG** |
| TutorHandPut | Ranger of Eos, Squadron Hawk | library order, truncated (no searched axis; owed, unexercised) | Melira Pod, Martyr Lifegain: policy |
| TutorToBattlefieldPut | Dragonstorm | TutorCandidates' library-order enumeration (no searched axis; owed, unexercised) | Dragonstorm: policy. Breaching Dragonstorm holds no such card (USER: "they don't play the same cards") |
| HedronMillTarget | Hedron Crab | no periodic target policy (period 0) | Mill: policy (period 4) |
| RevivePick | Reveillark | the deck-agnostic ranking, printed MV desc (no searched axis; owed, unexercised) | Melira Pod: policy |

"Owed, unexercised": every deck holding the card has a policy, so the engine default never plays; a new deck holding
one would carry the RED FLAG, and its searched default must be built before its policy is measured against it.

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
