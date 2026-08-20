# Analysis ledger — StompySurprise

Deck: `decks/StompySurprise/StompySurprise.cod` — mono-green elf-ramp stompy.
Elf mana engine (Llanowar/Mystic/Arbor, Priest of Titania, Elvish Archdruid, Wirewood Lodge)
into top-of-library control (Mirri's Guile, Worldly Tutor) + cheat-into-play
(Call of the Wild, Natural Order, Turntimber Symbiosis) for fat threats
(Worldspine Wurm, Craterhoof Behemoth, Terastodon, Vaultborn Tyrant, Hornet Queen,
Elderscale Wurm). 60 cards, 14 Forest + Wirewood Lodge (+4 Turntimber back-face lands).

Status: **ANALYZED — converged (2026-08-20).** All 16 missing cards implemented; verify_deck
GATE PASS with every blocking check green (coverage / card_fields / viewer + wiring /
mismatch / play_invariants / claude_sweep 0 unresolved); smoke 36/36 byte-identical after
every engine change; baseline profile + discard analysis regenerated on the final engine
(COST_NEUTRAL, STATUS_QUO_OK). Suite-style fingerprint (s1001, smoke sizing, final engine's
pre-regen profile): d0 6.173 (1000g) / d3 5.332 (250g b10) / d5 5.240 (150g b20); seed 8001:
d0 6.196 / d3 5.350 / d5 5.333 — 100% win at searched depths inside 8 turns.
Deferred (user-initiated later stages, per pipeline-ordering policy): 5g heuristic mining
(overnight), mulligan-profile generation, value-leaf.

Known conservative limits (v1, to re-check in Stage 5):
- Scaled elf mana (Priest/Archdruid): plan-level pools credit the TURN-START elf count;
  mid-plan growth (cast two elves, then tap Priest) is realized at apply time but may be
  under-projected by the enumerator → the search can miss the widest line (never overpredicts;
  fd-safe direction).
- Wirewood Lodge: untap applied AFTER main casts (elf can attack / fund trailing
  activations); the mid-payment double-tap is not modelled.
- Terastodon put onto the battlefield (not cast) resolves with K=0 (no searched axis on the
  put path).
- Turntimber put-choice candidates sized from the ENUMERATION-time top 7; a same-turn shuffle
  (Natural Order) can invalidate the named pick → falls back to the best-MV heuristic.

## Card plan (oracle fetched from Scryfall 2026-08-20)

| Card | Tier | Model |
|---|---|---|
| Llanowar Elves {G} 1/1 | 1 | mana_dork, produces [G] (Elvish Mystic twin) |
| Worldly Tutor {G} Instant | 1 | tutor_to_top + tutor_types [Creature]; searched target (GenericProvider returns all); "reveal" inert; instant-speed collapsed to main (goldfish) |
| Hornet Queen {4}{G}{G}{G} 2/2 | 1 | etb_self_creates_tokens 4 × 1/1 Insect; token flying/deathtouch NOT modeled (inert: opp never blocks/attacks); own flying inert |
| Elderscale Wurm {4}{G}{G}{G} 7/7 trample | 2 | vanilla + Trample + new `etb_life_floor 7`; ongoing damage-floor replacement NOT modeled (provably inert: no damage-to-us source exists in this sim; max self life loss = 4× Turntimber pay-3 = 12 → floor 8 ≥ 7) |
| Priest of Titania {1}{G} 1/1 | 2/3 | mana_dork + `mana_per_creature_subtype "Elf"` (feeder 0) + new `mana_per_creature_count_all` (counts each Elf on the battlefield) |
| Elvish Archdruid {1}{G}{G} 2/2 | 2/3 | same scaled elf mana (own side only) + lord params (+1/+1 other Elves, lord_excludes_self) |
| Arbor Elf {G} 1/1 | 2 | new `mana_requires_land_subtype "Forest"`: a G dork live iff a controlled Forest exists (equivalent to untap-a-Forest in a single-main goldfish; N Arbor Elves + 1 Forest = N extra G, matches repeated untap) |
| Craterhoof Behemoth {5}{G}{G}{G} 5/5 haste | 2 | Haste keyword + new `etb_team_pump_per_creature`: on ETB, +X/+X (temp, until EOT) to each own creature, X = own creature count incl. itself; trample grant inert (no blockers) |
| Worldspine Wurm {8}{G}{G}{G} 15/15 trample | 2 | Trample + dies_watch_includes_self + dies_trigger_creates_tokens 3 × 5/5 Wurm + graveyard_replace_shuffle_library extended to DEATH sites (was cleanup-only for Progenitus). Token trample inert. Reachable death path: Natural Order sac |
| Mirri's Guile {G} Ench | 2 | new `upkeep_reorder 3`: at upkeep, arrange top 3 (no bottoming, no shuffle — Ponder-family, not scry); provider-ordered wanted-first, human chooser via the existing reorder decision |
| Vaultborn Tyrant {5}{G}{G} 6/6 trample | 2/3 | enters-watcher with min-power filter: new `creature_enters_min_power 4` + own_creature_enters_lifegain 3 + new `own_creature_enters_draw 1` + self-inclusive; dies (nontoken) → token copy of itself (new `dies_trigger_copy_self_token`, reuses copy-token machinery; the copy's own enter fires the watcher). Ward {2} inert (opp never targets). "artifact in addition" cosmetic here |
| Natural Order {2}{G}{G} Sorc | 3 | additional cost: sacrifice a green creature (searched victim via plan variants, `sac_victim_id`) + search green creature → battlefield (searched target via tutor axis; new `tutor_color "G"` filter + single-target tutor_to_battlefield) |
| Call of the Wild {2}{G}{G} Ench | 3 | new activated ability `{2}{G}{G}: reveal top; creature → battlefield, else graveyard`; searched action, K activations per turn (variants); clairvoyant top known to search |
| Wirewood Lodge (land) | 3 | produces [C]; new action `{G},{T}: untap target Elf` — searched inclusion; target auto-resolved = highest-yield tapped Elf (weakly dominant; disclosed) |
| Terastodon {6}{G}{G} 9/9 | 3 | ETB "destroy up to 3 noncreature permanents → controller gets 3/3 Elephant each". Opponent has no noncreature permanents in this sim → only real mode = destroying OWN permanents for 3/3s. Searched K = 0..3 (rides chosen_x); candidates narrowed to own Forests, tapped first (provider narrowing, disclosed) |
| Turntimber Symbiosis // Turntimber, Serpentine Wood | 3 | MDFC spell//land. Back: synthesized land, produces [G], pay-3-life-or-tapped (new `mdfc_back_pay_life 3`; land enumeration extended to offer the back of a nonland front; hand keep/land counting must see it as a land). Front {4}{G}{G}{G}: look 7, put ≤1 creature onto battlefield (+3 +1/+1 counters if mv≤3), rest to bottom (deterministic order — "random order" unobservable); searched put-choice |

## Engine changes (all param-gated; other decks byte-identical)

1. `PermanentManaYield` gains a state-aware overload; elf-scaled dork yield = elf count.
   ColorFeasibility disables itself (usable=false) when a scaled DORK is on board (same
   rule as Three Tree City).
2. `mana_requires_land_subtype` liveness gate at dork mana sites (pool build + payment).
3. ETB cascade (OnGoblinEnters): team-pump-per-creature (Craterhoof), life floor
   (Elderscale), destroy-own-for-elephants (Terastodon).
4. Enters-watchers: min-power filter + self-include + draw rider (Vaultborn).
5. Death sites: dies-trigger copy-self token (Vaultborn); graveyard shuffle-back
   replacement honored on death (Worldspine).
6. Upkeep sites (both worlds): upkeep_reorder (Mirri's Guile).
7. New searched actions: Call of the Wild activation, Wirewood Lodge untap.
8. Natural Order: sacrifice-a-creature additional cost + single tutor_to_battlefield
   with color filter.
9. MDFC spell//land: DB back-face synthesis from a nonland front + land-enumeration
   + hand-land-count extensions.

## Viewer decision surface (2c-ter classification)

| Card choice | Bucket | Wiring |
|---|---|---|
| Worldly Tutor target | A | tutor_target plan variants (GenericProvider returns all) |
| Natural Order: sac victim + fetch target | A | plan variants (sac_victim_id × tutor_target) |
| Turntimber: face choice; pay-3-life; put-choice | A | land_face + land-entry chooser (existing); put-choice via plan variants |
| Call of the Wild: activate (× K) | A | main_phase plan variants |
| Mirri's Guile reorder | A | reorder decision (g_play_top_chooser) at upkeep |
| Terastodon: K destroyed | A (narrowed) | chosen_x variants; WHICH Forest auto (fungible) — disclosed |
| Wirewood Lodge: which Elf | auto | max-yield tapped Elf — disclosed (weakly dominant) |

## Claude-play sweep
- commit: `WORKTREE (post-c6200d95, StompySurprise onboarding — sweep ran on the final engine
  minus the three post-sweep fixes below; each fix was verified by replaying the flagged
  positions on the final binary)`
- seeds: 9101–9104 games: 16 (4 seeds × gi 0–3, Sonnet agents, Workflow fan-out)
- flags: 0 unresolved
- Win comparison: Claude 5.6 avg vs search 5.6 avg on the 14 completed games (Claude FASTER in
  3 games — s9102 gi0/1/2, 6 vs 7: all three are Call-of-the-Wild double-Worldspine lines the
  search also finds at higher budget; win-turn deltas are the weak signal, noted not chased).
- Flag resolutions (all verified by direct replay on the fixed binary):
  1. **Wirewood Lodge action offered while the Lodge is tapped** (confirmed, s9101 gi2/gi3) —
     a silent no-op plan that kept the human-play segment re-prompt loop alive forever.
     FIXED: emission now requires the source untapped, and in human play a matching creature
     already tapped (the option reappears mid-phase once an Elf taps). Verified: 0 Lodge
     plans at the previously stuck decision.
  2. **Natural Order absent from a crowded T3 plan display** (uncertain, s9101 gi1/gi3) —
     display-cap starvation: 637 enumerated plans, the first 200 all Arbor+double-Tutor
     variants. FIXED: diversity-aware display cap in WriteDecisionJson (one representative
     per distinct land+cast set first; true indices preserved). Verified: Natural Order now
     visible at the same decision.
  3. **`dragon` multi-pick dialog double-asking on Natural Order** (confirmed, s9102 gi3) —
     the put dialog fired even though the plan variant already carried the searched target,
     and its multi-int reply desynced agents' choice streams. FIXED: dialog suppressed when
     the put-list came from the plan (`preferred` non-empty); Dragonstorm unchanged.
  4. **"Worldly Tutor fetched the wrong card"** (s9104 gi3) — FALSE POSITIVE: the plan's
     actions were NO→Priest (sac Mystic) + WT→Worldspine-to-top; replay confirms execution
     matched exactly (Priest on battlefield, Mystic in graveyard, Worldspine revealed to
     top). The agent had swapped the two targets, confused by the (now removed) double-ask.
  5. **Picking the enumerated "(nothing)" plan re-prompts instead of ending the phase** —
     pre-existing engine-wide viewer behavior (the documented pass is `-1`); became a trap
     only in combination with fix-1's phantom. No change.
- Post-fix: smoke 36/36 PASS (byte-identical); profile regenerated on the final engine.

## Approved deferrals

(none yet — pending user sign-off in Stage 6; candidates listed as "inert"/"disclosed" above)

## Verification (Stage 5) verdicts

- **Smoke byte-identity (twice, after all engine changes + after the provider-detect fix):
  36/36 PASS, zero per-game movement** — every extension is correctly param-gated.
- **verify_deck gate: PASS** (coverage / card_fields / viewer self-guard + surface sweep /
  viewer_wiring / mismatch / play_invariants all green; claude_sweep recorded below).
- **5a mismatch harnesses: CLEAN** — zero `[nonconv]`, zero `[fd-diverge]` across seeds
  7001+7002 × 60 games at d3 b20 AND d5 b20 (lookahead-bottoming, threads 1).
- **5b multi-depth sanity (seed 8001)**: d0 6.196 (500g, 36 unwon-by-T8 greedy tail —
  acceptable d0 churn), d3 5.350 (200g, 100% win), d5 5.333 (150g, 100% win). Monotone,
  plausible T4–6 clock for elf ramp. Outliers read: wt=8 games are mull-to-5 land screws.
  One oddity inspected (s8028 gi7): the search holds a drawn Forest T2–T4 — proven
  outcome-equal (b10 == b100 == b1000 all T8; the land-line tiebreak is indifferent), a
  cosmetic no-op, not a play defect.
- **5c budget starvation: none** — b10 == b1000 on the slowest inspected game.
- **Mechanic coverage census (40 games, s9301 d3)**: every card cast/played — Natural
  Order ×16 (sac+fetch), Turntimber ×8 as sorcery + ×12 as land (both faces),
  Call of the Wild ×14 with 61 reveals, Worldly Tutor ×29, Craterhoof ×3 (T6 haste
  attack for 18 confirms pump+haste), Worldspine ×4, Terastodon ×4, Vaultborn ×2,
  Wirewood Lodge ×10 land drops — so the clean mismatch gate is not vacuous.
- **Provider**: GenericProvider via an explicit `stompy` detection signature (must WIN over
  `anti` — Worldly Tutor's tutor_to_top otherwise misroutes the deck to
  AntiLifegainProvider; caught before any measurement shipped, Stage-4 profile regenerated
  on the fixed binary).
- **Cost audit**: full-DB run green; 4 new entries were 429-transient and verified by hand
  against the saved Scryfall JSON (`logs/stompy_analysis/*.json`) — all match.
- **Recommended next (user-initiated)**: 5g `mine_heuristics.sh` overnight run
  (GAMES=300–500, ≥2 seeds) to ground any provider ordering rules; then the
  mulligan-profile + value-leaf stages per the pipeline-ordering policy.
