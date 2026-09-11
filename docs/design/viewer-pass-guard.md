# The primary button that changes meaning under your finger

> "it goes to the 2nd phase for no reason and I can't finish the combo"
> — USER, EldraziDisplacerFlicker seed 16, turn 4, 2026-09-11

Status: **shipped**, human play only, viewer-side. No engine change, no flag.

## The bug

The plan bar's primary button is **one element** whose label is

```js
`<button id="commit" class="primary" …>${empty ? 'Pass phase' : 'Commit Line'}</button>`
```

The instant a line commits, `S.plan` empties, `renderPlanbar` redraws, and that same element — same
`id`, same screen position, same `class="primary"` — becomes a **phase pass**, which throws the
floating pool away and ends the main phase.

That is fine when the player is deliberating. It is not fine in a go-off: grinding a 60-segment
Emiel/Cloud loop means clicking that one pixel over and over, and the click that lands just after a
commit resolves passes the phase instead of committing anything. From the player's seat the viewer
simply left the main phase "for no reason", and the combo is dead because the float is gone.

`commitLine`'s `if (S.busy) return` does **not** cover this: it swallows clicks that arrive *during*
the round trip, and the dangerous one arrives just *after* it. A slow frame makes it likelier, not
less likely — the reported frame enumerated 772 plans and ran a two-dimension choose-walk.

### Reproduced at the user's own frame

`--seed 16 --game-index 15`, turn 4, `main_ordinal 62`: the line
`Overgrowth → Brushland #1, Wild Growth → Brushland #1` (plan **742**).

| | choices appended | result |
|---|---|---|
| engine replay of the line alone | `742` | `pre_main` ordinal 63, pool `{G:6, C:2}` |
| the viewer, one further click | `742, -1` | **`post_main` ordinal 64, pool gone** |

So the extra `-1` is the client's, and the engine is innocent. It is **not** a `segmentsLeft`
mismatch (the two-cast line is one segment, `segmentsLeft` is 0 throughout), **not** the aura
sub-decision (the host walk resolves to 742 by either route — drag, double-click, or mixed), and
**not** the old server generation: `runStepCached`'s fast path only ever feeds a strict prefix
extension of what the client sent and respawns on any mismatch, and `/api/validate` spawns
statelessly, so no server generation can manufacture a choice the client did not send.

## The guard

**A pass that would discard floating mana needs a confirming second click.** Three rules, each
load-bearing:

| rule | constant | why it exists |
|---|---|---|
| a click within the dead time after the frame arrived does not even **arm** | `PASS_REARM_MS` 700 | this is the rapid-grind click — the bug. Arming on it would let a double-click arm *and* confirm |
| a confirming click must **dwell** after the arming one; anything faster **re-arms** | `PASS_DWELL_MS` 400 | mashing the button can never pass, however fast you click |
| the arm goes stale, and dies with the frame, with `Clear`, and the moment anything is queued | `PASS_ARM_MS` 8000 | an arm raised on the previous frame describes a pool that no longer exists |

**A pass with an empty pool stays one click.** Nothing is thrown away, undo covers a misclick, and
that is the common case across every deck — growing a confirmation there would be a tax on all 23
decks to fix a bug that only exists when there is a float to lose.

**No modal.** The user's standing objection to pop-ups (*"it's kind of a pain to constantly have
pop-ups"*) applies, and this is a confirmation, not a decision: the armed state is shown **on the
button** (`Pass phase — lose {G}×6 {C}×2?`, alpha-red) plus a hint. Putting it only in the hint
would leave a button that is still one click from the same loss, which is the failure being fixed.

### What must NOT be guarded

`advanceTo` auto-commits an empty plan on three paths — the dead-opponent auto-pass, the
**Commit turn** skip, and the multi-segment chain — each marked `dec._autoStep`. Those are the
client's own automation, not a human clicking a button that changed under them. Guarding them would
stall a decided game on its own confirmation and break Commit turn outright, so `passNeedsConfirm`
returns early on `_autoStep`. Conversely a refused pass **voids a pending `S.commitTurn`**, or
`advanceTo`'s skip would pass the whole turn on the next frame and the guard would have protected
nothing.

## Guard

`test/viewer_client_check.js` → `testPassGuard()`, driven at the user's own frame (seed 16, turn 4,
ordinal 62, the two-Aura line). Only a DOM can see this: the protocol sweep replays plan indices,
the save audit replays a finished stream, and the line-builder knows nothing of buttons — none of
them can produce a click that lands on a button whose label changed a moment earlier.

It pins four things, and three of them are ways the guard could be *worse than useless*:

* the rapid click is refused **and says why** (a silent no-op reads as a frozen viewer);
* a **double-click never passes**, however fast — the naive arm/confirm would reproduce the bug
  through the fix;
* a genuine pass still works in two deliberate clicks (not a lockout);
* a pass with an empty pool stays **one** click.

## Found while reproducing: `plans[i]` is not plan `i`

`applyAccepted` looked the committed plan up as **`decision.plans[planIndex]`** — by array position.
That is wrong whenever a frame overflows the emit cap (`MTG_PLAY_PLANS_CAP`, 200 by default;
`plans_total` carries the real count), because what is emitted is a **ranked top slice**, not the
first N by index. Measured on the same seed-16 turn-4 frame: **200 plans emitted, carrying indices
up to 771**, and `plans[i].index !== i` for most of them. So the positional read returned *some
other plan*, whose `actions` then drove the multiset guard that decides whether to emit the human's
`*` full-order pin — i.e. the pin was decided against a line the engine was not applying.

Fixed by looking up by `.index` (`planByIndex`), which is a strict improvement and can never be
worse: on an uncapped frame position and index agree, so nothing moves.

### What is still open

`planByIndex` does **not** recover the pin when the committed plan is *past* the cap — it is not in
the list at all, so `plan0` is null and no pin is emitted. On rich combo frames (which is where this
deck lives) the human's declared order therefore still reverts silently to enumerator order, which
is the defect `human-line-order-as-is.md` exists to prevent, reappearing through the plan cap.

Closing it needs the **accept validation** to carry the matched plan's own `actions` /
`cast_order_canonical` — the client already knows the plan index and the queued order, and is only
missing the engine's view of that one plan. That is an emitter change (`src/main.cpp`), so it is
recorded here rather than half-done in the client: guessing the plan's actions from the queue would
make the multiset guard tautological and pin every line unconditionally, which is precisely the
check it exists to be.

## Deliberately not done

* **Splitting the button in two.** A permanent "Pass phase" control beside "Commit Line" doubles the
  plan bar's primary actions for every deck and every turn, to remove a hazard that only exists for
  a few seconds after a commit, and it would still sit one click from the same loss.
* **A general click debounce on the plan bar.** The problem is not fast clicking, it is a *specific
  destructive action* appearing under a finger aimed at a constructive one. Debouncing everything
  would make the deliberate grind this feature exists to support feel broken.
* **Guarding on "a combo is live" rather than on floating mana.** The viewer cannot know what the
  player intends to finish; the float is the thing actually being destroyed, and it is exactly
  observable.
