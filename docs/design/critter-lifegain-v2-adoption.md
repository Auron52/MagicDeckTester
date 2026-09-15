# CritterLifegain v2 — the list, and the adoption pipeline

**Status: ADOPTED AND SHIPPED, 2026-09-15.** All five steps below are complete; the sections that
follow are kept as the record of what was planned and why. See **"Outcome"** at the foot of this file
for what each stage actually measured. `decks/CritterLifegain/CritterLifegain.cod` is now v2, with its
own play profile, value leaf and mulligan profile, and all three GT tiers re-accepted.

| step | commit | result |
|---|---|---|
| 1. install list, drop v1 sidecars | `07b41b0a` | 60 cards / 23 lands; coverage 15/15 full |
| 2. analyze → play profile | `07b41b0a` | `NO_COST_INTERACTIONS`, `DISCARD_INERT` |
| 3. value leaf | `438d791e` | −0.00162t at t −3.05, **0.31x cost** — clean win, adopted |
| 4. mulligan profile | `03cadcf2` | keep **−0.1967t**, bottoming **−0.0251t**, both 16/16 seeds |
| 5. regression GT | `4355394c` | **20/20 cases faster**, mean ≈ −0.37t |

**User decision, 2026-09-15:** *"let's save this list... The idea will be to make this the new list...
So a pipeline of Analyze -> value-leaf -> Mulligan profile"* and *"As usual we should keep the old
list around as v1, just like in Mirrorwing."*

---

## v1 is archived — `decks/CritterLifegain/v1-thune4-basilica/`

Same layout and same tracked set as `decks/Mirrorwing Dragon/v1-twinflame-anger/`: the `.cod`, the
play profile, the value leaf, and the two gzipped keep-table artifacts. (`gencache.json` is copied but
gitignored, as it is for Mirrorwing.) Named for what distinguishes it: 4 Archangel of Thune and the
Orzhov Basilica mana base.

```
decks/CritterLifegain/v1-thune4-basilica/
    CritterLifegain.cod
    CritterLifegain.profile.json
    CritterLifegain.value.json
    CritterLifegain.keepmodel.exhaustive.profile.json.gz
    CritterLifegain.keepmodel.exhaustive.raw.json.gz
```

---

## The v2 list — 60 cards, 23 lands

```
4 Soul Warden          4 Voice of the Blessed          2 Heliod, Sun-Crowned
4 Soul's Attendant     1 Daxos, Blessed by the Sun     3 Auriok Champion
4 Serra Ascendant      1 Ajani, Strength of the Pride  1 Ranger-Captain of Eos
4 Ajani's Pridemate    2 Archangel of Thune            3 Unexpectedly Absent
4 Ocelot Pride        19 Plains                        4 Remote Farm
```

### What changes from v1, and the evidence for each

| change | why |
|---|---|
| **+4 Ocelot Pride** | the new card; worth −0.17 beyond the value of the slots it takes (same-cut/different-fill control) |
| **−3 Orzhov Basilica, +4 Remote Farm, −1 Plains** | −0.090 for the Basilica swap; the 4th Farm is a wash vs a Plains (+0.0035 / +0.0016) |
| **Serra Ascendant 2 → 4** | the format finding: a 6/6 lifelink for `{W}` at 30 starting life. Its 2nd copy is worth +0.0118 at 20 life and **+0.1099** in 2HG, and the marginal is still nearly flat at four |
| **Archangel of Thune 4 → 2** | USER RULING. Two is an **access floor**, not a marginal: a singleton is seen by turn 8 in only 23.3% of games vs 41.5% for two, and you see *both* only 5.1% of the time. Costs 0.016t / 0.012t |
| **Ajani, Strength of the Pride 3 → 1** | its 2nd copy measures **worse than a Plains** in both formats (−0.0121 / −0.0086) |
| **Auriok Champion 4 → 3** | USER-PERMITTED; the 4th is worth less than a Plains. The **3rd is KEPT** deliberately — cutting it measures better but its protection is inert on all four DEBT axes here, so the sim flatters that cut |
| **Plains 20 → 19 (23 lands)** | the *price* of the 4th Serra, not a gain in itself: the 24th land swapped for a NEUTRAL filler measures +0.0051 (20 life, slightly bad) and −0.0012 (2HG, nil). The low curve makes the land cheap, which is what lets the mana base fund the Serra |
| **no Sol Ring** | USER DECISION 2026-09-15 (*"We'll skip the Sol Ring"*): break-even at best, clearly bad at two |

### Measured, held-out confirmed (80,000 games per format, vs the v1 list)

| | 20 life | 2HG |
|---|---:|---:|
| **v2** | **−0.3274** | **−0.4685** |

Full evidence and every caveat: `docs/design/critter-lifegain-format-breakdown.md` (3.5M games). The
running ledger is `docs/design/analysis-CritterLifegain.md`.

**One number to expect to MOVE at adoption.** Those deltas were measured on v1's apparatus via the
alias route — v1's keep table with Ocelot Pride and Remote Farm aliased into existing buckets, and
v1's value leaf. The pipeline below builds v2 its **own** artifacts, and the §6a control says v1's
24-land-fitted table was *understating* v2 by ~0.018t at 20 life. So v2's standalone number should
come out at least as good, but it is a **new measurement, not a reproduction** — do not treat a
difference as a regression.

### The `.cod` to install (step 1 below)

```xml
<?xml version="1.0" encoding="UTF-8"?>
<cockatrice_deck version="1">
    <deckname></deckname>
    <comments></comments>
    <zone name="main">
        <card number="4" name="Soul Warden"/>
        <card number="4" name="Soul's Attendant"/>
        <card number="4" name="Serra Ascendant"/>
        <card number="4" name="Ajani's Pridemate"/>
        <card number="4" name="Voice of the Blessed"/>
        <card number="4" name="Ocelot Pride"/>
        <card number="1" name="Daxos, Blessed by the Sun"/>
        <card number="19" name="Plains"/>
        <card number="4" name="Remote Farm"/>
        <card number="1" name="Ajani, Strength of the Pride"/>
        <card number="2" name="Archangel of Thune"/>
        <card number="2" name="Heliod, Sun-Crowned"/>
        <card number="3" name="Auriok Champion"/>
        <card number="1" name="Ranger-Captain of Eos"/>
        <card number="3" name="Unexpectedly Absent"/>
    </zone>
</cockatrice_deck>
```

---

## The pipeline

**Rule 0 for all of it: ONE frozen commit, strictly serial, alone on the box.** Every artifact here is
an engine-state fingerprint. Record `git rev-parse HEAD:src` before starting and do not let engine
work land underneath it; if it does, the freeze is broken and the stage restarts. Never two
generations at once, **not even a quick probe alongside** — a `recommend` probe run beside a value
leaf once produced a projection wrong on three axes at the same time.

### Step 1 — install the list, and DELETE the stale artifacts

```bash
# write the .cod above, then:
git rm decks/CritterLifegain/CritterLifegain.profile.json \
       decks/CritterLifegain/CritterLifegain.value.json \
       decks/CritterLifegain/CritterLifegain.keepmodel.exhaustive.profile.json.gz \
       decks/CritterLifegain/CritterLifegain.keepmodel.exhaustive.raw.json.gz
rm -f decks/CritterLifegain/CritterLifegain.keepmodel.* \
      decks/CritterLifegain/CritterLifegain.value.json \
      decks/CritterLifegain/CritterLifegain.profile.json      # incl. the untracked .bincache / uncompressed
```

**This deletion is load-bearing, not tidying.** The keep table and the value sidecar are
**presence-gated** — the file existing beside the deck *is* adoption. Leaving v1's artifacts next to a
v2 decklist makes every run of this deck play v2 with **v1's mulligan table and v1's value leaf**, and
nothing warns you. (Safe to delete: they are committed in `v1-thune4-basilica/` and in git history.)

### Step 2 — Analyze (produces the play profile)

```bash
python3 scripts/analyze_deck.py decks/CritterLifegain/CritterLifegain.cod --coverage-only
python3 scripts/analyze_deck.py decks/CritterLifegain/CritterLifegain.cod
```

Coverage should come back clean: **every card in v2 is already implemented and reviewed** — Ocelot
Pride was built and verified on 2026-09-15 (commit `7434bd2a`, CI green incl. determinism parity) and
Remote Farm was already in `cards.json` via BreachingDragonstorm. So this is the cheap stage; it
exists to write `CritterLifegain.profile.json`, which the next two stages both read.

### Step 3 — Value leaf (alone on the box)

```bash
bash scripts/valueleaf.sh run decks/CritterLifegain      # status: valueleaf.sh status decks/CritterLifegain
```

One command, all five phases pooled, resumable. **Do not hand-roll the phases and do not add knobs** —
hand-rolling is how the profile-less-measurement bug, the silent H-cell perf cliff and a run stuck at
3 of 24 cores all happened. It stages without adopting.

### Step 4 — Mulligan profile (alone on the box, AFTER step 3 has FINISHED)

```bash
bash scripts/mullgen.sh run decks/CritterLifegain
```

**The ordering is a DEPENDENCY, not a preference.** The mulligan generator reads its depth and budget
from `value_play` (`mull_gen_depth` / `mull_gen_budget_ms`, `expected_buckets`) in
`CritterLifegain.value.json` — a file **the value leaf's final stage writes**. Run it earlier and it
silently inherits the play depth and measures a run nobody would ever do. It also cannot be
worked around by hand-writing a `.value.json`: sidecar *presence* activates the value-leaf hybrid in
play, so creating one mid-generation changes the very play the value leaf is fitting.

Bottoming is baked **on** unconditionally and there is no off switch — nothing to decide or report.
`mullgen.sh run` already runs both A/Bs and the artifact check as one command.

v1 generated at **K=13** and was cheap; v2's K should be similar (same card classes, one new bucket at
most), but let the feasibility pre-check say so rather than assuming.

### Step 5 — regression ground truth

The deck changed, so **every tier's GT for CritterLifegain is stale by construction**. Re-run and
`--accept` per `.claude/skills/regression-testing.md`; do not hand-edit. Run `python3
test/check_gt_logs.py` afterwards if anything was rebased in between.

### Throughout

- **Never wrap a command in a timeout** — a truncated run reads as a result.
- **Check `[batch] heartbeat: N/M workers busy` inside the first ten minutes.** If it is not near M/M,
  fix the scheduling before letting it continue.
- Only the user cancels a user-requested run past ~10 minutes.

---

## Two things that need the user's call (neither blocks the pipeline)

1. **The 10 hand-played references belong to v1.** `references/CritterLifegain/claude_s*_gi*.json` were
   played on the v1 list and matched 10/10 (`ref_bench` 4.600 = 4.600). They are **commit-only — never
   revert, overwrite or delete them**, and nothing in this pipeline touches them. But after v2 they no
   longer correspond to the shipped deck, so that 10/10 is a statement about v1, not about v2.
   **Default taken: leave them exactly as they are**, and read them as v1 references. If you want a
   play-quality check on v2, that means playing new reference games on v2 — your call, not something
   to generate.
2. **Does v2 replace CritterLifegain in the regression suite, or sit beside it?** **Default taken:
   replaces** — `test/regression_cases.sh`'s `DECK_FILE`/`DECK_PROF` keep pointing at
   `decks/CritterLifegain/`, which now holds v2, and v1 lives on only as an archive. Adding v1 as a
   second suite deck would cost suite time in every tier for a list we no longer play.

---

## Outcome (2026-09-15) — what each stage actually measured

Run on ONE frozen commit throughout: `git rev-parse HEAD:src` = `7129a4d1`, unchanged across all four
commits (every commit touched only `decks/`, `test/` or `docs/`). Each generation stage ran alone on
the box at 29–31 of 32 cores.

### Step 2 — play profile

Coverage clean, 15/15 `full`, 0 missing. The profile prices the two headline changes independently of
the six screens that chose the list: **Ocelot Pride `[0.306, 0.211]`** is among the deck's best cards
and its 2nd copy still scores high, and **Serra Ascendant `[0.024, 0.150]`** scores *higher* on the
2nd copy than the 1st — the format finding reappearing in a separate measurement.

### Step 3 — value leaf

The v1 queue **refused to resume**: the script's decklist fingerprint caught that every row and matrix
cell in it was fitted to the old list. Archived to `logs/vlq_critterlifegain_v1_thune4_basilica/` and
regenerated from scratch — 11,275 rows from 2,500 games, held-out RMSE 0.4545, 52/52 matrix cells at
400 games, 0 condemned.

| arm | avg | delta | paired t | better/worse | cost |
|---|---:|---:|---:|---|---:|
| live (no sidecar) | 4.50887 | — | — | — | 3844 core-s |
| **staged (new leaf)** | 4.50725 | **−0.00162** | **−3.05** | 7/1 | **1175 core-s (0.31x)** |

Better *and* 3.2x cheaper — the no-drawback case that is pre-approved for adoption.

Two results the harness itself flags as **not** evidence: `value_trust_depth=5` was "accepted" but all
8 seeds are byte-identical, so the trust lever never engaged; and the V4 trust candidate gap `+0.0037`
is inconclusive against `tol=0.0020`, left unset.

**A shape probe was run first** (`scripts/shape_probe.py`, minutes) and is worth recording because it
raised a real question the leaf then answered. It found `fit_nl` — leafless, `ladder: single` — at
**0.24x** the plain rollout ladder with z +2.4, and only **6 games in 2,000 differing at all** between
shapes. That is the "fast decks prove everything inside the horizon" signature. It could not settle
leaf-vs-`fit_nl` (it compares only model-less shapes against `heur`), and the generated leaf then
measured 0.31x with quality over 8,000 games rather than 2,000. **Open item:** an on-policy screen of
the adopted leaf against `fit_nl` is the rigorous version of that question and has NOT been run.

### Step 4 — mulligan profile

Ran as the **first-version** case, correctly: v1's table buckets Orzhov Basilica and knows neither
Ocelot Pride nor Remote Farm, so every v2 hand would hit an unbucketed card and fall through. There
was no comparable prior table to A/B against.

K=13, entries 28997, sub_cells 38048, min_rollouts 40, `bottoming_enabled=True`.

| gate | delta | detail |
|---|---:|---|
| keep (exhaustive vs static) | **−0.196688t** | 16/16 seeds, mean/se −37.84 |
| bottoming (blind exh vs lookahead, confound-corrected) | **−0.025062t** | 16/16 seeds, mean/se −13.22 |

#### Why K=13, and what merged — the interesting part

15 distinct cards, two discovered merges. Discovery is objective-relative: swap A for B across 400
probe hands sharing a fixed library (CRN, so the rollout is deterministic) and see whether the
clairvoyant win-turn ever moves.

| bucket | members |
|---|---|
| 0 | Soul Warden + Soul's Attendant *(v1 merged these too)* |
| 1 | **Ajani's Pridemate + Voice of the Blessed** *(new in v2)* |

**The new merge is caused by this list's mana base, not by discovery being sloppy.** The two cards
differ only in cost (`{1}{W}` vs `{W}{W}`) and in Voice's counter thresholds (flying+vigilance at 4,
indestructible at 10). Voice's upgrades are all inert against a passive goldfish opponent that never
blocks, never attacks and casts no removal; and in a mono-white deck where every land makes `W`, the
cost difference is inert too. **In v1 it was NOT inert:** Orzhov Basilica makes `{W}{B}`, so a lone
Basilica could cast Pridemate (`{1}{W}`) but never Voice (`{W}{W}`) — which is exactly why v1 kept
them in separate buckets. Cutting the Basilica removed the last thing distinguishing them.

> **Deckbuilding caveat.** The sim now reports these two cards as identical **by construction**, so it
> cannot be used to screen Pridemate vs Voice. In a real game Voice's flying at 4 counters is genuine
> evasion.

**Remote Farm did NOT merge into Plains** (separate buckets 8 and 10). That is the specific failure
mode worth watching — a profile-less generation once merged a mana source that could not cast the
deck's key card. Discovery correctly kept them apart: Remote Farm enters tapped with depletion
counters and sacrifices itself after two uses.

#### Artifact shape

Ship **gzipped**; the uncompressed `.json` is deliberately removed. A batch run warned it had resolved
the sidecar to the uncompressed fallback, and that `.gz`→`.json` resolution flip is the leading
suspect in a past batch-pool contamination. Verified after compressing: no warning, and the table is
provably live — same seed, d3/b10, 120 games, **4.3250 shipped vs 4.4833 with
`MTG_EXHAUSTIVE_PROFILE=none`**.

### Step 5 — regression GT

All 20 cases were stale by construction and all 20 moved **faster**; mean ≈ −0.37t. Searched depths
(−0.39…−0.43) land on the −0.3274 the independent 80,000-game screen predicted, from a different
apparatus and disjoint seeds. Only critter keys changed; `check_gt_logs.py` over the whole corpus:
456 consistent, 0 STALE, 0 missing.

**Do not re-review the 571 searched "slower" games as a play regression.** That review exists to catch
the engine deciding worse on the *same* game; the decklist changed, so GT `gi=N` and new `gi=N` share
a seed and nothing else. The log contains a direct self-contradiction showing the tooling cannot speak
to this case — `.wins` says `gi11: 5->6` (slower) while `explain_game` says `old T7 -> new T6`
(faster), because `explain_game.py` replays the **old binary against the current deck file** and so
reproduces neither game. `.wins` is trusted on exactly this disagreement.

Reference reproducibility (`--strict`) is **clean**: 0 play-drift, 0 enum-gap. The 10 `mull-drift` are
the 10 hand-played `references/CritterLifegain/` games — played on v1, and unable to reproduce their
opening hands against a v2 library. `mull-drift` never gates. Those references are untouched
(commit-only) and should now be read as **v1** references.

## Still open

1. **On-policy screen: the adopted leaf vs `fit_nl`** (leafless, `ladder: single`, `alpha: relaxed`).
   The shape probe suggests v2 may prove nearly everything inside the horizon; the leaf is a clean win
   over *no sidecar*, but was never measured against the best leafless shape.
2. **New reference games on v2, if a human play-quality check is wanted.** The existing 10 are v1's.
