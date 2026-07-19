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
