# Mirrorwing Dragon play-viewer issue batch (2026-08-12)

**Status (updated 2026-09-03):** issue #1 SHIPPED exactly as prescribed (74c43ca4/1a7ca347,
TurnSolver enchant-target gate); the other seven remain open as analysed.

Status: **ANALYSED, NOT STARTED.** Every finding below was reproduced from committed evidence; no
code has been changed. Self-contained — everything needed to execute this lives in this file or in
git.

## Evidence

Eight issues reported by the user after a hand-played Mirrorwing Dragon session. User numbering is
by **SEED**, and each game is a committed reference (`ed4ff99`):
`references/Mirrorwing_Dragon/claude_s{5,6,7,8,9,11,12,13,14,15}_gi*.json`.

One piece of evidence is **not** in git: the saved rejection artifact
`logs/play/rejections/Mirrorwing_Dragon_cod_s10_gi9_t4.json` (`logs/` is gitignored, and seed 10 was
never saved as a reference). Its content is inlined under #4 below so the analysis stands without it.

Read `.claude/skills/regression-testing.md` before any A/B or accept, and `.claude/skills/mtg-rules.md`
before the #6 targeting change.

**Line numbers in this doc are hints from 2026-08-12 — anchor on the symbol name** (same convention
as `tools/play/DECISIONS.md`).

## The split

| # | Issue | Class | Moves GT? |
|---|---|---|---|
| 1 | Redundant target dialog before the board prompt | engine bug (viewer-only path) | no |
| 8 | Treasure tokens rendered as clickable tap abilities | scoping change | no |
| 4 | Twinflame + Gold Rush rejected as one line | viewer gap + a search check | no |
| 3 | Cleanup discard sheds the only non-bounceland | engine bug | yes |
| 5 | Depletion land sacrificed when it was not needed | engine bug | yes |
| 2 | Depletion land held while attack-capable dorks are tapped | heuristic | yes |
| 7 | Floating mana displayed as "any colour" | engine bug | yes |
| 6 | Tricks uncastable when only the opponent has creatures | model gap | yes |

---

## #1 — Redundant target dialog · VIEWER PATH ONLY

**Symptom.** Queueing a trick pops a variant-picker dialog asking which creature it targets, then the
correct board-click target prompt asks the same thing again. For Gold Rush / Scale the Heights the
first dialog degrades further into the flat "Choose how to resolve" picker.

**Root cause.** `TurnSolver::CheckLine` (`src/ai/TurnSolver.cpp`, ~16927) emits an `enchant`
sub-decision for *every* action carrying `enchant_target > 0`. The Zada/Mirrorwing solo-target
tricks **reuse `enchant_target` to carry their target** (see the `solo_target_trick` enumeration
block, ~3502-3544, and `TurnSolver.h`'s `Action::enchant_target` comment), so a queued trick becomes
a plan-variant dimension — even though the real target is asked at resolution by
`ResolveSoloTargetTrick` (`src/core/SpellEffects.h`, ~4196, added 2026-08-12).

For `trick_up_to_one` (Gold Rush, Scale the Heights) the untargeted variant has `enchant_target == 0`
and so carries **no** sub at all. The variant set is then mixed, and `renderChooseDialog` in
`tools/play/index.html` (~1221) falls back to `renderChooseFlat` — the ugly picker.

**Fix.** Apply the **Ponder precedent** documented immediately below the offending code
(`TurnSolver.cpp` ~16961-16965): *a decision re-asked at real resolution must not also be a plan
variant token.* Gate the `enchant` sub on the action's card not being `solo_target_trick`, looking
the definition up the way the sibling `soulfire` / `strive` / `crackle` blocks already do. Plans
differing only in trick target then share a signature, collapse to one representative, and the cast
grades `accept` — the board prompt owns the choice.

* **Keep** the `strive` sub: Twinflame's extra-target count is paid mana, a genuine plan choice.
* **Keep** the `enchant` sub for real Auras (`is_aura`) — that target is asked only once.
* GT-neutral: `CheckLine` is reached only from `--validate-line` and `--scenario`
  (`src/main.cpp` ~1993 and ~3497). Verify with a byte-identical smoke.
* `tools/play/index.html` needs no change — `enchantTargetsFor` already skips `a.trick` actions (~526).

---

## #8 — Treasure tokens should not have visible tap abilities · SCOPING

**Root cause.** `src/main.cpp` (~789-798) tags `Action::Kind::SacForMana` with
`"activate": true, "sacout": true`, which is what makes the viewer draw the ⟳ badge and demand a
click. That flag was added 2026-08-08 for the Goblin sac *outlets* (`SacCreatureOutlet` —
Skirk Prospector / Siege-Gang / Pashalik), which genuinely choose a victim.

`SacForMana` is a different thing: a **self**-sacrificing mana source. `CheckLine` already treats it
as implicit (`src/ai/TurnSolver.cpp` ~16860-16866 — counted in `planSacs`, deliberately outside the
declared cast multiset, and the pool sort already prefers a no-sac plan when one exists). So the
badge buys nothing for a Treasure.

Cards with `sac_for_mana_amount` in `src/cards/data/cards.json`: Treasure Token (1), Lotus Bloom (3),
Black Lotus (3).

**Fix (decision taken).** Emit `activate`/`sacout` for `SacForMana` only when the sacrificed
permanent is **not a token** — Treasures become implicit auto-tapped mana; Lotus Bloom / Black Lotus
stay clickable, because choosing to hold rather than crack a Lotus is a real decision. Token-ness is
already distinguishable (`m_number >= 1000`, see `GameState::next_token_number`); key off the
permanent, not the action kind. Record the split in the `sacrifice` / `sacout` row of
`tools/play/DECISIONS.md`.

---

## #3 — Cleanup discard sheds the only non-bounceland · ENGINE BUG

**Symptom.** Seed 9, turn 2, hand of 8 (over by 1), board = one tapped Gruul Turf. The engine's
default shed is the **Forest** — the only non-karoo land in hand. The other land in hand is a second
Gruul Turf, so taking the Forest leaves a karoo-only mana base that can never net a land.

**Root cause — a bug, not a tuning question.** `MirrorwingProvider::CleanupDiscardCandidates`
(`src/ai/DecisionProviders.cpp` ~6588) reads mana costs straight off the hand card: ~6657-6658
(`c.m_mana_cost.ManaValue()`, `c.m_mana_cost.red`), ~6668, ~6672.

Hand and library cards are **`DeckLoader::MakePlaceholder` placeholders carrying only a name**
(`src/deck/DeckLoader.cpp` ~159-165), assigned straight into the library by
`GoldFishRunner::SetupGame` (`src/runner/GoldFishRunner.cpp` ~480). `m_mana_cost` is
default-constructed and empty. (Battlefield permanents *do* carry the full card — they are built from
`def.card` on resolution — which is why this only bites hand-card reads. The existing
`CleanupDiscardManaValue` helper in `src/core/SpellEffects.h` ~219 exists precisely because of this,
and falls back to the card only when the DB lookup fails.)

So every hand card reads MV 0. Consequences inside the bucket rule:

* `cast_turns` returns the same value for every magnet → the "castable sooner" comparison is inert.
* `enabler_mv = 0` → `need = max(0, enabler_mv - board_sources) = 0`.
* Bucket **S1** therefore classifies **every mana card in hand as "excess"** and sheds it, ordered by
  `mana_shed_rank` — which puts the green Forest first, because the green guard is already satisfied
  by the board Gruul Turf.

Hand-traced against seed 9's decision 3 and it reproduces the reported pick exactly.

**Not the searched-discard path.** `MTG_DISCARD_NODE` defaults on (`src/ai/AIEngine.cpp` ~173), so
the searched block at `AIEngine.cpp` ~3673 is dead; and the discard fan-out is gated on
`CleanupDiscardSearchWidth() > 1 && !HumanPlayActive()` (`src/ai/TurnSolver.cpp` ~13000) with a
default width of 1 (only `TreasureHuntProvider` overrides). The pick is purely `cand.front()`.

**Fix.**
1. Resolve costs through the card database in the Mirrorwing bucket rule — the function already has a
   local `def_of(c)` helper; use `def_of(c)->card.m_mana_cost`, or the shared
   `CleanupDiscardManaValue` pattern.
2. **Audit the same read elsewhere.** `CombatCheatCandidates` (`DecisionProviders.cpp` ~380-395) and
   the Goblin enabler code (~5108-5130) correctly bind `hd->card` first. Grep the remaining direct
   `.m_mana_cost` reads on **hand/library** cards (~5323, ~5416, ~5441, ~5548, ~5594) and fix any
   that read a placeholder. A quick way to find the whole class:
   `grep -rn "\.m_mana_cost" src/ | grep -v "def\|->card\|d->"`.
3. Re-check the seed 9 turn 2 pick after the fix. Expected: Zada (S2, a redundant magnet), then
   Twinflame (S3) — both spares, neither a land. If a corrected rule still shortlists the last
   non-karoo land on some board, add an S1 guard: never shed the last land in hand that does not
   bounce a land on entry (`etb_bounce_land`) while the battlefield's land base is karoo-only.

---

## #5 + #2 — Mana reservation · ENGINE BUG (#5) + HEURISTIC (#2)

Both live in `TurnSolver::BatchPrepayMainCasts` (`src/ai/TurnSolver.cpp` ~7211).

### #5 — depletion land sacrificed when it was not needed (bug)

**Symptom.** Seed 11, turn 5. Board: Forest, Mountain, Mirrorwing Dragon, and a Sandstone Needle with
its **last** depletion counter. The turn casts one spell (Goblin Instigator, `{1}{R}`) after a Forest
land drop. The engine taps the Needle, spending the final counter and sacrificing the land, for mana
Mountain + Forest could have paid.

**Root cause.** The function returns early at `eligible < 2` (~7279) — **before the reserve mask is
computed at all** (~7295-7323). Single-cast turns therefore get no reservation whatsoever. The
per-pip greedy then ties Needle / Forest / Mountain at `ManaSourceRank` 10
(`src/ai/DecisionProviders.cpp` ~950: mono-colour → 10, and a depletion land is deliberately *not*
special-cased there), and the tie-break is battlefield order — the Needle sat at slot 0.

The old per-payment reservation (`ReserveEnabled` / `MTG_RESERVE`, `src/core/SpellEffects.h` ~5568)
is default off and documented as superseded by exactly this batch prepay, so nothing else covers it.

The `eligible < 2` early-out is justified in-comment purely on *affordability* grounds ("a single
cast is already optimal via the per-cast complete-solver fallback"). Reservation is orthogonal and
was lost by accident.

### #2 — depletion land held while attack-capable dorks are tapped (heuristic)

**Symptom.** Seed 7, turn 4 (reference decision 7): Forest, Mountain and **both** Ignoble Hierarchs
tapped, while the untapped Sandstone Needle is held.

**Root cause.** `reserved` is built as *every untapped depletion land* (~7295-7306) plus **only the
single greatest-power attacker** when it happens to be a mana source (~7312-7323, via
`FindBestOwnAttacker`). A 0/1 Ignoble Hierarch is never the greatest-power attacker (Zada is, and
Zada is not a mana source), so the dorks are free to be tapped and the depletion land wins the hold.

With no reserve at all the plain solve would have taken Forest + Mountain + Needle (ranks 10/10/10
against the tri-colour dork's 30) and left both dorks up — i.e. the reserve is what *causes* the bad
tap here. The user's ruling: a creature that can attack outranks a depletion counter.

### Fix (one change, one flag)

1. Compute the reserve mask **before** the `eligible < 2` early-out and run the held solve for
   single-cast turns too. The "judged against the whole turn, never stranded" soundness argument is
   unchanged: a single cast *is* the whole turn's combined cost.
2. Widen the attacker reserve from `FindBestOwnAttacker` to **every attack-capable creature mana
   source** (`CanAttackFull` plus `ManaDork` / `mana_rock`).
3. Make the hold **tiered** rather than all-or-nothing: try `attackers ∪ depletion` → `attackers` →
   `depletion` → none. Today's single all-or-nothing attempt is why (2) alone would silently degrade
   to holding nothing whenever the widened set is unaffordable.

Ship behind `MTG_RESERVE_TIERED` (default off; `EnvOn` per `.claude/skills/coding-conventions.md`),
A/B smoke + regression, report win% / avg-win-turn, adopt on approval, then rebaseline. Part (1) is a
bug fix and part (3) is a judgment call, so measure them as one arm but describe both in the report.

---

## #7 — Floating mana shown as "any colour" · ENGINE BUG (contained)

**Symptom.** Seed 15 turn 5 floats `{G:1, wild:2}` mid-resolution; seed 7 turn 4 floats
`{G:1, wild:1}`. `tools/play/index.html` (~571-577) renders `wild` as a ✦ "any-color" pip. There is
no such mana in Magic, and this deck's multi-colour sources are all R/G duals (Gruul Turf, Kazandu
Refuge, Rootbound Crag) or a B/R/G dork (Ignoble Hierarch).

**Root cause is much narrower than the deferred restricted-wild model.** The real tap path already
produces correct colours:

* `tap_source` (`src/ai/ManaPayment.cpp` ~103-107) gives a karoo one mana of *each* colour it makes,
  and a single-yield dual a *chosen* colour.
* A cracked Treasure floats `chosen_float_color` via `ApplySacForMana` (`src/ai/AIEngine.cpp` ~2457),
  so **Treasures never float wild** — an uncracked Treasure is a permanent and floats nothing. Only
  the search's subset arithmetic credits `ritual_float` as wild.

The user-visible `wild` comes entirely from the prepay's **reconstruction** at `TurnSolver.cpp`
~7368-7379, which pins the combined cost's coloured pips and dumps `produced.Total() - pinned` into
`pool.wild`, discarding the colours the solve actually produced. Note `produced.wild > 0` is already
a decline condition at ~7358 — so at that point every tapped source demonstrably made a real colour.

**Fix.** Rebuild the pre-loaded float in real colours instead of pinned-plus-wild.

**Do not** read `produced`'s colour breakdown directly. The colour-collapse optimisation deliberately
recolours generic-only colours, and its documented safety contract is exactly *"no `out_full_pool`
caller reads a per-colour count"* (`src/core/SpellEffects.cpp` ~1208-1218) — that path is described
there as "the whole win", so turning collapse off is not the answer either.

Instead, after the solve, diff the pre-solve battlefield snapshot (`bf_snap`) against the live
battlefield for newly-tapped sources and sum their concrete production using the same rules
`tap_source` applies (`EffectiveProduces` / `ManaProducedPerTap`, karoo = one of each colour),
assigning multi-colour sources scarcity-first so the combined cost's pips are covered and the surplus
floats in its true colour. This is independent of the collapse, so collapse stays on.

This changes how later casts drain the pool (`SpendFloatingTowardCost` currently takes wild first for
generic pips), so it moves GT. Flag `MTG_FLOAT_COLORS`, default off → on after A/B; suite, per-game
audit, rebaseline.

Scope note: this fixes the **float** (a real game object the player can see). It does **not** touch
the affordability accounting (`AddSourceToPool`, `CanPay`), which still models every multi-colour
source as full wild — that remains Option 1 of `viewer-mana-color-fidelity.md`. Add a line there
recording that the float half is done.

---

## #6 — Tricks uncastable when only the opponent has creatures · MODEL GAP

**Verified correct** for "target creatures you control" (Twinflame): with no own creature the
enumeration emits nothing and the spell is uncastable. Seed 15 turn 4 offers Gold Rush untargeted but
no Twinflame — right answer.

**Real gap.** The trick-target enumeration (`src/ai/TurnSolver.cpp` ~3626-3681) offers only *own
battlefield creatures*, *own hand creatures*, and the untargeted variant for `trick_up_to_one`.
Opponent creatures are never candidates. But Fists of Flame, Ancestral Anger and Expedite print plain
**"target creature"** — any creature — and all three **cantrip**, so on an empty board they are
legally castable at the opponent's creature for the card. The goldfish opponent spawns creatures
explicitly "to provide targets for creature-targeting spells"
(`src/runner/GoldFishRunner.cpp` ~132-153, `PopulateOpponentSpawns`), and seeds 9 and 15 both have
opponent bodies on board.

**Fix.**
1. Add a card param (`trick_target_any_creature`) in `src/cards/data/cards.json` for the tricks whose
   printed text is "target creature" rather than "creature you control": Fists of Flame, Ancestral
   Anger, Expedite, and also Gold Rush / Scale the Heights ("up to one target creature"). Twinflame
   stays own-only. **Verify each against Scryfall first** and record the reasoning in the
   `[bracket note]` these entries already carry.
2. **Addressing.** `enchant_target` keys on `m_number`, and opponent spawns carry `m_number == 0`
   (visible in the reference JSON). Follow the precedent recorded in `tools/play/DECISIONS.md` for
   `ChooseSacOutletVictimIndex` — address opponent creatures by **battlefield index** through a
   distinct encoding (e.g. a negative sentinel `-(index+1)`) rather than minting fake numbers, and
   resolve it in `ResolveSoloTargetTrick` (`src/core/SpellEffects.h` ~4132-4183) alongside the
   existing number and name lookups.
3. Extend three consumers in lockstep, or the change is a silent no-op somewhere: the enumeration
   above; `ResolveSoloTargetTrick`'s resolution **and** its human chooser candidate filter (~4220-4229,
   currently `p.controller_index == controller`); and `MirrorwingProvider::TrickTargetCandidates`
   (`src/ai/DecisionProviders.cpp` ~6386), whose search prune would otherwise drop the new candidates.
4. **Magnet interaction — check this explicitly.** A copy magnet copies a spell "that targets only
   this creature". A trick aimed at an *opponent's* creature must not fan out; confirm the `magnet`
   test at `SpellEffects.h` ~4249-4252 stays false in that case.

GT moves (new castable lines). Suite + audit + rebaseline.

---

## #4 — Twinflame + Gold Rush rejected as one line · VIEWER GAP (+ a search check)

**The rejection was arithmetically correct.** From the (gitignored) artifact, seed 10 game index 9,
turn 4 pre-main:

* Board: Forest, Mountain, Ignoble Hierarch, Goblin Instigator, a 1/1 Goblin token, Mirrorwing Dragon;
  Sandstone Needle already in the graveyard. **3 available mana.**
* Hand: Elvish Mystic, Gold Rush, Mirrorwing Dragon, Twinflame.
* Attempted line `cast=Twinflame;cast=Gold Rush` = `{1}{R}` + `{1}{G}` = **4 mana**.
* Verdict `illegal`, reason "can't pay {1}{G} for 'Gold Rush' with the mana available this phase".

The better line the user wanted is Gold Rush **first** — targeting Mirrorwing Dragon, the magnet
copies it for each other creature, each copy makes a Treasure (4 Treasures) — and *then* Twinflame off
the Treasures.

**Can the search run that line? By design, yes.** `TurnSolver.cpp` ~8717-8754 arms the trick-class
**deferred breakpoint (site 5)** for `creates_treasures > 0`, searchable by default — that is what
commit `2ffcc38` landed, and the comment there explains why Gold Rush must take the deferred path and
never the inline one. In human play the same branch is skipped (`!s_human_play`) and the engine
instead hands the player a fresh main-phase decision with the Treasures already on board — visible in
the references (seed 12 decision 10 and seed 15 decision 16 both show a post-resolution `main_phase`
with 4 Treasure Tokens). **So the line is playable today, in two commits; the viewer just cannot
express it as one.**

**Work items.**
1. **Prove the search line** with a `--scenario` fixture (`test/scenarios/`, see
   `scenario-harness.md`) reproducing the board above and asserting the autonomous engine casts Gold
   Rush and then Twinflame off the Treasures in the same turn. Cheap, and if it fails that is a
   search defect that outranks the viewer work.
2. **Make the viewer able to represent it.** Add a *carry-over queue*: when `--validate-line` rejects
   for affordability and the queued line contains a spell whose resolution creates mana
   (`creates_treasures` / `ritual_float`) sufficient to cover the shortfall, commit the payable prefix
   and re-queue the remainder against the follow-up decision, so the two-step line plays as one user
   action. **Surface it in the history** ("split: Gold Rush first, Twinflame off its Treasures") — a
   silent split would hide genuine rejections. Sites: the reject path in `tools/play/index.html`
   (~1390-1440) plus `S.plan` carry-over state.
3. #8 makes this natural: with Treasures implicit, the second step needs no Treasure clicks.

---

## Suggested order

1. **Viewer / protocol, no GT** — #1, #8, then #4's carry-over queue.
2. **#4 step 1** (the scenario proving the search line) — cheap, and it decides whether a search fix
   jumps the queue.
3. **#3** — plain bug fix plus the placeholder-cost audit; smallest GT blast radius of the four.
4. **#5 + #2** behind `MTG_RESERVE_TIERED`, then **#7** behind `MTG_FLOAT_COLORS`, then **#6**. One
   A/B arm each — never stack two unadopted flags in one measurement.

Note that #2, #5 and #7 all edit `BatchPrepayMainCasts`, #5 and #2 being literally the same edit and
#7 the twenty lines that follow. Do not split those across parallel workers.

## Verification

* Build with `./build.sh` (never raw `cmake` — a bare invocation leaves `CMAKE_BUILD_TYPE` empty and
  silently compiles at `-O0`).
* **Byte-identical smoke after each viewer-only change.** #1 and #8 must be GT-neutral; if smoke moves,
  the change leaked into the search.
* `bash test/viewer_checks.sh` — the protocol, linebuild and validate-line layers guard exactly the
  paths #1/#4/#8 touch.
* Replay every saved Mirrorwing reference and confirm all twelve still validate. **Never**
  `git checkout` / `restore` / `clean` anything under `references/` — those are user-owned and
  commit-only.
* Manual checks in the viewer on the exact reported games: seed 7 T4 (#1, #2), seed 9 T2 (#3),
  seed 11 T5 (#5), seed 15 T4/T5 (#6, #7, #8). Seed 10 has no saved reference — drive it from
  `--seed 10 --game-index 9` for #4.
* For each GT-moving change: `regression` mode A/B on train seeds, per-game audit of the diff, report
  win% / avg-win-turn to the user, adopt on approval, then `--accept`. Never `--accept` on aggregate,
  and never re-run the suite to regenerate GT.
* Long runs go through a single pooled `mtg --batch` manifest, never wrapped in `timeout`; check the
  `[batch] heartbeat` line inside the first ten minutes.
