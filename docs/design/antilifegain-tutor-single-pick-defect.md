# Anti-Lifegain: the tutor target is a one-option pick inside the search window

Status (2026-10-10): **MOVED INTO THE PROVIDER; the prune's proof is owed.** USER: *"Let's delete the
part that is useless. However, we should have a narrowing tutor for anti-lifegain."* So the narrowing
stays -- it is the deck's tutor policy -- and the two things that made it a defect are fixed:

* **Where it lives.** The body is now `AntiLifegainProvider::TutorCandidates`
  (src/ai/DecisionProviders.cpp); the free `TutorCandidates` in src/core/SpellEffects.h is deleted
  (its only caller was this provider). The dead `tutor_heuristic: "enabler_then_wincon"` card param
  (parsed, never read by any code in the repo's history) is deleted from cards.json, CardParams, the
  loader and audit_viewer_decisions.py, and both tutors' bracket notes now say what each deck does.
* **The shuffle-order fallback.** With no enabler and no payoff in the zone the function took the first
  legal card in library order; it now returns every legal target (the generic list). Unreachable on the
  shipped list (its only tutorable cards are Tainted Remedy and Aria of Flame), so the deck's play is
  unchanged.
* **Not tested -> tested.** test/unit/test_antilife_tutor.cpp (A1-A7) pins every branch.
* **The proof** (the USER's rule: a one-option provider prune is fine once tested; a counterexample means
  fix the ranking): `MTG_ANTILIFE_TUTOR_BRANCH` (heurarm `ANTILIFE_TUTOR_BRANCH`, DEFAULT OFF) returns the
  enabler AND the payoff, ranked, so the tutor axis (width 2) scores both. A paired pooled A/B of the
  narrowing against that arm on Anti-Lifegain is owed; every game the narrowing loses gets root-caused.
  Delete the lever once it settles.

The original write-up (2026-10-05, found during the Bruna onboarding) follows.

## What it is

`AntiLifegainProvider::TutorCandidates` (src/ai/DecisionProviders.cpp) returns
`::TutorCandidates(s, controller, pp)` in autonomous play -- the FREE function in
`src/core/SpellEffects.h` (`inline std::vector<std::string> TutorCandidates(...)`, the
"enabler_then_wincon" heuristic). That function returns **ONE candidate in two of its three
branches**:

* `ready_enabler` (a lifegain->loss enabler is active or affordable from hand): the first wincon
  (`verse_damage`), else the first enabler (`lifegain_to_loss`), else `any_name`;
* no enabler available: the first enabler, else the first wincon, else `any_name`;
* only the "unaffordable enabler in hand AND both an enabler and a wincon in the library" branch
  returns two names, which the search then branches over.

`any_name` is the **first matching card in LIBRARY order**, i.e. SHUFFLE order -- arbitrary, not a
ranking. The Plan::tutor_choice axis therefore has width 1 for Idyllic Tutor / Enlightened Tutor
in most Anti-Lifegain states: the search never scores the other target.

## Why it is a defect under the current rules

The user's hard rule (2026-09-30, "no greedy in the search window"): heuristics may narrow the
search ONLY from the provider, and a one-option narrowing is acceptable only if it lives in the
provider **and is sufficiently tested**. This one fails both conditions:

1. **Where it lives.** The ranking body is in an ENGINE file (`SpellEffects.h`'s free
   `TutorCandidates`), not the provider -- the provider merely forwards to it. Its only caller is
   `AntiLifegainProvider::TutorCandidates` (verified by grep), so it is provider logic in the wrong
   file.
2. **Not tested.** No unit test pins the enabler-vs-wincon pick against the branched alternative
   (no `test/unit` case references Idyllic / Enlightened Tutor's target choice).
3. **The `any_name` fallback is a shuffle-order pick.** In the shipped Anti-Lifegain list the only
   tutorable cards are Tainted Remedy (enabler) and Aria of Flame (wincon), so `any_name` is
   unreachable *in that list* -- but the function is written for any deck that routes here, and it
   is exactly how the Bruna list would have played (Bruna's Open the Armory / Glittering Wish set
   the `anti` routing signature; BrunaProvider now routes above it).

`TutorSearchWidth() == 2` on the provider is exhaustive for that list's pool, but width is
irrelevant when the candidate LIST has one entry.

## Proposed fix (next session)

* Move the enabler/wincon ranking into `AntiLifegainProvider` (out of SpellEffects.h).
* Return BOTH legal names (enabler and wincon) in every branch, ranked, and let the existing
  tutor_choice axis branch (width 2 is already the provider's TutorSearchWidth); OR keep the
  one-option prune but prove it with the control-arm method the Bruna ledger uses (branched arm
  vs pruned arm on the same seeds, every committed divergence root-caused).
* Delete the `any_name` shuffle-order fallback (return the full legal list instead).
* Expect Anti-Lifegain (and the unprofiled Mill / Unpredictable Cyclone lists that also route to
  AntiLifegainProvider) to move; per-game verdicts at the next suite run.
