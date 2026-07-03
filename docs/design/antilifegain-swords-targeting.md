# Anti-Lifegain: Swords-to-Plowshares targeting (shipped) + pump-then-Swords (deferred)

Self-contained record (2026-07-03). How the Anti-Lifegain deck uses Swords to Plowshares as a
life-loss tool, what shipped, and the larger combo left for later.

## The interaction

Swords to Plowshares exiles a creature and its **controller gains life equal to its power**
(`controller_lifegain_equals_power`). A Tainted Remedy / Plague Drone (`lifegain_to_loss`, i.e.
`RemedyActive`) replaces an *opponent's* lifegain with an equal life **loss**. So Swords on an
**opponent** creature, with an enabler in play, makes the opponent lose `power` life. Resolution
(`EffectHandler::ResolveRemoval`) and the search rollout (`ApplyPlanDirect` Removal branch) both model
this via `OpponentGainsLife(state, 1 - tgt_controller, tgt_power)`, so the search already simulates and
values the life-loss (and, without an enabler, correctly sees the opponent *gain* life).

## Shipped (2026-07-03): largest-target + enabler gate

Against a **passive goldfish opponent**, exiling a creature has no defensive value — the only payoff is
the life-loss. So:

- **Enabler gate:** the Swords cast is only offered / cast while a `lifegain_to_loss` enabler is in play
  (else it just hands the passive opponent life). Enforced in the enumeration guard (CollectActions),
  the executor (`CastSpellFromHand`), and the rollout — all via the one helper.
- **Largest target:** among the opponent's creatures, hit the **highest `EffectivePower()`** (max life
  loss). `EffectivePower()` includes temporary buffs, so a pumped creature is picked up automatically.

One shared helper keeps all three sites in lockstep: `FindLifegainRemovalTarget(state, active)` in
`SpellEffects.h` — returns the largest opponent creature's index, or **-1 = "do not cast"** (no enabler
in play, or no opponent creature). Only Swords carries `controller_lifegain_equals_power`, so every
other deck is byte-identical; verified: smoke/regression show all non-antilife decks unchanged, antilife
searched depth improves overall, d0 net strongly positive. Behaviourally confirmed in game logs: Swords
exiles the max-power opponent creature (e.g. the 6/6, or the power-2 among [1,1,2]) and drops opponent
life by that power.

### The enumeration gate: "enabler in play OR in hand" + a plan-validity check (updated 2026-07-03)

The gate that decides whether to *offer* the Swords cast originally checked `RemedyActive` (an enabler
on the **battlefield**) only. Loosening it to "in hand" *by itself* was tried first and **tanked the
greedy d0 path** (antilife d0 −33 wins): the deck holds ~8 enablers, so "enabler in hand" is true almost
every turn, and the myopic d0 greedy (which selects by heuristic eval, not rollout) then
considers/misfires Swords constantly. The fix that made "in hand" safe was to pair the loose gate with
the exact **plan-validity** check `SubsetHasUnbackedLifegainRemoval` — a subset may cast Swords only if
an enabler is in play or in that same subset. That rejects the myopic no-enabler line at the d0 greedy
too, so d0 stays healthy. See "FIXED: the same-turn combo" below for the full rationale and results.

### FIXED (2026-07-03): the same-turn "Tainted Remedy → Swords" combo

The in-play gate forgot the same-turn combo — on the turn the enabler lands it isn't on the
battlefield at enumeration (turn start), so Swords wasn't offered that turn and waited until the next.

**The clean fix (shipped): offer Swords when an enabler is in play OR in hand, and require it in the
plan.** The key realisation is that `EnumeratePlans` already SCORES every candidate plan by
`ApplyPlanDirect`, which casts enabler-first — so a `{Tainted Remedy, Swords}` plan resolves the combo
correctly (Swords sees the live enabler) and scores well. The only thing blocking it was the
enumeration gate. So:
- **Gate (CollectActions):** offer Swords when there is an opponent creature AND
  `RemedyActiveOrInHand` (an enabler on the battlefield or in hand). No affordability guess — the
  plan's own joint mana feasibility (`CanPay`, and `EnumeratePlansWithLand` which plays the land
  first) decides.
- **Plan validity (`SubsetHasUnbackedLifegainRemoval`, applied in `Solve::consider` AND
  `EnumeratePlans::eval_and_push`):** a subset that casts Swords is valid only if an enabler is in
  play OR the SAME subset casts one. This is EXACT (no approximation) — it rejects the myopic
  "Swords with no enabler" line at both the search and the d0 greedy, so relaxing the gate does NOT
  re-introduce the −33 d0 blow-up the old "enabler in hand" gate caused.

All three helpers are gated on `controller_lifegain_equals_power` / `lifegain_to_loss` (only Swords /
the enablers carry these), so every non-antilife deck is byte-identical.

**Result (verified + accepted into GT):** non-antilife byte-identical; **searched depth win->loss=0,
zero real slowdowns**, gi87 (d3+d5) T5->T4, gi97 T5->T4, gi266 T4->T3, smoke gi6 T5->T4; gi235
recovers to T6 **at d5** (its T6 line genuinely needs depth-5 lookahead — d3 cannot see it at any
budget, so d3 T7 is not a gate miss). The lone searched turn-later, gi206 (d3 4->5), is **classified
budget churn** — it recovers to T4 at 4×/16× the case's 10ms stress budget (the T4 line is still
found; the added combo-exploration breadth just needs ~5 more virtual-ms). d0 greedy (lighter bar):
net strongly positive (smoke +8 wins 842->850, regression +3) with a little greedy-myopia churn.

Repros (base = the config's seed; use the case budget, `--budget-ms 10` for d3 / `20` for d5):
`--seed 2089 --game-index 87 --depth 3` -> T4; `--seed 2237 --game-index 235 --depth 5 --budget-ms 20`
-> T6.

### GOLDFISHING ASSUMPTION (revisit for Phase 2)

Both rules above assume a **passive** opponent (goldfish): a real opponent's creature is worth exiling
on its own even without an enabler (it removes a blocker/attacker), and you might prefer removing a
specific threat over the largest. When a real opponent is added (Phase 2), gate these behind the
goldfish assumption / make them opponent-model-aware rather than unconditional.

## Shipped (2026-07-03): pump-then-Swords via Invigorate's FREE alt cost

Shipped as a **targeting redirect** in one shared helper, `TryPumpThenSwordsRedirect` (SpellEffects.h),
called from both Swords apply sites in lockstep: the rollout (`ApplyPlanDirect` Removal branch,
TurnSolver.cpp) and the executor (`EffectHandler::ResolveRemoval`). Just before Swords exiles the
opponent creature at `FindLifegainRemovalTarget`, it fires a full alt-cost Invigorate cast (alt
lifegain + on-cast triggers **incl. Aria of Flame verse** + prowess + the +N/+M) with the pump
**targeted at that creature** instead of an own attacker, then reads the now-boosted power for the
exile life-loss. Consuming the pump from hand also removes it from the later safe-alt auto-fire pass,
so it is never double-fired. Gated on `!DecisionUnpruned()` (the same condition as that pass): fires
for autonomous search + the engine's AI-hint rollout, suppressed for the play viewer / unpruned A/B
where the pump is a real enumerated decision.

**Verification (all decks byte-identical except antilife; the run was accepted into GT):**
- smoke + regression: non-antilife (knights, hinata) unchanged; antilife **searched depth
  win->loss=0, turn-later=0, 12 games earlier** (e.g. d3 s3003 gi36/gi112 4->3). Exactly the
  zero-searched-regression + strict improvement the model predicted.
- d0 greedy (lighter bar): net strongly positive (loss->win, 9+ earlier) with 2 tangled-line
  turn-later churn games (gi335, gi958) — benign greedy myopia (double Aria / Reverent Silence
  self-wipe lines), same category the shipped Swords change produced.
- Behavioural: seed 1023 gi22 wins **T4 (was T6)** — T3 Remedy, T4 Aria (opp 20->10), Swords cast
  fires the redirect (Invigorate alt -3, its Aria verse -2, pump the target 1/1 -> 5/5), exile the
  5/5 for -5 -> opp to -1. With no own creature the Invigorate was otherwise stuck uncastable, so
  the redirect turned a wasted 7 into the kill (the "can't afford an attacker" win).

## (Historical) deferred plan: pump-then-Swords via Invigorate's FREE alt cost (corrected model)

**IMPORTANT — the value comes from the alt cost, not a {2}{G} hard cast.** Invigorate reads: "If you
control a Forest, rather than pay {2}{G}, you may have an opponent gain 3 life. Target creature gets
+4/+4 until end of turn." So the alt cost is a *free* payment (opponent gains 3 → **loses 3** with our
enabler) that STILL grants the +4/+4. So the pump is FREE — no mana. (My earlier note here wrongly framed
it as a {2}{G} hard cast needing surplus mana; ignore that.)

The change is a **targeting redirect**: point Invigorate's alt-fire +4/+4 at the OPPONENT creature that
Swords will exile (the `FindLifegainRemovalTarget` creature), instead of an own attacker. Then Swords
exiles a +4-bigger creature → +4 more life loss, on top of the alt cost's 3. Total from the pair =
`power + 7` opponent life loss, for free.

**Why it's ~zero-regression and high-value (per the user):**
- Redirecting to the Swords target is `>=` pumping an own creature: equal when the own creature could
  actually swing this turn (both are +4 opponent life loss this turn — combat vs a bigger Swords), and
  **strictly better whenever the own creature can't convert the pump to damage this turn**:
  1. we have NO own creature — today Invigorate is stuck *uncastable* (no legal target), so its 3 (alt)
     + 4 (pump) = 7 is lost entirely (the biggest win, the "can't afford an attacker" case);
  2. the only own creature is a mana **dork tapped for mana** (Birds / Ignoble Hierarch used to cast) —
     it can't attack, so pumping it is wasted;
  3. the own creature is **summoning-sick** — can't swing either.
  In all three the redirect turns a wasted (or impossible) pump into +4 Swords damage.
- No mana, so no search-budget increase and no churn (unlike the same-turn-Remedy gate above). It rides
  the Swords decision the search already makes.

**Implementation sketch (do after compaction):**
- Deterministic, slaved to the Swords cast: when Swords is applied on opponent X (enabler in play, X =
  `FindLifegainRemovalTarget`), if an Invigorate is in hand and we're not under human-play, alt-fire the
  Invigorate targeting X FIRST (apply `X.temp_power_bonus += power_bonus` and the alt lifegain via
  `OpponentGainsLife(alt_lifegain)`), consume it, THEN read X's (now +4) power for the exile life loss.
- **Coordinate with the safe-alt auto-fire** so the same Invigorate isn't ALSO fired on an own attacker
  (double-fire). Touch points: `AntiLifegainProvider::CanAutoFireAltPayload` (DecisionProviders.cpp:306),
  the safe-alt-payload target selection + its pass ordering vs Swords, and the Swords apply sites in
  **lockstep**: rollout `ApplyPlanDirect` Removal branch (TurnSolver.cpp) + executor
  (`AIEngine::CastSpellFromHand` / the FireSafeAltPayloads mirror at AIEngine.cpp:1687).
- **Suppress under human-play** (`HumanPlayActive()`): the play viewer keeps full control of Invigorate's
  mode/target; the pump is an autonomous-AI heuristic only.
- Verify: a game with Swords + Invigorate + an opponent creature but NO own creature should fire the
  redirect (opp life drops by power+7 on the combo turn); full smoke+regression must show zero
  regressions (other decks byte-identical, d0 not worse).
