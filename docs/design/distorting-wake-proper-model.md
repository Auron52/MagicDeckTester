# Distorting Wake: model it properly, prune it in the provider (queued 2026-10-30)

USER 2026-10-30, on Distorting Wake appearing in Hinata2's slowest keep rollouts: *"Is that just because it does
nothing or are we actually considering using it? The spell is intended just to use on your opponent's permanents so it
should probably be disabled with a heuristic."* Then: *"As usual it should be modeled properly, I just don't see a reason
to consider it in your plans."*

**Today.** In `cards.json`, Distorting Wake (`{X}{U}{U}{U}`, "Return X target nonland permanents to their owners' hands")
is `template: custom` with `goldfish_inert: true`, so it is never cast. It is a dead draw, not a modelled card. The search
does not consider it. In the keep generation's labels, `Distorting Wake (+3)` is the name of a hand-composition BUCKET:
Wake plus 3 other cards the generator treats as interchangeable. Its rollouts being slow comes from the rest of the hand
(a weak hand that plays to the turn limit while the Orchard Spirits pile up), not from Wake.

**Wanted.**
1. Implement the real effect: X targets, nonland permanents, any controller, back to the owners' hands. Read
   `.claude/skills/mtg-rules.md` first and review the result against it.
2. Prune it in the deck's `DecisionProvider` (a tested option RESTRICTION, the permitted kind of heuristic): in the
   passive-opponent goldfish, never offer a Wake cast. Bouncing the opponent's Spirits does nothing, and bouncing your
   own permanents never advances the win.
3. This should be play-identical to today. Verify: smoke rows byte-identical, the `hinata` digests unchanged. If it is
   not identical, it is a play change and gets measured as one.

**When.** Only after the Hinata2 artifact-queue pipeline lands. Doing it in the same commit window as the queued
Hinata2 -> Hinata rename (`rename-hinata2-to-hinata.md`) is fine.
