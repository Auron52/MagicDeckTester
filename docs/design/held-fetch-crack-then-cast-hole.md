# Held battlefield fetch: "crack, then cast with the fetched land" is unfindable

**Status:** FIXED 2026-11-13 (Spirits integrator E1, commit in the ledger `analysis-Spirits.md`,
"Integrator E1"): fix shape 1 below, `AppendHeldFetchCrackFirstPlans` (src/ai/TurnSolver.cpp), default ON,
`MTG_HELD_FETCH_CRACK_FIRST=0` restores the hole for an A/B. Found 2026-11-13 by integrator A.

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

## Fix shapes

1. Enumerate casts against a post-crack copy for each held untapped-crack source (the `add_for_land`
   shape applied to an activation), emitted as crack-first (`land_search_act_slot = 0`) plans with the
   pick pinned (`land_search_pins`).  **<- CHOSEN.**
2. A breakpoint after a trailing land-search activation that put an untapped land: the continuation
   re-plans the rest of the main on the new board (the site-9 post-entry shape).
3. Treat a held untapped-crack fetch as an optimistic mana source in the cast bound (Treasure-like), with
   the apply cracking it first.

## The fix (shape 1), and why not 2 or 3

`AppendHeldFetchCrackFirstPlans` runs wherever a TOP-LEVEL main-phase list is built from `EnumeratePlans`:
`add_for_land` (the land-drop path, each land option), the no-drop path of
`EnumeratePlansWithLandUncachedBody` (drop used / main 2 / the executor's post-draw re-solve -- the path a
held fetch meets most) and `EnumerateM2PlansBody`. For every held crackable source NAME (a land-search
activation whose land enters untapped: `CrackPutsUntappedLand`; copies of one name are one move) and EVERY
pick k of its `LandSearchPicks` list (the pin axis's own width -- every distinct legal target, no provider
prune, no cap), it applies the crack on a copy through the same body the apply uses (`ApplyLandSearchActs`
under a one-entry `ScriptedLandSearch`) and runs `EnumeratePlans` on the post-crack board. Each plan comes
back with the crack prepended, run in the PRE-CAST pass (`land_search_act_slot = 0`, or the pre-cast-subset
mask when the post-crack plan cracks a further source in the trailing pass) and the pick in its ordinal
(`land_search_pins`, trailing zeros dropped so the all-front plan stays a BASE for every other axis).
Recursive: two held fetches can both precede the casts (sources in name order, so a pair is one ordering).

* **Lockstep for free.** Every field it sets already exists and is applied by both worlds at the same point
  (ApplyPlanDirect's pre-cast pass / AIEngine::TakeTurn's twin, through the shared `ApplyLandSearchActs`)
  and is already folded into the plan hash, SamePlan, IsApplyEmptyPlan, the census and dedup keys. No new
  Plan field: `check_plan_sig_lockstep.py` shows only the pre-existing `#BB` difference.
* **Exact.** The casts are priced on exactly the board the apply hands them (land-first drop, then the
  pre-cast crack with the same pin, same RNG state). The crack template is taken from the actions the base
  enumeration emitted on that board, so the activation's legality (life, dominated-crack gates, any
  provider prune) is the base enumeration's.
* **Folds.** A post-crack plan with NO action is skipped (crack then nothing == the trailing-crack plan the
  base list already holds). A crack-first plan is an axis variant (`PlanIsAxisVariant`: slot != -1), so a
  duplicate applied board is skipped by the post-apply dedup without a rollout.
* **Not in human play** (`HumanPlayActive`): see "Viewer" below. **Not in a breakpoint continuation**
  (`g_bp_enum_depth != 0`): a continuation's activations keep the trailing order (ApplyPlanDirect arms the
  positions for the top-level plan's section only), so a crack-first continuation would crack after its casts.

Why not shape 2: a breakpoint continuation is searched only through the wave (bp_choice slots), site 9 is
first-occurrence-only and absent at depth 0, and the post-entry gate keys on ACTIVATIONS, not mana -- a new
mana trigger would also fire for every untapped land drop unless a per-permanent "entered by an activation"
stamp were added. Shape 3 would put a speculative source into ManaPayment's colour-group / credit machinery
(the land-put credit of land-search SPELLS shows how much that costs to keep exact). Shape 1 reuses only
fields both worlds already apply.

**The main-2 premise it repairs.** The rollout plays a main 2 only for second-main-relevant decks: "in a
goldfish combat creates no new resources, so everything was castable in the first main"
(SimulateToEnd). A held fetch cracked in main 1's TRAILING pass broke that premise -- its land was new mana
nothing in main 1 could spend -- so before the fix even "crack in main 1, cast the Captain in main 2" was
not played (`resume_at: main2` with Plains x3 + Island untapped and the Captain in hand: no main-2 cast, the Captain
waited for turn 4's main 1 and the win came T5 -- the engine does not play main 2 for Spirits, by that premise).
With crack-first plans, everything castable after the crack is castable in main 1 again.

## Proof (written first, read the slower win at HEAD)

| fixture (all `--budget-ms 0`) | `MTG_HELD_FETCH_CRACK_FIRST=0` | fixed |
|---|---|---|
| `test/scenarios/spirits_held_strand_crack_then_cast.json` (no-drop path, `resume_at` main1) | T5 FAIL | T4 PASS |
| `test/scenarios/spirits_held_strand_crack_then_cast_with_drop.json` (land-drop path) | T5 FAIL | T4 PASS |
| `test/scenarios/spirits_two_held_strands_crack_then_cast.json` (both cracks first) | T5 FAIL | T4 PASS |
| `logs/spirits/repro/...` at `--depth 5 --budget-ms 0` | T5 | T4 |

Unit: `test/unit/test_held_fetch_crack_first.cpp` (enumerate + `ApplyPlan`: one and two held Strands; no
action-less crack-first plan).

Burn's "two Lava Spikes off a held Mesa" (the open note above) passes on both arms: one Spike in main 1, the
crack trailing, the second Spike in main 2 -- Burn is a second-main deck, so its main 2 is played.

## Which decks can move

Modern fetches (activation cost, untapped find): Flooded Strand (Spirits), Arid Mesa / Wooded Foothills (Modern
RW Burn, Anti-Lifegain, FiveColour). The legacy shape (Windswept Heath, Scalding Tarn, ...) resolves on play
and is never held; Evolving Wilds / Myriad Landscape / Khalni find TAPPED lands (excluded). Spirits keeps the
hold (SpiritsProvider). Burn / Anti-Lifegain / FiveColour decline it, but `FetchResolvesOnPlay` is false at
3 life or less (`ModernFetchCollapsible`), so a fetch they play there is HELD and those states can move.

## What is left (OPEN, deliberately)

* **A breakpoint continuation** cannot crack a held fetch to cast what it drew (see the gate above). A
  draw/dig continuation needing a held fetch's mana waits for the next main. The executor's post-draw
  RE-SOLVE is top-level and covered.
* **A non-front crack-first pick does not cross with LATER in-plan land-search ordinals** of the same plan
  (a cast land search, White Orchid Phantom's own-land basic): the k != 0 plan carries a pin and so is not a
  base for the pin axis -- the additive-axes rule every pin variant follows. The all-front (k = 0)
  crack-first plan is a base and gets the full product. A second held fetch is NOT affected (the
  recursion puts both cracks first, each pick searched).

## Viewer

The human can already express it, verified on the live protocol (`--claude-play`, Spirits seed 3, choices
`1,2,-1,-1,1,2`): T1 play the Strand and hold it; T2 commit "land=Plains; Flooded Strand: sacrifice it ...",
answer the `tutor_etb` prompt with the Island, and the SAME pre-combat main re-prompts (the always-prompt
segment loop, `PlaySegmentAlwaysEnabled`) with Plains + Island untapped and Metallic Mimic / Supreme Phantom
offered. The play-and-crack line is reachable the same way (play the Strand held, commit the crack, then the
cast). So the viewer menu is left exactly as it was: no crack-first entry is added under human play, and no
saved reference's menu index can move.
