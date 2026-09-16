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
- ~~Scaled elf mana (Priest/Archdruid): plan-level pools credit the TURN-START elf count;
  mid-plan growth (cast two elves, then tap Priest) is realized at apply time but may be
  under-projected by the enumerator~~ **CLOSED 2026-08-20** — see the growth/burst addendum
  at the bottom (`MTG_DORK_GROWTH`, fixture `test/scenarios/stompy_dork_growth.json`).
- ~~Wirewood Lodge: the mid-payment double-tap is not modelled.~~ **CLOSED 2026-08-20** —
  the untap BURST is modelled through the whole payment stack (`MTG_UNTAP_BURST`, fixture
  `test/scenarios/stompy_lodge_burst.json`); the post-cast searched untap action remains
  for the attack / trailing-activation uses.
- ~~Terastodon put onto the battlefield (not cast) resolves with K=0 (no searched axis on the
  put path).~~ **CLOSED 2026-08-20** — the put path picks K by the resolution-time lethality
  heuristic and the CAST path emits the searched K-set (which v1 never actually emitted); see
  the Terastodon addendum (`MTG_TERA_K`, fixture `test/scenarios/stompy_terastodon_k.json`).
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
3. ETB cascade (FireOwnEtbTriggers): team-pump-per-creature (Craterhoof), life floor
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

## Addendum 2026-08-20 — scaled-dork growth + Wirewood Lodge burst (USER follow-ups)

USER (2026-08-20): *"We should play every elf we can from the list without tapping scaling
dorks or Wirewood Lodge. Then we should play every elf remaining with scaled mana and see if
there are any remaining untapped"*; *"Lodge should tap for colourless when no elf taps for GG
or more and also allow for tapping an existing untapped elf as needed in order to generate
mana and using the Lodge to untap it"*; *"we need to be a bit careful ... allow using it for
colourless early if there are no scaling sources at 2+ elves. Otherwise the colourless could
be stranded"*; *"There is no haste, so scaling sources already on the board are what matters."*

Both v1 conservative limits above are now closed. This is the count-scaling generalisation
`scaling-source-widening.md` anticipated in its "Scope beyond domain_mana" section.

### MTG_DORK_GROWTH (default ON; =0 restores the v1 gap)

A live scaled dork's one-tap yield grows by 1 per matching creature cast before it taps.
Three coordinated pieces (shared reader in `EngineFlags.h`):
- **EnumeratePlans credit** (`TurnSolver.cpp`): per subset, each live scaled dork earns
  live × (matching creature casts the un-grown pool can stage) — all of them under the rock
  rule (`pool.CanPay(summed feeder costs)`), else the longest *selection-order* scalar
  prefix, which is exactly the order the executor's stable within-tier sort realises.
  EnumeratePlans only, never Solve (Medallion precedent). The scalar `ManaPruneBound` gets
  the same addend (the haste-unlock lesson: bound and credit move together), and the
  selection-exact gate bails when growth is live.
- **Cast order** (`GenericProvider::CastOrderRank` tier 9): creatures feeding a live scaled
  dork cast just before other creatures (the fatty no longer taps the Priest first).
- **Tap order** (`GenericProvider::ManaSourceRank` 61): scaled dorks tap LAST among sources
  (fixed Llanowar before Priest once the reservation releases dorks).

### MTG_UNTAP_BURST (default ON; =0 restores the v1 gap)

The Lodge's untap modelled as a mana BURST through the whole payment stack, via the
net-cancellation identity: feed pip ({G}) and target output ({G}) share a colour, so
"tap Elf for N, pay {G} + tap Lodge, untap, re-tap for N" ≡ one Lodge tap worth (N−1) {G}
with the Elf's tapped state unchanged (legal as a pre-float at main-phase priority; CR 605.3
only restricts activations inside one payment window). Sites, all through the shared helpers
`UntapBurstFeedColor/UntapBurstBestYield/UntapLandBurstNet` (SpellEffects.h):
- pool credit (`AddSourceToPool`: replaces the 1 {C} when net ≥ 1 — never both),
- flow oracle ({C}+{G} colset, sound over-credit), backtracker branch (requires the target
  TAPPED at the node; taps stay monotone so memo/undo are untouched), greedy kind-4,
- `ManaSourceRank` 63: reserved past even the scaled dorks while a 2+ scaled Elf is live —
  its net reads the count at fire time — else plain {C}-land rank (the USER's stranding
  guard), and `BuildColorFeasibility`/`ComputeAvailableColors` stand-downs.
- The burst credit reads the SNAPSHOT count (same-turn growth is not compounded into it) —
  conservative, disclosed.

### Measured (one binary, profile held fixed across arms, d3 b20)

| arm | s8001 (200g) | s8002 (200g) | s7001 (60g) | s7002 (60g) | d5 s8001 (100g) |
|---|---|---|---|---|---|
| both OFF | 5.3350 | 5.3350 | 5.2500 | 5.2500 | 5.3600 |
| growth only | 5.3100 | 5.3050 | — | — | — |
| burst only | 5.3300 | 5.3300 | — | — | — |
| **both ON (shipped)** | **5.3000** | **5.2950** | **5.2000** | **5.2000** | **5.3400** |

Every configuration improves; growth carries most of it and the two compose slightly
super-additively (burst rides grown counts). No arm regressed anywhere. With the profile
then regenerated on the final engine, the shipping configuration reads d3 s8001 5.2950 /
s8002 5.2900 (200g each), d5 s8001 5.3400 (100g).

### Verification

- Smoke 36/36 byte-identical, 0 play-changed (all sites param-gated).
- Fixtures: `stompy_dork_growth.json` + `stompy_lodge_burst.json` — both `validate_line`
  **accept**, both flip to `illegal` with their lever =0, each independent of the other's
  lever; `fivecolour_domain_widen` + `fivecolour_haste_dork_mana` still pass.
- Executor realisation (not just validate): growth board plays land → Llanowar → Mystic →
  Archdruid on T4 (Archdruid paid by the grown Priest); Lodge board casts Worldspine Wurm
  T6 via the burst with a Llanowar left untapped and the land drop unused.
- Mismatch harnesses re-run on the new engine: zero `[nonconv]` / `[fd-diverge]`, seeds
  7001+7002 × 60 games, d3 b20 and d5 b20 (threads 1, lookahead-bottoming).
- verify_deck GATE PASS on the new engine; profile regenerated on the final engine after
  measurement (arms above shared the pre-regen profile for fairness).

### Deferred / open

- Terastodon: USER flagged "we might want to tweak Terastodon slightly" — no spec yet;
  the v1 put-path K=0 and Forests-only-victim narrowings still stand.
- Lodge burst growth compounding (snapshot-count credit) — conservative, likely negligible.

## Addendum 2026-08-20 (2) — Terastodon destroy-K heuristic (USER follow-up)

USER: *"a heuristic to narrow and to widen the choice to all valid targets, but only if we run
out of forests. The only valid possibilities are no elephants so we can afford other threats,
(should be done only if we can drop a real threat) a number that kills them next turn and a
number that kills them the turn after next. Note that Worldly Tutor into Call of the Wild
activation is a real move for this deck"*; *"I suppose there is an argument for allowing more
elephants if we can also drop Craterhoof this turn, but that is a bit more rare."*

**Implementing this exposed a v1 defect:** the destroy-K axis was wired through
`chosen_x` → cast → `FireOwnEtbTriggers(etb_kx)` in both worlds, but **no K > 0 cast variant was
ever emitted** — the "searched K = 0..3" disclosure was the wiring, not the search. Reproduced:
lone Terastodon off 8 Forests vs 20 life won T8 with zero Elephants ever made (K=1 wins T7).
Now a fixture: `test/scenarios/stompy_terastodon_k.json` (expect_win_turn 7; fails at 8 with
the lever off).

One lever, `MTG_TERA_K` (default ON; =0 restores v1 = Forests-only pool, no K emission):

- **Projected K, decided at RESOLUTION for both entry paths** (`ProjectEtbDestroyK`,
  SpellEffects.h): ONE pick, no searched fan — USER 2026-08-21: "I don't want to roll them all
  out. That's too expensive. It would be better to make a projection"; and "all we need to do
  is avoid a case where we miscalculate the turn we can win on ... then figure out what is
  required in terms of elephants to end the game on that turn." The autonomous CAST now rides
  the same `kEtbKxHeuristic` sentinel as the puts (AIEngine's stack entry passes the sentinel
  through), so K is projected mid-plan — the battlefield already holds the plan's earlier
  casts and the REMAINING pool prices what the plan can still do today, which is the practical
  form of "we might be able to see it in the current turn plan? ... the easiest". Earliest
  winnable horizon, then the required (smallest) K for it:
  1. **h=0** — a Craterhoof still lands today (in hand / live Call of the Wild off the
     clairvoyant top / Worldly Tutor + Call / Natural Order fetch, route within the remaining
     pool): smallest K whose swing is lethal THIS turn ("maybe playing maximum elephants is
     not necessary ... what is crucial is taking Craterhoof plays into account"). Sick
     Elephants/Terastodon only feed the Hoof's X; skipped when a Hoof already resolved (its X
     is locked).
  2. **h=1** — smallest K with a lethal plain next-turn swing; an AFFORDABLE power-4+ hand
     threat's power folds in, so "keep the permanents and develop" falls out as K=0.
  3. **h=1′** — only when h=1 fails: a Craterhoof dropped NEXT turn ("ensuring we have the
     mana is trickier": next-turn mana = all board sources' yields regardless of tapped-ness,
     minus the K eaten, plus a land drop).
  4. **h=2** — smallest K lethal over two swings; else **cap** ("go for broke").
  Projections use EffectivePower net of until-EOT bonuses for future horizons, no lord bonuses
  (under-counting only asks for MORE elephants); every pick is inside a rollout-scored plan.
  `MTG_UNPRUNED`/`terak` and human play keep the explicit 0..cap fan (positive K flows as
  before). Measured: the emission-time projection was identical to the
  transient searched fan on every config (d3 5.0600/5.0550, d5 5.1100) at equal cost, and the
  final resolution-time form edges BOTH out (d3 5.0550/5.0500, d5 5.1100) — its census is MORE
  accurate than any emission-time guess (it sees the real post-payment attacker set: the
  Terastodon+Craterhoof probe kills T5 with the minimal Elephant count, elves tapped for mana
  correctly excluded from the swing).
- **Widened victim pool** (`EtbDestroyVictimClass`, shared by emission cap and resolution):
  Forests (v1 order preserved) → other lands → mana rocks (Sol Ring) → other noncreatures →
  Call of the Wild LAST (the Worldly-Tutor-into-Call engine is the most valuable victim).
  Verified: with only 2 Forests up, K=3 ate both plus Sol Ring and won T6 instead of T7; with
  3 Forests available it ate Forests only and spared the Ring.
- **Put-path K** (`HeuristicEtbDestroyK` + the `kEtbKxHeuristic` sentinel): Natural Order /
  Call of the Wild / Turntimber puts now pick K at resolution by the lethality windows alone
  (no cap fallback — no per-K rollout branch exists there, so stay conservative; the plan
  containing the put is still scored with the pick). Sentinel is -2, NOT -1: the executor's
  stack entry records chosen_x only when positive, so a searched K=0 hard-cast arrives as -1 —
  overloading it would have the executor heuristic-override a searched K=0 the rollout kept
  (a divergence by construction). Verified both directions: at 20 life with 11 power the put
  correctly took K=0 (already two-swing lethal; kept the Forests); at 26 life it took K=1 and
  won a turn.

The v1 ledger line "Terastodon put onto the battlefield resolves with K=0" is superseded by
this addendum.

### Measured (one binary, MTG_TERA_K=0 vs on, d3 b20; baseline == the growth/burst shipping
numbers exactly, confirming no other change leaked)

| config | off | on | delta |
|---|---|---|---|
| d3 s8001 (200g) | 5.2950 | **5.0550** | −0.240 |
| d3 s8002 (200g) | 5.2900 | **5.0500** | −0.240 |
| d5 s8001 (100g) | 5.3400 | **5.1100** | −0.230 |

~5x the growth+burst gain — consistent with the v1 defect (no Elephant was ever made) and the
put path being the deck's most common Terastodon entry (Natural Order x16 in the 40-game
census). Verification: smoke 36/36 byte-identical + 15/15 scenarios on the final binary;
mismatch harnesses clean (0 nonconv / fd-diverge, seeds 7001+7002, d3 b20 + d5 b20); the
`stompy_terastodon_k` fixture accepts at T7 and reverts to T8 under MTG_TERA_K=0; viewer +
card-field audits green. Profile regenerated on this final engine after the A/B
(COST_NEUTRAL / STATUS_QUO_OK again; all three fixtures re-pass), landing the shipping
configuration at d3 s8001 5.0600 / s8002 5.0550 (200g each), d5 s8001 5.1100 (100g) —
verify_deck GATE PASS on the regenerated profile.

## Addendum (2026-08-21): provider misroute fix, cleanup-discard buckets, tutor ranking

### The deck was silently running under GoblinsProvider

Hornet Queen's `etb_self_creates_tokens` trips the Goblins detection signature, and the goblin
return sat above the stompy one in `DetectDecisionProvider` — the exact Mirrorwing/Instigator
misroute class the detection comments warn about. Every measurement above therefore ran under
GoblinsProvider's hooks. The live ones for this deck: `TutorCandidates` (a goblin-tuned
power ranking ordered Worldly Tutor / Natural Order targets) and `UseLethalShortCircuit`;
`ForcedEarlyLandName` is hard-coded "Mountain" (never fired), sac-outlet/echo hooks inert.
Fixed by routing to a new `StompyProvider` (Generic + the hooks below) detected BEFORE goblin.
Attribution proof: the fixed binary pinned back via `MTG_PROVIDER_DECK=decks/Goblins/Goblins.cod`
reproduces the shipped 5.0550 (d3 s8001) exactly.

### StompyProvider hooks (all three are the whole provider)

- **Cleanup-discard role-bucket policy** (USER-AUTHORED 2026-08-21, iterated to a worst-case
  ALLOCATION the same day on user review; `MTG_STOMPY_BUCKET_DISCARD=0` → generic base): the
  hand divides into `<mana> <threats> <enablers>` role buckets and the worst case (a deep
  flood discarding to 7) keeps the tight breakdown **4 mana / 2 threats / 1 enabler**;
  buckets the board already covers shrink, and unneeded slots refill with threats or
  additional scaling mana (user). Dead copies (a hand Call/Guile with one on board) belong
  to no bucket and shed first. MANA: EFFECTIVE yield everywhere — every own permanent counts
  (lands/rocks by produces_amount, a scaling Priest/Archdruid by its live Elf count), hand
  cards by prospective yield; greedy keep up to a target (floor 6, raised to 7 when the
  cheapest live route or cheapest kept hardcast needs 7+) but never more than 4 slots, under
  a COMPOSITION preference (user): lean 1 land + 3 accelerators ("you might draw another
  land in the next two turns") but "you always want at least a land for next turn if you
  can" — best land first, then accelerators, backfilling from whichever side remains when
  the other runs short. Within accelerators: Sol Ring first when available ("a generically
  insane card"; an unpicked Ring also never sheds as early excess — slack zone only, last
  of the slack mana), then one 1-mana dork if neither hand nor battlefield has one, then
  the rest by yield (measured byte-identical at every train/held-out cell and 39/39
  smoke — it only reorders slot membership at these seeds). THREATS: spares (same-name in library, or a hand duplicate) never take
  a slot; a route-covered last copy unplayable from hand (mv > reach) sheds early; slot
  preference 7-8-drops, an 11-drop "only very very rarely" (only when nothing cheaper can);
  the hoof-role last copy always takes a slot (pitching it kills every fetch route and the
  h=0 projection). Routes: Call on board 3 / in hand 4, Natural Order + green fodder 4,
  Turntimber front 7, judged against reach = board + all hand yield. ENABLER: one slot
  (zero with Call on board), priority Call > NO > Tutor > Guile. The list names the full
  hand (the Mirrorwing gi295 lesson) and is a CHOICE, not a search
  (`CleanupDiscardSearchWidth()` stays at the base 1 — the ranking's front item IS the
  shed). **Shed-order subtlety that cost a real turn (game-seed 8095):** loose mana the
  greedy WANTED but the 4-slot cap truncated is not "excess" — a draft shed a 3rd Forest on
  an empty board ahead of a spare Worldspine (the baseline pitched the Worldspine, whose
  shuffle-from-anywhere trigger the clairvoyant rollouts then exploited into a T6; either
  way the Forest shed is wrong on its face, T6→T7 at d3 AND d5). Cap-truncated mana now
  sheds in the SLACK zone (after spares/dead lasts/surplus enablers, before slack dorks and
  slack threats), so it never survives the worst case but a 1-2-card discard takes real
  junk first. Final measurement: outcome-identical at every cell — train d0/d3×2/d5 =
  6.0040/4.9850/4.9850/4.9700 (searched digests byte-identical to the shipped baseline) and
  held-out 5.9480/4.9200/4.9150/4.8400; the only GT delta across the whole restructure is
  the play digest of one UNWON greedy game (smoke d0 gi253), re-accepted. Fixture
  `stompy_bucket_discard.json` (depth 0 ON PURPOSE) still proves the ranking: bucket keeps
  the hoof and kills T5; `=0` (generic highest-MV) sheds it and cannot win. A real cleanup
  discard occurs ~once per 200 games — correctness insurance for flood hands, not
  throughput.
- **Tutor target ranking** (`TutorCandidates`; honors `MTG_UNPRUNE=tutor`): Generic returns
  candidates in LIBRARY (shuffle) order and the width-6 axis then scores a random six of this
  deck's eleven creature names — the closers fall out of the window. Authored order: hoof-role
  first, then power + ETB-token count descending (Hornet Queen's four bodies count), elves at
  the tail. This is a window, not a pick — the search still scores the six.
- **UseLethalShortCircuit true**: wide elf/token/hoof boards are the shape it exists for, and
  Goblins had it on — dropping to Generic would have silently removed it.

### Measured (one binary per row where noted; d3 b20 200g s8001/s8002, d5 b20 100g s8001)

| build | d3 s8001 | d3 s8002 | d5 s8001 |
|---|---|---|---|
| shipped (Goblins-routed, above) | 5.0550 | 5.0500 | 5.1100 |
| routing fix only (Stompy, generic tutor order) | 5.0850 | 5.0800 | 5.1100 |
| + authored tutor ranking (FINAL) | **5.0250** | **5.0250** | **5.0300** |

The accidental goblin ranking was worth −0.03 at d3 over shuffle-order; the deck-aware ranking
beats it on every config, with the largest gain at d5 (−0.08 vs shipped): depth exploits a
window that reliably contains Craterhoof and the real closers. Verification on the final build:
smoke 36/36 byte-identical + 16/16 scenarios (new fixture included); provider line reads
`provider=Stompy`; mismatch harnesses re-run clean (see below).

NOTE: the deck's profile/keep-table artifacts were generated under the Goblins routing; a regen
on this engine is recommended before the next artifact-bound stage (value leaf / mulligan gen).

## Addendum (2026-08-21): branching-factor audit + profiling

Question: which situations drive this deck's search branching, and is any of it pathological?
Instruments: MTG_BRANCH_STATS (odometer by driver card, 60g d3+d5), MTG_DUMP_UNITS (per-game
work across the 500-game 3-config suite), callgrind on the heaviest game (build/Profile; perf
cannot write samples in this container).

- **Worldly Tutor never appears as an odometer driver -- but that was an INSTRUMENT BLIND
  SPOT, not an absence (user caught this).** Tutor targets are an additive post-dedup axis: the
  fan in EnumeratePlansWithLand appends full Plan variants AFTER the odometer is recorded, and
  every one is scored like a base plan. branchstats now has a third table (RecordAxis, one call
  site at the enumerator's return) counting plans-returned-for-scoring per axis. Measured (d3
  60g): **1.38M tutor-variant plans vs 2.32M base -- 37% of ALL plans scored** (avg 27 per fan,
  max 1255 in one enumeration): the tutor axis is the deck's single biggest scored-plans
  multiplier. Priced by collapse (MTG_TUTOR_WIDTH=1): -29% total units and ~half the d3 wall
  clock, but +0.105/+0.105/+0.130 avg win turn -- expensive AND clearly worth it. Width sweep
  with the authored ranking: W3 = 5.0300/5.0300/5.0300 (-13% units, +0.005 d3), W4 noise-equal
  to W3, W6 best (5.0250/5.0250/5.0300). Differences are one-game noise; outcome is the king
  metric, so the width-6 default STANDS and no narrowing is adopted.
- **Turntimber Symbiosis is the #1 driver** (sum_odo 6.2M/8.2M at d3/d5, avg 117-133x per call,
  max 6144x; two castable copies multiply together, avg 623x). Its named put variants are
  deliberately exempt from the axis collapse and the signature dedup, and they fan in
  EnumeratePlans AND every rollout-leaf Solve. Natural Order is #2 (sac-victim variants,
  max 1536x). The rest is the plain 2^N cast-subset odometer (elf swarm), bounded by the lethal
  short-circuit and mana prune.
- **The tail is budget-bound, not runtime-bound.** b20 = 18k units/decision; median game 48k
  units, p90 ~350k, worst (game-seed 8029, gi28@s8001) 1.156M == ~64 fully saturated decisions,
  IDENTICAL at d3 and d5 -- the budget, not depth, binds. No SLOW-GAME (>30s) in 500 games.
  Callgrind on that game is flat: SolveUncached + subset lambda ~10%, memo-key hashing ~6.7%,
  Action copies ~3%, lord-bonus scans ~3% -- search doing search, no micro-hotspot.
- **MTG_TT_PUT_WIDTH (new lever, default 0 = historical, byte-identical -- smoke 36/36):** cap
  the Turntimber put fan to the top W by EvalCard + decline (human play exempt). Swept W in
  {2,3,4} on the 3-config suite: W=3/4 outcome-identical to baseline; W=2 improved exactly ONE
  distinct game (seed 8033: T5->T4, -0.005/-0.010 train) with total units flat (saturation
  absorbs the cap -- the freed budget re-spends elsewhere). Held-out (s9001/s9002 d3 200g x2,
  s9001 d5 100g): ZERO -- identical avgs both arms; mismatch harnesses under W=2 clean (0
  nonconv / 0 fd-diverge, 8 runs). **NOT ADOPTED** (train gain was one game and did not
  generalize); the lever stays as a measured diagnostic for future budget-tuning work.

Conclusion: the deck's branching is healthy at b20 -- the Turntimber odometer is architecturally
ugly but the deterministic budget converts it into (measured-negligible) search-quality dilution
rather than wall-clock, and play quality is insensitive to narrowing it. If a future budget CUT
(b5/b10) or a Turntimber-heavier list revisits this, re-measure W=2 first -- the one train game
it won is exactly the dilution shape.

## Addendum (2026-08-21): USER-AUTHORED tutor lethality heuristic + uncertainty-gated truncation (ADOPTED)

`StompyProvider::TutorCandidates` ranks Worldly Tutor / Natural Order targets by the user's
lethality calculation ("it depends on when we can deploy it and how low the opponent is"):
threat pool = team pump + power-6+ bodies, per-candidate deploy turn (a live Call / hand
Turntimber flips the top for ITS cost regardless of the threat's MV; hard-cast waits for the
draw + mana at a conservative +1 source/turn), haste-aware wake turn, swing vs the opponent's
CURRENT life; Natural Order identical with deployment free (t=0, sacrifice docked, "doesn't
require any calculation"). Swept as MTG_STOMPY_TUTOR_HEUR V1/V2/V3; V3 (this design) won every
config train AND held-out (largest single heuristic gain measured on this deck); scaffold
deleted per the skill.

**Truncation is the CANDIDATE LIST, not a width** (user: "I hate the width idea in general...
return a list of serious contenders... branch only when we are absolutely uncertain"). A fixed
TutorSearchWidth=3 was briefly shipped and REPLACED by the uncertainty gate:
  * a LETHAL pick exists -> the calculation has decided: emit it alone (+exact ties);
  * NO lethal computable -> genuinely uncertain (the right fetch couples with the whole plan,
    down to the turn-1 land face in traced games): emit the top-3 threats PLUS the scaled
    dorks (a threats-only list here truncated the tutor->Archdruid engine line out of one game
    entirely -- T7 win became UNWON -- ramp-vs-threat IS the uncertain choice);
  * starving (<=2 sources, Worldly Tutor only) puts the cheapest scaled dork in front.
W1 ("just take the top entry") was tested honestly and is real-refuted: 26/29 diverged games
lose EVEN AT UNBOUNDED BUDGET -- no static front covers what the searched contenders decide.
A permanent-board-stats tie-break for the non-lethal case was tried and reverted (d0, the
purest front read, measured it 0.032 worse than the swing tie-break).

**METHOD CORRECTION recorded:** the first unbounded-budget classifications used a BROKEN repro
(`--seed base --game-index gi` replays the seed-`base` game every time -- in a goldfish
game-index only drives opponent spawns; the correct repro is `--seed base+gi --game-index gi`,
exactly what SLOW-GAME lines print). Every unbounded claim was re-run on corrected repros; the
W3-era "regressions are churn" verdict happened to survive, the constant-5.0000 tables did not.
See memory batch-game-repro-seed; ALWAYS validate a repro against the batch .wins at the
original budget first.

| arm (train 8001/8002 / held-out 9001/9002) | d0 1000g | d3 200g x2 | d5 100g |
|---|---|---|---|
| static ranking, full window (pre-heuristic) | 6.1040 / 6.0490 | 5.0250 / 5.0150 | 5.0300 / 4.9400 |
| lethality ranking, full window              | 6.0040 / 5.9480 | 4.9850 / 4.9300+4.9250 | 4.9800 / 4.8500 |
| lethality ranking + uncertainty gate (**SHIPPED**) | 6.0040 / 5.9480 | 4.9850 / 4.9200+4.9150 | 4.9700 / 4.8400 |

Gate vs full window per-game: train 2-up/2-down at d3 (both downs vanish unbounded = churn),
1-up d5; held-out 4-up/2-down per d3 seed, 2-up/1-down d5 -- and the one down that survived the
unbounded test (seed 9133, a Worldly Tutor + Natural Order -> Craterhoof T5 the gate declined)
is a CLAIRVOYANCE ARTIFACT (user's call): under MTG_SHUFFLE_SALT_SEARCH decoupling (salts 1 and
2) the full window's T5 collapses to the gate's own T6 -- the kill existed only because the
search foresaw the exact post-Tutor/NO reshuffle order. The gate therefore has ZERO honest
removed lines on everything measured. Axis cost: tutor-variant plans 1.38M -> 0.55M (-60%;
19% of scored plans, was 37%). Escape hatches: MTG_UNPRUNE=tutor (full list), MTG_TUTOR_WIDTH.
Verification: smoke 36/36 + 16/16 scenarios; mismatch harnesses 0/0 across 8 runs; d0
byte-identical to the ungated ranking (753c0bbc...).
Net from the session morning baseline: d3 5.0550 -> 4.9850 (held-out 4.9200/4.9150), d5
5.1100 -> 4.9700 (held-out 4.8400).

**Deferred (user idea, 2026-08-21, not yet built or measured):** `!(lethal computable) &&
!starved -> HOLD Worldly Tutor` — when the calculation can neither name a kill nor is the board
mana-starved, the tutor's shuffle-then-top may be worth more later (cast it the turn a lethal
becomes computable, or the turn before an outlet goes live) than spent on a speculative
board-building top now. Would live beside the gate in StompyProvider (a cast-gate hook, not a
target-ranking change — the same shape as TreasureHunt's ShouldCastDrawEngine defer). Measure
against the shipped gate before adopting; note the deck's 1-mana tutor often has spare mana, so
the hold's cost is mostly the information delay, and clairvoyant seeds will flatter EITHER arm
— use the NC-style read if the clairvoyant delta is suspicious.

## Addendum (2026-08-22): value leaf + d6 play policy ADOPTED, and what is still open

Shipped in `78f57f0d` as `decks/StompySurprise/StompySurprise.value.json` with an **enabled**
play block: `target_depth 6, escalation_cap 5, budget_ms 20`. Both halves matter and the second
is easy to lose — the staged model ships `value_play: null`, so adopting it as-is silently takes
the built-in d5 default, which is exactly the arm d6 beat. Verify with the startup line:
`[play] depth=6 budget=20ms source=value_play`.

`escalation_cap` is clamped to 5 (the deepest MEASURED heuristic rung); arming past the ladder
spends budget reaching a depth the crossover can never take. The crossover `[1,1,1,2,3,4,6,6]`
rides inside the model. `value_trust_depth` stays UNSET, and post-qdead that is a REAL verdict
rather than a sample-size artifact: the paired basis is now ~1592 games (resolution 0.0019 <
tol 0.0020) and the leaf needs depth 7 to match `h_conv = 4.8807`, past the shipped depth.

### Evidence

| measurement | result |
|---|---|
| phase E gate (leaf vs NO sidecar), 8 seeds x 8000 | -0.01200  t=-6.35  8/8 seeds  0.77x compute |
| d6 enabled vs leaf@default, 4 seeds x 2000        | -0.00550  t=-4.37  4/4 seeds  0.88x compute |
| **fresh-seed confirmation**, 8 FRESH seeds x 16000 games/arm | **-0.01425  se 0.00154  t=-9.26  8/8 seeds** |

The fresh-seed run (seeds 21001..28008, disjoint from phase E's 8008/9009/10010/11011 AND from
every suite seed, at the SHIPPED play point) exists because the skill's Knights lesson says an
8-seed read can hide a one-sided cost. It came out slightly STRONGER than the gate, same sign on
every seed. Raw: `logs/stompy_bucket_v2/freshseed.txt` (gitignored — the numbers above are the record).

GT rebaselined smoke + regression, every non-stompy key byte-identical. Shape is **d5 better /
d3 marginally worse**, which is what a leaf replacing the horizon rollout should do. All 8
searched-slower games categorised: 6 budget churn; gi112 reaches T5 at 4x/16x (better than its
old T6, so budget binds, not the leaf); gi195 diffed sidecar-on vs -off from an IDENTICAL opening
hand showed the **Worldly Tutor target moved** (Craterhoof -> Priest of Titania) — a decision
change making them different physical games, not a bug.

**Caveat for anyone reading the suite as the verdict:** every harness job carries
`ignore_play_profile: true`, so the suite bypasses the depth lock and measures the leaf WITHOUT
the d6 policy that makes it best. Phase E and the fresh-seed run are the honest reads on the
shipped config.

### Still open (deliberate, not forgotten)

1. **The four cast-order / payment levers are NOT adopted** — `MTG_STOMPY_ORDER`,
   `MTG_TOP_RESOLVE`, `MTG_TAP_SCALED_LAST`, `MTG_TAP_TRIM` (plus `MTG_SAC_SPARE_ATTACKERS`),
   all default-OFF. They measure train -0.158 / held-out -0.043 with zero cells worse, and the
   tap pair is additive rather than redundant with upstream's `MTG_DORK_ATK_SEARCH`. But every
   one of those numbers was taken with **no sidecar present**, i.e. a configuration we no longer
   ship. RE-MEASURE on top of the adopted leaf before deciding; adoption then needs the combo
   fixture (`/tmp/tutor_top_reset.json` shape — recreate it, it was session scratch) committed
   into `test/scenarios/` plus its own GT rebaseline.
2. **Phase F (mulligan-generation contract) is now unblocked** — it stood down on every run so
   far with "no StompySurprise.value.json -- the value leaf must exist first". Its marker is
   already `done` in the queue, so re-running needs that marker deleted.
3. **Stompy overnight GT rows are still NEW** — 12 cases are defined in `regression_cases.sh`
   but no overnight key has ever been generated.
4. The two modelling items in `stompy-top-of-library-consumers.md` (upkeep Call activation on an
   intentionally-stacked top; the late "tutor for Craterhoof" line the static rank cannot express)
   remain deferred, as does the Worldly-Tutor HOLD idea in the addendum above.

## New-card screening, 2026-09-15 (World War Hulk / 4 Natural Order / Apex Altisaur)

Full write-up: **`docs/design/saga-world-war-hulk.md`** (includes the first Saga implementation,
CR 714, and its two traps). Headline, 20,000 paired games per arm against the shipped R=40/K=15
table with the new cards aliased into the bucket they replace, base ≈ 4.54 avg win turn:

| list | delta (40,000 games, held-out confirmed) |
|---|---|
| `wwh4_no4` — +4 World War Hulk, Natural Order 2→4, −4 Call of the Wild, −Mirri's Guile, −Elderscale Wurm | **−0.2272** |
| `wwh4_no4_altisaur` — the same plus Apex Altisaur 1 / Worldspine Wurm 4→3 | **−0.1927** |
| Natural Order 2→4 alone (−Mirri's Guile, −Elderscale Wurm) | −0.1635 |

Findings that constrain future edits to this list:

* **Natural Order is the deck's best card** (+0.596 per-copy in the shipped profile, 2.4x the next)
  and wants to be a 4-of. 4 > 3 > 2 monotonically.
* **Hornet Queen is a keep.** Cutting her for Natural Order measured *worse* than cutting Mirri's
  Guile + Elderscale Wurm. Her Insect tokens are green — Natural Order fodder and Craterhoof bodies.
* **Craterhoof Behemoth is untouchable.** Trading it for a second Hornet Queen swings the list from
  −0.22 to +0.04.
* **Worldly Tutor must not pay for World War Hulk** — the worst spot tested (+0.038). The
  tutor→draw→free-cast line is real.
* **Apex Altisaur costs 0.0345 turns**, and that is a FLOOR: both its fight abilities are provably
  inert against a passive opponent that never controls a creature. User acknowledged, shipped
  deliberately as a vanilla 10/10.
* The apparatus bias floor is UNMEASURED on the alias route (`--floor` generates per arm, which the
  route forbids). Headline effects clear a typical floor by 20–45x; adjacent-arm gaps (~0.012) do not.

Not yet done for these lists: mulligan profile, value leaf, regression GT. The numbers are a
ranking, not an adopted deck's strength.

### Constrained re-screen, same evening (screens D–H)

The user set three deckbuilding constraints after seeing the above, and they change the answer:

1. **Call of the Wild ≥ 2** and **Elderscale Wurm stays** — "I don't think I want to drop more than
   2 Call of the Wild or Elderscale Wurm."
2. **Vaultborn Tyrant 2→1 only; cut a Worldspine Wurm instead.** The user accepts Apex Altisaur's
   measured cost here because "it doesn't [cost] so much in a real game, because the Altisaur's
   upside becomes apparent" — the creature removal this sim cannot model.
3. **THE DECK IS A TOOLBOX: every important piece stays at ≥ 1.** This is the governing rule, and it
   retires arms the measurement liked: `wwh3_no4_vault0` (−0.2219) and `wwh4_no4_vault0_wurm3`
   (−0.2258) both zero out Vaultborn Tyrant and are simply not legal lists, whatever they score.

**Recommended lists** (40,000 games each, held-out confirmed, base ≈ 4.54):

| list | delta | shrinkage t |
|---|---|---|
| `alt_wwh4_wurm1` — WWH **4**, NO 4, CotW 2, −Guile, Vaultborn 1, Worldspine **1**, **Altisaur 1** | **−0.1879** | −0.68 |
| `alt_wwh3_wurm2` — WWH 3, Worldspine 2, otherwise identical | **−0.1748** | −0.55 |
| `noalt_wwh3_wurm3` — WWH 3, Worldspine 3, **no Altisaur** | **−0.2139** | +0.86 |

`alt_wwh4_wurm2_tera1` (WWH 4, Worldspine 2, Terastodon 1) measured −0.1844, tied with
`alt_wwh4_wurm1` at 0.0015 — pick on deck-design grounds, not on these numbers.

**The slot ladder — what each card is worth as the marginal cut** (Screen D/F, holding Call of the
Wild at 2; the cheapest slot is the one to spend):

| cut | cost vs the best available cut |
|---|---|
| Mirri's Guile | free (always the first cut) |
| a redundant Worldspine Wurm | **cheapest** |
| the 2nd Vaultborn Tyrant | +0.001 |
| the 2nd Terastodon | +0.007 |
| Wirewood Lodge | +0.007 |
| a Forest (14→13) | +0.024 |
| a Turntimber Symbiosis (4→3) | +0.029 |
| an Arbor Elf (2→1) | +0.044 |
| **Sol Ring** | **+0.048 — do not cut** |

**World War Hulk is monotone in count across all five screens.** Every extra copy is worth
0.011–0.016 *provided a redundant copy pays for it*; the count is never limited by the Hulk, only by
what is left to spend. That is why the constrained best still wants 4. The user's prior ("I doubt we
want 4 World War Hulk") was tested directly and not supported — but note the mechanism, because it is
the toolbox rule applied to the one card that was not following it: **Worldspine Wurm is a 4-of in a
toolbox deck.** Its copies measure bad to *draw* (−0.275 per copy in the shipped profile) because it
exists to be fetched by Natural Order or freed by a Hulk chapter I, and one copy in the library serves
that. Trimming it toward 1 and spending the slots on Hulks is the toolbox principle, not a departure
from it.

**Apex Altisaur's price is stable at 0.037–0.048** depending on what pays: the 2nd Vaultborn Tyrant
+0.0374, a Worldspine Wurm +0.0432, a Terastodon +0.0466–0.0478; in the user's final shell it is
**0.0423**. Still a floor, for the reason above.

Other constrained findings: Natural Order 4 is non-negotiable (`alt_wwh3_no3` was the worst arm in
its screen, and `wwh3_no2` the worst in Screen D — the 3rd/4th Natural Order is worth ~5x an extra
Hulk); 2 Hulks beat 1 even when a Call of the Wild pays. Holding Call of the Wild at 2 and keeping
Elderscale Wurm costs about **0.017 turns** against the unconstrained `wwh4_no4` (−0.2276 in the
same apparatus); keeping the 2nd Vaultborn as well and adding the Altisaur brings the total to
about 0.040.

One apparatus note: the keep table buckets **Elderscale Wurm, Hornet Queen and Vaultborn Tyrant
together**, so "cut Elderscale" and "cut a Vaultborn" are *identical to the mulligan model* — the
0.0009 between them (Screen D) is pure play difference, and neither is favoured by the table.

### The marginal-cut ladder (Screen I) — THE DECK HAS NO FAT

The user pinned the threat suite ("2 wurms 2 terastodon + toolbox seems okay for creatures") and
asked the obvious follow-up: *"it's not clear whether we need as many other pieces"* — specifically,
could a 1-mana elf go? Screen I answers it exhaustively. Hold the list fixed, add a 4th World War
Hulk, and pay for it with **one** further card, one arm per candidate. Any arm that beats the
do-nothing baseline means that card is worth less than a 4th Hulk.

| cut | delta | cost vs keeping the list |
|---|---|---|
| Wirewood Lodge | −0.1701 | **+0.0019 — a tie** |
| a Forest (14→13) | −0.1620 | +0.0100 |
| a Worldly Tutor (4→3) | −0.1595 | +0.0126 |
| a Turntimber Symbiosis (4→3) | −0.1577 | +0.0144 |
| an Elvish Archdruid (4→3) | −0.1525 | +0.0196 |
| a Priest of Titania (4→3) | −0.1464 | +0.0257 |
| an Arbor Elf (2→1) | −0.1391 | +0.0330 |
| a Fyndhorn Elves (2→1) | −0.1376 | +0.0345 |
| an Elvish Mystic (4→3) | −0.1368 | +0.0352 |
| a Llanowar Elves (4→3) | −0.1331 | +0.0390 |
| Sol Ring | −0.1303 | +0.0418 |
| *baseline — cut nothing, stay at 3 Hulks* | *−0.1721* | — |

**Not one cut pays.** The support half is correctly sized, and the **1-mana elves are the second
most expensive cards in the deck to cut, behind only Sol Ring** — 0.033–0.039 each, about 3x what
the 4th Hulk gains. This is a ramp deck whose plan is an 11-mana Wurm on turn 4; the dorks are the
engine, not filler. **Wirewood Lodge is the only genuinely free slot** (0.0019) — tradeable for a
4th Hulk at no cost and no gain.

One trap this closes: the shipped profile's *second-copy opening-hand* scores are negative for
Priest of Titania (−0.171), Elvish Archdruid (−0.124) and Llanowar Elves (−0.054), which reads like
an invitation to trim them. It is not. Those measure awkwardness in an opening hand, not deck value;
cutting those cards still costs 0.020–0.039. **Do not use `card_scores` as a cut list.**

### Arbor Elf → Fyndhorn Elves

The user planned this swap independently ("I definitely plan to drop Arbor elf for Fyndhorn even if
it is kept"). Fyndhorn Elves was added to `cards.json` for it — a functional twin of Llanowar Elves
(`mana_dork`, `produces: ["G"]`, no bracket note; nothing about it is unmodelled) — and aliased into
Arbor Elf's keep bucket, which leaves that bucket at exactly 10 and so is apparatus-neutral.

Measured **−0.1742 vs −0.1721, a 0.0021 gain**. That is below any plausible bias floor, so the
honest reading is **"strictly not a downgrade, and free"**, not a measurable upgrade — which is what
the model predicts, since Arbor Elf's Forest condition (`mana_requires_land_subtype`) is live off 14
basic Forests nearly always. Turntimber's back face is a plain Land, not a Forest, so Arbor Elf has
14 live targets, not 18. The ladder corroborates the direction from the other side: cutting a
Fyndhorn costs *more* than cutting an Arbor Elf (0.0345 vs 0.0330).

### THE RECOMMENDED LIST

Confirmed **−0.1748 over 40,000 games** (held-out shrinkage t = −0.21), and reproduced in four
independent 20,000-game blocks within 0.004 of each other (−0.1733 / −0.1721 / −0.1764 / −0.1754).

```
4  Natural Order            (2 -> 4)      4  Worldly Tutor
3  World War Hulk           (new)         4  Turntimber Symbiosis
1  Apex Altisaur            (new)         1  Sol Ring
2  Call of the Wild         (4 -> 2)      1  Wirewood Lodge
2  Worldspine Wurm          (4 -> 2)      4  Llanowar Elves
2  Terastodon                             4  Elvish Mystic
1  Vaultborn Tyrant         (2 -> 1)      2  Fyndhorn Elves   (was 2 Arbor Elf)
1  Hornet Queen                           4  Priest of Titania
1  Craterhoof Behemoth                    4  Elvish Archdruid
1  Elderscale Wurm                       14  Forest
                                          0  Mirri's Guile    (1 -> 0)
```

Free variations, all inside the measured noise: **Wirewood Lodge → a 4th World War Hulk** (0.0019),
and **Worldspine Wurm 2 / Terastodon 2 → Worldspine 3 / Terastodon 1** (0.0027). Dropping the Apex
Altisaur and restoring the 3rd Worldspine Wurm gives `noalt_wwh3_wurm3` at **−0.2139** — 0.039
faster, which is the Altisaur's price and is bought deliberately for removal this sim cannot model.

Specs and raw output: `logs/stompy_screen/screen{D,E,F,G,H,I}.json` + `.out`,
`confirmH{1,2,3}.out`, `confirmI1.out` (gitignored).

### The Call of the Wild count, re-verified with the card fully modelled (2026-09-15)

Every screen above ran with the upkeep Call of the Wild window MISSING (`MTG_UPKEEP_CALL` defaults
OFF), which under-plays the card and therefore flatters the arms that cut it — the `[bracket note]`
question. That was re-run as a dedicated one-axis ladder (Call of the Wild ↔ World War Hulk over 5
combined slots on this shell, 40,000 paired games per arm on held-out seeds, the whole ladder run at
both lever states): `logs/stompy_screen/callrecheck.json`, `callrecheck_{off,on}.out`.

| Call / Hulk | lever OFF | lever ON |
|---|---|---|
| 1 / 4 | 4.3493 | 4.3491 |
| **2 / 3 — this list** | **4.3637** | **4.3633** |
| 3 / 2 | 4.3810 | 4.3804 |
| 4 / 1 | 4.3972 | 4.3963 |

The modelling half of this holds and is the part worth keeping: fully modelling the card moves the
4 → 2 decision by 0.0005 of the 0.0334 turns it is worth — **1.5%** — and changes neither the
ordering nor, materially, the slope. Full derivation in `stompy-top-of-library-consumers.md`.

**But the AXIS was wrong, and the USER caught it** (2026-09-16: *"Call is not really comparing
against Hulk."*). Pinning Call + Hulk = 5 forces every cut Call to be spent on a Hulk, so the 0.016
slope is mostly a statement about **Hulk's** marginal value, not Call's — and Hulk's count is set by
its own margin (the 4th Hulk is worth only 0.0019 against the Lodge). The live question is Call
against *whatever the slot would otherwise hold*. See the next section, which is the answer.

### What the Call of the Wild slot actually competes with (2026-09-16)

`logs/stompy_screen/callslot.json` prices the 2nd Call of the Wild in the currency the rest of the
deck was priced in — the marginal-cut ladder, where **every arm buys the same 4th World War Hulk
with exactly one card**, so the rungs are directly comparable. This is the rung screen I never had:
the toolbox rule excluded it, making Call of the Wild the one card never priced this way.

| what pays for the 4th Hulk | cost vs the recommended list |
|---|---|
| **a Call of the Wild (2nd copy)** | **−0.0160 — cutting it GAINS** |
| Wirewood Lodge | +0.0015 (reproduces screen I's 0.0019) |
| a Forest (14th) | +0.0092 |
| a Worldly Tutor (4th) | +0.0134 |
| Sol Ring | +0.0425 |

**Family B settles the attribution.** On the 4-Hulk list, cutting the 2nd Call gains *whatever*
replaces it: +0.0216 for a 15th Forest, +0.0052 for a 3rd Worldspine Wurm, **+0.0479 for a 3rd
Fyndhorn Elves**. So this is not Hulk being good — the 2nd Call of the Wild is simply the worst card
in the deck, and the Call↔Hulk framing hid that behind Hulk's own margin.

The obvious objection does not rescue it: the upkeep modelling gap is worth 0.00043 at Call 2 against
0.00018 at Call 1, a 0.00025 difference against effects of 0.016–0.048. Under 2%.

**This does not overturn the toolbox rule — and as it turns out, it does not have to.** See below:
once the deck takes the Fyndhorn upgrade the slot is paid for elsewhere and holding Call at 2 costs
nothing measurable.

### THE DECK DID HAVE FAT — it was a missing card, not a cuttable one (2026-09-16)

Chasing Family B's best filler turned up something an order of magnitude larger than the question
that started it. `logs/stompy_screen/fynd3.json` + `fynd4_h2h.json`, 40,000 paired games/arm:

| arm | vs the recommended list | honours Call ≥ 2? |
|---|---|---|
| **`C_fynd4` — Wirewood Lodge + a Forest → 4 Fyndhorn Elves** | **−0.0448 / −0.0507** | **yes** |
| `C_call_fynd` — cut the 2nd Call → 3rd Fyndhorn | −0.0454 | no |
| `B_fynd` — Call 1, Hulk 4, no Lodge, Fyndhorn 3 | −0.0464 / −0.0432 | no |
| `C_hulk_fynd` — 3rd Hulk → 3rd Fyndhorn | −0.0288 | yes |
| `C_lodge_fynd` — Lodge → 3rd Fyndhorn | −0.0283 / −0.0306 | yes |

Reproduced across three independent seed blocks (4000000 / 5000000 / 6000000); `C_fynd4`'s own
held-out confirm came back **better** than the screen (−0.2314 vs −0.2147 against the shipped base),
so the effect is not selection-inflated.

**And the toolbox rule comes out free.** The best constraint-LEGAL list (`C_fynd4`, Call at 2) ties
the best constraint-BREAKING list (`C_call_fynd`, Call at 1) at 0.0006 ± 0.0023 — a dead heat. Those
two lists differ in more than the Call count, so this is a claim about the two lists and not about an
isolated axis; but within everything measured, keeping the 2nd Call of the Wild costs nothing once
the slot is paid for out of the Lodge and a Forest instead.

**THE METHODOLOGICAL MISS, which is the real lesson.** Screen I's "THE DECK HAS NO FAT" was a
conclusion about **cutting**: every arm removed a card, and every removal cost something. A cut
ladder answers "what is cheapest to remove" and never asks "what is best to ADD" — they are
different questions, and the deck's answer differed by 0.05 turns. The signal was sitting in screen I
the whole time: `cut_fyndhorn` cost 0.0345, one of the most expensive cuts on the board, which is
exactly what an *underweighted* card looks like. Nobody tested adding one. **When a cut ladder says a
card is expensive to cut, test adding another copy before concluding the list is tight.**

### THE REVISED LIST (supersedes the one above)

```
4  Natural Order            4  Worldly Tutor           4  Llanowar Elves
3  World War Hulk           4  Turntimber Symbiosis    4  Elvish Mystic
1  Apex Altisaur            1  Sol Ring                4  Fyndhorn Elves   (2 -> 4)
2  Call of the Wild         0  Wirewood Lodge (1 -> 0) 4  Priest of Titania
2  Worldspine Wurm                                     4  Elvish Archdruid
2  Terastodon                                         13  Forest           (14 -> 13)
1  Vaultborn Tyrant
1  Hornet Queen             0  Mirri's Guile
1  Craterhoof Behemoth
1  Elderscale Wurm
```

Cutting the 14th Forest is cheap here specifically *because* Arbor Elf is already gone: Arbor needed
a Forest to be live (`mana_requires_land_subtype`), Fyndhorn does not. Twelve one-mana dorks now back
the same land count the two-Fyndhorn list ran on 14.

## 1v1 AND 2HG: the final-list screen (2026-09-16)

USER asked for significant testing of both formats to settle the list. `logs/stompy_screen/final_both.json`
— 12 arms x 50,000 paired games x 2 formats, **one pooled batch** (1.3M games, 32/32 workers
throughout), `max_turns` 10.

### First, two things that had to be established before any 2HG number could be read

**`opponent_heads` is INERT for this deck — proven, not assumed.** 30 life at heads=1 vs heads=2 gives
the **identical digest `9d17fc14ab81909b`** over 3,000 games. This 60 has no "each opponent" effect and
no multi-target damage spell; 2HG's two faces share one life pool, which is what `players[1]` already
models. **So "2HG" here means exactly one thing: a goldfish at 30 life.** That is worth stating plainly
because it makes the 2HG column readable as a life-total sensitivity test rather than a new format.

**The horizon is not censoring.** A probe put only 0.03% of 2HG games past turn 8 (1v1: zero), so
`max_turns` 10 leaves the mean uncensored in both formats.

### The result: the two formats want the SAME list

| arm | 1v1 | 2HG | 2HG − 1v1 |
|---|---|---|---|
| **`F4_arbor2` — Hulk 1, Arbor Elf 2** | **−0.2538** | **−0.3708** | −0.1170 |
| `F4_call1` — Call of the Wild 1, Hulk 4 | −0.2381 | −0.3573 | −0.1193 |
| `F4_arbor1` — Hulk 2, Arbor Elf 1 | −0.2399 | −0.3546 | −0.1147 |
| `F4_forest14` — Hulk 2, Forest 14 | −0.2262 | −0.3395 | −0.1133 |
| `F4` — the 4-Fyndhorn list | −0.2234 | −0.3368 | −0.1135 |
| `F4_hulk4` — Hulk 4, Fyndhorn 3 | −0.1973 | −0.3099 | −0.1125 |
| `REC` — the previous recommendation | −0.1768 | −0.2871 | −0.1103 |

**No sign flips in the difference-of-differences column** — every arm is worth more at 30 life, and the
ranking is the same top three in both formats. One list serves both, which is the answer to the
question that was asked.

### WORLD WAR HULK LOSES TO ELVES, ALL THE WAY DOWN

The Saga this whole analysis was built around ends at **one copy**:

| step | 1v1 | 2HG |
|---|---|---|
| Hulk 4 -> 3 | +0.0260 gain | +0.0270 |
| Hulk 3 -> 2, +1 Arbor Elf | +0.0165 | +0.0177 |
| Hulk 2 -> 1, +2 Arbor Elf | +0.0139 | +0.0162 |

Arbor Elf — a strictly *worse* Llanowar Elves (it needs a Forest) that this deck had already cut —
beats a second and third World War Hulk. **Read that as the warning it is**, see the caveat below.

### The USER's heuristic note, answered

USER: *"our heuristic for searching critters should take the new life total into account."* It does:
`StompyProvider::TutorCandidates` reads `opp_life = s.players[1-controller].life` and tests
`swing >= opp_life`; the "robust kill" collapse gate at DecisionProviders.cpp:13117 is the same. There
is no hardcoded 20 anywhere in that path. What *was* worth testing is whether the NO-DOCTRINE ORDERING
(Craterhoof > Worldspine > Terastodon > Hornet Queen > Vaultborn), adopted at 20 life, is stale at 30.
It is not — it is worth **4.6x MORE** there:

| | doctrine ON | OFF | worth |
|---|---|---|---|
| 1v1 | 4.3185 | 4.3273 | 0.0088 |
| 2HG | 4.6570 | 4.6973 | **0.0403** |

At 30 life the kill needs the biggest/fastest body more, so fetching blind costs more. No
recalibration needed. (One genuine 2HG side-effect: the collapse gate fires less often at 30 life, so
the tutor contender list stays wider and the search branches more for the same 20 ms budget — 2HG
games cost ~2.4x 1v1. Not wrong, but it means 2HG play is effectively shallower per candidate.)

### THE CAVEAT THAT MATTERS MORE THAN ANY NUMBER ABOVE

Every screen in this section says the same thing: **add mana dorks, cut payoffs.** That is the one
axis a goldfish is structurally guaranteed to overvalue — no removal, no sweepers, no flood
punishment, and the game ends before a surplus dork becomes a dead draw. The win-turn histogram
confirms the mechanism is pure acceleration: the 4-Fyndhorn list converts turn-5 kills into turn-4
kills (63.95% -> 67.94% at turn 4) and does nothing else.

Two things argue it is not *only* an artifact: the advantage GROWS at 30 life (0.0770 -> 0.0837 vs
REC), where longer games should punish a dork-flood if the model could see it at all; and the
improvement is uniform across every win-turn bucket, so it is not buying speed with tail risk. And one
thing argues it is: the ladder's gains are decelerating but have not turned over, and the direction
keeps recommending a strictly worse mana elf over the deck's marquee card.

**This is the Apex Altisaur decision again, and it is the USER's.** There it was a measured cost
accepted for unmodelled upside; here it is a measured gain sitting on unmodelled downside. The
engine cannot price a sweeper, so it cannot price the 22nd mana source.

## The in-between list, and three corrections the USER forced (2026-09-16)

USER, on the all-elf direction: *"I'm a bit concerned about completely going all-in on the elves.
The reason would be something we can't model which is sweepers. Even worse are cards like pyroclasm."*
Then: *"We may have to find an in-between option to avoid any slowness, but also avoid high exposure
to sweepers."* This section is that search, plus three places the user caught me being wrong.

### CORRECTION 1 — "more dorks, fewer payoffs" was FALSE

USER: *"Hulk is also not really a payoff. It is used to cheat things out, so it is somewhat fair to
compare against more acceleration."* Correct. The payoff count is IDENTICAL across every list in this
analysis — Worldspine 2, Terastodon 2, Craterhoof 1, Hornet Queen 1, Vaultborn 1, Elderscale 1,
Altisaur 1 = **nine fatties in all of them**. The deck was never trading payoffs for mana; it was
trading ONE acceleration engine (World War Hulk) for ANOTHER (mana elves). The only arm that cuts a
real payoff is `X_arbor4` (Worldspine 2 -> 1), and it sits at the end of the ladder.

### CORRECTION 2 — the elf-scaling mechanism is NOT demonstrated

I hypothesised that an extra Elf beats an extra land because Priest of Titania ({G} per Elf on the
battlefield) and Elvish Archdruid ({G} per Elf you control) make elves scale quadratically. The
difference-in-differences test (`logs/stompy_screen/mechanism.json`) **refutes the test, not the
idea**: the elf-over-land premium is 0.0125 WITH the scalers and 0.0523 WITHOUT them — 4x larger in
the shell that has no scaling at all. The test is confounded: removing Priest/Archdruid also removes
8 mana sources, so shell B is mana-starved (4.78 vs 4.30) and any mana source is worth more there.

What survives is the USER's own explanation, which is the larger half anyway: *"Lands are not
acceleration and they don't scale our mana more than 1, whereas an elf can do so with titania or
archdruid."* A land is just your land drop; a dork is a source ON TOP of the drop. In the
scaler-rich deck that premium is only 0.0125 because the deck is already mana-saturated. **The Elf
synergy may well be real and is NOT measured here** — the clean test needs a non-Elf one-mana dork,
which this deck does not contain.

### CORRECTION 3 — the Wirewood Lodge analysis, twice wrong

USER asked: *"Is the lodge cut because it can't play a T1 dork?"* The card data supports the premise —
Wirewood Lodge `produces: ["C"]` while every one-drop in the deck costs {G} (Forest and Turntimber's
back face are the only {G} sources). But the measurements corrected the story twice:

* I claimed the Lodge's untap "never fires", from 0 `UntapCreature` actions in 800 logged games.
  **That was a LOGGING GAP, not a play fact** — `AIEngine.cpp`'s `UntapCreature` branch pays and
  untaps but never calls `m_logger`, unlike every sibling branch around it. USER: *"Why does it never
  fire? That's quite suspect."* A digest probe settled it: `MTG_UNTAP_BURST` on vs off gives
  DIFFERENT digests (`103fd87ba7b57197` vs `0aaa5e6545711a7c`) and 4.3604 vs 4.3636. **The ability
  fires and is worth 0.0032.** (USER also corrected the mechanics: the Lodge does not need a Forest
  to function — *"The dorks can feed the lodge"* — only to cast the dorks in the first place.)
* The "can't play a T1 dork" case is real but rare POST-mulligan (0.4% of kept openers). USER saw
  why: *"There are, in fact, hands where you need Wirewood Lodge to tap for green, but we would
  consistently mulligan them."* Measured on the PRE-mulligan seven: **1.0% of opening hands have the
  Lodge as their only land, and 25 of 25 are mulliganed — 100%, against a 48.4% baseline.** The
  Lodge's cost is paid as mulligans, not as missed turns, which is why looking for it in turn-1 plays
  found nothing. ~1% of hands auto-mulliganing at ~0.4 turns each is ~0.004, about two thirds of the
  cost; the rest is colorless mana mid-game.

**And the comparison itself was mis-paired.** USER: *"do we count those hands as having a forest
properly in the comparison?"* NO — in `F4_lodge` the Lodge is card #46 and `F4` holds NATURAL ORDER at
#46, so the arms were never matched at that slot. Redone with `replace {"Wirewood Lodge": "Forest"}`
so the 15th Forest INHERITS the Lodge's number and the same shuffle deals a Forest exactly where the
other arm deals the Lodge (`logs/stompy_screen/lodge_swap.json`, assertion verified in the output):

| arm, matched hands | avg |
|---|---|
| Wirewood Lodge + 14 Forest | 4.3680 |
| 15 Forest, in the Lodge's slot | 4.3618 |
| **cost of the Lodge** | **0.0062 +- 0.0009 (t = -7.22)** |

The mis-paired version said 0.0101 — **it overstated the cost by ~60%.** Use 0.0062.

### Sweeper exposure, and the list that answers it

`scripts/board_exposure.py` (built for this exact question in 2026-08) over the ladder:

| list | faster than REC | peak board exposed | "nowhere" kills | Hulk | payoffs |
|---|---|---|---|---|---|
| REC | — | 4.13 | 2.7% | 3 | 9 |
| F4 | 0.0466 | 4.26 | 0.8% | 3 | 9 |
| F4_arbor1 | 0.0631 | 4.36 | 0.9% | 2 | 9 |
| F4_arbor2 | 0.0770 | 4.43 | 1.3% | 1 | 9 |
| X_arbor4 | 0.1152 | **4.66** | **0.7%** | **0** | **8** |

Peak board grows and out-of-nowhere kills fall — both bad against a sweeper. (`nowhere_kill` at 1,500
games is 10-40 events; read it as directional.) **The engine cannot see that World War Hulk is
SWEEPER-PROOF acceleration**: an enchantment Pyroclasm does not touch, that rebuilds by cheating a
fatty in from hand precisely when the board has been wiped. Hulk and the elves are anti-correlated
along exactly the axis the model is blind to, which is why the measurement keeps saying to cut it.

Then `logs/stompy_screen/greensources.json` answered the last objection (USER: *"I can see the
argument for more forests when we are this low on them"*) — F4 reached 4 Fyndhorn by cutting the Lodge
AND a Forest, leaving 17 green sources. Buying the Forest back out of a SPELL is nearly free:

| list | 1v1 | vs G13 | 2HG | vs G13 | green sources |
|---|---|---|---|---|---|
| G13 (= F4) | 4.3153 | — | 4.6485 | — | 17 |
| **G14_tutor3 (Forest 14, Worldly Tutor 3)** | 4.3195 | +0.0042 | **4.6486** | **+0.0001** | **18** |
| G14_sym3 (Forest 14, Symbiosis 3) | 4.3198 | +0.0045 | 4.6553 | +0.0068 | 17 |
| G15_tut3_sym3 (Forest 15) | 4.3228 | +0.0075 | 4.6609 | +0.0124 | 18 |

**In 2HG it is a dead heat (0.0001).** So the 14th Forest is free and 15 starts to cost.

### THE IN-BETWEEN LIST (`G14_tutor3`)

```
4  Natural Order          3  Worldly Tutor          4  Llanowar Elves
3  World War Hulk         4  Turntimber Symbiosis   4  Elvish Mystic
1  Apex Altisaur          1  Sol Ring               4  Fyndhorn Elves
2  Call of the Wild                                 4  Priest of Titania
2  Worldspine Wurm                                  4  Elvish Archdruid
2  Terastodon                                      14  Forest
1  Vaultborn Tyrant
1  Hornet Queen           0  Mirri's Guile
1  Craterhoof Behemoth    0  Wirewood Lodge
1  Elderscale Wurm        0  Arbor Elf
```

0.0445 faster than REC in 1v1 and 0.0501 in 2HG — ~95% of the available gain — while keeping
**18 green sources, 9 payoffs, World War Hulk at 3 and Call of the Wild at 2**. Every list below it
on the exposure table buys speed by selling the deck's only sweeper-proof engine.

NOT ADOPTED: no mulligan profile or value leaf of its own. Both must be regenerated against a
12-dork deck before this margin is trusted at face value.

## The settle screen: every slot re-priced on the final shell (2026-09-16)

Most of this deck's counts were decided on the **old** shell — 2 Fyndhorn Elves, Wirewood Lodge in,
13 Forests — and never re-tested after the shell changed. `logs/stompy_screen/settle.json` re-screens
all of them against `G14_tutor3`: 13 arms, each ONE change paid by a named partner, **both formats**,
30,000 paired games, seed 16000000, `max_turns` 10. Pairwise se ≈ 0.0023 (1v1) / 0.0025 (2HG).

| arm (change vs `G14_tutor3`) | 1v1 vs FINAL | 2HG vs FINAL | legal? |
|---|---|---|---|
| `alt0_wurm3` — Apex Altisaur 1→**0**, Worldspine 2→3 | **−0.0349** | **−0.0060** | **NO** — zeroes Altisaur |
| `call1_hulk4` — Call 2→**1**, Hulk 3→4 | **−0.0148** | **−0.0183** | **NO** — Call < 2 |
| `hulk2_tutor4` — Hulk 3→2, Worldly Tutor 3→4 | **−0.0098** | −0.0040 | **yes** |
| `wurm1_tera3` — Worldspine 2→1, Terastodon 2→3 | −0.0062 | +0.0035 | yes (sign flip) |
| `hornet0_hulk4` — Hornet Queen 1→**0**, Hulk 3→4 | −0.0057 | +0.0330 | **NO** — zeroes Hornet |
| `sym3_f15` — Symbiosis 4→3, Forest 14→15 | +0.0036 | +0.0026 | yes |
| `wurm3_tera1` — Worldspine 2→3, Terastodon 2→1 | +0.0081 | +0.0017 | yes |
| `hulk4_tutor2` — Hulk 3→4, Worldly Tutor 3→2 | +0.0167 | +0.0137 | yes |
| `vault2_hulk2` — Vaultborn 1→2, Hulk 3→2 | +0.0167 | +0.0187 | yes |
| `call3_hulk2` — Call 2→3, Hulk 3→2 | +0.0168 | +0.0224 | yes |
| `solring0_f15` — Sol Ring 1→**0**, Forest 14→15 | +0.0315 | +0.0383 | **NO** — zeroes Sol Ring |
| `no3_hulk4` — Natural Order 4→**3**, Hulk 3→4 | +0.0436 | +0.0737 | **NO** — NO 4 is fixed |

### The headline is a legality result, not a speed result

**The top arm in each format is an illegal list.** `alt0_wurm3` wins 1v1 and `call1_hulk4` wins 2HG,
and both are retired by constraints the user set *before* these numbers existed — the toolbox rule
(every important piece stays at ≥ 1), the explicit Apex Altisaur ruling, and Call of the Wild ≥ 2.
Ranking the table without that filter would have proposed a list the user had already rejected.

**Exactly one legal arm beat `G14_tutor3` in both formats: `hulk2_tutor4`** (Hulk 3→2 for a 4th
Worldly Tutor) — and its 2HG margin is **t ≈ −1.6, not significant**. On this evidence the current
list is either right or within noise of right.

### What the screen settled, by not moving

Eight counts were re-confirmed on the new shell, several of them decisively:

* **Natural Order 4 is the most load-bearing count in the deck.** `no3_hulk4` is the worst arm in
  both formats by a wide margin (+0.0436 / +0.0737) — cutting the 4th costs 2–4x what any other
  single change is worth.
* **Sol Ring stays** (+0.0315 / +0.0383 to cut it for a Forest), **Turntimber Symbiosis stays at 4**,
  **Vaultborn Tyrant stays at 1**, **Terastodon stays at 2**, and **Call of the Wild 3 is worse than
  2** — so the user's ≥ 2 constraint costs nothing on the upside, only on the downside.
* **Forest 14 was NOT confirmed.** Both arms that tried 15 Forests paid for it with a card that is
  independently good (Sol Ring, Symbiosis), so they cannot separate "15 Forests is wrong" from
  "cutting Sol Ring is wrong." That gap is what `legal.json` exists to close.

### The Altisaur price, isolated — and it is a 1v1 problem only

Two arms end at **Worldspine Wurm 3** and differ only in which slot paid for it:

| paid by | 1v1 | 2HG |
|---|---|---|
| `alt0_wurm3` — the Apex Altisaur | −0.0349 | −0.0060 |
| `wurm3_tera1` — a Terastodon | +0.0081 | +0.0017 |
| **gap = Altisaur vs a 2nd Terastodon** | **0.0430** | **0.0077** |

So the 3rd Worldspine Wurm is *not* good — it measures **worse** when a Terastodon pays for it. The
whole `alt0_wurm3` result is the Altisaur being cut, nothing else. That is the cleanest price yet put
on the toolbox slot, and it carries a genuine update for the user's ruling: **at 30 life the Altisaur
costs 0.0077, roughly a sixth of its 1v1 price.** The ruling was made for an unmodellable reason
(the creature removal a passive goldfish cannot present), and in the format with the longer clock the
measurable half of the trade is nearly free. Nothing here argues against keeping it.

**Hornet Queen flips sign the other way:** cutting it is mildly *good* in 1v1 (−0.0057) and clearly
*bad* at 30 life (+0.0330). Longer games reward five bodies. It is protected by the toolbox rule in
any case, but the 2HG number means the rule is not costing anything here either.

## The legal-space screen: the 4th Worldly Tutor, and one fewer Worldspine Wurm (2026-09-16)

`logs/stompy_screen/legal.json` — 10 arms, **every one constraint-legal**, both formats, **100,000**
paired games (pairwise se ≈ 0.0014), seed 21000000, disjoint from settle's block. It exists because
settle left two things genuinely unresolved and one thing untested.

Three Craterhoof-2 arms were **cut on preflight**: a 1-of going to 2 raises composition fall-through
to 1.57%, and the screen drops the keep table from *every* arm above 1% — symmetric, but ~22x per
game and not comparable with settle. Every arm below sits at settle's own 0.39%.

### `hulk2_tutor4` replicated on held-out seeds

| | settle (seed 16M) | legal (seed 21M) |
|---|---|---|
| 1v1 | −0.0098 | **−0.0097** |
| 2HG | −0.0040 (t ≈ −1.6, n.s.) | **−0.0063** (t ≈ −4.5) |

The 1v1 figure reproduced to within 0.0001 on disjoint seeds. The 2HG effect was real and simply
under-powered at 30,000 games.

### It is the 4th Worldly Tutor that is good, not the 3rd Hulk that is bad

Settle could not tell those apart, because the only arm that reached Worldly Tutor 4 paid for it with
a Hulk. Four independent payers settle it — **all four are negative in both formats**:

| the 4th Worldly Tutor, paid by | 1v1 | 2HG |
|---|---|---|
| Worldspine Wurm 2→1 (`tutor4_wurm1`) | **−0.0279** | −0.0256 |
| Terastodon 2→1 (`tutor4_tera1`) | −0.0198 | **−0.0262** |
| World War Hulk 3→2 (`hulk2_tutor4`) | −0.0097 | −0.0063 |
| Forest 14→13 (`tutor4_f13`) | −0.0061 | −0.0037 |

The Tutor is worth having whatever pays for it. The *spread* across payers (0.0061 → 0.0279) is the
price of the slot spent, not of the Tutor — and it says the cheapest thing in this deck is the
second Worldspine Wurm.

### Worldspine Wurm is monotone-down — and the 3rd copy is a trap

Sorting all ten arms by their Worldspine count separates them almost perfectly:

| Worldspine Wurm | arms | 1v1 range vs FINAL |
|---|---|---|
| **1** | `tutor4_wurm1`, `f15_wurm1`, `combo_tutor_f15` | −0.0191 … **−0.0304** (all good) |
| 2 | `FINAL`, `hulk2_tutor4`, `tutor4_f13`, `tutor4_tera1` | 0 … −0.0198 |
| **3** | `f13_wurm3`, `hulk2_wurm3`, `combo_tutor_f13` | **+0.0123 … +0.0204** (all bad) |

This closes the loop on settle's `alt0_wurm3` result: that arm looked like "a 3rd Worldspine Wurm is
great" and it was nothing of the kind — the 3rd Wurm is *bad*, and the whole effect was the Apex
Altisaur being cut. Every screen that has ever tried a 3rd Worldspine Wurm has measured it as a loss.

### Forest 13 is wrong; 15 is fine but loses the slot to the Tutor

`f15_wurm1` and `tutor4_wurm1` both cut a Worldspine Wurm and differ only in what they buy with it:

| the freed slot buys | 1v1 | 2HG |
|---|---|---|
| a 4th Worldly Tutor | −0.0279 | −0.0256 |
| a 15th Forest | −0.0191 | −0.0190 |
| **Tutor over Forest** | **0.0088** | **0.0066** |

Forest 13 is clearly wrong (`tutor4_f13` is the weakest of the four Tutor arms, and `f13_wurm3` is
among the worst arms on the board). So the land count is right at 14–15 and the marginal slot goes
to the Tutor.

### The winner, and why it is not yet the answer

`combo_tutor_f15` (Hulk 2, Worldspine 1, Tutor 4, Forest 15) leads both formats at **−0.0304 / −0.0270**
— and it is a **statistical dead heat** with `tutor4_wurm1`: 0.0025 ± 0.0014 (t = −1.77) in 1v1 and
0.0008 ± 0.0017 (t = −0.46) in 2HG. That is nowhere near enough to adopt a selection-biased winner.

It matters which of the two wins, because **they differ on the sweeper axis**: `combo_tutor_f15`
cuts the 3rd World War Hulk, and the Hulk is this deck's only sweeper-proof acceleration — an
enchantment Pyroclasm ignores, that rebuilds from hand after a wipe. `tutor4_wurm1` keeps Hulk at 3
and is within noise of the same speed. A 1.77-sigma edge is not a reason to sell the one card that
covers the model's blind spot, so this goes to a held-out confirm on a third seed block
(`confirm.json`, seed 31000000) before anything is adopted.

## The held-out confirm: `stack_all`, and the sweeper trade-off dissolves (2026-09-16)

`logs/stompy_screen/confirm.json` — third disjoint seed block (31000000), 7 arms, both formats,
100,000 paired games. Six arms were re-measurements; one, `stack_all`, was new: it stacks *every*
direction the previous screens liked at once — **Worldspine Wurm 2→1, Terastodon 2→1, Worldly Tutor
3→4, Forest 14→15**, net zero, and critically **World War Hulk stays at 3**.

### Everything replicated

| arm | legal (seed 21M) 1v1 / 2HG | confirm (seed 31M) 1v1 / 2HG |
|---|---|---|
| `combo_tutor_f15` | −0.0304 / −0.0270 | **−0.0306 / −0.0269** |
| `tutor4_wurm1` | −0.0279 / −0.0256 | −0.0261 / −0.0249 |
| `tutor4_tera1` | −0.0198 / −0.0262 | −0.0192 / −0.0248 |
| `f15_wurm1` | −0.0191 / −0.0190 | −0.0184 / −0.0194 |
| `hulk2_tutor4` | −0.0097 / −0.0063 | −0.0098 / −0.0053 |

Three seed blocks, no shrinkage worth naming. The apparatus is measuring something stable.

### The new arm won, and won big

| vs `G14_tutor3` | 1v1 | 2HG |
|---|---|---|
| **`stack_all`** | **−0.0364** (t = −24.4) | **−0.0421** (t = −24.7) |
| `combo_tutor_f15` | −0.0306 | −0.0269 |
| `tutor4_wurm1` | −0.0261 | −0.0249 |

Head-to-head, `stack_all` beats `combo_tutor_f15` by 0.0058 (t = −4.17) in 1v1 and 0.0152
(t = −9.45) in 2HG — decisively, not by a hair.

**This dissolves the trade-off flagged in the previous section.** `combo_tutor_f15` bought its speed
partly by cutting the 3rd World War Hulk — the deck's only sweeper-proof acceleration. `stack_all` is
*faster than it in both formats while keeping the Hulk at 3*, because it pays with the second
Worldspine Wurm and the second Terastodon instead. There is no longer any reason to sell the card
that covers the model's blind spot: the measurement and the unmodellable consideration now point the
same way.

### Why it works: the deck was over-stocked on redundant fat

Every screen in this document has said the same thing from a different angle — the marginal Worldspine
Wurm is the cheapest card in the deck, the 3rd is actively bad, and the 4th Worldly Tutor is good
whoever pays for it. `stack_all` is just that sentence applied twice. The deck goes from **9 fat
payoffs to 7** while keeping all seven *distinct* (the toolbox rule holds: every piece is still at
≥ 1) and spends the two freed slots on a tutor and a land. It trades redundancy it could not use for
consistency it can — and it does so without touching the elf count, so board exposure to a sweeper is
unchanged.

**NOT YET ADOPTED.** `stack_all` was a new arm on the screen it won, so it is the selection-biased
winner and cannot be taken at face value. A fourth disjoint block (`confirm2.json`, seed 41000000) is
re-measuring it, alongside two arms asking whether the direction continues past it (Forest 16 paid by
a Hulk, and Forest 16 paid by a Turntimber Symbiosis).
