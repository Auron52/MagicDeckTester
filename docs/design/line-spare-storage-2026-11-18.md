# Line float rung, storage tier (`MTG_LINE_SPARE_STORAGE`), 2026-11-18

## The USER ruling

Question asked: should Dragonstorm spend Mercadian Bazaar's banked (storage) counters when a named line is
short a colour? USER (2026-11-18): *"For Dragonstorm it should spend the counters if they are required for
the line."*

## The defect

Dragonstorm seed 30086338, game index 86338, T6 main 1. Board: Mercadian Bazaar x2 (4 and 3 storage
counters), Mountain x2, Lotus Bloom, Sandstone Needle (2 depletion counters). Hand: Pyretic Ritual x2,
Desperate Ritual, Apex of Power, Dragonlord Kolaghan, Karrthus. The mana oracle flagged 14 APPLY-DROPs on
lines such as `{Pyretic, Pyretic, Desperate, Apex of Power, Dragonlord Kolaghan}` (Kolaghan dropped).

Root cause, payment by payment (MTG_TAPDBG trace): Pyretic 1's `{1}` cracks Lotus Bloom for `{B}{B}{B}`, so
the turn reserve holds `{B}{B}` + `{R}{R}{R}`. Pyretic 2's `{1}` takes the surplus `{B}`. At Apex
(`{7}{R}{R}{R}`) the reserve is `{B}` + seven `{R}`; the reserve drain (`SpendFloatingTowardCost`, which runs
before any tap) pays the three `{R}` pips and then the generic with all of `{B}` + four `{R}`, and the
Needle pays the last two. Kolaghan's `{B}` is gone, and nothing left on the board makes black.

The rung that exists to stop this, `MTG_LINE_FLOAT_TAP_FIRST` (`LineFloatTapFirstPlan`, ManaPayment.cpp),
holds generic pips off the reserve and pays them from SPARE sources. Storage and depletion lands are never
spare there (banked mana), so on this board it found no spare and stood down. The oracle pays the generic
with a Bazaar's stored red instead and keeps the `{B}`.

## The change

A STORAGE TIER at the end of `LineFloatTapFirstPlan`, behind `MTG_LINE_SPARE_STORAGE` (heurarm
`LINE_SPARE_STORAGE`, reader `LineSpareStorageOn()` in `src/ai/EngineFlags.h`; default ON per the ruling,
`=0` restores the storage-never-spare rung). "Required for the line" is read narrowly. The tier fires only
when some colour the NAMED line still owes:

1. floats in the reserve after this payment's own pips;
2. has no other untapped provider (no storage land makes it, and no other land, rock, ready dork or pay-sac
   source can make it);
3. is spent by the payer's own drain (replayed with `GeneralFloatReplay`, as the rung's hold-exact step
   does) at the hold the plain spares give.

Then the hold grows to the fewest generic pips that keep those colours. Storage lands are released from the
hold only for the units the plain spares cannot pay: the smallest single land that covers them, otherwise
the largest first. If all of them together cannot cover the units, nothing changes. A storage tap bursts all
its counters and the surplus floats, so the tier never costs the line a unit; its only price is the banked
counters. That is why it is gated on need. Demand the line does not name, such as a card beyond a breakpoint
opener, is never read (`later` is the named line only).

This is the shared payer (`TapForCostSharedImpl`), so the executor, the rollout and every payability walk
that pays through it move together. It applies at every budget. The rung is autonomous-only, as it was
before.

**Byte-identical elsewhere by construction.** The tier requires a charged, untapped `storage_land` on the
payer's battlefield. Only Dragonstorm's list holds one (Mercadian Bazaar; Dwarven Hold is the other card
with `storage_land`, and no deck runs it). The control below confirms this.

**How many counters.** The number of held pips is the payer's drain, not a choice made here. A line that
opens a breakpoint (Apex of Power) drains generic in WUBRG order (`MTG_UNSEEN_KEEP_SCARCE` is off), so the
`{B}` goes first and all seven of Apex's generic are held: both Bazaars pay (4 + 3). On a named-only line the
drain takes the red surplus first, three pips are held, and the 3-counter Bazaar pays alone. If
`MTG_UNSEEN_KEEP_SCARCE` is adopted, the replayed drain shrinks the hold on opener lines with no change here.

## Verification

- Unit: `test/unit/test_dragonstorm_followups.cpp`, "Line float, storage tier": on the repro board with the
  lever on, the `{B}` is kept, the Needle stays up and Kolaghan pays after Apex's ten. With the lever off
  Kolaghan is unpayable. With a line that owes no black, or with a Swamp up, the Bazaars stay untapped and
  both arms tap the same lands. Full suite: 3467 / 3467 cases pass.
- Repro s30086338 T6 (oracle, lever on): APPLY-DROP 14 -> 0. The game still wins on T7: the search's T6
  choice did not change. The fixed lines now show as DOMINATED-LAND (14 more of them at T6): the payer
  leaves two Mountains less untapped than the oracle's best, because it taps the Mountains for the early
  rituals and then both Bazaars, with the same casts. That is a smaller class than a dropped cast and is
  left as it is.
- s30083522 T6 (Karrthus past the second Apex, not named): no storage land is on that board, so the tier
  cannot fire. The oracle output is identical with the lever on and off: APPLY-DROP 12, still flagged. Per
  the ruling the fix does not reach unnamed closers.

## Held-out A/B

One pooled `mtg --batch` (build/scratch/claude_storage/manifest_ab.json), profile attached, lever per job.
Dragonstorm game indices 590000-594999 (seed = 30000000 + gi), on vs off, paired:

| deck | games/arm | earlier | later | net | mean turn on / off | digest-only |
|---|---|---|---|---|---|---|
| Dragonstorm | 5000 | 0 | 0 | 0 | 4.3330 / 4.3330 | 2 (gi 593056, 594534) |
| Treasure Hunt (control) | 200 | 0 | 0 | 0 | 4.0850 / 4.0850 | 0 |
| Angels (control) | 200 | 0 | 0 | 0 | 4.6850 / 4.6850 | 0 |

Adoption bar: there are no slower games, so nothing needs a d8 b0 recovery, and the net is 0. The bar is met.
The tier is rare in real play (2 of 5000 games move, digest only). Its value is correctness on the lines it
reaches, which is what the USER asked for.
