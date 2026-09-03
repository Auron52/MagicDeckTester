# Land-signature completeness: an incomplete dedupe key made land abilities unreachable

**Status (updated 2026-09-03):** ADOPTED 2026-07-30 (cb806c84, dynamic land tapped-ness;
`MTG_LAND_CLOSING_WINDOW` default ON), GT rebaselined 2026-08-02 (c89a7c25). The signature fix
itself remains opt-in as `MTG_LAND_SIG_COMPLETE` (the `MTG_LEGACY_LAND_SIG` hatch named below
does not exist).

**Status:** BUILT, measured, GT-rebaseline pending (2026-07-29). Found while measuring the
demand-driven colour-bucketing idea from
[plan-odometer-factorization.md](plan-odometer-factorization.md) — the colour idea measured dead,
but the audit it required surfaced this.

## The defect

[`EnumeratePlansWithLand`](../../src/ai/TurnSolver.cpp) enumerates one plan per distinct land
**static signature**, not per land card:

```cpp
const std::string key = s_human_play_lands ? c.m_name : land_sig(def->params);
if (seen_key.insert(key).second) { land_names.push_back(c.m_name); }
```

Only the surviving representative is offered as a land play. So when two behaviourally *different*
lands hash to the same signature, the non-representative land is **never played by the search** —
and every ability it has is unreachable, for as long as a same-signature land sits in hand.

`land_sig` covered mana + ETB (produces, produces_amount, enters_tapped, etb_pay_life_to_untap,
etb_scry, etb_untap_reveal_subtypes, enters_tapped_with_depletion, no_max_hand_size, is_filter,
cycling_cost, sacrifice_draw_cost, creature_mana_only, fetch_land_types, mdfc_back_*). Auditing
**every** land parameter in `cards.json` against it found **20 omitted**:

```
animate_cost animate_power animate_toughness can_animate colored_creature_only
etb_bounce_land etb_surveil fastland_max_other_lands ramp_filter reflecting
storage_charge_mode storage_land tap_opponent_lifegain tap_self_damage
tap_token_cost tap_token_power tap_token_requires_subtypes tap_token_subtypes
tap_token_toughness taps_spawn_opp_token
```

Three were **live collisions in suite decks** (not hypothetical):

| deck | collided lands | the difference that was invisible |
|---|---|---|
| Auras | Brushland vs Razorverge Thicket | pain land (`tap_self_damage`) vs fastland (`fastland_max_other_lands`) — the fastland enters **tapped** once 3+ other lands are out, which the static `enters_tapped=false` cannot express |
| Dragonstorm | Dwarven Hold vs Mercadian Bazaar | storage lands differing only in `storage_charge_mode` (`upkeep_if_tapped` vs `tap`) |
| slivers_vial | Sliver Hive vs Cavern of Souls / Secluded Courtyard / Unclaimed Territory | Hive's `{5}, {T}`: create a 1/1 Sliver was unreachable |

A worked case (Auras `regression_d3_s2002` gi194 — identical kept hand and draws):

```
T4  old: land Razorverge Thicket; Ancestral Mask; ATTACK              [opp -13]
T4  new: land Brushland; Ancestral Mask; Hyena Umbra; ATTACK          [opp -19]
```

Razorverge was the representative and enters tapped on T4; Brushland does not. With Brushland
reachable the deck casts an extra aura.

`reflecting` (Reflecting Pool) and `taps_spawn_opp_token` (Forbidden Orchard) were dormant, not
live — Orchard and Pool were kept apart only *accidentally*, by their differing colour lists. That
matters for the colour idea below: demand-capping the colour list would have removed the accident
and collapsed a drawback land into a clean one.

## Why splitting on everything is the wrong fix (measured)

The obvious fix — add all 20 params to the signature — is correct but expensive. Splitting
Sliver Hive out of the Cavern/Courtyard/Territory group doubles the enumerated land branch whenever
both are in hand:

| deck | instructions (callgrind Ir) vs legacy signature |
|---|---|
| slivers_vial d5 | **+60.84 %** |
| Auras d5 | +1.92 % |
| Dragonstorm d5 | +0.01 % |

For **zero** measured quality gain (see below). Not worth it.

## The fix: split only what is incomparable; otherwise promote the dominant land

Two lands with the same mana and ETB, where one has a **strictly-optional extra activated ability**,
are not a symmetric pair needing two branches — one *dominates*. Every line available with the
plainer land is available with the richer one (the ability may simply be ignored), so the group still
needs only one representative: **the richer one**.

- `land_sig` keeps the legacy fields **plus** every param that is a real behavioural difference:
  `colored_creature_only`, `reflecting`, `taps_spawn_opp_token`, `tap_self_damage`,
  `tap_opponent_lifegain`, `fastland_max_other_lands`, `etb_bounce_land`, `etb_surveil`,
  `ramp_filter`, `storage_land`+`storage_charge_mode`. Each appends **only when non-default**, so a
  land with none of them keeps a byte-identical signature.
- `land_bonus` collects the strictly-optional activated abilities (`can_animate`+`animate_*`,
  `tap_token_*`). It is **not** part of the key; instead the group's representative is the member
  carrying it.
- An ETB is *not* optional and must split — `etb_surveil` in particular alters the draw stream.

Guard against a future silent collapse: the promotion emits **one representative per distinct
non-empty ability set**, so two lands with *different* optional abilities (mutually incomparable)
each keep a branch. No deck has that shape today; it must not collapse if one is added.

`MTG_LEGACY_LAND_SIG=1` restores the pre-audit signature *and* the old first-in-hand-order
representative, for a byte-identical A/B.

## Measured result

Instructions (callgrind Ir, single-thread, seed 1001; no hardware PMU on this box — see the sibling
doc's method note):

| deck | vs legacy signature |
|---|---|
| slivers_vial d5 | **+0.42 %**  (was +60.84 % when split) |
| Auras d5 | +1.98 %  (Brushland vs Razorverge are genuinely incomparable — this branch is real) |
| treasure_hunt d3 | +0.44 % |
| Knights d5 | +0.13 % |
| burn d5 | +0.11 % |
| Anti-Lifegain d5 | +0.07 % |
| Hinata2 d5 | +0.05 % |
| Dragonstorm d5 | +0.04 % |

Quality (THE metric: avg turn-to-win, unwon = max_turns+1; negative = better):

| mode | configs changed | slower | faster | net avg delta |
|---|---|---|---|---|
| smoke | 2 / 24 | 0 | 0 | 0.0000 |
| regression | 6 / 40 | 0 | 0 | 0.0000 (every changed case's avg identical) |
| overnight (held out) | 13 / 96 | 2 | 1 | **+0.00001 mean** |

Both overnight slowdowns classify as **churn** — `test/classify_turn_later.sh` recovers each at 4×
and 16× its case budget, i.e. benign truncation from the slightly wider enumeration, not a real
regression. `d0` is byte-identical in all three modes (0 changed), as expected: the greedy policy
does not use this enumeration.

So the change is **quality-neutral and essentially free**, and buys correctness: a land's abilities
are reachable regardless of what else is in hand. The value is largely *latent* — it shows up as
"Sliver Hive is now played first so its token ability comes online sooner" (104 games in smoke alone)
rather than as a metric gain on today's decks, and it protects any future deck where a deduped
land's ability is load-bearing.

Dragonstorm's storage-land split never fired in ~120,000 games — correct but unexercised.

## The bigger find: the land-priority passes read tapped-ness from the STATIC flag

Chasing the human rule *"drop Razorverge Thicket early to avoid issues later"* (user, 2026-07-29)
turned up a plain bug — a **modeling** error in a heuristic's input, not a heuristic to tune
(Rule 0 of `.claude/skills/heuristic-optimization.md`).

`AIEngine::TryPlayLand`'s four-pass land priority — *prefer untapped-entering, prefer multi-colour* —
classified tapped-ness as:

```cpp
bool is_tapped = def->params.enters_tapped;      // Razorverge Thicket: false, even on turn 5
```

But three land classes carry `enters_tapped == false` and still enter **tapped** depending on runtime
state: a **fastland** (`fastland_max_other_lands`, board-dependent), a **reveal land**
(`etb_untap_reveal_subtypes`, hand-dependent) and a **shock land** (`etb_pay_life_to_untap`,
life-dependent). The static read placed all of them in the *untapped* passes even when they were
about to come down tapped — defeating the exact preference the passes exist to express.
`play_land_iter` then calls the dynamic helper to set the permanent, so the land was *played*
correctly; only the **priority ordering** was blind.

The same bug was mirrored in `TurnSolver`'s `greedy_land_name`, the search's last-resort land
tiebreak — so when the search was indifferent between land lines it defaulted to the wrong land.

**Fix:** use `LandWouldEnterTapped(state, def)` at both sites. Note it must be
`LandWouldEnterTapped` (pure, `const GameState&`) and **not** `LandEntersTapped`, which *pays the
shock life* as a side effect — calling that once per candidate in a priority scan would leak life.
`MTG_LEGACY_STATIC_TAPPED=1` restores the static read.

### Measured: strictly better, sign-stable on held-out seeds

| | train (regression) | held out (overnight) |
|---|---|---|
| net avg delta | **−0.0150** | **−0.0280** |
| cases better / worse | 2 / 0 | 9 / 2 (both "worse" are the pre-existing land-sig churn) |
| treasure_hunt | −0.0130 | **−0.0195** (all 4 seeds: −0.0070/−0.0045/−0.0025/−0.0055) |
| Auras | −0.0020 | **−0.0085** (all 4 seeds: −0.0040/−0.0025/−0.0005/−0.0015) |

Every affected `d0` case improved and none regressed, in one direction on four disjoint seeds — well
clear of single-seed noise. The searched depths (d3/d5) are avg-identical: the search overrides the
greedy ordering and only consults it as a tiebreak. This is the expected `d0`-dominant signature of a
greedy-policy change.

**Reach:** only 2 of 8 suite decks move, and the drivers are the *conditional* lands — Frostboil
Snarl (hand-dependent) in treasure_hunt, Razorverge Thicket (board-dependent) in Auras.
Anti-Lifegain's **five** shock lands do *not* move it: at 20 life in a goldfish they always enter
untapped, so static and dynamic agree. That is latent value — shock lands start to matter the moment
life is pressured, i.e. in real 1v1 play.

Not clairvoyance-dependent: "does this land enter tapped right now" is deterministic from the current
board, hand and life, so the edge is robust to not knowing the future (cf. the Invigorate
lethal-closer precedent in the heuristic skill, adopted for the same reason).

### The explicit "fastland first" rule — measured separately

The user's rule as its own lever (`MTG_LAND_CLOSING_WINDOW=1`): among lands that *currently* enter
untapped, drop one whose untapped window is **closing** (a fastland) ahead of an unconditionally
untapped land — same mana now, strictly better options later. Implemented as a pre-pass in both
sites, firing only while the window is still open.

On the **train** set it changes 5 Auras cases' play but nets **exactly the same** avg as the dynamic
fix alone. On **held-out** it does add a little, consistently:

| | net on top of the dynamic fix | cases better / worse |
|---|---|---|
| train (regression) | 0.0000 | 0 / 0 |
| held out (overnight) | **−0.0030** | 4 / 0 (all four Auras `d0` seeds: −0.0005/−0.0010/−0.0010/−0.0005) |

Sign-stable (4/4, no regression) but **small in magnitude** — per-case −0.0005..−0.0010, near the
noise floor, and today it can only ever fire on one card in one deck (Razorverge Thicket in Auras).
Compare the skill's Attempt-4 precedent (`HasIdleManaCreature`: safe, zero regressions, sub-noise →
*discarded*). The counter-arguments for keeping it: it is ~10 lines, it encodes a stated human-play
principle, and it generalises free to any future fastland deck.

**Scope boundary worth keeping:** the rule is deliberately fastland-only. A fastland's untapped-entry
condition is **monotonically closing** (your land count only grows), so "use it before you lose it"
is sound. A reveal land's condition (`etb_untap_reveal_subtypes`) depends on hand contents, which can
open back up as you draw — not a closing window, so it must not be swept into the same rule.

## The colour idea this came from: measured dead

Capping each land's produced colours at the colours the deck can actually **demand** (the
"don't make a bucket for white in Hinata" reduction) was measured across all 17 decks in `decks/`,
using the *corrected* signature. It collapses **nothing on any of the 8 suite decks** (one hit on
non-suite `Creature Giving.cod`: Overgrown Tomb + Stomping Ground → one shockland in a G/W deck).
For two lands to collapse under capping they must be identical in every other respect and differ
*only* in unusable colours; real decklists' lands differ in ETB or drawback too. Do not re-attempt
without a decklist that actually has that shape — and note it would need the completed signature
first, or it silently collapses Forbidden Orchard into Reflecting Pool.

## Reproducing the audit

The audit is a decklist × `cards.json` scan, no build required: group each deck's lands by the
signature fields, then flag any group whose members differ in a param the signature omits. Re-run it
whenever a land param is added to `cards.json` — a new param defaults to *invisible* to the dedupe,
which is exactly how these three collisions arose.

## ADOPTED (2026-07-30, user-approved)

Shipped **default-on**: the dynamic tapped-ness fix and the fastland-first rule.
Shipped **opt-in, default-off**: the completed land signature (`MTG_LAND_SIG_COMPLETE=1`).

The user's reasoning for leaving the signature out: *"pretty much all lands with different names are
mechanically different. Trying to group them together is risky."* Note this argues against the
signature dedupe **in general**, not against the fix — the fix makes the key discriminate *more*. What
it declines is the one part that still groups distinct cards: the dominance-promotion that treats
Sliver Hive and Cavern of Souls as one land. The logical end of that principle is deduping by land
**name** (no grouping at all — what human-play mode already does via `s_human_play_lands`); its cost is
unmeasured and would multiply the land branch by every distinct name in hand. Recorded as the open
option if the grouping is ever revisited.

Dropping the signature fix also **improved** the result: it was the only source of churn, so the
adopted pair is 8 better / **0 worse** on held-out where the three-way combination had 2 churn cases.

Two related items confirmed as already-correct, no work needed:
- **Same-named copies**: [TurnSolver.cpp:5294](../../src/ai/TurnSolver.cpp#L5294) already prefers the
  EARLIEST-EXPIRING copy (staged before non-staged, lower `m_staged_expiry` first), kept in lockstep
  with `TryPlaySpecificLand` and the executor's `cast_by_name`. It exists because of a real Dragonstorm
  bug (s26 Scourge, 1 hand + 2 staged: casting hand+staged stranded a copy that expired and cost the T5
  follow-up).
- The greedy's notion of "otherwise equal" is coarse — tapped-ness and multi-vs-mono only, no
  colour-need model — so within a pass the fastland rule can still prefer a G/W fastland when blue is
  what is needed. That gap predates the rule. At searched depths it does not apply: `greedy_land_name`
  is consulted only when the search is already indifferent on win-turn and first-turn value.

## Verdict — three separable changes, ranked by measured value

Held-out (overnight, 96 cases) net avg turn-to-win vs committed GT; negative = better:

| change | held-out net | recommendation |
|---|---|---|
| **Dynamic tapped-ness** in the land-priority passes | **−0.0280** (9 better / 0 real regressions) | **Adopt** — a bug fix, strictly better, sign-stable on 4 disjoint seeds |
| Completed land signature (dominance-aware) | +0.0010 (quality-neutral; both "worse" are churn recovering at 4×) | Adopt for correctness, not for score |
| "Fastland first" closing-window rule | −0.0030 on top of the fix (4 better / 0 worse) | Marginal — sign-stable but near noise, one card in one deck |

The three are independent and separately hatched (`MTG_LEGACY_STATIC_TAPPED`, `MTG_LEGACY_LAND_SIG`,
`MTG_LAND_CLOSING_WINDOW`), so any subset can ship.

**Note on narrowing the signature fix.** Two of its three collisions were judged low-value by the
user (2026-07-29) and the data agrees: the Dragonstorm storage split never fired in ~120,000 games
(clairvoyant search sequences the charge modes correctly anyway), and the slivers Sliver Hive
promotion only matters on a flooded late board. Dropping those two would cut the rebaseline from
21 configs to ~4 (Auras only) at the cost of leaving two known-latent gaps open. The Auras half
(Brushland vs Razorverge) is the one carrying measurable value.

## Follow-up

- **GT rebaseline required**, scope depending on which subset ships: dynamic-tapped moves TH + Auras;
  the signature moves Auras + slivers (+ latent Dragonstorm); closing-window moves Auras.
  Per-game audit first — never `--accept` on the aggregate.
- The two remaining static `enters_tapped` reads (`DecisionProviders.cpp` Hinata land rank,
  `TurnSolver.cpp` `land_good_early_tapped`) have the same latent fragility but are **not live**
  today — neither deck has a conditional land. Fix if either deck gains one.
- The same "incomplete dedupe key" question applies to any other signature-style dedupe in the
  enumerator; this audit only covered lands.
