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

## Stage 5 verification verdicts

(pending)

## Approved deferrals

* **Taunting Elf -- lure ("All creatures able to block this creature do so")**: inert in goldfish; the
  opponent's spawn bodies never block (`GoldFishRunner::PopulateOpponentSpawns`; no blocker path in
  Combat.cpp). USER, 2026-11-26: *"Yes, Taunting Elf's ability does nothing in goldfish."*
