# Analysis ledger — Angels

Per-deck ledger for the `analyze-deck` workflow (see `.claude/skills/analyze-deck.md`, "The per-deck
ledger"). This file is the durable state a resumed session or a second machine reads to continue;
context is disposable, this is not.

**Deck:** `decks/Angels/Angels.cod` — mono-white Angels tribal, 60 cards + 5 sideboard.
**Started:** 2026-09-17. **Branch:** `phase-1-2-deck-analyzer`.

## The list

| n | card | board |
|---|---|---|
| 3 | Lyra Dawnbringer | main |
| 4 | Youthful Valkyrie | main |
| 4 | Resplendent Angel | main |
| 4 | Bishop of Wings | main |
| 4 | Righteous Valkyrie | main |
| 4 | Archangel of Thune | main (already implemented) |
| 2 | Giada, Font of Hope | main |
| 2 | Serra the Benevolent | main (planeswalker) |
| 1 | Legion Angel | main |
| 4 | Seraph Sanctuary | main (land) |
| 19 | Plains | main (already implemented) |
| 1 | Azorius Chancery | main (already implemented) |
| 1 | Sol Ring | main (already implemented) |
| 1 | Lightning Greaves | main (already implemented) |
| 3 | Swords to Plowshares | main (already implemented) |
| 3 | Unexpectedly Absent | main (already implemented) |
| 3 | Legion Angel | **side — REACHABLE** |
| 1 | Lyra, Archangel of Dawn | side — unreachable |
| 1 | Lightstall Inquisitor | side — unreachable |

## USER RULINGS (2026-09-17)

1. **Legion Angel's sideboard search is REAL and in scope.** User: *"Legion Angel is a real
   sideboard card (as it can search other copies) while the other two are cards I am considering for
   future modifications."* So the 3 sideboard Legion Angels are a reachable zone and must be
   modelled. **Lyra, Archangel of Dawn and Lightstall Inquisitor are NOT reachable** — Legion Angel
   searches only for a card *named Legion Angel* — and are explicitly **out of scope for this
   analysis**. They are future-deckbuilding candidates, to be screened later via
   `deck-screening.md`, not implemented now.
   * *Consequence:* `scripts/analyze_deck.py`'s sideboard-reachability computation is currently
     all-or-nothing (`reachable: true` → scan every sideboard card). Once Legion Angel carries a
     wish param that scan would demand the two out-of-scope cards. The reachability computation must
     become **name-aware** so the reachable set is `{Legion Angel}`. This is a correctness
     improvement to the scan, not a workaround.
2. **Scope: get the original list working**, autonomously, overnight. Performance testing and a
   value-leaf run (to surface slow games) are explicitly welcomed if the analysis finishes.

## Stage 1 — coverage (run 2026-09-17)

`missing` (9): Lyra Dawnbringer, Youthful Valkyrie, Resplendent Angel, Bishop of Wings, Legion
Angel, Seraph Sanctuary, Righteous Valkyrie, Serra the Benevolent, Giada Font of Hope.

`coverage` — all 7 already-implemented cards report `full`. Their bracket notes were read and
classified (Stage 1 requires this):

| card | bracket note | classification |
|---|---|---|
| Lightning Greaves | Equipment subsystem; shroud documented-inert | **Not a gap** — it describes the implementation. Shroud inert vs a passive opponent that never targets us; it *is* live against our own equip (CR 702.6b) and that is already handled. |
| Archangel of Thune | per-life-gain-EVENT team counters; flying inert; lifelink modelled | **Not a gap.** Carries `subtypes: ["Angel"]` — verified, load-bearing for every tribal hook in this deck. |
| Azorius Chancery | Karoo bounce land, {W}{U} modelled as wild | **Not a gap** for this deck; the {U} half is dead in mono-white, which only makes it a worse land, not a mis-model. |
| Swords to Plowshares | exile + controller-lifegain rider inert without Tainted Remedy | **Re-open — see Open items #1.** The rider is inert only when targeting an *opponent's* creature. Targeting **our own** creature gains *us* life, which in this deck is a live engine line. |
| Unexpectedly Absent | X=0 only, opponent-creature targets only (pruned) | **Disclosed narrowing**, carried to Stage 6a. Same self-target question as StP. |
| Sol Ring, Plains | none | clean |

## Open items

1. **Swords to Plowshares / Unexpectedly Absent self-targeting.** Both are currently narrowed to
   opponent creatures in the autonomous search (StP has no `allow_self_target`; UA has it but the
   search is pruned to opponent creatures). In *this* deck self-targeting is a real line: StP on our
   own creature gains life equal to its power → a **life-gain EVENT** → Archangel of Thune puts a
   +1/+1 counter on **every** creature we control, and feeds Righteous Valkyrie's life threshold and
   Resplendent Angel's 5-life end-step token. Swordsing our own 1/1 Spirit token with a wide board
   is plausibly correct. To be assessed at Stage 5; do not assume either way.
2. Whether Bishop of Wings' dies-trigger has any live death path in a goldfish (passive opponent
   never removes our creatures). Candidate paths: the **legend rule** (3 Lyra, 2 Giada, 2 Serra are
   all legendary), lethal-damage SBA, and self-targeted removal per #1.

## Stage 2 — per-card implementation  (COMPLETE)

Research was fanned out to 9 Opus subagents (one per missing card) per the CLAUDE.md agent-fan-out
directive; integration was serial, since `cards.json` and the shared C++ cannot take concurrent
edits. Every card's cost, P/T, keywords, subtypes, loyalty and **every oracle clause** was verified
against live Scryfall (the brief I gave the agents was wrong three times — Seraph Sanctuary's mana,
Resplendent Angel's activated ability, and Serra's ability split — and each time the agent's fetch
corrected it, which is the whole reason 2a says never to trust recall).

| card | cost | tier | new params | viewer bucket |
|---|---|---|---|---|
| Lyra Dawnbringer | `{3}{W}{W}` | 2 | `grants_lifelink` | A (no choice) |
| Youthful Valkyrie | `{1}{W}` | 2 | `own_creature_enters_self_counters` | A (no choice) |
| Bishop of Wings | `{W}{W}` | 2 | `dies_token_keywords` | A (no choice) |
| Righteous Valkyrie | `{2}{W}` | 3 | `own_creature_enters_lifegain_toughness`, `life_above_start_anthem_{life,power,tough}` | A (no choice) |
| Seraph Sanctuary | land | 2 | — (reuses `etb_lifegain` + the shared filter) | A (no choice) |
| Resplendent Angel | `{1}{W}{W}` | 3 | `endstep_lifegain_threshold`, `endstep_token_keywords`, `firebreathing_tough`, `firebreathing_grants_lifelink` | A (`main_phase` activation) |
| Legion Angel | `{2}{W}{W}` | 2 | `wish_requires_name` | A (`tutor_target` variant) |
| Serra the Benevolent | `{2}{W}{W}` | 2 | 3 new loyalty `effect` strings | A (`main_phase` + existing `loyalty` sub) |
| Giada, Font of Hope | `{1}{W}` | 3 | `other_subtype_enters_counters_{subtype,per_each}`, `mana_only_subtype` | A (no choice) |

Shared across cards: **`enters_watch_subtypes`** (a `vector<string>` OR-filter on the entering
creature) — three cards needed "whenever an Angel you control enters" with different payloads, so it
landed **once** on the existing `FireCreatureEnterWatchers` lockstep site rather than as three
one-off flags. Bishop of Wings uses `["Angel"]`, Seraph Sanctuary `["Angel"]` (on a **land** watcher —
the loop scans all permanents, not just creatures), Righteous Valkyrie `["Angel","Cleric"]`.

### The three decisions that were easy to get silently wrong

1. **Giada's counters are a REPLACEMENT EFFECT (CR 614), so they had to be applied ABOVE the enter
   triggers** in `FireEtbWatchers`, not below. Righteous Valkyrie gains life equal to the entrant's
   *toughness*, so the ordering is directly observable as a life total — the only reason it is
   testable at all. Both placements compile and produce plausible boards. Pinned by
   `test/unit/test_angels_tribal.cpp`.
2. **Resplendent Angel's threshold broke a memo-key assumption.** The sim key folded a *bare marker*
   on `life_gained_this_turn > 0`, justified by Ocelot Pride reading ">0 and never the amount". At
   threshold 5 that is false — a 3-life turn and a 7-life turn have different futures and must not
   share a key. `GameState::deck_reads_endstep_lifegain` (bool) became
   `deck_endstep_lifegain_max_threshold` (int), and the fold adds the counter clamped to that
   maximum **only when the max exceeds 1**, so Ocelot Pride / CritterLifegain keys are byte-identical.
3. **Giada's Angel-restricted mana could not use the `colored_creature_only` "any creature"
   collapse.** That collapse is justified in its own design doc as exact *for a mono-tribal deck*;
   this deck runs 4 Bishop of Wings (a Human **Cleric**), a non-Angel creature Giada's mana may not
   legally cast. So `mana_only_subtype` carries the subtype literally, on a thread_local pay scope
   (the documented pattern — threading a card pointer through every `TapForCost` signature is what
   `CreatureAbilityPayScope` exists to avoid), **and is mixed into the mana-cache key** — gated on a
   restricted source being on the battlefield, so no other deck's keys move.

### Coverage-scan fix (predicted, then observed)

Adding `wish_from_sideboard` to Legion Angel flipped `sideboard.reachable` to true, and the scan then
demanded implementations of the two sideboard cards the user ruled **out of scope**. Reachability was
always a *set* question that the code had collapsed to a bool. `SideboardReachability` now returns the
reachable NAME SET: a wish carrying `wish_requires_name` contributes only that name; an unrestricted
wish (Living Wish) still contributes the whole sideboard. Verified both ways — Angels reports
`reachable_names: ["Legion Angel"]` with the two others listed as `unreachable_names`, and
EldraziDisplacerFlicker is unchanged (all 8 still reachable).

### Verification that the new paths actually FIRE

Per this repo's "digest equality can mean BROKEN" lesson, traced before trusting any number. Over 20
games at d3/b300 the logs show: all 9 cards cast; `REVEAL source="Legion Angel (searched)"` fetching
sideboard card 200001 **to hand** (10 reveals, and 11 Legion Angel casts off a 1-of mainboard, so the
wish chain is real); 4 Serra loyalty `ABILITY` activations; 34 `4/4 Angel Token` permanents; player
life reaching **83 by turn 5** with distinct deltas of 1 / 4 / 5 / 6 / 7 (Seraph Sanctuary, Bishop,
and Righteous Valkyrie reading toughness *with* counters and anthem applied).

`test/unit/test_angels_tribal.cpp` (13 cases, all passing; suite is 104/104) pins what logs cannot
show: the replacement-before-trigger ordering, the "already control" off-by-one, the subtype filter in
both directions, the anthem's `StartingLife() + 7` edge (the assertion that fails if anyone writes a
literal 27), Lyra's lifelink grant, the cumulative-5 threshold, and Ocelot Pride's unchanged behaviour.

Two of those tests initially failed and **both were my test's fault, not the engine's** — worth
recording because each is a real fact about the deck:
* Soul Warden is a Human **Cleric**, so Righteous Valkyrie correctly pays on it. The OR-filter was
  right; the test needed a genuinely neutral creature (Llanowar Elves).
* An entering Archangel of Thune is itself a "whenever you gain life" watcher, so Righteous
  Valkyrie's gain immediately puts a counter on the whole team **including the Archangel that just
  arrived** — 3 counters, not 2. Both behaviours are now pinned explicitly.

## Stage 2d-bis — cost / field audits

* **`audit_card_costs.py` (live Scryfall):** rate-limited into 429s when two clients ran at once.
  Backed off and re-verified the nine new cards **sequentially and mechanically** instead — cost,
  power, toughness, keywords, subtypes, Legendary supertype, loyalty, and the presence of every
  Scryfall oracle clause in the entry: **0 mismatches on all 9**.
* **`audit_card_fields.py --update` + offline diff:** snapshot refreshed and committed (385 cards
  checked). **Zero hard mismatches on any Angels card.** The audit does report 2 hard mismatches —
  `Apex Altisaur` (`enrage`, `fight`) and `World War Hulk` (`double`) — which are **not this deck's
  cards**. They were missing from the snapshot entirely until this refresh added them, so this is
  the first time they have ever been diffed; the defect belongs to the Saga/Altisaur workstream, and
  they look like the same "real keyword, inert in goldfishing" class already allowlisted for
  Progenitus / Goblin Piledriver. **Surfaced, not fixed** — allowlisting another workstream's card is
  their call, not mine. The 319 oracle-text advisories are the normal consequence of this repo's
  bracket-note convention and are not findings.

## Stage 4 — baseline profile

Generated **twice**. The first run was invalid and thrown away — see 4a.

Final profile (`decks/Angels/Angels.profile.json`, under `GenericProvider`): `stop_at 4`,
`min_lands 1`, `max_lands 5`, `curve_check two_drop`, no required pieces. Notable card scores:
Sol Ring `+0.70`, Giada `+0.41`, Righteous Valkyrie `+0.30`, Resplendent Angel `+0.12`, Bishop of
Wings `+0.10`; the two removal spells score NEGATIVE (Unexpectedly Absent `-0.27`, Swords to
Plowshares `-0.19`), which is the expected shape against a passive opponent — they are near-dead
cards in a goldfish, and that is a property of the measurement, not of the list.

Discard analysis verdict: `STATUS_QUO_OK` (order A/B `mean_delta` 0.0005 at d0 / 0.0 at d3).

## Stage 4a — provider routing (THE CATCH)

`provider_audit.py` reported **`Angels -> CritterLifegain`**: a foreign provider, inherited by
accident. Cause: the `critter` signature trips on `lifegain_each_own_creature_counters`, which is
**Archangel of Thune** — a card Angels runs 4 of. That param is archetype-NEUTRAL (it describes what
a card does, not which deck it belongs to), which is the exact misroute class this check exists for
and which has now caught five decks (Mirrorwing, StompySurprise, Minotaur, Dragons, Angels).

It was not cosmetic: `CritterLifegainProvider` overrides discard buckets, `CastOrderRank` **and
`LegendKeepIndex`** — and this deck runs 3 Lyra + 2 Giada + 2 Serra, so the legend-keep override was
live. Every number produced under it was measured through another deck's narrowing, so **the first
profile was discarded and regenerated.**

Fix: an `angels` signature OR-ed across **five different cards'** gated params (Giada's as-enters
counters, Lyra's `grants_lifelink`, Youthful Valkyrie's self-counter watcher, Righteous Valkyrie's
life anthem, Legion Angel's named wish), routed **above** the `critter` branch, returning
**`GenericProvider`** — no narrowing at all, per the rule that a new deck earns its own provider only
once it has a *measured* hook to hold. Deliberately excludes Lightning Greaves and Sol Ring: a
colourless staple any deck might splash is precisely what the Dragons block warns against.

## Neutrality proof (this is the part that matters for the rest of the repo)

`test/regression.sh --smoke` FAILS 9 of 80 cases — **and it fails the identical 9 on a clean HEAD
worktree with none of this work in it.** Built `8edde761` in a separate worktree (separate
`test/logs/`, so no chimera), ran the same tier, and diffed: **byte-for-byte identical averages AND
identical play digests on all 80 cases, failures included.**

So every Angels change — 13 new `CardParams`, the shared enter-watcher filter, the `ComputeLordBonus`
anthem pass, the `CreatureHasLifelink` lord, the end-step threshold, the mana-cache subtype fold, the
`SpellSubtypePayScope`, and the provider routing change — is **provably neutral for every other deck**.
The 71 passing cases match GT exactly; the 9 failing ones fail the same way with or without me.

**The 9 pre-existing failures are NOT mine to accept.** GT was accepted at commit `03cadcf2`
(2026-09-15); `4f23627e` ("let the search sacrifice a creature the same plan just cast") landed after
it and was never rebaselined. Every affected deck is a sacrifice deck (Stompy/Natural Order,
FiveColour, CritterLifegain) and every one got **faster**, so it reads as an un-accepted improvement
belonging to that workstream. Left alone — see the closing questions.

## Stage 5 — verification

`python3 scripts/verify_deck.py decks/Angels/Angels.cod`:

| gate | verdict |
|---|---|
| coverage | **PASS** — all 16 cards full, 0 missing, 0 partial |
| card_costs | **PASS** — every mana cost matches Scryfall |
| card_fields | **FAIL** — 2 mismatches, both on cards this deck does not contain (see 2d-bis) |
| viewer | **PASS** — self-guard + surface sweep clean; oracle cross-check reports no choice phrase unmodeled |
| viewer_wiring | **PASS** — `bounce`, `target` both emitter- and GUI-wired |
| mismatch | **PASS** — 0 `[nonconv]`, 0 `[fd-diverge]` over seeds 7001/7002 x 60 games, both arms |
| play_invariants | **PASS** — 8 games / 880 decisions: determinism + integrity + progress hold |
| clause_ledger | SKIP (covered by coverage + bracket notes + oracle diff) |
| claude_sweep | see below |

### 5a — the mismatch gate found a REAL BUG, and it was mine

First run: **4 `[fd-diverge]` lines** (seeds 7017 and 7047, `realized_win=6 predicted_win=5`). Root-caused
rather than averaged away.

Reproduced deterministically (`--seed 7017 --game-index 6 --depth 5 --budget-ms 20`) and reconstructed
the board from the game log: Giada out, Archangel of Thune entering as a 4/5 (Giada's counter), three
Seraph Sanctuary lifegain events each firing Thune's team pump — Giada 5/5, Thune 7/8, 12 damage on
T5, lifelink 7. The executor was **self-consistent**, so the over-projection was in the rollout.

Cause: **`TurnSolver::ApplyPlanDirect` — the rollout's cast path — had no `SpellSubtypePayScope`.** So
the rollout happily paid for **Bishop of Wings (a Human Cleric)** out of Giada's Angel-only mana, while
the executor correctly refused; the search committed to a line it could not execute and lost the turn.
Fixed by scoping that site (and the graveyard-play twin in both worlds). The flagged game now wins **T5**,
matching the prediction, and both arms are clean.

**Coverage proven, not assumed.** A prose "I added it at the sites I found" is not proof, so the fix
ships with an audit (`MTG_SUBTYPE_MANA_AUDIT=1`, inert by default) that counts consultations reaching a
restricted source with no scope set, broken down by call site. Over 120 games at d5:
`checked=2535735 unset_scope=12690 (payer=0 flowprobe=4118 backtrack=3530 bound=5042)`.
**`payer=0` — the real payer always knows its spell.** The remainder are a prune-probe, the plan-level
prepay and an upper bound, where permissive is the safe direction (a bound must not under-count; a
prune-probe must not prune a payable line). Disclosed rather than hidden: the plan-level prepay can
still over-accept a multi-spell plan, and the measured consequence of that is 0 fd-diverge.

### 5b — multi-depth sanity (200 games/depth, seed 8001, budget 200ms)

| depth | avg turn-to-win |
|---|---|
| 0 | 5.825 |
| 3 | 5.445 |
| 5 | 5.445 |

Monotonic and plausible: a mono-white Angels aggro deck killing on turn ~5.4 is the right clock, and
d3 == d5 means the search has converged rather than being budget-starved.

### 5d — claude-play sweep

**The first attempt was mis-parameterised and I threw it away.** I told 18 agents to use
`--seed 4242 --game-index 0..17`, believing that varied the game. It does not: the SEED fixes the
shuffle and `--game-index` only selects the opponent-spawn pattern, so all 18 agents replayed the
**same opening hand**. Five finished (all clean, all winning T5 in lockstep with the search) and each
independently reported the same blind spot — *"Giada never hit play; another game must cover the
Giada-mana check"* — which is what exposed the error. The rest were stopped and relaunched.

The seed table was then rebuilt from a 40-game log scan, choosing games that actually deploy the new
cards, so the sweep tests the mechanics rather than one lucky curve. (A second self-inflicted trap on
the way: the per-game log's `seed` field is ALREADY the per-game seed, so the first table
double-counted `seed + gameNumber` and its coordinates did not reproduce.)

_(results table filled in when the agents land)_

## Claude-play sweep

commit: 3f05cd05
seeds: 4304/4 4306/6 4309/9 4312/2 4313/3 4315/5 4318/8 4319/9 4323/3 4325/5 4326/6 4330/0
       (+ 5 games at seed 4242 from a discarded first wave, see below)
games: 17
flags: 0 unresolved

Every game was driven by an independent Opus agent through the `--claude-play` stateless-replay
protocol, each reading the deck's `cards.json` entries first (Rule 0) and re-deriving the arithmetic
by hand. **Claude matched the search's win turn in all 17 games** (12 at T5, 5 at T6) — which for
this deck is unsurprising: the lines are forced curve-outs, not decision-dense.

Every new mechanic was verified with numbers, not eyeballs:

| mechanic | verified in | what was checked |
|---|---|---|
| Giada's as-enters counters | 4304, 4315, 4318, 4325 | The entrant never counts itself, Giada counts herself: 1 / 2 / 3 / 4 counters as the Angel count climbs. |
| **Giada's Angel-only mana** | 4304, 4315, 4318, 4325 | Never leaked onto a non-Angel. **4318 is the negative proof**: on a board where casting Serra would have required spending Giada's mana on a non-Angel, the engine correctly did not offer Serra at all — the exact defect fixed in 5a. |
| Thune's per-EVENT counters | 4304, 4309, 4312, 4313, 4326, 4330 | Two/three separate life-gain events produce two/three separate team pumps, never one merged counter. |
| **Righteous Valkyrie reads LIVE toughness** | 4304, 4306, 4309, 4312, 4315, 4318, 4326 | Gains the entrant's *current* toughness including Giada's counters, Lyra's lord and its own anthem — e.g. +7 for a printed 3/4, +10 for a printed 5/5. **This is the replacement-before-trigger ordering, confirmed in live play.** |
| the 27-life anthem edge | 4312, 4315, 4326 | **4312 proves it positively**: at 26 the trigger gained +5, not the +7 it would have gained if the anthem were already live; it armed at exactly 27. 4326 saw both sides through combat damage. |
| Legion Angel's wish | 4304, 4306, 4313, 4315, 4318, 4319, 4325, 4330 | Fetches a sideboard copy (ids 200001/200002) **to hand**, library size unchanged; chains correctly through 2 fetches; respects the 3-copy pool. |
| Lyra's lord + lifelink grant | 4315, 4319, 4323, 4330 | Both halves skip Bishop of Wings (Human Cleric) and Lyra herself; the lifelink total matched the Angels' damage exactly, with Bishop's damage excluded. |
| Resplendent Angel's end step | 4319 (positive, twice), 4323 + 4313 (negative) | Fires at 6 and 7 life gained; correctly does NOT fire at 4, or at 1. |
| Bishop / Seraph Sanctuary | 4319, 4326, 4330 | +4 and +1 per Angel, per copy; neither fires for a non-Angel, and Bishop does not pay for its own entry. |

### The discarded first wave (recorded because the error is instructive)

The first sweep used `--seed 4242 --game-index 0..17`, on my incorrect belief that the game index
varies the game. **It does not** — the SEED fixes the shuffle and `--game-index` only selects the
opponent-spawn pattern, so all 18 agents were handed the *same opening hand*. Five finished before I
caught it (all clean, all T5, matching the search) and every one of them independently reported the
same blind spot — *"Giada never hit play; another game must cover the Giada-mana check"* — which is
what surfaced the mistake. The rest were stopped and the seeds rebuilt from a 40-game log scan
choosing games that actually deploy the new cards. Their 5 results are counted above because they
are valid games; they are simply 5 views of one game rather than 5 independent ones.

A second trap on the way: the per-game log's `seed` field is **already** the per-game seed, so the
first rebuilt table double-counted `seed + gameNumber` and its coordinates did not reproduce.

### Observations recorded, not flagged

1. **Swords to Plowshares is never offered** (see Open items #1). Four agents independently traced
   this to the documented generic gate that suppresses `controller_lifegain_equals_power` spells
   absent a Tainted Remedy, because handing a passive opponent life is strictly bad. Correct for the
   decks it was written for; **arguably wrong for Angels**, where self-targeting gains US life and
   feeds the whole engine. Not a rules violation — an archetype-blind narrowing.
2. **Duplicate plan entries.** Every agent saw runs of byte-identical plans (commonly six
   `land=Plains; cast: Serra the Benevolent`); several probed them individually and confirmed
   identical resulting states. Harmless to the search, but real noise on the **human** decision
   surface — two different Serra copies also render identically, since casts carry no source id.
3. **The `bottom` step's `ai_choice` wanted to bottom Legion Angel** (4325), which would discard the
   deck's only maindeck copy *and* the entire 3-card sideboard wish chain. This is the documented
   per-step-hint caveat in `claude-play.md`, not an engine defect — but it is a concrete example of
   that hint being actively bad, worth remembering when the mulligan stage is run.

## Stage 6a — encoded heuristics & assumptions disclosure (MANDATORY)

Compiled from the code, not from memory.

### 1. Global engine assumptions shaping every number above

| assumption | effect on this deck |
|---|---|
| A single **passive opponent**: never blocks, attacks, casts, removes, or gains/prevents life | **Flying is inert** on all 9 fliers, **first strike** inert on Lyra, **vigilance** inert on tokens. Also means the deck's 3 Swords + 3 Unexpectedly Absent are near-dead cards, which is why they score negative. |
| The opponent **takes no turns** | Resplendent Angel's "at the beginning of **each** end step" collapses to one trigger per turn. Disclosed as a provably-inert collapse. |
| **Clairvoyant search** over a deterministically shuffled, known library | The win turns are a best-case clock, not a realistic one. |
| **First main only** — `DeckUsesSecondMain` does NOT fire for this deck | No card here generates a combat resource (no spectacle, no combat untap, no combat-damage free cast). Confirmed per card. |
| Opponent creature spawns in 8 of 10 game indices | The only reason `targeting: creature` ever has a legal target. |
| Measurement settings | profile-driven; the depth sweep above is d0/d3/d5 at budget 200 ms. |

### 2. Card-modeling simplifications (every bracket note in this deck)

* **Flying** (Lyra, Youthful Valkyrie, Resplendent Angel + its token, Righteous Valkyrie, Legion
  Angel, Giada, Archangel of Thune, Serra's token) — parsed, structurally inert for combat. **But
  NOT cosmetic:** Serra the Benevolent's +2 pumps "creatures you control with flying", so this is
  the first effect in the engine that READS the keyword. Every flier's keyword list is load-bearing.
* **First strike** (Lyra) — parsed, inert: no blockers, and the engine collapses combat damage into
  one event.
* **Vigilance** (Serra's and Resplendent Angel's tokens) — parsed, inert: the opponent never
  attacks, so there is never a reason to hold a blocker back. **Giada's vigilance is NOT inert** —
  she taps for mana, so it lets her attack and still pay for an Angel.
* **Serra the Benevolent's −6 emblem** — granted as a **no-op**, no emblem zone built. It is a damage
  floor on OUR life total and nothing in this game damages us. The loyalty COST is modelled, and the
  ability is value-gated out of the autonomous search (the Ajani-0 precedent) while staying
  reachable in human play. **PROVISIONAL — needs your sign-off.**
* **Azorius Chancery** {W}{U} modelled as wild — the {U} is dead in mono-white, so this only ever
  makes the land worse, never better.
* **Seraph Sanctuary taps for {C} only** — faithful, and a real constraint: 4 of 24 lands cannot pay
  a {W} pip, so Sanctuary-heavy openers genuinely colour-screw. Not a modelling gap.

### 3. DecisionProvider heuristics — **Angels rides `GenericProvider` and overrides NOTHING**

No deck-specific narrowing at all: pure search within the global assumptions above. This is
deliberate (Stage 4a) — a new deck earns its own provider only once it has a *measured* hook.

Two **generic** narrowings do affect this deck and you should see them:

* **Swords to Plowshares / Unexpectedly Absent self-targeting is suppressed.** A generic gate skips
  `controller_lifegain_equals_power` spells unless a lifegain→loss enabler is live. Sound reasoning
  for the decks it was written for; in Angels self-targeting gains **us** life, which is an engine
  trigger (Thune counters, the anthem, Resplendent's 5-life threshold). **What it could cost: a real
  line.** Swordsing our own 1/1 Spirit token with a wide board is plausibly correct and the search
  can never discover it. Flagged as the top follow-up.
* **Unexpectedly Absent is pruned to X=0 and opponent-creature targets** (pre-existing, disclosed in
  its own bracket note).

### 4. Play-viewer auto-resolved decisions

**Viewer-ready.** `audit_viewer_decisions.py` reports no HARD MISS, no self-guard failure, no driver
failure, and its oracle-text cross-check finds *no choice phrase left unmodeled*. Every interactive
choice this deck's cards create is surfaced: `main_phase` plans (incl. Serra's loyalty ability as its
own labelled sub-decision and Resplendent Angel's activation, newly labelled), `target`, `bounce`,
`discard`, `mulligan`, `bottom`.

Three known, disclosed gaps:

1. **Legion Angel's "you MAY" decline is not offered.** Taking a free card costs no mana, life,
   tempo or shuffle, so it is weakly dominant. Wiring it is one argument (`human_repick=true`) if you
   ever want it. **PROVISIONAL.**
2. **Giada never appears as a hand-tappable source in the viewer's "Tap mana" mode.** Correct by
   design — `HumanPreTapFaces` returns "" for a `creature_mana_only` source so a restricted unit
   cannot be laundered into the float — but it reads as a bug to a human. Same as Cavern of Souls.
3. **Duplicate plan entries** (see the sweep). Not a missing decision; a cluttered one.

### 5. Things I did NOT do, deliberately

* **Did not touch ground truth.** The 9 pre-existing smoke failures belong to `4f23627e`.
* **Did not add Angels to the regression suite.** That is its own task with shared time budgets, and
  it cannot be `--accept`ed cleanly while those 9 are outstanding.
* **Did not implement Lyra, Archangel of Dawn or Lightstall Inquisitor** — your ruling: unreachable,
  future-deckbuilding candidates. They are for `deck-screening.md` when you want them.
* **Did not generate a mulligan profile or a value leaf.** Per the pipeline-ordering policy those are
  the LAST stages and you kick them off.

## Search shape — ADOPTED (and the answer to "should we run a value leaf?")

**No value leaf is needed for this deck, and running one would be wasted work.**

`scripts/shape_probe.py` (3 seeds x 60 games, fresh seeds 880500+, d5b20), one pooled batch:

| shape | units | ratio | d_avg | better | worse |
|---|---|---|---|---|---|
| `heur` (plain rollout ladder, the default) | 8,468,329 | 1.00 | — | — | — |
| `esc_nl` | 4,169,040 | 0.49 | +0.0000 | 0 | 0 |
| **`fit_nl`** | **3,122,382** | **0.37** | +0.0000 | 0 | 0 |

The probe reported the shapes as UNRESOLVED on a z-test — but that is the *light-deck tie* case, and
a sign test is the wrong instrument when the play is literally the same. **Digest-style per-game
verification settles it:** 600 games (2 seeds x 300, d5 b200, `MTG_DUMP_WINS`) with the sidecar ON vs
OFF differ in **0 games**. Identical play, not merely equal averages.

What it buys, at d5 b2000 over 300 games: **wall 25.88s -> 14.08s (1.84x), CPU 199.24s -> 86.30s
(2.31x)**, average unchanged at 5.3967. Re-verified with the sidecar in place: 0 `[fd-diverge]`,
0 `[nonconv]`, and the depth sweep is unchanged (5.825 / 5.445 / 5.445).

Adopted as `decks/Angels/Angels.value.json`:
```json
{"value_play": {"ladder": "single", "leaf": "none", "alpha": "relaxed"}}
```
(The file's PRESENCE is what activates it; `leaf: "none"` means there is no learned model to build.)

Adopted without asking under the standing no-drawback rule (USER 2026-09-03): identical quality plus
a large cost win is not a trade-off. **Why the deck proves everything inside the horizon:** it is a
linear aggro curve-out — every game in the sweep was decided by turn 5-6 with no combo to project
past, so the rollout's horizon evaluation was never the binding constraint. That is precisely the
profile for which a value leaf is a net negative (cf. Stompy).

**Practical consequence:** the 2.3x CPU saving is exactly what makes the deferred, user-kicked-off
mulligan generation cheaper when you want it.

## Performance profile (with the leafless shape adopted)

One pooled `mtg --batch` over 1600 games (3 x 400 at d5 + 400 at d3, budget 200 ms), 32 cores:

* **wall 22.66 s, CPU 508.58 s** — about **0.32 s of CPU per game**.
* **Slow tail: 50 of 1600 games (3.1%) exceed 2 s**; median slow game 3.1 s, worst 12.5 s.
* The worst game (`--seed 9126 --game-index 26`) re-runs at **6.3 s single-threaded**, so roughly
  half of its batch cost was contention, not the game.
* Per-job averages agree across seeds (5.405 / 5.360 / 5.360 at d5; 5.383 at d3).

### What the slow tail is NOT (two hypotheses I tested and dropped)

1. **Lightning Greaves' free re-equip.** The single worst game shows `ABILITY: Lightning Greaves` on
   *every* turn with a growing board, which looks exactly like an equip-enumeration blowup — Equip {0}
   emits one action per (Equipment, creature) pair each turn, and shroud/haste are near-inert here.
   **The data refutes it:** across 300 logged games, Greaves was equipped in 18% of slow games vs 12%
   of fast ones (n=11 slow), 0.45 vs 0.26 equips per game. That is nothing.
2. **Board complexity.** Slow and fast games are statistically indistinguishable on every metric the
   log exposes: win turn 6.18 vs 6.33, peak creatures 4.91 vs 4.66, casts 4.55 vs 4.27, peak hand
   6.27 vs 7.17.

So **the slow tail is not board-shaped**, and I am not going to invent a mechanism for it. It is most
likely search-internal (how long the budget runs before a win is proven; `enum-memo` on the worst game
was 97 hits / 21,097 misses, i.e. an almost entirely non-repeating plan space). Nailing that down is a
profiling task, not an analysis one — and at 3.1% of games with the batch runner absorbing them into a
single tail, it is not currently costing anything worth fixing.

**No action taken.** Recorded so the next person does not re-run the same two dead ends.

## PENDING YOUR SIGN-OFF (provisional; nothing here blocked the work)

1. **`card_fields` gate is RED on two cards that are not in this deck** — `Apex Altisaur`
   (`enrage`, `fight`) and `World War Hulk` (`double`). My `--update` added them to the Scryfall
   snapshot for the first time, so this is their first-ever offline diff. They look like the
   "real keyword, inert in goldfishing" class already allowlisted for Progenitus / Goblin
   Piledriver, i.e. a `scryfall_divergences.json` entry rather than a code fix — **but they belong
   to the Saga/Altisaur workstream and allowlisting another workstream's card is not my call.**
   I did NOT sign these off, so `verify_deck.py` still exits non-zero on Angels.
2. **Serra's −6 emblem as a modelled no-op** (see 6a §2).
3. **Legion Angel's "you may" decline not being surfaced** (see 6a §4).
4. **Should Swords to Plowshares be allowed to target our own creatures in this deck?** This is a
   real strategic question, not paperwork — see Open items #1.

## Approved deferrals

_(none yet — every proposed deferral is PROVISIONAL until the user signs it off; see the closing
report)_

## Verification (Stage 5)

_(not started)_
