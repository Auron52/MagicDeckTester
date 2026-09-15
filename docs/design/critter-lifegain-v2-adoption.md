# CritterLifegain v2 — the list, and the adoption pipeline

**Status: SAVED, NOT INSTALLED.** `decks/CritterLifegain/CritterLifegain.cod` is still v1. This file is
the v2 list plus the exact steps to adopt it, written down so the work survives a context compaction.

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
