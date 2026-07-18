# Analysis ledger — Dragonstorm

Per-deck durable state for the analyze-deck workflow (survives compaction / handoff).
Deck: `decks/Dragonstorm/Dragonstorm.cod` (60 cards). Branch: `phase-1-2-deck-analyzer`.
Archetype provider routing: **GenericProvider** (no matching param signature today; correct
starting point — add an archetype provider only if 5g mining justifies it).

Status: **Stage 1 (coverage) + Stage 2a/2b (fetch + classify) DONE via 4 research subagents.**
**User decisions (2026-07-18):** (1) firebreathing — **IMPLEMENT NOW** (Scourge `{R}:+1/+0`
self + Lathliss `{1}{R}:` team pump; build the activated-ability framework this pass); (2)
deferrals — **NOT blanket-approved; user reviewing each explicitly** (see the itemized list
below); Apex cast-from-hand gate is IMPLEMENTED not deferred; (3) **build it all autonomously**
through Stages 2–5 to convergence once deferral list is resolved.

### Deferral decisions — RESOLVED (user, 2026-07-18)
1. **Rite of Flame** "+{R} for each Rite of Flame in each graveyard" — **IMPLEMENT.** Crucial for
   mana targets; copies chain (1 Mountain + 3 copies → +2/+3/+4 = **7 R** vs flat 4). Model: float =
   base 2 + (count of cards named "Rite of Flame" in ALL graveyards **at resolution**). Order-dependent
   within the turn → must be computed at resolve time, lockstep executor/planner/rollout. Now **Tier 3**.
2. **Desperate Ritual** Splice onto Arcane `{1}{R}` — **IMPLEMENT.** Real gain (splice keeps the copy
   in hand → its effect is used twice): cast #1 splicing #2 = pay 4, add **6**; #2 stays in hand, later
   hard-cast for +3 → net +3 vs +2, and NO storm-count loss (#2 still cast separately). Model:
   Desperate-Ritual cast takes a **search-chosen splice count** k (0..#other Desperate Rituals in hand);
   cost = (k+1)·`{1}{R}`, float = (k+1)·3 wild, spliced copies remain in hand. Viewer: splice-count choice.
   Now **Tier 3**.
3. **Karrthus** ETB untap-all-Dragons — **DEFER** (approved; doesn't matter until phase 2). Bracket-note it.

### Mana-COLOR fidelity (user, 2026-07-18) — wild over-credits color, use specific colors
- **Rituals** (Rite/Pyretic/Desperate/Seething) add **RED**, not wild. New param `ritual_float_color:"R"`
  makes `ApplyRitualFloat` add to `floating_mana.red`; absent → wild (Irencrag Feat/Reality Spasm keep
  wild → **Hinata byte-identical**). Rationale: wild `{R}{R}` could illegally pay the `{B}`/`{G}` pips of
  Karrthus/Kolaghan (which only arrive via Dragonstorm, never hard-cast).
- **Lotus Bloom** sac → **3 of ONE chosen color** (any single color; NOT wild — wild would pay a
  `{B}{R}{G}` mix). Color is a **real search decision** (e.g. black could enable a niche Karrthus hard-cast
  with Unclaimed Territory). `floating_mana.<chosen>+=3`. Viewer: color choice.
- **Apex of Power** "add ten mana of any one color" → same as Lotus: **10 of ONE chosen color**, NOT wild.

Disclosed-inert (no-op in goldfish, 6a disclosure, not decisions): flying/menace (no blockers);
Kolaghan same-name life-loss + Karrthus gain-control (passive opp never casts / has no Dragons).
**Dragonstorm "then shuffle" is KEPT** (user): the tutor-to-battlefield must trigger the deterministic
CRN reshuffle (`Library::ShuffleByKey`) like a fetch, so post-Dragonstorm draws come from a shuffled deck.

## Deck shape
20 lands (9 Mountain, 4 Sandstone Needle, 4 Mercadian Bazaar, 2 Unclaimed Territory, 1 Dwarven
Hold) + ritual/ramp shell (4 Rite of Flame, 4 Desperate Ritual, 4 Pyretic Ritual, 4 Seething
Song, 4 Lotus Bloom, 3 Ruby Medallion, 1 Irencrag Feat) → payoffs (4 Dragonstorm, 4 Apex of
Power) + 8 Dragons (Scourge ×3, Utvara ×2, Kolaghan ×1, Karrthus ×1, Lathliss ×1). It is a
dedicated mono-red (splash B/G on two dragons, cast via Dragonstorm not mana) ritual-storm deck:
ramp hard, cast a big Dragonstorm/Apex, drop a wave of Dragons that win via ETB ping + haste swings.

## Already implemented (coverage "full") — 4
Mountain, Sandstone Needle, Unclaimed Territory, Irencrag Feat. (Irencrag Feat = the
`ritual_floating_mana:7` + `max_casts_after:1` paste-template for the new rituals.)

## Per-card classification (15 missing)

### Tier 1 — cards.json only (reuse `ritual_floating_mana`, template `custom`)
Reserve mana floats as **wild** (`state.floating_mana.wild`, `SpellEffects.h:1737-1760`); wild pays
any single pip incl. `{R}` → faithful for these red rituals. Params are GROSS (full amount added; cost
paid separately, like Irencrag Feat=7).
| Card | cost | float | notes |
|---|---|---|---|
| Pyretic Ritual | `{1}{R}` (Instant) | `ritual_floating_mana:3` | clean |
| Seething Song | `{2}{R}` (Instant) | `ritual_floating_mana:5` | clean |

(Rite of Flame + Desperate Ritual moved to **Tier 3** — see "Deferral decisions RESOLVED": Rite gets
dynamic same-name-graveyard float scaling; Desperate gets search-chosen splice count.)

### Tier 2 — cards.json only (reuse `vanilla_creature` + `grants_haste`)
| Card | cost | P/T | params | inert clauses (disclose in 6a) |
|---|---|---|---|---|
| Dragonlord Kolaghan | `{4}{B}{R}` | 6/5 | Flying, Haste; `grants_haste`, `subtypes_affected:["Dragon"]`; subtypes `["Elder","Dragon"]` | flying (no blockers); "opponent loses 10 on same-name cast" (passive opp never casts); "other creatures haste" modelled Dragon-scoped (exact here) |
| Karrthus, Tyrant of Jund | `{4}{B}{R}{G}` | 7/7 | Flying, Haste; `grants_haste`, `subtypes_affected:["Dragon"]`; subtypes `["Dragon"]` | flying; ETB gain-control-all-Dragons (opp has none); ETB untap-all-Dragons (yours already untapped at main) |

### Tier 3 — new C++ engine mechanics (9 cards)
1. **Ruby Medallion** `{2}` — color-conditioned generic reduction. New param `reduces_spell_color:"R"`;
   in `EffectiveCost()` (`TurnSolver.cpp:576` + X-cost sites `906/1008` + `AIEngine.cpp:2987`), if the
   spell is red and controller has a Medallion, `cost.generic -= 1` (floor 0, stacks per copy). Keep an
   affinity-style prune-bail (`~1439`) so non-Medallion decks stay **byte-identical**. Cleanest Tier 3.
2. **Dwarven Hold** (land) — storage-counter battery, **upkeep-charged** (`storage_charge_mode:"upkeep_if_tapped"`).
3. **Mercadian Bazaar** (land) — same battery, **tap-charged** (`storage_charge_mode:"tap"`, a main-phase
   {T} action banking +1 counter, no mana that turn). Shared mechanic with Dwarven Hold, one param.
   Model: `Permanent.storage_counters` int; untapped storage land offers a **variable** single-tap R
   burst = counter count (zeroes on tap); **NOT** self-sacrificed (unlike depletion lands). Charge-vs-burst
   is a **search-decidable action**, not a baked heuristic (core-invariant: leave to search).
4. **Lotus Bloom** (artifact) — **Suspend 3—{0}** + `{T}, Sac: add 3 mana of ONE chosen color`.
   Model: suspend = a `{0}` action moving it to a suspended-timer list, arrives `turn+3` (reuse the
   `StagedCard{expiry_turn}` timer shape), enters untapped; then it's a one-shot sac that floats **3 of a
   search-chosen single color** (`floating_mana.<color>+=3`, NOT wild). Suspend timing + color = search-decidable.
   **STORM interaction (user):** when Lotus Bloom is CAST OFF SUSPEND (last time counter removed → free
   cast at upkeep), that IS a spell cast → it **increments the storm counter** (`spells_cast_this_turn`).
   The act of **suspending** (paying {0} to exile it) does NOT, and **sacrificing** it for mana does NOT.
   So an off-suspend arrival on the same turn as a Dragonstorm adds +1 storm. Model the arrival as a spell
   cast so the shared cast-site increment fires. Typical line: **suspend T1 → arrive T4**, and on T4 you get
   BOTH the sac-for-3 mana AND +1 storm from the off-suspend cast (the deck's usual T4 accelerant).
5. **Scourge of Valkas** `{2}{R}{R}{R}` 4/4 Flying — **ETB Dragon-count ping** (`dragon_ping_on_enter`):
   on this-or-any-Dragon ETB, deal (Dragons you control, counting the newcomer) to opponent face.
   **DEFER** `{R}:+1/+0` firebreathing.
6. **Lathliss, Dragon Queen** `{4}{R}{R}` 6/6 Flying — **ETB-other-nontoken-Dragon → 5/5 token**
   (`etb_other_subtype_creates_tokens`). Needs `Permanent.is_token` (nontoken gate → loop-safe).
   **DEFER** `{1}{R}: Dragons +1/+0` team firebreathing.
7. **Utvara Hellkite** `{6}{R}{R}` 6/6 Flying — **per-attacking-Dragon → 6/6 token**
   (`attack_per_matching_creates_tokens`), tokens **untapped/summoning-sick** (do NOT join this combat;
   attack next turn, or this turn under a haste-lord). Distinct from Adeline's tapped-attacking `attack_creates_tokens`.
8. **Dragonstorm** `{8}{R}` Sorcery, **Storm** — new `spells_cast_this_turn` counter (GameState; reset in
   `UntapStep:144`; incremented per cast in **lockstep** across executor/planner/rollout) + **repeatable
   tutor-to-battlefield** (`tutor_to_battlefield`, `tutor_types:["Dragon"]`): put `min(storm+1, dragons_left)`
   Dragons onto battlefield, each firing ETB (→ Scourge ping chains). Which-Dragons = **search enumeration**
   (reuse `TutorCandidates` multi-pick, `Unpruned::Tutor` gate). **"then shuffle" KEPT** → call
   `Library::ShuffleByKey` after the search (like a fetch). Heaviest lift.
9. **Apex of Power** `{7}{R}{R}{R}` Sorcery — **impulse-exile-7 this-turn** (reuse `m_is_staged`/`m_staged_expiry`,
   expiry = `turn_number` like Expressive Iteration; staged LANDS non-playable) + **conditional float of
   10 of ONE chosen color** (NOT wild; same colored-float as Lotus) gated on cast-from-hand (needs a
   cast-source flag on StackEntry).

**KILL PATTERN (user, 2026-07-18) — the engine must model this faithfully end-to-end:** the easiest
kill is **Lathliss + Scourge + a haste-Dragon (Kolaghan/Karrthus)** off a **storm 3+** Dragonstorm.
**Optimal put order = LATHLISS FIRST** (user), because Lathliss must already be in play when each *other*
nontoken Dragon enters so it spawns a token off each. Worked example — Dragonstorm puts
Lathliss→Scourge→Kolaghan, with the OPTIMAL trigger ordering (resolve Lathliss's token trigger BEFORE
Scourge's own ping, so the fresh token is in play when both pings resolve): Lathliss in (no Scourge, no
ping); Scourge in → make token T1 → T1 pings **3**, Scourge's own ping **3** (= 3+3); Kolaghan in → make
token T2 → T2 pings **5**, Scourge's ping for Kolaghan **5** (= 5+5) = **16 to the face** + **two** 5/5
tokens, and every Dragon+token has haste (Kolaghan/Karrthus grant_haste) → hasted alpha strike (~26 power)
→ lethal. (Scourge-first is worse: only ONE token + fewer pings, because Lathliss isn't in play when
Scourge enters.) **IMPLEMENTATION ORDERING:** `OnDragonEnters` must create the Lathliss token (and
recurse) BEFORE resolving the entered Dragon's Scourge ping, so every ping sees the maximal Dragon count
— the unambiguously-optimal line (more pings/tokens is never worse in goldfish), so token-first is a
faithful deterministic model, not a search choice. **This ordering/selection is a
reasonable DRAGONSTORM PROVIDER HEURISTIC** (Lathliss/token-makers first, then Scourge the pinger, then a
haste-Dragon, while they remain in the library) — encode it as the tutor-to-battlefield candidate ORDER
in a DragonstormProvider, but keep it search-explorable (unpruned / human-play must still see every legal
target+order), and validate via 5e. Requirements this imposes: (a) `OnDragonEnters` fires for **Lathliss
tokens** so they re-ping Scourge (call it from `CreateToken`); (b) **Lathliss triggers for EVERY
subsequent nontoken Dragon** (not just once) — put ORDER (Lathliss first) is a real search decision; (c)
the **win/lethal eval must count the Scourge ping (opponent life loss) PLUS the hasted
attackers**, or the search won't "see" the kill; (d) tokens attack THIS turn only under a haste-lord
(existing `HasHasteFromLords` on the token's Dragon subtype).

**DRAGONSTORM tutor-to-battlefield SELECTION HEURISTIC (user, 2026-07-18)** — the DragonstormProvider's
candidate ORDER for which Dragons to put in (and in what order), by what's left in the library. Encode as
a proposal; keep search-explorable (unpruned/human-play sees all legal target sets+orders); validate 5e.
- **Ideal (a haste-Dragon available):** **Lathliss first** → **Scourge second** → then **dump every
  remaining Dragon (all extras)** — additional Scourges, Utvara, and the haste-Dragon. Each extra
  entering while Lathliss+Scourge are out adds a Lathliss token + more Scourge pings, so more dumps =
  more damage; put extras RIGHT AFTER Scourge (we may dump several). The haste-Dragon must be IN the set
  (**Karrthus preferred over Kolaghan** — Karrthus by default, Kolaghan if Karrthus is gone) for the
  alpha strike, but its ENTRY order among the extras doesn't matter (haste applies at the attack step
  regardless of when it entered this turn).
- **Missing Lathliss:** **Scourge first, Utvara second, haste-Dragon third** (Karrthus preferred, else
  Kolaghan) — Utvara's combat tokens (one per attacking Dragon) enter and re-ping Scourge, amplifying
  Scourge damage in Lathliss's absence; the haste-Dragon still goes third to enable the same-turn alpha strike.
- **No haste-Dragon (toughest — no alpha strike this turn, so PING is the wincon):** take **Lathliss if
  available**, then **as many Scourges as possible**, then **Utvara** when Scourges run out. Ping damage
  (ETB, needs no haste) carries the kill.
- **General:** more Scourges is never bad with extra storm count (each adds a ping per Dragon entering);
  extra storm → grab more pingers/token-makers. (This is agent-step "Dragonstorm", NOT the kill-engine.)
- **ORDER RULE (user):** the provider must emit **ONE complete deterministic put-order for the whole
  fetched set** — **Lathliss first** (tokens off each later Dragon), **then all Scourges** (ping every
  later entry), **then every other Dragon in a fixed order** — so the search does NOT branch over
  orderings at all. This is legitimate because only Lathliss+Scourge are order-relevant (placed optimally
  by this reviewed ranking) and the remaining Dragons are **order-independent → equivalent outcomes**, so
  collapsing their permutations to one representative is LOSSLESS (core-invariant-safe: folds genuinely
  identical branches, doesn't pick among real alternatives). Keep `MTG_UNPRUNED`/human-play able to open
  every ordering so 5e can confirm the single order isn't leaving damage on the table. (The case bullets
  above are the SELECTION rules — which Dragons to grab when the library is short; this is the ORDER for a
  chosen set.)

**Shared engine hook (5,6,8):** one `OnDragonEnters(state, controller, &perm)` cascade called from
EVERY dragon-enter site — executor `EffectHandler::EnterBattlefield`/`ResolveVanillaCreature`
(`EffectHandler.cpp:11/165`), rollout `TurnSolver` creature-enter (~`3420`) + all `CreateToken` sites,
and eval (`TurnSolver.cpp:503`) — driving both Scourge's ping and Lathliss's token. Must stay lockstep
or the deck's kill-turn drifts. Interlock: a put-in Dragon pings Scourge + spawns a Lathliss 5/5 (token →
re-pings Scourge, loop-safe via nontoken gate); Utvara multiplies future attackers.

## Proposed deferrals — REQUIRE USER SIGN-OFF (skill 2a rule)
- Rite of Flame graveyard self-scaling (marginal dynamic count).
- Desperate Ritual **Splice onto Arcane** (no Arcane payoff in deck; net == hard-cast).
- Scourge `{R}:+1/+0` + Lathliss `{1}{R}:` team pump — **firebreathing**: needs a new activated-ability
  framework (repeatable, mana→combat-damage during the attack step). Real lethal amplifier but its own
  Tier-3 lift; both agents recommend shipping the ETB/token engines first and adding both pumps together.
- Karrthus ETB untap-all-Dragons (no live goldfish case; flag only).
- Apex cast-from-hand gate — recommend **implement** (not defer); note the exile-cast edge case.
- Inert goldfish collapses (standard, disclose in 6a): flying/menace = no-blockers; opponent-cast /
  gain-control-of-opponent triggers = passive opponent.

## Design decisions (recommended faithful model chosen unless user objects)
- Storm: full lockstep `spells_cast_this_turn` counter (correct kill-turn). Which-Dragons: search enumeration.
- Suspend: arrive `turn+3`, suspend timing search-decidable. Storage: charge/burst = search-decidable actions.
- Scourge X counts the just-entered Dragon (rules-correct); sequential ping on mass-enter.
- Apex: exile-7 wild-10 from hand; staged lands non-playable.
- Medallion: static −1 on red spells' generic (incl. red X-cost generic), prune-bail for byte-identity.

## Verification plan (Stage 5, after build)
`python3 scripts/verify_deck.py Dragonstorm` gate → nonconv/fd-diverge, multi-depth sanity (kill-turn
plausibility — this is a combo deck, expect fast wins on ramp-into-Dragonstorm turns), 5d 100-game
claude-play sweep, 5e/5g heuristic mining (order rules around ritual→payoff sequencing), 5h viewer
decision surface (Scourge ETB `target`, Dragonstorm `tutor_target` multi-pick, storage/Lotus mana choices).
Convergence loop back to Stage 2 on any flag.

## Build progress (Stage 2)
DONE + build-green + byte-identity-checked (hinata+slivers ALL PASS): Pyretic, Seething, Kolaghan,
Karrthus (cards.json), `ritual_float_color` (rituals→red), Ruby Medallion (`reduces_spell_color`), Rite
of Flame (gy self-scaling + triangular planner credit), **Dragon kill-engine** (Scourge ping /
Lathliss token / Utvara attack-tokens / firebreathing / `is_token` — shared `OnDragonEnters` cascade,
token-first ordering verified 3+3+5+5=16, lockstep executor `EffectHandler::EnterBattlefield`+`CreateToken`
/ rollout `ApplyPlanDirect`+`SimulateCombat` / eval = rollout), **Storage lands** (Dwarven Hold +
Mercadian Bazaar — `storage_land` + `storage_charge_mode`; see "Storage-land model" below),
**Lotus Bloom** (`suspend_time_counters` + `sac_for_mana_amount`; see "Lotus Bloom model" below).
REMAINING (3): Dragonstorm, Apex, Desperate Ritual (splice).

### Lotus Bloom model — DONE, build-green, byte-identity th+hinata+slivers ALL PASS (0 new)
Two new mechanics, both gated on new params so every other deck is byte-identical.
- **SUSPEND 3—{0}** (`suspend_time_counters:3`): a `{0}` main-phase action `Action::Kind::Suspend` moves
  the card hand→`Player::suspended_cards` (new list, folded into `BuildSimKey` only when non-empty) with
  `arrive_turn = turn+3`. At the controller's upkeep, `ProcessSuspendArrivals` (SpellEffects.h) casts off
  suspend any arrived card via **`CastOffSuspend`** — the SHARED free-cast site (executor
  `GameEngine::UpkeepStep` + rollout `SimulateEndAndStartNextTurn`, both after untap → arrives untapped).
  **STORM HOOK:** `CastOffSuspend` is the single documented home for Dragonstorm's future
  `++spells_cast_this_turn` — an off-suspend arrival flows through it, so it will auto-count +1 storm;
  suspending ({0}) and sacrificing do NOT. Lotus has `mana_cost ""` → the hand-cast enumeration skips it
  (suspend-only). WHEN to suspend = search decision (eval 0, valued by the multi-turn rollout).
- **{T},Sac: add 3 of ONE chosen color** (`sac_for_mana_amount:3`): a battlefield-activated ability
  `Action::Kind::SacForMana`, enumerated in `CollectActions` (one variant per candidate colour), applied
  BEFORE casts in both worlds (`ApplyPlanDirect` + `AIEngine::TakeTurn`, pre-BatchPrepay) via
  **`ApplySacForMana`** (tap+sacrifice the source → `AddChosenColorFloat` → `state.floating_mana.<colour>`).
  Credited in `Solve`/`EnumeratePlans` as `ritual_float` (wild). Colour variants of one source are mutually
  exclusive (`sac_source_id` = the permanent's `m_number`; guarded by `SubsetHasDuplicateSacSource` +
  distinct `plan_signature` bucket). Not sac'd twice.
- **REUSABLE CHOSEN-COLOUR-FLOAT DIMENSION (Apex will share this):** `Action::chosen_float_color`
  (sibling to `chosen_x`, TurnSolver.h) + **`AddChosenColorFloat(state, col, amt)`** (SpellEffects.h, the
  colour→`floating_mana` switch, also now backing `ApplyRitualFloat`). Candidate colours = the colours in
  the deck's spell costs, opened to all five under **`MTG_UNPRUNED`/`MTG_UNPRUNE=saccolor`**
  (`UnprunedGate::SacColor`) — NOT hardcoded red. **For Apex "add ten of one colour": stamp
  `chosen_float_color` on the cast action (one variant per candidate colour) and call `AddChosenColorFloat`
  at Apex's resolution — same dimension, no new plumbing.** `Suspend` keyword added to the enum as an
  INERT tag (mechanic is param-modelled) so the Scryfall `keywords:["Suspend"]` field stays faithful.
- **Verify:** build clean; th/hinata/slivers smoke ALL PASS 0 new. Functional: chain fires in ~35/120
  autonomous games (suspend T1→arrive T4/T7→sac red→casts a big dragon); `nonconv=0` (d3+d5) on the
  Lotus-heavy range. **fd-diverge NOT introduced by Lotus** (traced seed 5207: Lotus works; the off-by-one
  fd-diverges are the pre-existing kill-engine win over-projection + the MISSING Dragonstorm/Apex forcing
  illegal off-colour hard-casts of Karrthus/Kolaghan funded by red-as-wild credit — the SAME wild-credit
  pattern the rituals already use; both resolve once Dragonstorm lands and those dragons stop being
  hard-cast). Stage-5 convergence item, not a Lotus lockstep bug.
- **VIEWER 5h TODO (recorded, not wired):** suspend timing surfaces as a plan variant (Bucket A, confirm);
  the sac COLOUR is a NEW small chooser (Bucket B) — a `sac_color` decision type to wire like `vial_charge`.

### Storage-land model (Dwarven Hold + Mercadian Bazaar) — DONE, byte-identity th+hinata+slivers ALL PASS (0 new)
New mechanic (distinct from depletion): `Permanent.storage_counters` int + params `storage_land` +
`storage_charge_mode` ("upkeep_if_tapped" / "tap"). Template `basic_land` (REQUIRED — the mana machinery
keys tappable sources on `tmpl==BasicLand`; "custom"→`None` is NOT a mana source, so these use basic_land
like Sandstone Needle, NOT "custom").
- **BURST (variable / PARTIAL, search-driven):** an untapped charged storage land is a variable {R} source
  = its live `storage_counters` (per-permanent yield via `PermanentManaYield`, threaded through
  `AddSourceToPool`/`BuildPool`/`BuildAvailableMana`/greedy `tap_source`/`TapForCostBacktrack`). A single tap
  performs a **partial** burst — it removes ONLY the payment's remaining shortfall (`cost.ManaValue() −
  produced_total`) worth of counters and adds that many {R}; **the rest PERSIST** on the land for a later turn
  ("burst some now, bank the rest"). `ManaSourceRank` ranks storage **62 (last)** so the shortfall is minimal,
  and the reserve (`ReservableSpecialMask` + `BatchPrepayMainCasts`) holds the battery whenever the cost is
  payable without it. How much to burst is a real SEARCH decision (falls out of which plan the search commits
  to). NOT sacrificed. Lockstep executor (`AIEngine::TapForCostOnce`) / rollout (`TurnSolver::TapForCostDirectOnce`
  + backtracker) / planner. Verified live: a land charged 1→2→3 then partial-burst 3→2 (kept 2), no sacrifice.
- **CHARGE (Stage-6a disclosure — a modeling collapse, not silent):** both cards' charge modes are unified into
  one weakly-dominant rule — **+1 counter at end of any turn the land is left UNTAPPED (idle = not burst).**
  Implemented in `GameEngine::CleanupStep` (executor) + `SimulateEndAndStartNextTurn` (rollout), lockstep. This
  reproduces the LITERAL per-turn counter count of BOTH cards EXACTLY (verified off-by-one): Dwarven's "hold
  tapped → +1 at upkeep" and Mercadian's "{T}: put a counter" both accumulate +1 per idle turn with the earliest
  useful burst on (turn-played + 2). The abstraction: we do NOT model Dwarven's literal untap-step hold nor
  Mercadian's literal charge-tap as board state / a search action — but both are weakly dominant (charging an
  idle storage land is strictly non-worse), so this is a faithful deterministic collapse, not a stolen decision.
- **Known simplifications (disclose):** (a) a tap never bursts MORE counters than the plan needs (goldfish-
  dominated — wasted counters float and are lost); the search explores burst SIZE via plan choice, so there is
  no explicit per-land 0..N burst-amount branch (a literal `MTG_UNPRUNED`-gated burst-count enumeration would be
  a follow-up plan-enumeration change). (b) the rare filter-chain backtracker fallback removes-only-the-shortfall
  too but is not exercised by this mono-red deck. (c) storage handling is entirely gated on `storage_land` →
  every non-storage deck is byte-identical (th/hinata/slivers smoke: 3/3 pass, 0 new). Viewer: a `storage` counter
  badge is surfaced (GameEngine + main.cpp); no new chooser needed (burst size = plan variant, Bucket A).

### Follow-ups from the kill-engine (carry into later steps)
- **CRITICAL for Dragonstorm (item 8):** the tutor-to-battlefield PUT must route each Dragon through the
  shared `OnDragonEnters` (call it, or go through the same `EnterBattlefield` the executor uses) so put-in
  Dragons ping Scourge / trigger Lathliss. The hook exists but is wired only at hard-cast + `CreateToken`
  sites today. Without this, Dragonstorm drops inert bodies.
- **Eval fast-path caveat (validate in Stage 5):** `PendingAttackDamage`/`wins_this_turn` static pre-plan
  estimate (~TurnSolver:503) deliberately OMITS ping/Utvara/firebreathing (they depend on which Dragons a
  plan casts / leftover mana → would over-project → fd-diverge). Kills are found via the ROLLOUT
  (opp.life<=0 after ApplyPlanDirect + SimulateCombat). Under-estimate is safe; confirm zero fd-diverge in 5a.
- **Firebreathing** reads the leftover mana pool + applies temp power (does not tap real sources) — safe
  because Dragonstorm is first-main-only (combat is the last mana use). Revisit only if a firebreathing
  deck is also second-main.
- **Viewer 5h (both autonomous-inert / byte-identical, need wiring):** Scourge ETB ping = a `target`
  decision (fires many times/turn → needs per-ping `--choices` index); firebreathing activation count =
  a new Bucket-B decision type. Autonomous = face / spend-all-leftover = optimal. Wire in 5h.
- **scryfall_reference.json NOT updated** with the 3 new dragons (agent verified vs LIVE Scryfall instead)
  — run `python scripts/audit_card_fields.py --update` + commit before the verify_deck `card_fields` gate.

## Open loops
- [x] User sign-off on deferrals + firebreathing scope + mana-color fidelity — DONE (see above).
- [ ] Stage 2 build (serial integration; lockstep executor/planner/rollout for every new mechanic).
- [ ] **FOLLOW-UP (user, revisit at end): Irencrag Feat + Reality Spasm float color is ALSO wrong** —
      Irencrag adds red, Reality Spasm adds the untapped sources' colors (memory flags the any-color
      float as a latent mis-model). Left on `wild` for THIS build so the byte-identity smoke stays
      unambiguous. User risk read: **Irencrag → red is likely byte-identical for Hinata** (max_casts_after:1
      and the one follow-up is ~always Crackle-with-Power, a red/generic X-spell red pays identically) —
      just set `ritual_float_color:"R"` and confirm Hinata GT unchanged (rebaseline only if it moves).
      **Reality Spasm is trickier** (float must track the untapped sources' colors, not a flat red) — real
      de-abstraction, its own step.
- [ ] Stage 3 re-coverage, Stage 4 baseline profile, Stage 5 verify → converge.
