# The mana-clean gate: no deck leaves beta without a clean mana analysis

USER, 2026-11-02: *"I would also like to change things so that a requirement for a deck to pass beyond beta is a
mana analysis that passes on a clean set of games. An example could be 5000 games with no issues found and if we
find any we fix those and then need to run on another 5000 games. Naturally, some decks are unlikely to have any
mana issues and should just run 5000 games."*

## The rule

The play viewer grades a deck alpha / beta / stable (`tools/play/server.js` `tierFrom`, `tools/play/README.md`).
Beta -> stable needs three things:

1. 30 reference games.
2. A green reference bench.
3. **A clean mana analysis.** This one is new.

A mana analysis is one run of `scripts/mana_clean.py` for the deck:

- **Games:** at least 5,000, at the deck's shipped play settings (its profile), with the mana completeness oracle
  on (`MTG_MANA_ORACLE=2`, `docs/design/mana-completeness-oracle.md`). USER, same day: *"5000 would probably be the
  minimum, but if we can easily run 50000-100000 without it taking a long time that would be even better."* So a
  run is sized per deck to about 30 minutes of wall on every thread (`--minutes`), clamped to 5,000-100,000 games
  (rounded down to 1,000). The cost per game comes from the deck's last record, else from a probe of 2 x threads
  games on both arms. Census3 costs put about 25 decks at 100,000 games, the mid decks at 25,000-50,000, and
  Hinata2, Eldrazi, Selesnya and Melira at the 5,000 floor (Melira about 4 h).
- **Identity check:** the same games are also played with the oracle off. Play must be byte-identical (the per-job
  play digests), because an oracle that moves play is auditing games the deck does not play.

**Clean** means every one of these holds:

- No flag of an ISSUE class:
  - every class the oracle counts as an engine miss (`mana_oracle.MISS`: ENUM-GAP, APPLY-DROP, APPLY-DROP-ORDER,
    and the DOMINATED-* payment classes except DOMINATED-LAND);
  - ORACLE-GAP (the oracle could not reproduce the engine's own outcome, so the game was not fully audited).
- Play identity holds.
- At least 5,000 games were played.

The context classes are counted in the report but do not fail a run: PHANTOM, CONTINUATION-DIFFERS, DROP-COVERED,
DOMINATED-LAND, HIDDEN-INFO, UNMODELED and CAPPED.

**Fresh games every run.** Game `g` of a deck's sequence is seed `30000000 + g`, game index `g`. Run `k` plays the
games after run `k-1`'s. So once issues are fixed, the re-check is on games the fixes were not fitted to.

**The latest run is the verdict.** Each record is stamped with `HEAD:src` and the decklist's hash (line endings
folded).

- **A later src commit makes the record STALE.** STALE carries the last verdict forward. This is the reference
  bench's rule (USER 2026-08-29: a deck is beta *"only if they were not stable before"*): a commit stales every deck
  at once, so staleness says nothing about any one deck.
- **A changed decklist voids the record.** That run audited other cards.

## The tool

```
python3 scripts/mana_clean.py status               # per deck: verdict, last run, next game block, DUE
python3 scripts/mana_clean.py run                  # every DUE shipping deck, one pooled batch per arm
python3 scripts/mana_clean.py run niv decks/Snow   # named decks, due or not
python3 scripts/mana_clean.py run --resume <name>  # finish an interrupted run (same HEAD:src required)
```

**Which decks are DUE:**

- never run;
- the decklist changed;
- the last run was short;
- the last run had issues, and src has changed since then.

A deck whose last run had issues on the current src is not due: the same engine finds the same defects. Fix them
first, or name the deck to force a run.

**How a run is organised:**

- **The binary:** `build/PGO` from a clean `src/`. The PGO stamp is the record's proof of which engine ran, so the
  tool refuses a dirty src or a stale PGO build.
- **Pooling:** each deck's games are split into jobs of at most 250 games, about 2 minutes of work each and at
  least 8 jobs per thread, so one deck alone still fills the box and its tail stays short. Both arms are each ONE pooled `mtg --batch` over every
  deck, with no barrier between decks.
- **Resume:** the jobs are also the resume granularity. `--resume` reruns only the jobs no earlier attempt finished.
  A job's flags are counted only from the attempt that finished it.
- **Log size:** the oracle's stderr is about 40 KB per game at full detail, so it is filtered as it streams. ISSUE
  lines are kept whole; the context classes are cut to one-line counters.
- **Where the output goes:**
  - on the host drive: `logs/mana_clean/<name>/report.md` and `run.json`;
  - on the build volume: `build/scratch/mana_clean/<name>/`;
  - the record: `test/mana_clean.json` (committed).

**What the report holds:**

- a verdict table with per-class counts;
- the ISSUE flags grouped by signature, each with a single-game repro command (`MTG_MANA_ORACLE=3`, one game).

**After a run:**

1. Commit `test/mana_clean.json`.
2. For each deck with issues: root-cause it, fix it by mechanism (never a lever: the NO-GREEDY and
   completeness rules hold), land the fix through the usual validation, then run the deck again on its next block.

## Choices taken by default (USER to confirm)

- **The ISSUE set is the oracle's full MISS set plus ORACLE-GAP.** This is the literal reading of "no issues found".
  The DOMINATED-LIFE class (a painland's coloured face where a painless source pays) is common on painland decks:
  EDF showed 325 in 20 games after the 2026-10-31 fixes. Under this definition those decks cannot pass until it is
  fixed. The narrower alternative is to fail only on lost options (ENUM-GAP, APPLY-DROP, APPLY-DROP-ORDER) plus
  ORACLE-GAP, and report the DOMINATED-* classes without failing on them. That is a one-line change to `ISSUE` in
  `scripts/mana_clean.py`.
- **UNMODELED and CAPPED are disclosed, not failed.** On a plan where either fires, the oracle gives no verdict. So
  a deck can pass with some plans that were never verified. The report shows their counts per deck.
- **Archived lists** (`decks/<Deck>/<Version>/`) have no record and stay beta. The tool runs shipping lists only.
