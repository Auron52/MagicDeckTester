# Viewer feedback batch, 2026-08-25 — what shipped and what is still OPEN

A play-tester reported seven issues from a `tools/play` session, with no saved history and only seed
hints. All seven were root-caused and fixed, and the batch is **ADOPTED AND PUSHED** as
`90a537bd..c6743509` (three commits; CI green on Linux, Windows and determinism parity).
This doc is the standing record of the parts that are **not** finished, because they need the user or
a decision that was deliberately deferred.

The fixes themselves are described in the code they live in; the one-line index is at the bottom.

---

## CLOSED 1 — the drifting reference (fixed WITHOUT re-playing it)

`references/Dragonstorm/claude_s26_gi25.json` replayed **T5 → T6**, the single `play-drift` in the
224-reference sweep. It is now `repaired / win_turn 5`, and **the file was never touched**.

This was written up as USER-ONLY ("only the user can re-save a reference"). That was wrong, and the
user said so: *"one of the main goals of the design is not to require users to redo work they have
done."* The reference was fine; the CHECKER lost the thread.

**What actually happened.** At turn 4 that game casts Apex of Power, which impulse-exiles a Lotus
Bloom. The pre-fix engine then offered the illegal `{0}` suspend of that exiled card (suspend is a
from-hand-only alternative to casting, CR 702.61a; Apex grants only "you may CAST spells from among
them") — 24 of the turn's 35 plans, **plus two whole extra frames** whose entire plan list was that
one illegal plan, which the human declined (`-1`, `-1`). Fixing the bug removed those two frames.

`check_reference` aligned recorded frames to emitted ones strictly left-to-right and handled only
INSERTIONS. With two recorded frames gone it held `ri` on the first of them, mis-read the next real
frame (T5 pre_main) as an insertion and passed it — discarding the human's recorded T5 `cast:
Scourge of Valkas`, which is the game. Every later pick was stranded: *"3 decisions answered by
engine default; terminated with 3/12 recorded decisions unused"*.

**Fix:** deletion tolerance in `test/viewer_protocol_check.py`, the mirror of the insertion case —
skip forward to the next recorded frame with the emitted frame's identity, **but only over frames the
human DECLINED**. A pass carries no intent, so dropping it is lossless; a recorded frame with a real
pick is user work and stays loud (falls through, and the stranded picks surface as drift). Insertion
and deletion are the same problem from opposite ends.

**Control that proves it was never an engine regression:** the fixed checker against the *baseline*
binary (`90a537bd`, before this batch) also reports 0 play-drift.

## The per-game audit behind OPEN 2 (every game that got worse)

USER, before rebaselining: *"we literally should look at every game and ensure that the original
line is unplayable. There may also be some budget churn, which we can identify as always."* And, on
the first draft of this audit: *"I think I want to follow up on this diverging draws stuff that
agents keep falling back on. There seems to be no reason for that to happen? ... literally the only
reason it would happen is if your play changes. It should not ever stop you from finding a line with
unlimited budget and sufficient depth."*

**Both corrections are right, and they replace the first draft's reasoning.**

**"The draws diverged" is not a cause.** The library is a fixed permutation of the seed. A different
card can only be drawn after a different PLAY: a cantrip that did or did not resolve, a scry that
reordered the library, a turn that did or did not happen. Draw divergence is a RESTATEMENT of "the
engine chose differently at or before turn T", and reporting it as if it were exogenous variance
(which `explain_game.py` also does -- "variance, not a like-for-like quality change") hides the only
question worth asking. It plays no part in the account below.

**The decisive test is search depth.** A line removed because it was ILLEGAL is gone at any budget;
a line the search merely failed to find comes back when given more. So score every moved game with
the fixes OFF and ON, at rising depth/budget, and ask whether the two arms still differ
(`test/gt_line_playable.py --escalate-ab`; ladder d3/200ms, d5/500ms, d5/5000ms, d6/20000ms -- up to
2000x the case budget).

### Result

**76 cell-entries got worse and 33 got better, out of 49,000 games** across smoke + regression. Net,
weighted by games: **+0.0010 turns/game** (4.9127 → 4.9136). The "+0.118 turns" figure quoted
elsewhere is the SUM of per-cell deltas, which counts a 100-game d5 cell the same as a 1000-game d0
one -- see [[cell-sum-metric-misweights-small-cells]]. Weighted, the batch is flat.

| escalation A/B verdict | cell-entries |
|---|---|
| **the two arms CONVERGE** once the search is deep enough | **60** |
| still worse at every rung, to d6/20000ms | 16 |

So **four fifths of the ground-truth movement is a search-depth artifact, not a quality loss**: the
line is still there and the engine still finds it, just not within the tier's budget. Most of it is
the d0 tier, which is greedy by definition -- 53 of the 55 d0 entries converge at the very first
searched rung.

The 16 that never converge are **10 distinct games** (several appear in a d0/d3/d5 tier each):

| deck | distinct games | cause |
|---|---|---|
| hinata | 6 | ritual + prepay colour fixes (#6/#7) |
| dragonstorm | 1 | ritual colours (#6) |
| auras | 3 | aura fetch order (#5) |

**The 7 mana-fix games are the ones where the old line really was unplayable.** hinata
regression d3 s2002 gi93, at d5/5000ms, fixes-off vs fixed:

```
T2 off: Preordain; Preordain; Preordain; Reflecting Pool; Ponder; Gamble
T2 on : Preordain; Gamble
T4 off: Reality Spasm; Soulfire Eruption; Reality Spasm; Crackle with Power   <- wins T4
T4 on : Preordain
```
Six spells on turn two off two lands, a Sol Ring and an Ornithopter. Three Preordains plus Ponder is
four `{U}` pips from three blue-capable sources; the fourth came from **Sol Ring's `{C}{C}` sitting in
the `wild` bucket** -- the exact rules violation report #7 names. The T4 kill was funded by mana that
does not exist. No amount of search recovers it, and none should.

**The 3 Auras games are the price of the rule you asked for**, not a defect: fetch order is a policy,
so search cannot undo it (auras regression d3 gi306: the fixes-off arm takes Hyena Umbra on T4 and
Daybreak Coronet for the T5 win; the fixed arm sequences Umbra → Gladecover Scout → Kor Spiritdancer
and lands T6). You anticipated this: *"This might not provide that much benefit in terms of win-turn,
but is a good idea in our heuristic."*

### Which fix moved each game

Every fix has an off-switch, so the current binary can be run as the pre-fix engine and bisected one
switch at a time (`--attribute`). **All 76 reproduce their recorded score with the fixes off**, so
nothing outside this batch is involved:

| fix | cell-entries |
|---|---|
| prepay true colours (#7) | 40 |
| ritual float colours — Irencrag / Reality Spasm (#6) | 24 |
| both together | 2 |
| aura fetch order (#5) | 9 |
| staged suspend (#4) | 1 |

### Two methodology traps this audit hit

**1. A pre-fix BUILD is not a control here.** `src/cards/data/cards.json` is read at RUNTIME, and one
of the fixes IS card data (Irencrag Feat's `ritual_float_color`), so an old binary invoked from the
current tree loads the NEW card file and reproduces neither engine -- it scored Dragonstorm gi171 at
T7 where the ground truth it supposedly produced says T5. Use the off-switches: one binary, one card
file, one variable.

**2. Forcing a whole game accumulates drift.** Replaying a recorded line turn by turn through
`--validate-line` re-derives every sub-decision from defaults; a defaulted scry puts a different card
on top and the engine then rightly says *"'Ponder' is not in hand"* -- which reads exactly like a
legality finding and is not one. That produced 4 false "illegal" verdicts before a fixes-off control
walk was added to discriminate them. The escalation A/B above needs no forcing at all, which is why
it is the primary evidence.

## ADOPTED AND PUSHED — `90a537bd..c6743509`

All three tiers rebaselined and pushed on 2026-08-25:

| commit | what |
|---|---|
| `945c39d4` | `fix(refs)` — the reference CHECKER repair (deletion tolerance + label-anchored targets) |
| `e5e2eb16` | `fix(viewer,mana,rules)` — the seven reports, the auditor gate, the two instruments |
| `c6743509` | `gt(all)` — three-tier rebaseline, every mover attributed |

**Verified with the FINAL binary:**
- smoke `25/17/0 new`, regression `38/32/0 new`, overnight `75/93/0 new`, scenarios `25/25`;
- `MTG_WILD_PIP_AUDIT` CLEAN on all 14 regression decks;
- reference sweep `130 ok / 94 repaired / 0 drift / 0 enum-gap / 0 contract-fail`, `--strict` exit 0;
- `viewer_validate_check.js` 912 accept / 0 regression;
- per-game audit: **zero unexplained movers** in either tier group; 60 of 76 (train) and 38 of 97
  (overnight) converge under escalation, the rest being the price of the legality fixes;
- `--accept` reported `gt_logs consistent: 42 / 70 / 168, STALE 0, missing 0`.

**CI green on both platforms** (run `32906212100`): windows-latest 5m29s, ubuntu-latest 2m26s, and
the **Linux/Windows determinism parity** job passed — which is the one that matters, since MSVC
cannot be checked from the container.

Still OPEN after this batch, unchanged: the three pre-existing audit hard misses (Goblins echo /
KittyEquipment bounce / burn sacrifice), the `protection_from_everything` classification, the literal
replicate plan dimension, and the Reality Spasm model. **New and open: the mirrorwing run-to-run
divergence below — cause unknown.**

## The OVERNIGHT tier — audited the same way, and one thing it caught

The train tiers (smoke + regression) were audited game-by-game first; the overnight tier is
held-out seeds, so it got the same two instruments rather than an argument by analogy.

**Whole-tier, games-weighted: +0.00143 turns/game over 197,600 games** (4.8959 -> 4.8973). Four decks
are byte-identical (burn, th, knights, antilife — 64,000 games), and Stompy, the deck report #7 was
filed against, has 3 cells change DIGEST with its average unmoved: there the prepay fix is purely
representational, and the cost lands only on decks that were actually laundering colour.

| deck | delta turns/game | deck | delta turns/game |
|---|---|---|---|
| hinata | +0.01278 | slivers | +0.00106 |
| mirrorwing | +0.00463 | creature_giving | +0.00029 |
| dragonstorm | +0.00393 | fivecolour / minotaur / goblins | <= +0.0001 |
| auras | +0.00162 | burn / th / knights / antilife | **0.00000** |

**Attribution, all 97 searched-slower games:** ritual-colours 44, prepay-colours 41, both 3,
aura-fetch-order 4, combination 1 — and 4 that came back *"(none — not moved by this batch)"*, which
is the interesting part (below). **Escalation A/B: 38/97 converge**, a much lower share than the train
tiers' 60/76 — as it should be, because 88 of the 97 are caused by the two LEGALITY fixes, and a line
that was illegal does not come back at d6/20000ms. That is the price of legality, not a quality loss.

### What the 4 unattributed games exposed: a lever with no off-switch

All four were Minotaur, and all read `now == fixes-off` — flipping every switch did not restore the
old score, so the bisect had nothing to blame. The cause is the bestow `#B0`/`#B1` plan-signature
split: it is this batch's ONLY change to the AUTONOMOUS search space (gated on the param, not on
`HumanPlayActive()`), and it shipped without a switch. Minotaur runs 4x Gnarled Scarhide and is the
only deck holding a card any un-switched change touches.

Explaining it by elimination was not enough, because the whole audit method here rests on off-switches
existing. So `MTG_LEGACY_BESTOW_SIG` was added and wired into `SWITCHES`. Verified inert when unset:
smoke and regression re-ran **byte-for-byte identical** to the pre-flag binary (same averages, same
digests, same 25/17 and 38/32 verdicts).

Re-attributed with the switch in place, **all four Minotaur games come back `bestow-signature`** — the
elimination argument, now measured. That leaves **zero unexplained movers** across the tier: 44
ritual-colours, 41 prepay-colours, 4 bestow-signature, 4 aura-fetch-order, 3 both-mana-fixes, and 1
genuine combination (hinata gi96, where no single switch restores the old score but all of them
together do).

### A deck-wide divergence in 1 of 3 full runs — caught only by re-running

Re-running the tier after that rebuild produced a result that differed from the first run on exactly
**mirrorwing's 12 cells and nothing else** — every cell, ~13% of games each, uniformly spread across
game indices, all in the same direction (faster). A third full run then agreed with the first on **all
168 cells**. So run 2 is an outlier, and `--accept` would have promoted it into ground truth silently:
nothing in the harness flags it, because a batch that finishes cleanly looks like a measurement.

Seven hypotheses were tested and refuted — the new flag (byte-identical), a different binary (all
three `mtg.run` copies md5-identical to `build/Release/mtg`), the keep model being absent, the
rollout bottomer (`MTG_BOTTOM_ROLLOUTS=0` is digest-identical, so mirrorwing's bottoming is entirely
table-driven), the 255 MB `.bincache` (the `.gz` path gives the same digest), the sibling
`v1-twinflame-anger` model (6.0700, not the observed value), a load-time race (the differing games
are uniform, not clustered early), and a concurrency race (48 concurrent copies of the same job: 48/48
identical digests). **The cause is not yet known.**

What IS established, and what makes the tier safe to rebaseline anyway:
- the fixes-OFF arm reproduces the committed ground truth **exactly, digest included**
  (`mirrorwing_overnight_d0_s4004 = 5.9320/3059b1c73e0f9104`), so GT is a clean measurement and this
  batch's off-switches are faithful;
- the fixes-ON value is 5.9405, reproduced 9 times (2 direct, single-job batch, 56-job d0 batch,
  mixed heavy batch, no-bincache copy, 48-way stress, and full runs 1 and 3);
- runs 1 and 3 agree on all 168 cells.

Do NOT accept a tier from a single run without re-running it. Two runs of the SAME binary is what
caught this; it is cheap next to a corrupted baseline.

## CLOSED 2 — ground truth IS rebaselined (this section records the pre-accept state)

Deliberately left to the user: GT is shared state and another agent rebaselined the overnight tier
the same day (`29ea1ffa`). (The reference sweep inside regression mode is green now — `VPC_SKIP=1`
is no longer needed for a clean run.)

| tier | result |
|---|---|
| smoke | 50 PASS / 17 FAIL |
| regression | 63 PASS / 32 FAIL |
| scenarios | 25 passed, 0 failed |
| per-game (regression) | searched: 20 slower / 7 faster / 310 play-changed; d0: 26 slower / 14 faster |

Net **+0.118 turns** across the 15 cells whose average moved, and **+0.100 of that is Hinata alone**.
Every mover is attributable to a deliberate correctness fix:

- **hinata +0.100** (14 of the 20 searched-slower games) — Irencrag Feat's seven mana are now RED and
  Reality Spasm's untap-refloat carries the untapped sources' true colours, so the deck can no longer
  pay `{U}`/`{W}` pips with red or with laundered colourless. This is the price of legality.
- **dragonstorm +0.018** (2 searched-slower, 5→7) — the illegal Lotus Bloom suspend is gone.
- **mirrorwing +0.013** (2 searched-slower) — the prepay pool keeps true colours, so over-produced
  colourless can no longer pay a coloured pip. Isolated: d0 s1001 x1000 reads 5.9530 with
  `MTG_PREPAY_TRUE_COLOURS=0` (== baseline) and 5.9590 with it on.
- **auras / creature_giving / minotaur** — neutral to slightly better.

To promote after inspecting: `bash test/regression.sh --smoke --accept` and
`bash test/regression.sh --accept`. Run the FULL suite for the accept, never `--deck=X` (a scoped run
commits other decks' stale `.wins`). The overnight tier will also need it.

## CLOSED 1b — two reachability bugs the repaired checker UNMASKED

Aligning the Dragonstorm reference correctly let the walk reach frames it had been skipping, and it
immediately found a second reference whose recorded line the engine could no longer produce. Both
bugs below reproduce on the **baseline** binary (`90a537bd`) — the checker fix did not cause them, it
revealed them. Both fixes are `HumanPlayActive()`-scoped, so autonomous ground truth is unmoved
(smoke fingerprint byte-identical before and after: `25 passed / 17 failed / 0 new`, makespan 73s).

**(a) A narrowing that deleted the CAST, not just the target.**
`MirrorwingProvider::TrickTargetCandidates` ranks trick targets by what the AI would gain, and when
nothing scores it returns the `{0}` "narrowing active, no candidates" sentinel — which removes the
spell from the plan list entirely. On `Mirrorwing_Dragon/claude_s3_gi2` T5 (no magnet on board or in
hand, pending attack short of the gap-closing bound) that meant: four own creatures, Twinflame in
hand, `{1}{R}` trivially affordable, and **zero Twinflame plans offered** — on the exact turn that
reference records casting it. Reported as `ENUM-GAP … nplans 31->3; hand IDENTICAL`.

Fix: under human play, keep ONE representative battlefield target whenever the narrowing would
otherwise empty the set. One option-group entry, not the per-target fan-out that opening
`UnprunedGate::TrickTarget` would cost (measured 30ms → 2.1s → 21s → 31s per segment, past the
viewer's 120s step timeout — which is why `UnpruneHumanExempt` keeps that gate shut in human play).
The target itself is re-asked at RESOLUTION off the board and that chooser ignores the narrowing, so
the human still picks freely. The doctrine that Twinflame waits for a magnet is untouched autonomously.

**(b) Own TOKENS were dropped from every human target list.**
`CollectOwnCreatureTargets` (main.cpp) filtered on `CardDatabase::LookupCached(p.card)`, and tokens
have no `cards.json` entry — so every token was silently dropped. The two sibling collectors
(`CollectOpponentCreatureTargets`, `CollectCreatureTargets`) already carry exactly this fix, with a
comment spelling out the lesson; this one was missed. The search enumerates tokens as trick targets
deliberately ("tokens carry unique ids now, so they ride the target axis too"), so the human was
denied a target the AI can pick and the rules allow — on Mirrorwing, whose entire plan is token
copies. Now `1/1 Goblin Token (yours)` appears alongside the real creatures.

**Knock-on: target options are now matched by LABEL, not by index.** Fixing (b) adds one option to
Mirrorwing boards, and `find_option` replayed `target` picks by verbatim index — which drifted 8
references to a target the human never chose. `option_key` now keys a target option on its
normalised label (`"Goblin Guide (2/2, yours)"` ≡ `"Goblin Guide (yours)"`; `(face)`/`(self)` are
kept, since those name real targets). Positions were never the recorded intent; the label is.

## OPEN 3 — four PRE-EXISTING audit failures (not caused by this batch)

All four reproduce with the baseline binary too. They were surfaced by running
`scripts/audit_viewer_decisions.py` over every deck for the first time, and are out of scope here:

| deck | failure |
|---|---|
| Goblins | `Stingscourger: expected 'echo' -> NOT surfaced` (HARD MISS — the card WAS cast) |
| KittyEquipment | `Boros Garrison: expected 'bounce' -> NOT surfaced` (HARD MISS) |
| burn | `Shard Volley: expected 'sacrifice' -> NOT surfaced` (HARD MISS) |
| FiveColour | SELF-GUARD: `protection_from_everything` on Progenitus is in neither MANIFEST nor INERT_PARAMS |

The Progenitus one needs a *user decision* by the script's own rule ("add it to INERT_PARAMS with a
reason — which needs the user's OK"). The three hard misses are decision-TYPE wiring (sites 1-4), not
board activations; the new board-activation gate is **green on all 20 decks**.

Re-run: `python3 scripts/audit_viewer_decisions.py <deck> <profile> 1 6 8`.

## OPEN 4 — replicate is NOT a plan dimension (deliberate deviation from the ask)

The user asked to "make it a plan dimension for the viewer". What shipped instead is a **line-scoped
budget hold** (`g_line_unpaid_cost`, `SinkCostWithLineHold`) plus the existing `replicate` dialog.

**Why the deviation:** enumerating `replicate_count` as plan variants shifts every plan INDEX, and a
reference records the index. `test/viewer_protocol_check.py` re-anchors by plan CONTENT, but a recorded
`cast: Hatchery Sliver` would then match k=0/1/2 variants and pick one arbitrarily — drifting the 10
slivers references, which only the user can re-record. The budget hold delivers the same outcome (0
declared casts dropped, down from 8-9 per 40 human-play games) with zero index churn.

**Still open if the user wants the literal thing:** add `Action::replicate_count` with the cost
pre-scaled `(k+1) x` printed — the exact shape of Call of the Wild's `chosen_x` and Desperate Ritual's
`splice_count` — enumerated **only under `HumanPlayActive()`** (the `jitte_modes_open` precedent), and
accept the slivers reference re-record. That would also let the whole-turn solve price cast+replicate
jointly, which is a cleaner fix for OPEN 5 than the scoped reserve release.

## CLOSED 5 — `MTG_MANLAND_RANK` scaffolding removed

The selector existed only so the Mutavault reserve trade could be swept. Its result is recorded here
(slivers, 1800 games, seeds 2002/3003/4004: rank 5 and rank 30 are each **+0.05 turns worse** than the
reserve at 60, and identical to each other, so the reserve is the whole effect and the rung it would
move to is irrelevant), so per the convention that a sweep scaffold goes once its outcome is written
down, `ManaSourceRankBase` no longer carries it. The shipped behaviour is the human-play-only
`ManlandReserveReleaseScope`.

## OPEN 6 — Reality Spasm is still the wrong MODEL

Its refloat now carries the untapped sources' true colours, which fixes the illegal-mana half. The
underlying model is unchanged: "untap X target permanents" is still simulated as *floating* X mana
rather than actually untapping them, so the choice of WHICH permanents to untap does not exist. That
is the long-standing phase-2 deferral (an untap is only equivalent to mana against a passive
opponent); the colour fix does not close it.

## OPEN 7 — small unverified tails

- **Lotus Bloom's own suspend has no verb.** It rides `cast=Lotus Bloom` in the casts multiset. That is
  unambiguous *today* only because the card has no mana cost and can never be hard-cast — a second
  suspend card with a real cost would collide. Not round-tripped through `CheckLine`.
- **Deathrite's lifegain mode (`gy_exile_creature_lifegain`, mode 2)** is never enumerated:
  `MTG_SKIP_INERT_LIFEGAIN` defaults ON (a measured goldfish cut). It is deliberately excluded from the
  board-activation manifest for that reason, and must be re-added when phase 2 gives the opponent a
  clock.
- **Sliver Hive's `taptoken` reports UNVERIFIED in short sweeps** — `{5}` is rarely affordable in 6
  games x 8 turns. Verified instead by a targeted probe (slivers seed 11 / gi 10, turn 5:
  `taptoken=Sliver Hive` → `accept`).

---

## Index of what DID ship

| # | report | fix |
|---|---|---|
| 1 | abilities can't be used | `activate`+verbs for Deathrite (`gyexile=`), Mutavault (`animate=`), Sliver Hive (`taptoken=`), Twinshot (`channel=`); bestow mode sub + signature key; `BOARD_ACTIVATIONS` gate in the auditor |
| 2 | Mutavault deprioritised | `ManlandReserveReleaseScope` — reserve released for one human-play payment that has a replicate to follow |
| 3 | over-replication | `g_line_unpaid_cost` budget hold, human-play only |
| 4 | Lotus Bloom suspend off Apex | `m_is_staged` guard on Suspend (+ Channel, + Land's Edge pitch count) |
| 5 | aura fetch order | `MTG_AURA_RANK_MODE=4`, now the default — see the BEHAVIOUR census below |
| 6 | Hinata off-colour | Irencrag `ritual_float_color: "R"`; `RitualRefloatPool` colours Reality Spasm's refloat |
| 7 | leftover mana colourless | `BatchPrepayMainCasts` keeps true colours; `wild` is now exactly the batch's generic requirement |
| — | (reference repair) | `check_reference` deletion tolerance; `option_key` label-matching for target options; `TrickTargetCandidates` keeps one target in human play; `CollectOwnCreatureTargets` stops dropping tokens |

Reference sweep, final: **130 ok / 94 repaired / 0 play-drift / 0 shuffle-dead / 0 enum-gap /
0 mull-drift / 0 contract-fail** (224 refs). No reference file was modified.

### #5 is the one item judged on BEHAVIOUR, not on the win turn

The user asked for a *rule*, and predicted it would not pay: "this might not provide that much benefit
in terms of win-turn, but is a good idea in our heuristic". They were right — mode 4 vs mode 1 over
2700 Auras games (train 2002/3003/4004 at 400/seed, held-out 10010/11011/12012 at 500/seed) is a
**wash**, net -0.0030 turns summed, with three cells at exactly 0.0000.

A win-turn average cannot tell you whether the requested rule actually happened
([[measure-the-behaviour-not-just-the-outcome]]), so the acceptance test is the realised fetch census
(200 games, `MTG_TRACE=aura`), by card name:

| | mode 1 (old default) | mode 4 (new default) |
|---|---|---|
| Ethereal Armor | 102 (most-fetched) | **97 (still most-fetched)** |
| Rancor | 55 (4th) | **63 (2nd)** |

Both halves of the ask hold simultaneously: the flat Aura is now reached for when it is the last
fetch, *and* "we still want to get Ethereal Armor most of the time" is still true. `MTG_AURA_RANK_MODE=1`
restores the old unconditional scale-first for the A/B.

**Bracket note, as promised in the plan:** Rancor is modelled as `aura_power_bonus: 2` only — no
trample and no return-to-hand on death. The engine therefore *understates* Rancor on both arms, so
this census is a lower bound on how often the flat Aura is correct.

Off-switches (also the A/B controls the audit above is built on): `MTG_PREPAY_TRUE_COLOURS=0`,
`MTG_LEGACY_RITUAL_WILD=1`, `MTG_LEGACY_STAGED_SUSPEND=1`, `MTG_AURA_RANK_MODE=1`,
`MTG_NO_LINE_HOLD=1` / `MTG_LINE_HOLD=1`. The two `MTG_LEGACY_*` switches re-enable RULES
VIOLATIONS and exist only so a moved game can be attributed; never run a measurement on them. They
follow the existing `MTG_LEGACY_SHROUD` precedent.

## The standing instrument: `MTG_WILD_PIP_AUDIT`

`ManaPool::wild` means "one tap of a source that makes more than one colour", so spending it on a
coloured pip is legitimate — that is what a dual land looks like. Reports #6 and #7 were never about
that step; they were about **producers putting inflexible mana INTO `wild`**. Counting wild→colour
payments at the consumer is therefore useless as an assertion (~1M per 200 healthy Hinata games); the
meter lives at the two producers, and both must read ZERO:

| deck | fixes OFF | fixes ON |
|---|---|---|
| Hinata2 | ritual floats **9,408,348**, prepay excess **515,564** | 0 / 0 |
| Dragonstorm | ritual floats **3,571,619**, prepay excess **31** | 0 / 0 |
| Mirrorwing | ritual floats 0, prepay excess **109,525** | 0 / 0 |

(200 games each.) The split matches the per-deck attribution exactly — Hinata has both mechanisms,
Dragonstorm only the ritual one, Mirrorwing only the prepay one — and **all 14 regression decks read
CLEAN** with the fixes on. That is a suite-wide proof the hole is shut, rather than an argument from
a ground-truth diff. Purely additive: game logic and every digest are byte-identical with it on.
