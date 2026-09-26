# Analysis ledger — WhiteKnights

Per-deck ledger for `decks/WhiteKnights/WhiteKnights.cod`, per the `analyze-deck` skill ("The
per-deck ledger — durable state across compaction AND handoffs"). Updated continuously during
the run.

**Run started:** 2026-09-25. Branch `phase-1-2-deck-analyzer`, base commit `57c17eac`.

## Decklist (60 main / 0 side)

| n | card | role | status at Stage 1 |
|---|---|---|---|
| 4 | Dauntless Bodyguard | 1-drop Knight | implemented |
| 4 | Venerable Knight | 1-drop Knight | implemented |
| 4 | Worthy Knight | cast-trigger token maker | implemented (**bad bracket note — see below**) |
| 3 | Acclaimed Contender | ETB dig | implemented (**dig filter too narrow for THIS deck**) |
| 1 | Valiant Knight | Knight lord + activated double strike | **MISSING** |
| 4 | Knight Exemplar | Knight lord | implemented |
| 3 | Hero of Bladehold | attack-trigger engine | **MISSING** |
| 22 | Plains | land | implemented |
| 1 | Sol Ring | ramp | implemented |
| 1 | Swords to Plowshares | removal | implemented |
| 1 | Unexpectedly Absent | removal | implemented |
| 4 | Accorder Paladin | battle cry | **MISSING** |
| 1 | Silverblade Paladin | soulbond double strike | **MISSING** |
| 1 | Adeline, Resplendent Cathar | attack-trigger tokens | implemented |
| 3 | Aether Vial | free deploy | implemented |
| 1 | Lightning Greaves | haste enabler | implemented |
| 2 | Benalish Marshal | anthem | implemented |

No sideboard, so no wish reachability question (`reachable: false`, "deck has no sideboard").

**Relationship to the existing `Knights` deck.** This is a *different* list, not a revision:
`decks/Knights/` is a 4-colour-fixing Vial list (Unclaimed Territory / Secluded Courtyard /
Tournament Grounds, Inspiring Veteran, Marshal of Zhalfir, Haytham Kenway) and it is a SHIPPING
deck with its own profile, value sidecar and exhaustive keep table. WhiteKnights is mono-white
Plains-only and runs four cards Knights only ever had in its (unreachable) sideboard. Both decks
stay; this run must not disturb `Knights`' artifacts.

## Stage 1 — Coverage check (DONE)

`python3 scripts/analyze_deck.py decks/WhiteKnights/WhiteKnights.cod --coverage-only`

* **missing (4):** Valiant Knight, Hero of Bladehold, Accorder Paladin, Silverblade Paladin.
* **full (13):** everything else, inherited from the `Knights`/`KittyEquipment`/`Angels` work.
* Engine support checked before fanning research out: `Keyword::DoubleStrike`,
  `params.grants_double_strike`, `HasDoubleStrikeFromLords` and `HasDoubleStrikeFromEquipment`
  all **exist** (`src/ai/Combat.cpp:133-155`, `src/cards/CardDatabase.h:177`); **battle cry and
  soulbond do not exist anywhere in `src/`**.

### Bracket-note reclassification (mandatory Stage 1 step)

Every inherited bracket note on the 13 "full" cards was re-read against live Scryfall. Two are
**not** acceptable deferrals:

#### 1. Worthy Knight's bracket note describes a clause THAT DOES NOT EXIST — a fabrication

cards.json carries:

> `[Simplified: the real card's 'three or more tokens -> exchange for a 2/2 Knight' clause is not
> modelled; the 1/1 Human tokens are kept (a minor under-count of late-game power).]`

Live Scryfall (`cards/named?exact=Worthy Knight`, March of the Machine Commander, `card_faces`
absent) returns exactly one clause:

> `'Whenever you cast a Knight spell, create a 1/1 white Human creature token.'`

There is no token-exchange clause on the card. The **implementation is already faithful and
complete**; the bracket note invents a missing feature and then apologises for it. This is the
Irencrag Feat failure class the skill's 2a warns about (a fabricated oracle in cards.json), and
it is the reason 2a says never to trust the existing entry over Scryfall.

**Resolution:** delete the fabricated clause from `oracle_text` and drop the bracket note.
Worthy Knight then has zero deferrals. No C++ change, no behaviour change.

#### 2. Acclaimed Contender's dig filter is narrowed by a justification that is FALSE for this deck

cards.json carries `etb_dig_subtypes: ["Knight"]` and the note:

> `[Only Knight is dug for: the deck contains no Auras/Equipment/legendary artifacts. ...]`

That is true of `decks/Knights/` (where the note was written) and **false of WhiteKnights**,
which runs **1 Lightning Greaves — `Artifact — Equipment`** (subtype `Equipment` is already on
its cards.json entry). The real card reveals "a Knight, Aura, Equipment, or legendary artifact",
so as shipped the Contender is forbidden from finding a card it is legally allowed to find.

**Resolution:** widen to `etb_dig_subtypes: ["Knight", "Aura", "Equipment"]`.
* Faithful for the two subtypes the engine can express.
* **Inert for `decks/Knights/`** (no Equipment, no Auras in that list) → that deck's play must
  come back byte-identical; verified by digest, recorded under "Audits".
* "legendary artifact" is a supertype+type pair, not a subtype, so the param cannot express it.
  WhiteKnights runs no legendary artifacts (Sol Ring and Aether Vial are both plain `Artifact`),
  so this residue is **provably inert for this deck** — disclosed in Stage 6a, not silently
  dropped.

The dig machinery itself is sound and needed no work: `PerformEtbDig`
(`src/core/SpellEffects.cpp:617`) correctly excludes `self` from the "control **another** Knight"
condition (line 629), and WHICH match to take is a **searched** axis (plan-variant pin at line
681) plus a provider hook (`EtbDigCandidates`) plus a wired human `dig` decision — not an
arbitrary first-match.

#### Notes confirmed as genuine, already-reviewed deferrals (no action)

| card | clause | why inert |
|---|---|---|
| Dauntless Bodyguard | ETB "choose another creature"; sac for indestructible | passive opponent deals no damage and casts no removal → indestructible never matters; sacrificing a 2/1 is never correct |
| Venerable Knight | death trigger (+1/+1 counter on a Knight) | creatures never die: no blockers, no opponent removal, and the deck's only sac outlet (Dauntless Bodyguard) sacrifices *itself* |
| Knight Exemplar | first strike + indestructible grant | both need a blocking/removing opponent; the +1/+1 Knight lord IS modelled |
| Swords to Plowshares / Unexpectedly Absent / Adeline / Aether Vial / Lightning Greaves | inherited, previously user-reviewed | unchanged by this run |

## Stage 2 — Implementation

Per-card research fanned out (4 Opus agents, one per missing card) per the skill's Stage 2
decomposition; integration is serial (cards.json + shared combat C++ cannot take concurrent
edits).

| card | n | tier | status | how |
|---|---|---|---|---|
| Accorder Paladin | 4 | 2 | researched | new `battle_cry_power` + 4th block in `ApplyAttackSelfPumps` + `PendingAttackDamage` projection |
| Hero of Bladehold | 3 | 2 | researched | shared `battle_cry_power` + existing `attack_creates_tokens` family + **new `attack_tokens_per_opponent` (bug fix)** |
| Valiant Knight | 1 | 1 + 3 | researched | lord clause = Tier 1 (`lord_effect`, Knight Exemplar's shape); activated grant = Tier 3 (`PermAbilityMode::GrantDoubleStrike` + `double_strike_grant_cost`, Heliod's `lifelink_grant_cost` shape) + `Permanent::temp_double_strike` |
| Silverblade Paladin | 1 | 3 | researched | new `soulbond` + `Permanent::paired_with` (read-time verified) + arm on `HasDoubleStrikeFromLords` + `SoulbondPartner` provider hook + `attach_host` viewer reuse |

Scryfall corrections — **three of the four cards had a stat that recall would have got wrong**:
**Accorder Paladin is a 3/1** (not 2/1), **Valiant Knight is `{3}{W}` 3/4** (not `{2}{W}{W}` 2/2),
and see the fabrication below.

### I PUT A FABRICATED CLAUSE IN THE HERO OF BLADEHOLD BRIEF (my error, not the agent's)

The research brief I wrote asserted Hero of Bladehold has a *"Whenever this creature attacks, put
a +1/+1 counter on each creature you control"* clause, and instructed the agent to treat it as
"arguably the card's main power in a goldfish". **No such clause exists.** The card is exactly:

```
Battle cry (Whenever this creature attacks, each other attacking creature gets +1/+0 until end of turn.)
Whenever this creature attacks, create two 1/1 white Soldier creature tokens that are tapped and attacking.
```

The agent refused the premise, re-verified against two printings (`tdc` and the original `mbs/8`,
oracle is per-`oracle_id` and identical across them) and reported the brief as wrong. I then
confirmed it myself. This is the same fabricated-oracle failure class as the Worthy Knight bracket
note found in Stage 1 — committed here *by the orchestrator in a subagent brief*, which is a route
the skill's 2a warning does not explicitly cover. **Lesson for this ledger: the "never rely on
recall" rule applies to the PROMPT you hand a subagent, not just to the cards.json entry.** A brief
that asserts a clause is an instruction to implement it; only the agent's Scryfall discipline
stopped a phantom `+1/+1 counter` param from being designed, built and measured.

### A second, smaller modelling bug found in passing: `attack_creates_tokens` is hard-multiplied by opponent count

`FireAttackCreateTokens` (`src/core/SpellEffects.h:12325`) and the matching projection
(`src/ai/TurnSolver.cpp:6714`) both multiply the token count by `gamesetup::OpponentHeads()`.
That is **correct for Adeline** ("for each opponent, create a ... token") and **wrong for Hero of
Bladehold**, whose "create two" is a flat count. It is inert in 1-opponent goldfishing but would
double Hero's tokens in any 2HG (`opponent_heads: 2`) configuration — and this repo ships 2HG
variants (`angels2hg`, `knights2hg`, `melira2hg`). Fix: new `attack_tokens_per_opponent` defaulting
to **true**, so Adeline and every existing deck stay byte-identical, set **false** on Hero.

**Silverblade Paladin design call — pairing is READ-TIME VERIFIED, not detach-maintained.**
`equipped_to` is kept correct by 8 separate "zero it on death" sites; mirroring that for
`paired_with` would be 8 more chances to leave a dangling link granting permanent double strike.
Instead the link is stored one-sided on the soulbond permanent and the predicate re-checks the
partner is still on the battlefield under the same controller. That IS the literal oracle ("they
remain paired for as long as you control both"), needs zero detach sites, and makes re-pairing on
a later enter correct by construction.

### Verified integration traps (checked in the tree, not taken on trust)

1. **Do NOT model battle cry by reusing `attack_pump_matching_power`.** That field is OR-ed into
   the **Minotaur** archetype signature (`src/ai/DecisionProviders.cpp:9953`), so reusing it would
   silently route WhiteKnights to `MinotaurProvider` — the archetype-neutral-param misroute class
   that has already caught Mirrorwing, StompySurprise, Minotaur and Dragons. A new
   `battle_cry_power` field avoids it. **Corollary: `battle_cry_power` must NOT be added to any
   provider signature block either.**
2. **`KeywordFromString` (`src/cards/CardDatabase.cpp:418`) THROWS on an unknown keyword string.**
   So `"keywords": ["Battle Cry"]` in cards.json without the matching `Keyword::BattleCry`
   enumerator + parser line breaks DB load for **every deck in the repo**. Land the enum + parser
   first, or all three edits atomically.
3. **`ApplyAttackSelfPumps` is the correct shared apply site** — called in BOTH worlds at the same
   position (`src/core/GameEngine.cpp:629` executor, `src/ai/TurnSolver.cpp:31185` rollout), so
   executor and rollout stay lockstep and no `[fd-diverge]` can arise from the apply half.
4. **`TurnSolver::PendingAttackDamage` (the search's lethal projection) must also learn battle
   cry.** Neither Piledriver's nor Kragma's pump is projected there today; that precedent is not
   permission. Skipping it produces no mismatch flag — the search merely *under-rates attacking*,
   silently. Battle cry stacks and cross-pumps (team bonus `K*(A+T-1)`, not `K`), so the
   under-count is large on this deck's wide turns.
5. **The double-strike prefilter WRITE SITE is the silent-failure risk for BOTH double-strike
   cards.** `GatherBoardSources` (`src/core/SpellEffects.h:3704`) builds `bs.ds` with exactly
   `if (pp.grants_double_strike) { bs.ds.push_back(i); }`, and combat reads the **prefiltered**
   list (`src/ai/Combat.cpp:151-153`). Editing only `HasDoubleStrikeFromLords` and not this gate
   gives a feature that compiles, digests clean, and **never fires** — the "a cross-tab keyed on a
   struct FIELD must be checked at every WRITE site" lesson, and the "digest equality can mean
   BROKEN" lesson at once. Silverblade and Valiant Knight must merge into ONE widened predicate
   here, not two.
6. **The two double-strike cards MUST converge on ONE shared oracle.** Both agents reached this
   independently. There are three `ds` expressions — `src/ai/Combat.cpp:150-154` (execution),
   `src/ai/TurnSolver.cpp:6581-6585` (attack-tap discount) and `:6656-6660`
   (`PendingAttackDamage`, the projection that makes the search willing to PAY for the
   activation). Patching them per-card = six edits to three expressions and a near-certain miss.
   Build one `CreatureHasDoubleStrike(...)` folding printed keyword + lords + equipment +
   soulbond pair + `temp_double_strike`, on the **`CreatureHasLifelink` precedent**
   (`src/core/SpellEffects.h:2910`), and give each card one clause in it.
   Scryfall correction: **Valiant Knight is `{3}{W}` 3/4**, not the `{2}{W}{W}` 2/2 recall would
   have produced — wrong on both cost and body.
7. **Three more verified silent-failure sites for the activated grant** (all confirmed in-tree):
   * `SpendRepeatActivations`' mode→cost chain (`src/core/SpellEffects.h:16388-16401`) **ends in**
     `exile_opponent_top_cost`, so an unmapped mode reads a cost the card does not have and fires
     **zero times with no error**. The code comment at that exact site calls it "the class of bug
     the per-mode tables keep producing".
   * `PermAbilityTaps` (`src/core/Permanent.h:184`) must list the new mode, or the engine taps
     Valiant Knight on activation — blocking it on a summoning-sick or attacking body (CR 302.6
     restricts neither).
   * Omitting the `PendingAttackDamage` read compiles clean and passes every parity check; the
     search simply **never activates**. Dead but invisible.
8. **Token identity is sound for an `m_number`-keyed pair — verified, because two stale comments
   say otherwise.** `src/core/SpellEffects.h:9262` and `:11480` both assert "TOKENS all carry
   m_number 0". That is **not true of the token path this deck uses**: `CreateTokenFromProto`
   (`:4823`) assigns `state.next_token_number++` from a base of 1000 (`src/core/GameState.h:207`),
   and the attack-token site (`:12338`) goes through it. So Hero of Bladehold's and Adeline's
   tokens carry unique non-zero ids and can be paired unambiguously — but do not take the
   in-tree comments at face value while integrating. (Pre-existing doc drift, noted not fixed.)

**Why battle cry is not deferrable.** "Each *other* attacking creature gets +1/+0" against an
opponent that never blocks is a straight damage increase on every multi-attacker turn — exactly
the profile of this deck. 4 Accorder Paladin + 3 Hero of Bladehold means it is a core damage
source, not a corner case.

**Why double strike is not deferrable.** Against a passive opponent it *doubles* combat damage.

## Stage 4a — Provider routing (KNOWN ISSUE, must be fixed before anything is measured)

`SelectDecisionProvider` (`src/ai/DecisionProviders.cpp:9886-9899`) sets the `knights` signature
from the literal subtype string `"Knight"` across three cards — Knight Exemplar's
`subtypes_affected`, Worthy Knight's `cast_trigger_subtype`, Acclaimed Contender's
`etb_dig_subtypes`. **WhiteKnights carries all three**, so it will route to `KnightsProvider`
(which derives from `VialProvider`) — i.e. it will silently share a provider with `decks/Knights/`
and inherit `MTG_KNIGHTS_ORDER`, the user-reviewed Knights cast order adopted 2026-08-19.

Per the Stage 4a rule ("Every shipped deck OWNS a provider"; `--check` **fails** on two decks
sharing one provider), this is a failure to fix, not a judgement call. Planned:
`WhiteKnightsProvider : KnightsProvider` (derive from **what the deck actually routes to today**,
per the rule's explicit warning against deriving from a thematically-related deck's provider),
routed above the `knights` branch on a WhiteKnights-specific signature, with `Certificate()`
answered. An empty derivation is play-neutral by construction — verified by smoke `play-changed=0`.

Open question for the cast-order hook: `VialProvider::CastOrderRank` under `MTG_KNIGHTS_ORDER`
was reviewed and adopted for the *Knights* list. Whether it transfers to this list is a
measurement (5e), not an assumption — recorded below once run.

## RESUME HERE — latest state (2026-09-26, after the value-leaf run)

**Everything in the analyze-deck workflow is DONE. The deck ships a FITTED VALUE LEAF. What is left is
bookkeeping plus four user sign-offs.**

| item | state |
|---|---|
| Stages 1–6 + 6a | **DONE** (report at `## Stage 6 — Report`) |
| verify_deck.py | **RE-RUN under the adopted leaf — `GATE PASS`, exit 0, every gate green** (`logs/wk_verify2/verify.txt`). `card_costs` shows SKIP only because the run was `--no-network`; it passed 319/319 on the network run earlier the same day |
| Ledger tables | **REFRESHED under the adopted leaf.** One cell moved: §3 `d5 b20` 4.520 → **4.490** (slowest T8 → T7). Firing table and Stage 4 byte-identical |
| Value-leaf pipeline | **ALL 8 PHASES COMPLETE** (0/A/B/C/C.5/D/E/F), freeze intact `bb5d1038`, matrix 52/52 @400g, 11,288 rows, held-out RMSE 0.4236 |
| Search shape | **FITTED LEAF ADOPTED** (`721fe1b6`), superseding the leaf:none shape. `ladder: "single"` stays REJECTED |
| Reference bench | **10/10 EXACT after the user's re-play** — human 4.400 · search 4.400 · 0 short · 0 faster |
| Mulligan profile | **GENERATION STARTED 2026-09-26** (user: *"we may as well start generation"*) — the last pipeline stage, now unblocked because phase F derived its contract |
| Commits | `80ef7c34` deck+engine · `bb5d1038` lazy-leaf opt-out · `343366c3` leaf measurement · `6508fb37` references · `ce64976d` bench cache · `9bec6d2e` refline tool · `721fe1b6` leaf adoption. **Nothing pushed.** |

### The value leaf: adopted, and my earlier "no leaf" call was wrong on COST

`decks/WhiteKnights/WhiteKnights.value.json` now carries the fitted model (53,666 bytes) —
0.31–0.38x the leafless shape's units at d5, quality indistinguishable (0 better / 2 worse over 16,000
paired d5 games on two disjoint seed bases), **exactly 1.00x and byte-identical at d3**. Detail in
*Search shape* → Step 2b/2c/2d. `value_play` carries only `expected_buckets: 14`.

**Two traps in that run, both worth not re-learning:**
1. `phase_train` copies the LIVE sidecar and adds only `eval_model`, so an adopted `leaf: "none"`
   survives into the staged model and `AttachValueSidecar` swaps the fitted trees for `Constant()`.
   Move a shape sidecar aside before generating, and check the staged `value_play` before believing
   any result.
2. **Phase F must run AFTER adoption.** Before adoption it picked `d1 b3`; after, play settings became
   both cheapest (752 vs 1084 units/roll) and exact (rho 1.0000), so the answer flipped to *no
   override*. Running it in its normal position would have written a setting 1.44x costlier on the
   shipped deck at worse fidelity.

### Reference bench — the engine never loses to the human

`python3 scripts/ref_bench.py --deck whiteknights --log-root logs/wk_refbench` on the 10 committed
hand-played references (`references/WhiteKnights/`, commit `6508fb37`), each game's recorded mulligan
**forced** so the comparison isolates play:

* **FINAL, after the user's re-play: human 4.400 · search 4.400 · 0/10 shortfalls · 0 faster than the
  human · hand_mismatch 0/10.** All ten games now agree exactly. Cached for the viewer in
  `test/ref_bench.json` (stamped `src` 565f5a7397f8).
* The first bench read **human 4.700 · search 4.400**, 7 exact and 3 where the search won a turn
  earlier.

**Those three were audited for clairvoyance and all three were CLAIRVOYANCE-FREE**, so they were
handed back to the user rather than written off (their standing rule: *"if the decision is clairvoyant
in nature I will just leave it"*). The user re-played exactly those three, **confirmed independently
that none required knowing the library** — *"yes, they were possible without clairvoyance in this
case"* — and all three now win T4. Reprint any line with `python3 scripts/wk_ref_line.py <stem>`:

| game | the one decision | principle | after re-play |
|---|---|---|---|
| `claude_s1_gi0` | T2: **Worthy Knight (2/2)**, not Lightning Greaves | Greaves is `{2}` — the whole turn — and adds no power. Its shroud is inert vs a passive opponent and its haste only pays on a creature cast *that same turn* | T4 win; the only remaining divergence is the T4 cast (user **Knight Exemplar**, search **Acclaimed Contender**) and **both are lethal that turn**, so it does not separate them |
| `claude_s2_gi1` | T2: **Accorder Paladin (3/1)**, not Knight Exemplar | Exemplar buffs "**other** Knights", so into an empty board it is a textless 2/2. Play the lord after its targets exist | T4 win, and the line now matches the search **cast-for-cast** |
| `claude_s3_gi2` | T3: **two 1-drops**, not one 2-drop | Venerable Knight and Dauntless Bodyguard are both **2/1 for `{W}`** — 4 power for 2 mana vs Worthy Knight's 2 | T4 win, line matches the search **cast-for-cast** |

Caveat that ran the user's way on s1: Greaves-for-haste is exactly the line the search *cannot*
project (the deferred defect below), so it under-rated that plan and still won a turn faster.

**A reporting gap in `wk_ref_line.py`, found while auditing this and now fixed:** attackers are
recovered by diffing `tapped` flags across the MAIN_1 → COMBAT boundary, because the `ATTACK` action
carries only `damage`/`oppLife`. **A vigilant attacker never taps, so it was silently missing from
every attacker list** — and on this deck that is not an edge case: Adeline has vigilance *and* power
equal to your creature count, so she is routinely the largest attacker in the swing. On T4 of
`claude_s1_gi0` she supplied **8 of the 15 damage** while not appearing at all. The tool now names
vigilance candidates separately (untapped non-land creatures already on board before combat, excluding
anything cast that turn, which is summoning-sick); the trace schema carries no `attacking` flag, so a
candidate list is the honest ceiling here rather than a list that reads complete.

### Outstanding

1. ~~Re-run `verify_deck.py` under the adopted leaf.~~ **DONE — `GATE PASS`, exit 0.**
2. ~~Refresh the Stage-4/5/6 tables.~~ **DONE.** One cell moved, exactly the one predicted:
   `wk_lad_d5_b20` **4.520 → 4.490**, slowest **T8 → T7**. Everything else byte-identical, which
   Step 2d predicts (the leaf is byte-identical to leafless *and* to bare at d0 and d3, and every
   other table cell is a d0 or d3 configuration). The earlier note that this was "within seed noise"
   was too weak: it is a real, reproducible improvement at the deck's shipped depth — confirmed on the
   ladder's own seed base by `scripts/wk_ladder_arm_isolate.py` (4.4900/T7 across four arms, 300/300
   identical digests), and in the same direction as the 8,000-game paired screen.
3. **Four sign-offs**, none blocking: `expected_buckets = 14`; the Acclaimed Contender "legendary
   artifact" residue (the deck's only bracket note); `attack_tokens_require_self_attacking` as
   viewer-inert; and whether `Knights` + `WhiteKnights` both stay.
4. **Two deferred defects**, both written up:
   [haste-from-equipment-not-projected.md](haste-from-equipment-not-projected.md) (moves 5 GT tiers) and
   [lazy-leaf-corrupts-the-h5-reference.md](lazy-leaf-corrupts-the-h5-reference.md) (fleet-wide; default
   left armed deliberately, `MTG_VL_LAZY_LEAF=0` opts out).
5. **Play server** was left running on `http://localhost:8080`.

---

## Earlier resume block (2026-09-26, pre-value-leaf)

**Stages 1–5 are DONE. The bug found in Stage 5d is fixed and every number it invalidated has been
re-measured. What remains is the Stage 6 write-up and two user sign-offs.**

| item | state |
|---|---|
| Stages A–E (all four cards + the shared oracle + `bs.ds`) | **DONE**, built, 187/187 unit cases green |
| Stage 3 coverage | **CLEAN** — `missing: []`, all 17 cards `full` |
| Stage 4 profile | **REGENERATED 2026-09-26** on the fixed engine (`logs/wk_stage4/profile_run.txt`) |
| Stage 4a provider | **DONE** — `WhiteKnightsProvider`, audit exit 0, no collision with `Knights` |
| card_fields audit | **PASS** — `ok: true`, 420 checked, **0 mismatches, 0 unfetched**; all four new cards clear their hard fields |
| Firing evidence | **RE-RUN, all five mechanisms still FIRE**; win turns now measured with the profile attached |
| Stage 5b/5c/5c2 | **RE-RUN, PASS** (monotone depths, no budget starvation, tie-break does not bind) |
| Stage 5d sweep | **RE-RUN over base SEEDS — 20 games, 0 flags.** All five mechanisms now covered |
| Stage 5 verify_deck | **RE-RUN 2026-09-26, EVERY GATE PASSES** — coverage / card_fields / viewer / viewer_wiring / mismatch / play_invariants / claude_sweep. `card_costs` was red on 2 adventure-card faces and is now **PASS**: the audit had a bug, which is fixed (below) |
| Stage 6 report | **DONE** — see `## Stage 6 — Report (2026-09-26)` |
| Second-main prune | **MEASURED, not inert** — −0.0072 t, 6000 paired games, 42 earlier / 0 later |
| Search shape / value leaf | **`shape_probe` says `leaf: "none"` — do NOT generate a value leaf.** Adoption-grade multi-config screen run; see below |

### Exactly where this stands (2026-09-26, after the sweep re-run)

**Two more real bugs were found by the re-run sweep and BOTH ARE FIXED AND BUILT.** Unit tests
**190/190** green (187 + 3 new soulbond cases, the first of which fails on the old code by
construction). Both fixes verified by direct repro after rebuilding:

1. **soulbond Trigger B** now offers **only the creature that entered** — verified on the seed-7867
   pair-break repro, which pre-fix listed three candidates and defaulted to an illegal one; it now
   lists exactly `[Knight Exemplar]`, the entrant.
2. **Unexpectedly Absent** is no longer castable with no legal target — verified on the seed-7823
   repro: **0 plans** offer it on a board of two Plains, while all six legal creature casts are still
   offered (so the gate did not over-narrow).

**All three remaining items are now CLOSED (2026-09-26, later session).**

1. **`verify_deck.py` re-run — EVERY GATE PASSES.** `logs/wk_verify/verify_nonet.json`. The two
   previously-pending items came back green: `viewer` (the `attack_tokens_require_self_attacking`
   classification cleared the unclassified-param block — **still wants the user's sign-off, collected
   not blocked**) and `claude_sweep` (`0 unresolved flags`). `card_costs` was **fixed rather than
   deferred** — see *The `card_costs` gate was a tooling bug* below.

2. **BOTH fixes are autonomously play-neutral for WhiteKnights — proven, not assumed.** The firing
   harness was re-run on the post-fix binary and reproduces the pre-fix table **digit for digit**
   across **5,850 games** in 6 arms × 7 configurations (`logs/wk_firing/report_postfix2.txt` vs
   `report_postfix.txt`). The timestamps are what make this a real test rather than a re-read: the old
   table was written at **00:02**, `SpellEffects.h` was edited at **00:29**, `TurnSolver.cpp` at
   **00:34**, and the binary rebuilt at **00:39**.

   **And this is NOT the "equality means BROKEN" trap**, which is the first thing to rule out when a
   change lands with identical digests: the `nosb` strip arm still fires at **36/400**, so soulbond is
   demonstrably live and the Trigger-B restriction did not kill the mechanism on its way to being
   neutral. All five mechanisms still fire.

   **And the neutrality is EXPLAINED, not merely observed** — which is the difference between a result
   and a coincidence. The Stage-5d reachability analysis below ("Reachability, stated honestly") already
   derives why: autonomously, `DecisionProvider::SoulbondPartner` never declines while candidates
   exist, so **Trigger A always pairs immediately** and the wide candidate set Trigger B was mishandling
   only opens when a live pair **breaks** with other unpaired creatures present — and nothing in this
   deck's goldfish removes a creature. So the identical digests are the *predicted* outcome, and the
   prediction was in the ledger before the measurement was run. The claim should still not be
   overstated to "Trigger B verified autonomously": it was verified in human play, where it is trivially
   reachable, and shown not to disturb autonomous play, which is a different statement.

3. **Stage 6 report + 6a disclosure — written**, at `## Stage 6 — Report (2026-09-26)`.

**Work done beyond those three, because the box was free:**

* **The second-main prune was measured instead of disclosed as unmeasured** (−0.0072 t, 6000 paired
  games, 42 earlier / 0 later). It is the one item in the 6a table that is **not** inert.
* **`card_costs` root-caused and fixed** — an adventure-card face-selection bug in
  `audit_card_costs.py`, plus a misleading "likely custom/token" label on rate-limited fetches.
* **The `discard` / `retrace_discard` decision notes were fixed** (the last listed open item) and
  **proven digest-neutral** by re-running the full firing harness and diffing: identical.
* **The value-leaf question was answered by measurement**: `shape_probe` says `leaf: "none"`, so **no
  value leaf should be generated for this deck.** An adoption-grade multi-configuration screen
  followed, because one cell is not the bar this repo has used for that decision.

### Found but deliberately NOT fixed here

`docs/design/haste-from-equipment-not-projected.md` — the same-turn attack projection ignores haste
granted by an Equipment, so the search misses a **turn-4 lethal** on seed 7867 that a guided human
found, and misses it at d8/b3000 because the defect is in the projection rather than the search
effort. Diagnosed to the exact line (`TurnSolver.cpp:16588`). **Not fixed because Lightning Greaves is
in six shipping decks, five of them in ground truth** — the fix makes the search strictly better
informed, so win turns improve and five GT tiers move. A quality win with a real cost is the user's
call, not an agent's.

### Post-fix neutrality A/B — RUN, and the one "PLAY MOVED" is a LABEL, not play

`logs/wk_neutrality/report_final.txt`, 15 configs / 3,450 games per arm:

* **0 differ** on knights, knights2hg, kitty, kitty2hg, slivers, slivers2hg, goblins, goblins2hg,
  giants, fungus, melira. **Knights is 0/400 — which is the load-bearing row for the Hero fix**,
  since Knights' mainboard runs Adeline and the new param defaults false for her.
* **angels 13/300, angels2hg 13/150, minotaur 1/300, minotaur2hg 1/150** flagged PLAY MOVED.
* Liveness intact: LIVE-T 31 on knights2hg; LIVE-D 220/117/49/36 on kitty/kitty2hg/slivers/slivers2hg.

**Root-caused, and it is not a play change.** Two independent signals said so before I looked: the
win TURN is identical in every divergent game, and LIVE-T/LIVE-D show the *same* counts as NEW (so
the mover is shared engine code, not either cards.json mutation). The new binary is also
self-consistent (two runs, 0/300), so this is deterministic rather than budget noise.

Diffed a divergent game (Angels, seed 1004) action-by-action: **15 actions, 14 byte-identical, one
differing field —**

```
HEAD: (turn 4, ABILITY, Resplendent Angel, "team pump + haste")
NEW : (turn 4, ABILITY, Resplendent Angel, "self pump + lifelink")
```

`GameLogger::LogAbility` does `FoldStr(ability)` — **the ability's LABEL TEXT is folded into the play
digest.** So fixing the hardcoded `AIEngine.cpp` label moved the digest without moving play. It maps
exactly: Angels runs Resplendent Angel (ActivatePump **mode 3**), Minotaur runs Sethron (**mode 2**),
and every other deck in the harness has no mode-2/3 activation at all → 0.

**The old label was factually wrong**, which is why it was changed: mode 3 is a SELF pump granting
lifelink, and the code printed "team pump + haste" for it — a description of a different card. The
same hardcoding would have described Valiant Knight's double-strike grant as "team +1/+0 and haste".

**DECISION TAKEN (not blocking on it): keep the fix.** A mislabelled diagnostic event is precisely
what misdirects the next investigation — a sweep agent flagged it in those words. The cost is
**per-game DIGEST churn in ground truth for the angels and minotaur tiers, with win turns and
aggregates proven unchanged**. That is a clean, fully-explained rebaseline, not a regression. If the
user would rather not touch GT for a cosmetic string, the revert is one hunk in `src/ai/AIEngine.cpp`
and loses nothing but the correct label (the viewer-side fixes in `main.cpp` / `SpellEffects.h` do
**not** feed the digest and can stay either way).

**Lesson worth keeping:** this repo has a memory that "human-play fixes needn't move GT". The
converse also holds and is less obvious — a **diagnostic-label** fix DOES move GT, because the digest
folds the label string. Check `LogAbility` before assuming a log-text change is free.

### The ordered to-do list, post-compaction — **ALL SIX ITEMS COMPLETE (kept for the record)**

> **SUPERSEDED 2026-09-26.** Every item below is done; item 5's "`card_costs` stays red" was overtaken
> by fixing the audit bug that caused it. Read *Exactly where this stands* above for the current state.

1. **Finish the field-audit snapshot.** `python3 scripts/audit_card_fields.py --update` completed one
   pass and got **2 of the 4** new cards in: **Silverblade Paladin and Hero of Bladehold now PASS all
   hard fields (cost, P/T, types, keywords)** — which is the proof that the `battle cry` / `soulbond`
   entries in `MODELED_ELSEWHERE_KEYWORDS` work, since Scryfall reports those keywords and cards.json
   carries `keywords: []`. **Valiant Knight and Accorder Paladin are still UNFETCHED** (rate-limited);
   re-run `--update` (it fails closed, exit 1, which is correct). Their oracle_text divergence is
   advisory-only and expected — cards.json appends bracket notes.
2. **Regenerate the Stage-4 profile** — `python3 scripts/analyze_deck.py
   decks/WhiteKnights/WhiteKnights.cod --analyzer-seed 1001`. **Run it ALONE on the box** (CLAUDE.md:
   generation stages are strictly serial). The existing profile and every win turn in the Stage-4 and
   firing-evidence tables were measured pre-fix and are superseded.
3. **Re-run `scripts/wk_firing_evidence.sh`** on the fixed engine — the mechanism-fires verdicts will
   hold, but the win-turn deltas were inflated.
4. **Re-run the Stage-5d sweep fanned over base SEEDS** (e.g. 7801, 7802, … one agent per seed, ~15
   agents, Opus per CLAUDE.md). `--game-index` is inert in both the claude-play and single-deck
   benchmark paths, so the previous fan-out was 15 copies of one game. Then record
   `## Claude-play sweep` with `commit:` / `seeds:` / `games:` / `flags: N unresolved`.
5. **Re-run `verify_deck.py`** and clear or sign off what remains. Known non-WhiteKnights blocker:
   `card_costs` FAILs on **Brightcap Badger** and **Fungus Frolic** (adventure cards whose Scryfall
   cost is the combined `{3}{G} // {2}{G}`). Pre-existing, from the Fungus deck, **not caused by this
   work** — do NOT sign it off under WhiteKnights' approved deferrals, which would be claiming a
   sign-off for another deck.
6. **Stage 6 report + 6a disclosure.**

### Open items to carry forward

* The **`discard` decision's reply index** is into `options` (draw order), not `me.hand`
  (display-sorted), while its note says "hand index". Pre-existing; it cost a sweep agent 5 wrong
  discards. **Still open** — a one-line note fix in the emitter. The 2026-09-26 sweep agents were
  warned in their prompts instead, and one confirmed the behaviour matches the warning exactly.
* **`.claude/skills/claude-play.md` was misleading about `--game-index`** — **FIXED 2026-09-26.** The
  example no longer passes `gi`, and a warning block explains that `SetupGame(deck, seed)` makes the
  seed the whole story, with the 15-identical-games incident recorded as the reason.
* **`attach_host` was missing from the skill's decision-type list** — **FIXED 2026-09-26**, with an
  explicit warning that `-1` is not a universal pass: on `main_phase` it means "cast nothing", but on
  `attach_host` / `free_cast` / `target` it **declines** and the effect is lost. A sweep agent hit
  exactly that, declined a soulbond pairing by accident, and briefly read the result as "soulbond's
  double strike is missing" — a false negative manufactured by a doc gap.
* **Plan summaries under-describe two situations**, both raised by agents and both cosmetic:
  a pair of plans can share a `summary` naming two cards while only one actually casts both (the
  `drops` key is the only tell), and the `{X}` `Unexpectedly Absent` plans print neither X nor target,
  so a human cannot tell them apart. Also, `target` decision labels show **printed** P/T rather than
  lord-adjusted.
* **The soulbond partner ranking** (`SoulbondPartner`) sorts on effective power alone and can prefer a
  summoning-sick partner over an equally-powered one that could attack. A legitimate
  `heuristic-optimization` candidate, not a rules bug.
* **Acclaimed Contender's dig `heuristic_default`** pointed at a strictly worse legal option in one
  observed game (Worthy Knight over Hero of Bladehold). Same category.
* **Six byte-identical Valiant Knight plans** in the uncapped menu (pre-existing Site-9 machinery).
  Disclosed, not fixed.
* Two questions for the user, restated in the closing report: whether `Knights` and `WhiteKnights`
  both stay as separate shipping decks (assumed YES throughout), and sign-off on the Acclaimed
  Contender "legendary artifact" inert residue.

---

## Stage 6 — Report (2026-09-26)

### 1. Cards implemented this run

Four cards were missing at Stage 1; all four are now `full`, and coverage re-reports
`"missing": []`, 17/17.

| card | n | tier | what was built |
|---|---|---|---|
| Accorder Paladin | 4 | **2** | new `battle_cry_power` param + a 4th block in `ApplyAttackSelfPumps` + the `PendingAttackDamage` projection |
| Hero of Bladehold | 3 | **2** | shares `battle_cry_power`; reuses the `attack_creates_tokens` family; **plus two new gates that are bug fixes, not features** — `attack_tokens_per_opponent` (false here) and `attack_tokens_require_self_attacking` (true here) |
| Valiant Knight | 1 | **1 + 3** | lord clause is Tier 1 (`lord_effect`, Knight Exemplar's shape); the `{3}{W}{W}` activated grant is Tier 3 (`PermAbilityMode::GrantDoubleStrike` + `double_strike_grant_cost` + `Permanent::temp_double_strike`) |
| Silverblade Paladin | 1 | **3** | new `soulbond` + `Permanent::paired_with` (read-time verified, no detach sites) + `HasDoubleStrikeFromLords` + the `SoulbondPartner` provider hook + `attach_host` viewer reuse |

Two shared refactors rode along: one `CreatureHasDoubleStrike` oracle replacing three open-coded
expressions (Stage B, neutrality proven with a live control), and the `bs.ds` write predicate
(Stage E).

**Three bugs were found and fixed in the course of this run** — the first by the sweep's first pass,
the other two by its re-run:

| # | bug | rule | where | status |
|---|---|---|---|---|
| 1 | Hero of Bladehold's token trigger fired whether or not **Hero** attacked | trigger condition ("whenever *this creature* attacks") | shared `attack_creates_tokens` path | **FIXED** + regression case; worth ~0.03 t (below) |
| 2 | soulbond's **second** trigger offered every unpaired creature, not only the entrant | **CR 702.46b** | `src/core/SpellEffects.h` (`try_pair`) | **FIXED** + 3 unit cases, one failing on the old code by construction |
| 3 | Unexpectedly Absent was castable with **no legal target**, resolving as a no-op | **CR 601.2c** | `src/ai/TurnSolver.cpp` (`Targeting::NonlandPermanent` human fall-through) | **FIXED**; pre-existing shared machinery, not one of this deck's new cards |

Bug 1 and bug 2 are **mine**, introduced by this run's own work; bug 3 is pre-existing and was merely
found here. The `attack_tokens_per_opponent` gate is a fourth defect found in passing (Hero's flat
"create two" was being multiplied by opponent count, correct for Adeline and wrong for Hero) — inert
at one head, live in 2HG.

### 2. Mulligan profile

`decks/WhiteKnights/WhiteKnights.profile.json`, regenerated **on the fixed engine**
(`logs/wk_stage4/profile_run.txt`; the superseded pre-fix profile is kept alongside as
`profile.PREFIX.json` purely so the two can be diffed).

* Keep rule: `min_lands 1`, `max_lands 5`, `curve_check two_drop`, `stop_at 4`,
  `bottom_order count_first`, `required_pieces []`, `min_playable 0`.
* `hand_score_threshold: -1e+18` — i.e. **the score threshold is disabled**; the structural checks
  above are the whole keep rule. This is the default mulligan tier. **No exhaustive keep table has
  been generated** (no `.keepmodel.exhaustive.profile.json.gz`), which is correct: that is a separate
  later stage per `mulligan-profile.md`, and it must come after the value leaf.
* `vial_target_mv: 3` — Aether Vial's target charge level.
* Two diagnostics from the same run: **COST_NEUTRAL** (the reframe neither helps nor hurts — 4.445
  both arms, 200 g at d3) and **DISCARD_INERT** (400 games at d3 reached **zero** cleanup-shed
  decisions from either caller, so there is no discard policy to derive — consistent with a deck that
  empties its hand).

Card scores, and the three readings that are actually load-bearing:

| card | per-copy marginal score | reading |
|---|---|---|
| Accorder Paladin | **+0.357**, +0.001 | best in the deck, and it is the battle-cry carrier — battle cry is also the biggest measured mechanism (0.20 t). The two measurements agree, and they were produced independently |
| Sol Ring | +0.325 | ramp into the 4-drops |
| Venerable Knight | +0.220, **−0.080** | |
| Adeline, Resplendent Cathar | +0.213 | |
| Dauntless Bodyguard | +0.210, **−0.202** | |
| Lightning Greaves | +0.082 | |
| Benalish Marshal | +0.073 | |
| Worthy Knight | +0.071, **−0.125** | |
| Silverblade Paladin | +0.052 | matches its measured ~0.03 t as a 1-of |
| Knight Exemplar | −0.045 | |
| **Hero of Bladehold** | **−0.064** (was **+0.033** pre-fix) | **the sign flipped when bug 1 was fixed** |
| Aether Vial | −0.083 | |
| Acclaimed Contender | −0.115 | |
| Valiant Knight | −0.123 | matches its activation being nearly unaffordable (firing evidence, row 3) |
| Unexpectedly Absent | −0.229 | |
| Swords to Plowshares | **−0.257** | |

1. **Hero's sign flip is the most legible consequence of bug 1.** Under the bug the fit was crediting
   a 4-mana 3/3 with two free 1/1s on *every* attack the deck made, whoever made it. The honest
   reading is narrow and sufficient: **the pre-fix +0.033 was measuring a card the engine played
   wrong.** It is *not* a recommendation to cut Hero — a card score is a marginal fit against a
   passive goldfish that never blocks, and "makes two attacking bodies every combat" is worth far
   more against a real opponent with blockers than against one without.
2. **The two interaction spells scoring worst is a known goldfish bias, not a deckbuilding claim.**
   Swords to Plowshares at −0.257 and Unexpectedly Absent at −0.229 are near-dead cards *here*
   because the passive opponent presents no creatures and no nonland permanents to target — which is
   the same fact that bug 3 was hiding behind. Do not read these two rows as "cut the removal".
3. **Several second copies price negative** (Dauntless Bodyguard −0.202, Worthy Knight −0.125,
   Venerable Knight −0.080). That is a genuine, in-scope deckbuilding signal and the right follow-up
   is `deck-screening.md`, not a hand edit — and per the cut-ladder lesson it should test *adding* a
   third/fourth copy of an expensive-to-cut card too, not only cuts.

### 3. Win rate / average win turn

**Every game is won** in every configuration measured; the deck's clock, not its win rate, is the
metric here. Figures below are from the pooled batch in `scripts/wk_firing_evidence.sh` with the
deck's **profile attached** (`logs/wk_firing/report_postfix.txt`):

| config | games | avg win turn | slowest win |
|---|---|---|---|
| d0 | 600 | 4.735 | T7 |
| d3 b10 | 300 | 4.520 | T8 |
| d5 b20 | 300 | **4.490** | **T7** |
| d5 **b2000** | 150 | 4.507 | T7 |
| 2HG (life 30, 2 heads) | 200 | 5.275 | — |

**Monotone in depth and never worse deeper** (4.735 → 4.520 → 4.490), and the slowest-win column is
how that can be read without trusting a threshold: the worst game lands at T7–T8 against a `max_turns`
far above it.

**REFRESHED 2026-09-26 under the ADOPTED FITTED LEAF.** These are the numbers as the deck ships today.
Exactly **one** cell moved from the leafless figures: `d5 b20` **4.520 → 4.490, slowest T8 → T7**. All
15 firing rows, the d0 / d3 / d5-b2000 ladder rows, the 2HG row and the paired budget check came back
**byte-identical**.

*That pattern is predicted, not lucky.* The three-way sidecar comparison (Step 2d) found the fitted
leaf **byte-identical to both the leafless shape and the bare ladder at d0 and d3**, and the firing
cases are all d0 or d3 b10, so the firing table could not have moved. d5 is the only configuration
where the leaf changes play at all — and there it is *better*, which is the same direction the paired
8,000-game screen saw at 0.34–0.42x the units.

**`d5 b2000` reading 4.507 against `d5 b20`'s 4.490 is NOT a budget inversion.** The two cells are
different sample sizes (150 vs 300 games); on the 150 games they share, the paired check is **0
earlier, 0 later, 150 identical**. The gap is the extra 150 games in the b20 cell, not the budget.

**Two traps for anyone re-running `scripts/wk_firing_evidence.sh`:**
* **The script prints to STDOUT and writes no report file.** `logs/wk_firing/report_postfix.txt` is a
  hand-saved copy, so reading it after a fresh run gives the *previous* run's numbers. That is exactly
  how the `d5 b20` move was first mis-read as "no change" here — the file was 5 hours stale. Redirect
  the run and read what you redirected.
* Its `wk`/`wk_d3`/`wk_2hg` win turns (4.765 / 4.527 / 5.275) are the **Stage 4 baseline-speed
  figures**, on a different seed base (1001) from the ladder's (4401). They are unchanged by the leaf
  and Stage 4 needed no refresh.

**Deviation from the skill, stated plainly: item 3 asks for these from a regression-suite run, and
WhiteKnights is NOT in the regression suite.** The figures above come from a standalone pooled batch
instead. Adding the deck to `test/regression_cases.sh` would create GT keys and consume the shared
per-mode time budget, which is a user decision (§7).

### 4. Verification (Stage 5)

| gate | outcome |
|---|---|
| coverage | **PASS** — `missing: []`, 17/17 `full` |
| card_fields (Scryfall) | **PASS** — `ok: true`, 420 checked, **0 mismatches, 0 unfetched** |
| card_costs (Scryfall) | **PASS — the gate was fixed rather than signed off.** 319 costed cards, **all match**, 0 unchecked. See *The card_costs gate was a tooling bug* below |
| clause_ledger | **PASS** |
| viewer (5h decision surface) | **PASS** — no HARD MISS, no SELF-GUARD FAILURE, no DRIVER FAILURE |
| viewer_wiring | **PASS** — 4 decision types wired emitter+GUI, including the new `attach_host` |
| mismatch (`nonconv` / `fd-diverge`) | **PASS** — seeds 7001/7002 × 60 games, both arms completed, no divergence |
| play_invariants | **PASS** — 8 games / 116 decisions |
| claude_sweep | **PASS** — 20 games, **0 unresolved flags** |
| 5b/5c multi-depth + budget starvation | **PASS** — monotone; d5 at b20 vs b2000 is **150/150 identical**, paired by game index, so the suite budget does not constrain this deck |
| 5c2 horizon-honest tie-break | **NO SIGN AT THIS SAMPLE** — 24,000 games / 12,000 paired, 0 changed games. The lever never fires here; recorded as un-measurable, not as "harmless" |
| unit tests | **190/190 cases, 2,636,208 assertions** |

**Claude-play sweep (Stage 5d), the headline:** 20 games — 15 uniform seeds 7801–7815 plus 5
content-selected seeds — **20 of 20 tied the depth-5 search, 0 unresolved flags**, and it found bugs
2 and 3. Per-seed record: `logs/wk_sweep_seeds/RESULTS.md`.

Two things about the sweep are worth carrying forward as method, because both were failures of my
own design that the run exposed:

* **The first sweep fanned over `--game-index`, which does not vary the game.** `SetupGame(deck, seed)`
  makes the seed the whole story; `gi` only feeds `PopulateOpponentSpawns`. So that pass was 15 copies
  of one game reported as 15 games. The skill has been fixed.
* **A uniform seed sweep structurally cannot cover this deck's 1-ofs.** The deck kills on ~T4.5 and
  the sweep caps at `--max-turns 8`, so a singleton sitting at draw 10 is unreachable in *every*
  uniformly seeded game — more uniform seeds buy more of the same opening. `scripts/wk_seed_scan.sh`
  now selects seeds whose opening hand plus revealed draws actually contain a target card, and five
  extra agents ran those. **Win turns from a content-selected seed set are biased by construction and
  must never be quoted as the deck's speed** — hence §3 coming from the pooled batch instead.

**Firing evidence — all five new mechanisms proven to FIRE**, which is the precondition for reading
any of the A/Bs above (the "digest equality can mean BROKEN" rule): strip exactly one mechanism's
param and compare per-game play digests on this deck. See the Firing evidence table below; every row
is nonzero in at least one configuration, and the two that are nonzero *only* in 2HG or only rarely
are explained there rather than waved through.

### 5. Encoded heuristics & assumptions disclosure (Stage 6a)

Compiled by reading the tree, not from memory — `scripts/wk_stage6_disclosure.py` prints sources 2
and 3 verbatim from `cards.json` and the profile, and source 1/3 were read out of
`GoldFishRunner.cpp` and `DecisionProviders.h`.

| assumption / heuristic | source | classification | what it costs / why it is safe |
|---|---|---|---|
| Single **passive** opponent: never blocks, never casts, never gains or prevents life | global engine | **Modeling assumption** | The biggest one by far for this deck. It **overstates** an all-out attacker (no blockers, no sweeper) and **understates** interaction — which is exactly why Swords (−0.257) and Unexpectedly Absent (−0.229) price near-dead. Unfixable by heuristics; read every card score with it in mind |
| **Clairvoyant search** over a known library (deterministic shuffle) | global engine | **Modeling assumption** | The search can see the library order, so it sequences better than a real player. Systematically optimistic on the clock |
| **First main only** — `DeckUsesSecondMain` is **false** for this deck | `GoldFishRunner.cpp:56` | **Pruning** | Verified from source: none of the nine trigger params (`spectacle_cost`, `pod_mv_delta`, `convoke`, `devour`, `lifegain_to_loss`, `hinata_cost_reducer`, `combat_damage_puts_subtype_from_hand`, `attack_draw_cards`, `attack_trigger_impulse_exile`, `combat_damage_free_cast`, `combat_damage_tokens_per_damage`, `attack_dig_attach_count`) is set by any card here. **Flagged as the one I would expect the user to question**, and it is the one item in this table that is **NOT inert**: measured at **−0.0072 turns**, 6000 paired games, **42 earlier / 0 later**. The predicate's own comment names "the Utvara/Adeline attack-created-token question" as the case it is unsure about, and this deck runs **both Adeline and Hero of Bladehold**, whose tokens arrive tapped and attacking. Full result in *Second main — measured* below |
| Deck-specific decision hooks | `WhiteKnightsProvider` | **NONE — empty derivation** | `WhiteKnightsProvider : KnightsProvider : VialProvider : DeckProvider`. The two Knight classes override **nothing but `Name()`**. The only inherited deck-specific judgement in force is `VialProvider::CastOrderRank` / `CastOrderTierName` — the **USER-reviewed** `MTG_KNIGHTS_ORDER` cast order (cast-trigger watcher before the tribe; the gated ETB digger early when its board condition already holds, late otherwise). Everything else is root default, i.e. pure search |
| Provider exists only to satisfy the one-provider-per-deck rule | `DecisionProviders.h:484` | **Correctness shortcut (naming)** | Both lists trip the `knights` signature, so without it they would share a provider and `provider_audit.py --check` would fail. Because it is an empty derivation, a **misroute between these two is play-neutral** — there is no hook to lose. That stops being true the moment either list earns a measured hook |
| `Certificate()` = **NotAssessed** | `DecisionProviders.h:487` | **Disclosed non-assessment** | No "provably winless this turn" bound is claimed, and this list is *harder* to bound than Knights: Aether Vial puts creatures onto the battlefield at instant speed, Valiant Knight can double the whole team's damage for mana already counted, soulbond doubles one more body free, and Lightning Greaves grants haste so damage is not deferred a turn |
| Acclaimed Contender's dig cannot express **"legendary artifact"** | `cards.json` bracket note (the deck's **only** one) | **Card-modeling simplification — PROVISIONAL deferral** | `etb_dig_subtypes` is a SUBTYPE filter; "legendary artifact" is a supertype+type pair. Knight/Aura/Equipment are all expressible and filtered faithfully. Inert because **no deck running this card holds a legendary artifact** — WhiteKnights' Sol Ring and Aether Vial are both plain `Artifact`, and `Knights` holds none. Needs sign-off (§6) |
| `attack_tokens_per_opponent: false` on Hero | `cards.json` | **Correctness gate** | Hero's "create two" is a flat count with no "for each opponent" clause. Defaults **true** so Adeline and every existing deck stay byte-identical |
| `attack_tokens_require_self_attacking: true` on Hero | `cards.json` | **Correctness gate** | "Whenever *this creature* attacks". Defaults false, so Adeline (player-scoped) is unchanged. Classified as viewer-**inert** in `audit_viewer_decisions.py` because it is a trigger *condition* transcribed from oracle text, not a choice a human could make — **needs sign-off (§6)** |
| Valiant Knight `team_pump_power: 0` | `cards.json` | **Faithful** | The activation grants double strike only, with no power pump. Called out because a zero is easy to mistake for an unset field |
| Aether Vial charge policy | `GenericProvider::WantVialCharge` (root default since 2026-08-18) | **Root default, not an archetype opt-in** | The archetype opt-in this class once had is what silently lost other decks their Vial; it is now the root |
| Play-viewer auto-resolved decisions | 2c-ter / 5h audit | **NONE** | **Viewer-ready: no card choice in this deck is silently heuristic-resolved.** All four decision types the new cards create are surfaced emitter+GUI, `attach_host` included |

#### Second main — measured, and it is NOT inert

Because this deck is the exact case the whitelist's own comment is unsure about, the
`MTG_FORCE_USES_M2` arm was run against the default rather than left as a caveat.
`bash scripts/wk_second_main_ab.sh` → `logs/wk_m2/report.txt`. Both arms ride **one** pooled batch
(the arm is a per-job manifest `flags` entry, so no `--cards-json` split is needed), and the control
arm pins `MTG_FORCE_USES_M2: false` explicitly rather than relying on the default.

| config | games | wt (single main, shipped) | wt (second main on) | Δ | paired play |
|---|---|---|---|---|---|
| d0 | 600 | 4.7350 | 4.7300 | −0.0050 | 66 differ, **3 earlier, 0 later** |
| **d0** | **6000** | **4.7305** | **4.7233** | **−0.0072** | 557 differ, **42 earlier, 0 LATER** |
| d3 b10 | 300 | 4.5233 | 4.5133 | −0.0100 | 58 differ, 4 earlier, **1 later** |
| d5 b20 | 300 | 4.5200 | 4.5133 | −0.0067 | 58 differ, 4 earlier, **2 later** |

**The 6000-game d0 row is decisive and it refutes the "inert" reading.** 42 games win a full turn
earlier and **not one of 6000 wins later** — a 42–0 split is a sign test at p ≈ 2×10⁻¹³, so this is
not a sampling artifact. Play differs in **9.3%** of games. The first 600-game pass gave 3–0 and was
*not* resolvable on its own (p = 0.125); the honest response to "too small to call" on a
budget-free configuration is more pairs, not a hedge, which is why the larger arm exists.

Three things follow, and they are not the same thing:

1. **The prune is not free, and Stage 6a must not call it inert.** The measured cost to this deck is
   **~0.007 turns**. That is small, but it is a real one-sided loss rather than an unmeasured risk.
2. **It is still very probably the right prune**, which is why this is a disclosure and not a bug.
   Opening a second main makes **every turn solve twice**; the blunt arm measured **4.25x** at d1/b3
   on another deck. Paying a multiple of runtime for 0.007 turns is a bad trade, and the decision is
   the user's, not an agent's. The point of measuring was to replace "we think this is inert" with
   "this costs 0.007 turns", and that is now done.
3. **The two budgeted rows show the cost side directly** — 1 and 2 games win *later* with the second
   main on, where the budget-free row has none. That is the doubled solve competing for a fixed
   10/20 ms budget, i.e. the extra phase paying for itself unevenly once search effort is finite.
   It is also a caution against reading the budgeted deltas as the effect size: they are contaminated
   by wall clock, which is exactly why the load-bearing row carries no budget.

**What this does NOT show.** It does not identify *which* line the second main buys. The plausible
candidate is the one the whitelist comment itself raises — Adeline and Hero make tokens that arrive
tapped and attacking, so a post-combat main can deploy a creature that pre-combat deployment would
have had to choose against — but no game was replayed to confirm that mechanism, and per this
ledger's own rule a mechanism invented after the numbers explains them rather than extending them.
Naming the line would need a divergent-game replay.

Two heuristic-quality observations from the sweep are recorded as `heuristic-optimization` candidates
rather than defects, because neither is a rules error: **`SoulbondPartner` ranks on effective power
alone** and so can prefer a summoning-sick partner over an equally-powered one that could attack; and
**Acclaimed Contender's dig `heuristic_default`** once pointed at Worthy Knight when Hero of Bladehold
was also legal and strictly better.

### 6. Accepted deferrals

**One candidate, PROVISIONAL — not yet approved**, per the rule that an unanswered deferral is never
an approved one:

> `[PARTIAL: 'legendary artifact' is a supertype+type pair, which etb_dig_subtypes (a SUBTYPE filter)
> cannot express; Knight/Aura/Equipment are all expressible and are filtered faithfully. WHY inert: no
> deck running this card holds a legendary artifact -- WhiteKnights' Sol Ring and Aether Vial are both
> plain 'Artifact', and Knights holds none either. The dug card enters hand and is cast a later turn
> (no same-turn re-solve).]`

That is the **only** bracket note on any of this deck's 17 distinct cards. Both of the Stage-1 notes
that were reclassified were **fixed, not deferred** — one described a clause that does not exist
(a fabrication) and the other justified a narrowing with a claim that is false for this deck.

#### The `card_costs` gate was a tooling bug, and is now FIXED (not deferred, not signed off)

The earlier position in this ledger was that the gate stays red because Brightcap Badger and Fungus
Frolic are another deck's problem and signing them off under WhiteKnights' deferrals would be
claiming a sign-off that is not ours to give. That reasoning was right, but it stopped one step too
early: **the card data was never wrong — the audit was.**

`audit_card_fields.py` already reconciles exactly these two entries through its allowlist, with the
correct adventure-card reasoning (CR 715). `audit_card_costs.py` did not, and the cause is one
condition. It selected a name-matched Scryfall face only `if not sf_cost` — i.e. only when the
top-level `mana_cost` was empty. Verified against live Scryfall:

| card | top-level `mana_cost` | `card_faces` |
|---|---|---|
| Brightcap Badger | `"{3}{G} // {2}{G}"` (**non-empty**) | Brightcap Badger `{3}{G}`, Fungus Frolic `{2}{G}` |
| Kaldring, the Rimestaff | `""` (empty) | Jorn `{2}{G}`, Kaldring `{1}{U}{B}` |

So a **modal DFC** (empty top-level cost) took the face branch and worked, while an **adventure card**
(populated *combined* top-level cost) skipped it and had each face diffed against the combined string.
Both faces were therefore guaranteed to mismatch forever, on correct data.

Fixed by preferring a name-matched face whenever one exists, regardless of the top-level cost. This
is **not** a suppression list: a name-matched face is simply the more specific truth, so no real
miscost becomes invisible. Modal DFCs are unaffected (they already took this branch).

**Result: `card_costs` now PASSES — 319 costed cards, all match.** No deferral is needed, and the two
Fungus-deck entries are correct as authored.

While there, a second reporting bug in the same tool: every unfetched card was printed under
*"NOT RESOLVED -- likely custom/token cards"*. On a rate-limited run that is a **wrong diagnosis
presented as an observation** — one pass labelled Lightning Bolt, Monastery Swiftspear, Goblin Guide
and Eidolon of the Great Revel "likely custom". The report now splits a genuine 404 (**NOT ON
SCRYFALL** — really is custom/token) from any other failure (**UNCHECKED** — we failed to ask, so the
cards were never compared and nothing about them was learned).

### 7. Suggested next steps

1. **The deferred haste-projection fix** — `docs/design/haste-from-equipment-not-projected.md`. The
   same-turn attack projection ignores haste granted by an **Equipment**, so the search misses a
   turn-4 lethal on seed 7867 that a guided human found, and misses it at **d8/b3000** because the
   defect is in the projection rather than the search effort. Diagnosed to `TurnSolver.cpp:16588`.
   Not fixed here because Lightning Greaves is in **six** shipping decks, **five in ground truth** —
   the fix makes the search strictly better informed, so win turns improve and five GT tiers move.
   A quality win with a real cost is the user's call. The same doc notes the adjacent `ds` narrowing
   (equipment-granted double strike is missed in the same projection) which should ride the same change.
2. **Whether to add WhiteKnights to the regression suite** (§3). Without it there is no GT tier for
   this deck and no protection against a future engine change silently regressing it; with it, it
   consumes shared per-mode budget. Not an agent's call.
3. **`ladder: "single"` — a measured TRADE awaiting a ruling.** Cheaper at every configuration
   (0.66x–0.16x units) but 7 games worse at d3b20 (z −2.6). Rejected here, and I recommend keeping it
   rejected; see *Search shape* above for the full table.
4. **Mulligan generation is the next pipeline stage and is now unblocked.** Its labeller setting has
   been **derived** (see *Mulligan-generation setting* above): the answer is **no override** —
   generate at play settings, d5/b20, which is exact by construction and which the absent
   `mull_gen_*` keys already produce by fall-through. What remains before generating is a **frozen
   commit** (Rule 0 of `mulligan-profile.md`; this working tree is uncommitted) and the user's
   `expected_buckets` ruling, which is not an agent's to make.
5. **The negative second copies** (§2.3) — a `deck-screening.md` job, which should test third/fourth
   copies as well as cuts.
6. **Plan-summary under-description** (cosmetic, both raised by sweep agents): two plans can share a
   `summary` naming the same two cards while only one actually casts both — the `drops` key is the
   only tell — and the `{X}` Unexpectedly Absent plans print neither X nor target.

#### Closed here: the `discard` decision note (was item 4)

The recorded open item said the reply index is "into `options`, not `me.hand`". Reading the code, the
precise situation is slightly different and worth getting right, because the imprecision is the whole
hazard:

* The reply is matched against `hand_indices` (`src/main.cpp`, the discard chooser), which is exactly
  what is emitted as `options[].index`. So the reply is the **`index` field VALUE**, not a position in
  the `options` array. Those two coincide only when the indices happen to be contiguous `0..n-1`,
  which for a *cleanup* discard they usually are — which is why it looks like a position and works
  most of the time.
* `me.hand` in the same JSON is sorted **alphabetically by name** (`std::sort` on the card name), while
  the indices are the engine's **draw order**. So counting positions in the displayed hand is wrong.
* **The trap is that a wrong index does not error.** The chooser silently substitutes the heuristic
  pick, so the mistake surfaces as an unexplained discard several turns later. That is how it cost a
  sweep agent five wrong discards.
* `retrace_discard` has the same note and is **strictly worse**: its options are only the *lands* in
  hand, so the indices are never contiguous and a position-count is essentially always wrong.

Both notes now state the reply is the `index` value, that it is not a position in either list, and
that an invalid index is silently replaced. **Proven play-neutral**: the full firing harness (5,850
games, 6 arms, 7 configurations) is byte-identical before and after the change. That check was run
rather than assumed, because this ledger already records the converse surprise — a *diagnostic-label*
fix DID move ground truth, since `GameLogger::LogAbility` folds the label string into the play digest.
These two notes are emitted only on the human-play decision path and never reach the digest.

---

## Integration work plan (historical — all stages now complete)

| stage | what | status |
|---|---|---|
| A | battle cry (Accorder Paladin, Hero of Bladehold) + the `attack_tokens_per_opponent` gate | **DONE** — 7 unit cases / 64 assertions |
| B | one shared `CreatureHasDoubleStrike` oracle replacing three open-coded expressions | **DONE** — neutrality proven with a live control |
| C | Valiant Knight (lord clause + the activated team double-strike grant) | **DONE** — 5 unit cases; coverage now clean for it |
| D | Silverblade Paladin (soulbond) | **NOT STARTED — resume here** |
| E | widen the `GatherBoardSources` `bs.ds` write predicate | **NOT NEEDED** — see below |

### Verification already banked (do not redo)

* **`scripts/wk_engine_neutrality.sh`** — HEAD (worktree build) vs working tree, per-game play
  digests, 15 configs / 3,200 games at shipped settings: **0 games differ.** Two liveness arms,
  because equality can mean BROKEN:
  * LIVE-T (Adeline's heads gate flipped): 31/200 on `knights2hg` — nonzero on exactly the one row
    where a per-opponent token count is observable, zero on every deck not running Adeline.
  * LIVE-D (every double-strike grant stripped): 220/300 `kitty`, 117/150 `kitty2hg`, 49/300
    `slivers`, 36/150 `slivers2hg`, zero elsewhere. **Only two decks in the repo contain a
    double-strike source at all** (KittyEquipment's Kor Duelist + Balan for the two Equipment
    paths, slivers_vial's Thrumming Hivepool for the lord path and hence the `bs.ds` prefilter), so
    a Stage-B run without those two would have been vacuous. Report: `logs/wk_neutrality/report.txt`.
* **`./build/Release/mtg-test`** — 173 pre-existing cases still green, plus the two new files
  (`test/unit/test_battle_cry.cpp`, `test/unit/test_double_strike_grants.cpp`).

### Why Stage E is NOT needed (the plan was wrong about this, deliberately re-checked)

The original plan said `src/core/SpellEffects.h`'s `if (pp.grants_double_strike) { bs.ds.push_back(i); }`
must be widened or the new grants "build, digest clean, and never fire". That assumed both new
grants would be implemented as `grants_double_strike` LORDS. Neither is:

* Valiant Knight's activation sets `Permanent::temp_double_strike` — a property of the **creature**,
  not of a granting permanent. `CreatureHasDoubleStrike` reads it directly, **before** consulting
  the lord list.
* `bs.ds` is only ever passed to `HasDoubleStrikeFromLords`, which only inspects
  `grants_double_strike`. So the list stays exactly as correct as it was, and an empty one is still
  a valid proof about the lord scan alone.
* Verified there is **no early-out on an empty `bs.ds`** at any of the three call sites — each
  calls `CreatureHasDoubleStrike` unconditionally — so an empty list cannot skip the new checks.

The unit test `"the shared oracle still answers for the PRE-EXISTING double-strike sources"` pins
both halves of this (`bs.ds.size() == 1` for the Hivepool, and the oracle answering with *and*
without the prefilter). **Stage D must re-check the same question for soulbond** — if the pairing is
modelled as a `Permanent::paired_with` field read by the oracle, Stage E stays unnecessary; if it is
ever routed through a lord param, the predicate must be widened.

### Stage A — battle cry (Accorder Paladin + Hero of Bladehold share it)
1. `src/cards/CardDatabase.cpp` `BuildParamsFromJson` (~line 847, beside `attack_creates_tokens`):
   read `battle_cry_power` (default 0) and `attack_tokens_per_opponent` (**default true**).
2. New `inline void ApplyBattleCry(GameState&, int controller, const std::vector<int>& atk_idx)` in
   `src/core/SpellEffects.h`, placed after `ApplyAttackSelfPumps` (ends line 10481). Two passes:
   sum `battle_cry_power` over the controller's **attacking** sources into `bc_total` (early-return
   when 0 → every other deck pays nothing); then add `bc_total - own_bc` to each controller-owned
   attacker's `temp_power_bonus`. The `- own_bc` term IS the "each OTHER" self-exclusion and
   generalises to multiple copies without a nested loop. Bounds-check `idx` like its neighbours do.
3. Call it in **both** worlds, immediately after the existing `ApplyAttackSelfPumps` call:
   `src/core/GameEngine.cpp:629` (executor) and `src/ai/TurnSolver.cpp:31185` (rollout). Both or
   neither — a one-sided call is an `[fd-diverge]` generator.
   **Ordering is load-bearing and already correct at these sites:** they sit AFTER
   `FireAttackCreateTokens` widens the attacker list, so Hero's two Soldiers and Adeline's Human
   receive the pump. That is the CR 603.3b trigger order the player would always choose (more power
   on more bodies is strictly dominant vs an opponent that never blocks).
4. `src/ai/TurnSolver.cpp` `PendingAttackDamage` (line 6636) — **the projection; do not skip.**
   Retain per-attacker `battle_cry_power` and the already-computed `ds` flag (lines 6656-6660) in
   parallel vectors while the main loop builds, accumulating `bc_total`; then a second pass (needed
   for the same reason `exalted_bonus` needs one — it depends on the final attacker count) adds
   `(bc_total - own_bc) * (ds ? 2 : 1)`, plus `bc_total` x the token count from the block at
   6702-6717. Skipping this raises **no flag**: the search silently under-rates attacking.
5. `src/core/SpellEffects.h:12325` and `src/ai/TurnSolver.cpp:6714` — gate the
   `* gamesetup::OpponentHeads()` multiply on `attack_tokens_per_opponent`.
6. cards.json entries for **Accorder Paladin** (`{1}{W}` 3/1, `battle_cry_power: 1`) and **Hero of
   Bladehold** (`{2}{W}{W}` 3/4, `battle_cry_power: 1`, `attack_creates_tokens: 2`,
   `attack_tokens_per_opponent: false`, token 1/1 `["Soldier"]`). **`keywords: []` on both** — see
   the keyword-mask constraint below.
7. `scripts/audit_card_fields.py` — add `"battle cry"` to `MODELED_ELSEWHERE_KEYWORDS`, else the
   HARD keyword diff (`local=[] scryfall=['battle cry']`) fails `verify_deck.py`'s `card_fields`.
8. `scripts/audit_viewer_decisions.py` — add `battle_cry_power` to `INERT_PARAMS` (automatic attack
   trigger; no target/mode/amount). The auditor's self-guard **hard-fails** on an unlisted
   choice-bearing param, so `verify_deck.py` will not go green without it.

### Stage B — the shared double-strike oracle (must precede C and D)
Replace the three duplicated `ds` expressions with ONE
`CreatureHasDoubleStrike(const Permanent&, const GameState&, const std::vector<int>* ds_idx)` in
`src/core/SpellEffects.h`, on the **`CreatureHasLifelink` precedent (line 2910)**, folding: printed
keyword + `HasDoubleStrikeFromLords` + `HasDoubleStrikeFromEquipment` + (new) `temp_double_strike`
+ (new) soulbond pair. Call sites: `src/ai/Combat.cpp:150-154`,
`src/ai/TurnSolver.cpp:6581-6585`, `:6656-6660`.
*(The other five `ds` computations — TurnSolver 6968, 7192, 16532, 16901, 17375 — take a
`CardDefinition`/`Card` with no `Permanent` and correctly stay as they are.)*

### Stage C — Valiant Knight — **DONE, and NOT by the route the plan specified**

Card verified on Scryfall: `{3}{W}` 3/4 Human Knight, "Other Knights you control get +1/+1." +
"{3}{W}{W}: Knights you control gain double strike until end of turn."

Lord clause: Tier 1, Knight Exemplar's shape verbatim (`lord_effect`, `subtypes_affected: Knight`,
+1/+1, `lord_excludes_self: true`).

**The activated grant rides `Action::Kind::ActivatePump` mode 2, NOT a new `PermAbilityMode`.**
The plan called for the Heliod `lifelink_grant_cost` pipeline. That was the wrong precedent, and the
right one was already in the tree: **mode 2 is Sethron, "{2}{B/R}: Minotaurs you control get +1/+0
and gain menace and haste until end of turn"** — a mana-costed, subtype-filtered, until-EOT **team
keyword grant**, which is Valiant Knight's exact card shape minus the power half. So the
implementation is `team_pump_cost` + `team_pump_subtypes` + `team_pump_power: 0` + a new
`team_pump_grants_double_strike` rider, exactly paralleling the existing `team_pump_grants_haste`.

Why this matters rather than being a tidiness point — it **deletes all four of the silent-failure
sites the plan warned about**, because no new enum value exists:
* no `PermAbilityMode` ordinal, so nothing touches the persisted bit index (`1u << int(mode)`);
* no `PermAbilityTaps` entry, no `SpendRepeatActivations` mode→cost chain entry (the one whose
  in-code comment names this as a recurring bug class), no `PermAbilityLabel` case.

Two things it does NOT avoid, both done:
* **The `temp_double_strike` bookkeeping tax — 9 sites, all applied:** `Permanent.h` field;
  `CreatureHasDoubleStrike` read; `ApplyActivatePump` write; `FungibilityKey` bit (8192);
  **two** encode strings (`/ds` and `D`); the fold-refusal predicate + its census label;
  `SimulateEndAndStartNextTurn` reset; `GameEngine::CleanupStep` reset; the transposition `Fold`;
  `dominance::AtCleanBoundary`; `ManaPayment.cpp`'s parity checker.
  * **`sizeof(Permanent)` did NOT change** — the bool fit existing padding, so the
    `static_assert(sizeof(Permanent) == 320)` did **not** fire. That guard is a tripwire for LAYOUT
    changes, not a census of state; a classification note was added to `Dominance.h` anyway, saying
    exactly that, so the unchanged number is not read as "nothing to classify".
  * While adding the field beside it, found `temp_lifelink` had been **missing from
    `ManaPayment.cpp`'s parity checker** since it was introduced (2026-09-08). Added both. Neither
    can be set by a payment path, so this only makes a silent divergence loud.
* **K capped at 1** for a keyword-only grant (`team_pump_power <= 0`): the grant is idempotent, so
  K>1 is provably a no-op — wasted search branches *and* a guaranteed-no-op entry in the human-play
  plan menu, which is the Wirewood Lodge phantom-option rule. Sethron is untouched (its
  `team_pump_power` is 1, so its activations genuinely stack). Pinned by a unit test.

**Two pre-existing defects found and fixed while wiring this:**
1. `src/main.cpp`'s ActivatePump label printed mode 2 as `"team +<K>/+0 and haste"` — hardcoded.
   `K` is the activation COUNT, not the power (it only ever coincided because Sethron's
   `team_pump_power` is 1), and the rider is no longer always haste. Valiant Knight's activation
   would have been shown to a human as "team +1/+0 and haste" — a description of a different card.
   Now built from the params. Same class of gap as the mode-3 branch's own comment warns about.
2. `ApplyActivatePump`'s play-event text had the same hardcoded `"Minotaurs +N/+0 and haste"`.
3. `audit_viewer_decisions.py`: `team_pump_cost` sits in `DEFERRED_PARAMS` under a **2026-07-19
   user sign-off that is about Lathliss's pump AMOUNT** being search-resolved. `DEFERRED_PARAMS` is
   keyed by param NAME, so Valiant Knight would have silently inherited a deferral it was never
   granted — the same inherited-note trap that produced the fabricated Worthy Knight bracket note.
   The entry is now explicitly scoped to the combat-converter consumer, and it is recorded that
   cards on the searched `ActivatePump` path are genuinely **surfaced** (both riders map to
   `main_phase`), not relying on that deferral. For Valiant Knight there is no amount to defer at
   all.

### Stage D — Silverblade Paladin (soulbond) — **DONE, as planned**

Card verified on Scryfall: `{1}{W}{W}` 2/2 Human Knight, Soulbond + "As long as this creature is
paired with another creature, both creatures have double strike."

Implemented exactly as the plan specified, and the plan was right here:
* `soulbond` + `soulbond_grants_double_strike` params; `Permanent::paired_with` = the partner's
  `card.m_number`, 0 = unpaired, stored on the soulbond side only (the `equipped_to` pattern).
* **READ-TIME VERIFIED** via `SoulbondPartnerIndex`, so there are **zero detach sites**. This is
  not just cheaper than maintaining the link at 8 death sites — it gets the rules right for free:
  CR 702.46b breaks the pair when you stop controlling either half and does not re-form it, and
  soulbond only triggers "when either enters", so a verified-false read legitimately leaves the
  Paladin able to pair with the NEXT creature to enter. `m_number` is unique per copy and never
  reused, so a stale number cannot re-resolve onto a different creature. Pinned by a unit test that
  erases the partner from the battlefield and checks the stale number is still present but resolves
  to nothing.
* `SoulbondNumberIsClaimed` enforces "at most one pair per creature" (CR 702.46b) — unreachable
  with one copy, load-bearing with two. Unit-tested with two Paladins.
* Both triggers in `FireEtbWatchers`, gated on a soulbond permanent being in play. Trigger B (a
  creature enters while OUR Paladin is unpaired) is the half that is easy to omit and it is the one
  that matters most: on a 2-drop in a deck of 1- and 2-drops, the Paladin usually lands *first*.
* Viewer: reuses the existing `attach_host` board-click, decline arm (`-1`) for the printed
  "you may pair". Only a new call site was needed.
* Provider hook `SoulbondPartner` — highest effective power, ties to lower card number, reading
  power through **`DynamicBasePower`** so Adeline (printed 0/4, power = creature count) is not
  priced at 0 and silently avoided.
* `paired_with` folded into `FungibilityKey`, the transposition `Fold` and `dominance::Build()`,
  **all three nonzero-gated**. The `FungibilityKey` gate is not a micro-optimisation: an
  unconditional `Mix` would change the hash VALUE for every permanent in every deck, and those
  values reach disk via the memo/keep artifacts, so byte-identity would break for no reason play
  requires.

**The two new fields on this card pair are classified OPPOSITELY, which is the whole hazard:**
`temp_double_strike` is until-EOT → **boundary assertion**, in `AtCleanBoundary`, cleared at both
cleanup sites. `paired_with` **survives cleanup** → **exact-match field**, folded into `Build()`,
and deliberately **absent** from `AtCleanBoundary`. Putting `paired_with` in the until-EOT family
would have refused every dominance comparison from the turn a Paladin pairs onward; clearing it at
cleanup would have ended the pair every turn. Both directions are unit-tested.

`sizeof(Permanent)` 320 → **328**, and the `static_assert` **fired as designed** — unlike Stage C's
bool, which slipped into padding without tripping it. Both cases are now recorded in `Dominance.h`,
including the explicit warning that the assert is a tripwire for LAYOUT changes and not a census of
state.

### Stage E — the `bs.ds` write predicate — **DONE after all, and soulbond is exactly why**

Stage E is unnecessary for Valiant Knight (see above) but **required** for soulbond, and the reason
is worth stating because it is the non-obvious half. The payload is symmetric — "**both** creatures
have double strike" — but the pair link is stored only on the Paladin. So answering for the
**partner** needs a scan for a Paladin pointing at it, and that scan rides the `bs.ds` prefilter the
combat sites pass. Without the widening, the partner's double strike would be invisible in real
combat while still passing every nullptr-prefilter unit check — a feature that builds, digests
clean, and never fires from one side. Widening is safe by the documented BoardSources contract: the
list stays a superset, and `HasDoubleStrikeFromLords` still applies its own `grants_double_strike`
test per permanent, so a soulbond source in the list is correctly a no-op for the lord question.
A unit test asserts `bs.ds.size() == 1` with a Paladin out and reads the partner's doubled damage
through real combat.

### Stage E — the `GatherBoardSources` write site — **NOT NEEDED, re-verified**
See "Why Stage E is NOT needed" at the top of this section. In short: `bs.ds` feeds only
`HasDoubleStrikeFromLords`, which inspects only `grants_double_strike`; neither new grant is a lord,
the oracle reads them before consulting the list, and no call site early-outs on an empty `bs.ds`.
**Stage D must re-confirm this for soulbond** rather than assume it.

### Constraint on all four cards: do NOT add a `Keyword` enumerator
`Card::m_keyword_mask` is `uint32_t` and the `Keyword` enum already holds 35 values, so a 36th
would alias `Deathtouch`. Full investigation, measurement and fix in
[keyword-mask-overflow.md](keyword-mask-overflow.md). **It is NOT a live bug** (the UB
constant-folds to 0 today, so the over-width tags are silently dropped — proven byte-identical on
Melira Pod over 1,200 games with a live-probe control). Ship battle cry / soulbond as params with
`keywords: []`, which is the established idiom for all 20+ inert tags here.

### Firing evidence — DONE, all five mechanisms proven live (`scripts/wk_firing_evidence.sh`)

Method: strip exactly one mechanism's param from cards.json and compare per-game play digests **on
WhiteKnights**. A nonzero difference proves the mechanism executes in this deck; the win-turn columns
say whether it also matters.

**RE-RUN 2026-09-26 on the fixed engine.** The table below supersedes the pre-fix one. Two things
changed at once and both are stated so nothing is silently attributed to the wrong cause: the Hero
token fix landed, **and** the harness now attaches the deck's profile to every job (it did not
before — see the header comment in `scripts/wk_firing_evidence.sh`). The digest columns were never
affected by the missing profile (both arms always shared one apparatus); the win-turn columns were,
so every `wt` figure here is new. Full output: `logs/wk_firing/report_postfix.txt`.

| mechanism stripped | d0 (400 g) | d3 (150 g) | 2HG (200 g) | wt base → stripped (d0) | verdict |
|---|---|---|---|---|---|
| battle cry (both cards) | **275/400** | **100/150** | **142/200** | 4.765 → 4.965 | FIRES; the load-bearing addition, worth **0.20 turns** |
| soulbond | 36/400 | 14/150 | 29/200 | 4.765 → 4.798 | FIRES; worth ~0.03 t (it is a 1-of) |
| Valiant Knight's ds activation | 3/400 | **0/150** | 3/200 | 4.765 → 4.765 | FIRES, but **nearly inert** — see below |
| Hero's flat token gate | 0/400 | 0/150 | **49/200** | 5.275 → 5.250 (2HG) | FIRES only at 2 heads, **as predicted by construction** |
| Acclaimed Contender's widened dig | 5/400 | 2/150 | 2/200 | 4.765 → 4.765 | FIRES; closes the last outstanding Stage-1 item |

**RE-RUN AGAIN 2026-09-26 under the ADOPTED FITTED LEAF: every one of the 15 rows and every `wt`
figure came back byte-identical, so the table above needed no edit.** This is a prediction confirmed
rather than a coincidence — Step 2d measured the fitted leaf as byte-identical to both the leafless
shape and the bare ladder at d0 and d3, and all three firing cases run at d0 or d3 b10. The one cell
the leaf does move (`d5 b20`) is in the §3 ladder, not here.

Every row still fires, and the pattern is unchanged from the pre-fix run — which is the useful
negative result here. The Hero fix did not silently kill a neighbouring mechanism: had the new
`attack_tokens_require_self_attacking` gate been over-tight (say, suppressing Adeline too, or
suppressing Hero's tokens even when Hero *did* attack), battle cry's and the dig's counts would have
moved with it, because all three ride the same attack step.

Three of these rows are findings, not just green ticks:

1. **Valiant Knight's activated grant is nearly inert in this deck, and that is a real result rather
   than a wiring failure.** It fires (3/400 at d0, 3/200 at 2HG), so the path is live end-to-end —
   which is exactly the distinction this harness exists to draw. It is simply rarely *affordable*:
   the activation costs `{3}{W}{W}` = 5 mana, the deck's curve tops at 4, and it wins on turn ~4.8,
   so five spare mana almost never exists. Disclosed in 6a as implemented-but-rarely-reachable. The
   0/150 at d3 is the deeper search winning sooner still, which makes it rarer, not broken.
2. **The Hero token gate fixes a genuine 2HG defect, and the fix costs the deck a little speed.**
   With the old unconditional `OpponentHeads()` multiply, Hero made **four** Soldiers in 2HG instead
   of two; the strip arm is correspondingly *faster* (5.250 vs 5.275). Hero's oracle has no "for each
   opponent" clause, so two is right. Correct beats flattering — recorded plainly rather than
   presented as an improvement.
3. **The widened dig matters, which retro-justifies the Stage-1 fix.** 5/400 games play differently
   with the inherited Knight-only filter restored. Play can only diverge if a non-Knight was taken,
   i.e. Lightning Greaves — which is stronger evidence than the `MTG_ETBDIG_TRACE` candidate counts
   (those confirm `looked=5`, the widened count, with 1–5 legal matches, but do not name the cards).
   **This closes the last outstanding verification item from the previous session.**

#### USER-FLAGGED BIAS: battle cry's 0.20 turns is the most goldfish-inflated number in this ledger

The user, on reviewing the reference games (2026-09-26): **"Accorder Paladin is notably better in
goldfishing than in real play."** That is correct, it is *not* a modelling gap, and it lands squarely on
the biggest number in the firing table — so it is recorded here rather than left implicit.

Accorder Paladin is a **3/1 for `{1}{W}`** whose battle cry pumps every *other* attacker. `cards.json`
models it fully (`battle_cry_power`, applied at declare-attackers to every other attacker, tokens
included, stacking across copies) — the bracket note confirms nothing is deferred. The inflation is
entirely the **format**, via three compounding effects that all point the same way:

1. **A 3/1 has no downside here.** The passive opponent never blocks and never casts removal, so one
   toughness costs nothing. In real play a 3/1 trades down to almost any blocker and dies to every
   burn spell in the format.
2. **Battle cry pays off in proportion to board width, and nothing ever narrows the board.** Its value
   scales with the number of *other* attackers; a real opponent's blockers and sweepers are exactly what
   keep that count low, and neither exists. Adeline and Hero tokens make this deck maximally wide.
3. **It rewards attacking every turn unconditionally.** There is never a turn where holding back is
   right, so the trigger fires every combat from the moment it lands.

**So read the 0.20-turn figure as an upper bound on a real deck, while keeping it as a valid within-sim
result.** The digest columns are unaffected — both arms share one apparatus and one format, so "battle
cry fires in 275/400 games" stays exactly true. What is format-dependent is the *worth*, which is why
the firing harness reports firing and win-turn in separate columns. Same caveat class as the passive
opponent row in the 6a table, and the same reason Swords to Plowshares prices at −0.257: this
simulator **overstates an all-out attacker and understates interaction**. Accorder Paladin is the
clearest single beneficiary in the list.

### Firing evidence required before reading ANY A/B (the "equality can mean BROKEN" rule)
Each new mechanism must be shown to FIRE, not merely to build:
* battle cry — a game log where a non-battle-cry attacker's power exceeds its printed+lord value.
* `attack_tokens_per_opponent` — token count unchanged at 1 head, halved for Hero at 2 heads.
* the two double-strike grants — `bs.ds` membership and a doubled damage event.
* the widened Acclaimed Contender dig — `MTG_ETBDIG_TRACE=1` showing Lightning Greaves as a legal
  candidate. **CLOSED** by the digest strip arm above (5/400 games play differently with the
  Knight-only filter restored), which is stronger evidence than the trace: play can only diverge if a
  non-Knight was actually taken.

## Stage 4 — baseline profile (DONE)

`python3 scripts/analyze_deck.py decks/WhiteKnights/WhiteKnights.cod --analyzer-seed 1001`
(seed pinned so the run is reproducible). Wrote `decks/WhiteKnights/WhiteKnights.profile.json`.

**REGENERATED 2026-09-26 on the fixed engine** — the first profile was fit on the buggy one and is
superseded. Log: `logs/wk_stage4/profile_run.txt`; the pre-fix profile is kept at
`logs/wk_stage4/profile.PREFIX.json` purely so the two can be diffed.

* **Cost diagnostic: COST_NEUTRAL** — reframe neither helps nor hurts (4.445 both arms, 200 g, d3).
* **Discard analysis: DISCARD_INERT** — 400 games at d3 reached **zero** cleanup-shed decisions from
  either caller, so there is no policy to derive. Consistent with the deck: it empties its hand.
* Baseline speed, now measured **with the profile attached**: **avg win turn 4.765 at d0** (400 g),
  4.527 at d3, 5.275 in 2HG.
* Coverage re-reported clean in the same run: `"missing": []`, 17/17 cards `full`.

### What the bug was worth, measured

The Stage-5b depth ladder is the clean isolation: identical seeds, identical configs, profile
attached on **both** the pre-fix and post-fix runs, so the only differences are the Hero fix and the
refit that followed it.

| config | pre-fix | post-fix | Δ |
|---|---|---|---|
| d0 (600 g) | 4.703 | **4.735** | +0.032 |
| d3 b10 (300 g) | 4.497 | **4.523** | +0.026 |
| d5 b20 (300 g) | 4.493 | **4.520** | +0.027 |
| d5 b2000 (150 g) | 4.467 | **4.507** | +0.040 |

The buggy engine was flattering this deck by **~0.03 turns at every depth** — a small number, and
worth recording precisely because it is small: a defect that hands a deck free 1/1s on most attacks
moved the headline metric by less than the width of a rounding decision. That is a statement about
how insensitive an aggregate win-turn is to a real rules error, not a statement that the error was
minor. It is the per-game evidence (9 of 40 games showing phantom Soldiers, one of them a T5→T4 kill)
that carried the finding; the aggregate would never have raised it.

The card scores corroborate the firing evidence rather than merely existing, which is worth noting
because the two were produced independently:

| card | score | reading |
|---|---|---|
| Accorder Paladin | **+0.357** (best in deck) | the battle-cry carrier, and battle cry is the deck's biggest mechanism (0.20 t) |
| Sol Ring | +0.325 | ramp into the 4-drops |
| Venerable Knight / Dauntless Bodyguard / Adeline | +0.21 each | the cheap curve |
| Silverblade Paladin | +0.052 | matches its measured ~0.03 t as a 1-of |
| **Hero of Bladehold** | **−0.064** (was **+0.033** pre-fix) | **the sign flipped when the bug was fixed** — see below |
| Valiant Knight | **−0.123** | matches its activation being nearly unaffordable (see firing evidence) |
| Swords to Plowshares | **−0.257** | removal is near-worthless against a passive opponent — expected, and a known goldfish bias, not a deckbuilding claim |
| Unexpectedly Absent | −0.229 | same |

**Hero of Bladehold's score flipping sign is the single most legible consequence of the fix.** Under
the bug its tokens were unconditional, so the fit credited a 4-mana 3/3 with two free 1/1s on every
attack the deck made, whoever made it; with the trigger correctly requiring Hero to attack, the same
card fits *negative* in a deck that wins on turn 4.7 (a 4-drop is frequently just slower than the
1- and 2-drops it competes with). I am **not** presenting this as a deckbuilding recommendation to cut
Hero: a card score is a marginal fit against this goldfish, whose passive opponent never blocks — and
"makes two attacking bodies every combat" is worth far more against a real opponent with blockers than
against one without. The honest reading is narrower and enough on its own: **the pre-fix +0.033 was
measuring a card the engine was playing wrong.**

## Stage 4a — provider routing (DONE)

**The collision was real and is resolved.** Both lists trip the `knights` signature, so without
action they would have shared `KnightsProvider` and `provider_audit.py --check` would fail.

`WhiteKnightsProvider : KnightsProvider`, an **empty derivation** — every judgement hook inherited
byte-for-byte (VialProvider's charge policy, the USER-reviewed `MTG_KNIGHTS_ORDER` cast order, root
defaults for the rest), routed **above** the `knights` branch. Signature OR-ed over
`battle_cry_power` / `soulbond` / `team_pump_grants_double_strike` — four cards, three params, all new
and gated, so no pre-existing deck's mainboard can set them.

`provider_audit.py --check` now reports `WhiteKnights → WhiteKnights`, `Knights → Knights`, exit 0.

**Two things stated plainly because they are the weak points:**
1. **The discriminator leans on a sideboard boundary.** `Knights` lists Accorder Paladin, Hero of
   Bladehold and Valiant Knight in its **sideboard**, and the signature scan reads `deck.mainboard`
   only. So the discrimination breaks if any of those three is moved into Knights' mainboard. It
   survives any swap within WhiteKnights, which is what the OR-across-four-cards discipline buys.
2. **A misroute between these two would be play-neutral anyway**, because this is an empty derivation
   — there is no hook to lose. The cost of getting it wrong is the audit name and the certificate,
   not the deck's play. That stops being true the moment either list earns a measured hook, at which
   point the signature needs a sturdier discriminator.

`Certificate()` answered as **NotAssessed with the reasons**, and this list is *harder* to bound than
Knights: Aether Vial puts creatures onto the battlefield at instant speed, Valiant Knight can double
the whole team's damage for mana already counted, soulbond doubles one more body for free, and
Lightning Greaves grants haste so damage is not deferred a turn. Any "provably winless this turn"
bound that ignores the double-strike activation is simply wrong.

## Stage 5 — Verification

Gate: `python3 scripts/verify_deck.py decks/WhiteKnights/WhiteKnights.cod` (log
`logs/wk_verify/verify.log`). First run: **coverage PASS** (17/17 full), **viewer PASS**,
**viewer_wiring PASS** (4 types wired emitter+GUI, including `attach_host`), **mismatch PASS** (no
`nonconv` / `fd-diverge` over seeds 7001/7002 x 60 games, both arms completed), **play_invariants
PASS** (8 games / 116 decisions). Outstanding at that run: the `card_fields` Scryfall snapshot was
missing the 4 new cards, and `claude_sweep` was unrecorded.

### 5b/5c — multi-depth sanity + budget starvation (PASS)

Seed 4401 (disjoint from the suite's 1001). **Re-run 2026-09-26 on the fixed engine**; these four
jobs are now pooled into the firing harness's base arm rather than standing up their own batch, so the
whole ladder shares one tail with the five strip arms.

| config | games | avg win turn | slowest win |
|---|---|---|---|
| d0 | 600 | 4.735 | T7 |
| d3 b10 | 300 | 4.520 | T8 |
| d5 b20 | 300 | 4.520 | T8 |
| d5 **b2000** | 150 | 4.507 | T7 |

**Monotone** (4.735 → 4.520 → 4.520 → 4.507, never worse deeper) and **plausible** — a 22-land
mono-white curve of 1- and 2-drops with Sol Ring and Aether Vial killing on ~T4.5 is the deck's real
clock. Every game is won, and the last column is how you can see that without trusting a threshold:
the slowest win is T7–T8 against a `max_turns` far above it.

**No budget starvation:** on the SAME 150 games, d5 at b20 and b2000 are *identical* — 0 games win
earlier, 0 later, 150 identical. The suite budget is not constraining this deck. (The check is paired
by game index, not a comparison of the two aggregates, so it cannot be fooled by offsetting moves.)

### 5c2 — horizon-honest tie-break (`leaf_tiebreak_check.py`)

24,000 games / 12,000 paired: **0 changed games (0.000%)**. Verdict is explicitly **NO SIGN AT THIS
SAMPLE**, not "harmless" — the lever never fires here often enough to measure. Recorded as the tool
states it: the default (ON) costs this deck nothing because it does not bind.

## Stage 5d — Claude-play sweep: FOUND A REAL BUG (and a flaw in my own sweep design)

15 Opus agents, one per game, base seed 7701. **This is the step that justified itself.**

### The defect: Hero of Bladehold's token trigger ignored whether Hero attacked

**Reported independently, with repros and correct root-cause, by 9 of the agents.** Confirmed myself
before fixing: **9 of 40 games** at seed 7701 show Hero on the battlefield, *untapped after combat*
(so provably not a declared attacker), yet two Soldier tokens created tapped-and-attacking — plus
their battle-cry pump. One agent found a game where it **changed the win turn** (a turn-5 kill became
turn-4), so this was not cosmetic: it inflated the deck's measured clock.

**Cause, and it is mine.** The two cards using `attack_creates_tokens` have *different trigger
scopes*, one word of oracle text apart:
* Adeline — "Whenever **you** attack, for each opponent, create …" → **player**-scoped, she need not
  attack herself. The existing battlefield scan is correct for her.
* Hero of Bladehold — "Whenever **this creature** attacks, create two …" → **creature**-scoped,
  requires Hero to be a declared attacker (CR 508.1).

I implemented Hero by reusing Adeline's param family and inherited her player-scoped gate. The
galling part: I reasoned about exactly this distinction for **battle cry on the same card** — the
`battle_cry_power` comment says "the source must ITSELF be attacking … hence the sources are drawn
from attacker_indices, not from a battlefield scan" — and did not carry it across to the token half
of the same sentence.

**Why nothing else could have caught it:**
* the executor and the rollout **share** `FireAttackCreateTokens`, so both worlds were wrong in
  lockstep → no `fd-diverge`, no `nonconv`, self-consistent digests;
* the engine-neutrality A/B only asks whether *other* decks moved, and they did not;
* the firing-evidence harness only asks whether a mechanism *fires*, and it did — too often;
* **all 7 of my new unit tests put Hero into the attacker list.** The one untested branch was the
  only broken one.

**Fix:** new `attack_tokens_require_self_attacking` (default **false** → Adeline byte-identical, true
on Hero), `FireAttackCreateTokens` now takes the declared-attacker list and enforces membership for a
creature-scoped source, applied at **all three** sites — executor (`GameEngine::CombatPhase`), rollout
(`TurnSolver::SimulateCombat`) and the **projection** (`PendingAttackDamage`, the one that fails
silently). Regression tests added for both halves: a stay-at-home Hero makes **0** tokens (paired with
a declared Hero making 2, so "0" cannot mean a dead path), and **Adeline still makes hers without
attacking** — that second test is what stops the fix over-applying. Verified end-to-end: **9/40
defective → 0/40**, with Hero still present in 11 of the 40 games so the check is not vacuous.

### My sweep design was wrong: `--game-index` does not vary the game

`RunClaudePlay` calls `SetupGame(deck, seed)`; `game_index` feeds only `PopulateOpponentSpawns`, whose
bodies never attack or block. The single-deck benchmark path ignores it too. Verified directly: game
indices 0 and 7 at seed 7701 deal **byte-identical opening hands**, and `--games 1 --seed 7701
--game-index {0,7,13}` all report the same 5.0000.

So my fan-out over 15 game-indices was **15 copies of one game** — and that game draws none of the
five new mechanisms in its 5 turns (Accorder Paladin is the T7 draw, Contender T10, Hero T17,
Silverblade T18, Valiant Knight absent from the top 30). Every agent independently noticed this and
ran its own supplementary probes, which is the only reason the coverage exists at all and how the
Hero bug surfaced. Credit where due; the sweep worked *despite* my specification, not because of it.

**The skill's own example is misleading here** (`--seed <S> --game-index <GI>` implies gi varies the
game). A sweep must fan over base **seeds**. Re-run recorded below.

### Other findings from the sweep

| finding | status | action |
|---|---|---|
| Valiant Knight's activation logged as `"team pump + haste"` | CONFIRMED cosmetic | **FIXED** — a THIRD hardcoded label site (`AIEngine.cpp`) that I missed after fixing `main.cpp` and `SpellEffects.h`; all three now build the text from the params |
| soulbond's `attach_host` prompt read "the creature to attach the put **Equipment** to" | CONFIRMED cosmetic | **FIXED** — the note now branches on the source card's `soulbond` param; humans *and* future sweep agents read it |
| a Valiant Knight cast is emitted as **6 byte-identical plans** | CONFIRMED, **pre-existing machinery** | Recorded, not fixed — traced by two agents to `CardHasPostEntryActivation` returning true for any `team_pump_cost` card, so Site 9's wave-0 fan-out splits the base plan and the uncapped menu branch does not dedupe. Agents verified the copies are pure noise (identical resulting board from indices 0/18/20/22). Changing Site 9 is GT-moving for other decks and does not belong in a deck onboarding. **Disclosed in 6a.** |
| the `discard` decision's reply index is into `options` (draw order), not `me.hand` (display-sorted), and the note says "hand index" | CONFIRMED, pre-existing | Cost one agent 5 wrong discards before it worked this out. Not fixed here (it is not WhiteKnights' code and cleanup discard is outside claude-play's decision surface) — **recorded as an open item**, since it will mislead every future sweep agent. |
| the soulbond partner ranking preferred a summoning-sick partner over an equal-power unsick one | not a flag (heuristic) | A legitimate `heuristic-optimization` candidate: `SoulbondPartner` ranks on effective power alone and breaks ties without regard to whether the partner can attack. Disclosed in 6a as a ranking narrowing. |

**Consequence for everything measured before the fix:** the Stage-4 profile and the firing-evidence
win turns were produced on the buggy engine, which over-credited this deck. Both have been
regenerated; the numbers in those sections are superseded by the post-fix re-runs.

## Stage 5d RE-RUN (2026-09-26) — over SEEDS, and it found a SECOND real bug

20 games, 0 unresolved flags, **20 of 20 tied the depth-5 search**. Full per-seed record with the
verification each agent actually performed: `logs/wk_sweep_seeds/RESULTS.md`.

Two structural corrections to the first attempt, both of which changed what the sweep could see:

1. **Fanned over base SEEDS** (7801-7815), verified distinct before launch (three seeds, three
   different opening hands) rather than over `--game-index`, which is inert here.
2. **A content-selected second arm**, because the uniform arm had a coverage hole it could not fix by
   growing. The first agents back all reported the same thing: *battle cry verified, the other four
   mechanisms never appeared.* That is structural — the deck kills on turn ~4.5 and `--max-turns 8`
   caps the game, so a 1-of at draw 10 is unreachable in **every** uniformly seeded game, and more
   uniform seeds just buy more of the same opening curve. `scripts/wk_seed_scan.sh` selects seeds
   whose opening hand + revealed draws actually CONTAIN the target card; five agents then ran those
   at `--max-turns 12`. **Selection for coverage only — a content-picked seed set is biased by
   construction and its win turns are not quoted anywhere as the deck's speed.**

### The Hero fix, confirmed in the wild by SEVEN independent agents

7802, 7804, 7806, 7808, 7810, 7813 and 7815 each reached the exact frame the bug lived on — Hero on
the battlefield, summoning-sick, while *other* creatures attacked — and each saw **zero Soldier
tokens**. Several confirmed it twice over arithmetically (the damage total matches the no-token line
exactly, so a spurious pair could not hide). Three also reached the positive half: Hero attacking →
exactly **two** Soldiers, tapped and attacking, flat rather than per-opponent. Agents additionally
checked the half I would not have thought to ask for — that when Hero stays home its **battle cry**
must also stay silent, both clauses reading "whenever *this creature* attacks". It did, every time.

### The new defect: soulbond's SECOND trigger offered illegal partners (CR 702.46b)

Found on seed 7877. soulbond is **two** abilities with **different** partner sets:

* **Trigger A** — "You may pair this creature with another unpaired creature you control as it
  enters." Partner set: **any** unpaired creature.
* **Trigger B** — "Whenever another creature you control enters, if this creature is unpaired, you
  may pair it with **that** creature." Partner set: **the entrant, and nothing else.**

`try_pair` built its candidates from `legal_partners()` — every unpaired creature — and Trigger B
called it without restriction. So an unpaired Paladin could pair with a creature that had entered
turns earlier, which **no ability in the game permits**. The agent demonstrated it concretely:
declined the pairing on Silverblade's own entry (spending Trigger A), then on the next turn a Human
token entered and the offer listed a Worthy Knight from two turns before; taking it granted both
double strike for a **4-damage swing on that turn** against a matched control.

**Confirmed by reading the code before accepting it.** The guards at the Trigger B call site already
tested the *newcomer* — so the intent was right, it simply never reached the candidate list.

**FIXED:** `try_pair` takes an `only_idx`; Trigger A passes `-1` (unrestricted), Trigger B passes
`entered_index`. The restriction **intersects** with `legal_partners()` rather than trusting
`only_idx` to be legal on its own, keeping one source of truth for legality.

Three unit cases added, and the first is built to **fail on the old code** rather than merely describe
the new one: the stale candidate (Hero of Bladehold, 3 power) deliberately OUTRANKS the entrant
(Venerable Knight, 2 power), and `SoulbondPartner` ranks on effective power — so the unrestricted list
provably picks the illegal partner. The second case is the paired control proving Trigger A stayed
unrestricted (over-narrowing both triggers would silently disable soulbond for every Paladin entering
after its partners — the common case in this deck). The third asserts one entrant completes only one
pair.

**Reachability, stated honestly.** In **human play** (viewer / `--claude-play`) it is trivially
reachable, as demonstrated. **Autonomously** it is normally unreachable, because
`DecisionProvider::SoulbondPartner` never declines when candidates exist, so Trigger A always pairs
immediately; the wide set only opens when a live pair **breaks** with other unpaired creatures
present, and nothing in this deck's goldfish removes a creature. So for WhiteKnights specifically
this was a human-play defect — but it is **not structurally confined** to human play, and on the
break path the engine's own `heuristic_default` was an illegal partner too, so the built-in provider
would have picked one if it ever got there. That is the reason it is fixed rather than disclosed.

### The second new defect: Unexpectedly Absent castable with NO legal target (CR 601.2c)

Found on seed 7823 and **confirmed independently before accepting it**. At turn 3 the board is two
Plains (both lands) and the opponent's board is empty; the card reads *"Put target **nonland**
permanent into its owner's library…"*, so no legal target exists anywhere. The cast is offered anyway,
and taking it leaves `graveyard: ["Unexpectedly Absent"]` with **both Plains tapped** and the opponent
untouched at 20 — a card and 2 mana spent on a spell CR 601.2c says cannot be cast. Repro:
`--choices "1,2,-1,-1,8,-1,-1,6"`.

**FIXED** in `src/ai/TurnSolver.cpp`: the `Targeting::NonlandPermanent` human fall-through now also
requires that some nonland permanent exist. This is not a new doctrine, and that is what made it an
easy call — the `controller_lifegain` (Swords to Plowshares) gate **a few lines below in the same
function** already says *"Still requires SOME creature on the battlefield — with none, the spell has no
legal target and stays uncastable for the human too."* The `NonlandPermanent` branch simply never got
the same treatment. The two now agree.

**Why this is safe to fix inside a deck onboarding:** the branch is gated on `HumanPlayActive()`, which
is false in the search and rollout by construction, and the non-human path already `continue`d before
reaching it. So no autonomous play changes and no ground truth moves. It is a pre-existing defect in
shared machinery rather than one of this deck's four cards — recorded plainly as such — but it is
reachable in the viewer *with this deck's list*, and "never narrow the viewer" is a doctrine about
**value** judgements, not about legality.

### Valiant Knight's activation — the one mechanism nothing else could reach

Seeds 7843 and 7819 independently reached the `{3}{W}{W}` activation (T5 off five Plains; T7 via
Sol Ring + a Vial'd Valiant Knight) and verified it across **15 and 4 controlled branches**
respectively, on three different board compositions. The decisive measurement is Valiant Knight
attacking **alone**: 6 damage, not 3. So the grant **includes Valiant Knight itself**, correctly
unlike the lord clause's "Other Knights" — and that asymmetry between one card's two clauses was the
single most likely place for `lord_excludes_self` to leak into the `team_pump` path. It does not.

Also closed there: **expiry** two independent ways, **re-activation** on a later turn, the static lord
in all three directions (pumps other Knights, skips itself, skips the Human tokens while Benalish
Marshal's `affects_all_creatures` does reach them), and **soulbond + the activation together deal 2x,
not 4x** — double strike from two sources does not stack.

One agent inference I checked and **corrected rather than propagating**: it saw the cleanup-discard
heuristic pitch Valiant Knight and concluded the card "gets pitched before the mana ever arrives"
autonomously, which would have reframed the firing evidence's "rarely affordable" as a discard
artifact. It does not hold — the Stage-4 discard analysis measured **zero** cleanup-shed decisions in
400 autonomous games at d3, because this deck empties its hand. The agent only reached a discard
because it had deliberately slow-played to an 8-card hand. Real observation, wrong scope.

## Audits

### Acclaimed Contender dig widening — play-neutrality for `decks/Knights/` (DONE, PASS)

The dig filter is a SHARED cards.json entry, so widening it for WhiteKnights must not disturb the
shipping `Knights` deck. `Knights` holds no Aura and no Equipment, so the candidate set should be
unchanged — proven rather than argued, per the digest-equality-beats-a-sign-test rule:

`bash scripts/wk_knights_neutrality.sh` (old cards.json straight from `git show HEAD:` vs new, via
`--cards-json`, comparing the per-game `MTG_DUMP_WINS` stream):

| arm | games | verdict |
|---|---|---|
| shipped settings (value_play pins d5) | 250 | **IDENTICAL per-game play** |
| d0 b0 (`--ignore-play-profile`) | 500 | **IDENTICAL per-game play** |
| d3 b10 (`--ignore-play-profile`) | 250 | **IDENTICAL per-game play** |

1,000 games, zero divergence → **no `Knights` GT rebaseline is needed** for this change.

Caveat carried forward (the "equality can mean BROKEN" trap): neutrality on `Knights` is expected
*because* the new subtypes are absent there. It says nothing about whether the widened path FIRES
on WhiteKnights. That must be shown positively with `MTG_ETBDIG_TRACE=1` once the deck is
runnable — tracked as an open item under Stage 5.

## Claude-play sweep

- commit: `57c17eac` + uncommitted working tree (engine fp `2a07a96b632a06ca`)
- seeds: 7801-7815 uniform, plus content-selected 7819 / 7823 / 7843 / 7867 / 7877  games: 20
- flags: 0 unresolved

One flag was raised and **FIXED**, not deferred: soulbond's second trigger (CR 702.46b) offered every
unpaired creature instead of only the creature that just entered. Found on seed 7877, confirmed by
reading `src/core/SpellEffects.h`, fixed with a `only_idx` restriction on `try_pair` plus three unit
cases (one of which fails on the old code). Detail in Stage 5d below.

Everything else came back clean: **20 of 20 games tied the depth-5 search**, and the other findings
were heuristic-quality or presentation notes, correctly not filed as rules bugs. Full per-seed record:
`logs/wk_sweep_seeds/RESULTS.md`.

## Search shape — NO VALUE LEAF, and the probe's recommendation was overturned

**Headline, in three parts:**

1. The multi-configuration screen **rejected half of what the cheap probe recommended**
   (`ladder: "single"` regresses at d3b20). `leaf: "none"` + `alpha: "relaxed"` is adopted and shipping.
2. **A value leaf WAS then generated and measured, and it is ~3x CHEAPER than the adopted leafless
   shape at d5, at indistinguishable quality** (§ Step 2d). It is **staged, not adopted**, and adopting
   it is the recommendation. So the earlier claim in this section — "this deck should not have a value
   leaf generated" — was **wrong**, and wrong specifically on the *cost* axis; the quality half of that
   call held. `shape_probe` could not have settled it, because it only ranks **model-less** shapes.
   Generation cost **5 minutes**.
3. The whole detour also found a defect in the generator: `MTG_LAZY_LEAF` is not answer-identical at
   H5, the cell every leaf verdict is measured against (§ Step 2c).

### Step 1 — the cheap early test said "no leaf" (so hours of generation were never spent)

`python3 scripts/shape_probe.py decks/WhiteKnights` is the prescribed pre-step before any value-leaf
generation, and it exists precisely so a new deck's shape decision does not wait on hours of fitting.
`logs/wk_shape/probe.txt` (4 seeds) and `probe_s8.txt` (8 seeds × 500 games):

| shape | units | ratio | d_avg | better | worse | z |
|---|---|---|---|---|---|---|
| `heur` (plain rollout ladder) | 147,583,948 | 1.00 | — | — | — | — |
| `esc_nl` (leafless escalation) | 31,265,966 | 0.21 | −0.0013 | 5 | 0 | +2.2 |
| `fit_nl` (leafless + FIT single pass) | 24,689,673 | **0.17** | −0.0013 | 5 | 0 | +2.2 |

The 4-seed run came back **UNRESOLVED** (|z| < 2) and said so rather than calling a tie a verdict; the
8-seed re-run resolved it at z +2.2. Verdict: **`leaf: "none"`** — so **no value leaf was generated**,
which is the whole point of running the probe first.

### Step 2 — one configuration is NOT the adoption bar, and this is why

The probe's verdict was `fit_nl`. Adopting it off that single cell would have shipped a regression.
Every `leaf: none` / `ladder: single` adoption already in this repo cleared a far higher bar (their own
sidecar notes record it): Minotaur, Dragons, StompySurprise and Goblins each screened **5–6
configurations at 4,000–8,000 games per cell**, with a `d0b0` byte-identity control and a disjoint-seed
reproduction. `scripts/wk_shape_screen.py` does that — 3 arms × 5 configurations + a reproduction,
8 × 500 games per cell, **72,000 games in ONE pooled batch**, arm definitions *imported* from
`shape_probe` so the thing adopted cannot drift from the thing screened.

`logs/wk_shape/screen_report.txt`, seeds 5,510,000+ (reproduction 7,730,000+):

| config | `esc_nl` = leaf:none | | | `fit_nl` = + ladder:single | | |
|---|---|---|---|---|---|---|
| | units | better/worse (z) | | units | better/worse (z) | |
| **d0b0** (control) | n/a | 0/0, **4000/4000 byte-identical** | | n/a | 0/0, **4000/4000 byte-identical** | |
| d3b10 | 0.86x | 1/0 (z +1.0) | | 0.66x | **1/3 (z −1.0)** | |
| d3b20 | 0.89x | 0/0, **4000/4000 byte-identical** | | 0.69x | **0/7 (z −2.6)** | |
| d5b20 | 0.20x | 4/0 (z +2.0) | | 0.16x | 4/0 (z +2.0) | |
| d5b40 | 0.21x | 4/0 (z +2.0) | | 0.16x | 4/0 (z +2.0) | |
| **repro** d5b20 (disjoint seeds) | 0.21x | 0/0 (z 0.0) | | 0.17x | 0/0 (z 0.0) | |

**The `d0b0` row is the screen's own control, not padding.** At depth 0 there is no search, so all
three arms *must* agree; 4000/4000 identical on both says the arms are what they claim to be. Had that
failed, every other row would be void.

### What was ADOPTED, and what was REJECTED

**ADOPTED — `leaf: "none"` + `alpha: "relaxed"`** (`decks/WhiteKnights/WhiteKnights.value.json`).
Zero worse games in **all six cells**, units never above **0.89x** and as low as **0.20x** at the
deck's shipped d5/b20, byte-identical at two configurations, and a clean disjoint-seed reproduction.
That is a clean win on both axes with no regression on any — the pre-approved adoption class. Cost is
measured in deterministic **units** (`MTG_DUMP_UNITS`); wall clock was not separately measured, though
a 5x unit reduction sits far outside wall's noise.

**REJECTED — `ladder: "single"`**, which is what the probe actually recommended. It is **cheaper at
every configuration** (0.66x at d3b10, 0.69x at d3b20, 0.16x at d5) and **quality-identical to
leaf-only at d5** — but it **regresses at the shallower configs**: `d3b20` **0 better / 7 worse, z
−2.6**, where leaf-only is byte-identical, and `d3b10` 1/3, z −1.0. Cheaper on one axis and worse on
another is a **TRADE**, so it is the user's call and not an agent's — and I would recommend against
it, because d3 is not a hypothetical configuration: it is where mulligan generation and the suite's d3
tiers live.

**This is the repo's own "never classify a bundle by its cleanest part" rule landing on a live case.**
`fit_nl` bundles two independent decisions, and at the one configuration the probe measures they are
quality-identical, so the bundle looks exactly as good as its better half. All of the quality benefit
belongs to `leaf: none`; `ladder: single` contributes only cost. Separating them is what turned a
recommended adoption into a half-adoption.

### Step 2b — what a value leaf would COST here, and why that changes the argument

**USER CHALLENGE (2026-09-26), and it was right:** *"How expensive is it to generate the value-leaf?
It might be worth proving whether our model is correct on this assertion."*

The gap in my reasoning: `shape_probe` compares only the **model-less** shapes, so it cannot measure
what a **fitted** leaf would do. Its `leaf: "none"` verdict means "best among shapes available without
a model"; concluding "therefore a fitted leaf would not help" is an extra inference I made without
measuring. If generation is cheap, that inference is not worth making — just generate and measure.

Cost read out of `scripts/valueleaf.sh` rather than guessed, then measured where measurable
(`scripts/wk_matrix_h_cost.py`, phase C's own conditions: unbounded `budget_ms 0`, value OFF, **no
sidecar**, 200 games/cell):

| cell | ms/game | units/game | → 400 g × 4 seeds |
|---|---|---|---|
| H1 | 28 | 2,517 | 0.01 core-h |
| H2 | 130 | 11,002 | 0.06 core-h |
| H3 | 395 | 35,025 | 0.18 core-h |
| H4 | 692 | 62,469 | 0.31 core-h |
| H5 | 734 | 66,103 | 0.33 core-h |

Phase C's H half is **~0.9 core-h**. The V cells need the model and cannot be measured first, but the
driver's own scheduler prices a cell at `4^d` for H against `0.02·3^d` for V — summed over the ladders
that puts V at ~0.14x the H total, so **phase C ≈ 1 core-h**. Adding phase A (2,500 games × `ROW_K=3`
searched labels) and phase E (`AB_GAMES` 1,000 × 8 seeds × 2 arms, plus the trust/play A/Bs) at the
measured play rate lands the whole generation at roughly **4–6 core-h, i.e. ~10–15 minutes wall on 32
cores.**

**That is essentially free, and it settles the argument: the leaf SHOULD be generated and measured
rather than inferred.** For scale, the decks this repo found expensive are 3 orders of magnitude worse
(FiveColour and Snow were both 40–80 h projections). WhiteKnights is cheap for the same structural
reason the leaf is expected not to help: it is a 22-land aggro deck that kills on T4.5, so the search
resolves inside its own horizon.

**Two things must happen before that generation is worth running, though:**

1. **It needs a FROZEN COMMIT.** `valueleaf.sh`'s `phase_freeze` aborts on uncommitted changes under
   `src/` — "the frozen commit would not describe the binary" — and this working tree has 12 modified
   `src/` files. Committing is the user's call.
2. **`MTG_LAZY_LEAF` must be dealt with first, or the generation answers the wrong question.** See
   below — it corrupts the very reference the leaf verdict is measured against.

### Step 2c — the cost probe found a DEFECT in the generator: `MTG_LAZY_LEAF` is not answer-identical at H5

Full write-up: [lazy-leaf-corrupts-the-h5-reference.md](lazy-leaf-corrupts-the-h5-reference.md).

`valueleaf.sh` arms `MTG_LAZY_LEAF=1` on every H cell of every deck and documents it as
**"answer-identical (per-game win turns verified)"**. On this deck it is answer-identical at H1–H4 —
200/200 identical digests, 0 better, 0 worse — and **not** at H5:

| seed | identical digest | same win turn | better | worse | d_avg |
|---|---|---|---|---|---|
| 8008 | 158/200 | 161/200 | 0 | **39** | **+0.1950** |
| 9009 | 161/200 | 166/200 | 0 | **34** | +0.1700 |
| 10010 | 160/200 | 164/200 | 0 | **36** | +0.1800 |
| 11011 | 165/200 | 168/200 | 0 | **32** | +0.1600 |
| **pooled** | | | **0** | **141** | ~+0.18 |

**0 better / 141 worse across 800 paired games**, on all four of the matrix's own seeds, with the units
ratio collapsing to **0.307** at H5 alone (0.79–1.02 at every other depth) — so the lever is skipping
most of the work at that rung rather than trimming it.

**Why it is worse than a 0.18-turn measurement error.** H5 is the **escalation cap**, so per the
value-leaf skill "trust the leaf" *means* "the leaf matches H5" — H5 is the reference every V cell is
compared against. Inflating it by +0.18 turns makes the heuristic ladder look worse and therefore the
leaf look **better than it is**, on every deck whose matrix was built with the lever armed. The bias
runs toward *adopting* a leaf, which is not the conservative direction.

Not established: whether it generalises (the quoted verification was on **Snow**, not here), the
mechanism, or whether any shipped sidecar is actually wrong as a result. Those are fleet-wide questions.

**A near-miss worth recording.** The first reading of the anomaly was that my newly-adopted `leaf:
none` sidecar caused it — it was the recently-changed thing, and it *was* affecting cost (7x cheaper at
equal play quality, which is exactly what it was adopted for). Four controls (lazy on/off × sidecar
present/absent, `scripts/wk_h5_anomaly.sh`) were needed to separate them. Blaming the most recent
change would have produced a confident, wrong retraction of a good adoption.

### Step 2d — THE LEAF WAS GENERATED AND MEASURED. My "no leaf" call was WRONG on cost.

Generation ran on the frozen commit `bb5d1038` with the defect above disarmed
(`MTG_VL_LAZY_LEAF=0`), and it took **~5 minutes wall** — even cheaper than the ~10–15 min projection.
Log `logs/wk_valueleaf/run.log`; staged model `logs/eval/WhiteKnights.value.STAGED.json` (53,666 bytes
of fitted trees, `value_trust_depth_candidate: 5`, trust shipped UNSET).

**A trap that had to be dodged first, and it would have silently invalidated everything.**
`phase_train` builds the staged sidecar by copying the **live** one and adding only `eval_model`:

```python
if os.path.exists(live):
    L = json.load(open(live))      # REgeneration: keep value_play/table/crossover until measured
    L["eval_model"] = R["eval_model"]
```

The live sidecar is the adopted `leaf: "none"` shape — and `leaf: "none"` makes `AttachValueSidecar`
substitute `Constant()`, so the fitted trees are **never consulted**. Generating with it in place would
have produced a matrix whose V cells and phase-E A/B both measured the *leafless* shape while reporting
on "the leaf", and every number would have looked neutral for the wrong reason. The generator's own
phase-E output even names this as the usual cause of a dead trust experiment. So the sidecar was moved
aside for the run; the staged file's `value_play` is correctly **absent**, which was verified before
reading any result.

**Phase E is not the comparison that matters.** With the sidecar moved aside, `live` is *bare* — the
plain rollout ladder — so phase E answers "is the fitted leaf better than the plain ladder?"
(−0.00050 turns, t −1.87, **0.11x** cost). But this deck does not ship the plain ladder; it ships the
leafless escalation. `scripts/wk_leaf_vs_leafless.py` runs the three-way on the **sidecar route** (three
scratch deck dirs differing only in `value.json`), 8 × 500 games per cell, paired per game index, on
**two disjoint seed bases**:

| config | leaf vs LEAFLESS (3,310,000+) | units | repro (9,910,000+) | units |
|---|---|---|---|---|
| d0b0 | 0/0, 4000/4000 identical | n/a | 0/0, 4000/4000 identical | n/a |
| d3b10 | 0/0, **4000/4000 byte-identical** | **1.00x** | 0/0, **4000/4000 byte-identical** | **1.00x** |
| d3b20 | 0/0, **4000/4000 byte-identical** | **1.00x** | 0/0, **4000/4000 byte-identical** | **1.00x** |
| d5b20 | 0 better / 1 worse (z −1.0) | **0.42x** | **0 better / 0 worse** | **0.38x** |
| d5b40 | 0 better / 1 worse (z −1.0) | **0.34x** | **0 better / 0 worse** | **0.31x** |

**Pooled at d5: 0 better / 2 worse across 16,000 paired games (z −1.41, no sign). At d3: 16,000 games
byte-identical.** Cost: the leaf is **2.4–3.2x cheaper than the leafless shape** at both d5
configurations, and **exactly 1.00x at d3**.

**So the correction is specific, and it is not about quality.** The two shapes are
quality-indistinguishable everywhere — that part of the "no leaf" call holds. What was wrong is the
**cost** claim: I concluded the deck did not want a leaf, and the fitted leaf is ~3x cheaper than the
shape I adopted at the configuration the deck actually plays. `shape_probe` could not have told me
that, because it only ranks model-less shapes; the only way to know was to generate one, and that cost
five minutes.

**The mechanism, which the numbers state plainly:** the leaf is *exactly inert at d3* — identical play,
identical cost, 16,000 games — and pays only at d5. At d3 the search resolves inside its own horizon
and never consults a leaf estimate, so there is nothing for a model to do; at d5 the horizon rollout is
reached often enough that replacing it with an O(1) evaluator removes most of the work. That is the
same "wins inside the horizon" mechanism this repo records for Goblins, with the boundary sitting
between d3 and d5 for this deck rather than above d5.

**NOT adopted — staged, and the recommendation is to adopt.** Held back rather than taken because it
reverses a same-day documented adoption, because phase D itself says the trust question needs more
games ("the fix here is MORE GAMES, not acceptance" on an inconclusive V4 gap), and because shipping a
53 KB fitted model is a heavier, commit-bound artifact than a three-line shape file. The model stays at
`logs/eval/WhiteKnights.value.STAGED.json`, which is the documented staged-not-adopted location; the
deck still ships `leaf: "none"`. On the measured axes there is no trade — quality ties, cost is 3x
better — so by the repo's clean-win rule this is adoptable on the user's word.

### Step 3 — the adopted file reproduces the arm that was screened (verified, not assumed)

The screen measured its arms through **per-job manifest keys** with `ignore_play_profile: true`.
Production uses a different route entirely: it reads the sidecar, and the file's mere **presence** is
the activation mechanism. "Those should be equivalent" is the `screen-arm-must-pin-every-key` failure
waiting to happen, so `scripts/wk_sidecar_activation.py` checks it on three routes — the deck as it
ships, the screen's arm keys, and the sidecar moved aside — over 4 configurations × 4 × 250 games:

| config | sidecar == screened arm | sidecar == bare |
|---|---|---|
| d3b10 | **1000/1000** | 999/1000 |
| d3b20 | **1000/1000** | 998/1000 |
| d5b20 | **1000/1000** | 998/1000 |
| d5b40 | **1000/1000** | 1000/1000 |

**PASS.** The adopted file reproduces the screened shape exactly at every configuration, and the
divergences from *bare* are the liveness half — they prove the file is actually loading, so the match
with the measured arm cannot be a silent no-op (the "digest equality can mean BROKEN" check). The
d5b40 row showing no bare divergence at this sample is expected rather than suspicious: the
72,000-game screen puts that rate at roughly 6 games per 4,000, so 1,000 games is simply too few.
Liveness is a global property of one file on one load path, not a per-configuration one.

### Consequence for the numbers already in this ledger

Adoption changes play, so every figure measured before it described the un-adopted deck. Re-running
the entire firing harness under the adopted sidecar moved **exactly one cell** — `wk_lad_d3_b10`
4.523 → 4.520 — with all 15 firing rows, the other three ladder rows, the 2HG row and the paired
budget check unchanged. The tables above carry the shipped values.

## Mulligan-generation setting — DERIVED, and the answer is "no override"

Per CLAUDE.md the per-deck pipeline is **profile → value leaf → mulligan**, and the mulligan generator
reads `mull_gen_depth` / `mull_gen_budget_ms` from `value_play`. The value-leaf stage is now closed
(answer: no leaf), so the next stage's documented first step is to *measure* the labeller setting
rather than take a default — the user's standing instruction on this is explicit: *"We don't always
want to take d3 b3 just because it is the default."* This is a measurement, not a generation, so it
does not need a frozen commit.

`python3 scripts/derive_mullgen_setting.py decks/WhiteKnights/WhiteKnights.cod --hands 200`
(`logs/wk_mullgen/derive_200.txt`; a 48-opener pass is in `derive.txt` for comparison):

| arm | rho vs shipped play | units/rollout | cost_x | 48-opener cost_x |
|---|---|---|---|---|
| d1 b3 | 0.9987 | 1,065 | **0.69x** | 0.92x |
| d2 b3 | 0.9992 | 2,464 | 1.59x | 2.09x |
| d3 b3 | 0.9994 | 2,596 | 1.67x | 2.04x |
| d3 b20 | 0.9998 | 9,108 | **5.87x** | 7.71x |
| **d5 b20** (= shipped play) | **1.0000** | 1,552 | 1.00x | 1.00x |

The tool's mechanical pick is `d1 b3` — cheapest arm clearing the 0.990 rank-fidelity floor.
**I am not taking it, and no override is written.** The reasoning:

* **The saving is small and unstable across samples.** `d1 b3` measured 0.92x on 48 openers and 0.69x
  on 200 — the reference arm's own cost moved 1,173 → 1,552 units/rollout, because a budget-limited
  arm's cost depends on the hands drawn. So the real saving is "somewhere around 1.1–1.5x", not a
  figure worth trading fidelity for.
* **Play settings are exact by construction** (rho 1.0000 — it *is* the reference) and already close
  to the cheapest arm. The tool's own rationale for emitting no override says exactly this: for such a
  deck "there is no trade-off to arbitrate". Formally that rule is gated on a value trust depth, which
  this deck has none of (`value_trust_depth=None`, no leaf) — but the rationale applies regardless.
* **Generation is not cost-constrained for this deck.** The precedents that *did* take an override
  were escaping real infeasibility: KittyEquipment 4.44x, Snow 2.87x, FiveColour 1.67x, each on a deck
  where generation was hours-to-days. WhiteKnights is a 22-land mono-white aggro deck that kills on
  T4.7 — the 72,000-game shape screen above finished in about five minutes. Trading exactness for ~30%
  here buys nothing anyone needs.
* **Omitting the keys already yields the intended behaviour**, which is the neat part: with no
  `mull_gen_*` present, generation falls through to the built-in gen default of **d5/b20** — the same
  d5/b20 that is this deck's shipped play and the rho-1.0000 row. For Snow that same fall-through was
  a disaster (its built-in default was the most expensive labeller in the repo applied to the least
  tractable deck); here it is the correct answer arrived at for free.

**One genuine finding worth keeping, because it confirms the tool's premise on a fresh deck:** cost is
**not monotonic in depth**. `d3 b20` costs **5.87x** what `d5 b20` costs — going *shallower* at the
same budget is nearly six times more expensive. That is the starved-budget effect the tool's docstring
predicts: the escalation ladder commits the deepest *completed* pass, so a budget too small to finish
d3 pays for an abandoned pass and commits the shallower line anyway. Anyone tempted to "save time" by
lowering the generation depth on this deck would make it dramatically slower.

**A diagnostic caveat for whoever reads that log.** The tool prints
`play=d5 b20 [BuiltinDefaultPlay (deck ships no enabled value_play)]` **even though the sidecar
exists**. That is correct but easy to misread: the sidecar deliberately carries no `target_depth` and
no `enabled`, so `value_play.drives()` is false (the same deliberate shape the `Snow.value.json` note
documents) — yet the *shape* keys still apply, because `DeckShapeScope` reads `value_play.leaf` and
`.alpha` directly with no `drives()` gate. The sidecar-activation check above proves this empirically:
the deck plays differently with the file than without it. So the reference policy used here (d5/b20)
is right, and the message means "no play-settings override", not "no sidecar".

**Still not derived, and deliberately:** `expected_buckets`. Recording K is the **user's bucket
ruling**, never an agent's derivation, so it stays absent.

## Approved deferrals

*(none yet — the two reclassified Stage-1 notes were FIXED, not deferred; the "legendary artifact"
residue on Acclaimed Contender is the one candidate so far and is PROVISIONAL pending user sign-off.)*

**The `card_costs` gate FAIL was NOT deferred — it was FIXED (2026-09-26).** It was 2 mismatches on
**Brightcap Badger** and **Fungus Frolic**, and the position here used to be "they belong to the Fungus
deck, so the gate stays red rather than being signed off under WhiteKnights' deferrals". That was the
right refusal but the wrong conclusion: **the card data was correct and the audit was wrong.**
`audit_card_costs.py` only consulted a name-matched Scryfall face when the top-level `mana_cost` was
empty, which excluded the entire adventure class (CR 715) — those carry a populated *combined*
top-level cost, so each face was diffed against `{3}{G} // {2}{G}` and could never match. Fixed;
**the gate now PASSES, 319 costed cards, all match.** Full detail in Stage 6 §6.

## Side finding, fully closed out: the keyword-mask overflow

Investigated, measured and written up as [keyword-mask-overflow.md](keyword-mask-overflow.md).
Summary: `Keyword` has 35 values in a `uint32_t` mask, which is UB, but it currently constant-folds
to 0 so the three over-width tags (`Persist`, `Evoke`, `Convoke`) are silently **dropped** rather
than aliased — proven play-neutral on Melira Pod over 1,200 games with a control arm confirming the
probe was live. **No live bug, nothing to rebaseline.** It was first reported to me as a live bug
and I repeated that before testing the engine; the correction is recorded in that doc. It is a real
latent hazard (one inlining decision, or a different compiler on the Windows CI, flips it) and it
is why no card in this run adds a `Keyword` enumerator.

## Open questions for the user (surfaced, not blocking)

1. **`decks/Knights/` and `decks/WhiteKnights/` both stay?** Assumed YES (they are different
   lists, and Knights is a shipping deck with generated artifacts). If WhiteKnights is meant to
   *replace* Knights, the archive convention in CLAUDE.md applies instead.
2. **Acclaimed Contender "legendary artifact"** — proposed as a disclosed-inert residue
   (unexpressible as a subtype; deck runs none). Needs sign-off. It is the deck's **only** bracket note.
3. **`attack_tokens_require_self_attacking` classified as viewer-INERT** in
   `scripts/audit_viewer_decisions.py`'s `INERT_PARAMS`. Reason given: it is a trigger *condition*
   transcribed from oracle text, not a choice a human could make differently. This is the
   classification whose absence was previously holding the `viewer` gate; the gate now passes on it.
   `audit_viewer_decisions.py`'s own self-guard says such a call needs the user's OK.
4. **`ladder: "single"` — rejected as a TRADE, needs a ruling.** Cheaper on units at every
   configuration but **7 games worse at d3b20 (z −2.6)**. I did not adopt it and recommend not
   adopting it; `leaf: "none"` (the clean half) *was* adopted. Full table in *Search shape* above.
5. **The deferred haste-from-Equipment projection fix** — improves the search but moves **five GT
   tiers**. See `docs/design/haste-from-equipment-not-projected.md`.
6. **Add WhiteKnights to the regression suite?** Currently no GT tier exists for it.

**Decisions I took provisionally rather than blocking on** (all reversible, each recorded above):
adopted `leaf: "none"` as a pre-approved clean win; rejected `ladder: "single"` as a trade; kept the
Resplendent Angel diagnostic-label fix despite the GT digest churn it causes; fixed the `card_costs`
audit rather than deferring its two red entries.
