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

## Checkpoint (2026-11-10 ~21:00 UTC) -- RESUME HERE

Pushed: through d6b1c6b12 (Lurrus 46467b0e5, own provider 6bbc57216); CI green on 42843623386 (both OSes).
UNCOMMITTED in the primary checkout (orchestrator's, keep): `src/ai/DecisionProviders.{h,cpp}` + `src/ai/HeuristicArm.h`
(MartyrLifegainProvider opens Speaker's NARROW second main, shared body with ColossusHammer -- SpeakerSecondMainLive /
SpeakerSecondMainAdmits; MTG_SPEAKER_M2 default ON, PROVISIONAL); `src/main.cpp` (sac_tutor note for "(put in HAND)");
`test/regression_cases.sh` (martyr + martyr2hg rows, all three tiers + backstop 30201/30301); `test/suite_cost.json`
(martyr entry SCALED to hinata's committed number -- hinata's own entry deliberately NOT updated: this box inflates ms
~9x); `decks/Martyr Lifegain/Martyr Lifegain.profile.json` (Stage 4 profile, untracked). User's own edits
`.vscode/settings.json`, `test/ref_bench.json`: never touch.
Box: 12-thread i5-1235U laptop, 10 GB (not the 32-CPU box) -- use --threads 12.

Speaker M2 A/B (held-out, one pooled batch, `build/scratch/martyr/m2ab/`): std 600 games d5 b20 seed 71001:
5.5400 -> 5.4083 (81 faster / 2 slower; both slower, gi174/gi417, recover to T5 at b200 and b0); 2HG 200 games
seed 72001: 5.7300 -> 5.6200 (22 faster / 0 slower). Kept ON (provisional) -> REPORT to the USER.

RUNNING: (1) Opus agent root-causing the gi14 Jitte miss (from the human's T5 board, "Jitte gain 2 life x2 -> Serra
6/6 -> 13 exactly lethal" is unfindable at budget 0); handoff would be `logs/martyr-lifegain/jitte-miss-handoff.md`.
It may rebuild build/Release with temporary prints -- after it finishes, verify `git diff` has no instrumentation and
REBUILD clean. (2) `leaf_tiebreak_check.py` (5c2) -> `logs/martyr-lifegain/tiebreak.log`.
Smoke tier for martyr ran once on the pre-M2 binary (not accepted); tier chain stopped (play changed).

NEXT: act on the Jitte finding (fix if a sound searched option/breakpoint; scenario test) -> rebuild -> unit tests ->
full smoke (play-changed must be ColossusHammer-neutral: shared body) -> commit src + rows -> run
`logs/martyr-lifegain/tiers.sh` (smoke, regression, overnight for --deck=martyr,martyr2hg) -> inspect -> accept each
tier with the SAME --deck -> `python3 test/check_gt_logs.py` -> commit GT + push + watch CI. Then 5a nonconv/fd-diverge,
5b multi-depth, 5h `audit_viewer_decisions.py`, 5i discard bucket PROPOSAL (inherited Kitty buckets today),
`verify_deck.py --write-ledger`, Stage 6 report (repeat USER questions: second main [Speaker narrow M2 kept ON; wider
attack-then-sac main 2]; Innocence cast order; discard buckets; provisional deferrals: Companion inert, Hawk full
count, end-step Clue, Kami/Farm Animals instant speed, {X} not Lurrus-castable). CronDelete 7869914f at the end.

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
