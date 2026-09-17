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

## Approved deferrals

_(none yet — every proposed deferral is PROVISIONAL until the user signs it off; see the closing
report)_

## Verification (Stage 5)

_(not started)_
