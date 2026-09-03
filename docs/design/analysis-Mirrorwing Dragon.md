# Analysis ledger — Mirrorwing Dragon (decks/Mirrorwing Dragon/)

In-flight state for the analyze-deck workflow (skill: `.claude/skills/analyze-deck.md`).
Updated continuously; this is the durable memory for resume/handoff.

## Deck (60)

4 Mirrorwing Dragon, 4 Zada Hedron Grinder, 4 Goblin Instigator, 4 Ignoble Hierarch,
4 Elvish Mystic, 4 Fists of Flame, 4 Gold Rush, 4 Ancestral Anger, 3 Twinflame,
2 Expedite, 2 Scale the Heights, lands: 9 Forest, 4 Mountain, 3 Sandstone Needle,
2 Gruul Turf, 2 Rootbound Crag, 1 Kazandu Refuge.

**Archetype**: Zada/Mirrorwing "spell-copy swarm". A single-target instant/sorcery cast
targeting ONLY Zada (or only Mirrorwing) is copied for each other creature you control;
copies of cantrip pump spells mass-draw + mass-pump the board. Twinflame targeting only
Zada duplicates the whole board with hasted tokens for the turn. Goldfish kill = wide
pumped attack.

## Stage 1 — coverage (2026-08-10) — DONE

13 missing + Ignoble Hierarch partial (exalted bracket note absent; Keyword::Exalted +
CountExalted already exist in engine — entry fix only).

## Stage 2 — implementation plan (agreed design) — IN PROGRESS

New engine machinery (Tier 3), executor/rollout lockstep via shared SpellEffects helpers:

- **Solo-target trick spells** (`solo_target_trick` param): single-own-creature-target
  instant/sorcery. Target is a SEARCH decision: CollectActions emits one CastFromHand
  variant per own creature (reusing the threaded `enchant_target` int = target
  card.m_number, comment widened; aura precedent). `trick_up_to_one` adds a no-target
  variant (Gold Rush / Scale the Heights "up to one target").
- **Copy magnet** (`copies_solo_targeted_spells` on Zada/Mirrorwing): when the trick's
  only target IS the magnet, resolve one copy per other own creature, then the original
  on the magnet. Copy resolution order (player-chosen per CR; deterministic here):
  non-attack-eligible creatures first, then eligible, original last — optimal for
  goldfish since escalating payloads (Fists draw count, Gold Rush treasures) grow with
  resolution position. To disclose in 6a.
- **Payload params** (recomputed per copy — escalation is faithful):
  `cast_draw` (existing), `power_bonus`/`tough_bonus` (existing),
  `pump_per_cards_drawn_power` (Fists: after its own draw),
  `gy_self_power_bonus` (Ancestral Anger), `pump_per_treasure_power/_tough` +
  `creates_treasures` (Gold Rush), `grants_temp_haste` (Expedite),
  `counters_on_target` / `cast_lifegain` / `grants_extra_land_drop` (Scale the Heights),
  `token_copy_of_target` (Twinflame: haste token copy, exiled at beginning of end step),
  `strive_cost` (Twinflame; K-extra-target variants reuse `soulfire_own_targets` count,
  extras picked at resolution by printed-power rank — provider-ownable, to disclose).
- **New state**: `Permanent::temp_haste` (Expedite; read in CanAttackFull/CanTapNow,
  reset both cleanups), `Permanent::exile_at_end` (Twinflame tokens; swept at both
  end-step sites), `Player::cards_drawn_this_turn` (Fists; incremented at real draw
  sites, reset at both untap sites, folded into sim keys — fingerprint rule: ADD a
  field, never change the hash).
- **Treasures**: reuse existing "Treasure Token" cards.json def + sac_for_mana
  machinery (Jared precedent); shared CreateTreasureTokens helper.
- **Lands**: Gruul Turf = existing karoo params. Kazandu Refuge = enters_tapped +
  new `etb_lifegain`. Rootbound Crag = new `checkland_subtypes` branch in
  LandWouldEnterTapped (fastland precedent — pricing + play share the predicate).
- **Ignoble Hierarch**: mana_dork produces [B,R,G] + Exalted keyword (engine wired).
- **Second main**: none of these create combat resources — first-main only.

## Stage 2 progress

- [x] Scryfall data fetched for all 14 cards (2026-08-10, this session)
- [x] Engine machinery (params, state, helpers, both worlds) — built clean; regression
      SMOKE suite fully green afterwards (40/40 byte-identical) so existing decks are
      untouched. Keyword enum gained inert Strive/Treasure tags (Suspend precedent).
- [x] cards.json entries (13 new + Ignoble bracket-note fix; Gold Rush keywords
      emptied to match the Scryfall snapshot printing)
- [x] Viewer wiring (2c-ter): trick target rides the existing enchant_target sub-choice
      (battlefield + hand names resolve in CheckLine); strive count added as a "strive"
      SubChoice (emitted for K=0 too, renderChooseDialog precedent); human-play plan
      signature already splits both axes (enchant_target / soulfire_own_targets reuse).
      audit_viewer_decisions.py: MANIFEST rows (solo_target_trick / strive_cost /
      copies_solo_targeted_spells -> main_phase) + INERT_PARAMS rows for the payload
      params.
- [x] 2d review vs oracle text (during entry writing; deferrals below)
- [x] 2d-bis: audit_card_costs.py PASS ("All mana costs match Scryfall").
      audit_card_fields.py: all HARD fields match; snapshot refresh keeps 429-ing on
      ~50 cards (rate-limit transients, per the skill not failures) — re-verify later.
- [x] Build clean.

## Perf work (2026-08-11) — the trick-variant enumeration blowup

Symptom: 10-100x slow games (13.6 s/game d3 b200; the first Stage-4 analyzer run was
OOM-killed at "card scores 1000g d5"). MTG_BRANCH_STATS: per-target trick variant
groups were the top branching driver (Scale the Heights avg odo 1201, max 18000,
sum_odo ~3.6M/game). Three fixes, in order:

1. LOSSLESS target equivalence fold (CollectActions): two battlefield targets with
   identical (name, attack-eligibility, temp bonuses, counters, temp-haste) are
   interchangeable for every payload — one representative emitted. Invariant-clean
   (folds only outcome-identical branches).
2. Strive-K feasibility ceiling: don't emit strive variants whose ManaValue exceeds
   the board's conceivable mana (+2 margin).
3. MirrorwingProvider::TrickTargetCandidates — the 5f PERF PRUNE (provider-owned,
   MTG_UNPRUNED / MTG_UNPRUNE=tricktarget opens it): candidates = all magnets
   (battlefield + hand), best ready non-magnet attacker, plus (haste/copy payloads
   only) best sick creature and biggest hand creature. New UnprunedGate::TrickTarget.
   NEEDS the 5f pruned-vs-unpruned per-game A/B before adoption is final.
   Also: SelectDecisionProvider gained the mirrorwing signature (BEFORE goblin —
   Goblin Instigator's etb_self_creates_tokens would otherwise misroute the deck to
   GoblinsProvider).
4. SubsetHasMissingTrickTarget rewritten from per-subset zone scans (profiled 3.7%)
   to a name compare via Action::trick_hand_target stamped at emission.

After: ~10.7 s/game d3 b200 single-thread, d5 b400 ~7.8 s/game; sum_odo ~1M/game and
the probe game IMPROVED T5->T4 (budget dilution relief). Solve-in-rollout (the mass-
draw re-solve chains) remains the dominant cost — inherent to the archetype (TH-flood
class); revisit only if suite budgets demand it.

## Memory investigation (2026-08-11) — analyzer OOM/SEGV chain

Three analyzer runs died (-9 OOM at 33-44 GB; one -11). Root-caused:

- The footprint is NOT a leak: single games are ~10 MB RSS flat; per-DECISION live
  memory spikes (gdb at 19-27 GB: threads 10-14 frames deep in
  FSLineWin/FSLineTail -> SimulateToEnd -> SolveWithLookahead chains, each level
  holding its EnumeratePlansWithLand plan vector + rollout GameState copies). Wide
  plans x deep recursion x 24 workers stacks past RAM on this deck's flooded
  mass-draw nodes. MALLOC_ARENA_MAX=2 did not help (live data, not arena retention).
- The -11 (SEGV) is attributed to a stack-extension page fault under the extreme
  memory/swap pressure of the co-resident 33 GB run: a 144-game d5 seed sweep + a
  ~4 CPU-hour direct analyzer run on a quiet machine reproduced NO crash.
- Mitigations (all result-neutral):
  * MTG_TT_CAP (existing) — per-table SimulateToEnd memo cap.
  * MTG_FSL_CAP (NEW, this session; FSLineStoreWin/NoWin) — same contract for the
    full-depth line cache, whose entries are heavy (whole SearchLines). Default
    0 = unlimited = byte-identical.
  * Run the analyzer under `taskset -c 0-7` (8 workers): AffinityCpuCount honors the
    mask, and peak memory scales with worker count.
  Working analyzer recipe: MTG_TT_CAP=1000000 MTG_FSL_CAP=5000 taskset -c 0-7.
- OPEN (flag to user): if this deck joins the regression suite, its cases may need
  the same caps/affinity in the harness env, or a per-deck peak-memory guard; suite
  smoke/regression currently run other decks fine but have no memory margin
  discipline for a deck with this footprint.

## Approved deferrals (user sign-off 2026-08-11, Stage 6 review)

### Mulligan profile: ADOPTED 2026-09-02 (exhaustive keep + blind bottoming)

Generated 2026-09-01, **quarantined** when the confounded bottoming gate failed (+0.1006t, 0/16) —
which turned out to be a generator bug, not a bad heuristic: every bottoming sub-cell held a single
rollout (`docs/design/keepgen-subtable-starvation-detection.md`). Repaired in place by resuming from
the raw, which fills only the deficit (`rollsub = 142464 x 39` exactly, `roll7=0`).

| gate | starved (R=1) | repaired (R=40) |
|---|---|---|
| keep vs static | −0.0587t | **−0.2635t**, 16/16, mean/se −31.84 |
| bottoming, CONFOUNDED | **+0.1006t, 0/16 FAIL** | **−0.0918t**, 16/16, mean/se −18.13 |

Both clear the bar; the profile is live. **GT rebaselined** across all three tiers (20 cells,
`--deck=mirrorwing`): every cell improved, mean **−0.1594 t** (d0 −0.162, d3 −0.166, d5 −0.151), no
other deck's keys touched. Ships as `.keepmodel.exhaustive.profile.json.gz` (the `.gz` is resolved
first); verified the accepted GT reproduces bit-for-bit with the `.gz` in place.

**Caveat — stale generation provenance.** The table was fit at `d2/b3`, and `9927c730` (Mariposa rad
mode, pulled the same morning) changed d2/b3 play `fd0264fe65c2805d` → `3a2f252c8ec95e04`. Its stored
`play_digest` is therefore stale and it will not pool/resume with future runs. Kept because the
shipping condition is unaffected: d5 play is byte-identical across that commit and the A/Bs reproduce
exactly on the new binary. A future regeneration must not pool with this sidecar.

### PENDING SIGN-OFF (agent-recorded 2026-09-01, NOT yet user-approved)

**`mismatch` gate: 1 fd-diverge, attributed to the adopted value leaf.**

```
[fd-diverge] seed=7001 realized_win=5 predicted_win=4 proven_at_turn=1
```

Recorded here so `verify_deck.py` can distinguish a known, measured item from an unexamined
failure. It is **not** waved through: the attribution is experimental, the rate is quantified, and
the mechanism is explicitly still open.

*Attribution (same deck, same seeds, only the sidecar differs):*

| arm | fd-diverges / 240 games (seeds 7001-7004 x 60) |
|---|---|
| v3 list, value leaf REMOVED | **0** |
| v3 list, value leaf present (shipping) | **1** (0.4%) |
| archived v2 list + its own value leaf | **0** |

So it is this particular learned model over-projecting one position — not the ETB-cascade
projection fix (`c3be94f6`), which was the obvious suspect since the diverging line casts Frontline
Heroism on T3, and not value leaves in general, since v2's shows none.

*Why it is not being treated as blocking adoption:* the model's Phase E gate was
**−0.0220 t, 8/8 seeds better, t = −7.55, at 0.64x the cost**, and the skill's adoption bar is
per-game win-turn drift against the baseline (8/0/0 better/worse/tied), which is a different and
passing check. One over-projection in 240 games is a real but small imprecision in an evaluator
that is approximate *by construction* — it replaces the horizon rollout with an O(1) estimate.

*MECHANISM — now established (2026-09-01, instrumented and reproduced):*

```
[fd-diverge] seed=7001 realized_win=5 predicted_win=4 proven_at_turn=1 leaf_est=4
             [WIN WAS A LEAF ESTIMATE, NOT SIMULATED]
```

**The flagged "verified" win was never simulated — it was the value leaf's guess.** The oracle's
`fd_verified` test is `line.win_turn <= turn + searched_depth - 1`, i.e. "the win falls inside the
searched horizon, so the search must have found it by real simulation". That inference was sound
while the horizon leaf was `SimulateToEnd` (a rollout to game end, whose win turn IS simulated). The
learned value leaf instead returns a bare prediction — `FSLineWin`, `depth <= 0`:

```cpp
int w = static_cast<int>((score + 500) / 1000);   // milliturns -> a turn
if (w < state.turn_number) { w = state.turn_number; }
if (w > max_turns) { ...; w = max_turns + 1; }
return { w, {} };                                  // SearchLine carries a win_turn and NOTHING else
```

A prediction landing **inside** the horizon is therefore indistinguishable from a simulated win.
The oracle's own comment says it deliberately excludes "a beyond-horizon leaf ESTIMATE" — the
exclusion is real but **incomplete**: it only catches estimates that land *beyond* the horizon.

This explains every observation at once: it appears only with a value leaf attached (0 without, 0 on
v2's — a different model guesses differently), it is rare (the guess must be both in-horizon and
wrong), and it is not the ETB-cascade fix.

*Severity — worth a decision, not a shrug.* The consequence is larger than a mis-reported diagnostic:
**commit-only-verified can commit a whole line on an unverified estimate**, which is the exact thing
that doctrine exists to prevent (`AIEngine.cpp`: "when the win is only an estimate beyond the searched
horizon, commit just THIS turn and re-search next turn"). The clean fix is to mark the value leaf's
return as *estimated*, propagate that flag up through `SearchLine`, and require `!estimated` both for
the verified-commit decision and for the oracle. That changes commit behaviour (the engine would
re-search instead of committing more often), so it needs its own measured A/B before adoption — it is
**not** a free correctness patch, and is left for the user to schedule.

*Diagnostic shipped:* `MTG_FD_ORACLE` now also prints `leaf_est=`, and tags the line when the
committed win turn equals the leaf's best guess. Gated on the oracle flag; play verified
byte-identical (`fa8fd530b63f885e`, avg 4.9020, unchanged).

*If the user prefers zero fd-diverges:* rename to `Mirrorwing Dragon.value.DISABLED.json` (renaming
is what deactivates a presence-gated artifact) and re-accept the three tiers — the GT accepted in
`3cf1c491` was measured WITH the leaf, so it would have to be redone.


All judged goldfish-inert; none silently dropped:

- Fists of Flame / Ancestral Anger "gains trample": no blockers vs passive opponent —
  inert. Bracket-noted in entries. **APPROVED** ("okay for goldfish").
- Mirrorwing "a player casts" (opponent side): opponent never casts — inert collapse to
  our own casts. Zada "you cast" is exact. **APPROVED.**
- Mirrorwing/Zada "that the spell could target": no protection/shroud/hexproof effects
  in this deck's goldfish — all other own creatures always eligible. **APPROVED.**
- Copy fan-out order (sick → eligible → magnet last): **APPROVED** — user notes the
  magnet-last part "is not even a choice" (the original spell resolves after the copies
  per CR 601.2/608 — correct) and the recipient order matches real play. Optional idea
  (user, unprioritized): a viewer order-choice dialog, off by default.
- Strive extras ranked by PRINTED power: **CONFIRMED CORRECT** by user — a Twinflame
  token copies only printed (copiable) values, so current/buffed power would be a
  misleading rank. Code verified: `ResolveSoloTargetTrick` ranks by `p.card.m_power`
  (printed), not the permanent's effective power. User also notes strive is near-dead
  in practice here (a 2-target Twinflame is no longer copied by Zada/Mirrorwing —
  modeled correctly: `IsSoloTargetTrick` requires the magnet be the ONLY target).
- Mulligan generation deferred to user kickoff: **APPROVED** ("as intended").

## Stage 6 user review — directives (2026-08-11)

- **Trick-target rule blessed**: "If a magnet is present you always cast on it
  search-wise. That's a good rule." Nontoken-only enumeration OK for search but must
  NOT restrict the viewer. Status: the provider prune already does not restrict the
  viewer (human play sets MTG_UNPRUNED — main.cpp play path — which opens
  UnprunedGate::TrickTarget); the nontoken restriction DOES bind the viewer (tokens
  are unnumbered) and is resolved by the token-numbering directive below.
- **Token numbering — FIX FOR SURE** (lifts one-Treasure-per-plan + lets viewer/search
  target tokens). Follow-up change on top of the analysis commit, own verification.
- **Legend-rule corner — keep the COPY when it wins**: user identified the rare case
  where the deterministic keep-original rule is wrong: Zada summoning-sick, Twinflame
  makes a hasty Zada token, and attacking with the token is LETHAL this turn — then
  keep the token (losing Zada at end of turn doesn't matter; the game is over).
  Follow-up: narrow deterministic rule "on legend collision original-vs-own-temp-token,
  keep the token iff the original cannot attack this turn AND projected attack with
  the token is lethal"; both worlds identically.
- **Discard rule spec (user-supplied)** for the NO_RULE_CONSIDER_SEARCH verdict — keep
  priorities when discarding: 1 magnet enabler (Mirrorwing/Zada); ≥3-4 creatures total
  counting Goblin Instigator as 2; enough mana to cast the kept enabler, preferably
  with ≥2 red; then a few tricks preferring outright-win spells (Gold Rush, Fists of
  Flame), draw tricks also acceptable. Structure like antilife/hinata provider rules.
  Follow-up: implement in MirrorwingProvider, A/B vs searched discard per the
  discard-rule process (adopt only if >= searched).

## Stage 4 — profile

Generated 2026-08-11 (analyzer seed 777, MTG_TT_CAP=1000000 MTG_FSL_CAP=5000; card
scores 1000g d5 + discard analysis). Written to decks/Mirrorwing Dragon/
Mirrorwing Dragon.profile.json — card-scores-only baseline, defaults keep,
NO_GATE threshold, bottoming off. Discard verdict: NO_RULE_CONSIDER_SEARCH (0.13
mean label regret; no visible-info rule captured it — report-only, searched-width
escalation is the candidate follow-up). NOTE: profile min_playable = 0 (the skill's
notes say verify >= 1 for multicolour decks) — flagged to the user, not hand-edited.

## Stage 5 — verification log (2026-08-11)

- 5a nonconv (d3 b200, seeds 2002/3003/4004 x 100g): CLEAN — 0 flags.
- 5a fd-diverge (MTG_FD_ORACLE d5 b400, same seeds): first pass flagged 4/300 (each
  realized = predicted+1). ROOT-CAUSED via bp-pay/bounce instrumentation (game 4034
  gi30): depletion-sack TIMING divergence — the executor sacrifices an emptied
  Sandstone Needle immediately (SBA after every cast) while the rollout swept only at
  END of ApplyPlanDirect, so a mid-plan Gruul Turf bounce saw different candidates
  (rollout: dead-tapped Needle -> bounced it, kept Forest; executor: Needle already
  gone -> bounced the untapped Forest) and the recorded chain's last cast (Fists)
  became unpayable (AFFORD_AUDIT: colour-short). Pre-existing divergence — exposed
  because no other deck combines karoos + depletion lands. FIX: BounceKarooLand now
  runs SacrificeDepletedLands before its candidate scan (both worlds; no-op for the
  executor and for every deck without depletion+karoo). Full re-sweep: 0/300 CLEAN,
  and the four flagged games now REALIZE their predicted wins (4034: T5->T4).
  Smoke re-run after the fix: 40/40 byte-identical (fix inert elsewhere).
- 5b multi-depth (300g seed 5005): d0 6.6200, d3 5.2533, d5 5.2467. Monotone;
  plausible T5 swarm clock; search saturates ~d3 (the Goblins pattern); big d0 gap =
  greedy can't see magnet fan-out lines (expected; 5i candidate if d0 ever matters).
- 5c budget: d5 b400 vs b1600, 100 games — ZERO per-game win-turn diffs. No
  starvation at suite-class budgets.
- 5h viewer audit: PASS. Expected types {bounce} surfaced; trick targets surface as
  main_phase plan-variant choose subs (enchant-kind + new "strive" sub); oracle-text
  advisories are the five tricks' target phrases — all modeled+searched (triaged OK).
- 5f A/B (pruned vs MTG_UNPRUNE="tricktarget groupcap", d3 b200, seed 5005, 100
  paired games): DECISIVE for the prune — per-game diff 50 better / 46 equal /
  0 worse (common-game means 5.1458 vs 5.8438, delta −0.6979), AND 3 unpruned games
  (gi 18/60/88) did not complete after >3 h CPU (the enumeration is computationally
  INFEASIBLE unpruned — one completed unpruned game logged SLOW-GAME 814 s). At equal
  budget the unpruned width pure-DILUTES the search. The prune is therefore both a
  feasibility requirement and quality-positive; MTG_UNPRUNED keeps the standing A/B.

## Claude-play sweep
- commit: `<uncommitted tree, 2026-08-11 — the analyze branch state this ledger describes>`
- seeds: 8200 games: 12  (gi 0-6,8,9,11,12,14; gi 7,10,13,15-17 lost to a session cap
  mid-fan-out — the skill's known risk; 12-game sample kept, signal already clean)
- flags: 0 unresolved
  - Cosmetic (4 agents independently): SacForMana plan-summary said "sac 1 creature"
    for a Treasure crack (artifact). FIXED in main.cpp SummarizePlan (text-only;
    smoke re-verified byte-identical after).
  - Quality note (gi5): human-plan payment tapped a dork/depletion counter where a
    free source sufficed — mana allocation is a USER-SANCTIONED greedy area
    ("no greedy steps except attack decisions + mana allocation"); dismissed.
  - Quality note (gi6, uncertain): all-attack instead of lone-attacker Exalted
    stacking — attack decisions are the other sanctioned greedy area; dismissed
    (worth revisiting only if an Exalted-centric deck arrives).
- Win turns: 9 exact ties with the AI benchmark (T4-T6), 3 Claude-slower (+1 each),
  0 Claude-faster. Agents verified fan-out math exactly (gi0: 32 dmg = base 5 +
  escalating drawn-counts 2..7; gi4 Twinflame legend rule; gi12 exact-lethal 11).

## Final verify_deck gate (2026-08-11, logs/mirrorwing_verify2.log)

All checks PASS except one:
- coverage / card_fields / viewer / viewer_wiring / mismatch (0 nonconv+fd across
  seeds 7001-7002 x 60g) / play_invariants (8 games, 96 decisions, determinism+
  integrity+progress hold) / claude_sweep (recorded, 0 unresolved): **PASS**.
- card_costs: FAIL with "(PENDING) cost audit non-zero (rc=124)" — the audit
  SUBPROCESS timed out; NOT a mismatch. A standalone re-run got blanket HTTP 429s
  on every card (Scryfall rate-limiting this IP after the session's snapshot
  refresh). The audit DID pass cleanly in Stage 2 on this same cards.json
  ("All mana costs match Scryfall", recorded above) and no cost has changed since.
  Verdict: network transient; re-run `python3 scripts/audit_card_costs.py` once the
  rate limit clears if a green line in the gate log is wanted.
  **RESOLVED 2026-08-11**: standalone re-run (throttle 0.5s) after the limit cleared:
  "Checked 150 costed cards against Scryfall. All mana costs match Scryfall." — zero
  429s, zero mismatches (log: logs/cost_audit_retry.log). Gate fully green.

## Stage 6 follow-ups — implemented (2026-08-11, on top of 17c29a3)

1. **Legend-rule keep-copy corner (user directive)**: `MirrorwingProvider::LegendKeepIndex`
   keeps the hasty exile-at-end Twinflame copy over a summoning-sick original iff simulating
   the attack WITH the copy is lethal and WITHOUT it is not (RolloutSimulateCombat on scratch
   copies -- the pending-damage projection over-counts, commit-the-line lesson). Phase-gated to
   PreCombatMain/Combat; the rollout now maintains GameState::phase at its combat/turn-boundary
   sites (write-only for every other deck). Smoke 30/30 byte-identical.
2. **Cleanup-discard rule (user spec)**: MirrorwingProvider bucket policy -- keep 1 magnet
   (cheapest; Zada+Mirrorwing are one InterchangeableRequiredGroup role), >=4 weighted bodies
   (Instigator=2), mana to cast the kept enabler preferring 2 red + a green for a held Gold
   Rush, keep Gold Rush/Fists by omission, shed order Scale->Expedite->Twinflame->Anger.
   MTG_MW_BUCKET_DISCARD=0 reverts to the generic ranking (A/B lever). Smoke 30/30
   byte-identical.
3. **Token numbering (user: "fix for sure")**: `GameState::next_token_number` (base 1000);
   all five shared token-creation helpers assign unique per-copy ids; trick-target enumeration
   and the provider prune now include tokens (equivalence fold keeps width); multiple Treasures
   are distinct sac sources (the one-crack-per-plan collapse is gone). Sim keys don't fold
   m_number -> TT dedup unaffected.
   - **Pre-existing defect EXPOSED and fixed**: two sac-outlet activations enumerated against
     the same board bake the SAME canonical victim; under shared id 0 the second silently
     aliased onto the next token, and (worse) ApplySacForMana's `victim_id != 0` guard sent a
     token victim down the "sacrifice the SOURCE" path -- Skirk Prospector killed itself
     whenever its chosen victim was a token. Fixed with a canonical stale-victim RE-PICK at
     both applies (fungible victims; shared helpers, lockstep).
   - Suite impact: goblins-only, score-neutral after the re-pick fix (final full regression:
     searched slower=0 faster=0 play-changed=27; d0 slower=0 **faster=2** play-changed=8 --
     net POSITIVE; the play changes are which-token-dies identity churn + the corrected
     Prospector model). All other decks byte-identical. Viewer protocol 184 refs clean.
     GT digests for the 5 goblins configs need an accept (score-neutral rebaseline).

## Stage 6 round 2 (2026-08-11): branching enumeration + cap question + magnet preference

- **Magnet preference (user)**: kept-magnet choice in the discard rule is now
  earliest-cast-turn based (board sources + hand mana, one drop/turn, red pips checked):
  Mirrorwing when castable the SAME turn as Zada, Zada when strictly earlier or Mirrorwing
  uncoverable; tie -> bigger body. Commit f9c679c, smoke 30/30 byte-identical.
- **Branching enumerated (MTG_BRANCH_STATS, heavy game 5041/gi36)**: Twinflame drives ~94%
  of total odometer (345k of ~368k; single option-groups up to 768 wide) -- the width is the
  target x strive-count product INSIDE one group, i.e. mostly NOT the group-count axis the
  EnumGroupCap bounds. Ancestral Anger a distant second (21k).
- **Strive dominance fold (new, lossless)**: with a magnet on the battlefield every strive
  K>0 variant is goldfish-dominated by the solo-target fan-out (all creatures copied for
  base cost vs K+1 chosen for base + K x strive). K>0 now enumerates only on magnetless
  boards; opened by MTG_UNPRUNED/tricktarget (viewer unaffected -- human play is unpruned).
  300g d3: avg 5.2433 IDENTICAL to pre-fold, same 5 unwon games; smoke 30/30. A 1-game
  4->5 flip at b200 was classified budget churn (recovers at 4x; unpruned control is WORSE
  at b200: 6.0 -- dilution).
- **Cap-8 vs cap-12 A/B (user question "is lowering the cap the right lever?")**: cap-12
  (MTG_SOLVE_GROUP_CAP=12) re-inflates the tail at suite budgets -- same-game gi88 154s
  (cap 8) vs 253s (cap 12), gi60 <30s vs 119s, plus a still-longer tail game; the earlier
  NO-cap arm had a 1139s game and was killed after 7 CPU-h. Both wider arms confirm the
  cap's feasibility role. Result-quality comparison recorded when the arm lands.
- **Escalation question (user)**: verified in code -- there is NO capped-then-expand
  breadth escalation; the cap is static per provider, MTG_UNPRUNED/MTG_SOLVE_GROUP_CAP are
  manual levers, and the existing escalation ladder (value-leaf hybrid) escalates
  depth/budget only. The board-lethal short-circuit protects attack-only wins from the cap,
  but a CAST-dependent this-turn lethal needing a dropped group is unprotected (the win is
  found a turn later instead). The cap-site comment overclaims ("never drops a win").
  PROPOSAL (user to approve): expand-on-no-win rung -- when the capped enumeration finds no
  winning plan AND groups were dropped AND a cast-dependent lethal is plausible, re-enumerate
  once with the cap lifted. Bounded (fires only on no-win nodes) but exactly those nodes are
  the expensive ones -- needs a plausibility gate + measurement before adoption.

## Stage 6 round 3 (2026-08-11): Twinflame/go-off policy (user-authored) + suite add

USER play knowledge encoded (rounds 3-4 of the review):
- **Go-off cast order** (the search does not branch orderings; opaque sets applied plan-order
  before this): enabler ladder magnet(5) -> creatures(10) -> Twinflame(12) -> Gold Rush(15) ->
  draw tricks(20), via MirrorwingProvider::CastEnablerFirst/CastOrderRank + a stable
  CastOrderRank sort of the ENABLER loop in BOTH worlds' opaque apply paths (equal ranks keep
  plan order -> byte-identical for every other provider; smoke 30/30 confirmed). Rationale:
  bodies before the doubler ("as many creatures as possible... then Gold Rush"), the doubler
  before the pumps, Gold Rush before the DRAW tricks (its Treasures are spendable at the next
  draw-breakpoint re-solve -- "essentially a ritual"; its own pump counts its own Treasures).
- **Twinflame targets**: magnet fan-out UNGATED (draw-breakpoint re-solves own "might draw
  into lethal" -- no heuristic lethal gate, per user); magnetless boards offer a non-magnet
  target ONLY in the gap-closing corner (pending attack short of lethal by <= total copyable
  printed power -- "you would only cast it when you are 1 damage short. Pretty rare"); hand
  magnets kept (same-plan go-off). User accepts dropping marginal lines (Twinflame on
  Instigator for two 1/1s) as long as measurement stays clean.
- **Strive counts**: K in {0, max affordable} (StriveCountMaxOnly; a lethal burst, not a dial).
- MEASURED: branch stats -- Twinflame fell from #1 driver (6.0M sum_odo at suite budget) to
  below the top 5 (total odometer ~-30%). d3 300g s5005: 5.1633 vs 5.2433 (-0.08); HELD-OUT
  fresh seed 9009: 5.0967 vs 5.1867 (-0.09) -- validated, no overfit. Suite shapes now
  d3 b10 ~0.9 s/game, d5 b20 ~1.8 s/game (hinata-class).
- **SUITE ADD**: mirrorwing in test/regression_cases.sh (smoke/regression hinata-mirror
  sizing + overnight rows). Smoke + regression run: ALL PASS, 3+5 NEW keys accepted; all
  other decks byte-identical. Overnight keys will appear NEW on the next overnight run.
- ~~OPEN (deferred): same-plan Treasure credit for Gold Rush without a draw breakpoint~~
  DONE 2026-08-12, by a DIFFERENT (sounder) mechanism than the sketched planner credit: a
  `creates_treasures` cast now arms the SAME deferred site-3 post-cast re-solve the draw
  tricks use (`ApplyPlanDirect` solo-trick branch + `PlanOpensBreakpoint` + the executor's
  `note_draw_engine` d0 second pass). The re-solve sees the minted Treasures as REAL
  SacForMana candidates -- no credit arithmetic, no over-projection, executor lockstep via
  the existing recorded-script replay (fd-diverge 0/300). A planner credit was rejected:
  order-blind wild credit could fund casts that pay BEFORE Gold Rush (Twinflame at rank 12
  vs GR 15), and realization would have needed payment-side treasure-cracking.
  **DEFECT FOUND AND FIXED during measurement** (trace of s3003 gi52, T5 lethal lost): the
  first cut let a GR cast INSIDE a recorded continuation take the INLINE re-solve path --
  which (a) only runs when the continuation is being recorded (sink_stack non-empty), so the
  committed line diverged from the scored one, and (b) greedily tapped attack-ready mana
  dorks mid-continuation, stranding a proven this-turn lethal (22 -> 14 damage). Fix: GR
  arms the MAIN-LEVEL deferred re-solve only, never the inline one; a Treasure minted
  mid-continuation waits for the next decision (as before the feature).
  MEASURED (vs fd492d5 worktree control): d3 300g s5005 1 faster/2 slower; HELD-OUT s9009
  5 faster/0 slower; s3003 d3+d5 1 faster/3 slower; net -2 turns over 900 games. The
  recovered class is real (g70 s9009: bank GR T2 -> Mirrorwing T3 -> T4 win, was T5); the
  slower class is the same bank-vs-curve judgment landing wrong (gi28) plus near-tie flips
  in sanctioned-greedy areas (attack-hold, land-pick ties -- gi253 traced). All slower games
  traced to decisions; none are divergences or wasted resources.

## Open items / next steps

**(Updated 2026-09-03: this section is CLOSED.** The pipeline completed for the shipped
Anger-4/Oracle-3 list — profile, value leaf (adopted 7d13fd6c, 2026-09-01) and exhaustive keep
table (adopted 17dac2d7, 2026-09-02) are all committed under `decks/Mirrorwing Dragon/`, and
`test/regression_cases.sh` points mirrorwing at the CURRENT list, not v1. Every
"pending"/"deferred"/"running"/"points at v1" line below is the pre-completion record.)

- Mulligan profile generation: DEFERRED (user kicks off; policy 2026-07-17).
- Value-leaf: not generated (user decides post-freeze).
- ~~card_costs gate line~~: DONE 2026-08-11 — clean pass, 150/150 match (see above).
- Goblins value sidecar predates the corrected sac model (engine-state fingerprint policy):
  regeneration is cheap (~3 min) if pooling/regen ever needs it.
- ~~Expand-on-no-win breadth escalation~~: SUPERSEDED and DONE 2026-08-11 by **group waves**
  (defer-don't-cap deferred tranches for EnumGroupCap-dropped groups; user-approved in
  principle, then measured and adopted default-ON). Design + measurements:
  `docs/design/group-waves.md`. Headline: smoke game 137 (seed 1138) was UNWON under the cap
  and wins at T8 with waves — the exact "cast-dependent lethal in a dropped group" hazard the
  escalation question uncovered; every other deck byte-identical, wall cost free (16s vs 17s on
  the heaviest case), `MTG_GROUP_WAVES=0` restores the old engine byte-identically.
- ~~Deck-aware SituationalCardRank for Mirrorwing (which groups the cap drops)~~ MEASURED
  AND REJECTED 2026-08-12 (heuristic-optimization loop, scaffolding deleted): three motivated
  orderings (V1 chain-fuel-first: Fists > cantrip tricks > Gold Rush > Twinflame > bodies,
  with magnet-offline on top; V2 bodies-first; V3 Gold-Rush-high) swept vs the generic
  2-level rank on train seed 5005, d3 300g + d5 150g. V1/V2: **byte-identical win turns on
  every game at both depths**; V3: identical outcomes but one game 100x slower (it tickled
  the pre-existing `TapForCostBacktrackWorker` blow-up -- docs/design/tap-backtrack-blowup.md).
  The ranking path is live (V3 proves it), but the cap's keep-set choice is outcome-inert at
  suite shapes: the odometer already covers the winning plans within any 8 kept groups, and
  the group waves recover dropped tranches when budget allows. Baseline wins; complexity not
  earned. Revisit only if a budget/shape change makes the cap bind harder.

---

## 2026-08-22 — NEW SHIPPING LIST (tournament winner + Game Trail); v1 archived

The shipping list changed (user-directed). `decks/Mirrorwing Dragon/` is now the tournament's
measured winning suite; the previous Twinflame / Ancestral Anger / Scale the Heights / Expedite
list is archived intact at `decks/Mirrorwing Dragon/v1-twinflame-anger/` with ALL its fitted
artifacts (profile, value model, keep table + raw, caches) — they move together because every
sidecar resolves directory-relative off the profile.

**New list (60):** 4 Mirrorwing Dragon · 4 Zada · 4 Goblin Instigator · 4 Ignoble Hierarch ·
4 Elvish Mystic · 4 Fists of Flame · 4 Gold Rush · 4 Impolite Entrance · 3 Luxurious Libation ·
2 Oracle's Restoration · 2 Fortifying Draught · 8 Forest · 4 Game Trail · 3 Sandstone Needle ·
2 Gruul Turf · 2 Mountain · 2 Rootbound Crag.
Mana-base delta vs v1: −2 Mountain, −1 Forest, −1 Kazandu Refuge, +4 Game Trail.

### Game Trail — the reprint assumption was WRONG (Stage 2a earns its keep)

First implemented as a copy of Rootbound Crag on the recollection that they are functional
reprints. **They are not.** Scryfall:

> As this land enters, you may reveal a Mountain or Forest card **from your hand**. If you don't,
> this land enters tapped. {T}: Add {R} or {G}.

Rootbound Crag checks the BATTLEFIELD ("unless you **control** a Mountain or a Forest"); Game Trail
checks the HAND. The two diverge in opposite directions at both ends of the curve — Game Trail is
untapped on T1 off a Forest in hand (likely at 8 Forest) where the Crag is tapped, and tapped on an
empty late-game hand where the Crag is untapped. Corrected to **Tier 1**, no engine change: the
mechanic already exists as `etb_untap_reveal_subtypes` (Frostboil Snarl's cycle), which
`LandWouldEnterTapped` resolves through the shared predicate, so enumeration pricing and the real
land drop agree, and `LandEntryHasChoice` already surfaces the reveal to the play viewer (bucket A).
Verified against the committed Scryfall snapshot (`audit_card_fields.py`): oracle text matches
exactly.

### Pipeline state

| stage | status |
|---|---|
| 1 Coverage | **DONE** — 17 cards, 0 missing, every entry `full` |
| 2 Implement gaps | **DONE** — Game Trail only (Tier 1, above) |
| 2d-bis Cost/field audit | **DONE** — Game Trail in the snapshot, fields match (some unrelated cards 429'd; pre-existing advisory oracle diffs on Stompy cards are not from this change) |
| 3 Re-run coverage | **DONE** — clean |
| 4 Baseline profile | **NEXT** — `python3 scripts/analyze_deck.py "decks/Mirrorwing Dragon/Mirrorwing Dragon.cod" --no-rebuild` |
| 5 Verify | pending — `python3 scripts/verify_deck.py` gate |
| value leaf | pending — after 5 converges (`.claude/skills/value-leaf.md`) |
| mulligan profile | pending — LAST, user-kicked-off (`.claude/skills/mulligan-profile.md`) |

### Wiring notes for whoever resumes

- The new list has **no profile / value model / keep table yet** — that is what Stage 4 onward builds.
- `test/regression_cases.sh` deliberately still points `mirrorwing` at the **archived v1 list**
  (user: switch it once the new list has its own artifacts). GT is therefore unchanged and green.
- The 24 hand-played references were played on v1 and are BOUND to it by
  `deck_registry.REFERENCE_DECK` — never let them resolve to the new list by slug (verified: they
  bench against v1 with zero HAND-MISMATCH).
- `decks/Mirrorwing Trick Suite/` was deleted; its two scenario gates
  (`libation_x_lands_not_dorks`, `draught_magnet_escalation`) now run on the NEW list, and
  `anger_draws_feed_fists` on the archived v1 list.

---

## 2026-08-22 (overnight) — value-leaf generation + mulligan handoff, running autonomously

Stages 4 and 5 are CLOSED for the new shipping list. Stage 5 `verify_deck.py` reports
**GATE PASS**: coverage 17/17 full, card_costs all matching Scryfall, card_fields 245 matching,
viewer self-guard + surface sweep clean, no nonconv/fd-diverge, play invariants over 164
decisions, claude-play sweep 0 unresolved flags.

Two gate defects were fixed to get there, neither specific to this deck (commit `c74b049b`):
seven trick-pump params on Luxurious Libation / Fortifying Draught were unclassified in the
viewer manifest, and `gate_card_costs` counted a *timed-out* audit as "1 cost mismatch" while
capping a ~10-minute cold sweep at 300s.

### The value-leaf queue nearly measured the WRONG DECK

`valueleaf.sh status` for the new list reported every phase complete — 12,570 rows, a trained
model, a 94% matrix — all of it generated 2026-08-13/16 from the **archived v1 list**. The queue
dir (`logs/vlq_<key>`) and staged model (`logs/eval/<stem>.value.STAGED.json`) are keyed by the
deck FAMILY, and `run` is idempotent via done-markers, so it would have skipped every phase and
returned a "finished" model fitted to a deck we no longer ship.

The freeze could not catch it: it fingerprints `src` and PLAY (engine state only). The play digest
is no help either — a just-swapped deck is still deliberately pinned to its OLD list in smoke, so
the fold does not move. Fixed in `da5aad3a`: `freeze.decks` stamps a sha256 per decklist and both
`run` paths hard-stop on a mismatch (nothing to salvage, unlike a play change; and it refuses to
auto-wipe work belonging to the previous list). v1's artifacts are preserved as
`logs/vlq_mirrorwing_dragon_v1_twinflame_anger` and
`logs/eval/Mirrorwing Dragon.v1-twinflame-anger.value.STAGED.json`.

### Run state

Frozen at `da5aad3a`, src-tree `d10440440fcc`, play digest `1a04ebebfad8`.

| phase | state |
|---|---|
| 0 freeze+build | done |
| A rows | **done** — 12,319 rows from the full 2,500 games (v1: 12,570). Phase A has NO wall-clock backstop (its manifest carries no `condemn` block); the 3-game tail held 29 cores idle for ~30 min but landed on its own |
| A split / B train | done — 12,319 unique rows, held-out RMSE **0.6590** |
| C matrix | running — H=[1..5] V=[1..8], 400/cell, ONE pool, 32/32 cores |
| C.5 / D / E / F | pending |

### Standing instructions for the overnight run (user, 2026-08-22)

1. **Adopt the value model iff it is better on PERFORMANCE and better-or-EVEN on QUALITY**
   (phase E is the gate; judge quality on avg win turn). Adoption is not the default outcome —
   of the last nine models one was a clear win, six neutral.
2. If adopted, the standing gate applies before commit: **smoke + regression**, and an overnight
   tier for anything that moves a `value_trust_depth`. Trap: sidecar PRESENCE activates the
   hybrid, so a rejected model ships as `<stem>.value.DISABLED.json`, never `enabled: false`.
3. **Then start the mulligan run using the SAME generation settings as the previous deck**:
   `mull_gen_depth: 2`, `mull_gen_budget_ms: 3` (v1 also recorded `expected_buckets: 17`).
   v1 derived these by measurement 2026-08-16 (24 hands, R=12) against d5/b20 default play —
   d1/d2/d3 at b3 were indistinguishable in rank fidelity, so the tie broke on units/rollout.
   Phase F (`mullgen_finalize.py`) derives these independently; if it disagrees, follow the
   user's directive and REPORT the divergence.
   These live in `value_play` inside `<stem>.value.json` — and note `MulliganProfileIO.h` was
   fixed this session to stop dropping a presence-only sidecar that carries `mull_gen_*` but no
   `target_depth`, which is exactly v1's shape.
4. Mulligan route per `.claude/skills/mulligan-profile.md`: **skip the feasibility pre-check and
   the `recommend` scout — go straight to `--gen-mulligan fast`** (user, 2026-08-22). Justified
   rather than assumed: v1 recorded `expected_buckets: 17` and the new list has 17 DISTINCT cards
   over the same creature core, so K ≤ 17 and the hand count `C(K+6,7)` cannot exceed v1's, which
   generated successfully. `fast` = adaptive bottoming, cap R30 — comfortably clear of the hard
   R ≥ 10 floor below which no runtime profile can be written at all.
   Run it STRICTLY AFTER the value leaf (user: "they should run sequentially"). Never while
   phase C is live: phase C's `--intractable-median-sec-per-game 30` cutoff is WALL-CLOCK, so
   added load could condemn cells spuriously.
   Resume, if it is interrupted, is the IDENTICAL command — cells are journalled as they commit;
   there is no resume flag and no driver script.
5. **Then top up to R40 with a merged R=10 chunk** (user, 2026-08-22). `fast` is R30; a second
   pass at `MTG_KEEP_ROLLOUTS=10` on a DIFFERENT `--seed`, merged via `MTG_KEEP_MERGE`, sums
   per-cell `sum`+`count` element-wise to give R40 for ~a third of the first run's cost. Ship the
   MERGED profile. Deliberately NOT a second `complete` run — that would pool to R70, far more
   than one profile warrants.
   Three things this depends on, all checkable before spending anything:
   * Same commit + same buckets + **non-overlapping seed** — the merge is fingerprint-gated on
     `bucket_fp`/`deck_fp`/`commit` and rejects seed overlap. Read the fast run's `seed_base`
     out of its raw sidecar and pick a distinct one; record it here (per-deck seed ledger).
   * **Play must not move in between.** Keep-gen rollouts run the deck's real play policy,
     value sidecar included, so the value-model adopt decision must be SETTLED FIRST or the
     R30 raw is stranded and unpoolable. This is why adoption precedes generation.
   * A sub-10 chunk writes only a poolable raw, never a `.profile.json` — that is by design, and
     the merge is the step that produces the shippable artifact.
   Honest limit: this buys complete's **R**, not complete's **bottoming** (`complete` also sets
   `adaptive_bottom=false`, which a top-up cannot retroactively apply). Check the report's
   projected-regret-vs-R curve to see whether R30→R40 is measurable on this deck at all before
   treating the top-up as necessary.
6. Commit the profile AND the gzipped `.raw.json.gz` — the raw is the poolable unit, and without
   it the top-up/fill-out option does not exist.
