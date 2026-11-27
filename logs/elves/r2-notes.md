# Elves round 2 (wip/elves-r2) -- notes

Base: origin/phase-1-2-deck-analyzer f2e307fe3 (the landed Elves play batch).

## USER rulings this round (verbatim, 2026-11-27)

- Q1 (hold-back): "Hold all dorks back is okay, but not sufficient in my mind unless we have heuristically
  determined the right option in our situation. i.e. If just searching we should probably consider all, hold back
  normal dorks and hold back all dorks. If we add some heuristic logic to figure out the win turn we could
  potentially prune that further."
- Q1 follow-up: "We could search the options in-between, but that seems quite costly." then "Okay, fair enough. If we
  don't need a prune then we don't need a prune. Though we should check how much this particular branch costs and
  make a decision on whether we perhaps do want a prune. My thought was a bit rough, so if we actually need to prune
  this then perhaps we should take the "heuristic calculation" approach to set things up for lethal as soon as
  possible instead."
- Tap order: "This is also a tricky problem, because technically on earlier turns when we are setting up, you
  usually want to use the scaling elf mana last, to get more on board before you tap it. Then in late turns you want
  to work the other way around to leave multiple attackers."
- "That said, when using a scaling spell, (Joraga, Galadhrim, Genesis Wave) you usually would want to prioritize
  attackers or just pump all of the mana into the spell."
- "I see. So yeah, we should take control of the tap order a bit in the heuristic provider to make sure that we
  maintain whatever thing we want to maintain, whether that is the attackers or the scaling mana."
- "Usually on an earlier turn (like turns 2-3) you would focus on building your mana and keep the scaling back.
  After that you usually want to attack. But ideally how we handle this would depend more on our board than the
  turn."
- Q4 (discard): "I don't understand the Taunting Elf comment. Genesis Wave is technically MV 3, but realistically
  needs to be cast for 6 mana or preferably even more to be good. It shouldn't be counted as a cheap spell in the
  discard heuristic. Mana Elves should indeed be kept above spare lands. We ideally do want to play a land next turn,
  though, so keeping one is nice if we can manage it."
- Q6 (kick cap): earlier "I think Kick counts should be capped at lethal size of 20"; now "Yeah, we may want to scale
  it up for 2HG arguably."

## What was built (all temporary selectors are heurarm slots, per job in a manifest)

1. KICK CAP = the opponent's STARTING life (`MultikickCountCap()`, TurnSolver.cpp; `gamesetup::StartingLife()`):
   20 standard, 30 2HG. `kMaxMultikick` (the DB's face synthesis bound) 20 -> 40 (the largest starting life any
   format uses: 20 / 30 / 40). Only Elves holds multikicker / squad cards (Joraga Warcaller, Galadhrim Brigade).
2. DISCARD, Genesis Wave priced at X = 3 (mana value 6) in the threat order's distance-to-castable
   (`ElvesDiscardCost`, keyed on `reveal_x_put_permanents`). It is the only cost read in the Elves discard
   heuristic (`ElvesThreatValue` reads params only; `CleanupDiscardRankingWithOrder` keeps the provider's order).
3. DISCARD, the LAND FLOOR at every land count (was < 4 lands on the battlefield): one land in hand is the last mana
   card shed; mana Elves stay above SPARE lands. Comment block reworded (the Taunting-Elf reading in plain words).
   Tasks 1-3 sit behind the TEMPORARY `MTG_ELVES_R2` (default ON; =0 = origin) for the A/B; delete after it.
4. HOLD-BACK MODES: `MTG_ELVES_HOLDBACK_MODE=band|three|two` (heurarm twins `MTG_ELVES_HOLDBACK_THREE`,
   `MTG_ELVES_HOLDBACK_TWO`). three = {top, top - h_normal, top - h_all}; two = {top, top - h_normal} (the USER's
   "prioritize attackers or just pump all of the mana into the spell"). A target <= 0 admits no scaled count (the
   no-sink plan is not a row); a target with no exact member admits the largest member below it. The scaling census
   gains `inbetween` (rows in the band that are none of the three: the price of the in-between branch) in every mode.
5. TAP ORDER (`MTG_ELVES_TAP_ORDER`, TEMPORARY, default OFF): `ElvesProvider::ManaSourceRank`. On a pre-combat
   payment of a plan that casts a scaled sink (new `PlanTraits::casts_scaled_sink`), PRESERVE ATTACKERS: dorks that
   cannot attack tap first (64), then scaled dorks (65), the attack-capable fixed dorks last (66). Every other
   payment keeps the inherited scaling-preserving order. Keyed on what the plan does, never the turn number.
   Finding (Explore agent, ManaPayment / TurnSolver): the line-10091 rank 61 never applies to Priest / Archdruid;
   GenericProvider::ManaSourceRank maps them to 66 (tap last) when the plan "casts scaler food" -- and Joraga
   Warcaller and Galadhrim Brigade ARE Elves, so every Warcaller / Brigade sink plan taps the Llanowars first -- 64
   (first) on an attack turn with no Elf cast, else 65. Disclosed gaps: the whole-turn prepay's hold/release ladder
   (BatchPrepayMainCasts) is not driven by this rank; ExecutorPayerOrder (the lethal projection's payer model) does
   not set casts_scaled_sink; the Ezuri main-phase pump is not a sink for the tap order.
6. `MTG_ELVES_CHOSEN_CENSUS=1` (diagnostic only, AIEngine plan-choice point): per real main phase, the chosen sink
   count's category (top / normal / all / between / below / nosink) and the board facts for the tap-order frequency.

## Status

(see below; updated as runs land)
