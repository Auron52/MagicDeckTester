# Merfolk Turns analysis checkpoint

## 2026-11-14 nested Prophets champion correction (finding 1)

The independent static review at `01ca7fc04` found that championing a Prophets
fired its leaves trigger before the incoming Prophets recorded the exile link.
In the concrete chain B champions C, A champions B, then D champions A, A's
leaves trigger returns B. If returned B champions D, D's leaves trigger must
find A already exiled under D and return it. The old synchronous order returned
B before recording A, so D found nothing and A remained stranded in exile.

`ResolveMerfolkChampion` now removes and links the chosen victim before firing
that victim's leaves trigger. The shared core path is used by normal execution,
rollout application, and replay. The autonomous champion fan now reserves a pin
for each linked Prophets on the battlefield or linked in exile in addition to
explicit cast/Vial/Seahunter entries. This exposes returned B's own victim and
decline choices, plus the next returned Prophets choice, to plan search. A
scripted plan that exhausts its pins declines further champion triggers;
human prompts still override the scripted choice at each resolution.

The concrete replay found a second ordering defect in the linked-return helper:
while A's old leaves trigger was returning B, A could reenter and champion E;
the still-running helper then returned E from exile too, despite E belonging to
the new A instance. The helper now snapshots its linked return set before any
return ETBs run, so an old leaves trigger cannot consume a new link.

Validation: `MTG_BUILD_JOBS=6 ./build.sh` passed (six jobs kept the build below
30 GiB used memory). `build/Release/mtg-test --test-case='*Prophets*'` passed:
8 cases, 100 assertions. The focused cases check the live state during B's
returned champion prompt, all three linked exile/return transitions, B's
alternative victim and decline, the autonomous second and third pin choices,
and replay of the selected Vial plan. No full analyzer, regression, or play
server was run. This worktree covers finding 1 only; the separate Mihail
payment-axis finding is owned by another worktree.

The next review found an unlinked-return edge: with A on the battlefield and
two new Prophets D and E, D can champion A, E can champion D, and A returns
with a third champion choice. The linked-only fan missed that third pin. Plans
with multiple explicit Prophets entries now reserve a pin for each existing
Prophets, including unlinked ones. A focused two-Vial case checks that pin.
This synthetic case also exposed an unpinned speculative first-victim loop:
the same physical Prophets returned repeatedly until the stack overflowed
(`g_prophets_champion_cursor` reached 2270 in GDB). The unpinned default now
declines when a source reenters its own unresolved champion chain; explicit
searched pins and human choices retain their own decisions. The combined
Release build passed. The two-entry test passed (6 assertions); the focused
City, Seahunter, Mihail and Prophets set passed 22 cases/384 assertions. The
combined d5 seed 3000011/game 10 passed under the 18 GiB process RSS cap.

Started 2026-11-14. Source deck: `decks/Merfolk Turns/Merfolk Turns.cod`
(copied unchanged from the user's untracked file in the primary checkout).

## Coverage

Initial coverage reported 11 missing cards. After the Deeproot Pilgrimage
checkpoint, `scripts/analyze_deck.py --coverage-only` reports 10 missing:
Emperor Mihail II, Stonybrook Banneret, Silvergill
Douser, Wanderwine Prophets, Svyelun of Sea and Sky, Merfolk Sovereign,
Rishadan Dockhand, Rogue's Passage, Seahunter, and Tidal Warrior.
Three Tree City is present but its chosen subtype and mana color are hardcoded
to Goblin and red; this is incorrect for Merfolk Turns and is a required gap.
All twelve Oracle entries were fetched from the live Scryfall API on 2026-11-14.

| Card | Critical rule and viewer choice |
|---|---|
| Deeproot Pilgrimage | One token per event in which one or more nontoken Merfolk become tapped; attacks, activated abilities, and mana taps all matter. Token is blue 1/1 Merfolk with hexproof. |
| Emperor Mihail II | Controller may privately look at the library top any time and cast Merfolk spells from it under normal timing and mana rules; each Merfolk cast offers optional `{1}` for a token. |
| Stonybrook Banneret | Reduces generic cost of Merfolk **or** Wizard spells once per Banneret; islandwalk. |
| Silvergill Douser | `{T}` targets a creature for `-X/-0` (Merfolk and/or Faeries controlled). Even when power reduction is inert against the passive opponent, tapping it feeds Pilgrimage. Human target choice. |
| Wanderwine Prophets | Champion another Merfolk on entry, linked return on leaving; combat damage offers a Merfolk sacrifice for an extra turn. Human champion and sacrifice choices. |
| Svyelun of Sea and Sky | Attack draw, conditional indestructible with two other Merfolk, ward `{1}` to other Merfolk. |
| Merfolk Sovereign | Other controlled Merfolk +1/+1; `{T}` targets a Merfolk for unblockable. Tapping feeds Pilgrimage; human target choice. |
| Rishadan Dockhand | Islandwalk; `{1},{T}` taps any target land. Tapping feeds Pilgrimage; human target choice. |
| Rogue's Passage | `{T}: {C}`; `{4},{T}` targets a creature for unblockable. Human target choice. |
| Seahunter | `{3},{T}` searches for a **Merfolk permanent** and puts it onto battlefield, then shuffles. Human library choice. |
| Tidal Warrior | `{T}` turns target land into an Island until end of turn. Can fix own colored mana; human land choice. |
| Three Tree City | Choose creature type as it enters; `{2},{T}` chooses one color and adds mana equal to own creatures of that type. The current hardcoded Goblin/red text is false for this deck. Human type and color choices. |

## Current stage

Stage 4/5: all 17 deck cards have complete coverage and the viewer decision
emitters are wired. The final-source 1,000-game scorer completed on the C2
mana integration (`3564a2fbd`) plus the Codex-play corrections. Its baseline
profile contains 16 card scores, a static one-to-five-land keep window, and no
hand-score gate. The cost-aggregate diagnostic, leaf tie-break check, viewer
audit, and pooled Hinata cost gate are running. The provisional three-tier
suite rows remain unaccepted pending those results and verification. No value
leaf or exhaustive mulligan table is part of this analysis.

### Deeproot Pilgrimage checkpoint

Card definition and the blue 1/1 hexproof Merfolk token are modeled. The shared
combat core treats simultaneous attacking Merfolk as one tap event. The unified
payment entry recognizes each committed nontoken Merfolk mana tap after payment
trials finish; the manual tap route recognizes each explicit mana activation.
The shared `ResolveDeeprootPilgrimageTap` helper is available for the remaining
Merfolk `{T}` abilities as they are implemented: call it once after a successful
activation that tapped a nontoken Merfolk, including Douser, Sovereign, Dockhand
and Tidal Warrior. Seahunter is Human, so its tap does not trigger Pilgrimage.
Focused unit checks cover simultaneous attackers, token/vigilance exclusions,
multiple Pilgrimages, committed mana taps and manual tap.
`./build.sh` passed and the focused doctest run passed (3 cases, 36 assertions).
`python3 scripts/audit_card_fields.py` passed all hard card fields. This
checkpoint leaves the four future Merfolk `{T}` activation hooks for their
card implementations; no such activation currently exists in the engine.

### 2026-11-14 integrator checkpoint

The 60-card source list is unchanged. Live Scryfall Oracle data and mana costs
were fetched for all eleven missing cards and Three Tree City. Deeproot
Pilgrimage is the first implemented card; the other ten card definitions and
their mechanics plus Three Tree City remain open. The
parent agent explicitly owns full regression, GT regeneration and the play
server, so those are excluded from this worktree's verification.

## Verification owed

After coverage becomes clean: full build/unit tests, analyzer baseline profile,
provider routing audit, cost gate, and Stage 5 correctness/viewer checks.
The parent agent owns smoke/full regression, overnight tier rows, GT, and the
Codex-play sweep per the explicit worktree assignment.

## Remaining implementation map

All items below are open. Integrate one card at a time, then repeat build,
coverage, cost/field audit, focused rules tests, and viewer decision checks.
Do not generate a profile while coverage is incomplete.

1. **Wanderwine Prophets:** Add another-Merfolk champion choice on entry,
   record the exact exiled object on the Prophets permanent, and return it
   when Prophets leaves. On combat damage to a player, offer a choice of any
   controlled Merfolk to sacrifice (including Prophets); queue an extra turn
   only after a successful sacrifice. The shared enter/leave hooks are in
   `SpellEffects.h`; combat damage is in `Combat.cpp`; existing extra-turn
   queue state is in `GameState.h`. Surface both champion and sacrifice
   choices in human play and test Vial/Seahunter puts, Prophets sacrificing
   itself, and multiple extra turns.
2. **Emperor Mihail II:** Let only its controller inspect the library top
   privately and cast a top-card Merfolk spell with normal cost and timing.
   Extend action enumeration, apply, and human plan labels for that cast
   source. Offer the optional `{1}` token trigger on *casts* from any zone,
   before the spell resolves; battlefield puts must not trigger it. Test
   top changes, costs, the optional payment, and no token on Vial/Seahunter.
3. **Seahunter:** Implement `{3},{T}` as an activated search for any Merfolk
   **permanent** card to battlefield, then shuffle. Reuse the library tutor
   chooser for the human pick, including legal fail-to-find, and route the
   entrant through the universal ETB cascade. Seahunter is Human; its own
   tap must not make a Pilgrimage token.
4. **Stonybrook Banneret:** Extend subtype cost reduction to the OR of
   Merfolk and Wizard, counting each Banneret once for a Merfolk Wizard spell.
   Keep the exact same reduction in `ManaPayment.cpp` and all same-turn
   pricing/bounds in `TurnSolver.cpp`. Islandwalk is passive-opponent inert.
5. **Silvergill Douser / Merfolk Sovereign:** Add searched `{T}` targeted
   activations modeled after `PermAbilityMode::TapTargetPump` and its target
   pins. Douser's `-X/-0` uses the live union count of own Merfolk/Faeries
   at resolution; Sovereign targets a Merfolk creature for unblockable and
   also grants +1/+1 to other own Merfolk. Human play must offer every legal
   target. Call `ResolveDeeprootPilgrimageTap` after a legal source tap,
   before the ability resolves (its new token can raise Douser's X).
6. **Rishadan Dockhand / Rogue's Passage / Tidal Warrior:** Add searched
   targeted activation modes and human target variants. Dockhand can tap
   any land for `{1},{T}`; Passage makes any target creature unblockable for
   `{4},{T}` and retains its `{C}` mana mode. Tidal Warrior changes any land
   to an Island until end of turn, including its intrinsic mana ability;
   extend `LandTypeChangedToIsland` and clear the temporary effect in both
   cleanup paths. Dockhand and Warrior source taps fire Pilgrimage.
7. **Svyelun of Sea and Sky:** `attack_draw_cards=1` already enables second
   main in `GoldFishRunner`. Add live indestructible while two *other*
   Merfolk are controlled; ward `{1}` to other controlled Merfolk is relevant
   only when opponent targeting exists, and must be explicitly represented
   or disclosed for the passive-opponent model.
8. **Three Tree City:** Replace hardcoded Goblin/red. Its as-enters chosen
   type must be stored per permanent (`chosen_subtype_id`); every scaled mana
   count in `SpellEffects.h/.cpp` and `ManaPayment.cpp` must use that type
   and include only controlled matching creatures. The fed mana color is
   already a WUBRG human pre-tap choice. Add a human as-enters type choice;
   `audit_viewer_decisions.py` currently calls `chooses_creature_type` inert.

The remaining human choices should be audited against `tools/play/DECISIONS.md`
and `scripts/audit_viewer_decisions.py`; plan variants must retain all legal
targets in human play. The source deck file and this ledger are committed on
the implementation branch. No parent-owned regression, GT, or play-server
work has been run here.

### Check results at handoff

The final `./build.sh` passed after the Pilgrimage token-creation safety edit.
The focused Pilgrimage doctest passed 3 cases / 36 assertions. A broader unit
run reached 302 passing cases / 67,959 passing assertions before it was
intentionally stopped; it is **not** a full-suite pass. The targeted
`audit_card_costs.py` comparison passed Deeproot Pilgrimage (1/1 compared);
the full repository-wide cost audit was stopped, so it is not a pass.
`audit_card_fields.py` passed its hard fields. Coverage exits 1 with the ten
missing cards listed above; it currently labels Three Tree City "full"
despite the known hardcoded type/color gap, which must be repaired. The
reference-safety check passed. Working tree was clean at handoff.

### 2026-11-14 source batch in progress (Codex continuation)

Live Oracle text/costs were refetched for all ten remaining cards and Three Tree City.
Source changes are in progress; this section is an implementation ledger, not a
claim that coverage is complete. The one full `./build.sh` belongs after the batch.

- Added definitions for Stonybrook Banneret, Silvergill Douser, Svyelun of Sea and
  Sky, Merfolk Sovereign, Rishadan Dockhand, Rogue's Passage and Tidal Warrior.
- Extended the searched tap-target activation for Douser's live Merfolk/Faerie
  count, Sovereign/Passage unblockable targets, Dockhand's land tap, and Tidal
  Warrior's land-to-Island effect. Nontoken Merfolk activations call the existing
  Deeproot Pilgrimage tap-event helper. Temporary Island conversion uses a
  self-linked land marker cleared in both cleanup worlds.
- Stonybrook Banneret has an OR subtype reducer (Merfolk or Wizard, once for a
  spell with both); the same-turn reducer estimator is being aligned.
- Three Tree City now stores an ETB creature-type choice and the mana counter
  reads that subtype rather than every creature. Its Oracle text no longer says
  red-only. The human ETB type choice still needs viewer wiring.
- Svyelun's attack draw is wired by the existing shared `attack_draw_cards` path.
  Its conditional indestructible and ward clauses still need a scoped
  rules/engine audit before this card is considered finished.

Remaining implementation tasks before coverage/audit/build: Emperor Mihail II
(private top look, library-top Merfolk casts in both worlds, optional {1} paid
trigger/token and viewer choice); Wanderwine Prophets (champion choice, linked
return, combat-damage sacrifice and extra turn, viewer choices); Seahunter
({3},{T} Merfolk **permanent** library search, shuffle, searched/human target);
Three Tree City human ETB type choice and source-specific handling of multiple
copies with different choices; review every new activated ability's target
legality, liveness, mana payment, keys and human plan labels. Then complete the
Banneret same-turn line validator, apply `audit_card_fields.py --update`, run
`audit_viewer_decisions.py`, establish a dedicated Merfolk Turns provider and
run `provider_audit.py --check`; finally run one full `./build.sh`, fix failures,
run focused tests and coverage to zero. No full regression, GT, push or play
server in this worktree.

### Source checkpoint a2312bf31 and follow-on Seahunter work

`a2312bf31` committed the first source batch and ledger, with no build (per the
requested single-build-after-batch rule). The current uncommitted follow-on
adds Seahunter's definition and a `TapTutorPut` activation: one plan variant per
Merfolk permanent card in the library, {3}+{T} payment in both apply worlds,
a pinned library pick, shared put/ETB cascade, and the ordinary deterministic
post-search shuffle. The human plan menu gets the target name and the legal
fail-to-find option. A dedicated `MerfolkTurnsProvider` now routes from three
independent deck-specific params and chooses Merfolk for Three Tree City's
ordinary ETB; it still needs the mandatory `provider_audit.py --check` after
building. The viewer static audit currently fails only on the unclassified
`choose_type_on_enter` param, because the real ETB choice needs a new human
chooser. The audit's Oracle advisory still mentions Douser, Rogue's Passage and
Seahunter although their target/pick rides `main_phase` plan variants; the
manifest/cross-check mapping needs review after the actual protocol sweep.

**Exact remaining tasks for the next continuation if context expires:**
Prophets source checkpoint after `20d6bb88f` now has card data/params and a
shared champion exile/owner-aware return. The next checkpoint adds searched
champion and combat-sacrifice pins, extra turns on each combat-damage event,
viewer `champion`/`sacrifice` decisions, state hash/reset/replay wiring, and
focused unit cases. These changes are **not built or run**. The parent must
review the projected search fan's size, compile/static assertion for `Plan`
size, run the focused tests, and fix any failure in the one requested build.
The full deck task remains incomplete until all checks below pass.
1. Implement Emperor Mihail II from live Scryfall Oracle ({1}{U}{U}; private top
   look, legal Merfolk spell cast from library top, per-copy optional {1} on
   Merfolk cast, blue 1/1 Merfolk token). Add a true library-top cast source to
   enumerator, rollout, executor, replay and viewer; do not fake a hand card.
   Search must branch on paying each Mihail trigger; human gets an option per
   trigger. The top card is visible only to its controller, not public reveal.
2. Implement Wanderwine Prophets from live Oracle ({4}{U}{U}; champion another
   Merfolk on ETB, sacrifice if none; exiled card returns when this leaves;
   combat damage to a player may sacrifice a Merfolk for an extra turn).
   Champion must accept tokens (which cease to exist on exile), preserve
   ownership on return, fire return ETBs, and offer every legal champion and
   sacrifice choice to the human. Search must branch on both choices.
3. Finish Three Tree City: human ETB creature-type chooser, and ensure each
   copy's chosen type is used at every scaled-mana path (some bounds still call
   `ScaledManaCreatureCount(state)` without the source). Its chosen color is
   already in the manual mana pre-tap menu; audit both payers and source keys.
4. Review/fix the follow-on Seahunter code for payment, liveness, the target
   plan subchoice, fail-to-find, shuffling and full permanent type filter; add
   focused tests. Review Svyelun's conditional indestructible and ward; the
   opponent is passive but any own destroy interaction must be ruled out.
5. Complete viewer manifest and registry classifications, especially the new
   type chooser; run actual `--claude-play` protocol checks for each target.
6. Fetch/update Scryfall snapshot (`audit_card_fields.py --update`), coverage
   zero, `provider_audit.py --check`, then ONE `./build.sh` after all source
   changes, fix compile/test failures, run focused unit tests and the viewer
   decision audit. Parent owns regression, GT, push, play server and play sweep.

### 2026-11-14 Mihail source checkpoint (unbuilt)

Emperor Mihail II now has a card definition and parameters for private
library-top permission and optional {1} cast-trigger tokens. The solver emits a
distinct library-top Merfolk cast action, verifies the same physical top card
when paying, and removes it from the library in the rollout and executor.
Replay carries `cast_from_top`; the viewer publishes the top privately to its
controller, queues it as `casttop=`, and checks that source separately from a
same-named hand copy. Each Mihail trigger reads an ordinal bit in the searched
pay mask or asks the human through the dig chooser, then pays {1} before making
a blue 1/1 Merfolk. State keys and turn resets carry the mask and ordinal.
The viewer decision manifest maps the new choice and source.

The focused viewer line builder check passed: 4,713 main-phase lines
reconstructed, 0 failures, including the new top Merfolk source test. New
`test_merfolk_turns.cpp` cases check private-top access and independent paid
triggers; they are **UNBUILT/UNRUN**. The full source change is still
**UNBUILT** under the user's one-build constraint. The paid-mask axis currently
declines to fan plans with 20 or more trigger occurrences; remove that bound
or establish a sound bounded alternative before calling the source task
complete. Confirm top-cast eligibility for alternate Merfolk spell costs,
run the focused compiled tests, card cost/field audits, coverage, viewer
decision audit and provider audit after the single build. Prophets, City type
chooser, and the remaining Seahunter/Svyelun reviews remain open.

### 2026-11-14 continuation checkpoint after b882f8340

Source edits in this checkpoint: `ScaledManaNetYield` now accepts its specific
Three Tree City permanent, and the actual mana projection and same-turn colour
bound pass it. Every direct `ScaledManaCreatureCount` payment/bound call now has
the source pointer. `GenericProvider::ManaSourceRank` still calls the source-free
net helper because its interface receives only a card definition; that call is
an ordering heuristic, not a mana amount or payment, and can read the first City
if two copies with different types are put on a synthetic board. Review it with
the type chooser. Seahunter's action enumeration now includes the legal
fail-to-find target in searched play as well as human play, and labels that
branch as a search and shuffle. Its liveness check validates a pinned real
target before cost payment; the shared tutor resolver filters Merfolk plus all
six permanent types and shuffles on a pinned decline. Focused tests were added
in `test/unit/test_merfolk_turns.cpp` for separate City counts, the permanent
filter, a pinned Seahunter put, and fail-to-find. They are **UNBUILT/UNRUN**;
the single `./build.sh` is still owed after all source work.

Live Scryfall was fetched again for Mihail, Prophets, Seahunter, Svyelun and
Three Tree City. Mihail's cost is `{1}{U}{U}`; Prophets' is `{4}{U}{U}`.
No definitions exist yet for Mihail or Prophets. Coverage cannot be zero until
they and their rules paths are implemented. The static viewer audit was run
and fails on unclassified `choose_type_on_enter`; its Douser/Passage/Seahunter
Oracle advisory is for choices represented by `main_phase` plan variants.
No full build, regression, GT, push, play server, or play sweep ran here.

**Precise implementation map for the remaining work:**

1. Mihail: add its card definition and a true top-of-library cast source to
   `Action`/plan enumeration (`TurnSolver.cpp` hand loop near 39319), rollout
   `ApplyPlanDirect` cast near 70280, executor `AIEngine.cpp` cast dispatch near
   5424/5702 and `CastSpellFromHand` near 10118, replay/line grammar and viewer
   (`main.cpp` top context near 1695). Preserve the exact top card number and
   remove it with `Library::DrawTop`; a hand placeholder is invalid. Private
   top knowledge must not set public `library_top_revealed`; `RawAt(0)` is for
   display, `front()` for a real top read. `FireOnCastTriggers` near
   `SpellEffects.h:6320` is shared; add per-copy optional `{1}` paid token
   trigger before resolution, with search pins/fanout (Militia Pride's
   `scripted_militia_pride_pay` + `FireMilitiaPrideTokens` is a precedent),
   human choice per trigger, and no trigger for Vial/Seahunter puts. Top changes
   must reopen a same-turn continuation. Check timing, normal cost, summoning
   sickness, multiple Mihails and top card private visibility.
2. Prophets: add definition; champion at shared enter cascade (all casts,
   Vial and Seahunter), one plan branch for each other controlled Merfolk and
   deliberate decline, including tokens that cease to exist on exile. Store
   exact linked exile object and owner; existing `ReturnOrphanedLinkedExiles`
   assumes opponent ownership, so cannot be reused unchanged. Return via
   `FireLeavesBattlefieldTriggers` near 13443 using the universal put/ETB path;
   death/sacrifice uses `SacrificePermanentAt` near 17171. Add combat-damage
   trigger in shared `src/ai/Combat.cpp` for each damage step, branch over any
   controlled Merfolk sacrifice (including Prophets) or decline, then call
   `GameState::QueueExtraTurn` only after a successful sacrifice. Account for
   double strike and source disappearance. Human gets champion exile and
   combat sacrifice choices. Test no fodder, token exile, return ownership/ETB,
   self-sacrifice and multiple queued extra turns.
3. Three Tree City: wire a real human as-enters creature-type chooser at
   `FireEtbWatchers` near 10649. Follow `g_play_etb_color_chooser` across
   `GameLogger.h`, `main.cpp` protocol writer/choice stream, `tools/play/index.html`
   modal dispatch and `tools/play/DECISIONS.md`. Classify
   `choose_type_on_enter` in `audit_viewer_decisions.py` as a real decision.
   Preserve the autonomous Merfolk provider default. Audit the source-free
   `GenericProvider::ManaSourceRank` call described above, both payers and
   keys; run the new focused count test.
4. Seahunter: run the new focused tests after build; verify actual activation
   payment/liveness, exact target plan subchoice, deterministic shuffle and
   ETB cascade through the protocol. Svyelun: its `attack_draw_cards=1` path
   is implemented. The source 60 has no own destroy or damage effect, and a
   passive opponent never targets, attacks or blocks, so conditional
   indestructible and ward have no reachable interaction in this deck's
   goldfish. Record any provisional inert-clause disclosure for user signoff;
   if wider fidelity is required, add a state-aware indestructible predicate
   across destroy/SBA readers (plain `Permanent::HasKeyword` lacks GameState).
5. Finish viewer registry/manifest and actual protocol checks; update Scryfall
   snapshot and run coverage/cost/field/provider audits; only then run ONE
   `./build.sh`, fix compile errors, and run focused unit tests. The parent
   retains regression, GT, push, play server and the full play sweep.

### Three Tree City viewer source checkpoint in progress

A `choose_creature_type` human as-enters chooser is now wired through the
shared enter cascade, `GameLogger` pause scopes, the Codex-play choice stream,
the viewer's searchable modal, `DECISIONS.md`, and the static audit manifest.
The menu is the complete Scryfall creature-type catalog fetched 2026-11-14
(377 names), interned before the subtype registry freezes. Autonomous play
still takes the Merfolk provider's default. `audit_viewer_decisions.py
"decks/Merfolk Turns/Merfolk Turns.cod" --no-sweep` now passes its static
self-guard with expected `choose_creature_type` and `vial_charge`; its three
Oracle advisories are plan-variant choices. Python syntax check and
`git diff --check` passed. This checkpoint is **UNBUILT and not protocol
verified**. Mihail and Prophets remain unimplemented, so the single full build
is still owed after their source work. Add an actual as-enters prompt test
and verify a selected non-default type changes only that copy's mana yield.

### 2026-11-14 source integration and verification (supersedes earlier checkpoint statuses)

Source checkpoints `84e7ac80b`, `615b975e3`, `7861faf1d`, `20d6bb88f`,
`25b60829e`, and `b01b1ba7b` implement the six items in the exact remaining-task
list above. Mihail has a real top-library cast source and per-copy paid token
trigger; Prophets has linked champion exile/owner-aware return and searched
combat sacrifice/extra turns; Three Tree City has a human type prompt and
copy-specific scaled mana; Seahunter has searched target/fail-to-find variants;
the viewer and Merfolk provider are wired. A normal hand land drop needed an
explicit entry watcher call to show City's type chooser; that is in `b01b1ba7b`.
Svyelun's conditional indestructible and ward are documented as unreachable
in this passive-opponent deck's goldfish; attack draw remains implemented.

The one full `./build.sh` was run after source integration. Its compile errors
were fixed, and an incremental Release target rebuild passed. Focused tests
passed: Three Tree City 2 cases/25 assertions, Seahunter 2/15, Mihail 2/18,
Prophets 4/45. Coverage-only reports zero missing definitions. The static
viewer audit (`--no-sweep`) and `node test/viewer_linebuild_check.js` pass;
the latter reconstructs 4,713 lines with zero failures. A direct batch job
reports `provider=MerfolkTurns`; `provider_audit.py --check` passes but its
global profile list has no Merfolk Turns profile to route, so the direct job
is the deck-specific route check. Live viewer replays reached Mihail `casttop`
and `dig`, Prophets `champion` and `sacrifice`, City's type choice, and the
Seahunter main-phase plan/target list. Parent retains regression, GT, push,
play server and the full Codex-play sweep.

The Scryfall snapshot update fetched the new Merfolk entries but returned
HTTP 429 for 208 unrelated cards. The deck-specific cost audit compares all
13 costed cards and finds no mismatch; the field audit checks all 17 cards
with no hard mismatch. Prophets' `Champion` keyword is allowlisted because
the mechanic is represented by `champion_merfolk`, not an engine keyword bit.
The field audit also reports 17 pre-existing stale allowlist entries for
unrelated decks, which are outside this source assignment.

The seed-1 viewer replay exposed a Seahunter liveness discrepancy: the plan
enumerator checked the canonical card definition, while activation liveness
checked the compact library Card's subtype and silently declined the plan.
The gate now checks the same canonical definition. In the exact turn-11 replay,
choosing Rishadan Dockhand puts card 33 onto the battlefield, taps Seahunter,
and removes its activation from the next menu. A compact-card unit test guards
this case. The incremental Release rebuild passed; final focused tests pass
(Seahunter 3 cases/18 assertions; City/Mihail/Prophets combined 10 cases/175
assertions), coverage remains zero missing, and the viewer line-build check
remains 4,713 lines/zero failures. `check_references_safe.py` scanned 89
checkouts and passed. No full regression, GT, push, play server or full play
sweep ran in this worktree.

### 2026-11-14 Mihail lossless pay-choice implementation

Replaced the signed mask in plans and game state with `MihailPayChoices`, an
ordered dynamic sequence with an explicit unpinned state. Trigger consumption
uses a `size_t` ordinal and reads every position. Plan and state hashes,
dominance, replay comparison, diagnostics, resets, labels, memory accounting,
and executor/rollout installation now use the same sequence. The enumerator
walks binary successors without shifts or an integer mask limit. It no longer
drops `bound >= 20` or plans that carry another decision axis. A source cast
from the graveyard also enters the eligibility check.

The exhaustive fan still materializes all plans, so a synthetic high bound
can be expensive. No provider pruning or performance claim is adopted here;
the parent will measure actual Merfolk deck cost and decide separately.
`./build.sh` passed in Release, followed by a clean incremental `./build.sh`.
The focused `mtg-test --test-case='*Mihail*'` run passed 4 cases and 42
assertions, including high ordinals, ordered choices across copies, key
material distinction, and human prompt independence. GT/regression and deck
cost measurement were intentionally not run in this focused implementation.

### 2026-11-14 first profile and cost diagnosis (in progress)

The default d5 card-score pass was host-OOM-killed after about 140 games with
24 workers. A six-worker retry reached roughly 31 GiB RSS; an eight-worker run
with result-neutral caches capped at 8 GiB hit its 18 GiB RSS watchdog at game
106. The documented `MTG_ANALYZE_DEPTH=3` fallback also hit the same cap at
games 7/10, even on two workers. The cap reports show empty FSL/plan pools:
this is transient search branching, not cache retention. No baseline profile
was produced by these attempts.

Seed 3000011, game index 10 at d3/b20 reproduces the memory spike.
`MTG_AXIS_TRACE` shows 480 plans before the Prophets axes on turn 6; 128
plans cast two Wanderwine Prophets. The previous fan assumes both fresh
Prophets can deal combat damage and multiplies champion choices by two
sacrifice-choice ranges, exceeding 11 GiB RSS before scoring. The 60-card
list has exactly one Lightning Greaves and no other haste grant, so at most
one freshly entered Prophets can deal combat damage in that turn.
`MerfolkTurnsProvider::ProphetsFreshCombatEventCap` encodes that proven
upper bound while the generic provider retains the full bound.

A separate d3 seed 3000008 completed in 325 seconds/763,748 work units.
`MTG_BRANCH_STATS` attributes 6.1 million odometer candidates to Rogue's
Passage activations and 3.3 million final plans to Rishadan Dockhand casts.
Rogue's Passage's unblockable activation has no effect against the passive
opponent, and tapping its land source does not trigger Pilgrimage. The
Merfolk Turns provider therefore removes that dominated activation from
autonomous targets; human play still offers every legal target. The mana
ability remains available. Both performance edits are rebuilding and must
pass the seeded repro, focused tests, cost gate and outcome checks before
being treated as verified.

### 2026-11-14 Prophets choice-expansion memory fix

The first capped seed-10 retry still reached 20.05 GiB RSS. A trace inside
`AppendSubdecisionAxes` then showed the champion fan alone exceeding 5.4
million materialized plans before combat pins started. The previous width
added four hypothetical Merfolk per action, including casts and equips that
create none. Those surplus ranks were clamped to the last legal choice at
resolution, so the fan copied large numbers of equivalent plans before it
could score or deduplicate them.

`MerfolkTurnsProvider::ProphetsMerfolkVictimWidth` now provides a proved upper
bound for this 60-card pool: existing Merfolk, at most one new Merfolk for each
cast/Vial/Seahunter put, one token per Merfolk spell cast for each possible
Emperor Mihail II, and one token per tap event for each possible Deeproot
Pilgrimage. The tap count includes every permanent-ability action plus the
attack declaration; none of this pool's creatures taps for mana. It counts
Mihails in champion exile and Mihails put by Vial or Seahunter, and assumes
every optional Mihail payment succeeds. Death, champion exile and sacrifices
can only reduce the list. Thus every reachable victim rank remains inside the
bound. The generic provider retains the original broad width.

The fresh-Prophets combat cap also checks the physical target of a Greaves
attachment, including a Prophets cast from Mihail's library top. A fresh
Prophets with no Greaves attachment action cannot have haste in this pool;
other decks keep their generic full event bound. The first version of this
check left library-top IDs unknown and still exceeded 18 GiB. Reading the
already-visible top card's physical ID removed that unnecessary uncertainty.

Validation: `MTG_BUILD_JOBS=6 ./build.sh release -- --target mtg mtg-test
mtg-analyze` built all three targets; the focused Prophets/provider tests
passed 6 cases and 57 assertions. The exact seed 3000011/game-index 10 d3/b20
command with `MTG_AXIS_TRACE=1 MTG_RSS_CAP_GB=18` exited 0 in 16.25 s and
reported average 5.0000 turns. Peak process RSS was 11,566,316 KiB (about
11.0 GiB) on the first successful run and 11,611,256 KiB on the final
build; the largest traced turn-6 lists held 1,364,470 plans after
champion and 2,486,046 after combat. The earlier capped retry had exited
137 near 20 GiB before scoring. The width is still an eager Cartesian fan;
other boards or an expanded card pool may need a lazy representation. No
full analyzer, regression ground truth, push or play server was run here.

### 2026-11-14 Mihail source timing (static finding 2)

The static review found that the optional {1} token payment could not be
searched when Aether Vial or Seahunter put Emperor Mihail II onto the battlefield
before a later Merfolk cast. The payment axis had learned to count either put,
but the combined plan still put Mihail after the cast. Moving a Seahunter action
earlier in `plan.actions` alone did not fix that: ordinary permanent abilities
run in the trailing activation pass.

The plan fan now retains the existing timing and adds explicit orders placing a
Mihail put at each cast boundary. In an explicit order, a Seahunter put with a
later cast runs at that position through the same activation dispatcher that
handles its trailing form; the trailing pass skips the already dispatched
action. `ApplyPlanDirect` and `AIEngine::TakeTurn` follow the same rule. The
existing Mihail payment pin then supplies the independent pay and decline
choices when the later Merfolk is cast. A Vial put after the final cast is also
an explicit variant, so the post-cast outcome remains available.

Focused evidence: the unit case `Mihail put by Vial or Seahunter can be paid for
on a later cast` checks both sources for source-first pay, source-first decline,
and cast-first timing. A second case checks before, between, and after with two
casts. Both apply searched plans in the rollout and commit them through
`AIEngine::TakeTurn`, checking token counts, trigger ordinals, and state keys.
The Release `mtg-test` target built with
`MTG_BUILD_JOBS=8 ./build.sh release -- --target mtg-test`; the first focused
case passed 73/73 assertions, all six Mihail cases passed 148/148, and the
final related-card run passed 359/359 across 20 cases. The final source changes
were rebuilt and rerun after this result was recorded. No full analyzer,
regression, external-play server, or profile generation was run for this fix.

### 2026-11-14 Prophets combat fan memory fix

The standard d5 card-score pass reached game 68 and the 28 GiB RSS watchdog
stopped it. A one-game replay (`--seed 3000069 --game-index 68 --depth 5
--budget-ms 20`) reproduced the failure. At turn 6, a list with one ready
Prophets, possible fresh Prophets, two Seahunter sources, and many Pilgrimage
tokens had 217,522 plans after the champion axis. The eager combat sacrifice
axis then multiplied those plans by both trigger choices and exceeded 29 GiB
before scoring. The earlier seed-10 d3 case exercised the champion fan; this
case exposed the later combat fan.

The Prophets pins now use the existing lazy-candidate layout. Champion variants
are materialized before the combat axis because the latter must also branch
from them; the much larger combat variants remain synthesized on demand.
This keeps the same candidate order and mixed-radix pin values as the eager
list. `MTG_PB_LAZY_CANDS_VERIFY=1` on seed 3000011/game 10 at d3/b20
compared 5,350,908 list entries with zero mismatches. The exact seed-68
d5/b20 replay completed in 156 seconds, won on turn 6, and never reached the
watchdog's 21 GiB warning threshold. The focused Merfolk tests passed 22
cases/384 assertions. The change is committed and backed up on
`origin/wip/merfolk-turns` as `25e86c28a`. The standard profile run was
restarted from this build; its result and the suite cost gate remain pending.

### 2026-11-14 final-source checkpoint

The original Merfolk source was merged with the shared branch through the C2
Cavern of Souls payment change in `3564a2fbd`. Paired held-out A/B against the
pre-C2 integration build changed **0/2,000 d0 games** (seed 933000) and
**0/200 d3/b10 games** (same seed). This is outcome evidence, not a substitute
for generating the profile on the final source. The C2 fixed-recipe profile run
is underway on two pinned cores with a 28 GiB process RSS cap. The pre-C2
1,000-game scorer completed and wrote a provisional baseline profile; its
remaining diagnostic was stopped because it measured the superseded engine.

The live repository-wide Scryfall cost audit passed 630/630 costed cards, and
`provider_audit.py --check` routes Merfolk Turns to `MerfolkTurnsProvider` with
`NotAssessed` certificate. The static viewer audit expects champion, creature
type, dig, sacrifice and Vial-charge prompts. Its three text advisories are
Silvergill Douser's targeted power reduction (opponent-inert), Rogue's
Passage's unblockable target (opponent-inert for autonomous play, present for
human play), and Seahunter's search (the dig choice). The live viewer audit,
full C2 unit suite, cost gate and regression tiers are still running or owed.

The C2 Release `mtg-test` run passed 3,120 cases and 3,597,181 assertions.
Codex-play seed 987451 matched the d5/b20 search on turn 5 with no flags.
Seed 987452 produced a provisional search-quality lead: Codex-play won on turn
6 while the search won on turn 7 by holding Aether Vial at three counters
and deploying Merfolk Sovereign. The independent player is bisecting the
opening and turn choices before this is classified. No engine-state bug was
reported on either seed.

### 2026-11-14 Codex-play correctness findings and source correction

Held-out seed 987453 surfaced two real viewer/game-state defects while still
matching the search's turn-6 win. The passive opponent's three lands were all
number 0 for this list, so Tidal Warrior and Rishadan Dockhand could not offer
them as distinct human targets. `GoldFishRunner::SetupGame` now numbers those
lands when either ability is in the deck, reusing the existing Expropriate ID
range. A Prophets combat trigger sacrificed a Merfolk token, but the shared
`SacrificePermanentAt` left its Card in the graveyard vector. The Prophets
resolver now removes that physical token after sacrifice and death watchers
finish. Both fixes have focused unit tests. The C2 scorer, pooled cost gate and
live viewer sweep were stopped because their old-binary results cannot be
adopted after this source change. The final-source rebuild is in progress;
all three will be rerun.

Codex-play seed 987452's apparent T6-vs-T7 search gap is an opening-hand
comparison, not a like-for-like main-phase miss: the shipped static policy kept
five Islands plus two Pilgrimages, while the independent player mulliganed twice.
Handing search its own opening returns T7; handing it the player's post-mulligan
opening returns T6. On the player's turn-6 board, a separate budget-sensitive
choice remains: d5/b20 through b5000 taps Silvergill Douser and wins T7, while
unbudgeted d5 passes and wins T6. Pass exists in the candidate set, so this is
a search-quality/budget finding rather than an enumerator legality bug. The
static keep policy is baseline only; exhaustive keep/bottom optimization awaits
the user's later artifact stage.

Held-out seed 987458 found a presentation error in the shared `dig` prompt for
Emperor Mihail II's optional cast trigger: the generic note said the examined
Merfolk spell would go to hand, while choosing 0 actually pays `{1}` and
creates a token. `WriteDigDecisionJson` now gives that source marker an explicit
reply note (0 pay, -1 decline). This is presentation only; the observed game
state and turn-5 win matched the search. Recheck the exact prompt after the
`mtg` rebuild.

Unbudgeted backstop screening (three games per block, d3 and d5, 2,000,000
units/game cap) found two clean, disjoint blocks. Regression seed 30131 took
7.2 core-s at d3 and 9.7 core-s at d5; overnight seed 30161 took 19.7 and
4.3 core-s. All twelve games completed, with no abandonment. Earlier five-game
blocks 30101/30106/30121 each abandoned a d5 game, so they were rejected.
The backstop rows are now provisionally present in both tiers; their GT remains
unaccepted until the corrected profile and official cost gate pass.

Held-out seed 987459 exposed a third correctness bug. Tidal Warrior had turned
Three Tree City into an Island, and the viewer showed its `{U}` tap, yet
`--validate-line 'cast=Tidal Warrior'` at decision 11 said no blue source and
omitted the spell from the menu. A payable line using two Rogue's Passages and
that City for Deeproot Pilgrimage was likewise `legal_not_enumerated`. The
actual payer already recognizes the type change; `ComputeAvailableColors`
and `BuildColorFeasibility` still read the City’s printed mana colors. Both
pre-filters now credit one unrestricted blue from an islandized land (and the
latter retains any Aura bonus). A focused unit case checks that both blue
spells are enumerated on the reproduced board. The profile, cost and viewer
runs were stopped and must restart after the incremental build.

The Deeproot line was a second gate failure after Tidal Warrior's type change
expired. Two Rogue's Passages supply the City's `{2}` activation; two Merfolk
make its fed mode add `{U}{U}`, even though that mode has zero net mana gain.
`ScaledManaNetYield` correctly keeps the flat pool's basic `{C}` credit, but
the colour-presence check used that net value and rejected blue before the
exact payer could try the fed mode. It now tests whether the colour conversion
is live, including a feeder, independently of the flat-pool credit. A focused
unit case checks the zero-net board. Both exact viewer lines and the full test
suite still need replay after the header rebuild.

Both exact viewer lines now return `accept`; the zero-net City test passes.
The autonomous d5/b20 benchmark for seed 987459 improved from turn 7 to turn
6 after the mana-gate fix, matching the Codex-play result on that seed.

Seed 987461 yielded a same-opening search-quality lead: Codex-play won on
turn 5 by putting Prophets with Seahunter, equipping Greaves and taking extra
turns; d5/b20 won on turn 6. No rules transition discrepancy was reported.
Budget escalation to d5/b200 and unbudgeted d5 still returned turn 6, so this
is a strategy/search-horizon finding rather than a budget truncation by itself.

Held-out seed 987463 found a terminal-order bug: combat damage put the passive
opponent below zero, yet the viewer asked which Merfolk to sacrifice for a
Prophets trigger. Both combat damage paths now skip that trigger resolution
once either player has lost at the damage step. A focused combat test checks
that no sacrifice, extra turn, or scripted choice is consumed after lethal.
The final-source profile and viewer audit were stopped again for this fix and
must restart from its linked binary.

Held-out seed 987464 found a `not_verified` viewer proposal that equips
Lightning Greaves to Merfolk Sovereign and then declares Sovereign's own
targeted ability on that shrouded Sovereign. Selecting the proposal attaches
Greaves and drops the illegal activation; the board remains legal. This is
within the protocol's `enumerated_proposals` contract: the menu explicitly
does not certify every action in a proposal. It remains a viewer clarity gap,
not a demonstrated state-transition bug. No broad plan-pruning change was
made during the profile run.

Held-out seed 987467 supplied a stronger same-opening search-quality lead:
Codex-play chose Dockhand plus Seahunter putting Wanderwine Prophets on turn 3
and won turn 4 through extra turns; autonomous d5/b20 won turn 5. Both kept
the same seven with no bottom. Handing the search the player's turn-3 line
then auto-playing the rest also wins turn 4, isolating the choice to that
turn. The engine transitions were legal. This is a candidate for the
earliest-win heuristic miner and paired A/B, not a rules defect.

The 18-game Codex-play sweep used disjoint seeds 987451–987468. Most outcomes
matched. The confirmed correctness defects were opponent-land IDs and token
cleanup (987453), the islandized and fed City mana gates (987459), and the
post-lethal Prophets prompt (987463); each has a focused fix above. Mihail's
pay prompt was clarified (987458). Seed 987464's shroud combination is an
explicitly uncertified proposal. Remaining same-opening search-quality leads
are seeds 987461 (T5 vs T6), 987467 (T4 vs T5) and 987468 (T6 vs T7).
Seed 987452's apparent lead is explained by different mulligans. The corrected
source passes the full Release suite: 3,125 cases and 3,597,235 assertions.

The preliminary same-seed multi-depth sanity sample (seed 910000, 150 games,
provisional pre-final card scores) is monotone: d0 avg 6.0067, d3/b10 avg
5.4933, d5/b20 avg 5.4733. D3 improved 66 games over d0 and worsened none;
d5 improved two over d3 and worsened none. D5 game index 55 took about 18
minutes on one core despite b20, yet won turn 5 at both d3 and d5. It is a
runtime outlier to inspect after the official profile; it did not cause a
search-quality regression in this sample.

The final-source 1,000-game scorer wrote the baseline profile with 16 card
scores, a one-to-five-land static keep window, and no hand-score threshold.
Its pooled cost-aggregate reframe A/B returned `COST_NEUTRAL`; the discard
evidence stage is still running. The first pooled suite-cost gate, which began
with the provisional scores, passed at 10,126.843 core-ms/game against
Hinata's 5,236.733, or 1.93× versus the 3× ceiling. The repeat pooled gate
with the final profile passed at 14,819.533 core-ms/game versus Hinata's
6,314.483, or 2.35× under the same 3× ceiling. A live 100-game viewer sweep on seeds
777000–777099 surfaced every expected decision type and passed. The static
verifier passed coverage, live Scryfall mana costs, card fields, and viewer
wiring; its sole blocking result is the expected absence of accepted GT for
the new three-tier rows at the time of that check. The reference safety scan found no unprotected games
across 93 checkouts and worktrees.

The sized smoke tier completed in 8m45s and its four GT keys were accepted:
d0 averaged 6.1280 over 1,000 games, d3/b10 5.7000 over 10, play d5/b20
5.6667 over 30, and 2HG d3/b10 6.2000 over 10. In the first ten paired
one-player games, d5 improved one over d3 and worsened none. An initial
50/100/50 searched-row smoke probe took 30m41s, so those rows were reduced
before acceptance to respect the tier's 15-minute wall budget.

The turn-three heuristic miner covered 50 games on each of seeds 987467 and
990467, with search-order expansion. It found no supported cast-order rule.
Deeproot Pilgrimage had a small inclusion benefit (six help, two hurt, 33
neutral among 41 applicable decisions), and Greaves/Vial were often slower
in sparse turn-three comparisons. These are contextual search observations,
not a safe basis for a new heuristic. In particular, the miner's continuation
oracle failed to recover the known Codex-play turn-four seed 987467 line, so
no rule was adopted from that report.

The Merfolk-specific action A/B used identical held-out openings with all
three proposed choices disabled versus enabled. On 200 d0 games (seed 9009),
the package improved eleven results and worsened none, lowering average win
turn from 6.1450 to 6.0850; one formerly unwon game won on turn 7. At d5/b20,
seed 2002 (40 games) improved two games and average 5.4000 to 5.3500 while
priced work fell from 8,024,750 to 3,659,287 units. Seed 5005 (40 games)
improved one game and average 5.7250 to 5.7000 while work fell from 12,162,651
to 3,195,275 units. No paired game became slower. A search-shuffle-salted
seed-5005 repeat improved the same game, with work falling from 11,669,674
to 3,178,464 units. Reproductions of all three searched improvements show
legal Seahunter, Greaves, Pilgrimage, or Prophets lines.

Isolating the choices attributed most of the search-cost reduction to pruning
Dockhand, Tidal Warrior, and Douser's opponent-control targets. That choice
alone saved 51.5% and 71.7% of priced work on the two searched sets without
changing a searched win turn. Prophets victim ranking alone saved 6.5% and
15.2% without a searched win-turn change. Seahunter's forced target alone
improved one game in seed 2002 but used 5.1% more work there. The three choices
together had the best measured quality and cost. The original narrowly gated
extra-turn walk changed no win turns and consumed negligible extra work; a
broader same-round walk is being tested separately before adoption.

The 40-game discard-shed census recorded 4,796 rollout decisions and zero
real-game decisions. It supplies no direct evidence for a new discard rule,
so the existing discard policy remains in place.
