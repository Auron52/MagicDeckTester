# Mulligan Checker

A static, client-side page for checking an opening hand against a deck's **mulligan table**. You
build the hand you actually drew (or deal a random one), choose how many mulligans you have taken and
whether you are on the play or the draw, and it tells you:

- **Keep or Mulligan**, graded **Clear / Likely / Marginal**,
- **which cards to bottom** after a mulligan, with every alternative ranked,
- **how many turns** separate the call from the alternative, with the standard error and the number
  of simulated games behind it,
- the same 7 cards' call at **every mulligan count**, and on the other side of the coin.

It needs no server, no engine binary and no build: `index.html` and `keepcore.js`, plus `decks.js`,
the generated list of this repo's decks.

## Use

1. Open `tools/mulligan/index.html` in a browser (`file://` works: double-click it).
2. Choose a deck from the dropdown. It lists every deck in this repo with a mulligan table, current
   lists first, then **Older lists** (archived `v<N>-…` folders and the like).
3. The first time, the page asks for the folder: click **Choose the folder…** and pick the repo's
   `decks` folder (the page shows its path), or the repo folder above it. A page opened from disk may
   only read a folder you have allowed. Chrome and Edge remember it, so later visits ask at most for
   one **Allow** click, and none if you choose "Allow on every visit" where the browser offers it.
   Browsers without the folder picker (Firefox) fall back to a folder upload dialog each visit.
4. The table starts loading at once, and you can build the hand while it does. Click cards in the
   deck grid to add them to the hand, and click a hand card to remove it. Or press **Random hand**
   (<kbd>R</kbd>).

**Other folder…** (or dragging a folder onto the page) loads decks from anywhere else, such as a deck
folder outside this repo.

`decks.js` is generated: run `node tools/mulligan/build_deck_list.js` after a deck gains or loses a keep
table. The gate below fails while it is out of date. A deck missing from it still loads, since the
dropdown is rebuilt from the folder itself once the folder can be read.

The hand is kept in the URL (`#hand=A|B|…&m=1&draw`), so a hand can be bookmarked or sent.

Browsers: anything with `DecompressionStream` (Chrome/Edge 80+, Firefox 113+, Safari 16.4+). Card
images and the lands/spells sort come from Scryfall, like the replay viewer. A decklist name Scryfall's
bulk lookup does not know is retried with an exact-name search, which also matches printed names, so
an alternate-name printing such as Secret Lair "Rick, Steadfast Leader" gets that printing's art. Offline, the page still
works, with card names in place of images and the deck in decklist order.

## What it reads, and why it matches the engine exactly

Per deck, from the deck folder:

| file | used for |
|---|---|
| `<stem>.cod` / `<stem>.txt` | the decklist: the hand builder, random deals, and the draw probabilities |
| `<stem>.keepmodel.exhaustive.raw.json.gz` | **the data**: per hand composition and play/draw, the rollout count, sum and sum of squares |
| `<stem>.profile.json` | `mulligan.keep_table_alias` only (the same card-rename stand-ins the engine applies) |
| `<stem>.keepmodel.exhaustive.profile.json.gz` | only its first few thousand entries, to confirm the bottoming rule (below) |

The compiled table the engine plays from (`…exhaustive.profile.json.gz`, up to 1.7 GB of JSON) is a
pure function of the raw sidecar, so the page recomputes it instead of loading it:

- **keep flag**: `KeepVal(h, m) <= Dopt[m+1]`, with a forced keep at the table's last mulligan level.
  `KeepVal` is the best mean win turn among the hand's `(7-m)`-card sub-hands, and `Dopt` is the
  backward induction over every 7-card draw weighted by its hypergeometric probability.
- **bottoming**: the first strict argmin of the table's score over those sub-hands.

These are the rules in `src/analyzer/ExhaustiveKeep.cpp` (`KeepVal`, `ComputeDopt`,
`BuildPolicyFromTables`), with the same floating-point operations in the same order, so the decisions
are **bit-identical**, not approximately equal.

Two build-time choices change the bottoming rule, and a raw sidecar does not always record them:

- **adaptive bottoming**: the argmin only admits cells refined past the floor (`count > sub_target`),
- **shrinkage** (`MTG_KEEP_BOTTOM_SHRINK`; FiveColour): candidates are ranked on a shrunk score. Each
  cell borrows strength from a precision-weighted quadratic fit over every cell holding the same
  cards. `keepcore.js` ports `FitBottomPrior` operation for operation.

So on load the page reads the first ~16 MB of the compiled table and keeps the rule that reproduces
it. The sidebar says what it found. "The first N hands match exactly" is the normal case. If the
compiled table is absent, the page says the bottoming rule is unconfirmed (keep calls do not depend
on it). The rule only ever changes the bottoming of hands the table mulligans: for every kept hand
the candidates agree, because the generator always samples a keepable hand's best bottom fully.

**When the raw sidecar cannot reproduce the table** (calibration finds a differing keep call, or the
compiled table is deeper than the raw data), the page reads the compiled table **whole**, once, and
takes every keep call and bottom from it (2 bytes of keep flags plus 14 bottom targets per hand).
Numbers still come from the raw data wherever it covers the level. Past its depth the page shows the
engine's call marked "no statistics survive for this level". One number those lost levels fed into
the covered ones is the value of mulliganing past the raw data's last level, `Dopt[M+1]`, and it is
recovered from the table's own decisions. The keep flags at level M bracket it, and of the values in
that bracket, the page uses the middle of those whose backward induction also reproduces the shipped
calls at every lower level. That value is shown as recovered, with its spread.

The one deck in this state is **Anti-Lifegain**. Its table was deepened from 3 to 6 mulligans on
2026-07-14 (`ec297a5b`) by a run that warm-started from the committed 3-level raw sidecar, and that
run's own output was never committed. With the fallback, the page reproduces all six levels exactly;
the recovered value is 5.5125 ±0.0125 turns on the draw and 6.2345 ±0.0094 on the play.

**Memory.** The raw sidecar is up to ~300 MB uncompressed and is never held whole. It is decompressed
as a stream and parsed chunk by chunk straight into typed arrays (about 33 bytes per cell). The largest
deck, FiveColour, holds about 90 MB and loads in roughly 15–20 s. Every other deck loads in 0.1–5 s.

## How the numbers are defined

- **Win turn**: the average turn the deck kills a goldfish in the table's own simulated games. A game
  with no kill by `max_turns` (8) counts as 9. Lower is better.
- **Keep** is the hand's value after the best bottoming. **Mulligan to N** is `Dopt[m+1]`, the expected
  win turn of the next hand with every later decision made optimally.
- **±** is the standard error. For a hand it uses the generator's own noise model: the cell's
  per-rollout variance, shrunk toward its table's pooled variance by `se_prior` (8) pseudo-games, so a
  3-game cell cannot claim zero variance. For the mulligan value it is propagated through the backward
  induction, treating cells as independent. It is small, because that value averages many hands.
- **Confidence**: `z = |gap| / combined standard error`. **Clear** means z ≥ 2 (≈98%+ that a fresh,
  larger sample would make the same call), **Likely** means 1 ≤ z < 2 (≈84–98%), **Marginal** means
  z < 1. The tiers are presentation only and change no decision. They are defined once, in
  `KeepCore.TIERS`.
- **Games**: the rollouts behind the specific number shown. Tables sample adaptively, so an obvious
  mulligan may have stopped after 3 games while a close call ran the full R.
- **Goldfish only.** The opponent in every game has 20 life (`start_life`) and never casts a spell,
  blocks or interacts. A card is worth exactly what it adds to that kill, so removal, counterspells,
  discard, blockers and life gain count for little, and no hand pays for being slow under pressure or
  fragile against interaction. That changes the answer only when the call is close, so every
  **Marginal** verdict or bottoming carries a short reminder. A Clear call carries none.
- **≡**: cards in one equivalence bucket. The table cannot tell them apart, so bottoming either is the
  same choice. The engine bottoms the first such card in hand order (`AIEngine::BottomCards`).

## The gate

```bash
node test/mulligan_app_check.js                # every deck with a table (~1 min)
node test/mulligan_app_check.js Giants Snow    # just these deck folders
```

For each deck it runs the page's own code path (`keepcore.js`: the streaming load, backward induction
and calibration), then streams the **whole** compiled table and compares every keep flag and bottoming
target, for every hand, mulligan count and play/draw. Any difference the page would show as a
confident call fails the run. A difference the calibration detected, which the page flags, is a WARN.
Run it after regenerating a deck's mulligan table, or after changing the bottoming rules in
`ExhaustiveKeep.cpp`.

A deck that matches only through the shipped-table fallback is WARN: the page is right, but the
artifacts still have a gap. Status at creation (2026-10-16): 28 decks exact, and **Anti-Lifegain
WARN**, reproduced at all six levels by the fallback (see above).

## Porting it elsewhere

Copy `index.html` and `keepcore.js` anywhere, including a static web host, and supply deck folders
with **Open decks folder…** or by dragging them in. Without `decks.js` the dropdown starts empty and fills
from the folder you open. `keepcore.js` has no dependencies and also loads in Node
(`require('./keepcore')`). Its API is `parseDecklist`, `discoverDecks`, `KeepTable` (`load`,
`computeDopt`, `calibrate`, `evaluate`) and `dealRandom`. That is the whole engine of the page if you
want a different front end.
