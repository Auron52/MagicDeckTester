# Shadowborn tutor continuation: Lotus before Lake trial

After the line-surplus payment fix, the ten focused Shadowborn seeds still
reported 13 `APPLY-DROP` events. Six of them, in seeds 30076192 and 30078248,
begin with a tutor and later try to cast Thrumming Stone. In seed 30076192,
Diabolic Intent pays with the Swamp and Lake of the Dead's ordinary tap. Both
lands are then tapped, so the Lotus Bloom alone cannot pay the Stone. The
oracle can spend Lotus on Intent, leaving Lake ready to crack for Stone.

A default-off `MTG_ONDEMAND_LUMP_BEFORE_PLAIN` trial tried Lotus before Lake's
ordinary tap when a black/generic card already in hand cost more than the
post-payment board could produce, and a Lotus payment left enough potential
mana including Lake's crack. It read printed card definitions because live
hand placeholders carry no reliable mana cost. The trial cleared six oracle
drops without increasing any other issue class; four extra `PHANTOM` reports
were quiet classifications of other payments.

The 1,000-game paired check (indices 85000–85999) had identical outcomes and
play digests, with +0.06% work units. In 5,000 fresh pairs (86000–90999),
both arms won 4,979 games. The trial improved game 87629 from turn six to
turn four, but worsened game 86704 from turn seven to turn eight. Its average
improved just 0.0002 turns per game (paired approximate 95% interval
[-0.00108, +0.00068]); work units rose 0.045%. The slower game's first
chosen-plan divergence occurs on turn three, before the later tutor payment.
Thus the rule changes search valuation beyond the exact payments it repairs.

The trial is **not shipped**; its source was reverted. Raw results and the
two per-game traces are under `build/scratch/gods_counter_mask_ab/shadow_lump_*`.
The remaining seven focused drops need narrower payment/continuation work.
