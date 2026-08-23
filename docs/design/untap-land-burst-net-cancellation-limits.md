# The untap-land burst is modelled as NET MANA, which forecloses every non-mana use of the untap

**Status:** KNOWN LIMITATION, deliberately accepted for the goldfish (USER, 2026-08-23). Recorded
because it is expected to become a real gap for **1v1 play**, not because it is wrong today. Nothing
here is a bug report against the current engine.

## What is modelled

Wirewood Lodge is `{T}: Untap target Elf`. The engine reaches it only from inside a mana payment, and
models the whole `tap Elf -> Lodge untaps it -> tap Elf again` sequence as ONE net-mana operation
(`ManaPayment.cpp`, the `best_kind == 4` branch):

```cpp
const int by = UntapBurstBestYield(state, active, *bdef, /*require_tapped=*/true);
bp.tapped = true;                 // the LODGE taps
ConsumeFloating(floating, *feed); // pay the feed mana
floating.Add(*feed, by);          // credit the Elf's full yield
// ...the Elf's tapped state is deliberately UNCHANGED
```

The Elf is required to be **already tapped** for the burst to be offered at all
(`require_tapped=true`), and it stays tapped afterwards. Net effect: `+ (yield - 1)` mana, one Lodge
tapped, board otherwise unchanged. `UntapLandBurstNet` is the planner-side form of the same
identity, and `ManaSourceRank`'s tier 63 exists to make the executor realise that order.

**This is a sound and cheap model for a goldfish**, where the only thing an untap can buy is more
mana. It also means the tap-then-untap ordering the combo requires is guaranteed structurally by the
`require_tapped` gate rather than by any rank — the Lodge cannot fire before the Elf has tapped.

## What it cannot represent

Because the untap is never *materialised* — the Elf is never actually left untapped — no line that
uses the untap for anything other than immediate re-tapping exists in the plan space:

1. **Untap to ATTACK.** Tap the Elf for mana in main 1, spend the mana, then Lodge-untaps it so it
   can be declared as an attacker. Legal, and it converts a spent mana dork into extra damage on a
   lethal turn.
2. **Untap to BLOCK — the USER's case, and the sharpest one.** *"Alpha strike + untap your biggest
   elf to hold off a counterattack is not something we can represent."* Attack with everything, then
   untap the largest Elf so it is available to block on the crack-back.
3. Any other use of an untapped body (a tap-cost ability, a target that must be untapped).

## Why it does not bite today, and when it will

The goldfish opponent never attacks and never blocks, so **(2) is worth exactly zero** and **(1) is
worth only the marginal damage of one Elf**. That is the whole reason the net-mana collapse is an
acceptable approximation right now: the only currency in a goldfish is mana and turn count.

**1v1 changes the sign of (2) specifically.** Holding a blocker is a defensive resource with no
mana value at all, so a model that can only express "untap for mana" cannot express the defensive
line even in principle — it is not a matter of the heuristic ranking it low. Expect this to surface
as soon as the opponent attacks back.

## Consequence for tap-order measurement (recorded so it is not re-derived)

Because the burst bundles the re-tap, the *rank* of a scaled mana dork relative to the Lodge's tier
changes results only through that bundling. USER, 2026-08-23: *"the original order would normally not
have changed the results, except that we probably tap the scaling dork immediately for mana after
using Wirewood Lodge. If we just used it to untap I believe the two orders are the same."* That is
correct against the code above.

So any measured gain that comes from re-ordering a scaled dork against tier 63 is measured **against
this modelling choice**, not against the rules. Treat such a gain as valid for the engine we run,
but do not harden a constant around it without noting the dependency — if the untap is ever
materialised, the comparison has to be re-run. See `mana-creature-tap-order.md` §4.

## If it is ever built

The shape is to make the untap a real, searchable action rather than a payment-internal identity:

* materialise it (the Elf actually becomes untapped) so the body is available to combat and to the
  block step, and let the search decide between "re-tap for mana" and "keep it up";
* keep the current net-mana path as the fast case, since re-tapping for mana is the common use and
  collapsing it avoids a plan-space branch on every payment;
* the branch is only worth opening when the untapped body has a live alternative use — i.e. in 1v1,
  or on a turn where the extra attacker is lethal-relevant. A goldfish should keep the collapse.

Note this is the same tension the rest of the mana work keeps hitting: a creature's value is its
BODY, and the mana model only prices its mana. Compare `TapPowerOrderEnabled`'s note that per-permanent
body state "cannot live in ManaSourceRank -- that hook takes a CardDefinition and cannot see counters,
temp pump, or animation."

## Related

* `mana-creature-tap-order.md` — the tap-order measurement this constrains
* `mana-source-reservation.md` — the whole-turn reserve doctrine ("a land has no use but its mana, a
  creature does")
