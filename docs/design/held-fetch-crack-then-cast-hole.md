# Held battlefield fetch: "crack, then cast with the fetched land" is unfindable

**Status:** OPEN engine completeness hole, found 2026-11-13 (Spirits onboarding, integrator A). Deferred
to its own change; nothing here is implemented.

## The hole

A modern battlefield fetch (Arid Mesa / Wooded Foothills / Flooded Strand: `land_search_activation_cost`
{0}, 1 life, sacrifice, finds one land that enters UNTAPPED) that is ALREADY on the battlefield at the
start of a main phase cannot be cracked to pay for a cast in that same main, at any budget:

* the main-phase enumerator prices casts against the board BEFORE any activation, and a fetch makes no
  mana, so a cast that needs the fetched land's mana is never in a candidate plan (`MTG_AXIS_TRACE=1`
  shows `max_casts=0` for the repro below);
* the land-search activation is applied in the TRAILING pass (after every cast); the position twins
  (`Plan::land_search_act_slot`, TurnSolver's AppendSubdecisionAxes step (0)) exist only for landfall
  watchers, land-counting ETBs and library-top readers, never for mana, and they are twins of plans whose
  casts were already enumerated without the crack's mana anyway.

Repro: `logs/spirits/repro/spirits_held_strand_crack_then_cast.json` (`mtg --scenario`): two Plains + a held
Flooded Strand, Drogskol Captain ({1}{W}{U}) in hand, the only Island in the library. Expected T4 (crack for
the Island, cast the Captain T3, attack T4); actual T5 at `--budget-ms 0 --depth 5`.

Modern RW Burn's `modern_rw_burn_held_fetch_landfall` passes only because Searing Blaze is a landfall
watcher (and Burn reaches the line through its landfall machinery); two Lava Spikes off a held Mesa also
pass in Burn (not traced further -- worth checking which mechanism admits them before fixing).

## What IS covered (2026-11-13)

A fetch PLAYED this turn: `FetchCrackOnPlayOffered` (DecisionProviders.cpp) offers the crack-on-play
variants (one per searched target, a pinned `fetch_target` that `PlayLandCore` collapses) BESIDE the hold,
so "play Strand, crack for an Island, cast Captain" is a plan
(`test/scenarios/spirits_strand_fetches_the_blue_source.json`).

## Why it matters

It biases the USER-owned fetch-hold measurement (analyze-deck Stage 1 `fetch_hold_review`): a held fetch's
mana is always a turn late in autonomous play, so "hold for the next draw's information, then crack and
spend" is never scored, and full-timing vs a hold policy is measured against a crippled hold. Spirits (4
Flooded Strand, hold kept by SpiritsProvider) is the first shipping deck that keeps the hold in autonomous
play; Burn / FiveColour / Anti-Lifegain decline it (except Burn at 3 life or less).

## Fix shapes (not chosen)

1. Enumerate casts against a post-crack copy for each held untapped-crack source (the `add_for_land`
   shape applied to an activation), emitted as crack-first (`land_search_act_slot = 0`) plans with the
   pick pinned (`land_search_pins`).
2. A breakpoint after a trailing land-search activation that put an untapped land: the continuation
   re-plans the rest of the main on the new board (the site-9 post-entry shape).
3. Treat a held untapped-crack fetch as an optimistic mana source in the cast bound (Treasure-like), with
   the apply cracking it first.

Shape 2 reuses the most existing machinery; any shape must stay lockstep (executor / rollout / claude-play)
and lossless.
