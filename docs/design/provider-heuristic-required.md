# "A provider heuristic is EXPECTED" -- searched default + a RED FLAG

Status: BUILT 2026-11-18 as a setup-time THROW (branch `sea-sin-gorger`, Trench Gorger only). REWORKED 2026-11-20
(branch `provreg-1120`) per the USER ruling below: no throw; the engine default is the searched menu; a deck with no
policy gets a RED FLAG. Eight decisions registered. 2026-11-20 late: Knights' Contender policy `MTG_KN_ETBDIG_RANK`
is default ON (the parent's default; the USER may overrule). The Paragon / Genesis Wave searched defaults are OWED
(WIP on `backup/2026-11-20/provreg`; see "Owed").

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
| EtbDigPick | Acclaimed Contender, Staunch Crewmate | **SEARCHED: the ETB-dig axis over EVERY legal match** (was: the 3 front matches in library order). Beyond the horizon the playout still takes the first match in library order | Pirates: policy (name fold). Knights: policy `MTG_KN_ETBDIG_RANK` (default ON 2026-11-20; `=0` = searched default + RED FLAG) |
| LifegainCountersSpend | Nykthos Paragon | **NOT YET SEARCHED** -- the old greedy (spend every use on the first gain). A searched axis is a new plan pin in both worlds; owed (see "Owed") | **Selesnya Lifegain: RED FLAG** |
| GenesisWavePut | Genesis Wave | **NOT YET SEARCHED** -- the disclosed rule (every legal permanent except a creature whose mandatory ETB destroys one of ours). Full menu = 2^L subsets of the L legal reveals; owed | **Landfall, Selesnya Lifegain: RED FLAG** |
| TutorHandPut | Ranger of Eos, Squadron Hawk | library order, truncated (no searched axis; owed, unexercised) | Melira Pod, Martyr Lifegain: policy |
| TutorToBattlefieldPut | Dragonstorm | TutorCandidates' library-order enumeration (no searched axis; owed, unexercised) | Dragonstorm: policy. Breaching Dragonstorm holds no such card (USER: "they don't play the same cards") |
| HedronMillTarget | Hedron Crab | no periodic target policy (period 0) | Mill: policy (period 4) |
| RevivePick | Reveillark | the deck-agnostic ranking, printed MV desc (no searched axis; owed, unexercised) | Melira Pod: policy |

"Owed, unexercised": every deck holding the card has a policy, so the engine default never plays; a new deck holding
one would carry the RED FLAG, and its searched default must be built before its policy is measured against it.

## Measurements (2026-11-20)

**Knights, Acclaimed Contender's dig.** ONE pooled `mtg --batch`, 18,000 games, 134 s wall; held-out seeds
61001..64000, paired (`game_index 0`), d5 b20, std + 2HG; arms `old` (`MTG_REQHEUR_OLD_DEFAULT=1`, the 3-cap = the
pre-2026-11-20 play), `searched` (every legal match), `policy` (`MTG_KN_ETBDIG_RANK=1`).

| fmt | arm | wins | avg | delta vs searched | t | faster / slower | units/g | units x |
|---|---|---|---|---|---|---|---|---|
| std | old | 2999 | 4.3357 | +0.0013 | 2.00 | 0 / 4 | 1062 | 0.986 |
| std | policy | 2999 | 4.3343 | +0.0000 | 0.00 | 0 / 0 | 1060 | 0.984 |
| std | searched | 2999 | 4.3343 | -- | -- | -- | 1077 | 1.000 |
| 2hg | old | 2997 | 5.0000 | +0.0010 | 1.73 | 0 / 3 | 3774 | 0.968 |
| 2hg | policy | 2997 | 4.9990 | +0.0000 | 0.00 | 0 / 0 | 3827 | 0.982 |
| 2hg | searched | 2997 | 4.9990 | -- | -- | -- | 3899 | 1.000 |

* The policy plays IDENTICALLY to the searched default on all 6000 paired games, at ~1.6% (std) / ~1.8% (2HG) fewer
  units; so it is ON by default. Versus the OLD 3-cap (today's play before this change) both are 7 games faster in
  6000 and 0 slower -- so Knights' play does move, slightly, in the right direction.
* Census (`MTG_ETBDIG_CENSUS`): the dig fires in ~30% of games; the search deviates from look-order index 0 in 51 of
  901 digs; no win turn moves between searched and policy. Library-order picking does NOT explain Contender's poor
  screens: the card itself is weak at a T4-T5 kill.
* Files (worktree `provreg`): `logs/provreg/kn_main_results.txt`, `kn_main_slower.txt`, `kn_main_manifest.json`,
  `ab.py`, `census.py`.

## Owed

1. **Nykthos Paragon (LifegainCountersSpend) and Genesis Wave (GenesisWavePut) searched defaults.** Built as WIP on
   `backup/2026-11-20/provreg` (commit `96b47813e`, branch `provreg-1120`), NOT landed:
   * Paragon pin `Plan::paragon_spend_pins` (one spend count per eligible main-phase gain event; past the list = 0),
     Wave pin `Plan::wave_skip_choice` (skip mask over the first Wave's legal reveals, per-name subsets), consumed in
     `ResolveLifegainCountersOnceBudget` / `PerformGenesisWave`, installed in both ApplyPlanDirect and the executor,
     fanned out in `AppendSubdecisionAxes` off one probe apply per base plan.
   * Draft deck policies (default OFF): `MTG_SL_PARAGON_POLICY` (Selesnya: the old greedy as a one-option restriction)
     and `MTG_WAVE_RULE_POLICY` (Selesnya + Landfall: the disclosed rule). Rule: default ON only if no worse on the
     A/B AND cheaper; otherwise the searched default stands and the deck keeps its RED FLAG.
   * Unit file `test/unit/test_searched_paragon_wave.cpp`. Its two executor-lockstep failures were the TEST's no-op
     stack resolver (`TakeTurn`'s third argument resolves the stack; a `[](GameState&){}` leaves every cast spell on
     it), not the pins -- fixed on the backup branch, where the full unit suite passes (3781/3781).
   * The pooled A/B is READY, not run: manifest `build/scratch/provreg/pw_main/manifest.json` (copy
     `logs/provreg/pw_main_manifest.json` in the `provreg` worktree), runner `build/scratch/provreg/run_pw_main.sh`,
     13 jobs x 3000 games (Selesnya std + 2HG: old / searched / polpara / polwave / policy; Landfall std: old / searched
     / policy), seeds 61001.., d5 b20. Analyse with `python3 logs/provreg/ab.py analyze build/scratch/provreg/pw_main
     searched`.
   * Known limits of the axes: main-phase applies only (combat gains keep the greedy), E <= 12 events, the event
     count comes from the base plan's probe, the Wave mask covers only the first Wave and the first 31 reveals, the
     axes do not cross other axes or ride continuations.
   * Until it lands, Selesnya Lifegain (Paragon, Wave) and Landfall (Wave) carry the RED FLAG and play the OLD engine
     default (the registry fallback text says NOT YET SEARCHED).
2. Searched axes for TutorHandPut, TutorToBattlefieldPut and RevivePick ("owed, unexercised": every deck holding
   the card has a policy today).

## Open USER questions

* ~~Knights: `MTG_KN_ETBDIG_RANK` default ON, overrule?~~ KEPT ON. USER 2026-11-21: "That is potentially a reasonable
  rule. Even better might be to project damage it could deal based on what you have in hand, but your idea gets pretty
  close." **Owed (idea, not started):** rank the dig's matches by the damage each projects to deal this turn and next,
  given the hand and the board (the lord bonus is today's proxy for it); measure against the current rank.
* ~~Should the Contender re-open stay closed?~~ CLOSED. USER 2026-11-21: "We'll close it. You are correct that it is
  slow compared to some other cards. It's just that this bad rule didn't help matters at all."
* Selesnya / Landfall draft policies: pending the pw_main A/B.
