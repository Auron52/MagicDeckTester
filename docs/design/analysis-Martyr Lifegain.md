# Analysis ledger — Martyr Lifegain

Deck: `decks/Martyr Lifegain/Martyr Lifegain.cod` (60, mono-white + 2 Orzhov Basilica; Martyr of
Sands / Serra Ascendant / Speaker of the Heavens life-total payoffs, Abiding Grace + Lurrus
recursion, Ranger of Eos / Ranger-Captain tutors; no sideboard, so no wish reach).
Started 2026-11-10 on branch `phase-1-2-deck-analyzer` (base 5db229fa6).

## Stage 1 — coverage (2026-11-10)

Already implemented (12): Ranger of Eos, Ranger-Captain of Eos, Serra Ascendant, Martyr of Sands,
Swords to Plowshares, Unexpectedly Absent, Speaker of the Heavens, Orzhov Basilica, Plains,
Abiding Grace, Mother of Runes, Umezawa's Jitte. No fetchlands (`fetch_hold_review.required=false`).

Missing (6): **Lurrus of the Dream-Den, Squadron Hawk, Kami of False Hope, Thraben Inspector,
Curious Farm Animals, Enduring Innocence.**

## Stage 2 — cards

Research fan-out (2026-11-10): four Opus agents, read-only (Hawk + Thraben Inspector and Kami + Curious Farm Animals paired);
integration serial.

| card | tier | model | viewer bucket | status |
|---|---|---|---|---|
| Lurrus of the Dream-Den | 3 | `gy_cast_permanent_max_mv 2`: once-per-turn per-object graveyard CAST permission (reuses `once_per_turn_used`), `CastFromGraveyard` actions with `gy_cast_source_id`, executor gy-cast path through `CastSpellFromHand`, site-9 activation key per castable gy name (re-solve on a new legal gy card / Lurrus entry), `GyR_GyCastNames` dominance bit; Companion inert tag | B: `gycast=` verb + clickable graveyard | IMPLEMENTED de56cc001 (rebased 46467b0e5) |
| Squadron Hawk | 1 | `etb_tutor_hand_count 3` + `wish_requires_name` (Ranger of Eos multi path) | A: `sac_tutor` | IMPLEMENTED f402e5aeb |
| Kami of False Hope | 2/3 | self-only sac outlet + `sac_outlet_prevent_combat_damage` (`GameState::combat_damage_prevented_turn`, read by combat core + PendingAttackDamage) — a pure cost vs the goldfish | A: `sacout=` | IMPLEMENTED e419123be |
| Thraben Inspector | 2 | `etb_investigate 1` -> `CreateClueTokens` in `FireOwnEtbTriggers` | A: Clue SacDraw | IMPLEMENTED f402e5aeb |
| Curious Farm Animals | 2 | `dies_trigger_self_gain 3` + `{2}` self-only sac outlet + optional destroy artifact/enchantment (zero-target dominance) | A: `sacout=`, `dig` (may destroy) | IMPLEMENTED e419123be |
| Enduring Innocence | 2 + small 3 | own-creature-enter draw, `creature_enters_max_power 2`, once each turn per copy; dies-if-creature -> returns as noncreature Enchantment | none (mandatory) | IMPLEMENTED 539fe952d |

Drafts (full): `logs/martyr-lifegain/drafts/0{1..4}-*.md` (gitignored scratch; the table above is the durable record).

Integrator A notes (2026-11-10): 15 unit cases (`test/unit/test_martyr_lifegain_cards.cpp`) + 7
`test/scenarios/martyr_*.json` PASS (scenario suite 452/452); smoke 182/183, the one mismatch
(`extraturns2hg_smoke_d5_s1001`) is PRE-EXISTING (GT rewritten by f7cafbc15 with no source change; our binary
reproduces the pre-f7cafbc15 value) — not ours, nothing accepted. Deviations: Angels signature now also requires
`wish_from_sideboard` (Hawk's `wish_requires_name` would otherwise route the deck to AngelsProvider; Legion Angel
has both, Angels unchanged); Kami flag is a bool reset at both untap sites (`combat_damage_prevented_this_turn`),
folded into every key; Farm Animals destroy resolves after its own dies-gain; a destroyed token now ceases to
exist (human-play only); self-sac gate counts the source's own dies payload. Deck routes to **EquipmentProvider**
today (with or without the five cards) — provider stage. `test_pirates_provider.cpp` routing table needs the
new deck pinned.
PROVISIONAL deferrals: end-step Clue not crackable that turn; Hawk takes full count; Kami / Farm Animals
instant-speed activation not modelled.

### Open questions raised by research (provider stage; USER-owned ones marked)
- **[USER] Second main.** Kami / Farm Animals / Martyr+Lurrus want "attack, then sacrifice"; Speaker of
  the Heavens can cross life 27 via lifelink combat damage. Card integration is first-main only.
- **[USER] Cast order.** Enduring Innocence's draw is lost if a power<=2 creature is cast before it the same turn.
- Jitte -1/-1 on our own creature is pruned in autonomous search; here it can pay (Farm Animals 3 life, Grace/Lurrus re-buy).
- "Cast Abiding Grace, then sacrifice in the same plan" may need a re-collect breakpoint.
- Clue created at END STEP (Grace returning Inspector) cannot be cracked that turn (no instant-speed window).

Lurrus integrator B notes (2026-11-10, handoff `logs/martyr-lifegain/lurrus-handoff.md`): unit 2709/2709 (11 new in
`test/unit/test_lurrus.cpp`, both worlds), scenarios 456/456 (4 new `martyr_lurrus_*`, each a T4 win), viewer driven via
`--claude-play` seed 54 (`gycast_gy` listed, `gycast=` accepted). Smoke 183/183, play-changed=0. Deviation: the re-solve
point is site 9 (an activation key per castable name), not a new site 10. Executor fix: its site-9 continuation now
realises Lurrus casts (without it the rollout's T4 loop realised T5 even at unlimited budget). PRE-EXISTING gap left
byte-identical: a RETRACE cast inside a site-9 continuation (and any gy cast in a site-7 Pod continuation) is applied by
the rollout but not the executor. `test/check_plan_sig_lockstep.py` reports `fast-only ['#BB']` -- pre-existing
(a78921c85, Extra Turns), not ours. PROVISIONAL: Companion inert; {X} permanents never offered by Lurrus (none in list).
Untested: Hawk cast from the graveyard (ETB search) -- shared cast path, no scenario of its own.

## Stage 2d-bis / 3 (2026-11-10)
- `audit_card_costs.py`: 565/565 costs match Scryfall. `audit_card_fields.py`: no hard findings. The new cards' oracle
  advisories are the long bracket notes after the verbatim text (611 cards trip the same advisory).
- Coverage re-run: 18/18 full, missing [].

## Provider

Stage 4a (6bbc57216): `MartyrLifegainProvider`, an EMPTY derivation of `EquipmentProvider` -- the provider Umezawa's
Jitte routed the deck to -- so play-neutral by construction. Routed ABOVE `equipment` on four new gated params from four
cards (Lurrus, Kami, Farm Animals, Enduring Innocence). It INHERITS KittyEquipment's judgement hooks: removal NeverCast
(Swords / Unexpectedly Absent never cast -- the USER's 2026-10-04 goldfish ruling for Kitty), land-first / equip-last
activation order, Kitty's cleanup-discard buckets (MTG_KE_BUCKET_DISCARD), the equip-route trial, fold opt-ins. Each is
a provider-stage question for this deck (discard buckets are USER-owned, 5i).

## Verification

### Stage 4 (2026-11-10)
- Profile: card-scores-only, 17 cards, no hand-score gate, default land window (1-5). Cost diagnostic
  NO_COST_INTERACTIONS. Discard analysis STATUS_QUO_OK (50 sheds in 400 d3 games, 98% multi-optimal; status quo
  = the INHERITED KittyEquipment bucket rule, mean regret 0.06 vs band 0.02 / derived order 0.04).

### 4-bis / 5j regression tiers
- Cost gate (`suite_gate.py --cost`, pooled with Hinata): PASS, martyr 30,515 ms/game vs hinata 44,228 = 0.69x.
  In UNITS martyr's d5 b20 games are ~1.75x Hinata's (113K vs 65K units/game); the box is a 12-thread laptop
  (i5-1235U), so ms are contention-noisy. Rows sized at HINATA's shape (smoke d5 100 + 2hg 25; regression d5
  2x150 + 2hg 75; overnight ENTRY d3 125 + d5 4x300 + 2hg 4x75), plus `martyr2hg` (Serra's 30 and Speaker's
  start+7 read the 2HG life total).
- Backstop (budget 0, abandon 2M units): screened 16 five-game blocks 30001..30301 at d3+d5. d3 all clean; d5
  ABANDONED 7 of 80 games (blocks 30041/30101/30121/30161/30241/30281) -- a degenerate UNBUDGETED d5 tail on
  this deck (~11 s/game single-thread mean, 25 s worst finished). Chosen: regression 30201, overnight 30301
  (5 games each, every game finished at both depths).
- Smoke rows ran once (4 NEW keys) on the pre-M2 binary; the chain was stopped before regression because the
  Speaker second main (below) changes play. Tiers re-run after it.

## Claude-play sweep
- commit: `d6b1c6b12` (binary of 6bbc57216 + ledger)
- seeds: 61001 games: 16 (gi 0-15, Sonnet players)
- flags: 0 unresolved
- Results: 11 games matched the search's win turn, 4 were one turn SLOWER (weak signal), and 1 was FASTER
  (gi14: Claude T5 vs search T6).
- Dismissed / fixed: the `sac_tutor` note said "onto the battlefield" for Squadron Hawk / Ranger of Eos (cards
  correctly go to HAND) -> note now keyed on the "(put in HAND)" source (src/main.cpp). gi3's Abiding Grace
  gained 1 with no decision: verified the graveyard was empty at that end step (gain is the only mode).
- Exercised: Martyr x_value reveal, Ranger-Captain tutor_target variants, Hawk + Ranger of Eos sac_tutor,
  Basilica bounce, Serra 6/6 at >= 30, Speaker's token at >= 27 only, Innocence once-per-copy draws, Jitte modes,
  `gycast_gy` / Lurrus "(from graveyard, Lurrus)" plan, mulligan + bottom, discard. NOT exercised: Abiding Grace
  return mode, Farm Animals' optional destroy, an actual Lurrus gy cast in a committed line.
- gi14 ROOT CAUSES (search miss, structural -- unlimited budget still T6):
  1. Speaker's token needed life >= 27, reached only via LIFELINK in T4 combat -> the post-combat activation was
     unfindable (no second main). Fixed provisionally: MartyrLifegainProvider now opens ColossusHammer's NARROW
     Speaker second main (MTG_SPEAKER_M2, shared body, default ON, PROVISIONAL -- main 1 vs main 2 is the USER's).
  2. Even from the human's own T5 board the search does not find "Jitte: gain 2 life x2 -> Serra 6/6 -> 13
     exactly lethal" (budget 0 -> T6). Under investigation.

## Checkpoint (2026-11-12 ~22:35 UTC) -- RESUME HERE

FARM LANDING REBASED twice (now 0668b9388 code + 83cd80de3 GT, on origin 714f955bc). After the first rebase (onto
4f3522ad4) martyr smoke 4/4 + regression 7/7 PASS byte-identical (logs/martyr-lifegain/farm-rebase-{smoke,reg}.log).
Origin then moved again (hydras P13 engine levers SUBRED / X-waste / walk pre-test, Extra Turns per-game reset):
rebuild + martyr smoke + regression re-running (farm-rebase2-*.log) before the push.
5c2 TIE-BREAK DONE -- VERDICT KEEP (default ON). Extension (blocks s2200000.., 12000 paired): net -19, 2 worse / 18
better (half A -5, half B -14), script verdict KEEP. POOLED with the first run (net -12, 3/15): 24000 paired, 38
changed, net -31, 5 worse / 33 better; all four halves negative. Nothing to change (GradesNoWinLeaf stays true).
NEXT: push -> 5h audit_viewer_decisions -> verify_deck --write-ledger -> Stage 6 report -> mullgen.sh run (alone)
-> valueleaf.sh run.

### Checkpoint (2026-11-12 ~18:45 UTC, history)

FARM AMENDMENT LANDED, DEFAULT ON (a80914138 + GT 045348604): Ranger-Captain / Ranger of Eos admit Curious Farm
Animals (after Martyr) only while life < the top own threshold AND the hand holds no white card (Martyr's reveal
dry). The first DRY build was inert (CardHasColorNamed on a hand Card reads no mask; fixed via
CountHandCardsOfColor). Held-out ab7 (d5 b20, std 79001 x600, 2HG 80001 x200): literal 5.3167 / 5.6250; broad
5.3183 (+1 gi233, units +4.8%); DRY 5.3167 / 5.6250 (0 slower / 0 faster, units +0.04%). Regression s2002 gi0
T6 -> T5 at d3 b10 and d5 b20; smoke d0 gi742 line change only. Smoke + regression GT accepted (martyr-scoped);
overnight keys for d3/d5 may now be stale -> left to the periodic overnight run (USER: no re-runs). Decision
surfaced to the USER (they suggested "prune it in more cases"; =0 restores the literal rule).
5c2 EXTENSION RUNNING (PGO of the pre-Farm tree, logs/martyr-lifegain/tiebreak-b2.log, task bat0jrqq3).
NEXT: push -> on the extension's verdict pool with the first run -> 5h audit_viewer_decisions -> verify_deck
--write-ledger -> Stage 6 report -> mullgen.sh run (alone) -> valueleaf.sh run.

### Checkpoint (2026-11-12 ~11:00 UTC, history)

REBASED onto origin cc24d1362 (upstream mana 5d/5f cuts + hydras), unpushed. Release rebuilt; post-rebase smoke
ALL PASS, 195/195 unchanged, 0 play changes (logs/martyr-lifegain/postrebase-smoke.log). The pre-rebase martyr
overnight keys were DROPPED in the rebase (upstream src moved play); re-running overnight --deck=martyr,martyr2hg
on the rebased binary (logs/martyr-lifegain/postrebase-overnight.log) to re-accept.
5c2 TIE-BREAK (blocks 1..12, s1000000.., PGO of the pre-rebase tree): NO SIGN, 18 changed of 12000 paired, net -12
(3 worse / 15 better; half A -3, half B -9 -- both say it HELPS). Script asks for ~24 blocks: extension with 12 FRESH
blocks (`--seed-base 2200000 --blocks 12 --tag martyr-b2`) pooled with the first run, AFTER the overnight accept.
Generators wait on it (an OPT OUT would change play).
USER 2026-11-12 (verbatim): "Yes, the 1-drop tutor thing seems fine." (APPROVES: Martyr ranked second, "need the
lifegain" = life < max(30, start+7), Ranger of Eos pair = distinct names first then library order.) "That's
unfortunate about Farm animals. Maybe we should prune it in more cases?" "Doing the overnight once makes sense, but
you shouldn't frequently run it." (-> NO further overnight runs for martyr.) "That's fine. As long as it is in the
plan. Let's proceed until actual planned changes have been exhausted first." (-> run the 5c2 extension before
mullgen.) "I want you to continue on these overnight."
FARM NARROWING (agent's reading of "prune it in more cases"): gi233's broad-amendment loss is search DILUTION -- Farm
admitted at T3 (life 20, Martyr on board, hand Captain/Lurrus/Jitte) is never fetched, but the extra cast-axis
candidate makes d5 b20 pick Jitte over Captain (T5 -> T6). New sub-flag MTG_MARTYR_TUTOR_FARM_DRY (heurarm
MARTYR_TUTOR_FARM_DRY, default ON, only under TUTOR_FARM): Farm admitted only while the hand holds NO white card
(Martyr's reveal colour) -- gi0's fetch hand is two Plains (admitted), gi233's is not. In worktree
.claude/worktrees/martyr-split, UNCOMMITTED (DecisionProviders.cpp, HeuristicArm.h, test_martyr_tutor_prune.cpp +1
case); building -> build/scratch/martyr-split/build3.log. A/B READY: build/scratch/martyr/ab7/run.sh (3 arms lit /
broad / dry on ab6's held-out seeds std 79001x600 + 2HG 80001x200, gi0 d8 b0, gi233 d5 b20 + d8 b0); launch it
ALONE after the overnight accept: `nohup bash build/scratch/martyr/ab7/run.sh &`, DONE marker ab7/ab.DONE.
Status timer: cron 0280a824 (session-only, :17 every 2 h; old 7869914f deleted).
NEXT: overnight accept (`regression.sh --overnight --deck=martyr,martyr2hg --accept`) + commit ledger -> push ->
ab7 A/B -> report to USER (adoption theirs) -> 5c2 extension (`--seed-base 2200000 --blocks 12 --tag martyr-b2`)
with 5h audit_viewer_decisions + verify_deck --write-ledger alongside -> Stage 6 report -> mullgen.sh run (alone)
-> valueleaf.sh run.

### Checkpoint (2026-11-12 ~03:10 UTC, history)

PUSHED 0425e34e4 (01:38 UTC; next shared-branch push no earlier than ~05:38 UTC). CI green (windows, ubuntu,
determinism parity). Contents: the 1-drop tutor prune (7d2bb4f37) + smoke GT (5be4bb944: 191 unchanged, martyr
4 faster / 0 slower) + regression GT (0425e34e4, deck-scoped accepted-with-regressions note). Ref bench 10/10 (5.2 = 5.2).
REGRESSION SLOWER GAMES: d3 s2002 gi28 recovers at d8 b0; d3+d5 s2002 gi0 (T5 -> T6) does NOT. Root cause: on T3
Ranger-Captain fetched Curious Farm Animals with the prune off; T4 cast {W} + sacrifice {2} gains 3 (its dies
trigger) -- with a hand of lands Martyr's reveal gains nothing -- and lifelink puts life at 28 over Speaker's 27
gate, Angel in main 2, win T5. The prune fetches Serra instead -> 25 life, Angel a turn later, T6.
AMENDMENT (USER decides): MTG_MARTYR_TUTOR_FARM -- admit Farm Animals while life < the top own threshold, ranked
after Martyr. On branch wip/martyr-tutor-farm (worktree a35bc76b5), NOT landed. Recovers gi0 (T5 at d8 b0). Held-out
A/B vs the literal rule (build/scratch/martyr/ab6, same seeds as ab5): std 5.3167 -> 5.3183 (1 slower gi233, recovers
at d8 b0; 0 faster; net +1), 2HG 5.6250 = 5.6250. A wash; the USER's literal rule stays the default.
NEXT: cost gate ALONE (logs/martyr-lifegain/costgate.log) -> overnight tier --deck=martyr,martyr2hg (first accept:
no martyr overnight keys yet) -> Stage 5 remainder -> mullgen.sh run -> valueleaf.sh run.

### Checkpoint (2026-11-11 ~23:15 UTC, history)

1-DROP TUTOR PRUNE (USER request below) BUILT + MEASURED + LANDED LOCALLY: 7d2bb4f37 (cherry-pick of worktree
d34e16db7) on bf1784395, not pushed (4-hour window opens ~01:12 UTC). MTG_MARTYR_TUTOR_PRUNE (heurarm
MARTYR_TUTOR_PRUNE, default ON). Rule as built: Ranger-Captain's searched MV<=1 fetch offers Serra Ascendant, Martyr
of Sands (only with no Martyr in hand/battlefield AND life below the highest own life threshold: 30 std, 37 2HG),
Speaker of the Heavens -- in that order (Martyr second, an agent choice: surfaced); generic list when none is
admissible. Ranger of Eos's pair (a RESOLUTION-time list, TutorHandPutList -- no cast axis, existing architecture)
ranks the same names, distinct names first, then fills in library order. Unit tests test_martyr_tutor_prune.cpp (5).
HELD-OUT A/B (build/scratch/martyr/ab5, d5 b20, seeds std 79001 x600, 2HG 80001 x200, p0 = prune off vs p1 = on):
  std 5.3350 -> 5.3167 (11 faster, 0 slower, net -11), wall -22%, units -20%
  2HG 5.6400 -> 5.6250 (3 faster, 0 slower, net -3), wall -29%, units -22%
  -> clears the bar with no slower game to recover.
The rb9 regression run was CONTAMINATED (my own `suite_gate.py --cost` probe ran a tier in the same test/logs/regression
dir and overwrote its manifest; killed) -- NOT accepted. Running now: scenarios -> full smoke -> clean
`--regression --deck=martyr,martyr2hg` (logs/martyr-lifegain/tp-*.log, DONE marker tp-chain.DONE). Then accept
smoke + regression, ref_bench, cost gate ALONE, overnight tier, Stage 5 remainder, mullgen, valueleaf.
CI on 6b3dea976: green (windows, ubuntu, determinism parity).

### Checkpoint (2026-11-11 ~21:30 UTC, history)

PUSHED 6b3dea976 (21:12 UTC; next shared-branch push no earlier than ~01:12 UTC, 4-hour rule): candidate b1 landed
(all new defaults ON, amended split OFF) + martyr smoke GT (d0 6.1050, d3 5.2000, d5 5.2700; 2hg 5.4800 unchanged).
Full smoke on the rebased tree: 192 unchanged, only martyr d0/d3/d5 moved (0 slower / 2 faster). Scenarios 492/492.
CI watch: logs/martyr-lifegain/ci-6b3dea9.log. Branch martyr-pre-rebase-1111 = the pre-rebase tip (backup only).
Worktree .claude/worktrees/martyr-split reset to 6b3dea976 (clean), ready for the next change.

RUNNING (my own): regression tier `--regression --deck=martyr,martyr2hg` -> logs/martyr-lifegain/rb9-regression.log.
Games done: d3/d5 s30201 PASS; d0 6.1420->6.1170, d3 s2002 5.5200 (digest only), d5 s2002 5.5800->5.5600,
d5 s3003 5.4267->5.4133, 2hg 5.7600 (digest only) -- aggregates equal or faster; check per-game slower list,
then accept with --deck=martyr,martyr2hg. Also stray `suite_gate.py --cost "decks/Martyr Lifegain"` (was idle).

NEW USER REQUESTS (2026-11-11 ~21:20 UTC, verbatim):
  * "Please queue up the mulligan profile and value-leaf if our performance is good enough to meet the bar."
  * "Prior to that we may want to add a heuristic prune for the 1-drop search to get only Martyr (if we don't have
    one and need the lifegain), Serra Ascendant or Speaker of the Heavens. Usually it will be Serra Ascendant I
    would imagine, so that should rank first."
PLAN: MartyrLifegainProvider::TutorCandidates override for the creature-mv<=1 tutors (Ranger-Captain of Eos ETB;
Ranger of Eos "up to two"): admit Serra Ascendant (rank 1), Speaker of the Heavens, Martyr of Sands only when no
Martyr in hand/battlefield AND life < the highest unmet own gate (max(30, start+7)); fall back to the generic list
only if none of the three is in the library (surface that interpretation to the USER). Flag MTG_MARTYR_TUTOR_PRUNE
(heurarm slot), scenarios, then a held-out A/B vs 6b3dea976 (ab4 harness build/scratch/martyr/ab4/mk.py, std 600 +
2HG 200 on fresh seeds) and d8/b0 recovery of every slower game. Land it if it clears the bar (USER asked for it),
accept smoke + regression, then overnight tier for martyr, remaining Stage 5 items (5c2, 5a, 5b, 5h, 5i,
verify_deck --write-ledger), cost gate re-check (suite_gate --cost; was 0.69x Hinata), THEN `mullgen.sh run`
(alone), THEN `valueleaf.sh run` (alone). 2-hourly session cron + PID waiters on each long stage.

### Earlier checkpoint (2026-11-11 ~14:30 UTC, history)

PUSHED 7e88d9fd5 (phase-1-2-deck-analyzer): martyr smoke GT accepted (d0 6.1300, d3 5.2200, d5 5.2800, 2hg 5.5200),
colossus unchanged; CI watch -> logs/martyr-lifegain/ci-7e88d9f.log. Post-rebase rebuild + filtered smoke RUNNING
(logs/martyr-lifegain/rb6-*; expect all 8 keys PASS unchanged -- origin's rebase brought only Hydras/AA provider code).

WORKTREE .claude/worktrees/martyr-split, branch martyr-split (LOCAL, not rebased onto 7e88d9fd5 yet):
4825e1092 prune + split + loop route; 7d027cda2 amended split + per-card prune flags; 624cd16f8 the 2026-11-11 USER
rulings: MTG_MARTYR_JITTE_GAIN default ON + MTG_MARTYR_JITTE_GAIN_GATE (offered only when the counters can carry life
across Serra 30 / Speaker start+7), MTG_MARTYR_PRUNE_FARM_AT30 (Farm sac pruned only at life >= 30),
MTG_MARTYR_SPEAKER_M2_JITTE (Speaker's narrow M2 also opens for a Jitte gain that reaches the gate), MTG_BP_SPEAKER_GATE
(engine: kActTapTokenLive key + site-9 marking -> a mid-main gain that crosses Speaker's gate re-plans, so
"Martyr sac -> Angel" is findable in ONE main). mtg-test 2713/2713, scenarios 489/489, colossus smoke unchanged.
Handoffs: logs/martyr-lifegain/breakpoint-handoff.md, split-rootcause-handoff.md; write-up split-rootcause.md (sec 1-5).

ab3 RESULTS (std 600 @75001, 2HG 200 @76001, d5 b20; mean turn, nonwin=9):
a1 shipped-prune 5.3850/5.6450; a2 USER split 5.4467/5.6950 (std 14f/51s, 2HG 1f/11s vs a1); aA amended split
5.3800/5.6500 (15f/12s, 2f/3s); k Kami-only 5.3783/5.6450; n no prune 5.3800/5.6450 (k vs n 2f/1s std gi582, 0/0 2HG;
a1 vs n 2f/5s std -> Farm prune is the loss); aAk 5.3733/5.6500.

RUNNING: final A/B build/scratch/martyr/ab4 (binary copy of 624cd16f8; std 600 @77001, 2HG 200 @78001; mk.py):
b0 all-new-off (committed play) | b1 CANDIDATE (loop route + Kami prune + Farm prune at 30+ + gated Jitte gain + Speaker
M2 Jitte + BP Speaker gate) | b2 b1+amended split | b3 b1 ungated Jitte | b4 b1 no BP gate | b5 b1 no Jitte gain |
b6 b1 Farm never pruned. DONE marker ab4/ab.DONE (waiter armed). Then: d8/b0 recovery of every b1-vs-b0 slower game
(pooled), land b1's defaults (rebase martyr-split onto the branch; split stays default OFF = USER's call), rebuild,
filtered smoke accept, ref_bench must match all 10, push.

ab4 RESULTS (binary = 624cd16f8 build; std 600 @77001, 2HG 200 @78001, d5 b20; mean turn std / 2HG):
b0 committed 5.4033 / 5.5100 | b1 CANDIDATE 5.3950 / 5.5050 | b2 b1+amended split 5.4033 / 5.4800 |
b3 Jitte ungated 5.3983 / 5.5100 | b4 no BP gate 5.3967 / 5.5050 | b5 no Jitte gain 5.3933 / 5.5050 | b6 Farm never
pruned 5.3950 (byte-identical to b1) / 5.5100.
Per game: b1 vs b0 std 6f/1s (net -5), 2HG 1f/0s; the one slower game (std gi220, a Jitte-gain game: b5 vs b1 1f/0s)
RECOVERS at d8/b0 (5 vs 5) -> b1 CLEARS THE BAR, LANDING. Gate matters: ungated b3 vs b1 std 0f/2s, 2HG 0f/1s.
BP gate: b4 vs b1 std 0f/1s. Farm at 30+: b6 vs b1 2HG 0f/1s (gi3).
Amended split b2 vs b1: std 9f/14s (net +5), 2HG 7f/2s (net -5); d8/b0 recovery of the 16 slower: 12 recover,
4 do NOT (std gi436, gi458, gi462; 2HG gi74) -> fails the bar as is; USER decision, stays default OFF.
Logs: build/scratch/martyr/ab4/{run,rec}.

Play viewer running from the primary (pid logs/martyr-lifegain/viewer.pid) -- do not kill.
NOT running: 5c2 tie-break (rerun alone later).
MACHINE: 12 threads / 10 GB. Builds only with MTG_BUILD_JOBS=4 and >= 5 GB free; never compile beside two batches.

NEXT after landing: regression + overnight tiers for martyr (accept --deck=martyr,martyr2hg) -> 5c2, 5a/5b/5h/5i,
verify_deck.py --write-ledger -> Stage 6 report (USER: amended split, Ranger-Captain, Farm prune's literal 30 in 2HG
(start 30, Speaker gate 37), Jitte one-gain-per-plan gap, Innocence order, discard buckets, deferrals) -> THEN the
mulligan profile (mullgen.sh run, alone on the box). Cron 7869914f (session-only) stays until the end.

## Stage 5b -- multi-depth sanity (2026-11-12)

Paired games (same seeds, d3 vs d5): smoke s1001 50 games d3 5.18 = d5 5.18 (0 slower / 0 faster); regression
s2002 50 games d3 5.56 vs d5 5.52 (d5 faster on gi4, gi28; never slower). Monotone. Overnight: d0 ~6.09, d3 5.38,
d5 5.34-5.42 (std); 2HG d5 5.43-5.61. Plausible for a T5 aggro-lifegain clock (10/10 references: human 5.2 = search 5.2).

## Stage 5i -- discard BUCKET proposal (AGENT'S BEST SHOT; USER-owned, NOT implemented)

The deck currently uses the generic max-MV fallback. Proposed buckets (cleanup discard to 7; return ONE index):
1. MANA -- lands, net of board: keep lands while board lands + kept lands < 4 (curve top: Ranger of Eos {3}{W},
   Unexpectedly Absent X); at most 1 beyond that while board lands < 6. Orzhov Basilica counts as a land drop but sheds
   before a Plains once board lands >= 3 (enters tapped, bounces). Lands are COLOURLESS: they never feed Martyr's reveal.
2. LIFE ENGINE -- Martyr of Sands: keep 1 net of board (in hand or on battlefield); a second copy is overflow.
3. THREATS (catch-all, value order, highest kept first): Serra Ascendant > Speaker of the Heavens > Ranger of Eos >
   Ranger-Captain of Eos > Lurrus > Enduring Innocence > Umezawa's Jitte > Abiding Grace > Squadron Hawk >
   Thraben Inspector > Mother of Runes > Curious Farm Animals > Kami of False Hope.
4. DEAD vs the goldfish: Unexpectedly Absent, then Swords to Plowshares (Swords on an own creature is a life gain,
   so it sheds after Absent).
Shed order = overflow first, then lowest priority: extra lands beyond quota -> Unexpectedly Absent -> Swords ->
Kami -> Farm -> Mother -> Inspector -> extra Hawks -> ... STATE PROMOTION (doubt flagged): while a Martyr is on the
battlefield or kept, every white nonland card is reveal fuel (3 life each), so overflow LANDS shed before any white
spell, including Absent/Swords. Not measured: discard to 7 is rare in this curve (Hawk / Ranger / Clue / Innocence
draws are the sources); measure frequency + an A/B once the USER approves the shape.

## USER rulings (2026-11-10, verbatim)

1. *"The Narrow speaker version is fine for the most part. We can heuristically prune the kami and farm animals
   sacrifice abilities. Only Martyr is a bit questionable and we might want to test that once we are done. The
   tricky part is that you want to activate that early to gain more life and doing it before attacking is important
   to make Serra Ascendant larger, so most likely main 2 isn't crucial there."*
2. *"However, I am wondering whether perhaps we should move almost everything to main 2 instead. So for example, we
   could have only have Squadron Hawk and Ranger of Eos, (Cards that add more white in hand) in main 1. Martyr and
   Martyr sacrifice would go in main 1 if we need the lifegain to pump Serra and the beginning of Main 2 otherwise.
   The martyrs would be played and activations would happen immediately after in a loop if you had multiples.
   (because playing them all first is ineffecient)"*
3. *"I geuss ideally we would also split Squadron Hawk with targets which should be main 1 vs Squadron Hawk without
   targets which should be main 2."*

### USER rulings (2026-11-11, verbatim)

4. *"Jitte-gain makes sense if it can hit breakpoints."* -- read as: Umezawa's Jitte's gain-2 mode is ON for this deck
   (relaxing the 2026-08-14 "+2/+2 only" doctrine here), offered while the counters' gain can carry life across a
   threshold an own card reads (Serra Ascendant 30, Speaker of the Heavens start+7); and Speaker's gate becomes a
   mid-main BREAKPOINT so a gain re-opens the menu for the Angel. Both readings are being built (agent brief 11-11).
5. *"Oh right. I forgot there was a death trigger on the animals. We can leave that for now, as long as we are below
   30 life."* -- Curious Farm Animals' sacrifice stays on the menu below 30 life and is pruned at 30+. Kami's prune
   is unchanged (USER ruling 1).

Reading taken (surfaced to the user): Speaker narrow M2 APPROVED. Kami of False Hope / Curious Farm Animals sac
abilities PRUNED from the autonomous menu (both mains). The "almost everything to main 2" split is an EXPERIMENT to
build and A/B (Main 1 = Hawk/Ranger while they have a fetch target -- the same "adds white cards to hand" reading
applied to Ranger of Eos; Martyr cast+sac in main 1 only when the gain pumps an attacking Serra; everything else
main 2; equips/Jitte stay main 1 since they feed combat), then report.

Umezawa's Jitte gain mode (Stage 5d gi14 root cause): opening it relaxes the 2026-08-14 USER "+2/+2 only"
doctrine, so it ships as `MTG_MARTYR_JITTE_GAIN` DEFAULT OFF pending the user, with an A/B.

## Provisional decisions awaiting the user

(none yet)

## Approved deferrals

(none)
