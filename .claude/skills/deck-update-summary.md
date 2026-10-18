# Deck-update summary skill

Measuring **a deck's current shipped list against the list it replaced**, and writing it up — "how
much did the deck actually improve when we adopted v2?". One pooled batch per format, then a document
with one page per deck carrying the new decklist, the cards added and removed, and the before/after
averages.

This is NOT the per-combination screen (`deck-screening.md`, which compares candidate lists *within*
one pool against a SHARED apparatus to decide what to adopt), and NOT the adoption pipeline
(`mulligan-profile.md` + `value-leaf.md`). It runs **after** adoption, on two lists that each already
shipped, and it answers a retrospective question. Nothing here regenerates an artifact.

Worked references, each with three decks, both formats, the win-turn distributions and the full method
section — copy their shape:

* `docs/design/v1-vs-current-stompy-angels-critter.md`
* `docs/design/v1-vs-current-fungus-kitty-whiteknights.md`

**Every run produces TWO outputs (USER 2026-10-18):** the full document above (the record), AND a
condensed, printable summary in `docs/deck-update-summaries/` (`<name>.md` + the rendered `<name>.html`)
— see THE PRINTABLE SUMMARY at the end. Worked references:
`docs/deck-update-summaries/v1-vs-current-{stompy-angels-critter,fungus-kitty-whiteknights}.{md,html}`.

**Six decks fit badly in one document and three fit well.** The first draft put all six in one file and
the user split it; a six-deck front page cannot carry both a headline table and a `<=T4` table without
one of them becoming unreadable. Group decks three at a time, and prefer a grouping that lets the front
page say something — the Fungus/Kitty/White Knights document is "the three most rebuilt decks", which is
also where the mean is most misleading.

## RULE 0 — EACH ARM PLAYS WITH ITS OWN SHIPPED APPARATUS, AND YOU MUST SAY SO

An archived list directory carries its own **full** apparatus, not just a decklist:

```
decks/<Deck>/v1-<slug>/<Deck>.cod
decks/<Deck>/v1-<slug>/<Deck>.profile.json
decks/<Deck>/v1-<slug>/<Deck>.value.json
decks/<Deck>/v1-<slug>/<Deck>.keepmodel.exhaustive.profile.json.gz
```

So point each arm at its own directory and let each play with the profile, value sidecar and keep
table that were fitted to it. That is the **correct** pairing for "is the deck we ship now better than
the deck we shipped then" — the list and its refitted artifacts were adopted together and they ship
together.

**It is also a deliberate confound, and the document MUST state it.** These numbers cannot answer "how
much of the gain came from the cards alone"; that needs both lists against one shared apparatus, which
is `deck-screening.md`'s design and a different (cheaper) experiment. Write the caveat into the
document rather than leaving a reader to infer it. Quote the margin against it: if the smallest gain is
~180x its own se and 10/10 blocks consistent, apparatus bias is not a live alternative explanation.

**READ BOTH `.value.json` FILES — THE ARMS CAN DIFFER IN SEARCH SHAPE, NOT JUST IN CARDS.** The
appendix-level caveat is not always enough. Fungus v1 ships a 136 KB learned value-leaf table while the
current list's own v2 generation concluded it was better off **leafless** — a 160-byte config whose
`value_play.leaf: "none"` substitutes `MidGameEvaluator::Constant()` and sets `value_trust_depth` to 0
(`apply_leaf_policy`, `src/ai/MulliganProfileIO.h:1251`). That is a different horizon treatment on the
two arms, and it moves **cost** as much as play — so a 5.7x cost drop cannot be attributed to the cards.
Diff the sidecars' top-level keys (`value_leaf_table` present or absent, `value_play.leaf`,
`escalation_skip_r`) and name the difference **on that deck's page**, not only in the appendix. It is
still inside "did the deck get better", because the leaf decision was the deck's own measured adoption.

## THE PAIRING CONTRACT — GET THIS WRONG AND THE ARMS SILENTLY STOP BEING PAIRED

Split each arm into blocks and make block *b* of both arms cover the **same global games**. Per
`BatchRunner.cpp`'s `Job::game_index` comment, a chunk covering `[off, off+n)` sets **both**:

```
"seed":       BASE + off        # shuffle seed of game i is seed + i
"game_index": off               # opponent-spawn index of game i is game_index + i
```

Set one and not the other and the two arms get different shuffles — the run still completes, the
numbers still look plausible, and the pairing is gone. Ten blocks of 50,000 gives 10 paired
observations; **the se you report is over the block deltas**, which is what absorbs shuffle variance.
Use a different `BASE` per format so the two experiments sit on their own seed blocks.

## VERIFY `provider=` PARITY ACROSS ARMS — THE ONE ASYMMETRY NOTHING ELSE CATCHES

Archetype detection is by card params, so **an edited decklist can cross a signature and silently give
the two arms different decision heuristics**. `ParseJob` prints it per job:

```
[play] angels-v1_b0 depth=7 budget=200ms source=cli provider=Angels
```

Grep every `[play]` line and confirm `provider=` matches across `cur` and `v1` for each deck. If one
arm reports `provider_detected=` as well, the provider was pinned and detection disagreed — stop and
resolve it before interpreting anything.

## SIZE THE RUN OFF A REAL BLOCK, NOT A TINY PROBE

A 50-game-per-job probe **over-estimated per-game cost by 5x** (61 ms/game projected; 12 ms/game
actual) because startup dominates a tiny sample. It turned a 20-minute job into a projected 1.6 hours.

So: use the probe only for *correctness* checks (provider parity, the `[play]` line, format
validation), and re-project from the **first completed full block** once the real run starts. Do not
scale a run down — or refuse one — on a small probe's timing.

## VALIDATE `max_turns` FOR THE FORMAT BEFORE SPENDING THE RUN

`max_turns` is 8 by default and a non-win is scored 9. At 2HG the opposing team has **30** life instead
of 20, so a slower deck can push games past the horizon — at which point the "average win turn" is
mostly measuring the non-win rate, and the two formats are no longer comparable.

**Probe win rates first** (a 12,000-game probe is ~1 min). If they are high — 98%+ across all arms —
the horizon does not bind and the formats stay comparable; record that you checked. If they are not,
raise the point with the user: changing `max_turns` changes what the metric means, so it is their call,
not a quiet adjustment.

## STANDARD AND 2HG ARE CO-EQUAL (user rule)

There is no weighting rule that makes one the headline and the other a footnote. Run both, and lay them
out side by side on each deck's page. **Expect the format to reorder the gains** — it did on all three
reference decks, and in one case by 60%.

**2HG is `starting_life 30` + `opponent_heads 2`** on the same deck and profile. Take that from the
"2HG gate" block in `test/regression_cases.sh` rather than from memory; it is also where the repo
records WHICH cards are 2HG-relevant per deck, which is the best lead for the mechanism step below.

## EXPLAIN A FORMAT-SENSITIVE RESULT FROM `cards.json`, NOT FROM THE ORACLE TEXT

When a deck's gain differs between formats, the cause is usually a card whose output scales with
`opponent_heads` — and **the oracle text alone will mislead you**. Read the `parameters` block:

* **Adeline, Resplendent Cathar** — `attack_creates_tokens: 1` and *no* `attack_tokens_per_opponent`
  key, so it takes the database default (**true**) and makes **two** tokens per attack at 2HG.
* **Hero of Bladehold** — `attack_creates_tokens: 2` with `attack_tokens_per_opponent: **False**`, so
  it makes two tokens at 2HG exactly as at standard.

Both are "attack trigger makes tokens" cards; only one scales. Trading a head-neutral one for
head-scaling ones is exactly the shape that shows up as a larger 2HG gain — and it is invisible unless
you look at the params. The default being *true* means **absence of the key is the scaling case**,
which is the opposite of what a reader expects.

## THE MEAN IS NOT THE HEADLINE — REPORT THE WIN-TURN DISTRIBUTION

**USER 2026-10-17:** *"some games are just losses and even games where you would win on T7 or T8 in a
goldfish are generally losses in a real game"*, and *"T6 or even T5 will also be losses against a
reasonably fast opponent."*

So the value of a win **decays steeply with turn number**, and the mean win turn prices a T6→T5 shift
the same as a T4→T3 shift. It therefore **systematically understates an improvement that lands in the
fast region** — which is where a good list adoption usually lands.

**It is free to fix.** The `.wins` files already hold every per-game win turn, so one pass gives a
histogram plus the only two aggregates that matter: **`<=T4` (wins that count)** and **`>=T6`
(games that are losses in practice, non-wins included)**. Put that table on every deck page, above or
beside the means.

What it caught on the reference run, where the mean was actively misleading:

* **Kitty**: mean −0.4423t (the middling one of six) but **T3 wins 13.26% → 50.56%** — it kills on turn
  three in half of all games, up from an eighth — with its `>=T6` tail unchanged (7.30% → 7.21%). All of
  its extra non-wins sit in the zone that does not matter.
* **Angels v1 and Fungus v1 were dead-zone decks** — 27% and 31% of games at T6-or-later — and the
  current lists cut that to ~4–7%. No mean conveys that.

**Corollary: do NOT hedge a faster list for whiffing more at the far tail.** A lower `max_turns 8` win
rate is not a defect when the mass moved forward; it is the deck converting marginal grind-outs into
fast kills. Report win% as a plain fact without the cautionary gloss. Full ruling:
`goldfish-late-wins-are-losses-anyway` in agent memory.

**A `0.00%` IN THE TABLE IS A ROUNDING, NOT A ZERO — COUNT THE GAMES BEFORE WRITING "NEVER".** Fungus
v1 at 2HG shows `T4 0.00%`, and the temptation is to write "v1 never won by turn 4". It won twice in
50,000 games. Go back to the `.wins` files for the raw count and state that instead ("2 games out of
50,000"), which is both true and more striking than the rounded zero.

**NEVER PUT A NUMBER IN THE DOCUMENT THAT A RUN HAS NOT PRODUCED — NOT EVEN AS A PLACEHOLDER.** Drafting
a page while its last cell is still measuring is the right use of the waiting time, but the empty cell
must be a visible marker (`«PENDING»`) that cannot be mistaken for data. Make the fill-in mechanical: a
helper that reads the `.wins` files and emits the finished markdown rows, and that **exits non-zero
rather than printing anything** while the format is short of its expected blocks. Validate the helper by
running it on a cell that is already in the document and diffing — it confirms the transcription and the
tool at once.

## CONFIRM WHAT `avg` MEANS — DO NOT READ IT OFF THE SOURCE

`BatchRunner.cpp:1485-1486` reads as if `average_win_turn = sum / games_won`, i.e. won games only. The
reported `avg` is in fact **already the penalised average**, scoring a non-win as 9. Verify it the cheap
way — run with `--game-log-dir` and recompute both forms from the `.wins` files — rather than trusting
either reading. Report both per deck:

* `avg(pen=9)` — the headline. It cannot be gamed by converting a slow win into a non-win.
* `avg(won)` — mean over won games only, alongside it.
* **win% and the raw non-win count**, because a list that closes games its predecessor lost outright is
  making a real gain that a win-turn average understates. On the reference decks the penalised delta
  was consistently *larger* than the won-only delta, which is the signature of exactly that.

## RUNNING IT

ONE pooled batch per format — never a loop per deck or per arm, and never waves.

```
build/PGO/mtg --batch run.manifest.json --game-log-dir build/scratch/<name>
```

* **Check utilisation in the first ten minutes.** `[batch] heartbeat` should be near M/M.
* **Per-game logs go to `build/scratch/`**, not `logs/`: 3M per-game records are regenerable churn and
  `logs/` is on the 9p host mount. Copy only summaries back.
* **`--game-log-dir` is passive** — verified by running a 3M-game batch with and without it and getting
  byte-identical digests on all 60 jobs. So one pass with logging is enough; you do not need two.
* Expect **2HG to cost ~3x standard per game** (longer games, more board state). Budget for it.
* `max_turns` 8 and `depth`/`budget_ms` pinned per job. A depth well above the suite's cases (the
  reference run used d7/b200 against suite cases at d0/d3/d5 and 0/10/20 ms) makes the comparison about
  the *lists* rather than about which one copes with a tight budget. Check first whether the decks set
  `value_play.target_depth`: if none does, `drives()` is false and a pinned depth is simply deeper, not
  off-policy. If one does, say so — its play-levers will not fire off-policy.

## THE DOCUMENT

One page per deck (`<div style="page-break-after: always;"></div>` between them), after a short front
page carrying the headline table and the method. Per deck page:

1. **The current decklist**, with its land count.
2. **Cards ADDED** and **cards REMOVED** as two separate lists — not one mixed +/- table. Flag which
   are wholly new (`0 -> N`) or wholly cut (`N -> 0`) versus merely re-counted, so the page reads as an
   applicable diff. Diff the `main` zone, and **check the sideboard separately** — on the reference run
   the two cards v1 kept as sideboard singletons were the two the new list promoted to 4-ofs, which a
   main-only diff reports as "new cards" and misses the story.
   **A "cut" may be a DEMOTION.** Kitty Equipment's current list carries a 23-card sideboard against
   v1's none, and six of its sixteen main-deck "cuts" (Bonesplitter, Umezawa's Jitte, O-Naginata, Sol
   Ring, Armored Skyhunter, Kor Duelist) reappear there as singletons — so a main-only diff overstates
   what left the deck. Say "demoted to the sideboard", not "cut". A 16-row removed list also needs a
   two-wide table to stay on one page.
3. **Both formats' averages** in one table: win%, non-wins, `avg(won)`, `avg(pen=9)`.
4. **Block delta range** (`-0.6424 to -0.6579`) and `n/10 blocks better`. A narrow range across
   500,000 games is the evidence that the effect is flat rather than carried by a few blocks.
5. **Cost per game in both formats.** It moves independently of quality and in both directions: of the
   three reference decks one was flat, one 35% dearer, one less than half the cost. A deck that got
   dearer matters for sizing its tier rows.

**Stamp the engine.** Record `git rev-parse HEAD:src` in the document. These runs take an hour and the
branch moves under you — the reference document is stamped `55191e63` and the tree had already moved to
`b6985a8a` by the time it was pushed. The stamp is what keeps the numbers interpretable later; without
it a reader cannot tell which engine produced them.

**Embed the manifest generator in the appendix.** The run apparatus lives under `logs/` (gitignored),
so a document that only references those paths is not reproducible from a clean clone. The generator
*is* the experiment definition — especially the `seed`/`game_index` pairing — so paste it in full.

## THE PRINTABLE SUMMARY (`docs/deck-update-summaries/`)

The full document is the record; the user also wants something they can print and read in one sitting.
USER 2026-10-18: *"make each of the sections fit on one page"*, *"Focus on cutting low-priority content
and focus on headline numbers and changes"*, *"Could we show just the common part of the decklist as
well?"*. Write `docs/deck-update-summaries/<name>.md` (same `<name>` as the full document), then render:

```
python3 scripts/md_to_print_html.py docs/deck-update-summaries/<name>.md     # -> <name>.html beside it
```

One page per section, split with the same `<div style="page-break-after: always;"></div>` divs:

* **Front page:** the headline table (deck, format, v1, current, change, blocks better: no se/t, no
  games/arm), the `<=T4` / `>=T6` table, three or four takeaway bullets, and a two-line method note
  (games, depth/budget, `max_turns`, 2HG definition, paired blocks, own apparatus per arm).
* **Per deck page, in this order:**
  1. The headline in the `#` title, plus one bold summary sentence.
  2. **Shared core**: the cards both lists play, at `min(old, new)` copies, three columns wide.
  3. **Cards added** and **Cards removed**, adjacent with nothing between them. The renderer puts the pair
     side by side, but only when the two `h2 + table` blocks are adjacent. A note goes after both tables.
  4. **Win-turn table**: columns `T3 | T4 | T5 | >=T6 | <=T4`, rows v1 / current / change for each format.
  5. **Averages and cost**: one row per format, with `avg turn v1 -> current | change | win% | ms/game`.
  6. Two or three bullets: the mechanism, and anything a reader would otherwise get wrong.
* **Cut:** the full current decklist (the core plus the changes covers it), the T2/T7/T8 columns, se/t,
  non-wins and `avg(won)` (keep a bullet when they tell a story, as Kitty's do), long explanations, the
  method detail, and the appendix.

Build the core, added and removed tables from the `.cod` files, never from the full document's text:
`python3 scripts/deck_list_diff.py <old.cod> <new.cod> --md` prints all three, and asserts that
core + added = new and core + removed = old. **Verify every number mechanically**: each number in the
summary must appear verbatim in the full document, apart from deliberate roundings, which you check by
hand. That is the same transcription check the full document gets.

**The renderer** (`scripts/md_to_print_html.py`, stdlib only, nothing to install) produces one
self-contained HTML file. It has one stylesheet for screen and print, A4 pages, and a fit script that
scales any section taller than a page down to fit. On screen, each page carries a red "printed at N%"
label (hidden when printing). **This container cannot render pages**, so ask the user to report any page
whose label shows a low percentage, and trim that page rather than shrinking it further.

**Wording — the user corrected both of these (2026-10-18):**
* **A goldfish measures SPEED, not deck strength.** Do not call a deck "losing late", "a dead-zone deck"
  or "losses in practice" from its goldfish turns. Interactive decks (lifegain, removal) push real games
  long. Angels v1 was "a good deck even before any changes"; its weakness was fast combo. Say "too slow
  against a fast deck", and note that the speed gain kept the decks' interaction.
* **A card-change reason comes from the card itself and the deck's alternatives**, not from a nearby
  mechanic. Arbor Elf was cut because it needs a Forest and has no upside over Fyndhorn Elves, not
  because it "doesn't scale" with Priest of Titania. If you don't know why a card changed, give the
  measured effect and leave the reason out.
