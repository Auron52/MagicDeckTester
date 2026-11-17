# Dragonstorm visible-closer lump: count only a creature that adds a pip (2026-11-17)

Lever: `MTG_ONDEMAND_VISIBLE_CONTRIB` (heurarm slot `ONDEMAND_VISIBLE_CONTRIB`), DEFAULT ON; `=0` restores the
sole-hand-creature read byte-for-byte.

## The defect

Dragonstorm mana-clean gate `codex-dragonstorm-ruby-song-20261117`, seed `30073082` T5: APPLY-DROP x10 across
four plan variants `{Unclaimed Territory, Pyretic Ritual, Seething Song, Apex of Power[, Scourge of Valkas]}`.
Karrthus, Tyrant of Jund sits in hand and is cast in Apex's continuation.

The board at Apex has:
- Ruby Medallion;
- Mercadian Bazaar with one storage counter;
- Lotus Bloom;
- the new Unclaimed Territory;
- seven `{R}` floating off the rituals.

Apex costs `{6}{R}{R}{R}`. The ordinary payment taps the Territory for `{C}` and the Bazaar for `{R}`. Apex's
continuation then has Bloom for only one of Karrthus's `{B}` and `{G}` (Bloom makes three of ONE colour), and
nothing for the other, so Karrthus is dropped. The oracle cracks Bloom for `{B}` on Apex, floats one `{B}`, and
keeps the Territory's creature-only `{G}` for Karrthus.

The on-demand payer already has a Dragonstorm rule for exactly this: a visible later creature's off-colour pips
can justify cracking the lump early (`MTG_ONDEMAND_VISIBLE_LUMP`, s30060428 / s30074224). It missed in two ways:

1. **"Sole creature" counted every hand creature.** Across an unseen Apex breakpoint, two visible closers are
   competing continuations, so the rule stands down rather than pick one's colour. Scourge of Valkas beside
   Karrthus made two. But Scourge's pips are all `{R}`, a colour this cost already makes, so it competes for
   nothing.
2. **It required a rest owed after this cost.** The Apex-last variants owe nothing after Apex, so the rule never
   ran.

## The change

In the on-demand payer's visible-lump block (`ManaPayment.cpp`), with the lever on:
- A hand creature counts toward "sole" only if it ADDS a pip: a colour this cost does not make, beyond what the
  line's rest already owes. Two creatures that add pips still stand the rule down, as before (the test "two
  visible closers do not select an unseen Apex continuation's Bloom colour" is unchanged).
- When the line opens an unseen breakpoint, the rule also runs on a zero rest. That is allowed only when the
  hold IS this cost, colour for colour, so a payment outside the line is never read as its last cast.

The trial itself is unchanged. It still requires that:
- the crack's floated colour covers the later pip;
- another later colour gains supply against the ordinary payment.

## Measurement

Exact seed: APPLY-DROP 10 -> 0 (lever off: 10). The one new HIDDEN-INFO flag is informational: a payment that
needs a card off the top of the library.

The rule is gated on the Dragonstorm provider, so no other deck can move. Paired A/B, one pooled Release batch,
both arms per job via the heurarm flag, Dragonstorm games 540,000–549,999 (10,000 pairs;
`build/scratch/claude_visible_contrib_ab/`):

| deck | earlier | later | digest-only | work units |
|---|---|---|---|---|
| Dragonstorm (10,000) | 0 | 0 | 0 | +0.00% (identical) |

Every one of the 10,000 games is byte-identical, work units included: the shape is rare in play and never decided
a game in this sample. It is adopted as a payment correctness fix (the oracle's line is legal and keeps the
closer), not for a measured turn gain. Unit suite 3,394/3,394.

Tests: `test_dragonstorm_followups.cpp`, "a red-only creature beside Karrthus does not hide Karrthus's Bloom
colour". It covers the lever-on outcome for both plan shapes. On that reduced direct-payment board, the old code
reaches the same crack by another route, so the lever-off arm is not asserted; the exact-seed replay is the
lever's evidence.
