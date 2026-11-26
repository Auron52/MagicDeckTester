# Dragons mana-clean: the Haven ORACLE-GAP family (2026-11-26)

Gate `claude-queue-20261125a` (src `773d4e440d5c`), Dragons gi 96000-195999 (100,000 games): 4,874 issues, of
which 3,747 ORACLE-GAP and 1,127 APPLY-DROP. This note records what the ORACLE-GAPs were, what was fixed (oracle
side only, play byte-identical), and the engine-side items left open.

## Mechanism

ORACLE-GAP = the oracle's complete realisation of a plan does not reproduce the engine's own outcome (I1).
Split of the 3,747 by family:

| family | flags | mechanism |
|---|---|---|
| gy-return plans (`gy-return:Haven of the Spirit Dragon`) | 3,433 | (1) + (2) below |
| a Mind Stone tap difference | 238 | (3) below |
| other (Lathliss continuations holding the Haven) | 76 | (2): the continuation's rebuy dropped silently |

1. **The payment table paid Haven's `{2}` with the Haven.** Haven's rebuy is `{2}, {T}, Sacrifice`. Both apply
   worlds pay the `{2}` under a hold on the Haven (`SelfTapSourceHoldBit`, MTG_RESTRICTED_MANA_RANK), so the
   engine pays with other sources or not at all. The oracle's table (`BuildSources`) offered the Haven as a source,
   so its outcome was "Haven tapped for {C}, rebuy a no-op" (e.g. T8 `{no land + gy-return}` on Haven + Mountain:
   one oracle outcome, `Haven:C Mountain:R`, against an engine that left both up). That covers neither the engine's
   line nor any legal one.
2. **An unactivated rebuy was never reported.** `ApplyGraveyardReturnAbility` no-ops when the `{2}` cannot be paid,
   the Haven is already tapped or gone, or no target is left, and nothing named that to the act-drop sink. So the
   engine's realisation of an unpayable rebuy read as "every action done" (the same defect as the Lavamancer ping
   before `NoteGraveyardDamageDrop`).
3. **A mana rock's age was identity.** `{Dragon Tempest + Mind Stone}` beside a Mind Stone in play: the plan's
   trailing Mind Stone dig taps the first untapped stone as its `{T}`, cannot pay its `{1}`, and rolls the tap
   back. "Old stone paid, new one digs" and "new stone paid, old one digs" reach one frontier board, so the prune
   keeps one; the end boards then differ only in whether the new or the old stone is left up. `AgeIsInert` already
   folded that for a plain land; a rock has no summoning sickness either.

## Fix (commit 8b1dd8706, branch `wip/dragons-ogap`)

* `SelfTapSrcScope` / `t_pay_self_tap_src` (ManaPayment.h): both gy-return apply sites name the held source next to
  the hold bit (play never reads it). The audit hook records it at the payment frontier (`Hook::f_self_tap_src`),
  `FrontKey` keys it, and `BuildSources` leaves it out of that frontier's table.
* `ApplyGraveyardReturnAbility` returns whether it activated; both apply worlds call `NoteGraveyardReturnDrop`
  (TurnSolver.cpp, twin of `NoteGraveyardDamageDrop`, armed only by the oracle / plan dump / MTG_ACT_DROP_AUDIT).
* `AgeIsInert` covers a `mana_rock` that cannot animate.

## Measurement (same 100,000 games, base = origin `9e566000b`, Release, oracle level 2)

| class | base | fix |
|---|---|---|
| ORACLE-GAP | 3,747 | **0** |
| APPLY-DROP | 1,127 | 1,136 (+10 newly visible, -1) |
| CONTINUATION-DIFFERS | 468 | 187 |
| PHANTOM (disclosed) | 99,380 | 103,802 |
| DROP-COVERED | 3,858 | 3,970 |
| oracle applies | 386,903,370 | 386,919,744 (+0.004%) |

Play with the oracle off: byte-identical on Dragons std 20,000 + 2HG 10,000, Kitty Equipment (Cid) and Landfall
(Haunted Fengraf) std 5,000 + 2HG 2,500 each; the oracle-on digests are identical too.

## Open engine-side items (deferred; play changes, each needs its own A/B)

* **Unpayable gy-return plans are enumerated (PHANTOM +4,422).** The plan enumeration offers a rebuy whose `{2}`
  only the Haven itself could pay (e.g. Haven + one Mountain). The apply drops it, so the plan equals the same plan
  without the rebuy: a dead branch. A sound collapse candidate (default ON under the COLLAPSE rule), but it moves
  the search's menu, so it is a play change.
* **10 newly visible APPLY-DROPs: a breakpoint continuation's rebuy unpaid** (s30128278 T5, s30137584 T6,
  `{... Lathliss, Dragon Queen ...}`): the continuation's casts spend what the rebuy needed while an order pays both.
  These hid as ORACLE-GAP before (2).
* **APPLY-DROP x262, T3 m1 `{land=Gruul Turf}` dropped `[cast Lightning Bolt]`** (repro s30096713 gi96713): the
  Karoo float leaves `{R}` floating, the Mind Stone dig's generic `{1}` is paid from that `{R}` instead of Haven's
  `{C}`, and the Bolt the continuation then picks has no red. The Bolt is not declared in the plan (it is the
  continuation's), so the line-unpaid bill cannot see it at the dig's payment.
