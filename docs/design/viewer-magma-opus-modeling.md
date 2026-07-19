# Magma Opus: divide surfacing + unmodeled "tap two" / Treasure clauses (issue #9)

Status: **PARTIALLY SHIPPED — the FUNCTIONAL blocker is FIXED (GT-neutral); the full-faithful discount
is a deferred, GT-negative correctness improvement awaiting supervised sign-off.**

## 2026-07-19 — the "prevented from casting" blocker is FIXED (WS6a, GT-neutral, shipped)

The actual reproduction question ("does the human's Magma cost track the divide targets or the
all-permanents over-count?") is resolved by the code: the cast cost is finalized at plan ENUMERATION
via `HinataGenericDiscount`; the `divide` chooser does NOT feed back into cost. So the human is not
blocked by the divide at all — they were blocked because **`CheckLine` (the viewer's `--validate-line`
path) charged the full printed `{6}{U}{R}` and never applied the Hinata discount**, so a hand-built
Magma line with Hinata in play was rejected as unpayable ("I needed more targets to cast"). Fixed by
seeding `pc.full_cost` from `EffectiveCost` (commit on `phase-1-2-deck-analyzer`); CheckLine is
viewer-only ⇒ smoke 18/18 byte-identical, per-game audit 0 flips. **The human can now hand-build and
cast Magma Opus at the discounted cost, then the `divide` panel opens to split the 4 damage (opponent
creatures included).**

## What's LEFT (deferred, GT-negative, needs sign-off): the full-faithful discount + tap-two

The autonomous SEARCH still uses the over-count `HinataAvailableTargets = 2 + every permanent`, which
keeps Magma cheap ({U}{R}) even when the board can't supply 6 distinct targets. Making the discount
faithful is GT-negative (Magma gets more expensive when the spread can't reach 6 distinct). The user
chose "full faithful, heuristic does most of the work," but also observed **"the autonomous is probably
not that much off"** — so the GT gain is expected to be small, and this is a correctness/faithfulness
polish, not a blocker. Recommend implementing in a SUPERVISED session (per
`.claude/skills/heuristic-optimization.md`: escape-hatch, sweep train seeds, validate on overnight,
report, adopt on approval) rather than unsupervised, because it needs lockstep changes across the three
resolution sites (EffectHandler / ApplyPlanDirect / Solve) plus a new tap-two model.

### Lower-risk implementation than the full declared-count enumeration

The user's own framing — "the only real decision in goldfishing is whether to pay more mana to deal
more damage to the opponent" — means the SEARCH does **not** need to enumerate every declared target
count (the heavy Crackle-style multi-plan machinery). A single **heuristic-chosen** line per board
state suffices, gated on lethality, computed identically at enumeration and resolution so the discount
and the damage stay lockstep:

- **Concentrate line** (opponent at/near lethal from Magma face damage): all 4 damage on the opponent
  face = 1 damage-target + 2 tap = **3 distinct** → `{3}{U}{R}` (pay more, deal 4 to face).
- **Spread line** (default, developing): 1 damage each to **your own creatures first**, then the two
  players (self + opponent), then an opponent creature only if needed to reach the count, up to 4
  damage-targets; + tap 2 no-cost permanents = **5–6 distinct** → `{1}{U}{R}` / `{U}{R}` (cheapest,
  but only ~1 to the face).

`HinataAvailableTargets` (behind `MTG_MAGMA_FAITHFUL`, default off) returns the count the heuristic
actually hits; the resolution applies the SAME spread + taps. Because the heuristic is a pure function
of the board, the enumerator and the resolution agree ⇒ lockstep, conservative (never the cheap-cost-
plus-full-face-damage over-rating the current model allows). Measure the smoke/overnight delta with the
hatch ON and present before flipping the default / rebaselining Hinata GT.

## IMPLEMENTED behind `MTG_MAGMA_FAITHFUL` (default OFF) — 2026-07-19

Shipped the minimal, lockstep-correct version: since the SEARCH resolves Magma as ALL damage to the
opponent face (1 damage-target, [TurnSolver.cpp:3508](../../src/ai/TurnSolver.cpp#L3508); the divided
allocation is human-only), the faithful distinct-target count for the search's actual line is
**1 (opp face) + 2 (the oracle's mandatory "tap two target permanents") = 3**, capped by
`min(2, #permanents)`. `HinataAvailableTargets` returns that when the hatch is on, instead of the
`2 + every permanent` over-count. No resolution change needed (the tap is goldfish-inert; only the count
is load-bearing, and Magma always taps 2, so the count is faithful, not a phantom). Magma's cost goes
`{U}{R}` → `{3}{U}{R}` on a permanent-rich board. Scoped to `damage_divided && discount_targets_permanents`
(Magma only; Reality Spasm is `scale_x` untap, excluded).

**Measured delta (metric = mean turn-to-win, unwon=max+1; lower better):**
- Hinata d0, seed 1001, 1000 games: OFF **7.4760** (== GT, byte-identical) → ON **7.4910** = **+0.015**.
- Tiny GT-negative, matching the user's "the autonomous is probably not that much off." (d5 search-path
  delta pending; expected similar or smaller since the search plans around the higher cost.)

This is the CONCENTRATE line (4-to-face, `{3}{U}{R}`) — the goldfish-value line the search already
plays. The user's cheaper SPREAD default (`{U}{R}`, 1-to-face) would need the search resolution to
actually spread damage onto own creatures (a bigger change to TurnSolver.cpp:3508 + the two other
lockstep sites) and a lethality gate to concentrate when near-lethal; deferred as a follow-up if the
spread option is wanted. Adoption of the concentrate faithful discount = flip the default + rebaseline
Hinata GT (+0.015).

## What was confirmed (2026-07-19)

- Magma Opus **casts fine** and the `divide` decision surfaces (reproduced Seed 5 Game 4). The divide's
  `legal_targets` include opponent creatures (`CollectDamageTargets(players_only=false)`), and opponent
  permanents carry a board `idx` (`JsonBattlefield` for both players), so the divide stepper attaches to
  them exactly as it does to your own Hinata (which works). So there is **no separate "can't select
  opponent creatures" wiring bug** — the practical block was reaching the target COUNT.
- **Root cause of "prevented from casting / needed more targets":** Hinata discounts per DISTINCT
  target. Real Magma Opus reaches 6 targets = **4 damage (spread, ≤2 to face) + 2 tapped permanents**.
  The model **omits tap-two**, so the human maxes at the ≤4 divide targets and can't reach the discount
  the cheap `{R}{U}` line needs. Dumping all 4 damage on the face = 1 damage-target (poor spread).
- The model's autonomous discount uses `HinataAvailableTargets = 2 + every permanent`
  ([SpellEffects.h:2420](../../src/core/SpellEffects.h#L2420)) — a free over-count (the "reduction
  without distinct targets" issue). It keeps the SEARCH's Magma cheap, so autonomous play is roughly OK.

## The fix (user design)

1. **Tap-two as a separate target step** — a new decision to choose 2 target permanents to tap; those
   are 2 more DISTINCT targets that earn the discount. (Tap effect itself is ~inert vs a passive
   goldfish opponent; the target COUNT is what matters.)
2. **Distinct-target discount** — Hinata's reduction counts the distinct targets actually chosen
   (divide damage targets ∪ tap targets), not `2 + all permanents`. More faithful.
3. **Autonomous heuristic (goldfishing).** Under the distinct-target discount the search's real choice
   is: *spread* the 4 damage 1-each across cheap **distinct** targets, then tap 2 lands, vs. concentrate
   damage on the opponent face (fewer targets → more mana, more face damage). "The only real decision in
   goldfishing is whether to pay more mana to deal more damage to the opponent" (user). Default to the
   max-discount spread (cheapest cast); concentrate face damage only when the extra points are worth the
   extra mana (lethal / near-lethal). Heuristic tuning (see `.claude/skills/heuristic-optimization.md`)
   on top of the faithful model.

   **Exact cost ladder (user).** The 4 damage-targets are: **the two players (opponent + self) + Hinata
   + one other creature** (yours or the opponent's — *sometimes absent*), 1 damage each; plus **tap 2
   lands**. Magma Opus is `{6}{U}{R}`; Hinata discounts `{1}` per DISTINCT target (cap 6):
   - 4 damage-targets + 2 tap = **6 distinct** → `{U}{R}` (2 mana).
   - no 4th creature → 3 damage-targets (put ≤2 on a face) + 2 tap = **5 distinct** → `{1}{U}{R}` (3 mana).
   Distinctness is load-bearing: a permanent targeted twice earns only one `{1}`.

   **Damage-target priority (user).** For the 1-damage spread, prefer **your OWN creatures first** —
   all friendly creatures survive 1 damage, so it's a free distinct target, and it leaves the
   opponent's 1/1s alive (they stay useful as future targets). Order: own creatures → the two players
   (self + opponent, both safe/relevant) → an opponent creature ONLY if no other target exists to
   reach the count (killing a 1/1 to hit the target count is fine as a last resort). Tap targets =
   2 permanents that don't cost you (opponent's, or your own already-tapped lands — never untap-needed
   lands).

## The GT tradeoff (needs sign-off before rebaseline)

The Soulfire fix moved GT positively (more reach). Magma is the opposite: switching the autonomous
discount from `2 + all permanents` to **distinct chosen targets** makes Magma Opus *more expensive* for
the search (it can only count what it targets), so it likely casts later / less — a **GT-negative**
move, though a more faithful one. Options:
- **(A) Human-path only:** add the tap-two step + distinct-count for the VIEWER/human cast (like the
  Soulfire chooser is human-only), leaving the autonomous discount as-is → GT-neutral, unblocks the
  human, but keeps the search's over-count.
- **(B) Full faithful:** change the discount everywhere → GT-negative but correct; rebaseline Hinata.

Recommend confirming A vs B with the user. Reference-safe either way (no reference casts Magma —
none replay a `divide`). Read `.claude/skills/mtg-rules.md` before implementing.

## Problem (as reported)

Seed 5 Game 4 (Hinata): the user "cannot divide Magma Opus damage to opponent's creatures, nor select
the lands," which "prevented me from casting Magma Opus even though there were plenty of targets."

## What the model has vs. omits

`cards.json` — **Magma Opus** `{6}{U}{R}` Instant, `template:"direct_damage"`,
`{ damage:4, targeting:"any", cast_draw:2, discount_max_targets:6, discount_targets_permanents:true,
damage_divided:true }`. Real oracle: "deals 4 damage divided among any number of targets. **Tap two
target permanents.** Create a 4/4 Elemental. Draw two cards. `{U/R}{U/R}, Discard: create a Treasure`."

The model's own note flags the omissions: "**STILL omitted** … the 4/4 Elemental token, tap-two (inert
vs a passive opponent), and the `{U/R}{U/R}`-discard-for-Treasure ramp mode." So:

- **"select the lands" (tap two permanents)** is literally not modeled → **no engine decision is
  emitted**, so the viewer has nothing to render. Matches that half of the complaint exactly.
- **Divide IS implemented and uncapped** — engine `WriteDivideDecisionJson`
  ([src/main.cpp:788](../../src/main.cpp#L788)) + viewer `wireDivideBoard`/`commitDivide`
  ([tools/play/index.html:1142](../../tools/play/index.html#L1142)). Magma Opus is flagged
  `damage_divided:true`, and `CollectDamageTargets(players_only=false)` includes opponent creatures.
  So dividing 4 among opponent creatures is *representable* — **when the divide sub-decision actually
  reaches the human**. The divide panel only opens during real cast execution; if the Magma Opus cast
  is folded into a committed combo plan (choices supplied) or the cast is never reached, the panel
  never opens — the likely reason "cannot divide … prevented casting."

## Fix (deferred, staged)

1. **Reproduce** `--seed 5 --game-index 4`, step to the turn-4 Magma Opus draw, and dump the emitted
   `main_phase` plans + whether a `divide` sub-decision fires on the cast. Pin down whether the blocker
   is (a) the plan enumerator not offering the cast at that mana, or (b) the `divide` sub-decision not
   surfacing interactively.
2. **Surface the `divide` sub-decision for interactive casts** so the human can split the 4 among
   opponent creatures (not only the autonomous all-to-face default).
3. **Model the omitted clauses** (per `.claude/skills/mtg-rules.md`, conservative-then-validated):
   add a "tap two target permanents" decision (new decision `type` + viewer panel reusing the
   target-selection UI) and, if wanted, the 4/4 token and `{U/R}{U/R}` Treasure alt-mode.
4. Engine changes move **Hinata GT** (Magma Opus is a Hinata 1-of) → rebaseline per
   `.claude/skills/regression-testing.md` and regen the Hinata profile on a frozen commit; disclose in
   the Stage-6a note.

## Verification

Cast Magma Opus interactively in the repro: divide 4 among opponent creatures, choose two permanents to
tap, confirm the 4/4 token + draw-two land. Regression-suite audit before `--accept`.

---

## 2026-07-19 (session 2): per-game analysis → CHOSEN MODEL = two-variant search (NOT yet implemented)

### Measured deltas (Hinata d0, seed 1001, 1000 games; metric = mean turn-to-win, unwon=max+1)
- Over-count (current GT, default): **7.4760**.
- CONCENTRATE faithful ({3}{U}{R}, all 4 to face): **7.4910 (+0.015)**. Committed `4536d64` then edited.
- Lethality-gated SPREAD ({U}{R}, ~1 to face unless opp_life<=4): **7.5020 (+0.026)** — WORSE.
  (This is the CURRENTLY-UNCOMMITTED / then-committed state: `MagmaFaithfulPlan` in SpellEffects.h +
  the face-damage override in EffectHandler::ResolveDirectDamage + ApplyPlanDirect.)

### Why spread is worse (the key finding — measurement refutes intuition)
Worked example gi=0 (Magma cast turn 5 with **Hinata + Ornithopter** on board = 4 damage-targets + 2 tap
= 6 distinct available → legitimately `{U}{R}`): the concentrate model IGNORES creatures (counts only
opp+2tap=3) so it over-charges `{3}{U}{R}` and delays 6→7. BUT across 1000 games the deck **values
Magma's 4-to-face more than the mana the spread saves** — dropping face 4→1 to get the cheap cost is a
net loss (+0.026). So "were there targets to reduce cost / was the face damage needed": targets existed,
but at the goldfish level the face damage IS generally worth it. The over-count is fastest precisely
because it is doubly over-rated (cheap AND 4-to-face); any single-line faithful correction is slower.

### CHOSEN MODEL (user, session 2): two-variant search — "max affordable vs 1 damage"
Make Magma a **branch point** the search resolves, NOT a fixed line, because 4-to-face costs 5 mana
({3}{U}{R}) and isn't always affordable. Enumerate TWO Magma variants (NO in-between levels — user:
"we just choose based on what the plan allows"):
- **Cheap**: 1 to face, spread the other 3 across distinct targets → max discount → `{U}{R}` (or
  `{1}{U}{R}` w/ few creatures). Frees mana for other casts.
- **Concentrate (max affordable)**: as much face damage as the plan's leftover mana pays for, up to 4
  (`{3}{U}{R}`). Only affordable in mana-rich plans; the affordability check drops it otherwise.
The search's normal plan enumeration + affordability + value picks per-game → should land ~neutral
(keeps cheap Magma when mana is the bottleneck, concentrates when it can afford it). Face F costs (F+1)
total mana: F=1→{U}{R}, F=2→{1}{U}{R}, F=3→{2}{U}{R}, F=4→{3}{U}{R}; discount(F)=min(6, 1 +
min(4-F,1+#creatures) + min(2,#perms)).

**Crackle interaction (user):** if the cheap-Magma-frees-mana line competes with Crackle for the same
mana, **prioritize a higher Crackle X** (Crackle's 5X face damage usually wins the race).

### Implementation plan (mirror Crackle's declared-count machinery)
Crackle already does exactly this: `TurnSolver.cpp:1233-1294` loops X-candidates × declared `count`,
sets `a.crackle_targets`, threads it `Action → cast_by_name(AIEngine.cpp:1881+ / :3124 :3146) →
StackEntry.crackle_targets (GameState.h:101) → ResolveDirectDamage / ApplyPlanDirect`, and derives the
discount from the count (`HinataGenericDiscount(def,state,x,count)` SpellEffects.h:2502). For Magma
(fixed-cost, so the FIXED-cost enumeration path near TurnSolver.cpp:1360-1520, NOT the X-loop):
1. When `IsMagmaFaithful(def.params)` && Hinata in play, emit TWO Action variants sharing hand_index
   (mutually exclusive): a spread variant (face=1) and a concentrate variant (face=4). Set each
   variant's `a.cost` directly from discount(F), and thread the face via a field (REUSE `crackle_targets`
   as the "extra spread targets beyond the face" count = 4-F, so 3 for spread / 0 for concentrate, OR add
   a dedicated `magma_face`). Reuse avoids threading a new field through the ~10 cast_by_name call sites.
2. Extend `cast_by_name` (AIEngine.cpp:3146) to also set `entry.crackle_targets` for IsMagmaFaithful.
3. In ResolveDirectDamage + ApplyPlanDirect, for Magma compute `face = damage - entry.crackle_targets`
   (concentrate=0→4, spread=3→1) and deal that to the opp face. REMOVE the lethality-gate
   `MagmaFaithfulPlan` face override (superseded).
4. Discount per variant: distinct = 1 + crackle_targets + min(2,#perms) → cost via the enumerator (like
   Crackle's `HinataGenericDiscount(def,state,x,count)` derived path; Magma is non-X so compute directly).
5. Keep `MTG_MAGMA_FAITHFUL` gate (default off, byte-identical). Build → measure d0/d5 vs GT → per-game
   audit → accept only if the delta makes sense. Read `.claude/skills/heuristic-optimization.md`.

### Current code state at compaction
`MTG_MAGMA_FAITHFUL` behind-the-hatch behavior = the LETHALITY-GATED SPREAD (measured +0.026, the worse
one). It is to be REPLACED by the two-variant model above. Default OFF ⇒ all GT byte-identical (d0 OFF =
7.4760 verified). Committed so it's not lost; the two-variant is the next step.

---

## 2026-07-19 (session 3): IMPLEMENTED — general "scaled cast" mechanism, model in the provider

### Architectural pivot (user directive)
The user reframed the invasive part to be **general**: "treat this case similarly to how you would scale
{X} spells if you have extra mana." Magma's opponent-face damage is a scaled OUTPUT (like {X}) — more
face ⇒ fewer distinct spread/tap targets ⇒ less Hinata discount ⇒ more mana. The rules:
- **No card-specific branches in general logic.** The engine gets a general **scaled-cast** mechanism
  (emit one mutually-exclusive cast per output level, thread the committed level to resolution, deal it);
  the whole Magma MODEL (which face levels, what each costs) lives in the **HinataProvider** — "within the
  deck-specific heuristics we can do whatever card-specific logic we want."
- **The search picks by default; a heuristic may narrow** (like `XCandidates`). With several scaling
  spells in hand (Crackle {X} + Magma face) the search allocates spare mana across them — Crackle's X
  moves in 3-mana steps, Magma's face in 1-mana steps, so the fine-grained face soaks up mana the coarse
  X can't. "By default the search must pick, but we'll override with a heuristic here."

### What was built (all behind `MTG_MAGMA_FAITHFUL`, default OFF ⇒ byte-identical)
- **`DecisionProvider.h`**: general `struct ScaledCastVariant { int face; ManaCost cost; }` + Hook 28
  `ScaledCastVariants(state, def)` (base returns `{}` ⇒ every other deck's single-line cast, byte-identical).
- **`HinataProvider::ScaledCastVariants`** (`DecisionProviders.cpp`): the DECK-SPECIFIC cost model.
  Gated on the flag + Magma-by-DATA (`damage_divided && discount_targets_permanents`) + Hinata in play.
  For each face `F` in `1..damage`: `distinct(F) = 1 (face) + min(damage−F, 1+#creatures) (spread) +
  min(2, #perms) (tap)`; `discount(F) = min(discount_max_targets, distinct(F))`;
  `cost.generic = printed − discount(F)`. Dominated levels (a HIGHER face at the SAME cost) are dropped.
- **`TurnSolver::CollectActions`**: a general block — for a `DirectDamage && damage_divided` spell, ask
  the provider for variants; if non-empty, emit one cast per `{face, cost}` (thread the face on
  `crackle_targets`, the searched-scalar carrier) and `continue`; else fall through (byte-identical).
- **Lockstep**: the committed face rides on `crackle_targets` (Magma is not `IsCrackleCountSpell`, so no
  Crackle resolution fires). Resolution deals `face` in `EffectHandler::ResolveDirectDamage` and
  `ApplyPlanDirect` (gated on `damage_divided && crackle_targets>=0`). The cost is recomputed from the
  committed face via the provider on the CURRENT board at BOTH the rollout (`apply_one`) and the executor
  (`CastSpellFromHand`) — the same recompute-from-the-searched-count pattern as Soulfire/Crackle — so
  enumeration/CanPay/rollout/executor price the same face identically. `cast_by_name` gate extended to set
  `entry.crackle_targets` for a scaled Magma variant.
- **Removed** the old single-line `IsMagmaFaithful` / `MagmaFaithfulPlan` (SpellEffects.h) + its two
  resolution overrides; `HinataAvailableTargets` reverts to the plain over-count as the OFF fallback.

### Measured (Hinata; metric = mean turn-to-win, unwon=max+1; lower better)
| depth | seeds | games | OFF (== GT) | ON (scaled) | Δ | per-game win-turn changes |
|-------|-------|-------|-------------|-------------|-----|---------------------------|
| d0 (greedy) | 1001 | 1000 | 7.4760 | 7.4840 | **+0.008** | 14 (11 slower, 3 FASTER) |
| d5 (search) | 1001 |   75  | 6.0400 | 6.0400 | 0.000 | 0 changed *(small-sample fluke — see below)* |
| **d5 (search)** | **4004/5005/6006/7007** | **1200** | **6.1467** | **6.1633** | **+0.0167** | **22 (20 slower, 2 FASTER)** |

Per held-out seed the ON delta is CONSISTENTLY positive (+0.0134 / +0.0167 / +0.0133 / +0.0234), so the
tiny 75-game d5 "0 changed" at seed 1001 was a **small-sample fluke, not true neutrality**. At the real
held-out scale (1200 d5 games) the faithful model is a **small, consistent GT-negative of ~+0.017** — the
same magnitude as the concentrate single-line's d0 +0.015.

**Honest conclusion: no faithful Magma model beats the over-count for the goldfish.** The over-count is
doubly over-rated — it lets Magma be BOTH cheap (`{U}{R}`) AND deal 4-to-face, an impossible line that
genuinely wins ~0.017 turns faster on average. The two-variant/scaled model is the *least-negative* and
*most correct* faithful option (far better than concentrate +0.015 / spread +0.026 as single lines, and
only 22/1200 games move — 20 slower by 1 turn, 2 faster), but it is **NOT free**. Adoption is therefore a
genuine correctness-vs-goldfish-speed tradeoff, not a free win.

**Mechanism verified** (60 games, d0, ON): Magma cast-cost distribution `{3}{U}{R}`×8 (concentrate 4-face
when affordable), `{U}{R}`×1 (cheap spread to free mana), `{6}{U}{R}`×2 (no Hinata ⇒ provider returns `{}`
⇒ generic path, full cost, byte-identical). The search genuinely picks the face level per plan.

### Per-game audit of all 22 changed d5 games (user criterion: adopt iff the faster OFF lines are impossible)
Reproduced every changed held-out d5 game single-threaded (`--seed job.seed+gi --game-index gi`, matches
the batch for all Magma-casting games):
- **17 games — OFF's faster win used a PROVABLY IMPOSSIBLE Magma line.** Every one cast Magma at `{U}{R}`
  (2 mana) dealing 4-to-face. Legal cost for 4-to-face = 1 target + 2 tap = 3 distinct = `{3}{U}{R}` (5
  mana). The over-count credited a `{1}` discount for all ~6 permanents it never targeted AND dealt the
  full 4 to the face — a line that cannot be played. These +1-turn "regressions" are the correct removal
  of an illegal free lunch.
- **5 games — batch noise, not real** (4004/213, 5005/283, 5005/291, 7007/122, 7007/264): none cast Magma
  in the OFF line, and in deterministic single-thread repro they are NEUTRAL (OFF == ON) or don't even
  reproduce the batch win turn (283: batch OFF won T8, repro OFF unwon). Known CPU-oversubscription
  nondeterminism flipping borderline games, unrelated to the Magma change.

**INITIAL conclusion (WRONG, corrected below): "every reproducible faster OFF line is impossible → adopt."**

### CORRECTION 2026-07-19 (session 3b): the +1 games are SEARCH OVER-CONCENTRATION, not cheat-removal
The over-count's specific line ({U}{R} for 4-to-face) IS impossible, but the WIN it reached was usually
LEGALLY reachable a turn sooner — the audit's "impossible" framing missed that. Worked example gi163 (OFF
T6 → faithful T7): OFF cast Magma cheap ({U}{R}, chip) and the **Crackle combo** did the kill (opp 12→-3);
the faithful SEARCH instead paid `{3}{U}{R}` (concentrate, 5 mana) for Magma's 4-face, **starving the
lethal Crackle** → slipped to T7. So a legal T6 line existed (cheap 1-face Magma + bigger Crackle) and the
search didn't pick it. Eval-neutralising the variants (removing `eval=face*100`) recovered NONE — the
over-concentration isn't an eval-pruning artifact.

**Fix (user directive): Crackle-reserve rule** — when a Crackle is in hand it competes with Magma for
mana, and Crackle is the better sink (3 mana → 5 damage vs Magma's 3 mana → +3 face), so RESERVE mana for
Crackle: Magma emits ONLY the cheap 1-to-face variant when a Crackle is in hand. Implemented in
`HinataProvider::ScaledCastVariants` (off-switch `MTG_NO_MAGMA_RESERVE`). Recovers **11/17** audited d5
games. BUT d0 aggregate got WORSE (over-count 7.4760 → faithful-no-reserve 7.4840 → faithful+reserve
7.4920): "always reserve" helps the d5 SEARCH (uses the reserved mana for Crackle) but hurts greedy d0 (no
lookahead to spend it). The **net d5 aggregate with reserve is UNMEASURED** (the +0.017 held-out d5
validation predates the reserve rule).

### Adoption status: UN-ADOPTED 2026-07-19 — faithful+reserve is OPT-IN (`MTG_MAGMA_FAITHFUL`), default OFF
Reverted the premature adoption: default is the over-count single line again (original GT restored,
smoke back to 7.4760/6.0133/6.0400). All the work — the general scaled-cast mechanism + the Crackle-reserve
rule — is preserved behind `MTG_MAGMA_FAITHFUL` (enables faithful+reserve; add `MTG_NO_MAGMA_RESERVE` to
A/B the reserve rule). **NEXT: re-measure the held-out d5 aggregate WITH the reserve rule** (vs over-count);
audit the residual 6 still-slower games (4004_115, 5005_73, 5005_189, 7007_1, 7007_107, 7007_269 — are
these forced, or a different search miss?); only then decide adoption + rebaseline. The bincache
(`26683d9`) and the general mechanism are independent and stay.

--- (superseded) original adoption note below ---
### (superseded) Adoption status: ADOPTED 2026-07-19
Committed behind `MTG_MAGMA_FAITHFUL` (default OFF ⇒ byte-identical) at `4658d4f`. Adopting = flip the
default (drop the gate, add an `MTG_LEGACY` escape hatch) + `regression.sh --accept` to rebaseline Hinata
GT (d0/d3/d5 avg + all play digests move). **The held-out d5 validation shows adoption costs ~+0.017 avg
turn-to-win** (consistent across the 4 overnight seeds) — small but real, not free. So this is a
correctness call, not a metric win:
- **FOR adopting**: the over-count models an IMPOSSIBLE line (Magma cheap AND 4-to-face); the faithful
  model is honest, prices each face level correctly, and the search still picks sensibly (concentrate
  when it can afford it, cheap spread to free mana for Crackle). Only 22/1200 d5 games move.
- **AGAINST**: it's a ~+0.017 goldfish slowdown at every depth, with no compensating metric gain — the
  goldfish genuinely wins faster on the fiction.
Presented to the user for the decision (per `heuristic-optimization.md`: adopt only on approval).
