# Wish restrictions are not applied by the EDF finisher-wish sideboard scans (follow-up)

Status: **deferred follow-up** (recorded 2026-10-05 during the Bruna onboarding; inert today).

Glittering Wish added `wish_requires_multicolored` (Bruna). Like `wish_requires_name` (Legion
Angel) it is a conjunct inside `TutorNumericFilterOk`, so it reaches every TUTOR enumeration site
(the free `TutorCandidates`, `GenericProvider::GenericTutorList`, the cleanup-reach walk, the human
tutor chooser) plus a resolution guard in `PerformTutor`.

It does NOT reach the EldraziDisplacerFlicker finisher-wish scans, which iterate
`Player::sideboard` directly to ask "can a wish in hand fetch a drain / exile sink?" -- e.g.
`src/core/SpellEffects.h` around lines 18561, 18680, 19212, 20496, 22248, 22272 and
`src/ai/DecisionProviders.cpp` around lines 18102 and 18245 (line numbers at commit time; search for
`ap.sideboard`). Those scans apply none of the wish's filters (type, colour, name, multicolour).

**Why it is inert today:** the only deck running those scans (EldraziDisplacerFlicker) runs Living
Wish, whose `tutor_types` happen to admit every sink in its sideboard; Bruna's sideboard contains no
drain / exile sink, so the scans return nothing for it.

**Fix when a restricted wish meets a sink deck:** add one shared predicate,
`WishCanFetch(const CardParams& wish, const CardDefinition* card)` = type filter + `tutor_color` +
`TutorNumericFilterOk`, and route every direct sideboard scan through it. Expect EDF to be
byte-identical (its wish admits every card the scans look for); verify with its smoke cells.
