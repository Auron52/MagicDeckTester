# Analysis ledger -- Elves

Deck: `decks/Elves/Elves.cod` (60 cards, mono-G). Started 2026-11-26.
Work tree: `.claude/worktrees/elves` on branch `wip/elves-analysis` (base a825e81f4).

Plan of the deck: elf mana (Llanowar / Mystic / Priest of Titania / Elvish Archdruid / Marwyn)
into a wide board of stacked lords (Clancaller, Galadhrim Brigade + squad copies, Leaf-Crowned
Visionary, Joraga Warcaller multikicked), Concordant Crossroads for haste (dorks tap the turn they
land), Genesis Wave as the big-mana refill, Ezuri / Allosaurus Shepherd / Immaculate Magistrate as
the mana-sink finishers.

## Stage 1 -- coverage (2026-11-26)

Missing (8): Immaculate Magistrate, Joraga Warcaller, Allosaurus Shepherd, Taunting Elf,
Elvish Clancaller, Galadhrim Brigade, Leaf-Crowned Visionary, Marwyn, the Nurturer.
Already implemented (full): Llanowar Elves, Elvish Mystic, Elvish Archdruid, Priest of Titania,
Genesis Wave, Concordant Crossroads, Ezuri, Wirewood Lodge, Cavern of Souls, Forest.
No fetchlands (fetch-hold review not required). No sideboard.

Existing-card issues found at Stage 1:
* **Concordant Crossroads -- World rule (CR 704.5m) NOT modelled**, and its own bracket says the
  invariant "GOES LIVE AT TWO OR MORE COPIES". This list runs **2**. Tier 2/3 gap: implement the
  world SBA (all but the most recently entered World permanent go to the graveyard).
* Cavern of Souls' "choose a creature type" is simplified to "any creature"; exact here, since
  every creature in the list is an Elf.

| card | Scryfall cost | type | proposed tier | notes |
|---|---|---|---|---|
| Immaculate Magistrate | {3}{G} | Elf Shaman 2/2 | 2/3 | {T}: +1/+1 counter on TARGET creature for each Elf you control (target = viewer `target` decision) |
| Joraga Warcaller | {G} | Elf Warrior 1/1 | 3 | MULTIKICKER {1}{G}: enters with N +1/+1 counters; other Elf creatures +1/+1 per counter on it. Kick count = cast axis (like X) |
| Allosaurus Shepherd | {G} | Elf Shaman 1/1 | 2/3 | uncounterable x2 (inert: passive opp); {4}{G}{G}: Elf creatures you control base P/T 5/5 (layer 7b) + Dinosaur until EOT |
| Taunting Elf | {G} | Elf 0/1 | 1 | lure: "all creatures able to block it do so" -- inert (opponent never blocks); USER-APPROVED deferral |
| Elvish Clancaller | {G}{G} | Elf Druid 1/1 | 2/3 | lord (other Elves +1/+1); {4}{G}{G},{T}: search for a card NAMED Elvish Clancaller -> battlefield, shuffle |
| Galadhrim Brigade | {2}{G} | Elf Soldier 2/2 | 3 | SQUAD {1}{G}: N token copies on ETB, each a lord (other Elves +1/+1) |
| Leaf-Crowned Visionary | {G}{G} | Elf Druid 1/1 | 2/3 | lord; whenever you cast an Elf spell, MAY pay {G}: draw (optional payment = decision) |
| Marwyn, the Nurturer | {2}{G} | Legendary Elf Druid 1/1 | 2/3 | +1/+1 counter when another Elf you control enters; {T}: add {G} x Marwyn's power |

## Stage 2 -- implementation (2026-11-26)

Two integrators (handoff `logs/elves/stage2-handoff.md`). Commits on `wip/elves-analysis`: a096eceb4 (faces,
lords, Magistrate, Marwyn, Visionary, world rule), ba43b4223 (keyword tags, routing pin), and the second
integrator's Shepherd / tests / viewer / gates commit (see `git log`).

| card | tier | template + params | engine | viewer |
|---|---|---|---|---|
| Joraga Warcaller | 3 | vanilla_creature; `multikicker_cost {1}{G}`, `multikick_enter_counters`, lord `power/tough_bonus 1` + `lord_per_own_plus_counter`, `lord_excludes_self` | the DB synthesizes "<name> (Kicked xK)" faces K = 1..16 (printed + K x {1}{G}, `kick_times`); `Action::kick_count` / `CastRiders::kick_count` swap the face in both cast sites (`KickedFaceFor`); K counters in the as-enters window of BOTH enter sites; `ComputeLordBonus` multiplies the lord bonus by the counters on the Warcaller, read live | A: K = main_phase plan variants + GUI K cycling |
| Galadhrim Brigade | 3 | lord_effect; `squad_cost {1}{G}` | the same faces; `ResolveSquadCopies` (both enter sites) makes K token copies of the printed card through the universal enter cascade, each a lord; a copy makes no copies (the squad payment is not copiable, CR 707.2) | A: K = plan variants |
| Immaculate Magistrate | 2 | vanilla_creature; `tap_target_pump_cost ""` ({T} only), `tap_target_counters_per_subtype Elf` | the TapTargetPump route with an N-counters payload (`PutPlusCounters`, N = Elves we control at resolution, itself included) | A: board-click `target` at activation |
| Allosaurus Shepherd | 2 | vanilla_creature; `team_base_pt_cost {4}{G}{G}`, `team_base_pt_power/tough 5`, `team_base_pt_subtype Elf` | NEW `PermAbilityMode::TeamBasePT` (no {T}, single emission), `ResolveTeamBasePT`: layer 7b set on every Elf creature we control at resolution, carried as an until-EOT delta (5 - base) in `temp_power/tough_bonus`, marked by the new `Permanent::temp_base_set` bit (reset at every cleanup site, folded wherever `unearthed` is) | A: main_phase activation (`act=`) |
| Elvish Clancaller | 2 | lord_effect; `tap_tutor_put_cost {4}{G}{G}`, `wish_requires_name Elvish Clancaller`, `tutor_shuffle_after` | the Seahunter TapTutorPut route with a name filter in `TapTutorPutLibraryCandidate` and `PerformTutorToBattlefield` | A: TapTutorPut plan variants |
| Leaf-Crowned Visionary | 2 | lord_effect; `cast_trigger_subtype Elf`, `cast_subtype_pay_draw_cost {G}` | Mihail's optional-payment trigger with a draw payload in `FireOnCastTriggers` (every cast site of both worlds), on the shared ordinal pin `Plan::mihail_pay_choice`; the plan post-pass fans the pay/decline variants (prefix fold under BpPutInHand) | A: `dig` yes/no (Mihail's) |
| Marwyn, the Nurturer | 2 | mana_dork; `mana_per_own_power`, `own_creature_enters_self_counters 1`, `enters_watch_subtypes [Elf]` | a scaled dork (`IsScaledManaDork` / `ScaledDorkCount` = its live `CombatPowerOf`); grouped in `ScanDorkGrowthGroups` (growth +1 per entering Elf) | none (mana payment exemption) |
| Taunting Elf | 1 | vanilla_creature | -- | none (lure inert, USER-APPROVED) |
| Concordant Crossroads | 2 (existing card) | unchanged | the WORLD RULE (CR 704.5m): `EnforceWorldRule` at the top of `EnforceLegendRule`; the enter-site gates take `Supertype::World` (EffectHandler + three TurnSolver sites); the stale "not modelled" bracket rewritten | none |

Keyword tags `Keyword::Multikicker` / `Keyword::Squad` are inert (the mechanics are param-modelled);
`KeywordFromString` throws on an unmapped Scryfall keyword, which is why they had to exist.

**Allosaurus Shepherd, the 7b check.** CR 613.4: 7a characteristic-defining, 7b effects that SET base P/T,
7c effects that MODIFY P/T, counters included (613.4c; the old 7d counters sublayer was merged into 7c), 7d
switches. So "each Elf creature you control has base P/T 5/5" replaces only the base, and every lord,
until-EOT pump and +1/+1 counter still adds on top, whatever their timestamps. Every P/T reader in the engine
sums printed + `temp_*_bonus` + counters + `ComputeLordBonus` + CDA (+ animation, Auras, Equipment), so adding
`5 - (printed + animation base + CDA)` to the temp bonus yields exactly 5 + every 7c term. Measured in
`test_elves.cpp`: a Llanowar Elves with a Clancaller, two counters and an earlier +3/+3 reads 11/11 after the
activation (5 + 1 + 2 + 3); a counter or a second lord added afterwards still adds; a second activation leaves
it at the same value and reaches only an Elf that entered since (CR 611.2c), and cleanup resets the bonus and
the bit. The activation is an `ActivatePermAbility`, which both worlds apply in the TRAILING pass after the
plan's casts (`ApplyPlanDirect`'s `apply_trailing_activations`; `AIEngine::TakeTurn`'s
`exec_trailing_activations`), so an Elf cast in the same plan is set too: verified from one enumerated
plan (cast Llanowar + activate) in the rollout and in the committed-plan executor, both 5/5. Site 9 (the
post-entry activation breakpoint) knows the mode (`CollectActivationKeys` row + `PostEntryActivationPending`),
so a Shepherd CAST this plan can activate in the same main.

PARTIAL / disclosed (Shepherd): both "can't be countered" clauses are inert (the opponent casts nothing);
"becomes a Dinosaur" is not applied (nothing reads the type; the Elf type is kept); a CDA value is
snapshotted at resolution rather than hidden for the turn (inert: no Elf in the list has a CDA).

Tests: `test/unit/test_elves.cpp`, 9 cases, each run from ONE enumerated plan in the rollout (`ApplyPlan`) and the
committed-plan executor (`AIEngine::TakeTurn`): Shepherd 7b semantics (lords / earlier pump / counters on top,
no stacking, a newcomer reached by a second activation) and same-plan Elf cast + cleanup; Warcaller K fan
{0,1,2}, K counters, per-counter lord read live; Brigade K fan + two token copies, each a lord; Magistrate N
counters (N = 3 Elves); Marwyn's counter on Marwyn and its live-power yield; Visionary pay / decline (the
shared trigger pinned both ways, a non-Elf spell does not trigger, both plan variants realise identically);
the world rule; Clancaller's name filter (no other find offered) and the put. Full suite 3957/3957.

Known gaps / items to raise:
* Multikick / squad faces stop at 16 kicks (Warcaller x16 = 33 mana). A model bound.
* Visionary unpinned = decline (Mihail's semantics): the horizon playout and the d0 runner never pay, so
  leaves undervalue Visionary. A Stage 5 quality item.
* Magistrate's AUTONOMOUS target set is the board the plan starts from: a creature cast earlier in the same
  plan cannot be the target in that main (the human picks on the live board, so the viewer is not affected).
  The same holds for Kessig Wolf Run and the Merfolk tap-target pumps. A completeness gap to raise with the
  USER: it is a missing line, not a pruned one.
* Visionary + Mihail in one list: the Visionary fan is skipped (one shared ordinal stream). No list runs both.
* World-rule simultaneous-entry tie not modelled (inert: Genesis Wave's put set is a choice).
* Search-cost watch for Stage 5: the multikick fan (up to 16 variants per Warcaller / Brigade in hand), the
  Visionary fan (1 + sum s_j per plan), Magistrate targets (identity-folded own creatures). The USER-directed
  scaling-sink restriction (ruling 2) is the ElvesProvider agent's, not done here; the deck still routes to
  SelesnyaLifegainProvider (pinned in `test_pirates_provider.cpp`).

Gates (second integrator, 2026-11-26):
* Coverage (`analyze_deck.py --coverage-only`): 18/18 `full`, `missing: []`; remaining bracket notes are the
  disclosed inert / model-bound items above, plus Ezuri's pre-existing regenerate / trample PROVISIONAL note and
  its PENDING main-phase pump (chunk C of alchemist-untap-loop.md), neither touched here.
* Cost audit (`audit_card_costs.py`): rc 0, 707 of 707 costed cards compared, all match Scryfall.
* Field audit: `--update` added the eight new Elves cards to `scryfall_reference.json` (committed); it exited 1
  because 240 ALREADY-snapshotted cards hit HTTP 429 on refresh (their existing entries are kept, so nothing
  is unverified for this deck). Offline `audit_card_fields.py`: rc 0, "All hard fields (cost, P/T, types,
  keywords) match the snapshot"; the Elves oracle_text lines are the bracket-note advisories only.
* Viewer audit (`audit_viewer_decisions.py --no-sweep`): rc 0, oracle cross-check clean, expected types
  dig / dragon / firebreathe / target.
* Units: 3957/3957 (3948 + the 9 Elves cases).
* Smoke (`MTG_PGO=0 bash test/regression.sh --smoke`, Release, d2cd4c39e): 243 passed, 0 failed, 0 new;
  scenarios 608/608; searched AND d0: slower 0, faster 0, play-changed 0. No existing deck's play moved. No GT
  accepted. (Elves is not yet a suite case -- Stage 4-bis.)

## Viewer classification (2c-ter)

Every interactive choice the new cards create, bucketed. No bucket-B type was needed; the one new GUI piece is
the kick-count cycling on the existing kicker toggle. Registry rows: `tools/play/DECISIONS.md` (five rows
tagged "Elves, 2026-11-26"); `scripts/audit_viewer_decisions.py` MANIFEST + board-activation MANIFEST + six
INERT_PARAMS entries (reasons in the script; per that script's self-guard the INERT classifications want the
USER's OK, collected, not blocking).

| card | choice | bucket | surface |
|---|---|---|---|
| Joraga Warcaller | kick count K (0..kmax) | A (+ GUI) | main_phase plan variants (`kick_count`); hand-thumb kicker badge cycles K (`LB.toggleKicker`, `[k/kmax kicks]`); `kicked=<name> xK`, step `+kickedx<K>`. Human play and `MTG_UNPRUNED=replicate` bypass `MultikickCounts` |
| Galadhrim Brigade | squad count K | A (+ GUI) | the same |
| Immaculate Magistrate | whether to activate; WHICH creature | A | `act=` activation; target = board-click `target` on the live board at activation (`ChooseTapTargetPumpTargetAtActivation`, both apply paths) |
| Allosaurus Shepherd | whether / when to activate | A | main_phase board activation (`act=Allosaurus Shepherd`, `act_label`) |
| Elvish Clancaller | whether to activate; find or fail to find | A | TapTutorPut plan variants (the named card + decline) |
| Leaf-Crowned Visionary | pay {G} to draw, per Elf cast | A | `dig` yes/no (Mihail's prompt), only when the {G} is payable |
| Marwyn | -- | none | its tap is the mana-payment exemption |
| Taunting Elf, Concordant Crossroads | -- | none | no choice (lure inert; world rule is mandatory) |

`audit_viewer_decisions.py decks/Elves/Elves.cod --no-sweep`: rc 0, no unclassified param, oracle cross-check
clean (the Clancaller "search" advisory closed by teaching the cross-check that `tap_tutor_put_cost` models a
search -- also true for Seahunter / Maze's End). The live 5h sweep is Stage 5.

## Stage 4a -- provider routing (2026-11-26)

Elves rode `SelesnyaLifegainProvider` (Genesis Wave's `reveal_x_put_permanents` sets the `selesnya` signature).
It now owns **`ElvesProvider : SelesnyaLifegainProvider`** (derived from what it routed to, so every inherited
judgement hook is byte-for-byte the same, INCLUDING SelesnyaLifegain's USER-settled cleanup-discard buckets --
an inherited narrowing settled for another deck, kept until this deck's own discard rule is settled).
Routed ABOVE `selesnya` in `DetectDecisionProvider` by the `elves` signature, params of seven different cards,
each carried by no other card in cards.json (checked 2026-11-26): Joraga Warcaller `lord_per_own_plus_counter`,
Immaculate Magistrate `tap_target_counters_per_subtype "Elf"`, Allosaurus Shepherd `team_base_pt_cost` (subtype
Elf), Leaf-Crowned Visionary `cast_subtype_pay_draw_cost`, Marwyn `mana_per_own_power`, and
`multikicker_cost` / `squad_cost` on an ELF card only (the bare mechanic params would capture any deck adding
Everflowing Chalice). Every other deck routes as before (`test_pirates_provider.cpp`'s every-deck table; the
Elves row now pins `Elves`).

Certificate: **NotAssessed**. What would need checking: combat is the only route to the opponent's life, but a
winless-this-turn bound must price Concordant Crossroads' same-turn haste, the live lord stack (Clancaller,
Visionary, squad-copy lords, Warcaller's per-counter lord), Shepherd's 5/5 base set, Ezuri's repeatable +3/+3 at
combat from leftover mana, Wirewood Lodge's untap and a Genesis Wave putting a whole hasted board.

`provider_audit.py`: Elves has no `.profile.json` yet, so the audit lists it as NOT AUDITED (routing read from
the unit table instead). `--check` FAILS on the base tree already, for a pre-existing reason this change does not
touch: the RED FLAG for Genesis Wave's `GenesisWavePut` decision (no deck policy) on Landfall and Selesnya
Lifegain (`docs/design/provider-heuristic-required.md`, "Owed" item 1). Elves holds Genesis Wave too, so once it
has a profile it will carry the same RED FLAG: ElvesProvider does NOT implement the put-subset policy (the
scaling rule below is about X, not about which revealed permanents are put). OWED.

## Provider heuristics

### The scaling-sink rule (USER-directed provider prune, 2026-11-26)

USER, verbatim: *"There are multiple scaling effects like Genesis Wave's X and Joraga Warcaller, but generally
you want to pick just one of those and scale it up fully, minus perhaps a few 1-mana producing attackers. So
there might be a couple different relevant options, (i.e. do I tap this Llanowar Elves to scale it further or
keep it as an attacker, which can either be searched or heuristically pruned depending on the situation) but we
shouldn't try every possibility."*

Where: `ElvesProvider::PlanListAdmitAt` -> `ElvesScalingAdmit` (DecisionProviders.cpp), a plan-list
restriction applied to every enumeration (root and continuation, main 1 and main 2) through the existing
`ApplyProviderPlanListFilter` host; the new `DecisionProvider::PlanListAdmitAt` hook only adds the
enumeration's phase (default forwards to `PlanListAdmit`, Maze's End byte-identical), and `PlanView` gained an
opaque `plan` pointer for the group key. Nothing in generic enumeration changed.

* **Sinks:** Genesis Wave's X; Joraga Warcaller's multikick count and Galadhrim Brigade's squad count
  (`Action::kick_count`); Ezuri's MAIN-PHASE team pump count (ActivatePump mode 2, emitted only with floating
  mana). Ezuri's ordinary pump is NOT a plan count: the combat converter (`ApplyFirebreathing`) spends leftover
  mana on it at combat (the standing exemption). Clancaller's and Shepherd's activations and Magistrate's {T} are
  single activations, not counts.
* **Candidate rule:** (1) a plan scaling two or more sinks (count > 0; Wave always counts) is removed -- no
  partial splits. (2) The remaining one-sink plans are grouped by everything except the counts (the whole
  `TurnSolver::Plan` with the sinks' counts, their cost/eval and the plan value blanked, compared with
  `Plan::operator==`); per group and sink card, the top count is the largest enumerated one -- every member is a
  payable enumerated plan, so the top is exactly "max affordable after the plan's other casts" (the
  `ReplicateTopCountOnly` shape). Kept: counts >= top - h. (3) Plans with no scaled sink, including a minimal
  Warcaller K = 0 / Brigade K = 0 cast beside another sink, are never touched; the idle pass is not in this list
  (the hosts append it).
* **Genesis Wave's X menu** is widened to every X (`ElvesProvider::XCandidates`, largest first) so "Wave for what
  the rest of the plan leaves" exists to be kept; before, the base gave only the whole-pool max, so no plan cast
  Wave beside anything else.
* **Hold-back rule:** k = our untapped mana_dork creatures with a static one-mana tap (Llanowar Elves, Elvish
  Mystic; not Priest / Archdruid / Marwyn) that can attack this turn (`CanAttackFull`: not summoning-sick, or
  hasted by Concordant Crossroads), capped at 2; h = ceil(k / mana per count step) (Wave 1, a kick or squad copy
  2, Ezuri's pump 5). **Pruned when attacking cannot matter:** k = 0 in the post-combat main, and with no such
  attacker on the board.
* **Lift switches:** `MTG_ELVES_SCALING_PRUNE=0` (heurarm `ELVES_SCALING_PRUNE`, per job; default ON) -> every
  count and split. `MTG_UNPRUNED`, and `MTG_UNPRUNE=replicate` (the gate the multikick fan already shares -- the
  64-gate mask is full) or `=xspell` (Wave's X), lift it. Human play never sees it (the host skips it under
  `HumanPlayActive()`), and the engine still offers the human every K and X.
* **Lines it can miss:** two sinks scaled together (Warcaller x2 + Brigade x1, Wave + a kicked Warcaller); a
  count below the band that frees mana for something that is not a plan action -- Visionary's {G} draws (paid at
  trigger time), Ezuri's combat pump from leftover mana (only the no-sink / K = 0 plan keeps "Ezuri fully
  scaled"), a post-combat activation; holding back a scaled dork (Priest / Archdruid / Marwyn) or a third 1-mana
  dork to attack; a smaller Wave chosen to keep the other Waves in the library. The hold-back variants only help
  if the mana payer then leaves the attacker untapped (they at least leave the mana for Ezuri's combat pump).
* **Measurement (2026-11-26, quick probe, NOT the adoption A/B):** ONE pooled `mtg --batch`, 30 blocks x 10 games
  per arm, arms innermost, seed 7007+off / game_index off, d5 b20, no profile (none exists yet). Manifest:
  `logs/elves/scaling_probe_manifest.py`.

  | arm | won | avg win turn | units/game | ms/game |
  |---|---|---|---|---|
  | rule ON (`MTG_ELVES_SCALING_PRUNE=1`) | 299/300 | 4.3311 | 16,756 | 715 |
  | rule OFF | 299/300 | 4.3311 | 17,677 | 720 |

  0 of 300 paired games moved; units x0.948. Census: 6,457,023 plans enumerated over 270,776 enumerations,
  256,084 dropped as two-sink splits and 581,873 as out-of-band counts (13.0% of plans). Play-neutral on this
  sample and removes work. The OFF arm is wider than the pre-provider play (every Wave X), so this is not a
  comparison against the old play. The definitive with/without A/B (larger, held-out) is Stage 5e/5f.
* **Signature key:** the autonomous plan-dedup signature did not key Genesis Wave's X, so the dedup kept ONE X per cast set (the first enumerated). ElvesProvider opts in to a `#WX` key (`DecisionProvider::PlanSignatureKeysWaveX`, `t_sig_wave_x` in TurnSolver.cpp), so the rule sees every X; every other deck's signature is byte-identical. OWED, for the USER: the same collapse is still live for Landfall and Selesnya Lifegain (their XCandidates offer the un-grown and the grown max, and first-enumerated wins, i.e. the un-grown one), a generic limiter by the core invariant; fixing it moves their play.
* The definitive with/without A/B (Stage 5e/5f) is the coordinator's.

## Claude-play sweep

commit: `948933d6c` (benchmark d5 b200). seeds: 9500001..9500018 (disjoint from every suite tier; seed only --
--game-index does not vary a --claude-play game). games: 18. Driver: 6 Opus player agents x 3 games (Claude session).
Result: Claude faster 1 (s9500006, 4 vs 5), equal 17, slower 0. Rules / card-data violations: 0.
Full per-seed tally, notes and repro CSVs: `logs/elves/sweep-tally.md`.

Flags (each verified against cards.json + the rules skill; each became a fix branch -- see the coordinator checkpoints):
1. fd-diverge s7036 (breakpoint resume reset the Visionary cast-trigger ordinal): FIXED `wip/elves-fd` bac7df0a2.
2. Continuation casts' Visionary pay-{G} choice not pinned (ContPins): `wip/elves-contpin`.
3. Genesis Wave puts open no re-solve, s9500006 misses T4 at every budget incl. d3 b0: `wip/elves-wavebp`.
4. Immaculate Magistrate targets only the pre-cast battlefield (fixture mag4.json, lethal unfindable at b0):
   `wip/elves-tapt`.
5. Mana cluster (hasted scaled-dork credit 1; in-plan scaled yield; Marwyn prepay power; Lodge untap burst unused by
   the payer; Cavern `wild` paid a non-creature cost): `wip/elves-mana`.
6. Cosmetic labels (Clancaller "Merfolk", Visionary dig frame, Magistrate "X=0" / "+0/+0") and validate_line `act=`
   for a card in hand: in `wip/elves-contpin`.
Payer over-taps in human play (s9500002, s9500007, s9500014) are the standing mana-payment exemption, not flags.

flags: 4 unresolved
(2-6 open until their branches are merged onto wip/elves-analysis; this line is updated at the merge.)

## Stage 5 verification verdicts

(pending)

## Approved deferrals

* **Taunting Elf -- lure ("All creatures able to block this creature do so")**: inert in goldfish; the
  opponent's spawn bodies never block (`GoldFishRunner::PopulateOpponentSpawns`; no blocker path in
  Combat.cpp). USER, 2026-11-26: *"Yes, Taunting Elf's ability does nothing in goldfish."*

## Coordinator checkpoint (2026-11-26 ~07:00 UTC; superseded by the update below)

Work tree `.claude/worktrees/elves`, branch `wip/elves-analysis` (local commits only; nothing on the shared branch).
Done: Stage 1; Stage 2 (all 18 cards full; units 3964/3964; smoke 243/243, play-changed 0); ElvesProvider +
USER scaling rule (probe: 0/300 moved, units x0.948); Stage 4 baseline profile (`decks/Elves/Elves.profile.json`,
seed 26112601; discard evidence 3 sheds / 400 games, STATUS_QUO_OK); first `verify_deck.py` run
(`logs/elves/verify1.out`).

Uncommitted at checkpoint: `ElvesProvider::GenesisWavePutPicks` = put every legal reveal (+
`ImplementsRequiredHeuristic(GenesisWavePut)`), a dominance fold argued in the header comment (byte-identical to the
engine default on this pool). REBUILD FAILED rc=1 (log `build/scratch-elves-build-coord.log`: check for a real compile error vs the 9p alloc flake; retry). Then re-run `provider_audit.py --check` to confirm the Elves red flag clears,
then commit.

verify_deck gates open, in order:
1. **mismatch FAIL**: `[fd-diverge] seed=7036 job=s7001d5 gi=35 realized_win=6 predicted_win=5 proven_at_turn=3`.
   Repro: `MTG_FD_ORACLE=1 build/Release/mtg decks/Elves/Elves.cod --profile decks/Elves/Elves.profile.json --games 1
   --seed 7036 --game-index 5 --depth 5 --budget-ms 20 --threads 1 --log-dir build/scratch/elves-fd` (launched;
   output `build/scratch/elves-fd/{out,err}.txt`). REPRODUCED as a single game (realized 6 vs predicted 5, proven at T3;
   per-game log `build/scratch/elves-fd/20261126_063031_7036_game_0.json`), on the binary BEFORE the Wave policy. Root-cause: rollout vs executor disagreement on a new mechanic
   (suspects: Visionary pay/draw, multikick/squad faces, Marwyn power mana, Shepherd 5/5, Magistrate counters).
2. **provider_policy FAIL**: should clear with the uncommitted Wave policy.
3. **regression_tiers / suite FAIL**: add Elves to SMOKE / REGRESSION / OVERNIGHT (copy the worldfire entry-set
   shape: smoke d0 1000, d3 10 b10, d5 ~40 b20, elves2hg d5 20 b20; regression d0 1000, d3 15, d5 60 x2 seeds, 2hg
   30; overnight entry set d0 4x2000, d3 25 b10, d5 4x300 b20), screen budget-0 backstop blocks (30001..30541 step
   20, d3+d5, abandon_units 2000000, MTG_SLOW_GAME_MS=5000, ONE pooled batch; one block for REGRESSION, another for
   OVERNIGHT), `suite_gate.py --cost decks/Elves` (3x Hinata ~15,000 ms/game; probe says ~715 ms/game), run each tier
   `--deck=elves,elves2hg`, accept with the same filter, `test/check_gt_logs.py`.
4. **claude_sweep SKIP**: ~18 Sonnet player agents (Agent tool), seed base disjoint from the suite; verify flags
   on the main model; record `## Claude-play sweep` (commit / seeds / games / flags: N unresolved).
Then: 5b depth sweep d0/d3/d5, 5c2 `leaf_tiebreak_check.py`, the scaling-rule with/without A/B (held-out,
pooled), discard bucket policy draft (report only), Stage 6 report. Then land via `scripts/push_queue.py`.

Open items for the USER (non-blocking): Shepherd uncounterable/Dinosaur inert (PROVISIONAL); six inert-param
classifications in `audit_viewer_decisions.py`; Magistrate/Kessig/Merfolk tap-target can't hit a creature cast
earlier in the same plan (completeness gap, recommend a general fix); the k=2 hold-back cap and other provider
PROVISIONALs (ledger "Provider heuristics"); inherited SelesnyaLifegain judgement hooks incl. its discard buckets;
the Wave-X dedup collapse still live for Landfall / SelesnyaLifegain (fixing it moves their play).

## Coordinator checkpoint UPDATE (2026-11-26 ~07:05 UTC; superseded below)

* Wave put-all policy COMMITTED `948933d6c`; `provider_audit.py --check` no longer lists Elves (only the pre-existing
  Landfall / Selesnya flags remain). Unit `test_elves*` 7/7 in default / MTG_UNPRUNED=1 / MTG_HUMAN_PLAY=1.
* fd-diverge seed 7036 DIAGNOSED to the line level (trace `logs/elves/fd7036-bptrace.err`): the committed T4 plan
  `Forest + spells[Genesis Wave X=2, Llanowar Elves] bp[k0:Taunting Elf] bp_choice=0` is unaffordable after
  Llanowar + Visionary's pay-{G} draw (7 mana needed, 6 available). The fd-pred replay and the executor agree
  (Wave `[bp-pay] -> FAILED`, continuation casts Taunting Elf, opp 15/10/4, realised T6) -- only the SEARCH's own
  scoring of that line reaches T5. Not a cache (all *_VERIFY clean; caches off still reproduces). Delegated to an
  Opus agent in its own worktree (branch from 948933d6c; it will report a branch to cherry-pick). After the fix:
  rebuild, re-run verify_deck, re-smoke.
* Tier rows WRITTEN (uncommitted) in `test/regression_cases.sh`: smoke / regression / overnight ENTRY set +
  elves2hg canaries + DECK_FILE/DECK_PROF. Backstop screen (`logs/elves/backstop_screen_manifest.py`, 56 jobs, 3
  games each, budget 0, abandon 2M, MTG_SLOW_GAME_MS=5000): ALL 28 blocks finished every game, 0 SLOW-GAME, max job
  4.9 s -> Elves is cheap unbudgeted; 20-game confirmation of 30081 / 30101 / 30381 running
  (`build/scratch/elves-bs20/`). No GT accepted yet -- accept AFTER the fd fix lands.
* Claude-play sweep RUNNING: 6 Opus players x 3 games, seeds 9500001..9500018 (seed only; --game-index does not vary
  a --claude-play game), benchmark d5 b200, binary at 948933d6c.

## Coordinator checkpoint 3 (2026-11-26, pre-compaction) -- RESUME HERE

State of `wip/elves-analysis` (this worktree): HEAD `5cdf60ff0`. UNCOMMITTED: `test/regression_cases.sh` (all Elves rows:
smoke/regression/overnight entry set + elves2hg + backstop 30081 REGRESSION / 30381 OVERNIGHT, 20 games, d3+d5 b0) and
`test/suite_cost.json` (cost gate: elves 2082.4 ms/game worst case elves2hg_regression_d5_b20, 0.34x Hinata 6196.7 -> PASS).
Commit them WITH the accepted GT, after the fixes below are merged.

Claude-play sweep DONE (6 Opus players x 3, seeds 9500001..9500018, binary 948933d6c, bench d5 b200): 18 games, Claude
faster 1 (9500006), equal 17, slower 0; 0 rules/card-data violations. Full per-seed tally + repro CSVs:
`logs/elves/sweep-tally.md`. Flags -> four fix agents (all branch from `4fc09f9b3`, separate worktrees under
`.claude/worktrees/`, each will report a branch to cherry-pick; heavy work serialised on `build/heavy.lock`):
1. `wip/elves-fd` (worktree `agent-a8914a64a6ecea402`): fd-diverge s7036 FIXED in 4fc09f9b3 (breakpoint resume reset
   `scripted_mihail_trigger_ordinal` via WritePostApplyStatePins -> continuation re-read a consumed Visionary pay bit,
   drew an extra card). A continuation agent is finishing units / fd sweep / smoke / lockstep doc #20. OWED after it:
   the second defect -- pay-{G} variants on continuation casts are inert (ContPins lacks the pay choice).
2. `wip/elves-wavebp`: Genesis Wave puts open no re-solve (s9500006, misses T4 at every budget incl d3 b0).
3. `wip/elves-tapt`: TapTargetPump (Magistrate) targets only pre-cast battlefield (fixture
   `build/scratch/elves-sweep/mag4.json`, lethal unfindable at b0).
4. `wip/elves-mana`: hasted scaled-dork credit=1 (~46448); in-plan scaled yield -1; PP_SCALER_GROWN misses Marwyn;
   Lodge untap-burst unused by payer; Cavern `wild` paid a non-creature cost.
Not yet assigned (small, do after merges): cosmetic labels (Clancaller "Merfolk" SpellEffects.h:40037; Visionary dig
frame SpellEffects.h:~7995; Magistrate "X=0" menu + "+0/+0 until end of turn" log; kicked/unkicked Warcaller same
summary); validate_line `act=X` for X in hand falls back to a cast.

Next, in order: cherry-pick the four branches onto wip/elves-analysis (resolve conflicts; rebuild; units; Elves fd
sweep 0; smoke) -> record `## Claude-play sweep` (flags: 0 unresolved once fixed) -> run each tier
`--deck=elves,elves2hg`, accept same filter, `test/check_gt_logs.py`, commit rows+GT+suite_cost -> also accept the
smoke/regression keys of other decks the fixes moved (held-out A/B tables from the agents) -> 5b depth sweep, 5c2
leaf_tiebreak_check, scaling-rule held-out A/B, discard bucket draft, Stage 6 report -> push_queue land, CI watch.

## Coordinator checkpoint 4 (2026-11-26 ~12:00 UTC, pre-compaction) -- RESUME HERE

Uncommitted in this worktree still: test/regression_cases.sh (22 elves rows, all three tiers) and test/suite_cost.json
(Elves 0.34x Hinata, PASS). Commit them only with the accepted GT.

Fix branches (all worktrees under .claude/worktrees/; none pushed to the shared branch):

| branch | worktree | state |
|---|---|---|
| wip/elves-fd | agent-a8914a64a6ecea402 | DONE. bac7df0a2 (fix, reworded 4fc09f9b3) + 7274bbc1e (lockstep #20). Units 3965/3965; Elves fd sweep 200g d5 b20: 0 fd-diverge (pre-fix 1, s7036); smoke 243/243 unchanged. |
| wip/elves-contpin | elves-contpin | IN PROGRESS (2nd agent). Based 7274bbc1e. WIP 8c0f031b2 = (1) ContPins Visionary pay-pin (fails-before confirmed), (2) human-play guards for 2 old test_elves failures, (3) labels + validate_line act=. Remaining: units 3 modes, fd sweep both bins, smoke, per-deck A/B, lockstep #21, 3-way commit split. Handoff logs/elves/contpin-handoff.md. |
| wip/elves-wavebp | agent-a0234af8f175346b7 | IN PROGRESS (2nd agent). WIP 22bdb2433 = site-10 re-solve on battlefield puts; escape hatch MTG_BP_PUT_BATTLEFIELD (to be DELETED after A/B). Repro s9500006: T4 at d5 b200/d5 b0/d3 b0 (hatch off: T5). Remaining: units, fd, smoke, hatch-faithfulness smoke, A/B (Landfall, Selesnya cost), delete hatch, restore .claude/settings.local.json. Handoff logs/elves/wavebp-handoff.md. |
| wip/elves-tapt | agent-acfa1e28af7026501 | IN PROGRESS (2nd agent). 2822dd408 = Magistrate same-plan hand targets + provider hook (Merfolk/Landfall prunes). fd 0/0 both bins; Elves 200g digest identical; held-out A/B: no win-turn change any deck (Elves digest moved, Merfolk/Maze identical); 0 slower. Remaining: units. Open: Nylea same hole; land-target same-plan. Handoff logs/elves/tapt-handoff.md. |
| wip/elves-mana | agent-afb472d868144e4a1 | IN PROGRESS (2nd agent; 1st reached 534K). 1ef0bb101, eb79f7109, b1ba35a38 = 5 mana defects + squad/multikick count dropped in BP records (lockstep, renumber to #22), each behind default-ON lever. Units default 3971/3971. Smoke (no accept): 236/7 FAIL, all Stompy + Selesnya v2; searched slower 0; Selesnya d0 slower gi496, gi980 (5->6) need d8 b0. Remaining: UNPRUNED/HUMAN_PLAY units, A/B Elves/Stompy/Selesnya, Cavern side paths (rules bug), delete levers, re-validate. Handoff logs/elves/mana-handoff.md. |

Note: wip/elves-wavebp, -tapt, -mana are based on 4fc09f9b3 (pre-reword); cherry-pick their own commits only.
Lockstep numbering: #20 fd, #21 contpin, #22 mana (tell the merge).

Deferred, not assigned: Mihail pay variants not emitted for continuation plans (Merfolk Turns completeness gap);
Saproling Migration kicked/unkicked share a summary (kept: references contain it); Nylea same-plan target hole.

Next, unchanged from checkpoint 3: once all four in-progress branches report -> cherry-pick fd, contpin, wavebp,
tapt, mana onto wip/elves-analysis (resolve conflicts; rebuild; units 3 modes; Elves fd sweep 0; smoke) -> record
`## Claude-play sweep` -> tiers `--deck=elves,elves2hg` + accept + check_gt_logs, commit rows+GT+suite_cost -> accept
other decks' moved keys per the agents' A/B tables -> 5b depth sweep, 5c2 leaf_tiebreak_check, scaling-rule A/B,
discard bucket draft, Stage 6 report -> push_queue land, CI watch --interval 60.
