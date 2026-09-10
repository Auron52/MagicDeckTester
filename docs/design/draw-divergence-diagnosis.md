# Why "DRAWS DIVERGE" happens — and one case where the diagnosis is wrong

Investigated 2026-09-10 against the real old binary (`0ab355c7`, built in a worktree) for the five
overnight games the audit classified as physically-different rather than like-for-like. All five are
downstream consequences of a **real play-policy change**, not shuffle luck: the opening shuffle is
seeded identically and the mid-game CRN reshuffle (`Library::ShuffleByKey`) behaved exactly as
designed in every case. Four distinct mechanisms, and they are worth telling apart.

## The four real mechanisms

**1. A library-manipulating cantrip cast on a DIFFERENT TURN.** `hinata gi202` — opening hands and
mulligan sequences byte-identical, both lines draw the same card at T2's draw step. Old casts
**Ponder** (`draw_spell`, `cast_reorder: 3` + `draw: 1`) on T2; new defers it to T3 and casts
Ornithopter instead. Ponder REORDERS the top three and draws, so from that point the two lines hold
genuinely different libraries. `hinata gi232` is the mirror image (new casts Ponder on T2, old does
not).

**2. Scry / exile effects, same story.** `hinata gi255` — T1 and T2 identical; old then casts
**Preordain** (`cast_scry: 2`, which BOTTOMS cards) and **Expressive Iteration** (top three: one to
hand, one to bottom, one exiled) on T3 while new casts Hinata. Both genuinely rewrite the library.

**3. The bottoming choice.** `melira gi17` — the audit says KEPT HANDS DIFFER and is right, but the
detail matters: **both lines were dealt the identical 7 on attempt 0 AND on attempt 1, and both
mulliganed exactly once.** The only difference is which card went to the bottom — old bottomed
`Caves of Koilos`, new bottomed `Birthing Pod`. That is a London-mulligan bottoming policy change
(bottoming is decided by clairvoyant rollouts, so any search change can move it), not a different
shuffle.

**4. Nothing at all — the SAME library consumed at a different RATE. This one is MISDIAGNOSED.**
`mirrorwing gi173` is reported as *"a fetch/shuffle resolved differently; physically different from
there on"*. **Mirrorwing's line contains no fetch, no tutor and no shuffle effect whatsoever.** What
actually happened, proved by card numbers:

* NEW T3: hand `{35,40,51,9}` -> casts Ignoble Hierarch (40) and **Oracle's Restoration** (51), whose
  `cast_draw: 1` draws **card 13 (Forest)**, and then plays that same Forest as its land drop -> `{35,9}`.
* OLD T4: draws **card 13 (Forest)** at its draw step and plays it; its own Oracle's Restoration
  (cast a turn later) then draws **card 30** — exactly what NEW draws at *its* T4 draw step.

Identical library, identical order, consumed **one card apart** because the same cantrip was cast one
turn earlier. Hand-size arithmetic shows it independently: OLD T4 draws to 4, plays a land and casts
two spells (-3), and ends on **2**, not 1.

## The cause of the misdiagnosis: `cast_draw` draws are NOT logged

`EffectHandler.cpp` resolves `cast_draw` with a bare
`cp.library.DrawN(def.params.cast_draw, cp.hand)` and **never calls `LogDraw`**, while the turn's
draw step (`GameEngine.cpp`) and the `draw_spell` template both do. So Ponder's draw appears in the
game log and Oracle's Restoration's does not.

`test/explain_game.py` then compares the two logged draw sequences with a naive
`zip(old["draws"], new["draws"])` and reports the first index where the card names differ. An
unlogged draw shifts one line's stream by one, so the tool compares two different game moments and
concludes a shuffle resolved differently. It has no way to know a draw happened.

Two further caveats found while confirming this, both about the same tool:
* **`explain_game.py` runs the games itself as SINGLE-GAME probes.** The batch reuses one AI engine
  per job (`cached_job`), so suite games run with a WARM search memo; a single-game probe is cold.
  Its old-vs-new per-turn diffs are therefore indicative, not the suite's actual line.
* **It pins `--depth D --budget-ms B --ignore-play-profile`.** For a deck whose manifest job carries
  no `depth` (hinata, melira, mirrorwing — their depth comes from the profile's `value_play`), that
  overrides the deck's real search shape.

## Fix options for the logging gap

The obvious fix — call `LogDraw` from the `cast_draw` path — is **not free**: `LogDraw` folds into the
play digest (`FoldStr("D"); FoldInt(card_num)`), so it would change the digest of every game on every
deck running a `cast_draw` card and force a ground-truth rebaseline across the suite.

The zero-churn alternative is a `LogDraw`-style call that appends the `Action` for the log/viewer but
skips the digest fold, making the log complete for humans and diagnostics while leaving every
existing digest byte-identical. That is the recommended route; it is not applied here because it is
an engine change beyond the scope of the question that prompted this investigation.

## "Physically different game" is NOT exculpatory — run the DEPTH/BUDGET LADDER on both binaries

USER, 2026-09-10: *"Even the library manipulation spell cases are only so legitimate. They should get
the same results at a higher budget and depth."* Correct, and testing it changed the verdict. If two
engines disagree about WHEN to cast a cantrip, that is itself a search-quality artifact: with enough
search both should reach the same decision. So the ladder — **both binaries, d5/d7/d9/d11 x rising
budget** — is the test that "different physical game" was letting people skip.

| game | memo ON (new) | memo OFF (old lever) | verdict |
|---|---|---|---|
| `hinata gi202` | 6 at d5, **5 at d7+** | 5 at every depth | costs a turn only at shallow depth — RECOVERABLE |
| `hinata gi255` | 6 everywhere | 5 at d5/b20, **6 at d7+** | NOT a regression; old's T5 was shallow-search luck |
| `hinata gi232` | **6 at every depth** | **5 at every depth** | **REAL, STABLE one-turn loss** |

`gi232` holds at T6 even at **d11 / b10240 — 512x the case budget** — so it is definitively not churn.

**Cause named by hatch sweep: `MTG_MEMO_WIN_ORDERFREE` (`d71b4157`, order-free WIN memo reuse,
default ON).** Setting it to 0 restores T5 at every depth; none of the other levers
(`MTG_FOLD_ACT_SOURCES`, `MTG_FOLD_HAND_CASTS`, `MTG_CAND_DEDUP`, `MTG_LADDER_EMULATED`,
`MTG_ENUM_MEMO`, `MTG_ID_ANYTIME`) moves it. Mechanism in the game: new casts **Ponder on T2** where
old holds it, which reorders its draws; at T5 new's line (Reality Spasm, Irencrag Feat, Crackle with
Power) leaves the opponent on exactly **1 life**, while old's T5 fits an extra Soulfire Eruption and a
second Reality Spasm for -1.

**But the lever is an improvement, measured on the whole deck.** A/B over all 16 hinata /
hinata2hg overnight keys: **better on 8 keys, worse on 0, equal on 8, sum -0.1409**. Every d0 key is
byte-identical, confirming it is search-only. So `gi232` is a genuine but isolated cost paid against
many gains — the rebaseline stands, and this is what "analyze the differences" is supposed to
produce: a named cause and a deck-level verdict, not a shrug at "different physical game".

**Bottoming is the one case that cannot converge by construction** (USER: *"the bottoming decision
on the other hand is an actual different game"*). Changing which card goes to the bottom changes the
library from turn 0, so the two lines are not the same game at any depth. (USER also noted it *might*
match given enough budget and search if lookahead bottoming runs at the same settings — untested,
and low priority.)

## What this does NOT change

CRN is working. `ShuffleByKey` orders the live library by `splitmix64(seed, m_number)`, so removing a
card leaves every other card's relative order intact, and the reshuffle seed
(`SearchShuffleSeed(game_seed, state.search_count)`) is keyed on the per-game shuffle ORDINAL — two
lines that reshuffle at the same ordinal stay aligned. No case among the five was a CRN failure.

The practical consequence for A/B judgement is unchanged: cases 1-3 ARE physically different games
and cannot be judged per-game, only in aggregate (hinata: 26 better vs 3 worse, net -25 turns — the
new policy is clearly better there). Case 4 is not physically different at all and should never have
been counted as variance.
