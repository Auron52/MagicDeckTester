# Payment-path re-check set — games to re-evaluate after the prepay work

**Status:** OPEN, and deliberately NOT being fixed here. The payment path is owned by another
agent with unpushed work; this document exists so their fix can be checked against a concrete,
pre-built set of games rather than a re-derivation.

**Owner of the defect:** `BatchPrepayMainCasts` / the prepaid-pool spend path in
`src/ai/TurnSolver.cpp`, gated by `MTG_PREPAY_TRUE_COLOURS` (default on).

**The list:** `test/prepay_recheck_cases.tsv` — 418 games, self-contained.
**The tool:** `test/prepay_recheck.py verify` — run it on your tree, read the summary.

---

## 1. What went wrong

The 2026-08-25 colour-honesty batch (`90a537bd..c6743509`) shipped three mana fixes:

| fix | switch | what it does |
|---|---|---|
| ritual float colours | `MTG_LEGACY_RITUAL_WILD=1` | Irencrag Feat's seven mana are RED, not wild; Reality Spasm's untap-refloat carries the untapped sources' true colours |
| staged suspend | `MTG_LEGACY_STAGED_SUSPEND=1` | a card impulse-exiled by Apex of Power can no longer be Suspended |
| **prepaid pool true colours** | `MTG_PREPAY_TRUE_COLOURS=0` | `BatchPrepayMainCasts` pre-loads the pool with the colours actually produced instead of dumping everything unpinned into `ManaPool::wild` |

The first two are unambiguous rules fixes: they delete lines that were never legal. The third is
**also a real soundness fix** — before it, over-produced colourless folded into `wild` could pay a
coloured pip, so a Sol Ring's `{C}{C}` could fund a `{U}`. Nothing below argues for reverting it.

But it changed the **payment path**, and it has a side effect that is not a rules matter: paying for
the *same* line, the engine now sometimes taps a mana creature that the pre-fix path spared. A tapped
creature cannot attack, so the turn deals less damage and the game runs long.

Ground truth was rebaselined **with that side effect baked in** (commit `c6743509`). That is the debt
this document tracks.

## 2. The evidence — `mirrorwing_overnight_d3_s5005` gi309 (4 → 5)

Both arms of the same binary were forced down the recorded line, one with
`MTG_PREPAY_TRUE_COLOURS=0` and one without. **Every decision matched** — same plan indices, same
targets, every action accepted by both engines:

```
T1  land=Game Trail (cast nothing)
T2  land=Mountain;    cast Elvish Mystic; cast Impolite Entrance
T3  land=Forest;      cast Zada, Hedron Grinder
T4  land=Game Trail;  cast Oracle's Restoration; cast Fortifying Draught; cast Gold Rush
```

At the **pre-combat T4 decision**, both arms at 26 life, opponent at 20, identical battlefield
(Elvish Mystic, Zada, 2 Treasure tokens, 4 lands):

| arm | Elvish Mystic | outcome |
|---|---|---|
| `MTG_PREPAY_TRUE_COLOURS=0` | **untapped** → attacks | lethal, wins T4 |
| default (fix on) | **tapped for mana** | only Zada swings for 14 → opponent at 6 → wins T6 |

The T4 payment is `{G}` + `{G}` + `{1}{G}`. Three green pips are available from two Game Trails
(`produces: ["R","G"]`) plus a Forest, with the Mountain covering the generic — so the dork never
needed to be tapped. It is not a legality question; it is a source-selection question.

Reproduce:

```
# the losing arm (current default)
build/Release/mtg "decks/Mirrorwing Dragon/Mirrorwing Dragon.cod" \
  --profile "decks/Mirrorwing Dragon/Mirrorwing Dragon.profile.json" \
  --games 1 --seed 5314 --game-index 309 --depth 3 --budget-ms 10 --ignore-play-profile
#   -> avg (turns) : 5.0000

MTG_PREPAY_TRUE_COLOURS=0 <same command>          # -> avg (turns) : 4.0000

# the forced-line, both-arms comparison that proves the line is identical:
python3 test/gt_line_playable.py overnight --verbose --jobs 1 mirrorwing_overnight_d3_s5005 309
```

**Ruled out.** `MTG_DORK_HOLD_PARTIAL=1` and `MTG_DORK_TAP_LAST=1` do not recover it; only
`MTG_PREPAY_TRUE_COLOURS=0` does. There is no decline and no drop — `MTG_PREPAY_PROBE` reports zero
`PP_WILD`/`PP_UNPAYABLE` (only "<2 casts") and `MTG_AFFORD_AUDIT=2` reports `real drops: 0` on both
arms. So the batch prepay *succeeds*; it just picks a tap set that spends a creature.

A likely place to look, unverified: the reserve ladder in `BatchPrepayMainCasts` holds creatures via
`rungs[r]`, but a held rung is only accepted when `produced.wild == 0`, and a dual land's tap is
exactly what puts mana in `wild`. On a manabase of R/G duals the hold rung may therefore be rejected
and the unreserved solve taken, which is free to tap the dork. **Treat that as a hypothesis, not a
finding** — it was not confirmed, and the observed symptom (identical line, different tap set) is the
part that is established.

## 3. Scope — what is and is not suspect

| deck family | attributed to | verdict |
|---|---|---|
| dragonstorm | ritual colours | **warranted.** T2 Karrthus `{4}{B}{R}{G}` off Unclaimed Territory + Mountain: two distinct off-red pips, one non-red source. The second came from Irencrag's wild. Hand-checkable from card data. |
| hinata | ritual colours (+ prepay) | **warranted**, but rests on attribution + the producer audit (9,408,348 uncoloured ritual floats with the fix off, 0 with it on) rather than a hand count — the Reality Spasm untap chain has to be traced to verify by eye. |
| mirrorwing | prepay colours | **NOT warranted** — the defect above. |
| auras / minotaur / others | aura-fetch-order, bestow-signature | out of scope for the payment path; those are policy/enumeration changes, not payment. |

**Not yet checked, and worth checking:** whether any *hinata* or *dragonstorm* mover also loses a
creature to the tap order. Their movement is correctly attributed to the ritual-colour fix, but
attribution names the switch, not the mechanism — a game can be attributed to one fix and still be
carrying this symptom. `classify` (below) labels these.

## 4. The case set

`test/prepay_recheck_cases.tsv` — every game whose outcome the batch made **worse**, versus
`90a537bd` (the commit before it). Columns:

```
key  gi  deck  profile  seed  game_index  depth  budget  pre_fix  post_fix  attribution  walk_class
```

`deck`/`profile`/`seed`/`game_index`/`depth`/`budget` are baked in, so the file is usable from a
checkout that does not share this repo's `gt_logs` or `explain_game.py` state. `pre_fix` is the win
turn before the batch, `post_fix` the rebaselined value now (`-1` = no win inside `max_turns`).

`walk_class` is the discriminating column:

- **EXECUTION-DIFFERS** — both arms were forced down the identical recorded line and still disagreed
  on the result. This cannot be a search, ranking or enumeration effect; it is the payment path.
  **These are the cases the prepay work should fix.**
- **CHOICE-ONLY** — the recorded line still executes correctly under the fix; only the plan the search
  *picked* changed. Fixing the payment path need not recover these, and they may be legitimate.
- **REFUSED** — the current engine refuses an action of the old line (none expected; would be a
  separate and more serious finding).

## 5. How to use it

On a tree with the payment-path work applied, after `./build.sh`:

```
python3 test/prepay_recheck.py verify --jobs 22
```

It runs every case autonomously and reports per game:

| class | meaning |
|---|---|
| `RECOVERED` | now at or better than `pre_fix` — the defect is gone for this game |
| `STILL-WORSE` | unchanged from `post_fix` |
| `MOVED` | a third value; inspect before concluding anything |

Control result, for comparison: on `c6743509` (the defective binary) every case reads
`STILL-WORSE` by construction, so any `RECOVERED` count above zero is signal.

`--filter mirrorwing` narrows it; `--bin PATH` points at a different build. To re-derive the set
itself (only needed if the baseline moves): `python3 test/prepay_recheck.py build --baseline <sha>`.
To recompute the `walk_class`/`attribution` columns: `classify --jobs N` (expensive — it forces both
arms down every prepay-attributed line).

## 6. What to do with the result

**The recovered games must be rebaselined back.** They are currently committed ground truth at their
*worse* values, so they will read as "improvements" on the next run and the harness will flag them:

1. re-run the FULL suite per tier — smoke, regression, overnight — never `--deck=X`
   (a scoped run promotes other decks' stale `.wins`);
2. run each tier **twice** and diff `test/results/<mode>.env` before accepting. This is not
   boilerplate: an overnight run during this batch disagreed with three other runs on exactly one
   deck's twelve cells, and `--accept` would have written that into ground truth silently. See
   `docs/design/viewer-feedback-2026-08-25.md`;
3. `bash test/regression.sh <mode> --accept` per tier;
4. push and watch CI — the Linux/Windows determinism-parity job is the one that matters.

## 7. Honest limits of this document

- The mechanism in §2 is *observed* (identical line, different tap set, creature lost as an
  attacker). The `produced.wild == 0` explanation is a hypothesis and was not confirmed.
- The 418 cases are games that got **worse**. 125 got better over the same batch; those are not
  tracked here and some may be luck of the same re-ranking.
- `pre_fix` is not automatically the "right" answer. For the ritual-colour decks the pre-fix value
  was produced by an *illegal* line, so a case reading `STILL-WORSE` there is correct behaviour, not
  a failure. Only `EXECUTION-DIFFERS` cases carry the presumption that the old value should return.
