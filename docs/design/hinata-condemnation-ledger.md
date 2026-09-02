# DEFERRED: the hinata condemnation case-ledger (the "fair shake")

**Status: DEFERRED (USER 2026-09-02: "I would rather look at our other options first, because we
badly need something.").** The USER designed ordered condemnation, is not ready to give up on it
for hinata, but wants the other cost routes (the sound recipe) tried first. This doc is the
self-contained plan for when it is picked up.

## Why hinata condemns nothing today (recorded facts)

* Sound condemnation = **0 drops on hinata** (ranges built and inert -- see
  `exemption-free-condemnation-order.md`); absolute ceiling with soundness ignored = **-8.1%
  units**; the node's own dedup already removes **91%** of the same candidates. All three numbers
  are from the OLD machinery (wave-based, greedy fallback) -- none has been re-measured under the
  sound recipe (root-turn node + MTG_BP_CANON_CONT).
* Hinata's provider deliberately does not opt into `CondemnsConsideredAtBreakpoint` (rank ties --
  a tie cannot be a decline; and the order does not establish draws-before-deploys).
* The mana-adding-SITE exemption (bug 7) is SITE-keyed: when the breakpoint's spell added mana it
  blanket-exempts every candidate. Hinata's breakpoints are float-wrapped (Spasm/Irencrag), so the
  exemption plausibly fires almost everywhere. This is the prime suspect for the zero.

## The plan (a day of instrument-and-measure, no speculative machinery)

1. **Itemize the zero.** Instrument every breakpoint continuation candidate on hinata that
   condemnation COULD drop, recording WHY it survived (mana-site exemption / drawn-card exemption /
   rank tie / range overlap / payability guard), deduped by signature with hand + pool at first
   occurrence -- the same shape as the corrected MTG_CONT_DIFF case list (post-land state!).
   Deliverable: a ledger the USER reads case by case. (Precedent: the land-condemnation rule came
   from the USER reading concrete cases, not from aggregates.)
2. **Test the blanket exemption.** Per case: did the float actually change THIS candidate's
   viability (affordability flip or X-size change)? If mostly no, replace the site-keyed exemption
   with a POOL-DELTA-AWARE decline test ("condemn unless this card's affordability or X-size
   changed") -- tighter, still sound, simple in the sense the USER's drawn-card rule is simple.
3. **Re-derive the ceiling under the sound recipe.** The -8.1% and 91%-overlap figures predate the
   root-turn node + canon; measure the in-node residue directly (count node children a sound
   filter would have dropped, instrument-only).
4. **USER reads the ledger** and either distills a rule or accepts per-provider off with per-case
   evidence.

Context docs: `bp-node-partition.md` (scope ruling + sound recipe), `bp-greedy-continuation-deletion.md`
§9 (canon), `exemption-free-condemnation-order.md`, `cast-order-ideal-with-ranges.md`.
