# Reveal visibility: never make the player guess what was revealed

**Rule (user, 2026-08-03):** a player must never have to guess **what was revealed** or **what we did
with it**. Two halves:

1. **Where there is a CHOICE, surface a picker.** Not a heuristic pick behind the player's back.
2. **Where there is a REVEAL, put it in the log — always.** Including the choice-free effects, which
   are exactly the ones that surface no picker to infer the outcome from.

## What is DONE (2026-08-03)

**The picker half.** An ETB tutor resolving from a *cast* was already a human choice: the search
enumerates one plan variant per candidate, so the viewer's variant dialog asks. But the same ETB
fires when the creature is **put** onto the battlefield — a Goblin Lackey combat cheat, an Aether
Vial deploy, a Muxus reveal — and that path has no plan and no variant, so `PerformTutor` fell
through to `cands.front()`. Reported case: Goblins seed 4, a Lackey-dropped Goblin Matron silently
searched up Goblin Chieftain. Fixed with `g_play_tutor_chooser` (`GameLogger.h`) → the `tutor_etb`
decision → the viewer's image picker, including a **decline** option (Matron reads "you *may*
search", a line the engine previously could not express).

**The log half.** Every reveal already funnelled through `GameLogger::LogReveal`, but that only
reached the *saved game log*: `--claude-play` attaches **no** `GameLogger`, so a
`if (g_reveal_logger)` guard dropped every reveal in the one mode a human is watching. Now
`EmitReveal()` (`GameLogger.cpp`) fans out to the saved log **and** a viewer sink
(`g_play_reveal_sink`), the decision JSON carries a `reveals` array, and the viewer prints one
history line per reveal with a per-card disposition. Because it is one chokepoint, it covers Muxus's
put/bottom split, a tutor's searched-up card, Treasure Hunt's reveal-until-nonland, scry/surveil and
Expressive Iteration alike — a new reveal site cannot add a reveal the player cannot see.

**Muxus is NOT a picker, deliberately.** Its oracle text is "Put **all** Goblin creature cards with
mana value 5 or less from among them onto the battlefield and the rest on the bottom" — mandatory,
no player choice. Offering a picker would be a rules violation. What it needed was *reporting*, which
is what it got: all six revealed cards, each tagged `→ battlefield` or `→ bottom of library`.

## THE RULE, restated (user, 2026-08-03): "what we would show in the viewer history should show in the log"

The first cut of this work had **two** disposition channels and a `viewer_only` escape hatch, both
added to dodge one fact: `GameLogger::LogReveal` folds `dispositions` into the **play digest**, so
logging something new forces a ground-truth rebaseline. Avoiding that cost made the saved log
strictly less informative than the screen — and it bit immediately: during the Goblins tutor-ranking
A/B, the Lackey cheat-into-play choices (the deck's entire engine) were absent from the rendered
log and had to be reverse-engineered from per-phase board deltas.

**That trade is the wrong way round. Pay the rebaseline.** `EmitReveal` now has ONE `dispositions`
channel feeding both sinks, and `viewer_only` is gone. Where a site supplies no explicit label, one
is derived from the kept / bottomed sets — by `RevealDisposition()`, the single shared derivation the
viewer and `scripts/render_game_log.py` both use, so the two cannot drift and no redundant string is
folded into the digest at every scry.

Measured blast radius of the collapse (smoke): **antilife, goblins, hinata** — the decks with
tutors. antilife and hinata moved **digest-only, `avg` identical to 4 dp**, i.e. pure reporting, no
play change. Every other deck passed untouched.

**Pinned by `test/reveal_log_parity_check.js`** (static, milliseconds, wired into
`test/viewer_checks.sh`), because this invariant has now broken once in *each* direction:

1. log-but-not-viewer — every site guarded on `g_reveal_logger`, and `--claude-play` attaches no
   `GameLogger`, so a Muxus put/bottom split showed as nothing in the one mode a human watches.
2. viewer-but-not-log — the `viewer_only` / `viewer_labels` hatches described above.

The check forbids both structural escape hatches (no `viewer_only`/`viewer_labels` symbols;
`EmitReveal` keeps exactly one disposition channel and no bool sink selector; `RevealDisposition()`
and the renderer's REVEAL rendering still exist). Verified against a negative control.

## What is DEFERRED

**Show the revealed cards visually, as a reveal step, before they are placed** — rather than only as
a history line after the fact. Today a Muxus reveal resolves instantly and the player reads what
happened; the cards themselves are never shown as art at the moment of the reveal.

**Why this matters more than it looks (user):** in **1v1** the reveal is *public information the
opponent also needs to see*. A goldfish player can reconstruct it from the log, but a real opponent
must observe the reveal as it happens — it is information they are entitled to and may act on. So
this is not cosmetic polish; it is a correctness requirement for the 1v1 mode, and it should be
designed in when the real-opponent work lands rather than retrofitted.

Shape it would likely take: a non-blocking reveal modal (the `dragon` / `lackey_put` panels are the
existing image-grid precedent) showing the revealed cards with their destinations, auto-dismissed or
acknowledged, driven by the same `reveals` payload the history already consumes — so the engine side
is already built. For 1v1 it needs to be shown to **both** seats, which is the part that does not
exist yet.

## Pointers

- `src/core/GameLogger.h` — `TutorChooser`, `PlayReveal`, `RevealVisible()`, `EmitReveal()`
- `src/core/GameLogger.cpp` — `EmitReveal` fan-out
- `src/core/SpellEffects.cpp` — `PerformTutor` (picker hook), `PerformMuxusReveal` (put/bottom split)
- `src/main.cpp` — `WriteTutorDecisionJson`, the `reveals` array on the main-phase decision
- `tools/play/index.html` — `tutorPanelHtml`, `commitTutor`, `logReveals`
- `test/viewer_decision_types_check.js` — pins every decision type as answerable by the viewer
