# The three clairvoyant reference shortfalls — root causes (2026-07-28)

Of 140 saved references, the shipped clairvoyant search finishes **later than the human** on three.
The audit that found them is [reference-shortfall-audit-2026-07-28.md](reference-shortfall-audit-2026-07-28.md).

They are **two different search failures**, and neither is a resource problem:

| deck | ref | human | search | class |
|------|-----|-------|--------|-------|
| TH | `claude_s2_gi1` | 4 | 6 | **A. The land drop is not a searched decision** — the post-dig breakpoint has no search node at all. 6 → **5** (Hook 22 + draw-safe prepay); the last turn needs [post-breakpoint-search.md](post-breakpoint-search.md) |
| Dragonstorm | `claude_s1_gi0` | 4 | 6 | **B. Model coverage** — the winning line is never generated as a candidate |
| Dragonstorm | `claude_s24_gi23` | 6 | 7 | **B.** same |

**TH `claude_s2_gi1` is NOT closed — engine 5, human 4.** A prototype reached turn 4, but it needed a
second static land-drop rule (Hook 23) that lost games no budget could recover; it was rejected. The
remaining turn needs the post-dig breakpoint to become a real search node:
see [post-breakpoint-search.md](post-breakpoint-search.md).

## Budget and depth are irrelevant — established, not assumed

All three are **completely flat** across budget 20 → 100 → 500 → 2000 → 8000 → 32000 virtual-ms
**and `--budget-ms 0` (unbounded)**; across depth 5, 6, 7, 8, **9** (verified via the `[play]` line,
so the depth really was applied); and with `MTG_VALUE_MODEL=0` / `MTG_ESC_BEAM=0`, separately and
together. Unbounded runs finish in **0.4–1.9 s** — the search exhausts its tree and still returns the
late line.

All three human lines also **execute correctly under shipped pruning** (`ref_line_replay.py` with
`MTG_CLAUDE_PLAY_SHIPPED_PRUNING=1`): every plan the human picked is enumerated and the executor
performs it. So nothing here is about what the engine can represent or run.

## Class A — TH `claude_s2_gi1`: the search proves a win it then fails to deliver

**ROOT-CAUSED AND FIXED (adopted, default ON): the draw-safe batch prepay, A3 below.** With it the
game realises the turn 5 the search proves (6 → 5) and the divergence disappears. The remaining gap
to the human's turn 4 is A4: the post-dig land drop is not a searched decision at all
(see [post-breakpoint-search.md](post-breakpoint-search.md)).

### A1. The committed line does not realise its predicted win

```
$ MTG_FD_ORACLE=1 MTG_FLAG_NONCONV=1 mtg treasure_hunt --seed 2 --game-index 1 \
      --force-mulligan "0:" --budget-ms 0 --threads 1
[fd-diverge] seed=2 realized_win=6 predicted_win=5 proven_at_turn=1
avg (turns) : 6.0000
```

At **turn 1** the search *proved* a turn-5 win, committed to that line, and the game then took **6**.
This is the engine's own fidelity oracle (`MTG_FD_ORACLE`) firing — a rollout-vs-execution
divergence. The same probe is silent on all three references the search matches, and silent on both
Dragonstorm shortfalls, so it is specific.

This is why unbounded budget cannot help: **the search already believes it has a turn-5 line.** More
search time is spent confirming an estimate that execution will not honour. Any target better than 5
is also invisible, because 5 is already the incumbent bound.

### A3. The draw-safe batch prepay (ADOPTED, default ON)

Hatch: `MTG_NO_BATCH_PAY_DRAWSAFE` restores the unconditional prepay. This was briefly dropped as
"superseded" on smoke evidence and then **restored** — the larger regression seeds showed it is
required; see "A3 is required" below.


`TurnSolver::BatchPrepayMainCasts` pays the turn's combined main-cast cost in ONE joint solve and
pre-loads `state.floating_mana`. It assumes `acts` **is** the turn's cast set. On a Treasure Hunt
turn that is false: the dig DRAWS the cards cast later that same turn at a post-draw breakpoint
(recorded in `Action::breakpoint_casts`). Prepaying only the *known* casts lets the joint solve
spend scarce coloured sources on generic pips the later cast needs, so the recorded breakpoint cast
is declared and then unpayable.

Here: turn 5 casts two Treasure Hunts, which draw Land's Edge; the prepay taps Thundering Falls +
Steam Vents (the only red-capable duals) for `{1}{U}` + `{1}{U}`, leaving the blue-only Saprazzan
Skerry and Cascade Bluffs up. Land's Edge `{1}{R}{R}` is then unpayable and is dropped:

```
shipped   T5: PLAY Cascade Bluffs; CAST Treasure Hunt {1}{U}; CAST Treasure Hunt {1}{U}
              board: Thundering Falls(T) Steam Vents(T) Saprazzan Skerry(u) Cascade Bluffs(u)  -> T6
drawsafe  T5: ... same two Treasure Hunts ...; CAST Land's Edge {1}{R}{R}                      -> T5
```

`MTG_NO_BATCH_PAY=1` and `MTG_RESERVE=1` also both fix it, confirming batch-pay as the cause.

The gate declines batch prepay when the plan contains a **flood engine** (`DrawUntilNonland` or
`DigDraw`) — the casts after which the turn's cast set is not yet known.

**Scoping matters.** A first version keyed on the whole `OrderingOpaque` predicate, which includes
`draw > 0` and so caught ordinary cantrips. That disabled batch-pay on most Hinata turns and was
**net worse** (smoke: hinata +0.019/+0.013/+0.027 at d0/d3/d5 vs th −0.013/−0.027 at d3/d5; net
≈ +0.021). Rejected. Narrowing to the flood-engine class only:

| measurement | result |
|---|---|
| target game TH `s2_gi1` | 6 → **5**, `[fd-diverge]` gone |
| references (n=140) | fd-divergences **2 → 1**; exactly one win-turn change, the target, 6→5 |
| smoke, all other decks | **byte-identical** (slivers, burn, knights, antilife, hinata, dragonstorm) |
| smoke TH d3 / d5 | 4.2333 → **4.2200**, 4.1600 → **4.1333** |
| smoke TH d0 | 5.4780 → 5.4800 (2 games slower: gi246 5→6, gi393 6→7) |
| per-game audit, searched depth | **slower=0, faster=2**, play-changed=10 |

Net smoke LP ≈ **−0.038 (better)**, with zero searched-depth regressions. Its only cost is two d0
games (`th_smoke_d0` gi246 5→6, gi393 6→7) where the greedy pilot can no longer fit one cast without
the whole-turn prepay; both recover to T4 at depth 9 with unbounded budget. Tracked in
[post-breakpoint-search.md](post-breakpoint-search.md).

### A4. Why the human's rule-compliant turn-4 line is invisible (MEASURED)

Using the new `MTG_FORCE_LAND` diagnostic to put the engine on the human's land sequence
(`MTG_FORCE_LAND="1:Saprazzan Skerry,2:Saprazzan Skerry"`, fd fix on, strict flood ON) the engine
still finishes T5. The divergence is turn 3, and it is not a search decision at all:

```
turn 3, engine, in order:            turn 3, human:
  CAST Treasure Hunt                   CAST Treasure Hunt
  dig 8 cards - Reliquary Tower ABSENT  CAST Treasure Hunt
  PLAY_LAND Cascade Bluffs   <-- drop   PLAY_LAND Reliquary Tower  <-- drop, after BOTH digs
  CAST Treasure Hunt                    -> 15-card flood KEPT (no max hand size)
  dig 15 cards - Reliquary Tower PRESENT (but the drop is gone; played T4 instead)
```

`play_drawn_flood_keep_land` (`TurnSolver.cpp` ~4036) runs after **every** dig. It asks
`PostDrawKeepLandName` for a flood-keep land; that returns "" here because the first dig did not
reveal a Reliquary Tower, so it falls through to `play_breakpoint_land` -> `SimulateLandPlay`, which
**eagerly develops the best normal land**. That spends the land drop -- the only way to play a
Reliquary Tower -- one dig too early. The second dig then reveals the Reliquary with no drop left,
so the 15-card flood is discarded at cleanup instead of being kept as Land's Edge ammo, and the kill
slips to T5.

**This is why the line is invisible to the search: the losing choice is made by a static heuristic
inside the executor, after the plan is committed.** "Hold the drop through the second dig" is never
enumerated as a plan, so no amount of depth or budget can reach it. The eager-develop rule is
justified in its own comment by gi=881 (an undeveloped drop meant a drawn land was discarded) --
correct for a turn with ONE dig, wrong for a turn with more digs to come.

Fixed in [A5](#a5-the-fix--the-post-dig-land-drop-hooks-13--23-prototype-default-off).

### A5. The post-dig land drop (Hook 22 ADOPTED; Hook 23 REJECTED)

A4 localised the defect to the *timing* of the deferred drop. Instrumenting the land choice with
`MTG_FORCE_LAND` showed it is really **two independent static heuristics**, each costing one turn:

| # | decision | who decides it today | defect |
|---|----------|----------------------|--------|
| 22 | **WHEN** to spend the deferred drop | `play_drawn_flood_keep_land` → `play_breakpoint_land` | develops eagerly after dig 1, so a Reliquary Tower revealed by dig 2 can never be played |
| 23 | **WHICH** land to spend it on | `SimulateLandPlay` | ranks by colour breadth only — takes a 1-mana dual over a land that taps for two |

Both are provider hooks, so every other deck is byte-identical by construction.

Both are **default ON** for Treasure Hunt; `MTG_NO_TH_HOLD_FOR_DIG` / `MTG_NO_TH_DROP_YIELD` restore
the old behaviour for A/Bs (presence-tested, so `=0` also disables — see the `MTG_MAGMA_FAITHFUL`
gotcha).

**Hook 22 `HoldDeferredDropForFurtherDig`.** Hold the drop when the hand is
already flooding, no no-max-hand-size land is in play, and another dig is payable *from mana available
without the held land* (so holding can never starve the dig it is waiting for). The same step runs
again after that dig, so a whiff still develops — one dig later. That preserves gi=881 (the case the
eager rule was written for) while making the human's "dig, dig, then Tower" reachable.

**Hook 23 `PostDrawDropLandName`.** Among lands in hand, take the highest
per-tap yield (`ManaProducedPerTap`); yield ≤ 1 returns "" and the generic ranker is unchanged. The
drop lands *after* this turn's mana is committed, so it is really about next turn: Saprazzan Skerry
`{U}{U}` beats Thundering Falls (1 mana, 2 colours). On s2 gi1 that is 4 mana on turn 3 instead of 3
— two Treasure Hunts instead of one.

Either hook alone reaches turn 5; **together they reach turn 4, playing the human's exact line**:

```
T1 Saprazzan Skerry | T2 Treasure Hunt, Saprazzan Skerry | T3 Treasure Hunt, Treasure Hunt,
Reliquary Tower (whole 15-card flood KEPT) | T4 Cascade Bluffs, Land's Edge -> lethal
```

#### Measurements

`ref_bench --deck all` (140 references): clairvoyant shortfalls **3 → 2**, TH `0/5`. The two
survivors are the Dragonstorm class-B pair. TH reference LP 4.800 → **4.400**.

**Every TH case in both suite modes improves; every non-TH case is byte-identical** (smoke 18 pass /
3 TH fail, regression 30 pass / 5 TH fail — the failures are the intended fingerprint moves, since
rebaselined):

| case | old GT | adopted | Δ |
|------|--------|---------|---|
| `th_smoke_d0_s1001` | 5.4780 | 5.4570 | −0.0210 |
| `th_smoke_d3_s1001` | 4.2333 | 4.2000 | −0.0333 |
| `th_smoke_d5_s1001` | 4.1600 | 4.0933 | −0.0667 |
| `th_regression_d0_s2002` | 5.4830 | 5.4370 | −0.0460 |
| `th_regression_d3_s2002` | 4.3040 | 4.2620 | −0.0420 |
| `th_regression_d3_s3003` | 4.1300 | 4.0640 | −0.0660 |
| `th_regression_d5_s2002` | 4.2567 | 4.2200 | −0.0367 |
| `th_regression_d5_s3003` | 4.1400 | 4.0767 | −0.0633 |

Across all eight TH cases (3825 games): **189 faster, 35 slower.**

Held-out (3000 games, seed 700001, disjoint from every suite seed), shipped policy:
**4.2433 → 4.1927 (−0.0506)**, fd-divergences 3 → 2.

Both suites were re-run and are byte-reproducible, and **all three hatches together restore the
pre-change ground truth exactly** (`MTG_NO_TH_HOLD_FOR_DIG=1 MTG_NO_TH_DROP_YIELD=1
MTG_NO_BATCH_PAY_DRAWSAFE=1` → smoke 21/21 ALL PASS, regression 35/35 ALL PASS against the *old* GT).

#### Per-game audit of every changed game (required before the GT rebaseline)

Isolated by ENV rather than by binary (`logs/th_hold/env_ab_explain.py`: one binary, hatches on vs
off), so each diff is exactly this change and nothing else.

**All 8 searched-depth slowdowns are self-consistent** — `MTG_FD_ORACLE` is silent on every one, so
the search delivered what it predicted. They are honest heuristic losses, not bugs. Four of the eight
(`d3_s2002 gi157/gi171`, `d5_s2002 gi157/gi158`) are not even like-for-like: they cast **Throes of
Chaos**, whose cascade randomises the bottom of the library, so a different number of digs yields a
physically different game. That leaves **4 true like-for-like one-turn losses against 189 faster**.

The faster games are all the intended mechanism:

```
gi14  T4->T3  T2 land Cascade Bluffs -> Sandstone Needle ({R}{R}) => T3 affords Treasure Hunt AND
                Land's Edge {1}{R}{R} in one turn
gi17  T6->T5  T3 Cascade Bluffs -> Saprazzan Skerry => T4 casts TWO Treasure Hunts + Reliquary Tower
gi59  T7->T5  same shape
gi61  T5->T4  T2 Steam Vents -> Sandstone Needle => Land's Edge lands a turn earlier
```

The same-score changes are the same substitution without a turn falling: a 1-mana land replaced by a
2-mana one, usually buying an extra Treasure Hunt a turn earlier, sometimes just a different (never
worse) route to the same turn. Several show Hook 22 holding the drop as Land's Edge ammo instead of
developing (`T4 old: land Temple of Epiphany; Treasure Hunt; Land's Edge; ATTACK [opp 0]` →
`T4 new: Treasure Hunt; Land's Edge; ATTACK [opp -2]`) — same turn, one more land of reach.

**17 of 19 audited smoke changes draw a byte-identical library**, so they are true like-for-like line
changes rather than shuffle luck; the two exceptions are Throes of Chaos games again, and both hold
their win turn.

Two defects were found *by* this audit and fixed before the rebaseline — the drawsafe regression and
the colour gate, both below. Auditing the changed games was not a formality: the first pass looked
clean on smoke and was wrong.

#### A3's drawsafe fix is REQUIRED alongside these hooks (not superseded)

On the smoke seeds alone, hold+yield looked like it replaced A3: it removed the same fd-divergence
and stacking drawsafe on top measured slightly worse. **The larger regression seeds refuted that.**
`th_regression_d3_s3003 gi123` went from a turn-5 win to an outright **LOSS**:

```
[fd] T3 LINE win=5 | <pass>{land=Sandstone Needle} | spells[TH,TH]{land=Island} | spells[TH,TH]{land=Fiery Islet}
[replay-bp] turn=5 recs=1: Land's Edge          <- the committed line records the Land's Edge cast
   ...the game never casts it...
[fd-diverge] realized_win=9 predicted_win=5 proven_at_turn=3
```

That is exactly A3's bug: the whole-turn prepay stranded the `{1}{R}{R}` of a cast the dig had drawn.
The hooks did not remove the bug — they route **more** turns into the double-dig shape that triggers
it. `MTG_NO_BATCH_PAY=1` fixes the game, confirming batch-pay as the cause; the narrowed drawsafe
decline is now **default ON** (`MTG_NO_BATCH_PAY_DRAWSAFE` restores the unconditional prepay).

Lesson worth keeping: the smoke seeds were too small to see this. Two of the three "adopt hold+yield,
drop drawsafe" data points were real; the conclusion drawn from them was wrong.

#### Colour gate on Hook 23 (found by auditing the slower games)

Pure yield is colour-blind, and this deck's payoff is coloured. `th_regression_d3_s2002 gi375` took a
second Saprazzan Skerry (`{U}{U}`) over a Fiery Islet and stranded Land's Edge `{1}{R}{R}`, losing a
turn. Hook 23 now applies **castability before quantity**: if a spell in hand needs a colour the
battlefield cannot produce, only lands supplying that colour are eligible (highest yield among them);
otherwise highest yield wins outright. That recovers gi375 and gi32 and leaves the target reference at
turn 4.

#### Honest limitation

This replaces two *bad* static heuristics with two *better* static heuristics. The post-dig land drop
is still decided by the executor after the plan is committed, not by the search — so a case where
"hold" and "highest yield" are both wrong is still invisible for exactly the reason A4 describes. The
principled fix is to make the breakpoint drop a searched branch; these hooks are the cheap 90%.

### A2. The root land choice is decided by a static tiebreak, not by the search

**SUPERSEDED — this was a SYMPTOM of A1, not an independent defect.** With the A3 fix applied,
`MTG_NO_DEVELOP_TIEBREAK=1` no longer changes this game at all (5 either way): once the strand is
gone, Thundering Falls reaches turn 5 like the other lands, so there is no tie for the tiebreak to
mis-resolve. The evidence below is kept because it is what localised A1, and because the underlying
observation — that equal-estimate root plans are settled by a static ordering the search never
revisits — remains true in general. It just did not cost this game a turn on its own.

The plan comparator in `EnumeratePlansWithLand` (`TurnSolver.cpp:7644`, `s_develop_tiebreak`) sorts
by `wins_this_turn`, then static `value`, then — on ties — **develop-first, then "good early tapped
land", then the greedy land**. The search then replaces its incumbent only on a *strictly* better
win turn, so whatever this ordering puts first survives every tie.

A tiebreak, by construction, can only change the outcome when the search was indifferent. It changes
this game by a full turn:

```
TH s2_gi1     shipped = 6      MTG_NO_DEVELOP_TIEBREAK=1 = 5
DS s1_gi0     shipped = 6      MTG_NO_DEVELOP_TIEBREAK=1 = 6   (unaffected)
DS s24_gi23   shipped = 7      MTG_NO_DEVELOP_TIEBREAK=1 = 7   (unaffected)
```

So on this game the turn-1 land — worth 1–2 turns — is chosen by a heuristic ordering, with the
search declining to distinguish the candidates. The legacy engine (`MTG_LEGACY_SEARCH=1`) has a
per-candidate trace that shows the tie directly; at every pass, five of six candidates score
identically:

```
[trace] T1 top-level sub_depth=2  candidates=6
  <pass>  val=0  win=5      <pass>  val=0  win=5      <pass>  val=0  win=5
  <pass>  val=0  win=5      <pass>  val=0  win=5      <pass>  val=0  win=9
  -> T1 COMMITTED sub_depth=2: <pass>  win=5
```

(That trace is the *legacy* path, which is worse still on this game — 9. On the shipped
commit-the-line path the `MTG_NO_DEVELOP_TIEBREAK` result above is the evidence, since a tiebreak
cannot fire unless the search tied.)

### What the search is failing to see

`EnumerateEarliestWins` (a per-candidate full search, no cross-candidate branch-and-bound) ranks the
turn-1 lands differently from the search's own pick. At turn 1, under the shipped gate, at depth
6/7/8 (stable across all three):

| T1 land | earliest reachable win |
|---|---|
| **Thundering Falls — the engine's pick** | **6** |
| Saprazzan Skerry (the human's pick) | 5 |
| Steam Vents / Cascade Bluffs / Forgotten Cave | 5 |
| (no land) | 7 |

The search takes the candidate this enumerator rates **worst**. Caveat discovered later: with the A3
fix applied the search *realises* turn 5 from Thundering Falls, while this table still reports 6 for
it — so `EnumerateEarliestWins` is a stronger estimate, **not ground truth**; it carries its own
rollout-fidelity gap. Treat its numbers as a second opinion, not an oracle. The distinguishing fact is mana
available *without spending the land drop*: Saprazzan Skerry taps for `{U}{U}` (2 mana from one
land), Thundering Falls for 1. Treasure Hunt costs `{1}{U}`. Under the flood-engine rule (see below)
that quantity decides whether Treasure Hunt is castable at all — and nothing in the search's
per-candidate estimate measures it.

### Interaction with the flood-engine gate (`THStrictFlood`)

`TreasureHuntProvider::ShouldCastDrawEngine` permits a flood-engine cast only if: (1) Land's Edge in
play, (2) Reliquary Tower in play, (3) **the land drop is still open**, or (4) untapped mana suffices
for engine + Land's Edge this turn. Rule 3 is checked *after* the candidate land has been played into
the trial state, so any "play a land AND cast Treasure Hunt" plan fails it. That is deliberate — the
design rule is "don't play a land before Treasure Hunt".

**The rule does not forbid the human's line.** The human casts every Treasure Hunt with the drop
open: T1 Skerry; T2 Skerry taps for 2 → cast Treasure Hunt (drop open), *then* play Skerry #2; T3 two
Skerries = 4 mana → two Treasure Hunts (drop open), *then* play Reliquary Tower; T4 Land's Edge for
the win. Fully compliant.

The engine never reaches a state where it can use the rule: T1 Thundering Falls (1 mana) → T2 has 1
drop-free mana, so Treasure Hunt is unaffordable deferred and the only affordable route (play a land
first) is refused by rule 3 → **no dig at all on T2**. Turning the clause off (`MTG_TH_STRICT_FLOOD=0`
→ turn 4) "fixes" the game by letting the engine misuse the drop, not by fixing the sequencing.

Across the TH reference set the clause is otherwise neutral (only this game moves; LP 4.800 → 4.400).
Not a recommendation to flip it: it was adopted on *non-clairvoyant* evidence (d0 greedy −0.123, NC
search −0.034) that this measurement does not revisit. Worth noting, though, that its adoption
comment claims the clairvoyant regressions were "FAKE known-draw speed … 0 real regressions" — the
play GUI runs with no `--reveal`, so this turn-4 line was found *without* clairvoyance.

## Class B — Dragonstorm: the winning line is never generated

Neither Dragonstorm game trips the fidelity oracle: **the search predicts what it delivers.** It is
internally consistent. The human's line is simply not in its candidate space.

`claude_s1_gi0` — the human wins T4 with a three-segment turn, each segment possible only because the
previous one resolved:

| seg | cast |
|---|---|
| 1 | land Unclaimed Territory → Rite of Flame, Seething Song, **Apex of Power** (float R) |
| 2 | from Apex's exiled seven: Ruby Medallion, Rite of Flame, **Apex of Power** #2 |
| 3 | from Apex #2's exiled seven: Rite of Flame, Seething Song, **Dragonstorm** → 5 dragons, lethal |

Apex of Power (verified in `src/cards/data/cards.json`) exiles the top seven and lets you cast them
until end of turn. Segments 2 and 3 therefore draw on a card pool that does not exist when segment 1
is enumerated. A plan is a cast list built from the currently-castable set, so this chain is never
constructed as a candidate — no depth or budget can search what is never generated.

The exhaustive enumerator agrees, from the opening:

```
turn 1 candidates:  Sandstone Needle -> 6    Unclaimed Territory -> 6
                    Mountain -> 6            (no land) -> 8
```

Every turn-1 candidate reports 6; the human achieved 4. At turn 3 it rates the human's actual play
(`land=Mountain, casts=[]`) at **7**, worse than the Scourge-of-Valkas line at 6 — so the search
picks correctly *per its model*.

`claude_s24_gi23` is the same class: its human turn-5 is a six-spell chain (Ruby Medallion, Pyretic
Ritual ×2, Seething Song, Desperate Ritual, Apex of Power) and the enumerator offers a **single**
candidate at that decision.

The engine *has* a staged-exile re-solve breakpoint (`TurnSolver.cpp` ~3673, ~4878) and the executor
uses it correctly — which is why the human's line replays perfectly. The search does not expand
through it. **The engine can execute the chain but cannot find it.**

## The remaining fd-divergence — Dragonstorm `claude_s29_gi28` (predicted 4, realised 7)

A census of all 140 references (`MTG_FD_ORACLE`) finds exactly **two** divergences. A3 fixes one;
this is the other, and it is three times larger:

```
[fd-diverge] seed=29 realized_win=7 predicted_win=4 proven_at_turn=1
```

At turn 1 the search commits a turn-4 win via
`Pyretic Ritual, Ruby Medallion, Desperate Ritual, Desperate Ritual, Irencrag Feat, Dragonstorm`.
What the executor actually does on turn 4 is **cast Ruby Medallion and nothing else**. It then
re-commits a go-off every turn (T5 predicts 5, T6 predicts 6, T7 predicts 7) and only lands the real
kill on turn 7:

```
T4 oppLife=20: CAST Ruby Medallion [{2}]
T5 oppLife=20:
T6 oppLife=20:
T7 oppLife=-70: PLAY Mountain; Pyretic Ritual; Pyretic Ritual; Desperate Ritual; Desperate Ritual; Dragonstorm
```

This is the **already-root-caused, unfixed** bug in
[d0-mana-realization-strand.md](d0-mana-realization-strand.md): the flat-pool affordability check
accepts a ritual+payoff subset that cannot be paid in cast order, and specifically the
*same-turn Ruby Medallion discount* strand it names (`gi523`). Its proof-of-concept fix
(`MTG_REALIZE_CHECK`) over-rejected and made d0 worse; the accurate fix is still open. Note the
human also wins this game on turn 7, so it is **not** a reference shortfall — but it is a 3-turn
fidelity bug, i.e. a bigger self-consistency failure than the one fixed here.

## Instrumentation gaps found

* The shipped **commit-the-line path has no PER-CANDIDATE trace** — `FullSearchLine` contains zero
  `s_trace_solve` references, so "why did the search prefer plan X over plan Y?" is unanswerable on
  the path that ships (the legacy `SolveWithLookahead` has it). A per-COMMITTED-LINE trace does
  exist: `MTG_FD_TRACE` prints `[fd] T<n> LINE win=<w> | <phases>` plus `[fd-pred]` per-turn
  predicted state and `[replay-bp]` recorded breakpoint casts — that is what localised A1, paired
  with `MTG_FD_ORACLE`.
* `--trace` was reachable only via `--diag-depth`; now wired into the normal goldfish path
  (single-game, single-thread only, since `SetTraceSolve` is process-global).
* `PlanDesc` prints `<pass>` for every land-only plan, so even the legacy trace cannot tell you
  *which land* a candidate played.
* `MTG_CLAUDE_PLAY_SHIPPED_PRUNING=1` added (`src/main.cpp`, inert by default): claude-play otherwise
  force-sets `MTG_UNPRUNED`, so it could not answer any question about a prune gate.
* `main_phase` decisions carry no `ai_choice`, so there is no supported way to read the search's pick
  mid-game. (A handover probe built on this assumption was discarded as invalid.)

## Dead ends (measured, do not repeat)

* Budget, including unbounded — flat on all three.
* Depth 6/7/8/9 — flat on all three.
* All 15 `MTG_UNPRUNE` gates individually — only `drawengine` moves TH; nothing moves Dragonstorm.
* Global `MTG_UNPRUNED` — no help; makes Dragonstorm `s1_gi0` *worse* (8), stably at every budget.
* `MTG_VALUE_MODEL=0`, `MTG_ESC_BEAM=0`, `MTG_NO_GROUP_CAP=1`, `MTG_NO_MOVE_ORDER=1`,
  `MTG_COLOR_BLIND_TIEBREAK=1` — all flat on TH.
* `MTG_LEGACY_SEARCH=1` — much worse on TH (9).
