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
`chosen_x` → cast → `OnGoblinEnters(etb_kx)` in both worlds, but **no K > 0 cast variant was
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
