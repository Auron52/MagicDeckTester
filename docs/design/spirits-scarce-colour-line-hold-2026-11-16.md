# Spirits: preserve white for the cast after Souls flashback

The first Spirits 5,000-game mana gate reported 27 payment drops. In seed
30,000,089, turn four, the menu contains Island, Lingering Souls flashback,
and White Orchid Phantom. The flashback is `{1}{B}`; Phantom is `{W}{W}`.
Watery Grave plus Island pay Souls, leaving Hallowed Fountain plus Godless
Shrine to pay Phantom. The ordinary greedy instead uses Godless Shrine's
white mana for Souls' generic pip and strands Phantom.

The existing `ScarceColorHoldMask` tries the plan's scarce-colour providers
last, then retries without the hold if the current cast cannot pay. Its global
diagnostic lever clears this seed and all nine payment drops in the first
500 Spirits games. The global setting previously lost games in other decks,
so Spirits now opts into the same path through its decision provider; other
providers keep the previous default. `MTG_SPIRITS_SCARCE_COLOR_MASK=0`
restores Spirits' old payment behavior. This is a deterministic payment
choice, with no new searched action.

Before scoping, a fresh paired 5,000-game Release A/B (seeds
9,970,001–9,975,000) compared the global hold on against the default off for
Spirits alone. One play digest changed, zero win turns changed, and summed
work was 203,200,999 versus 203,200,545 units (+0.0002%). Per-game logs and
batch results are under `build/scratch/spirits_scarce_ab*`.

The provider-scoped build reproduces the same first-500 oracle classes as the
global hold. Seed 30,000,089 has zero issues with the scoped default and nine
payment drops with `MTG_SPIRITS_SCARCE_COLOR_MASK=0`. A focused unit case
offers Souls flashback followed by White Orchid; both rollout and executor
cast both spells with the default, and both drop Orchid with the lever off.
The full Release suite passes 3,365 cases and 3,614,556 assertions.

The pooled Spirits/2HG smoke and Spirits regression gate has no digest or win
changes in any of its ten cells. All 561 scenarios pass, strict viewer replay
has zero play drift and zero enum gaps across 864 references, and require-beat
passes. A fresh PGO mana-clean block is still needed to establish whether
the remaining Spirits flags have other signatures.
