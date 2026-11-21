# burn-damage-order handoff 2 (2026-11-21 ~09:30 UTC, agent 2 -> agent 3)

Worktree `/workspaces/MagicDeckTester/.claude/worktrees/burn-damage-order`, branch `wip/burn-damage-order`
(pushed to origin as a wip branch; NOT the shared branch). Read `handoff.md` (same dir) first for mechanisms and
the original step list; this file supersedes its "Remaining" section.

## Done (commit hashes)
- c8cc17d7b (agent 1) WIP: `MTG_PO_DAMAGE_ORDER` (default OFF) apply-site damage-order fallback + spectacle credit;
  Lavamancer ping drops reported to the act-drop sink (oracle-only).
- 17382ff7a (agent 2): the spectacle credit's forced `before` under PO_DAMAGE_ORDER only for a movable
  DirectDamage hand burn without a reveal cost (a Vexing Devil trigger ranked after is never reordered by
  DamagePayableOrder, so crediting it would be unrealisable).
- Built clean (Release, build/ -> build volume). Full unit suite 3805/3805 green. Oracle-only identity verified:
  MTG_MANA_ORACLE=0 vs 2 on 1DB + Midrange, d3/b10 + d5/b20, 40 games each, seed 72001 -> all four .wins files
  byte-identical including play digests (build/scratch/bdo/ident/).
- Repros (build/scratch/bdo/rp/*.out; arms base / fl=FLOAT / fd=FLOAT+DMG; script build/scratch/bdo/repros.sh):
  * 1DB s30000108 T2 {Fireblast, suspend Rift Bolt, Needle Drop}: base + fl APPLY-DROP-ORDER, **fd CLEAN**.
  * 1DB s30000096 T2 same family: **fd CLEAN**; its T3 Lavamancer ping now DROP-COVERED (Family B reporting works).
  * Midrange s30105052 T3 {Fireblast, Skewer}: **fl and fd CLEAN** (float alone fixes).
  * Midrange s30105349 T2 APPLY-DROP {suspend, Light Up, Fireblast}: fl fixes it; a NEW T3 ORACLE-GAP
    {suspend Rift Bolt x2, Fireblast} appears in both float arms (engine leaves Mountain#31 untapped, oracle's
    nearest floats {R}). Not investigated; let the gate count it.
  * 1DB s30000167 T3 {Needle Drop, Fireblast, Fireblast}: still flagged under fd -- see the finding below.
- NOTHING has been A/B'd, no lever flipped, no GT touched, no smoke run (identity is by construction with levers
  OFF; run smoke once, after the rebase).

## FINDING: Fireblast double-naming ENUM PHANTOM (not fixed)
Each Fireblast copy in hand emits its alternative-cost variants independently (TurnSolver.cpp ~43494,
`a.tutor_target = "fireblast:<m1>,<m2>"`), so with two Fireblasts and only two Mountains BOTH copies carry the same
pin, and nothing in the plan guard rejects a selection holding both. The apply then silently drops the second
(TurnSolver.cpp ~75577: `if (IsFireblastAlternative && !PayFireblastAlternative) return;` -- no NoteActDrop),
so no order of the line can pay every cast and DamagePayableOrder keeps the given order (Needle Drop first ->
dropped). The mana oracle also believes the second Fireblast resolves ("Fireblast > Fireblast > Needle Drop pays
every action"). Repro: s30000167 gi167 T3, `MTG_FIREBLAST_FLOAT=1 MTG_PO_DAMAGE_ORDER=1 MTG_MANA_ORACLE=3
MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/1 Drop Burn/1 Drop Burn.cod' --profile 'decks/1 Drop Burn/1 Drop
Burn.profile.json' --games 1 --seed 30000167 --game-index 167`.
**Gate claude-queue-20261120b: 573 flags have a plan holding two `cast:Fireblast`** (1DB 451, Midrange 122; by
class APPLY-DROP 52, APPLY-DROP-ORDER 118, ORACLE-GAP 403). Counted from
/workspaces/MagicDeckTester/build/wt/batch-1120b/scratch/mana_clean/claude-queue-20261120b/on.0.err with
`grep 'mana-oracle\] \(APPLY-DROP\|APPLY-DROP-ORDER\|ORACLE-GAP\) ' | grep 'cast:Fireblast.*cast:Fireblast'`.
Proposed sound fix (a physically impossible selection, not a pick): a new kDup clause in
SubsetHasDuplicateSacSource (TurnSolver.cpp ~22381; clause enum ~22049) rejecting two Fireblast-alternative
casts whose pins share a Mountain, plus its SubsetFilterPre::DupM compact entry bit (the compact path must keep
skipping only clauses that would return false), plus a NoteActDrop on the apply's silent return. Check first
whether the other session's 1DB update already changes Fireblast pins / payment (it may supersede this).

## THE TEST that detects the other session's 1 Drop Burn update on origin
Baseline = 906d3c0a4 (origin tip when the hold began; origin was at 1f0a46ce2 at 09:30 UTC with all three EMPTY).
Run in this worktree after `git fetch -q origin`; ANY non-empty output = the update (or part of it) has landed:
```
git log --oneline -i -E --grep='drop burn|fireblast|lavamancer|1DB' 906d3c0a4..origin/phase-1-2-deck-analyzer
git log --oneline 906d3c0a4..origin/phase-1-2-deck-analyzer -- "decks/1 Drop Burn" "decks/Midrange Burn"
git log --oneline -G'OneDropBurn|ONEDROPBURN|FIREBLAST_FLOAT|FireblastAlternative' 906d3c0a4..origin/phase-1-2-deck-analyzer -- src/
```
(Agent 2's waiter used only the first, with `--grep='drop burn\|fireblast\|lavamancer\|1DB'`; the other two are
belt-and-braces.) The owning session's work starts at 1eff08565 "1 Drop Burn: USER 2026-11-19 items as default-OFF
levers for an A/B". It may land in several commits -- confirm with the coordinator that it is COMPLETE before
running the A/B.

## Remaining, in order (agent 3)
1. When the update is on origin: `git fetch` + `git rebase origin/phase-1-2-deck-analyzer` (never merge). Delete
   stale .o for touched TUs, wait ~200 s (9p clock lag), rebuild with `./build.sh` via
   `bash scripts/box_lane.sh --slots 4 --cap-gb 3 -- ./build.sh` (box_lane.sh is not +x in git: call it with bash).
2. Overlap check against the update: MTG_FIREBLAST_FLOAT (EngineFlags.h ~1115, SpellEffects.h PayFireblastAlternative
   ~27054), MTG_ONEDROPBURN_FIREBLAST_EARLY (DecisionProviders.cpp ~67063, OneDropBurnProvider::CastOrderRank),
   my DamagePayableOrder (TurnSolver.cpp after UnlockPayableOrder; hooked in ManaPayment.cpp PayableCastOrderCore),
   the spectacle credit (BuildSpectacleCreditCtx), NoteGraveyardDamageDrop. DROP anything the update supersedes
   (e.g. if it reorders Fireblast ahead of Needle Drop/Skewer by policy, re-measure whether DMG still adds anything).
   Do NOT take FIREBLAST_EARLY decisions -- USER-owned (ruling below).
3. Lavamancer prune (USER-ordered, below): Grim Lavamancer's ping (TurnSolver.cpp ~48943, the
   `tap_graveyard_exile_damage` block: `targets{0, kAbilitySelfTarget}` + every creature/animated/planeswalker on
   the battlefield) targets ONLY the opponent (0), plus OPPONENT-controlled creatures/planeswalkers (they do not
   occur in goldfish but keep them legal). Drop kAbilitySelfTarget and every own permanent. Per CLAUDE.md the
   prune belongs in the DecisionProvider (ORDER/RESTRICT only) like the fuel prune next to it
   (`ResolveProvider(state).GraveyardDamageFuelCandidates`, UnprunedGate::LavamancerFuel in DecisionProviders.h
   ~173): add a provider hook / root default, keep every target in human play (`HumanPlayActive()`) and under an
   UnprunedGate audit gate. Only 1 Drop Burn and Midrange Burn hold Grim Lavamancer. Evidence it matters: gate plans
   with `gy-exile:Grim Lavamancer@13` targeting OUR Goblin Guide#13 (the oracle realised it and killed the Guide),
   and the self-target ping (own life 20 -> 18) in the ORACLE-GAP family. Unit test it.
4. Optionally the Fireblast double-naming fix above (sound; default ON; part of the same GT accept).
5. A/B (held until 1-4 are in): `python3 logs/burn-damage-order/mk_ab.py build/scratch/bdo/ab.json` (44 jobs, 65,600
   games, seed 71001; arms base/float/dmg/fdmg/early over 1DB, Midrange, Burn, Modern RW Burn + 2HG; regenerate
   after the rebase -- the arm set must match the levers that survive step 2, and the "early" arm is MEASURE-ONLY
   for the USER). Run as ONE big process: `flock /workspaces/MagicDeckTester/build/scratch/claude_box.lock env
   MTG_RSS_CAP_GB=40 MTG_DUMP_UNITS=1 build/Release/mtg --batch build/scratch/bdo/ab.json --threads 0 --game-log-dir
   build/scratch/bdo/ab_wins`, then `python3 test/paired_arms.py build/scratch/bdo/ab_wins --base base --arm <arm>`.
   If the Lavamancer prune is default ON (USER-ordered), measure it as its own arm too (prune off vs on) so its
   effect is reported. Bar: every slower game recovers at `--depth 8 --budget-ms 0`; every deck net <= 0 (+1 is not
   neutral); report the CPU ratio and cut any cost above noise.
6. If the bar passes: flip the surviving levers default ON (FLOAT is the USER's own 2026-11-19 request; DMG if it
   still adds), then smoke + regression (`bash test/regression.sh --both` under the box lock), audit every changed
   game, accept in THIS worktree, `python3 test/check_gt_logs.py`.
7. Gate slice under the box lock: `python3 scripts/mana_clean.py run --games 5000 --name <name> 1_drop_burn
   midrange_burn` (deck KEYS). Compare to claude-queue-20261120b: 1DB 7,478 = ORACLE-GAP 4,881 + APPLY-DROP-ORDER
   2,232 + ENUM-GAP 361; Midrange 4,778 = APPLY-DROP 2,594 + ORACLE-GAP 1,087 + ENUM-GAP 556 + APPLY-DROP-ORDER 541.
8. Doc: docs/design/burn-damage-order-2026-11-21.md (mechanisms, measurements, flags, both USER rulings verbatim).
Still unaddressed (from handoff.md): Midrange ENUM-GAP {Fireblast, Incinerator, Bolt} (Incinerator priced full at
plan start); 1DB ENUM-GAP {Skewer, Fireblast, suspend} T2 (the 17382ff7a credit change targets it -- verify in
the gate); T1 PHANTOM {land, Needle Drop} with no damage source on the menu (noted only).

## USER rulings (VERBATIM)
2026-11-21 (relayed by the coordinator):
- On MTG_ONEDROPBURN_FIREBLAST_EARLY / the 1 Drop Burn cast order: "The agent that worked on this made some changes
  for this. Let's leave this item until we see their update on origin."
- On the Grim Lavamancer self-target ping: "This indeed should be pruned. In fact everything but the opponent should
  be pruned. That said, let's wait for the other 1-drop burn changes I mentioned above to land on origin."
- Coordinator's reading: do NOT run the 1DB A/B, flip any 1DB lever, or take Fireblast-early decisions until the
  update is on origin; once it lands rebase, check overlap, drop anything superseded, implement the Lavamancer prune
  (opponent only, or the opponent's creatures/planeswalkers if those ever matter; every other target pruned), then
  run the A/B on what remains.
From the original brief (see handoff.md for the full list):
- "A play change must be sound, never a greedy pick in the search window."
- "Every slower game must recover at `--depth 8 --budget-ms 0`. Every deck must net <= 0; +1 is not neutral."
- "Report the CPU cost and cut it if it is above noise. The 3x suite limit is not a CPU licence."
- "Default ON only if the bar passes. Then accept smoke+regression GT in this worktree"
- "An oracle-only fix must keep oracle-off play byte-identical."
- "USER-owned decisions (cast order lists, keep policy, deck settings) are surfaced in your report, never settled by you."
- "NEVER push phase-1-2-deck-analyzer and never join the push queue; the parent lands it."
- From the code: MTG_ONEDROPBURN_FIREBLAST_EARLY was the USER's suggestion 2026-11-19: "Casting it before Needle Drop
  and Skewer could be okay if we float the red mana automatically."

## Running jobs
NONE (agent 2's origin waiter stopped at the coordinator's request). Scratch: /workspaces/MagicDeckTester/build/scratch/bdo/.

## Environment notes
- The 4-slot lane was saturated for hours (a 4-slot build waited ~2.5 h; units ~4 h at 1 slot). Queue early.
- scripts/box_lane.sh is mode 644 in git: invoke as `bash scripts/box_lane.sh ...`.
