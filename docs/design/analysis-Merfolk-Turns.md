# Merfolk Turns analysis checkpoint

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

Stage 2: implement the missing cards and repair Three Tree City. The critical
play interactions are Prophets' champion and extra turn, Seahunter's library
put, Mihail's top-library casts and paid token trigger, Deeproot's tap trigger,
and Tidal Warrior's temporary Island effect. Do not run or accept an analyzer
profile with these missing.

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
